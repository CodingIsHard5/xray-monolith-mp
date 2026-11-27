# wu2 desk notes (2026-11-26): facts of the wu1 prime load, decisions, limits

wu2 is a NEW registration (the Overseer's option B). Same exe (run/bin/Warm18.exe), same flags (`-perf_capture
0,20000,<tag>`, `-perf_slow 20`, ...), same order (1 prime + 16 ON + 16 OFF, ABBA), same H1-H3, NV rules, exit codes
and probe-before-any-scored-load. What changed: the scorer finds the end of loading INSIDE the capture and starts
every window there. Source read: wt-simtick, branch perf/alife-warmup, d77d70b5c (the commit Warm18.exe is built
from; the binary itself was not disassembled). Data read in place: out/wu1_prime (nothing rerun, nothing copied
whole). No engine run, no box claim.

## 1. Limits and doubts first
1. **One real load.** Every fact below about where things fall in the capture comes from ONE OFF load (wu1_prime).
   No ON load with the new layout exists. The held-frame path (ON, `precache held K`) is checked on made data only.
2. **The log cannot pin line 8492 to one frame.** In log order the "Handled Necessary Textures Destruction" line sits
   between the clock line of device frame 40 (capture 22) and that of frame 102 (capture 84): 62 frames. What pins it to
   one frame is code plus the engine's own counters (2.3), not the log.
3. **CLOCK_TOL_MS = 15 ms is derived from this same load** (max residual 4.27 ms over 23 pairs). One load is thin
   evidence for the tail of that residual. A scored load with a residual over 15 ms stops the series (VOID clock_fit),
   it does not give a wrong verdict.
4. **A one-frame move of a single clock line in quiet frames is NOT caught** (residual about one frame, 4-6 ms, under
   the 15 ms tolerance; self-test real_prime_play_line_moved: 4.96 ms, probe ok). It does not move the window (E comes
   from the counters) and moves the load time by at most that line's share. A countdown line moved one frame forward
   IS caught (its countdown says E+1). A renumbering of EVERY device frame by the same amount is invisible and
   harmless (real_prime_f_plus_1_all: D=19, same E). A uniform shift of every time_global against the mcm_log
   "Time continual is:" stamps is, since review pass 1, only REPORTED (clock_cross note; real_prime_t_plus_1000 now
   passes with the note): see 7.3.
5. **"Precache frame" count = 58, not 60.** PreCache(60) is set in net_start_client6 (Level_network_start_client.cpp:273);
   the counters say 58 FrameMove frames had the countdown > 0. The 2 missing are most likely the End() of LoadDraw in
   the 2 loading frames after it (x_ray.cpp:1590-1601: LoadDraw calls Device.End() while g_appLoaded is false); I did
   not trace g_appLoaded through those two frames. The scorer does not use 60 or 58: E = loading frames + 'Precache
   frame' calls, cross-checked by the countdown lines.
6. The intro-frame marker (CGamePersistent OnFrame slow row at E-2) is REPORTED only, not required: it is a 52 ms lua GC
   after intro_start in this load; whether it is always over 20 ms is not known.
7. The prime load's numbers (hitch 339.8 ms at 6.44 s, n20 = 20, load 19.88 s) are now seen before registration. No
   registered bound was changed because of them except CLOCK_TOL_MS (limit 3).

## 2. Facts (MEASURED = out/wu1_prime files, CODE = wt-simtick src file:line)
### 2.1 Which device frame is capture index 0
- CODE: perf_autocapture() runs right after PROF_FRAME at the top of on_idle (device.cpp:675, 682). Its start condition
  is `g_pGameLevel && g_bLoaded && !dwPrecacheFrame && !Paused()` (device.cpp:422); g_bLoaded is set in every FrameMove
  (device.cpp:1125) so it is already TRUE from the main menu; dwPrecacheFrame is 0 until net_start_client6 sets it.
  So the capture starts in the first on_idle after the level object exists. xrprof::start (device.cpp:434) sets the
  tick base (profiler_agg.cpp:100); the next PROF_FRAME stores the first frame (profiler_agg.cpp:187). Capture index 0
  is therefore the REST of the on_idle that started the capture.
- MEASURED: the started line is xray.log line 3719, 3 lines after "* [x-ray]: Prefetched Data" (3716,
  IGame_Persistent.cpp:262). Sites file: every one of the 12 loading events has its calls INSIDE the capture
  (net_start1 1 call, ...). Slow file: capture frame 1 (5761.1 ms) holds CALifeSimulator::CALifeSimulator (2790.9 ms
  self) and "Load Alife Simulator" (2103.2 ms): that is net_start2, the 2nd loading event. So capture 0 = the on_idle
  that ran net_start1 (1.5257 ms; net_start1 incl 0.979 ms).
- Device frame numbers: the fit of 2.4 gives dwFrame = capture index + 18 for every FrameMove frame (capture 13 =
  dwFrame 31). Loading frames do not run FrameMove; their LoadDraw may bump dwFrame (x_ray.cpp:1591), which the scorer
  does not rely on.

### 2.2 Loading, precache and play frames in the capture
| capture | what | how known |
|---|---|---|
| 0-12 (13 frames, 6239.8 ms) | loading frames: one loading event each, return before FrameMove: no seqFrame, no time_global update | CODE device.cpp:695-704; the event order Level_start.cpp:95-100 (net_start1-6), 221-235 (net_start4 pushes net_start_client1-6 in front); MEASURED sites: 'Loading...' 13 = 'LoadDraw' 13 = 11 single events x 1 + net_start_client6 x 2; frame 1 = net_start2 (CALifeSimulator), frame 12 = net_start6 (slow rows name net_start6 and 'Loading...', 213.0 ms) |
| 13-70 (58 frames, 4700.8 ms) | precache frames: FrameMove runs, countdown 58 -> 1 at FrameMove time; the loading screen is still up | MEASURED sites 'Precache frame' (device.cpp:773) 58 calls; countdown lines F31 P58, F33 P56, F34 P54, F40 P49 put P = 0 at dwFrame 89 = capture 71; frames 13, 14, 17 are 1234.4, 2494.7, 488.9 ms (cl_Process_Spawn, CScriptBinder::shedule_Update) |
| 69 | intro_start (log 8489) acts here: first frame with countdown <= 2 (GamePersistent.cpp:564) | MEASURED slow row 'CGamePersistent OnFrame' 52.1 ms self in frame 69 (58.8 ms), right after "luagc_cleanup collectgarbage" (log 8490-8491); 69 + 18 = dwFrame 87, countdown 2 there |
| 70 | its End() counts 1 -> 0 and prints log line 8492 "Handled Necessary Textures Destruction" | CODE device.cpp:166-188 |
| 71-19999 (19929 frames, 91112.5 ms) | play | MEASURED sites 'Start xrSASH Benchmark' (device.cpp:709, countdown 0 before FrameMove) 19929 = 20000 - 13 - 58 |

### 2.3 Where line 8492 falls, and how tightly
- CODE: it is printed inside End() of the frame whose End() takes the countdown from 1 to 0 (device.cpp:166-188), i.e.
  the LAST frame with 'Precache frame' (the countdown is > 0 at device.cpp:773 and End() comes after it in the same
  on_idle, device.cpp:898-910). So its capture frame = 13 + 58 - 1 = **70**, from the engine's own counters.
- Countdown lines (clock lines with precache > 0): capture + precache = 71, 71, 70, 71 -> Handled at 70 (the 70 is
  frame 34, an A-Life-thread line that read the countdown after that frame's End(): 1 frame, allowed).
- Log order only: after the clock line of capture 22 (dwFrame 40, line 8488), before that of capture 84 (dwFrame 102,
  line 8494). **The log alone pins it to 62 frames, not one.**
- The intro marker (capture 69) gives 70 independently of the counters.
- Proposed "end of loading" (registered in wu2): **E = the first play frame = loading frames + precache frames = 71**,
  i.e. the frame after the End() that printed line 8492. Distance from line 8492: it is printed at the END of capture
  frame 70 (5.1 ms long), frame 71 starts right after: 0 frames / about 0-5 ms. E is 10940.6 ms after capture start.

### 2.4 The 13 frames without seqFrame
'Process seqFrame' 19987 calls for 20000 frames. The code path: on_idle with a pending loading event runs ONE event
(PROF_EVENT "Loading..."), then pApp->LoadDraw() (PROF_EVENT "LoadDraw") and returns (device.cpp:695-704) before
FrameMove (device.cpp:744) where seqFrame is processed (device.cpp:1121). 'Loading...' = 'LoadDraw' = 13 calls exactly.
13 = net_start1, 2, 3, 4, net_start_client1, 2, 3, 4, 5, 6 (twice: it returns false while synchronize_map_data() is not
done, Level_network_start_client.cpp:244-245), net_start5, net_start6. Proven frame by frame by the order: frame 1 is
the 2nd event (CALifeSimulator), frame 12 the 13th (net_start6 in its slow rows); events run one per on_idle while the
queue is not empty, and nothing else pushes loading events (only Level_start.cpp pushes; Level_network_map_sync.cpp
only erases). So frames 0-12 are the loading frames.

### 2.5 Load time now
- Engine clock (time_global, ms since device init) at E: **19876 ms = 19.88 s** (from the play line dwFrame 102,
  T 19948, minus the 71.6 ms of frames 71-83; from the last countdown line, dwFrame 40 T 19486 plus 390.4 ms: 19876 ms;
  the two agree to 0.02 ms).
- The old 5.4 method (F0 = F + P, then subtract from the first play line) is replaced: the capture now gives the frames
  between any clock line and E directly, and E comes from the counters. The capture also gives the in-capture loading
  duration directly: 10.94 s from capture start (net_start1) to E; it misses device init -> level creation (about 8.9 s
  of engine clock before the capture: 15175 - 6239.8 at capture 13). Review pass 2: H3 is now on the in-capture
  duration; the engine-clock load time is reported beside it (section 8).
- MEASURED wu1 probe "13708 ms not between 19486 and 19948": wu1's rule took the frames after F0 from capture index 0,
  which are loading frames; same cause as the stop.

### 2.6 Does capturing the loading change what the lever or the scorer's other rules see?
- -perf_slow cap (profiler_agg.cpp:43, 190): rows are kept while rows + 14 <= 8400, counted from capture START. The
  loading/precache frames over 20 ms now take rows: MEASURED 10 such frames (1, 10, 12, 13, 14, 15, 16, 17, 31, 69),
  140 rows; whole capture 31 slow frames, 434 rows (19x under 8386). wu1 5.3's "the cap cannot be reached before 12 s
  after loading" is wrong for wu2 (the 12 s now start at capture start, 10.9 s before E). New bound by counting: the
  cap needs 600 slow frames, i.e. at least 600 captured frames: capture index >= 599. The save frame is capture 155
  (MEASURED): it can never lose its rows while it is under index 599 (2.80 s after E in this load). VOID
  slow_cap stays.
- frames_slow: every slow row's frame_ms equals the frames file's ms of that frame (MEASURED 434 of 434, to 4 decimals:
  same ticks, same scale at dump, profiler_agg.cpp:153, 166, 187, 210). New check, catches a frames file shifted against
  the profiler's own rows.
- The lever: its full pass and any held frames happen in capture frames 13..E-1 (the precache block), outside the
  window by construction. Held frames add to 'Precache frame' (they keep the countdown at 3 > 0): E = L + 58 + K.
- Capture coverage: play frames are 20000 - E, not 20000. MEASURED 91.1 s of play (mean 4.57 ms, min 3.76 ms).
- The save: capture 155 (79.8 ms), 84 frames after E (0.54 s).
- wu1's OFF grouping for this load: OFF-IN-PLAY (no wide pass; widened at dwFrame 31 precache 58, narrowed at
  dwFrame 33 precache 56, inside loading). Window (30 s from E, save excluded): worst 339.8 ms at 6.44 s, n20 20,
  n33 6, span 20.38 s; 15 s: n20 7.

## 3. Decisions I made that the brief did not specify (to confirm)
1. **E comes from the engine's counters (sites file: 'Loading...' + 'Precache frame'), and the clock lines must agree
   with it** (every countdown line's capture + precache in {E-1, E}). The brief said "from the frames file plus the
   clock lines". I found the counters give E exactly; the clock lines + frames file are the independent cross-check
   (and give the load time). If they disagree: VOID loading_end (stop).
2. **The offset D (dwFrame = capture + D) is FITTED from the times alone** over every D that puts all clock lines in
   FrameMove frames of the capture, and must be unique: best max residual <= 15 ms, second best > 15 ms (else VOID
   clock_fit "ambiguous"). On wu1_prime: D=18 at 4.27 ms, next D=17 at 1213.2 ms.
3. **All new checks are stop-VOIDs** (like wu1's capture_lines / stock_loop / framecount): loading_frames, clock_fit,
   loading_end, clock_order, frames_slow. In the probe they make PROBE FAIL by name. No clock line at all stays the
   wu1 FAIL no_measure_lines. Never a fallback.
4. ~~H3 unchanged: load time = engine clock at E~~ (superseded by review pass 2: H3 on loading_in_capture_s, section 8).
5. **NO VERDICT `loadtime` withdrawn**: under wu2 every load that is not VOID has a load time (E and the clock fit are
   stop-VOID conditions), so the rule could never fire; a valid load without one would be FAIL scorer_error. (The
   rule's hook and constants LOADTIME_MIN_* are gone.)
6. stock_loop now: seqFrame main calls = N - L exactly, L = 'Loading...' calls, with L = LoadDraw = 11 + net_start_client6
   calls and every other event exactly once (loading_frames). Derived, not loosened.
7. capture_lines now: started < Handled < finished (log order). wu1's "Handled < started" is the wrong rule.
8. clock_order: (a) no clock line of capture > E-1 logged before line HANDLED, none of capture < E-2 logged after it
   (E-2: the A-Life thread may print a frame's line after that frame's End(); one more frame allowed for its read race);
   (b) since review pass 1: each clock kind ALONE never goes back by more than CLOCK_BACK_MS = 1 ms in log order (stop);
   the cross-kind comparison is the reported-only clock_cross (7.3).
9. Self-test disk: pool files are stored once per content and hard-linked (identical frames files across made loads);
   the self-test folder is 32 MB. One earlier iteration folder of mine (50 MB, failed run) was removed by its literal
   path; the passing reference run is kept.
10. In made fixtures the lever `after_intro` alife_pass line now reads its own frame's countdown (2) instead of 33
    (an inconsistency of the wu1 fixture that the new countdown check flags).

## 4. What I could not verify
- That Warm18.exe prints exactly these lines (source read, not the binary) - but wu1_prime is real Warm18 output.
- That net_start6 / CALifeSimulator are > 20 ms in every load (CALifeSimulator: 2790 ms self, very likely; it is the
  only marker the scorer REQUIRES; net_start6 is not required).
- That PreCache is not called again during play (game_sv_single.cpp:366 also calls PreCache(60); a second countdown in
  play would make E_counters disagree with the countdown lines: VOID loading_end, not a silent error).
- The held-frame layout on a real ON load (none exists).
- That the 15 ms tolerance holds for loads whose precache frames are slower (the residual is the pre-FrameMove part of
  the two frames, device.cpp:675-744).

## 5. Fixture list
- Real, in place: out/wu1_prime (probe passes; 12 mutants as one changed file + symlinks), parser on out/cp1, cp2, cp3,
  cp4, uc1, lc1-3, cpl1, wu1_prime; folders cp1, cp2, uc1, r1a_cor_1 as loads; cp1, cp2, cp4, uc1 spliced.
- Self-test: `cd wt-render-split/perf-harness/wu2 && nice -n 19 bash wu2_selftest.sh`
  -> result line in section 6. wu2 mutants must be caught by EVERY listed target (wu1 stopped at the first case that
  differed).

## 6. Self-test reference run and hashes
- See 7.6 (review pass 1). The pre-fix reference run 20261126-184239-1446505 (218/218) is superseded; kept (not mine).

## 7. Review pass 1 (CodeRabbit 2026-11-26, triage table in coderabbit-triage-2026-11-26.md), facts and decisions
Source: wt-simtick perf/alife-warmup d77d70b5c (working tree clean). Data: out/wu1_prime in place. Desk only.
### 7.1 Held frames extend the precache block (finding 5) - CODE
- End(): `if (!(dwPrecacheFrame == 3 && alife_warmup_hold_frame())) dwPrecacheFrame--;` (device.cpp:165-169);
  alife_warmup_hold_frame() returns true (no count-down) while g_alife_warmup_hold == 1 and fewer than 300 frames are
  held, counting s_alife_warmup_held (device.cpp:131-139); the release frame prints "precache held K frames" and
  counts 3 -> 2 (device.cpp:147-152); a new PreCache() zeroes the count (device.cpp:223).
- So the countdown is still 3 at the next FrameMove: 'Precache frame' (device.cpp:771-773, `if (dwPrecacheFrame)`) is
  called in every held frame, dwFrame++ runs in each (device.cpp:1048), and 'Start xrSASH Benchmark' (device.cpp:707-710,
  countdown 0) does not. Precache block = 58 + K frames, **E = L + 58 + K** (71 + 12 = 83 in the fixture), as the
  scorer already computed (E = L + 'Precache frame' calls). Only the fixture was wrong (it fitted the hold inside 58).
- Fixture now: capture 13..67 countdown 58..4, 68 = 3, 69..68+K = 3 (held), 69+K = 2, 70+K = 1, E = 71+K;
  'Precache frame' = 58 + K, 'Start xrSASH Benchmark' = N - 71 - K. Self-test l_held_ok (E=83, 70 precache frames,
  load time at capture 83 from device frame 106), l_held0_ok (same path, K = 0: E=71), check_held / check_held0.
### 7.2 Load time: an estimate, with a code bound (finding 2)
- time_global of a clock line in capture frame c is read at device.cpp:1112, o_c after that frame's PROF_FRAME
  (device.cpp:675), FrameMove being called at device.cpp:744 in the same on_idle; the frames file measures PROF_FRAME to
  PROF_FRAME in TSC ticks scaled to QPC time over the whole capture (profiler_agg.cpp:119-121), TimerGlobal is QPC
  (FTimer.h). So 0 <= o_c <= ms[c]; estimate - true value at E = o_c - o_E in [-ms[E], +ms[c]], + 1 ms of integer
  time_global, if no timer pause lies between c and E. There is no tighter engine bound on o_E: between PROF_FRAME and
  the read run perf_autocapture, the seqParallelBeforRender callbacks (any number, device.cpp:719-735), and FrameMove's
  head; nothing logs when. The clock fit says nothing about o_E (CodeRabbit's point, correct); it places the lines and
  says the offsets at the clock-line frames agree within the residual.
- MEASURED-PRIME: bound -5.9 / +5.5 ms (frame E 4.9 ms, frame 84 4.5 ms). The scorer prints the bound per load.
### 7.3 clock_order (finding 8) - CODE; the CodeRabbit question refuted, the cross-clock rule made report-only
- Where each value is sampled: dwTimeContinual = TimerMM - app_inactive_time (device.cpp:1051) and dwTimeGlobal =
  TimerGlobal (device.cpp:1112), both only in FrameMove. alife_pass: copies Device.dwFrame / dwTimeGlobal /
  dwPrecacheFrame at the START of CALifeUpdateManager::update and Msg()s them AFTER the pass, wide or full
  (alife_update_manager.cpp:173-193). alife_opu: live read at the Msg (alife_simulator_script.cpp:506-509). The stamp:
  mcm_log.script:203-205 printf -> _g.script:765 time_continual() -> device():time_continual() -> Device.dwTimeContinual
  (script_render_device_script.cpp:45-48): a live read of the frame's sample.
- When each line is logged: the A-Life update runs from seqParallel inside the GameThread task (EngineThreading.cpp:
  161-166; pushed by shedule_Update, alife_update_manager.cpp:205-213), started at device.cpp:865 and joined at
  device.cpp:918, before the on_idle ends; the next FrameMove (the only writer) is in a later on_idle. So a pass line,
  however long its pass (612 ms full pass, a wide stock pass, any held frame), is logged before any value of a later
  frame exists. Same for every other print. Hence each kind is non-decreasing in log order (TimerGlobal frozen, never
  back, while paused; dwTimeContinual drops only at an app re-activation, device.cpp:1248). The per-kind rule cannot
  stop valid data; kept as a stop.
- Across kinds the same-frame gap is TimerMM - TimerGlobal: started together (Device_Initialize.cpp:40-41), one QPC
  clock, so < 1 ms, UNLESS TimerGlobal was paused (Device::Pause -> g_pauseMngr, device.cpp:1153-1155; callers include
  UIGameTutorial.cpp:192-194 for UI sequences with a pause flag and GamePersistent.cpp:959 on focus loss), the app was
  deactivated (device.cpp:1248, 1266), or time_factor changed (TimerGlobal only, device.h:363-367;
  console_commands.cpp:2049, 2160, which GAMMA scripts can run). The "game_loaded" sequence (intro_start,
  GamePersistent.cpp:563-575) is a CUISequencer; whether its xml sets the pause flag I could not read (not unpacked).
  So the source does not settle that a normal load has a 0 ms gap: the cross rule is now REPORTED only (clock_cross).
  MEASURED-PRIME: gap 0 in 3 same-frame pairs (7045/7046 frame 31, 8130/8132 frame 33, 8527/8553 frame 1413).
  Protection lost: a uniform shift of every time_global against the stamps is no longer a stop (it moves that load's
  load time, H3); it is printed per load.
### 7.4 Driver (findings 3, 9)
- Finding 3: order_copy (copy into the new state folder, chmod a-w, pin the COPY with the same ORDER_SHA, order_ok on
  the copy); ORDER is then the copy for pending and check_run. Tested offline: the function block between the
  BEGIN/END markers of wu2_run.sh is extracted and run on a copy of wu2_order.txt in the self-test folder (copy ok,
  hash, read-only, dispatch unchanged after the original is edited, original changed before the copy -> refused by
  name, PIN_ME refused, existing copy refused, wrong shape refused); 3 mutants caught; a static check of the body.
- Finding 9: slice_ok $SL_END $((BENCH_TO + END_RESERVE)) right after the claim renewal, before the bench (900 + 120 s).
- `bash -n wu2_run.sh` ok; the driver itself never run.
### 7.5 Decisions the brief did not specify (to confirm)
1. Stopped series name: FAIL `stopped_series` (prereg 3.3 says a stopped series is FAIL; the code could PASS: code
   fixed). The prime's VERDICT counts toward the 3, as in the driver (its loop counts the prime); a missing or
   unreadable prime conditions.txt counts as not clean (the driver's empty VERDICT does). The made series cases now
   use a real clean prime fixture (p_prime_ok) instead of a missing wu2_prime folder; nv_few_on uses 9 window_short
   loads instead of 9 CONFOUNDED (9 would now be a stopped series).
2. clock_order split: per-kind stop (CLOCK_BACK_MS = 1 named), cross-kind report-only `clock_cross`.
3. END_RESERVE = 120 s (GUESS except the 60 s notify timeout); usable slice 165 -> 163 min in 3.3 / 6.1.
4. The load-time bound is printed in every report's "load time:" line and the probe/check info line.
5. measure_flags: both flags are part of arm_mismatch (FAIL), as the brief's name says.
6. The find-error test uses a missing -newer file (works as root) and an unreadable subfolder (skipped as root).
### 7.6 Self-test run and hashes (review pass 1)
- Run 2026-11-26 19:23:39-19:24:35 (`date`), folder selftest-runs/20261126-192339-3368140 (36 MB):
  `WU2 SELFTEST PASS 251/251 (28 real cases, 140 made cases, 55 hook mutants caught, 18 constant mutants caught,
  1 hook-coverage check, 1 MAX_CONFOUNDED agreement check, 2 find-error checks, 5 driver checks, 1 untouched check)`.
  My earlier passing run of this round (20261126-191952-3185680, before a citation fix) removed by its literal path.
- sha256 at that run: wu2_score.py d86c928fee7678997634a8681bf95c42785bf0dd2344c54bab6468e70ff84143;
  wu2_selftest.sh cbb6a4601f311943f4a20716eee69c0851567b5ff01f35547026bf9af82f370d;
  wu2_run.sh cecfa44d200489207015338d9bbf5c853a8cb2f73356cb60ca6ee73eb0dc77f7 (bash -n ok, never run);
  wu2_order.txt 21e80fc1f29524c29f476c2af0545bd5fb72092ee0141664efb66015cd6f08ae (unchanged).
- Still not pinned (PIN_ME_* in wu2_run.sh: ORDER_SHA, score, selftest; prereg 13).

## 8. Review pass 2 (CodeRabbit 2026-11-26, plan-facts/cr_wu2_kj3_pass2_2026-11-26.txt; triage section "pass 2")
Desk only. Source: wt-simtick (perf/alife-warmup d77d70b5c tree as checked out). Data read in place: out/wu1_prime and
the earlier cordon1 logs named below. Each finding was checked against the code first.
### 8.1 Prime conditions.txt that exists but cannot be read (finding wu2_score.py:865) - CONFIRMED, FIXED
- read_verdict -> lines_of opens the file; a mode-000 file raises PermissionError (an OSError, not Bad), which the prime
  loop did not catch: the scorer exited with a traceback before writing the report. Now `except OSError` (hook
  prime_oserror) gives `unreadable (<type>: <text>)`, counted as not clean like a missing one. Scored loads were already
  safe (score_safe: scorer_error FAIL).
- Tests: s_prime_unreadable_ok (prime mode 000, nothing else non-clean: PASS, report written), s_prime_unreadable (with 2
  CONFOUNDED loads: FAIL stopped_series naming p_prime_unread). The fixture file is its own file (unlinked from the pool
  blob, created with "xb", chmod 0), never a shared hard link. Both skipped as root (root reads mode 000), as the driver
  tests. Mutant prime_oserror (re-raise) caught by both: exit 1 with no report instead of the result.
### 8.2 Does v_g_back really hold the swap? (finding wu2_selftest.sh:206) - REFUTED by the bytes
- `log_events` sorts the play lines by k BEFORE the swap (`play.sort(...)`, then `play[0], play[1] = play[1], play[0]`),
  and `make` writes them in list order (`head + cs + ... + made + play + ...`): nothing sorts after the swap.
- The generated file (selftest-runs/20261126-194722-289594/pool/v_g_back/xray.log), lines 68 and 69, right after the
  loading-end line 67:
  `* alife_pass frame 177 time_global 33453 precache 0 n 20 of 2400 limit 20`
  `* alife_pass frame 89 time_global 33000 precache 0 n 20 of 2400 limit 20`
  The scorer's VOID names exactly that pair ("log line 69 time_global 33000 after log line 68 time_global 33453").
- Added: a counted self-test check on the fixture bytes themselves (before the cases run): the first two play
  alife_pass lines after the loading-end line must have frame and time_global both decreasing; prints both lines.
### 8.3 H3 and a timer paused during loading (finding wu2_score.py:499) - CONFIRMED as a gap; H3 moved (the Overseer's decision)
- The concern holds: time_global is TimerGlobal, a CTimer_paused (device.h:124) registered with g_pauseMngr
  (FTimer.h:194); Device::Pause -> g_pauseMngr().Pause (device.cpp:1155) -> each timer's Pause adds the paused span to
  qwPauseAccum (FTimer.cpp:37-45; FTimer.h:173-187), elapsed = QPC - start - accum (FTimer.h:46); the freeze absorb
  also adds to it (device.cpp:1105, FTimer.h:41; default off). The loading frames run no FrameMove (device.cpp:695-704),
  so no clock line exists in capture 0..12: a pause there shifts every later time_global by the same amount, which the
  clock fit (differences only) cannot see.
- The frames file is not affected (checked BEFORE building, as ordered): the only PROF_FRAME is device.cpp:675 (top of
  on_idle, after the b_is_Ready return at 669-673); PROF_FRAME = ::xrprof::frame() (profiler.h:123); frame() pushes
  `__rdtsc() - g_tsc_last` (profiler_agg.cpp:185-187) and sets g_tsc_last (217); start sets the QPC and TSC bases
  (99-100); dump() converts with ms_per_tick = QPC ms of the whole capture / TSC ticks (116-121) and writes ms = ticks x
  ms_per_tick (153). No CTimer, no pause manager, no time factor on that path.
- H3 now: median ON - median OFF-IN-PLAY of loading_in_capture_s = sum ms[0..E-1] <= 2.0 s. The engine clock at E stays
  in every LOAD line, the probe/check info line and the stats (engine_clock_load_s). Report block "LOAD TIME" with the
  player line "loading takes X s longer / shorter with the lever", the engine-clock delta, the difference of the two
  deltas, its bound and the named note loadtime_measures_differ (prereg 5.4, 6, 6.1).
- **The note's bound (derivation in prereg 5.4):** per load engine - in capture = G + e, e in [-(ms[E]+1), +(ms[c]+1)];
  with G equal in every load, medians are monotone, so (de - dc) in [-(max ON lo + max OFF up), +(max ON up + max OFF
  lo)]: about +-11.4 ms on wu1_prime's frame sizes, +-12 ms on made loads. **UNDER 2x, far below 1x:** G (engine time
  at the capture start) varies per load. Proxy, MEASURED: the mcm_log stamp printed during the loading events (xray.log
  line 5212-5217; dwTimeContinual is written only in FrameMove, device.cpp:1051, so it is the last FrameMove before the
  loading events): cord1-3 7567/7604/7604, cp1-4 7934/7695/7650/7504, cpl1 8360, uc1 8019, lc1-3 8007/8136/8047,
  wu1_prime 7749 ms: range 856 ms over 13 cordon1 loads. In wu1_prime G = 19876 - 10940.6 = 8935 ms, 1186 ms more than its
  stamp (time between that FrameMove and the capture start, not seen). So the note will very likely fire on a valid
  series (the made "good" series prints it: +0.736 s, from the fixtures' t_end spread). It is a pointer, decides nothing.
- 2.0 s re-derived (prereg 6): the moved pass (0.5-0.75 s) and any held frames lie inside capture 13..E-1, so the whole
  price is in the in-capture time; margin 2.5x on wu1 P9's -0.3..+0.8 s (GUESS, carried). The old 6.1 row (load-time
  estimate >= 300x against 2.0 s) is withdrawn; wu1's carried "H3 price 2.5x" row replaced by a re-derived one.
- Tests: h_price_capture (9 ON loads with the net_start2 loading frame +3000 ms, engine clock at E as the good ON loads:
  H3 +3.00 s FAIL, "loading takes 3.00 s longer", SAME DIFFERENT, note printed), h_price_engine_only (wu1's PRICE loads:
  engine clock +3.70 s, in capture +0.00 s: H3 ok, note printed, SAME), loadtime_agree (16 ON loads at the OFF-IN-PLAY
  median engine clock 33010 ms: delta difference +0.000 s, "agree"); probe/check info line patterns (real_prime_probe:
  loading=10.94 s in capture, engine=19.88 s). Mutants: h3_capture (H3 and SAME on the engine clock) caught by
  h_price_capture and h_price_engine_only; lt_differ (note never printed) caught by both; h3 now targets h_price_capture.
### 8.4 Decisions the brief did not specify
1. SAME AS GOOD ORDER's load term also moved to the in-capture time (same reason; reported only).
2. The note line says "agree" inside the bound; outside it a separate "NOTE loadtime_measures_differ" line.
3. Stats key renamed load_s -> engine_clock_load_s; loading_in_capture_s kept as H3's key.
4. No constant for the note's bound (it is computed per series from the frame sizes).
### 8.5 Self-test run and hashes (review pass 2)
- Run 2026-11-26 19:47:22-19:48:20 (`date`), folder selftest-runs/20261126-194722-289594 (40 MB):
  `WU2 SELFTEST PASS 259/259 (28 real cases, 144 made cases, 58 hook mutants caught, 18 constant mutants caught,
  1 hook-coverage check, 1 MAX_CONFOUNDED agreement check, 1 fixture-bytes check, 2 find-error checks, 5 driver checks,
  1 untouched check)`. My two earlier runs of this round (19:41:53, 4 pattern fails; 19:43:18, before the summary-line
  label) were removed by their literal paths.
- sha256: wu2_score.py b5d14b1256f5aab5aa139db59771d1e97a9c23aa90f31400ec309d82bd985c41;
  wu2_selftest.sh fda4065ce184598e12e570a0f2f1459f8bbf25c9e2c1ac1cab5901b54b2e0ddb;
  wu2_run.sh cecfa44d200489207015338d9bbf5c853a8cb2f73356cb60ca6ee73eb0dc77f7 (unchanged);
  wu2_order.txt 21e80fc1f29524c29f476c2af0545bd5fb72092ee0141664efb66015cd6f08ae (unchanged). Still PIN_ME.
