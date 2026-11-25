# lost-draft: lost-pose handling (LOST, cross time limit, session number), DRAFT

Design: ../DESIGN-NOTE-pose-lost-2026-11-24.md. Never run in the engine. Installed under their OWN names (tools/t2_xray_lost.sh, tools/t2_lost_pose.py,
tools/t3_pfL.sh, tools/t3_pfL_check.py, harness/zz_p1_puppet_lost.script); the files R3 arm A uses are not touched.
Reviewed 2026-11-24 (CodeRabbit, 16 findings; see the design note's last section).

- `zz_p1_puppet.script`: copy of harness/zz_p1_puppet.script 473dec25 plus: flags `-t2_lost <ms>` / `-t2_lostx <ms>`
  (absent or 0 = off), STALE + LOST in one age function, ABSORB_ARM at LOST under freeze, pose v3 (7th field =
  session number) with EPOCH, FOUND, a second line in cross.txt (policy, session), the cross wait timed from
  CROSS_RESUME, and `lost=`, `epochs=` on the T2SUM line.
- `t2_xray.sh`: copy of tools/t2_xray.sh 6597a067 plus: T2_LOST_MS (5000) / T2_LOSTX_MS (120000), lost.csv, the
  freeze path also taken for LOST + freeze (STOP row state `lost`), the cross hold ends when lost, the pose's 7th
  field is dropped when the first line is read.
- `t2_lost_pose.py`: stand-in pose writer (pose v3) that the driver kills and restarts.
- `t3_pfL.sh <case> <out dir>`: one X-only boot per call: keep, freeze, cross, mut_off, mut_sess.
- `t3_pfL_check.py <out dir> <keep|freeze|cross>`: the checker.
- `lost_stub.lua`: the offline stub with a dead-writer gap, a short pause, a seq restart and session numbers.

Offline results (stub, 2026-11-24):
- Flags off: play and crossing output equal the frozen script except the two new fields on T2SUM and the second
  line of cross.txt.
- keep and freeze: STALE and RESUME on the 3 s pause without LOST; LOST at 5,000 ms; no pose applied until FOUND;
  EPOCH on the new session; under freeze the absorb is armed at the LOST line.
- After a level change with no pose: LOST at the cross limit after CROSS_RESUME.
- Limits off: no LOST. Same session with seq back at 1: no EPOCH, poses rejected.
- Checker on stub data: every lost-pose check passes for keep and freeze, mut_off and mut_sess FAIL. The stub does
  not model the stance read-back, so check V cannot pass offline.
- Not shown offline: the runner's part (wall clock, STOP/CONT, lost.csv), the driver, the engine.
- After the review: the same pose still on disk after a level change gives LOST 120,000 ms after CROSS_RESUME
  (135,000 ms with the marker's seq lowered, i.e. without the fix); the runner's pose reader returns the header
  for a complete record and nothing for a torn, empty or mismatched one; flags-off output still differs from the
  frozen script only in T2SUM's two new fields and the marker's second line (now `x,<policy>,<session>,<seq>`).
