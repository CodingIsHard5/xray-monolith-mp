# R1 step 1a: main-scene share of submission - pre-registration (measurement only; Overseer OK 2026-11-22)

## What
Coarse14b.exe = Coarse14 (perf/render-split) + 4 PROF_EVENT sites in src/Layers/xrRenderPC_R4/r4_R_render.cpp (my
file, not on gammavr's list), added as wrappers only: r1a_static0, r1a_dynamic0 (Part 0, G-buffer), r1a_forward_a
(static(1)) and r1a_forward_b (dynamic(1) + fade + sorted). The forward pass's CParticlesAsync::Wait() stays outside
the zones, because it is a wait, not submission. The sun cascades are not in any zone. About 4 extra probe calls per frame.

## Runs (tools/r1a_order.txt; tools/r1_series.sh with R1_ORDER, WANT, fair share, settle, re-run once if not clean)
Rostok (bench1) x3, Cordon (cordon1) x3, same engine args as step 0. About 10-15 min of box.

## Computed (tools/r1a_zones.py): main = static0 + dynamic0 + forward, main-thread ms per frame, mean of 3 per scene.
Sanity: main <= step 0's (b) in every run, and frame within 5 % of step 0's K means (5.75 / 4.27). If either check
fails, the zones are wrong and I report that instead of a decision.

## Abort rule (registered with the Overseer)
main < 0.6 ms in ROSTOK -> R1 STOPS (the gain cannot clear overheads and noise; the large refactor is not worth it).
Otherwise the zone numbers pick the one-pass slice (Overseer's next step): the single biggest of static0 / dynamic0 /
forward_a / forward_b in Rostok. Its own gain is registered before any code; stop if under a quarter, plan the rest if
at least half.

## Desk prediction (before the build)
(b) without the sun is about 1.0 ms in Rostok. Inside it are, besides these passes: emissive, wallmarks, lights
accumulation, HUD, LODs, details, sky. Guess: main = 0.5-0.8 ms in Rostok (central 0.65), so near the 0.6 bar, a
coin toss. static0 the biggest single pass (0.25-0.4). Cordon main 0.3-0.6.

## Stop before any 1a run (2026-11-22 22:41-23:20): the write-through into Caden's install

- The pre-run fingerprint differed from step 0 on the configs line (01b8f4a02901641d -> 1d846873421b09e3, same file
  count). The cause: run/gamedata/configs/axr_options.ltx was a SYMLINK into Caden's GAMMA mod "G.A.M.M.A. MCM
  values". Game scripts save that file on start (itms_manager.script, axr_main.script), so engine runs wrote into his
  install. It was written at 22:06 and 22:42, with gammavr/itrzone holding the box. Earlier runs, mine included, very
  likely wrote it too. A read-only search found it was the only file changed in his GAMMA folder since 2026-11-18 23:18.
  The series was stopped before claiming; nothing ran.
- Overseer condition (1), "re-baseline if the private copy equals what RefDet14 saw": NOT MET. The configs hash with
  the current content (= the private copy = the 22:42 snapshot) is 1d846873421b09e3; with the installer's pristine
  file it is ca95fcc3df01dd58; before step 0 it was 01b8f4a02901641d. So the content step 0 saw at its start is not
  available, and it changed after step 0 began. I do NOT re-baseline on that reason. Within step 0, F and K ran
  interleaved under the same conditions, so its F-vs-K comparison stands. Its absolute numbers carry this caveat.
- Sweep (Overseer order): every link under run/ into ~/Games that is not a read-only binary asset is now a private
  copy: 10,757 files, 257 MB (manifest run/private_copies_manifest.tsv, copy in perf-harness). The copies were checked
  byte for byte against their sources, and the fingerprint is unchanged by the sweep (hash_*_sweep.txt). 50,406 asset
  links remain (dds/ogf/ogg/omf/anm, level geom/cform/spawn, db archives, editor files; list in run_noguard.sh).
  No file under run/ is hardlinked.
- Guard tools/run_noguard.sh, called by bench.sh, tb_run.sh and det_run.sh before anything starts. It refuses (exit 7)
  if any non-asset link resolves into ~/Games, or any file under run/ has a hard-link count above 1. Witness test:
  clean mock passes; a mock .ltx link and a mock hard link are both refused. Not covered: coopload/*/db (directory
  links to the read-only Anomaly db; co-op measurement is paused; to be guarded before it resumes).

## Desk check: did any OPTION value change under the runs? (Overseer, 2026-11-22)

- The 01b8 content (before step 0) CANNOT be reconstructed: no run saved a copy, and the engine log does not print
  the file. So this is NOT a byte comparison, and "the option values were constant" is not shown. What is shown,
  from the scripts in this run tree:
  - Writers that fire by themselves in a headless run (load a save, no menus, no keys, no fast travel), all in
    bookkeeping sections: itms_manager.script ([temp] item strings), wpn_sway_enable.script ([temp] base_sway),
    axr_main.script ([temp] rspec_default, only on first creation), modded_exes_migration.script ([modded_exes]
    version), lower_weapon_sprint.script ([lower_weapon_sprint] blacklist, rebuilt with Lua pairs(), so the ORDER
    changes from run to run with the same set; seen in the key diff against the installer copy).
  - Writers that change OPTION values need a player action: ui_options / ui_mcm (menu apply), ui_main_menu /
    ui_mm_faction_select (new game), game_fast_travel (a fast travel; it writes an undefined variable, so
    stay_time becomes empty), zz_ui_debug_inputs (key binds), game_achievements (unlocked_factions). The MCM
    session counter (ui_mcm) bumps only when a session starts with no level present; no "MCM Session ID" line
    appears in my run logs.
- Reading: runs rewrite the file on every start, and the content changes (blacklist order, [temp]), but no
  automatic writer touches [options] (A-Life, video) or [mcm] values. So the option values were MOST LIKELY
  constant for all tier (b) runs since 11-18. Not proven.
- If an option value did change, the runs at risk are: every tier (b) and tier (a) run after the change. For A-Life,
  [options] alife/general/* (dynamic_relations, excl_dist, heli_spawn) are read at game start, and in the current
  file they equal Caden's own settings, not the installer defaults. Their earlier values are unknown. Within each
  tier (b) verdict, candidate and reference runs were interleaved or close in time, so a one-time change would split
  them only if it fell between groups. R1/R2 (11-20/21) and the C controls (11-22) were run on different days.
- Key diff (current vs installer pristine) kept in out/axr_options_keydiff.txt.

## AMENDMENT before any 1a run (Overseer OK, 2026-11-22 ~23:40)
Reason: the config changed since step 0 (above), so step 0's K means are not a fair reference for the frame sanity
check. Added: 3 + 3 fresh Coarse14.exe reference runs (K0), interleaved: Rostok K0 K' K' K0 K0 K', Cordon K' K0 K0 K'
K' K0. The sanity check compares K' frame against the fresh K0 means (within 5 %). main <= (b) is checked per K' run as
before. The abort rule is UNCHANGED: main < 0.6 ms in Rostok (K' mean) -> R1 stops.

## RESULT (scored once, 2026-11-22 23:10; series 22:51-23:07, all 12 runs VERDICT clean, no re-runs)
Numbers in r1a_score.txt (r1a_zones.py on the K' runs, r1_split.py on K' and K0). Means, ms per frame:

| | static0 | dynamic0 | forward | main | (b) | K' frame | K0 frame |
|---|---|---|---|---|---|---|---|
| Rostok | 0.395 | 0.025 | 0.167 | **0.587** | 2.129 | 5.727 | 5.727 |
| Cordon | 0.272 | 0.011 | 0.007 | 0.291 | 1.653 | 4.609 | 4.272 |

Sanity checks as registered:
- main <= (b) in every K' run: PASS (6/6).
- K' frame within 5 % of the fresh K0 mean: Rostok PASS (0.0 %); Cordon FAIL (+7.9 %).
Registered consequence: a failed sanity check means "report that instead of a decision". So step 1a gives NO
registered decision. I do not override that.

Reported only, not a decision:
- Rostok, where every check passed: main = 0.587 ms, under the 0.6 ms abort bar (by 0.013 ms; per run 0.585-0.588).
  If the Cordon failure is ruled noise, the registered rule says R1 STOPS.
- The Cordon gap is not in the render feed: K' vs K0 feed 1.862 vs 1.831, (a) 0.144 vs 0.142, (b) 1.653 vs 1.625.
  It is in the rest of the frame. The K0 Cordon runs themselves spread 4.10-4.52 (10 %); K'
  spreads 4.52-4.67. The zones add 3 probe calls per pass, too few to cost 0.34 ms. My guess: run-to-run
  noise in the non-render part of the Cordon frame. Not shown.
- My prediction (main 0.5-0.8, central 0.65; static0 biggest, 0.25-0.4; Cordon 0.3-0.6): main landed below my
  central value; static0 was biggest as predicted (0.395, at the top of the range); Cordon main 0.291 just below
  my range.
