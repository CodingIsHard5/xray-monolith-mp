# PREREG S2 stage 4b slice 1: objects drawn between their last two ticks (registered 2026-11-26, before any run)

Build: perf/sim-tick b24960eff (CI green, run 45753185664), installed as run/bin/SimTick16.exe
sha256 91a8c0df50f61bbbb0d4f1a6b0737057016492675e58a4701d737a419b5f8cd5. Previous build: SimTick15.exe 2ffd4f2b (stage 4a).
Stock reference for part A: Stance14.exe b4ce9c77. Nothing below has been run. No box run without the Overseer's GO.
Design and the desk read behind it: `s4b/S2-sim-tick-plan-copy-2026-11-26.md` (section "Stage 4b design").

## 1. What the build changes (gated mode only)
- In the tick, after each object's UpdateCL: its previous and current transform are stored (CObjectList::SingleUpdate).
- At draw: the renderer is handed a per-object render-only matrix between the two (position lerp, rotation slerp), with
  the same alpha as the 4a camera. Children (held weapons, attachments) follow the parent's drawn matrix.
- XFORM(), the matrix the simulation reads, is never written for rendering.
- Stock path kept on purpose: the local player's body, the vehicle or mounted gun the player is in, HUD and UI passes.
- Switch off: the frame flag is never set, the capture does not run, each draw site hands over &XFORM() as before.
  One difference that is not a branch: every object is about 220 bytes larger, so heap addresses differ from stock
  even when off. Part A is the test of whether that matters.
- Review: CodeRabbit, two questions. Flag cleared before the worker join: moved after the join. Transforms written
  after the capture in the same tick: kept as a documented limit (drawn up to one tick late).

## 2. Part A: off = stock (method of PREREG-S2-simtick section 9, as in stage 4a; flags unchanged)
`tools/s4b_partA.sh` b3313684 = `tools/s4a_partA.sh` with tags s4b_s1..3 / s4b_o1..3, build SimTick16 and its hash.
Flag preflight run today: ok. Scored once:
`python3 -I tools/s2_partA2_score.py out s4b_s1,s4b_s2,s4b_s3 s4b_o1,s4b_o2,s4b_o3 out/s4b_partA_score.txt` (3e487ea9).
R1 FAIL = harness not usable, no verdict. R1 PASS and R2, R3 or R4 FAIL = off is NOT stock: stop, no part B.
Guess: all four PASS 65 % (lower than 4a's 75 % because of the object size change).
Limit: serial test mode, first 320 A-Life passes; shows the new code is inert when off, nothing about gated mode.

## 3. Part B: objects are captured and drawn in between, and what it costs (only after part A PASS)
`tools/s4b_run.sh` 12981688: series script 75b2335b and bench 072af8ed unchanged, on `tools/s4b_order.txt` e1789ccc:
ten runs, mirrored, 12000 frames after 3000 warm-up, bench1, uncapped: new build at tick 30 x2, tick 60 x2, off x1;
previous build (4a) the same. Lowercase tags. The series' own per-run check stops the slice on a failure; no in-slice
fixes. Scored once: `python3 -I tools/s4b_score.py out out/s4b_score.txt` (7efb142b). The scorer reads `<tag>_rr` when
`<tag>` is not clean (the series' re-run rule) and names each substitution.
| check | PASS when | guess |
|---|---|---|
| W1 captured and drawn in between | each of the four new gated runs: capture calls >= updates, in-between draws > 0 | 70 % |
| W2 off | new build off: no capture, no in-between draw | 97 % |
| V1 view every frame (4a, kept) | new gated runs: camera calls in N-2..N, actor updates = updates | 95 % |
| V2 off call pattern (4a, kept) | new build off: camera = actor = updates = frames | 97 % |
| V3 frame time | mean new / mean previous <= 1.05 at 30 and at 60 (arm spread > 8 % = no verdict for that rate) | 80 % each; I expect 1.00 to 1.03 |
| V4 tick cost | sim ms per tick new / previous <= 1.05 at 30 and at 60 | 80 %; I expect 1.00 to 1.03 (the capture is in the tick) |
| V5 off frame time | new off / previous off within 0.95..1.05 (one run each) | 90 % |
All PASS: about 40 %.
W1's weak point, stated now: "in-between draws > 0" needs a MOVING object in view in the bench1 scene during the
capture. I have not checked that one is there. A zero is a FAIL as registered, and it would not tell a broken draw path
from an empty scene; captures per tick (reported) would then be the only evidence, and I would say so.
Also reported, no gate: captures per tick, in-between draws per frame, cost per in-between draw, p99 frame time.
In the off run the two new sites are expected to be ABSENT from the profiler file (a site never entered is not listed):
absent = 0 for these two sites in that one run only; everywhere else a missing site is NO VERDICT by name.
Self-test `python3 -I tools/s4b_selftest.py out <work dir>` (352f3385): PASS 18/18 on real part B run folders; the 4a
cases plus: no in-between draw in one run, capture site missing in a gated run, fewer captures than ticks, off run
captures, off run draws in between.

## 4. What these runs cannot show
- Whether it LOOKS right: no picture is judged. Held items on moving NPCs may be off by one tick on tick frames;
  only eyes can tell. That, and smoothness, is Caden's sheet.
- Still at tick state in this slice: animation poses, lights and particles attached to moving objects, blood and
  bullet marks on moving NPCs, culling, tracers, the player's own body and shadow.
- The mouse path is still not exercised (bench input off).
- Hit tests use tick positions while the player sees in-between positions (up to one tick of target motion).

## 5. Result
(empty until scored)
