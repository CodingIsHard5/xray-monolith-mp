#include "stdafx.h"
#include "game_sv_single.h"
#include "xrserver_objects_alife_monsters.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "alife_graph_registry.h"
#include "alife_time_manager.h"
#include "object_broker.h"
#include "gamepersistent.h"
#include "xrServer.h"
#include "ai_space.h"                              // MP fork: ai().alife()
#include "../xrNetServer/xr_enet_transport.h"      // MP fork: xr_enet::enabled()
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
void game_sv_Single::coop_poll_spawns()
{
	if (!xr_enet::enabled() || !ai().get_alife())
		return; // co-op (ENet) only; stock single-player untouched

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
		coop_spawn_actor_for(CL);
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


void game_sv_Single::Update()
{
	inherited::Update();
	coop_poll_spawns(); // MP fork (§14 co-op): give ready clients their own actor
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
