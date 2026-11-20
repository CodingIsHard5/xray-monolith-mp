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
