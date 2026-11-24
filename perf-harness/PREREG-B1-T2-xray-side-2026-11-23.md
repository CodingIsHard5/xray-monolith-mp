# B1-T2, X-Ray side: pose feedback + crash-restart (JOINT DRAFT with ITRZone, 2026-11-23; nothing run, nothing built)
Overseer (after the crash diagnosis, 67440faf9): "T2 goes ahead now with an X-Ray crash-restart path in the bridge:
register jointly with itrzone. Include one check that a forced X-Ray kill mid-run brings X-Ray back, puppets hold
meanwhile, then resync."
ITRZone's design: ~/Projects/ITRZone/docs/design-notes/2026-11-23-b1-t2-pose-feedback.md (67663ff). This file is the
X-Ray side of it plus the restart path. It becomes binding only when both sides sign the joint registration.

## 1. Answers to ITRZone's open questions (desk, engine source of RefDet14's commit 54a338e15)
- `set_actor_direction`: exported (script_game_object_script2.cpp:348-351; 1, 2 or 3 floats, or a vector). It sets
  the ACTIVE CAMERA's yaw/pitch/roll (script_game_object2.cpp:434-444), not the body directly. X-Ray camera yaw is often
  the negated heading, so the sign is a first-boot check: set yaw a, read back `db.actor:direction()`, compare with
  atan2(d.x, d.z). Headless is fine (camera exists without a window; not checked yet, part of the pre-flight boot).
- `ChangeLevel(pos, lvid, gvid, dir, true)`: a global GAMMA's own scripts already call (game_fast_travel.script:1425,
  lc_custom.script:67), so it exists in this build. T2f uses it with the destination changer's vertex ids.
- Kill for `k,<id>`: `level.object_by_id(id):kill(db.actor)` when the object is online; offline objects get
  `alife():release` NOT used (that deletes, not kills); an offline id is kept in the list and killed when it comes
  online. Each id is killed once. (Later test; the rig cannot shoot.)
- Snapshot header gets the level and a boot id: `seq,<n>,<t_ms>,<t_track>,<level>,<run_id>` (ITRZone's name). `<run_id>` is X-Ray's start
  time in UTC ms, new on every (re)start. ITR2 resets its last-seq whenever level OR run_id changes.
- Away/dead + `freeze`: `level.set_time_factor` cannot stop A-Life switching; I propose `device():pause(true)` (exported,
  script_render_device_script.cpp:89) on `freeze`, `pause(false)` on `play`. Needs one boot check that a paused
  headless X-Ray still runs Lua `actor_on_update` (if it does not, the pose reader must move to a timer that runs
  while paused). Not decided until checked.

## 2. Pose reader (X-Ray, new script zz_t2_pose.script, beside zz_p1_puppet.script)
- Reads Z:\dev\shm\itrz_b1\pose.txt in `actor_on_update`, first line only unless the seq is new; strips \r (memory:
  wine-crlf-readers); accepts only seq,n ... end,n with n newer; torn reads counted.
- Applies `set_actor_position` (P1's off-mesh hold kept and counted) and `set_actor_direction` (sign per section 1).
- Stale 1 s: hold, log `T2 stale since seq=n`. dead/away: hold (and freeze if the header says so).
- The P1 track is OFF in T2: the actor follows only pose.txt.
- Logs per accepted pose `P,<seq>,<utc_ms written>,<utc_ms applied>,<dist to pose after apply>` for T2b/T2c.

## 3. Crash-restart path (the bridge, X-Ray side)
- A supervisor loop in the driver (tools/t2_xray.sh, from t1_xray.sh) watches the X-Ray engine process. If it exits
  before the run end (crash, fatal, kill), or the heartbeat fires (30 s without a new seq, outside planned gaps),
  it: saves that boot's xray.log as xray_boot<k>.log; starts X-Ray again from the SAME start save (cordon1), same
  args; logs `RESTART k at utc_ms`. At most 3 restarts per run; a 4th failure ends the run (rc 5).
- After the restart the first accepted pose teleports the actor to the ITR2 player (one step, as T2e's re-entry).
- What a restart loses (said plainly): A-Life progress since the start save. Creatures come back as the save had
  them. For the test that is fine. For play it is not "a life the same for everything" (Rule One): a play version
  would restart from a periodic autosave. That is a question for Caden later, not part of T2.
- Measured boot time today: 24.5-24.8 s from launch to first snapshot (6 diag runs). Budget below uses 90 s.
- ITR2 side (ITRZone's to build; listed so both sides agree): on stale snapshots puppets HOLD (T1's rule); on a new
  `<run_id>`, reset last-seq, then reconcile: puppets whose id is not in the first new snapshot are removed, new ids are
  spawned, kept ids snap to the new pose.

## 4. Check added to T2's list
- **T2i (forced kill):** in run R1 at about track 700 s (ITRZone's timing: after its rt 300 s write gap and my 600 s SIGSTOP), the driver kills
  X-Ray's process with SIGKILL (the engine pid only, not wineserver). PASS when all of:
  - X-Ray's first new-run_id snapshot is stamped within 90 s of the kill;
  - during the gap ITR2's puppet count never drops (puppets hold; ITRZone logs count per second);
  - within 2 s after the first new-run_id snapshot, ITR2's puppet set equals that snapshot's alive set (resync);
  - within 1 s after the first new pose applied, the X-Ray actor is ≤ 1 m from the ITR2 pose;
  - ITR2 does not crash (T2h), and X-Ray does not need a second restart before the run ends.
- T2h is reworded: "no ITR2 crash; any X-Ray restart other than T2i's planned one is logged and FAILS T2h" (the
  restart path keeps the run going so the other checks are still scored, but an unplanned crash is not hidden).

## 5. Pre-flights (offline, real files, before any box)
- My pose reader on ITRZone's writer's real pose.txt files (CRLF and LF), N in a row, torn ones skipped.
- The restart supervisor on a stub process killed with SIGKILL, and on a Wine X-Ray killed headless outside the
  claim? NO: any engine boot needs the box, so the real-engine check is one short claimed boot (below).
- One short claimed X-Ray-only boot (~10 min, WANT): set_actor_direction sign; device pause keeps Lua running or not;
  one SIGKILL + restart with the supervisor (boot time, new `<run_id>` in the header). This boot is a pre-flight, not
  scored.

## 6. Exe
- RefDet14.exe (same as T1), with its pdb restored as AnomalyDX11.pdb. If Caden approves the clamp fix (BUGFIXES.md
  CANDIDATE 3) before the joint run, the run uses RefDet14 + fix instead and says so; T2h then also checks that the
  [FIX] line for wpn_mosin19070 appears and no fatal follows.

PREDICTIONS (mine, X-Ray side; scored once after the joint run):
- the restart in T2i comes back within 90 s: 85 %;
- the X-Ray actor tracks the pose ≤ 1 m p95 (T2c): 60 % (off-mesh holds near buildings are the risk);
- an unplanned X-Ray crash during the run (T2h fail on my side) with the stock RefDet14: 20 %.

## 7. Agreed with ITRZone (its proposal, 2026-11-23)
- Three runs, each its own EXCLUSIVE joint boot, all in Cordon: R1 core (pose at 20 Hz; ITR2 write gap ~rt 300 s;
  my 20 s SIGSTOP at 600 s; forced X-Ray kill ~700 s = T2i); R2 away_sim (one rig death, keep then freeze);
  R3 Cordon->Garbage, now possible (ChangeLevel exists, section 1).
- ITR2 holds puppets through the gap and through X-Ray's warm-up (t_track < 0), then resyncs.
- The snapshot keeps the `a,x,y,z,yaw,health` actor line, so tracking error (actor vs pose) is scored from it.
- Host: this file holds my side; the joint PREREG will be my file with ITRZone's section verbatim, as in T1.

## 8. Built and tested offline (2026-11-23; no engine boot)
- harness/zz_p1_puppet.script gains -t2_pose (sha 8a4b359fa3ff7afa...): pose reader, run_id header, kill list, pause behind -t2_pause.
  Stub test (t2-offline/pose_reader_stub.lua, plain lua5.1 with the engine API stubbed): LF and CRLF accepted; a torn
  file counted ONCE and then goes STALE (first version logged it every frame and never went stale: fixed); RESUME;
  off-mesh HOLD; k-kill once; PAUSE/UNPAUSE. T1 mode (-p1 -b1_snap) gives byte-identical snap/meta output to the old
  script in the same stub.
- tools/t2_xray.sh (sha 339f8122f89fb901...): supervisor with planned stall, planned kill, restart. Fake-engine test
  (t2-offline/fake_engine.py + fake_tb_run.sh, track time x20): stall, planned kill -> boot 1 (planned=1), a crash in
  boot 1 -> boot 2 (planned=0, rc 6), a hang -> heartbeat 30 s -> restart (planned=0). Bug found and fixed: the engine
  lookup picked the WATCHER (its argv names the exe too) when the engine was small; it now skips t1_watch. In real runs
  the engine's RSS is far bigger, so T1's runs were not affected, but t1_xray.sh has the same lookup (noted, not changed:
  T1's script is frozen).
- Still needs the short claimed pre-flight boot (section 5): yaw sign, pause keeps Lua running, a real kill + restart.

## 9. Pre-flight boot plan (registered before it runs; not scored; waits for the Overseer's OK)
- tools/t2_preflight.sh: WANT, one EXCLUSIVE X-Ray-only slice (claim 20 min), RefDet14, cordon1. A stand-in pose
  writer (tools/t2_preflight_pose.py, not ITR2's code) holds the actor at its own on-mesh position and steps the yaw:
  0 / +1.0 / -1.0 rad (10 s each), then dead+freeze 15 s, then play yaw 0.5. t2_xray.sh with T2_RUN_S=240,
  T2_KILL_AT=120 (planned SIGKILL), no stall, T2_PAUSE=1.
- Readings: (1) yaw sign = snapshot actor yaw vs pose yaw in phases A-C (same sign, or negated, or neither);
  (2) pause: whether snapshots and PAUSE/UNPAUSE lines continue through phase D. If Lua stops while paused, the
  heartbeat restarts X-Ray (an expected outcome here, not a failure), and freeze needs another mechanism;
  (3) restart: kill -> first new-run_id snapshot time (budget 90 s; guess ~30 s).
PREDICTIONS: yaw negated (camera yaw = -heading) 55 %, same sign 35 %, neither 10 %; Lua keeps running while
device-paused 35 %; restart within 90 s 90 %.

- Engine lookup changed on the Overseer's ruling (pid/ancestry, not a name match): skip the watcher by PID, and
  require WINEPREFIX=$RUN/prefix in the process environ. Fake test re-run: stall, planned kill (killed pid = the 9.8 MB
  fake engine, logged with comm and RSS), crash restart, rc 6. t2_xray.sh sha now d8a391281cb7a344.

- ITRZone review: under freeze the 30 s heartbeat is skipped while pose.txt says dead/away + freeze (fake test: hung
  engine, pose dead+freeze 60 s -> no restart; pose play -> restart 30 s later). Pre-flight phase D lengthened to 40 s
  so the skip is exercised. t2_xray.sh sha fd7d72bb79d20061.
- Pre-flight attempt 1 (14:11): t2_xray.sh was not executable (rc 126); no engine boot; slice released at once.
  Fixed (chmod +x) and re-queued.

## 10. Pre-flight 1 RESULT (attempt 2, 19:13-19:25Z, slice released; read once)
- Boot to first snapshot: 25 s (boot 0 and boot 1).
- Pose following WORKS headless: 599 poses applied in boot 0 and 900 in boot 1; actor-to-pose distance after apply
  median 0.11-0.13 m, p95 0.58 m, max 0.60 m; 0 off-mesh holds (the stand-in held a fixed on-mesh point).
- PAUSE: device():pause(true) STOPS Lua. The PAUSE line was logged at seq 400; no snapshot after it, so UNPAUSE can
  never run, and X-Ray stays frozen for good. (Prediction "Lua keeps running while paused" 35 %: it did not.) So
  device pause is NOT a usable freeze mechanism. The freeze skip in the supervisor worked (hold logged for the 40 s
  dead phase).
- ENGINE LOOKUP FAILED under real Wine: after the pose went back to play, the 30 s heartbeat fired, but engine()
  returned nothing (the WINEPREFIX-in-environ check matched no process), so nothing was killed, and the boot only
  ended at tb_run's own timeout (640 s, rc 5). The supervisor then restarted X-Ray (boot 1, first snapshot 25 s
  later, ran to the end): the restart itself works. The fake test could not show this (its engine is python).
- Yaw sign: NOT measured. The run logged no actor yaw per pose (only the last snapshot survives). The P line now also
  logs the pose yaw and the actor's yaw read back.
- The planned kill at track 120 never happened (the pause froze the game at about track 30).
Fixes (before any further boot): engine() now picks by /proc/<pid>/exe being Wine (and skips the watcher by PID),
and logs every candidate (pid, exe, RSS) at each boot's first snapshot. t2_xray.sh sha 278b8292293c609c.

## 11. Pre-flight 2 plan (registered; waits for the Overseer's OK)
- Freeze mechanism, proposed (needs the Overseer's OK; it implements Caden's away_sim=freeze setting): the BRIDGE freezes
  X-Ray by SIGSTOP of the engine while the pose says dead/away + freeze, and SIGCONT on play (T2_FREEZE=sigstop;
  freeze.csv). T1 showed X-Ray survives a 20 s SIGSTOP (twice). A-Life is fully frozen, with no engine change. For a
  Windows player build the same is a process suspend from the bridge. -t2_pause stays off.
- Run: PF_OUT=out/t2pre2 PF_PAUSE=0 PF_FREEZE=sigstop PF_KILL_AT=120 PF_RUN_S=240, the stand-in writer with phases A-C
  (yaw), D (dead+freeze 40 s), E (play). Planned SIGKILL at track 120, restart, boot 1 to the end. ~8 min engine time.
- Readings: (1) the candidate list and the killed pid (must be the engine: Wine exe, RSS in GB); (2) boot 1's first
  snapshot within 90 s of the kill; (3) yaw sign from the P lines in phases A-C; (4) freeze: STOP and CONT in
  freeze.csv, no snapshot between them, snapshots and P lines resume within 2 s of CONT, no unplanned restart.
PREDICTIONS: engine() picks the engine 85 %; restart within 90 s 90 %; yaw negated 55 % / same 35 % / neither 10 %;
freeze stops and resumes cleanly 80 %.
t2_xray.sh sha 4c0bf34e2a0eb0a4.

## 12. Pre-flight 2, final plan (Overseer: SIGSTOP freeze approved; add a 5-min stop; registered before the run)
- The 5-min stop IS the freeze path: the stand-in writer's dead+freeze phase lasts 300 s (PF_POSE_ARG=d300), so the
  bridge SIGSTOPs X-Ray for about 5 min and SIGCONTs on play. Then the planned SIGKILL (PF_KILL_AT=360) and restart.
  PF_OUT=out/t2pre2 PF_PAUSE=0 PF_FREEZE=sigstop PF_RUN_S=600. Wall time about 11 min; 20-min claim via WANT.
- New logging in T2 mode (puppet script sha 89cdf093a8161863): once a second G,<time_global>,<os.time>,<frame>,<game.time ms>,
  <alife object count>; after a wall gap > 5 s between updates, RESUMED_GAP and F,<time_global>,<f_time_delta ms> for
  every frame of the next 30 s. There is no exported A-Life tick counter, so the A-Life measures are the game time,
  the object count, and the ON/OFF switch lines (per 10 s, before vs after). Said plainly: a proxy, not a tick count.
- Desk (xrEngine/device.cpp:794-809): the frame delta fTimeDelta is smoothed and CLAMPED to 0.1 s, but time_global
  (dwTimeGlobal, from TimerGlobal) is wall-clock based, so after a stop it jumps by the whole stop.
PREDICTIONS for the long stop:
- time_global jumps by about the stop (300 s +/- 5 s): 90 %;
- the first frame's f_time_delta after resume <= 100 ms (clamp, no physics catch-up burst): 85 %;
- game time jumps by the stop x GAMMA's time factor (i.e. the world clock skips ahead, it does not catch up in a burst
  of A-Life ticks): 70 %. Alternative: the game time advances only by the clamped 0.1 s (A-Life time froze): 25 %;
- one slow frame (> 100 ms wall) within the first 2 s after resume, as timers that came due all fire: 60 %;
- a burst of ON/OFF switch lines in the first 10 s after resume (more than 3x the 10 s before the stop): 40 %;
- no crash, X-Ray continues: 90 %.
Readings of (1)-(4) of section 11 are unchanged. t2_xray.sh sha 4c0bf34e2a0eb0a4.

- Before pre-flight 2 (found while answering ITRZone): a torn or empty pose read during a freeze ended the hold
  (and would have sent SIGCONT early). Now the hold ends only on a pose whose awaysim field is exactly keep or
  freeze and is not dead/away+freeze. Fake test: truncated "...,fre" and an empty file kept the hold; "play,keep"
  ended it. t2_xray.sh sha bfe2a36978b8754d (this is the version pre-flight 2 runs).

## 13. Pre-flight 2 RESULT (19:48-20:07Z, slice released; read once)
- Engine lookup: WORKS. Candidates per boot: two small /usr/lib/wine/wine wrappers (18-29 MB) and the engine
  /usr/lib/wine/wine64 (17.2 GB RSS); the engine was picked both boots and the planned kill hit it
  (comm "X-RAY Primary t", 17.2 GB). Prediction 85 %: held.
- RESTART: SLOW, prediction "within 90 s" (90 %) WRONG. The kill was at 19:55:11, but tb_run only noticed at its own
  timeout (20:05:22, rc 5), because the two Wine wrapper processes stay alive with the exe in their command line, and
  tb_run waits while any matching process lives. Boot 1's first snapshot came 25 s after its launch (20:05:47), so
  kill to first snapshot was 10 min 36 s. Fix: after any SIGKILL of the engine, end the rest of OUR prefix
  (WINEPREFIX=run/prefix wineserver -k, as t1_xray.sh's heartbeat path already does). Expected kill to first
  snapshot then: ~5 s (tb_run's poll) + 25 s boot. Not yet measured.
- YAW: SAME sign, prediction "negated" (55 %) WRONG; "same" was 35 %. The actor's read-back yaw equals the pose yaw
  (0 / 1.0 / -1.0 / 0.5) on every pose but the 2 at phase changes (one frame late). Yaw is applied as sent.
- FREEZE (bridge SIGSTOP, 300 s): STOP and CONT in freeze.csv 300.0 s apart; no unplanned restart; X-Ray resumed
  and ran to the planned kill. Prediction "stops and resumes cleanly" (80 %): held.
- LONG STOP, what X-Ray does on resume:
  - time_global jumped 300.45 s (61215 -> 361666 ms). Prediction 90 %: held.
  - first frame's f_time_delta 100.0 ms (the clamp), then 29, 21, 20 ms: no physics catch-up burst. 85 %: held.
  - GAME TIME jumped 1802.7 s = 30 min of game time for 5 min of stop (time factor 6). Prediction 70 %: held. So the
    freeze is NOT a freeze of the world clock: an hour in the Hub would skip 6 game hours (weather, respawns, needs).
  - no slow frame: the biggest time_global step in the 30 s after resume was 56 ms, in the first 2 s 29 ms.
    Prediction "one slow frame" (60 %): WRONG.
  - ON/OFF switch lines: 0 in the 10 s before the stop, 2 in the 10 s after: no burst. Prediction "burst" (40 %):
    did not happen.
  - A-Life object count 26112 before and after.
- Noise: FREEZE_UNSUPPORTED was logged on every pose during the dead phase (PF_PAUSE=0): to be logged once.

## 14. Proposal: wind the world clock back over a freeze (Overseer: "propose a compensation ... rather than accept")
- Why the jump: game time = m_game_time + time_factor x (Device.dwTimeGlobal - m_start_time)
  (alife_time_manager_inline.h:25), and the client clock also runs from dwTimeGlobal. dwTimeGlobal comes from
  TimerGlobal (QPC wall time). Lua cannot fix it: level.change_game_time only moves forward (u32 args), and Lua
  timers keyed to time_global would still all come due.
- Engine change (small, default OFF): CTimerBase gets AbsorbGap(ticks) (adds to the existing qwPauseAccum, the same
  field the engine's own pause uses). In the frame timer (xrEngine/device.cpp ~805), when a new console variable
  g_freeze_absorb_ms > 0 and the wall gap since the last frame exceeds it, the gap minus one 33 ms frame is
  absorbed before dwTimeGlobal is read. Effect: after a SIGSTOP, time_global, game time, the scheduler and every Lua
  timer see one normal frame, exactly as if the stop never happened. The bridge sets it at start through the puppet
  script (get_console():execute("g_freeze_absorb_ms 2000")) only in T2/B1 runs. With the variable at 0 (default) the
  engine is unchanged (Rule One). It is player-visible only through Caden's own freeze setting.
- Test (pre-flight 3, ~7 min, X-Ray only, registered first): two 60 s freeze stops, the first with absorb off and the
  second on, then the planned kill to measure the fixed restart. PASS for absorb: game time advances <= 1 s and
  time_global <= 1 s across the second stop, vs ~360 s / ~60 s across the first; X-Ray continues. PASS for the
  restart: kill to first new snapshot <= 90 s.
- For ITRZone (asked): freeze.csv gains game_ms (from the last snapshot before STOP and the first after CONT).

- After pre-flight 2: t2_xray.sh ends the rest of OUR prefix (WINEPREFIX=run/prefix wineserver -k) right after any
  SIGKILL of the engine; FREEZE_UNSUPPORTED is logged once. Not yet measured on the box.

## 15. Pre-flight 3 (Overseer OK with conditions; registered before the run; runs AFTER the clamp witness)
Conditions (Overseer): absorb only while a freeze is active, armed before the STOP and cleared after the CONT; every
absorbed gap logged; and show that with absorb ON but no freeze, a 2-3 s hitch is NOT absorbed.
- Engine perf/freeze-absorb 7c8331264 (on perf/fixes e8eb661df, so it includes the ammo clamp): console variables
  g_freeze_absorb_ms (threshold; 0 = off) and g_freeze_absorb_armed (0/1), both default 0 and NEVER saved to
  user.ltx (a CCC_Integer subclass with an empty Save: otherwise cfg_save on exit would keep "armed" for the next
  session). The frame timer absorbs only when both are set and the gap exceeds the threshold; it logs
  "* [FREEZE] absorbed <drop> ms of a <gap> ms gap" to xray.log and disarms itself (one absorb per arming).
- Script (sha 1a9afb7cb8ddfc5c): -t2_absorb <ms> sets the threshold at start (ABSORB_ON). When the pose turns dead/away + freeze,
  it arms (ABSORB_ARM) or, for the first k freezes with -t2_absorb_skip k, does not (ABSORB_SKIP); on play it
  disarms (ABSORB_DISARM). The resume gap line now fires at >= 2 s (so the 3 s hitch is seen). Game time is printed
  unsigned (u32 ms). Stub test: SKIP, DISARM, ARM, RESUMED_GAP, DISARM in that order; console commands as intended.
- Bridge (t2_xray.sh sha 096f53666495cc5d): with T2_ABSORB, waits <= 3 s for the ARM/SKIP line before the SIGSTOP; freeze.csv gets
  a header and a 5th column game_ms (STOP: game time at arming; CONT: game time of the first frame after resume,
  from RESUMED_GAP).
- Run: PF_OUT=out/t2pre3 PF_EXE=Absorb14.exe (CI build of 7c8331264, hash-checked) PF_POSE_ARG=pf3 PF_PAUSE=0
  PF_FREEZE=sigstop PF_ABSORB=2000 PF_ABSORB_SKIP=1 PF_STALL_AT=20 PF_STALL_S=3 PF_KILL_AT=275 PF_RUN_S=420.
  Timeline (stand-in writer, wall s from track 0): 3 s hitch at track 20 (pose play: absorb ON, not armed);
  D1 dead+freeze 40-100 s (first freeze: SKIP = absorb-off control); E1 play; D2 dead+freeze 130-190 s (armed);
  E play; planned SIGKILL at track 275 (~wall 215 s; track runs ~63 s ahead after the unabsorbed hitch and D1);
  restart; boot 1 to the end. About 9 min wall; 20-min claim via WANT.
PASS (absorb): hitch: time_global step >= 2.5 s across it and NO [FREEZE] line before D2; D1: game_ms advances
  >= 300 s x 1000 (60 s x factor 6, i.e. not absorbed); D2: exactly one [FREEZE] absorbed line, game_ms STOP->CONT
  <= 1000 and time_global step across it < 1 s; X-Ray continues. PASS (restart): kill to boot 1's first snapshot
  <= 90 s, and the two Wine wrappers gone after the kill.
PREDICTIONS: absorb PASS 75 % (risk: the arming command or the 3 s wait misbehaves); hitch not absorbed 95 %;
  restart <= 90 s 85 % (guess ~35 s).

- Absorb build: CI 45256626066 (7c8331264) success, 0 errors; run/bin/Absorb14.exe sha e35a289b31534f0d (+ Absorb14.pdb).

## 16. Pre-flight 3 RESULT (20:53-21:09Z, Absorb14 e35a289b, slice released; read once)
- HITCH NOT ABSORBED (absorb ON, unarmed): the 3 s SIGSTOP at track 20.8 s gave a time_global step of 3991 ms
  (G lines 50686 -> 54677) and no [FREEZE] line. PASS. Prediction 95 %: held.
- D1, unarmed control (ABSORB_SKIP seen before STOP): 60.2 s stop; time_global +61073 ms, game time +366438 ms
  (6.1 game min). Not absorbed, as intended.
- D2, armed (ABSORB_ARM seen before STOP): engine log "* [FREEZE] absorbed 60144 ms of a 60177 ms gap" (exactly one
  absorb in the run); time_global from the arming frame to the first frame after resume +516 ms, game time +3096 ms.
  The 516 ms is X-Ray running between the arming (on the first dead pose) and the bridge's STOP (its 1 s poll), plus
  one 33 ms frame. The absorb itself removed all but 33 ms of the stop.
  As REGISTERED, "game_ms STOP->CONT <= 1000" is FAIL (3096): the STOP row's game_ms was the arming time, not the
  stop time. That was my measurement definition, not the absorb. Fixed for the joint run: the STOP row now carries
  the game time of X-Ray's LAST frame before the gap (RESUMED_GAP fields 6-7), so the check measures the stop only.
  Prediction "absorb PASS" (75 %): FAIL as registered, for that reason; the absorb itself worked.
- RESTART: SLOW AGAIN, prediction "<= 90 s" (85 %) WRONG. The kill was at 21:00:10; tb_run ended only at its timeout
  (21:07:44, rc 5). Its own  kept matching the WATCHER (t1_watch.py; cpu_samples show pid
  929231 python3), the same self-match class as before, now inside tb_run. Boot 1's first snapshot came 25 s after
  launch. Fix: t2_xray.sh stops the boot's watcher BEFORE waiting for tb_run whenever a boot ends early, and ends a
  boot when the engine process has been gone for 3 s, with tb_run's rc deciding normal end (0) vs crash. Fake test
  with a tb_run that waits like the real one (t2-offline/fake_tb_run.sh, fake engine exe named wine64): kill ->
  tb_run ended within 3 s -> restart; normal end -> rc 0 and no unplanned restart; crash -> unplanned restart, rc 6.
- No crash in either boot. t2_xray.sh sha fa138a52ec746c67, puppet script sha 966e3a475163c950.

## 17. No bare name matches in the harness (Overseer: second self-match; fix all in one go)
- Every `pgrep -f` engine lookup in tools/ (tb_run, det_run, bench, p1_run, stall_run, t1_xray, t2_xray, coop_load:
  12 uses) now goes through tools/lib_engine_pids.sh: a process is the engine only if its BINARY (/proc/<pid>/exe) is
  a Wine loader and its command line names the exe. Python watchers, shells, grep and tee can never match. "Is the
  engine still running" checks (tb_run, det_run, bench, t2_xray) use engine_main: the biggest match AND RSS > 500 MB,
  because the 20-30 MB Wine wrappers can outlive a killed or crashed engine. Remaining pgrep uses are `pgrep -x
  sober` (exact process name, the Roblox guard), which cannot self-match.
- Desk test: a Wine-like binary and a python process with the same argv: only the Wine-like one is returned.
  Fake t2 test (engine_main threshold lowered for the 10 MB fake): planned kill, crash restart, normal end all right.
- Hash changes: t1_xray.sh (T1's record keeps its frozen hash; the old copy stays in git history), tb_run.sh 1b8d103f2208befc,
  lib_engine_pids.sh cb183b7b894eab3e, t2_xray.sh 745d975f7e24e3e9.

## 18. Pre-flight 4 (Overseer OK; registered before the run; ~6 min, X-Ray only, WANT, 20-min claim)
- PF_OUT=out/t2pre4 PF_EXE=Absorb14.exe PF_POSE_ARG=pf4 PF_PAUSE=0 PF_FREEZE=sigstop PF_ABSORB=2000 PF_ABSORB_SKIP=0
  PF_KILL_AT=80 PF_RUN_S=200. Stand-in writer: play 0-20 s, dead+freeze 20-50 s (armed), play after; planned SIGKILL at
  track 80; restart; boot 1 to the end.
PASS: (a) freeze.csv STOP->CONT game_ms <= 1000 with the new STOP definition (last frame before the gap); exactly one
  [FREEZE] absorbed line; (b) kill -> boot 1's first snapshot <= 90 s; (c) after the kill no process of ours names the
  exe until boot 1 starts; (d) rc 0, no unplanned restart.
PREDICTIONS: (a) 90 % (expected ~200 ms: one 33 ms frame x 6); (b) 85 % (expected ~35 s: <= 5 s tb_run poll + 25 s
  boot); (c) 90 %; (d) 85 %.

## 19. Pre-flight 4 RESULT (21:29-21:34Z; slice released; read once) and FROZEN hashes
- (a) freeze.csv STOP game_ms 3165214900 -> CONT 3165215098: +198 ms of game time across a 30.1 s stop (one 33 ms
  frame x 6). Engine: "* [FREEZE] absorbed 30093 ms of a 30126 ms gap", exactly one. PASS (prediction 90 %: held).
- (b) planned kill 21:32:22.716 -> boot 1's first snapshot 21:32:50.595 = 27.9 s (tb_run ended 2.8 s after the kill,
  boot 25 s). PASS (85 %: held; I expected ~35 s).
- (c) tb_run ended 2.8 s after the kill, so no process of ours kept it waiting. PASS (inferred from that, not a
  process listing).
- (d) rc 0, no unplanned restart, no fatal. PASS.
FROZEN for the joint B1-T2 runs (sha256):
- tools/t2_xray.sh            745d975f7e24e3e9abc2407c6625d4bea767524174a361ded4a359f42eda7a14
- harness/zz_p1_puppet.script 966e3a475163c95034308ef6431b2d754756a1f6f8854f0cd0b6bae0db13f4e5
- tools/lib_engine_pids.sh    cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e
- tools/tb_run.sh             1b8d103f2208befc554c48cf0b628830e67e0223cbe5c99ab1fcc34205363953
- tools/t1_watch.py           7bb3d29aa3e933a8104f06ce426e30326f8c20221a68c02a17e36e17a73bfe16
- run/bin/Absorb14.exe        e35a289b31534f0df840f7b28b6283cf702a46199f6d5edc2091a5151ab6ad37 (perf/freeze-absorb 7c8331264 = perf/fixes e8eb661df + absorb; its pdb is
  installed as run/bin/AnomalyDX11.pdb)
- X-Ray env from ITRZone's driver: both runs T2_EXE=Absorb14.exe T2_FREEZE=sigstop T2_ABSORB=2000; r2 also
  T2_ABSORB_SKIP=0 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_RUN_S=1500.

## 20. Defect found AFTER the freeze, before any joint run (r1 held by ITRZone; nothing booted)
- While testing my X-check scorer on pre-flight 4's real data: after a restart, the killed boot's last snapshot
  stays in snap.txt (seq 878); the new boot's watcher stamps it first and then ignores every new snapshot (seq 1, 2,
  ...) as "not newer". boot1/watch/written.csv held 1 stale row although X-Ray ran (1179 poses applied). It would
  have broken X-kill timing and ITRZone's merged watch data after the kill.
- Fix: t2_xray.sh deletes snap.txt before every restart boot (k > 0) starts its watcher. Fake test: boot 1's watcher
  stamps 572 snapshots from seq 1; X-kill reads 2.7 s on the fake.
- My own slip, said plainly: the first attempt at this edit did not apply (a failed grep earlier in the same command
  chain skipped it) and I only saw it because the fake test still showed the stale row. Re-applied and re-tested.
- Scorer tools/t2_xray_check.py (committed BEFORE the joint runs, sha 16e89cb796614a6634cab262f3d69fef6f7abd60d536712cc98dde8515a2cedc): X-kill, X-noplan, X-freeze, X-track as in
  the joint file. On pre-flight data it gives: pre-flight 4 as r2 PASS (game_ms +198); pre-flight 3 FAIL as expected
  (365106 unarmed, 3096 old STOP definition).
- NEW FROZEN t2_xray.sh sha256: 6ae8d27fd8a8f6ad4409b5618bbf60b4e9bb94b32fc5d7228bf135c0ed66142d (other frozen files unchanged).

## 21. Joint r1 X-side RESULT (ITRZone run 20261123-185125; scored ONCE with the committed scorer, r1)
Out dir ~/Projects/ITRZone/out/runs/20261123-185125-t2r1-xray. Verbatim scorer output:
```
X-kill PASS: kill at 1795478751980, boot 1 first snapshot after 30319 ms
X-noplan PASS: 0 unplanned restarts (); last line rc=0: True
X-freeze PASS: r1: 0 STOP rows (none expected); absorbed lines 0
X-track FAIL: 328 poses applied; dist p50 0.295 p95 1.053 max 3.014 m; off-mesh holds 30; torn 0
X SIDE (r1): FAIL
```
- **X SIDE r1 = FAIL** (X-track p95 1.053 m > 1.0 m). The score stands; it is not re-scored.
- Context (read after scoring, descriptive only): ITRZone reports its rig pawn took fall damage during placement and
  died at play 29.1 s; the pose said `dead` for the rest (~23 min), so only 328 poses were applied (all in boot 0,
  none in boot 1). The off-mesh HOLDs are seq 503-505 and so on; all 18 P rows over 1 m are seq 510-525 (distance growing smoothly
  1.13 -> 3.01 m in ~0.75 s, which looks like a free fall of the target, ~0.5 g t^2 = 2.7 m) plus 579 and 582 (1.05, 1.04 m).
  My reading, not checked: the actor stayed on the navmesh while the ITR2 pose fell, so the tracking error comes from the
  same rig fall. The sample is too small and too short to judge following.
- Kill/restart worked: new boot's first snapshot 30.3 s after the kill (limit 90 s); ITRZone I-T2h resync 42.6 s, 43
  puppets held. No unplanned restart, rc 0.
- A re-run with a fixed rig is a NEW run (needs its own registration); r2 is held by ITRZone for the same reason.

## 22. Joint r1b X-side RESULT (ITRZone run 20261123-201528; r1's one retry; scored ONCE, scorer sha 16e89cb7..., r1)
(An earlier r1b attempt, 20261123-195823, was a NO-START: ITR2 never launched; the X-Ray out dir was empty and no engine
ran. Nothing was scored. The Overseer re-queued it.) Out dir ~/Projects/ITRZone/out/runs/20261123-201528-t2r1b-xray. Verbatim:
```
X-kill PASS: kill at 1795483794360, boot 1 first snapshot after 30401 ms
X-noplan PASS: 0 unplanned restarts (); last line rc=0: True
X-freeze PASS: r1: 0 STOP rows (none expected); absorbed lines 0
X-track FAIL: 19697 poses applied; dist p50 0.427 p95 209.365 max 271.086 m; off-mesh holds 5183; torn 0
X SIDE (r1): FAIL
```
- **X SIDE r1b = FAIL.** It stands; r1's retry is used up. ITR2 side: all 8 I- checks PASS (ITRZone), no pawn death.
- Descriptive, after scoring:
  - One stretch drives the p95: boot 0, pose seq 7811-11824 (time_global 430.8-631.4 s, utc 1795483491328-1795483691979,
    ~200 s). Every pose in it was off the AI mesh by my HOLD rule (4040 HOLDs; the rule is "no vertex, or vertex more
    than 2 m off in Y"). The actor held its last on-mesh point while the pose went up to 271 m away.
  - Yaw followed throughout: pose yaw and actor yaw read back are equal.
  - Leaving that stretch out (NOT a re-score) gives p50 0.311 m, p95 1.439 m over 15,683 poses. That is still over 1.0 m,
    from many short HOLD stretches where distance sits at 2-8 m. Boot 1 alone: 6186 of 6505 poses within 1 m.
  - Unexplained so far: why a pose carrying X-Ray's own track height is off X-Ray's AI mesh for 200 s. I asked ITRZone
    for the pose x,y,z in that window. Possible readings, not checked:
    (a) the track itself leaves the AI mesh there (the T1 puppet held those points too);
    (b) the pose's track-time mapping drifted after the I-T2f gap at play 300 s;
    (c) an axis or height mix-up on some points.
  - My X-track rule (p95 <= 1 m with HOLD on off-mesh points) also counts held distance as tracking error. That is
    a design point for r2's registration, NOT a change to this score.
- **Cause of the r1b X-track FAIL (explained jointly with ITRZone; checked by me on the file):** it is a fact of the track,
  not a pose-path bug. ITRZone's writer sends out/p1/cordon_full_track.csv (t,x,y,z,node_dist, X-Ray metres) interpolated
  by play clock:
  - 0-380 s: 0 of 947 points have node_dist < 0.
  - 380-410 s: 9 of 75.
  - 410-600 s: 475 of 475, with x 10.5-13.2, y fixed at 16.59 (X-Ray y is up), and z sweeping 707-958 m and back at about 2.5 m/s.
    That is a straight horizontal leg recorded by P1's generator past the walkable area.
  - The pose sent exactly those points. X-Ray correctly HELD the actor, as its rule says. ITR2's ground traces missed there too.
  - The r1b FAIL stands.
- **Overseer ruling:** X-track stays as registered for r2. h1 and r2 play only the on-mesh parts of the track. Option A is not used.
- **Overseer ruling, final:** r2 and h1 replay the track's first 380 s ping-ponged. X-track stays exactly as registered
  (p95 <= 1.0 m). The gap (full track 410-600 s) is named, and the HOLD count is reported. Option A is not used.
  ITRZone aligns amendment 95 with this ruling. On the X-Ray side, r2's env and frozen hashes are unchanged.

## 23. Joint r2 X-side RESULT (ITRZone run 20261123-210325; scored ONCE, scorer sha 16e89cb7..., r2)
Out dir ~/Projects/ITRZone/out/runs/20261123-210325-t2r2-xray. Verbatim:
```
X-noplan PASS: 0 unplanned restarts (); last line rc=0: True
X-freeze FAIL: 0 STOP, 0 CONT; game_ms deltas []; bad []; [FREEZE] absorbed lines 0
X-track FAIL: 4546 poses applied; dist p50 0.326 p95 1.906 max 8.18 m; off-mesh holds 412; torn 0
X SIDE (r2): FAIL
```
- **X SIDE r2 = FAIL.** It stands.
- X-freeze: nothing to measure. ITRZone reports a rig defect: after the first planned death (play 240 s) its driver
  loaded a save that does not exist, the player stayed in the Hub, and the pose said `dead` + `keep` for the rest. So
  no freeze was ever requested; freeze.csv has only its header. Not an X-Ray fault, but the check needs >= 1 STOP: FAIL.
- **X-track: this one is MY side's defect, and my earlier readings were wrong.**
  - Every pose in r2 came from track time 0-380 s, where every track row has node_dist >= 0. Still 412 of 4546 poses
    (9 %) were HELD by my on-mesh test, and the held distance (2-8 m) gives p95 1.906 m.
  - The HOLD stretches are at the SAME pose seq in all three runs. r2: 503-525, 576-586, 1394-1438, 1817-1889, ...;
    r1b: 503-..., 1408-1439, 1839-1890, ...; r1: 503-525.
  - So r1's 18 rows over 1 m at seq 510-525 were NOT the rig pawn falling, as I wrote in section 21. They were this
    same held spot (track time ~25-26 s; a slope, y -19.3 -> -17.5, node_dist 0.3-0.8 m). And r1b's "p95 1.44 m outside
    the off-mesh leg" (section 22) was this too.
  - Cause, read from the script, not yet measured: my test (zz_p1_puppet.script:176-178) needs an AI node in the exact
    0.7 m cell under the pose, with its Y within 2 m. The track's own node_dist only says a node is NEAR (up to 1.8 m
    away). Small holes in the AI mesh (slopes, props) therefore HOLD the actor although a player can stand there.
    The script does not log which of the two conditions failed.
  - The frozen script is unchanged. A fix (accept a pose when a node is near, log the HOLD reason) is a change to a
    frozen file: it needs its own registration and a short X-only pre-flight. Asked the Overseer.

## 24. Fix of the pose on-mesh test, registered BEFORE r2b (Overseer approved; a bridge behaviour defect; X-track unchanged)
- **Cause, now measured offline.** tools/t2_mesh_offline.py reads Cordon's level.ai (624,967 nodes, 0.7 m cells; ITRZone's
  extracted copy, read only) and samples the track at 20 Hz as ITRZone's writer does.
  - The OLD test (own cell only) holds 482 of 7,600 poses (6.3 %) on track 0-380 s. In 474 the pose's own cell has no
    node; in 8 the node is more than 2 m off in height.
  - The model's held runs are 502-524, 576-586, 1394-1437, 1817-1820, 1823-1889, ... The real runs held
    503-525, 576-586, 1394-1438, 1817-1821, 1824-1889, ... (r1, r1b, r2). They match to one pose, so the cause is confirmed.
- **New rule (zz_p1_puppet.script, t2_near_ring):** a pose is on the mesh if an AI node lies within +-2 cells
  (1.4 m) in x and in z of the pose's cell AND within +-2.0 m of the pose's height. The own cell is tried first.
  - Why 2 cells: +-1 cell still holds 1.9 %, +-2 holds 0.1 %, +-3 holds 0 %. 1.4 m is under the track's own largest
    node_dist (1.82 m). Height stays at the old 2.0 m.
  - The probe calls level.vertex_id at pose y + 2.0 and needs node y >= pose y - 2.0. vertex_id returns the highest
    node at or below the y given, so this means "any node within the band".
  - Logging: `HOLD,<tg>,<seq>,nonear` (the reason; the only one left), `NEAR,<tg>,<seq>,<ring 1|2>` when a neighbour
    cell was needed, and `near=<n>` in T2SUM. The scorer reads only the first HOLD field, so it is unchanged.
  - Nothing outside the -t2_pose path changes. No engine change.
- **Offline check (the Overseer's condition), REAL script + REAL mesh + REAL track (t2-offline/mesh_stub.lua):**
  - on-mesh part 0-380 s: 7 of 7,600 poses held (0.09 %, one run of 7 poses at seq 3292-3298); 467 accepted through
    a neighbour cell (329 ring 1, 138 ring 2).
  - witness, the off-mesh leg 410-600 s: 3,800 of 3,800 held. PASS on both.
  - Limits of the model: node height is the cell-centre height (plane tilt ignored); the engine's physics is not modelled.
- **Not tested in the engine yet.** The new Lua has only run under lua5.1 with stubs. r2b is r2's one retry, so I asked
  the Overseer for one short X-only pre-flight first (t2_preflight.sh, about 6 min, WANT queue): PASS = no Lua error,
  poses applied, NEAR lines present. If refused, r2b carries that risk.
- **New frozen hash:** harness/zz_p1_puppet.script sha256
  887f44889623bb44fb5a6e83f405a98925d5da0aa0cebadfd76c2d6e76784617 (was 966e3a47...).
  Unchanged: t2_xray.sh 6ae8d27f..., lib_engine_pids.sh cb183b7b..., Absorb14.exe e35a289b..., scorer 16e89cb7...
- **Expectation for r2b (stated before it runs):** X-track p95 <= 1.0 m, at 75 %. In r2 the non-held poses had p50 0.33 m;
  the risk is slopes where the engine's physics moves the actor after placement. X-freeze: one STOP..CONT pair at death 2
  (ITRZone: death 1 is `keep`, death 2 is `freeze`), game time delta <= 1000 ms, at 80 %.

## 25. Pre-flight 5: the new on-mesh test inside the engine (Overseer approved; registered BEFORE the run; X-Ray only)
- One WANT slice, 20-min claim, about 6-7 min of engine time, one boot, no kill, no stall, no freeze.
  `PF_OUT=out/t2pre5 PF_EXE=Absorb14.exe PF_FREEZE=sigstop PF_ABSORB=2000 PF_PAUSE=0 PF_KILL_AT=99999 PF_RUN_S=170
   PF_POSE_ARG=track:<cordon_full_track.csv> tools/t2_preflight.sh`
- The stand-in pose writer (t2_preflight_pose.py, new `track:` mode; NOT ITR2's code) plays the real track from t = 0 by
  wall time at about 20 Hz, state play/keep, yaw 0. About 150 s of track, which covers the old held spots at track time
  25-26 s, 29 s, 70-72 s, 91-94 s and 109-110 s.
- Script under test: harness/zz_p1_puppet.script sha256 887f4488... (section 24). t2_xray.sh is unchanged (6ae8d27f...).
- **PASS needs all of:**
  1. no Lua error naming zz_p1_puppet in xray.log, and t2_xray rc 0;
  2. at least 2,000 P lines;
  3. at least 1 NEAR line (the new path really ran);
  4. HOLD lines: at most 10 (the offline model predicts 0 for track 0-150 s), each with the reason `nonear`.
- Reported, not a pass condition: P distance p50/p95/max. My prediction: p95 <= 1.0 m, at 75 %.
- A FAIL is reported and the cause found before any r2b; the script is not handed to ITRZone until this passes.

## 26. Pre-flight 5 RESULT: PASS (slice 22:19:43-22:23:08 local, released; read once; out/t2pre5)
- 1. PASS: no Lua error from zz_p1_puppet; t2_xray rc 0.
  - xray.log has 5 STACK TRACEBACK blocks, all from GAMMA mods registering unknown callbacks at load. r2's log has the same 5.
  - It has 83 "Invalid position for CLevelGraph::vertex_id" lines, at points far from the track. r2's log, with the old
    script, has 82, so they are not from the new probe.
- 2. PASS: 3,082 P lines (need >= 2,000).
- 3. PASS: 239 NEAR lines (204 ring 1, 35 ring 2).
- 4. PASS: 0 HOLD lines (at most 10 allowed; the offline model predicted 0).
- Reported: P distance p50 0.301 m, p95 0.585 m, max 1.863 m. My prediction (p95 <= 1.0 m, 75 %) was right.
- The old held spots (track 25-26 s, 29 s, 70-72 s, 91-94 s, 109-110 s) were all played without a hold.
- Limits: the stand-in writer played ~150 s of track forward only, yaw 0, no deaths, no ITR2.
- **zz_p1_puppet.script stays frozen at 887f4488... for r2b.** No file changed after the pre-flight.

## 27. Joint r2b X-side RESULT: PASS (ITRZone run 20261123-222755; r2's one retry; scored ONCE, scorer 16e89cb7..., r2)
Out dir ~/Projects/ITRZone/out/runs/20261123-222755-t2r2b-xray. Verbatim:
```
X-noplan PASS: 0 unplanned restarts (); last line rc=0: True
X-freeze PASS: 1 STOP, 1 CONT; game_ms deltas [198]; bad []; [FREEZE] absorbed lines 1
X-track PASS: 21365 poses applied; dist p50 0.291 p95 0.608 max 2.228 m; off-mesh holds 22; torn 0
X SIDE (r2): PASS
```
- Freeze (death 2, `freeze`): STOP 03:40:48.47Z, CONT 03:42:05.76Z, 77.3 s frozen. Engine line:
  `[FREEZE] absorbed 77277 ms of a 77310 ms gap`. Game time moved +198 ms across it (limit 1000 ms).
  Death 1 (`keep`) made no STOP, as designed.
- Tracking: 22 HOLDs of 21,365 poses (0.1 %), all `nonear`, at seq 3292-3298 (the run the offline model predicted),
  3821, 14945-14951 and 21533-21539 (the same track spot on later ping-pong passes). 1,373 poses were accepted through
  a neighbour cell. 125 poses were over 1 m. Both predictions of section 24 were right.
- Observed, not scored (follow-up): at ITR2's end (03:52:07Z) the script logged a second ABSORB_ARM (seq 24671), but
  the bridge sent no STOP, and the pose went stale 3 s later. So the absorb stayed armed for X-Ray's last ~6 min with
  no freeze. No gap was absorbed in that time (only 1 absorbed line in the log), but an armed absorb without a STOP
  breaks the Overseer's condition "armed only during a freeze". Cause not yet read. To fix before T3: disarm on STALE,
  and find why the script and the bridge disagreed on that last pose.
- ITR2 side (ITRZone, scored once): FAIL, 5 of 7.
  - T2d: its checker counts snapshots X-Ray wrote after ITR2 had ended. X-Ray ran its full T2_RUN_S = 1500 s.
  - T2e: 7 spawn failures in its mod.
  - The Overseer decides what follows.

## 28. Fix of the stray absorb arm, registered BEFORE r3 (Overseer order after r2b)
- **Cause (confirmed with ITRZone's header values):** ITR2's one final pose is state `end` with `freeze`
  (seq 24671 in r2b), and then nothing is written. The old script armed on ANY non-play state with `freeze`. The
  bridge freezes only on `dead` or `away`. So the script armed, no STOP came, and the flag stayed 1 for ~6 min.
- **New rules (zz_p1_puppet.script):**
  - ARM only on state `dead` or `away` with `freeze`: exactly the bridge's own test.
  - DISARM, each logged as `ABSORB_DISARM,<tg>,<seq>,<game ms>,<reason>`:
    - `play`: a play pose (the path after every CONT);
    - `state`: any other pose that is not dead/away + freeze (for example `end`);
    - `stale`: the pose is stale and X-Ray is still running 4 s after arming;
    - `timeout`: X-Ray is still running 10 s after arming;
    - `end`: at game end.
  - Why 4 s and 10 s: the bridge stops X-Ray within ~3 s of the ARM line (it waits at most 3 s for it; r2b: 26 ms).
    While X-Ray is stopped no Lua runs, and an absorbed gap does not advance time_global, so a real freeze never
    reaches either limit. X-Ray still running after them means no stop is coming.
  - The engine itself already clears the flag when it absorbs a gap (perf/freeze-absorb 7c8331264, unchanged).
  - "Disarm on bridge exit": the bridge cannot run a console command inside X-Ray. If the bridge is gone, no STOP comes,
    and the `stale` / `timeout` rules clear the flag within 4-10 s. t2_xray.sh is NOT changed (6ae8d27f...).
- **Fake tests, REAL script with engine stubs (t2-offline/arm_stub.lua), old script 887f4488 as the witness.**
  Engine flag at the end, old -> new:
  - end_freeze (r2b's case): 1 -> 0 (new: no ARM at all).
  - dead_then_stale (writer dies after the dead pose, no STOP): 1 -> 0 (`stale`, 4 s after arming).
  - dead_no_stop (poses keep coming, bridge never stops): 1 -> 0 (`timeout`, 10 s).
  - dead_then_end: 1 -> 0 (`state`).
  - armed_at_game_end: 1 -> 0 (`end`).
  - dead_then_play (the CONT path): 0 -> 0 (`play`).
  - real_freeze (stop, gap absorbed, then play): 0 -> 0 (`play`); no early disarm.
  - The on-mesh offline test is unchanged: 7 of 7,600 held, 467 near.
- **New hash:** harness/zz_p1_puppet.script sha256
  11c81c5538133c2efb8daf613af6328eb682a9fc65cf3914c86c496cda9efffd (was 887f4488...). All other hashes unchanged.

## 29. Pre-flight 6: the new arm rules inside the engine (registered BEFORE the run; X-Ray only)
- One WANT slice, 20-min claim, one boot, about 6 min.
  `PF_OUT=out/t2pre6 PF_EXE=Absorb14.exe PF_FREEZE=sigstop PF_ABSORB=2000 PF_PAUSE=0 PF_KILL_AT=99999 PF_RUN_S=130
   PF_POSE_ARG=pf6 tools/t2_preflight.sh`
- Stand-in writer (`pf6` mode; NOT ITR2's code), actor held at its start point:
  - play 0-20 s;
  - dead + freeze 20-50 s (a real bridge STOP, armed);
  - play 50-70 s;
  - then ONE final pose `end` + `freeze` and no more writes (r2b's case), with X-Ray running ~45 s more.
- **PASS needs all of:**
  1. no Lua error from zz_p1_puppet; t2_xray rc 0;
  2. freeze.csv: exactly one STOP and one CONT; game_ms delta <= 1000; one `[FREEZE] absorbed` engine line;
  3. exactly one ABSORB_ARM line, followed by one `ABSORB_DISARM,...,play`;
  4. NO ABSORB_ARM after the `end` pose (the old script armed there).
- A FAIL is reported and fixed before the hash goes to ITRZone for r3.

## 30. Pre-flight 6 RESULT: PASS (slice 23:19:20-23:22:35 local, released; read once; out/t2pre6)
- 1. PASS: no Lua error from zz_p1_puppet; t2_xray rc 0.
- 2. PASS: one STOP (04:20:19.38Z) and one CONT (04:20:49.48Z); game_ms 3165217612 -> 3165217810, +198 ms;
  one engine line `[FREEZE] absorbed 30071 ms of a 30104 ms gap`.
- 3. PASS: exactly one `ABSORB_ARM` (seq 401), then `ABSORB_DISARM,52205,1003,3165217810,play`.
- 4. PASS: the final `end` + `freeze` pose (seq 1398) was read (the STALE line names seq 1398) and NO ABSORB_ARM
  followed. X-Ray ran 105 s more with the flag clear.
- **Correction to section 26.** I wrote there that all 5 STACK TRACEBACK blocks come from GAMMA mods. One of the 5 is from
  my own script, and it is in every run since r1: `[axr_main callback_set] callback on_game_end doesn't exist!`, at
  zz_p1_puppet.script's `RegisterScriptCallback("on_game_end", on_quit)`.
  - It is a warning, not a Lua error: the script loads and runs.
  - It means my quit handler is registered only for `on_before_level_changing`, never for the game's end. So the new
    `end` disarm and the final T2SUM line do not run when X-Ray simply exits.
  - Harmless for the flag: it is a NoSave console variable and dies with the process. The 60 s T2SUM lines still appear.
  - The fake test's armed_at_game_end case passes only because the stub accepts that callback name. In the engine that
    path is NOT covered.
  - Left as is for r3 (the hash stays). To fix later with a real callback name.
- **zz_p1_puppet.script is frozen for r3 at 11c81c5538133c2efb8daf613af6328eb682a9fc65cf3914c86c496cda9efffd.**
  Unchanged: t2_xray.sh 6ae8d27f..., lib_engine_pids.sh cb183b7b..., Absorb14.exe e35a289b..., scorer 16e89cb7...
- **Expectation for r3 (before it runs):** X side PASS on all three, at 80 %. The X side is the same as r2b except the
  arm rules; the risk is run-to-run variation in X-track (r2b p95 0.608 m).

## 31. Joint r3 X-side RESULT: PASS (ITRZone run 20261123-232930; scored ONCE, scorer 16e89cb7..., arg r2)
Out dir ~/Projects/ITRZone/out/runs/20261123-232930-t2r3-xray. Verbatim:
```
X-noplan PASS: 0 unplanned restarts (); last line rc=0: True
X-freeze PASS: 1 STOP, 1 CONT; game_ms deltas [198]; bad []; [FREEZE] absorbed lines 1
X-track PASS: 21356 poses applied; dist p50 0.29 p95 0.611 max 91.46 m; off-mesh holds 22; torn 0
X SIDE (r2): PASS
```
- ITR2 side (ITRZone, scored once): PASS, all 8. **So r3 passes on both sides.** My expectation (X PASS, 80 %) was right.
- Freeze: STOP 04:42:24.26Z, CONT 04:43:41.54Z, 77.3 s; `[FREEZE] absorbed 77276 ms of a 77309 ms gap`; game time +198 ms.
- Arm rules: one ABSORB_ARM (seq 11149), one `ABSORB_DISARM,...,play` (seq 12655), and NO arm at ITR2's `end` pose
  (the last pose is seq 24715; STALE follows it, nothing else). r2b's stray arm is gone.
- Tracking: 22 HOLDs (the same spot as r2b: seq 3292-3298, 3821, 14950-14956, 21538-21544), 1,375 near accepts,
  122 poses over 1 m.
- Observed, unexplained (passes the check, but worth a look before T3): three single poses read 91.5, 75.2 and 90.5 m
  from the pose (seq 19655, 19660, 19663, within 0.4 s, time_global ~931.6-932.0 s). The poses between them are normal
  (0.6-1.4 m), and r2b has nothing like it at the same seq. So for single ticks the actor's position, read right
  after it was set, was ~90 m away. Not checked: the log has `pfx_teleport` particle lines (854.8 s), so a teleport
  anomaly moving the actor is one candidate; a stale position read is another.

## 32. The three ~90 m single-tick readings in r3: what they most likely are (logs and code only; no box; NOT reproduced)
- Facts:
  - P lines seq 19655, 19660, 19663 read 91.5, 75.2 and 90.5 m; the poses between read 0.6-1.4 m. All three were
    accepted (NEAR ring 1), so the actor WAS set to the pose position on those ticks.
  - ITRZone: its writes there are normal (50 ms cadence, state play, no turn, no death). Play clock ~950 s, which is
    track time ~190 s on the second forward leg. Its position code is a plain interpolation and cannot make a one-pose jump.
  - Track at 186-193 s: x 61-64, z 159-177, y climbing -0.06 -> 2.76 -> 0.69 within 6 m, node_dist up to 1.74 m. It is
    a steep bank. In the 0.3 s before the first reading the distance was already 1.0-1.4 m, above the usual 0.3 m.
  - The script reads `a:position()` in the statement right after `a:set_actor_position(pos)`. Nothing else runs in between.
- Reading from the code: the only code between the set and the read is the engine's own setter.
  `set_actor_position(pos)` -> `CActor::ForceTransform` -> `CCharacterPhysicsSupport::set_movement_position`
  -> `CollisionCorrectObjPos` (CharacterPhysicsSupport.cpp:746-790). That activates the actor's box at the target, lets the
  physics push it out of whatever it overlaps, and then MOVES the entity by the result (`Position().sub(activation_res, shift)`).
  - So on those three ticks the collision corrector threw the actor ~90 m, because the pose point sat inside
    geometry on the bank. The next pose put it back.
  - A teleport anomaly is ruled out as the direct cause: it cannot act between two consecutive Lua statements.
    The `pfx_teleport` log lines are particle statistics, 77 s earlier.
  - A stale read is ruled out the same way: position() returns the XFORM that the setter has just written.
- Not shown: the exact pose coordinates (nobody logged them) and the reproduction. r2b passed the same track point
  several times with no such reading, so it needs a particular sub-cell position.
- Consequence for real play (T3): a pose point inside X-Ray geometry can throw the actor far away for a tick. For
  A-Life that is a one-tick 90 m move of the player: online/offline radius, NPC vision and anomalies all see it.
  T3's "X-Ray grounds itself" design must not use the collision-correcting setter blindly. Options for the T3 note:
  - place the actor at the AI node's height plus a small lift, with the setter's skip-collision flag, and let
    physics settle it;
  - or check the result and re-place the actor when it lands more than a few metres from the target.
- For T3 I asked ITRZone to log x/y/z per pose write, and X-Ray should log the pose position it applied.

## 33. T3 step 1: script revision (pose v2 grounding, guard, logging) and pre-flight A, registered BEFORE any boot
Design: DESIGN-T3-xray-side-2026-11-24.md (Overseer approved a51b261cf). Pose v2, agreed with ITRZone:
`p,x,yfloor,z,yaw,hp,stance` (X-Ray axes, y up; yfloor = ITR2 feet + 0.3 m, a hint; stance 0|1|2). A `p` line with 5
fields stays v1 and takes the T2 path, unchanged.
- **Script (zz_p1_puppet.script sha256 1f923b67b334390688feb0b9b02654c683871190936213fbf1f1e1b900af0c90):**
  - Floor: `level.vertex_id` at yfloor + 0.7 m, in the pose's cell, then within +-2 cells. A node is accepted if it is
    within 3.0 m of the ITR2 feet height. No node: `HOLD,<tg>,<seq>,nonode`.
  - Height: physics owns it. The actor keeps its current height while that is within 0.7 m of the node height (1.4 m
    when the node came from a neighbour cell). Otherwise it is snapped to node height + 0.2 m. This is the note's
    "alternative" form, chosen now because a node's stored height is its cell-centre height and is off on slopes.
  - Setter: `set_actor_position(pos, true, keep_speed)`, the skip-collision form. keep_speed is true except on a snap,
    so gravity keeps acting between poses.
  - Guard: after the set, if the actor is more than 5.0 m from the target, a `GUARD` line with both positions is
    logged and the actor is put back to the last good position.
  - Log: `P2,seq,utc,tg,horizontal dist,pose yaw,actor yaw,target x,y,z,node id,node y,ring,snap,actor y,stance`.
  - Stance: calls `actor:set_actor_stance(k)` if the engine has it, else logs STANCE_UNSUPPORTED once. (The export
    is not built yet; it gets its own registration.)
  - The quit handler now registers on `actor_on_net_destroy`; `on_game_end` does not exist (section 30).
- **Offline tests, REAL script + REAL Cordon mesh + REAL track (t2-offline/t3_stub.lua; no physics in the stub):**
  - v2, track 0-380 s: 7,593 of 7,600 poses applied, 7 HOLD (the known spot), 0 GUARD, 0 collision-correcting sets.
    Node height against track height: p50 0.01 m, p95 0.13 m, max 1.67 m.
  - Hint moved by +2.4 m and by -0.6 m (ITR2's measured ground differences): the same result.
  - The two-floor spot (track 84-87 s): hint on the upper floor gives nodes at -3.7..-3.8; hint -9.7 gives the lower
    floor (-10.0; three neighbour picks at -8.9).
  - Off-mesh leg 410-600 s: 3,800 of 3,800 HOLD.
  - Guard: a setter stub that throws the actor 90 m on two sets gives exactly 2 GUARD lines, and the actor is put back.
  - Without the export: STANCE_UNSUPPORTED once, poses applied.
  - v1 poses: the T2 result unchanged (7 HOLD, 467 near, all sets collision-correcting). The seven arm cases still
    end disarmed.
- **Pre-flight A (X-Ray only, one WANT slice, 20-min claim, one boot, ~7 min):**
  `PF_OUT=out/t3pfA PF_EXE=Absorb14.exe PF_FREEZE=sigstop PF_ABSORB=2000 PF_PAUSE=0 PF_KILL_AT=99999 PF_RUN_S=200
   PF_POSE_ARG=track2:<cordon_full_track.csv> tools/t2_preflight.sh`
  The stand-in writer plays the real track from t = 0 as pose v2: x and z from the track, yfloor = track height + 0.3 m
  (the height is only a hint now), stance 0/1 alternating every 10 s. About 180 s of track: three old hold spots, the
  two-floor spot (84.6-86.2 s) and slopes.
- **Scored once by tools/t3_pfA_check.py (sha256 056eb474...). PASS needs all of:**
  1. no Lua error from zz_p1_puppet, and t2_xray rc 0;
  2. at least 2,000 P2 lines;
  3. no GUARD line, and no two consecutive applied targets more than 5.0 m apart;
  4. at most 10 HOLD lines.
- Reported, not pass conditions: horizontal distance after the set; actor height minus track height (p05/p50/p95);
  snaps; rings. Predictions: PASS at 70 %; |actor height - track height| p95 <= 0.5 m at 55 % (the open risk is how the
  physics settles the actor with the skip-collision setter: it may sink, bob or lag on slopes).
- A FAIL is reported and the cause found before pre-flight B or C.

## 34. T3 engine change: the `set_actor_stance` script export, registered BEFORE it is built (Overseer approved)
- **Why an export with stored state.** Read from the code: `CActor::UpdateCL` clears the wishful crouch and accel bits
  every frame unless the player's toggle options are on (Actor.cpp:1874-1877); a held key sets them again each frame
  (ActorInput.cpp:408-418). A script that only set the bits at 20 Hz would lose them on the next frame.
- **Change (branch perf/t3-stance, on perf/freeze-absorb 7c8331264):**
  - `CActor` gets `int m_script_stance`, default -1 (= no script stance).
  - `actor:set_actor_stance(k)`: k = 0 stand, 1 crouch, 2 low crouch (crouch + accel); any other value sets -1.
  - In `UpdateCL`, right after the stock per-frame clear: only if `m_script_stance >= 0`, the crouch and accel bits of
    `mstate_wishful` are set from it (both cleared for 0). With -1 the block does nothing.
  - `actor:get_actor_stance()` returns the stored value, for the check.
  - Not saved, not sent over the net, no console variable.
- **Rule One.** Nothing in GAMMA calls the new function, so `m_script_stance` stays -1 and the frame path is the stock
  code plus one integer compare. Only the bridge's puppet script calls it.
- **Checks before it is used in any joint run:**
  1. CI builds with 0 errors.
  2. "The exe without the bridge behaves as before": one X-only boot of the new exe WITHOUT `-t2_pose` (the T1 track
     path, which never calls the export), compared with the same boot on Absorb14. Both must reach the end with rc 0,
     with no `set_actor_stance` or STANCE line anywhere, and with the actor's movement state unchanged (0 crouch
     frames in both). Details registered with pre-flight B.
  3. Pre-flight B (with the bridge): STANCE lines appear, `get_actor_stance` reads back what was set, and the actor's
     real crouch state (`IsMoveState('mcCrouch')`) follows within a few frames; stance 0 stands it up again.
     The NPC "noticed later when crouched" witness is tried there, report-only.
- The puppet script already calls the export when present (section 33); no script change is needed for this.

## 35. Pre-flight A RESULT: PASS on its four conditions, but the grounding assumption was wrong; script revised; pre-flight A2 registered BEFORE its boot
- **Run:** out/t3pfA, Absorb14.exe, script 1f923b67..., slice 2026-11-24 01:43-01:47, rc 0. Scored ONCE with
  t3_pfA_check.py (056eb474...).
- **Result:** (1) no Lua error, rc 0: PASS. (2) 3,673 P2 lines: PASS. (3) 0 GUARD, largest step 1.499 m: PASS.
  (4) 7 HOLD (the known no-node spot): PASS. STANCE_UNSUPPORTED once, as expected on Absorb14.
- **Reported:** horizontal distance after the set 0.0 m throughout. Actor height minus track height: p50 -0.10,
  p05 -0.55, p95 +0.22; |.| p95 0.578, max 1.06 m. 27 snaps.
- **My guesses:** PASS (70 %): right. |height difference| p95 <= 0.5 m (55 %): WRONG (0.578).
- **What the data shows (looked at after scoring, exploratory):** the actor's height never changes between sets.
  Between two snaps it stays at exactly the last snap height (for example -20.21 for 400+ poses while the node height
  climbs from -19.89 to -19.62), then steps up at the next snap. So with the skip-collision setter at 20 Hz, X-Ray's
  physics does NOT settle the actor on the ground. The design assumption in section 33 ("physics owns the height
  while within tolerance") was wrong: the actor was up to 0.78 m under its node and up to 0.68 m over it; in 899 of
  3,673 poses it was more than 0.3 m under.
- **Script revision (sha256 e90c1555...):**
  - Height is the node height + 0.05 m on EVERY set (T3_LIFT 0.2 -> 0.05). `snap` now only marks a height step
    larger than 0.7 m (report).
  - Measured only, never applied: a ray straight down onto static geometry (the `ray_pick` class, flags 2, as GAMMA's
    own scripts use it) from 1.2 m above the node, range 3 m, inside pcall. Its ground height is a new P2 field
    (-9999 = no hit); a failure logs `RAY_ERROR` once. Purpose: learn how far the node height is from the collision
    ground, and whether a ray can serve the no-node spots.
  - New P2 field `drift`: the actor's height now minus the height set on the previous pose (does the engine move it
    at all between sets).
  - Nothing else changed; the v1 path is untouched.
- **Offline (REAL script, REAL mesh, REAL track; t3_stub.lua):** v2 0-380 s: 7,593 applied, 7 HOLD, 0 GUARD, actor
  minus node 0.05 on every pose; with a fake ray class the ray field is right on 1,200 of 1,200; thrown setter:
  2 GUARD; off-mesh leg: 3,800 of 3,800 HOLD; v1: 7,600 P, 7 HOLD, 467 near (unchanged). The arm cases were not
  re-run (that code is untouched).
- **Pre-flight A2 (same command as A, new output dir; one WANT slice, 20-min claim, one boot):**
  `PF_OUT=out/t3pfA2 PF_EXE=Absorb14.exe PF_FREEZE=sigstop PF_ABSORB=2000 PF_PAUSE=0 PF_KILL_AT=99999 PF_RUN_S=200
   PF_POSE_ARG=track2:<cordon_full_track.csv> tools/t2_preflight.sh`
- **Scored ONCE with t3_pfA2_check.py (a89de5eb...). PASS needs all of:** conditions 1-4 as in A; (5) actor height
  = node height + 0.05 m (+-0.01) on every P2 line; (6) no RAY_ERROR line. Witness for (5): the same checker on
  pre-flight A's data FAILS it (worst 0.826 m).
- **Report-only:** ray hits, ray ground minus node height (p05/p50/p95/min/max), drift, actor minus track height.
- **Guesses, written before the boot:** PASS 75 %. Ray hits on >= 90 % of poses: 60 %. |ray ground - node height|
  p95 <= 0.3 m: 55 %. Drift nonzero on < 1 % of poses: 70 %.
- **Stop rule:** FAIL means find the cause before anything else; at most one retry.
- **Stance build:** perf/t3-stance 8a835585a, CI 45331173775 success, installed as run/bin/Stance14.exe
  (sha256 b4ce9c772ae5fa4b...). Not run yet.

## 36. Pre-flight A2 RESULT: PASS. Pre-flight B (stance export, with the bridge) registered BEFORE its boot
- **A2 run:** out/t3pfA2, Absorb14.exe, script e90c1555..., slice 2026-11-24 02:19-02:23, rc 0. Scored ONCE with
  t3_pfA2_check.py (a89de5eb...).
- **A2 result: PASS, 6 of 6.** 3,672 poses applied; 0 GUARD, largest step 0.99 m; 7 HOLD; actor height = node + 0.05
  on every pose; no RAY_ERROR.
- **Reported:** actor minus track height p50 +0.05, |.| p95 0.18, max 0.61 m (A: 0.578 / 1.06). Ray hit on 3,672 of
  3,672 poses; ray ground minus node height p05 -0.11, p50 -0.02, p95 +0.11, min -0.51, max +1.20 m; |.| p95 0.18.
  Height drift between sets: nonzero on 2,429 of 3,672 poses, p05 -0.10, p50 -0.02, max +0.99 m.
- **My guesses:** PASS (75 %): right. Ray hits >= 90 %: right (100 %). |ray - node| p95 <= 0.3 m: right (0.18).
  Drift nonzero on < 1 % of poses: WRONG (66 %).
- **What the drift means (exploratory):** set just above the ground, the actor does sink a few centimetres toward the
  real ground between sets (2,260 of the nonzero drifts are downward). So physics acts when the actor is set near
  the ground; in A it was set under or far above it and did not move. The node height is within about 0.2 m of the
  collision ground on 95 % of this route. A ray is usable as a ground source (for the no-node spots): not applied yet.
- **Script for B (sha256 751022ac...):** adds only logging. `MOVESUM,frames=,crouch=,low=,unread=,stance_api=` at
  the end (frames in real crouch / low crouch, read with GAMMA's own `IsMoveState`, the function its vision script
  uses), and two P2 fields: the real movement state (0/1/2, -1 unread) and `get_actor_stance` (-9 = no export).
  Offline: all earlier cases unchanged; A2's checker still passes on the stub output.
- **Pre-flight B (X-Ray only, one WANT slice, 20-min claim, one boot):**
  `PF_OUT=out/t3pfB PF_EXE=Stance14.exe PF_FREEZE=sigstop PF_ABSORB=2000 PF_PAUSE=0 PF_KILL_AT=99999 PF_RUN_S=200
   PF_POSE_ARG=track3:<cordon_full_track.csv> tools/t2_preflight.sh`
  Stance14.exe = perf/t3-stance 8a835585a, sha256 b4ce9c772ae5fa4b... The writer (fbe74ebe...) plays the track as
  pose v2 with the stance cycling 0, 1, 2 every 10 s.
- **Scored ONCE with t3_pfB_check.py (c2c59a55...). PASS needs all of:** conditions 1-6 as in A2; (7) at least 15
  STANCE lines and no STANCE_UNSUPPORTED; (8) `get_actor_stance` equals the pose stance on every P2 line; (9) the
  real movement state equals the pose stance on at least 80 % of P2 lines, and after every stance change it matches
  within 2 s (changes in the last 2 s of data are not judged); (10) the movement state is readable on every P2 line.
- **Witnesses:** the checker FAILS A2's data (no export: conditions 7-10). Offline, a stub whose movement state
  follows the export one tick late PASSES; a stub that never crouches FAILS condition 9.
- **Report-only:** MOVESUM, heights, ray, drift as in A2. The "NPC notices a crouched actor later" witness is NOT
  attempted here: this pre-flight has no measure of it. It moves to the joint T3 as report-only.
- **Guesses, written before the boot:** PASS 55 %. Risks: `IsMoveState` not readable from my script (20 %); the
  actor does not really crouch while it is being set 20 times a second (20 %); low crouch (2) reads as 1 (10 %).
- **Order changed, with the reason:** section 34 listed the no-bridge boot (check 2) before B. B goes first because
  it costs 7 minutes and shows whether the movement state can be read at all; the no-bridge check needs that reading
  and costs about 40 minutes (two full T1 tracks, Stance14 and Absorb14). Both are still done before any joint run.
- **Stop rule:** FAIL means find the cause first; at most one retry.

## 37. Pre-flight B RESULT: PASS. The no-bridge check (section 34, check 2) registered BEFORE its boots
- **B run:** out/t3pfB, Stance14.exe (b4ce9c77...), script 751022ac..., slice 2026-11-24 02:45-02:49, rc 0. Scored
  ONCE with t3_pfB_check.py (c2c59a55...).
- **B result: PASS, 10 of 10.** Conditions 1-6 as A2 (3,671 poses, 0 GUARD, 7 HOLD, largest step 0.99 m). (7) 19
  STANCE lines, 0 unsupported. (8) `get_actor_stance` equals the pose stance on 3,671 of 3,671. (9) the real
  movement state equals the pose stance on 3,369 of 3,671 (92 %); all 18 judged changes followed within 2 s.
  (10) 0 unread.
- **Reported:** the mismatches are the transition after each change, about 17 poses (0.85 s) on average.
  MOVESUM frames=12983, crouch=3030, low=4784, unread=0. Heights and ray as in A2 (|actor - track| p95 0.18 m).
- **My guess:** PASS (55 %): right. None of the three named risks happened.
- **No-bridge check ("the exe without the bridge behaves as before"):** one WANT slice, 55-min claim,
  `tools/t3_nobridge.sh` (966953f1...): two full T1 track boots (`-p1 -b1_snap`, no `-t2_pose`, no pose file, stall
  off, tools/t1_xray.sh unchanged), Stance14.exe then Absorb14.exe, same script 751022ac... If the first boot does not
  end cleanly the second is not started.
- **Scored ONCE with t3_nobridge_check.py (1fb36539...). PASS needs, for BOTH boots:** (1) track ended, rc 0, no FATAL
  ERROR; (2) no STANCE line in the meta file and no `set_actor_stance` in the engine log; (3) MOVESUM with
  crouch=0, low=0, unread=0 and at least 10,000 frames; (4) stance_api = -1 on Stance14 and `na` on Absorb14.
- **Witness:** the checker FAILS the old clamp-fix witness run (no MOVESUM line) and passes it once the expected
  MOVESUM lines are added by hand. Pre-flight B shows the counters do count crouch frames when there are any.
- **Report-only:** each boot's SUMMARY line.
- **Guess, written before the boots:** PASS 85 %. Risk: a boot ends early for an unrelated reason (the T1 retry
  crash class).
- **Stop rule:** FAIL means find the cause first; at most one retry.

## 38. No-bridge check RESULT: PASS. Pre-flight C (R3: level change on a `cross` pose) registered BEFORE its boot
- **No-bridge run:** out/t3nb, slice 2026-11-24 03:22-04:00, script 751022ac..., two full T1 track boots. Scored ONCE
  with t3_nobridge_check.py.
- **Result: PASS, 8 of 8.** Both boots: track ended, rc 0, no FATAL; no STANCE line and no `set_actor_stance`
  anywhere; 0 crouch and 0 low-crouch frames (Stance14 68,766 frames, Absorb14 68,979), 0 unread. The export read
  -1 (never set) on Stance14 and is absent on Absorb14.
- **Reported:** SUMMARY lines alike (hits 0, cancelled 0, min health 1.0; export mean 587 us vs 563 us, p99 1,018 us
  vs 1,052 us; one run each, so no speed claim).
- **My guess:** PASS (85 %): right. With sections 36 and 37 this closes section 34's checks 1-3 for the stance export.
- **Script for C (sha256 d5283d23...), change from 751022ac:**
  - Pose state `cross` whose zone is not the current level: once, find a level changer in the save that stands on
    this level and leads to that level (server objects of class level_changer_s; levels from the game graph), log
    `CROSS_REQ,<tg>,<seq>,<from>,<to>,<changer>,<dest x,y,z>,<lvid>,<gvid>,<pose utc>`, flush, write the marker file
    `cross.txt` on the bridge tmpfs, and call the stock `ChangeLevel(dest position, lvid, gvid, zero angle)`.
    `CROSS_NOCHANGER` or `CROSS_ERROR` otherwise. `cross` never arms the absorb and is never stopped.
  - My reading (untested): the level change restarts the Lua state. So at start a non-empty marker means "continue
    the same log": the meta file is opened in append mode and the marker is emptied; the first update logs
    `CROSS_RESUME,<tg>,<level>,<marker>`.
  - Every start logs `CHANGER,<name>,<from>,<to>,<dest x,y,z>,<lvid>,<gvid>` for the changers from or to the current
    level (the table ITRZone asked for). A failure of that lookup logs `CHANGER_ERROR` and nothing else changes.
  - Play path otherwise unchanged. Offline: the cross, resume and no-changer cases behave as described
    (t3c-draft/t3c_stub.lua); the earlier v2, v1 and off-mesh cases give the same counts with the lookup failing in
    the old stub (CHANGER_ERROR logged, nothing else affected).
- **Runner t2_xray.sh (96160265..., was 6ae8d27f...):** one addition: while the pose state is `cross` the 30 s
  "no new seq" rule is skipped and logged ("cross hold"). No stop is sent.
- **Stand-in writer (f3455247...), mode `cross:<dest>:<track>`:** v2 track poses for 40 s, then `cross` with zone
  <dest> until the snapshot header names <dest> and 30 s more, then `play` on the new level at the actor's own
  snapshot position (yfloor = its height + 0.3).
- **Pre-flight C (X-Ray only, one WANT slice, 20-min claim, one boot):**
  `PF_OUT=out/t3pfC PF_EXE=Stance14.exe PF_FREEZE=sigstop PF_ABSORB=2000 PF_PAUSE=0 PF_KILL_AT=99999 PF_RUN_S=320
   PF_POSE_ARG=cross:l02_garbage:<cordon_full_track.csv> tools/t2_preflight.sh`
- **Scored ONCE with t3_pfC_check.py (80ba5729...) `out/t3pfC l01_escape l02_garbage`. PASS needs all of:**
  (1) rc 0, no Lua error naming the script, no FATAL ERROR; (2) exactly one CROSS_REQ l01_escape -> l02_garbage and
  no CROSS_NOCHANGER / CROSS_ERROR; (3) exactly one CROSS_RESUME on l02_garbage after it; (4) the snapshot header
  names l02_garbage at most 180 s after the first `cross` pose; (5) after the resume at least 200 P2 lines and no
  GUARD; (6) the runner logged the cross hold and there is no restart row.
- **Witnesses:** the checker FAILS pre-flight B's data (conditions 2-6) and PASSES stitched stub data.
- **Report-only:** the load time, the CHANGER table, HOLD count on the new level, the summary lines of both Lua
  states, anything unexpected in the engine log around the change.
- **Guess, written before the boot:** PASS 35 %. This is the first level change in this harness. Named risks: the
  Lua state does not restart the way I read it, or the script does not start again on the new level (25 %); the
  headless run's own overlay ends or hangs the run at the change (20 %); no changer found because the class or field
  names differ in this build (15 %); the load takes over 180 s (10 %).
- **Stop rule:** FAIL means find the cause first; at most one retry. A crash stops everything.
- **The run writes an autosave on the level change into the project's own run/appdata (never Caden's saves; the
  runner's no-guard check stays on).**

## 39. Pre-flight C RESULT: PASS. Script frozen for the first joint T3 pre-flight
- **Run:** out/t3pfC, Stance14.exe, script d5283d23..., runner 96160265..., slice 2026-11-24 04:32-04:39, rc 0.
  Scored ONCE with t3_pfC_check.py (80ba5729...).
- **Result: PASS, 6 of 6.** One CROSS_REQ l01_escape -> l02_garbage through `esc_level_changer_to_garbage_1`
  (destination 30.431, 0.489, -326.702, level vertex 188617, game vertex 764); one CROSS_RESUME on l02_garbage; the
  snapshot named l02_garbage 21.7 s after the first `cross` pose; 5,470 P2 lines on Garbage, 0 GUARD, 0 HOLD;
  the runner logged the cross hold; no restart; no Lua error, no FATAL.
- **Reported:** the Lua state did restart as read (the first state wrote its T2SUM / MOVESUM at the change, the second
  appended to the same file). The engine-time clock kept running (CROSS_REQ at 71,600 ms, CROSS_RESUME at 78,259).
  Snapshot seq restarted (486 before, 408 at the end of the hold). The project's cordon1 save is unchanged
  (sha256 6dea752b... before and after). The CHANGER table is in t3-changers-cordon-garbage.csv
  (name, from, to, destination x,y,z, level vertex, game vertex): Cordon -> Garbage has two changers, Garbage ->
  Cordon two.
- **My guess:** PASS at 35 %: it passed, so I was under-confident; none of the four named risks happened.
- **Not covered by C:** which of two changers to use (the script takes the first that leads to the level; real play
  should take the one nearest the ITR2 player's crossing point); crossing back; a crossing while a freeze is active.
- **Script frozen for the joint pre-flight: sha256 899c28e9...** One addition after C, logging only: a `VITALS`
  line at the end (lowest health, highest radiation and bleeding seen, read in pcall each update), for Caden's rule
  that the actor takes no damage from anything. Offline: all earlier cases unchanged; a stub actor with radiation
  0.25 / bleeding 0.1 reports them. This one line has NOT run in the engine yet; the joint pre-flight is its first
  boot (a read failure only counts `unread`).

## 40. First joint T3 pre-flight (ITRZone arm j1): X side, registered BEFORE the boot. No boot before the Overseer's go
- **ITR2 side:** ITRZone amendment 100 (their 8d73a2a), copied verbatim into PREREG-B1-T2-JOINT-2026-11-23.md and
  diff-checked. ITRZone verified my hashes on disk.
- **Frozen X files:** harness/zz_p1_puppet.script 899c28e9...9c485; run/bin/Stance14.exe b4ce9c77...46cbe;
  tools/t2_xray.sh 96160265...63136; tools/lib_engine_pids.sh cb183b7b...8ed6e.
- **X env (from ITRZone's driver):** T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999
  T2_KILL_AT=99999 T2_RUN_S=640. T2_ABSORB_SKIP is left out; the runner's default is 0, the same as r3's explicit 0.
- **ITR2 levers:** t2v=2 t2st=30 t2stop=380 (the on-mesh first 380 s once; stance 0,1,2 stepping every 30 s; no
  death, no gap, no freeze).
- **X side scored ONCE** with `t3_jpf_xray_check.py <X out dir> <cordon_full_track.csv>` (75020915...). **PASS needs
  all of:** (1) rc 0 and no Lua error naming the script; (2) at least 6,000 P2 lines; (3) no GUARD and no step over
  5 m; (4) at most 15 HOLD; (5) actor height = node height + 0.05 m on every P2; (6) no RAY_ERROR; (7) at least 10
  STANCE lines and no STANCE_UNSUPPORTED; (8) `get_actor_stance` = pose stance on every P2; (9) the real movement
  state = pose stance on at least 80 % of P2 and after every change within 2 s; (10) movement state readable on
  every P2; (11) no restart row.
- **Witness:** the checker FAILS pre-flight B's data on (2) only (3,671 poses), as it should for a 200 s run.
- **Report-only:** the VITALS line (first engine run of that logging); the per-seq compare of ITR2's `T2 wrote`
  x, z, stance against my P2 lines; yfloor minus my node height; ray and drift as before.
- **Guess, written before the boot:** X side PASS 65 %. Risks: ITR2's yfloor picks another floor or misses the 3 m
  band at more spots than the track did, so HOLD goes over 15 (15 %); fewer than 6,000 poses applied because ITR2's
  write rate is under 20 Hz (10 %); a 30 s stance step leaves a change unfollowed within 2 s (5 %).
- **Stop rule:** a FAIL is reported, not tuned; at most one retry, registered first.
- **Open for R3, not part of j1:** ITRZone will send `c,<changer name>`; the lookup by name is not built yet
  (the script takes the first changer leading to the level; each Cordon/Garbage pair has two, about 180 m apart).

## 41. Joint pre-flight j1: X side RESULT (scored once, 2026-11-24)

- **Run:** ITRZone's driver, 2026-11-24 10:46-10:57 UTC. X out dir
  `~/Projects/ITRZone/out/runs/20261124-054342-t2j1-xray`; ITR2 lines
  `~/Projects/ITRZone/out/runs/20261124-054342-stream-t2j1/t2_lines_for_xside.txt`. Checker 75020915..., puppet script
  899c28e9... (both hashes checked on disk before scoring).
- **X SIDE: PASS, 11 of 11.** ITR2 side (ITRZone's scoring): PASS 6/6.
  1. No Lua error, rc 0: PASS.
  2. P2 lines >= 6,000: PASS (7,338).
  3. No GUARD, no step over 5 m: PASS (0 GUARD, largest step 1.70 m).
  4. HOLD <= 15: PASS (8, all "nonode": seqs 3292-3298 and 3821).
  5. Actor height = node height + 0.05 m on every P2: PASS.
  6. No RAY_ERROR: PASS.
  7. STANCE lines >= 10, none unsupported: PASS (13, 0).
  8. get_actor_stance = pose stance on every P2: PASS (0 differ of 7,338).
  9. Real movement state = pose stance: PASS (7,326 of 7,338; all 12 changes followed within 2 s).
  10. Movement state readable on every P2: PASS (0 unread).
  11. No restart row: PASS (0).
- **Guess:** PASS 65 %. It passed; none of the three named risks happened (HOLD 8, 7,338 poses, 0 unfollowed).
- **Report-only:**
  - `VITALS,min_health=1.000,max_radiation=0.0000,max_bleeding=0.0000,unread=0`. First engine run of this logging.
  - Per-seq compare with ITR2's `T2 wrote` (7,600 play writes): all 7,338 P2 seqs are in ITR2's log; x and z differ
    by at most 0.005 m (ITR2's log rounds to 2 decimals); stance differs on 0.
  - Play seqs not applied: 262. 245 before my first apply (seq 246, engine still loading), 8 HOLD, 9 single seqs the
    20 Hz reader skipped.
  - Actor height minus the height ITR2 sent: p05 -0.447, p50 -0.230, p95 -0.144, min -3.112, max +0.374 m (ITR2
    sends its ground + 0.3 m, so about -0.25 is agreement).
  - Actor height minus track height: p50 +0.050, |.| p95 0.212, max 1.616 m.
  - Ray: hit on 7,338 of 7,338; ray ground minus node height p05 -0.108, p50 -0.021, p95 +0.097, min -0.611,
    max +1.198 m. Height drift between sets nonzero on 4,382 of 7,338 (min -0.497 m).
  - Snaps 4; rings 0/1/2: 6,871 / 330 / 137. MOVESUM frames 37,350, crouch 6,533, low 6,544.
- **Not covered by j1:** death, gap, freeze, kill, level change, and the 8 no-node poses (the ray hit there is
  measured but not used). Vitals become gating from T3 proper (Overseer).

## 42. Pre-flight D (R3: level change by changer NAME, there and back), registered BEFORE its boot

- **Order (Overseer, 2026-11-24):** build the changer lookup by name, give it an X-only pre-flight, then the joint
  crossing Cordon -> Garbage -> Cordon. No boot before the Overseer's go.
- **Script installed: sha256 1d7143cf...30f45** (was 899c28e9..., j1's). Change: a pose line `c,<changer name>` is
  read; on `cross`, with a name only that changer is used, and only if it stands on this level and leads to the pose's
  zone; no fallback (`CROSS_NOCHANGER,...,<name>` otherwise). Without a `c` line the first changer is taken as before.
- **Offline (t3c-draft/t3c_stub.lua, two changers to the same level):** no name -> the first; the second's name ->
  the second (its destination and vertices); an unknown name -> CROSS_NOCHANGER, no marker, no level change; a
  changer that leads the other way -> CROSS_NOCHANGER. A 60 s play run gives the same lines as 899c28e9 (all but the
  wall-clock field of G lines).
- **Stand-in writer (2cb582bc...ef4d6), new mode `crossn:<dest1>=<changer1>,<dest2>=<changer2>:<track>`:** 40 s of v2
  track poses; then per leg `cross` with the zone and a `c,<changer>` line until the snapshot names that level and
  30 s more; then 30 s of `play` there at the actor's snapshot position; then the next leg. Smoke-tested against a
  fake snapshot source.
- **Run (X-Ray only, one WANT slice, 20-min claim, one boot):**
  `PF_OUT=out/t3pfD PF_EXE=Stance14.exe PF_FREEZE=sigstop PF_ABSORB=2000 PF_PAUSE=0 PF_KILL_AT=99999 PF_RUN_S=400
   PF_POSE_ARG=crossn:l02_garbage=esc_level_changer_to_garbage_2,l01_escape=gar_level_changer_to_escape_2:<cordon_full_track.csv>
   tools/t2_preflight.sh`. Both names are the SECOND changer of their pair, so a fallback to the first would show.
  Runner t2_xray.sh 96160265..., t2_preflight.sh 88f2cb18..., Stance14.exe b4ce9c77... (all unchanged).
- **Scored ONCE with t3_pfD_check.py (461b8e53...20dfd)**
  `out/t3pfD perf-harness/t3-changers-cordon-garbage.csv l01_escape l02_garbage esc_level_changer_to_garbage_2 gar_level_changer_to_escape_2`.
  **PASS needs all of:** (1) rc 0, no Lua error naming the script, no FATAL ERROR; (2) exactly two CROSS_REQ, in
  order, each through the named changer with that changer's destination and vertices from the table; no
  CROSS_NOCHANGER / CROSS_ERROR; (3) exactly two CROSS_RESUME, on Garbage then on Cordon, each after its request;
  (4) each new level in the snapshot at most 180 s after the first `cross` pose of that leg; (5) after each resume
  at least 200 P2 lines and no GUARD; (6) the first P2 after each resume within 5 m (x,z) of the changer's
  destination; (7) the runner logged two cross holds and there is no restart row.
- **Witness:** the checker FAILS pre-flight C's data on 2-7 (one crossing, by the first changer).
- **Report-only:** load times, HOLD counts, the VITALS / SUMMARY / MOVESUM lines of each Lua state, the cordon1 save
  hash before and after, anything unexpected in the engine log.
- **Guess, written before the boot:** PASS 60 %. Risks: the way back behaves differently from the way out (second
  Lua restart, marker file, the runner's second cross hold) (20 %); the second changer's arrival spot has no AI node
  under it, so fewer than 200 poses apply (10 %); a bug in the new writer mode that the fake test did not show (10 %).
- **Stop rule:** FAIL means find the cause first; at most one retry, registered first. A crash stops everything.
- **Not covered:** an unknown name in the engine (offline only); a crossing during a freeze; the bridge live
  throughout (that is the joint crossing run, registered separately with ITRZone).

## 43. T3 proper, X side (FINAL as written: ITRZone amendment 103 copied into the joint prereg 2026-11-24)

- **Order (Overseer):** the full on-mesh track with stance and both deaths (keep, freeze) as in T2 r3, vitals gating,
  one registration. ITRZone asks the Overseer before the box run.
- **ITR2 side (ITRZone's proposal, arm t3):** hcheal=2 t2span=380 wtest=6 t2die=240 t2dien=2 t2flip=1 t2v=2 t2st=30;
  the on-mesh 380 s ping-ponged, about 18 min of play clock; death 1 at play 240 s under keep, death 2 at 480 s under
  freeze; stance steps every 30 s of play clock and is unchanged through a death.
- **X env:** T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_RUN_S=1500.
- **X files:** script 1d7143cf...30f45 (the by-name script; its play path is the j1 script's). It is frozen for T3
  only after pre-flight D has run with it. Stance14.exe b4ce9c77..., t2_xray.sh 96160265..., lib_engine_pids.sh
  cb183b7b...
- **While ITR2's player is dead or away:** under keep the script applies no pose; the actor stays where it was last
  set, A-Life runs on, hits are cancelled, vitals are still read. Under freeze the process is stopped by the bridge.
- **Scored ONCE with t3_xray_check.py (fda8c5f8...97ff5) `<X out dir> <cordon_full_track.csv>`. PASS needs all of:**
  1-11 as in j1 (section 40) with these numbers: (2) at least 17,000 P2 lines; (4) at most 45 HOLD (j1: 8 per 380 s
  pass); (7) at least 25 STANCE lines. New: (12) exactly one STOP..CONT pair, game time across it at most 1000 ms,
  exactly one "[FREEZE] absorbed" engine line; (13) one VITALS line with lowest health >= 0.999, highest radiation
  <= 0.0001, highest bleeding <= 0.0001, 0 unread; (14) the SUMMARY line shows hits = cancelled and 0 refills.
- **Witnesses:** j1's data FAILS on 2, 7 and 12 only; r3's data PASSES 12 (and fails the pose-v2 conditions, as it
  must: r3 used pose v1 and an older script).
- **Report-only:** T3c (the two-floor spot at track 84.6-86.2 s) and T3e as agreed; the hit lines (who hit, type,
  power); the per-seq compare with ITR2's `T2 wrote`; height, ray and drift numbers as in j1.
- **What the vitals gate does and does not test:** j1 covered this route once with no hit at all. T3 is 25 min with
  two periods where the actor stands still, so creature hits are likely; (14) tests that each is cancelled, (13)
  that nothing else lowers health. The route does not cross a radiation field or an anomaly that I know of, so
  (13) says little about those; a separate check is needed for them.
- **Guess, written before the boot:** X side PASS 50 %. Risks: a vitals or refill failure in 25 min with creatures
  near (20 %); HOLD over 45 or a guard trip around the deaths (10 %); a stance change not followed within 2 s
  around a death (10 %); the freeze check (5 %); anything else (5 %).
- **Stop rule:** a FAIL is reported, not tuned; at most one retry, registered first.

## 44. Pre-flight D RESULT: PASS. Script frozen for T3 proper

- **Run:** out/t3pfD, Stance14.exe, script 1d7143cf...30f45, writer 2cb582bc..., runner 96160265..., slice
  2026-11-24 06:33:00-06:42:42 local (Overseer's go), rc 0. Scored ONCE with t3_pfD_check.py (461b8e53...).
- **Result: PASS, 7 of 7.**
  1. rc 0, no Lua error, no FATAL: PASS.
  2. Two CROSS_REQ through the named changers with the table's values: PASS. Out by
     `esc_level_changer_to_garbage_2` (-152.625, 2.255, -297.913; level vertex 55976, game vertex 762); back by
     `gar_level_changer_to_escape_2` (-162.193, -2.852, 329.859; 88513, 367). No CROSS_NOCHANGER / CROSS_ERROR.
  3. Two CROSS_RESUME, Garbage then Cordon, each after its request: PASS.
  4. New level in the snapshot after the first `cross` pose: PASS (21.7 s out, 21.5 s back).
  5. After each resume at least 200 P2, no GUARD: PASS (598 on Garbage, 7,065 back on Cordon; 0 GUARD).
  6. First applied pose within 5 m of the changer's destination: PASS (0.000 m and 0.001 m).
  7. Two runner cross holds, no restart: PASS.
- **Guess:** PASS 60 %. It passed; none of the three named risks happened.
- **Reported:** 0 HOLD on both levels. Each of the three Lua states wrote its own SUMMARY and VITALS line: hits 0,
  refills 0, lowest health 1.000, radiation 0, bleeding 0, 0 unread, all three. The project's cordon1 save is
  unchanged (scoc a9f9a8a9..., scop 6dea752b... before and after).
- **Consequence for the joint crossing run's checker:** a crossing run has one VITALS line per Lua state (three for
  there-and-back), so its vitals gate must take every line, not "exactly one" as T3 proper does.
- **Not covered:** an unknown name in the engine (offline only); a crossing during a freeze or a death; the bridge
  live throughout (the joint crossing run).
- **Script frozen for T3 proper: sha256 1d7143cf7883bf6b0af1b82202cf442245411e4d41291d4ad3e58ec735330f45.**

## 45. T3 proper: X side RESULT (scored once, 2026-11-24)

- **Run:** ITRZone's driver (arm t3, amendment 103), Overseer's go. X out dir
  `~/Projects/ITRZone/out/runs/20261124-071431-t2t3-xray`; ITR2 lines
  `~/Projects/ITRZone/out/runs/20261124-071432-stream-t2t3/t2_lines_for_xside.txt`. Checker fda8c5f8..., script
  1d7143cf... (both checked on disk before scoring).
- **X SIDE: PASS, 14 of 14.** ITR2 side (ITRZone's scoring): PASS 9/9.
  1. No Lua error, rc 0: PASS.
  2. P2 lines >= 17,000: PASS (21,343).
  3. No GUARD, no step over 5 m: PASS (0 GUARD; largest step 1.70 m, across both deaths too).
  4. HOLD <= 45: PASS (22, all nonode: the same two spots as j1, the 7-pose spot on three passes).
  5. Actor height = node height + 0.05 m on every P2: PASS.
  6. No RAY_ERROR: PASS.
  7. STANCE lines >= 25, none unsupported: PASS (37, 0).
  8. get_actor_stance = pose stance on every P2: PASS (0 differ of 21,343).
  9. Real movement state = pose stance: PASS (21,231 of 21,343; 35 changes judged, 0 not followed within 2 s).
  10. Movement state readable on every P2: PASS (0 unread).
  11. No restart row: PASS.
  12. Freeze: PASS (1 STOP, 1 CONT; game time across it 198 ms; 1 absorbed line).
  13. Vitals: PASS (`VITALS,min_health=1.000,max_radiation=0.0000,max_bleeding=0.0000,unread=0`).
  14. Every hit cancelled, no refill: PASS (`SUMMARY,hits=0,cancelled=0,refills=0,min_health=1.000,...`).
- **Guess:** PASS 50 %. It passed; none of the named risks happened. I was under-confident again.
- **What 13 and 14 do NOT show:** there was no hit on the actor at all in 25 minutes (hits=0), also not during the
  two standstills. So the cancel path and the no-damage rule were not exercised by a real hit in this run; the gate
  passed with nothing to cancel. A run where creatures do attack the actor is still owed.
- **Report-only:**
  - Per-seq compare with ITR2's `T2 wrote` (21,630 play writes): all 21,343 P2 seqs are in ITR2's log, none on a
    non-play seq; x and z differ by at most 0.005 m; stance differs on 0.
  - Play seqs not applied: 287 (244 before my first apply, 22 HOLD, 21 single seqs the 20 Hz reader skipped).
  - Actor height minus the height ITR2 sent: p05 -0.427, p50 -0.231, p95 -0.147, min -3.153, max +0.374 m. 588
    poses more than 1.0 m below, 68 more than 2.5 m below (the object at x 62.6, z 167, as expected).
  - Actor height minus track height: p50 +0.050, |.| p95 0.208, max 1.616 m. Ray hit on every pose; ray ground minus
    node height p05 -0.108, p50 -0.021, p95 +0.097, min -0.612, max +1.200 m. Height drift between sets nonzero on
    14,536 of 21,343 (min -0.747, max +1.079 m).
  - Snaps 13; rings 0/1/2: 19,968 / 969 / 406. MOVESUM frames 85,812, crouch 19,497, low 24,652.
  - Export cost: mean 711 us, p99 1,358 us, max 8,491 us (one run; no speed claim).
- **Not covered:** a real hit on the actor; radiation fields and anomalies; a level crossing with the bridge live
  (R3, next); a real player's stance.

## 46. Correction to section 45, and pre-flight E (forced damage at the actor), registered BEFORE its boots

- **Correction (my error in wording T3's check 14):** every run since the P1 retry turns engine god mode on
  (`g_god on`). In the engine, god mode zeroes a hit on the actor and returns before the script hit callback runs
  (Actor.cpp:671), so the script's hit handler never fires and `hits` is always 0. This was already recorded in
  PREREG-P1-puppet ("hit logging under god mode: FAILED, not measurable"). Check 14 (hits = cancelled) could
  therefore not fail. The T3 result stands (no damage occurred); check 14 carries no evidence.
- **Overseer's ruling (2026-11-24):** Caden's requirement is that X-Ray's actor takes no damage. "Logged" was the
  Overseer's wording for diagnosis, not Caden's rule. Pre-flight E now, with the current exe and no engine change;
  a hit log line in the god-mode path is optional, low priority, later.
- **What protects the actor today:** engine god mode, plus a health refill to 1.0 on every actor update and after
  each hit. With god mode off, GAMMA's own damage script lowers health directly inside the hit callback.
- **Script (sha256 a0322a90...944b8, was 1d7143cf...):** a test mode that is ON only if the bridge tmpfs holds
  `dmgtest.txt` with `prot`, `god_off` or `god_off_norefill`. Only tools/t2_preflight.sh (bdd67a5b...58af7) writes
  that file (PF_DMG) and removes it after the boot. Offline: without the file, a 60 s play run and a named crossing
  give the same lines as 1d7143cf; with it, the three phases run in the stub.
  - `rad`, track time 20-80 s: the actor is pinned at the centre of the nearest `zone_field_radioactive*` object of
    the level. `anom`, 80-140 s: the same for the nearest `zone_mine_*` of kind electric, acid, chemical, thermal or
    gravitational. `mut`, 150-240 s: `dog_strong_red`, `boar_01a_hard` and `cat_strong_a` are spawned 4 m from the
    actor, which the pose holds in place. Each target is logged by name and section (`DMG_TARGET`, `DMG_SPAWN`).
  - Once a second: `DMG_AT` (distance to the zone centre, zone online, health, radiation, bleeding) or `DMG_MUT`
    (per mutant: distance, alive, its enemy, and the actor's three values). `DMG_PHASE_END` per phase.
  - `prot`: god mode on, refill on, as in every run so far. `god_off`: god mode off, the script's own hit cancel
    off, refill on. `god_off_norefill`: refill off too; below health 0.2 the script sets it back to 1.0 and logs
    `DMG_FLOOR`, because a death would end the run.
- **Runs:** tools/t3_pfE.sh (29ecc163...b293e): three X-only boots of about 7 min, each its own WANT slice with a
  20-min claim, in the order god_off_norefill, god_off, prot. Stand-in writer mode `nofreeze` (the actor held at its
  start point). Stance14.exe b4ce9c77..., t2_xray.sh 96160265... (unchanged). Out dirs out/t3pfE_<mode>.
- **Each boot scored ONCE with t3_pfE_check.py (bcabd34b...1c48c) `<out dir> <mode>`.**
  - All modes: (1) clean run, the mode line, god mode on for prot and off for the witnesses; (2) rad: one target
    and at least 40 lines with the actor within 1.5 m of the zone centre and the zone online; (3) anom: the same;
    (4) mut: three spawned and at least one mutant engaged for at least 10 lines (alive, its enemy is the actor,
    within 3 m).
  - prot, (5): health >= 0.999, radiation <= 0.0001, bleeding <= 0.0001 on every line, in every phase and in the
    VITALS line; 0 refills.
  - Witnesses, (W): damage shows in EVERY phase (hits logged, or health below 0.99, or radiation or bleeding above
    0.001); god_off_norefill also needs health below 0.9 or radiation above 0.01 in at least one phase.
- **Reading the three results together:** the no-damage claim holds for a source only if the witness shows damage
  from that source and prot shows none. A phase with no damage in the witnesses proves nothing for that source.
- **Checker witnesses:** PASSES stub data in prot; FAILS stub data as a witness (the stub does no damage); FAILS
  pre-flight D's data on all five.
- **Evidence of engagement is by position and target, not by animation:** the script cannot read the mutant's
  attack animation; the witness boots' hit lines (god mode off, so the callback fires) name the hitter.
- **Guess, written before the boots:** all three boots PASS: 35 %. Named risks: a zone gives no damage to an actor
  standing still at its centre, or the centre is under the ground (25 %); the actor dies in a witness boot despite
  the refill or floor (15 %); no radiation field or usable anomaly on Cordon in this save (10 %); the spawned
  mutants do not attack or spawn badly (10 %); prot shows a radiation or bleeding change that god mode does not
  cover (10 %).
- **Stop rule:** a boot with rc not 0 stops the series. A FAIL is reported with its cause, not tuned; at most one
  retry, registered first.
- **Rule One:** the spawns and placements happen only in these test boots, on the project's own test save, which is
  not written (hash checked before and after).

### 46a. Amendment before the first boot of E (Overseer's condition with the go, 2026-11-24)
- The test-mode control file must not be left behind and must be absent before any T or R run.
- tools/t2_preflight.sh (773ac813..., was bdd67a5b...): removes the file on ANY exit of the runner (trap), and passes T2_DMG_OK=1
  to the runner only when PF_DMG is set.
- tools/t2_xray.sh (6597a067..., was 96160265...): at every start, unless T2_DMG_OK=1, a control file found on the tmpfs is
  removed and the removal is logged. ITRZone's driver never sets T2_DMG_OK, so no joint run can start in test mode.
  Nothing else in the runner changes. This new runner hash is the one R3 will freeze.
- Checked in isolation: the file is removed without the flag and kept with it.

## 47. Pre-flight E, first try: FAIL (stopped at the first boot). The ONE retry registered BEFORE its boots

- **Run:** tools/t3_pfE.sh, slice 2026-11-24 08:23:03-08:30:09 local, boot god_off_norefill only. The runner ended
  with rc 6 (two unplanned restarts), so the series stopped as registered; god_off and prot did not run.
  Scored ONCE with t3_pfE_check.py: **FAIL** (1 FAIL: rc not 0; 2 rad PASS; 3 anom FAIL: 19 lines, then the run
  ended; 4 mut FAIL: phase not reached; W FAIL: only the rad phase finished).
- **Cause (my named risk "the actor dies in a witness boot", 15 %):** with god mode off and no refill, the actor
  died in the electric anomaly about 19 s into that phase. The 0.2 floor is checked once per update and health
  fell from 0.29 to below 0 between two updates. Snapshots stopped, the runner restarted the engine twice (each
  restart ran the test mode again), then the run time was used up.
- **What the boot did show (protection off):**
  - Radiation field `esc_zone_field_radioactive_weak_0005` (-109.2, 4.7, 241.2): 60 of 60 lines inside, zone
    online; 592 hits of type 3 (radiation) from `zone_field_radioactive_weak`; radiation rose to 0.904; health fell
    to 0.000 (floor used 5 times in the phase).
  - Anomaly `zone_mine_electric_weak27423` (-121.1, 4.0, 226.8): inside from the first second; 7 hits of type 1
    (shock), 0.325 damage each by GAMMA's own damage print; the actor died.
  - So both zone placements do reach the actor and do damage when the protection is off. The mutant phase and the
    protected run have no data yet.
- **The test save is unchanged** (scoc a9f9a8a9..., scop 6dea752b... before and after); the control file was
  removed.
- **Retry, changes (script 1b1c08d1..., was a0322a90...; driver 8229f869..., was 29ecc163...):**
  - god_off_norefill: the floor is 0.7 instead of 0.2 and is also applied right after each hit, so a death needs a
    single hit of 0.7 or more. "Refill off" therefore means: no refill to 1.0 on every update; health can fall to
    0.7 before it is restored.
  - Both witnesses: at the end of each phase health is set to 1.0 and radiation back to 0 (`DMG_RESET`), so the
    anomaly and mutant phases do not start with 0.9 radiation from the first phase.
  - Driver: T2_MAXRESTART=0, so a dead actor ends the boot instead of restarting it in test mode. Out dirs
    out/t3pfE2_<mode> (PFE_PREFIX=t3pfE2).
  - Offline: without the control file the play and crossing lines are still the same as 1d7143cf; the reset lines
    appear in the stub.
- **Unchanged:** the checker (bcabd34b...), its conditions, the three modes and their order, the phases, the env.
- **Guess, written before the boots:** all three boots PASS: 40 %. Risks: the actor still dies in a witness
  (15 %); the spawned mutants do not attack, spawn badly, or are killed by the anomaly nearby (20 %); the protected
  run shows a radiation or bleeding change (10 %); other (15 %).
- **Stop rule:** this is the one retry. A second FAIL is reported with its cause and nothing more is run without
  a new registration and the Overseer's word.

## 48. Pre-flight E, the one retry: FAIL (stopped after the first boot). E stops here and goes to the Overseer

- **Run:** PFE_PREFIX=t3pfE2 tools/t3_pfE.sh, slice 2026-11-24 09:14:23-09:19:42 local, boot god_off_norefill only
  (script 1b1c08d1..., driver 8229f869...). The runner ended with rc 5, so the series stopped; god_off and prot did
  not run. Scored ONCE with t3_pfE_check.py (bcabd34b...): **FAIL**, on condition 1 only.
  1. Clean run: FAIL (rc 5: "no new seq for 30 s", restarts used up).
  2. rad: PASS (60 of 60 lines inside `esc_zone_field_radioactive_weak_0005`, zone online).
  3. anom: PASS (60 of 60 lines inside `esc_zone_mine_gravitational_weak_0050` at -98.9, 24.1, 318.1).
  4. mut: PASS (three spawned; the boar engaged on 59 lines and the cat on 63: alive, enemy = the actor, within 3 m).
  W. Damage in every phase with the protection off: PASS.
- **Cause of the FAIL (a bug of mine in the test mode):** all three phases finished (the last `DMG_RESET` at engine
  time 270,890 ms). The last line of the log is 5.7 s later. After the last phase the witness mode left god mode
  off and applied no floor any more, while the boar and the cat were still attacking and the actor was bleeding.
  The actor died after the test had ended, snapshots stopped, and the runner counted a dead engine.
- **What this boot shows, protection off (god mode off, no script cancel, floor 0.7):**
  - Radiation field: 594 hits in the phase (type 3, about 10 a second); radiation to 0.916; lowest health 0.392.
  - Gravitational anomaly: 40 hits of type 7 in the phase; lowest health 0.442. Radiation hits went on during
    this phase too (559), so that spot also lies in a radiation field.
  - Mutants: 35 hits of type 5 (24 from `cat_strong_a`, 13 from `boar_01a_hard` over the whole boot); lowest
    health 0.278; bleeding rose to 10.0. The dog ran off (67.9 m away, its enemy was the cat).
  - The floor was used 77 times. The reset of radiation between phases worked (0 in the mutant phase).
- **So the witness side is complete in substance:** each of the three sources damages an unprotected actor, with
  the hitter named in the hit lines. Together with the first try, radiation and anomaly are shown twice.
- **Still missing:** the protected run (the one that tests Caden's rule), and the witness with the refill on.
- **The test save is unchanged** (hashes as before); the control file was removed after the boot.
- **My guess (all three PASS, 40 %):** wrong. The named risk "the actor still dies in a witness" happened, though
  not in the way I named: it died after the phases, through an end-of-test gap I did not see.
- **Stop:** as ordered, E stops here. Nothing more runs without a new registration and the Overseer's word.
- **Proposal for that registration (not run):** at the end of the last phase the witness modes turn god mode on
  and the refill on, clear bleeding and radiation, and log it, so the actor survives to the end of the boot. Then
  run only the two missing boots (god_off, prot). The no-refill witness would be taken from this boot's data, with
  its rc recorded as failed for the stated reason.

## 49. Pre-flight E2 (forced damage, end of test fixed), registered BEFORE its boots. No boot before the Overseer's go

- **Overseer's order (2026-11-24):** E's FAIL stands. E2 is a new pre-flight: the mutants are removed and the
  protection is restored BEFORE the test mode ends, with an end-of-test check that the actor is alive and not
  bleeding; all three boots run again on the fixed script so the witnesses and the protected run share one build;
  a mutation for the end gap (skip the restore) must fail.
- **Script (sha256 7f613c84...ce957, was 1b1c08d1...):**
  - At the end of the last phase, in every mode: the three spawned mutants are released (`DMG_REMOVE` per mutant);
    radiation and bleeding are cleared, health is set to 1.0, `g_god on` is executed, and from then on the refill
    and the script's hit cancel work as in a normal run (`DMG_RESTORE,<time>,g_god=,clear=,remove_errors=`).
  - 10 s later, once: `DMG_ENDCHECK,<time>,alive=,health=,radiation=,bleeding=,mutants_left=,g_god=`.
  - Mutation mode `god_off_norefill_norestore`: as god_off_norefill, but the restore is skipped on purpose
    (`DMG_NORESTORE`); the mutants are still removed.
  - Offline: without the control file the play and crossing lines are the same as the frozen 1d7143cf; with it,
    all four modes run in the stub and write the new lines.
- **Driver tools/t3_pfE.sh (9de62bfc...91fb6):** four X-only boots, each its own WANT slice (20-min claim, about
  7 min each): god_off_norefill, god_off, prot, then the mutation. A boot of the first three with rc not 0 stops
  the series. `PFE_PREFIX=t3pfE3`, out dirs out/t3pfE3_<mode>. Env as before (Stance14.exe, writer `nofreeze`,
  PF_RUN_S=330, T2_MAXRESTART=0). Runner t2_xray.sh 6597a067..., t2_preflight.sh 773ac813...
- **Each boot scored ONCE with t3_pfE_check.py (11fcdb6f...5d9ef).** Conditions 1-5 and W as in section 46, plus
  for ALL modes: **(6)** exactly one DMG_RESTORE with g_god=true, clear=ok and remove_errors=0, and exactly one
  DMG_ENDCHECK with alive=1, health >= 0.999, radiation <= 0.0001, bleeding <= 0.0001, mutants_left=0, g_god=true.
- **E2 PASSES only if:** god_off_norefill PASS, god_off PASS, prot PASS, and the mutation boot FAILS (on 6 or 1).
  A source is covered only if both witnesses show damage from it and prot shows none.
- **Checker witnesses:** stub prot data PASSES all six; stub mutation data FAILS on (6) (no restore line, still
  bleeding); the E retry boot's real data FAILS on (1) and (6) and passes 2, 3, 4 and W as before.
- **Not shown offline:** that `bleeding = 1` and `change_radiation` clear the real values, and that releasing an
  online mutant works; the stub only models them. The end check measures exactly that in the engine.
- **Guess, written before the boots:** E2 PASS (three PASS and the mutation FAIL): 40 %. Risks: bleeding or
  radiation is not fully cleared at the end check, or a release fails (20 %); the actor dies in the refill-on
  witness from one big hit (10 %); mutants do not engage in one of the boots (10 %); the protected run shows a
  change in radiation or bleeding (10 %); the mutation boot passes by luck (5 %); other (5 %).
- **Stop rule:** a FAIL is reported with its cause, not tuned. No retry without a new registration.
- **Rule One:** spawns, releases and placements happen only in these test boots on the project's test save, which
  is not written (hash checked before and after). The control file is removed after every boot (section 46a).

## 50. Pre-flight E2: FAIL (scored once, 2026-11-24). The protected run passes; both witnesses fail the end check

- **Boots:** four, each its own slice (10:30, 12:09, 13:16, 13:58), out/t3pfE3_<mode>. No retry was used. Script
  7f613c84, checker 11fcdb6f, as registered. Test save unchanged (scoc a9f9a8a9..., scop 6dea752b...). The control
  file was absent after the series.
- **Result per boot:**
  - god_off_norefill (witness): **FAIL on 6 only.** 1, 2, 3, 4, W PASS. Hits: radiation 593 (health down to 0.46,
    radiation 0.90), gravitational anomaly 17 (0.50), mutants 32 (0.30, bleeding 10). End check: alive, health 1.0,
    radiation 0, mutants 0, god mode on, but **bleeding 7.65**.
  - god_off (witness, refill on): **FAIL on 6 only.** 1, 2, 3, 4, W PASS. 1,230 hit lines. End check as above but
    **bleeding 10.0**.
  - prot (the protected run): **PASS 6/6.** Zero hits reach the script in all three phases; health 1.000, radiation
    0, bleeding 0 on all 390 lines; no refill needed; end check clean.
  - god_off_norefill_norestore (mutation): **FAILS, as it must** (on 6: no restore line, health 0.10, bleeding 10,
    god mode off; and on 1: runner rc 5).
- **E2 by its rule: FAIL** (it needs both witnesses to PASS). The guess was 40 % PASS; the named 20 % risk
  ("bleeding is not fully cleared at the end check") is what happened.
- **Cause (read from code after scoring):** the restore clears bleeding with `a.bleeding = 1`. Section 49 said this
  heals all wounds. It does not: CWound::Incarnation (Wound.cpp:97) subtracts the value from each wound size, it is
  not a fraction. Wounds larger than 1 stay. The script printed `clear=ok` because the call did not error, not
  because bleeding was zero. This is a flaw in the test's restore step, in witness modes only.
- **What the data shows in substance:** with protection off, all three sources damage the actor in both witnesses;
  with protection on, none of them changes health, radiation or bleeding. The protected run never needs the
  restore step, so the flaw does not touch it.
- **Not tuned, no retry.** A fix (clear bleeding until it reads zero, and make `clear=` report the value read back)
  would be a new pre-flight, registered first, if the Overseer wants the witnesses clean by the rule.

## 51. Restore fix after E2 (Overseer: no E3), script frozen; R3 joint crossing pre-flight (ITRZone arm x1), X side

- **Overseer ruling (2026-11-24):** E2's FAIL stands as recorded; what Caden's rule needs is shown in substance by
  the protected boot. No E3. The restore is fixed prospectively and recorded; if it ever runs in a scored boot
  again, its end check covers it.
- **Restore fix (test mode only, witness modes only):** the bleeding clear now sets a large value (1000), up to
  five times, until bleeding reads back as zero, and `clear=` is `ok` only if bleeding and radiation both read back
  <= 0.0001; otherwise it prints the values left. Script **473dec25...43537** (was 7f613c84).
  - Offline, stub with the engine's subtract rule and a wound of 3: restore gives clear=ok and end-check bleeding
    0; the mutation still shows bleeding 3; prot unchanged.
  - Offline, without the control file: play and named-crossing output equal the frozen 1d7143cf, 0 differing lines
    (wall-clock G lines apart).
  - Not shown in the engine: no boot has run the fixed restore.
- **Script 473dec25 is frozen for R3.** R3 never writes the control file; the runner removes a stale one
  (section 46a) and the checker fails on any DMG line.
- **R3 x1, ITR2 side (ITRZone amendment 107, commit 1c76503):** boot_t2.sh x1, levers hcheal=2 t2span=380 t2gap=0
  t2v=2 t2st=30 transit=1 txa=60 txn=esc_level_changer_to_garbage_2 txb=60 txm=gar_level_changer_to_escape_2
  txe=60. 60 s of Cordon track, into the changer, 60 s standing at the Garbage arrival, back by the paired
  changer, 60 s, end. No death, no freeze.
- **X env:** T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_RUN_S=480.
- **X files:** script 473dec25...43537; runner t2_xray.sh **6597a067...2a3d7** (changed since T3: it removes a
  stale control file); Stance14.exe b4ce9c77...46cbe; lib_engine_pids.sh cb183b7b...8ed6e.
- **Scored ONCE with tools/t3_x1_xray_check.py (c19740e8...2518c):**
  `<X out dir> out/t3pfD/run/boot0/meta_t2.csv l01_escape l02_garbage esc_level_changer_to_garbage_2
  gar_level_changer_to_escape_2`. PASS needs all nine:
  1. rc 0, no Lua error naming the script, no FATAL ERROR, no DMG line.
  2. Exactly two CROSS_REQ, in order, each through the named changer with pre-flight D's destination and vertices;
     no CROSS_NOCHANGER or CROSS_ERROR.
  3. Exactly two CROSS_RESUME, on Garbage then on Cordon, each after its request.
  4. For each crossing, the first applied play pose was written by ITR2 at most 90 s after the `cross` pose.
  5. P2 lines: at least 400 before the first request, at least 600 after each resume; no GUARD; at most 15 HOLD.
  6. The first P2 after each resume is within 5 m (x, z) of that changer's destination.
  7. The runner logged exactly two cross holds; restarts.csv has no row.
  8. On every P2: stance read back = pose stance, movement state readable, actor height = node height + 0.05 m
     (+-0.01).
  9. Exactly three VITALS lines (one per Lua state), each with lowest health >= 0.999, highest radiation and
     bleeding <= 0.0001, 0 unread; no REFILL line.
- **Witness:** pre-flight D's data fails on 5 only (598 P2 on Garbage, by design: D stood there half as long) and
  passes the other eight.
- **Not covered:** a death during a crossing (needs a checker exemption for the first pose after a resume; a later
  arm); spots with no AI node (ray fallback).
- **Guess, written before the boot:** X side PASS 65 %. Risks: stance or height read-back off on Garbage, where
  no stance step has run before (10 %); HOLD over 15 or a guard at an arrival with ITR2's real timing (10 %); the
  real mod's cross/play timing differs from my writer's and trips 2, 3 or 7 (10 %); other (5 %).
- **Stop rule:** a FAIL is reported, not tuned; at most one retry, registered first. ITRZone asks the Overseer
  for the box.
