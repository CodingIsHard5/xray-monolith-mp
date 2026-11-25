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

## Witness for the marker fault (ordered by the Overseer, 2026-11-25)

Same sandbox, a planned kill by track time at 12 s (the old runner's only kill lever), the script's marker written at
10 s as before a level change, then the restart boot. The marker as boot 1 finds it is then given to the REAL frozen
script 473dec25 in the offline stub, on l01_escape, with one old line already in the log file.
| runner | marker at boot 1 | frozen script on Cordon |
|---|---|---|
| A2 runner 296563fd | 37 bytes (left behind) | 1 false line `CROSS_RESUME,1,l01_escape,l01_escape,l02_garbage,...`; the old log line is kept (append) |
| arm C runner 14f436d0 | 0 bytes; log says "crossing marker emptied" | no CROSS_RESUME; the log is rewritten |

## After the CodeRabbit review (2026-11-25; 7 findings on the two runner drafts)

Fixed in this runner: the script install is checked (refuse on failure); a cancelled runner continues an engine it
stopped (freeze or stall); a freeze is recorded only after the STOP succeeded; an empty snapshot read counts as "no
new seq" for the 30 s rule; the track-time kill (T2_KILL_AT) is off when T2_KILL_XS is set.
Left as it is, with the reason: the over-run limit can fire during a level load that is still going on. That is the
intended strict deadline: T2_OVER_S is 120 s in every registered run and a load takes about 9 s.
Sandbox after the fixes (scratchpad/wallR): schedule fits (rc 0), stall killed by the 30 s rule, kill 5 s after the
cross pose with wait and marker emptied (rc 0), levers off as before, and with T2_KILL_AT=5 AND T2_KILL_XS=5 the kill
comes from the cross timer, not from track time.
`t3_armC_xray_check.py` (tools/t3_armC_xray_check.py): the X-side checker, nine conditions. Tested on a synthetic good
run made from b1's real data (PASS) and seven mutants, each failing the intended check: load finished before the
kill (2), boot 1 appended to boot 0's log (4), a false CROSS_RESUME in boot 1 (4), kill 9 s after the pose (3), no
"marker emptied" line (3), an unplanned restart (1), too short a stay (6).
