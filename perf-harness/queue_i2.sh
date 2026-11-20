#!/bin/bash
# queue_i2.sh [--gate-only] "T0" -- waiter for the inert boot i2 (Overseer order d83ae9, 2026-11-20).
# Gate reads the quiet-claim AUDIT LOG only (no process names): open only after, both later than T0,
#   votv (by=votv, lane A) has had its claim "what=pair bh2 ..." GRANTED and then RELEASED, and
#   gammavr has had its claim containing "7c attempt 5" GRANTED and then RELEASED.
# After the gate: check the fingerprint against fc2's, then run the series driver (it joins the WANT queue and
# claims; refused -> retry every 120 s, staying queued). Gives up 6 h after start and withdraws from the queue.
set -u; P=~/Projects/StalkerGameEngine; cd "$P"; T="$HOME/.claude/overseer/tools"; L="$HOME/.claude/overseer/quiet-audit.log"
GATEONLY=0; [ "${1:-}" = "--gate-only" ] && { GATEONLY=1; shift; }
T0="${1:?need T0}"; START=$(date +%s); GIVEUP=$((START + 6*3600))
gate() { awk -v t0="$T0" '
  function ts() { return $1 " " $2 }
  ts() < t0 { next }
  ($3=="CLAIM"||$3=="CLAIM2") && $4=="by=votv"    && index($0, "what=pair bh2 ") && !vg { vg=ts(); next }
  ($3=="CLAIM"||$3=="CLAIM2") && $4=="by=gammavr" && index($0, "7c attempt 5") && !gg { gg=ts(); next }
  vg && !vr && $3 ~ /^RELEASE[0-9]*$/ && $4=="by=votv"    { vr=ts() }
  gg && !gr && $3 ~ /^RELEASE[0-9]*$/ && $4=="by=gammavr" { gr=ts() }
  END { s = "votv bh2: " (vg ? "granted " vg (vr ? ", released " vr : ", not yet released") : "not yet granted") \
            "; gammavr 7c attempt 5: " (gg ? "granted " gg (gr ? ", released " gr : ", not yet released") : "not yet granted")
        if (vr && gr) { print "open: " s; exit 0 } print "waiting: " s " (since " t0 ")"; exit 1 }' "$L"; }
if [ $GATEONLY = 1 ]; then gate; exit $?; fi
note() { timeout 60 "$T/notify-overseer.sh" engineperf - <<<"$1" >/dev/null 2>&1; }
last=0
until g=$(gate); do now=$(date +%s)
  [ $now -ge $GIVEUP ] && { echo "$(date +%T) GIVE UP after 6 h: $g"; note "engineperf: i2 waiter GAVE UP after 6 h: $g. Nothing booted, not in the queue."; exit 5; }
  [ $((now - last)) -ge 1800 ] && { echo "$(date +%T) status: $g"; last=$now; }
  sleep 30
done
echo "$(date +%T) gate $g"
tools/coop_hash.sh > out/i2_hashes_before.txt
cmp out/i2_hashes_before.txt out/coop_fc2/hashes_after.txt || { echo "FINGERPRINT DIFFERS from fc2; i2 NOT started"; note "engineperf: i2 NOT started: scratch fingerprint differs from fc2's. Nothing booted."; exit 6; }
echo "fingerprint equals fc2's"
while :; do
  [ $(date +%s) -ge $GIVEUP ] && { "$T/quiet-required.sh" withdraw engineperf | tail -1; echo "$(date +%T) GIVE UP after 6 h: gate open, no slot"; note "engineperf: i2 waiter GAVE UP after 6 h: gate opened but no exclusive slot. Nothing booted; withdrawn from the queue."; exit 5; }
  ( sleep 100; [ -s out/coop_i2/conditions.txt ] && grep -q "^start" out/coop_i2/conditions.txt && [ $(( $(date +%s) - $(stat -c %Y out/coop_i2/conditions.txt) )) -lt 200 ] && note "engineperf: i2 (inert boot) STARTED $(head -1 out/coop_i2/conditions.txt | cut -d' ' -f2-3) under my EXCLUSIVE claim, after: $g" ) &
  tools/coop_series.sh i2 -fp_inert2; rc=$?; echo "$(date +%T) === i2 rc=$rc"
  [ $rc = 3 ] || break
  wait; sleep 120
done
wait; "$T/quiet-required.sh" withdraw engineperf 2>&1 | tail -1
[ $rc = 0 ] || note "engineperf: i2 ended rc=$rc (4 = trace gate failed, 1 = server not ready). See my next report."
exit $rc
