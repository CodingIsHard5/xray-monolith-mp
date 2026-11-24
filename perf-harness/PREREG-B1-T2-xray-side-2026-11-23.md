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
