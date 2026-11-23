#!/usr/bin/env bash
# Equivalence-harness run: boot the bench save with -det_mode, dump A-Life state at checkpoints, quit.
# Functional run (not a timing measurement). Usage: tools/det_run.sh <exe> <tag> [interval_s] [checkpoints]
# Output: out/det_<tag>/alife_dump_<tag>_<k>.txt
set -u
P="$(cd "$(dirname "$0")/.." && pwd)"; RUN="$P/run"
EXE="${1:?exe}"; TAG="${2:?tag}"; TAG="${TAG,,}"; INT="${3:-60}"; N="${4:-3}"
SAVE="${BENCH_SAVE:-bench1}"; DISP="${BENCH_DISPLAY:-:77}"; LIMIT="${BENCH_TIMEOUT:-1500}"
T="$HOME/.claude/overseer/tools"; W=engineperf
OUT="$P/out/det_$TAG"; mkdir -p "$OUT"
[ "$(stat -c %s "$RUN/bin/$EXE" 2>/dev/null || echo 0)" -gt 1000000 ] || { echo "no usable exe $EXE"; exit 2; }
if "$T/protected-title-check.sh" >/dev/null 2>&1; then echo "REFUSED: protected title running"; exit 3; fi
"$P/tools/run_noguard.sh" "$RUN" || exit 7   # no path from run/ may write into ~/Games (Overseer 2026-11-22)
# DET_NOCLAIM=1: the caller (tools/det_pair.sh) already holds the claim for a whole series of runs
if [ "${DET_NOCLAIM:-0}" != 1 ] && ! "$T/quiet-required.sh" claim $W $((LIMIT/60+5)) "headless Xvfb GAMMA A-Life equivalence run $TAG" >"$OUT/claim.txt" 2>&1; then
  echo "REFUSED: no quiet slot ($(tail -1 "$OUT/claim.txt"))"; exit 3; fi
export WINEPREFIX="$RUN/prefix" DISPLAY="$DISP"
XVFB_PID=""; WM_PID=""
cleanup() { wineserver -k >/dev/null 2>&1; sleep 1; cp "$P/out/user.ltx.orig" "$RUN/appdata/user.ltx"; [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null; [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  [ "${DET_NOCLAIM:-0}" != 1 ] && "$T/quiet-required.sh" release $W >/dev/null 2>&1; }
trap cleanup EXIT
if [ ! -e "/tmp/.X11-unix/X${DISP#:}" ]; then Xvfb "$DISP" -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & XVFB_PID=$!; sleep 2; fi
if ! wmctrl -m >/dev/null 2>&1; then openbox >/dev/null 2>&1 & WM_PID=$!
  for _ in $(seq 1 30); do wmctrl -m >/dev/null 2>&1 && break; sleep 0.5; done; fi
# Serial test settings (Overseer ruling 2026-11-19): all mt_* off; restored from the pristine copy before every run
# because the engine rewrites its settings file on exit.
cp "$P/out/user_det.ltx.orig" "$RUN/appdata/user.ltx"   # engine's -ltx option did not load the file; swap my run copy instead
LOG="$RUN/appdata/logs/xray_$(whoami).log"; rm -f "$LOG" "$RUN"/appdata/logs/alife_dump_"$TAG"_*.txt
( cd "$RUN" && ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy WINEDLLOVERRIDES="d3dcompiler_47=n" WINEDEBUG=-all \
  nice -n 10 wine explorer "/desktop=edet$$,1280x720" "Z:${RUN//\//\\}\\bin\\$EXE" -nointro -dbg \
  -start "server($SAVE/single/alife/load)" "client(localhost)" -det_mode -det_tag "$TAG" -det_interval "$INT" -det_checkpoints "$N" ${DET_EXTRA:-} \
  > "$OUT/wine_stderr.log" 2>&1 ) &
START=$(date +%s); RC=1
while :; do
  sleep 5
  if "$T/protected-title-check.sh" >/dev/null 2>&1; then echo "ABORT: protected title started"; RC=3; break; fi
  if [ -s "$RUN/appdata/logs/alife_dump_${TAG}_$N.txt" ] && grep -qa "^#actor" "$RUN/appdata/logs/alife_dump_${TAG}_$N.txt"; then RC=0; sleep 8
    case "${DET_EXTRA:-}" in *-det_save*) for _ in $(seq 1 30); do [ -s "$RUN/appdata/savedgames/det_${TAG}_$N.scop" ] && break; sleep 3; done; sleep 5;; esac
    break; fi
  if ! pgrep -f "bin\\\\$EXE" >/dev/null 2>&1 && [ $(( $(date +%s) - START )) -gt 30 ]; then echo "engine exited early"; RC=4; break; fi
  [ $(( $(date +%s) - START )) -gt "$LIMIT" ] && { echo "TIMEOUT after ${LIMIT}s"; RC=5; break; }
done
cp "$LOG" "$OUT/xray.log" 2>/dev/null; mv "$RUN"/appdata/logs/alife_dump_"$TAG"_*.txt "$OUT/" 2>/dev/null; mv "$RUN"/appdata/logs/det_trace_"$TAG".txt "$RUN"/appdata/logs/det_trace_"$TAG".lua.txt "$RUN"/appdata/logs/det_trace_"$TAG".txt.sched "$OUT/" 2>/dev/null
mv "$RUN"/appdata/savedgames/det_"$TAG"_* "$OUT/" 2>/dev/null; mv "$RUN"/appdata/logs/det_streams_"$TAG"_*.txt "$OUT/" 2>/dev/null; mv "$RUN"/appdata/logs/det_counters_"$TAG"_*.txt "$RUN"/appdata/logs/det_alife_counts_"$TAG"_*.txt "$OUT/" 2>/dev/null
ls "$RUN"/appdata/logs/*.mdmp >/dev/null 2>&1 && { echo "CRASH DUMP present"; mv "$RUN"/appdata/logs/*.mdmp "$OUT/"; }
echo "rc=$RC elapsed=$(( $(date +%s) - START ))s out=$OUT"; grep -a "eperf dump" "$OUT/xray.log" 2>/dev/null | tail -4
exit $RC
