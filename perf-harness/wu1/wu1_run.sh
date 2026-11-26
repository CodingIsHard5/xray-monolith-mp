#!/usr/bin/env bash
# DRAFT - NOT RUN, NOT REGISTERED (only `bash -n` checked). wu1 driver (PREREG-wu1-alife-warmup-DRAFT.md section 7).
#   wt-render-split/perf-harness/wu1/wu1_run.sh [--preflight-only]
# 33 EXCLUSIVE loads (1 prime + 16 lever ON + 16 lever OFF, ABBA) in ONE WANT slice (at most 3 h), in the order of
# wu1_order.txt (one source with the scorer): Warm18.exe, cordon1, real time (BENCH_FIXED=0), uncapped, capture from the
# first frame after the loading screen (-perf_capture 0,<frames>,<tag>), headless on its own display :141.
# Bench and flags as sf1 (tools/s2_bench.sh, -perf_noinput -perf_nopresent -perf_slow 20); ON loads add -alife_warmup.
# Box rules: hash preflight of every pinned file (missing = refused by name; PIN_ME_* = refused until registered);
# display :141 refused if /tmp/.X11-unix/X141 or /tmp/.X141-lock exists (preflight, slice grant, before every load);
# claim/renew/release decided by EXIT CODE ONLY (quiet-required.sh header: "THE EXIT CODE IS THE API. DO NOT PARSE THE
# OUTPUT"); out folders must be new. STOP on: bench rc != 0 (crash, timeout, no capture), any failed check after a load
# (files, frames, stock loop, limiter, slow threshold, command line, lever log check), a refused claim renewal, the third
# CONFOUNDED load, display not free, settings not stock, settle cap, < 15 min left, protected title, Roblox, Caden's
# launcher. NO RETRY: a load is never run twice; a stopped series is not resumed without a new GO.
# Not scored here. Score ONCE afterwards, output written in the same call:
#   python3 -I wt-render-split/perf-harness/wu1/wu1_score.py root=out order=wt-render-split/perf-harness/wu1/wu1_order.txt out=out/wu1_state/wu1_score.txt
set -u
cd "$(dirname "$0")/../../.." || exit 2          # project root
T="$HOME/.claude/overseer/tools"; W=engineperf
WU=wt-render-split/perf-harness/wu1; ORDER=$WU/wu1_order.txt; ST=out/wu1_state
EXE=Warm18.exe; EXE_SHA=15828a7a6cadd38e42cc376869f581166884c04f1eb236baf79054a74332ae9d; CFG=run/appdata/user.ltx
FLAGS="-perf_noinput -perf_nopresent"          # + "-perf_slow <slow_ms>" per load, + "-alife_warmup" on ON loads
NLOADS=33; NON=16; NOFF=16; MAX_CONFOUNDED=3
pin() {   # <file> <sha256>   (tools/t3_kw1.sh)
  local f=$1 want=$2 got
  [ -e "$f" ] || { echo "REFUSED: $f is missing"; return 1; }
  case "$want" in PIN_ME_*) echo "REFUSED: $f has no registered hash ($want)"; return 1 ;; esac
  got=$(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)
  [ "$got" = "$want" ] || { echo "REFUSED: $f is not ${want:0:8} (it is ${got:0:8})"; return 1; }
}
bad=0
pin tools/s2_bench.sh                    072af8edb67eab095ea4408f3950a428709757b3ce1abdfe25efc25a3fb150b5 || bad=1
pin tools/lib_engine_pids.sh             cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e || bad=1
pin tools/run_noguard.sh                 92729abc420784053cee4b1b1832f5d112f74979a4760e2173d45578358455d5 || bad=1
pin run/bin/$EXE                         $EXE_SHA || bad=1
pin run/appdata/savedgames/cordon1.scop  6dea752bc652a45c748e1438e3bc8ffd2d8d89f24730177c792328aeb5d13f3e || bad=1
pin run/appdata/savedgames/cordon1.scoc  a9f9a8a91b034de28a49be7f8029d823d057e46b4445f3f94d7255dbc77b0b6b || bad=1
pin $ORDER                               PIN_ME_ORDER    || bad=1
pin $WU/wu1_score.py                     PIN_ME_SCORER   || bad=1
pin $WU/wu1_selftest.sh                  PIN_ME_SELFTEST || bad=1
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
lines=$(grep -v '^#' "$ORDER" 2>/dev/null | grep .)
[ "$(echo "$lines" | grep -c .)" = $NLOADS ] || { echo "REFUSED: $ORDER does not list $NLOADS loads"; bad=1; }
[ "$(echo "$lines" | grep -c '|on$')" = $NON ] && [ "$(echo "$lines" | grep -c '|off$')" = $NOFF ] && \
  [ "$(echo "$lines" | grep -c '|prime$')" = 1 ] || { echo "REFUSED: $ORDER is not 1 prime + $NON on + $NOFF off"; bad=1; }
echo "$lines" | awk -F'|' -v e=$EXE 'NF!=7 || $2!=e || $3!="cordon1" || $4!="0" || $5!="10000" || $6!="20" || $7!~/^(on|off|prime)$/ {print "REFUSED: order line " $0 " is not <tag>|" e "|cordon1|0|10000|20|on/off/prime"; f=1} END{exit f}' || bad=1
for t in $(echo "$lines" | cut -d'|' -f1); do
  [ "$t" = "${t,,}" ] || { echo "REFUSED: tag $t is not lowercase"; bad=1; }
  [ -e out/$t ] && { echo "REFUSED: out/$t exists"; bad=1; }
done
[ "$(echo "$lines" | cut -d'|' -f1 | sort | uniq -d | grep -c .)" = 0 ] || { echo "REFUSED: a tag is listed twice"; bad=1; }
[ "$bad" = 0 ] || exit 2
[ "${1:-}" = "--preflight-only" ] && { echo "preflight ok"; exit 0; }

mkdir -p $ST; touch $ST/done.txt
WHAT="EXCLUSIVE: headless wu1 alife_warmup loads (one engine at a time)"
log() { echo "$(date '+%F %T') $*" | tee -a $ST/series.log; }
pending() { grep -v '^#' "$ORDER" | grep . | while IFS='|' read -r tag rest; do grep -qx "$tag" $ST/done.txt || echo "$tag|$rest"; done; }
heavy_ps() { ps -eo pcpu,pid,comm,args --sort=-pcpu | awk 'NR>1 && $1>=100 && $3!~/wineserver|Xvfb/ && $3!="ps" {print $1"%:"$3}' | head -5 | tr '\n' ' '; }
caden_lock() { [ -e run/.caden-play.lock ] && kill -0 "$(cat run/.caden-play.lock 2>/dev/null)" 2>/dev/null; }
settle() { local t0 l1 hv; t0=$(date +%s)      # tools/s4c_series.sh: 1-min load < 2 and no other >= 100 % process, 15 s checks, 5 min cap
  while :; do l1=$(cut -d' ' -f1 /proc/loadavg); hv=$(heavy_ps)
    awk -v l="$l1" 'BEGIN{exit !(l<2)}' && [ -z "${hv// /}" ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[] ok"; return 0; }
    [ $(( $(date +%s) - t0 )) -ge 300 ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[$hv] cap"; return 1; }
    sleep 15; done; }
check_run() {   # outcome inside the capture and the log: <tag> <frames> <slow_ms> <arm>
  local tag=${1,,} n=$2 s=$3 arm=$4 d=out/${1,,} f bad=""
  for f in perf_frames_$tag.csv perf_sites_$tag.csv perf_slow_$tag.csv conditions.txt xray.log; do
    [ -s "$d/$f" ] || bad="$bad missing:$d/$f"; done
  [ -z "$bad" ] || { echo "check FAIL:$bad"; return 1; }
  local rows up st eco th lev
  rows=$(tr -d '\r' < $d/perf_frames_$tag.csv | grep -c '^[0-9]')
  up=$(tr -d '\r' < $d/perf_sites_$tag.csv | awk -F, '$1=="Process seqFrame"{print $4}' | head -1)
  st=$(tr -d '\r' < $d/perf_sites_$tag.csv | awk -F, '$1=="Sim tick"{s+=$4+$7} END{print s+0}')
  eco=$(tr -d '\r' < $d/perf_sites_$tag.csv | awk -F, '$1=="Eco Render"{s+=$4+$7} END{print s+0}')
  th=$(head -1 $d/perf_slow_$tag.csv | tr -d '\r' | cut -d, -f2)
  [ "$rows" = "$n" ] || bad="$bad frames($rows)!=$n"
  [ "${up:-0}" = "$n" ] || bad="$bad seqFrame-calls(${up:-none})!=$n"
  [ "$st" = 0 ] || bad="$bad sim-tick-calls($st)"
  [ "$eco" = 0 ] || bad="$bad frame-limiter-calls($eco)"
  awk -v a="$th" -v b="$s" 'BEGIN{exit !(a-b<0.0005 && b-a<0.0005)}' || bad="$bad slow-threshold($th)!=$s"
  # command line and lever log check (same rules as wu1_score.py; the scorer decides, this only stops the series early)
  lev=$(tr -d '\r' < $d/xray.log | awk -v arm="$arm" -v cap="0,$n,$tag" '
    /^command line / && !cmd { cmd=1; w=(index($0" ", " -alife_warmup ")>0); fx=(index($0" ", " -perf_fixed_dt ")>0); pc=(index($0" ", " -perf_capture " cap " ")>0) }
    $0=="intro_start game_loaded" && !intro { intro=NR }
    $0=="* [x-ray]: Handled Necessary Textures Destruction" && !hand { hand=NR }
    /\[alife_warmup\]/ { aw++ }
    /^\* \[alife_warmup\] full pass: / { full++; if (full==1) { fl=NR; N=$5+0; M=$7+0; P=$12+0 } }
    /^! \[alife_warmup\] hold cap hit/ { capl++ }
    END { r="";
      if (!cmd) r=r " no-command-line";
      if (cmd && w!=(arm=="on")) r=r " -alife_warmup-" (w ? "present" : "absent") "-on-" arm;
      if (fx) r=r " -perf_fixed_dt-present";
      if (cmd && !pc) r=r " -perf_capture-not-" cap;
      if (!intro) r=r " no-intro_start";
      if (arm=="off" && aw) r=r " off_not_stock(" aw "-lever-lines)";
      if (arm=="on") {
        if (full!=1) r=r " lever_count(" full+0 ")";
        else { if (!(fl<intro && fl<hand)) r=r " lever_after_intro"; if (P<3) r=r " lever_precache(" P ")"; if (N<0.9*M) r=r " lever_partial(" N "/" M ")" }
        if (capl) r=r " lever_cap";
      }
      if (r=="") print "ok" (arm=="on" ? " full pass " N "/" M " precache_left " P : ""); else print "FAIL" r }')
  case "$lev" in ok*) ;; *) bad="$bad log:${lev#FAIL}";; esac
  [ -z "$bad" ] && { echo "check ok (frames $rows, seqFrame $up, sim tick 0, limiter 0, slow threshold $th, log $lev)"; return 0; }
  echo "check FAIL:$bad"; return 1; }

# WANT queue and slice. Every decision on the EXIT CODE of quiet-required.sh (its header: "THE EXIT CODE IS THE API.
# DO NOT PARSE THE OUTPUT"): claim exit 0 = granted; otherwise queue with want (its exit code is not acted on: the loop
# simply tries claim again in 60 s) and wait. After the grant, withdraw from the queue at once (tools/t3_kw1.sh).
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && ! caden_lock && \
      "$T/quiet-required.sh" claim $W 30 "$WHAT" >/dev/null 2>&1; do
  "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
  sleep 60
done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
SL_END=$(( $(date +%s) + 3*3600 )); stop=""; BP=""; rc=0; nconf=0
log "slice granted, wu1, display $DISP, load $(cut -d' ' -f1-3 /proc/loadavg)"
# ONE way out, also when cancelled: the bench stopped and reaped (its own trap kills wine and its Xvfb), then the slice
# given back, withdraw FIRST (tools/t3_kw1.sh)
finish() { trap - EXIT TERM INT
  [ -n "$BP" ] && kill -0 $BP 2>/dev/null && { kill -TERM $BP 2>/dev/null; wait $BP 2>/dev/null; }
  "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
  "$T/quiet-required.sh" release $W >/dev/null 2>&1 || log "release exit code $? (claim may already have expired)"
  log "slice end: $(grep -c . $ST/done.txt) done, $(pending | wc -l) left, $nconf confounded. ${stop:-list finished}"
  timeout 60 "$T/notify-overseer.sh" engineperf - <<<"EnginePerf wu1 alife_warmup series: slice ended $(date +%T), $(grep -c . $ST/done.txt) loads done, $(pending | wc -l) left, $nconf confounded. ${stop:-list finished}. Not scored yet (score once by hand)." >/dev/null 2>&1
  exit $rc; }
trap finish EXIT; trap 'stop="cancelled"; rc=143; finish' TERM INT
# the wait for the slice can be long: the display must still be free now
disp_free >> $ST/series.log 2>&1 || { stop="display $DISP not free at the slice grant"; rc=2; finish; }
while :; do
  line=$(pending | head -1); [ -n "$line" ] || break
  IFS='|' read -r tag exe save warm frames slow arm <<<"$line"
  sr=$(settle); log "$sr before $tag"; case "$sr" in *cap) stop="not quiet before $tag"; rc=1; break;; esac
  [ $(( SL_END - $(date +%s) )) -lt 900 ] && { stop="slice time used"; rc=1; break; }
  "$T/protected-title-check.sh" >/dev/null 2>&1 && { stop="protected title running"; rc=1; break; }
  pgrep -x sober >/dev/null && { stop="Roblox running"; rc=1; break; }
  caden_lock && { stop="Caden's launcher running"; rc=1; break; }
  # X server from the previous load: give its socket and lock up to 20 s to go, then refuse
  for _ in 1 2 3 4 5 6 7 8 9 10; do disp_free >/dev/null 2>&1 && break; sleep 2; done
  disp_free >> $ST/series.log 2>&1 || { stop="display $DISP not free before $tag"; rc=1; break; }
  cfg_ok >> $ST/series.log 2>&1 || { stop="settings not stock/uncapped/lever-off before $tag"; rc=1; break; }
  [ -e out/${tag,,} ] && { stop="out/${tag,,} exists before its load"; rc=1; break; }
  pin run/bin/$EXE $EXE_SHA >> $ST/series.log 2>&1 || { stop="exe changed"; rc=1; break; }
  # same-text renewal for this load (tools/s4c_series.sh), decided by exit code
  "$T/quiet-required.sh" claim $W 30 "$WHAT" >/dev/null 2>&1 || { stop="claim renewal refused before $tag (exit $?)"; rc=1; break; }
  LEVER=""; [ "$arm" = on ] && LEVER="-alife_warmup"
  log "load $tag arm=$arm exe=$exe save=$save warm=$warm frames=$frames slow=$slow display=$DISP load $(cut -d' ' -f1-3 /proc/loadavg)"
  BENCH_DISPLAY=$DISP BENCH_NOCLAIM=1 BENCH_EXCLUSIVE=1 BENCH_SAVE=$save BENCH_LOWEND=0 BENCH_FIXED=0 BENCH_TIMEOUT=900 \
    tools/s2_bench.sh "$exe" "$tag" "$warm" "$frames" $FLAGS -perf_slow "$slow" $LEVER > $ST/run_$tag.log 2>&1 & BP=$!
  wait $BP; brc=$?; BP=""
  v=$(grep -a '^VERDICT' out/${tag,,}/conditions.txt 2>/dev/null)
  log "load $tag rc=$brc $v"
  echo "$tag" >> $ST/done.txt                     # ran (whatever happened): never run again (no retry)
  [ $brc = 0 ] || { stop="load $tag ended rc=$brc (crash, timeout or no capture)"; rc=1; break; }
  c=$(check_run "$tag" "$frames" "$slow" "$arm"); crc=$?; log "load $tag $c"
  [ $crc = 0 ] || { stop="load $tag failed a check: $c"; rc=1; break; }
  if [ "$v" != "VERDICT: clean" ]; then
    nconf=$((nconf+1)); log "load $tag not clean ($nconf of $MAX_CONFOUNDED allowed): the scorer voids it, no re-run"
    [ $nconf -ge $MAX_CONFOUNDED ] && { stop="$nconf confounded loads: the box is not quiet"; rc=1; break; }
  fi
done
finish
