# PREREG wu3 (REGISTERED 2026-11-27): does `-alife_warmup` remove the slow frames after a level load? (wu2 re-registered with re-runs)

**Status: REGISTERED 2026-11-27 by EnginePerf. No wu3 load before the supervisor's GO.** Built by an Opus sub-worker to
EnginePerf's brief; EnginePerf read the driver diff, ran the self-test itself and set the pins (section 10).

**claim class: EXCLUSIVE (verdict depends on frame timing).**

## 0. What this is
wu2 stopped on 2026-11-27 after 5 of 33 loads: 3 loads were marked not clean (another agent's desk test used one core)
and wu2 allowed no re-run (RESULT-wu2-2026-11-27.md, FAIL `stopped_series`). The supervisor ordered a new registration:
re-run a voided load, with a derived cap, and state what share of voids makes the series NO VERDICT rather than FAIL.

**Everything of wu2 holds except the following.** wu2 = `../wu2/PREREG-wu2-alife-warmup-2026-11-26.md`, sha256
`f19b50180be2f7edf7bc250bbd12ac8da73c2cf26004720b78dbe63949994c2f`. Unchanged: question, exe and its pin, every flag,
the save, the order pattern (1 prime + 16 ON + 16 OFF, ABBA x8), the window, every per-load check (L1-L5, the wu2
layout checks, the VOIDs and FAILs of wu2 4.3 except `stopped_series`), H1-H3 and their bounds, every wu2 constant except
MAX_CONFOUNDED, the probe before any scored load, display, the settle rule (its cap is changed, section 6), slice length, exit codes (0 PASS / 1 FAIL /
3 NO VERDICT / 2 refused).

Names: `wu3_run.sh`, `wu3_score.py`, `wu3_selftest.sh`, `wu3_order.txt` (wu2's order with tags `wu3_prime`,
`wu3_01_on` ... `wu3_32_on`), state folder `out/wu3_state`.

## 1. Changed: the re-run rule (replaces wu2 3.1 "No load is ever run twice" and wu2 4.3 `stopped_series`)
- A **scored** load that ended rc 0, passed its per-load check (`check=`) and whose bench VERDICT is not
  `VERDICT: clean` is run once more right away, as `<tag>_r1` (folder `out/<tag>_r1`).
- The re-run goes through the normal settle and every normal pre-load gate (slice time, protected title, Roblox,
  Caden's launcher, display, settings, folder new, exe pin, claim renewal, slice recheck). It gets its own `check=`.
- At most **one** re-run per order slot. If the re-run is not clean either, the slot is VOID. There is no third attempt.
- A load that crashed, timed out, had no capture or failed a check is **never** re-run. The series stops (fault), as
  in wu2.
- The prime is never re-run and does not count toward the budget. A not-clean prime is fine if its probe passes (as wu2).
- The driver reads the VERDICT with CR stripped (engine-side files may be CRLF).

## 2. New: the re-run budget R_MAX = 8 (derivation)
Inputs:
- Usable slice: 163 min = 9780 s after the grant (wu2 3.3: a load starts only with >= 900 + 120 s left of 3 h). COPIED.
- Load 138-216 s plus settle (wu2 3.3). COPIED.
- **MEASURED today** (wu2_series_log.txt, load start to next load start): 252, 168, 156, 214 s. Mean 197.5 s.
  After a clean load: 168, 156 s (mean 162). After a not-clean load: 252, 214 s (mean 233; the settle waited longer).
  First settle after the grant: 303 s. Bench run (start to rc line): 122-130 s.

Derivation. Load number k starts at 303 + (k - 1) x cycle seconds. The last of 33 + R loads must start by 9780 s.
So R <= floor((9780 - 303) / cycle) + 1 - 33.

| cycle used | largest R that fits | time to the last start with R = 8 | margin vs 9780 s |
|---|---|---|---|
| 197.5 s (measured mean) | 15 | 8203 s = 137 min | **1.19x (UNDER 2x)** |
| 216 s (wu2 upper per-load) | 11 | 8943 s = 149 min | **1.09x (UNDER 2x)** |
| 252 s (measured max) | 5 | 10383 s = 173 min | **0.94x: does not fit** |
| mixed: 162 s after clean, 233 s after not clean, 8 re-runs all clean | - | 7351 s = 123 min | **1.33x (UNDER 2x)** |
| mixed, 8 re-runs all not clean too | - | 7919 s = 132 min | **1.24x (UNDER 2x)** |

**R_MAX = 8** (the supervisor's proposal; the derivation allows 5 to 15 by cycle, so 8 stands). Every margin is under
2x. If the cycles run at the slow end the slice check stops the series first; that stop is class box, so the result is
NO VERDICT, not FAIL. A series with no re-run at all needs 110 min at the measured mean (1.48x).

When a not-clean scored load needs a re-run and 8 re-runs are already done, the driver stops (class box).

## 3. New: the stop record
On every exit after the state folder exists, the driver writes `out/wu3_state/stop.txt`, exactly one line ended by LF:
`class=<done|box|fault> reason=<text>`, or, for a box stop while a bench was live (fix round 1, B):
`class=box interrupted=<tag> reason=<text>` (the tag is the load whose bench was running; `interrupted=` appears only
with class=box, always before `reason=`).
- **What a TERM does to a live bench** (read from tools/s2_bench.sh, pinned 072af8ed, not edited): the folder
  out/<tag> is made at once (line 14) and conditions.txt gets its start line (line 29); the engine's stderr and the DXVK
  logs are written into the folder while it runs (lines 54-56). The only trap is `trap cleanup EXIT` (line 38;
  cleanup lines 32-37: `wineserver -k`, kill the window manager and Xvfb). Bash runs that EXIT trap on TERM (checked
  with a 3-line script: the trap ran, exit 143, the code after the loop did not). So lines 70-77 never run: no end line,
  no VERDICT line, no xray.log copy (line 76), and the perf_*.csv files stay in run/appdata/logs (line 77). A TERM
  leaves a **partial out/<tag>**. The driver's finish() also sends TERM to the bench and waits for it.
- **done**: the list finished.
- **box** (the box was not quiet or not ours): settle cap (10 min; wu2 had 5 min; now a box reason), re-run budget used,
  slice time used, slice too short for the next load, protected title, Roblox, Caden's launcher, claim renewal
  refused, cancelled (TERM/INT; with `interrupted=<tag>` when a bench was live).
- **fault** (something did not run as registered): crash / timeout / no capture (bench rc != 0), a failed per-load
  check, failed probe (wu2's "probe failed: nothing scored, nothing consumed" text and handling kept), state write
  failure, exe changed, settings not stock, display not free, an existing out folder, order copy refused.
- Before the state folder exists (preflight refusals) nothing is written: the folder must be new, so it is not made.
- If the record cannot be written (fix round 1, C), the driver still reaps the bench, withdraws and releases, but exits
  non-zero (rc 1 unless it was already non-zero) and puts "stop record NOT written" into its series.log line and the
  notify text. The scorer then gives FAIL `stop_record`.

## 4. New scorer rules (replace wu2 7.1 `stopped_series`)
Score command (one more argument):
`python3 -I wt-render-split/perf-harness/wu3/wu3_score.py root=out order=wt-render-split/perf-harness/wu3/wu3_order.txt stop=out/wu3_state/stop.txt out=out/wu3_state/wu3_score.txt`

Per scored slot T:
- out/T clean: T is scored. out/T_r1 must not exist, else **FAIL `unexpected_rerun`**.
- out/T not clean and out/T_r1 exists: T_r1 is scored in T's place with every per-load rule. T_r1 not clean too: the
  slot is VOID [not_clean].
- out/T not clean and no out/T_r1: the slot is VOID.
- A FAIL on either attempt (any ON lever check L1-L5, any wu2 FAIL) is FAIL and is never dropped. Such a slot counts as
  **failed** in the summary, and its loads are left out of the group metrics (fix round 1, D).
- The driver re-runs only a first load that passed check=. out/T_r1 next to a T that fails check= (a FAIL or a wu2
  stop-VOID) is **FAIL `stop_contradiction`** (fix round 1, A).
- A load whose conditions.txt holds a VERDICT but whose other file fails to parse keeps that VERDICT in the not-clean
  counts (still FAIL; fix round 1, F).
- A re-run of the prime, any `T_r2` or higher: **FAIL `unexpected_rerun` / `second_rerun`**. More `_r1` folders than
  R_MAX: **FAIL `rerun_budget`**. A `T_r1` without T: **FAIL `stop_contradiction`**.

The stop record against the folders:
- stop.txt missing, empty, unreadable, without a line end, more than one physical line (an extra blank line counts),
  or not in the form above (`interrupted=` with a class other than box included): **FAIL `stop_record`**. Exactly one
  line ended by LF or CRLF is accepted (fix round 1, E).
- class=box with `interrupted=<tag>` (fix round 1, B): the folder out/<tag>, whatever it holds, is treated as **not
  run**. The tag must be a load of the order (prime, T or T_r1) and, if its folder exists, the last folder in run order
  (prime, then each slot T, T_r1), else **FAIL `stop_contradiction`**. Without the field a partial folder stays FAIL
  `missing` / `unparsable` as before.
- class=fault: **FAIL `fault_stop`**, plus FAIL `missing` for every slot not run (as wu2).
- class=done but a slot not run or a not-clean slot without a re-run: **FAIL `stop_contradiction`** (plus `missing`).
- Any class: a slot not run or not resolved, followed by later slots with folders: **FAIL `stop_contradiction`** (the
  driver runs the slots in order). Scored folders without the prime folder: **FAIL `stop_contradiction`**.
- class=box and a present load with a wu2 stop-VOID (the driver stops those as fault): **FAIL `stop_contradiction`**.
- class=box and no FAIL: **NO VERDICT `box_not_quiet`**. The report prints the reason, the not-clean attempts as count
  and share (all and per arm), re-runs used, void slots per arm and slots not run. This holds also when no folder is
  missing (a box stop at the last slot, or a cancel).
- class=done and no FAIL: the wu2 rules decide (MIN_ON 8 valid ON, MIN_OFFP 3 valid OFF-IN-PLAY, else NO VERDICT;
  then H1-H3).
- Always reported: per arm the slots valid / void / failed / not run, attempts and not-clean attempts, re-runs used.
  **NOTE `void_arms_differ`** (reported only) if the ON and OFF void-slot counts differ by more than 4.
- Precedence: FAIL > NO VERDICT box_not_quiet > the wu2 rules.
- **Scoring input check (by hand):** before the single scoring run EnginePerf checks the sha256 of BOTH the tree copy
  `wt-render-split/perf-harness/wu3/wu3_order.txt` and the state copy `out/wu3_state/wu3_order.txt` against the
  registered order hash.
- `probe=` and `check=` work for T and for T_r1 (T_r1 is checked with its slot's line under its own tag). T_r2 and a
  prime re-run are refused (exit 2). An order tag ending in `_r<number>` is refused (driver and scorer).

## 5. What share of voids gives NO VERDICT (as numbers)
- **Stop by budget:** the 9th not-clean first attempt (R_MAX + 1) stops the series: class box, NO VERDICT. That is
  9 of the 32 scored slots = **28 %** of first attempts. Counted over the attempts run at that moment (9 first
  attempts not clean among at least 17 attempts, 8 of them re-runs) it is at least 9 of 17 ... 9 of 40.
  If each load is not clean by chance with probability q, P(stop) = P(at least 9 of 32): q = 0.10: 0.3 %;
  q = 0.20: 18 %; **q = 0.27: 50 %**; q = 0.40: 94 %. **Today's rate was 3 of 5 loads (60 %): at that rate the series
  ends NO VERDICT almost surely.** wu3 does not make a busy box quiet; it only turns "busy" into NO VERDICT instead of FAIL.
- **Few valid ON loads:** more than 8 of the 16 ON slots void = NO VERDICT `few_on`. In a complete (done) series at
  most 8 slots can be void for not_clean (each one used a re-run), so not_clean alone leaves at least 8 valid ON =
  MIN_ON exactly. Any one other ON VOID (window_short, save_unlocated, slow_cap) on top of 8 not-clean ON voids gives
  `few_on`.
- **Few OFF-IN-PLAY loads:** with all 8 not-clean voids on OFF slots, 8 valid OFF loads remain. P(fewer than 3 of
  them in-play) = 1.6 % at p = 0.68 (wu1 3.2), 14.5 % at p = 0.5 (wu2 3.2 had 0.2 % with 16 loads).
- Voids from other reasons (wu2 4.3 VOIDs) are counted as in wu2.

## 6. Margin table: NEW or CHANGED bounds only (derivation beside each; every other bound is wu2's, COPIED)
| bound | value | expected | margin | label |
|---|---|---|---|---|
| R_MAX, series time | 41 loads must start within 163 min | 137 min at the measured mean cycle; 149 at 216 s; 173 at the measured max | **1.19x / 1.09x / 0.94x: UNDER 2x** | MEASURED cycle (4 cycles, one morning), DESK formula |
| series time without re-runs | 33 loads within 163 min | 110 min (mean cycle), 91 min (162 s) | 1.48x / 1.78x: **UNDER 2x** | MEASURED; wu2's own row was 1.37-2.14x (COPIED) |
| R_MAX as a void cap | 9th not-clean first attempt stops | q = 0.1: 0.3 % stop; q = 0.27: 50 % | - (a rule, not a noise bound) | DESK, binomial, independence assumed (GUESS) |
| MIN_ON with 8 not-clean ON voids | 8 valid ON | 8 | **1.0x: no slack** | DESK; MIN_ON COPIED from wu1 |
| MIN_OFFP with 8 not-clean OFF voids | 3 valid OFF-IN-PLAY | 5.4 of 8 at p 0.68 | 1.8x: **UNDER 2x** | DESK; p from wu1 3.2 (COPIED) |
| VOID_ARM_DIFF (note only) | > 4 | 0-2 when voids fall at random (E[void slots] = 32 q^2: 1.3 at q 0.2) | - | GUESS; reported only, decides nothing |
| settle cap | **600 s** (wu2: 300 s), a box stop | measured 30-120 s mid-series, first 301 s (wu2's first settle passed its 300 s cap by luck: the test comes before the cap test) | 5x on the longest mid-series wait; **1.99x on the first: UNDER 2x** | CHANGED by EnginePerf at review; n = 1 for the first wait. Not mutant-tested (the self-test stubs settle): DESK |
| series time with a 600 s first settle | 41 loads within 163 min | 8203 + 297 = 8500 s = 142 min at the mean cycle | **1.15x: UNDER 2x** | DESK; a long first settle eats re-run room, then the slice check stops as box |
| not-clean threshold | another process >= 100 % of a core at load start or end | - | - | COPIED (tools/s2_bench.sh); effect on a load NOT measured (section 8) |
| BENCH_TIMEOUT, END_RESERVE, slice 3 h | 900 s, 120 s, 3 h | - | - | COPIED (wu2) |

Constants: **new** R_MAX = 8 (driver and scorer; the self-test checks they agree; constant mutants 7 / 9 in both),
VOID_ARM_DIFF = 4 (scorer; constant mutants 3 / 5). **Removed:** MAX_CONFOUNDED (and FAIL `stopped_series`).

## 7. Self-test
`cd wt-render-split/perf-harness/wu3 && nice -n 19 bash wu3_selftest.sh [<parent folder>]` (default parent
`wu3/selftest-runs`). Draft run 2026-11-27:
`WU3 SELFTEST PASS 335/335 (33 real cases, 179 made cases, 75 hook mutants caught, 20 constant mutants caught, 1 hook-coverage check, 1 R_MAX agreement check, 1 fixture-bytes check, 2 find-error checks, 22 driver checks, 1 untouched check)` (fix round 1, run with a relative parent folder; run folder under /tmp, deleted after; 22 driver checks = 1 offline test set + 18 driver mutants + R_MAX 7 / 9 + 1 static body check)

What it adds to wu2's self-test (all wu2 checks are ported; by design wu2's stopped_series cases and v_not_clean are
replaced, since their rule is gone):
- Scorer cases, each with a hook mutant that must be caught by every listed case: re-run scored in place (ON, OFF),
  not clean twice = VOID, unexpected_rerun (clean first load; prime), second_rerun (T_r2), rerun_budget (9 > 8) and
  its 8-re-run control, a lever FAIL on the first attempt kept / on the re-run, done with an unresolved slot, done with
  a missing slot, box tail missing = NO VERDICT with the shares, budget stop = NO VERDICT (9 of 25 attempts, 36 %),
  box with a FAIL, box with a stop-VOID, gap after an unresolved slot, T_r1 without T, prime missing, fault = FAIL
  (with and without missing folders), stop record missing / unparsable / two lines / CRLF (passes), not-clean prime =
  PASS, void_arms_differ at 5 (and not at 4), check= on T_r1, refused T_r2 / prime re-run / re-run tag in the order.
- Real fixtures, read in place: the five real wu2 loads of 2026-11-27. wu2_03_off (not clean) gets a re-run made from
  the clean wu2_02_off re-tagged (only xray.log is rewritten, 4 tag lines; the other files are symlinks under the new
  name); wu2_04_on likewise from wu2_01_on. Cases: a complete real series (both re-runs VALID, NO VERDICT few_on), a
  box stop at 04_on (2 of 5 attempts not clean, 40 %), done with a gap (FAIL), check= on the real re-run and on the
  real not-clean load.
- Driver, offline: the function block (now including the gates and the series loop) runs with stubs for the gates'
  helpers, the bench and the check: re-run right away; prime not re-run; no re-run after a failed check or a crash;
  not clean twice = no third attempt, the series goes on; 8 re-runs = done (41 loads), the 9th = box (18 loads);
  settle cap before a load and before a re-run = box; settings = fault; claim renewal = box; failed probe = fault;
  cancel = box; claim renewal then slice recheck right before every bench; the real verdict_of on real conditions.txt;
  the bench stub writes CRLF. Driver mutants and R_MAX 7 / 9 must each be caught.
- Fix round 1 (2026-11-27), each with a check and a mutant caught by name: A re-run after a first load that failed
  check= (FAIL stop_contradiction); B a cancel during a live bench writes `class=box interrupted=wu3_02_off reason=
  cancelled during wu3_02_off` (driver mutants: LIVE not set, field not written), a partial folder like the one the
  bench leaves on TERM + interrupted = NO VERDICT, the same folder without the field = FAIL missing, an interrupted tag
  that is not the last folder = FAIL, interrupted= with class=done = FAIL stop_record; C a stop record that cannot be
  written gives rc 1 (rc 143 kept) and the "stop record NOT written" note (driver mutant: rc not set; static check:
  the note is in the log line and the notify); D the slot with a failed first attempt counts as failed and leaves the
  ON group (15 valid); E an extra blank line or no line end = FAIL stop_record; F a not-clean load with a missing
  xray.log keeps its VERDICT in the not-clean counts; G a relative parent folder is made absolute (checked by running
  the suite with a relative parent).

## 8. Weaknesses (plain)
- **Whether one busy core really disturbs a load is not measured.** The not-clean mark fires when any other process
  uses >= 100 % of one of 16 cores at load start or end. wu2's data, descriptive only (one load each, decides nothing):
  ON clean 01: worst 35.9 ms, n20 5, loading 11.67 s; ON voided 04: 43.1 ms, 5, 11.34 s. OFF clean 02: 311.7 ms, 21,
  11.05 s; OFF voided 03: 291.8 ms, 21, 10.83 s. The voided loads look like the clean ones. wu3 keeps the mark as it is;
  it only re-runs.
- The mark samples start and end only. A desk test that runs only mid-load is not seen; one that runs at the end voids a
  load that may have been fine.
- At today's not-clean rate (60 %) wu3 ends NO VERDICT almost surely (section 5). A quiet box is still needed.
- A re-run is a later load of the same slot: the ABBA position is kept, the time of day is not. One re-run per slot, at
  most 8, so the shift is small; not measured.
- R_MAX's time margin is under 2x everywhere and does not fit at the slowest measured cycle (section 2); then the slice
  stop ends the series as box, NO VERDICT.
- The re-run fixtures in the self-test are made loads plus two re-tagged real loads; no real re-run has been run.
- The driver has had `bash -n` and offline function tests only, as wu2.

## 9. Predictions (EnginePerf, written 2026-11-27 before any wu3 load)
I have SEEN four wu2 loads (2 per arm, section 8). These guesses are informed by them and are not blind.
- Outcome: **PASS 50 % / NO VERDICT 30 % / FAIL 20 %.**
- NO VERDICT mostly from a box stop (the fleet rule against desk tests in timed slices is hours old) or a slow series.
- FAIL mostly from one of 16+ ON loads failing a lever or clock check, or H1 failing on one bad ON load.
- If it completes: ON worst frame 30-60 ms (median), OFF-IN-PLAY 250-350 ms; frames over 20 ms: ON 3-8, OFF 15-25;
  loading 0.4-0.9 s longer with the lever; 0-3 re-runs used; OFF loads in-play: 10-16 of 16.
- `loadtime_measures_differ` fires: 80 %.

## 10. Registration (EnginePerf, 2026-11-27)
Pins (sha256), recomputed from the committed bytes:
| file | sha256 |
|---|---|
| wu3_order.txt | 7360330ff8837e280736d4a8be3472dd938b1a9b8d75bee00a7cb36ad198c60c |
| wu3_score.py | b435a9ead5cdcf1d13af59f61ed4e5c20270dbd30de183464d3806ded94b8766 |
| wu3_selftest.sh | 7bacc55d6490bf9ed46028a59a05685e361b5ab5d56327d2f05d2b8a5933915e |
| wu3_run.sh | 89e8a8b3a8191280d74af75d07fa6ea53962b110ec51cd8d9351b2fa1ba7acf5 |
Exe, save and tool pins inside wu3_run.sh are wu2's, unchanged.

- Self-test on the pinned bytes, run by EnginePerf (10:43): `WU3 SELFTEST PASS 335/335 (33 real cases, 179 made cases,
  75 hook mutants caught, 20 constant mutants caught, 1 hook-coverage check, 1 R_MAX agreement check, 1 fixture-bytes
  check, 2 find-error checks, 22 driver checks, 1 untouched check)`.
- Review: two CodeRabbit passes (10 findings: 7 fixed, 3 skipped with reasons; then 1 minor, skipped with a reason):
  coderabbit-triage-2026-11-27.md. After pass 2 only the three pins and the driver's header line changed.
- EnginePerf's own change to the sub-worker's draft: settle cap 300 -> 600 s (section 6), predictions (section 9).
- Run command (from /home/caden/Projects/StalkerGameEngine, as a harness background task):
  `bash wt-render-split/perf-harness/wu3/wu3_run.sh`. Score once with the command of section 4, after checking the
  sha256 of both order copies by hand.
- Game options file: run/gamedata/configs/axr_options.ltx carries a `version` line that each exe rewrites with its own
  build stamp (found 2026-11-27: Stance14.exe writes 20261124, Warm18.exe 20261126). wu3 does not pin this file. If a
  Stance14 boot ran before the series, the PRIME load rewrites the stamp; the prime is never scored. Stated, not tested.
- The fleet rule against CPU-heavy desk tests during timed EXCLUSIVE slices (the supervisor's order of 2026-11-27) is
  what keeps the box quiet; the re-run budget only covers the leftovers (section 5).
- This text's own sha256 is sent to the supervisor with the GO request and is not written here.
