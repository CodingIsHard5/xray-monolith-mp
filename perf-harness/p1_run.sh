#!/usr/bin/env bash
# P1 puppet probe (engine/perf-harness/PREREG-P1-puppet-2026-11-23.md, b6dc6fd94). One EXCLUSIVE slice via WANT:
# R1 "on" (export on), R2 "off" (export off). Installs the script + a private track copy into run/, removes them after.
# Heartbeat: export (R1) / meta (R2) file under /dev/shm/p1 must grow (30 s R1, 150 s R2); on a stall: screenshot,
# per-thread CPU, log tail, then kill (stop and report). Stops on any failed run.
set -u
cd "$(dirname "$0")/.."; P=$PWD; RUN=$P/run
T="$HOME/.claude/overseer/tools"; W=engineperf; EXE=RefDet14.exe
TRK=/home/caden/Projects/ITRZone/out/p1/cordon_full_track.csv; SHM=/dev/shm/p1
OUTB=$P/out/p1; mkdir -p "$OUTB"; LOG=$RUN/appdata/logs/xray_$(whoami).log
WHAT="EXCLUSIVE: headless P1 puppet probe (one engine)"
log() { echo "$(date '+%F %T') $*" | tee -a "$OUTB/p1.log"; }
[ "$(sha256sum "$TRK" | cut -c1-16)" = 213ff8f125723f01 ] || { log "REFUSED: track hash differs"; exit 2; }
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 60 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
log "slot granted, load $(cut -d' ' -f1-3 /proc/loadavg)"
cp "$P/harness/zz_p1_puppet.script" "$RUN/gamedata/scripts/zz_p1_puppet.script"
cp "$TRK" "$RUN/gamedata/configs/p1_track.csv"
"$P/tools/run_noguard.sh" "$RUN" >/dev/null || { log "REFUSED: noguard"; stop="noguard"; }
engine() { local best="" br=0 r; for p in $(pgrep -f "bin\\\\$EXE"); do r=$(awk '/VmRSS/{print $2}' /proc/$p/status 2>/dev/null); [ "${r:-0}" -gt "$br" ] && { br=$r; best=$p; }; done; echo "$best"; }
ticks() { for t in /proc/$1/task/*; do s=$(sed 's/^.*) //' $t/stat 2>/dev/null) || continue; echo "$(basename $t) $(echo "$s" | awk '{print $1, $12+$13}') $(cat $t/wchan 2>/dev/null) $(tr -d '\n' < $t/comm 2>/dev/null | tr ' ' '_')"; done; }
settle() { local t0=$(date +%s) l1; while :; do l1=$(cut -d' ' -f1 /proc/loadavg); awk -v l="$l1" 'BEGIN{exit !(l<2)}' && { echo "settle ok load=$l1 wait=$(( $(date +%s)-t0 ))s"; return 0; }
  [ $(( $(date +%s)-t0 )) -ge 300 ] && { echo "settle cap load=$l1"; return 1; }; sleep 15; done; }
stop=${stop:-}
for spec in "on|-p1_export|30" "off||150"; do
  [ -n "$stop" ] && break
  IFS='|' read -r tag xf hb <<<"$spec"; O=$OUTB/$tag; mkdir -p "$O"; rm -rf "$SHM"; mkdir -p "$SHM"
  log "$(settle) before $tag"
  "$T/protected-title-check.sh" >/dev/null 2>&1 && { stop="protected title"; break; }
  "$T/quiet-required.sh" claim $W 60 "$WHAT" >/dev/null 2>&1
  log "run $tag load $(cut -d' ' -f1-3 /proc/loadavg)"
  TB_NOCLAIM=1 TB_EXE=$EXE TB_TIMES=1100 TB_TIMEOUT=1500 tools/tb_run.sh "p1$tag" cordon1 -p1 $xf -p1_tag $tag > "$O/tb_run.log" 2>&1 &
  TBP=$!; HBF=$SHM/export_$tag.csv; [ "$tag" = off ] && HBF=$SHM/meta_$tag.csv
  last=-1; same=0; stalled=""
  sleep 20
  while kill -0 $TBP 2>/dev/null; do
    sleep 10; sz=$(stat -c %s "$HBF" 2>/dev/null || echo 0); EP=$(engine)
    if [ -n "$EP" ] && [ "$sz" -gt 0 ] && [ "$sz" = "$last" ] && ! grep -q TRACK_END "$SHM/meta_$tag.csv" 2>/dev/null; then same=$((same+10)); else same=0; fi; last=$sz
    [ $((SECONDS % 120)) -lt 10 ] && log "$tag hb_bytes $sz engine $EP rss_kb $(awk '/VmRSS/{print $2}' /proc/$EP/status 2>/dev/null) anon_kb $(awk '/RssAnon/{print $2}' /proc/$EP/status 2>/dev/null)"
    [ -n "$EP" ] && grep -E 'VmRSS|RssAnon|RssFile' /proc/$EP/status 2>/dev/null | tr -s ' ' | tr '\n' ' ' | sed "s/^/$(date +%T) /;s/$/\n/" >> "$O/mem_samples.txt"
    [ $same -ge $hb ] || continue
    stalled=1; log "STALL in $tag: heartbeat unchanged ${hb}s at $sz bytes"
    DISPLAY=:77 xwd -root -silent > "$O/screen.xwd" 2>/dev/null && convert "$O/screen.xwd" "$O/screen.png" 2>/dev/null
    tail -40 "$LOG" > "$O/log_tail.txt"; ticks $EP > "$O/ticks_t0.txt"; sleep 30; ticks $EP > "$O/ticks_t30.txt"
    WINEPREFIX=$RUN/prefix wineserver -k >/dev/null 2>&1; break
  done
  wait $TBP; rc=$?
  cp "$SHM"/* "$O/" 2>/dev/null; cp -r "$P/out/tb_p1$tag" "$O/tb" 2>/dev/null
  log "run $tag rc=$rc stalled=${stalled:-no} $(grep -a '^SUMMARY' "$O/meta_$tag.csv" 2>/dev/null)"
  [ -n "$stalled" ] && { stop="stall in $tag"; break; }
  [ $rc = 0 ] || { stop="run $tag rc=$rc"; break; }
done
rm -f "$RUN/gamedata/scripts/zz_p1_puppet.script" "$RUN/gamedata/configs/p1_track.csv"
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1
log "released. ${stop:-both runs finished}"
timeout 60 "$T/notify-overseer.sh" engineperf - <<<"EnginePerf P1 slice ended $(date +%T): ${stop:-both runs finished}. Scoring with itrzone next." >/dev/null 2>&1
