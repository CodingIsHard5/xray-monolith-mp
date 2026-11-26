# Kill channel, X-Ray side: DESK DRAFT (2026-11-26). Not built, not registered, nothing run.

Ordered by the Overseer. The other half is itrzone's note (ITRZone docs/design-notes/2026-11-26-kill-channel-itr2-side.md).
The joint pose-v4 text is written with itrzone after amendment 125 is registered. Any box run needs the Overseer's GO.
Everything here is a change to the bridge script only (today harness/zz_p1_puppet_lost3.script). No engine change.

## What I checked in the engine source, and what I did not
Checked by reading (perf/sim-tick tree):
- `obj:set_health_ex(v)` sets the creature's health directly, clamped to -0.01..1.0 (script_game_object4.cpp:451). No
  armour, no immunity. Health is on a 0..1 scale, so "a share of full health" needs no table on my side.
- `obj:hit(h)` sends a normal hit event; it goes through immunities and armour and arrives on a later frame
  (script_game_object2.cpp:186). The hit records its source as "who hit last" (EntityCondition.cpp:448).
- `obj:kill(who)` kills at once with `who` as the killer; it does nothing on an object already dead
  (script_game_object_use.cpp:108).
- A creature whose health reaches 0 without a kill call dies on its next update, credited to "who hit last"
  (entity_alive.cpp:232). That field starts at 0, which is the actor's id.
- Offline: `alife():kill_entity(se_obj)` exists; the script form passes NO killer (alife_simulator_script.cpp:125-133).
  There is no script call that changes an offline creature's health.
- Ids: a released id goes back to a pool and CAN be handed out again later (id_generator.h).
NOT checked, each needs a test before any registration:
- `set_health_ex` on every monster class and on stalkers in every state (wounded, in a scripted scene, in a vehicle).
- What GAMMA's own hit and death callbacks do with a near-zero hit from the actor (pain animation, sounds, "you are
  being shot" reactions, hit markers), and with `kill(actor)` (statistics, goodwill, squad turning hostile).
Checked after the first draft: the script's Lua state does NOT survive a level change. The script already carries
its crossing state (session number, last seq) over in `cross.txt` and reads it back at game start for that reason
(zz_p1_puppet_lost3.script lines 319 and 585). So the table is written beside it at the crossing request and read
back at the resume. An X-Ray restart (new run id) does not read it.

## 1. Reading the report
- `k,<id>,<total>,<dead>` lines between `p` and `end`, taken only from a complete pose (header seq = footer seq, as
  today) of version 4. How version 4 is marked in the header is for the joint text; a v3 reader must ignore `k` lines
  by rule and a v4 reader must accept a pose with none.
- Refused and counted, never applied: id 0 (the actor), an id that is not a creature or stalker, a total that is not a
  number or is below 0, more than 64 `k` lines in one pose.
- Nothing is applied while the pose is LOST, in a crossing, or when the pose's level is not this level.

## 2. The table (one row per id seen in a `k` line)
`applied` (share already applied), `dead_done` (time the death was applied or seen), `section` (the creature's
section name when the row was made), `first_total`.

## 3. Applying a part hit (creature online and alive, `total - applied` > 0)
1. Send a hit with the actor as source, power 0.0001, direction from the actor to the creature, no impulse. Purpose:
   the creature and GAMMA's scripts see "the player hit me". It also sets "who hit last" to the actor.
2. Lower health directly by the difference: `set_health_ex(health - (total - applied))`.
   If that would reach 0 while `dead` is 0, stop at 0.01: only a `dead=1` report kills. (ITR2 decides death; a
   rounding difference must not kill a creature the player still sees alive.)
3. `applied = total`. Log `KAPPLY,<t>,<game ms>,<id>,<difference>,<applied>,<health before>,<health after>`.
- A difference above 1.0 is cut to 1.0 and counted.
- A total BELOW `applied` inside one ITR2 session is ignored and counted (`KBACK`): totals only grow.

## 4. Applying a death (`dead` = 1)
| the creature in X-Ray | what the script does |
|---|---|
| online, alive | part hit as above if owed, then `kill(actor)`. Log `KDEAD,...,online` |
| online, already dead (X-Ray's own fight got there first) | nothing; confirm dead |
| offline, alive | `alife():kill_entity(se_obj)`. No killer can be named: no statistics, no relation change. Log `KDEAD,...,offline`. Counted; expected rare |
| not there any more (released) | confirm dead |
After any of these the row keeps `dead_done` and the id is confirmed dead for 10 s (itrzone asked for at least 5).

## 5. Offline part hits (owed)
A part hit for an offline creature is not applied and not confirmed: `applied` stays where it is, so the snapshot
shows less than the total. It is applied when the creature is online again. Log `KOWED` once per id. If the creature
never comes online again on this level, the hit is never applied; itrzone retires its line by its 60 s rule.

## 6. Confirming, in the snapshot
My choice: lines of their own, after the `o` lines and before `end`. The `o` line and its readers are not touched.
- `k,<id>,<applied>` for every id in the table with `applied` > 0 that is alive, online or not.
- `k,<id>,<applied>,dead` for 10 s after a death was applied or seen.
Snapshot version: the next number in the snapshot's own series, named in the joint text. A reader of the older
version must ignore `k` lines by rule.

## 7. The id-reuse rule
- A row is tied to the section name seen when it was made. If the id later belongs to an object with another section
  name, or the row's death was confirmed more than 10 s ago and the id is alive again, the `k` line is STALE: nothing
  is applied, it is logged `KSTALE`, and the snapshot carries `k,<id>,<applied>,dead` again so the writer drops it.
- Asked of itrzone: drop a `k` line once my `dead` confirmation for it was read, and never send a line for an id that
  was not in a snapshot of the current X-Ray run id.
- Not covered by a section name: the same id reused by another creature of the SAME section within the 60 s that
  itrzone keeps a line. I think this cannot happen that fast (the pool hands out the longest-free block first) but I
  have not shown it. If the joint text wants it closed, the `k` line can carry the section name.

## 8. When the report or a side is lost
| event | X-Ray side |
|---|---|
| pose writes missed | nothing; every pose carries every total |
| pose LOST (5 s; 120 s in a crossing) | nothing applied; table kept; on the same session it goes on where it was |
| new ITR2 session number | every `applied` set to 0, damage kept, as itrzone proposed. Death confirmations still run out their 10 s |
| an old pose-v2 or v3 writer | no `k` lines: nothing to do |
| X-Ray restarts | the table is gone with the script state and the world is the save's; itrzone drops its totals on the new run id. Kills since the last save are undone: a limit to state to Caden |
| a crossing | ids stay; the table is written to a file at the crossing request and read back at the resume. A crossing that fails before the resume leaves the file; it is only read when the run id matches |
| X-Ray stalled or stopped by the bridge (freeze) | nothing is read; after the stop the newest totals are applied in one step |
| the creature dies in X-Ray first | confirmed dead; an open total for it is dropped |

## 9. Counters (in the T2SUM line) and log lines
`khits`, `kdead_online`, `kdead_offline`, `kowed`, `kstale`, `kback`, `krefused`, `kclamped`.
Lines: KAPPLY, KDEAD, KOWED, KSTALE, KBACK, KREFUSED.

## 10. How I would test my half (each registered before it runs)
1. Offline, in the Lua stub that already runs the real script: missed and repeated reads, a part hit, a death, an
   offline death, an owed hit that is applied on return, a total going backwards, id 0, a non-creature id, a new
   session, a stale id with another section, LOST in the middle. A mutant for every check.
2. X-only boot, stand-in writer (as d0): it picks the nearest stalker and the nearest mutant from the snapshot and
   sends: 0.250, the same again for 5 s (no second effect), 0.600, then `dead=1`. Draft checks, all on outcomes:
   - the creature's health in the `o` line drops by 0.250 +/- 0.01 within 1 s, then by 0.350, and does not rise by
     more than normal regeneration between;
   - `k,<id>,0.250` then `k,<id>,0.600` appear within 1 s; after `dead=1` the id leaves the `o` lines and
     `k,<id>,...,dead` is present for 10 s;
   - the kill is the actor's: the actor's kill statistic rises by one for each, and for the stalker the squad's
     relation to the actor changes as after a normal player kill (read through the script, logged);
   - no Lua error, no crash, the actor takes no damage (the standing rule), no creature other than the two changes
     health at those moments by more than the scene explains (control: the same boot without `k` lines).
3. One joint run with itrzone's rig lever (its draft): part hit, kill, kill during a 20 s freeze.

## 11. RULED by Caden (Discord 2026-11-26 16:50-16:54 UTC, confirmed to me by the Overseer): all four as proposed
1 damage by ITR2's health and weapons, not X-Ray armour or headshots; 2 ITR2 kills count as the player's in X-Ray
(goodwill, hostility); 3 a restart undoing kills since X-Ray's last save is OK for now; 4 looting happens in ITR2.
Section 4's offline death is superseded: it uses the alife object's own kill(), not alife():kill_entity (which leaves
health unchanged, alife_combat_manager.cpp:459). The registered text is itrzone's amendment 134.
The questions as they were put:
1. Feel: the player's shots count by ITR2's health and weapons. X-Ray's armour, hit zones and headshots are not used
   for them. (itrzone's proposal; my half fits either way, but with X-Ray's rules it would be a different design.)
2. A kill in ITR2 counts as the player's kill in X-Ray: statistics, faction goodwill, squads turning hostile. I
   assume yes. The offline case cannot name a killer.
3. Kills since X-Ray's last save are undone when X-Ray restarts.
4. The body: X-Ray makes its corpse with X-Ray loot, ITR2 leaves its own. Which one is searched is a loot question.

## 12. What I am not sure of
- Step 3.1: a near-zero hit may be enough for the reactions, or may be filtered out before the callbacks; it may also
  play a pain animation on every report. Only the X-only boot tells.
- The order "hit event, then direct health" across frames: the hit arrives a frame later than the health change.
  With the stop at 0.01 this cannot kill by itself, but the hit-reaction and the health drop are one frame apart.
- Regeneration: X-Ray creatures regain health over time, ITR2's puppet shows the snapshot's health, so this should
  agree by construction; not looked at.
