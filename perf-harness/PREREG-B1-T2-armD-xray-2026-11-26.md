# PREREG B1-T2 arm D, runs d1 and d2, X side (registered 2026-11-26, before any boot)

ITR2 is stopped hard and started again while X-Ray runs on. First runs of my pose-lost script and v3 runner against a
REAL ITR2. Joint with itrzone's Amendment 125 (their PREREG.md, master 7a3c4df). d3 (freeze) is not part of this.

## 1. Joint text and pins
- itrzone's registered text, copied byte for byte: `armD-xray/itrzone-amendment-125.txt`, sha256
  0073468183d4ac2a73517d81f6a2550c7f998e64a5e3cfac6a236d37290c13df (86 lines, cut from the heading "## Amendment 125
  (REGISTERED 2026-11-26)" to the next "## " heading). I read it against my values: the four hashes, the env of both
  arms, the display rule, the stop times (120 s of play in d1; 4 s after the cross start in d2), session 2 at once
  with t2end=30, and "X-Ray ends first" all MATCH what I sent.
- My files, as in PREREG-B1-T2-armD-d0 section 1 (both d0 boots passed with them): runner tools/t2_xray_v3.sh
  71d070e5, script harness/zz_p1_puppet_lost3.script 5bf69e5c, checker tools/t3_armD_xray_check.py b113d37d,
  Stance14.exe b4ce9c77.
- Env (set by itrzone's boot script, read there today, lines 55 and 56): T2_EXE=Stance14.exe T2_FREEZE=sigstop
  T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_MAXRESTART=0 T2_LOST_MS=5000 T2_LOSTX_MS=120000 T2_OVER_S=120
  BENCH_DISPLAY=:141; d1 T2_RUN_S=700 T2_WALL_S=1100; d2 T2_RUN_S=450 T2_WALL_S=1400. T2_V2RESET at its default 1.

## 2. Who runs what
itrzone's boot script starts my runner and holds the box; I start nothing. After each run itrzone sends me the X out
directory. I score once per run:
- d1: `python3 -I tools/t3_armD_xray_check.py <X out dir> out/t3pfD/run/boot0/meta_t2.csv play keep gap=150-330 spot=-220.99,-144.99`
- d2: the same with `cross` in place of `play`.
The spot is the first row of itrzone's track (out/p1/cordon_full_track.csv: x -220.99, z -144.99), where their text
predicts session 2's first pose (0 to 0.3 m in d1, 0 to 0.5 m in d2; my bound is 5 m).
gap=150-330 is their predicted bound for the time without a pose.

## 3. PASS = every condition of the checker's arm (its header), nine each
- d1 (`play`): no crash, restart, Lua error or damage line; one LOST 5.0 to 5.6 s after the last pose; nothing applied
  while lost; one EPOCH and then one FOUND on the new session; the gap inside 150 to 330 s; at least 400 poses
  applied before and 600 after; no stop of X-Ray; the first pose applied after FOUND within 5 m of the spot.
- d2 (`cross`): X-Ray finishes the level change alone; one LOST with age 120.0 to 122.5 s, at most 137.5 s after the
  resume; one EPOCH and one FOUND after it; at least 20 level-mismatch lines and nothing applied on the Garbage; the
  recover crossing back by a Garbage-to-Cordon changer; poses applied again on Cordon, the first within 5 m of the spot.
- Stop gate (their text): d2 only after a d1 whose infrastructure checks pass. On my side those are check 1 (runner
  rc 0, engine log present, no restart, no Lua error, no FATAL) and the script having loaded (a T2SUM line exists).

## 4. Guesses (X side)
d1 PASS 60 %. d2 PASS 40 %. Risks: a gap about seven times longer than d0's; ITR2's first poses after its restart
(state, level, position) are read from their code and stub, never seen by my script; in d2 the first recover out of a
lost crossing in the engine, with a new session that sends the crossing against snapshots it never saw change; the
spot check has never run on real data.

## 5. What these runs cannot show
- Freeze policy (d3). An ITR2 that comes back later than 330 s or sooner than 150 s. A player who actually plays:
  the rig walks a track.
- Anything about damage or kills: the kill channel is a separate, later item.

## 6. Retry rule
As itrzone's text and the joint text's section 64a: one retry per run only for a run that dies before its data,
registered first; nothing is re-scored.

## 6.1 Note 1 (2026-11-26, registered before the retry): d1's first boot gave no data on the restart; one retry
- itrzone's report: session 1 ran and was stopped as planned; session 2 never booted (a fault in their boot script:
  it launched session 2 while the arm lock was still held). My runner ran alone to its end, rc 0, and logged LOST
  5.2 s after the last pose. X out dir: ITRZone out/runs/20261126-070213-t2d1-xray.
- I do NOT score that folder as d1: with no second session the checker would fail on EPOCH, FOUND and the gap for a
  reason that says nothing about my side. It is kept, unscored, as "run died before its data".
- itrzone's text gained "Amendment 125 note 1" (boot script only: wait for the arm lock before session 2). New text,
  copied byte for byte: `armD-xray/itrzone-amendment-125-note1.txt`, sha256
  48eac5dd3b27162f413876caa8feb4a4f4fcc966147243365bbcf6270d4fc375 (101 lines, 9582 bytes; their master 37adcd3).
  The diff against the first copy is fifteen added lines at the end, nothing else. My files, env, checks, score
  commands and guesses are unchanged.
- This is the one retry of d1 under section 6. It needs the Overseer's word (asked by itrzone).

## 6.2 Note 2 (2026-11-26, registered before d2): the gap rule, joint with itrzone's Amendment 133
Ordered by the Overseer after d1 (section 7.1, which stands as FAIL and is not re-scored).
- itrzone's text, copied byte for byte: `armD-xray/itrzone-amendment-133.txt`, sha256
  94816bd93ea980b2af38892b2a50b1c19d9cf22639c05a19bbe1c6951294631b (61 lines, 5498 bytes; their master b1db010).
  Their Amendment 125 section is unchanged (48eac5dd). Every number in it that is mine is as I gave it.
- The gap is counted as before (my runner's lost limit plus its "lost end" figure).
- Floor = the larger of two meanings. (a) The pose was really lost: d1 10 s (my 5 s limit plus the runner's poll);
  d2 150 s (my cross-lost limit fires 120 s after the last pose my script took, or after my arrival on the new level,
  so at most 129 s after ITR2's last pose; plus 20 s). (b) ITR2 really booted again: itrzone's bound, 100 s (no boot
  faster than 115 s in 36 of theirs), 145 s in d2 with the hold.
- Ceiling 330 s in both, as what the run leaves room for: d1, 220 s remain of my 700 s run for my 600 poses and
  itrzone's session-2 minimums; d2, about 115 s remain of the 450 s Garbage stay for 20 mismatch lines, their 3 s and
  the recover crossing.
- d2 only: itrzone's boot script launches session 2 no sooner than 60 s after the stop, so that my LOST fires before
  their first pose. Without it the return would land within seconds of my limit, on either side; my script takes a
  new session at once, with no LOST, and the run would show another case than the one registered.
- My score commands from here on: d1 `gap=100-330`, d2 `gap=150-330`; spot, files, env and every other condition
  unchanged. The d1 retry's gap (129.8 s) would pass the new d1 rule; it is not scored again.
- Guess for d2, X side: PASS 40 %, unchanged.

## 7. Result
### 7.1 d1, the retry (2026-11-26, X out dir ITRZone out/runs/20261126-083206-t2d1-xray; scored once, checker b113d37d): FAIL, 9 of 10 conditions
The one failing condition is the registered bound on the time without a pose, inside check 4:
| check | result |
|---|---|
| 1 no crash, restart, Lua error, FATAL, damage line; runner rc 0 | PASS |
| V health and stance read-back | PASS (11082 poses applied, 0 differ) |
| 2 one LOST 5.0 to 5.6 s after the last pose | PASS (5004 ms) |
| 3 nothing applied while lost | PASS (0) |
| 4 one EPOCH, then one FOUND, gap inside 150 to 330 s | **FAIL: gap 129.8 s.** EPOCH and FOUND are there: one each, old session 1795700082172, new 1795700340489, taken on the new session's seq 1 |
| 5 poses applied before and after | PASS (2316 before, 8766 after; no guard) |
| 6 runner's own record | PASS |
| 7 X-Ray never stopped | PASS |
| 8 last summary lost=1, epochs=1 | PASS |
| 9 first pose after the return within 5 m of the spot | PASS (0.0 m) |
What it shows: against a real ITR2 that was stopped hard and came back as a new session, X-Ray lost the pose after
5.0 s, ran on alone for 130 s applying nothing, took the new session on its first pose, and followed it again from
the track's start for the rest of the run. No crash.
Why it is a FAIL all the same: the bound 150 to 330 s came from itrzone's prediction of how long ITR2 needs to come
back (200 s); it came back in 130 s. The bound was registered, the run is scored against it, and I do not move it
after the fact. itrzone's side fails the same single condition (their D3, 131.9 s).
My guess was PASS 60 %: wrong as scored. The risks I named (the long gap, a real ITR2's first poses, the spot check
on real data) did not bite; the one that did was a bound I took over from itrzone's text without asking what a
too-fast return should mean. A fast return is the better outcome for a player, not the worse.
Stop gate: my infrastructure checks (check 1; the script loaded, T2SUM lines present) PASS. d2 does not run; the
Overseer decides what follows (itrzone's stop gate names their D3 as an infrastructure check).
Output: `armD-xray/d1_retry_xscore.txt`.
