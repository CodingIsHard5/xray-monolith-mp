# Pre-registration 2026-11-20 15:45: corrected inert arm (a live player whose scripts run elsewhere), 1 boot
Approved by the Overseer 2026-11-20. Committed before the boot. Measurement only; same scratch server, binaries, flags,
claim rules, windows and join schedule as series A/B (0, 0, 1, 2, 4, check, 8 players; n = 120 one-second samples each).
What changed against the VOID inert arm: flag `-fp_inert2`. For every joined player's actor the wrapper runs the STOCK
script update ONCE (its first call), inside a protected call so the HUD-mod error at the end of that update does not retire
the binder, and from the second call on runs an empty function. The host's actor (id 0) is untouched, as in the full arm.
So the stock first update does its server-wide housekeeping (the squad-search flag, the timed reset of the A-Life rate),
and afterwards the server carries a live player binder with no script work: the thin-player case.
Sampled process identity (as ruled): command line with the scratch path and port 5461, launcher-recorded pid, resident
size; thread = tid equal to pid; thread time <= process time in every sample; else void.

CONTROL A (must agree with a known quantity): the A-Life rate is back at its stock value after each join.
  Limit stated now: this build has NO way to read the value (the script API has only set_objects_per_update,
  alife_simulator_script.cpp:697; the console command al_objects_per_update only writes, console_commands.cpp:765-777), and
  adding a reader would be an engine change. So the two stock WRITERS are logged with the game clock: the write of 65,534
  in actor_binder:reinit ("RATE reinit") and the stock reset to 20 when its timed event executes ("RATE reset ran").
  Control A is MET if, before every measured window, the last RATE line in the server log is a "reset ran" (so the last
  write was the stock 20), and every joined actor's reinit is followed by a reset. If a window starts with a reinit as
  the last write, that window is void. This shows the last value WRITTEN, not a read-back; said so in the result.
CONTROL B (must show about zero): from the second call on, a joined player's update costs under 0.01 ms per call, and it
  is called about once per server frame (70-95 calls per second per actor). Otherwise the arm is void.
Also logged: the network thread (xrnet-enet-sv) by name, per window.

PREDICTIONS (numbers fixed now). Reference per window = boot z2 (no clients): 101.0, 106.1, 104.3, 99.1, 97.0, 96.8.
Full arm (mean of A, B): 101.0, 103.2, 103.4, 68.1, 102.0, 90.7.
- If the desk reading is right (the void arm's +75-105 was the A-Life sweep): M here is within 25 ms per second of the
  full arm in every window with players, the server holds 88-92 frames per second, and its worst frame per window stays
  under 20 ms. READING CONFIRMED.
- If M is 60 or more above the full arm at 1 player, or worst frames are 30 ms or more: READING KILLED (a live binder
  costs by itself, or something else differs). Between: PARTIAL, reported with the numbers.
- Network thread: 8, 8, 10, 11, 14, 20 ms per second (+/- 2) in the six windows, replicating about 1.5 per player.
- Cost of a live scriptless player on the main thread: I expect no resolvable rise over the full arm (under 4 ms per
  second per player). The 2-player window may again sit about 30 below the reference; that is reported, not explained.
What it cannot give: a capacity number (8 players, one spot, no player scripts anywhere), or the cost of scripts that
must stay on the server in a thin-player design.
Stop rules as before; one retry at most. Queued behind the other workers through the claim queue (want EXCLUSIVE); no
retry loop racing for the slot.

## Amendment 2026-11-20 15:48, before the boot
A desk read found that the server runs an ungated test schedule (mp_spike_anchor.script) from the first join: it shrinks
the A-Life online radius and moves anchors at fixed game times, changing main-thread cost by up to 80 ms per second inside
single windows. So window means are not properties of a player count. This boot keeps the schedule (same gamedata as the
full arm) so that the registered comparison, corrected-inert against the full arm window by window, stays like for like.
Added check: the schedule's log lines (village_set, base_added, village_cleared, village_readded) must fall within 5 s of
series A's on the server clock; otherwise the comparison is reported as misaligned and not scored. The sentence "cost of a
live scriptless player on the main thread" is read ONLY as the difference to the full arm; no absolute per-player figure,
and the z2 "reference" row in the predictions is withdrawn (that boot's game clock was halted).

## Amendment 2, 2026-11-20 15:50: CLEAN DESIGN (Overseer ruling). Replaces the decision rule and predictions above. Before the boot.
Change to the setup: the three test-instrument links mp_spike.script, mp_spike_anchor.script, mp_spike_drive.script are
removed from MY scratch server's script folder only (1,015 -> 1,012 entries; lists in out/coop_scripts_before.txt and
_after.txt; nothing else references them; gamma_flat and the co-op worker's trees untouched). Clients keep the plain
gamma_flat gamedata (the instruments need the server's A-Life and do nothing on a client). Everything else as registered:
`-fp_inert2`, same join schedule and windows (0, 0, 1, 2, 4, check, 8 players; 30 s settle + 120 s, n = 120 each).
CONTROLS (all must hold or the boot is void for the reading):
- R, must agree: the A-Life online radius (alife():switch_distance(), logged every 5 s from a level-wide call, and at once
  on any change) keeps its boot value in every line, with no "CHANGED" line. Expected value 450 (the boot log of series A
  showed limits 405/495 m = 450 with the 0.1 factor); the value itself is reported, the control is "no step".
- A, must agree: before every window the last logged A-Life-rate write is the stock reset to 20 (writers logged; the
  value cannot be read back on this build, as stated above).
- B, must show about zero: joined players' update under 0.01 ms per call after the first, 70-95 calls per second each.
- Also must hold: no MP_ANCHOR line and no "trader-switch ... 180 m" line in the server log (the schedule is really gone).
- Identity of the sampled process and thread as ruled; thread time <= process time in every sample.
WHAT IS READ, and the rule (fixed now):
- The full arm (A, B) and the z/z2 boots are NOT comparable any more (schedule; halted clock). The only like-for-like
  figure I have for "clock running, stock radius, binders retired" is the first 60 s after the first join in A and B:
  about 135 ms per second (A: 134, 136, 130; B: 134, 144, 146 in 20 s blocks).
- READING CONFIRMED (the void arm's +75-105 was the A-Life sweep, a live scriptless binder is cheap): the means of windows
  3-6 (1, 2, 4, 8 players) all lie in 105-165 ms per second AND the server's worst frame per window stays under 20 ms.
- READING KILLED: window 3 is 195 or more, or worst frames are 30 ms or more.
- Otherwise PARTIAL, reported with the numbers.
- Stationarity, reported per window in 20 s blocks: a step of more than 30 ms per second between blocks inside a window is
  flagged "world not steady" (the test-schedule effect should be gone; if steps remain, the cause is something else).
- Main-thread growth with players: reported as (window 6 - window 3) / 7 per player, labelled "players and elapsed time
  together, not separated"; windows 1-2 (clock halted) are informational only. NOT a capacity number.
- Network thread by name: predicted 8, 8, 10, 11, 14, 20 ms per second (+/- 2) in the six windows.
Not answered by one boot: whether the live binder adds anything over retired binders under the same clean conditions;
that needs a clean full-arm boot beside this one (asked of the Overseer, not assumed).

## Amendment 3, 2026-11-20 15:51: paired clean boots (Overseer approved one clean FULL-arm boot). Before the first boot.
Two boots back to back on the same scratch tree (mp_spike instruments removed), full first:
- FC: tools/coop_series.sh fc            (no -fp_inert flag: joined players' binders retire at the first update, as in A/B)
- I2: tools/coop_series.sh i2 -fp_inert2 (stock first update inside a protected call, then empty)
Same windows and join schedule, each boot under its own EXCLUSIVE claim. Controls R (online radius, no step, no MP_ANCHOR
line) and A (last A-Life-rate write before every window is the stock reset) apply to BOTH boots; control B (empty update
under 0.01 ms, 70-95 calls per second) applies to I2; in FC every joined actor must show exactly 1 update call and a
"binder RETIRED" line. A boot failing its controls is void and the pair is not scored.
THE RULE NOW (replaces amendment 2's rule; read ARM AGAINST ARM ONLY): D(w) = I2(w) - FC(w), mean main-thread ms per
second, for windows 3-6 (1, 2, 4, 8 players). Known boot-to-boot spread with identical setups: up to 12 (A vs B), 9 (z vs z2).
- READING CONFIRMED (a live scriptless binder adds nothing resolvable; the void arm's +75-105 was the A-Life sweep):
  |D| <= 25 in all four windows AND I2's worst frame per window under 20 ms.
- READING KILLED: D >= 60 at window 3, or I2's worst frames 30 ms or more while FC's stay under 20.
- Otherwise PARTIAL, with the numbers.
Reported without a rule: both arms in 20 s blocks (steps over 30 flagged "world not steady"); the network thread by name
in both arms (predicted 8, 8, 10, 11, 14, 20 +/- 2); amendment 2's absolute band (105-165) as information only.
No capacity number is derived from any of this.

## Amendment 4 (2026-11-20 16:21) - first FC boot void (my trace bug); one retry of the pair

What happened. FC ran 16:02:43-16:20:20 clean (rc 0). I2 did not start: on FC's release the box was reserved for the
next worker in the queue (rc 3). No retry loop; I queued again.

FC of 16:02 is VOID by the rule as written: control A cannot be shown. The log has 9 "RATE reinit" lines and ZERO
"RATE reset queued / reset ran" lines. Cause is in my trace, found by reading it: the wrapper was assigned as a bare
`CreateTimeEvent = ...`, which on this engine lands in my script's own namespace, so stock bind_stalker never called it.
Whether the stock reset to 20 ran in that boot is therefore UNKNOWN. Control R was met (208 radius lines, all 450, no
CHANGED, no MP_ANCHOR; the one "trader-switch" line is the engine's own 405/495 m line, not the 180 m test line).
I have already seen FC's numbers (main thread 98, 97, 139, 179, 187, 246 ms/s; network 6.4, 6.6, 9.1, 11.2, 14.9, 23.9).
They are information only and are not scored; the rule below is unchanged by having seen them.

Changes, committed before the next boot:
1. Trace fix only: the wrapper is assigned to `_G.CreateTimeEvent`. Nothing else in the overlay changes.
2. FC's "exactly 1 update call per joined actor" is read from the log (one "first update of actor id N" line and one
   "binder RETIRED" line per actor), because the timer file is not written when the stock update raises.
3. One retry of the pair (standing stop rule: at most one retry): FC2 = `tools/coop_series.sh fc2`, then
   I2 = `tools/coop_series.sh i2 -fp_inert2`. I2 is queued the moment FC2 releases; the gap between the boots is
   whatever the queue gives and is reported. No third attempt without a new ruling.
4. Pre-stated: if the fixed trace shows that the reset is never queued or never runs (rate left at 65,534), control A
   is FAILED as written, the boot is void, the pair is not scored, and that is reported as a finding about this server.
Rule, windows, other controls: exactly as amendment 3.

## Amendment 5 (2026-11-20 16:22) - trace gate before the paired boots (Overseer condition)

The retry of the pair is approved on one condition: the fixed rate trace must prove itself first. The driver now gates
every boot: within 60 s of the host's first update the server log must show "RATE reinit (stock sets 65534)", then
"RATE reset queued", then "RATE reset ran (stock sets 20)". If not, the driver stops the server, releases the box and
exits 4; I report at once, I2 is not started, and (Overseer) that does not count as the retry being used.
The gate adds up to 60 s before window 1 in both arms alike. Nothing else changes; rule and controls as amendment 3/4.

## Amendment 6 (2026-11-20 16:42) - I2 runs alone, later; conditions for its pairing with FC2 (Overseer ruling)

FC2 (16:24:02-16:41:36) passed the trace gate and all its controls. I2 was refused the box on FC2's release and has not
run. Ruling: I2 runs ALONE when the Overseer gives me the box. Its pairing with FC2 is valid only if all of these hold:
1. Same scratch tree and binaries. Fingerprint by `tools/coop_hash.sh` (binaries; the 1,012 server scripts by name and
   content; gamedata names; overlay script; driver + launcher + sampler). FC2's fingerprint was taken after the boot, at
   2026-11-20 16:42; nothing in those paths has a modification time after FC2's start except the overlay copy the driver itself
   re-copies (content identical to the source, compared). Values:
     bin        e7b26092a4d3b8b4  files=9
     scripts    89452b0a6da13019  entries=1012
     gamedata   0401a8a83edc109b  (names to depth 2)
     overlay    41462a8c9314f287
     tools      5d86ea12a35e45dc
   The fingerprint is taken again immediately before I2 and must be byte-identical, or I2 does not start.
2. Load: the median of the 1-minute load average over the boot's seven logged points (start + end of each of the six
   windows; the only load points FC2 recorded) must be within +/- 1.5 of FC2's. FC2's seven points:
   2.23, 0.52, 0.26, 0.36, 0.49, 1.03, 1.15 -> median 0.52. So I2's median must be 2.02 or lower.
   If this fails, I2 is VOID for the pairing; I ask the Overseer; no automatic re-run.
3. Same windows, same trace gate, same controls (R, A, B, identity) as amendments 3-5.
4. The gap between FC2's end (16:41:36) and I2's start is reported.
Rule unchanged: D(w) = I2(w) - FC2(w) for windows 3-6, thresholds as amendment 3. No capacity number.

## Note (2026-11-20 16:44) - how I2 is started (Overseer order)

I2 is armed behind `tools/queue_i2.sh` (copy here), T0 = 2026-11-20 16:44:00. It reads the claim audit log only and
opens after votv (claim text containing "bh") and gammavr (claim text containing "7c") have each been granted and
released after T0. Then: fingerprint compared with FC2's (differs -> no boot), the unchanged driver joins the queue and
claims, 6 h give-up with withdrawal from the queue. Proven on live state at arming: "waiting: votv bh: not yet granted;
gammavr 7c: not yet granted". Measurement, rule and controls are untouched by this.

## Note (2026-11-20 18:03) - I2 attempt of 17:59 gave no data; re-armed with a load condition (Overseer ruling)

The server did not come up within 180 s at load 8.4 (another worker's whole-disk search); stopped, nothing sampled.
Ruling: this does not use the retry. Re-armed with the same audit-log gate (already open) plus: the boot is attempted
only after the 1-minute load has been under 2 for 3 straight minutes (7 readings 30 s apart). Give-up 3 h.
Amendment 6's pairing conditions, the rule and the controls are unchanged.
