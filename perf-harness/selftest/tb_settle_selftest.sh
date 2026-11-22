#!/bin/bash
# Self-test for settle() in tools/tb_slice.sh (between-runs load wait, Overseer ruling 2026-11-21).
# Fake loadavg reads 3.0, then 1.5 after ~3 s. PASS: settle waits (>= 2 s), then the "run" starts at 1.5.
# Witness W1: settle removed from the loop line -> the run starts at 3.0 (must be detected as red).
# Witness W2: heavy process present -> settle hits the cap (TB_SETTLE_MAX=3) and reports "cap".
# Also checks that in the real loop settle is called before the slice-time and window guards.
cd "$(dirname "$0")/../.." || exit 2
S=${TB_SLICE:-tools/tb_slice.sh}   # scheduler under test
F=$(mktemp -d); trap 'rm -rf "$F"; kill $(jobs -p) 2>/dev/null' EXIT
fn=$(sed -n '/^heavy_ps()/,/^    sleep "\${TB_SETTLE_POLL/p' "$S")
loopline=$(grep -m1 'settle) before' "$S")
run_case() { # $1 = loop line to use; prints the load the "run" started at
  echo "3.00 2.90 2.80 1/500 1" > $F/la; ( sleep 3; echo "1.50 2.50 2.70 1/500 1" > $F/la ) &
  TB_LOADAVG_FILE=$F/la TB_HEAVY_CMD=true TB_SETTLE_POLL=1 bash -c "$fn"$'\n'"log() { echo \"\$*\"; }; tag=x"$'\n'"$1"$'\n'"echo start-load=\$(cut -d' ' -f1 $F/la)"; wait; }
fail=0
r=$(run_case "$loopline"); w=$(grep -o 'wait=[0-9]*' <<<"$r" | cut -d= -f2)
if grep -q 'start-load=1.50' <<<"$r" && [ "${w:-0}" -ge 2 ]; then echo "PASS waits ${w}s at 3.0, starts at 1.5"; else echo "FAIL settle: $r"; fail=1; fi
r=$(run_case ":")
if grep -q 'start-load=3.00' <<<"$r"; then echo "PASS witness W1 (wait removed) starts at 3.0 -> red"; else echo "FAIL witness W1: $r"; fail=1; fi
echo "1.00 1 1 1/1 1" > $F/la
r=$(TB_LOADAVG_FILE=$F/la TB_HEAVY_CMD="echo 180%:other" TB_SETTLE_POLL=1 TB_SETTLE_MAX=3 bash -c "$fn"$'\n'"settle" 2>&1)
if grep -q ' cap$' <<<"$r"; then echo "PASS witness W2 (heavy process) waits to the cap: $r"; else echo "FAIL W2: $r"; fail=1; fi
a=$(grep -n 'settle) before' "$S" | cut -d: -f1); b=$(grep -n 'SL_END - \$(date' "$S" | cut -d: -f1); c=$(grep -n 'secs_to_8) -lt 1320' "$S" | cut -d: -f1)
if [ -n "$a" ] && [ "$a" -lt "$b" ] && [ "$a" -lt "$c" ]; then echo "PASS settle (line $a) precedes the guards (lines $b, $c)"; else echo "FAIL guard order $a $b $c"; fail=1; fi
# yield_gap (Overseer ruling 2026-11-21, second form): WANT again after TB_GAP s, or earlier only when another worker
# claimed and since released AND nobody was refused a claim in the last 5 min. Fake audit log, TB_GAP=4, poll 1 s.
g=$(sed -n '/^yield_gap()/,/^    sleep 30; done; }/p' "$S" | sed 's/sleep 30/sleep 1/')
gap() { local t0 r; t0=$(date +%s); r=$(TB_AUDIT_LOG=$F/audit TB_GAP=4 bash -c "log() { echo \"\$*\"; }"$'\n'"$1"$'\n'"yield_gap"); echo "$(( $(date +%s) - t0 ))|$r"; }
now() { date '+%F %T'; }
alog() { : > $F/audit; ( sleep 1; for l in "$@"; do echo "$(now) $l" >> $F/audit; done ) & }
alog "RELEASE  by=engineperf was=engineperf" "CLAIM    by=itrzone id=x mins=45 what=y" "RELEASE  by=itrzone was=itrzone"
r=$(gap "$g"); wait; [ "${r%%|*}" -lt 4 ] && grep -q 'itrzone claimed and released' <<<"$r" && echo "PASS gap (b): other claimed+released, no refusal -> ends early (${r%%|*}s)" || { echo "FAIL gap (b): $r"; fail=1; }
alog "CLAIM    by=itrzone id=x mins=45 what=y" "CLAIM-RESERVED by=gammavr refused; EXCLUSIVE reservation held by engineperf" "RELEASE  by=itrzone was=itrzone"
r=$(gap "$g"); wait; [ "${r%%|*}" -ge 4 ] && grep -q 'gap: 4s passed' <<<"$r" && echo "PASS gap: refused claim in last 5 min -> waits the full gap (${r%%|*}s)" || { echo "FAIL gap/refused: $r"; fail=1; }
alog "CLAIM    by=itrzone id=x mins=45 what=y" "CLAIM-RESERVED by=gammavr refused; EXCLUSIVE reservation held by engineperf" "RELEASE  by=itrzone was=itrzone"
w=$(sed 's/if ! tail -n 5000 "$AL" | awk -v s="$cut" .*; then/if true; then/' <<<"$g")
r=$(gap "$w"); wait; [ "${r%%|*}" -lt 4 ] && echo "PASS witness W3 (refusal check removed) ends early -> red" || { echo "FAIL witness W3: $r"; fail=1; }
alog "CLAIM    by=itrzone id=x mins=45 what=y"
r=$(gap "$g"); wait; [ "${r%%|*}" -ge 4 ] && echo "PASS gap: other still holding (no release) -> waits the full gap" || { echo "FAIL gap/holding: $r"; fail=1; }
alog "CLAIM    by=engineperf id=x mins=30 what=y" "RELEASE  by=engineperf was=engineperf"
r=$(gap "$g"); wait; [ "${r%%|*}" -ge 4 ] && echo "PASS gap: only our own claim/release -> waits the full gap" || { echo "FAIL gap/self: $r"; fail=1; }
exit $fail
