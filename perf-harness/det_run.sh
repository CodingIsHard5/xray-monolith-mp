#!/usr/bin/env bash
# Equivalence-harness run: boot the bench save with -det_mode, dump A-Life state at checkpoints, quit.
# Functional run (not a timing measurement). Usage:
#   BENCH_DISPLAY=:141 tools/det_run.sh <exe> <tag> [interval_s] [checkpoints]
#   BENCH_DISPLAY=:141 tools/det_run.sh --preflight-only <exe> <tag>
#                      the box checks only (source + checks + exit; starts nothing, writes nothing, calls no claim-tool
#                      verb): exit 0 clear, 1 refused, 2 refused (cannot tell / bad caller settings), 3 the shared file is bad
# Output: out/det_<tag>/alife_dump_<tag>_<k>.txt
# 2026-11-27 (supervisor's order): the box checks come from wt-render-split/perf-harness/common/box_preflight.sh
# (pinned below; refused if missing, unreadable, not the pinned file, or without its marker, version or functions).
# 2026-11-28 (plan-facts/box-preflight-second-review-2026-11-27.md): v2 of the shared file, and
#   B-1  the box is checked again (box_fresh: title, Sober, engine of the run tree, play lock LAST) right after the
#        claim, and again inside box_guarded IMMEDIATELY before every step that writes under run/: the user.ltx swap,
#        the log clean-up, the engine start, the collection of this tag's files, the user.ltx restore.
#   B-2  once any check refuses (before or during the boot), or a signal stops this script, NOTHING under run/ is
#        written, moved or deleted any more: only `wineserver -k` in run/prefix (box_wine_kill: never empty, never
#        prefix_play), our own Xvfb / openbox (box_kill_own: parent and name checked), and a list of the files left.
#   S-4  BENCH_DISPLAY must be :141 (no default); the display must be free before Xvfb starts.
#   N-2  the traps are set before the claim; N-3 tag, exe, save and numbers are validated before any path is built;
#   N-6  the waits after the last checkpoint keep checking; N-7 only crash dumps newer than this boot's start marker
#        are moved; N-9 the pid library must load; N-15 TERM / HUP / INT are trapped explicitly.
# 2026-11-28 (plan-facts/box-preflight-third-review-2026-11-28.md): S3-1 `wineserver -k` repeated every ~5 s in the
#   60-s stop wait; S3-2 the project root is the physical path and must be the one the engine checks look at; S3-3 a
#   DET_BOX_TOKEN must look like <name>-<stamp>-<pid>; S3-4 DET_BOX_CLASS=exclusive only with DET_NOCLAIM=1; N3-5 dump
#   names read line by line; N3-6 caller variables that change a run refused, DET_EXTRA split without globbing; N3-7 no
#   leading zeros; N3-8 the X server on :141 must be our own Xvfb; N3-9 `wmctrl` bounded; N3-10 user.ltx is overwritten
#   only while it is one of the two known files.
# THE WINDOW THAT IS LEFT (plain words, review S3-6): a check is a moment, not a lock. Between box_fresh reading "no
# play lock" and the write on the next line there are microseconds to milliseconds. The larger window is SECONDS long
# and sits on the launcher's side: play/caden_simtick_play.sh tests for an engine (:21) and a lock (:22), then runs
# run_noguard.sh (a `find` over the whole run tree) and its path guards (:28-33), and writes its lock only at :34. A
# box_fresh that falls in that stretch reads "no lock, no engine" while his start is under way. It needs his claim test
# (:20) to pass just before this run's claim is visible (in the s4d2 case the claim is always visible first) AND his
# :21-:34 to take longer than this script's claim -> engine-start stretch (at least ~8 s). The title answer can also be
# up to 2 s old (the checker's own result cache) plus its 2-s sample; Sober and the engine are read a second or two
# before the lock. Nothing in THIS script can close that window; the launcher writing its lock before its :21 test
# would close it from his side (the launcher is not ours to change; raised with its owner).
# Caller settings (environment):
#   DET_NOCLAIM=1      the caller holds the claim; then DET_BOX_TOKEN is REQUIRED: a text that is in the caller's
#                      claim <what>, so own_claim does not refuse the caller's own claim but refuses any other claim
#                      under the worker name. It must be <name>-<stamp>-<pid> ([A-Za-z0-9_-]{3,}, >= 9 digits,
#                      digits) and >= 20 characters (S3-3: a generic word like "headless" would match other jobs'
#                      claims). Without DET_NOCLAIM this script claims itself (token in its <what>) and releases
#                      only that claim, by its claim-id.
#   DET_BOX_CLASS      shared (default) | exclusive: the caller's claim class (exclusive skips the EXCLUSIVE test);
#                      exclusive is refused without DET_NOCLAIM=1 (S3-4: this script's own claim is always SHARED).
#   DET_BOX_WORKER     the worker name of the claim (default engineperf).
#   BENCH_DISPLAY      REQUIRED, must be :141.   BENCH_SAVE (default bench1), BENCH_TIMEOUT (default 1500 s), DET_EXTRA
#                      (one line; split on blanks with no globbing). Numbers: digits, no leading zero.
#   Refused (N3-6, in both modes): NOGUARD_GAMES, WINESERVER, WINELOADER, ENGINE_MIN_RSS_KB, BASH_ENV set at all.
# Environment the checks trust: see box_preflight.sh "ENVIRONMENT THE CHECKS TRUST". BOX_* inputs and DET_BOX_LIB*
# are refused in a real run; PTC_* refused; the claim tool's tuning variables are dropped for its calls (BOX_QR_UNSET).
# Unsupported: callers that set DET_NOCLAIM=1 without DET_BOX_TOKEN, or no BENCH_DISPLAY=:141 (tools/det_pair.sh and
# the closed part-A drivers): they are refused by name (common/README.md).
set -u
. "$(cd "$(dirname "$0")" && pwd)/lib_engine_pids.sh" || { echo "REFUSED: cannot load tools/lib_engine_pids.sh"; exit 2; }
declare -F engine_main >/dev/null || { echo "REFUSED: tools/lib_engine_pids.sh did not define engine_main"; exit 2; }
P="$(cd "$(dirname "$0")/.." && pwd -P)" || { echo "REFUSED: cannot find the project root"; exit 2; }   # S3-2: physical
RUN="$P/run"
PRE=0; [ "${1:-}" = --preflight-only ] && { PRE=1; shift; }
EXE="${1:-}"; TAG0="${2:-}"; TAG="${TAG0,,}"; INT="${3:-60}"; N="${4:-3}"
SAVE="${BENCH_SAVE:-bench1}"; DISP="${BENCH_DISPLAY:-}"; LIMIT="${BENCH_TIMEOUT:-1500}"
T="$HOME/.claude/overseer/tools"; W="${DET_BOX_WORKER:-engineperf}"; CLS="${DET_BOX_CLASS:-shared}"
# ---- N-3: every value that becomes part of a path or a command line, before any path is built ----
bad() { echo "REFUSED: $*"; exit 2; }
case "$TAG" in ''|*[!abcdefghijklmnopqrstuvwxyz0123456789_]*) bad "tag '$TAG0' is not [a-z0-9_]+" ;; esac
case "$EXE" in ''|.*|*[!A-Za-z0-9_.-]*) bad "exe '$EXE' is not a file name of [A-Za-z0-9_.-]" ;; *.exe|*.EXE) ;; *) bad "exe '$EXE' does not end in .exe" ;; esac
case "$SAVE" in ''|*[!A-Za-z0-9_]*) bad "BENCH_SAVE '$SAVE' is not [A-Za-z0-9_]+" ;; esac
case "$INT$N$LIMIT" in *[!0-9]*) bad "interval '$INT', checkpoints '$N' and BENCH_TIMEOUT '$LIMIT' must be numbers" ;; esac
for _v in "$INT" "$N" "$LIMIT"; do   # N3-7: N=03 would wait for a dump name the engine never writes; 0900 is octal to bash
  case "$_v" in 0?*) bad "interval '$INT', checkpoints '$N' and BENCH_TIMEOUT '$LIMIT' must have no leading zero" ;; esac; done
{ [ -n "$INT" ] && [ -n "$N" ] && [ -n "$LIMIT" ] && [ "$N" -ge 1 ] && [ "$N" -le 99 ]; } || bad "interval, checkpoints (1-99) and BENCH_TIMEOUT must be set"
for _v in NOGUARD_GAMES WINESERVER WINELOADER ENGINE_MIN_RSS_KB BASH_ENV; do   # N3-6: each changes what a run does
  [ -n "${!_v+x}" ] && bad "$_v is set in the caller's environment (it changes the ~/Games guard, the Wine server, the early-exit test or the shell start-up): unset it"; done
case "${DET_EXTRA:-}" in *$'\n'*|*$'\r'*) bad "DET_EXTRA holds a line break" ;; esac
DX=(); read -r -a DX <<< "${DET_EXTRA:-}"   # N3-6: split on blanks, NO pathname expansion (the engine starts in run/)
case "$W" in ''|*[!A-Za-z0-9._-]*) bad "DET_BOX_WORKER '$W' is not [A-Za-z0-9._-]+" ;; esac
[ "$DISP" = :141 ] || bad "BENCH_DISPLAY '$DISP' is not :141 (review S-4: no default display, :141 only)"
ME=$(id -un); case "$ME" in ''|*[!A-Za-z0-9_.-]*) bad "user name '$ME' is not [A-Za-z0-9_.-]+" ;; esac
# ---- the shared box checks (DET_BOX_LIB / DET_BOX_LIB_SHA and BOX_* inputs: --preflight-only only, for fixtures) ----
BOXLIB_SHA=f8026040f2f9b3f6cd5c7c33b11062f7efc1ed726af03651ce16bbb0fccd53ef   # common/box_preflight.sh v2, third-review revision (was f3ad9351...)
LIBRC=2; [ "$PRE" = 1 ] && LIBRC=3
libfail() { echo "REFUSED: box_preflight: $*"; exit $LIBRC; }
if [ "$PRE" != 1 ] && [ -n "${DET_BOX_LIB+x}${DET_BOX_LIB_SHA+x}" ]; then libfail "DET_BOX_LIB / DET_BOX_LIB_SHA are set: only --preflight-only may override the shared file"; fi
BOXLIB="${DET_BOX_LIB:-$P/wt-render-split/perf-harness/common/box_preflight.sh}"; BOXSHA="${DET_BOX_LIB_SHA:-$BOXLIB_SHA}"
{ [ -e "$BOXLIB" ] || [ -L "$BOXLIB" ]; } || libfail "missing file $BOXLIB"
{ [ -f "$BOXLIB" ] && [ -r "$BOXLIB" ]; } || libfail "$BOXLIB is not a readable file"
[ "$(sha256sum < "$BOXLIB" 2>/dev/null | cut -d' ' -f1)" = "$BOXSHA" ] || libfail "$BOXLIB is not the pinned file (${BOXSHA:0:16})"
unset BOX_PREFLIGHT_MARKER BOX_PREFLIGHT_VERSION
. "$BOXLIB" || libfail "sourcing $BOXLIB failed"
[ "${BOX_PREFLIGHT_MARKER:-}" = engineperf-box-preflight-v2 ] || libfail "$BOXLIB did not set the marker engineperf-box-preflight-v2"
[ "${BOX_PREFLIGHT_VERSION:-}" = 2 ] || libfail "$BOXLIB is version '${BOX_PREFLIGHT_VERSION:-none}', not 2"
for _f in box_preflight_all box_ok_play_lock box_ok_title box_ok_roblox box_ok_claims box_claim_has_token box_ok_display_free \
          box_fresh box_guarded box_rm_name box_dumps_newer box_wine_kill box_kill_own box_is_own box_engines_in_prefix; do
  declare -F "$_f" >/dev/null || libfail "$BOXLIB does not define $_f"; done
declare -p BOX_QR_UNSET >/dev/null 2>&1 || libfail "$BOXLIB does not define BOX_QR_UNSET"
[ "$PRE" != 1 ] && [ -n "$BOX_OVERRIDES" ] && { echo "REFUSED: box_preflight inputs overridden ($BOX_OVERRIDES): only --preflight-only may run with fixtures"; exit 2; }
# S3-2: the engine checks look for engines under BOX_RUN by its exact spelling, and s4d2_run.sh compares prefixes by the
# physical path; this run's tree must be exactly that path (P is physical). With BOX_RUN pointed at a fixture
# (--preflight-only only) the tree must still be the default one.
case " $BOX_OVERRIDES " in *" BOX_RUN "*) _want="$HOME/Projects/StalkerGameEngine/run" ;; *) _want=$BOX_RUN ;; esac
[ "$RUN" = "$_want" ] || { echo "REFUSED: the run tree $RUN (physical) is not $_want, the tree the engine checks look at"; exit 2; }
case "$CLS" in shared|exclusive) ;; *) echo "REFUSED: DET_BOX_CLASS '$CLS' is not shared or exclusive"; exit 2 ;; esac
[ "$CLS" = exclusive ] && [ "${DET_NOCLAIM:-0}" != 1 ] && { echo "REFUSED: DET_BOX_CLASS=exclusive without DET_NOCLAIM=1 (this script's own claim is SHARED, so the EXCLUSIVE test must run; S3-4)"; exit 2; }
if [ "${DET_NOCLAIM:-0}" = 1 ]; then
  TOKEN="${DET_BOX_TOKEN:-}"
  [ -n "$TOKEN" ] || { echo "REFUSED: DET_NOCLAIM=1 needs DET_BOX_TOKEN (a text in the caller's claim), or own_claim cannot tell the caller's claim from another job's"; exit 2; }
  _tre='^[A-Za-z0-9_-]{3,}-[0-9]{9,}-[0-9]+$'   # S3-3: <name>-<stamp>-<pid>, as s4d2_run.sh's s4d2-<mode>-<14 digits>-<pid>
  { [[ $TOKEN =~ $_tre ]] && [ "${#TOKEN}" -ge 20 ]; } || { echo "REFUSED: DET_BOX_TOKEN '${TOKEN:0:40}' is not <name>-<stamp of >= 9 digits>-<pid> of >= 20 characters (a generic text would match other jobs' claims; S3-3)"; exit 2; }
  box_claim_has_token "$W" "$TOKEN"; _r=$?
  [ $_r = 0 ] || { echo "REFUSED: DET_NOCLAIM=1 but no live claim under '$W' carries '$TOKEN' ($BOX_WHY)"; exit 2; }
else
  TOKEN="det_run-$TAG-$(date +%Y%m%d%H%M%S)-$$"   # in this script's own claim <what>
fi
_tok=""; [ "${DET_NOCLAIM:-0}" = 1 ] && _tok=$TOKEN
box_preflight_all "$CLS" "$DISP" "$W" "$_tok" >/dev/null; _r=$?; _o=$BOX_OUT   # in this shell: a title sighting is kept (S3-5)
if [ $_r != 0 ]; then echo "$_o" | grep -v '^BOX OVERRIDES' | sed 's/^/REFUSED: /'; exit $([ "$PRE" = 1 ] && echo $_r || echo 3); fi
box_ok_display_free "$DISP" || { echo "REFUSED: display_free: $BOX_WHY"; exit $([ "$PRE" = 1 ] && echo 1 || echo 3); }
[ "$PRE" = 1 ] && { [ -n "$BOX_OVERRIDES" ] && echo "BOX OVERRIDES: $BOX_OVERRIDES"; echo "det_run preflight ok ($CLS, $DISP, $W)"; exit 0; }
# ---- the preflight passed. From here on, EVERY write under run/ goes through box_guarded (B-1, B-2). ----
OUT="$P/out/det_$TAG"; LOGDIR="$RUN/appdata/logs"; SAVES="$RUN/appdata/savedgames"; PF="$RUN/prefix"
[ "$(stat -c %s "$RUN/bin/$EXE" 2>/dev/null || echo 0)" -gt 1000000 ] || { echo "no usable exe $EXE"; exit 2; }
"$P/tools/run_noguard.sh" "$RUN" || exit 7   # no path from run/ may write into ~/Games (Overseer 2026-11-22)
mkdir -p "$OUT" || { echo "REFUSED: cannot make $OUT"; exit 2; }
CLAIMED=0; XVFB_PID=""; WM_PID=""; ENGINE_STARTED=0; LTX_SWAPPED=0; WINE_LEFT=0; CLEANED=0; RC=1; START=$(date +%s)
mine() {   # <name>: this tag's own files, by exact name (a tag that is another tag's prefix never matches)
  local n=$1 p k
  for p in alife_dump det_streams det_counters det_alife_counts; do
    case "$n" in "${p}_${TAG}_"*.txt) k=${n#"${p}_${TAG}_"}; k=${k%.txt}; case "$k" in ''|*[!0-9]*) ;; *) return 0 ;; esac ;; esac
  done
  case "$n" in "det_trace_$TAG.txt"|"det_trace_$TAG.lua.txt"|"det_trace_$TAG.txt.sched") return 0 ;; esac
  for p in scop scoc dds; do
    case "$n" in "det_${TAG}_"*".$p") k=${n#"det_${TAG}_"}; k=${k%".$p"}; case "$k" in ''|*[!0-9]*) ;; *) return 0 ;; esac ;; esac
  done
  return 1; }
release_own() {   # only the claim this script made: by its claim-id; by name only if it carries this run's token alone
  local id; [ "$CLAIMED" = 1 ] || return 0
  id=$(sed -n 's/^claim-id: \([A-Za-z0-9._-][A-Za-z0-9._-]*\)$/\1/p' "$OUT/claim.txt" 2>/dev/null | tail -1)
  if [ -n "$id" ]; then env "${BOX_QR_UNSET[@]}" "$T/quiet-required.sh" release "$W" "$id" >/dev/null 2>&1 </dev/null; echo "released claim $id (rc $?)"; return 0; fi
  box_claim_has_token "$W" "$TOKEN"; case $? in
    0) if box_ok_claims "$W" "$TOKEN"; then env "${BOX_QR_UNSET[@]}" "$T/quiet-required.sh" release "$W" >/dev/null 2>&1 </dev/null; echo "released the claim carrying $TOKEN by name (rc $?)"
       else echo "claim NOT released (no claim-id, and another claim under $W is live: $BOX_WHY): it expires by itself"; fi ;;
    1) echo "no claim carries $TOKEN: nothing to release" ;;
    *) echo "claim NOT released (cannot tell which is ours: $BOX_WHY): it expires by itself" ;;
  esac; }
stop_wine() {   # our engine only: wineserver -k in run/prefix, then wait (from date, 60 s) until no engine of the run tree uses it
  # S3-1: an engine started just before a stop may have had no wineserver yet when the first -k ran, so -k is repeated
  # every ~5 s inside the wait (`nowait`: -k only, bounded 10 s, started only while >= 10 s of the 60 are left, so
  # the wait stays within 60 s plus one poll).
  local t0 t tk
  [ "$ENGINE_STARTED" = 1 ] || return 0
  box_wine_kill "$PF" || { echo "WINE NOT STOPPED: $BOX_WHY"; WINE_LEFT=1; return 1; }
  t0=$(date +%s); tk=$t0
  until box_engines_in_prefix "$PF"; do
    t=$(date +%s)
    [ $(( t - t0 )) -ge 60 ] && { echo "ENGINE LEFT RUNNING after wineserver -k: $BOX_WHY"; WINE_LEFT=1; return 1; }
    if [ $(( t - tk )) -ge 5 ] && [ $(( t - t0 )) -le 50 ]; then
      box_wine_kill "$PF" nowait || { echo "WINE NOT STOPPED: $BOX_WHY"; WINE_LEFT=1; return 1; }
      tk=$t; fi
    sleep 1
  done
  ENGINE_STARTED=0; return 0; }
report_left() {   # B-2: what was NOT cleaned up, by name, for a later manual step (reading only)
  local f
  echo "STOPPED: $BOX_STOPPED"
  echo "  nothing under run/ was written, moved or deleted after the stop${BOX_SKIPPED:+; skipped: $BOX_SKIPPED}"
  for f in "$LOGDIR"/* "$SAVES"/*; do [ -e "$f" ] && mine "${f##*/}" && echo "  left: $f"; done
  [ "$LTX_SWAPPED" = 1 ] && echo "  left: $RUN/appdata/user.ltx holds the det settings; by hand, once Caden is not playing: cp out/user.ltx.orig run/appdata/user.ltx"
  if [ -e "$OUT/boot.start" ]; then   # N3-5: one name per line, no splitting, no globbing
    while IFS= read -r f; do [ -n "$f" ] && echo "  left: $LOGDIR/$f (crash dump newer than this boot's start)"; done < <(box_dumps_newer "$LOGDIR" "$OUT/boot.start"); fi
  [ "$WINE_LEFT" = 1 ] && echo "  left: an engine in run/prefix may still run (see above)"
  return 0; }
cleanup() {
  [ "$CLEANED" = 1 ] && return 0; CLEANED=1
  trap '' TERM HUP INT
  stop_wine
  if [ "$LTX_SWAPPED" = 1 ] && [ -z "$BOX_STOPPED" ] && [ "$WINE_LEFT" = 0 ]; then
    box_guarded "restore run/appdata/user.ltx" cp -- "$P/out/user.ltx.orig" "$RUN/appdata/user.ltx" && LTX_SWAPPED=0
  fi
  box_kill_own "$WM_PID" openbox $$ || [ -z "$WM_PID" ] || echo "openbox: $BOX_WHY"
  box_kill_own "$XVFB_PID" Xvfb $$ || [ -z "$XVFB_PID" ] || echo "Xvfb: $BOX_WHY"
  [ -n "$BOX_STOPPED" ] && report_left
  if [ "$WINE_LEFT" = 1 ]; then [ "$CLAIMED" = 1 ] && echo "claim NOT released: an engine of this run may still run in run/prefix; the claim expires by itself"
  else release_own; fi
  return 0; }
on_sig() { [ -n "$BOX_STOPPED" ] || BOX_STOPPED="signal $1 (the caller stopped this run; its reason is not known here, so nothing under run/ is touched)"; RC=143; exit 143; }
trap cleanup EXIT; trap 'on_sig TERM' TERM; trap 'on_sig HUP' HUP; trap 'on_sig INT' INT   # N-2, N-15: before the claim
# DET_NOCLAIM=1: the caller already holds the claim for a whole series of runs
if [ "${DET_NOCLAIM:-0}" != 1 ]; then
  CLAIMED=1   # set first: a signal during the call is acted on after it, and release_own reads the claim-id it wrote
  if ! env "${BOX_QR_UNSET[@]}" "$T/quiet-required.sh" claim "$W" $((LIMIT/60+5)) "headless Xvfb GAMMA A-Life equivalence run $TAG $TOKEN" >"$OUT/claim.txt" 2>&1 </dev/null; then
    echo "REFUSED: no quiet slot ($(tail -1 "$OUT/claim.txt"))"; exit 3; fi
fi
box_fresh || { BOX_STOPPED="after the claim: $BOX_WHY"; echo "ABORT: $BOX_STOPPED"; exit 3; }   # B-1
export WINEPREFIX="$PF" DISPLAY="$DISP"
box_ok_display_free "$DISP" || { echo "REFUSED: $BOX_WHY"; exit 3; }   # S-4: never another worker's Xvfb
Xvfb "$DISP" -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & XVFB_PID=$!
_t0=$(date +%s); until [ -e "/tmp/.X11-unix/X${DISP#:}" ]; do [ $(( $(date +%s) - _t0 )) -ge 10 ] && { echo "Xvfb $DISP did not start"; exit 2; }; sleep 0.5; done
# N3-8: the socket may be ANOTHER server's that took :141 after the display_free test; ours then fails and exits at
# once ("server already active"). After a 1-s settle the X server must still be our own live Xvfb, else refuse.
sleep 1
box_is_own "$XVFB_PID" Xvfb $$ || { echo "REFUSED: the X server on $DISP is not this run's Xvfb ($BOX_WHY)"; exit 3; }
# N3-9: wmctrl bounded (an X round trip takes milliseconds; 5 s is far over it)
if ! timeout 5 wmctrl -m >/dev/null 2>&1; then openbox >/dev/null 2>&1 & WM_PID=$!
  for _ in $(seq 1 30); do timeout 5 wmctrl -m >/dev/null 2>&1 && break; sleep 0.5; done; fi
: > "$OUT/boot.start" || { echo "REFUSED: cannot write $OUT/boot.start"; exit 2; }   # N-7: this boot's start marker (out/)
guard() {   # <what> <command...>: box_guarded; a refusal ends the run (rc 3), a failing command rc 2
  box_guarded "$@" && return 0
  [ -n "$BOX_STOPPED" ] && { echo "ABORT: $BOX_STOPPED"; RC=3; exit 3; }
  echo "FAILED: $1"; RC=2; exit 2; }
# Serial test settings (Overseer ruling 2026-11-19): all mt_* off; restored from the pristine copy before every run
# because the engine rewrites its settings file on exit. The engine's -ltx option did not load the file; swap my run copy.
ltx_swap() {   # N3-10: overwrite user.ltx only while it holds one of the two known files' bytes, never other settings
  if ! cmp -s -- "$RUN/appdata/user.ltx" "$P/out/user.ltx.orig" && ! cmp -s -- "$RUN/appdata/user.ltx" "$P/out/user_det.ltx.orig"; then
    echo "REFUSED: $RUN/appdata/user.ltx is neither out/user.ltx.orig nor out/user_det.ltx.orig: not overwritten (may hold settings of Caden's)"; return 1; fi
  LTX_SWAPPED=1; cp -- "$P/out/user_det.ltx.orig" "$RUN/appdata/user.ltx"; }
clear_logs() {   # the old log and this tag's old dumps: one fixed folder, validated names only (box_rm_name)
  local f r=0
  box_rm_name "$LOGDIR" "xray_$ME.log" || { echo "$BOX_WHY"; r=1; }
  for f in "$LOGDIR"/alife_dump_"$TAG"_*.txt; do
    [ -e "$f" ] || continue; mine "${f##*/}" || continue
    box_rm_name "$LOGDIR" "${f##*/}" || { echo "$BOX_WHY"; r=1; }
  done; return $r; }
start_engine() {
  ENGINE_STARTED=1
  ( cd "$RUN" && ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy WINEDLLOVERRIDES="d3dcompiler_47=n" WINEDEBUG=-all \
    nice -n 10 wine explorer "/desktop=edet$$,1280x720" "Z:${RUN//\//\\}\\bin\\$EXE" -nointro -dbg \
    -start "server($SAVE/single/alife/load)" "client(localhost)" -det_mode -det_tag "$TAG" -det_interval "$INT" -det_checkpoints "$N" "${DX[@]}" \
    > "$OUT/wine_stderr.log" 2>&1 ) </dev/null &
  return 0; }
guard "user_det.ltx.orig -> run/appdata/user.ltx" ltx_swap
guard "remove run/appdata/logs/xray_$ME.log and this tag's old dumps" clear_logs
guard "start the engine" start_engine
START=$(date +%s)
inboot() {   # the checks while our engine runs (engine_running excluded: ours matches); a refusal records the stop
  box_ok_play_lock || { BOX_STOPPED="play_lock during the boot: $BOX_WHY"; return 1; }
  box_ok_title     || { BOX_STOPPED="protected_title during the boot: $BOX_WHY"; return 1; }
  box_ok_roblox    || { BOX_STOPPED="roblox during the boot: $BOX_WHY"; return 1; }
  return 0; }
waitchk() {   # <seconds>: wait, from date, checking every ~2 s (N-6); 1 = a stop was recorded
  local t0; t0=$(date +%s)
  while [ $(( $(date +%s) - t0 )) -lt "$1" ]; do sleep 1; inboot || return 1; done; return 0; }
while :; do
  sleep 5
  inboot || { echo "ABORT: $BOX_STOPPED"; RC=3; break; }
  if [ -s "$LOGDIR/alife_dump_${TAG}_$N.txt" ] && grep -qa "^#actor" "$LOGDIR/alife_dump_${TAG}_$N.txt"; then RC=0
    waitchk 8 || { echo "ABORT: $BOX_STOPPED"; RC=3; break; }
    case "${DET_EXTRA:-}" in *-det_save*)
      _t0=$(date +%s)
      until [ -s "$SAVES/det_${TAG}_$N.scop" ] || [ $(( $(date +%s) - _t0 )) -ge 90 ]; do waitchk 3 || break; done
      [ -n "$BOX_STOPPED" ] || waitchk 5
      [ -n "$BOX_STOPPED" ] && { echo "ABORT: $BOX_STOPPED"; RC=3; } ;;
    esac
    break; fi
  if [ -z "$(engine_main "$EXE")" ] && [ $(( $(date +%s) - START )) -gt 30 ]; then echo "engine exited early"; RC=4; break; fi
  [ $(( $(date +%s) - START )) -gt "$LIMIT" ] && { echo "TIMEOUT after ${LIMIT}s"; RC=5; break; }
done
stop_wine   # our engine down BEFORE anything is collected (and the engine check below must then read clear)
collect() {   # this tag's files and this boot's own crash dumps into out/ (one guarded step: no wait inside)
  local f
  cp -- "$LOGDIR/xray_$ME.log" "$OUT/xray.log" 2>/dev/null
  for f in "$LOGDIR"/* "$SAVES"/*; do [ -e "$f" ] && mine "${f##*/}" && mv -- "$f" "$OUT/"; done
  while IFS= read -r f; do   # N3-5: one name per line, no splitting, no globbing
    case "$f" in '') continue ;; *[!A-Za-z0-9_.-]*) echo "CRASH DUMP $f not moved (odd name)"; continue ;; esac
    echo "CRASH DUMP present"; mv -- "$LOGDIR/$f" "$OUT/"; done < <(box_dumps_newer "$LOGDIR" "$OUT/boot.start")
  return 0; }
if [ -z "$BOX_STOPPED" ] && [ "$WINE_LEFT" = 0 ]; then
  box_guarded "collect this tag's files from run/appdata" collect || { [ -n "$BOX_STOPPED" ] && { echo "ABORT: $BOX_STOPPED"; RC=3; }; }
fi
_old=$(find "$LOGDIR" -maxdepth 1 -type f -name '*.mdmp' ! -newer "$OUT/boot.start" -printf '%f ' 2>/dev/null)
[ -n "$_old" ] && echo "older crash dump(s) left in $LOGDIR (not this boot's, may be Caden's): $_old"
echo "rc=$RC elapsed=$(( $(date +%s) - START ))s out=$OUT"; grep -a "eperf dump" "$OUT/xray.log" 2>/dev/null | tail -4
exit $RC
