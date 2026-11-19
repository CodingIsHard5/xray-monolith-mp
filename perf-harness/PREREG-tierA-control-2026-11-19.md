# Pre-registration: tier (a) check and its must-fail control (2026-11-19)

Written and committed BEFORE the control runs. Ruling by the Overseer 2026-11-19 after the growth result.

## Tier (a) check (adopted)
- Run: serial det_mode, save `bench1` (Rostok, player stationary), headless, dumps at 25 s / 300 s / 900 s.
- PASS = at every dump: every OFFLINE object identical in all fields; every faction-relation line and the actor
  line identical; no object present in one run only that is offline there.
- Not part of tier (a): online objects (fields and positions), objects present in one run only that are online
  there, and the `#objects` count line (it follows online prop items). Those are judged statistically, like tier (b).
- Tool: `perf-harness/det_growth.py --tier-a A B` (exit 0 = PASS), as committed here.
- Reference-vs-reference: the four existing runs g1a, g1b, g2a, g2b (RefDet12, CI 44510900390) PASS in every
  pairing checked (g1a/g1b, g2a/g2b, g1a/g2b) at all three dumps.
- Stated limit: holds for a stationary player. A moving player switches objects online/offline, which carries
  online drift into offline objects; tier (a) does not cover that.

## Control (must fail)
Purpose: show that the OFFLINE check on its own catches a small real change to offline A-Life.
Same exe (RefDet12), same mode, same dump times. Perturbation is in the test script only
(`zz_eperf_dump.script`, `-det_perturb`), applied once at 60 s of engine time:
- C1 `-det_perturb skip`: the first update of an OFFLINE squad after 60 s is skipped once.
- C2 `-det_perturb draw`: one extra `math.random()` call at 60 s (the Lua random sequence shifts by one draw).
One run each (`c1`, `c2`), each compared against reference run g1a with `det_growth.py --tier-a`.

## Rules fixed now
1. The control counts as CAUGHT if tier (a) reports FAIL at the 300 s or 900 s dump, and PASS at the 25 s dump
   (before the perturbation; a FAIL there would mean the run differs for another reason and the control is void).
2. The engine log must contain the `* det_perturb` line proving the perturbation was applied; otherwise void.
3. Tier (a) is usable for perf work only if at least one of C1, C2 is CAUGHT.
4. If one of them is NOT caught, that is reported as a measured blind spot of tier (a) (what size of change it
   cannot see within 15 min), not explained away. If neither is caught, tier (a) is not adopted and I report.
5. No change to the tool or the dump format between this commit and the control runs.
