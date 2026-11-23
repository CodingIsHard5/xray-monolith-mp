#!/usr/bin/env bash
# R1 step 0 series (engine/perf-harness/PREREG-R1-step0-2026-11-22.md): one slice, EXCLUSIVE, through the WANT queue.
# Usage: tools/r1_series.sh            order file tools/r1_order.txt: "tag|exe|save|lowend(0/1)" per line, '#' = comment
# Same box rules as tb_slice.sh: WANT (only if not already holding), claim when granted, withdraw at once; settle before
# every run (1-min load < 2 and no other >=100 % process, 15 s checks, 5 min cap); stop on any failed run; withdraw
# BEFORE release at the end. One slice only, at most 3 h. Progress in out/r1_state/.
set -u
cd "$(dirname "$0")/.."
T="$HOME/.claude/overseer/tools"; W=engineperf; ORDER=${R1_ORDER:-tools/r1_order.txt}; ST=out/r1_state; mkdir -p $ST
touch $ST/done.txt
WHAT="EXCLUSIVE: headless R1 render-split timing runs (one engine at a time)"
log() { echo "$(date '+%F %T') $*" | tee -a $ST/series.log; }
pending() { grep -v '^#' "$ORDER" | grep . | while IFS='|' read -r tag rest; do grep -qx "$tag" $ST/done.txt || echo "$tag|$rest"; done; }
heavy_ps() { ps -eo pcpu,pid,comm,args --sort=-pcpu | awk 'NR>1 && $1>=100 && $3!~/wineserver|Xvfb/ {print $1"%:"$3}' | head -5 | tr '\n' ' '; }
settle() { local t0 l1 hv; t0=$(date +%s)
  while :; do l1=$(cut -d' ' -f1 /proc/loadavg); hv=$(heavy_ps)
    awk -v l="$l1" 'BEGIN{exit !(l<2)}' && [ -z "${hv// /}" ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[] ok"; return 0; }
    [ $(( $(date +%s) - t0 )) -ge 300 ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[$hv] cap"; return 1; }
    sleep 15; done; }
[ -n "$(pending)" ] || { log "nothing pending"; exit 0; }
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 30 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
SL_END=$(( $(date +%s) + 3*3600 )); stop=""
log "slice granted, load $(cut -d' ' -f1-3 /proc/loadavg)"
while :; do
  line=$(pending | head -1); [ -n "$line" ] || break
  IFS='|' read -r tag exe save low <<<"$line"
  log "$(settle) before $tag"
  [ $(( SL_END - $(date +%s) )) -lt 900 ] && { stop="slice time used"; break; }
  "$T/protected-title-check.sh" >/dev/null 2>&1 && { stop="protected title running"; break; }
  pgrep -x sober >/dev/null && { stop="Roblox running"; break; }
  "$T/quiet-required.sh" claim $W 30 "$WHAT" >/dev/null 2>&1          # same-text renewal for this run
  log "run $tag exe=$exe save=$save lowend=$low load $(cut -d' ' -f1-3 /proc/loadavg)"
  BENCH_NOCLAIM=1 BENCH_EXCLUSIVE=1 BENCH_SAVE=$save BENCH_LOWEND=$low tools/bench.sh "$exe" "$tag" 600 3000 -perf_noinput -perf_nopresent -perf_slow 20 > $ST/run_$tag.log 2>&1; rc=$?
  v=$(grep -a '^VERDICT' out/$tag/conditions.txt 2>/dev/null)
  log "run $tag rc=$rc $v"
  [ $rc = 0 ] || { stop="run $tag ended rc=$rc"; break; }
  echo "$tag" >> $ST/done.txt
done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1   # withdraw FIRST
log "slice end: $(grep -c . $ST/done.txt) done, $(pending | wc -l) left. ${stop:-list finished}"
timeout 60 "$T/notify-overseer.sh" engineperf - <<<"EnginePerf R1 step 0 series: slice ended $(date +%T), $(grep -c . $ST/done.txt) runs done, $(pending | wc -l) left. ${stop:-list finished}. Scoring next." >/dev/null 2>&1
