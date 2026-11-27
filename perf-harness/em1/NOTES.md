# em1 X side: notes for EnginePerf (desk draft, 2026-11-27 14:00-14:30 EST, Opus sub-worker)

No engine boot, no git, no CodeRabbit, no network. Files written only under em1/. Everything else read only.

## Files
| file | what |
|---|---|
| em1_check.py | the X-side checker (emission checks 1-13 from kj4p + kill checks k2..k15 from kj2 for one monster) |
| em1_selftest.sh | selftest: one stub-made run, made fixtures, 44 checker mutants, real kj4p-r1 and kj2 folders, then the axr step test |
| em1_stub.lua | kj4/kill7h_stub.lua + one fixture command `lag <ms>` (pending kill); diff = 3 lines |
| t2_xray_em1.sh | X runner = kj4/t2_xray_kj4.sh + the axr_options restore step (exit 7) + the exit-code header line |
| em1_axr_step_test.sh | desk test of that step alone, on a scratch layout (good / missing / wrong bytes) |
| PREREG-em1-joint-emission-witness-DRAFT.md | the draft text (limits, checks, NV, margin table, claim class) |

## Decisions EnginePerf should confirm
1. **What "the pending state" spans.** The brief said "from the hit until the kill call". The rig code says otherwise
   (ITRZone main.lua STR.kcHealth: kcShort is called in the same pass as kcDeath, so the 0.005 short travels in the SAME
   pose as dead = 1; kill7 then applies the difference and calls o:kill in the same Lua call, KAPPLY and KDEAD at one X
   time). Between M's 0.3 hit (play 30) and play 50, M is at ~0.7, not near 0. So kp judges the window from the kill call
   (KDEAD online) to the KCONF: M alive 1, health in [0.0020, 0.0200], plain k line. itrzone's draft em1_check.py reads the
   pending path the same way ("X-Ray applied lines between the death and its confirmation"). If EnginePerf meant something
   else, kp must change.
2. **nvk7 is new** (not in the brief): if the engine puts the death into effect at once (KCONF 0 ms, as in kj2), there is
   no pending snapshot and kp cannot be judged; nvk7 fires and leaves only kp untested. Alternative: make "no pending
   snapshot" a plain FAIL of kp. I chose NO VERDICT because a kill that takes effect at once is not a fault of X-Ray.
3. **kd judges the kill call's PLAY time** (50 +/- 5 s) via kj4p's X->W P2 map and the runner's play0. The +/- 5 s mirrors
   itrzone's own +/- 5 s; the measured offset in kj2 is -1.04..-1.09 s (4.6x). The hit's play time is only reported (k2).
4. **Missing / empty / unreadable required files FAIL by name** (kj2's principle, as the brief asked), exit 1, no check
   computed. kj4p REFUSED (exit 2) instead. A score-once wrapper modelled on kj4p_score.sh would need to treat 1 as a score.
5. **hits_t2.csv may be empty** (kj4p's rule kept; kj2 had an empty one). stall.csv must NOT exist (check 1).
6. **Check 10 replaces kj2's check 12** (actor health = the first): 1.0000 in every snapshot implies it. k12 is dropped.
7. **Check 11 vs k14.** The non-kill zeros (torn, lost, epochs, ktabfail, eerr, paused, key order) stay in 11, which is
   always judged; every key (incl. those) is also in k14's table. So a kill-side NO VERDICT can never hide a torn pose or
   eerr. eerr is judged twice (11 and k14): mutant m11 fails both.
8. **The 10-of-12 rule for n != 12** is my generalisation: judged x 12 >= 10 x n. For n = 10 (the stub) 9 judged pass,
   8 fail. The brief's text ("at least 10 of 12 judged") is exact only for 12 vortices.
9. **The vortex gap uses the X clock** (snapshot header ms between the first v line's snapshot and the kept snapshot
   before it). Measured on kj2: X-clock gaps p99 136 ms, watcher-clock p99 138 ms: the same in practice.
10. **kp's band [0.0020, 0.0200]**: upper edge = FALL (0.02) and = 0.0056 + 1.5 s x 0.0096/s healing; lower edge 0.0020 is
    a choice (2.2x under the derived 0.0044). A puppet whose health sits ABOVE X-Ray's (it is updated down at once, up once
    a second) would leave M below 0.0044; I found no path for that in main.lua but did not prove there is none.
11. **k6 excludes every k-line id**, not only M (kj2 excluded S and M); only matters when nvk1 fires.
12. **Runner step order**: copy first, then hash the RESULT (as ordered). A wrong source therefore overwrites
    configs/axr_options.ltx with wrong bytes before the refusal. Hashing the source before the copy would leave the configs
    file untouched on a refusal; say if you want that instead. The step uses `log` (timestamped line into t2_xray.log and
    stdout), as the runner's other REFUSED lines do.
13. **No em1 driver.** As in kj2 (prereg sections 2 and 8), itrzone's boot script holds the box, starts ITR2 and then runs
    the pinned X runner; so the restore lives in the runner, and em1_run.sh was not written (coordinator's ruling). What
    itrzone's boot script must pass: the env of PREREG section 1 (T2_RUN_S 445, T2_WALL_S 1300, T2_OVER_S 120,
    T2_STALL_PLAY 99999, T2_SURGE_PLAY 70, T2_PSI_PLAY 310, the rest as kj4p) and an EXCLUSIVE claim whose text the runner's
    claim test accepts (`^HELD:.* EXCLUSIVE` or `B1-T2`). The runner's `cd "$(dirname "$0")/.."` assumes it is installed in
    tools/ (from em1/ its RUN would point at perf-harness/run: never run it from em1/).
14. **Disk/selftest**: each selftest run leaves ~31 MB under em1/selftest-runs/em1-st-*/ (the selftest deletes nothing;
    the CRLF full copy is most of it). Left now: em1-st-20261127-142134-628419 (31 MB, the PASS run), axr-20261127-141154-*
    (1 MB), adhoc-20261127-141100 (68 KB, the first kj2 symlink read). I removed my first, failed selftest run (12 MB).

## Guesses (not verified)
- That ITR2's rig picks a monster at all in em1 (kj2 picked flesh 23183 at the same spot; with the actor moving the pick
  may differ). No pick -> no k line -> nvk1.
- That the surge at play 70 does not interfere with M (dead at ~49; body may be removed: allowed by k11).
- That kill7's snapshot order is `a o* r? e (w|v)* ns k*` in the engine as in the script text (export_b1: em_snap after r,
  k lines after it); the stub agrees; no real engine snapshot with both e and k lines exists yet.
- That the play-end offset stays ~15 s in a joint run with emissions (kj2: 15.04 s without emissions).
- That RUN_S 445 leaves the psi storm's EEND + 2 s inside the run (latest ~421 vs end ~430).

## What was tested, and how
- `nice -n 19 bash em1/em1_selftest.sh` (2026-11-27 14:21:34, after the QUIET-REQUIRED check printed nothing; ~10 s):
  `EM1 SELFTEST: PASS`. In it: 1 stub-made run (real kill7 reader, stub with kill lag 250 ms, rig stand-in: M 301 hit
  0.300, lethal 0.995 = 0.3 + 0.7 - 0.005, KAPPLY after 0.0050, 2 pending snapshots, KCONF 250 ms; 10 vortices) -> PASS;
  47 made FAIL fixtures with exact failing sets (every check id at least once); 4 PASS fixtures at the new bounds
  (dy 0.45, distance 151.0, one vortex unjudged by a 300 ms gap with dy 0.9, a falling neighbour 60 m away); 10 NO
  VERDICT fixtures (every nve/nvk rule fires once; nvk1 by a stalker k line; nvk6 by M unlisted at the dead pose); one
  NV-does-not-hide fixture (nvk7 + check 10 -> FAIL); 3 unparsable; 8 missing + 3 empty (FAIL by name) + empty hits (PASS);
  CRLF copy (PASS); the checker writes nothing; 44 checker mutants (31 checks forced to pass, 10 rules never firing, 3
  bound mutants: V_DY 0.05, no gap rule, no 10-of-12 rule): 44 of 44 give a different outcome on their fixture.
- Real bytes, read in place: `out/kj4p-r1/run` -> NO VERDICT (nvk1: no k line), every emission check 1-13 PASS; check 9
  PASS with 12 of 12 vortices judged (gaps 100 ms), vortex 8 at -0.064 m (kj4p's FAIL) now inside +/-0.50; check 1 play
  end 449.3 >= 425. The kill checks fail exactly as "no k line at all" (k2 k4 k6 kp kd k7 k8 k11 k14), all inside nvk1.
- kj2's real X folder: in place -> FAIL, `MISSING emforce.csv`. Through symlinks + a header-only emforce.csv -> FAIL;
  emission checks all FAIL except 10 (actor unhurt) and 12 (no e lines in kill5). Kill checks it CAN exercise on real
  bytes, M = flesh 23183: k2 PASS (0.300, exact), k3 PASS (5 KAPPLY), k4 PASS (689 snapshots, +0.0277 healing), k6 PASS,
  k8 PASS, k11 PASS, k13 PASS (no stop allowance needed), k15 PASS (180 dead lines), nvk3's death-step exemption (950 ->
  951). It CANNOT exercise: kp (no pending: KCONF 0 ms, lethal after 0.0000 -> nvk7 fires), kd (M's kill at play 125.7,
  inside the 20 s stop), k7 (M's dead pose read inside the stop: 11839 ms), k14 (kdead_online 2, no eerr key in kill5,
  2 KCONF), nvk1 fires (stalker 19425 has k lines). Asserted per check in the selftest.
- `bash em1/em1_axr_step_test.sh` (also run by the selftest): 8/8 (marker once, order RUN < step < claim check < first
  boot, no rm in the step, good -> continues and the hash holds, missing source -> 7 + one REFUSED line, wrong bytes -> 7
  + one REFUSED line, bash -n of the runner, the real source file unchanged). Scratch layout under
  em1/selftest-runs/.../axr/; the real run/ folder only read (its axr_options_play.ltx hashes to 27d54b9f... today).

## Not tested / not done
- The runner as a whole (bash -n only, plus its one step in isolation); the engine; ITR2; a moving actor during an
  emission (moving-actor checks rest on made edits of the actor line); the pending path in the engine; bounds exactly at
  their edges beyond the four PASS fixtures; a stub run with a moving actor (the stub's actor stands).
- No CodeRabbit pass (not allowed in this task). No scorer (em1_score.sh) was written: open item.
- Predictions and pins left for EnginePerf (PREREG sections 6-8).
