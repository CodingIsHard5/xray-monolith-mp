#include "stdafx.h"
#include "game_sv_single.h"
#include "xrserver_objects_alife_monsters.h"
#include "xrServer_Objects_ALife_Items.h"          // MP fork (§20): CSE_ALifeItemWeapon (kit mag pre-load)
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "alife_graph_registry.h"
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

game_sv_Single::game_sv_Single()
{
	m_alife_simulator = NULL;
	m_type = eGameIDSingle;
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
// MP fork (§9.3/9.4 co-op reconnection): orphan the actor when a co-op client disconnects.
// The entity stays in the world (alive, at its last position) but has no owning client.
// If the same player name reconnects within the timeout, the actor is re-associated.
void game_sv_Single::coop_orphan_actor(xrClientData* CL)
{
	if (!CL || !CL->owner)
		return;

	CSE_Abstract* actor = CL->owner;
	coop_orphan orphan;
	orphan.entity_id = actor->ID;
	orphan.player_name = CL->ps ? CL->ps->getName() : CL->name;
	orphan.disconnect_time = Device.dwTimeGlobal;

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
}

void game_sv_Single::OnCoopClientDisconnected(xrClientData* CL)
{
	if (!xr_enet::enabled())
		return;
	coop_orphan_actor(CL);
	// Clean up the grace-period entry so a reconnecting client gets a fresh grace window
	m_coop_seen.erase(CL->ID.value());
}

CSE_Abstract* game_sv_Single::coop_find_orphan(LPCSTR name)
{
	for (auto it = m_coop_orphans.begin(); it != m_coop_orphans.end(); ++it)
	{
		if (!xr_strcmp(it->player_name.c_str(), name))
		{
			u16 eid = it->entity_id;
			m_coop_orphans.erase(it);
			CSE_Abstract* entity = m_server->ID_to_entity(eid);
			if (entity)
			{
				Msg("- XRNET(dbg): co-op orphan matched for player '%s' -> entity id %u", name, eid);
				return entity;
			}
			Msg("! XRNET(dbg): co-op orphan entity id %u no longer exists for player '%s'", eid, name);
			return NULL;
		}
	}
	return NULL;
}

void game_sv_Single::coop_cleanup_orphans()
{
	if (m_coop_orphans.empty())
		return;

	const u32 now = Device.dwTimeGlobal;
	for (auto it = m_coop_orphans.begin(); it != m_coop_orphans.end(); )
	{
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

void game_sv_Single::coop_poll_spawns()
{
	if (!xr_enet::enabled() || !ai().get_alife())
		return; // co-op (ENet) only; stock single-player untouched

	// Expire orphaned actors past the reconnect timeout
	coop_cleanup_orphans();

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
		LPCSTR client_name = CL->ps ? CL->ps->getName() : CL->name.c_str();
		CSE_Abstract* orphan = coop_find_orphan(client_name);
		if (orphan)
		{
			// Clear orphan state and restore ownership
			orphan->m_coop_orphaned = false;
			orphan->owner = CL;
			CL->owner = orphan;
			orphan->set_name_replace(client_name);

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

			// The reconnecting client already received this entity as stripped/remote
			// during SendConnectionData. Destroy that client-side representation and
			// re-send as LOCAL+ASPLAYER so the client takes control of it.
			// (SendTo bypasses server event processing — only the client acts on it.)
			{
				// Destroy the stripped entity on the reconnecting client
				NET_Packet P;
				P.w_begin(M_EVENT);
				P.w_u32(Level().timeServer());
				P.w_u16(GE_DESTROY);
				P.w_u16(orphan->ID);
				m_server->SendTo(CL->ID, P, net_flags(TRUE, TRUE));

				// Re-send the actor as LOCAL+ASPLAYER
				NET_Packet P2;
				Flags16 save = orphan->s_flags;
				orphan->s_flags.set(M_SPAWN_UPDATE, TRUE);
				orphan->s_flags.set(M_SPAWN_OBJECT_ASPLAYER, TRUE);
				orphan->Spawn_Write(P2, TRUE); // TRUE = LOCAL
				orphan->UPDATE_Write(P2);
				orphan->s_flags = save;
				m_server->SendTo(CL->ID, P2, net_flags(TRUE, TRUE));

				// Re-send children (inventory items) as LOCAL
				for (u16 child_id : orphan->children)
				{
					CSE_Abstract* child = m_server->ID_to_entity(child_id);
					if (!child) continue;

					// Destroy stripped child on client
					NET_Packet Pd;
					Pd.w_begin(M_EVENT);
					Pd.w_u32(Level().timeServer());
					Pd.w_u16(GE_DESTROY);
					Pd.w_u16(child->ID);
					m_server->SendTo(CL->ID, Pd, net_flags(TRUE, TRUE));

					// Re-send as LOCAL
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
