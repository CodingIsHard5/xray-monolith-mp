# Kill channel, joint run "kj2", X side (2026-11-26). Registered by its commit; see the hash named in itrzone's amendment 138.

No boot before the Overseer has reviewed this text and itrzone's half of amendment 138, and given the GO.
Drafted by an Opus sub-worker (kj2/PREREG-killchannel-joint-kj2-DRAFT-2026-11-26.md, kept); read, re-run and completed by me.

Limits first:
- Everything kj1 said about limits still holds: a rig deals the damage (itrzone's lever `kcj`), not a weapon in a hand;
  nobody looks (no animation, sound or feel is judged); one stalker and one other creature; no offline creature, no
  crossing, no restart, one ITR2 session; the stop is made by my runner, not by a pose.
- The new reader (`kill5`) has run only in the offline stub. The stub's fake kill takes effect one frame later because
  kj1's real engine did that once (M, snap 963 -> 964); it is a model, not a reading of the engine source. Whether a real
  kill can stay pending longer than one frame is not known.
- The new checker has met real kept bytes once: the kj1 run folder (copied; the original untouched). That folder was
  written by `kill4`, so the checker's reading of `kill5`'s new lines (KCONF, KNOCONF, the `stale` word) has been tried on
  fixture bytes only, and on two KCONF lines I inserted by hand into a copy of kj1's meta file.
- The runner installs the reader by a fixed name. The kj2 runner `tools/t2_xray_kj2.sh` is kj1's runner with ONE line
  changed (line 54: kill5 instead of kill4; `diff` shows nothing else). It has had `bash -n` only; its other lines ran in kj1.
- A kill pending at a level crossing is not saved (no crossing in this run). A creature that reads alive again inside the
  10 s hold gets the plain line. Both are kill5's choices, tested offline only or not at all.
- Nothing here tells how the channel behaves on a story character (section 10.2, unchanged from kj1).

## 1. What it is for, and what changed since kj1
kj1 (registered sha 8be749f5..., result section 11 of its text) ended NO VERDICT on my side and FAIL on itrzone's. Both came
from one gap: my reader wrote `k,<id>,<applied>,dead` as soon as it made the kill CALL, while the engine handles the death
later. In kj1, M's `dead` line stood in snapshot 963 beside M's own o line with alive 1 at health 0.0008; M read dead (alive
0, health -1.0000) only in 964. The ITR2 mod confirmed M on 963's dead line, then saw M alive and spawned the puppet again for
one snapshot. My checker's NV3 then fired on the death step itself (S 804 -> 805, M 963 -> 964: health to -1 one snapshot
after the KDEAD line, with no K line between those two snapshots).
kj2 is the same run again with the fix on both sides. Formats: itrzone's amendments 134 and 136 as in kj1, plus joint
amendment 138 (version stays 4):
1. `k,<id>,<applied>,dead` only when, as the snapshot is built, the creature reads dead (its o line alive 0) or no object
   exists for the id. A kill call not yet in effect gives the plain `k,<id>,<applied>`. A dead line and an alive-1 o line
   for one id never stand in one snapshot.
2. New word `k,<id>,<applied>,stale` for an id listed that is not the creature of the line (another section, or alive again
   more than 10 s after its CONFIRMED death). kill4 answered those with `dead`. An id with no object keeps `k,<id>,0.000,dead`.
3. The `r` line is unchanged: it advances when the pose is processed, before the kill is in effect.
4. An online kill whose creature still reads alive 2.0 s after the call: logged `KNOCONF,<t>,<id>,<ms>`, counted in
   `kdead_fail`, not retried. The 10 s dead hold runs from the confirmation; the confirmation is logged `KCONF,<t>,<id>,<ms
   since the call>`. KDEAD and KWHO stay at the call, as in kj1.
What changed on my side and why:
| file | change | why |
|---|---|---|
| reader `harness/zz_p1_puppet_kill5.script` (copy of kill4) | points 1-4 above; nothing else | kj1's defect; amendment 138 |
| checker `tools/t3_kj2_check.py` (copy of the kj1 checker) | (i) NV3 exempts one death step per id; (ii) new check 15; (iii) wording of 7 and 10; (iv) KCONF/KNOCONF rows in check 14's table | kj1's NV3 fired on the death itself; 15 judges point 1 and 2 directly; the dead line can now come one snapshot later |
| reader tests `kj2/kill5h_stub.lua`, `kj2/kill5h_test.sh` | fake kill that can take effect later; 8 new checks, 15 new mutants; `stale_section` expects `stale` | test the new behaviour on outcomes |
| fixtures and selftest `kj2/t3_kj2_mkfix.py`, `kj2/t3_kj2_selftest.sh` | simulation follows kill5; copies of the REAL kj1 folder; checker mutants | prove the checker fix on the bytes that showed the defect |
| runner `tools/t2_xray_kj2.sh` (copy of the kj1 runner) | line 54 installs kill5 | the kj1 runner installs kill4 by a fixed path |
Caden's rulings behind the channel are kj1's (2026-11-26 16:50 and 18:56 UTC), unchanged.

## 2. The run (the same schedule as kj1)
Same as kj1 section 2, word for word in substance: one boot of each side, Cordon, headless (X-Ray on `:141`,
exists-refusal); itrzone's boot script holds the box, starts ITR2, then my runner. ITR2 (their text, as kj1):
`hcheal=2 t2span=4 t2gap=0 t2v=4 t2rec0=1 t2fc=1 kcj=30 kcjs=0.6 kcjt=0.5 t2stop=300 t2end=600`, away_sim keep; the rig
stands at the first 4 s of the Cordon track. Their play clock starts at the first puppet tick with an accepted snapshot.
| their clock | the rig | what must happen in X-Ray |
|---|---|---|
| 30 s | picks the nearest generic stalker S (`sim_default_*`) and the nearest other creature M with snapshot health >= 0.9; S takes 0.6 of full health, M 0.3 | each total applied once |
| 30 to 70 s | nothing | S heals at X-Ray's rate; nothing of mine touches it |
| 70 s | S takes 0.5 more (total above 1.000) | applied as 0.500; S alive |
| 90 s | kills S | S dies, killer the actor; dead line once S reads dead |
| 115 s | kills M, inside my stop | nothing during the stop; M dies after it, once; dead line once M reads dead |
My runner stops X-Ray by signal for 20 s, 105 s after the header time of the first new play pose it reads (STOP about
106 to 108.3 on their clock, CONT up to about 128.3). The stop is not absorbed: X-Ray's script clock jumps 20 s at CONT.
X-Ray ends first (240 engine seconds from level load); ITR2 ends at 300. Env as kj1: `T2_EXE=Stance14.exe T2_FREEZE=sigstop
T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_MAXRESTART=0 T2_LOST_MS=5000 T2_LOSTX_MS=120000 T2_OVER_S=120
T2_STALL_PLAY=105 T2_STALL_S=20 T2_RUN_S=240 T2_WALL_S=700 BENCH_DISPLAY=:141`. Runner: `tools/t2_xray_kj2.sh`.
kj1 measured: the stop was 20.002 s; M's dead pose was read 7.7 s after STOP and 12.3 s before CONT.

## 3. What is kept
As kj1: `tools/t1_watch_keep.py` keeps every complete snapshot (`boot0/watch/snaps_all.txt`) and every new pose
(`boot0/watch/poses_all.txt`) with read times; the runner writes `stall.csv`. The checker reads these, the script's meta
lines, the engine log and the runner's log, nothing of ITR2's. Snapshot period 100 ms.

## 4. PASS = all fifteen. Every bound is derived for this run (margin table, section 10.1).
S and M are found in the data as in kj1. Every expected total comes from the kept poses.
| # | check (outcome) | bound | where the bound comes from |
|---|---|---|---|
| 1 | no crash, no FATAL, no Lua error naming the bridge script, runner rc 0, one boot | 0 | - |
| 2 | first hit: one KAPPLY for S and one for M, difference = the pose's total, after = before - difference | +/- 0.0002 | two 4-decimal roundings, doubled |
| 3 | no second effect: every KAPPLY belongs to a NEW total in the kept poses | 0 others | the rule |
| 4 | healing left alone: between S's two hits, and between M's hit and its KDEAD, no KAPPLY for the id and no fall of its health over the bound with a K line of mine between the two snapshots | 0.02 | one snapshot step: healing 0.001, rounding 0.0001; smallest hit 0.3 |
| 5 | S healed by at least 0.15 between its hits; the total above 1.000 applied with the pose difference; S alive above 0.02 after it | 0.15; 0.02 | kj1 measured +0.357 in 40 s; 0.15 is under half; 0.02 is above the 0.01 stop |
| 6 | no other creature within 10 m of S or M loses more than the bound across a KAPPLY or KDEAD | 0.02 | as 4 |
| 7 | S's death: one `KDEAD,…,online`; S's `dead` line (now written once S READS dead) read within the bound of the watcher's first read of the pose with dead = 1 | -0.05 to 2.0 s | chain: pose 0.05 s + one frame + the kill taking effect, up to one more snapshot 0.10 s + snapshot 0.10 s + watcher = about 0.25 s plus frames; 2.0 s leaves a stall of over 1.5 s. -0.05: the watcher may read the snapshot before the pose in one 2 ms poll |
| 8 | the kills are the actor's: one KWHO with killer 0 for S and for M | exact | the death callback |
| 9 | the stop held: no snapshot read inside it; the script's RESUMED_GAP matches it (edges 1.1 s; first frame after CONT not later than CONT + 5 s + 1 s); no KAPPLY or KDEAD with a script time inside the gap | +/- 0.1 s; 1.1 s; 6 s | as kj1 (unchanged) |
| 10 | M's death after the stop: one `KDEAD,…,online` at or after the first frame after the gap; M's `dead` line (written once M reads dead) in no snapshot read before CONT, and read within the bound of the first kept snapshot read at or after CONT | 2.0 s | after CONT: one script read, the kill taking effect (kj1: one snapshot), one snapshot write; as 7 |
| 11 | dead in X-Ray: from the bound after each `dead` line the id is alive 0 or not listed, to the end | 2.0 s | kept from kj1 (not tightened; 15 judges the same snapshot) |
| 12 | the actor is unhurt: actor health in every snapshot = the first | exact | standing rule |
| 13 | the `r` line: present after the first pose taken; never back; within the bound of the newest kept pose; the first snapshot at or after CONT is allowed the stop's length more; not more than 0.05 s ahead | 2.0 s; -0.05 s | as kj1 |
| 14 | counters: EVERY key of the last T2SUM judged against one table (as kj1: torn 0, lost 0, epochs 0, paused false, kdead_online 2, kdead_offline 0, kdead_fail 0, krefused 0, kstale 0, kback 0, kclamped 0, kver 0, kowed 0, ktabfail 0, khits from the poses, last_seq between the last r and the highest pose seq, holds/near/guards = their own lines); plus two rows in the same table for kill5's new lines: exactly 2 KCONF lines, one per KDEAD online id, each later than its KDEAD, ms = its time - the KDEAD time, 0 <= ms <= 2.0 s; 0 KNOCONF lines | exact; 2.0 s | KCONF's 2.0 s: kill5's own KNOCONF limit, and 7/10's bound |
| 15 | NEW: no kept snapshot holds `k,<id>,…,dead` beside an o line alive 1 for the same id; no `stale` line in any kept snapshot | 0; 0 | amendment 138 points 1 and 2; this run has no stale case by design |
No T2SUM key was added. `kdead_fail` now also counts an online kill still alive 2.0 s after the call.
What a `dead` line means now: the creature read dead in that snapshot (or has no object). That it stays dead is 11's.

## 5. NO VERDICT, decided before. Each rule names the checks it leaves untested.
| rule | untested |
|---|---|
| NV1: not exactly one stalker and one monster with `k` lines | 2 to 11, 14 |
| NV2: S or M hurt by more than 0.02 before its first hit, or killed before its step, by X-Ray's own world | 2 to 11, 14 |
| NV3: S or M falls more than 0.02 between two snapshots with no K line of mine between them, EXCEPT its death step (below) | 2 to 11, 14 |
| NV4: the watcher's first read of M's pose with dead = 1 is less than 2.0 s after STOP or less than 2.0 s before CONT | 9, 10 |
| NV6: S or M is not listed in X-Ray's snapshot at the time of the first pose of one of its later steps | 3 to 11, 14 |
NV3's death-step exemption (exact rule, one step per id at most). Let T be the script time of the id's first
`KDEAD,…,online` line. Look at the consecutive pairs of kept snapshots that list the id, (p, q), with p written at or after
T (p's t_xray >= T), whose fall is more than 0.02 and which have no K line of mine between them (t_xray(p) < K time <=
t_xray(q)). The FIRST such pair is exempt if q lists the id alive 0 at health <= 0. If that first pair ends alive 1, or above
0, nothing is exempt for the id. An id with no KDEAD online has nothing exempt. So NV3 still fires on: a fall before the
KDEAD; a fall on a creature with no KDEAD; a fall after the kill call while the creature still reads alive (the world
hurting it before the kill takes effect), and then on the death step after it as well. kj1's two death steps (S 804 -> 805,
M 963 -> 964) are exactly what it exempts.
A NO VERDICT never hides a failure it does not explain: a failing check outside the fired rules' sets makes the run a FAIL,
named. Checks 1, 12, 13 and 15 are always judged. A missing, empty, unreadable or unparsable required file is a FAIL by name.
2.0 s in NV4: the same chain bound as 7. One retry needs the Overseer's GO. itrzone's side is not scored when mine is NO
VERDICT for S or M, and the reverse.

## 6. Reported, not judged
As kj1: CONT to the first snapshot; the largest snapshot gap in each step; health of creatures beyond 10 m at the hits; S's
and M's healing curves. New: each KCONF's ms (the time from the kill call to the snapshot that reads the creature dead) and
the exempt death steps (printed in NV3's line).

## 7. Guess
My outcome guess: PASS 45 %, NO VERDICT 20 %, FAIL 35 %. Named risks: a real kill staying pending longer than one
snapshot, or the engine writing KCONF/KNOCONF bytes my checker has only met in fixtures (FAIL 14 or 7/10); the death step
taking a form the NV3 exemption does not cover, e.g. three snapshots (NO VERDICT); another S healing under 0.15 (FAIL 5) or
the world hurting S or M (NO VERDICT); check 9's whole-second edges (1.4 times). kj1's guess was wrong in its reason; this
one may be too.

## 8. Process
Score once: `python3 -I tools/t3_kj2_check.py <X out dir> out=<X out dir>/t3_kj2_check.txt` (the checker refuses to
overwrite a report). No re-score after a checker fix: the registered checker's output stands, or NO VERDICT if it gives
none; a fixed checker's read is POST-HOC DESCRIPTIVE; a verdict that counts needs a new run under a re-registered checker
(the Overseer's rule, kw1; applied in kj1, whose NO VERDICT stands and is not re-scored by this checker: the kj2 checker's
reading of the kj1 folder in its selftest is a test of the checker, not a score). Stop on a crash, timeout or failed
preflight; no second boot without the Overseer.

## 9. Pins (sha256, "kj1" = unchanged from kj1's pin)
| file | sha256 | vs kj1 |
|---|---|---|
| tools/t2_xray_kj2.sh (runner) | f52da5c08e2852410f44307b9bc75a796db57832ce7a20d463c2d122c4d6a7ad | new: kj1's runner (870d854f…) with line 54 pointing at kill5, nothing else |
| tools/t1_watch_keep.py (watcher) | 9df9c8e4eb2a34ec5b9059c0b26569b7d9e2e2886cc847c9630a8451e8240393 | kj1 |
| tools/t3_kj2_check.py (checker) | 2a41b09c1c20eb181ad8c670aa061e882ee2e85e93f7deb39ad0035c34afb881 | new |
| kj2/t3_kj2_mkfix.py (fixture maker) | b5abbcd65ae29fce426de20241ecb8acecd1b7949506dc836874ddf15dd26f57 | new |
| kj2/t3_kj2_selftest.sh | a5e8b4685bde46e6c08fafccd5f9c92725a599a469bf91252896bab2ffc4e117 | new |
| tools/t3_kj1_check.py (old checker, run by the selftest for the proof) | 4b8388505fa5847e5b4de27d70904027456843b0310979ef5b1a7944b09027ee | kj1 |
| tools/lib_engine_pids.sh | cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e | kj1 |
| tools/tb_run.sh | 1b8d103f2208befc554c48cf0b628830e67e0223cbe5c99ab1fcc34205363953 | kj1 |
| tools/run_noguard.sh | 92729abc420784053cee4b1b1832f5d112f74979a4760e2173d45578358455d5 | kj1 |
| harness/zz_eperf_dump.script | 2b79a817a08a1e7ff4e29007f76677dba698a2a5945374331dcc2ae2de874b45 | kj1 |
| out/user.ltx.orig | 3efd38eecbca504001cf03bd4c60bc6e837a3c3284e8517979a1bd9e1526de78 | kj1 |
| harness/zz_p1_puppet_kill5.script (the reader) | f010a341db6a7b889c348db551d48bad33ae82a633927c21624f4b48b7ed70b9 | new (kill4 was f19b28c0…) |
| run/bin/Stance14.exe | b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe | kj1 |
| run/appdata/savedgames/cordon1.scop | 6dea752bc652a45c748e1438e3bc8ffd2d8d89f24730177c792328aeb5d13f3e | kj1 |
| kj2/kill5h_stub.lua, kj2/kill5h_test.sh (reader's offline tests) | 75f31bdc276355c0994966ca54a971b46903b5b58b12e30ed1ef1f3e1553a3e9, aed7e73de401cc5ddad0bb4b5d37e3f7dfa3c17e103ed5295dc84ddffdf7de01 | new |
| harness/zz_p1_puppet_lost3.script (the reader tests' v3 reference) | 5bf69e5ce0ed7d775efe2ab9755826dca43152e093f6fbc5b9909416f12641cb | not pinned in kj1 |
The boot script is itrzone's and checks these hashes before the claim. I score from the X out dir it sends me.

Tests on these bytes (2026-11-26; run by the sub-worker and run again by me with the same final lines):
- Reader offline tests: `== checks: 42 pass, 0 fail; mutants: 55 caught, 0 not caught or missing` / `RESULT: PASS`. 34 of
  them are kj1's; one (`stale_section`) now expects `stale` where kj1 expected `dead` (amendment 138 point 2), and two kj1
  mutants (`m_death_online`, `m_snap_hold`) anchor on kill5's changed text. kj1's stub run unmodified against kill5 fails
  only `stale_section`.
- Checker selftest: `SELFTEST: PASS (123 folders: good 5/5; mutant 45/45; nv 15/15; counter 24/24; nvfail 2/2; missing 7/7;
  empty 7/7; unreadable 7/7; unparsed 8/8; real 3/3; checker-mutant 4/4; counters-only 4/4; refuse-overwrite ok; old checker
  on the real copy NO VERDICT via NV3 (ok); real folder unchanged ok; ...)`. In it: on a byte copy of the real kj1 folder the
  OLD checker gives NO VERDICT via NV3 on S 804->805 and M 963->964 and the NEW one gives NV3 clear with those two steps
  exempt, FAIL on 15 (snap 963: dead beside alive 1, health 0.0008) and on 14.log.KCONF (kill4 wrote none); with 963's dead
  line made plain, only 14.log.KCONF fails; with the two KCONF lines kill5 would have written added, PASS. Four checker
  mutants (exemption removed; 15 removed; exemption widened to all falls; widened to every fall after the KDEAD) each caught.
Not tested: kill5 in the engine; a kill that stays pending in the real engine for more than one frame; KCONF/KNOCONF and the
`stale` word in bytes the engine wrote; bounds exactly at their edges; the kj2 runner's changed line (bash -n only).

## 10. Margins and the story-character rule
### 10.1 Margins: what I expect next to each bound (each bound derived for this run; kj1 = measured in the kj1 run)
| check | bound | derivation of the bound | expected | margin | basis |
|---|---|---|---|---|---|
| 2 arithmetic | 0.0002 | two 4-decimal roundings (0.00005 each), doubled | 0.0000 | all of it | kj1: 5 KAPPLY exact to 4 decimals |
| 4 fall between snapshots | 0.02 | healing in 100 ms 0.001 + rounding 0.0001, far under; smallest hit 0.3, far over | 0.0000 | all of it | kj1: largest fall 0.0000 for S (361 snapshots) and M (701) |
| 5 healing between S's hits | >= 0.15 | under half of kj1's measured rise; far over 4's 0.02 | 0.357 | 2.4 times | kj1: 0.3999 -> 0.7571 in 40 s (one stalker; another S may heal slower) |
| 5 S survives the second hit (ITR2's rule) | healing > 0.10 | the 0.5 hit on a puppet at 0.4 + healing | 0.357 | 3.6 times | kj1 |
| 5 S's health after the second hit | > 0.02 | above the 0.01 stop | 0.257 | 13 times | kj1: 0.2571 |
| 6 fall of a neighbour | 0.02 | as 4 | 0 | all of it | kj1: none in 7 events |
| 7 S's dead line after its pose | -0.05 to 2.0 s | chain about 0.25 s plus frames (section 4) | 0.011 s if S reads dead in the call frame (kj1: the 0.411 hit took S to 0.0000, alive 0 at once); about 0.11 s if one snapshot later | 18 times above; 61 ms to the lower edge | kj1: 11 ms; the one-snapshot case: kj1's M (101 ms KCONF) |
| 9 RESUMED_GAP's last frame before the gap vs STOP | 1.1 s | the 0.1 s stamp edge + 1 s of whole-second stamps | 0.795 s (kj1: whole second ...134 vs STOP ...134.795) | 1.4 times: FLAGGED | the method's own error (whole seconds), not X-Ray's |
| 9 RESUMED_GAP's first frame after vs CONT, lower side | CONT - 1.1 s | as above | -0.797 s (kj1: whole second ...154 vs CONT ...154.797) | 1.4 times: FLAGGED (not in kj1's table) | as above |
| 9 first frame after CONT, upper side | 5 s + 1 s | the bridge's T2_LOST_MS; + whole seconds | 0.026 s to the first new frame's snapshot (kj1, snap 963) | over 200 times | kj1 (see NOTES: the snapshot read 1 ms after CONT was the frame cut by the stop) |
| 10 M's dead line after the first snapshot at or after CONT | 2.0 s | as 7 | 0.125 s (kj1's bytes under kill5: snap 964 read 125 ms after snap 962) | 16 times | kj1, plus one snapshot (the kill in effect one frame later, as kj1's M) |
| 11 dead after the dead line | 2.0 s | kept from kj1 | 0 s (kill5: the dead line requires alive 0 in the same snapshot) | all of it | kill5's rule; offline only |
| 13 r line lag | 2.0 s; not ahead by 0.05 s | as 7 | max 0.068 s; 0 ahead | 29 times; 50 ms | kj1: 1848 snapshots, largest lag 68 ms outside the stop, smallest 0 |
| 14.log.KCONF ms | 0 to 2.0 s | kill5's KNOCONF limit = 7's bound | 0 to 0.1 s (S 0 if it reads dead at once; M about 0.1) | 20 times | kj1's M: one snapshot (101 ms) |
| 15 dead beside alive 1; stale lines | 0; 0 | the format (amendment 138) | 0; 0 | exact | kill5 uses one alive() read for the o line and the k line; offline tests only |
| NV4 M's dead pose inside the stop | 2.0 s from each end | as 7 | 7.7 s after STOP; 12.3 s before CONT | 3.8 times; 6.1 times | kj1 |
| NV3 exemption | one death step per id | section 5 | two exempt steps (S, M), nothing else | - | kj1's bytes: exactly S 804->805 and M 963->964 |
| run length | X-Ray must see their clock 145 | - | ends about 240 | 95 s | kj1 ran to the end |
Under 2 times, or unknown: check 9's two whole-second edges (1.4 times each, thin by construction of the stamp, both
passed in kj1); check 7's lower edge (61 ms, absolute); check 5's healing (2.4 times, one stalker measured); a kill pending
longer than one frame in the real engine (unknown; it would move 7, 10 and KCONF's ms toward their 2.0 s bound).

### 10.2 Story characters (as kj1, unchanged)
The rig picks S only among stalkers whose section begins with `sim_default_`. My checker does not print the section: I read
S's section by hand from the `k` line of the kept poses, with one command written into the result with its output:
`tr -d '\r' < boot0/watch/poses_all.txt | grep '^k,<S id>,' | cut -d, -f3 | sort | uniq -c`.
If it names another section, the run is reported as outside the registered conditions and S's checks are not scored (NO
VERDICT for S). If the rig finds no generic stalker, no stalker `k` line is sent: NV1, NO VERDICT on both sides.
