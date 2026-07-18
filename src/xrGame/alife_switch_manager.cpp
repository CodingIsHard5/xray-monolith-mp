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

// MP fork: attention-anchor registry (design doc §5.1). Kept here to
// avoid a new translation unit in the vcxproj.
namespace mp_anchors
{
	struct anchor { Fvector position; bool used; };
	static anchor s_anchors[max_anchors] = {};
	static u32 s_count = 0;

	void set(u32 idx, const Fvector& position)
	{
		if (idx >= max_anchors) return;
		if (!s_anchors[idx].used) ++s_count;
		s_anchors[idx].position = position;
		s_anchors[idx].used = true;
	}

	void clear(u32 idx)
	{
		if (idx >= max_anchors || !s_anchors[idx].used) return;
		s_anchors[idx].used = false;
		--s_count;
	}

	void clear_all()
	{
		for (u32 i = 0; i < max_anchors; ++i)
			s_anchors[i].used = false;
		s_count = 0;
	}

	u32 count() { return s_count; }

	float min_distance_to(const Fvector& pos, const Fvector& fallback_pos)
	{
		if (!s_count)
			return fallback_pos.distance_to(pos);
		float best = flt_max;
		for (u32 i = 0; i < max_anchors; ++i)
			if (s_anchors[i].used)
			{
				float d = s_anchors[i].position.distance_to(pos);
				if (d < best) best = d;
			}
		return best;
	}
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
		object->switch_online();
	STOP_PROFILE
}

void CALifeSwitchManager::switch_offline(CSE_ALifeDynamicObject* object)
{
	START_PROFILE("ALife/switch/switch_offline")
	// MP fork: was #ifdef DEBUG only (see switch_online above)
	if (strstr(Core.Params, "-dbg"))
		Msg							("[LSS][%d] Going offline [%d][%s][%d] ([%f][%f][%f] : [%f][%f][%f]), on '%s'",Device.dwFrame,Device.dwTimeGlobal,object->name_replace(), object->ID,VPUSH(graph().actor()->o_Position),VPUSH(object->o_Position), "*SERVER*");
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

// MP fork (§15 co-op diag): tally where objects drop out of the switch pipeline on
// the headless co-op server (nothing ever goes online — find out why). Throttled,
// co-op-gated. Remove once co-op A-Life switching works.
namespace
{
	struct coop_switch_diag
	{
		u32 calls = 0, redundant = 0, sync_fail = 0, off_attempts = 0, on_attempts = 0;
		u32 became_online = 0, became_offline = 0, first_lvl_id = 0xffffffff;
		u32 last_log_ms = 0;
	};
	static coop_switch_diag s_csd;
}

void CALifeSwitchManager::switch_object(CSE_ALifeDynamicObject* I)
{
	const bool coop = xr_enet::enabled();
	if (coop)
	{
		++s_csd.calls;
		s_csd.first_lvl_id = ai().game_graph().vertex(I->m_tGraphID)->level_id();
	}

	if (I->redundant())
	{
		if (coop) ++s_csd.redundant;
		release(I);
		return;
	}

	if (!synchronize_location(I))
	{
		if (coop) ++s_csd.sync_fail;
		// MP fork: an online object silently skipped here would explain a
		// "refuses to switch offline" state — make the skip visible
		if (I->m_bOnline && strstr(Core.Params, "-dbg"))
			Msg("[MPSW] [%d][%s] online but skipped: synchronize_location failed", I->ID, I->name_replace());
		return;
	}

	const bool was_online = !!I->m_bOnline;
	if (I->m_bOnline)
	{
		if (coop) ++s_csd.off_attempts;
		try_switch_offline(I);
	}
	else
	{
		if (coop) ++s_csd.on_attempts;
		try_switch_online(I);
	}

	if (coop)
	{
		if (!was_online && I->m_bOnline) ++s_csd.became_online;
		if (was_online && !I->m_bOnline) ++s_csd.became_offline;
		const u32 now = Device.dwTimeGlobal;
		if (now - s_csd.last_log_ms >= 5000)
		{
			s_csd.last_log_ms = now;
			Msg("- XRNET(diag): SWITCH tally: calls=%u redundant=%u sync_fail=%u on_try=%u off_try=%u ->online=%u ->offline=%u lastLvl=%u anchors=%u online_dist=%.0f",
				s_csd.calls, s_csd.redundant, s_csd.sync_fail, s_csd.on_attempts, s_csd.off_attempts,
				s_csd.became_online, s_csd.became_offline, s_csd.first_lvl_id,
				mp_anchors::count(), online_distance());
			FlushLog();
			s_csd.calls = s_csd.redundant = s_csd.sync_fail = 0;
			s_csd.off_attempts = s_csd.on_attempts = s_csd.became_online = s_csd.became_offline = 0;
		}
	}

	if (I->redundant())
		release(I);
}
