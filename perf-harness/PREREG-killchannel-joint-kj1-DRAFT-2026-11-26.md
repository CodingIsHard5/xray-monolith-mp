# Kill channel, joint run "kj1", X side: DRAFT 1 (2026-11-26). NOT registered, nothing built, nothing run.

Ordered by the Overseer after kw1 (PASS 12/12). No boot until the Overseer has reviewed the registered text.
Limits first: a rig, not a player (itrzone's lever `kcrig` deals the damage); nobody looks; one stalker and one
mutant unless the rig can do more; no offline creature; no crossing; no restart of either side.

## 0. Open before this can be registered
- **[C] regeneration.** Overseer: by Caden's ruling ITR2's health is the authority on damage, so a bridged creature's
  X-Ray health must not drift above ITR2's total. Every mechanism for that changes feel: a creature the player
  wounded and did not kill never heals that damage (the registered total never falls within a session). Today a
  wounded stalker heals about 0.01 a second (kw1: 0.7500 to 0.8008 in 5 s), a flesh about 20 times slower. Sent to
  the Overseer for Caden. Section 3 is written for "stay wounded" and is PROPOSED only.
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
| B, T1 to T2 | nothing for at least 30 s | health stays at or under the ceiling (section 3) |
| C, at T2 | kills S | S dies, killer the actor |
| D, at T3 | pose says `freeze`: my runner stops X-Ray for 20 s; at TF inside the stop the rig kills M | nothing during the stop; M dies after the stop, once |
30 s in step B: derived, not copied. At the stalker's measured 0.01 a second, 30 s without a ceiling is a rise of
0.30, more than the whole 0.250 hit and 3000 times the check's bound; 5 s would also show it, 30 s shows it on a
flesh as well (0.0018 in 5 s gives about 0.011 in 30 s, 100 times the bound).

## 2. What is kept (new)
The watcher writes every COMPLETE snapshot, whole, to `snaps_all.txt` and every complete pose it sees, whole, to
`poses_all.txt`, each with its own read time. The checker reads these two files and the script's meta lines; it does
not need ITR2's log. Size: about 20 snapshots a second; at kw1's listing (to be measured from the first file) it is
tens of MB for the run. A missing or empty file fails by name.
Built and tried on made-up files (draft 2): `kj1/t1_watch_keep.py` (sha256 9df9c8e4…; = t1_watch.py plus the two
kept files; pose file named by env T1_KEEP_POSE). 5 snapshots kept of 5, a torn one not kept, CR stripped, a pose of
a new session with seq back at 1 kept. Not yet wired into the runner; not reviewed.

## 3. Regeneration: PROPOSED mechanism, the ceiling (X side only, no format change)
For every row with `applied` > 0 whose creature is online and alive: in the SAME script step that writes the
snapshot, before the `o` lines are built, if health > 1 - applied then `set_health_ex(1 - applied)`; logged
`KCEIL,<time>,<id>,<before>,<after>` at most once a second per id, counted (`kceil`).
- X-Ray's own damage below the ceiling still heals, up to the ceiling.
- New ITR2 session: `applied` is 0, so the ceiling is lifted (as registered: damage kept, totals restart).
- Why this and not "overwrite from ITR2": the pose carries totals, not health; an overwrite would need the puppet's
  health in the pose (a format change) and would also erase X-Ray's own damage. Why not "switch regeneration off":
  there is no script switch for it per creature that I have found; the ceiling needs none.
- Not covered: an offline creature (no health can be set offline); it is clamped at its first snapshot online.
- itrzone (no objection, 2026-11-26) asked what happens when THEIR row is retired without a new session (their 60 s
  unlisted rule, a section change): my row and its `applied` stay, so the ceiling stays, until a new ITR2 session or
  an X-Ray restart. A creature the player wounded stays under its ceiling even after ITR2 has forgotten it.
Bound for check 4: snapshot health <= 1 - applied + 0.0001. Derivation: the clamp and the snapshot's health read
are in one script step with no engine update between, so the only error is the two 4-decimal roundings (0.00005
each). A value above it means the clamp did not run or something raised health inside the step.

## 4. PASS = all of these (X side). Every bound derived here; none copied.
| # | check (outcome) | bound | where it comes from |
|---|---|---|---|
| 1 | no crash, no FATAL, no Lua error naming the bridge script, runner rc 0, one boot | 0 | - |
| 2 | step A: one KAPPLY for S and one for M, difference = the pose's total, after = before - difference | +/- 0.0002 | both healths read in one script step; two 4-decimal roundings, doubled (re-derived: same code path as kw1, checked against kill5's source at registration) |
| 3 | no second effect: no further KAPPLY for an id while its total in `poses_all.txt` is unchanged | 0 | the rule |
| 4 | the ceiling: in every snapshot from the first KAPPLY of an id to its death, health <= 1 - applied | + 0.0001 | section 3 |
| 5 | the ceiling was needed and worked on S: at least one KCEIL for S in step B, and S's health in the last snapshot of step B is within 0.0001 of 1 - applied or below it | - | without a KCEIL the check would pass on a creature that does not regenerate; S regenerated 0.05 in 5 s in kw1. If S shows no KCEIL in 30 s the run is NO VERDICT on the ceiling, not PASS |
| 6 | only S and M change: every OTHER creature listed in the snapshot just before and just after each KAPPLY and each KDEAD has health not lower by more than 0.02 | 0.02 | one snapshot step is 50 ms; X-Ray's own fights can hurt others, so this is judged only for creatures within 10 m of S or M and REPORTED for the rest; 0.02 is far under any hit of mine (0.25) and over 50 ms of anything but a hit. A fail names the creature |
| 7 | step C: one `KDEAD,…,online` for S; `k,S,…,dead` in a snapshot within 2.0 s of the first pose with dead = 1 (both times from the watcher's own reads) | 2.0 s | chain pose write, script read once a frame, snapshot write, watcher read = 0.15 s plus one frame; 2.0 s allows one stall of 1.5 s. kw1 measured 0.101 s with a stand-in; ITR2's writer has the same 0.05 s period (their text), so the chain is the same; to be re-checked against their registered period |
| 8 | the kill is the actor's: one KWHO with killer 0 for S and for M | exact | the death callback's killer |
| 9 | step D: no KAPPLY, KDEAD or KCEIL with a script time inside the stop; no snapshot written inside the stop | 0 | the engine is stopped by signal; a line inside means the stop did not hold. Stop window from `freeze.csv` (the runner's own STOP and CONT rows) |
| 10 | step D: after CONT, one `KDEAD,…,online` for M and its `dead` line within 2.0 s of the first snapshot after CONT | 2.0 s | after CONT the newest pose is already on disk, so the chain is one script read and one snapshot write, plus the first frame after a 20 s stop, which may be long; 2.0 s as in 7. If the first frame after CONT alone is over 2.0 s the check fails and says so: that is a result |
| 11 | dead in X-Ray: from 2.0 s after each `dead` line the id is alive 0 or not listed, to the end | 2.0 s | the kill call's death event is handled on a later frame |
| 12 | the actor is unhurt: actor health in every snapshot = the first | exact | standing rule |
| 13 | the `r` line: present in every snapshot after the first v4 pose was taken, never back; within 2.0 s of the newest pose in `poses_all.txt`, EXCEPT snapshots are absent during the stop and the first snapshot after CONT may lag by the stop's length | 2.0 s outside the stop | as 7; inside the stop there is no snapshot to judge |
| 14 | counters, EVERY one in the last T2SUM judged, none skipped: khits (A: 2, plus one for each death step that still had a difference; exact number fixed from the rig's totals at registration), kdead_online 2, kdead_offline 0, kdead_fail 0, krefused 0, kstale 0, kback 0, kclamped 0, kver 0, kowed 0, ktabfail 0, kceil >= 1, torn 0, lost 0, epochs 0 | exact but kceil | from the steps. The checker takes its counter list from the T2SUM line itself: a counter in the line that the checker has no expectation for FAILS by name (so a new counter cannot go unjudged, as ktabfail did in kw1) |
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
stalker and a monster; S shows no KCEIL in step B (ceiling not exercised). One retry needs the Overseer's GO.

## 7. Guess: to be written at registration, after the build.
## 8. Process: as kw1 section 6; score once, output to a file in that call; no re-score after a checker fix
(Overseer's rule, kw1 section 7).
