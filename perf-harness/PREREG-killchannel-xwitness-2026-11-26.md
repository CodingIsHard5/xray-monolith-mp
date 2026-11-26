# Kill channel, X-Ray-only witness "kw1": REGISTERED 2026-11-26, before any run. The boot needs the Overseer's GO.

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
| to the run's end (about 80 s) | no `k` line |
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
| 2 | part hits: for S and for M exactly two KAPPLY lines before the first pose with dead = 1, differences 0.250 and 0.350; "health after" = "health before" minus the difference. (The death step logs a third KAPPLY, 0.400: counted in check 12, its arithmetic not judged, because with dead = 1 the 0.01 stop does not apply and health ends at 0) | +/- 0.0002 | both healths are read in the same script step, before and after the write; the only error is the log's 4 decimals, twice 0.00005, doubled |
| 3 | no second effect: while a total is repeated for 5 s (about 100 poses) no further KAPPLY for that id | count 0 | the rule itself |
| 4 | the creature really lost health: its `o` line health in the first snapshot after each KAPPLY is below its health in the last snapshot before it | by at least half the difference (0.125, 0.175) | regeneration and X-Ray's own hits can move health between two snapshots 50 ms apart; half the step is far above any regeneration in 50 ms and still proves the write reached the object the snapshot reads. Stated as a floor only; a larger drop is not a failure |
| 5 | confirmation: `k,<id>,0.250` then `k,<id>,0.600` are read by the writer | within 2.0 s of the first pose carrying the total | chain: pose file write (0.05 s period) + the script's pose read (once a frame) + snapshot write (0.05 s) + writer read (0.05 s) = 0.15 s plus one frame; 2.0 s allows a frame-rate stall of over 1.5 s, which I have seen headless at level events; a miss beyond that means the chain is broken, not slow |
| 6 | death: for S and for M one `KDEAD,…,online`; the writer reads `k,<id>,…,dead` | within 2.0 s of the first pose with dead = 1 | as 5 |
| 7 | the creature is dead in X-Ray: after the `dead` line its `o` line shows alive 0 or the id is no longer listed, to the end of the run | from 2.0 s after the `dead` line | the kill call sends a death event handled on a later frame; 2.0 s as in 5 |
| 8 | the `dead` line stays while the writer still sends the line and for 10 s after it stops, then goes | last `dead` line 10.0 s after the last pose that carried the line, +/- 1.0 s | the 10 s hold is refreshed by every pose line; after the last one it runs out once; +/- 1.0 s = one stall as in 5 on one side, snapshot spacing on the other |
| 9 | the kill is the actor's: one `KWHO,<time>,<id>,<killer id>` line for S and for M with killer id 0 | exact | the death callback's own killer argument; id 0 is the actor |
| 10 | the actor is unhurt and the actor line is refused: actor health in every snapshot = its value in the first; one `KREFUSED,…,0,actor`; no KAPPLY or KDEAD for id 0 | exact | the standing rule (the actor takes no damage); the bench save's actor has no bleeding or radiation |
| 11 | the `r` line: absent in no snapshot after the first v4 pose was taken; its seq never goes back; at every snapshot it is within 2.0 s of the writer's newest pose | 2.0 s | as 5 |
| 12 | counters in the last T2SUM: khits 6 (two part hits and the death step's own, for each), kdead_online 2, kdead_offline 0, kdead_fail 0, krefused 1, kstale 0, kback 0, kclamped 0, kver 0, kowed 0, ktabfail 0 | exact | follows from the steps |
Reported, not judged: the health of every other listed creature at the two part-hit moments and at the deaths
(control for "only S and M change"); frame time around the steps; any GAMMA log line within 1 s after a KAPPLY or
KDEAD that names S or M.

## 5. Guess
PASS 35 %, NO VERDICT 25 %, FAIL 40 %. Named risks: the death callback's killer is not the actor for `kill(actor)`
on a monster (check 9); GAMMA reacts to the near-zero hit by turning S hostile and S or its squad fights (no check
fails by itself, but it can spoil 4 or cause NO VERDICT); `set_health_ex` on a stalker is undone by GAMMA's own
health scripts within the same frame (check 4); S dies in a fight before 40 s (NO VERDICT); GAMMA's silent-kill script treats the near-zero hit on an unaware stalker as a stealth shot, kills S at the first part hit or names the weapon as killer (checks 2, 9; its trigger is inside the game archives and I could not read it); a poltergeist as M gets no death callback (check 9).
First draft of this table had two slips, found by building the checker before registration: it expected two KAPPLY lines and khits 4, but the death step applies the last 0.400 as a hit of its own.

## 6. Process
Own display `:141` with exists-refusal. WANT queue, one slice. Score once, output written to a file in that same
call. Stop on crash, timeout or failed preflight; no second boot without the Overseer.

## 7. Pins
| file | sha256 |
|---|---|
| tools/t3_kw1.sh (driver; refuses unless every file below matches, the display is free, the out dir is new) | cecc098f88dde60591907daad22dce62a1221e1094b703a2340ed3fa54c8e6a4 |
| tools/t2_xray_kw.sh (runner; = t2_xray_v3.sh 71d070e5 with the script name and one more header field read) | 16cb80107136af54234bdad37b31f55b1526915aeb41f69a0d4649b5104b6185 |
| harness/zz_p1_puppet_kill4.script | f19b28c094dabaab99e64b71716f696bdee200d46d37aa366e32b7eba5ae33af |
| tools/t2_kill_pose.py (stand-in writer) | 9f3075cdafa728a808eec43b135dac0080e311d1de6cf204e2416bea119046e6 |
| tools/t3_kw1_check.py | 94bfe3abb299c5bfe6e524012659ae1868b3a94c40f60245b485f032b8ba33b1 |
| tools/t3_kw1_selftest.sh | 08fe6760ee1d3f07bf9b58a212778929b4ce2a9e47b4d43edd116c11509fdee9 |
| run/bin/Stance14.exe | b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe |
| run/appdata/savedgames/cordon1.scop | 6dea752bc652a45c748e1438e3bc8ffd2d8d89f24730177c792328aeb5d13f3e |
| ITRZone out/p1/cordon_full_track.csv | 213ff8f125723f011b6cae3972c5e9afa574bf0418a36c0fb34fbde652734cd1 |
Copies of the tools: `kw1/`; of the script and its offline tests: `killchannel-draft/`.
Tests run by me on these bytes: offline reader tests 33 checks pass, 38 mutants each fail their check; checker
selftest PASS (good = PASS; 13 mutant folders each fail exactly their own check; 3 NO VERDICT folders; 6 missing-file
folders fail by name). ALL checker fixtures are made by the offline stub, none by the engine: the first real run is
also the first time the checker reads real engine bytes (CRLF is stripped; a reader fault would show as a FAIL by
name or a crash of the checker).
Correction by the Overseer, written in before the boot (2026-11-26): there is NO re-score after a checker fix. If the
registered checker faults on real bytes, its output stands as of record, or the run is NO VERDICT if it cannot
produce one. A fixed checker's read of the same data is reported beside it, labelled POST-HOC DESCRIPTIVE. Any
verdict that counts needs a new run under a re-registered checker.
Run: `BENCH_DISPLAY=:141 tools/t3_kw1.sh out/kw1` (background task; env as d0: T2_EXE=Stance14.exe T2_FREEZE=sigstop
T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_MAXRESTART=0, run length 125 engine seconds).
Score, once: `python3 -I tools/t3_kw1_check.py out/kw1 out=out/kw1/t3_kw1_check.txt`.
CodeRabbit on the tools (2026-11-26): 4 findings. Fixed: the checker's check 4 now takes the PICK row as a possible
health baseline before the first hit (a correct run could have failed); the writer's NOPICK row without any snapshot
had 16 of 17 columns. Neither case has a fixture of its own in the selftest. Not changed: the runner does not reap
the engine when it is cancelled mid-run (the same code ran d0, d1, d2; I keep the runner's diff to the two lines);
the driver gives the slice back by itself, so after a cancel I look for a leftover engine at once, stop it and report it.
