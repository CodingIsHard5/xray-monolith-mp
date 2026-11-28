#!/usr/bin/env bash
# Caden's test launcher for the fixed-tick build (S2 stage 4) with the A-Life warm-up (wu3 PASS, 2026-11-27). Runs ONLY from EnginePerf's own run tree
# (~/Projects/StalkerGameEngine/run): no game install, save folder or Wine prefix of Caden's is touched.
#   - its own Wine prefix (run/prefix_play, a copy of the bench prefix, made on first start)
#   - its own settings file (run/appdata/user_play.ltx, made from the bench settings on first start)
#   - the bench save "bench1" (GAMMA, Army Warehouses, a Duty squad walks past)
# Start (on the desktop):  DISPLAY=:0 setsid ~/.claude/overseer/tools/display-launch.sh "engineperf: Caden fixed-tick test" \
#                            ~/Projects/StalkerGameEngine/play/caden_simtick_play.sh </dev/null >/tmp/simtick-play.log 2>&1 &
# In game: Numpad 9 = tick 30, Numpad 6 = tick 60, Numpad 0 = off (stock). Console (~): sim_tick <hz>.
# A-Life warm-up is ON by default (smoother first 20 s after a load). Turn it OFF for one start:
#   ALIFE_WARMUP=0 ~/Projects/StalkerGameEngine/play/caden_simtick_play.sh   (or in game: console `alife_warmup 0`, then load)
# Warm18.exe = SimTick17's commit f36fe61b8 + only the lever commit d77d70b5c (lever default off in the exe itself).
set -u
ROOT="$HOME/Projects/StalkerGameEngine"; RUN="$ROOT/run"; EXE="${SIMTICK_EXE:-Warm18.exe}"; SHA="15828a7a6cadd38e42cc376869f581166884c04f1eb236baf79054a74332ae9d"
WARMUP="${ALIFE_WARMUP:-1}"; case "$WARMUP" in 0) WARMARG=();; 1) WARMARG=(-alife_warmup);; *) echo "NOT STARTED: ALIFE_WARMUP must be 0 or 1"; exit 2;; esac
RES="${SIMTICK_RES:-1920x1080}"; HZ="${SIMTICK_HZ:-30}"; T="$HOME/.claude/overseer/tools"; LOCK="$RUN/.caden-play.lock"
TEST="${SIMTICK_SELFTEST:-0}"   # 1 = EnginePerf's own headless boot test: no pop-up, and its own machine claim does not block the start
die() { echo "NOT STARTED: $*"; [ "$TEST" = 1 ] || { command -v notify-send >/dev/null && notify-send "Fixed-tick test not started" "$*"; }; exit 2; }
[ "$(sha256sum "$RUN/bin/$EXE" 2>/dev/null | cut -d' ' -f1)" = "$SHA" ] || die "$EXE is not the tested build"
[ "$TEST" != 1 ] && "$T/quiet-required.sh" check 2>/dev/null | grep -Eq '^(HELD|SLOT[0-9]+):.* engineperf ' && die "an EnginePerf benchmark holds the machine; try again in a few minutes"   # 2026-11-27: any slot, not only the main claim (a shared test claim sits in a SLOTn line)
pgrep -f "$RUN/bin/|${RUN//\//\\\\}\\\\bin" >/dev/null && die "an engine from this run tree is already running"
[ -e "$LOCK" ] && kill -0 "$(cat "$LOCK" 2>/dev/null)" 2>/dev/null && die "the test is already running"
# Everything the game can WRITE must be a private copy (Overseer 2026-11-26). The engine writes only below run/:
# appdata/ (settings, saves, logs, shader cache, screenshots) and gamedata/configs/axr_options.ltx (MCM).
#  1. the no-link guard of the benchmarks: no link under run/ may lead to a writable file in ~/Games, no hard links;
#  2. the written places themselves must be real folders/files inside run/, not links;
#  3. the play prefix has no link into the home folder (Wine's Documents, Desktop, ... become empty private folders).
"$ROOT/tools/run_noguard.sh" "$RUN" >/dev/null || die "the write guard refused: a path in the run folder leads into ~/Games"
for w in appdata appdata/savedgames appdata/logs appdata/shaders_cache gamedata gamedata/configs gamedata/configs/axr_options.ltx appdata/user.ltx fsgame.ltx; do
  [ -e "$RUN/$w" ] || continue
  [ "$(realpath "$RUN/$w")" = "$RUN/$w" ] || die "write guard: $w is not a private copy"
done
[ -n "$(find "$RUN/appdata" -type l -print -quit)" ] && die "write guard: a link inside appdata"
echo $$ > "$LOCK"
# The benchmarks use the same run folder: their MCM options and saves are put back when the game closes; Caden's
# own MCM options are kept in appdata/axr_options_play.ltx and used again on the next start.
KEEP="$RUN/.play_keep"; OPT="$RUN/gamedata/configs/axr_options.ltx"; MYOPT="$RUN/appdata/axr_options_play.ltx"
restore() {
  WINEPREFIX="$RUN/prefix_play" wineserver -k >/dev/null 2>&1; sleep 1
  if [ -d "$KEEP" ]; then
    [ -e "$OPT" ] && cp -f "$OPT" "$MYOPT"
    [ -e "$KEEP/axr_options.ltx" ] && cp -f "$KEEP/axr_options.ltx" "$OPT"
    for f in "$KEEP"/saves/*; do [ -e "$f" ] && { cmp -s "$f" "$RUN/appdata/savedgames/${f##*/}" || cp -f "$f" "$RUN/appdata/savedgames/${f##*/}"; }; done
  fi
  rm -f "$LOCK"
}
trap restore EXIT
mkdir -p "$KEEP/saves" || die "could not make the keep folder"
if [ ! -e "$KEEP/axr_options.ltx" ] && [ -e "$OPT" ]; then cp -p "$OPT" "$KEEP/axr_options.ltx"; fi
for f in bench1.scop bench1.scoc cordon1.scop cordon1.scoc; do [ -e "$KEEP/saves/$f" ] || cp -p "$RUN/appdata/savedgames/$f" "$KEEP/saves/$f" || die "could not keep $f"; done
[ -e "$MYOPT" ] && cp -f "$MYOPT" "$OPT"
if [ ! -d "$RUN/prefix_play" ]; then
  cp -a "$RUN/prefix" "$RUN/prefix_play" || die "could not make the play prefix"
  find "$RUN/prefix_play/drive_c/users" -type l | while IFS= read -r l; do rm -f "$l" && mkdir -p "$l"; done
  for d in "$RUN/prefix_play/dosdevices"/*; do case "${d##*/}" in c:|z:) ;; *) [ -L "$d" ] && rm -f "$d";; esac; done
fi
[ -n "$(find "$RUN/prefix_play/drive_c" -type l -print -quit)" ] && die "write guard: a link inside the play prefix"
P="$RUN/appdata/user_play.ltx"
if [ ! -s "$P" ]; then
  grep -v -E '^(vid_mode|sim_tick|r__framelimit|rs_screenmode|bind_console sim_tick) ' "$RUN/appdata/user.ltx" > "$P"
  printf 'vid_mode %s\r\nrs_screenmode borderless\r\nr__framelimit 0\r\nbind_console sim_tick 30 kNUMPAD9\r\nbind_console sim_tick 60 kNUMPAD6\r\nbind_console sim_tick 0 kNUMPAD0\r\n' "$RES" >> "$P"
fi
grep -v '^sim_tick ' "$P" > "$P.tmp" && printf 'sim_tick %s\r\n' "$HZ" >> "$P.tmp" && cat "$P.tmp" > "$P" && rm -f "$P.tmp"
cd "$RUN" && WINEPREFIX="$RUN/prefix_play" WINEDLLOVERRIDES="d3dcompiler_47=n" WINEDEBUG=-all \
  wine explorer "/desktop=simtickplay,$RES" "Z:${RUN//\//\\}\\bin\\$EXE" -nointro -ltx user_play.ltx "${WARMARG[@]}" \
  -start "server(bench1/single/alife/load)" "client(localhost)"
