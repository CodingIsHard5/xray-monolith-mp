# Pre-registration 2026-11-20 08:36: which side of control 1 was wrong? (fake-player generator, measurement only)
Approved by the Overseer 2026-11-20. Written and committed before the first run. Earlier series: control 1 FAILED as
registered (a fake player's script cost 0.715-0.755 ms at 10 fake players against a reference of 0.96 ms +/- 25 %).
Candidates: (a) the generator under-reproduces a player (handlers that throttle by time skip work on repeat calls);
(b) the 0.96 ms reference is not the cost of the player's own script update in this setup (it is the profiler's figure for
ALL script binders on the main thread, taken from another run).
Setup, same as the failed series: RefDet13, save bench1, headless Xvfb, EXCLUSIVE quiet claim per run, one run at a time,
`-perf_noinput -perf_nopresent`, 1,500 warmup + 2,000 measured frames. Tool: zz_eperf_fakeplayers.script with two new flags:
`-fp_time` (the REAL player's binder call is timed with the same timer as the repeat calls; updates after the 60th, so the
one-off first-update work is excluded from both) and `-fp_handlers` (every callback handler timed, split into "real call"
and "repeat calls").
Runs: t0 (`-fp_n 0 -fp_time`), t10a t10b (`-fp_n 10 -fp_mode full -fp_time`), h10 (`-fp_n 10 -fp_mode full -fp_handlers`).
Quantities: R0 = real call, ms per call, in t0. R10, F10 = real call and repeat call, ms per call, in the same t10 run.
DECISION RULE (fixed now):
- (b) holds if R0 is below 0.82 ms (more than 15 % under 0.96). (b) does not hold if R0 is 0.82-1.10.
- (a) holds if F10/R10 is below 0.75 in both t10a and t10b. (a) does not hold if it is 0.75-1.25 in both.
- Both, neither, or t10a and t10b disagreeing: reported exactly so.
SUCCESSOR CONTROL (replaces control 1 for any later series): the fake-player cost per call is within 25 % of the real call
measured in the same run (F/R between 0.75 and 1.25). Scored here on t10a and t10b; both must pass.
Handlers (h10; per-handler wrappers inflate times, so only ratios are read): a handler is named a THROTTLE if its real cost
is at least 0.005 ms per call and its repeat cost per call is below 50 % of its real cost. The list is reported with both
costs. Check on the method: sum of per-handler real times must not exceed the real call total of that run.
Also reported, no rule: R10 against R0 (does the real call itself get dearer with 10 fake players present).
Not in this series: the non-linearity (0.74 -> 1.02 ms per player from 10 to 100) stays open.
Any run not VERDICT clean is repeated once, then reported as confounded.
