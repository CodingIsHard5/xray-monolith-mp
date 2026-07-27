#include "stdafx.h"
#include "game_sv_single.h"
#include "xrserver_objects_alife_monsters.h"
#include "xrServer_Objects_ALife_Items.h"          // MP fork (§20): CSE_ALifeItemWeapon (kit mag pre-load)
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "alife_graph_registry.h"
#include "game_graph.h"                            // MP fork (§14 step 7 phase 2): level_id check before add_online
#include "alife_time_manager.h"
#include "object_broker.h"
#include "gamepersistent.h"
#include "xrServer.h"
#include "xrMessages.h"                            // MP fork (§9.3): M_SPAWN_OBJECT_*, GE_DESTROY
#include "Level.h"                                 // MP fork (§9.3): Level().timeServer()
#include "GameTaskManager.h"                        // MP fork (§19): coop_broadcast_tasks on connect
#include "GameTask.h"                               // MP fork (test): -coop_test_quest synthetic task
#include "ai_space.h"                              // MP fork: ai().alife()
#include "script_engine.h"                         // MP fork: server-side Lua init hook
#include "../xrNetServer/xr_enet_transport.h"      // MP fork: xr_enet::enabled()
#include "mp_anchors.h"                            // MP fork: A-Life attention anchors
#include "../xrEngine/x_ray.h"
#include "../xrEngine/dedicated_server_only.h"
#include "../xrEngine/no_single.h"

// MP fork (§14 step 7, co-op save/load): the ONE co-op world slot. Everything that names the
// server's own save — the autosave, its ownership sidecar, and the D2 dirty flag — goes through
// this constant so the three can never drift apart. Space-free so it survives inside
// server(<name>/single/alife/load) on reload (see dedicated.sh).
static const char* const COOP_SAVE_SLOT = "coop_world";

// MP fork (§14 step 7 phase 4 D2): the only clean stop this server has. A dedicated co-op
// server is started detached under wine and stopped with `wineserver -k`, which terminates the
// process — the engine's own exit path never runs, so "a clean shutdown clears the flag" would
// have been dead code in every existing harness and every real stop. So the operator asks for a
// stop by dropping this file next to the saves, and the server performs the stop itself.
static const char* const COOP_STOP_FILE = "coop_stop";

// The FS file registry is built by scanning at startup and updated by w_open/file_delete, so it
// cannot see a file another process created while we were running (the stop file, by definition).
// Ask the OS directly. (The dirty flag WOULD be in the registry at boot, but it is read through
// the same helper so both answers come from the same place.)
static bool coop_file_on_disk(LPCSTR full_path)
{
	return GetFileAttributesA(full_path) != INVALID_FILE_ATTRIBUTES;
}

// MP fork (§14 step 7 phase 4 D2): is this a position a player could actually be standing at?
// `_valid()` only rejects NaN/inf, and the corruption this codebase keeps meeting is not NaN — it
// is uninitialized memory that happens to be finite. Measured values from the two occurrences:
// `-421888.0, 1.1e17, 142.2` (P2 §3b) and `114688.0, -4.4e17, -3128.0` (D2). An X-Ray level fits
// inside a few thousand metres of the origin, so a magnitude gate separates the two cases cleanly,
// and a record that fails it must be DROPPED rather than persisted: keeping the last good value is
// always better than writing a position nobody can be put back at.
static bool coop_plausible_pos(const Fvector& p)
{
	const float LIMIT = 32768.f;   // ~16x the largest real level extent
	return !!_valid(p) && _abs(p.x) < LIMIT && _abs(p.y) < LIMIT && _abs(p.z) < LIMIT;
}

game_sv_Single::game_sv_Single()
{
	m_alife_simulator = NULL;
	m_type = eGameIDSingle;
	m_coop_autosave_interval_ms = 0;
	m_coop_autosave_last = 0;
	m_coop_autosave_init = false;
	m_coop_test_worlditem_id = 0xffff;
	m_coop_test_worlditem_pos.set(0.f, 0.f, 0.f);
	m_coop_test_checkpoint_ms = 0;
	m_coop_test_checkpoint_armed = 0;
	m_coop_test_checkpoint_retry = 0;
	m_coop_test_checkpoint_init = false;
	m_coop_test_checkpoint_done = false;
	m_coop_prev_crash = false;
	m_coop_dirty_checked = false;
	m_coop_dirty_streak = 0;
	m_coop_stop_poll_last = 0;
};

game_sv_Single::~game_sv_Single()
{
	delete_data(m_alife_simulator);
}

void game_sv_Single::Create(shared_str& options)
{
	inherited::Create(options);

#ifndef NO_SINGLE
	if (strstr(*options, "/alife"))
		m_alife_simulator = xr_new<CALifeSimulator>(&server(), &options);
#endif //#ifndef NO_SINGLE

	// MP fork (§14 co-op): the co-op server runs game_sv_single, so GAMMA's full
	// single-player + client script env executes here (axr_main.on_game_start, per-mod
	// on_game_load callbacks). Some GAMMA scripts reference globals that another mod's
	// client-only script defines (e.g. nextTick from Spatial Audio's kute_common, which
	// never loads on a -nosound headless server), and crash with "attempt to call global
	// (nil)". Give the mod ONE server-init hook, symmetric to mp_client.on_client_level_start,
	// where its overlay defines the server-safe globals/stubs it needs. Runs after the alife
	// simulator (script engine ready) and before any on_game_load. Optional: absent -> no-op.
	// Gated to ENet co-op so plain SP/MP are untouched.
	if (xr_enet::enabled())
	{
		::luabind::functor<void> server_init;
		if (ai().script_engine().functor("mp_client.on_server_game_start", server_init))
			server_init();
	}

	// MP fork (§14 step 7 phase 2): when this server booted FROM a save, the persisted
	// actors are now in the world but ownerless. Re-seed the player_name -> entity_id
	// bindings from the save's sidecar so returning players reclaim their own bodies.
	// (m_game_params still holds the parsed server options; the A-Life ctor above rewrote
	// only the returned option string.) Co-op (ENet) only — stock SP/MP untouched.
	if (xr_enet::enabled() && m_alife_simulator)
	{
		// MP fork (§14 step 7 phase 4 D2): read how the LAST process ended BEFORE anything
		// else is restored, so the log reads "the previous process died" and then what this
		// one recovered, and claim the flag for this process.
		coop_mark_dirty();

		const IGame_Persistent::params& gp = g_pGamePersistent->m_game_params;
		if (!xr_strcmp(gp.m_new_or_load, "load"))
			coop_load_bindings(gp.m_game_or_spawn);
	}

	switch_Phase(GAME_PHASE_INPROGRESS);
}

/**
CSE_Abstract*		game_sv_Single::get_entity_from_eid		(u16 id)
{
	if (!ai().get_alife())
		return			(inherited::get_entity_from_eid(id));

	CSE_Abstract		*object = ai().alife().objects().object(id,true);
	if (!object)
		return			(inherited::get_entity_from_eid(id));

	return				(object);
}
/**/

void game_sv_Single::OnCreate(u16 id_who)
{
	if (!ai().get_alife())
		return;

	CSE_Abstract* e_who = get_entity_from_eid(id_who);
	VERIFY(e_who);
	if (!e_who->m_bALifeControl)
		return;

	CSE_ALifeObject* alife_object = smart_cast<CSE_ALifeObject*>(e_who);
	if (!alife_object)
		return;

	alife_object->m_bOnline = true;

	if (alife_object->ID_Parent != 0xffff)
	{
		CSE_ALifeDynamicObject* parent = ai().alife().objects().object(alife_object->ID_Parent, true);
		if (parent)
		{
			CSE_ALifeTraderAbstract* trader = smart_cast<CSE_ALifeTraderAbstract*>(parent);
			if (trader)
				alife().create(alife_object);
			else
			{
				CSE_ALifeInventoryBox* const box = smart_cast<CSE_ALifeInventoryBox*>(parent);
				if (box)
					alife().create(alife_object);
				else
					alife_object->m_bALifeControl = false;
			}
		}
		else
			alife_object->m_bALifeControl = false;
	}
	else
		alife().create(alife_object);
}

// MP fork (§14 co-op): give each connecting co-op client its OWN actor. The single
// game type has only the save's one actor (id 0), which every client would bind and
// collide on -> players can't see each other. Spawn a fresh actor per client at the
// save actor's position (SP levels have no MP respawn points, so we can't assign_RP),
// owned by that client. spawn_end() -> Process_spawn() replicates it to all clients
// with per-recipient ownership flags: the owner receives it LOCAL+ASPLAYER (controls
// it), peers receive it stripped (render it as a remote player). Player position sync
// then rides the inherited M_CL_UPDATE path + our FIX-A relay.
void game_sv_Single::OnPlayerConnectFinished(ClientID id_who)
{
	inherited::OnPlayerConnectFinished(id_who);
	coop_poll_spawns(); // in case M_CLIENTREADY did fire; the poll also runs each Update
}

// Spawn a co-op actor for one ready, actorless client at the save actor's position.
// MP fork (§17 co-op): clone every inventory item of the save actor `base` onto the
// freshly-spawned player actor `owner` (owned by client CL). Items are separate CSE
// entities (base->children); a bare actor clone has none, so peers see empty hands and
// no outfit. We re-spawn each child parented to `owner` — Process_spawn attaches it via
// OnTouch, replicates it (LOCAL to the owning client, stripped/remote to peers), and the
// item's third-person visual then rides along on the peer body.
// MP fork (§17 co-op): give a freshly-spawned player actor a basic starting kit. The
// co-op test save (n1-autosave) ships an almost-empty actor (only the PDA), so cloning
// its inventory leaves players with nothing to use. Spawn a small set of standard items
// straight into the actor's inventory (parented => Process_spawn attaches + replicates,
// LOCAL to the owner). Invalid sections are skipped (logged), so the list is safe to
// tune. A save that already has gear still gets it via coop_clone_inventory_for.
void game_sv_Single::coop_give_starting_kit(CSE_Abstract* owner, xrClientData* CL)
{
	if (!owner)
		return;

	static const char* kit[] = {
		"wpn_knife", "wpn_pm", "ammo_9x18_fmj", "ammo_9x18_fmj",
		"medkit", "bandage", "bread", "device_torch"
	};

	CSE_ALifeDynamicObject* od = smart_cast<CSE_ALifeDynamicObject*>(owner);
	int given = 0;
	for (const char* sec : kit)
	{
		CSE_Abstract* it = F_entity_Create(sec);
		if (!it)
		{
			Msg("! XRNET(dbg): starting_kit: section '%s' invalid (skipped)", sec);
			continue;
		}
		it->s_name = sec;
		it->set_name_replace("");
		it->s_RP = 0xFE;
		it->ID = 0xffff;
		it->ID_Phantom = 0xffff;
		it->ID_Parent = owner->ID;
		it->RespawnTime = 0;
		it->o_Position = owner->o_Position;
		it->s_flags.assign(M_SPAWN_OBJECT_LOCAL);
		if (CSE_ALifeDynamicObject* dyn = smart_cast<CSE_ALifeDynamicObject*>(it))
			if (od) { dyn->m_tNodeID = od->m_tNodeID; dyn->m_tGraphID = od->m_tGraphID; }
		if (CSE_ALifeObject* al = smart_cast<CSE_ALifeObject*>(it))
		{
			al->m_story_id = INVALID_STORY_ID;
			al->m_spawn_story_id = INVALID_SPAWN_STORY_ID;
		}
		// MP fork (§20): pre-load kit weapons so they're immediately usable on the thin
		// client. Reloading pulls ammo from inventory via server-authoritative transfer
		// the client can't drive, so a freshly spawned weapon would sit at 0/mag. Set the
		// magazine full of ammo_type 0 (the weapon's first/default ammo section) on spawn;
		// the client clamps a_elapsed to the real mag size in net_Import.
		if (CSE_ALifeItemWeapon* wpn = smart_cast<CSE_ALifeItemWeapon*>(it))
		{
			if (pSettings->line_exist(sec, "ammo_mag_size"))
			{
				int mag = pSettings->r_s32(sec, "ammo_mag_size");
				if (mag > 0)
				{
					wpn->a_elapsed = (u16)mag;
					wpn->ammo_type = 0;
				}
			}
		}
		if (spawn_end(it, CL->ID))
			++given;
	}
	Msg("- XRNET(dbg): starting_kit: gave %d item(s) to actor id %u for client 0x%08x",
		given, owner->ID, CL->ID.value());
}

void game_sv_Single::coop_clone_inventory_for(CSE_ALifeCreatureActor* base, CSE_Abstract* owner, xrClientData* CL)
{
	if (!base || !owner)
		return;

	// snapshot the id list first: spawning mutates registries/child vectors.
	xr_vector<u16> item_ids = base->children;
	int cloned = 0;
	for (u16 child_id : item_ids)
	{
		CSE_ALifeDynamicObject* src = ai().alife().objects().object(child_id, true);
		if (!src)
			continue;
		// only carry actual inventory items (skip anything odd parented under the actor)
		if (!src->cast_inventory_item())
			continue;

		CSE_Abstract* copy = F_entity_Create(src->s_name.c_str());
		if (!copy)
		{
			Msg("! XRNET(dbg): coop_clone_inventory: F_entity_Create('%s') failed", src->s_name.c_str());
			continue;
		}
		{
			NET_Packet pk;
			src->Spawn_Write(pk, TRUE);
			copy->Spawn_Read(pk);
		}
		// fresh identity, parented to the new player actor
		copy->ID = 0xffff;
		copy->ID_Phantom = 0xffff;
		copy->ID_Parent = owner->ID;
		copy->s_RP = 0xFE;
		copy->RespawnTime = 0;
		copy->o_Position = owner->o_Position;
		// LOCAL to the owning client (so they can fire/reload); peers get it stripped.
		copy->s_flags.assign(M_SPAWN_OBJECT_LOCAL);

		// Clear the story ids: cloning a UNIQUE story item (e.g. the PDA, whose section
		// prints as device_pdaNNNN where NNNN is its story id) otherwise makes duplicate
		// story objects. The client's story registry maps a story id to ONE object, so
		// the second copy collides ("Object with ID already exists ... device_pda",
		// "Failed to spawn entity 'device_pda'") -> that player's PDA never spawns and
		// the PDA is broken. Each player gets a plain, functional (non-story) copy.
		if (CSE_ALifeObject* copy_alife = smart_cast<CSE_ALifeObject*>(copy))
		{
			copy_alife->m_story_id = INVALID_STORY_ID;
			copy_alife->m_spawn_story_id = INVALID_SPAWN_STORY_ID;
		}

		CSE_Abstract* NI = spawn_end(copy, CL->ID);
		if (NI)
			++cloned;
	}
	Msg("- XRNET(dbg): coop_clone_inventory: cloned %d item(s) onto actor id %u for client 0x%08x",
		cloned, owner->ID, CL->ID.value());
}

void game_sv_Single::coop_spawn_actor_for(xrClientData* CL)
{
	CSE_ALifeCreatureActor* base = ai().alife().graph().actor();
	if (!base)
	{
		Msg("! XRNET(dbg): coop_spawn_actor_for: no base actor to clone spawn from");
		return;
	}

	// Clone the save actor's CSE (via the spawn wire-format, same as set_client_actor)
	// so the new actor inherits a real VISUAL/character — a bare spawn_begin("actor")
	// has no model and would be invisible to the other player (and trips scripts that
	// assume a full actor). Inventory items are separate child entities, so they are
	// NOT cloned; the actor is visible but without gear.
	CSE_Abstract* E = F_entity_Create(base->s_name.c_str());
	if (!E)
	{
		Msg("! XRNET(dbg): coop_spawn_actor_for: F_entity_Create('%s') failed", base->s_name.c_str());
		return;
	}
	{
		NET_Packet clone_packet;
		base->Spawn_Write(clone_packet, TRUE);
		E->Spawn_Read(clone_packet);
	}
	// reset identity so spawn_end/Process_spawn assigns a fresh server ID (not id 0)
	E->ID = 0xffff;
	E->ID_Parent = 0xffff;
	E->ID_Phantom = 0xffff;
	E->s_RP = 0xFE;
	E->RespawnTime = 0;

	// position slightly offset from the base actor so co-op players don't overlap
	static int s_coop_actor_seq = 0;
	++s_coop_actor_seq;
	E->o_Position = base->o_Position;
	E->o_Position.x += 1.5f * float(s_coop_actor_seq);
	E->o_Angle = base->o_Angle;

	CSE_ALifeCreatureActor* na = smart_cast<CSE_ALifeCreatureActor*>(E);
	if (na)
	{
		na->m_tNodeID = base->m_tNodeID;
		na->m_tGraphID = base->m_tGraphID;
		na->m_bALifeControl = false; // client-driven, not A-Life
	}
	// LOCAL+ASPLAYER: Process_spawn keeps these for the owner, strips for peers
	E->s_flags.assign(M_SPAWN_OBJECT_LOCAL | M_SPAWN_OBJECT_ASPLAYER);

	CSE_Abstract* N = spawn_end(E, CL->ID); // sets CL->owner = N
	Msg("- XRNET(dbg): co-op actor spawned for client 0x%08x -> entity id %u (seq %d)",
		CL->ID.value(), N ? N->ID : u16(-1), s_coop_actor_seq);

	// MP fork (§17 co-op): clone the save actor's inventory (weapons/outfit/ammo) onto
	// this player's actor, so players spawn with the starting gear AND peers can SEE the
	// held weapon / worn outfit (the third-person visual is driven by the child item
	// entities, which the bare actor clone lacks). Each item is re-spawned parented to N.
	if (N)
	{
		coop_clone_inventory_for(base, N, CL);
		coop_give_starting_kit(N, CL); // bare test save -> give players usable gear
	}

	// MP fork (§13 co-op late-join snapshot): Process_spawn only BROADCASTS this new
	// actor to clients that are ALREADY connected — it is a one-shot event with no
	// replay for late joiners. Clients start staggered, so client 2 connects after
	// client 1's actor already spawned and never learns of it (client 1 sees client 2,
	// but not the reverse). Fix: right after giving CL its actor, replay every OTHER
	// client's existing actor to CL as a stripped (=> remote) spawn, so the new client
	// gets a snapshot of the peers that spawned before it joined.
	struct peer_replay
	{
		game_sv_Single* self;
		xrClientData* target;
		void operator()(IClient* client)
		{
			xrClientData* other = static_cast<xrClientData*>(client);
			if (other == target) return;         // don't replay the actor to its own owner
			if (other == self->m_server->GetServerClient()) return; // skip the server's
			                                      // own save-actor (id 0) — not a player
			if (!other->owner) return;            // client without an actor yet
			CSE_Abstract* peer = other->owner;
			NET_Packet Packet;
			peer->Spawn_Write(Packet, FALSE);     // FALSE strips LOCAL/ASPLAYER => remote peer
			self->m_server->SendTo(target->ID, Packet, net_flags(TRUE, TRUE));
			Msg("- XRNET(dbg): late-join replay: peer actor id %u -> client 0x%08x",
				peer->ID, target->ID.value());
		}
	};
	peer_replay pr; pr.self = this; pr.target = CL;
	m_server->ForEachClientDo(pr);
}

// MP fork (§14 co-op): the single game type has only the save's one actor, so every
// client would collide on it and players couldn't see each other. Give each connected
// client its OWN actor. M_CLIENTREADY is unreliable for co-op clients, so poll here:
// any client that is net_Ready (in the world, sending updates) but does not yet own an
// entity (CL->owner == NULL) gets a fresh actor. spawn_end/Process_spawn replicates it
// with per-recipient ownership (owner LOCAL+ASPLAYER, peers stripped -> remote render).
// MP fork (§9.3 / §14 step 7 phase 2): THE co-op player identity string — the single
// source of truth for every name-keyed co-op path (orphan store, reconnect lookup,
// persisted ownership bindings). A co-op thin client HAS a game_PlayerState, but its
// ACCOUNT name is empty: the thin client never sends profile data, and the name the
// player actually connected with ("client(localhost/name=<x>)") arrives in the ENet
// hello and lands in IClient::name. Reading ps->getName() alone therefore yields ""
// for every player — which silently made reconnection key every orphan on the empty
// string (any reconnecting client would match the FIRST orphan, i.e. could take
// another player's body) and made phase 2 record zero bindings. Prefer the first
// NON-EMPTY of (player-state account name, client connect name); NULL if neither.
static LPCSTR coop_player_name(xrClientData* CL)
{
	if (!CL)
		return NULL;
	if (CL->ps)
	{
		LPCSTR n = CL->ps->getName();
		if (n && xr_strlen(n))
			return n;
	}
	return CL->name.size() ? CL->name.c_str() : NULL;
}

// MP fork (§9.3/9.4 co-op reconnection): orphan the actor when a co-op client disconnects.
// The entity stays in the world (alive, at its last position) but has no owning client.
// If the same player name reconnects within the timeout, the actor is re-associated.
void game_sv_Single::coop_orphan_actor(xrClientData* CL)
{
	if (!CL || !CL->owner)
		return;

	CSE_Abstract* actor = CL->owner;
	LPCSTR nm = coop_player_name(CL);
	coop_orphan orphan;
	orphan.entity_id = actor->ID;
	orphan.player_name = nm ? nm : "";   // unnamed => body is preserved but unmatchable
	orphan.disconnect_time = Device.dwTimeGlobal;
	orphan.persistent = false;   // live disconnect: expires on the reconnect timeout
	// MP fork (§14 step 7 phase 4, increment E / §9.3): record the logged-off position HERE,
	// at the instant ownership is detached and while the CSE is still known-good. Within one
	// process the frozen body's CSE would answer the same question, but this value is what
	// rides into the v3 sidecar, and a reclaim after a RESTART must not have to read a position
	// back off a reloaded entity (P2 §3b rule 2). Non-finite => record nothing rather than a
	// trap the player would resume into.
	orphan.have_saved_pos = coop_plausible_pos(actor->o_Position);
	if (orphan.have_saved_pos)
		orphan.saved_pos = actor->o_Position;
	else
		orphan.saved_pos.set(0.f, 0.f, 0.f);
	// D2 run 2: the health travels with the position for the same reason — a live disconnect
	// keeps the body's CSE intact, but a RESTART in between does not, and the reclaim ships
	// whatever the CSE says to the client.
	orphan.have_saved_health = false;
	orphan.saved_health = 1.f;
	if (CSE_ALifeCreatureAbstract* creature = smart_cast<CSE_ALifeCreatureAbstract*>(actor))
	{
		orphan.saved_health = creature->get_health();
		orphan.have_saved_health = orphan.saved_health > 0.f;
	}
	orphan.frozen = true;          // ownership is detached below — nothing will overwrite it

	// Mark the entity as orphaned so Perform_connect_spawn won't claim it for other
	// connecting clients. Detach ownership so the update loop skips it (frozen body).
	actor->m_coop_orphaned = true;
	actor->owner = NULL;
	CL->owner = NULL;

	// Also orphan child entities (inventory items) — they share the actor's fate
	for (u16 child_id : actor->children)
	{
		CSE_Abstract* child = m_server->ID_to_entity(child_id);
		if (child)
		{
			child->m_coop_orphaned = true;
			child->owner = NULL;
		}
	}

	m_coop_orphans.push_back(orphan);
	Msg("- XRNET(dbg): co-op actor id %u orphaned for player '%s' (reconnect window %ds)",
		orphan.entity_id, orphan.player_name.c_str(), RECONNECT_TIMEOUT_MS / 1000);

	// MP fork (§14 step 7 phase 4 D2): this player is no longer in the world, so they no longer
	// have a recovery position — the binding record written just above is what describes them.
	// Done AFTER the orphan exists, so there is never an instant where neither record does.
	if (nm)
		coop_drop_recovery(nm);
}

void game_sv_Single::OnCoopClientDisconnected(xrClientData* CL)
{
	if (!xr_enet::enabled())
		return;
	coop_orphan_actor(CL);
	// Clean up the grace-period entry so a reconnecting client gets a fresh grace window
	m_coop_seen.erase(CL->ID.value());
}

// MP fork (§14 step 7 phase 4 D2, reclaim delivery): does this entity belong to the orphaned body
// reserved for CL's player? Walks the parent chain, so the actor's inventory children answer yes
// too. See the declaration for why the connection snapshot has to know.
bool game_sv_Single::coop_is_own_orphan(CSE_Abstract* E, xrClientData* CL)
{
	if (!xr_enet::enabled() || !E || !CL || !m_server || m_coop_orphans.empty())
		return false;
	LPCSTR nm = coop_player_name(CL);
	if (!nm || !xr_strlen(nm))
		return false;   // an unnamed client owns nothing (P2 §3b)

	// Bounded walk: a malformed parent chain must not spin here, and inventory nests shallowly
	// (item -> container -> actor is the deepest real case).
	CSE_Abstract* cur = E;
	for (int depth = 0; cur && depth < 8; ++depth)
	{
		for (const coop_orphan& o : m_coop_orphans)
		{
			if (o.entity_id != cur->ID || !o.player_name.size())
				continue;
			return !xr_strcmp(o.player_name.c_str(), nm);
		}
		cur = (cur->ID_Parent == 0xffff) ? NULL : m_server->ID_to_entity(cur->ID_Parent);
	}
	return false;
}

CSE_Abstract* game_sv_Single::coop_find_orphan(LPCSTR name, Fvector* out_pos, bool* out_have_pos,
                                               float* out_health)
{
	if (out_have_pos)
		*out_have_pos = false;
	if (out_health)
		*out_health = -1.f;   // "no recorded health"; the caller falls through to its next source

	// An unnamed client must never match an (equally unnamed) orphan — that would hand
	// it whichever body happens to sit first in the list, quite possibly someone else's.
	if (!name || !xr_strlen(name))
		return NULL;

	for (auto it = m_coop_orphans.begin(); it != m_coop_orphans.end(); ++it)
	{
		if (!it->player_name.size())
			continue;
		if (!xr_strcmp(it->player_name.c_str(), name))
		{
			u16 eid = it->entity_id;
			const bool persisted = it->persistent;
			if (out_pos && out_have_pos && it->have_saved_pos)
			{
				*out_pos = it->saved_pos;
				*out_have_pos = true;
			}
			if (out_health && it->have_saved_health)
				*out_health = it->saved_health;
			m_coop_orphans.erase(it);
			CSE_Abstract* entity = m_server->ID_to_entity(eid);
			if (entity)
			{
				Msg("- XRNET(dbg): co-op orphan matched for player '%s' -> entity id %u%s", name, eid,
					persisted ? " (restored from save)" : "");
				return entity;
			}

			// MP fork (§14 step 7 phase 2): a binding restored from a save points at an
			// A-Life entity that may still be OFFLINE — offline objects live only in the
			// A-Life object registry, not in the server's online entity map. Bring it
			// online so the re-association path (Spawn_Write to the client) has a real
			// server entity to send. add_online() asserts the object is on the current
			// level, so refuse anything that graphs elsewhere and fall back to a fresh
			// spawn rather than trip the assert.
			CSE_ALifeDynamicObject* dyn = ai().get_alife()
				? ai().alife().objects().object(eid, true)
				: NULL;
			if (dyn && !dyn->m_bOnline)
			{
				const bool same_level = ai().game_graph().valid_vertex_id(dyn->m_tGraphID) &&
					ai().game_graph().vertex(dyn->m_tGraphID)->level_id() ==
					ai().alife().graph().level().level_id();
				if (same_level)
				{
					alife().add_online(dyn);
					entity = m_server->ID_to_entity(eid);
					if (entity)
					{
						Msg("- COOP(bindings): player '%s' -> persisted actor id %u switched ONLINE for reclaim",
							name, eid);
						return entity;
					}
				}
				Msg("! COOP(bindings): persisted actor id %u for player '%s' is offline and not "
					"reclaimable here (same_level=%s) — falling back to a fresh spawn",
					eid, name, same_level ? "yes" : "no");
				return NULL;
			}

			Msg("! XRNET(dbg): co-op orphan entity id %u no longer exists for player '%s'", eid, name);
			return NULL;
		}
	}
	return NULL;
}

// MP fork (§14 step 7 phase 2): a body restored from a save switches online owned by the
// server's loopback client — it has to, or Process_event's owner assert fires while its
// inventory children attach. But an owned entity is one the server keeps in sync from its
// own (never actually placed) object state, which overwrites the CSE position: run 3
// measured the saved -235.6,27.9,253.9 becoming -0.1,0.2,0.7 before the owner logged in.
// So as soon as a restored body IS online, detach ownership — the exact state a live
// disconnect leaves behind (coop_orphan_actor), i.e. a frozen body nothing updates.
void game_sv_Single::coop_freeze_restored_bodies()
{
	for (coop_orphan& o : m_coop_orphans)
	{
		if (!o.persistent || o.frozen)
			continue;

		CSE_Abstract* entity = m_server->ID_to_entity(o.entity_id);
		if (!entity)
			continue;               // still offline; nothing owns/updates it yet

		// MP fork (§14 step 7 phase 4 D2, run 2): THIS is the last instant the restored body's
		// CSE is known-good — leg 2 measured it holding the exact saved position here and
		// -0.1,0.2,0.7 / hp 0.00 two minutes later, before its owner had even connected. The
		// position was already covered (the sidecar carries it); the HEALTH was not, and the
		// reclaim ships the CSE's health to the client, so the returning player was handed a
		// corpse: dead on the client => never net_Relevant => never sends an M_CL_UPDATE =>
		// the server's CSE stays garbage and the sampler persists it. Snapshot it here.
		if (CSE_ALifeCreatureAbstract* creature = smart_cast<CSE_ALifeCreatureAbstract*>(entity))
		{
			const float hp = creature->get_health();
			if (hp > 0.f)
			{
				o.saved_health = hp;
				o.have_saved_health = true;
			}
		}

		if (o.have_saved_pos)
			Msg("- COOP(bindings): freezing restored body id %u for '%s' — CSE pos %.1f,%.1f,%.1f "
				"(saved %.1f,%.1f,%.1f) hp %.2f%s, detaching server ownership",
				o.entity_id, o.player_name.c_str(),
				entity->o_Position.x, entity->o_Position.y, entity->o_Position.z,
				o.saved_pos.x, o.saved_pos.y, o.saved_pos.z,
				o.saved_health, o.have_saved_health ? " (snapshotted)" : " (NOT usable)");

		// Clear BOTH directions of the ownership link. Process_spawn refuses to point a
		// client's owner at an orphan, so the reverse pointer should never name this body —
		// but a dangling CL->owner would be a client "owning" an unowned entity, which is
		// exactly the confusion this phase exists to remove. (CodeRabbit)
		xrClientData* previous_owner = entity->owner;
		entity->owner = NULL;
		if (previous_owner && previous_owner->owner == entity)
			previous_owner->owner = NULL;
		for (u16 child_id : entity->children)
		{
			CSE_Abstract* child = m_server->ID_to_entity(child_id);
			if (child) child->owner = NULL;
		}
		o.frozen = true;
	}
}

void game_sv_Single::coop_cleanup_orphans()
{
	if (m_coop_orphans.empty())
		return;

	const u32 now = Device.dwTimeGlobal;
	for (auto it = m_coop_orphans.begin(); it != m_coop_orphans.end(); )
	{
		// MP fork (§14 step 7 phase 2 / §9.3): bindings restored from a save never expire —
		// a player must be able to reclaim their body however long the server was down (and
		// however long it has been up before they log back in). Only LIVE disconnects age out.
		if (it->persistent)
		{
			++it;
			continue;
		}
		if (now - it->disconnect_time > RECONNECT_TIMEOUT_MS)
		{
			u16 eid = it->entity_id;
			Msg("- XRNET(dbg): co-op orphan expired for player '%s' (entity id %u) — destroying",
				it->player_name.c_str(), eid);

			CSE_Abstract* entity = m_server->ID_to_entity(eid);
			if (entity)
			{
				// Clear orphan flags before destroying
				entity->m_coop_orphaned = false;
				for (u16 child_id : entity->children)
				{
					CSE_Abstract* child = m_server->ID_to_entity(child_id);
					if (child) child->m_coop_orphaned = false;
				}

				// Perform_destroy handles children, entity removal, and broadcast
				m_server->Perform_destroy(entity, net_flags(TRUE, TRUE));
			}
			it = m_coop_orphans.erase(it);
		}
		else
			++it;
	}
}

// MP fork (§9.4/9.5 co-op save/load — step 7 phase 1): server-authoritative atomic
// world save. The dedicated server owns the one true world; it snapshots to disk on
// its own timer — no client, no console `save`, no live/alive actor required. This
// drives the SAME atomic ALife save the console path uses (prepare_objects_for_save =
// flush live online state -> CSE, then compress+write the .scop), but invoked directly
// server-side. update_name=false leaves the "current save name" untouched.
void game_sv_Single::coop_autosave()
{
	if (!ai().get_alife())
		return;

	// Save name (Appendix B): the fixed co-op world slot, shared with the sidecar and the
	// D2 dirty flag (see COOP_SAVE_SLOT).
	LPCSTR save_name = COOP_SAVE_SLOT;

	// --- diag (feeds phase 2, gap B): how many co-op player actors exist, and how many
	// are A-Life-registered (=> actually written into the .scop)? Co-op actors spawn with
	// m_bALifeControl=false, so this is an open question the save must answer empirically.
	u32 coop_actors = 0, alife_reg = 0;
	{
		struct counter
		{
			game_sv_Single* self;
			u32* n; u32* reg;
			void operator()(IClient* client)
			{
				xrClientData* cd = static_cast<xrClientData*>(client);
				if (cd == self->m_server->GetServerClient()) return; // skip host save-actor (id 0)
				if (!cd->owner) return;
				++(*n);
				if (ai().alife().objects().object(cd->owner->ID, true)) ++(*reg);
			}
		};
		counter c; c.self = this; c.n = &coop_actors; c.reg = &alife_reg;
		m_server->ForEachClientDo(c);
		// orphaned (disconnected-but-alive) co-op actors count too
		for (const coop_orphan& o : m_coop_orphans)
		{
			++coop_actors;
			if (ai().alife().objects().object(o.entity_id, true)) ++alife_reg;
		}
	}
	Msg("- COOP(autosave-diag): coop_actors=%u alife_reg=%u", coop_actors, alife_reg);

	// MP fork (§14 step 7 phase 4 D1, diagnostic): the player CSE positions as they stand
	// BEFORE the world write. Paired with the COOP(recovery) line logged after it, this
	// measures how far a player's CSE moves across one save — see coop_sample_recoveries()
	// for why that window is not zero.
	{
		struct prober
		{
			game_sv_Single* self;
			void operator()(IClient* client)
			{
				xrClientData* cd = static_cast<xrClientData*>(client);
				if (cd == self->m_server->GetServerClient()) return;
				if (!cd->owner) return;
				LPCSTR nm = coop_player_name(cd);
				Msg("- COOP(autosave-diag): '%s' CSE pos %.2f,%.2f,%.2f before the world write",
					nm ? nm : "<unnamed>", cd->owner->o_Position.x,
					cd->owner->o_Position.y, cd->owner->o_Position.z);
			}
		};
		prober pr; pr.self = this;
		m_server->ForEachClientDo(pr);
	}

	// Save the CSE/ALife world directly via the public save(name, update_name=false) form —
	// deliberately NOT the NET_Packet form. The NET_Packet form runs prepare_objects_for_save()
	// = Level().ClientSend()+ClientSave(); ClientSave() flushes every ONLINE object through
	// Objects_net_Save -> CScriptBinder::save, which fires each object's Lua save-callback. On
	// the flat co-op gamedata those callbacks hit undefined globals (e.g. hf_obj_manager ->
	// game_objects_iter, an S3-class gamedata gap); the co-op "continue on Lua error" handler
	// swallows the error mid-callback and corrupts the LuaJIT VM -> AV in lj_vm_return (crash
	// symbolicated, build 30184076690). The direct form writes the authoritative CSE registry
	// (header/time/spawns/objects/registry — entity positions, inventory, A-Life state) with NO
	// per-object Lua save, so it is crash-safe and format-identical to a normal .scop (loads via
	// dedicated.sh). Trade-off: online objects' live runtime state is not re-flushed to CSE at
	// the save instant (as-of-last-sync); a Lua-free position flush is a later refinement.
	alife().save(save_name, false);
	// MP fork (§14 step 7 phase 4 D1): sample recovery positions AFTER the world write and
	// immediately before the sidecar, so the recovery record, the binding record and the
	// `.scop` all describe the same read of the same field. Sampling before the write was
	// measured to disagree with the file by the full length of a walk (see the function).
	coop_sample_recoveries();
	// MP fork (§14 step 7 phase 2): the .scop holds the actor ENTITIES; the sidecar holds
	// who owns them. Written after the world so a sidecar never describes a save that
	// doesn't exist.
	coop_save_bindings(save_name);
	Msg("- COOP(autosave): saved '%s'", save_name);
}

// MP fork (§14 step 7 phase 4 D1, gap D): find a player's recovery record (NULL if none).
game_sv_Single::coop_recovery* game_sv_Single::coop_find_recovery(LPCSTR player_name)
{
	if (!player_name || !xr_strlen(player_name))
		return NULL;
	for (coop_recovery& r : m_coop_recoveries)
		if (!xr_strcmp(r.player_name.c_str(), player_name))
			return &r;
	return NULL;
}

// MP fork (§14 step 7 phase 4 D1, gap D / doc §9.4): snapshot where every connected player
// ACTUALLY is, so a `kill -9` costs them at most one autosave interval instead of rewinding
// them to a checkpoint they banked an hour ago (a crash is not a death — §9.4). D0 settled
// where the number comes from: a client-owned actor's M_CL_UPDATE stream keeps the server CSE
// current, measured exact on all three axes after a 15 m walk, so the CSE read here IS the
// authoritative position.
//
// WHEN this runs is load-bearing, and the first D1 run got it wrong. That stream is applied by
// the ENet PUMP THREAD, not the game thread: `xrServer::OnMessage` peeks the position out of
// each M_CL_UPDATE and assigns `CL->owner->o_Position` (xrServer.cpp, §15 co-op). So the field
// can change under the game thread at any instant, and in particular across the ~1 s the world
// write takes. Sampling BEFORE `alife().save()` measured -235.6 (the spawn point) while the
// same field read after it — the value the `.scop` and the binding record both carry — was
// -220.6, the walked-to point: a 15 m disagreement inside one save. Sampling last makes all
// three agree by construction; a race can then only cost the player one client update, which
// is a fraction of the autosave interval this record is already accurate to.
//
// Only CONNECTED players are sampled — a player who logged off has a binding record with their
// logoff position (increment E) and no business having their recovery position moved by someone
// else's autosave. Each player keeps exactly one record, replaced in place; players sampled by
// an earlier autosave and since gone keep theirs (which of the three positions a boot actually
// hands back is increment D2's decision, not this one).
void game_sv_Single::coop_sample_recoveries()
{
	if (!xr_enet::enabled())
		return;

	struct sampler
	{
		game_sv_Single* self;
		u32 sampled;
		void operator()(IClient* client)
		{
			xrClientData* cd = static_cast<xrClientData*>(client);
			if (cd == self->m_server->GetServerClient()) return;  // host save-actor is not a player
			if (!cd->owner) return;                               // no body yet: nothing to recover to
			LPCSTR nm = coop_player_name(cd);
			if (!nm || !xr_strlen(nm)) return;                    // unnamed: nothing to key on

			CSE_Abstract* actor = cd->owner;
			// MP fork (§14 step 7 phase 4 D2, run 2): only sample a body its client is actually
			// DRIVING. The magnitude gate below catches the wild garbage but not the quiet kind:
			// leg 2 measured a failed hand-over leaving the CSE at -0.1,0.2,0.7 with hp 0.00 —
			// perfectly plausible coordinates, near the world origin, and the sampler dutifully
			// wrote them over the crash-recovery record the player's own return depended on. A
			// client that has never sent an M_CL_UPDATE has never told us where it is, so the
			// server's copy is the server's own invention. Keep the earlier record instead.
			if (cd->m_coop_cl_update_count == 0)
			{
				Msg("! COOP(recovery): '%s' has never sent a client update — NOT sampled (the CSE "
					"describes the server's own copy, not the player; keeping any earlier record)", nm);
				return;
			}

			// Same rule as the checkpoint bank: a position you cannot be put back at is worse
			// than none, so refuse an implausible one instead of persisting a trap. This is not
			// hypothetical — a body whose reclaim delivery failed reads as finite garbage
			// (114688, -4.4e17, -3128 measured 2026-07-26), and sampling it would overwrite the
			// good record the player's own crash recovery depends on. Keep the last good one.
			if (!coop_plausible_pos(actor->o_Position))
			{
				Msg("! COOP(recovery): '%s' has an implausible actor position %.1f,%.1f,%.1f — NOT "
					"sampled (keeping any earlier record)", nm,
					actor->o_Position.x, actor->o_Position.y, actor->o_Position.z);
				return;
			}

			coop_recovery r;
			r.player_name = nm;
			r.pos = actor->o_Position;
			r.node_id = 0;
			r.graph_id = 0;
			r.health = 1.f;
			r.sampled_time = Device.dwTimeGlobal;
			// Sampled here and now: this describes THIS process, so a reclaim must not treat it
			// as a previous process's crash record (D2). Assigned over any persisted record the
			// same player still had, which is exactly the intent — they are back and connected.
			r.persisted = false;
			if (CSE_ALifeCreatureAbstract* creature = smart_cast<CSE_ALifeCreatureAbstract*>(actor))
				r.health = creature->get_health();
			if (CSE_ALifeDynamicObject* dyn = smart_cast<CSE_ALifeDynamicObject*>(actor))
			{
				r.node_id = dyn->m_tNodeID;
				r.graph_id = dyn->m_tGraphID;
			}

			if (coop_recovery* existing = self->coop_find_recovery(nm))
				*existing = r;
			else
				self->m_coop_recoveries.push_back(r);
			++sampled;

			Msg("- COOP(recovery): sampled '%s' pos %.1f,%.1f,%.1f hp=%.2f node=%u",
				nm, r.pos.x, r.pos.y, r.pos.z, r.health, r.node_id);
		}
	};
	sampler s; s.self = this; s.sampled = 0;
	m_server->ForEachClientDo(s);
	Msg("- COOP(recovery): sampled %u connected player(s), %u record(s) held",
		s.sampled, (u32)m_coop_recoveries.size());
}

// MP fork (§14 step 7 phase 4 D2, gap D+E / doc §9.3-9.4): a player who logs off GRACEFULLY is
// no longer "in the world", so they stop having a recovery position — from here on they are
// described by their binding record, which coop_orphan_actor has just stamped with the position
// they left at. This one line is what makes the dirty flag non-load-bearing for POSITION: with
// the record dropped on logoff, its mere presence answers all four cases the flag was invented
// to separate — connected-at-crash => recovery (their real spot), logged-off-then-crash => no
// record => binding (where they left), connected-at-clean-quit => recovery (also where they
// were), logged-off-then-clean-quit => binding. No shutdown path has to be trusted for a player
// to resume in the right place.
void game_sv_Single::coop_drop_recovery(LPCSTR player_name)
{
	if (!player_name || !xr_strlen(player_name))
		return;
	for (auto it = m_coop_recoveries.begin(); it != m_coop_recoveries.end(); ++it)
	{
		if (xr_strcmp(it->player_name.c_str(), player_name))
			continue;
		Msg("- COOP(recovery): dropped '%s' recovery record on a graceful logoff — their binding "
			"record (logged-off position) describes them now", player_name);
		m_coop_recoveries.erase(it);
		return;
	}
}

// MP fork (§14 step 7 phase 4 D2, gap D / doc §9.4): claim the dirty flag for this process, and
// report how the LAST one ended. Present at boot => the previous process died without performing
// a stop (crash, OOM kill, `wineserver -k`).
//
// Deliberately NOT load-bearing for any player's resume position — coop_drop_recovery() above
// resolves that on its own. What this earns: §9.4 wants to KNOW a session was interrupted (for
// policy, for logging, and for whatever anti-abuse rule reads it later), and that is a fact no
// per-player record can carry, because a process can die before it ever writes one.
void game_sv_Single::coop_mark_dirty()
{
	if (m_coop_dirty_checked)
		return;
	m_coop_dirty_checked = true;

	string_path fname, path;
	strconcat(sizeof(fname), fname, COOP_SAVE_SLOT, ".coop.dirty");
	FS.update_path(path, "$game_saves$", fname);

	m_coop_prev_crash = coop_file_on_disk(path);
	m_coop_dirty_streak = 0;         // a clean predecessor deleted the flag: the count starts over
	if (m_coop_prev_crash)
	{
		// Plain stdio, not FS.r_open: this file is written by one process and read by the NEXT
		// one, and FS's reader only serves files that are in its registry. Reading it the same
		// way coop_file_on_disk() tests for it keeps the two answers from ever disagreeing.
		u32 hdr[COOP_DIRTY_WORDS] = {0};
		size_t got = 0;
		if (FILE* f = fopen(path, "rb"))
		{
			got = fread(hdr, sizeof(u32), COOP_DIRTY_WORDS, f);
			fclose(f);
		}
		// A file too short even for the v1 header tells us nothing but its own existence.
		const bool have_hdr = (got >= 4);
		const u32 magic   = have_hdr ? hdr[0] : 0;
		const u32 version = have_hdr ? hdr[1] : 0;
		const u32 pid     = have_hdr ? hdr[2] : 0;
		const u32 stamp   = have_hdr ? hdr[3] : 0;

		// MP fork (§14 step 7 phase 4 D3.4): how many dirty boots in a row, INCLUDING this one.
		// Only a version this build understands may have its payload read: v1 carried no counter
		// (so the predecessor's streak is genuinely 0 and this boot is the first counted one),
		// v2 carries it. Anything else — a newer build's flag, or a v2 file truncated below its
		// own field — is dirty (the magic still says a process died here) with the count
		// RESTARTED, because guessing at a layout we do not know is how a diagnostic becomes a
		// lie. The `else` on this chain is the unknown-VERSION handling D2 did not have.
		u32  prev_streak = 0;
		bool streak_known = false;
		if (magic == COOP_DIRTY_MAGIC && version == 1)
			streak_known = true;                       // v1: no field, and none is missing
		else if (magic == COOP_DIRTY_MAGIC && version == COOP_DIRTY_VERSION && got >= COOP_DIRTY_WORDS)
		{
			prev_streak  = hdr[4];
			streak_known = true;
		}
		m_coop_dirty_streak = streak_known ? prev_streak + 1 : 1;

		if (magic != COOP_DIRTY_MAGIC)
			Msg("! COOP(shutdown): previous process ended DIRTY — '%s' is present but unreadable "
				"(magic 0x%08x); treating it as a crash, which is the safe reading.", fname, magic);
		else if (!streak_known)
			Msg("! COOP(shutdown): previous process ended DIRTY — '%s' is version %u and this "
				"build writes v%u (%u words read). Treating it as a crash and RESTARTING the "
				"consecutive-crash count: a flag written by another build must not have its "
				"fields parsed as if they were ours.",
				fname, version, (u32)COOP_DIRTY_VERSION, (u32)got);
		else
			Msg("! COOP(shutdown): previous process ended DIRTY — '%s' left behind by pid %u "
				"(v%u, unix time %u). Players who were still CONNECTED resume at their recovery "
				"position; anyone who had logged off resumes where they logged off.",
				fname, pid, version, stamp);

		Msg("%s COOP(shutdown): consecutive dirty boots: %u",
			m_coop_dirty_streak >= COOP_DIRTY_LOOP_WARN ? "!" : "-", m_coop_dirty_streak);
		if (m_coop_dirty_streak >= COOP_DIRTY_LOOP_WARN)
			Msg("! COOP(shutdown): CRASH LOOP — %u consecutive dirty boots with no clean stop in "
				"between. This server is either dying on this world every session or failing "
				"during boot; a single crash and a loop look identical in every other log line "
				"we keep. Diagnostic only — nothing here changes what any player is handed. The "
				"count resets when a stop completes cleanly (the flag is deleted).",
				m_coop_dirty_streak);
	}
	else
	{
		Msg("- COOP(shutdown): previous process ended CLEAN — no '%s'", fname);
	}

	const u32 out[COOP_DIRTY_WORDS] = {(u32)COOP_DIRTY_MAGIC, (u32)COOP_DIRTY_VERSION,
	                                   (u32)GetCurrentProcessId(), (u32)time(NULL),
	                                   m_coop_dirty_streak};
	FILE* w = fopen(path, "wb");
	if (!w || fwrite(out, sizeof(u32), COOP_DIRTY_WORDS, w) != COOP_DIRTY_WORDS)
	{
		// Not fatal: the flag is diagnostic. Say so loudly rather than let a later boot read
		// "clean" off a flag we simply failed to write.
		Msg("! COOP(shutdown): cannot write '%s' — a crash of THIS process will look clean", fname);
		if (w) fclose(w);
		return;
	}
	fclose(w);   // flushed and closed NOW: the next thing this flag has to survive is a kill -9
	Msg("- COOP(shutdown): dirty flag '%s' claimed by pid %u (v%u, consecutive dirty boots %u)",
		fname, out[2], (u32)COOP_DIRTY_VERSION, m_coop_dirty_streak);
}

// MP fork (§14 step 7 phase 4 D3.2 / doc §9.4): hand one player one line about their session.
//
// Sent to a single client (never broadcast — the fact is per-player), reliably and ordered on the
// same channel as the body hand-over it follows, so a client cannot be told about a resume before
// it has the body it resumed into. The client logs it unconditionally and passes it to gamedata
// through the mp_api seam; nothing on either side DECIDES anything on it. That is the point: D3
// is where the flag stops being a fact only the server log knows.
void game_sv_Single::coop_send_notice(xrClientData* CL, u8 code, LPCSTR text)
{
	if (!m_server || !CL || !text || !xr_strlen(text))
		return;

	NET_Packet P;
	P.w_begin(M_XRNET_COOP_NOTICE);
	P.w_u8(code);
	P.w_stringZ(text);
	m_server->SendTo(CL->ID, P, net_flags(TRUE, TRUE));

	LPCSTR nm = coop_player_name(CL);
	Msg("- COOP(notice): -> '%s' [%u] %s", nm ? nm : "<unnamed>", u32(code), text);
}

// MP fork (§14 step 7 phase 4 D2): the world on disk is complete and this process is going away
// on purpose. Called only from the clean-stop sequence, and only AFTER the final autosave.
//
// D3.4: this deletion is also the consecutive-crash counter's ONLY reset. Nothing zeroes the
// field in place — the next boot finds no flag, reports CLEAN, and starts its own count at 0.
void game_sv_Single::coop_clear_dirty()
{
	string_path fname, path;
	strconcat(sizeof(fname), fname, COOP_SAVE_SLOT, ".coop.dirty");
	FS.update_path(path, "$game_saves$", fname);

	DeleteFileA(path);               // the flag is written with stdio, so delete it at the OS level
	FS.file_delete(path);            // ...and drop any registry entry a scan may have made

	if (coop_file_on_disk(path))
		Msg("! COOP(shutdown): could NOT delete '%s' — the next boot will read this stop as a crash", fname);
	else
		Msg("- COOP(shutdown): dirty flag '%s' cleared", fname);
}

// MP fork (§14 step 7 phase 4 D2): poll for the stop file. Once a second, off the co-op Update
// tick — an operator stop is not worth a syscall per frame.
void game_sv_Single::coop_check_stop_request()
{
	if (!xr_enet::enabled() || !ai().get_alife())
		return;
	const u32 now = Device.dwTimeGlobal;
	if (m_coop_stop_poll_last && now - m_coop_stop_poll_last < 1000)
		return;
	m_coop_stop_poll_last = now;

	string_path path;
	FS.update_path(path, "$game_saves$", COOP_STOP_FILE);
	if (!coop_file_on_disk(path))
		return;

	// Consume the request FIRST: if anything below fails, the next boot must not stop itself
	// again the moment it comes up. (Created by another process, so it is an OS-level delete —
	// FS.file_delete only touches files its own registry knows about.)
	DeleteFileA(path);
	FS.file_delete(path);
	if (coop_file_on_disk(path))
		Msg("! COOP(shutdown): could not delete the stop file '%s' — delete it before rebooting", path);

	coop_clean_shutdown(COOP_STOP_FILE);
}

// MP fork (§14 step 7 phase 4 D2 / doc §9.4): the clean stop. Flush the world and the sidecar,
// release the dirty flag, then go.
//
// Why this does NOT drive the engine's own `quit` (Console->Execute("quit") =>
// KERNEL:disconnect + KERNEL:quit): disconnect tears the level down through every online
// object's net_Destroy, which is the same per-object script-callback machinery P1 §3a proved
// fatal on the flat co-op gamedata (an AV in the LuaJIT VM), and §3c then confirmed in a live
// playtest. Running it here would put a crash surface INSIDE the shutdown whose whole purpose is
// to be crash-free, and a hang there would leave the port held with no way to observe it. A
// co-op clean stop is therefore defined by what reached DISK — the final autosave, the sidecar,
// the cleared flag — and the process exits immediately once those are done. Nothing later in a
// stock teardown writes co-op state.
void game_sv_Single::coop_clean_shutdown(LPCSTR reason)
{
	// An operator who ran with -coop_autosave 0 asked this server NOT to write the world; a stop
	// is not the moment to overrule them. (Before the flag is parsed we do not know yet, and the
	// conservative answer there is to persist.)
	const bool autosave_off = m_coop_autosave_init && m_coop_autosave_interval_ms == 0;
	Msg("- COOP(shutdown): clean stop requested (%s) — %s", reason ? reason : "?",
		autosave_off ? "autosave is disabled, NOT writing the world" : "taking a final autosave");
	if (!autosave_off)
		coop_autosave();             // world + sidecar as of the stop instant
	coop_clear_dirty();
	Msg("- COOP(shutdown): clean stop complete — exiting");
	FlushLog();
	TerminateProcess(GetCurrentProcess(), 0);
}

// MP fork (§14 step 7 phase 2, gap B): write the player_name -> entity_id ownership map
// for <save_name> to "$game_saves$/<save_name>.coop". Covers both currently connected
// players (CL->owner) and orphaned-but-alive actors (disconnected within the reconnect
// window) — both of those entities are in the .scop, so both must be re-claimable.
// Format (v3): u32 magic, u32 version,
//   u32 count,      count      * { u16 entity_id, stringZ name, u8 have_pos, fvector3 pos }
//   u32 checkpoints, checkpoints * { stringZ name, fvector3 pos, fvector3 angle, float health,
//                                    u32 node, u16 graph, u32 items, items * {...} }   (v2+)
//   u32 recoveries,  recoveries  * { stringZ name, fvector3 pos, u32 node, u16 graph,
//                                    float health }                                    (v3+)
// v1 stops after the bindings and v2 after the checkpoints, and each binding record is
// { u16, stringZ } there; the reader keys every one of those off `version`.
void game_sv_Single::coop_save_bindings(LPCSTR save_name)
{
	if (!save_name || !xr_strlen(save_name))
		return;

	// Collect: live clients first, then orphans (a name can only appear once — a
	// reconnected player's orphan entry is erased on re-association).
	xr_vector<coop_orphan> bindings;
	{
		struct collector
		{
			game_sv_Single* self;
			xr_vector<coop_orphan>* out;
			void operator()(IClient* client)
			{
				xrClientData* cd = static_cast<xrClientData*>(client);
				if (cd == self->m_server->GetServerClient()) return; // host save-actor (id 0) is not a player
				if (!cd->owner) return;
				LPCSTR nm = coop_player_name(cd);
				if (!nm) return;                                     // unnamed client: nothing to match on
				coop_orphan b;
				b.entity_id = cd->owner->ID;
				b.player_name = nm;
				b.disconnect_time = 0;
				b.persistent = true;
				b.frozen = false;
				// MP fork (§14 step 7 phase 4, increment E): a connected player's binding
				// carries their live position too, so the v3 record means the same thing for
				// a player who was online at save time as for one who had logged off.
				b.have_saved_pos = coop_plausible_pos(cd->owner->o_Position);
				if (b.have_saved_pos)
					b.saved_pos = cd->owner->o_Position;
				else
					b.saved_pos.set(0.f, 0.f, 0.f);
				out->push_back(b);
			}
		};
		collector c; c.self = this; c.out = &bindings;
		m_server->ForEachClientDo(c);
		for (const coop_orphan& o : m_coop_orphans)
		{
			if (!o.player_name.size()) continue;   // unnamed: nothing to match on
			bindings.push_back(o);
		}
	}

	string_path fname, path;
	strconcat(sizeof(fname), fname, save_name, ".coop");
	FS.update_path(path, "$game_saves$", fname);

	IWriter* writer = FS.w_open(path);
	if (!writer)
	{
		Msg("! COOP(bindings): cannot open '%s' for writing — ownership will NOT persist", path);
		return;
	}
	writer->w_u32((u32)COOP_BINDINGS_MAGIC);
	writer->w_u32((u32)COOP_BINDINGS_VERSION);
	writer->w_u32((u32)bindings.size());
	for (const coop_orphan& b : bindings)
	{
		writer->w_u16(b.entity_id);
		writer->w_stringZ(b.player_name.c_str());
		// v3 (phase 4, increment E): the position the server recorded for this body — at
		// logoff for an orphan, at save time for a connected player. A reclaim after a
		// RESTART then has a deliberately-recorded value to restore and never has to trust
		// what a reloaded entity says about itself (P2 §3b rule 2).
		writer->w_u8(b.have_saved_pos ? 1 : 0);
		writer->w_fvector3(b.saved_pos);
	}

	// MP fork (§14 step 7 phase 3 C2): version 2 appends the CHECKPOINT block after the
	// binding records. A player's checkpoint is per-instance person-state (§9.1) and is
	// useless if it dies with the process — the whole point is that death rolls you back
	// to it, including a death after a server bounce. Readers of v1 files see no block.
	writer->w_u32((u32)m_coop_checkpoints.size());
	for (const coop_checkpoint& c : m_coop_checkpoints)
	{
		writer->w_stringZ(c.player_name.size() ? c.player_name.c_str() : "");
		writer->w_fvector3(c.pos);
		writer->w_fvector3(c.angle);
		writer->w_float(c.health);
		writer->w_u32(c.node_id);
		writer->w_u16(c.graph_id);
		writer->w_u32((u32)c.items.size());
		for (const coop_checkpoint_item& it : c.items)
		{
			writer->w_stringZ(it.section.size() ? it.section.c_str() : "");
			writer->w_float(it.condition);
			writer->w_u16(it.ammo_elapsed);
			writer->w_u8(it.ammo_type);
			writer->w_u8(it.slot);
		}
	}

	// MP fork (§14 step 7 phase 4 D1): version 3 appends the RECOVERY block after the
	// checkpoints — where each player actually was as of this save (§9.4). Written last so a
	// v1/v2 reader's stream ends exactly where it always did. sampled_time is deliberately not
	// written: it is a Device.dwTimeGlobal, which restarts with the process and would be
	// meaningless (the checkpoint block drops banked_time for the same reason).
	writer->w_u32((u32)m_coop_recoveries.size());
	for (const coop_recovery& r : m_coop_recoveries)
	{
		writer->w_stringZ(r.player_name.size() ? r.player_name.c_str() : "");
		writer->w_fvector3(r.pos);
		writer->w_u32(r.node_id);
		writer->w_u16(r.graph_id);
		writer->w_float(r.health);
	}
	FS.w_close(writer);

	Msg("- COOP(bindings): saved %u binding(s) + %u checkpoint(s) + %u recovery record(s) to '%s'",
		(u32)bindings.size(), (u32)m_coop_checkpoints.size(), (u32)m_coop_recoveries.size(), fname);
	for (const coop_orphan& b : bindings)
		Msg("- COOP(bindings):   '%s' -> entity id %u", b.player_name.c_str(), b.entity_id);
}

// IReader::r_stringZ(shared_str&) constructs straight off the raw buffer and advances by the
// string's length — with NO bounds check. A truncated or corrupt sidecar whose last record
// lacks a terminator would read past the end of the file mapping (CodeRabbit). Confirm a NUL
// exists in the bytes that remain before handing the buffer over; false = stop reading.
static bool coop_read_stringZ(IReader* reader, shared_str& out)
{
	const char* raw = (const char*)reader->pointer();
	const int left = reader->elapsed();
	int len = 0;
	while (len < left && raw[len]) ++len;
	if (len >= left)
		return false;
	reader->r_stringZ(out);
	return true;
}

// MP fork (§14 step 7 phase 2, gap B): read "<save_name>.coop" back after the world loads
// and seed m_coop_orphans with PERSISTENT entries, so the first client that connects under
// a recorded name re-claims its own persisted actor through the existing reconnection path
// instead of being handed a fresh spawn. The entities are also flagged m_coop_orphaned so
// Perform_connect_spawn cannot hand a persisted body to the wrong (or an unnamed) client.
// A missing sidecar is normal (fresh world / save written before phase 2) -> no-op.
void game_sv_Single::coop_load_bindings(LPCSTR save_name)
{
	if (!save_name || !xr_strlen(save_name) || !ai().get_alife())
		return;

	string_path fname, path;
	strconcat(sizeof(fname), fname, save_name, ".coop");
	FS.update_path(path, "$game_saves$", fname);

	// Absent sidecar is the NORMAL case for a fresh world or a pre-phase-2 save: r_open
	// returns NULL and every player simply gets a fresh actor (existing behaviour).
	IReader* reader = FS.r_open(path);
	if (!reader)
	{
		Msg("- COOP(bindings): no ownership sidecar '%s' — every player gets a fresh actor", fname);
		return;
	}

	u32 restored = 0, stale = 0, count = 0;
	// Blocks are positional: the checkpoint block starts where the bindings ended and the
	// recovery block where the checkpoints ended. So the moment ONE block gives up mid-record
	// the read cursor no longer points at a block header, and every later block must be
	// abandoned rather than parsed out of whatever bytes happen to be under the cursor.
	bool stream_ok = true;
	if (reader->elapsed() < (int)(3 * sizeof(u32)))
	{
		Msg("! COOP(bindings): '%s' is truncated (%d bytes) — ignored", fname, reader->elapsed());
		FS.r_close(reader);
		return;
	}
	const u32 magic = reader->r_u32();
	const u32 version = reader->r_u32();
	// v1 = bindings only; v2 (phase 3 C2) appends a checkpoint block; v3 (phase 4 D1/E) widens
	// the binding record and appends a recovery block. Older files stay readable on purpose —
	// a sidecar written before checkpoints or recovery existed must not cost a player their
	// body, it just means they have neither yet.
	if (magic != COOP_BINDINGS_MAGIC || version < 1 || version > COOP_BINDINGS_VERSION)
	{
		Msg("! COOP(bindings): '%s' has magic 0x%08x version %u (expected 0x%08x / <=%u) — ignored",
			fname, magic, version, (u32)COOP_BINDINGS_MAGIC, (u32)COOP_BINDINGS_VERSION);
		FS.r_close(reader);
		return;
	}
	count = reader->r_u32();

	for (u32 i = 0; i < count; ++i)
	{
		if (reader->elapsed() < (int)sizeof(u16))
		{
			Msg("! COOP(bindings): '%s' ends after %u of %u record(s) — rest ignored", fname, i, count);
			stream_ok = false;
			break;
		}
		const u16 eid = reader->r_u16();

		shared_str name;
		if (!coop_read_stringZ(reader, name))
		{
			Msg("! COOP(bindings): '%s' record %u has an unterminated name — rest ignored", fname, i);
			stream_ok = false;
			break;
		}

		// v3 (phase 4, increment E): the recorded position rides in the record itself. Read it
		// BEFORE any of the skip paths below — a record that is ignored must still be consumed
		// whole, or every later record (and block) reads from the wrong offset.
		Fvector rec_pos; rec_pos.set(0.f, 0.f, 0.f);
		bool rec_have_pos = false;
		if (version >= 3)
		{
			const int tail = (int)(sizeof(u8) + sizeof(Fvector));
			if (reader->elapsed() < tail)
			{
				Msg("! COOP(bindings): '%s' record %u is truncated before its position — rest ignored",
					fname, i);
				stream_ok = false;
				break;
			}
			rec_have_pos = reader->r_u8() != 0;
			reader->r_fvector3(rec_pos);
			// A recorded position that isn't finite is exactly the trap this field exists to
			// avoid restoring; fall back to the .scop entity rather than resume into the void.
			if (rec_have_pos && !coop_plausible_pos(rec_pos))
			{
				Msg("! COOP(bindings): '%s' record %u has a non-finite saved position — ignoring it",
					fname, i);
				rec_have_pos = false;
			}
		}

		if (!name.size())
		{
			Msg("! COOP(bindings): record %u has an empty player name — ignored", i);
			++stale;
			continue;
		}

		CSE_Abstract* entity = ai().alife().objects().object(eid, true);
		if (!entity)
		{
			Msg("! COOP(bindings): '%s' -> entity id %u is NOT in the loaded world — will get a fresh actor",
				name.c_str(), eid);
			++stale;
			continue;
		}

		// Reserve the body: nothing else may claim it before its owner logs in.
		entity->m_coop_orphaned = true;
		for (u16 child_id : entity->children)
		{
			CSE_Abstract* child = ai().alife().objects().object(child_id, true);
			if (child) child->m_coop_orphaned = true;
		}

		coop_orphan o;
		o.entity_id = eid;
		o.player_name = name;
		o.disconnect_time = Device.dwTimeGlobal;
		o.persistent = true;          // never expires — see coop_cleanup_orphans()
		// Prefer the position the SERVER recorded for this body (v3, increment E) over the one
		// the entity carries: for a player who logged off it is the logoff position taken at
		// freeze time, and it cannot have been touched by the load->online round trip. Reading
		// the entity is the v1/v2 fallback and is safe HERE (this runs at load, before the body
		// goes online and before P2 §3b's corruption) — but only here.
		if (rec_have_pos)
		{
			o.saved_pos = rec_pos;
			o.have_saved_pos = true;
		}
		else
		{
			o.saved_pos = entity->o_Position;   // v1/v2 sidecar: straight out of the .scop
			o.have_saved_pos = coop_plausible_pos(entity->o_Position);
		}
		// D2 run 2: read the health straight out of the freshly loaded entity, for the same
		// reason the position may be read here and nowhere later — this runs BEFORE the body
		// goes online, so it is the .scop's value and not the corruption's. If the save itself
		// holds a dead body, leave have_saved_health false and let the reclaim's later sources
		// answer; a checkpoint respawn (P3) is what a genuinely dead player gets.
		o.have_saved_health = false;
		o.saved_health = 1.f;
		if (CSE_ALifeCreatureAbstract* creature = smart_cast<CSE_ALifeCreatureAbstract*>(entity))
		{
			o.saved_health = creature->get_health();
			o.have_saved_health = o.saved_health > 0.f;
		}
		o.frozen = false;             // becomes true once it is online and detached
		m_coop_orphans.push_back(o);
		++restored;

		// The harness scrapes this line for the position a body came back at — keep the
		// existing shape and report the SIDECAR value separately when v3 supplied one, so a
		// disagreement between the two is visible in the log instead of silently resolved.
		if (rec_have_pos)
			Msg("- COOP(bindings): '%s' sidecar position %.1f,%.1f,%.1f (entity says %.1f,%.1f,%.1f)",
				name.c_str(), rec_pos.x, rec_pos.y, rec_pos.z,
				entity->o_Position.x, entity->o_Position.y, entity->o_Position.z);

		CSE_ALifeDynamicObject* dyn = smart_cast<CSE_ALifeDynamicObject*>(entity);
		Msg("- COOP(bindings): restored '%s' -> entity id %u (online=%s, %d item(s), pos %.1f,%.1f,%.1f)",
			name.c_str(), eid, (dyn && dyn->m_bOnline) ? "yes" : "no",
			(int)entity->children.size(),
			entity->o_Position.x, entity->o_Position.y, entity->o_Position.z);
	}
	// --- v2: the CHECKPOINT block (§14 step 7 phase 3 C2) ---
	u32 checkpoints = 0;
	if (stream_ok && version >= 2 && reader->elapsed() >= (int)sizeof(u32))
	{
		const u32 cp_count = reader->r_u32();
		for (u32 i = 0; i < cp_count; ++i)
		{
			coop_checkpoint c;
			if (!coop_read_stringZ(reader, c.player_name))
			{
				Msg("! COOP(bindings): '%s' checkpoint %u has an unterminated name — rest ignored", fname, i);
				stream_ok = false;
				break;
			}
			// fixed-size head: 2 vec3 + float + u32 + u16 + u32(item count)
			const int head = (int)(2 * sizeof(Fvector) + sizeof(float) + sizeof(u32) + sizeof(u16) + sizeof(u32));
			if (reader->elapsed() < head)
			{
				Msg("! COOP(bindings): '%s' checkpoint %u is truncated — rest ignored", fname, i);
				stream_ok = false;
				break;
			}
			reader->r_fvector3(c.pos);
			reader->r_fvector3(c.angle);
			c.health = reader->r_float();
			c.node_id = reader->r_u32();
			c.graph_id = reader->r_u16();
			c.banked_time = Device.dwTimeGlobal;   // runtime-only field; re-stamped on load
			const u32 item_count = reader->r_u32();

			bool truncated = false;
			for (u32 k = 0; k < item_count; ++k)
			{
				coop_checkpoint_item it;
				if (!coop_read_stringZ(reader, it.section))
				{
					Msg("! COOP(bindings): '%s' checkpoint %u item %u has an unterminated "
						"section — rest ignored", fname, i, k);
					truncated = true;
					break;
				}
				const int item_tail = (int)(sizeof(float) + sizeof(u16) + 2 * sizeof(u8));
				if (reader->elapsed() < item_tail)
				{
					Msg("! COOP(bindings): '%s' checkpoint %u item %u is truncated — rest ignored",
						fname, i, k);
					truncated = true;
					break;
				}
				it.condition = reader->r_float();
				it.ammo_elapsed = reader->r_u16();
				it.ammo_type = reader->r_u8();
				it.slot = reader->r_u8();
				c.items.push_back(it);
			}

			// A checkpoint whose item list was cut short would silently roll the player back
			// with PART of their gear — worse than an honest "no checkpoint", because the
			// loss looks like a game rule rather than a corrupt file. Drop it. (CodeRabbit)
			if (truncated)
			{
				Msg("! COOP(bindings): checkpoint %u for '%s' has an incomplete item list — dropped",
					i, c.player_name.size() ? c.player_name.c_str() : "");
				stream_ok = false;
				break;
			}

			// A checkpoint you cannot be put back at is worse than none: drop unnamed or
			// non-finite records rather than let a death teleport someone into the void.
			if (!c.player_name.size() || !coop_plausible_pos(c.pos))
			{
				Msg("! COOP(bindings): checkpoint %u is unusable (name='%s', finite pos=%s) — dropped",
					i, c.player_name.size() ? c.player_name.c_str() : "", coop_plausible_pos(c.pos) ? "yes" : "no");
			}
			else
			{
				m_coop_checkpoints.push_back(c);
				++checkpoints;
				Msg("- COOP(checkpoint): restored '%s' pos %.1f,%.1f,%.1f hp=%.2f items=%u",
					c.player_name.c_str(), c.pos.x, c.pos.y, c.pos.z, c.health, (u32)c.items.size());
			}
		}
	}

	// --- v3: the RECOVERY block (§14 step 7 phase 4 D1) ---
	// Where each player actually was as of the save this sidecar belongs to. Loading it is all
	// D1 does with it: nothing reads m_coop_recoveries at boot yet — increment D2 adds the
	// dirty flag that decides between this position, the logged-off one, and the checkpoint.
	u32 recoveries = 0;
	if (stream_ok && version >= 3 && reader->elapsed() >= (int)sizeof(u32))
	{
		const u32 rec_count = reader->r_u32();
		for (u32 i = 0; i < rec_count; ++i)
		{
			coop_recovery r;
			if (!coop_read_stringZ(reader, r.player_name))
			{
				Msg("! COOP(bindings): '%s' recovery %u has an unterminated name — rest ignored", fname, i);
				stream_ok = false;
				break;
			}
			const int head = (int)(sizeof(Fvector) + sizeof(u32) + sizeof(u16) + sizeof(float));
			if (reader->elapsed() < head)
			{
				Msg("! COOP(bindings): '%s' recovery %u is truncated — rest ignored", fname, i);
				stream_ok = false;
				break;
			}
			reader->r_fvector3(r.pos);
			r.node_id = reader->r_u32();
			r.graph_id = reader->r_u16();
			r.health = reader->r_float();
			r.sampled_time = Device.dwTimeGlobal;   // runtime-only field; re-stamped on load
			// D2: this record came out of a file, so it describes the PREVIOUS process — i.e.
			// a player who was still connected when it ended. That is the flag the reclaim
			// reads to decide it owes them their real position rather than a logged-off one.
			r.persisted = true;

			// Same rule as the checkpoint block: a position nobody can be put back at is worse
			// than no record, because the player resumes in the void instead of at their
			// logged-off position. Drop it and let the other two candidates answer.
			if (!r.player_name.size() || !coop_plausible_pos(r.pos))
			{
				Msg("! COOP(bindings): recovery %u is unusable (name='%s', finite pos=%s) — dropped",
					i, r.player_name.size() ? r.player_name.c_str() : "", coop_plausible_pos(r.pos) ? "yes" : "no");
				continue;
			}

			// One record per player, as the sampler maintains it.
			if (coop_recovery* existing = coop_find_recovery(r.player_name.c_str()))
				*existing = r;
			else
				m_coop_recoveries.push_back(r);
			++recoveries;
			Msg("- COOP(recovery): restored '%s' pos %.1f,%.1f,%.1f hp=%.2f node=%u",
				r.player_name.c_str(), r.pos.x, r.pos.y, r.pos.z, r.health, r.node_id);
		}
	}

	FS.r_close(reader);

	Msg("- COOP(bindings): loaded %u/%u binding(s) + %u checkpoint(s) + %u recovery record(s) "
		"from '%s' (v%u, %u stale)",
		restored, count, checkpoints, recoveries, fname, version, stale);
}

// MP fork (§14 step 7 phase 3, gap C): find a player's banked checkpoint (NULL if none).
game_sv_Single::coop_checkpoint* game_sv_Single::coop_find_checkpoint(LPCSTR player_name)
{
	if (!player_name || !xr_strlen(player_name))
		return NULL;
	for (coop_checkpoint& c : m_coop_checkpoints)
		if (!xr_strcmp(c.player_name.c_str(), player_name))
			return &c;
	return NULL;
}

// MP fork (§14 step 7 phase 3, gap C / doc §9.1): snapshot one player's PERSON-state — the
// thing a death rolls back to, while everything they added to the WORLD stays put. Read
// entirely off the ENGINE CSEs: the actor entity for position/health/location and its child
// item entities for the inventory. Deliberately NOT via the GAMMA script save-manager (P1
// §3a: firing per-object Lua save callbacks on the dedicated server corrupts the LuaJIT VM).
// Re-banking replaces the player's previous checkpoint. Returns true if a snapshot was taken.
bool game_sv_Single::coop_bank_checkpoint(LPCSTR player_name)
{
	if (!xr_enet::enabled() || !ai().get_alife())
		return false;
	if (!player_name || !xr_strlen(player_name))
	{
		Msg("! COOP(checkpoint): refusing to bank a checkpoint for an unnamed player");
		return false;
	}

	// Locate that player's live actor. Same name rule as every other per-player record.
	struct finder
	{
		game_sv_Single* self;
		LPCSTR want;
		CSE_Abstract* found;
		void operator()(IClient* client)
		{
			if (found) return;
			xrClientData* cd = static_cast<xrClientData*>(client);
			if (cd == self->m_server->GetServerClient()) return; // host save-actor is not a player
			if (!cd->owner) return;
			LPCSTR nm = coop_player_name(cd);
			if (nm && !xr_strcmp(nm, want)) found = cd->owner;
		}
	};
	finder f; f.self = this; f.want = player_name; f.found = NULL;
	m_server->ForEachClientDo(f);
	if (!f.found)
	{
		Msg("! COOP(checkpoint): no connected player '%s' owns an actor — nothing to bank", player_name);
		return false;
	}

	CSE_Abstract* actor = f.found;
	// P2 §3b found that an actor CSE can carry garbage coordinates after a load->online
	// round trip. A checkpoint exists to put a player BACK somewhere, so refuse to bank a
	// position that isn't plausible rather than store a trap they respawn into. The garbage is
	// finite, so this needs the magnitude gate and not just _valid() — see coop_plausible_pos().
	if (!coop_plausible_pos(actor->o_Position))
	{
		Msg("! COOP(checkpoint): '%s' has an implausible actor position %.1f,%.1f,%.1f — NOT banking",
			player_name, actor->o_Position.x, actor->o_Position.y, actor->o_Position.z);
		return false;
	}

	coop_checkpoint cp;
	cp.player_name = player_name;
	cp.pos = actor->o_Position;
	cp.angle = actor->o_Angle;
	cp.health = 1.f;
	cp.node_id = 0;
	cp.graph_id = 0;
	cp.banked_time = Device.dwTimeGlobal;

	if (CSE_ALifeCreatureAbstract* creature = smart_cast<CSE_ALifeCreatureAbstract*>(actor))
		cp.health = creature->get_health();
	if (CSE_ALifeDynamicObject* dyn = smart_cast<CSE_ALifeDynamicObject*>(actor))
	{
		cp.node_id = dyn->m_tNodeID;      // rollback lands on the nav mesh, not just a point
		cp.graph_id = dyn->m_tGraphID;
	}

	for (u16 child_id : actor->children)
	{
		CSE_Abstract* child = m_server->ID_to_entity(child_id);
		if (!child)
			child = ai().alife().objects().object(child_id, true);   // offline items still count
		if (!child)
			continue;

		coop_checkpoint_item it;
		it.section = child->s_name;
		it.condition = 1.f;
		it.ammo_elapsed = 0;
		it.ammo_type = 0;
		it.slot = 0xff;

		if (CSE_ALifeInventoryItem* inv = smart_cast<CSE_ALifeInventoryItem*>(child))
			it.condition = inv->m_fCondition;
		if (CSE_ALifeItemWeapon* wpn = smart_cast<CSE_ALifeItemWeapon*>(child))
		{
			it.ammo_elapsed = wpn->a_elapsed;   // rounds in the magazine
			it.ammo_type = wpn->ammo_type;
			// get_slot() is a bare pSettings->r_u8(s_name,"slot") — a section without that
			// line would hard-error the server mid-snapshot. Banking must never be able to
			// kill the server, so read it only when the line exists.
			it.slot = pSettings->line_exist(child->s_name, "slot")
				? (u8)pSettings->r_u8(child->s_name, "slot") : u8(0xff);
		}
		else if (CSE_ALifeItemAmmo* ammo = smart_cast<CSE_ALifeItemAmmo*>(child))
			it.ammo_elapsed = ammo->a_elapsed;  // rounds left in the box

		cp.items.push_back(it);
	}

	// Re-banking replaces the previous checkpoint for this player.
	if (coop_checkpoint* existing = coop_find_checkpoint(player_name))
		*existing = cp;
	else
		m_coop_checkpoints.push_back(cp);

	Msg("- COOP(checkpoint): banked '%s' pos %.1f,%.1f,%.1f hp=%.2f node=%u items=%u",
		player_name, cp.pos.x, cp.pos.y, cp.pos.z, cp.health, cp.node_id, (u32)cp.items.size());
	return true;
}

// MP fork (§14 step 7 phase 3 C3, harness): drop one item into the WORLD (unparented, next
// to the player) right after the checkpoint is banked, and remember it. §9.2 says a death
// rewinds the PERSON and leaves the WORLD untouched — so the rollback re-checks this entity
// and reports whether it is still there and unmoved. Gated on -coop_test_worlditem [section].
void game_sv_Single::coop_test_drop_world_item()
{
	LPCSTR p = strstr(Core.Params, "-coop_test_worlditem");
	if (!p)
		return;

	// optional section argument; default to something every install has
	string64 section = "";
	p += sizeof("-coop_test_worlditem") - 1;
	while (*p == ' ') ++p;
	if (*p && *p != '-')
	{
		u32 i = 0;
		while (*p && *p != ' ' && i < sizeof(section) - 1) section[i++] = *p++;
		section[i] = 0;
	}
	if (!xr_strlen(section))
		xr_strcpy(section, "medkit");

	// place it beside the first player we can find (any actor will do — it just has to be
	// somewhere the world is loaded, so the entity is real and online)
	struct any_actor
	{
		game_sv_Single* self; CSE_Abstract* found;
		void operator()(IClient* client)
		{
			if (found) return;
			xrClientData* cd = static_cast<xrClientData*>(client);
			if (cd == self->m_server->GetServerClient()) return;
			if (cd->owner) found = cd->owner;
		}
	};
	any_actor a; a.self = this; a.found = NULL;
	m_server->ForEachClientDo(a);
	if (!a.found)
		return;

	CSE_Abstract* it = F_entity_Create(section);
	if (!it)
	{
		Msg("! COOP(checkpoint-test): world item section '%s' invalid", section);
		return;
	}
	CSE_ALifeDynamicObject* od = smart_cast<CSE_ALifeDynamicObject*>(a.found);
	it->s_name = section;
	it->set_name_replace("");
	it->s_RP = 0xFE;
	it->ID = 0xffff;
	it->ID_Phantom = 0xffff;
	it->ID_Parent = 0xffff;             // WORLD item: parented to nobody
	it->RespawnTime = 0;
	it->o_Position = a.found->o_Position;
	it->o_Position.x += 2.f;            // beside the player, not inside them
	it->s_flags.assign(M_SPAWN_OBJECT_LOCAL);
	if (CSE_ALifeDynamicObject* dyn = smart_cast<CSE_ALifeDynamicObject*>(it))
		if (od) { dyn->m_tNodeID = od->m_tNodeID; dyn->m_tGraphID = od->m_tGraphID; }
	if (CSE_ALifeObject* al = smart_cast<CSE_ALifeObject*>(it))
	{
		al->m_story_id = INVALID_STORY_ID;
		al->m_spawn_story_id = INVALID_SPAWN_STORY_ID;
	}

	CSE_Abstract* N = spawn_end(it, m_server->GetServerClient()->ID);
	if (!N)
	{
		Msg("! COOP(checkpoint-test): failed to spawn world item '%s'", section);
		return;
	}
	m_coop_test_worlditem_id = N->ID;
	m_coop_test_worlditem_pos = N->o_Position;
	Msg("- COOP(checkpoint-test): world item '%s' id=%u pos %.1f,%.1f,%.1f",
		section, N->ID, N->o_Position.x, N->o_Position.y, N->o_Position.z);
}

// MP fork (§14 step 7 phase 3 C3 / doc §9.1-9.2): roll a dead player's PERSON back to their
// checkpoint. The WORLD is deliberately untouched — anything they dropped, opened or killed
// stays exactly as it is; only entities parented to the actor are rewound. Returns true (and
// fills io_pos with the checkpoint position) when a checkpoint was applied; false leaves the
// caller's existing death-position behaviour alone, which is what a player with no checkpoint
// still gets.
bool game_sv_Single::coop_checkpoint_respawn(u16 actor_id, xrClientData* CL, Fvector& io_pos, float& io_health)
{
	if (!xr_enet::enabled() || !ai().get_alife())
		return false;

	LPCSTR nm = coop_player_name(CL);
	coop_checkpoint* cp = coop_find_checkpoint(nm);
	if (!cp)
		return false;
	if (!coop_plausible_pos(cp->pos))
	{
		Msg("! COOP(checkpoint): '%s' has an implausible checkpoint — falling back to death position", nm);
		return false;
	}

	CSE_Abstract* actor = get_entity_from_eid(actor_id);
	if (!actor)
	{
		Msg("! COOP(checkpoint): respawn for '%s': actor %u not found", nm, actor_id);
		return false;
	}

	// --- position + health (the CSE is authoritative; the client is told separately) ---
	io_pos = cp->pos;
	io_health = (cp->health > 0.f) ? cp->health : 1.f;
	actor->o_Position = cp->pos;
	actor->o_Angle = cp->angle;
	if (CSE_ALifeDynamicObject* dyn = smart_cast<CSE_ALifeDynamicObject*>(actor))
	{
		dyn->m_tNodeID = cp->node_id;     // land on the nav mesh, not merely at a point
		dyn->m_tGraphID = cp->graph_id;
	}

	// --- inventory rollback: destroy what they are carrying now, restore what they banked ---
	// Snapshot the child ids first: Perform_destroy mutates the children vector as it goes.
	xr_vector<u16> current = actor->children;
	// Name what is on the body BEFORE destroying it — after the loop these entities are gone
	// from both the server map and A-Life, so the sections are unreadable.
	string4096 at_death;
	at_death[0] = 0;
	for (u16 child_id : current)
	{
		CSE_Abstract* c = m_server->ID_to_entity(child_id);
		if (!c)
			c = ai().alife().objects().object(child_id, true);
		xr_strcat(at_death, sizeof(at_death), c ? c->s_name.c_str() : "<unknown>");
		xr_strcat(at_death, sizeof(at_death), " ");
	}
	u32 destroyed = 0;
	u32 skipped = 0;
	for (u16 child_id : current)
	{
		CSE_Abstract* child = m_server->ID_to_entity(child_id);
		if (!child)
		{
			// C3 follow-up diagnostic: banking falls back to the A-Life registry for a child the
			// server ID map misses, but destroying one that way is not safe from here — so say so
			// out loud. A child that is banked and NOT destroyed comes back duplicated.
			++skipped;
			CSE_Abstract* al = ai().alife().objects().object(child_id, true);
			Msg("! COOP(checkpoint): rollback child id=%u not in the server ID map (%s) — NOT destroyed",
				child_id, al ? al->name_replace() : "and not in A-Life either");
			continue;
		}
		m_server->Perform_destroy(child, net_flags(TRUE, TRUE));
		++destroyed;
	}
	// The counts that matter for the duplication question: how many children the actor had at
	// death vs how many the checkpoint banked. Measured: the list SHRINKS by one across the
	// death itself (9 banked, 8 present) and not with elapsed time, so the death detaches or
	// consumes exactly one item. These two section lists name it.
	Msg("- COOP(checkpoint): rollback children at death=%u (destroyed %u, skipped %u), banked=%u",
		(u32)current.size(), destroyed, skipped, (u32)cp->items.size());
	{
		string4096 at_bank;
		at_bank[0] = 0;
		for (const coop_checkpoint_item& it : cp->items)
		{
			xr_strcat(at_bank, sizeof(at_bank), it.section.size() ? it.section.c_str() : "<empty>");
			xr_strcat(at_bank, sizeof(at_bank), " ");
		}
		Msg("- COOP(checkpoint):   at bank : %s", at_bank);
		Msg("- COOP(checkpoint):   at death: %s", at_death);
	}

	u32 restored = 0;
	for (const coop_checkpoint_item& it : cp->items)
	{
		if (!it.section.size())
			continue;
		if (coop_spawn_checkpoint_item(actor, CL, it))
			++restored;
	}

	Msg("- COOP(checkpoint): ROLLBACK '%s' -> pos %.1f,%.1f,%.1f hp=%.2f "
		"(dropped %u carried item(s), restored %u of %u banked)",
		nm, cp->pos.x, cp->pos.y, cp->pos.z, io_health, destroyed, restored, (u32)cp->items.size());

	// §9.2: the rollback must NOT have touched the world. Report the harness's world item.
	if (m_coop_test_worlditem_id != 0xffff)
	{
		CSE_Abstract* w = m_server->ID_to_entity(m_coop_test_worlditem_id);
		if (!w)
			w = ai().alife().objects().object(m_coop_test_worlditem_id, true);
		if (w)
			Msg("- COOP(checkpoint-test): world item id=%u PRESENT at %.1f,%.1f,%.1f (was %.1f,%.1f,%.1f)",
				m_coop_test_worlditem_id, w->o_Position.x, w->o_Position.y, w->o_Position.z,
				m_coop_test_worlditem_pos.x, m_coop_test_worlditem_pos.y, m_coop_test_worlditem_pos.z);
		else
			Msg("! COOP(checkpoint-test): world item id=%u MISSING after rollback — the rollback "
				"ate a world entity", m_coop_test_worlditem_id);
	}
	return true;
}

// Spawn one banked item back into the player's inventory. Same shape as
// coop_give_starting_kit's per-item path (parented => Process_spawn attaches and replicates
// it LOCAL to the owner), with the banked condition and magazine state applied.
bool game_sv_Single::coop_spawn_checkpoint_item(CSE_Abstract* owner, xrClientData* CL,
                                                const coop_checkpoint_item& rec)
{
	if (!owner || !CL)
		return false;

	LPCSTR sec = rec.section.c_str();
	CSE_Abstract* it = F_entity_Create(sec);
	if (!it)
	{
		Msg("! COOP(checkpoint): rollback section '%s' invalid (skipped)", sec);
		return false;
	}
	it->s_name = sec;
	it->set_name_replace("");
	it->s_RP = 0xFE;
	it->ID = 0xffff;
	it->ID_Phantom = 0xffff;
	it->ID_Parent = owner->ID;
	it->RespawnTime = 0;
	it->o_Position = owner->o_Position;
	it->s_flags.assign(M_SPAWN_OBJECT_LOCAL);

	CSE_ALifeDynamicObject* od = smart_cast<CSE_ALifeDynamicObject*>(owner);
	if (CSE_ALifeDynamicObject* dyn = smart_cast<CSE_ALifeDynamicObject*>(it))
		if (od) { dyn->m_tNodeID = od->m_tNodeID; dyn->m_tGraphID = od->m_tGraphID; }
	// Story ids must be cleared for the same reason coop_clone_inventory_for clears them:
	// a duplicate story object collides in the client's story registry and fails to spawn.
	if (CSE_ALifeObject* al = smart_cast<CSE_ALifeObject*>(it))
	{
		al->m_story_id = INVALID_STORY_ID;
		al->m_spawn_story_id = INVALID_SPAWN_STORY_ID;
	}
	if (CSE_ALifeInventoryItem* inv = smart_cast<CSE_ALifeInventoryItem*>(it))
		if (rec.condition > 0.f && rec.condition <= 1.f)
			inv->m_fCondition = rec.condition;
	if (CSE_ALifeItemWeapon* wpn = smart_cast<CSE_ALifeItemWeapon*>(it))
	{
		wpn->a_elapsed = rec.ammo_elapsed;
		wpn->ammo_type = rec.ammo_type;
	}
	else if (CSE_ALifeItemAmmo* ammo = smart_cast<CSE_ALifeItemAmmo*>(it))
		if (rec.ammo_elapsed)
			ammo->a_elapsed = rec.ammo_elapsed;

	return spawn_end(it, CL->ID) != NULL;
}

void game_sv_Single::coop_poll_spawns()
{
	if (!xr_enet::enabled() || !ai().get_alife())
		return; // co-op (ENet) only; stock single-player untouched

	// Expire orphaned actors past the reconnect timeout
	coop_cleanup_orphans();

	// Detach server ownership of save-restored bodies the moment they come online, so
	// nothing overwrites the position their owner logged off at (§14 step 7 phase 2)
	coop_freeze_restored_bodies();

	// A co-op client never reliably sends M_CLIENTREADY, and it can't be net_Ready
	// before it has a Local actor to export (chicken-and-egg). So use a grace period:
	// once we've seen an actorless client for GRACE_MS (time to finish loading the
	// level), spawn its actor.
	const u32 now = Device.dwTimeGlobal; // grace: spawn 6s after first sighting (load time)

	struct collector
	{
		game_sv_Single* self;
		u32 now;
		xr_vector<xrClientData*> pending;
		void operator()(IClient* client)
		{
			xrClientData* CL = static_cast<xrClientData*>(client);
			if (CL == self->m_server->GetServerClient()) return; // no player on the server
			if (CL->owner) return;                               // already has an actor
			u32& seen = self->m_coop_seen[CL->ID.value()];
			if (seen == 0) { seen = now; return; }               // first sighting: start grace
			if (now - seen < 6000) return;                       // still loading
			pending.push_back(CL);
		}
	};
	collector c; c.self = this; c.now = now;
	m_server->ForEachClientDo(c);
	for (xrClientData* CL : c.pending)
	{
		// MP fork (§9.3/9.4 co-op reconnection): check if this client matches an orphaned
		// actor from a previous disconnect. If so, re-associate the existing entity
		// (preserving position, health, and inventory) instead of spawning fresh.
		LPCSTR client_name = coop_player_name(CL);   // ps account name is empty on thin clients
		Fvector saved_pos; bool have_saved_pos = false; float saved_health = -1.f;
		CSE_Abstract* orphan = coop_find_orphan(client_name, &saved_pos, &have_saved_pos, &saved_health);
		if (orphan)
		{
			// MP fork (§14 step 7 phase 4 D2 / doc §9.4) — THE BOOT DECISION. Two candidate
			// positions reach this point and exactly one is right:
			//   * a PERSISTED recovery record, which exists only for a player who was still
			//     connected when the previous process ended (a graceful logoff drops it), so it
			//     is the last place they actually were. A crash is not a death: it must not cost
			//     them the walk, and it must never hand them their CHECKPOINT — that belongs to
			//     the death path (P3 C3) and is deliberately not consulted here;
			//   * otherwise the binding record's position: where they logged off (§9.3), or for
			//     a live reconnect within one process, where the body was frozen.
			// The dirty flag agrees with this by construction and is not consulted either — see
			// coop_drop_recovery() for the four-case argument.
			LPCSTR pos_source = have_saved_pos ? "LOGGED-OFF" : "none";
			if (coop_recovery* rec = coop_find_recovery(client_name))
			{
				if (rec->persisted && coop_plausible_pos(rec->pos))
				{
					saved_pos = rec->pos;
					have_saved_pos = true;
					pos_source = "RECOVERY";
					// D2 run 2: the health rides with the position, out of the SAME record. It has
					// to come from one place — a recovery position paired with a freeze-time health
					// would describe a player who was somewhere else when that health was taken.
					if (rec->health > 0.f)
						saved_health = rec->health;
					// Consumed: from here on this player is live and the sampler owns their record.
					rec->persisted = false;
				}
			}
			// MP fork (§14 step 7 phase 4 D3.2 / doc §9.4): does this player need to be TOLD?
			// Both halves are load-bearing and neither is enough alone. RECOVERY says the last
			// process ended while they were still connected — but so does a CLEAN stop with
			// players online, which is not an interruption and not worth a word. The flag says
			// the process died — but that says nothing about THIS player, who may have logged
			// off politely an hour before it did and is being handed their logged-off position
			// exactly as they would be after any other stop. The conjunction, and only the
			// conjunction, means "your session was interrupted". This is the flag's whole job.
			const bool notify_crash_resume = m_coop_prev_crash && !xr_strcmp(pos_source, "RECOVERY");

			Msg("- COOP(resume): '%s' resumes at the %s position %.1f,%.1f,%.1f "
				"(previous process ended %s, checkpoint NOT consulted)",
				client_name, pos_source,
				have_saved_pos ? saved_pos.x : orphan->o_Position.x,
				have_saved_pos ? saved_pos.y : orphan->o_Position.y,
				have_saved_pos ? saved_pos.z : orphan->o_Position.z,
				m_coop_prev_crash ? "dirty" : "clean");

			// Clear orphan state and restore ownership
			orphan->m_coop_orphaned = false;
			orphan->owner = CL;
			CL->owner = orphan;
			orphan->set_name_replace(client_name);

			// MP fork (§14 step 7 phase 2 / §9.3): a body restored from a save carries the
			// position it was saved at; make sure that is what the returning player gets.
			// Belt and braces with coop_freeze_restored_bodies() — if anything overwrote the
			// CSE before we detached ownership, the .scop value still wins here, because the
			// packets sent below (Spawn_Write/UPDATE_Write) are what place the player.
			if (have_saved_pos && orphan->o_Position.distance_to(saved_pos) > 1.f)
			{
				Msg("- COOP(bindings): restoring saved position for '%s': %.1f,%.1f,%.1f "
					"(CSE had %.1f,%.1f,%.1f)", client_name,
					saved_pos.x, saved_pos.y, saved_pos.z,
					orphan->o_Position.x, orphan->o_Position.y, orphan->o_Position.z);
				orphan->o_Position = saved_pos;
			}

			// MP fork (§14 step 7 phase 4 D2, run 2): HAND OVER A LIVE BODY. The packets below
			// ship the CSE's health as well as its position, and a save-restored body's CSE is
			// overwritten by the server's own unplaced object while it waits for its owner —
			// leg 2 measured hp 0.00 alongside the -0.1,0.2,0.7 position. A client that receives
			// a dead actor is not merely cosmetically wrong: `CActor::net_Relevant()` is
			// `Local() & g_Alive()`, so it never exports an M_CL_UPDATE, so the server's CSE
			// never follows the player, so the recovery sampler persists the garbage — a silent
			// three-step failure downstream of one zero. Restore the recorded health for exactly
			// the reason the position is restored: this CSE is not to be trusted (P2 §3b rule 2).
			if (CSE_ALifeCreatureAbstract* body = smart_cast<CSE_ALifeCreatureAbstract*>(orphan))
			{
				const float cse_hp = body->get_health();
				LPCSTR hp_source = "RECORD";
				if (saved_health <= 0.f && cse_hp > 0.f)
				{
					saved_health = cse_hp;      // no record (v1/v2 sidecar): the CSE is all we have
					hp_source = "CSE";
				}
				if (saved_health <= 0.f)
				{
					// Neither source can say. Handing back a corpse is the worst answer available:
					// the player would be stuck dead with no death event to respawn them, which is
					// not a state §9 has a path out of. A real death goes through the death path
					// (P3 C3) and never arrives here.
					saved_health = 1.f;
					hp_source = "CLAMPED";
				}
				if (_abs(cse_hp - saved_health) > 0.01f)
					Msg("%s COOP(bindings): restoring saved health for '%s': %.2f (%s) — the CSE "
						"said %.2f", xr_strcmp(hp_source, "CLAMPED") ? "-" : "!",
						client_name, saved_health, hp_source, cse_hp);
				// The killer has to go with the zero. A body being handed back to a living player
				// is not a corpse, and `set_health` itself asserts the pair is impossible
				// (`VERIFY(!(killer != -1 && health > 0))`) — leaving a killer id on a revived
				// body means every death-state test downstream can still read it as dead, on both
				// sides of the wire. Clear it BEFORE the health, so the two are never inconsistent.
				if (saved_health > 0.f && body->get_killer_id() != u16(-1))
				{
					Msg("- COOP(bindings): clearing killer id %u on '%s' body — it is being handed "
						"back alive", body->get_killer_id(), client_name);
					body->set_killer_id(ALife::_OBJECT_ID(-1));
				}
				body->set_health(saved_health);
			}

			// Restore children ownership
			for (u16 child_id : orphan->children)
			{
				CSE_Abstract* child = m_server->ID_to_entity(child_id);
				if (child)
				{
					child->m_coop_orphaned = false;
					child->owner = CL;
				}
			}

			// Hand the body over as LOCAL+ASPLAYER so the client takes control of it.
			// (SendTo bypasses server event processing — only the client acts on it.)
			//
			// MP fork (§14 step 7 phase 4 D2): this used to send a GE_DESTROY first, to clear the
			// stripped copy the connection snapshot had given the client. That could not work: the
			// destroy is an M_EVENT (queued and executed by timestamp) while the spawn is applied on
			// receipt, and `setDestroy` only MARKS an object, so the spawn still sees it and the
			// duplicate-actor guard drops it — leaving the client with no body, nine
			// "GE_DESTROY ... has parent" errors, and a FATAL in `CAttachmentOwner::net_Destroy`
			// (measured 2026-07-26 on a save-restored reclaim). The fix is upstream:
			// `Perform_connect_spawn` now withholds a client's own orphaned body, so there is
			// nothing to destroy and one packet does the whole job. Anything else that could put a
			// copy on the client before the reclaim — today only the §3 per-client relevance pass,
			// which is inert unless -coop_cull_radius is set — would need the same treatment.
			{
				// Send the actor as LOCAL+ASPLAYER
				// D2 run 4: the server said it restored hp 1.00 and the client read 0.00 off the
				// spawn. Log what the CSE holds at the instant the packet is written — with the
				// client's matching wire_hp line this brackets the wire and names the side that
				// wrote the zero, instead of another round of reasoning about who could have.
				if (CSE_ALifeCreatureAbstract* snd = smart_cast<CSE_ALifeCreatureAbstract*>(orphan))
					Msg("- COOP(bindings): sending body id %u to '%s' — CSE hp %.2f killer %u pos "
						"%.1f,%.1f,%.1f", orphan->ID, client_name, snd->get_health(),
						snd->get_killer_id(), orphan->o_Position.x, orphan->o_Position.y,
						orphan->o_Position.z);

				NET_Packet P2;
				Flags16 save = orphan->s_flags;
				orphan->s_flags.set(M_SPAWN_UPDATE, TRUE);
				orphan->s_flags.set(M_SPAWN_OBJECT_ASPLAYER, TRUE);
				orphan->Spawn_Write(P2, TRUE); // TRUE = LOCAL
				orphan->UPDATE_Write(P2);
				orphan->s_flags = save;

				// D2 run 5: the bracket says the packet left with hp 0.00 AND pos 0,0,0, while the
				// id and flags — read AFTER the position out of the same header — parsed perfectly.
				// So those bytes really were zero when Spawn_Write ran, microseconds after this same
				// field logged -220.6,27.9,253.9. Nothing on this thread runs in between, which
				// leaves the ENet PUMP THREAD: `CL->owner` was pointed at this body a few lines
				// above, and the M_CL_UPDATE peek assigns `CL->owner->o_Position` from whatever a
				// client packet carries (xrServer.cpp, §15 — the D1 threading finding, one field
				// further on). Re-read the CSE after the write: if it disagrees with the pre-write
				// line, the race is proven and the packet must be built from a local copy.
				if (CSE_ALifeCreatureAbstract* aft = smart_cast<CSE_ALifeCreatureAbstract*>(orphan))
					Msg("- COOP(bindings): body id %u AFTER the write — CSE hp %.2f pos %.1f,%.1f,%.1f"
						" (packet %u bytes)", orphan->ID, aft->get_health(),
						orphan->o_Position.x, orphan->o_Position.y, orphan->o_Position.z,
						P2.B.count);

				m_server->SendTo(CL->ID, P2, net_flags(TRUE, TRUE));

				// Children (inventory items) as LOCAL — same story as the actor above: they were
				// withheld at connect, so they are spawned here rather than destroyed and re-sent.
				// Their GE_DESTROYs were the ones the client refused outright, because a child that
				// still has a parent is never destroyed by the stock handler (GameObject.cpp).
				for (u16 child_id : orphan->children)
				{
					CSE_Abstract* child = m_server->ID_to_entity(child_id);
					if (!child) continue;

					NET_Packet Ps;
					Flags16 csave = child->s_flags;
					child->s_flags.set(M_SPAWN_UPDATE, TRUE);
					child->Spawn_Write(Ps, TRUE);
					child->UPDATE_Write(Ps);
					child->s_flags = csave;
					m_server->SendTo(CL->ID, Ps, net_flags(TRUE, TRUE));
				}
			}

			// Replay other players' actors to this client (late-join snapshot)
			struct peer_replay
			{
				game_sv_Single* self;
				xrClientData* target;
				void operator()(IClient* client)
				{
					xrClientData* other = static_cast<xrClientData*>(client);
					if (other == target) return;
					if (other == self->m_server->GetServerClient()) return;
					if (!other->owner) return;
					CSE_Abstract* peer = other->owner;
					NET_Packet Packet;
					peer->Spawn_Write(Packet, FALSE); // stripped => remote
					self->m_server->SendTo(target->ID, Packet, net_flags(TRUE, TRUE));
				}
			};
			peer_replay pr; pr.self = this; pr.target = CL;
			m_server->ForEachClientDo(pr);

			Msg("- XRNET(dbg): co-op player '%s' RECONNECTED -> actor id %u restored "
				"(pos %.1f,%.1f,%.1f, %d inventory items)",
				client_name, orphan->ID,
				orphan->o_Position.x, orphan->o_Position.y, orphan->o_Position.z,
				(int)orphan->children.size());

			// D3.2: last, and only now — the body is on its way, so the notice cannot arrive
			// describing a resume the client has not been given yet.
			if (notify_crash_resume)
				coop_send_notice(CL, COOP_NOTICE_CRASH_RESUME,
					"The server's last session ended unexpectedly. You have been resumed where "
					"you were, not at your last checkpoint.");
		}
		else
		{
			coop_spawn_actor_for(CL);
		}

	}

	// MP fork (§19 co-op): push the server's quest list to any newly connected clients.
	// coop_broadcast_tasks normally fires only on task-list changes, but a connecting
	// client needs the existing list immediately.
	if (!c.pending.empty())
		Level().GameTaskManager().coop_broadcast_tasks();
}

BOOL game_sv_Single::OnTouch(u16 eid_who, u16 eid_what, BOOL bForced)
{
	CSE_Abstract* e_who = get_entity_from_eid(eid_who);
	VERIFY(e_who);
	CSE_Abstract* e_what = get_entity_from_eid(eid_what);
	VERIFY(e_what);

	if (ai().get_alife())
	{
		CSE_ALifeInventoryItem* l_tpALifeInventoryItem = smart_cast<CSE_ALifeInventoryItem*>(e_what);
		CSE_ALifeDynamicObject* l_tpDynamicObject = smart_cast<CSE_ALifeDynamicObject*>(e_who);

		if (
			l_tpALifeInventoryItem &&
			l_tpDynamicObject &&
			ai().alife().graph().level().object(l_tpALifeInventoryItem->base()->ID, true) &&
			ai().alife().objects().object(e_who->ID, true) &&
			ai().alife().objects().object(e_what->ID, true)
		)
			alife().graph().attach(*e_who, l_tpALifeInventoryItem, l_tpDynamicObject->m_tGraphID, false, false);
#ifdef DEBUG
		else
			if (psAI_Flags.test(aiALife)) {
				Msg				("Cannot attach object [%s][%s][%d] to object [%s][%s][%d]",e_what->name_replace(),*e_what->s_name,e_what->ID,e_who->name_replace(),*e_who->s_name,e_who->ID);
			}
#endif
	}
	return TRUE;
}

void game_sv_Single::OnDetach(u16 eid_who, u16 eid_what)
{
	if (ai().get_alife())
	{
		CSE_Abstract* e_who = get_entity_from_eid(eid_who);
		VERIFY(e_who);
		CSE_Abstract* e_what = get_entity_from_eid(eid_what);
		VERIFY(e_what);

		CSE_ALifeInventoryItem* l_tpALifeInventoryItem = smart_cast<CSE_ALifeInventoryItem*>(e_what);
		if (!l_tpALifeInventoryItem)
			return;

		CSE_ALifeDynamicObject* l_tpDynamicObject = smart_cast<CSE_ALifeDynamicObject*>(e_who);
		if (!l_tpDynamicObject)
			return;

		if (
			ai().alife().objects().object(e_who->ID, true) &&
			!ai().alife().graph().level().object(l_tpALifeInventoryItem->base()->ID, true) &&
			ai().alife().objects().object(e_what->ID, true)
		)
			alife().graph().detach(*e_who, l_tpALifeInventoryItem, l_tpDynamicObject->m_tGraphID, false, false);
		else
		{
			if (!ai().alife().objects().object(e_what->ID, true))
			{
				u16 id = l_tpALifeInventoryItem->base()->ID_Parent;
				l_tpALifeInventoryItem->base()->ID_Parent = 0xffff;

				CSE_ALifeDynamicObject* dynamic_object = smart_cast<CSE_ALifeDynamicObject*>(e_what);
				VERIFY(dynamic_object);
				dynamic_object->m_tNodeID = l_tpDynamicObject->m_tNodeID;
				dynamic_object->m_tGraphID = l_tpDynamicObject->m_tGraphID;
				dynamic_object->m_bALifeControl = true;
				dynamic_object->m_bOnline = true;
				alife().create(dynamic_object);
				l_tpALifeInventoryItem->base()->ID_Parent = id;
			}
#ifdef DEBUG
			else
				if (psAI_Flags.test(aiALife)) {
					Msg			("Cannot detach object [%s][%s][%d] from object [%s][%s][%d]",l_tpALifeInventoryItem->base()->name_replace(),*l_tpALifeInventoryItem->base()->s_name,l_tpALifeInventoryItem->base()->ID,l_tpDynamicObject->base()->name_replace(),l_tpDynamicObject->base()->s_name,l_tpDynamicObject->ID);
				}
#endif
		}
	}
}


// MP fork (§15 co-op A-Life): feed each connected player's position into the A-Life
// attention-anchor registry so online/offline switching centres on the REAL players
// (min distance to any anchor) instead of the static save-actor. Without this, NPCs
// and monsters only spawn/despawn around the save-actor's fixed spot and never follow
// the players. Rebuilt each tick (players move / join / leave). Anchors already wire
// into alife_dynamic_object / alife_online_offline_group switching via
// mp_anchors::min_distance_to().
void game_sv_Single::coop_update_anchors()
{
	if (!xr_enet::enabled() || !ai().get_alife())
		return; // co-op (ENet) only; stock single-player uses the actor entity

	struct anchor_feeder
	{
		u32 idx;
		game_sv_Single* self;
		void operator()(IClient* client)
		{
			xrClientData* CL = static_cast<xrClientData*>(client);
			if (CL == self->m_server->GetServerClient()) return; // no player on the server
			if (!CL->owner) return;                              // client without an actor yet
			if (idx >= mp_anchors::gamedata_anchor_base) return; // reserve [base, max) for gamedata anchors
			mp_anchors::set(idx, CL->owner->o_Position);
			++idx;
		}
	};

	// MP fork (§4): clear only the per-actor range so PERSISTENT gamedata anchors ([base, max), e.g. a
	// populated smart pinned online for the decision system) survive across frames.
	mp_anchors::clear_below(mp_anchors::gamedata_anchor_base);
	anchor_feeder f; f.idx = 0; f.self = this;
	m_server->ForEachClientDo(f);
}

void game_sv_Single::Update()
{
	inherited::Update();
	coop_poll_spawns();    // MP fork (§14 co-op): give ready clients their own actor + reconnection
	coop_update_anchors(); // MP fork (§15 co-op): re-centre A-Life on the players
	// MP fork (§14 step 7 phase 4 D2): an operator/harness stop request. Does not return if one
	// is pending — the clean stop flushes the world and exits from inside it.
	coop_check_stop_request();

	// MP fork (§9.4/9.5 co-op save/load — step 7 phase 1): server-authoritative autosave.
	// Interval (Appendix B) via -coop_autosave <seconds> (default 120s; 0 disables). The
	// harness fast-path -coop_test_autosave <seconds> overrides it for a short-interval E2E.
	if (xr_enet::enabled() && ai().get_alife())
	{
		if (!m_coop_autosave_init)
		{
			m_coop_autosave_init = true;
			const float MIN_SECS = 1.f, MAX_SECS = 86400.f; // clamp: [1s, 24h]; 0 (autosave-only) disables
			float secs = 120.f; // default: crash-loss window ~2 min
			bool have_test = false, have_arg = false;
			LPCSTR p = strstr(Core.Params, "-coop_test_autosave");
			if (p) { p += sizeof("-coop_test_autosave") - 1; while (*p == ' ') ++p; secs = (float)atof(p); have_test = true; }
			else if ((p = strstr(Core.Params, "-coop_autosave")) != nullptr)
			{ p += sizeof("-coop_autosave") - 1; while (*p == ' ') ++p; secs = (float)atof(p); have_arg = true; }
			// Sanitize atof output: NaN/inf and out-of-range values would make secs*1000 an
			// undefined u32 narrowing (CodeRabbit). NaN fails every compare, so >=0 catches it.
			if (!(secs >= 0.f)) secs = have_test ? MIN_SECS : 0.f;         // non-finite -> safe default
			else if ((have_arg || have_test) && secs > 0.f)               // an explicit positive value...
				secs = _min(_max(secs, MIN_SECS), MAX_SECS);              // ...clamped to [1s, 24h]
			// (have_arg with secs==0 means the operator explicitly disabled autosave -> keep 0)
			m_coop_autosave_interval_ms = (secs <= 0.f) ? 0u : (u32)(secs * 1000.f);
			m_coop_autosave_last = Device.dwTimeGlobal; // first save one interval from now
			Msg("- COOP(autosave): interval=%ums (%s)", m_coop_autosave_interval_ms,
				m_coop_autosave_interval_ms ? "enabled" : "disabled");
		}
		if (m_coop_autosave_interval_ms &&
		    Device.dwTimeGlobal - m_coop_autosave_last >= m_coop_autosave_interval_ms)
		{
			m_coop_autosave_last = Device.dwTimeGlobal;
			coop_autosave();
		}
	}

	// MP fork (§14 step 7 phase 3, test harness): -coop_test_checkpoint <seconds> banks a
	// checkpoint for every connected player once, N seconds in. Gamedata drives the real
	// thing via game.mp_set_checkpoint(name) at a campfire/base; this is just so the
	// headless acceptance test can bank one without a scripted trigger.
	if (xr_enet::enabled() && ai().get_alife())
	{
		if (!m_coop_test_checkpoint_init)
		{
			m_coop_test_checkpoint_init = true;
			LPCSTR p = strstr(Core.Params, "-coop_test_checkpoint");
			if (p)
			{
				p += sizeof("-coop_test_checkpoint") - 1;
				while (*p == ' ') ++p;
				const float secs = (float)atof(p);
				// NaN/inf fail every compare, so >0 also rejects non-finite input
				m_coop_test_checkpoint_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 30000u;
				// Measure the delay from HERE, not from engine start: this flag is parsed on
				// the first co-op Update, minutes into a dedicated boot, so comparing against
				// an absolute Device.dwTimeGlobal made every value fire instantly. (CodeRabbit)
				m_coop_test_checkpoint_armed = Device.dwTimeGlobal;
				Msg("- COOP(checkpoint): test auto-bank armed, firing in %ums", m_coop_test_checkpoint_ms);
			}
		}
		if (m_coop_test_checkpoint_ms && !m_coop_test_checkpoint_done &&
		    Device.dwTimeGlobal - m_coop_test_checkpoint_armed >= m_coop_test_checkpoint_ms &&
		    Device.dwTimeGlobal - m_coop_test_checkpoint_retry >= 5000)
		{
			// Retry every 5s (not every frame) until a player actually has an actor, so a
			// failing bank cannot flood the log with one '!' line per frame. (CodeRabbit)
			m_coop_test_checkpoint_retry = Device.dwTimeGlobal;
			struct banker
			{
				game_sv_Single* self;
				u32 banked;
				void operator()(IClient* client)
				{
					xrClientData* cd = static_cast<xrClientData*>(client);
					if (cd == self->m_server->GetServerClient()) return;
					if (!cd->owner) return;
					LPCSTR nm = coop_player_name(cd);
					if (nm && self->coop_bank_checkpoint(nm)) ++banked;
				}
			};
			banker b; b.self = this; b.banked = 0;
			m_server->ForEachClientDo(b);
			if (b.banked)
			{
				m_coop_test_checkpoint_done = true;   // one-shot, only once someone was banked
				Msg("- COOP(checkpoint): test auto-bank done (%u player(s))", b.banked);
				coop_test_drop_world_item();          // harness: §9.2 negative case (see header)
			}
		}
	}

	// MP fork (test harness): -coop_test_quest creates a synthetic task after 10s,
	// triggering quest replication to connected clients. For automated E2E testing.
	if (xr_enet::enabled() && ai().get_alife())
	{
		static bool s_test_init = false;
		static bool s_test_done = false;
		static u32  s_test_start = 0;
		if (!s_test_init)
		{
			s_test_init = true;
			if (strstr(Core.Params, "-coop_test_quest"))
				s_test_start = Device.dwTimeGlobal;
		}
		if (s_test_start && !s_test_done && Device.dwTimeGlobal - s_test_start > 10000)
		{
			s_test_done = true;
			CGameTask* task = xr_new<CGameTask>();
			task->m_ID = "coop_test_quest_e2e";
			task->m_Title = "Test Quest";
			task->SetTaskState(eTaskStateInProgress);
			task->m_ReceiveTime = GetGameTime();
			Level().GameTaskManager().GiveGameTaskToActor(task, 0, false, 0);
			// Force immediate broadcast — don't rely on UpdateTasks eChanged path
			Level().GameTaskManager().coop_broadcast_tasks();
			Msg("- COOP_TEST_QUEST: synthetic task 'coop_test_quest_e2e' created, broadcast forced");
		}
	}

	// MP fork (test harness): -coop_test_decision broadcasts a synthetic scheduled decision
	// once a client is actually connected, proving the M_XRNET_DECISION wire + shared-clock
	// scheduling end to end. The client should log COOP_DECISION_CL (queued) then
	// COOP_DECISION_EXEC with a small delta. We RETRY every 5s and stop only once the decision
	// reached a REAL remote player (coop_broadcast_decision returns clients whose owner->ID != 0):
	// a one-shot fire would hit the dedicated server's own loopback self-client (the fake host
	// actor, id 0) before the real client connects, and never reach it.
	// For automated E2E testing (dev/harness/test_coop_decision.sh).
	if (xr_enet::enabled() && strstr(Core.Params, "-coop_test_decision"))
	{
		static bool s_dec_done     = false;
		static u32  s_dec_last_try = 0;
		if (!s_dec_done && Device.dwTimeGlobal - s_dec_last_try > 5000)
		{
			s_dec_last_try = Device.dwTimeGlobal;
			const u32 magic = 0xDEC15100;                    // payload the client can integrity-check
			const u32 lead  = 1000;                          // 1s LEAD so scheduling is observable on loopback
			const u32 n = Level().coop_broadcast_decision(/*subject*/ 0xC0DE, /*kind*/ 200,
			                                               &magic, u16(sizeof(magic)), lead);
			Msg("- COOP_TEST_DECISION: broadcast attempt (subject=0xC0DE kind=200 lead=%ums) -> %u client(s)", lead, n);
			if (n > 0)
				s_dec_done = true;
		}
	}
	/*	switch(phase) 	{
			case GAME_PHASE_PENDING : {
				OnRoundStart();
				switch_Phase(GAME_PHASE_INPROGRESS);
				break;
			}
		}*/
}

ALife::_TIME_ID game_sv_Single::GetStartGameTime()
{
	if (ai().get_alife() && ai().alife().initialized())
		return (ai().alife().time_manager().start_game_time());
	else
		return (inherited::GetStartGameTime());
}

ALife::_TIME_ID game_sv_Single::GetGameTime()
{
	if (ai().get_alife() && ai().alife().initialized())
		return (ai().alife().time_manager().game_time());
	else
		return (inherited::GetGameTime());
}

float game_sv_Single::GetGameTimeFactor()
{
	if (ai().get_alife() && ai().alife().initialized())
		return (ai().alife().time_manager().time_factor());
	else
		return (inherited::GetGameTimeFactor());
}

void game_sv_Single::SetGameTimeFactor(const float fTimeFactor)
{
	if (ai().get_alife() && ai().alife().initialized())
		return (alife().time_manager().set_time_factor(fTimeFactor));
	else
		return (inherited::SetGameTimeFactor(fTimeFactor));
}

ALife::_TIME_ID game_sv_Single::GetEnvironmentGameTime()
{
	if (ai().get_alife() && ai().alife().initialized())
		return (alife().time_manager().game_time());
	else
		return (inherited::GetGameTime());
}

float game_sv_Single::GetEnvironmentGameTimeFactor()
{
	return (inherited::GetGameTimeFactor());
}

void game_sv_Single::SetEnvironmentGameTimeFactor(const float fTimeFactor)
{
	return (inherited::SetGameTimeFactor(fTimeFactor));
}

bool game_sv_Single::change_level(NET_Packet& net_packet, ClientID sender)
{
	if (ai().get_alife())
		return (alife().change_level(net_packet));
	else
		return (true);
}

void game_sv_Single::save_game(NET_Packet& net_packet, ClientID sender)
{
	if (!ai().get_alife())
		return;

	alife().save(net_packet);
}

bool game_sv_Single::load_game(NET_Packet& net_packet, ClientID sender)
{
	if (!ai().get_alife())
		return (inherited::load_game(net_packet, sender));
	shared_str game_name;
	net_packet.r_stringZ(game_name);
	return (alife().load_game(*game_name, true));
}

void game_sv_Single::reload_game(NET_Packet& net_packet, ClientID sender)
{
}

void game_sv_Single::switch_distance(NET_Packet& net_packet, ClientID sender)
{
	if (!ai().get_alife())
		return;

	alife().set_switch_distance(net_packet.r_float());
}

void game_sv_Single::teleport_object(NET_Packet& net_packet, u16 id)
{
	if (!ai().get_alife())
		return;

	GameGraph::_GRAPH_ID game_vertex_id;
	u32 level_vertex_id;
	Fvector position;

	net_packet.r(&game_vertex_id, sizeof(game_vertex_id));
	net_packet.r(&level_vertex_id, sizeof(level_vertex_id));
	net_packet.r_vec3(position);

	alife().teleport_object(id, game_vertex_id, level_vertex_id, position);
}

void game_sv_Single::add_restriction(NET_Packet& packet, u16 id)
{
	if (!ai().get_alife())
		return;

	ALife::_OBJECT_ID restriction_id;
	packet.r(&restriction_id, sizeof(restriction_id));

	RestrictionSpace::ERestrictorTypes restriction_type;
	packet.r(&restriction_type, sizeof(restriction_type));

	alife().add_restriction(id, restriction_id, restriction_type);
}

void game_sv_Single::remove_restriction(NET_Packet& packet, u16 id)
{
	if (!ai().get_alife())
		return;

	ALife::_OBJECT_ID restriction_id;
	packet.r(&restriction_id, sizeof(restriction_id));

	RestrictionSpace::ERestrictorTypes restriction_type;
	packet.r(&restriction_type, sizeof(restriction_type));

	alife().remove_restriction(id, restriction_id, restriction_type);
}

void game_sv_Single::remove_all_restrictions(NET_Packet& packet, u16 id)
{
	if (!ai().get_alife())
		return;

	RestrictionSpace::ERestrictorTypes restriction_type;
	packet.r(&restriction_type, sizeof(restriction_type));

	alife().remove_all_restrictions(id, restriction_type);
}

void game_sv_Single::sls_default()
{
	alife().update_switch();
}

shared_str game_sv_Single::level_name(const shared_str& server_options) const
{
	if (!ai().get_alife())
		return (inherited::level_name(server_options));
	return (alife().level_name());
}

void game_sv_Single::on_death(CSE_Abstract* e_dest, CSE_Abstract* e_src)
{
	inherited::on_death(e_dest, e_src);

	if (!ai().get_alife())
		return;

	alife().on_death(e_dest, e_src);
}

void game_sv_Single::restart_simulator(LPCSTR saved_game_name)
{
	shared_str& options = *alife().server_command_line();

	delete_data(m_alife_simulator);
	server().clear_ids();

	xr_strcpy(g_pGamePersistent->m_game_params.m_game_or_spawn, saved_game_name);
	xr_strcpy(g_pGamePersistent->m_game_params.m_new_or_load, "load");

	pApp->ls_header[0] = '\0';
	pApp->ls_tip_number[0] = '\0';
	pApp->ls_tip[0] = '\0';
	pApp->LoadBegin();
	m_alife_simulator = xr_new<CALifeSimulator>(&server(), &options);
	//	g_pGamePersistent->LoadTitle		("st_client_synchronising");
	g_pGamePersistent->LoadTitle();
	Device.PreCache(60, true, true);
	pApp->LoadEnd();
}
