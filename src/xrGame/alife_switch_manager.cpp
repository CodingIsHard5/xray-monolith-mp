////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_switch_manager.cpp
//	Created 	: 25.12.2002
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator switch manager
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "alife_switch_manager.h"
#include "xrServer_Objects_ALife.h"
#include "alife_graph_registry.h"
#include "alife_object_registry.h"
#include "alife_schedule_registry.h"
#include "game_level_cross_table.h"
#include "xrserver.h"
#include "ai_space.h"
#include "level_graph.h"
#include "mp_anchors.h"
#include "../xrNetServer/xr_enet_transport.h"   // MP fork (§15 diag): xr_enet::enabled()
#include "xrServer_Objects_ALife_Monsters.h"      // MP fork (§10.3 S3a): trader switch log

// MP fork (design doc §10.3 S3a): every online/offline switch of a trader NPC on the co-op server, with the connected player
// nearest to it (the anchor that explains the switch) and the stock it carries across.
struct coop_nearest_player_finder
{
	Fvector at;
	u16 id;
	float dist;
	xrServer* server;
	void operator()(IClient* client)
	{
		xrClientData* const CL = static_cast<xrClientData*>(client);
		if (CL == server->GetServerClient() || !CL->owner)
			return;
		const float d = CL->owner->o_Position.distance_to(at);
		if (d < dist)
		{
			dist = d;
			id = CL->owner->ID;
		}
	}
};

static void coop_trader_switch_log(CSE_ALifeDynamicObject* object, bool online, xrServer* server, float on_d, float off_d)
{
	if (!xr_enet::enabled() || !object || !server)
		return;
	CSE_ALifeTraderAbstract* const t = smart_cast<CSE_ALifeTraderAbstract*>(object);
	if (!t || smart_cast<CSE_ALifeCreatureActor*>(object) || xr_strcmp(t->CommunityName(), "trader"))
		return;
	coop_nearest_player_finder f;
	f.at = object->o_Position;
	f.id = 0xffff;
	f.dist = flt_max;
	f.server = server;
	server->ForEachClientDo(f);
	Msg("- COOP(trader-switch): %s [%u] %s — nearest player %u at %.1f m (online within %.0f m, offline beyond %.0f m), stock %u",
		object->name_replace(), u32(object->ID), online ? "ONLINE" : "OFFLINE", u32(f.id), f.id == 0xffff ? -1.f : f.dist, on_d, off_d,
		u32(object->children.size()));
}

// MP fork: attention-anchor registry (design doc §5.1). Kept here to
// avoid a new translation unit in the vcxproj.
namespace mp_anchors
{
	static registry<Fvector>& reg()
	{
		static registry<Fvector> r;
		return r;
	}

	void set(u32 idx, const Fvector& position) { reg().set(idx, position); }
	void set(u32 idx, const Fvector& position, int source, bool stale) { reg().set(idx, position, source, stale); }
	bool slot_used(u32 idx) { return idx < max_anchors && reg().slots[idx].used; }
	Fvector slot_position(u32 idx) { return reg().slots[idx].position; }
	int slot_source(u32 idx) { return idx < max_anchors ? reg().slots[idx].source : -1; }
	bool slot_stale(u32 idx) { return idx < max_anchors && reg().slots[idx].stale; }
	void clear(u32 idx) { reg().clear(idx); }
	void clear_all() { reg().clear_all(); }
	// MP fork (§4): clear only the per-actor range [0, n), leaving persistent gamedata anchors [n, max).
	void clear_below(u32 n) { reg().clear_below(n); }
	void clear_range(u32 from, u32 to) { reg().clear_range(from, to); }
	u32 count() { return reg().n; }
	u32 player_count() { return reg().player_count(); }
	bool emptied() { return reg().emptied(); }
	void set_empty_offline(bool on) { reg().empty_offline = on; }

	float min_distance_to(const Fvector& pos, const Fvector& fallback_pos)
	{
		return reg().min_distance_to(pos, fallback_pos, flt_max);
	}
}

// MP fork, item (3) diagnostics (2026-09-18) — DUMP ONLY, consulted by nothing.
// Prints every anchor slot with the position it holds, which client fed it, whether that client's owner had a
// resolvable game object at the time, and the distance from `pos` to THAT slot specifically. The per-slot
// distance is the value the [SQSW] line's single best_d cannot show: with any anchor registered,
// min_distance_to ignores the fallback entirely, so a wrong anchor position is invisible in the aggregate.
void coop_anchor_dump(const char* tag, u16 id, const Fvector& pos, const Fvector& fallback)
{
	if (!strstr(Core.Params, "-coop_anchordump"))
		return;

	string256 head;
	xr_sprintf(head, sizeof(head), "[ANCHORS] %s [%d] at %.1f,%.1f,%.1f: slots=%d players=%d emptied=%d",
		tag, id, VPUSH(pos), mp_anchors::count(), mp_anchors::player_count(), mp_anchors::emptied() ? 1 : 0);
	Msg("%s", head);
	Msg("[ANCHORS]   fallback (graph actor / object 0) at %.1f,%.1f,%.1f: d=%.1f",
		VPUSH(fallback), fallback.distance_to(pos));

	bool any = false;
	for (u32 i = 0; i < mp_anchors::max_anchors; ++i)
	{
		if (!mp_anchors::slot_used(i))
			continue;
		any = true;
		const Fvector p = mp_anchors::slot_position(i);
		Msg("[ANCHORS]   slot %d %s src %d stale %d at %.1f,%.1f,%.1f: d=%.1f", i,
			i < mp_anchors::gamedata_anchor_base ? "player " : "gamedata",
			mp_anchors::slot_source(i), mp_anchors::slot_stale(i) ? 1 : 0, VPUSH(p), p.distance_to(pos));
	}
	if (!any)
		Msg("[ANCHORS]   (no slots in use — the fallback above is what min_distance_to returns)");
}

#ifdef DEBUG
#	include "level.h"
#endif // DEBUG

using namespace ALife;

struct remove_non_alife_controlled_predicate
{
	xrServer* m_server;

	IC remove_non_alife_controlled_predicate(xrServer* server)
	{
		VERIFY(server);
		m_server = server;
	}

	IC bool operator()(const ALife::_OBJECT_ID& id) const
	{
		CSE_Abstract* object = m_server->game->get_entity_from_eid(id);
		return (!object || !object->m_bALifeControl);
	}
};

CALifeSwitchManager::~CALifeSwitchManager()
{
}

void CALifeSwitchManager::add_online(CSE_ALifeDynamicObject* object, bool update_registries)
{
	START_PROFILE("ALife/switch/add_online")
		VERIFY((ai().game_graph().vertex(object->m_tGraphID)->level_id() == graph().level().level_id()));

		object->m_bOnline = true;

		NET_Packet tNetPacket;
		CSE_Abstract* l_tpAbstract = smart_cast<CSE_Abstract*>(object);
		server().entity_Destroy(l_tpAbstract);
		object->s_flags.or(M_SPAWN_UPDATE);
		ClientID clientID;
		clientID.set(server().GetServerClient() ? server().GetServerClient()->ID.value() : 0);
		server().Process_spawn(tNetPacket, clientID,FALSE, l_tpAbstract);
		object->s_flags.and(u16(-1) ^ M_SPAWN_UPDATE);

		//Alundaio: Knowing last object to spawn can be very useful to debugging
		if (strstr(Core.Params, "-dbg"))
			Msg("[LSS] Spawning object [%s][%s][%d]", object->name_replace(), *object->s_name, object->ID);

		//Alundaio: Workaround for crash with corpses that end up outside AI map
		//R_ASSERT2(!object->used_ai_locations() || ai().level_graph().valid_vertex_id(object->m_tNodeID), make_string("Invalid vertex for object %s", object->name_replace()));

		object->add_online(update_registries);
	STOP_PROFILE
}

void CALifeSwitchManager::remove_online(CSE_ALifeDynamicObject* object, bool update_registries)
{
	START_PROFILE("ALife/switch/remove_online")
		object->m_bOnline = false;

		// Drop client-only children (m_bALifeControl=false, e.g. an online npc's
		// bolt): they aren't in objects(), so add_offline_impl can't handle them.
		m_saved_chidren = object->children;
		m_saved_chidren.erase(
			std::remove_if(
				m_saved_chidren.begin(),
				m_saved_chidren.end(),
				remove_non_alife_controlled_predicate(&server())
			),
			m_saved_chidren.end()
		);

		server().Perform_destroy(object, net_flags(TRUE,TRUE));
		VERIFY(object->children.empty());

		_OBJECT_ID object_id = object->ID;
		object->ID = server().PerformIDgen(object_id);

#ifdef DEBUG
	if (psAI_Flags.test(aiALife))
		Msg						("[LSS] Destroying object [%s][%s][%d]",object->name_replace(),*object->s_name,object->ID);
#endif

		object->add_offline(m_saved_chidren, update_registries);
	STOP_PROFILE
}

void CALifeSwitchManager::switch_online(CSE_ALifeDynamicObject* object)
{
	START_PROFILE("ALife/switch/switch_online")
	// MP fork: was #ifdef DEBUG only — Release builds logged no manager
	// switches at all (boot 20's "zero [LSS] lines" mystery)
	if (strstr(Core.Params, "-dbg"))
		Msg						("[LSS][%d] Going online [%d][%s][%d] ([%f][%f][%f] : [%f][%f][%f]), on '%s'",Device.dwFrame,Device.dwTimeGlobal,object->name_replace(), object->ID,VPUSH(graph().actor()->o_Position),VPUSH(object->o_Position), "*SERVER*");
		coop_trader_switch_log(object, true, &server(), online_distance(), offline_distance());
		object->switch_online();
	STOP_PROFILE
}

void CALifeSwitchManager::switch_offline(CSE_ALifeDynamicObject* object)
{
	START_PROFILE("ALife/switch/switch_offline")
	// MP fork: was #ifdef DEBUG only (see switch_online above)
	if (strstr(Core.Params, "-dbg"))
		Msg							("[LSS][%d] Going offline [%d][%s][%d] ([%f][%f][%f] : [%f][%f][%f]), on '%s'",Device.dwFrame,Device.dwTimeGlobal,object->name_replace(), object->ID,VPUSH(graph().actor()->o_Position),VPUSH(object->o_Position), "*SERVER*");
		coop_trader_switch_log(object, false, &server(), online_distance(), offline_distance());
		object->switch_offline();
	STOP_PROFILE
}

bool CALifeSwitchManager::synchronize_location(CSE_ALifeDynamicObject* I)
{
	START_PROFILE("ALife/switch/synchronize_location")
#ifdef DEBUG
	VERIFY3					(ai().level_graph().level_id() == ai().game_graph().vertex(I->m_tGraphID)->level_id(),*I->s_name,I->name_replace());
	if (!I->children.empty()) {
		u32					size = I->children.size();
		ALife::_OBJECT_ID	*test = (ALife::_OBJECT_ID*)_alloca(size*sizeof(ALife::_OBJECT_ID));
		Memory.mem_copy		(test,&*I->children.begin(),size*sizeof(ALife::_OBJECT_ID));
		std::sort			(test,test + size);
		for (u32 i=1; i<size; ++i) {
			VERIFY3			(test[i - 1] != test[i],"Child is registered twice in the child list",(*I).name_replace());
		}
	}
#endif // DEBUG

		// check if we do not use ai locations
		if (!I->used_ai_locations())
			return (true);

		// check if we are not attached
		if (0xffff != I->ID_Parent)
			return (true);

		// check if we are not online and have an invalid level vertex id
		if (!I->m_bOnline && !ai().level_graph().valid_vertex_id(I->m_tNodeID))
			return (true);

		return ((*I).synchronize_location());
	STOP_PROFILE
}

void CALifeSwitchManager::try_switch_online(CSE_ALifeDynamicObject* I)
{
	START_PROFILE("ALife/switch/try_switch_online")
		// so, the object is offline
		// checking if the object is not attached
		if (0xffff != I->ID_Parent)
		{
			// so, object is attached
			// checking if parent is offline too
#ifdef DEBUG
		if (psAI_Flags.test(aiALife)) {
			CSE_ALifeCreatureAbstract	*l_tpALifeCreatureAbstract = smart_cast<CSE_ALifeCreatureAbstract*>(objects().object(I->ID_Parent));
			if (l_tpALifeCreatureAbstract && (l_tpALifeCreatureAbstract->get_health() < EPS_L))
				Msg				("! uncontrolled situation [%d][%d][%s][%f]",I->ID,I->ID_Parent,l_tpALifeCreatureAbstract->name_replace(),l_tpALifeCreatureAbstract->get_health());
			VERIFY2				(!l_tpALifeCreatureAbstract || (l_tpALifeCreatureAbstract->get_health() >= EPS_L),"Parent online, item offline...");
			if (objects().object(I->ID_Parent)->m_bOnline)
				Msg				("! uncontrolled situation [%d][%d][%s][%f]",I->ID,I->ID_Parent,l_tpALifeCreatureAbstract->name_replace(),l_tpALifeCreatureAbstract->get_health());
		}
		VERIFY2					(!objects().object(I->ID_Parent)->m_bOnline,"Parent online, item offline...");
#endif
			return;
		}
#ifdef DEBUG
		VERIFY2(
			(
				ai().game_graph().vertex(I->m_tGraphID)->level_id()
				!=
				ai().level_graph().level_id()
			) ||
			!Level().Objects.net_Find(I->ID) ||
			Level().Objects.dump_all_objects(),
			make_string("frame [%d] time [%d] object [%s] with id [%d] is offline, but is on the level",Device.dwFrame,
				Device.dwTimeGlobal,I->name_replace(),I->ID)
		);
#endif
		I->try_switch_online();

		if (!I->m_bOnline && !I->keep_saved_data_anyway())
			I->clear_client_data();

	STOP_PROFILE
}

void CALifeSwitchManager::try_switch_offline(CSE_ALifeDynamicObject* I)
{
	START_PROFILE("ALife/switch/try_switch_offline")
		// checking if the object is not attached
		if (0xffff != I->ID_Parent)
		{
#ifdef DEBUG
		// checking if parent is online too
		CSE_ALifeCreatureAbstract	*l_tpALifeCreatureAbstract = smart_cast<CSE_ALifeCreatureAbstract*>(objects().object(I->ID_Parent));
		if (l_tpALifeCreatureAbstract && (l_tpALifeCreatureAbstract->get_health() < EPS_L))
			Msg				("! uncontrolled situation [%d][%d][%s][%f]",I->ID,I->ID_Parent,l_tpALifeCreatureAbstract->name_replace(),l_tpALifeCreatureAbstract->get_health());

		VERIFY2				(!smart_cast<CSE_ALifeCreatureAbstract*>(objects().object(I->ID_Parent)) || (smart_cast<CSE_ALifeCreatureAbstract*>(objects().object(I->ID_Parent))->get_health() >= EPS_L),"Parent offline, item online...");

		if (!objects().object(I->ID_Parent)->m_bOnline)
			Msg				("! uncontrolled situation [%d][%d][%s][%f]",I->ID,I->ID_Parent,l_tpALifeCreatureAbstract->name_replace(),l_tpALifeCreatureAbstract->get_health());

		VERIFY2				(objects().object(I->ID_Parent)->m_bOnline,"Parent offline, item online...");
#endif
			return;
		}

		I->try_switch_offline();
	STOP_PROFILE
}

void CALifeSwitchManager::switch_object(CSE_ALifeDynamicObject* I)
{
	if (I->redundant())
	{
		release(I);
		return;
	}

	if (!synchronize_location(I))
	{
		// MP fork: an online object silently skipped here would explain a
		// "refuses to switch offline" state — make the skip visible
		if (I->m_bOnline && strstr(Core.Params, "-dbg"))
			Msg("[MPSW] [%d][%s] online but skipped: synchronize_location failed", I->ID, I->name_replace());
		return;
	}

	if (I->m_bOnline)
		try_switch_offline(I);
	else
		try_switch_online(I);

	if (I->redundant())
		release(I);
}
