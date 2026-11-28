#!/usr/bin/env bash
# Equivalence-harness run: boot the bench save with -det_mode, dump A-Life state at checkpoints, quit.
# Functional run (not a timing measurement). Usage:
#   tools/det_run.sh <exe> <tag> [interval_s] [checkpoints]
#   tools/det_run.sh --preflight-only <exe> <tag>     the box checks only (source + checks + exit; starts nothing,
#                                                      writes nothing): exit 0 clear, 1 refused, 2 refused (cannot
#                                                      tell / bad caller settings), 3 the shared file is bad
# Output: out/det_<tag>/alife_dump_<tag>_<k>.txt
# 2026-11-27 (supervisor's order): the box checks come from wt-render-split/perf-harness/common/box_preflight.sh
# (pinned below; refused if missing, unreadable, not the pinned file, or without its marker, version or functions).
# They run fail-closed BEFORE anything is created, deleted or moved: the out folder, the claim, Xvfb, the user.ltx
# swap, the `rm -f` of the xray log and old dumps, and the crash-dump move all come after the preflight passed.
# Caller settings (environment):
#   DET_NOCLAIM=1      the caller holds the claim; then DET_BOX_TOKEN is REQUIRED: a text that is in the caller's
#                      claim <what>, so own_claim does not refuse the caller's own claim but refuses any other claim
#                      under the worker name. Without DET_NOCLAIM this script claims itself (token in its <what>)
#                      and releases only that claim, by its claim-id.
#   DET_BOX_CLASS      shared (default) | exclusive: the caller's claim class (exclusive skips the EXCLUSIVE test).
#   DET_BOX_WORKER     the worker name of the claim (default engineperf).
#   BENCH_DISPLAY      default :77; :0 (Caden's screen) refused.
# Checks at this level: play lock, protected title, Sober, engine_running (an engine of the run tree already running
# BEFORE this call starts one: refused), own claim (with the token), EXCLUSIVE (class shared), display. While the
# engine runs, every 5 s: play lock, title, Sober (fail-closed: a check that cannot tell aborts, rc 3). engine_running
# is not re-checked after the start (this call's own engine would match). On an abort for the play lock, no crash
# dump is moved out of run/appdata/logs (it may be Caden's).
# Unsupported since this change: callers that set DET_NOCLAIM=1 without DET_BOX_TOKEN (tools/det_pair.sh and the
# closed part-A drivers that use it): they are refused by name (common/README.md).
set -u
. "$(cd "$(dirname "$0")" && pwd)/lib_engine_pids.sh"   # engine lookup by Wine binary, never a bare name match
P="$(cd "$(dirname "$0")/.." && pwd)"; RUN="$P/run"
PRE=0; [ "${1:-}" = --preflight-only ] && { PRE=1; shift; }
EXE="${1:?exe}"; TAG="${2:?tag}"; TAG="${TAG,,}"; INT="${3:-60}"; N="${4:-3}"
SAVE="${BENCH_SAVE:-bench1}"; DISP="${BENCH_DISPLAY:-:77}"; LIMIT="${BENCH_TIMEOUT:-1500}"
T="$HOME/.claude/overseer/tools"; W="${DET_BOX_WORKER:-engineperf}"; CLS="${DET_BOX_CLASS:-shared}"
# ---- the shared box checks (DET_BOX_LIB / DET_BOX_LIB_SHA and BOX_* inputs: --preflight-only only, for fixtures) ----
BOXLIB_SHA=420c5f2af6272e341559b10a4c9cad7f99e5276a90afe470fcd5e4e0f916e8d1   # common/box_preflight.sh v1
LIBRC=2; [ "$PRE" = 1 ] && LIBRC=3
libfail() { echo "REFUSED: box_preflight: $*"; exit $LIBRC; }
if [ "$PRE" != 1 ] && [ -n "${DET_BOX_LIB+x}${DET_BOX_LIB_SHA+x}" ]; then libfail "DET_BOX_LIB / DET_BOX_LIB_SHA are set: only --preflight-only may override the shared file"; fi
BOXLIB="${DET_BOX_LIB:-$P/wt-render-split/perf-harness/common/box_preflight.sh}"; BOXSHA="${DET_BOX_LIB_SHA:-$BOXLIB_SHA}"
{ [ -e "$BOXLIB" ] || [ -L "$BOXLIB" ]; } || libfail "missing file $BOXLIB"
{ [ -f "$BOXLIB" ] && [ -r "$BOXLIB" ]; } || libfail "$BOXLIB is not a readable file"
[ "$(sha256sum < "$BOXLIB" 2>/dev/null | cut -d' ' -f1)" = "$BOXSHA" ] || libfail "$BOXLIB is not the pinned file (${BOXSHA:0:16})"
unset BOX_PREFLIGHT_MARKER BOX_PREFLIGHT_VERSION
. "$BOXLIB" || libfail "sourcing $BOXLIB failed"
[ "${BOX_PREFLIGHT_MARKER:-}" = engineperf-box-preflight-v1 ] || libfail "$BOXLIB did not set the marker engineperf-box-preflight-v1"
[ "${BOX_PREFLIGHT_VERSION:-}" = 1 ] || libfail "$BOXLIB is version '${BOX_PREFLIGHT_VERSION:-none}', not 1"
for _f in box_preflight_all box_ok_play_lock box_ok_title box_ok_roblox box_claim_has_token; do
  declare -F "$_f" >/dev/null || libfail "$BOXLIB does not define $_f"; done
[ "$PRE" != 1 ] && [ -n "$BOX_OVERRIDES" ] && { echo "REFUSED: box_preflight inputs overridden ($BOX_OVERRIDES): only --preflight-only may run with fixtures"; exit 2; }
case "$CLS" in shared|exclusive) ;; *) echo "REFUSED: DET_BOX_CLASS '$CLS' is not shared or exclusive"; exit 2 ;; esac
if [ "${DET_NOCLAIM:-0}" = 1 ]; then
  TOKEN="${DET_BOX_TOKEN:-}"
  [ -n "$TOKEN" ] || { echo "REFUSED: DET_NOCLAIM=1 needs DET_BOX_TOKEN (a text in the caller's claim), or own_claim cannot tell the caller's claim from another job's"; exit 2; }
  box_claim_has_token "$W" "$TOKEN"; _r=$?
  [ $_r = 0 ] || { echo "REFUSED: DET_NOCLAIM=1 but no live claim under '$W' carries '$TOKEN' ($BOX_WHY)"; exit 2; }
else
  TOKEN="det_run-$TAG-$(date +%Y%m%d%H%M%S)-$$"   # in this script's own claim <what>
fi
_o=$(box_preflight_all "$CLS" "$DISP" "$W" "$([ "${DET_NOCLAIM:-0}" = 1 ] && echo "$TOKEN")"); _r=$?
if [ $_r != 0 ]; then echo "$_o" | grep -v '^BOX OVERRIDES' | sed 's/^/REFUSED: /'; exit $([ "$PRE" = 1 ] && echo $_r || echo 3); fi
[ "$PRE" = 1 ] && { [ -n "$BOX_OVERRIDES" ] && echo "BOX OVERRIDES: $BOX_OVERRIDES"; echo "det_run preflight ok ($CLS, $DISP, $W)"; exit 0; }
# ---- the preflight passed: from here on files may be created, deleted and moved ----
OUT="$P/out/det_$TAG"
[ "$(stat -c %s "$RUN/bin/$EXE" 2>/dev/null || echo 0)" -gt 1000000 ] || { echo "no usable exe $EXE"; exit 2; }
"$P/tools/run_noguard.sh" "$RUN" || exit 7   # no path from run/ may write into ~/Games (Overseer 2026-11-22)
mkdir -p "$OUT"
# DET_NOCLAIM=1: the caller already holds the claim for a whole series of runs
CLAIMED=0
if [ "${DET_NOCLAIM:-0}" != 1 ]; then
  if ! "$T/quiet-required.sh" claim $W $((LIMIT/60+5)) "headless Xvfb GAMMA A-Life equivalence run $TAG $TOKEN" >"$OUT/claim.txt" 2>&1 </dev/null; then
    echo "REFUSED: no quiet slot ($(tail -1 "$OUT/claim.txt"))"; exit 3; fi
  CLAIMED=1
fi
release_own() {   # only the claim this script made: by its claim-id; by name only if it carries this run's token alone
  local id; [ "$CLAIMED" = 1 ] || return 0
  id=$(sed -n 's/^claim-id: \([A-Za-z0-9._-][A-Za-z0-9._-]*\)$/\1/p' "$OUT/claim.txt" 2>/dev/null | tail -1)
  if [ -n "$id" ]; then "$T/quiet-required.sh" release $W "$id" >/dev/null 2>&1 </dev/null
  elif box_claim_has_token "$W" "$TOKEN" && box_ok_claims "$W" "$TOKEN"; then "$T/quiet-required.sh" release $W >/dev/null 2>&1 </dev/null
  else echo "claim not released (no claim-id, cannot tell it is the only one): it expires by itself"; fi; }
export WINEPREFIX="$RUN/prefix" DISPLAY="$DISP"
XVFB_PID=""; WM_PID=""
cleanup() { wineserver -k >/dev/null 2>&1; sleep 1; cp "$P/out/user.ltx.orig" "$RUN/appdata/user.ltx"; [ -n "$WM_PID" ] && kill "$WM_PID" 2>/dev/null; [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  release_own; }
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
  > "$OUT/wine_stderr.log" 2>&1 ) </dev/null &
START=$(date +%s); RC=1; LOCKED=0
while :; do
  sleep 5
  box_ok_play_lock || { echo "ABORT: play lock ($BOX_WHY)"; RC=3; LOCKED=1; break; }
  box_ok_title || { echo "ABORT: protected title ($BOX_WHY)"; RC=3; break; }
  box_ok_roblox || { echo "ABORT: Roblox ($BOX_WHY)"; RC=3; break; }
  if [ -s "$RUN/appdata/logs/alife_dump_${TAG}_$N.txt" ] && grep -qa "^#actor" "$RUN/appdata/logs/alife_dump_${TAG}_$N.txt"; then RC=0; sleep 8
    case "${DET_EXTRA:-}" in *-det_save*) for _ in $(seq 1 30); do [ -s "$RUN/appdata/savedgames/det_${TAG}_$N.scop" ] && break; sleep 3; done; sleep 5;; esac
    break; fi
  if [ -z "$(engine_main "$EXE")" ] && [ $(( $(date +%s) - START )) -gt 30 ]; then echo "engine exited early"; RC=4; break; fi
  [ $(( $(date +%s) - START )) -gt "$LIMIT" ] && { echo "TIMEOUT after ${LIMIT}s"; RC=5; break; }
done
cp "$LOG" "$OUT/xray.log" 2>/dev/null; mv "$RUN"/appdata/logs/alife_dump_"$TAG"_*.txt "$OUT/" 2>/dev/null; mv "$RUN"/appdata/logs/det_trace_"$TAG".txt "$RUN"/appdata/logs/det_trace_"$TAG".lua.txt "$RUN"/appdata/logs/det_trace_"$TAG".txt.sched "$OUT/" 2>/dev/null
mv "$RUN"/appdata/savedgames/det_"$TAG"_* "$OUT/" 2>/dev/null; mv "$RUN"/appdata/logs/det_streams_"$TAG"_*.txt "$OUT/" 2>/dev/null; mv "$RUN"/appdata/logs/det_counters_"$TAG"_*.txt "$RUN"/appdata/logs/det_alife_counts_"$TAG"_*.txt "$OUT/" 2>/dev/null
if ls "$RUN"/appdata/logs/*.mdmp >/dev/null 2>&1; then
  if [ "$LOCKED" = 1 ] || ! box_ok_play_lock; then echo "CRASH DUMP present (NOT moved: the play lock is live, it may be Caden's)"
  else echo "CRASH DUMP present"; mv "$RUN"/appdata/logs/*.mdmp "$OUT/"; fi
fi
echo "rc=$RC elapsed=$(( $(date +%s) - START ))s out=$OUT"; grep -a "eperf dump" "$OUT/xray.log" 2>/dev/null | tail -4
exit $RC
