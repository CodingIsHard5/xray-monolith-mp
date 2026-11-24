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
