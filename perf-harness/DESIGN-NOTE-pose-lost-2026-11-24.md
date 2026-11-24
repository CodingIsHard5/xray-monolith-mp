# Design note: X-Ray notices that ITR2 stopped writing the pose ("lost")

Status: DESIGN NOTE, desk only (Overseer's order, 2026-11-24, after R3 x1's first run). Nothing built. The x1 retry
files are not touched; this lands in the next registration.

## What happened

- In R3 x1 ITR2 crashed at its arrival on Garbage. Its last pose said `cross`. X-Ray then ran about 390 s with
  A-Life going on around an actor that nobody moved, and nothing reacted.
- The script wrote one `STALE` line after 1 s without a new pose. That line is a log only; nothing acts on it.

## What exists today (read from the frozen files)

- **Script (zz_p1_puppet.script):** a pose is new only if its seq is higher than the last one. After 1,000 ms of
  game time without a new seq it logs `STALE` once; on the next new seq it logs `RESUME`. With no new pose the
  actor is simply not moved. God mode stays on.
- **Runner (t2_xray.sh):** it freezes X-Ray (SIGSTOP) only while the pose FILE says dead or away with `freeze`. A
  stale file keeps whatever state it had last. While the file says `cross` the runner's own "no new snapshot"
  rule is switched off, with no time limit.
- **Two gaps found while reading:**
  1. A crash during `cross` is never noticed by either side (this run).
  2. If ITR2 restarts and its seq starts again at 1, the script rejects every pose (seq not higher than the last),
     so X-Ray would stay stale for good. ITR2's restart behaviour has to be asked of ITRZone before this is built.

## Proposal

- **New state on the X side: LOST.** The pose is LOST when no new complete pose has arrived for longer than a
  limit. LOST is treated exactly like the pose state `away`, with the keep or freeze policy of the last good pose.
- **Limits (proposed):**
  - 5 s when the last pose said play, dead or away. ITR2 writes at 20 poses a second, so 5 s is 100 missed poses.
    The largest gap between two poses in T3 proper and x1 is to be read from the data before the limit is
    registered; the limit must be at least three times that.
  - 120 s when the last pose said `cross`. A crossing took 48 to 52 s so far; ITRZone's own limit is 60 s.
  - Never after a pose that said `end`.
- **Who measures:** both, each with its own clock.
  - The script measures game time since the last new seq (the STALE rule today, with a second step) and writes
    `LOST,<track s>,<game ms>,<last seq>,<age ms>,<last state>,<policy>` once. It does what it does for `away`:
    no pose applied, protection stays on, and under freeze it arms the absorb before the stop.
  - The runner measures wall time since the pose file's seq last changed. Under freeze it stops X-Ray the same
    way as for away + freeze (wait for the arm line, SIGSTOP, row in freeze.csv with reason `lost`). Under keep it
    only logs.
  - The runner ends a `cross` hold after 120 s, so its "no new snapshot" rule works again.
- **Coming back:** the first new complete pose ends LOST: the runner sends SIGCONT, the script logs `RESUME`.
  The 5 m guard does not apply to the first pose after LOST (the player may be anywhere), as after a crossing.
- **What LOST does under each policy:**
  - keep: the actor stays where it was, A-Life runs on. This is what happened in x1; the change is that it is
    now a named, logged state with a limit, not an accident.
  - freeze: X-Ray is stopped until ITR2 writes again or the run ends. No A-Life time passes.
- **Rule One:** no change while poses arrive. LOST only adds a path for a case that today has no handling.
- **Player-visible, for Caden via the Overseer if not already covered by his keep/freeze choice:** how long X-Ray
  may stay frozen waiting for a crashed ITR2 (for ever, or stop and save after some minutes).

## Witness (X only, my own pose writer; pre-flight, registered before it runs)

Three boots, each about 4 minutes:
1. **keep, kill during play.** The writer is killed (SIGKILL) at track 60 s and started again 30 s later.
   Expected: `STALE` within 1 to 1.5 s, one `LOST` at 5 s (within 0.5 s), no pose applied between LOST and RESUME,
   actor position constant, vitals clean, no STOP row, `RESUME` and at least 400 poses after it, rc 0.
2. **freeze, kill during play.** Same kill. Expected: `LOST`, then one STOP row with reason `lost` within 2 s of
   it, one CONT row after the writer is back, game time across the stop at most 1,000 ms, one "[FREEZE] absorbed"
   engine line, then poses again.
3. **kill during `cross`.** The writer is killed right after its `cross` pose and not restarted. Expected: the
   level change still completes, no LOST before 120 s, one `LOST` at 120 s (within 2 s), the runner's cross hold
   ends, rc 0.
- **Negative control in boot 1:** before the kill the writer pauses 3 s (under the limit). Expected: `STALE` and
  `RESUME`, no `LOST`.

## Mutation

- A fourth boot as boot 2 but with the limit switched off (the new code present, its limit set to "never").
  The checker MUST FAIL it: no `LOST` line and no STOP row while the writer is dead. This shows the checker
  cannot pass on a run where nothing noticed.

## Not covered by this note

- ITR2 restarting with seq back at 1 (gap 2 above); it needs ITRZone's answer first.
- What ITR2 does when X-Ray is the one that stops writing snapshots; that is ITRZone's side.
