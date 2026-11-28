#!/usr/bin/env bash
# Mutant self-test of common/box_preflight.sh, and of s4d2_run.sh's refusal to start without it. FIXTURES ONLY.
#   nice -n 19 bash wt-render-split/perf-harness/common/box_preflight_selftest.sh
# Needs no box claim and starts no game. Every input of box_preflight.sh is pointed at a fixture in this run's own
# output folder (common/selftest-runs/<stamp>-<pid>/fx): the play lock, the /proc stand-in, the title checker, pgrep,
# quiet-required.sh, the claim-file folder, the X root and the run tree. The fixtures are small shell scripts that only
# exit with a code (and print a canned `check` text). The test never creates or removes the real lock, never calls
# the real claim tool (no check, want, claim or release), never runs the real title checker or pgrep, never sends a
# signal. s4d2_run.sh is run only in its `--preflight-only` box mode (source + checks + exit) and once as
# `probe --preflight-only` (start checks only, exits before any state folder, queue entry or claim); its shared file
# is moved aside through the S4D2_BOX_LIB override, never by moving the real file.
# Prints one line per case and "BOX SELFTEST: PASS|FAIL (n/N)".
set -u
C=$(cd "$(dirname "$0")" && pwd)
LIB=$C/box_preflight.sh; RUNNER=$C/../s4d2/s4d2_run.sh; DETRUN=$(cd "$C/../../.." && pwd)/tools/det_run.sh
OUT=$C/selftest-runs/$(date +%Y%m%d-%H%M%S)-$$
mkdir -p "$OUT" || { echo "cannot make $OUT"; exit 2; }
FX=$OUT/fx; mkdir -p "$FX/bin" "$FX/proc/4242" "$FX/proc/5151" "$FX/proc/5252" "$FX/xroot/.X11-unix" "$FX/run/bin" || exit 2
ln -s /fixture/not/a/real/wine64-preloader "$FX/proc/5151/exe"   # dangling on purpose: only its name is read
ln -s /fixture/not/a/real/bash "$FX/proc/5252/exe"
LIBSHA=$(sha256sum < "$LIB" | cut -d' ' -f1)

# ---- fixture tools (each only exits, or prints a canned text and exits) ----
mk() { printf '#!/bin/sh\n%s\n' "$2" > "$FX/bin/$1"; chmod +x "$FX/bin/$1"; }
mk title_none   'exit 1'
mk title_play   'exit 0'
mk title_fail   'exit 3'
mk pgrep_clean  'exit 1'
mk pgrep_sober  'case " $* " in *" sober "*) exit 0 ;; esac; exit 1'
mk pgrep_engine 'case "$1" in -f) echo 5151; exit 0 ;; esac; exit 1'
mk pgrep_watcher 'case "$1" in -f) echo 5252; exit 0 ;; esac; exit 1'
mk pgrep_garbage 'case "$1" in -f) echo abc; exit 0 ;; esac; exit 1'
mk pgrep_err    'exit 2'
H='2026-11-27 22:00:00'
mk qr_clear     'echo clear; exit 1'
mk qr_other_job "echo 'HELD: $H engineperf 45 headless Xvfb GAMMA A-Life equivalence series s4d2 (series) s4d2-series-1-1'; exit 0"
mk qr_mine      "echo 'HELD: $H engineperf 45 headless series s4d2 (probe) TOKMINE'; exit 0"
mk qr_handoff   "echo 'HELD: $H engineperf 5 EXCLUSIVE handoff pre-claim (claim within 5 min)'; exit 0"
mk qr_slot_ours "printf '%s\n' 'SLOT2: $H engineperf 45 headless other engineperf job' 'HELD: $H rootsim 90 headless rootsim game'; exit 0"
mk qr_other_wk  "echo 'HELD: $H rootsim 90 headless rootsim full game'; exit 0"
mk qr_mine_and_other "printf '%s\n' 'HELD: $H engineperf 45 headless series s4d2 (probe) TOKMINE' 'SLOT2: $H engineperf 30 headless another engineperf job'; exit 0"
mk qr_fail      'exit 2'
mk qr_held_noline 'echo clear; exit 0'
for d in q_clean q_excl q_slot_excl q_expired q_unread q_none; do mkdir -p "$FX/$d"; done
echo "$H rootsim 90 headless: rootsim full game" > "$FX/q_clean/QUIET-REQUIRED"
echo "$H rootsim 90 headless: rootsim full game (Ollama GPU) EXCLUSIVE" > "$FX/q_excl/QUIET-REQUIRED"
echo "$H rootsim 90 headless: rootsim full game" > "$FX/q_slot_excl/QUIET-REQUIRED"
echo "$H other 30 EXCLUSIVE timing" > "$FX/q_slot_excl/QUIET-REQUIRED-2"
echo "$H rootsim 90 headless: rootsim full game" > "$FX/q_expired/QUIET-REQUIRED"
echo "$H old 30 EXCLUSIVE timing" > "$FX/q_expired/QUIET-REQUIRED-3.expired"
mkdir -p "$FX/q_unread/QUIET-REQUIRED"          # a folder where the claim file should be: not a readable file
echo 4242 > "$FX/lock_live"; echo 999999 > "$FX/lock_dead"; echo abc > "$FX/lock_garbage"; : > "$FX/lock_empty"
printf '4242\r\n' > "$FX/lock_live_crlf"
printf 'BOX_PREFLIGHT_VERSION=1\n' > "$FX/lib_nomarker.sh"
printf 'BOX_PREFLIGHT_MARKER=engineperf-box-preflight-v1\nBOX_PREFLIGHT_VERSION=0\n' > "$FX/lib_oldversion.sh"
{ cat "$LIB"; echo 'unset -f box_ok_claims'; } > "$FX/lib_nofunc.sh"
: > "$FX/x141sock"

# the clean environment: EVERY input overridden (a missing override would read a real input)
CLEAN=(BOX_PLAY_LOCK="$FX/no_lock" BOX_PROC="$FX/proc" BOX_TITLE_CHECK="$FX/bin/title_none" BOX_PGREP="$FX/bin/pgrep_clean"
       BOX_QR="$FX/bin/qr_clear" BOX_QUIET_DIR="$FX/q_clean" BOX_XROOT="$FX/xroot" BOX_RUN="$FX/run")
# refuse to go on if a fixture path could be a real one
for kv in "${CLEAN[@]}"; do case "${kv#*=}" in "$OUT"/*) ;; *) echo "fixture outside $OUT: $kv"; exit 2 ;; esac; done

n=0; ok=0
case_() {   # <name> <want rc> <must text ('' = none)> <mustnot text ('' = none)> <env...> -- <command words...>
  local name=$1 want=$2 must=$3 mustnot=$4 envs=() out r good=1; shift 4
  while [ "$1" != -- ]; do envs+=("$1"); shift; done; shift
  out=$(env "${CLEAN[@]}" "${envs[@]}" "$@" 2>&1 </dev/null); r=$?
  [ "$r" = "$want" ] || good=0
  [ -z "$must" ] || printf '%s\n' "$out" | grep -qF -- "$must" || good=0
  [ -z "$mustnot" ] || ! printf '%s\n' "$out" | grep -qF -- "$mustnot" || good=0
  n=$((n + 1)); [ $good = 1 ] && ok=$((ok + 1))
  printf '%-4s %-36s rc %s (want %s) | %s\n' "$([ $good = 1 ] && echo ok || echo BAD)" "$name" "$r" "$want" \
    "$(printf '%s\n' "$out" | grep -v '^BOX OVERRIDES' | tr '\n' ' ' | cut -c1-150)"
  printf '== %s rc %s\n%s\n' "$name" "$r" "$out" >> "$OUT/cases.log"
}
ALL=(bash -c '. "$0"; box_preflight_all "$@"' "$LIB")        # + <class> <display> <worker> [<token>]
ONE=(bash -c '. "$0"; f=$1; shift; "$f" "$@"; r=$?; echo "WHY: $BOX_WHY"; exit $r' "$LIB")

# ---- the checks one by one, through box_preflight_all (refusal BY NAME) ----
case_ clean_all_overridden 0 "BOX OVERRIDES: BOX_PLAY_LOCK BOX_PROC BOX_TITLE_CHECK BOX_PGREP BOX_QR BOX_QUIET_DIR BOX_XROOT BOX_RUN" "BOX REFUSED" -- "${ALL[@]}" shared :141 engineperf
case_ lock_live            1 "BOX REFUSED: play_lock: Caden's play session is running" "" BOX_PLAY_LOCK="$FX/lock_live" -- "${ALL[@]}" shared :141 engineperf
case_ lock_live_crlf       1 "BOX REFUSED: play_lock: Caden's play session" "" BOX_PLAY_LOCK="$FX/lock_live_crlf" -- "${ALL[@]}" shared :141 engineperf
case_ lock_garbage         2 "BOX REFUSED: play_lock:" "" BOX_PLAY_LOCK="$FX/lock_garbage" -- "${ALL[@]}" shared :141 engineperf
case_ lock_empty           2 "holds no pid" "" BOX_PLAY_LOCK="$FX/lock_empty" -- "${ALL[@]}" shared :141 engineperf
case_ lock_is_folder       2 "not a readable file" "" BOX_PLAY_LOCK="$FX/proc" -- "${ALL[@]}" shared :141 engineperf
case_ lock_stale_accepted  0 "" "BOX REFUSED" BOX_PLAY_LOCK="$FX/lock_dead" -- "${ALL[@]}" shared :141 engineperf
case_ title_playing        1 "BOX REFUSED: protected_title: a protected title is playing" "" BOX_TITLE_CHECK="$FX/bin/title_play" -- "${ALL[@]}" shared :141 engineperf
case_ title_check_fails    2 "BOX REFUSED: protected_title: the title checker exited 3" "" BOX_TITLE_CHECK="$FX/bin/title_fail" -- "${ALL[@]}" shared :141 engineperf
case_ title_check_missing  2 "BOX REFUSED: protected_title: the title checker" "" BOX_TITLE_CHECK="$FX/bin/no_such_tool" -- "${ALL[@]}" shared :141 engineperf
case_ roblox_running       1 "BOX REFUSED: roblox: Sober (Roblox) is running" "engine_running" BOX_PGREP="$FX/bin/pgrep_sober" -- "${ALL[@]}" shared :141 engineperf
case_ pgrep_fails          2 "BOX REFUSED: engine_running: pgrep -f" "" BOX_PGREP="$FX/bin/pgrep_err" -- "${ALL[@]}" shared :141 engineperf
case_ engine_running       1 "BOX REFUSED: engine_running: an engine from" "roblox" BOX_PGREP="$FX/bin/pgrep_engine" -- "${ALL[@]}" shared :141 engineperf
case_ engine_watcher_ignored 0 "" "BOX REFUSED" BOX_PGREP="$FX/bin/pgrep_watcher" -- "${ALL[@]}" shared :141 engineperf
case_ engine_pgrep_garbage 2 "BOX REFUSED: engine_running: pgrep printed 'abc'" "" BOX_PGREP="$FX/bin/pgrep_garbage" -- "${ALL[@]}" shared :141 engineperf
case_ claim_other_job      1 "BOX REFUSED: own_claim: a live claim under 'engineperf'" "" BOX_QR="$FX/bin/qr_other_job" -- "${ALL[@]}" shared :141 engineperf
case_ claim_other_job_tok  1 "BOX REFUSED: own_claim:" "" BOX_QR="$FX/bin/qr_other_job" -- "${ALL[@]}" shared :141 engineperf TOKMINE
case_ claim_mine_by_token  0 "" "BOX REFUSED" BOX_QR="$FX/bin/qr_mine" -- "${ALL[@]}" shared :141 engineperf TOKMINE
case_ claim_mine_no_token  1 "BOX REFUSED: own_claim:" "" BOX_QR="$FX/bin/qr_mine" -- "${ALL[@]}" shared :141 engineperf
case_ claim_handoff_pre    0 "" "BOX REFUSED" BOX_QR="$FX/bin/qr_handoff" -- "${ALL[@]}" exclusive :141 engineperf
case_ claim_ours_in_slot   1 "BOX REFUSED: own_claim:" "" BOX_QR="$FX/bin/qr_slot_ours" -- "${ALL[@]}" shared :141 engineperf
case_ claim_other_worker   0 "" "BOX REFUSED" BOX_QR="$FX/bin/qr_other_wk" -- "${ALL[@]}" shared :141 engineperf
case_ claim_check_fails    2 "BOX REFUSED: own_claim: 'quiet-required.sh check' exited 2" "" BOX_QR="$FX/bin/qr_fail" -- "${ALL[@]}" shared :141 engineperf
case_ claim_held_no_line   2 "prints no HELD line" "" BOX_QR="$FX/bin/qr_held_noline" -- "${ALL[@]}" shared :141 engineperf
case_ exclusive_named      1 "BOX REFUSED: exclusive:" "" BOX_QUIET_DIR="$FX/q_excl" -- "${ALL[@]}" shared :141 engineperf
case_ exclusive_in_slot    1 "QUIET-REQUIRED-2 names EXCLUSIVE" "" BOX_QUIET_DIR="$FX/q_slot_excl" -- "${ALL[@]}" shared :141 engineperf
case_ exclusive_expired_ok 0 "" "BOX REFUSED" BOX_QUIET_DIR="$FX/q_expired" -- "${ALL[@]}" shared :141 engineperf
case_ exclusive_unreadable 2 "BOX REFUSED: exclusive:" "" BOX_QUIET_DIR="$FX/q_unread" -- "${ALL[@]}" shared :141 engineperf
case_ exclusive_no_files   0 "" "BOX REFUSED" BOX_QUIET_DIR="$FX/q_none" -- "${ALL[@]}" shared :141 engineperf
case_ exclusive_class_skip 0 "" "BOX REFUSED" BOX_QUIET_DIR="$FX/q_excl" -- "${ALL[@]}" exclusive :141 engineperf
case_ display_0            1 "BOX REFUSED: display: display :0 is Caden's screen" "" -- "${ALL[@]}" shared :0 engineperf
case_ display_00           1 "Caden's screen" "" -- "${ALL[@]}" shared :00 engineperf
case_ display_host0        2 "BOX REFUSED: display: display 'localhost:0'" "" -- "${ALL[@]}" shared localhost:0 engineperf
case_ display_screen0      2 "BOX REFUSED: display:" "" -- "${ALL[@]}" shared :0.0 engineperf
case_ class_bogus          2 "BOX REFUSED: claim_class:" "" -- "${ALL[@]}" EXCL :141 engineperf
case_ two_refusals_named   1 "BOX REFUSED: protected_title:" "" BOX_PLAY_LOCK="$FX/lock_live" BOX_TITLE_CHECK="$FX/bin/title_play" -- "${ALL[@]}" shared :141 engineperf
case_ two_refusals_lock    1 "BOX REFUSED: play_lock:" "" BOX_PLAY_LOCK="$FX/lock_live" BOX_TITLE_CHECK="$FX/bin/title_play" -- "${ALL[@]}" shared :141 engineperf
# ---- the helpers outside box_preflight_all ----
case_ display_free_ok      0 "WHY: display :141 free" "" -- "${ONE[@]}" box_ok_display_free :141
cp "$FX/x141sock" "$FX/xroot/.X11-unix/X141"
case_ display_socket       1 "WHY: display :141 exists" "" -- "${ONE[@]}" box_ok_display_free :141
case_ token_held           0 "WHY: a claim under 'engineperf' carries TOKMINE" "" BOX_QR="$FX/bin/qr_mine" -- "${ONE[@]}" box_claim_has_token engineperf TOKMINE
case_ token_not_held       1 "WHY: no claim under 'engineperf' carries TOKMINE" "" BOX_QR="$FX/bin/qr_other_job" -- "${ONE[@]}" box_claim_has_token engineperf TOKMINE
case_ token_check_fails    2 "cannot tell" "" BOX_QR="$FX/bin/qr_fail" -- "${ONE[@]}" box_claim_has_token engineperf TOKMINE

# ---- s4d2_run.sh: refuses to start without the real shared file; box-only mode answers by name ----
case_ driver_lib_missing   3 "REFUSED: box_preflight: missing file $FX/moved_aside.sh" "" S4D2_BOX_LIB="$FX/moved_aside.sh" -- bash "$RUNNER" --preflight-only
case_ driver_lib_unpinned  3 "is not the pinned file" "" S4D2_BOX_LIB="$FX/lib_nomarker.sh" -- bash "$RUNNER" --preflight-only
case_ driver_lib_nomarker  3 "did not set the marker" "" S4D2_BOX_LIB="$FX/lib_nomarker.sh" S4D2_BOX_LIB_SHA="$(sha256sum < "$FX/lib_nomarker.sh" | cut -d' ' -f1)" -- bash "$RUNNER" --preflight-only
case_ driver_lib_version   3 "is version '0'" "" S4D2_BOX_LIB="$FX/lib_oldversion.sh" S4D2_BOX_LIB_SHA="$(sha256sum < "$FX/lib_oldversion.sh" | cut -d' ' -f1)" -- bash "$RUNNER" --preflight-only
case_ driver_lib_nofunc    3 "does not define box_ok_claims" "" S4D2_BOX_LIB="$FX/lib_nofunc.sh" S4D2_BOX_LIB_SHA="$(sha256sum < "$FX/lib_nofunc.sh" | cut -d' ' -f1)" -- bash "$RUNNER" --preflight-only
case_ driver_lib_unreadable 3 "is not a readable file" "" S4D2_BOX_LIB="$FX/proc" -- bash "$RUNNER" --preflight-only
case_ driver_clean         0 "box preflight ok (box_preflight.sh v1)" "BOX REFUSED" S4D2_BOX_LIB="$LIB" -- bash "$RUNNER" --preflight-only
case_ driver_real_lib_pin  0 "box preflight ok" "" -- bash "$RUNNER" --preflight-only
case_ driver_lock          1 "BOX REFUSED: play_lock:" "" BOX_PLAY_LOCK="$FX/lock_live" -- bash "$RUNNER" --preflight-only
case_ driver_title_fails   2 "BOX REFUSED: protected_title:" "" BOX_TITLE_CHECK="$FX/bin/title_fail" -- bash "$RUNNER" --preflight-only
case_ driver_other_claim   1 "BOX REFUSED: own_claim:" "" BOX_QR="$FX/bin/qr_other_job" -- bash "$RUNNER" --preflight-only
case_ driver_exclusive     1 "BOX REFUSED: exclusive:" "" BOX_QUIET_DIR="$FX/q_excl" -- bash "$RUNNER" --preflight-only
case_ driver_display0      2 "REFUSED: BENCH_DISPLAY ':0' is not :141" "" BENCH_DISPLAY=:0 -- bash "$RUNNER" --preflight-only
case_ driver_full_preflight_box 2 "REFUSED (box): BOX REFUSED: play_lock:" "" BOX_PLAY_LOCK="$FX/lock_live" -- bash "$RUNNER" probe --preflight-only

# ---- tools/det_run.sh --preflight-only (source + checks + exit: no out folder, claim, Xvfb, Wine or engine) ----
DR=(bash "$DETRUN" --preflight-only Fixture.exe st_fixture)
case_ detrun_lib_missing     3 "REFUSED: box_preflight: missing file $FX/moved_aside.sh" "" DET_BOX_LIB="$FX/moved_aside.sh" -- "${DR[@]}"
case_ detrun_lib_unpinned    3 "is not the pinned file" "" DET_BOX_LIB="$FX/lib_nomarker.sh" -- "${DR[@]}"
case_ detrun_lib_nomarker    3 "did not set the marker" "" DET_BOX_LIB="$FX/lib_nomarker.sh" DET_BOX_LIB_SHA="$(sha256sum < "$FX/lib_nomarker.sh" | cut -d' ' -f1)" -- "${DR[@]}"
case_ detrun_lib_version     3 "is version '0'" "" DET_BOX_LIB="$FX/lib_oldversion.sh" DET_BOX_LIB_SHA="$(sha256sum < "$FX/lib_oldversion.sh" | cut -d' ' -f1)" -- "${DR[@]}"
case_ detrun_lib_unreadable  3 "is not a readable file" "" DET_BOX_LIB="$FX/proc" -- "${DR[@]}"
case_ detrun_clean_selfclaim 0 "det_run preflight ok (shared, :77, engineperf)" "REFUSED" -- "${DR[@]}"
case_ detrun_noclaim_no_token 2 "DET_NOCLAIM=1 needs DET_BOX_TOKEN" "" DET_NOCLAIM=1 -- "${DR[@]}"
case_ detrun_noclaim_not_held 2 "no live claim under 'engineperf' carries 'TOKMINE'" "" DET_NOCLAIM=1 DET_BOX_TOKEN=TOKMINE -- "${DR[@]}"
case_ detrun_noclaim_mine    0 "det_run preflight ok" "REFUSED" DET_NOCLAIM=1 DET_BOX_TOKEN=TOKMINE BOX_QR="$FX/bin/qr_mine" -- "${DR[@]}"
case_ detrun_noclaim_other   1 "REFUSED: BOX REFUSED: own_claim:" "" DET_NOCLAIM=1 DET_BOX_TOKEN=TOKMINE BOX_QR="$FX/bin/qr_mine_and_other" -- "${DR[@]}"
case_ detrun_selfclaim_other 1 "REFUSED: BOX REFUSED: own_claim:" "" BOX_QR="$FX/bin/qr_mine" -- "${DR[@]}"
case_ detrun_lock            1 "REFUSED: BOX REFUSED: play_lock:" "" BOX_PLAY_LOCK="$FX/lock_live" -- "${DR[@]}"
case_ detrun_title_fails     2 "REFUSED: BOX REFUSED: protected_title:" "" BOX_TITLE_CHECK="$FX/bin/title_fail" -- "${DR[@]}"
case_ detrun_roblox          1 "REFUSED: BOX REFUSED: roblox:" "" BOX_PGREP="$FX/bin/pgrep_sober" -- "${DR[@]}"
case_ detrun_engine_before   1 "REFUSED: BOX REFUSED: engine_running:" "" BOX_PGREP="$FX/bin/pgrep_engine" -- "${DR[@]}"
case_ detrun_exclusive       1 "REFUSED: BOX REFUSED: exclusive:" "" BOX_QUIET_DIR="$FX/q_excl" -- "${DR[@]}"
case_ detrun_exclusive_class 0 "det_run preflight ok (exclusive" "" DET_BOX_CLASS=exclusive BOX_QUIET_DIR="$FX/q_excl" -- "${DR[@]}"
case_ detrun_display0        1 "REFUSED: BOX REFUSED: display: display :0 is Caden's screen" "" BENCH_DISPLAY=:0 -- "${DR[@]}"
case_ detrun_class_bogus     2 "REFUSED: DET_BOX_CLASS 'EXCL'" "" DET_BOX_CLASS=EXCL -- "${DR[@]}"

echo "shared file sha256 $LIBSHA"
echo "BOX SELFTEST: $([ $ok = $n ] && echo PASS || echo FAIL) ($ok/$n)"
echo "output folder: $OUT ($(du -sh "$OUT" | cut -f1))"
