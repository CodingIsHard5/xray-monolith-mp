#!/usr/bin/env bash
# B1-T2 pre-flight boot (PREREG-B1-T2-xray-side section 5): ONE X-Ray-only claimed slice via WANT, ~10 min. Not scored.
# Checks: set_actor_direction sign (pose yaw vs the snapshot's actor yaw), whether -t2_pause keeps Lua running (do
# snapshots continue during phase D, and does UNPAUSE follow), and a real SIGKILL + restart (boot time, new run_id).
# Uses tools/t2_xray.sh unchanged, with a stand-in pose writer (tools/t2_preflight_pose.py), no ITR2.
set -u
cd "$(dirname "$0")/.."; T="$HOME/.claude/overseer/tools"; W=engineperf; O=${PF_OUT:-out/t2pre}; mkdir -p $O
WHAT="EXCLUSIVE: headless X-Ray-only B1-T2 pre-flight boot (one engine)"
log() { echo "$(date '+%F %T') $*" | tee -a $O/preflight.log; }
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 20 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
log "slice granted, load $(cut -d' ' -f1-3 /proc/loadavg)"
SHM=/dev/shm/itrz_b1; mkdir -p $SHM; rm -f $SHM/pose.txt $SHM/snap.txt
# pre-flight E (section 46): PF_DMG=prot|god_off|god_off_norefill switches the script's forced-damage test mode on for this boot only
rm -f $SHM/dmgtest.txt; [ -n "${PF_DMG:-}" ] && echo "$PF_DMG" > $SHM/dmgtest.txt
python3 tools/t2_preflight_pose.py $SHM $O/pose_written.csv ${PF_POSE_ARG:-} > $O/pose_writer.log 2>&1 & PW=$!
T2_EXE=${PF_EXE:-RefDet14.exe} T2_ABSORB=${PF_ABSORB:-0} T2_ABSORB_SKIP=${PF_ABSORB_SKIP:-0} T2_STALL_S=${PF_STALL_S:-20} T2_RUN_S=${PF_RUN_S:-240} T2_STALL_AT=${PF_STALL_AT:-99999} T2_KILL_AT=${PF_KILL_AT:-120} T2_PAUSE=${PF_PAUSE:-1} T2_FREEZE=${PF_FREEZE:-} tools/t2_xray.sh $O/run none-itr2 > $O/t2_xray.out 2>&1; rc=$?
kill $PW 2>/dev/null; wait $PW 2>/dev/null; rm -f $SHM/pose.txt $SHM/dmgtest.txt
log "t2_xray rc=$rc"
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1   # withdraw FIRST
log "slice end"
timeout 60 "$T/notify-overseer.sh" engineperf - <<<"EnginePerf B1-T2 pre-flight boot: slice ended $(date +%T), t2_xray rc=$rc. Reading it next." >/dev/null 2>&1
