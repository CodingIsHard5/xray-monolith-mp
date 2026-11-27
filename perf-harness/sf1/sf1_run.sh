#!/usr/bin/env bash
# DRAFT - NOT RUN, NOT REGISTERED. sf1 slow-frame classes driver (PREREG-sf1-slow-frame-classes-DRAFT.md section 7).
#   wt-render-split/perf-harness/sf1/sf1_run.sh [--preflight-only]
# Made from wu3_run.sh (registered 2026-11-27, the newest registered series driver) with sf1's names, order and checks,
# plus the claim handoff of kj4/kj4p_run.sh and the options-file restore of em1/t2_xray_em1.sh:57-70.
# TWO probe loads (one per scene) + 6 scored EXCLUSIVE timing runs (+ at most R_MAX re-runs) in ONE WANT slice (<= 3 h), in the
# order of the hash-checked state-dir copy of sf1_order.txt (one source with the scorer): Coarse14.exe, stock frame
# loop, real time (BENCH_FIXED=0), uncapped, headless on its own display :141.
#   - PROBE GATE (prereg 6.1): the first two loads (arm probe: sf1_probe_cor on cordon1, sf1_probe_ros on bench1, each
#     with its scene's scored-run settings) are never scored and never re-run. Right after each, the
#     scorer's probe= mode checks its output (files, frame count, slow threshold header and rows, stock loop, no limiter,
#     the engine's command line = real time, the zone names the class map relies on, median frame in the plausible
#     range of its scene, no FATAL, window, coverage, no train). Any failed check of EITHER probe: the series stops before
#     any scored run, class=fault, the stop record names the probe and the failed checks. A scored run needs both passed.
#   - RE-RUN (wu3 rule): a SCORED load that ended rc 0, passed check= and is not "VERDICT: clean" is run once more right
#     away as <tag>_r1. At most one per slot; not clean twice = slot VOID, no third attempt. The probe is never re-run.
#     A load that crashed, timed out or failed a check is never re-run (fault stop). BUDGET: at most R_MAX re-runs
#     (prereg 3.1: R_MAX = 6 = one per slot; time fits with margin, so the budget never binds before the per-slot rule).
#   - OPTIONS FILE RESTORE before EVERY boot (prereg 7): run/appdata/axr_options_play.ltx is hashed (refused if not the
#     registered hash: the destination is then untouched), copied over run/gamedata/configs/axr_options.ltx, and the
#     destination hashed. Each exe rewrites that file's version line when it boots (wu3 RESULT), Coarse14 included, so
#     restoring once would let a probe boot change what scored boots read; before every boot every load starts from the
#     same bytes. A failed restore is a fault stop.
#   - CONDITIONS (fleet desk rule): every load logs its settle (load, heavy processes), and after the bench the bench's
#     own conditions.txt start / end lines (loadavg, heavy_others) and VERDICT, into series.log and conditions.log.
#   - STOP RECORD: on every exit after the state folder exists, $ST/stop.txt, one line
#     "class=<done|box|fault>[ interrupted=<tag>] reason=<text>" (wu3 section 3, same classes).
# It claims the box itself through the WANT queue (quiet-required.sh) and decides by EXIT CODE ONLY (its header: "THE
# EXIT CODE IS THE API. DO NOT PARSE THE OUTPUT"). The one text read is kj4p's ownership test (w_holds, below): it never
# decides a claim, it only makes this driver WAIT while a claim under our worker name that is not the queue's handoff
# pre-claim is live (claiming then would renew / nest into that claim, quiet-required.sh:1007, :1116).
# Not scored here. Score ONCE afterwards, output written in the same call:
#   python3 -I wt-render-split/perf-harness/sf1/sf1_classify.py root=out order=out/sf1_state/sf1_order.txt out=out/sf1_state/sf1_score.txt
set -u
cd "$(dirname "$0")/../../.." || exit 2          # project root
T="$HOME/.claude/overseer/tools"; W=engineperf
SF=wt-render-split/perf-harness/sf1; ORDER=$SF/sf1_order.txt; ST=out/sf1_state; OUT=out
EXE=Coarse14.exe; EXE_SHA=65edcf401faeb125c68283084f55500c8d55cca58353e682f9ff8f96d884d88c; CFG=run/appdata/user.ltx
FLAGS="-perf_noinput -perf_nopresent"          # + "-perf_slow <slow_ms>" per load (tools/s4c_series.sh:74 default)
NSCORED=6; NPROBE=2; R_MAX=6                     # R_MAX: prereg 3.1 derivation
ORDER_SHA=ef87cbe894a543ba6e080d3300269ca36cf05e92235f32153ec775a87ca5df7a                          # the order file's sha256 (preflight AND the state-dir copy), set at registration
AXR_SRC=run/appdata/axr_options_play.ltx; AXR_DST=run/gamedata/configs/axr_options.ltx
AXR_SHA=27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29
BENCH_TO=900                                    # BENCH_TIMEOUT passed to the bench (tools/s2_bench.sh:12 default)
END_RESERVE=120   # s after a bench timeout: the bench's own trap (wine + Xvfb), check_run, withdraw/release, notify (timeout 60)
POLL_S=60         # s between claim attempts while queued (wu3)
# ---- BEGIN driver functions (sf1_selftest.sh extracts this block and tests it offline; nothing here runs a load) ----
pin() {   # <file> <sha256>   (tools/t3_kw1.sh)
  local f=$1 want=$2 got
  [ -e "$f" ] || { echo "REFUSED: $f is missing"; return 1; }
  case "$want" in PIN_ME_*) echo "REFUSED: $f has no registered hash ($want)"; return 1 ;; esac
  got=$(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)
  [ "$got" = "$want" ] || { echo "REFUSED: $f is not ${want:0:8} (it is ${got:0:8})"; return 1; }
}
order_ok() {   # <order file>: the registered shape (2 probes first + 3 cordon1 at 6.0 + 3 bench1 at 8.0, fields, tags)
  local o=$1 lines t f=0
  lines=$(grep -v '^#' "$o" 2>/dev/null | grep .)
  [ "$(echo "$lines" | grep -c .)" = $((NSCORED + NPROBE)) ] || { echo "REFUSED: $o does not list $((NSCORED + NPROBE)) loads"; f=1; }
  [ "$(echo "$lines" | sed -n 1p)" = "sf1_probe_cor|$EXE|cordon1|14000|10000|6.0|probe" ] || { echo "REFUSED: the first load of $o is not the registered Cordon probe line"; f=1; }
  [ "$(echo "$lines" | sed -n 2p)" = "sf1_probe_ros|$EXE|bench1|14000|10000|8.0|probe" ] || { echo "REFUSED: the second load of $o is not the registered Rostok probe line"; f=1; }
  [ "$(echo "$lines" | tail -n +3 | grep -c "^[a-z0-9_]*|$EXE|cordon1|14000|10000|6\.0|scored$")" = 3 ] && \
    [ "$(echo "$lines" | tail -n +3 | grep -c "^[a-z0-9_]*|$EXE|bench1|14000|10000|8\.0|scored$")" = 3 ] || \
    { echo "REFUSED: $o is not 3 cordon1 (slow 6.0) + 3 bench1 (slow 8.0) scored lines of $EXE, warm 14000, frames 10000"; f=1; }
  for t in $(echo "$lines" | cut -d'|' -f1); do
    [ "$t" = "${t,,}" ] || { echo "REFUSED: tag $t is not lowercase"; f=1; }
    case "$t" in *_r[0-9]*) echo "REFUSED: tag $t looks like a re-run folder name (<tag>_r<n>)"; f=1 ;; esac
  done
  [ "$(echo "$lines" | cut -d'|' -f1 | sort | uniq -d | grep -c .)" = 0 ] || { echo "REFUSED: a tag is listed twice"; f=1; }
  return $f
}
order_copy() {   # <order file> <sha256> <state dir>: copy into the state dir, then the COPY must have the registered hash
  local src=$1 want=$2 dst=$3/sf1_order.txt
  [ -e "$dst" ] && { echo "REFUSED: $dst exists"; return 1; }
  cp -- "$src" "$dst" 2>/dev/null || { echo "REFUSED: cannot copy $src to $dst"; return 1; }
  chmod a-w "$dst" 2>/dev/null
  pin "$dst" "$want" || { echo "REFUSED: the state-dir copy of the order file failed its hash check"; return 1; }
  order_ok "$dst" || return 1
}
axr_restore() {   # <source> <destination> <sha256>: em1/t2_xray_em1.sh:57-70. Source hashed FIRST (a refusal leaves the
  # destination untouched), then copied, then the destination hashed. 0 = restored; 1 = refused, the reason printed
  local src=$1 dst=$2 want=$3 got
  [ -f "$src" ] || { echo "REFUSED: options restore: source $src missing"; return 1; }
  got=$(sha256sum < "$src" 2>/dev/null | cut -c1-64)
  [ "$got" = "$want" ] || { echo "REFUSED: options restore: sha256 of source $src is ${got:-unreadable}, want $want (destination untouched)"; return 1; }
  cp -- "$src" "$dst" 2>/dev/null || { echo "REFUSED: options restore: copy to $dst failed"; return 1; }
  got=$(sha256sum < "$dst" 2>/dev/null | cut -c1-64)
  [ "$got" = "$want" ] || { echo "REFUSED: options restore: sha256 of $dst after the copy is ${got:-unreadable}, want $want"; return 1; }
  echo "options restored: $src -> $dst (sha256 ${want:0:16})"; return 0
}
pending() {   # <order file>: the slots not yet in $ST/done.txt, in order
  grep -v '^#' "$1" | grep . | while IFS='|' read -r tag rest; do grep -qx "$tag" $ST/done.txt || echo "$tag|$rest"; done; }
slice_ok() {   # <slice end, epoch s> <seconds needed>: 0 if at least that many seconds are left now
  [ $(( $1 - $(date +%s) )) -ge "$2" ]; }
halt() {   # <class box|fault> <reason>: the series stops here (rc 1)
  sc=$1; stop=$2; rc=1; }
stop_write() {   # the stop record, ONE line ended by LF (wu3 section 3)
  printf 'class=%s%s reason=%s\n' "${sc:-fault}" "${INTR:+ interrupted=$INTR}" "$(printf '%s' "${stop:-list finished}" | tr '\n\r' '  ')" > "$ST/stop.txt"; }
cancel() {   # TERM / INT: a box stop; when a bench is live (LIVE = its tag) the record names it
  halt box "cancelled${LIVE:+ during $LIVE}"; rc=143; INTR=$LIVE; }
record_stop() {   # the stop record or, if it cannot be written, rc != 0 and SNOTE for the log line and the notify
  stop_write 2>/dev/null && return 0
  [ "$rc" = 0 ] && rc=1
  SNOTE="stop record NOT written ($ST/stop.txt)"; return 1; }
verdict_of() {   # <tag>: the bench's VERDICT line of out/<tag> (CR stripped: engine-side files may be CRLF)
  grep -a '^VERDICT' "$OUT/$1/conditions.txt" 2>/dev/null | head -1 | tr -d '\r'; }
cond_of() {   # <tag>: the bench's start / end conditions (loadavg, heavy_others) and VERDICT, one line, CR stripped
  local c; c=$(grep -a -E '^(start|end|VERDICT)' "$OUT/$1/conditions.txt" 2>/dev/null | tr -d '\r' | tr '\n' '|')
  echo "conditions $1: ${c:-conditions.txt missing}"; }
flag_lines() { "$T/quiet-required.sh" check 2>/dev/null | sed -n 's/^\(HELD\|SLOT[0-9]*\): //p'; }
# kj4p (amendment 1): a claim under our name, EXCEPT the queue's own 5-minute handoff pre-claim written for us to claim over
w_holds() { flag_lines | awk -v w="$W" '$3 == w && !index($0, "handoff pre-claim") {f = 1} END {exit !f}'; }
acquire() {   # the WANT slice: claim by EXIT CODE only; wait while a claim under our name is live (kj4p); 0 = claimed
  local got
  while :; do
    got=0
    trap 'CANCEL=1' TERM INT
    if ! title_running && ! roblox_running && ! caden_lock && ! w_holds && \
       "$T/quiet-required.sh" claim $W 30 "$WHAT" >/dev/null 2>&1; then got=1; CLAIMED=1; fi   # CLAIM-EXIT
    trap 'cancel; finish' TERM INT
    [ "$CANCEL" = 1 ] && { cancel; finish; }
    [ "$got" = 1 ] && break
    "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
    sleep "$POLL_S"
  done
  "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
  return 0; }
gates() {   # <tag> <arm>: every pre-load gate, in wu3's order + the options restore; 1 = stop (halt set the class, reason)
  local t=$1 arm=$2 sr r
  sr=$(settle "$t"); log "$sr before $t"
  case "$sr" in *cap) halt box "not quiet before $t (settle cap: $sr)"; return 1 ;; esac
  slice_ok $SL_END 900 || { halt box "slice time used before $t"; return 1; }
  title_running "$t" && { halt box "protected title running (or the title check failed) before $t"; return 1; }
  roblox_running "$t" && { halt box "Roblox running before $t"; return 1; }
  caden_lock "$t" && { halt box "Caden's launcher running before $t"; return 1; }
  disp_wait "$t" || { halt fault "display $DISP not free before $t"; return 1; }
  cfg_ok "$t" >> $ST/series.log 2>&1 || { halt fault "settings not stock/uncapped before $t"; return 1; }
  [ -e "$OUT/$t" ] && { halt fault "$OUT/$t exists before its load"; return 1; }
  exe_ok "$t" >> $ST/series.log 2>&1 || { halt fault "exe changed before $t"; return 1; }
  claim_renew "$t" || { halt box "claim renewal refused before $t"; return 1; }
  # the claim is held: the options file is put back before THIS boot (prereg 7)
  if r=$(axr_restore "$AXR_SRC" "$AXR_DST" "$AXR_SHA"); then log "$r (before $t)"; else log "$r (before $t)"; halt fault "$r (before $t)"; return 1; fi   # AXR-GATE
  slice_ok $SL_END $((BENCH_TO + END_RESERVE)) || { halt box "slice too short for $t (< $BENCH_TO + $END_RESERVE s left)"; return 1; }
  [ "$arm" = probe ] || [ "$PROBED" = "$NPROBE" ] || { halt fault "scored load $t reached without both probes passed ($PROBED of $NPROBE)"; return 1; }   # PROBED-GATE
  return 0
}
series_loop() {   # every slot of the order copy in order; one re-run <slot>_r1 for a not-clean scored load (wu3 rule)
  local line tag exe save warm frames slow arm t a v brc c crc
  while :; do
    line=$(pending "$ORDER" | head -1); [ -n "$line" ] || break
    IFS='|' read -r tag exe save warm frames slow arm <<<"$line"
    t=${tag,,}; a=0
    while :; do      # a = 0: the slot's load; a = 1: its one re-run
      gates "$t" "$arm" || return 0
      log "load $t arm=$arm exe=$exe save=$save warm=$warm frames=$frames slow=$slow attempt=$((a+1)) display=$DISP load $(cut -d' ' -f1-3 /proc/loadavg)"
      LIVE=$t                                                                           # LIVE-BENCH set
      bench "$t" "$exe" "$save" "$warm" "$frames" "$slow"; brc=$?
      LIVE=""
      v=$(verdict_of "$t")
      log "load $t rc=$brc $v"
      c=$(cond_of "$t"); log "$c"; echo "$c" >> $ST/conditions.log                      # COND-LOG
      echo "$t" >> $ST/done.txt || { halt fault "state write failed: cannot append $t to $ST/done.txt"; return 0; }
      [ $brc = 0 ] || { halt fault "load $t ended rc=$brc (crash, timeout or no capture)"; return 0; }
      c=$(check_run "$t" "$arm"); crc=$?
      echo "$c" > $ST/check_$t.txt || { halt fault "state write failed: cannot write $ST/check_$t.txt"; return 0; }
      log "load $t $(echo "$c" | tail -1)"
      if [ "$arm" = probe ]; then
        # PROBE GATE: failed = the series ends before any scored run, nothing scored, the failed checks named
        [ $crc = 0 ] || { halt fault "PROBE FAILED on $t, series stopped before any scored run: $(echo "$c" | tail -1 | sed 's/^PROBE [^ ]* //') $(echo "$c" | grep '^PROBE STOP' | head -2 | tr '\n' ' ')"; return 0; }   # PROBE-GATE
        PROBED=$((PROBED+1))
        [ "$v" = "VERDICT: clean" ] || { nnc=$((nnc+1)); log "probe $t not clean: allowed, the probe is never re-run"; }
        break        # RERUN-RULE probe: never re-run
      fi
      [ $crc = 0 ] || { halt fault "load $t failed a check: $(echo "$c" | grep '^CHECK STOP' | head -3 | tr '\n' ' ')"; return 0; }
      [ "$v" = "VERDICT: clean" ] && break                                              # RERUN-RULE clean
      nnc=$((nnc+1))
      [ $a = 1 ] && { log "re-run $t not clean too: slot $tag is VOID, no third attempt"; break; }   # RERUN-RULE one
      [ $nrr -ge $R_MAX ] && { halt box "re-run budget used: $t not clean after $nrr of $R_MAX re-runs"; return 0; }   # RERUN-RULE budget
      nrr=$((nrr+1)); a=1; t=${tag,,}_r1                                                # RERUN-RULE rerun
      log "load $tag not clean ($v): re-run $nrr of $R_MAX as $t"
    done
  done
  sc=done; stop=""; return 0
}
# ---- END driver functions ----
bad=0
pin tools/s2_bench.sh                    072af8edb67eab095ea4408f3950a428709757b3ce1abdfe25efc25a3fb150b5 || bad=1
pin tools/lib_engine_pids.sh             cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e || bad=1
pin tools/run_noguard.sh                 92729abc420784053cee4b1b1832f5d112f74979a4760e2173d45578358455d5 || bad=1
pin run/bin/$EXE                         $EXE_SHA || bad=1
pin run/appdata/savedgames/cordon1.scop  6dea752bc652a45c748e1438e3bc8ffd2d8d89f24730177c792328aeb5d13f3e || bad=1
pin run/appdata/savedgames/cordon1.scoc  a9f9a8a91b034de28a49be7f8029d823d057e46b4445f3f94d7255dbc77b0b6b || bad=1
pin run/appdata/savedgames/bench1.scop   891693c5e318ad1e97e8437476654200eb39d3bab89de79a778fa9b90ff89ffa || bad=1
pin run/appdata/savedgames/bench1.scoc   7c2ddf33ccdaa75c341ba8a7c3cd84cb454972e81a47ce844d6919cb2a2f167e || bad=1
pin $AXR_SRC                             $AXR_SHA || bad=1
pin $ORDER                               $ORDER_SHA || bad=1
pin $SF/sf1_classify.py                  5d67ba2abedb84d3bc8f2229a9dbb4d83cd81c199fd8feedd43b6ae079e72eb1 || bad=1
pin $SF/sf1_selftest.sh                  dfe0aef92856ebb9670c45b02462a27cbf65b0ff313ad43d4092bcc5ac8e05c5 || bad=1
DISP=${BENCH_DISPLAY:-:141}
[ "$DISP" = ":141" ] || { echo "REFUSED: BENCH_DISPLAY '$DISP' is not :141 (this experiment's own display)"; bad=1; }
disp_free() {   # refuses a display that exists or left a lock (tools/t3_kw1.sh)
  local n=${DISP#:}
  [ -e "/tmp/.X11-unix/X$n" ] && { echo "REFUSED: display $DISP exists (/tmp/.X11-unix/X$n)"; return 1; }
  [ -e "/tmp/.X$n-lock" ] && { echo "REFUSED: display $DISP is locked (/tmp/.X$n-lock)"; return 1; }
  return 0
}
disp_free || bad=1
# stock, uncapped frame loop in the settings the engine reads (read only; this driver never edits user.ltx)
cfgval() { awk -v k="$1" '$1==k{gsub("\r",""); v=$2} END{print v}' "$CFG" 2>/dev/null; }
cfg_ok() {
  [ -s "$CFG" ] || { echo "REFUSED: $CFG is missing"; return 1; }
  [ "$(cfgval r__framelimit)" = 0 ] || { echo "REFUSED: $CFG r__framelimit is '$(cfgval r__framelimit)', not 0"; return 1; }
  case "$(cfgval sim_tick)" in ''|0) ;; *) echo "REFUSED: $CFG sim_tick is '$(cfgval sim_tick)'"; return 1 ;; esac
  return 0
}
cfg_ok || bad=1
[ -e "$ST" ] && { echo "REFUSED: $ST exists (state folder must be new)"; bad=1; }
order_ok "$ORDER" || bad=1
for t in $(grep -v '^#' "$ORDER" 2>/dev/null | grep . | cut -d'|' -f1); do
  for f in $OUT/$t $OUT/${t}_r1; do [ -e "$f" ] && { echo "REFUSED: $f exists"; bad=1; }; done
done
[ "$bad" = 0 ] || exit 2
[ "${1:-}" = "--preflight-only" ] && { echo "preflight ok"; exit 0; }

# the state folder must be created (new) and writable BEFORE the queue is joined (wu2 Revision 3, D5)
mkdir "$ST" 2>/dev/null && : > "$ST/done.txt" 2>/dev/null && : >> "$ST/series.log" 2>/dev/null && : > "$ST/conditions.log" 2>/dev/null || {
  echo "REFUSED: cannot create the state folder $ST (or its done.txt / series.log / conditions.log)"; exit 2; }
# from here on only the hash-checked state-dir copy of the order is read (wu2 review pass 1, finding 3)
order_copy "$ORDER" "$ORDER_SHA" "$ST" >> $ST/series.log 2>&1 || {
  sc=fault; stop="order copy in $ST refused (see $ST/series.log)"; INTR=""; stop_write; echo "REFUSED: $stop"; exit 2; }
ORDER=$ST/sf1_order.txt
WHAT="EXCLUSIVE: headless sf1 slow-frame timing runs (one engine at a time)"
log() { echo "$(date '+%F %T') $*" | tee -a $ST/series.log; }
heavy_ps() { ps -eo pcpu,pid,comm,args --sort=-pcpu | awk 'NR>1 && $1>=100 && $3!~/wineserver|Xvfb/ && $3!="ps" {print $1"%:"$3}' | head -5 | tr '\n' ' '; }
caden_lock() { [ -e run/.caden-play.lock ] && kill -0 "$(cat run/.caden-play.lock 2>/dev/null)" 2>/dev/null; }
title_running() {   # 0 = a protected title is playing OR the check cannot tell (kj4p title_state: a failed check is NOT clear)
  local r; "$T/protected-title-check.sh" >/dev/null 2>&1; r=$?
  [ $r != 1 ]; }
roblox_running() { pgrep -x sober >/dev/null; }
settle() { local t0 l1 hv; t0=$(date +%s)      # tools/s4c_series.sh: 1-min load < 2 and no other >= 100 % process, 15 s checks, 10 min cap (wu3)
  while :; do l1=$(cut -d' ' -f1 /proc/loadavg); hv=$(heavy_ps)
    awk -v l="$l1" 'BEGIN{exit !(l<2)}' && [ -z "${hv// /}" ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[] ok"; return 0; }
    [ $(( $(date +%s) - t0 )) -ge 600 ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[$hv] cap"; return 1; }
    sleep 15; done; }
disp_wait() {   # X server from the previous load: give its socket and lock up to 20 s to go, then refuse
  for _ in 1 2 3 4 5 6 7 8 9 10; do disp_free >/dev/null 2>&1 && break; sleep 2; done
  disp_free >> $ST/series.log 2>&1; }
exe_ok() { pin run/bin/$EXE $EXE_SHA; }
claim_renew() { "$T/quiet-required.sh" claim $W 30 "$WHAT" >/dev/null 2>&1; }   # same-text renewal, exit code only
bench() {   # <tag> <exe> <save> <warm> <frames> <slow>: one load; returns the bench's rc
  local r
  BENCH_DISPLAY=$DISP BENCH_NOCLAIM=1 BENCH_EXCLUSIVE=1 BENCH_SAVE=$3 BENCH_LOWEND=0 BENCH_FIXED=0 BENCH_TIMEOUT=$BENCH_TO \
    tools/s2_bench.sh "$2" "$1" "$4" "$5" $FLAGS -perf_slow "$6" > $ST/run_$1.log 2>&1 & BP=$!
  wait $BP; r=$?; BP=""; return $r; }
check_run() {   # <tag> <arm>: the scorer's own rules (one source; exit 0 go on, else stop): probe= for the probe, else check=
  local tag=${1,,} mode=check
  [ "$2" = probe ] && mode=probe
  python3 -I $SF/sf1_classify.py $mode=$OUT/$tag order=$ORDER 2>&1; }

stop=""; sc=""; BP=""; LIVE=""; INTR=""; SNOTE=""; rc=0; nnc=0; nrr=0; CLAIMED=0; PROBED=0; SL_END=0; CANCEL=0
# ONE way out, also when cancelled (installed BEFORE the first queue operation): the stop record first, then the bench
# stopped and reaped, the WANT entry withdrawn, the slice released ONLY if this invocation acquired the claim
finish() { trap - EXIT TERM INT
  [ -n "$sc" ] || halt fault "driver exited without a recorded stop (rc $rc)"
  record_stop || echo "$SNOTE" >&2
  [ -n "$BP" ] && kill -0 $BP 2>/dev/null && { kill -TERM $BP 2>/dev/null; wait $BP 2>/dev/null; }
  "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
  if [ "$CLAIMED" = 1 ]; then
    "$T/quiet-required.sh" release $W >/dev/null 2>&1 || log "release exit code $? (claim may already have expired)"
  fi
  log "slice end: $(grep -c . $ST/done.txt) loads done, $(pending "$ORDER" | wc -l) slots left, $nrr of $R_MAX re-runs, $nnc not clean. $(cat $ST/stop.txt 2>/dev/null)${SNOTE:+ $SNOTE}"
  timeout 60 "$T/notify-overseer.sh" engineperf - <<<"EnginePerf sf1 slow-frame series: slice ended $(date +%T), $(grep -c . $ST/done.txt) loads done, $(pending "$ORDER" | wc -l) slots left, $nrr of $R_MAX re-runs, $nnc not clean. $(cat $ST/stop.txt 2>/dev/null)${SNOTE:+ $SNOTE}. $([ "$PROBED" = "$NPROBE" ] && echo "Not scored yet (score once by hand)." || echo "NOT scored: $PROBED of $NPROBE probes passed (a probe failed or was not reached), no scored run happened; another attempt needs the supervisor's word.")" >/dev/null 2>&1
  exit $rc; }
trap finish EXIT; trap 'cancel; finish' TERM INT
acquire
SL_END=$(( $(date +%s) + 3*3600 ))
log "slice granted, sf1, display $DISP, load $(cut -d' ' -f1-3 /proc/loadavg)"
# the wait for the slice can be long: the display must still be free now
disp_free >> $ST/series.log 2>&1 || { halt fault "display $DISP not free at the slice grant"; rc=2; finish; }
series_loop
finish
