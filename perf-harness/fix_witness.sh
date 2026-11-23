#!/usr/bin/env bash
# Witness boot for BUGFIXES.md CANDIDATE 3 (ammo/grenade index clamp, perf/fixes e8eb661df). One X-Ray-only EXCLUSIVE
# slice via WANT (fair share; no queue jumping). One full T1 track run (stalker 19068 with wpn_mosin19070 comes online at
# track ~775-796 s in all 6 diag runs), stall off, on Fix14.exe. PASS: a "[FIX] CWeapon::net_Spawn: [wpn_mosin19070]" line,
# NO stock "m_ammoTypes[m_ammoType] is invalid" line, NO "FATAL ERROR", and the track ends.
set -u
cd "$(dirname "$0")/.."; T="$HOME/.claude/overseer/tools"; W=engineperf; O=out/fixwit; mkdir -p $O
EXE=Fix14.exe; WHAT="EXCLUSIVE: headless X-Ray-only witness boot for the ammo clamp fix (one engine)"
log() { echo "$(date '+%F %T') $*" | tee -a $O/witness.log; }
[ "$(sha256sum run/bin/$EXE 2>/dev/null | cut -c1-16)" = "${FIX_SHA:?set FIX_SHA}" ] || { log "REFUSED: $EXE hash differs"; exit 2; }
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 30 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
log "slice granted, load $(cut -d' ' -f1-3 /proc/loadavg)"
T1_EXE=$EXE T1_STALL_AT=99999 tools/t1_xray.sh $O/run none-itr2 > $O/run.out 2>&1; rc=$?
L=$O/run/tb/xray.log
fix=$(grep -a -c '\[FIX\] CWeapon::net_Spawn: \[wpn_mosin19070\]' $L 2>/dev/null); inv=$(grep -a -c 'm_ammoTypes\[m_ammoType\] is invalid' $L 2>/dev/null)
fat=$(grep -a -c 'FATAL ERROR' $L 2>/dev/null); allfix=$(grep -a -c '\[FIX\]' $L 2>/dev/null)
v=FAIL; [ "${fix:-0}" -ge 1 ] && [ "${inv:-1}" = 0 ] && [ "${fat:-1}" = 0 ] && [ $rc = 0 ] && v=PASS
log "t1_xray rc=$rc fix_mosin=$fix fix_all=$allfix stock_invalid=$inv fatal=$fat -> WITNESS $v"
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1   # withdraw FIRST
log "slice end"
timeout 60 "$T/notify-overseer.sh" engineperf - <<<"EnginePerf clamp-fix witness boot: $v (fix_mosin=$fix stock_invalid=$inv fatal=$fat rc=$rc). Slice released $(date +%T)." >/dev/null 2>&1
