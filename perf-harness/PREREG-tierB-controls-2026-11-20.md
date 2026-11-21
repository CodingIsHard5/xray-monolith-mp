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

## Approval and arming (2026-11-20 19:48)

APPROVED as registered by the Overseer on 2026-11-20, including the 1 % bar; first slice 01:00 on 2026-11-21.
Fixed before slice 1 and committed with this note:
- run order: tools/tb_order.txt (42 lines: smoke; then five cycles of r, d10, r, d100, r, d1000, s, h; then walk);
- scheduler: tools/tb_slice.sh (wall-clock gate 01:00-08:00, slices of at most 3 h, EXCLUSIVE through WANT, title and
  Roblox checks before every run, stop on any failed run or failed smoke field check, notices to the Overseer);
- reference exe: run/bin/RefDet14.exe (the tier (a) reference build), save bench1; runner priority nice 19, idle I/O;
- SPEED runs use the low-end bench settings unchanged (cores 0-3, 8 GB, reported VRAM 2,048 MB), so they change more
  than speed alone; a statistic struck by SPEED is therefore "sensitive to the low-end settings", stated as such;
- fingerprint at arming (tools/tb_hash.sh RefDet14.exe bench1): exe bc0426a10810f461, scripts d2b14bc4bfbf01ea,
  configs a1c31bfb1d88f4ab, save d4a4cb0f30c90d39, overlay 5f96c97bf40ae93f; recorded again at every slice.

## Note (2026-11-21 01:05:26 EST): checker defect found at the smoke; no rule or threshold changed

- Slice 1 opened 01:00:20 on 2026-11-21. The smoke run finished clean (rc 0, 01:03:56). The scheduler's field check
  (smoke_ok in tb_slice.sh) then reported a failure and stopped the slice, as registered.
- Cause: a defect in the checker, not in the run. The engine (under wine) writes CRLF line ends, so the field read
  "382\r" and the shell integer test errored. Found in the same sweep: the "squads without a level" count read
  field 3 (squad section) instead of field 4 (level), so that guard could never have fired.
- Fix (checker only): every smoke field is now read from a CR-stripped copy; the level guard reads field 4; the
  Python parsers in tier_b.py strip CR explicitly (text mode already did). tb_hash only hashes bytes; tb_run.sh and
  the other greps in tb_slice.sh read files written by bash (LF). New self-test tools/selftest/tb_smoke_selftest.sh:
  a CRLF fixture must be green; witness W1 (CR strip removed) must be red with "integer expression expected";
  witness W2 (one squad with level "-") must be red. All four checks passed at 2026-11-21 01:05:26 EST.
- Smoke judged: PASSED ON RE-READ AFTER A CHECKER FIX (Overseer ruling, 01:04, verified d23cec937). Values from
  out/tb_smoke/tb_extra_smoke_3.txt: passes 382, squads 575, squads without a level 0 (on field 4), stalkers 806,
  monsters 329, smarts 477; every registered threshold met. No threshold, rule, run order or statistic changed.

## Note (2026-11-21 03:43:14 EST): between-runs settle wait (prospective; Overseer ruling)

- Slice 1 (01:05:40-03:41:46, 2026-11-21): 10 runs, all rc 0; 9 flagged DISTURBED (1-minute load at start 2.10-3.75,
  heavy_others empty every time). Each flagged run started about 2 s after the previous engine exited, so the load
  was the tail of my own previous run: the scheduler settled the load only at slice start. Only r01 (load 1.08) is clean.
- Those 9 (d10_01, r02, d100_01, r03, d1000_01, s01, h01, r04, d10_02) STAY EXCLUDED: their data is kept, unscored,
  and will not be re-scored. Their _rr re-runs stand as queued.
- From slice 2 on: before EVERY run the scheduler waits until the 1-minute load is below 2 and no other process is at
  >= 100 % CPU, checking every 15 s, capped at 5 min; past the cap the run starts and tb_run.sh marks it DISTURBED as
  before. The wait length is logged ("settle wait=..s"). The wait comes before the slice-time and 04:00 guards, so it
  counts against them. The threshold stays at 2; no rule, threshold or statistic changed.
- Self-test tools/selftest/tb_settle_selftest.sh: fake load 3.0 then 1.5 -> waits, then starts at 1.5; witness W1
  (wait removed) starts at 3.0; witness W2 (heavy process present) waits to the cap; settle precedes both guards.
  All passed at 2026-11-21 03:43:14 EST.

## Note (2026-11-21 18:09:21 EST): slices at any hour (Overseer; Caden away six days, "use the PC whenever")

- The 01:00-08:00 window is lifted (TB_ANYTIME=1). Each slice: at most 3 h under one EXCLUSIVE claim through WANT.
  After each slice: release, then wait 25 min or until another worker holds the box, whichever is first, before the
  next WANT. Unchanged: settle wait before every run, 21-min run timeout, protected-title and Roblox checks.
  Self-test cases added for the gap. Scheduling only; no rule, threshold or statistic changed.
