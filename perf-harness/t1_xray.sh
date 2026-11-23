#!/usr/bin/env bash
# B1-T1, X-Ray side (perf-harness/PREREG-B1-T1-2026-11-23.md). Runs UNDER the joint EXCLUSIVE claim held by the joint
# driver: it never claims, wants or releases. Usage: tools/t1_xray.sh <out_dir> [itr2_proc_pattern]
# - installs the P1 script (with -b1_snap) and the track copy into the project's own run/, removes them at the end;
# - starts the watcher (tools/t1_watch.py): write stamps (UTC ms), torn reads, CPU/memory of both engines every 5 s;
# - starts headless X-Ray (RefDet14.exe, cordon1) with -p1 -b1_snap;
# - STALL: when the snapshot's track time reaches T1_STALL_AT (600 s), SIGSTOP the X-Ray engine process for
#   T1_STALL_S (20 s), then SIGCONT; both times are logged in UTC ms (stall.csv);
# - heartbeat: outside the stall, no new seq for 30 s = a stall of X-Ray itself: screenshot, per-thread ticks, kill.
# Exit codes: 0 track ended; 3 X-Ray heartbeat stall; 4 X-Ray ended early; 2 refused.
set -u
. "$(cd "$(dirname "$0")" && pwd)/lib_engine_pids.sh"   # engine lookup by Wine binary, never a bare name match
cd "$(dirname "$0")/.."; P=$PWD; RUN=$P/run; O=${1:?out_dir}; ITR=${2:-itr2-shadow}   # ITRZone: match its run dir, never the exe name
EXE=${T1_EXE:-RefDet14.exe}; TRK=/home/caden/Projects/ITRZone/out/p1/cordon_full_track.csv; SHM=/dev/shm/itrz_b1
AT=${T1_STALL_AT:-600}; DUR=${T1_STALL_S:-20}
mkdir -p "$O"; log() { echo "$(date -u '+%F %T.%3N')Z $*" | tee -a "$O/t1_xray.log"; }
utc() { echo $(( $(date +%s%N) / 1000000 )); }
[ "$(sha256sum "$TRK" | cut -c1-16)" = 213ff8f125723f01 ] || { log "REFUSED: track hash differs"; exit 2; }
"$HOME/.claude/overseer/tools/quiet-required.sh" check 2>/dev/null | grep -q '^HELD:.* EXCLUSIVE\|^HELD:.*B1-T1' || { log "REFUSED: no joint EXCLUSIVE claim held"; exit 2; }
"$P/tools/run_noguard.sh" "$RUN" >/dev/null || { log "REFUSED: noguard"; exit 2; }
rm -f "$SHM"/snap.txt "$SHM"/meta_t1.csv "$SHM"/hits_t1.csv; mkdir -p "$SHM"
cp "$P/harness/zz_p1_puppet.script" "$RUN/gamedata/scripts/zz_p1_puppet.script"
cp "$TRK" "$RUN/gamedata/configs/p1_track.csv"
cleanup() { rm -f "$RUN/gamedata/scripts/zz_p1_puppet.script" "$RUN/gamedata/configs/p1_track.csv"; }
trap cleanup EXIT
engine() { engine_big "$EXE"; }
ticks() { for t in /proc/$1/task/*; do s=$(sed 's/^.*) //' $t/stat 2>/dev/null) || continue; echo "$(basename $t) $(echo "$s" | awk '{print $1, $12+$13}') $(tr -d '\n' < $t/comm 2>/dev/null | tr ' ' '_')"; done; }
hdr() { head -1 "$SHM/snap.txt" 2>/dev/null; }          # seq,<seq>,<t_xray_ms>,<t_track_s>
python3 "$P/tools/t1_watch.py" "$SHM/snap.txt" "$O/watch" "bin\\$EXE" "$ITR" > "$O/watch.log" 2>&1 & WP=$!
log "start: load $(cut -d' ' -f1-3 /proc/loadavg); stall at track ${AT}s for ${DUR}s"
TB_NOCLAIM=1 TB_EXE=$EXE TB_TIMES=1100 TB_TIMEOUT=1500 tools/tb_run.sh t1 cordon1 -p1 -b1_snap -p1_tag t1 > "$O/tb_run.log" 2>&1 &
TBP=$!; stalled=""; done_stall=""; last=""; same=0; rc=0
sleep 20
while kill -0 $TBP 2>/dev/null; do
  sleep 1; h=$(hdr); s=$(cut -d, -f2 <<<"$h"); tt=$(cut -d, -f4 <<<"$h")
  if [ -z "$done_stall" ] && [ -n "$tt" ] && awk -v t="$tt" -v a="$AT" 'BEGIN{exit !(t>=a)}'; then
    EP=$(engine); log "STALL start: seq $s track ${tt}s engine $EP"
    echo "utc_ms,event,seq,track_s" > "$O/stall.csv"; echo "$(utc),STOP,$s,$tt" >> "$O/stall.csv"
    kill -STOP "$EP"; sleep "$DUR"; kill -CONT "$EP"
    echo "$(utc),CONT,$s,$tt" >> "$O/stall.csv"; log "STALL end: SIGCONT sent"; done_stall=1; same=0; last=$s; continue
  fi
  if [ -n "$s" ] && [ "$s" = "$last" ]; then same=$((same+1)); else same=0; fi; last=$s
  # the export stops BY DESIGN at TRACK_END (on_update returns); P1's driver had this exclusion and T1 run 1 lacked it
  grep -q '^TRACK_END' "$SHM/meta_t1.csv" 2>/dev/null && same=0
  if [ $same -ge 30 ]; then
    stalled=1; EP=$(engine); log "X-RAY HEARTBEAT STALL: seq $s unchanged 30 s"
    DISPLAY=:77 xwd -root -silent > "$O/screen.xwd" 2>/dev/null && convert "$O/screen.xwd" "$O/screen.png" 2>/dev/null
    [ -n "$EP" ] && { ticks $EP > "$O/ticks_t0.txt"; sleep 10; ticks $EP > "$O/ticks_t10.txt"; }
    WINEPREFIX=$RUN/prefix wineserver -k >/dev/null 2>&1; break
  fi
done
wait $TBP; trc=$?
kill $WP 2>/dev/null; wait $WP 2>/dev/null
cp "$SHM"/meta_t1.csv "$SHM"/hits_t1.csv "$SHM"/snap.txt "$O/" 2>/dev/null; cp -r "$P/out/tb_t1" "$O/tb" 2>/dev/null
grep -q '^TRACK_END' "$O/meta_t1.csv" 2>/dev/null && ended=1 || ended=""
if [ -n "$stalled" ]; then rc=3; elif [ -z "$ended" ]; then rc=4; fi
log "end: tb_run rc=$trc track_end=${ended:-no} stall_done=${done_stall:-no} rc=$rc $(grep -a '^SUMMARY' "$O/meta_t1.csv" 2>/dev/null)"
exit $rc
