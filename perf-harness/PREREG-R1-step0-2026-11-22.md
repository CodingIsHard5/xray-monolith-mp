# R1 step 0: split of the main thread's render feed - pre-registration
# (2026-11-22, EnginePerf. Overseer approval of step 0 "as written" in PROPOSAL-first-candidate-2026-11-22.md.
#  Measurement only. Nothing run. No optimisation code.)

## 1. Why the build differs from the proposal's wording

The proposal said "split with low-overhead probes" and "remove the per-call probes on flush_cache and GetCBuffer".
Reading the existing capture (base1-3) showed the render zones ALREADY exist, and the problem is wider: the CSV
profiler makes about 80,000 probe calls per frame on the main thread, 78,600 of them at 30 sites with 50+ calls per
frame. Each call costs atomics and two time reads, so the per-draw numbers are inflated. So no probe is added. The
30 hot sites become PROF_EVENT_HOT, which compiles to nothing under /DXRCORE_PROFILER_COARSE. Without that define it
is identical to PROF_EVENT. Branch perf/render-split only; its Perf-Build sets the define. Engine source otherwise
equals RefDet14's (54a338e15; `git diff 54a338e15 -- src` was empty before this change). gammavr-2a was told first
and has no objection. None of its files are touched.

## 2. Exes and runs (tools/r1_order.txt, fixed here)

F = RefDet14.exe (every probe). K = Coarse14.exe (this branch, hot probes compiled out). Same source otherwise.
Before any run: "PROF_EVENT_HOT"-sited names (e.g. "R_constants::GetCBuffer") must be ABSENT from Coarse14.exe and
present in RefDet14.exe (string check), and Coarse14's fingerprint must equal RefDet14's except the exe line.
14 runs, one slice, tools/r1_series.sh (WANT, EXCLUSIVE, settle before each run, stop on any failed run):
Rostok bar (save bench1) F K K F F K; Cordon (cordon1) K F F K K F; then pinned to 4 cores (BENCH_LOWEND=1) on
bench1, K then F. Engine args as the baseline: warm-up 600 frames, 3000 frames, -perf_fixed_dt -perf_noinput
-perf_nopresent -perf_slow 20. A run whose VERDICT is not "clean" is excluded and re-run once at the end; it is never averaged in.

## 3. What is computed (tools/r1_split.py, committed with this note, dry-run on base1-3 / cord1-3 in r1_dryrun_baseline.txt)

Per run, main thread, ms per frame: (a) list building = sum of SELF of r_dsgraph_capture*, CDSGraphManager::traverse,
CSector::traverse, Render HOM. (b) submission = incl("IGame_Level::OnRender: Render") - (a), an upper bound.
(c) rest = incl("Process seqRender") - incl(OnRender). Also: frame mean, sun cascades incl (R_sun.cpp, gammavr's,
reported only), probe calls per frame. Group value = mean of the 3 K runs per scene. K is the instrument; F gives the
probe overhead (F frame - K frame).

## 4. Decision rule (as approved; applied to the K means)

- (a) >= 1.0 ms in BOTH scenes -> step 1 targets list building (parallel list build, fixed merge order).
- else (b) >= 1.5 ms in BOTH scenes -> step 1 targets submission (D3D11 deferred contexts / command lists).
- else -> DROPPED: the render feed is spread thin; that would be the local minimum, and I say so.
Any step 1 still waits for Caden's ruling (17:36 rule). If a step 1 target lies in R_sun.cpp, gammavr is asked first.

## 5. Desk prediction (before any run)

- From the dry run with all probes: (a) = 0.54 (Rostok) / 0.14 (Cordon). Removing probes cannot raise (a), so
  (a) < 1.0 in both scenes is PREDICTED with near certainty. List building is out.
- (b) with all probes = 3.05 / 2.23. Probe overhead: about 66,000 hot calls per frame land inside the render path. At
  20-40 ns each that is 1.3-2.6 ms. PREDICTED K values: (b) Rostok 0.8-1.8 (central 1.2), Cordon 0.6-1.4 (central 0.9).
  So DROP is predicted as more likely than not. A (b) target is possible only if a call costs less than about 20 ns.
- K frame mean Rostok 4.0-5.5 ms (F about 6.5); Cordon 3.5-4.5 (F about 5.0). K probe calls per frame below 2,000.
- F frame means within 5 % of the baseline (6.56 Rostok); otherwise the rig changed, and I report that before scoring.

## 6. Limits stated now

One scene each, player standing still, headless 1280x720, no present, under Wine/DXVK (not native D3D11). Profiler
on in both builds (K still has about 1,400 coarse calls per frame). It ranks; it gives no verdict on old hardware.

## 7. Ruling received after registration, before any run (Overseer relaying Caden, Discord 00:48 UTC)

Caden: "Go for it", to the question of whether R1's optimisation may go ahead before the rest of the plan is ruled on.
Scope as relayed: step 1 is approved ONLY if section 4's rule picks a target; DROP stays DROP; covers R1 only.
Nothing in sections 1-6 changes.
