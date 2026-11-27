# em1: joint emission witness with one pending monster kill, X-RAY SIDE (DRAFT, NOT REGISTERED, 2026-11-27)

Drafted at the desk by an Opus sub-worker for EnginePerf: no engine, no Wine, no display, no claim, no git, no review pass.
Everything below about the engine comes from kj2 (joint, real), kj3p r1 and kj4p r1 (X-only, real) and the desk stub.

**claim class: EXCLUSIVE.** The X verdict depends on wall/engine timing (check 2: runner append +2.5 s and EFORCE delay
-0.25..+2.5 s; 6: EEND X vs t 0.5 s; 9: the 250 ms gap rule; kd: the kill call at play 50 +/- 5 s; k7/k13: 2.0 s chains).
Several of those margins are under 5x (2.6x, 3.5x, 4.6x, 4.7x in section 5), so the correctness-with-timing rule keeps it
EXCLUSIVE. itrzone's own em1 checks use only the play clock (+/- 5 s, |drift| <= 3 s); that alone would not decide it, but
the joint boot takes the stricter side's class. Also: the X runner refuses to start unless an EXCLUSIVE (or B1-T2) claim is
held (t2_xray_em1.sh, line 66), so a shared claim cannot run this boot at all.

## 0. Limits first
- One joint boot: ITR2 (itrzone) drives the actor, X-Ray forces the events. God mode stays on: "actor unhurt" (check 10)
  is reported, never evidence of exposure (kj3p section 0 holds unchanged).
- **The pending kill path has never run in the engine.** kj2's two kills read dead in the frame of the call (KCONF 0 ms);
  only kj1 showed a creature alive one snapshot after the call (M at 0.0008). em1 is built to leave M at about 0.005; whether
  the engine then keeps it alive for one or more snapshots is not known. If it does not (KCONF 0 ms again), nvk7 fires and
  the pending check is NOT answered (NO VERDICT on kp only).
- **The vortex and wave checks have never seen a moving actor.** The height and distance bounds of check 9 are derived from
  one moving run WITHOUT any emission (kj2, t2span=4) and two standing runs WITH emissions (kj3p, kj4p).
- The kill checks have read real bytes from ONE joint run (kj2, kill5, with a stalker and a 20 s stop); the em1 shape (one
  monster, no stop, pending) has met only stub-made and hand-made fixtures.
- The desk stub's kill lag (250 ms) is a model of kj1's one observation, not of the engine source.
- The X runner `t2_xray_em1.sh` is kj4's runner plus one pre-boot step (axr_options restore); that step has had a desk test
  of its own lines only (3 cases); the whole runner has had `bash -n` only.

## 1. What it is
One joint boot on Cordon (save cordon1, Stance14.exe, display :141). ITR2 drives X-Ray's actor along the first metres of
the Cordon track (t2span=4: ~10 m along x at 2.5 m/s, back and forth, y changing up to ~0.5 m per leg). X-Ray's runner
forces a surge at play second 70 and a psi storm at play 310 (as kj4p). itrzone's rig deals ONE pending monster kill
(levers `kcj=30 kcjo=1 kcjd=20 kcjp=0.005`): the nearest monster M with snapshot health >= 0.9 is hit by 0.3 of full at
their play 30; at their play 50 it is killed, and the death is reported 0.005 of full short (main.lua STR.kcShort, in the
same pass as STR.kcDeath), so the pose that carries the death carries total = 0.3 + M's remaining share - 0.005 with
dead = 1. X-Ray applies that difference (KAPPLY: M left at ~0.005) and calls the kill in the same Lua call (KDEAD online);
until the engine puts the death into effect M reads alive just above 0 (the pending state), and the dead line must wait for
it (amendment 138). ITR2 stops at its play 440 (t2stop); X-Ray ends earlier (play ~430). No stop (T2_STALL_PLAY=99999).
Questions: the kj3p F1-F6 for a moving actor, plus K1: does the pending path behave as amendment 138 says in the engine
(plain k line while pending, `dead` only once M reads dead, KCONF > 0 ms, nothing applied twice)?

**Who launches X-Ray.** As in kj2 (its section 2): itrzone's boot script holds the box, starts ITR2, then runs EnginePerf's
pinned X runner. So there is no EnginePerf driver for em1. The X runner is `em1/t2_xray_em1.sh` (to be installed and pinned
by EnginePerf as `tools/t2_xray_em1.sh`): an exact copy of `tools/t2_xray_kj4.sh` (installs kill7, unchanged pin 0d5e5927)
with ONE new first step: copy `run/appdata/axr_options_play.ltx` over `run/gamedata/configs/axr_options.ltx`, check sha256
= 27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29, else print `REFUSED: axr_options restore: <reason>` and
exit 7 (new code). Runner env, to be set by itrzone's boot script (kj4p's env with the em1 run length):
`T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_MAXRESTART=0 T2_LOST_MS=5000
T2_LOSTX_MS=120000 T2_OVER_S=120 T2_STALL_PLAY=99999 T2_RUN_S=445 T2_WALL_S=1300 T2_SURGE_PLAY=70 T2_PSI_PLAY=310
BENCH_DISPLAY=:141` (option A, itrzone's choice 2026-11-27).

| play s (runner clock: header utc of the first play pose it reads) | what | expected in X-Ray |
|---|---|---|
| ~29 (their 30) | M hit 0.300 | one KAPPLY d 0.300, after = before - 0.300 |
| ~49 (their 50) | M's death, 0.005 short | KAPPLY d = pose difference, M left at 0.0044..0.0056 (+ healing); KDEAD online in the same call; M alive just above 0 with a plain k line; then M reads dead: KCONF, dead line; KWHO killer 0 |
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
| 1 | runner rc 0, no restart row, one boot, no FATAL, no bridge Lua error (three log forms), every snapshot on l01_escape with one run id, **em1: no stall.csv (no stop)**, last kept snapshot at play >= **425** (kj4p's "no kept pose with a k line" removed) | 425 s |
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
| k2 | M's hit: one KAPPLY, d = the pose total, after = before - d, the first KAPPLY of M | +/- 0.0002 |
| k3 | no second effect: every KAPPLY (any id) is a NEW total of the kept poses | 0 |
| k4 | healing left alone: between M's hit KAPPLY and its KDEAD: no other KAPPLY, no fall > 0.02 with a K line of mine between two snapshots | 0.02 |
| k6 | no creature other than the k-line ids within 10 m of M loses more than 0.02 across a KAPPLY/KDEAD | 10 m; 0.02 |
| kp | **new**: the lethal KAPPLY (same call, before the KDEAD) has d = the pose difference +/- 0.0002, after = before - d +/- 0.0002, after in [0.0020, 0.0200]; every kept snapshot from the KDEAD to the KCONF that lists M has M alive 1, health in [0.0020, 0.0200], a plain k line (no dead, no stale); at least one such snapshot; the first snapshot listing M at or after KCONF has M alive 0 | [0.0020, 0.0200] |
| kd | **new**: exactly one KDEAD for M, kind online, at play 50 +/- 5 s (X->W by the P2 map, play0 the runner's), after M's hit KAPPLY, from a pose at or after M's first dead pose | +/- 5.0 s |
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
| nve5 | the psi storm started but no v line | 9 |
| nvk1 | not exactly one id with k lines in the kept poses, or that id not of kind m (a stalker, or two creatures) | k2 k3 k4 k6 kp kd k7 k8 k11 k14 |
| nvk2 | M hurt by the world by > 0.02 before its hit (snapshots before the KAPPLY vs its `before`), a KDEAD for M not dealt by the bridge (seen / released), or M dead in X-Ray before its dead pose | k2 k3 k4 k6 kp kd k7 k8 k11 k14 |
| nvk3 | M falls > 0.02 between two consecutive snapshots with no K line of mine between them, except its death step (kj2's exemption: the FIRST such fall from a snapshot written at or after M's KDEAD online, if it ends alive 0 at health <= 0; one per id) | k2 k3 k4 k6 kp kd k7 k8 k11 k14 |
| nvk6 | M not listed in the newest snapshot read at or before the first pose of a later step (its dead pose) | k3 k4 k6 kp kd k7 k8 k11 k14 |
| nvk7 | **new**: M's one KDEAD online exists and no kept snapshot written at or after it lists M alive 1 (the pending path did not occur) | kp |
kj2's NV4 (M's dead pose inside the stop) is dropped: there is no stop. A NO VERDICT never hides a failure it does not
explain: a failing check outside the union of the fired rules' sets makes the run a FAIL, named. Always judged: 1, 2, 4n,
10, 11, 12, 13, k13, k15. A missing, empty (except hits_t2.csv) or unreadable required file: FAIL BY NAME (kj2's rule;
kj4p refused instead), no check computed. Unparsable input (any malformed E*/K*/P2 line, KCONF/KNOCONF included, a snapshot
or pose breaking the grammar, emforce rows): FAIL, never excused. nvk1 fires on kj4p-r1 (no k line) and on kj2 (a stalker).

## 4. Reported, not judged
F1 ANSWER; tf forms; ESTART/EEND lines; each wave (first z, crossing, last); each vortex (start, s, y offset, distance,
judged or not and the gap before its first snapshot); the pending snapshots (count, healths, KCONF ms); M's steps; every
K line; F3 hits by draftsman; the actor's x/y/z range (moving); F5 stalker counts per event; F6 SUMMARY export cost against
kj2's; the last T2SUM; M's hit time on the play clock (k2's line; plan 30).

## 5. Margin table (each bound re-derived for em1; measured = real engine runs)
Flag: `<2x` = margin under 2x; `COPIED` = value taken over without an independent derivation for em1 (the derivation given
is the source's); `desk` = no real value yet; `rule` = the edge is the scripts' own rule.
| check | bound | derivation (em1) | real measured value(s), source | margin | flag |
|---|---|---|---|---|---|
| 1 play end | >= 425 s | play end = RUN_S - 15 s (offsets 15.04 kj2, 15.3 kj3p, 15.7 kj4p) = 430; minus 5 s slack; above the latest psi end 310 + 2.5 + 2.5 + 104 = 419 (+2 s for F5) | 224.96 of 240 (kj2), 449.7 (kj3p r1), 449.3 (kj4p r1) of 465 | 5 s slack / 0.7 s largest offset deviation = 7x | - |
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
| k2, kp arithmetic | +/- 0.0002 | two 4-decimal roundings, doubled | 0.0000 on 5 KAPPLY (kj2) | all of it | - |
| k4 / k6 / nvk2 / nvk3 fall | 0.02 | healing per 100 ms << 0.001 + rounding 0.0001; smallest hit 0.3 | M's largest fall 0.0000 over 689 snapshots; M healed +0.0277 in ~97 s (kj2) | far | - |
| k6 near | 10 m | kj2's (one creature's reach) | no fall in 7 events (kj2) | - | **COPIED** (kj2's value, not re-derived) |
| kp pending health | [0.0020, 0.0200] | 0.005 short -/+ 0.0005 (pose total %.3f) -/+ 0.00005 (snapshot %.4f the puppet copies) -> 0.0044..0.0056; + healing during the puppet's <= 1.5 s staleness (up-writes once a second + ~0.25 s chain) at <= 0.0096/s (fastest measured: S +0.357 in 40 s, kj1/kj2) -> <= 0.0200; lower edge 0.0020 = 0.0044 / 2.2 | **none** (kj1's M: 0.0008 after a different total; kj2: 0.0000) | 2.2x low; 3.6x high on 0.0056 | **desk only** |
| kd kill time | play 50 +/- 5 s | itrzone's rig fires at clk >= kcj + kcjd; their clock vs the runner's play0 measured | kj2: rig events at plan -1.04, -1.09, -1.04 s (S's hit, second hit, kill; X->W by P2) | 5 / 1.09 = 4.6x | one joint run |
| k7 dead line | -0.05..2.0 s | pose 0.05 + frame + pending (expected 1-3 snapshots) + snapshot 0.1 + watcher | kj2 S: 6 ms; kj1 M (pending one snapshot): KCONF 101 ms | ~5x on a 0.35 s pending chain | lower edge 50 ms **COPIED** (kj2's build decision) |
| k11 | 2.0 s | kept from kj1/kj2 | 0 s (kj2: 981 snapshots) | - | **COPIED** |
| k13 r lag | 2.0 s; -0.05 s | as kj2 (no stop allowance) | kj2: max lag 68 ms (kj1), smallest 0; em1_check on kj2 bytes: PASS | 29x | 2.0 s **COPIED** |
| k14 KCONF ms | 0..2000 | kill7's own KNOCONF limit | 0 ms (kj2 both), 101 ms (kj1) | 20x on 0.1 s | - |
| NEAR / KILLER / counters | exact | the format and the plan (k14 table) | kj2 all as expected (19 keys) | exact | - |
Flags summarised: under 2x: 8 first z (upper edge, carried as registered), 9 gap rule edge (1.8x of p99), 9 integer start
(2.0x); thin: 9 height (one moving run, no storm), kp (desk only), kd (one joint run); COPIED values: k6 near 10 m, k7
lower edge 50 ms, k11 2.0 s, k13 2.0 s.

## 6. Predictions
EnginePerf, written before any em1 boot. Informed by kj2, kj3p r1 and kj4p r1, which I have seen: not blind.
- Outcome: **PASS 40 % / NO VERDICT 30 % / FAIL 30 %.**
- NO VERDICT mostly from nvk7: the engine confirms the kill at once (kj2: KCONF 0 ms for both kills), so the pending
  path never shows. I give "nvk7 clear, the pending path seen" 45 %.
- FAIL mostly from a first-time combination: emission with the actor moving (check 9 or 8 near an edge), or a kill
  counter the X-only runs never had (k14).
- Check 9: 12 vortices, judged 12 of 12 (80 %); worst |height error| 0.10-0.30 m.
- Wave first z: 247 or 250 again; a value outside 244..250.5: 10 %.
- If pending: 1-3 snapshots, KCONF 100-350 ms.

## 7. Score once
From /home/caden/Projects/StalkerGameEngine, after itrzone sends the X run folder and before reading any of it:
`bash wt-render-split/perf-harness/em1/em1_score.sh <X run dir> <X run dir>/em1_x_check.txt`
em1_score.sh = kj4p_score.sh with the names and the checker hash changed: it refuses unless em1_check.py hashes to the
pinned value, opens the report no-clobber before the checker runs, writes the exit code to `.rc` after, and refuses a
second call. Tested 2026-11-27 on the real kj4p-r1 folder: rc 3 (NO VERDICT, nvk1), second call refused, rc 2.

## 8. Pins
To fill at registration (checker, selftest, stub, axr step test, runner t2_xray_em1.sh as installed in tools/, reader
kill7 0d5e5927 unchanged, writer/watcher/tools as kj4p, exe, save, the axr_options pinned hash 27d54b9f).

## 9. Tests on these bytes (2026-11-27, desk)
See NOTES.md section "What was tested": em1_selftest.sh (stub-made run + made fixtures + checker mutants + real kj4p-r1 and
kj2 folders), em1_axr_step_test.sh (3 cases + order + bash -n). Not tested: the runner as a whole; any em1 byte of the
engine; a moving actor during an emission; the pending path in the engine.
