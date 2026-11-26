# PREREG B1-T2 arm D, runs d0a and d0b: an old (pose v2) writer against the new reader (registered 2026-11-26, before any boot)

X-Ray only, no ITR2. Ordered by the Overseer on 2026-11-25 (a mixed install, old ITR2 mod with new X-Ray, is a real
state for Caden; d0 scores the fix, the old behaviour is the control). Nothing below has run in the engine.
Background and desk results: `armD-draft/README.md`. No boot without the Overseer's GO.

## 1. Files (copies in `armD-d0/`)
| file | sha256 (first 8) |
|---|---|
| tools/t2_xray_v3.sh (runner; replaces b31879be, kept as tools/t2_xray_v3_b31879be.sh) | 71d070e5 |
| harness/zz_p1_puppet_lost3.script (reader; from b6be10b4, flag -t2_v2reset) | 5bf69e5c |
| tools/t2_lost_pose2.py (stand-in writer, sess=none = pose v2) | 75f0941b |
| tools/t3_pfD0.sh (driver, from t3_pfL2.sh d1c786cd; hash preflight of the four files above and Stance14) | c542a57c |
| tools/t3_armD_xray_check.py (checker) | b113d37d |
| run/bin/Stance14.exe | b4ce9c77 |
The runner and the reader have never run in the engine; the reader's predecessor b6be10b4 passed pre-flight L2.
Review: CodeRabbit 2026-11-26, two questions, no change (README, last section).

## 2. Runs: one X-only boot each, each its own EXCLUSIVE slice (20 min), run length 200 s
The stand-in writer sends pose version 2 (no session field), pauses 3 s at 20 s (must not be lost), is killed at 60 s,
and a new writer starts 30 s later with its seq at 1 again, as a restarted old ITR2 mod would. Policy keep.
- d0a, the control: `tools/t3_pfD0.sh d0a out/t3pfD0a` (T2_V2RESET=0). Then, once:
  `python3 -I tools/t3_armD_xray_check.py out/t3pfD0a out/t3pfD/run/boot0/meta_t2.csv v2 keep`
- d0b, the fix: `tools/t3_pfD0.sh d0b out/t3pfD0b` (T2_V2RESET=1). Then, once:
  `python3 -I tools/t3_armD_xray_check.py out/t3pfD0b out/t3pfD/run/boot0/meta_t2.csv v2r keep`
Order: d0a first. d0b only when d0a has ended with its data (PASS or FAIL); a d0a that dies before its data stops both.
Env inside the driver, as in pre-flight L2: T2_MAXRESTART=0 T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000
T2_STALL_AT=99999 T2_KILL_AT=99999 T2_RUN_S=200; the runner's own defaults for the lost limits (5 s / 120 s).

## 3. PASS (the checker's arms, conditions in its header)
- Both: runner rc 0, engine log present, no restart, no Lua error naming the script, no FATAL, no DMG line; one clean
  VITALS line; every writer row has session `none`; exactly one LOST at 5000..5600 ms; nothing applied while lost.
- d0a (`v2`): NO RESET line and no EPOCH; the returning writer is taken only when its seq has passed the old one
  (one FOUND, late); last T2SUM lost=1, epochs=0.
- d0b (`v2r`): exactly one RESET after LOST (new seq <= 200), then one FOUND 25..45 s after the last old pose; no
  EPOCH; last T2SUM lost=1, epochs=1.
A missing file or line is named by the checker and is not a pass.

## 4. Guesses
d0a PASS 70 %, d0b PASS 65 %, both 50 %. Risks: first engine run of this runner and reader; the v2 header path of
the reader has only run in the Lua stub; the checker's v2/v2r arms have only seen stub data (check V cannot pass
there, so V is untested for these arms).
Desk (stub with the real reader): flag off = log byte-identical to b6be10b4, FOUND after 87 s at seq 1141; flag on =
RESET and FOUND 30 s after the last pose at seq 1.

## 5. What these two boots cannot show
- Anything about a real ITR2: the writer is a stand-in. Arm D proper (d1, d2, d3) is itrzone's amendment 125.
- Freeze with a v2 writer (the runner continues X-Ray on the new pose, then the reader resets): not run.
- A session longer than 60 s: in d0a the wait is (old seq)/20 s, about 57 s here and about 10 minutes after a
  10-minute session. d0a shows the mechanism, not the long wait.

## 6. Retry rule
One retry per run, only for a run that dies before its data, registered here first. A protected-title abort is judged
by the timestamp rule of section 64a of the joint text.

## 7. Result
(none yet)
