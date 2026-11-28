# PREREG s4d1: stage 4d-1 build 1 (body, tracers, crosshair) under the synthetic gated lever, with the held-item nudge measured (DRAFT 2026-11-27, revision 2, NOT registered)

DRAFT. Nothing here has been run. Not registered until: the exe is built and pinned, the two probe boots (section 2)
have run and said GO, every `PROBE_` value is filled in by the rules in 2.3, and the self-test has passed on the
probes' real bytes (section 8). No box run without the Overseer's GO.
Code: wt-simtick perf/tick-4d HEAD `7b1232ffc`: 8527808c9 (lever, mask, tripwires, breaks, S2, S3, S1 crosshair),
c253fafdb (bit 8 skip of the shadow nudge on render-only frames), b51285fde (bit 8 = EXPERIMENT, default mask 7),
7b1232ffc (s4 witness with six values). Design: plan-facts/stage4d1-design-2026-11-27.md; its sections F and G override
its body. Nudge: plan-facts/shadow-nudge-desk-read-2026-11-27.md ("the nudge note").
**Revision 2 (Overseer's ruling, 2026-11-27): measure before designing.** No nudge fix is registered. S4A, S4M and the
run s4d_n2 are gone. The nudge is a PROBE question (a second probe boot, mask 15) plus reported lines. Parts A, L and S
run whatever s4 shows. TWB2's bound comes from the break run's own witness. What was wrong in revision 1: NOTES.md 3.
Model copied for form and flags: PREREG-S2-4b-objects-between-ticks-2026-11-26.md, sf1/ and wu3/.
Labels: **[D]** desk (file:line), **[M]** measured (a number in a file), **[G]** guess, **COPIED** (a bound taken from an
earlier registration without re-deriving it).

## 0. Limits, first
- **The det harness is not exact for online state or for `::Random`.** Two stock det runs of the same exe already differ
  in 15-19 online object positions, in `Random=` and in `det_lua_state` (design section 0, [M]). So the exact gates
  here are only: the offline counters C (counters_diff.py rules), the in-run tripwires, and witness columns that are
  pure functions of the lever pattern. Online state, `::Random`, Lua state and the held item's position are reported,
  never gated.
- **Synthetic, not real.** The lever replaces the wall clock with a fixed pattern (one 33 ms det tick, then K-1
  render-only frames, alpha = j/K). It does not show real-time races (in det mode the game thread's half runs inline
  before the render), uneven alpha, frames with 2+ ticks, dropped ticks, the mouse path, cost, or whether anything
  LOOKS right. Those are Caden's eyes.
- **Serial det mode, first 320 A-Life passes** (about 5440 ticks [M]), bench1 save, headless Xvfb, mt_* all off
  (out/user_det.ltx.orig lines 338-346, 760-771).
- **Legs and actor shadow are ON** in the det profile: out/user_det.ltx.orig line 261 `g_legs 1` (console name of
  `g_legs_enabled`), 262 `g_legs_attach_to_camera 1`, 263 `g_legs_fwd_offset -0.5`, 537 `r__actor_shadow on` [D, read
  2026-11-27]. So the legs part of S2 and the stock shadow-pass nudge (Actor.cpp:2318-2378) run in the lever boots, IF a
  shadow pass reaches the actor headless (sun or a shadowed light; not shown; the probe's s4 draw column tells). The
  legs' drawn matrix has NO witness column: only the S2 tripwire and C cover it. Where legs and shadows are drawn is
  Caden's eyes.
- **The held item (s4) is described, not judged.** s4 shows the active item only: attachments (torch) and the
  shadow's place relative to the leg shadow are not in it (nudge note section 6).
- **Positions are printed with 5 decimals**, so every "equal" position is scored to 1e-4 m per coordinate, not
  bitwise. Lerp checks add 4 float32 ulps of the largest coordinate (tracers up to ~500 m: ulp 6e-5 m). alpha and dt
  are printed with 6 decimals (scored to 1.5e-6), hud with 3 (2e-3).
- **No real fixtures yet.** The scorer and self-test were built on synthetic run folders made from the witness format
  in the code (device.cpp sim_synth_witness_write at 7b1232ffc) and the log formats of out/det_s4c_s1. The self-test
  must be re-run on the probes' real bytes before registration.
- Not settled at the desk, settled by the probes: whether the crosshair easing runs headless, how many frames a tracer
  is drawn, whether `level.add_bullet` accepts the `hit.fire_wound` enum from Lua, whether the actor holds an item
  headless, and what the item does between ticks.

## 1. What the build changes, and the exe
- Gated mode only (sim_tick 30/60, or the lever): S2 draws the local player on foot (body, shadow root, held items
  through the child rule, legs through `m_legs_draw`) between its last two ticks. S3 draws a tracer head at
  lerp(tracer_start_position, bullet_pos, alpha), with a tick+epoch stamp. S1 eases the crosshair with
  `g_sim_render_dt`. Mask `sim_4d_mask`: 1 body, 2 tracers, 4 crosshair, 8 = EXPERIMENT (skip the shadow pass's
  held-item nudge on render-only frames), **default 7**. With sim_tick 0 and no lever, every new branch is not taken.
- Test-only, inert without `-det_mode`: `-det_synth_k K`, `-det_synth_wallclock` (must-fail: alpha and dt from the
  real clock), `-det_synth_tw` (tripwires: FNV checksum of the actor fields XFORM, XFORMShadow, active item,
  attachments and bolt around sim_render_world and the legs draw; of every bullet's position, start, dir, speed,
  fly_dist, life_time and the bullet count around CBulletManager::Render; a difference logs `* tw <slice> BAD <frame>`),
  `-sim_4d_break 1` (the actor's draw matrix is written into its XFORM) and `-sim_4d_break 2` (the drawn head is
  written into bullet_pos). The witness file is `$logs$/synth_<det_tag>.txt`.
- **Exe:** lever and tripwire boots run only on an exe from the perf-build workflow (XRCORE_PROFILER_AGG, which makes
  det mode serial; .github/workflows/perf-build.yml:48). Installed as `run/bin/SimTick18.exe`, sha256 **PIN_ME_EXE**
  (to fill when CI has built 7b1232ffc or later). The same exe serves part A (switches absent), the probes, L and S.
  Stock reference for part A: `run/bin/Stance14.exe` b4ce9c77 (COPIED from 4b/4c).
- Stimulus: `harness/zz_eperf_synth.script` sha256 prefix d7dbc6c54cd02600, NOT installed today
  (`run/gamedata/scripts/zz_eperf_synth.script` does not exist). It must be copied there before the probes and stays for
  the whole series (inert without its flags: it reads `command_line()`). If the probe forces a change (requirement R1,
  NOTES.md 4), the new hash is pinned and both probes run once more.

### 1.1 The witness line, as the code prints it (device.cpp sim_synth_witness_write at 7b1232ffc)
One line per gated frame whose presentation step ran, written after the render's join:
`F <dwFrame> T <tick> j <j> K <K> n <0|1> a <%.6f> dt <%.6f> hud <%.3f> | s2 <draw xyz> <xform xyz> <cam xyz> |
s3 <drawn> <head xyz> <pos xyz> <start xyz> <stamp_ok> | s1 <dt_used %.6f> <pos xyz> <opacity %.6f> |
s4 <item xyz> <item draw xyz> | tw <BAD count this frame>`, positions `%.5f`. Absent values are one `-` each: s2 9 dashes
(no actor); s3 11 dashes (Render not called) or `0` plus 10 dashes (Render ran, nothing drawn); s1 5 dashes (no
crosshair eased); s4 has exactly three forms: 6 numbers, 3 numbers + `- - -` (item not handed a matrix this frame), 6
dashes (no active item). 58 tokens per line. **Any other shape of any column (s4 included) makes that run's witness
malformed: every check that reads it FAILS by name.** s3 shows the FIRST drawn tracer, s1 the first crosshair eased.
s2 is filled by calling `sim_render_world()` after the render (a per-frame cache), so it exists with the body bit off
too (then draw = XFORM). s4 item = the active item's XFORM().c (simulation state) after the render; s4 draw = its
per-frame sim_render cache, read without computing it.
Engine log lines read: `command line ...`; `* det_synth: k K wallclock W tw T break B mask M`; `* det_rng_state <name>:
Random=... frame=<f> time_ms=<ms> tick=<T>` (the ` tick=` suffix only under the lever); `* tw <s2|s3> BAD <frame>`;
`det_synth stimulus: installed / start at update / <N> bullets fired`.

## 2. Probes (two boots, unscored, FIRST, before registration)
### 2.1 The runs
Both SimTick18.exe, **K = 3** (accepted by the Overseer: K = 2 has one render-only frame per tick and cannot separate
"wiped" from "adds up"), walk + tracer stimulus, tripwires on, no break:
- `s4d_probe`, mask 7 (as shipped; bit 8 off): `DET_EXTRA="-det_passes 100,200,320 -det_lua_streams -det_counters
  -det_synth_k 3 -sim_4d_mask 7 -det_synth_tw -det_synth_walk -det_synth_tracer"`.
- `s4d_probe15`, mask 15 (bit 8 on), otherwise the same: only to see the s4 pattern with the experiment on.
Reader: `python3 -I s4d1_score.py mode=probe root=out out=s4d1/probe_readings.txt` (written once; reads
out/det_s4d_probe and out/det_s4d_probe15; exit 0 GO, 1 STOP). Probe folders are kept; a probe is re-run only after a
fix that a stop rule asks for.

### 2.2 Probe stop rules (any one = the series does not start)
| code | rule | on | why |
|---|---|---|---|
| flags | engine command line carries exactly the probe switches; engine line `k 3 wallclock 0 tw 1 break 0 mask 7` (15) | both | the runs are the ones registered |
| pattern | every witness line keeps the L1 pattern (4.1) and cp1-3 sit on witnessed tick frames | both | the lever works; the witness parses (s4 included) |
| input | both probe folders and their files exist | both | |
| stimulus | `installed`, `start at update`, highest `bullets fired` >= 50, no log line naming zz_eperf_synth with "error" | mask 7 | Lua calls work, incl. add_bullet's enum argument |
| walk | actor x/z range in s2 xform >= 1.5 m | mask 7 | the walk moves the actor |
| tripwire | 0 `* tw` lines | mask 7 | the slices write no simulation field |
| alife | >= 50 A-Life calls to cp1, all on tick frames, gaps in ticks 16/17/18, at most one 16 | mask 7 | L3's rule is usable in ticks |
| body | >= 300 render-only frames with draw != xform | mask 7 | S2 draws between ticks at all |
**What s4 shows never stops the series** (Overseer: the nudge is not gated). If no item is held headless, the s4
description says so; requirement R1 (NOTES.md 4) is then a question for the Overseer, not a stop.

### 2.3 Placeholders the probe supplies (mask-7 probe; rule from reading to registered bound). Kp = 3
| placeholder | probe reading | registered bound |
|---|---|---|
| `PROBE_S2_MOVING` | n=0 frames with \|draw - xform\| > 1e-4 | S2 minimum at K = floor(P x (K-1)/(Kp-1) / 3); TWB1 minimum = floor(P x 1/2 / 3) |
| `PROBE_S2_LAG` | the L in {0,1,2} for which draw = lerp(X[T-L-1], X[T-L], a) on >= 99 % of moving n=0 frames (X[t] = s2 xform on the tick frame of tick t); desk guess L = 1 (the 4b capture is taken before the scheduler writes the actor's XFORM, design B.1) | S2 (ii) uses that L; no L at 99 % -> -1: S2 (ii) reported only |
| `PROBE_S3_FRAMES` | n=0 frames with a tracer drawn and \|head - pos\| > 1e-4 | S3 minimum = floor(P x (K-1)/(Kp-1) / 3); **P < 300 -> S3 and TWB2 dropped ("Caden's eyes only"), s4d_brk2 not booted** |
| `PROBE_S1_FRAMES` | frames with the s1 columns present | S1 minimum = floor(P x K/Kp / 3); **P < 300 -> S1 dropped ("Caden's eyes only")** |
| `PROBE_ALIFE_GAP16` | number of 16-tick gaps between A-Life calls (0 or 1) | L3: every lever run must show exactly this many |
| `PROBE_BOOT_S` (series only) | the longer probe's elapsed seconds (det_run.sh `rc=0 elapsed=`) | BENCH_TIMEOUT = max(1900, ceil(5 x PROBE_BOOT_S / 60) x 60) |
| `PIN_ME_EXE` (series only) | sha256 of the CI-built SimTick18.exe | the series refuses another hash |
The scorer holds the first five in its `CONST` dict and REFUSES to score while any is still the placeholder string.
Reported by the reader, not a bound: tracer tick frames, tracer life in ticks, drawn frames with stamp_ok 0, the lag
fit per L. "Why /3": every count minimum is a third of what the probe, scaled to the run's K, says to expect, which is
a 3x margin by construction. The scaling is exact in ticks: the walk and the tracers are functions of simulated time
and update count, and every K runs the same ~5440 ticks to cp3. Only the number of render-only frames per tick changes.

### 2.4 The held item (s4): registered predictions, read by the probe reader, never gated
Notation of the nudge note: B = the item rebuilt from the actor's bones and XFORM, d = the nudge (XFORMShadow.c -
XFORM().c, about 0.5 m horizontal, backwards). Values over j = 0, 1, 2 of one tick (K = 3). The reader prints, for
each probe and each j: the distribution (n, min, median, p90, max) of |s4_item - s4_item(j=0 of the same tick)| and of
|s4_draw - s4_item - body offset|, plus how many item frames have no draw. Body offset = s2 draw - s2 xform. The child
rule draws the item at actor_draw x inv(actor XFORM) x item, so the walk's own offset (up to ~0.2 m) is taken out.
It then gives each probe a signature: an item step of "0,0", "d,d" (equal steps at j=1 and j=2, ratio 0.75-1.25) or
"d,2d" (ratio 1.5-2.5), using a 0.05 m threshold, on >= 80 % of ticks, else "other". The draw is "absent" (>= 90 % of
item frames undrawn), "near" (median <= 0.25 m) or "far". The reader names the first case whose two signatures match:
| case | mask 7 (bit 8 off): item; draw | mask 15 (bit 8 on): item; draw | signatures 7 / 15 | probability |
|---|---|---|---|---|
| weapon, desk prediction: the render's UpdateXForm wipes the nudge (HudItem.cpp:128) | B+d, B, B; drawn at the item | B+d, B, B (bit 8 a no-op); drawn at the item | d,d near / d,d near | 45 % |
| nudge adds up (c253fafdb's premise) | B+d, B+2d, B+3d | B+d, B+d, B+d | d,2d / 0,0 | 3 % |
| bolt active, desk prediction | B+d x3; drawn at B (far) | B+d, B, B | 0,0 far / d,d | 3 % |
| no shadow pass reaches the item | B x3; never drawn | B x3; never drawn | 0,0 absent / 0,0 absent | 12 % |
| no active item headless (s4 all dashes) | - | - | reader: "none of the registered cases", item step "no item" | 30 % |
| anything else | | | "none of the registered cases" | 7 % |
Attachments (nudge note section 1: +d, +2d, +d with a weapon held) are not in s4: not measurable here.

### 2.5 After the probes
Fill CONST and the placeholders here. Add a self-test case built from the probes' real folders (real witness, log and
counters; mutants made from them). Run the self-test, register (rename without -DRAFT, record the hashes of scorer
and self-test in section 8), then run the series.

## 3. Part A: off = stock (6 boots; method of PREREG-S2-4b section 2, COPIED)
Tags alternating s4d_s1 (Stance14), s4d_o1 (SimTick18), s4d_s2, s4d_o2, s4d_s3, s4d_o3. No lever, mask or stimulus
switch. `DET_EXTRA="-det_passes 100,200,320 -det_lua_streams -det_counters"`.
| check | PASS when | bound and derivation |
|---|---|---|
| A1 stock agrees (harness) | C = 0 at cp1, 2, 3, s4d_s2 and s4d_s3 against s4d_s1 | exact; held in every part A so far [M] |
| A2 off agrees | C = 0 at 1, 2, 3 for each s4d_o* against s4d_s1 | exact |
| A3 no extra pass (frame) | \|mean cp1 frame of off - of stock\| <= 8 | COPIED (4b R3): one pass = 17 frames; observed means 0.75-2.7 apart [M] |
| A4 no extra pass (spacing) | every run: >= 50 A-Life calls to cp1, first call on the same frame, gaps 16/17/18, exactly one 16 | COPIED (4b R4); 50 = half the 100 calls traced [M] |
| A5 lever inert | the off runs log no `det_synth:` line, write no witness file, and their det_rng_state lines carry no `tick=` | exact (device.cpp: the lever needs -det_mode AND -det_synth_k; console_commands.cpp: the suffix only with the lever) |
A1 FAIL = harness not usable today: A2-A4 NO VERDICT.

## 4. Part L: the lever (5 boots; mask 0 = every slice off; no stimulus; no tripwires)
Tags s4d_l2a, s4d_l2b (K=2), s4d_l3 (K=3), s4d_l1 (K=1), s4d_l2w (K=2 + `-det_synth_wallclock`, must-fail).
### 4.1 Checks
| check | PASS when | bound and derivation |
|---|---|---|
| L1 pattern | l2a, l2b, l3, l1, every witness line: K column = K; n = 1 iff j = 0; F +1 per line; j = (previous j + 1) mod K; T +1 on n = 1 lines, unchanged on n = 0; a = j/K; dt = float(0.033f/K); hud +33 from tick frame to tick frame and = tick hud + a x 33 on n = 0; each of cp1-3 sits on a witnessed tick frame with T = its `tick=` | exact by construction (device.cpp sim_tick_plan, sim_view_present); tolerances = print rounding x 3 / x 4 |
| L1W must-fail | l2w: the pattern part of L1 holds and its F T j K n columns equal l2a's to the lower cp3 frame, AND a != j/K on >= 90 % of lines AND dt != 0.033/K on >= 90 % | a from the real clock equals j/K to 6 decimals with chance ~1e-6 per frame [D]; on a tick frame it is the tick's own duration / 33 ms > 0; the clamp gives 1, never j/K < 1. Expected ~100 % |
| L2 no wall clock | l2a vs l2b: lever columns F..hud identical line by line to the lower of the two cp3 frames | exact: every lever column is a function of the frame index, the first gated frame and dwTimeGlobal, all wall-clock free |
| L3 harness usable (harness) | l2a vs l2b: C = 0 at 1, 2, 3; cp1 ticks within 8; l2a, l2b, l3, l1: >= 50 A-Life calls to cp1, every call on a tick frame, gaps 16/17/18 in TICKS with exactly `PROBE_ALIFE_GAP16` sixteens, first call on the same tick | part A's R1/R3/R4 in ticks (COPIED); the 16 count from the probe, because a lever run may start its witness after the first call |
| L4 K=1 vs stock | reported: C of l1 against s4d_s1 | guess C = 0 70 % (design A.4) |
| L5 K=2 vs K=3 | reported: C of l2a against l3; and l2w against l2a | section 10 |
L3 FAIL = the lever is not usable: every part-S check is NO VERDICT.

## 5. Part S: slices (8 boots; all with `-det_synth_walk -det_synth_tracer -det_synth_tw`)
s4d_off2a, s4d_off2b (K=2 mask 0), s4d_on2a, s4d_on2b (K=2 mask 7), s4d_off3 (K=3 mask 0), s4d_on3 (K=3 mask 7),
s4d_brk1 (K=2 mask 7, `-sim_4d_break 1`), s4d_brk2 (K=2 mask 7, `-sim_4d_break 2`). "On runs" = on2a, on2b, on3;
"off runs" = off2a, off2b, off3. Bit 8 is off in every part-S run.
| check | PASS when | bound and derivation |
|---|---|---|
| S0 stimulus keeps det (harness) | off2a vs off2b: C = 0 at 1, 2, 3; cp1 ticks within 8 | exact / COPIED. FAIL -> SC NO VERDICT |
| S0B stimulus ran | every S run: `installed`, `start at update`, highest `bullets fired` >= 50, no script error line, actor x/z range >= 1.5 m | walk spans 3.0 m (2 x WALK_HALF 1.5, script) -> 2.0x; bullets ~171 [G: (5440-300)/30] -> 3.4x, probe-checked |
| SC same work | C = 0 at 1, 2, 3 and cp1 ticks within 8: on2a vs off2a, on2b vs off2a, on3 vs off3 | exact |
| TW tripwire | every S run except the breaks: 0 `* tw` lines; every S run: per frame to cp3, the number of `* tw` log lines = the witness tw column, and every such line is on a witnessed frame | exact (sim_4d_tw_bad counts and logs in one call) |
| TWB1 must-fail | brk1: `s2` BAD on >= floor(PROBE_S2_MOVING/2/3) distinct frames, and 0 `s3` BAD lines | the break writes XFORM on every frame whose drawn matrix differs from XFORM, at least the moving n=0 frames -> >= 3x (the brk1 witness cannot be used: its xform is printed after the break wrote it) |
| TWB2 must-fail | brk2's OWN witness: every frame to cp3 whose first drawn tracer has head != pos carries an `s3` BAD line; there are >= 30 such frames; 0 `s2` BAD lines | per frame, exact: the witness copies head and bullet_pos BEFORE the break writes bullet_pos = head (Level_Bullet_Manager.cpp Render), so on each such frame a hashed field changes. No assumption about how long pulled-back bullets live; the 30 is only an evidence floor (expected: about one frame per tick with a tracer, several hundred [G]) |
| S2 body | on runs: (i) n=0 frames with draw != xform >= floor(PROBE_S2_MOVING x (K-1)/2/3); on each of them (ii) draw = lerp(X[T-L-1], X[T-L], a), L = PROBE_S2_LAG (ii reported if L = -1), and (iii) draw = cam (4a camera between ticks); off runs: 0 frames with draw != xform | (ii), (iii) exact to 1e-4 (+4 ulp for the lerp): body and camera lerp the same captured pair with the same alpha (design B.1); 3x on (i) |
| S3 tracers | on runs: n=0 frames with head != pos >= floor(PROBE_S3_FRAMES x (K-1)/2/3); on every frame with a tracer drawn: stamp_ok 1 -> head = lerp(start, pos, a); stamp_ok 0 -> head = pos; off runs: head = pos on every drawn frame | exact to 1e-4 + 4 ulp; 3x. Dropped with the probe rule |
| S1 crosshair dt | on runs: dt_used = float(0.033f/K) on every s1 frame; off runs: dt_used = 0.033 on n=1 and 0 on n=0 frames (Device.fTimeDelta: FrameMove det step, and sim_tick_run's render-only branch); every run: s1 frames >= floor(PROBE_S1_FRAMES x K/3/3) | exact to 1.5e-6 (HUDTarget.cpp sim_4d_xhair_dt); 3x. Dropped with the probe rule |
Reported, not gated:
- s4 per on and off run: item frames, median |item - item(j=0)| per j, median |draw - item - body offset|, frames
  undrawn.
- S4X cross-rate: s4 item on the tick frames of the same T, to cp1. Pairs: on2a vs on3 and off2a vs off3. Reported:
  the share equal to 1e-4 and the largest difference.
- Online objects differing (det_diff rule) and generator/Lua state lines, for off2a/off2b (the floor), on2a/off2a,
  on3/off3, brk1/off2a and brk2/off2a, with C for the breaks.
**S4X cannot be a gate.** The item's tick-frame position = the actor's hand bones x the actor's XFORM
(Weapon.cpp UpdateXForm) + one nudge (XFORMShadow follows the camera x/z, g_legs_attach_to_camera 1). The actor's XFORM
comes from the walk, which is the same at every K, but also from its physics; the bones come from its animation. All
of that is online state, and stock det does not repeat it even against itself (section 0). Registered expectation:
equal on >= 95 % of common ticks to cp1, on pair 35 %, off pair 35 %.
Verdict rule: L3 FAIL -> every part-S check NO VERDICT; S0 FAIL -> SC NO VERDICT. The result is FAIL if any
non-harness check FAILs, else NO VERDICT if a harness check (A1, L3, S0) failed or anything is NO VERDICT, else PASS.
A dropped check is not counted.

## 6. Run order, tags, commands, time, stop and retry rules
### 6.1 Order (one series, one shared claim; tags lowercase)
probes: s4d_probe, s4d_probe15 -> (GO, fill, self-test on real bytes, register) -> A: s4d_s1 s4d_o1 s4d_s2 s4d_o2
s4d_s3 s4d_o3 -> L: s4d_l2a s4d_l3 s4d_l1 s4d_l2w s4d_l2b -> S: s4d_off2a s4d_on2a s4d_off3 s4d_on3 s4d_off2b s4d_on2b
s4d_brk1 s4d_brk2 (brk2 only if S3 is registered). The arms of each pair are spread out, not back to back. The breaks
go last; each boot starts fresh (det_run.sh restores user.ltx and loads the save).
### 6.2 Command per boot (series script s4d1_run.sh: TO BE WRITTEN, NOTES.md 4, R2)
`DET_NOCLAIM=1 BENCH_TIMEOUT=1900 DET_EXTRA="<base> <lever>" tools/det_run.sh <exe> <tag> 60 3`, then
`mv run/appdata/logs/synth_<tag>.txt out/det_<tag>/` (det_run.sh does not move the witness: det_run.sh:44-45).
`<base>` = `-det_passes 100,200,320 -det_lua_streams -det_counters` (tools/s4c_partA.sh:11, COPIED; "60 3" is ignored
under -det_passes, PREREG-S2-simtick section 9).
| tags | exe | `<lever>` |
|---|---|---|
| s4d_s1..3 | Stance14.exe | (none) |
| s4d_o1..3 | SimTick18.exe | (none) |
| s4d_probe / s4d_probe15 | SimTick18.exe | `-det_synth_k 3 -sim_4d_mask 7` / `15`, `-det_synth_tw -det_synth_walk -det_synth_tracer` |
| s4d_l2a, s4d_l2b | SimTick18.exe | `-det_synth_k 2 -sim_4d_mask 0` |
| s4d_l3 / s4d_l1 | SimTick18.exe | `-det_synth_k 3 -sim_4d_mask 0` / `-det_synth_k 1 -sim_4d_mask 0` |
| s4d_l2w | SimTick18.exe | `-det_synth_k 2 -sim_4d_mask 0 -det_synth_wallclock` |
| s4d_off2a, s4d_off2b / s4d_off3 | SimTick18.exe | `-det_synth_k 2` / `3`, `-sim_4d_mask 0 -det_synth_tw -det_synth_walk -det_synth_tracer` |
| s4d_on2a, s4d_on2b / s4d_on3 | SimTick18.exe | `-det_synth_k 2` / `3`, `-sim_4d_mask 7 -det_synth_tw -det_synth_walk -det_synth_tracer` |
| s4d_brk1 / s4d_brk2 | SimTick18.exe | `-det_synth_k 2 -sim_4d_mask 7 -det_synth_tw -sim_4d_break 1` / `2`, `-det_synth_walk -det_synth_tracer` |
The mask is always passed explicitly. The scorer checks every switch on the engine's own command line and the engine's
det_synth line (F0), so a wrong flag fails by name.
### 6.3 Time per boot (derivation)
A stock det boot to cp3 (5473-5479 frames) takes 180-211 s [M, out/s4c_partA.txt]: loading plus ~5440 tick frames.
A lever boot runs the same ticks plus (K-1) x ~5440 render-only frames. A render-only frame costs about 15 ms [G:
design C.4; no measurement], so K=1 ~211 s, K=2 ~290 s, K=3 ~375 s. Tripwire hashing is negligible [G]. Add ~40 s per
boot between boots (6 boots took 1229 s, against 1191 s elapsed [M]).
Probes 2 x 375 s + 2 x 40 s = 14 min. A 1191 s + 6 x 40 s = 24 min. L 3 x 290 + 375 + 211 s + 5 x 40 s = 28 min.
S 6 x 290 + 2 x 375 s + 8 x 40 s = 47 min. **Scored series (19 boots) ~1 h 39 min; with the two probes (21 boots)
~1 h 53 min.** The probes' elapsed times replace the 15 ms guess before registration.
BENCH_TIMEOUT 1900 s = 5.1x the K=3 guess and 9x the stock maximum (shared needs >= 5x, section 7).
### 6.4 Stop rules (no check is scored before the end; these look only at each run's own completion)
Before the series:
- SimTick18 sha256 = PIN_ME_EXE; Stance14 b4ce9c77.
- run/appdata/user.ltx has sim_tick 0 and r__framelimit 0 (cfg0, tools/s4c_partA.sh:23).
- out/user_det.ltx.orig sha256 3e0e0a6f1fe8010c (prefix; has g_legs 1 and r__actor_shadow on).
- The stimulus script is installed with its pinned hash.
- No `run/appdata/logs/synth_s4d_*.txt` and no `out/det_s4d_*` exist (except the two probe folders).
- Flag preflight as tools/s4c_partA.sh:10-16 for the base flags.
During:
- A protected title or a lost claim -> stop (box).
- A crash dump -> stop (fault).
- In-run preflight on s4d_l2a at cp1: the counter file is present, the witness file is present and non-empty, and the
  engine command line and det_synth line are as registered. Otherwise stop at once (as tools/s4c_partA.sh:38-41).
### 6.5 One-retry rule
A boot that ends rc != 0 (timeout, early exit) is run once more right away as `<tag>_r1`, with the same switches.
The scorer reads `det_<tag>_r1` when it exists and names the substitution. A second failure of the same slot stops the
series (fault). A boot is never re-run because of what its output shows. Scored ONCE, at the end:
`python3 -I s4d1/s4d1_score.py root=out out=s4d1/s4d1_score_result.txt`; no re-score after a checker fix.

## 7. Margin table
| bound | value | expected | margin | note |
|---|---|---|---|---|
| C = 0 (A1, A2, L3, S0, SC) | 0 | 0 | exact | held in every earlier part A [M] |
| cp1 frame / tick difference (A3, L3, S0, SC) | <= 8 | 0.75-2.7 [M]; an extra pass = 17 | 3.0x over the noise, 2.1x under the signal | COPIED |
| A-Life gaps (A4, L3) | 16/17/18, one 16 (L3: PROBE_ALIFE_GAP16) | held 26 of 26 earlier runs [M] | exact | COPIED |
| A-Life calls to cp1 | >= 50 | 100 [M] | 2.0x | at 2x, flagged |
| L1 a, dt | 1.5e-6 | print rounding 5e-7 | 3x | |
| L1 hud | 2e-3 | print rounding 5e-4 | 4x | |
| L1W differ share | >= 0.90 | ~1.0 | 10 % allowed vs ~0 expected | |
| walk range (S0B, probe) | >= 1.5 m | 3.0 m | 2.0x | at 2x, flagged |
| bullets fired (S0B, probe) | >= 50 | ~171 [G] | 3.4x | probe-checked |
| position equal | 1e-4 m | print rounding 5e-6 | 20x | |
| lerp ulps | + 4 ulp | lerp error <= 2 ulp [D] | 2x | at 2x, flagged |
| S2 (i), S3, S1 counts | floor(probe scaled / 3) | probe scaled | 3x | by construction |
| TWB1 count | floor(PROBE_S2_MOVING/2/3) | >= PROBE_S2_MOVING/2 | >= 3x | |
| TWB2 | every own-witness frame caught; >= 30 frames | several hundred frames [G] | per frame exact; floor ~10x [G] | no cross-run assumption |
| probe drop / stop count | 300 | ~5440 x (K-1) [G] | ~36x | |
| BENCH_TIMEOUT | 1900 s | <= 375 s [G] | 5.1x | guess; reset from PROBE_BOOT_S |
| s4 reader: item step threshold (descriptive) | 0.05 m | ~0.5 m [G] | 10x | not a verdict bound |
| s4 reader: draw near/far (descriptive) | 0.25 m | 0 or ~0.5 m | 2x either side | at 2x, flagged; not a verdict bound |
No margin is under 2x. Four are at exactly 2x (calls to cp1, walk range, lerp ulps, and the descriptive draw
threshold) and one at 2.1x (A3 against a real extra pass). All are flagged. The first two are COPIED or set by the
script's own geometry.
**Claim class per run: shared, every run** (both probes, A, L, S). No verdict depends on frame or wall timing: under
the lever every gated column is a function of the pattern. The one real-clock run (s4d_l2w) is a must-fail whose
outcome (a != j/K) holds at any load. The only timing bound is the timeout, at >= 5x. No run needs most of the GPU
(headless det boot, 1280x720).

## 8. Scorer and self-test
`s4d1/s4d1_score.py` (stdlib, `python3 -I`):
- Score mode: `root=<dir with det_<tag> folders> out=<file>`; exit 0 PASS, 1 FAIL, 3 NO VERDICT, 2 REFUSED
  (placeholders, out exists, usage).
- Probe mode: `mode=probe root=<dir> out=<file>`; exit 0 GO, 1 STOP, 2 REFUSED. The s4 description and case name are
  printed and never change the exit code.
It reads per run:
- synth_<tag>.txt, the witness (all 58 tokens validated, s4 included).
- xray.log: command line, det_synth line, det_rng_state, tw and stimulus lines.
- det_alife_counts and det_counters, for C. This mirrors tools/counters_diff.py sha256 5f2cf91df575ecf1...; the lines
  are named in the scorer header.
- det_trace_<tag>.txt.sched, for the A-Life calls, as tools/s2_partA2_score.py 3e487ea90974315d... line 22.
- alife_dump, for the reported online diff, as tools/det_diff.py ce40b1f6a24e10a1....
Every reader strips \r. Output: verdict lines `<ID> PASS|FAIL|NO VERDICT|DROPPED: ...`, then reported lines, then
`S4D1 RESULT: ...`. **19 gated checks:** F0, A1-A5, L1, L1W, L2, L3, S0, S0B, SC, TW, TWB1, TWB2, S2, S3, S1. F0 = every
run carries exactly its registered switches (the engine's own command line and det_synth line).
`s4d1/s4d1_selftest.sh` (run with nice 19) uses synthetic fixtures (LIMIT: no real run exists yet). It has:
- one good set;
- one mutant per gated check and per must-fail. Each mutant must change exactly its verdicts, plus the registered NO
  VERDICT consequences of a harness check. TWB2 has three: did not fail, a frame not caught, wrong slice;
- CRLF variants, a missing witness, a malformed s4 column (fails by name), the `_r1` retry, a dropped check, and the
  refusals;
- ten probe-mode cases over both probe boots: one fixture per registered s4 case, each named correctly; a garbage
  fixture and a no-item fixture ("none of the registered cases"); CRLF; no stimulus (STOP); no crosshair (S1
  dropped); a missing mask-15 probe (STOP).
Result 2026-11-27: `S4D1 SELFTEST: PASS (41/41)`
(s4d1/selftest-runs/20261127-192126-3224414/selftest_output.txt). **Must be re-run with a case built on the probes'
real folders before registration.** Hashes of scorer and self-test: to fill at registration.

## 9. Predictions (fixed before any run)
Probes:
- Stimulus works (incl. add_bullet's enum from Lua): 70 %. Body drawn between ticks: 85 %. Pattern, walk, tripwire and
  A-Life rules: 85 % together.
- S1 witness present on >= 300 frames (else S1 dropped, not a stop): 60 %. S3 >= 300 frames (else dropped): 70 %.
- s4 case: the table in 2.4 (weapon desk prediction 45 %, no item 30 %, no shadow pass 12 %, none of the cases 7 %,
  bolt 3 %, adds up 3 %).
- **Probe GO (series may start): about 50 %.** This is now the stimulus and the body only. s4 no longer stops it.
Series, given GO:
- Part A all PASS 75 % (as 4a/4c). L1 85 %, L1W fails as predicted 90 %, L2 80 %, L3 75 %.
- L4 (reported) C = 0: 70 %. L5 (reported) C = 0: 55 % (section 10).
- S0 80 %, S0B 90 %, SC 75 %, TW 85 %, TWB1 fails as predicted 90 %, TWB2 fails as predicted 90 %, S2 80 % (the
  camera-equality rule is the risk), S3 85 %, S1 90 %.
- All gated checks PASS, given GO: about 30 %.
- S4X (reported): equal on >= 95 % of common ticks, 35 % for each pair.

## 10. Known causes of K=2 vs K=3 differences (reported items only)
- **The shadow-pass nudge (PRE-EXISTING, Actor.cpp:2318-2378) runs in every lever boot** with legs and shadow on
  (section 0), since bit 8 is off everywhere in the series. By the nudge note's desk read, the weapon's XFORM at the
  end of a frame is B+d after a tick frame and B after a render-only frame, so which of the two the next tick reads
  depends on the frame kind and not on K. It does not build up. Attachments do build up with K (up to K x d, nudge
  note section 2). It is the same with the mask on and off at the same K, since only bit 8 touches it.
- Can it reach C (offline counters)? Only indirectly: the item and attachment positions are online state. An offline
  counter changes only if some online consequence (a sound or particle position, a script reading position()) changes
  when a squad goes offline or what it does there. The stock online noise is already there, and C has held at 0 under
  it in every part A [M]. I predict L5 C = 0 at 55 % and do not gate on K=2 vs K=3.
- Also reported only: per-frame `cam_Update` on role 1 and ROS updates per dwFrame (design A.4, F3 of the desk study).

## 11. What these runs cannot show
- Whether anything LOOKS right (body, legs, shadow, held item and its shadow, tracers, crosshair): Caden's eyes. Every
  dropped check is named "Caden's eyes only" in the result.
- Real gated mode: real alpha, real dt, threads beside the render (S3's stale-stamp branch is never exercised), the mouse.
- The legs' drawn matrix (no witness column), attachments, the minimap (dropped from build 1).
- Anything after A-Life pass 320, any other save, native Windows.
