# PREREG sf1: what the slowest 1 % of frames are made of (REGISTERED 2026-11-27 15:35 EST; no run before the Overseer's GO)

A measurement, not an optimisation: nothing passes or fails, but every run check and every no-verdict rule below is
fixed before any run. Nothing here has been run on the box. No box run without the Overseer's GO.
Source of the task: plan-facts/next-step-sizing-2026-11-26.md, runner-up "classify the steady 1 % slow frames".
Draft 2026-11-26 revised 2026-11-27 to the Overseer's confirmation and its 8 changes (NOTES.md section 1).
Labels: MEASURED (a number in a file, cited), DESK (my count from existing files), GUESS, COPIED (taken from an earlier
registration without re-deriving it).

**claim class: EXCLUSIVE (the measurement is frame timing).** What sf1 classifies is the slowest 1 % of frames. Another
process on any core can lengthen exactly those frames (a pre-empted main thread shows up as `main_unzoned_or_wait`, a
slowed worker as a longer wait), so a shared box would put foreign time into the very tail being ranked.

## 0. Limits first
- **PLAYER FRAMING.** On this PC a p99 of about 11 ms (Rostok, MEASURED section 3.3) is invisible to a player: it is
  still above 90 frames per second. sf1 serves the old-PC target (Caden's goal: smooth on ancient hardware for 1-4
  players), where the same work running 3-4x slower makes the same tail a 35-45 ms frame, a visible stutter. sf1 does
  not measure an old PC; it ranks what the tail is made of here, on the assumption (GUESS) that the same classes make
  the tail there.
- **Rankings only, as Caden ruled.** sf1 reports which classes of profiler zones rank largest among the slow frames. It
  does not say what causes a slow frame and it proposes no fix. A class is an attribution by a fixed rule (section 4),
  never a proven cause.
- One box: Ryzen 9800X3D (16 threads), Wine + DXVK, headless Xvfb 1280x720, no Present (`-perf_nopresent`: the frame
  ends in a wait for the GPU instead of a present), no input (`-perf_noinput`). Says NOTHING about the 2012 reference
  machine, native Windows, vsync, a real compositor or a 4-core box.
- Two saves, player standing still: cordon1 (Cordon, l01_escape, GammaVR's test save) and bench1 (Rostok, copy of
  Caden's "rodent1"). No combat, no movement, no level change, no menus.
- Profiler cost: the full-probe builds (RefDet14, all SimTick builds) make about 80,000 probe calls per frame, about
  0.7 ms (INFORMATION.md "R1 step 0"; DESK: s4c_noff_1 perf_sites 79,792 main calls/frame). This experiment uses
  Coarse14.exe, the hot probes compiled out: 1,444 (Rostok) / 712 (Cordon) main calls per frame (DESK, r1a_ros_k0_1 /
  r1a_cor_k0_1 perf_sites), about 0.01 ms at 9 ns per call (DESK). The price: render internals are coarser (30 hot
  sites folded into their parents' self time).
- Coarse14 is the perf/render-split build 98a444975 of 2026-11-22. Later engine changes (S2 tick, 4a-4c, fixes) are
  not in it. Frame-time equality between Coarse14 and today's builds is not shown.
- The profiler's slow-frame dump keeps only the 14 largest self times per frame across all threads, books a scope's
  time in the frame where it ENDS, has no zone for the main thread's wait on workers, and lumps all non-main threads
  into "other". NOTES.md section 5 lists what it cannot tell apart.
- Real time (stock frame loop). Coarse14 has never run in real time; the two probe loads (section 6.1) are the first time.

## 1. Question
In steady play, fixed tick OFF (stock frame loop, real time, uncapped), on two scenes: which share of the frames above
each run's own p99 falls into which class of work (rules in section 4), and how many ms each class adds over the
median. Reported as a ranking of classes.

## 2. What runs (flags copied from PREREG-S2-4c part B and wu3)
Exe `run/bin/Coarse14.exe` sha256 `65edcf401faeb125c68283084f55500c8d55cca58353e682f9ff8f96d884d88c`
(16-hex prefix registered in PREREG-R1-step0-2026-11-22.md line 70; full hash taken from the file).
Saves: cordon1.scop 6dea752b..., cordon1.scoc a9f9a8a9..., bench1.scop 891693c5..., bench1.scoc 7c2ddf33... (full
hashes pinned in sf1_run.sh). Settings: run/appdata/user.ltx as is, checked read-only: r__framelimit 0, no sim_tick.
Options file: run/gamedata/configs/axr_options.ltx restored from run/appdata/axr_options_play.ltx (sha256 27d54b9f...)
before every boot (section 7).

| switch / setting | source of the value (file:line) | what it produces |
|---|---|---|
| bench `tools/s2_bench.sh` (072af8ed) | tools/s4c_series.sh:74, wu3_run.sh | out/<tag>/conditions.txt (start/end loadavg and heavy_others, clean/CONFOUNDED verdict, peak RSS), wine_stderr.log, xray.log (copy of the engine log, incl. its `command line`), DXVK logs |
| `-perf_capture <warm>,<frames>,<tag>` (bench adds) | tools/s2_bench.sh:56 | perf_sites_<tag>.csv, perf_frames_<tag>.csv (frame,ms for every captured frame) |
| `-perf_quit`, `-nointro -dbg -start server(<save>/single/alife/load) client(localhost)` | tools/s2_bench.sh:55-56 | quits after the capture |
| BENCH_FIXED=0 (no `-perf_fixed_dt`: real time) | tools/s2_bench.sh:11, wu3_run.sh | checked in the engine's own command line (R8) |
| `-perf_noinput -perf_nopresent` | tools/s4c_series.sh:74 (default S2_BENCH_FLAGS) | nopresent makes the site "bench: wait for GPU (no present)" |
| `-perf_slow <slow_ms>`: 6.0 Cordon, 8.0 Rostok | derived 3.3 | perf_slow_<tag>.csv: `#threshold_ms`, then per frame above it the 14 largest self times; at most about 600 frames (profiler_agg.cpp:42-43, 190) |
| BENCH_EXCLUSIVE=1, BENCH_NOCLAIM=1, BENCH_LOWEND=0, BENCH_TIMEOUT=900 | wu3_run.sh (COPIED) | - |
| BENCH_DISPLAY=:141 | box rule (tools/t3_kw1.sh:36-42) | - |
| warm 14000, frames 10000 | derived 3.2, 3.3 | - |

The driver additionally writes out/sf1_state/{series.log, conditions.log, done.txt, stop.txt, sf1_order.txt (hash-
checked copy), check_<tag>.txt, run_<tag>.log}.

## 3. Design
### 3.1 Runs, re-runs and the re-run cap
Two probe loads (sf1_probe_cor on cordon1, then sf1_probe_ros on bench1, each with its scene's scored-run settings,
never scored, never re-run, section 6.1), then six scored runs, one WANT slice, order ABBAAB (sf1_order.txt, one source for driver and scorer): sf1_cor_1, sf1_ros_1, sf1_ros_2, sf1_cor_2, sf1_cor_3,
sf1_ros_3.

Re-run rule (wu3 3.1, COPIED): a scored run that ended rc 0, passed its per-load check and whose bench VERDICT is not
`VERDICT: clean` (another process >= 100 % of a core at start or end) is run once more right away as `<tag>_r1`, after
the normal settle and every pre-load gate. One re-run per slot; not clean twice = slot VOID, no third attempt. The probe
are never re-run. A crash, a timeout or a failed check is never re-run (fault stop).

**Cap R_MAX = 6 (derivation).** Inputs (MEASURED, wu3_series_log.txt, 2026-11-27): settle waits 0-195 s (mostly 15-30
s), load start to next load start 145-325 s for 20,000-frame Cordon loads, bench 122-130 s, so the part of a cycle
outside the bench is 15-203 s. sf1 loads are 24,000 frames (DESK): Cordon about 131-139 s of bench (MEASURED wu3: 20,000
frames in 97.7-102.6 s + 4,000 at 4.29-4.56 ms, minus 0.39 ms x 24,000 of probe cost, + 25-27 s bench overhead);
Rostok about 160 s (24,000 x 5.15-5.30 ms: SimTick's MEASURED mean 5.80-6.00 ms minus 0.7 ms, + about 35 s overhead
from s4c_noff_1: 15,000 frames in a 123 s bench). So a cycle is 150-365 s (worst 203 + 160 = 363). A load starts only
with >= 1020 s (BENCH_TO 900 + END_RESERVE 120) of the 3 h left: the last start must be by 9780 s.
- 8 loads + R re-runs, worst measured cycle (365 s) and a first settle at its 600 s cap: last start at
  600 + (7 + R) x 365 s. R = 6: 5345 s against 9780 s: **1.83x (UNDER 2x)**. Expected cycle (about 190 s),
  R = 6: 600 + 13 x 190 = 3070 s: 3.2x.
- Each scene needs all 3 runs valid (V0), so a cap below 6 would leave a not-clean slot VOID and the scene NO VERDICT
  without saving time the slice needs. So R_MAX = 6 = one per slot: the budget never binds before the per-slot rule. It
  is kept (and tested at R_MAX = 5) so that a later, longer order would be capped. When it binds, the stop is class box.

### 3.2 Warm-up exclusion: 14,000 frames before the capture (changed from 11,500; margin re-derived from real-time data)
- What must be excluded: the after-load A-Life warm-up (one ~300 ms frame, then a train of 22-52 ms frames).
- MEASURED (out/wu3_01_on ... wu3_32_on, Warm18.exe, cordon1, REAL TIME, capture from frame 0, the same frame count
  origin as `-perf_capture`'s warm-up): the last frame above 20 ms of each load came at 11.7-31.3 s of wall time after
  capture frame 0 (32 loads; the 16 stock-loop loads with the pass in play: 31.1-31.3 s; RESULT-wu3: "how long slow
  frames keep coming 20.4 s" after loading ends). The draft's game-time bound (22.7 s) was too low; the measured 31.3 s
  replaces it.
- MEASURED (same 32 loads): the first 14,000 frames took 71.8-75.4 s of wall time (11,500 frames: 61.0-64.0 s).
  Coarse14 has about 0.39 ms per frame less probe cost (DESK: 44,519 vs 712 main calls per frame at 9 ns, x 14,000 =
  5.5 s), so DESK 66.3-69.9 s.
- Margin: 66.3 / 31.3 = **2.12x**; on Warm18's own times 71.8 / 31.3 = 2.29x (11,500 frames gave 1.81x / 1.95x).
- Rostok: no capture from frame 0 exists on disk, so the Rostok train end is NOT measured (GUESS: like Cordon's, both
  are the same A-Life warm-up). DESK 14,000 frames at 5.10-5.30 ms = 71-74 s, 2.3x against Cordon's 31.3 s.
- Outcome checks that it worked: both probe gates (train rule on each probe's own capture) and run check R6. A train
  found voids the run and says the warm-up is short.

### 3.3 Capture length and slow thresholds (re-derived from real-time data)
- p99 of N frames leaves N/100 slow frames. N = 10,000 per run gives about 100 per run and 300 per scene: the binomial
  SE of a pooled share of 0.5 is 0.029, of a per-run share 0.05.
- `-perf_slow T` must be BELOW each run's p99 (else the frames between p99 and T have no zone rows) and the number of
  frames above T must stay under the dump cap of about 600 frames per capture (profiler_agg.cpp:43, 190).
- Cordon T = 6.0 ms. MEASURED real time (wu3 loads, Warm18, frames after 34 s of the capture, 10,000 each, 32 loads):
  p50 4.10-4.23 ms, p99 9.92-11.15 ms, 2.1-4.0 % of frames above 6 ms. Coarse14 (DESK, minus up to 0.39 ms): p99 about
  9.5-10.8 ms; frames above 6 ms <= 4.0 % = 400 per 10,000 < 600.
- Rostok T = 8.0 ms (changed from 9.0). MEASURED real time (s2_off_1, s2_off_2, s4a_noff_1, s4a_poff_1, s4b_noff_1,
  s4b_poff_1, s4c_noff_1, s4c_poff_1; SimTick, full probes, 12,000 frames each; computed from perf_frames_<tag>.csv
  2026-11-27): p50 5.56-5.67 ms, p99 11.35-12.14 ms; frames above 8.7 ms 2.02-3.67 %, above 9.7 ms 1.77-3.45 %. The
  tail has two humps: the main body ends near 7 ms and a slow hump sits above about 9 ms (8.7 -> 9.7 ms loses only
  0.14-0.77 points). SimTick's probes cost about 0.7 ms per frame more than Coarse14's (79.8k vs 712 main calls per
  frame at 9 ns), so Coarse14 at T = 8.0 corresponds to SimTick at 8.7, and SimTick's 9.7 to Coarse14's 9.0.
  - Dump frames: <= 3.67 % = about 370 per 10,000 against the cap of about 600: 1.63x. Overflow only cuts the window.
  - Coverage: window p99 >= T x 1.02 = 8.16 ms against an expected p99 of 10.65-11.44 ms (11.35-12.14 minus 0.7):
    **1.30x (UNDER 2x)** (it was 1.15x at T = 9.0).
  - Second form of the same margin: p99 lies in the slow hump as long as that hump holds more than 1 % of frames; it
    holds 1.77-3.45 % (SimTick > 9.7 ms, i.e. Coarse14 > 9.0 ms): **1.77x (UNDER 2x)**. If it held less than 1 %,
    p99 would fall into the main body (7-8 ms) and below 8.16 ms: coverage_p99 would void the run.
- Limit (fixed step is a different loop): the Coarse14 FIXED-STEP captures (3,000 frames each, MEASURED) have far more
  slow frames: r1a_ros_k0_1/2/3 6.3-7.4 % above 9 ms and 8.0-9.0 % above 8 ms; r1a_cor_k0_1/2/3 4.5-7.8 % above 6 ms.
  If real-time Coarse14 looked like that, the dump cap would cut the window: Rostok 800-900 slow frames per 10,000 fill
  600 dumps after about 6,700-7,500 frames, Cordon 780 after about 7,700 (the window is still >= 50 %, allowed). The
  50 % rule (MIN_WINDOW) voids a run when 600 slow frames fall inside the first 5,000 captured frames: a share above
  600 / 5,000 = **12 %** of frames above T, evenly spread (less if the slow frames bunch early). No real-time capture on
  disk comes near that (max 4.0 % Cordon > 6 ms, 3.67 % Rostok-equivalent > 8.0 ms).

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
   that class (gc counts as unexplained: the zone grows with the frame, so the rule cannot rank it either way).
2. Else if the main thread's unzoned time grew by >= 0.5 x E, the frame is `main_unzoned_or_wait`. Unzoned growth =
   (ms - capture mean frame) - (sum of signed main-thread deltas of the dumped rows); sites not in the dump are taken at
   their mean. It lumps the join wait on workers, unzoned main code and OS/Wine pre-emption: NOT separable.
3. Else `unknown`. A slow frame without rows (between p99 and T x 1.02) is `no_data`.
Explained = alife, script, render, gpu_wait, online_sim, io_stream, sound, other_known.
Unexplained = gc, main_unzoned_or_wait, unknown, no_data.

### 4.4 Reported per scene (3 runs pooled)
Per class: number of slow frames, share, mean ms over p50, ms over p50 per 1000 window frames, share of the class score
on the main thread; the classes ranked by share. Per run p50/p99/max/frames > 100 ms and the bench's start / end
heavy_others (so the result says whether busy cores were seen). Reported only: median wall time between consecutive
alife-class slow frames.

## 5. Constants (each re-derived for sf1 in section 10)
THETA 0.5; UNEXPLAINED_MAX 50 %; DOMINANT pooled share - 2 SE > 50 %; P99_SPREAD +-12 %; SHARE_Z 3 SE + 1/n;
SHARE_CHECK_MIN 10 %; COVER_TOL 2 %; MIN_WINDOW 50 %; TRAIN 3 frames with ALife/* self >= 15 ms, gaps <= 1000 ms;
ROW_MS_TOL 0.0005 ms; PROBE_P50 cordon1 [1.5, 8.0] ms, bench1 [2.0, 10.0] ms; HITCH_MS 100 ms (reported only); R_MAX 6.

## 6. Checks (each tests an outcome inside the run's own files)
### 6.1 PROBE GATE (driver, before any scored run; the scorer applies the same rules again)
Two probe loads, first in the order, each never scored and never re-run: sf1_probe_cor (cordon1, Coarse14, warm 14000,
frames 10000, slow 6.0: exactly a scored Cordon run), then sf1_probe_ros (bench1, Coarse14, warm 14000, frames 10000,
slow 8.0: exactly a scored Rostok run). Right after each, `sf1_classify.py probe=out/<probe tag> order=<state copy>`
checks it with its own scene's values (each failed check named, every one printed):
- `missing` / `empty` / `malformed`: perf_frames, perf_sites, perf_slow, conditions.txt, xray.log present, non-empty,
  parseable.
- `framecount`: frames file rows = sites `#frames` = 10000. `threshold_header`: perf_slow `#threshold_ms` = 6.000
  (Cordon) / 8.000 (Rostok).
- `slow_rows`: perf_slow has rows; every row's frame_ms equals the frames file's ms for that frame (+-0.0005; the
  difference is 0 in every perf_slow on disk, DESK scan) and is >= T x 0.98.
- `stock_loop`: "Process seqFrame" main calls = frames; "Sim tick" absent or 0 calls. `frame_limiter`: "Eco Render"
  absent or 0 calls.
- `cmdline` (R8): the engine's own `command line` in xray.log names Coarse14.exe, `server(<save>/single/alife/load)`,
  `-perf_capture 14000,10000,<probe tag>`, `-perf_slow 6.0` / `8.0`, `-perf_noinput`, `-perf_nopresent`, `-perf_quit`,
  `-nointro`, and NOT `-perf_fixed_dt` (this is the real-time check on the engine's side).
- `fatal` (R9): no line of xray.log contains `FATAL` (case-sensitive; the lowercase script name
  modxml_lua_fatal_window is in every log and is not matched).
- `zones`: "Process seqFrame", "bench: wait for GPU (no present)", "seqLuaGC" and at least one site of each class the
  map relies on (alife, gc, gpu_wait, script, io_stream, sound, render, online_sim) appear in perf_sites with calls
  (DESK: every class is present in all 9 Coarse14 / SimTick / Warm18 sites files checked).
- `frame_range` Cordon (sf1_probe_cor): median captured frame in [1.5, 8.0] ms. Derivation: expected 3.7-3.8 ms (MEASURED Coarse14 Cordon
  fixed-step p50 3.74-3.79, r1a_cor_k0_1/3; Warm18 real-time 4.10-4.23 minus the probe cost). Upper 8.0 ms: below
  8.33 ms, so any frame cap at 120 fps or slower, a vsync-like wait or a far slower path (software rendering, GUESS
  >= 20 ms) fails; margin 8.0 / 4.23 = 1.89x against the slowest real-time Cordon median on disk (Warm18), 2.11x
  against Coarse14's. Lower 1.5 ms: frames that render no level are far faster (MEASURED: the loading frames at the
  start of the wu3 captures are 0.05-0.10 ms); margin 3.74 / 1.5 = 2.49x.
- `frame_range` Rostok (sf1_probe_ros), its own derivation: median captured frame in [2.0, 10.0] ms. Expected about
  4.9-5.1 ms (MEASURED: Coarse14 Rostok fixed-step p50 5.02-5.08, r1a_ros_k0_1/2/3; SimTick real-time p50 5.56-5.67,
  8 captures, minus its 0.7 ms probe cost = 4.86-4.97 DESK). Upper 10.0 ms: a 60 Hz wait (16.7 ms) or a far slower
  path (software rendering, GUESS >= 20 ms) fails; margin 10.0 / 5.08 = 1.97x against the highest Coarse14 median on
  disk, 1.76x against SimTick's real-time 5.67 (the slowest real-time Rostok median on disk, with 0.7 ms more probe
  cost). A 100-120 fps cap (8.3-10 ms) is NOT excluded by the range in Rostok (it is in Cordon): the engine's own
  limiter is caught by `frame_limiter`, and `-perf_nopresent` means no Present, so no display wait. Lower 2.0 ms:
  frames that render no level are far faster (0.05-0.10 ms, MEASURED on Cordon; Rostok GUESS the same); margin
  4.86 / 2.0 = 2.43x. The range cannot tell the two saves apart (Cordon's 3.7 ms is inside it): `cmdline` does that.
- `window_short`, `coverage_p99`, `train`: the window rules of 4.1 / R6 / R7 on each probe's own capture.
- Not checked: the VERDICT (a not-clean probe is allowed, as wu3's prime).
Any failed check of EITHER probe: the series stops before any scored run (the Rostok probe is not booted after a
failed Cordon probe), stop record `class=fault reason=PROBE FAILED on <probe tag>, series stopped before any scored run:
FAIL [<codes>] <first two checks in full>`. A scored load is refused by the gates unless both probes passed (the
driver counts passed probes; a scored load with fewer than 2 is a fault stop). The scorer also re-runs both gates (V-1).

### 6.2 Per run (any failure voids the run, every reason is listed by code)
- R0 `missing`/`empty`/`malformed`/`slow_rows` as 6.1. R1 `framecount`. R2 `threshold_header`. R3 `stock_loop`.
  R4 `frame_limiter`. R8 `cmdline`. R9 `fatal`. (The driver's per-load check `check=` applies exactly R0-R4, R8, R9 and
  stops the series as fault on any of them.)
- R5 `not_clean`: bench verdict clean, else read `<tag>_r1`; if that is missing or not clean: void.
  `unexpected_rerun`: `<tag>_r1` next to a clean `<tag>` voids the slot (the driver never does that).
- R6 `train`: no after-load train in the window. R7 `coverage_p99` / `window_short`: window p99 >= T x 1.02;
  window >= 50 % of frames.
### 6.3 Per scene
- V-stop `stop`: the scorer reads the driver's stop record `<folder of order=>/stop.txt` (out/sf1_state/stop.txt) and
  prints it. Missing, not exactly one physical line (counted in bytes: the file must be exactly one line ended by LF,
  CRLF allowed; a blank second line fails), not of the 6.4 form, or a class other than `done` -> NO VERDICT [stop]
  for every scene, by name.
- V-1 `probe` (decided: per scene): a scene whose OWN probe (sf1_probe_cor for Cordon, sf1_probe_ros for Rostok) fails
  6.1 or is missing gets NO VERDICT [probe]; the other scene is judged on its own probe. Reason: each probe checks
  only its own scene's save, threshold and capture, so a Rostok fault says nothing about Cordon's runs. In a real
  series this cannot differ from "void both": the driver stops before any scored run on either failed probe, so the
  other scene has no runs either (V0). It matters only for an incomplete record, which V-stop already voids.
- V0 `runs`: all 3 runs valid, else NO VERDICT for the scene.
- V1 `p99_spread`: else NO VERDICT (not repeatable).
- V2 `coarse`: unexplained share > 50 % -> NO VERDICT: "the profiler zones are too coarse to say what the tail is
  made of". A result about the tool, reported as such, no class ranking claimed.
- V3 `share_repeat`: else NO VERDICT on the composition (shares do not repeat across runs).
- Then RESULT: DOMINANT <class> (pooled share - 2 SE > 50 %) or NO SINGLE DOMINANT CLASS with the largest two named.
### 6.4 Stop record (driver)
`out/sf1_state/stop.txt`, one line `class=<done|box|fault>[ interrupted=<tag>] reason=<text>` (wu3 section 3 classes;
`interrupted=` only with box, when a cancel came during a bench). The scorer requires class=done (6.3 V-stop).
box: settle cap (600 s), re-run budget used, slice time used / too short, protected title (or the title check failed),
Roblox, Caden's launcher, claim renewal refused, cancelled. fault: crash / timeout / no capture, failed check, failed
probe, options restore refused, state write failure, exe / settings / display problems, existing out folder, order copy.
done: the list finished. A stopped series is not resumed without a new GO.

## 7. Driver (sf1_run.sh, DRAFT; `bash -n` and the self-test's offline function tests only)
Made from wu3_run.sh (newest registered series driver) with sf1's order, checks and names.
- Hash preflight of bench, its two helpers, exe, four save files, the options source, order, scorer, self-test (the
  last three are PIN_ME placeholders: the preflight REFUSES until registration fills them); display :141 only, refused
  if /tmp/.X11-unix/X141 or /tmp/.X141-lock exists; user.ltx read-only check; out/<tag>, out/<tag>_r1 and out/sf1_state
  must not exist; the order is copied into the state folder, the COPY is hash-checked, made read-only and is the only
  order read afterwards.
- **Claim by exit code** (quiet-required.sh header: "THE EXIT CODE IS THE API. DO NOT PARSE THE OUTPUT"):
  `quiet-required.sh claim engineperf 30 "<what>" >/dev/null 2>&1` and its exit status, both at the slice claim and at
  the per-run renewal; want / withdraw / release likewise. Claim handoff (kj4p, amendment 1): while a claim under our
  worker name that is NOT the queue's "handoff pre-claim" is live, the driver waits instead of claiming (a claim then
  would renew or nest into it). That test reads `check`'s text; it never decides a claim (NOTES.md section 1, point 5).
- **Options file restore before EVERY boot**: after the claim renewal (claim held), before the slice recheck and the
  bench, the driver hashes run/appdata/axr_options_play.ltx against 27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29
  (refused with a named fault stop if it differs; the destination is then untouched), copies it over
  run/gamedata/configs/axr_options.ltx and hashes the destination (em1/t2_xray_em1.sh:57-70). Why before every boot,
  not once: each exe rewrites that file's `version` line when it boots (wu3 RESULT; Coarse14 too, GUESS: it is a
  2026-11-22 build like Stance14), and between two sf1 loads nothing holds the file. Restoring each time costs two
  hashes and one 227 KB copy and makes every boot, the probes' included, start from the same bytes.
- Gates before every load (wu3 order): settle (1-min load < 2 and no other process >= 100 %, 15 s checks, 600 s cap,
  COPIED from wu3), slice time, protected title, Roblox, Caden's launcher, display, settings, out folder new, exe pin,
  claim renewal, options restore, slice recheck (>= 1020 s), both probes passed (scored loads only).
- **Conditions per load (fleet desk rule)**: the settle line (load, heavy processes) before each load; after each bench
  its conditions.txt start / end lines (loadavg, heavy_others) and VERDICT into series.log and out/sf1_state/
  conditions.log; the scorer prints the heavy_others of every run it uses.
- Stop record on every exit (6.4); bench reaped, WANT entry withdrawn, slice released only if this invocation claimed it.

**Slice length estimate (<= 3 h).** 8 loads (2 probes + 6) + 0-6 re-runs. Per load (section 3.1): settle 15-195 s
(MEASURED wu3), display wait <= 20 s, bench about 131-139 s Cordon / about 160 s Rostok (DESK). Each bench: load and
quit overhead about 25-35 s + warm-up 14,000 frames at real-time speed (Cordon 66.3-69.9 s DESK, Rostok about 71-74 s
DESK) + capture 10,000 frames (Cordon about 39-42 s, Rostok about 52 s). Expected (30 s settles, no re-run): about
24 min; with all 6 re-runs about 43 min; worst measured cycles (365 s), a 600 s first settle and 6 re-runs: last start
at 5345 s, end about 97 min (1.85x under 3 h). Hard bound by construction: no load starts
with < 1020 s of the 3 h left, and that reserve covers one bench timeout (900 s) and the teardown (120 s): <= 3 h.

## 8. Scorer and self-test
`sf1_classify.py` (stdlib, python3 -I): score mode, `probe=` (6.1) and `check=` (6.2 R0-R4, R8, R9) modes.
`sf1_selftest.sh`: run `cd wt-render-split/perf-harness/sf1 && nice -n 19 bash sf1_selftest.sh` (the run folder goes
under sf1/selftest-runs/). Contents: scorer cases on real run folders read in place and on fixtures made from real
frame / site files; probe-gate and check cases for both probes (real folders where possible, each fail case named);
stop-record cases (missing, blank second line, two lines, no line end, box, fault, box interrupted, done with
interrupted=, malformed, CRLF done); 42 checker mutants
(a scorer copy with one rule removed or one constant moved; each must be caught by a listed case); the driver's
function block run offline with stubs (order shape and copy, options restore on a scratch layout, series loop incl. the
probe gate, re-run rule, stop record, conditions log, claim by exit code against a stub that prints decoy text, the
handoff pre-claim, the both-probes gate) with 24 driver mutants and R_MAX = 5; static checks of the driver body; an untouched check of the
real fixture folders and the real options files. Self-test result and hashes: section 11.
Score ONCE after the series, output in the same call, after checking the sha256 of both order copies (tree and state)
by hand against the registered order hash:
`python3 -I wt-render-split/perf-harness/sf1/sf1_classify.py root=out order=out/sf1_state/sf1_order.txt out=out/sf1_state/sf1_score.txt`

## 9. Predictions
EnginePerf, written before any sf1 boot. Not blind: I have seen the wu3 real-time Cordon loads (Warm18), the eight
real-time Rostok captures (SimTick) and the Coarse14 fixed-step captures. Coarse14 has never run in real time.
- Both probes pass their gate: 75 %. If one fails: Rostok (train rule, since when Rostok's after-load train ends is a
  guess; or coverage) more likely than Cordon.
- Given both probes pass: Cordon gets a result (not NO VERDICT) 55 %; Rostok 40 %.
- Most likely NO VERDICT reason: V2 `coarse` (unexplained share over 50 %), Rostok 45 %, Cordon 25 %.
- Cordon: the largest class is `alife` 60 % (1 % of frames at about 4 ms is one frame per 0.4 s, close to the 470 ms
  A-Life pass interval); DOMINANT alife 40 %. Second: `main_unzoned_or_wait`.
- Rostok: the largest class is `main_unzoned_or_wait` 35 %, `alife` 30 %, another 35 %. No DOMINANT class: 70 %.
- Numbers: Cordon p50 3.7-3.9 ms, p99 9.5-10.8 ms; Rostok p50 4.9-5.1 ms, p99 10.6-11.5 ms. No frame over 100 ms in
  any window. No window cut by the dump cap (80 %). Re-runs: 0 or 1.
- Slice: 24-45 min.

## 10. Margin table (every bound and constant re-derived for sf1; flag = margin under 2x, or COPIED)
| item | expected (source) | bound | derivation of the bound | margin | flag |
|---|---|---|---|---|---|
| warm-up 14,000 covers the after-load train (Cordon) | warm-up 66.3-69.9 s (DESK from MEASURED 71.8-75.4 s, wu3 x32) | last frame > 20 ms at <= 31.3 s (MEASURED wu3 x32) | the train must end inside the warm-up | 2.12x (2.29x on Warm18 times) | - |
| warm-up covers the train (Rostok) | 71-74 s (DESK) | Rostok train end NOT measured (GUESS: Cordon's 31.3 s) | as above | 2.3x | **GUESS** |
| Cordon T vs p99 | p99 9.5-10.8 (DESK from MEASURED 9.92-11.15, wu3 x32) | p99 >= 6.12 | T x (1 + COVER_TOL) | 1.55x | **UNDER 2x** |
| Rostok T 8.0 vs p99 | p99 10.65-11.44 (DESK from MEASURED 11.35-12.14, s2/s4 x8, minus 0.7) | p99 >= 8.16 | T x (1 + COVER_TOL) | 1.30x | **UNDER 2x** |
| Rostok slow hump vs p99 | 1.77-3.45 % of frames in the slow hump (MEASURED SimTick > 9.7 ms = Coarse14 > 9.0, x8) | > 1 % | p99 must fall in the hump, above 8.16 | 1.77x | **UNDER 2x** |
| Cordon dump frames | <= 400 per 10,000 (MEASURED 2.1-4.0 % > 6 ms, wu3) | about 600 | profiler_agg.cpp:43, 190 | 1.5x | **UNDER 2x** (a cut is allowed, MIN_WINDOW) |
| Rostok dump frames | <= 370 per 10,000 (MEASURED 2.02-3.67 % > 8.7 ms SimTick = Coarse14 > 8.0) | about 600 | as above | 1.63x | **UNDER 2x** (a cut is allowed) |
| dump share that voids a run | <= 4.0 % real time (MEASURED); 4.5-9.0 % fixed step (MEASURED r1a_*_k0) | 12 % (600 in the first 5,000 frames) | MIN_WINDOW x cap | 3x real time / 1.33x vs fixed step | **UNDER 2x vs fixed step** |
| MIN_WINDOW 50 % | no cut expected (dump frames < 600) | window >= 5,000 frames | keeps >= 50 slow frames per run: per-run share SE <= 0.07 | 2x | - |
| Cordon probe frame_range upper | p50 3.7-3.8 (Coarse14), 4.10-4.23 (Warm18) | <= 8.0 ms | under a 120 fps cap (8.33 ms) | 2.11x / 1.89x | **UNDER 2x vs Warm18** |
| Cordon probe frame_range lower | p50 3.74-3.79 (MEASURED r1a_cor_k0) | >= 1.5 ms | non-level frames 0.05-0.10 ms (MEASURED wu3) | 2.49x | - |
| Rostok probe frame_range upper | p50 5.02-5.08 (MEASURED Coarse14 fixed step), 5.56-5.67 (MEASURED SimTick real time) | <= 10.0 ms | a 60 Hz wait (16.7 ms) and software rendering fail | 1.97x / 1.76x | **UNDER 2x** |
| Rostok probe frame_range lower | p50 4.86-4.97 (DESK) | >= 2.0 ms | non-level frames 0.05-0.10 ms (MEASURED Cordon; Rostok GUESS) | 2.43x | - |
| P99_SPREAD +-12 % | half-range +-5.8 % Cordon real time (wu3 x32, 9.92-11.15), +-3.4 % Rostok real time (x8) | +-12 % | 2 x the largest half-range seen in groups of one build and scene | 2.07x | - |
| SHARE_Z 3 SE + 1/n | per-run deviation about 1 SE (GUESS) | 0.15-0.18 | about 30 comparisons: Bonferroni at 5 % gives z about 2.9; 1/n for discreteness at n about 100 | 3x | GUESS |
| THETA 0.5 | - | half of E | the only share that one class alone can hold with no other also reaching it | - (a rule) | - |
| UNEXPLAINED_MAX 50 % | Cordon 10-35 %, Rostok 25-55 % (GUESS) | 50 % | above it no class can be shown to hold a majority of the tail | Rostok under 1x possible | **GUESS, thin** |
| DOMINANT | - | share - 2 SE > 50 % | "most of the slow frames" with sampling error | - (a rule) | - |
| COVER_TOL 2 % | slow rows exist for every frame > T in all perf_slow on disk; one row at exactly T (s2_c50) (DESK) | 2 % of T | engine decides "slow" with a 10 ms clock calibration (profiler_agg.cpp:84-91) | not measurable (no counter-example on disk) | COPIED from the draft |
| ROW_MS_TOL 0.0005 ms | max difference 0 (DESK, every out/*/perf_slow) | 0.0005 | half the last printed digit | infinite | - |
| TRAIN 3 x >= 15 ms, gaps <= 1 s | wu3 OFF loads with the train: >= 12 frames (wu3_02_off); lever-on loads 1-2 (wu3_09_on: frames 14, 16) (DESK) | 3 | 15 ms = lower edge of a train pass (INFORMATION.md cp1-4); 1 s = 2 x the 470 ms pass interval + margin | 1.5x on the 2-frame lever-on loads | **UNDER 2x**, 15 ms COPIED |
| R_MAX 6 | 0-1 re-runs (GUESS) | 6 re-runs | one per slot; 8 loads + 6 fit (3.1) | 1.83x (worst) / 3.2x (expected) on the last start | **UNDER 2x (worst)** |
| settle cap 600 s | 15-195 s (MEASURED wu3) | 600 s | wu3 (changed there from 300 s after a 301 s first settle) | 3.1x | COPIED |
| BENCH_TIMEOUT 900 s | 131-160 s (DESK) | 900 s | bench default | 5.6x | COPIED |
| END_RESERVE 120 s | - | 120 s | wu3 | - | COPIED |
| slice 3 h | 24-97 min (section 7) | 3 h | box rule | 1.85x worst / 7.5x expected | **UNDER 2x (worst)**, COPIED (rule; hard bound by the 1020 s start rule) |
| not-clean rule | - | another process >= 100 % of a core at bench start or end | tools/s2_bench.sh | - | COPIED |
| HITCH_MS 100 ms | 0 frames > 100 ms in windows (wu3 steady max 16.7 ms) | reported only | plan's hitch target | - | COPIED |

## 11. Pins
Registered 2026-11-27 15:35 EST. This file keeps its name (`...-DRAFT.md`) because the pinned order file and scripts name it; the
status is the title line. It is pinned by its commit.
| file | sha256 |
|---|---|
| sf1/sf1_run.sh (driver; carries the order, scorer, self-test, bench, exe, save and options pins) | 3e0047b205eadca34e804cde39e612d05a561d27b7711ce4e16ffa6d6a73b022 |
| sf1/sf1_order.txt | ef87cbe894a543ba6e080d3300269ca36cf05e92235f32153ec775a87ca5df7a |
| sf1/sf1_classify.py | 5d67ba2abedb84d3bc8f2229a9dbb4d83cd81c199fd8feedd43b6ae079e72eb1 |
| sf1/sf1_selftest.sh | dfe0aef92856ebb9670c45b02462a27cbf65b0ff313ad43d4092bcc5ac8e05c5 |
| run/bin/Coarse14.exe | 65edcf401faeb125c68283084f55500c8d55cca58353e682f9ff8f96d884d88c |
| tools/s2_bench.sh | 072af8edb67eab095ea4408f3950a428709757b3ce1abdfe25efc25a3fb150b5 |
| run/appdata/axr_options_play.ltx | 27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29 |
Self-test on these bytes (2026-11-27 15:34:05-15:34:55 EST, run by EnginePerf, nice 19, no EXCLUSIVE claim held):
`SF1 SELFTEST PASS 193/193` (7 real-folder scorer cases, 67 made scorer cases, 48 gate/check cases, 42 checker mutants
caught, 28 driver checks incl. 24 mutants, 1 untouched check), folder sf1/selftest-runs/20261127-153405-3126782.
`sf1_run.sh --preflight-only` on the box at 15:33 EST: `preflight ok` (every pin above and in the driver matches on disk;
display :141 free; out/sf1_state and the eight out/<tag> folders absent).
Decisions taken at registration (EnginePerf): a failed probe voids only its own scene in the scorer (the driver stops the
series either way); Rostok's probe frame range upper bound 10.0 ms is accepted at 1.76x. Not independently reviewed
(CodeRabbit is down); the driver has never run on the box as a whole.

## 12. What this cannot show
See NOTES.md section 5. In short: the main thread's wait on workers is not zoned; "other" is all workers summed; only
the top 14 self times per frame; time booked when a scope ends; GC is elastic; no Present; one box, two saves, no
movement; a ranking of classes, not causes.
