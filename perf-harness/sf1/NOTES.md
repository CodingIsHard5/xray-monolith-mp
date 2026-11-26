# sf1 desk notes (2026-11-26): guesses, unconfirmed flags, fixtures, limits of the profiler output

## 1. Decisions I made that the Overseer should confirm
1. **Exe = Coarse14.exe** (sha256 65edcf401faeb125c68283084f55500c8d55cca58353e682f9ff8f96d884d88c). Basis: the sizing
   study names "Coarse14 with -perf_slow 9" (plan-facts/next-step-sizing-2026-11-26.md:170) and the log calls it "the
   existing profiler build" (engine-perf.txt:1439). Prefix 65edcf401faeb125 registered in PREREG-R1-step0-2026-11-22.md:70;
   full hash computed today from run/bin/Coarse14.exe. It has no fixed-tick code (no "Sim tick" string in the exe), so the
   stock loop holds by construction. Alternative: SimTick17.exe with sim_tick 0 (newest engine, 4a-4c inert when off), but
   it carries all ~80k probes per frame (about 0.7 ms, mostly render), which distorts exactly the render tail we classify.
2. **Real time, not `-perf_fixed_dt`.** Every Coarse14 run on disk used bench.sh, which always adds `-perf_fixed_dt`
   (tools/bench.sh:54; tools/r1_series.sh:37). The brief asks for the stock frame loop in steady play; a fixed 33 ms game
   step per frame runs the game about 6x faster than wall time and changes how often time-scheduled work lands per frame.
   So I use the S2 bench with BENCH_FIXED=0 (tools/s2_bench.sh:10-11), as the last prereg on this harness did. Coarse14
   has NEVER been run this way. If the Overseer prefers comparability with the sizing study's K0 numbers, the change is
   one lever (BENCH_FIXED=1) plus new thresholds and warm-up; my derivations in prereg 3.2-3.3 would not hold.
3. **Per-scene thresholds 6.0 / 9.0 ms** instead of the sizing study's single 9 ms: in Cordon 9 ms would sit at or above
   the real-time p99 (frames between p99 and T get no zones).
4. **Warm-up 11,500 frames** instead of 3,000 (S2-4c): 3,000 real-time frames (about 13-15 s) may not clear the
   after-load train, which by my inference lasts about 15 s of wall time in real-time play (prereg 3.2).
5. **The claim pattern is copied from tools/t3_kw1.sh:55-58 as asked**, including `claim ... 2>&1 | grep -q '^claimed'`.
   quiet-required.sh's own header says "THE EXIT CODE IS THE API. DO NOT PARSE THE OUTPUT." The kw1 pattern parses
   output. I did not change it (brief: copy the pattern); the Overseer may want `claim ... >/dev/null 2>&1` with the exit code.
6. CodeRabbit was not run: it works on git changes and the brief forbids git commands. Instead: a desk mutation pass
   (scorer copies in my scratchpad with one check removed at a time; each was caught by its self-test case).

## 2. Every flag and value, and where it came from
| item | value | taken from (file:line) | confirmed by a previous prereg? |
|---|---|---|---|
| bench | tools/s2_bench.sh 072af8ed... | tools/s4c_run.sh:7 pin, tools/s4c_series.sh:74 | yes (S2-4c part B) |
| `-perf_noinput -perf_nopresent` | | tools/s4c_series.sh:74 default S2_BENCH_FLAGS | yes |
| `-perf_slow` | 6.0 Cordon / 9.0 Rostok | sizing study line 170 suggests 9; values derived here | NO (every run on disk used 20; float parsing: profiler_agg.cpp:80 `%f`) |
| `-perf_capture W,N,tag -perf_quit -nointro -dbg -start server(<save>/single/alife/load) client(localhost)` | | tools/s2_bench.sh:55-56 | yes |
| BENCH_FIXED | 0 | tools/s4c_order.txt:3-12 (fixed column), tools/s2_bench.sh:11 | yes for SimTick builds; NO for Coarse14 |
| BENCH_LOWEND | 0 | tools/s4c_order.txt (lowend column) | yes |
| BENCH_EXCLUSIVE=1, BENCH_NOCLAIM=1 | | tools/s4c_series.sh:74 | yes |
| BENCH_TIMEOUT | 900 | tools/s2_bench.sh:12 default | yes (default) |
| BENCH_DISPLAY | :141 | tools/t3_kw1.sh header + :36-42 (refusal) | not in a bench-harness prereg (they used the default :77); :141 is the box rule |
| warm, frames | 11500, 10000 | derived (prereg 3.2, 3.3) | NO (S2-4c: 3000, 12000) |
| saves | cordon1, bench1 | tools/r1_order.txt (Coarse14 lines), PREREG-R1-step0 | yes; "bench1 = Rostok bar": INFORMATION.md:123/137 say Rostok, standing still; "bar" is from the brief, not seen in a file |
| save hashes | full sha256 in sf1_run.sh | computed today; cordon1.scop prefix 6dea752bc652a45c matches out/cordon1_before_pfC.sha and tools/t3_kw1.sh pin | bench1 hashes: no earlier pin found |
| user.ltx | r__framelimit 0, no sim_tick, read-only check | run/appdata/user.ltx:553 today | the S2 series SET these; I only check |
| settle, re-run once, slice 3 h, stop rules | | tools/s4c_series.sh:25-29, 61, 68, 83 | yes (copied) |
| Caden's launcher lock wait | | tools/s4c_series.sh:58 | yes |

## 3. Where I am guessing
- That the A-Life pass is scheduled in GAME time (inferred from the 14-frame period at 33 ms = 462 ms matching
  alife.ltx 170-470 ms); hence the 22.7 s train bound in real time and "about 15 s of train in real-time play".
- The warm-up frames before a fixed-step capture took <= 7 ms each (only the first MEASURED frames are known: 6.2-6.6 ms).
- Coarse14's real-time Rostok p99 (10.6-11.5 ms): SimTick real-time p99 minus the 0.7 ms probe cost, assuming the
  probe cost shifts the tail like the median.
- Cordon real-time p99 7-10 ms and 3-8 % of frames above 6 ms: no real-time Cordon capture exists.
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
Self-test folder: sf1/selftest-runs/20261126-151722-463192 (49/49 PASS).
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
- If the A-Life pass is game-time scheduled (section 3), B2b's "3.5 s train" is wall time of a fixed-step capture; in
  real-time play the same train should last about 15 s (about 32 passes, 462 ms apart). Worth stating in the B2b sizing.
