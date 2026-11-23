# P1: headless X-Ray puppet probe (JOINT prereg, EnginePerf + ITRZone; DRAFT, not yet registered)

Status: draft. EnginePerf wrote the X-Ray side (sections 1-5). ITRZone adds the track, the scorer and its predictions
(section 6) and agrees. It counts as registered once it is committed with both parts. Then the Overseer is asked for
the box. Nothing runs before that.
Order: Overseer, 2026-11-22/23. Approved in principle. Desk study: ~/Projects/ITRZone/docs/design-notes/
2026-11-22-xray-puppet-feasibility.md (83c58eb). Question: can a headless X-Ray session, with its actor moved from
outside along an ITR track, give mutant states (online switching, chase, hits) at 10 Hz, and at what cost?
This is a probe only. No engine change. It adds one new script, which loads only in my run tree (never in Caden's
install; run_noguard.sh is checked before every run).

## 1. Rig (EnginePerf)
- Exe: RefDet14.exe (upstream dev-mt + profiler; -det_mode NOT set, so normal behaviour). The walk stall was
  classified on it, so it is the known exe on this rig.
- Save: cordon1 (Cordon, used in R1). At load the script teleports the actor to the track's t=0 point (south village).
- Headless as tools/tb_run.sh: Xvfb :77, `wine explorer /desktop`, normal user.ltx, -nointro -dbg. The run is
  EXCLUSIVE, through WANT and fair share.
- The track is a private copy of /home/caden/Projects/ITRZone/out/p1/cordon_full_track.csv (sha256 prefix
  213ff8f125723f01). It goes into run/ (not a link), and its hash is checked before each run.

## 2. Script (new: run/gamedata/scripts/zz_p1_puppet.script; inert unless the command line has -p1)
- Move, 20 Hz: on actor_on_update, every >= 50 ms of time_global, the actor is placed with set_actor_position at
  the track point for elapsed time t (linear interpolation between samples). Off-mesh section (node_dist = -1, t 407-615):
  the actor HOLDS the last on-mesh point, then resumes. Track time = engine time since the first teleport.
- INVULNERABLE, with hits still logged (Overseer). A callback on actor_on_before_hit (registered last) logs every hit
  to the export: time, hitter id and section, hit type, power, bone. Then it sets flags.ret_value = false, which
  cancels the hit (the same mechanism momo_multihit_fix.script uses). Backup: if health is below 0.5 on any update,
  it is set back to 1 and the event is logged. Min health is reported. Death would be the walk-stall artefact again,
  so ANY actor death voids the run.
  Note: other mods' before-hit handlers run before mine, so the GAMMA "Damage to actor" lines still appear. They
  are the damage the hit would have done, not damage taken.
- Export, 10 Hz: every >= 100 ms, one line per object in db.storage that is online and is a monster or stalker:
  t, id, section, x, y, z, health, best_enemy id (or -1), distance to the actor. Also one actor line with position
  and health. It appends to Z:\dev\shm\p1\export_<tag>.csv (tmpfs, opened once, flushed each tick).
  Plus online/offline switch events through the binder-independent callbacks: server_entity_on_register /
  on_unregister, or the squad online flag compared at each export tick. Which one is used is fixed at build and
  written here before the run.
- Export cost: each export tick is timed with the engine's high-resolution timer (profiler zone "p1_export" plus
  os.clock), and the per-tick mean and p99 are written at the end.
- Heartbeat: the export file grows every 100 ms. A stall = no growth for 30 s while the engine lives. It triggers the
  same captures as tools/stall_run.sh (screenshot, per-thread CPU, log tail, winedbg if idle).

## 3. Runs
- R1 "on": export on, full track (1,021 s of track time; the run cap is 1,500 s wall from start).
- R2 "off": export off, same track and save. The move and hit-log stay on. It measures the export's cost (frame time
  and main-thread time, R1 against R2) and gives a second sample of the AI side.
- N for "no hang": the full track, 1,021 s, in both runs. The engine must stay alive and the heartbeat (R1) or the log
  (R2) keep growing to the end of the track.
- Stop rules: crash, timeout, actor death, or a failed check -> stop and report, at most one retry. A run whose start
  load is >= 2 is marked disturbed and re-run once.
- Box: 2 runs of about 20 min each, one slice, released after.

## 4. Reported by EnginePerf
- Export cost per tick (mean, p99, ms) and the frame-time difference R1 against R2.
- Memory: VmRSS and RssAnon (RssAnon is the private part; VmRSS includes mapped archives, see the walk-stall note).
- Hits: count, hitters, cancelled; min actor health.
- Hang: none, or classified with stall_run's rule.
- Squads at the track's mutant smarts at load (esc_smart_terrain_5_2, 5_4, 4_11, 5_6, 6_6), from the export's
  first ticks and a k=0 alife dump. A lair counts only if A-Life has a squad there.

## 5. EnginePerf predictions (scored by the rule above)
- Export cost: 0.05-0.5 ms per tick (central 0.2); at 10 Hz, under 0.1 % of main-thread time.
- No hang over the full track in either run; no actor death (hit cancel works).
- Hits on the actor: yes, at least one mutant hit near 5_2 or 5_4, IF a squad sits there at load (a coin toss).
- Online switch: creatures appear in the export at about the A-Life switch distance. I don't know GAMMA's value; it is
  read from the save at load and written here before the run.
- RssAnon 3-6 GB (a guess; never measured on this rig).

## 6. ITRZone (track, scorer, predictions): to be added by ITRZone
