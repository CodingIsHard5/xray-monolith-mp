#!/usr/bin/env bash
# Headless GAMMA benchmark run on the project's own run dir (never Caden's install).
# Usage: tools/bench.sh <exe in run/bin> <tag> [warmup] [frames] [extra engine args...]
# Output: out/<tag>/ (engine log, wine stderr, perf_sites_<tag>.csv, perf_frames_<tag>.csv)
# Refuses to start, and kills itself, when a protected title (Caden's game) is running: exit code only.
set -u
P="$(cd "$(dirname "$0")/.." && pwd)"; RUN="$P/run"
EXE="${1:?exe}"; TAG="${2:?tag}"; TAG="${TAG,,}"; WARM="${3:-600}"; FRAMES="${4:-3000}"; shift 4 2>/dev/null || shift $#
SAVE="${BENCH_SAVE:-bench1}"; DISP="${BENCH_DISPLAY:-:77}"; LIMIT="${BENCH_TIMEOUT:-900}"
T="$HOME/.claude/overseer/tools"; W=engineperf
OUT="$P/out/$TAG"; mkdir -p "$OUT"
[ -s "$RUN/bin/$EXE" ] || { echo "no exe $RUN/bin/$EXE"; exit 2; }
[ "$(stat -c %s "$RUN/bin/$EXE")" -gt 1000000 ] || { echo "exe is a stub (failed build?)"; exit 2; }
if "$T/protected-title-check.sh" >/dev/null 2>&1; then echo "REFUSED: protected title running"; exit 3; fi
# BENCH_EXCLUSIVE=1: timing run. Needs the whole box (Overseer rule 2026-11-18): EXCLUSIVE claim, load
# logged at start and end, result marked CONFOUNDED if another heavy process was present.
# Without it: a functional run (smoke test, state-diff harness); its timings are NOT measurements.
X="${BENCH_EXCLUSIVE:-0}"; KIND="functional"; [ "$X" = 1 ] && KIND="EXCLUSIVE timing"
if ! "$T/quiet-required.sh" claim $W $((LIMIT/60+5)) "headless Xvfb GAMMA engine benchmark $TAG ($KIND)" >"$OUT/claim.txt" 2>&1; then
  echo "REFUSED: no quiet slot ($(tail -1 "$OUT/claim.txt"))"; exit 3; fi
others() { ps -eo pcpu,pid,comm --sort=-pcpu | awk 'NR>1 && $1>=100 && $3!~/PerfDX11|wineserver|Xvfb|[Aa]nomaly|Stock/ {print $1"%:"$3}' | head -5 | tr '\n' ' '; }
echo "start $(date '+%F %T') kind=$KIND loadavg=$(cut -d' ' -f1-3 /proc/loadavg) heavy_others=[$(others)]" > "$OUT/conditions.txt"
export WINEPREFIX="$RUN/prefix" DISPLAY="$DISP"
XVFB_PID=""; WM_PID=""
cleanup() {
  wineserver -k >/dev/null 2>&1
  [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  "$T/quiet-required.sh" release $W >/dev/null 2>&1
}
trap cleanup EXIT
if [ ! -e "/tmp/.X11-unix/X${DISP#:}" ]; then Xvfb "$DISP" -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & XVFB_PID=$!; sleep 2; fi
if ! wmctrl -m >/dev/null 2>&1; then openbox >/dev/null 2>&1 & WM_PID=$!
  for _ in $(seq 1 30); do wmctrl -m >/dev/null 2>&1 && break; sleep 0.5; done; fi
LOG="$RUN/appdata/logs/xray_$(whoami).log"
rm -f "$LOG" "$RUN"/appdata/logs/perf_*_"$TAG".csv
DESK="eperf$$"
( cd "$RUN" && ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy WINEDLLOVERRIDES="d3dcompiler_47=n" WINEDEBUG=-all \
  DXVK_LOG_LEVEL=info DXVK_LOG_PATH="$OUT" nice -n 10 wine explorer "/desktop=$DESK,1280x720" \
  "Z:${RUN//\//\\}\\bin\\$EXE" -nointro -dbg -start "server($SAVE/single/alife/load)" "client(localhost)" \
  -perf_capture "$WARM,$FRAMES,$TAG" -perf_fixed_dt -perf_quit "$@" > "$OUT/wine_stderr.log" 2>&1 ) &
START=$(date +%s); RC=1
while :; do
  sleep 5
  if "$T/protected-title-check.sh" >/dev/null 2>&1; then echo "ABORT: protected title started"; RC=3; break; fi
  if [ -s "$RUN/appdata/logs/perf_sites_$TAG.csv" ] && grep -qa "xrprof\] capture '$TAG' finished" "$LOG" 2>/dev/null; then RC=0; sleep 8; break; fi
  if ! pgrep -f "bin\\\\$EXE" >/dev/null 2>&1 && [ $(( $(date +%s) - START )) -gt 30 ]; then echo "engine exited without a capture"; RC=4; break; fi
  [ $(( $(date +%s) - START )) -gt "$LIMIT" ] && { echo "TIMEOUT after ${LIMIT}s"; RC=5; break; }
  # nudge past any "press any key" screen (harmless once in game)
  if ! grep -qa "xrprof\] capture '$TAG' started" "$LOG" 2>/dev/null; then
    w=$(xdotool search --name "$DESK" 2>/dev/null | head -1); [ -n "$w" ] && xdotool windowactivate --sync "$w" key space 2>/dev/null
  fi
done
HO="$(others)"; echo "end   $(date '+%F %T') loadavg=$(cut -d' ' -f1-3 /proc/loadavg) heavy_others=[$HO]" >> "$OUT/conditions.txt"
if [ "$X" != 1 ]; then echo "VERDICT: functional run, timings are not measurements" >> "$OUT/conditions.txt"
elif [ -n "$HO" ] || grep -q "^start.*heavy_others=\[[^]]" "$OUT/conditions.txt"; then echo "VERDICT: CONFOUNDED (other heavy process present)" >> "$OUT/conditions.txt"
else echo "VERDICT: clean" >> "$OUT/conditions.txt"; fi
cat "$OUT/conditions.txt"
cp "$LOG" "$OUT/xray.log" 2>/dev/null
mv "$RUN"/appdata/logs/perf_*_"$TAG".csv "$OUT/" 2>/dev/null
echo "rc=$RC elapsed=$(( $(date +%s) - START ))s out=$OUT"; grep -a "xrprof" "$OUT/xray.log" 2>/dev/null | tail -3
exit $RC
