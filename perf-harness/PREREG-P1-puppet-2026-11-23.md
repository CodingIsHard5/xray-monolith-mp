# P1: headless X-Ray puppet probe (JOINT prereg, EnginePerf + ITRZone; REGISTERED 2026-11-23, before any run)

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
- INVULNERABLE, with hits still logged (Overseer). A callback on actor_on_before_hit, signature (s_hit, bone_id, flags) as in momo_multihit_fix.script, logs every hit
  to the export: time, hitter id and section, hit type, power, bone. Then it sets flags.ret_value = false and s_hit.power = 0,
  which cancels the hit (the same mechanism momo_multihit_fix.script uses). Callback order is NOT guaranteed (the callback
  registry is a Lua table). Backup: if health is below 0.5 on any update,
  it is set back to 1 and the event is logged. Min health is reported. Death would be the walk-stall artefact again,
  so ANY actor death voids the run.
  Note: other mods' before-hit handlers may run before mine, so the GAMMA "Damage to actor" lines still appear. They
  are the damage the hit would have done, not damage taken.
- Export, 10 Hz: every >= 100 ms, one line per object in db.storage that is online and is a monster or stalker:
  t, id, section, x, y, z, health, best_enemy id (or -1), distance to the actor. Also one actor line with position
  and health. It appends to Z:\dev\shm\p1\export_<tag>.csv (tmpfs, opened once, flushed each tick).
  Online/offline switch events (fixed at build): an id appearing in db.storage between two export ticks is written as an ON line
  (with its distance), and an id disappearing as an OFF line. db.storage holds an object only while it is online.
- Export cost: each export tick is timed with the engine's high-resolution timer (Lua profile_timer, in microseconds;
  os.clock is not used because it is process CPU time), and the per-tick mean and p99 are written at the end.
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
- Online switch: SWITCH DISTANCE FOR THE SCORER = 450 m (--switch 450), from run/gamedata/configs/alife.ltx
  (switch_distance 450; auto_switch on: 1250 m for 10 s after load, then 450). The script starts track time 15 s after
  the first update, so the 450 m value is in force from t = 0. The value seen at load is written to the meta file. If
  GAMMA scripts change it during the run, I say so; the scorer still uses 450. My guess: P1-switch PASS at 55 %
  (squad-based switching and hysteresis may pull the first-seen distance under 450 by more than 25 %).
- RssAnon 3-6 GB (a guess; never measured on this rig).

## 6. ITRZone (track, scorer, predictions)
- Track: out/p1/cordon_full_track.csv (sha256 prefix 213ff8f125723f01), made by tools/p1_track.py (ITRZone 1c68868) from ITR's M3 route.
- Scorer: ITRZone tools/p1_check.py (commit 63195d7, blob 56edce4c). It reads R1's export only and is scored once:
  `p1_check.py export_on.csv cordon_full_track.csv l01_escape/level.ai --switch <m> --actor-id <id>`, where --switch is A-Life's switch distance as written in section 5 before the run.
  - P1-track: the actor follows the track. The x,z error against track(t), off-mesh hold t 407-615 excluded, median < 0.5 m. This checks the mover; a FAIL voids P1-switch and P1-chase.
  - P1-mesh: creatures stand on X-Ray's AI mesh, so on ITR2's ground (level.ai matches our ground to a 2 cm median). The 3-D distance to the nearest level.ai node within 2 cells, over all o lines: median < 0.5 m and p95 < 1.5 m. This is the puppet question for ITR2.
  - P1-switch: the first-seen distance of each creature that appears after t 5 (those online at load are excluded): median within +-25 % of --switch.
  - P1-chase: at least one creature has best_enemy = the actor for >= 10 s while its distance falls by >= 10 m.
  - P1-hits: at least one h line with hitter_kind m.
  - P1-chase and P1-hits are "no test", never FAIL, if no monster comes within 50 m of the actor (empty lairs).
- ITRZone predictions:
  - P1-track 90 %; P1-mesh 85 % (p95 < 1.5 m on its own 75 %);
  - P1-switch 60 % (X-Ray may switch by squad, with hysteresis, so the first-seen distance can sit below the nominal value);
  - P1-chase 60 % and P1-hits 50 %, both given a mutant squad within 50 m;
  - "no test" for chase and hits 25 %.
- What follows for ITR2:
  - P1-mesh PASS means X-Ray positions can drive ITR2 puppets on our ground with no correction step.
  - P1-chase or P1-hits FAIL with a mutant near means a script-moved actor is not a usable target. The hybrid then needs another way to give X-Ray the player, and is reported before any ITR2 bridge boot (P2).

## Build note (EnginePerf)
The script is harness/zz_p1_puppet.script (copy in perf-harness/). It parses with luac. Lua names it uses were checked in source:
set_actor_position, set_health_ex, best_enemy, is_monster, is_stalker, profile_timer, alife():switch_distance, and the SHit fields
power, draftsman and type. The driver is planned as tb_run.sh with TB_TIMES=1100 (alife dump at load and at 1,100 s of engine time, then quit),
plus a heartbeat watcher like stall_run.sh. Both are untested until the box run.

## Export formats (fixed to ITRZone's scorer, 2026-11-23)
Z:\dev\shm\p1\export_<tag>.csv holds only these lines, with no header:
  a,t,x,y,z,health
  o,t,id,section,kind,x,y,z,health,best_enemy,distance      (kind m = is_monster, s = is_stalker; position = object
                                                             position, which in X-Ray is at the feet)
  h,t,hitter_id,hitter_section,hitter_kind,type,power,bone  (hitter_kind m/s, a = actor, x = other or none; power is
                                                             the hit's power before it is cancelled)
t = track time in s (negative during the 15 s wait). Everything else (S start line with actor_id, ON/OFF switch events,
REFILL, TICK every 60 s, TRACK_END, SUMMARY with export cost) goes to Z:\dev\shm\p1\meta_<tag>.csv. The actor id
is written in the run report from the S line (expected 0 in single player).

## Scorer addendum (pre-run, ITRZone f8e8273, blob 8ea73f79)
p1_check.py skips export lines with t < 0 (the 15 s settle wait after load). Nothing else changed. Scored with --switch 450 --actor-id <from meta's S line>.
Driver: tools/p1_run.sh (copy in perf-harness/), Overseer GO 2026-11-23.

## R1 "on" (2026-11-23 00:15:38-00:19:54): VOID, stopped as registered. R2 not run. Nothing scored.
Two independent reasons, each enough on its own:
1. ACTOR DEATH at track time about 150 s (screenshot out/p1/on/screen.png: GAME OVER). The registered rule says any
   death voids the run. Cause, from source: GAMMA's grok_actor_damage_balancer.script applies actor damage ITSELF in its
   own actor_on_before_hit (set_health_ex(actor.health - damage), line 751; it sets ret_value=false itself, lines 648
   and 853). So cancelling the engine hit in my handler does not stop that damage, and the handler order is not fixed.
   My backup refill at < 0.5 fired 21+ times (min health 0.274) and could not keep up with several cats (hits about
   0.24) and a bandit. My mechanism was wrong. The Overseer's requirement (invulnerable, hits logged) was NOT met.
2. NOT EXCLUSIVE: an ITR2 process (IntoTheRadius2 on :88, started 23:43:43, about 2.2 cores, "GameThread") ran during
   the whole run. Its slot-2 claim had expired at 00:13:59 (STALE2) while the process kept running. tb_run logged
   heavy_others=[215%:GameThread] at start, but my p1_run.sh settle checked only load (1.81 < 2). That is a driver
   gap of mine: r1_series.sh also checks heavy processes.
Facts seen in the void run (NOT scored, exploratory only): 107 hits logged in about 150 s (cats cat_weak_d/cat_strong_a,
one bandit), so h lines with hitter_kind m are written. Export cost mean 560 us, max 7.2 ms per tick, above my
0.05-0.5 ms predicted range (and the machine was shared). Memory RssAnon about 3.3 GB, VmRSS about 17 GB. The switch
distance at load was 1250 (auto_switch), as expected.
Proposed for the ONE retry (needs Overseer OK, as an amendment before any run): (a) g_god on (the engine zeroes hit power)
PLUS a refill to 1.0 on EVERY actor update instead of below 0.5, so that one balancer hit (capped by its own line 733) cannot
reach 0. The hit log is unchanged. (b) The driver refuses to start a run while any other process uses 100 % or more
of a core (as r1_series), with the 5-minute settle cap.

## RETRY AMENDMENT (Overseer OK, 2026-11-23; registered before the retry; this is the ONE retry)
Desk check (source): god mode does NOT block set_health_ex. CScriptGameObject::SetHealthEx (script_game_object4.cpp:451)
-> CEntity::SetfHealth (Entity.h:39) -> condition SetHealth, which writes health directly with no GodMode check.
GodMode only zeroes the engine's own hit power (Actor.cpp:671) and stops condition updates (ActorCondition.cpp:219).
GAMMA's balancer damages through set_health_ex, so god mode alone would NOT have saved the actor.
Changes:
(a) Engine-level god mode: `g_god on` at the first actor update (registered under -dbg, which the run passes); its
    state is written in meta's S line. The health refill stays as the backup and is raised to 1.0 on EVERY actor update,
    plus a second refill after each hit (actor_on_hit_callback). From 1.0, one balancer hit cannot kill: its own
    "avoided one shot" rule (line 733) leaves 0.25. Two big hits in one frame still could. A death still voids the run.
(b) The driver refuses a run (5-min settle cap) while any other process is at >= 100 % CPU, as r1_series.sh does.
    Output goes to out/p1_retry/.
Added check, same run: HIT LOGGING UNDER GOD MODE. If any monster comes within 3 m of the actor (o lines) and there
is not a single h line with hitter_kind m in R1, then hit logging failed under god mode. I report it as FAILED, and P1-hits
becomes "not measurable", not a PASS or FAIL.
Not changed: everything else, including itrzone's scorer, predictions and the formats. Note: REFILL lines now mean
health fell below 0.5 between two updates.

## RETRY RESULT, EnginePerf part (scored once, 2026-11-23 01:20; runs 00:33-00:52 "on" and 00:52-01:11 "off")
Both runs are CLEAN: start load 0.81 / 1.66, heavy_others=[] at start and end, the full track walked in both
(TRACK_END 1021.4 s), rc=0, no stall, and no death (min health 1.000, 0 refills). g_god=true in both S lines.
Data: out/p1_retry/{on,off}/ (export_on.csv has 425,786 lines and 9,569 export ticks).
- HIT LOGGING UNDER GOD MODE (amendment check): FAILED. Monsters (cats) were within 3 m of the actor in 981 o lines
  and had best_enemy = actor (id 0) in 33,855 o lines, yet there are 0 h lines. GAMMA's "Damage to actor" prints are
  also absent. With g_god on, the actor before-hit callback does not fire, so P1-hits is NOT MEASURABLE (per the amendment).
  Lesson for the hybrid: engine god mode hides hits from scripts. To see hits on an invulnerable actor, keep god mode
  off and neutralise damage another way (for example, hook GAMMA's balancer).
- Export cost, predicted 0.05-0.5 ms per tick (central 0.2): mean 0.452 ms, p99 0.754, max 6.48. Inside the range,
  at its top; my central guess was too low. "Under 0.1 % of main-thread time" was WRONG: 10 Hz x 0.45 ms = 0.45 %.
- Frame-time difference R1 against R2: NOT DELIVERED. The driver did not capture frame times (tb_run has no frame
  CSV). That is my gap. A stand-in, reported only: the engine process's CPU ticks at about 18 min were 290,198 (on)
  against 292,789 (off), so the export cost is not visible at process level (well inside run-to-run noise).
- No hang over the full track in either run: PASS. No death: PASS.
- Hits on the actor: NOT MEASURABLE (above).
- RssAnon, predicted 3-6 GB: 3.26-3.56 GB in "on" and 3.34-3.56 GB in "off": PASS. VmRSS about 17 GB (mapped archives).
- Switch distance at load was 1250 (auto_switch), and 450 from t = 0 as designed. It was not re-read during the run.
- P1-switch, P1-track, P1-mesh, P1-chase: ITRZone's scorer (section 6), reported separately.

## RETRY RESULT, ITRZone scorer (scored once by ITRZone, p1_check.py blob 8ea73f79, --switch 450 --actor-id 0)
- P1-track PASS: 7,526 actor samples, error median 0.29 m, p95 0.60 m.
- P1-mesh PASS: 411,071 creature samples, distance to the nearest level.ai node median 0.28 m, p95 0.46 m.
- P1-switch PASS: 36 of 76 creatures appeared after t 5; first-seen distance median 372 m (band 337.5-562.5).
  ITRZone notes it is low in the band (creatures coming online inside the radius as the actor moves), not a spawn edge.
- P1-chase PASS: 11 chases (e.g. cat_weak_d 23200 at t 39, 59 -> 7 m; boar_01a_hard 23185 at t 978, 74 -> 53 m).
- P1-hits: NOT MEASURABLE (the scorer prints FAIL with 0 hits; both sides agree that, under the amendment, it is neither).

## Predictions, each scored
ITRZone: P1-track 90 % -> PASS. P1-mesh 85 % -> PASS (p95 < 1.5 m alone, 75 % -> PASS). P1-switch 60 % -> PASS.
P1-chase 60 % -> PASS. P1-hits 50 % -> not measurable. "No test" for chase and hits, 25 % -> did not happen (monsters came
within 50 m).
EnginePerf: export 0.05-0.5 ms -> inside (0.45; central 0.2 wrong). "< 0.1 % of main thread" -> WRONG (0.45 %).
No hang -> right. No death -> right (on the retry; the first run died). Hits near 5_2 / 5_4 -> not measurable.
P1-switch PASS at 55 % -> PASS. RssAnon 3-6 GB -> right (3.3-3.6).
What it means (ITRZone's section 6 rule): P1-mesh PASS means X-Ray positions can drive ITR2 puppets on ITR2's ground
with no correction step. A script-moved actor IS a usable target (chase PASS). Whether it can also be HIT visibly is
still open (the hit test was not measurable).
