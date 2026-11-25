#!/usr/bin/env bash
# Pre-flight L series (prereg section 57 + 57a): waits until R3 arm A's X-side runner has ended, then runs the five
# boots in the registered order, ALL of them even if one fails (Overseer, 2026-11-24). Scores nothing.
set -u
cd "$(dirname "$0")/.."; L=out/t3pfL_series.log; log() { echo "$(date '+%F %T') $*" | tee -a "$L"; }
chk() { sha256sum harness/zz_p1_puppet_lost.script tools/t2_xray_lost.sh tools/t2_lost_pose.py tools/t3_pfL.sh | cut -c1-8 | tr '\n' ' '; }
[ "$(chk)" = "0347bdee d2ec0233 7733cbe4 625db9d0 " ] || { log "REFUSED: file hashes differ from section 57: $(chk)"; exit 2; }
log "waiting for arm A's X runner to end"
until grep -qs ' end: ' "$HOME"/Projects/ITRZone/out/runs/*-t2a1-xray/t2_xray.log; do sleep 30; done
log "arm A's X runner has ended"
for c in keep mut_sess freeze mut_off cross; do
  [ -e "out/t3pfL_$c" ] && { log "$c: out dir exists, skipped"; continue; }
  tools/t3_pfL.sh "$c" "out/t3pfL_$c" >> "$L" 2>&1; log "$c: driver exit $?"
  sleep 20
done
log "series end"
