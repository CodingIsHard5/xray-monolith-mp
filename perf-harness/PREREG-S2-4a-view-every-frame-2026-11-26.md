# PREREG S2 stage 4a: the view every frame (registered 2026-11-26, before any run of the build)

Build: perf/sim-tick 9977d365d (CI green, run 45726047869), installed as run/bin/SimTick15.exe
sha256 2ffd4f2bbd386e0b6da35ce6ac9978610e07c4f7f6abc3d6005f74da6c3dd5ba. Previous build: SimTick14.exe 93fff4b5 (81e0b8fd2).
Stock reference for part A: Stance14.exe b4ce9c77. Nothing below has been run. No box run without the Overseer's GO.

## 1. What the build changes (gated mode only: sim_tick >= 10, not det_mode, not rsConstantFPS)
- Once per rendered frame, after the ticks: the actor's camera and hands update with the real frame time
  (`sim_view_present`, device.cpp), the camera placed between the player's last two tick positions.
- On a frame with no tick: mouse motion is read and delivered; buttons, wheel and keys wait for the next tick.
- In the tick, the actor skips its camera and hands update and stores its position instead.
- Switch off: the gate flag is set only on the gated branch, so the actor calls what it called. Desk read done; the
  run in section 2 is the test.
- One profiler site added on `CActor::cam_Update` (both paths): its call count is the outcome the checks read.
- Review: CodeRabbit, one finding (turn rate for weapon spread read up to one tick late): kept as a documented limit.

## 2. Part A: off = stock (the method of PREREG-S2-simtick section 9, copied, flags unchanged)
`tools/s4a_partA.sh` a472b9e1 = `tools/s2_partA2.sh` with the tags (s4a_s1..3, s4a_o1..3), the build (SimTick15) and
its hash changed, plus one added preflight: `sim_tick` and `r__framelimit` must be 0 in the settings file at start and
when the slice is granted. Same call: `DET_EXTRA="-det_passes 100,200,320 -det_lua_streams -det_counters"
tools/det_pair.sh 60 3 Stance14.exe:s4a_s1 SimTick15.exe:s4a_o1 ...`; same flag preflight against the 11-19 method
(run today: ok) and the same counter-file preflight on the first run.
Scored once: `python3 -I tools/s2_partA2_score.py out s4a_s1,s4a_s2,s4a_s3 s4a_o1,s4a_o2,s4a_o3 out/s4a_partA_score.txt`
(scorer 3e487ea9, unchanged). Checks R1 to R4 and their meaning exactly as section 9.4 there.
- R1 FAIL = harness not usable, no verdict. R1 PASS and R2, R3 or R4 FAIL = off is NOT stock: stop, part B is not run.
- Guess: all four PASS 75 %.
- Limit: this is the serial test mode and the first 320 A-Life passes; in that mode the gate is off by construction,
  so part A tests that the new code is inert when off. It says nothing about gated mode.

## 3. Part B: the view step happens, and what it costs (only after part A PASS)
`tools/s4a_run.sh` d6ebe0fc: the series script (75b2335b) and bench (072af8ed) of part B unchanged, on
`tools/s4a_order.txt` 2b6d4785: ten runs, mirrored, 12000 frames after 3000 warm-up, bench1, uncapped:
new build at tick 30 x2, tick 60 x2, off x1; previous build the same. All-lowercase tags. The series' own per-run
check (updates against frames, profiler counts) is unchanged and stops the slice on a failure; no in-slice fixes.
Scored once: `python3 -I tools/s4a_score.py out out/s4a_score.txt` (a55182b2).
| check | PASS when | guess |
|---|---|---|
| V1 view every frame | each of the four new gated runs: camera calls in N-2..N (N = frames), actor updates = updates, updates < N | 85 % |
| V2 off = stock call pattern | new build off: camera calls = actor updates = updates = frames | 95 % |
| V3 frame time | mean new / mean previous <= 1.05 at 30 and at 60 (arm spread > 8 % = no verdict for that rate) | 80 % each; I expect 1.00 to 1.04 |
| V4 tick cost | sim ms per tick (part B's sum) new / previous <= 1.05 at 30 and at 60 | 85 %; I expect 0.97 to 1.00 (the camera left the tick) |
| V5 off frame time | new off / previous off within 0.95..1.05 (one run each) | 90 % |
Also reported, no gate: camera ms per call (guess 0.03 to 0.10 ms), p99 frame time. All checks PASS: about 45 %.
A missing file, site or clean verdict is named and gives NO VERDICT; nothing missing is read as 0.
Self-test `python3 -I tools/s4a_selftest.py out <work dir>` (79fc1608): PASS 11/11. Fixtures are real part B run
folders under the names the bench gives; cases: good; camera only per tick; 50 frames short; twice per frame; off run
with an extra call; site missing; folder missing; run not clean; 10 % slower; one run 20 % slower; off 10 % slower.

### 3.1 Amendment, registered before any stage 4a run exists (2026-11-25 20:10 EST; no s4a folder in out/)
E1 showed the gap (PREREG-S2C section 6): the series script re-runs a not-clean run in the slice as `<tag>_rr`, and a
scorer with a fixed name list then gives no verdict. The Overseer's rule: the re-run policy and the scorer's names
come from one source. `tools/s4a_score.py` is now 3a77202c: a registered run whose conditions are not clean is read
from `<tag>_rr` when that folder exists, and the output names every substitution ("SUBSTITUTED ..."); a re-run that is
not clean either gives NO VERDICT by name. Nothing else changed. Self-test 3c8f8b6f: PASS 13/13 (two cases added).
Part A is not affected (its harness has no re-run rule).

## 4. What these runs cannot show
- The bench runs with input off (`-perf_noinput`): the mouse path (motion on frames with no tick, buttons kept for
  the next tick) is NOT exercised. Only a run with a real mouse does that.
- Smoothness is not measured. Whether 30 or 60 feels right is Caden's test, on the live switch.
- Other objects, the player's own body and vehicle cameras stay at tick positions (stage 4b).
- Values the simulation reads that now move per frame in gated mode (free-look, lean, aim-in progress, camera
  effect lifetimes, the camera used as fire origin) are not checked here.

## 5. Result
### 5.1 Part A (run 2026-11-25, ended 20:56 EST; six runs, both preflights ok; scored once): PASS 4 of 4
- R1 stock agrees with stock: C = 0 at all three checkpoints, both pairs.
- R2 switch-off (SimTick15) agrees with stock: C = 0 at all three checkpoints, all three runs.
- R3 frame at checkpoint 1: off mean minus stock mean -0.7 frames (limit 8); reported checkpoint 2 -3.3, 3 -1.7.
- R4 A-Life call spacing: 100 calls in every run, first call on the same frame, gaps 16/17/18 only.
My guess was 75 % for all four: it held. Output: `s4a/s4a_partA_score.txt`.
Limit, as registered: serial test mode, first 320 A-Life passes; it shows the new code is inert when off, nothing
about gated mode.

### 5.2 Part B
(empty until scored)
