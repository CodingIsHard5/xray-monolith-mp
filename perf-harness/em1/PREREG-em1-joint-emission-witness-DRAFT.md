# em1: joint emission witness with one monster kill (alive near zero, held, then killed), X-RAY SIDE (REGISTERED 2026-11-27 15:27 EST; no run before the Overseer's GO)

Drafted at the desk by an Opus sub-worker for EnginePerf: no engine, no Wine, no display, no claim, no git, no review pass.
Everything below about the engine comes from kj2 (joint, real), kj3p r1 and kj4p r1 (X-only, real) and the desk stub.

**claim class: EXCLUSIVE.** The X verdict depends on wall/engine timing (check 2: runner append +2.5 s and EFORCE delay
-0.25..+2.5 s; 6: EEND X vs t 0.5 s; 9: the 250 ms gap rule; kh/kd: the hold at play 50 and the kill call at play 52, +/- 5 s each; k7/k13: 2.0 s chains).
Several of those margins are under 5x (2.6x, 3.5x, 3.6x, 4.6x, 4.7x in section 5; round 2 adds the hold's 3.6x and 2.0x, no change of class), so the correctness-with-timing rule keeps it
EXCLUSIVE. itrzone's own em1 checks use only the play clock (+/- 5 s, |drift| <= 3 s); that alone would not decide it, but
the joint boot takes the stricter side's class. Also: the X runner refuses to start unless an EXCLUSIVE (or B1-T2) claim is
held (t2_xray_em1.sh, line 56, the first action after the log setup), so a shared claim cannot run this boot at all.

## 0. Limits first
- One joint boot: ITR2 (itrzone) drives the actor, X-Ray forces the events. God mode stays on: "actor unhurt" (check 10)
  is reported, never evidence of exposure (kj3p section 0 holds unchanged).
- **em1 is an "alive near zero, held, then killed" witness, NOT a pending-path witness.** itrzone's split death (kcjh=2) forces M to be
  held alive at kill7's 0.0100 floor (kill7:476) with a plain k line for 2 s before the kill call: this shows neither side
  closes a death early on a very low health. It does NOT force the pending path (kill call made, creature not yet reading
  dead). That path stays proven offline (kill7h stub tests) plus by the guard (KNOCONF at 2000 ms), and is seen live only if
  the engine happens to defer (kj2: KCONF 0 ms twice; kj1: 101 ms once). KCONF is judged 0..2000 ms, 0 ms PASSES; pending
  snapshots are reported only. (Design: plan-facts/em1-pending-design-2026-11-27.md, Overseer ruling 2026-11-27.)
- The hold health band rests on ONE monster (kj2's flesh 23183) in ONE run; em1's monster may be another kind.
- **The vortex and wave checks have never seen a moving actor.** The height and distance bounds of check 9 are derived from
  one moving run WITHOUT any emission (kj2, t2span=4) and two standing runs WITH emissions (kj3p, kj4p).
- The kill checks have read real bytes from ONE joint run (kj2, kill5, with a stalker and a 20 s stop); the em1 shape (one
  monster, no stop, the split death) has met only stub-made and hand-made fixtures.
- The desk stub's kill lag (250 ms) is a model of kj1's one observation, not of the engine source.
- The X runner `t2_xray_em1.sh` is kj4's runner plus one step right after the claim check (axr_options restore); that step has had a desk test
  of its own lines only (3 cases); the whole runner has had `bash -n` only.

## 1. What it is
One joint boot on Cordon (save cordon1, Stance14.exe, display :141). ITR2 drives X-Ray's actor along the first metres of
the Cordon track (t2span=4: ~10 m along x at 2.5 m/s, back and forth, y changing up to ~0.5 m per leg). X-Ray's runner
forces a surge at play second 70 and a psi storm at play 310 (as kj4p). itrzone's rig deals ONE monster kill, split in
two poses (levers `kcj=30 kcjo=1 kcjd=20 kcjp=0.005 kcjh=2`): the nearest monster M with snapshot health >= 0.9 is hit by
0.3 of full at their play 30; at their play 50 a pose carries M's total = full - kcjp (0.995) with dead = 0: kill7 applies
d = 0.695 with the non-dead floor (kill7:476), so M is HELD ALIVE at max(before - d, 0.0100) with a plain k line; at their
play 52 (hold + 2.00 +/- 0.30 s) the death pose carries dead = 1 and a total >= the hold's (itrzone's wire value 1.005 =
0.995 + the puppet's last 0.01; only >= the hold's is judged): KAPPLY d 0.010, after = max(before - 0.010, 0), no floor
(judged) + o:kill in the same Lua call (KDEAD online), then KCONF when a snapshot reads M dead (0 ms allowed). ITR2 stops
at its play 440 (t2stop); X-Ray ends earlier (play ~430). No stop (T2_STALL_PLAY=99999).
Questions: the kj3p F1-F6 for a moving actor, plus K1: is M held alive near zero with a plain k line until the kill call,
then dead in X-Ray within 2 s of it, with nothing applied twice? (Not asked: whether the engine defers the kill.)

**Who launches X-Ray.** As in kj2 (its section 2): itrzone's boot script holds the box, starts ITR2, then runs EnginePerf's
pinned X runner. So there is no EnginePerf driver for em1. The X runner is `em1/t2_xray_em1.sh` (to be installed and pinned
by EnginePerf as `tools/t2_xray_em1.sh`): an exact copy of `tools/t2_xray_kj4.sh` (installs kill7, unchanged pin 0d5e5927)
with ONE new step right after the claim check (before any other write to run/ or the shm folder, before the first boot): copy `run/appdata/axr_options_play.ltx` over `run/gamedata/configs/axr_options.ltx`, check sha256
= 27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29, else print `REFUSED: axr_options restore: <reason>` and
exit 7 (new code). The SOURCE is hashed before the copy (a refusal leaves the configs file untouched), the
result again after it. Runner env, to be set by itrzone's boot script (kj4p's env with the em1 run length):
`T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_MAXRESTART=0 T2_LOST_MS=5000
T2_LOSTX_MS=120000 T2_OVER_S=120 T2_STALL_PLAY=99999 T2_RUN_S=445 T2_WALL_S=1300 T2_SURGE_PLAY=70 T2_PSI_PLAY=310
BENCH_DISPLAY=:141` (option A, itrzone's choice 2026-11-27).

| play s (runner clock: header utc of the first play pose it reads) | what | expected in X-Ray |
|---|---|---|
| ~29 (their 30) | M hit 0.300 | one KAPPLY d 0.300, after = before - 0.300 |
| ~49 (their 50.00..50.30) | the hold: `k,<id>,flesh,0.995,0` | one KAPPLY d = 0.695 (pose difference), after = max(before - d, 0.0100); M alive at 0.0100..0.0149 with a plain k line, ~18 snapshots |
| ~51 (their 52) | the death pose: dead 1, total >= 0.995 (wire 1.005) | KAPPLY d 0.010, after = max(before - 0.010, 0) = 0.0000 (judged); KDEAD online in the same call; KCONF 0..2000 ms later, then M alive 0; dead line; KWHO killer 0 |
| 70 | surge forced | as kj4p (EFORCE, ESTART, phases 1..12(13), waves at t 120 / 168, EEND ~221.7) |
| ~292 | surge ends | |
| 310 | psi forced | as kj4p (phases 1..5(6), vortices from t 33, EEND ~102.9) |
| ~414 | psi ends | |
| ~430 | X-Ray's run ends (445 engine s from the level load - ~15 s) | SUMMARY, T2SUM (eerr last), VITALS |
| 440 | ITR2 stops (t2stop) | not seen by X-Ray |

## 2. Checks (em1/em1_check.py). PASS = all of them.
Emission checks = kj4p's checks 1-13 with the changes marked **em1**; kill checks = kj2's restricted to M (prefix k).
| id | outcome judged | bound |
|---|---|---|
| 1 | runner rc 0, no restart row, one boot, no FATAL, no bridge Lua error (three log forms), every snapshot on l01_escape with one run id, **em1: no stall.csv (no stop)**, **freeze.csv and lost.csv each hold their header line and no data row (round 4; any row fails, named)**, last kept snapshot at play >= **425** (kj4p's "no kept pose with a k line" removed) | 425 s |
| 2 | forced starts: emforce rows [surge, psi]; one EFORCE each; result started/notstarted; append at play 70/310 + 0..2.5 s; EFORCE -250..+2500 ms after the append (X->W by the 10 s P2 minimum); no Lua error in the event scripts | 2.5 s; -0.25..2.5 s |
| 3s / 3p | one ESTART 0..1000 ms after its EFORCE (X), t 0..1.0 | 1000 ms; 1.0 |
| 4s / 4p / 4n | tf == 10 in every surge / psi snapshot, == 6 in every none snapshot | exact |
| 5s / 5p | max - min of (t - X/1000) over the event | <= 0.5 s |
| 6s / 6p | one EEND, t in [220.5, 223.0] / [101.5, 104.0], no event snapshot after it; EEND t vs the snapshot clock at its X | <= 0.5 s |
| 7s / 7p | EPHASE 1..12 (13) / 1..5 (6) in order, each t in (X-1, X-1+1.0]; every e line's phase = the table's | 1.0 s |
| 8 | w only in a surge; per wave first t in (X-1, X+1.0], first z - actor z **(actor of that snapshot)** in [244, 250.5], speed [-16.5, -13.5] | see 5 |
| 9 | v only in a psi storm; per vortex integer start (0.02, every sample) in [33, 77], gaps [3, 16], first s [19.0, 21], last s [-1.0, 1.5]; **em1: y = actor y + 20 +/- 0.50 m and horizontal distance <= 151.5 m against the actor of the snapshot of its FIRST v line; a vortex whose first v line follows a kept-snapshot gap > 250 ms (X) is reported, not judged, for y and distance; at least 10 of every 12 vortices judged (judged x 12 >= 10 x n)** | see 5 |
| 10 | actor health 1.0000 in every snapshot; VITALS clean; SUMMARY min 1.000, refills 0, hits = cancelled (implies kj2's check 12) | exact |
| 11 | last T2SUM has the 20 keys in order (eerr last); torn, lost, epochs, ktabfail, eerr = 0; paused false; no EERR; no degraded snapshot (**em1: kill-channel values moved to k14**) | exact |
| 12 | no ENOTREADY / EREADY | exact |
| 13 | every snapshot `a o* r? e (w|v)* ns k*` (**em1: k lines after ns, as kill7 writes them**) | exact |
| ka | **new, ALWAYS judged (B1)**: every KAPPLY line (hit, hold, death; any id): applied = the total of the kept pose at or before its pose seq; d = that total - the id's previous applied total (capped at 1, kill7's clamp); after = max(before - d, 0), floored at min(before, 0.0100) when that pose's dead = 0 (kill7:476) | +/- 0.0002 |
| k2 | M's hit: one KAPPLY, d = the pose total, after = before - d, the first KAPPLY of M. **Judged whatever fired once M has a KAPPLY (B2)** | +/- 0.0002 |
| k3 | no second effect: every KAPPLY (any id) is a NEW total of the kept poses | 0 |
| k4 | healing left alone: **from M's hit KAPPLY to the hold KAPPLY and within the hold up to the kill call**: no KAPPLY but the hit's and the hold's, no fall > 0.02 with a K line of mine other than the hold KAPPLY between two snapshots | 0.02 |
| k6 | no creature other than the k-line ids within 10 m of M loses more than 0.02 across a KAPPLY/KDEAD | 10 m; 0.02 |
| kh | **new, the hold**: exactly one dead = 0 total after M's hit, and exactly one KAPPLY for it, at play 50 +/- 5 s (play clock, X->W by the P2 map, as kd); d = the pose difference +/- 0.0002; after = max(before - d, 0.0100) +/- 0.0002; its pose seq >= the hold total's first pose; then EVERY kept snapshot written after the hold KAPPLY and before the kill call that lists M has M alive 1, health in [0.0098, 0.0300], a plain k line (no dead, no stale), and there are at least 5 of them | +/- 5.0 s; +/- 0.0002; [0.0098, 0.0300]; >= 5 |
| kd | M's death: exactly one KDEAD for M, kind online, at play **52** +/- 5 s, after the hold KAPPLY, from a pose at or after M's first dead pose; that dead pose has dead = 1 and total >= the hold's total - 0.0002 (no exact value); if its total is above the hold's, exactly one KAPPLY in the kill call (same X, before the KDEAD) with d = the difference and after = max(before - d, 0), each +/- 0.0002; if equal, none | +/- 5.0 s; +/- 0.0002 |
| kc | **new**: exactly one KCONF for M, 0..2000 ms after the kill call (its X and its own ms field), no KNOCONF for M, and the first kept snapshot listing M at or after the KCONF has M alive 0. 0 ms is a PASS; the snapshots between the kill call and KCONF (pending) are reported only | 0..2000 ms |
| k7 | M's dead line read -0.05..+2.0 s after the watcher's first read of M's dead pose (kj2's check 7, for M) | -0.05..2.0 s |
| k8 | one KWHO for M, killer 0 | exact |
| k11 | from 2.0 s after M's first dead line, M alive 0 or not listed, to the end | 2.0 s |
| k13 | r line present after the first pose taken, never back, lag <= 2.0 s behind the newest kept pose, not ahead by > 0.05 s (no stop allowance) | 2.0 s; 0.05 s |
| k14 | EVERY key of the last T2SUM against one table: torn 0, holds/near/guards = their lines, paused false, lost 0, epochs 0, khits = the rises in the kept poses (2), **kdead_online 1**, kdead_offline 0, kowed 0, kstale 0, kback 0, krefused 0, kclamped 0, kver 0, kdead_fail 0, ktabfail 0, **eerr 0**, last_seq between the final r and the highest pose seq; **exactly 1 KCONF** (after its KDEAD, ms = time - KDEAD time, 0..2000), 0 KNOCONF; any unknown key FAILS | exact; 2.0 s |
| k15 | no snapshot holds `k,<id>,...,dead` beside an o line alive 1; no `stale` line; no id with two o or two k lines in one snapshot | 0 |
Dropped from kj2: 5 (stalker healing; no stalker), 9 and 10 (the stop; none), 12 (actor health: 10 is stricter).
Every T2SUM key of kill7 (zz_p1_puppet_kill7.script:868-869) has a row in k14: last_seq torn holds paused near guards lost
epochs khits kdead_online kdead_offline kowed kstale kback krefused kclamped kver kdead_fail ktabfail eerr.

## 3. NO VERDICT rules (decided before; each names what it leaves untested)
| rule | fires when | leaves untested |
|---|---|---|
| nve1 | an event the run did not force (kj4p NV1) | 3s 3p 4s 4p 5s 5p 6s 6p 7s 7p 8 9 |
| nve2 / nve3 | EFORCE surge / psi `notstarted` (the game refused) | 3s 4s 5s 6s 7s 8 / 3p 4p 5p 6p 7p 9 |
| nve4 | psi forced before the surge's EEND | 3p 4p 5p 6p 7p 9 |
| nvk1 | not exactly one id with k lines in the kept poses, or that id not of kind m (a stalker, or two creatures) | k2 k3 k4 k6 kh kd kc k7 k8 k11 k14 |
| nvk2 | M hurt by the world by > 0.02 before its hit (snapshots before the KAPPLY vs its `before`), a KDEAD for M not dealt by the bridge (seen / released), or M dead in X-Ray before its dead pose. **em1 (B2): the last clause does NOT fire when M's first alive-0 snapshot follows a KAPPLY of mine for M with after <= 0 or a KDEAD online for M (a death the bridge caused is judged, not excused)** | k2 k3 k4 k6 kh kd kc k7 k8 k11 k14 |
| nvk3 | M falls > 0.02 between two consecutive snapshots with no K line of mine between them, except its death step (kj2's exemption: the FIRST such fall from a snapshot written at or after M's KDEAD online, if it ends alive 0 at health <= 0; one per id) | k2 k3 k4 k6 kh kd kc k7 k8 k11 k14 |
| nvk6 | M not listed in the newest snapshot read at or before the first pose of a later step (the hold or the dead pose) | k3 k4 k6 kh kd kc k7 k8 k11 k14 |
kj2's NV4 (M's dead pose inside the stop) is dropped: there is no stop. kj4p's NV5 (no vortex) is dropped: it could never decide a
verdict (a psi storm cut short already fails 1/6p); a psi storm with no v line is a FAIL of 9. Round 1's nvk7 (the pending path did not occur) and
check kp are dropped (the split death; KCONF 0 ms is a PASS of kc). **B1/B2:** every KAPPLY's arithmetic is in ka, which no
rule leaves untested; k2 is removed from every fired rule's set whenever M has a KAPPLY. A NO VERDICT never hides a failure it does not
explain: a failing check outside the union of the fired rules' sets makes the run a FAIL, named. Always judged: 1, 2, 4n,
10, 11, 12, 13, ka, k13, k15 (and k2 once M has a KAPPLY). A missing, empty (except hits_t2.csv) or unreadable required file: FAIL BY NAME (kj2's rule;
kj4p refused instead), no check computed. Unparsable input (any malformed E*/K*/P2 line, KCONF/KNOCONF included, a snapshot
or pose breaking the grammar, emforce rows): FAIL, never excused. nvk1 fires on kj4p-r1 (no k line) and on kj2 (a stalker).

## 4. Reported, not judged
F1 ANSWER; tf forms; ESTART/EEND lines; each wave (first z, crossing, last); each vortex (start, s, y offset, distance,
judged or not and the gap before its first snapshot); the pending snapshots between the kill call and KCONF (count,
healths, KCONF ms: the pending path, if the engine happens to defer); M's health range in the hold; M's steps; every
K line; F3 hits by draftsman; the actor's x/y/z range (moving); F5 stalker counts per event; F6 SUMMARY export cost against
kj2's; the last T2SUM; M's hit time on the play clock (k2's line; plan 30).

## 5. Margin table (each bound re-derived for em1; measured = real engine runs)
Flag: `<2x` = margin under 2x; `COPIED` = value taken over without an independent derivation for em1 (the derivation given
is the source's); `desk` = no real value yet; `rule` = the edge is the scripts' own rule.
| check | bound | derivation (em1) | real measured value(s), source | margin | flag |
|---|---|---|---|---|---|
| 1 play end | >= 425 s | play end = RUN_S - 15 s (offsets 15.04 kj2, 15.3 kj3p, 15.7 kj4p) = 430; minus 5 s slack; above the latest psi end 310 + 2.5 + 2.5 + 104 = 419 (+2 s for F5) | 224.96 of 240 (kj2), 449.7 (kj3p r1), 449.3 (kj4p r1) of 465 | 5 s slack / 0.7 s largest offset deviation = 7x | - |
| 1 freeze / lost rows | 0 data rows in each | by the code on both sides 0: no stop is planned (T2_STALL_PLAY=99999, so no SIGSTOP and no freeze row); a lost row needs 5000 ms without a new pose line while the last state is not `end` (t2_xray_em1.sh:153-156), and ITR2 writes poses the whole run, then `end` poses from its stop at play 440 (first within 0.30 s, last line stays `end`: itrzone's half); the reader has stopped by play ~430 (RUN_S 445 - 15.3..15.7) | 0 rows in both files in kj2 (which had a stop of ITR2's own), kj3p r1, kj4p r1 | exact (a rule) | rule |
| 2 runner append | plan + 0..2.5 s | runner loop `sleep 1` + body | +0.910/0.945 s (kj3p r1), +0.807/0.858 s (kj4p r1) | 2.6x | - |
| 2 EFORCE late | <= 2.5 s | reader reads emtest.txt once per 1000 ms X + a frame; X->W map early <= ~70 ms | 240 ms (kj3p surge), 711 / 537 ms (kj4p) | 3.5x | - |
| 2 EFORCE early | >= -250 ms | map error: freshest P2 age + drift over 10 s (<= 70 ms, kj2 P2) | min +240 ms | 490 ms from the edge; 3.6x over the 70 ms error | - |
| 3 ESTART after EFORCE | 0..1000 ms; t 0..1.0 | start(true) in the same call; next snapshot <= 100 ms + frame | 0 ms, t -0.00 (4 events, kj3p/kj4p) | lower edge by construction; 1000/130 = 7.5x above | rule (lower) |
| 4 tf | exact | set and restored in the same Lua call | 10 / 6 in every snapshot (kj4p: 2031 / 946 / 1262) | exact | - |
| 5 t - X spread | <= 0.5 s | game seconds quantum 0.1 s at factor 10 + print | 0.010 s (kj4p surge and psi) | 50x | - |
| 6 surge EEND t | 220.5..223.0 | end at the 1 s update with ceil(t) >= 222 | 221.92 (kj3p), 221.73 (kj4p) | 1.23 s low / 1.08 s high over a 0.11 s step: 10x | - |
| 6 psi EEND t | 101.5..104.0 | as above with 103 | 102.87 (kj4p) | 1.37 / 1.13 s: 10x | one value |
| 6 EEND X vs t | <= 0.5 s | same clock as the snapshots | 0.104 (kj3p), 0.102 / 0.106 (kj4p) | 4.7x | - |
| 7 phase t | (X-1, X-1+1.0] | phase = ceil(t) >= X, first snapshot after | +0.00..+0.10 (kj4p, 19 phases) | 10x above; lower edge by construction | rule (lower) |
| 8 first w t | (X-1, X+1.0] | start_wave at ceil(t) >= X | 119.25 / 167.64 (kj3p), 119.10 / 167.47 (kj4p) | 1.36 s to the upper edge; lower by construction | rule (lower) |
| 8 first z - actor z | 244..250.5 | inited z = actor z + 250 at creation, <= 2 steps of 3 m seen late; moving actor's z changes <= 0.067 m in 250 ms (kj2) | 247.0 / 247.0 (kj3p), 250.0 / 247.0 (kj4p) | upper: 0.5 m, ~0.43 m with motion: **<2x** (one 3 m step = 1.0x) | **<2x**, kept as registered in kj3p |
| 8 speed | -16.5..-13.5 | 3 m per 2 game s at factor 10 | -15.00 (4 waves) | 1.5 m/s over ~0 spread | - |
| 9 integer start | 0.02 | two %.2f roundings | none outside 0.02 (kj4p, 2225 v lines) | desk 0.01: **2.0x** | at 2x |
| 9 start range / gaps | 33..77 / 3..16 | PSM:357-365, PVF 3..6 or stock 8..16 | 33..76, gaps 3..5 (kj4p) | rule | rule |
| 9 first s | 19.0..21.005 | created at ceil(t) = start | 20.26..20.67 (kj4p) | 1.26 s low; upper by construction | rule (upper) |
| 9 last s | -1.0..1.5 | strike at the first 1 s update with s < 1 | 0.24..0.56 (kj4p) | 0.94 s high: 2.7x | - |
| 9 height | +/-0.50 m | the actor of the first v line's snapshot vs the actor at creation, at most one gap earlier: max actor dy within 250 ms on the moving track 0.243 m (kj2, 1848 snapshots, t2span=4) | standing: 0.000 (9), 0.030, 0.031, -0.064 (kj4p); moving: none yet | 2.06x on 0.243 | **thin: one moving run without storm** |
| 9 distance | <= 151.5 m | vortex_distance_max 150 + 2 x 0.715 m (max horizontal move within 250 ms, kj2) | 47..148 m standing (kj4p) | 1.43 m above the script's 150 | rule + motion |
| 9 gap rule | first v line after a gap <= 250 ms (X) is judged | 2 x p99 snapshot gap (137 ms, kj2; X-clock p99 136, median 110) | kj4p: all 12 gaps 100 ms; kj2: 3 gaps > 250 ms in 1949 (2 around the stop) | 250 / 137 = 1.8x of p99 | **<2x (rule edge)** |
| 9 judged share | >= 10 of 12 | unjudged expected 0..1 per run | kj4p 12 of 12 | 2 unjudged allowed | guess (my generalisation to n != 12) |
| 10, 11, 12, 13 | exact | standing rule / the format | all PASS on kj4p r1 | exact | - |
| ka, k2, kh, kd arithmetic | +/- 0.0002 | two 4-decimal roundings, doubled | 0.0000 on 5 KAPPLY (kj2, ka PASS on its bytes) | all of it | - |
| k4 / k6 / nvk2 / nvk3 fall | 0.02 | healing per 100 ms << 0.001 + rounding 0.0001; smallest hit 0.3 | M's largest fall 0.0000 over 689 snapshots; M healed +0.0277 in ~97 s (kj2) | far | - |
| k6 near | 10 m | kj2's (one creature's reach) | no fall in 7 events (kj2) | - | **COPIED** (kj2's value, not re-derived) |
| kh hold health | [0.0098, 0.0300] | after the hold = max(hb - 0.695, 0.0100) (kill7:476); hb = 0.7 + regen since the hit (<= 20 s x 0.00045/s = 0.009) -> 0.0100..0.0140; + <= 0.0009 regen within the 2 s hold -> 0.0100..0.0149 expected. Lower edge 0.0100 - 0.0002 (4-decimal print + rounding); upper 0.0300 = 2.0x the expected top. The plain k line does not pin health each snapshot (kill7 writes health only on a new total), so regen shows | kj2's M (flesh 23183): 1947 snapshots, 210 upward steps while alive, max step 0.0002, max rise within any 2 s 0.0009 (~0.00045/s); no hold seen in the engine yet | lower: 0.0002 = the rounding only (floor by rule); upper 2.0x | **<5x; thin: ONE monster (flesh) in ONE run; em1's monster may be another kind** |
| kh hold snapshots | >= 5 | the 2.0 s hold (kcjh=2) / snapshot gap median 110 ms (kj2) = ~18 | kj2 gaps: median 110, p99 137 ms (X p99 136); stub: 20 | 18 / 5 = 3.6x | **<5x**; desk only for the hold itself |
| kh hold time | play 50 +/- 5 s | itrzone's rig fires at clk >= kcj + kcjd; their clock vs the runner's play0 measured | kj2: rig events at plan -1.04, -1.09, -1.04 s (S's hit, second hit, kill; X->W by P2) | 5 / 1.09 = 4.6x | **<5x; one joint run** |
| kd kill time | play 52 +/- 5 s | as kh, + kcjh = 2 s | as kh (kj2) | 4.6x | **<5x; one joint run** |
| kc KCONF | 0..2000 ms after the kill call | kill7's own KNOCONF limit (2000 ms); 0 ms allowed (the engine may confirm in the frame of the call) | 0 ms (kj2 both; kc PASS on kj2 bytes), 101 ms (kj1) | 20x on 0.1 s | rule (kill7's limit) |
| k7 dead line | -0.05..2.0 s | pose 0.05 + frame + pending (0..3 snapshots) + snapshot 0.1 + watcher | kj2 S: 6 ms; kj1 M (pending one snapshot): KCONF 101 ms | ~5x on a 0.35 s pending chain | lower edge 50 ms **COPIED** (kj2's build decision) |
| k11 | 2.0 s | kept from kj1/kj2 | 0 s (kj2: 981 snapshots) | - | **COPIED** |
| k13 r lag | 2.0 s; -0.05 s | as kj2 (no stop allowance) | kj2: max lag 68 ms (kj1), smallest 0; em1_check on kj2 bytes: PASS | 29x | 2.0 s **COPIED** |
| k14 KCONF ms | 0..2000 | kill7's own KNOCONF limit (same rule as kc) | 0 ms (kj2 both), 101 ms (kj1) | 20x on 0.1 s | - |
| NEAR / KILLER / counters | exact | the format and the plan (k14 table) | kj2 all as expected (19 keys) | exact | - |
Flags summarised: under 2x: 8 first z (upper edge, carried as registered), 9 gap rule edge (1.8x of p99), 9 integer start
(2.0x); under 5x: 2 runner append 2.6x, 2 EFORCE late 3.5x, kh hold snapshots 3.6x, kh/kd play time 4.6x, 6 EEND X 4.7x,
kh upper band 2.0x; thin: 9 height (one moving run, no storm), kh band (one flesh, one run), kh/kd (one joint run); COPIED
values: k6 near 10 m, k7 lower edge 50 ms, k11 2.0 s, k13 2.0 s. Claim class unchanged: EXCLUSIVE (it already was).

## 6. Predictions
EnginePerf, written before any em1 boot. Informed by kj2, kj3p r1 and kj4p r1, which I have seen: not blind.
- Outcome: **PASS 50 % / NO VERDICT 15 % / FAIL 35 %.**
- NO VERDICT mostly from X-Ray's world hurting M while it is held at 0.0100 (nvk2/nvk3), or not exactly one monster
  picked (nvk1).
- FAIL mostly from a first-time combination: emissions with the actor moving (check 9 or 8 near an edge), a counter the
  X-only runs never had (k14), or the hold band if em1's monster heals faster than kj2's flesh (kh).
- Hold: 15-20 snapshots; M's health 0.0100..0.0150 through it.
- KCONF: 0 ms (no pending snapshot) 60 %; otherwise 1-3 snapshots, 100-350 ms.
- Check 9: 12 vortices, 12 of 12 judged (80 %); worst |height error| 0.10-0.30 m.
- Wave first z: 247 or 250 again; outside 244..250.5: 10 %.

## 7. Score once
From /home/caden/Projects/StalkerGameEngine, after itrzone sends the X run folder and before reading any of it:
`bash wt-render-split/perf-harness/em1/em1_score.sh <X run dir> <X run dir>/em1_x_check.txt`
em1_score.sh = kj4p_score.sh with the names and the checker hash changed: it refuses unless em1_check.py hashes to the
pinned value, opens the report no-clobber before the checker runs, writes the exit code to `.rc` after, and refuses a
second call. Pinned checker hash: see em1_score.sh (CHK_SHA); re-tested in round 2 (see NOTES.md).

## 8. Pins (sha256, round 4, computed 2026-11-27 15:25 EST; the PREREG itself is pinned by its commit)
| file | sha256 |
|---|---|
| em1/em1_check.py | e3297a10283f5ede4cd5ab897b72e449067d4fb327d819506151636ba78298e3 |
| em1/em1_score.sh | 9c06d1630a3e3e09f3a621f2061df86003693ba852717d0eff29b39d9aabee92 |
| em1/em1_selftest.sh | d9ef42d0776d05dee9ec46d6836e7b565d38babc831b93243ec31a3998a36e9a |
| em1/em1_stub.lua | cd6edfb5e2a35eac28fd71aeaced50e42289d8568e3b6eea4a3a7e06bbeb2f94 |
| em1/em1_axr_step_test.sh | 96761a9e6974c3453b9e75639f838d381f3465b8befb0b35a57f962ad33235b5 |
| tools/t2_xray_em1.sh (= em1/t2_xray_em1.sh; NEW: the runner itrzone launches) | b444d8e46f534683f4ca3bf7e5fa491bd4b84243943c6953228dcce9f5536989 |
| tools/lib_engine_pids.sh (kj4p, unchanged) | cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e |
| harness/zz_p1_puppet_kill7.script (reader, kj4p, unchanged) | 0d5e59279775498e54155bc7a1ef9ecc7d8fa4f9132f7a77827fb30e3d6d9b54 |
| tools/tb_run.sh (kj4p, unchanged) | 1b8d103f2208befc554c48cf0b628830e67e0223cbe5c99ab1fcc34205363953 |
| tools/run_noguard.sh (kj4p, unchanged) | 92729abc420784053cee4b1b1832f5d112f74979a4760e2173d45578358455d5 |
| tools/t1_watch_keep.py (kj4p, unchanged) | 9df9c8e4eb2a34ec5b9059c0b26569b7d9e2e2886cc847c9630a8451e8240393 |
| run/appdata/axr_options_play.ltx (options source; the runner checks it) | 27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29 |
| run/bin/Stance14.exe (T2_EXE, kj4p, unchanged) | b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe |
| run/appdata/savedgames/cordon1.scop (kj4p, unchanged) | 6dea752bc652a45c748e1438e3bc8ffd2d8d89f24730177c792328aeb5d13f3e |
| run/appdata/savedgames/cordon1.scoc (not pinned before; hashed today) | a9f9a8a91b034de28a49be7f8029d823d057e46b4445f3f94d7255dbc77b0b6b |
| harness/zz_eperf_dump.script (tb_run.sh installs it; itrzone's hash matches) | 2b79a817a08a1e7ff4e29007f76677dba698a2a5945374331dcc2ae2de874b45 |
| out/user.ltx.orig (tb_run.sh's settings source; itrzone's hash matches) | 3efd38eecbca504001cf03bd4c60bc6e837a3c3284e8517979a1bd9e1526de78 |
itrzone pins its own mod blob, boot script and the runner/lib/reader/options source above in its half.

**Registration (2026-11-27 15:27 EST).** This file keeps its name (`...-DRAFT.md`) because the pinned checker's header names it; the
status is the title line. Its draft form was sha256 d40577d65f1ba8611c78e27ec72518e233e4f9c375195f83e626733f92a36205
(commit d51b8a259); the only changes since are the title line and this paragraph. This registered form is pinned by its
commit. itrzone's half, byte-matched by me on disk before registering (its desk commit a78411f):
| itrzone file | pin |
|---|---|
| docs/design-notes/2026-11-27-em1-itr2-half-DRAFT.md (becomes its Amendment 143 verbatim) | sha256 5d0171521420b5accde09c1512b034d921e523dd5ea9d7ac7cb1c04b04431bc9 |
| mod/ITRZone/Scripts/main.lua | git blob 9522c50a6dbb493988a1b5858978a12a73ebd027 |
| scripts/boot_t2.sh | 11138eb7ec86885ec737b837b5b10d8163195732c59a003255ba87cf203c91ef |
| tools/em1_check.py (its scorer) | 70c1ed7014029300cf96ce2986ae4d5a82fac79cccb7f8444790adbd65073e2b |
| tools/em1_selftest.py | 3ac293c9358e855de98b034a18754c3053d3818b7a838089e8386771370468ae |
| tools/kj1_check.py (imported) | f8583123a4e6f33f66a35c4e0886554520c8c2713d871bb765217b8f92b3c863 |
Matched: the schedule (30 / 50 / 52 / 70 / 310 / 440), the arm levers, the runner env in boot_t2.sh's em1 arm against
section 1 here, every X-side pin wired in that arm, the NO VERDICT table's last column against NV_UNTESTED, and the
mirror constants (FALL 0.02, TOL_H 0.0002, EARLY_MS 50). Joint verdict = the worse of the two halves.

Clock drift behind the +/- windows that mix the wall clock and the engine clock (measured, kj4p r1): wall minus engine
+3 ms over the 222 s surge, +1 ms over the 103 s psi storm; one -2.1 s step, before play 0, so outside every judged window.

## 9. Tests on these bytes (2026-11-27, desk)
See NOTES.md section "What was tested": em1_selftest.sh (stub-made run + made fixtures + checker mutants + real kj4p-r1 and
kj2 folders), em1_axr_step_test.sh (3 cases + order + bash -n). Not tested: the runner as a whole; any em1 byte of the
engine; a moving actor during an emission; the hold or the pending path in the engine.
