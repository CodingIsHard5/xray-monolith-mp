#!/usr/bin/env bash
# Tier (b) run (PREREG-tierB): ONE normal-mode run (no -det_mode, mt_* as shipped), headless, on the project's own run
# dir. Boots <save>, the overlay dumps A-Life state at the start point (k=0) and at 300, 600, 900 s of engine time, quits.
# Usage: TB_EXE=<exe in run/bin> tools/tb_run.sh <tag> <save> [extra engine flags, e.g. -tb_skip 100 -tb_walk 60]
# Output: out/tb_<tag>/ alife_dump_<tag>_<k>.txt tb_extra_<tag>_<k>.txt xray.log conditions.txt
# Env: TB_EXE (required), TB_TIMES (default 300,600,900), BENCH_DISPLAY, TB_TIMEOUT (s, default 1800),
#      TB_NOCLAIM=1 (caller holds the claim for a series, as DET_NOCLAIM), BENCH_LOWEND/BENCH_CORES/BENCH_MEM (as bench.sh).
# The claim is EXCLUSIVE (prereg section 8) and held for the whole engine lifetime, released on exit (trap).
# A run whose 1-minute load at start is >= 2 is marked DISTURBED in conditions.txt (prereg section 8); it still runs.
set -u
P="$(cd "$(dirname "$0")/.." && pwd)"; RUN="$P/run"
TAG="${1:?tag}"; TAG="${TAG,,}"; SAVE="${2:?save}"; shift 2
EXE="${TB_EXE:?set TB_EXE to the exe in run/bin}"; TIMES="${TB_TIMES:-300,600,900}"
DISP="${BENCH_DISPLAY:-:77}"; LIMIT="${TB_TIMEOUT:-1800}"
T="$HOME/.claude/overseer/tools"; W=engineperf
OUT="$P/out/tb_$TAG"; mkdir -p "$OUT"
NLAST=$(echo "$TIMES" | tr ',' '\n' | grep -c .)
[ "$(stat -c %s "$RUN/bin/$EXE" 2>/dev/null || echo 0)" -gt 1000000 ] || { echo "no usable exe $EXE"; exit 2; }
[ -s "$RUN/appdata/savedgames/$SAVE.scop" ] || { echo "no save $SAVE in $RUN/appdata/savedgames"; exit 2; }
cmp -s "$RUN/gamedata/scripts/zz_eperf_dump.script" "$P/harness/zz_eperf_dump.script" || { echo "REFUSED: run tree overlay differs from harness/zz_eperf_dump.script (install it first)"; exit 2; }
if "$T/protected-title-check.sh" >/dev/null 2>&1; then echo "REFUSED: protected title running"; exit 3; fi
if [ "${TB_NOCLAIM:-0}" != 1 ] && ! "$T/quiet-required.sh" claim $W $((LIMIT/60+5)) "headless Xvfb GAMMA tier (b) normal-mode run $TAG EXCLUSIVE" >"$OUT/claim.txt" 2>&1; then
  echo "REFUSED: no quiet slot ($(tail -1 "$OUT/claim.txt"))"; exit 3; fi
# own engine excluded by its command line (this project's run dir), as in bench.sh
others() { ps -eo pcpu,pid,comm,args --sort=-pcpu | awk -v own="$RUN" -v ownw="$(echo "${RUN,,}" | sed 's|/|\\\\|g')" 'NR>1 && $1>=100 && index(tolower($0), tolower(own))==0 && index(tolower($0), ownw)==0 && $3!~/wineserver|Xvfb/ {print $1"%:"$3}' | head -5 | tr '\n' ' '; }
L1=$(cut -d' ' -f1 /proc/loadavg)
echo "start $(date '+%F %T') exe=$EXE save=$SAVE times=$TIMES extra=[$*] loadavg=$(cut -d' ' -f1-3 /proc/loadavg) heavy_others=[$(others)]" > "$OUT/conditions.txt"
awk -v l="$L1" 'BEGIN{exit !(l>=2)}' && echo "DISTURBED: 1-minute load at start $L1 >= 2 (prereg section 8: exclude and re-run once)" >> "$OUT/conditions.txt"
export WINEPREFIX="$RUN/prefix" DISPLAY="$DISP"
XVFB_PID=""; WM_PID=""
cleanup() { wineserver -k >/dev/null 2>&1; sleep 1; cp "$P/out/user.ltx.orig" "$RUN/appdata/user.ltx"; [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null; [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  [ "${TB_NOCLAIM:-0}" != 1 ] && "$T/quiet-required.sh" release $W >/dev/null 2>&1; }
trap cleanup EXIT
if [ ! -e "/tmp/.X11-unix/X${DISP#:}" ]; then Xvfb "$DISP" -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & XVFB_PID=$!; sleep 2; fi
if ! wmctrl -m >/dev/null 2>&1; then openbox >/dev/null 2>&1 & WM_PID=$!
  for _ in $(seq 1 30); do wmctrl -m >/dev/null 2>&1 && break; sleep 0.5; done; fi
# Normal settings: the shipped user.ltx (mt_* on), restored from the pristine copy before the run (the engine rewrites it
# on exit) and again in cleanup, as det_run.sh does with its serial copy.
cp "$P/out/user.ltx.orig" "$RUN/appdata/user.ltx"
LOG="$RUN/appdata/logs/xray_$(whoami).log"; rm -f "$LOG" "$RUN"/appdata/logs/alife_dump_"$TAG"_*.txt "$RUN"/appdata/logs/tb_extra_"$TAG"_*.txt
# BENCH_LOWEND=1: same constrained-run prefix as bench.sh (C2 speed control: pinned vs unpinned)
LOWPFX=""; if [ "${BENCH_LOWEND:-0}" = 1 ]; then
  LOWPFX="taskset -c ${BENCH_CORES:-0-3} systemd-run --user --scope -q -p MemoryMax=${BENCH_MEM:-8G} -p MemorySwapMax=0"
  printf 'dxgi.maxDeviceMemory = %s\n' "${BENCH_VRAM_MB:-2048}" > "$OUT/dxvk_lowend.conf"; export DXVK_CONFIG_FILE="$OUT/dxvk_lowend.conf"
  echo "lowend: cores=${BENCH_CORES:-0-3} mem=${BENCH_MEM:-8G} vram_reported_mb=${BENCH_VRAM_MB:-2048}" >> "$OUT/conditions.txt"
fi
( cd "$RUN" && ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy WINEDLLOVERRIDES="d3dcompiler_47=n" WINEDEBUG=-all \
  $LOWPFX nice -n 19 ionice -c3 wine explorer "/desktop=etb$$,1280x720" "Z:${RUN//\//\\}\\bin\\$EXE" -nointro -dbg \
  -start "server($SAVE/single/alife/load)" "client(localhost)" -tb_dump -det_tag "$TAG" -det_times "$TIMES" "$@" \
  > "$OUT/wine_stderr.log" 2>&1 ) &
START=$(date +%s); RC=1; PEAK=0; AFF=""
DONE="$RUN/appdata/logs/alife_dump_${TAG}_$NLAST.txt"
while :; do
  sleep 5
  for EP in $(pgrep -f "bin\\\\$EXE"); do [ -z "$AFF" ] && AFF=$(awk '/Cpus_allowed_list/{print $2}' /proc/$EP/status 2>/dev/null) && [ -n "$AFF" ] && echo "engine pid $EP cpus_allowed=$AFF" >> "$OUT/conditions.txt"; R=$(awk '/VmRSS/{print $2}' /proc/$EP/status 2>/dev/null); [ "${R:-0}" -gt "$PEAK" ] && PEAK=$R; done
  if "$T/protected-title-check.sh" >/dev/null 2>&1; then echo "ABORT: protected title started"; RC=3; break; fi
  if [ -s "$DONE" ] && grep -qa "^#actor" "$DONE" && grep -qa "^#end" "$RUN/appdata/logs/tb_extra_${TAG}_$NLAST.txt" 2>/dev/null; then RC=0; sleep 8; break; fi
  if ! pgrep -f "bin\\\\$EXE" >/dev/null 2>&1 && [ $(( $(date +%s) - START )) -gt 30 ]; then echo "engine exited early"; RC=4; break; fi
  [ $(( $(date +%s) - START )) -gt "$LIMIT" ] && { echo "TIMEOUT after ${LIMIT}s"; RC=5; break; }
done
echo "engine peak resident memory (sampled every 5 s): $((PEAK/1024)) MiB" >> "$OUT/conditions.txt"
echo "end   $(date '+%F %T') loadavg=$(cut -d' ' -f1-3 /proc/loadavg) heavy_others=[$(others)] rc=$RC" >> "$OUT/conditions.txt"
cat "$OUT/conditions.txt"
cp "$LOG" "$OUT/xray.log" 2>/dev/null
mv "$RUN"/appdata/logs/alife_dump_"$TAG"_*.txt "$RUN"/appdata/logs/tb_extra_"$TAG"_*.txt "$OUT/" 2>/dev/null
ls "$RUN"/appdata/logs/*.mdmp >/dev/null 2>&1 && { echo "CRASH DUMP present"; mv "$RUN"/appdata/logs/*.mdmp "$OUT/"; }
echo "rc=$RC elapsed=$(( $(date +%s) - START ))s out=$OUT"; grep -a "eperf dump\|tb_skip\|tb_dump" "$OUT/xray.log" 2>/dev/null | tail -6
exit $RC
