////////////////////////////////////////////////////////////////////////////
//	Module 		: moving_objects.cpp
//	Created 	: 27.03.2007
//  Modified 	: 27.03.2007
//	Author		: Dmitriy Iassenev
//	Description : moving objects
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "moving_objects.h"
#include "ai_space.h"
#include "level_graph.h"
#include "moving_object.h"

moving_objects::moving_objects() :
	m_tree(0)
{
}

moving_objects::~moving_objects()
{
	xr_delete(m_tree);
}

void moving_objects::on_level_load()
{
	xr_delete(m_tree);
	m_tree = xr_new<TREE>(ai().level_graph().header().box(), ai().level_graph().header().cell_size() * .5f, 16 * 1024,
	                      16 * 1024);
}

void moving_objects::register_object(moving_object* moving_object)
{
#ifdef DEBUG
	VERIFY2(
		m_objects.find(moving_object) == m_objects.end(),
		make_string("moving object %s is registers twice",*moving_object->id())
	);

	m_objects.insert		(moving_object);
#endif // DEBUG

	// MP fork (§19/§21 co-op thin client): the AI space (moving_objects::on_level_load)
	// is loaded only via the A-Life graph registry (setup_current_level -> ai().load),
	// which never runs on a thin client — so m_tree is null. Creatures replicated from
	// the server still register as moving obstacles here and deref the null tree (crash;
	// the VERIFY is DEBUG-only). The client owns no AI/pathfinding, so obstacle tracking
	// isn't needed: skip when the tree isn't built.
	if (!m_tree)
		return;
	m_tree->insert(moving_object);
}

void moving_objects::unregister_object(moving_object* moving_object)
{
#ifdef DEBUG
	VERIFY2(
		m_objects.find(moving_object) != m_objects.end(),
		make_string("moving object %s is not yet registered or unregisters twice",*moving_object->id())
	);

	m_objects.erase			(m_objects.find(moving_object));
#endif // DEBUG

	if (!m_tree) // MP fork (§19 co-op): thin client has no AI space / tree — see register_object
		return;
	m_tree->remove(moving_object);
}

void moving_objects::on_object_move(moving_object* moving_object)
{
#ifdef DEBUG
	VERIFY2(
		m_objects.find(moving_object) != m_objects.end(),
		make_string("moving object %s is not yet registered",*moving_object->id())
	);
#endif
#pragma todo("this place can be optimized in case of slowdowns")
	if (!m_tree) // MP fork (§19 co-op): thin client has no AI space / tree — see register_object
		return;

	m_tree->remove(moving_object);

	moving_object->update_position();

	m_tree->insert(moving_object);
}

void moving_objects::clear()
{
	m_previous_collisions.clear_not_free();
}
