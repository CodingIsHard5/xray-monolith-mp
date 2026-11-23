# R1 step 1 (D3D11 deferred contexts): desk findings, registered gain and checks. NOTHING BUILT.
# (2026-11-22 late, EnginePerf. Overseer conditions 1-3 of 182cdc686's acceptance; Caden's "Go for it" covers R1 only,
#  conditional on step 0 picking a target, which it did: submission.)

## 1. Scope after gammavr's answer
R_sun.cpp is NOT touched while gammavr's 8c runs go on (their choice, option 2). Step 1 covers only the main scene
passes in my files. Of step 0's submission figure (2.13 Rostok / 1.64 Cordon), the sun cascades take 1.13 / 0.49. So
the CEILING for step 1 is about 1.0 / 1.15 ms of main-thread time per frame (an upper bound: (b) includes other work).

## 2. DXVK (the rig): can the rig see a gain? YES, with a limit
Read from DXVK source (doitsujin/dxvk master, src/d3d11/; via a sub-agent, code quotes on file):
- Deferred context calls record on the CALLING thread into the context's own chunk list
  (d3d11_context_def.cpp: EmitCsChunk -> m_commandList->AddChunk, a plain push). Its LockContext() returns an empty
  lock (d3d11_context_def.h), so contexts share no mutex. Map(WRITE_DISCARD) allocates a slice from the context's own cache.
- ExecuteCommandList hands the recorded chunks to the immediate context's single CS thread, with no re-recording
  (d3d11_context_imm.cpp). Vulkan recording stays serial on that thread, as it is today for immediate calls, and it
  already runs off the game's main thread.
- DriverCommandLists is reported TRUE by default (d3d11_options.cpp: exposeDriverCommandLists default true).
So under DXVK the engine's own per-draw work plus DXVK's front-end can leave the main thread, and the rig measures that.
Costs to expect: state reset/restore on every Finish and Execute, and per-resource tracking at execute time.
Not shown on the rig: native Windows. A driver without command lists is emulated by the D3D runtime, which replays on
the immediate context, so only the engine-side share would be gained there. Testing that needs: one native Windows
run (Caden's PC on his return, same save and scenes, plus the driver's DriverCommandLists value logged at start),
and ideally the reference machine (choice 4). Until then, no claim about goal B's machine.

## 3. Engine side: the change is LARGE (Opus audit, spot-checked by me on disk)
- One global backend RCache (3,725 uses, 149 files). Making it per thread is easy (exported accessor macro; nothing
  takes &RCache). But CBackend has no context of its own: about 52 calls on the draw path go straight to HW.pContext,
  and the state, texture and sampler caches are separate global objects.
- Constant buffers are SHARED across shaders: reused by layout in _CreateConstantBuffer
  (dx10ResourceManager_Resources.cpp:881-888, checked), one CPU copy and one dirty flag each. set_Constants holds
  static arrays (dx10R_Backend_Runtime.h:549-555, checked). Two recording threads would race on both, so each context
  needs its own copies: a resource-system change.
- Dynamic streams: their buffers are cached in geometry at load time (skinned meshes, LODs), software skinning
  writes into them, and deferred maps must start with DISCARD.
- Per-frame globals touched while drawing: RImplementation.phase, o_hemi/o_sun, Device.mFullTransform (custom FOV),
  FTreeVisual statics, skeleton previous-frame matrices. Races here give silent image differences, not crashes.
  (A-Life and gameplay are not on this path.)
- Smallest first slice: record the opaque static pass static(0) on one worker while the main thread records
  dynamic(0), then execute in order. It still needs the per-context backend, state caches and constant buffers.

## 4. Registered expected gain (from step 0's numbers)
- Today's profile cannot separate the main scene's static(0) from the cascade calls: r_dsgraph_render_static totals
  0.92 / 0.42 ms over 11 / 5 calls, cascades included.
- Full step 1 (all main-scene passes): gain <= 1.0 / 1.15 ms of the K frame (5.75 / 4.27). Desk guess, after
  execute/reset overhead: 0.4-0.7 ms in Rostok, 7-12 %. First slice (static(0) only): <= 0.3-0.5 ms in Rostok,
  smaller in Cordon, possibly inside the 2.8 % noise there.
- Checks (from the proposal, unchanged):
  1. pixel-identical frame dumps on fixed camera paths;
  2. tier (a) exact counters equal to RefDet14;
  3. frame time, 3 repeats per scene: the gain above the 2.8 % spread, p99 no worse;
  4. the 4-core pinned run is not a loss;
  5. absence check: no per-frame-number cache, plus gammavr's stereo smoke run on the commit hash.

## 5. Recommendation before any build (for the Overseer)
Step 1a, measurement only, about 15 min of box: add three coarse zones in r4_R_render.cpp (my file, not on gammavr's
list) around Part 0 static(0), dynamic(0) and the forward pass, so the main scene's share is known.
Registered abort: if static(0) + dynamic(0) + forward < 0.6 ms in Rostok on K, R1 STOPS: the gain cannot clear
the overheads and the noise, and the large refactor is not worth it. Otherwise build the first slice, with the
checks above.
