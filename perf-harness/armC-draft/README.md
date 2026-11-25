# Arm C runner draft (2026-11-25): X-Ray killed inside a level load

`t2_xray.sh` here = tools/t2_xray_c.sh, made from the A2 runner (296563fd). Pose v2, frozen script 473dec25.
- A (always): the crossing marker cross.txt is emptied before every restart boot. A kill inside a level load leaves
  the marker; the restarted script would log a false CROSS_RESUME and append to the old log.
- B `T2_KILL_XS=<s>` (0 = off): boot 0 only, the planned kill comes that many wall seconds after the pose first says
  `cross` (T2_KILL_AT uses snapshot track time, which does not advance during a load).
- C `T2_RESTART_WAIT_S=<s>` (0): wait after a planned kill before the restart boot; the protected-title check is
  repeated after the wait.

Sandbox (fake tb_run and engine; RUN_S 60, pose turns cross at 10 s, marker written then):
- KILL_XS=5, RESTART_WAIT_S=20: kill 5 s after the cross pose; one restarts.csv row with planned=1; 20 s wait; boot 1
  logs "crossing marker emptied"; the marker has 0 bytes; boot 1 runs to its end; rc 0, unplanned restarts 0.
- levers unset: one boot, no kill, marker untouched (27 bytes), rc 0: as the A2 runner.
Not shown offline: the real engine killed inside a load, and what that leaves in the run folder.
