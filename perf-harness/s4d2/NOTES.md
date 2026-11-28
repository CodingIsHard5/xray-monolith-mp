# s4d2 notes (2026-11-27, desk only)

These notes go with PREREG-s4d2-npc-pose-DRAFT.md, s4d2_score.py, s4d2_selftest.sh and s4d2_run.sh. Nothing in this
folder has run against the engine. Code: wt-simtick perf/tick-4d c216081fd plus the uncommitted W1 edits (the "W1
commit" to be: uu, pj, pa form I, N5, N6; plan-facts/stage4d2-w1-code-notes-2026-11-27.md). Never compiled.

## 1. How to read the new witness columns
Line lengths: 58 tokens is build 1 (b1off2); 138 is build 2 with `-det_synth_pose` as the W1 commit prints it (uu and
pj). 126 (c216081fd, no W1) and 132 (W1 without pj) are still parsed so that their absence fails or reports by name.
Every reader strips `\r`, because engine files under Wine may be CRLF.

| column | values (sim_pose.cpp:630-740) | read it as |
|---|---|---|
| `p` (6) | neval nlerp nguard nsnap nro_diff nro_equal | per-frame counts. The last two are F5: render-only recalcs that differ from or equal cur |
| `pw` (19) | form u ptick ctick hd hl hp hc hu hdu jd(3) jp(3) jc(3) | first marked skeleton evaluated this frame. Form L: blended. Sn, Sb, Sg, Ss: stock with a reason |
| `up` (2) | hu hl | first skinned leaf uploaded this frame, any skeleton. Stock means hu == hl |
| `pn` (5) | form u hd hl hsim | the named NPC; `X` = found but not evaluated |
| `pa` (26) | kind form cls ba bb u wd(3) ws(3) hd(3) hr(3) ld(3) ls(3) hx hxe | the NPC's active item at its first placement of the frame. Forms: C, R = bone corrections; **I** = corrected with C = identity (bones drawn as read, only the parent moved: c216081fd's S1 path, review F1), NOT a bone correction; O, P, S uncorrected |
| `uu` (4, W1) | nu nud nus nposed | per frame: unmarked-skeleton leaf uploads; of them, leaves handed any matrix that is not the live bone itself (address compare); of them, leaves whose skeleton holds storage; unmarked SKELETONS whose live pose differs from their upload in the immediately previous frame |
| `pj` (4, W2) | bone jw(3) | the witness NPC's bip01_r_hand (else last bone) joint as its mesh is drawn, in world; 4 dashes unless pn is L and the NPC's drawn matrix was set this frame. Compare with pa hd when pa ba == bone |

## 2. Known traps
- **ld == ls is a tautology** (re-review N2). It is only reported. The real check (PA2) is that ld is CONSTANT per
  (kind, ba, bb) across frames, because then it equals the item's offset in its drawn placement frame.
- **pw is comparable only within one run, line by line.** Which skeleton comes "first" changes with exe and switch set
  (S4b review O2).
- **Compare hu with hdu, never with hd.** hu hashes `count` matrices of one leaf; hd hashes all bones.
- **`up` hu == hl holds by construction when the draw is stock** (LL_GetTransform_R = mRenderTransform). It proves
  nothing about WHICH skeleton was uploaded. That is why W1 is needed.
- **pa is present in off runs too.** The child rule runs on every gated frame. Off-arm pa is kind `-`, form P or S,
  cls `-`, u `-`.
- **Absent values are dashes, with fixed counts:** pw 19, pn 5, pa 26, up `- -`. A pw with form L must carry its tick
  pair (the scorer enforces this).
- **Hashes are 16 lowercase hex digits; compare them as strings.**
- **The F5 counters are written only while captures run.** They are 0 in off runs by design, not as evidence.
- **tools/det_run.sh does not move `synth_<tag>.txt`** (lines 44-47). The series script must (prereg 1.4 item 6).
- **Build 1 (SimTick18) does not parse the pose switches** (git grep at 6acb77044). It gets the same command line and
  prints 58 tokens. SimTick18 itself does not exist yet in run/bin (only SimTick14-17 and Stance14).
- **`nposed` can be inflated by a reused skeleton address after a free** (same bone count, consecutive frames). It is
  only a count floor, never a pass condition. A full 32-slot table can only lower it.
- **`nposed` compares consecutive frames only.** A skeleton not uploaded in the previous frame is not counted in this
  one. On an n=0 frame no tick ran since the previous frame's upload (det serial: ticks, bones thread and game thread
  run before the render), so a count there is posing between ticks.
- **`nud` is an address compare, not a byte compare.** A draw array with bytes equal to the live bones still counts.
- **Form I is not a correction.** PROBE_PA_CR, the probe's attach rule and PA2's count use C and R only. TWB32 and PA1
  include I (the break fires on it; PA1 checks I has cls c/p, u = a and hd = hr).
- **The unmarked skeletons headless:** only the first-person HUD hands and HUD item models are expected to be posed
  between ticks, and they are drawn only while an attached item needs rendering (player_hud.cpp:1338-1341). Without
  s4d1 R1 (an active item) expect nposed about 0 and the probe's `optin` stop.

## 3. Code changes (made 2026-11-27, uncommitted, in wt-simtick src/xrEngine and src/xrGame only)
- **W1 (required):** the `uu` column in sim_pose_wit_upload / sim_pose_witness_print (prereg 1.3). Reset with the
  other per-frame counts; the 32-slot table is static and never reset.
- **W2 (optional, done):** the `pj` column (prereg 10.3). Reported in the scorer, not gated.
- **F1:** pa form `I` for the S1 path. **N5:** that path draws `parent_draw * L` without the multiply by identity
  (SimAttachInfo::c_identity). **N6:** the comment at the top of sim_pose.cpp.
- **R2 (series script, harness):** s4d2_run.sh written, `bash -n` only.
- **Still possible, decided by the probe:**
  - the F5 fix in xrGame (design D6) if share > 50 %;
  - the camera-heading lever (design D9) if `pose` stops;
  - s4d1 R1 (active item) if `optin` stops.
- **TOL_LD** is now set by the probe (prereg 7.1): CONST `PROBE_TOL_LD` (0 = PA2's cluster part reported only).

## 4. Verified vs assumed
**Verified at the desk (file:line read at c216081fd, or by mechanical count):**
- the column formats and their absent-dash counts;
- the log line formats;
- u = clamp(alpha) = the witness a;
- the mask and break ranges;
- the break 16 and 32 write sites;
- the opt-in mark path;
- the SimPoseState allocation site (sim_pose.cpp:316), and the layout members (GameObject.h:258, Kinematics.h:130);
- build 1 parses no pose switch;
- det_run.sh does not move the witness file;
- (W1 commit) the HUD hands' render path reaches the upload hook only through render_hud with an attached item; the HUD
  models are re-posed every gated frame in sim_view_present; renderer_r4 in the det profile.

**Assumed [G]:**
- every per-boot time beyond the s4d1 figures, and the ~1 ms/frame hashing cost;
- that NPCs are drawn and blended headless;
- that the Duty NPC is armed and in view;
- that render-only recalcs are byte-equal (F5);
- that the HUD hands are posed between ticks and drawn only with an active item;
- TOL_LD's noise and signal levels (the probe now measures both: PROBE_LD_NOISE, PROBE_HAND_STEP);
- that the actor's body and legs are posed in ticks only (they would add to nposed otherwise);
- that break 32 does not reduce form-C frames;
- every probability in prereg 2.4 and 9.

The probe exists to settle the first five.

**Not verified at all:** that the code compiles, and any real byte the engine writes.

## 5. Differences from the s4d1 template
- There is no part L. PAT re-checks the lever pattern on every part-S witness instead.
- PAT is a gated check but not a harness check, so a missing witness fails by name and does not cascade to NO
  VERDICT.
- New gated checks:
  - SC (on = off, same exe);
  - LB (build 2 bit-off = build 1, gated, build-1 side SimTick18);
  - TWB16, TWB32 (must-fail, per frame);
  - P1-P4, U1, PA1, PA2.
- BENCH_TIMEOUT is 2000 s, not s4d1's 1900 s: 1900 s would be 4.9x the K=3 guess after the added hashing.

## 6. The self-test
`nice -n 19 bash s4d2_selftest.sh` builds synthetic fixtures. Each case must change exactly the verdicts listed for
it. The scorer copy the self-test imports writes a `__pycache__` into its stamp folder; remove it by literal path. The
cases include:
- the counters-differ-with-tripwires-clean case (SC FAIL naming "heap layout" and sim_pose.cpp:316) and its LB
  sibling (naming GameObject.h:258);
- missing-file-by-name, CRLF and absent-dash cases;
- the three must-fails passing (each a FAIL);
- the W1/W2/F1 cases (form I in the good set; I-only corrections fail PA2 and stop the probe's attach rule; U1 storage
  and never-posed; TWB32 on an I frame; pj mismatch reported, pj absent, pj without pn L malformed; PROBE_TOL_LD 0);
- 16 probe cases (TOL_LD rule branches a, b, c included).

Fixtures are synthetic. The self-test must be re-run with a case built on the probe's real bytes before registration.

## 7. Self-test result
`S4D2 SELFTEST: PASS (70/70)` (second revision, run 2026-11-27 22:10 with nice -n 19, right after
`grep -q EXCLUSIVE ~/.claude/overseer/QUIET-REQUIRED` found no match (the file does not exist); output read: 70 `ok`
lines, no failed case).
Folder: /home/caden/Projects/StalkerGameEngine/wt-render-split/perf-harness/s4d2/selftest-runs/20261127-221049-2137883
(selftest_output.txt; 21 MB on disk with hard links and symlinks). Its `__pycache__` and the previous folder
(20261127-214140-1115043, 57/57, first revision) were removed by their literal paths.

## 8. The series script s4d2_run.sh
- Modes `probe`, `probe_b`, `series`; state in `out/s4d2_state/<mode>/` (new each time).
- Refuses while `grep -q EXCLUSIVE ~/.claude/overseer/QUIET-REQUIRED` matches, while any PIN_ME_ remains (exe names and
  hashes, the stimulus hash), and in series mode while any REG_ME_ remains (NPC, timeout, scorer and self-test hashes).
- Display :141 only, free before every boot. Shared claim, renewed before every boot.
- Moves `run/appdata/logs/synth_<tag>.txt` into `out/det_<tag>/` after every boot; a lever boot without it stops by name.
- Stops on the first crash dump, timeout, protected title, failed pre-check, missing file, or failed in-run preflight
  (after s4p_off2a and after the probe). One retry `<tag>_r1` in the whole series, only for an early exit (rc 4).
- No `rm`. Times from `date`. Not run: `bash -n` only.

No memory file was written under ~/Documents: the brief forbids writing there.
