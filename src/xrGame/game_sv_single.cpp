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
#include "mp_coop_owner.h"                         // MP fork (§14 step 8 P1 / §6): the two ownership tiers
#include "InventoryOwner.h"                        // MP fork (§14 step 8 Q4): HasInfo / CharacterInfo
#include "character_info.h"                        // MP fork (§14 step 8 Q4): the player's community
#include "Actor.h"                                 // MP fork (§14 step 8 Q4): tell a player actor apart
#include "entity_alive.h"                          // MP fork (§14 step 8 Q4): only talk to the living
#include "ai/stalker/ai_stalker.h"                 // MP fork (§14 step 8 P4 R3.0): the stock KILL path needs a stalker victim
#include "relation_registry.h"                    // MP fork (§14 step 8 P4 R1): goodwill storage
#include "character_community.h"                  // MP fork (§14 step 8 P4 R1): faction indices
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

// MP fork (§14 step 8 phase 2): a command-line flag lookup that will not match a LONGER flag.
// The codebase's usual `strstr(Core.Params, "-coop_x")` matches a PREFIX, so "-coop_test_rpg"
// also matches inside "-coop_test_rpg2" and "-coop_test_rpg_verify" — passing only the phase-2
// flag would silently arm the phase-1 probe as well, with "2" parsed as its interval. Require a
// delimiter after the flag, and keep scanning so a genuine later occurrence still wins.
// Returns the first argument character (past any spaces), or NULL if the flag is absent.
static LPCSTR coop_param(LPCSTR flag)
{
	const size_t n = xr_strlen(flag);
	LPCSTR p = Core.Params;
	while ((p = strstr(p, flag)) != NULL)
	{
		LPCSTR after = p + n;
		if (!*after || *after == ' ' || *after == '\t')
		{
			while (*after == ' ' || *after == '\t') ++after;
			return after;
		}
		p += n;
	}
	return NULL;
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

// MP fork (§14 step 8 phase 1 / doc §6): the two ownership tiers. See mp_coop_owner.h for why
// the world tier is the base actor entity rather than a spawned stand-in.
namespace mp_coop_owner
{
	// Not a member of game_sv_Single: the READERS live on both sides of the server/client
	// split (GametaskManager, the Lua export), and a client legitimately has no server game.
	// A file-scope value that reads `none` everywhere but the co-op server is the honest shape.
	static u16 s_acting_actor = mp_coop_owner::none;

	u16 world_actor()
	{
		if (!ai().get_alife())
			return none;                       // a client has no A-Life: there is no world tier here

		return world_key;
	}

	u16 acting_actor() { return s_acting_actor; }

	acting_scope::acting_scope(u16 actor_id) : m_prev(s_acting_actor) { s_acting_actor = actor_id; }
	acting_scope::~acting_scope() { s_acting_actor = m_prev; }
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
	m_coop_test_rpg_ms = 0;
	m_coop_test_rpg_armed = 0;
	m_coop_test_rpg_retry = 0;
	m_coop_test_rpg_init = false;
	m_coop_test_rpg_done = false;
	m_coop_test_rpg_verify_only = false;
	m_coop_world_key_checked = false;
	m_coop_census_ms = 0;
	m_coop_census_last = 0;
	m_coop_census_init = false;
	m_coop_test_rpg2_ms = 0;
	m_coop_test_quest6_ms = 0;
	m_coop_test_quest6_init = false;
	m_coop_test_quest6_done = false;
	m_coop_test_quest6_armed = 0;
	m_coop_test_quest6_retry = 0;
	m_coop_test_rpg2_armed = 0;
	m_coop_test_rpg2_retry = 0;
	m_coop_test_rpg2_init = false;
	m_coop_test_rpg2_done = false;
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
	// MP fork (§14 step 7 phase 4 D3.3 / doc §9.4): how long ago this player was last hurt, as
	// of right now. Stamped here because this is the last moment both halves exist together: the
	// client data goes away with the connection, and the body that stays behind cannot say when
	// it was damaged. See coop_orphan::damage_age_ms for why it goes no further than memory.
	orphan.damage_age_ms = CL->m_coop_last_damage_time
		? (Device.dwTimeGlobal - CL->m_coop_last_damage_time)
		: COOP_NO_DAMAGE;

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
                                               float* out_health, u32* out_damage_age_ms)
{
	if (out_have_pos)
		*out_have_pos = false;
	if (out_health)
		*out_health = -1.f;   // "no recorded health"; the caller falls through to its next source
	if (out_damage_age_ms)
		*out_damage_age_ms = COOP_NO_DAMAGE;

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
			if (out_damage_age_ms)
				*out_damage_age_ms = it->damage_age_ms;
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
		// D3.3: a body restored from a save carries no damage history — the process that could
		// have observed it is gone, and the sidecar deliberately does not record it. "Unknown"
		// is the truthful value, and it reads the same as "was never hurt": no log line.
		o.damage_age_ms = COOP_NO_DAMAGE;
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

// ============================================================================================
// MP fork (§14 step 8 phase 3 Q4 / doc §7.3): the item side of a dialogue-routed TURN-IN.
//
// §7.3's fetch/deliver shape is the one completion condition that is not a world-state change the
// server already observes. Nothing happens in the world when a player is carrying the right items;
// the completion IS the hand-over, and the hand-over is an act of dialogue. So this is where the
// increment needed a hot-path hook on item ownership rather than a reuse of an existing one — and
// the whole reason it goes here is that ONLY the server may answer "what is this player carrying".
// A turn-in that trusted the asking client would be an item-duplication exploit with a dialogue
// box in front of it: claim three medkits you do not have, keep the ones you do.
// ============================================================================================
u32 game_sv_Single::coop_count_carried(u16 owner_id, const shared_str& section)
{
	if (!m_server || !section.size() || owner_id == u16(-1))
		return 0;
	CSE_Abstract* const owner = m_server->ID_to_entity(owner_id);
	if (!owner)
		return 0;

	u32 n = 0;
	for (u16 child_id : owner->children)
	{
		// The SERVER ID map only, deliberately, unlike the checkpoint bank which also consults
		// A-Life for offline children. What is counted here must also be MOVABLE — an item this
		// map cannot resolve cannot be handed to anybody, and counting it would turn a refusal
		// ("you do not have them") into a failure after the fact ("we could not move them"),
		// which is a strictly worse answer to the same question.
		CSE_Abstract* const c = m_server->ID_to_entity(child_id);
		if (!c || !c->s_name.size())
			continue;
		if (!xr_strcmp(c->s_name.c_str(), section.c_str()))
			++n;
	}
	return n;
}

u32 game_sv_Single::coop_hand_over(u16 from_id, u16 to_id, const shared_str& section, u16 count)
{
	if (!m_server || !count || !section.size())
		return 0;

	CSE_Abstract* const from = m_server->ID_to_entity(from_id);
	CSE_Abstract* const to   = m_server->ID_to_entity(to_id);
	// Perform_transfer ASSERTS all three of these (what->ID_Parent == from->ID, from != to, and
	// non-null). An assert on a dedicated server takes the session down for everyone, so each one
	// is a checked refusal here rather than a trusted precondition.
	if (!from || !to || from == to)
	{
		Msg("! COOP(quest): hand-over %ux '%s' refused — from=%u(%p) to=%u(%p)",
			u32(count), section.c_str(), u32(from_id), from, u32(to_id), to);
		return 0;
	}

	xr_vector<CSE_Abstract*> take;
	for (u32 i = 0; i < from->children.size() && take.size() < u32(count); ++i)
	{
		CSE_Abstract* const c = m_server->ID_to_entity(from->children[i]);
		if (!c || !c->s_name.size())
			continue;
		if (xr_strcmp(c->s_name.c_str(), section.c_str()))
			continue;
		if (c->ID_Parent != from->ID)
			continue;                       // the children list and the parent link disagree
		take.push_back(c);
	}
	if (take.size() < u32(count))
	{
		Msg("! COOP(quest): hand-over %ux '%s' refused — only %u movable on %u; NOTHING moved",
			u32(count), section.c_str(), u32(take.size()), u32(from_id));
		return 0;
	}

	// The stock server-side transfer: re-parent the CSE and ship the reject/take pair to every
	// client in one M_EVENT_PACK, exactly as the deathmatch corpse-looting path does. The items
	// are not DESTROYED — they change hands, which is what a turn-in is and what keeps §7.2's
	// "same conservation logic as loot" true for the goods as well as for the task.
	NET_Packet EventPack, PacketReject, PacketTake;
	EventPack.w_begin(M_EVENT_PACK);
	for (u32 i = 0; i < take.size(); ++i)
	{
		m_server->Perform_transfer(PacketReject, PacketTake, take[i], from, to);
		EventPack.w_u8(u8(PacketReject.B.count));
		EventPack.w(&PacketReject.B.data, PacketReject.B.count);
		EventPack.w_u8(u8(PacketTake.B.count));
		EventPack.w(&PacketTake.B.data, PacketTake.B.count);
	}
	if (EventPack.B.count > 2)
		u_EventSend(EventPack);

	Msg("- COOP(quest): hand-over %u x '%s' from %u to %u (server tree re-parented, clients told)",
		u32(take.size()), section.c_str(), u32(from_id), u32(to_id));
	return u32(take.size());
}

u32 game_sv_Single::coop_grant_items(u16 owner_id, const shared_str& section, u16 count)
{
	if (!m_server || !count || !section.size())
		return 0;
	CSE_Abstract* const owner = m_server->ID_to_entity(owner_id);
	if (!owner || !owner->owner)
	{
		Msg("! COOP(quest-test): cannot grant %ux '%s' — entity %u is absent or unowned",
			u32(count), section.c_str(), u32(owner_id));
		return 0;
	}

	coop_checkpoint_item rec;
	rec.section      = section;
	rec.condition    = 1.f;
	rec.ammo_elapsed = 0;
	rec.ammo_type    = 0;
	rec.slot         = 0xff;

	u32 made = 0;
	for (u16 i = 0; i < count; ++i)
		if (coop_spawn_checkpoint_item(owner, owner->owner, rec))
			++made;
	Msg("- COOP(quest-test): granted %u of %u '%s' to %u", made, u32(count), section.c_str(),
		u32(owner_id));
	return made;
}

// The task layer's view of the two above: it knows about players and sections, not about
// IPureServer. Declared in GametaskManager.h.
namespace
{
	game_sv_Single* coop_server_game()
	{
		if (!g_pGameLevel || !Level().Server)
			return NULL;
		return smart_cast<game_sv_Single*>(Level().Server->game);
	}
}

u32 coop_items_count(u16 owner_id, const shared_str& section)
{
	game_sv_Single* const g = coop_server_game();
	return g ? g->coop_count_carried(owner_id, section) : 0u;
}

u32 coop_items_hand_over(u16 from_id, u16 to_id, const shared_str& section, u16 count)
{
	game_sv_Single* const g = coop_server_game();
	return g ? g->coop_hand_over(from_id, to_id, section, count) : 0u;
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
		u32 damage_age_ms = COOP_NO_DAMAGE;
		CSE_Abstract* orphan = coop_find_orphan(client_name, &saved_pos, &have_saved_pos,
		                                        &saved_health, &damage_age_ms);
		if (orphan)
		{
			// MP fork (§14 step 7 phase 4 D3.3 / doc §9.4) — MEASUREMENT, NOT A RULE. The one
			// abuse shape §9.4 names is pulling the plug in a losing fight, and the honest
			// reading today is that it gains nothing: a disconnect orphans the body IN THE WORLD
			// for the reconnect window (§9.3), the damage already landed, and a resume returns
			// the player exactly as hurt as they were. So this logs the shape and does nothing
			// about it. A punitive timer written on a hypothesis would be a real regression
			// (every dropped connection is indistinguishable from every rage-quit) — the rule,
			// if one is ever wanted, gets written on top of these lines and not before them.
			if (damage_age_ms <= COOP_POLICY_DAMAGE_WINDOW_MS)
				Msg("! COOP(policy): '%s' reconnected to a body that was taking damage %.1fs "
					"before the disconnect (window %us). Logged for §9.4 data only — no rule "
					"fires on this and the player is handed back exactly what they left.",
					client_name, float(damage_age_ms) / 1000.f,
					COOP_POLICY_DAMAGE_WINDOW_MS / 1000);

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
			// MP fork (§14 step 7 phase 4 D3.1 / doc §9.4) — NO EXPIRY, and that is a decision,
			// not an omission. A record is used however old it is: a server that crashed, sat
			// dead for a week and came back still hands the player back exactly where they were.
			// The alternative is to silently demote them to an older position, which is the very
			// class of surprise D2's negative gate exists to forbid, and the argument for it
			// ("the world has moved on") does not survive contact with the facts — the `.scop`
			// loaded beside this record is from the SAME instant, so the world and the player
			// are consistent with each other however long the gap was. If a bound is ever
			// wanted it belongs on REPORTING (D3.2's notice), never on placement.
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

// MP fork (§14 step 8 phase 1 / doc §6): one-shot boot check of the world tier's key.
//
// The key is reserved rather than borrowed from an entity because the first phase-1 run
// measured what borrowing costs: with the world tier read off ai().alife().graph().actor(),
// the harness reported world=22678 player=22678 — one entity wearing both hats. m_actor is
// re-pointed to ANY object spawned with M_SPAWN_OBJECT_ASPLAYER, and every co-op player actor
// carries that flag, so "the level's actor" silently becomes "the last player to spawn".
//
// A reserved key has exactly one failure mode — something else claiming it — so check for that
// instead of assuming it. Logged either way: a run that never prints this line did not check.
void game_sv_Single::coop_check_world_key()
{
	if (m_coop_world_key_checked)
		return;
	m_coop_world_key_checked = true;

	CSE_ALifeDynamicObject* const squatter =
		ai().alife().objects().object(mp_coop_owner::world_key, true);
	CSE_ALifeCreatureActor* const graph_actor = ai().alife().graph().actor();

	if (squatter)
		Msg("! COOP(rpg): world key %u is held by a live entity ('%s') — the world tier and that "
			"entity now share a registry row", u32(mp_coop_owner::world_key), squatter->name_replace());
	else
		Msg("- COOP(rpg): world tier = reserved registry key %u (free); graph actor is entity %u, "
			"and it follows the last spawned player — which is why the key is not read off it",
			u32(mp_coop_owner::world_key), graph_actor ? u32(graph_actor->ID) : 0xffffu);
}

// MP fork (§14 step 8 phase 1, harness): one call into gamedata's probe. Gamedata does the
// reading and writing because gamedata is where the 557 real call sites are — the point of the
// probe is to exercise the SAME routed primitives (has_alife_info / give_info / disable_info),
// not a private engine path that happens to agree with them.
void game_sv_Single::coop_rpg_probe_call(LPCSTR fn, LPCSTR phase, u16 world_id, u16 player_id)
{
	luabind::functor<void> f;
	if (!ai().script_engine().functor(fn, f))
	{
		Msg("! COOP(rpg): gamedata probe %s not registered (phase '%s')", fn, phase);
		return;
	}
	f(phase, u32(world_id), u32(player_id));
}

// MP fork (§14 step 8, harness): the entity id of the first connected client that has an actor.
// The server's own loopback self-client is not a player and a client that has connected but has
// not been given a body yet is not one either, so both are skipped and the caller retries.
// MP fork (§14 step 8 phase 3 Q3, harness): a task's state READ AT THIS INSTANT.
//
// It exists so the probes can sample a transition while it is happening. A completion condition is
// consumed by its own progress, so "the task was still in progress after two of three deaths" is
// not recoverable afterwards from anything — not from the condition (spent), not from the task
// (which by then says completed either way). Either it is sampled at the step or it is not
// measured at all. -1 = no such task.
static int coop_task_state_now(LPCSTR id)
{
	CGameTask* const t = Level().GameTaskManager().HasGameTask(shared_str(id), false);
	return t ? int(t->GetTaskState()) : -1;
}

// MP fork (§14 step 8 phase 3 Q4, harness): a live NPC that can be talked to and handed things.
//
// A dialogue needs a partner and a turn-in needs somebody to receive the goods, and both must be
// ONLINE on this server — the harness cannot invent one, and it must not silently substitute the
// player for one either, because a hand-over to yourself is not a hand-over. Returns
// mp_coop_owner::none if the world has nobody, which the probe reports rather than working around:
// "we could not find an NPC" and "the NPC refused" are different results and only one is a defect.
static u16 coop_find_npc(u16 except_id)
{
	if (!g_pGameLevel)
		return mp_coop_owner::none;
	u16 found = mp_coop_owner::none;
	u32 candidates = 0;
	for (u32 i = 0; i < Level().Objects.o_count(); ++i)
	{
		CObject* const o = Level().Objects.o_get_by_iterator(i);
		if (!o || o->getDestroy() || o->ID() == except_id)
			continue;
		if (smart_cast<CActor*>(o))
			continue;                      // a player, not somebody to talk to
		CInventoryOwner* const io = smart_cast<CInventoryOwner*>(o);
		CEntityAlive* const alive = smart_cast<CEntityAlive*>(o);
		if (!io || !alive || !alive->g_Alive())
			continue;
		++candidates;
		if (found == mp_coop_owner::none)
			found = o->ID();
	}
	Msg("- COOP(quest5): NPC search: %u live inventory owner(s) online, chose %u",
		candidates, u32(found));
	return found;
}

// Does this entity know this info portion? Read off the same store the dialogue's <give_info>
// writes to (CInventoryOwner), so the question and the answer cannot be about different places.
static bool coop_has_info(u16 id, LPCSTR info)
{
	if (!g_pGameLevel || id == mp_coop_owner::none || !info || !*info)
		return false;
	CObject* const o = Level().Objects.net_Find(id);
	CInventoryOwner* const io = o ? smart_cast<CInventoryOwner*>(o) : NULL;
	return io ? io->HasInfo(shared_str(info)) : false;
}

u16 game_sv_Single::coop_first_player_actor()
{
	struct finder
	{
		game_sv_Single* self;
		u16 player_id;
		void operator()(IClient* client)
		{
			if (player_id != mp_coop_owner::none) return;   // first one is enough
			xrClientData* cd = static_cast<xrClientData*>(client);
			if (cd == self->m_server->GetServerClient()) return;
			if (!cd->owner) return;
			player_id = cd->owner->ID;
		}
	};
	finder fd;
	fd.self = this;
	fd.player_id = mp_coop_owner::none;
	m_server->ForEachClientDo(fd);
	return fd.player_id;
}

void game_sv_Single::coop_all_player_actors(xr_vector<u16>& out)
{
	struct collector
	{
		game_sv_Single* self;
		xr_vector<u16>* out;
		void operator()(IClient* client)
		{
			xrClientData* cd = static_cast<xrClientData*>(client);
			if (!cd || !cd->owner) return;
			if (cd == self->m_server->GetServerClient()) return;
			out->push_back(cd->owner->ID);
		}
	};
	collector c;
	c.self = this;
	c.out = &out;
	m_server->ForEachClientDo(c);
}

// MP fork (§14 step 8 phase 1 / doc §6): drive the ownership-tier probe.
//
// The sequence is the test. The acting-player context is engine-owned and scoped, so the three
// calls measure three different things with one primitive:
//   autonomous -> no scope open: this is world simulation, and the write must land on the WORLD;
//   acting     -> inside a scope for a real connected player: the write must land on THAT player;
//   after      -> the scope has closed: it must read as world again. A context that leaked would
//                 pass the first two legs and quietly attribute every later world read to the
//                 last player who interacted with anything.
// Returns false while no connected client has an actor yet, so the caller retries.
bool game_sv_Single::coop_test_rpg_probe()
{
	const u16 world_id = mp_coop_owner::world_actor();
	if (world_id == mp_coop_owner::none)
	{
		Msg("! COOP(rpg): no world actor — A-Life has no base actor entity");
		return false;
	}

	const u16 player_id = coop_first_player_actor();
	if (player_id == mp_coop_owner::none)
		return false;   // nobody has an actor yet — retry

	// The tiers being DISTINCT is the first assertion, not a formality: if the world tier shared
	// a key with a player's actor, everything below would pass while proving nothing. `graph=` is
	// reported alongside because that is the value this assertion caught on its first run.
	CSE_ALifeCreatureActor* const graph_actor = ai().alife().graph().actor();
	Msg("- COOP(rpg): tiers world=%u player=%u distinct=%u graph=%u", u32(world_id), u32(player_id),
		u32(world_id != player_id ? 1 : 0), graph_actor ? u32(graph_actor->ID) : 0xffffu);

	if (!m_coop_test_rpg_verify_only)
	{
		coop_rpg_probe_call("_G.mp_coop_rpg_probe", "autonomous", world_id, player_id);
		{
			mp_coop_owner::acting_scope scope(player_id);
			coop_rpg_probe_call("_G.mp_coop_rpg_probe", "acting", world_id, player_id);
		}
		coop_rpg_probe_call("_G.mp_coop_rpg_probe", "after", world_id, player_id);
	}
	coop_rpg_probe_call("_G.mp_coop_rpg_probe", "verify", world_id, player_id);
	return true;
}

// MP fork (§14 step 8 phase 2 / doc §6.3): drive the bridge case and the "any player" default.
//
// Three facts are written inside ONE acting scope and differ only in their classification, so
// the scope cannot be what decides where they land — the classification has to be. Then the gate
// is read the way the world actually asks it: with no acting player, through the same global a
// smart terrain calls. A bridge wired to the TIER instead of the CLASSIFICATION promotes all
// three and passes every positive assertion in the run.
bool game_sv_Single::coop_test_rpg2_probe()
{
	const u16 world_id = mp_coop_owner::world_actor();
	if (world_id == mp_coop_owner::none)
	{
		Msg("! COOP(rpg2): no world tier");
		return false;
	}
	const u16 player_id = coop_first_player_actor();
	if (player_id == mp_coop_owner::none)
		return false;   // nobody has an actor yet — retry

	Msg("- COOP(rpg2): tiers world=%u player=%u distinct=%u", u32(world_id), u32(player_id),
		u32(world_id != player_id ? 1 : 0));

	coop_rpg_probe_call("_G.mp_coop_rpg_probe2", "setup", world_id, player_id);
	{
		mp_coop_owner::acting_scope scope(player_id);
		coop_rpg_probe_call("_G.mp_coop_rpg_probe2", "bridge", world_id, player_id);
	}
	coop_rpg_probe_call("_G.mp_coop_rpg_probe2", "anyplayer", world_id, player_id);
	coop_rpg_probe_call("_G.mp_coop_rpg_probe2", "verify", world_id, player_id);
	return true;
}

// ===== MP fork (§14 step 8 phase 3 Q5 / doc §7.5): QUEST NPCs ARE WORLD ENTITIES ================
//
// §7.5 is the one §7 requirement Q1-Q4 never covered, and this document only ever referred to it as
// "Q5" without defining it. The doc is explicit:
//
//     "An escort or protect target is a SHARED world entity (one of it, everyone sees the same one,
//      survival is server-authoritative). The QUEST RELATIONSHIP to it ('am I escorting this guy')
//      is per-player or per-party. Same body/relationship split as everything else — the NPC doesn't
//      hold flags, it IS a world object, and the quest lives on whoever took it."
//
// So this is Phase 1's ownership seam applied to a quest TARGET, and the discriminating gate is a
// NEGATIVE: the relationship must NOT land on the NPC. A probe that only checked "the player has the
// escort flag" would pass just as happily on an implementation that wrote the flag to all three.
//
// The write goes through the gamedata probe rather than through CInventoryOwner directly, and that
// is deliberate: writing the info portion in C++ would bypass the very routing layer under test and
// measure nothing. Lua `give_info` inside an acting scope is the same call a real quest script makes.
//
// The NPC is chosen with coop_find_npc — the same picker Q4 uses — so "we could not find an NPC" and
// "the split failed" stay different results, which is the distinction Q4's header already insists on.
bool game_sv_Single::coop_test_quest6_probe()
{
	const u16 world_id = mp_coop_owner::world_actor();
	if (world_id == mp_coop_owner::none)
	{
		Msg("! COOP(quest6): no world tier");
		return false;
	}
	const u16 player_id = coop_first_player_actor();
	if (player_id == mp_coop_owner::none)
		return false;   // nobody has an actor yet — retry, same as every other probe here

	const u16 npc_id = coop_find_npc(player_id);
	if (npc_id == mp_coop_owner::none)
	{
		// NOT a failure of the split — a failure to set up. Reported as its own state so the
		// harness can call the gates NOT MEASURED instead of passing them vacuously.
		Msg("! COOP(quest6): no live NPC online to stand in for an escort target — the §7.5 gates "
			"below are NOT MEASURED, not passed");
		return false;
	}

	Msg("- COOP(quest6): tiers world=%u player=%u npc=%u distinct=%u",
		u32(world_id), u32(player_id), u32(npc_id),
		u32((world_id != player_id && npc_id != world_id && npc_id != player_id) ? 1 : 0));

	coop_quest6_probe_call("setup", world_id, player_id, npc_id);
	// Runs BEFORE the take, so the control's own write cannot be confused with the escort write,
	// and so a run whose store cannot answer for NPCs says so before the gate is even asked.
	coop_quest6_probe_call("control", world_id, player_id, npc_id);
	{
		// The relationship is taken BY a player. Written inside that player's acting scope, exactly
		// as a quest script would write it when the player accepts the escort.
		mp_coop_owner::acting_scope scope(player_id);
		coop_quest6_probe_call("take", world_id, player_id, npc_id);
	}
	coop_quest6_probe_call("verify", world_id, player_id, npc_id);

	// ---- THE PARTY HALF OF §7.5 ----------------------------------------------------------------
	// "per-player OR per-party": the per-player half is above. A party-scoped relationship is shared
	// by every member and is STILL not world state — which is the only thing that distinguishes it
	// from the world tier once the party is the whole session. So the discriminating gates are
	// world=false with members>=2, not "the players have it".
	// WHAT "PARTY" MEANS HERE IS A DECISION, NOT A DISCOVERY. §7.5 says the relationship is
	// "per-player or per-party" and §7.4 says to lean toward party-shared reward — but NEITHER the
	// doc NOR this fork ever defines a party: there is no grouping, no invite, no sub-party, and
	// mp_coop_owner has exactly two tiers. So the only implementable reading today is
	// **party = everyone in the session**, with the real consequence that there cannot be two
	// parties on one server. Taken explicitly here rather than smuggled in, and the first thing to
	// overrule if sub-parties are ever wanted.
	//
	// coop_all_player_actors ALREADY EXISTED and is already used by the R3.1 probe. The first
	// attempt at this increment added a second copy of it and the build caught it as
	// "already has a body" — a reminder that the class-0 control applies to one's own additions:
	// search for the CAPABILITY, not for the word you happen to be thinking in.
	xr_vector<u16> members;
	coop_all_player_actors(members);
	Msg("- COOP(quest6): party members=%u (a party of fewer than 2 cannot tell party-scope from "
		"per-player scope, and the harness reports those gates NOT MEASURED)", u32(members.size()));
	coop_quest6_probe_call("party_reset", world_id, player_id, npc_id);
	for (u32 i = 0; i < members.size(); ++i)
	{
		mp_coop_owner::acting_scope scope(members[i]);
		coop_quest6_probe_call("party_take", world_id, members[i], npc_id);
	}
	coop_quest6_probe_call("party_verify", world_id, player_id, npc_id);
	return true;
}

void game_sv_Single::coop_quest6_probe_call(LPCSTR phase, u16 world_id, u16 player_id, u16 npc_id)
{
	luabind::functor<void> f;
	if (!ai().script_engine().functor("_G.mp_coop_quest6_probe", f))
	{
		Msg("! COOP(quest6): gamedata probe _G.mp_coop_quest6_probe not registered (phase '%s') — "
			"the §7.5 gates are NOT MEASURED", phase);
		return;
	}
	f(phase, u32(world_id), u32(player_id), u32(npc_id));
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

	// MP fork (§14 step 8 phase 1, harness): -coop_test_rpg [<seconds>] runs the ownership-tier
	// probe once a player has an actor; -coop_test_rpg_verify runs the read-only half, for the
	// leg that boots from the save and must not re-write what it is checking survived.
	if (xr_enet::enabled() && ai().get_alife())
	{
		coop_check_world_key();   // one-shot; nothing may squat the world tier's key
		if (!m_coop_test_rpg_init)
		{
			m_coop_test_rpg_init = true;
			// coop_param refuses a prefix match, so "-coop_test_rpg" no longer claims
			// "-coop_test_rpg_verify" or "-coop_test_rpg2" (see the helper).
			LPCSTR p = coop_param("-coop_test_rpg_verify");
			if (p)
				m_coop_test_rpg_verify_only = true;
			else
				p = coop_param("-coop_test_rpg");

			if (p)
			{
				const float secs = (float)atof(p);
				// NaN/inf fail every compare, so >0 also rejects non-finite input
				m_coop_test_rpg_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 30000u;
				m_coop_test_rpg_armed = Device.dwTimeGlobal;   // delay runs from HERE, not engine start
				Msg("- COOP(rpg): tier probe armed (%s), firing in %ums",
					m_coop_test_rpg_verify_only ? "verify-only" : "write+verify", m_coop_test_rpg_ms);
			}
		}
		if (m_coop_test_rpg_ms && !m_coop_test_rpg_done &&
		    Device.dwTimeGlobal - m_coop_test_rpg_armed >= m_coop_test_rpg_ms &&
		    Device.dwTimeGlobal - m_coop_test_rpg_retry >= 5000)
		{
			m_coop_test_rpg_retry = Device.dwTimeGlobal;   // retry 5s apart, not every frame
			if (coop_test_rpg_probe())
				m_coop_test_rpg_done = true;
		}

		// MP fork (§14 step 8 phase 2, harness): -coop_test_rpg2 <seconds>, §6.3's bridge case.
		if (!m_coop_test_rpg2_init)
		{
			m_coop_test_rpg2_init = true;
			LPCSTR p = coop_param("-coop_test_rpg2");
			if (p)
			{
				const float secs = (float)atof(p);
				m_coop_test_rpg2_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 30000u;
				m_coop_test_rpg2_armed = Device.dwTimeGlobal;
				Msg("- COOP(rpg2): bridge probe armed, firing in %ums", m_coop_test_rpg2_ms);
			}
		}
		if (m_coop_test_rpg2_ms && !m_coop_test_rpg2_done &&
		    Device.dwTimeGlobal - m_coop_test_rpg2_armed >= m_coop_test_rpg2_ms &&
		    Device.dwTimeGlobal - m_coop_test_rpg2_retry >= 5000)
		{
			m_coop_test_rpg2_retry = Device.dwTimeGlobal;
			if (coop_test_rpg2_probe())
				m_coop_test_rpg2_done = true;
		}

		// MP fork (§14 step 8 phase 3 Q5 / doc §7.5): -coop_test_quest6 <seconds>. Numbered one
		// ahead of its Q, because -coop_test_quest5 is already Q4's flag.
		if (!m_coop_test_quest6_init)
		{
			m_coop_test_quest6_init = true;
			LPCSTR p = coop_param("-coop_test_quest6");
			if (p)
			{
				const float secs = (float)atof(p);
				m_coop_test_quest6_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 30000u;
				m_coop_test_quest6_armed = Device.dwTimeGlobal;
				Msg("- COOP(quest6): §7.5 escort-target probe armed, firing in %ums", m_coop_test_quest6_ms);
			}
		}
		// Retries on the same 5 s cadence as the others, because the probe legitimately returns
		// false until a player has an actor AND an NPC is online — two conditions the harness
		// cannot force and must not paper over.
		if (m_coop_test_quest6_ms && !m_coop_test_quest6_done &&
		    Device.dwTimeGlobal - m_coop_test_quest6_armed >= m_coop_test_quest6_ms &&
		    Device.dwTimeGlobal - m_coop_test_quest6_retry >= 5000)
		{
			m_coop_test_quest6_retry = Device.dwTimeGlobal;
			if (coop_test_quest6_probe())
				m_coop_test_quest6_done = true;
		}

		// MP fork (§14 step 8 phase 2 / doc §6.2): the flag census on a timer. This is how the
		// world-fact classification is DERIVED rather than authored — gamedata logs every info
		// id it has seen and which tier read or wrote it, and "read with no acting player" IS
		// §6.2's definition of a world-read flag. Periodic and delta-only, because a census is
		// only as complete as the paths that have run by the time you look at it.
		if (!m_coop_census_init)
		{
			m_coop_census_init = true;
			LPCSTR p = coop_param("-coop_rpg_census");
			if (p)
			{
				const float secs = (float)atof(p);
				m_coop_census_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 300000u;
				m_coop_census_last = Device.dwTimeGlobal;
				Msg("- COOP(rpg-census): enabled, every %ums", m_coop_census_ms);
			}
		}
		if (m_coop_census_ms && Device.dwTimeGlobal - m_coop_census_last >= m_coop_census_ms)
		{
			m_coop_census_last = Device.dwTimeGlobal;
			luabind::functor<void> f;
			if (ai().script_engine().functor("_G.mp_coop_rpg_census", f))
				f("timer");
			else
				Msg("! COOP(rpg-census): _G.mp_coop_rpg_census not registered");
		}

		// MP fork (§14 step 8 phase 3 QR / doc §7.2): TWO LIVE CLAIMANTS RACE ONE CLAIM ON THE WIRE.
		//   -coop_test_race <seconds> [-coop_test_race_lead <ms>] [-coop_test_race_wait <seconds>]
		//
		// Every phase-3 increment named this as the thing it did NOT cover. Q1 and Q4 each proved a
		// refusal, but the loser was a SYNTHETIC id the server claimed on behalf of — so what was
		// never exercised is two real client claim paths contending for one pool entry, with the
		// loss landing in a real client's own adopted task list.
		//
		// The server's job here is the SETUP and the HANDSHAKE, not the claims: the claims are the
		// clients' and they arrive as real M_XRNET_DIALOG_ACTION packets.
		//
		//   stage 0  wait for TWO player actors, then offer three tasks:
		//              coop_race_a  the side-'a' client's own uncontested control
		//              coop_race_b  the side-'b' client's own uncontested control
		//              coop_race_x  THE CONTESTED ONE — both clients say the same phrase for it
		//   stage 1  wait until BOTH controls have left the pool. This is a handshake and not a
		//            timer on purpose: it is the only thing that makes "client b did not get x"
		//            readable, because a client whose claim path is broken cannot land its own
		//            uncontested control either, and the run reports NOT MEASURED instead of a
		//            first-claim-wins that was really a walkover.
		//   stage 2  broadcast the start on the decision channel and, after the lead, print the
		//            verdict the harness reads.
		//
		// Bounded at every stage, and a give-up line says which stage and that it is a HARNESS
		// outcome rather than a result — "two clients never both connected" and "the transaction
		// admitted two claimants" are different findings and must not share an exit.
		if (coop_param("-coop_test_race"))
		{
			static bool s_r_init  = false;
			static u32  s_r_armed = 0, s_r_ms = 0, s_r_retry = 0;
			static u32  s_r_lead_ms = 3000, s_r_wait_ms = 240000, s_r_grace_ms = 10000;
			static u32  s_r_stage = 0, s_r_since = 0;
			static u16  s_r_owner_a = mp_coop_owner::none, s_r_owner_b = mp_coop_owner::none;
			if (!s_r_init)
			{
				s_r_init = true;
				LPCSTR p = coop_param("-coop_test_race");
				const float secs = p ? (float)atof(p) : 0.f;
				s_r_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 60000u;
				LPCSTR l = coop_param("-coop_test_race_lead");
				const float lead = l ? (float)atof(l) : 0.f;
				if (lead > 0.f && lead <= 60000.f) s_r_lead_ms = (u32)lead;
				LPCSTR w = coop_param("-coop_test_race_wait");
				const float wsecs = w ? (float)atof(w) : 0.f;
				if (wsecs > 0.f && wsecs <= 3600.f) s_r_wait_ms = (u32)(wsecs * 1000.f);
				// The verdict's grace window. A knob and not a constant because QR-D deliberately
				// HOLDS the claims in the delayed queue (-coop_test_disc_hold) so a disconnect can
				// land between queue and drain — and a verdict printed before the held claims drain
				// would report x_owner=none and call it a result.
				LPCSTR g = coop_param("-coop_test_race_grace");
				const float gsecs = g ? (float)atof(g) : 0.f;
				if (gsecs > 0.f && gsecs <= 300.f) s_r_grace_ms = (u32)(gsecs * 1000.f);
				s_r_armed = Device.dwTimeGlobal;
				s_r_since = Device.dwTimeGlobal;
				Msg("- COOP(race): §7.2 two-claimant race armed, first look in %ums "
					"(lead=%ums, per-stage wait=%ums, verdict grace=%ums)",
					s_r_ms, s_r_lead_ms, s_r_wait_ms, s_r_grace_ms);
			}
			if (s_r_stage < 3 && Device.dwTimeGlobal - s_r_armed >= s_r_ms &&
			    Device.dwTimeGlobal - s_r_retry >= 2000)
			{
				s_r_retry = Device.dwTimeGlobal;
				// Stage 0's give-up window starts at the FIRST LOOK, not at arm time. Otherwise
				// -coop_test_race <seconds> would silently eat into the budget for "did two
				// clients ever both get an actor", which is the one thing stage 0 is waiting on.
				static bool s_r_looked = false;
				if (!s_r_looked) { s_r_looked = true; s_r_since = Device.dwTimeGlobal; }
				const bool late = (Device.dwTimeGlobal - s_r_since) >= s_r_wait_ms;
				if (s_r_stage == 0)
				{
					xr_vector<u16> members;
					coop_all_player_actors(members);
					if (members.size() < 2)
					{
						if (late)
						{
							Msg("! COOP(race): only %u player actor(s) after %ums — a race needs "
								"TWO live claimants, so the gates below are NOT MEASURED, not "
								"passed", u32(members.size()), s_r_wait_ms);
							s_r_stage = 3;
						}
					}
					else
					{
						// The offerer is cosmetic here (no faction task, no kill condition), but a
						// real NPC id keeps the pool entries looking like every other offer.
						const u16 npc = coop_find_npc(members[0]);
						coop_task_offer("coop_race_a", npc, false, false, u16(-1));
						coop_task_offer("coop_race_b", npc, false, false, u16(-1));
						coop_task_offer("coop_race_x", npc, false, false, u16(-1));
						Msg("- COOP(race): offers up pool=%u players=%u p0=%u p1=%u offerer=%u",
							coop_task_pool_size(), u32(members.size()), u32(members[0]),
							u32(members[1]), u32(npc));
						s_r_stage = 1;
						s_r_since = Device.dwTimeGlobal;
					}
				}
				else if (s_r_stage == 1)
				{
					const bool a_gone = !coop_task_offered("coop_race_a");
					const bool b_gone = !coop_task_offered("coop_race_b");
					if (!a_gone || !b_gone)
					{
						if (late)
						{
							Msg("! COOP(race): the two control claims did not both land in %ums "
								"(a_claimed=%d b_claimed=%d) — one client's claim path never "
								"worked, so the race gates are NOT MEASURED", s_r_wait_ms,
								a_gone ? 1 : 0, b_gone ? 1 : 0);
							s_r_stage = 3;
						}
					}
					else
					{
						s_r_owner_a = coop_task_owner_of("coop_race_a");
						s_r_owner_b = coop_task_owner_of("coop_race_b");
						Msg("- COOP(race): controls landed a_owner=%u b_owner=%u distinct=%d — "
							"both claim paths work, so a loss below means the transaction refused "
							"it and not that the client was mute",
							u32(s_r_owner_a), u32(s_r_owner_b),
							(s_r_owner_a != s_r_owner_b) ? 1 : 0);
						// Subject 0xFFFF and not 0: coop_broadcast_decision marks its subject
						// decision-driven, which throttles that entity's M_UPDATE stream. This
						// decision has no subject at all — it is a starting pistol — so it names
						// an id no entity has rather than an id some entity might.
						const u32 sent = Level().coop_broadcast_decision(
							/*subject*/ u16(0xFFFF), u8(CLevel::COOP_DECISION_KIND_RACE),
							NULL, 0, s_r_lead_ms);
						// clients= is the count of clients backed by a REAL player actor, i.e.
						// how many claimants the starting pistol actually reached. Fewer than two
						// and there was no race, whatever the claim lines go on to say.
						Msg("- COOP(race): START broadcast kind=%u lead=%ums clients=%u "
							"pool=%u now=%u", u32(CLevel::COOP_DECISION_KIND_RACE), s_r_lead_ms,
							sent, coop_task_pool_size(), Level().timeServer());
						s_r_stage = 2;
						s_r_since = Device.dwTimeGlobal;
					}
				}
				else if (s_r_stage == 2)
				{
					// The lead, plus a grace window big enough for the two claim packets to arrive
					// and be drained. Fixed rather than "wait until x is claimed", because the
					// interesting failure — NOBODY got it — has no event to wait for.
					if (Device.dwTimeGlobal - s_r_since >= s_r_lead_ms + s_r_grace_ms)
					{
						const u16 winner = coop_task_owner_of("coop_race_x");
						Msg("- COOP(race): VERDICT x_owner=%u still_offered=%d pool=%u "
							"a_owner=%u b_owner=%u controls_distinct=%d",
							u32(winner), coop_task_offered("coop_race_x") ? 1 : 0,
							coop_task_pool_size(), u32(s_r_owner_a), u32(s_r_owner_b),
							(s_r_owner_a != s_r_owner_b) ? 1 : 0);
						FlushLog();
						s_r_stage = 3;
					}
				}
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
			// coop_param, not strstr: a prefix match would arm this synthetic-task probe from
			// "-coop_test_quest3" as well, and a phase-3 run would then be measuring a pool that
			// an unrelated harness had also been writing into. Same class of collision the P2
			// commit fixed for -coop_test_rpg.
			if (coop_param("-coop_test_quest"))
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

	// MP fork (§14 step 8 phase 3 Q1, harness): -coop_test_quest3 <seconds> — tasks as a finite
	// shared resource (doc §7.2), the claim transaction, and §7.4's tiering.
	//
	// THE SECOND CLAIMANT IS SYNTHETIC and that is named rather than hidden: two headless clients
	// cannot share one wine prefix, so there is no second real player to race. The design answer
	// is to make the synthetic half the ATTACKER and the real client the VICTIM — leg B claims
	// first with the synthetic id and then with the real player, so the assertion that carries
	// the result ("the refused player never receives the task") is still measured on a real
	// client's own log. A test where the synthetic id only ever LOSES would prove nothing except
	// that a function returned false.
	if (xr_enet::enabled() && ai().get_alife() && coop_param("-coop_test_quest3"))
	{
		static bool s_q3_init  = false;
		static bool s_q3_done  = false;
		static u32  s_q3_armed = 0;
		static u32  s_q3_ms    = 0;
		static u32  s_q3_retry = 0;
		// §7.3 exactly-once (Q2): the entity the world-state bindings were made against, and when
		// to replay its death. Remembered here rather than re-read later on purpose — a replay
		// that asked coop_first_player_actor() again would be measuring whatever id the player
		// holds AFTER the death, and would then return 0 for the wrong reason.
		static u16  s_q3_target     = mp_coop_owner::none;
		static u32  s_q3_fired      = 0;
		static u32  s_q3_replay_ms  = 0;
		static bool s_q3_replayed   = false;
		if (!s_q3_init)
		{
			s_q3_init = true;
			LPCSTR p = coop_param("-coop_test_quest3");
			const float secs = p ? (float)atof(p) : 0.f;
			s_q3_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 30000u;
			s_q3_armed = Device.dwTimeGlobal;
			LPCSTR r = coop_param("-coop_test_quest3_replay");
			const float rsecs = r ? (float)atof(r) : 0.f;
			s_q3_replay_ms = (rsecs > 0.f && rsecs <= 86400.f) ? (u32)(rsecs * 1000.f) : 0u;
			Msg("- COOP(quest3): claim probe armed, firing in %ums (death replay %s)",
				s_q3_ms, s_q3_replay_ms ? "armed" : "off");
		}
		if (!s_q3_done && Device.dwTimeGlobal - s_q3_armed >= s_q3_ms &&
		    Device.dwTimeGlobal - s_q3_retry >= 5000)
		{
			s_q3_retry = Device.dwTimeGlobal;
			const u16 player_id = coop_first_player_actor();
			if (player_id != mp_coop_owner::none)
			{
				s_q3_done = true;
				// An id no client owns. It is only ever a map key here — nothing looks it up in
				// the object registry — but it must not collide with the real player or with the
				// world tier, so it is asserted distinct rather than assumed.
				const u16 synth = u16(0xF000);
				Msg("- COOP(quest3): player=%u synthetic=%u world=%u distinct=%u",
					u32(player_id), u32(synth), u32(mp_coop_owner::world_key),
					u32((synth != player_id && synth != mp_coop_owner::world_key) ? 1 : 0));

				coop_task_offer("coop_q3_a", 0, /*faction*/ false, /*world_state*/ true);
				coop_task_offer("coop_q3_b", 0, false, true);
				coop_task_offer("coop_q3_f", 0, /*faction*/ true,  true);
				coop_task_offer("coop_q3_u", 0, false, true);   // never claimed — stays in the pool

				// §7.3 (Q2): two more offers, each bound to a WORLD-STATE change — the death of
				// an entity. The entity is the test player's own actor, and that is a NAMED LIMIT
				// rather than a convenience: it is the only death this headless harness can cause
				// on demand (-coop_test_kill on the client), and the completion path never asks
				// what kind of entity died. What the choice does buy is the discriminating case:
				// coop_q3_v is a FACTION offer, so once claimed its owner is world_key — a key no
				// entity holds and which therefore can never be the killer. A completion rule that
				// credited only the owner would leave every faction quest permanently
				// uncompletable, and this is the only leg that would notice.
				coop_task_offer("coop_q3_k", 0, /*faction*/ false, /*world_state*/ true, player_id);
				coop_task_offer("coop_q3_v", 0, /*faction*/ true,  /*world_state*/ true, player_id);

				// Leg A: the real player wins, the synthetic one arrives late.
				const int a1 = int(coop_task_claim("coop_q3_a", player_id));
				const int a2 = int(coop_task_claim("coop_q3_a", synth));
				// Leg B: the synthetic one wins and the REAL player is refused. This is the leg
				// that matters — the client must not end up holding coop_q3_b.
				const int b1 = int(coop_task_claim("coop_q3_b", synth));
				const int b2 = int(coop_task_claim("coop_q3_b", player_id));
				// §7.4: a faction quest ends up owned by the world tier, not by whoever claimed it.
				const int f1 = int(coop_task_claim("coop_q3_f", player_id));
				// §7.3 (Q2): the two world-state-bound offers, claimed by the real player. 'v' is
				// the faction one, so its owner must come out as the world tier, not the claimant.
				const int k1 = int(coop_task_claim("coop_q3_k", player_id));
				const int v1 = int(coop_task_claim("coop_q3_v", player_id));
				const u16 fowner = coop_task_owner_of("coop_q3_f");
				const u16 aowner = coop_task_owner_of("coop_q3_a");
				const u16 kowner = coop_task_owner_of("coop_q3_k");
				const u16 vowner = coop_task_owner_of("coop_q3_v");
				const u16 ktarget = coop_task_target_of("coop_q3_k");
				const u16 vtarget = coop_task_target_of("coop_q3_v");

				const u32 pool = coop_task_pool_size();
				const bool pass = (a1 == coop_claim_ok)    && (a2 == coop_claim_taken) &&
				                  (b1 == coop_claim_ok)    && (b2 == coop_claim_taken) &&
				                  (f1 == coop_claim_ok)    &&
				                  (k1 == coop_claim_ok)    && (v1 == coop_claim_ok) &&
				                  (fowner == mp_coop_owner::world_key) &&
				                  (vowner == mp_coop_owner::world_key) &&
				                  (aowner == player_id)    && (kowner == player_id) &&
				                  (ktarget == player_id)   && (vtarget == player_id) &&
				                  (pool == 1);
				Msg("- COOP(quest3): claims a1=%d a2=%d b1=%d b2=%d f1=%d k1=%d v1=%d "
					"aowner=%u fowner=%u kowner=%u vowner=%u ktarget=%u vtarget=%u pool=%u pass=%d",
					a1, a2, b1, b2, f1, k1, v1, u32(aowner), u32(fowner), u32(kowner),
					u32(vowner), u32(ktarget), u32(vtarget), pool, pass ? 1 : 0);
				coop_dump_task_pool();
				Level().GameTaskManager().coop_broadcast_tasks();
				s_q3_target = player_id;
				s_q3_fired  = Device.dwTimeGlobal;
			}
		}

		// §7.3 exactly-once (Q2): -coop_test_quest3_replay <seconds after the claims fired> replays
		// the SAME world death and asserts nothing completes a second time.
		//
		// This is the assertion the single self-kill the harness can produce cannot make on its
		// own: one death proves a task completes, it does not prove the binding was SPENT, and a
		// completion that re-fires is how one dead mutant pays two rewards. It is also
		// self-checking in the other direction — if the real death never happened, this replay
		// completes both tasks and reports 2, so a missed kill cannot read as a clean second pass.
		if (s_q3_done && s_q3_replay_ms && !s_q3_replayed &&
		    Device.dwTimeGlobal - s_q3_fired >= s_q3_replay_ms)
		{
			s_q3_replayed = true;
			const u16 kt = coop_task_target_of("coop_q3_k");
			const u16 vt = coop_task_target_of("coop_q3_v");
			const u32 again = coop_task_on_world_death(s_q3_target, s_q3_target);
			const bool pass = (again == 0) && (kt == mp_coop_owner::none) && (vt == mp_coop_owner::none);
			Msg("- COOP(quest3r): replayed world death of %u -> completed_again=%u ktarget=%u vtarget=%u pass=%d",
				u32(s_q3_target), again, u32(kt), u32(vt), pass ? 1 : 0);
			FlushLog();
		}
	}

	// MP fork (§14 step 8 phase 3 Q3, harness): -coop_test_quest4 <seconds> — §7.3's completion
	// conditions that need BOOKKEEPING rather than a hook.
	//
	// Three tasks, chosen so that each one's failure mode is invisible to the other two:
	//   coop_q4_clear  kill over THREE entities. The point is the INTERMEDIATE state: an
	//                  implementation that completed on the first death leaves an end state
	//                  identical to a correct one, so the only place the difference exists is at
	//                  the transitions, and only while they are happening.
	//   coop_q4_hold   defend with a SHORT deadline and a target that never dies -> completes via
	//                  the real 1 Hz sweep on the real game clock.
	//   coop_q4_guard  defend with a deadline so far out it cannot arrive during the test -> its
	//                  verdict can only come from the real death, and it must be a FAILURE.
	//
	// The first two deaths are SYNTHETIC — they call coop_task_on_world_death directly, which is
	// the same entry point game_sv_Single::on_death calls, so what goes unexercised is only the
	// GE_DIE->on_death plumbing that Q2 already measured on a real death. The THIRD death is real
	// (the client's -coop_test_kill), so every terminal verdict here is driven by a genuine
	// server-authoritative event; the synthetic ones only move the bookkeeping to the interesting
	// point. Two headless clients cannot share a wine prefix, so there is no second real body to
	// kill (see [[xray-two-headless-clients]]).
	if (xr_enet::enabled() && ai().get_alife() && coop_param("-coop_test_quest4"))
	{
		static bool s_q4_init      = false;
		static bool s_q4_done      = false;
		static u32  s_q4_armed     = 0;
		static u32  s_q4_ms        = 0;
		static u32  s_q4_retry     = 0;
		static u32  s_q4_fired     = 0;
		static u32  s_q4_replay_ms = 0;
		static bool s_q4_replayed  = false;
		static u64  s_q4_guard_deadline = 0;
		if (!s_q4_init)
		{
			s_q4_init = true;
			LPCSTR p = coop_param("-coop_test_quest4");
			const float secs = p ? (float)atof(p) : 0.f;
			s_q4_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 30000u;
			s_q4_armed = Device.dwTimeGlobal;
			LPCSTR r = coop_param("-coop_test_quest4_replay");
			const float rsecs = r ? (float)atof(r) : 0.f;
			s_q4_replay_ms = (rsecs > 0.f && rsecs <= 86400.f) ? (u32)(rsecs * 1000.f) : 0u;
			Msg("- COOP(quest4): condition probe armed, firing in %ums (forced tick %s)",
				s_q4_ms, s_q4_replay_ms ? "armed" : "off");
		}
		if (!s_q4_done && Device.dwTimeGlobal - s_q4_armed >= s_q4_ms &&
		    Device.dwTimeGlobal - s_q4_retry >= 5000)
		{
			s_q4_retry = Device.dwTimeGlobal;
			const u16 player_id = coop_first_player_actor();
			if (player_id != mp_coop_owner::none)
			{
				s_q4_done = true;
				const u16 synth1 = u16(0xF001);
				const u16 synth2 = u16(0xF002);
				const u64 now    = u64(GetGameTime());
				// SHORT, so it arrives promptly whatever multiplier the game clock is running at —
				// the test must not depend on a rate it never measured. FAR, so it provably cannot.
				const u64 hold_deadline  = now + 60ull * 1000ull;             // ~1 game-minute
				s_q4_guard_deadline      = now + 24ull * 3600ull * 1000ull;   // a game-day away
				Msg("- COOP(quest4): setup player=%u synth=%u,%u now_game=%I64u hold_deadline=%I64u guard_deadline=%I64u",
					u32(player_id), u32(synth1), u32(synth2), now, hold_deadline, s_q4_guard_deadline);

				coop_task_offer("coop_q4_clear", 0, /*faction*/ false, /*world_state*/ true);
				coop_task_offer("coop_q4_hold",  0, false, true);
				coop_task_offer("coop_q4_guard", 0, false, true);
				coop_task_claim("coop_q4_clear", player_id);
				coop_task_claim("coop_q4_hold",  player_id);
				coop_task_claim("coop_q4_guard", player_id);

				xr_vector<u16> three;
				three.push_back(synth1);
				three.push_back(synth2);
				three.push_back(player_id);        // the REAL one, deliberately last
				coop_task_set_condition("coop_q4_clear", u8(coop_cond_kill), three, 0);

				xr_vector<u16> one_hold;
				one_hold.push_back(u16(0xF003));   // never dies
				coop_task_set_condition("coop_q4_hold", u8(coop_cond_defend), one_hold, hold_deadline);

				xr_vector<u16> one_guard;
				one_guard.push_back(player_id);    // dies, and that is a FAILURE
				coop_task_set_condition("coop_q4_guard", u8(coop_cond_defend), one_guard, s_q4_guard_deadline);

				const u32 rem0 = coop_task_cond_remaining("coop_q4_clear");

				// Each step samples the task state IMMEDIATELY after the event that moved it, not
				// at the end. By the time the sequence finishes there is no state anywhere that
				// says "this was 2 a moment ago" — the condition is consumed by its own progress,
				// so the intermediate values exist only in these lines.
				const u32 t1   = coop_task_on_world_death(synth1, synth1);
				const u32 rem1 = coop_task_cond_remaining("coop_q4_clear");
				const int st1  = coop_task_state_now("coop_q4_clear");
				Msg("- COOP(quest4): step1 death=%u terminal=%u clear_remaining=%u clear_state=%d",
					u32(synth1), t1, rem1, st1);

				const u32 t2   = coop_task_on_world_death(synth2, synth2);
				const u32 rem2 = coop_task_cond_remaining("coop_q4_clear");
				const int st2  = coop_task_state_now("coop_q4_clear");
				Msg("- COOP(quest4): step2 death=%u terminal=%u clear_remaining=%u clear_state=%d",
					u32(synth2), t2, rem2, st2);

				// eTaskStateInProgress == 1. The two NON-terminal steps carry this leg: a kill
				// condition that fired early would show terminal=1 and state=2 at step 1, and an
				// end-state check would see exactly what a correct run leaves behind.
				const bool pass = (rem0 == 3) &&
				                  (t1 == 0) && (rem1 == 2) && (st1 == int(eTaskStateInProgress)) &&
				                  (t2 == 0) && (rem2 == 1) && (st2 == int(eTaskStateInProgress)) &&
				                  (coop_task_cond_remaining("coop_q4_hold")  == 1) &&
				                  (coop_task_cond_remaining("coop_q4_guard") == 1);
				Msg("- COOP(quest4): setup done rem0=%u rem1=%u rem2=%u st1=%d st2=%d hold_rem=%u guard_rem=%u pass=%d",
					rem0, rem1, rem2, st1, st2,
					coop_task_cond_remaining("coop_q4_hold"),
					coop_task_cond_remaining("coop_q4_guard"), pass ? 1 : 0);
				Level().GameTaskManager().coop_broadcast_tasks();
				s_q4_fired = Device.dwTimeGlobal;
				FlushLog();
			}
		}

		// The forced tick: evaluate every condition at a game time PAST the far deadline.
		//
		// This is the assertion the Overseer's hazard is made of. coop_q4_guard has already FAILED
		// (the real death), and its record was spent by that verdict. If a verdict did not consume
		// its condition, this tick would find guard's deadline passed and COMPLETE a task that
		// already failed — turning a loss into a win, silently, minutes later. Nothing read after
		// the fact could tell the difference: the task would simply say "completed".
		//
		// It is also self-checking in the other direction. If the real death never landed, guard is
		// still live here, this tick completes it, and terminal comes back non-zero instead of 0.
		if (s_q4_done && s_q4_replay_ms && !s_q4_replayed &&
		    Device.dwTimeGlobal - s_q4_fired >= s_q4_replay_ms)
		{
			s_q4_replayed = true;
			const u64 far_future = s_q4_guard_deadline + 1000ull;
			const u32 terminal = coop_task_tick_conditions(far_future);
			const u32 rc = coop_task_cond_remaining("coop_q4_clear");
			const u32 rh = coop_task_cond_remaining("coop_q4_hold");
			const u32 rg = coop_task_cond_remaining("coop_q4_guard");
			const int sc = coop_task_state_now("coop_q4_clear");
			const int sh = coop_task_state_now("coop_q4_hold");
			const int sg = coop_task_state_now("coop_q4_guard");
			// eTaskStateFail == 0, eTaskStateCompleted == 2.
			const bool pass = (terminal == 0) && (rc == 0) && (rh == 0) && (rg == 0) &&
			                  (sc == int(eTaskStateCompleted)) &&
			                  (sh == int(eTaskStateCompleted)) &&
			                  (sg == int(eTaskStateFail));
			Msg("- COOP(quest4r): forced tick at %I64u -> terminal=%u clear(rem=%u state=%d) "
				"hold(rem=%u state=%d) guard(rem=%u state=%d) pass=%d",
				far_future, terminal, rc, sc, rh, sh, rg, sg, pass ? 1 : 0);
			FlushLog();
		}
	}

	// MP fork (§14 step 8 phase 3 Q4, harness): -coop_test_quest5 <seconds> — §7.3's HARD category,
	// dialogue-routed acquisition and turn-in.
	//
	// What this probe does NOT do is run the dialogue: that is the client's job (-coop_test_dialog),
	// because the whole point is that the claim and the turn-in arrive as a real M_XRNET_DIALOG_ACTION
	// from a real player and are attributed by the acting scope the server opens for it. What the
	// probe does is build the state the dialogue then acts on, and take the assertions that need
	// server-side reads.
	//
	// The setup is arranged so that every refusal has a DIFFERENT cause, because a turn-in that
	// refuses everything for one reason passes a test that only checks it refused:
	//   coop_q5_d  unclaimed, deliver 3      -> claimed BY THE DIALOGUE, then short, then delivered,
	//                                          then replayed (and the replay must take nothing)
	//   coop_q5_b  claimed by the attacker   -> the dialogue's claim LOSES, on a real client
	//   coop_q5_p  the attacker's own errand -> refused: not the owner
	//   coop_q5_f  faction, player's community, claimed by the attacker -> ACCEPTED: §7.4 says
	//                                          completion is member-agnostic, and this is the
	//                                          discriminating pair with coop_q5_p — same setup,
	//                                          same claimant, one bit different
	//   coop_q5_w  faction, a community the player is not in -> refused
	//   coop_q5_r  the player's own, owed to a recipient nobody is -> refused: wrong NPC
	//   coop_q5_u  never claimed -> stays in the pool, the client's control that a pool exists
	//   coop_q5_z  the ROUTING control: claimed through the same entry point gamedata uses, once
	//              with no acting scope (must refuse) and once with one (must succeed)
	if (xr_enet::enabled() && ai().get_alife() && coop_param("-coop_test_quest5"))
	{
		static bool    s_q5_init     = false;
		static bool    s_q5_done     = false;
		static bool    s_q5_replayed = false;
		static u32     s_q5_armed    = 0;
		static u32     s_q5_ms       = 0;
		static u32     s_q5_retry    = 0;
		static u32     s_q5_fired    = 0;
		static u32     s_q5_replay_ms = 0;
		static string64 s_q5_item    = {0};
		static u16     s_q5_player   = mp_coop_owner::none;
		static u16     s_q5_partner  = mp_coop_owner::none;
		if (!s_q5_init)
		{
			s_q5_init = true;
			LPCSTR p = coop_param("-coop_test_quest5");
			const float secs = p ? (float)atof(p) : 0.f;
			s_q5_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 30000u;
			s_q5_armed = Device.dwTimeGlobal;
			LPCSTR r = coop_param("-coop_test_quest5_replay");
			const float rsecs = r ? (float)atof(r) : 0.f;
			s_q5_replay_ms = (rsecs > 0.f && rsecs <= 86400.f) ? (u32)(rsecs * 1000.f) : 0u;
			// The fetch item is a FLAG, not a constant, because a section name that does not exist
			// in this install fails at spawn time and would cost a full build cycle to change.
			LPCSTR i = coop_param("-coop_test_quest5_item");
			u32 n = 0;
			while (i && *i && *i != ' ' && *i != '-' && n < sizeof(s_q5_item) - 1)
				s_q5_item[n++] = *i++;
			s_q5_item[n] = 0;
			if (!n)
				xr_strcpy(s_q5_item, "conserva");   // in stock Anomaly, and NOT in the starting kit
			Msg("- COOP(quest5): dialogue probe armed, firing in %ums (item '%s', verify %s)",
				s_q5_ms, s_q5_item, s_q5_replay_ms ? "armed" : "off");
		}
		if (!s_q5_done && Device.dwTimeGlobal - s_q5_armed >= s_q5_ms &&
		    Device.dwTimeGlobal - s_q5_retry >= 5000)
		{
			s_q5_retry = Device.dwTimeGlobal;
			const u16 player_id = coop_first_player_actor();
			// The player's community, read off the live object — the same call the task layer
			// makes, so the two cannot disagree about what a member is. It is read BEFORE the
			// probe commits, because CharacterInfo() is not populated the instant the actor
			// appears: about one boot in three it answered -1, and a community of "none" means
			// "anyone may finish it", which would quietly turn both §7.4 gates into gates that
			// cannot fail. Waiting is free — the probe already retries for the actor itself.
			u16 pcomm = u16(-1);
			if (player_id != mp_coop_owner::none)
				if (CObject* const po = Level().Objects.net_Find(player_id))
					if (CInventoryOwner* const pio = smart_cast<CInventoryOwner*>(po))
						pcomm = u16(pio->CharacterInfo().Community().index());
			if (player_id != mp_coop_owner::none && pcomm == u16(-1))
				Msg("- COOP(quest5): player %u has no community yet — waiting rather than measuring "
					"a faction gate that cannot fail", u32(player_id));
			if (player_id != mp_coop_owner::none && pcomm != u16(-1))
			{
				s_q5_done   = true;
				s_q5_player = player_id;
				const shared_str item(s_q5_item);
				const u16 synth = u16(0xF005);
				// A community the player is demonstrably NOT in. Picked by construction rather
				// than by finding some other faction, because "some other faction" is exactly the
				// kind of environmental assumption that makes a negative gate vacuous when the
				// level happens to disagree.
				const u16 wrongcomm = (pcomm == 0) ? u16(1) : u16(0);

				// A live NPC to be the dialogue's partner and the recipient of the goods. It is
				// found here as well as on the client because the harness's info-subject replay
				// needs one server-side; a missing one is reported, not worked around.
				s_q5_partner = coop_find_npc(player_id);

				const u32 base = coop_items_count(player_id, item);
				const u32 granted = coop_grant_items(player_id, item, 2);   // 2 of the 3 owed
				const u32 carried = coop_items_count(player_id, item);

				coop_task_offer("coop_q5_d", 0, /*faction*/ false, /*world_state*/ false);
				coop_task_set_deliver("coop_q5_d", item, 3, mp_coop_owner::none);
				coop_task_offer("coop_q5_b", 0, false, false);
				coop_task_offer("coop_q5_p", 0, false, false);
				coop_task_set_deliver("coop_q5_p", item, 1, mp_coop_owner::none);
				coop_task_offer("coop_q5_f", 0, /*faction*/ true, false);
				coop_task_set_deliver("coop_q5_f", item, 1, mp_coop_owner::none);
				coop_task_offer("coop_q5_w", 0, /*faction*/ true, false);
				coop_task_set_deliver("coop_q5_w", item, 1, mp_coop_owner::none);
				coop_task_offer("coop_q5_r", 0, false, false);
				coop_task_set_deliver("coop_q5_r", item, 1, u16(0xF00A));   // a recipient nobody is
				coop_task_offer("coop_q5_u", 0, false, false);              // never claimed
				coop_task_offer("coop_q5_z", 0, false, false);              // the routing control

				// The attacker gets there first on four of them. b is the one the real client will
				// try for and lose; p, f and w are the ones it will try to TURN IN without having
				// claimed, which is the §7.4 question.
				const int b1 = int(coop_task_claim("coop_q5_b", synth));
				coop_task_claim("coop_q5_p", synth);
				coop_task_claim("coop_q5_f", synth);
				coop_task_claim("coop_q5_w", synth);
				// AFTER the claims: claiming a faction offer records the community captured at
				// offer time, which for an ambient offer (offerer 0) is "none".
				coop_task_set_community("coop_q5_f", pcomm);
				coop_task_set_community("coop_q5_w", wrongcomm);

				// The routing control, and it is the assertion this probe can make with no client
				// dialogue at all: the SAME entry point gamedata calls, once with no acting player
				// and once with one. The first must refuse — the server runs script autonomously
				// all the time, and a claim taken in that context would hand a personal errand to
				// nobody.
				const int z0 = int(coop_task_claim_acting("coop_q5_z"));
				int z1 = -1;
				{
					mp_coop_owner::acting_scope scope(player_id);
					z1 = int(coop_task_claim_acting("coop_q5_z"));
					coop_task_claim_acting("coop_q5_r");     // the player's own, for the NPC gate
				}
				const int t0 = int(coop_task_turn_in_acting("coop_q5_z", player_id));
				const u16 zowner = coop_task_owner_of("coop_q5_z");
				const u16 rowner = coop_task_owner_of("coop_q5_r");

				const bool pass = (base == 0) && (granted == 2) && (carried == 2) &&
				                  (b1 == coop_claim_ok) &&
				                  (z0 == coop_claim_no_actor) && (z1 == coop_claim_ok) &&
				                  (t0 == coop_turnin_no_actor) &&
				                  (zowner == player_id) && (rowner == player_id) &&
				                  (pcomm != u16(-1)) && (s_q5_partner != mp_coop_owner::none);
				Msg("- COOP(quest5): setup done player=%u partner=%u synth=%u item='%s' base=%u "
					"granted=%u carried=%u pcomm=%d wrongcomm=%d b1=%d z_noscope=%d z_scoped=%d "
					"turnin_noscope=%d zowner=%u rowner=%u pool=%u pass=%d",
					u32(player_id), u32(s_q5_partner), u32(synth), s_q5_item, base, granted,
					carried, int(short(pcomm)), int(short(wrongcomm)), b1, z0, z1, t0,
					u32(zowner), u32(rowner), coop_task_pool_size(), pass ? 1 : 0);
				coop_dump_task_pool();
				Level().GameTaskManager().coop_broadcast_tasks();
				s_q5_fired = Device.dwTimeGlobal;
				FlushLog();
			}
		}

		// The verify stage: read the world the DIALOGUE left behind, and take the one assertion
		// that needs the server to drive a phrase itself.
		if (s_q5_done && s_q5_replay_ms && !s_q5_replayed &&
		    Device.dwTimeGlobal - s_q5_fired >= s_q5_replay_ms)
		{
			s_q5_replayed = true;
			const shared_str item(s_q5_item);
			const u16 player_id = s_q5_player;
			const u16 synth = u16(0xF005);

			// --- what the dialogue did ---
			const u16 downer = coop_task_owner_of("coop_q5_d");
			const u16 bowner = coop_task_owner_of("coop_q5_b");
			const int dstate = coop_task_state_now("coop_q5_d");
			const int bstate = coop_task_state_now("coop_q5_b");
			const int pstate = coop_task_state_now("coop_q5_p");
			const int fstate = coop_task_state_now("coop_q5_f");
			const int wstate = coop_task_state_now("coop_q5_w");
			const int rstate = coop_task_state_now("coop_q5_r");
			const u32 drem = coop_task_cond_remaining("coop_q5_d");
			const u32 prem = coop_task_cond_remaining("coop_q5_p");
			const u32 wrem = coop_task_cond_remaining("coop_q5_w");
			const u32 rrem = coop_task_cond_remaining("coop_q5_r");
			const u32 carried = coop_items_count(player_id, item);
			const u32 with_npc = (s_q5_partner != mp_coop_owner::none)
				? coop_items_count(s_q5_partner, item) : 0u;

			// --- the info subject: ONE phrase, TWO acting scopes, opposite outcomes ---
			//
			// This is the only way the §6.1 routing is observable with a single client connected.
			// Stock writes a dialogue's <give_info> to Actor(), which on this server is g_actor —
			// "whichever player actor spawned last", i.e. this very player. So a run under an
			// acting scope for an id that resolves to nobody must leave the player WITHOUT the
			// portion; under stock behaviour they would have it, and nothing else in this test
			// would notice.
			bool ghost_after_bogus = true, ghost_after_real = false;
			if (Level().Server && s_q5_partner != mp_coop_owner::none)
			{
				Level().Server->coop_run_dialog_phrase(synth, player_id, s_q5_partner,
					"coop_q5_dialog", "probe_info");
				ghost_after_bogus = coop_has_info(player_id, "coop_q5_ghost");
				Level().Server->coop_run_dialog_phrase(player_id, player_id, s_q5_partner,
					"coop_q5_dialog", "probe_info");
				ghost_after_real = coop_has_info(player_id, "coop_q5_ghost");
			}
			// The portion the dialogue's own turn-in phrase gives, which lands through the same
			// routing on the live path rather than the replayed one.
			const bool delivered_info = coop_has_info(player_id, "coop_q5_delivered");

			// eTaskStateInProgress == 1, eTaskStateCompleted == 2.
			const bool pass =
				(downer == player_id) && (dstate == int(eTaskStateCompleted)) && (drem == 0) &&
				(bowner == synth) && (bstate != int(eTaskStateCompleted)) &&
				(pstate == int(eTaskStateInProgress)) && (prem == 1) &&
				(fstate == int(eTaskStateCompleted)) &&
				(wstate == int(eTaskStateInProgress)) && (wrem == 1) &&
				(rstate == int(eTaskStateInProgress)) && (rrem == 1) &&
				(carried == 0) &&
				(!ghost_after_bogus) && ghost_after_real && delivered_info;
			// with_npc is REPORTED and never gated. With no named recipient the goods go to
			// whoever the player is talking to, and the client picks that NPC out of its own
			// object list — so the one this probe picked out of the SERVER's is a bystander who
			// was handed nothing, and a run where everything worked reads 0 here. The real
			// reading is taken by the dialogue itself, which is the only place that knows the
			// recipient (mp_coop_dialogs.script logs "NPC <id> now holds N").

			Msg("- COOP(quest5v): verify downer=%u dstate=%d drem=%u bowner=%u bstate=%d "
				"pstate=%d prem=%u fstate=%d wstate=%d wrem=%u rstate=%d rrem=%u "
				"carried=%u with_npc=%u ghost_bogus=%d ghost_real=%d delivered_info=%d pass=%d",
				u32(downer), dstate, drem, u32(bowner), bstate, pstate, prem, fstate, wstate,
				wrem, rstate, rrem, carried, with_npc, ghost_after_bogus ? 1 : 0,
				ghost_after_real ? 1 : 0, delivered_info ? 1 : 0, pass ? 1 : 0);
			coop_dump_task_pool();
			FlushLog();
		}
	}

	// MP fork (§14 step 8 phase 3 Q3 / doc §7.3): the condition tick. Kill conditions are advanced
	// by an EVENT (a death) and need no clock; defend conditions are the ones with a deadline, and
	// a deadline nobody looks at never arrives.
	//
	// Once a second of wall time is plenty — the game clock runs faster than wall time, so this is
	// already fine-grained relative to what it measures, and the resolution of "the escort survived"
	// is not a thing any player can perceive to the frame. What it must NOT be is per-frame: the
	// sweep walks every live condition, and this runs inside the server's Update.
	if (xr_enet::enabled() && ai().get_alife())
	{
		static u32 s_cond_tick = 0;
		if (Device.dwTimeGlobal - s_cond_tick >= 1000)
		{
			s_cond_tick = Device.dwTimeGlobal;
			coop_task_tick_conditions(u64(GetGameTime()));
		}
	}

	// MP fork (§14 step 8 phase 3 Q2, harness): -coop_test_quest3_verify <seconds> — the READ-ONLY
	// half. It boots from the .scop a -coop_test_quest3 run wrote and asserts what came back out.
	//
	// It offers nothing and claims nothing, and that is the whole design of the leg: a verify pass
	// that re-offered would rebuild the very state it is supposed to be measuring and would pass
	// with the persistence ripped out. Everything it reports therefore came from the file or from
	// nowhere. (coop_param refuses a prefix match, so this flag does not also arm the write leg.)
	if (xr_enet::enabled() && ai().get_alife() && coop_param("-coop_test_quest3_verify"))
	{
		static bool s_q3v_init  = false;
		static bool s_q3v_done  = false;
		static u32  s_q3v_armed = 0;
		static u32  s_q3v_ms    = 0;
		static u32  s_q3v_retry = 0;
		if (!s_q3v_init)
		{
			s_q3v_init = true;
			LPCSTR p = coop_param("-coop_test_quest3_verify");
			const float secs = p ? (float)atof(p) : 0.f;
			s_q3v_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 30000u;
			s_q3v_armed = Device.dwTimeGlobal;
			Msg("- COOP(quest3v): survival probe armed, firing in %ums", s_q3v_ms);
		}
		if (!s_q3v_done && Device.dwTimeGlobal - s_q3v_armed >= s_q3v_ms &&
		    Device.dwTimeGlobal - s_q3v_retry >= 5000)
		{
			s_q3v_retry = Device.dwTimeGlobal;
			const u16 player_id = coop_first_player_actor();
			if (player_id != mp_coop_owner::none)
			{
				s_q3v_done = true;
				const u16 synth   = u16(0xF000);
				const u16 aowner  = coop_task_owner_of("coop_q3_a");
				const u16 bowner  = coop_task_owner_of("coop_q3_b");
				const u16 fowner  = coop_task_owner_of("coop_q3_f");
				const u16 kowner  = coop_task_owner_of("coop_q3_k");
				const u16 vowner  = coop_task_owner_of("coop_q3_v");
				const u16 ktarget = coop_task_target_of("coop_q3_k");
				const u32 pool    = coop_task_pool_size();

				// aowner == player_id is TWO claims at once and worth reading as such: the owner
				// tag came back out of the file AND the reconnecting player got the same entity
				// id back (step-7 P2's reclaim). Either half failing breaks it, which is right —
				// an ownership tag that survives while the body it names does not is worthless.
				const bool pass = (pool == 1) &&
				                  (aowner == player_id) && (bowner == synth) &&
				                  (fowner == mp_coop_owner::world_key) &&
				                  (kowner == player_id) &&
				                  (vowner == mp_coop_owner::world_key) &&
				                  (ktarget == player_id);

				vGameTasks& tl = Level().GameTaskManager().GetGameTasks();
				Msg("- COOP(quest3v): survived player=%u aowner=%u bowner=%u fowner=%u kowner=%u "
					"vowner=%u ktarget=%u pool=%u tasks=%u pass=%d",
					u32(player_id), u32(aowner), u32(bowner), u32(fowner), u32(kowner),
					u32(vowner), u32(ktarget), pool, u32(tl.size()), pass ? 1 : 0);
				// The task list itself is reported separately from the annotations, because the
				// two can fail independently: registry().save carries the tasks, our own chunk
				// carries who owns them, and a run where only one of the two came back must not
				// look like a run where neither did.
				for (u32 i = 0; i < tl.size(); ++i)
					Msg("    task '%s' state=%d owner=%u", tl[i].task_id.c_str(),
						tl[i].game_task ? int(tl[i].game_task->GetTaskState()) : -1,
						u32(coop_task_owner_of(tl[i].task_id)));
				coop_dump_task_pool();
				Level().GameTaskManager().coop_broadcast_tasks();
			}
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
	// ---------------------------------------------------------------------------------------
	// MP fork (§14 step 8 PHASE 4 increment R1, dev/RPG_LAYER_PLAN.md): measure the two storage
	// claims before anything is built on either of them.
	//
	//   -coop_test_rep1 <seconds>   write pass: stamp a distinctive PERSONAL standing on the
	//                               connected player's entity id, and move a FACTION<->FACTION
	//                               relation by a distinctive amount. Then the harness restarts.
	//   -coop_test_rep1_verify      read-only pass: report both, and nothing else.
	//
	// Why measure at all when the recon already read the source? Because the two tiers fail
	// IDENTICALLY to a reader: `community_relation(a,b)` returning its ltx value after a restart
	// looks the same whether the table was restored from the save or simply re-read from config,
	// and only a value that no config file contains can tell those apart. So the write pass moves
	// it to a number the ltx cannot produce, and the verify pass says which world it woke up in.
	//
	// The verify pass WRITES NOTHING, deliberately: a verify that re-stamped would rebuild the
	// state it is measuring and would pass with the persistence ripped out — Q2's rule, and the
	// reason its own verify probe is read-only.
	if (xr_enet::enabled() && ai().get_alife() &&
	    (coop_param("-coop_test_rep1") || coop_param("-coop_test_rep1_verify")))
	{
		static bool     s_r1_init   = false;
		static bool     s_r1_done   = false;
		static u32      s_r1_armed  = 0;
		static u32      s_r1_ms     = 0;
		static u32      s_r1_retry  = 0;
		if (!s_r1_init)
		{
			s_r1_init = true;
			LPCSTR p = coop_param("-coop_test_rep1");
			const float secs = p ? (float)atof(p) : 0.f;
			s_r1_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 25000u;
			s_r1_armed = Device.dwTimeGlobal;
		}

		// The values are chosen to be unreachable by accident: personal standing is clamped into
		// [community_goodwill_limits] so it must sit inside that band, and the faction delta is
		// an odd number no communities_relations row uses.
		const int  R1_PERSONAL = 77;
		const int  R1_FACTION  = -63;
		LPCSTR     R1_COMM     = "stalker";
		LPCSTR     R1_FROM     = "stalker";
		LPCSTR     R1_TO       = "bandit";

		if (!s_r1_done && (Device.dwTimeGlobal - s_r1_armed) >= s_r1_ms)
		{
			const u16 player_id = coop_first_player_actor();
			if (player_id == mp_coop_owner::none)
			{
				// Retry rather than fail: a probe that fired before the client connected would
				// report "no player" as if that were the measurement.
				//
				// The budget is WALL CLOCK, not a frame count. Run 1 of R1 counted frames and gave
				// up in under a minute — while the client was still booting — and then reported
				// "no player ever connected" about a client that connected fine and was sending
				// M_CL_UPDATE for entity 22677 a moment later. A frame budget is a duration whose
				// length depends on the server's frame rate, which is exactly the thing that
				// varies between a quiet server and a loaded one.
				if ((Device.dwTimeGlobal - s_r1_retry) >= 5000u)
				{
					s_r1_retry = Device.dwTimeGlobal;
					Msg("- COOP(rep1): waiting for a connected player (%u s so far)",
						(Device.dwTimeGlobal - s_r1_armed - s_r1_ms) / 1000u);
				}
				if ((Device.dwTimeGlobal - s_r1_armed) > (s_r1_ms + 240000u))
				{
					s_r1_done = true;
					Msg("! COOP(rep1): no player connected within 240 s of arming — this run "
						"measured NOTHING (and that is a harness failure, not a result)");
					FlushLog();
				}
			}
			else
			{
				s_r1_done = true;
				CHARACTER_COMMUNITY comm;  comm.set(R1_COMM);
				CHARACTER_COMMUNITY from;  from.set(R1_FROM);
				CHARACTER_COMMUNITY to;    to.set(R1_TO);

				const int p_before = RELATION_REGISTRY().GetCommunityGoodwill(comm.index(), player_id);
				const int f_before = RELATION_REGISTRY().GetCommunityRelation(from.index(), to.index());

				if (coop_param("-coop_test_rep1_verify"))
				{
					// READ ONLY. The gate the harness takes is on these two numbers alone.
					Msg("- COOP(rep1): VERIFY player=%u personal['%s']=%d (wrote %d) "
						"faction['%s'->'%s']=%d (wrote %d) personal_kept=%d faction_kept=%d",
						player_id, R1_COMM, p_before, R1_PERSONAL, R1_FROM, R1_TO, f_before,
						R1_FACTION, (p_before == R1_PERSONAL) ? 1 : 0,
						(f_before == R1_FACTION) ? 1 : 0);
				}
				else
				{
					RELATION_REGISTRY().SetCommunityGoodwill(comm.index(), player_id, R1_PERSONAL);
					RELATION_REGISTRY().SetCommunityRelation(from.index(), to.index(), R1_FACTION);

					const int p_after = RELATION_REGISTRY().GetCommunityGoodwill(comm.index(), player_id);
					const int f_after = RELATION_REGISTRY().GetCommunityRelation(from.index(), to.index());

					// before AND after, on one line: a write that was clamped or refused reads
					// exactly like a write that never happened once the restart has been taken,
					// and by then there is nothing left to tell them apart.
					Msg("- COOP(rep1): WROTE player=%u personal['%s'] %d -> %d (want %d, took=%d) "
						"faction['%s'->'%s'] %d -> %d (want %d, took=%d)",
						player_id, R1_COMM, p_before, p_after, R1_PERSONAL,
						(p_after == R1_PERSONAL) ? 1 : 0,
						R1_FROM, R1_TO, f_before, f_after, R1_FACTION,
						(f_after == R1_FACTION) ? 1 : 0);
				}
				FlushLog();
			}
		}
	}
	// ---------------------------------------------------------------------------------------
	// MP fork (§14 step 8 PHASE 4 increment R2, dev/RPG_LAYER_PLAN.md, doc §8.1): the seam.
	//
	//   -coop_test_rep2 <seconds>          write pass, then a read-only report of both tiers.
	//   -coop_test_rep2_verify <seconds>   read-only pass, for the boot after the restart.
	//   -coop_test_rep2_badver             (consumed by the SAVE, in relation_registry.cpp)
	//                                      stamps a version this build cannot know, so the
	//                                      refusal path is measured rather than argued.
	//
	// THE SEQUENCE IS THE TEST, and its order is not the phase-1 order. R2 writes the acting
	// pass FIRST and attempts the autonomous one SECOND, because a personal write that wrongly
	// landed on the player during autonomous simulation would be INVISIBLE if the acting write
	// came after it — overwritten by the value the test expects to find. Taken in this order,
	// one read of the player's row proves three things at once: the acting write landed, the
	// acting scope did not leak past its closing brace, and the seam did not quietly fall back
	// to a subject when it had none.
	//
	// The writes go through the LUA bindings, not through RELATION_REGISTRY directly, because
	// the Lua bindings ARE the seam — R1 wrote in C++ and that was right for a storage question,
	// but a routing question asked in C++ would be testing the harness.
	if (xr_enet::enabled() && ai().get_alife() &&
	    (coop_param("-coop_test_rep2") || coop_param("-coop_test_rep2_verify")))
	{
		static bool s_r2_init  = false;
		static bool s_r2_done  = false;
		static u32  s_r2_armed = 0;
		static u32  s_r2_ms    = 0;
		static u32  s_r2_retry = 0;
		if (!s_r2_init)
		{
			s_r2_init = true;
			LPCSTR p = coop_param("-coop_test_rep2");
			if (!p) p = coop_param("-coop_test_rep2_verify");
			const float secs = p ? (float)atof(p) : 0.f;
			s_r2_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 25000u;
			s_r2_armed = Device.dwTimeGlobal;
		}

		if (!s_r2_done && (Device.dwTimeGlobal - s_r2_armed) >= s_r2_ms)
		{
			const u16 world_id  = mp_coop_owner::world_actor();
			const u16 player_id = coop_first_player_actor();
			if (world_id == mp_coop_owner::none || player_id == mp_coop_owner::none)
			{
				// Wall clock, not frames — R1 run 1 spent a whole run learning that a frame
				// budget is a duration whose length depends on the frame rate.
				if ((Device.dwTimeGlobal - s_r2_retry) >= 5000u)
				{
					s_r2_retry = Device.dwTimeGlobal;
					Msg("- COOP(rep2): waiting for a connected player (%u s so far)",
						(Device.dwTimeGlobal - s_r2_armed - s_r2_ms) / 1000u);
				}
				if ((Device.dwTimeGlobal - s_r2_armed) > (s_r2_ms + 240000u))
				{
					s_r2_done = true;
					Msg("! COOP(rep2): no player connected within 240 s of arming — this run "
						"measured NOTHING (and that is a harness failure, not a result)");
					FlushLog();
				}
			}
			else
			{
				s_r2_done = true;
				// The tiers being DISTINCT is the first assertion, exactly as in phase 1: if the
				// world tier shared a key with the player's actor, every negative below would be
				// unfalsifiable and the run would pass while proving nothing.
				Msg("- COOP(rep2): tiers world=%u player=%u distinct=%u", u32(world_id),
					u32(player_id), u32(world_id != player_id ? 1 : 0));

				const u32 routed_before  = coop_rep_routed_count();
				const u32 refused_before = coop_rep_refused_count();

				if (!coop_param("-coop_test_rep2_verify"))
				{
					{
						mp_coop_owner::acting_scope scope(player_id);
						coop_rpg_probe_call("_G.mp_coop_rep_probe", "acting", world_id, player_id);
					}
					coop_rpg_probe_call("_G.mp_coop_rep_probe", "autonomous", world_id, player_id);
				}
				coop_rpg_probe_call("_G.mp_coop_rep_probe", "verify", world_id, player_id);

				// The seam's own counters. A seam that works and a seam that was never reached
				// read identically off the values alone: routed=0 with every value correct means
				// the probe named its subject itself and the routing was never exercised.
				Msg("- COOP(rep2): seam routed=%u refused=%u zero=%u (this pass: routed=%u refused=%u)",
					coop_rep_routed_count(), coop_rep_refused_count(), coop_rep_routed_zero_count(),
					coop_rep_routed_count() - routed_before,
					coop_rep_refused_count() - refused_before);
				FlushLog();
			}
		}
	}

	// ---------------------------------------------------------------------------------------
	// MP fork (§14 step 8 PHASE 4 increment R3.0, dev/RPG_LAYER_PLAN.md, doc §8.2/§8.3):
	// MEASURE WHAT A KILL ALREADY DOES, before a line of propagation is written.
	//
	// The recon found that stock already propagates a kill — CEntityAlive::Die ->
	// RELATION_REGISTRY::Action(killer, victim, KILL) — and that it moves the PERSONAL tier only:
	// the victim community's goodwill toward the KILLER's entity id, plus reputation and rank.
	// It also found that NOTHING in the engine writes faction<->faction (every SetCommunityRelation
	// caller in src/ is the Lua binding or R1's probe). So the shooter tier of §8.2 may already be
	// satisfied and the collective tier is built from nothing — and R3.1 must not double-apply the
	// half that already works, nor assume the half that does not.
	//
	// This is R1's shape for a different question: both outcomes are informative, the probe says
	// which world it woke up in, and the only real failure is a run that measured nothing.
	//
	// THE VICTIM MUST BE A CAI_Stalker. The whole effect in Action lives inside `if (stalker)`, so
	// a monster kill moves nothing and a harness that killed the nearest thing would report "stock
	// propagates nothing" about a path that was never going to run.
	//
	// THE BYSTANDER ROW IS SEEDED FIRST, with a value nothing else produces. Its gate is that it
	// does NOT move, and an untouched row of a nonexistent subject reads 0 before and 0 after —
	// unchanged for the boring reason. Seeding makes "unchanged" mean something. It stands in for
	// a second player because two headless clients cannot share a wine prefix; that is a
	// CONSTRUCTION, and the report says so on the line itself rather than in a comment nobody
	// reads next to the number.
	if (xr_enet::enabled() && ai().get_alife() && coop_param("-coop_test_rep3"))
	{
		static bool s_r3_init   = false;
		static int  s_r3_stage  = 0;      // 0 = waiting, 1 = killed and settling, 2 = done
		static u32  s_r3_armed  = 0;
		static u32  s_r3_ms     = 0;
		static u32  s_r3_retry  = 0;
		static u32  s_r3_killed_at = 0;
		static u16  s_r3_victim = mp_coop_owner::none;
		static u16  s_r3_player = mp_coop_owner::none;
		static int  s_r3_vcomm = -1, s_r3_kcomm = -1;
		static int  s_r3_p_before = 0, s_r3_b_before = 0, s_r3_f_before = 0;
		static int  s_r3_rep_before = 0, s_r3_rank_before = 0;

		const u16 R3_BYSTANDER = u16(0xFFF0);   // a stand-in id, not an entity — see above
		const int R3_BYSTANDER_SEED = 55;       // distinctive, so "did not move" is a measurement
		const u32 R3_SETTLE_MS = 5000;          // the death is a NET event; let it land

		if (!s_r3_init)
		{
			s_r3_init = true;
			LPCSTR p = coop_param("-coop_test_rep3");
			const float secs = p ? (float)atof(p) : 0.f;
			s_r3_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 30000u;
			s_r3_armed = Device.dwTimeGlobal;
		}

		if (s_r3_stage == 0 && (Device.dwTimeGlobal - s_r3_armed) >= s_r3_ms)
		{
			const u16 player_id = coop_first_player_actor();
			CObject* const pobj = (player_id != mp_coop_owner::none)
			                      ? Level().Objects.net_Find(player_id) : NULL;
			CInventoryOwner* const pio = pobj ? smart_cast<CInventoryOwner*>(pobj) : NULL;

			// A stalker victim, online and alive. Not the player, and not a monster.
			u16 victim_id = mp_coop_owner::none;
			u32 stalkers = 0, others = 0;
			if (pio)
			{
				for (u32 i = 0; i < Level().Objects.o_count(); ++i)
				{
					CObject* const o = Level().Objects.o_get_by_iterator(i);
					if (!o || o->getDestroy() || o->ID() == player_id)
						continue;
					CEntityAlive* const alive = smart_cast<CEntityAlive*>(o);
					if (!alive || !alive->g_Alive())
						continue;
					if (!smart_cast<CAI_Stalker*>(o)) { ++others; continue; }
					++stalkers;
					if (victim_id == mp_coop_owner::none)
						victim_id = o->ID();
				}
			}

			if (!pio || victim_id == mp_coop_owner::none)
			{
				if ((Device.dwTimeGlobal - s_r3_retry) >= 5000u)
				{
					s_r3_retry = Device.dwTimeGlobal;
					Msg("- COOP(rep3): waiting (%u s) player=%u live_stalkers=%u other_living=%u",
						(Device.dwTimeGlobal - s_r3_armed - s_r3_ms) / 1000u, u32(player_id),
						stalkers, others);
				}
				if ((Device.dwTimeGlobal - s_r3_armed) > (s_r3_ms + 300000u))
				{
					s_r3_stage = 2;
					// "No stalker was online" and "stock propagation does nothing" are different
					// results and only one of them is about the engine.
					Msg("! COOP(rep3): no connected player with a LIVE STALKER to kill within 300 s "
						"— this run measured NOTHING (a harness failure, not a result)");
					FlushLog();
				}
			}
			else
			{
				CObject* const vobj = Level().Objects.net_Find(victim_id);
				CInventoryOwner* const vio = vobj ? smart_cast<CInventoryOwner*>(vobj) : NULL;
				CEntity* const ventity = vobj ? smart_cast<CEntity*>(vobj) : NULL;
				if (!vio || !ventity)
				{
					s_r3_stage = 2;
					Msg("! COOP(rep3): victim %u is not an inventory owner/entity — measured NOTHING",
						u32(victim_id));
					FlushLog();
				}
				else
				{
					s_r3_player = player_id;
					s_r3_victim = victim_id;
					s_r3_vcomm  = vio->Community();
					s_r3_kcomm  = pio->Community();

					// Seed the stand-in row, then sample everything BEFORE.
					RELATION_REGISTRY().SetCommunityGoodwill(s_r3_vcomm, R3_BYSTANDER, R3_BYSTANDER_SEED);
					s_r3_p_before    = RELATION_REGISTRY().GetCommunityGoodwill(s_r3_vcomm, player_id);
					s_r3_b_before    = RELATION_REGISTRY().GetCommunityGoodwill(s_r3_vcomm, R3_BYSTANDER);
					s_r3_f_before    = RELATION_REGISTRY().GetCommunityRelation(s_r3_vcomm, s_r3_kcomm);
					s_r3_rep_before  = pio->Reputation();
					s_r3_rank_before = pio->Rank();

					Msg("- COOP(rep3): BEFORE player=%u victim=%u victim_comm=%d killer_comm=%d "
						"personal_killer=%d bystander_seeded=%d faction=%d reputation=%d rank=%d "
						"(live_stalkers=%u)",
						u32(player_id), u32(victim_id), s_r3_vcomm, s_r3_kcomm, s_r3_p_before,
						s_r3_b_before, s_r3_f_before, s_r3_rep_before, s_r3_rank_before, stalkers);
					FlushLog();

					// A REAL death, attributed to the player: GE_DIE carries the killer id, and
					// the server's own death path runs from there. Nothing here calls Action
					// directly — a probe that did would measure itself rather than the engine.
					ventity->KillEntity(player_id);
					s_r3_killed_at = Device.dwTimeGlobal;
					s_r3_stage = 1;
					Msg("- COOP(rep3): killed victim=%u attributed to player=%u; sampling again in %u ms",
						u32(victim_id), u32(player_id), R3_SETTLE_MS);
					FlushLog();
				}
			}
		}
		else if (s_r3_stage == 1 && (Device.dwTimeGlobal - s_r3_killed_at) >= R3_SETTLE_MS)
		{
			s_r3_stage = 2;
			CObject* const vobj = Level().Objects.net_Find(s_r3_victim);
			CEntityAlive* const valive = vobj ? smart_cast<CEntityAlive*>(vobj) : NULL;
			CObject* const pobj = Level().Objects.net_Find(s_r3_player);
			CInventoryOwner* const pio = pobj ? smart_cast<CInventoryOwner*>(pobj) : NULL;

			const int p_after    = RELATION_REGISTRY().GetCommunityGoodwill(s_r3_vcomm, s_r3_player);
			const int b_after    = RELATION_REGISTRY().GetCommunityGoodwill(s_r3_vcomm, R3_BYSTANDER);
			const int f_after    = RELATION_REGISTRY().GetCommunityRelation(s_r3_vcomm, s_r3_kcomm);
			const int rep_after  = pio ? pio->Reputation() : s_r3_rep_before;
			const int rank_after = pio ? pio->Rank() : s_r3_rank_before;

			// died= is the gate before every other number: a victim still alive means the kill did
			// not happen, and then "nothing moved" is about the harness, not about the engine.
			Msg("- COOP(rep3): AFTER died=%d personal_killer %d -> %d (moved=%d) "
				"bystander[SYNTHETIC, no second live client] %d -> %d (moved=%d) "
				"faction %d -> %d (moved=%d) reputation %d -> %d (moved=%d) rank %d -> %d (moved=%d)",
				(valive && valive->g_Alive()) ? 0 : 1,
				s_r3_p_before, p_after, (p_after != s_r3_p_before) ? 1 : 0,
				s_r3_b_before, b_after, (b_after != s_r3_b_before) ? 1 : 0,
				s_r3_f_before, f_after, (f_after != s_r3_f_before) ? 1 : 0,
				s_r3_rep_before, rep_after, (rep_after != s_r3_rep_before) ? 1 : 0,
				s_r3_rank_before, rank_after, (rank_after != s_r3_rank_before) ? 1 : 0);
			FlushLog();
		}
	}

	// ---------------------------------------------------------------------------------------
	// MP fork (§14 step 8 PHASE 4 increment R3.1, dev/RPG_LAYER_PLAN.md, doc §8.2/§8.3):
	// THE BLAST RADIUS — two kills, and the second one is a different question from the first.
	//
	// R3.0 left R3.1 two things to settle and one thing not to break, and this probe is shaped
	// around all three:
	//
	//   * NOT TO BREAK: the shooter tier is already stock (-140 on the victim community's row
	//     toward the killer). Each leg's personal delta must be EXACTLY that. Two kills give
	//     the gate teeth a single kill cannot: a double-application shows up as -280 per kill,
	//     and a propagation that accidentally re-entered would show up as drift between legs.
	//   * TO SETTLE: `actor` is not a faction, and every player is `actor`. Leg 1 kills with the
	//     player's booted community and the collective hit must be REFUSED; leg 2 gives the
	//     player a real faction and it must LAND — on `stalker -> actor_stalker`, while
	//     `stalker -> actor` stays at zero THROUGH BOTH LEGS. That last one is the discriminating
	//     gate, and it is the §8.3 failure that would otherwise be invisible on one client:
	//     a run where the collective hit lands on the shared pseudo-community looks exactly like
	//     a run where it landed correctly, unless something watches the cell it must not touch.
	//     The chosen community is RE-READ after it is set, because "I called the setter" and
	//     "the player is in that faction" are different claims (R3.0's scope says so explicitly).
	//   * TO CONSTRUCT HONESTLY: there is no second live client, so the bystander is synthetic —
	//     but it is injected into the SAME enumeration real players go through, so the radius
	//     test, the same-faction test, the falloff and the write are all the production path.
	//     Leg 1 places it INSIDE the radius (a scaled hit) and leg 2 OUTSIDE it (exactly zero),
	//     because a bystander term that is merely "small" passes any test that only checks the
	//     shooter. Every line about it carries [SYNTHETIC, no second live client] on the line.
	// MP fork (§14 step 8 P4 R3.1): drive the deferred half of the blast radius. The positions
	// were sampled at the kill; the shooter's magnitude only exists after it, so the apply waits
	// here. Cheap and silent when nothing is pending.
	if (xr_enet::enabled())
		coop_rep_propagate_tick();

	if (xr_enet::enabled() && ai().get_alife() && coop_param("-coop_test_rep31"))
	{
		static bool s_r31_init  = false;
		static int  s_r31_stage = 0;   // 0 armed/leg1 setup, 1 leg1 settle, 2 leg2 setup, 3 leg2 settle, 4 done
		static u32  s_r31_armed = 0, s_r31_ms = 0, s_r31_retry = 0, s_r31_killed_at = 0;
		static int  s_r31_leg   = 0;
		static u16  s_r31_player = mp_coop_owner::none;
		static u16  s_r31_victim = mp_coop_owner::none;
		static int  s_r31_vcomm = -1, s_r31_kcomm = -1;
		static int  s_r31_actor_idx = -1, s_r31_faction_idx = -1;
		static int  s_r31_p_before = 0, s_r31_b_before = 0;
		static u16  s_r31_bys = mp_coop_owner::none;      // the REAL second player
		static int  s_r31_prefer_comm = -2;               // -2 = not resolved yet
		static int  s_r31_realb_before = 0;
		static float s_r31_realb_dist = -1.f;
		static int  s_r31_f_actor_before = 0, s_r31_f_chosen_before = 0;
		static u32  s_r31_bys_moved_before = 0, s_r31_fac_moved_before = 0, s_r31_fac_ref_before = 0;
		static bool s_r31_shooter_logged = false;         // the shooter choice is logged once, not per tick
		// R4: the collective tier now goes through R3.2's accumulator, so each leg records what the
		// bar did as well as what the relation did.
		static u32  s_r31_cross_before = 0, s_r31_held_before = 0;
		// R4: leg 3 needs a THIRD victim, and legs 1-2 have already killed two of leg 1's
		// community. If that pool is empty the leg would wait out its whole bound measuring
		// nothing — so the requirement is dropped, once, and SAID.
		static bool s_r31_anycomm = false;

		// A stand-in id, not an entity — the same construction R3.0 used, kept at the same value
		// so the two runs' bystander rows are directly comparable.
		const u16 R31_BYSTANDER = u16(0xFFF0);
		const int R31_BYSTANDER_SEED = 55;
		const u32 R31_SETTLE_MS = 5000;

		// ---- R4: leg 2 and leg 3 straddle the bar, and the two values are DERIVED ----------------
		//
		// R3.1 measured `leg2 collective moved = 1` on a pre-bar binary, where the write went
		// straight at the relation. On any R3.2 binary the collective hit is
		// `shooter_delta / COOP_REP_COLLECTIVE_DIVISOR` = -140/20 = -7 against a production bar of
		// 50, so one kill is correctly HELD and that gate fails — which is R3.2 working, and is why
		// R3.1's 25/25 was pinned to an old build.
		//
		// The re-baseline sets the bar to the single-kill magnitude ITSELF rather than disabling it.
		// A bar of 0 or 1 would restore the old number by testing a path production does not have,
		// which is the exact failure §8.3 names. At bar = 7 the comparison is `_abs(-7) < 7` — false,
		// so it crosses — and at bar = 8 it is true, so it holds. The two legs therefore pin the
		// BOUNDARY and its inclusivity, which no run has ever exercised: leg 2 alone is satisfied by
		// a bar that crosses on anything, and leg 3 is what stops that.
		const s32 R31_LEG2_BAR = 7;   // == |stock -140| / COOP_REP_COLLECTIVE_DIVISOR(20): crosses
		const s32 R31_LEG3_BAR = 8;   // one above it: must HOLD, or the comparison is not a threshold

		if (!s_r31_init)
		{
			s_r31_init = true;
			LPCSTR p = coop_param("-coop_test_rep31");
			const float secs = p ? (float)atof(p) : 0.f;
			s_r31_ms = (secs > 0.f && secs <= 86400.f) ? (u32)(secs * 1000.f) : 30000u;
			s_r31_armed = Device.dwTimeGlobal;

			// The falloff CURVE, measured at chosen distances rather than inferred from one
			// write. The last sample is the increment's negative gate and it must be exactly 0.
			const float r = coop_rep_bystander_radius();
			// The lock table, dumped in the SAME RUN the wedge will happen in — the addresses are
			// per-run heap pointers, so a dump from any other run is worthless for reading this
			// run's wait graph. R4's deadlock is `0118 holds 112A9C0 and wants heap.cs` against
			// `0174 holds heap.cs and wants 112A9C0`; this is what turns `112A9C0` into a name.
			coop_cs_dump();
			// And the fault table, dumped HERE for the same reason but with a different job: at arm
			// time there have been no faults yet, so this prints "INSTALLED, 0 recorded" — which is
			// the control that makes a later zero readable. A run that ends with no COOP(av) lines
			// and no arm-time line is an instrument that never registered; a run that ends with no
			// COOP(av) lines but WITH the arm-time line genuinely had no access violations.
			coop_av_dump();
			// R5's control, same argument: R5's gate says a run in which the cell does NOT move must
			// report NOT MEASURED rather than PASS, and that is only meaningful if the instrument is
			// known to be live. This prints "INSTALLED, 0 writes" before any write happens.
			extern void coop_rel_dump();
			coop_rel_dump();
			Msg("- COOP(rep31): falloff[SYNTHETIC, no second live client] radius=%.1f m  "
				"scale(0)=%.3f scale(r/2)=%.3f scale(r-0.1)=%.3f scale(r)=%.3f scale(r+1)=%.3f",
				r, coop_rep_bystander_scale(0.f), coop_rep_bystander_scale(r * 0.5f),
				coop_rep_bystander_scale(r - 0.1f), coop_rep_bystander_scale(r),
				coop_rep_bystander_scale(r + 1.f));
			FlushLog();
		}

		if ((s_r31_stage == 0 || s_r31_stage == 2 || s_r31_stage == 5)
		    && (Device.dwTimeGlobal - s_r31_armed) >= s_r31_ms)
		{
			// THE SHOOTER IS NOT "WHOEVER CONNECTED FIRST" ANY MORE, and run 6 is why.
			//
			// The stock kill penalty this whole increment scales is applied by the GAMMA addon
			// `grok_killing_friends_reduces_goodwill.script`, whose first gate is
			// `killer:id() == db.actor:id()`. `db.actor` is a SINGLE-ACTOR global, so with two
			// clients connected it names exactly one of them. Run 6 measured that gate at the gate,
			// on both of the probe's own kills:
			//
			//   ~ COOP(rep3a): npc_death victim=22645 killer=21681 db.actor=26268 killer_is_db_actor=no
			//
			// The probe was shooting with `coop_first_player_actor()` — the first-connected client —
			// which is not the actor `db.actor` names, so the tier was silent and three runs
			// measured a blast radius of zero times a falloff. The gate is not wrong; the probe was
			// shooting with the wrong player. So pick the shooter the world will actually attribute
			// the kill to, and let the OTHER player be the bystander.
			//
			// WHICH PLAYER IS `db.actor`? Run 7 answered it by falsifying the first guess.
			//
			// The first attempt used `g_actor` (Actor_Network.cpp's "most recently spawned actor")
			// on the belief that it tracks the same object gamedata's `db.actor` does. It does not:
			//
			//   shooter=21284 chosen by first-connected (g_actor is not a connected player)
			//                 [g_actor=0 first_connected=21284]
			//   ~ COOP(rep3a): npc_death victim=22645 killer=21284 db.actor=26475 killer_is_db_actor=no
			//
			// `g_actor` names an actor with id 0 — the single-player actor slot, not a connected
			// client — so the guard refused it and fell back, which is the only reason this cost one
			// run instead of becoming a fourth silent zero.
			//
			// What the same line measured is the answer: `db.actor` was **26475, the second-connected
			// client** — and in run 6 it was 26268, also the second-connected. Two runs, and the
			// mechanism agrees: `db.actor` is assigned in the actor binder's `net_spawn`, so the LAST
			// actor to spawn wins. So the shooter is the last-connected player, and the bystander
			// becomes the first-connected one (the selection loop skips the shooter either way).
			//
			// Still not asserted blind: COOP(rep3a) logs `db.actor` from Lua at every death and this
			// logs the choice with every candidate beside it, so a run where they disagree says so.
			// g_actor is declared in Actor.h, already included above.
			const u16 world_actor_id = g_actor ? g_actor->ID() : u16(mp_coop_owner::none);
			const u16 first_player   = coop_first_player_actor();

			xr_vector<u16> shooter_cands;
			coop_all_player_actors(shooter_cands);

			u16 player_id = first_player;
			LPCSTR shooter_why = "first-connected (only one player)";
			bool g_actor_is_player = false;
			for (u32 si = 0; si < shooter_cands.size(); ++si)
				if (shooter_cands[si] == world_actor_id && world_actor_id != mp_coop_owner::none)
					g_actor_is_player = true;

			if (g_actor_is_player)
			{
				// Kept as the preferred path: if a build ever makes g_actor name a real client, that
				// is a direct answer rather than an ordering argument, and it should win.
				player_id = world_actor_id;
				shooter_why = "g_actor (it IS a connected player on this build)";
			}
			else if (shooter_cands.size() >= 2)
			{
				player_id = shooter_cands.back();
				shooter_why = "last-connected (measured: db.actor is the second client, runs 6 and 7)";
			}
			if (!s_r31_shooter_logged && player_id != mp_coop_owner::none)
			{
				s_r31_shooter_logged = true;
				Msg("- COOP(rep31): shooter=%u chosen by %s  [g_actor=%u first_connected=%u "
					"players=%u] — the stock tier's gate is killer==db.actor, so shooting with the "
					"wrong player measures zero and reports it as a blast radius. Cross-check this "
					"against COOP(rep3a)'s db.actor= on the kill.",
					u32(player_id), shooter_why, u32(world_actor_id), u32(first_player),
					u32(shooter_cands.size()));
				FlushLog();
			}

			CObject* const pobj = (player_id != mp_coop_owner::none)
			                      ? Level().Objects.net_Find(player_id) : NULL;
			CInventoryOwner* const pio = pobj ? smart_cast<CInventoryOwner*>(pobj) : NULL;

			// THE SECOND PLAYER, and run 3 is the first run that could have one. The plan
			// carried "no second live client" as settled fact; re-running the two-client test
			// as-is on the current engine PASSED (both clients connect and replicate on one
			// wine prefix), so the blast radius can now be OBSERVED on a real player instead of
			// constructed. The synthetic candidate stays anyway — it is what pins the exact
			// boundary at r and r+1, which a real player parked wherever it spawned cannot.
			if (s_r31_prefer_comm == -2)
				s_r31_prefer_comm = int(CHARACTER_COMMUNITY::IdToIndex(
					"stalker", CHARACTER_COMMUNITY_INDEX(-1), true));

			xr_vector<u16> players;
			coop_all_player_actors(players);
			u16 bys_id = mp_coop_owner::none;
			Fvector bys_pos;
			bys_pos.set(0.f, 0.f, 0.f);
			for (u32 pi = 0; pi < players.size(); ++pi)
			{
				if (players[pi] == player_id)
					continue;
				CSE_Abstract* const e = ai().alife().objects().object(players[pi], true);
				if (!e || !_valid(e->o_Position))
					continue;
				bys_id = players[pi];
				bys_pos = e->o_Position;       // the CSE: the pump thread keeps it live, the object does not
				break;
			}

			// VICTIM SELECTION IS THE EXPERIMENT. Leg 1 picks the stalker NEAREST the second
			// player, so the kill lands inside the radius and the scaled hit is observable; leg 2
			// picks one BEYOND the radius, where the correct answer is exactly zero. Choosing the
			// distance by choosing the victim needs no client movement — the two clients spawn
			// co-located, so moving one would be a whole subsystem to get a number this gets for
			// free. Leg 2's victim must also share leg 1's community, or the -140 regression gate
			// compares two different numbers.
			const float radius = coop_rep_bystander_radius();
			u16 victim_id = mp_coop_owner::none;
			float victim_score = -1.f, victim_dist = -1.f;
			u32 stalkers = 0;
			if (pio)
			{
				for (u32 i = 0; i < Level().Objects.o_count(); ++i)
				{
					CObject* const o = Level().Objects.o_get_by_iterator(i);
					if (!o || o->getDestroy() || o->ID() == player_id || o->ID() == s_r31_victim)
						continue;
					if (bys_id != mp_coop_owner::none && o->ID() == bys_id)
						continue;
					CEntityAlive* const alive = smart_cast<CEntityAlive*>(o);
					if (!alive || !alive->g_Alive() || !smart_cast<CAI_Stalker*>(o))
						continue;
					CInventoryOwner* const cio = smart_cast<CInventoryOwner*>(o);
					if (!cio)
						continue;
					if (!s_r31_anycomm && s_r31_vcomm >= 0 && int(cio->Community()) != s_r31_vcomm)
						continue;
					// Run 3 measured that the shooter tier does NOT move for every victim: a
					// `zombied` victim gave `personal 0 -> 0` where a `stalker` victim gives -140,
					// in four separate runs. Whatever writes it is community-specific, so leg 1
					// PREFERS the community that has actually been observed to produce a hit —
					// otherwise the run measures a blast radius of zero times a falloff and calls
					// it a result. Resolved by NAME, and the fallback is reported, not silent.
					if (s_r31_leg == 0 && s_r31_prefer_comm >= 0
					    && int(cio->Community()) != s_r31_prefer_comm)
						continue;
					++stalkers;

					const float d = (bys_id != mp_coop_owner::none)
					                ? o->Position().distance_to(bys_pos) : 0.f;
					// leg 1 wants the smallest distance, leg 2 the largest. Same loop, one sign.
					const float score = (s_r31_leg == 0) ? -d : d;
					if (victim_id == mp_coop_owner::none || score > victim_score)
					{
						victim_id = o->ID();
						victim_score = score;
						victim_dist = d;
					}
				}
			}
			if (s_r31_leg == 0 && victim_id == mp_coop_owner::none && s_r31_prefer_comm >= 0)
			{
				Msg("- COOP(rep31): no live '%s' victim available — dropping the preference and "
					"taking any stalker. The shooter tier may then measure 0, and that is a "
					"property of the run, reported here rather than inferred from the result.",
					"stalker");
				s_r31_prefer_comm = -1;
			}

			// Leg 2 only means something if its victim is genuinely OUTSIDE the radius. Say so
			// rather than silently reporting a zero that the distance did not earn.
			if (s_r31_leg == 1 && bys_id != mp_coop_owner::none && victim_dist >= 0.f
			    && victim_dist <= radius)
			{
				Msg("! COOP(rep31): leg2's farthest available stalker is only %.1f m from the "
					"second player (radius %.0f) — its zero would NOT be attributable to the "
					"radius. Reported, not hidden.", victim_dist, radius);
			}
			s_r31_bys = bys_id;
			s_r31_realb_dist = victim_dist;

			if (!pio || victim_id == mp_coop_owner::none)
			{
				if ((Device.dwTimeGlobal - s_r31_retry) >= 5000u)
				{
					s_r31_retry = Device.dwTimeGlobal;
					Msg("- COOP(rep31): leg %d waiting %u s — player=%u live_stalkers%s=%u",
						s_r31_leg + 1,
						(Device.dwTimeGlobal - s_r31_armed) / 1000u, u32(player_id),
						(s_r31_vcomm >= 0) ? "(matching leg 1's community)" : "", stalkers);
					// R4: FLUSHED. Without this the one line that says WHY a leg is stuck sits in the
					// buffer until something else flushes it — and R4 run 1 lost leg 3 exactly that
					// way: the log stopped growing, the harness's stall detector concluded "the
					// server is gone" and killed it, and the probe's own account of the problem died
					// in the buffer. A waiting line that does not reach the log is worse than no
					// waiting line, because the silence gets attributed to the wrong component.
					FlushLog();
				}
				// R4: leg 3 is the third kill of the run, and legs 1-2 have already spent two
				// members of leg 1's community. If none is left, drop the community requirement
				// rather than wait out the bound measuring nothing — and REPORT the consequence,
				// because the shooter tier is community-specific (runs 3-5: a `zombied` victim
				// gives 0 where a `stalker` gives -140). A leg 3 whose shooter takes +0 accumulates
				// nothing, and its gates then correctly read NOT MEASURED instead of passing on an
				// unmoved row that no write ever attempted.
				if (!s_r31_anycomm && s_r31_leg >= 2 && stalkers == 0
				    && (Device.dwTimeGlobal - s_r31_armed) > (s_r31_ms + 20000u))
				{
					s_r31_anycomm = true;
					Msg("- COOP(rep31): leg %d found NO live stalker of leg 1's community left — "
						"legs 1-2 killed them. Dropping the community requirement and taking any "
						"live stalker. The shooter tier is community-specific, so this leg may now "
						"measure a +0 shooter delta; that is a property of the run and its gates "
						"will say NOT MEASURED rather than pass on an unmoved row.", s_r31_leg + 1);
					FlushLog();
				}
				if ((Device.dwTimeGlobal - s_r31_armed) > (s_r31_ms + 300000u))
				{
					s_r31_stage = 4;
					Msg("! COOP(rep31): leg %d found no connected player with a LIVE STALKER%s "
						"within 300 s — this run measured NOTHING (a harness failure, not a result)",
						s_r31_leg + 1, (s_r31_vcomm >= 0) ? " of leg 1's community" : "");
					// R4: emit DONE on the give-up too. The harness waits on this line, so a leg
					// that gives up without it leaves the run to expire on a timeout — which R4
					// run 1 did, and a timeout tells the reader nothing about WHICH leg stalled.
					// The gates for the leg that never ran read NOT MEASURED on their own; this
					// only stops the run hanging around to find that out.
					Msg("- COOP(rep31): DONE legs=%d(GAVE UP on leg %d) bystanders_considered=%u "
						"bystanders_moved=%u faction_moved=%u faction_refused=%u "
						"pressure_crossed=%u pressure_held=%u",
						s_r31_leg, s_r31_leg + 1, coop_rep_bystanders_considered(),
						coop_rep_bystanders_moved(), coop_rep_faction_moved_count(),
						coop_rep_faction_refused_count(), coop_rep_pressure_crossed(),
						coop_rep_pressure_held());
					FlushLog();
				}
			}
			else
			{
				CObject* const vobj = Level().Objects.net_Find(victim_id);
				CInventoryOwner* const vio = vobj ? smart_cast<CInventoryOwner*>(vobj) : NULL;
				CEntity* const ventity = vobj ? smart_cast<CEntity*>(vobj) : NULL;
				if (!vio || !ventity)
				{
					s_r31_stage = 4;
					Msg("! COOP(rep31): victim %u is not an inventory owner/entity — measured NOTHING",
						u32(victim_id));
					FlushLog();
				}
				else
				{
					// LEG 2's whole question: give the player a REAL faction, then re-read it.
					// "I called SetCommunity" and "the player is in that faction" are different
					// claims, and only the second one licenses reading the leg-2 faction cell.
					if (s_r31_leg >= 1)
					{
						const CHARACTER_COMMUNITY_INDEX chosen =
							CHARACTER_COMMUNITY::IdToIndex("actor_stalker", CHARACTER_COMMUNITY_INDEX(-1), true);
						const int before_set = int(pio->Community());
						if (chosen >= 0)
							pio->SetCommunity(chosen);
						s_r31_faction_idx = int(pio->Community());     // RE-READ, not assumed
						Msg("- COOP(rep31): leg%d chose a faction: 'actor_stalker' index=%d; "
							"player community %d -> %d (re-read) took=%d",
							s_r31_leg + 1, int(chosen), before_set, s_r31_faction_idx,
							(chosen >= 0 && s_r31_faction_idx == int(chosen)) ? 1 : 0);

						// R4: set the bar for THIS leg, BEFORE the kill that tests it. Only the bar
						// is touched — the half-lives and the tick keep their production values, so
						// what these two legs measure is the threshold and nothing else.
						coop_rep_test_set_pressure_tunables(
							(s_r31_leg == 1) ? R31_LEG2_BAR : R31_LEG3_BAR, 0ull, 0ull, 0ull);
						Msg("- COOP(rep31): leg%d bar set to %d — one collective hit of "
							"|shooter|/%d is expected to %s (leg 2 and leg 3 straddle the boundary; "
							"the AFTER line below prints the pressure actually produced, so this "
							"prediction can be checked against it without leaving the log)",
							s_r31_leg + 1, coop_rep_pressure_bar(), 20,
							(s_r31_leg == 1) ? "CROSS it" : "be HELD BY it");
						FlushLog();
					}

					s_r31_player = player_id;
					s_r31_victim = victim_id;
					s_r31_vcomm  = int(vio->Community());
					s_r31_kcomm  = int(pio->Community());
					if (s_r31_actor_idx < 0)
						s_r31_actor_idx = int(CHARACTER_COMMUNITY::IdToIndex(
							"actor", CHARACTER_COMMUNITY_INDEX(-1), true));

					// The synthetic bystander: INSIDE the radius on leg 1, OUTSIDE it on leg 2.
					// Its community is set to the killer's so the same-faction test passes and
					// the leg-2 zero is attributable to the RADIUS and to nothing else.
					// THE RADIUS IS SET FROM THE DISTANCE WE ACTUALLY HAVE. The two co-op
					// clients spawn ~105 m from the nearest live stalker, so the 30 m default
					// makes every real-bystander measurement a zero — the correct answer to a
					// question worth nothing. Leg 1 sets r = 2 x d, so the live bystander sits at
					// EXACTLY half the radius and its predicted hit is exactly half the shooter's;
					// leg 2 sets r = d / 2, so it is unambiguously outside. The radius was always
					// a tunable; this makes the run state which value it used instead of hoping
					// the world placed an NPC conveniently.
					if (s_r31_bys != mp_coop_owner::none && s_r31_realb_dist > 1.f)
					{
						const float want_r = (s_r31_leg == 0)
							? (s_r31_realb_dist * 2.f) : (s_r31_realb_dist * 0.5f);
						coop_rep_set_bystander_radius(want_r);
						Msg("- COOP(rep31): leg%d radius set to %.1f m from the live bystander's "
							"%.1f m (%s)", s_r31_leg + 1, want_r, s_r31_realb_dist,
							(s_r31_leg == 0) ? "so it sits at exactly r/2" : "so it is outside r");
					}
					const float r = coop_rep_bystander_radius();
					const float offset = (s_r31_leg == 0) ? (r * 0.5f) : (r + 10.f);
					Fvector bpos = ventity->Position();
					bpos.x += offset;
					coop_rep_test_set_bystander(true, R31_BYSTANDER, bpos,
					                            CHARACTER_COMMUNITY_INDEX(s_r31_kcomm));

					// The second player joins the killer's faction too, so leg 2's zero is
					// attributable to the RADIUS and to nothing else — §8.2's same-faction test
					// would otherwise supply a second, indistinguishable reason for it.
					if (s_r31_leg == 1 && s_r31_bys != mp_coop_owner::none && s_r31_faction_idx >= 0)
					{
						CObject* const bo = Level().Objects.net_Find(s_r31_bys);
						CInventoryOwner* const bio = bo ? smart_cast<CInventoryOwner*>(bo) : NULL;
						if (bio)
							bio->SetCommunity(CHARACTER_COMMUNITY_INDEX(s_r31_faction_idx));
					}

					RELATION_REGISTRY().SetCommunityGoodwill(s_r31_vcomm, R31_BYSTANDER, R31_BYSTANDER_SEED);
					s_r31_realb_before = (s_r31_bys != mp_coop_owner::none)
						? RELATION_REGISTRY().GetCommunityGoodwill(s_r31_vcomm, s_r31_bys) : 0;
					s_r31_p_before        = RELATION_REGISTRY().GetCommunityGoodwill(s_r31_vcomm, player_id);
					s_r31_b_before        = RELATION_REGISTRY().GetCommunityGoodwill(s_r31_vcomm, R31_BYSTANDER);
					s_r31_f_actor_before  = (s_r31_actor_idx >= 0)
						? RELATION_REGISTRY().GetCommunityRelation(s_r31_vcomm, s_r31_actor_idx) : 0;
					s_r31_f_chosen_before = (s_r31_faction_idx >= 0)
						? RELATION_REGISTRY().GetCommunityRelation(s_r31_vcomm, s_r31_faction_idx) : 0;
					s_r31_bys_moved_before = coop_rep_bystanders_moved();
					s_r31_fac_moved_before = coop_rep_faction_moved_count();
					s_r31_fac_ref_before   = coop_rep_faction_refused_count();
					s_r31_cross_before     = coop_rep_pressure_crossed();
					s_r31_held_before      = coop_rep_pressure_held();

					Msg("- COOP(rep31): leg%d BEFORE player=%u victim=%u victim_comm=%d killer_comm=%d "
						"personal=%d bystander_seeded=%d faction_to_actor=%d faction_to_chosen=%d "
						"synthetic_at=%.1f m (radius %.1f, %s) "
						"LIVE_bystander=%u at %.1f m row=%d",
						s_r31_leg + 1, u32(player_id), u32(victim_id), s_r31_vcomm, s_r31_kcomm,
						s_r31_p_before, s_r31_b_before, s_r31_f_actor_before, s_r31_f_chosen_before,
						offset, r, (s_r31_leg == 0) ? "INSIDE" : "OUTSIDE",
						u32(s_r31_bys), s_r31_realb_dist, s_r31_realb_before);
					FlushLog();

					ventity->KillEntity(player_id);
					s_r31_killed_at = Device.dwTimeGlobal;
					s_r31_stage = (s_r31_leg == 0) ? 1 : ((s_r31_leg == 1) ? 3 : 6);
					Msg("- COOP(rep31): leg%d killed victim=%u attributed to player=%u",
						s_r31_leg + 1, u32(victim_id), u32(player_id));
					FlushLog();
				}
			}
		}
		else if ((s_r31_stage == 1 || s_r31_stage == 3 || s_r31_stage == 6)
		         && (Device.dwTimeGlobal - s_r31_killed_at) >= R31_SETTLE_MS)
		{
			CObject* const vobj = Level().Objects.net_Find(s_r31_victim);
			CEntityAlive* const valive = vobj ? smart_cast<CEntityAlive*>(vobj) : NULL;

			const int p_after = RELATION_REGISTRY().GetCommunityGoodwill(s_r31_vcomm, s_r31_player);
			const int b_after = RELATION_REGISTRY().GetCommunityGoodwill(s_r31_vcomm, R31_BYSTANDER);
			const int f_actor_after = (s_r31_actor_idx >= 0)
				? RELATION_REGISTRY().GetCommunityRelation(s_r31_vcomm, s_r31_actor_idx) : 0;
			const int f_chosen_after = (s_r31_faction_idx >= 0)
				? RELATION_REGISTRY().GetCommunityRelation(s_r31_vcomm, s_r31_faction_idx) : 0;

			// `faction_to_actor` is printed as a DELTA as well as a value because zero is the
			// answer here and an absolute zero is also what an unwritten cell reads: the delta
			// says the cell was watched across a kill, not merely found empty afterwards.
			const int realb_after = (s_r31_bys != mp_coop_owner::none)
				? RELATION_REGISTRY().GetCommunityGoodwill(s_r31_vcomm, s_r31_bys) : 0;

			// The LIVE bystander is an OBSERVATION and is labelled as one, beside the synthetic
			// that is not. Both are printed on the same line so neither can be quoted without
			// the other's status.
			Msg("- COOP(rep31): leg%d LIVE_bystander[OBSERVED, real second client] id=%u "
				"at %.1f m (radius %.0f) %d -> %d (delta %+d)",
				s_r31_leg + 1, u32(s_r31_bys), s_r31_realb_dist, coop_rep_bystander_radius(),
				s_r31_realb_before, realb_after, realb_after - s_r31_realb_before);

			Msg("- COOP(rep31): leg%d AFTER died=%d personal %d -> %d (delta %+d) "
				"bystander[SYNTHETIC, no second live client] %d -> %d (delta %+d) "
				"faction_to_actor %d -> %d (delta %+d) faction_to_chosen %d -> %d (delta %+d) "
				"bystanders_moved=%u faction_moved=%u faction_refused=%u",
				s_r31_leg + 1, (valive && valive->g_Alive()) ? 0 : 1,
				s_r31_p_before, p_after, p_after - s_r31_p_before,
				s_r31_b_before, b_after, b_after - s_r31_b_before,
				s_r31_f_actor_before, f_actor_after, f_actor_after - s_r31_f_actor_before,
				s_r31_f_chosen_before, f_chosen_after, f_chosen_after - s_r31_f_chosen_before,
				coop_rep_bystanders_moved() - s_r31_bys_moved_before,
				coop_rep_faction_moved_count() - s_r31_fac_moved_before,
				coop_rep_faction_refused_count() - s_r31_fac_ref_before);

			// R4: what the BAR did, on its own line, with every term needed to check it. The
			// pressure is read AFTER the settle above, so a crossing shows +0 here — the cell is
			// spent by the crossing — and that is exactly why `crossed`/`held` are reported as
			// counts rather than inferred from the value.
			Msg("- COOP(rep31): leg%d BAR bar=%d crossed=%u held=%u pressure_now=%+d cells=%u "
				"faction_moved=%u — a hit of |shooter|/%d against bar %d",
				s_r31_leg + 1, coop_rep_pressure_bar(),
				coop_rep_pressure_crossed() - s_r31_cross_before,
				coop_rep_pressure_held() - s_r31_held_before,
				(s_r31_vcomm >= 0 && s_r31_kcomm >= 0)
					? coop_rep_pressure_of(CHARACTER_COMMUNITY_INDEX(s_r31_vcomm),
					                       CHARACTER_COMMUNITY_INDEX(s_r31_kcomm)) : 0,
				coop_rep_pressure_cells(),
				coop_rep_faction_moved_count() - s_r31_fac_moved_before,
				20, coop_rep_pressure_bar());
			FlushLog();

			if (s_r31_stage == 1)
			{
				// Leg 2 re-arms from now, so its own wait is bounded the same way leg 1's was.
				s_r31_leg = 1;
				s_r31_stage = 2;
				s_r31_armed = Device.dwTimeGlobal;
				s_r31_ms = 3000;
			}
			else if (s_r31_stage == 3)
			{
				// LEG 3 — the same kill against a bar one point HIGHER, which must hold. Leg 2's
				// crossing SPENT its accumulator (the cell is erased on crossing), so leg 3 starts
				// from zero pressure rather than from leg 2's residue; without that this leg would
				// be measuring the leftovers of the previous one.
				s_r31_leg = 2;
				s_r31_stage = 5;
				s_r31_armed = Device.dwTimeGlobal;
				s_r31_ms = 3000;
			}
			else
			{
				s_r31_stage = 4;
				coop_rep_test_set_bystander(false, 0, Fvector().set(0.f, 0.f, 0.f),
				                            CHARACTER_COMMUNITY_INDEX(-1));
				Msg("- COOP(rep31): DONE legs=3 bystanders_considered=%u bystanders_moved=%u "
					"faction_moved=%u faction_refused=%u pressure_crossed=%u pressure_held=%u",
					coop_rep_bystanders_considered(), coop_rep_bystanders_moved(),
					coop_rep_faction_moved_count(), coop_rep_faction_refused_count(),
					coop_rep_pressure_crossed(), coop_rep_pressure_held());
				FlushLog();
			}
		}
	}


	// ================== §14 step 8 P4 R3.2 — THE GUARD RAILS (doc §8.3) ==================
	//
	// `-coop_test_rep32 <arm_seconds>`. ONE client is enough and is deliberate: the bar is about
	// history, not bystanders, and with a single client the shooter IS db.actor, which control A
	// measured to be the condition under which the stock -140 fires at all. A two-client run would
	// add the R3.1 shooter-selection variable to a test that is not about it.
	//
	// FOUR legs, because three of them are individually satisfiable by a broken bar:
	//
	//   A. the bar HOLDS      — kills accumulate, the relation does NOT move. A bar that refuses
	//                           everything passes this leg, which is why leg B exists.
	//   B. the bar is CROSSED — keep killing; at the threshold the relation moves exactly ONCE, by
	//                           the accumulated amount, and the accumulator resets. A bar that
	//                           refuses everything FAILS here. This is the leg that makes A mean
	//                           something rather than being "the feature is switched off".
	//   C. decay RUNS         — pressure falls as GAME time passes, on the clock, without any kill.
	//   D. the restart is MEASURED — the world is saved mid-decay and the harness restarts the
	//                           server. On reload the stored stamp must be the SAME u64 that was
	//                           written, and the decay applied must correspond to the elapsed game
	//                           time since it — not to zero. A fresh timer and a correctly-resumed
	//                           one are indistinguishable from the pressure VALUE alone, so both
	//                           the stamp and the elapsed difference are printed at both ends.
	if (xr_enet::enabled() && ai().get_alife() && coop_param("-coop_test_rep32"))
	{
		static u32  s_r32_armed = 0;
		static u32  s_r32_ms    = 0;
		static bool s_r32_init  = false;
		static int  s_r32_stage = 0;      // 0 wait/kill  3 settle  1 decay-watch  2 done
		static u16  s_r32_player = mp_coop_owner::none;
		static u16  s_r32_last_victim = mp_coop_owner::none;
		static int  s_r32_vcomm = -1, s_r32_kcomm = -1;
		static u32  s_r32_kills = 0;
		static u32  s_r32_retry = 0;
		static u32  s_r32_next_kill_at = 0;
		static s32  s_r32_rel_at_start = 0;
		static s32  s_r32_peak_pressure = 0;
		static u32  s_r32_moves_seen = 0;
		static u64  s_r32_decay_start_game = 0;
		static s32  s_r32_decay_start_val  = 0;
		static u32  s_r32_decay_started_at = 0;
		static bool s_r32_resume_mode = false;
		// The relation AT THE CROSSING. Run 4 moved it to -46 and then the OVERLAY decayed back to
		// baseline across the 540 s window, so a gate comparing start against the post-window value
		// read "the relation never moved" on a run where it moved and then correctly cooled off.
		// Guard rail 1 and guard rail 2 assert on the same cell at different times; sampling once at
		// the end cannot see both.
		static s32  s_r32_rel_at_crossing = 0;
		// The LAST kill's pressure add has not landed when the kill count reaches its target — see
		// the settle stage below.
		static u32  s_r32_settle_from = 0;
		static u32  s_r32_settle_want = 0;

		const u32 R32_MAX_KILLS   = 14;     // enough to cross a 50 bar at -7/kill, with margin
		const u32 R32_KILL_GAP_MS = 1500;
		const u32 R32_DECAY_WATCH_MS = 90000;
		// Bound on the settle wait. The condition below is what actually releases it; this only
		// stops a run where the tier never fires from waiting forever.
		const u32 R32_SETTLE_MAX_MS = 8000;

		if (!s_r32_init)
		{
			s_r32_init  = true;
			s_r32_armed = Device.dwTimeGlobal;
			LPCSTR p = coop_param("-coop_test_rep32");
			s_r32_ms = p ? u32(atoi(p) * 1000) : 60000;

			// RESUME MODE is leg D. The harness passes -coop_rep32_resume on the SECOND boot,
			// against the world the first boot saved. This leg does not kill anything: it reads
			// what the load restored and checks it against the game clock.
			s_r32_resume_mode = (strstr(Core.Params, "-coop_rep32_resume") != NULL);

			// Compress the clock's SCALE so decay is observable inside a test. Not the clock.
			// THE HALF-LIFE HAS TO CLEAR THE BURST, and run 2 measured why it did not.
			//
			// Game time here runs ~6x real (540060 ms of game time inside a 90 s window), so the
			// 1.5 s gap between kills is ~9 s of GAME time. At a 60 s half-life each gap decayed the
			// accumulator to 0.90 of itself, so -7 per kill converged on an asymptote near -70 and
			// peaked at -46 across 14 kills: it could not cross a bar of 50 however long the loop
			// ran. Decay was outrunning the burst.
			//
			// That is the griefer assertion holding — but by accident, and it left leg B (the bar
			// CAN be crossed) unproven, which is the one leg that separates a working bar from one
			// that refuses everything. At 600 s the burst loses ~1% per gap and crosses around kill
			// 8, while the 540 s decay window still shows a clear fall. Both legs become askable.
			coop_rep_test_set_pressure_tunables(/*bar*/ 50,
			                                    /*pressure halflife*/ 600ull * 1000ull,  // 10 game-min
			                                    /*overlay halflife*/ 300ull * 1000ull,   // 5 game-min
			                                    // A COARSER TICK, because truncation — not the
			                                    // half-life — dominates small magnitudes. Each tick
			                                    // rounds toward zero, so at a 5 s tick a 540 s window
			                                    // applies 108 truncations and anything under ~108
			                                    // reaches zero regardless of half-life: run 4's
			                                    // residual of -17 was gone long before 600 s implied,
			                                    // taking leg D's payload with it. At 60 s the window
			                                    // is ~9 steps, so -17 decays visibly and survives.
			                                    /*decay tick*/ 60ull * 1000ull);         // 60 game-sec
			Msg("- COOP(rep32): armed in %u ms  mode=%s  game_now=%I64u",
				s_r32_ms, s_r32_resume_mode ? "RESUME (leg D: the restart)" : "FRESH (legs A-C)",
				coop_rep_game_time_ms());
			FlushLog();
		}

		// ---------------- LEG D: the restart, measured ----------------
		if (s_r32_resume_mode && s_r32_stage != 2
		    && (Device.dwTimeGlobal - s_r32_armed) >= s_r32_ms)
		{
			s_r32_stage = 2;
			const int stalker_idx = int(CHARACTER_COMMUNITY::IdToIndex(
				"stalker", CHARACTER_COMMUNITY_INDEX(-1), true));
			const int actor_idx = int(CHARACTER_COMMUNITY::IdToIndex(
				"actor", CHARACTER_COMMUNITY_INDEX(-1), true));
			const s32 pressure_now = (stalker_idx >= 0 && actor_idx >= 0)
				? coop_rep_pressure_of(CHARACTER_COMMUNITY_INDEX(stalker_idx),
				                       CHARACTER_COMMUNITY_INDEX(actor_idx)) : 0;
			const u64 game_now = coop_rep_game_time_ms();
			// The load already printed the stamp it restored and the elapsed difference. This line
			// is what the harness compares against the SAVE line from the previous boot: same
			// stamp, later clock, and a pressure that has moved BECAUSE of the gap.
			Msg("- COOP(rep32): RESUME cells=%u pressure(stalker->actor)=%+d overlay_stamp=%I64u "
				"game_now=%I64u — compare stamp with the pre-restart save line; a fresh timer and a "
				"resumed one differ only there",
				coop_rep_pressure_cells(), pressure_now, coop_rep_overlay_stamp(), game_now);
			Msg("- COOP(rep32): DONE mode=RESUME kills=0 crossed=%u held=%u",
				coop_rep_pressure_crossed(), coop_rep_pressure_held());
			FlushLog();
		}

		// ---------------- LEGS A + B: kill in a loop, watch the bar ----------------
		if (!s_r32_resume_mode && s_r32_stage == 0
		    && (Device.dwTimeGlobal - s_r32_armed) >= s_r32_ms
		    && (Device.dwTimeGlobal - s_r32_next_kill_at) >= R32_KILL_GAP_MS)
		{
			const u16 player_id = coop_first_player_actor();
			CObject* const pobj = (player_id != mp_coop_owner::none)
			                      ? Level().Objects.net_Find(player_id) : NULL;
			CInventoryOwner* const pio = pobj ? smart_cast<CInventoryOwner*>(pobj) : NULL;

			// Same community every time, or the pressure would be spread over several pairs and
			// the bar would never be approached by any of them — a griefer loop that looks stopped
			// because it was never aimed.
			if (s_r32_vcomm < 0)
				s_r32_vcomm = int(CHARACTER_COMMUNITY::IdToIndex(
					"stalker", CHARACTER_COMMUNITY_INDEX(-1), true));

			u16 victim_id = mp_coop_owner::none;
			u32 live = 0;
			if (pio)
			{
				for (u32 i = 0; i < Level().Objects.o_count(); ++i)
				{
					CObject* const o = Level().Objects.o_get_by_iterator(i);
					if (!o || o->getDestroy() || o->ID() == player_id)
						continue;
					CEntityAlive* const alive = smart_cast<CEntityAlive*>(o);
					if (!alive || !alive->g_Alive() || !smart_cast<CAI_Stalker*>(o))
						continue;
					CInventoryOwner* const cio = smart_cast<CInventoryOwner*>(o);
					if (!cio || int(cio->Community()) != s_r32_vcomm)
						continue;
					++live;
					if (victim_id == mp_coop_owner::none)
						victim_id = o->ID();
				}
			}

			if (!pio || victim_id == mp_coop_owner::none)
			{
				if ((Device.dwTimeGlobal - s_r32_retry) >= 5000u)
				{
					s_r32_retry = Device.dwTimeGlobal;
					Msg("- COOP(rep32): waiting — player=%u live 'stalker' victims=%u kills so far=%u",
						u32(player_id), live, s_r32_kills);
				}
				if ((Device.dwTimeGlobal - s_r32_armed) > (s_r32_ms + 300000u))
				{
					s_r32_stage = 2;
					Msg("! COOP(rep32): no player with a live 'stalker' victim within 300 s — this "
						"run measured NOTHING (a harness failure, not a result)");
					FlushLog();
				}
			}
			else
			{
				CObject* const vobj = Level().Objects.net_Find(victim_id);
				CEntity* const ventity = vobj ? smart_cast<CEntity*>(vobj) : NULL;
				CInventoryOwner* const vio = vobj ? smart_cast<CInventoryOwner*>(vobj) : NULL;
				if (ventity && vio)
				{
					if (!s_r32_kills)
					{
						s_r32_player = player_id;

						// THE PLAYER NEEDS A REAL FACTION BEFORE THE FIRST KILL, and run 1 is why.
						//
						// Run 1 killed 14 stalkers, the shooter tier fired -140 every time, and the
						// pressure accumulator never saw a single unit: `killer_comm` was 0, the
						// `actor` pseudo-community every player shares, so R3.1's tier-3 guard
						// REFUSED the collective hit 14 times before the bar was ever consulted.
						// That refusal is correct — R3.0 established that a collective hit keyed on
						// `actor` would move `stalker -> actor` and make one player's kill hostile
						// for everybody — so the probe has to do what R3.1's leg 2 does and put the
						// player in a real faction, or it measures a bar that was never reached.
						//
						// Re-read rather than assumed: "I called SetCommunity" and "the player is in
						// that faction" are different claims, and only the second one licenses
						// reading a faction-tier result.
						const CHARACTER_COMMUNITY_INDEX chosen =
							CHARACTER_COMMUNITY::IdToIndex("actor_stalker", CHARACTER_COMMUNITY_INDEX(-1), true);
						const int before_set = int(pio->Community());
						if (chosen >= 0)
							pio->SetCommunity(chosen);
						s_r32_kcomm = int(pio->Community());
						Msg("- COOP(rep32): faction for the shooter: 'actor_stalker' index=%d; "
							"player community %d -> %d (re-read) took=%d — without this the "
							"collective tier is refused before the bar is consulted",
							int(chosen), before_set, s_r32_kcomm,
							(chosen >= 0 && s_r32_kcomm == int(chosen)) ? 1 : 0);
						s_r32_rel_at_start = (s_r32_vcomm >= 0 && s_r32_kcomm >= 0)
							? RELATION_REGISTRY().GetCommunityRelation(
								CHARACTER_COMMUNITY_INDEX(s_r32_vcomm),
								CHARACTER_COMMUNITY_INDEX(s_r32_kcomm)) : 0;
						Msg("- COOP(rep32): START player=%u victim_comm=%d killer_comm=%d "
							"relation_at_start=%d bar=%d game_now=%I64u",
							u32(player_id), s_r32_vcomm, s_r32_kcomm, s_r32_rel_at_start,
							coop_rep_pressure_bar(), coop_rep_game_time_ms());
					}

					const u32 moves_before = coop_rep_faction_moved_count();
					ventity->KillEntity(player_id);
					++s_r32_kills;
					s_r32_last_victim = victim_id;
					s_r32_next_kill_at = Device.dwTimeGlobal;

					// Sampled on the NEXT tick would be wrong: the propagation settles ~1 s after
					// the kill, so the harness reads the per-kill COOP(rep32) line below only after
					// the settle. Report what is true now and let the propagation's own lines carry
					// the rest.
					Msg("- COOP(rep32): kill %u/%u victim=%u — relation now %d, pressure %+d "
						"(bar %d) crossed=%u held=%u moves_total=%u",
						s_r32_kills, R32_MAX_KILLS, u32(victim_id),
						(s_r32_vcomm >= 0 && s_r32_kcomm >= 0)
							? RELATION_REGISTRY().GetCommunityRelation(
								CHARACTER_COMMUNITY_INDEX(s_r32_vcomm),
								CHARACTER_COMMUNITY_INDEX(s_r32_kcomm)) : 0,
						(s_r32_vcomm >= 0 && s_r32_kcomm >= 0)
							? coop_rep_pressure_of(CHARACTER_COMMUNITY_INDEX(s_r32_vcomm),
							                       CHARACTER_COMMUNITY_INDEX(s_r32_kcomm)) : 0,
						coop_rep_pressure_bar(), coop_rep_pressure_crossed(),
						coop_rep_pressure_held(), moves_before);
					FlushLog();

					if (s_r32_vcomm >= 0 && s_r32_kcomm >= 0)
					{
						const s32 pv = coop_rep_pressure_of(CHARACTER_COMMUNITY_INDEX(s_r32_vcomm),
						                                    CHARACTER_COMMUNITY_INDEX(s_r32_kcomm));
						if (_abs(pv) > _abs(s_r32_peak_pressure))
							s_r32_peak_pressure = pv;
					}
					s_r32_moves_seen = coop_rep_faction_moved_count();

					// STOP ON THE KILL COUNT ONLY — never on the crossing. Run 3 stopped the loop
					// the moment the bar was crossed, and crossing RESETS the accumulator to zero,
					// so the decay window always began from a freshly-zeroed cell: leg C measured
					// `+0 -> +0` and leg D saved `cells=0`. Both became unreachable exactly when
					// leg B started succeeding — the same shape as the half-life, moved from the
					// parameter into the sequencing.
					//
					// Running the full count instead means kills after the crossing rebuild a
					// residual (~3 x -7 against a bar of 50, so it cannot cross twice), which is
					// what leg C decays and what leg D carries across the restart.
					if (s_r32_kills >= R32_MAX_KILLS)
					{
						// Stop killing, but do NOT sample the decay baseline here: the kill above
						// was issued microseconds ago and its reputation propagation settles ~1 s
						// later (the comment on the per-kill line says so). Sampling now records
						// the pressure from BEFORE the final kill, so run 6 logged a window of
						// `-24 -> -16` whose ratio (67%) matches no half-life, while the cell that
						// actually decayed was the -31 the final kill produced 6 s of game time
						// INSIDE the window — making "with zero kills in the window" false as well.
						// Both were invisible while decay drained to zero, because zero is zero
						// whatever the baseline was; the formula-accurate decay is what exposed
						// them. Same class as the crossing-sample this probe already fixed once:
						// a baseline sampled at the wrong moment, not a wrong mechanism.
						//
						// So settle first. The release condition is OBSERVABLE — every kill has
						// produced a bar decision — rather than a duration guessed to cover it.
						s_r32_stage = 3;
						s_r32_settle_from = Device.dwTimeGlobal;
						s_r32_settle_want = s_r32_kills;
					}
				}
			}
		}

		// ---------------- SETTLE: let the final kill's pressure add land ----------------
		if (!s_r32_resume_mode && s_r32_stage == 3)
		{
			const u32 decided = coop_rep_pressure_held() + coop_rep_pressure_crossed();
			const bool landed  = (decided >= s_r32_settle_want);
			const bool expired = (Device.dwTimeGlobal - s_r32_settle_from) >= R32_SETTLE_MAX_MS;
			if (landed || expired)
			{
				// A run whose tier never fired decides nothing, so the bound must exist — but it
				// must also SAY it was hit, or a baseline sampled early looks identical to one
				// sampled correctly.
				Msg("- COOP(rep32): SETTLED after %u ms — %u of %u kills had reached a bar "
					"decision%s",
					Device.dwTimeGlobal - s_r32_settle_from, decided, s_r32_settle_want,
					landed ? "" : " — SETTLE BOUND HIT, the baseline below may predate the final "
					              "kill's pressure add");
				s_r32_stage = 1;
				s_r32_rel_at_crossing = (s_r32_vcomm >= 0 && s_r32_kcomm >= 0)
					? RELATION_REGISTRY().GetCommunityRelation(
						CHARACTER_COMMUNITY_INDEX(s_r32_vcomm),
						CHARACTER_COMMUNITY_INDEX(s_r32_kcomm)) : 0;
				s_r32_decay_started_at = Device.dwTimeGlobal;
				s_r32_decay_start_game = coop_rep_game_time_ms();
				s_r32_decay_start_val  = (s_r32_vcomm >= 0 && s_r32_kcomm >= 0)
					? coop_rep_pressure_of(CHARACTER_COMMUNITY_INDEX(s_r32_vcomm),
					                       CHARACTER_COMMUNITY_INDEX(s_r32_kcomm)) : 0;
				Msg("- COOP(rep32): KILLING DONE after %u kills — peak pressure %+d, "
					"crossed=%u held=%u, relation %d -> %d. Now watching decay with NO "
					"further kills from pressure %+d at game %I64u",
					s_r32_kills, s_r32_peak_pressure, coop_rep_pressure_crossed(),
					coop_rep_pressure_held(), s_r32_rel_at_start,
					(s_r32_vcomm >= 0 && s_r32_kcomm >= 0)
						? RELATION_REGISTRY().GetCommunityRelation(
							CHARACTER_COMMUNITY_INDEX(s_r32_vcomm),
							CHARACTER_COMMUNITY_INDEX(s_r32_kcomm)) : 0,
					s_r32_decay_start_val, s_r32_decay_start_game);
				FlushLog();
			}
		}

		// ---------------- LEG C: decay, with nothing being killed ----------------
		if (!s_r32_resume_mode && s_r32_stage == 1
		    && (Device.dwTimeGlobal - s_r32_decay_started_at) >= R32_DECAY_WATCH_MS)
		{
			s_r32_stage = 2;
			const s32 now_val = (s_r32_vcomm >= 0 && s_r32_kcomm >= 0)
				? coop_rep_pressure_of(CHARACTER_COMMUNITY_INDEX(s_r32_vcomm),
				                       CHARACTER_COMMUNITY_INDEX(s_r32_kcomm)) : 0;
			const u64 game_now = coop_rep_game_time_ms();
			Msg("- COOP(rep32): DECAY WATCH pressure %+d -> %+d over %I64u ms of game time "
				"(halflife %I64u ms) with zero kills in the window",
				s_r32_decay_start_val, now_val,
				(game_now > s_r32_decay_start_game) ? (game_now - s_r32_decay_start_game) : 0ull,
				coop_rep_pressure_halflife_ms());
			Msg("- COOP(rep32): DONE mode=FRESH kills=%u peak_pressure=%+d crossed=%u held=%u "
				"relation_start=%d relation_at_crossing=%d relation_end=%d pressure_end=%+d "
				"cells=%u overlay_stamp=%I64u game_now=%I64u",
				s_r32_kills, s_r32_peak_pressure, coop_rep_pressure_crossed(),
				coop_rep_pressure_held(), s_r32_rel_at_start, s_r32_rel_at_crossing,
				(s_r32_vcomm >= 0 && s_r32_kcomm >= 0)
					? RELATION_REGISTRY().GetCommunityRelation(
						CHARACTER_COMMUNITY_INDEX(s_r32_vcomm),
						CHARACTER_COMMUNITY_INDEX(s_r32_kcomm)) : 0,
				now_val, coop_rep_pressure_cells(), coop_rep_overlay_stamp(), game_now);
			FlushLog();
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

	// MP fork (§14 step 8 phase 3 Q2 / doc §7.3): this is THE world-state change the server
	// already tracks. A death reaches here as GE_DIE through xrServer::Process_event, i.e. it is
	// server-authoritative by construction — no new bookkeeping, no client is trusted for it — so
	// a task bound to an entity completes exactly when that entity dies, and exactly once (§7.2:
	// the entity dies once in the ONE world, so the reward is paid once).
	//
	// Placed AFTER alife().on_death so the A-Life side of the death has already been applied when
	// the completion broadcast goes out; a task that says "completed" before the world agrees is
	// the same lie as a task that never completes.
	if (xr_enet::enabled() && e_dest)
		coop_task_on_world_death(e_dest->ID, e_src ? e_src->ID : mp_coop_owner::none);
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
