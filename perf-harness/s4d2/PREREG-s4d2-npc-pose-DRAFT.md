# PREREG s4d2: stage 4d-2 build 2 (NPC and mutant poses drawn between ticks, S4a, with items on bones following the drawn pose, S4b) under the synthetic gated lever (DRAFT 2026-11-27, NOT registered)

DRAFT. Nothing here has been run. It is not registered until every item of section 1.4 is done: the exe is built
with the witness addition W1 and pinned, the probe (section 2) has said GO, every `PROBE_` value is filled in by the
rules in 2.3, and the self-test has passed on the probe's real bytes (section 8). No box run without the Overseer's GO.

**Code:** wt-simtick perf/tick-4d HEAD `c216081fd`, on top of build 1 (8527808c9..6acb77044):
- S4a: 0dfdff031 (capture, draw, mark, witness p/pw/up/pn, tripwire `pose`, break 16) and 0a8e94094 (review fixes:
  players' bodies unmarked, unchanged pose drawn as cur).
- S4b: f2d97ac4f (record, correction, witness `pa`, tripwire `attach`, break 32), f5b23fd7e (the record holds the bones
  the placement read) and c216081fd (re-review S1 and S2 fixes).

Never compiled; the first CI build is pending. S4a and S4b are registered as ONE player-facing step: both sit under
mask bit 16, and the S4a design says S4a must not reach players without S4b (design D3).
**c216081fd is getting its own independent review before this registration is final.** A code change from that review
means a new exe, a new pin and a new probe.

Design documents (plan-facts/):
- stage4d2-npc-animation-design-2026-11-27.md ("the design"); its section 12 overrides its body.
- stage4d2-s4a-code-notes-2026-11-27.md ("S4a notes").
- stage4d2-s4b-design-and-code-2026-11-27.md ("S4b notes"); its section 11 is current.
- Reviews: stage4d2-s4a-review-, stage4d2-s4b-review- and stage4d2-s4b-rereview-2026-11-27.md.

Model copied for form, flags and method: s4d1/PREREG-s4d1-leftovers-build1-DRAFT.md ("the s4d1 draft") and its
scorer.

Labels:
- **[D]** desk (file:line read at c216081fd);
- **[M]** measured (a number in a file);
- **[G]** guess;
- **COPIED** (a bound taken from an earlier registration without re-deriving it; listed in the margin table).

## 0. Limits, first
- **The det harness is not exact for online state or for `::Random`**, even stock against stock (the s4d1 draft,
  section 0, [M]). So the exact gates here are only these three:
  - the offline counters C (counters_diff.py rules);
  - the in-run tripwires;
  - witness relations that hold inside one frame of one run.

  Online state, `::Random`, Lua state, the NPC's bone matrices (`pn` hsim) and the item's XFORM across runs are
  reported, never gated.
- **Synthetic, not real.** The lever (`-det_synth_k K`: one 33 ms det tick, then K-1 render-only frames, alpha = j/K)
  does not show any of the following; those are Caden's eyes:
  - real-time races. In det serial mode CalculateBonesThread and the game thread run inline before the render, so
    the S4a/S4b "capture lands after the first evaluation" windows (S4a notes 5.4; S4b review O1) never open;
  - uneven alpha, or multi-tick frames (where S4a draws stock, S4a review);
  - soft skinning (R1);
  - whether a blended NPC or its weapon LOOKS right.
- **Serial det mode, first 320 A-Life passes** (about 5440 ticks [M, s4d1 draft]), bench1 save, headless Xvfb
  1280x720, mt_* off. Legs and the actor shadow are on in the det profile (s4d1 NOTES 1). Neither matters here: the
  actor is unmarked (S4b notes 7), so the player's own body is drawn as in build 1.
- **What a skeleton draw looks like headless is not known.** Not known from the desk:
  - how many NPCs are drawn and in which passes;
  - whether they use a skeleton LOD;
  - whether the witness NPC is in view and armed;
  - whether a render-only-frame recalculation of an armed NPC is byte-equal (F5).

  The probe settles each of these BEFORE registration (section 2). Each probe answer has a registered plan.
- **Positions are printed with 5 decimals** (scored to 1e-4 m per coordinate); `u` and alpha with 6 (scored to
  1.5e-6); hashes are FNV-1a 64 as 16 hex digits (scored by string equality).
- **No real fixtures yet.** The scorer and self-test are built on synthetic run folders made from the format in the
  code at c216081fd, plus the required addition W1 (1.3).
- **Build 1's open question (the held item's nudge, its s4 column) is not re-tested.** s4 is parsed for format only.

## 1. What the build changes, the witness, the exes
- **Bit 16 (`SIM4D_POSE`) of `sim_4d_mask`.** The default mask stays 7 (device.cpp:491), so the bit is off.
  - **S4a.** Every skeleton xrGame MARKED as the visual of a simulated object (CGameObject::OnChangeVisual, except
    the actor) is captured at the end of CalculateBones, stamped (tick, epoch) (sim_pose.cpp:277-347). On a gated
    frame it is drawn as a per-bone joint-pivot nlerp of the last two ticks' `mRenderTransform` at u = alpha
    (sim_pose.cpp:229-259, 400-432), through the Layers upload hook (SkeletonX.cpp:63, 141).
  - **Unmarked skeletons are stock, always.** This is the opt-in rule, design 12 C1: HUD models, legs, gammavr's VR
    models, anything from a bare model_Create.
  - **S4b.** An item placed from a marked parent's bones (CWeapon, CMissile, CArtefact, CInventoryItem,
    AttachmentCallback) leaves a render-only record (GameObject.h:216-257). The record holds the bones the placement
    READ, the parent XFORM and the XFORM it produced. The 4b child rule then draws the item as the same placement
    redone from the drawn bones: `draw = parent_draw * C * inverse(recorded parent) * XFORM()`
    (GameObject.cpp:879-975, 1066-1133).
  - Script attachments get the same correction.
- **Simulation-read fields written:** none by design. That includes `mTransform`, `mTransformHidden`,
  `mRenderTransform`, `UCalc_Time` and the item's XFORM(). The only writes are SimPoseState, the record, and
  `mRenderTransform_temp` (render-only by grep).
- **Layout changes in every arm of the same exe** (S4b notes 11.1; re-review N3). They are present with sim_tick 0 too:
  - IKinematics +1 pointer (Kinematics.h:130), CKinematics +1 bool;
  - CGameObject +~280 B (the record, GameObject.h:257).
- **Heap use in the ON arm only:** ~21 KB per captured skeleton (`xr_new<SimPoseState>`, sim_pose.cpp:292), section
  10.1.
- **Test-only, inert without `-det_mode`:**

  | switch | what it does |
  |---|---|
  | `-det_synth_pose` | the pose witness columns, whole-token match (sim_pose.cpp:502-529) |
  | `-det_synth_pose_npc <name>` | the witness NPC |
  | `-det_synth_tw` | the tripwires `pose` (bracketing every capture and draw evaluation, sim_pose.cpp:349-361, 434-448) and `attach` (bracketing the child rule's correction, GameObject.cpp:1084-1127), besides build 1's `s2`, `s3` |
  | `-sim_4d_break 16` | after a form-L evaluation, the drawn pose is written into the live `mRenderTransform`, sim_pose.cpp:428-430 |
  | `-sim_4d_break 32` | after a corrected placement, the draw matrix is written into the item's XFORM(), GameObject.cpp:1119-1120 |

  The ranges are mask 0..31 (device.cpp:629) and break 0..63 (device.cpp:645). With them come build 1's
  `-det_synth_k`, `-det_synth_wallclock`, `-det_synth_walk` and `-det_synth_tracer`.

### 1.1 The witness line, as the code prints it (c216081fd)
With `-det_synth_pose`, sim_pose_witness_print (sim_pose.cpp:630-740) appends five columns to build 1's 58-token line
(device.cpp:546-576), before ` tw N`. The line becomes:

`F .. T .. j .. K .. n .. a .. dt .. hud .. | s2 (9) | s3 (11) | s1 (5) | s4 (6) | p <neval> <nlerp> <nguard>
<nsnap> <nro_diff> <nro_equal> | pw <form> <u> <ptick> <ctick> <hd> <hl> <hp> <hc> <hu> <hdu> <jd xyz> <jp xyz>
<jc xyz> | up <hu> <hl> | pn <form> <u> <hd> <hl> <hsim> | pa <kind> <form> <cls> <ba> <bb> <u> <wd xyz> <ws xyz>
<hd xyz> <hr xyz> <ld xyz> <ls xyz> <hx> <hxe> | tw <n>`

That is **126 tokens**: p 6, pw 19, up 2, pn 5, pa 26 values. The absent forms were counted mechanically from the
print strings. With W1 (1.3) it is **132 tokens**: `| uu <nu> <nud> <nus> <nposed> |` sits before `tw`.

**`p`** holds per-frame counts, reset each frame:
- skeletons evaluated;
- of them L (blended), Sg (guard) and Ss (snap);
- render-only-frame recalculations whose bytes differ from cur (history dropped) and that are byte-equal (no-op).
  These last two are the F5 counters; they are written only while captures run, so they are 0 with the bit off.

**`pw`** is the first skeleton evaluated this frame, of any marked kind. Which skeleton that is depends on exe and
switches (S4b O2), so pw is compared only line by line.
- 19 dashes: nothing evaluated. The scorer checks that pw is present exactly when neval > 0.
- Form L: everything present, except `hu` (`-` if that skeleton's first skinned leaf was not uploaded this frame) and
  `hdu` (`-` if hu is absent).
- Forms Sn, Sb, Sg, Ss: u, hd, hdu and the 9 joint values are `-`; ptick, ctick, hp and hc are `-` without a capture;
  hl is always present.
- `hd`, `hl`, `hp` and `hc` hash all n bones of draw, live, prev and cur. `hu` hashes the `count` matrices handed to
  `set_ca` for the first skinned leaf; `hdu` hashes the draw array over the same count. **hu is compared with hdu,
  never with hd** (S4a notes 5.9).
- The joints are those of `bip01_r_hand`, else the last bone, in model space.

**`up`** is the first skinned leaf uploaded this frame, ANY skeleton, marked or not: the matrices handed over and the
live ones. Stock draws give `hu == hl` by construction (SkeletonX.cpp:141; `LL_GetTransform_R` =
`mRenderTransform`, SkeletonCustom.h:309). up does not name its skeleton.

**`pn`** is the named NPC, looked up every frame:
- 5 dashes: not found, or no skeleton;
- form `X`: found but not evaluated;
- `hsim` hashes `mTransform`.

**`pa`** is the witness NPC's ACTIVE item at the first placement of the frame (GameObject.cpp:978-1033). It is
filled with bit 16 off too: the child rule runs on every gated frame.

| field | values |
|---|---|
| kind | `-` (no record), B, T, W, N, X (record stale) |
| form | C, R (corrected), O, P, S |
| cls | c, p, o, `-` |
| ba, bb | the record's bones; `-` = none |
| u, hd, hr | `-` unless the parent's pose is drawn L this frame and ba is a captured bone |
| ld, ls | only on C or R |
| wd, ws, hx | always present |
| hxe | `-` if the child is not found after the join |

All 26 values are dashes when no placement of that item reached the child rule this frame.

**Any other shape of any column makes that run's witness malformed.** Every check that reads it then FAILS by name.

**Engine log lines read:**
- `command line ...`;
- `* det_synth: k K wallclock W tw T break B mask M`;
- `* det_synth: pose witness on, npc <name>` (sim_pose.cpp:528);
- `* det_rng_state <cp>: ... frame=<f> time_ms=<ms> tick=<T>`;
- `* tw <s2|s3|pose|attach> BAD <frame>` (device.cpp:509-513);
- the stimulus lines of zz_eperf_synth.

### 1.2 Exes
- **Build 2.** Lever boots run only on an exe from the perf-build workflow: XRCORE_PROFILER_AGG, which makes det mode
  serial (DET_SERIAL, device.cpp:380-384), and all four tripwires assume it (S4b notes 11.4 T1). It is installed as
  `run/bin/SimTick19.exe` (name proposed), sha256 **PIN_ME_EXE**. It must contain c216081fd and W1.
- **Build 1** (for LB, 4.2) is s4d1's `run/bin/SimTick18.exe`, sha256 **PIN_ME_EXE_B1** (s4d1's PIN_ME_EXE), built
  from 7b1232ffc or 6acb77044 (comments only in between).
  - It does not parse `-det_synth_pose` or `-det_synth_pose_npc`. [D] `git grep det_synth 6acb77044 -- src` shows
    only `-det_synth_k`, `-det_synth_wallclock` and `-det_synth_tw`, and the stimulus script matches only
    `-det_synth_walk` and `-det_synth_tracer` (zz_eperf_synth.script:13-14).
  - So it gets the identical command line and prints 58-token lines and no pose log line.
- **Stock reference for part A:** `run/bin/Stance14.exe` b4ce9c77 (COPIED).
- **Stimulus:** `harness/zz_eperf_synth.script`, pinned as in the s4d1 draft (its R3: installed before the probe;
  inert without its flags).

### 1.3 Witness gap W1: an unmarked skeleton cannot be isolated (REQUIRED code change before registration)
The opt-in check U1 needs to know whether a skeleton NOT marked (the stand-in for gammavr's self-posed VR models) is
drawn byte-stock with bit 16 on, while it is posed between ticks. The witness at c216081fd cannot show this:
- `pw`, `pn`, `pa` and the `p` counts see only skeletons with storage, and storage exists only for marked ones;
- `up` is "the first skinned leaf uploaded, any skeleton". It names neither the skeleton nor its mark, and it is
  `hu == hl` by construction whenever the upload used the live bones.

So no column can tell an unmarked skeleton drawn stock from a marked one, and no column shows that an unmarked one was
posed between ticks. **Required addition, test-only, under `g_sim_synth_pose`, no engine behaviour change; not made
here:**
- **sim_pose.cpp `sim_pose_wit_upload`.** For every skinned-leaf upload of a skeleton with
  `!K->LL_GetSimObjectVisual()`:
  - hash the `count` matrices handed over and the live `mRenderTransform` over the same bones, as `up` does;
  - at the leaf's last matrix: `nu++`; `nud++` if the two hashes differ; `nus++` if `K->has_sim_pose()`;
  - at the first leaf of that skeleton in this frame: look up its live hash at its previous upload frame in a
    test-only map keyed by K and its bone count, `nposed++` if it differs, then store this one. The map lives on the
    main thread only.
- **Reset** per frame with the other counts (sim_pose_witness_reset).
- **Print** ` uu %u %u %u %u |` after `pa`, before ` tw` (sim_pose_witness_print).

The cost is det-only hashing. A reused K address after a free can only inflate `nposed`, which is a count floor, not
a pass condition (named in NOTES.md).

Without W1:
- the scorer reads 126-token lines and U1 FAILS by name ("required witness addition W1");
- the probe STOPs (`w1`).

### 1.4 Required before registration
1. W1 (1.3) in the code and reviewed. It is test-only, but it adds to sim_pose.cpp.
2. **c216081fd's independent review**, and the W1 review, done, and any finding fixed. Any code change means a new CI
   build, a new pin and a new probe.
3. A CI perf-build of the result, installed as SimTick19.exe and pinned (PIN_ME_EXE).
4. s4d1's build-1 exe built and pinned (PIN_ME_EXE_B1). LB cannot run without it.
5. The stimulus script installed with its pinned hash (s4d1 R3).
6. **The series script `s4d2_run.sh` (not written, R2 of 6.2).** It must move `run/appdata/logs/synth_<tag>.txt` into
   `out/det_<tag>/` after every boot. tools/det_run.sh does not: its lines 44-47 move dumps, traces, counters and
   saves only (s4d1 NOTES 2.13, re-read today).
7. The probe GO (2.2), the `PROBE_` constants filled into the scorer's CONST, and BENCH_TIMEOUT set from PROBE_BOOT_S.
8. The self-test re-run with a case built on the probe's real folder (CRLF, real hashes, real `pa` forms), PASS. The
   hashes of the scorer and the self-test recorded in section 8.
9. Recommended before the probe: s4d1's R1 (the actor holds an active item headless, s4d1 NOTES 4). The first-person
   HUD hands are the only unmarked skeleton the desk expects to be posed between ticks (4c's HUD clock). Without an
   item they are not drawn, and the probe would STOP on `optin` [G, 2.4].
10. The Overseer's GO.

## 2. Probe (one boot, unscored, FIRST, before registration)
### 2.1 The run
`s4p_probe` runs SimTick19.exe with K = 3, mask 23 (7 | 16), the walk and tracer stimulus, tripwires on and no break:

`DET_EXTRA="-det_passes 100,200,320 -det_lua_streams -det_counters -det_synth_k 3 -sim_4d_mask 23 -det_synth_tw
-det_synth_walk -det_synth_tracer -det_synth_pose -det_synth_pose_npc sim_default_duty_428175"`

The NPC is the walking Duty squad member, 60/32/6 m from the player at cp1/2/3 (design 7.1 [M]).

Reader: `python3 -I s4d2_score.py mode=probe root=out out=s4d2/probe_readings.txt`. It is written once and exits
0 GO, 1 STOP.

**One re-probe is allowed, only on the stop rules `npc` or `attach`.** The re-probe is `s4p_probe_b`, with
`-det_synth_pose_npc esc_2_12_stalker_fanat`: the nearest NPC, 4 m away at cp1 [M]; whether it is armed is [G]. Its
folder is read as `det_s4p_probe` by renaming the first probe to `det_s4p_probe_a`. A re-probe after a code or
stimulus fix that a stop rule asks for is allowed as in s4d1.

### 2.2 Probe stop rules (any one = the series does not start) and the plan for each outcome
| code | rule | why | plan if it stops |
|---|---|---|---|
| flags | engine command line exactly the probe switches; `k 3 wallclock 0 tw 1 break 0 mask 23`; pose line names the NPC | the run is the registered one | fix the command, re-probe |
| pattern | build 1's lever pattern on every line; cp1-3 on witnessed tick frames | the lever works on this exe | stop: harness |
| input | probe folder, log and witness present; every line well-formed | | fix the series script (R2), re-probe |
| w1 | every line has the `uu` column | U1 needs it (1.3) | land W1, rebuild, re-probe |
| stimulus, walk | as s4d1 (installed, start, >= 50 bullets, no script error; walk >= 1.5 m) | the stimulus is the same in all arms | as s4d1 |
| tripwire | 0 `* tw` lines | the slices write no simulation field | stop: a Rule One finding for the code, not the harness |
| pose | PROBE_P_LERP >= 300 and PROBE_P_TICK >= 300 | (a) skeletons are drawn AND blended headless | design D9: a camera-heading lever (`set_actor_direction` toward the Duty squad), or "Caden's eyes only": an Overseer decision; the series does not start |
| uploads | PROBE_UPLOADS >= 300 | P4 has a subject | same as pose |
| f5 | byte-different render-only recalcs <= 50 % of all render-only recalcs (p columns 5, 6 on n=0 frames) | (b) F5: an armed NPC whose render-only recalc is not byte-equal loses its history every frame, so it is drawn stock and its item never corrected | design D6: fix F5 in xrGame (no `CalculateBones_Invalidate` from a render-time UpdateXForm), rebuild, re-probe. 0 < share <= 50 % is reported, not a stop |
| npc | `pn` present on >= 50 % of lines | (c) the witness NPC exists and is online | the one re-probe with the fallback NPC; then the Overseer |
| attach | PROBE_PA_CR >= 300 (n=0 frames with pa form C or R and wd != ws) | (c) the NPC holds an item and the S4b correction moves it | the same re-probe; if it still fails, S4b would be unverified while being half of the step, so the Overseer decides (recommendation: a stimulus that arms a named NPC, not dropping S4b) |
| ld | <= 2 ld clusters per (kind, ba, bb) group | the record's bones are those the placement used (the outcome PA2 gates) | back to the code (re-review S2 class); not a harness issue |
| hx | hx == hxe on every pa line | the item's XFORM is not changed after the child rule in a stock frame; otherwise PA1's hx rule cannot be used | the Overseer: drop that rule or find the writer |
| optin | PROBE_U_POSED >= 300 (n=0 frames with an unmarked skeleton posed between ticks) | U1 has a subject: a stand-in for a self-posed model on screen | land s4d1 R1 (active item, so the HUD hands are drawn), re-probe; else the Overseer |

**(d) Build 1's s4 question is not asked here.**

### 2.3 Placeholders the probe supplies (rule from reading to registered bound). Kp = 3
| placeholder | probe reading | registered bound |
|---|---|---|
| `PROBE_P_LERP` | n=0 frames with pw form L and hd != hl | P1 minimum at K = floor(P x (K-1)/(Kp-1)/3) |
| `PROBE_P_TICK` | n=1 frames with pw form L and hp != hc | P3 minimum = floor(P/3) at every K (one tick frame per tick, ~5440 ticks at every K); TWB16 minimum = floor(P/3) |
| `PROBE_UPLOADS` | frames with an `up` column | P4 minimum = floor(P x K/Kp/3) |
| `PROBE_PA_CR` | n=0 frames with pa form C/R and wd != ws | PA2 minimum = floor(P x (K-1)/(Kp-1)/3); TWB32 minimum at K = 2 = floor(P/2/3) |
| `PROBE_PA_CLUSTERS` | max ld leader-clusters (radius TOL_LD) per (kind, ba, bb) group | PA2: no group may show more |
| `PROBE_JOINT_LERP` | 1 if every form-L line has jd on lerp(jp, jc, u), else 0 | 1: P2's joint rule is strict. 0: a joint drawn as cur also passes (a bone the blend left at cur: hidden, mirrored, skewed; sim_pose.cpp:252-257), so a known per-bone fallback cannot fail P2 predictably |
| `PROBE_U_POSED` | n=0 frames with uu nposed > 0 | U1 minimum = floor(P x (K-1)/(Kp-1)/3) |
| `PROBE_NPC` | the NPC named in the probe's pose line | F0: every pose run must name it |
| `PROBE_BOOT_S` (series only) | the probe's det_run.sh `elapsed=` | BENCH_TIMEOUT = max(2000, ceil(5 x PROBE_BOOT_S / 60) x 60) |
| `PIN_ME_EXE`, `PIN_ME_EXE_B1` (series only) | sha256 of SimTick19 / SimTick18 | the series refuses other hashes |

- The scorer REFUSES to score while any CONST value is still its placeholder string.
- **Why "/3":** every count minimum is a third of what the probe, scaled to the run's K, says to expect. That is a 3x
  margin by construction.
- **Why the scaling is exact in ticks:** the walk and the NPCs' animation are functions of simulated time, every K
  runs the same ~5440 ticks to cp3, and only the number of render-only frames per tick changes (s4d1 draft 2.3).
- **Reported by the reader, not bounds:**
  - for (a): skeletons evaluated per frame, and L, guard and snap counts;
  - for (b): the F5 counts;
  - for (c): the pa kind, form and cls histograms;
  - the joint fallback count, and the hx == hxe count.

### 2.4 Predictions for the probe (fixed before it runs)
| question | prediction |
|---|---|
| (a) skeletons drawn and blended headless (`pose`) | 80 % [G: six NPCs within 46 m of the player at cp1, idle animations change every tick; the camera heading is unknown] |
| (b) F5 recalcs byte-equal (share different <= 50 %) | 75 % [G: a render-only recalc runs UpdateTracks with dt = 0; IK ray picks and callbacks could still differ] |
| (c) NPC found (`npc`) | 85 % |
| (c) NPC armed and corrected on >= 300 n=0 frames (`attach`) | 50 % on the first probe; 65 % with the re-probe [G: it must be in view or in a shadow pass for its item's renderable_Render to run] |
| ld one cluster per group | 75 %; <= 2 clusters 85 % |
| hx stable | 85 % |
| `optin`, without s4d1 R1 | 30 % |
| `optin`, with R1 | 70 % |
| **GO on the first probe** | **about 10 %**. The likely stops are `optin` and `attach`, both with registered plans |
| GO after the allowed re-probe and R1 | about 30 % |

### 2.5 After the probe
1. Fill CONST and the placeholders here.
2. Build a self-test case from the probe's real folder (its real witness, log and counters, with mutants made from
   them) and run the self-test.
3. Register: rename without -DRAFT and record the hashes of the scorer and the self-test.
4. Run the series.

## 3. Part A: off = stock with sim_tick 0 (6 boots; method of the s4d1 draft section 3, COPIED)
Tags alternate: s4p_s1 (Stance14), s4p_o1 (SimTick19), s4p_s2, s4p_o2, s4p_s3, s4p_o3. No lever, mask or stimulus
switch: `DET_EXTRA="-det_passes 100,200,320 -det_lua_streams -det_counters"`. Build 2's code that runs with sim_tick 0
(the unconditional mark writes, GameObject.cpp OnChangeVisual; the layout) is judged here.

| check | PASS when | bound and derivation |
|---|---|---|
| A1 stock agrees (harness) | C = 0 at cp1, 2, 3 for s4p_s2 and s4p_s3 against s4p_s1 | exact; held in every part A so far [M] |
| A2 off agrees | C = 0 at 1, 2, 3 for each s4p_o* against s4p_s1 | exact. This is a cross-build comparison WITH layout changes (IKinematics +8 B, CKinematics +1 B, CGameObject +~280 B); earlier part A runs across layout-changing builds (4b SimRenderState, 4c) held C = 0 [M per the s4d1 draft] |
| A3 no extra pass (frame) | \|mean cp1 frame of off - of stock\| <= 8 | COPIED (4b R3) |
| A4 no extra pass (spacing) | every run: >= 50 A-Life calls to cp1, first call on the same frame, gaps 16/17/18, exactly one 16 | COPIED (4b R4) |
| A5 lever inert | the off runs log no `det_synth:` line (also no pose line), write no witness file, and carry no `tick=` | exact (device.cpp: the lever needs -det_mode and -det_synth_k) |

A1 FAIL means the harness is not usable today: A2-A4 are NO VERDICT.

## 4. Part S: the slice under the lever (10 boots)
There is no part L: build 1's part L validated the lever. Here the pattern is re-checked on every part-S witness
(PAT), and S0 re-checks harness use on this exe.

Every part-S run carries the s4d1 part-S flags plus build 2's two switches:

`<base> -det_synth_k K -sim_4d_mask M -det_synth_tw -det_synth_walk -det_synth_tracer -det_synth_pose
-det_synth_pose_npc PROBE_NPC`, with `<base>` = `-det_passes 100,200,320 -det_lua_streams -det_counters`.

| tag | exe | K | mask | extra |
|---|---|---|---|---|
| s4p_off2a, s4p_off2b | SimTick19 | 2 | 7 | |
| s4p_on2a, s4p_on2b | SimTick19 | 2 | 23 | |
| s4p_off3 / s4p_on3 | SimTick19 | 3 | 7 / 23 | |
| s4p_b1off2 | **SimTick18 (build 1)** | 2 | 7 | (the two pose switches are inert there, 1.2) |
| s4p_brk16 | SimTick19 | 2 | 23 | `-sim_4d_break 16` (must-fail) |
| s4p_brk32 | SimTick19 | 2 | 23 | `-sim_4d_break 32` (must-fail) |
| s4p_wc2 | SimTick19 | 2 | 23 | `-det_synth_wallclock` (must-fail) |

"On runs" are on2a, on2b and on3. "Off runs" are off2a, off2b and off3. The mask is always passed explicitly.

### 4.1 Gated checks
| check | PASS when | bound and derivation |
|---|---|---|
| F0 flags | every run (A and S): the engine's own command line carries exactly its registered switches; its det_synth line is `(K, wc, tw, brk, mask)`; build-2 part-S runs log `pose witness on, npc PROBE_NPC`; b1off2 and part A log none | exact |
| PAT lever pattern | every part-S run except wc2: s4d1's L1 rules (K, n iff j = 0, F +1, j cycles, T +1 on tick frames, a = j/K, dt = float(0.033f/K), hud) on every line; cp1-3 on witnessed tick frames with T = `tick=` | exact by construction (device.cpp sim_tick_plan, sim_view_present); print tolerances x3 / x4 (same printf formats, device.cpp:546: re-derived, not only copied) |
| PWC must-fail (wallclock) | wc2: the pattern part holds and its F T j K n columns equal on2a's to the lower cp3 frame; a != j/K on >= 90 % of lines; on >= 30 form-L pw lines, u = a on EVERY one and u != j/K on >= 90 % | u = clamp(g_sim_render_alpha) (sim_pose.cpp:416-417) and the witness a is the same alpha (device.cpp:799-804). With the real clock alpha equals j/K to 6 decimals with chance ~1e-6 per frame [D, s4d1 L1W], so a pose that does not follow the lever's alpha, or a P2 u-rule that cannot see it, makes this fail. Expected ~100 % |
| S0 harness (harness) | off2a vs off2b: C = 0 at 1, 2, 3; cp1 ticks within 8 | exact / COPIED. FAIL -> SC and LB NO VERDICT |
| S0B stimulus ran | every part-S run: installed, start, highest `bullets fired` >= 50, no script error line, actor x/z range >= 1.5 m | COPIED (s4d1 S0B) |
| **SC on = off** (requirement 1) | C = 0 at cp1, 2, 3 and cp1 ticks within 8: on2a vs off2a, on2b vs off2a, on3 vs off3 (same exe, same flags, only the mask differs) | **exact**, section 5 |
| **LB bit-off = build 1** (requirement 2) | b1off2 (build 1 exe) vs off2a (build 2 exe, bit 16 off), same flags: C = 0 at 1, 2, 3; cp1 ticks within 8 | **exact, gated**, section 5. The build-1 side is SimTick18 (1.2) |
| TW tripwires | every part-S run except the breaks: 0 `* tw` lines of any slice (s2, s3, pose, attach); every part-S run: per frame to cp3 the number of `* tw` lines = the witness tw column, every line on a witnessed frame | exact (sim_4d_tw_bad counts and logs in one call, device.cpp:509-513) |
| TWB16 must-fail | brk16's OWN witness: every frame to cp3 whose pw is form L with hd != hl carries a `pose` BAD line; >= floor(PROBE_P_TICK/3) such frames; 0 BAD lines of attach, s2, s3 | per frame exact. The break writes draw into `mRenderTransform` inside the pose bracket and after the witness took hl (sim_pose.cpp:422-430), so hd != hl means a hashed field changed in that frame. On tick frames (u = 0) draw = prev != cur = live whenever the pose changed, so at least the probe's moving tick frames qualify (3x). The attach bracket hashes the bones only after the parent's evaluation (GameObject.cpp:1088-1095), so break 16 cannot trip `attach` [D] |
| TWB32 must-fail | brk32's OWN witness: every frame to cp3 with pa form C/R and hx != hxe carries an `attach` BAD line; >= floor(PROBE_PA_CR/2/3) such frames; 0 BAD lines of pose, s2, s3 | per frame exact: hx is hashed before the break (GameObject.cpp:1080), hxe after the join, so hx != hxe means the break changed XFORM() inside the attach bracket (:1119-1125). Frames with C/R and hx == hxe (draw bitwise equal to XFORM) are reported. The count floor assumes the break does not reduce form-C frames: a weapon is re-placed every rendered frame (HudItem.cpp:128), so its record is fresh again [D]; B-kind items stay stale until the next CalculateBones [G] |
| P1 drawn between ticks | on runs: n=0 frames with pw form L and hd != hl >= floor(PROBE_P_LERP x (K-1)/2/3) | 3x by construction |
| P2 pure function (S4a) | on runs, every form-L pw line: ctick = ptick + 1 and ctick = T; u = a (1.5e-6); hl = hc (the guard); if hu is present then hdu is present and hu = hdu (**the uploaded matrices are the draw array**); jd on lerp(jp, jc, u) within 1e-4 + 4 ulp (or jd = jc if PROBE_JOINT_LERP = 0) | the pair and guard parts are the code's own L conditions (sim_pose.cpp:408-418) and are named for the reader. The OUTCOME parts are u = a (the pose is drawn at the frame's alpha) and hu = hdu (the GPU gets the blend). The joint part checks the pivot: p = lerp(R_prev(j), R_cur(j), u) (sim_pose.cpp:220-224); its rounding is ~2 ulp of <= 2 m, so >= 20x under 1e-4 |
| P3 one tick behind | on runs: every tick frame (n = 1, a = 0) with form L has hd = hp; >= floor(PROBE_P_TICK/3) such frames with hp != hc | exact: u = 0 draws prev bitwise (sim_pose.cpp:242-251; an unchanged pose is cur = prev). Root and pose are one tick behind together (design 3.2) |
| P4 off = stock upload | off runs: p column all zero and pw absent on every line; up hu = hl on every frame with up; frames with up >= floor(PROBE_UPLOADS x K/3/3) | exact: with bit 16 off g_sim_pose_active is FALSE, so there is no capture, no storage and simR = nullptr (S4a notes 3) |
| **U1 opt-in** | on and off runs: the uu column present (W1) and nud = 0, nus = 0 on every line; on runs: n=0 frames with nposed > 0 >= floor(PROBE_U_POSED x (K-1)/2/3) | exact: an unmarked skeleton has no capture (SkeletonRigid.cpp:135 tests the mark) and no draw (sim_pose.cpp:437), so it is uploaded live. nposed shows it changed between ticks, which is what made gammavr's VR models a risk (design 12 C1). 3x on the count |
| PA1 attach forms (S4b) | on and off runs, every pa line: hx = hxe when both are present (the render leaves the item's XFORM unchanged); forms P, O, S have wd = ws; on runs: C and R have cls c or p and u = a; O has cls o; off runs: only forms S or P, cls `-`, u `-` | exact: P/O/S draw the 4b rule, and the witness's ws is that rule's matrix (GameObject.cpp:1099-1116, 1129-1130). The hx part is stock behaviour under det serial mode (no XFORM writer after the render [D, with the CInventoryItem no-guard caveat of s4d1 NOTES]); the probe's `hx` rule settles it first |
| **PA2 item rides the drawn hand** (S4b outcome) | on runs: n=0 frames with pa form C/R and wd != ws >= floor(PROBE_PA_CR x (K-1)/2/3); form-C lines grouped by (kind, ba, bb): ld leader-clusters (radius TOL_LD = 1e-3 m) per group <= PROBE_PA_CLUSTERS | ld = the drawn item's origin in its DRAWN placement frame (inverse(parent_draw x A_d) x wd, GameObject.cpp:1021-1027). When the recorded bones are those the placement read, ld = inverse(A_c) x L = the item's offset O, a constant per item (re-review N2's proposal, "compare against its offset"). A wrong or torn record (re-review S2) makes it wander by the hand's motion. **ld = ls is a tautology** (re-review N2): only reported. Form R is excluded (right hand alone: ld varies with the two-hand geometry) |

Reported, not gated:
- per on, break and wallclock run:
  - the skeleton, guard and snap counts;
  - **F5:** render-only recalcs byte-different and byte-equal;
  - the pw form histogram;
  - the pa kind+form and cls histograms (how often c, p and o occur);
  - max |ld - ls| (rounding only);
  - for kinds B, T and N, the max of | |wd - hd| - |ld| |. That rigidity check is exact only for orthonormal frames,
    and W's first axis is not unit (S4b notes 2.1);
  - pn presence;
- pn hsim on tick frames, on vs off at the same K (online state);
- C of K=2 vs K=3 (on and off), b1off2 vs off2b, and wc2 vs on2a;
- online objects and generator/Lua lines for off2a/off2b, on/off pairs, b1off2/off2a and the breaks vs off2a.

**Verdict rule.**
- A1 FAIL makes A2-A4 NO VERDICT; S0 FAIL makes SC and LB NO VERDICT.
- The result is FAIL if any non-harness check FAILs. That includes a must-fail (PWC, TWB16, TWB32) that "passes",
  i.e. does not fail as predicted.
- Otherwise the result is NO VERDICT if a harness check (A1, S0) failed or anything is NO VERDICT, else PASS.
- **22 gated checks:** F0, A1-A5, PAT, PWC, S0, S0B, SC, LB, TW, TWB16, TWB32, P1, P2, P3, P4, U1, PA1, PA2. None is
  droppable: every condition that could empty one is a probe STOP (2.2).

## 5. The Rule One gate: why exact, and how SC and LB separate layout from logic
- **Structure (kept from s4d1).** The equivalence gate is the offline A-Life counters, EXACT, plus the in-run
  tripwires. Online state is reported only, because the det harness does not repeat it stock vs stock (section 0).
- **Why exact is the right bound for SC (requirement 1).**
  - The offline counters (A-Life update counts per offline object, squad states, stream counters) are a function of
    the offline simulation only. Stock det runs repeat them exactly: A1 and S0, held in every part A so far [M].
  - Bit 16's code writes only render-only state (1; S4a notes 3; S4b notes 5; both reviews VR).
  - So the counters of the on and off arms are the same function of the same inputs, unless something the simulation
    reads differs. That can happen through a write outside the hashed fields, or through an address-dependent order.
  - There is no tolerance between "the same offline simulation" and "a different one". A difference of 1 is a
    different simulation, so the bound is 0.
- **LB (requirement 2): bit 16 off on build 2 vs build 1, same flags.**
  - The build-1 side is s4d1's SimTick18.exe (1.2). It gets the identical command line, and the two pose switches are
    inert there [D].
  - LB is gated, with the same exactness argument as A2. Bit-off build 2 must equal build 1 by Rule One, and A2 has
    held C = 0 across layout-changing builds before [M].
  - What LB adds over A2: the gated-mode code paths with the bit off (the record writers' `if (g_sim_pose_active)`,
    the child rule's S4b lines, the witness) run here, and under A2's sim_tick 0 they do not.
- **How to read the pair** (also the named risk, 10.1):

  | SC | LB | A2 | tripwires | reading |
  |---|---|---|---|---|
  | PASS | PASS | PASS | clean | no Rule One effect seen |
  | FAIL | PASS | PASS | clean | an ON-arm-only effect that no tripwire hashes: the heap allocation of SimPoseState (layout through addresses) or a bit-16 write outside the hashed fields (logic). Look first at sim_pose.cpp:292 |
  | PASS or FAIL | FAIL | PASS | clean | a gated-mode bit-off effect of build 2: the static layout (record GameObject.h:257, IKinematics pointer Kinematics.h:130) seen under the lever, or a bit-off code path |
  | any | any | FAIL | | build 2's layout or sim_tick-0 code (the mark writes) already differs without the lever |
  | FAIL | | | BAD lines present | a logic leak the tripwires caught (TW fails too) |

  These runs localize a difference; they cannot prove that a pointer-ordered container exists.

## 6. Run order, tags, commands, time, stop and retry rules
### 6.1 Order (one series, one shared claim; tags lowercase)
1. **Probe:** s4p_probe, plus s4p_probe_b only by 2.1. Then: GO, fill, self-test on real bytes, register.
2. **Part A:** s4p_s1 s4p_o1 s4p_s2 s4p_o2 s4p_s3 s4p_o3.
3. **Part S:** s4p_off2a s4p_on2a s4p_b1off2 s4p_off3 s4p_on3 s4p_off2b s4p_on2b s4p_wc2 s4p_brk16 s4p_brk32.

The arms of each pair are spread out. The breaks go last; each boot starts fresh (det_run.sh restores user.ltx and
loads the save).
### 6.2 Command per boot (series script s4d2_run.sh: TO BE WRITTEN, requirement R2)
`DET_NOCLAIM=1 BENCH_TIMEOUT=<2000 or from PROBE_BOOT_S> DET_EXTRA="<base> <lever>" tools/det_run.sh <exe> <tag> 60 3`,
then `mv run/appdata/logs/synth_<tag>.txt out/det_<tag>/`. "60 3" is ignored under -det_passes. `<lever>` is per
section 4's table, with the NPC = PROBE_NPC.

The script must:
- refuse to start if any `run/appdata/logs/synth_s4p_*.txt` or `out/det_s4p_*` exists, except the probe folders;
- pin SimTick19 (PIN_ME_EXE), SimTick18 (PIN_ME_EXE_B1), Stance14 b4ce9c77, out/user_det.ltx.orig 3e0e0a6f1fe8010c
  (prefix) and the stimulus script;
- check cfg0 (sim_tick 0, r__framelimit 0 in run/appdata/user.ltx; tools/s4c_partA.sh:23);
- run the flag preflight of tools/s4c_partA.sh:10-16 for the base flags;
- run the in-run preflight on s4p_off2a at cp1: counter file present, witness file present and non-empty with
  126/132-token lines, and the engine command line, det_synth line and pose line as registered. Otherwise stop at once;
- keep one shared claim (WANT shared) for the whole series. det_pair.sh cannot be used: it passes one DET_EXTRA to all
  its runs.
### 6.3 Time per boot (derivation)
These figures are from the s4d1 draft 6.3; they are not measured for build 2:
- stock to cp3: 180-211 s [M];
- K=2 ~290 s and K=3 ~375 s [G: 15 ms per render-only frame];
- ~40 s between boots [M].

Added for build 2: the tripwire hashing of every capture and evaluation, plus the witness hashing, about 1 ms per
frame [G, design 7.3]. That is ~11 s at K=2 (~10,900 frames) and ~16 s at K=3. So K=2 ~300 s and K=3 ~390 s.

| part | estimate |
|---|---|
| probe | 390 + 40 s = 7 min (re-probe +7 min) |
| A | 1191 s [M] + 6 x 40 s = 24 min |
| S | 8 x 300 + 2 x 390 + 10 x 40 s = 60 min |
| **scored series (16 boots)** | **~1 h 24 min** |
| **with the probe (17 boots)** | **~1 h 31 min** |
| worst case (18 boots, with the re-probe) | ~1 h 38 min |

That is under 3 h: one slice. The probe's elapsed time replaces the guesses before registration.

BENCH_TIMEOUT is 2000 s: 5.1x the K=3 guess and 9.5x the stock maximum. A shared claim needs >= 5x. 1900 s (s4d1)
would be only 4.9x here, because of the added hashing: flagged, and not COPIED.
### 6.4 Stop rules (no check is scored before the end; these look only at each run's own completion)
- **Before the series:** the pins, cfg0 and the user_det hash of 6.2; no leftover witness files or out folders.
- **During the series:**
  - a protected title or a lost claim: stop (box);
  - a crash dump: stop (fault);
  - the in-run preflight of 6.2: stop.
### 6.5 One-retry rule
- A boot that ends rc != 0 (timeout, early exit) is run once more right away as `<tag>_r1`, with the same switches.
- The scorer reads `det_<tag>_r1` when it exists and names the substitution.
- A second failure of the same slot stops the series (fault).
- A boot is never re-run because of what its output shows.

**Scored ONCE, at the end:** `python3 -I s4d2/s4d2_score.py root=out out=s4d2/s4d2_score_result.txt`. The scorer
refuses an existing out file and has no override, so there is no re-score after a checker fix.

## 7. Margin table
| bound | value | expected | margin | note |
|---|---|---|---|---|
| C = 0 (A1, A2, S0, SC, LB) | 0 | 0 | exact | section 5 |
| cp1 frame / tick difference (A3, S0, SC, LB) | <= 8 | 0.75-2.7 [M]; an extra pass = 17 | 3.0x over the noise, **2.1x** under the signal | **COPIED** |
| A-Life calls to cp1 (A4) | >= 50 | 100 [M] | **2.0x** | **COPIED**, flagged |
| PAT a, dt / hud | 1.5e-6 / 2e-3 | print rounding 5e-7 / 5e-4 | 3x / 4x | re-derived: same printf formats |
| walk range (S0B, probe) | >= 1.5 m | 3.0 m | **2.0x** | **COPIED** (script geometry), flagged |
| bullets fired (S0B, probe) | >= 50 | ~171 [G] | 3.4x | **COPIED** |
| position equal (PA1 wd = ws) | 1e-4 m | 0: the same matrix printed twice | exact in practice; 20x over print rounding | |
| u = a (P2, PA1, PWC) | 1.5e-6 | 0: the same float printed twice | 3x over print rounding | |
| joint lerp (P2) | 1e-4 + 4 ulp | ~2 ulp of <= 2 m = 5e-7 + print 5e-6 | >= 18x | |
| P1, P3, P4, U1, PA2 counts; TWB16, TWB32 floors | floor(probe scaled / 3) | probe scaled | 3x | by construction. TWB32 also assumes [G] the break does not reduce form-C frames |
| PWC shares | >= 0.90 | ~1.0 [D] | 10 % allowed vs ~1e-6 expected | |
| PWC form-L floor | >= 30 | thousands [G] | > 30x | evidence floor |
| TOL_LD (PA2) | 1e-3 m | float32 rounding of world coords up to ~270 m: ~3e-5 per op, ~3e-4 worst [G]; a wrong-tick record shifts ld by the hand's motion per tick, 1-30 mm [G] | **~3x over the noise, 1-30x under the signal** | **flagged: for idle hand motion of ~1 mm the signal side is < 2x** [G]. The probe's cluster count shows the noise side on real bytes |
| PROBE_PA_CLUSTERS (PA2) | probe value, stop > 2 | 1 per item; 2 if an offset changes (strap) [G] | exact against the probe | |
| probe stops: counts | 300 | ~5440 x (K-1) for pose [G]; the NPC's frames unknown | ~36x for pose; unknown for attach and optin | these are decision rules, not verdict bounds |
| probe stops: npc share / f5 share | 0.5 / 0.5 | ~1.0 / ~0 [G] | **2x** / n.a. | decision rules, flagged |
| BENCH_TIMEOUT | 2000 s | <= 390 s [G] | 5.1x | reset from PROBE_BOOT_S |

Flags:
- **At 2x:** A-Life calls (COPIED), walk range (COPIED) and the probe's npc share.
- **At 2.1x:** cp1 against a real extra pass (COPIED).
- **Possibly under 2x:** TOL_LD on the signal side for idle-only hand motion.
- **COPIED bounds:** cp1 difference, A-Life calls and gaps, walk range, bullets fired, and the part-A method.

**Claim class per run: SHARED, every run** (probe, A, S).
- No verdict depends on frame or wall timing: under the lever every gated column is a function of the pattern or of
  one frame's own values.
- The one real-clock run (s4p_wc2) is a must-fail whose outcome (u = a != j/K) holds at any load.
- The only timing bound is the timeout, at 5.1x (>= 5x).
- No run needs most of the GPU (headless det boot, 1280x720).

## 8. Scorer and self-test
`s4d2/s4d2_score.py` (stdlib, `python3 -I`):
- **Score mode:** `root=<dir with det_<tag> folders> out=<file>`. Exit 0 PASS, 1 FAIL, 3 NO VERDICT, 2 REFUSED
  (placeholders, out exists, usage). It refuses a second run onto an existing out file, with no override.
- **Probe mode:** `mode=probe root=<dir> out=<file>`. Exit 0 GO, 1 STOP, 2 REFUSED.
- **It reads per run:**
  - the witness `synth_<tag>.txt`: 58 tokens for b1off2; 126 or 132 for build-2 part-S runs; every column validated;
  - `xray.log`;
  - `det_alife_counts`, `det_counters` (C, as counters_diff.py);
  - `det_trace_<tag>.txt.sched` (part A);
  - `alife_dump` (reported online diff).
- Every reader strips \r.
- **Output:** verdict lines, then reported lines, then `S4D2 RESULT: ...`. SC and LB failures carry the named-risk
  text of 10.1 when the runs' tripwires are clean.

`s4d2/s4d2_selftest.sh` (run with nice 19) uses synthetic fixtures (LIMIT: no real run exists yet). It has:
- the good set, plus a CRLF variant and an absent-value-dash variant (pa, pn, up absent on some frames);
- one mutant per gated check, each of which must change exactly its verdicts. This includes:
  - the heap-layout fixture (on vs off counters differ, every tripwire clean: SC FAIL naming "heap layout" and
    sim_pose.cpp:292);
  - the build-1 fixture (LB FAIL naming GameObject.h:257);
- the three must-fails (PWC, TWB16, TWB32) "did not fail", and for each break also "a frame not caught" and "wrong
  slice";
- a missing witness and a malformed pw column, each failing by name in every check that reads it;
- a witness without the W1 column (U1 fails by name);
- the `_r1` retry, both joint rules, and the refusals (placeholders; a second scoring onto the same out file);
- 13 probe-mode cases: GO with the expected CONST lines, CRLF, and one per stop rule (w1, pose, f5, npc + attach, ld,
  hx, optin, tripwire, stimulus, malformed input, missing input).

**Result 2026-11-27:** `S4D2 SELFTEST: PASS (57/57)`, folder s4d2/selftest-runs/20261127-214140-1115043 (see NOTES.md 7).
**It must be re-run with a case built on the probe's real folder before registration.** The hashes of the scorer and
the self-test are to be filled at registration.

## 9. Predictions (fixed before any run)
- **Probe:** see 2.4 (GO on the first boot about 10 %; after the re-probe and R1 about 30 %).
- **Series, given GO:**

  | check | prediction |
  |---|---|
  | Part A all PASS | 75 % (as 4a/4c/s4d1) |
  | PAT | 85 % |
  | PWC fails as predicted | 90 % |
  | S0 | 80 % |
  | S0B | 90 % |
  | **SC** | **70 %** (the heap-layout risk, 10.1) |
  | **LB** | **70 %** |
  | TW | 80 % (two new tripwires on real data) |
  | TWB16 fails as predicted | 90 % |
  | TWB32 fails as predicted | 85 % |
  | P1 | 90 % |
  | P2 | 75 % (hu = hdu on real uploads; the joint rule) |
  | P3 | 85 % |
  | P4 | 95 % |
  | U1 | 85 % |
  | PA1 | 75 % (the hx rule) |
  | PA2 | 70 % |
  | **All 22 gated checks PASS, given GO** | **about 15 %** |

## 10. Named risks
### 10.1 Heap layout
- **What it is.** Build 2 changes memory layout in two ways:
  - **in both arms of the same exe:** every CGameObject grows by ~280 B (`m_sim_attach`, GameObject.h:257) and
    IKinematics by a pointer (`m_sim_pose`, Kinematics.h:130);
  - **in the bit-16 ON arm only:** ~21 KB is allocated per captured skeleton from the engine heap
    (`xr_new<SimPoseState>` in sim_pose_capture_body, sim_pose.cpp:292), freed at Spawn or destruction.

  So heap addresses differ between the on and off arms, and between build 1 and build 2. Any simulation container
  ordered or hashed by pointer (an address-keyed map, a sort by pointer, iteration of an unordered set of objects)
  could then visit objects in another order. The offline A-Life counters would then differ with no tripwire firing,
  because no simulation field the tripwires hash was written. The S4a review (note) and the S4b review and re-review
  (R1-a, N3) flagged this; none found such a container, and none searched for one.
- **What a failure of SC with every tripwire clean would mean.** An on-arm-only Rule One effect that no tripwire sees:
  - most likely the allocation pattern (layout through addresses);
  - else a bit-16 write to simulation state outside the hashed fields (logic).

  LB and A2 tell them apart (section 5 table).
- **Where to look first:**
  - **SC:** the storage allocation in `sim_pose_capture_body` (sim_pose.cpp:292). Then search xrGame/xrAICore for
    pointer-keyed containers walked during the offline A-Life update.
  - **LB or A2:** the record member `CGameObject::m_sim_attach` (GameObject.h:257), then `IKinematics::m_sim_pose`.
- **Fixes, if it shows** (not in this series):
  - allocate SimPoseState from a private heap or a fixed slab, so the xrMemory arena is untouched (S4a review NOTE);
  - or allocate it in both arms.

  A follow-up diagnostic could be a det-only switch that allocates storage with the bit off, to re-run SC with the
  allocations equal. It is not in this registration.
### 10.2 F5 (pre-existing, design 0 and D6)
An armed NPC's bones are recalculated on render-only frames (CWeapon::UpdateXForm invalidates its parent).
- Byte-equal recalcs are a no-op for S4a.
- Byte-different ones drop the history (sim_pose.cpp:304-320). The NPC is then drawn stock, and its item is never
  corrected.

The probe's `f5` rule decides it. In non-det builds each recalc also draws `::Random` (design 0): that is a Rule One
leak of gated mode since 4a, and it is not measured here (det serial).
### 10.3 What the S4b checks reach
- **PA2 shows the item at a constant offset in the drawn placement frame A_d.**
- **It does not show that A_d is the mesh's drawn hand.** That rests on two things:
  - sim_pose_bone_drawn taking t_draw from the same `draw[]` the upload uses (sim_pose.cpp:472-481, [D]);
  - P2's hu = hdu, which is shown only for the pw skeleton.

  An optional witness addition W2 would close it: pn printing the `bip01_r_hand` joint triple jd/jp/jc, so that on
  cls-c frames |hd - hr| could be compared with |jd - jc| for the witness NPC. W2 is not required. Without it, this
  link is desk-only.
- **ld = ls is a tautology** (re-review N2): only reported.
- pa has no child id, so a weapon switch shows as a new ld cluster (covered by PROBE_PA_CLUSTERS).
### 10.4 pw comparability (S4b review O2)
pw is the first skeleton evaluated in the frame, and the child rule (and the tw bracket) can move that evaluation
earlier. So pw is compared only line by line, never across runs, exes or switch sets.
### 10.5 The review of c216081fd
c216081fd is under independent review. If the review changes the code, the pin, the probe and the self-test's
real-bytes case are all redone. If it changes a witness format, the scorer is changed BEFORE the probe, and section 1.1
is updated.

## 11. What these runs cannot show
- Whether a blended NPC, its weapon, a torch or a corpse LOOKS right (Caden's eyes).
- The light, particles, wallmarks, script-attachment lights and the muzzle flash, which still use live bones (S4b
  notes 3).
- Real gated mode: races between the capture and the first evaluation, real alpha, multi-tick frames.
- R1 soft skinning, skeleton LOD switches, mutants specifically (unless one is pw), ragdolls.
- The VR path itself: U1 uses the HUD hands as a stand-in for a self-posed unmarked model; gammavr's models are not run.
- Anything after A-Life pass 320, any other save, native Windows.
