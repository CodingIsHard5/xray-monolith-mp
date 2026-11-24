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
