////////////////////////////////////////////////////////////////////////////
// mp_coop_owner: the two tiers of ownership the RPG layer resolves against
// (design doc §6 — PLAYER ACTOR for everything about an individual, WORLD
// ACTOR for everything collective; §14 step 8 phase 1).
//
// The engine already STORES every fact this layer needs per entity id
// (CInfoPortionRegistry, CRelationRegistry, CGameTaskRegistry are all
// CALifeAbstractRegistry<u16, ...> and all ride registry().save into the
// .scop). What was missing is the answer to "which entity id?" at each read
// and write — on a server where db.actor is nil, the script layer had no way
// to name either tier. This module answers exactly that, and nothing else:
// no policy, no classification, no storage.
//
//  world_actor()  = a RESERVED REGISTRY KEY, not an entity.
//
//                   The first attempt used ai().alife().graph().actor() — the
//                   level's own actor entity — and the phase-1 harness killed
//                   it on its first run: it measured world=22678 player=22678,
//                   the same entity. CALifeGraphRegistry::update() re-points
//                   m_actor to ANY object spawned with M_SPAWN_OBJECT_ASPLAYER
//                   (alife_graph_registry.cpp:54-58), and every co-op player
//                   actor carries that flag — so graph().actor() does not mean
//                   "the level's actor", it means "whoever spawned last". It
//                   also moves again on a reload, so it cannot be a stable key
//                   across a restart either.
//
//                   The registry this tier lives in is keyed by u16 and asks
//                   nothing of the key but uniqueness: CInfoPortionRegistry is
//                   an xr_map<u16, KNOWN_INFO_VECTOR> saved and loaded whole.
//                   So the world tier does not need an entity, and doc §6.4 is
//                   explicit that collective state should NOT get one ("an
//                   X-Ray actor is a heavy object with position and inventory;
//                   faction state is just data"). A reserved key is stable by
//                   construction — across spawns, reloads and level changes —
//                   and persists for free with the registry.
//
//                   Collision is the one risk, and it is checked rather than
//                   assumed: the server verifies at boot that no live entity
//                   holds the key and says so loudly if one ever does.
//
//  acting_actor() = the player actor on whose behalf the server is currently
//                   executing script, or none when the server is running
//                   autonomous world simulation. Doc §6.1's "check the
//                   interacting player", generalised from the dialogue-only
//                   g_coop_dialog_actor it replaces.
//
// Implementation lives in game_sv_single.cpp (no new translation unit).
////////////////////////////////////////////////////////////////////////////
#pragma once

namespace mp_coop_owner
{
	// No such actor / no interaction in progress. 0 is NOT usable as the
	// sentinel: it is a legal entity id, and the task map already spends 0 on
	// "global", so reusing it would make an unowned task and a task owned by
	// entity 0 indistinguishable.
	static const u16 none = u16(-1);

	// The world tier's registry key. One below the invalid-id sentinel, so it sits at the far
	// end of the id space from anything PerformIDgen hands out in practice. Not an entity —
	// nothing may look it up in the object registry.
	static const u16 world_key = u16(-2);

	u16 world_actor();                  // world_key, or none where there is no A-Life (a client)
	u16 acting_actor();                 // none outside a player-initiated action

	// Scoped setter. Every future call site (trade, use, hit) must set the
	// context through this: a LEAKED context is the failure that would
	// silently mis-attribute every world read taken after it to whichever
	// player last talked to someone — a bug that produces plausible answers
	// forever and no error. Restores the previous value, so nesting is safe.
	struct acting_scope
	{
		explicit acting_scope(u16 actor_id);
		~acting_scope();

	private:
		u16 m_prev;

		acting_scope(const acting_scope&);
		acting_scope& operator=(const acting_scope&);
	};
}
