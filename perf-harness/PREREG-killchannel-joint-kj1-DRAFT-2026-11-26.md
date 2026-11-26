# Kill channel, joint run "kj1", X side: DRAFT 7 (2026-11-26). NOT registered, nothing built, nothing run.

Ordered by the Overseer after kw1 (PASS 12/12). No boot until the Overseer has reviewed the registered text.
Limits first: a rig, not a player (itrzone's lever `kcrig` deals the damage); nobody looks; one stalker and one
mutant unless the rig can do more; no offline creature; no crossing; no restart of either side.

## 0. Open before this can be registered
- **Regeneration: RULED by Caden** (Discord 2026-11-26 18:56 UTC, relayed by the Overseer): "Heal like stalker unless
  it's hard to implement". The ceiling of drafts 1 to 4 is OUT and nothing was built for it. Section 3 says how the
  registered design already heals and what small format change is still owed (a total above 1.000). Open with
  itrzone: their answers a) to d) to my message of today; the amendment is theirs to register, mine to byte-match.
- **[I] kc1.** itrzone's ITR2-only witness (their amendment 135) is running. If ITR2 removes a dying puppet before the
  mod's health read, their rule "a vanished puppet is not a death" changes; my reader would not. Its result is read
  before this text is fixed.
- **[I]** the rig's steps and their times (below: T1, T2, T3, TF are the rig's, to be filled from their text).
  itrzone, 2026-11-26: today's lever `kcrig` (their am135) cannot do steps A to D (it hits 30 % then kills, puppet by
  puppet in id order). A new scripted lever is theirs to draft after kc1: a list of play-clock time, nearest stalker
  or nearest monster at that time, hit share or kill; the id is logged. Their pose period is 50 ms.
  - The hit share is exact only if ITR2 lets the mod write health; with engine damage it is what the game gives.
    So no check here names 0.250: checks 2 to 5 take the total from the pose my watcher kept.
  - Step D has two forms. (i) The real path: the rig goes to state away under away_sim = freeze for 20 s, X-Ray is
    stopped by the registered v3 rule, the kill is dealt in that time; possible only if ITR2 keeps the puppets while
    away (itrzone checks on the desk). (ii) My runner stops X-Ray on its own (T2_STALL_AT, as arm D d0) while their
    state stays play. Preferred: (i), because it is the path a player would take; (ii) only if (i) cannot deal the kill.
    The registered text names one, and check 9's stop window comes from `freeze.csv` in both.
- Build owed on my side: reader `kill5` (section 3), a watcher that keeps whole files (section 2), the checker and
  its selftest (section 5). CodeRabbit on each.

## 1. The run
One boot of each side, Cordon, itrzone's boot script starts my runner and holds the box (as arm D). ITR2's rig:
| step | the rig does | what must happen in X-Ray |
|---|---|---|
| A, at T1 | part hit on S and on M, total 0.250 | health down by 0.250, once |
| B, T1 to T2 | nothing for at least 30 s | health heals by X-Ray's own rate; nothing of mine touches it (section 3) |
| C, at T2 | kills S | S dies, killer the actor |
| D, at T3 | pose says `freeze`: my runner stops X-Ray for 20 s; at TF inside the stop the rig kills M | nothing during the stop; M dies after the stop, once |
30 s in step B: derived, not copied. At the stalker's measured 0.01 a second, 30 s is a rise of 0.30, twenty times
check 4's noise bound and twice check 5's floor; on a flesh (about 0.0004 a second) it is about 0.011, which is
why healing is JUDGED on S only and reported for M.

### 1.1 Draft 4 (2026-11-26): kc1 read, the freeze step settled, the schedule
- itrzone's kc1 (their am135, ITR2 alone against a stand-in): PASS 10/10 by their score; 8 kills, none lost, no puppet
  removed before the health read, so their death rule stands and nothing changes for my reader. ITR2 did not
  regenerate a puppet in 215 s. Engine damage on puppets was exact (0.300). Their limits: a stand-in, 8 creatures.
- Step D is form (ii): MY runner stops X-Ray, their state stays play. Reason (itrzone's desk check): their pose is
  away or dead only when the ITR2 world changes, and the mod destroys every puppet before that, so there is nothing
  to kill inside a pose-made freeze, in the rig or in play. What a player can meet is X-Ray stalling while they
  keep shooting; the runner's stop imitates that.
- Schedule, seconds on ITR2's play clock from its first play pose: 30 pick, part hit 0.3 on S and M; 70 second part
  hit 0.2 on S (total 0.500); 90 kill S; 115 kill M; end 160. My stop: 105 to 125.
  Derivation of the times I depend on: 40 s between the first hit and the second (healing, check 5: section 1's 30 s
  plus 10); the kill at 115 is 10 s inside each end of the stop; my runner polls once a second and sees their first
  play pose at most about 1 s late, and SIGSTOP is immediate, so the stop starts 105 to about 107 on their clock:
  8 s of margin before the kill, about 12 after. 35 s after the stop for the death, the 10 s `dead` hold and the end.
- New on my side for this: a runner trigger "stop N s after the first play pose the runner read" (today's stall
  trigger counts X-Ray's track time, which has no fixed offset to ITR2's play clock). Stop window from the runner's
  own STOP and CONT rows (`stall.csv` for this trigger; check 9 reads that file, not `freeze.csv`).
- New NO VERDICT rule: the first kept pose with dead = 1 for M must be read by the watcher at least 2 s after STOP
  and 2 s before CONT. Otherwise the step did not test a kill inside a stop; it is not a failure of the channel.
- (Draft 5: the schedule's shares for S change to 0.6 and 0.5, section 3; the times stay. khits in check 14 is
  fixed from the final shares at registration.)

## 2. What is kept (new)
The watcher writes every COMPLETE snapshot, whole, to `snaps_all.txt` and every complete pose it sees, whole, to
`poses_all.txt`, each with its own read time. The checker reads these two files and the script's meta lines; it does
not need ITR2's log. Size: about 20 snapshots a second; at kw1's listing (to be measured from the first file) it is
tens of MB for the run. A missing or empty file fails by name.
Built and tried on made-up files (draft 2): `kj1/t1_watch_keep.py` (sha256 9df9c8e4…; = t1_watch.py plus the two
kept files; pose file named by env T1_KEEP_POSE). 5 snapshots kept of 5, a torn one not kept, CR stripped, a pose of
a new session with seq back at 1 kept. Not yet wired into the runner; not reviewed.

## 3. Healing (replaces the ceiling)
How it works, all of it already in amendment 134 and seen in kw1:
- The total is damage dealt so far, a counter, not a health level. X-Ray applies each new piece once.
- After that the creature's health is X-Ray's own and heals at X-Ray's rates (kw1: stalker about 0.01 a second,
  flesh about 0.0004). Nothing in the bridge touches it between hits.
- ITR2's puppet shows the snapshot's health minus (total - applied), so it heals as X-Ray's creature heals.
- ITR2's damage stays the authority: X-Ray never lowers a bridged creature's health for the player except by a
  total from ITR2; healing is X-Ray's.
So X-Ray health above 1 - total is the healing itself. No falling total is needed.
Owed, small: the registered text limits the total to 0.000 to 1.000. With healing a creature can take more than
1.0 in sum and live. My reader already accepts it (any non-negative number; only a single DIFFERENCE above 1.0 is
cut and counted, `kclamped`). Proposed to itrzone: lift the upper limit in the text and in their writer; an offline
test with a mutant on each side; a new joint amendment. Not hard: desk work.
Not covered, said plainly: healing by an X-Ray stalker's own medkit use and X-Ray's own fights also move health;
the puppet follows them too, by the same arithmetic. An OFFLINE creature's health cannot be read or set, so its
healing while offline is whatever the alife simulation does (not looked at).
Agreed with itrzone (2026-11-26, their option A, my choice as lead): the total stays a running sum of damage
dealt and is NOT capped; nothing ever falls, so missed and repeated reads stay harmless. Rejected, option B (a total
that falls as the creature heals): X-Ray would have to send the lowered figure back, the total becomes state with
two writers, and the player gains nothing because the shown health already follows the snapshot.
itrzone's side, theirs to register: the cap at 1.0 in the mod is removed; a healing puppet's health is rewritten
about once a second (an upward change is written from 1 % of full; damage is shown at once as now).
Offline, done (2026-11-26): `kj1/kill4h_stub.lua` (15e43dab…) and `kj1/kill4h_test.sh` (3909030d…) = the kill4
tests plus the check `total_above_one` (0.600, health set from 0.4 to 0.8 as healing, then a total of 1.100: applied
as 0.500, health 0.3, alive, not refused, not counted as cut) with two mutants (total capped at 1; total above 1
refused). Run on the REGISTERED reader kill4 (f19b28c0…, unchanged): 34 checks pass, 40 mutants caught. So the
reader for kj1 is kill4 as it is; no kill5.
itrzone's amendment 136 DRAFT (their desk-armd d8e6814): my byte copy `kj1/itrzone-amendment-136-draft.txt`, sha256
9f56a56f…, 64 lines, 5596 bytes: MATCH with what I agreed; its section 3 (my rules) is mine word for word in
meaning. The second match is on the REGISTERED text.
Correction to draft 5's derivation, from itrzone: the rig's hit is a share of FULL health dealt to the puppet's
CURRENT health, and the puppet dying is ITR2's death. So if S has not healed from 0.4 to above 0.5 in the 40 s, the
0.5 hit kills the puppet and the pose says dead = 1 at 70 s; my 0.01 stop never comes into it. I keep 0.6 and 0.5
and register that outcome as: check 5 FAILS by name ("S did not heal enough: dead at the second hit"), the later
steps for S are then not judged, M's are. Margin: needs a rise above 0.10 in 40 s; kw1 measured 0.40 in 40 s (0.05
in 5 s). The puppet can also stand up to about 0.01 under X-Ray's health (their one-a-second upward write).
Built (draft 6): `kj1/t2_xray_kj.sh` = the kw runner plus the keep watcher and `T2_STALL_PLAY=<s>` (stop the engine
once, <s> seconds after the utc_ms of the first new play pose the runner read; rows in `stall.csv`). Syntax
checked only; never run. Open with itrzone: whether their play clock can start before my runner is up (then the
first play pose I read is not their first).
Amendment 136 REGISTERED (ITRZone master 79dae83): my byte copy `kj1/itrzone-amendment-136.txt`, sha256
ac48771ca360e1eaacc6e1767d273eb5fbf4beba28adafd5a8d85a05a424e131, 64 lines, 5650 bytes: MATCH. Against the draft
only the title word and my own precision (offline: owed) differ; read by diff.
Play clock, settled (itrzone, from their code): their clock starts at 0 on the first puppet tick with an accepted
snapshot of mine, and no play pose is written before it; old pose and snapshot files are removed before ITR2
starts. So the first new play pose my runner reads is their first, and its header time is their clock 0 within one
frame. Their clock adds at most 0.25 s a frame, so a long frame makes their events LATER on the wall, never
earlier; in d2 the loss was nil. A hitch before 115 s moves the monster's kill toward the end of my stop, which the
2 s NO VERDICT rule covers (10 s of room at the end, 8 at the start).
Their run line: t2v=4 kcj=30 kcjs=0.6 kcjt=0.5 (defaults 40, 60, 85), no kcrig.
### Draft 7 corrections (2026-11-26), from itrzone's readiness review, each checked by me in my code
- MY SLIP, twice: the snapshot period is 100 ms, not 50 (reader line 673: an export every 100 ms; kw1's rows are
  100 ms apart). Wherever this draft says 50 ms between snapshots or 20 snapshots a second, read 100 ms and 10 a
  second. The chain of checks 7 and 10 is then pose 0.05 s + one frame + snapshot 0.10 s + read 0.05 s = 0.20 s plus
  one frame; the 2.0 s bound still allows a stall of over 1.5 s. Check 6's "one snapshot step" is 100 ms; 0.02 is
  still far under a hit and over 100 ms of healing (0.001). kw1's registered text has the same slip in two
  derivations; its bounds and result do not move.
- Stop timing: my runner reads no pose before its own first snapshot and then polls about once a second, so the
  first play pose it reads is 1 to about 2.2 s after ITR2's clock 0. The stop starts about 106 to 108.3 s after
  their clock 0, CONT up to about 128.3. Their kill at 115 is at least 6.7 s after STOP and about 11 s before CONT.
  (Draft 4 said 105 to 107.)
- What my `dead` line means for an ONLINE death: "the kill call was made", not "the creature was seen dead". The
  reader sets the death time in the step of the call. Read in the engine: the call has no refusal but an object
  already dying or being destroyed (Entity.cpp KillEntity); the death itself (health -1) is done when the death
  event is handled, later in the frame or the next, so a read-back in the same step would be wrong. The
  invulnerable flag of story characters is honoured in the hit path only (CustomMonster.cpp:854), not in the kill
  path. So I expect a story character to die by the call too; NOT seen in a game. If a kill ever did not take:
  check 11 fails, and after the 10 s hold the reader logs KSTALE alive_again and counts kstale (check 14 fails).
  On a story character the near-zero hit is ignored by the engine (invulnerable), the direct health change is not.
- Who is near the track's start: from kw1's boot dump (positions at boot, creatures move): stalkers about 27 m off
  are devushka, the story character esc_2_12_stalker_fanat, and four sim_default_stalker_0 (one is kw1's S); Wolf
  (esc_2_12_stalker_wolf) at 34 m. So S may be a story character. In a bench boot nothing is saved.
- Check 10's row contradicted itself (my text): "within 2.0 s of the first snapshot after CONT" and "a slow first
  frame fails". Settled with itrzone, both sides alike: the 2.0 s count from the FIRST KEPT SNAPSHOT READ AT OR AFTER
  CONT; the time from CONT to that snapshot is reported, not judged. A slow first frame fails neither side by itself.
- Fact, from a real run (out/t2pre2, a 300 s stop; I first wrote t2pre6, whose stop was absorbed and shows no jump): X-Ray's script clock (time_global) JUMPS by the length of an
  unabsorbed stop; only the frame delta is clamped. My stop here is not absorbed, so after CONT the script's time is
  20 s further on. For this run: S's 10 s `dead` hold has run out before the stop anyway; M's starts after it.
- The verdict rule (as itrzone's): a NO VERDICT never hides a failure it does not explain. Every NO VERDICT rule
  names the checks it leaves untested; a failing check outside that set makes the run a FAIL.
- Draft 5's "if no total above 1.000 was sent, NO VERDICT" is gone: S dead at the second hit, or no live total
  above 1.000, is a FAIL of check 5 by name.
- More pins (the runner's own dependencies): tools/run_noguard.sh 92729abc…, harness/zz_eperf_dump.script
  2b79a817…, out/user.ltx.orig 3efd38ee….
- New NO VERDICT rule, the same on both sides: S or M is not listed (offline or gone) in my kept snapshot at the
  time of the first pose of one of its later steps. Then the channel's online path was not tested for that step.

From their lever (`kcj`, built, not merged): if the picked creature's snapshot health is below 1 (say 0.95), a
later kill reports total = the part hits + what was left, not a round number. So no check here names a death total.
Proposed for the run, so both are judged in the engine: S gets 0.6 at 30 s and 0.5 at 70 s (total 1.100), kill at
90 s. Derivation: after 0.6 S stands at 0.4; 40 s at kw1's 0.01 a second gives about 0.8; after 0.5 about 0.3, alive
with a total above 1.000. If S did not heal, 0.4 - 0.5 stops at 0.01 by the registered rule, S still lives, and
check 5 fails by name.

## 4. PASS = all of these (X side). Every bound derived here; none copied.
| # | check (outcome) | bound | where it comes from |
|---|---|---|---|
| 1 | no crash, no FATAL, no Lua error naming the bridge script, runner rc 0, one boot | 0 | - |
| 2 | step A: one KAPPLY for S and one for M, difference = the pose's total, after = before - difference | +/- 0.0002 | both healths read in one script step; two 4-decimal roundings, doubled (re-derived: same code path as kw1, checked against kill5's source at registration) |
| 3 | no second effect: no further KAPPLY for an id while its total in `poses_all.txt` is unchanged | 0 | the rule |
| 4 | healing is left alone: between the first and the second KAPPLY of S, and between M's KAPPLY and its death step, no KAPPLY for the id and its snapshot health never falls from one snapshot to the next by more than 0.02 | 0.02 | nothing of mine may lower health between hits; 0.02 in 50 ms is far above rounding and regeneration noise and far below any hit of mine (0.3 and up). A fall beyond it is X-Ray's own world (a fight): then NO VERDICT by section 6, not FAIL, unless a K line of mine stands at that time |
| 5 | S healed and a total above 1.000 is applied: S's health in the last snapshot before the second KAPPLY is at least 0.15 above its health in the first snapshot after the first KAPPLY; the second KAPPLY has applied = the pose's total (1.100), difference 0.500, is not counted in `kclamped`, and S is alive after it with health above 0.02 | 0.15; 0.02 | kw1's rate gives 0.40 in 40 s; 0.15 is under half of that, so a slower class of stalker still passes, and it is far above the 0.02 noise of check 4: no healing at all cannot pass. Health above 0.02 after the hit means the 0.01 stop was not what kept S alive |
| 6 | only S and M change: every OTHER creature listed in the snapshot just before and just after each KAPPLY and each KDEAD has health not lower by more than 0.02 | 0.02 | one snapshot step is 50 ms; X-Ray's own fights can hurt others, so this is judged only for creatures within 10 m of S or M and REPORTED for the rest; 0.02 is far under any hit of mine (0.25) and over 50 ms of anything but a hit. A fail names the creature |
| 7 | step C: one `KDEAD,…,online` for S; `k,S,…,dead` in a snapshot within 2.0 s of the first pose with dead = 1 (both times from the watcher's own reads) | 2.0 s | chain pose write, script read once a frame, snapshot write, watcher read = 0.15 s plus one frame; 2.0 s allows one stall of 1.5 s. kw1 measured 0.101 s with a stand-in; ITR2's writer has the same 0.05 s period (their text), so the chain is the same; to be re-checked against their registered period |
| 8 | the kill is the actor's: one KWHO with killer 0 for S and for M | exact | the death callback's killer |
| 9 | step D: no KAPPLY or KDEAD with a script time inside the stop; no snapshot written inside the stop | 0 | the engine is stopped by signal; a line inside means the stop did not hold. Stop window from `freeze.csv` (the runner's own STOP and CONT rows) |
| 10 | step D: after CONT, one `KDEAD,…,online` for M and its `dead` line within 2.0 s of the first snapshot after CONT | 2.0 s | after CONT the newest pose is already on disk, so the chain is one script read and one snapshot write, plus the first frame after a 20 s stop, which may be long; 2.0 s as in 7. If the first frame after CONT alone is over 2.0 s the check fails and says so: that is a result |
| 11 | dead in X-Ray: from 2.0 s after each `dead` line the id is alive 0 or not listed, to the end | 2.0 s | the kill call's death event is handled on a later frame |
| 12 | the actor is unhurt: actor health in every snapshot = the first | exact | standing rule |
| 13 | the `r` line: present in every snapshot after the first v4 pose was taken, never back; within 2.0 s of the newest pose in `poses_all.txt`, EXCEPT snapshots are absent during the stop and the first snapshot after CONT may lag by the stop's length | 2.0 s outside the stop | as 7; inside the stop there is no snapshot to judge |
| 14 | counters, EVERY one in the last T2SUM judged, none skipped: khits (A: 2, plus one for each death step that still had a difference; exact number fixed from the rig's totals at registration), kdead_online 2, kdead_offline 0, kdead_fail 0, krefused 0, kstale 0, kback 0, kclamped 0, kver 0, kowed 0, ktabfail 0, torn 0, lost 0, epochs 0 | exact | from the steps. The checker takes its counter list from the T2SUM line itself: a counter in the line that the checker has no expectation for FAILS by name (so a new counter cannot go unjudged, as ktabfail did in kw1) |
Frame time: judged in 10 (through the 2.0 s) and REPORTED as the gap between consecutive snapshots: the largest
gap in each step and around each KAPPLY and KDEAD, from `snaps_all.txt`.

## 5. The checker and its tests
- Selftest: fixtures made from the REAL kw1 folder where they can be (its meta, its T2SUM), stub-made for the rest
  and labelled so.
- A mutant per check, each failing exactly its own check.
- The test that fails on purpose (Overseer's point 1): for EVERY counter in check 14, a folder with that counter one
  off must fail check 14 naming it; and one folder with an extra, unknown counter in T2SUM must fail by name. The
  selftest itself fails if any of these folders passes.
- kw1's own folder run through the new counter check as a real-bytes fixture (descriptive; kw1's verdict does not
  change).

## 6. NO VERDICT, decided before
S or M hurt by X-Ray's own world by more than 0.02 before step A or killed before its step; the rig cannot find a
stalker and a monster; S or M loses more than 0.02 between two snapshots with no K line of mine (X-Ray's own fight, check 4). One retry needs the Overseer's GO.

## 7. Guess: to be written at registration, after the build.
## 8. Process: as kw1 section 6; score once, output to a file in that call; no re-score after a checker fix
(Overseer's rule, kw1 section 7).
