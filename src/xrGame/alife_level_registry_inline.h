////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_level_registry_inline.h
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife level registry inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "ai_space.h"

IC CALifeLevelRegistry::CALifeLevelRegistry(const GameGraph::_LEVEL_ID& level_id)
{
	m_level_id = level_id;
}

IC GameGraph::_LEVEL_ID CALifeLevelRegistry::level_id() const
{
	return (m_level_id);
}

// MP fork, item (3): defined in alife_dynamic_object.cpp. Same extern pattern as coop_functor_missing.
extern bool coop_is_watched(const CSE_ALifeDynamicObject* object);

IC void CALifeLevelRegistry::add(CSE_ALifeDynamicObject* object)
{
	// THE SUSPECTED DROP. This registry is what the A-Life switch scan walks, and an object whose graph vertex
	// belongs to a DIFFERENT level is discarded here without a word. A grouped member teleported to a job on a
	// neighbouring level would therefore never be scanned, never reach try_switch_online, and never come back —
	// which is exactly the measured state: member 29958 sat 3.2 m from a player, offline, and produced no
	// [SYNCLOC] and no [SWON] line at all while 24 other grouped members were scanned normally.
	if (ai().game_graph().vertex(object->m_tGraphID)->level_id() != level_id())
	{
		if (coop_is_watched(object))
		{
			GameGraph::_LEVEL_ID const target = ai().game_graph().vertex(object->m_tGraphID)->level_id();
			Msg("[LVLREG] %d DROPPED from level registry: vertex %d is on level %d [%s], current level is %d [%s]",
				object->ID, (int)object->m_tGraphID, (int)target,
				*(ai().game_graph().header().level(target).name()),
				(int)level_id(), *(ai().game_graph().header().level(level_id()).name()));
		}
		return;
	}
	if (coop_is_watched(object))
	{
		Msg("[LVLREG] %d ADDED to level registry: vertex %d, level %d [%s]",
			object->ID, (int)object->m_tGraphID, (int)level_id(),
			*(ai().game_graph().header().level(level_id()).name()));
	}

#ifdef DEBUG
	if (psAI_Flags.test(aiALife)) {
		Msg				("[LSS] adding object [%s][%d] to current level",object->name_replace(),object->ID);
	}
#endif
	inherited::add(object->ID, object);
}

IC void CALifeLevelRegistry::remove(CSE_ALifeDynamicObject* object, bool no_assert)
{
#ifdef DEBUG
	if (psAI_Flags.test(aiALife)) {
		Msg				("[LSS] removing object [%s][%d] from current level",object->name_replace(),object->ID);
	}
#endif
	inherited::remove(object->ID, no_assert);
}

template <typename _update_predicate>
IC void CALifeLevelRegistry::update(const _update_predicate& predicate, bool const iterate_as_first_time_next_time)
{
	//	u32					object_count = 
	inherited::update(predicate, iterate_as_first_time_next_time);
#ifdef FULL_LEVEL_UPDATE
	m_first_update		= true;
#endif
#ifdef DEBUG
	if (psAI_Flags.test(aiALife)) {
//		Msg				("[LSS][OOS][%d : %d]",object_count, objects().size());
	}
#endif
}

IC CSE_ALifeDynamicObject* CALifeLevelRegistry::object(const ALife::_OBJECT_ID& id, bool no_assert) const
{
	_REGISTRY::const_iterator I = objects().find(id);
	if (I == objects().end())
	{
		THROW2(no_assert, "The spesified object hasn't been found in the current level!");
		return (0);
	}
	return ((*I).second);
}
