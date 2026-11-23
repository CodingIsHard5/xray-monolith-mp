#!/usr/bin/env bash
# B1-T1 crash diagnosis series (perf-harness/PREREG-B1-T1-crashdiag-2026-11-23.md). X-Ray ONLY (no ITR2), one EXCLUSIVE
# slice via WANT. 6 runs, 3 with the 20 s SIGSTOP at track 600 s and 3 without, interleaved S N N S S N, each a full
# t1_xray.sh run (same track, save and -b1_snap as T1) on the diagnostic exe B1Diag14.exe (RefDet14 + log lines).
# A crash is the event being COUNTED, so it does not stop the series. A refusal (rc 2), a protected title, Roblox, or a
# box that will not settle does stop it. Box rules as r1b1_series.sh: WANT only if not holding, claim, withdraw at once;
# settle before each run; withdraw BEFORE release; at most 3 h.
set -u
cd "$(dirname "$0")/.."
T="$HOME/.claude/overseer/tools"; W=engineperf; ST=out/diag_state; mkdir -p $ST; touch $ST/done.txt
EXE=B1Diag14.exe; WHAT="EXCLUSIVE: headless X-Ray-only B1-T1 crash diagnosis runs (one engine at a time)"
ORDER="d1_s d2_n d3_n d4_s d5_s d6_n"
log() { echo "$(date '+%F %T') $*" | tee -a $ST/series.log; }
heavy_ps() { ps -eo pcpu,pid,comm,args --sort=-pcpu | awk 'NR>1 && $1>=100 && $3!~/wineserver|Xvfb/ && $3!="ps" {print $1"%:"$3}' | head -5 | tr '\n' ' '; }
settle() { local t0 l1 hv; t0=$(date +%s)
  while :; do l1=$(cut -d' ' -f1 /proc/loadavg); hv=$(heavy_ps)
    awk -v l="$l1" 'BEGIN{exit !(l<2)}' && [ -z "${hv// /}" ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 ok"; return 0; }
    [ $(( $(date +%s) - t0 )) -ge 300 ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[$hv] cap"; return 1; }
    sleep 15; done; }
[ "$(sha256sum run/bin/$EXE 2>/dev/null | cut -c1-16)" = "${DIAG_SHA:?set DIAG_SHA to the first 16 hex of the exe hash}" ] || { log "REFUSED: $EXE hash differs"; exit 2; }
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 40 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
SL_END=$(( $(date +%s) + 3*3600 )); stop=""
log "slice granted, load $(cut -d' ' -f1-3 /proc/loadavg)"
for tag in $ORDER; do
  grep -qx "$tag" $ST/done.txt && continue
  sr=$(settle); log "$sr before $tag"; case "$sr" in *cap) stop="not quiet before $tag"; break;; esac
  [ $(( SL_END - $(date +%s) )) -lt 1800 ] && { stop="slice time used"; break; }
  "$T/protected-title-check.sh" >/dev/null 2>&1 && { stop="protected title running"; break; }
  pgrep -x sober >/dev/null && { stop="Roblox running"; break; }
  "$T/quiet-required.sh" claim $W 40 "$WHAT" >/dev/null 2>&1          # same-text renewal for this run
  at=99999; [[ "$tag" == *_s ]] && at=600
  log "run $tag stall_at=$at load $(cut -d' ' -f1-3 /proc/loadavg)"
  T1_EXE=$EXE T1_STALL_AT=$at tools/t1_xray.sh out/diag/$tag none-itr2 > $ST/run_$tag.log 2>&1; rc=$?
  crash=$(grep -a -c 'FATAL ERROR' out/diag/$tag/tb/xray.log 2>/dev/null); diag=$(grep -a -c 'B1DIAG' out/diag/$tag/tb/xray.log 2>/dev/null)
  log "run $tag rc=$rc fatal=${crash:-?} b1diag_lines=${diag:-?}"
  [ $rc = 2 ] && { stop="run $tag refused"; break; }
  echo "$tag" >> $ST/done.txt
done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1   # withdraw FIRST
log "slice end: $(grep -c . $ST/done.txt) of 6 done. ${stop:-list finished}"
timeout 60 "$T/notify-overseer.sh" engineperf - <<<"EnginePerf crash-diagnosis series: slice ended $(date +%T), $(grep -c . $ST/done.txt) of 6 runs. ${stop:-list finished}. Scoring next." >/dev/null 2>&1
