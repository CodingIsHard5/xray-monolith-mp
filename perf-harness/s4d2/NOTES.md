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
  - a different slot list or an item spawn in the stimulus if `item` stops (prereg 2.2.1); s4d1 R1 itself is now
    the switch `-det_synth_item` on every boot.
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
`S4D2 SELFTEST: PASS (76/76)`, re-run 2026-11-28 00:19:00 with nice -n 19 after the second-review fixes (the scorer and
this self-test are unchanged; only s4d2_run.sh changed, which this self-test does not run). **That run started 12 s
after rootsim took an EXCLUSIVE claim (00:18:48)**: the check before it found EXCLUSIVE in QUIET-REQUIRED, but the
check was not used as a gate (my error; reported). Folder:
/home/caden/Projects/StalkerGameEngine/wt-render-split/perf-harness/s4d2/selftest-runs/20261128-001900-1524685 (22 MB);
the previous folder 20261127-233722-880626 was removed by its literal path.
Shared box checks (v2): `BOX SELFTEST: PASS (143/143)`, 2026-11-28 00:15:17 (no EXCLUSIVE then), folder
/home/caden/Projects/StalkerGameEngine/wt-render-split/perf-harness/common/selftest-runs/20261128-001517-1467978. The
folders 20261127-234139-942947 and the reviewer's 20261127-235147-1082074 were removed by their literal paths; the
failed first v2 run 20261128-001450-1457920 (140/141, a test-side false positive, fixed) is still there.

## 8. The series script s4d2_run.sh (revised 2026-11-27 after the review and the shared-checks order)
- Modes `probe`, `probe_b`, `probe_fix <n>`, `series`; state in `out/s4d2_state/<mode>[<n>]/` (new each time).
  `--preflight-only` alone: only the shared box checks; `<mode> --preflight-only`: every start check of the mode.
- Box checks come from perf-harness/common/box_preflight.sh (pinned sha256, marker and version checked; see
  common/README.md). No private copy of them is left in the script. Real modes refuse while any BOX_* input or
  S4D2_BOX_LIB(_SHA) is set.
- Refuses while any PIN_ME_ remains (exe names and hashes, Stance14 and user_det full hashes), and in series mode
  while any REG_ME_ remains. The stimulus pin is filled: 1777fb68... (with `-det_synth_item`).
- `-det_synth_item` is on every boot (ITEM in the script, after BASE). After every boot the item line must be one
  `slot N section S` line, the same as the run's first boot.
- det_run.sh (e2980094..., retrofitted 2026-11-27, revised 2026-11-28 twice) gets DET_BOX_TOKEN / DET_BOX_CLASS=shared / BENCH_SAVE=bench1 /
  DET_BOX_WORKER / BENCH_DISPLAY=:141 and runs the shared box checks itself before it touches anything, again on the
  line before each of its writes under run/, and touches nothing under run/ after a stop; its rc 3 is a box stop here.
- Display :141 only, free before every boot. Shared claim with a per-run token, renewed by the same claim before every
  boot, wait capped at 6 h, `want` re-sent each poll.
- Options file restored before every boot (sf1's reason: each exe rewrites its version line).
- Moves `run/appdata/logs/synth_<tag>.txt` into `out/det_<tag>/` after every boot (inside box_guarded: title, Sober,
  engine, play lock on the line before; the options copy likewise);
  a lever boot without it stops by name. Needs det_alife_counts and det_counters at cp1, cp2, cp3.
- Stops on the first crash dump, timeout, box condition (before or during a boot), failed pre-check, missing file,
  item NONE, or failed in-run preflight (after s4p_off2a and after the probe). One retry `<tag>_r1` in the whole
  series, only for an early exit (rc 4).
- No `rm`. Times from `date`. Not run: `bash -n`, plus its `--preflight-only` box mode inside the common self-test
  (fixtures).

Review findings (plan-facts/stage4d2-w1-and-runscript-review-2026-11-27.md), what was done:
| finding | done |
|---|---|
| B2-1 play lock, own claim, Roblox before each boot | shared file checks, before start / claim / every boot / during a boot |
| S2-1 title fails open | shared box_ok_title: any exit but 1 refuses |
| S2-2 claim granted with CLAIMED=0 on a signal | TERM/INT/HUP held over every claim call (`held`); release decided from the claim's receipt (claim-id), not CLAIMED |
| S2-3 det_run in the foreground | background + `wait`; finish TERMs it, KILL after 180 s (third review S3-7), reaps; HUP trapped |
| S2-4 plan on stdin | `read -u 3 ... 3< plan.txt`; every child `</dev/null` |
| S2-5 probe_b renames before the gates | rename inside the loop after every gate passed |
| S2-6 probe_b not gated | needs the first reading ending in STOP with only npc / attach codes |
| S2-7 unbounded wait, `want` once | 6 h cap (class=box), `want` every poll |
| S2-8 saves not pinned | bench1.scop / .scoc pinned (sf1 values, matched 2026-11-27) |
| N2-1 GO anywhere | last non-empty line, CR stripped, exact |
| N2-2 unanchored greps | whole-token command-line match; full-line det_synth and pose lines with the boot's own k, wallclock, break, mask |
| N2-3 retry-path mv unchecked | checked and logged (a failure is logged, not a stop: the retry's file name cannot clash) |
| N2-4 leftover dump | refused at start and before every boot |
| N2-5 prefix pins | full-hash PIN_ME_ placeholders, today's values in the comments |
| N2-6 mismatches 2-4 | `probe_fix <n>`, the reading chain, all six counter files |
| N2-7 options restore, notify | both done, reasons in the header |
| N2-8 resume | prereg 6.4: not resumed; a new full series after the Overseer's word |
| mismatch 5 (in-run regex) | exact k, wallclock, break, mask |

Second review (plan-facts/box-preflight-second-review-2026-11-27.md), in this script: box_preflight.sh v2 (no `check`
call, claim records read from the tool's files, Sober by flatpak, title linger, PTC_* refused); the options copy and
the witness moves inside box_guarded (B-1/B-2); after det_run.sh ends or is KILLed, `wineserver -k` in run/prefix and a
60-s wait from date until no engine of the run tree uses it, else the claim is NOT released and the pids are logged
(S-2); `want` only when no entry under engineperf exists, `withdraw` only of an entry this run made while exactly one
exists (S-3, known limit in common/README.md); the TERM grace counted from date (N-14); done.txt for completed boots
only, boots.txt for every boot with its rc (N-13); the claim tool's tuning variables dropped for its calls.

## 9. Exit paths of s4d2_run.sh (what each one releases and stops)
"Own claim" = released by its claim-id from claim_calls.txt; with no id, by name only if the claim carrying this run's
token is the only claim under engineperf; else left to expire (<= one boot timeout + 10 min). "Own child" =
this run's det_run.sh (BP) and its sleep (SP); nothing else is signalled.

| exit path | when | stop record | queue entry | claim | child | notify |
|---|---|---|---|---|---|---|
| refusal before the state folder (exit 2; box-only mode 1/2/3) | usage, BENCH_DISPLAY, shared file bad, overrides in a real run, placeholders, box check, pins, cfg0, display, dump, folders, reading chain | none (no folder yet) | none made | none made | none started | no |
| `--preflight-only` / `<mode> --preflight-only` (exit 0) | all checks clear | none | none | none | none | no |
| claim wait capped (6 h) | box never clear or claim refused | class=box | withdrawn only if this run made the entry and it is the only one under engineperf | none unless a claim call committed one: then own claim | none | yes |
| gate stop before a boot | box check, claim renewal, display, cfg0, pins, dump, options restore, folders | class=box or fault | none (left the queue at the claim) | own claim | none running | yes |
| box condition during a boot | play lock, title, Sober, another claim of ours, EXCLUSIVE (every ~5 s) | class=box, the reason names the check and the boot | none | own claim, only once no engine is left in run/prefix (else NOT released, pids logged) | TERM det_run.sh (on a signal its cleanup touches nothing under run/: wineserver -k in run/prefix, its own Xvfb/openbox, the files left listed by name; user.ltx is NOT restored), KILL after 180 s (from date), reaped; then wineserver -k in run/prefix if an engine is left | yes |
| boot result stop | rc 3, 4 (second), 5, other, crash dump (9) | class=box (rc 3) or fault | none | own claim | already ended | yes |
| after-boot stop | missing file, item NONE / different, a box refusal before a move (nothing moved), in-run preflight | class=fault (box for a box refusal) | none | own claim | already ended | yes |
| TERM / INT / HUP at any point after the state folder | the trap | class=box, `interrupted=<tag>` if a boot ran | as "claim wait capped" | own claim (a signal during a claim call is held until the call returns, so its receipt is on disk) | as "during a boot" | yes |
| a signal during finish | ignored (`trap '' TERM INT HUP` at its start) | finish completes | | | | |
| normal end | plan finished | class=done | none | own claim | none | yes |
| bash dies another way (KILL of the driver) | cannot be trapped | not written | left (the queue's TTL) | left to expire | det_run.sh keeps running to its own end or timeout: NOT covered | no |

No memory file was written under ~/Documents: the brief forbids writing there.

## 10. Fix round 2026-11-28 (plan-facts/s4d1-driver-review-2026-11-28.md: S-1, S-2, S-3, N-1, N-5 hold here too)
s4d2_run.sh is now sha256 f885501efda874ce6d6bd1b6084eec730cd9458294107d5ab2c05de5fc0bd29d (was 2777c68f...), the same changes
as s4d1_run.sh (s4d1/NOTES.md 9.8):
- S-1: one `claim` per call (acquire); before every boot `renew engineperf <min>` with QR_CLAIM_ID = that claim's id
  (quiet-required.sh renew_own extends exactly that record when nested, keeps the id with one record), never a second
  `claim`; finish()'s release_mine releases every distinct `claim-id:` in claim_calls.txt. Section 9's "own claim" now
  means all of them.
- S-2: box_claim_has_token in box_gate and watch_boot: a lost claim is a box stop.
- S-3: engines_gone right after det_run.sh returns (rc 10 = fault stop "an engine still used run/prefix").
- N-1: TERM/INT/HUP held from the launch until BP is set. N-5: deadline before every claim attempt, sleep cut to it.
Not changed here: CLAIM_MIN (one boot's timeout + 10 min, renewed before every boot), test content, modes.
Checked: `bash -n`; `--preflight-only` box mode on fixtures rc 0; `probe --preflight-only` refuses by name on the
PIN_ME_ placeholders as before; common/box_preflight_selftest.sh (which runs this driver's refusals and its structure
test) re-run, result in s4d1/NOTES.md 9.8 / the coordinator's report.
