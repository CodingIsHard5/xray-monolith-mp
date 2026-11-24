# T3 (real play), X-Ray side: design note

Status: DESIGN, not built, nothing registered yet. EnginePerf, 2026-11-24, on the Overseer's order after B1-T2 closed
(r3 PASS on both sides). To be agreed with ITRZone (their note: docs/design-notes/2026-11-23-t3-pose-height-design.md).
Everything marked "read from code" is untested.

## 1. What changes from T2
In T2 a rig pawn replayed a track that X-Ray itself had recorded, so the pose's height was X-Ray's ground.
In T3 a real ITR2 player moves freely. Four things follow.

| # | Item | T2 | T3 |
|---|------|----|----|
| 1 | Height | pose carries x, y, z; X-Ray applies all three | X-Ray picks its own ground at the pose's x and z |
| 2 | Stance | none | pose carries stand / crouch / prone; X-Ray sets it on the actor |
| 3 | Bad placement | 3 ticks of ~90 m in r3 (prereg section 32) | a guard after every set, plus the target logged |
| 4 | Zone | one level (Cordon) | the player can cross to another level (R3) |

## 2. Grounding: X-Ray places the actor on its own ground
- Pose line for T3 (ITRZone's proposal, agreed): x, z, yaw, stance, plus ITR2's height sent only as a hint.
- Floor choice: `level.vertex_id` at (x, hint + 0.3 m, z). The engine picks the highest AI node at or below the y it
  is given (level_graph.cpp:259), so the hint selects the floor on stairs and in buildings. X-Ray never uses the
  hint as the actor's height.
- Near-node rule, as in T2 (prereg section 24): the own cell first, then +-2 cells (1.4 m). No node: HOLD at the last
  good point and log it.
- Height: the chosen node's height plus a small lift (0.2 m, to be fixed in the prereg), so the actor's box does not
  start inside the ground.
- Setter: `set_actor_position(pos, true)`, the skip-collision form (script_game_object2.cpp:386-402). It sets the
  position and zeroes the velocity without running the collision corrector that threw the actor 90 m in r3.
  Gravity then settles the actor onto the real collision ground.
  - Not known yet: whether settling from 0.2 m every 50 ms makes the actor bob or take fall damage (g_god is on in
    the harness, but real play is not). A pre-flight must measure the height wobble.
  - Alternative if it bobs: set x and z every tick, and set y only when the node changes or the actor is more than
    0.5 m off the node height.
- Spots with no AI node (some roofs, props): a downward ray onto collision geometry. It needs a script call that
  X-Ray may not export; to be checked. Until then such spots HOLD.

## 3. Guard and logging (Overseer, 2026-11-24)
- After every set, read the actor's position. If it is more than **5.0 m** from the target, set the actor back to
  the last good position (skip-collision form) and log `GUARD,<tg>,<seq>,<dist>,<target xyz>,<got xyz>`.
- Every applied pose logs its target: the P line gains the target x, y, z and the node id.
- T3's prereg gets a check: **no tick moves the actor more than 5.0 m** (from the P lines: the distance between
  consecutive applied positions, and zero GUARD lines, or each GUARD line named).
- ITRZone logs x, y, z per pose write (agreed), so any such event can be replayed offline.

## 4. Stance
- X-Ray has no script call that sets the actor's stance (read from code). Stance is the bit mask `mstate_wishful`
  (Actor.h:474): mcCrouch, and mcCrouch + mcAccel for the low crouch. Physics turns it into the real state and
  swaps the collision box (Actor.cpp:1760).
- X-Ray has no prone. Proposed mapping: stand -> none; crouch -> mcCrouch; prone -> mcCrouch + mcAccel (low crouch).
- **Proposed engine change, to be registered before it is built:** one script export on the actor,
  `set_actor_stance(0|1|2)`, which sets or clears those two bits in `mstate_wishful`. Branch `perf/t3-stance`, on top
  of perf/freeze-absorb. Nothing calls it unless the bridge does, so stock play is unchanged (Rule One).
  - The key-press route (`level.press_action`) is rejected: it depends on key bindings and on the crouch-toggle option.
- Why it matters: GAMMA's vision script reads the real crouch state (visual_memory_manager.script:93-96) and
  applies the stealth multipliers. With the export, mutants and stalkers notice a crouched ITR2 player more slowly,
  as they would a real X-Ray player.
- Open: the player's own input also writes mstate_wishful every frame (ActorInput.cpp). In the headless host nobody
  presses keys, but the export should be re-applied on every pose, not once.

## 5. R3: zone crossing (Cordon -> Garbage)
- Trigger: the pose's zone field changes to a level other than `level.name()`. Today the script logs ZONE_MISMATCH
  and holds; that stays as the safe default.
- New: a table maps (from level, to level) to an X-Ray entry point (position, level vertex, game vertex, direction),
  taken from the stock level changers. On a mismatch with a table entry, X-Ray calls `ChangeLevel(pos, lvid, gvid, dir)`
  (_g.script:406), the same event the stock level changer sends.
- During the load X-Ray writes no snapshots (tens of seconds). ITR2 holds or clears its puppets, as it does for a
  restart. The bridge's 30 s heartbeat must not count the load as a hang: a `LEVEL_CHANGE` line before the call, and
  the bridge waits for the first snapshot with the new level name, with a limit (to be set from a measured load).
- A-Life: a level change is stock behaviour. The simulation carries on, the old level's objects go offline, and the
  new level's come online. Nothing is cut (Rule One). Game time advances by the real load time unless the absorb
  is armed for it; my proposal is NOT to absorb, because stock play does not either.
- Not known, needs one X-only pre-flight before anything is registered:
  - whether the puppet script's state survives a level change or `on_game_start` runs again (seq, run_id, file handles);
  - how long Cordon -> Garbage takes headless;
  - whether the snapshot header's level field changes cleanly.
- ITR2 side (ITRZone's to design): its own world change to the matching zone, and which side leads. Proposal: ITR2
  leads (the player walks through), X-Ray follows the pose's zone field.

## 6. Known small defects to fix in the same script revision
- `on_game_end` is not a callback in this GAMMA build (prereg section 30): use a real one so the end-of-run summary
  and the end disarm run.
- The P line's distance mixes "actor held" with "following error"; with the target logged (section 3) both can be
  reported separately.

## 7. Questions that are Caden's (via the Overseer), not mine
1. Prone: is low crouch an acceptable stand-in, since X-Ray has no prone?
2. Ground mismatch: where the two games' grounds differ by up to 2.4 m (ITRZone's map of such spots), the player's
   body is on each game's own ground, so a puppet or the player can look off by that much in the other view. Accept,
   or fix the worst spots in content?
3. A crash restart restarts X-Ray from the start save, so A-Life progress since then is lost (Rule One). Autosave
   more often, or accept for now?
4. STALKER anomalies and the ITR2 player: X-Ray's anomalies act on X-Ray's actor (damage, and some move it). The
   bridge puts the actor back every tick, so a moving anomaly has no lasting effect, but damage would count. Wanted?
   (The r3 90 m event was NOT an anomaly; this question stands on its own.)

## 8. Proposed order (each step registered before it runs)
1. Script revision: grounding, guard, target logging, callback fix. Offline tests with the real mesh, as in section 24.
2. X-only pre-flight A: grounding on the 0-380 s track with heights REMOVED from the pose (x, z, hint only).
   Checks: no tick over 5 m, no GUARD line, height wobble measured, HOLD count.
3. Engine: `set_actor_stance` export, CI build, X-only pre-flight B (stance changes; the crouch state read back;
   an NPC's visibility value with and without crouch as the witness).
4. X-only pre-flight C: Cordon -> Garbage by ChangeLevel, with the three unknowns of section 5 measured.
5. Joint T3 registration with ITRZone, then the joint runs.
