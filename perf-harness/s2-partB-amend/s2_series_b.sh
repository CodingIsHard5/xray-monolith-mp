#!/usr/bin/env bash
# S2 experiment series (fixed simulation tick 30/50/60 Hz against stock; PREREG-S2-simtick). From tools/r1b1_series.sh.
# One slice, EXCLUSIVE, through the WANT queue; same box rules (WANT only if not holding, claim when granted, withdraw at
# once; settle before every run; stop on any failed run or failed check; withdraw BEFORE release at the end; at most 3 h).
# Order file: "tag|exe|save|lowend|warm|frames|fixed|cfg" per line; cfg = "var=value;..." set in run/appdata/user.ltx
# before the run. The engine saves user.ltx on exit, so EVERY run sets BOTH switches explicitly (unset = 0), and both are
# set back to 0 at the end.
# Per-run checks (a failure stops the series):
#  - both switches read back from the saved user.ltx equal what was set;
#  - the profiler shows the path asked for: site "Sim tick" present exactly when sim_tick > 0; in a gated run (sim_tick > 0,
#    fixed = 0) fewer updates than frames and at least one [SIMTICK] line; in every other run updates = frames and no
#    [SIMTICK] line.
set -u
cd "$(dirname "$0")/.."
T="$HOME/.claude/overseer/tools"; W=engineperf; ORDER=${S2_ORDER:-tools/s2_order.txt}; ST=out/s2_state; mkdir -p $ST
CFG=run/appdata/user.ltx; VARS="sim_tick r__framelimit"; EXE_SHA=${S2_EXE_SHA:?set S2_EXE_SHA to the registered sha256 of SimTick14.exe}
touch $ST/done.txt $ST/rerun.txt
WHAT="EXCLUSIVE: headless S2 fixed-tick timing runs (one engine at a time)"
log() { echo "$(date '+%F %T') $*" | tee -a $ST/series.log; }
pending() { cat <(grep -v '^#' "$ORDER") $ST/rerun.txt | grep . | while IFS='|' read -r tag rest; do grep -qx "$tag" $ST/done.txt || echo "$tag|$rest"; done; }
heavy_ps() { ps -eo pcpu,pid,comm,args --sort=-pcpu | awk 'NR>1 && $1>=100 && $3!~/wineserver|Xvfb/ && $3!="ps" {print $1"%:"$3}' | head -5 | tr '\n' ' '; }
settle() { local t0 l1 hv; t0=$(date +%s)
  while :; do l1=$(cut -d' ' -f1 /proc/loadavg); hv=$(heavy_ps)
    awk -v l="$l1" 'BEGIN{exit !(l<2)}' && [ -z "${hv// /}" ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[] ok"; return 0; }
    [ $(( $(date +%s) - t0 )) -ge 300 ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[$hv] cap"; return 1; }
    sleep 15; done; }
want_val() { local v; v=$(tr ';' '\n' <<<"$2" | awk -F= -v k="$1" '$1==k{print $2}'); [ -n "$v" ] && { echo "$v"; return; }; echo 0; }
set_cfg() { local tmp; tmp=$(mktemp); grep -v -E "^($(tr ' ' '|' <<<"$VARS")) " "$CFG" > "$tmp"
  for v in $VARS; do printf '%s %s\r\n' "$v" "$(want_val $v "$1")" >> "$tmp"; done; cat "$tmp" > "$CFG"; rm -f "$tmp"; }
read_cfg() { awk -v k="$1" '$1==k{gsub("\r",""); print $2}' "$CFG" | tail -1; }
check_run() { # tag cfg fixed frames -> "check ok ..." or "check FAIL: ..."; returns 1 on fail
  local R=${S2_CHECK_ROOT:-out}; local tag=$1 cfg=$2 fixed=$3 frames=$4 f=$R/$1/perf_sites_$1.csv L=$R/$1/xray.log v bad="" hz st up nl
  for v in $VARS; do [ "$(read_cfg $v)" = "$(want_val $v "$cfg")" ] || bad="$bad $v=$(read_cfg $v)!=$(want_val $v "$cfg")"; done
  hz=$(want_val sim_tick "$cfg")
  st=$(tr -d '\r' < "$f" 2>/dev/null | awk -F, '$1=="Sim tick"{print $4+$7}' | head -1); st=${st:-0}
  up=$(tr -d '\r' < "$f" 2>/dev/null | awk -F, '$1=="Process seqFrame"{print $4}' | head -1); up=${up:-0}
  nl=$(grep -a -c '\[SIMTICK\]' "$L" 2>/dev/null); nl=${nl:-0}
  # Amended (prereg section 11): the checks test the OUTCOME inside the capture (profiler counts), not a log line.
  # The [SIMTICK] line is printed by the real-time gate, which is also active OUTSIDE the capture window of a
  # fixed-step run with sim_tick > 0 (warm-up and quit frames), so its presence there says nothing about the capture.
  if [ "$hz" -gt 0 ]; then [ "$st" = "$up" ] && [ "$st" -gt 0 ] || bad="$bad sim-tick-site($st)!=updates($up)"; else [ "$st" = 0 ] || bad="$bad sim-tick-site-in-stock-run($st)"; fi
  if [ "$hz" -gt 0 ] && [ "$fixed" = 0 ]; then
    [ "$up" -gt 0 ] && [ "$up" -lt "$frames" ] || bad="$bad gated-run-updates($up)-not-below-frames($frames)"
    [ "$nl" -gt 0 ] || bad="$bad no-SIMTICK-line"
  else
    [ "$up" = "$frames" ] || bad="$bad updates($up)!=frames($frames)"
    [ "$hz" -gt 0 ] || [ "$nl" = 0 ] || bad="$bad SIMTICK-line-in-stock-run($nl)"
  fi
  [ -z "$bad" ] && { echo "check ok (updates $up of $frames frames, sim-tick site $st, SIMTICK lines $nl)"; return 0; }; echo "check FAIL:$bad"; return 1; }
[ "${1:-}" = "--check" ] && { CFG=${S2_CHECK_ROOT:-out}/$2/user.ltx.after; check_run "$2" "$3" "$4" "$5"; exit $?; }   # desk test of the check on a finished run folder
[ -s run/bin/SimTick14.exe ] && [ "$(sha256sum run/bin/SimTick14.exe | cut -d' ' -f1)" = "$EXE_SHA" ] || { log "REFUSED: SimTick14.exe hash differs"; exit 2; }
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 30 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
SL_END=$(( $(date +%s) + 3*3600 )); stop=""
cp "$CFG" $ST/user.ltx.before
log "slice granted, load $(cut -d' ' -f1-3 /proc/loadavg)"
while :; do
  line=$(pending | head -1); [ -n "$line" ] || break
  IFS='|' read -r tag exe save low warm frames fixed cfg <<<"$line"
  sr=$(settle); log "$sr before $tag"; case "$sr" in *cap) stop="not quiet before $tag"; break;; esac
  [ $(( SL_END - $(date +%s) )) -lt 900 ] && { stop="slice time used"; break; }
  "$T/protected-title-check.sh" >/dev/null 2>&1 && { stop="protected title running"; break; }
  pgrep -x sober >/dev/null && { stop="Roblox running"; break; }
  "$T/quiet-required.sh" claim $W 30 "$WHAT" >/dev/null 2>&1          # same-text renewal for this run
  set_cfg "$cfg"
  log "run $tag exe=$exe save=$save lowend=$low warm=$warm frames=$frames fixed=$fixed cfg=[$cfg] load $(cut -d' ' -f1-3 /proc/loadavg)"
  BENCH_NOCLAIM=1 BENCH_EXCLUSIVE=1 BENCH_SAVE=$save BENCH_LOWEND=$low BENCH_FIXED=$fixed tools/s2_bench.sh "$exe" "$tag" "$warm" "$frames" -perf_noinput -perf_nopresent -perf_slow 20 > $ST/run_$tag.log 2>&1; rc=$?
  cp "$CFG" out/$tag/user.ltx.after 2>/dev/null
  v=$(grep -a '^VERDICT' out/$tag/conditions.txt 2>/dev/null)
  log "run $tag rc=$rc $v"
  [ $rc = 0 ] || { stop="run $tag ended rc=$rc"; break; }
  c=$(check_run "$tag" "$cfg" "$fixed" "$frames"); crc=$?; log "run $tag $c"
  [ $crc = 0 ] || { stop="run $tag failed a check: $c"; break; }
  echo "$tag" >> $ST/done.txt
  # as in 1a: a timing run whose VERDICT is not "clean" is excluded and re-run ONCE at the end (never a re-run of a re-run)
  if [ "$v" != "VERDICT: clean" ] && [[ "$tag" != *_rr ]]; then echo "${tag}_rr|$exe|$save|$low|$warm|$frames|$fixed|$cfg" >> $ST/rerun.txt; log "run $tag not clean: excluded, re-run queued as ${tag}_rr"; fi
done
set_cfg ""   # leave both switches at 0 in the run dir
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1   # withdraw FIRST
log "slice end: $(grep -c . $ST/done.txt) done, $(pending | wc -l) left. ${stop:-list finished}"
timeout 60 "$T/notify-overseer.sh" engineperf - <<<"EnginePerf S2 tick series: slice ended $(date +%T), $(grep -c . $ST/done.txt) runs done, $(pending | wc -l) left IN THIS SLICE. ${stop:-list finished}. Nothing is scored until all 18 registered runs are done." >/dev/null 2>&1
