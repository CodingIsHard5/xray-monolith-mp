# sf1 desk notes (2026-11-26, revised 2026-11-27): guesses, open points, fixtures, limits of the profiler output

## 1. Decisions: confirmed by the Overseer (2026-11-27) and what changed
Confirmed as drafted: Coarse14.exe; real time (BENCH_FIXED=0); threshold 6.0 ms Cordon; saves cordon1 + bench1,
3 runs each; EXCLUSIVE. Changes made 2026-11-27 on the Overseer's order (round 1, items 1-8; round 2, items 9-12):
1. Claim by EXIT CODE: `claim ... >/dev/null 2>&1` and its status, at the slice claim and the per-run renewal; the
   draft's `check | awk HELD` before `want` is gone (want is sent on every failed claim, as wu3).
2. PROBE GATE: an unscored probe load first (sf1_probe, Cordon, scored-run settings); `sf1_classify.py probe=` checks
   its own output (prereg 6.1); a failed check stops the series before any scored run (class=fault, checks named).
3. OPTIONS FILE RESTORE before EVERY boot, after the claim renewal (prereg 7: why every boot).
4. Conditions per load incl. heavy_others (series.log, conditions.log, and in the score); wu3's re-run rule (`<tag>_r1`,
   one per slot, cap R_MAX = 6 derived in prereg 3.1). The draft's `_rr` re-run at the end of the list is gone.
5. Player framing and "Rankings only, as Caden ruled" in prereg 0; cause wording removed (gc note in 4.3, the B2b
   inference moved here, section 6).
6. Margin table re-derived (prereg 10) with real-time data that did not exist when the draft was written: the 32 wu3
   loads of 2026-11-27 (Warm18, cordon1, real time). Claim class line and slice estimate added.
7. Predictions and pins left for EnginePerf / registration.
8. New self-test (prereg 8).
9. Warm-up 14,000 frames for every load (was 11,500): Cordon margin 2.12x (DESK; 2.29x on Warm18's own times).
   Bench, cycle, R_MAX and slice estimate re-derived (prereg 3.1, 3.2, 7).
10. Rostok T = 8.0 ms (was 9.0). Checked from the files: SimTick real time > 8.7 ms 2.02-3.67 %, > 9.7 ms 1.77-3.45 %
   (the coordinator's 2.0-3.7 / 1.8-3.5 confirmed). Coverage bound 8.16 ms against expected p99 10.65-11.44: 1.30x;
   hump share 1.77-3.45 % against the 1 % p99 needs: 1.77x; dump <= about 370 of 600: 1.63x (prereg 3.3, with the
   fixed-step limit and the 12 % share at which MIN_WINDOW voids a run).
11. Second probe sf1_probe_ros (bench1, Rostok's scored settings) right after sf1_probe_cor (renamed from sf1_probe).
   Both must pass before any scored run (driver counts passed probes; fewer than 2 at a scored load = fault). Rostok's
   own frame_range [2.0, 10.0] ms (prereg 6.1). Scorer V-1 decided PER SCENE: a scene's own failed / missing probe
   voids only that scene (the driver stops either way, so in a real series both scenes end without runs).
12. The scorer reads out/sf1_state/stop.txt (the folder of order=), prints it, and gives NO VERDICT [stop] for every
   scene if it is missing, not exactly one physical line (byte count: one LF at the end, CRLF allowed, a blank second
   line fails), malformed, or not class=done.

Open points for the Overseer (decide before registration):
- **Rostok T = 8.0 margin 1.30x (coverage) / 1.77x (hump share).** Still under 2x. If Coarse14's real-time Rostok
  p99 is below 8.16 ms, the Rostok probe fails coverage_p99 and the series stops before any scored run (fault).
- **Rostok warm-up not measured.** No Rostok capture from frame 0 exists; the Rostok train end is assumed to be like
  Cordon's 31.3 s (GUESS). The Rostok probe's train check is the outcome check.
- **Rostok frame_range upper 10.0 ms (1.76-1.97x).** Does not exclude a 100-120 fps cap in Rostok (Cordon's 8.0 does);
  frame_limiter and -perf_nopresent cover that (prereg 6.1).
- **Claim handoff text read.** The kj4p guard (wait while a non-handoff claim under `engineperf` is live) reads
  `quiet-required.sh check` text; quiet-required.sh has no exit-code verb for "who holds it". It never decides a claim
  (a failed parse = no wait = wu3's behaviour). Remove it if the Overseer wants zero text reads.
- (Closed in round 2: the scorer now reads the stop record.)

## 2. Every flag and value, and where it came from
| item | value | taken from (file:line) | confirmed by a previous prereg? |
|---|---|---|---|
| bench | tools/s2_bench.sh 072af8ed... | tools/s4c_run.sh:7 pin, tools/s4c_series.sh:74 | yes (S2-4c part B) |
| `-perf_noinput -perf_nopresent` | | tools/s4c_series.sh:74 default S2_BENCH_FLAGS | yes |
| `-perf_slow` | 6.0 Cordon / 8.0 Rostok | sizing study line 170 suggests 9; values derived here (Rostok 8.0 on the coordinator's order, checked in prereg 3.3) | NO (every run on disk used 20; float parsing: profiler_agg.cpp:80 `%f`) |
| `-perf_capture W,N,tag -perf_quit -nointro -dbg -start server(<save>/single/alife/load) client(localhost)` | | tools/s2_bench.sh:55-56 | yes |
| BENCH_FIXED | 0 | tools/s4c_order.txt:3-12 (fixed column), tools/s2_bench.sh:11 | yes for SimTick builds; NO for Coarse14 |
| BENCH_LOWEND | 0 | tools/s4c_order.txt (lowend column) | yes |
| BENCH_EXCLUSIVE=1, BENCH_NOCLAIM=1 | | tools/s4c_series.sh:74 | yes |
| BENCH_TIMEOUT | 900 | tools/s2_bench.sh:12 default | yes (default) |
| BENCH_DISPLAY | :141 | tools/t3_kw1.sh header + :36-42 (refusal) | not in a bench-harness prereg (they used the default :77); :141 is the box rule |
| warm, frames | 14000, 10000 | derived (prereg 3.2, 3.3; 14,000 on the coordinator's order) | NO (S2-4c: 3000, 12000) |
| saves | cordon1, bench1 | tools/r1_order.txt (Coarse14 lines), PREREG-R1-step0 | yes; "bench1 = Rostok bar": INFORMATION.md:123/137 say Rostok, standing still; "bar" is from the brief, not seen in a file |
| save hashes | full sha256 in sf1_run.sh | computed today; cordon1.scop prefix 6dea752bc652a45c matches out/cordon1_before_pfC.sha and tools/t3_kw1.sh pin | bench1 hashes: no earlier pin found |
| user.ltx | r__framelimit 0, no sim_tick, read-only check | run/appdata/user.ltx:553 today | the S2 series SET these; I only check |
| settle (600 s cap), re-run once as <tag>_r1, slice 3 h, stop record | | wu3_run.sh (registered 2026-11-27) | yes (copied); R_MAX 6 derived (prereg 3.1) |
| options restore | axr_options_play.ltx sha256 27d54b9f... | em1/t2_xray_em1.sh:57-70; Overseer order 2026-11-27 | the hash: yes (em1); the per-boot placement: NO (new) |
| probe gate frame range | [1.5, 8.0] ms | derived (prereg 6.1) | NO |
| Caden's launcher lock wait | | tools/s4c_series.sh:58 | yes |

## 3. Where I am guessing
- Probe cost moves Coarse14's frames down by the call count x 9 ns: 0.39 ms vs Warm18 (Cordon), 0.7 ms vs SimTick
  (Rostok), and shifts p99 like the median. Coarse14's real-time Cordon p99 9.5-10.8 ms, Rostok 10.6-11.5 ms, and
  warm-up wall times 56.5-59.5 s / 59-61 s all rest on it. (Supported for the median: Coarse14 fixed-step Cordon p50
  3.74-3.79 vs Warm18 real-time 4.10-4.23.)
- That the real-time Cordon numbers of Warm18 (a 2026-11-26 build) carry to Coarse14 (2026-11-22) apart from probes.
- That Coarse14 rewrites axr_options.ltx's version line at boot like Stance14 / Warm18 (not seen; the restore before
  every boot makes it moot).
- Bench durations for sf1 loads (125-150 s) and the 140-365 s cycle: scaled from wu3 and s4c_noff_1 by frame count.
- That frames which render no level (menu, pause) take < 1.5 ms here (the loading frames do: 0.05-0.10 ms, MEASURED).
- That a slow old PC shows the same classes on top (prereg 0, player framing).
- The class map: CKinematics::CalculateBones is left in other_known (it serves render and game logic); Particle* and
  MT_CALC details are counted as render; CScriptBinder::shedule_Update on worker threads (NPC binders) is counted as
  script although it can include AI work called from Lua.
- That profiler_agg.cpp in wt-render-split today equals the source of Coarse14 (98a444975): not checked (no git).
- All percentages in prereg section 9.

## 4. Fixture folders (real, read in place, never written; the self-test checks that nothing in them changed)
Under /home/caden/Projects/StalkerGameEngine/out/:
- r1a_cor_k0_1, r1a_cor_k0_2, r1a_cor_k0_3, r1a_ros_k0_1, r1a_ros_k0_2, r1a_ros_k0_3 (Coarse14, fixed step, T 20)
- cord1, cord2, cp1 (RefDet12/13, Cordon, with the after-load train, bench verdict CONFOUNDED)
- s2_off_1, s4c_noff_1, s4c_poff_1 and s4c_poff_1_rr (SimTick14/17/16, tick off, real time, 12000 frames)
- s2_c30, s2_c60, s2_c50 (frame limiter on), s4c_n30_1, s4c_n60_1, s4c_p30_1 (tick on)
- hk2, hk3 (no perf_slow file), base1
Source frames and sites for the made fixtures (re-written into the stamp folder with planned slow rows):
r1_cor_k3, r1a_cor_k0_3, r1_cor_k2 (Cordon, no train, max 14.9-22.1 ms), s2_off_1, s4c_noff_1, s4a_noff_1 (Rostok).
2026-11-27 additions: wu3_09_on, wu3_02_off, wu3_01_on (Warm18, cordon1, real time, warm 0), s2_off_2, s4c_noff_1,
s4c_poff_1_rr (shown to the scorer as s4c_poff_1_r1 through symlinks; only a one-line re-tagged xray.log is made),
and a copy of run/appdata/axr_options_play.ltx inside the stamp folder (the real file is only read).
Round 2 (2026-11-27): real Rostok probes in the scorer cases: s2_off_2 (real_trains_and_not_clean), r1a_ros_k0_3
(real_missing_slow_file), hk2 (real_registration_mismatch); cord2 as the real Cordon probe (real_k0_coarse14). The
Rostok probe gate's pass / range / train / coverage / cmdline / zones cases and every stop-record case are MADE
fixtures (Rostok from ROS[0]'s real frame and site files); no real folder holds an sf1 stop.txt.
No real folder can pass the probe gate: every real capture used T = 20 with p99 10-12 ms (coverage_p99), and the
real-time ones have no slow rows at T = 20 (s2_off_1, s4c_noff_1) or start at warm 0 (wu3: seqFrame calls 19,987 of
20,000). So the real "gate" cases show every OTHER check passing on real bytes, and the full pass is a made fixture.
Not exercised by any fixture: the `no_data` class (a frame above p99 but within 2 % above T, without rows).

## 5. What the existing profiler output CANNOT distinguish
1. **The main thread's wait on workers is not zoned.** `secondary_tasks.wait()` (device.cpp:639) has no PROF_EVENT. A
   frame where the main thread sat waiting for the game thread, a frame with unzoned main-thread code, and a frame where
   Wine/the OS pre-empted the main thread all look the same: `main_unzoned_or_wait`. Example from real data:
   r1_cor_k2 frame 1032 is 22 ms with no zone above 1.2 ms.
2. **"other" is every non-main thread summed.** Which worker ran a zone, whether it ran in parallel with the main
   thread, and whether it was on the critical path are unknown. An other-thread class is attributed only because its
   extra time covers half the frame's excess; that is an inference that the main thread waited for it.
3. **Only the 14 largest self times per frame**, across both threads. Many small increases (the "death by a thousand
   cuts" frame) cannot be summed and end as unknown. Sites not in the dump are assumed at their mean.
4. **Self time only, booked when the scope ends.** Work that started in an earlier frame on a worker is charged to the
   frame in which it finished; inclusive time is not in the dump. Lua run inside ALife/scheduled/update or a binder is
   charged to that zone: which script function, and whether a Lua GC step ran inside it, cannot be seen.
5. **GC is elastic.** seqLuaGC repeats while the main thread renders (EngineThreading.cpp:177-190), so a long frame grows
   GC time; a long GC step can also make the main thread wait. Cause and consequence are not separable: gc counts as
   unexplained.
6. **No Present.** -perf_nopresent replaces Present with a wait for the GPU. Present cost, vsync, compositor and DXVK
   frame pacing are not measured; GPU-bound frames show as gpu_wait, a DXVK shader compile on its own thread shows only
   as a wait.
7. **No per-object attribution in the dump.** Coarse14 does print `alife_slow` lines (offline update() >= 1 ms) to
   xray.log with an ENGINE frame number, but the capture's first engine frame is not written anywhere, so they are not
   joined to capture frames. Not used by any rule.
8. **The dump cap** (about 600 slow frames per capture) can end the zone data before the capture ends (handled by the
   window cut, but frames after the cut are lost).
9. **Two saves, standing still, one box**: nothing about movement, combat, level changes, other levels, 4 cores or the
   2012 reference machine.

## 6. Side findings (for the Overseer, not part of the prereg)
- The SimTick builds carry all probes (79,792 main probe calls per frame in s4c_noff_1, as in RefDet14's 79,752;
  Coarse14 1,444), so every S2-series frame time includes the ~0.7 ms probe cost.
- B2b sizing: the "3.5 s train" is wall time of a fixed-step capture. In real-time play (wu3, MEASURED) slow frames
  keep coming for about 20 s after loading ends (RESULT-wu3), and the last frame > 20 ms comes at about 31 s after
  capture frame 0. (The draft's 15 s inference was low.)
