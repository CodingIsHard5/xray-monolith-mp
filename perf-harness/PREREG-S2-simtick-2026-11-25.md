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
