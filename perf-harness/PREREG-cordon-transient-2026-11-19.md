# Pre-registration: is the Cordon hitch train a load transient? (2026-11-19)

Written before the run. Asked by the Overseer after captures cp1-cp4.

## What the existing captures already show (3000 measured frames after a 600-frame warmup, about 15 s)
In the four runs that show the train (cp1, cp3, cord1, cord2) the last A-Life-topped slow frame is measured frame 547-556,
3.5-3.7 s into the measured window. In the 2400 frames after frame 600 (about 11 s) there are 0, 1, 0, 0 frames of 20 ms or
more, and the one (cp3 frame 746, 23 ms) is not topped by A-Life. Those 11 s are too short to call it steady state.

## Run
One capture `cpl1`: build RefDet13, save cordon1, 600 warmup frames, 60,000 measured frames (about 5 min at ~5 ms),
`-perf_noinput -perf_nopresent -perf_slow 20 -perf_luatime`, headless Xvfb, EXCLUSIVE quiet claim. Stationary player.

## Definition and verdict
TRANSIENT if, after measured second 10, no frame of 20 ms or more has `ALife/scheduled/update` with 5 ms or more of its own
time, and the log has no `alife_slow` line of 5 ms or more after that point.
NOT TRANSIENT if there is one or more such frame. Then report how many, how often, and which objects.
Also reported either way: slow frames (>= 20 ms, any cause) per minute after second 10, with the top cost of each; mean and
1% low frame time for seconds 0-10 and for the rest.
If in this run the warm-up falls inside loading (as in cp2 and cp4), the verdict still stands for steady state; it is said so.

## Second purpose: which script function costs the ~300 ms
`-perf_luatime` (test-only script zz_eperf_luatime.script) times se_smart_terrain update/show/try_respawn, the squad update
functions and each handler of the smart/squad update callbacks, logging calls of 1 ms or more. Reported: the function(s) that
account for the 300 ms call and for the 2-5 ms squad calls. The wrappers add overhead, so frame times from this run are NOT
used as baseline numbers. Limit stated now: the wrappers may themselves shift whether the warm-up lands in loading.
