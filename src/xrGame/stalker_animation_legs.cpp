////////////////////////////////////////////////////////////////////////////
//	Module 		: stalker_animation_legs.cpp
//	Created 	: 25.02.2003
//  Modified 	: 19.11.2004
//	Author		: Dmitriy Iassenev
//	Description : Stalker animation manager : legs animations
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "stalker_animation_manager.h"
#include "ai/stalker/ai_stalker.h"
#include "sight_manager.h"
#include "stalker_movement_manager_smart_cover.h"
#include "stalker_animation_data.h"

#include "weapon.h"
#include "missile.h"
#include "inventory.h"
#include "stalker_animation_manager_impl.h"
#include "../xrEngine/xr_object.h"   // MP fork (§19 co-op): CObject for -coop_legstrace
#include "level.h"   // MP fork (§19 co-op): -coop_legstrace distance to the controlled actor

bool coop_legs_dwell_on();   // MP fork (§19 co-op, NPC shuffling round 2): defined below

const float right_forward_angle = PI_DIV_4;
const float left_forward_angle = PI_DIV_4;
//const float standing_turn_angle			= PI_DIV_6;
const float epsilon = EPS_L;

const u32 direction_switch_interval = 500;

const u32 need_look_back_time_delay = 0;

const float direction_angles[] = {
	0.f, //	eMovementDirectionForward
	PI, //	eMovementDirectionBackward
	PI_DIV_2, //	eMovementDirectionLeft
	-PI_DIV_2 //	eMovementDirectionRight
};

void CStalkerAnimationManager::legs_play_callback(CBlend* blend)
{
	CAI_Stalker* object = (CAI_Stalker*)blend->CallbackParam;
	VERIFY(object);

	CStalkerAnimationPair& pair = object->animation().legs();
	pair.on_animation_end();
}

IC float CStalkerAnimationManager::legs_switch_factor() const
{
	if (
		(m_target_direction == eMovementDirectionForward) &&
		(m_current_direction == eMovementDirectionBackward)
	)
		return (0.f);

	if (
		(m_target_direction == eMovementDirectionBackward) &&
		(m_current_direction == eMovementDirectionForward)
	)
		return (0.f);

	if (
		(m_target_direction == eMovementDirectionLeft) &&
		(m_current_direction == eMovementDirectionRight)
	)
		return (0.f);

	if (
		(m_target_direction == eMovementDirectionRight) &&
		(m_current_direction == eMovementDirectionLeft)
	)
		return (0.f);

	return (1.f);
}

bool CStalkerAnimationManager::need_look_back() const
{
	if (m_looking_back)
		return (true);

	if (m_previous_speed_direction != eMovementDirectionBackward)
		return (false);

	if ((m_change_direction_time + need_look_back_time_delay) > Device.dwTimeGlobal)
		return (false);

	m_looking_back = ::Random.randI(2) + 1;
	return (true);
}

void CStalkerAnimationManager::legs_assign_direction(float switch_factor, const EMovementDirection& direction)
{
	if (m_current_direction == direction)
	{
		m_direction_start = Device.dwTimeGlobal;
		return;
	}

	if (m_target_direction != direction)
	{
		m_direction_start = Device.dwTimeGlobal;
		m_target_direction = direction;
		return;
	}

	VERIFY(m_direction_start <= Device.dwTimeGlobal);
	if ((Device.dwTimeGlobal - m_direction_start) <= (u32)iFloor(switch_factor * direction_switch_interval))
		return;

	m_direction_start = Device.dwTimeGlobal;
	m_current_direction = direction;
}

void CStalkerAnimationManager::legs_process_direction(float yaw)
{
	float switch_factor = legs_switch_factor();
	stalker_movement_manager_smart_cover& movement = object().movement();
	float head_current = movement.head_orientation().current.yaw;
	float left = left_angle(yaw, head_current);
	float test_angle_forward = right_forward_angle;
	float test_angle_backward = left_forward_angle;
	if (left)
	{
		test_angle_forward = left_forward_angle;
		test_angle_backward = right_forward_angle;
	}
	test_angle_backward = PI - test_angle_backward;

	float difference = angle_difference(yaw, head_current);

	if (difference <= test_angle_forward)
		legs_assign_direction(switch_factor, eMovementDirectionForward);
	else
	{
		if (difference > test_angle_backward)
			legs_assign_direction(switch_factor, eMovementDirectionBackward);
		else if (left)
			legs_assign_direction(switch_factor, eMovementDirectionLeft);
		else
			legs_assign_direction(switch_factor, eMovementDirectionRight);
	}

	movement.m_body.target.yaw = yaw + direction_angles[m_current_direction];
}

MotionID CStalkerAnimationManager::legs_move_animation()
{
	m_no_move_actual = false;

	stalker_movement_manager_smart_cover& movement = object().movement();

	VERIFY(
		(movement.body_state() == eBodyStateStand) ||
		(movement.mental_state() != eMentalStateFree)
	);

	// MP fork (§19 co-op, NPC shuffling 2026-09-29): on a client this is reached because standing() said "moving", and
	// standing() answers from the server's DEBOUNCED standing bit (ai_stalker.cpp, 200 ms latch) while movement_type()
	// is the server's RAW value from the same packet. For up to 200 ms at every start/stop the two disagree: "moving"
	// with movement type STAND. m_movement.A[eMovementTypeStand] has no walk animations, so the lookups below read a
	// null MotionID vector (COOP(av) legs_move_animation+0xd0 / +0x63b on 95155ea6, dev/evidence/anim-av-95155ea6-1),
	// CStalkerAnimationManager::update catches it as "! error in stalker with visual ...", and resets EVERY track: the
	// NPC restarts its animation, over and over, in place. A puppet that is moving moves at walk speed or faster, so
	// index the walk set. -coop_legs_standfix_off is the control arm (the old lookup, faults and all).
	const bool coop_dwell = movement.replicated_state() && coop_legs_dwell_on();
	static int s_standfix_off = -1;
	if (s_standfix_off < 0)
		s_standfix_off = strstr(Core.Params, "-coop_legs_standfix_off") ? 1 : 0;
	const MonsterSpace::EMovementType move_type_raw =
		(movement.replicated_state() && (movement.movement_type() == eMovementTypeStand) && (s_standfix_off != 1))
			? eMovementTypeWalk
			: movement.movement_type();
	// NPC shuffling round 2: a walk<->run change must hold 400 ms before the legs switch gait (see coop_latch)
	const MonsterSpace::EMovementType move_type =
		coop_dwell ? MonsterSpace::EMovementType(m_coop_type_latch.get(int(move_type_raw), Device.dwTimeGlobal, 400)) : move_type_raw;

	if (eMentalStateDanger != movement.mental_state())
	{
		m_target_speed = movement.speed(eMovementDirectionForward);
		m_last_non_zero_speed = m_target_speed;

		//Alun: Sprint stalker fix
		if (movement.movement_type() == eMovementTypeRun && movement.mental_state() == eMentalStatePanic)
			return (m_data_storage->m_part_animations.A[eBodyStateStand].m_movement.A[2].A[4].A[0]);

		return (m_data_storage->m_part_animations.A[body_state()].m_movement.A[move_type].A[
				eMovementDirectionForward].A[1]
		);
	}

	// MP fork (§19 co-op): GetDirectionAngles returns false and leaves BOTH outputs untouched
	// when there is no path and no usable movement history — and stock ignores the return
	// value and uses them anyway, i.e. reads uninitialised stack. In single player that is
	// rare and self-correcting; for a replicated NPC it is the normal state (a puppet never
	// builds a path), so the direction was different garbage every frame, the
	// forward/back/left/right choice flipped constantly, and the leg animation was reselected
	// and restarted every single frame. That is the "animations looping from the start".
	// Fall back to the creature's own heading, which means "moving the way I am facing" — the
	// sane default, and stable frame to frame.
	float yaw = 0.f, pitch = 0.f;
	if (movement.replicated_state())
	{
		// MP fork (§19 co-op): the facing diagnostic proved the body ROOT rotation replicates
		// correctly; what was arbitrary was THIS - the leg-direction pick keyed off the NPC's
		// SIGHT direction, which is undriven on a puppet, so the legs pointed independently of
		// the body. Use the puppet's real direction of TRAVEL instead (from the network
		// position deltas). Standing still -> face forward (legs aligned with the body). This
		// makes "walking left" actually strafe left relative to the body, etc.
		yaw = object().coop_net_moving() ? object().coop_net_heading()
		                                 : movement.body_orientation().current.yaw;
	}
	else if (object().sight().GetDirectionAngles(yaw, pitch))
		yaw = angle_normalize_signed(-yaw);
	else
		yaw = movement.body_orientation().current.yaw;

	legs_process_direction(yaw);

	float body_current = movement.body_orientation().current.yaw;
	bool left = left_angle(yaw, body_current);
	float test_angle_forward = right_forward_angle;
	float test_angle_backward = left_forward_angle;
	if (left)
	{
		test_angle_forward = left_forward_angle;
		test_angle_backward = right_forward_angle;
	}
	test_angle_backward = PI - test_angle_backward;

	EMovementDirection speed_direction;
	float difference = angle_difference(yaw, body_current);

	if (difference <= test_angle_forward)
		speed_direction = eMovementDirectionForward;
	else
	{
		if (difference > test_angle_backward)
			speed_direction = eMovementDirectionBackward;
		else
		{
			if (left)
				speed_direction = eMovementDirectionLeft;
			else
				speed_direction = eMovementDirectionRight;
		}
	}

	// NPC shuffling round 2: a direction change (forward/left/right/back, from a heading derived from noisy network position
	// deltas) must hold 400 ms before the legs switch to it (see coop_latch)
	if (coop_dwell)
		speed_direction = EMovementDirection(m_coop_dir_latch.get(int(speed_direction), Device.dwTimeGlobal, 400));

	if (m_previous_speed_direction != speed_direction)
	{
		if (m_change_direction_time < Device.dwTimeGlobal)
			m_change_direction_time = Device.dwTimeGlobal;

		if (!legs_switch_factor())
		{
			m_previous_speed = 0.f;
			m_target_speed = 0.f;
		}

		m_previous_speed_direction = speed_direction;
	}

	m_target_speed = movement.speed(speed_direction);
	m_last_non_zero_speed = m_target_speed;

	return (
		m_data_storage->m_part_animations.A[
			body_state()
		].m_movement.A[
			move_type
		].A[
			speed_direction
		].A[
			0
		]
	);
}

MotionID CStalkerAnimationManager::legs_no_move_animation()
{
	m_previous_speed = 0.f;
	m_target_speed = 0.f;

	if (!m_no_move_actual)
	{
		m_no_move_actual = true;
		if (m_crouch_state_config == -1)
			m_crouch_state = ::Random.randI(2);
		else
			m_crouch_state = m_crouch_state_config;
	}

	m_change_direction_time = Device.dwTimeGlobal;

	// NPC shuffling round 2: standing ends a moving stint, so the next one starts from what the network says, not from the
	// gait and direction this NPC had minutes ago
	m_coop_type_latch.reset();
	m_coop_dir_latch.reset();

	EBodyState body_state = this->body_state();
	const xr_vector<MotionID>& animation = m_data_storage->m_part_animations.A[body_state].m_in_place->A;

	stalker_movement_manager_smart_cover& movement = object().movement();

	// MP fork (§19 co-op): a standing replicated NPC has nothing to turn towards. This
	// function plays a TURN-IN-PLACE animation whenever target yaw differs from current, and
	// on a puppet the target is written from head orientation (just below, and from
	// legs_process_direction) which nothing drives — so it never matched, and every stationary
	// NPC shuffled its feet forever with its legs pointing somewhere other than its body.
	// A puppet expresses turning by its replicated heading changing over time, which rotates
	// the model directly; there is no separate turn to animate. Make target agree with
	// current so the idle animation is chosen.
	if (movement.replicated_state())
		movement.m_body.target.yaw = movement.m_body.current.yaw;

	const SBoneRotation& body_orientation = movement.body_orientation();
	float current = body_orientation.current.yaw;
	float target = body_orientation.target.yaw;
	if (angle_difference(target, current) < EPS_L)
	{
		//		float					head_current = movement.head_orientation().current.yaw;
		if ((movement.mental_state() != eMentalStateFree) || !object().sight().turning_in_place())
		{
			if (movement.mental_state() == eMentalStateFree)
				return (animation[1]);

			if (body_state == eBodyStateCrouch)
				return (animation[m_crouch_state]);

			return (animation[0]);
		}

		movement.m_body.target.yaw = movement.head_orientation().target.yaw;
		target = movement.m_body.target.yaw;
	}

	if (left_angle(current, target))
	{
		if (movement.mental_state() == eMentalStateFree)
			return (animation[4]);

		return (animation[2]);
	}

	if (movement.mental_state() == eMentalStateFree)
		return (animation[5]);

	return (animation[3]);
}

// MP fork (§19 co-op, NPC shuffling round 2, 2026-10-02): the dwell latches (stalker_animation_manager.h coop_latch)
bool coop_legs_dwell_on()
{
	static int s_off = -1;
	if (s_off < 0)
		s_off = strstr(Core.Params, "-coop_legs_dwell_off") ? 1 : 0;
	return s_off != 1;
}

// MP fork (§19 co-op, NPC shuffling round 2, 2026-10-01) — measurement only, -coop_legstrace: Caden still sees NPCs
// shuffle on his rendering clients with the legs_move_animation fault gone (0 "error in stalker" on both clients,
// dev/evidence/human-20261001-1603). Log, for each replicated NPC within 40 m of the actor this client controls, every
// change of the chosen LEGS animation, with what the server sent (standing bit, speed, movement type, mental, body state)
// and the yaw inputs of the turn-in-place choice. A shuffle is the client changing its pick while the server's state
// holds still, or a turn/move pick on an NPC the server says is standing.
static void coop_legstrace(CAI_Stalker& obj, stalker_movement_manager_smart_cover& movement, bool standing_pick,
                           const MotionID& result, const xr_vector<MotionID>& in_place)
{
	static int s_on = -1;
	if (s_on < 0)
		s_on = strstr(Core.Params, "-coop_legstrace") ? 1 : 0;
	if (s_on != 1 || !movement.replicated_state() || !g_pGameLevel)
		return;
	CObject* const me = Level().CurrentControlEntity();
	if (!me || (me->Position().distance_to(obj.Position()) > 40.f))
		return;
	const char* branch = "move";
	if (standing_pick)
	{
		branch = "turn";
		for (u32 i = 0; i < 2 && i < in_place.size(); ++i)
			if (in_place[i] == result)
				branch = "idle";
	}
	// (round 2 verify, Overseer 2026-10-02) two things a dwell cannot fake: SLIDE, legs standing while the NPC travels (or
	// moving while it stands), integrated over a 200 ms sample; and LAG, from the server's standing bit changing (exact,
	// logged as it changes) to the legs following (the change lines below)
	{
		static xr_map<u16, int> s_bit;
		static xr_map<u16, u32> s_sample;
		static u32 s_bits = 0, s_samples = 0;   // separate caps: samples must never starve the bit events
		const int bit = int(obj.coop_net_standing());
		auto b = s_bit.find(obj.ID());
		if ((b == s_bit.end() || b->second != bit) && (++s_bits <= 6000))
		{
			s_bit[obj.ID()] = bit;
			Msg("~ COOP(legsbit): t=%u npc %u bit %d speed %.2f", Device.dwTimeGlobal, obj.ID(), bit, obj.coop_net_speed());
		}
		auto sm = s_sample.find(obj.ID());
		if ((sm == s_sample.end() || Device.dwTimeGlobal - sm->second >= 200) && (++s_samples <= 40000))
		{
			s_sample[obj.ID()] = Device.dwTimeGlobal;
			Msg("~ COOP(legss): t=%u npc %u %s speed %.2f bit %d", Device.dwTimeGlobal, obj.ID(), standing_pick ? "stand" : "move",
			    obj.coop_net_speed(), bit);
		}
	}
	static xr_map<u16, u32> s_last;
	static u32 s_lines = 0;
	const u32 key = (u32(result.slot) << 24) ^ (u32(result.idx) << 8) ^ (standing_pick ? 1u : 0u) ^ (u32(movement.body_state()) << 4);
	auto it = s_last.find(obj.ID());
	if (it != s_last.end() && it->second == key)
		return;
	s_last[obj.ID()] = key;
	if (++s_lines > 3000)
		return;
	Msg("~ COOP(legs): t=%u npc %u %s anim %u.%u | net standing %d speed %.2f moving %d | type %d mental %d body %d | "
	    "yaw cur %.2f tgt %.2f head-tgt %.2f turning %d dist %.1f",
	    Device.dwTimeGlobal, obj.ID(), branch, u32(result.slot), u32(result.idx), int(obj.coop_net_standing()),
	    obj.coop_net_speed(), obj.coop_net_moving() ? 1 : 0, int(movement.movement_type()), int(movement.mental_state()),
	    int(movement.body_state()), movement.body_orientation().current.yaw, movement.body_orientation().target.yaw,
	    movement.head_orientation().target.yaw, obj.sight().turning_in_place() ? 1 : 0,
	    me->Position().distance_to(obj.Position()));
}

MotionID CStalkerAnimationManager::assign_legs_animation()
{
	MotionID result;
	if (standing())
	{
		result = legs_no_move_animation();
		if (!result.valid())
			legs_no_move_animation();
		coop_legstrace(object(), object().movement(), true, result,
		               m_data_storage->m_part_animations.A[body_state()].m_in_place->A);
		return (result);
	}

	result = legs_move_animation();
	if (!result.valid())
		legs_move_animation();

	coop_legstrace(object(), object().movement(), false, result,
	               m_data_storage->m_part_animations.A[body_state()].m_in_place->A);
	return (result);
}
