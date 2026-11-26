# Kill channel, joint run "kj1", X side: REGISTRATION TEXT, NOT YET REGISTERED (pins in section 9 are open)

It becomes REGISTERED when section 9 is filled and committed. No boot before the Overseer has reviewed this text and
itrzone's (their amendment 137) and given the GO. History of how it got here: the DRAFT file of the same name
(drafts 1 to 7b); where the two differ, this text counts.

Limits first:
- A rig deals the damage (itrzone's lever `kcj`), not a weapon in a hand. Nobody looks: animations, sounds, hit
  markers and what the player would feel are not shown.
- One stalker and one other creature, picked by the rig at the track's start. No offline creature, no crossing, no
  restart of either side, one ITR2 session.
- The checker has never read kept files written by the engine. Its fixtures' snapshots and poses are made by a
  fixture maker (the snapshot parser was tried on snapshots written by the real script in the offline stub). If it
  faults on real bytes its output stands, or the run is NO VERDICT if it gives none; no re-score (section 8).
- The stop in step D is made by my runner, not by a pose. itrzone showed on the desk that a pose-made freeze can
  never contain a kill (the mod destroys every puppet before any away or dead state).

## 1. What it is for
The first run of the kill channel between the real ITR2 mod and the real X-Ray reader. Formats and rules: itrzone's
amendments 134 (my copy `killchannel-draft/itrzone-amendment-134.txt`, 653e6f00…) and 136 (healing; the total has no
upper limit; my copy `kj1/itrzone-amendment-136.txt`, ac48771c…). Caden's rulings behind it: the four of 2026-11-26
16:50 UTC (ITR2's health and weapons count; ITR2's kills are the player's in X-Ray; a restart may undo kills; loot
in ITR2) and 18:56 UTC, "Heal like stalker unless it's hard to implement".
Healing needs no mechanism of its own: the total is a count of damage dealt, X-Ray applies each new piece once, its
own health then regenerates at its own rates, and the puppet shows the snapshot's health. The reader is the
registered `kill4`, unchanged.

## 2. The run
One boot of each side, Cordon, headless (X-Ray on `:141`, exists-refusal). itrzone's boot script
(`scripts/boot_t2.sh kj1`) holds the box, starts ITR2 and then my runner; I start nothing.
ITR2 (their text): `hcheal=2 t2span=4 t2gap=0 t2v=4 t2rec0=1 t2fc=1 kcj=30 kcjs=0.6 kcjt=0.5 t2stop=300 t2end=600`,
away_sim keep. The rig stands at the first 4 s of the Cordon track.
Their play clock starts at the first puppet tick with an accepted snapshot of mine; no play pose is written before.
| their clock | the rig | what must happen in X-Ray |
|---|---|---|
| 30 s | picks the nearest stalker S and the nearest other creature M with snapshot health >= 0.9; S takes 0.6 of full health, M 0.3 | each total applied once |
| 30 to 70 s | nothing | S heals at X-Ray's rate; nothing of mine touches it |
| 70 s | S takes 0.5 more (total 1.100) | applied as 0.500; S alive |
| 90 s | kills S | S dies, killer the actor |
| 115 s | kills M, inside my stop | nothing during the stop; M dies after it, once |
My runner stops X-Ray by signal for 20 s, 105 s after the header time of the first new play pose it reads. It reads
no pose before its own first snapshot and polls about once a second, so on their clock STOP is at about 106 to 108.3
and CONT at up to about 128.3. The stop is not absorbed: X-Ray's script clock jumps 20 s at CONT (seen in out/t2pre2).
X-Ray ends first (240 engine seconds from its level load, about their clock 240); ITR2 ends at 300.
Env (set by their boot script): `T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999
T2_KILL_AT=99999 T2_MAXRESTART=0 T2_LOST_MS=5000 T2_LOSTX_MS=120000 T2_OVER_S=120 T2_STALL_PLAY=105 T2_STALL_S=20
T2_RUN_S=240 T2_WALL_S=700 BENCH_DISPLAY=:141`. Called as `tools/t2_xray_kj.sh <X out dir> itr2-shadow`.
Derivations of the times I depend on:
- 40 s between S's hits: at kw1's 0.01 a second S heals 0.40, from 0.4 to about 0.8. The rig's hit is a share of
  FULL health dealt to the puppet's CURRENT health, so the 0.5 hit kills the puppet unless S healed by more than
  0.10. That outcome is a FAIL of check 5 by name, not a no-verdict.
- The kill at 115 is at least 6.7 s after STOP and about 11 s before CONT. Their clock can only run late against
  the wall (it adds at most 0.25 s a frame), which moves the kill toward CONT; NV4 guards both ends.

## 3. What is kept
`tools/t1_watch_keep.py` writes every complete snapshot whole to `boot0/watch/snaps_all.txt` and every complete
pose whose header differs from the last kept one to `boot0/watch/poses_all.txt`, each with its read time. The runner
writes `stall.csv` (STOP and CONT rows, UTC ms). The checker reads these, the script's meta lines, the engine log
and the runner's log. It reads nothing of ITR2's. The snapshot period is 100 ms.

## 4. PASS = all fourteen. Every bound is derived for this run; none is copied.
S and M are found in the data (the two ids with `k` lines in the kept poses; S = snapshot kind `s`, M = kind `m`).
Every expected total comes from the kept poses; no check names 0.6, 0.5 or 0.3.
| # | check (outcome) | bound | where the bound comes from |
|---|---|---|---|
| 1 | no crash, no FATAL, no Lua error naming the bridge script, runner rc 0, one boot | 0 | - |
| 2 | first hit: one KAPPLY for S and one for M, difference = the pose's total, after = before - difference | +/- 0.0002 | both healths are read in one script step; two 4-decimal roundings (0.00005 each), doubled |
| 3 | no second effect: every KAPPLY belongs to a NEW total in the kept poses | 0 others | the rule |
| 4 | healing is left alone: between S's two hits, and between M's hit and its death step, no KAPPLY for the id and no fall of its snapshot health over the bound with a K line of mine between the two snapshots | 0.02 | one snapshot step is 100 ms; healing in it is 0.001, rounding 0.0001; my smallest hit is 0.3. A fall beyond it with no K line of mine is X-Ray's own world: NV3 |
| 5 | S healed, and a total above 1.000 is applied: S's health in the last snapshot before its second KAPPLY is above its health in the first snapshot after the first by at least the bound; the second KAPPLY's difference = the pose difference (so it was not cut); S is alive after it above 0.02 | 0.15; 0.02 | kw1's rate gives 0.40 in 40 s; 0.15 is under half of it (a slower stalker still passes) and far over check 4's 0.02 (no healing cannot pass). 0.02 is above the 0.01 stop: the stop was not what kept S alive. S dead at the second hit, or no live total above 1.000: FAIL by name; S's later steps are then not judged, M's are |
| 6 | only S and M change: no other creature within 10 m of S or M loses more than the bound across a KAPPLY or KDEAD (the snapshot before and the one after) | 0.02 | as 4. Within 10 m is judged because a hit of mine leaking to a neighbour would show there; farther creatures are reported (X-Ray's own fights) |
| 7 | S's death: one `KDEAD,…,online`; its `dead` line is read within the bound of the watcher's first read of the pose with dead = 1 | -0.05 to 2.0 s | chain: pose 0.05 s + one frame + snapshot 0.10 s + watcher 0.002 s = about 0.15 s plus a frame; 2.0 s allows a stall of over 1.5 s. -0.05: the watcher reads the snapshot before the pose in each 2 ms poll, so the script can have taken the pose first |
| 8 | the kills are the actor's: one KWHO with killer 0 for S and for M | exact | the death callback's killer |
| 9 | the stop held: no snapshot read inside the stop; the script's own RESUMED_GAP line matches it; no KAPPLY or KDEAD with a script time inside the gap | edges +/- 0.1 s | STOP is stamped just before the signal and CONT just after; 0.1 s is far over a process start and far under 20 s. The script-time clause uses the script's own last frame before and first frame after the gap, exact. A second, wall-clock form of it has an error of 0 to 1 s (whole seconds in the script's lines) and counts an event only if it lies wholly inside |
| 10 | M's death after the stop: one `KDEAD,…,online` for M at or after the first frame after the gap; M's `dead` line in no snapshot read before CONT; and read within the bound of the first kept snapshot read at or after CONT | 2.0 s | after CONT the newest pose is on disk, so it is one script read and one snapshot write; 2.0 s as in 7. Counted from the first snapshot, not from CONT: a slow first frame after the 20 s jump is REPORTED (CONT to first snapshot) and fails neither side by itself (agreed with itrzone, whose J6 counts the same way) |
| 11 | dead in X-Ray: from the bound after each `dead` line the id is alive 0 or not listed, to the end | 2.0 s | the kill call's death is handled on a later frame; kw1 showed alive 0 in the next snapshot |
| 12 | the actor is unhurt: actor health in every snapshot = the first | exact | standing rule |
| 13 | the `r` line: in every snapshot after the first pose was taken; never back; within the bound of the newest kept pose (pose header time); the first snapshot at or after CONT is allowed the stop's length more | 2.0 s | as 7; inside the stop there is no snapshot |
| 14 | counters: EVERY key of the last T2SUM line is judged against one table; a key without an expectation, or an expected key missing, FAILS by name. torn 0, lost 0, epochs 0, paused false, kdead_online 2, kdead_offline 0, kdead_fail 0, krefused 0, kstale 0, kback 0, kclamped 0, kver 0, kowed 0, ktabfail 0; khits = the number of (id, total) steps with a positive difference in the kept poses (5 by the plan: S 0.6, 1.1, death; M 0.3, death); last_seq between the last kept snapshot's `r` seq and the highest kept pose seq; holds, near and guards equal to the count of their own meta lines | exact | follows from the steps. holds, near and guards are not kill-channel outcomes: they are judged only for agreeing with their own log lines, so that no key is skipped |
The `dead` line for an online death means "the kill call was made" (the reader sets it in that step; the engine
handles the death later in the frame). That it really died is check 11's.

## 5. NO VERDICT, decided before. Each rule names the checks it leaves untested.
| rule | untested |
|---|---|
| NV1: not exactly one stalker and one monster with `k` lines | 2 to 11, 14 |
| NV2: S or M hurt by more than 0.02 before its first hit, or killed before its step, by X-Ray's own world | 2 to 11, 14 |
| NV3: S or M falls more than 0.02 between two snapshots with no K line of mine between them | 2 to 11, 14 |
| NV4: the watcher's first read of M's pose with dead = 1 is less than 2.0 s after STOP or less than 2.0 s before CONT | 9, 10 |
| NV6: S or M is not listed in X-Ray's snapshot at the time of the first pose of one of its later steps | 3 to 11, 14 |
A NO VERDICT never hides a failure it does not explain: if any failing check is outside the fired rules' sets, the
run is a FAIL and the output names it. Checks 1, 12 and 13 are always judged. A missing, empty, unreadable or
unparsable required file is a FAIL by name. (There is no NV5; it became check 5's FAIL by name.)
2.0 s in NV4: the same chain bound as 7, so a kill that close to an edge could be confirmed outside the stop.
One retry needs the Overseer's GO. itrzone's side is not scored when mine is NO VERDICT for S or M, and the reverse.

## 6. Reported, not judged
CONT to the first snapshot; the largest gap between snapshots in each step (frame time); health of creatures beyond
10 m at the hits; S's and M's healing curves; M's healing.

## 7. Guess
PASS 25 %, NO VERDICT 30 %, FAIL 45 %. Named risks:
- the checker meets real kept bytes for the first time (a parse fault is a FAIL by name);
- S is a story character near the rookie village (Fanatic, or Wolf further off): the near-zero hit is ignored by
  the engine for an invulnerable character, the health change and the kill call should work by my reading of the
  source; not seen in a game (checks 2, 5, 11);
- GAMMA's reaction to a 0.6 hit by the actor: S and its squad turn hostile, a fight starts (NV3), or S at 0.4 goes
  into the wounded state and heals differently or not at all (check 5: dead at the second hit);
- M, hurt for 0.3, runs off and goes offline in the 85 s before its kill (NV6);
- the 20 s clock jump after my stop: every timed script fires at once; a Lua error (check 1) or a very slow first
  frame (reported); the first pose read after CONT is torn, which the reader counts as LOST (check 14);
- a GAMMA script names the weapon or nobody as the killer (check 8).

## 8. Process
Score once: `python3 -I tools/t3_kj1_check.py <X out dir> out=<X out dir>/t3_kj1_check.txt` (the checker refuses to
overwrite a report). No re-score after a checker fix: the registered checker's output stands, or NO VERDICT if it
gives none; a fixed checker's read is POST-HOC DESCRIPTIVE; a verdict that counts needs a new run under a
re-registered checker (the Overseer's rule, kw1). Stop on a crash, timeout or failed preflight; no second boot
without the Overseer.

## 9. Pins (sha256). OPEN until the last review is in; then filled and the title changed to REGISTERED.
| file | sha256 |
|---|---|
| tools/t2_xray_kj.sh (runner) | OPEN |
| tools/t1_watch_keep.py (watcher) | OPEN |
| tools/t3_kj1_check.py (checker) | OPEN |
| kj1/t3_kj1_mkfix.py, kj1/t3_kj1_selftest.sh (fixtures, selftest) | OPEN |
| tools/lib_engine_pids.sh | cb183b7b… (full at registration) |
| tools/tb_run.sh | 1b8d103f… |
| tools/run_noguard.sh | 92729abc… |
| harness/zz_eperf_dump.script | 2b79a817… |
| out/user.ltx.orig | 3efd38ee… |
| harness/zz_p1_puppet_kill4.script (the registered reader, unchanged) | f19b28c094dabaab99e64b71716f696bdee200d46d37aa366e32b7eba5ae33af |
| run/bin/Stance14.exe | b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe |
| run/appdata/savedgames/cordon1.scop | 6dea752bc652a45c748e1438e3bc8ffd2d8d89f24730177c792328aeb5d13f3e |
Tests on these bytes, to be restated with the final hashes: checker selftest (104 folders at the last count: good,
one mutant or more per check, one folder per counter off by one, an unknown and a missing counter, every NO VERDICT
rule, NO VERDICT with an unrelated failure, every required file missing, empty and unreadable, unparsable records);
the reader's offline tests with the check for a total above 1.000 (34 checks, 40 mutants).
Not tested by the selftest: bounds exactly at their edges; several T2SUM lines; a session change; and no fixture's
kept files come from the engine.
