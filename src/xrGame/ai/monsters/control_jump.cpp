#include "stdafx.h"
#include "control_jump.h"
#include "BaseMonster/base_monster.h"
#include "control_manager.h"
#include "../../PHMovementControl.h"
#include "../../../Include/xrRender/KinematicsAnimated.h"
#include "../../detail_path_manager.h"
#include "../../level.h"
#include "control_animation_base.h"
#include "control_direction_base.h"
#include "control_movement_base.h"
#include "control_path_builder_base.h"
#include "monster_velocity_space.h"
#include "../../ai_space.h"
#include "../../level_graph.h"
#include "../../ai_object_location.h"
#include "../../CharacterPhysicsSupport.h"
#ifdef DEBUG
#include "../../level_debug.h"
#endif

#include "../../trajectories.h"
#include "../../../xrPhysics/IPHWorld.h"
#include "../../../xrPhysics/PHCharacter.h"
#include "../../../xrCore/_vector3d_ext.h"
#include "../../../xrNetServer/xr_enet_transport.h"   // MP fork (§3.4 inc 3 diagnostic): xr_enet::enabled()


// MP fork (design doc §3.4 increment 3, stuck-jump diagnostic): MEASUREMENT-ONLY, log-only, co-op only. A jump that starts and
// never ends holds the monster's pure capture forever (measured: 85 s, 0.00 m, dev/evidence/leap-measure4b). The three exits are
// pre-registered as hypotheses H1 (bounce + path end), H2 (ground gate + ground ray), H3 (animation end). Every input to all three
// is printed here, so the log NAMES the blocked exit instead of it being read out of the numbers.
void CControlJump::coop_trace(LPCSTR tag)
{
	// Opt-in: the diagnostic does a ray query and writes log lines, so an ordinary co-op session must not pay for it.
	static int s_on = -1;
	if (s_on < 0) s_on = strstr(Core.Params, "-coop_jumpdiag") ? 1 : 0;   // plain literal: the App. B key drift check reads flags from source literals
	if (!s_on || !xr_enet::enabled() || !m_object) return;
	const u32 now = time();
	const bool periodic = (0 == xr_strcmp(tag, "run"));
	if (periodic)
	{
		if (m_time_started == 0 || (now - m_time_started) < 3000) return;
		if (m_coop_trace_last && (now - m_coop_trace_last) < 1000) return;
	}
	m_coop_trace_last = now;
	const bool gate = (m_time_started != 0) && (m_time_started + u32(m_jump_time * 1000.f) <= now);
	bool ray = false;
	if (gate)
	{
		Fvector dir, from;
		dir.set(0.f, -1.f, 0.f);
		m_object->Center(from);
		collide::rq_result rq;
		if (Level().ObjectSpace.RayPick(from, dir, m_trace_ground_range, collide::rqtStatic, rq, m_object))
			ray = (rq.range < m_trace_ground_range);
	}
	// v7 (Overseer, 2026-09-16): the decision parameters, once per jump at activate, so the fast/slow regimes can be compared on
	// what the jump was ASKED to do — no already-logged parameter separates them (dev/harness/jump_regime_table.py over 98 jumps).
	if (0 == xr_strcmp(tag, "activate"))
	{
		Fvector v;
		v.set(0.f, 0.f, 0.f);
		m_object->PHGetLinearVell(v);
		const Fvector& tp = m_data.target_position;
		const Fvector p = m_object->Position();
		Msg("- COOP(jumpparam): %u t %u target %.3f,%.3f,%.3f dist %.2f dy %.2f force_factor %.3f flags 0x%x jump_factor %.3f "
			"jump_time %.3f build_line %.2f trace_ground %.2f vel %.2f,%.2f,%.2f ground_valid %d prepare_valid %d glide_valid %d",
			m_object->ID(), now, tp.x, tp.y, tp.z, p.distance_to(tp), tp.y - p.y, m_data.force_factor, m_data.flags.get(),
			m_jump_factor, m_jump_time, m_build_line_distance, m_trace_ground_range, v.x, v.y, v.z,
			m_data.state_ground.motion.valid() ? 1 : 0, m_data.state_prepare.motion.valid() ? 1 : 0,
			m_data.state_glide.motion.valid() ? 1 : 0);
		// §3.4 inc 3a (Overseer, name the enemy): what the jump was aimed at, and whom the monster's own AI is fighting.
		// A point jump has no target object; a monster with no current enemy logs none.
		const CObject* tobj = m_data.target_object;
		const CEntityAlive* enemy = m_object->EnemyMan.get_enemy();
		Msg("- COOP(jumptarget): %u t %u target_obj %d %s %.2f enemy %d %s %.2f",
			m_object->ID(), now,
			tobj ? int(tobj->ID()) : -1, tobj ? tobj->cNameSect().c_str() : "none", tobj ? p.distance_to(tobj->Position()) : -1.f,
			enemy ? int(enemy->ID()) : -1, enemy ? enemy->cNameSect().c_str() : "none", enemy ? p.distance_to(enemy->Position()) : -1.f);
	}
	Msg("- COOP(jumpstate): %u t %u %s age %u anim_state %d prev %d bounced %d path_end %d on_path %d jump_time %.3f ground_gate %d ground_ray %d pos %.3f,%.3f,%.3f",
		m_object->ID(), now, tag, m_time_started ? (now - m_time_started) : 0, int(m_anim_state_current), int(m_anim_state_prev),
		m_velocity_bounced ? 1 : 0, m_man->path_builder().is_path_end(0.1f) ? 1 : 0, m_man->path_builder().is_moving_on_path() ? 1 : 0,
		m_jump_time, gate ? 1 : 0, ray ? 1 : 0, m_object->Position().x, m_object->Position().y, m_object->Position().z);
}

void CControlJump::reinit()
{
	inherited::reinit();

	m_time_started = 0;
	m_time_next_allowed = 0;
}


void CControlJump::load(LPCSTR section)
{
	m_delay_after_jump = pSettings->r_u32(section, "jump_delay");
	m_jump_factor = pSettings->r_float(section, "jump_factor");
	m_trace_ground_range = pSettings->r_float(section, "jump_ground_trace_range");
	m_hit_trace_range = pSettings->r_float(section, "jump_hit_trace_range");
	m_build_line_distance = pSettings->r_float(section, "jump_build_line_distance");
	m_min_distance = pSettings->r_float(section, "jump_min_distance");
	m_max_distance = pSettings->r_float(section, "jump_max_distance");
	m_max_angle = pSettings->r_float(section, "jump_max_angle");
	m_max_height = pSettings->r_float(section, "jump_max_height");

	m_auto_aim_factor = 0.f;

	if (pSettings->line_exist(section, "jump_auto_aim_factor"))
		m_auto_aim_factor = pSettings->r_float(section, "jump_auto_aim_factor");
}

bool CControlJump::check_start_conditions()
{
	if (is_active()) return false;
	if (m_man->is_captured_pure()) return false;


	return true;
}

void CControlJump::remove_links(CObject* object)
{
	if (m_data.target_object == object)
		m_data.target_object = NULL;
}

void CControlJump::activate()
{
	m_coop_trace_last = 0;
	coop_trace("activate");
	// MP fork (design doc §3.4 increment 3 FIX, co-op only): hold a processing reference for the life of this jump.
	// MEASURED (dev/evidence/jump-selector): a monster left the per-frame processing list while its jump was still active
	// (processing 0, needed 1, updatecl +0, shedule still rising), so UpdateCL — and with it this control's update_frame —
	// never ran again, no exit condition was ever evaluated, and the jump held the pure capture for 115 s with the mutant
	// frozen and every later jump refused. Props.bActiveCounter is a COUNTER, so an island going to sleep now takes it from
	// 2 to 1 and the object keeps ticking. -coop_jumpfix_off restores the old behaviour for the control arm.
	if (xr_enet::enabled() && m_object && !m_coop_processing_held)
	{
		static int s_off = -1;
		if (s_off < 0) s_off = strstr(Core.Params, "-coop_jumpfix_off") ? 1 : 0;   // plain literal: the App. B key drift check reads flags from source literals
		if (!s_off)
		{
			m_object->processing_activate();
			m_coop_processing_held = true;
		}
	}
	m_man->capture_pure(this);
	m_man->subscribe(this, ControlCom::eventAnimationEnd);
	m_man->subscribe(this, ControlCom::eventAnimationStart);

	if (!m_data.flags.test(SControlJumpData::eDontUseVelocityBounce))
	{
		m_man->subscribe(this, ControlCom::eventVelocityBounce);
		m_data.flags.set(SControlJumpData::eDontUseVelocityBounce, false);
	}

	if (m_data.target_object &&
		!m_data.flags.test(SControlJumpData::eUseTargetPosition))
	{
		start_jump(get_target(m_data.target_object));
	}
	else
	{
		m_data.flags.set(SControlJumpData::eUseTargetPosition, false);
		start_jump(m_data.target_position);
	}
}

void CControlJump::on_release()
{
	// MP fork (§3.4 inc 3 FIX): release the processing reference with the control itself, so it follows the com's lifetime.
	if (m_coop_processing_held && m_object)
	{
		m_object->processing_deactivate();
		m_coop_processing_held = false;
	}
	m_man->unlock(this, ControlCom::eControlPath);

	SControlDirectionData* ctrl_data_dir = (SControlDirectionData*)m_man->data(this, ControlCom::eControlDir);
	VERIFY(ctrl_data_dir);
	ctrl_data_dir->linear_dependency = true;

	m_man->release_pure(this);
	m_man->unsubscribe(this, ControlCom::eventVelocityBounce);
	m_man->unsubscribe(this, ControlCom::eventAnimationEnd);
	m_man->unsubscribe(this, ControlCom::eventAnimationStart);

	if (m_data.target_object && m_data.flags.test(SControlJumpData::eUseAutoAim))
	{
		m_object->character_physics_support()->movement()->PHCharacter()->SetAirControlFactor(0.f);
	}

	m_data.flags.set(SControlJumpData::eUseAutoAim, 0);

	m_object->path().prepare_builder();
	m_object->set_ignore_collision_hit(false);
}

//////////////////////////////////////////////////////////////////////////
// Start jump
//////////////////////////////////////////////////////////////////////////
void CControlJump::start_jump(const Fvector& point)
{
	// initialize internals
	m_last_time_added_impulse = 0;
	m_velocity_bounced = false;
	m_object_hitted = false;

	m_target_position = point;
	m_blend_speed = -1.f;

	m_jump_start_pos = m_object->Position();
	m_time_started = 0;
	m_jump_time = 0;
	m_last_saved_pos_time = 0;

	// ignore collision hit when object is landing
	m_object->set_ignore_collision_hit(true);

	// select correct state 
	if (is_flag(SControlJumpData::ePrepareSkip))
	{
		m_anim_state_current = eStateGlide;
		m_anim_state_prev = eStatePrepare;

		m_man->path_stop(this);
		m_man->move_stop(this);
	}
	else
	{
		// check if can prepare in move
		bool prepared = false;

		if (is_flag(SControlJumpData::ePrepareInMove))
		{
			// get animation time
			float time = m_man->animation().motion_time(m_data.state_prepare_in_move.motion, m_object->Visual());
			// set acceleration and velocity
			SVelocityParam& vel = m_object->move().get_velocity(m_data.state_prepare_in_move.velocity_mask);
			float dist = time * vel.velocity.linear;

			// check nodes in direction
			Fvector target_point;
			target_point.mad(m_object->Position(), m_object->Direction(), dist);
			if (m_man->path_builder().accessible(target_point))
			{
				// нода в прямой видимости?
				m_man->path_builder().restrictions().add_border(m_object->Position(), target_point);
				u32 node = ai().level_graph().check_position_in_direction(
					m_object->ai_location().level_vertex_id(), m_object->Position(), target_point);
				m_man->path_builder().restrictions().remove_border();

				if (ai().level_graph().valid_vertex_id(node) && m_man->path_builder().accessible(node))
					prepared = true;
			}

			// node is checked, so try to build path
			if (prepared)
			{
				if (m_man->build_path_line(this, target_point, u32(-1),
				                           m_data.state_prepare_in_move.velocity_mask | MonsterMovement::
				                           eVelocityParameterStand))
				{
					//---------------------------------------------------------------------------------------------------
					// set path params
					SControlPathBuilderData* ctrl_path = (SControlPathBuilderData*)m_man->data(
						this, ControlCom::eControlPath);
					VERIFY(ctrl_path);
					ctrl_path->enable = true;

					m_man->lock(this, ControlCom::eControlPath);
					//---------------------------------------------------------------------------------------------------

					m_anim_state_current = eStatePrepareInMove;
					m_anim_state_prev = eStateNone;

					m_man->dir_stop(this);
				}
				else
				{
					prepared = false;
				}
			}
		}

		// if cannot perform prepare in move
		if (!prepared)
		{
			VERIFY(m_data.state_prepare.motion.valid() || is_flag(SControlJumpData::eGlideOnPrepareFailed));

			if (m_data.state_prepare.motion.valid())
			{
				m_anim_state_current = eStatePrepare;
				m_anim_state_prev = eStateNone;

				m_man->path_stop(this);
				m_man->move_stop(this);
			}
			else
			{
				m_anim_state_current = eStateGlide;
				m_anim_state_prev = eStatePrepare;
			}
		}
	}

	select_next_anim_state();
}

//////////////////////////////////////////////////////////////////////////
// Animation startup
//////////////////////////////////////////////////////////////////////////
void CControlJump::select_next_anim_state()
{
	coop_trace("anim_state");
	if (m_anim_state_current == eStateNone)
	{
		coop_trace("stop:anim_none");
		stop();
		return;
	}
	// check gliding state
	if ((m_anim_state_current == eStateGlide) && (m_anim_state_prev == eStateGlide))
		if (is_flag(SControlJumpData::eGlidePlayAnimOnce)) return;

	//---------------------------------------------------------------------------------------------------
	// start new animation
	SControlAnimationData* ctrl_data = (SControlAnimationData*)m_man->data(this, ControlCom::eControlAnimation);
	VERIFY(ctrl_data);
	ctrl_data->global.actual = false;

	switch (m_anim_state_current)
	{
	case eStatePrepare: ctrl_data->global.set_motion(m_data.state_prepare.motion);
		break;
	case eStatePrepareInMove: ctrl_data->global.set_motion(m_data.state_prepare_in_move.motion);
		break;
	case eStateGlide: ctrl_data->global.set_motion(m_data.state_glide.motion);
		break;
	case eStateGround: ctrl_data->global.set_motion(m_data.state_ground.motion);
		break;
	default: NODEFAULT;
	}

	//---------------------------------------------------------------------------------------------------
	// switch state if needed
	m_anim_state_prev = m_anim_state_current;

	if (m_anim_state_current != eStateGlide)
	{
		if (m_anim_state_current != eStatePrepare)
			m_anim_state_current = EStateAnimJump(m_anim_state_current + 1);
		else
			m_anim_state_current = eStateGlide;
	}

	if (in_auto_aim())
		m_object->character_physics_support()->movement()->PHCharacter()->
		          SetAirControlFactor(100.f * m_auto_aim_factor);
}

float CControlJump::relative_time()
{
	float time = ((Device.dwTimeGlobal - m_time_started) / 1000.f) / m_jump_time;
	if (time > 1.f)
		time = 1.f;

	return time;
}

bool CControlJump::in_auto_aim()
{
	if (!m_data.target_object)
		return false;

	if (!m_data.flags.test(SControlJumpData::eUseAutoAim))
		return false;

	if (!m_auto_aim_factor)
		return false;

	if (m_anim_state_prev != eStateGlide)
		return false;

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Frame update jump state
//////////////////////////////////////////////////////////////////////////
void CControlJump::update_frame()
{
	coop_trace("run");
	// check if all jump stages are ended
	if (m_velocity_bounced && m_man->path_builder().is_path_end(0.1f))
	{
		coop_trace("stop:bounce+path_end");
		stop();
		return;
	}

	if (m_anim_state_current == eStateGlide && in_auto_aim())
	{
		// set angular speed in exclusive force mode
		SControlDirectionData* ctrl_data_dir = (SControlDirectionData*)m_man->data(this, ControlCom::eControlDir);
		VERIFY(ctrl_data_dir);

		ctrl_data_dir->heading.target_angle = m_man->direction().angle_to_target(m_data.target_object->Position());

		float cur_yaw, target_yaw;
		m_man->direction().get_heading(cur_yaw, target_yaw);
		ctrl_data_dir->heading.target_speed = angle_difference(cur_yaw, target_yaw) / m_jump_time;
		ctrl_data_dir->linear_dependency = false;

		// 		ctrl_data->set_speed	(m_man->animation().current_blend()->timeTotal / m_man->animation().current_blend()->speed / m_jump_time);
	}

	// trace enemy for hit
	hit_test();

	// set velocity from path if we are on it
	if (m_man->path_builder().is_moving_on_path())
	{
		//---------------------------------------------------------------------------------------------------------------------------------
		// Set Velocity from path
		//---------------------------------------------------------------------------------------------------------------------------------
		SControlMovementData* ctrl_move = (SControlMovementData*)m_man->data(this, ControlCom::eControlMovement);
		VERIFY(ctrl_move);

		ctrl_move->velocity_target = m_object->move().get_velocity_from_path();
		ctrl_move->acc = flt_max;
		//---------------------------------------------------------------------------------------------------------------------------------
	}

	// check if we landed
	if (is_on_the_ground())
		grounding();
}

//////////////////////////////////////////////////////////////////////////
// Trace ground to check if we have already landed
//////////////////////////////////////////////////////////////////////////
bool CControlJump::is_on_the_ground()
{
	if (m_time_started == 0) return false;
	if (m_time_started + (m_jump_time * 1000) > time()) return false;

	Fvector direction;
	direction.set(0.f, -1.f, 0.f);
	Fvector trace_from;
	m_object->Center(trace_from);

	collide::rq_result l_rq;

	bool on_the_ground = false;
	if (Level().ObjectSpace.RayPick(trace_from, direction, m_trace_ground_range, collide::rqtStatic, l_rq, m_object))
	{
		if (l_rq.range < m_trace_ground_range) on_the_ground = true;
	}
	return (on_the_ground);
}

//////////////////////////////////////////////////////////////////////////
// 
//////////////////////////////////////////////////////////////////////////

void CControlJump::grounding()
{
	coop_trace("grounding");
	if ((m_data.state_ground.velocity_mask == u32(-1)) || is_flag(SControlJumpData::eGroundSkip) || !m_data
	                                                                                                 .state_ground.
	                                                                                                 motion.valid())
	{
		coop_trace("stop:ground_skip");
		stop();
		return;
	}

	Fvector target_position;
	target_position.mad(m_object->Position(), m_object->Direction(), m_build_line_distance);

	if (!m_man->build_path_line(this, target_position, u32(-1),
	                            m_data.state_ground.velocity_mask | MonsterMovement::eVelocityParameterStand))
	{
		coop_trace("stop:ground_path_failed");
		stop();
	}
	else
	{
		SControlPathBuilderData* ctrl_path = (SControlPathBuilderData*)m_man->data(this, ControlCom::eControlPath);
		VERIFY(ctrl_path);
		ctrl_path->enable = true;
		m_man->lock(this, ControlCom::eControlPath);

		// lock dir
		m_man->dir_stop(this);

		m_time_started = 0;
		m_jump_time = 0;
		m_anim_state_current = eStateGround;
		select_next_anim_state();
	}
}

void CControlJump::stop()
{
	m_man->notify(ControlCom::eventJumpEnd, 0);
}

//////////////////////////////////////////////////////////////////////////
// Get target point in world space
Fvector CControlJump::get_target(CObject* obj)
{
	u16 bone_id = smart_cast<IKinematics*>(obj->Visual())->LL_GetBoneRoot();
	CBoneInstance& bone = smart_cast<IKinematics*>(obj->Visual())->LL_GetBoneInstance(bone_id);

	Fmatrix global_transform;
	global_transform.mul(obj->XFORM(), bone.mTransform);

	if (m_object->m_monster_type == CBaseMonster::eMonsterTypeOutdoor)
		return (predict_position(obj, global_transform.c));
	else
		return (global_transform.c);
}

void CControlJump::calculate_jump_time(Fvector const& target, bool const check_force_factor)
{
	float ph_time = m_object->character_physics_support()->movement()->JumpMinVelTime(target);
	// выполнить прыжок в соответствии с делителем времени
	float cur_factor = (check_force_factor && m_data.force_factor > 0) ? m_data.force_factor : m_jump_factor;

	m_jump_time = ph_time / cur_factor;
}

void CControlJump::on_event(ControlCom::EEventType type, ControlCom::IEventData* data)
{
	if (type == ControlCom::eventVelocityBounce)
	{
		SEventVelocityBounce* event_data = (SEventVelocityBounce *)data;
		// !TEMP!
		if ((event_data->m_ratio < 0) && !m_velocity_bounced && (m_jump_time != 0))
		{
			if (is_on_the_ground())
			{
				m_velocity_bounced = true;
				grounding();
			}
			else
			{
				coop_trace("stop:bounce_airborne");
				stop();
			}
		}
	}
	else if (type == ControlCom::eventAnimationEnd)
	{
		select_next_anim_state();
	}
	else if (type == ControlCom::eventAnimationStart)
	{
		// start new animation
		SControlAnimationData* ctrl_data = (SControlAnimationData*)m_man->data(this, ControlCom::eControlAnimation);
		VERIFY(ctrl_data);

		if ((m_anim_state_current == eStateGlide) && (m_anim_state_prev == eStateGlide))
		{
			//---------------------------------------------------------------------------------
			// start jump here
			//---------------------------------------------------------------------------------
			// получить время физ.прыжка
			calculate_jump_time(m_target_position, true);

			m_object->character_physics_support()->movement()->Jump(m_target_position, m_jump_time);
			m_time_started = time();
			m_time_next_allowed = m_time_started + m_delay_after_jump;
			//---------------------------------------------------------------------------------

			// set angular speed in exclusive force mode
			SControlDirectionData* ctrl_data_dir = (SControlDirectionData*)m_man->data(this, ControlCom::eControlDir);
			VERIFY(ctrl_data_dir);

			if (!m_data.flags.test(SControlJumpData::eUseAutoAim) || !m_data.target_object)
			{
				ctrl_data_dir->heading.target_angle = m_man->direction().angle_to_target(m_target_position);
			}

			float cur_yaw, target_yaw;
			m_man->direction().get_heading(cur_yaw, target_yaw);
			ctrl_data_dir->heading.target_speed = angle_difference(cur_yaw, target_yaw) / m_jump_time;
			ctrl_data_dir->linear_dependency = false;
			//---------------------------------------------------------------------------------

			CBlend* current_blend = m_man->animation().current_blend();
			ctrl_data->set_speed(
				(current_blend ? current_blend->timeTotal / current_blend->speed : 1.0f) /
				m_jump_time);
		}
		else
			ctrl_data->set_speed(-1.f);
	}
}

void CControlJump::hit_test()
{
	if (m_object_hitted) return;
	if (!m_data.target_object) return;

	// Проверить на нанесение хита во время прыжка
	Fvector trace_from;
	m_object->Center(trace_from);

	collide::rq_result l_rq;

	if (Level().ObjectSpace.RayPick(trace_from, m_object->Direction(), m_hit_trace_range, collide::rqtObject, l_rq,
	                                m_object))
	{
		if ((l_rq.O == m_data.target_object) && (l_rq.range < m_hit_trace_range))
		{
			m_object_hitted = true;
		}
	}

	if (!m_object_hitted && m_data.target_object)
	{
		m_object_hitted = true;
		// определить дистанцию до врага
		Fvector d;
		d.sub(m_data.target_object->Position(), m_object->Position());
		if (d.magnitude() > m_hit_trace_range)
			m_object_hitted = false;

		// проверка на  Field-Of-Hit
		float my_h, my_p;
		float h, p;

		m_object->Direction().getHP(my_h, my_p);
		d.getHP(h, p);

		float from = angle_normalize(my_h - PI_DIV_6);
		float to = angle_normalize(my_h + PI_DIV_6);

		if (!is_angle_between(h, from, to)) m_object_hitted = false;

		from = angle_normalize(my_p - PI_DIV_6);
		to = angle_normalize(my_p + PI_DIV_6);

		if (!is_angle_between(p, from, to)) m_object_hitted = false;
	}

	if (m_object_hitted)
		m_object->HitEntityInJump(smart_cast<CEntity*>(m_data.target_object));
}

bool CControlJump::can_jump(CObject* target)
{
	const bool aggressive_jump = m_object->can_use_agressive_jump(target);
	Fvector target_position;
	target->Center(target_position);

	return can_jump(target_position, aggressive_jump);
}

bool CControlJump::jump_intersect_geometry(Fvector const& target, CObject* const ignored_object)
{
	calculate_jump_time(target, false);

	Fvector velocity;
	velocity.sub(target, m_object->Position());

	TransferenceToThrowVel(velocity, m_jump_time, physics_world()->Gravity());

	Fvector collide_position;
	collide::rq_results temp_rq_results;
	xr_vector<trajectory_pick>* pass_jump_picks = NULL;
	xr_vector<Fvector>* pass_collide_tris = NULL;
#ifdef DEBUG
	xr_vector<trajectory_pick>	jump_picks;
	pass_jump_picks		=	&jump_picks;
	xr_vector<Fvector>		collide_tris;
	pass_collide_tris	=	&collide_tris;
#endif // #ifdef DEBUG		

	Fvector const sizes = {0.8f, 1.4f, 0.8f};

	Fvector const start_to_target = target - m_object->Position();
	if (magnitude(start_to_target) < 1.f)
		return false;

	Fvector const traj_start = m_object->Position() + Fvector().set(0, 1.2f, 0);
	Fvector const traj_target = target + Fvector().set(0, 1.2f, 0) - (normalize(start_to_target) * 1);

	if (trajectory_intersects_geometry(m_jump_time,
	                                   traj_start,
	                                   traj_target,
	                                   velocity,
	                                   collide_position,
	                                   m_object,
	                                   ignored_object,
	                                   temp_rq_results,
	                                   pass_jump_picks,
	                                   pass_collide_tris,
	                                   sizes))
	{
#ifdef DEBUG
		m_object->m_jump_picks			=	jump_picks;
		m_object->m_jump_collide_tris	=	collide_tris;
#endif // #ifdef DEBUG		

		return true;
	}

#ifdef DEBUG
	m_object->m_jump_picks				=	jump_picks;
	m_object->m_jump_collide_tris		=	collide_tris;
#endif // #ifdef DEBUG

	return false;
}

bool CControlJump::can_jump(Fvector const& target, bool const aggressive_jump)
{
	if (m_time_next_allowed != 0)
	{
		// in aggressive mode we can jump after 1/3 of m_delay_after_jump
		if (m_time_next_allowed - (int)aggressive_jump * (2 * m_delay_after_jump / 3) > Device.dwTimeGlobal)
		{
			return false;
		}
	}

	if (!m_object->movement().restrictions().accessible(target))
		return false;

	Fvector source_position = m_object->Position();
	Fvector target_position = target;

	// проверка на dist
	float dist = source_position.distance_to(target_position);

	// in aggressive mode we can jump from distance >= 1
	const float test_min_distance = aggressive_jump ? _min(1.f, m_min_distance) : m_min_distance;
	if ((dist < test_min_distance) || (dist > m_max_distance))
		return false;

	// получить вектор направления и его мир угол
	float dir_yaw = Fvector().sub(target_position, source_position).getH();
	dir_yaw = angle_normalize(-dir_yaw);

	// проверка на angle
	float yaw_current, yaw_target;
	m_object->control().direction().get_heading(yaw_current, yaw_target);

	if (angle_difference(yaw_current, dir_yaw) > m_max_angle)
		return false;

	// check if target on the same floor etc
	if (_abs(target_position.y - source_position.y) > m_max_height)
		return false;

	// проверка prepare
	if (!is_flag(SControlJumpData::ePrepareSkip) && !is_flag(SControlJumpData::eGlideOnPrepareFailed))
	{
		if (!is_flag(SControlJumpData::ePrepareInMove))
		{
			VERIFY(m_data.state_prepare.motion.valid());
		}
		else
		{
			VERIFY(m_data.state_prepare_in_move.motion.valid());
			VERIFY(m_data.state_prepare_in_move.velocity_mask != u32(-1));

			// try to trace distance according to prepare animation
			bool good_trace_res = false;

			// get animation time
			float time = m_man->animation().motion_time(m_data.state_prepare_in_move.motion, m_object->Visual());
			// set acceleration and velocity
			SVelocityParam& vel = m_object->move().get_velocity(m_data.state_prepare_in_move.velocity_mask);
			float dist = time * vel.velocity.linear;

			// check nodes in direction
			Fvector target_point;
			target_point.mad(m_object->Position(), m_object->Direction(), dist);

			if (m_man->path_builder().accessible(target_point))
			{
				// нода в прямой видимости?
				m_man->path_builder().restrictions().add_border(m_object->Position(), target_point);
				u32 node = ai().level_graph().check_position_in_direction(
					m_object->ai_location().level_vertex_id(), m_object->Position(), target_point);
				m_man->path_builder().restrictions().remove_border();

				if (ai().level_graph().valid_vertex_id(node) && m_man->path_builder().accessible(node))
					good_trace_res = true;
			}

			if (!good_trace_res)
			{
				// cannot prepare in move, so check if can prepare in stand state
				if (!m_data.state_prepare.motion.valid()) return false;
			}
		}
	}

	return true;
}


Fvector CControlJump::predict_position(CObject* obj, const Fvector& pos)
{
	return pos;
}
