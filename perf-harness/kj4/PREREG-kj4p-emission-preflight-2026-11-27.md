# kj4p: second X-ONLY emission preflight, with the fixed reader kill7 (REGISTERED 2026-11-27). No boot before the supervisor's GO.

**claim class: EXCLUSIVE** (kept as ruled for kj3p: the checks have timing bounds of 0.5-2.5 s, and two margins are at 2x).

## 0. Limits first
- One boot, X-Ray only, no ITR2, god mode on. "Actor unhurt" (check 10) is reported, never evidence of exposure.
- The psi storm half has never produced a line in the real engine. kj3p r1 (RESULT-kj3p-2026-11-27.md) failed there:
  the kill6 reader compared two manager objects with `==`, which raises in the engine.
- The desk stub is a model. It now raises on that comparison, but other engine-only behaviour can still differ.
- The new self-test check on the real kj3p r1 folder has no mutant of its own (section 5).

## 1. What this is
**Everything of kj3p holds except the following.** kj3p = `../kj3/PREREG-kj3p-emission-preflight-2026-11-26.md`,
sha256 `54ea1fed8a9bba7ebb7545d563bd8d63f32490bd5e58abbe2193fd2518b04719` (with its amendment 1). Unchanged: the
questions, the boot (Stance14.exe, save cordon1, display :141, runner env, forced surge at play second 70, psi at 310,
T2_RUN_S 465, T2_WALL_S 900), every check and bound of the checker, the NO VERDICT rules, the stop rules, the driver's
logic (with the handoff fix, which worked live on 2026-11-27 10:11), score once.

Changed:
1. **Reader:** `harness/zz_p1_puppet_kill7.script` (copy `kj4/zz_p1_puppet_kill7.script`). Against kill6, three lines:
   166, 168, 172 choose by `kind == "surge"` / `kind == "psi"` instead of `M == sm` / `M == pm`. Nothing else. The
   line format, version 4 and every other rule are as kill6.
2. **Runner:** `tools/t2_xray_kj4.sh` = t2_xray_kj3.sh with one line changed (line 62 installs kill7).
3. **Desk stub** `kill7h_stub.lua`: the two fake managers get a shared `__eq` that raises the engine's message.
   `kill7h_test.sh` on kill7: 87 checks pass, 124 mutants caught. The same stub on the OLD kill6 bytes fails 5 checks
   (em_psi_timeline, em_psi_bounds, em_vortex, em_ns, em_order) with the degraded group, the engine's symptom.
4. **Files renamed** kj3p -> kj4p (driver, self-test, checker, scorer, track row). The checker differs from kj3p's
   only in two comment lines (names). State folder out/kj4p_state, out dir `out/kj4p`, claim text says kj4p.
5. **Self-test:** one more check: the checker run on the REAL engine folder out/kj3p-r1/run (read in place) must give
   FAIL with exactly 3p, 4p, 4n, 5p, 6p, 7p, 9, 11, as scored on 2026-11-27. Missing folder fails by name.

## 2. Command
From /home/caden/Projects/StalkerGameEngine, as a harness background task:
`BENCH_DISPLAY=:141 bash wt-render-split/perf-harness/kj4/kj4p_run.sh out/kj4p`
One retry after a clean stop before the boot, on the supervisor's GO only: same command, `out/kj4p-r1`.
Before the boot: `nice -n 19 bash kj4/kj4p_selftest.sh` on the pinned bytes, its last line into the run log.
Score once: `bash wt-render-split/perf-harness/kj4/kj4p_score.sh out/kj4p/run out/kj4p/kj4p_check.txt`

## 3. Margin table
Every bound is **COPIED from kj3p section 6** (flag: copied, not re-derived). The boot is the same; what is new is one
real run to hold the surge bounds against. Psi bounds still rest on the desk derivation alone.

| check | bound | kj3p r1 measured (real engine) | margin on the real value | flag |
|---|---|---|---|---|
| 1 play end | >= 440 s | 449.7 s | 9.7 s slack | COPIED |
| 2 runner append | plan + 0..2.5 s | +0.910 s (surge), +0.945 s (psi) | 2.6x | COPIED |
| 2 EFORCE delay | -250..2500 ms | 240 ms (surge) | 10x late side; 490 ms from the early edge | COPIED |
| 3 ESTART after EFORCE, t | 0..1000 ms, 0..1.0 | 0 ms, -0.00 | on the lower edge by construction (same call) | COPIED; **prints -0.00** |
| 5 t - X spread | <= 0.5 s | 0.010 s | 50x | COPIED |
| 6 surge EEND t | 220.5..223.0 | 221.92 | 1.42 below / 1.08 above, t steps 0.11: 10x | COPIED |
| 6 EEND X vs t | <= 0.5 s | 0.104 s | 4.8x | COPIED |
| 7 phase t | (X-1, X-1+1.0] | X-1 + 0.02..0.09 | 11x above; **lower edge +0.02 (by construction)** | COPIED |
| 8 first w t | (X-1, X+1.0] | 119.25, 167.64 | 0.25 / 0.64 above the lower edge (by construction); 1.75 / 1.36 below the upper | COPIED |
| 8 first z - actor z | 244..250.5 | 247.0 (both waves) | 3.0 m = one 3 m step: **1.0 step, UNDER 2x** | COPIED; **thinner than the desk said** (desk expected 247..250) |
| 8 speed | -16.5..-13.5 | -15.00 | 1.5 over ~0 spread | COPIED |
| 4 tf, 10-13 | exact | 10 / 6; all PASS | exact | COPIED |
| psi: 3p, 5p, 6p (101.5..104.0), 7p, 9 (start 33..77, gaps 3..16, first s 19.0..21, last s -1.0..1.5, y +-0.05, <= 150.5 m) | as kj3p | **no real value yet** | desk only (kj3p: 2.0x at the tightest) | COPIED, UNMEASURED |

Flags: every row copied. One real margin under 2x (first z: a wave seen two steps late would be 244.0, on the edge, and
would FAIL; kept as registered, not widened after seeing data). `-0.00` at ESTART passed the 0..1.0 bound in r1.

## 4. Predictions (EnginePerf, before the boot; informed by kj3p r1's surge half)
- **PASS 40 % / NO VERDICT 15 % / FAIL 45 %.**
- Surge half passes again: 85 %. Its risk is the first-z bound and run-to-run timing.
- Psi half: the reader no longer raises: 90 %. All psi checks pass given that: 50 %. Never run in the engine, so a wrong
  desk line is likely somewhere: vortex fields (PSM:412), the 103 s end, phase thresholds, or the game's own PSM:361.
- NO VERDICT: NV5 (no vortex at all) 10 %, the rest 5 %.
- eerr = 0 at the end: 80 %.

## 5. Tests on these bytes (2026-11-27)
- `KJ4P SELFTEST: PASS (checker part rc 0; score-once states 7/7; driver functions offline 34/34)`; checker part:
  34/34 mutants failing exactly their checks, made NO VERDICT 4/4, missing-file REFUSED 8/8, real kj2 bytes ok; plus
  `ok   real kj3p-r1 run folder: FAIL 3p 4p 4n 5p 6p 7p 9 11`.
- Weakness: the real-folder check has no mutant. It is a second read of bytes the kj3p checker already scored; it
  shows the renamed checker reads real engine bytes the same way, nothing more.
- The driver's functions are tested offline; the driver's main body ran for real once (kj3p r1, same logic).

## 6. Pins (sha256)
| file | sha256 |
|---|---|
| kj4/kj4p_run.sh | c24279e6902835c59ecfbca0391e9c0943585031c6ac71440f2ba9ca4323f26c |
| kj4/kj4p_selftest.sh | 307cff81725f3b067d7d63e370b23fa1c87b731898cf7fb836c01d98f1d992fb |
| kj4/kj4p_check.py | b0b6d70f08eb61bf6778c9ad77c300489c36b961d84aec277882aaaeeaa9bd51 |
| kj4/kj4p_score.sh | d29b06a4f1822c26bbc6679b3ddd66fdeb447991faca0409809737057ef8d445 |
| kj4/kj4p_track_start.csv | 0a8bce3976ff0ef81fb0b5ded24c878088e83410f988ecf52d43fa148eb5a5e9 |
| tools/t2_xray_kj4.sh (copy kj4/t2_xray_kj4.sh) | 16f48396fd6e17637acec6481882199d096f2a54f10c843b892e36fc2b7ab54f |
| harness/zz_p1_puppet_kill7.script (copy kj4/) | 0d5e59279775498e54155bc7a1ef9ecc7d8fa4f9132f7a77827fb30e3d6d9b54 |
| kj4/kill7h_stub.lua | de1b6f8b0ce66a9555f512a48bcbf70e7d95a21cabb8edda38d43bb2a8dc43a7 |
| kj4/kill7h_test.sh | 783bc9bb0e6d8c05c903b181711a523e28d3b0e508914627670324d57eec15a9 |
Every other pin inside kj4p_run.sh is kj3p's, unchanged (exe, save, tools).

## 7. Stated at registration (EnginePerf, 2026-11-27)
- Review: one CodeRabbit pass on the committed draft (d8f9bb147, all 10 files): no new findings
  (plan-facts/cr_kj4p_2026-11-27.txt). No byte of a pinned file changed after that pass.
- The pins of section 6 were recomputed from the committed bytes; the self-test line of section 5 is from those bytes.
- This text's own sha256 is sent to the supervisor with the GO request and is not written here.
