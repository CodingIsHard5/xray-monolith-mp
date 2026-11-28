# common/: shared box-safety checks for EnginePerf's engine drivers (2026-11-27, v2 2026-11-28)

- `box_preflight.sh` (v2, third-review revision 2026-11-28, sha256 f8026040f2f9b3f6cd5c7c33b11062f7efc1ed726af03651ce16bbb0fccd53ef; was f3ad9351...): the checks, as functions with exit codes (0 clear, 1 refused,
  2 cannot tell = refused), and the guarded-write helpers. Source it; it defines `BOX_PREFLIGHT_VERSION=2` and
  `BOX_PREFLIGHT_MARKER=engineperf-box-preflight-v2`. Its header lists the functions, the inputs (overridable only for
  fixtures), the environment the checks trust, and where it is stricter than the drivers it was taken from.
  v2 (plan-facts/box-preflight-second-review-2026-11-27.md): it never calls quiet-required.sh (v1 called `check`,
  which is not read-only: it moves expired slot files, can wake the queue head through tmux and runs the title scan);
  claims are read from the tool's files, every nested record counted; the title check honours the tool's 180-s memory
  (`.protected-seen`) and refuses PTC_* variables; Sober is found by its flatpak; `box_fresh` / `box_guarded` re-check
  the box on the line before every write under run/ and make a stop sticky; `box_rm_name`, `box_dumps_newer`,
  `box_wine_kill`, `box_kill_own`, `box_engines_in_prefix`, `box_queue_count`; a host-pid-namespace check.
  Third-review revision (plan-facts/box-preflight-third-review-2026-11-28.md; marker and version unchanged, the callers
  pin the sha256): the caller's own title sightings (`BOX_SEEN_AT`, S3-5, below); `flatpak ps` under
  XDG_RUNTIME_DIR=/run/user/$UID (N3-1); `10#` on slot-line minutes and sighting times (N3-7); the title checker bounded
  by `timeout 10` (N3-9: it samples ~2 s; 5x that; a timeout = cannot tell); `box_preflight_all` also leaves its lines
  in `BOX_OUT`; `box_is_own` (N3-8); `box_wine_kill <prefix> nowait` (-k only, 10 s, for S3-1's repeated kill).
- `box_preflight_selftest.sh`: mutant self-test, fixtures only (no claim, no game, no real lock, no signal). One output
  folder is kept under `selftest-runs/`.
- Users today: `s4d2/s4d2_run.sh`, `s4d1/s4d1_run.sh` (2026-11-28) and `tools/det_run.sh` (all pin this file's sha256 and refuse without it).
- Also: `box_ok_engine` counts a `pgrep -f` match only if its binary is a Wine loader (tools/lib_engine_pids.sh rule:
  no bare name match), so a watcher whose argv names the path is never taken for an engine.

## Where the shared logic is stricter than the drivers it came from

| check | taken from | the drivers differ how | shared file |
|---|---|---|---|
| play lock | sf1, wu1-3, kj3p, kj4p, s4c_series: `[ -e lock ] && kill -0 "$(cat lock)"` | an empty, unreadable or non-numeric lock reads as "no lock"; `kill -0` on a live pid of another user fails, so it also reads as "no lock" | unreadable / empty / non-numeric = cannot tell (refuse); liveness by `/proc/<pid>`; a lock whose pid is gone is accepted as stale (as all drivers) |
| protected title | sf1 `title_running` (rc != 1 = running), kj3p/kj4p `title_state` (rc 2 = not clear) | wu1, wu2, wu3, kw1, s4c_series, s4c_partA, det_run.sh (before), em1's watch: `check && stop`, so a checker that is missing or fails reads as "no title" (fail-open) | any exit but 1 refuses; a missing checker refuses; a sighting in `.protected-seen` younger than 180 s refuses (the claim tool's own linger); any PTC_* variable in the caller's environment refuses; the checker runs without INTERVAL/MIN_PCT |
| Roblox (Sober) | all: `pgrep -x sober` (dead: see the Sober note) | only kj3p/kj4p treat a pgrep error as "not clear" | the flatpak: a process in an `app-flatpak-org.vinegarhq.Sober-*.scope` cgroup or with a Sober `.flatpak-info` (protected-title-check.sh rule 7), `flatpak ps`, an active Sober systemd scope (the ops worker's proposal), and `pgrep -x sober` as one more refusal; any tool failing refuses |
| own claim | sf1, kj3p, kj4p `w_holds` | kw1 and s4c_series only test `HELD: ... <worker>` to decide whether to `want`, not to refuse; no other driver has the test. None refuses when `check` itself fails | read from the claim tool's files, not through `check`: every live record in quiet-claims/<slot> (nested claims included), else the slot line as one legacy claim; handoff pre-claim excepted; an own token (kj4p) excludes the caller's own claim; an unreadable or unparsable file refuses |
| EXCLUSIVE named | the s4d2 brief: `grep -q EXCLUSIVE ~/.claude/overseer/QUIET-REQUIRED` | no earlier driver has it (they are EXCLUSIVE claimers, the queue decides) | every LIVE slot line (QUIET-REQUIRED, QUIET-REQUIRED-<n>; `*.expired` and expired slots ignored); an unreadable or unparsable file refuses; applied for class `shared` only |
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
| tools/det_run.sh (was 01fb40dc, then da66e107, then 0b7a6edb, now e2980094) = perf-harness/det_run.sh (identical copy, the git record) | via common | FC (start, after the claim, before every write, every 5 s) | FC | yes (caller's token with DET_NOCLAIM=1) | yes (class shared) | :141 only | yes (claim-id) | PREREG-S2-simtick-2026-11-25 (01fb40dc, registered and closed: the old bytes are in common/retired/); s4d2_run.sh and both drafts (updated to e2980094) | **RETROFITTED 2026-11-27, revised 2026-11-28** (second review): box_preflight.sh v2, guarded writes, nothing under run/ touched after a stop | yes: s4d2 (and s4d1 when written) boot through it; callers must pass DET_BOX_TOKEN with DET_NOCLAIM=1, and BENCH_DISPLAY=:141 |
| tools/det_pair.sh (d4c38a79) = perf-harness/det_pair.sh | no | FO | no | no | no | no | no (name) | PREREG-S2-simtick | **UNSUPPORTED since det_run.sh's retrofit**: it calls det_run.sh with DET_NOCLAIM=1 and no DET_BOX_TOKEN, which det_run.sh now refuses by name. Its callers (tools/s2_partA*.sh, s4a/s4b/s4c_partA.sh) are closed. RETIRED: do not run; use the shared-preflight driver instead | no |
| tools/s4c_series.sh (6539d1ce) | yes | FO | yes, FO | no | no | no | no | PREREG-S2-4c; s4c_run.sh, s4c_input_run.sh | closed (s4c scored) | no. **RETIRED: do not run; use the shared-preflight driver instead.** |
| tools/s4c_partA.sh (af4f021c) | no | FO | yes, FO | no | no | no | no | PREREG-S2-4c | closed | no. **RETIRED: do not run; use the shared-preflight driver instead.** |
| s4d1/s4d1_run.sh (af73868b, written 2026-11-28 from s4d2_run.sh) | via common | FC | FC | yes (token; a lost claim stops) | yes | yes (:141 only) | yes (one claim, renewed by id; every id released; name only when no id was printed) | s4d1 PREREG DRAFT 6.2 (not registered) | open draft; stimulus pinned 1777fb68 (shared with s4d2); refuses until it is installed | yes (not run: `bash -n` and fixture preflight-only calls only) |
| s4d2/s4d2_run.sh (f885501e, 2026-11-28 fix round) | via common | FC | FC | yes (token; a lost claim stops) | yes | yes (:141 only) | yes (one claim, renewed by id; every id released; name only when no id was printed) | not registered (draft) | open draft | yes |

## The other runnable tools/ scripts (review S-5; NOT edited, read-only survey 2026-11-28)

Every executable `tools/*.sh` that boots the engine or calls a script that does, except det_run.sh (above). Surveyed by
a read-only sub-worker (each file read; the t2_xray variants compared to t2_xray.sh by grep); line numbers as read on
2026-11-28. Columns: L = play-lock test; T = protected-title test (FO = fail-open: `check && stop`, `if check`,
`until ! check`, so a failing checker reads as "no title"); R = `pgrep -x sober` (DEAD everywhere: Sober's game
process is named "Main", see the Sober note below; the title checker's rule 7 still catches an active Sober); C = claim
handling (own = want/claim loop for engineperf; "any holder" = only tests that SOME claim is held); E = an engine of
the run tree already running is refused; wsk = `wineserver -k` with WINEPREFIX=run/prefix (prefix_play where named).
Every one of them is missing the shared checks; none is retired by this table: they stay as they are until the
supervisor decides (S-5: "do NOT edit"). **LIVE for ITRZone** = called by /home/caden/Projects/ITRZone/scripts/boot_t1.sh
or boot_t2.sh (another worker's live drivers): these cannot simply be retired.

| script | boots | L | T | R | C | E | deletes / moves / overwrites under run/ without a lock check (file:line) | called by |
|---|---|---|---|---|---|---|---|---|
| tools/bench.sh | run/bin/<exe>, BENCH_SAVE, run/prefix, Xvfb (default :77) | no | FO :15, :59 | no | own :22 (skipped with BENCH_NOCLAIM=1) | no | wsk :31 (trap); **rm -f xray_<user>.log + perf csv :41**; mv csv :75 | r1_series.sh:37, r1b1_series.sh:78 |
| tools/coop_load.sh | OurDedicatedDX8.exe server + clients, coopload/prefix | no | no | no | none | no | none under run/ (wsk on coopload/prefix :51) | coop_series.sh:16, :46 |
| tools/coop_series.sh | coop_load.sh | no | FO :8 | dead :9 | want + claim :10-11 | no | none under run/ | queue_i2.sh:54 |
| tools/det_pair.sh | det_run.sh (now refused: no DET_BOX_TOKEN) | no | FO :7 | no | own :8 | no | none directly | s2_partA*.sh, s4a/s4b/s4c_partA.sh (UNSUPPORTED, above) |
| tools/diag_series.sh | t1_xray.sh (B1Diag14) | no | FO :22, :31 | dead | own :21-23 | no | none directly (t1_xray does) | none |
| tools/fix_witness.sh | t1_xray.sh (Fix14) | no | FO :12 | dead | own :11-13 | no | none directly | none |
| tools/p1_run.sh | tb_run.sh (RefDet14, cordon1, 2 runs) | no | FO :16, :33 | dead | own :15-17 | no | cp puppet script -> run/gamedata/scripts :20; cp p1_track.csv -> run/gamedata/configs :21; wsk :49; rm both :57 | none |
| tools/play_boottest.sh | play/caden_simtick_play.sh with SIMTICK_SELFTEST=1 (run/prefix_play), Xvfb :78 | after the run only :42 | FO :18 | dead | own :17-19 | no (the launcher's) | **wsk on run/prefix_play :21 (EXIT trap) and :38, unconditional: kills Caden's game server if he plays** | none |
| tools/play_boottest2.sh | the same, Xvfb :141 | after only :48 | FO :18 | dead | own :17-19 | no (launcher) | **wsk on run/prefix_play :21, :44** | none |
| tools/play_boottest3.sh | the same (Warm18), :141, shared claim | after only :56 | FO :25 | dead | own shared :24-26 (+ no EXCLUSIVE) | no (launcher) | **wsk on run/prefix_play :28, :52** | none |
| tools/queue_i2.sh | coop_series.sh i2 | no | delegated | no | want :37, claim loop :40 | no | none | wt-render-split copy only |
| tools/r1_series.sh | bench.sh | no | FO :23, :33 | dead | own :22-24 | no | none directly | none |
| tools/r1b1_series.sh | bench.sh (R1b14) | no | FO :62, :73 | dead | own :61-63 | no | overwrites run/appdata/user.ltx (set_cfg :32) at :76, :89 | none |
| tools/s2_bench.sh | copy of bench.sh (BENCH_FIXED) | no | FO :17, :61 | no | own :24 unless NOCLAIM | no | wsk :33; **rm -f xray log + csv :43**; mv :77 | s2_series.sh:65, s2_series_b.sh:71, s4c_series.sh:74 |
| tools/s2_partA.sh | det_pair.sh x4 | no | FO :12 | dead | own :11-13 | no | none directly | none |
| tools/s2_partA2.sh | det_pair.sh x6 | no | FO :25 | dead | own :24-26 | no | none directly | none |
| tools/s2_partB.sh | s2_series.sh x3 | no | delegated | delegated | delegated | no | none directly | none |
| tools/s2_partB2.sh | s2_series_b.sh x2 | no | delegated | delegated | delegated | no | none directly | none |
| tools/s2_series.sh | s2_bench.sh | no | FO :49, :60 | dead | own :48-50 | no | user.ltx (set_cfg :29) at :63, :76 | s2_partB.sh:17 |
| tools/s2_series_b.sh | s2_bench.sh | no | FO :55, :66 | dead | own :54-56 | no | user.ltx (set_cfg :29) at :69, :82 | s2_partB2:11, s2c_run:12, s4a_run:11, s4b_run:11 |
| tools/s2c_run.sh | s2_series_b.sh | no | delegated | delegated | delegated | no | none directly | none |
| tools/s4a_partA.sh | det_pair.sh | no | FO :28 | dead | own :27-29 | no | none (reads user.ltx :23) | none |
| tools/s4a_run.sh | s2_series_b.sh | no | delegated | delegated | delegated | no | none directly | none |
| tools/s4b_partA.sh | det_pair.sh | no | FO :28 | dead | own :27-29 | no | none (reads user.ltx) | none |
| tools/s4b_run.sh | s2_series_b.sh | no | delegated | delegated | delegated | no | none directly | none |
| tools/s4c_input_run.sh | s4c_series.sh | via s4c_series | delegated | delegated | delegated | no | none directly | none |
| tools/s4c_partA.sh | det_pair.sh | no | FO :28 | dead | own :27-29 | no | none (reads user.ltx) | none (RETIRED above) |
| tools/s4c_run.sh | s4c_series.sh | via s4c_series | delegated | delegated | delegated | no | none directly | none |
| tools/s4c_series.sh | s2_bench.sh | at the claim only :58 (fail-open on an unreadable pid file) | FO :58, :69 | dead | own :57-59 | no | user.ltx (set_cfg :32) at :72, :85, no lock test inside its 3-h loop | s4c_run:11, s4c_input_run:11 (RETIRED above) |
| tools/stall_run.sh | tb_run.sh (RefDet14, bench1, walk) | no | FO :13 | dead | own :12-14 | no | wsk :48 | none |
| tools/t1_xray.sh | tb_run.sh (T1_EXE, default RefDet14, cordon1) | no | **none** | no | tests only that some holder has EXCLUSIVE/B1-T2 :19 | no | cp puppet script :22; cp track csv :23; rm (trap) :24; wsk :49 | diag_series:36, fix_witness:16, t3_nobridge:18; **ITRZone boot_t1.sh:10 (LIVE for ITRZone)** |
| tools/t2_preflight.sh | t2_xray.sh | no | FO :11 | dead | own :10-12 | no | none under run/ (/dev/shm) | t3_pfE.sh:8 |
| tools/t2_xray.sh | tb_run.sh (T2_EXE RefDet14, cordon1), restarts | no | FO between boots :134; none before boot 0 | no | any holder :37 | no | cp puppet :42; rm (trap) :43; wsk :85, :122 | t2_preflight:20; **ITRZone boot_t2.sh:16 (LIVE)** |
| tools/t2_xray_c.sh | as t2_xray | no | FO :154, :156 | no | any holder :43 | no | cp :49; rm :51; wsk :94, :108, :142 | **ITRZone boot_t2.sh:29 (LIVE)** |
| tools/t2_xray_c2.sh | as t2_xray | no | FO :159, :161 | no | any holder :43 | no | cp :49; rm :51; wsk :99, :113, :147 | **ITRZone boot_t2.sh:30 (LIVE)** |
| tools/t2_xray_c3.sh | as t2_xray | no | FO :192, :194 | no | any holder :47 | no | cp :53; rm :55; wsk :78, :126, :140, :180 | **ITRZone boot_t2.sh:32 (LIVE)** |
| tools/t2_xray_em1.sh | as t2_xray | no | FO :205 | no | any holder :56 | no | **cp axr_options_play.ltx over run/gamedata/configs/axr_options.ltx :66**; cp puppet :77; rm :79; wsk :126, :193 | **ITRZone boot_t2.sh:48 (LIVE)** (closed copy RETIRED above) |
| tools/t2_xray_kj.sh | as t2_xray | no | FO :177 | no | any holder :49 | no | cp :55; rm :57; wsk :104, :165 | **ITRZone boot_t2.sh:38 (LIVE)** |
| tools/t2_xray_kj2.sh | as t2_xray | no | FO :177 | no | any holder :49 | no | cp :55; rm :57; wsk :104, :165 | **ITRZone boot_t2.sh:41 (LIVE)** |
| tools/t2_xray_kj3.sh | as t2_xray | no | FO :191 | no | any holder :56 | no | cp :63; rm :65; wsk :112, :179 | none (closed copy RETIRED above) |
| tools/t2_xray_kj4.sh | as t2_xray | no | FO :191 | no | any holder :56 | no | cp :63; rm :65; wsk :112, :179 | none (closed copy RETIRED above) |
| tools/t2_xray_kw.sh | as t2_xray | no | FO :165 | no | any holder :45 | no | cp :51; rm :53; wsk :100, :153 | t3_kw1.sh:76 |
| tools/t2_xray_lost.sh | as t2_xray | no | FO :146 | no | any holder :34 | no | cp :40; rm :42; wsk :83, :134 | t3_pfL.sh:51 |
| tools/t2_xray_lost2.sh | as t2_xray | no | FO :146 | no | any holder :34 | no | cp :40; rm :42; wsk :83, :134 | t3_pfL2.sh:52 |
| tools/t2_xray_v3.sh | as t2_xray | no | FO :165 | no | any holder :45 | no | cp :51; rm :53; wsk :100, :153 | t3_pfD0:64; **ITRZone boot_t2.sh:35 (LIVE)** |
| tools/t2_xray_v3_b31879be.sh | as t2_xray | no | FO :162 | no | any holder :42 | no | cp :48; rm :50; wsk :97, :150 | none |
| tools/t3_kw1.sh | t2_xray_kw.sh (Stance14, :141) | no | FO :56 | dead | own :55-57 | no | none under run/ | none (closed copy RETIRED above) |
| tools/t3_nobridge.sh | t1_xray.sh x2 | no | FO :12 | dead | own :11-13 | no | none directly | none |
| tools/t3_pfD0.sh | t2_xray_v3.sh | no | FO :46 | dead | own :45-47 | no | none under run/ | none |
| tools/t3_pfE.sh | t2_preflight.sh x4 | no | delegated | delegated | delegated | no | none directly | none |
| tools/t3_pfL.sh | t2_xray_lost.sh | no | FO :33 | dead | own :32-34 | no | none under run/ | t3_pfL_series.sh:17 |
| tools/t3_pfL2.sh | t2_xray_lost2.sh | no | FO :34 | dead | own :33-35 | no | none under run/ | none |
| tools/t3_pfL_series.sh | t3_pfL.sh x5 | no | delegated | delegated | delegated | no | none directly | none |
| tools/tb_run.sh | run/bin/$TB_EXE, <save>, run/prefix, Xvfb :77 | no | FO :22, :59 | no | own :24 (skipped with TB_NOCLAIM=1) | no | cp user.ltx.orig over run/appdata/user.ltx :41 and in the trap :33; wsk :33; **rm -f xray_<user>.log + dumps :42**; mv dumps :68; **mv EVERY run/appdata/logs/*.mdmp :69** | p1_run, stall_run, t1_xray, every t2_xray*, tb_slice:99; **LIVE for ITRZone through t1/t2_xray (boot_t2 pins its hash)** |
| tools/tb_slice.sh | tb_run.sh (RefDet14, bench1) | no | FO :79, :93 | dead | own :77-80 | no | none directly | wt-render-split copy only |

Not booting (not in the table): tb_hash.sh, coop_hash.sh (hash files only), run_noguard.sh (a guard),
lib_engine_pids.sh (sourced), t3_kw1_selftest.sh (desk test). The wt-render-split/perf-harness copies of these scripts
are old duplicates; tb_slice, queue_i2 and coop_series `cd ~/Projects/StalkerGameEngine` and so would call the live
tools/ scripts (inferred from their paths, not run).

Summary: 55 booting scripts besides det_run.sh. 52 have no play-lock gate (s4c_series checks it at the claim only and
fail-open; the play_boottest scripts look at it only after the run); none refuses an engine already running; every
title test is fail-open; every `pgrep -x sober` is dead. LIVE for ITRZone: t1_xray.sh, t2_xray.sh, t2_xray_c/c2/c3,
t2_xray_v3, t2_xray_kj/kj2, t2_xray_em1, and tb_run.sh under them. **Most dangerous, for the supervisor:** the three
play_boottest*.sh run `wineserver -k` in **run/prefix_play** (Caden's play prefix) unconditionally, so one started while
he plays would end his game; tb_run.sh / bench.sh / s2_bench.sh delete `xray_caden.log` (the file his engine writes)
with no lock test; t2_xray_em1.sh overwrites his MCM options file.

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

**What det_run.sh checks, and when** (header of the file, revised 2026-11-28): tag `[a-z0-9_]+`, exe, save and numbers
validated before any path is built; BENCH_DISPLAY must be :141. Before anything is created: host pid namespace, play
lock, title, Sober, an engine of the run tree already running, own claim (with DET_NOCLAIM=1 the caller's
DET_BOX_TOKEN must be in a live claim record, and any other record under the worker refuses), EXCLUSIVE (class shared),
display not :0 and free. The traps (EXIT, TERM, HUP, INT) are set before the claim. Right after the claim: box_fresh
(title, Sober, engine, play lock last). Then every write under run/ (the user.ltx swap, the removal of the old log and
this tag's old dumps by validated name in the fixed logs folder, the engine start, the collection of this tag's files
and of the crash dumps newer than this boot's start marker, the user.ltx restore) runs inside box_guarded: box_fresh
on the line before, no wait. Every ~2-5 s while the engine runs (also in the waits after the last checkpoint): play
lock, title, Sober. After a refusal or a signal NOTHING under run/ is written, moved or deleted any more: its own
engine is stopped by `wineserver -k` in run/prefix only (box_wine_kill), its own Xvfb / openbox are killed only if
parent and name match (box_kill_own), and the files left (this tag's logs and saves, the det user.ltx, new dumps) are
printed by name for a later manual step. If an engine is still in run/prefix 60 s after `wineserver -k`, the claim is
not released. Older crash dumps are never moved. `--preflight-only <exe> <tag>` runs the checks only.
Third review (2026-11-28): the project root is taken with `pwd -P` and must be the tree the engine checks look at
($HOME/Projects/StalkerGameEngine/run); a DET_BOX_TOKEN must be `<name>-<stamp of >= 9 digits>-<pid>`, >= 20 characters
(s4d2's `s4d2-<mode>-<14 digits>-<pid>` passes); DET_BOX_CLASS=exclusive only with DET_NOCLAIM=1; numbers without
leading zeros; NOGUARD_GAMES, WINESERVER, WINELOADER, ENGINE_MIN_RSS_KB, BASH_ENV refused when set (both modes);
DET_EXTRA split without globbing; after Xvfb's socket appears the server must be its own live Xvfb; wmctrl bounded 5 s;
user.ltx is swapped only while it is byte-identical to out/user.ltx.orig or out/user_det.ltx.orig; `wineserver -k` is
repeated every ~5 s inside the 60-s stop wait (an engine that was still starting at the first -k).

**The window that is left** (both drivers; corrected after the third review, S3-6): a check is a moment, not a lock.
Between box_fresh reading "no play lock" and the write on the next line there are microseconds to milliseconds. The
larger window is SECONDS long and on the launcher's side: play/caden_simtick_play.sh tests for an engine (:21) and the
lock (:22), runs run_noguard.sh (a `find` over the whole run tree) and its path guards (:28-33), and writes its lock only
at :34. A box_fresh in that stretch reads "no lock, no engine" while his start is under way. Reaching it needs his claim
test (:20) to pass just before our claim is visible (in s4d2 it always is first) AND his :21-:34 to outlast our claim ->
engine-start stretch (at least ~8 s). The title answer can also be up to 2 s old (the checker's cache) plus its 2-s
sample. Not closable from these scripts: the launcher writing its lock BEFORE its :21 test would close it from his side
(the launcher is not ours; left to its owner).

**The caller's own title sightings (S3-5).** `box_ok_title` records a checker exit 0 in `BOX_SEEN_AT` (`date +%s`) and
refuses for 180 s after it in the SAME shell, as the claim tool does after its own sightings, so a game read as playing
and then idle on the next 2-s sample (a menu) still refuses. Both drivers run `box_preflight_all` outside `$(...)` for
this (it leaves its lines in `BOX_OUT`). Limits: it does not carry to a parent shell or another process, so a
det_run.sh that s4d2_run.sh starts begins with no sighting of s4d2's (each process keeps its own); and it is not written
to the fleet's `~/.claude/overseer/.protected-seen`, which the shared file still READS: that file is the claim tool's
(quiet-required.sh writes it), and writing it from here would change what every other worker's claim tool decides.

**Sober (Roblox).** `pgrep -x sober` alone never matched: the Sober game process is named "Main" (exe /app/bin/sober,
protected-title-check.sh:84-85; ops report ~/Projects/Ops/reports/sober-check-20261127.md). The fleet's title checker
already covers an active Sober correctly (its rule 7: the flatpak scope / .flatpak-info, with the display taken from
the parent chain), so box_ok_roblox is a second, independent refusal: it refuses on ANY Sober process (idle or in a
menu too), by the checker's flatpak method plus the ops worker's `flatpak ps` / systemd-scope method (reference only,
not sourced: outside our tree and unpinned), and keeps `pgrep -x sober` as one more refusal.

**Known limits, not fixed (second review):**
- S-3 `withdraw`: the queue is keyed by worker name only (`want` dedupes by name, `withdraw <w>` drops every entry of
  <w>, and the name must be a tmux session `overseer-<name>`, so a per-run queue name is impossible). s4d2_run.sh now
  sends `want` only when no entry under `engineperf` exists, and withdraws only an entry it made and only while exactly
  one exists. Not knowable without the tool: another engineperf job's `want` that landed on that same entry; it is
  withdrawn with ours (that job is then not woken by the queue).
- N-12: own_claim exempts any "handoff pre-claim" under engineperf, also one written for another engineperf job (the
  name cannot tell them apart); the claim tool then replaces it with ours (nest_ok refuses to nest on a pre-claim).
- N-16: quiet-required.sh `release <w> <id>` of the last record on a slot falls through to the FIRST extra slot held by
  <w> (`slot_of`), not necessarily that id's slot. Not reachable today (one slot per worker); a defect for the tool's owner.
- N-17: s4d2_run.sh reads Caden's own options file run/appdata/axr_options_play.ltx as its copy source (read-only,
  pinned: a change by him stops the run).
- S-7 residue: a slot whose records file header does not match its slot line (another writer just rewrote the line)
  is read as ONE legacy claim, as the tool itself reads it; for that moment a nested second record is not seen
  (engine_running still refuses a second engine).
- N-5 residue: `wineserver -k` in run/prefix would also stop any OTHER job in run/prefix; det_run.sh runs it only for
  an engine it started, after refusing to start beside an engine of the run tree.

**Known limits, not fixed (third review, plan-facts/box-preflight-third-review-2026-11-28.md):**
- N3-2: from a context with no session bus (cron, a bare `systemd-run`, ssh without the user bus) box_ok_roblox reads
  `systemctl --user` exit 1 as cannot tell and refuses every time (s4d2 then waits its 6 h). Kept: the false refusal is
  the safe direction, and from tmux on this machine all three commands answer.
- N3-3: box_ok_roblox does not read CADEN-TITLE-OVERRIDE: with Caden's "start them now" override set while Roblox is
  open, the title check clears but roblox still refuses. Kept: the safe direction; honouring the override here is
  Caden's or the supervisor's call, not a fix.
- N3-11: the claim tool writes the slot line before it rebinds the records file; for those milliseconds a nested
  second record reads as one legacy claim. Kept: it is inside the claim tool (not ours), and engine_running still
  refuses a second engine.
- N3-12: a future Proton launcher may write no lock and run in a pressure-vessel container, where the engine test
  (run/bin in argv, "wine" in /proc/<pid>/exe) may not see it. Kept: no Proton launcher exists yet; re-check both
  tests when one does.
- N3-13: s4d2_run.sh was not executable; it is now `chmod +x` (run it as `wt-render-split/perf-harness/s4d2/s4d2_run.sh`
  or `bash .../s4d2_run.sh`, both work). tools/det_run.sh was already +x.

Summary for the supervisor: of 15 existing drivers/helpers (the survey before the retrofit), only kj3p_run.sh and
kj4p_run.sh had every check except EXCLUSIVE-in-QUIET-REQUIRED; sf1 lacked fail-closed Sober and release-by-id;
wu1-3, kw1, s4c_* and det_run.sh treated a failing title checker as "no title". All closed drivers are now RETIRED
(not edited). det_run.sh is retrofitted; note that its old hash 01fb40dc is ALSO recorded in the registered, closed
PREREG-S2-simtick-2026-11-25 (lines 130 and 285), not only in the two drafts: that record now names bytes that live
only in git history. det_pair.sh is unsupported (refused by det_run.sh) and RETIRED.

## Retired bytes kept for closed records (added 2026-11-27, supervisor's order)

A closed registration names a file by hash. When that file is later changed, the old bytes are kept here, non-executable,
so the recorded hash still points at a file that exists. Do not run anything in `retired/`. Closed registrations are never
edited.

| registration (closed) | lines | file as named there | sha256 recorded | retired copy |
|---|---|---|---|---|
| perf-harness/PREREG-S2-simtick-2026-11-25.md | 130, 285 | tools/det_run.sh | 01fb40dc2891d0602f3f596b8cde9664d05edd89eaa42a664cc9a34fbdc70f38 | common/retired/det_run.sh.S2-01fb40dc (mode 444; bytes of git 99b59a9f5~1:perf-harness/det_run.sh, unchanged since 450f7cfb9) |

The live helper is `~/Projects/StalkerGameEngine/tools/det_run.sh`, OUTSIDE git; `perf-harness/det_run.sh` is its tracked,
byte-identical copy. A change to it must be reported to the security worker so its file baseline gets the new hash.
