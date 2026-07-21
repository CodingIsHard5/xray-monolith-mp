#include "stdafx.h"
#include "base_monster.h"

#include "../../../ai_object_location.h"
#include "../../../game_graph.h"
#include "../../../ai_space.h"
#include "../../../hit.h"
#include "../../../PHDestroyable.h"
#include "../../../CharacterPhysicsSupport.h"
#include "../control_animation_base.h"   // MP fork (§19 co-op): replicated animation
#include "../../../../xrNetServer/xr_enet_transport.h"   // MP fork (§19 co-op): xr_enet::enabled()

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
	// NOTE: reach CControlAnimationBase through CBaseMonster::anim(), not
	// control().animation() — the latter is typed as the abstract CControlAnimation, which
	// exposes only update_frame() and knows nothing about EMotionAnim.
	const EMotionAnim current = anim().GetCurAnim();
	if ((current == eAnimUndefined) || (u32(current) >= u32(eAnimCount)))
		return coop_anim_none;
	return u8(current);
}

void CBaseMonster::coop_apply_animation(u8 packed)
{
	if ((packed == coop_anim_none) || (u32(packed) >= u32(eAnimCount)))
	{
		// The server has no opinion this frame. Drop any previous override rather than
		// leaving the puppet latched onto a stale animation forever.
		anim().clear_override_animation();
		return; // also: never index m_anim_storage out of range
	}

	// index -1 leaves variant selection to select_animation()'s normal path; only the
	// animation itself is worth a wire byte. set_override_animation ignores animations this
	// monster does not have, so a mismatched creature simply keeps animating on its own.
	anim().set_override_animation(EMotionAnim(packed), u32(-1));
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
	coop_refresh_export_sample(); // MP fork (§19 co-op): see CCustomMonster::coop_refresh_export_sample

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
	// MP fork (§19 co-op): DISCARD these on a puppet — see CCustomMonster::net_Import for the
	// full reasoning. Short version: agent_manager() resolves through
	// seniority_holder().team(g_Team()).squad(g_Squad()).group(g_Group()), and a group's
	// CAgentManager only exists once a member REGISTERS in it (CEntity::ChangeTeam is the only
	// path that does that). Assigning raw leaves the creature registered in one group while
	// reporting another, so net_Relcase later dereferences a null component — a real crash
	// from live play, symbolicated to CAgentManager::remove_links.
	{
		const u8 net_team = P.r_u8();
		const u8 net_squad = P.r_u8();
		const u8 net_group = P.r_u8();
		if (!(xr_enet::enabled() && !ai().get_alife()))
		{
			id_Team = net_team;
			id_Squad = net_squad;
			id_Group = net_group;
		}
	}

	GameGraph::_GRAPH_ID l_game_vertex_id = ai_location().game_vertex_id();
	P.r(&l_game_vertex_id, sizeof(l_game_vertex_id));
	P.r(&l_game_vertex_id, sizeof(l_game_vertex_id));

	// MP fork (§19 co-op): see CCustomMonster::net_Import — never let an invalid position into
	// the interpolation buffer; a NaN reaching the physics shape is fatal.
	if (_valid(N.p_pos) && (NET.empty() || (NET.back().dwTimeStamp < N.dwTimeStamp)))
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
