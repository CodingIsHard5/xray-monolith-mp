# PREREG wu1: does `-alife_warmup` remove the slow frames after a level load? (DRAFT 2026-11-26, desk only, NOT registered)

Every check, rule, number of loads and bound below is fixed before any run. Nothing here has been run on the box. No box
run without the Overseer's GO. Sources: plan-facts/B2b-warmup-design-2026-11-26.md (with its CORRECTED section and the
Overseer's prereg requirements), B2b-desk-read-2026-11-26.md, B2b-patch-notes-2026-11-26.md; harness conventions from
perf-harness/sf1. Labels: MEASURED (a number in a file, cited), DESK (my count or reading of existing files or code),
GUESS. "Load" = one engine start that loads cordon1 and captures the first seconds of play.

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
- Warm18.exe is a profiler build (all probes, about 0.7 ms per frame of probe cost, as the SimTick builds: sf1 NOTES 6).
  Absolute frame times are a little high; both arms carry the same cost.
- The load time is measured on the engine's own clock from device start-up (section 5.4), not from process start: it
  misses Wine start-up and file-system init, which the lever cannot touch.
- Fixtures: the scorer has only been run on made data and on real logs that predate the alife_pass / alife_opu lines.
  The first real Warm18 log is the first test of those parsers on real bytes.

## 1. Question
On cordon1 in real time, with the lever ON (`-alife_warmup`) against lever OFF (stock), in ABBA order:
(a) does every ON load show that the one full A-Life pass ran behind the loading screen;
(b) in the first 15 s of play after the loading screen, are the worst frame and the number of slow frames of ON loads
    much smaller than those of the stock loads that took the IN-PLAY order;
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
| `-perf_capture W,N,tag` | **W = 0 (CHANGED)**, N = 10000 (derived 5.3) | s2_bench.sh:56 | capture from the first frame after the loading screen (5.1) |
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

### 3.3 Box time
Per load: Wine start about 9 s (MEASURED cp1: bench "start 09:57:04", engine "Game started 09:57:13"), engine clock to the
end of loading 19-23 s (MEASURED: "Time continual is:" stamps right after intro_start in 22 past Cordon logs, 18.97-23.02 s),
capture 10,000 frames at about 5 ms = about 50 s, dump, quit and the bench's 8 s wait about 10-15 s: bench about 90-110 s.
Driver overhead per load: settle (1-min load < 2) about 20-60 s after an engine run, display grace 0-20 s.
Estimate: 33 x 2.2-3.0 min = **73-100 min**, under the 3 h slice (the driver stops with < 15 min left).

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
- `arm_mismatch` (**FAIL**): the logged command line must name Warm18.exe, cordon1, `-perf_capture 0,10000,<tag>`, no
  `-perf_fixed_dt` / `-det_mode` / `-sim_tick`, and `-alife_warmup` present exactly on ON loads.
- `off_not_stock` (**FAIL**): any `[alife_warmup]` line in an OFF load.
- VOID: `not_clean` (bench VERDICT not clean), `framecount` (frames rows = sites header = 10000), `capture_lines`
  (xrprof started/finished lines with 10000 frames, started after loading), `stock_loop` ("Process seqFrame" main calls =
  frames, "Sim tick" 0 calls), `frame_limiter` ("Eco Render" 0 calls), `threshold_header` (perf_slow 20.000),
  `window_short` (capture shorter than 15 s), `save_unlocated` (save calls in the capture but no slow row names them).

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
of frame i = sum of ms[0..i-1]. The 15 s window = frames starting before 15,000 ms.
### 5.3 Capture length and the frame limiter (decisions)
- The profiler has NO cap on recorded frame times (a vector of `frames` entries, profiler_agg.cpp:94-96, 187). Its only cap
  is on the slow-frame dump: 600 frames x 14 rows (profiler_agg.cpp:42-43, 190). wu1 expects < 40 frames over 20 ms
  per capture (DESK: 18 in cp1's whole fixed-step capture), so the cap is 15x away.
- N = 10,000: 15 s must be covered at the fastest frames seen. Fastest Cordon frame in any capture 3.22 ms (sf1 3.2,
  DESK) -> 3.0 ms: 10,000 x 3.0 ms = 30 s = 2x the window, and covers the 30 s secondary window at that worst case.
  At the expected ~5 ms it is about 50 s of capture.
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
frame with precache 0; +-1 frame if the A-Life thread read P after End() in that frame); then the first such line with
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
### 5.6 Headline numbers per load (15 s window, save frame excluded)
worst frame (ms); n20 = frames > 20 ms; n33 = frames > 33.3 ms; train span = from the start of the first to the end of
the last frame > 20 ms (s); load time (s, engine clock). Secondary, reported: the same in a 30 s window; worst including
the save frame; for ON the pass's X ms and held frames.

## 6. Constants, each with its derivation (all derived for wu1; none copied from another arm's bound)
| constant | value | derivation |
|---|---|---|
| WINDOW | 15 s | supervisor's player window. Stock's hitch is expected at about 7.4-8.6 s after loading (DESK: in-play logs put it at engine frame 791-800 = about 132 fixed-step frames (4.4 s) after a 600-frame real-time warm-up (about 3-4.2 s at 5-7 ms); time_global decides when passes run). Train starts about 2.7 s later, ends about 12 s after that, so 15 s sees the hitch and roughly the first 8-11 of 27 passes |
| SLOW1 / SLOW2 | 20 / 33.3 ms | supervisor; 33.3 ms = one frame at 30 fps |
| LEGACY_WIDE | 50 lines in one frame | 20-per-pass can log at most 20 lines per pass; in-play logs peak at 16-19, wide passes log 128-185 (DESK, 22 logs). 50 is 2.6x above 19 and 2.6x below 128 |
| WIDE | n > 20 | stock limits are 3 (alife.ltx) and 20 (script reset); n <= limit (CUpdatePredicate) |
| P_MIN | 3 | hold point (device.cpp:168); game_loaded acts at <= 2 |
| N_M_MIN | 0.9 | 4.1 L5: churn of a few squads during one pass versus a count-limited pass at 3 or 20 of ~thousands |
| MIN_ON | 8 of 16 | half the registered ON loads; below it the ON median rests on a minority of what was planned |
| MIN_OFFP | 3 | smallest group with a median that is not one load and with a stray rule that means something; 3.2 sizes the run for it |
| STOCK_HITCH_MIN | 150 ms | half of the smallest MEASURED first-update cost of zat_b106 (287 ms over 22 logs: 270-352 ms); below it the 15 s window did not catch stock's hitch: nothing to remove, NO VERDICT |
| STOCK_TRAIN_MIN | 3 frames > 20 ms | DESK expectation 4-9 in the window (cp1's full fixed-step capture: 18 frames > 20 ms for the hitch plus 27 passes, about 0.65 per pass; 8-11 passes in 15 s plus the hitch); 3 = the H2 drop, so H2 is decidable |
| WORST_RATIO (H1) | 0.5 | "removes the hitch": ON's typical worst at most half of stock in-play's (~310 ms -> 155 ms); an ON worst made of ordinary first-seconds frames (GUESS 20-80 ms) clears it, a hitch of the same kind (> 155 ms) does not |
| WORST_STRAY (H1) | 1 ON load may reach the in-play median | one stray hitch unrelated to A-Life in 16 loads (Wine/DXVK stall) should not decide; two may not be chance |
| N20_DROP (H2) | 3 | stock in-play adds about 7 frames > 20 ms in the window (hitch + about 6 train frames, DESK above); a DIFFERENCE, so slow frames unrelated to A-Life that both arms share (shader compile, GC) cancel; 3 is under half the expected 7 |
| PRICE_MAX (H3) | 2.0 s | a full pass costs 0.5-0.75 s (DESK: alife_slow sum of the wide loading pass 506-573 ms over objects >= 1 ms in 7 in-loading logs, plus objects under 1 ms); 2.0 = 2x the 1.0 s upper end: more than the moved work itself means something else (held frames, a second pass) was added. Load-to-load spread in one series about 0.2-0.4 s (MEASURED stamps, 5.4) |
| stock_orders_alike (NV) | OFF-IN-LOADING median worst > 0.5 x OFF-IN-PLAY median worst, or n20 differs by < 3 | the same bounds as H1/H2 applied to stock's own two orders: if stock's good order cannot be told from its bad order in the window, the window cannot show the lever. Checked only with >= 2 OFF-IN-LOADING loads |
| LOADTIME_MIN | 8 ON and 3 OFF-IN-PLAY measurable | as MIN_ON / MIN_OFFP |
| SAME_* (secondary) | worst <= 1.25 x OFF-IN-LOADING + 10 ms; n20 <= +2; load within 1.0 s | GUESS: no real-time post-load Cordon data exists; reported only, decides nothing. Flagged |
| MAX_CONFOUNDED (driver) | stop at the 3rd CONFOUNDED load | GUESS: one or two transient heavy processes are tolerable; three means the box is not quiet |
| N capture frames | 10,000 | 5.3 |
| HOLD_CAP | 300 | COPIED from the patch (device.cpp:128), reported against only |

### 6.1 Margin table (expected vs bound; UNDER 2x flagged; COPIED = not derived for wu1, flagged)
| item | expected | bound | margin | status |
|---|---|---|---|---|
| valid OFF-IN-PLAY loads | ~11 of 16 (p 0.68) | >= 3 | 3.7x | derived (3.2) |
| stock hitch time after loading | 7.4-8.6 s (DESK) | inside 15 s | 1.7-2.0x | **UNDER 2x** (NO VERDICT no_stock_hitch if it lands late) |
| median OFF-IN-PLAY worst | 290-360 ms (MEASURED first-update cost 287-352 + frame) | >= 150 ms | 1.9-2.4x | **UNDER 2x at the low end** |
| median OFF-IN-PLAY n20 (15 s) | 4-9 (DESK) | >= 3 | 1.3-3x | **UNDER 2x** (THIN: the window sees only the train's start) |
| H1 ON worst vs 0.5 x in-play | 20-80 ms (GUESS) vs ~155 ms | <= 0.5 x | 1.9-7.8x | **UNDER 2x at the high GUESS** |
| H1 strays | 0 | <= 1 | - | derived |
| H2 drop in n20 | ~7 (4-9) | >= 3 | 1.3-3x | **UNDER 2x** |
| H3 price | -0.3 to +0.8 s | <= 2.0 s | 2.5x | derived; load spread 0.2-0.4 s MEASURED-ish |
| stock_orders_alike: OFF-IN-LOADING worst | as ON (20-80 ms) | <= 155 ms | 1.9-7.8x | **UNDER 2x at the high GUESS** |
| L3 precache_left | ~25-35 (DESK: wide pass at frame 32 of 60 in L logs) | >= 3 | 8x | derived |
| L5 N/M | 1.0 (DESK) | >= 0.9 | 1.1x on the ratio, 40x against a count-limited pass | derived (the bound is about failure modes, not noise) |
| LEGACY_WIDE | 16-19 (P) / 128-185 (L) lines | 50 | 2.6x both sides | derived from MEASURED |
| held frames | 0 (DESK) | cap 300 | - | COPIED (patch), reported only |
| capture covers 15 s | 30 s at 3.0 ms, ~50 s at 5 ms | 15 s | 2-3.3x | derived |
| capture covers 30 s (secondary) | 30-50 s | 30 s | 1.0-1.7x | **UNDER 2x** (reported only; n/a if not covered) |
| slow-dump cap | < 40 frames > 20 ms | 600 frames | 15x | derived |
| load time measurable | in-play lines exist if the precache phase < 10 s (GUESS 1-6 s) | 10 s window | 1.7-10x | **UNDER 2x at the slow end** (NO VERDICT loadtime) |
| load duration (bench) | 90-110 s | BENCH_TIMEOUT 900 s | 8x | COPIED (bench default) |
| series duration | 73-100 min | 3 h slice, stop at < 15 min left | 1.6-2.2x | **UNDER 2x at the slow end**; box rule COPIED |
| settle | load < 2, no other >= 100 % process | 5 min cap | - | COPIED (s4c_series.sh) |
| CONFOUNDED loads | 0-1 | stop at 3rd | - | GUESS |
| SAME_* secondary | ON = OFF-IN-LOADING | 1.25x + 10 ms, +2, 1.0 s | - | GUESS, reported only |

## 7. Verdict rules (implemented exactly in wu1_score.py)
1. **FAIL** (exit 1) if any scored load has: a missing/empty/unparsable file (named), arm_mismatch, off_not_stock, or any
   ON lever check L1-L5 failing. A failing ON load is never dropped.
2. Else **NO VERDICT** (exit 3) if any of: `few_on` (< 8 valid ON), `few_inplay` (< 3 valid OFF-IN-PLAY),
   `no_stock_hitch` (median OFF-IN-PLAY worst < 150 ms), `no_stock_train` (median OFF-IN-PLAY n20 < 3),
   `stock_orders_alike` (6), `loadtime` (load time measurable in < 8 ON or < 3 OFF-IN-PLAY). All that apply are listed.
3. Else **PASS** (exit 0) if all of:
   - H1: median ON worst <= 0.5 x median OFF-IN-PLAY worst, and at most 1 ON load has worst >= median OFF-IN-PLAY worst;
   - H2: median OFF-IN-PLAY n20 - median ON n20 >= 3;
   - H3: median ON load time - median OFF-IN-PLAY load time <= 2.0 s.
   Otherwise **FAIL** naming H1/H2/H3.
4. Secondary, reported only: "SAME AS GOOD ORDER" ON versus OFF-IN-LOADING (SAME / DIFFERENT / NO VERDICT with < 2).
5. n33, train span, the 30 s window, worst including the save, X ms, held frames, OFF-OTHER: reported only.
The headline comparison is ON versus OFF-IN-PLAY. ON versus OFF-IN-LOADING is the "is ON the same as stock's own good
order" comparison. The groups are never pooled; a self-test mutant that pools them is caught (section 8).

## 8. Scorer, self-test, driver
`wu1_score.py` (stdlib, `python3 -I`), `wu1_selftest.sh`, `wu1_run.sh` (bash -n only), `wu1_order.txt`; hashes in
NOTES.md and in the final message (they are pinned into the driver at registration; until then the driver refuses).
Self-test 2026-11-26: `WU1 SELFTEST PASS 101/101 (11 real cases, 53 made cases, 35 mutants caught, 1 hook-coverage
check, 1 untouched check)`. Real: the log parser on 9 real Cordon logs (CRLF), 4 real run folders scored as loads
(wrong exe, fixed step, missing slow file: FAIL by name), made loads spliced into 4 real logs (real alife_slow lines,
real intro_start). Made: a fixture for every check, every NO VERDICT rule, each H rule, refusals. Mutants: every check
switched off one at a time (35 hooks, including "the two OFF groups pooled" and "the before-intro_start test removed");
the self-test also fails if a hook has no mutant.
Score ONCE after the series, output written in the same call:
`python3 -I wt-render-split/perf-harness/wu1/wu1_score.py root=out order=wt-render-split/perf-harness/wu1/wu1_order.txt out=out/wu1_state/wu1_score.txt`
Driver: hash preflight (PIN_ME placeholders refuse until registration), display :141 rule, user.ltx read-only check
(r__framelimit 0, no sim_tick, no alife_warmup), claim / renewal / release decided by EXIT CODE only (fixes sf1's
output match: quiet-required.sh header "THE EXIT CODE IS THE API. DO NOT PARSE THE OUTPUT"), after every load the
same checks as 4.1-4.3 in bash, stop on crash / timeout / failed check / refused renewal / third CONFOUNDED load; no retry.

## 9. Predictions (registered; GUESS unless marked)
| # | prediction | P |
|---|---|---|
| P1 | every ON load passes L1-L5 | 90 % |
| P2 | no ON load logs "precache held" (the pass is claimed before the countdown reaches 3) | 85 % |
| P3 | OFF-IN-PLAY 8-14 of 16; OFF-IN-LOADING 2-8 | 75 % |
| P4 | median OFF-IN-PLAY worst (15 s) 280-370 ms | 70 % |
| P5 | median ON worst (15 s, save excluded) <= 100 ms | 75 % |
| P6 | median OFF-IN-PLAY n20 (15 s) 4-10; n33 1-4; train span 3-7 s | 55 % |
| P7 | median ON n20 (15 s) <= 3; n33 <= 1 | 75 % |
| P8 | 30 s window: median OFF-IN-PLAY n20 12-22, median ON <= 4 | 60 % |
| P9 | load-time price, median ON - median OFF-IN-PLAY: -0.3 to +0.8 s (brief: +0.3 to +0.7; past stamps show in-play loads already pay a long loading frame, 5.4) | 65 % |
| P10 | median forced-pass X 450-900 ms; N = M in every ON load | 70 % |
| P11 | load time (engine clock) 18-25 s in every load | 85 % |
| P12 | a save frame found in every capture, 300-1500 ms | 70 % |
| P13 | SAME AS GOOD ORDER: SAME | 65 % |
| P14 | result PASS 60 %, NO VERDICT 25 % (most likely no_stock_train or stock_orders_alike), FAIL 15 % | - |

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
