# Kill channel, X-Ray-only witness "kw1": DRAFT (2026-11-26). Not registered until section 7 is filled and the Overseer says GO.

Limits first: one X-Ray-only boot with a stand-in writer, no ITR2. Nothing is looked at. It cannot show: what the
player sees or hears (pain animation, sounds, hit markers); an OFFLINE creature (the snapshot lists only online ones,
so the stand-in cannot name one; the offline death stays covered by the offline test only); stalkers in special states;
more than one creature of each kind; anything about ITR2's side.

## 1. What it is for
The formats and rules are itrzone's amendment 134 (joint text draft 7; my byte-matched copy
`killchannel-draft/itrzone-amendment-134-draft.txt`, sha256 20453032…). The offline tests (28 checks, a mutant each)
run my reader against my own model of the engine. This run asks the engine itself the questions the model cannot
answer: does the direct health change work on a real stalker and a real mutant, does a kill by the actor kill and
name the actor, does the actor stay unhurt, do GAMMA's scripts survive a near-zero hit and a script kill.

## 2. The run
One boot, Cordon bench save, reader `harness/zz_p1_puppet_kill4.script`, runner and stand-in writer as arm D d0
(new copies, pinned in section 7). The writer walks the registered track, version 4 poses, 20 a second, and reads
every snapshot. Steps, on the writer's own clock from its first pose:
| time | the writer sends |
|---|---|
| 0 to 30 s | no `k` line (control: the health of every listed creature is logged) |
| at 30 s | picks S = the nearest alive stalker and M = the nearest alive monster in the newest snapshot, both with health >= 0.9 |
| 30 to 35 s | `k,S,<section>,0.250,0` and `k,M,<section>,0.250,0`, and `k,0,actor,0.500,0` (must be refused) |
| 35 to 40 s | totals 0.600 |
| 40 s on | totals 1.000 with dead = 1, until my `dead` line for the id was read, then 5 s more, then the line is dropped |
| to 90 s | no `k` line |
The writer logs one row per snapshot read: its time, the snapshot seq, the `r` line, the actor's health, and for S
and M the `o` line's health and alive field and the `k` line. That log is the stand-in's view, as ITR2 would have it.

## 3. NO VERDICT (not PASS, not FAIL), decided before the run
- No stalker or no monster with health >= 0.9 is listed at 30 s.
- S or M is hurt or killed by X-Ray's own world before its step: a KAPPLY line whose "health before" is more than
  0.02 below the health the writer logged for it at 30 s, or a `KDEAD,…,seen` line for it.
  Derivation of 0.02: nothing; it is a guard against a fight, not a pass bound. A NO VERDICT run is reported with its
  numbers and is not scored. One retry, at another start time of the picks (45 s), needs the Overseer's GO.

## 4. PASS = all of these. Every bound has its derivation; none is copied from another arm.
| # | check (outcome) | bound | where the bound comes from |
|---|---|---|---|
| 1 | no crash, no FATAL, no Lua error naming the bridge script, runner rc 0, one boot | counts 0 | - |
| 2 | part hits: for S and for M exactly two KAPPLY lines, differences 0.250 and 0.350; "health after" = "health before" minus the difference | +/- 0.0002 | both healths are read in the same script step, before and after the write; the only error is the log's 4 decimals, twice 0.00005, doubled |
| 3 | no second effect: while a total is repeated for 5 s (about 100 poses) no further KAPPLY for that id | count 0 | the rule itself |
| 4 | the creature really lost health: its `o` line health in the first snapshot after each KAPPLY is below its health in the last snapshot before it | by at least half the difference (0.125, 0.175) | regeneration and X-Ray's own hits can move health between two snapshots 50 ms apart; half the step is far above any regeneration in 50 ms and still proves the write reached the object the snapshot reads. Stated as a floor only; a larger drop is not a failure |
| 5 | confirmation: `k,<id>,0.250` then `k,<id>,0.600` are read by the writer | within 2.0 s of the first pose carrying the total | chain: pose file write (0.05 s period) + the script's pose read (once a frame) + snapshot write (0.05 s) + writer read (0.05 s) = 0.15 s plus one frame; 2.0 s allows a frame-rate stall of over 1.5 s, which I have seen headless at level events; a miss beyond that means the chain is broken, not slow |
| 6 | death: for S and for M one `KDEAD,…,online`; the writer reads `k,<id>,…,dead` | within 2.0 s of the first pose with dead = 1 | as 5 |
| 7 | the creature is dead in X-Ray: after the `dead` line its `o` line shows alive 0 or the id is no longer listed, to the end of the run | from 2.0 s after the `dead` line | the kill call sends a death event handled on a later frame; 2.0 s as in 5 |
| 8 | the `dead` line stays while the writer still sends the line and for 10 s after it stops, then goes | last `dead` line 10.0 s after the last pose that carried the line, +/- 1.0 s | the 10 s hold is refreshed by every pose line; after the last one it runs out once; +/- 1.0 s = one stall as in 5 on one side, snapshot spacing on the other |
| 9 | the kill is the actor's: one `KWHO,<id>,<killer id>` line for S and for M with killer id 0 | exact | the death callback's own killer argument; id 0 is the actor |
| 10 | the actor is unhurt and the actor line is refused: actor health in every snapshot = its value in the first; one `KREFUSED,…,0,actor`; no KAPPLY or KDEAD for id 0 | exact | the standing rule (the actor takes no damage); the bench save's actor has no bleeding or radiation |
| 11 | the `r` line: absent in no snapshot after the first v4 pose was taken; its seq never goes back; at every snapshot it is within 2.0 s of the writer's newest pose | 2.0 s | as 5 |
| 12 | counters in the last T2SUM: khits 4, kdead_online 2, kdead_offline 0, kdead_fail 0, krefused 1, kstale 0, kback 0, kclamped 0, kver 0, kowed 0 | exact | follows from the steps |
Reported, not judged: the health of every other listed creature at the two part-hit moments and at the deaths
(control for "only S and M change"); frame time around the steps; any GAMMA log line within 1 s after a KAPPLY or
KDEAD that names S or M.

## 5. Guess
PASS 35 %, NO VERDICT 25 %, FAIL 40 %. Named risks: the death callback's killer is not the actor for `kill(actor)`
on a monster (check 9); GAMMA reacts to the near-zero hit by turning S hostile and S or its squad fights (no check
fails by itself, but it can spoil 4 or cause NO VERDICT); `set_health_ex` on a stalker is undone by GAMMA's own
health scripts within the same frame (check 4); S dies in a fight before 40 s (NO VERDICT).

## 6. Process
Own display `:141` with exists-refusal. WANT queue, one slice. Score once, output written to a file in that same
call. Stop on crash, timeout or failed preflight; no second boot without the Overseer.

## 7. Pins (to fill before registration)
Runner, writer, checker with selftest (real run folder fixtures: none exists yet for v4, so the checker's fixtures
are made by the offline stub and marked as such; a mutant per check), script, exe, save, env, the score command.
