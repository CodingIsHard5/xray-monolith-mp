#!/bin/bash
# Self-test for settle() in tools/tb_slice.sh (between-runs load wait, Overseer ruling 2026-11-21).
# Fake loadavg reads 3.0, then 1.5 after ~3 s. PASS: settle waits (>= 2 s), then the "run" starts at 1.5.
# Witness W1: settle removed from the loop line -> the run starts at 3.0 (must be detected as red).
# Witness W2: heavy process present -> settle hits the cap (TB_SETTLE_MAX=3) and reports "cap".
# Also checks that in the real loop settle is called before the slice-time and window guards.
cd "$(dirname "$0")/../.." || exit 2
F=$(mktemp -d); trap 'rm -rf "$F"; kill $(jobs -p) 2>/dev/null' EXIT
fn=$(sed -n '/^heavy_ps()/,/^    sleep "\${TB_SETTLE_POLL/p' tools/tb_slice.sh)
loopline=$(grep -m1 'settle) before' tools/tb_slice.sh)
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
a=$(grep -n 'settle) before' tools/tb_slice.sh | cut -d: -f1); b=$(grep -n 'SL_END - \$(date' tools/tb_slice.sh | cut -d: -f1); c=$(grep -n 'secs_to_8) -lt 1320' tools/tb_slice.sh | cut -d: -f1)
if [ -n "$a" ] && [ "$a" -lt "$b" ] && [ "$a" -lt "$c" ]; then echo "PASS settle (line $a) precedes the guards (lines $b, $c)"; else echo "FAIL guard order $a $b $c"; fail=1; fi
exit $fail
