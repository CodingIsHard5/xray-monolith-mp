# PREREG wu1: does `-alife_warmup` remove the slow frames after a level load? (2026-11-26; registered by its commit; nothing run yet)

Every check, rule, number of loads and bound below is fixed before any run. Nothing here has been run on the box. No box
run without the Overseer's GO. Sources: plan-facts/B2b-warmup-design-2026-11-26.md (with its CORRECTED section and the
Overseer's prereg requirements), B2b-desk-read-2026-11-26.md, B2b-patch-notes-2026-11-26.md; harness conventions from
perf-harness/sf1. Labels: MEASURED (a number in a file, cited), DESK (my count or reading of existing files or code),
GUESS. "Load" = one engine start that loads cordon1 and captures the first seconds of play.

**Revision 2 (2026-11-26, decided by the experiment's owner): the HEADLINE window is 30 s, not 15 s.** The supervisor
asked for "~15 s". By the numbers in this draft stock's ~300 ms hitch comes 6.5-7.8 s after loading and its slow train
(27 passes 452 ms apart) runs to about 21-22 s, so a 15 s window sees the hitch and only the train's start: in the real
captures it holds a median of 3-4 frames > 20 ms (2-6), which is under 2x of any useful bound (section 6). The headline
is therefore widened to 30 s, which holds the hitch and the whole train. The 15 s numbers are still computed and
reported beside the headline, deciding nothing. Every number that depends on the window is re-derived in sections 3.3,
5.3, 6, 6.1 and 9, each labelled MEASURED / DESK / GUESS. Arms, grouping rule, lever checks, exe, flags other than the
frame count, verdict structure and exit codes are unchanged.

**Revision 3 (2026-11-26, review fixes; NOTES "Revision 3").** Unchanged: arms, load count, windows, H1-H3 bounds,
exit codes, every NO VERDICT rule except that a held-frame load time can now be unmeasurable (5.4). Added: a PROBE of
the prime load's real log before any scored load (8; a failed probe ends the attempt with no verdict); FAIL
`no_measure_lines` (4.3); a malformed bench VERDICT line, a non-finite or unconvertible number and any scorer exception
are named FAILs (4.3); capture markers must be in the order loading end < started < finished (4.3); the driver's
per-load check is the scorer's own (8). Wording: the converted hitch and train times are estimates at their low end (6).

## 0. Limits first
- One box (Ryzen 9800X3D, Wine + DXVK, headless Xvfb 1280x720), no Present (`-perf_nopresent`), no input
  (`-perf_noinput`), uncapped frame rate. Nothing about the 2012 reference machine, native Windows, vsync or 4 cores.
- One save (cordon1), one load path (save load from the command line), player standing still. Same-level load, level
  change and new game are NOT tested (section 11). The patch's two doubts are therefore only partly covered.
- The lever's purpose is a real-time effect (stock takes the bad order only when a loading frame is long in wall time),
  so the order stock takes is luck: the OFF arm's group sizes are not under our control (section 3.2).
- This experiment does NOT prove simulation equivalence (Rule One, section 10). The deterministic harness always takes
  the in-loading order, so it can show "off = stock" but cannot exercise the change.
- cordon1 runs a scripted full-game save about 4 s of time_global after the actor's first update (DESK:
  itms_manager.script:245-246 -> new_game_equippment, 698-712, `exec_console_cmd("save ...")`). In all 38 past Cordon
  logs it lands just after the loading screen (DESK: "Saving objects" after "Handled Necessary Textures Destruction" in
  every bench log). It is in both arms, it is a property of this test save (a player's own save has the info
  `start_equippment_handled` and does not do it), and its cost has never been captured (every past capture started
  600+ frames later). Its frame is excluded from the headline numbers by a fixed rule (section 5.5) and reported apart.
- No second save falls inside 30 s (Revision 2 check, DESK): itms_manager.script:245-246 makes the scripted save ONCE
  (4 s after the actor's first update, then the info is given, 698-712); game_autosave.script has its whole body
  commented out (stock copy plan-facts/stock_scripts/_unpacked/scripts/game_autosave.script:21-65);
  game_autosave_new.script first checks 60 s after the actor's first update and saves only when "autosave_timer_new"
  minutes (>= 1) have passed (lines 7-8, 17-27). MEASURED: every one of the 22 past Cordon logs has exactly one
  "is successfully saved" line, including cpl1's 60,000-frame capture. If a second save did happen, the rule in 5.5
  excludes every frame its zones name, not only the first.
- Warm18.exe is a profiler build (all probes, about 0.7 ms per frame of probe cost, as the SimTick builds: sf1 NOTES 6).
  Absolute frame times are a little high; both arms carry the same cost.
- The load time is measured on the engine's own clock from device start-up (section 5.4), not from process start: it
  misses Wine start-up and file-system init, which the lever cannot touch.
- Fixtures: the scorer has only been run on made data and on real logs that predate the alife_pass / alife_opu lines.
  The first real Warm18 log is the first test of those parsers on real bytes. Revision 3: that first log is the PRIME
  load's, and the driver PROBES it (section 8) before any scored load: if a line the scorer needs is missing or not
  parsed, the series stops with no verdict and nothing consumed. The probe cannot catch a format drift that shows only
  in later loads, nor a line that parses but means something else.
- Revision 3: a load with NO alife_pass line or NO alife_opu line at all is a FAIL `no_measure_lines`, not OFF-OTHER
  (a build that prints them differently must not quietly turn stock loads into "other"). Both are guaranteed in every
  Warm18 load by the code (DESK, perf/alife-warmup d77d70b5c): alife_pass on the simulator's first scheduled pass
  (alife_update_manager.cpp:174-191: the 10 s log window starts AT that pass, so it always prints); alife_opu on the
  actor's reinit (stock bind_stalker.script:200 `set_objects_per_update(65534)` -> alife_simulator_script.cpp:504-511,
  not windowed). The alife_opu guarantee rests on a stock SCRIPT (no loose override of bind_stalker.script in
  run/gamedata/scripts; a mod archive was not checked): if a mod removed that call, every load would FAIL by name.

## 1. Question
On cordon1 in real time, with the lever ON (`-alife_warmup`) against lever OFF (stock), in ABBA order:
(a) does every ON load show that the one full A-Life pass ran behind the loading screen;
(b) in the first 30 s of play after the loading screen (HEADLINE window, widened in Revision 2 from the supervisor's
    ~15 s so that it holds stock's hitch and its whole slow train; the 15 s numbers are reported beside it), are the
    worst frame and the number of slow frames of ON loads much smaller than those of the stock loads that took the
    IN-PLAY order;
(c) what does it cost in load time;
(d) (secondary) do ON loads look like the stock loads that took the good IN-LOADING order.

## 2. What runs (flags copied from sf1 unless marked CHANGED)
Exe `run/bin/Warm18.exe` sha256 `15828a7a6cadd38e42cc376869f581166884c04f1eb236baf79054a74332ae9d` (CI build of
perf/alife-warmup d77d70b5c, profiler configuration; hash checked today from the file). The SAME exe in both arms.
Save cordon1 (.scop 6dea752b..., .scoc a9f9a8a9...; full hashes pinned in wu1_run.sh, same as sf1).
Settings: run/appdata/user.ltx as is, checked read-only before every load: r__framelimit 0, no sim_tick, no alife_warmup.

| switch / setting | value | source | what it produces / why |
|---|---|---|---|
| bench | tools/s2_bench.sh 072af8ed | sf1_run.sh:29 (COPIED) | out/<tag>/: conditions.txt (VERDICT clean/CONFOUNDED), xray.log, perf_*.csv |
| BENCH_FIXED | 0 (real time, no `-perf_fixed_dt`) | s2_bench.sh:11, sf1 (COPIED) | dwTimeGlobal = wall clock (device.cpp:1110-1113) |
| `-perf_capture W,N,tag` | **W = 0 (CHANGED)**, **N = 20000 (Revision 2, was 10000; derived 5.3)** | s2_bench.sh:56 | capture from the first frame after the loading screen (5.1) |
| `-perf_quit -nointro -dbg -start server(cordon1/single/alife/load) client(localhost)` | | s2_bench.sh:55-56 | no key prompt with -nointro (GamePersistent.cpp:504-509, 590-615) |
| `-perf_noinput -perf_nopresent` | | sf1 (COPIED) | the bench's space-key nudges (s2_bench.sh:66-68) are not read (Xr_input.cpp:735-736) |
| `-perf_slow` | **20 (CHANGED from sf1's 6/9)** | profiler_agg.cpp:79-80 | perf_slow_<tag>.csv: per frame > 20 ms, the 14 largest self times; used only to find the save frame (5.5) |
| `-alife_warmup` | ON loads only | B2b patch notes, alife_update_manager.cpp:82-90 | the lever |
| BENCH_EXCLUSIVE=1, BENCH_NOCLAIM=1, BENCH_LOWEND=0, BENCH_TIMEOUT=900 | | sf1 (COPIED) | |
| BENCH_DISPLAY | :141, refused if /tmp/.X11-unix/X141 or /tmp/.X141-lock exists | box rule (COPIED) | |
| frame limiter | none (uncapped, decision 5.3) | | check: site "Eco Render" 0 calls |

## 3. Design
### 3.1 Loads and order
One priming load (OFF, not scored: first engine start after idle warms the disk cache), then 32 scored loads, 16 ON and
16 OFF, in blocks ON OFF OFF ON (x8). One order file `wu1_order.txt` is read by both the driver and the scorer.
Tags wu1_01_on ... wu1_32_on/off, all lowercase. No load is ever run twice (no retry, no re-run).

### 3.2 Why 16 OFF loads (derived)
The headline comparison needs at least K = 3 valid OFF loads that took the IN-PLAY order (MIN_OFFP, section 6). Past
real-time-loading Cordon bench loads (DESK, my count from out/*/xray.log with alife_slow lines): the brief's 8 of 11, and
over all 22 such loads 15 in-play (cp1, cp3, cpl1, lc1-3, uc1, r1a_cor_1-3, r1a_cor_k0_1, r1_cor_f2-3, r1_cor_k1,
r1_cor_k1_rr) and 7 in-loading (cp2, cp4, r1a_cor_k0_2-3, r1_cor_f1, r1_cor_k2-3). p(in-play) about 0.68; with a flat
prior the 5th percentile is 0.50. Warm18 adds log lines during loading, which could shift the wall-clock race (GUESS).
Probability of fewer than 3 in-play loads among n OFF loads (binomial):

| n OFF | p = 0.727 (8/11) | p = 0.68 (15/22) | p = 0.50 | p = 0.40 | p = 0.30 |
|---|---|---|---|---|---|
| 8 | 0.007 | 0.015 | 0.145 | 0.315 | 0.552 |
| 12 | 0.0001 | 0.0004 | 0.019 | 0.083 | 0.253 |
| **16** | < 0.0001 | < 0.0001 | **0.002** | 0.018 | 0.099 |

16 keeps the risk of NO VERDICT [few_inplay] at or below 0.2 % even at the pessimistic p = 0.5. It also makes at least
2 OFF-IN-LOADING loads likely for the secondary comparison: P(fewer than 2 of 16) = 0.019 at q = 7/22. Equal ON count:
16 lever checks (a lever that fails 1 load in 10 is caught with probability 0.81).

### 3.3 Box time (Revision 2: re-derived for 20,000 frames)
Per load: Wine start about 9 s (MEASURED cp1: bench "start 09:57:04", engine "Game started 09:57:13"), engine clock to the
end of loading 19-23 s (MEASURED: "Time continual is:" stamps right after intro_start in 22 past Cordon logs, 18.97-23.02 s),
capture 20,000 frames at the expected real-time 4.1-5.0 ms = 82-100 s (5.3), dump, quit and the bench's 8 s wait about
10-15 s: bench about 120-147 s. Driver overhead per load: settle (1-min load < 2) about 20-60 s after an engine run, display
grace 0-20 s. Per load 140-227 s = 2.3-3.8 min. Estimate: 33 x 2.3-3.8 min = **77-125 min** (if every frame took 6 ms,
the low-end level, the capture is 120 s and the series about 136 min). Under the 3 h slice (the driver stops with
< 15 min left, so 165 min are usable: 1.3-2.1x). The number of loads is unchanged (33), so the probability table of 3.2
stands as it is.

## 4. Per-load checks (all fixed now)
### 4.1 ON loads: the full pass ran behind the loading screen (a failure is a FAIL of the lever, never dropped)
From xray.log (CR stripped):
- L1 `lever_count`: exactly one line `* [alife_warmup] full pass: N of M objects, frame F, precache_left P, X ms`.
- L2 `lever_after_intro`: that line comes BEFORE `intro_start game_loaded` and before
  `* [x-ray]: Handled Necessary Textures Destruction` in the log (line order: the log has no time stamps; desk read).
- L3 `lever_precache`: P >= 3. The hold keeps the countdown at 3 while the pass is pending (device.cpp:165-169,
  131-153) and game_loaded acts at <= 2 (GamePersistent.cpp:564), so a pass that ran behind the screen sees P >= 3.
  This is stronger than P > 0.
- L4 `lever_cap`: no line `! [alife_warmup] hold cap hit after K frames, pass not run`.
- L5 `lever_partial`: N >= 0.9 M. What N < M means, from the code: the pass walks the map from the cursor and stops only
  when it meets an object already done in THIS pass (each pass has a new cycle number, safe_map_iterator_inline.h:
  128-146; alife_schedule_registry.h:36-47); the limit is u32(-1). So N = M except for objects added BEHIND the cursor
  during the pass (smart_terrain respawns spawn squads: N < M) or removed after their update (N > M); M is counted
  after the pass (alife_update_manager.cpp:151-152). A count-limited pass would show N = 3 or 20 (the only other
  limits), far below 0.9 M.
- Reported, not a check: `* [alife_warmup] precache held K frames` (held frames are new versus stock: section 10), X ms.
A pass cannot come from a stock in-loading order by chance: stock's wide pass logs `alife_pass ... limit 65534` and never
an `[alife_warmup]` line; L1 counts only the lever's own line.

### 4.2 OFF loads: which order stock took (rule fixed now; the two stock groups are never pooled)
Lines (both arms, profiler build): `* alife_pass frame F time_global T precache P n N of M limit L` (every scheduled pass
in the first 10 s of time_global after the simulator's first pass), `* alife_opu frame F time_global T precache P value V`
(every script set_objects_per_update), `* alife_slow frame F id I name [section] X ms` (any object update >= 1 ms),
`intro_start game_loaded`. A pass is WIDE if n > 20 (stock limits are 3 and 20, and n <= limit).
- OFF-IN-LOADING (L): some wide pass has precache > 0 AND comes before intro_start in the log, AND the alife_slow lines
  agree: some single frame has >= 50 alife_slow lines before intro_start.
- OFF-IN-PLAY (P): no wide pass at all; a widening line (value 65534) and a later narrowing line (value < 65534) exist;
  the pass lines reach at least the narrowing's time_global (so no wide pass can have been missed after the 10 s
  window); AND the alife_slow lines agree: no frame anywhere has >= 50 alife_slow lines.
- OFF-OTHER: anything else (no measurement lines; a wide pass only after loading ended; window too short; the two kinds
  of line disagree). Reported with its reason, never used in a verdict, never pooled.
The order stock took changes counters and log lines by construction: an L load logs ~130 alife_slow lines in one loading
frame and wide alife_pass lines, a P load logs neither but has a later hitch and a train of alife_slow lines in play.
Counters and log lines may therefore differ between the groups; that is expected, not a fault.
The rule was run on the real logs (legacy alife_slow part only, the pass lines did not exist yet): cp2 and cp4 L, cp1,
cp3, uc1, lc1-3, cpl1 P (self-test cases real_parse_*).

### 4.3 Run validity (VOID: the load is not counted, no re-run; FAIL where marked)
- `missing` / `empty` / `unparsable` file (perf_frames, perf_sites, perf_slow, conditions.txt, xray.log; a malformed
  alife_pass / alife_opu / alife_slow / [alife_warmup] line; no command line / intro_start / textures-handled line):
  **FAIL, by name** (supervisor rule).
- `arm_mismatch` (**FAIL**): the logged command line must name Warm18.exe, cordon1, `-perf_capture 0,20000,<tag>`, no
  `-perf_fixed_dt` / `-det_mode` / `-sim_tick`, and `-alife_warmup` present exactly on ON loads.
- `off_not_stock` (**FAIL**): any `[alife_warmup]` line in an OFF load.
- VOID: `not_clean` (bench VERDICT not clean), `framecount` (frames rows = sites header = 20000), `capture_lines`
  (xrprof started/finished lines with 20000 frames, started after loading), `stock_loop` ("Process seqFrame" main calls =
  frames, "Sim tick" 0 calls), `frame_limiter` ("Eco Render" 0 calls), `threshold_header` (perf_slow 20.000),
  `window_short` (capture shorter than the 30 s headline window; Revision 2, was 15 s), `save_unlocated` (save calls in
  the capture but no slow row names them), `slow_cap` (Revision 2: perf_slow has more than 8386 rows, so the profiler's
  600-frame slow dump may be full and a save frame may be missing from it; 5.3).
- Revision 3, **FAIL**: `no_measure_lines` (no alife_pass line or no alife_opu line at all in the log; section 0);
  `unparsable` also covers a conditions.txt whose VERDICT line is not exactly one of the bench's three forms
  (s2_bench.sh:72-74; or more than one VERDICT line), any inf / nan in a frame time, site time, slow threshold or slow
  row, and a log field that matches its pattern but is not a finite number (e.g. `1..2 ms`); `scorer_error` (any other
  exception while reading a load: named, and the report is still written). Revision 3, VOID `capture_lines` also when
  the markers are out of order: required "Handled Necessary Textures Destruction" < capture started < capture finished
  (log line order), each with the registered tag and frame count.

## 5. Measures in player units
### 5.1 The capture starts at the end of loading (code)
`perf_autocapture()` runs at the top of every on_idle (device.cpp:682). With warm-up 0 it starts the capture on the first
frame where `g_pGameLevel && g_bLoaded && !Device.dwPrecacheFrame && !Device.Paused()` (device.cpp:422-434), i.e. the
first frame after End() counted the precache to 0 (device.cpp:165-171), which is the frame after
"Handled Necessary Textures Destruction" and 2-3 frames after intro_start (acts at precache <= 2). `xrprof::start` is
called after this frame's PROF_FRAME (device.cpp:675), so capture frame 0 is that whole frame. If the game were paused at
that moment the capture would start later; nothing in the -nointro path pauses it (not verified at run time).
### 5.2 Frames file
`perf_frames_<tag>.csv`: line 1 `frame,ms`, then one line `i,ms` per captured frame, i from 0, ms = wall time from one
PROF_FRAME to the next (rdtsc scaled by QPC at dump, profiler_agg.cpp:147-155, 181-187). Time since the end of loading
of frame i = sum of ms[0..i-1]. The 30 s headline window = frames starting before 30,000 ms; the reported-only 15 s
window = frames starting before 15,000 ms.
### 5.3 Capture length, the slow-dump cap and the frame limiter (decisions; Revision 2)
- Cost per frame (DESK, profiler_agg.cpp on perf/alife-warmup d77d70b5c): one float per frame in a vector reserved at
  start (`g_frame_ms.reserve(frames)`, lines 20, 94-95, 187): 20,000 frames = 80 KB. The frames CSV is about 13 bytes a
  frame (MEASURED: 157 KB for 12,000 frames, out/s2_off_1): about 260 KB. The per-frame work (`frame()`, 181-220: one
  push_back and, with -perf_slow, one walk of all sites) does not depend on N; the dump writes N short lines once,
  after the capture. No cap on recorded frame times exists. A load folder is about 1 MB (MEASURED 780-812 KB for
  3,000-12,000 frames), so the series writes about 35 MB.
- N = 20,000 (Revision 2, was 10,000): the 30 s headline window must be covered with 2x margin at the fastest frames
  seen. Fastest Cordon frame in any capture 3.21 ms (MEASURED, out/r1a_cor_1; 3.22 in the r1a_cor_k0 and r1_cor_k
  series). Those are fixed-step captures; on bench1 the real-time stock captures are about 10 % faster on the mean
  (MEASURED 5.85-5.98 ms against 6.60-6.63 ms fixed-step) and 3-4 % faster on the minimum (4.92-5.00 against
  5.11-5.18 ms), so the real-time Cordon minimum is about 3.1 ms (DESK). Floor 3.0 ms: 20,000 x 3.0 ms = 60 s = 2.0x the
  30 s window (4x the reported 15 s). At the expected real-time mean 4.1-5.0 ms (fixed-step Cordon means 4.5-5.05 ms
  less up to 10 %) it is 82-100 s, 2.7-3.3x. A capture shorter than 30 s is VOID `window_short` (kept, now at 30 s).
- The -perf_slow dump cap (profiler_agg.cpp:42-43, 190): a slow frame's rows are kept only while rows + 14 <= 8400,
  so at least 600 slow frames always fit. Expected frames > 20 ms in a whole 20,000-frame capture: MEASURED 16-19
  (in-play) and 0-2 (in-loading) over the 3,000-frame fixed-step captures, which span 99 s of time_global, about the
  82-100 s of time a 20,000-frame real-time capture spans; plus the save frame; plus the first seconds after loading
  that no past capture saw (GUESS <= 20): under about 45 in either arm, 13x below 600. Bound by arithmetic: 600 frames
  > 20 ms take more than 12 s, so the cap cannot be reached before 12 s after loading, and the cordon1 save (in the
  first 1-2 s, stamps in 5.4) can never lose its rows. It CAN in principle be reached within 30 s (30 s holds up to
  1,500 frames > 20 ms), so a registered rule handles it: VOID `slow_cap` if perf_slow has more than 8386 rows (600 x
  14 - 14, the largest count at which the profiler still accepts a frame). The frame numbers themselves come from the
  frames file and are never capped.
- Uncapped, as sf1 and every real-time stock-loop run on this harness: (1) user.ltx has r__framelimit 0 and the driver
  does not edit it; a cap would need a new switch never used in a prereg here; (2) the A-Life pass runs on the game
  thread and the main thread waits for it (secondary_tasks.wait), so a pass of 22-52 ms makes a frame of about that
  length capped or not; a 60 fps cap only floors normal frames at 16.7 ms, which leaves the > 20 ms and > 33.3 ms counts
  and the worst frame unchanged in what they measure; (3) uncapped keeps the frame-limiter check ("Eco Render" 0 calls)
  meaningful. Cost: frame counts per second are not those of a 60 fps player; only frames above 20 ms are counted.
### 5.4 Load time (engine clock)
xray.log has no time stamps and is written through a buffered fwrite (xrCore/FS_internal.h CFileWriter::w; flushes only
on request, log.cpp:244-257), so a driver watching the file cannot time intro_start. Instead: time_global (dwTimeGlobal =
TimerGlobal ms since device init, Device_Initialize.cpp:40; real time, device.cpp:1110-1113) at the first frame after the
loading screen, from the measurement lines:
F0 = F + P of the LAST alife_pass/alife_opu line with precache P > 0 (the countdown loses 1 per frame: F0 is the first
frame with precache 0; +-1 frame if the A-Life thread read P after End() in that frame).
**Revision 3, held frames (ON loads with `precache held K frames`, K > 0).** Code (device.cpp:131-170 on
perf/alife-warmup): End() counts the precache down by one each frame, except at 3 while the pass is pending (one held
frame per End(), up to 300); the End() that finally counts 3 -> 2 prints the held line first. The held line has no
frame number, and a line read at P = 3 may lie anywhere in the hold (0..K frames before its end; the A-Life thread's
claim and the main thread's End() race by a frame). So: a line with P > 3 gives F0 = F + P + K, a line with P <= 2
gives F0 = F + P, a line with P = 3 is NOT used when K > 0 (K = the held line's K, plus a cap line's K). If no line is
left, that load's load time is unmeasurable (named), which counts toward NO VERDICT `loadtime` (7.2). Without this rule
an ON load whose last loading line is at P = 3 early in the hold put F0 up to K frames early and understated the price
(self-test l_held_ok: K = 12, the old rule gives 60 ms less). Then the first such line with
P = 0 and frame F1 >= F0 gives load time = T(F1) - sum(ms[0 .. F1-F0-1]). It must lie between the T of the two lines,
else the load's load time is "unmeasurable" (named). Error: about one frame (5-30 ms). It misses process start ->
device init (Wine start, FS init: identical in both arms, the lever is not armed before the simulator exists).
Reference (MEASURED, a coarser stamp on the same clock): "Time continual is:" right after intro_start in past Cordon
logs: 18.97-19.86 s for the r1/r1a series, in-loading and in-play loads alike, 19.8-21.0 s cp1-4, 22.0-23.0 lowend.
### 5.5 The save frame (excluded by a fixed rule)
Save frames = capture frames whose perf_slow rows name `CALifeStorageManager::save` or
`CALifeStorageManager::prepare_objects_for_save` (the PROF_EVENT() zones of the save path, alife_storage_manager.cpp:48,
237, 247; the site name is __FUNCTION__, profiler.h:124-126). Excluded from worst / n20 / n33 / span; reported apart as
"worst_incl_save". If perf_sites shows calls of those zones but no slow row names them: VOID `save_unlocated`.
CScriptBinder::save is NOT used: objects going offline also call it (it could hide a train frame).
### 5.6 Headline numbers per load (30 s window, save frame excluded; Revision 2, was 15 s)
worst frame (ms); n20 = frames > 20 ms; n33 = frames > 33.3 ms; train span = from the start of the first to the end of
the last frame > 20 ms (s); load time (s, engine clock). These are taken in the **30 s window** (Revision 2).
Secondary, reported only: the same in the supervisor's 15 s window; worst including the save frame; for ON the pass's
X ms and held frames.

## 6. Constants, each with its derivation (all derived for wu1; none copied from another arm's bound)
| constant | value | derivation |
|---|---|---|
| WINDOW (headline) | **30 s** (Revision 2, was 15 s) | DESK from MEASURED, re-derived for Revision 2 from the 14 in-play fixed-step captures with frame files (cp1, cp3, lc1-3, uc1, r1a_cor_1-3, r1a_cor_k0_1, r1_cor_f2-3, r1_cor_k1, r1_cor_k1_rr). Hitch = capture frame k_h 99-122 (MEASURED); its time after loading C = W + 33 ms x k_h (time_global moves 33 ms a fixed-step frame, device.cpp:1063-1074; it is wall time in the 600 real-time warm-up frames). W is bounded ABOVE by the mcm_log "Time continual is:" wall stamps (mcm_log.script:201-207, dwTimeContinual = TimerMM, device.cpp:1050) from before intro_start to after the capture start (a real bound: the stamps span more than the warm-up). Its low end, 600 x the capture's mean frame, is an ESTIMATE, not a bound (Revision 3, review: the mean frame of the later capture does not bound the 600 warm-up frames, which could be faster). Result: **hitch at most 7.8 s** after loading (bound), estimated from 6.5 s (the earlier 7.4-8.6 s was an estimate of W; the stamps put it lower); the train's passes follow 14 frames (462 ms) apart from hitch + 2.8 s to hitch + 14.8 s; the last frame > 20 ms inside 30 s lies at most at 22.1 s, estimated from 20.8 s (12 of 14), with a non-train straggler at most at 28.3 s, estimated from 25.6 s, in 2. So 30 s holds the hitch (3.8x on the bound) and the whole train (1.36x on the bound of its end, UNDER 2x: flagged in 6.1). Only the upper ends decide whether 30 s holds them; the lower ends (estimates) decide nothing registered. The supervisor's ~15 s is kept as a reported-only window |
| SLOW1 / SLOW2 | 20 / 33.3 ms | supervisor; 33.3 ms = one frame at 30 fps |
| LEGACY_WIDE | 50 lines in one frame | 20-per-pass can log at most 20 lines per pass; in-play logs peak at 16-19, wide passes log 128-185 (DESK, 22 logs). 50 is 2.6x above 19 and 2.6x below 128 |
| WIDE | n > 20 | stock limits are 3 (alife.ltx) and 20 (script reset); n <= limit (CUpdatePredicate) |
| P_MIN | 3 | hold point (device.cpp:168); game_loaded acts at <= 2 |
| N_M_MIN | 0.9 | 4.1 L5: churn of a few squads during one pass versus a count-limited pass at 3 or 20 of ~thousands |
| MIN_ON | 8 of 16 | half the registered ON loads; below it the ON median rests on a minority of what was planned |
| MIN_OFFP | 3 | smallest group with a median that is not one load and with a stray rule that means something; 3.2 sizes the run for it |
| STOCK_HITCH_MIN | 150 ms (unchanged) | half of the smallest MEASURED first-update cost of zat_b106 (287 ms over 22 logs: 270-352 ms); MEASURED worst frame of the 14 in-play captures inside the 30 s window 293-361 ms (median 323). Below it the 30 s window did not catch stock's hitch: nothing to remove, NO VERDICT. The longer window does not change what the hitch costs, so the value stays |
| STOCK_TRAIN_MIN | **7 frames > 20 ms** (Revision 2, was 3) | DESK from MEASURED: frames > 20 ms in the 30 s window of the 14 in-play captures 16-19, median 17 (with W at either bound). Robust to real-time frames being faster: counted above 22 ms 15-18, above 25 ms 11-18. 7 is under half of the smallest (16/7 = 2.3x, median 2.4x) and = N20_DROP, so H2 is decidable whenever this guard passes. For comparison the 15 s window holds 2-6 (median 3-4): this is why the headline was widened |
| WORST_RATIO (H1) | 0.5 | "removes the hitch": ON's typical worst at most half of stock in-play's (~310 ms -> 155 ms); an ON worst made of ordinary first-seconds frames (GUESS 20-80 ms) clears it, a hitch of the same kind (> 155 ms) does not |
| WORST_STRAY (H1) | 1 ON load may reach the in-play median | one stray hitch unrelated to A-Life in 16 loads (Wine/DXVK stall) should not decide; two may not be chance |
| N20_DROP (H2) | **7** (Revision 2, was 3) | DESK from MEASURED: in-play 16-19 frames > 20 ms in 30 s, in-loading 0-2 (MEASURED, the 7 in-loading captures, standing in for ON; GUESS for the first 3-4 s after loading that no past capture saw: frames there are in both arms and cancel in a DIFFERENCE). Expected drop 14-19, median about 17; 7 is half of the low end (14/7 = 2.0x, 17/7 = 2.4x). A DIFFERENCE, not a ratio, so slow frames unrelated to A-Life that both arms share (shader compile, GC) cancel |
| PRICE_MAX (H3) | 2.0 s | a full pass costs 0.5-0.75 s (DESK: alife_slow sum of the wide loading pass 506-573 ms over objects >= 1 ms in 7 in-loading logs, plus objects under 1 ms); 2.0 = 2x the 1.0 s upper end: more than the moved work itself means something else (held frames, a second pass) was added. Load-to-load spread in one series about 0.2-0.4 s (MEASURED stamps, 5.4) |
| stock_orders_alike (NV) | OFF-IN-LOADING median worst > 0.5 x OFF-IN-PLAY median worst, or n20 differs by < 7 (Revision 2: the H2 bound, was 3) | the same bounds as H1/H2 applied to stock's own two orders in the 30 s window: if stock's good order cannot be told from its bad order there, the window cannot show the lever. MEASURED in the 30 s window: in-loading worst 13-23 ms, n20 0-2; in-play worst 293-361 ms, n20 16-19 (difference 14-19). Checked only with >= 2 OFF-IN-LOADING loads |
| LOADTIME_MIN | 8 ON and 3 OFF-IN-PLAY measurable | as MIN_ON / MIN_OFFP |
| SAME_* (secondary) | worst <= 1.25 x OFF-IN-LOADING + 10 ms; n20 <= +2; load within 1.0 s | GUESS: no real-time post-load Cordon data exists; reported only, decides nothing. Flagged |
| MAX_CONFOUNDED (driver) | stop at the 3rd CONFOUNDED load | GUESS: one or two transient heavy processes are tolerable; three means the box is not quiet |
| N capture frames | **20,000** (Revision 2, was 10,000) | 5.3: 30 s at the 3.0 ms floor = 60 s = 2.0x; 82-100 s at the expected 4.1-5.0 ms |
| SLOW_CAP_ROWS (new, VOID slow_cap) | 8386 rows | 5.3: 600 x 14 - 14, the largest row count at which profiler_agg.cpp:190 still accepts a slow frame; more rows = the slow dump may be full |
| HOLD_CAP | 300 | COPIED from the patch (device.cpp:128), reported against only |

### 6.1 Margin table (Revision 2, redone in full for the 30 s headline; expected vs bound; UNDER 2x flagged; COPIED = not derived for wu1, flagged)
| item | expected | bound | margin | label / status |
|---|---|---|---|---|
| valid OFF-IN-PLAY loads | ~11 of 16 (p 0.68) | >= 3 | 3.7x | DESK (3.2); unchanged |
| stock hitch time after loading | at most 7.8 s (bound); from about 6.5 s (ESTIMATE, Revision 3) | inside 30 s | 3.8x on the bound (4.6x on the estimate) | DESK from MEASURED (6, WINDOW); the low end is an estimate, the margin rests on the bound |
| stock train end (last frame > 20 ms) | at most 22.1 s (bound), from about 20.8 s (ESTIMATE) (12 of 14); stragglers at most 28.3 s (2 of 14) | inside 30 s | 1.36x on the bound (1.44x on the estimate; 1.06x for the straggler) | DESK from MEASURED; **UNDER 2x**: a train ending late loses frames from n20; backed by no_stock_train (7 of 16-19) |
| median OFF-IN-PLAY worst (30 s) | 293-361 ms, median 323 (MEASURED captures) | >= 150 ms | 1.95-2.4x | MEASURED; **UNDER 2x at the low end** (293/150) |
| median OFF-IN-PLAY n20 (30 s) | 16-19, median 17 | >= 7 (STOCK_TRAIN_MIN) | 2.3-2.7x | DESK from MEASURED; at a 25 ms cut 11-18 (1.6x): flagged if real-time frames run much faster |
| H1 ON worst vs 0.5 x in-play | 13-23 ms after 3 s (MEASURED in-loading captures) + first 3-4 s (GUESS 20-80 ms) vs ~160 ms | <= 0.5 x | 2.0-7x (12x on the MEASURED part) | **UNDER 2x at the high GUESS (80 ms)**; unchanged by the window except that 30 s also holds 15 s more of ordinary play (MEASURED quiet: in-loading worst 13-23 ms) |
| H1 strays | 0 | <= 1 | - | derived; unchanged |
| H2 drop in n20 (30 s) | 14-19, median ~17 | >= 7 (N20_DROP) | 2.0-2.7x | DESK from MEASURED |
| H3 price | -0.3 to +0.8 s | <= 2.0 s | 2.5x | derived; window-independent; unchanged |
| stock_orders_alike: OFF-IN-LOADING worst (30 s) | 13-23 ms MEASURED + first seconds GUESS 20-80 ms | <= 0.5 x in-play (~160 ms) | 2.0-7x | **UNDER 2x at the high GUESS** |
| stock_orders_alike: n20 difference (30 s) | 14-19 (in-play 16-19 minus in-loading 0-2) | >= 7 | 2.0-2.7x | DESK from MEASURED |
| L3 precache_left | ~25-35 (DESK: wide pass at frame 32 of 60 in L logs) | >= 3 | 8x | unchanged |
| L5 N/M | 1.0 (DESK) | >= 0.9 | 1.1x on the ratio, 40x against a count-limited pass | unchanged (the bound is about failure modes, not noise) |
| LEGACY_WIDE | 16-19 (P) / 128-185 (L) lines | 50 | 2.6x both sides | unchanged, derived from MEASURED |
| held frames | 0 (DESK) | cap 300 | - | COPIED (patch), reported only |
| capture covers 30 s (headline) | 60 s at the 3.0 ms floor; 82-100 s at 4.1-5.0 ms | 30 s | 2.0x at the floor, 2.7-3.3x expected | DESK from MEASURED frame times; VOID window_short below 30 s |
| capture covers 15 s (reported only) | 60-100 s | 15 s | 4-6.7x | derived |
| slow-dump cap | < 45 frames > 20 ms per capture (16-19 / 0-2 MEASURED + save + GUESS <= 20 for the first seconds) | 600 frames (8386 rows) | 13x; cannot be reached before 12 s | derived; VOID slow_cap if reached (new rule, 5.3) |
| second save inside 30 s | none (scripts, 0.1) | excluded by the zone rule if it happens | - | DESK; MEASURED 1 save per log in 22 logs |
| load time measurable | in-play lines exist if the precache phase < 10 s (GUESS 1-6 s) | 10 s window | 1.7-10x | **UNDER 2x at the slow end** (NO VERDICT loadtime); unchanged |
| load duration (bench) | 120-147 s | BENCH_TIMEOUT 900 s | 6.1-7.5x | COPIED (bench default) |
| series duration | 77-125 min (136 min at 6 ms frames) | 3 h slice, stop at < 15 min left (165 min usable) | 1.3-2.1x | **UNDER 2x at the slow end**; box rule COPIED; a stopped series is FAIL [missing] and needs a new registration |
| settle | load < 2, no other >= 100 % process | 5 min cap | - | COPIED (s4c_series.sh) |
| CONFOUNDED loads | 0-1 | stop at 3rd | - | GUESS |
| SAME_* secondary | ON = OFF-IN-LOADING | 1.25x + 10 ms, +2, 1.0 s | - | GUESS, reported only; now in the 30 s window, still deciding nothing |

## 7. Verdict rules (implemented exactly in wu1_score.py)
1. **FAIL** (exit 1) if any scored load has: a missing/empty/unparsable file (named), arm_mismatch, off_not_stock, or any
   ON lever check L1-L5 failing; Revision 3: or no_measure_lines, or scorer_error (4.3). A failing ON load is never dropped.
2. Else **NO VERDICT** (exit 3) if any of: `few_on` (< 8 valid ON), `few_inplay` (< 3 valid OFF-IN-PLAY),
   `no_stock_hitch` (median OFF-IN-PLAY worst < 150 ms), `no_stock_train` (median OFF-IN-PLAY n20 < 7),
   `stock_orders_alike` (6), `loadtime` (load time measurable in < 8 ON or < 3 OFF-IN-PLAY; Revision 3: an ON load with
   held frames and no measurement line outside the hold is not measurable, 5.4). All that apply are listed.
3. Else **PASS** (exit 0) if all of (worst and n20 in the 30 s headline window):
   - H1: median ON worst <= 0.5 x median OFF-IN-PLAY worst, and at most 1 ON load has worst >= median OFF-IN-PLAY worst;
   - H2: median OFF-IN-PLAY n20 - median ON n20 >= 7;
   - H3: median ON load time - median OFF-IN-PLAY load time <= 2.0 s.
   Otherwise **FAIL** naming H1/H2/H3.
4. Secondary, reported only: "SAME AS GOOD ORDER" ON versus OFF-IN-LOADING (SAME / DIFFERENT / NO VERDICT with < 2).
5. n33, train span, the 15 s window (Revision 2: reported only, it was the headline in the first draft), worst
   including the save, X ms, held frames, OFF-OTHER: reported only.
The headline comparison is ON versus OFF-IN-PLAY. ON versus OFF-IN-LOADING is the "is ON the same as stock's own good
order" comparison. The groups are never pooled; a self-test mutant that pools them is caught (section 8).

## 8. Scorer, self-test, driver
`wu1_score.py` (stdlib, `python3 -I`), `wu1_selftest.sh`, `wu1_run.sh` (bash -n only), `wu1_order.txt`; hashes in
NOTES.md and in the final message (they are pinned into the driver at registration; until then the driver refuses).
Self-test 2026-11-26 Revision 3: SEE NOTES "Revision 3" for the current line and folder (it adds the review fixes,
each with a case that fails without it: VERDICT form, inf/nan and `1..2` numbers, capture order, no_measure_lines,
held-frame load time (K = 12), the probe and check modes, slow-dump cutoff at 8386 (VALID) / 8387 and 8400 (VOID, the
largest the profiler can write) with constant mutants 8400 and 8385, and a find-error check of the untouched test).
Self-test 2026-11-26 Revision 2: `WU1 SELFTEST PASS 118/118 (11 real cases, 61 made cases, 36 hook mutants caught, 8 constant mutants caught, 1 hook-coverage check, 1 untouched check)` (folder selftest-runs/20261126-170026-3025479). (First draft: 101/101.) Revision 2 added: made
in-play loads shaped from the real captures (hitch at 7 s; 16 of 27 passes over 20 ms, from 9.8 s to 21.1 s: 17 frames
> 20 ms in 30 s, 3 in 15 s), FR 7000 frames per made load (~35 s), edge cases on both sides of every changed bound
(n20 6/7 for STOCK_TRAIN_MIN, drop 6/7 for H2 and for stock_orders_alike, a hitch at 20 s (in) and 31 s (out) for
the window, capture 21 s for window_short, 8414 slow rows for slow_cap (Revision 3: replaced, 8414 cannot be written
by the profiler; now 8386 / 8387 / 8400)), a hook mutant for slow_cap and 8 constant
mutants (window 15 s, windows swapped, window 45 s, STOCK_TRAIN_MIN 6 and 8, N20_DROP 6 and 8, SLOW_CAP_ROWS raised),
each caught. Real: the log parser on 9 real Cordon logs (CRLF), 4 real run folders scored as loads
(wrong exe, fixed step, missing slow file: FAIL by name), made loads spliced into 4 real logs (real alife_slow lines,
real intro_start). Made: a fixture for every check, every NO VERDICT rule, each H rule, refusals. Mutants: every check
switched off one at a time (35 hooks, including "the two OFF groups pooled" and "the before-intro_start test removed");
the self-test also fails if a hook has no mutant.
Score ONCE after the series, output written in the same call:
`python3 -I wt-render-split/perf-harness/wu1/wu1_score.py root=out order=wt-render-split/perf-harness/wu1/wu1_order.txt out=out/wu1_state/wu1_score.txt`
Driver: hash preflight (PIN_ME placeholders refuse until registration; the first order line must be the prime load),
display :141 rule, user.ltx read-only check (r__framelimit 0, no sim_tick, no alife_warmup), state folder created and
writable BEFORE the queue is joined (else refused), the cleanup trap installed before the first queue operation
(withdraws WANT; releases only a claim this invocation acquired), claim / renewal / release decided by EXIT CODE only
(fixes sf1's output match: quiet-required.sh header "THE EXIT CODE IS THE API. DO NOT PARSE THE OUTPUT"); stop on
crash / timeout / failed check / refused renewal / third CONFOUNDED load / failed write of done.txt; no retry.
- **Probe (Revision 3, registered).** After the PRIME load and before the first scored load the driver runs
  `python3 -I wu1_score.py probe=out/wu1_prime order=wu1_order.txt` (exit 0 / 1). It applies every per-load rule of
  this prereg to the prime load's REAL log and files, and also requires a located save frame: files parse, command line
  = the registered (OFF) arm, intro_start / textures-handled lines, xrprof started / finished in order with the tag and
  frame count, at least one alife_pass and one alife_opu line, frame / stock-loop / limiter / threshold checks, a save
  frame located. **A failed probe ends the attempt with NO verdict:** the series stops before any scored load, the
  claim is released, the series is NOT scored and nothing is consumed (the prime load is never scored). Another
  attempt needs the supervisor's word. The same holds for any stop before the probe has passed (no scored load ran).
  The driver refuses to start a scored load without a passed probe.
- **Per-load check (Revision 3).** After every scored load: `python3 -I wu1_score.py check=out/<tag>
  order=wu1_order.txt`, the scorer's own rules for that one load (replaces the bash copy). Stop on any FAIL (missing /
  unparsable file, arm_mismatch: exe, save, -perf_capture, lever flag present/absent, -perf_fixed_dt / -det_mode /
  -sim_tick; off_not_stock, L1-L5, no_measure_lines) and on VOID framecount / capture_lines / stock_loop /
  frame_limiter / threshold_header; not on not_clean (counted, stop at the third), window_short, save_unlocated or
  slow_cap (VOIDs the scorer handles). tools/s2_bench.sh returns rc 0 for a completed CONFOUNDED capture (RC set only
  by its capture loop, lines 57-64; the VERDICT is written after, 72-74), so such a load reaches the counter.

## 9. Predictions (registered; GUESS unless marked)
| # | prediction | P |
|---|---|---|
| P1 | every ON load passes L1-L5 | 90 % |
| P2 | no ON load logs "precache held" (the pass is claimed before the countdown reaches 3) | 85 % |
| P3 | OFF-IN-PLAY 8-14 of 16; OFF-IN-LOADING 2-8 | 75 % |
| P4 | median OFF-IN-PLAY worst (30 s) 280-370 ms (DESK from MEASURED 293-361) | 70 % |
| P5 | median ON worst (30 s, save excluded) <= 100 ms (MEASURED in-loading 13-23 ms after 3 s; first seconds GUESS) | 70 % |
| P6 | median OFF-IN-PLAY n20 (30 s) 13-21 (MEASURED 16-19); n33 3-13 (MEASURED 1-12, median 10); train span 12-22 s (MEASURED hitch-to-last 13.9-21.1 s; may start earlier on early frames, GUESS) | 60 % |
| P7 | median ON n20 (30 s) <= 4; n33 <= 1 (MEASURED in-loading 0-2 / 0; GUESS first seconds) | 70 % |
| P8 | 15 s window (reported only): median OFF-IN-PLAY n20 2-6 (MEASURED 2-6), median ON <= 3 | 65 % |
| P9 | load-time price, median ON - median OFF-IN-PLAY: -0.3 to +0.8 s (brief: +0.3 to +0.7; past stamps show in-play loads already pay a long loading frame, 5.4) | 65 % |
| P10 | median forced-pass X 450-900 ms; N = M in every ON load | 70 % |
| P11 | load time (engine clock) 18-25 s in every load | 85 % |
| P12 | a save frame found in every capture, 300-1500 ms | 70 % |
| P13 | SAME AS GOOD ORDER: SAME | 65 % |
| P14 | (the drafting sub-worker's guess, kept for the record; MY registered guess is in section 13) result PASS 65 %, NO VERDICT 20 % (most likely loadtime, or stock_orders_alike if the first seconds after loading are slow in OFF-IN-LOADING), FAIL 15 % (Revision 2: no_stock_train is now unlikely, 16-19 against 7) | - |

## 10. Rule One (the lever must not change the simulation beyond picking stock's in-loading order)
New versus stock, and nothing else intended:
1. Exactly ONE full pass per load (limit u32(-1)), at the first scheduled pass with the actor present. In an
   in-loading load it IS stock's first wide pass (same moment, same map order from the same cursor); stock may run 1-3
   wide passes and the script's own 65534 window still runs as in stock. In an in-play load it is a pass stock did not
   have. Whether stock ever runs exactly one is not known (alife_pass lines in OFF loads will show it: reported).
2. Held precache frames (only if the pass has not run when the countdown reaches 3, at most 300): stock never has more
   than 60 precache frames. On each held frame: no input, muted sound, no scheduler budget, no task-manager update, later
   capture start. Every held frame is logged and reported.
Picks stock's in-loading order: timing of first object updates, random draws and switch decisions follow the in-loading
family, not the in-play one (desk read Q6). **This experiment does NOT prove simulation equivalence.** It measures frames
and load time. The deterministic harness always takes the in-loading order (desk read Q5), so equivalence of the ON
in-play case to stock's in-loading case is a prediction, not shown here.

## 11. The patch's two doubts: explicit decision, no second arm
- Same-level load / level change (actor spawned after precache frame 3 -> up to 300 held frames): NOT testable
  headlessly with tools/s2_bench.sh. It only starts a save from the command line (`-start server(.../load)`); a
  same-level load needs a console `load` after the first load, a level change needs the actor to walk into a level
  changer; the bench cannot type console commands and `-perf_noinput` turns input off. Listed as a limit. (A future arm
  could use `-ltx <file>` to run a startup console script, x_ray.cpp:259, 324; not checked, and it would need a new file
  under run/.)
- Very fast loading frames (cap fires early): partly covered. Every ON load reports held frames K against the cap 300 and
  a cap hit is a FAIL (L4). Only the cordon1 save load on this fast box is covered; a slower or faster loading path is not.

## 12. What this cannot show
Other saves and levels; level change and same-level load; vsync or a frame cap; the 2012 machine; whether ON's later
simulation equals stock's in-loading simulation; the true process-start-to-play time (engine clock only); the cost of the
cordon1 scripted save beyond reporting it.

## 13. Registration (EnginePerf, 2026-11-26)
Drafted in three rounds by Opus sub-workers; read by me, self-test rerun by me after each round (final: 164/164).
Review: CodeRabbit, wu1/coderabbit-triage-2026-11-26.md (14 findings on wu1, 13 fixed with tests, 1 answered; 2 more on the
fix round, fixed). The driver has had `bash -n` only: its first real run is the series itself; the probe after the prime
load (section 8) is the guard.
Pins (sha256): wu1_order.txt 66b38e7e1606fbb2aaec2c9626ffb119301196f5faf27c129f608c9ae0e8bfa7; wu1_score.py 54532b9521045bcddb8925dea63a1d5b203f0259c3eaf5537a062307df2145c1;
wu1_selftest.sh 536bbde2809b0d26c59c7879c91f87872d653512e94d7cddfd1d26a8de9bd1ca; wu1_run.sh 0a252197ea6cfbc22f800b8140609a1c658b5fd7967d5b5280742c5209a4dbdd;
run/bin/Warm18.exe 15828a7a6cadd38e42cc376869f581166884c04f1eb236baf79054a74332ae9d; the bench, save and tool pins are
in wu1_run.sh lines 37-42 and are checked by its preflight.
Departure from the Overseer's wording, stated plainly: the headline window is 30 s, not "~15 s" (section 1b); the 15 s
numbers are reported beside it.
MY outcome guess: PASS 45 %, NO VERDICT 30 %, FAIL 25 %. Reasons: no real-time capture of the first seconds after loading
exists (every bound on them is mapped from fixed-step captures that start 600 frames later); the new log lines have
never been parsed from real bytes (the probe turns that into "no attempt" rather than a wasted series, which is not one
of the three outcomes); H1's stray rule and stock_orders_alike are the likeliest NO VERDICT / FAIL routes; a lever-on
load that holds the loading screen to its cap is a FAIL of the lever and I cannot rule it out from the desk.
Score once: `python3 -I wu1_score.py` with the arguments printed by the driver at the end of the series (section 8); the
report is written by that single run; no re-score after a scorer fix (a fixed scorer's reading is POST-HOC DESCRIPTIVE).
