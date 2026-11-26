#!/usr/bin/env bash
# Arm D run d0 (made from t3_pfL2.sh d1c786cd): a pose VERSION 2 writer (no session field) against the v3 reader.
#   t3_pfD0.sh <d0a|d0b> <out dir>   as `keep`, but both writers send v2 and the second starts its seq at 1 again, as an
#   old ITR2 mod would after a restart. d0a = control, T2_V2RESET=0 (the returning writer waits until its seq has passed
#   the old one); d0b = T2_V2RESET=1 (it is taken at once, logged RESET). Runner: tools/t2_xray_v3.sh (installs zz_p1_puppet_lost3.script).
# Pre-flight L (lost pose; design note 2026-11-24): ONE X-Ray-only boot per call, its own WANT slice, no ITR2.
#   t3_pfL.sh <case> <out dir>
# The stand-in writer (tools/t2_lost_pose.py) is KILLED with SIGKILL in mid-run, as a crashed ITR2 would stop.
#   keep      policy keep. 3 s pause at track 20 (must NOT be lost). Kill at 60 s, a NEW writer 30 s later (new session
#             number, seq from 1, as a restarted ITR2). Expected: LOST, EPOCH, FOUND, poses again. No stop.
#   freeze    as keep with policy freeze. Expected: LOST, the bridge stops X-Ray (reason lost), CONT after the restart.
#   cross     policy keep. The writer sends `cross` from track 58.5 s and is killed 3 s later, never restarted.
#             Expected: the level change completes, LOST 120 s after the resume, the runner's cross hold ends.
#   mut_off   MUTATION: as freeze (pause included) with the limits switched off. The checker must FAIL it.
#   mut_sess  MUTATION: as keep (pause included), but the new writer reuses the OLD session number, and is killed
#             45 s after its start, before its seq can pass the old one (about 57 s). No pose may be applied after
#             the restart. The checker must FAIL it.
# Exit status: the runner's. Installed names: tools/t3_pfL.sh, tools/t2_xray_lost.sh, tools/t2_lost_pose.py,
# harness/zz_p1_puppet_lost.script (the files arm A uses keep their names and bytes).
set -u
cd "$(dirname "$0")/.."; T="$HOME/.claude/overseer/tools"; W=engineperf; C=${1:?case}; O=${2:?out dir}
# hash preflight (registration PREREG-B1-T2-armD-d0): the pinned files or nothing starts; missing = refused by name
[ "$(sha256sum tools/t2_xray_v3.sh 2>/dev/null | cut -d' ' -f1)" = 71d070e5323c1a562bba5b41bb46b19d7dcbd8686ec6f460379e5318f79cff16 ] || { echo "REFUSED: tools/t2_xray_v3.sh is missing or not 71d070e5"; exit 2; }
[ "$(sha256sum harness/zz_p1_puppet_lost3.script 2>/dev/null | cut -d' ' -f1)" = 5bf69e5ce0ed7d775efe2ab9755826dca43152e093f6fbc5b9909416f12641cb ] || { echo "REFUSED: harness/zz_p1_puppet_lost3.script is missing or not 5bf69e5c"; exit 2; }
[ "$(sha256sum tools/t2_lost_pose2.py 2>/dev/null | cut -d' ' -f1)" = 75f0941b8ca52adc44ac78d969c05093a0456ffbd4a97816b9b860e3f3320694 ] || { echo "REFUSED: tools/t2_lost_pose2.py is missing or not 75f0941b"; exit 2; }
[ "$(sha256sum run/bin/Stance14.exe 2>/dev/null | cut -d' ' -f1)" = b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe ] || { echo "REFUSED: run/bin/Stance14.exe is missing or not b4ce9c77"; exit 2; }
[ -s "${PFL_TRACK:-$HOME/Projects/ITRZone/out/p1/cordon_full_track.csv}" ] || { echo "REFUSED: the track file is missing"; exit 2; }
[ "${3:-}" = "--preflight-only" ] && { echo "preflight ok"; exit 0; }
[ -e "$O" ] && { echo "$O exists, not overwriting"; exit 2; }; mkdir -p "$O"
TRK=${PFL_TRACK:-$HOME/Projects/ITRZone/out/p1/cordon_full_track.csv}
AW=keep; RUN_S=200; LOSTENV=""; KILL_AT=60; BACK_AFTER=30; W1=""; SAME=""; W2_S=""; V2=""
case $C in
  keep) W1="pause=20-23" ;;
  d0a) W1="pause=20-23"; V2=1; LOSTENV="T2_V2RESET=0" ;;
  d0b) W1="pause=20-23"; V2=1; LOSTENV="T2_V2RESET=1" ;;
  freeze) AW=freeze; W1="pause=20-23" ;;
  cross) RUN_S=280; KILL_AT=62; BACK_AFTER=""; W1="crossat=58.5 dest=l02_garbage changer=esc_level_changer_to_garbage_2" ;;
  mut_off) AW=freeze; W1="pause=20-23"; LOSTENV="T2_LOST_MS=0 T2_LOSTX_MS=0" ;;
  mut_sess) SAME=1; W1="pause=20-23"; W2_S=45 ;;
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
# ONE way out, also when cancelled: writer killed, runner stopped and reaped (it CONTs and removes what it installed),
# and only then the slice given back, so nobody starts while the old engine is still there.
PW=""; XP=""; rc=1
finish() { trap - EXIT TERM INT
  [ -n "$PW" ] && { kill -KILL $PW 2>/dev/null; wait $PW 2>/dev/null; }
  [ -n "$XP" ] && kill -0 $XP 2>/dev/null && { kill -TERM $XP 2>/dev/null; wait $XP 2>/dev/null; }
  rm -f $SHM/pose.txt $SHM/cross.txt
  "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1   # withdraw FIRST
  log "slice end (exit $rc)"; exit $rc; }
trap finish EXIT; trap 'log "cancelled"; rc=143; finish' TERM INT
SESS1=$(utc); [ -n "$V2" ] && SESS1=none
python3 -I tools/t2_lost_pose2.py $SHM "$O/pose_written.csv" "$TRK" aw=$AW sess=$SESS1 $W1 > "$O/pose_writer.log" 2>&1 & PW=$!
env $LOSTENV T2_MAXRESTART=0 T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_RUN_S=$RUN_S \
  tools/t2_xray_v3.sh "$O/run" none-itr2 > "$O/t2_xray.out" 2>&1 & XP=$!
echo "utc_ms,event,detail" > "$O/writer_events.csv"
# the writer's own clock starts at its first write (after X-Ray's first snapshot with track time >= 0)
until [ "$(wc -l < "$O/pose_written.csv" 2>/dev/null || echo 0)" -ge 2 ] || ! kill -0 $XP 2>/dev/null || ! kill -0 $PW 2>/dev/null; do sleep 0.2; done
if [ "$(wc -l < "$O/pose_written.csv" 2>/dev/null || echo 0)" -lt 2 ]; then
  log "NO FIRST POSE: the writer or the runner ended before it (writer log: $(tail -1 "$O/pose_writer.log" 2>/dev/null))"; rc=3; finish; fi
W0=$(utc); echo "$W0,first_write,session $SESS1" >> "$O/writer_events.csv"
while kill -0 $XP 2>/dev/null && [ $(( $(utc) - W0 )) -lt $(( KILL_AT * 1000 )) ]; do sleep 0.2; done
if kill -0 $XP 2>/dev/null; then
  kill -KILL $PW 2>/dev/null; wait $PW 2>/dev/null; PW=""; KU=$(utc); echo "$KU,kill,writer killed at $(( (KU - W0) / 1000 )) s" >> "$O/writer_events.csv"; log "writer killed"
  if [ -n "$BACK_AFTER" ]; then
    while kill -0 $XP 2>/dev/null && [ $(( $(utc) - KU )) -lt $(( BACK_AFTER * 1000 )) ]; do sleep 0.2; done
    if kill -0 $XP 2>/dev/null; then
      SESS2=$(utc); [ -n "$SAME" ] && SESS2=$SESS1; [ -n "$V2" ] && SESS2=none
      python3 -I tools/t2_lost_pose2.py $SHM "$O/pose_written.csv" "$TRK" aw=$AW sess=$SESS2 lvl="$(head -1 "$O/pose_written.csv.lvl")" t0=$(( KILL_AT + BACK_AFTER )) >> "$O/pose_writer.log" 2>&1 & PW=$!
      RU=$(utc); echo "$RU,restart,session $SESS2" >> "$O/writer_events.csv"; log "writer started again (session $SESS2)"
      if [ -n "$W2_S" ]; then
        while kill -0 $XP 2>/dev/null && [ $(( $(utc) - RU )) -lt $(( W2_S * 1000 )) ]; do sleep 0.2; done
        kill -KILL $PW 2>/dev/null; wait $PW 2>/dev/null; PW=""; echo "$(utc),kill2,second writer killed after $W2_S s" >> "$O/writer_events.csv"; fi
    fi
  fi
fi
wait $XP; rc=$?; XP=""
log "t2_xray rc=$rc"
finish
