#!/usr/bin/env bash
# B1-T2, X-Ray side, with the crash-restart path (perf-harness/PREREG-B1-T2-xray-side-2026-11-23.md, sections 2-4).
# Runs UNDER the joint EXCLUSIVE claim held by the joint driver: it never claims, wants or releases.
# Usage: tools/t2_xray.sh <out_dir> [itr2_proc_pattern]
# - installs the P1 script (run with -p1 -t2_pose: the actor follows pose.txt, no track) into the project's own run/;
# - BOOTS X-Ray (tb_run.sh) one or more times. Every boot gets a new run_id (UTC ms at launch, -t2_run) that the
#   snapshot header carries, and its own watcher (t1_watch.py, unchanged from T1) in <out>/boot<k>/watch;
# - boot 0 only: STALL (SIGSTOP of the engine for T2_STALL_S at snapshot track time T2_STALL_AT) and the PLANNED KILL
#   (SIGKILL of the engine process only, at track time T2_KILL_AT, after the stall) = check T2i;
# - RESTART: after the planned kill, an engine exit before the run end, no first snapshot within 120 s of a launch, or
#   30 s without a new seq (outside the stall), X-Ray is booted again from the SAME save, at most T2_MAXRESTART times.
#   Every restart is logged in <out>/restarts.csv (utc_ms,boot,reason,planned). Only the planned one is planned=1.
# - the run ends when a boot reaches its last engine time (tb_run rc 0); a later boot runs the REMAINING time.
# Exit codes: 0 run ended, no unplanned restart; 6 run ended with unplanned restart(s); 5 restarts used up; 2 refused.
# Env: T2_EXE (RefDet14.exe), T2_RUN_S (1100 engine s), T2_STALL_AT (600; 99999 off), T2_STALL_S (20),
#      T2_KILL_AT (700; 99999 off), T2_MAXRESTART (3), T2_PAUSE (0; 1 adds -t2_pause: NOT usable, see pre-flight 1),
#      T2_FREEZE (sigstop = the bridge freezes X-Ray while the pose is dead/away + freeze), T2_SAVE (cordon1).
set -u
. "$(cd "$(dirname "$0")" && pwd)/lib_engine_pids.sh"   # engine lookup by Wine binary, never a bare name match
cd "$(dirname "$0")/.."; P=$PWD; RUN=$P/run; O=${1:?out_dir}; ITR=${2:-itr2-shadow}
EXE=${T2_EXE:-RefDet14.exe}; SHM=${T2_SHM:-/dev/shm/itrz_b1}; SAVE=${T2_SAVE:-cordon1}
RUN_S=${T2_RUN_S:-1100}; AT=${T2_STALL_AT:-600}; DUR=${T2_STALL_S:-20}; KAT=${T2_KILL_AT:-700}; MAXR=${T2_MAXRESTART:-3}
PFLAG=""; [ "${T2_PAUSE:-0}" = 1 ] && PFLAG="-t2_pause"
# T2_ABSORB=<ms> (with T2_FREEZE=sigstop, engine perf/freeze-absorb): the world clock skips the frozen time.
# T2_ABSORB_SKIP=<k>: the first k freezes stay unarmed (pre-flight 3 control).
[ "${T2_ABSORB:-0}" -gt 0 ] && PFLAG="$PFLAG -t2_absorb $T2_ABSORB -t2_absorb_skip ${T2_ABSORB_SKIP:-0}"
mkdir -p "$O"; log() { echo "$(date -u '+%F %T.%3N')Z $*" | tee -a "$O/t2_xray.log"; }
utc() { echo $(( $(date +%s%N) / 1000000 )); }
"$HOME/.claude/overseer/tools/quiet-required.sh" check 2>/dev/null | grep -q '^HELD:.* EXCLUSIVE\|^HELD:.*B1-T2' || { log "REFUSED: no joint EXCLUSIVE claim held"; exit 2; }
"$P/tools/run_noguard.sh" "$RUN" >/dev/null || { log "REFUSED: noguard"; exit 2; }
# pre-flight E test mode (section 46): its control file may exist only when the pre-flight runner says so (T2_DMG_OK=1)
[ "${T2_DMG_OK:-0}" = 1 ] || { [ -e "$SHM/dmgtest.txt" ] && { rm -f "$SHM/dmgtest.txt"; log "REMOVED a stale test-mode control file (dmgtest.txt)"; }; }
rm -f "$SHM"/snap.txt "$SHM"/meta_t2.csv "$SHM"/hits_t2.csv; mkdir -p "$SHM"      # pose.txt is ITR2's: never touched here
cp "$P/harness/zz_p1_puppet.script" "$RUN/gamedata/scripts/zz_p1_puppet.script"
cleanup() { rm -f "$RUN/gamedata/scripts/zz_p1_puppet.script"; }
trap cleanup EXIT
# the engine = the biggest process that (1) is not the watcher, by PID ($WP), (2) is a Wine process by its /proc/<pid>/exe
# (a wine preloader/loader binary; the watcher's exe is python), (3) names the exe in its command line. Pre-flight 1
# showed WINEPREFIX is NOT in the engine's /proc environ under Wine, so that check found nothing and a hang was not
# killed. engine_all lists every candidate (pid exe rss) for the log.
engine_all() { for p in $(engine_pids "$EXE"); do [ "$p" = "${WP:-}" ] && continue; echo "$p $(readlink /proc/$p/exe 2>/dev/null) $(awk '/VmRSS/{print $2}' /proc/$p/status 2>/dev/null)"; done; }
engine() { engine_all | awk '$2 ~ /wine/ && $3+0 > br {br = $3+0; best = $1} END {print best}'; }
hdr() { head -1 "$SHM/snap.txt" 2>/dev/null | tr -d '\r'; }   # seq,<n>,<t_ms>,<t_track>,<level>,<run_id>
echo "utc_ms,boot,reason,planned" > "$O/restarts.csv"
echo "utc_ms,event,state,pid,game_ms" > "$O/freeze.csv"   # game_ms: X-Ray game time (u32 ms) of the last frame before the stop / the first frame after CONT; both rows written at CONT
T_START=$(date +%s); k=0; unplanned=0; final=""; done_stall=""; done_kill=""
while :; do
  B="$O/boot$k"; mkdir -p "$B"; RID=$(utc)
  # a restart must not inherit the killed boot's last snapshot: the new watcher would stamp it first and then ignore
  # every new snapshot as "not newer" (seq restarts at 1). Found on pre-flight 4's data (boot1/watch: 1 stale row).
  [ $k -gt 0 ] && rm -f "$SHM/snap.txt"
  if [ $k = 0 ]; then TIMES=$RUN_S; else TIMES=$(( RUN_S - ($(date +%s) - T_START) + 25 )); [ $TIMES -lt 60 ] && TIMES=60; fi
  python3 "$P/tools/t1_watch.py" "$SHM/snap.txt" "$B/watch" "bin\\$EXE" "$ITR" > "$B/watch.log" 2>&1 & WP=$!
  log "boot $k: run_id $RID engine_s $TIMES load $(cut -d' ' -f1-3 /proc/loadavg)"
  TB_NOCLAIM=1 TB_EXE=$EXE TB_TIMES=$TIMES TB_TIMEOUT=$(( TIMES + 400 )) tools/tb_run.sh t2b$k "$SAVE" -p1 -t2_pose -t2_run $RID $PFLAG -p1_tag t2 > "$B/tb_run.log" 2>&1 &
  TBP=$!; L0=$(date +%s); first=""; last=""; same=0; why=""; narm=0; gone=0   # meta_t2.csv is rewritten by every boot
  while kill -0 $TBP 2>/dev/null; do
    sleep 1; h=$(hdr); IFS=, read -r _ s _ tt _ rid <<<"$h"
    [ "${rid:-}" = "$RID" ] || { s=""; tt=""; }                         # a snapshot from an earlier boot does not count
    if [ -z "$first" ]; then
      [ -n "$s" ] && { first=1; log "boot $k: first snapshot seq $s after $(( $(date +%s) - L0 )) s; engine $(engine); candidates: $(engine_all | tr '\n' ';')"; }
      [ -z "$first" ] && [ $(( $(date +%s) - L0 )) -ge 120 ] && { why="no snapshot 120 s after launch"; break; }
      continue; fi
    if [ $k = 0 ] && [ -z "$done_stall" ] && [ -n "$tt" ] && awk -v t="$tt" -v a="$AT" 'BEGIN{exit !(t>=a)}'; then
      EP=$(engine); [ -n "$EP" ] || { why="no engine pid found (stall)"; break; }; log "STALL start: seq $s track ${tt}s engine $EP"
      echo "utc_ms,event,seq,track_s" > "$O/stall.csv"; echo "$(utc),STOP,$s,$tt" >> "$O/stall.csv"
      kill -STOP "$EP"; sleep "$DUR"; kill -CONT "$EP"
      echo "$(utc),CONT,$s,$tt" >> "$O/stall.csv"; log "STALL end: SIGCONT sent"; done_stall=1; same=0; last=$s; continue
    fi
    if [ $k = 0 ] && [ -z "$done_kill" ] && [ -n "$tt" ] && awk -v t="$tt" -v a="$KAT" 'BEGIN{exit !(t>=a)}'; then
      EP=$(engine); [ -n "$EP" ] || { why="no engine pid found (kill)"; break; }; KC=$(cat /proc/$EP/comm 2>/dev/null); KR=$(awk '/VmRSS/{print $2}' /proc/$EP/status 2>/dev/null); KU=$(utc); kill -KILL "$EP"; done_kill=1
      echo "$KU,$k,planned kill (T2i) seq $s track ${tt}s pid $EP,1" >> "$O/restarts.csv"; log "PLANNED KILL: seq $s track ${tt}s pid $EP comm $KC rss_kb $KR"
      sleep 1; WINEPREFIX=$RUN/prefix wineserver -k >/dev/null 2>&1   # pre-flight 2: the Wine wrappers kept tb_run waiting 10 min
      why="planned"; break
    fi
    if [ -n "$s" ] && [ "$s" = "$last" ]; then same=$((same+1)); else same=0; fi; last=$s
    # ITRZone review: under freeze X-Ray may legitimately stop exporting while ITR2 is dead/away (Hub ~20 s + reload),
    # so the 30 s rule is skipped while pose.txt says dead/away + freeze; it applies again once the pose says play.
    IFS=, read -r _ _ _ pst _ paw <<<"$(head -1 "$SHM/pose.txt" 2>/dev/null | tr -d '\r')"
    # T2_FREEZE=sigstop (pre-flight 1: device():pause stops Lua for good, so X-Ray cannot unpause itself): the BRIDGE
    # freezes X-Ray by SIGSTOP of the engine while the pose says dead/away + freeze, and SIGCONT when it says play
    # (T1: X-Ray survived two 20 s SIGSTOPs). Logged in freeze.csv (utc_ms,event,pose_state).
    if [ "${paw:-}" = freeze ] && { [ "${pst:-}" = dead ] || [ "${pst:-}" = away ]; }; then
      if [ -z "${fz:-}" ]; then fz=1; log "freeze hold: pose state $pst, heartbeat skipped"
        if [ "${T2_FREEZE:-}" = sigstop ]; then
          # with T2_ABSORB, wait (<= 3 s) for the script's ABSORB_ARM/ABSORB_SKIP line, so the engine is armed BEFORE the stop
          gms=""; if [ "${T2_ABSORB:-0}" -gt 0 ]; then for _w in 1 2 3 4 5 6; do
              aline=$(grep -a '^ABSORB_ARM\|^ABSORB_SKIP' "$SHM/meta_t2.csv" 2>/dev/null | sed -n "$(( ${narm:-0} + 1 ))p")
              [ -n "$aline" ] && break; sleep 0.5; done
            [ -n "$aline" ] && { narm=$(( ${narm:-0} + 1 )); gms=$(cut -d, -f4 <<<"$aline"); log "freeze: ${aline%%,*} seen before STOP"; } || log "freeze: NO arm line within 3 s (stopping anyway)"; fi
          FZP=$(engine); [ -n "$FZP" ] && kill -STOP "$FZP" && { SU=$(utc); SST=$pst; log "freeze: STOP pid $FZP"; }; fi; fi
      same=0
    elif [ -n "${fz:-}" ] && [ -n "${pst:-}" ] && { [ "${paw:-}" = keep ] || [ "${paw:-}" = freeze ]; }; then fz=""; log "freeze hold end: pose state $pst"   # a torn/empty/truncated pose read (awaysim not exactly keep or freeze) never ends a freeze
      if [ -n "${FZP:-}" ]; then nres=$(grep -a -c '^RESUMED_GAP' "$SHM/meta_t2.csv" 2>/dev/null); kill -CONT "$FZP"; CU=$(utc)
        # game_ms after the resume = the RESUMED_GAP line the script writes on its first frame (wait <= 3 s for it)
        gms=""; for _w in 1 2 3 4 5 6; do rl=$(grep -a '^RESUMED_GAP' "$SHM/meta_t2.csv" 2>/dev/null | sed -n "$(( ${nres:-0} + 1 ))p"); [ -n "$rl" ] && break; sleep 0.5; done
        gpre=""; [ -n "$rl" ] && { gms=$(cut -d, -f5 <<<"$rl"); gpre=$(cut -d, -f7 <<<"$rl"); }
        # STOP row: game time of X-Ray's LAST frame before the gap (pre-flight 3: the arming time was up to ~1 s
        # earlier, which at factor 6 read as 3 s of game time that the absorb never had to remove)
        echo "$SU,STOP,$SST,$FZP,${gpre:-}" >> "$O/freeze.csv"; echo "$CU,CONT,$pst,$FZP,${gms:-}" >> "$O/freeze.csv"; FZP=""; fi; same=0; fi
    # T3 R3: while the pose says `cross`, X-Ray is changing level (no export during the load): heartbeat skipped, no stop
    if [ "${pst:-}" = cross ]; then [ -z "${cx:-}" ] && { cx=1; log "cross hold: heartbeat skipped (seq ${s:-none})"; }; same=0
    elif [ -n "${cx:-}" ] && [ -n "${pst:-}" ]; then cx=""; log "cross hold end: pose state $pst (seq ${s:-none})"; fi
    [ $same -ge 30 ] && { why="no new seq for 30 s"; break; }
    if [ -z "${FZP:-}" ] && [ -z "$(engine_main "$EXE")" ]; then gone=$(( ${gone:-0} + 1 )); [ $gone -ge 3 ] && { kill $WP 2>/dev/null; break; }; else gone=0; fi
    # (engine gone: stop the watcher so tb_run can finish; tb_run's own rc then decides: 0 = normal end, else a crash)
  done
  [ -n "${FZP:-}" ] && { kill -CONT "$FZP" 2>/dev/null; FZP=""; fz=""; }
  [ -n "$why" ] && [ "$why" != planned ] && { EP=$(engine); [ -n "$EP" ] && kill -KILL "$EP"; sleep 1; WINEPREFIX=$RUN/prefix wineserver -k >/dev/null 2>&1; }
  # pre-flight 3: after the planned kill tb_run still waited 7.5 min, because its own pgrep -f "bin\\<exe>" matched the
  # WATCHER (its argv names the exe). Stop the watcher first whenever the boot is ending early.
  [ -n "$why" ] && { kill $WP 2>/dev/null; wait $WP 2>/dev/null; }
  wait $TBP; trc=$?
  kill $WP 2>/dev/null; wait $WP 2>/dev/null
  cp "$SHM"/meta_t2.csv "$B/" 2>/dev/null; cp "$SHM"/hits_t2.csv "$B/" 2>/dev/null; cp -r "$P/out/tb_t2b$k" "$B/tb" 2>/dev/null
  [ -z "$why" ] && [ $trc != 0 ] && why0="engine exited (tb_run rc $trc)" || why0="${why:-engine run ended}"
  log "boot $k end: tb_run rc=$trc reason=$why0"
  if [ -z "$why" ] && [ $trc = 0 ]; then final="ran to the end"; break; fi
  if [ "$why" != planned ]; then unplanned=$((unplanned+1)); echo "$(utc),$k,${why:-engine exited (tb_run rc $trc)},0" >> "$O/restarts.csv"; fi
  [ $k -ge $MAXR ] && { final="restarts used up"; break; }
  "$HOME/.claude/overseer/tools/protected-title-check.sh" >/dev/null 2>&1 && { final="protected title running"; break; }
  k=$((k+1)); log "RESTART -> boot $k"
done
rc=0; [ $unplanned -gt 0 ] && rc=6; [ "$final" != "ran to the end" ] && rc=5
log "end: $final; boots $((k+1)); unplanned restarts $unplanned; stall ${done_stall:-no}; planned kill ${done_kill:-no}; rc=$rc"
exit $rc
