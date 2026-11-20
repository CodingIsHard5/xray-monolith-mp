#!/bin/bash
# Fingerprint of what a co-op series boot runs: server/client binaries, the server script set (link names + the
# content they resolve to), the overlay script source, and the driver/launcher/sampler. Prints one line per part.
S=~/Projects/StalkerGameEngine/coopload; P=~/Projects/StalkerGameEngine
h() { sha256sum | cut -c1-16; }
echo "bin        $(cd $S/bin && find . -type f | sort | xargs sha256sum | h)  files=$(find $S/bin -type f | wc -l)"
echo "scripts    $(cd $S/gamedata_sv/scripts && for f in $(ls | sort); do if [ -d "$f" ]; then echo "$f DIR $(find -L "$f" -type f | sort | xargs sha256sum | sha256sum)"; else echo "$f $(sha256sum < "$f")"; fi; done | h)  entries=$(ls $S/gamedata_sv/scripts | wc -l)"
echo "gamedata   $(cd $S/gamedata_sv && find . -maxdepth 2 | sort | h)  (names to depth 2)"
echo "overlay    $(sha256sum < $P/harness/zz_eperf_svbinder.script | cut -c1-16)"
echo "tools      $(cat $P/tools/coop_series.sh $P/tools/coop_load.sh $P/tools/coop_sample.py | h)"
