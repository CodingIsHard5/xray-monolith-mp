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
//  world_actor()  = ai().alife().graph().actor() — the level's OWN actor
//                   entity. It already exists in every save with a stable id,
//                   it is the clone TEMPLATE for player actors
//                   (game_sv_Single::coop_spawn_actor_for) so it is never
//                   owned by a person, and it is already the de-facto subject
//                   of autonomous world simulation. Doc §6.4 forbids spawning
//                   a fake entity to hold collective state; this reuses one
//                   A-Life requires anyway.
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

	u16 world_actor();                  // none if there is no A-Life / no base actor
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
