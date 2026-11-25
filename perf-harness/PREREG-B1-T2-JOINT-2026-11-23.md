# B1-T2 JOINT REGISTRATION (EnginePerf + ITRZone, 2026-11-23)

Two halves, both binding. Check names are prefixed: X- for EnginePerf (X-Ray side), I- for ITRZone.

## Part X (EnginePerf): perf-harness/PREREG-B1-T2-xray-side-2026-11-23.md
Sections 1-19 of that file, as committed with this file. Frozen hashes and env: its section 19. X-checks:
- X-kill (= I-T2h on ITRZone's side): planned SIGKILL at about track 700 s in r1; X-Ray back (first new-run_id snapshot)
  within 90 s; puppets hold meanwhile; resync (ITRZone measures the puppet side).
- X-noplan: no X-Ray restart other than the planned one (restarts.csv planned=0 rows FAIL it).
- X-freeze: under away_sim=freeze, freeze.csv STOP/CONT per death, game_ms CONT-STOP <= 1000 (mod 2^32), and one
  "[FREEZE] absorbed" engine line per freeze. Under keep, no STOP row.
- X-track: the X-Ray actor within 1 m of the pose at p95 (P lines; off-mesh holds counted and reported).

## Part I (ITRZone): ~/Projects/ITRZone/PREREG.md "## Amendment 93" to its end, VERBATIM, at ITRZone 5b593ca

## Amendment 93 (Overseer: B1-T2 GO, register jointly; offline pre-flights first, then ask for the box)
- Mod (lever t2=1, which implies t1=1):
  - Pose writer:
    - ITR2 rewrites /dev/shm/itrz_b1/pose.txt in place ("wb", LF, no rename) on a fixed 50 ms schedule.
    - Header: seq,<n>,<utc_ms>,<play|dead|away|end>,<X-Ray level name, e.g. l01_escape>,<keep|freeze>. Body: p,<x>,<y>,<z>,<yaw_rad>,<hp> in X-Ray
      metres (x = Y/100+ox, y = Z/100+oy, z = X/100+oz; yaw = rad(UE yaw)). Then end,<n>.
  - The pawn on the rig:
    - It plays P1's cordon_full_track.csv on a play clock that starts at the first accepted snapshot, runs only in
      freeend, and pauses while dead.
    - The pose is the pawn's XY read back, plus the track's Z.
  - States:
    - dead: from HP DEATH or the RespawnDeadPlayer stop, until our re-entry.
    - away: any other world change.
    - end: T0's end, written about 40 times.
  - away_sim: lever awaysim, else away_sim.txt, else keep (Caden's player setting).
  - Under T2 the pre-load stop destroys every puppet before the world changes.
  - The T1 reader takes the B1-T2 header (level, run_id):
    - A new run_id or level resets its last seq.
    - After a restart, X-Ray's warm-up (t_track < 0) is accepted but not applied, so puppets are held. `T1 resync` is
      logged when t_track >= 0 again.
    - The stale end is 300 s (t2end).
- Offline pre-flights, all PASS before registration (tools/t2_preflight.sh):
  - names: dup_names PASS.
  - T1 reader: CRLF and LF 60/60.
  - B1-T2, on the real main.lua with P1's real track and a real X-Ray snapshot:
    - writer at 20.0 Hz at both 60 and 45 fps; pose within 1 mm of the track; yaw equals X-Ray's atan2(dx, dz)
      convention; no torn reads; no CRLF;
    - the 20 s gap holds, then resumes;
    - dead, then play with freeze after re-entry; away; end;
    - reader: run A 30/30; restart warm-up held (0 of 5 advanced); resync; level reset.
  - The pre-flight found one defect, fixed before registration: the writer's "50 ms since the last write" rule gave
    16 Hz at 60 fps.
- Runs: one EXCLUSIVE joint claim each, via scripts/boot_t2.sh <arm>. Cordon, the headset levers, t2=1.
  - r1 core:
    - one 20 s ITR2 write gap at play 300 s;
    - X-Ray's 20 s SIGSTOP and the FORCED X-Ray kill with restart, both inside EnginePerf's t2_xray.sh (the kill is
      the planned=1 row of restarts.csv);
    - rig pawn healed.
  - r2 away_sim: two rig deaths at play 240 s and 480 s (wtest=6; the pawn is otherwise healed, and ChangeHealth -1000
    takes it to 0 at once). X-Ray runs with no stall, no planned kill, freeze by SIGSTOP, and 1500 s for the whole run
    (T2_STALL_AT=99999 T2_KILL_AT=99999 T2_FREEZE=sigstop T2_RUN_S=1500). EnginePerf's pre-flight 1 found that
    device():pause stops X-Ray's Lua too, so X-Ray could never unpause itself; T2_PAUSE is dropped. Instead t2_xray.sh
    SIGSTOPs X-Ray while the pose says dead or away with freeze and SIGCONTs it on play, logging <out>/freeze.csv
    (utc_ms,STOP|CONT,state,pid). Its 30 s no-new-seq restart rule is off while frozen.
- Joint naming: in the joint file EnginePerf's checks are prefixed X- and ours I- (I-T2a..I-T2i). Their forced-kill check
  is X-kill; ours is I-T2h. The first is under keep; the second is
    under freeze (t2flip=1 switches it at the first re-entry). Each death goes respawn, 20 s in the Hub, then load slot 1.
- ITR2-side checks (tools/t2_check.py, scored once per run):
  - Both arms:
    - T2a: write rate >= 95 % of 20 Hz in play, outside the gap.
    - T2b: write p99 <= 1000 us.
    - T2c: frame p95 <= 25 ms (r1 also max <= 250).
    - T2d: as T1b/T1c/T1e: latency p95 <= 300 ms; MemAvailable >= 8192 MB; swap growth <= 1024 MB; accepted >= 95 %
      outside the STOP..CONT window, each restart..resync window (restarts.csv), and each death..first accept after
      back-in-play window. X-Ray's per-boot files (boot<k>/) are merged.
    - T2e: clean. T2 end and T0 end present; no T2 FAIL or T0 FAIL; no dump; HP DEATH count 0 in r1, 2 in r2.
  - r1 only:
    - T2f: gap 19.5-21 s, no writes inside, the next write within 0.2 s.
    - T2g: X-Ray stall, as T1d.
    - T2h: the kill (the planned=1 row of restarts.csv; an unplanned restart is reported). A new run is logged;
      resync <= 90 s after the kill; the puppet count never drops through the gap; a
      T0 stat with waiting=0 within 2 s after the resync.
  - r2 only, T2i1 (keep) and T2i2 (freeze): firing under the right away_sim, then dead, then puppets destroyed at the
    stop, then back in play, then puppets again within 60 s.
- Predictions:
  - r1: T2a 90 %, T2b 90 %, T2c 85 %, T2d 75 %, T2e 75 %, T2f 90 %, T2g 70 %, T2h 50 %; all 30 %.
  - r2: T2a 85 %, T2b 90 %, T2c 80 %, T2d 65 %, T2e 60 %, T2i1 55 %, T2i2 50 %; all 25 %.
  - The deaths are the risk: puppets alive across a load are H2's crash class, now destroyed at the stop, which has
    not been proven yet.
- Stop rules:
  - Each run is scored once; a failure is reported, not tuned.
  - At most one retry per arm, registered first.
  - R3 (the zone crossing Cordon -> Garbage via ChangeLevel) is registered later as its own amendment, after r1 and r2.
    It needs the transit state written by our transit flow, which isn't built yet.
Amendment 93, Opus review (before any boot): 7 findings, all fixed.
- (1) X-Ray's files are per boot (boot<k>/watch/, boot<k>/meta_t2.csv). The scorer merges every boot, and the driver
  checks boot0's written.csv.
- (2) The kill is the planned=1 row of restarts.csv, not a stall.csv event. Every restart window is excluded until the
  next `T1 resync`.
- (3) r2 gets no X-Ray stall and no planned kill (EnginePerf's env knobs), and X-Ray's freeze is on (now SIGSTOP, see
  the r2 line; the pause support it first named was dropped after EnginePerf's pre-flight 1).
- Freeze check added after EnginePerf's pre-flight 1 (before registration): I-T2i1 (keep) also needs no STOP row in
  freeze.csv between its dead line and its `T2 back in play`; I-T2i2 (freeze) also needs a STOP with state dead|away in
  that span and a CONT with state play after it, at most 2 s after `T2 back in play`.
- Game time across the freeze (the Overseer's caveat; EnginePerf's pre-flight 2 saw 30 min of game time for a 5 min
  stop, and the Overseer approved their freeze-absorb compensation, default off, armed only during a freeze): I-T2i2
  also needs X-Ray's game time (freeze.csv column 5, game_ms, unsigned 32-bit, difference taken mod 2^32) to advance
  at most 1000 ms from that STOP row to that CONT row. Expected about 200 ms with the compensation, about 6000 ms per
  second of freeze without it. An empty game_ms FAILs. freeze.csv has a header line: utc_ms,event,state,pid,game_ms.
- EnginePerf's wineserver -k after the forced kill is scoped to X-Ray's own prefix (WINEPREFIX=<their run>/prefix,
  set explicitly); their pre-flight 3 measures the restart against the 90 s that I-T2h needs.
- X-Ray build and env (EnginePerf; t2_xray.sh's default exe stays RefDet14): both arms pass T2_EXE=Absorb14.exe
  T2_FREEZE=sigstop T2_ABSORB=2000, and r2 also T2_ABSORB_SKIP=0 (every freeze armed). boot_t2.sh refuses unless
  run/bin/Absorb14.exe's sha256 starts with the 16+ hex EnginePerf sends with the frozen t2_xray.sh hash; it re-checks
  both just before the call. The strings are confirmed with the frozen hash.
- (4) r2 keeps the rig heal (hcheal=1), so a creature can't cause an unplanned death that nothing respawns.
  ChangeHealth -1000 still kills, because the heal acts only at HP > 0.
- (5) The UTC clock offset is the maximum over every observed second boundary, not only the first, which a hitch could
  make low.
- (6) In r2, each death's excluded window ends at the first `T1 accepted` after `T2 back in play` (the floor rebuild),
  not 2 s after it.
- (7) The pre-flight now also runs the real wtTick(6) -> hpTick -> wsFire("RespawnDeadPlayer") -> Hub load -> re-entry
  -> t0Tick end path on stub engine objects: one kill, one respawn and one Hub load; a puppet is destroyed at the stop;
  dead, then play with freeze; T2 end, then T0 end, then "end" written 40 times. PASS.
- Also, from EnginePerf (8214f214e): the pose's zone field must equal X-Ray's level.name() (l01_escape for Cordon;
  otherwise their reader logs ZONE_MISMATCH). It is now mapped from our zone dir (cordon_full -> l01_escape,
  garbage_full -> l02_garbage), and the pre-flight checks it.

Amendment 93, Opus re-check of the fixes: clean. A synthetic two-boot run of the scorer gave latency matched per boot, accepted exactly outside the kill..resync window, and T2h found the planned row, the new run, the resync and waiting=0.

## ITRZone amendment 95 (verbatim from ITRZone PREREG.md at 4992007, line 4540 to end)

Amendment 95 (2026-11-23, before any boot of it): B1-T2 h1 (a short heal check) and r2, with the rig heal on every
frame and the track played only on X-Ray's AI mesh.
- r1b (am94, r1's one retry) stands as scored:
  - ITR2 side PASS, all 8 checks.
  - X side FAIL on X-track only (p95 209 m). X-kill, X-noplan and X-freeze PASS.
- The X-track FAIL is explained, jointly with EnginePerf. It is not a pose-path bug:
  - P1's track (out/p1/cordon_full_track.csv) leaves X-Ray's AI mesh. All 475 points at track time 410-600 s have
    node_dist -1, and 9 of the 75 at 380-410 s do.
  - That stretch is a synthetic straight leg: x is fixed at 10.50, y at 16.59, and z sweeps 706.8 -> 957.8 -> 706.8.
  - X-Ray's actor rightly held there. The pose path was checked against the track (pose_reconstructed.csv in the r1b
    run folder).
- r1's cause is corrected:
  - In r1b the rig pawn took 1,175 hits of 16 HP, and 1,170 of them came while AI-driven puppets were near it (T0 stat
    ai >= 1).
  - The ITR2-alone pre-flight, with no puppets, took none.
  - So the damage is the puppets attacking (the hybrid working as designed: ITR2 owns combat near the player), not
    falls. Am94's ground placement stays: it is harmless, and the pawn stands on ITR2's ground.
- Heal fix (lever hcheal=2, rig only; the Overseer):
  - The heal refills on ANY damage, every frame (when health is below the highest seen, minus 0.01).
  - It never heals inside a planned death window, from wtest's kill firing to the re-entry (STR.wt.busy), so the planned
    deaths happen. `HP heal paused: planned death window` is logged once per window.
  - Puppets keep targeting the pawn.
- Track span (lever t2span=380, rig only; the Overseer and EnginePerf):
  - The pawn plays only track time 0-380 s, ping-ponged: forward to 380, back to 0, and so on, for the same run length
    (t2Done unchanged). There is no wrap jump, and every pose is on the mesh (no point at or below 381 s has
    node_dist < 0).
  - `T2 span turn clk= track_t= forward|back` is logged at each turn. The yaw follows the direction of motion.
  - X-track stays exactly as registered.
  - Offline: tools/t2_span_test.lua runs the real main.lua and the real track for 1,100 s at 60 fps. Every row used is
    at or below 380.2 s, the largest step per frame is 5.6 cm, and the turns come at clk 380 and 760: PASS.
  - The T1 and T2 Lua pre-flights and dup_names PASS.
- h1, the short heal check (not part of the joint X checks), via boot_t2.sh h1:
  - ITR2 levers: hcheal=2 t2span=380 t2gap=0 t2end=30.
  - X-Ray: EnginePerf's frozen t2_xray.sh with T2_EXE=Absorb14.exe T2_FREEZE=sigstop T2_ABSORB=2000
    T2_STALL_AT=99999 T2_KILL_AT=99999 T2_RUN_S=600, so puppets attack for about 10 min.
  - Scored once by tools/t2_heal_check.py. PASS needs all of:
    - ITR2 seen alive, and `T2 end` present;
    - no `HP DEATH`;
    - every damage line followed by an `HP heal` within 1.0 s;
    - no FAIL line, and no dump.
  - With no damage at all, the result is INCONCLUSIVE, not PASS.
  - Applied to r1b's log, with the old heal, the check FAILs: 132 hits were not healed within 1 s, and health went as low
    as 20.
  - A FAIL is reported, not tuned.
- r2 as registered in amendment 93, with two changes: hcheal=1 becomes hcheal=2, and t2span=380 is added.
  - The planned deaths stay at play clock 240 s and 480 s (t2die=240, t2dien=2). On the ping-ponged track they fall at
    track time 240 s (forward) and 280 s (on the way back, 480 = 760 - 280). Both are on the mesh.
  - The play clock pauses while dead, so the second death is 480 s of play, not of wall time.
  - EnginePerf's r2 env and hashes are unchanged.
- Order: h1, then r2, each with one EXCLUSIVE claim.

EnginePerf, on the X-Ray side: amendment 95 above is diff-checked as identical to the source. I checked these claims on the track file: no row at or below 381 s has node_dist < 0; the track rows at t 240 s and 280 s have node_dist 0.43 and 0.24 (both on the mesh). r2 X-Ray env and hashes are unchanged. h1 is not scored on the X side.

## ITRZone amendment 96 (verbatim from ITRZone PREREG.md at bf31764, from "Amendment 96 (2026-11-23" to end)

Amendment 96 (2026-11-23, before any boot of it): B1-T2 r2's rig defect fixed, d1 (the death path on the real game,
ITR2 alone), then r2b, r2's one retry. Approved by the Overseer.
- r2 (scored once) stands: the ITR2 side FAILs. After the first planned death, the Hub load of manual save 1 did
  nothing, because that save did not exist: r2's start-only route never wrote it. The run idled to the cap.
  - Kept as evidence: a death with 40 live puppets under T2 went through the stop and the world change with no crash.
- Fixes:
  - (1) The r2 arms use out/zones/cordon_full_vis/route_t2r2.txt: route_t0's start waypoint with `reload`. That is a
    save-and-reload leg before play, as route_w.txt has, and it writes manual save 1 (healthy, Cordon). The driver
    builds the file from route_t0 and refuses to run without the reload waypoint.
  - (2) The death driver (wtest 5 and 6) logs `CheckSave manual 1 -> <b>` in its `load from Hub called` line.
  - (3) The way back has timeouts: 172 s from the kill to the Hub load, and 150 s from the load to the re-entry. On
    either, `WT FAIL k=<k> ...: run ended` is logged and the run ends (VIL.finish) instead of idling to the cap.
  - The Lua pre-flights and dup_names PASS. They stub the load call, which is why they missed r2's defect. Hence d1.
- d1, scripts/boot_t2solo.sh d1: one EXCLUSIVE claim, ITR2 alone with one static snapshot.
  - Levers: hcheal=2 t2span=380 t2gap=0 t2end=300 wtest=6 t2die=60 t2dien=1.
  - Scored once by tools/t2_d1_check.py. PASS needs these lines in this order:
    - `RELOAD save ... CheckSave manual 1 -> true`;
    - `WT k=1 kill firing`;
    - `HP DEATH`;
    - `T2 state play -> dead`;
    - `WC away in L_Hub`;
    - `WT k=1 load from Hub called ok=true CheckSave manual 1 -> true`;
    - `WC zone world ... re-entry`;
    - `T2 back in play`;
    - `T2 state dead -> play`;
    - `T2 end`.
  - It also needs exactly one HP DEATH, no FAIL line, no dump, and ITR2 seen alive.
  - Applied to r2's log, the check FAILs at "save written", as it should.
  - A FAIL is reported, not tuned.
- r2b: the r2 arm of amendments 93 and 95 unchanged, apart from the route. It is scored once with the same checks and
  is r2's one retry.
  - It waits for d1 to PASS, and for EnginePerf's word on their X-track fix: new hashes, or the Overseer's order to
    run as is. Any new hashes are frozen in boot_t2.sh before it queues.

EnginePerf, on the X-Ray side: amendment 96 above is diff-checked as identical to the source. X side for r2b: the pose on-mesh test is fixed (xray-side prereg section 24); zz_p1_puppet.script sha256 887f44889623bb44fb5a6e83f405a98925d5da0aa0cebadfd76c2d6e76784617; all other hashes and the r2 env are unchanged. X-freeze expects exactly one STOP..CONT pair (death 2, `freeze`); death 1 runs under `keep`. X-track is unchanged.

## ITRZone amendment 96 note (verbatim, last 6 lines of ITRZone PREREG.md at faa2f4c)

Amendment 96 note (before r2b): EnginePerf's on-mesh fix is frozen for r2b (their 7bbbdee07; approved by the Overseer).
- harness/zz_p1_puppet.script is now sha256 887f4488...84617 (the full value is in boot_t2.sh). Its hash on disk is
  verified. t2_xray.sh, lib_engine_pids.sh, Absorb14.exe and the r2 env are unchanged.
- boot_t2.sh r2b refuses to run unless the puppet script has that hash. It checks before the claim and again before the
  call.
- r2b waits for d1 to PASS and for EnginePerf's short X-only pre-flight of the new script.

EnginePerf: pre-flight 5 of the new script PASSED (xray-side prereg section 26). The X side is settled for r2b.

## ITRZone amendment 97 (verbatim from ITRZone PREREG.md at 6b7c865, line 4631 to end)

## Amendment 97 (Overseer after r2b): the CreepBig respawn fix, the checker cut at ITR2's end, r3

B1-T2 stands as FAIL on r2b as registered (Overseer). The evidence alongside: both death paths work, keep and freeze
(the freeze held X-Ray's game time to 198 ms), and the link accepted 9,827 of 9,827 snapshots while ITR2 was alive.
r3 is a new, separately registered run, not a retry of r2.
- Defect 1 (the mod, a player would hit it after any death): after a world change, every spawn of class CreepBig
  failed (7 in r2b); it spawned fine before. Suspected cause: T0 kept the class reference it got in the first world.
  CreepBig is a class we load ourselves, and the engine appears to drop it at the world change. Not proven; r3 tests it.
  - Fix: STR.t0Class looks the class up again at every spawn (at most 4 a frame) and loads it again if it is gone.
    Only "not loadable" is remembered, and only until the next world change.
  - New log line, one per stretch of play between world changes and one at the end:
    `T0 life k=<n> (<world change|end>) classes=<c> spawned/failed: <Class>=<spawned>/<failed> ...`.
    At each world change the per-life counts, the class notes and the per-creature fail marks are reset, so every
    creature gets a fresh try in the next world. The world-change teardown still makes no engine call.
- Defect 2 (the registration): r2's X-Ray run (T2_RUN_S=1500) is longer than ITR2's run, and T2d counted the 3,103
  snapshots written after ITR2's `T2 end` as misses. Fix: in arm r3, snapshots written after `T2 end` are not counted,
  and their number is printed. Applied to r2b's data as a checker self-test (not a score): 9,827/9,827, 3,103 not
  counted. r2b's score is unchanged.
- r3 = r2b's arm unchanged (levers, route, X-Ray env), scored once by `tools/t2_check.py r3`:
  - T2a, T2b, T2c, T2e, T2i1, T2i2 as in r2;
  - T2d with the cut above;
  - T2j respawn (new): exactly 3 `T0 life` lines (two deaths and the end); in every life every class listed has
    spawned >= 1 and failed == 0; every class that spawned in life 1 is listed in lives 2 and 3.
    Also no `T0 class <X> NOT loadable` line: a Creep stand-in would otherwise count as a spawn (review finding 1).
    Known limit, accepted: a class whose every creature has died in X-Ray before a re-entry would FAIL T2j without a
    defect. It would be reported as such, not re-scored.
- Offline first: tools/t2_preflight.sh now drops a self-loaded class at a stub world change and checks the reload,
  the two life lines and the resets. PASS, with dup_names and the span test. An Opus review of the diff stands in
  for CodeRabbit: 3 findings. Fixed: T2j fails on a not-loadable class; no life line after `T0 end`. The third is the
  known limit above.
- Then `scripts/boot_t2.sh r3`: one EXCLUSIVE claim via WANT, fair share, renewal, frozen hashes checked before the
  claim and before the call (the four of r2b unless EnginePerf sends new ones, which are verified on disk first).
  EnginePerf scores the X side. A FAIL is reported, not tuned. At most one retry, registered first.

EnginePerf, on the X-Ray side for r3: amendment 97 above is diff-checked as identical to the source. X-Ray env as in r2b. The puppet script changes (stray absorb arm fix, xray-side prereg section 28); its hash is confirmed to ITRZone only after pre-flight 6 (section 29) passes. X checks for r3: X-noplan, X-freeze and X-track as registered (scorer arg r2).

## ITRZone amendment 97 note (verbatim, last 4 lines of ITRZone PREREG.md at 2b97ad1)

Amendment 97 note (before r3): EnginePerf's end-state fix is frozen for r3 (their 0922f69ae; ordered by the Overseer).
- harness/zz_p1_puppet.script is now sha256 11c81c55...efffd (the full value is in boot_t2.sh), verified on disk after
  their pre-flight 6 PASSED. t2_xray.sh, lib_engine_pids.sh, Absorb14.exe and the r2 env are unchanged.
- boot_t2.sh r3 refuses to run unless the puppet script has that hash, before the claim and again before the call.

## ITRZone amendment 100 (verbatim from ITRZone PREREG.md at 8d73a2a, line 4713 to end)

## Amendment 100 (joint with EnginePerf): the first joint T3 pre-flight, arm j1. Registered; no boot before approval

A pre-flight, not the T3 run: does X-Ray read our version-2 pose in a real joint boot, ground its own actor and follow
the stance? EnginePerf's X-only pre-flights passed first (their prereg sections 35-39: grounding 6/6, stance 10/10,
no-bridge 8/8, level change 6/6).
- Run: `scripts/boot_t2.sh j1`. One EXCLUSIVE claim via WANT, fair share, renewal. ITR2 headless on :88, X-Ray on :77.
  - Our levers: `hcheal=2 t2span=380 t2gap=0 t2v=2 t2st=30 t2stop=380`. The on-mesh first 380 s of the track, played
    once; pose version 2; the stance steps 0, 1, 2 every 30 s of play clock (12 changes); no death, no gap, no freeze.
  - X-Ray env (EnginePerf): `T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999
    T2_RUN_S=640`.
- Frozen files of EnginePerf, sha256, each verified on disk at registration and checked by the driver before the claim
  and again before the call:
  - tools/t2_xray.sh 96160265a6c329e44046f01bf5b885e46c75df8ee58df3b7ef341cb724963136 (changed since r3: no heartbeat
    while the pose says `cross`)
  - run/bin/Stance14.exe b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe
  - harness/zz_p1_puppet.script 899c28e96805a9c8816a230d09b41cb40b05507b57d6b24ac477bb284939c485
  - tools/lib_engine_pids.sh cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e (unchanged)
  - Their scorer tools/t3_jpf_xray_check.py, sha256 starting 750209151c2a11eb (theirs to run; seen on disk).
- ITR2 side, scored once by `tools/t2_check.py j1` (PASS = all six):
  - T2a rate, T2b cost: as in B1-T2.
  - T2c frames: p95 <= 25 ms. The maximum is reported, not scored.
  - T2d link: as r3, with the cut at ITR2's end.
  - T2e clean: both end lines, no FAIL line, no dump, no death.
  - T3w writer (new): the end line says version 2 and stance lever 30; every play write's log line carries x, y, z
    and a stance 0..2; exactly 12 stance changes, each to (a + 1) mod 3, 29.5-30.5 s of play clock apart; every play
    write carries the stance in force at its seq (at most one write per change may differ).
  - Reported, not scored: `feet_miss_frames` (frames where our ground trace found nothing and the track height was
    sent instead), and the ground-difference line.
- X side, EnginePerf's, as they register it: rc 0 with no Lua error and no restart; at least 6,000 poses applied; no
  guard trip and no step over 5 m; at most 15 holds; the actor at node + 0.05 m on every pose; at least 10 stance
  lines; the export equals our pose's stance on every pose; the real crouch state matches on at least 80 % and follows
  every change within 2 s. Report-only: a VITALS line (lowest health, highest radiation and bleeding; this logging
  has not yet run in the engine) and the per-seq compare of our `T2 wrote` x, z, stance with their P2 lines.
- After the run they get the X-Ray out dir and our UE4SS.log lines `T2 wrote`, `T2 stance`, `T2 pose`, `T2 ground`.
- A FAIL is reported, not tuned. At most one retry, registered first.
- Offline before registration: the j1 checker was run on r3's log (FAILs T2e and T3w, as it must; r3 as r3 still
  PASSes), on a synthetic good log (T3w PASS) and on one with wrong stances (T3w FAIL). tools/t2_preflight.sh,
  tools/t3_pose_test.lua, the span test, dup_names and luac PASS; the mod is unchanged since 8d8cc1b.
- Not covered: a death, a freeze or a level crossing under version 2. Those belong to the T3 run and to R3.

## Amendment 103 (joint with EnginePerf): T3 proper, arm t3. Registered; no boot before the Overseer's go

The T3 run: the full on-mesh walk with pose version 2, the stance lever, and both death modes, X-Ray grounding its
own actor and taking no damage. j1 (amendment 100) passed on both sides first; EnginePerf's X-only pre-flight D passed
7 of 7 with the script frozen here.
- Run: `scripts/boot_t2.sh t3`. One EXCLUSIVE claim via WANT, fair share, renewal; about 30 minutes. ITR2 on :88,
  X-Ray on :77.
  - Our levers: `hcheal=2 t2span=380 wtest=6 t2die=240 t2dien=2 t2flip=1 t2v=2 t2st=30`. That is r3's run (the
    on-mesh 380 s walked back and forth for the track's full time, death 1 at play 240 s under keep, death 2 at 480 s
    under freeze, each back via the Hub and a save load) with pose version 2 and a stance step every 30 s of play
    clock. The play clock stands still while dead or away; the pose keeps its last position and stance meanwhile.
  - X-Ray env: `T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_RUN_S=1500`.
  - Mod blob 3a3b94a0. It contains the crossing and rig code of amendments 101 and 102, which does nothing in this run
    (no transit, levers off): the offline checks that pin that are below.
- Frozen files of EnginePerf, sha256, verified on disk at registration, checked by the driver before the claim and
  again before the call:
  - harness/zz_p1_puppet.script 1d7143cf7883bf6b0af1b82202cf442245411e4d41291d4ad3e58ec735330f45
  - run/bin/Stance14.exe b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe
  - tools/t2_xray.sh 96160265a6c329e44046f01bf5b885e46c75df8ee58df3b7ef341cb724963136
  - tools/lib_engine_pids.sh cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e
  - Their scorer tools/t3_xray_check.py fda8c5f8956814d02efa520117abbe8cb1dbc916d92b6fab749bf56379997ff5 (theirs
    to run).
- ITR2 side, scored once by `tools/t2_check.py t3` (blob 872da5a8), PASS = all nine:
  - T2a rate, T2b cost, T2c frames (p95 <= 25 ms; max reported), T2d link with the cut at ITR2's end, T2e clean with
    exactly 2 deaths: as r3.
  - T2i1 death under keep, T2i2 death under freeze (X-Ray's game time across the freeze <= 1000 ms): as r3.
  - T2j every creature class spawns in all three lives: as r3, with its known limit (a class extinct in X-Ray).
  - T3w writer: version 2 and stance lever 30 in the end line; every play write's log line carries x, y, z and a
    stance 0..2; the number of stance changes = floor(end play clock / 30) (about 36), each to (a + 1) mod 3 and
    29.5-30.5 s of play clock apart; every play write carries the stance in force at its seq (at most one per change
    may differ).
  - Reported, not scored: feet_miss_frames, the ground-difference line, and the pass over the 2.2 m object at
    x 62.6, z 167 (about 6 times; our feet height there is the object's top).
- X side, EnginePerf's, 14 conditions (their prereg section 43): j1's eleven with at least 17,000 poses applied, at
  most 45 holds and at least 25 stance lines; exactly one stop-and-continue pair with game time across it <= 1000 ms
  and one absorbed line; the no-damage gate: lowest health >= 0.999, highest radiation <= 0.0001, highest bleeding
  <= 0.0001, no unread value, every hit cancelled and no refill. Their stated limit: this route had no hit at all in
  j1, so the gate mainly tests creature hits during the two standstills. T3c (the two-floor spot at track 84.6-86.2 s)
  is report-only.
- After the run they get the X-Ray out dir and our `T2` log lines, as for j1. Each side scores once; each check is
  reported separately.
- A FAIL is reported, not tuned. At most one retry, registered first.
- Offline before registration, all PASS on this mod: tools/t2_preflight.sh, t3_pose_test, the span test,
  r3_cross_test, dup_names, luac. The t3 checker arm gives r3's log the same eight results as the r3 arm, plus T3w
  FAIL (r3 had no stance), as it must.
- Not covered: a real player's stance (the rig steps it by a lever), a level crossing (R3).

EnginePerf, on the X-Ray side for T3 proper: amendment 103 above is diff-checked as identical to the source (ITRZone 2d23e5e). The five hashes and the X env are mine and match the files on disk. X side: xray-side prereg section 43 (14 conditions, scorer tools/t3_xray_check.py fda8c5f8...97ff5), final as drafted; script frozen by section 44 after pre-flight D. Guess, written before the boot: X side PASS 50 %. Expected and report-only: the actor about 3 m below ITR2's feet height at x 62.6, z 167 on each pass (ITR2 sends the top of an object there; X-Ray grounds the actor on the AI mesh below it).


---

ITRZone amendment 107, copied verbatim from /home/caden/Projects/ITRZone/PREREG.md at ITRZone commit 98d3e24 (5107 bytes). X side: PREREG-B1-T2-xray-side section 51.

## Amendment 107 (REGISTERED 2026-11-24, joint with EnginePerf's section 51, f2e82c71f): the R3 pre-flight, arm x1

Registered before any boot; the box run needs the Overseer's go. The update notes at the end of this amendment carry
the X side's frozen hashes and conditions.
- Run: `scripts/boot_t2.sh x1`, two zones (out/zones/zones_t2.txt). Levers `hcheal=2 t2span=380 t2gap=0 t2v=2
  t2st=30 transit=1 txa=60 txn=esc_level_changer_to_garbage_2 txb=60 txm=gar_level_changer_to_escape_2 txe=60`.
  60 s of the Cordon track; the pawn is put into the changer; 60 s standing at the Garbage arrival; back by the
  paired changer; 60 s standing; end. No death, no freeze. X-Ray env as T3 with T2_RUN_S=480.
- ITR2 side, `tools/r3_check.py`, PASS = all eleven: T2a rate, T2b cost, T2c frames (p95), T2d link outside the
  crossing windows, T2e clean (also no TRANSIT FAIL or WC FAIL line), T3w writer, and:
  - R3a: two crossings by the two named changers in order, both ended; the rig arrived twice; pose states play,
    cross, play, cross, play, end; no dropped, recovered or still-open crossing.
  - R3b: each crossing at most 60 s from start to play (our part and X-Ray's reported).
  - R3c: no creature spawned between a crossing's start and its end; creatures present within 60 s after each.
  - R3h ends un-held: no `FREE HELD` line, no rig hold release, no hold rebase during play.
  - R3g ground: at most 10 track points without ground (r3 and T3 had 175 with the ring frozen; j1 had 8).
- The checker was run on j1's log (the six shared checks PASS as in j1, R3a-c FAIL as they must), on T3's log (R3h
  and R3g FAIL: 588 rebases, 175 points), on a synthetic good crossing log (all PASS) and with a 75 s crossing
  (R3b FAIL).
- Reported: the miss-reason lines (the seven fixed spots), level-mismatch snapshot count, life lines.
- Update 2026-11-24 after the watchdog witness (PASS 7/7, mod 2e37167, RESULTS): the mod for x1 is 2e37167 or later
  with no change to main.lua; the watchdog is live in this run, so R3h also proves it stays silent over two real
  transits. The hole sampler (the free follow) runs throughout; R3g is its check. In the witness the first 60 s of
  the track had one miss (t 25.9, trace hit an object 2.8 m above the track), so R3g's limit of 10 stands.
- S3 (foreign travel) is not in x1. It needs a route into one of ITR2's own levels and back, which is a different
  rig leg and a second kind of failure in a run meant to prove the crossing; it stays a separate short ITR2-alone
  arm, to be registered if the Overseer wants it.
- X side: EnginePerf's conditions as sent (pre-flight D's seven, the stance read-back, the vitals gate on each of
  the three VITALS lines), with their script frozen after pre-flight E2 (no-damage rule shown, 6/6).
- Registration 2026-11-24 (this replaces "Hash: pending" and the mod line above):
  - Mod: the commit that carries amendment 109 (main.lua gains only the test lever wtest=7, off here; blob printed
    in the driver's claim line). Pose format and crossing code unchanged since T3 and amendment 104.
  - X-side files, all verified on disk by sha256 today and checked again by the driver before X-Ray starts:
    zz_p1_puppet.script 473dec259dc3048924b262640ee8132e84963c33fa05bc6abd56a2156e643537;
    t2_xray.sh 6597a067a1797e051e874bd4a5feee2764277860e0c9795a254719db7042a3d7 (new: removes a stale test-mode
    control file at start); Stance14.exe b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe;
    lib_engine_pids.sh cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e.
  - X env: T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_RUN_S=480.
  - X conditions (EnginePerf's, scored once by them with tools/t3_x1_xray_check.py c19740e8...2518c), PASS = all nine:
    1. rc 0, no Lua error naming the script, no FATAL ERROR, no DMG line.
    2. Exactly two CROSS_REQ, in order, each through the named changer with pre-flight D's destination and vertices;
       no CROSS_NOCHANGER or CROSS_ERROR.
    3. Exactly two CROSS_RESUME, on Garbage then on Cordon, each after its request.
    4. For each crossing, the first applied play pose was written by ITR2 at most 90 s after the cross pose.
    5. P2 lines: at least 400 before the first request, at least 600 after each resume; no GUARD; at most 15 HOLD.
    6. The first P2 after each resume is within 5 m (x, z) of that changer's destination.
    7. The runner logged exactly two cross holds; restarts.csv has no row.
    8. On every P2: stance read back = pose stance, movement state readable, actor height = node height + 0.05 m
       (within 0.01).
    9. Exactly three VITALS lines, each with lowest health >= 0.999, highest radiation and bleeding <= 0.0001,
       0 unread; no REFILL line.
  - Scored once, each check separately, both sides. A FAIL is reported, not tuned. No retry without a new registration.
  - Guesses: ITR2 side PASS 55 % (first real transit under the bridge started from the rig's placement; first real
    crossing end against a live X-Ray); EnginePerf wrote X PASS 65 %.


EnginePerf note on the copied text (not part of it): "pre-flight E2 (no-damage rule shown, 6/6)" means the protected boot, which passed 6/6. E2 as a whole is a FAIL by its rule (section 50); the Overseer ruled the substance shown and no E3.


---

ITRZone amendment 111, copied verbatim from /home/caden/Projects/ITRZone/PREREG.md at ITRZone commit 709bf8f (2964 bytes). X side: PREREG-B1-T2-xray-side sections 51, 52 and 52a.

## Amendment 111 (REGISTERED 2026-11-24, Overseer's three conditions after x1): the R3 pre-flight as a new run, arm x2

This replaces amendment 110's plan to run x1 again. Amendment 110's mod block is superseded by the routine below.
1. The dump was read (RESULTS): a dead engine object read from the mod's Lua on the game thread; the puppet table by
   code order. Not provable further without symbols.
2. The sweep (RESULTS) and its fixes, all in main.lua:
   - `STR.worldGone(why)`: one routine for both level-change paths (wcTeardown and the transit's zone switch). Plain
     Lua. Drops the puppet table with its counts, a nav-floor build in progress, the nav-floor reference, the light
     snapshot (light=2), the god flag and the watchdog timers. Logs
     `WORLD gone (<why>) world=<n>: puppets before=<N> after=<M>; nav build dropped=<b> floor ref dropped=<b>`.
   - `STR.t0Stale()`: every puppet remembers the world it was spawned in; before any puppet is touched, those of
     another world are dropped untouched with `T0 FAIL stale puppets: ...` (a FAIL line: T2e fails on it).
   - The health watch and the light poll do not run while our transit waits for the new level.
   - Lever `t0keep=1` (test only, off by default): the puppet clear is skipped. Not used in any box run.
3. The witness:
   - In the run: new check R3p in tools/r3_check.py. One `WORLD gone (transit)` line within 1 s after each
     `TRANSIT zone now` line, two in all, each with after=0, the first with before > 0; no `T0 FAIL stale puppets`
     line; no t0keep line.
   - Offline mutation: `tools/t0_world_selftest.sh`. The real main.lua with stub actors that count every read
     (tools/t0_world_test.lua): with the clear, 3 dead puppets and the twins are dropped with 0 reads and R3p PASSES;
     with the clear skipped (t0keep=1) the line shows after=3, the guard drops them with 0 reads, and R3p FAILS.
     The stub does see a read when the old path (t0Kill on a dead puppet) is taken. The committed mod of x1 fails
     the test. Limit: the stub shows the logic, not the engine's behaviour.
- Run: `scripts/boot_t2.sh x2` = x1's levers, files and env exactly (amendment 107: EnginePerf's four hashes, their
  nine conditions, their one retry registered in their section 52). Our side: the eleven checks of amendment 107 plus
  R3p, twelve in all, scored once, each separately. The driver stops ITR2 if a crash dump appears (amendment 110).
- If ITR2 crashes again at an arrival: the cause was wrong or incomplete; the dump is read first, no third run is
  asked for before that.
- Offline at registration: luac, dup_names, t2_preflight, span, pose, crossing tests and the new self-test PASS. The
  checker run on x1's data gives the same eleven results as scored, and R3p FAIL (no line).
- Guess: ITR2 side PASS 45 %. Still unseen: puppets spawning in the Garbage, the first crossing's end against X-Ray,
  the way back, 60 s per crossing with X-Ray's part included.


---

ITRZone amendment 112, copied verbatim from /home/caden/Projects/ITRZone/PREREG.md at ITRZone commit 619d1ea (2704 bytes). X side: PREREG-B1-T2-xray-side section 54.

## Amendment 112 (REGISTERED 2026-11-24, Overseer's order after x2): two instruments fixed, the R3 pre-flight as a new run, arm x3
x2 (amendment 111) stands as scored: ITR2 side FAIL 10 of 12 (T2c, R3c), X side PASS 9 of 9 (EnginePerf's section 53).
Both FAILs were instruments. They are fixed here before the next run, not applied back to x2.

**Changed (scorers only; the mod, the levers, the X side and its four hashes are unchanged, mod blob 60c1c87):**
1. **T2c reads the summary line.** `t2_check.t0_summary` matches `T0 end ticks=<n> ` exactly; used by t2_check and
   r3_check (T2c, and T2e's "T0 end present"), and the same match in t0_check and t1_check. The old match took the first
   line starting `T0 end `, which in x2 was a per-puppet `T0 end id` line.
2. **R3c reworded to what it was meant to catch:** FAIL if a creature of the OLD level spawns or updates after the transit
   starts. Per crossing: (a) a `T1 level <old> -> <destination>` line follows the `T2 cross start`, with no `T0 spawn`
   between the two; (b) from the start to the next crossing's start (or the run's end) no per-puppet line
   (`T0 <event> id <n>`) carries an id spawned in the level just left; (c) no `T0 FAIL stale puppets` line. Kept: a
   `T0 stat` with puppets > 0 within 60 s after each crossing's end. Destination-level spawns after the `T1 level` line
   are allowed, also in the tick that ends the crossing.
   Known limit: a creature that really walks from one level to the other inside the 60 s stay would fail (b). If that
   happens it is reported as such, not re-scored.
3. **Self-test `tools/r3c_selftest.sh`** on x2's real log (local): the summary reader must read p95 18.02 and T2c must
   PASS on it; R3c must PASS the log as it is and FAIL three mutants (an old-level creature's spawn injected in the
   Garbage; a spawn injected after the transit start, before X-Ray names the destination; a handoff line of an old-level
   creature). Result at registration: PASS.

**The run, arm x3** (`scripts/boot_t2.sh x3`): identical to x2 in every lever, hash and X environment. Scored once by
`tools/r3_check.py` with the twelve checks T2a, T2b, T2c, T2d, T2e, T3w, R3a, R3b, R3c (as reworded), R3h, R3g, R3p.
PASS = all twelve, and EnginePerf's nine X conditions on their side. One retry only for a run that dies before its data,
registered first. A crash at an arrival: read the dump before anything else.

Guess before the run: ITR2 side PASS 65 %. x2's data passes all twelve under the fixed scorers, which is why this is not
higher than it looks: that reading is post-hoc and one run. The main risks are a crossing over 60 s (x2: 48.3 and 49.0)
and an arrival crash that x2 happened not to hit.

ITRZone amendment 113, copied verbatim from /home/caden/Projects/ITRZone/PREREG.md at ITRZone commit d6f9194 (2797 bytes). X side: PREREG-B1-T2-xray-side section 56.

## Amendment 113 (REGISTERED 2026-11-24): R3 proper, arm A: the other changer pair and a long stay, arm a1
Ordered by the Overseer after x3's joint PASS (order A, B, C, D; each arm registered and its box run asked for separately).
Desk draft: `docs/design-notes/2026-11-24-r3-proper-draft.md`.

**The run** (`scripts/boot_t2.sh a1`, joint, headless):
- The mod is x3's, unchanged (blob 60c1c87). Levers as x3 except the rig's: `txa=120 txn=esc_level_changer_to_garbage_1
  txb=300 txm=gar_level_changer_to_escape_1 txe=120`. So: 120 s of the Cordon track, out by the pair's first changer,
  300 s standing at the Garbage arrival, back, 120 s standing at the Cordon arrival, end. Two crossings, about 540 s of play.
- Changed from the draft: one out-and-back, not two. The second, by pair `_2`, would need a rig change, and x3 covered that pair.
- X side (EnginePerf): the same five files as x3 (script 473dec25…, runner 6597a067…, Stance14.exe b4ce9c77…, lib
  cb183b7b…, checker c19740e8…); env as x3 except `T2_RUN_S=600` (X-Ray counts its run from each level load; the Garbage
  stay is about 350 s). Their changer table holds pair `_1`; their nine conditions carry over with the two names.
- No Garbage track: the hole sampler runs on the Cordon leg only. The Cordon track came from our own `tools/p1_track.py`;
  a Garbage track and a rig that walks it are a mod change and go into arm B's registration.

**Scored once** by `tools/r3_check.py <run> <x out> 1` (the third argument names the pair; with it x3's data fails R3a, as
it must). The twelve checks of amendment 112, unchanged in wording: T2a, T2b, T2c, T2d, T2e, T3w, R3a (pair `_1`), R3b
(each crossing at most 60 s), R3c, R3h, R3g, R3p. PASS = all twelve, and EnginePerf's nine conditions on their side.
One retry only for a run that dies before its data, registered first. A crash: the dump is read before anything else.

**R3c and a creature that walks across** (EnginePerf, 2026-11-24): snapshots list only creatures on X-Ray's current level,
and ids stay the same across levels. So an id that was spawned in the level just left and shows up after the `T1 level`
line is a creature that really crossed. R3c part (b) still fails on it, as registered. If that happens it is reported
with the id and both snapshots, as a walker and not a leak, and the score is not changed.

**Expected and not scored:** the two known trace misses on the Cordon leg (t 25.9 and 109.8); about 18 stance steps; X-Ray
running about 8 minutes after our end (its run counter restarts at the last arrival).

Guess before the run: ITR2 side PASS 70 %. Risks: pair `_1` has never been used by the rig (its arrival points, the ring
there, a crossing over 60 s); 300 s standing in the Garbage with creatures around (the heal lever is on); a walker.

ITRZone amendment 114, copied verbatim from /home/caden/Projects/ITRZone/PREREG.md at ITRZone commit c59bc82 (3506 bytes). X side: PREREG-B1-T2-xray-side section 59.

## Amendment 114 (REGISTERED 2026-11-24): arm A again as a new run, arm a2
Overseer's ruling after arm A's joint FAIL (ours 11 of 12 on R3c, a walker; X side 6 of 9 on its runner's wall limit):
re-register arm A as A2, same route and stays, with the spawn log uncapped, R3c judged by X-Ray's own record, and
EnginePerf's wall limit fixed. Arm B follows after A2.

**Changed on our side:**
1. **Mod, one log line** (blob 7441261): every puppet spawn is logged (the cap of 80 is gone), with `lv` = the level named
   by the snapshot it came from, `zone` = our zone's level, `w` = our world number. No behaviour change.
2. **R3c, new rule (`tools/r3_check.py`, function `r3c`):** every creature we spawn is one X-Ray itself has on our level.
   (a) every `T0 spawn id` line carries the three fields and lv == zone; (b) for every spawn line, X-Ray's record (the X
   out dir's `boot0/meta_t2.csv`: ON, OFF, CROSS_RESUME and G lines, read by `xrecord`) has that id online on that level
   at the line's time, within 3 s; (c) the number of spawn lines equals `spawned=` in the end summary; (d) per crossing
   a `T1 level <old> -> <destination>` line follows the start, with no spawn between; (e) no `T0 FAIL stale puppets`
   line; (f) puppets > 0 within 60 s after each crossing's end. A creature that walks between the levels passes.
   If X-Ray restarts (a `boot1`), spawns after the restart are not in `boot0`'s record and fail (b): reported as such.
3. **Self-test `tools/r3c_selftest.sh`** (PASS at this draft): on arm A's real log with its 80 spawn lines given the
   new fields and arm A's real X-Ray record: all 80 real spawns, the walker 19518 among them, are online on our level
   in X-Ray's record (count not scored): PASS; the same with the count scored: FAIL (80 lines, 110 spawned: the cap is
   caught); four mutants FAIL: a spawn past spawn 80, on Cordon, of a creature X-Ray only had on the Garbage; a spawn
   whose snapshot level is not our level; a spawn before X-Ray names the destination; a spawn line without the fields.
   The T2c reader test on x2's log is kept.

**The run** (`scripts/boot_t2.sh a2`): levers, route and stays exactly arm a1's (amendment 113). Scored once by
`tools/r3_check.py <run> <x out> 1`: T2a, T2b, T2c, T2d, T2e, T3w, R3a (pair `_1`), R3b, R3c (as above), R3h, R3g, R3p.
PASS = all twelve and EnginePerf's nine conditions. One retry only for a run that dies before its data, registered first.

**X side (EnginePerf, their section 59):** runner `tools/t2_xray.sh` sha256
296563fde4a756882eea024593117a06cfff528ffd38fb085bc3d83a3106c3a7 (was 6597a067…: the launcher's wall limit is now set from
the schedule, and the engine is ended if it still runs 600 + 120 s after the last level load). Unchanged from arm A:
script 473dec25…, Stance14.exe b4ce9c77…, lib_engine_pids.sh cb183b7b…, checker c19740e8…. Env: `T2_EXE=Stance14.exe
T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_RUN_S=600 T2_WALL_S=1660 T2_OVER_S=120`. Their
offline test on arm A's schedule: the old runner times out at 1,001 s as in arm A, the new one ends cleanly at 1,138 s.
Their nine conditions as in arm A. X-Ray runs about 8 minutes after our end; expected. Their guess: X PASS 75 %.

Guess before the run: ITR2 side PASS 75 %. Arm A's data passes the new R3c for its 80 logged spawns; the 30 unlogged
ones are the unknown. Other risks as arm A: a crossing over 60 s (arm A: 51.0 s), X-Ray's record and our log disagreeing
by more than 3 s at an arrival.

## Amendment 116 (REGISTERED 2026-11-25): R3 proper, arm B: a death next to and inside a crossing, arms b1 and b2
Ordered by the Overseer (order A, B, C, D; arm A closed on A2's joint PASS). Pose v2 stays (Overseer, 2026-11-25: pose v3
goes to arm D after EnginePerf's lost-pose series). Two runs, each registered here and its box run asked for separately,
b1 first. One death policy per run, keep in both (EnginePerf's answer; freeze at a level change gets its own run later).

**Changed on our side (mod blob 3e18249, was 7441261):**
1. **Rig only, three levers** (default off; with them off the rig is arm A's): `txw=1` the rig walks the zone's own track
   to and fro during the stay (the Garbage track `out/p1/garbage_full_track.csv`, local: 204 points, 284 m, 104 s, made by
   `tools/p1_track.py --mesh` along the AI mesh between the two Garbage arrival points; the hole sampler keeps its own
   heights per track); `txd=<s>` one planned death that many s after a changer, while the new zone's ring fills; after a
   death the rig's plan ends (phase 5: it stands where the return left it, the run ends `txe` s of play after the return).
2. **Logging only** (amendment 115's measuring lines, registered separately; no behaviour change): the loader's job
   tally and the `TRANSIT near` / `TRANSIT jobs` lines.
3. **Pose writer unchanged: version 2**, pinned by this blob; T3w requires the line `T2 pose version=2`.
4. Offline: `tools/r3_armb_test.lua` (walk, turn, no pose jump, a far arrival stands, the plan ends at a death, the
   crossing death with and without play-clock deaths, the unfinished jobs line) PASS; span, pose and cross tests PASS;
   Opus review: no high finding, two medium and three low fixed.

**What play code is under test (never run before):** a death while X-Ray is on the other level. ITR2 sends the player to
the Hub and the test save loads into Cordon. The mod must rebuild Cordon, hand the player back un-held, and after 3 s of
level mismatch send X-Ray back with a `cross` pose (`T2 cross recover`) until X-Ray's snapshots name Cordon. In b2 the
crossing itself must be dropped first (`T2 cross dropped`).

Both runs start with the save-and-reload leg of the earlier death runs (r3, t3), so the test save exists on Cordon at the
track's first point; after the death the player stands there (X-Ray x -220.9, z -145.0; EnginePerf's condition 6 uses it).
**b1** (`scripts/boot_t2.sh b1`): `txa=120 txn=esc_level_changer_to_garbage_1 txb=600 txm=gar_level_changer_to_escape_1
txe=120 txw=1 wtest=6 t2die=210 t2dien=1`. 120 s of the Cordon track, out by pair `_1`, the Garbage walk, a planned death
at play clock 210 (90 s of play after the crossing; the draft said 10 s: moved so the Garbage walk and its sampler get
90 s), return to Cordon, recover crossing, 120 s standing, end. `txb` is never reached.
**b2** (`scripts/boot_t2.sh b2`): the same without `txw` and `t2die`, with `t2dien=0 txd=10`: the death comes 10 s after
the changer, inside the crossing. If ITR2 does not let the pawn die in that window (no `HP DEATH` within 20 s of the
changer), b2 is reported as not reachable: a FAIL of R3d by rule, not forced and not retried.

**Scored once each** by `tools/r3_check.py <run> <x out> 1 b1|b2`. PASS = all of:
- T2a, T2b, T2c, T3w, R3g, as arm A2. T2d as A2, with the window from the death to the first accepted snapshot after
  the recover crossing's end left out. T2e: as A2 but exactly one `HP DEATH`.
- R3a cross: one `T2 cross start` by `esc_level_changer_to_garbage_1`; one `T2 cross recover` (we on l01_escape, X-Ray
  on l02_garbage, a `gar_level_changer_to_escape_*` changer); cross end lines 2 in b1, 1 in b2; `T2 cross dropped` 0 in
  b1, 1 in b2; no `still open`; one `T2 rig plan ended by a death`; pose states (an `away` right after `dead` accepted)
  b1: play, cross, play, dead, play, cross, play, end; b2: play, cross, dead, play, cross, play, end.
- R3b time: the crossing out at most 60 s (b1); the recover crossing at most 90 s.
- R3c levels: as amendment 114 with one cross start; puppets > 0 within 60 s after every cross end. So nothing of the
  Garbage is spawned on Cordon after the return, by X-Ray's own record.
- R3d death (new): `HP DEATH` 85 to 95 s after the first cross end (b1), or 9 to 20 s after the cross start with the
  dropped line (b2); `T2 back in play after the death` within 120 s; `WC done in cordon_full`; the recover line 0 to 15 s
  after back in play.
- R3w walk (new, b1 only): `T2 rig walk starts` with a track end within 5 m of the arrival; between the first cross end
  and the death the pose goes at least 50 m from its first point with no step over 2 m between two writes.
- R3h un-held: no `FREE HELD` line, no rig hold release; no `hold rebase` line after the track start except between
  `HP DEATH` and the next `WC done`.
- R3p world: one transit, its table emptied to 0; no stale-puppet line. The `WORLD gone` lines of the death are reported.
- And EnginePerf's ten conditions on their side.
**Which first-time event failed** (Overseer, 2026-11-25; reported by the scorer, not a check): b1 meets three things for
the first time (the save leg with the two-zone table, the return into Cordon after a transit, the recover crossing). The
scorer lists fifteen stages in order from lines the mod already logs (track start, crossing out, walk, death, the Hub
seen, the Cordon world recognised, Cordon rebuilt, the return's end, back in play, X-Ray seen on the other level, the
recover's start, its end, creatures back, the run's end) and names the first one not reached. No extra pre-flight.
One retry only for a run that dies before its data, registered first. A crash: the dump is read before anything else.

**Scorer self-test `tools/r3b_selftest.py`** (PASS): no arm B run exists, so R3a, R3b, R3d, R3w and T2e's death count are
tested on synthetic logs: a good b1 and a good b2 pass; twelve mutants fail the named check (death at the wrong time, no
recover, recover of 100 s, rig stands, pose jumps 6 m, arrival 9 m from the track, the wrong zone rebuilt, two deaths, no
death, b2 death late, b2 without the dropped line, a b1 log scored as b2). A2's real data scores as before without the
arm argument (12 of 12) and fails as b1, as it must. The synthetic lines are my reading of the mod's log formats; a
mismatch with the real log would show as a FAIL of the new checks and is then a scorer fault to be ruled on, not tuned.

**Expected and not scored:** the Garbage arrival `_1` to the track's start is under 0.4 m; the two known Cordon trace
misses; the hole sampler's count on the Garbage track (first time: reported per point, limit 10 with Cordon's).

**X side (EnginePerf, their section 61, commit c4d0453ca):** runner 296563fd…, script 473dec25…, Stance14.exe b4ce9c77…,
lib cb183b7b…, all as A2; new checker `tools/t3_armB_xray_check.py` a71e36e69b66c009ef827b0946e2ae5df79b38db6fd57d874f66bcbc0c717d65;
env as A2 (`T2_RUN_S=600 T2_WALL_S=1660 T2_OVER_S=120`). Ten conditions; from our answers, as expected values: about 100
zone-mismatch lines naming l01_escape before the recover request (floor 20), and the first applied pose after the
recover within 5 m of x -220.9, z -145.0. Their stated risks for b2: their level load must end within about 40 s of our
death, and a level change within 30 s of a load has not run before. Their guess, X side PASS: b1 60 %, b2 50 %.

Guess before the runs: b1 ITR2 side PASS 40 %, b2 35 %. Risks: the recover path has only run offline; the return into
Cordon after a transit has never run (the zone to rebuild, the rig's state); misses on the new track pushing R3g over 10;
a scorer mismatch on a line never seen in a real log; in b2 the death may not land in the fill window.

## Amendment 119 (REGISTERED 2026-11-25): R3 proper, arm C: X-Ray lost during a crossing, arm c1
From the R3 draft (section C) and agreed with EnginePerf (their section 67). Pose v2, their frozen script 473dec25;
only their runner is new. One joint boot, `scripts/boot_t2.sh c1`.

**What changes in the mod** (new behaviour, default on; lever `t2cx=<s>`, default 120, 0 = off):
- A crossing is closed on our side when our ground in the new zone is ready, no snapshot names the destination, and
  `t2cx` s have passed since the changer: log `T2 cross closed without X-Ray`, the pose says `play` on our level.
- "X-Ray is writing" = the snapshot header's seq or run id was seen to change within the last 5 s, and the change
  before that came at most 5 s earlier (two changes: snapshots are read only in free play, so after a load the first
  read of a dead X-Ray's last file is itself one change). Only then does a snapshot naming the destination end a
  crossing (so one snapshot, about 50 ms, later than before), and only then does a snapshot of another level start a
  recover crossing, which also needs a change seen within the last second when its 3 s are up. A killed X-Ray's last
  file does neither; a `T2 cross recover waits` line says so.
- With `t2cx=0` every rule of this amendment is off and the mod writes exactly what the mod before it wrote.
- With no snapshot file at all and the last header naming another level, the last tick is dropped, as a mismatching
  snapshot's is.
- After a close, with the rig off its track, the pose's position is the pawn's real position (before: the last
  position sent before the changer).
Runs that are not arm C: a crossing that X-Ray confirms within 120 s reads as before. A crossing open longer, with our
ground ready, is now closed (then a recover crossing follows if X-Ray is alive on another level); the scorers of arms
A and B count a close as a failure of R3a.

**The run.** Cordon track 120 s; changer `esc_level_changer_to_garbage_1`. EnginePerf's runner kills X-Ray 4 to 5 s
after the pose first says `cross` (inside its level load) and starts it again 150 s later. We close the crossing
120 s after the changer. The restarted X-Ray boots on Cordon; its first snapshot comes about 180 s after the kill;
3 s after we see it we send the recover crossing (level l02_garbage, the same changer). X-Ray follows it about 15 s
later, when its warm-up ends. After its first snapshot of the Garbage the rig walks the Garbage track for 120 s of play;
end. Levers: `hcheal=2 t2span=380 t2gap=0 t2v=2 t2st=30 transit=1 txa=120 txn=esc_level_changer_to_garbage_1 txe=120
txw=1 t2end=600`. This also runs, for the first time, a level change about 15 s after a load on X-Ray's side.

**Scored once on each side**, one retry only for a run that dies before its data.
ITR2 side, `tools/r3_check.py <run> <X dir> 1 c1`. PASS = all of:
- T2a rate, T2b cost, T2c frames, T3w writer: as in arms A and B.
- T2d link: as before, outside the window from the cross start to the first accepted snapshot after the recover
  crossing's end; no accepted snapshot after that end is a FAIL.
- T2e clean: `T2 end`, no FAIL line, no dump, no death.
- R3a cross: one cross start by the changer; our ground ready within 60 s; exactly one `closed without X-Ray k=1`,
  120 to 125 s after the cross start; no cross end for k=1; one recover line (we on l02_garbage, X-Ray on l01_escape, a
  changer `esc_level_changer_to_garbage_*`); one cross end (k=2); nothing dropped, nothing still open; states play,
  cross, play, cross, play, end; the summary says `closed_without_xray=1`; the rig arrives after the recover line.
- R3q quiet: between the close and the `T1 new X-Ray run` line no recover crossing starts and at least one
  `recover waits` line is written; exactly one new-run line; the recover line 2 to 10 s after it.
- R3b time: the recover crossing at most 90 s.
- R3c levels: as arm B, with X-Ray's record read from both boots (`boot0/` and `boot1/`); a missing record of either
  boot is a FAIL.
- R3n nothing old: no spawn line between the cross start and the reader's line for X-Ray's first Garbage header after
  the recover line (`T1 level ... -> l02_garbage`); no stale-puppet line.
- R3w walk: after the recover crossing's end the pose goes at least 50 m with no step over 2 m.
- R3h un-held; R3g ground (miss + resolved at most 10); R3p one transit (amendment 118's rule, no other zone switch).
The scorer names the first of ten stages not reached: cross start; our ground ready; closed without X-Ray; recover
waits; new X-Ray run; recover crossing starts; rig arrived; cross end k=2; rig walk starts; T2 end.
X side: EnginePerf's nine conditions, their section 67, checker `t3_armC_xray_check.py` 7ec98e73. Files: runner
`tools/t2_xray_c.sh` 40b875766c0c984f62a65cda2ab787d4a6b105ac5ea7dd55995852096e678eb3, script 473dec25, Stance14.exe
b4ce9c77, lib cb183b7b. Env: `T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999
T2_RUN_S=600 T2_OVER_S=120 T2_MAXRESTART=1 T2_KILL_XS=4 T2_RESTART_WAIT_S=150`.

**Offline before the boot:** `tools/r3_armc_test.lua` on the real main.lua (close at 120 s and not before; no recover
on a dead X-Ray's file; recover 3 s after a restarted X-Ray's first snapshots; a recover crossing whose X-Ray dies is
closed and does not loop; no file drops the tick; a stale file naming the destination does not end a crossing; ground
not ready or lever off: nothing closes; removing either guard fails the test). `tools/r3c1_selftest.py`: a good log
passes, ten mutants each fail their check. The scorer's verdicts on the real A2, b1 and b2 logs are unchanged.

**The Opus review of the mod change, and what was done.** No real bug in the three changes; the planned sequence was
traced through the code. Its points:
1. Snapshots are read only in free play with the ground and the floor ready, so "X-Ray is writing" is unknown during a
   load. Effects: a crossing could close with X-Ray alive if our floor build failed for over 120 s; a recover may log
   one needless `waits` line. Left as is; named as a risk.
2. The run does not end early: with the tick dropped the stale-end timer (`t2end`) is not reached and the play clock
   stands. Exception found: with the snapshot file deleted the old tick stayed. FIXED (no file drops the tick).
3. After a close the pose kept the pre-changer position. FIXED for the rig (the pawn's real position).
4. The close also applies to a recover crossing: a slow recover closes and starts again 3 s later. Left as is; tested.
5. A dead X-Ray's last file naming the destination ended a crossing. FIXED (must be writing).
6. With the lever on, `still open after 120 s` now appears only when our ground is not ready. The scorers of arms A and
   B now also fail on a close.
7. The test's weak spots: it calls the reader directly (not through the frame loop), has no warm-up snapshots, and
   never runs the run-end check. Cases added for the file-gone and stale-destination points; the rest is what the
   boot tests.

**The solo arms** (`scripts/boot_t2solo.sh`, all modes) now run with `t2cx=0`: their stand-in for X-Ray is a static
snapshot rewritten once per crossing, which is not a "writing" X-Ray, so with the rules on their crossings would only
end by the close at 120 s.

**CodeRabbit on the diff (one run, 12 findings; read from its saved review), each handled:**
1. Major, mod: X-Ray dying inside the recover's 3 s still started a recover crossing. FIXED: a header change seen
   within the last second is needed when the 3 s are up; tested, and the test fails without it.
2. Major (question), self-test: does it reach its assertions with no X-Ray folder? Refuted: it runs to the end, the
   good log passes and every mutant fails its check.
3. Major (question), mod and scorer: can the first Garbage snapshot spawn a puppet before `T2 cross end k=2`? Yes, by
   design: that snapshot is ours to apply, and the cross end now comes one snapshot later. FIXED in the scorer: R3n's
   boundary is the reader's own line for X-Ray's first Garbage header.
4. Minor, tests: a relative track path aborted the offline test. FIXED.
5. Minor, self-test: a mutant counted as failed when its verdict was only missing. FIXED: an explicit FAIL is needed.
6. Major (question), boot: does a restart that never writes a snapshot hang the boot? Refuted: the boot script's own
   ITR2 time cap ends the run and the claim is released by its exit trap; X-Ray's runner ends by its own run length.
7. Minor, scorer: one missing X-Ray record was read as complete. FIXED: both boots' records are required.
8. Major, mod: a header without its footer sets the level before the body is checked, so a half-written destination
   snapshot could end a crossing. Not changed here: that order is as registered in amendment 101 and unchanged by this
   diff, and the two-change rule now asks for a second header. Named as a known limit.
9. Minor, scorer: two zone lines with one time stamp were both excluded. FIXED: paired by record.
10. Minor, draft: the two-change rule was not stated. FIXED above.
11. Minor, test: a close at 115 s would have passed. FIXED: the test asserts the close time (120.0 to 120.2 s).
12. Major, scorer: T2d passed if the link never resumed after the recover crossing. FIXED.

**Second review (Opus, after the fixes above), on the mod and boot script diff:** the planned run goes through, traced
in the code: the restarted X-Ray's second snapshot makes it "writing", the 3 s are not reset, the recover crossing ends
on X-Ray's second Garbage snapshot, and the file's deletion at the restart does not hide the new run. Nothing in a run
with a live X-Ray is delayed by more than one snapshot, also after a freeze or X-Ray's own load. With `t2cx=0` every
rule is off, and every solo mode gets it. No real bug. Left as is and named:
- a live X-Ray that never follows would cycle: 120 s of `cross`, a close, 3 s later a recover crossing, and again;
- a load of ours shorter than 5 s would let the first read after it count as "writing" (ours take about 51 s);
- polls with no snapshot file are not counted in `level_mismatch_snapshots`.

**Predicted values** (written before the run; reported against, the bounds above are what is scored):

| Quantity | Predicted | Range | From |
|---|---|---|---|
| Our ground ready, s after the changer | 51 | 49 to 54 | b1 50.8, A2 50.9 |
| The close, s after the cross start | 120.05 | 120.0 to 120.1 | the pose tick, 20 a second |
| First `recover waits` line | at the close, same tick | within 0.1 s after it | the code |
| `T1 new X-Ray run` line, s after the cross start | 183 | 180 to 187 | kill at 4 to 5 s, wait 150 s, EnginePerf's first snapshot 26 to 30 s after launch |
| Recover line, s after the new-run line | 3.05 | 3.0 to 3.2 | 3 s of standing mismatch; b1 and b2 3.0 |
| Recover crossing, s | 21 | 18 to 26 | X-Ray reads poses 15 s after its first snapshot, its load 8 to 9 s |
| Cross end k=2, s after the cross start | 207 | 201 to 216 | the three rows above |
| `level_mismatch_snapshots` at the end | 7,200 | 6,300 to 8,000 | one per frame while a snapshot file names Cordon: about 100 s of the dead X-Ray's file and 24 s of the restarted one, at 55 to 61 frames a second (b1: 756 in 12.3 s); the 28 s with no file are not counted |
| Play clock at the end | 240.3 | 240.0 to 240.6 | 120 s, the arrival, 120 s |
| Wall time of ITR2's play, cross start to end | 327 | 320 to 340 | 207 s plus 120 s of walk |

**Witness and control, offline** (`tools/r3_armc_witness.lua`, one scripted sequence with the reader called only in
free play, as the frame loop does): on the mod before this amendment (b408b4d, blob ec2f862) a killed X-Ray's last
file starts a recover crossing after a death's return (1 started); on this mod with `t2cx=120` it starts none and a
`recover waits` line is written. With `t2cx=0` this mod's output (every pose state and level, every `T2 cross`,
`T2 state` and `T1 level` line) is byte-identical to the old mod's on the same sequence.

Guess before the run: ITR2 side PASS 35 %. Three things run for the first time: the close, the wait on a dead X-Ray's
file, and a recover crossing with an X-Ray that has just booted. Risks: the floor build after the arrival taking long;
X-Ray's warm-up snapshots (negative track time) and our held puppets after its restart; the rig's arrival and walk
starting only after 4 minutes of standing; our own stale-end timer; anything a kill inside X-Ray's load leaves behind
(EnginePerf's main risk).

## Amendment 120 (REGISTERED 2026-11-25): R3 arm C, c2: a replicate of c1 under a corrected scorer
c1 (amendment 119) stands as FAIL 14 of 15 on our side, cause: R3a measured the close by the log's time stamps (119.993 s
between the start line and the closed line; bound 120). It is not re-scored. This amendment changes the scorer only, for a
new arm `c2`, and registers c2 as a fresh replicate: the same run, the same mod (blob c0b8952), the same levers, the same
X-Ray runner settings. Arm `c1` in the scorer is left as it was.

**Scorer changes (arm c2 only):**
- R3a's close: 120.0 to 125.0 s by the mod's own count (the `after N s` figure of its `closed` line), and the log's
  stamps (start line to closed line) within 0.5 s of that figure. Why the mod's figure: the mod counts from the moment the
  crossing starts and writes the start line some milliseconds later, so the stamps can be short of 120 s. Why 0.5 s: the
  two clocks must tell the same story; they differed by 7 ms in c1 and a frame is at most about 0.1 s.
- The stage list (reported, not scored) has `cross end k=2` before `rig arrived`: the restarted X-Ray's warm-up holds
  the play clock, so the rig arrives after the end (c1: 12.8 s).
- Everything else as arm c1 in amendment 119: R3a's other parts, R3q, R3b, R3n, R3c, R3w, R3h, R3g, R3p, T2a to T2e, T3w.

**Offline (`tools/r3c2_selftest.py`):** on c1's real log the old rule fails R3a and the new rule passes all 15 checks
with all ten stages reached; a mutant of that log closed at 126 s (figure and stamp) fails R3a; a mutant with the stamp at
126 s and the figure left at 120.0 fails R3a. Margin test: synthetic logs built from the predicted values below (the low
end of every range, the predicted value, the high end), every line's stamp moved by a random -10 to +10 ms, 100 logs each:
none fails a check or misses a stage.

**Predicted values and their margins to the pass bounds** (from c1 as measured; a range that touches or crosses a bound is
marked):

| Quantity | Predicted | Range | Pass bound | Margin |
|---|---|---|---|---|
| Our ground ready, s | 49.8 | 48 to 54 | at most 60 | 6 s |
| The close by the mod's count, s | 120.0 | 120.0 to 120.1 | 120.0 to 125.0 | **touches the lower bound**, see below |
| The close by the stamps, against the mod's count, s | 0.007 short | 0.05 short to 0.1 long | within 0.5 | 0.4 s |
| `recover waits` after the close, s | 0.014 | 0 to 0.1 | at least one before the new run | 60 s |
| New X-Ray run after the cross start, s | 184.5 | 180 to 188 | after the close | 55 s |
| Recover line after the new run, s | 4.05 | 3.0 to 5.5 | 2 to 10 | 1.0 s below, 4.5 s above |
| Recover crossing, s | 17.8 | 15 to 26 | at most 90 | 64 s |
| Rig arrives after the cross end k=2, s | 12.8 | 10 to 16 | after the recover line | not near |
| Walk after the recover crossing, m; largest step, m | 185; 0.42 | 150 to 200; 0.3 to 0.6 | at least 50; at most 2 | 100 m; 1.4 m |
| Ground misses (miss + resolved) | 3 | 2 to 5 | at most 10 | 5 |
| Pose rate, share of the wanted writes | 1.00 | 0.99 to 1.00 | at least 0.95 | 0.04 |
| Pose write cost, 99th percentile, us | 1000 | 1000 | at most 1000 | **touches the bound**, see below |
| Frame time, 95th percentile, ms | 17.4 | 16.5 to 19.5 | at most 25 | 5.5 ms |
| Link: latency 95th percentile, ms; accepted share | 18; 1.000 | 15 to 30; 0.99 to 1.00 | at most 300; at least 0.95 | 270 ms; 0.04 |
| `level_mismatch_snapshots` (reported) | 8,300 | 7,400 to 9,300 | none | |
| Play clock at the end (reported) | 240.3 | 240.0 to 240.6 | none | |

The two that touch a bound:
- **The close by the mod's count** is predicted exactly on the lower bound. The bound is kept, because the mod cannot
  write a smaller figure: it closes only once its count is above 120 s, and writes the count with one decimal, so the
  figure is 120.0 or more by construction (120.0 or 120.1 at the frame rates seen). A figure below 120.0 would be a real
  fault of the rule and must fail.
- **The pose write cost** (T2b, a bound from the base T2 registration, not changed here): the mod's clock for it resolves
  1 ms, so the 99th percentile can only read 0, 1000 or 2000. It read 1000 in all seven joint runs so far (the largest
  single write 2000 in each). It would read 2000, and fail, if more than 1 in 100 writes took 2 ms; the log does not
  show how far from that the runs were. Named as a known risk of this replicate; a finer measure of the write cost is
  for a later amendment.

**The run:** `scripts/boot_t2.sh c2`: as c1 (levers `hcheal=2 t2span=380 t2gap=0 t2v=2 t2st=30 transit=1 txa=120
txn=esc_level_changer_to_garbage_1 txe=120 txw=1 t2end=600`, the save-leg route, `t2cx` at its default 120), scored by
`tools/r3_check.py <run> <X-Ray dir> 1 c2`, once.

**EnginePerf's side (their section 69):** runner `tools/t2_xray_c2.sh`
d140e58300fd9b43999877338f085f69529fb96f28d380fd18453741e7481385 (c1's runner plus one log line at the restart boot: the
marker's size before and after it is emptied), checker `tools/t3_armC2_xray_check.py`
c24ffd31e8b56a0627780c83396000777af2c2c91f663ebdabbc7d8b8bb673d7 (only their check 3 differs from c1's); unchanged:
script 473dec25, Stance14.exe b4ce9c77, lib cb183b7b. Env as c1: `T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000
T2_STALL_AT=99999 T2_KILL_AT=99999 T2_RUN_S=600 T2_OVER_S=120 T2_MAXRESTART=1 T2_KILL_XS=4 T2_RESTART_WAIT_S=150`.

The recover delay of 4.05 s in c1 (3.05 predicted then) is explained by EnginePerf's write times: after a fresh boot
X-Ray's second snapshot comes 1.05 s after its first, and our "writing" rule needs the second before the 3 s start.
An X-Ray write ends up as an empty or cut file only if the kill lands inside a write (X-Ray writes in place): as in c1,
the kill comes inside X-Ray's level load, when it writes none; if it happens anyway c2 is a FAIL by rig with that cause
named (the reader's fix is amendment 121, not part of this run).

Guess before the run: ITR2 side PASS 75 %.
