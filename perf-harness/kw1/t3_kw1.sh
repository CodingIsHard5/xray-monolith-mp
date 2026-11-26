#!/usr/bin/env bash
# Kill channel witness kw1, X-Ray only (made from t3_pfD0.sh; design perf-harness/PREREG-killchannel-xwitness-2026-11-26.md).
#   t3_kw1.sh <out dir> [--preflight-only]
# ONE X-Ray-only boot, Cordon bench save, its own WANT slice, no ITR2. Runner tools/t2_xray_kw.sh (installs
# harness/zz_p1_puppet_kill4.script), stand-in writer tools/t2_kill_pose.py (pose v4, the step table of section 2).
# Env: BENCH_DISPLAY (default :141, design section 6): REFUSED if /tmp/.X11-unix/X<n> or /tmp/.X<n>-lock exists (an X
#      server is there or was: tb_run.sh would reuse it). Checked at the preflight and again when the slice is granted.
#      KW1_PICK (default 30): the writer's pick time. The registered retry (section 3) uses 45 and needs the Overseer's GO.
# Out dir: preflight.log, pose_written.csv, writer_snaps.csv (the writer's per-snapshot log), pose_writer.log,
#   writer_events.csv, t2_xray.out, run/ (the runner's folder: t2_xray.log, restarts.csv, lost.csv, freeze.csv, boot0/...).
# Score: python3 -I tools/t3_kw1_check.py <out dir> out=<out dir>/t3_kw1_check.txt
# Exit status: the runner's; 2 = refused.
set -u
cd "$(dirname "$0")/.."; T="$HOME/.claude/overseer/tools"; W=engineperf; O=${1:?out dir}
PICK=${KW1_PICK:-30}
TRK=${PFL_TRACK:-$HOME/Projects/ITRZone/out/p1/cordon_full_track.csv}
# hash preflight (registration: PREREG kw1 section 7). Every pinned file or nothing starts; missing = refused by name.
# The PIN_ME_* values are placeholders: until they are replaced by the registered sha256 the preflight REFUSES.
pin() {   # <file> <sha256>
  local f=$1 want=$2 got
  [ -e "$f" ] || { echo "REFUSED: $f is missing"; return 1; }
  case "$want" in PIN_ME_*) echo "REFUSED: $f has no registered hash ($want)"; return 1 ;; esac
  got=$(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)
  [ "$got" = "$want" ] || { echo "REFUSED: $f is not ${want:0:8} (it is ${got:0:8})"; return 1; }
}
bad=0
pin tools/t2_xray_kw.sh                  16cb80107136af54234bdad37b31f55b1526915aeb41f69a0d4649b5104b6185       || bad=1
pin harness/zz_p1_puppet_kill4.script    f19b28c094dabaab99e64b71716f696bdee200d46d37aa366e32b7eba5ae33af  || bad=1
pin tools/t2_kill_pose.py                9f3075cdafa728a808eec43b135dac0080e311d1de6cf204e2416bea119046e6     || bad=1
pin tools/t3_kw1_check.py                94bfe3abb299c5bfe6e524012659ae1868b3a94c40f60245b485f032b8ba33b1     || bad=1
pin tools/t3_kw1_selftest.sh             08fe6760ee1d3f07bf9b58a212778929b4ce2a9e47b4d43edd116c11509fdee9  || bad=1
pin run/bin/Stance14.exe                 b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe        || bad=1
pin run/appdata/savedgames/cordon1.scop  6dea752bc652a45c748e1438e3bc8ffd2d8d89f24730177c792328aeb5d13f3e        || bad=1
pin "$TRK"                               213ff8f125723f011b6cae3972c5e9afa574bf0418a36c0fb34fbde652734cd1           || bad=1
DISP=${BENCH_DISPLAY:-:141}
disp_free() {   # refuses a display that exists or left a lock
  local n=${DISP#:}
  case "$DISP" in :[0-9]*) ;; *) echo "REFUSED: BENCH_DISPLAY '$DISP' is not :<n>"; return 1 ;; esac
  case "$n" in *[!0-9]*) echo "REFUSED: BENCH_DISPLAY '$DISP' is not :<n>"; return 1 ;; esac
  [ -e "/tmp/.X11-unix/X$n" ] && { echo "REFUSED: display $DISP exists (/tmp/.X11-unix/X$n)"; return 1; }
  [ -e "/tmp/.X$n-lock" ] && { echo "REFUSED: display $DISP is locked (/tmp/.X$n-lock)"; return 1; }
  return 0
}
disp_free || bad=1
case "$PICK" in ''|*[!0-9]*) echo "REFUSED: KW1_PICK '$PICK' is not whole seconds"; bad=1 ;; esac
[ "$bad" = 0 ] || exit 2
[ "${2:-}" = "--preflight-only" ] && { echo "preflight ok"; exit 0; }
[ -e "$O" ] && { echo "$O exists, not overwriting"; exit 2; }; mkdir -p "$O"
export BENCH_DISPLAY=$DISP
# engine seconds from the level load: 15 s warm-up (WAIT_MS) + pick + 60 s of steps and after + 20 s margin
AW=keep; RUN_S=$(( 15 + PICK + 60 + 20 ))
WHAT="EXCLUSIVE: headless X-Ray-only B1-T2 pre-flight boot (one engine)"
log() { echo "$(date '+%F %T') $*" | tee -a "$O/preflight.log"; }
utc() { echo $(( $(date +%s%N) / 1000000 )); }
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 20 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
log "slice granted, kw1, pick $PICK s, display $DISP, load $(cut -d' ' -f1-3 /proc/loadavg)"
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
# the wait for the slice can be long: the display must still be free now
disp_free >> "$O/preflight.log" 2>&1 || { log "REFUSED at start: display $DISP not free"; rc=2; finish; }
SESS1=$(utc)
python3 -I tools/t2_kill_pose.py $SHM "$O/pose_written.csv" "$TRK" aw=$AW sess=$SESS1 pick=$PICK snaplog="$O/writer_snaps.csv" > "$O/pose_writer.log" 2>&1 & PW=$!
env T2_MAXRESTART=0 T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_RUN_S=$RUN_S \
  tools/t2_xray_kw.sh "$O/run" none-itr2 > "$O/t2_xray.out" 2>&1 & XP=$!
echo "utc_ms,event,detail" > "$O/writer_events.csv"
# the writer's own clock starts at its first write (after X-Ray's first snapshot with track time >= 0)
until [ "$(wc -l < "$O/pose_written.csv" 2>/dev/null || echo 0)" -ge 2 ] || ! kill -0 $XP 2>/dev/null || ! kill -0 $PW 2>/dev/null; do sleep 0.2; done
if [ "$(wc -l < "$O/pose_written.csv" 2>/dev/null || echo 0)" -lt 2 ]; then
  log "NO FIRST POSE: the writer or the runner ended before it (writer log: $(tail -1 "$O/pose_writer.log" 2>/dev/null))"; rc=3; finish; fi
W0=$(utc); echo "$W0,first_write,session $SESS1 pick $PICK" >> "$O/writer_events.csv"
wait $XP; rc=$?; XP=""
log "t2_xray rc=$rc"
finish
