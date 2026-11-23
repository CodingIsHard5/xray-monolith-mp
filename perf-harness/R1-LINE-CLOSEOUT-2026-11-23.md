# R1 line close-out: render feed threading (R1 + R1b), 2026-11-23

Overseer ruling on R1b-1 (e4e34410e): the 0.056 ms gain misses 1b-2's 0.06 gate, frame time shows no gain, and check 4
(4-core) fails with ON worse. 1b-2 is not approved and R1b STOPS. r__mt_sun_static stays default 0, on perf/r1b-sun only.
gammavr will NOT cherry-pick it.

## What the line found
1. The render feed is spread thin. No single block is big enough to pay for moving it off the main thread.
   - R1 (render-split) stopped on its registered abort: the main scene's list building was 0.587 of what it needed,
     below the 0.6 bar (R1-CLOSEOUT-2026-11-22.md).
   - R1b-1 moved the sun cascades' static list building (0.243 ms per frame in Rostok) to worker tasks. Main still
     waits 0.133 ms at the join for the largest cascade, and the net gain was 0.056 ms, about 1 % of the frame.
     Frame time did not change.
   - My predictions were too high both times (R1b-1: central 0.12, measured 0.056).
2. The self-test with its witness worked, and it is the pattern to reuse. Each frame builds the lists both ways and
   compares ordered hashes. A witness switch breaks one item on purpose, and the test must catch exactly that.
   This proved "nothing changes" (Rule One) for a threading change without deterministic replays: 0 mismatches in
   5,400 compares per scene and per VR eye, and the witness caught 1,800 of 1,800 renders on the right cascade only.
3. The 4-core result is the warning for any threading on low-end machines. Pinned to 4 cores, the same change made the
   main thread 0.12 ms SLOWER and the frame 0.16 ms slower. Worker tasks compete with the engine's other threads.
   Caden's goal includes old hardware, so any future threading change must pass a 4-core check before it can be on
   by default.

## Lessons for my own process
- Before registering a check, confirm it can run on the target branch. -det_mode and the frame dumps could not, and
  both needed amendments.
- Before using a log comparison as a check, confirm it is stable between runs of the same arm. GAMMA's random loot
  spawns made the Cordon comparison uninformative.

## State left behind
- perf/r1b-sun (515e6d043, 4367f4081): kept as a record, default-off, not merged anywhere.
- run/bin/R1b14.exe stays in the project's run dir. run/appdata/user.ltx is left with every R1b switch at 0.
- Open note: the static lists hold leftover items before the cascade traverse (about 119 per render in Rostok).
  Unexplained and harmless to the result. Look at it only if this area is reopened.
- Latent bug for BUGFIXES: r__sector.cpp:191 operator precedence in a debug-only condition (not fixed).
