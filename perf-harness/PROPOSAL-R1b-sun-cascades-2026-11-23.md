# R1b: sun cascades off the main thread (DESK PROPOSAL, 2026-11-23; nothing built or run)

Order: Overseer, 2026-11-23. gammavr's 8c is closing, which was the condition on R_sun.cpp. This is still R1 (the
render feed) under Caden's "Go for it" for R1. No box until the Overseer rules. gammavr-bd is told before anything
is built.

## 1. The cascades' own share, from step 0 / 1a data already on disk (no new run)
The K0 reference runs of step 1a (Coarse14, 3 runs per scene, clean) carry the existing sub-zones in R_sun.cpp.
Main thread, ms per frame, means (run-to-run spread under 0.01 ms):

| zone (3 cascades) | Rostok | Cordon | what it is |
|---|---|---|---|
| Render Cascade (total) | 1.131 | 0.480 | |
| SMAP traverse | 0.344 | 0.073 | list building: GMCascade.traverse + r_dsgraph_capture |
| SMAP Finalize | 0.754 | 0.376 | submission: phase_smap_direct + r_dsgraph_render_graph/sorted |
| Accumulate | 0.027 | 0.026 | phase_accumulator |
| Prepass | 0.005 | 0.005 | frustum and transforms |

Liftable (traverse plus finalize) = 1.10 ms in Rostok (19 % of the 5.73 ms frame) and 0.45 ms in Cordon (10 %).
Rostok frame 5.73, submission (b) 2.13: the cascades are about half of all submission.
For GammaVR both eyes run the full cascade set (gammavr's condition), so in VR this share doubles per frame.

## 2. Abort bars (registered as for 1a)
- Bar 0, the share: liftable >= 0.6 ms in Rostok. PASSED from existing data (1.10). No measuring run needed.
- Every later slice uses the Overseer's slice rule: stop if the measured main-thread gain is < 1/4 of that slice's
  predicted share; plan the next slice only if it is >= 1/2.

## 3. Design: two slices, smallest first
Starting point (source): each cascade already has its OWN CDSGraphManager (r_sun_cascades.h:38, cascade.GMCascade),
so the three cascades' render lists are already separate objects. Cascades are rendered one after another from
render_sun_cascades() (R_sun.cpp:67-79), called once from r4_R_render.cpp:286.

### Slice 1b-1: build the three cascades' lists on worker tasks (no D3D work leaves the main thread)
- Prepass (transforms and frustums) stays on main for all three cascades, as now. Then the three
  traverse + r_dsgraph_capture calls run as three worker tasks. Main waits for them, then submits cascades 0, 1, 2
  serially exactly as today. The draw order is identical; only WHEN the lists are built changes.
- Overlap: to gain more than the parallel speed-up, the tasks are started at the top of the frame's render. Main
  runs its main-scene passes meanwhile, and waits only at the point where the cascades are submitted today. That needs
  the cascade frustums before the main scene, and they depend only on the camera and the sun direction.
- Shared state to make safe, all to be checked at the desk before building (verified so far: separate graph managers):
  the global `phase` (set to PHASE_SMAP before traverse; it must become per-task or be read only at submit), the
  visual "marker" counters written during capture, `fuckingsun->X.D` (written per cascade in Prepass and Finalize:
  stays on main), the PortalTraverser debug statics (r__dsgraph_manager.cpp:11-12), the skeleton/tree statics touched
  at capture, and HOM (cascades use their own frustum culling; to confirm they do not touch HOM).
- Predicted gain: Rostok 0.15-0.30 ms of main-thread time (central 0.22; the ceiling is 0.344); Cordon 0.03-0.07.
  This is small alone, but it proves the overlap and costs little code.
- Code size: about 150-300 lines. R_sun.cpp (gammavr's) about 60-120; r__dsgraph_build/manager (mine) about 80-150;
  r4_R_render.cpp (mine; HOM.cpp and DetailManager.cpp untouched) about 10-30.

### Slice 1b-2: record the cascades' shadow draws on a deferred context (only if 1b-1 passes its 1/2 bar)
- One worker records cascades 0-2 into a D3D11 deferred context. Main calls ExecuteCommandList for each, in cascade
  order, at today's submit point. Under DXVK, recording runs on the worker and ExecuteCommandList hands the chunks to
  the CS thread with no re-record (step 1 desk).
- This needs the step-1 obstacles solved for the shadow path only: a second CBackend bound to the deferred context
  (RCache is global: 3,725 uses, so RCache becomes a per-thread reference, e.g. a thread-local pointer behind the same
  name), per-context constant-buffer copies (shared today: dx10ResourceManager_Resources.cpp:881-888), the static arrays
  in set_Constants (dx10R_Backend_Runtime.h:549-555), and per-context state, sampler and texture caches. The shadow
  pass uses a narrow state set (depth-only and alpha-test shaders), which is why it is the right first user.
- Predicted gain on top of 1b-1: Rostok 0.35-0.6 ms (ceiling 0.754 minus record overhead and ExecuteCommandList cost);
  Cordon 0.15-0.3. On native Windows without driver command lists the runtime replays on the immediate context, so
  only part of this would show there (a native run is needed to know).
- Code size: about 800-1,500 lines, mostly xrRenderDX10 backend (mine), plus about 50-100 in R_sun.cpp. A LARGE change;
  it gets its own prereg and ruling.

R1b total predicted: Rostok 0.5-0.9 ms (9-16 %), central 0.7; Cordon 0.2-0.4. Not measured; a desk guess from
the shares above.

## 4. Checks (registered with each slice's prereg, as for R1)
1. Image equality: frame dumps on fixed camera paths (Rostok, Cordon, plus one with the sun low), before and after,
   pixel-identical. No tolerance unless registered in advance with a reason. Shadow maps included (dumped as images).
2. Tier (a): exact counters under -det_mode equal to RefDet14, showing the change did not reach the simulation.
3. Frame time: Rostok and Cordon, 3 repeats each against fresh references run in between. The gain must exceed the
   repeat spread; p99 must not get worse.
4. A 4-core pinned repeat: the gain must not turn into a loss when worker tasks compete with the main thread.
5. Absence: no new per-frame-number cache (code search); the cascade order and per-eye state unchanged; both eyes run the
   full cascade set. One stereo smoke run, run by gammavr-bd on my commit hashes.
Measurement exe as before: AGG profiler with the hot probes compiled out (Coarse), plus the 1a-style zones.

## 5. gammavr's conditions (from their 2026-11-22 message, unchanged)
(a) gammavr gets the R_sun.cpp diff BEFORE I commit it. (b) It stays on my perf/* branch; gammavr cherry-picks it onto
vr-spike-mt and runs their regressions. (c) The cascade order and per-eye state must not change, both eyes run the full
cascade set, and nothing is cached per frame number. Plus: Render->Calculate() + Render() stay callable twice per frame
with different cameras.
Files: R_sun.cpp and r_sun_cascades.h are gammavr's, edited only under (a)-(c). Mine: r__dsgraph_*, the xrRenderDX10
backend, r4_R_render.cpp (only at the cascade call). Not touched: player_hud.cpp, Level.cpp, UIMapWnd.cpp, HOM.cpp,
DetailManager.cpp, SkeletonCustom.cpp, the rendertarget phase files, the vr files.

## 6. Proposed next step (needs the Overseer's OK)
A desk audit of the shared state listed for 1b-1 (section 3), then the 1b-1 prereg (gain, the checks, the code
size), the diff to gammavr, a build, and then a request for box time.

## Overseer ruling and gammavr's answers (2026-11-23)
Overseer: approved in this order: desk audit, 1b-1 prereg, diff to gammavr, build, then a box request. 1b-1 waits for
gammavr's answers. Any objection goes to the Overseer, not worked around. The 1b-2 stop rule (>= 1/2 of 1b-1's
prediction) goes into 1b-1's prereg.
gammavr (no objection; constraints):
1. 8c is CLOSED. R_sun.cpp is free for my diff after the Overseer's ruling on 1b-1, under (a)-(c). Their 7c regression set
   is running (about 2 h); a diff sent to them does not affect it, but they cherry-pick nothing until it is scored.
2. The diff is BASED ON mp/vr-spike-mt's R_sun.cpp (bad71b1de, df0180a69: +90/-36 against 1cf2b14a7), not upstream's.
   Those commits add r_stereo_bench 2: eye 0 renders each cascade and saves its depth map; eye 1 skips the cascade render
   and its prepass and copies the saved map back. In mode 2, eye 1 must NOT start the early traverse tasks (or must
   discard them). Mode 2 is a bench mode (the headset uses 0), but it must keep working.
3. Cascades run per Render() call: in VR, or with r_stereo_bench >= 1, the whole pipeline runs once per eye with that
   eye's camera. So the early tasks BELONG TO ONE Render() call and are joined inside it. Nothing carries over from
   eye 0 to eye 1 except mode 2's explicit reuse.
