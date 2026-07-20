#include "stdafx.h"
#include "base_monster.h"

#include "../../../ai_object_location.h"
#include "../../../game_graph.h"
#include "../../../ai_space.h"
#include "../../../hit.h"
#include "../../../PHDestroyable.h"
#include "../../../CharacterPhysicsSupport.h"
#include "../control_animation_base.h"   // MP fork (§19 co-op): replicated animation
#include "../control_manager.h"

// MP fork (§19 co-op): mutants replicate the CHOSEN ANIMATION rather than the state behind
// it. Stalkers can send three small enums and let the client's animation manager re-derive
// the motion, but CControlAnimationBase picks from m_tAction plus the path builder plus the
// control manager's state machine — none of which run on a puppet. The engine already has
// the right hook for this: CControlAnimationBase::set_override_animation(), which short-
// circuits SelectAnimation() and validates against m_anim_storage. EMotionAnim has 83
// values so it fits the "flags" byte that net_Export has always written as a literal 0 and
// net_Import discards; 0xFF means "no opinion, animate normally".
static const u8 coop_anim_none = 0xFF;

u8 CBaseMonster::coop_pack_animation()
{
	const EMotionAnim anim = control().animation().GetCurAnim();
	if ((anim == eAnimUndefined) || (u32(anim) >= u32(eAnimCount)))
		return coop_anim_none;
	return u8(anim);
}

void CBaseMonster::coop_apply_animation(u8 packed)
{
	if ((packed == coop_anim_none) || (u32(packed) >= u32(eAnimCount)))
	{
		// The server has no opinion this frame. Drop any previous override rather than
		// leaving the puppet latched onto a stale animation forever.
		control().animation().clear_override_animation();
		return; // also: never index m_anim_storage out of range
	}

	// index -1 leaves variant selection to select_animation()'s normal path; only the
	// animation itself is worth a wire byte. set_override_animation ignores animations this
	// monster does not have, so a mismatched creature simply keeps animating on its own.
	control().animation().set_override_animation(EMotionAnim(packed), u32(-1));
}

void CBaseMonster::net_Save(NET_Packet& P)
{
	inherited::net_Save(P);
	m_pPhysics_support->in_NetSave(P);
}

BOOL CBaseMonster::net_SaveRelevant()
{
	return (inherited::net_SaveRelevant() || BOOL(PPhysicsShell() != NULL));
}

void CBaseMonster::net_Export(NET_Packet& P)
{
	R_ASSERT(Local());

	// export last known packet
	R_ASSERT(!NET.empty());
	net_update& N = NET.back();
	P.w_float(GetfHealth());
	P.w_u32(N.dwTimeStamp);
	P.w_u8(coop_pack_animation()); // MP fork (§19 co-op): see coop_pack_animation above
	P.w_vec3(N.p_pos);
	P.w_float /*w_angle8*/(N.o_model);
	P.w_float /*w_angle8*/(N.o_torso.yaw);
	P.w_float /*w_angle8*/(N.o_torso.pitch);
	P.w_float /*w_angle8*/(N.o_torso.roll);
	P.w_u8(u8(g_Team()));
	P.w_u8(u8(g_Squad()));
	P.w_u8(u8(g_Group()));

	GameGraph::_GRAPH_ID l_game_vertex_id = ai_location().game_vertex_id();
	P.w(&l_game_vertex_id, sizeof(l_game_vertex_id));
	P.w(&l_game_vertex_id, sizeof(l_game_vertex_id));
	//	P.w						(&m_fGoingSpeed,			sizeof(m_fGoingSpeed));
	//	P.w						(&m_fGoingSpeed,			sizeof(m_fGoingSpeed));
	float f1 = 0;
	if (ai().game_graph().valid_vertex_id(l_game_vertex_id))
	{
		f1 = Position().distance_to(ai().game_graph().vertex(l_game_vertex_id)->level_point());
		P.w(&f1, sizeof(f1));
		f1 = Position().distance_to(ai().game_graph().vertex(l_game_vertex_id)->level_point());
		P.w(&f1, sizeof(f1));
	}
	else
	{
		P.w(&f1, sizeof(f1));
		P.w(&f1, sizeof(f1));
	}
}

void CBaseMonster::net_Import(NET_Packet& P)
{
	R_ASSERT(Remote());
	net_update N;

	u8 flags;

	float health;
	P.r_float(health);
	SetfHealth(health);

	P.r_u32(N.dwTimeStamp);
	P.r_u8(flags);
	coop_apply_animation(flags); // MP fork (§19 co-op): see coop_pack_animation above
	P.r_vec3(N.p_pos);
	P.r_float /*r_angle8*/(N.o_model);
	P.r_float /*r_angle8*/(N.o_torso.yaw);
	P.r_float /*r_angle8*/(N.o_torso.pitch);
	P.r_float /*r_angle8*/(N.o_torso.roll);
	id_Team = P.r_u8();
	id_Squad = P.r_u8();
	id_Group = P.r_u8();

	GameGraph::_GRAPH_ID l_game_vertex_id = ai_location().game_vertex_id();
	P.r(&l_game_vertex_id, sizeof(l_game_vertex_id));
	P.r(&l_game_vertex_id, sizeof(l_game_vertex_id));

	if (NET.empty() || (NET.back().dwTimeStamp < N.dwTimeStamp))
	{
		NET.push_back(N);
		NET_WasInterpolating = TRUE;
	}

	//	P.r						(&m_fGoingSpeed,			sizeof(m_fGoingSpeed));
	//	P.r						(&m_fGoingSpeed,			sizeof(m_fGoingSpeed));
	float f1 = 0;
	if (ai().game_graph().valid_vertex_id(l_game_vertex_id))
	{
		f1 = Position().distance_to(ai().game_graph().vertex(l_game_vertex_id)->level_point());
		P.r(&f1, sizeof(f1));
		f1 = Position().distance_to(ai().game_graph().vertex(l_game_vertex_id)->level_point());
		P.r(&f1, sizeof(f1));
	}
	else
	{
		P.r(&f1, sizeof(f1));
		P.r(&f1, sizeof(f1));
	}


	setVisible(TRUE);
	setEnabled(TRUE);
}
