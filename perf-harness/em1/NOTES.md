# em1 X side: notes for EnginePerf (desk draft, 2026-11-27, Opus sub-worker; round 2 14:29-14:45 EST)

No engine boot, no git, no CodeRabbit, no network. Files written only under em1/. Everything else read only.

## Files
| file | what |
|---|---|
| em1_check.py | the X-side checker (emission checks 1-13 from kj4p + kill checks ka k2..k15 for one monster, split death) |
| em1_selftest.sh | selftest: two stub-made runs (kill lag 250 / 0), made fixtures, 45 checker mutants, real kj4p-r1 and kj2 folders, then the axr step test |
| em1_stub.lua | kj4/kill7h_stub.lua + one fixture command `lag <ms>`; diff = 3 lines (unchanged in round 2) |
| em1_score.sh | score once = kj4p_score.sh with em1 names; CHK_SHA = the round-2 checker |
| t2_xray_em1.sh | X runner = kj4/t2_xray_kj4.sh + the axr_options restore step (exit 7), placed right after the claim check |
| em1_axr_step_test.sh | desk test of that step alone, on a scratch layout (good / missing / wrong bytes) + its place in the runner |
| PREREG-em1-joint-emission-witness-DRAFT.md | the draft text (limits, checks, NV, margin table, claim class) |

## Round 2 (2026-11-27): the split death and the review fixes
- **Design (plan-facts/em1-pending-design-2026-11-27.md, Overseer ruling):** em1 is an "alive near zero, held, then killed" witness.
  kcjh=2: play 50 a dead = 0 pose with total 0.995 (M held alive at kill7:476's 0.0100 floor), play 52 the dead = 1 pose
  with total >= the hold's (itrzone wire values, blob 50cbbd22: hit 0.300, hold 0.995 at play 50.00..50.30, death
  1.005 dead 1 at hold + 2.00 +/- 0.30 s; X-Ray: death KAPPLY d 0.010, after 0.0000, no floor, judged by ka and kd; the
  stub's good run uses exactly these totals, with the stub creature's own section). The pending path is NOT forced: proven offline (kill7h stub tests) + the guard (KNOCONF at
  2000 ms); seen live only if the engine defers. kp and nvk7 are gone.
- **kh** (new): one dead = 0 total after the hit and one KAPPLY for it, play 50 +/- 5 s; d = pose difference, after =
  max(before - d, 0.0100), each +/- 0.0002; then EVERY kept snapshot strictly after the hold KAPPLY and strictly before the
  kill call (X) listing M: alive 1, health 0.0098..0.0300, plain k line; at least 5 of them. Choice: "at least 5" read as
  "every one good AND >= 5" (a bad one fails, not only a short count). Strict edges: a snapshot at the KAPPLY's own X may
  precede the apply in the same frame.
- **kd**: one KDEAD (online) for M at play 52 +/- 5 s, after the hold KAPPLY, n >= first dead pose; dead pose total >= hold
  total - 0.0002; total above the hold -> exactly one KAPPLY in the kill call with d = difference, after = max(before - d,
  0); total equal -> none.
- **kc** (new): exactly one KCONF for M, 0..2000 ms after the kill call (X and its ms field), no KNOCONF, first snapshot
  listing M at or after KCONF alive 0. Pending snapshots reported only.
- **k4** window: hit KAPPLY -> kill call; KAPPLY other than hit and hold FAIL; the fall across the hold KAPPLY is exempt
  (only when the hold KAPPLY is the sole K line in between).
- **B1 -> ka** (new, ALWAYS judged): every KAPPLY of any id: applied = total of the kept pose at or before its pose seq;
  d = total - the id's previous applied total (cap 1, kill7's clamp); after = max(before - d, 0), floored at
  min(before, 0.01) for a dead = 0 pose. It uses kill7's exact floor; kh uses the Overseer's max(before - d, 0.0100)
  (equal while before >= 0.01).
- **B2**: nvk2's "dead before its step" clause does not fire when M's first alive-0 snapshot follows a KAPPLY of mine with
  after <= 0 or a KDEAD online; k2 is removed from every untested set once M has a KAPPLY (checker prints
  `verdict: M has a KAPPLY: k2 judged whatever fired`). Fixture mk2_kill -> FAIL (k2, ka), nvk2 clear.
- **B3 runner**: restore block moved after the claim check (runner line 56), lines 57-66, before run_noguard (line 67/68).
  Before the claim check the runner writes only `mkdir -p "$O"` and log lines into $O (the out folder, kj4 unchanged).
  The step test asserts RUN set < claim check < BEGIN..END < first later write to run/ or shm < first boot.
- **B4 nve5 -> dropped** (coordinator 2026-11-27): it could never decide a verdict; nve5_cut (no v, no EEND psi) is a
  plain FAIL (6p, 9). A storm that ended with no v line FAILS 9.
- **Runner**: the source is now hashed against 27d54b9f before the copy (refusal, exit 7, leaves configs untouched); the
  after-copy hash stays. Step test adds: wrong source -> 7 AND destination byte-identical to before (9/9).

## Decisions kept from round 1 (confirm)
1. kd/kh judge PLAY time via kj4p's X->W P2 map and the runner's play0 (+/- 5 s mirrors itrzone; kj2 offset -1.04..-1.09).
2. Missing/empty/unreadable required file: FAIL by name, exit 1 (kj4p refused). em1_score.sh records 1 as a score.
3. hits_t2.csv may be empty; stall.csv must not exist. Check 10 replaces kj2's 12. Non-kill zeros stay in 11 (always).
4. 10-of-12 vortex rule for n != 12: judged x 12 >= 10 x n (my generalisation). Vortex gap on the X clock.
5. k6 excludes every k-line id. No em1 driver: itrzone's boot script runs the pinned runner (installed in tools/; never run it from em1/).

## Guesses (not verified)
- ITR2's rig picks a monster in em1 (kj2 picked flesh 23183 there). The hold band holds for em1's monster (rests on kj2's
  flesh, one run: max 2 s rise 0.0009). The engine puts set_health_ex(0.0100) into the next snapshot's health as written.
- Snapshot order `a o* r? e (w|v)* ns k*` in the engine; the play-end offset ~15 s with emissions; psi EEND + 2 s < ~430.
- Stub: on o:kill health goes negative (fall 1.005 from 0.005): the death-step exemption needs alive 0 and h <= 0.

## What was tested (round 2)
- QUIET-REQUIRED grep printed nothing at 14:35, 14:36, 14:37, 14:40, 14:42; `nice -n 19 bash em1/em1_selftest.sh`, final run (wire values 1.005)
  14:42:09-14:42:54: `EM1 SELFTEST: PASS`, 101 ok lines, 0 BAD; 45 of 45 checker mutants caught (46 runs). Two earlier
  runs FAILED on my own fixture expectations (mk3 now also fails ka; mkh_hi made an nvk3 fall; mkd_nokap no k14; mkc_alive
  breaks the death-step exemption -> replaced by mkc_early; khits 3 not 2; stub lag 0 gives KCONF 50 ms, so a made
  pkc_zero fixture proves 0 ms PASSES). Those run folders were deleted by literal path.
- Stub `good`: M 301 hit 0.300 -> 0.7000; hold d 0.695 -> 0.0100, 20 hold snapshots; death d 0.010 -> 0.0000 (total 1.005); KCONF 250 ms,
  2 pending snapshots (reported). `good_lag0` PASS (KCONF 50 ms, 0 pending). Made fixtures for every kh/kd/kc clause,
  wrong hold d / wrong death d with no snapshot between -> FAIL (ka + kh / ka + kd), nvk2_heldkill (world kills M in the
  hold -> NO VERDICT, kh), nve5_cut, m9_novortex (storm ended, no v -> FAIL 9).
- Real: kj4p-r1 -> NO VERDICT (nvk1, no k line), every emission check PASS. kj2 links -> FAIL (emission, no e lines) and
  ka, k2, k3, k6, kc (KCONF 0 ms), k8, k11, k13, k15 PASS; k4, kh, kd (no hold in kj2), k7 (stop), k14 FAIL; nvk1 fires.
- em1_score.sh on kj4p-r1 into em1/selftest-runs/score-r2/: rc 3, second call REFUSED rc 2.
- Left on disk: em1/selftest-runs/em1-st-20261127-144405-1013554 (50 MB; final, 14:44:05-14:44:49, 44/44 mutants, axr 9/9), adhoc-20261127-141100 (68 KB).

## Not tested / not done
- The runner as a whole (bash -n + the step); the engine; ITR2; the hold, a moving actor during an emission, or the
  pending path in the engine; a stub run with a moving actor. No CodeRabbit (not allowed). Predictions and pins: EnginePerf.

## Round 4 (EnginePerf, 2026-11-27 15:20 EST)
- em1_check.py: freeze.csv and lost.csv are required files; check 1 fails by name on any data row or a wrong header
  (FL_HEAD, FL_ROWS_MAX). Agreed with itrzone: a data row there = my check 1 FAIL = joint FAIL.
- em1_selftest.sh: stub run writes both files with the runner's header; cases m1_freeze_row, m1_lost_row,
  m1_freeze_head, m1_lost_head, miss_freeze.csv, miss_lost.csv; mutant b_flrows. Real folders (kj4p r1, kj2) have both
  files header-only.
- Self-test 15:20:33-15:21:28: PASS, 45 of 45 checker mutants caught, axr step test 9/9
  (selftest-runs/em1-st-20261127-152033-2128855). em1_score.sh with the new CHK_SHA on kj4p-r1: rc 3, second call refused.
- Pins added: harness/zz_eperf_dump.script, out/user.ltx.orig. Runner, stub, axr step test unchanged.
- Not reviewed independently (CodeRabbit down); the change is one clause and two table entries.
