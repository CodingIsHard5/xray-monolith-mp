# Tier (b) - pre-registration of the CONTROL runs (instrument validation). FOR APPROVAL; nothing run yet

Written 2026-11-20 19:46 by EnginePerf. Supersedes the control part of PREREG-tierB-DRAFT-2026-11-20.md after the Overseer's
rulings of 2026-11-20 (controls first; overnight slices; false-fail rate measured; detection limit as a number).
No candidate is judged by anything in this document. The tools are built and committed (57c6e382a) but have never run
in the engine; section 7 lists what is unverified.

## 1. Runs (scenario S1 only: save bench1, stationary player, Rostok)

Every run: normal mode, `tools/tb_run.sh <tag> bench1` (headless, pinned as the bench runs are, memory-capped),
dumps at 0, 300, 600, 900 s of engine time, about 17 min of wall time. Lowest priority (nice 19, idle I/O class) for
every run alike.
- Smoke (1 run, 3 min, dumps at 60/120/180 s): only to check that the new dump fields are filled. Not data.
- REF: 15 reference runs (r01..r15).
- HOLD: 5 more reference runs (h01..h05), not looked at until the band is frozen.
- DOSE: the lost-update perturbation `-tb_skip N -tb_skip_at 60` at N = 10, 100, 1000; 5 runs each (15 runs).
- SPEED: 5 reference runs pinned to 4 cores (the low-end bench pinning).
- WALK smoke (1 run): `-tb_walk 60`, only to see whether the scripted walk keeps the player alive for 15 min.
Total 42 runs, about 12 h of the box. Order: smoke, then REF/DOSE/SPEED/HOLD interleaved in a fixed, committed list
(tools/tb_order.txt, written before slice 1) so that machine drift falls on all groups alike.
Scenario S2 (Cordon) and any candidate check are NOT part of this registration.

## 2. Slices and machine rules (Overseer ruling)

Overnight only, 01:00-08:00, slices of at most 3 h, a new run is started only if it can finish inside the slice and
before 08:00. EXCLUSIVE claim through the WANT queue, held only while a run is alive plus the gap to the next run of
the same slice; released at the end of every slice. Before every run: protected-title check and Roblox check; if
either is up, the slice ends there. One run at a time. Load average logged at start and end of each run.
A run is DISTURBED (excluded, re-run once at the end of the list, never averaged in) if its 1-minute load at start
is 2 or more or another worker's claim overlapped it. About 10 runs fit a slice, so about 5 slices over 3 nights.

## 3. Statistics

P, L, V, M, D, U exactly as defined in the draft (section 3), computed by `tools/tier_b.py` at the 900 s dump.

## 4. Step 1 - false-fail rate of the rule on stock against stock (sets the band)

`tier_b.py null --ref r01..r15 --n 5`: disjoint splits of the 15 REF runs into pseudo R1, R2, C; the FULL rule
(every statistic) applied to each split; false-fail rate = share of splits that fail.
- The band is the SMALLEST of 80, 85, 90, 95, 97.5, 99, 100 (percentile of R1 x R2) whose overall false-fail rate is
  5 % or lower. It is then frozen and written into this file before HOLD or DOSE are scored.
- If no band reaches 5 %: drop the single statistic with the highest own false-fail rate, once, and repeat. If it
  still fails, or fewer than four statistics would remain, the rule does NOT work at N = 5; I report that and
  propose a larger N. Nothing is scored further.
- Out-of-sample check, because the band was chosen on the same 15 runs: HOLD is scored ONCE as a candidate against
  r01-r05 / r06-r10 with the frozen band. It must PASS. If it fails, the instrument is not validated.

## 5. Step 2 - the control that must fail, and the detection limit as a number

Each DOSE group is scored as a candidate with the frozen band (R1 = r01-r05, R2 = r06-r10).
- Dose is reported in two units from the run logs: lost offline squad updates per minute while skipping
  (`tier_b.py lost-rate`), and lost updates as a share of all offline squad updates in the 15 min (from U's pass
  count of the reference runs).
- Detection limit = the smallest dose whose group FAILS. Quoted with every later tier (b) verdict.
- Required for the instrument to be usable [PROPOSED threshold, to be ruled on]: N = 1000 fails, and the detection
  limit corresponds to a loss of 1 % or less of the offline squad updates of the run. If the limit is coarser than
  that, I report the number and the Overseer decides whether it is good enough; I do not decide it after the fact.
- Monotonic sanity: if a larger dose passes while a smaller one fails, the result is reported as NOT INTERPRETABLE.

## 6. Step 3 - speed control

SPEED is scored as a candidate with the frozen band. Any statistic that FAILS here measures machine speed, not
correctness, and is struck from the rule for all later candidate checks; the struck list is published. After a
strike the false-fail rate (section 4) is recomputed for the remaining statistics; the band is NOT re-chosen.
If fewer than three statistics survive, tier (b) as designed is not usable on this engine and I say so.

## 7. Unverified until the smoke run (any of these failing stops the slice and is reported; it is not worked around)

- the level-name call that L and V need; the stalker/monster kind column; the pass counter in normal mode;
- whether the run reaches its first update without a key press; the frame hitch of the extra dump sweep;
- `tb_run.sh` end to end; `tb_hash.sh` on the real run tree (the fingerprint is recorded with every slice and must
  be identical across all 42 runs, or the differing runs are void).
- The scripted walk may kill or strand the player. If the WALK smoke does not end with a living player and at least
  10 completed steps, the moving-player scenario stays a STATED LIMIT of tier (b).

## 8. Predictions (so they can be wrong)

- Band lands at 95 or 97.5. HOLD passes.
- Dose 1000 fails, dose 10 passes, dose 100 undecided; detection limit between 0.1 % and 2 % of offline squad updates.
- SPEED strikes V and maybe U.
- Chance that one of the unverified items in section 7 stops slice 1: about one in three.

## 9. First real candidate (asked by the Overseer)

None exists, and none may be built before Caden rules on the plan. By the plan's staged route (section 5) the first
candidate would be stage A1: the co-op branch and this branch on one base (the dev-mt worker model merged with
renderless-r0; trial merge done in a scratch clone, never pushed). Its tier (b) check would be A1 against stock.
