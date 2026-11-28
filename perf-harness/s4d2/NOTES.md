# s4d2 notes (2026-11-27, desk only)

These notes go with PREREG-s4d2-npc-pose-DRAFT.md, s4d2_score.py and s4d2_selftest.sh. Nothing in this folder has
run against the engine. Code: wt-simtick perf/tick-4d HEAD c216081fd (never compiled).

## 1. How to read the new witness columns
Line lengths: 58 tokens is build 1 (b1off2), 126 is build 2 with `-det_synth_pose`, 132 is build 2 plus W1 `uu`.
Every reader strips `\r`, because engine files under Wine may be CRLF.

| column | values (sim_pose.cpp:630-740) | read it as |
|---|---|---|
| `p` (6) | neval nlerp nguard nsnap nro_diff nro_equal | per-frame counts. The last two are F5: render-only recalcs that differ from or equal cur |
| `pw` (19) | form u ptick ctick hd hl hp hc hu hdu jd(3) jp(3) jc(3) | first marked skeleton evaluated this frame. Form L: blended. Sn, Sb, Sg, Ss: stock with a reason |
| `up` (2) | hu hl | first skinned leaf uploaded this frame, any skeleton. Stock means hu == hl |
| `pn` (5) | form u hd hl hsim | the named NPC; `X` = found but not evaluated |
| `pa` (26) | kind form cls ba bb u wd(3) ws(3) hd(3) hr(3) ld(3) ls(3) hx hxe | the NPC's active item at its first placement of the frame |
| `uu` (4, W1, not in code yet) | nu nud nus nposed | unmarked-skeleton uploads, uploads off live, uploads with storage, and leaves posed since the previous upload |

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
- **`nposed` can be inflated by a reused skeleton address after a free.** It is only a count floor, never a pass
  condition.

## 3. Required code changes found (none made; engine code was not edited)
- **W1 (required):** the `uu` column in sim_pose_wit_upload / sim_pose_witness_reset / sim_pose_witness_print
  (prereg 1.3). Without it no witness column can isolate an unmarked skeleton, so the opt-in check U1 has no subject.
- **W2 (optional):** pn prints the `bip01_r_hand` joints, so the item's drawn hand can be tied to the mesh's drawn hand
  (prereg 10.3). Without W2 that link is desk-only.
- **R2 (series script, not engine):** move `synth_<tag>.txt` into `out/det_<tag>/` after every boot.
- **Possible, decided by the probe:**
  - the F5 fix in xrGame (design D6) if share > 50 %;
  - the camera-heading lever (design D9) if `pose` stops;
  - s4d1 R1 (active item) if `optin` stops.

## 4. Verified vs assumed
**Verified at the desk (file:line read at c216081fd, or by mechanical count):**
- the column formats and their absent-dash counts;
- the log line formats;
- u = clamp(alpha) = the witness a;
- the mask and break ranges;
- the break 16 and 32 write sites;
- the opt-in mark path;
- the SimPoseState allocation site (sim_pose.cpp:292), and the layout members (GameObject.h:257, Kinematics.h:130);
- build 1 parses no pose switch;
- det_run.sh does not move the witness file.

**Assumed [G]:**
- every per-boot time beyond the s4d1 figures, and the ~1 ms/frame hashing cost;
- that NPCs are drawn and blended headless;
- that the Duty NPC is armed and in view;
- that render-only recalcs are byte-equal (F5);
- that the HUD hands are posed between ticks and drawn only with an active item;
- TOL_LD's noise and signal levels;
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
it. The cases include:
- the counters-differ-with-tripwires-clean case (SC FAIL naming "heap layout" and sim_pose.cpp:292) and its LB
  sibling (naming GameObject.h:257);
- missing-file-by-name, CRLF and absent-dash cases;
- the three must-fails passing (each a FAIL);
- 13 probe cases.

Fixtures are synthetic. The self-test must be re-run with a case built on the probe's real bytes before registration.

## 7. Self-test result
`S4D2 SELFTEST: PASS (57/57)` (run 2026-11-27 with nice -n 19, after the QUIET-REQUIRED check; output read: 57 `ok` lines, no `BAD`).
Folder: /home/caden/Projects/StalkerGameEngine/wt-render-split/perf-harness/s4d2/selftest-runs/20261127-214140-1115043 (selftest_output.txt; 17 MB on disk with hard links and symlinks)

No memory file was written under ~/Documents: the brief forbids writing there.
