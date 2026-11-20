#!/usr/bin/env bash
# EnginePerf scratch launcher for the co-op second-actor measurement. Reads the co-op worker's install, writes only under
# ~/Projects/StalkerGameEngine/coopload. Own Wine prefix (a COPY of theirs), own port, server memory cap 6G, cores 0-1 kept free.
# usage: coop_load.sh setup | server [extra engine flags] | client <n> | svpid | clpid <n> | stop
set -u
P="$HOME/Projects/StalkerGameEngine"; S="$P/coopload"; SRC="$HOME/Projects/StalkerMPMod"; PORT=5461
GD=/media/caden/games/gamma_flat/gamedata; DB="$HOME/Games/STALKER/ANOMALY/db"
export WINEPREFIX="$S/prefix"
common=(env -u DISPLAY WINEPREFIX="$WINEPREFIX" WINEDEBUG= WINEDLLOVERRIDES="winedbg.exe=d" ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy)
mkroot() { # dir, gamedata target
  mkdir -p "$1/appdata/logs" "$1/appdata/savedgames"
  cp "$SRC/run/fsgame.ltx" "$1/fsgame.ltx"
  ln -sfn "$S/bin" "$1/bin"; ln -sfn "$DB" "$1/db"; ln -sfn "$2" "$1/gamedata"
}
big() { for p in "$@"; do echo "$(awk '/VmRSS/{print $2}' /proc/$p/status 2>/dev/null || echo 0) $p"; done | sort -rn | head -1 | cut -d' ' -f2; }
case "${1:-}" in
setup)
  mkdir -p "$S/bin"
  for f in OurDedicatedDX8.exe OurDedicatedDX8.run_id icudt65.dll icuuc65.dll tbb.dll soft_oal.dll xrd3d9-null.dll discord_game_sdk.dll alsoft.ini; do
    [ -e "$SRC/run/bin/$f" ] && cp -n "$SRC/run/bin/$f" "$S/bin/"; done
  [ -d "$S/prefix" ] || cp -a "$SRC/run/prefix" "$S/prefix"
  # server gamedata: links to every top-level entry of gamma_flat, scripts as a folder of links plus the test overlay
  mkdir -p "$S/gamedata_sv/scripts"
  for e in "$GD"/*; do b=$(basename "$e"); [ "$b" = scripts ] || ln -sfn "$e" "$S/gamedata_sv/$b"; done
  # the co-op worker's ungated test instruments (mp_spike*.script) are left OUT of the scratch server's scripts: they
  # change the A-Life online radius and anchors on a schedule (Overseer ruling 2026-11-20). gamma_flat is not touched.
  for e in "$GD"/scripts/*; do case "$(basename "$e")" in mp_spike*.script) continue;; esac; ln -sfn "$e" "$S/gamedata_sv/scripts/$(basename "$e")"; done
  cp "$P/harness/zz_eperf_svbinder.script" "$S/gamedata_sv/scripts/"
  mkroot "$S/sv" "$S/gamedata_sv"
  cp -n "$SRC/run/appdata/savedgames/n1-autosave."* "$S/sv/appdata/savedgames/"; cp -n "$SRC/run/appdata/user.ltx" "$S/sv/appdata/"
  for i in 1 2 3 4 5 6 7 8; do mkroot "$S/c$i" "$GD"; cp -n "$SRC/run2/appdata/user.ltx" "$S/c$i/appdata/"; done
  echo "setup done: $(du -sh "$S" | cut -f1), exe run_id $(cat "$S/bin/OurDedicatedDX8.run_id" 2>/dev/null), sha $(sha256sum "$S/bin/OurDedicatedDX8.exe" | cut -c1-16) (theirs $(sha256sum "$SRC/run/bin/OurDedicatedDX8.exe" | cut -c1-16))"
  ;;
server)
  shift; cd "$S/sv" || exit 2
  systemd-run --user --scope --quiet --collect -p MemoryMax=6G -p MemorySwapMax=0 -- \
    "${common[@]}" taskset -c 2,3 nice -n 19 ionice -c3 wine bin/OurDedicatedDX8.exe -nointro -noprefetch -nosound -mp_host -mp_port $PORT \
    -xrnet_udp -mp_noauth -start "server(n1-autosave/single/alife/load)" -fp_svtime -coop_clframetrace "$@" \
    > "$S/sv/appdata/logs/stderr.log" 2>&1 &
  ;;
client)
  n="$2"; cd "$S/c$n" || exit 2; c0=$((2 + 2 * n)); c1=$((c0 + 1)); [ "$n" -gt 6 ] && { c0=$((n - 3)); c1=$((n + 3)); } # 1..6 -> 4/5..14/15; 7 -> 4,10; 8 -> 5,11
  setsid nohup "${common[@]}" taskset -c "$c0,$c1" nice -n 19 ionice -c3 wine bin/OurDedicatedDX8.exe -nointro -nosound -noinput \
    -coop_noimgui -coop_test_input -xrnet_udp -mp_port $PORT -mp_noauth -start "client(localhost/name=ep$n)" \
    > "$S/c$n/appdata/logs/stderr.log" 2>&1 &
  ;;
# several processes carry the command line (launcher stubs); the engine is the one with the most resident memory
svpid) big $(pgrep -f '[O]urDedicatedDX8.exe.*-mp_host.*'"$PORT") ;;
clpid) big $(pgrep -f '[O]urDedicatedDX8.exe.*name=ep'"$2"'\b') ;;
stop) wineserver -k >/dev/null 2>&1; sleep 2; pgrep -f "[O]urDedicatedDX8.exe.*$PORT" >/dev/null && echo "STILL ALIVE" || echo stopped ;;
*) echo "usage: setup|server|client n|svpid|clpid n|stop"; exit 2 ;;
esac
