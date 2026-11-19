# Pre-registration: tier (a) with separate Lua random streams, and the dose series (2026-11-19)

Written and committed BEFORE the first run with `-det_lua_streams`. Rulings by the Overseer 2026-11-19 (cause 8, dose series).

## Background
The save-diff check missed both controls, and reference runs differ in real script state (weather at 300 s) because all scripts
share one Lua random generator (cause 8). Fix, test-only and det_mode-only: `math.random` is routed by which script calls.
- stream "alife": sim_board, sim_squad_scripted, sim_squad_bounty, sim_squad_warfare, zzz_sim_squad_scripted_milpda,
  simulation_objects, smart_terrain, smart_terrain_warfare, sim_offline_combat, squads_filler, se_stalker, se_monster,
  se_artefact, se_item, se_zones, se_heli, se_car, se_level_changer, se_smart_cover, smr_pop, smr_civil_war, smr_loot,
  heli_alife, heli_alife_warfare
- stream "weather": level_weathers, surge_manager, psi_storm_manager, idiots_surge
- everything else: the stock shared generator
Frames of the test script and of generic helpers (_g, utils_data, utils_obj, utils_item, util) are skipped when finding the
caller. The actual call sites and counts per stream are written by every run (`det_streams_<tag>_<k>.txt`) and reported.
Known weakness, stated now: routing is by FILE. A-Life scripts that also contain code run for online NPCs put those draws on
the "alife" stream, and A-Life code that draws through a non-listed script stays on the shared stream. The site table shows both.

## Runs
Build RefDet12 (CI 44510900390), serial det_mode, save bench1, headless,
`-det_times 25,300,900 -det_save -det_lua_streams`, perturbations applied at 60 s:
- references: d0a, d0b, d0c
- k1: skip the first 1 offline squad update; k10: the first 10; k100: the first 100 (`-det_perturb skip -det_perturb_n N`)
- kd: one extra `math.random()` immediately before the first target choice (`SIMBOARD:get_squad_target`) made for an offline
  squad (`-det_perturb decision`). The extra draw is taken from the "alife" stream (the wrapper's caller is routed as the squad
  script's caller would be; verified from the site table, otherwise the run is void).

## Measure
`perf-harness/tier_a.py A B` gives D = number of tier (a) difference items at each save, by these fixed rules:
1. save object records (state + update packet bytes): one item per differing or one-sided record of an object that is OFFLINE
   (online in neither run's dump) or UNKNOWN (in neither dump). Online objects are informational only.
2. game-time chunk: one item if not byte-equal.
3. registries: info portions, relations, tasks, statistics - one item per differing entry. Specific characters and map
   locations are compared as SETS (order-only differences masked: maps/lists ordered by memory address, `xrstring.h:280`,
   `map_location_defs.h:21`; both used by key lookup only). Game news is informational only (ambient chatter near the player;
   differs between the earlier reference runs in real content).
4. script save: every differing leaf, tables compared as maps (order masked), EXCEPT top-level groups game_object, xr_sound,
   disguise_npcs_memory (client-side state of things near the player) and per-id entries whose id is an ONLINE object.
Counting details of the tool as committed (fixed): a registry "entry" is a key plus one element of its list; a changed value
counts as 2 items (old only in A, new only in B). Other save chunks (spawns, header) are informational.
Checked before this commit on the earlier runs (no `-det_lua_streams`): a run against itself gives D=0 at all three saves; the
old reference pairs s1a/s1b/s1c give D = 4-6 at 25 s, 6-11 at 300 s, 8-13 at 900 s, all from the script save (se_object entries
of offline ids, weather_manager, delay1/delay2). That is the floor BEFORE the stream split; the new runs measure it after.
No other mask. Every excluded class is listed above; nothing may be added after the runs.

## Decision rules
- Floor: F(k) = the largest D among the three reference pairs at save k. Reported as measured, even if not zero.
- A control is CAUGHT if, at 300 s or at 900 s, its D against EVERY reference run is larger than F at that save, AND at 25 s
  (before the perturbation) its D against every reference is within the floor (otherwise void), AND its log shows the
  `* det_perturb` line(s).
- Tier (a) is ADOPTED if kd and k10 are both caught. The smallest caught dose (k1, k10, k100, kd) is reported as the check's
  sensitivity and goes into Rule One together with the floor.
- If kd or k10 is missed: not adopted; I report the floor, what each control changed, and how many offline records change per
  run. A raised A-Life time factor (identical in all runs) is then the next proposal, as a separate pre-registered round.
- Reported in every case: offline records changing between the 25 s and 900 s save per run; the per-stream call-site table.
