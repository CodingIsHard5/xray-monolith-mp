#!/usr/bin/env bash
# S2 stage 4b part B driver (PREREG-S2-4b section 3): ten runs, one slice through the WANT queue, with the series script
# and bench of part B UNCHANGED, on tools/s4b_order.txt. Hash preflight: scripts and both builds, or nothing starts.
set -u
cd "$(dirname "$0")/.."
chk() { [ "$(sha256sum "$1" | cut -c1-16)" = "$2" ] || { echo "REFUSED: $1 is not $2"; exit 2; }; }
chk tools/s2_series_b.sh 75b2335b52bc662f; chk tools/s2_bench.sh 072af8edb67eab09; chk tools/s4b_order.txt e1789ccc6238b56d
chk run/bin/SimTick14.exe 93fff4b537711cb3; chk run/bin/SimTick15.exe 2ffd4f2bbd386e0b; chk run/bin/SimTick16.exe 91a8c0df50f61bbb
for t in $(grep -v '^#' tools/s4b_order.txt | cut -d'|' -f1); do [ "$t" = "${t,,}" ] || { echo "REFUSED: tag $t is not lowercase"; exit 2; }; [ -e out/$t ] && { echo "REFUSED: out/$t exists"; exit 2; }; done
[ "${1:-}" = "--preflight-only" ] && { echo "preflight ok"; exit 0; }
S2_ORDER=tools/s4b_order.txt S2_EXE_SHA=93fff4b537711cb38062f432c29551642e36ab852703b191727edb337e858f21 tools/s2_series_b.sh > out/s2_state/s4b.out 2>&1
tail -1 out/s2_state/series.log
