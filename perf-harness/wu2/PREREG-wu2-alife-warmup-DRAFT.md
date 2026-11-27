# PREREG wu2 (DRAFT): does `-alife_warmup` remove the slow frames after a level load? (2026-11-26; not registered; nothing run)

## What changed from wu1 and why (read first)
**wu1 stopped at its probe because my desk read in wu1's section 5.1 was wrong.** wu1 5.1 said: with
`-perf_capture 0,N` the capture starts on the first frame after the loading screen, because the start condition is
`g_pGameLevel && g_bLoaded && !dwPrecacheFrame && !Paused()`. That is false. The condition is already true right after
"* [x-ray]: Prefetched Data", before the level is loaded: the level object exists, g_bLoaded has been TRUE since the
main menu (set in every FrameMove, device.cpp:1125) and the precache countdown is not set yet (it is set later, in
net_start_client6). So the capture begins with the level's loading events, and it holds the loading frames and the
precache frames before play. Past captures never showed it because they all used a 600-frame warm-up that ran out in
play. With wu1's rules every scored load's "30 s after loading" window would have started inside loading. The probe
caught it on the prime load: **one prime load of wu1 was spent (out/wu1_prime), no scored load ran, nothing was
scored.** (plan-facts/wu1-probe-stop-2026-11-26.md.)

The Overseer ruled option B: a NEW registration that keeps the exe and the flags and makes the scorer find the end of
loading inside the capture. **Changed** (only what the finding forces): 5.1 (where the capture starts, what each
captured frame is), 5.2 (time is counted from the first play frame E), 5.3 (play frames = N - E; slow-dump cap
recounted), 5.4 (load time from E and the clock lines; old method replaced), 4.3 (capture_lines order corrected,
stock_loop derived for the loading frames, five new stop-VOIDs), 6 / 6.1 (new constants CLOCK_TOL_MS and RACE_FRAMES,
new margin rows for the window start), 7.2 (NO VERDICT `loadtime` withdrawn: it can no longer fire), 3.3 (duration),
8 (probe prints the located end of loading). **Unchanged:** question, arms, exe, every flag, order and load count
(1 prime + 16 ON + 16 OFF, ABBA), OFF grouping rule, lever checks L1-L5, H1-H3 and their bounds, every other NO VERDICT
rule, exit codes, the save-frame rule, probe before any scored load, score-once command form, no retry.

**Review pass 1 (CodeRabbit, 2026-11-26; triage in wu2/coderabbit-triage-2026-11-26.md), still before registration:**
the probe FAILs on window_short (8); the load time is called an estimate with a code bound (5.4); the driver runs
from a hash-checked copy of the order file and rechecks the slice deadline right before each load (8, 3.3);
FAIL stopped_series (4.3, 7.1); -perf_nopresent / -perf_noinput required (4.3); clock_order now checks each clock
kind alone, the cross-clock comparison is reported only (4.3; one stated protection lost).

**Review pass 2 (CodeRabbit, 2026-11-26; triage section "pass 2"), still before registration:** **H3's measure is now the
IN-CAPTURE loading time** (5.4; the Overseer's decision): the engine-clock load time can miss time the engine timer
was paused during loading, the frames file's wall time cannot; the engine clock at E stays in the report beside it,
with a report-only note `loadtime_measures_differ` (5.4, 6.1: its bound is far UNDER 2x of the expected noise). A prime
conditions.txt that exists but cannot be read counts as not clean without stopping the scorer (4.3). The v_g_back
fixture's swapped bytes are checked by the self-test itself (8).

Every check, rule, number of loads and bound below is fixed before any run. Nothing of wu2 has been run on the box.
Sources: wu1 prereg (Revision 3) and NOTES; wu2/NOTES.md (the facts of out/wu1_prime with file:line); engine source
wt-simtick perf/alife-warmup d77d70b5c. Labels: MEASURED (a number in a file, cited), DESK (my count or reading of
existing files or code), GUESS. "Load" = one engine start that loads cordon1 and captures the loading and the first
~90 s of play. **MEASURED-PRIME** = a number from out/wu1_prime, the one real capture with this layout (an OFF load,
not scored, seen before this registration).

## 0. Limits first
- One box (Ryzen 9800X3D, Wine + DXVK, headless Xvfb 1280x720), no Present (`-perf_nopresent`), no input
  (`-perf_noinput`), uncapped frame rate. Nothing about the 2012 reference machine, native Windows, vsync or 4 cores.
- One save (cordon1), one load path (save load from the command line), player standing still. Same-level load, level
  change and new game are NOT tested (section 11).
- The lever's purpose is a real-time effect, so the order stock takes is luck: the OFF arm's group sizes are not under
  our control (3.2).
- This experiment does NOT prove simulation equivalence (section 10).
- cordon1 runs a scripted full-game save (itms_manager.script:245-246, 698-712); MEASURED-PRIME: capture frame 155,
  79.8 ms, 0.54 s after the end of loading. Excluded from the headline numbers by the fixed rule of 5.5; no second
  save inside 30 s (wu1 0, unchanged).
- Warm18.exe is a profiler build (about 0.7 ms per frame of probe cost); both arms carry the same cost.
- **wu2: the end of loading is located inside the capture from ONE real example.** The layout rules (5.1) are read
  from the code and checked on out/wu1_prime (an OFF load). No real ON load with this layout exists; the held-frame
  case (ON, `precache held K frames`) is checked on made data only. The PROBE of wu2's prime load (section 8) is the
  guard: if the layout is not found on its real files, the series stops with no verdict and nothing consumed.
- **wu2: the log alone cannot pin the engine's loading-end line to one frame.** In log order "Handled Necessary Textures
  Destruction" (xray.log line 8492 of wu1_prime) lies between the clock lines of capture frames 22 and 84. It is pinned
  to one frame (70) by code plus the engine's own call counters (5.1), and agrees with the countdown lines and with
  the intro frame visible in the slow file. See 5.1.
- **wu2: a one-frame move of one clock line in quiet frames is not detected** (about 4-6 ms, under the 15 ms clock
  tolerance). It cannot move the window (E comes from the counters). A renumbering of every device frame is invisible
  and harmless. **A uniform shift of every time_global against the mcm_log "Time continual is:" stamps is REPORTED
  (clock_cross), no longer a stop** (review pass 1, 4.3): the source does not settle that a normal load never pauses
  the engine timer, so a cross-clock gap cannot be told apart from valid data; such a shift moves the engine-clock load
  time and is printed per load, not caught. **Since review pass 2 it cannot move H3** (H3 is on the in-capture loading
  time, 5.4).
- **wu2 (review pass 2): H3's load time is the IN-CAPTURE loading time** (capture start -> E, the frames file's wall
  time, 5.4). It leaves out everything before the capture start (Wine start-up, device init, the level's creation up to
  "Prefetched Data": the same code in both arms, the lever not yet built). The engine clock at E (reported) is an
  ESTIMATE (5.4): the engine's own clock at E is not logged; it is inferred from a clock line in another frame, with a
  bound from the code (|error| <= the longer of frame E and that line's frame + 1 ms), not from the clock fit.
- **The note `loadtime_measures_differ` is weak** (6.1): its bound covers only the estimate errors (about 12 ms), while
  the engine time before the capture start varies from load to load by hundreds of ms (a proxy: 7504-8360 ms over 13
  earlier cordon1 loads). It is expected to fire on valid series and cannot tell a timer pause from that variation;
  it decides nothing.
- A load with NO alife_pass line or NO alife_opu line at all is a FAIL `no_measure_lines` (wu1, unchanged). The
  alife_opu guarantee rests on a stock script (bind_stalker.script:200).

## 1. Question (unchanged)
On cordon1 in real time, with the lever ON (`-alife_warmup`) against lever OFF (stock), in ABBA order:
(a) does every ON load show that the one full A-Life pass ran behind the loading screen;
(b) in the first 30 s of play after the loading screen (HEADLINE window; the supervisor's ~15 s reported beside it),
    are the worst frame and the number of slow frames of ON loads much smaller than those of the stock loads that took
    the IN-PLAY order;
(c) what does it cost in load time;
(d) (secondary) do ON loads look like the stock loads that took the good IN-LOADING order.

## 2. What runs (unchanged from wu1; copied rows marked)
Exe `run/bin/Warm18.exe` sha256 `15828a7a6cadd38e42cc376869f581166884c04f1eb236baf79054a74332ae9d` (CI build of
perf/alife-warmup d77d70b5c, profiler configuration). The SAME exe in both arms. Save cordon1 (pins in wu2_run.sh,
same as wu1). Settings: run/appdata/user.ltx as is, checked read-only before every load.

| switch / setting | value | source | what it produces / why |
|---|---|---|---|
| bench | tools/s2_bench.sh 072af8ed | wu1 (COPIED) | out/<tag>/: conditions.txt, xray.log, perf_*.csv |
| BENCH_FIXED | 0 (real time) | wu1 (COPIED) | dwTimeGlobal = wall clock (device.cpp:1110-1113) |
| `-perf_capture W,N,tag` | W = 0, N = 20000 (COPIED) | s2_bench.sh:56 | **wu2: the capture starts with the level's first loading event and holds 13 loading + 58 precache frames before play (5.1)**; the scorer finds the end of loading inside it |
| `-perf_quit -nointro -dbg -start server(cordon1/single/alife/load) client(localhost)` | | wu1 (COPIED) | |
| `-perf_noinput -perf_nopresent` | | wu1 (COPIED) | wu2 review pass 1: both REQUIRED in the logged command line (`arm_mismatch` by name, 4.3) |
| `-perf_slow` | 20 (COPIED) | profiler_agg.cpp:79-80 | per frame > 20 ms, the 14 largest self times; used for the save frame (5.5) and, wu2, the frame-1 marker of the loading layout (5.1) and the frames_slow check (4.3) |
| `-alife_warmup` | ON loads only | | the lever |
| BENCH_EXCLUSIVE=1, BENCH_NOCLAIM=1, BENCH_LOWEND=0, BENCH_TIMEOUT=900 | | wu1 (COPIED) | |
| BENCH_DISPLAY | :141, refused if /tmp/.X11-unix/X141 or /tmp/.X141-lock exists | wu1 (COPIED) | |
| frame limiter | none (uncapped) | wu1 5.3 | check: site "Eco Render" 0 calls |

## 3. Design
### 3.1 Loads and order (unchanged)
One priming load (OFF, not scored), then 32 scored loads, 16 ON and 16 OFF, in blocks ON OFF OFF ON (x8). One order
file `wu2_order.txt` read by both the driver and the scorer. Tags wu2_prime, wu2_01_on ... wu2_32_on, all lowercase.
No load is ever run twice.

### 3.2 Why 16 OFF loads (unchanged from wu1)
At least K = 3 valid OFF-IN-PLAY loads are needed (MIN_OFFP). p(in-play) about 0.68 over 22 past loads (wu1 3.2);
P(fewer than 3 of 16) <= 0.002 at p = 0.5. MEASURED-PRIME: the one Warm18 OFF load took the in-play order (wu2 NOTES
2.6), consistent with p 0.68; one load says nothing more.

### 3.3 Box time (re-derived for wu2)
MEASURED-PRIME: bench start 18:08:38, end 18:10:45 = 127 s (bench rc line: elapsed 124 s) for a 102.05 s capture
(10.94 s of it loading, 91.11 s play at a mean 4.57 ms). So about 25 s per load are outside the capture (Wine start,
device init to capture start, dump, quit, the bench's wait); the first load after idle may be the slowest of these.
Per load: 25 s + 10.9 s + 19,929 play frames x 4.1-5.0 ms (81.7-99.6 s) = 118-136 s (155 s at 6 ms frames). Driver
overhead per load: settle 20-60 s (GUESS, wu1; the prime had 0 s, it was the first), display grace 0-20 s. Per load
138-216 s. **33 loads: 76-119 min** (130 min at 6 ms frames). Usable 163 min of the 3 h slice (review pass 1: a load
starts only while >= BENCH_TIMEOUT 900 s + 120 s of cleanup = 17 min are left, rechecked right before the bench, after
the settle, the display wait and the claim renewal; the old 15 min check stays and is now inside it): 1.37-2.14x,
1.25x at 6 ms frames: **UNDER 2x at the slow end** (as wu1). A stopped series is FAIL and needs a new registration:
FAIL [missing] for the loads it did not run, and, when the stop left no folder missing (the MAX_CONFOUNDED-th
non-clean load was the last one), FAIL [stopped_series] (4.3, 7.1). ON loads add the pass (0.5-0.75 s) and any held frames (<= 300 frames,
< 2 s): within the per-load range.

## 4. Per-load checks
### 4.1 ON loads: the full pass ran behind the loading screen (unchanged, L1-L5 as wu1 4.1)
- L1 `lever_count`: exactly one `* [alife_warmup] full pass: N of M objects, frame F, precache_left P, X ms`.
- L2 `lever_after_intro`: that line before `intro_start game_loaded` and before the textures-handled line (log order).
- L3 `lever_precache`: P >= 3. L4 `lever_cap`: no `! [alife_warmup] hold cap hit` line. L5 `lever_partial`: N >= 0.9 M.
- Reported: `precache held K frames`, X ms. (wu2: the pass and any held frames lie in the precache block, capture
  13..E-1, outside the window by construction.)

### 4.2 OFF loads: which order stock took (unchanged, wu1 4.2)
OFF-IN-LOADING / OFF-IN-PLAY / OFF-OTHER exactly as wu1 4.2 (wide pass n > 20 with precache > 0 before intro_start
and >= 50 alife_slow lines in one frame before intro_start; or no wide pass, a 65534 widening and a later narrowing,
pass lines reaching the narrowing's time_global and no frame with >= 50 alife_slow lines; else OTHER). Never pooled.

### 4.3 Run validity (VOID: the load is not counted, no re-run; FAIL where marked)
- `missing` / `empty` / `unparsable` file, `arm_mismatch`, `off_not_stock`, `no_measure_lines`, `scorer_error`: **FAIL**
  by name (wu1, unchanged). wu2 review pass 1: `arm_mismatch` also when `-perf_nopresent` or `-perf_noinput` is absent
  from the logged command line (a capture scored standalone must show it ran with the registered measurement flags).
- **FAIL `stopped_series`** (review pass 1): the driver stops the series at the load that makes MAX_CONFOUNDED = 3
  non-clean loads (VERDICT not "clean"; the prime counts, as in wu2_run.sh). The scorer counts the same loads (the
  prime's conditions.txt is read for its VERDICT only; unreadable counts as not clean, as the driver's empty VERDICT
  does; review pass 2: also a file that exists but cannot be opened or read (OSError), which no longer stops the scorer
  before the report) and a series with >= 3 is FAIL by name, also when no folder is missing. wu2_score.py and wu2_run.sh hold the
  same MAX_CONFOUNDED (self-test check). Before this fix the text (3.3: "a stopped series is FAIL") and the code (a
  PASS was possible) disagreed; the code was fixed.
- VOID (wu1, unchanged): `not_clean`, `framecount` (frames rows = sites header = 20000), `frame_limiter`,
  `threshold_header`, `window_short` (wu2: under 30 s of play AFTER E), `save_unlocated`, `slow_cap`.
- VOID `capture_lines` **(corrected)**: xrprof started / finished lines with the tag and 20000 frames, in the log order
  **started < "Handled Necessary Textures Destruction" < finished** (the end of loading is inside the capture). wu1's
  rule (Handled < started) was the wrong one.
- VOID `stock_loop` **(derived for the loading frames)**: "Process seqFrame" main calls = N - L exactly, L = the
  "Loading..." main calls (each loading frame runs one loading event and returns before FrameMove, device.cpp:695-704);
  "Sim tick" 0 calls. L itself is checked by `loading_frames`.
- **New, wu2 (all stop-VOIDs: the driver stops the series; in the probe a PROBE FAIL by name):**
  - `loading_frames`: "Loading..." = "LoadDraw" calls = 11 + the net_start_client6 calls; net_start1-6 and
    net_start_client1-5 exactly 1 call each (the fixed event list of a save load, Level_start.cpp:95-100, 221-235;
    net_start_client6 repeats while the map sync is not done, Level_network_start_client.cpp:244-245); perf_slow names
    `CALifeSimulator::CALifeSimulator` in capture frame 1 and nowhere else (it is built in net_start2, the 2nd event:
    so frame 0 is net_start1 and frames 0..L-1 are the loading frames); "Precache frame" calls Pc >= 1 and
    "Start xrSASH Benchmark" calls = N - L - Pc; E = L + Pc < N.
  - `clock_fit`: the clock lines (every alife_pass / alife_opu line logged before the capture-finished line) are placed
    in the capture by ONE offset D (dwFrame = capture index + D) found from the times alone: over every D that puts all
    of them in FrameMove frames (capture L..N-1), the largest |time_global difference - sum of captured frame ms| over
    consecutive lines must be <= CLOCK_TOL_MS (15 ms) for the best D and > 15 ms for every other D (else "ambiguous").
    Fewer than 2 clock lines in the capture: VOID.
  - `loading_end`: every countdown line (precache P > 0; with held frames K > 0: P != 3) must say E: capture(F) + P
    (+ K if P > 3) in {E - 1, E} (RACE_FRAMES = 1: a line of the A-Life thread may read the countdown after that
    frame's End()). No countdown line: VOID.
  - `clock_order`: no clock line of capture > E-1 is logged before the textures-handled line and none of capture < E-2
    after it; **each clock kind alone** (the time_global of the alife_pass / alife_opu lines; the "Time continual is:"
    stamps) never goes back by more than CLOCK_BACK_MS = 1 ms in log order. Derivation (CODE, wt-simtick d77d70b5c):
    dwTimeGlobal and dwTimeContinual are written only in FrameMove (device.cpp:1051, 1112); an alife_pass line copies
    them at the start of its update and is logged at its end, inside the GameThread task (EngineThreading.cpp:161-166)
    that the main thread joins at device.cpp:918 before the next on_idle; alife_opu (alife_simulator_script.cpp:508) and
    the stamp (mcm_log.script:203-205 -> _g.script time_continual -> Device.dwTimeContinual,
    script_render_device_script.cpp:45-48) read them live in the same frame. So each kind is a run of frame samples
    that only goes forward: TimerGlobal is frozen while paused, never back; dwTimeContinual goes back only at an app
    re-activation (device.cpp:1248), after a deactivation that pauses the game (GamePersistent.cpp:959), i.e. a load
    that did not run as registered. The lever's full pass, a wide stock pass and held frames only make one update
    longer inside its own frame: they cannot put a newer value before an older one (review pass 1, finding 8: the
    concern is refuted by the code). MEASURED-PRIME: both kinds monotone.
  - **reported only, `clock_cross`** (review pass 1; was part of clock_order): the largest step back across the two
    kinds (a stamp below an earlier time_global or the reverse). Their same-frame gap is TimerMM - TimerGlobal: 0 by
    code (started together, Device_Initialize.cpp:40-41; one QPC clock; read 61 lines apart in FrameMove) unless the
    engine timer was paused (Device::Pause -> g_pauseMngr, device.cpp:1153-1155: UI sequences UIGameTutorial.cpp:192-194,
    focus loss GamePersistent.cpp:959), the app deactivated (device.cpp:1248, 1266) or time_factor changed (TimerGlobal
    only, device.h:363-367; console_commands.cpp:2049, 2160). Whether a normal load does one of these is NOT settled by
    the source (the "game_loaded" sequence's pause flag is in an xml I could not read; scripts can run time_factor), so
    a stop here could stop valid data. MEASURED-PRIME: gap 0 ms in all 3 same-frame pairs (log lines 7045/7046,
    8130/8132, 8527/8553), before and after intro_start. **Protection lost:** a uniform shift of every time_global
    against the stamps (self-test real_prime_t_plus_1000: +1000 ms) is no longer a stop; it is printed per load (probe /
    check note, report line). Such a shift moves that load's engine-clock load time; since review pass 2 not H3 (5.4).
  - `frames_slow`: every perf_slow row's frame_ms equals the frames file's ms of that frame to 0.0005 ms (the profiler
    writes both from the same ticks with the same scale, profiler_agg.cpp:153, 166, 187, 210; MEASURED-PRIME 434 of 434).

## 5. Measures in player units
### 5.1 Where the capture starts and what each captured frame is (REWRITTEN; facts in wu2/NOTES.md 2.1-2.4)
- **Capture start (CODE).** perf_autocapture() runs right after PROF_FRAME at the top of every on_idle (device.cpp:
  675, 682). With warm-up 0 it starts the capture in the first on_idle where `g_pGameLevel && g_bLoaded &&
  !dwPrecacheFrame && !Paused()` (device.cpp:422): g_bLoaded is TRUE since the main menu (device.cpp:1125), the
  precache countdown is still 0, so this is the first on_idle after the level object exists. xrprof::start sets the
  tick base (profiler_agg.cpp:100) and the next PROF_FRAME stores frame 0 (profiler_agg.cpp:187): capture frame 0 is
  the rest of that on_idle, which runs the first loading event, net_start1 (MEASURED-PRIME: 1.53 ms, the started line
  3 lines after "Prefetched Data").
- **Loading frames: capture 0..L-1** (MEASURED-PRIME L = 13, 6239.8 ms). While a loading event is pending, each on_idle
  runs one event and its LoadDraw and returns before FrameMove (device.cpp:695-704): no seqFrame, no time_global
  update. The 13 = net_start1-4, net_start_client1-5, net_start_client6 twice, net_start5, net_start6. Frame 1 is
  net_start2 (CALifeSimulator, 5761 ms). Checked by `loading_frames`.
- **Precache frames: capture L..E-1** (MEASURED-PRIME 58 frames, 4700.8 ms, with 1234, 2494 and 489 ms frames).
  FrameMove runs; the countdown set by net_start_client6 (PreCache(60), Level_network_start_client.cpp:273) is > 0 at
  "Precache frame" (device.cpp:773) and counted down by End() (device.cpp:157-185; held at 3 while the lever's pass is
  pending, ON only). Pc = the "Precache frame" calls. intro_start acts in the first frame with countdown <= 2
  (GamePersistent.cpp:564): MEASURED-PRIME capture 69 (a 52 ms lua GC, slow row "CGamePersistent OnFrame").
- **The engine's loading-end line** "Handled Necessary Textures Destruction" is printed in End() of the frame whose
  End() takes the countdown from 1 to 0 (device.cpp:166-188): the last precache frame, **capture E-1**.
- **End of loading (registered): E = L + Pc, the first play frame** (MEASURED-PRIME E = 71, 10940.6 ms after capture
  start). It is the frame right after the End() that printed the loading-end line.
- **Cross-checks (each a stop-VOID, 4.3):** the clock lines are placed in the capture by the fitted offset D (clock_fit)
  and their countdown must say E within one frame (loading_end); the loading-end line must lie between them in log
  order (clock_order); the frames file must match the slow rows (frames_slow).
- **Condition (1) of the Overseer, on wu1_prime (the probe prints all of these; self-test real_prime_probe):**
  engine counters: loading-end line in capture 70, E = 71; countdown lines: 71, 71, 70, 71 (dwFrame 31 P58, 33 P56,
  34 P54, 40 P49; D = 18) -> loading-end at 70; log order: between capture 22 and capture 84 (the log alone cannot do
  better); intro marker capture 69 = E-2. **Located 70 vs the engine line's frame 70: 0 frames apart** (the per-line
  spread is 0-1 frame, the A-Life read race).
- If the game were paused at the capture start the capture would start later; then net_start1 would be missing from
  the capture and `loading_frames` would VOID the load (named), not shift the window.
### 5.2 Frames file
`perf_frames_<tag>.csv`: line 1 `frame,ms`, then `i,ms` per captured frame, i from 0, ms = wall time from one
PROF_FRAME to the next. **wu2: time since the end of loading of frame i >= E = sum of ms[E..i-1]. The 30 s headline
window = frames i >= E starting before 30,000 ms after E; the 15 s window likewise. Loading frames (0..L-1) and precache
frames (L..E-1) are never in a window** (self-test loading_outside_window: frames of 5761, 1234, 2494 ms before E, ON
worst 5.3 ms, n20 0; a mutant that starts the window at frame 0 is caught; MEASURED-PRIME: 71 frames, 10 of them
> 20 ms, worst 5761.1 ms, all outside; the window holds worst 339.8 ms, n20 20).
### 5.3 Capture length, the slow-dump cap and the frame limiter (re-derived for wu2)
- Cost per frame and file sizes: wu1 5.3, unchanged (one float per frame; ~13 bytes a CSV line; MEASURED-PRIME frames
  file 269,544 bytes).
- **Play frames = N - E**, not N: MEASURED-PRIME 20000 - 71 = 19,929; an ON load with K held frames has 19,929 - K.
  At the 3.0 ms floor (wu1 5.3) 19,929 frames = 59.8 s = **1.99x the 30 s window (UNDER 2x, by 0.2 s; was exactly 2.0x
  in wu1)**; with the cap K = 300: 58.9 s, 1.96x. MEASURED-PRIME: 91.1 s (mean 4.57 ms, min 3.76 ms): 3.0x. VOID
  `window_short` below 30 s of play after E (kept). N is not raised: the flags are the registered ones (Overseer's
  ruling); the floor is 22 % under the fastest MEASURED-PRIME frame.
- **-perf_slow cap (recounted).** The cap (rows + 14 <= 8400, profiler_agg.cpp:43, 190) counts rows from capture START,
  so the loading and precache frames over 20 ms take rows too: MEASURED-PRIME 10 such frames (140 rows) before E; the
  whole capture 31 slow frames, 434 rows. wu1's "the cap cannot be reached before 12 s after loading" is wrong for wu2
  (its 12 s start at capture start, 10.9 s before E). New bound by counting: the cap needs 600 slow frames, so it
  cannot be reached before capture index 599; the save frame (MEASURED-PRIME index 155; P12) cannot lose its rows while
  its index is under 599. It can in principle be reached within the 30 s window; VOID `slow_cap` (> 8386 rows) stays.
  Expected rows: < (45 + 10) x 14 = 770, 11x below 8386.
- Uncapped (wu1 5.3, unchanged).
### 5.4 Load time (REWRITTEN; review pass 2: H3 on the in-capture loading time)
**H3's measure (review pass 2): the IN-CAPTURE LOADING TIME = sum of the frames file's ms of capture frames 0..E-1**
(capture start -> start of the first play frame; MEASURED-PRIME 10940.6 ms). H3: median ON minus median OFF-IN-PLAY
<= 2.0 s (7.3; derivation 6). Why it is the lever's price and does not depend on the engine clock:
- The capture starts in the first on_idle after the level object exists, right after "* [x-ray]: Prefetched Data", in
  BOTH arms (5.1; device.cpp:422; MEASURED-PRIME: the started line 3 lines after it). Everything before the capture
  start (Wine, device init, the main menu frames, the level's creation) is the same code in both arms: the lever's
  state is armed when the simulator is built (CALifeUpdateManager, alife_update_manager.cpp:82-104) in net_start2,
  capture frame 1, and the only difference before it is the `-alife_warmup` text in the command line. The lever acts
  only inside the capture: its full pass and held frames lie in the precache block 13..E-1 (4.1, 5.1). So the ON minus
  OFF difference of in-capture loading times is the lever's price.
- It cannot be paused (CODE, wt-simtick d77d70b5c): the only PROF_FRAME is device.cpp:675, at the top of every on_idle
  once the device is ready (device.cpp:669-673), PROF_FRAME = xrprof::frame() (profiler.h:123); frame() stores
  `__rdtsc()` minus the previous frame's (profiler_agg.cpp:185-187, 217); dump() scales all ticks by the QPC time of
  the whole capture over its TSC ticks (profiler_agg.cpp:116-121, 153); the base is set at the start (profiler_agg.cpp:
  99-100). No timer object is involved. TimerGlobal (time_global) is a CTimer_paused (device.h:124) registered with
  g_pauseMngr (FTimer.h:194), whose Pause adds the paused span to qwPauseAccum (FTimer.cpp:37-45, FTimer.h:173-187;
  elapsed = QPC - start - accum, FTimer.h:46), reached from Device::Pause (device.cpp:1155) and the freeze absorb
  (AbsorbGap, device.cpp:1105, FTimer.h:41; default off); time_factor scales it too (device.h:363-367). None of these
  touches the frames file: a paused span inside the capture stays in the frames' ms (on_idle keeps running; a frame
  that waits holds the wait in its ms). Limit: the TSC-to-QPC scale is one factor for the whole capture (ppm-level
  error, about 0.1 ms over 10 s).

**Engine clock at E (reported beside it, deciding nothing).** Engine-clock load time = time_global (ms since device
init) at the first play frame E. From the clock lines placed by D (5.1):
from the first clock line with capture c >= E: T - sum(ms[E..c-1]); if there is none, from the last one before E:
T + sum(ms[c..E-1]). **This is an ESTIMATE of the engine's clock at E, which is not logged** (review pass 1, finding 2).
What bounds it (CODE): a clock line of capture frame c holds time_global as read at device.cpp:1112, o_c ms after that
frame's PROF_FRAME (device.cpp:675); the frames file's ms run from one PROF_FRAME to the next on the same QPC clock as
TimerGlobal (profiler_agg.cpp:119-121 scales the TSC ticks to QPC time; FTimer.h CPU::QPC), and FrameMove (device.cpp:744)
runs between them, so 0 <= o_c <= ms[c] and 0 <= o_E <= ms[E]. The estimate is the start of frame E + o_c; the engine's
value at E is the start of frame E + o_E. **Error = o_c - o_E, in [-ms[E], +ms[c]], plus 1 ms of integer time_global**,
provided the engine timer was not paused between frames c and E (clock_cross, 4.3, would show a pause against the
stamps). The scorer prints this bound per load. **The clock fit does not narrow it:** its residual (<= 15 ms,
MEASURED-PRIME 4.27 ms) compares the offsets o at the clock-line frames with each other; it says nothing about o_E.
What the fit does support: the clock lines are placed in the right capture frames (D unique), and the offsets o at
those frames agree to within the residual. MEASURED-PRIME: **19876 ms = 19.88 s** both ways (19876.37 / 19876.39 ms);
bound -5.9 / +5.5 ms (frame E 4.9 ms, frame 84 4.5 ms). (Before review pass 2 this bound entered H3, >= 300x against
2.0 s; H3 no longer uses this estimate.) The old 5.4 method
(F0 = F + P, frames counted from capture 0) is withdrawn: the capture now holds the frames between every clock line
and E, and E comes from the counters. With held frames, the countdown check uses F + P + K for P > 3 and skips P = 3
(wu1 Revision 3 rule, kept). Every valid load has a load time (E and the fit are stop-VOID conditions), so NO VERDICT
`loadtime` cannot fire and is withdrawn (7.2). Reported beside it, deciding nothing: loading_in_capture_s = sum of
ms[0..E-1] (capture start -> E; MEASURED-PRIME 10.94 s; it covers every loading event, from the simulator's
construction on, where the lever is armed and runs). **Review pass 2: that sum is now H3's measure** (above); the
engine-clock load time is the one reported beside it.

**The two measures side by side (report-only note `loadtime_measures_differ`).** Per load, engine clock at E minus
in-capture time = G + e: G = the engine time at the capture start minus any timer pause / absorb inside frames 0..E-1,
e = the estimate's error, in [-(ms[E] + 1), +(ms[c] + 1)] ms (above). If G were the same in every load, the median
being monotone in each value gives: (ON - OFF-IN-PLAY delta of the engine clock) - (the same delta of the in-capture
time) in [-(max over ON of ms[E] + 1 + max over OFF-IN-PLAY of ms[c] + 1), +(max ON ms[c] + 1 + max OFF ms[E] + 1)]
(MEASURED-PRIME frame sizes: about +-11.4 ms; made loads +-12 ms). The report prints both deltas, their difference and
this bound; outside it, the named note `loadtime_measures_differ` (report only, not a verdict rule). G is NOT the same
in every load (6.1): the note is a pointer, weak by construction.
### 5.5 The save frame (unchanged, wu1 5.5)
### 5.6 Headline numbers per load (30 s window from E, save frame excluded)
worst frame (ms); n20; n33; train span (s); loading_in_capture_s (s, H3's measure, review pass 2). Secondary, reported
only: the 15 s window; worst including the save frame; X ms and held frames (ON); engine_clock_load_s (s, engine clock
at E, an estimate). Player line in the report: "loading takes X s longer / shorter with the lever" (median in-capture
loading time ON minus OFF-IN-PLAY).

## 6. Constants, each with its derivation
All wu1 constants (section 6 of wu1 Revision 3) are kept with their wu1 derivations: WINDOW 30 s, SLOW1/SLOW2,
LEGACY_WIDE 50, WIDE n > 20, P_MIN 3, N_M_MIN 0.9, MIN_ON 8, MIN_OFFP 3, STOCK_HITCH_MIN 150 ms, STOCK_TRAIN_MIN 7,
WORST_RATIO 0.5, WORST_STRAY 1, N20_DROP 7, PRICE_MAX 2.0 s (review pass 2: re-derived for the in-capture measure, below),
stock_orders_alike, SAME_* (GUESS, reported only; SAME's load term now also on the in-capture time),
MAX_CONFOUNDED 3, N 20,000, SLOW_CAP_ROWS 8386, HOLD_CAP 300 (COPIED from the patch). Their wu1 derivations were made
for a window that starts at the first play frame; wu2's window does start there (5.2), so they apply as derived.
**Removed:** LOADTIME_MIN_ON / LOADTIME_MIN_OFFP (5.4, 7.2). **New:**

| constant | value | derivation |
|---|---|---|
| CLOCK_TOL_MS | 15 ms | DESK from MEASURED-PRIME: the residual between two clock lines = the difference of the two frames' time from PROF_FRAME to the time_global read in FrameMove (device.cpp:675-744 and 1112) + 1 ms of integer time_global; over the 23 consecutive pairs of wu1_prime the max is 4.27 ms (15 is 3.5x). On the wrong side: the best wrong offset (D=17) leaves 1213.2 ms (81x the tolerance), because the countdown lines span the long precache frames. Derived from the same single load whose numbers it is tested on: flagged |
| RACE_FRAMES | 1 | DESK from MEASURED-PRIME: an alife_pass line is printed by the A-Life thread, which may read the countdown after that frame's End(): MEASURED dwFrame 34 says precache 54 where FrameMove saw 55 (1 of 4 countdown lines, -1 frame). Only -1 is allowed (a late read only lowers the countdown) |
| E-2 / E-1 in clock_order | log-order bracket | DESK: a main-thread line of frame f is printed before End(f); an A-Life-thread line of frame f may be printed after End(f) (one frame), plus its read race (one frame) |
| SLOW_MS_EQ | 0.0005 ms | DESK: frame_ms and the frames file are the same float ticks times the same ms_per_tick, printed with 4 decimals (profiler_agg.cpp:153, 166, 187, 210); MEASURED-PRIME 434 of 434 rows equal. Not a noise bound (none expected); no constant mutant |
| loading event list | 11 single + net_start_client6 >= 1 | CODE (5.1); MEASURED-PRIME 13 |
| CLOCK_BACK_MS | 1 ms | CODE (4.3): each clock kind is a run of frame samples that never goes back; 1 ms is the integer step of a u32 ms value; it was the literal 1 of clock_order before review pass 1. Constant mutants 0 / 2 |
| MAX_CONFOUNDED (now also in the scorer) | 3 | wu1 GUESS (unchanged); the scorer holds the driver's value (self-test checks the two agree) for FAIL stopped_series. Constant mutants 2 / 4 |
| PRICE_MAX_S (H3, review pass 2) | 2.0 s | re-derived for the in-capture measure from wu1's parts: the lever moves one full pass into loading, 0.5-0.75 s (wu1 DESK: alife_slow sums of the wide loading pass 506-573 ms in 7 in-loading logs, plus objects under 1 ms); the pass and any held frames (<= 300 x ~5 ms, 3.3) lie inside capture frames 13..E-1 (4.1, 5.1): so the whole price is in the in-capture time and none of it before the capture start. 2.0 = 2x the 1.0 s upper end of the moved pass: more means something else (held frames, a second pass) was added (wu1's reading, kept). wu1's load-to-load spread (0.2-0.4 s, MEASURED engine-clock stamps) was of the engine clock; the in-capture time drops the pre-capture part of it (not measured: one load) |
| loadtime_measures_differ bound | per series, -(max ON lo + max OFF up) / +(max ON up + max OFF lo), lo = ms[E] + 1, up = ms[c] + 1 ms | CODE (5.4: the estimate's error per load; a median is monotone in each value). Report only. MEASURED-PRIME frames: about +-11.4 ms |
| END_RESERVE (driver) | 120 s | DESK: after a bench that runs to BENCH_TIMEOUT: the bench's own trap (wine + Xvfb), the check= run, withdraw / release, the notify call (timeout 60 s). GUESS for everything but the 60 s |

### 6.1 Margin table (wu1 Revision 3 rows kept as registered there; NEW rows for the window start; UNDER 2x flagged; COPIED flagged)
**New rows (wu2, the window start):**
| item | expected | bound | margin | label / status |
|---|---|---|---|---|
| E from the counters vs the countdown lines | 0 frames (MEASURED-PRIME: best line 0; per line 0, 0, -1, 0) | every line in {E-1, E} | 1 frame of slack on the one line that used it | DESK from MEASURED-PRIME (one load); exact by code otherwise |
| engine loading-end line vs E | line 8492 in capture E-1 = 70 (counters), 70 (countdown), 70 (intro marker + 1) | E-1 | 0 frames | MEASURED-PRIME + CODE; the log order alone: 22 < 70 < 84 |
| log-order bracket of the loading-end line | before it: clock lines up to capture 22; after it: from capture 84 | <= E-1 = 70 / >= E-2 = 69 | 48 frames / 15 frames | MEASURED-PRIME; the 15-frame side rests on the 10 s pass-log window's spacing (pass lines ~450 ms apart) |
| clock fit residual | 4.27 ms max (23 pairs) | <= 15 ms | 3.5x | MEASURED-PRIME; one load: thin. Not a bound on the load time at E (5.4) |
| load-time estimate at E | MEASURED-PRIME bound -5.9 / +5.5 ms | (reported; no longer H3's measure) | - | CODE bound (5.4), per load in the report; review pass 2: H3 moved to the in-capture time, so the old row (>= 300x against 2.0 s) is withdrawn |
| H3 price, in-capture loading time (review pass 2) | -0.3 to +0.8 s (wu1 P9's range, GUESS; the lever's work 0.5-0.75 s + held frames, all inside the capture) | <= 2.0 s | 2.5x | RE-DERIVED for the new measure (6); the expected range is wu1's GUESS, carried; the in-capture spread is not measured (one OFF load: 10.94 s) |
| loadtime_measures_differ: bound vs expected difference | ON - OFF difference of the engine time before the capture start: tens to hundreds of ms (proxy, MEASURED: the mcm_log stamp printed during the loading events, i.e. TimerMM at the last FrameMove before the capture, 7504-8360 ms over 13 earlier cordon1 loads cord1-3, cp1-4, cpl1, uc1, lc1-3, wu1_prime; range 856 ms; it covers only part of G: in wu1_prime G = 19876 - 10940.6 = 8935 ms vs the stamp 7749 ms) | about +-12 ms (estimate errors only) | **far UNDER 2x (below 0.1x)** | the note is expected to fire on valid series (the made `good` series shows it: +0.736 s from the fixtures' spread of engine time); report only, decides nothing |
| clock fit discrimination | next offset 1213.2 ms | > 15 ms | 81x | MEASURED-PRIME |
| loading frames L | 13 = 11 + 2 | 11 + net_start_client6 calls, exact | exact | CODE + MEASURED-PRIME |
| loading + precache frames outside the window | 71 frames, 10.94 s, worst 5761 ms | none in the window | by construction (5.2) | proven on made fixtures and on wu1_prime (self-test) |
| play frames cover 30 s | 59.8 s at the 3.0 ms floor (19,929 frames); 58.9 s with K = 300 held; 81.7-99.6 s at 4.1-5.0 ms; MEASURED-PRIME 91.1 s | 30 s | **1.99x at the floor (UNDER 2x)**, 1.96x with the cap; 2.7-3.3x expected; 3.0x measured | re-derived; VOID window_short below 30 s on a scored load; on the PRIME load a PROBE FAIL (review pass 1, 8) |
| capture covers 15 s (reported) | as above | 15 s | 3.9-6.6x | re-derived |
| stock hitch after E | MEASURED-PRIME 6.44 s (339.8 ms); wu1 bound at most 7.8 s | inside 30 s | 4.7x (3.8x on wu1's bound) | one load; wu1's bound was derived for a window starting at the first play frame, which is E |
| stock train end after E | MEASURED-PRIME 20.57 s (last frame > 20 ms in 30 s); wu1 bound at most 22.1 s | inside 30 s | 1.46x (1.36x on the bound) | **UNDER 2x** (as wu1); backed by no_stock_train |
| stock n20 (30 s from E) | MEASURED-PRIME 20; wu1 16-19 | >= 7 | 2.9x (2.3x on wu1's low end) | one load |
| stock worst (30 s from E) | MEASURED-PRIME 339.8 ms; wu1 293-361 | >= 150 ms | 2.3x (1.95x on wu1's low end: **UNDER 2x**, as wu1) | |
| slow-dump cap | MEASURED-PRIME 434 rows (31 frames, 10 before E); expected < 770 | 8386 rows | 19x measured, 11x expected; not reachable before capture index 599 (save at 155) | recounted for wu2 (5.3) |
| load time measurable | every valid load (E + fit are stop-VOIDs) | - | - | wu1's UNDER-2x row "load time measurable" is withdrawn |
| load time, engine clock at E (reported) | MEASURED-PRIME 19.88 s | (P11 18-25 s, a prediction) | - | |
| loading_in_capture_s (H3's measure) | MEASURED-PRIME 10.94 s | (P17 10-13 s, a prediction) | - | |
| series duration | 76-119 min (130 min at 6 ms frames) | 163 min usable (a load starts only with >= 900 + 120 s left) | 1.37-2.14x; 1.25x at 6 ms | **UNDER 2x at the slow end** (re-derived from the prime's 127 s bench; review pass 1: 165 -> 163 min) |
| slice end vs a started load | a load starts with >= 1020 s left; it ends by BENCH_TIMEOUT 900 s + cleanup | slice end | 120 s of reserve after a timed-out bench | DESK; END_RESERVE GUESS except the notify's 60 s |
| load duration (bench) | 118-136 s (MEASURED-PRIME 127 s) | BENCH_TIMEOUT 900 s | 6.6-7.6x | bound COPIED (bench default) |

**wu1 rows kept as they were registered** (valid OFF-IN-PLAY loads 3.7x; H1 ON worst 2.0-7x, UNDER 2x at the high
GUESS; H1 strays; H2 drop 2.0-2.7x; stock_orders_alike rows; L3 8x; L5; LEGACY_WIDE 2.6x; held frames
COPIED; settle COPIED; CONFOUNDED GUESS; SAME_* GUESS). Their numbers were derived for a window from the first play
frame; that is where wu2's window starts. Not re-derived; flagged as carried over. (wu1's H3 price row 2.5x is no
longer carried: review pass 2 re-derived it for the in-capture measure, row above.)

## 7. Verdict rules (implemented exactly in wu2_score.py)
1. **FAIL** (exit 1) if any scored load has: a missing/empty/unparsable file (named), arm_mismatch, off_not_stock, any
   ON lever check L1-L5 failing, no_measure_lines, or scorer_error; or if the series has >= MAX_CONFOUNDED (3)
   non-clean loads, prime included (`stopped_series`: the driver stopped it, 3.3). A failing ON load is never dropped.
2. Else **NO VERDICT** (exit 3) if any of: `few_on` (< 8 valid ON), `few_inplay` (< 3 valid OFF-IN-PLAY),
   `no_stock_hitch` (median OFF-IN-PLAY worst < 150 ms), `no_stock_train` (median OFF-IN-PLAY n20 < 7),
   `stock_orders_alike`. All that apply are listed. **wu2: `loadtime` withdrawn** (5.4).
3. Else **PASS** (exit 0) if H1, H2, H3 (wu1 7.3; worst and n20 in the 30 s window from E; **review pass 2: H3 = median
   ON minus median OFF-IN-PLAY in-capture loading time <= 2.0 s**, 5.4). Otherwise FAIL naming H1/H2/H3.
4. Secondary, reported only: SAME AS GOOD ORDER (unchanged, except that its load term uses the in-capture time too).
5. n33, train span, the 15 s window, worst including the save, X ms, held frames, engine_clock_load_s, the note
   loadtime_measures_differ, OFF-OTHER: reported only.

## 8. Scorer, self-test, driver
`wu2_score.py` (stdlib, `python3 -I`), `wu2_selftest.sh`, `wu2_run.sh` (bash -n only), `wu2_order.txt`; hashes in
NOTES.md; pinned into the driver at registration (PIN_ME until then: the driver refuses).
Self-test 2026-11-26 (draft, after review pass 2): `WU2 SELFTEST PASS 259/259 (28 real cases, 144 made cases, 58 hook
mutants caught, 18 constant mutants caught, 1 hook-coverage check, 1 MAX_CONFOUNDED agreement check, 1 fixture-bytes
check, 2 find-error checks, 5 driver checks, 1 untouched check)`; command
`cd wt-render-split/perf-harness/wu2 && nice -n 19 bash wu2_selftest.sh`; folder in NOTES section 6. It contains:
out/wu1_prime read in place as a real fixture on which the probe PASSES with the numbers of 5.1; 12 spliced mutants of
it (one changed file each, the rest symlinks), each failing by name or, for the two stated limits, passing as stated:
opu line moved one frame onto the long frames (clock_fit), pass line frame 40 -> 41 (clock_fit, 17.0 ms), countdown
lines removed (no_measure_lines + clock_fit ambiguous), countdown set to 0 (loading_end), all clock lines removed
(no_measure_lines + clock_fit), every time_global + 1000 (review pass 1: PASSES with the clock_cross note, the stated
limit of 4.3), two play pass lines swapped and one stamp lowered (clock_order, each kind alone), a play line moved
before the loading-end line and the loading-end line moved before a countdown line (clock_order), frames file one frame
early (frames_slow), frames file without the loading frames (framecount), sites file without "Loading..."
(stock_loop + loading_frames), every device frame + 1 (passes, D = 19: limit), one play line moved one frame (passes,
4.96 ms: limit). Made loads now hold the loading frames as the engine writes them (13 loading frames with wu1_prime's
ms, 58 precache frames, then play; the clock lines' frames and times follow the frames file); every wu1 case kept,
re-pointed; new made cases for each new check on both sides (race 1 ok / 2 VOID; clock 14 ms ok / 16 ms VOID; flat
frames: ambiguous). Every hook mutant must be caught by EVERY listed target case. Constant mutants: wu1's 9 plus
CLOCK_TOL 13 / 17 / 3 and RACE_FRAMES 0 / 2, and (review pass 1) CLOCK_BACK_MS 0 / 2, MAX_CONFOUNDED 2 / 4. Review
pass 1 also added: the held-frame fixture holds K = 12 frames IN ADDITION to the 58 (precache block 70 frames, E = 83,
checked by the window start and the countdown lines; the same fixture with K = 0 gives E = 71); a prime whose capture
covers 23.7 s after E (PROBE FAIL window_short); stopped series (3rd non-clean load last; non-clean prime + 2) and
the 2-non-clean control (PASS); command lines without -perf_nopresent / -perf_noinput; the untouched check's find
errors on an existing folder (missing -newer file; unreadable subfolder) with a mutant that ignores find's status;
the driver's function block (order copy + hash, dispatch from the copy, slice deadline) run offline on a copy of
wu2_order.txt with 3 driver mutants, plus a static check of the driver body. Review pass 2 added: h_price_capture (in
capture +3 s, engine clock as the good loads: H3 FAIL) and h_price_engine_only (engine clock +3.7 s, in capture the
same: H3 ok, note loadtime_measures_differ), each catching the mutants h3_capture (H3 on the engine clock) and
lt_differ (note never printed); loadtime_agree (both measures equal: "agree"); s_prime_unreadable_ok /
s_prime_unreadable (prime conditions.txt mode 000: not clean, report written; with 2 more non-clean loads
stopped_series; skipped as root) catching prime_oserror; a check that the v_g_back fixture's own xray.log holds the
later time_global line before the earlier one.
Score ONCE after the series, output written in the same call:
`python3 -I wt-render-split/perf-harness/wu2/wu2_score.py root=out order=wt-render-split/perf-harness/wu2/wu2_order.txt out=out/wu2_state/wu2_score.txt`
Driver: wu1_run.sh with wu2 names and paths (state out/wu2_state). Unchanged: hash preflight (PIN_ME refuses), display
:141 rule, user.ltx read-only check, state folder created before the queue, cleanup trap before the first queue
operation, claim / renewal / release by EXIT CODE only, the box claimed through the WANT queue by the driver itself (no
look for other scripts), stop rules, no retry.
- **Probe (registered).** After the PRIME load and before the first scored load:
  `python3 -I wu2_score.py probe=out/wu2_prime order=wu2_order.txt` (exit 0 / 1). It applies every per-load rule of
  this prereg to the prime load's real files, requires a located save frame, and **prints the located end of loading
  (E as capture index, ms from capture start, engine time at E), the offset D with its residual and the next offset's,
  each countdown line's E, and the engine loading-end line's frame from the counters and from the countdown lines,
  its log-order bracket (capture indices) and the intro marker, as numbers.** Its info line gives the in-capture loading
  time (H3's measure) and the engine clock at E with its bound (review pass 2). **It also requires the whole 30 s
  headline window after E inside the capture: `window_short` is a PROBE FAIL by name on the prime (review pass 1),
  while on a scored load it stays a VOID that does not stop the series.** A failed probe ends the attempt with NO
  verdict (wu1 8, unchanged).
- **Per-load check.** After every scored load: `check=out/<tag>`; stop on any FAIL and on VOID framecount /
  capture_lines / stock_loop / frame_limiter / threshold_header and **wu2: loading_frames / clock_fit / loading_end /
  clock_order / frames_slow**; not on not_clean (counted, stop at the third), window_short, save_unlocated, slow_cap;
  clock_cross is printed as a note.
- **Order file in the driver (review pass 1).** After the hash preflight the driver copies wu2_order.txt into the new
  state folder (read-only), checks the COPY's sha256 against the registered pin and its shape again, and from then on
  reads only the copy (dispatch and every probe= / check= call). The scorer does not check the order hash in code
  (declined): its report prints the order file's sha256 in its header and I compare it with the pin by hand at scoring.
- **Slice deadline (review pass 1).** Right before a load starts (after the settle, the display wait and the claim
  renewal) the driver stops unless >= BENCH_TIMEOUT 900 s + END_RESERVE 120 s of the slice are left.

## 9. Predictions (registered; GUESS unless marked)
P1-P10, P12-P13 as wu1 9 (unchanged; review pass 2: P9's load-time price is read on the in-capture loading time).
P11 (load time, engine clock at E, 18-25 s in every load; MEASURED-PRIME 19.88 s; reported only since review pass 2)
85 %. New: P15 every scored load passes loading_frames, clock_fit, loading_end, clock_order and frames_slow 90 % (the
likeliest stop: a clock residual over 15 ms in a slow-loading load). P16 L = 13 in every load 85 %. P17 median
loading_in_capture_s 10-13 s, ON minus OFF-IN-PLAY -0.3 to +0.8 s (since review pass 2 this is H3's measure) 60 %.
P18 (review pass 2): the note loadtime_measures_differ is printed on the real series 70 % (6.1: its bound is far
under the expected spread).

## 10. Rule One (unchanged, wu1 10)
## 11. The patch's two doubts (unchanged, wu1 11)
## 12. What this cannot show (unchanged, wu1 12)

## 13. Registration
DRAFT by an Opus sub-worker for EnginePerf, 2026-11-26. Not registered. Pins (sha256), filled at registration:
wu2_order.txt PIN_ME; wu2_score.py PIN_ME; wu2_selftest.sh PIN_ME; wu2_run.sh PIN_ME; run/bin/Warm18.exe
15828a7a6cadd38e42cc376869f581166884c04f1eb236baf79054a74332ae9d (unchanged); bench, save and tool pins in wu2_run.sh
(copied from wu1, unchanged). Departures from the Overseer's wording, stated plainly: E is taken from the engine's
call counters (sites file) and the clock lines + frames file are the cross-check, not the other way round (NOTES 3.1);
the log alone cannot pin the loading-end line to one frame (5.1, 0).
