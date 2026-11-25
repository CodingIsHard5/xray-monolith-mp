# Arm D, X side: DESK DRAFT (2026-11-25). NOT REGISTERED. Waits for itrzone's arm D draft.

Arm D = ITR2 is killed and started again while X-Ray keeps running. Pose v3 (a session number in the header).
Ordered by the Overseer as desk work while arm C waits. Nothing here has run on the box.

## Files (drafts until the arm is registered)

- Script harness/zz_p1_puppet_lost2.script b6be10b4e8ae57a4573e8445996feb292c0ca310c23b927916092e3d30de2943 (passed
  pre-flight L2 in the engine).
- Runner tools/t2_xray_v3.sh b31879beb9d6ca238f51beb5d648e60aadefc7cf0f9cd6e023112e0383cc4492. NEVER run in the
  engine: L2 ran its predecessor 5890b34b; this one adds the wall limit, the marker emptying and two review fixes.
- Checker `t3_armD_xray_check.py` (here; not installed in tools/ yet), Stance14.exe b4ce9c77, lib cb183b7b.
- Stub `armD_stub.lua` = the lost stub with a writer that can name another level and go on counting.

## Proposed runs (itrzone decides the schedule; these are the shapes my side can score)

| run | ITR2 is killed | policy | what it shows on my side |
|---|---|---|---|
| d1 | during play on Cordon | keep | LOST after 5 s, X-Ray runs on alone for the whole gap, EPOCH + FOUND on the new session, poses applied again |
| d2 | during a crossing out, a few seconds after its `cross` pose | keep | X-Ray finishes the level change alone, LOST 120 s after its resume, the new session appears on Cordon, EPOCH + FOUND, recover crossing back |
| d3 | during play on Cordon | freeze | as d1, but the bridge stops X-Ray at LOST and continues it when a pose returns |

- itrzone's planning number: an ITR2 restart leaves me 3.5 to 4 minutes without a pose. Both limits (5 s, 120 s) are
  far below that, so the pose is LOST in every run and only the new session number brings it back.
- d3 is the risky one: X-Ray stopped for about 4 minutes. Longest stop so far: 25 s (pre-flight L), 20 s twice (T1).
  If ITR2's first poses after its restart say `away` + freeze (its save-and-reload leg), the bridge stops X-Ray again
  at once; the checker allows more than one STOP..CONT pair for that. I propose d3 last, on its own GO.
- Freeze during a crossing is not drafted (the Overseer's earlier rule: freeze at a level change gets its own run).

## Env (draft; T2_RUN_S and T2_WALL_S are set from itrzone's schedule)

`T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_MAXRESTART=0
T2_LOST_MS=5000 T2_LOSTX_MS=120000 T2_OVER_S=120` plus:
- d1, d3 (no level change): the engine counts its run length once. Needed: warm-up 15 s + ITR2's first leg + stay +
  gap of up to 240 s + stay after. With two stays of 120 s: T2_RUN_S=700. Under freeze the stopped time is not engine
  time but IS wall time: T2_WALL_S = 700 + 400 + 240 = 1340.
- d2 (two level changes): T2_RUN_S=600 counts from each load; the Garbage stay is about 250 s (120 s to LOST, the
  rest of the gap, 3 s mismatch). T2_WALL_S=1660 as arm A2.

## Score commands (draft)

- d1: `python3 -I tools/t3_armD_xray_check.py <X out dir> out/t3pfD/run/boot0/meta_t2.csv play keep gap=150-330 spot=<x>,<z>`
- d2: `... cross keep gap=150-330 spot=<x>,<z>`
- d3: `... play freeze gap=150-330 spot=<x>,<z>`
- gap = seconds without a pose by the runner's wall clock; spot = where ITR2's player stands after its restart
  (if it is the save spot, -220.9, -145.0, as after a death). Both come from itrzone's draft.
- The conditions are in the checker's header: nine for each arm.

## The two engine-side gaps

1. **A writer returning after a lost crossing: COVERED by d2, as scored checks.** Condition 3 (LOST 120 to 122.5 s
   after the resume) and condition 4 (exactly one EPOCH and one FOUND after it, at least 20 level-mismatch lines
   before the recover request, nothing applied on the Garbage), then conditions 2 and 5 (the recover crossing and
   poses applied on Cordon).
   - Desk witness with the REAL script b6be10b4 in the stub: crossing at seq 100; the old writer writes to seq 170
     during the load and dies; LOST 120,000 ms after CROSS_RESUME; a new session (seq from 1) names l01_escape:
     EPOCH and FOUND on its first pose, 99 ZONE_MISMATCH lines, CROSS_REQ back by gar_to_esc on its `cross` pose,
     CROSS_RESUME on Cordon, 400 poses applied there.
2. **A pose WITHOUT the session field read by this script in the engine: NOT COVERED by arm D.** Arm D's writer is
   v3 from its first pose, before and after the restart. Offline only so far (the stub's `L_v3_plain` case).
   - It matters only if a v2 writer ever meets this script. If arm D passes, every later arm uses v3 on both sides
     and the frozen v2 script 473dec25 is retired, so the case would not occur.
   - If the Overseer wants it shown anyway: one X-only boot with the stand-in writer sending no session field
     (keep case: LOST, then a returning writer whose seq goes on above the old one). It needs one new key in the
     stand-in writer and its own registration.
   - Known and by design: a v2 writer that restarts with seq from 1 is NOT taken until its seq passes the old one
     (that is what the session number fixes; mut_sess showed the same for a reused session number).

## Checker tests (desk)

- play arm on REAL engine data of pre-flight L (gap=20-40): keep run as keep PASS; freeze run as freeze PASS;
  mut_sess and mut_off FAIL; keep scored as freeze and freeze scored as keep FAIL; keep with the default gap
  (60-420 s) FAILS on the gap, as it must (L's gap was 30 s).
- cross arm on a synthetic run spliced from real lines (L2's crossing and LOST, b1's recover and Cordon stay): PASS;
  seven mutants each FAIL: LOST at 135 s (L's real data) on 3; no EPOCH on 4 and 8; a pose applied on the Garbage on
  4; no recover crossing on 2; a mismatch line outside the window on 4; five mismatch lines on 4; no "lost end" on 6.
- Not testable at the desk: check V on stub data (the stub has no stance read-back); the runner b31879be itself.

## Predictions (X side PASS; to be restated at registration)

- d1 55 %. Risks: first engine run of runner b31879be; a gap eight times longer than any run so far; ITR2's first
  poses after its restart (state and level) are read from code, not seen.
- d2 40 %. As d1, plus the first recover out of a lost crossing in the engine, and ITR2's new session must send the
  recover crossing against snapshots it never saw change.
- d3 35 %. As d1, plus X-Ray stopped for about 4 minutes (Wine, audio and timers after a long SIGSTOP are untested)
  and a possible second stop on `away`.

## Section text for the prereg (to be completed from itrzone's draft, then registered)

> ## NN. R3 arm D (ITRZone amendment NNN, arms d1, d2, d3), X side, registered BEFORE any boot
> ITR2 is killed and started again while X-Ray runs; pose version 3. Files: script b6be10b4, runner b31879be,
> checker <hash>, Stance14.exe b4ce9c77, lib cb183b7b. Env: as above with T2_RUN_S / T2_WALL_S from the schedule.
> Score once per run with the commands above (gap and spot from itrzone's text). PASS = all conditions of the
> checker's arm. Covered: a writer returning after a lost crossing (d2). Not covered: a pose without the session
> field. Guesses and risks as above. One retry per run only for a run that dies before its data, registered first;
> a protected-title abort is judged by the timestamp rule of section 64a.
