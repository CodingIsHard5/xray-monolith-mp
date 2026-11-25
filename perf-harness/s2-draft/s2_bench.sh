#!/usr/bin/env bash
# Headless GAMMA benchmark run on the project's own run dir (never Caden's install).
# Usage: tools/bench.sh <exe in run/bin> <tag> [warmup] [frames] [extra engine args...]
# Output: out/<tag>/ (engine log, wine stderr, perf_sites_<tag>.csv, perf_frames_<tag>.csv)
# Refuses to start, and kills itself, when a protected title (Caden's game) is running: exit code only.
set -u
. "$(cd "$(dirname "$0")" && pwd)/lib_engine_pids.sh"   # engine lookup by Wine binary, never a bare name match
P="$(cd "$(dirname "$0")/.." && pwd)"; RUN="$P/run"
EXE="${1:?exe}"; TAG="${2:?tag}"; TAG="${TAG,,}"; WARM="${3:-600}"; FRAMES="${4:-3000}"; shift 4 2>/dev/null || shift $#
# S2 (copy of bench.sh; ONE change): BENCH_FIXED=0 leaves out -perf_fixed_dt, so the run uses real time. Default 1 = bench.sh.
FIXED="-perf_fixed_dt"; [ "${BENCH_FIXED:-1}" = 0 ] && FIXED=""
SAVE="${BENCH_SAVE:-bench1}"; DISP="${BENCH_DISPLAY:-:77}"; LIMIT="${BENCH_TIMEOUT:-900}"
T="$HOME/.claude/overseer/tools"; W=engineperf
OUT="$P/out/$TAG"; mkdir -p "$OUT"
[ -s "$RUN/bin/$EXE" ] || { echo "no exe $RUN/bin/$EXE"; exit 2; }
[ "$(stat -c %s "$RUN/bin/$EXE")" -gt 1000000 ] || { echo "exe is a stub (failed build?)"; exit 2; }
if "$T/protected-title-check.sh" >/dev/null 2>&1; then echo "REFUSED: protected title running"; exit 3; fi
"$P/tools/run_noguard.sh" "$RUN" || exit 7   # no path from run/ may write into ~/Games (Overseer 2026-11-22)
# BENCH_EXCLUSIVE=1: timing run. Needs the whole box (Overseer rule 2026-11-18): EXCLUSIVE claim, load
# logged at start and end, result marked CONFOUNDED if another heavy process was present.
# Without it: a functional run (smoke test, state-diff harness); its timings are NOT measurements.
X="${BENCH_EXCLUSIVE:-0}"; KIND="functional"; [ "$X" = 1 ] && KIND="EXCLUSIVE timing"
# BENCH_NOCLAIM=1: a series driver (tools/r1_series.sh) already holds the EXCLUSIVE claim for the slice; no claim or release here.
if [ "${BENCH_NOCLAIM:-0}" != 1 ] && ! "$T/quiet-required.sh" claim $W $((LIMIT/60+5)) "headless Xvfb GAMMA engine benchmark $TAG ($KIND)" >"$OUT/claim.txt" 2>&1; then
  echo "REFUSED: no quiet slot ($(tail -1 "$OUT/claim.txt"))"; exit 3; fi
# Own engine shows up as comm "X-RAY Primary t": excluded by its command line (this project's run dir), not by name,
# so another worker's X-Ray engine still counts as a heavy other.
others() { ps -eo pcpu,pid,comm,args --sort=-pcpu | awk -v own="$RUN" -v ownw="$(echo "${RUN,,}" | sed 's|/|\\\\|g')" 'NR>1 && $1>=100 && index(tolower($0), tolower(own))==0 && index(tolower($0), ownw)==0 && $3!~/wineserver|Xvfb/ && $3!="ps" {print $1"%:"$3}' | head -5 | tr '\n' ' '; }
echo "start $(date '+%F %T') kind=$KIND loadavg=$(cut -d' ' -f1-3 /proc/loadavg) heavy_others=[$(others)]" > "$OUT/conditions.txt"
export WINEPREFIX="$RUN/prefix" DISPLAY="$DISP"
XVFB_PID=""; WM_PID=""
cleanup() {
  wineserver -k >/dev/null 2>&1
  [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  [ "${BENCH_NOCLAIM:-0}" = 1 ] || "$T/quiet-required.sh" release $W >/dev/null 2>&1
}
trap cleanup EXIT
if [ ! -e "/tmp/.X11-unix/X${DISP#:}" ]; then Xvfb "$DISP" -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & XVFB_PID=$!; sleep 2; fi
if ! wmctrl -m >/dev/null 2>&1; then openbox >/dev/null 2>&1 & WM_PID=$!
  for _ in $(seq 1 30); do wmctrl -m >/dev/null 2>&1 && break; sleep 0.5; done; fi
LOG="$RUN/appdata/logs/xray_$(whoami).log"
rm -f "$LOG" "$RUN"/appdata/logs/perf_*_"$TAG".csv
DESK="eperf$$"
# BENCH_LOWEND=1: constrained run (ranks causes, gives no verdict): 4 physical cores, 8 GB memory limit without swap,
# DXVK reports a 2 GB video card. No root needed. CPU speed, GPU speed, disk and native Windows are NOT imitated.
LOWPFX=""; if [ "${BENCH_LOWEND:-0}" = 1 ]; then
  LOWPFX="taskset -c ${BENCH_CORES:-0-3} systemd-run --user --scope -q -p MemoryMax=${BENCH_MEM:-8G} -p MemorySwapMax=0"
  printf 'dxgi.maxDeviceMemory = %s\n' "${BENCH_VRAM_MB:-2048}" > "$OUT/dxvk_lowend.conf"; export DXVK_CONFIG_FILE="$OUT/dxvk_lowend.conf"
  echo "lowend: cores=${BENCH_CORES:-0-3} mem=${BENCH_MEM:-8G} vram_reported_mb=${BENCH_VRAM_MB:-2048}" >> "$OUT/conditions.txt"
fi
PEAK=0
( cd "$RUN" && ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy WINEDLLOVERRIDES="d3dcompiler_47=n" WINEDEBUG=-all \
  DXVK_LOG_LEVEL=info DXVK_LOG_PATH="$OUT" $LOWPFX nice -n 10 wine explorer "/desktop=$DESK,1280x720" \
  "Z:${RUN//\//\\}\\bin\\$EXE" -nointro -dbg -start "server($SAVE/single/alife/load)" "client(localhost)" \
  -perf_capture "$WARM,$FRAMES,$TAG" $FIXED -perf_quit "$@" > "$OUT/wine_stderr.log" 2>&1 ) &
START=$(date +%s); RC=1
while :; do
  sleep 5
  for EP in $(engine_pids "$EXE"); do [ -z "${AFF:-}" ] && AFF=$(awk '/Cpus_allowed_list/{print $2}' /proc/$EP/status 2>/dev/null) && [ -n "$AFF" ] && echo "engine pid $EP cpus_allowed=$AFF cgroup=$(tail -1 /proc/$EP/cgroup | cut -c1-120)" >> "$OUT/conditions.txt"; R=$(awk '/VmRSS/{print $2}' /proc/$EP/status 2>/dev/null); [ "${R:-0}" -gt "$PEAK" ] && PEAK=$R; done
  if "$T/protected-title-check.sh" >/dev/null 2>&1; then echo "ABORT: protected title started"; RC=3; break; fi
  if [ -s "$RUN/appdata/logs/perf_sites_$TAG.csv" ] && grep -qa "xrprof\] capture '$TAG' finished" "$LOG" 2>/dev/null; then RC=0; sleep 8; break; fi
  if [ -z "$(engine_main "$EXE")" ] && [ $(( $(date +%s) - START )) -gt 30 ]; then echo "engine exited without a capture"; RC=4; break; fi
  [ $(( $(date +%s) - START )) -gt "$LIMIT" ] && { echo "TIMEOUT after ${LIMIT}s"; RC=5; break; }
  # nudge past any "press any key" screen (harmless once in game)
  if ! grep -qa "xrprof\] capture '$TAG' started" "$LOG" 2>/dev/null; then
    w=$(xdotool search --name "$DESK" 2>/dev/null | head -1); [ -n "$w" ] && xdotool windowactivate --sync "$w" key space 2>/dev/null
  fi
done
echo "engine peak resident memory (sampled every 5 s): $((PEAK/1024)) MiB" >> "$OUT/conditions.txt"
HO="$(others)"; echo "end   $(date '+%F %T') loadavg=$(cut -d' ' -f1-3 /proc/loadavg) heavy_others=[$HO]" >> "$OUT/conditions.txt"
if [ "$X" != 1 ]; then echo "VERDICT: functional run, timings are not measurements" >> "$OUT/conditions.txt"
elif [ -n "$HO" ] || grep -q "^start.*heavy_others=\[[^]]" "$OUT/conditions.txt"; then echo "VERDICT: CONFOUNDED (other heavy process present)" >> "$OUT/conditions.txt"
else echo "VERDICT: clean" >> "$OUT/conditions.txt"; fi
cat "$OUT/conditions.txt"
cp "$LOG" "$OUT/xray.log" 2>/dev/null
mv "$RUN"/appdata/logs/perf_*_"$TAG".csv "$OUT/" 2>/dev/null
echo "rc=$RC elapsed=$(( $(date +%s) - START ))s out=$OUT"; grep -a "xrprof" "$OUT/xray.log" 2>/dev/null | tail -3
exit $RC
