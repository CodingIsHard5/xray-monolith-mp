#!/bin/bash
# fake tb_run that ends like the real one: rc 0 when the run's done marker exists; rc 4 when no process names the exe
export WINEPREFIX=$PWD/run/prefix; TAG=$1; RID=$6; mkdir -p out/tb_$TAG; rm -f out/tb_$TAG/done
CR=99999; [ "$TAG" = t2b1 ] && CR=${FAKE_CRASH_B1:-99999}
/tmp/claude-1000/-home-caden-Projects-StalkerGameEngine/a16cf6b0-5be3-45bc-b92c-647e8bfcd9b5/scratchpad/t2sup/fakebin/wine64 tools/fake_engine.py 'bin\RefDet14.exe' "$T2_SHM" "$RID" "$TB_TIMES" $CR $TAG &
START=$(date +%s)
while :; do sleep 5; [ -f out/tb_$TAG/done ] && exit 0; pgrep -f 'bin\\RefDet14.exe' >/dev/null || { echo "engine exited"; exit 4; }; [ $(($(date +%s)-START)) -gt $TB_TIMEOUT ] && { echo TIMEOUT; exit 5; }; done
