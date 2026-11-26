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

## 7. Result
(none yet)
