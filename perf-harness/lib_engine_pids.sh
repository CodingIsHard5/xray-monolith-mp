# Engine process lookup for the harness (source it). Overseer 2026-11-23: no bare `pgrep -f` name match anywhere.
# Twice in B1-T2 a name match hit our own watcher (its argv names the exe): once in t2_xray.sh, once inside tb_run.sh.
# A process counts as the engine only if its BINARY (/proc/<pid>/exe) is a Wine loader AND its command line names
# bin\<exe>. Python, bash, grep, tee, watchers and launch shells never have a Wine binary, so they can never match.
# engine_pids <exe> [pattern]   all matching Wine pids (pattern default "bin\<exe>")
# engine_big  <exe> [pattern]   the one with the biggest RSS (the engine itself, not the wine/explorer wrappers)
engine_pids() {
  local pat=${2:-"bin\\\\$1"} p
  for p in $(pgrep -f "$pat" 2>/dev/null); do
    case "$(readlink "/proc/$p/exe" 2>/dev/null)" in *wine*) echo "$p" ;; esac
  done
}
engine_big() {
  local best="" br=0 r p
  for p in $(engine_pids "$@"); do r=$(awk '/VmRSS/{print $2}' "/proc/$p/status" 2>/dev/null); [ "${r:-0}" -gt "$br" ] && { br=$r; best=$p; }; done
  echo "$best"
}
# engine_main <exe> [pattern]  the engine only if it is really running: the biggest match with RSS > 500 MB (ENGINE_MIN_RSS_KB, for fake tests only). The Wine
# wrappers (wine/explorer, ~20-30 MB) can outlive a crashed or killed engine (pre-flight 2), so "is the engine still
# running" checks use this, not engine_pids.
engine_main() {
  local p; p=$(engine_big "$@"); [ -n "$p" ] && [ "$(awk '/VmRSS/{print $2}' "/proc/$p/status" 2>/dev/null || echo 0)" -gt "${ENGINE_MIN_RSS_KB:-500000}" ] && echo "$p"
}
