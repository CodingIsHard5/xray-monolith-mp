#!/usr/bin/env bash
# S2 stage 4c part A (PREREG-S2-4c section 2; a copy of tools/s2_partA2.sh with the tags and the build changed): off = stock with checkpoints at A-Life PASS COUNTS, 3 stock + 3 switch-off
# runs, alternating. WANT-queue wrapper; output out/s4c_partA.txt. Start as a background task. Score afterwards, once:
#   python3 -I tools/s2_partA2_score.py out s4c_s1,s4c_s2,s4c_s3 s4c_o1,s4c_o2,s4c_o3 out/s4c_partA_score.txt
set -u
cd "$(dirname "$0")/.."; T="$HOME/.claude/overseer/tools"; W=engineperf; O=out/s4c_partA.txt
# --- Flag preflight (Overseer 2026-11-25): this call against the counter method adopted on 2026-11-19
# (PREREG-tierA-counters-amend-and-worldproc, part 1). Every difference must be listed in ALLOWED, each with its reason
# in section 9. Any other difference refuses the run before the queue is joined.
REF="-det_passes 50,600,1650 -det_save -det_lua_streams -det_counters"
EXTRA="-det_passes 100,200,320 -det_lua_streams -det_counters"
ALLOWED="-:50,600,1650 -:-det_save +:100,200,320"
DIFF=$( { for w in $REF; do case " $EXTRA " in *" $w "*) ;; *) echo "-:$w";; esac; done
          for w in $EXTRA; do case " $REF " in *" $w "*) ;; *) echo "+:$w";; esac; done; } | tr '\n' ' ' )
[ "$(echo $DIFF)" = "$ALLOWED" ] || { echo "REFUSED: flags differ from the 11-19 method in an unlisted way: [$DIFF] allowed [$ALLOWED]"; exit 2; }
echo "flag preflight ok: differences from the 11-19 method = [$ALLOWED]"
[ "${1:-}" = "--flags-only" ] && exit 0
TAGS="s4c_s1 s4c_o1 s4c_s2 s4c_o2 s4c_s3 s4c_o3"
for t in $TAGS; do [ -e out/det_$t ] && { echo "out/det_$t exists, not overwriting"; exit 2; }; done
[ -e "$O" ] && { echo "$O exists, not overwriting"; exit 2; }
[ "$(sha256sum run/bin/SimTick17.exe | cut -d' ' -f1)" = cb5f2f8706ed89e5bcab9fdc832676b461543e725b13375a3be382240da11d82 ] || { echo "REFUSED: SimTick17.exe hash differs"; exit 2; }
# stage 4a addition: the settings file must have both switches at 0 (the series scripts leave them so); checked again when the slice is granted
cfg0() { for k in sim_tick r__framelimit; do [ "$(awk -v k=$k '$1==k{gsub("\r",""); print $2}' run/appdata/user.ltx | tail -1)" = 0 ] || { echo "REFUSED: $k is not 0 in run/appdata/user.ltx"; return 1; }; done; }
cfg0 || exit 2
[ "$(sha256sum run/bin/Stance14.exe | cut -c1-8)" = b4ce9c77 ] || { echo "REFUSED: Stance14.exe hash differs"; exit 2; }
WHAT="headless Xvfb GAMMA A-Life equivalence series (6 runs)"      # the text det_pair.sh claims with: its claim is a renewal
"$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && \
      "$T/quiet-required.sh" claim $W 60 "$WHAT" 2>&1 | grep -q '^claimed'; do sleep 60; done
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
trap '"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1' EXIT
cfg0 > "$O" 2>&1 || { cat "$O"; exit 2; }
{ echo "$(date '+%F %T') slice granted, load $(cut -d' ' -f1-3 /proc/loadavg)"
  # Run preflight: when checkpoint 1 of the FIRST run is written, its counter file must be there and the engine's own
  # command line must carry every flag; if not, the series is stopped at once (group TERM: the runs' exit traps clean up).
  L=run/appdata/logs
  DET_EXTRA="$EXTRA" setsid tools/det_pair.sh 60 3 Stance14.exe:s4c_s1 SimTick17.exe:s4c_o1 Stance14.exe:s4c_s2 SimTick17.exe:s4c_o2 Stance14.exe:s4c_s3 SimTick17.exe:s4c_o3 & DP=$!
  ( until grep -qa "^#actor" $L/alife_dump_s4c_s1_1.txt 2>/dev/null || [ -d out/det_s4c_o1 ] || ! kill -0 $DP 2>/dev/null; do sleep 2; done
    sleep 6; XL=$L/xray_$(whoami).log; [ -d out/det_s4c_o1 ] && XL=out/det_s4c_s1/xray.log
    if { [ -s $L/det_alife_counts_s4c_s1_1.txt ] || [ -s out/det_s4c_s1/det_alife_counts_s4c_s1_1.txt ]; } && grep -a "command line" "$XL" | grep -qaF -- "$EXTRA"; then echo "PREFLIGHT OK: counter file present, command line carries [$EXTRA]"
    else echo "PREFLIGHT FAIL: counter file of checkpoint 1 missing or the flags are not on the engine's command line; series stopped"; kill -TERM -- -$DP 2>/dev/null; fi ) & PF=$!
  wait $DP; echo "det_pair rc=$?"; wait $PF
  grep -q "^PREFLIGHT FAIL" "$O" && { echo "$(date '+%F %T') end (preflight abort)"; exit 4; }
  echo "$(date '+%F %T') end"; } > "$O" 2>&1
grep -a "PREFLIGHT\|^rc=\|det_pair rc\|end" "$O"
