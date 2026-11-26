#!/usr/bin/env bash
# Caden's test launcher for the fixed-tick build (S2 stage 4). Runs ONLY from EnginePerf's own run tree
# (~/Projects/StalkerGameEngine/run): no game install, save folder or Wine prefix of Caden's is touched.
#   - its own Wine prefix (run/prefix_play, a copy of the bench prefix, made on first start)
#   - its own settings file (run/appdata/user_play.ltx, made from the bench settings on first start)
#   - the bench save "bench1" (GAMMA, Army Warehouses, a Duty squad walks past)
# Start (on the desktop):  DISPLAY=:0 setsid ~/.claude/overseer/tools/display-launch.sh "engineperf: Caden fixed-tick test" \
#                            ~/Projects/StalkerGameEngine/play/caden_simtick_play.sh </dev/null >/tmp/simtick-play.log 2>&1 &
# In game: Numpad 9 = tick 30, Numpad 6 = tick 60, Numpad 0 = off (stock). Console (~): sim_tick <hz>.
set -u
ROOT="$HOME/Projects/StalkerGameEngine"; RUN="$ROOT/run"; EXE="${SIMTICK_EXE:-SimTick17.exe}"; SHA="cb5f2f8706ed89e5bcab9fdc832676b461543e725b13375a3be382240da11d82"
RES="${SIMTICK_RES:-1920x1080}"; HZ="${SIMTICK_HZ:-30}"; T="$HOME/.claude/overseer/tools"; LOCK="$RUN/.caden-play.lock"
die() { echo "NOT STARTED: $*"; command -v notify-send >/dev/null && notify-send "Fixed-tick test not started" "$*"; exit 2; }
[ "$(sha256sum "$RUN/bin/$EXE" 2>/dev/null | cut -d' ' -f1)" = "$SHA" ] || die "$EXE is not the tested build"
"$T/quiet-required.sh" check 2>/dev/null | grep -q '^HELD:.* engineperf ' && die "an EnginePerf benchmark holds the machine; try again in a few minutes"
pgrep -f "$RUN/bin/|${RUN//\//\\\\}\\\\bin" >/dev/null && die "an engine from this run tree is already running"
[ -e "$LOCK" ] && kill -0 "$(cat "$LOCK" 2>/dev/null)" 2>/dev/null && die "the test is already running"
echo $$ > "$LOCK"; trap 'WINEPREFIX="$RUN/prefix_play" wineserver -k >/dev/null 2>&1; rm -f "$LOCK"' EXIT
[ -d "$RUN/prefix_play" ] || cp -a "$RUN/prefix" "$RUN/prefix_play" || die "could not make the play prefix"
P="$RUN/appdata/user_play.ltx"
if [ ! -s "$P" ]; then
  grep -v -E '^(vid_mode|sim_tick|r__framelimit|rs_screenmode|bind_console sim_tick) ' "$RUN/appdata/user.ltx" > "$P"
  printf 'vid_mode %s\r\nrs_screenmode borderless\r\nr__framelimit 0\r\nbind_console sim_tick 30 kNUMPAD9\r\nbind_console sim_tick 60 kNUMPAD6\r\nbind_console sim_tick 0 kNUMPAD0\r\n' "$RES" >> "$P"
fi
grep -v '^sim_tick ' "$P" > "$P.tmp" && printf 'sim_tick %s\r\n' "$HZ" >> "$P.tmp" && cat "$P.tmp" > "$P" && rm -f "$P.tmp"
cd "$RUN" && WINEPREFIX="$RUN/prefix_play" WINEDLLOVERRIDES="d3dcompiler_47=n" WINEDEBUG=-all \
  wine explorer "/desktop=simtickplay,$RES" "Z:${RUN//\//\\}\\bin\\$EXE" -nointro -ltx user_play.ltx \
  -start "server(bench1/single/alife/load)" "client(localhost)"
