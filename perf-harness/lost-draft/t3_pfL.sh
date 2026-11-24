#!/usr/bin/env bash
# Pre-flight L (lost pose; design note 2026-11-24): ONE X-Ray-only boot per call, its own WANT slice, no ITR2.
#   t3_pfL.sh <case> <out dir>
# The stand-in writer (tools/t2_lost_pose.py) is KILLED with SIGKILL in mid-run, as a crashed ITR2 would stop.
#   keep      policy keep. 3 s pause at track 20 (must NOT be lost). Kill at 60 s, a NEW writer 30 s later (new session
#             number, seq from 1, as a restarted ITR2). Expected: LOST, EPOCH, FOUND, poses again. No stop.
#   freeze    as keep with policy freeze. Expected: LOST, the bridge stops X-Ray (reason lost), CONT after the restart.
#   cross     policy keep. The writer sends `cross` from track 58.5 s and is killed 3 s later, never restarted.
#             Expected: the level change completes, LOST 120 s after the resume, the runner's cross hold ends.
#   mut_off   MUTATION: as freeze with the limits switched off. The checker must FAIL it.
#   mut_sess  MUTATION: as keep, but the new writer reuses the OLD session number. No pose may be applied after the
#             restart. The checker must FAIL it.
set -u
cd "$(dirname "$0")/.."; T="$HOME/.claude/overseer/tools"; W=engineperf; C=${1:?case}; O=${2:?out dir}
[ -e "$O" ] && { echo "$O exists, not overwriting"; exit 2; }; mkdir -p "$O"
TRK=${PFL_TRACK:-$HOME/Projects/ITRZone/out/p1/cordon_full_track.csv}
AW=keep; RUN_S=200; LOSTENV=""; KILL_AT=60; BACK_AFTER=30; W1=""; SAME=""
case $C in
  keep) W1="pause=20-23" ;;
  freeze) AW=freeze; W1="pause=20-23" ;;
  cross) RUN_S=280; KILL_AT=62; BACK_AFTER=""; W1="crossat=58.5 dest=l02_garbage changer=esc_level_changer_to_garbage_2" ;;
  mut_off) AW=freeze; LOSTENV="T2_LOST_MS=0 T2_LOSTX_MS=0" ;;
  mut_sess) SAME=1; RUN_S=140 ;;   # ends before the reused session could pass the old seq again (about 57 s after the restart)
  *) echo "unknown case $C"; exit 2 ;;
esac
WHAT="EXCLUSIVE: headless X-Ray-only B1-T2 pre-flight boot (one engine)"
log() { echo "$(date '+%F %T') $*" | tee -a "$O/preflight.log"; }
utc() { echo $(( $(date +%s%N) / 1000000 )); }
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 20 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
log "slice granted, case $C, load $(cut -d' ' -f1-3 /proc/loadavg)"
SHM=/dev/shm/itrz_b1; mkdir -p $SHM; rm -f $SHM/pose.txt $SHM/snap.txt $SHM/dmgtest.txt $SHM/cross.txt
PW=""; trap '[ -n "$PW" ] && kill -KILL $PW 2>/dev/null' EXIT
SESS1=$(utc)
python3 -I tools/t2_lost_pose.py $SHM "$O/pose_written.csv" "$TRK" aw=$AW sess=$SESS1 $W1 > "$O/pose_writer.log" 2>&1 & PW=$!
env $LOSTENV T2_MAXRESTART=0 T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_RUN_S=$RUN_S \
  tools/t2_xray.sh "$O/run" none-itr2 > "$O/t2_xray.out" 2>&1 & XP=$!
echo "utc_ms,event,detail" > "$O/writer_events.csv"
# the writer's own clock starts at its first write (after X-Ray's first snapshot with track time >= 0)
until [ "$(wc -l < "$O/pose_written.csv" 2>/dev/null || echo 0)" -ge 2 ] || ! kill -0 $XP 2>/dev/null; do sleep 0.2; done
W0=$(utc); echo "$W0,first_write,session $SESS1" >> "$O/writer_events.csv"
while kill -0 $XP 2>/dev/null && [ $(( $(utc) - W0 )) -lt $(( KILL_AT * 1000 )) ]; do sleep 0.2; done
if kill -0 $XP 2>/dev/null; then
  kill -KILL $PW 2>/dev/null; wait $PW 2>/dev/null; PW=""; KU=$(utc); echo "$KU,kill,writer killed at $(( (KU - W0) / 1000 )) s" >> "$O/writer_events.csv"; log "writer killed"
  if [ -n "$BACK_AFTER" ]; then
    while kill -0 $XP 2>/dev/null && [ $(( $(utc) - KU )) -lt $(( BACK_AFTER * 1000 )) ]; do sleep 0.2; done
    if kill -0 $XP 2>/dev/null; then
      SESS2=$(utc); [ -n "$SAME" ] && SESS2=$SESS1
      python3 -I tools/t2_lost_pose.py $SHM "$O/pose_written.csv" "$TRK" aw=$AW sess=$SESS2 t0=$(( KILL_AT + BACK_AFTER )) >> "$O/pose_writer.log" 2>&1 & PW=$!
      echo "$(utc),restart,session $SESS2" >> "$O/writer_events.csv"; log "writer started again (session $SESS2)"
    fi
  fi
fi
wait $XP; rc=$?
[ -n "$PW" ] && { kill -KILL $PW 2>/dev/null; wait $PW 2>/dev/null; PW=""; }
rm -f $SHM/pose.txt $SHM/cross.txt
log "t2_xray rc=$rc"
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1   # withdraw FIRST
log "slice end"
