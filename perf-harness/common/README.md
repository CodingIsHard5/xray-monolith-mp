# common/: shared box-safety checks for EnginePerf's engine drivers (2026-11-27)

- `box_preflight.sh`: the checks, as functions with exit codes (0 clear, 1 refused, 2 cannot tell = refused). Source it;
  it defines `BOX_PREFLIGHT_VERSION=1` and `BOX_PREFLIGHT_MARKER=engineperf-box-preflight-v1`. Its header lists the
  functions, the inputs (overridable only for fixtures) and where it is stricter than the drivers it was taken from.
- `box_preflight_selftest.sh`: mutant self-test, fixtures only (no claim, no game, no real lock, no signal). One output
  folder is kept under `selftest-runs/`.
- Users today: `s4d2/s4d2_run.sh` and `tools/det_run.sh` (both pin this file's sha256 and refuse without it).
- Also: `box_ok_engine` counts a `pgrep -f` match only if its binary is a Wine loader (tools/lib_engine_pids.sh rule:
  no bare name match), so a watcher whose argv names the path is never taken for an engine.

## Where the shared logic is stricter than the drivers it came from

| check | taken from | the drivers differ how | shared file |
|---|---|---|---|
| play lock | sf1, wu1-3, kj3p, kj4p, s4c_series: `[ -e lock ] && kill -0 "$(cat lock)"` | an empty, unreadable or non-numeric lock reads as "no lock"; `kill -0` on a live pid of another user fails, so it also reads as "no lock" | unreadable / empty / non-numeric = cannot tell (refuse); liveness by `/proc/<pid>`; a lock whose pid is gone is accepted as stale (as all drivers) |
| protected title | sf1 `title_running` (rc != 1 = running), kj3p/kj4p `title_state` (rc 2 = not clear) | wu1, wu2, wu3, kw1, s4c_series, s4c_partA, det_run.sh, em1's watch: `check && stop`, so a checker that is missing or fails reads as "no title" (fail-open) | any exit but 1 refuses; a missing checker refuses |
| Roblox (Sober) | all: `pgrep -x sober` | only kj3p/kj4p treat a pgrep error as "not clear" | pgrep exit other than 0/1 refuses |
| own claim | sf1, kj3p, kj4p `w_holds` | kw1 and s4c_series only test `HELD: ... <worker>` to decide whether to `want`, not to refuse; no other driver has the test. None refuses when `check` itself fails | the same awk test (handoff pre-claim excepted), SLOTn lines included; `check` failing, or exit 0 with no HELD line, refuses; an own token (kj4p) excludes the caller's own claim |
| EXCLUSIVE named | the s4d2 brief: `grep -q EXCLUSIVE ~/.claude/overseer/QUIET-REQUIRED` | no earlier driver has it (they are EXCLUSIVE claimers, the queue decides) | also reads the slot files `QUIET-REQUIRED-<n>` that `check` reads (`*.expired` ignored); an unreadable file refuses; applied for class `shared` only |
| display not :0 | kj3p, kj4p `disp_free` (`:<digits>`, n != 0) | sf1 and wu1-3 require exactly :141 (stricter for themselves); kw1 accepts any `:<n>` and refuses :0 only because its socket exists | `:<digits>` with n != 0, else refused (`:00`, `:0.0`, `localhost:0` included) |
| engine of the run tree | play/caden_simtick_play.sh:21 (the launcher's own test) | no driver has it | new: `pgrep -f` with the launcher's exact pattern; refused while one runs (outside the boot itself) |

## Existing drivers (none edited; read-only survey 2026-11-27 22:45)

Columns: L = Caden's play lock test; T = protected-title test (FC = fail-closed, FO = fail-open); R = Sober test;
O = own-claim (w_holds) test; X = EXCLUSIVE-in-QUIET-REQUIRED test; D = display :0 refused; Rel = releases only its
own claim (by claim-id or token) rather than by worker name. "Pinned in" = files that record the driver's sha256 (the
8-hex prefix was searched in perf-harness and plan-facts; ITRZone amendment files are in perf-harness copies). "Closed" =
registered and run with a result file; editing a closed driver breaks a recorded hash.

| driver (sha256 prefix) | L | T | R | O | X | D | Rel | pinned in | state | could it run again? |
|---|---|---|---|---|---|---|---|---|---|---|
| perf-harness/wu1/wu1_run.sh (0a252197) | yes | FO | yes, FO | no | no | :141 only | no (name, if it claimed) | wu1/PREREG-wu1-...-2026-11-26.md | closed: stopped at its probe, superseded by wu2 (wu2 PREREG "What changed from wu1") | no (superseded). **RETIRED: do not run; use the shared-preflight driver instead.** |
| perf-harness/wu2/wu2_run.sh (257f4ec7) | yes | FO | yes, FO | no | no | :141 only | no | wu2 PREREG; wu3_run.sh names it | closed: RESULT-wu2 (FAIL stopped_series) | no (superseded by wu3). **RETIRED: do not run; use the shared-preflight driver instead.** |
| perf-harness/wu3/wu3_run.sh (89e8a8b3) | yes | FO (`title_running` = the checker's exit) | yes, FO | no | no | :141 only | no | wu3 PREREG (registered) | closed: RESULT-wu3 PASS | only as a new registration. **RETIRED: do not run; use the shared-preflight driver instead.** |
| perf-harness/sf1/sf1_run.sh (3e0047b2) | yes | FC | yes, FO | yes | no | :141 only | no (name, if CLAIMED) | sf1/PREREG-sf1-...-DRAFT.md (registered text, file name kept) | closed: RESULT-sf1 (scored 17:38) | only as a new registration. **RETIRED: do not run; use the shared-preflight driver instead.** |
| perf-harness/kj1/t2_xray_kj.sh = tools/t2_xray_kj.sh (870d854f) | no | FO | no | no claim (runs under ITRZone's joint claim) | no | no | n/a | PREREG-killchannel-joint-kj1 and -kj2, kj2 DRAFT, kj1/itrzone-amendment-137(-draft) | closed (kj1_xscore.txt) | no. **RETIRED: do not run; use the shared-preflight driver instead.** |
| perf-harness/kj2/t2_xray_kj2.sh = tools/ copy (f52da5c0) | no | FO | no | no claim | no | no | n/a | PREREG-killchannel-joint-kj2, kj2/itrzone-amendment-138(-draft) | closed (kj2_xscore.txt) | no. **RETIRED: do not run; use the shared-preflight driver instead.** |
| perf-harness/kj3/t2_xray_kj3.sh = tools/ copy (245c7dac) | no | FO | no | no claim (kj3p_run holds it) | no | no | n/a | kj3p PREREG; kj3p_run.sh pins it | closed (RESULT-kj3p) | no. **RETIRED: do not run; use the shared-preflight driver instead.** |
| perf-harness/kj3/kj3p_run.sh (101c8dab) | yes | FC (tri-state) | yes, FC | yes | no | yes | yes (claim-id, token) | kj3p PREREG; kj3p_run_log_r1.txt | closed: RESULT-kj3p (retry r1 used) | no. **RETIRED: do not run; use the shared-preflight driver instead.** |
| perf-harness/kj4/t2_xray_kj4.sh = tools/ copy (16f48396) | no | FO | no | no claim | no | no | n/a | kj4p PREREG; kj4p_run.sh pins it | closed (RESULT-kj4p) | no. **RETIRED: do not run; use the shared-preflight driver instead.** Its unused registered retry (r2) is **ABANDONED** (supervisor 2026-11-27; reason: em1 superseded it) |
| perf-harness/kj4/kj4p_run.sh (c24279e6) | yes | FC (tri-state) | yes, FC | yes | no | yes | yes (claim-id, token) | kj4p PREREG; kj4p_run_log_r1.txt | closed: RESULT-kj4p FAIL, but "the registered retry is unused" | no. **RETIRED: do not run; use the shared-preflight driver instead.** Its unused registered retry (r2) is **ABANDONED** (supervisor 2026-11-27; reason: em1 superseded it) |
| perf-harness/em1/t2_xray_em1.sh = tools/ copy (b444d8e4) | no | FO (in its watch loop) | no | no claim; refuses unless a joint EXCLUSIVE claim is HELD | no | no | n/a | em1/PREREG-em1-...-DRAFT.md (registered 15:27) | closed: ran 17:01 under ITRZone's run (plan-facts/surge-body-posthoc-em1-2026-11-27.md) | no. **RETIRED: do not run; use the shared-preflight driver instead.** |
| perf-harness/kw1/t3_kw1.sh = tools/ copy (cecc098f) | no | FO | yes, FO | no (HELD test decides `want` only) | no | only via "display exists" | no (withdraw + release by name) | PREREG-killchannel-xwitness; kj3p_run.sh and kj4p_run.sh pin it | closed (kw1_xscore.txt) | no. **RETIRED: do not run; use the shared-preflight driver instead.** Its unused registered retry (r2) is **ABANDONED** (supervisor 2026-11-27; reason: em1 superseded it) |
| tools/det_run.sh (was 01fb40dc, now da66e107) = perf-harness/det_run.sh (identical copy, the git record) | via common | FC (start and every 5 s) | FC | yes (caller's token with DET_NOCLAIM=1) | yes (class shared) | yes | yes (claim-id) | PREREG-S2-simtick-2026-11-25 (01fb40dc, registered and closed: **that recorded hash no longer matches the file**; the old bytes stay in git, wt-render-split 450f7cfb9 and earlier); s4d2_run.sh (updated to da66e107) | **RETROFITTED 2026-11-27** (supervisor's decision 2): sources common/box_preflight.sh | yes: s4d2 (and s4d1 when written) boot through it; callers must pass DET_BOX_TOKEN with DET_NOCLAIM=1 |
| tools/det_pair.sh (d4c38a79) = perf-harness/det_pair.sh | no | FO | no | no | no | no | no (name) | PREREG-S2-simtick | **UNSUPPORTED since det_run.sh's retrofit**: it calls det_run.sh with DET_NOCLAIM=1 and no DET_BOX_TOKEN, which det_run.sh now refuses by name. Its callers (tools/s2_partA*.sh, s4a/s4b/s4c_partA.sh) are closed. RETIRED: do not run; use the shared-preflight driver instead | no |
| tools/s4c_series.sh (6539d1ce) | yes | FO | yes, FO | no | no | no | no | PREREG-S2-4c; s4c_run.sh, s4c_input_run.sh | closed (s4c scored) | no. **RETIRED: do not run; use the shared-preflight driver instead.** |
| tools/s4c_partA.sh (af4f021c) | no | FO | yes, FO | no | no | no | no | PREREG-S2-4c | closed | no. **RETIRED: do not run; use the shared-preflight driver instead.** |
| s4d1/s4d1_run.sh (planned) | - | - | - | - | - | - | - | not written (s4d1 PREREG DRAFT 6.2 "TO BE WRITTEN") | open draft | yes: can source common/box_preflight.sh from the start |
| s4d2/s4d2_run.sh | via common | FC | FC | yes (token) | yes | yes (:141 only) | yes (claim-id; name only when its token claim is the only one) | not registered (draft) | open draft | yes |

Not surveyed one by one: the other tools/ drivers (s2_*, s4a_*, s4b_*, tb_*, r1*, t1/t2/t3_* boot scripts); a grep
shows they follow the same fail-open title pattern as det_run.sh.

**ITRZone (read-only, not ours; listed only).** /home/caden/Projects/ITRZone/scripts has 53 shell scripts that call
quiet-required.sh or protected-title-check.sh (boot_*.sh, run_itr2.sh, hub_boot.sh, queue_s1g_q1.sh, *_arm.sh,
claim_renew.sh, zone_pair.sh). By grep: none tests run/.caden-play.lock or the handoff pre-claim; the boot_*.sh and
run_itr2.sh call the title checker twice and pgrep sober once. The joint runs kj1, kj2 and em1 were driven from there.

**Supervisor's decisions 2026-11-27, applied here:** (1) the closed drivers are NOT edited (their pinned hashes are
part of the record) and are marked RETIRED above; (3) kj4p_run.sh is RETIRED, its unused registered retry ABANDONED
(em1 superseded it); (2) tools/det_run.sh is retrofitted to the shared file.

**det_run.sh copies.** The drivers call `tools/det_run.sh` (s4d2_run.sh `DETRUN=tools/det_run.sh`, det_pair.sh
`$P/tools/det_run.sh`). `wt-render-split/perf-harness/det_run.sh` is the git-tracked copy and is kept byte-identical
(it is never run in place: from there its project root would be wt-render-split, which has no run/). The other
worktrees (wt-simtick, wt-stance, wt-fixes, wt-absorb, wt-diag) carry an older det_run.sh (f82a2c24...) on their own
branches; they were not touched (other branches' trees; no driver calls them).

**What det_run.sh checks, and when** (header of the file): before anything is created, deleted or moved (out folder,
claim, Xvfb, user.ltx swap, the `rm -f` of the xray log, the crash-dump move): play lock, title, Sober, an engine of
the run tree already running, own claim (with DET_NOCLAIM=1 the caller's DET_BOX_TOKEN must be in a live claim, and
any other claim under the worker refuses), EXCLUSIVE (DET_BOX_CLASS shared), display not :0. Every 5 s while the
engine runs: play lock, title, Sober (abort rc 3); engine_running is not re-checked then (its own engine would match).
A crash dump is not moved while the play lock is live. `--preflight-only <exe> <tag>` runs the checks only.

Summary for the supervisor: of 15 existing drivers/helpers (the survey before the retrofit), only kj3p_run.sh and
kj4p_run.sh had every check except EXCLUSIVE-in-QUIET-REQUIRED; sf1 lacked fail-closed Sober and release-by-id;
wu1-3, kw1, s4c_* and det_run.sh treated a failing title checker as "no title". All closed drivers are now RETIRED
(not edited). det_run.sh is retrofitted; note that its old hash 01fb40dc is ALSO recorded in the registered, closed
PREREG-S2-simtick-2026-11-25 (lines 130 and 285), not only in the two drafts: that record now names bytes that live
only in git history. det_pair.sh is unsupported (refused by det_run.sh) and RETIRED.
