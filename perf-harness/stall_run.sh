#!/usr/bin/env bash
# Walk-stall reproduction (engine/perf-harness/PREREG-walkstall-2026-11-22.md). One run, EXCLUSIVE via WANT.
# Runs tb_run.sh (walk route, RefDet14) and watches the engine log; on a stall captures screenshot, per-thread CPU,
# wchan, memory, log tail, and (deadlock/unclassified only) a winedbg "bt all". Then kills the engine.
set -u
cd "$(dirname "$0")/.."; P=$PWD; RUN=$P/run
T="$HOME/.claude/overseer/tools"; W=engineperf; TAG=${1:-walkst1}; EXE=RefDet14.exe
OUT=$P/out/stall_$TAG; mkdir -p "$OUT"; LOG=$RUN/appdata/logs/xray_$(whoami).log
WHAT="EXCLUSIVE: headless walk-stall reproduction (one engine)"
log() { echo "$(date '+%F %T') $*" | tee -a "$OUT/watch.log"; }
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 35 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
log "slot granted, load $(cut -d' ' -f1-3 /proc/loadavg)"
TB_NOCLAIM=1 TB_EXE=$EXE TB_TIMEOUT=1500 tools/tb_run.sh "$TAG" bench1 -tb_walk 60 > "$OUT/tb_run.log" 2>&1 &
TBP=$!
engine() { local best="" br=0 r; for p in $(pgrep -f "bin\\\\$EXE"); do r=$(awk '/VmRSS/{print $2}' /proc/$p/status 2>/dev/null); [ "${r:-0}" -gt "$br" ] && { br=$r; best=$p; }; done; echo "$best"; }
ticks() { for t in /proc/$1/task/*; do s=$(sed 's/^.*) //' $t/stat 2>/dev/null) || continue; echo "$(basename $t) $(echo "$s" | awk '{print $1, $12+$13}') $(cat $t/wchan 2>/dev/null) $(tr -d '\n' < $t/comm 2>/dev/null | tr ' ' '_')"; done; }
sleep 20; last=-1; same=0; result=""
while kill -0 $TBP 2>/dev/null; do
  sleep 10; sz=$(stat -c %s "$LOG" 2>/dev/null || echo 0); EP=$(engine)
  if [ -n "$EP" ] && [ "$sz" -gt 102400 ] && [ "$sz" = "$last" ]; then same=$((same+10)); else same=0; fi; last=$sz
  [ $((SECONDS % 60)) -lt 10 ] && log "log_bytes $sz engine $EP rss_kb $(awk '/VmRSS/{print $2}' /proc/$EP/status 2>/dev/null)"
  [ $same -ge 90 ] || continue
  log "STALL: log unchanged 90 s at $sz bytes, engine pid $EP"
  DISPLAY=:77 xwd -root -silent > "$OUT/screen.xwd" 2>/dev/null && convert "$OUT/screen.xwd" "$OUT/screen.png" 2>/dev/null
  DISPLAY=:77 xwininfo -root -tree > "$OUT/xwininfo.txt" 2>&1
  tail -40 "$LOG" > "$OUT/log_tail.txt"; grep -E 'Vm(RSS|Size|Swap)' /proc/$EP/status > "$OUT/mem.txt"
  ticks $EP > "$OUT/ticks_t0.txt"; sleep 30; ticks $EP > "$OUT/ticks_t30.txt"
  HZ=$(getconf CLK_TCK); cores=$(awk -v hz=$HZ 'NR==FNR{a[$1]=$3;next} {d+=$3-a[$1]} END{printf "%.3f", d/hz/30}' "$OUT/ticks_t0.txt" "$OUT/ticks_t30.txt")
  awk 'NR==FNR{a[$1]=$3;next} {print $3-a[$1], $0}' "$OUT/ticks_t0.txt" "$OUT/ticks_t30.txt" | sort -rn | head -8 > "$OUT/hot_threads.txt"
  log "cpu over 30 s: $cores cores; hottest: $(head -1 "$OUT/hot_threads.txt")"
  dlg=$(grep -ciE 'FATAL|assert|Expression' "$OUT/log_tail.txt")
  if awk -v c=$cores 'BEGIN{exit !(c>=0.5)}'; then result=SPIN; else result=LOW_CPU; fi
  log "log FATAL/assert lines: $dlg; preliminary: $result (dialog check needs the screenshot, done by hand)"
  if [ "$result" = LOW_CPU ]; then
    export WINEPREFIX=$RUN/prefix DISPLAY=:77 WINEDEBUG=-all
    timeout 60 winedbg --command "info process" > "$OUT/winedbg_proc.txt" 2>&1
    WP=$(grep -i "$EXE" "$OUT/winedbg_proc.txt" | grep -oE '^[[:space:]>]*[0-9a-f]{8}' | tr -d ' >' | head -1)
    log "winedbg pid 0x$WP"; [ -n "$WP" ] && timeout 180 winedbg --command "bt all" 0x$WP > "$OUT/winedbg_bt_all.txt" 2>&1
    log "winedbg bt all: $(wc -l < "$OUT/winedbg_bt_all.txt" 2>/dev/null) lines"
  fi
  break
done
[ -z "$result" ] && log "NO STALL detected; run ended: $(tail -1 "$OUT/tb_run.log")"
WINEPREFIX=$RUN/prefix wineserver -k >/dev/null 2>&1; wait $TBP; log "tb_run rc=$?"
cp -r "$P/out/tb_$TAG" "$OUT/tb" 2>/dev/null
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1
log "released; result=${result:-NOT_REPRODUCED}"
timeout 60 "$T/notify-overseer.sh" engineperf - <<<"EnginePerf walk-stall run $TAG ended $(date +%T): ${result:-no stall detected}. Classifying next." >/dev/null 2>&1
