#!/usr/bin/env bash
# Desk test of t2_xray_em1.sh's one new step (the axr_options restore), ALONE: the lines between the runner's
# "# em1 (BEGIN axr restore)" and "# em1 (END axr restore)" markers are cut out of the runner file and run in a fresh
# bash with RUN pointing at a SCRATCH copy of the folder layout under em1/selftest-runs/<stamp>/axr/. The real run/ folder
# is only READ (its appdata/axr_options_play.ltx is the good case's source bytes); nothing is written under it.
#   bash em1/em1_axr_step_test.sh [<out dir under em1/selftest-runs>]
# Cases: good (source = the pinned bytes) -> the step continues (rc 0, the file copied, hash ok); missing source -> 7 with
# the REFUSED line; wrong bytes (one byte appended to the source) -> 7 with the REFUSED line; plus: the step's text is
# present exactly once and lies before the claim check and before any boot.
# Last line: EM1 AXR STEP TEST: PASS|FAIL (<n>/<m>)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; P="$(cd "$HERE/../../.." && pwd)"; RUNNER="$HERE/t2_xray_em1.sh"
REALSRC="$P/run/appdata/axr_options_play.ltx"
D="${1:-$HERE/selftest-runs/axr-$(date +%Y%m%d-%H%M%S)-$$}"
case "$D" in "$HERE"/selftest-runs/*) ;; *) echo "EM1 AXR STEP TEST: FAIL (out dir must be under $HERE/selftest-runs)"; exit 1 ;; esac
mkdir -p "$D" || { echo "EM1 AXR STEP TEST: FAIL (cannot make $D)"; exit 1; }
n=0; ok=0
chk() { n=$((n+1)); if [ "$1" = 0 ]; then ok=$((ok+1)); echo "ok   $2"; else echo "BAD  $2"; fi; }
STEP="$D/step.sh"
awk '/^# em1 \(BEGIN axr restore\)/{f=1} f{print} /^# em1 \(END axr restore\)/{f=0}' "$RUNNER" > "$STEP"
[ "$(grep -c '^# em1 (BEGIN axr restore)' "$RUNNER")" = 1 ] && [ "$(grep -c '^# em1 (END axr restore)' "$RUNNER")" = 1 ] && [ -s "$STEP" ]
chk $? "the step is marked exactly once in the runner and was cut out ($(wc -l < "$STEP") lines)"
lb=$(grep -n '^# em1 (BEGIN axr restore)' "$RUNNER" | cut -d: -f1); lq=$(grep -n 'quiet-required.sh" check' "$RUNNER" | head -1 | cut -d: -f1)
lt=$(grep -n 'tools/tb_run.sh' "$RUNNER" | head -1 | cut -d: -f1); lr=$(grep -n '^cd "$(dirname "$0")/.."; P=$PWD; RUN=$P/run' "$RUNNER" | cut -d: -f1)
le=$(grep -n '^# em1 (END axr restore)' "$RUNNER" | cut -d: -f1)
# the first line after the claim check that writes to run/ or the shm folder (run_noguard, rm/cp/: > on $RUN or $SHM)
lw=$(awk -v q="$lq" -v b="${lb:-0}" -v e="${le:-0}" 'NR > q && (NR < b || NR > e) && !/^ *#/ && /run_noguard|rm -f "\$SHM|rm -f "\$RUN|cp [^|]*"\$(RUN|SHM)|> "\$(SHM|RUN)/ {print NR; exit}' "$RUNNER")
[ -n "$lb" ] && [ -n "$lq" ] && [ -n "$lt" ] && [ -n "$lr" ] && [ -n "$le" ] && [ -n "$lw" ] && [ "$lr" -lt "$lq" ] && [ "$lq" -lt "$lb" ] && [ "$le" -lt "$lw" ] && [ "$le" -lt "$lt" ]
chk $? "order: RUN set (line $lr) < claim check (line $lq) < restore step (lines $lb..$le) < first other write to run/ or shm (line $lw) and < first boot (line $lt)"
grep -q 'rm ' "$STEP"; [ $? != 0 ]; chk $? "the step contains no rm"

run_case() {   # $1 name: makes $D/$1/run/{appdata,gamedata/configs}, runs the step, prints rc
  local R="$D/$1/run"; mkdir -p "$R/appdata" "$R/gamedata/configs" "$D/$1/out"
  printf 'version = 0 (stale, rewritten by an exe)\n' > "$R/gamedata/configs/axr_options.ltx"
  case "$1" in
    good) cp "$REALSRC" "$R/appdata/axr_options_play.ltx" ;;
    missing) : ;;
    wrong) cp "$REALSRC" "$R/appdata/axr_options_play.ltx"; printf 'x' >> "$R/appdata/axr_options_play.ltx" ;;
  esac
  ( RUN="$R"; O="$D/$1/out"; log() { echo "$(date -u '+%F %T.%3N')Z $*" | tee -a "$O/t2_xray.log"; }
    . "$STEP"; echo "CONTINUED past the step"; exit 0 ) > "$D/$1/stdout.txt" 2>&1
  echo $?
}
[ -f "$REALSRC" ] || { echo "EM1 AXR STEP TEST: FAIL (no $REALSRC to read the good bytes from)"; exit 1; }
WANT=27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29
rc=$(run_case good)
[ "$rc" = 0 ] && grep -q '^CONTINUED' "$D/good/stdout.txt" && [ "$(sha256sum < "$D/good/run/gamedata/configs/axr_options.ltx" | cut -c1-64)" = "$WANT" ] && grep -q 'axr_options restored' "$D/good/out/t2_xray.log"
chk $? "good: rc $rc, continued, configs file now has the pinned hash"
rc=$(run_case missing)
[ "$rc" = 7 ] && ! grep -q '^CONTINUED' "$D/missing/stdout.txt" && [ "$(grep -c 'REFUSED: axr_options restore: source .* missing' "$D/missing/stdout.txt")" = 1 ]
chk $? "missing source: rc $rc, one REFUSED line: $(grep -o 'REFUSED.*' "$D/missing/stdout.txt" | cut -c1-90)"
rc=$(run_case wrong)
[ "$rc" = 7 ] && ! grep -q '^CONTINUED' "$D/wrong/stdout.txt" && [ "$(grep -c 'REFUSED: axr_options restore: sha256 of source' "$D/wrong/stdout.txt")" = 1 ]
chk $? "wrong source bytes: rc $rc, one REFUSED line: $(grep -o 'REFUSED.*' "$D/wrong/stdout.txt" | cut -c1-90)"
[ "$(printf 'version = 0 (stale, rewritten by an exe)\n' | sha256sum | cut -c1-64)" = "$(sha256sum < "$D/wrong/run/gamedata/configs/axr_options.ltx" | cut -c1-64)" ]
chk $? "wrong source bytes: the destination is byte-identical to before (not overwritten)"
bash -n "$RUNNER"; chk $? "bash -n t2_xray_em1.sh"
[ "$(sha256sum < "$REALSRC" | cut -c1-64)" = "$WANT" ]; chk $? "the real source file is unchanged (read only)"
[ $ok = $n ] && r=PASS || r=FAIL
echo "EM1 AXR STEP TEST: $r ($ok/$n) dir $D"
[ $r = PASS ]
