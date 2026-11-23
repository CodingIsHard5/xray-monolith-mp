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

## 8. Pre-run checks (CI 45090787686 green, 98a444975; installed as run/bin/Coarse14.exe; before any run)

- String check: 27 of the 30 hot site names are absent from Coarse14.exe and present in RefDet14.exe.
  R_constants::GetCBuffer, CSkeletonX::_Render and CUIWindow::Draw remain, but they are the function's own name.
  The compiler keeps one copy of that text for the probe and for __FUNCTION__ uses (asserts, _Render_soft, UI
  messages), so RefDet14 also shows a single copy, and the string check cannot decide for these three.
  AMENDMENT (before any run): for those three, the check is that they do not appear in any K run's perf_sites CSV;
  if one does, the K runs are void.
- Fingerprint (tb_hash.sh, bench1 + cordon1): equal to RefDet14 on every line except exe (65edcf401faeb125 vs
  bc0426a10810f461). Files out/coarse14/hash*.txt.

## 9. Series 20:55-21:13 and two re-runs (written before any result was read)

14 runs done, all rc 0. Two runs were marked "CONFOUNDED": r1_ros_k2 and r1_cor_k1 (both K). In both, the only
"heavy other" at the end was 100%:ps, i.e. the checker's own ps command seen by itself (start load 1.87 / 1.64, no
other process). Per section 2 they are EXCLUDED and re-run once. My series script had not implemented that re-run
(a defect: it marked them done). Fixed: r1_series.sh now queues "<tag>_rr" for any non-clean run (never a re-run of a
re-run). bench.sh's heavy-process check now ignores a process named exactly "ps". wineserver and Xvfb are matched as
before, and anything else at 100 %, psql included, still counts. Re-runs r1_ros_k2_rr and r1_cor_k1_rr run after the
fair-share gap. K group per scene = the three clean K runs (k1, k3, and the re-run). No split was computed before this
note.
Also corrected: series.log was created; I wrongly reported it missing (my listing filtered it out).

## 10. RESULT (scored once, 2026-11-22 22:10; r1_score.txt)

Checks: all 16 runs rc 0. The 14 scored slots are all "clean" (the 2 originals were flagged by the checker matching its
own ps, a checker defect, not real load: excluded and UNSCORED, shown in r1_score.txt for the record only). The three
pooled names are absent from every K run's perf_sites (0), and the same search finds all 3 in F (positive control).
K probe calls per frame are about 1,444 (Rostok) and 705 (Cordon), below 2,000. F Rostok frame 6.45 ms, within 5 % of the
baseline's 6.56.

| K means, main thread ms/frame | frame | feed | (a) lists | (b) submission | (c) rest | sun cascades (in b) |
|---|---|---|---|---|---|---|
| Rostok bar | 5.75 | 2.76 | 0.54 | 2.13 | 0.08 | 1.13 |
| Cordon | 4.27 | 1.85 | 0.14 | 1.64 | 0.07 | 0.49 |
F means: Rostok frame 6.45, (b) 3.03; Cordon frame 4.94, (b) 2.23. Probe overhead (F - K frame): 0.70 / 0.67 ms, i.e.
about 9 ns per removed call. 4 cores (one run each): K frame 7.03, (b) 2.06; F frame 7.59.

DECISION BY THE RULE: (a) < 1.0 in both scenes -> not list building. (b) >= 1.5 in both (2.13, 1.64) -> STEP 1
TARGETS SUBMISSION (D3D11 deferred contexts / command lists).

Wrong predictions, stated plainly: I predicted (b) 1.2 / 0.9 and DROP as more likely. Wrong: the probes cost about
9 ns per call, not 20-40, so far less of (b) was probe overhead. K Rostok frame 5.75 is above my 4.0-5.5 range.
Right: (a) out in both scenes; Cordon frame inside its range; F within 5 % of the baseline.

Caveats that bind step 1:
- (b) is an upper bound, and 1.13 / 0.49 ms of it sits under "Render Cascade" (sun shadow maps). That pass is driven
  from R_sun.cpp, gammavr's file. Without it, (b) is about 1.0 / 1.15. Section 4 says gammavr is asked first.
- Deferred contexts help only if recording is spread over threads AND the driver gains from it. That is the case
  under DXVK, as measured here. On old Windows drivers, deferred contexts are often emulated by the runtime, so the
  gain on goal B's machine is not known (choice 4).
