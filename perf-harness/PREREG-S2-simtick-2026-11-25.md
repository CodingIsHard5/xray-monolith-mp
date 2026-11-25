# PREREG S2: fixed simulation tick, 30 against 60 Hz (and 50), prototype behind a switch

Status: REGISTERED 2026-11-25, before any run. No box run without the Overseer's GO. Ordered by Caden (Discord 2026-11-25
17:08 UTC, via the Overseer): "Experiment with 60 and 30 fps and see which works best for the stuff". Plan and code
read: plan-facts/S2-sim-tick-plan.md, plan-facts/frame-loop-read.md.

## 0. Caveats first
- **This measures cost, not feel.** The prototype has no interpolation: between ticks the world stands still, so 30 Hz
  looks like 30 fps. Which rate is best to PLAY is Caden's test sheet, on a later build.
- **The box is not the target machine.** 16 threads, about 200 fps in this scene. The 4-core arms rank causes, they
  give no verdict.
- **"Off = stock" is proven for the serial test mode by exact counters, and for the normal mode only by the profiler's
  path check and by reading the diff** (three stock lines wrapped in a condition; everything else is new code that is
  skipped at 0). Normal-mode runs are not repeatable, so no byte comparison exists for them.
- **The input delay is computed from measured parts, not measured** (no input is injected).
- **Real-clock leaks stay**: code inside the simulation that reads the real timer (weapons, grenades, conditions: 129
  uses) and the scheduler's wall-clock budget are unchanged. In this scene nobody shoots; a firefight is not tested.
- **One scene** (bench1, the Rostok bar save), one build, one box session.

## 1. What is tested
Build: branch perf/sim-tick, commit 81e0b8fd2 (base perf/t3-stance 8a835585a = Stance14.exe), CI Perf-Build run
45664597008, green, installed as run/bin/SimTick14.exe, sha256
93fff4b537711cb38062f432c29551642e36ab852703b191727edb337e858f21 (pdb in ci/45664597008). Switch: console `sim_tick <hz>` (saved in
user.ltx), 0 = off. CodeRabbit read the change: 6 findings, 5 fixed, 1 named as a limit (particles run slow on a
frame with several ticks).
- **off**: stock. One update per rendered frame, real time step.
- **tNN (the tick gate)**: real time; every due tick runs one whole update with a step of 1/NN s; every frame is
  rendered; a frame with no tick only renders again.
- **cNN**: stock, capped at NN fps by the engine's own `r__framelimit` (update = frame at the capped rate).
- **fNN**: every frame is one tick of 1/NN s, as fast as the box goes (the bench's fixed-step mode with the new step).
  This is the server's case: no waiting. f33 = the stock fixed 33 ms step, the link to all older benches.
- **L**: the same on 4 cores, 8 GB (BENCH_LOWEND).

## 2. Part A: off = stock (runs first; a failure stops everything)
`tools/det_pair.sh 60 3 Stance14.exe:s2ref_a Stance14.exe:s2ref_b SimTick14.exe:s2off_a SimTick14.exe:s2off_b`, then
`tools/counters_diff.py` for every pair against s2ref_a at checkpoints 1, 2, 3.
- A1 PASS: the two reference runs agree exactly (C = 0 at all three checkpoints). If not, the harness is not usable
  today and part A gives no verdict (no retry beyond one, registered here).
- A2 PASS: both SimTick14 runs agree exactly with the reference (C = 0 at all three checkpoints).
- Guess: A1 PASS 90 %, A2 PASS given A1 95 %.
- What this does NOT show: the normal (threaded, real-time) mode. For that, every stock arm of part B is checked
  for the absence of the tick path (below), which shows the switch is off, not that the build is identical.

## 3. Part B: the experiment
Driver tools/s2_series.sh with tools/s2_order.txt (18 runs, one slice, EXCLUSIVE, at most 3 h; stops on a failed run or
check; a run whose conditions are not clean is excluded and re-run once at the end). Each run: tools/s2_bench.sh
(bench.sh with one lever: the fixed-step flag can be left out), flags `-perf_noinput -perf_nopresent -perf_slow 20`.
Order: off, t30, t60, t50, t50, t60, t30, off (two replicates, mirrored), then c30, c60, c50, f33, f30, f60, f50,
then offL, t30L, t60L. Uncapped runs: 3,000 warm-up frames, 12,000 measured. Capped: 15 s warm-up, 60 s measured.
Fixed-step: 600 and 3,000 frames, as the older benches.
**Per-run path checks (in the driver):** both switches read back from the saved user.ltx as set; profiler site
"Sim tick" present exactly when sim_tick > 0; a gated run has fewer updates than frames and at least one [SIMTICK]
line; every other run has updates = frames and no [SIMTICK] line.
**Score once:** `python3 -I tools/s2_score.py out tools/s2_pred.json out/s2_score.txt`. The docstring of the scorer
defines every figure and what makes an arm invalid (unfinished capture, conditions not clean, a site missing, the
measured rate more than 3 % off for tNN and cNN, no [SIMTICK] line after the warm-up).

### What is reported (the Overseer's list)
| Asked | Figure | From |
|---|---|---|
| main-thread ms per tick and per frame | main_tick; render; frame p50 | profiler sites "Process seqFrame", "Process seqRender"; frame file |
| frame time p95/p99 and hitches | p95, p99, fmax; over25 (frames over 25 ms per 1,000) | frame file |
| script cost per tick | script_tick (all threads); actor_tick (the player's own update, main thread) | sites "CScriptBinder::shedule_Update", "CActor shedule_Update" |
| physics and how its steps line up | phys_steps (0.01 s steps per tick, mean) | site "CPHWorld::Step" calls / updates. The per-tick spread is arithmetic: 3 or 4 at 30 Hz, 1 or 2 at 60 Hz, always 2 at 50 Hz |
| input-to-world latency | delay (computed) | half the poll interval + poll-to-picture time; for gated runs also the measured age of the world state at render start |
| cost per player for the server | sim_s (ms of simulation CPU per second: main + game thread); actor_tick x rate | sites; rate |
| extra | gc_s (Lua garbage collection ms per second: it runs once per tick, so a slower tick collects less), peak memory from conditions.txt | site "seqLuaGC" |

### Predictions (tools/s2_pred.json, 48 ranges; basis: three older runs of this scene at the fixed 33 ms step)
Basis per 33 ms tick: main 1.57 ms, game thread 2.09 ms, scripts 1.69 ms, the player's update 0.92 ms, 3.3 physics
steps; render 2.75 ms per frame; frames p50 5.0, p95 9.3, p99 12.1 ms; run-to-run spread about 2 %.
| What I expect | Range registered | Why |
|---|---|---|
| Simulation CPU per second: 30 Hz about 110 ms, 60 Hz about 170 ms, stock uncapped about 350 ms | t30 95-130; t60 140-210; off 250-480; t60/t30 1.3-1.8; off/t30 2.2-4.5 | the main-thread part costs the same per tick, so it doubles; the scheduler part is limited per object to one update per 30 ms or more, so it does not |
| Script cost per tick is LOWER at 60 Hz, per second higher | t60/t30 script_tick 0.60-0.95 | the player's script update runs every tick; other objects' are spread by the scheduler |
| Physics steps per tick 3.33 / 2.00 / 1.67 | within 0.04 | arithmetic |
| With the gate most frames have no tick | n0: t30 0.82-0.93, t60 0.65-0.85; no frame with two ticks at 60 Hz beyond 1 %; no dropped tick | about 250 fps against 30 or 60 ticks |
| Frame p99 is set by the tick frames and is about the same at 30 and 60 Hz | p99 6-14 ms both; ratio 0.80-1.25 | a tick frame costs render + one tick at either rate |
| No frame over 25 ms beyond 2 per 1,000 in any main arm | over25 0-2 | older runs: max 25.6 ms in 3,000 |
| Computed input delay: off 6-9 ms, 60 Hz 11.5-15, 30 Hz 20-24; stock capped is WORSE than the gate at the same rate (c60 24-26, c30 48-52) | as listed | the cap waits between the update and the render; the gate renders at once |
| Lua garbage collection per second falls with the tick rate | t30 25-70, t60 50-140, off 150-450 ms | it is driven per update. LOW confidence: its loop length depends on the render time |
| Fixed-step arms: 60 Hz costs 0.65-0.90 of 30 Hz per tick | f60/f30 sim_tick | same reasoning, no waiting |
| 4 cores: the same ratio | t60L/t30L 1.3-1.8 | reported; one range only |
**Guess:** 34 of 48 ranges hit (70 %). The ranges I trust least: the garbage collection three, the stock-uncapped
cost, the p99 pair.

### Margins of the validity rules
| Rule | Bound | Expected | Margin |
|---|---|---|---|
| measured rate of tNN / cNN | within 3 % of NN | within 0.5 % (the gate carries the remainder; the cap is a busy wait) | 6 times; if the box cannot hold 60 fps capped the arm is invalid and reported so |
| conditions clean | no other process at 100 % CPU or more | clean in the last series | none: one re-run, then the arm is missing |
| updates below frames in a gated run | U < N | U about 0.15 to 0.3 of N | wide |
| [SIMTICK] lines after the warm-up | at least one | 4 (one per 10 s of real time; about 55 s from the level start at 270 fps; the first is dropped) | a run shorter than 20 s would have none: that needs about 750 fps. The lines cover part of the warm-up too (the gate runs from the level start): the tick shares are for about the last 45 s, not exactly the measured frames; named here |
| slice | 3 h | about 18 runs of 2 to 3 min plus part A (4 runs of 5 min) | about 2 times |

### Each expected line or value against the paths that may not produce it
| Expects | A path that would not produce it | Verdict |
|---|---|---|
| site "Sim tick" in tNN and fNN runs | the gate is skipped while paused, loading, precaching, in det_mode: none of these during a capture (the capture itself waits for a running level) | keep |
| [SIMTICK] line | written every 10 s of REAL time in gated mode only; never in fNN (every-frame mode has no gate): the driver and the scorer expect none there | keep |
| site "CPHWorld::Step" | present only if physics ran in the capture: always in a running level | keep |
| site "seqLuaGC" | only with the parallel collector on (the default in user.ltx); if off the arm is INVALID by "site missing": checked in user.ltx before the series | keep, named |
| "CActor shedule_Update" on the main thread | the player's update is real-time scheduled on the main thread in this build (older runs: 3,000 of 3,000) | keep |
| updates = frames in stock runs | a paused frame still runs the update: equal | keep |
| "VERDICT: clean" | another worker's heavy process: re-run once | keep |

### Scorer self-test (perf-harness/s2-draft/s2_selftest.py: PASS)
A real older run scored as arm f33 gives the values in its own file (main_tick 1.563, 3.3 steps, rate); synthetic gated
arms with known numbers give them back (rates, replicate mean, step counts, tick shares with the first line dropped,
percentiles, both delay formulas, ratios, HIT / MISS / NO VALUE); five invalid arms are each refused for the right
reason and no other; a re-run replaces its original. Files are written with CRLF as the engine writes them under Wine.
Limit: no real gated run exists yet, so the [SIMTICK] line format is tested against the format string in the source,
not against engine output.

## 4. What the results can and cannot decide
They can say what each rate costs (per tick, per second, per player), whether the gate holds its rate, how frame times
and the computed delay move, and whether 50 Hz buys anything from lining up with physics. They cannot say which rate
feels best, cannot speak for a firefight or a crowded level, and cannot give a verdict for an old machine.

## 5. Pinned files (tools/, installed 2026-11-25)
| File | sha256 |
|---|---|
| tools/s2_series.sh | f014c60f55069721ec29ce501499653d1f2075ad6894b913d9a4f3c88971a336 |
| tools/s2_bench.sh | 072af8edb67eab095ea4408f3950a428709757b3ce1abdfe25efc25a3fb150b5 |
| tools/s2_order.txt | c0be4edd55a3a991d7597bb13fab5acdfdbe95d8ac9e399015c5f32483b496e5 |
| tools/s2_score.py | c6b47292df593211046c609413a2ac5bc2d214f953fb85b0def74ace423cfce1 |
| tools/s2_pred.json | 27e0ae840f4f64bd04ae3a94e325554aa806ffe21cdb5b7538647472e2389dcc |
| tools/s2_selftest.py | 31006cf288a3f845c63b2e93e4be77018504267a06be29028887be0a97036128 |
| tools/det_pair.sh | d4c38a79ce0263e72a9a970191a8d5fea34fc308ae17280aaabe4ac42f093895 |
| tools/det_run.sh | 01fb40dc2891d0602f3f596b8cde9664d05edd89eaa42a664cc9a34fbdc70f38 |
| tools/counters_diff.py | 5f2cf91df575ecf1e853c9f7b32b8fd18f72262155f2dd9416977d0c7ccbd61f |
| run/bin/Stance14.exe (reference) | b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe |
Series call: `S2_EXE_SHA=<the hash above> tools/s2_series.sh` (started as a background task). Checked before the
series: run/appdata/user.ltx has `lua_parallel_gc 1`; `sim_tick` and `r__framelimit` are set per run and left at 0.
The result goes into section 6 of this file, scored once.

## 7. Part A, first attempt: NO VERDICT (my harness error), and the one retry (registered before it runs, 2026-11-25)

**What happened.** The four runs of part A ran to the end (rc 0, 205 to 236 s each, out/det_s2ref_a, _b, s2off_a, _b;
log out/s2_partA.txt). `tools/counters_diff.py` then stopped with "missing file det_alife_counts_s2ref_a_1.txt" for all
three comparisons: the exact counters were never written. The call I registered in section 2 lacks the start switch
`-det_counters` (and `-det_lua_streams`), which the 2026-11-19 counter runs passed through `DET_EXTRA`. So neither A1
nor A2 has a result. Nothing of part B has run.

**My error, by the process rule:** I did not check the expected files against the path that produces them. The
counter files exist only with `-det_counters`; section 2 never named it.

**What I saw in the failed attempt, and what it is not.** det_pair's own position diff printed "checkpoints differ"
for every pair, the two STOCK runs against each other included (13 to 16 objects of 30,461, online objects by
positions; the A-Life generator state is equal in all four runs). That diff is not the registered check and is known
to differ between identical builds; it is why the counters exist. It gives no verdict either way.

**The retry (the one retry section 2 allows; no second one).**
`DET_EXTRA="-det_lua_streams -det_counters" tools/det_pair.sh 60 3 Stance14.exe:s2ref_c Stance14.exe:s2ref_d SimTick14.exe:s2off_c SimTick14.exe:s2off_d`,
then `tools/counters_diff.py` for s2ref_d, s2off_c, s2off_d against s2ref_c at checkpoints 1, 2, 3.
Wrapper: tools/s2_partA.sh (sha256 da21e3c5…, with the preflight below), output out/s2_partA_retry.txt. A1, A2 and the guesses are unchanged
(A1 PASS 90 %, A2 PASS given A1 95 %). Part B starts only on A1 and A2 PASS.

**Checked at the desk before registering this (the path table for the retry):**
| expected | produced by | checked |
|---|---|---|
| det_alife_counts_<tag>_<k>.txt | console command `det_alife_counts`, called by the dump script only with `-det_counters` | the command's name is in Stance14.exe and SimTick14.exe (3 hits each, as in RefDet14.exe) |
| det_counters_<tag>_<k>.txt | the dump script, same switch | script lines 181-182 |
| both moved into out/det_<tag>/ | tools/det_run.sh line 45 | read |
| counters_diff reads them and prints C | | run on out/det_wn against itself: C=0 at k = 1, 2, 3, rc 0 |
| NOT checked, the open risk | a 60 s interval with counters on these two builds has never run; the 11-19 runs used `-det_times 25,300,900` on RefDet14.exe | if a file is missing again: no verdict, stop, report |

**Preflight inside the retry (the Overseer's condition with its GO, added before the run).** When checkpoint 1 of the
first run (s2ref_c) is written, `det_alife_counts_s2ref_c_1.txt` must exist and be non-empty. If not, the wrapper stops
the whole series at once (exit 4, "PREFLIGHT FAIL" in the output) and that abort IS this retry's outcome: no verdict,
no third attempt without the Overseer. Desk test of the stop path: a group TERM runs the scripts' exit traps and leaves
no child (tested with two stand-in scripts in the same pipeline shape). Not tested: the stop against a live engine.

## 8. Part A retry: result, scored once (2026-11-25). A1 PASS, A2 FAIL as registered. Part B has NOT run.

Runs out/det_s2ref_c, _d (Stance14.exe), out/det_s2off_c, _d (SimTick14.exe, switch off); log out/s2_partA_retry.txt.
The preflight passed (counter file present after checkpoint 1); all four runs rc 0.

| check | result | values |
|---|---|---|
| A1 the two stock runs agree | **PASS** | C = 0 at checkpoints 1, 2, 3 |
| A2 both SimTick14 runs agree with stock | **FAIL** | both: C = 0 at checkpoint 1, C = 20 at 2 and at 3 |

My guess was A2 PASS at 95 % given A1. Wrong as scored. By the registration a failure stops everything: no part B,
no third attempt without the Overseer.

**What the 20 are.** At checkpoints 2 and 3 the same 20 offline objects (two blocks of 10 consecutive ids; other ids at
checkpoint 3) have ONE more update in the SimTick14 runs (7 against 6; 9 against 8). The pass counter says the same:
213 against 214 A-Life passes at checkpoint 2. The two SimTick14 runs agree with each other exactly (C = 0, 0, 0).
Frame number and engine time at every checkpoint are equal in all four runs, and so is the A-Life generator state.

**Why I do not read this as "the build differs", and why I cannot rule it out.**
- This is the signature the 2026-11-19 amendment recorded between IDENTICAL stock runs (PREREG-tierA-counters-amend…,
  part 1): "exactly ONE scheduler pass (20 consecutive objects) ahead", because a checkpoint taken by TIME and the
  A-Life pass clock are a few frames out of phase between runs. The cure registered then was checkpoints at fixed
  pass counts (`-det_passes`). My part A call used time checkpoints (`det_pair.sh 60 3`). That is a second harness
  error of mine in this part: I registered the mode that amendment had already found unfit.
- Looked at after scoring (not a registered check): the frames on which the A-Life simulator was scheduled differ by
  one frame between ALL pairs, the two stock runs and the two SimTick14 runs included (scheduler traces, first 107
  A-Life calls). So the phase does move between identical builds in these four runs.
- CORRECTION (added after section 9's desk work, Overseer 2026-11-25): the "four of four" below is TWO runs, each
  counted at two checkpoints that share one offset. Read it as 2 of 2 runs, not 4 of 4.
- Against that: both SimTick14 runs are ahead at both later checkpoints and neither stock run is. On 11-19 the shift
  hit 2 of 12 run-checkpoints. Four of four on one build by chance is unlikely, so a systematic shift of one pass on
  this build is NOT excluded by this data.
- In the code, the off path is the stock call: `sim_tick_plan()` returns -1 in det_mode and `FrameMove()` runs as
  before; the other edits are inside branches that need the switch on. Reading the code is not a test.

**What would decide it (not run; needs the Overseer's GO and its own registration):** the same four builds with
checkpoints at pass counts (`-det_passes`, the mode adopted on 11-19), three stock runs and three SimTick14 runs, so
that a one-pass phase shift cannot count as a difference and a real one still does.

## 9. Part A2: off = stock at PASS-COUNT checkpoints. A NEW registration (Overseer GO 2026-11-25), not a re-score.

Section 8 stands: A2 FAILED as registered; "off = stock" is not proven; part B stays blocked.

### 9.1 Desk read of the off path (commit 81e0b8fd2), the Overseer's three suspects
| suspect | what the code does with the switch off | could it add an A-Life pass? |
|---|---|---|
| accumulator init on the first frame | `sim_tick_plan()` parses the start switch once, copies the rate, and returns -1 at its first test (`s_sim_hz < 10`); the only write is `s_sim_synced = false`. The time accumulator is written only after that test | no: nothing the update reads is touched |
| a tick-gate branch before the switch check | in `FrameMove` the tick branch needs `s_sim_tick_now` (set only inside `sim_tick_run`) or `g_sim_tick_hz >= 10`; in `on_idle` the "no game thread" branch needs `sim_n == 0`, and off is -1; `sim_tick_stat` needs `sim_n >= 0` | no |
| the frame-number move | `dwFrame++` outside `FrameMove` exists only in `sim_tick_run(0)`, never called when off | no |
| also checked | the engine logs of both switch-off runs: `sim_tick 0` in the settings, no `[SIMTICK]` line; frame number and engine time at every checkpoint equal to stock | |
A code read is not a test; the run below is.

### 9.2 What the eight existing runs say about the one-pass shift (data already on disk, no new run)
The scheduler traces (start to checkpoint 1) of all eight part A runs, four stock and four switch-off:
- All eight: first A-Life call on frame 32, 107 calls by checkpoint 1, one gap of 16 frames (the first), every other gap 17 or 18.
- A gap of 18 is one frame of delay. In det_mode the scheduler has no clock budget; its limit is 256 objects a frame,
  and A-Life already runs 3 frames after it is due (561 ms against a period of 452 ms). How many objects are due
  depends on what is ONLINE, which differs run to run. Each delay moves the A-Life phase one frame, for good.
- Delays in the first 107 calls: stock 4, 2, 6, 7; switch-off 2, 9, 2, 3. Means 4.75 and 4.0. No build pattern.
- In the retry the two stock runs had 6 and 7, the two switch-off runs 2 and 3: about 4 frames apart by 60 s, the
  switch-off runs ahead. A checkpoint at a fixed TIME then falls on the other side of a pass for both later
  checkpoints together (the offset stays). So "4 of 4" is two runs, each counted twice, and the direction fits.
- This is evidence, not proof: the traces end at checkpoint 1, and the difference appeared after it.

### 9.3 Prediction: will pass-count checkpoints show the shift or hide it?
- If the shift is the phase drift above: C = 0 everywhere (R1, R2 PASS), as for the stock references on 11-19.
- If the off path really ran one extra pass: a pass-count checkpoint WOULD HIDE it in the counters (every run is
  compared at the same pass number). So the counters alone are not enough, and two checks are added that see it:
  **R3** the frame at which checkpoint 1's pass count is reached (an extra pass = 17 frames early), and **R4** the
  spacing of the A-Life calls in the trace (an extra pass = a short gap or an extra call). Limit: R4's trace ends at
  checkpoint 1, and R3 is gated at checkpoint 1 only, so an extra pass that first appears later than 100 passes is
  seen only as a reported number at checkpoints 2 and 3, not as a gate. All three suspects act from the first frames.

### 9.4 Runs and checks (scored once, `tools/s2_partA2_score.py`)
Six runs in one EXCLUSIVE slice, alternating so that load and order cannot line up with the build:
Stance14.exe s2p_s1, SimTick14.exe s2p_o1, s2p_s2, s2p_o2, s2p_s3, s2p_o3 (switch off = settings file `sim_tick 0`).
`DET_EXTRA="-det_passes 100,200,320 -det_lua_streams -det_counters" tools/det_pair.sh 60 3 …` through tools/s2_partA2.sh.
| check | PASS when | margin / basis |
|---|---|---|
| R1 stock agrees with stock | C = 0 at checkpoints 1, 2, 3, s2p_s2 and s2p_s3 against s2p_s1 | exact; 11-19: C = 0 for 3 pairs x 3 checkpoints with this method |
| R2 switch-off agrees with stock | C = 0 at 1, 2, 3 for each of the three against s2p_s1 | exact |
| R3 frame at checkpoint 1 | \|mean of switch-off - mean of stock\| <= 8 frames | one pass = 17 frames; in the eight runs the 100th call fell on frames 1716 to 1722, means 0.75 apart |
| R4 A-Life call spacing to checkpoint 1 | every run: first call on the same frame, gaps only 16/17/18, exactly one 16 | holds in 8 of 8 existing runs |
- R1 FAIL = the harness is not usable today: no verdict on the build, stop. R1 PASS and any of R2, R3, R4 FAIL =
  off is NOT stock: stop, part B stays blocked, I look for the cause before anything else runs.
- All four PASS = part A is met for the serial test mode; then, and only then, part B may be asked for again.
- Data missing (a file, a checkpoint) = NO VERDICT; the scorer exits 2. No retry without the Overseer.
- Guesses: R1 PASS 85 %; R2 PASS given R1 90 %; R3 PASS 90 %; R4 PASS 90 %; all four about 65 %.
- Self-test `python3 -I tools/s2_partA2_selftest.py out`: PASS. On the eight real runs R4 passes and R3 passes
  (100th call, difference -0.75); six mutants FAIL (an extra call, a late first call, a skipped call, the off runs 17
  frames early, 17 late); the counters reader gives C = 0/0/0 for the stock pair, 0/20/20 for the known case of
  section 8, and "no verdict" for runs without counter files.

### 9.5 Preflight 1: this call against the 11-19 counter method (the Overseer's condition)
Reference: `-det_passes 50,600,1650 -det_save -det_lua_streams -det_counters` on RefDet14.exe
(PREREG-tierA-counters-amend-and-worldproc-2026-11-19, part 1; adopted: C = 0 for the references, a skipped update caught).
The wrapper compares the flags word by word and refuses to start on any difference not listed here (tested: an
added flag is refused).
| difference | reason |
|---|---|
| pass counts 100,200,320 instead of 50,600,1650 | about 60/120/180 s of engine time instead of 28/340/930 s: six runs fit one slice of an hour. Cost: a difference that needs more than 320 passes to show is not seen. The section 8 difference showed by pass 213 |
| no `-det_save` | the saves feed the state diff, which is not a check here; the counters do not read them. Equal in all six runs |
| Stance14.exe and SimTick14.exe instead of RefDet14.exe | they are the two builds under test; both carry the counters command (checked in the binaries, and section 8's runs wrote the files) |
| `60 3` given to det_pair.sh | needed by its argument list; the dump script ignores the interval when `-det_passes` is given and takes the checkpoint count from the list (script lines 39-40, 158) |
Not different: det_mode, the serial settings file, the save (bench1), the dump script, det_run.sh, counters_diff.py.

### 9.6 Preflight 2, in the run: after checkpoint 1 of the first run its counter file must exist and the engine's own
command-line log line must carry the three flags; if not, the series is stopped (exit 4) and that is the outcome.

### 9.7 Pinned (sha256, first 16)
tools/s2_partA2.sh 5b976d5898e67645; s2_partA2_score.py 3e487ea90974315d; s2_partA2_selftest.py 549f353a92cf9462;
counters_diff.py 5f2cf91df575ecf1; det_pair.sh d4c38a79ce0263e7; det_run.sh 01fb40dc2891d060;
harness/zz_eperf_dump.script 2b79a817a08a1e7f; SimTick14.exe 93fff4b5…858f21; Stance14.exe b4ce9c77.
Copies of the three new files: perf-harness/s2-partA2/.

## 10. Part A2: result, scored once (2026-11-25). PASS, 4 of 4. Part B has NOT run (it needs the Overseer's GO).

Runs out/det_s2p_s1, _s2, _s3 (Stance14.exe) and out/det_s2p_o1, _o2, _o3 (SimTick14.exe, switch off), alternating, one
slice; all rc 0; both preflights passed (flags against the 11-19 method; counter file and the engine's command line
after checkpoint 1). Scorer 3e487ea9, report out/s2_partA2_score.txt.

| check | result | values |
|---|---|---|
| R1 stock agrees with stock | PASS | C = 0 at checkpoints 1, 2, 3 for both pairs |
| R2 switch-off agrees with stock | PASS | C = 0 at 1, 2, 3 for all three runs |
| R3 frame at checkpoint 1 (gate 8 frames; a pass is 17) | PASS | switch-off mean minus stock mean -2.7 frames; reported: checkpoint 2 +2.0, checkpoint 3 +2.7 |
| R4 A-Life call spacing to checkpoint 1 | PASS | 100 calls in every run, first on the same frame, gaps 16/17/18 with one 16 |

Frames at which pass 100 / 200 / 320 was reached: stock 1719/3425/5473, 1722/3427/5474, 1721/3426/5475;
switch-off 1718/3428/5479, 1717/3429/5474, 1719/3427/5477. The two builds overlap; nothing is near 17 frames apart.
My guess was 65 % for all four; it held.

**What this shows:** in the serial test mode, the build with the switch off makes the same exact A-Life counters as
stock at equal pass counts, and reaches those pass counts on the same frames within a few frames, with no extra or
missing A-Life call in the first 100.
**What it does not show:** (1) the normal threaded, real-time mode; (2) anything after pass 320 (about 180 s);
(3) a difference that only the online world would show: the counters are exact for offline objects, online ones are
reported, not gated; (4) an extra pass first appearing between pass 100 and 320 would show only in the reported
frames of checkpoints 2 and 3 (+2.0, +2.7: none seen).
**Section 8 in this light:** the one-pass difference there was the time-checkpoint phase drift (9.2), as predicted in
9.3. Section 8's FAIL stays in the record as scored; part A is met by this section, not by re-reading that one.

## 11. Part B stopped at run 14 of 18 on a failed per-run check. Cause: my check, not the run. AMENDMENT, REGISTERED 2026-11-25 on the Overseer's GO, before the remaining runs; nothing scored

**What happened.** Slices 1 and 2 ran 14 runs (13 passed their checks; `s2_t60_2` was flagged not clean, excluded and
re-run as `s2_t60_2_rr`, as registered, at the end of slice 1 because the series was split into slices to yield).
`s2_f30` (every frame = one tick of 1/30 s) then failed "no [SIMTICK] line in an ungated run" with 2 lines, and the
series stopped as registered. Runs `s2_f60`, `s2_f50`, `s2_offL`, `s2_t30L`, `s2_t60L` have not run.

**It is not a switch left over from the run before.** For all 14 runs (table in out/s2_state/check_table.txt): the
value of `sim_tick` the engine read at boot equals what the run asked for; every stock, capped and fixed-33 run booted
with 0, has 0 [SIMTICK] lines and 0 calls of the tick path; every gated run has tick-path calls = updates < frames.
The series script sets both switches before every run and reads them back after it (unchanged).

**The cause.** `s2_f30` is meant to run with `sim_tick 30`. The bench sets the fixed-step flag only DURING the capture
(device.cpp 386-398). In its warm-up and quit frames the flag is not set, so there the real-time gate runs at 30 Hz
and prints its 10-second line: one line at the first warm-up frame, one after the capture's end. Inside the capture
the run did what was asked: 3,000 updates in 3,000 frames, 3,000 calls of the tick path. The check tested a log line
that another phase of the same run can print: the third time in this series that a check of mine tested something
other than the outcome. My path table (section 4) had the line as "only printed by the gate" and missed that the gate
is live outside the capture.
**A side effect to name:** the warm-up of the three f-runs with `sim_tick > 0` runs gated in real time (about 69
world updates in 600 frames), the warm-up of `s2_f33` runs stock. The world at the capture's start is therefore not
the same between f33 and f30/f50/f60; between f30, f50 and f60 it differs only by the gate's rate.

**The amended check (tools/s2_series_b.sh, the pinned script with 8 changed lines):**
- with `sim_tick > 0`: calls of the tick path = updates (before: more than 0);
- gated run: unchanged (updates below frames, at least one [SIMTICK] line);
- every other run: updates = frames; "no [SIMTICK] line" only when `sim_tick` is 0.
- Desk test on the 14 finished run folders (`--check`, added for this): all 14 pass, `s2_f30` included; five mutants
  fail (f30 checked as stock; a gated run checked as fixed; a stock run checked as gated; f33 checked as f30; a capped
  run checked as uncapped).

**The remaining runs (tools/s2_order_b.txt), two slices: three fixed-step runs, then the three 4-core runs:**
`s2_f30_rr` (the registered check failed on `s2_f30`, so it is run again under the amended check, not re-scored; the
scorer takes `_rr` in place of its original), `s2_f60`, `s2_f50`, `s2_offL`, `s2_t30L`, `s2_t60L`. Lines as registered.
Predictions, margins and the scorer are unchanged. The 13 finished runs stay as they are.
(The hashes of the draft are replaced by 11.1 below.)

### 11.1 Registered form (the Overseer's four conditions)
1. **The first `s2_f30` stays EXCLUDED**, as run, under the check it was registered with. The amended scorer carries it
   in a fixed exclusion list and prints an EXCLUDED line for it. The run under this amendment is the new tag
   `s2_f30_rr`. Nothing is re-scored.
2. **The 13 finished runs pass BOTH checks:** the registered one live in their run, the amended one at the desk
   (perf-harness/s2-partB-amend/check_table.txt, two columns). `s2_f30` is the only row with FAIL / ok.
3. **Warm-up confound, a stated limit of every f-against-f33 comparison:** f30, f50 and f60 warm up gated in real
   time, f33 warms up stock. Direction: in 600 warm-up frames the gated runs make about 69 world updates and f33
   makes 600, each at its own step, so at the capture's start the f33 world has been updated about nine times as
   often. A-Life and scripts that settle after a load (spawn bursts, first schedules) are then further along in f33.
   If the first seconds after a load cost more per update, the f-runs carry more of that cost inside their capture
   and look MORE expensive per tick against f33 than they are. f30 against f60 is far less affected (both gated).
4. **Phase test before the remaining runs, run and passed:** `tools/s2_phase_selftest.py out <work>` = every run type
   of part B (14 types: off, t30, t60, t50, c30, c60, c50, f33, f30, f60, f50, offL, t30L, t60L), on real run folders
   (types not run yet are made from the nearest real one), each log with warm-up, capture and quit phases.
   - every type: the amended check passes and the amended scorer finds the arm valid;
   - switch on: a poisoned gate line (dropped 999, age 9999) in the warm-up AND in the quit phase changes neither
     the check's answer nor any scorer value;
   - switch off: the same line FAILS the check (the gate printed in a run that asked for stock);
   - gated types: the same line in the middle of the capture IS seen (dropped +999).
   Result: PHASE SELFTEST PASS, 33 of 33.
   **It found a second leak, in the SCORER, before any scoring:** the registered scorer took "every [SIMTICK] line but
   the first" for the gated arms' shares, drops and ages. The first line is printed at the first gated frame and
   covers ONE frame; the second covers the warm-up; a last one can cover quit frames. So one warm-up window was in
   the gated arms' numbers. Amended scorer `tools/s2_score_b.py`: only the lines printed between the capture's start
   and finish lines, less the first of them (its 10 s window reaches back before the start); an arm without those
   two log lines is invalid. Its self-test `tools/s2_selftest_b.py` gives every gated fixture a poisoned line in
   the warm-up, at the capture's start and in the quit phase: PASS 20 of 20, and the REGISTERED scorer on the same
   fixtures reads dropped 999 or more (so the fixtures do catch the leak). No other definition of the scorer changed;
   the 48 predictions are unchanged. The gated arms' shares, drops and ages come from fewer windows (about 4 of 5).
   What the phase test cannot see: the profiler's own files (sites, frames) are taken as capture-only by its
   start/finish; I did not test that.
   While testing, the amended scorer ran on real run folders one at a time; I read only pass/fail and the dropped
   count (0 in the gated runs). The series has not been scored.
- The series script's notice to the Overseer now says its counts are for the slice only.
**Pinned (sha256, first 16):** s2_series_b.sh 83d809d7e91cf75a; s2_order_b.txt c656b5e4489752f3; s2_score_b.py caeb5715cb86159f;
s2_selftest_b.py 46478fc24ffd3a49; s2_phase_selftest.py d49a671b35c56825; driver s2_partB2.sh 18bfa3585fa8d70f (two
slices: f30_rr, f60, f50; then offL, t30L, t60L). SimTick14.exe 93fff4b5…858f21. Copies: perf-harness/s2-partB-amend/.
Score, once, when all six are done: `python3 -I tools/s2_score_b.py out tools/s2_pred.json out/s2_score.txt`.

### 11.2 The first 4-core run failed its check: the instrument again, not the run. AMENDMENT (DRAFT until the Overseer's GO)
**State:** 16 of 18 registered runs are done and passed their checks (the 13 of 11.1, then `s2_f30_rr`, `s2_f60`,
`s2_f50` under the amended check). `s2_offL` then failed "updates(0) != frames(12000)" and the series stopped.
`s2_t30L` and `s2_t60L` have not run. Nothing is scored.

**Real or instrument: the instrument.** The run did its work: the profiler file has 12,000 calls of the update in
12,000 frames, the engine read `sim_tick 0` at boot, there is no gate line, conditions clean. The bench lowercases
the tag for its folder and file names (`TAG=${TAG,,}`, tools/s2_bench.sh line 9), so the run is in `out/s2_offl`;
the series check looked in `out/s2_offL`, found no file and read "0 updates". The three 4-core tags are the only
ones in the series with a capital letter. Fourth check error of mine in this series. Why the phase test of 11.1 did
not catch it: it built its folders under the tag as the ORDER FILE spells it, not as the bench names them.
**The same fault sat in the scorer** (both the registered and the 11.1 one): its tag pattern wants a capital L, so
`s2_offl` would have been filed as an unknown arm name with no prediction matched, and `s2_t30l` as "kind unknown":
not gated, no rate check. Found before any scoring.

**Changes:**
- Series check: looks in the lowercased folder; when the run's files are missing it FAILS with "run files missing",
  never as a count of 0. The after-run copy of the settings file and the conditions verdict use the same folder.
- Scorer: a folder `s2_offl`, `s2_t30l`, `s2_t60l` is filed under the registered arm name with the capital L (the
  prediction keys are unchanged); a folder whose name is no registered arm is INVALID by name, not silently
  "unknown"; `s2_offl` joins `s2_f30` in the fixed exclusion list.
- Phase test: folders and files are now named as the bench names them; the 4-core stock type is the REAL run
  (a copy of `out/s2_offl`, with the run dir's settings file standing in for the after-run copy it never got);
  new assertions: every run is filed under its registered arm name; missing files fail by name.
  Result: PHASE SELFTEST PASS, 49 of 49. Scorer self-test: PASS.
- Not proven on real 4-core bytes: the two GATED 4-core types (made from the 16-core t30 and t60 folders under
  lowercase names). The first of them to run is its own proof or failure.

**Remaining runs (tools/s2_order_c.txt, one slice):** `s2_offL_rr` (the first failed the check it ran under, so it
stays excluded as run and is run again, as with s2_f30), `s2_t30L`, `s2_t60L`. Lines as registered.
**The registered scorer, to be shown beside the amended one (Overseer's condition):** run as it is, it cannot name
the 4-core arms (lowercase folders). I will show its output as it is, and say so; the comparison of the warm-up
window's effect is on the six real-time gated runs, where both scorers work.
**Pinned (first 16):** s2_series_b.sh 75b2335b52bc662f; s2_score_b.py d4548104e1e00b9d; s2_phase_selftest.py b1994b3173a086cb; s2_order_c.txt c8d1078952884303;
s2_selftest_b.py 46478fc24ffd3a49 (unchanged). These replace the 11.1 hashes of the same files.
