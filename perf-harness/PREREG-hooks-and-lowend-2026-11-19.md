# Pre-registration 2026-11-19: (1) player-update cost by script hook, (2) constrained "low-end" bench
Both are measurements only (plan stage 0 / S0, B1). Written before the runs. All runs: EXCLUSIVE quiet claim, headless Xvfb,
build RefDet13, 600 warmup frames + 3000 measured, `-perf_noinput -perf_nopresent`, stationary player.

## 1. What makes up the player's script update (0.96 ms per frame in the baseline)
Runs hk1, hk2: save bench1 (Rostok bar), extra flag `-perf_luahooks` (test-only script zz_eperf_luahooks.script: times every
script callback handler and the actor binder's update; totals in lua_hooks.csv).
Reported: per handler of `actor_on_update`: script file:line, calls, ms per player update, share; the same for the other
callbacks that fire every frame; the part of the binder update that is NOT in callbacks (binder total minus callbacks).
Checks on the measurement itself:
- the wrappers add cost, so absolute ms are inflated; the run's own profiler line `CScriptBinder::shedule_Update` (main thread)
  is compared with the baseline's 0.96 ms per frame and the inflation is stated;
- hk1 and hk2 must agree: the top 10 handlers by share must be the same set, each share within 20 % (relative) of the other
  run; otherwise the ranking is reported as unstable;
- sum of handler times must not exceed the binder total (if it does, the method is wrong and nothing is reported from it).
Limit: one place (the bar), calm, no combat, no UI open, no input. Hooks that only work when the player moves, fights or opens
the inventory are under-counted.

## 2. Constrained bench: "ranks causes, gives no verdict"
Constraint set (no root): engine pinned to 4 physical cores (taskset 0-3), memory limit 8 GB without swap (systemd-run
--user scope), DXVK told the video card has 2,048 MB. NOT imitated: slow cores (frequency cap needs root), a slow GPU, a
hard disk, Windows with native D3D11. So frame times from these runs are NOT what the reference machine would show; only the
ORDER of costs under fewer cores and less memory, and whether memory fits, are read from them.
Runs: lr1 lr2 lr3 (bench1, Rostok) and lc1 lc2 lc3 (cordon1) with BENCH_LOWEND=1; ur1 and uc1 without (fresh unconstrained
references on the same build; the older base1-3 / cord1-3 runs are used only as a cross-check).
Method checks, per constrained run (a constraint that cannot be shown to be in force is VOID and said so):
- cores: the engine process's allowed-CPU list is 0-3 (logged in conditions.txt);
- memory: the engine's cgroup is the systemd-run scope (logged); peak resident memory is reported against 8 GB;
- video memory: the DXVK log reports the 2,048 MB limit.
Reported: mean frame time, 1 % low, count of frames over 20 ms and over 100 ms, top costs by system on the main thread and
on workers, each against the unconstrained run; peak resident memory; whether the run completed (an out-of-memory kill is a result).
Spread: the three runs' means must be within 10 % of each other, otherwise the numbers are marked unstable.
What counts as a finding: a system whose share of the main-thread frame grows by more than a quarter under the constraint,
or worker work that visibly delays the main thread (the per-frame join wait grows). No fix is proposed from this alone.
