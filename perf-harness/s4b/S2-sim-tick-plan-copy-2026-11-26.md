# S2: fixed simulation tick, 30 against 60 Hz. Plan and stage list (2026-11-25)

Ordered by Caden (Discord 2026-11-25 17:08 UTC, via the Overseer): "Experiment with 60 and 30 fps and see which works
best for the stuff". No time estimates, by order. Nothing here is built yet.

## Caveats first
- **Feel cannot be judged on the box.** Stages 1 to 3 give costs. Which rate "works best" for aiming and movement
  is Caden's test sheet (stage 5), and that needs stage 4, the largest piece.
- **Without stage 4 a 30 Hz tick LOOKS like 30 fps**, whatever the render rate: X-Ray's update computes the camera,
  the weapon in the hands and every object's place in the same pass as the simulation.
- **Switch on is a script-visible change** (Rule Two): mods get one update per tick, not per frame. Switch off must be
  stock (Rule One), and that is proven first.
- **The plan said S2 should wait for the base-branch choice (A1).** This is a prototype on the dev-mt line; if the
  co-op branch becomes the base, the gate moves with it, but the per-frame/per-tick split of stage 4 would be redone.
- **Physics already has its own fixed step of 0.01 s.** Neither 30 nor 60 Hz is a whole number of those steps
  (33.3 ms = 3 or 4 steps, 16.7 ms = 1 or 2). 50 Hz (2 steps) and 25 Hz (4 steps) are. I add 50 Hz to the lever as
  a cheap third value and report how the steps line up; the question asked stays 30 against 60.

## What the code does today (read 2026-11-25, src/xrEngine/device.cpp)
- One loop: `on_idle` -> `FrameMove` (sets the time step, then runs every per-frame subscriber: the whole game
  update) -> render. One update per rendered frame.
- The time step is the real time since the last frame, lightly SMOOTHED (0.9 of the new frame time + 0.1 of the old step, device.cpp:796) and clamped to 0.1 s. So
  below 10 fps anything driven by the step does run slower. (A correction to "it never runs slower".)
- A fixed-step mode already exists: `rsConstantFPS` makes every frame a 33 ms step (the test-only det_mode uses it).
  It is a fixed tick TIED to the frame: one tick per frame, no more, no less.

## The smallest prototype that runs (stage 2)
A tick gate in `FrameMove`, behind a start switch `-sim_tick <hz>` and a console lever (0 = off, 25/30/50/60):
- real time is added to an accumulator; while a whole tick is due (at most a few per frame), the game update runs
  once with a fixed step of 1/hz; the frame is rendered every time, tick or no tick;
- a frame with no tick due draws the world as the last tick left it (no interpolation yet);
- off = the code path of today, untouched.
That is enough to measure every cost on the Overseer's list. It is not enough for feel.

## Stages
| # | Stage | Needs | Gives | Stops if |
|---|---|---|---|---|
| 0 | Code read (one Opus sub-worker, I verify): every per-frame subscriber sorted into "simulation" and "presentation"; who reads the time step (346 uses in 136 files); where camera, hands, input, sound, UI and particles get their time | desk | the split list for stages 2 and 4; the list of scripts' per-frame assumptions | - |
| 1 | E0, stock engine, NO new build: the existing frame cap (console `r__framelimit 30/50/60`, xr_ioc_cmd.cpp:1345; update = frame at the capped rate), bench scene, pre-registered. The cap busy-waits on the main thread (device.cpp:591-611), so main-thread time is taken from the update's own timers, not from CPU use | box, GO | cost per tick of main thread and scripts, scheduler and A-Life cadence, physics steps per tick, memory: the server-side answer (a server does not render, so its tick IS its loop) | the bench scene cannot hold 60 fps on this box even capped: then 60 is reported as "not reached" with the rate it did reach |
| 2 | Prototype: the tick gate above, built by CI. PROOF "off = stock": no change with the switch off by diff of the off path, plus the equivalence harness (exact A-Life counters and dice state) on off against the base build | CI; box, GO | the build for stage 3 | off differs from stock |
| 3 | The experiment, pre-registered (predictions, margin table, scorer self-test): 30 / 50 / 60 Hz and off, render uncapped, bench scene; then again on 4 cores (taskset: cheap) | box, GO | per tick and per frame main-thread ms; frame time p95/p99 and hitches; script ms per tick; physics steps per tick and their jitter; input-to-world delay (a stamped input, the tick that applies it, the frame that shows it); cost per player per second = cost per tick x rate | - |
| 4 | Presentation apart from the tick: mouse look, camera and the hands' model every frame; drawn objects placed between the last two ticks (interpolation) | CI; desk | a build Caden can judge | the split of stage 0 shows the hands or the camera cannot be moved out without touching mod scripts: then ask |
| 5 | Caden's test sheet: one save, one console command to switch 30 / 60 / off live, five things to try (aim at a walking NPC, strafe and turn, sprint through the bar, a firefight, a grenade), what to write down | Caden (about 11-27) | the verdict on feel | - |
| 6 | Report: which rate, with the costs of 1 to 3 and Caden's verdict; what S2 proper would need | desk | the decision | - |

## What I expect (to be registered properly before each run; rough now)
- A 30 Hz tick costs about half the simulation time per second of 60 Hz, less a part that does not scale: the object
  scheduler already limits each object to one update per 30 ms or more, so at 60 Hz most objects skip every second
  tick. Script cost per tick should be near equal at both rates, so per second 60 Hz costs about double there.
- With the gate and no thread split, frames alternate between cheap (no tick) and dear (a tick): p99 frame time is
  set by the tick, so 30 Hz with a fast renderer may show WORSE p99 than 60 Hz even though it costs less in all.
- Input delay: about half a tick more on average at 30 Hz (17 ms against 8 ms), before interpolation adds one tick.
- For a server, 30 Hz is the cheaper and likely sufficient rate; for a player's own view 60 Hz or per-frame
  presentation is needed. So the likely answer is "not one number": 30 for the world, the frame rate for the view.
  That is a guess to be tested, not a finding.

## Stage 0 result (2026-11-25): see plan-facts/frame-loop-read.md
What it changes in this plan:
- **The first prototype is "a tick = one old frame's update; a frame with no tick only renders again".** On a no-tick
  frame nothing in the per-frame list runs (not input either), only the frame counter moves so the renderer's
  per-frame guards work. That is the smallest change, and it makes the measured costs honest: tick cost and render
  cost are separate. Input is then read at the tick rate, so its delay is a real effect of the rate, not an artefact.
- **More than one tick in a frame needs the frame counter to move per tick**: objects refuse a second update in the
  same frame number (xr_object_list.cpp:105), and 94 other places use the frame number as "already done".
- **The gameplay half that runs on the game thread** (scheduler, physics, network, sound) belongs to the tick. On a
  frame with several ticks, all but the last run it inline; the last runs beside the render as today.
- **What gets simulated depends on what was drawn** (the "crow" list is filled by rendering and by distance to the
  camera, xr_object.cpp:392-405). Not touched by the prototype; it is the plan's "near any player replaces drawn".
- **Real-clock leaks inside the simulation** stay in the prototype and are named in the results: `timeServer()` reads
  the real timer (129 uses: weapons, grenades, conditions, hits), and the scheduler's batch budget is wall-clock.
  Under a tick they agree with tick time on average, and drift when ticks are dropped; dropped ticks are counted.
- **Stage 4 is now concrete:** mouse look is already applied on the input event; what must move out of the actor's
  update is `cam_Update` (Actor.cpp:1180) and the hands' model `g_player_hud->update` (Actor.cpp:1287-1292), plus
  the input poll and the presentation subscribers. Object positions are read live at render (GameObject.cpp:766), so
  interpolation means keeping the last two tick transforms per drawn object.

## Stage 4 design (the build Caden can judge), written 2026-11-25 before any code
What the code read fixes (Actor.cpp:1180 `cam_Update`, :1287-1292 the hands' model; ActorCameras.cpp:479-705):
- `cam_Update` is called from the player's update with the tick's step. It reads the player's position (`XFORM().c`),
  advances camera effects by the step, and writes the view into the device. It also derives values from the step
  (camera turn speed = change / step, ActorCameras.cpp:672).
- So the view cannot simply be "called again" on render-only frames: effects would advance twice on tick frames, and
  the turn speed would divide by a zero step on the others.

Plan, in two builds:
- **4a, the view every frame.** On every rendered frame in gated mode: poll the input (mouse look is applied on the
  event), then one presentation step for the player with the REAL frame time: `cam_Update` and the hands' model.
  Inside the tick, the player's update skips those two when the gate is on. The camera's position is placed between
  the player's last two tick positions (one tick of delay for movement, none for looking).
- **4b, drawn objects between ticks.** Every drawn moving object keeps its last two tick transforms; the renderer
  gets the in-between one. Because the game thread reads the same transforms while the render runs, in this mode the
  game thread's half runs before the render, not beside it (costs main-thread time: a prototype trade, measured).
- **Stays at tick rate in both builds, and is named on Caden's sheet:** character and weapon animation frames,
  particles, key presses (acted on at the next tick).
Each build goes through CI, an off = stock check, and a headless smoke run; neither can be judged for feel on the box.

## Stage 4a result (2026-11-25): built, off = stock PASS 4/4, view step PASS 10/10
Camera and hands every rendered frame at +1 to 2 % frame time (inside the spread). Not tested: the mouse path, smoothness.
See wt-render-split/perf-harness/PREREG-S2-4a-view-every-frame-2026-11-26.md.

## Stage 4b design (desk read 2026-11-25, sub-worker's read, the two driving facts checked by me in code)
Facts:
- A draw item keeps a POINTER to the object's matrix and reads it at draw time (r__dsgraph_types.h:78,
  r__dsgraph_render.cpp:105/223/397/428); CGameObject::renderable_Render passes &XFORM() (GameObject.cpp:766).
- The game thread's half (AI, bullets, scheduler) and the bones/IK thread read XFORM() while the render runs.
  So writing an in-between value into XFORM() and restoring it is not safe. The renderer must get another pointer.
- Every UpdateCL passes CObjectList::SingleUpdate on the main thread (xr_object_list.cpp:103-133): the one place to
  capture "previous and current".
- Animation poses advance on dwTimeGlobal only (SkeletonAnimated.cpp:639-654, SkeletonRigid.cpp:29): tick rate, and
  no recalculation on frames without a tick. Particles, rain, water/fire shader time, grass wind, sound: tick rate too.
- The old multiplayer interpolation is dead or yaw-only in single player; physics interpolates only inside its
  newest step. Neither is a base.
Design (option A, render-only transform):
- capture in SingleUpdate after UpdateCL, gated mode only: prev = cur, cur = XFORM(), tick id; no history after a
  missed tick, a new epoch, or a jump over 5 m (4a's rule);
- device publishes the frame's alpha and tick id (same alpha, same "one tick behind" as the 4a camera);
- draw: renderable_Render passes &m_render_xform (position lerp, rotation nlerp) when the pair is valid; children
  (held weapons, attachments) follow the root's delta;
- XFORM() is never written: nothing the simulation reads changes. Off: one branch not taken per place.
Slices: 1 root transforms + children; 2 the player's body and shadow; 3 tracers; 4 bone poses only if the tick-rate
pose looks bad. Then the frozen clocks that are visible (shader time, particles, rain) as their own list.
Not covered by slice 1: poses, attached lights and particles, tracers, culling/lighting position (small edge pops).
Values the simulation reads that 4a moved to frame rate (the Overseer's list item):
- fire origin: g_fireParams takes the HUD pick, built in the tick from the Device camera = the camera as last DRAWN.
  So a shot starts where the player saw the camera; the camera itself is up to one tick behind the body. To confirm
  by reading g_hud->OnFrame's pick build, and NPC-side hit tests use tick positions while the player sees in-between
  positions (up to one tick of target motion: at 30 Hz and 5 m/s, 17 cm).
- lean (r_torso.roll), aim-in factor, free-look, effector lifetimes: advance per frame with real dt; totals match,
  within-tick values differ. To decide per value: keep per frame (feel) or move back into the tick (determinism).
