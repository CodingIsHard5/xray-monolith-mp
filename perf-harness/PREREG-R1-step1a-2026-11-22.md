# R1 step 1a: main-scene share of submission - pre-registration (measurement only; Overseer OK 2026-11-22)

## What
Coarse14b.exe = Coarse14 (perf/render-split) + 4 PROF_EVENT sites in src/Layers/xrRenderPC_R4/r4_R_render.cpp (my
file, not on gammavr's list), added as wrappers only: r1a_static0, r1a_dynamic0 (Part 0, G-buffer), r1a_forward_a
(static(1)) and r1a_forward_b (dynamic(1) + fade + sorted). The forward pass's CParticlesAsync::Wait() stays outside
the zones, because it is a wait, not submission. The sun cascades are not in any zone. About 4 extra probe calls per frame.

## Runs (tools/r1a_order.txt; tools/r1_series.sh with R1_ORDER, WANT, fair share, settle, re-run once if not clean)
Rostok (bench1) x3, Cordon (cordon1) x3, same engine args as step 0. About 10-15 min of box.

## Computed (tools/r1a_zones.py): main = static0 + dynamic0 + forward, main-thread ms per frame, mean of 3 per scene.
Sanity: main <= step 0's (b) in every run, and frame within 5 % of step 0's K means (5.75 / 4.27). If either check
fails, the zones are wrong and I report that instead of a decision.

## Abort rule (registered with the Overseer)
main < 0.6 ms in ROSTOK -> R1 STOPS (the gain cannot clear overheads and noise; the large refactor is not worth it).
Otherwise the zone numbers pick the one-pass slice (Overseer's next step): the single biggest of static0 / dynamic0 /
forward_a / forward_b in Rostok. Its own gain is registered before any code; stop if under a quarter, plan the rest if
at least half.

## Desk prediction (before the build)
(b) without the sun is about 1.0 ms in Rostok. Inside it are, besides these passes: emissive, wallmarks, lights
accumulation, HUD, LODs, details, sky. Guess: main = 0.5-0.8 ms in Rostok (central 0.65), so near the 0.6 bar, a
coin toss. static0 the biggest single pass (0.25-0.4). Cordon main 0.3-0.6.
