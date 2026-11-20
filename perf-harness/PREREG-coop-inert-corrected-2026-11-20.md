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
