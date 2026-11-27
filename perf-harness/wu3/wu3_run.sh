#!/usr/bin/env bash
# REGISTERED 2026-11-27 (PREREG-wu3-alife-warmup-2026-11-27.md). Not yet run for real: `bash -n` and the self-test's offline function tests only.
# wu3 driver (PREREG-wu3-alife-warmup-2026-11-27.md, DRAFT).   wt-render-split/perf-harness/wu3/wu3_run.sh [--preflight-only]
# wu3 = wu2_run.sh (registered 2026-11-26, sha 257f4ec7...) with the wu3 names and paths (state out/wu3_state, tags
# wu3_*) and ONE change in how a not-clean load is handled. Same exe, bench, flags, order, window, slice, per-load checks.
#   - RE-RUN (prereg wu3 3.1): a SCORED load that ended rc 0, passed its per-load check and is not "VERDICT: clean" is
#     run once more right away as <tag>_r1 (folder out/<tag>_r1), after the normal settle and every normal pre-load
#     gate. At most ONE re-run per order slot; a re-run that is not clean either leaves the slot VOID (no third attempt).
#     The prime is never re-run and does not count. A load that crashed, timed out or failed a check is NEVER re-run.
#   - BUDGET: at most R_MAX re-runs in the series (derivation in the prereg, 3.2). A not-clean load that needs a re-run
#     when the budget is used stops the series (class box).
#   - STOP RECORD: on every exit after the state folder exists the driver writes $ST/stop.txt, one line
#     "class=<done|box|fault> reason=<text>". box = the box was not quiet or not ours (settle cap, re-run budget used,
#     slice time used / too short, protected title, Roblox, Caden's launcher, claim renewal refused, cancelled);
#     fault = something did not run as registered (crash / timeout / no capture, a failed per-load check, failed probe,
#     state write failure, exe / settings / display problems, order copy); done = the list finished.
# It claims the box itself through the WANT queue (quiet-required.sh) and decides by EXIT CODE ONLY (its header: "THE
# EXIT CODE IS THE API. DO NOT PARSE THE OUTPUT"). 33 EXCLUSIVE loads + at most R_MAX re-runs in ONE WANT slice (3 h).
# Not scored here. Score ONCE afterwards, output written in the same call:
#   python3 -I wt-render-split/perf-harness/wu3/wu3_score.py root=out order=wt-render-split/perf-harness/wu3/wu3_order.txt stop=out/wu3_state/stop.txt out=out/wu3_state/wu3_score.txt
set -u
cd "$(dirname "$0")/../../.." || exit 2          # project root
T="$HOME/.claude/overseer/tools"; W=engineperf
WU=wt-render-split/perf-harness/wu3; ORDER=$WU/wu3_order.txt; ST=out/wu3_state; OUT=out
EXE=Warm18.exe; EXE_SHA=15828a7a6cadd38e42cc376869f581166884c04f1eb236baf79054a74332ae9d; CFG=run/appdata/user.ltx
FLAGS="-perf_noinput -perf_nopresent"          # + "-perf_slow <slow_ms>" per load, + "-alife_warmup" on ON loads
NLOADS=33; NON=16; NOFF=16; R_MAX=8              # R_MAX: wu3_score.py has the same value (self-test checks)
ORDER_SHA=7360330ff8837e280736d4a8be3472dd938b1a9b8d75bee00a7cb36ad198c60c                           # the order file's sha256 (preflight AND the state-dir copy), set at registration
BENCH_TO=900                                    # BENCH_TIMEOUT passed to the bench
END_RESERVE=120   # s after a bench timeout: the bench's own trap (wine + Xvfb), check_run, withdraw/release, notify (timeout 60)
# ---- BEGIN driver functions (wu3_selftest.sh extracts this block and tests it offline; nothing here runs a load) ----
pin() {   # <file> <sha256>   (tools/t3_kw1.sh)
  local f=$1 want=$2 got
  [ -e "$f" ] || { echo "REFUSED: $f is missing"; return 1; }
  case "$want" in PIN_ME_*) echo "REFUSED: $f has no registered hash ($want)"; return 1 ;; esac
  got=$(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)
  [ "$got" = "$want" ] || { echo "REFUSED: $f is not ${want:0:8} (it is ${got:0:8})"; return 1; }
}
order_ok() {   # <order file>: the registered shape (33 loads, 1 prime first + 16 on + 16 off, fields, lowercase, no dup)
  local o=$1 lines t f=0
  lines=$(grep -v '^#' "$o" 2>/dev/null | grep .)
  [ "$(echo "$lines" | grep -c .)" = $NLOADS ] || { echo "REFUSED: $o does not list $NLOADS loads"; f=1; }
  [ "$(echo "$lines" | grep -c '|on$')" = $NON ] && [ "$(echo "$lines" | grep -c '|off$')" = $NOFF ] && \
    [ "$(echo "$lines" | grep -c '|prime$')" = 1 ] || { echo "REFUSED: $o is not 1 prime + $NON on + $NOFF off"; f=1; }
  [ "$(echo "$lines" | head -1 | cut -d'|' -f7)" = prime ] || { echo "REFUSED: the first load of $o is not the prime load"; f=1; }
  echo "$lines" | awk -F'|' -v e=$EXE 'NF!=7 || $2!=e || $3!="cordon1" || $4!="0" || $5!="20000" || $6!="20" || $7!~/^(on|off|prime)$/ {print "REFUSED: order line " $0 " is not <tag>|" e "|cordon1|0|20000|20|on/off/prime"; f=1} END{exit f}' || f=1
  for t in $(echo "$lines" | cut -d'|' -f1); do
    [ "$t" = "${t,,}" ] || { echo "REFUSED: tag $t is not lowercase"; f=1; }
    case "$t" in *_r[0-9]*) echo "REFUSED: tag $t looks like a re-run folder name (<tag>_r<n>)"; f=1 ;; esac
  done
  [ "$(echo "$lines" | cut -d'|' -f1 | sort | uniq -d | grep -c .)" = 0 ] || { echo "REFUSED: a tag is listed twice"; f=1; }
  return $f
}
order_copy() {   # <order file> <sha256> <state dir>: copy into the state dir, then the COPY must have the registered hash
  local src=$1 want=$2 dst=$3/wu3_order.txt
  [ -e "$dst" ] && { echo "REFUSED: $dst exists"; return 1; }
  cp -- "$src" "$dst" 2>/dev/null || { echo "REFUSED: cannot copy $src to $dst"; return 1; }
  chmod a-w "$dst" 2>/dev/null
  pin "$dst" "$want" || { echo "REFUSED: the state-dir copy of the order file failed its hash check"; return 1; }
  order_ok "$dst" || return 1
}
pending() {   # <order file>: the slots not yet in $ST/done.txt, in order
  grep -v '^#' "$1" | grep . | while IFS='|' read -r tag rest; do grep -qx "$tag" $ST/done.txt || echo "$tag|$rest"; done; }
slice_ok() {   # <slice end, epoch s> <seconds needed>: 0 if at least that many seconds are left now
  [ $(( $1 - $(date +%s) )) -ge "$2" ]; }
halt() {   # <class box|fault> <reason>: the series stops here (rc 1)
  sc=$1; stop=$2; rc=1; }
stop_write() {   # the stop record, ONE line ended by LF; written on every exit once the state folder exists. Fix B: a box
  # stop while a bench was live adds interrupted=<tag> (fixed form: class=box interrupted=<tag> reason=<text>)
  printf 'class=%s%s reason=%s\n' "${sc:-fault}" "${INTR:+ interrupted=$INTR}" "$(printf '%s' "${stop:-list finished}" | tr '\n\r' '  ')" > "$ST/stop.txt"; }
cancel() {   # TERM / INT: a box stop; when a bench is live (LIVE = its tag) the record names it
  halt box "cancelled${LIVE:+ during $LIVE}"; rc=143; INTR=$LIVE; }
record_stop() {   # fix C: the stop record or, if it cannot be written, rc != 0 and SNOTE for the log line and the notify
  stop_write 2>/dev/null && return 0
  [ "$rc" = 0 ] && rc=1
  SNOTE="stop record NOT written ($ST/stop.txt)"; return 1; }
verdict_of() {   # <tag>: the bench's VERDICT line of out/<tag> (CR stripped: engine-side files may be CRLF)
  grep -a '^VERDICT' "$OUT/$1/conditions.txt" 2>/dev/null | head -1 | tr -d '\r'; }
gates() {   # <tag> <arm>: every pre-load gate, in wu2's order; 1 = stop (halt set the class and reason)
  local t=$1 arm=$2 sr
  sr=$(settle "$t"); log "$sr before $t"
  case "$sr" in *cap) halt box "not quiet before $t (settle cap: $sr)"; return 1 ;; esac
  slice_ok $SL_END 900 || { halt box "slice time used before $t"; return 1; }
  title_running "$t" && { halt box "protected title running before $t"; return 1; }
  roblox_running "$t" && { halt box "Roblox running before $t"; return 1; }
  caden_lock "$t" && { halt box "Caden's launcher running before $t"; return 1; }
  disp_wait "$t" || { halt fault "display $DISP not free before $t"; return 1; }
  cfg_ok "$t" >> $ST/series.log 2>&1 || { halt fault "settings not stock/uncapped/lever-off before $t"; return 1; }
  [ -e "$OUT/$t" ] && { halt fault "$OUT/$t exists before its load"; return 1; }
  exe_ok "$t" >> $ST/series.log 2>&1 || { halt fault "exe changed before $t"; return 1; }
  claim_renew "$t" || { halt box "claim renewal refused before $t"; return 1; }
  # wu2 review pass 1 (finding 9): right before the bench, after the settle, display wait and renewal
  slice_ok $SL_END $((BENCH_TO + END_RESERVE)) || { halt box "slice too short for $t (< $BENCH_TO + $END_RESERVE s left)"; return 1; }
  [ "$arm" = prime ] || [ "$PROBED" = 1 ] || { halt fault "scored load $t reached without a passed prime probe"; return 1; }
  return 0
}
series_loop() {   # every slot of the order copy in order; one re-run <slot>_r1 for a not-clean scored load (prereg wu3 3.1)
  local line tag exe save warm frames slow arm t a v brc c crc
  while :; do
    line=$(pending "$ORDER" | head -1); [ -n "$line" ] || break
    IFS='|' read -r tag exe save warm frames slow arm <<<"$line"
    t=${tag,,}; a=0
    while :; do      # a = 0: the slot's load; a = 1: its one re-run
      gates "$t" "$arm" || return 0
      log "load $t arm=$arm exe=$exe save=$save warm=$warm frames=$frames slow=$slow attempt=$((a+1)) display=$DISP load $(cut -d' ' -f1-3 /proc/loadavg)"
      LIVE=$t                                                                           # LIVE-BENCH set
      bench "$t" "$exe" "$save" "$warm" "$frames" "$slow" "$arm"; brc=$?
      LIVE=""
      v=$(verdict_of "$t")
      log "load $t rc=$brc $v"
      # ran (whatever happened): this tag is never run again. A failed append stops the series by name
      echo "$t" >> $ST/done.txt || { halt fault "state write failed: cannot append $t to $ST/done.txt"; return 0; }
      [ $brc = 0 ] || { halt fault "load $t ended rc=$brc (crash, timeout or no capture)"; return 0; }
      c=$(check_run "$t" "$arm"); crc=$?
      echo "$c" > $ST/check_$t.txt || { halt fault "state write failed: cannot write $ST/check_$t.txt"; return 0; }
      log "load $t $(echo "$c" | tail -1)"
      if [ "$arm" = prime ]; then
        # wu2 Revision 3 (D1): failed probe = the series ends with NO verdict and nothing consumed
        [ $crc = 0 ] || { halt fault "PROBE FAILED on the prime load $t ($(echo "$c" | grep '^PROBE STOP' | head -3 | tr '\n' ' ')): series NOT scored, nothing consumed, supervisor's word needed for another attempt"; return 0; }
        PROBED=1
        [ "$v" = "VERDICT: clean" ] || { nnc=$((nnc+1)); log "prime $t not clean: allowed, the prime is never re-run"; }
        break        # RERUN-RULE prime: never re-run
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
pin $ORDER                               $ORDER_SHA || bad=1
pin $WU/wu3_score.py                     b435a9ead5cdcf1d13af59f61ed4e5c20270dbd30de183464d3806ded94b8766 || bad=1
pin $WU/wu3_selftest.sh                  7bacc55d6490bf9ed46028a59a05685e361b5ab5d56327d2f05d2b8a5933915e || bad=1
DISP=${BENCH_DISPLAY:-:141}
[ "$DISP" = ":141" ] || { echo "REFUSED: BENCH_DISPLAY '$DISP' is not :141 (this experiment's own display)"; bad=1; }
disp_free() {   # refuses a display that exists or left a lock (tools/t3_kw1.sh)
  local n=${DISP#:}
  [ -e "/tmp/.X11-unix/X$n" ] && { echo "REFUSED: display $DISP exists (/tmp/.X11-unix/X$n)"; return 1; }
  [ -e "/tmp/.X$n-lock" ] && { echo "REFUSED: display $DISP is locked (/tmp/.X$n-lock)"; return 1; }
  return 0
}
disp_free || bad=1
# stock, uncapped frame loop, lever off in the settings the engine reads (read only; this driver never edits user.ltx)
cfgval() { awk -v k="$1" '$1==k{gsub("\r",""); v=$2} END{print v}' "$CFG" 2>/dev/null; }
cfg_ok() {
  [ -s "$CFG" ] || { echo "REFUSED: $CFG is missing"; return 1; }
  [ "$(cfgval r__framelimit)" = 0 ] || { echo "REFUSED: $CFG r__framelimit is '$(cfgval r__framelimit)', not 0"; return 1; }
  case "$(cfgval sim_tick)" in ''|0) ;; *) echo "REFUSED: $CFG sim_tick is '$(cfgval sim_tick)'"; return 1 ;; esac
  case "$(cfgval alife_warmup)" in ''|0) ;; *) echo "REFUSED: $CFG alife_warmup is '$(cfgval alife_warmup)' (OFF must be stock)"; return 1 ;; esac
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

# wu2 Revision 3 (D5): the state folder must be created (new) and writable BEFORE the queue is joined
mkdir "$ST" 2>/dev/null && : > "$ST/done.txt" 2>/dev/null && : >> "$ST/series.log" 2>/dev/null || {
  echo "REFUSED: cannot create the state folder $ST (or its done.txt / series.log)"; exit 2; }
# wu2 review pass 1 (finding 3): from here on only the hash-checked state-dir copy of the order is read
order_copy "$ORDER" "$ORDER_SHA" "$ST" >> $ST/series.log 2>&1 || {
  sc=fault; stop="order copy in $ST refused (see $ST/series.log)"; INTR=""; stop_write; echo "REFUSED: $stop"; exit 2; }
ORDER=$ST/wu3_order.txt
WHAT="EXCLUSIVE: headless wu3 alife_warmup loads (one engine at a time)"
log() { echo "$(date '+%F %T') $*" | tee -a $ST/series.log; }
heavy_ps() { ps -eo pcpu,pid,comm,args --sort=-pcpu | awk 'NR>1 && $1>=100 && $3!~/wineserver|Xvfb/ && $3!="ps" {print $1"%:"$3}' | head -5 | tr '\n' ' '; }
caden_lock() { [ -e run/.caden-play.lock ] && kill -0 "$(cat run/.caden-play.lock 2>/dev/null)" 2>/dev/null; }
title_running() { "$T/protected-title-check.sh" >/dev/null 2>&1; }
roblox_running() { pgrep -x sober >/dev/null; }
settle() { local t0 l1 hv; t0=$(date +%s)      # tools/s4c_series.sh: 1-min load < 2 and no other >= 100 % process, 15 s checks, 10 min cap (wu3: wu2 had 5 min and its first settle took 301 s)
  while :; do l1=$(cut -d' ' -f1 /proc/loadavg); hv=$(heavy_ps)
    awk -v l="$l1" 'BEGIN{exit !(l<2)}' && [ -z "${hv// /}" ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[] ok"; return 0; }
    [ $(( $(date +%s) - t0 )) -ge 600 ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[$hv] cap"; return 1; }
    sleep 15; done; }
disp_wait() {   # X server from the previous load: give its socket and lock up to 20 s to go, then refuse
  for _ in 1 2 3 4 5 6 7 8 9 10; do disp_free >/dev/null 2>&1 && break; sleep 2; done
  disp_free >> $ST/series.log 2>&1; }
exe_ok() { pin run/bin/$EXE $EXE_SHA; }
claim_renew() { "$T/quiet-required.sh" claim $W 30 "$WHAT" >/dev/null 2>&1; }   # same-text renewal, exit code only
bench() {   # <tag> <exe> <save> <warm> <frames> <slow> <arm>: one load; returns the bench's rc
  local LEVER="" r; [ "$7" = on ] && LEVER="-alife_warmup"
  BENCH_DISPLAY=$DISP BENCH_NOCLAIM=1 BENCH_EXCLUSIVE=1 BENCH_SAVE=$3 BENCH_LOWEND=0 BENCH_FIXED=0 BENCH_TIMEOUT=$BENCH_TO \
    tools/s2_bench.sh "$2" "$1" "$4" "$5" $FLAGS -perf_slow "$6" $LEVER > $ST/run_$1.log 2>&1 & BP=$!
  wait $BP; r=$?; BP=""; return $r; }
check_run() {   # <tag> <arm>: the scorer's own per-load rules (one source; exit 0 go on, else stop); a re-run <slot>_r1 too
  local tag=${1,,} mode=check
  [ "$2" = prime ] && mode=probe
  python3 -I $WU/wu3_score.py $mode=$OUT/$tag order=$ORDER 2>&1; }

stop=""; sc=""; BP=""; LIVE=""; INTR=""; SNOTE=""; rc=0; nnc=0; nrr=0; CLAIMED=0; PROBED=0; SL_END=0
# ONE way out, also when cancelled (installed BEFORE the first queue operation): the stop record first, then the bench
# stopped and reaped, the WANT entry withdrawn, the slice released ONLY if this invocation acquired the claim
finish() { trap - EXIT TERM INT
  [ -n "$sc" ] || halt fault "driver exited without a recorded stop (rc $rc)"
  record_stop || echo "$SNOTE" >&2     # fix C: on failure rc != 0 and SNOTE goes into the log line and the notify; go on
  [ -n "$BP" ] && kill -0 $BP 2>/dev/null && { kill -TERM $BP 2>/dev/null; wait $BP 2>/dev/null; }
  "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
  if [ "$CLAIMED" = 1 ]; then
    "$T/quiet-required.sh" release $W >/dev/null 2>&1 || log "release exit code $? (claim may already have expired)"
  fi
  log "slice end: $(grep -c . $ST/done.txt) loads done, $(pending "$ORDER" | wc -l) slots left, $nrr of $R_MAX re-runs, $nnc not clean. $(cat $ST/stop.txt 2>/dev/null)${SNOTE:+ $SNOTE}"
  timeout 60 "$T/notify-overseer.sh" engineperf - <<<"EnginePerf wu3 alife_warmup series: slice ended $(date +%T), $(grep -c . $ST/done.txt) loads done, $(pending "$ORDER" | wc -l) slots left, $nrr of $R_MAX re-runs, $nnc not clean. $(cat $ST/stop.txt 2>/dev/null)${SNOTE:+ $SNOTE}. $([ "$PROBED" = 1 ] && echo "Not scored yet (score once by hand)." || echo "NOT scored: no passed prime probe (failed or not reached), no scored load ran, nothing is consumed; another attempt needs the supervisor's word.")" >/dev/null 2>&1
  exit $rc; }
trap finish EXIT; trap 'cancel; finish' TERM INT
# WANT queue and slice, decided by EXIT CODE only (wu2, unchanged). A TERM/INT during the claim call is deferred until
# CLAIMED records its outcome, then handled as a cancel
CANCEL=0
while :; do
  got=0
  trap 'CANCEL=1' TERM INT
  if ! title_running && ! roblox_running && ! caden_lock && \
     "$T/quiet-required.sh" claim $W 30 "$WHAT" >/dev/null 2>&1; then got=1; CLAIMED=1; fi
  trap 'cancel; finish' TERM INT
  [ "$CANCEL" = 1 ] && { cancel; finish; }
  [ "$got" = 1 ] && break
  "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
  sleep 60
done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
SL_END=$(( $(date +%s) + 3*3600 ))
log "slice granted, wu3, display $DISP, load $(cut -d' ' -f1-3 /proc/loadavg)"
# the wait for the slice can be long: the display must still be free now
disp_free >> $ST/series.log 2>&1 || { halt fault "display $DISP not free at the slice grant"; rc=2; finish; }
series_loop
finish
