#!/usr/bin/env bash
# T3 "the exe without the bridge behaves as before" (PREREG-B1-T2-xray-side section 34 check 2, details section 37).
# One X-Ray-only EXCLUSIVE slice via WANT. Two full T1 track boots (no -t2_pose, no pose file, stall off):
# Stance14.exe, then Absorb14.exe. Modelled on tools/fix_witness.sh. Scored afterwards by t3_nobridge_check.py.
set -u
cd "$(dirname "$0")/.."; T="$HOME/.claude/overseer/tools"; W=engineperf; O=out/t3nb; mkdir -p $O
WHAT="EXCLUSIVE: headless X-Ray-only stance-export no-bridge check (two T1 boots, one engine at a time)"
log() { echo "$(date '+%F %T') $*" | tee -a $O/nobridge.log; }
[ "$(sha256sum run/bin/Stance14.exe 2>/dev/null | cut -c1-16)" = b4ce9c772ae5fa4b ] || { log "REFUSED: Stance14 hash differs"; exit 2; }
[ "$(sha256sum run/bin/Absorb14.exe 2>/dev/null | cut -c1-16)" = e35a289b31534f0d ] || { log "REFUSED: Absorb14 hash differs"; exit 2; }
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 55 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
log "slice granted, load $(cut -d' ' -f1-3 /proc/loadavg)"
rc=0
for E in Stance14 Absorb14; do
  T1_EXE=$E.exe T1_STALL_AT=99999 tools/t1_xray.sh $O/$E none-itr2 > $O/$E.out 2>&1; r=$?
  log "$E t1_xray rc=$r"
  [ $r = 0 ] || { rc=$r; log "STOP: $E did not end cleanly; the second boot is not started"; break; }
done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1   # withdraw FIRST
log "slice end rc=$rc"
exit $rc
