# Tier (b) equivalence check - pre-registration, DRAFT for the Overseer's review

Written 2026-11-20 19:37 by EnginePerf. NOTHING here has been run or built. No run starts until the Overseer has ruled on this
text and the final version (without "DRAFT") is committed. Numbers marked [PROPOSED] are mine to be ruled on.

## 1. What it is for

Tier (a) (exact counters under the serial det_mode) proves that a change keeps the game's LOGIC identical. It says
nothing about a change to threading, because det_mode runs the engine serially. Tier (b) is the second check every
threading change must also pass: in NORMAL mode, with threads as shipped, the candidate's A-Life must be statistically
indistinguishable from stock's. It can show "no detectable difference at these statistics, this run count and this
detection limit". It can never show "no difference". The detection limit is measured, not assumed (section 6).

## 2. Runs

- Mode: normal play settings (no -det_mode, mt_* as shipped), headless, stock GAMMA scripts, same exe build options
  for reference and candidate except the change under test.
- Scenario S1 (required): save bench1, stationary player in Rostok, 15 min of engine time, state dumps at 300, 600 and
  900 s of engine time.
- Scenario S2 (required): the Cordon save used for the low-end bench, same length and dump times. Two levels, so a
  result does not rest on one map.
- Scenario S3 (OPEN, not in this registration): a MOVING player. Tier (a)'s third stated limit is that a stationary
  player hides online/offline switching. I have no scripted single-player movement yet; until it exists, any change
  that touches the online/offline switch or online AI cannot be passed by this check alone. Stated, not solved.
- Groups per scenario: R1 and R2 = reference exe, C = candidate exe, N = 5 runs each [PROPOSED]. Order interleaved
  R1, C, R2, R1, C, R2, ... so slow drift of the machine falls on all groups alike.
- Cost: 15 runs of about 17 min per scenario, about 4.5 h of the box per scenario, about 9 h for a first full check
  [PROPOSED; this is the main thing to rule on]. R1 and R2 are reusable for later candidates while the reference exe,
  the saves and the script tree (fingerprint by a hash tool like tools/coop_hash.sh) are unchanged, so a later
  candidate costs 5 runs per scenario.

## 3. Statistics (registered; all computed from the state dump at each of the three dump times)

For a pair of runs (x, y):
- P  population per smart terrain: sum over smarts of |count_x - count_y|.
- L  squads on a different level: number of squad ids present in both whose level differs.
- V  squads at a different game vertex: number of squad ids present in both, same level, different game vertex.
- M  object mix: sum over object sections of |count_x - count_y| (covers spawn items, mutants, stalkers).
- D  deaths: |deaths_x - deaths_y|, a death = a stalker or monster id alive in the 0 s dump and absent or dead later.
- U  A-Life work done: |passes_x - passes_y| / mean, the number of A-Life scheduler passes in the 15 min (catches a
     threading change that starves or speeds the A-Life scheduler).
The noise floor of a statistic is the set of its 25 R1 x R2 pair values; the candidate's set is its 25 C x R1 values.

## 4. Decision rule (per scenario, per statistic, at the 900 s dump; 300 and 600 s are reported, not scored)

- PASS for a statistic: the median of C x R1 is not above the 90th percentile of R1 x R2, AND a two-sided permutation
  test on run labels (C against R1, distances to R2 as the yardstick) gives p > 0.05 [PROPOSED].
- The candidate PASSES tier (b) only if every statistic passes in both scenarios AND all hard invariants hold (5).
- One statistic failing = FAIL. No re-scoring, no dropping of a statistic after the fact, no extra runs "to see".
  A run excluded as disturbed (rule in section 8) is re-run once; it is never averaged in.

## 5. Hard invariants (exact, every candidate run; any breach = FAIL regardless of statistics)

- No crash, no engine assert, no access violation line in 15 min; plus ONE soak run of 2 h per scenario.
- No duplicate object id in any dump; no id that disappears without a matching release/death record.
- No Lua error line that does not also occur in the reference group's logs.
- Soak memory: resident size at 2 h not more than the reference soak's + 10 % [PROPOSED].

## 6. Controls (run BEFORE any candidate is judged; the check is not usable until they behave as stated)

- C0 must PASS (false-alarm check): a third reference group R3 scored exactly as a candidate. If stock fails against
  stock, the rule is too tight or the statistics too noisy; the check is then NOT usable and I report that.
- C1 must FAIL (the control Caden's rule demands): a deliberately wrong candidate. Stage 1: the existing test-only
  perturbation "skip offline squad updates", moved to normal mode, as a DOSE series (skip 1, 10, 100, 1000 updates).
  The smallest dose that FAILS is the measured detection limit, and it is quoted with every later verdict.
  If even the largest dose passes, the check cannot see lost A-Life work and is NOT usable.
- C1b (later, with the first real threading change): that change with its locking removed on purpose. A real race is
  the right control, but it does not exist until there is threaded code to break; C1 stands in until then.
- C2 speed control (decides which statistics may be used at all): reference pinned to 4 cores against reference
  unpinned. Stock's object scheduler stops on a wall-clock budget, so speed alone changes which objects update. Any
  statistic that FAILS on C2 measures machine speed, not correctness; it is struck from the rule BEFORE any candidate
  is judged, and the struck list is published. If fewer than three statistics survive, the check is NOT usable.

## 7. What has to be built first (test overlay and tools only; no engine or game behaviour change)

1. A normal-mode switch for the dump overlay (today it is active only with -det_mode): dump at the three times, quit.
2. tools/tier_b.py: the six statistics, the pair tables, the permutation test, the verdict lines.
3. Normal-mode version of the "skip N offline squad updates" perturbation for C1.
4. A fingerprint tool for the single-player run tree (exe, saves, scripts).
Each is checked on existing dumps where possible before any box time is asked for.

## 8. Run rules (unchanged from the standing measurement rules)

One run at a time under an EXCLUSIVE claim held for the whole process lifetime; headless only; cores 0-1 free; no run
while a protected title or Roblox is up; load average logged at start and end of every run. A run is DISTURBED, and is
excluded and re-run once, if its 1-minute load at start is 2 or more, or another worker's claim overlapped it.

## 9. What I expect (stated before any run, so it can be wrong)

- C0 passes. C1 fails from dose 100 upward and passes at dose 1 (a single skipped update is inside stock's own noise).
- C2 strikes V (exact vertex) and possibly U; P, L, M and D survive.
- If my C2 guess is wrong in the other direction (everything is struck), tier (b) as written does not work on this
  engine, and the fallback is the fixed-tick decision, which is one of Caden's four open choices.

## 10. Questions for the Overseer

1. Box time: about 9 h for the first full check plus about 6 h for the controls. Acceptable, and in what slices?
2. N = 5 per group, the 90th-percentile bar and p > 0.05: accept, or tighten?
3. May the four build items in section 7 start now (desk and script work, no boots)?
