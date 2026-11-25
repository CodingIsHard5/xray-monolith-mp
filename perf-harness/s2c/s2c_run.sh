#!/usr/bin/env bash
# S2-C E1 driver (PREREG-S2C-gate-overhead section 5): ten runs, one slice through the WANT queue, with the series
# script and bench of part B UNCHANGED (so the bench call is part B's by construction), on tools/s2c_order.txt.
# Flag preflight: the two scripts and the build must carry the pinned hashes, or nothing starts.
set -u
cd "$(dirname "$0")/.."
chk() { [ "$(sha256sum "$1" | cut -c1-16)" = "$2" ] || { echo "REFUSED: $1 is not $2"; exit 2; }; }
chk tools/s2_series_b.sh 75b2335b52bc662f; chk tools/s2_bench.sh 072af8edb67eab09; chk tools/s2c_order.txt dfa2f0db6dee7631
chk run/bin/SimTick14.exe 93fff4b537711cb3
for t in $(grep -v '^#' tools/s2c_order.txt | cut -d'|' -f1); do [ "$t" = "${t,,}" ] || { echo "REFUSED: tag $t is not lowercase"; exit 2; }; [ -e out/$t ] && { echo "REFUSED: out/$t exists"; exit 2; }; done
[ "${1:-}" = "--preflight-only" ] && { echo "preflight ok"; exit 0; }
S2_ORDER=tools/s2c_order.txt S2_EXE_SHA=93fff4b537711cb38062f432c29551642e36ab852703b191727edb337e858f21 tools/s2_series_b.sh > out/s2_state/s2c.out 2>&1
tail -1 out/s2_state/series.log
