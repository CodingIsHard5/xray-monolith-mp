# s4d1 notes (2026-11-27, desk only: no game run, no build, no git change)

Files: PREREG-s4d1-leftovers-build1-DRAFT.md, s4d1_score.py, s4d1_selftest.sh, this file, selftest-runs/<last>/.

## 1. Corrections from the coordinator, applied
- **Legs and actor shadow are ON, not off.** The first brief said "legs are off". The det runs use out/user_det.ltx.orig
  (tools/det_run.sh:28 copies it over run/appdata/user.ltx before every boot). That file says, read 2026-11-27:
  line 261 `g_legs 1`, 262 `g_legs_attach_to_camera 1`, 263 `g_legs_fwd_offset -0.5`, 266
  `g_legs_render_attachments_shadow 1`, 537 `r__actor_shadow on`, 538 `r__actor_shadow_in_demo_record 1`. The console
  name is `g_legs` (variable `g_legs_enabled`, read by canRenderLegs, Actor.cpp). Consequences in the prereg: the legs
  part of S2 runs in the lever boots (if the shadow pass and first-eye legs render headless); the pre-existing
  shadow-pass nudge is active wherever mask bit 8 is off; K=2 vs K=3 may differ in the held item's XFORM (section 10
  of the prereg: predicted, not gated). The legs' drawn matrix still has no witness column.
- **(Superseded, revision 1)** "Nudge fix c253fafdb is registered": S4A, S4M, run s4d_n2, on runs at mask 15, stop
  rule P8. All removed in revision 2 (next item, and section 3).
- **Revision 2 (Overseer, 2026-11-27): measure before designing.** b51285fde makes bit 8 an EXPERIMENT, off by default
  (default mask 7); on runs use mask 7, off runs mask 0. 7b1232ffc gives s4 six values (item XFORM().c, then the item's
  draw position from the sim_render cache, or `- - -`). Applied: S4A, S4M and s4d_n2 removed; a second unscored probe
  s4d_probe15 (K=3, mask 15); the s4 patterns of plan-facts/shadow-nudge-desk-read-2026-11-27.md registered as
  predictions with probabilities (prereg 2.4); scorer `mode=probe` prints per-j distributions and names the matching
  case (descriptive, never gated); what s4 shows no longer stops the series; TWB2 bound from brk2's own witness; s4
  still parsed and validated in every run (a malformed s4 fails by name); s4 per run and S4X cross-rate reported.

## 2. Where the code's real output differs from the design text (C.1, A.6, A.7, B)
1. No s9 column (minimap dropped); instead an **s4** column before `tw`: 6 values at 7b1232ffc (item xyz, item draw
   xyz), three forms only: 6 numbers, 3 numbers + `- - -`, 6 dashes. 58 tokens per line.
2. **s3 has 11 values**, not 10: `drawn` count first, then head, pos, start (= tracer_start_position), stamp_ok. Two
   absent forms: 11 dashes = Render not called; `0` + 10 dashes = Render called, nothing drawn (also when there are no
   bullets: the witness sets drawn 0 before the empty-list return).
3. **s1 values**: dt_used and opacity with 6 decimals, pos with 5. s1 is filled whenever IntegrateOpacity runs, with the
   mask bit on OR off (so off runs show Device.fTimeDelta: 0.033 on tick frames, 0 on render-only frames).
4. **s2 is filled with the body bit off too** (the witness calls sim_render_world itself); then draw = xform.
5. `tw` is the per-frame BAD count (sim_4d_tw_bad increments it and logs `* tw <slice> BAD <frame>` with Device.dwFrame).
   There is no S1 tripwire (no simulation field), only s2 and s3.
6. The witness is written only for frames whose presentation step ran (after the render's join, while
   g_sim_render_lerp); `a` and `j` are the presentation step's (before j is advanced).
7. The `* det_synth: k K wallclock W tw T break B mask M` line is printed only if K, tw or break is set; a second line
   `* det_synth: K <k>[ (wallclock)], witness <path>` when the witness file opens.
8. det_rng_state gets ` tick=<g_sim_view_tick>` only under the lever (byte-identical line otherwise).
9. Breaks: `-sim_4d_break` bit 1 = body (XFORM = draw matrix), bit 2 = tracers (bullet_pos = drawn head). The design's
   `S-brk29` (S2+S9) is now `s4d_brk1` (S2 only) and `S-brk3` is `s4d_brk2`.
10. Mask: design 15 = S2|S3|S1|S9. Now bit 8 = nudge-skip EXPERIMENT (b51285fde), default mask 7; masks above 15 are
    refused by the parser (0..15).
11. Wallclock variant: n and j still follow the pattern; only alpha, dt (and so hud) come from the real clock.
12. Tracer stamp = tick AND epoch (review fix), not tick only.
13. The witness file goes to `$logs$` (run/appdata/logs); **tools/det_run.sh does not move it** (lines 44-45 move dumps,
    traces, counters, saves only). The series must move it (R2).
14. `hud` under the lever = s_sim_ms + alpha x 33 where s_sim_ms = dwTimeGlobal after the newest tick (exact).
15. On a tick frame alpha = 0, so a tracer head is drawn at tracer_start_position (not at bullet_pos). This is what makes
    the break-2 must-fail fire on every tick frame with a tracer. TWB2's bound no longer uses that count from another
    run: it is read from brk2's own witness (head != pos on a drawn frame -> an s3 BAD line on that frame), section 5.

## 3. What was wrong in the first version of S4, and why
Revision 1 gated S4A ("with bit 8 on, every render-only frame's s4 = its tick frame's s4") and S4M (must-fail: at mask 7
the item moves by a nudge per render-only frame). I registered them because the brief said to, while writing down in
this section that my desk read did not support their premise. That was wrong in three ways:
1. **The gate tested a premise, not an outcome.** c253fafdb's message says the nudge "added up between ticks" because
   the items' update resets XFORM only once per update. But CWeapon::renderable_Render -> CHudItem::renderable_Render
   calls UpdateXForm() first (HudItem.cpp:126-128), and UpdateXForm rebuilds XFORM from the bones whenever dwFrame is
   new (Weapon.cpp:183-250; a render-only frame has a new dwFrame). So the item is rebuilt every rendered frame and the
   nudge cannot add up. The nudge note confirms it (B+d, B, B with bit 8 off AND on). Under that reading S4A FAILS in
   both arms (the tick frame keeps one nudge, render-only frames do not), and S4M "passes" or fails for reasons that
   have nothing to do with the fix. A registered check whose likely outcome says nothing about the code is a predictable
   non-result, which the house rules forbid.
2. **A stop rule hung the whole series on that premise.** P8 stopped parts A, L and S unless the probe showed
   "accumulates", my own 20 % guess. So most of the time, an unproven fix to a pre-existing stock behaviour would have
   blocked the checks of the three slices that do work (body, tracers, crosshair).
3. **The s4 column could not tell the cases apart.** With item XFORM().c only, "wiped by the render" and "never drawn
   in a shadow pass" look alike, and the bolt (drawn at B while its XFORM keeps B+d) looks like "constant". The second
   triple (the draw position, 7b1232ffc) together with the bit-8-on probe separates the registered cases (prereg 2.4).
   Attachments still are not visible.
What replaced it: no gate on the nudge. Two probe boots (mask 7 and 15), a descriptive reader that names the case, and
reported s4 lines per run. A malformed s4 still fails by name, because the column is part of the witness format.
Any decision about bit 8 (drop it, or skip the rebuild instead) is the Overseer's, after the probes.

## 4. Requirements for things outside this delivery
- **R1 (stimulus, only if the probes show no active item: s4 all dashes):** the actor must hold an item headless. Lua API exists:
  `db.actor:item_in_slot(n)`, `db.actor:active_slot()`, `db.actor:activate_slot(n)`, `db.actor:active_item()`
  (script_game_object_script2.cpp:161, 363, 368, 369). Proposed addition to harness/zz_eperf_synth.script (NOT made
  here): at the first stimulus update, if `db.actor:active_item()` is nil, call `db.actor:activate_slot(n)` for the
  first n in {2, 3} (pistol, rifle in CoP numbering [G for GAMMA]) whose `item_in_slot(n)` is not nil; print
  `* det_synth stimulus: active item <section> slot <n>` or `... no item`. Same in both arms of every pair, so SC is
  not affected. Spawning a weapon (alife_create_item) is the fallback, but it creates a server object (registered
  count, id allocation) and would make part S differ from part L more; avoid. After any script change: re-pin its hash
  and both probe boots once more. Not a stop rule now: a question for the Overseer.
- **R2 (series script s4d1_run.sh, not written):** per boot `DET_NOCLAIM=1 BENCH_TIMEOUT=<1900 or from PROBE_BOOT_S>
  DET_EXTRA="..." tools/det_run.sh <exe> <tag> 60 3`, then move `run/appdata/logs/synth_<tag>.txt` into
  `out/det_<tag>/`; refuse to start if any `run/appdata/logs/synth_s4d_*.txt` or `out/det_s4d_*` exists; pin
  SimTick18 (PIN_ME_EXE), Stance14 b4ce9c77, user_det.ltx.orig 3e0e0a6f..., the stimulus script; cfg0; the in-run
  preflight on s4d_l2a; the one-retry rule as `<tag>_r1` (its det_tag is `<tag>_r1`, which the scorer expects);
  one shared claim for the whole series (WANT shared, not EXCLUSIVE). det_pair.sh cannot be used directly: it passes
  one DET_EXTRA to all its runs.
- **R3:** install harness/zz_eperf_synth.script into run/gamedata/scripts/ before the probe (it is not there today).
- **R4 (script constants, design G):** TRACER_AZIMUTH_DEG 180 is not checked against the Duty squad's bearing; bullets are
  sent as the actor's with power 0 but hit callbacks still fire on an NPC. The probe reports, not checks, that; a
  script-error or an NPC hit would show only as online noise or a C difference.

## 5. Open questions
1. (Settled) Probe at K = 3: accepted.
2. (Settled) The series runs whatever s4 shows; P8 is gone.
3. (Settled) TWB2 no longer assumes how long pulled-back bullets live: every brk2 frame whose drawn tracer has
   head != pos must carry an s3 BAD line, with >= 30 such frames as an evidence floor. Still a desk claim: the witness
   copies head and bullet_pos before the break writes (Level_Bullet_Manager.cpp Render). If that order is wrong, the
   synthetic fixture would not show it; the real brk2 bytes would (frames without BAD).
4. S2 (iii) "draw = camera" assumes both use the same captured pair. If the 4b history is rejected for the actor (by
   the 5 m snap or a missing previous capture) while the camera's is not, draw = xform and the frame is not counted;
   if the reverse, it would show as a failure. Desk says it does not happen (design B.1 (a)); not shown.
5. Part L runs have no tripwires (no `-det_synth_tw`), as in the design. The probes and part S have them.
6. The probe reader's draw metric takes out the body's own draw offset (s2 draw - s2 xform), because the child rule
   draws the item at actor_draw x inv(actor XFORM) x item. That is right for translation only. Headless there is no
   turning, so the rotation part should be ~0 [G]. If not, "near" and "far" blur; the distributions are printed anyway.
7. Memory note: the brief forbids writing to ~/Documents, so the CLAUDE.md memory entry for this task was not
   written; the calling session should add it.

## 6. What the probe must tell us
- Does the lever pattern hold on real bytes (P2), do the checkpoints sit on witnessed tick frames, and how long is a
  K=3 boot (PROBE_BOOT_S -> timeout).
- Does the stimulus run: the add_bullet enum argument from Lua (P3), the walk moves the actor (P4).
- Is the body drawn between ticks (P7) and with which tick lag (PROBE_S2_LAG; my guess 1).
- How many frames show a tracer drawn between ticks (PROBE_S3_FRAMES), how many tick frames show one
  (reported), the tracer life in ticks (reported), and whether stamp_ok is ever 0 (reported).
- Does the crosshair easing run headless and on how many frames (PROBE_S1_FRAMES; < 300 -> S1 "Caden's eyes only").
- Is an item in the hands headless (s4 present); which registered s4 case holds (weapon desk prediction, adds up,
  bolt, no shadow pass, or none of them), from the mask-7 and mask-15 probes together. Descriptive only.
- How many 16-tick gaps the A-Life calls show under the lever (PROBE_ALIFE_GAP16).
- That no tripwire fires with mask 7 (P5).

## 7. Not settled at the desk
- Whether the actor shadow pass runs headless at all (sun / light shadows at the bench1 time of day, renderer R2+
  under DXVK). If it does not, the nudge never runs and the reader says "no shadow pass" (item B x3, never drawn):
  the nudge is then Caden's eyes only.
- Whether det_rng_state cp lines are logged on a tick frame (the dump script runs in an actor update, i.e. inside the
  tick's FrameMove [G]); L1 anchors check it, the probe shows it.
- Whether the first gated frame and dwTimeGlobal at the first tick are the same in two boots (L2 assumes so; R4 of part A
  shows the first A-Life call on the same frame in every stock run [M], which suggests it).
- The render-only frame cost (15 ms is the design's guess) and so every time estimate.

## 8. Self-test
`nice -n 19 bash s4d1_selftest.sh` -> `S4D1 SELFTEST: PASS (41/41)`, folder
selftest-runs/20261127-192126-3224414 (6.3 MB apparent, 12 MB on disk: many small files at 4 KB blocks). The fixtures
are synthetic (120 ticks, cp1/2/3 at ticks 60/85/110; probes 175 ticks), six-value s4. The filled copy of the scorer
lowers ALIFE_MIN_CALLS from 50 to 3 for them; that rule's code is unchanged and runs. Cases:
- the good set and its CRLF variant;
- one mutant per gated check (19);
- must-fail "did not fail" cases: L1W, TWB1, TWB2;
- TWB2 also: one frame not caught (the break moved a tracer, no BAD line) and a wrong slice;
- a tripwire consistency mutant, a CRLF mutant, a missing witness, a malformed s4 column (fails by name);
- the `_r1` retry, a dropped check (S1), and the refusals (placeholders, out exists);
- ten `mode=probe` cases over both probe boots. There is one fixture per registered case (weapon, adds up, bolt, no
  shadow pass), each named correctly. Garbage and no-item fixtures give "none of the registered cases". The rest are
  CRLF, no stimulus (STOP), no crosshair (S1 dropped) and a missing mask-15 probe (STOP).
The previous folder (20261127-185925-2916249, revision 1, 36/36) was removed by its literal path.
