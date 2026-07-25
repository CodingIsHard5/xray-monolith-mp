#include "stdafx.h"
#include "base_monster.h"
#include "../ai_monster_squad.h"
#include "../ai_monster_squad_manager.h"
#include "profiler.h"
#include "../state_manager.h"
#include "../../../../xrphysics/PhysicsShell.h"
#include "../../../detail_path_manager.h"
#include "../monster_velocity_space.h"
#include "../../../level.h"
#include "../control_animation_base.h"

// §3 item 3 inc3 (DIAGNOSTIC, temporary): breadcrumb which AI sub-phase a locally-driven Remote monster
// is in, so the LAST-flushed phase before the graph-engine-solver crash (null+0x88 in CSolverConditionValue
// emplace) localizes the culprit caller — the single-address symbol + ICF-fold hide it statically (see
// dev/DECISION_REPLICATION_PLAN.md "graph-engine-solver crash"). Gated on -coop_aiphase (off by default,
// zero cost when off — short-circuits before any call); FlushLog each set so it survives the crash. Fires
// only for Remote() && coop_locally_driven() monsters (the flipped population). REMOVE once pinned+guarded.
#define MP_AIPHASE(ph) do { \
	static int s_ap = -1; if (s_ap == -1) s_ap = strstr(Core.Params, "-coop_aiphase") ? 1 : 0; \
	if (s_ap && Remote() && coop_locally_driven()) { Msg("- MP_AIPHASE: id=%u %s", ID(), ph); FlushLog(); } \
} while(0)

void CBaseMonster::Think()
{
	START_PROFILE("Base Monster/Think")
		;

		if (!g_Alive() || getDestroy()) return;

		// Инициализировать
		MP_AIPHASE("init");
		InitThink();
		anim().ScheduledInit();

		// Обновить память
		START_PROFILE("Base Monster/Think/Update Memory")
			;
			MP_AIPHASE("mem");
			UpdateMemory();
		STOP_PROFILE;

		// Обновить сквад
		START_PROFILE("Base Monster/Think/Update Squad")
			;
			MP_AIPHASE("squad");
			monster_squad().update(this);
		STOP_PROFILE;

		// Запустить FSM
		START_PROFILE("Base Monster/Think/FSM")
			;
			MP_AIPHASE("fsm");
			update_fsm();
		STOP_PROFILE;

		MP_AIPHASE("think_done");
	STOP_PROFILE;
}

void CBaseMonster::update_fsm()
{
	MP_AIPHASE("fsm.state");
	StateMan->update();

	// завершить обработку установленных в FSM параметров
	MP_AIPHASE("fsm.post");
	post_fsm_update();

	MP_AIPHASE("fsm.path");
	TranslateActionToPathParams();

	// информировать squad о своих целях
	MP_AIPHASE("fsm.squadnotify");
	squad_notify();

#ifdef DEBUG
	debug_fsm						();
#endif
}

void CBaseMonster::post_fsm_update()
{
	if (!EnemyMan.get_enemy()) return;

	EMonsterState state = StateMan->get_state_type();


	// Look at enemy while running
	m_bRunTurnLeft = m_bRunTurnRight = false;


	Fvector direction;
	if (is_state(state, eStateAttack) &&
		control().path_builder().is_moving_on_path() &&
		control().path_builder().detail().try_get_direction(direction))
	{
		Fvector const self_to_enemy = Fvector().sub(EnemyMan.get_enemy()->Position(), Position());
		if (magnitude(self_to_enemy) > 3.f)
		{
			float dir_yaw = direction.getH();
			float yaw_target = self_to_enemy.getH();

			float angle_diff = angle_difference(yaw_target, dir_yaw);

			if ((angle_diff > PI_DIV_3) && (angle_diff < 5 * PI_DIV_6))
			{
				if (from_right(dir_yaw, yaw_target)) m_bRunTurnRight = true;
				else m_bRunTurnLeft = true;
			}
		}
	}
}

void CBaseMonster::squad_notify()
{
	CMonsterSquad* squad = monster_squad().get_squad(this);
	SMemberGoal goal;

	EMonsterState state = StateMan->get_state_type();

	if (is_state(state, eStateAttack))
	{
		goal.type = MG_AttackEnemy;
		goal.entity = const_cast<CEntityAlive*>(EnemyMan.get_enemy());
	}
	else if (is_state(state, eStateRest))
	{
		goal.entity = squad->GetLeader();

		if (state == eStateRest_Idle) goal.type = MG_Rest;
		else if (state == eStateRest_WalkGraphPoint) goal.type = MG_WalkGraph;
		else if (state == eStateRest_MoveToHomePoint) goal.type = MG_WalkGraph;
		else if (state == eStateCustomMoveToRestrictor) goal.type = MG_WalkGraph;
		else if (state == eStateRest_WalkToCover) goal.type = MG_WalkGraph;
		else if (state == eStateRest_LookOpenPlace) goal.type = MG_Rest;
		else goal.entity = 0;
	}
	else if (is_state(state, eStateSquad))
	{
		goal.type = MG_Rest;
		goal.entity = squad->GetLeader();
	}

	squad->UpdateGoal(this, goal);
}
