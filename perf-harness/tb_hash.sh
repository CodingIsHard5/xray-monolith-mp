#!/bin/bash
# Fingerprint of what a single-player tier (b) run (tools/tb_run.sh) executes: one line per part, 16-hex sha256 prefix
# and a file count. Usage: tools/tb_hash.sh <exe in run/bin> [save ...]   (default saves: bench1 cordon1)
# R1/R2 reference groups are reusable only while every line except "exe" (and "tools", if only tooling changed) matches.
# db archives: names + sizes + mtimes by default (17 GB); TB_HASH_FULL=1 hashes their content (slow).
# TB_P=<dir> points it at another project tree (used to test the script on a mock tree).
P="${TB_P:-$HOME/Projects/StalkerGameEngine}"; RUN="$P/run"
EXE="${1:?exe}"; shift; [ $# -gt 0 ] || set -- bench1 cordon1
h() { sha256sum | cut -c1-16; }
cnt() { find -L "$@" -type f 2>/dev/null | wc -l; }
[ -f "$RUN/bin/$EXE" ] || { echo "no exe $RUN/bin/$EXE"; exit 2; }
echo "exe        $(h < "$RUN/bin/$EXE")  files=1  ($EXE)"
echo "bin_libs   $(cd "$RUN/bin" && find -L . -type f ! -name '*.exe' ! -name '*.run_id' | LC_ALL=C sort | xargs -d '\n' sha256sum | h)  files=$(cd "$RUN/bin" && find -L . -type f ! -name '*.exe' ! -name '*.run_id' | wc -l)"
if [ "${TB_HASH_FULL:-0}" = 1 ]; then
  echo "db         $(cd "$RUN/db" && find -L . -type f | LC_ALL=C sort | xargs -d '\n' sha256sum | h)  files=$(cnt "$RUN/db")  (content)"
else
  echo "db         $(cd "$RUN/db" && find -L . -type f -printf '%p %s %T@\n' | LC_ALL=C sort | h)  files=$(cnt "$RUN/db")  (names+sizes+mtimes)"
fi
echo "scripts    $(cd "$RUN/gamedata/scripts" && find -L . -type f | LC_ALL=C sort | xargs -d '\n' sha256sum | h)  files=$(cnt "$RUN/gamedata/scripts")  (content)"
echo "configs    $(cd "$RUN/gamedata" && find -L configs Configs -type f 2>/dev/null | LC_ALL=C sort | xargs -d '\n' sha256sum | h)  files=$(cd "$RUN/gamedata" && cnt configs Configs)  (content)"
echo "gamedata   $(cd "$RUN/gamedata" && find -L . -path ./scripts -prune -o -path ./configs -prune -o -path ./Configs -prune -o -type f -printf '%p %s\n' | LC_ALL=C sort | h)  files=$(cd "$RUN/gamedata" && find -L . -path ./scripts -prune -o -path ./configs -prune -o -path ./Configs -prune -o -type f -print | wc -l)  (rest: names+sizes)"
echo "fsgame     $(h < "$RUN/fsgame.ltx")  files=1"
echo "user_ltx   $(h < "$P/out/user.ltx.orig")  files=1  (out/user.ltx.orig, swapped in by tb_run.sh)"
for s in "$@"; do
  f=$(ls "$RUN/appdata/savedgames/$s".sc* 2>/dev/null | LC_ALL=C sort)
  [ -n "$f" ] || { echo "save:$s   MISSING"; continue; }
  echo "save:$s   $(echo "$f" | xargs -d '\n' cat | h)  files=$(echo "$f" | wc -l)"
done
echo "overlay    $(cat "$RUN/gamedata/scripts/zz_eperf_dump.script" | h)  files=1  (run tree copy; harness copy $(h < "$P/harness/zz_eperf_dump.script"))"
echo "tools      $(cat "$P/tools/tb_run.sh" "$P/tools/tier_b.py" | h)  files=2"
