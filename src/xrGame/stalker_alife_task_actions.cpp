////////////////////////////////////////////////////////////////////////////
//	Module 		: stalker_alife_task_actions.cpp
//	Created 	: 25.10.2004
//  Modified 	: 25.10.2004
//	Author		: Dmitriy Iassenev
//	Description : Stalker alife task action classes
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "stalker_alife_task_actions.h"
#include "ai/stalker/ai_stalker.h"
#include "ai/trader/ai_trader.h"
#include "inventory_item.h"
#include "weapon.h"
#include "script_game_object.h"
#include "inventory.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "alife_graph_registry.h"                 // MP fork item (3): graph().actor() for the anchor fallback
#include "game_graph.h"                           // MP fork item (3): vertex()->level_id(), header().level().name()
#include "mp_anchors.h"                           // MP fork item (3): min_distance_to over connected players
#include "stalker_decision_space.h"
#include "cover_manager.h"
#include "cover_evaluators.h"
#include "cover_point.h"
#include "movement_manager_space.h"
#include "detail_path_manager_space.h"
#include "game_location_selector.h"
#include "sight_manager.h"
#include "ai_object_location.h"
#include "stalker_movement_manager_smart_cover.h"
#include "ai/stalker/ai_stalker_space.h"
#include "ai_space.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "alife_human_brain.h"
#include "alife_smart_terrain_task.h"

#include "pch_script.h"
#include "patrol_path_manager.h"

using namespace StalkerSpace;
using namespace StalkerDecisionSpace;

#ifdef DEBUG
//#define GRENADE_TEST
#endif // #ifdef DEBUG

#ifdef GRENADE_TEST
#	include "actor.h"
#endif // #ifdef GRENADE_TEST

//////////////////////////////////////////////////////////////////////////
// CStalkerActionSolveZonePuzzle
//////////////////////////////////////////////////////////////////////////

CStalkerActionSolveZonePuzzle::CStalkerActionSolveZonePuzzle(CAI_Stalker* object, LPCSTR action_name) :
	inherited(object, action_name)
{
}

void CStalkerActionSolveZonePuzzle::initialize()
{
	inherited::initialize();

#ifndef GRENADE_TEST
	m_stop_weapon_handling_time = Device.dwTimeGlobal;
	if (object().inventory().ActiveItem() && object().best_weapon() && (object().inventory().ActiveItem()->object().ID()
		== object().best_weapon()->object().ID()))
		m_stop_weapon_handling_time += ::Random32.random(30000) + 30000;

	//	object().movement().set_desired_position	(0);
	object().movement().set_desired_direction(0);
	object().movement().set_path_type(MovementManager::ePathTypeGamePath);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_body_state(eBodyStateStand);
	object().movement().set_movement_type(eMovementTypeWalk);
	object().movement().set_mental_state(eMentalStateFree);
	object().sight().setup(CSightAction(SightManager::eSightTypeCover, false, true));
#else
#	if 1
//		object().movement().set_desired_position	(0);
		object().movement().set_desired_direction	(0);
		object().movement().set_path_type			(MovementManager::ePathTypeLevelPath);
		object().movement().set_detail_path_type	(DetailPathManager::eDetailPathTypeSmooth);
		object().movement().set_body_state			(eBodyStateStand);
		object().movement().set_movement_type		(eMovementTypeStand);
		object().movement().set_mental_state		(eMentalStateDanger);
		object().sight().setup						(CSightAction(g_actor,true));
//		object().sight().setup						(CSightAction(SightManager::eSightTypeCurrentDirection));
#	else
//		object().movement().set_mental_state		(eMentalStateDanger);
		object().movement().set_mental_state		(eMentalStateFree);
		object().movement().set_movement_type		(eMovementTypeWalk);
		object().movement().set_body_state			(eBodyStateStand);
		object().movement().set_desired_direction	(0);
		object().movement().set_path_type			(MovementManager::ePathTypePatrolPath);
		object().movement().set_detail_path_type	(DetailPathManager::eDetailPathTypeSmooth);
		object().movement().patrol().set_path		("test_sight",PatrolPathManager::ePatrolStartTypeNearest,PatrolPathManager::ePatrolRouteTypeContinue);
//		object().movement().set_nearest_accessible_position();
		object().sight().setup						(CSightAction(SightManager::eSightTypePathDirection));
		//		object().CObjectHandler::set_goal			(eObjectActionFire1,object().inventory().ItemFromSlot(GRENADE_SLOT),0,1,2500,3000);
#	endif
#endif
}

void CStalkerActionSolveZonePuzzle::finalize()
{
	inherited::finalize();

	//	object().movement().set_desired_position	(0);

	if (!object().g_Alive())
		return;

	object().sound().remove_active_sounds(u32(eStalkerSoundMaskNoHumming));
}

void CStalkerActionSolveZonePuzzle::execute()
{
	inherited::execute();

#ifndef GRENADE_TEST
	if (Device.dwTimeGlobal >= m_stop_weapon_handling_time)
		if (!object().best_weapon())
			object().CObjectHandler::set_goal(eObjectActionIdle);
		else
			object().CObjectHandler::set_goal(eObjectActionStrapped, object().best_weapon());
	else
		object().CObjectHandler::set_goal(eObjectActionIdle, object().best_weapon());
#else
#	if 1
	//		object().throw_target					(g_actor->Position(), g_actor);
	//		if (object().throw_enabled()) {
	//			object().CObjectHandler::set_goal	(eObjectActionFire1,object().inventory().ItemFromSlot(GRENADE_SLOT));
	//			return;
	//		}
	//
//		object().CObjectHandler::set_goal			(eObjectActionIdle,object().inventory().ItemFromSlot(GRENADE_SLOT));
		object().CObjectHandler::set_goal			(eObjectActionFire1,object().best_weapon());
#	else
#		if 1
			const CWeapon							*weapon = smart_cast<const CWeapon*>(object().best_weapon());
			VERIFY									(weapon);
			if (!weapon->strapped_mode())
				object().CObjectHandler::set_goal	(eObjectActionStrapped,object().best_weapon());
			else
				object().CObjectHandler::set_goal	(eObjectActionIdle,object().best_weapon());
#		else
			const CWeapon							*weapon = smart_cast<const CWeapon*>(object().best_weapon());
			VERIFY									(weapon);
//			Msg										("weapon %s is strapped : %c",*weapon->cName(),weapon->strapped_mode() ? '+' : '-');

			static u32 m_time_to_strap = 0;
			static u32 m_time_to_idle = 0;
			if (!object().inventory().ActiveItem() || (object().inventory().GetActiveSlot() == INV_SLOT_2)) {
				if (!m_time_to_strap)
					m_time_to_strap					= Device.dwTimeGlobal + 10000;
				if (Device.dwTimeGlobal >= m_time_to_strap) {
					m_time_to_idle					= 0;
					object().CObjectHandler::set_goal	(eObjectActionStrapped,object().best_weapon());
				}
			}
			else {
				const CWeapon						*weapon = smart_cast<const CWeapon*>(object().best_weapon());
				VERIFY								(weapon);
				if (weapon->strapped_mode()) {
					if (!m_time_to_idle)
						m_time_to_idle					= Device.dwTimeGlobal + 10000;
					if (Device.dwTimeGlobal >= m_time_to_idle) {
						m_time_to_strap					= 0;
						object().CObjectHandler::set_goal	(eObjectActionIdle,object().inventory().ItemFromSlot(INV_SLOT_2));
					}
				}
			}

#		endif
#	endif
#endif
}

//////////////////////////////////////////////////////////////////////////
// CStalkerActionSmartTerrain
//////////////////////////////////////////////////////////////////////////

CStalkerActionSmartTerrain::CStalkerActionSmartTerrain(CAI_Stalker* object, LPCSTR action_name) :
	inherited(object, action_name)
{
	set_inertia_time(30000);
}

void CStalkerActionSmartTerrain::initialize()
{
	inherited::initialize();
	//	object().movement().set_desired_position		(0);
	object().movement().set_desired_direction(0);
	object().movement().game_selector().set_selection_type(eSelectionTypeMask);
	object().movement().set_detail_path_type(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_body_state(eBodyStateStand);
	object().movement().set_movement_type(eMovementTypeWalk);
	object().movement().set_mental_state(eMentalStateFree);
	object().sight().setup(CSightAction(SightManager::eSightTypePathDirection));

	if (!object().best_weapon())
	{
		object().CObjectHandler::set_goal(eObjectActionIdle);
		return;
	}

	object().CObjectHandler::set_goal(eObjectActionIdle);

	CWeapon* best_weapon = smart_cast<CWeapon*>(object().best_weapon());
	if (object().CObjectHandler::weapon_strapped(best_weapon))
		return;

	object().CObjectHandler::set_goal(eObjectActionIdle, object().best_weapon());
}

void CStalkerActionSmartTerrain::finalize()
{
	inherited::finalize();
	//	object().movement().set_desired_position	(0);
	object().movement().game_selector().set_selection_type(eSelectionTypeRandomBranching);
}

// MP fork, item (3) FIX (a) — gate and rationale at the call site below.
//
// Behind -coop_hold_offlevel_job so the SAME build carries both arms: flag off reproduces the defect
// (the DROPPED line), flag on is the fix. A within-build control beats comparing two builds.
//
// The gate is deliberately narrow. Every condition that is NOT met leaves stock behaviour untouched:
//   * not a group member            -> an ordinary NPC may leave the level, which is correct and by design
//   * job is on this level          -> untouched
//   * the group is offline          -> stock A-Life is managing it; not our case
//   * no player within online_distance -> nobody is watching, and shape (b) covers that case honestly
static bool coop_hold_offlevel_job(CSE_ALifeHumanAbstract* stalker, GameGraph::_GRAPH_ID target)
{
	static int s_on = -1;
	if (s_on < 0)
		s_on = strstr(Core.Params, "-coop_hold_offlevel_job") ? 1 : 0;
	if (s_on != 1 || !stalker)
		return false;

	if (stalker->m_group_id == 0xffff)
		return false;
	if (!ai().game_graph().valid_vertex_id(target) || !ai().game_graph().valid_vertex_id(stalker->m_tGraphID))
		return false;

	GameGraph::_LEVEL_ID const target_level = ai().game_graph().vertex(target)->level_id();
	GameGraph::_LEVEL_ID const here_level = ai().game_graph().vertex(stalker->m_tGraphID)->level_id();
	if (target_level == here_level)
		return false;

	CSE_ALifeDynamicObject* const group = ai().alife().objects().object(stalker->m_group_id, true);
	if (!group || !group->m_bOnline)
		return false;

	float const d = mp_anchors::min_distance_to(stalker->o_Position, ai().alife().graph().actor()->o_Position);
	if (d > ai().alife().online_distance())
		return false;

	// Rate-limited: this action runs every tick and the hold is permanent while the gate holds, so an
	// unbounded line here would be the 1650-lines-a-second mistake recorded in alife_dynamic_object.cpp.
	static u32 s_holds = 0;
	++s_holds;
	if (s_holds <= 5 || (s_holds % 200) == 0)
		Msg("[HOLDJOB] %d held: off-level job vertex %d on level %d [%s], staying on level %d [%s] "
			"(group %d online, nearest player %.1f m <= %.1f) hold #%d",
			stalker->ID, (int)target, (int)target_level,
			*(ai().game_graph().header().level(target_level).name()),
			(int)here_level, *(ai().game_graph().header().level(here_level).name()),
			(int)stalker->m_group_id, d, ai().alife().online_distance(), s_holds);
	return true;
}

void CStalkerActionSmartTerrain::execute()
{
	inherited::execute();

#ifndef GRENADE_TEST
	if (completed())
		object().CObjectHandler::set_goal(eObjectActionStrapped, object().best_weapon());

	object().sound().play(eStalkerSoundHumming, 60000, 10000);

	CSE_ALifeHumanAbstract* stalker = smart_cast<CSE_ALifeHumanAbstract*
	>(ai().alife().objects().object(m_object->ID()));
	VERIFY(stalker);
	VERIFY(stalker->m_smart_terrain_id != 0xffff);

	CALifeSmartTerrainTask* task = stalker->brain().smart_terrain().task(stalker);
	THROW2(task, "Smart terrain is assigned but returns no task");
	if (object().ai_location().game_vertex_id() != task->game_vertex_id())
	{
		// MP fork, item (3) FIX (a): HOLD AN OFF-LEVEL JOB while this member's group is online and a player is
		// near. Taking it is what loses the NPC for the rest of the session:
		//
		//   [SQCALLER] member vertex 627 level 2 -> TARGET vertex 228 level 1 [k00_marsh];
		//              group 29952 vertex 627 level 2 online 1
		//   [LVLREG]   29958 DROPPED from level registry: vertex 228 is on level 1, current level is 2
		//
		// A game path to another level is walked by teleport (movement_manager_game.cpp:126), which switches the
		// member offline as an individual and hands it to graph().change(); level().add then refuses it for the
		// wrong level. register_member already removed it from that registry, so that refused add was its only
		// route back, and its group — which stays online here — is the only thing that could respawn it and
		// never does. Measured: offline on 61 of 61 samples, 3 m from the player, 60 re-placements ignored.
		//
		// Held HERE, at the job, rather than at the teleport: refusing the teleport leaves the path state
		// machine re-selecting the same intermediate vertex every tick. Declining the destination means no path
		// is ever built, so there is nothing to retry.
		if (coop_hold_offlevel_job(stalker, task->game_vertex_id()))
		{
			object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
			object().movement().set_level_dest_vertex(object().ai_location().level_vertex_id());
			return;
		}
		object().movement().set_path_type(MovementManager::ePathTypeGamePath);
		object().movement().set_game_dest_vertex(task->game_vertex_id());
		return;
	}

	object().movement().set_path_type(MovementManager::ePathTypeLevelPath);
	if (object().movement().accessible(task->level_vertex_id()))
	{
		object().movement().set_level_dest_vertex(task->level_vertex_id());
		Fvector temp = task->position();
		object().movement().set_desired_position(&temp);
		return;
	}

	object().movement().set_nearest_accessible_position(task->position(), task->level_vertex_id());
#else
	object().movement().set_desired_direction	(0);
	object().movement().set_path_type			(MovementManager::ePathTypeLevelPath);
	object().movement().set_detail_path_type	(DetailPathManager::eDetailPathTypeSmooth);
	object().movement().set_body_state			(eBodyStateStand);
	object().movement().set_movement_type		(eMovementTypeStand);
	object().movement().set_mental_state		(eMentalStateDanger);
	object().sight().setup						(CSightAction(g_actor,true));
	object().throw_target						(g_actor->Position(), g_actor);
	if (object().throw_enabled()) {
		object().CObjectHandler::set_goal		(eObjectActionFire1,object().inventory().ItemFromSlot(GRENADE_SLOT));
		return;
	}

	object().CObjectHandler::set_goal			(eObjectActionIdle,object().inventory().ItemFromSlot(GRENADE_SLOT));
#endif
}
