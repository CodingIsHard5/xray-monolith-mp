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

## Review of the three follow-up commits (independent Opus review, 2026-11-27) — for reading s4
- No compile/link finding; no behaviour change at mask 7, with sim_tick 0, or without -det_mode.
- s4 values 4-6 (where the item was drawn) are read after the render from the item's per-frame cache. When that cache
  points at the item's own XFORM (parent drawn at its XFORM, or body bit off), values 4-6 simply repeat values 1-3 as
  they are AFTER the frame, not as they were when drawn. Only when the item was drawn through its in-between matrix
  (body bit on, the probe's case) do they show the drawn position. If the cache was filled in an earlier pass of the
  frame, they show the position from before the nudge, which is still what every later pass of that frame drew.
- CInventoryItem::UpdateXForm (inventory_item.cpp:1292) has no per-frame guard; CWeapon and CMissile have one.
- Do not bisect with c253fafdb as an endpoint: on its own it has default mask 15.

## 9. The series script s4d1_run.sh (written 2026-11-28, desk only; sha256 af73868b8d5c32bbd1cd2569829ca54aad9be73a606c88618646694f126559ae after the review fix round 9.8; earlier 8dc70c49..., e06358a8...)
Made from s4d2/s4d2_run.sh (2777c68f..., four reviews). Brief: the DRAFT's test content wins (boots, tags, flags, order,
probe, stop / retry rules, pins); the TEMPLATE's safety rules win (shared box checks, one claim with a token, child
handling, guarded writes under run/, refusals). Not run: only `bash -n` and fixture `--preflight-only` calls (9.5).

### 9.1 Modes, boots, expected wall time (prereg 6.3 figures; [G] = its 15 ms per render-only frame guess)
| mode | boots (order) | expected | worst case by the bounds |
|---|---|---|---|
| `--preflight-only` | none: the shared box checks only | < 1 min | - |
| `probe [--preflight-only]` | s4d_probe (K3 mask 7), s4d_probe15 (K3 mask 15) | 2 x 375 + 2 x 40 = 830 s = ~14 min | watch threshold per boot 1900 + 900 = 2800 s; wall time per boot after it up to + 5 (poll) + 250 (TERM grace) + 120 (engines_gone) + ~150 (gates) = ~3325 s, so <= ~6650 s, plus the claim wait (<= 6 h) |
| `series [--preflight-only]` | A: s4d_s1 o1 s2 o2 s3 o3; L: l2a l3 l1 l2w l2b; S: off2a on2a off3 on3 off2b on2b brk1 [brk2] | 19 boots: 5897 s = ~98 min; 18 (no brk2): 5567 s = ~93 min | watch threshold per boot BENCH_TIMEOUT + 900 s; wall time per boot up to BENCH_TIMEOUT + ~1425 s (poll, TERM grace, engines_gone, gates); each slot may run twice (6.5) |
Series arithmetic: A = 1191 s [M, 6 boots] + 6 x 40 = 1431 s; L = 3 x 290 (l2a, l2b, l2w, K=2) + 375 (l3) + 211 (l1)
+ 5 x 40 = 1656 s; S = 6 x 290 (off2a, on2a, off2b, on2b, brk1, brk2) + 2 x 375 (off3, on3) + 8 x 40 = 2810 s;
1431 + 1656 + 2810 = 5897 s. Without brk2: 5897 - 290 - 40 = 5567 s. (The prereg rounds the same sums to 1 h 39 min.)
The probes' measured elapsed times replace the 15 ms guess before registration (6.3).

### 9.2 To install before the probe (the script only CHECKS it, by full sha256, and never installs it)
- The stimulus: from `/home/caden/Projects/StalkerGameEngine/harness/zz_eperf_synth.script` to
  `run/gamedata/scripts/zz_eperf_synth.script` (not there on 2026-11-28). A write under run/: by hand, while Caden is
  not playing. The bytes: harness/zz_eperf_synth.script as it is now, sha256 1777fb6865cc5b56c80f197f3f95a250104d6c90ce0b8b2a35422e3bed39ff95
  (= STIM_SHA in the script; decided 9.7). Until it is installed, `probe` and `series` refuse by name ("missing file").
- Already in place and pinned (read 2026-11-28): run/bin/SimTick18.exe 40d2d37d..., run/bin/Stance14.exe b4ce9c77...
  (series), out/user_det.ltx.orig 3e0e0a6f..., out/user.ltx.orig, tools/det_run.sh e2980094..., tools/lib_engine_pids.sh
  cb183b7b..., tools/run_noguard.sh 92729abc..., bench1.scop / .scoc, run/appdata/axr_options_play.ltx 27d54b9f...;
  run/appdata/user.ltx is byte-identical to out/user.ltx.orig.
- Before the series, by hand: PROBE_BOOT_S (copy the `PROBE_BOOT_S=<n>` line the probe run logs, also in
  out/s4d1_state/probe/probe_boot_s.txt; the series refuses a value that differs from the probe logs) and PROBE_S3_FRAMES in the script (the latter must equal the scorer's CONST
  value: the script reads it and refuses a mismatch), SCORER_SHA / SELFTEST_SHA (REG_ME_) at registration, and
  s4d1/probe_readings.txt ending in `S4D1 PROBE: GO`.

### 9.3 Open: need a decision (the script refuses rather than choose)
1. **Stimulus pin.** Prereg 1 pins harness/zz_eperf_synth.script at prefix d7dbc6c54cd02600. That file is now
   1777fb6865cc5b56c80f197f3f95a250104d6c90ce0b8b2a35422e3bed39ff95 (s4d2's `-det_synth_item` revision, 2026-11-27 22:15);
   no copy of the d7dbc6c5 bytes exists in the tree or in any worktree's git history (searched). Choices: (a) re-pin s4d1
   to 1777fb68 (no s4d1 boot passes `-det_synth_item`, so the item branch should stay off; a draft change, and both probes
   then run on it); (b) recover the d7dbc6c5 bytes. Note both series install to the SAME path, so s4d1 and s4d2 can only
   share the box without swaps if they use the same file.
2. **Re-probe.** 2.1 and 1 allow a re-run of both probes after a fix or a stimulus change, but do not register where the
   first probes' folders and reading go (the scorer always reads out/det_s4d_probe(15) and the reading is "written
   once"). No re-probe mode was written; `probe` refuses while any out/det_s4d_* folder or probe_readings.txt exists.
3. **Options-file restore** (template, not in the draft): kept, so part A's Stance14 / SimTick18 arms start from the same
   axr_options.ltx bytes (each exe rewrites its version line). Earlier part A runs (4b/4c, whose bounds A3/A4 copy) ran
   without it. Keep or drop?

### 9.4 Where I had to read the draft (draft-vs-template points)
| point | draft | template | script |
|---|---|---|---|
| retry | 6.5: rc != 0 "(timeout, early exit)" -> `<tag>_r1` once; a second failure of the SAME slot stops | one retry per series, rc 4 only | rc 4 or 5 without a crash dump, once per slot, series only (draft) |
| probe retry | 2.1: re-run only after a fix | none | none |
| in-run preflight | s4d_l2a "at cp1 ... stop at once" (s4c_partA.sh:38-41, during the boot) | after the boot | after l2a's boot (a TERM mid-boot leaves user.ltx with the det settings, N4-12, and blocks later boots); checks det_counters + det_alife_counts cp1, witness non-empty, command line, det_synth line `k 2 wallclock 0 tw 0 break 0 mask 0`; no token count (the template's 58/138 is s4d2 content) |
| in-run on the probe | not registered (the probe reader checks flags) | yes | no |
| exe pin | 2.3 "PIN_ME_EXE (series only)"; 1: lever boots only on that exe | before any boot | every mode (safety) |
| leftover folders | 6.4: no synth_s4d_*, no out/det_s4d_* except the two probe folders | per tag | the draft's globs (probe: none at all) |
| cfg0 | run/appdata/user.ltx (would refuse: no sim_tick line) | out/user_det.ltx.orig, missing = 0 | template form, plus 6.4's "has g_legs 1 and r__actor_shadow on" |
| user.ltx known file | change log caller rule | not in s4d2 | before start and every boot (N4-12, by name) |
| brk2 | only if S3 registered (P >= 300) | - | from PROBE_S3_FRAMES, cross-checked with the scorer's CONST |
| series timeout | 2.3 max(1900, ceil(5 P/60) x 60) | REG_ME constant | computed from PROBE_BOOT_S |
| scorer / self-test hashes | "recorded at registration" (2.5, 8) | REG_ME_ pins | REG_ME_ pins, series only |
| extra pins | not in the draft | pid lib, run_noguard, saves, options source | kept (safety / same inputs) |
| after-boot files | 6.4 silent | xray.log + six counter files + lever witness | kept ("missing files fail by name") |
| item line | - | s4d2 only | dropped |
| N4-8 | det_run refuses 5 variables | not in s4d2 | refused first, before anything |
| N4-11 | - | no per-boot cap | watch cap BENCH_TIMEOUT + 900 s (fault stop) |
| N4-9 | - | token substring | token last in the claim text; the shared file's substring match is unchanged (contrived residue: same mode, same second, pid prefix) |

### 9.5 Bounds in the script (each derivation is in a comment next to it)
| bound | value | derivation / flag |
|---|---|---|
| probe BENCH_TIMEOUT | 1900 s | prereg 6.2/6.3: K=3 guess 375 s -> 5.1x |
| series BENCH_TIMEOUT | max(1900, ceil(5 PROBE_BOOT_S/60) x 60) | prereg 2.3; >= 5x for K=3 S runs, more for the shorter arms |
| CHILD_GRACE_S | 250 s | det_run cleanup after TERM <= 165 s worst -> 1.5x (template 180, the caller-rule minimum) |
| WATCH_EXTRA_S | 900 s | det_run time outside LIMIT <= ~679 s worst -> 1.3x (new, N4-11) |
| CLAIM_MIN | ceil((BENCH_TIMEOUT + 1420)/60) min | renewal-to-renewal sum (probe: 56 min) |
| WAIT_MAX_S | 6 h | **COPIED** from s4d2 (S2-7); no s4d1 quantity sets it |
| POLL_S | 60 s | start lags a cleared box <= ~1.7 min |
| display wait | 10 x 2 s | Xvfb socket gone within ~1 s [G] -> ~20x |
| ENG_GONE_S | 60 s | det_run.sh's own stop_wine wait (det_run.sh:156) |
| watch interval | 5 s | det_run.sh's inboot rate (det_run.sh:244) |

### 9.6 What was run (2026-11-28 01:2x, EXCLUSIVE held by another worker: each call < 1 s, nice 19, timeout 30)
`bash -n` OK. common/selftest-runs/20261128-012128-3674898/fx/struct.awk (read-only use) over the script: no unguarded
write under run/. Fixture calls (BOX_* pointed at a scratchpad fixture, real tree read only): `--preflight-only` rc 0;
missing S4D1_BOX_LIB rc 3; WINESERVER set rc 2; BENCH_DISPLAY=:0 rc 2; `probe --preflight-only` rc 2 "STIM_SHA is still
the placeholder"; `series --preflight-only` rc 2 naming STIM_SHA, PROBE_BOOT_S, PROBE_S3_FRAMES, SCORER_SHA,
SELFTEST_SHA; with the stimulus pin filled in a copy: probe rc 2 only "missing file
run/gamedata/scripts/zz_eperf_synth.script"; a PROBE_S3_FRAMES differing from the scorer refused by name; with every
value filled in a copy (stimulus path pointed at harness/, a fake GO reading): series rc 0, 19 boots (P=812) / 18 (P=299),
timeout 2040 s for PROBE_BOOT_S 401. Not tested: any real mode, the claim, det_run.sh calls, watch_boot, the retry, the
witness moves, finish(). s4d1_selftest.sh was not re-run (scorer and self-test unchanged). HEAD of wt-render-split is
5a7ca117d (one self-test-only commit after ac7a8a859).

### 9.7 Coordinator's decisions (2026-11-28), applied
1. **Stimulus re-pinned** to harness/zz_eperf_synth.script as it is now, 1777fb6865cc5b56c80f197f3f95a250104d6c90ce0b8b2a35422e3bed39ff95
   (verified with sha256sum). **The old d7dbc6c5 bytes are gone**: "a superset of the old file" rests on reading all
   104 lines of the new one and s4d2's change note, not on a diff. Without `-det_synth_item`, item_on (:20) is false, the
   item block (:68-79) never runs, and nothing else reads it except the `installed` line, which now ends `, item false)`
   (:102); the scorer matches only the substring `det_synth stimulus: installed` (s4d1_score.py:206). **s4d1 and s4d2
   share the one installed path, run/gamedata/scripts/zz_eperf_synth.script, with this one hash.** The script refuses
   any s4d1 boot whose flags contain `-det_synth_item` (NOITEM). 9.2's install step now has a definite file: copy
   harness/zz_eperf_synth.script (1777fb68...) to run/gamedata/scripts/ by hand; the script only checks it.
2. **No re-probe mode.** A re-probe needs a new decision from the supervisor and a registered place for the old probe
   folders. `probe` refuses while any out/det_s4d_* folder (or the probe reading) exists: checked on a fixture copy.
3. **Options-file restore kept** (template safety rule); one line in prereg 6.2 says so and why.
Fixture checks re-run 2026-11-28 (sub-second, nice 19, EXCLUSIVE held elsewhere): box-only rc 0; missing lib rc 3;
WINESERVER rc 2; :0 rc 2; `probe --preflight-only` rc 2, ONLY "missing file run/gamedata/scripts/zz_eperf_synth.script";
`series --preflight-only` rc 2 naming PROBE_BOOT_S, PROBE_S3_FRAMES, SCORER_SHA, SELFTEST_SHA; with the stimulus
path pointed at harness/: probe rc 0 (2 boots, 1900 s); a probe folder present: rc 2 by name; `-det_synth_item` added
to a lever: rc 2 by name; PROBE_S3_FRAMES differing from the scorer: refused by name; every value filled: series rc 0,
19 boots (P=812) / 18 (P=299), timeout 2040 s.

### 9.8 Fix round after plan-facts/s4d1-driver-review-2026-11-28.md (and its automated findings), 2026-11-28
s4d1_run.sh is now sha256 af73868b8d5c32bbd1cd2569829ca54aad9be73a606c88618646694f126559ae; s4d2_run.sh got the same fixes (s4d2/NOTES.md 10).
- **S-1 (claims left live, blocking Caden's launcher):** choice (i). quiet-required.sh has a `renew <w> <min>` verb that,
  with QR_CLAIM_ID=<id>, extends exactly that record when several are nested and, with one record, rewrites the slot
  keeping the id (renew_own, recs_fresh). So the driver claims ONCE (acquire) and before every boot runs `renew` with
  the acquire claim's id, never `claim` again; finish() releases EVERY distinct claim id in claim_calls.txt (normally
  one). Before each renewal box_gate has checked that a live record carries our token and no other engineperf record is
  live, so the renewal touches only ours. A claim period is boot-sized (probe 56 min) and refused above 180 min.
- **S-2:** a lost claim stops: box_claim_has_token in box_gate (before every boot) and in watch_boot (every 5 s).
- **S-3 / (c):** engines_gone right after det_run.sh returns, any rc, before the retry decision; an engine still there
  60 s after `wineserver -k` is a fault stop (claim kept). A late-stopped engine no longer costs a slot's retry.
- **N-1 / (e):** TERM/INT/HUP held from just before the launch until BP is recorded, then acted on.
- **N-5 / :456:** the 6-h deadline is checked before every claim attempt, also right after the box checks; sleeps are
  cut to the time left.
- **N-7 / :337:** the series refuses unless out/det_s4d_probe and out/det_s4d_probe15 exist.
- **N-4 / PROBE_BOOT_S:** the scorer is not changed; the driver logs `PROBE_BOOT_S=<n>` at the end of `probe` (the
  larger `rc=0 elapsed=` of the two run logs, also written to out/s4d1_state/probe/probe_boot_s.txt), and the series
  refuses a PROBE_BOOT_S that differs from those logs (or the logs missing).
- **N-3:** the end of `probe` logs the sha256 of s4d1_score.py and s4d1_selftest.sh to read with.
- **S-4 / S-5 / minor docs:** draft 6.2 BENCH_TIMEOUT per mode and det_run.sh:259-266; 6.3 the probe's 5.1x is [G],
  desk bound 3.0x (N-2); 6.4 lost claim and engine check, the l2a check after the boot with the reason; 6.5 rc 4/5, no
  probe retry; 7 the operational bounds under 2x flagged; 2.1 the re-probe must also move out/s4d1_state/probe (the
  script refuses an existing state folder); NOTES 9.1 threshold vs wall time; 9.2 stimulus text.
- **Not fixed (NOTE, listed):** N-6 `wineserver -k` in run/prefix stops any Wine process there, not only ours (the
  documented exception; engine_running / own_claim make a foreign one unlikely). N-8 the options restore copies Caden's
  play options into the bench file (his launcher restores its own copy; a change by him stops the run by AXR_SHA).
  Residue of S-1: if our record expired in the instant between box_gate and renew, `renew` with an unmatched QR_CLAIM_ID
  extends every live engineperf record (renew_own fallback); box_gate's own_claim test makes any other one unlikely.
- Fixture checks re-run (9.6 form, all sub-second): every earlier result unchanged, plus: series without the probe
  folders refused by name; PROBE_BOOT_S 400 vs logs 401 refused by name; with probe folders and logs present, series
  rc 0 (19 / 18 boots). Not tested: renew, release-all, the lost-claim stop, engines_gone after a boot (no real claim
  or engine in a fixture call).
