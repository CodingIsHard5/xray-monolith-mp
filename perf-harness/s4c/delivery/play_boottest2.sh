#!/usr/bin/env bash
# One headless (Xvfb) boot of Caden's launcher (Overseer GO 2026-11-26). Output out/play_boottest.txt. Background task.
# PASS, stated before the run, when all hold:
#  B1 the launcher starts and the engine log (written after the start) shows a [SIMTICK] line with ticks >= 100 within 300 s
#  B2 no "FATAL ERROR" in that log (test 1 also matched "stack trace": wrong, GAMMA prints Lua tracebacks as warnings in every run)
#  B3 after the stop: the lock is gone, the benchmark MCM options and the four kept saves are byte-identical to before
#  B4 nothing under ~/Games changed: sha256 + mtime of every former link target in run/private_copies_manifest.tsv and of
#     every axr_options*.ltx, user.ltx, *.scop, *.scoc found under ~/Games/STALKER are equal before and after, and no file
#     under ~/Games/STALKER is newer than the start marker
# Missing inputs fail by name. No retry.
set -u
cd "$(dirname "$0")/.."; T="$HOME/.claude/overseer/tools"; W=engineperf; O=out/play_boottest2.txt; RUN="$PWD/run"; G="$HOME/Games/STALKER"
[ -e "$O" ] && { echo "$O exists, not overwriting"; exit 2; }
[ -s run/private_copies_manifest.tsv ] || { echo "missing run/private_copies_manifest.tsv"; exit 2; }
[ -d "$G" ] || { echo "missing $G"; exit 2; }
S=out/play_boottest2_state; mkdir "$S" || exit 2
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 25 "headless Xvfb boot test of the fixed-tick launcher" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
XV=""; trap '[ -n "$XV" ] && kill "$XV" 2>/dev/null; WINEPREFIX="'"$RUN"'/prefix_play" wineserver -k >/dev/null 2>&1; "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1' EXIT
exec > "$O" 2>&1
echo "$(date '+%F %T') slice granted"
snap() { { cut -f2 run/private_copies_manifest.tsv | grep '^/'; find "$G" \( -name 'axr_options*.ltx' -o -name 'user.ltx' -o -name '*.scop' -o -name '*.scoc' \) -type f; } | sort -u | while IFS= read -r f; do
           if [ -f "$f" ]; then printf '%s\t%s\t%s\n' "$(sha256sum < "$f" | cut -c1-64)" "$(stat -c %Y "$f")" "$f"; else printf 'ABSENT\t-\t%s\n' "$f"; fi; done > "$1"; }
snap "$S/games_before.tsv"; echo "watched files under ~/Games: $(wc -l < "$S/games_before.tsv") ($(grep -c '^ABSENT' "$S/games_before.tsv") absent)"
sha256sum run/gamedata/configs/axr_options.ltx run/appdata/savedgames/bench1.scop run/appdata/savedgames/bench1.scoc run/appdata/savedgames/cordon1.scop run/appdata/savedgames/cordon1.scoc run/appdata/user.ltx > "$S/own_before.txt" || { echo "RESULT: NO VERDICT (an own file is missing)"; exit 2; }
touch "$S/marker"; sleep 1
# its OWN display: a number no other project's tools use, free right now (test 1 took :78, which GammaVR's runs use)
D=""; for n in 141; do [ -e /tmp/.X11-unix/X$n ] || [ -e /tmp/.X$n-lock ] || { D=:$n; break; }; done
[ -n "$D" ] || { echo "RESULT: NO VERDICT (display :141 is already in use: refused)"; exit 2; }
Xvfb $D -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 & XV=$!; sleep 2
kill -0 $XV 2>/dev/null || { echo "RESULT: NO VERDICT (Xvfb did not start on $D)"; exit 2; }; echo "own display $D"
L=run/appdata/logs/xray_$(whoami).log
DISPLAY=$D ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy SIMTICK_SELFTEST=1 SIMTICK_RES=1280x720 setsid play/caden_simtick_play.sh > "$S/launcher.log" 2>&1 & LP=$!
b1=FAIL; for i in $(seq 100); do sleep 3
  kill -0 $XV 2>/dev/null || { echo "the display server went away: not a launcher result"; b1="NO VERDICT"; break; }
  w=$(DISPLAY=$D xdotool search --name simtickplay 2>/dev/null | head -1); [ -n "$w" ] && DISPLAY=$D xdotool windowactivate --sync "$w" key space 2>/dev/null   # past a "press any key" screen, as the bench does
  kill -0 $LP 2>/dev/null || { echo "launcher ended early: $(tail -2 "$S/launcher.log")"; break; }
  if [ "$L" -nt "$S/marker" ] && grep -a '\[SIMTICK\]' "$L" | tr -d '\r' | awk '{for(i=1;i<NF;i++) if($i=="ticks" && $(i+1)>=100) f=1} END{exit !f}'; then b1=PASS; break; fi; done
echo "B1 engine in game with the tick on: $b1 ($(grep -a '\[SIMTICK\]' "$L" 2>/dev/null | tail -1 | tr -d '\r'))"
[ $b1 = PASS ] && sleep 20      # a little play time with input on
cp "$L" "$S/xray.log" 2>/dev/null
WINEPREFIX="$RUN/prefix_play" wineserver -k >/dev/null 2>&1
for i in $(seq 30); do kill -0 $LP 2>/dev/null || break; sleep 1; done; kill -0 $LP 2>/dev/null && { echo "launcher still alive after the stop"; kill -TERM -- -$LP 2>/dev/null; sleep 3; }
b2=PASS; grep -a -q 'FATAL ERROR' "$S/xray.log" 2>/dev/null && b2=FAIL; [ -s "$S/xray.log" ] || b2="NO VERDICT (no log)"
echo "B2 no fatal error in the log: $b2"
b3=PASS; [ -e run/.caden-play.lock ] && { b3=FAIL; echo "  lock still there"; }
sha256sum -c "$S/own_before.txt" > "$S/own_after.txt" 2>&1 || { b3=FAIL; grep -v ': OK$' "$S/own_after.txt" | sed 's/^/  /'; }
echo "B3 lock gone, benchmark options, saves and settings unchanged: $b3"
snap "$S/games_after.tsv"; newer=$(find "$G" -newer "$S/marker" 2>/dev/null | head -5)
b4=PASS; cmp -s "$S/games_before.tsv" "$S/games_after.tsv" || { b4=FAIL; diff "$S/games_before.tsv" "$S/games_after.tsv" | head -5 | sed 's/^/  /'; }
[ -n "$newer" ] && { b4=FAIL; echo "  newer than the start: $newer"; }
echo "B4 nothing under ~/Games changed ($(wc -l < "$S/games_after.tsv") files by sha256 and mtime; none newer than the start): $b4"
echo "launcher output: $(tr '\n' '|' < "$S/launcher.log" | cut -c1-300)"
echo "play prefix links left in drive_c: $(find run/prefix_play/drive_c -type l 2>/dev/null | wc -l); own play settings file: $(ls run/appdata/user_play.ltx 2>&1)"
if [ "$b1$b2$b3$b4" = PASSPASSPASSPASS ]; then echo "RESULT: PASS"; else echo "RESULT: FAIL"; fi
echo "$(date '+%F %T') end"
