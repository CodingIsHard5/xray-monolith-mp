#!/usr/bin/env bash
# S2 part B, the six remaining runs (prereg section 11), two slices through the WANT queue: three fixed-step runs, then
# the three 4-core runs. Runs the amended tools/s2_series_b.sh on tools/s2_order_b.txt, lines 1-3 and 4-6.
set -u
cd "$(dirname "$0")/.."; ST=out/s2_state
[ "$(sha256sum tools/s2_order_b.txt | cut -c1-8)" = c656b5e4 ] && [ "$(sha256sum tools/s2_series_b.sh | cut -c1-8)" = "${S2_SERIES_SHA8:?}" ] || { echo "REFUSED: pinned series files differ"; exit 2; }
grep -v '^#' tools/s2_order_b.txt | grep . > $ST/order_b_all.txt; [ "$(wc -l < $ST/order_b_all.txt)" = 6 ] || { echo "REFUSED: not 6 runs"; exit 2; }
sed -n 1,3p $ST/order_b_all.txt > $ST/order_b1.txt; sed -n 4,6p $ST/order_b_all.txt > $ST/order_b2.txt
for p in 1 2; do
  echo "$(date '+%F %T') slice b$p starts"
  S2_ORDER=$ST/order_b$p.txt tools/s2_series_b.sh > $ST/partb$p.out 2>&1
  tail -1 $ST/series.log
  tail -1 $ST/series.log | grep -q "list finished" || { echo "STOP: slice b$p did not finish its list"; exit 1; }
done
echo "$(date '+%F %T') all remaining runs finished: $(grep -c . $ST/done.txt) runs in done.txt"
