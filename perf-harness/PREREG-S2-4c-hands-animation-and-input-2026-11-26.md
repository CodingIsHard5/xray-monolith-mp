# PREREG S2 stage 4c: first-person hands and weapon animate every frame; first run with input on (registered 2026-11-26, before any run)

Build: perf/sim-tick f36fe61b8 (CI green, run 45775072388), installed as run/bin/SimTick17.exe
sha256 cb5f2f8706ed89e5bcab9fdc832676b461543e725b13375a3be382240da11d82. Previous build: SimTick16.exe 91a8c0df (stage 4b
slice 1). Stock reference for part A: Stance14.exe b4ce9c77. Nothing below has been run. No box run without the Overseer's GO.

## 1. What the build changes (gated mode only)
- The hands and held-item models (marked by xrGame) take their animation time from one presented clock, tick time plus
  the 4a fraction, so their pose advances on every rendered frame at simulated speed. WHEN a motion ends is still
  decided in the tick (unchanged).
- A 4a gap, fixed here: near-wall offset, attachment transforms and hand IK ran only in the tick while the hands update
  had moved to the per-frame step. They now run right after the hands update each frame (player_hud::OnFramePose).
- Three counters: `sim_hud_anim` (the per-frame hands step), `sim_mouse_poll` (the per-frame mouse code reached the
  device read), `sim_mouse_read` (that read succeeded).
- Switch off: the models are marked but the clock branch is not taken; OnFrame runs whole, as stock.
- Review: CodeRabbit, one question (is the pose step run once per hands update): yes by construction.

## 2. Part A: off = stock (method and flags of PREREG-S2-4b section 2, unchanged)
`tools/s4c_partA.sh` af4f021c = `tools/s4b_partA.sh` with tags s4c_s1..3 / s4c_o1..3, build SimTick17 and its hash.
Flag preflight run today: ok. Scored once:
`python3 -I tools/s2_partA2_score.py out s4c_s1,s4c_s2,s4c_s3 s4c_o1,s4c_o2,s4c_o3 out/s4c_partA_score.txt` (3e487ea9).
R1 FAIL = harness not usable, no verdict. R1 PASS and R2, R3 or R4 FAIL = off is NOT stock: stop, no part B or C.
Guess: all four PASS 75 %.
Limit: serial test mode, first 320 A-Life passes; shows the new code is inert when off, nothing about gated mode.

## 3. Part B: the hands step runs every frame, and what it costs (only after part A PASS)
`tools/s4c_run.sh` 1f3f7340: `tools/s4c_series.sh` 6539d1ce and bench 072af8ed on `tools/s4c_order.txt` b2ea5397.
s4c_series.sh is the part B series script 75b2335b with two changes: the bench flags can be set by S2_BENCH_FLAGS
(default, used here: the same three flags as before), and the box is not claimed while Caden's test launcher is
running. Ten runs, mirrored, 12000 frames after 3000 warm-up, bench1, uncapped: new build at tick 30 x2, tick 60 x2,
off x1; previous build (4b) the same. Scored once: `python3 -I tools/s4c_score.py out out/s4c_score.txt` (4ce55785);
it reads `<tag>_rr` when `<tag>` is not clean (the series' rule, same source) and names each substitution.
| check | PASS when | guess |
|---|---|---|
| X1 hands step every frame | each of the four new gated runs: `sim_hud_anim` calls in N-2..N | 75 % |
| X2 off | new build off: no `sim_hud_anim` call | 97 % |
| W1, W2 (4b, kept) | captures >= updates and in-between draws > 0 gated; both 0 off | 93 % |
| V1, V2 (4a, kept) | camera calls in N-2..N gated, actor updates = updates; off: all = frames | 95 % |
| V3 frame time | mean new / mean previous <= 1.05 at 30 and at 60 (arm spread > 8 % = no verdict for that rate) | 75 % each; I expect 1.00 to 1.04 |
| V4 tick cost | sim ms per tick new / previous <= 1.05 at 30 and at 60 | 85 %; I expect 0.97 to 1.01 (pose work moved out of the tick) |
| V5 off frame time | new off / previous off within 0.95..1.05 (one run each) | 88 % |
All PASS: about 40 %.
X1's weak points, stated now: (a) the step runs only when the actor is "focused"; I have not checked that it is in a
headless run. A zero is a FAIL as registered and would mean "not exercised here", which I would say. (b) X1 counts the
step, not a pose change: it does not show that a marked model was on screen or that its pose moved.
Self-test `python3 -I tools/s4c_selftest.py out <work dir>` (b34ba300): PASS 21/21 on real run folders (run today).

## 4. Part C: input ON for the first time (only after part B is scored; a part B FAIL on V3 to V5 alone does not block it)
`tools/s4c_input_run.sh` 73e4f28a: the same series script and bench, on `tools/s4c_input_order.txt` f7753b2a, with
S2_BENCH_FLAGS="-perf_nopresent -perf_slow 20" (no -perf_noinput). Two runs of the new build, 6000 frames after 1500
warm-up, bench1: tick 30, tick 60. Headless (Xvfb); no one moves a mouse.
Scored once: `python3 -I tools/s4c_input_score.py out out/s4c_input_score.txt` (75bf5ae9). Per run PASS when all hold:
- the capture finished (frames file rows = frames) -- this is "no crash";
- updates U with 0 < U < N, actor updates = U;
- camera calls and `sim_hud_anim` calls each in N-2..N;
- `sim_mouse_poll` between N-U-2 and N (the mouse code is reached on the frames without a tick).
Reported, no gate: `sim_mouse_read` (a headless run may never acquire the device: 0 is possible), frame ms, the
bench's clean/confounded verdict (this part measures no time). A run the series re-runs is read from `<tag>_rr`.
Guess: both PASS 60 %. Most likely failure: the mouse device does not exist headless, so the poll site is never reached.
Self-test `python3 -I tools/s4c_input_selftest.py out <work dir>` (cdf81240): PASS 8/8 (run today).

## 5. What these runs cannot show
- Whether it LOOKS right, or feels right: no picture is judged and no mouse is moved. A real mouse in a real window
  (DirectInput acquired, cursor captured, buttons and wheel deferred to the tick) is first exercised on Caden's sheet.
- Part C shows at most that the per-frame mouse code runs without a crash with zero motion.
- Still at tick state: near-wall smoothing, animated script attachments on the hands, animation poses of world
  objects, attached lights and particles, marks on moving NPCs, culling, tracers, the player's own body and shadow.
- The shot origin reads a hands bone posed at tick time plus the fraction. Hit tests use tick positions.

## 6. Result
(none yet)
