////////////////////////////////////////////////////////////////////////////
//	Module 		: group_hierarchy_holder.cpp
//	Created 	: 12.11.2001
//  Modified 	: 03.09.2004
//	Author		: Dmitriy Iassenev, Oles Shishkovtsov, Aleksandr Maksimchuk
//	Description : Group hierarchy holder
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "group_hierarchy_holder.h"
#include "squad_hierarchy_holder.h"
#include "entity.h"
#include "agent_manager.h"
#include "agent_member_manager.h"
#include "agent_memory_manager.h"
#include "ai/stalker/ai_stalker.h"
#include "memory_manager.h"
#include "visual_memory_manager.h"
#include "sound_memory_manager.h"
#include "hit_memory_manager.h"

CGroupHierarchyHolder::~CGroupHierarchyHolder()
{
	VERIFY(m_members.empty());
	VERIFY(!m_visible_objects);
	VERIFY(!m_sound_objects);
	VERIFY(!m_hit_objects);
	VERIFY(!m_agent_manager);
}

#ifdef SQUAD_HIERARCHY_HOLDER_USE_LEADER
void CGroupHierarchyHolder::update_leader()
{
	m_leader = 0;
	MEMBER_REGISTRY::iterator I = m_members.begin();
	MEMBER_REGISTRY::iterator E = m_members.end();
	for (; I != E; ++I)
		if ((*I)->g_Alive())
		{
			m_leader = *I;
			break;
		}
}
#endif // SQUAD_HIERARCHY_HOLDER_USE_LEADER

void CGroupHierarchyHolder::register_in_group(CEntity* member)
{
	VERIFY(member);
	MEMBER_REGISTRY::iterator I = std::find(m_members.begin(), m_members.end(), member);
	VERIFY3(I == m_members.end(), "Specified group member has already been found", *member->cName());

	if (m_members.empty())
	{
		m_visible_objects = xr_new<VISIBLE_OBJECTS>();
		m_sound_objects = xr_new<SOUND_OBJECTS>();
		m_hit_objects = xr_new<HIT_OBJECTS>();

		//		m_visible_objects->reserve	(128);
		//		m_sound_objects->reserve	(128);
		//		m_hit_objects->reserve		(128);
	}

	m_members.push_back(member);
}

void CGroupHierarchyHolder::register_in_squad(CEntity* member)
{
#ifdef SQUAD_HIERARCHY_HOLDER_USE_LEADER
	if (!leader() && member->g_Alive())
	{
		m_leader = member;
		if (!squad().leader())
			squad().leader(member);
	}
#endif // SQUAD_HIERARCHY_HOLDER_USE_LEADER
}

void CGroupHierarchyHolder::register_in_agent_manager(CEntity* member)
{
	if (!get_agent_manager() && smart_cast<CAI_Stalker*>(member))
	{
		m_agent_manager = xr_new<CAgentManager>();
		agent_manager().memory().set_squad_objects(&visible_objects());
		agent_manager().memory().set_squad_objects(&sound_objects());
		agent_manager().memory().set_squad_objects(&hit_objects());
	}

	if (get_agent_manager())
		agent_manager().member().add(member);
}

void CGroupHierarchyHolder::register_in_group_senses(CEntity* member)
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(member);
	if (monster)
	{
		monster->memory().visual().set_squad_objects(&visible_objects());
		monster->memory().sound().set_squad_objects(&sound_objects());
		monster->memory().hit().set_squad_objects(&hit_objects());
	}
}

// ===== COOP (§14 step 8 P4 R7): erase(end()) IS WHAT KILLS THE CO-OP SERVER AFTER LEG 2 ==========
//
// This function's only protection against "member is not in this group" was `VERIFY3`, and VERIFY3
// COMPILES OUT IN RELEASE. So the shipped binary ran `m_members.erase(m_members.end())` — undefined
// behaviour, in practice a memmove of elements from past the end of the vector.
//
// That is not a theoretical concern. The first-chance access-violation reporter caught it with a
// stack, on the run that R3.1 has been dying on for five increments:
//
//     <wine CRT builtin, reading 0x7EE88D0D0000>      <- FAULTS
//     CGroupHierarchyHolder::unregister_member+0x38
//     CEntity::ChangeTeam+0x16e
//     CInventoryOwner::SetCommunity+0x16f
//     game_sv_Single::Update                          <- the R3.1 probe giving the player a faction
//     xrServer::Update -> CLevel::net_Update
//
// and the engine's own handler agreed, as the LAST line the server ever wrote:
//
//     !! unhandled exception 0xC0000005 at address 0x00006FFFFCA1240E (accessing 0x00007EE88D0D0000)
//
// It is UNHANDLED because it is not inside a Lua call — nothing swallows it, the process dies, and
// the tail of the log is the crash handler's console-variable dump. Which is why a log read from the
// bottom looks like a server that simply stopped.
//
// `CEntity::ChangeTeam` calls here unconditionally, guarded by another compiled-out
// `VERIFY(m_registered_member)` — unlike `CEntity::net_Destroy`, which tests that flag for real.
//
// THE FIX IS DELIBERATELY NARROW, AND IT DOES NOT PRETEND TO ANSWER THE REAL QUESTION. Skipping an
// erase that cannot be performed replaces undefined behaviour with a no-op, which is strictly better
// on every path — but *why* an entity reaches here unregistered is a separate defect, and silently
// swallowing it would hide the thing worth fixing. So it is REPORTED, bounded the same way the other
// co-op instruments here are bounded, rather than quietly returning.
void CGroupHierarchyHolder::unregister_in_group(CEntity* member)
{
	VERIFY(member);
	MEMBER_REGISTRY::iterator I = std::find(m_members.begin(), m_members.end(), member);
	if (I == m_members.end())
	{
		static u32 s_seen = 0;
		++s_seen;
		if (s_seen <= 8u || (s_seen % 256u) == 0u)
		{
			Msg("!COOP(group): unregister_in_group called for an entity that is NOT in this group — "
				"skipping the erase, because erase(end()) is undefined behaviour and is what faulted "
				"in CEntity::ChangeTeam. The hierarchy was already inconsistent before this call; "
				"that is the defect this line reports rather than fixes. entity=%s group has %u "
				"members. count=%u",
				*member->cName(), (u32)m_members.size(), s_seen);
			FlushLog();
		}
		return;
	}
	m_members.erase(I);
}

void CGroupHierarchyHolder::unregister_in_squad(CEntity* member)
{
#ifdef SQUAD_HIERARCHY_HOLDER_USE_LEADER
	if (leader() && (leader()->ID() == member->ID()))
	{
		update_leader();
		if (squad().leader()->ID() == member->ID())
			if (leader())
				squad().leader(leader());
			else
				squad().update_leader();
	}
#endif // SQUAD_HIERARCHY_HOLDER_USE_LEADER
}

void CGroupHierarchyHolder::unregister_in_agent_manager(CEntity* member)
{
	if (get_agent_manager())
	{
		agent_manager().member().remove(member);
		if (agent_manager().member().members().empty())
			xr_delete(m_agent_manager);
	}

	if (m_members.empty())
	{
		xr_delete(m_visible_objects);
		xr_delete(m_sound_objects);
		xr_delete(m_hit_objects);
	}
}

void CGroupHierarchyHolder::unregister_in_group_senses(CEntity* member)
{
	CCustomMonster* monster = smart_cast<CCustomMonster*>(member);
	if (monster)
	{
		monster->memory().visual().set_squad_objects(0);
		monster->memory().sound().set_squad_objects(0);
		monster->memory().hit().set_squad_objects(0);
	}
}

void CGroupHierarchyHolder::register_member(CEntity* member)
{
	register_in_group(member);
	register_in_squad(member);
	register_in_agent_manager(member);
	register_in_group_senses(member);
}

void CGroupHierarchyHolder::unregister_member(CEntity* member)
{
	unregister_in_group(member);
	unregister_in_squad(member);
	unregister_in_agent_manager(member);
	unregister_in_group_senses(member);
}
