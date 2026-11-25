# S2-C: why does a gated tick cost more than a stock update of the same step? DESK READ + DRAFT (not registered, nothing booted)

Ordered by the Overseer after part B (PREREG-S2-simtick section 12, finding 3): tick 30 costs 5.05 ms per tick against
3.88 for stock capped at 30 (+30 %); tick 60 3.59 against 3.06 (+17 %). Stage 4 (interpolation) is built on the gate,
so the cause comes first.

## 1. Desk read: what the part B data already says (no new run; profiler files of s2_t30_1, s2_t30_2, s2_c30, s2_f30_rr, s2_t60_1, s2_c60)
| question | data | answer |
|---|---|---|
| Does the tick do MORE update work (deferred work piling up for the tick frame)? | calls per tick of the update-side sites: script updates 124.7 gated against 126.6 capped; stalker updates 3.1 against 3.2; A-Life 1.2 against 1.2; every scheduler site 1.0 against 1.0 | **No.** The same number of updates run per tick |
| Is it the scheduler's wall-clock budget spending more when ticks are rarer? | the same counts; and both arms have 33 ms of real time between ticks | **No** |
| Is it the fixed step, or the gate's own code path? | "every frame a tick of 1/30 s" (same tick path, no render-only frames): 3.78 ms; stock fixed 33 ms: 3.74 | **No** |
| Where is the extra? | SELF time per tick, same call counts: scripts (CScriptBinder::shedule_Update) 2.21 gated against 1.66 capped and 1.70 every-frame (+33 %); stalker brain 0.55 against 0.43 and 0.44 (+30 %); smaller sites the same way. Both 30 Hz replicates | **The same calls run slower, by about the same share everywhere** |
| What else rises? | sites whose calls per tick rise 1.2 to 6 times: spatial frustum queries, bone/IK calculation, lock acquisitions. Their count follows the renders (7.2 a tick), their extra is about 1.0 ms per tick of worker time | render-side work, not the update; counted under the game thread only where it overlaps |
| Do scripts key work on the frame number? | `device().frame`: 0 uses in the scripts; real-time clocks: 31 uses of `time_continual` in 11 files | not by frame number; real time per tick is equal in the gated and capped arms |

So two of the three ideas in the order (deferred work piling up, the wall-clock budget) do not fit the data. What is
left is what differs ONLY in the gated arm: 6 render-only frames between two ticks, and a frame number that jumps
by 7 per tick.

## 2. Two hypotheses
- **H1, the machine:** the renders between ticks leave the CPU in a worse state for the update (data pushed out of
  the caches and TLB by the render and its worker threads, the update's worker threads moved to other work). The
  update does the same instructions, slower. Against it: every arm has ONE render between updates and pays nothing
  for it; H1 needs renders 2 to 7 to hurt where render 1 does not.
- **H2, the game:** something in the update does more work when the frame number has jumped (the engine has 94
  frame-number guards; "valid for this frame" caches miss every tick; work spread "every N frames" comes due on
  every tick), inside code the profiler does not split further (the script binder's Lua, the planner in the brain).
  The same calls, more instructions each.
`perf` (instructions against cycles would split them directly) is not usable here: perf_event_paranoid is 4.

## 3. Measurements that separate them
**E1, the existing build SimTick14.exe, no code:** tick 30 with the frame cap at 31, 60, 120 and none: 1, 2, 4 and
7.2 frames per tick. Cost per tick against frames per tick.
- H2 with "valid for this frame" logic predicts a STEP: 2 frames per tick already costs most of the +30 %.
- H1 predicts a RISE with the number of renders (and nothing at 1).
- Either way E1 says how much of the cost a frame cap would remove, which matters for old hardware.
**E2, one lever in the next build (`sim_tick_norender 1`, test only):** on a frame with no tick the frame number still
advances but NOTHING is rendered (the thread sleeps for the time a render-only frame takes). Frame numbers jump by 7
per tick, no renders in between.
- H1 predicts the cost falls back to the capped level (about 3.9 ms).
- H2 predicts it stays at the gated level (about 5.0 ms).
- In between: both, with the split read off the two ends.
E2 needs a build; it goes into the stage 4 branch as a test-only switch. E1 can run first on the pinned build.

## 4. To be written before any boot (registration)
Arms and capture lengths, predicted ranges for E1 under each hypothesis, margins from the replicate spread of part B
(30 Hz: 4.89 and 5.22 ms, so about 7 %; a difference under 10 % decides nothing), the per-run checks in outcome form
with all-lowercase tags, the flag preflight against part B's bench call, a phase test of scorer and check on real
run folders. Then the Overseer's GO.
