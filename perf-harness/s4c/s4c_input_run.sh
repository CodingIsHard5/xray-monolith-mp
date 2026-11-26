#!/usr/bin/env bash
# S2 stage 4c part C (input on) driver (PREREG-S2-4c): one slice through the WANT queue with tools/s4c_series.sh (the part B series
# script with the bench flags settable and a wait on Caden's launcher lock) on tools/s4c_input_order.txt. Hash preflight or nothing starts.
set -u
cd "$(dirname "$0")/.."
chk() { [ "$(sha256sum "$1" | cut -c1-16)" = "$2" ] || { echo "REFUSED: $1 is not $2"; exit 2; }; }
chk tools/s4c_series.sh 6539d1ceacb0a4b2; chk tools/s2_bench.sh 072af8edb67eab09; chk tools/s4c_input_order.txt f7753b2a3484229c
chk run/bin/SimTick14.exe 93fff4b537711cb3; chk run/bin/SimTick16.exe 91a8c0df50f61bbb; chk run/bin/SimTick17.exe cb5f2f8706ed89e5
for t in $(grep -v '^#' tools/s4c_input_order.txt | cut -d'|' -f1); do [ "$t" = "${t,,}" ] || { echo "REFUSED: tag $t is not lowercase"; exit 2; }; [ -e out/$t ] && { echo "REFUSED: out/$t exists"; exit 2; }; [ -e out/${t}_rr ] && { echo "REFUSED: out/${t}_rr exists"; exit 2; }; done
[ "${1:-}" = "--preflight-only" ] && { echo "preflight ok"; exit 0; }
S2_BENCH_FLAGS="-perf_nopresent -perf_slow 20" S2_ORDER=tools/s4c_input_order.txt S2_EXE_SHA=93fff4b537711cb38062f432c29551642e36ab852703b191727edb337e858f21 tools/s4c_series.sh > out/s2_state/s4c_input.out 2>&1
tail -1 out/s2_state/series.log
