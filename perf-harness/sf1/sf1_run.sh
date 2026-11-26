#!/usr/bin/env bash
# DRAFT - NOT RUN, NOT REGISTERED. sf1 slow-frame classes driver (PREREG-sf1-slow-frame-classes-DRAFT.md section 7).
#   wt-render-split/perf-harness/sf1/sf1_run.sh [--preflight-only]
# Six EXCLUSIVE timing runs in ONE WANT slice (at most 3 h), in the order of sf1_order.txt (one source with the scorer):
# Coarse14.exe, stock frame loop, real time (BENCH_FIXED=0), uncapped, headless on its own display :141.
# Bench and flags as PREREG-S2-4c part B (tools/s4c_series.sh:74, tools/s2_bench.sh:53-56), with -perf_slow <slow_ms>
# from the order file in place of 20, and warm/frames from the order file.
# Box rules: hash preflight of every pinned file (missing = refused by name; PIN_ME_* = refused until registered);
# display :141 refused if /tmp/.X11-unix/X141 or /tmp/.X141-lock exists (checked at preflight, at the slice grant and
# before every run); WANT queue + claim/withdraw/release as tools/t3_kw1.sh; out folders must be new; stop on the first
# failed run (crash, timeout, missing capture) or failed check. A run whose VERDICT is not "clean" is re-run ONCE at the
# end as <tag>_rr (the rule of tools/s4c_series.sh; the scorer reads the same order file and the same rule).
# Not scored here. Score ONCE afterwards, output written in the same call:
#   python3 -I wt-render-split/perf-harness/sf1/sf1_classify.py root=out order=wt-render-split/perf-harness/sf1/sf1_order.txt out=out/sf1_state/sf1_score.txt
set -u
cd "$(dirname "$0")/../../.." || exit 2          # project root
T="$HOME/.claude/overseer/tools"; W=engineperf
SF=wt-render-split/perf-harness/sf1; ORDER=$SF/sf1_order.txt; ST=out/sf1_state
EXE=Coarse14.exe; CFG=run/appdata/user.ltx
FLAGS="-perf_noinput -perf_nopresent"          # + "-perf_slow <slow_ms>" per run (s4c_series.sh:74 default, slow value changed)
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
pin run/bin/$EXE                         65edcf401faeb125c68283084f55500c8d55cca58353e682f9ff8f96d884d88c || bad=1
pin run/appdata/savedgames/cordon1.scop  6dea752bc652a45c748e1438e3bc8ffd2d8d89f24730177c792328aeb5d13f3e || bad=1
pin run/appdata/savedgames/cordon1.scoc  a9f9a8a91b034de28a49be7f8029d823d057e46b4445f3f94d7255dbc77b0b6b || bad=1
pin run/appdata/savedgames/bench1.scop   891693c5e318ad1e97e8437476654200eb39d3bab89de79a778fa9b90ff89ffa || bad=1
pin run/appdata/savedgames/bench1.scoc   7c2ddf33ccdaa75c341ba8a7c3cd84cb454972e81a47ce844d6919cb2a2f167e || bad=1
pin $ORDER                               PIN_ME_ORDER    || bad=1
pin $SF/sf1_classify.py                  PIN_ME_SCORER   || bad=1
pin $SF/sf1_selftest.sh                  PIN_ME_SELFTEST || bad=1
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
tags=$(grep -v '^#' "$ORDER" 2>/dev/null | grep . | cut -d'|' -f1)
[ "$(echo "$tags" | grep -c .)" = 6 ] || { echo "REFUSED: $ORDER does not list 6 runs"; bad=1; }
for t in $tags; do
  [ "$t" = "${t,,}" ] || { echo "REFUSED: tag $t is not lowercase"; bad=1; }
  [ -e out/$t ] && { echo "REFUSED: out/$t exists"; bad=1; }
  [ -e out/${t}_rr ] && { echo "REFUSED: out/${t}_rr exists"; bad=1; }
done
grep -v '^#' "$ORDER" | grep . | awk -F'|' -v e=$EXE '$2!=e{print "REFUSED: order line uses " $2 ", not " e; f=1} END{exit f}' || bad=1
[ "$bad" = 0 ] || exit 2
[ "${1:-}" = "--preflight-only" ] && { echo "preflight ok"; exit 0; }

mkdir -p $ST; touch $ST/done.txt $ST/rerun.txt
WHAT="EXCLUSIVE: headless sf1 slow-frame timing runs (one engine at a time)"
log() { echo "$(date '+%F %T') $*" | tee -a $ST/series.log; }
pending() { cat <(grep -v '^#' "$ORDER") $ST/rerun.txt | grep . | while IFS='|' read -r tag rest; do grep -qx "$tag" $ST/done.txt || echo "$tag|$rest"; done; }
heavy_ps() { ps -eo pcpu,pid,comm,args --sort=-pcpu | awk 'NR>1 && $1>=100 && $3!~/wineserver|Xvfb/ && $3!="ps" {print $1"%:"$3}' | head -5 | tr '\n' ' '; }
settle() { local t0 l1 hv; t0=$(date +%s)      # tools/s4c_series.sh: 1-min load < 2 and no other >= 100 % process, 15 s checks, 5 min cap
  while :; do l1=$(cut -d' ' -f1 /proc/loadavg); hv=$(heavy_ps)
    awk -v l="$l1" 'BEGIN{exit !(l<2)}' && [ -z "${hv// /}" ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[] ok"; return 0; }
    [ $(( $(date +%s) - t0 )) -ge 300 ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[$hv] cap"; return 1; }
    sleep 15; done; }
check_run() {   # outcome inside the capture (as tools/s4c_series.sh check_run): <tag> <frames> <slow_ms>
  local tag=${1,,} n=$2 s=$3 d=out/${1,,} f bad=""
  for f in perf_frames_$tag.csv perf_sites_$tag.csv perf_slow_$tag.csv conditions.txt xray.log; do
    [ -s "$d/$f" ] || bad="$bad missing:$d/$f"; done
  [ -z "$bad" ] || { echo "check FAIL:$bad"; return 1; }
  local rows up st eco th
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
  [ -z "$bad" ] && { echo "check ok (frames $rows, seqFrame $up, sim tick 0, limiter 0, slow threshold $th)"; return 0; }
  echo "check FAIL:$bad"; return 1; }

# WANT queue and slice: tools/t3_kw1.sh (want only if not already holding; claim when granted; withdraw at once)
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      ! { [ -e run/.caden-play.lock ] && kill -0 "$(cat run/.caden-play.lock 2>/dev/null)" 2>/dev/null; } && \
      "$T/quiet-required.sh" claim $W 30 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
SL_END=$(( $(date +%s) + 3*3600 )); stop=""; BP=""; rc=0
log "slice granted, sf1, display $DISP, load $(cut -d' ' -f1-3 /proc/loadavg)"
# ONE way out, also when cancelled: the bench stopped and reaped (its own trap kills wine and its Xvfb), then the slice
# given back, withdraw FIRST (tools/t3_kw1.sh)
finish() { trap - EXIT TERM INT
  [ -n "$BP" ] && kill -0 $BP 2>/dev/null && { kill -TERM $BP 2>/dev/null; wait $BP 2>/dev/null; }
  "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1
  log "slice end: $(grep -c . $ST/done.txt) done, $(pending | wc -l) left. ${stop:-list finished}"
  timeout 60 "$T/notify-overseer.sh" engineperf - <<<"EnginePerf sf1 slow-frame series: slice ended $(date +%T), $(grep -c . $ST/done.txt) runs done, $(pending | wc -l) left. ${stop:-list finished}. Not scored yet (score once by hand)." >/dev/null 2>&1
  exit $rc; }
trap finish EXIT; trap 'stop="cancelled"; rc=143; finish' TERM INT
# the wait for the slice can be long: the display must still be free now
disp_free >> $ST/series.log 2>&1 || { stop="display $DISP not free at the slice grant"; rc=2; finish; }
while :; do
  line=$(pending | head -1); [ -n "$line" ] || break
  IFS='|' read -r tag exe save warm frames slow <<<"$line"
  sr=$(settle); log "$sr before $tag"; case "$sr" in *cap) stop="not quiet before $tag"; rc=1; break;; esac
  [ $(( SL_END - $(date +%s) )) -lt 900 ] && { stop="slice time used"; rc=1; break; }
  "$T/protected-title-check.sh" >/dev/null 2>&1 && { stop="protected title running"; rc=1; break; }
  pgrep -x sober >/dev/null && { stop="Roblox running"; rc=1; break; }
  { [ -e run/.caden-play.lock ] && kill -0 "$(cat run/.caden-play.lock 2>/dev/null)" 2>/dev/null; } && { stop="Caden's launcher running"; rc=1; break; }
  # X server from the previous run: give its socket and lock up to 20 s to go, then refuse
  for _ in 1 2 3 4 5 6 7 8 9 10; do disp_free >/dev/null 2>&1 && break; sleep 2; done
  disp_free >> $ST/series.log 2>&1 || { stop="display $DISP not free before $tag"; rc=1; break; }
  cfg_ok >> $ST/series.log 2>&1 || { stop="settings not stock/uncapped before $tag"; rc=1; break; }
  [ -e out/${tag,,} ] && { stop="out/${tag,,} exists before its run"; rc=1; break; }
  pin run/bin/$EXE 65edcf401faeb125c68283084f55500c8d55cca58353e682f9ff8f96d884d88c >> $ST/series.log 2>&1 || { stop="exe changed"; rc=1; break; }
  "$T/quiet-required.sh" claim $W 30 "$WHAT" >/dev/null 2>&1          # same-text renewal for this run (tools/s4c_series.sh)
  log "run $tag exe=$exe save=$save warm=$warm frames=$frames slow=$slow display=$DISP load $(cut -d' ' -f1-3 /proc/loadavg)"
  BENCH_DISPLAY=$DISP BENCH_NOCLAIM=1 BENCH_EXCLUSIVE=1 BENCH_SAVE=$save BENCH_LOWEND=0 BENCH_FIXED=0 BENCH_TIMEOUT=900 \
    tools/s2_bench.sh "$exe" "$tag" "$warm" "$frames" $FLAGS -perf_slow "$slow" > $ST/run_$tag.log 2>&1 & BP=$!
  wait $BP; brc=$?; BP=""
  v=$(grep -a '^VERDICT' out/${tag,,}/conditions.txt 2>/dev/null)
  log "run $tag rc=$brc $v"
  [ $brc = 0 ] || { stop="run $tag ended rc=$brc (crash, timeout or no capture)"; rc=1; break; }
  c=$(check_run "$tag" "$frames" "$slow"); crc=$?; log "run $tag $c"
  [ $crc = 0 ] || { stop="run $tag failed a check: $c"; rc=1; break; }
  echo "$tag" >> $ST/done.txt
  if [ "$v" != "VERDICT: clean" ] && [[ "$tag" != *_rr ]]; then
    echo "${tag}_rr|$exe|$save|$warm|$frames|$slow" >> $ST/rerun.txt; log "run $tag not clean: excluded, re-run queued as ${tag}_rr"; fi
done
finish
