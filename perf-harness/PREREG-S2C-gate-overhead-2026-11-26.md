# S2-C: why does a gated tick cost more than a stock update of the same step? Desk read (sections 1-4) and E1 REGISTERED (section 5, 2026-11-26, before any boot)

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
`perf` (instructions against cycles would split them directly) is not usable here: perf_event_paranoid is 4. That is a
kernel security setting and Caden's call (Overseer); nothing here plans around it.

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

## 5. E1, REGISTERED (Overseer: E1 first, no build; E2 only if E1 does not separate, registered apart)
**Caveats first.** (1) One box, one save, headless; costs, not feel. (2) E1 can separate "a step at the first
render-only frame" from "a rise with the number of renders"; it cannot say WHICH logic or WHICH cache. (3) H1 and H2
as written are not the only readings of a step or a rise: a step could also be a machine effect that saturates at
one extra render. The verdict words name the shape; section 2 says what each shape favours. (4) At cap 31 two
30 Hz clocks beat against each other (a frame now and then has no tick); that arm is the "one frame per tick" control.

**Runs (tools/s2c_order.txt, ten runs, mirrored, one EXCLUSIVE slice):** stock capped 30; tick 30 with the frame cap
at 31, 60, 120 and none: about 1, 2, 4 and 7.2 frames per tick. Two runs of each. Build SimTick14.exe 93fff4b5 (the
part B build). Captures of about 55 to 60 s (1,800 / 1,700 / 3,300 / 6,600 / 12,000 frames).

**Measure (tools/s2c_score.py, scored once):** sim ms per tick as in part B (main-thread update plus the game thread's
three sites, per update). E(x) = sim(x) / sim(capped 30) - 1. F(x) = E(x) / E(uncapped).
| verdict | when | what it favours |
|---|---|---|
| NO VERDICT | a run not valid (named), or an arm's two runs over 12 % apart | nothing |
| EFFECT NOT REPRODUCED | E(uncapped) under 15 % (part B: 30 %) | part B's finding itself is in doubt |
| GATE PATH OR BEAT | E(cap 31) over 10 % | the gate's own path or the beat costs: neither H1 nor H2 as written |
| H2 STEP | F(cap 60) at least 0.70: two frames per tick already give most of the extra | the frame-number jump (or a machine effect that saturates at once) |
| H1 RISE | F(cap 60) at most 0.35 and F(60) <= F(120) <= 1.10 | the amount of rendering between ticks |
| NOT SEPARATED | anything else | E2 |
**Predicted under each hypothesis (extra over capped 30, sim per tick):**
| arm | frames per tick | H2 (step) | H1 (rise, linear in the renders between) | margin |
|---|---|---|---|---|
| cap 31 | 1.03 | 0 to 3 % | 0 to 3 % | verdict line at 10 % |
| cap 60 | 2 | 21 to 30 % (F 0.7 to 1) | 3 to 8 % (F 0.1 to 0.27) | lines at F 0.70 and 0.35; the two predictions are 0.4 apart |
| cap 120 | 4 | 27 to 32 % | 10 to 18 % (F 0.35 to 0.6) | |
| none | 7.2 | 25 to 35 % | 25 to 35 % | part B: 30.2 % (4.89 and 5.22 against 3.88) |
Noise: part B's two 30 Hz runs were 7 % apart in sim per tick; the capped arms about 1 %. An extra of 30 % with
7 % run noise puts about 0.15 of uncertainty on F: the 0.35 and 0.70 lines are two of those apart from each other.
**My guess:** H1 RISE 45 %, H2 STEP 25 %, NOT SEPARATED 20 %, the rest 10 %.

**Checks and preflights (the four errors of parts A and B, each closed here by construction):**
| past error | here |
|---|---|
| a call that was not the known method | the series script and the bench are part B's files, unchanged and hash-checked by the driver (s2_series_b.sh 75b2335b, s2_bench.sh 072af8ed); only the order file is new |
| a check on a log line another phase prints | the scorer reads only the profiler's capture files and conditions.txt; the per-run check is part B's amended one (outcome counts) |
| a path built from a tag the bench renames | all ten tags are lowercase; the driver refuses any that is not; missing files fail by name (self-test) |
| fixtures that were not real bytes | self-test `tools/s2c_selftest.py out <work>`: real part B folders under the E1 tags; six shapes give the six verdicts, six broken runs give NO VERDICT by name: PASS 12 of 12 |
- New and never run: `sim_tick` and `r__framelimit` set together. Desk: the limiter's wait runs on every frame, also
  render-only ones (device.cpp 758-776); the per-run check passes a real gated folder under such a setting and fails
  it when either switch is not as asked. If a gated run does not hold 30 updates a second under a cap, the scorer
  calls the run invalid by name.
- Any failed run or check stops the series (part B's rule); a not-clean run is excluded and run again once.

**Pinned (sha256, first 16):** s2c_order.txt dfa2f0db6dee7631; s2c_score.py 803b517d7f283303; s2c_selftest.py
0ac0fc7897f44fc4; driver s2c_run.sh a2e218c0d93d07cc; s2_series_b.sh 75b2335b52bc662f; s2_bench.sh 072af8edb67eab09;
SimTick14.exe 93fff4b537711cb38062f432c29551642e36ab852703b191727edb337e858f21. Copies: perf-harness/s2c/.
Run: `tools/s2c_run.sh` (background). Score once: `python3 -I tools/s2c_score.py out out/s2c_score.txt`. Result: section 6.
