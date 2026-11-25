#!/usr/bin/env bash
# Pre-flight L series (prereg section 57 + 57a): waits until R3 arm A's X-side runner has ended, then runs the five
# boots in the registered order, ALL of them even if one fails (Overseer, 2026-11-24). Scores nothing.
set -u
cd "$(dirname "$0")/.."; L=out/t3pfL_series.log; log() { echo "$(date '+%F %T') $*" | tee -a "$L"; }
# the runner's LAST line only ("<date> <time>Z end: <how>; boots ..."). Section 58: the first version matched
# "cross hold end:" and "boot 0 end:" as well and queued during arm A.
ended() { grep -qsE '^[0-9-]+ [0-9:.]+Z end: .*; boots [0-9]+; ' "$@"; }
[ "${1:-}" = --test-wait ] && { shift; ended "$@" && echo ended || echo "not ended"; exit 0; }
chk() { sha256sum harness/zz_p1_puppet_lost.script tools/t2_xray_lost.sh tools/t2_lost_pose.py tools/t3_pfL.sh | cut -c1-8 | tr '\n' ' '; }
[ "$(chk)" = "0347bdee d2ec0233 7733cbe4 625db9d0 " ] || { log "REFUSED: file hashes differ from section 57: $(chk)"; exit 2; }
log "waiting for arm A's X runner to end"
until ended "$HOME"/Projects/ITRZone/out/runs/*-t2a1-xray/t2_xray.log; do sleep 30; done
log "arm A's X runner has ended"
for c in keep mut_sess freeze mut_off cross; do
  [ -e "out/t3pfL_$c" ] && { log "$c: out dir exists, skipped"; continue; }
  tools/t3_pfL.sh "$c" "out/t3pfL_$c" >> "$L" 2>&1; log "$c: driver exit $?"
  sleep 20
done
log "series end"
