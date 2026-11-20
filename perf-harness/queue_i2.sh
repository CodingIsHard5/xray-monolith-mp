#!/bin/bash
# queue_i2.sh [--gate-only] "T0" -- waiter for the inert boot i2 (Overseer order d83ae9, 2026-11-20).
# Gate reads the quiet-claim AUDIT LOG only (no process names): open only after, both later than T0,
#   votv (by=votv, lane A) has had its claim "what=pair bh2 ..." GRANTED and then RELEASED, and
#   gammavr has had its claim containing "7c attempt 5" GRANTED and then RELEASED.
# After the gate: check the fingerprint against fc2's, then run the series driver (it joins the WANT queue and
# claims; refused -> retry every 120 s, staying queued). Gives up 6 h after start and withdraws from the queue.
set -u; P=~/Projects/StalkerGameEngine; cd "$P"; T="$HOME/.claude/overseer/tools"; L="$HOME/.claude/overseer/quiet-audit.log"
GATEONLY=0; [ "${1:-}" = "--gate-only" ] && { GATEONLY=1; shift; }
T0="${1:?need T0}"; START=$(date +%s); GIVEUP=$((START + ${GIVEUP_H:-6}*3600))
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
  [ $now -ge $GIVEUP ] && { echo "$(date +%T) GIVE UP: $g"; note "engineperf: i2 waiter GAVE UP after 6 h: $g. Nothing booted, not in the queue."; exit 5; }
  [ $((now - last)) -ge 1800 ] && { echo "$(date +%T) status: $g"; last=$now; }
  sleep 30
done
echo "$(date +%T) gate $g"
tools/coop_hash.sh > out/i2_hashes_before.txt
cmp out/i2_hashes_before.txt out/coop_fc2/hashes_after.txt || { echo "FINGERPRINT DIFFERS from fc2; i2 NOT started"; note "engineperf: i2 NOT started: scratch fingerprint differs from fc2's. Nothing booted."; exit 6; }
echo "fingerprint equals fc2's"
# Order (Overseer, 18:5x): queue and CLAIM FIRST, so the claim system drains the box; then wait INSIDE the claim for the
# 1-min load to be under 2 for 3 straight minutes (7 readings 30 s apart); if that has not happened 20 min after the
# grant, release and report. The claim text is exactly the driver's own, so the driver's claim is a same-text renewal.
WHAT="EXCLUSIVE: co-op server + up to 8 headless clients, series i2 (scratch copy, port 5461)"
"$T/quiet-required.sh" want engineperf EXCLUSIVE 2>&1 | tail -1
ok=0
while [ $(date +%s) -lt $GIVEUP ]; do
  o=$("$T/quiet-required.sh" claim engineperf 45 "$WHAT" 2>&1); echo "$o" | grep -q "^claimed" && { ok=1; break; }
  now=$(date +%s); [ $((now - last)) -ge 900 ] && { echo "$(date +%T) status: queued, claim refused: $(echo "$o" | head -1 | cut -c1-140)"; last=$now; }
  sleep 60
done
[ $ok = 1 ] || { "$T/quiet-required.sh" withdraw engineperf | tail -1; echo "$(date +%T) GIVE UP: no exclusive slot"; note "engineperf: i2 waiter GAVE UP: gate open but no exclusive slot. Nothing booted; withdrawn from the queue."; exit 5; }
GRANT=$(date +%s); echo "$(date +%T) claim granted, load $(cut -d' ' -f1-3 /proc/loadavg); waiting inside the claim for load under 2 for 3 min"
n=0; while [ $n -lt 7 ] && [ $(( $(date +%s) - GRANT )) -lt 1200 ]; do l=$(cut -d' ' -f1 /proc/loadavg)
  if awk -v l="$l" 'BEGIN{exit !(l<2)}'; then n=$((n+1)); else n=0; fi
  echo "$(date +%T) load $l ($n of 7)"; [ $n -lt 7 ] && sleep 30; done
if [ $n -lt 7 ]; then "$T/quiet-required.sh" release engineperf 2>&1 | tail -1
  echo "$(date +%T) load did not settle within 20 min of the grant (now $(cut -d' ' -f1-3 /proc/loadavg)); released, i2 NOT started"
  note "engineperf: i2 NOT started: held the box 20 min but the 1-min load never stayed under 2 for 3 min (now $(cut -d' ' -f1-3 /proc/loadavg)). Released; not queued."; exit 7; fi
echo "$(date +%T) load settled ($(cut -d' ' -f1-3 /proc/loadavg)); starting i2"
( sleep 100; [ -s out/coop_i2/conditions.txt ] && grep -q "^start" out/coop_i2/conditions.txt && [ $(( $(date +%s) - $(stat -c %Y out/coop_i2/conditions.txt) )) -lt 200 ] && note "engineperf: i2 (inert boot) STARTED $(head -1 out/coop_i2/conditions.txt | cut -d' ' -f2-3) under my EXCLUSIVE claim (granted $(date -d @$GRANT +%T), load settled first), after: $g" ) &
tools/coop_series.sh i2 -fp_inert2; rc=$?; echo "$(date +%T) === i2 rc=$rc"
[ $rc = 3 ] && "$T/quiet-required.sh" release engineperf 2>&1 | tail -1
wait; "$T/quiet-required.sh" withdraw engineperf 2>&1 | tail -1
[ $rc = 0 ] || note "engineperf: i2 ended rc=$rc (4 = trace gate failed, 1 = server not ready). See my next report."
exit $rc
