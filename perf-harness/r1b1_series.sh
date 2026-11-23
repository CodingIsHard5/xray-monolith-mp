#!/usr/bin/env bash
# R1b-1 series (engine/perf-harness/PREREG-R1b-1-2026-11-23.md on perf/render-split, with amendments 1 and 2).
# One slice, EXCLUSIVE, through the WANT queue. Same box rules as tools/r1_series.sh (WANT only if not holding, claim
# when granted, withdraw at once; settle before every run; stop on any failed run or failed check; withdraw BEFORE
# release at the end; at most 3 h). Order file: "tag|exe|save|lowend|frames|cfg" per line; cfg = "var=value;..." set
# in run/appdata/user.ltx before the run. The engine saves user.ltx on exit (x_ray.cpp:305 cfg_save), so EVERY run
# sets ALL its switches explicitly; nothing is inherited from the previous run.
# Per-run checks (a failure stops the series):
#  - the switches read back from the saved user.ltx equal what was set;
#  - the profiler shows the path that was asked for: r1b_task_static on worker threads (calls_other > 0) when
#    r__mt_sun_static or r__mt_sun_selftest is 1, and not at all when both are 0;
#  - self-test runs: a summary line is present; 0 mismatches without the witness; with witness k, mismatches on
#    cascade k-1 only.
set -u
cd "$(dirname "$0")/.."
T="$HOME/.claude/overseer/tools"; W=engineperf; ORDER=${R1B_ORDER:-tools/r1b1_order.txt}; ST=out/r1b1_state; mkdir -p $ST
CFG=run/appdata/user.ltx; VARS="r__mt_sun_static r__mt_sun_selftest r__mt_sun_selftest_witness r__mt_sun_selftest_every r_stereo_bench"
touch $ST/done.txt $ST/rerun.txt
WHAT="EXCLUSIVE: headless R1b-1 sun-cascade timing runs (one engine at a time)"
log() { echo "$(date '+%F %T') $*" | tee -a $ST/series.log; }
pending() { cat <(grep -v '^#' "$ORDER") $ST/rerun.txt | grep . | while IFS='|' read -r tag rest; do grep -qx "$tag" $ST/done.txt || echo "$tag|$rest"; done; }
heavy_ps() { ps -eo pcpu,pid,comm,args --sort=-pcpu | awk 'NR>1 && $1>=100 && $3!~/wineserver|Xvfb/ && $3!="ps" {print $1"%:"$3}' | head -5 | tr '\n' ' '; }
settle() { local t0 l1 hv; t0=$(date +%s)
  while :; do l1=$(cut -d' ' -f1 /proc/loadavg); hv=$(heavy_ps)
    awk -v l="$l1" 'BEGIN{exit !(l<2)}' && [ -z "${hv// /}" ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[] ok"; return 0; }
    [ $(( $(date +%s) - t0 )) -ge 300 ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[$hv] cap"; return 1; }
    sleep 15; done; }
# value of var in the run's cfg string; defaults: every R1b switch 0, cadence 100, stereo 0
want_val() { local v; v=$(tr ';' '\n' <<<"$2" | awk -F= -v k="$1" '$1==k{print $2}'); [ -n "$v" ] && { echo "$v"; return; }
  case "$1" in r__mt_sun_selftest_every) echo 100;; *) echo 0;; esac; }
set_cfg() { local tmp; tmp=$(mktemp); grep -v -E "^($(tr ' ' '|' <<<"$VARS")) " "$CFG" > "$tmp"
  for v in $VARS; do printf '%s %s\r\n' "$v" "$(want_val $v "$1")" >> "$tmp"; done; cat "$tmp" > "$CFG"; rm -f "$tmp"; }
read_cfg() { awk -v k="$1" '$1==k{gsub("\r",""); print $2}' "$CFG" | tail -1; }
check_run() { # tag cfg -> prints "check ok" or "check FAIL: ..."; returns 1 on fail
  local tag=$1 cfg=$2 f=out/$1/perf_sites_$1.csv L=out/$1/xray.log v s=0 st w co bad=""
  for v in $VARS; do [ "$(read_cfg $v)" = "$(want_val $v "$cfg")" ] || bad="$bad $v=$(read_cfg $v)!=$(want_val $v "$cfg")"; done
  st=$(want_val r__mt_sun_selftest "$cfg"); w=$(want_val r__mt_sun_selftest_witness "$cfg")
  [ "$(want_val r__mt_sun_static "$cfg")" = 1 ] || [ "$st" = 1 ] && s=1
  co=$(awk -F, '$1=="r1b_task_static"{print $7}' "$f" 2>/dev/null | head -1)
  if [ $s = 1 ]; then [ "${co:-0}" -gt 0 ] || bad="$bad no-worker-tasks(calls_other=${co:-none})"
  else [ -z "$co" ] || [ "$co" = 0 ] || bad="$bad worker-tasks-in-OFF-run($co)"; fi
  if [ "$st" = 1 ]; then
    local sum; sum=$(grep -a 'r1b_selftest_summary' "$L" | tail -1)
    [ -n "$sum" ] || bad="$bad no-selftest-summary"
    # "| eye0/mono compares A mismatches c0 B c1 C c2 D | eye1 compares E mismatches c0 F c1 G c2 H |"
    read -r a b c d e g h i <<<"$(sed -E 's/.*eye0\/mono compares ([0-9]+) mismatches c0 ([0-9]+) c1 ([0-9]+) c2 ([0-9]+) \| eye1 compares ([0-9]+) mismatches c0 ([0-9]+) c1 ([0-9]+) c2 ([0-9]+).*/\1 \2 \3 \4 \5 \6 \7 \8/' <<<"$sum")"
    echo "selftest $tag: compares eye0 ${a:-?} eye1 ${e:-?} | mism eye0 ${b:-?}/${c:-?}/${d:-?} eye1 ${g:-?}/${h:-?}/${i:-?} | $sum" >> $ST/selftest.txt
    local rn; rn=$(sed -E 's/.*renders ([0-9]+) .*/\1/' <<<"$sum")
    if ! [[ "$a$b$c$d$e$g$h$i$rn" =~ ^[0-9]+$ ]] || [ -z "$i" ]; then bad="$bad selftest-summary-unparsed"; a=0 b=0 c=0 d=0 e=0 g=0 h=0 i=0 rn=0; fi
    # minimum counts (prereg amendment 1): >= 1,000 renders; stereo: each eye >= 500 renders = 1,500 compares (3 cascades)
    [ "$rn" -ge 1000 ] || bad="$bad too-few-renders($rn)"
    [ "$(want_val r_stereo_bench "$cfg")" = 1 ] && { [ "$a" -ge 1500 ] && [ "$e" -ge 1500 ] || bad="$bad eye-compares-low(eye0=$a,eye1=$e)"; }
    if [ "$w" = 0 ]; then [ "$((b+c+d+g+h+i))" = 0 ] 2>/dev/null || bad="$bad SELFTEST-MISMATCH"
    else local m=(0 0 0); m[0]=$((b+g)); m[1]=$((c+h)); m[2]=$((d+i)); local k=$((w-1)) j
      [ "${m[$k]}" -gt 0 ] || bad="$bad WITNESS-NOT-DETECTED(cascade $k)"
      for j in 0 1 2; do [ $j = $k ] || [ "${m[$j]}" = 0 ] || bad="$bad WITNESS-LEAK(cascade $j=${m[$j]})"; done; fi
  fi
  [ -z "$bad" ] && { echo "check ok (worker calls_other=${co:-0})"; return 0; }; echo "check FAIL:$bad"; return 1; }
[ -n "$(pending)" ] || { log "nothing pending"; exit 0; }
[ -s run/bin/R1b14.exe ] && [ "$(sha256sum run/bin/R1b14.exe | cut -c1-16)" = 30ee2a5b9cb76659 ] || { log "REFUSED: R1b14.exe hash differs"; exit 2; }
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 30 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
SL_END=$(( $(date +%s) + 3*3600 )); stop=""
cp "$CFG" $ST/user.ltx.before
log "slice granted, load $(cut -d' ' -f1-3 /proc/loadavg)"
while :; do
  line=$(pending | head -1); [ -n "$line" ] || break
  IFS='|' read -r tag exe save low frames cfg <<<"$line"
  sr=$(settle); log "$sr before $tag"; case "$sr" in *cap) stop="not quiet before $tag"; break;; esac
  [ $(( SL_END - $(date +%s) )) -lt 900 ] && { stop="slice time used"; break; }
  "$T/protected-title-check.sh" >/dev/null 2>&1 && { stop="protected title running"; break; }
  pgrep -x sober >/dev/null && { stop="Roblox running"; break; }
  "$T/quiet-required.sh" claim $W 30 "$WHAT" >/dev/null 2>&1          # same-text renewal for this run
  set_cfg "$cfg"
  log "run $tag exe=$exe save=$save lowend=$low frames=$frames cfg=[$cfg] load $(cut -d' ' -f1-3 /proc/loadavg)"
  BENCH_NOCLAIM=1 BENCH_EXCLUSIVE=1 BENCH_SAVE=$save BENCH_LOWEND=$low tools/bench.sh "$exe" "$tag" 600 "$frames" -perf_noinput -perf_nopresent -perf_slow 20 > $ST/run_$tag.log 2>&1; rc=$?
  cp "$CFG" out/$tag/user.ltx.after 2>/dev/null
  v=$(grep -a '^VERDICT' out/$tag/conditions.txt 2>/dev/null)
  log "run $tag rc=$rc $v"
  [ $rc = 0 ] || { stop="run $tag ended rc=$rc"; break; }
  c=$(check_run "$tag" "$cfg"); crc=$?; log "run $tag $c"
  [ $crc = 0 ] || { stop="run $tag failed a check: $c"; break; }
  echo "$tag" >> $ST/done.txt
  # as in 1a: a timing run whose VERDICT is not "clean" is excluded and re-run ONCE at the end (never a re-run of a re-run)
  if [ "$v" != "VERDICT: clean" ] && [[ "$tag" != *_rr ]]; then echo "${tag}_rr|$exe|$save|$low|$frames|$cfg" >> $ST/rerun.txt; log "run $tag not clean: excluded, re-run queued as ${tag}_rr"; fi
done
set_cfg ""   # leave every R1b switch at 0 in the run dir
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1   # withdraw FIRST
log "slice end: $(grep -c . $ST/done.txt) done, $(pending | wc -l) left. ${stop:-list finished}"
timeout 60 "$T/notify-overseer.sh" engineperf - <<<"EnginePerf R1b-1 series: slice ended $(date +%T), $(grep -c . $ST/done.txt) runs done, $(pending | wc -l) left. ${stop:-list finished}. Scoring next." >/dev/null 2>&1
