#include "stdafx.h"
#include "control_animation.h"
#include "BaseMonster/base_monster.h"
#include "control_manager.h"
#include "profiler.h"
#include "../../../xrNetServer/xr_enet_transport.h"   // MP fork: xr_enet::enabled() (melee measurement gate)

//#ifdef _DEBUG
//#include "control_animation_base.h"
//#endif

void SAnimationPart::set_motion(MotionID const& m)
{
	VERIFY(m.valid());
	motion = m;
}

void CControlAnimation::reinit()
{
	inherited::reinit();

	m_skeleton_animated = smart_cast<IKinematicsAnimated*>(m_object->Visual());

	m_anim_events.clear();

	m_global_animation_end = false;
	m_legs_animation_end = false;
	m_torso_animation_end = false;

	m_freeze = false;
}

void CControlAnimation::reset_data()
{
	m_data.global.init();
	m_data.legs.init();
	m_data.torso.init();
	m_data.set_speed(-1.f);
}

void CControlAnimation::update_frame()
{
	if (m_freeze) return;

	// move to schedule update
	START_PROFILE("BaseMonster/Animation/Update Tracks")
		;
		m_skeleton_animated->UpdateTracks();
	STOP_PROFILE;

	START_PROFILE("BaseMonster/Animation/Check callbacks")
		;
		check_callbacks();
	STOP_PROFILE;

	START_PROFILE("BaseMonster/Animation/Play")
		;
		play();
	STOP_PROFILE;

	START_PROFILE("BaseMonster/Animation/Check Events")
		;
		check_events(m_data.global);
		check_events(m_data.torso);
		check_events(m_data.legs);
	STOP_PROFILE;
}

static void global_animation_end_callback(CBlend* B)
{
	CControlAnimation* controller = (CControlAnimation *)B->CallbackParam;
	controller->m_global_animation_end = true;
}

static void legs_animation_end_callback(CBlend* B)
{
	CControlAnimation* controller = (CControlAnimation *)B->CallbackParam;
	controller->m_legs_animation_end = true;
}

static void torso_animation_end_callback(CBlend* B)
{
	CControlAnimation* controller = (CControlAnimation *)B->CallbackParam;
	controller->m_torso_animation_end = true;
}

void CControlAnimation::play()
{
	if (!m_data.global.actual)
	{
		play_part(m_data.global, global_animation_end_callback);
		if (m_data.global.blend) m_saved_global_speed = m_data.global.blend->speed;
	}

	if (!m_data.legs.actual)
		play_part(m_data.legs, legs_animation_end_callback);
	if (!m_data.torso.actual)
		play_part(m_data.torso, torso_animation_end_callback);

	// speed only for global
	if (m_data.global.blend)
	{
		if (m_data.get_speed() > 0)
		{
			m_data.global.blend->speed = m_data.get_speed(); // TODO: make factor
		}
		else
		{
			m_data.global.blend->speed = m_saved_global_speed;
		}
	}
}

static int coop_meleediag()
{
	static int s_on = -1;
	if (s_on < 0) s_on = (xr_enet::enabled() && strstr(Core.Params, "-coop_meleediag")) ? 1 : 0;   // plain literal: the App. B key drift check reads flags from source literals
	return s_on;
}

// MP fork (§3.4/§16.3 crow-list melee measurement): called right after a part starts a motion. Logs every event of the motion the
// part played before that was never handled (MISSED), then the start of this motion when it carries events (an attack).
void CControlAnimation::coop_melee_part_started(SAnimationPart& part)
{
	const int k = (&part == &m_data.global) ? 0 : ((&part == &m_data.legs) ? 1 : 2);
	if (m_coop_prev_motion[k].valid())
	{
		ANIMATION_EVENT_MAP_IT pit = m_anim_events.find(m_coop_prev_motion[k]);
		if (pit != m_anim_events.end())
			for (ANIMATION_EVENT_VEC_IT e = pit->second.begin(); e != pit->second.end(); ++e)
				if (!e->handled)
					Msg("- COOP(melee-miss): %u t %u part %d motion %s event %u time_perc %.3f played_ms %u len_ms %u", m_object->ID(),
						Device.dwTimeGlobal, k, m_skeleton_animated->LL_MotionDefName_dbg(m_coop_prev_motion[k]).first, e->event_id,
						e->time_perc, Device.dwTimeGlobal - m_coop_prev_start[k], m_coop_prev_len_ms[k]);
	}
	m_coop_prev_motion[k] = part.get_motion();
	m_coop_prev_start[k] = part.time_started;
	m_coop_prev_len_ms[k] = part.blend ? u32(part.blend->timeTotal / part.blend->speed * 1000.f) : 0;
	ANIMATION_EVENT_MAP_IT it = m_anim_events.find(part.get_motion());
	if (it != m_anim_events.end() && part.blend)
		Msg("- COOP(melee-anim): %u t %u part %d motion %s len_ms %u events %u", m_object->ID(), Device.dwTimeGlobal, k,
			m_skeleton_animated->LL_MotionDefName_dbg(part.get_motion()).first, m_coop_prev_len_ms[k], u32(it->second.size()));
}

void CControlAnimation::play_part(SAnimationPart& part, PlayCallback callback)
{
	VERIFY(part.get_motion().valid());

	u16 bone_or_part = m_skeleton_animated->LL_GetMotionDef(part.get_motion())->bone_or_part;
	if (bone_or_part == u16(-1)) bone_or_part = m_skeleton_animated->LL_PartID("default");

	// initialize synchronization of prev and current animation
	float pos = -1.f;
	if (part.blend && !part.blend->stop_at_end)
		pos = fmod(part.blend->timeCurrent, part.blend->timeTotal) / part.blend->timeTotal;
#ifdef DEBUG
	//IKinematicsAnimated * K = m_object->Visual()->dcast_PKinematicsAnimated();
	//Msg				("%6d Playing animation : %s , %s , Object %s",Device.dwTimeGlobal, K->LL_MotionDefName_dbg(part.motion).first,K->LL_MotionDefName_dbg(part.motion).second, *(m_object->cName()));
#endif

	part.blend = m_skeleton_animated->LL_PlayCycle(bone_or_part, part.get_motion(), TRUE, callback, this);


	///////////////////////////////////////////////////////////////////////////////
	//#ifdef _DEBUG	
	//	Msg("Monster[%s] Time[%u] Anim[%s]",*(m_object->cName()), Device.dwTimeGlobal,*(m_object->anim().GetAnimTranslation(part.motion)));
	//#endif
	///////////////////////////////////////////////////////////////////////////////

	// synchronize prev and current animations
	if ((pos > 0) && part.blend && !part.blend->stop_at_end)
	{
		part.blend->timeCurrent = part.blend->timeTotal * pos;
	}

	part.time_started = Device.dwTimeGlobal;
	part.actual = true;
	if (coop_meleediag())
		coop_melee_part_started(part);   // before the handled flags below are reset, so a replay of the same motion still counts

	m_man->notify(ControlCom::eventAnimationStart, 0);

	if ((part.get_motion() != m_data.torso.get_motion()) && part.blend)
		m_object->CStepManager::on_animation_start(part.get_motion(), part.blend);


	ANIMATION_EVENT_MAP_IT it = m_anim_events.find(part.get_motion());
	if (it != m_anim_events.end())
	{
		for (ANIMATION_EVENT_VEC_IT event_it = it->second.begin(); event_it != it->second.end(); ++event_it)
		{
			event_it->handled = false;
		}
	}
}


void CControlAnimation::add_anim_event(MotionID motion, float time_perc, u32 id)
{
	// if there is already event with exact timing - return
	ANIMATION_EVENT_MAP_IT it = m_anim_events.find(motion);
	if (it != m_anim_events.end())
	{
		ANIMATION_EVENT_VEC& anim_vec = it->second;

		for (ANIMATION_EVENT_VEC_IT I = anim_vec.begin(); I != anim_vec.end(); ++I)
		{
			if (fsimilar(I->time_perc, time_perc)) return;
		}
	}

	SAnimationEvent event;
	event.time_perc = time_perc;
	event.event_id = id;

	m_anim_events[motion].push_back(event);
}

void CControlAnimation::check_events(SAnimationPart& part)
{
	if (part.get_motion().valid() && part.actual && part.blend)
	{
		ANIMATION_EVENT_MAP_IT it = m_anim_events.find(part.get_motion());
		if (it != m_anim_events.end())
		{
			float cur_perc = float(Device.dwTimeGlobal - part.time_started) / ((part.blend->timeTotal / part
			                                                                                            .blend->speed) *
				1000);

			for (ANIMATION_EVENT_VEC_IT event_it = it->second.begin(); event_it != it->second.end(); ++event_it)
			{
				SAnimationEvent& event = *event_it;
				if (!event.handled && (event.time_perc < cur_perc))
				{
					event.handled = true;
					if (coop_meleediag())
					{
						const float len_ms = (part.blend->timeTotal / part.blend->speed) * 1000.f;
						Msg("- COOP(melee): %u t %u motion %s event %u time_perc %.3f cur_perc %.3f len_ms %.0f delay_ms %.0f", m_object->ID(),
							Device.dwTimeGlobal, m_skeleton_animated->LL_MotionDefName_dbg(part.get_motion()).first, event.event_id,
							event.time_perc, cur_perc, len_ms, (cur_perc - event.time_perc) * len_ms);
					}

					// gen event
					SAnimationSignalEventData anim_event(part.get_motion(), event.time_perc, event.event_id);
					m_man->notify(ControlCom::eventAnimationSignal, &anim_event);
				}
			}
		}
	}
}

void CControlAnimation::check_callbacks()
{
	if (m_global_animation_end)
	{
		m_man->notify(ControlCom::eventAnimationEnd, 0);
		m_global_animation_end = false;
	}

	if (m_legs_animation_end)
	{
		m_man->notify(ControlCom::eventLegsAnimationEnd, 0);
		m_legs_animation_end = false;
	}

	if (m_torso_animation_end)
	{
		m_man->notify(ControlCom::eventTorsoAnimationEnd, 0);
		m_torso_animation_end = false;
	}
}

void CControlAnimation::restart(SAnimationPart& part, PlayCallback callback)
{
	VERIFY(part.get_motion().valid());
	VERIFY(part.blend);

	u16 bone_or_part = m_skeleton_animated->LL_GetMotionDef(part.get_motion())->bone_or_part;
	if (bone_or_part == u16(-1)) bone_or_part = m_skeleton_animated->LL_PartID("default");

	//save 
	float time_saved = part.blend->timeCurrent;

	// start
	part.blend = m_skeleton_animated->LL_PlayCycle(bone_or_part, part.get_motion(), TRUE, callback, this);

	// restore
	part.blend->timeCurrent = time_saved;
}

void CControlAnimation::restart()
{
	m_skeleton_animated = smart_cast<IKinematicsAnimated*>(m_object->Visual());

	if (m_data.global.blend) restart(m_data.global, global_animation_end_callback);
	if (m_data.legs.blend) restart(m_data.legs, legs_animation_end_callback);
	if (m_data.torso.blend) restart(m_data.torso, torso_animation_end_callback);
}

void CControlAnimation::freeze()
{
	if (m_freeze) return;
	m_freeze = true;

	if (m_data.global.blend)
	{
		m_saved_global_speed = m_data.global.blend->speed;
		m_data.global.blend->speed = 0.f;
	}
	if (m_data.legs.blend)
	{
		m_saved_legs_speed = m_data.legs.blend->speed;
		m_data.legs.blend->speed = 0.f;
	}
	if (m_data.torso.blend)
	{
		m_saved_torso_speed = m_data.torso.blend->speed;
		m_data.torso.blend->speed = 0.f;
	}
}

void CControlAnimation::unfreeze()
{
	if (!m_freeze) return;
	m_freeze = false;

	if (m_data.global.blend)
	{
		m_data.global.blend->speed = m_saved_global_speed;
	}
	if (m_data.legs.blend)
	{
		m_data.legs.blend->speed = m_saved_legs_speed;
	}
	if (m_data.torso.blend)
	{
		m_data.torso.blend->speed = m_saved_torso_speed;
	}
}
