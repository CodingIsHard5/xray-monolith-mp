#!/usr/bin/env bash
# kj3p: X-ONLY emission preflight driver (kj3/PREREG-kj3p-emission-preflight-DRAFT.md). Made from tools/t3_kw1.sh (cecc098f).
#   kj3p_run.sh <out dir> [--preflight-only]
# ONE X-Ray-only boot, Cordon bench save, its own WANT slice, no ITR2. Runner tools/t2_xray_kj3.sh (installs
# harness/zz_p1_puppet_kill6.script; T2_SURGE_PLAY / T2_PSI_PLAY append `surge` / `psi` to emtest.txt). Pose feed: the
# kw1 stand-in writer tools/t2_kill_pose.py (pose v4, 20 Hz) with pick=99999 (no k line, ever) on a ONE-ROW track
# (kj3/kj3p_track_start.csv = the first data row of ITRZone's cordon_full_track.csv, checked here): the actor stands at the
# track's start point for the whole run.
# Differences from t3_kw1.sh: other pins; the box claim branches on quiet-required.sh's EXIT CODE (never its text); the
# display may never be :0; a watchdog stops the run if a protected title or Sober starts; a leftover engine is stopped
# before the slice is given back; the writer gets pick=99999 and the one-row track; the runner env below.
# Env: BENCH_DISPLAY (default :141): REFUSED if it is :0 or if /tmp/.X11-unix/X<n> or /tmp/.X<n>-lock exists. Checked at
#      the preflight and again when the slice is granted. KJ3P_WAIT_MIN (default 240): give up waiting for the slice.
# Out dir: preflight.log, pose_written.csv, writer_snaps.csv, pose_writer.log, writer_events.csv, t2_xray.out,
#   run/ (the runner's folder: t2_xray.log, restarts.csv, emforce.csv, lost.csv, freeze.csv, boot0/...).
# Score ONCE (registration section 10): bash -c 'set -C; python3 -I wt-render-split/perf-harness/kj3/kj3p_check.py <out>/run > <out>/kj3p_check.txt; echo $? > <out>/kj3p_check.rc'
# Exit status: the runner's; 2 = refused; 3 = no first pose; 4 = stopped for a protected title / Sober.
set -u
K="$(cd "$(dirname "$0")" && pwd)"; cd "$K/../../.."; P=$PWD
T="$HOME/.claude/overseer/tools"; W=engineperf; O=${1:?out dir}
TRK=$HOME/Projects/ITRZone/out/p1/cordon_full_track.csv; TRK1=$K/kj3p_track_start.csv
# hash preflight. Every pinned file or nothing starts; missing = refused by name; PIN_ME_* = not registered yet = refused.
pin() {   # <file> <sha256>
  local f=$1 want=$2 got
  [ -e "$f" ] || { echo "REFUSED: $f is missing"; return 1; }
  case "$want" in PIN_ME_*) echo "REFUSED: $f has no registered hash ($want)"; return 1 ;; esac
  got=$(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)
  [ "$got" = "$want" ] || { echo "REFUSED: $f is not ${want:0:8} (it is ${got:0:8})"; return 1; }
}
bad=0
pin tools/t2_xray_kj3.sh                     245c7dacf849d2466478c3bd7f24e285b8d3fe8e91cb7bcacf33037ffb31ad69 || bad=1
pin harness/zz_p1_puppet_kill6.script        7d7c173dae815a0d7673acffb9d911413a18708f9beb7914afc1dc4685d4c8e9 || bad=1
pin tools/t2_kill_pose.py                    9f3075cdafa728a808eec43b135dac0080e311d1de6cf204e2416bea119046e6 || bad=1
pin tools/t1_watch_keep.py                   9df9c8e4eb2a34ec5b9059c0b26569b7d9e2e2886cc847c9630a8451e8240393 || bad=1
pin tools/lib_engine_pids.sh                 cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e || bad=1
pin tools/tb_run.sh                          1b8d103f2208befc554c48cf0b628830e67e0223cbe5c99ab1fcc34205363953 || bad=1
pin tools/run_noguard.sh                     92729abc420784053cee4b1b1832f5d112f74979a4760e2173d45578358455d5 || bad=1
pin harness/zz_eperf_dump.script             2b79a817a08a1e7ff4e29007f76677dba698a2a5945374331dcc2ae2de874b45 || bad=1
pin out/user.ltx.orig                        3efd38eecbca504001cf03bd4c60bc6e837a3c3284e8517979a1bd9e1526de78 || bad=1
pin run/bin/Stance14.exe                     b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe || bad=1
pin run/appdata/savedgames/cordon1.scop      6dea752bc652a45c748e1438e3bc8ffd2d8d89f24730177c792328aeb5d13f3e || bad=1
pin run/gamedata/scripts/surge_manager.script                aad00a89333b4d47271fd48f66a0f4ee4a1b955f76e411f1e3d220c5bcc61a34 || bad=1
pin run/gamedata/scripts/psi_storm_manager.script            5e83cea9ca5b6e9f11bf40f41c50507ddb6fcd8b28c085604334adf721915b10 || bad=1
pin run/gamedata/scripts/pulse_vortex_consistency_fix.script e795db4da2a91c507b41571c357ae1a0ff44b76aa6433db2926e8ccb108c6164 || bad=1
pin run/gamedata/configs/axr_options.ltx     27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29 || bad=1
pin "$TRK"                                   213ff8f125723f011b6cae3972c5e9afa574bf0418a36c0fb34fbde652734cd1 || bad=1
pin "$TRK1"                                  PIN_ME_TRACK_START || bad=1
pin "$K/kj3p_check.py"                       PIN_ME_CHECKER || bad=1
pin "$K/kj3p_selftest.sh"                    PIN_ME_SELFTEST || bad=1
# the one-row track is exactly the header and the first data row of the pinned track (CR stripped on both)
[ "$(head -2 "$TRK" 2>/dev/null | tr -d '\r')" = "$(tr -d '\r' < "$TRK1" 2>/dev/null)" ] || { echo "REFUSED: $TRK1 is not the first row of $TRK"; bad=1; }
[ "$bad" = 0 ] && . "$P/tools/lib_engine_pids.sh"   # sourced only once its hash matched
[ "$bad" = 0 ] || { echo "REFUSED: pins"; exit 2; }
[ -e run/gamedata/scripts/zz_p1_puppet.script ] && { echo "REFUSED: run/gamedata/scripts/zz_p1_puppet.script exists (a runner left it, or one is running)"; bad=1; }
[ -n "$(engine_main Stance14.exe)" ] && { echo "REFUSED: an engine (Stance14.exe) is already running"; bad=1; }
DISP=${BENCH_DISPLAY:-:141}
disp_free() {   # refuses :0 and any display that exists or left a lock
  local n=${DISP#:}
  case "$DISP" in :[0-9]*) ;; *) echo "REFUSED: BENCH_DISPLAY '$DISP' is not :<n>"; return 1 ;; esac
  case "$n" in *[!0-9]*) echo "REFUSED: BENCH_DISPLAY '$DISP' is not :<n>"; return 1 ;; esac
  [ "$n" -eq 0 ] && { echo "REFUSED: BENCH_DISPLAY $DISP is Caden's screen"; return 1; }
  [ -e "/tmp/.X11-unix/X$n" ] && { echo "REFUSED: display $DISP exists (/tmp/.X11-unix/X$n)"; return 1; }
  [ -e "/tmp/.X$n-lock" ] && { echo "REFUSED: display $DISP is locked (/tmp/.X$n-lock)"; return 1; }
  return 0
}
disp_free || bad=1
WAITMIN=${KJ3P_WAIT_MIN:-240}; case "$WAITMIN" in ''|*[!0-9]*) echo "REFUSED: KJ3P_WAIT_MIN '$WAITMIN' is not whole minutes"; bad=1 ;; esac
[ "$bad" = 0 ] || exit 2
[ "${2:-}" = "--preflight-only" ] && { echo "preflight ok"; exit 0; }
[ -e "$O" ] && { echo "$O exists, not overwriting"; exit 2; }; mkdir -p "$O"
export BENCH_DISPLAY=$DISP
# engine seconds from the level load: 15 s warm-up (WAIT_MS, the writer's first pose at track time >= 0) + 440 s of the
# plan + 10 s so the last T2SUM comes after play 440 (registration section 2)
RUN_S=465; WALL_S=900
WHAT="EXCLUSIVE: headless X-Ray-only B1-T2 kj3p emission preflight boot (one engine)"
log() { echo "$(date '+%F %T') $*" | tee -a "$O/preflight.log"; }
utc() { echo $(( $(date +%s%N) / 1000000 )); }
title_on() { "$T/protected-title-check.sh" >/dev/null 2>&1 || pgrep -x sober >/dev/null; }   # exit 0 = playing: stop
# the slice: queue (want), then claim; every branch on EXIT CODES only (quiet-required.sh header: the exit code is the API)
"$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1; wrc=$?
log "want rc $wrc"
W0=$(date +%s)
until ! title_on && "$T/quiet-required.sh" claim $W 30 "$WHAT" >/dev/null 2>&1; do
  [ $(( $(date +%s) - W0 )) -ge $(( WAITMIN * 60 )) ] && { "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; log "REFUSED: no slice in $WAITMIN min"; exit 2; }
  sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
log "slice granted, kj3p, display $DISP, load $(cut -d' ' -f1-3 /proc/loadavg)"
SHM=/dev/shm/itrz_b1; mkdir -p $SHM; rm -f $SHM/pose.txt $SHM/snap.txt $SHM/dmgtest.txt $SHM/cross.txt
# ONE way out, also when cancelled: watchdog and writer killed, runner stopped and reaped (it CONTs and removes what it
# installed, emtest.txt included), a leftover engine stopped, and only then the slice given back.
PW=""; XP=""; WD=""; rc=1
finish() { trap - EXIT TERM INT
  [ -n "$WD" ] && { kill $WD 2>/dev/null; wait $WD 2>/dev/null; }
  [ -n "$PW" ] && { kill -KILL $PW 2>/dev/null; wait $PW 2>/dev/null; }
  [ -n "$XP" ] && kill -0 $XP 2>/dev/null && { kill -TERM $XP 2>/dev/null; wait $XP 2>/dev/null; }
  EP=$(engine_main Stance14.exe); if [ -n "$EP" ]; then log "LEFTOVER ENGINE pid $EP: killed"; kill -KILL "$EP" 2>/dev/null
    sleep 1; WINEPREFIX="$P/run/prefix" wineserver -k >/dev/null 2>&1; fi
  rm -f $SHM/pose.txt $SHM/cross.txt
  "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1   # withdraw FIRST
  log "slice end (exit $rc)"; exit $rc; }
trap finish EXIT; trap 'log "cancelled"; rc=143; finish' TERM INT
disp_free >> "$O/preflight.log" 2>&1 || { log "REFUSED at start: display $DISP not free"; rc=2; finish; }
title_on && { log "REFUSED at start: a protected title or Sober is running"; rc=4; finish; }
SESS1=$(utc)
python3 -I tools/t2_kill_pose.py $SHM "$O/pose_written.csv" "$TRK1" aw=keep sess=$SESS1 pick=99999 snaplog="$O/writer_snaps.csv" > "$O/pose_writer.log" 2>&1 & PW=$!
env T2_MAXRESTART=0 T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 \
    T2_LOST_MS=5000 T2_LOSTX_MS=120000 T2_OVER_S=120 T2_STALL_PLAY=0 T2_RUN_S=$RUN_S T2_WALL_S=$WALL_S \
    T2_SURGE_PLAY=70 T2_PSI_PLAY=310 \
  tools/t2_xray_kj3.sh "$O/run" none-itr2 > "$O/t2_xray.out" 2>&1 & XP=$!
# watchdog: Caden's box rules: no run while a protected title or Sober runs. Checked every 10 s while the runner lives.
( while kill -0 $XP 2>/dev/null; do sleep 10
    if title_on; then echo "$(date '+%F %T') STOP: a protected title or Sober started; runner $XP sent TERM" >> "$O/preflight.log"
      echo stop > "$O/.title_stop"; kill -TERM $XP 2>/dev/null; exit 0; fi; done ) & WD=$!
echo "utc_ms,event,detail" > "$O/writer_events.csv"
until [ "$(wc -l < "$O/pose_written.csv" 2>/dev/null || echo 0)" -ge 2 ] || ! kill -0 $XP 2>/dev/null || ! kill -0 $PW 2>/dev/null; do sleep 0.2; done
if [ "$(wc -l < "$O/pose_written.csv" 2>/dev/null || echo 0)" -lt 2 ]; then
  log "NO FIRST POSE: the writer or the runner ended before it (writer log: $(tail -1 "$O/pose_writer.log" 2>/dev/null))"; rc=3; finish; fi
echo "$(utc),first_write,session $SESS1 pick 99999 track $TRK1" >> "$O/writer_events.csv"
wait $XP; rc=$?; XP=""
log "t2_xray rc=$rc"
[ -e "$O/.title_stop" ] && { log "run STOPPED by the watchdog (protected title / Sober): not a run of record"; rc=4; }
finish
