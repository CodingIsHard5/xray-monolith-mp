# PREREG s4d2: stage 4d-2 build 2 (NPC and mutant poses drawn between ticks, S4a, with items on bones following the drawn pose, S4b) under the synthetic gated lever (DRAFT 2026-11-27, NOT registered)

DRAFT. Nothing here has been run. It is not registered until every item of section 1.4 is done: the exe is built
with the witness addition W1 and pinned, the probe (section 2) has said GO, every `PROBE_` value is filled in by the
rules in 2.3, and the self-test has passed on the probe's real bytes (section 8). No box run without the Overseer's GO.

**Code:** wt-simtick perf/tick-4d HEAD **HEAD_AFTER_W1 (to be filled at registration)**, on top of build 1
(8527808c9..6acb77044). The chain:
- S4a: 0dfdff031 (capture, draw, mark, witness p/pw/up/pn, tripwire `pose`, break 16) and 0a8e94094 (review fixes:
  players' bodies unmarked, unchanged pose drawn as cur).
- S4b: f2d97ac4f (record, correction, witness `pa`, tripwire `attach`, break 32), f5b23fd7e (the record holds the bones
  the placement read) and c216081fd (re-review S1 and S2 fixes).
- **The W1 commit** (not yet made; the edits are uncommitted in wt-simtick, 2026-11-27): the witness columns `uu` (W1)
  and `pj` (W2), the pa form letter `I` for c216081fd's S1 path (review F1), the exact `parent_draw * L` on that path
  (review N5), and the N6 comment. Code note: plan-facts/stage4d2-w1-code-notes-2026-11-27.md.

So the chain is 0dfdff031, 0a8e94094, f2d97ac4f, f5b23fd7e, c216081fd + the W1 commit.

Never compiled; the first CI build is pending. S4a and S4b are registered as ONE player-facing step: both sit under
mask bit 16, and the S4a design says S4a must not reach players without S4b (design D3).
**c216081fd's independent review is done** (plan-facts/stage4d2-c216081fd-review-2026-11-27.md: nothing blocking; F1,
N5, N6 applied in the W1 commit). **The W1 commit gets its own independent review before this registration is final.**
A code change from that review means a new exe, a new pin and a new probe.

Design documents (plan-facts/):
- stage4d2-npc-animation-design-2026-11-27.md ("the design"); its section 12 overrides its body.
- stage4d2-s4a-code-notes-2026-11-27.md ("S4a notes").
- stage4d2-s4b-design-and-code-2026-11-27.md ("S4b notes"); its section 11 is current.
- Reviews: stage4d2-s4a-review-, stage4d2-s4b-review- and stage4d2-s4b-rereview-2026-11-27.md.

Model copied for form, flags and method: s4d1/PREREG-s4d1-leftovers-build1-DRAFT.md ("the s4d1 draft") and its
scorer.

Labels:
- **[D]** desk (file:line read at c216081fd, except where the W1 commit moved them: the two named-risk sites are given
  at the W1 tree, `sim_pose.cpp:316` (the xr_new, c216081fd :292) and `GameObject.h:258` (m_sim_attach, c216081fd :257).
  Other c216081fd line numbers map to the W1 tree as: sim_pose.cpp +5 after line 16, +24 after 103, +84 after 530 and
  more in the witness print code after 622; GameObject.cpp +3 after 962, +5 after 1106);
- **[M]** measured (a number in a file);
- **[G]** guess;
- **COPIED** (a bound taken from an earlier registration without re-deriving it; listed in the margin table).

## Change log (draft revisions; newest last)
- 2026-11-27, third revision (coordinator's follow-up after plan-facts/stage4d2-w1-and-runscript-review-2026-11-27.md):
  - **Stimulus switch `-det_synth_item` on EVERY boot** (probe, part A, part S; both arms of every pair; build 1's
    b1off2 too, the stimulus script is exe-independent). Stimulus harness/zz_eperf_synth.script is now sha256
    1777fb6865cc5b56c80f197f3f95a250104d6c90ce0b8b2a35422e3bed39ff95 (was d7dbc6c5...). At update START_UPD (300)
    the actor activates the first filled slot of 3, 2, 1, 5, 6; nothing is spawned; the log says
    `* det_synth stimulus: item slot N section S` or `item NONE (no filled slot)`. **Reason:** U1 has no subject
    headless unless the first-person hands are drawn, and they are drawn only with an active item (1.3). That was the
    probe's known likely stop (`optin`), so it goes into the stimulus now rather than costing a probe. New probe stop
    `item`, F0 item rule, 2.2.1 row, 2.4 probabilities, part-A note (3, 7).
  - The series script revised for the review (6.2): box checks from the shared file
    perf-harness/common/box_preflight.sh (fail-closed: Caden's play lock, a protected title or Sober, an engine of
    the run tree, another claim of ours, EXCLUSIVE, display :0), before the start, the claim, every boot and every ~5 s
    of a boot; the boot in the background and killed on any exit; release by claim-id; the options file restored
    before every boot; probe_b gated on the first reading's stop codes; `probe_fix <n>`; the reading-file chain.
  - Section 11: the reviewer's two U1 limits. 1.3: the sim_pose.cpp:118 comment note.
- 2026-11-27, fourth revision (supervisor's decisions): **tools/det_run.sh changed** (was 01fb40dc..., now sha256
  da66e107b79fb76481c04f7de4d4b079f486941aa00941c8a7f806ff321c6222). Why: it now sources common/box_preflight.sh (refused if
  missing, unreadable, wrong marker or version) and uses the shared fail-closed checks instead of its fail-open title
  check, all before it creates, deletes or moves anything (the xray-log `rm -f` and the crash-dump move included); a
  crash dump is not moved while the play lock is live. s4d2_run.sh passes it `DET_BOX_TOKEN` (its claim token),
  `DET_BOX_CLASS=shared` and the worker, and pins the new hash. The shared file's engine test now counts only Wine
  binaries (tools/lib_engine_pids.sh rule), so the shared file's hash changed too (420c5f2a...).
- 2026-11-28, fifth revision (coordinator's fix list after plan-facts/box-preflight-second-review-2026-11-27.md):
  **common/box_preflight.sh is v2** (was 420c5f2a..., now sha256 f3ad9351c517393c8e8f6b3e947a9b5a2435074bd62508932a9410d4c6d6e4fa) and **tools/det_run.sh changed
  again** (was da66e107..., now sha256 0b7a6edb5f575dea379fb8a4185b4a39605849bab5809e5ddafe45fe313035e0); s4d2_run.sh pins both. Why: (B-1) the box is checked again
  right after the claim and on the line before every write under run/ (det_run.sh's user.ltx swap, log clean-up,
  engine start, collection, restore; s4d2_run.sh's options copy and witness moves); (B-2) after a stop by the play lock,
  a title, Sober or a signal nothing under run/ is written, moved or deleted, only `wineserver -k` in run/prefix and the
  run's own Xvfb, and the files left are listed by name; (S-1) Sober is found by its flatpak (the old `pgrep -x sober`
  never matched: the process is "Main"); (S-6) the claim tool's 180-s title memory is honoured and PTC_* refused;
  (S-7, N-1) claims are read from the claim tool's own files, every nested record counted, and no claim-tool verb is
  called by the checks (`check` is not read-only); (S-2) after det_run.sh ends or is KILLed no engine may be left in
  run/prefix, else the claim is not released; (S-3) the queue entry is withdrawn only if this run made it (a known
  limit remains, 6.2); (S-4) det_run.sh requires display :141; (N-7) only crash dumps newer than the boot's start are
  moved; (N-13) done.txt lists completed boots only.

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
  code at c216081fd plus the uncommitted W1 edits (W1 `uu`, W2 `pj`, pa form I; 1.1, 1.3).
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
  `mRenderTransform_temp` (render-only by grep). The W1 commit adds only test-only witness statics (a 32-slot static
  table, no heap) and, on the S1 path, `parent_draw * L` without the multiply by identity (render-only `s.draw`).
- **Layout changes in every arm of the same exe** (S4b notes 11.1; re-review N3). They are present with sim_tick 0 too:
  - IKinematics +1 pointer (Kinematics.h:130), CKinematics +1 bool;
  - CGameObject +~280 B (the record, GameObject.h:258).
- **Heap use in the ON arm only:** ~21 KB per captured skeleton (`xr_new<SimPoseState>`, sim_pose.cpp:316), section
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

### 1.1 The witness line, as the code prints it (c216081fd + the W1 commit)
With `-det_synth_pose`, sim_pose_witness_print (sim_pose.cpp, witness section) appends seven columns to build 1's
58-token line (device.cpp:546-576), before ` tw N`. The line becomes:

`F .. T .. j .. K .. n .. a .. dt .. hud .. | s2 (9) | s3 (11) | s1 (5) | s4 (6) | p <neval> <nlerp> <nguard>
<nsnap> <nro_diff> <nro_equal> | pw <form> <u> <ptick> <ctick> <hd> <hl> <hp> <hc> <hu> <hdu> <jd xyz> <jp xyz>
<jc xyz> | up <hu> <hl> | pn <form> <u> <hd> <hl> <hsim> | pa <kind> <form> <cls> <ba> <bb> <u> <wd xyz> <ws xyz>
<hd xyz> <hr xyz> <ld xyz> <ls xyz> <hx> <hxe> | uu <nu> <nud> <nus> <nposed> | pj <bone> <jw xyz> | tw <n>`

That is **138 tokens**: p 6, pw 19, up 2, pn 5, pa 26, uu 4, pj 4 values. The column order before `uu` is unchanged
from c216081fd. The absent forms were counted mechanically from the print strings:
- `uu` is always 4 unsigned counts (`0 0 0 0` when no unmarked leaf was uploaded); it has no dash form;
- `pj` is `<bone> <x y z>` or 4 dashes.

The scorer also parses 126 tokens (c216081fd without W1: U1 then FAILS by name) and 132 (W1 without W2: the W2 link
is reported as absent). Every line of one witness must have the same length.

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
| form | C, R (bone corrections), **I** (corrected with C = identity: the bones are drawn bitwise as the placement read them and only the parent's XFORM moved since the placement; c216081fd's S1 path, review F1; NOT a bone correction), O, P, S |
| cls | c, p, o, `-` |
| ba, bb | the record's bones; `-` = none |
| u, hd, hr | `-` unless the parent's pose is drawn L this frame and ba is a captured bone |
| ld, ls | only on C, R or I |
| wd, ws, hx | always present |
| hxe | `-` if the child is not found after the join |

All 26 values are dashes when no placement of that item reached the child rule this frame.

**`uu`** (W1, 1.3) is the unmarked skeletons' skinned-leaf uploads of this frame:
- `nu`: leaves of unmarked skeletons uploaded (every pass and leaf counts);
- `nud`: of them, leaves where any matrix handed to `set_ca` was not the live bone itself (address compare);
- `nus`: of them, leaves whose skeleton holds S4a storage;
- `nposed`: unmarked SKELETONS whose live pose (hash of all bones' mRenderTransform at their first upload of the frame)
  differs from the one stored at their upload in the immediately previous frame.

**`pj`** (W2, 10.3) is the witness NPC's `bip01_r_hand` joint (else its last bone, as pw), as its mesh is drawn in
this frame, in world: the NPC's drawn matrix of this frame (`sim_render.ptr`, the same matrix its item's child rule
used as parent_draw) times `draw[b]` applied to the bone's joint. Present only when pn is form L and that matrix was
set in this frame. `<bone>` is the bone index, to be matched with pa's `ba`.

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
  - The stimulus script is not part of the exe: `-det_synth_item` (like walk and tracer) acts identically on build 1,
    so b1off2 takes the same item as the build-2 off runs it is compared with (LB). F0 checks that every run took the
    same item.
- **Stock reference for part A:** `run/bin/Stance14.exe` b4ce9c77 (COPIED).
- **Stimulus:** `harness/zz_eperf_synth.script` sha256 **1777fb6865cc5b56c80f197f3f95a250104d6c90ce0b8b2a35422e3bed39ff95**
  (with `-det_synth_item`, 2026-11-27), installed byte-identical as run/gamedata/scripts/zz_eperf_synth.script before
  the probe (s4d1 R3); inert without its flags. s4d2_run.sh pins the installed copy to that hash.

### 1.3 Witness gap W1: an unmarked skeleton cannot be isolated (REQUIRED; committed as f19819c78)
The opt-in check U1 needs to know whether a skeleton NOT marked (the stand-in for gammavr's self-posed VR models) is
drawn byte-stock with bit 16 on, while it is posed between ticks. The witness at c216081fd cannot show this:
- `pw`, `pn`, `pa` and the `p` counts see only skeletons with storage, and storage exists only for marked ones;
- `up` is "the first skinned leaf uploaded, any skeleton". It names neither the skeleton nor its mark, and it is
  `hu == hl` by construction whenever the upload used the live bones.

**W1 as coded** (committed f19819c78: the uu and pj columns in sim_pose.cpp; the commit also touches sim_pose.h and
GameObject.cpp/.h for form I and N5; no file under src/Layers or src/Include changed; details and the Rule One and
thread arguments in plan-facts/stage4d2-w1-code-notes-2026-11-27.md):
- In `sim_pose_wit_upload` (already called for every skinned-leaf matrix by SkeletonX.cpp:142, with the skeleton), at
  each leaf's mid 0 the skeleton is classed unmarked if `!K->LL_GetSimObjectVisual()`.
- For an unmarked leaf, every matrix handed over is compared BY ADDRESS with the live bone
  (`&M != &K->LL_GetBoneInstance(mid).mRenderTransform`). Stock passes `LL_GetTransform_R(mid)`, which is that
  address (SkeletonCustom.h:309). This is stronger than the hash compare first proposed: a draw array with equal bytes
  also counts. At the leaf's last matrix: `nu++`, `nud++` if any address differed, `nus++` if `K->has_sim_pose()`.
- At the first leaf of an unmarked skeleton in a frame: its live pose is hashed and looked up in a test-only table
  keyed by (K, bone count). `nposed++` only if the stored hash is from the IMMEDIATELY previous frame and differs;
  then this frame's hash is stored.
  - "Consecutive frames only" is a change from the first spec. On a render-only frame no tick ran since the previous
    frame's upload (in det serial mode the tick, CalculateBonesThread and the game thread all run before the render,
    device.cpp:942, 1039-1053, before seqRender), so a difference there is posing between ticks. A comparison across skipped frames could
    include a tick and would inflate the count.
  - The table is 32 static slots, not a map: no heap (an allocation in the off arm only would be a layout difference
    against build 1, LB). A full table evicts the oldest slot, so a miss can only LOWER nposed.
  - A freed skeleton whose address is reused with the same bone count can raise nposed once. nposed is only a count
    floor (U1's subject), never a pass condition by itself.
  - **Code fact to fix (review S1-1):** the comment at sim_pose.cpp:118 says a full table "can only lower nposed, never
    raise it". It needs "an address reused one frame later can raise nposed once" (with the same bone count). The
    coordinator changes the comment himself; it is a comment only, but any edit means a new exe and pin (1.4 item 2).
- Reset per frame with the other counts; the table is not reset. Printed ` uu %u %u %u %u |` after `pa`, before `pj`.

**What it can see headless** [D for the path, G for the frames]: the first-person HUD hands and the HUD item model
(player_hud m_model, m_model_2, the attached item's HUD model) are unmarked. Under the lever they are re-posed on every
gated frame in `sim_view_present` (ActorCameras.cpp:765-770, `g_player_hud->update` with the 4c HUD clock, then
CalculateBones_Invalidate + CalculateBones, player_hud.cpp:1622-1627). They are drawn through `render_hud`
(player_hud.cpp:1336-1366) → `add_Dynamic` → `CSkeletonX::_Render` → the upload loop, **but only when an attached item
needs rendering or a script animation runs** (player_hud.cpp:1338-1341). The det profile uses renderer_r4
(out/user_det.ltx.orig:591), so the hands are hardware-skinned (RM_SKINNING_xB) and pass the hook; an RM_SINGLE or
RM_SKINNING_SOFT leaf never reaches it. The actor's own body and legs are unmarked too, but they are calculated in
ticks only [G], so they add to `nu`, not to `nposed`. **Expectation: nposed > 0 on most n=0 frames only if the actor
holds an active item; otherwise about 0** [G]. `-det_synth_item` (every boot) gives it one at update 300 if the bench
save's actor carries anything in slots 3, 2, 1, 5 or 6; the probe's `item` and `optin` rules settle it.

Without W1 in the exe:
- the scorer reads 126-token lines and U1 FAILS by name ("required witness addition W1");
- the probe STOPs (`w1`).

### 1.4 Required before registration
1. W1 (1.3) in the code (done, uncommitted), committed as "the W1 commit", and reviewed. It is test-only, but it adds
   to sim_pose.cpp, and it changes two lines of the S4b draw (review N5) in GameObject.cpp.
2. **c216081fd's independent review**, and the W1 review, done, and any finding fixed. Any code change means a new CI
   build, a new pin and a new probe.
3. A CI perf-build of the result, installed as SimTick19.exe and pinned (PIN_ME_EXE).
4. s4d1's build-1 exe built and pinned (PIN_ME_EXE_B1). LB cannot run without it.
5. The stimulus script installed with its pinned hash 1777fb68... (s4d1 R3; with `-det_synth_item`).
6. **The series script `s4d2_run.sh` (written and revised 2026-11-27, `bash -n` only, never run; R2 of 6.2).** It moves
   `run/appdata/logs/synth_<tag>.txt` into `out/det_<tag>/` after every boot. tools/det_run.sh does not: its lines 44-47
   move dumps, traces, counters and saves only (s4d1 NOTES 2.13, re-read today). Its PIN_ME_ and REG_ME_ values are
   filled at registration; it refuses to boot while any remains.
7. The probe GO (2.2), the `PROBE_` constants filled into the scorer's CONST, and BENCH_TIMEOUT set from PROBE_BOOT_S.
8. The self-test re-run with a case built on the probe's real folder (CRLF, real hashes, real `pa` forms), PASS. The
   hashes of the scorer and the self-test recorded in section 8.
9. Done 2026-11-27 in the stimulus: s4d1's R1 (the actor holds an active item headless, s4d1 NOTES 4) is the switch
   `-det_synth_item` on every boot. The first-person HUD hands are the only unmarked skeleton the desk expects to be
   posed between ticks (4c's HUD clock); without an item they are not drawn. If the bench actor has no filled slot,
   the probe STOPs on `item` (2.2).
10. The Overseer's GO.

## 2. Probe (one boot, unscored, FIRST, before registration)
### 2.1 The run
`s4p_probe` runs SimTick19.exe with K = 3, mask 23 (7 | 16), the walk and tracer stimulus, tripwires on and no break:

`DET_EXTRA="-det_passes 100,200,320 -det_lua_streams -det_counters -det_synth_item -det_synth_k 3 -sim_4d_mask 23
-det_synth_tw -det_synth_walk -det_synth_tracer -det_synth_pose -det_synth_pose_npc sim_default_duty_428175"`

The NPC is the walking Duty squad member, 60/32/6 m from the player at cp1/2/3 (design 7.1 [M]).

Reader: `python3 -I s4d2_score.py mode=probe root=out out=s4d2/probe_readings.txt`. It is written once and exits
0 GO, 1 STOP. Its last line is `S4D2 PROBE: GO` or `S4D2 PROBE: STOP (<n> rules)`, each failed rule above it as
`PROBE STOP <code>: ...`.

**One re-probe is allowed, only on the stop rules `npc` or `attach`.** The re-probe (`s4d2_run.sh probe_b`) uses
`-det_synth_pose_npc esc_2_12_stalker_fanat`: the nearest NPC, 4 m away at cp1 [M]; whether it is armed is [G]. The
script refuses unless `probe_readings.txt` (CR stripped) ends in `S4D2 PROBE: STOP (<n> rules)` and its PROBE STOP codes
are only `npc` and/or `attach`, and no other reading exists. After the boot's gates have passed (so a box stop does not
use it up), it renames the first probe's folder to `out/det_s4p_probe_a`, then boots the re-probe with the SAME tag
`s4p_probe`, so its folder, det_tag and file names are what the reader expects. Its reading goes to
`s4d2/probe_readings_b.txt`.

A re-probe after a code or stimulus fix that a stop rule asks for is allowed as in s4d1 (the earlier folders are kept):
`s4d2_run.sh probe_fix <n>` (n = 1, 2, ...). The documented manual step comes first: write `s4d2/probe_fix<n>.txt`
(which stop, what changed, the new hashes) and edit the script's pin lines by hand for any changed file. The script
needs the latest reading to end in STOP, renames `out/det_s4p_probe` to `out/det_s4p_probe_before_f<n>` after the
gates, boots `s4p_probe` with the NPC of the previous attempt, and the reading goes to `s4d2/probe_readings_f<n>.txt`.

**Which reading the series uses:** the latest of the chain probe_readings.txt, probe_readings_b.txt,
probe_readings_f1.txt, f2, ... Its last line must be exactly `S4D2 PROBE: GO`, and every earlier one must end in STOP.

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
| attach | PROBE_PA_CR >= 300 (n=0 frames with pa form C or R and wd != ws; **form I is not counted**, review F1: on a moving parent it has wd != ws with no bone correction) | (c) the NPC holds an item and the S4b correction moves it | the same re-probe; if it still fails, S4b would be unverified while being half of the step, so the Overseer decides (recommendation: a stimulus that arms a named NPC, not dropping S4b) |
| ld | <= 2 ld clusters per (kind, ba, bb) group | the record's bones are those the placement used (the outcome PA2 gates) | back to the code (re-review S2 class); not a harness issue |
| hx | hx == hxe on every pa line | the item's XFORM is not changed after the child rule in a stock frame; otherwise PA1's hx rule cannot be used | the Overseer: drop that rule or find the writer |
| item | exactly one `* det_synth stimulus: item slot N section S` line, not `item NONE`, no script error | the HUD hands are drawn only with an active item (1.3); without one, `optin` cannot pass | 2.2.1: a different slot list, or spawn an item; re-probe (probe_fix) |
| optin | PROBE_U_POSED >= 300 (n=0 frames with an unmarked skeleton posed between ticks) | U1 has a subject: a stand-in for a self-posed model on screen | with `item` passed: the Overseer (the hands are drawn but not posed between ticks: the 4c HUD clock claim is wrong); with `item` stopped: as `item` |

**(d) Build 1's s4 question is not asked here.**

#### 2.2.1 What each stop changes (written before the probe)
"CI build" = a code change, so a new perf-build, a new pin, a new probe and a new self-test on real bytes. "Series plan"
= the checks or boots of sections 3-4.

| stop | change that follows | CI build? | series plan changes? |
|---|---|---|---|
| flags | fix the DET_EXTRA in s4d2_run.sh (a harness file), re-probe | no | no |
| pattern | none: the lever does not work on this exe; the series does not start; back to build 1's lever code | yes, if a code fix follows | no checks change; nothing runs until a new probe passes |
| input | fix s4d2_run.sh (witness move, R2) or the reader, re-probe | no | no |
| w1 | W1 is not in the exe: rebuild from the W1 commit, re-pin, re-probe | yes | no |
| stimulus | as s4d1: fix or re-install the stimulus script (pinned hash changes) and re-probe | no (script only) | no |
| walk | as s4d1: fix the walk part of the stimulus, re-probe | no | no |
| tripwire | a Rule One finding: back to the code (the BAD slice names where), fix, rebuild | yes | no; the series waits for the fix |
| pose | design D9: a camera-heading stimulus (`set_actor_direction` toward the Duty squad), or "Caden's eyes only" (Overseer) | no for D9 (script); none for eyes-only | D9: none, the probe is re-run with it. Eyes-only: P1-P4 have no subject and the series is NOT run (Overseer decision) |
| uploads | same as pose | as pose | as pose |
| f5 | design D6: F5 fixed in xrGame (no `CalculateBones_Invalidate` from a render-time UpdateXForm), rebuild, re-probe | yes | no |
| npc | the one re-probe with the fallback NPC (`probe_b`); its name becomes PROBE_NPC | no | the NPC switch of every pose boot becomes the fallback name |
| attach | the same re-probe; if it still stops: the Overseer (recommendation: a stimulus that arms a named NPC; S4b not dropped) | no for a stimulus | with a stimulus: one more stimulus switch in every part-S boot, the same on all arms; dropping S4b would remove PA1/PA2/TWB32 and is not recommended |
| ld | back to the code (the record is not what the placement read: re-review S2 class), rebuild | yes | no |
| hx | the Overseer: find the XFORM writer after the render (code, CI build) or drop PA1's hx part | yes if the writer is fixed; no if dropped | dropped: PA1 loses its hx part and TWB32 its hx-based frame set (both registered as reduced) |
| item | (a) **a different slot list** in the stimulus (e.g. add 4, 7, 8, 9 or a device slot): a script edit, new stimulus hash, `probe_fix`, one probe boot (~7 min); works only if the bench actor carries something in a slot at all, which the NONE line does not tell. (b) **spawn an item** into the actor's hands (`alife_create_item` of a pinned section, then activate it): a script edit and one probe boot as (a), and it always gives an item, but it adds one server object to every boot, so the C counters and part A's copied bounds then rest on a spawn the earlier part A runs never had (identical in both arms, but new) | no (script) | (a) none: same switch, same boots. (b) no new switch either (it lives behind `-det_synth_item`), but 7's part-A rows must say the spawn is in both arms and A1 re-checks it. Recommended order: (a) first if a slot list can be read from the save desk-side, else (b) |
| optin | with `item` passed: the Overseer (the hands are drawn but not posed between ticks); with `item` stopped, the `item` row | no | without a subject, U1 would have only its nud/nus part, which needs the Overseer's word |

The TOL_LD rule of 7.1 is not a stop: its branch is a registration value (PROBE_TOL_LD), with no CI build and no change
of boots. Its branch c makes PA2's cluster part reported only.

### 2.3 Placeholders the probe supplies (rule from reading to registered bound). Kp = 3
| placeholder | probe reading | registered bound |
|---|---|---|
| `PROBE_P_LERP` | n=0 frames with pw form L and hd != hl | P1 minimum at K = floor(P x (K-1)/(Kp-1)/3) |
| `PROBE_P_TICK` | n=1 frames with pw form L and hp != hc | P3 minimum = floor(P/3) at every K (one tick frame per tick, ~5440 ticks at every K); TWB16 minimum = floor(P/3) |
| `PROBE_UPLOADS` | frames with an `up` column | P4 minimum = floor(P x K/Kp/3) |
| `PROBE_PA_CR` | n=0 frames with pa form C/R (bone corrections only, not I) and wd != ws | PA2 minimum = floor(P x (K-1)/(Kp-1)/3); TWB32 minimum at K = 2 = floor(P/2/3) |
| `PROBE_PA_CLUSTERS` | max ld leader-clusters per (kind, ba, bb) group of form-C lines, at the radius PROBE_TOL_LD (at 1e-3 when that is 0) | PA2: no group may show more |
| `PROBE_TOL_LD` | the TOL_LD rule of 7.1 applied to the probe's PROBE_LD_NOISE and PROBE_HAND_STEP (both printed) | PA2's cluster radius; 0 = PA2's cluster part reported only |
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
  - for (c): the pa kind, form and cls histograms, and the form-I count (not a correction);
  - the W2 hand link: lines where pa's drawn bone and pj are the same bone, and the max |pa hd - pj|;
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
| `item` (the bench actor has a filled slot of 3, 2, 1, 5, 6) | 85 % [G: a GAMMA bench save normally carries a rifle or a pistol and always a knife and bolts; not read from the save] |
| `optin`, given `item` passed | 70 % [G: the hands are re-posed every gated frame by the 4c HUD clock, design reading only] |
| `optin` overall (with the item in the stimulus) | about 60 % (was 30 % without an item) |
| **GO on the first probe** | **about 15 %** (was 10 %: `optin` is now likelier). The likely stops are `attach` and `optin`, both with registered plans |
| GO after the allowed re-probe | about 25 % (was 30 % "after the re-probe and R1"; R1 is now in the first probe, so only `attach` gains) |

### 2.5 After the probe
1. Fill CONST and the placeholders here.
2. Build a self-test case from the probe's real folder (its real witness, log and counters, with mutants made from
   them) and run the self-test.
3. Register: rename without -DRAFT and record the hashes of the scorer and the self-test.
4. Run the series.

## 3. Part A: off = stock with sim_tick 0 (6 boots; method of the s4d1 draft section 3, COPIED)
Tags alternate: s4p_s1 (Stance14), s4p_o1 (SimTick19), s4p_s2, s4p_o2, s4p_s3, s4p_o3. No lever, mask, walk, tracer
or pose switch; **one stimulus switch, `-det_synth_item`, on all six boots, both arms**:
`DET_EXTRA="-det_passes 100,200,320 -det_lua_streams -det_counters -det_synth_item"`. So part A's boots are identical
to part S's in what the actor holds, and the stimulus script is installed (an actor_on_update callback) in every
boot. This is a change from every earlier part A (no stimulus at all): see 7 for what it does to the COPIED bounds. Build 2's code that runs with sim_tick 0
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

`<base> -det_synth_item -det_synth_k K -sim_4d_mask M -det_synth_tw -det_synth_walk -det_synth_tracer -det_synth_pose
-det_synth_pose_npc PROBE_NPC`, with `<base>` = `-det_passes 100,200,320 -det_lua_streams -det_counters`.
`-det_synth_item` is in both arms of every pair (off/on, b1off2/off2, the must-fails against their on runs).

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
| F0 flags | every run (A and S): the engine's own command line carries exactly its registered switches (`-det_synth_item` in every run); its det_synth line is `(K, wc, tw, brk, mask)`; build-2 part-S runs log `pose witness on, npc PROBE_NPC`; b1off2 and part A log none; every run logs exactly one `item slot N section S` line (not NONE, no script error), and all 16 the same item | exact |
| PAT lever pattern | every part-S run except wc2: s4d1's L1 rules (K, n iff j = 0, F +1, j cycles, T +1 on tick frames, a = j/K, dt = float(0.033f/K), hud) on every line; cp1-3 on witnessed tick frames with T = `tick=` | exact by construction (device.cpp sim_tick_plan, sim_view_present); print tolerances x3 / x4 (same printf formats, device.cpp:546: re-derived, not only copied) |
| PWC must-fail (wallclock) | wc2: the pattern part holds and its F T j K n columns equal on2a's to the lower cp3 frame; a != j/K on >= 90 % of lines; on >= 30 form-L pw lines, u = a on EVERY one and u != j/K on >= 90 % | u = clamp(g_sim_render_alpha) (sim_pose.cpp:416-417) and the witness a is the same alpha (device.cpp:799-804). With the real clock alpha equals j/K to 6 decimals with chance ~1e-6 per frame [D, s4d1 L1W], so a pose that does not follow the lever's alpha, or a P2 u-rule that cannot see it, makes this fail. Expected ~100 % |
| S0 harness (harness) | off2a vs off2b: C = 0 at 1, 2, 3; cp1 ticks within 8 | exact / COPIED. FAIL -> SC and LB NO VERDICT |
| S0B stimulus ran | every part-S run: installed, start, highest `bullets fired` >= 50, no script error line, actor x/z range >= 1.5 m | COPIED (s4d1 S0B) |
| **SC on = off** (requirement 1) | C = 0 at cp1, 2, 3 and cp1 ticks within 8: on2a vs off2a, on2b vs off2a, on3 vs off3 (same exe, same flags, only the mask differs) | **exact**, section 5 |
| **LB bit-off = build 1** (requirement 2) | b1off2 (build 1 exe) vs off2a (build 2 exe, bit 16 off), same flags: C = 0 at 1, 2, 3; cp1 ticks within 8 | **exact, gated**, section 5. The build-1 side is SimTick18 (1.2) |
| TW tripwires | every part-S run except the breaks: 0 `* tw` lines of any slice (s2, s3, pose, attach); every part-S run: per frame to cp3 the number of `* tw` lines = the witness tw column, every line on a witnessed frame | exact (sim_4d_tw_bad counts and logs in one call, device.cpp:509-513) |
| TWB16 must-fail | brk16's OWN witness: every frame to cp3 whose pw is form L with hd != hl carries a `pose` BAD line; >= floor(PROBE_P_TICK/3) such frames; 0 BAD lines of attach, s2, s3 | per frame exact. The break writes draw into `mRenderTransform` inside the pose bracket and after the witness took hl (sim_pose.cpp:422-430), so hd != hl means a hashed field changed in that frame. On tick frames (u = 0) draw = prev != cur = live whenever the pose changed, so at least the probe's moving tick frames qualify (3x). The attach bracket hashes the bones only after the parent's evaluation (GameObject.cpp:1088-1095), so break 16 cannot trip `attach` [D] |
| TWB32 must-fail | brk32's OWN witness: every frame to cp3 with pa form C/R/I and hx != hxe carries an `attach` BAD line; >= floor(PROBE_PA_CR/2/3) such frames; 0 BAD lines of pose, s2, s3 (form I is included: the break fires on every corrected placement, GameObject.cpp `corrected`) | per frame exact: hx is hashed before the break (GameObject.cpp:1080), hxe after the join, so hx != hxe means the break changed XFORM() inside the attach bracket (:1119-1125). Frames with C/R and hx == hxe (draw bitwise equal to XFORM) are reported. The count floor assumes the break does not reduce form-C frames: a weapon is re-placed every rendered frame (HudItem.cpp:128), so its record is fresh again [D]; B-kind items stay stale until the next CalculateBones [G] |
| P1 drawn between ticks | on runs: n=0 frames with pw form L and hd != hl >= floor(PROBE_P_LERP x (K-1)/2/3) | 3x by construction |
| P2 pure function (S4a) | on runs, every form-L pw line: ctick = ptick + 1 and ctick = T; u = a (1.5e-6); hl = hc (the guard); if hu is present then hdu is present and hu = hdu (**the uploaded matrices are the draw array**); jd on lerp(jp, jc, u) within 1e-4 + 4 ulp (or jd = jc if PROBE_JOINT_LERP = 0) | the pair and guard parts are the code's own L conditions (sim_pose.cpp:408-418) and are named for the reader. The OUTCOME parts are u = a (the pose is drawn at the frame's alpha) and hu = hdu (the GPU gets the blend). The joint part checks the pivot: p = lerp(R_prev(j), R_cur(j), u) (sim_pose.cpp:220-224); its rounding is ~2 ulp of <= 2 m, so >= 20x under 1e-4 |
| P3 one tick behind | on runs: every tick frame (n = 1, a = 0) with form L has hd = hp; >= floor(PROBE_P_TICK/3) such frames with hp != hc | exact: u = 0 draws prev bitwise (sim_pose.cpp:242-251; an unchanged pose is cur = prev). Root and pose are one tick behind together (design 3.2) |
| P4 off = stock upload | off runs: p column all zero and pw absent on every line; up hu = hl on every frame with up; frames with up >= floor(PROBE_UPLOADS x K/3/3) | exact: with bit 16 off g_sim_pose_active is FALSE, so there is no capture, no storage and simR = nullptr (S4a notes 3) |
| **U1 opt-in** | on and off runs: the uu column present (W1) and nud = 0, nus = 0 on every line; on runs: n=0 frames with nposed > 0 >= floor(PROBE_U_POSED x (K-1)/2/3) | exact: an unmarked skeleton has no capture (SkeletonRigid.cpp:135 tests the mark) and no draw (sim_pose.cpp:437), so it is uploaded live: nud = 0 means every matrix handed to set_ca was the live bone ITSELF (address compare, 1.3), nus = 0 that no unmarked skeleton holds storage. nposed counts unmarked skeletons whose pose changed since the previous frame's upload, on consecutive frames only, so on n=0 frames it is posing between ticks, which is what made gammavr's VR models a risk (design 12 C1). 3x on the count |
| PA1 attach forms (S4b) | on and off runs, every pa line: hx = hxe when both are present (the render leaves the item's XFORM unchanged); forms P, O, S have wd = ws; on runs: C, R and I have cls c or p and u = a; **I has hd = hr** (1e-4); O has cls o; off runs: only forms S or P, cls `-`, u `-` | exact: P/O/S draw the 4b rule, and the witness's ws is that rule's matrix (GameObject.cpp:1099-1116, 1129-1130). Form I needs A_d == A_c bitwise, and every placement frame's origin is bone_a's (sim_attach_hands F.c = mR.c; A = da / ta; translate(da.c)), so the drawn and the read bone_a coincide: hd = hr exactly [D]. The hx part is stock behaviour under det serial mode (no XFORM writer after the render [D, with the CInventoryItem no-guard caveat of s4d1 NOTES]); the probe's `hx` rule settles it first |
| **PA2 item rides the drawn hand** (S4b outcome) | on runs: n=0 frames with pa form C/R (not I) and wd != ws >= floor(PROBE_PA_CR x (K-1)/2/3); form-C lines grouped by (kind, ba, bb): ld leader-clusters (radius PROBE_TOL_LD, 7.1) per group <= PROBE_PA_CLUSTERS; with PROBE_TOL_LD = 0 the cluster part is reported only | ld = the drawn item's origin in its DRAWN placement frame (inverse(parent_draw x A_d) x wd, GameObject.cpp:1021-1027). When the recorded bones are those the placement read, ld = inverse(A_c) x L = the item's offset O, a constant per item (re-review N2's proposal, "compare against its offset"). A wrong or torn record (re-review S2) makes it wander by the hand's motion. **ld = ls is a tautology** (re-review N2): only reported. Form R is excluded (right hand alone: ld varies with the two-hand geometry) |

Reported, not gated:
- per on, break and wallclock run:
  - the skeleton, guard and snap counts;
  - **F5:** render-only recalcs byte-different and byte-equal;
  - the pw form histogram;
  - the pa kind+form and cls histograms (how often c, p and o occur);
  - max |ld - ls| (rounding only);
  - for kinds B, T and N, the max of | |wd - hd| - |ld| |. That rigidity check is exact only for orthonormal frames,
    and W's first axis is not unit (S4b notes 2.1);
  - form I lines and their n=0 moved count (the S1 path; never counted as a correction);
  - **the W2 hand link:** on lines where pa has u, cls c or p, and `ba` equals pj's bone, the max Chebyshev distance
    |pa hd - pj jw|. Both are the same bone of the same mesh in the same frame through the same drawn root matrix, one
    via `sim_pose_bone_drawn` (draw x inverse(m2b), or the captured mTransform bitwise at an endpoint), one via
    `draw[b]` applied to the joint. Expected: rounding only, ~1e-6 m in model space plus 1e-5 of print rounding;
    anything near 1e-3 or more means the item's frame A_d is NOT built on the mesh's drawn hand (10.3). Reported, not
    gated: no probe count floor exists for it yet (registration may promote it if the probe shows >= 300 lines);
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
  | FAIL | PASS | PASS | clean | an ON-arm-only effect that no tripwire hashes: the heap allocation of SimPoseState (layout through addresses) or a bit-16 write outside the hashed fields (logic). Look first at sim_pose.cpp:316 |
  | PASS or FAIL | FAIL | PASS | clean | a gated-mode bit-off effect of build 2: the static layout (record GameObject.h:258, IKinematics pointer Kinematics.h:130) seen under the lever, or a bit-off code path |
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
### 6.2 Command per boot (series script s4d2_run.sh: written and revised 2026-11-27, `bash -n` only, requirement R2)
`DET_NOCLAIM=1 DET_BOX_TOKEN=<this run's claim token> DET_BOX_CLASS=shared DET_BOX_WORKER=engineperf BENCH_DISPLAY=:141
BENCH_TIMEOUT=<2000 for the probe; REG_ME_BENCH_TIMEOUT from PROBE_BOOT_S> DET_EXTRA="<base> -det_synth_item <lever>"
tools/det_run.sh <exe> <tag> 60 3` (det_run.sh 0b7a6edb5f575dea..., which runs the shared box checks again itself), started
in the background, then
`mv run/appdata/logs/synth_<tag>.txt out/det_<tag>/`. "60 3" is ignored under -det_passes. `<lever>` is per section
4's table, with the NPC = PROBE_NPC (empty in part A).
Modes: `probe`, `probe_b`, `probe_fix <n>` (2.1) and `series` (16 boots in the order of 6.1); each keeps its own new
state folder `out/s4d2_state/<mode>/` (series.log, plan.txt, done.txt (completed boots), boots.txt (every boot and its rc), claim_calls.txt, run_<tag>.log, stop.txt with
`class=done|box|fault`). `--preflight-only` alone runs only the shared box checks; `<mode> --preflight-only` runs
every start check of the mode; both exit before any state folder, queue entry or claim.

What the script does (each item is a refusal or a stop BY NAME):
- **box checks from one shared file**, perf-harness/common/box_preflight.sh (pinned by full sha256; the script
  refuses to start if it is missing, unreadable, not the pinned file, or lacks its marker, version or functions), all
  fail-closed (a check that cannot tell refuses): Caden's play lock (run/.caden-play.lock naming a live pid), a
  protected title, Sober (Roblox), an engine of the run tree, another live claim under `engineperf` (not the queue's
  handoff pre-claim, not this run's own token), `EXCLUSIVE` in ~/.claude/overseer/QUIET-REQUIRED or a QUIET-REQUIRED-<n>
  slot file, and display :0. Run at the start (refuse), before every claim attempt (wait), before every boot (twice,
  stop), and every ~5 s while a boot runs (all but the engine test; a hit stops this run's own det_run.sh). A real run
  refuses while any of the file's inputs is overridden (they exist only for its fixture self-test);
- refuses while any `PIN_ME_` value remains (exe names and hashes of build 2 and build 1, Stance14's and
  user_det.ltx.orig's full hashes), and in series mode while any `REG_ME_` value remains (PROBE_NPC, BENCH_TIMEOUT, the
  scorer and self-test hashes);
- pins, by full sha256, by name, before the first boot and again before every boot: the shared box file, build 2,
  build 1 (series), Stance14 (series), out/user_det.ltx.orig, the installed stimulus (1777fb68...), the bench saves
  bench1.scop / .scoc, run/appdata/axr_options_play.ltx, tools/det_run.sh (0b7a6edb5f575dea...), lib_engine_pids.sh, run_noguard.sh, and in
  series mode the registered scorer and self-test;
- checks cfg0 (sim_tick 0, r__framelimit 0 in run/appdata/user.ltx; tools/s4c_partA.sh:23) before every boot;
- runs the flag preflight of tools/s4c_partA.sh:10-16 for the base flags;
- display :141 only: BENCH_DISPLAY must be unset or :141, and the display must be free (no socket, no lock) before every
  boot; no other display is named;
- refuses a crash dump already in run/appdata/logs before the start and before every boot (it would be blamed on the
  next boot; it is not moved: not this run's);
- **options file before every boot** (as sf1): with the claim held and the box clear, run/appdata/axr_options_play.ltx
  is checked against 27d54b9f..., copied over run/gamedata/configs/axr_options.ltx (refused if that is a link or not
  a plain file in run/), and the copy checked. Each exe rewrites that file's version line when it boots (wu3, kj4p
  RESULTs), and s4d2 alternates three exes, so without it the arms of a pair would not start from the same bytes;
- refuses if any out/det_<tag>, out/det_<tag>_r1 or run/appdata/logs/synth_<tag>[_r1].txt of its plan exists; the
  reading chain of 2.1 (probe_b, probe_fix, series);
- after every boot: moves the witness (only through box_guarded: title, Sober, engine and play lock checked on the line
  before; refused = class box, nothing moved), then needs xray.log, det_alife_counts and
  det_counters at cp1, cp2 and cp3 (the scorer's C reads all six), exactly one item line that is not NONE and the same
  item as the first boot of the run, and for a lever boot a non-empty witness;
- **the in-run preflight**, after the boot of s4p_off2a (and of the probe) ends: every witness line has the registered
  138 tokens (58 for build 1); the engine's command line carries the base flags, `-det_synth_item` and the lever
  switches as whole tokens; exactly one det_synth line `k K wallclock W tw 1 break B mask M` with the boot's own values
  (full line, CR stripped); exactly one pose line naming the NPC (full line). Any miss stops the series. (Checked when
  the boot ends, not at cp1.)
- keeps one SHARED claim (`want` without EXCLUSIVE, re-sent every 60 s; `claim` with a per-run token in its text, signals
  held over the call; renewed by the same claim before every boot; exit codes only; the wait is capped at 6 h, then
  class=box). It releases only its own claim (by the claim-id the claim printed; by name only if its token claim is
  the only one under `engineperf`), withdraws the queue entry only if this run made it (no entry under `engineperf`
  before its `want`) and exactly one is there (KNOWN LIMIT: the queue is keyed by name only, so another engineperf
  job's `want` that landed on the same entry is withdrawn with it), kills and reaps its own det_run.sh on every exit
  (TERM, KILL after 60 s, from `date`), then makes sure no engine is left in run/prefix (`wineserver -k` there, 60 s;
  if one is left the claim is NOT released and the pids are logged), and sends the Overseer one notify line at the end
  (as sf1).
  det_pair.sh cannot be used: it passes one DET_EXTRA to all its runs.
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
  - any box check of 6.2 refusing before a boot or during one (the boot is stopped), or a refused claim renewal: stop
    (box);
  - a boot whose stimulus took no item, or another item than the run's first boot: stop (fault);
  - a crash dump already in run/appdata/logs before a boot: stop (fault);
  - a crash dump (det_run.sh's "CRASH DUMP present" or a .mdmp in the folder): stop (fault);
  - a timeout (det_run.sh rc 5): stop (fault);
  - any pre-boot check of 6.2 failing, or det_run.sh's own refusals (rc 2, 7): stop (fault);
  - a missing file after a boot, or the in-run preflight of 6.2: stop (fault).
- **After a stop mid-series (box or fault) the series is not resumed.** The state folder and every out/det_<tag> of the
  plan must be new, so a second attempt needs the Overseer's word, the stopped series' folders moved aside by hand
  (kept, named in the result), and a new start of all 16 boots. A box stop is not a retry and uses no part of 6.5.
### 6.5 One-retry rule
- **At most one retry in the whole series**, and only for a boot that exited early (det_run.sh rc 4, "engine exited
  early") with no crash dump. It is run once more right away as `<tag>_r1`, with the same switches (det_tag
  `<tag>_r1`). A timeout or a crash dump is never retried: it stops the series (6.4).
- The scorer reads `det_<tag>_r1` when it exists and names the substitution.
- A failure of the retry, or a second early exit anywhere in the series, stops the series (fault).
- A boot is never re-run because of what its output shows. The probe is never retried (only re-probed by 2.1).

**Scored ONCE, at the end:** `python3 -I s4d2/s4d2_score.py root=out out=s4d2/s4d2_score_result.txt`. The scorer
refuses an existing out file and has no override, so there is no re-score after a checker fix.

## 7. Margin table
| bound | value | expected | margin | derivation, or COPIED and why it still applies to build 2 |
|---|---|---|---|---|
| C = 0 (A1, A2, S0, SC, LB) | 0 | 0 | exact | section 5 |
| cp1 frame difference (A3, part A) | <= 8 frames | noise 0.75-2.7 [M, 4b pairs]; an extra A-Life pass = 17 frames (gaps 16-18 [M]) | 3.0x over the noise, **2.1x** under the signal | **COPIED** (4b R3). Applies because what decides the cp1 frame is identical in build 2's part-A boots: sim_tick 0 and no lever (cfg0, A5), the same save, det profile, det_run.sh and Stance14, and no change to the scheduler, A-Life, det harness or stimulus code (`git diff --stat 6acb77044 c216081fd`: 20 files, none of them there [D]). **Item switch (2026-11-27):** s4d2's part A adds `-det_synth_item` to BOTH arms; the 4b noise (0.75-2.7) was measured without it. The item is taken at actor update 300, before cp1 (pass 100 is ~1700 frames at 16-18 frames per A-Life call) [G], by the same script in both arms, so it moves the cp1 frame of both arms alike unless the exes react to it differently, which is what A3 is there to see. The noise side with the item is not measured: flagged [G]. A1 (stock against stock, both with the item) still proves the harness exact with it |
| cp1 tick difference (S0, SC, LB, part S) | <= 8 ticks | signal re-derived: an extra pass = one A-Life gap = 16-18 TICKS, because under the lever each tick runs one update and the A-Life call cadence is counted in updates (s4d1 L3, the same scheduler) [D]; noise in ticks: **not measured for any lever boot** [G: <= 2.7 as in frames] | **2.1x** under the signal; noise side unknown | re-derived on the signal side, flagged on the noise side. S0 (off2a vs off2b) is the first measure and is a harness check: a noisy harness gives NO VERDICT for SC and LB, not a false FAIL |
| A-Life calls to cp1 (A4) | >= 50 | 100 [M in every earlier part A; cp1 is set at pass 100 by -det_passes] | **2.0x** | **COPIED** (4b R4), flagged. Applies because it counts lines of the det_trace .sched file, whose writer build 2 does not touch [D, diff list], in part-A boots identical to the earlier ones except the exe and `-det_synth_item` (both arms; one actor_on_update callback and one slot activation, no A-Life call added [D: zz_eperf_synth.script]). It only separates "the trace covers the run" from "the trace started late" |
| PAT a, dt / hud | 1.5e-6 / 2e-3 | print rounding 5e-7 / 5e-4 | 3x / 4x | re-derived: same printf formats |
| walk range (S0B, probe) | >= 1.5 m | 3.0 m (2 x WALK_HALF 1.5, script) | **2.0x** | **COPIED** (s4d1 S0B), flagged. Applies because the walk is the pinned stimulus script's geometry, the actor is unmarked (S4a/S4b never touch it) and its movement code is not in build 2's diff [D]; the s2 column that measures it is build 1's code, unchanged. It separates "walk ran" (3.0 m) from "walk did not" (0 m) |
| bullets fired (S0B, probe) | >= 50 | ~171 | 3.4x | **re-derived for build 2**: one bullet per 30 updates from update 300 (script cadence [G, as s4d1]); build 2's boots run the same ~5440 updates to cp3 at every K and in every arm (one update per tick) [M, s4d1], so (5440 - 300) / 30 = 171. The probe shows it |
| part-A method (A1-A5) | 6 boots alternating Stance14 / build 2, C exact, A3, A4 | | | **COPIED** (4b section 2 via the s4d1 draft 3). Applies because the question is the same (off = stock with sim_tick 0) and the boots are the same: alternation, Stance14 b4ce9c77, `-det_passes 100,200,320 -det_lua_streams -det_counters` (flag preflight) **plus `-det_synth_item` in both arms (new, 2026-11-27: the earlier part A runs had no stimulus at all; the exact checks A1, A2, A5 are unaffected in kind because both arms carry it, and A1 re-proves the harness with it; A3 and A4 are flagged above)**, cfg0, save, det profile. What build 2 adds that part A judges: the unconditional mark writes (OnChangeVisual) and the layout; A2's exactness across layout changes held before [M] |
| position equal (PA1 wd = ws; I hd = hr) | 1e-4 m | 0: the same matrix printed twice / the same bone bitwise | exact in practice; 20x over print rounding | |
| u = a (P2, PA1, PWC) | 1.5e-6 | 0: the same float printed twice | 3x over print rounding | |
| joint lerp (P2) | 1e-4 + 4 ulp | ~2 ulp of <= 2 m = 5e-7 + print 5e-6 | >= 18x | |
| P1, P3, P4, U1, PA2 counts; TWB16, TWB32 floors | floor(probe scaled / 3) | probe scaled | 3x | by construction. TWB32 also assumes [G] the break does not reduce form-C frames; its frame set now includes form I, which only adds frames |
| PWC shares | >= 0.90 | ~1.0 [D] | 10 % allowed vs ~1e-6 expected | |
| PWC form-L floor | >= 30 | thousands [G] | > 30x | evidence floor |
| TOL_LD (PA2) | PROBE_TOL_LD by the rule of 7.1 (1e-3 if the probe allows) | see 7.1 | >= 2x on both sides by the rule, or the cluster part reported | 7.1 |
| PROBE_PA_CLUSTERS (PA2) | probe value at PROBE_TOL_LD, stop > 2 | 1 per item; 2 if an offset changes (strap) [G] | exact against the probe | |
| W2 hand link (reported) | none (reported) | ~1e-6 m + 1e-5 print [D] | | not gated: no probe floor yet |
| probe stops: counts | 300 | ~5440 x (K-1) for pose [G]; the NPC's frames unknown | ~36x for pose; unknown for attach and optin | these are decision rules, not verdict bounds |
| probe stops: npc share / f5 share | 0.5 / 0.5 | ~1.0 / ~0 [G] | **2x** / n.a. | decision rules, flagged |
| BENCH_TIMEOUT | 2000 s | <= 390 s [G] | 5.1x | reset from PROBE_BOOT_S |

### 7.1 TOL_LD, its own line (PA2's ld cluster radius)
- **Expected noise.** ld = inverse(parent_draw x A_d) x wd, in float32 from world coordinates up to ~270 m [G]. The
  float32 spacing at 256-512 m is 3.05e-5 m; an inverse, two products and a transform are each about half an ulp to
  one ulp, so ~1e-4 m typical and ~3e-4 m worst [G]. Print rounding adds 5e-6 per value.
- **Expected smallest signal, derived.** A record from the wrong capture (the re-review S2 class) builds A_c from bones
  one tick away from those the placement used, so ld moves by the hand's per-tick motion in its own frame: the hand's
  translation per tick plus |O| x its rotation per tick. Walking or turning: ~1 m/s x 33 ms = ~3 cm. Aiming or idle:
  ~1 mm per tick [G]. The smallest signal is therefore the idle hand, **~1e-3 m**.
- **Bound and headroom at 1e-3 m:** 3x over the worst noise (10x over typical), **about 1x against the idle signal**
  (no headroom) and 30x against walking.
- **What the probe measures** (printed by the reader, rule applied mechanically):
  - `PROBE_LD_NOISE`: per (kind, ba, bb) group of form-C lines, the per-coordinate spread (max - min) of ld inside the
    group's largest leader cluster at 1e-3 m; the max over groups. Floored at 1e-5 (two print roundings).
  - `PROBE_HAND_STEP`: the 10th percentile, over n=0 form-C lines with 0 < u < 1, of the per-tick hand step
    |hd - hr| / (1 - u) for cls c (hr is the newest capture) or |hd - hr| / u for cls p. So 90 % of frames carry at
    least this much signal.
  - `PROBE_TOL_LD` from N = PROBE_LD_NOISE and S = PROBE_HAND_STEP:
    - (a) if 2N <= 1e-3 <= S/2: **1e-3** (unchanged);
    - (b) else if 4N <= S: **sqrt(N x S)**, which is >= 2x over N and >= 2x under S; PROBE_PA_CLUSTERS is read at it;
    - (c) else: **0**, and PA2's cluster part is reported only.
- **What changes if the probe shows < 2x** (branch c): PA2 stays gated on its C/R count; its cluster part (the record
  is what the placement read) is reported only. That outcome then rests on the probe's `ld` stop (clusters <= 2 at
  1e-3, which catches only errors above ~1e-3) and on the desk reviews, and section 10.3 says so. No code change, no CI
  build, no change of boots or switches; the Overseer is told at registration.

Flags:
- **At 2x:** A-Life calls (COPIED), walk range (COPIED) and the probe's npc share.
- **At 2.1x:** cp1 against a real extra pass (COPIED for frames, re-derived for ticks).
- **Noise side unknown:** cp1 in ticks (no lever boot measured yet).
- **TOL_LD:** set by the probe (7.1); at its old fixed 1e-3 it would have had no headroom against an idle hand.
- **COPIED bounds, each with its reason above:** cp1 difference (frames), A-Life calls and gaps, walk range, and the
  part-A method. Bullets fired is re-derived.

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
  - the witness `synth_<tag>.txt`: 58 tokens for b1off2; 138 for build-2 part-S runs (126 and 132 are parsed so that
    a missing W1 or W2 fails or reports by name); one length per file; every column validated;
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
    sim_pose.cpp:316);
  - the build-1 fixture (LB FAIL naming GameObject.h:258);
- the three must-fails (PWC, TWB16, TWB32) "did not fail", and for each break also "a frame not caught" and "wrong
  slice";
- a missing witness and a malformed pw column, each failing by name in every check that reads it;
- a witness without the W1 column (U1 fails by name);
- the W1/W2/F1 cases added 2026-11-27 (second revision):
  - the good set carries form-I lines (every tenth tick's render-only frames) and pj columns;
  - U1: an unmarked skeleton with storage, and one never posed (count part);
  - PA1: an I line with hd != hr, and form I in an off run;
  - PA2: every correction turned into form I (F1: the count must FAIL);
  - TWB32: an I frame of the break without its BAD line;
  - W2: a pj mismatch (reported value shown, no verdict change), a witness without pj (132 tokens, reported absent),
    and pj on a line whose pn is not L (malformed by name);
  - PROBE_TOL_LD 0: PA2's cluster part reported only;
- the item switch (third revision): every fixture boot carries `-det_synth_item` and logs `item slot 3 section
  wpn_ak74`; F0 mutants for the switch missing in a part-A run, `item NONE`, no item line (b1off2), and one run taking
  another item;
- the `_r1` retry, both joint rules, and the refusals (placeholders; a second scoring onto the same out file);
- 18 probe-mode cases (two added: `item NONE` stops on `item`, the switch missing stops on `flags`): GO with the expected CONST lines (PROBE_PA_CR counts C/R only; the I count, the TOL_LD branch
  and the W2 link printed), CRLF, one per stop rule (w1, pose, f5, npc + attach, ld, hx, optin, tripwire, stimulus,
  malformed input, missing input), attach stopped when every correction is form I, and the TOL_LD rule's branches b
  and c.

**Result 2026-11-27 23:37 (third revision, with the item switch):** `S4D2 SELFTEST: PASS (76/76)`, folder
s4d2/selftest-runs/20261127-233722-880626 (NOTES.md 7). Hashes at that run (not registered): s4d2_score.py
76a1ab794ce98047..., s4d2_selftest.sh 4a53bd2d97016fb5.... (Second revision: 70/70.) The shared box checks have their
own self-test: common/box_preflight_selftest.sh, v2 after the second review `BOX SELFTEST: PASS (143/143)` (2026-11-28
00:15), folder common/selftest-runs/20261128-001517-1467978. This self-test re-run 2026-11-28 00:19: `S4D2 SELFTEST:
PASS (76/76)`, folder s4d2/selftest-runs/20261128-001900-1524685 (scorer and self-test unchanged).
**It must be re-run with a case built on the probe's real folder before registration.** The hashes of the scorer and
the self-test are to be filled at registration.

## 9. Predictions (fixed before any run)
- **Probe:** see 2.4 (GO on the first boot about 15 %; after the re-probe about 25 %).
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
  - **in both arms of the same exe:** every CGameObject grows by ~280 B (`m_sim_attach`, GameObject.h:258) and
    IKinematics by a pointer (`m_sim_pose`, Kinematics.h:130);
  - **in the bit-16 ON arm only:** ~21 KB is allocated per captured skeleton from the engine heap
    (`xr_new<SimPoseState>` in sim_pose_capture_body, sim_pose.cpp:316), freed at Spawn or destruction.

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
  - **SC:** the storage allocation in `sim_pose_capture_body` (sim_pose.cpp:316). Then search xrGame/xrAICore for
    pointer-keyed containers walked during the offline A-Life update.
  - **LB or A2:** the record member `CGameObject::m_sim_attach` (GameObject.h:258), then `IKinematics::m_sim_pose`.
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

  **W2 (coded, uncommitted) closes it on the witness NPC:** the `pj` column prints that NPC's `bip01_r_hand` joint
  as its mesh is drawn, in world, through the same drawn root matrix the item's child rule used. On lines where pa's
  `ba` is that bone and pa has u (cls c or p), |pa hd - pj| compares the bone the item's frame is built on with the
  mesh's drawn hand directly (not only distances, as first proposed). It is reported, not gated (no probe floor yet);
  a value near 1e-3 m or more would mean A_d is not the mesh's drawn hand. It covers only the witness NPC and only
  when its item hangs on bip01_r_hand.
- **If PROBE_TOL_LD is 0** (7.1 branch c), PA2 shows the correction's count but not, at fine scale, that the record is
  what the placement read.
- **ld = ls is a tautology** (re-review N2): only reported.
- pa has no child id, so a weapon switch shows as a new ld cluster (covered by PROBE_PA_CLUSTERS).
### 10.4 pw comparability (S4b review O2)
pw is the first skeleton evaluated in the frame, and the child rule (and the tw bracket) can move that evaluation
earlier. So pw is compared only line by line, never across runs, exes or switch sets.
### 10.5 The review of c216081fd, and the W1 commit
c216081fd's review (plan-facts/stage4d2-c216081fd-review-2026-11-27.md) found nothing blocking. Its SHOULD FIX F1 (the
S1 path reported as a bone correction) is fixed by the new form letter I, which the probe's attach count, PA2's count
and the PROBE_PA_CR reading exclude, and which TWB32 and PA1 include. N5 (the exact `parent_draw * L`) and N6 (the
comment) are applied. The witness format changed (uu, pj, form I) and the scorer was changed BEFORE the probe; 1.1 is
updated. The W1 commit itself still needs its independent review; any change from it means a new CI build, pin, probe
and real-bytes self-test case.

## 11. What these runs cannot show
- Whether a blended NPC, its weapon, a torch or a corpse LOOKS right (Caden's eyes).
- The light, particles, wallmarks, script-attachment lights and the muzzle flash, which still use live bones (S4b
  notes 3).
- Real gated mode: races between the capture and the first evaluation, real alpha, multi-tick frames.
- R1 soft skinning, skeleton LOD switches, mutants specifically (unless one is pw), ragdolls.
- The VR path itself: U1 uses the HUD hands as a stand-in for a self-posed unmarked model; gammavr's models are not run.
- **U1 is blind to two kinds of break** (review N1-1, item 1 section 3 a/b):
  - a blend that reaches an unmarked skeleton through an RM_SINGLE or soft-skinned (RM_SKINNING_SOFT) mesh: the
    witness hooks only the hardware-skinned upload loop (SkeletonX.cpp:107-121), so such a mesh is never counted;
  - a skeleton marked by mistake (a HUD or actor model given the mark): uu classes it as marked and never looks at it.
    Only a drop of nposed below its floor would show it, and only for the HUD hands.
- U1's nposed can be raised once by a freed skeleton's address reused one frame later (1.3); it is only a floor.
- Anything after A-Life pass 320, any other save, native Windows.
