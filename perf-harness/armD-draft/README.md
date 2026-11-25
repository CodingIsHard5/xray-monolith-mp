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
| d0 | X-ONLY boot, no ITR2: the stand-in writer sends pose VERSION 2, is killed at 60 s and comes back 30 s later with its seq at 1 | keep | an old ITR2 mod against the new reader: v2 poses applied, LOST after 5 s, the returning writer taken only when its seq has passed the old one |
| d1 | during play on Cordon | keep | LOST after 5 s, X-Ray runs on alone for the whole gap, EPOCH + FOUND on the new session, poses applied again |
| d2 | during a crossing out, a few seconds after its `cross` pose | keep | X-Ray finishes the level change alone, LOST 120 s after its resume, the new session appears on Cordon, EPOCH + FOUND, recover crossing back |
| d3 | during play on Cordon | freeze | as d1, but the bridge stops X-Ray at LOST and continues it when a pose returns |

- itrzone's planning number: an ITR2 restart leaves me 3.5 to 4 minutes without a pose. Both limits (5 s, 120 s) are
  far below that, so the pose is LOST in every run and only the new session number brings it back.
- d3 is the risky one: X-Ray stopped for about 4 minutes. Longest stop so far: 25 s (pre-flight L), 20 s twice (T1).
  If ITR2's first poses after its restart say `away` + freeze (its save-and-reload leg), the bridge stops X-Ray again
  at once; the checker allows more than one STOP..CONT pair for that. I propose d3 last, on its own GO.
- Freeze during a crossing is not drafted (the Overseer's earlier rule: freeze at a level change gets its own run).

- **d0 (ordered by the Overseer, 2026-11-25: a mixed version, an old ITR2 mod with the new X-Ray, is a realistic
  state for Caden).** It runs first, before d1, as its own short X-only boot, and is also the first engine run of
  runner b31879be.
  - Files: `t3_pfD0.sh` (driver, from t3_pfL2.sh; case `d0`), `t2_lost_pose2.py` (the stand-in writer with
    `sess=none` = no session field), runner tools/t2_xray_v3.sh, script b6be10b4. Run length 200 s, one 20-minute slice.
  - Score: `python3 -I tools/t3_armD_xray_check.py out/t3pfD0 out/t3pfD/run/boot0/meta_t2.csv v2 keep` (eight checks
    and V).
  - Desk run with the REAL script in the stub (gap 60 to 90 s, 3 s pause, seq back at 1): LOST at 5,000 ms, no
    EPOCH, nothing applied for 87 s, FOUND at seq 1141 (the old one was 1140), 1,053 poses applied after.
  - What it will show, and it is by design: with a v2 writer a restart is NOT picked up at once. The pose stays lost
    until the new writer's seq passes the old one: (old seq) / 20 seconds, 57 s here, and about 10 minutes after a
    10-minute session. The session number of v3 is the cure. The runner (bridge) takes any new pose, so it reports
    the pose back after 30 s while the script still waits; under keep that has no effect.
  - Under freeze this mixed state would continue X-Ray at once while the script still applies nothing until the seq
    has passed. Not run; named here so nobody is surprised.
  - Checker tests: stub data passes checks 2 to 8 (V cannot pass on stub data); the real keep run and the real
    mut_sess run scored as v2 FAIL (a session was sent; EPOCH present or no FOUND).
  - Guess: PASS 70 %. Risks: first engine run of the runner; the v2 header path of this script has only run in the stub.

- **d0 becomes two boots (Overseer, 2026-11-25: a player must not wait out (old seq)/20 s; pick a fix, d0 scores it,
  the old behaviour stays as the control).** I picked option (a), the reader takes the reset:
  - Script draft `zz_p1_puppet_lost3.script` (from b6be10b4; three small edits, flag `-t2_v2reset`): while the pose is
    LOST, a pose WITHOUT a session number and with a seq BELOW the last one is taken as a restarted writer, logged
    `RESET,<t>,<game ms>,<old seq>,<new seq>` and counted with the epochs. Never when any pose of this run carried a
    session number, so a v3 writer is untouched.
  - Runner draft `t2_xray_v3.sh` here (from b31879be): installs the lost3 script; `T2_V2RESET` (default 1; 0 = the
    old behaviour). These two replace b6be10b4 and b31879be for ALL of arm D if registered.
  - Why (a) and not (b), a plain "update the other side" message: (a) makes the mixed install simply work, with no
    step for the player; (b) needs a place to show a message on both sides and still leaves the wait. Left open:
    a v2 writer that restarts and counts PAST the old seq within the lost limit (5 s) cannot happen (ITR2 needs
    minutes to restart).
  - Runs: `t3_pfD0.sh d0a <out>` = control, T2_V2RESET=0, scored `v2`; `t3_pfD0.sh d0b <out>` = reset on, scored
    `v2r` (one RESET after LOST, FOUND 25 to 45 s after the last old pose, last T2SUM epochs=1).
  - Desk, the real lost3 script in the stub: flag off = byte-identical log to b6be10b4 (FOUND after 87 s at seq 1141);
    flag on = RESET and FOUND 30 s after the last pose, at seq 1, 3,333 poses applied instead of 2,193; flag on with
    a v3 writer reusing its session number = still rejected (as mut_sess); with a new session = EPOCH as before;
    v2 writer with a gap but no restart = plain FOUND, no RESET.
  - Checker: stub data of each boot passes its own arm except V (no stance read-back in the stub) and FAILS the
    other arm on checks 4 and 8.
  - Not reviewed by CodeRabbit yet; I run it on the script and runner diffs before registration.
  - Guesses: d0a PASS 70 %, d0b PASS 65 %.

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
2. **A pose WITHOUT the session field read by this script in the engine: COVERED by d0 (added on the Overseer's
   order), as scored checks.** What follows was the first draft's reasoning:
   - (first draft) NOT COVERED by arm D. Arm D's writer is
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

- d0 70 % (above).
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

## Half-written files (Overseer's order, 2026-11-25, after the arm C question)

**Why rename was removed in T1.** It was not a choice of ours. The engine removes it from every script's Lua:
`disable_os_funcs` in src/xrServerEntities/script_storage.cpp (lines 318-335) sets `os.execute`, `os.rename`,
`os.remove`, `os.exit` and `io.popen` to nil. It is the engine's sandbox for all mods. So a script cannot write to a
temporary name and rename; the T1 prereg (section 2) says the same and chose "rewrite in place, header seq and
`end,<seq>` footer, the reader takes a file only when both match".

**What the reason allows.**
- Write-then-rename from the script: NOT possible without an engine change (give `os.rename` back to all mods, or
  export a new C++ function for it). Opening the sandbox for every mod is not something I propose for a test bridge.
- A reader-side whole-file check: possible, and it is the rule both sides agreed in T1. I propose this one.

**My reader (ITR2's pose file) already does it; witness with deliberately cut files.** The stub's `STUB_CUT` makes
every 10th pose a cut file that would do harm if any part were used: a higher seq, state `cross`, another level, a
changer line and a NEW session number (or, with `STUB_CUT=v2`, no session and seq 1), cut five ways: empty; half a
header; header only; header + pose + changer without the end line; whole but with another seq in the end line.
100 s of track, 2,000 writes, 200 of them cut:

| reader | writer | poses applied | TORN logged | CROSS_REQ | EPOCH | RESET |
|---|---|---|---|---|---|---|
| lost2 b6be10b4 | v3 | 1,800 | 160 | 0 | 0 | 0 |
| lost3 draft, fix on | v3 | 1,800 | 160 | 0 | 0 | 0 |
| lost3 draft, fix on | v2 | 1,800 | 0 (seq 1 is not newer) | 0 | 0 | 0 |
| frozen 473dec25 | v2 | 1,800 | 0 | 0 | 0 | 0 |
| lost3 draft, no cut files (control) | v3 | 2,000 | 0 | 0 | 0 | 0 |

No cut file is applied, starts a crossing, or counts as a new session, on any of the three scripts.
- Not shown by the stub: a cut v2 file arriving WHILE the pose is lost (the reset path). By reading: the RESET line
  and the counters are written only after the end-line check, like EPOCH.

**Where the gap is.** In ITR2's reader of my snapshot: amendment 119's finding 8 says a header without its footer
sets the level before the body is checked. The cure is on that side: use nothing of a snapshot, the level included,
before the end line matches the header seq. That is itrzone's amendment to write; I offer this witness and, if it
helps, a header field with the line count so a reader can also check the length.

## CodeRabbit on the reader and runner diffs (2026-11-25, after three rate-limited tries)

Four findings on the diff b6be10b4 -> lost3 and b31879be -> the draft runner:
1. Major, reader: the crossing marker wrote session 0 for a cross pose without a session field, so a run that had
   seen a session number could lose it over a level change and then meet the reset rule. FIXED: the marker now
   carries the last known session number. (Only a writer mixing both formats could reach it; ITR2 does not.)
2. Minor, reader: take the reset only for a pose with the v2 stance field. NOT CHANGED: a pose without it (v1) fails
   the stance read anyway and no v1 writer exists any more.
3. Major, bridge: a restarted writer with a lower seq must end the runner's LOST state and continue X-Ray. REFUTED by
   the code: the runner ends LOST on ANY new complete pose header and never compares the seq (runner lines 109-112).
4. Major, runner: harness/zz_p1_puppet_lost3.script does not exist where the runner installs it from. TRUE today and
   intended: the drafts are installed at registration; the runner's install is guarded and refuses to start without it.

After fix 1 the reader draft is 5bf69e5ce0ed7d775efe2ab9755826dca43152e093f6fbc5b9909416f12641cb (earlier draft
b048dbcf). The old-against-new witness on the five shapes was redone with it (witness_old_vs_new.txt): masked logs
identical, same hashes as before. The v2 runs repeat: fix off, found after 87 s; fix on, RESET and found after 30 s.
The cut-file witness above was run on b048dbcf and not repeated: the fix touches only the marker write at a crossing.

**Cut-file witness repeated on 5bf69e5c (Overseer's order, 2026-11-25).** Same result: v3 writer 1,800 applied, 160
TORN, no CROSS_REQ, EPOCH or RESET; v2 writer 1,800 applied, none of the three; no marker file written; control without
cut files 2,000 applied. The masked logs of both cut runs are identical to the b048dbcf runs.
