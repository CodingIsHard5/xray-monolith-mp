////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_online_offline_group.cpp
//	Created 	: 25.10.2005
//  Modified 	: 25.10.2005
//	Author		: Dmitriy Iassenev
//	Description : ALife Online Offline Group class
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "ai_space.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "alife_graph_registry.h"
#include "alife_schedule_registry.h"
#include "game_level_cross_table.h"
#include "alife_online_offline_group_brain.h"
#include "level_graph.h"
#include "Level.h"          // item (3) diagnostics: Level().Objects.net_Find, to count resolvable members
#include "alife_monster_movement_manager.h"
#include "alife_monster_detail_path_manager.h"
#include "mp_anchors.h" // MP fork: squads switch by anchor distance too

#pragma warning(push)
#pragma warning(disable:4995)
#include <malloc.h>
#pragma warning(pop)

extern void setup_location_types_line(GameGraph::TERRAIN_VECTOR& m_vertex_types, LPCSTR string);

CSE_ALifeItemWeapon* CSE_ALifeOnlineOfflineGroup::tpfGetBestWeapon(ALife::EHitType& tHitType, float& fHitPower)
{
	return (0);
}

ALife::EMeetActionType CSE_ALifeOnlineOfflineGroup::tfGetActionType(CSE_ALifeSchedulable* tpALifeSchedulable,
                                                                    int iGroupIndex, bool bMutualDetection)
{
	return (ALife::eMeetActionTypeIgnore);
}

bool CSE_ALifeOnlineOfflineGroup::bfActive()
{
	return (!m_bOnline && !m_members.empty());
}

CSE_ALifeDynamicObject* CSE_ALifeOnlineOfflineGroup::tpfGetBestDetector()
{
	return (0);
}

bool CSE_ALifeOnlineOfflineGroup::need_update(CSE_ALifeDynamicObject* object)
{
	return true;
}

void CSE_ALifeOnlineOfflineGroup::update()
{
    try
    {
        m_members.begin(); // force actualize
        if (m_bOnline && !m_members.empty())
        {
            MEMBER* commander = (*m_members.begin()).second;
            if (commander)
            {
                o_Position = commander->o_Position;
                m_tNodeID = commander->m_tNodeID;
                m_tGraphID = commander->m_tGraphID;
            }
        }
        if (!bfActive())
            return;

        brain().update();

        MEMBERS::iterator I = m_members.begin();
        MEMBERS::iterator E = m_members.end();
        for (; I != E; ++I)
        {
            MEMBER* ptr = ((*I).second);
            if (ptr)
            {
                ptr->o_Position = o_Position;
                ptr->m_tNodeID = m_tNodeID;
                ptr->m_tGraphID = m_tGraphID;
                ptr->m_fDistance = m_fDistance;
            }

        }
    }
    catch (...)
    {

    }
}

void CSE_ALifeOnlineOfflineGroup::on_location_change() const
{
	brain().on_location_change();
}


void CSE_ALifeOnlineOfflineGroup::register_member(ALife::_OBJECT_ID member_id)
{
	VERIFY(m_members.find(member_id) == m_members.end());
	CSE_ALifeDynamicObject* object = ai().alife().objects().object(member_id);
	CSE_ALifeMonsterAbstract* monster = smart_cast<CSE_ALifeMonsterAbstract*>(object);
	VERIFY(monster);
	VERIFY(monster->g_Alive());

	bool empty = m_members.empty();
	if (!object->m_bOnline)
	{
		if (m_bOnline)
		{
			object->switch_online();
			VERIFY(object->ID_Parent == 0xffff);
			alife().graph().level().remove(object);
		}
		else
		{
			alife().graph().remove(object, object->m_tGraphID);
			alife().scheduled().remove(object);
		}
	}
	else
	{
		if (!m_bOnline)
		{
			switch_online();
		}
		VERIFY(object->ID_Parent == 0xffff);
		alife().graph().level().remove(object);
	}
	VERIFY((monster->m_group_id == 0xffff) || (monster->m_group_id == ID));
	monster->m_group_id = ID;
	m_members.insert(std::make_pair(member_id, monster));

	if (!empty)
		return;

	o_Position = monster->o_Position;
	m_tNodeID = monster->m_tNodeID;
	m_tGraphID = monster->m_tGraphID;
	m_fGoingSpeed = monster->m_fGoingSpeed;
	m_fCurrentLevelGoingSpeed = monster->m_fCurrentLevelGoingSpeed;
	m_flags.set(flUsedAI_Locations,TRUE);
	alife().graph().update(this);
}

void CSE_ALifeOnlineOfflineGroup::unregister_member(ALife::_OBJECT_ID member_id)
{
	CALifeGraphRegistry& graph = alife().graph();
	//	CALifeLevelRegistry			&level = graph.level();

	MEMBERS::iterator I = m_members.find(member_id);
	VERIFY(I != m_members.end());
	VERIFY((*I).second->m_group_id == ID);
	(*I).second->m_group_id = 0xffff;

	graph.update((*I).second);
	alife().scheduled().add((*I).second);
	m_members.erase(I);

	if (m_members.empty())
	{
		m_flags.set(flUsedAI_Locations,FALSE);
	}
}

CSE_ALifeOnlineOfflineGroup::MEMBER* CSE_ALifeOnlineOfflineGroup::member(ALife::_OBJECT_ID member_id, bool no_assert)
{
	MEMBERS::iterator I = m_members.find(member_id);
	if (I == m_members.end())
	{
		if (!no_assert)
			Msg("! There is no member with id %d in the OnlineOfflineGroup id %d", member_id, ID);
		VERIFY(no_assert);
		return (0);
	}
	return ((*I).second);
}

bool CSE_ALifeOnlineOfflineGroup::synchronize_location()
{
    m_members.begin(); // force actualize
	if (m_bOnline && !m_members.empty())
	{
		MEMBER* member = (*m_members.begin()).second;
        if (member)
        {
            o_Position = member->o_Position;
            m_tNodeID = member->m_tNodeID;
            m_tGraphID = member->m_tGraphID;
            m_fDistance = member->m_fDistance;
        }
	}

	return (true);
}

// MP fork, item (3) diagnostics (2026-09-18) — DUMP ONLY, gated on -coop_anchordump.
// The [SQSW] lines say what a decision CONCLUDED; this says what the squad WAS when it concluded it. The open
// question is whether a squad that went online once and then fell silent is still online as a group while its
// members have no resolvable game object — so both halves are values here, not inferences.
static void coop_squad_state_dump(const char* tag, u16 id, LPCSTR nm, bool group_online, bool can_online,
	const CSE_ALifeOnlineOfflineGroup::MEMBERS& members)
{
	if (!strstr(Core.Params, "-coop_anchordump"))
		return;

	// resolvable is only meaningful with a level: without one it reads 0 for every member, which would look
	// exactly like the finding under test. The dump says which case it is rather than printing an ambiguous 0.
	const int have_level = g_pGameLevel ? 1 : 0;
	int resolvable = 0;
	int alive = 0;
	CSE_ALifeOnlineOfflineGroup::MEMBERS::const_iterator I = members.begin();
	CSE_ALifeOnlineOfflineGroup::MEMBERS::const_iterator E = members.end();
	for (; I != E; ++I)
	{
		CSE_ALifeOnlineOfflineGroup::MEMBER* const m = (*I).second;
		if (!m)
			continue;
		if (m->g_Alive())
			++alive;
		if (g_pGameLevel && Level().Objects.net_Find(m->ID))
			++resolvable;
	}
	Msg("[SQSTATE] %s [%d][%s]: group_online=%d members=%d alive=%d resolvable_game_objects=%d can_switch_online=%d have_level=%d",
		tag, id, nm, group_online ? 1 : 0, (int)members.size(), alive, resolvable, can_online ? 1 : 0, have_level);
}

void CSE_ALifeOnlineOfflineGroup::try_switch_online()
{
	// Item (3): a squad that is VISITED but does nothing is indistinguishable in the log from one never visited.
	// Record the visit itself, before any gate, so "one decision then silence" separates into the two.
	if (strstr(Core.Params, "-coop_anchordump"))
		Msg("[SQVISIT] [%d][%s] try_switch_online entered: group_online=%d members=%d",
			ID, name_replace(), m_bOnline ? 1 : 0, (int)m_members.size());

	// MP fork (§4 A-Life squad decisions, DIAGNOSTIC): why don't anchored squads switch online?
	// Log the gate results + the nearest member's anchor distance vs online_distance. -dbg gated,
	// throttled by a per-object frame stamp so it doesn't spam every switch cycle.
	const bool mpsw_dbg = strstr(Core.Params, "-coop_sqsw") != nullptr;
	if (m_members.empty())
	{
		if (mpsw_dbg) Msg("[SQSW] [%d][%s] no members", ID, name_replace());
		return;
	}

	if (!can_switch_online())
	{
		if (mpsw_dbg) Msg("[SQSW] [%d][%s] can_switch_online=FALSE", ID, name_replace());
		return;
	}

	if (!can_switch_offline())
	{
		inherited1::try_switch_online();
		return;
	}
	float best_d = flt_max;
	MEMBERS::iterator I = m_members.begin();
	MEMBERS::iterator E = m_members.end();
	for (; I != E; ++I)
	{
		VERIFY3((*I).second->g_Alive(), "Incorrect situation : some of the OnlineOffline group members is dead",
		        (*I).second->name_replace());
		VERIFY3((*I).second->can_switch_online(),
		        "Incorrect situation : some of the OnlineOffline group members cannot be switched online due to their personal properties",
		        (*I).second->name_replace());
		VERIFY3((*I).second->can_switch_offline(),
		        "Incorrect situation : some of the OnlineOffline group members cannot be switched online due to their personal properties",
		        (*I).second->name_replace());
		const float d = mp_anchors::min_distance_to((*I).second->o_Position, alife().graph().actor()->o_Position);
		if (d < best_d) best_d = d;
		if (d > alife().online_distance())
		{
			continue;
		}
		if (mpsw_dbg) Msg("[SQSW] [%d][%s] SWITCHING ONLINE: member d=%.0f <= online_dist=%.0f (anchors=%d)",
			ID, name_replace(), d, alife().online_distance(), mp_anchors::count());
		coop_anchor_dump("squad-switching-online", ID, (*I).second->o_Position,
			alife().graph().actor() ? alife().graph().actor()->o_Position : o_Position);
		coop_squad_state_dump("switching-online", ID, name_replace(), m_bOnline, can_switch_online(), m_members);
		inherited1::try_switch_online();
		// AFTER the attempt, which is the state the "before" dump cannot report. The 2026-09-18 dump printed
		// group_online=0 at the decision and I could not tell whether that meant the switch had not happened yet
		// or had failed — the instrument could not answer its own question. Both sides are now values.
		coop_squad_state_dump("after-switch-attempt", ID, name_replace(), m_bOnline, can_switch_online(), m_members);
		return;
	}
	if (mpsw_dbg) Msg("[SQSW] [%d][%s] no member in range: best_d=%.0f > online_dist=%.0f (members=%d anchors=%d)",
		ID, name_replace(), best_d, alife().online_distance(), (int)m_members.size(), mp_anchors::count());
	// Item (3) diagnostics: best_d is a MINIMUM, so it cannot show which anchor produced it or whether some
	// anchor is in the wrong place. Dump every slot against this squad's own position, plus the squad's state,
	// so "the anchor is wrong" separates from "this squad is never re-evaluated". Diagnostic only.
	coop_anchor_dump("squad-no-member-in-range", ID, o_Position,
		alife().graph().actor() ? alife().graph().actor()->o_Position : o_Position);
	coop_squad_state_dump("no-member-in-range", ID, name_replace(), m_bOnline, can_switch_online(), m_members);
	on_failed_switch_online();
}

void CSE_ALifeOnlineOfflineGroup::try_switch_offline()
{
	// MP fork, item (3) diagnostics (2026-09-18). An ONLINE group takes THIS branch, not try_switch_online, so
	// [SQVISIT] could never see it — which is why "our squad 1 visit, all squads 13,000" looked like a result
	// and was not. This is the missing half.
	const bool sqdbg = strstr(Core.Params, "-coop_anchordump") != nullptr;
	if (sqdbg)
		Msg("[SQOFF] [%d][%s] try_switch_offline entered: members=%d online=%d",
			ID, name_replace(), (int)m_members.size(), m_bOnline ? 1 : 0);
	if (m_members.empty())
		return;

	if (!can_switch_offline())
		return;

	if (!can_switch_online())
	{
		alife().switch_offline(this);
		return;
	}

	MEMBERS::iterator I = m_members.begin();
	MEMBERS::iterator E = m_members.end();
	for (; I != E; ++I)
	{
		VERIFY3((*I).second->g_Alive(), "Incorrect situation : some of the OnlineOffline group members is dead",
		        (*I).second->name_replace());
		VERIFY3((*I).second->can_switch_offline(),
		        "Incorrect situation : some of the OnlineOffline group members cannot be switched online due to their personal properties",
		        (*I).second->name_replace());
		VERIFY3((*I).second->can_switch_online(),
		        "Incorrect situation : some of the OnlineOffline group members cannot be switched online due to their personal properties",
		        (*I).second->name_replace());

		// THE DECISION, AS A VALUE. "returns to its smart terrain and goes offline there" is where the co-op
		// question sits: with a client 6 m away the anchors should hold it online, and this line is what says
		// whether the anchors were consulted for THIS group at all. min_distance_to uses the anchor registry
		// and falls back to the graph actor ONLY when the registry is empty, so the anchor count is the tell.
		const float d_off = mp_anchors::min_distance_to((*I).second->o_Position, alife().graph().actor()->o_Position);
		if (sqdbg)
			Msg("[SQOFF]   member %d at %.1f,%.1f,%.1f: d=%.1f vs offline_dist=%.1f (anchors=%d players=%d) -> %s",
				(*I).second->ID, VPUSH((*I).second->o_Position), d_off, alife().offline_distance(),
				mp_anchors::count(), mp_anchors::player_count(),
				(d_off <= alife().offline_distance()) ? "STAYS ONLINE" : "would go offline");
		if (d_off <= alife().offline_distance())
			return;
	}

	if (sqdbg)
		Msg("[SQOFF] [%d][%s] SWITCHING OFFLINE: no member within offline_dist=%.1f (anchors=%d players=%d)",
			ID, name_replace(), alife().offline_distance(), mp_anchors::count(), mp_anchors::player_count());
	alife().switch_offline(this);
}

void CSE_ALifeOnlineOfflineGroup::switch_online()
{
	R_ASSERT(!m_bOnline);
	m_bOnline = true;

	MEMBERS::iterator I = m_members.begin();
	MEMBERS::iterator E = m_members.end();
	for (; I != E; ++I)
	{
		if ((*I).second->m_bOnline == false)
		{
			// "the member got a game object" has only ever been INFERRED from a placement succeeding. Name it.
			// The object is created through server().Process_spawn and is NOT synchronous, so this records the
			// REQUEST; whether an object exists is a separate question the probe answers.
			if (strstr(Core.Params, "-coop_anchordump"))
				Msg("[SQSPAWN] group %d: add_online requested for member %d at %.1f,%.1f,%.1f",
					ID, (*I).second->ID, VPUSH((*I).second->o_Position));
			alife().add_online((*I).second, false);
		}
	}

	alife().scheduled().remove(this);
	alife().graph().remove(this, m_tGraphID, false);
}

void CSE_ALifeOnlineOfflineGroup::switch_offline()
{
	R_ASSERT(m_bOnline);
	if (strstr(Core.Params, "-coop_anchordump"))
		Msg("[SQOFF] [%d][%s] switch_offline: group going OFFLINE with %d member(s)",
			ID, name_replace(), (int)m_members.size());
	m_bOnline = false;

	if (!m_members.empty())
	{
		MEMBER* member = (*m_members.begin()).second;

		member->synchronize_location();

		o_Position = member->o_Position;
		m_tNodeID = member->m_tNodeID;
		m_tGraphID = member->m_tGraphID;
		m_fDistance = member->m_fDistance;
	}

	MEMBERS::iterator I = m_members.begin();
	MEMBERS::iterator E = m_members.end();
	for (; I != E; ++I)
	{
		if ((*I).second->m_bOnline == true)
		{
			(*I).second->clear_client_data();
			alife().remove_online((*I).second, false);
		}
	}

	alife().scheduled().add(this);
	alife().graph().add(this, m_tGraphID, false);
}

bool CSE_ALifeOnlineOfflineGroup::redundant() const
{
	return (m_members.empty());
}

void CSE_ALifeOnlineOfflineGroup::notify_on_member_death(MEMBER* member)
{
	unregister_member(member->ID);
}

void CSE_ALifeOnlineOfflineGroup::on_before_register()
{
	m_tGraphID = GameGraph::_GRAPH_ID(-1);
	m_flags.set(flUsedAI_Locations,FALSE);
}

void CSE_ALifeOnlineOfflineGroup::on_after_game_load()
{
	if (m_members.empty())
		return;

	ALife::_OBJECT_ID* temp = (ALife::_OBJECT_ID*)_alloca(m_members.size() * sizeof(ALife::_OBJECT_ID));
	ALife::_OBJECT_ID *i = temp, *e = temp + m_members.size();

	{
		MEMBERS::const_iterator I = m_members.begin();
		MEMBERS::const_iterator E = m_members.end();
		for (; I != E; ++I, ++i)
		{
			VERIFY(!(*I).second);
			*i = (*I).first;
		}
	}

	m_members.clear();

	for (i = temp; i != e; ++i)
		register_member(*i);
}

ALife::_OBJECT_ID CSE_ALifeOnlineOfflineGroup::commander_id()
{
	if (!m_members.empty())
		return (*m_members.begin()).first;
	return 0xffff;
}

CSE_ALifeOnlineOfflineGroup::MEMBERS const& CSE_ALifeOnlineOfflineGroup::squad_members() const
{
	return m_members;
}

u32 CSE_ALifeOnlineOfflineGroup::npc_count() const
{
	return m_members.size();
}

void CSE_ALifeOnlineOfflineGroup::clear_location_types()
{
	m_tpaTerrain.clear();
	MEMBERS::iterator I = m_members.begin();
	MEMBERS::iterator E = m_members.end();
	for (; I != E; ++I)
	{
		(*I).second->m_tpaTerrain.clear();
	}
}


void CSE_ALifeOnlineOfflineGroup::add_location_type(LPCSTR mask)
{
	setup_location_types_line(m_tpaTerrain, mask);
	MEMBERS::iterator I = m_members.begin();
	MEMBERS::iterator E = m_members.end();
	for (; I != E; ++I)
	{
		setup_location_types_line((*I).second->m_tpaTerrain, mask);
	}
}

void CSE_ALifeOnlineOfflineGroup::force_change_position(Fvector position)
{
	u32 new_level_vertex = ai().level_graph().vertex_id(position);
	GameGraph::_GRAPH_ID new_graph_vertex = ai().cross_table().vertex(new_level_vertex).game_vertex_id();
	o_Position = position;
	m_tNodeID = new_level_vertex;
	if (m_tGraphID != new_graph_vertex)
	{
		alife().graph().change(this, m_tGraphID, new_graph_vertex);
	}

	//m_tGraphID				= new_graph_vertex;
}

void CSE_ALifeOnlineOfflineGroup::on_failed_switch_online()
{
	MEMBERS::const_iterator I = m_members.begin();
	MEMBERS::const_iterator E = m_members.end();
	for (; I != E; ++I)
	{
		(*I).second->clear_client_data();
	}
}
