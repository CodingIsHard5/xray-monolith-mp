#!/bin/bash
# fake: args tag save -p1 -t2_pose -t2_run RID ...
TAG=$1; RID=$6; mkdir -p out/tb_$TAG; CR=99999; [ "$TAG" = t2b0 ] && CR=${FAKE_CRASH_B1:-99999}
python3 tools/fake_engine.py 'bin\RefDet14.exe' "$T2_SHM" "$RID" "$TB_TIMES" "$CR" & E=$!
wait $E; r=$?; echo "fake engine rc=$r" > out/tb_$TAG/xray.log; [ $r = 0 ] && exit 0 || exit 4
