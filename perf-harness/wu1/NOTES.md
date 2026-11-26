# wu1 desk notes (2026-11-26): limits, my decisions, contradictions, what I could not verify

## 1. Limits (read first)
1. Nothing was run on the box. The driver is checked with `bash -n` only. Its per-load check function was run against
   the self-test fixture folders (copied out of the script into my scratchpad): it agrees with the scorer on all 21 tried.
2. The scorer has never seen a real Warm18 log. The alife_pass / alife_opu / [alife_warmup] parsers are tested on made
   lines written from the source format strings (alife_update_manager.cpp:151-152, 191-192; alife_simulator_script.cpp:507-508;
   device.cpp:142, 149), and spliced into real CRLF logs. A format drift in the CI build would make every load FAIL
   [unparsable] by name. It would not give a wrong verdict.
3. No real-time capture of the first seconds after a Cordon load exists. Every past capture started 600+ frames after
   loading. So the ON-side numbers (worst frame, n20) are GUESSES, and so is the cost of the scripted save frame.
4. Only the cordon1 save load is covered. Same-level load and level change are not testable headlessly with the bench
   (prereg 11).
5. The load time is on the engine clock from device init, not from process start (prereg 5.4).
6. The 15 s window sees the stock hitch and only roughly the first third of stock's train (prereg 6). That is why the
   NO VERDICT guards exist and why the 30 s window is reported.
7. This experiment does not show simulation equivalence (prereg 10).

## 2. Decisions I made that the brief did not specify (the Overseer should confirm)
1. **The cordon1 scripted save is excluded from the headline numbers.** It is found by the perf_slow rows naming
   CALifeStorageManager::save / ::prepare_objects_for_save. To get those rows I added `-perf_slow 20`. If the zone is in
   perf_sites but in no slow row, the load is VOID [save_unlocated]. Without this rule, the worst frame in both arms would
   probably be the save, not A-Life. Nobody has measured its cost.
2. **Load time = engine clock (time_global) at the first frame after the loading screen.** It is derived from the
   measurement lines plus the frames file (prereg 5.4). It is not a driver-side watcher, because xray.log is written
   through a buffered fwrite (FS_internal.h CFileWriter, log.cpp:244-257) and could lag by seconds.
3. **16 ON + 16 OFF + 1 priming OFF load** (not scored), in ABBA blocks starting with ON. The derivation is in prereg 3.2.
4. **Uncapped**, as sf1. Reasons are in prereg 5.3.
5. **N = 10,000 capture frames, warm-up 0.** 30 s at the fastest frames seen, so the 30 s secondary window is covered.
6. **FAIL versus VOID.** FAIL: missing/empty/unparsable file (the brief), arm_mismatch on the logged command line,
   off_not_stock, and the lever checks L1-L5. VOID (not counted, no re-run): not_clean, framecount, capture_lines,
   stock_loop, frame_limiter, threshold_header, window_short, save_unlocated. A missing folder (the driver stopped early)
   is also a FAIL by name, so a stopped series scores FAIL. It must be re-registered, not scored as is.
7. **Lever check P >= 3**, not P > 0. This is the stronger condition the code supports (prereg 4.1 L3).
   **N >= 0.9 M** is my bound (prereg 4.1 L5).
8. **OFF grouping requires the alife_pass lines AND the alife_slow lines to agree.** There is a third bucket, OFF-OTHER,
   reported and never used. The cut is 50 alife_slow lines in one frame (measured both sides of it on 22 logs).
9. **H1 / H2 / H3 forms.**
   - H1: a median ratio (0.5) plus at most one stray ON load at or above stock's in-play median.
   - H2: a DIFFERENCE of at least 3 frames over 20 ms, not a ratio. Slow frames that are not A-Life and appear in both
     arms would otherwise make a working lever fail a ratio rule.
   - H3: a 2.0 s price bound.
   - n33 and the train span are reported, not deciding.
10. **NO VERDICT `stock_orders_alike`** (added beyond the brief). With at least 2 in-loading loads, if stock's good order
    cannot be told apart from its bad order in the 15 s window, the window cannot judge the lever.
11. **CONFOUNDED loads:** the scorer voids them, there is no re-run (the brief says no retry), and the driver stops at the
    third.
12. **The driver repeats the scorer's per-load checks in awk.** This lets it stop at the first failed check: command line,
    lever log check, off_not_stock, files, frames, stock loop, limiter, threshold.
13. **Claim handling.** Claim, renewal and release are decided by exit code. `want` is called when a claim is refused; its
    exit code is not acted on, because the loop tries claim again every 60 s. sf1 had used
    `check | awk '$1=="HELD:"'` and `claim | grep '^claimed'`; both were output matches and are gone.
14. **Self-test mutants use an `on("<name>")` hook** in the scorer, always True. A mutant is the scorer with one hook
    returning False. The self-test fails if a hook has no mutant.
15. **Things I did not do.**
    - I did not run CodeRabbit. It reviews git changes and the brief forbids git commands that change anything.
      The mutant pass stands in for it.
    - I did not write a memory note under ~/Documents/claude memories/. The brief allows new files only inside wu1/ and
      no edit of any existing file. Caden's CLAUDE.md asks for one, so I am flagging the conflict here and in my report.
16. **The self-test leaves its stamp folders** under wu1/selftest-runs/ (about 18 MB each; 4 exist from my iterations,
    the last one is the reference). Nothing was deleted.

## 3. Where the brief or the three notes are contradicted or refined by the code / files
1. **"8 of 11 past Cordon loads".** On disk there are 22 real-time-loading Cordon bench loads with alife_slow lines:
   15 in-play, 7 in-loading (list in prereg 3.2). The ratio agrees (0.68 vs 0.73).
2. **The design note's first rule said "worst frame in the first 5 s".** Stock's hitch lands at about 7.4-8.6 s after
   loading (DESK, prereg 6), so a 5 s window would show nothing. The Overseer's later 15 s is needed, and it is still tight.
3. **The brief's "load time ... from process start ... to intro_start".** The log has no time stamps, and file writes
   are buffered. Process start is not visible in the log except "Game started: <time of day>", which comes before device
   init. I used the engine clock (decision 2). The end point is the first frame after the loading screen: 2-3 loading
   frames after intro_start.
4. **The brief's "the profiler has a cap on recorded frames".** There is no cap on the frames file: frame times go into a
   vector of `frames` entries (profiler_agg.cpp:94-96, 187). The only cap is on the slow-frame dump: 600 frames x 14 rows
   (profiler_agg.cpp:42-43, 190).
5. **The brief's "capture starts only after loading ends plus a warm-up count".** It also needs `g_bLoaded` and
   `!Device.Paused()` (device.cpp:422). A pause at load end would shift capture frame 0 (not seen in the -nointro path;
   not verified at run time).
6. **None of the three notes mention cordon1's scripted save.** It runs about 4 s of time_global after the actor's first
   update and lands just after the loading screen in all 38 past Cordon logs. That is inside the headline window in
   both arms.
7. **The load-time price.** The design note and brief expect +0.3 to +0.7 s, the cost of one full pass (DESK 0.5-0.75 s:
   the logged sum of the wide pass is 506-573 ms). Past "Time continual is:" stamps (same device clock) show in-loading
   loads NOT longer than in-play loads in the r1/r1a series: 18.97-19.82 s against 19.55-19.86 s. A plausible reason is
   that in-play loads had their own long loading frame, which is how they skipped the wide window (desk read Q3). The
   measured ON - OFF-IN-PLAY price may therefore be smaller than the pass's cost. The pass's own X ms is reported for
   that reason.
8. **sf1's prereg 3.2 and NOTES 3 assume the A-Life pass is scheduled in GAME time.** The desk read showed it is
   time_global (wall time in real play). This does not affect wu1, but sf1's 11,500-frame warm-up derivation rests on it.
9. **The patch notes' scorer advice "P >= 3" and the brief's "precache_left > 0".** They agree in practice. I registered
   the stronger P >= 3 (decision 7).
10. **The desk read cites safe_map_iterator_inline.h lines 165 / 181-200.** The patch notes correct this to 104-112 /
    128-146; I confirmed 128-146 (the pass loop).

## 4. What I could not verify
- That Warm18.exe (hash 15828a7a...) is built from d77d70b5c and prints exactly these lines. I read the source on the
  branch, not the binary.
- That nothing pauses the game at the end of loading. If something does, capture frame 0 is late and the load time
  derivation is off by the paused frames.
- That TimerGlobal is not paused during loading by a focus change (the bench's xdotool windowactivate nudges). If it is,
  the load time would read short in that load.
- When stock's hitch lands in real time. The 7.4-8.6 s figure is inferred from fixed-step captures after a 600-frame
  real-time warm-up whose wall time was never recorded.
- The cost of the scripted save, and whether its zone is always among the 14 largest self times of its frame (if not:
  VOID save_unlocated).
- Whether Warm18's extra log lines during loading shift the race between the pass and the 1 s reset. That would change
  p(in-play) and the group sizes.
- Whether alife_pass lines with precache 0 always exist. They stop 10 s after the simulator's first pass; a precache
  phase longer than about 10 s would leave none (load time unmeasurable, NO VERDICT [loadtime] if common).
- `quiet-required.sh release <worker>` without a claim id, after same-text renewals: copied from sf1 / t3_kw1, not re-read
  in full.
- The fastest Cordon frame, 3.22 ms, is sf1's figure. I did not re-derive it.

## 5. Fixture list
- Real logs read in place by the parser: out/cp1, cp2, cp3, cp4, uc1, lc1, lc2, lc3, cpl1 (xray.log).
- Real folders scored as loads: out/cp1, cp2, uc1, r1a_cor_1.
- Real logs spliced into made loads: cp1, cp2, cp4, uc1.
- The self-test checks that nothing in these folders changed (find -newer).
- Reference self-test folder: wu1/selftest-runs/20261126-162802-2201255 (101/101).
