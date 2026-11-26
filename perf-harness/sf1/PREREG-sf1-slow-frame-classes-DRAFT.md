# PREREG sf1: what the slowest 1 % of frames are made of (DRAFT 2026-11-26, desk only, NOT registered)

A measurement, not an optimisation: nothing passes or fails, but every run check and every no-verdict rule below is
fixed before any run. Nothing here has been run on the box. No box run without the Overseer's GO.
Source of the task: plan-facts/next-step-sizing-2026-11-26.md, runner-up "classify the steady 1 % slow frames".
Labels: MEASURED (a number in a file, cited), DESK (my count from existing files), GUESS.

## 0. Limits first
- One box: Ryzen 9800X3D (16 threads), Wine + DXVK, headless Xvfb 1280x720, no Present (`-perf_nopresent`: the frame
  ends in a wait for the GPU instead of a present), no input (`-perf_noinput`). Says NOTHING about the 2012 reference
  machine, native Windows, vsync, a real compositor or a 4-core box.
- Two saves, player standing still: cordon1 (Cordon, l01_escape, GammaVR's test save) and bench1 (Rostok bar, copy of
  Caden's "rodent1"). No combat, no movement, no level change, no menus.
- Profiler cost: the full-probe builds (RefDet14, all SimTick builds) make about 80,000 probe calls per frame, about
  0.7 ms (INFORMATION.md "R1 step 0"; DESK: s4c_noff_1 perf_sites 79,792 main calls/frame). This experiment uses
  Coarse14.exe, the hot probes compiled out: 1,444 (Rostok) / 705 (Cordon) main calls per frame (MEASURED,
  PREREG-R1-step0 section 10), so about 0.01 ms at 9 ns per call (DESK). The price: render internals are coarser
  (30 hot sites folded into their parents' self time).
- Coarse14 is the perf/render-split build 98a444975 of 2026-11-22. Later engine changes (S2 tick, 4a-4c, fixes) are
  not in it. With the tick off they are meant to be inert (S2-4c part A: off = stock, C = 0), but frame-time
  equality between Coarse14 and today's builds is not shown.
- The profiler's slow-frame dump keeps only the 14 largest self times per frame across all threads, books a scope's
  time in the frame where it ENDS, has no zone for the main thread's wait on workers, and lumps all non-main threads
  into "other". So a class here is an attribution by rule, not a proven cause. NOTES.md lists what it cannot tell apart.
- Real time (stock frame loop). The sizing study's frame files (K0 runs) used `-perf_fixed_dt` (a fixed 33 ms game step
  per frame), so its "job every 12-14 frames" and "3.5 s train" do not carry over in frames or seconds (section 3.2).

## 1. Question
In steady play, fixed tick OFF (stock frame loop, real time, uncapped), on two scenes: which share of the frames above
each run's own p99 belongs to which class of work (rules in section 4), and how many ms each class adds over the median.

## 2. What runs (flags copied from the LAST prereg on this harness: PREREG-S2-4c part B)
Exe `run/bin/Coarse14.exe` sha256 `65edcf401faeb125c68283084f55500c8d55cca58353e682f9ff8f96d884d88c`
(16-hex prefix registered in PREREG-R1-step0-2026-11-22.md line 70; full hash taken today from the file).
Saves: cordon1.scop 6dea752b..., cordon1.scoc a9f9a8a9..., bench1.scop 891693c5..., bench1.scoc 7c2ddf33... (full
hashes pinned in sf1_run.sh). Settings: run/appdata/user.ltx as is, checked read-only: r__framelimit 0, no sim_tick.

| switch / setting | source of the value (file:line) | what it produces |
|---|---|---|
| bench `tools/s2_bench.sh` (072af8ed) | tools/s4c_series.sh:74, tools/s4c_run.sh:7 | out/<tag>/conditions.txt (clean/CONFOUNDED verdict, load, peak RSS), wine_stderr.log, xray.log (copy of the engine log), <exe>_d3d11.log, <exe>_dxgi.log |
| `-perf_capture <warm>,<frames>,<tag>` (bench adds) | tools/s2_bench.sh:56 | perf_sites_<tag>.csv (per site: calls, inclusive and self ms, main and "other"), perf_frames_<tag>.csv (frame,ms for every captured frame) |
| `-perf_quit`, `-nointro -dbg -start server(<save>/single/alife/load) client(localhost)` | tools/s2_bench.sh:55-56 | quits after the capture |
| BENCH_FIXED=0 (no `-perf_fixed_dt`: real time) | tools/s4c_order.txt:3-12 (fixed column 0), tools/s2_bench.sh:11 | - |
| `-perf_noinput -perf_nopresent` | tools/s4c_series.sh:74 (default S2_BENCH_FLAGS) | - ; nopresent makes the site "bench: wait for GPU (no present)" |
| `-perf_slow <slow_ms>`: 6.0 Cordon, 9.0 Rostok (CHANGED from 20, derived 3.3) | tools/s4c_series.sh:74 has 20 | perf_slow_<tag>.csv: `#threshold_ms`, then per frame above it the 14 largest self times (frame, frame_ms, thread main/other, self_ms, calls, name, file, line); at most 600 x 14 rows (profiler_agg.cpp:42-43, 190) |
| BENCH_EXCLUSIVE=1, BENCH_NOCLAIM=1, BENCH_LOWEND=0, BENCH_TIMEOUT=900 | tools/s4c_series.sh:74; timeout = bench default tools/s2_bench.sh:12 (COPIED) | - |
| BENCH_DISPLAY=:141 | box rule (tools/t3_kw1.sh:36-42 refusal) | - |
| warm 11500, frames 10000 (CHANGED from 3000 / 12000, derived 3.2, 3.3) | tools/s4c_order.txt | - |
The driver additionally writes out/sf1_state/{series.log, done.txt, rerun.txt, run_<tag>.log}.

## 3. Design
### 3.1 Runs
Six runs, one WANT slice, order ABBAAB (sf1_order.txt, one source for driver and scorer):
sf1_cor_1, sf1_ros_1, sf1_ros_2, sf1_cor_2, sf1_cor_3, sf1_ros_3. A run whose bench verdict is not "clean" is re-run
once at the end as <tag>_rr (rule copied from tools/s4c_series.sh:83; the scorer reads the same order file and rule).

### 3.2 Warm-up exclusion: 11,500 frames before the capture (derived)
- What must be excluded: the after-load A-Life warm-up (one ~300 ms frame, then a train of 22-52 ms frames).
- MEASURED (INFORMATION.md "Is the Cordon hitch train a load transient?"): with 600 real-time warm-up frames and then a
  `-perf_fixed_dt` capture, the last A-Life-topped train frame was capture frame 547-556 in all 4 runs with a train
  (the "3.5-3.7 s" of the sizing study is WALL time of those 556 frames).
- The A-Life pass is scheduled in GAME time (alife.ltx 170-470 ms; the observed period, 14 frames x 33 ms = 462 ms,
  matches). In a fixed-step capture game time runs at 33 ms per frame, i.e. about 6x wall time. So the train ends at
  most 600 warm-up frames (real time, <= 600 x 7 ms = 4.2 s; the first measured frames averaged 6.2-6.6 ms) plus
  556 x 33.3 ms = 18.5 s of game time = at most 22.7 s of game time after the load ends.
- In a real-time run game time = wall time, so the exclusion must cover >= 22.7 s of wall time. With a 1.5x margin:
  34 s. The warm-up counts frames (device.cpp perf_autocapture: only frames after precache, not paused). At the
  fastest frame seen in any Cordon or Rostok capture, 3.22 ms (DESK, r1a_cor_k0_*), rounded down to 3.0 ms:
  34 s / 3.0 ms = 11,333 -> 11,500 frames. Expected duration at the median frame (about 4.2 ms Cordon, 5 ms Rostok):
  48-58 s. Same warm-up for both scenes (one rule).
- Outcome check that it worked: run check R6 (section 6). A train found in the window voids that run and says the
  derivation is wrong.
- Consequence for B2b (an INFERENCE, not measured): in real-time play the train should last about 15 s of wall time
  (about 32 passes x 462 ms), not 3.5 s.

### 3.3 Capture length and slow thresholds (derived)
- p99 of N frames leaves N/100 slow frames. N = 10,000 per run gives about 100 per run and 300 per scene: the binomial
  SE of a pooled share of 0.5 is 0.029, of a per-run share 0.05.
- `-perf_slow T` must be BELOW each run's p99 (else the frames between p99 and T have no zone rows) and the number of
  frames above T must stay under the dump cap of about 600 frames per capture (profiler_agg.cpp:43, 190).
- Rostok T = 9.0 ms. DESK: the eight real-time stock-loop Rostok runs on disk (s2_off_1/2, s4a/s4b/s4c noff/poff; full
  probes) have p99 11.35-12.14 ms and 1.9-3.6 % of frames above 9 ms. Coarse14 drops about 0.7 ms of probes, mostly in
  render: expected p99 about 10.6-11.5 ms, so 9.0 is >= 1.4 ms below. Frames above 9: <= 3.6 % x 10,000 = 360 < 600.
- Cordon T = 6.0 ms. No real-time Cordon capture exists. DESK from the Coarse14 fixed-step Cordon runs: p99 9.9-11.7
  with the periodic job, 6.7-7.2 without it (sizing study); 4.5-7.8 % of frames above 6 ms. T = 6.0 is 10 % under the
  lowest no-job p99. Frames above 6 may reach 780 per 10,000 > 600: then the dump stops early and the scorer cuts the
  window at the first frame above T without rows (rule 4.1); at 7.8 % the cut comes after about 7,700 frames, above
  the 5,000 minimum. THIN: no real-time Cordon data at all.

## 4. Outcome rules (implemented exactly in sf1_classify.py)
### 4.1 Window and slow set, per run
Window = captured frames 0 .. cut-1; cut = the first frame with ms > T x 1.02 that has no rows in perf_slow (dump cap
reached), else all frames. p50 and p99 by linear interpolation over the window. Slow frames = window frames with
ms > p99. Excess E = ms - p50.

### 4.2 Class of a zone (first match; full lists in zone_class())
| class | zones (prefix or name) | note |
|---|---|---|
| (excluded) | mt_FreezeThread, Check timer | watchdog thread: a 500 ms Sleep loop (device.cpp:310-327); its "self" is sleep |
| alife | `ALife/` | offline A-Life pass incl. the Lua of offline objects; runs in the game-thread chain |
| gc | seqLuaGC | elastic: repeats while the main thread renders (EngineThreading.cpp:177-190) |
| gpu_wait | `bench: wait for GPU` | the no-present GPU wait on the main thread |
| script | CScriptBinder::shedule_Update, CScriptZone::shedule_Update, names containing script/lua | object binders (actor on main, NPCs on other) |
| io_stream | Sound: Load, r_open_impl, CInifile::, CTexture::Load, Process{Prefetch,Spawn,Game}Events, Loading... | file loads and spawns |
| sound | CSoundRender*, SoundEvent*, Sound* | |
| render | r_dsgraph*, R_constant*, Render*, phase_*, CDetailManager*, MT_CALC details, RCache*, CBackend::, Particle*, ... | main-thread render feed and its helpers |
| online_sim | stalker/, Level_Graph, COLLIDER, xrCDB, ISpatial_DB::q_ray/q_box, Build Path, Memory Manager, CPH*, ... | online AI, physics, scheduler |
| other_known | any other named zone | listed by name in the frame lines |

### 4.3 Class of a slow frame
For each dumped row (not excluded): delta = self - (that site's self per frame on that thread over the capture, from
perf_sites). score(class) = sum of positive deltas of its rows, both threads.
1. Winner = the class with the largest score among the explained classes and gc. If score >= 0.5 x E, the frame is
   that class (gc counts as unexplained: cause and consequence are not separable).
2. Else if the main thread's unzoned time grew by >= 0.5 x E, the frame is `main_unzoned_or_wait`. Unzoned growth =
   (ms - capture mean frame) - (sum of signed main-thread deltas of the dumped rows); sites not in the dump are taken at
   their mean. It is the join wait on workers, unzoned main code or OS/Wine pre-emption: NOT separable.
3. Else `unknown`. A slow frame without rows (between p99 and T x 1.02) is `no_data`.
Explained = alife, script, render, gpu_wait, online_sim, io_stream, sound, other_known.
Unexplained = gc, main_unzoned_or_wait, unknown, no_data.

### 4.4 Reported per scene (3 runs pooled)
Per class: number of slow frames, share, mean ms over p50, ms over p50 per 1000 window frames, share of the class score
on the main thread. Per run p50/p99/max/frames > 100 ms. Reported only: median wall time between consecutive
alife-class slow frames (periodicity).

## 5. Constants, each with its derivation
| constant | value | derivation (what each end means) |
|---|---|---|
| THETA (frame explained) | 0.5 of E | a class covering more than half the excess is the unique largest share: no other class can also reach it. Lower would let two classes "explain" one frame; higher would push frames where one job clearly dominates into unknown |
| UNEXPLAINED_MAX | 50 % of slow frames | above it no class can be shown to hold a majority of the tail; any "the tail is X" would describe a minority: no conclusion |
| DOMINANT | pooled share - 2 SE > 50 % | "most of the slow frames" with sampling error of n slow frames |
| P99_SPREAD | +-12 % of the median-run p99 | 2 x the largest half-range of p99 seen in earlier 3-run groups of one build and scene (K0 Rostok 12.71-13.72: +-3.8 %; Cordon no-train 9.89-11.02: +-5.4 %; real-time Rostok 11.35-12.14: +-3.4 %), rounded up. Beyond it the runs do not repeat and pooling is not allowed |
| SHARE_Z | 3 SE + 1/n per run | about 30 comparisons (<= 5 classes x 3 runs x 2 scenes): Bonferroni at 5 % gives z about 2.9; the 1/n covers discreteness at n about 100 |
| SHARE_CHECK_MIN | 10 % | smaller classes have too few frames per run (< 10) for a share test |
| COVER_TOL | 2 % of T | the engine decides "slow" with a 10 ms clock calibration (profiler_agg.cpp:84-91), the frame file with the final scale; the error is far below 1 %, 2 % is generous |
| MIN_WINDOW | 50 % of N | keeps >= 50 slow frames per run (per-run share SE <= 0.07) |
| TRAIN | 3 frames with ALife/* self >= 15 ms, gaps <= 1000 ms | 15 ms = lower edge of a train pass (INFORMATION.md: "a slow pass takes 15-40 ms"); 1000 ms = 2 x the longest pass interval (470 ms) plus margin. Steady state: cpl1 saw 6 such frames in 44,000 (about 1 per 35 s), so 3 within 1 s gaps by chance is not expected |
| HITCH_MS | 100 ms | reported only; the plan's hitch target (COPIED) |

## 6. Checks (each tests an outcome inside the capture)
Per run (any failure voids the run, every reason is listed by code):
- R0 `missing`/`empty`/`malformed`: folder, perf_frames, perf_sites, perf_slow, conditions.txt present, non-empty,
  parseable; a missing file is named, never read as 0.
- R1 `framecount`: frames file rows = sites header = registered frames.
- R2 `threshold_header`: perf_slow threshold = registered slow_ms.
- R3 `stock_loop`: site "Process seqFrame" main calls = frames, site "Sim tick" absent or 0 calls.
- R4 `frame_limiter`: site "Eco Render" absent or 0 calls (uncapped).
- R5 `not_clean`: bench verdict clean, else read <tag>_rr; if that is missing or not clean: void.
- R6 `train`: no after-load train in the window (section 5).
- R7 `coverage_p99` / `window_short`: window p99 >= T x 1.02; window >= 50 % of frames.
Per scene:
- V0 `runs`: all 3 runs valid, else NO VERDICT for the scene.
- V1 `p99_spread`: else NO VERDICT (not repeatable).
- V2 `coarse`: unexplained share > 50 % -> NO VERDICT: "the profiler zones are too coarse to say what the tail is
  made of". This is a result about the tool, reported as such, no class conclusion.
- V3 `share_repeat`: else NO VERDICT on the composition (shares do not repeat across runs).
- Then RESULT: DOMINANT <class> (rule in section 5) or NO SINGLE DOMINANT CLASS with the largest two named.
Stop rules (driver): first run with rc != 0 (crash, timeout, engine exit without capture), first failed driver check
(files, frames, seqFrame calls, sim tick, limiter, slow threshold), display not free, settings not stock, settle cap,
less than 15 min left in the 3 h slice, protected title, Roblox, Caden's launcher. A stopped series is not resumed
without a new GO.

## 7. Driver (sf1_run.sh, DRAFT, only `bash -n` checked)
Hash preflight of bench, its two helpers, exe, four save files, order, scorer, self-test (the last three are PIN_ME
placeholders: the preflight REFUSES until registration fills them); display :141 only, refused if
/tmp/.X11-unix/X141 or /tmp/.X141-lock exists (preflight, slice grant, before every run after a 20 s grace for the
previous run's Xvfb); user.ltx read-only check; out/<tag>, out/<tag>_rr and out/sf1_state must not exist; WANT/claim/
withdraw/release and "slice granted" log as tools/t3_kw1.sh:55-59, 68; settle and per-run claim renewal as
tools/s4c_series.sh. Expected box time: 6 x (about 35 s load + 50 s warm-up + 45 s capture) plus settles: 20-35 min.

## 8. Scorer and self-test
`sf1_classify.py` sha256 34ab59f744251ecac4c06fce5115ea049fa979a4310dc4c595b6fedcf1583510 (stdlib, python3 -I).
`sf1_selftest.sh` sha256 d55fa9a1d7b434eb45f86ef50bb8717d427ab0c7594718b7c48416128d06da3d.
`sf1_order.txt` sha256 abcd195514e44ecb3ec70ad3a075393edb123c2e48b5cedcb8067903c4768577.
Self-test 2026-11-26: `SF1 SELFTEST PASS 49/49 (6 real-folder cases, 42 made cases, 1 untouched check)`, folder
selftest-runs/20261126-151722-463192. Real folders read in place (list in NOTES.md); they exercise coverage_p99, train
(found at capture frames 112-117 in r1a_cor_k0_1, cord1, cord2, cp1, as INFORMATION.md reports), not_clean, the _rr
substitution (s4c_poff_1 -> s4c_poff_1_rr), frame_limiter (s2_c30/50/60), stock_loop (s4c_n30_1 etc.), framecount,
threshold_header and a missing slow file (hk2, hk3). No real folder has slow rows below 20 ms, so every class rule is
tested on made fixtures: real frame and site files of r1_cor_k3, r1a_cor_k0_3, r1_cor_k2, s2_off_1, s4c_noff_1,
s4a_noff_1 with rows made to a plan. One mutant per class (11), both sides of THETA, and one per check and no-verdict
rule; out-file refusal, uppercase tag, wrong slot count. Desk mutation pass (scorer copies in my scratchpad with one
check removed: train, coverage, sim tick, limiter, exclusion, THETA, substitution, share repeat, window cut): every
one was caught by its case.
Score ONCE after the series, output in the same call:
`python3 -I wt-render-split/perf-harness/sf1/sf1_classify.py root=out order=wt-render-split/perf-harness/sf1/sf1_order.txt out=out/sf1_state/sf1_score.txt`

## 9. Predictions (GUESS unless marked)
| # | prediction | P |
|---|---|---|
| P1 | all 6 runs valid in the first slice (no _rr needed beyond the rule) | 65 % |
| P2 | Cordon: alife is the largest class | 70 % |
| P3 | Cordon: alife DOMINANT by the section 5 rule | 50 % |
| P4 | Cordon: median gap between alife-class slow frames 130-590 ms (the 170-470 ms schedule +-25 %) | 60 % |
| P5 | Rostok: the largest class is render, gpu_wait or main_unzoned_or_wait | 75 % |
| P6 | Rostok: alife share < 20 % | 80 % |
| P7 | Rostok: NO VERDICT [coarse] (unexplained > 50 %) | 30 % |
| P8 | Cordon: NO VERDICT for any reason | 30 % |
| P9 | at least one Cordon run's window is cut by the dump cap | 30 % |
| P10 | at least one Cordon run voided by coverage_p99 (p99 < 6.12 ms) | 10 % |
| P11 | a train found in any window (R6) | 5 % |
| P12 | Rostok p99 (Coarse14, real time) 10.0-12.0 ms in all 3 runs | 70 % |
Basis: sizing study (Cordon tail set by a job every 12-14 fixed-step frames, GUESS A-Life; Rostok tail mostly not
simulation, S2 12.3); the real-time cadence of the A-Life pass (170-470 ms) is about 1-2.5 % of frames at 4.2 ms,
i.e. right at the p99 boundary, which is why P3 is only 50 %. Named risks: no real-time Cordon data (T 6.0 thin),
main_unzoned_or_wait swallowing the render/GPU tail in Rostok (P7), the dump cap in Cordon.

## 10. Margin table (expected vs bound)
| item | expected | bound | margin | status |
|---|---|---|---|---|
| Rostok T vs p99 | p99 10.6-11.5 (DESK) | p99 >= 9.18 | >= 1.4 ms | derived |
| Cordon T vs p99 | p99 7-10 (GUESS) | p99 >= 6.12 | >= 0.9 ms | derived, THIN |
| Rostok dump frames | 190-360 per 10,000 (DESK) | cap about 600 | >= 1.67x | derived |
| Cordon dump frames | 300-780 per 10,000 (DESK, fixed-step data) | cut allowed, window >= 5,000 | cut >= 7,700 | derived, may cut |
| warm-up wall time | 48-58 s (34.5 s at 3.0 ms/frame) | >= 22.7 s | >= 1.5x | derived |
| p99 spread | +-3-6 % | +-12 % | 2x | derived from other arms' DATA (not their bounds) |
| unexplained share | Cordon 10-35 %, Rostok 25-55 % (GUESS) | 50 % | Rostok thin | derived |
| per-run share deviation | about 1 SE (0.05) | 3 SE + 1/n (0.15-0.18) | 3x | derived |
| train passes in window | 0 | 3 within 1 s gaps | - | derived (15 ms edge from MEASURED cp1-4) |
| run duration | 130-170 s | BENCH_TIMEOUT 900 s | >= 5x | COPIED (bench default) |
| series duration | 20-35 min | slice 3 h, stop at < 15 min left | >= 5x | COPIED (box rule, s4c_series.sh:61, 68) |
| settle | load < 2, no other >= 100 % process | 5 min cap | - | COPIED (s4c_series.sh:25-29) |
| re-run policy | 0-1 re-runs | once per tag | - | COPIED (s4c_series.sh:83) |
| hitch level | 0 frames > 100 ms in windows | reported only | - | COPIED (plan target) |

## 11. What this cannot show
See NOTES.md "What the profiler output cannot distinguish". In short: the main thread's wait on workers is not zoned;
"other" is all workers summed; only the top 14 self times per frame; time booked when a scope ends; GC is elastic;
no Present; one box, two saves, no movement.
