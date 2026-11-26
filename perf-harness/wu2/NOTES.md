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
   harmless (real_prime_f_plus_1_all: D=19, same E). A uniform shift of every time_global is caught only because
   the mcm_log "Time continual is:" stamps exist (real_prime_t_plus_1000); a build without that script would not catch it.
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
  of engine clock before the capture: 15175 - 6239.8 at capture 13). H3 stays on the engine-clock load time (as wu1);
  the in-capture duration is reported (decision 3.4).
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
4. **H3 unchanged**: load time = engine clock at E. Reported beside it: loading_in_capture_s.
5. **NO VERDICT `loadtime` withdrawn**: under wu2 every load that is not VOID has a load time (E and the clock fit are
   stop-VOID conditions), so the rule could never fire; a valid load without one would be FAIL scorer_error. (The
   rule's hook and constants LOADTIME_MIN_* are gone.)
6. stock_loop now: seqFrame main calls = N - L exactly, L = 'Loading...' calls, with L = LoadDraw = 11 + net_start_client6
   calls and every other event exactly once (loading_frames). Derived, not loosened.
7. capture_lines now: started < Handled < finished (log order). wu1's "Handled < started" is the wrong rule.
8. clock_order: (a) no clock line of capture > E-1 logged before line HANDLED, none of capture < E-2 logged after it
   (E-2: the A-Life thread may print a frame's line after that frame's End(); one more frame allowed for its read race);
   (b) time_global and "Time continual is:" values never go back in log order by more than 1 ms (dwTimeContinual is
   read before dwTimeGlobal in FrameMove from a timer started after it, device.cpp:1051, Device_Initialize.cpp:40-41).
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
  -> `WU2 SELFTEST PASS 218/218 (26 real cases, 124 made cases, 51 hook mutants caught, 14 constant mutants caught,
  1 hook-coverage check, 1 find-error check, 1 untouched check)`; folder: see section 6. wu2 mutants must be caught by
  EVERY listed target (wu1 stopped at the first case that differed).

## 6. Self-test reference run and hashes
- Run 2026-11-26 18:42:39-18:43:26 (`date`), folder selftest-runs/20261126-184239-1446505 (32 MB), result line as in 5.
- sha256 at that run: wu2_score.py 5a6203cbe70bab82632d05af755ad32459e7449f515a98008adfaaaf3f5f4c6f; wu2_selftest.sh 5587851ae4a62c281c0b761ca8b24944e36cb7c3016c5c9eee616290154cca71; wu2_order.txt 21e80fc1f29524c29f476c2af0545bd5fb72092ee0141664efb66015cd6f08ae; wu2_run.sh 0428792391367c877828679263949467e3047d636b5a82a60392421c16f8b99f (bash -n ok, never run).
- Not pinned anywhere yet (PIN_ME in wu2_run.sh and prereg 13).
