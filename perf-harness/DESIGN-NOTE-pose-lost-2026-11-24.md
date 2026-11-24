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
    Checked against the data (2026-11-24): the largest gap between two applied poses during play was 408 ms in
    T3 proper (21,343 poses; median 50 ms, 99th percentile 66 ms) and 95 ms in x1's first run (952 poses). So 5 s
    is 12 times the largest real gap. T3's two 80 s gaps are its two deaths, where ITR2 kept writing poses and
    the script applied none; they are not gaps in the file.
    ITRZone's own measure of the file (2026-11-24): it writes in every state from its frame loop; its longest
    gap between two writes was 1,188 ms in T3 proper (state dead, during its load to the Hub) and 350 ms in x1
    (in cross). So 5 s is about 4 times the longest real gap in the file. Not measured: a menu load or a
    first-boot stall under the bridge.
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

## Counter reset after an ITR2 restart (gap 2; Overseer: same registration)

- ITRZone's answers (2026-11-24): its seq starts again at 1 on every ITR2 start (it lives in the mod's memory
  only), so today a restarted ITR2 would be rejected for good. It agrees to the session number below, with
  session = the mod's start time in UTC ms, fixed for the life of the process. It is a pose format change (v3),
  so it goes into a joint registration; not before the x1 retry is done.
- Proposed and agreed in principle: ITR2 adds a session number to the pose's first line that changes on every
  ITR2 start (for example its start time in ms). A pose whose session number differs from the last one is
  accepted whatever its seq, the script's seq counter restarts from it, and it logs
  `EPOCH,<track s>,<game ms>,<old session>,<new session>,<seq>`. Within one session the rule stays "seq must be
  higher".
- Without a session number the script cannot tell a restarted ITR2 from an old pose file left on disk, so a rule
  like "accept any lower seq" is not safe.
- Witness: the writer is killed and started again with seq 1 and a new session number: LOST, then EPOCH and
  RESUME, poses applied again. Mutation: the same restart with the OLD session number: no pose may be applied.
- The first pose after an EPOCH line gets the same guard exemption as the first pose after LOST.

## Time-out while LOST (Overseer's ruling, 2026-11-24)

- **Ruling:** this is resource hygiene, not feel, so it is not Caden's call. His keep/freeze choice covers a player
  who is away on purpose; a crashed ITR2 is not that. Under either policy, LOST for 30 minutes means: save
  X-Ray's game and stop it cleanly. The next ITR2 start boots X-Ray from that save as usual.
- **Before the 30 minutes:** under freeze nothing happens (a stopped process costs nothing); under keep A-Life
  runs on.
- **Setting:** the limit is a runner setting (proposed name T2_LOST_SAVE_S, default 1800), measured on the
  runner's wall clock from the LOST moment. A new pose before it cancels it.
- **How (proposed):**
  - The runner writes a small request file on the bridge tmpfs. Under freeze it first lets the engine continue
    (SIGCONT, with the absorb armed as for any freeze, so no game time jumps).
  - The script sees the request, logs `LOST_SAVE,<track s>,<game ms>,<age s>,<save name>`, runs the console
    `save` command, checks the save file exists, logs `LOST_SAVED,<ok|error>`, then quits the game.
  - The runner logs the save's name, size and sha256, and ends with its own reason `lost save`. No restart.
  - The save has its own fixed name. It never overwrites a save of Caden's, and in tests never the test save.
- **Optional witness (does not block the first registration):** boot 1 or 2 of the witness with the limit
  shortened to 60 s and the writer not restarted. Expected: LOST, then LOST_SAVE and LOST_SAVED,ok at 60 s
  (within 5 s), the save file present, the engine gone, runner rc 0 with reason `lost save`, test save hash
  unchanged. A second boot loads that save and runs 60 s with no error.
- **Not decided here:** who starts X-Ray again when ITR2 comes back, and from which save; that is the bridge's
  start-up path, to be agreed with ITRZone.

## Not covered by this note

- What ITR2 does when X-Ray is the one that stops writing snapshots; that is ITRZone's side. Its answer: the mod
  logs the stale snapshot after 1 s and keeps the puppets as they were; it resumes at the next accepted snapshot
  (my header's run id or a level change resets its seq check). It has no "X-Ray is gone" behaviour yet; open on
  ITRZone's side.
