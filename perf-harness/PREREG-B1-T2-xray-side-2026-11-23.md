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
