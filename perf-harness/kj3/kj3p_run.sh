#!/usr/bin/env bash
# kj3p: X-ONLY emission preflight driver (kj3/PREREG-kj3p-emission-preflight-2026-11-26.md). Made from tools/t3_kw1.sh (cecc098f).
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
# Pass 3: one kj3p driver at a time (flock on out/kj3p_state/driver.lock, refused by name). It does NOT serialize against
# EnginePerf's other drivers (wu2_run.sh etc.): stated limit "EnginePerf never starts kj3p while another of its drivers is
# queued or running"; the refusal to claim while any engineperf claim is held stays. Xvfb / openbox / watcher are waited
# for and counted in the final "nothing of this run left" check; user.ltx is put back from a run-local marker.
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
  pin "$TRK1"                                  0a8bce3976ff0ef81fb0b5ded24c878088e83410f988ecf52d43fa148eb5a5e9 || b=1
  pin "$K/kj3p_check.py"                       7fa0ccf1d411f106b40a9951747729c770499edb0ad47f1cd1b1093a3c4d891b || b=1
  pin "$K/kj3p_score.sh"                       1b24dc7423c01f7af00dc75876fb5145f24a1be1a4485bc6e5ac43439c573024 || b=1
  pin "$K/kj3p_selftest.sh"                    1f87bf69f68dc058e7af424eb793adfb1b971189fe89eaa28cd5941bde3aeb0a || b=1
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
is_own() {   # <pid>: 1 unless the process belongs to THIS run: DISPLAY or BENCH_DISPLAY in its environment is ours (the
             # display was refused at start if it existed; tb_run exports DISPLAY, the driver BENCH_DISPLAY), or its
             # command line has -t2_run <this run's id> (t2_xray_kj3.sh:92 passes it to tb_run and the engine)
  local p=$1
  [ -r "/proc/$p/environ" ] && tr '\0' '\n' 2>/dev/null < "/proc/$p/environ" | grep -Eqx -- "(BENCH_)?DISPLAY=$DISP" && return 0
  [ -n "${RID:-}" ] && tr '\0' ' ' 2>/dev/null < "/proc/$p/cmdline" | grep -Eq -- "(^| )-t2_run $RID( |\$)" && return 0
  return 1
}
# candidate lists (the selftest replaces them with its fake processes); a candidate is killed only if is_own says so
wine_pids() { local d; for d in /proc/[0-9]*; do case "$(readlink "$d/exe" 2>/dev/null)" in *wine*) echo "${d#/proc/}" ;; esac; done; }
launch_pids() { pgrep -u "$(id -u)" -f 'tools/tb_run\.sh' 2>/dev/null; }
xsrv_pids() { pgrep -u "$(id -u)" -x Xvfb 2>/dev/null; pgrep -u "$(id -u)" -x openbox 2>/dev/null; }
own_of() { local p; for p in $("$@"); do [ "$p" = "$$" ] || [ "$p" = "${BASHPID:-}" ] && continue; is_own "$p" && echo "$p"; done; }
run_id() { RID=$(sed -n 's/^.* boot 0: run_id \([0-9][0-9]*\) .*$/\1/p' "$O/run/t2_xray.log" 2>/dev/null | head -1); }
stop_run() {   # 0 = no process of this run is left. LAUNCHER FIRST (tb_run of this run, KILL: its TERM would run its EXIT
               # cleanup, whose wineserver -k hits the whole shared prefix), then every Wine process of this run (engine,
               # explorer, helpers; its wineserver gets TERM), scanned again after a wait until two scans in a row are empty;
               # then this run's Xvfb / openbox and watcher: TERM, a bounded wait, KILL, and a FINAL check over all five
               # classes (pass 3 #1): anything of this run still alive = 1 (finish then keeps the claim). No prefix-wide
               # wineserver -k here (pass 2, #2).
  local i clean=0 l e s x left
  run_id
  for i in 1 2 3 4 5 6 7 8; do
    l=$(own_of launch_pids | tr '\n' ' '); e=""; s=""
    for x in $(own_of wine_pids); do case "$(cat "/proc/$x/comm" 2>/dev/null)" in wineserver*) s="$s $x" ;; *) e="$e $x" ;; esac; done
    if [ -z "$l$e$s" ]; then clean=$((clean + 1)); [ $clean -ge 2 ] && break
    else clean=0
      # the marker is written BEFORE the kill: whoever runs cleanup next (this subshell or the parent) restores user.ltx
      [ -n "$l" ] && { echo "$(date '+%F %T') tb_run pid $l KILLed: put out/user.ltx.orig back" > "$O/.restore_user_ltx"
                       log "LAUNCHER of this run (tb_run) pid $l: KILL"; kill -KILL $l 2>/dev/null; }
      [ -n "$e" ] && { log "WINE processes of this run pid$e: KILL"; kill -KILL $e 2>/dev/null; }
      [ -n "$s" ] && { log "wineserver of this run pid$s: TERM"; kill -TERM $s 2>/dev/null; }
    fi
    sleep "${KJ3P_SCAN_S:-2}"
  done
  x=$(rest_pids); [ -n "$x" ] && { log "Xvfb / openbox / watcher of this run pid $x: TERM"; kill -TERM $x 2>/dev/null; }
  for i in $(seq 1 25); do [ -z "$(rest_pids)" ] && break; sleep 0.2; done   # <= 5 s
  x=$(rest_pids); [ -n "$x" ] && { log "Xvfb / openbox / watcher of this run pid $x: still alive after 5 s, KILL"; kill -KILL $x 2>/dev/null; sleep 0.5; }
  restore_ltx
  left="$(own_of launch_pids | tr '\n' ' ')$(own_of wine_pids | tr '\n' ' ')$(rest_pids)"
  [ -z "${left// /}" ] && [ "$clean" -ge 2 ] && return 0   # pass 4: two empty scans in a row AND nothing left
  [ -z "${left// /}" ] && { log "scan budget used up without two empty scans in a row: claim kept"; return 1; }
  log "LEFT ALIVE (launcher / Wine / Xvfb / openbox / watcher of this run): $left"; return 1
}
watch_pids() {   # the runner's watcher (t1_watch_keep.py) writing OUR run dir: it outlives a runner that was TERMed
  local p
  for p in $(pgrep -u "$(id -u)" -f t1_watch_keep.py 2>/dev/null); do
    tr '\0' '\n' 2>/dev/null < "/proc/$p/cmdline" | grep -Fxq "$O/run/boot0/watch" && echo "$p"
  done; return 0
}
rest_pids() { { own_of xsrv_pids; watch_pids; } | tr '\n' ' '; }
restore_ltx() {   # tb_run's own cleanup (tools/tb_run.sh:33) puts the pristine user.ltx back; a KILLed tb_run did not.
                  # Driven by the run-local marker, so it also happens when the watchdog subshell did the kill (pass 3 #5).
  [ -e "$O/.restore_user_ltx" ] || return 0
  if [ -f "$P/out/user.ltx.orig" ] && [ -d "$P/run/appdata" ] && cp -f "$P/out/user.ltx.orig" "$P/run/appdata/user.ltx"; then
    mv -f "$O/.restore_user_ltx" "$O/.restore_user_ltx.done"; log "user.ltx put back from out/user.ltx.orig"
  else log "user.ltx NOT put back (copy failed): marker kept"; fi
}
take_lock() {   # one kj3p driver at a time (pass 3 #2): exclusive, non-blocking flock on a fixed file in the main project's
                # state area, held on fd 9 for the driver's whole life (children inherit it: it ends with the last of them)
  mkdir -p "$(dirname "$LOCKF")" 2>/dev/null || { echo "REFUSED: cannot make $(dirname "$LOCKF")"; return 1; }
  exec 9>>"$LOCKF" || { echo "REFUSED: cannot open $LOCKF"; return 1; }
  flock -n 9 || { echo "REFUSED: another kj3p driver holds $LOCKF"; exec 9>&-; return 1; }
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
# Ownership read of the box flag. quiet-required.sh has no exit-code way to say who holds a slot or with which <what>
# (triage, extra); its `check` TEXT (HELD: / SLOTn: <date> <time> <worker> <mins> <what>) is used for this test ONLY.
flag_lines() { "$T/quiet-required.sh" check 2>/dev/null | sed -n 's/^\(HELD\|SLOT[0-9]*\): //p'; }
# any claim under our name, EXCEPT the queue's own 5-minute handoff pre-claim (amendment 1, 2026-11-27: that one is
# written in our name for us to claim over; quiet-required.sh matches the same text, *"handoff pre-claim"*, :534 :628)
w_holds() { flag_lines | awk -v w="$W" '$3 == w && !index($0, "handoff pre-claim") {f = 1} END {exit !f}'; }
mine_held() { flag_lines | awk -v w="$W" -v t="$TOKEN" '$3 == w && index($0, t) {f = 1} END {exit !f}'; }   # OUR claim
# want / claim with TERM and INT held over the call: the signal is acted on right AFTER it, once the flags say what this
# invocation now may hold. A claim can be committed although the call returns non-zero or is cut off (pass 2, #3), so
# CLAIMTRY=1 from the moment the call starts; release_mine decides from the claim's receipt or the flag what to give back.
held_call() {   # <flag var> <qr args...>
  local v=$1 r; shift
  SIGPEND=0; trap 'SIGPEND=1' TERM INT
  case "$v" in
    WANTED) qr "$@"; r=$?; WANTED=1 ;;
    CLAIMED) CLAIMTRY=1; "$T/quiet-required.sh" "$@" >> "$O/claim_calls.txt" 2>&1; r=$?; [ $r = 0 ] && CLAIMED=1 ;;
  esac
  trap on_sig TERM INT; [ "$SIGPEND" = 1 ] && on_sig
  return $r
}
release_mine() {   # after ANY claim call: release the claim this run made, never another claim under the same name
  [ "$CLAIMTRY" = 1 ] || return 0
  local id; id=$(sed -n 's/^claim-id: \([A-Za-z0-9._-][A-Za-z0-9._-]*\)$/\1/p' "$O/claim_calls.txt" 2>/dev/null | tail -1)
  if [ -n "$id" ]; then qr release $W "$id"; log "released claim $id (rc $?)"; RELEASED=1; return 0; fi
  if mine_held; then qr release $W; log "released a claim with this run's token $TOKEN but no receipt (rc $?)"; RELEASED=1; return 0; fi
  log "no claim of this run found (token $TOKEN): nothing released"; return 0
}
acquire() {   # queue, then claim when no title plays; 0 = claimed; 2 = refused (time out or title check failed)
  local w0 ts said=""
  held_call WANTED want $W EXCLUSIVE; log "want rc $?"
  w0=$(date +%s)
  while :; do
    title_state; ts=$?
    [ $ts = 2 ] && { log "REFUSED: the protected-title check failed (cannot tell): not clear"; return 2; }
    if [ $ts = 1 ] && w_holds; then   # `claim` would renew / nest into that claim (quiet-required.sh:1007, :1116)
      [ -n "$said" ] || { log "waiting: a claim under '$W' is already held (not this run's); not claiming beside it"; said=1; }
    elif [ $ts = 1 ]; then held_call CLAIMED claim $W 30 "$WHAT" && break; fi
    [ $(( $(date +%s) - w0 )) -ge $(( WAITMIN * 60 )) ] && { log "REFUSED: no slice in $WAITMIN min"; return 2; }
    sleep "${KJ3P_POLL_S:-60}"
  done
  qr withdraw $W; WANTED=0   # the claim is held; leave the queue
  return 0
}
watch_once() {   # 0 = go on; 1 = stop: marker, runner TERMed and gone (KILL after 5 s), then everything of this run stopped
  local ts i; title_state; ts=$?
  [ $ts = 1 ] && return 0
  echo "$(date '+%F %T') STOP: $([ $ts = 0 ] && echo 'a protected title or Sober started' || echo 'the protected-title check failed (not clear)'); runner $XP sent TERM, then this run's launcher and engine stopped" >> "$O/preflight.log"
  echo stop > "$O/.title_stop"
  if [ -n "${XP:-}" ]; then kill -TERM "$XP" 2>/dev/null   # the runner first: it must not launch tb_run after the scan
    for i in $(seq 1 25); do kill -0 "$XP" 2>/dev/null || break; sleep 0.2; done; kill -KILL "$XP" 2>/dev/null; fi
  stop_run
  return 1
}
final_rc() {   # <rc>: the watchdog stop wins over every other outcome
  [ -e "$O/.title_stop" ] && { log "run STOPPED by the watchdog (protected title / Sober / failed title check): not a run of record" >&2; echo 4; return; }
  echo "$1"
}
# ONE way out, also when cancelled: the watchdog stopped and reaped FIRST and the exit status decided only then (a late
# stop marker gives 4, pass 2 #4); writer killed; runner stopped and reaped (it CONTs and removes what it installed); every
# process of this run stopped (stop_run); only then what THIS invocation holds given back, the claim only if nothing of
# this run is left alive (else it is kept and lapses after its 30 min: never a free box under a live engine).
finish() { trap - EXIT TERM INT
  local left=0
  [ -n "$WD" ] && { kill $WD 2>/dev/null; wait $WD 2>/dev/null; }
  rc=$(final_rc "$rc")
  [ -n "$PW" ] && { kill -KILL $PW 2>/dev/null; wait $PW 2>/dev/null; }
  [ -n "$XP" ] && kill -0 $XP 2>/dev/null && { kill -TERM $XP 2>/dev/null; wait $XP 2>/dev/null; }
  [ "$LAUNCHED" = 1 ] && { stop_run || left=1; [ "$SHM" = /dev/shm/itrz_b1 ] && rm -f /dev/shm/itrz_b1/pose.txt /dev/shm/itrz_b1/cross.txt; }
  [ "$WANTED" = 1 ] && qr withdraw $W
  restore_ltx   # also when the watchdog subshell killed tb_run and was itself stopped before it put user.ltx back
  if [ $left = 1 ]; then log "CLAIM KEPT: a process of this run is still alive (see LEFT ALIVE); the claim lapses by its minutes"
  else release_mine; fi
  log "slice end (exit $rc; withdrew $WANTED, claim tried $CLAIMTRY, released $RELEASED)"; exit $rc; }

[ "${KJ3P_FUNCS_ONLY:-0}" = 1 ] && { return 0 2>/dev/null; echo "REFUSED: KJ3P_FUNCS_ONLY=1 is for source only" >&2; exit 2; }

# ------------------------------------------------------------------------------------------------ the run
K="$(cd "$(dirname "$0")" && pwd)"; cd "$K/../../.." || exit 2; P=$PWD
T="$HOME/.claude/overseer/tools"; W=engineperf; O=${1:?out dir}; XROOT=/tmp
TRK=$HOME/Projects/ITRZone/out/p1/cordon_full_track.csv; TRK1=$K/kj3p_track_start.csv; LOCKF=$P/out/kj3p_state/driver.lock
PW=""; XP=""; WD=""; rc=2; WANTED=0; CLAIMED=0; CLAIMTRY=0; RELEASED=0; LAUNCHED=0; SIGPEND=0; RID=""; SHM=/dev/shm/itrz_b1
TOKEN="kj3p-$(date +%Y%m%d%H%M%S)-$$"   # unique per invocation: in the claim's <what>, so only THIS run's claim is ever released
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
take_lock || exit 2                      # before the out dir and any claim (pass 3 #2)
make_out || exit 2                       # before any claim: nothing is queued or claimed for a folder we cannot use
export BENCH_DISPLAY=$DISP
# engine seconds from the level load: 15 s warm-up (WAIT_MS, the writer's first pose at track time >= 0) + 440 s of the
# plan + 10 s so the last T2SUM comes after play 440 (registration section 2)
RUN_S=465; WALL_S=900
WHAT="EXCLUSIVE: headless X-Ray-only B1-T2 kj3p emission preflight boot (one engine) $TOKEN"
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
  log "NO FIRST POSE: the writer or the runner ended before it (writer log: $(tail -1 "$O/pose_writer.log" 2>/dev/null))"; rc=3; finish; fi
echo "$(utc),first_write,session $SESS1 pick 99999 track $TRK1" >> "$O/writer_events.csv"
wait $XP; rc=$?; XP=""
log "t2_xray rc=$rc"
finish   # finish decides 4 if the watchdog stopped the run (after reaping it)
