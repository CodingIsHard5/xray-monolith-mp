# Kill channel, joint run "kj1", X side: REGISTERED 2026-11-26, before any run

No boot before the Overseer has reviewed this text and
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
| 6 | only S and M change: no other creature within 10 m of S or M loses more than the bound across a KAPPLY or KDEAD (the snapshot before and the one after; an event without a snapshot on each side FAILS by name) | 0.02 | as 4. Within 10 m is judged because a hit of mine leaking to a neighbour would show there; farther creatures are reported (X-Ray's own fights) |
| 7 | S's death: one `KDEAD,…,online`; its `dead` line is read within the bound of the watcher's first read of the pose with dead = 1 | -0.05 to 2.0 s | chain: pose 0.05 s + one frame + snapshot 0.10 s + watcher 0.002 s = about 0.15 s plus a frame; 2.0 s allows a stall of over 1.5 s. -0.05: the watcher reads the snapshot before the pose in each 2 ms poll, so the script can have taken the pose first |
| 8 | the kills are the actor's: one KWHO with killer 0 for S and for M | exact | the death callback's killer |
| 9 | the stop held: no snapshot read inside the stop; the script's own RESUMED_GAP line matches it (its last frame before the gap within 1.1 s of STOP; its first frame after not before CONT - 1.1 s and not later than CONT + 6 s); no KAPPLY or KDEAD with a script time inside the gap | edges +/- 0.1 s; 1.1 s; 5 s + 1 s | STOP is stamped just before the signal and CONT just after; 0.1 s is far over a process start and far under 20 s. The script-time clause uses the script's own last frame before and first frame after the gap, exact. A second, wall-clock form of it has an error of 0 to 1 s (whole seconds in the script's lines) and counts an event only if it lies wholly inside. 1.1 s = the 0.1 s edge plus the 1 s of whole seconds. 5 s for the first frame after CONT: the bridge's own limit for calling a pose LOST (T2_LOST_MS); an X-Ray that needs longer has stalled on its own and that is no longer "the stop". Up to 5 s the delay is reported, not judged |
| 10 | M's death after the stop: one `KDEAD,…,online` for M at or after the first frame after the gap; M's `dead` line in no snapshot read before CONT; and read within the bound of the first kept snapshot read at or after CONT | 2.0 s | after CONT the newest pose is on disk, so it is one script read and one snapshot write; 2.0 s as in 7. Counted from the first snapshot, not from CONT: a slow first frame after the 20 s jump is REPORTED (CONT to first snapshot) and fails neither side by itself (agreed with itrzone, whose J6 counts the same way) |
| 11 | dead in X-Ray: from the bound after each `dead` line the id is alive 0 or not listed, to the end | 2.0 s | the kill call's death is handled on a later frame; kw1 showed alive 0 in the next snapshot |
| 12 | the actor is unhurt: actor health in every snapshot = the first | exact | standing rule |
| 13 | the `r` line: in every snapshot after the first pose was taken; never back; within the bound of the newest kept pose (pose header time); the first snapshot at or after CONT is allowed the stop's length more; an `r` line more than 0.05 s AHEAD of the poses the watcher had read fails too | 2.0 s; -0.05 s | as 7; inside the stop there is no snapshot |
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
  frame (reported up to 5 s, a FAIL of check 9 beyond); the first pose read after CONT is torn, which the reader counts as LOST (check 14);
- a GAMMA script names the weapon or nobody as the killer (check 8).

## 8. Process
Score once: `python3 -I tools/t3_kj1_check.py <X out dir> out=<X out dir>/t3_kj1_check.txt` (the checker refuses to
overwrite a report). No re-score after a checker fix: the registered checker's output stands, or NO VERDICT if it
gives none; a fixed checker's read is POST-HOC DESCRIPTIVE; a verdict that counts needs a new run under a
re-registered checker (the Overseer's rule, kw1). Stop on a crash, timeout or failed preflight; no second boot
without the Overseer.

## 9. Pins (sha256)
| file | sha256 |
|---|---|
| tools/t2_xray_kj.sh (runner; = the kw1 runner plus the keep watcher and T2_STALL_PLAY) | 870d854ffcf80f24cc0e3dfcc74247743fcd4c96a1d90cb9bbc9fa4050f60af3 |
| tools/t1_watch_keep.py (watcher) | 9df9c8e4eb2a34ec5b9059c0b26569b7d9e2e2886cc847c9630a8451e8240393 |
| tools/t3_kj1_check.py (checker; copy `kj1/t3_kj1_check.py`) | 4b8388505fa5847e5b4de27d70904027456843b0310979ef5b1a7944b09027ee |
| kj1/t3_kj1_mkfix.py (fixture maker) | b1ebf71e42afb3ca5debcaa4713280566a7e1928ce630194b39bf7f7d1e0c803 |
| kj1/t3_kj1_selftest.sh | ee74e8d12793723199c1d52f75328b53d9c56c90b0c332e285e730fe67bd3602 |
| tools/lib_engine_pids.sh | cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e |
| tools/tb_run.sh | 1b8d103f2208befc554c48cf0b628830e67e0223cbe5c99ab1fcc34205363953 |
| tools/run_noguard.sh | 92729abc420784053cee4b1b1832f5d112f74979a4760e2173d45578358455d5 |
| harness/zz_eperf_dump.script | 2b79a817a08a1e7ff4e29007f76677dba698a2a5945374331dcc2ae2de874b45 |
| out/user.ltx.orig | 3efd38eecbca504001cf03bd4c60bc6e837a3c3284e8517979a1bd9e1526de78 |
| harness/zz_p1_puppet_kill4.script (the registered reader, unchanged) | f19b28c094dabaab99e64b71716f696bdee200d46d37aa366e32b7eba5ae33af |
| run/bin/Stance14.exe | b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe |
| run/appdata/savedgames/cordon1.scop | 6dea752bc652a45c748e1438e3bc8ffd2d8d89f24730177c792328aeb5d13f3e |
| kj1/kill4h_stub.lua, kj1/kill4h_test.sh (reader's offline tests) | 15e43dab3ca6d26721928cfb79f4117bc0102a37415977f458e26007fa100226, 3909030dc94786a3159e62d9f83d0a71f9c7ea25af205f2f87d3db0d21048b43 |
The boot script is itrzone's and checks these hashes before the claim. I score from the X out dir it sends me.

Tests run by me on these bytes (2026-11-26):
- Checker selftest: PASS, 110 folders (`kj1/selftest_output_round4b_2026-11-26.txt`): 4 good; 40 mutants, each failing
  its own check and at least one for every check 1 to 14; 22 counter folders (every T2SUM key off by one fails
  check 14 naming it; an unknown key `kfoo` and a removed key fail by name: the tests that fail on purpose, the
  Overseer's point); 13 NO VERDICT folders; 2 where a NO VERDICT rule fires and an unrelated check fails (verdict
  FAIL); every required file missing, empty and unreadable (7 each); 8 unparsable records; 4 counters-only cases.
  The selftest itself fails if a deliberately bad folder passes.
- The counter table on kw1's REAL meta file: reads the bytes without fault; `ktabfail` is judged; fails only on
  krefused = 1, which kw1 had by design (`kj1/kw1_counters_only_v4_2026-11-26.txt`).
- The reader's offline tests with the check for a total above 1.000: 34 checks pass, 40 mutants caught.
- The runner copy: syntax check only. The watcher: tried on made-up files only. Neither has run with the engine.
Not tested: bounds exactly at their edges; several T2SUM lines; a session change; and NO fixture's kept snapshots,
poses or stall file come from the engine (the fixture maker writes them; each fixture's README says which lines are
real kw1 bytes).

CodeRabbit, two passes (2026-11-26), 28 findings. 16 taken into the checker and selftest, each with a fixture that
fails without the fix. Among them: a death at an unchanged total was dropped; non-finite numbers were accepted; a pose of
another version with 8 fields was accepted; a malformed stall file could still give a stop window; check 6 passed
on missing snapshots; unreadable files gave a traceback. NOT changed, with the reason:
- runner, code that ran arm D d0 to d2 unchanged (script install, claim check, cancel path, state reset at a
  boot, the planned kill, which is off here): I keep the runner's diff to the stop trigger and the watcher name;
- runner, the result of the STOP and CONT signals is not checked: a stop that did not hold fails check 9 on its outcome;
- watcher, one time stamp for the snapshot and pose read of one 2 ms poll: millisecond scale against 2 s bounds,
  and check 7's -0.05 s covers the order;
- watcher, no drain of the last pose at the end: the watcher outlives the engine;
- check 13's stop-length allowance: the watcher is never stopped, so a snapshot written before STOP is read before CONT;
- fixture maker's fixed path to kw1's folder; a selftest argument for it.

## 10. Added 2026-11-26 after the Overseer's review, before any boot: margin table and the story-character rule
No bound, check or pin above is changed by this section.

### 10.1 Margins: what I expect next to each bound
"Basis" says where the expected value comes from. kw1 = the X-only witness (one stalker, one flesh, stand-in writer).
| check | bound | expected | margin | basis, and how weak |
|---|---|---|---|---|
| 2 arithmetic | 0.0002 | 0.0000 | all of it | kw1: exact to 4 decimals on 6 hits |
| 4 fall between snapshots | 0.02 | 0 (health only rises) | all of it | kw1: both creatures rose monotonically over 5 s; WEAK for a stalker hit for 0.6 (bleeding or a wounded state not seen) |
| 5 healing between S's hits | at least 0.15 | 0.40 | 2.7 times | kw1: 0.05 in 5 s on one sim_default stalker at 0.75 and at 0.45 health; a rate at 0.40 health over 40 s is an extrapolation |
| 5 S survives the second hit (ITR2's rule) | healing above 0.10 | 0.40 | 4 times | as above |
| 5 S's health after the second hit | above 0.02 | about 0.30 | 15 times | 0.80 - 0.50 |
| 6 fall of a neighbour | 0.02 | 0 | all of it | kw1 kept no snapshots: NO measurement; from the code only (the reader touches only the ids in k lines) |
| 7 S's dead line after its pose | -0.05 to 2.0 s | 0.10 to 0.20 s | 10 times above; 0.10 s to the lower edge | kw1: 0.101 s with a 100 ms snapshot period |
| 9 last frame before STOP | 1.1 s | under 0.05 s plus up to 1.0 s of whole-second stamps | 0.1 s over the stamp error; about 2 times a frame of 0.05 s | the stamp error is the method's, not X-Ray's; a frame over 0.1 s just before STOP would FAIL this: FLAGGED as the thinnest margin |
| 9 first frame after CONT | 5 s (+1 s) | under 1 s | 5 times | WEAK: out/t2pre2 shows a prompt first frame after a 300 s stop, but that was the older script and no GAMMA timed scripts were looked at. FLAGGED: unknown |
| 10 M's dead line after the first snapshot after CONT | 2.0 s | 0 to 0.2 s | 10 times | the pose is on disk at CONT; itrzone's stand-in confirmed on the first snapshot (kc1) |
| 11 dead after the dead line | 2.0 s | 0.1 s (next snapshot) | 20 times | kw1 |
| 13 r line lag | 2.0 s; not ahead by 0.05 s | 0.05 to 0.15 s; 0 ahead | 13 times; 0.05 s | kw1 (stand-in): never over 2 s in 1001 snapshots; "ahead" never measured |
| 14 counters | exact | as listed | - | kw1 for the kill counters; lost 0 needs the first pose read after CONT not to be torn (rare, not measured) |
| NV4 M's dead pose inside the stop | 2.0 s from each end | 6.7 to 9 s after STOP, about 11 s before CONT | 3.3 times; 5.5 times | itrzone's clock and my runner's polling, both read in code |
| run length | X-Ray must see their clock 145 | ends about 240 | 95 s | d2: level load and their clock 0 under 1 s apart |
Under 2 times, or unknown: check 9's edge before STOP (thin by construction); check 9's first frame after CONT
(unknown); check 4 and 6 (expected 0, never measured in this situation). Checks 1, 3, 8, 12 are counts or exact.
Scorer selftest line, run by me on the pinned bytes:
`SELFTEST: PASS (110 folders: good 4/4; mutant 40/40; nv 13/13; counter 22/22; nvfail 2/2; missing 7/7; empty 7/7; unreadable 7/7; unparsed 8/8; counters-only 4/4; refuse-overwrite ok)`

### 10.2 Story characters (the Overseer's point; agreed with itrzone 2026-11-26 and built into their lever, amendment 137)
The nearest stalker to the rig may be Fanatic or Wolf. A story or scripted character failing checks 2, 5 or 11
would be GAMMA's script, not the channel. So the rig does not pick one: S is picked only among stalkers whose
snapshot section begins with `sim_default_` (the generic simulation stalkers; kw1's S was `sim_default_stalker_0`).
My checker is not changed and does not print the section: I read S's section by hand from the `k` line of the
kept poses (one grep, written into the result with the line). If the pick still names another
section, the run is reported as outside the registered conditions and S's checks are not scored (NO VERDICT for S).
Limit this adds: nothing here says how the channel behaves on a story character. That needs its own run.
If the rig finds no generic stalker (itrzone's line `KC joint pick FAILED … generic_stalkers_listed=0`), no `k`
line for a stalker is ever sent: my NV1 fires (not exactly one stalker and one monster) and the run is NO VERDICT
on both sides, "no generic stalker near the rig". My files cannot tell that case from "one was listed but the rig
picked none" (itrzone's J1 FAIL, their fault); there my side still says NO VERDICT and the joint run is a FAIL
by theirs.
My 5 s rule for the first frame after CONT (check 9) is NOT mirrored by itrzone: if it fails, the joint run is a
FAIL on my side only, and I say so.
itrzone's text read by me as DRAFT: `kj1/itrzone-amendment-137-draft.txt`, sha256 c251d3e1…, 139 lines, 14646 bytes
(desk-armd 75d1cf7). It is their half, not a joint format text; I checked every line that states something about my
side (files, env, the stop, the death's look, the three shared NO VERDICT reasons) and found no contradiction.

## 11. RESULT (2026-11-26, appended after the run; sections 1-10 unchanged, registered sha 8be749f5…)

**X side, of record: NO VERDICT (checker rc 3). Scored once, registered checker 4b838850…, nothing re-scored.**
Run: one boot on the Overseer's GO, booted by itrzone. X out dir /home/caden/Projects/ITRZone/out/runs/20261126-152750-t2kj1-xray
(runner rc 0). Score text: kj1/kj1_xscore.txt, sha256 ac6e2dbaf28388f27d6eb57f9100dde3fe8115faf8f190b8129f7ff1626d8eea (67 lines), written by the single scoring run.
ITR2 side (itrzone's own scoring, theirs to state): FAIL 6 of 8, J6 and J7.

Limits first:
- NV3 fired, so by the registered rule checks 2-11 and 14 are untested, although each printed PASS. No verdict counts.
- Why NV3 fired is a GAP IN MY CHECKER, not X-Ray's world hurting S or M. NV3 looks for a fall above 0.02 between two
  snapshots with no K line of mine between them. Both falls are the death itself, one snapshot after my KDEAD line:
  S snap 804 health 0.0000 (alive 0) -> 805 health -1.0000; M snap 963 health 0.0008 (alive 1) -> 964 health -1.0000.
  The rule has no exemption for the step to -1 after a kill, and no fixture had the death spread over two snapshots.
- My desk fact "the first snapshot after a kill reads alive 0, health -1" was WRONG for this run: the snapshot that first
  carries the `dead` line showed S at health 0.0000 and M still alive 1 at 0.0008. The `dead` line means "kill call made".
- That is also the cause of the ITR2-side FAIL: the mod confirmed M on the `dead` line while the same snapshot's `o` line
  read alive, and spawned the puppet again for one snapshot (0.10 s). A real defect of the joint text, on my side's line.
- POST-HOC DESCRIPTIVE (not a verdict): with the two death steps set aside, no NV rule would fire and all 14 checks
  printed PASS, T2SUM 19 keys as expected (khits 5, kdead_online 2, all refusal counters 0).

Story-character rule (10.2): `tr -d '\r' < boot0/watch/poses_all.txt | grep '^k,19425,' | cut -d, -f3 | sort | uniq -c`
-> `1200 sim_default_stalker_0`. S's section is inside sim_default_, so S is scored. M: id 23183 flesh_03a_normal.

Numbers reported (descriptive): S healed +0.357 between its hits (0.3999 -> 0.7571 in 40 s); the total above 1.000 was
applied as the difference 0.500, not cut; S dead line 11 ms after the first dead pose; M's dead pose read 7.7 s after
STOP and 12.3 s before CONT, nothing applied or written inside the 20.0 s stop; first snapshot 1 ms after CONT, M's dead
line 25 ms later; actor health 1.0000 in all 1948 snapshots; both kills credited to the actor (KWHO killer 0).
The margin-table unknown "first frame after CONT" measured 1 ms against the 5 s bound.

My guess was PASS 25 / NO VERDICT 30 / FAIL 45. Outcome NO VERDICT, but not for a reason I listed: I expected NV from
the scene (a creature hurt by the world), and it came from my own rule meeting a two-snapshot death.

Next, needs the Overseer: (1) joint amendment: X-Ray writes the `dead` line only once the creature reads dead (alive 0)
or is unlisted, and the mod confirms on that; (2) NV3 exempts the step to -1 after a KDEAD, with a fixture cut from this
run; (3) a new run under the re-registered text and checker. No second boot without the Overseer.
