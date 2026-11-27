#!/usr/bin/env bash
# kj3p: X-ONLY emission preflight driver (kj3/PREREG-kj3p-emission-preflight-DRAFT.md). Made from tools/t3_kw1.sh (cecc098f).
#   kj3p_run.sh <out dir> [--preflight-only]          (<out dir> relative to the project root, or absolute)
# ONE X-Ray-only boot, Cordon bench save, its own WANT slice, no ITR2. Runner tools/t2_xray_kj3.sh (installs
# harness/zz_p1_puppet_kill6.script; T2_SURGE_PLAY / T2_PSI_PLAY append `surge` / `psi` to emtest.txt). Pose feed: the
# kw1 stand-in writer tools/t2_kill_pose.py (pose v4, 20 Hz) with pick=99999 (no k line, ever) on a ONE-ROW track
# (kj3/kj3p_track_start.csv = the first data row of ITRZone's cordon_full_track.csv, checked here): the actor stands at the
# track's start point for the whole run.
# Differences from t3_kw1.sh: other pins; the box claim branches on quiet-required.sh's EXIT CODE (never its text); the
# display may never be :0; a watchdog stops the run if a protected title or Sober starts, or if the title check itself
# fails; only an engine of THIS run (its display, or -t2_run <this run id>) is ever killed; the writer gets pick=99999
# and the one-row track; the runner env below.
# Review fixes 2026-11-26 (kj3/kj3p-coderabbit-triage-2026-11-26.md): traps before WANT, and finish gives back only what
# this invocation queued / claimed; a title check that fails is NOT clear; the out dir is made exclusively before any
# claim; every pin is checked again after the claim and before either child starts; the watchdog stop wins (exit 4).
# Env: BENCH_DISPLAY (default :141): REFUSED if it is :0 or if /tmp/.X11-unix/X<n> or /tmp/.X<n>-lock exists. Checked at
#      the preflight and again when the slice is granted. KJ3P_WAIT_MIN (default 240): give up waiting for the slice.
# Out dir: preflight.log, pose_written.csv, writer_snaps.csv, pose_writer.log, writer_events.csv, t2_xray.out,
#   run/ (the runner's folder: t2_xray.log, restarts.csv, emforce.csv, lost.csv, freeze.csv, boot0/...).
# Score ONCE (registration section 10): bash wt-render-split/perf-harness/kj3/kj3p_score.sh <out>/run <out>/kj3p_check.txt
# Exit status: the runner's; 2 = refused; 3 = no first pose; 4 = stopped for a protected title / Sober / failed title check.
# KJ3P_FUNCS_ONLY=1 with `source`: defines the functions below and returns (the selftest's offline tests); nothing runs.
set -u

# ------------------------------------------------------------------------------------------------ functions (no side effect)
pin() {   # <file> <sha256>
  local f=$1 want=$2 got
  [ -e "$f" ] || { echo "REFUSED: $f is missing"; return 1; }
  case "$want" in PIN_ME_*) echo "REFUSED: $f has no registered hash ($want)"; return 1 ;; esac
  got=$(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)
  [ "$got" = "$want" ] || { echo "REFUSED: $f is not ${want:0:8} (it is ${got:0:8})"; return 1; }
}
pins_ok() {   # every pinned file, relative to the project root (the cwd); 0 only if all match
  local b=0
  pin tools/t2_xray_kj3.sh                     245c7dacf849d2466478c3bd7f24e285b8d3fe8e91cb7bcacf33037ffb31ad69 || b=1
  pin harness/zz_p1_puppet_kill6.script        7d7c173dae815a0d7673acffb9d911413a18708f9beb7914afc1dc4685d4c8e9 || b=1
  pin tools/t2_kill_pose.py                    9f3075cdafa728a808eec43b135dac0080e311d1de6cf204e2416bea119046e6 || b=1
  pin tools/t1_watch_keep.py                   9df9c8e4eb2a34ec5b9059c0b26569b7d9e2e2886cc847c9630a8451e8240393 || b=1
  pin tools/lib_engine_pids.sh                 cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e || b=1
  pin tools/tb_run.sh                          1b8d103f2208befc554c48cf0b628830e67e0223cbe5c99ab1fcc34205363953 || b=1
  pin tools/run_noguard.sh                     92729abc420784053cee4b1b1832f5d112f74979a4760e2173d45578358455d5 || b=1
  pin harness/zz_eperf_dump.script             2b79a817a08a1e7ff4e29007f76677dba698a2a5945374331dcc2ae2de874b45 || b=1
  pin out/user.ltx.orig                        3efd38eecbca504001cf03bd4c60bc6e837a3c3284e8517979a1bd9e1526de78 || b=1
  pin run/bin/Stance14.exe                     b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe || b=1
  pin run/appdata/savedgames/cordon1.scop      6dea752bc652a45c748e1438e3bc8ffd2d8d89f24730177c792328aeb5d13f3e || b=1
  pin run/gamedata/scripts/surge_manager.script                aad00a89333b4d47271fd48f66a0f4ee4a1b955f76e411f1e3d220c5bcc61a34 || b=1
  pin run/gamedata/scripts/psi_storm_manager.script            5e83cea9ca5b6e9f11bf40f41c50507ddb6fcd8b28c085604334adf721915b10 || b=1
  pin run/gamedata/scripts/pulse_vortex_consistency_fix.script e795db4da2a91c507b41571c357ae1a0ff44b76aa6433db2926e8ccb108c6164 || b=1
  # the ONLY option file the game reads: axr_main.script:15 ini_file_ex("axr_options.ltx") -> $game_config$ =
  # gamedata\configs\ (_g.script:1598, fsgame.ltx:23); appdata/axr_options_play.ltx is read by no script (triage #17)
  pin run/gamedata/configs/axr_options.ltx     27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29 || b=1
  pin "$TRK"                                   213ff8f125723f011b6cae3972c5e9afa574bf0418a36c0fb34fbde652734cd1 || b=1
  pin "$TRK1"                                  PIN_ME_TRACK_START || b=1
  pin "$K/kj3p_check.py"                       PIN_ME_CHECKER || b=1
  pin "$K/kj3p_score.sh"                       PIN_ME_SCORE || b=1
  pin "$K/kj3p_selftest.sh"                    PIN_ME_SELFTEST || b=1
  # the one-row track is exactly the header and the first data row of the pinned track (CR stripped on both)
  [ "$(head -2 "$TRK" 2>/dev/null | tr -d '\r')" = "$(tr -d '\r' < "$TRK1" 2>/dev/null)" ] || { echo "REFUSED: $TRK1 is not the first row of $TRK"; b=1; }
  # Caden's play wrapper (play/caden_simtick_play.sh) copies his own options over the pinned file while it runs
  [ -e run/.caden-play.lock ] && kill -0 "$(cat run/.caden-play.lock 2>/dev/null)" 2>/dev/null && { echo "REFUSED: Caden's play session is running (run/.caden-play.lock)"; b=1; }
  return $b
}
disp_free() {   # refuses :0 and any display that exists or left a lock
  local n=${DISP#:}
  case "$DISP" in :[0-9]*) ;; *) echo "REFUSED: BENCH_DISPLAY '$DISP' is not :<n>"; return 1 ;; esac
  case "$n" in *[!0-9]*) echo "REFUSED: BENCH_DISPLAY '$DISP' is not :<n>"; return 1 ;; esac
  [ "$n" -eq 0 ] && { echo "REFUSED: BENCH_DISPLAY $DISP is Caden's screen"; return 1; }
  [ -e "$XROOT/.X11-unix/X$n" ] && { echo "REFUSED: display $DISP exists ($XROOT/.X11-unix/X$n)"; return 1; }
  [ -e "$XROOT/.X$n-lock" ] && { echo "REFUSED: display $DISP is locked ($XROOT/.X$n-lock)"; return 1; }
  return 0
}
title_state() {   # 0 = a protected title or Sober is playing; 1 = clear; 2 = cannot tell (helper missing / failed): NOT clear
  local r
  "$T/protected-title-check.sh" >/dev/null 2>&1; r=$?
  [ $r = 0 ] && return 0; [ $r = 1 ] || return 2   # protected-title-check.sh:26: 0 playing, 1 none; anything else = unknown
  pgrep -x sober >/dev/null 2>&1; r=$?
  [ $r = 0 ] && return 0; [ $r = 1 ] || return 2   # pgrep: 0 found, 1 none, else error
  return 1
}
is_own() {   # <pid>: 1 unless the process belongs to THIS run: its DISPLAY is ours (refused at start if it existed), or
             # its command line has -t2_run <this run's id> (t2_xray_kj3.sh:92 passes it to the engine)
  local p=$1
  [ -r "/proc/$p/environ" ] && tr '\0' '\n' < "/proc/$p/environ" 2>/dev/null | grep -Fxq "DISPLAY=$DISP" && return 0
  [ -n "${RID:-}" ] && tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null | grep -Eq -- "(^| )-t2_run $RID( |\$)" && return 0
  return 1
}
cand_pids() { engine_pids Stance14.exe; }   # the selftest replaces this; the real one needs lib_engine_pids.sh
run_id() { RID=$(sed -n 's/^.* boot 0: run_id \([0-9][0-9]*\) .*$/\1/p' "$O/run/t2_xray.log" 2>/dev/null | head -1); }
kill_own() {   # kill every engine process of this run; wineserver -k only if no other Wine process is left in the prefix
  local p mine="" other=""
  run_id
  for p in $(cand_pids); do if is_own "$p"; then mine="$mine $p"; else other="$other $p"; fi; done
  [ -z "$mine" ] && return 0
  log "ENGINE OF THIS RUN pid$mine: killed${other:+ (not touched, not ours:$other)}"; kill -KILL $mine 2>/dev/null
  [ -n "$other" ] && { log "wineserver -k skipped: another engine runs"; return 0; }
  [ "${KJ3P_NO_WINESERVER:-0}" = 1 ] && return 0   # selftest only (no Wine there)
  sleep 1; WINEPREFIX="$P/run/prefix" wineserver -k >/dev/null 2>&1; return 0
}
kill_watcher() {   # the runner's watcher (t1_watch_keep.py) outlives a runner that was TERMed: kill the one writing OUR run dir
  local p
  for p in $(pgrep -u "$(id -u)" -f t1_watch_keep.py 2>/dev/null); do
    tr '\0' '\n' < "/proc/$p/cmdline" 2>/dev/null | grep -Fxq "$O/run/boot0/watch" && kill "$p" 2>/dev/null
  done; return 0
}
make_out() {   # the out dir is NEW and writable, made exclusively (mkdir without -p: two drivers cannot both get it)
  [ -e "$O" ] && { echo "REFUSED: $O exists, not overwriting"; return 1; }
  mkdir -- "$O" 2>/dev/null || { echo "REFUSED: cannot make $O (exists now, or its parent is missing / not writable)"; return 1; }
  [ -d "$O" ] && [ -w "$O" ] && : > "$O/preflight.log" || { echo "REFUSED: $O is not writable"; return 1; }
}
log() { echo "$(date '+%F %T') $*" | tee -a "$O/preflight.log"; }
utc() { echo $(( $(date +%s%N) / 1000000 )); }
on_sig() { log "cancelled"; rc=143; finish; }
qr() { "$T/quiet-required.sh" "$@" >/dev/null 2>&1; }
# want / claim with TERM and INT held over the call: the signal is acted on right AFTER it, once the flag says what this
# invocation now holds (bash runs a trap only after the foreground command anyway; this keeps the flag in step)
held_call() {   # <flag var> <qr args...>: sets the flag to 1 when the call ran (want) / succeeded (claim)
  local v=$1 r; shift
  SIGPEND=0; trap 'SIGPEND=1' TERM INT
  qr "$@"; r=$?
  case "$v" in WANTED) WANTED=1 ;; CLAIMED) [ $r = 0 ] && CLAIMED=1 ;; esac
  trap on_sig TERM INT; [ "$SIGPEND" = 1 ] && on_sig
  return $r
}
acquire() {   # queue, then claim when no title plays; 0 = claimed; 2 = refused (time out or title check failed)
  local w0 ts
  held_call WANTED want $W EXCLUSIVE; log "want rc $?"
  w0=$(date +%s)
  while :; do
    title_state; ts=$?
    [ $ts = 2 ] && { log "REFUSED: the protected-title check failed (cannot tell): not clear"; return 2; }
    [ $ts = 1 ] && held_call CLAIMED claim $W 30 "$WHAT" && break
    [ $(( $(date +%s) - w0 )) -ge $(( WAITMIN * 60 )) ] && { log "REFUSED: no slice in $WAITMIN min"; return 2; }
    sleep "${KJ3P_POLL_S:-60}"
  done
  qr withdraw $W; WANTED=0   # the claim is held; leave the queue
  return 0
}
watch_once() {   # 0 = go on; 1 = stop (marker written, engine of this run killed, runner TERMed)
  local ts; title_state; ts=$?
  [ $ts = 1 ] && return 0
  echo "$(date '+%F %T') STOP: $([ $ts = 0 ] && echo 'a protected title or Sober started' || echo 'the protected-title check failed (not clear)'); engine of this run killed, runner $XP sent TERM" >> "$O/preflight.log"
  echo stop > "$O/.title_stop"; kill_own; [ -n "${XP:-}" ] && kill -TERM "$XP" 2>/dev/null
  return 1
}
final_rc() {   # <rc>: the watchdog stop wins over every other outcome
  [ -e "$O/.title_stop" ] && { log "run STOPPED by the watchdog (protected title / Sober / failed title check): not a run of record" >&2; echo 4; return; }
  echo "$1"
}
# ONE way out, also when cancelled: watchdog and writer killed, runner stopped and reaped (it CONTs and removes what it
# installed, emtest.txt included), the engine of this run stopped, and only then what THIS invocation holds given back.
finish() { trap - EXIT TERM INT
  [ -n "$WD" ] && { kill $WD 2>/dev/null; wait $WD 2>/dev/null; }
  [ -n "$PW" ] && { kill -KILL $PW 2>/dev/null; wait $PW 2>/dev/null; }
  [ -n "$XP" ] && kill -0 $XP 2>/dev/null && { kill -TERM $XP 2>/dev/null; wait $XP 2>/dev/null; }
  [ "$LAUNCHED" = 1 ] && { kill_own; kill_watcher; rm -f $SHM/pose.txt $SHM/cross.txt; }
  [ "$WANTED" = 1 ] && qr withdraw $W
  [ "$CLAIMED" = 1 ] && qr release $W
  log "slice end (exit $rc; withdrew $WANTED, released $CLAIMED)"; exit $rc; }

[ "${KJ3P_FUNCS_ONLY:-0}" = 1 ] && { return 0 2>/dev/null; echo "REFUSED: KJ3P_FUNCS_ONLY=1 is for source only" >&2; exit 2; }

# ------------------------------------------------------------------------------------------------ the run
K="$(cd "$(dirname "$0")" && pwd)"; cd "$K/../../.." || exit 2; P=$PWD
T="$HOME/.claude/overseer/tools"; W=engineperf; O=${1:?out dir}; XROOT=/tmp
TRK=$HOME/Projects/ITRZone/out/p1/cordon_full_track.csv; TRK1=$K/kj3p_track_start.csv
PW=""; XP=""; WD=""; rc=2; WANTED=0; CLAIMED=0; LAUNCHED=0; SIGPEND=0; RID=""; SHM=/dev/shm/itrz_b1
# hash preflight. Every pinned file or nothing starts; missing = refused by name; PIN_ME_* = not registered yet = refused.
bad=0; pins_ok || bad=1
[ "$bad" = 0 ] && . "$P/tools/lib_engine_pids.sh"   # sourced only once its hash matched
[ "$bad" = 0 ] || { echo "REFUSED: pins"; exit 2; }
[ -e run/gamedata/scripts/zz_p1_puppet.script ] && { echo "REFUSED: run/gamedata/scripts/zz_p1_puppet.script exists (a runner left it, or one is running)"; bad=1; }
[ -n "$(engine_main Stance14.exe)" ] && { echo "REFUSED: an engine (Stance14.exe) is already running"; bad=1; }
DISP=${BENCH_DISPLAY:-:141}
disp_free || bad=1
WAITMIN=${KJ3P_WAIT_MIN:-240}; case "$WAITMIN" in ''|*[!0-9]*) echo "REFUSED: KJ3P_WAIT_MIN '$WAITMIN' is not whole minutes"; bad=1 ;; esac
title_state; [ $? = 2 ] && { echo "REFUSED: the protected-title check failed (cannot tell)"; bad=1; }
[ "$bad" = 0 ] || exit 2
[ "${2:-}" = "--preflight-only" ] && { echo "preflight ok"; exit 0; }
make_out || exit 2                       # before any claim: nothing is queued or claimed for a folder we cannot use
export BENCH_DISPLAY=$DISP
# engine seconds from the level load: 15 s warm-up (WAIT_MS, the writer's first pose at track time >= 0) + 440 s of the
# plan + 10 s so the last T2SUM comes after play 440 (registration section 2)
RUN_S=465; WALL_S=900
WHAT="EXCLUSIVE: headless X-Ray-only B1-T2 kj3p emission preflight boot (one engine)"
trap finish EXIT; trap on_sig TERM INT   # BEFORE the want: a cancel while queued leaves the queue
# the slice: queue (want), then claim; every branch on EXIT CODES only (quiet-required.sh header: the exit code is the API)
acquire || { rc=2; finish; }
log "slice granted, kj3p, display $DISP, load $(cut -d' ' -f1-3 /proc/loadavg)"
# everything checked again now that the wait (up to KJ3P_WAIT_MIN) is over, before either child starts
pins_ok >> "$O/preflight.log" 2>&1 || { log "REFUSED after the claim: a pin changed during the wait"; rc=2; finish; }
[ -e run/gamedata/scripts/zz_p1_puppet.script ] && { log "REFUSED after the claim: zz_p1_puppet.script exists"; rc=2; finish; }
[ -n "$(engine_main Stance14.exe)" ] && { log "REFUSED after the claim: an engine (Stance14.exe) is running (not ours: left alone)"; rc=2; finish; }
disp_free >> "$O/preflight.log" 2>&1 || { log "REFUSED after the claim: display $DISP not free"; rc=2; finish; }
title_state; ts=$?; [ $ts = 1 ] || { log "REFUSED after the claim: a protected title or Sober is running, or the check failed ($ts)"; rc=4; finish; }
mkdir -p $SHM; rm -f $SHM/pose.txt $SHM/snap.txt $SHM/dmgtest.txt $SHM/cross.txt
SESS1=$(utc); LAUNCHED=1
python3 -I tools/t2_kill_pose.py $SHM "$O/pose_written.csv" "$TRK1" aw=keep sess=$SESS1 pick=99999 snaplog="$O/writer_snaps.csv" > "$O/pose_writer.log" 2>&1 & PW=$!
env T2_MAXRESTART=0 T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 \
    T2_LOST_MS=5000 T2_LOSTX_MS=120000 T2_OVER_S=120 T2_STALL_PLAY=0 T2_RUN_S=$RUN_S T2_WALL_S=$WALL_S \
    T2_SURGE_PLAY=70 T2_PSI_PLAY=310 \
  tools/t2_xray_kj3.sh "$O/run" none-itr2 > "$O/t2_xray.out" 2>&1 & XP=$!
# watchdog: Caden's box rules: no run while a protected title or Sober runs (or the check cannot tell). Every 10 s while
# the runner lives. It kills this run's engine itself: TERM to the runner does not stop the engine (triage #12).
( trap - EXIT TERM INT; while kill -0 $XP 2>/dev/null; do sleep 10; watch_once || exit 0; done ) & WD=$!
echo "utc_ms,event,detail" > "$O/writer_events.csv"
until [ "$(wc -l < "$O/pose_written.csv" 2>/dev/null || echo 0)" -ge 2 ] || ! kill -0 $XP 2>/dev/null || ! kill -0 $PW 2>/dev/null; do sleep 0.2; done
if [ "$(wc -l < "$O/pose_written.csv" 2>/dev/null || echo 0)" -lt 2 ]; then
  log "NO FIRST POSE: the writer or the runner ended before it (writer log: $(tail -1 "$O/pose_writer.log" 2>/dev/null))"; rc=$(final_rc 3); finish; fi
echo "$(utc),first_write,session $SESS1 pick 99999 track $TRK1" >> "$O/writer_events.csv"
wait $XP; rc=$?; XP=""
log "t2_xray rc=$rc"
rc=$(final_rc $rc)
finish
