# Pre-registration: tier (a) = exact counters + state diff (2026-11-19)

Written and committed before the first run with counters. Ruling by the Overseer 2026-11-19 (option A approved; 2 builds).
Reason: the dose series showed the 15-min end-state diff cannot see even 100 skipped offline squad updates.

## What is counted (det_mode only, test-only)
Engine (new console command `det_alife_counts`, file det_alife_counts_<tag>_<k>.txt):
- `upd <id> <count> <name> <online>`: number of A-Life scheduler update() calls per object since start.
- `registered` / `unregistered`: totals of server objects registered / unregistered.
Script (flag `-det_counters`, file det_counters_<tag>_<k>.txt):
- `stream alife|weather <draws> <state> <hash>`: number of draws, generator state, running hash of every draw and its arguments.
- `sq <id> <updates> <targets> <actchg> <deaths> <online>` per squad: script update calls that actually ran, target choices
  (get_squad_target), updates after which the squad's action or assigned target changed, member deaths (if the hook exists).
- `info register/unregister`: script callback totals.
Dumped at the same checkpoints as before (25 s, 300 s, 900 s). Compared by tools/counters_diff.py: `COUNTERS k=<k> C=<n>`.
EXACT items (counted in C): both `stream` lines; `upd` of every object offline in both runs; `sq` of every squad offline in both.
INFORMATIONAL, declared now (not in C): `registered`/`unregistered` totals and `info` lines, because they include objects
spawned by online play (items dropped or created near the player), and online play is known not to repeat; counters of
objects online in either run. Whether the informational ones matched is reported anyway.

## Runs (RefDet build with the counters, bench1 save, stationary player, serial det_mode)
All with `-det_times 25,300,900 -det_save -det_lua_streams -det_counters`:
c0a, c0b, c0c: references. cs1: `-det_perturb skip -det_perturb_n 1` (skip one offline squad update after 60 s).
cad: `-det_perturb adraw` (one extra draw on the A-Life stream at 60 s).

## Adoption (all four needed)
1. References: C = 0 for all three pairs at all three checkpoints.
2. cs1: C > 0 against EVERY reference at 300 s and at 900 s, the difference including the `sq` update count of the skipped
   squad; C = 0 at 25 s; the log has the "skipped update 1 of 1" line. (The skip is done in script, so the engine `upd` count
   still counts the call; the script `sq` count is the one that must show it. Stated now.)
3. cad: the `stream alife` line differs against EVERY reference at 300 s and 900 s; C = 0 at 25 s; log has the adraw line.
4. State diff (tools/tier_a.py) for the three new reference pairs no worse than the current floor 7 / 16 / 14.
If 1 fails: report which counters differ and how many; that is a finding about det_mode, not a reason to mask. No masking
of exact items after the fact.

## Limit, to be written into Rule One
Counters prove the same WORK ran in the same order with the same dice. They do not prove a changed function computes the same
result. A change that edits what an update computes also needs the state diff, plus a raised-time-factor run (so the state
actually moves) as a required extra for that class of change. That run is not built now.
