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
# Exit codes: 0 run ended, no unplanned restart; 6 run ended with unplanned restart(s); 5 restarts used up; 2 refused; 7 refused: axr_options restore failed (em1).
# Env: T2_EXE (RefDet14.exe), T2_RUN_S (1100 engine s), T2_STALL_AT (600; 99999 off), T2_STALL_S (20),
#      T2_KILL_AT (700; 99999 off), T2_MAXRESTART (3), T2_PAUSE (0; 1 adds -t2_pause: NOT usable, see pre-flight 1),
#      T2_FREEZE (sigstop = the bridge freezes X-Ray while the pose is dead/away + freeze), T2_SAVE (cordon1).
# Run length and limits (section 58: arm A's engine was killed by a limit counted from the wrong start):
#   T2_RUN_S counts in the engine from EACH LEVEL LOAD (a level change starts a new Lua state and its counter).
#   T2_WALL_S = tb_run's wall limit for boot 0, from the LAUNCH. Default T2_RUN_S + 400 (as before). For a run with
#     level changes set it from the schedule: boot + every stay + every crossing + T2_RUN_S + a margin.
#   T2_OVER_S (0 = off, as before) = the engine is killed (unplanned restart) when it is still running T2_RUN_S +
#     T2_OVER_S seconds after the last level load seen in the snapshot header; time stopped by a freeze is not counted.
#   The 30 s no-new-snapshot rule is unchanged and still kills a stall.
set -u
. "$(cd "$(dirname "$0")" && pwd)/lib_engine_pids.sh"   # engine lookup by Wine binary, never a bare name match
cd "$(dirname "$0")/.."; P=$PWD; RUN=$P/run; O=${1:?out_dir}; ITR=${2:-itr2-shadow}
EXE=${T2_EXE:-RefDet14.exe}; SHM=${T2_SHM:-/dev/shm/itrz_b1}; SAVE=${T2_SAVE:-cordon1}
RUN_S=${T2_RUN_S:-1100}; AT=${T2_STALL_AT:-600}; DUR=${T2_STALL_S:-20}; KAT=${T2_KILL_AT:-700}; MAXR=${T2_MAXRESTART:-3}
WALL_S=${T2_WALL_S:-0}; OVER_S=${T2_OVER_S:-0}
# kj1: T2_STALL_PLAY=<s> (0 = off): boot 0 only, stop the engine for T2_STALL_S once, <s> seconds after the first NEW
# complete pose with state play that this runner read in this boot, counted from that pose's own utc_ms (header field 3).
# Rows go to stall.csv as for T2_STALL_AT; the two triggers share the "done" latch, so use one of them.
SPLAY=${T2_STALL_PLAY:-0}
# kj3 Stage 2: T2_SURGE_PLAY / T2_PSI_PLAY = <s> after the first play pose's utc_ms (the T2_STALL_PLAY clock; default 99999 =
# never): the runner appends `surge` / `psi` to $SHM/emtest.txt once (the reader then calls the manager's own start function)
# and logs it in emforce.csv. emtest.txt is created EMPTY before the engine starts when either is set (the reader enables its
# test mode only if the file is there at game start) and removed at start and at cleanup. T2_STALL_PLAY=99999 = no stop at all.
SURGE_PLAY=${T2_SURGE_PLAY:-99999}; PSI_PLAY=${T2_PSI_PLAY:-99999}
[ "$SPLAY" = 99999 ] && SPLAY=0
EMPLAY=""; { [ "$SURGE_PLAY" != 99999 ] || [ "$PSI_PLAY" != 99999 ]; } && EMPLAY=1
PFLAG=""; [ "${T2_PAUSE:-0}" = 1 ] && PFLAG="-t2_pause"
# T2_ABSORB=<ms> (with T2_FREEZE=sigstop, engine perf/freeze-absorb): the world clock skips the frozen time.
# T2_ABSORB_SKIP=<k>: the first k freezes stay unarmed (pre-flight 3 control).
[ "${T2_ABSORB:-0}" -gt 0 ] && PFLAG="$PFLAG -t2_absorb $T2_ABSORB -t2_absorb_skip ${T2_ABSORB_SKIP:-0}"
# LOST (design note 2026-11-24): no new pose line for T2_LOST_MS (5000) after a play/dead/away pose, or T2_LOSTX_MS
# (120000) after a cross pose = the pose is LOST and handled like "away" with the last pose's keep/freeze policy.
# 0 = off (mutation / old behaviour). Never after an end pose, and never before the first pose change of this run.
LOST_MS=${T2_LOST_MS:-5000}; LOSTX_MS=${T2_LOSTX_MS:-120000}
[ "$LOST_MS" -gt 0 ] && PFLAG="$PFLAG -t2_lost $LOST_MS"; [ "$LOSTX_MS" -gt 0 ] && PFLAG="$PFLAG -t2_lostx $LOSTX_MS"
# T2_V2RESET (1; 0 = off, the behaviour before arm D): while the pose is lost, a pose WITHOUT a session number and with a
# seq below the last one is taken as a restarted old ITR2 mod (see -t2_v2reset in the script)
[ "${T2_V2RESET:-1}" = 1 ] && PFLAG="$PFLAG -t2_v2reset"
mkdir -p "$O"; log() { echo "$(date -u '+%F %T.%3N')Z $*" | tee -a "$O/t2_xray.log"; }
utc() { echo $(( $(date +%s%N) / 1000000 )); }
"$HOME/.claude/overseer/tools/quiet-required.sh" check 2>/dev/null | grep -q '^HELD:.* EXCLUSIVE\|^HELD:.*B1-T2' || { log "REFUSED: no joint EXCLUSIVE claim held"; exit 2; }
# em1 (BEGIN axr restore): put the game's option file back right after the claim check, before any other write to run/
# or the shm folder and before the first boot (Overseer order 2026-11-27: each exe rewrites its `version` line).
# The SOURCE is hashed first (a refusal leaves configs/axr_options.ltx untouched), then copied, then the result hashed;
# any failure refuses with exit 7.
AXR_SRC="$RUN/appdata/axr_options_play.ltx"; AXR_DST="$RUN/gamedata/configs/axr_options.ltx"
AXR_SHA=27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29
[ -f "$AXR_SRC" ] || { log "REFUSED: axr_options restore: source $AXR_SRC missing"; exit 7; }
AXR_GOT=$(sha256sum < "$AXR_SRC" 2>/dev/null | cut -c1-64)
[ "$AXR_GOT" = "$AXR_SHA" ] || { log "REFUSED: axr_options restore: sha256 of source $AXR_SRC is ${AXR_GOT:-unreadable}, want $AXR_SHA"; exit 7; }
cp "$AXR_SRC" "$AXR_DST" || { log "REFUSED: axr_options restore: copy to $AXR_DST failed"; exit 7; }
AXR_GOT=$(sha256sum < "$AXR_DST" 2>/dev/null | cut -c1-64)
[ "$AXR_GOT" = "$AXR_SHA" ] || { log "REFUSED: axr_options restore: sha256 of $AXR_DST is ${AXR_GOT:-unreadable}, want $AXR_SHA"; exit 7; }
log "axr_options restored from $AXR_SRC (sha256 $AXR_SHA)"
# em1 (END axr restore)
"$P/tools/run_noguard.sh" "$RUN" >/dev/null || { log "REFUSED: noguard"; exit 2; }
# pre-flight E test mode (section 46): its control file may exist only when the pre-flight runner says so (T2_DMG_OK=1)
[ "${T2_DMG_OK:-0}" = 1 ] || { [ -e "$SHM/dmgtest.txt" ] && { rm -f "$SHM/dmgtest.txt"; log "REMOVED a stale test-mode control file (dmgtest.txt)"; }; }
rm -f "$SHM"/snap.txt "$SHM"/meta_t2.csv "$SHM"/hits_t2.csv; mkdir -p "$SHM"      # pose.txt is ITR2's: never touched here
rm -f "$SHM/emtest.txt"; if [ -n "$EMPLAY" ]; then : > "$SHM/emtest.txt"; echo "utc_ms,wall_utc,kind,ms_after_play0" > "$O/emforce.csv"; log "emtest.txt created (surge at ${SURGE_PLAY} s, psi at ${PSI_PLAY} s after the first play pose; 99999 = never)"; fi
SRC="$P/harness/zz_p1_puppet_kill7.script"   # kj3 (kill6 + Stage 2 emission lines), was kill5; kj2 (am138), was kill4; kw1: the kill channel script (pose v4) is installed beside the frozen one, under its own name
cp "$SRC" "$RUN/gamedata/scripts/zz_p1_puppet.script" && cmp -s "$SRC" "$RUN/gamedata/scripts/zz_p1_puppet.script" || { log "REFUSED: could not install $SRC"; exit 2; }
# a runner that is cancelled must not leave a stopped engine behind: CONT whatever this runner stopped
cleanup() { [ -n "${FZP:-}" ] && kill -CONT "$FZP" 2>/dev/null; [ -n "${STP:-}" ] && kill -CONT "$STP" 2>/dev/null; rm -f "$RUN/gamedata/scripts/zz_p1_puppet.script"; rm -f "$SHM/emtest.txt"; }
trap cleanup EXIT; trap 'exit 143' TERM INT
# first line of pose.txt, but only of a COMPLETE record (first line seq,<n>,... and last line end,<n>); else empty
posehdr() { awk '{ sub(/\r$/, "") } NR == 1 { h = $0; split($0, a, ","); if (a[1] != "seq") exit } NF { l = $0 } END { if (h != "" && l == "end," a[2]) print h }' "$SHM/pose.txt" 2>/dev/null; }
# the engine = the biggest process that (1) is not the watcher, by PID ($WP), (2) is a Wine process by its /proc/<pid>/exe
# (a wine preloader/loader binary; the watcher's exe is python), (3) names the exe in its command line. Pre-flight 1
# showed WINEPREFIX is NOT in the engine's /proc environ under Wine, so that check found nothing and a hang was not
# killed. engine_all lists every candidate (pid exe rss) for the log.
engine_all() { for p in $(engine_pids "$EXE"); do [ "$p" = "${WP:-}" ] && continue; echo "$p $(readlink /proc/$p/exe 2>/dev/null) $(awk '/VmRSS/{print $2}' /proc/$p/status 2>/dev/null)"; done; }
engine() { engine_all | awk '$2 ~ /wine/ && $3+0 > br {br = $3+0; best = $1} END {print best}'; }
hdr() { head -1 "$SHM/snap.txt" 2>/dev/null | tr -d '\r'; }   # seq,<n>,<t_ms>,<t_track>,<level>,<run_id>
echo "utc_ms,boot,reason,planned" > "$O/restarts.csv"
echo "utc_ms,age_ms,state,policy" > "$O/lost.csv"
echo "utc_ms,event,state,pid,game_ms" > "$O/freeze.csv"   # game_ms: X-Ray game time (u32 ms) of the last frame before the stop / the first frame after CONT; both rows written at CONT
T_START=$(date +%s); k=0; unplanned=0; final=""; done_stall=""; done_kill=""
while :; do
  B="$O/boot$k"; mkdir -p "$B"; RID=$(utc)
  # a restart must not inherit the killed boot's last snapshot: the new watcher would stamp it first and then ignore
  # every new snapshot as "not newer" (seq restarts at 1). Found on pre-flight 4's data (boot1/watch: 1 stale row).
  [ $k -gt 0 ] && rm -f "$SHM/snap.txt"
  # a restart loads the save: a crossing marker left by an engine that died inside a level load must not reach the new script
  [ $k -gt 0 ] && [ -s "$SHM/cross.txt" ] && { log "boot $k: crossing marker emptied (was: $(head -1 "$SHM/cross.txt" | tr -d '\r'))"; : > "$SHM/cross.txt"; }
  if [ $k = 0 ]; then TIMES=$RUN_S; else TIMES=$(( RUN_S - ($(date +%s) - T_START) + 25 )); [ $TIMES -lt 60 ] && TIMES=60; fi
  T1_KEEP_POSE="$SHM/pose.txt" python3 "$P/tools/t1_watch_keep.py" "$SHM/snap.txt" "$B/watch" "bin\\$EXE" "$ITR" > "$B/watch.log" 2>&1 & WP=$!
  log "boot $k: run_id $RID engine_s $TIMES load $(cut -d' ' -f1-3 /proc/loadavg)"
  TBLIM=$(( TIMES + 400 )); [ $k = 0 ] && [ "$WALL_S" -gt 0 ] && TBLIM=$WALL_S
  log "boot $k: wall limit ${TBLIM} s from launch; over-run limit $([ "$OVER_S" -gt 0 ] && echo "$(( TIMES + OVER_S )) s from the last level load" || echo off)"
  TB_NOCLAIM=1 TB_EXE=$EXE TB_TIMES=$TIMES TB_TIMEOUT=$TBLIM tools/tb_run.sh t2b$k "$SAVE" -p1 -t2_pose -t2_run $RID $PFLAG -p1_tag t2 > "$B/tb_run.log" 2>&1 &
  TBP=$!; L0=$(date +%s); first=""; last=""; same=0; why=""; narm=0; gone=0; ppl=$(posehdr); pseen=""; pchg=0; lost=""; plv=""; LL=$L0   # meta_t2.csv is rewritten by every boot
  while kill -0 $TBP 2>/dev/null; do
    sleep 1; h=$(hdr); IFS=, read -r _ s _ tt lv rid _ <<<"$h"   # kw1: v4 header has a 7th field (4); without the trailing _ rid would read "<run id>,4"
    [ "${rid:-}" = "$RID" ] || { s=""; tt=""; lv=""; }                  # a snapshot from an earlier boot does not count
    # the engine's run length starts again at every level load: note when the header's level changes
    [ -n "$lv" ] && [ "$lv" != "${plv:-}" ] && { plv=$lv; LL=$(date +%s); log "boot $k: level $lv (run length counted from here)"; }
    if [ -z "$first" ]; then
      [ -n "$s" ] && { first=1; log "boot $k: first snapshot seq $s after $(( $(date +%s) - L0 )) s; engine $(engine); candidates: $(engine_all | tr '\n' ';')"; }
      [ -z "$first" ] && [ $(( $(date +%s) - L0 )) -ge 120 ] && { why="no snapshot 120 s after launch"; break; }
      continue; fi
    if [ $k = 0 ] && [ -z "$done_stall" ] && [ -n "$tt" ] && awk -v t="$tt" -v a="$AT" 'BEGIN{exit !(t>=a)}'; then
      EP=$(engine); [ -n "$EP" ] || { why="no engine pid found (stall)"; break; }; log "STALL start: seq $s track ${tt}s engine $EP"
      echo "utc_ms,event,seq,track_s" > "$O/stall.csv"; echo "$(utc),STOP,$s,$tt" >> "$O/stall.csv"
      STP=$EP; kill -STOP "$EP"; sleep "$DUR"; kill -CONT "$EP"; STP=""
      echo "$(utc),CONT,$s,$tt" >> "$O/stall.csv"; log "STALL end: SIGCONT sent"; done_stall=1; same=0; last=$s; continue
    fi
    if [ $k = 0 ] && [ -z "$done_kill" ] && [ -n "$tt" ] && awk -v t="$tt" -v a="$KAT" 'BEGIN{exit !(t>=a)}'; then
      EP=$(engine); [ -n "$EP" ] || { why="no engine pid found (kill)"; break; }; KC=$(cat /proc/$EP/comm 2>/dev/null); KR=$(awk '/VmRSS/{print $2}' /proc/$EP/status 2>/dev/null); KU=$(utc); kill -KILL "$EP"; done_kill=1
      echo "$KU,$k,planned kill (T2i) seq $s track ${tt}s pid $EP,1" >> "$O/restarts.csv"; log "PLANNED KILL: seq $s track ${tt}s pid $EP comm $KC rss_kb $KR"
      sleep 1; WINEPREFIX=$RUN/prefix wineserver -k >/dev/null 2>&1   # pre-flight 2: the Wine wrappers kept tb_run waiting 10 min
      why="planned"; break
    fi
    # an empty read (file missing or caught in mid-write) counts as "no new seq": it must not reset the 30 s rule
    if [ -z "$s" ] || [ "$s" = "$last" ]; then same=$((same+1)); else same=0; fi; [ -n "$s" ] && last=$s
    # ITRZone review: under freeze X-Ray may legitimately stop exporting while ITR2 is dead/away (Hub ~20 s + reload),
    # so the 30 s rule is skipped while pose.txt says dead/away + freeze; it applies again once the pose says play.
    # State and policy come from the last COMPLETE pose and are kept until the next complete one: a writer killed
    # between truncating and writing the file must not blank them (7th field, the session number, is dropped here).
    pl=$(posehdr); [ -n "$pl" ] && IFS=, read -r _ _ _ pst _ paw _ <<<"$pl"
    # LOST: wall time since a new complete pose last appeared. Counted only after one appeared in this boot.
    if [ -n "$pl" ] && [ "$pl" != "$ppl" ]; then ppl=$pl; pchg=$(utc); pseen=1
      [ -n "$lost" ] && { log "lost end: new pose (state ${pst:-?}) after $(( pchg - lostu )) ms"; lost=""; }; fi
    if { [ "$SPLAY" -gt 0 ] || [ -n "$EMPLAY" ]; } && [ -n "$pseen" ] && [ -z "${play0:-}" ] && [ "${pst:-}" = play ]; then
      IFS=, read -r _ _ play0 _ <<<"$pl"; case "$play0" in ''|*[!0-9]*) play0="" ;; *) log "first play pose read: $pl (stop planned ${SPLAY} s after its utc_ms; 0 = none)" ;; esac; fi
    if [ -n "$EMPLAY" ] && [ -n "${play0:-}" ]; then el=$(( $(utc) - play0 ))   # kj3: forced starts, once each per run (every boot)
      if [ "$SURGE_PLAY" != 99999 ] && [ -z "${em_s_done:-}" ] && [ $el -ge $(( SURGE_PLAY * 1000 )) ]; then em_s_done=1
        echo surge >> "$SHM/emtest.txt"; echo "$(utc),$(date -u '+%FT%T.%3NZ'),surge,$el" >> "$O/emforce.csv"; log "EMFORCE surge appended to emtest.txt, $el ms after the first play pose"; fi
      if [ "$PSI_PLAY" != 99999 ] && [ -z "${em_p_done:-}" ] && [ $el -ge $(( PSI_PLAY * 1000 )) ]; then em_p_done=1
        echo psi >> "$SHM/emtest.txt"; echo "$(utc),$(date -u '+%FT%T.%3NZ'),psi,$el" >> "$O/emforce.csv"; log "EMFORCE psi appended to emtest.txt, $el ms after the first play pose"; fi
    fi
    if [ $k = 0 ] && [ "$SPLAY" -gt 0 ] && [ -z "$done_stall" ] && [ -n "${play0:-}" ] && [ $(( $(utc) - play0 )) -ge $(( SPLAY * 1000 )) ]; then
      EP=$(engine); [ -n "$EP" ] || { why="no engine pid found (stall)"; break; }; log "STALL start (play clock): $(( $(utc) - play0 )) ms after the first play pose; seq $s track ${tt}s engine $EP"
      echo "utc_ms,event,seq,track_s" > "$O/stall.csv"; echo "$(utc),STOP,$s,$tt" >> "$O/stall.csv"
      STP=$EP; kill -STOP "$EP"; sleep "$DUR"; kill -CONT "$EP"; STP=""
      echo "$(utc),CONT,$s,$tt" >> "$O/stall.csv"; log "STALL end: SIGCONT sent"; done_stall=1; same=0; last=$s; continue
    fi
    if [ -n "$pseen" ] && [ -z "$lost" ] && [ "${pst:-}" != end ]; then
      lim=$LOST_MS; [ "${pst:-}" = cross ] && lim=$LOSTX_MS; page=$(( $(utc) - pchg ))
      if [ "$lim" -gt 0 ] && [ $page -ge "$lim" ]; then lost=1; lostu=$(utc); log "LOST: no new pose for $page ms (limit $lim; last state ${pst:-?}, policy ${paw:-?})"
        echo "$lostu,$page,${pst:-},${paw:-}" >> "$O/lost.csv"; fi; fi
    fzwant=""; [ "${paw:-}" = freeze ] && { [ "${pst:-}" = dead ] || [ "${pst:-}" = away ] || [ -n "$lost" ]; } && fzwant=1
    # never stop X-Ray in a level change: the script's marker file is non-empty from its request until the arrival
    [ -n "$fzwant" ] && [ -n "$lost" ] && [ -z "${fz:-}" ] && [ -s "$SHM/cross.txt" ] && fzwant=""
    # T2_FREEZE=sigstop (pre-flight 1: device():pause stops Lua for good, so X-Ray cannot unpause itself): the BRIDGE
    # freezes X-Ray by SIGSTOP of the engine while the pose says dead/away + freeze, and SIGCONT when it says play
    # (T1: X-Ray survived two 20 s SIGSTOPs). Logged in freeze.csv (utc_ms,event,pose_state).
    if [ -n "$fzwant" ]; then
      if [ -z "${fz:-}" ]; then fz=1; log "freeze hold: pose state $pst${lost:+ (lost)}, heartbeat skipped"
        if [ "${T2_FREEZE:-}" = sigstop ]; then
          # with T2_ABSORB, wait (<= 3 s) for the script's ABSORB_ARM/ABSORB_SKIP line, so the engine is armed BEFORE the stop
          gms=""; if [ "${T2_ABSORB:-0}" -gt 0 ]; then for _w in 1 2 3 4 5 6; do
              # the NEWEST arm line not yet used; all older ones are then spent (an arm the runner never acted on
              # must not be taken for this freeze's)
              acnt=$(grep -a -c '^ABSORB_ARM\|^ABSORB_SKIP' "$SHM/meta_t2.csv" 2>/dev/null); aline=""
              [ "${acnt:-0}" -gt "${narm:-0}" ] && aline=$(grep -a '^ABSORB_ARM\|^ABSORB_SKIP' "$SHM/meta_t2.csv" 2>/dev/null | tail -1)
              [ -n "$aline" ] && break; sleep 0.5; done
            [ -n "$aline" ] && { narm=$acnt; gms=$(cut -d, -f4 <<<"$aline"); log "freeze: ${aline%%,*} seen before STOP"; } || log "freeze: NO arm line within 3 s (stopping anyway)"; fi
          FZC=$(engine); FZP=""; [ -n "$FZC" ] && kill -STOP "$FZC" && { FZP=$FZC; SU=$(utc); SST=$pst; [ -n "$lost" ] && SST=lost; log "freeze: STOP pid $FZP"; }; fi; fi
      same=0
    elif [ -n "${fz:-}" ] && [ -n "${pst:-}" ] && { [ "${paw:-}" = keep ] || [ "${paw:-}" = freeze ]; }; then fz=""; log "freeze hold end: pose state $pst"   # a torn/empty/truncated pose read (awaysim not exactly keep or freeze) never ends a freeze
      if [ -n "${FZP:-}" ]; then nres=$(grep -a -c '^RESUMED_GAP' "$SHM/meta_t2.csv" 2>/dev/null); kill -CONT "$FZP"; CU=$(utc)
        # game_ms after the resume = the RESUMED_GAP line the script writes on its first frame (wait <= 3 s for it)
        gms=""; for _w in 1 2 3 4 5 6; do rl=$(grep -a '^RESUMED_GAP' "$SHM/meta_t2.csv" 2>/dev/null | sed -n "$(( ${nres:-0} + 1 ))p"); [ -n "$rl" ] && break; sleep 0.5; done
        gpre=""; [ -n "$rl" ] && { gms=$(cut -d, -f5 <<<"$rl"); gpre=$(cut -d, -f7 <<<"$rl"); }
        # STOP row: game time of X-Ray's LAST frame before the gap (pre-flight 3: the arming time was up to ~1 s
        # earlier, which at factor 6 read as 3 s of game time that the absorb never had to remove)
        echo "$SU,STOP,$SST,$FZP,${gpre:-}" >> "$O/freeze.csv"; echo "$CU,CONT,$pst,$FZP,${gms:-}" >> "$O/freeze.csv"; FZP=""; LL=$(( LL + (CU - SU) / 1000 )); fi; same=0; fi
    # T3 R3: while the pose says `cross`, X-Ray is changing level (no export during the load): heartbeat skipped, no stop
    if [ "${pst:-}" = cross ] && [ -z "$lost" ]; then [ -z "${cx:-}" ] && { cx=1; log "cross hold: heartbeat skipped (seq ${s:-none})"; }; same=0
    elif [ -n "${cx:-}" ] && [ -n "${pst:-}" ]; then cx=""; log "cross hold end: pose state $pst${lost:+ (lost)} (seq ${s:-none})"; fi
    [ $same -ge 30 ] && { why="no new seq for 30 s"; break; }
    [ "$OVER_S" -gt 0 ] && [ -z "${FZP:-}" ] && [ $(( $(date +%s) - LL )) -gt $(( TIMES + OVER_S )) ] && { why="still running $(( TIMES + OVER_S )) s after the last level load"; break; }
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
