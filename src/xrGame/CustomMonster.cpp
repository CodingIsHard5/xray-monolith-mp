// CustomMonster.cpp: implementation of the CCustomMonster class.
//
//////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "ai_debug.h"
#include "CustomMonster.h"
#include "ai_space.h"
#include "../xrNetServer/xr_enet_transport.h"   // MP fork (§19 diag): xr_enet::enabled()
#include "ai/monsters/BaseMonster/base_monster.h"
#include "xrserver_objects_alife_monsters.h"
#include "xrserver.h"
#include "seniority_hierarchy_holder.h"
#include "team_hierarchy_holder.h"
#include "squad_hierarchy_holder.h"
#include "group_hierarchy_holder.h"
#include "customzone.h"
#include "../Include/xrRender/Kinematics.h"
#include "detail_path_manager.h"
#include "memory_manager.h"
#include "visual_memory_manager.h"
#include "sound_memory_manager.h"
#include "enemy_manager.h"
#include "item_manager.h"
#include "danger_manager.h"
#include "ai_object_location.h"
#include "level_graph.h"
#include "game_graph.h"
#include "movement_manager.h"
#include "entitycondition.h"
#include "sound_player.h"
#include "level.h"
#include "level_debug.h"
#include "material_manager.h"
#include "sound_user_data_visitor.h"
#include "mt_config.h"
#include "PHMovementControl.h"
#include "profiler.h"
#include "date_time.h"
#include "characterphysicssupport.h"
#include "ai/monsters/snork/snork.h"
#include "ai/monsters/burer/burer.h"
#include "GamePersistent.h"
#include "actor.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "client_spawn_manager.h"
#include "moving_object.h"
#include "level_path_manager.h"

// Lain: added
#include "../xrEngine/IGame_Level.h"
#include "../xrCore/_vector3d_ext.h"
#include "debug_text_tree.h"
#include "../xrPhysics/IPHWorld.h"

#ifdef DEBUG
#	include "debug_renderer.h"
#   include "animation_movement_controller.h"
#endif // DEBUG

#ifdef HOLDERCUSTOM_NEW
#include "ai/stalker/ai_stalker.h"
#endif

void SetActorVisibility(u16 who, float value);
extern int g_AI_inactive_time;

#ifndef MASTER_GOLD
	Flags32		psAI_Flags	= {aiObstaclesAvoiding | aiUseSmartCovers};
#endif // MASTER_GOLD

void CCustomMonster::SAnimState::Create(IKinematicsAnimated* K, LPCSTR base)
{
	char buf[128];
	fwd = K->ID_Cycle_Safe(strconcat(sizeof(buf), buf, base, "_fwd"));
	back = K->ID_Cycle_Safe(strconcat(sizeof(buf), buf, base, "_back"));
	ls = K->ID_Cycle_Safe(strconcat(sizeof(buf), buf, base, "_ls"));
	rs = K->ID_Cycle_Safe(strconcat(sizeof(buf), buf, base, "_rs"));
}

//void __stdcall CCustomMonster::TorsoSpinCallback(CBoneInstance* B)
//{
//	CCustomMonster*		M = static_cast<CCustomMonster*> (B->Callback_Param);
//
//	Fmatrix					spin;
//	spin.setXYZ				(0, M->NET_Last.o_torso.pitch, 0);
//	B->mTransform.mulB_43	(spin);
//}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CCustomMonster::CCustomMonster() :
	// this is non-polymorphic call of the virtual function cast_entity_alive
	// just to remove warning C4355 if we use this instead
	Feel::Vision(cast_game_object())
{
	m_sound_user_data_visitor = 0;
	m_memory_manager = 0;
	m_movement_manager = 0;
	m_sound_player = 0;
	m_already_dead = false;
	m_invulnerable = false;
	m_moving_object = 0;
	m_coop_net_speed = 0.f;
	m_coop_net_heading = 0.f;
	m_coop_net_moving = false;
	m_coop_locally_driven = false; // §14 step 3: default = dense streaming (coop_puppet)
	m_coop_locally_driven_ts = 0;
}

// §14 step 3 (increment D): setter stamps the refresh time on set-true so UpdateCL can auto-clear the
// flag once decisions stop arriving. The client's decision executor calls this every decision (~3s),
// keeping the flag alive; when the stream ends the flag ages out (TTL) and the NPC reverts to a
// dense-streamed puppet — symmetric with the server's m_coop_decision_driven TTL (which resumes dense
// M_UPDATE for the same NPC). Not inline: needs Device.dwTimeGlobal.
void CCustomMonster::coop_set_locally_driven(bool v)
{
	m_coop_locally_driven = v;
	if (v)
		m_coop_locally_driven_ts = Device.dwTimeGlobal;
}

CCustomMonster::~CCustomMonster()
{
	xr_delete(m_sound_user_data_visitor);
	xr_delete(m_memory_manager);
	xr_delete(m_movement_manager);
	xr_delete(m_sound_player);

	// Lain: added (asking GameLevel to forget about self)
	if (g_pGameLevel)
	{
		g_pGameLevel->SoundEvent_OnDestDestroy(this);
	}

#ifdef DEBUG
	Msg							("dumping client spawn manager stuff for object with id %d",ID());
	if(!g_dedicated_server)
		Level().client_spawn_manager().dump	(ID());
#endif // DEBUG
	if (!g_dedicated_server)
		Level().client_spawn_manager().clear(ID());
}

void CCustomMonster::Load(LPCSTR section)
{
	inherited::Load(section);

	if (character_physics_support())
	{
		material().Load(section);
		character_physics_support()->movement()->Load(section);
	}

	memory().Load(section);
	movement().Load(section);
	//////////////////////////////////////////////////////////////////////////

	///////////
	// m_PhysicMovementControl: General

	//Fbox	bb;

	//// m_PhysicMovementControl: BOX
	//Fvector	vBOX0_center= pSettings->r_fvector3	(section,"ph_box0_center"	);
	//Fvector	vBOX0_size	= pSettings->r_fvector3	(section,"ph_box0_size"		);
	//bb.set	(vBOX0_center,vBOX0_center); bb.grow(vBOX0_size);
	//m_PhysicMovementControl->SetBox		(0,bb);

	//// m_PhysicMovementControl: BOX
	//Fvector	vBOX1_center= pSettings->r_fvector3	(section,"ph_box1_center"	);
	//Fvector	vBOX1_size	= pSettings->r_fvector3	(section,"ph_box1_size"		);
	//bb.set	(vBOX1_center,vBOX1_center); bb.grow(vBOX1_size);
	//m_PhysicMovementControl->SetBox		(1,bb);

	//// m_PhysicMovementControl: Foots
	//Fvector	vFOOT_center= pSettings->r_fvector3	(section,"ph_foot_center"	);
	//Fvector	vFOOT_size	= pSettings->r_fvector3	(section,"ph_foot_size"		);
	//bb.set	(vFOOT_center,vFOOT_center); bb.grow(vFOOT_size);
	//m_PhysicMovementControl->SetFoots	(vFOOT_center,vFOOT_size);

	//// m_PhysicMovementControl: Crash speed and mass
	//float	cs_min		= pSettings->r_float	(section,"ph_crash_speed_min"	);
	//float	cs_max		= pSettings->r_float	(section,"ph_crash_speed_max"	);
	//float	mass		= pSettings->r_float	(section,"ph_mass"				);
	//m_PhysicMovementControl->SetCrashSpeeds	(cs_min,cs_max);
	//m_PhysicMovementControl->SetMass		(mass);


	// m_PhysicMovementControl: Frictions
	/*
	float af, gf, wf;
	af					= pSettings->r_float	(section,"ph_friction_air"	);
	gf					= pSettings->r_float	(section,"ph_friction_ground");
	wf					= pSettings->r_float	(section,"ph_friction_wall"	);
	m_PhysicMovementControl->SetFriction	(af,wf,gf);

	// BOX activate
	m_PhysicMovementControl->ActivateBox	(0);
	*/
	////////

	Position().y += EPS_L;

	//	m_current			= 0;

	eye_fov = pSettings->r_float(section, "eye_fov");
	eye_range = pSettings->r_float(section, "eye_range");

	// Health & Armor
	//	fArmor					= 0;

	// Msg				("! cmonster size: %d",sizeof(*this));
}

void CCustomMonster::reinit()
{
	CScriptEntity::reinit();
	CEntityAlive::reinit();

	if (character_physics_support())
		material().reinit();

	movement().reinit();
	sound().reinit();

	m_client_update_delta = 0;
	m_last_client_update_time = Device.dwTimeGlobal;

	eye_pp_stage = 0;
	m_dwLastUpdateTime = 0xffffffff;
	m_tEyeShift.set(0, 0, 0);
	m_fEyeShiftYaw = 0.f;
	NET_WasExtrapolating = FALSE;

	//////////////////////////////////////////////////////////////////////////
	// Critical Wounds
	//////////////////////////////////////////////////////////////////////////

	m_critical_wound_type = u32(-1);
	m_last_hit_time = 0;
	m_critical_wound_accumulator = 0.f;
	m_critical_wound_threshold = pSettings->r_float(cNameSect(), "critical_wound_threshold");
	m_critical_wound_decrease_quant = pSettings->r_float(cNameSect(), "critical_wound_decrease_quant");

	if (m_critical_wound_threshold >= 0)
		load_critical_wound_bones();
	//////////////////////////////////////////////////////////////////////////
	m_update_rotation_on_frame = true;
	m_movement_enabled_before_animation_controller = true;
}

void CCustomMonster::reload(LPCSTR section)
{
	sound().reload(section);
	CEntityAlive::reload(section);

	if (character_physics_support())
		material().reload(section);

	movement().reload(section);
	load_killer_clsids(section);

	m_far_plane_factor = READ_IF_EXISTS(pSettings, r_float, section, "far_plane_factor", 1.f);
	m_fog_density_factor = READ_IF_EXISTS(pSettings, r_float, section, "fog_density_factor", .05f);

	m_panic_threshold = pSettings->r_float(section, "panic_threshold");
}

void CCustomMonster::mk_orientation(Fvector& dir, Fmatrix& mR)
{
	// orient only in XZ plane
	dir.y = 0;
	float len = dir.magnitude();
	if (len > EPS_S)
	{
		// normalize
		dir.x /= len;
		dir.z /= len;
		Fvector up;
		up.set(0, 1, 0);
		mR.rotation(dir, up);
	}
}

// MP fork (§19 co-op): the exported sample is NET.back(), and NET only gains an entry when
// shedule_Update runs — i.e. when the SCHEDULER ticks this creature, every 100-250ms and
// worse for distant or idle ones. Clients therefore receive the same position repeated for
// several packets and then a jump, which is why replicated NPCs move in visible steps no
// matter how well the client interpolates: there is nothing between the steps to interpolate.
// net_Export runs from CLevel::ClientSend every server frame, so take a fresh sample here.
// That turns a coarse, irregular ~5Hz stream into a dense, evenly spaced one, and costs
// nothing on the wire — the same one sample per packet is sent either way.
//
// Server-only: it needs the authoritative Position() and body orientation, and a thin client
// has neither (its creatures are Remote and never call net_Export at all).
void CCustomMonster::coop_refresh_export_sample()
{
	if (!xr_enet::enabled() || !ai().get_alife() || NET.empty())
		return;

	const u32 now = Level().timeServer();
	if (NET.back().dwTimeStamp >= now)
		return; // already sampled this millisecond — keep timestamps strictly increasing

	net_update current;
	current.dwTimeStamp = now;

	// Take the facing from the OBJECT TRANSFORM, not from movement().m_body.current.yaw.
	// The client applies this with XFORM().rotateY(o_model), which replaces the rotation
	// outright, so o_model has to be the creature's real world heading. m_body is the
	// movement manager's own idea of where the body should be pointing; for a stalker whose
	// rotation is animation-driven it is not the same thing, and using it left every
	// replicated NPC facing whatever value it happened to hold — reported from play as
	// "npcs are all facing one direction". A pure Y-rotation has k = (sin a, 0, cos a), so
	// the heading getHP() returns is exactly the angle rotateY() wants back.
	// Never sample an invalid transform. Stock exported whatever the scheduler last recorded,
	// which was at least a position the creature really occupied; sampling live means a
	// creature caught mid-teleport or before its physics settles can hand us a NaN, and that
	// NaN then travels to every client and ends in
	// CPHActivationShape::Create "assertion failed _valid(start_pos)". Skip the sample and
	// keep the previous one - one stale frame is invisible, a NaN is fatal.
	if (!_valid(Position()) || !_valid(XFORM()))
		return;

	// Sample whichever source is actually AUTHORITATIVE for this creature's facing, because
	// which one that is varies — and getting it wrong produced both facing bugs so far.
	//
	//  * When an animation owns the transform (animation_movement_controlled), the transform
	//    IS the truth and m_body is only the movement manager's intent. For a stalker standing
	//    around, that intent is often stale, which is why every idle NPC pointed the same way.
	//  * Otherwise XFORM is driven FROM o_model by the rotateY below — which is not gated on
	//    Remote(), so it runs on the server for its own creatures too. Sampling XFORM there
	//    closes a feedback loop and freezes the angle, which is the other bug I shipped.
	//
	// So: read XFORM only when something else is driving it, and m_body otherwise. Note the
	// negation - rotateY(a) sets k = (sin a, 0, cos a) while getHP returns -a for that vector.
	if (animation_movement_controlled())
	{
		float model_yaw, model_pitch;
		XFORM().k.getHP(model_yaw, model_pitch);
		current.o_model = angle_normalize(-model_yaw);
	}
	else
		current.o_model = movement().m_body.current.yaw;

	current.o_torso = movement().m_body.current;
	current.o_torso.yaw = current.o_model;
	current.p_pos = Position();
	current.fHealth = GetfHealth();
	NET.push_back(current);

	// MP fork (§19 co-op) facing diagnostic: what the SERVER is exporting, and where it came
	// from. Throttled per creature. Compare with XRNET(face-cl) on the client to see the
	// mismatch instead of guessing. Enable with -xrnet_facelog to avoid log spam otherwise.
	if (strstr(Core.Params, "-xrnet_facelog"))
	{
		float xf_yaw, xf_pitch;
		XFORM().k.getHP(xf_yaw, xf_pitch);
		static u32 s_face_sv = 0;
		if ((++s_face_sv % 30) == 0)
		{
			Msg("- XRNET(face-sv): id=%u src=%s o_model=%.3f m_body=%.3f xform=%.3f animctl=%d",
				ID(), animation_movement_controlled() ? "xform" : "m_body",
				current.o_model, movement().m_body.current.yaw, angle_normalize(-xf_yaw),
				animation_movement_controlled() ? 1 : 0);
			FlushLog();
		}
	}
	// Bounded here as well as in shedule_Update: this runs far more often than the scheduler
	// trim, and the exporter only ever looks at the newest entry.
	while (NET.size() > 4)
		NET.pop_front();
}

void CCustomMonster::net_Export(NET_Packet& P) // export to server
{
	R_ASSERT(Local());
	coop_refresh_export_sample();

	// export last known packet
	R_ASSERT(!NET.empty());
	net_update& N = NET.back();
	P.w_float(GetfHealth());
	P.w_u32(N.dwTimeStamp);
	P.w_u8(0);
	P.w_vec3(N.p_pos);
	P.w_float /*w_angle8*/(N.o_model);
	P.w_float /*w_angle8*/(N.o_torso.yaw);
	P.w_float /*w_angle8*/(N.o_torso.pitch);
	P.w_float /*w_angle8*/(N.o_torso.roll);
	P.w_u8(u8(g_Team()));
	P.w_u8(u8(g_Squad()));
	P.w_u8(u8(g_Group()));
}

void CCustomMonster::net_Import(NET_Packet& P)
{
	R_ASSERT(Remote());
	net_update N;

	u8 flags;

	float health;
	P.r_float(health);
	// MP fork (§4C combat-outcome replication diag, -coop_npcdeath): does a server-authoritative NPC's
	// combat damage/death reach the co-op client? Health is streamed (below), so log on the client when a
	// replicated NPC puppet takes damage or its health crosses to <=0 — and whether the engine then treats
	// the puppet as dead (g_Alive()). Reveals if server kills render on the client or need a death event.
	{
		static int s_nd = -1;
		if (s_nd < 0) s_nd = strstr(Core.Params, "-coop_npcdeath") ? 1 : 0;
		if (s_nd && xr_enet::enabled() && !ai().get_alife())
		{
			const float old_hp = GetfHealth();
			if (health <= 0.f && old_hp > 0.f)
			{
				Msg("~ MP_NPCDEATH: [CLIENT] puppet id=%u DIED via stream (hp %.2f->%.2f) alive_after=%d",
					ID(), old_hp, health, (health > 0.f) ? 1 : 0);
				FlushLog();
			}
			else if (health < old_hp - 0.05f)
			{
				Msg("~ MP_NPCDEATH: [CLIENT] puppet id=%u took damage (hp %.2f->%.2f)", ID(), old_hp, health);
				FlushLog();
			}
		}
	}
	SetfHealth(health);

	P.r_u32(N.dwTimeStamp);
	P.r_u8(flags);
	P.r_vec3(N.p_pos);
	P.r_float /*r_angle8*/(N.o_model);
	P.r_float /*r_angle8*/(N.o_torso.yaw);
	P.r_float /*r_angle8*/(N.o_torso.pitch);
	P.r_float /*r_angle8*/(N.o_torso.roll);

	// MP fork (§19 co-op): DISCARD the replicated team/squad/group on a puppet. Stock assigns
	// these three ids raw, which is fine when nothing replicates them — but CAI_Stalker::
	// agent_manager() resolves through
	//   Level().seniority_holder().team(g_Team()).squad(g_Squad()).group(g_Group())
	// and a group's CAgentManager is only created when a member REGISTERS in it
	// (CEntity::ChangeTeam is the only path that unregisters from the old group and registers
	// in the new one). Assigning raw leaves the creature registered in the group it spawned
	// into while REPORTING a different one, so agent_manager() hands back a group that was
	// never populated and net_Relcase dereferences its null components — a real crash from
	// live play, symbolicated to CAgentManager::remove_links.
	// Keeping the spawn-time ids leaves the hierarchy self-consistent, which is what actually
	// matters here: a puppet runs no AI, so team/squad/group have no other consumer on this
	// client. (Routing through ChangeTeam would be the "correct" fix, but it churns
	// registration and fires on_before/on_after_change_team AI hooks on every difference —
	// not a trade worth making for a value nothing here reads.)
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

	// Belt and braces against the same NaN: refuse to let an invalid position into the
	// interpolation buffer at all, so it can never reach the physics shape.
	if (_valid(N.p_pos) && (NET.empty() || (NET.back().dwTimeStamp < N.dwTimeStamp)))
	{
		NET.push_back(N);
		NET_WasInterpolating = TRUE;
	}

	setVisible(TRUE);
	setEnabled(TRUE);
}

void CCustomMonster::shedule_Update(u32 DT)
{
	VERIFY(!g_Alive() || processing_enabled());
	// Queue shrink
	VERIFY(_valid(Position()));
	u32 dwTimeCL = Level().timeServer() - NET_Latency;
	VERIFY(!NET.empty());
	// MP fork (§19 co-op): UpdateCL interpolates puppets with a wider, sample-spacing-derived
	// window than NET_Latency, so culling against the stock 50ms cutoff would throw away the
	// history it needs and leave the keyframe search with no bracketing pair. Cull those
	// against the same worst-case window instead.
	if (Remote() && xr_enet::enabled() && !ai().get_alife())
		dwTimeCL = Level().timeServer() - 600;
	// MP fork (§19 co-op): UpdateCL interpolates replicated creatures with a larger,
	// sample-spacing-derived latency (see there), so trimming down to the stock two entries
	// against the 50ms window would discard samples it still needs. Keep a deeper history —
	// still bounded, so this cannot grow without limit.
	const u32 keep = (Remote() && xr_enet::enabled() && !ai().get_alife()) ? 8u : 2u;
	while ((NET.size() > keep) && (NET[1].dwTimeStamp < dwTimeCL)) NET.pop_front();

	float dt = float(DT) / 1000.f;

	// MP fork (§19 co-op): a replicated creature decides nothing on the thin client — the
	// server ran its senses and AI and sent us the result, and the Remote() branch below
	// already skips Think()/Exec_Action for exactly that reason. Running the visibility
	// raycasts and the memory managers for every puppet is pure cost, and a crash surface
	// besides: they reach into A-Life state this client does not have.
	// §14 step 3 (inversion, increment B): a DECISION-DRIVEN NPC (m_coop_locally_driven) is the
	// exception — the client runs its native AI locally to EXECUTE the replicated decision, so it
	// is NOT a coop_puppet here: it runs Exec_Visibility/memory().update below and the AI call
	// further down. This is the deliberate crossing of the crash surface the comment above warns
	// about; expect to symbolicate + guard A-Life-state derefs on the first flip.
	const bool coop_puppet = Remote() && xr_enet::enabled() && !ai().get_alife() && !m_coop_locally_driven;

	// *** general stuff
	if (g_Alive() && !coop_puppet)
	{
		if (false && g_mt_config.test(mtAiVision))
#ifndef DEBUG
			Device.seqParallel.push_back(fastdelegate::FastDelegate0<>(this, &CCustomMonster::Exec_Visibility));
#else // DEBUG
		{
			if (!psAI_Flags.test(aiStalker) || !!smart_cast<CActor*>(Level().CurrentEntity()))
				Device.seqParallel.push_back(fastdelegate::FastDelegate0<>(this,&CCustomMonster::Exec_Visibility));
			else
				Exec_Visibility				();
		}
#endif // DEBUG
		else
			Exec_Visibility();
		memory().update(dt);
	}
	inherited::shedule_Update(DT);

	// Queue setup
	if (dt > 3) return;

	m_dwCurrentTime = Device.dwTimeGlobal;

	VERIFY(_valid(Position()));
	// §14 step 3 (inversion, increment B): a decision-driven NPC falls into the AI branch below so
	// the client runs its Think/ProcessScripts/Exec_Action locally — under script control (the
	// decision system holds it via obj:script(true)), GetScriptControl() is true so ProcessScripts()
	// executes the replicated move order LOCALLY. Default (flag off) keeps the stock Remote no-op.
	if (Remote() && !m_coop_locally_driven)
	{
	}
	else
	{
		// §14 step 3: confirm (throttled) that a decision-driven NPC is running client-local AI.
		if (Remote() && m_coop_locally_driven)
		{
			static u32 s_localai_log = 0;
			if ((s_localai_log++ % 60) == 0)
			{
				Msg("- MP_COOP_LOCALAI: id=%u running client-local AI (script_ctrl=%d alive=%d)",
					ID(), GetScriptControl() ? 1 : 0, g_Alive() ? 1 : 0);
				FlushLog();
			}
		}
		// here is monster AI call
		m_fTimeUpdateDelta = dt;
		Device.Statistic->AI_Think.Begin();
		Device.Statistic->TEST1.Begin();
		if (GetScriptControl())
			ProcessScripts();
		else
		{
			if (Device.dwFrame > spawn_time() + g_AI_inactive_time)
				Think();
		}
		m_dwLastUpdateTime = Device.dwTimeGlobal;
		Device.Statistic->TEST1.End();
		Device.Statistic->AI_Think.End();

		// Look and action streams
		float temp = conditions().health();
		if (temp > 0)
		{
			Exec_Action(dt);
			VERIFY(_valid(Position()));
			//Exec_Visibility		();
			VERIFY(_valid(Position()));
			//////////////////////////////////////
			//Fvector C; float R;
			//////////////////////////////////////
			// С Олеся - ПИВО!!!! (Диме :-))))
			// m_PhysicMovementControl->GetBoundingSphere	(C,R);
			//////////////////////////////////////
			//Center(C);
			//R = Radius();
			//////////////////////////////////////
			/// #pragma todo("Oles to all AI guys: perf/logical problem: Only few objects needs 'feel_touch' why to call update for everybody?")
			///			feel_touch_update		(C,R);

			net_update uNext;
			uNext.dwTimeStamp = Level().timeServer();
			uNext.o_model = movement().m_body.current.yaw;
			uNext.o_torso = movement().m_body.current;
			uNext.p_pos = Position();
			uNext.fHealth = GetfHealth();
			NET.push_back(uNext);
		}
		else
		{
			net_update uNext;
			uNext.dwTimeStamp = Level().timeServer();
			uNext.o_model = movement().m_body.current.yaw;
			uNext.o_torso = movement().m_body.current;
			uNext.p_pos = Position();
			uNext.fHealth = GetfHealth();
			NET.push_back(uNext);
		}
	}
}

void CCustomMonster::net_update::lerp(CCustomMonster::net_update& A, CCustomMonster::net_update& B, float f)
{
	// 
	o_model = angle_lerp(A.o_model, B.o_model, f);
	o_torso.yaw = angle_lerp(A.o_torso.yaw, B.o_torso.yaw, f);
	o_torso.pitch = angle_lerp(A.o_torso.pitch, B.o_torso.pitch, f);
	p_pos.lerp(A.p_pos, B.p_pos, f);
	fHealth = A.fHealth * (1.f - f) + B.fHealth * f;
}

void CCustomMonster::update_sound_player()
{
	sound().update(client_update_fdelta());
}

void CCustomMonster::UpdateCL()
{
	START_PROFILE("CustomMonster/client_update")
		m_client_update_delta = (u32)std::min(Device.dwTimeGlobal - m_last_client_update_time, u32(100));
		m_last_client_update_time = Device.dwTimeGlobal;

		// §14 step 3 (increment D): age out the locally-driven flag when its decision stream stops.
		// The executor refreshes it (~every 3s) while decisions flow; once they cease, after the TTL
		// the NPC reverts to a normal coop_puppet (dense-streamed). TTL(5s) > broadcast period(3s), so
		// this never fires during a live decision stream — the continuous move demo is unaffected.
		if (m_coop_locally_driven &&
			(Device.dwTimeGlobal - m_coop_locally_driven_ts) > u32(COOP_LOCALLY_DRIVEN_TTL_MS))
		{
			m_coop_locally_driven = false;
			// §14 step 3 (D polish): the decision system is done with this NPC — RELEASE the script
			// control its decision executor took (obj:script(true) in mp_coop_decision_client), so it
			// reverts to a genuine dense-streamed puppet instead of a script-controlled object idling
			// on a completed order (which the coop_puppet stream path can't cleanly drive). Release
			// with the CURRENT control name so SetScriptControl's name-match guard passes.
			const bool released = GetScriptControl();
			if (released)
				SetScriptControl(false, GetScriptControlName());
			if (strstr(Core.Params, "-coop_local_ai"))
				Msg("- MP_COOP_LOCALAI: id=%u decision stream ended (>%ums) — reverting to dense-streamed puppet (script_ctrl_released=%d)",
				    ID(), u32(COOP_LOCALLY_DRIVEN_TTL_MS), released ? 1 : 0);
		}

#ifdef DEBUG
	if( animation_movement() )
				animation_movement()->DBG_verify_position_not_chaged();
#endif

		START_PROFILE("CustomMonster/client_update/inherited")
			inherited::UpdateCL();
		STOP_PROFILE

#ifdef DEBUG
	if( animation_movement() )
				animation_movement()->DBG_verify_position_not_chaged();
#endif

		CScriptEntity::process_sound_callbacks();

		/*	//. hack just to skip 'CalculateBones'
		if (sound().need_bone_data()) {
			// we do this because we know here would be virtual function call
			IKinematics					*kinematics = smart_cast<IKinematics*>(Visual());
			VERIFY						(kinematics);
			kinematics->CalculateBones	();
		}
		*/

		if (g_mt_config.test(mtSoundPlayer))
			Device.seqParallel.push_back(fastdelegate::FastDelegate0<>(this, &CCustomMonster::update_sound_player));
		else
		{
			START_PROFILE("CustomMonster/client_update/sound_player")
				update_sound_player();
			STOP_PROFILE
		}

		START_PROFILE("CustomMonster/client_update/network extrapolation")
			if (NET.empty())
			{
				update_animation_movement_controller();
				return;
			}

			m_dwCurrentTime = Device.dwTimeGlobal;

			// MP fork (§19 co-op): replicated creatures are NOT sampled at the packet rate.
			// The server ships update packets at psNET_ServerUpdate (30Hz), but the payload is
			// whatever CCustomMonster::shedule_Update last pushed into NET — and creatures run
			// on the SCHEDULER, so consecutive samples are 100-250ms apart, more when they are
			// distant or idle. Against a fixed 50ms NET_Latency the client's render time is
			// then almost always past the newest sample, so it takes the extrapolation branch
			// below: NET_Last snaps to the last sample and SelectAnimation is never called.
			// That is the jerky, "tripping" look — the puppet teleports between samples while
			// its walk cycle only advances in bursts. Buffer by the spacing we actually
			// observe so it genuinely interpolates. The extra lag is a fraction of a second on
			// creatures this client does not own, and is invisible next to the stutter it removes.
			const bool coop_puppet = Remote() && xr_enet::enabled() && !ai().get_alife();
			u32 latency = NET_Latency;
			if (coop_puppet)
			{
				// Floor of 100ms: with the server now sampling every frame the gaps are ~33ms,
				// and 50ms leaves barely one sample of slack — one late packet and we are back
				// to extrapolating. 100ms is three samples of headroom and is not perceptible on
				// a creature the client does not control.
				latency = 100;
				if (NET.size() >= 2)
				{
					// Twice the observed gap, so a creature the server is still sampling coarsely
					// (asleep, far away) also stays inside the buffer. Capped so one that was
					// idle for seconds cannot park us far in the past.
					const u32 spacing = NET.back().dwTimeStamp - NET[NET.size() - 2].dwTimeStamp;
					const u32 wanted = (spacing * 2 < 600) ? (spacing * 2) : 600;
					if (wanted > latency)
						latency = wanted;
				}
			}

			// distinguish interpolation/extrapolation
			u32 dwTime = Level().timeServer() - latency;

			// MP fork (§19 co-op): never ask the buffer for a time it cannot answer. The
			// keyframe search below has NO fallback — if no pair brackets dwTime it leaves
			// NET_Last untouched and skips SelectAnimation entirely, so the puppet freezes
			// where it stands and its animation stops advancing. Widening the interpolation
			// window made exactly that the common case (NPCs stationary and stuttering),
			// because the trim in shedule_Update still culls against the stock 50ms cutoff.
			// Clamping costs a little smoothing depth in the worst case and guarantees the
			// search always has an answer.
			if (coop_puppet && (dwTime < NET.front().dwTimeStamp))
				dwTime = NET.front().dwTimeStamp;

			// MP fork (§19 co-op): the puppet's real ground speed, from the two newest samples.
			// This is the number CStalkerAnimationManager::standing() actually needs. Computed
			// here rather than inside the interpolation branch: that branch is skipped whenever
			// a packet is late, and a stale speed left an NPC that had stopped still playing a
			// walk cycle.
			if (coop_puppet && (NET.size() >= 2))
			{
				const net_update& prev = NET[NET.size() - 2];
				const net_update& last = NET.back();
				const u32 gap = last.dwTimeStamp - prev.dwTimeStamp;
				Fvector d; d.sub(last.p_pos, prev.p_pos); d.y = 0.f;
				const float dist = d.magnitude();
				m_coop_net_speed = gap ? (dist / (float(gap) / 1000.f)) : 0.f;
				// Heading of travel. Keep the last real heading when nearly stationary so a
				// momentarily-still NPC does not snap its legs to a random direction.
				m_coop_net_moving = (dist > 0.03f);
				if (m_coop_net_moving)
				{
					float h, pch; d.getHP(h, pch);
					m_coop_net_heading = angle_normalize(-h); // getHP returns -a for rotateY(a)
				}
			}

			net_update& N = NET.back();
			if ((dwTime > N.dwTimeStamp) || (NET.size() < 2))
			{
				// BAD.	extrapolation
				NET_Last = N;

				// MP fork (§19 co-op): keep the animation ticking while extrapolating. Stock
				// code only advances it on the interpolation path, which is fine when that is
				// the normal case; for a puppet whose samples are momentarily late it means
				// freezing mid-stride and then snapping — half of the "tripping" look.
				if (coop_puppet)
				{
					// MP fork (§19 co-op): the animation manager decides which leg animation to play
					// by comparing the sight direction against movement().body_orientation().current
					// — and on a puppet nothing ever drives that, so it keeps whatever value it was
					// constructed with. In the alerted mental state (which is where a whole camp ends
					// up as soon as one NPC spots something) CStalkerAnimationManager takes exactly
					// that branch, so the forward/back/left/right choice was garbage and flipped from
					// frame to frame: NPCs walking sideways or backwards, and the leg animation
					// restarting every time the choice changed — the "shakey" look. We replicate the
					// body yaw already; put it where the animation manager reads it.
					movement().m_body.current.yaw = NET_Last.o_model;
					movement().m_body.target.yaw = NET_Last.o_model;

					if (!bfScriptAnimation())
						SelectAnimation(XFORM().k, movement().detail().direction(), movement().speed());
				}
			}
			else
			{
				// OK.	interpolation
				NET_WasExtrapolating = FALSE;
				// Search 2 keyframes for interpolation
				int select = -1;
				for (u32 id = 0; id < NET.size() - 1; ++id)
				{
					if ((NET[id].dwTimeStamp <= dwTime) && (dwTime <= NET[id + 1].dwTimeStamp)) select = id;
				}
				if (select >= 0)
				{
					// Interpolate state
					net_update& A = NET[select + 0];
					net_update& B = NET[select + 1];
					u32 d1 = dwTime - A.dwTimeStamp;
					u32 d2 = B.dwTimeStamp - A.dwTimeStamp;
					//			VERIFY					(d2);
					float factor = d2 ? (float(d1) / float(d2)) : 1.f;

					Fvector l_tOldPosition = Position();
					NET_Last.lerp(A, B, factor);
					if (Local())
					{
						NET_Last.p_pos = l_tOldPosition;
					}
					else
					{
						if (coop_puppet)
						{
						// MP fork (§19 co-op): the animation manager decides which leg animation to play
						// by comparing the sight direction against movement().body_orientation().current
						// — and on a puppet nothing ever drives that, so it keeps whatever value it was
						// constructed with. In the alerted mental state (which is where a whole camp ends
						// up as soon as one NPC spots something) CStalkerAnimationManager takes exactly
						// that branch, so the forward/back/left/right choice was garbage and flipped from
						// frame to frame: NPCs walking sideways or backwards, and the leg animation
						// restarting every time the choice changed — the "shakey" look. We replicate the
						// body yaw already; put it where the animation manager reads it.
						movement().m_body.current.yaw = NET_Last.o_model;
						movement().m_body.target.yaw = NET_Last.o_model;

						}
						if (!bfScriptAnimation())
							SelectAnimation(XFORM().k, movement().detail().direction(), movement().speed());
					}

					// Signal, that last time we used interpolation
					NET_WasInterpolating = TRUE;
					NET_Time = dwTime;
				}
				else if (coop_puppet)
				{
					// MP fork (§19 co-op): belt and braces for the hole described above. The
					// clamp should make this unreachable, but a puppet must never be left
					// tracking nothing, so pin it to the nearest end of the buffer and keep
					// its animation ticking rather than silently freezing.
					NET_Last = (dwTime < NET.front().dwTimeStamp) ? NET.front() : NET.back();
					movement().m_body.current.yaw = NET_Last.o_model;
					movement().m_body.target.yaw = NET_Last.o_model;
					if (!bfScriptAnimation())
						SelectAnimation(XFORM().k, movement().detail().direction(), movement().speed());
				}
			}
		STOP_PROFILE

#ifdef DEBUG
	if( animation_movement() )
				animation_movement()->DBG_verify_position_not_chaged();
#endif

		// §14 step 3 (inversion, increment C1b): a decision-driven NPC's position is DRIVEN BY ITS
		// OWN client-local AI, not the server stream. Run UpdatePositionAnimation (the movement
		// manager integrates its AI path via move_along_path) exactly like a Local object; it fills
		// NET_Last.p_pos (by reference) with the locally-computed advanced position. The apply block
		// below then writes that into the object XFORM (see the C1b note there).
		if ((Local() || m_coop_locally_driven) && g_Alive())
		{
#pragma todo("Dima to All : this is FAKE, network is not supported here!")

			// §14 step 3 (increment C1b): confirm the local movement pipeline is live and the body is
			// advancing. Logs the pipeline state + the object's current XZ (which reflects the PRIOR
			// frame's translate_over apply, so it advances frame-to-frame). GATED behind -coop_local_ai
			// (the C-workstream opt-in, parsed once) so it is silent in normal play, and throttled with
			// no per-sample FlushLog (relies on the normal log cadence) — CodeRabbit: avoid per-frame
			// synchronous I/O and unconditional hot-path logging.
			static int s_c1b_enabled = -1; // -1 unparsed, 0 off, 1 on
			if (s_c1b_enabled == -1)
				s_c1b_enabled = strstr(Core.Params, "-coop_local_ai") ? 1 : 0;
			if (s_c1b_enabled && Remote() && m_coop_locally_driven)
			{
				static u32 s_c1b_log = 0;
				if ((s_c1b_log++ % 30) == 0)
				{
					CPHMovementControl* mc = character_physics_support() ? character_physics_support()->movement() : NULL;
					const Fvector p = Position();
					Msg("- MP_C1B: id=%u en=%d path_n=%u desspd=%.3f pcompl=%d spd=%.3f chExist=%d chEn=%d pos=%.2f,%.2f",
						ID(),
						movement().enabled() ? 1 : 0,
						(u32)movement().detail().path().size(),
						movement().old_desirable_speed(),
						movement().path_completed() ? 1 : 0,
						movement().speed(),
						mc ? (mc->CharacterExist() ? 1 : 0) : -1,
						(mc && mc->CharacterExist()) ? (mc->IsCharacterEnabled() ? 1 : 0) : -1,
						p.x, p.z);
				}
			}
			UpdatePositionAnimation();
		}

		// Use interpolated/last state
		// §14 step 3 (increment C1b): a locally-driven monster MUST reach this block. UpdatePositionAnimation
		// above passes NET_Last.p_pos BY REFERENCE into move_along_path, which resets it to the object's
		// current position and then advances it along the local AI path — so after that call NET_Last.p_pos
		// holds the LOCAL-AI-COMPUTED position, not the streamed sample. translate_over(NET_Last.p_pos) below
		// is the ONLY writer of the object's visible XFORM for a monster (CPHMovementControl::SetPosition
		// only moves the physics box). C1 skipped this block for locally-driven NPCs on the mistaken premise
		// that NET_Last was still the stream — which discarded the computed motion and froze the monster at
		// spawn (diagnosed via MP_C1B: en=1 path_n=21 desspd=1.5 but the body never moved). This is exactly
		// the Local-monster path (original: `if (Local()) UpdatePositionAnimation(); ... translate_over(...)`).
		// The NET yaw/pitch still applied here acts as a soft heading leash toward the server's copy.
		if (g_Alive())
		{
			if (!animation_movement_controlled() && m_update_rotation_on_frame)
				XFORM().rotateY(NET_Last.o_model);
				if (strstr(Core.Params, "-xrnet_facelog"))
				{
					float applied_yaw, applied_pitch;
					XFORM().k.getHP(applied_yaw, applied_pitch);
					static u32 s_face_cl = 0;
					if ((++s_face_cl % 30) == 0)
					{
						Msg("- XRNET(face-cl): id=%u recv_o_model=%.3f applied=%.3f animctl=%d",
							ID(), NET_Last.o_model, angle_normalize(-applied_yaw),
							animation_movement_controlled() ? 1 : 0);
						FlushLog();
					}
				}
			if (!animation_movement_controlled())
				XFORM().translate_over(NET_Last.p_pos);

			if (!animation_movement_controlled() && m_update_rotation_on_frame)
			{
				Fmatrix M;
				M.setHPB(0.0f, -NET_Last.o_torso.pitch, 0.0f);
				XFORM().mulB_43(M);
			}
		}

#ifdef DEBUG
	if( animation_movement() )
				animation_movement()->DBG_verify_position_not_chaged();
#endif

#ifdef DEBUG
	if (IsMyCamera())
		UpdateCamera				();
#endif // DEBUG

		update_animation_movement_controller();

#ifdef DEBUG
	if( animation_movement() )
				animation_movement()->DBG_verify_position_not_chaged();
#endif

	STOP_PROFILE
}

void CCustomMonster::UpdatePositionAnimation()
{
	START_PROFILE("CustomMonster/client_update/movement")
		movement().on_frame(character_physics_support()->movement(), NET_Last.p_pos);
	STOP_PROFILE

	START_PROFILE("CustomMonster/client_update/animation")
		if (!bfScriptAnimation())
			SelectAnimation(XFORM().k, movement().detail().direction(), movement().speed());
	STOP_PROFILE
}

BOOL CCustomMonster::feel_visible_isRelevant(CObject* O)
{
	CEntityAlive* E = smart_cast<CEntityAlive*>(O);
	if (0 == E) return FALSE;
	if (E->g_Team() == g_Team()) return FALSE;
	return TRUE;
}

void CCustomMonster::eye_pp_s0()
{
	// Eye matrix
	IKinematics* V = smart_cast<IKinematics*>(Visual());
	V->CalculateBones();
	Fmatrix& mEye = V->LL_GetTransform(u16(eye_bone));
	Fmatrix X;
	X.mul_43(XFORM(), mEye);
	VERIFY(_valid(mEye));

	const MonsterSpace::SBoneRotation& rotation = head_orientation();

	VERIFY(_valid(rotation.current.yaw));
	VERIFY(_valid(m_fEyeShiftYaw));
	VERIFY(_valid(rotation.current.pitch));

	eye_matrix.setHPB(-rotation.current.yaw + m_fEyeShiftYaw, -rotation.current.pitch, 0);

#ifdef HOLDERCUSTOM_NEW
	if (cast_stalker() && cast_stalker()->Holder())
	{
		eye_matrix.setHPB(Direction().getH() + m_fEyeShiftYaw, Direction().getP(), 0);
	}
#endif

	eye_matrix.c.add(X.c, m_tEyeShift);

	VERIFY(_valid(eye_matrix));
}

void CCustomMonster::update_range_fov(float& new_range, float& new_fov, float start_range, float start_fov)
{
	const float standard_far_plane = eye_range;

	float current_fog_density = GamePersistent().Environment().CurrentEnv->fog_density;
	// 0=no_fog, 1=full_fog, >1 = super-fog
	float current_far_plane = GamePersistent().Environment().CurrentEnv->far_plane;
	// 300=standart, 50=super-fog

#ifdef HOLDERCUSTOM_NEW
    if (cast_stalker() && cast_stalker()->Holder())
    {
#ifdef STATIONARYMGUN_NEW
        CWeaponStatMgun* stm = smart_cast<CWeaponStatMgun*>(cast_stalker()->Holder());
        if (stm)
        {
            stm->OverrideRangeFOV(cast_game_object(), start_range);
        }
#endif
    }
#endif

	new_fov = start_fov;
	new_range =
		start_range
		*
		(
			_min(m_far_plane_factor * current_far_plane, standard_far_plane)
			/
			standard_far_plane
		)
		*
		(
			1.f
			/
			(
				1.f + m_fog_density_factor * current_fog_density
			)
		);
}

void CCustomMonster::eye_pp_s1()
{
	float new_range = eye_range, new_fov = eye_fov;
	if (g_Alive())
	{
#ifndef USE_STALKER_VISION_FOR_MONSTERS
		update_range_fov					(new_range, new_fov, human_being() ? memory().visual().current_state().m_max_view_distance*eye_range : eye_range, eye_fov);
#else
		update_range_fov(new_range, new_fov, memory().visual().current_state().m_max_view_distance * eye_range,
		                 eye_fov);
#endif
	}
	// Standart visibility
	Device.Statistic->AI_Vis_Query.Begin();
	Fmatrix mProject, mFull, mView;
	mView.build_camera_dir(eye_matrix.c, eye_matrix.k, eye_matrix.j);
	VERIFY(_valid(eye_matrix));
	mProject.build_projection(deg2rad(new_fov), 1, 0.1f, new_range);
	mFull.mul(mProject, mView);
	feel_vision_query(mFull, eye_matrix.c);
	Device.Statistic->AI_Vis_Query.End();
}

void CCustomMonster::eye_pp_s2()
{
	// Tracing
	Device.Statistic->AI_Vis_RayTests.Begin();
	u32 dwTime = Level().timeServer();
	u32 dwDT = dwTime - eye_pp_timestamp;
	eye_pp_timestamp = dwTime;
	feel_vision_update(this, eye_matrix.c, float(dwDT) / 1000.f, memory().visual().transparency_threshold());
	Device.Statistic->AI_Vis_RayTests.End();
}

void CCustomMonster::Exec_Visibility()
{
	//if (0==Sector())				return;
	if (!g_Alive()) return;

	Device.Statistic->AI_Vis.Begin();
	switch (eye_pp_stage % 2)
	{
	case 0:
		eye_pp_s0();
		eye_pp_s1();
		break;
	case 1: eye_pp_s2();
		break;
	}
	++eye_pp_stage;
	Device.Statistic->AI_Vis.End();
}

// MP fork (§4C headless-visibility diag): dump this NPC's feel_vision internals against an
// EXPLICIT target (the caller — the real-combat scenario via game_object:vissdbg(target) — knows
// the forced pair, so this does NOT depend on memory().enemy().selected(), which is itself nil
// headless because enemy selection needs the visible_now we are debugging).
// Disambiguates the visible_now=false drop that blocks headless ranged aiming:
//   in_frustum=1 but visible_now=0 (fuzzy<=0) -> B: o_trace/RayQuery reports occlusion on a clear LOS
//   in_frustum=0 while ang < fov/2 and dist < range -> the q_frustum pass never returns the target
//   enabled=0 / seen==0 -> C: feel_vision not enabled/updating for this NPC headless
void CCustomMonster::coop_vissdbg_dump(const CGameObject* target)
{
	if (!target) return;

	CObject* target_obj = const_cast<CObject*>(static_cast<const CObject*>(target));

	// geometry: target offset from this NPC's own eye, angle off the eye forward (eye_matrix.k)
	Fvector to;
	to.sub(target->Position(), eye_matrix.c);
	float dist = to.magnitude();
	float ang_deg = -1.f;
	if (dist > EPS_S)
	{
		Fvector dir = to;
		dir.div(dist);
		float d = eye_matrix.k.dotproduct(dir);
		clamp(d, -1.f, 1.f);
		ang_deg = rad2deg(acosf(d));
	}

	const bool  en       = memory().visual().enabled();
	const u32   seen_cnt = feel_vision_seen_count();
	const bool  in_frus  = feel_vision_in_frustum(target_obj);
	const float fuzzy    = feel_vision_fuzzy_of(target_obj);
	const float lum      = memory().visual().coop_vissdbg_luminocity(target);
	const bool  vis_now  = memory().visual().visible_now(target);
	const bool  vis_rn   = memory().visual().visible_right_now(target);

	Msg("~ MP_VISSDBG: id=%u target=%u dist=%.2f ang=%.1f fov=%.1f range=%.1f | enabled=%d seen=%u in_frustum=%d fuzzy=%.3f lum=%.4f | visible_right_now=%d visible_now=%d",
		ID(), target->ID(), dist, ang_deg, eye_fov, eye_range,
		en ? 1 : 0, seen_cnt, in_frus ? 1 : 0, fuzzy, lum,
		vis_rn ? 1 : 0, vis_now ? 1 : 0);
	FlushLog();
}

void CCustomMonster::UpdateCamera()
{
	float new_range = eye_range, new_fov = eye_fov;
	if (g_Alive())
		update_range_fov(new_range, new_fov, memory().visual().current_state().m_max_view_distance * eye_range,
		                 eye_fov);
	g_pGameLevel->Cameras().Update(eye_matrix.c, eye_matrix.k, eye_matrix.j, new_fov, .75f, new_range, 0);
}

void CCustomMonster::HitSignal(float /**perc/**/, Fvector& /**vLocalDir/**/, CObject* /**who/**/)
{
}

void CCustomMonster::Die(CObject* who)
{
	inherited::Die(who);
	//Level().RemoveMapLocationByID(this->ID());
	SetActorVisibility(ID(), 0.f);
}

BOOL CCustomMonster::net_Spawn(CSE_Abstract* DC)
{
	memory().reload(*cNameSect());
	memory().reinit();

	// MP fork (§19 co-op diag): creatures replicated from the server fail net_Spawn on
	// the thin client ("Failed to spawn entity"). Pin WHICH sub-spawn fails. Preserves
	// the original short-circuit (inherited runs only if movement ok, etc.). Temp.
	{
		const bool _coop = xr_enet::enabled() && !ai().get_alife();
		const bool mv = !!movement().net_Spawn(DC);
		const bool inh = mv && !!inherited::net_Spawn(DC);
		const bool scr = inh && !!CScriptEntity::net_Spawn(DC);
		if (!scr)
		{
			if (_coop)
				Msg("- XRNET(diag): monster net_Spawn FAIL '%s': movement=%d inherited=%d script=%d",
					cNameSect().c_str(), mv ? 1 : 0, inh ? 1 : 0, scr ? 1 : 0);
			return (FALSE);
		}
	}

	ISpatial* self = smart_cast<ISpatial*>(this);
	if (self)
	{
		self->spatial.type |= STYPE_VISIBLEFORAI;
		// enable react to sound only if alive
		if (g_Alive())
			self->spatial.type |= STYPE_REACTTOSOUND;
	}

	CSE_Abstract* e = (CSE_Abstract*)(DC);
	CSE_ALifeMonsterAbstract* E = smart_cast<CSE_ALifeMonsterAbstract*>(e);

	eye_matrix.identity();
	movement().m_body.current.yaw = movement().m_body.target.yaw = -E->o_torso.yaw;
	movement().m_body.current.pitch = movement().m_body.target.pitch = 0;
	SetfHealth(E->get_health());
	if (!g_Alive())
	{
		set_death_time();
		//		Msg						("%6d : Object [%d][%s][%s] is spawned DEAD",Device.dwTimeGlobal,ID(),*cName(),*cNameSect());
	}

	if (ai().get_level_graph() && UsedAI_Locations() && (e->ID_Parent == 0xffff))
	{
		if (ai().game_graph().valid_vertex_id(E->m_tGraphID))
			ai_location().game_vertex(E->m_tGraphID);

		if (
			ai().game_graph().valid_vertex_id(E->m_tNextGraphID)
			&&
			(ai().game_graph().vertex(E->m_tNextGraphID)->level_id() == ai().level_graph().level_id())
			&&
			movement().restrictions().accessible(
				ai().game_graph().vertex(
					E->m_tNextGraphID
				)->level_vertex_id()
			)
		)
			movement().set_game_dest_vertex(E->m_tNextGraphID);

		if (movement().restrictions().accessible(ai_location().level_vertex_id()))
			movement().set_level_dest_vertex(ai_location().level_vertex_id());
		else
		{
			Fvector dest_position;
			u32 level_vertex_id;
			level_vertex_id = movement().restrictions().accessible_nearest(
				ai().level_graph().vertex_position(ai_location().level_vertex_id()),
				dest_position);
			if (level_vertex_id != (u32)-1 && movement().restrictions().accessible(level_vertex_id))
			{
				movement().set_level_dest_vertex(level_vertex_id);
				movement().detail().set_dest_position(dest_position);
			}
		}
	}

	// Eyes
	eye_bone = smart_cast<IKinematics*>(Visual())->LL_BoneID(pSettings->r_string(cNameSect(), "bone_head"));

	// weapons
	if (Local())
	{
		net_update N;
		N.dwTimeStamp = Level().timeServer() - NET_Latency;
		N.o_model = -E->o_torso.yaw;
		N.o_torso.yaw = -E->o_torso.yaw;
		N.o_torso.pitch = 0;
		N.p_pos.set(Position());
		NET.push_back(N);

		N.dwTimeStamp += NET_Latency;
		NET.push_back(N);

		setVisible(TRUE);
		setEnabled(TRUE);
	}

	// Sheduler
	shedule.t_min = 100;
	shedule.t_max = 250; // This equaltiy is broken by Dima :-( // 30 * NET_Latency / 4;

	m_moving_object = xr_new<moving_object>(this);

	return TRUE;
}

#ifdef DEBUG
void CCustomMonster::OnHUDDraw(CCustomHUD *hud)
{
}
#endif

void CCustomMonster::Exec_Action(float /**dt/**/)
{
}

//void CCustomMonster::Hit(float P, Fvector &dir,CObject* who, s16 element,Fvector position_in_object_space, float impulse, ALife::EHitType hit_type)
void CCustomMonster::Hit(SHit* pHDS)
{
	if (!invulnerable())
		inherited::Hit(pHDS);
}

void CCustomMonster::OnEvent(NET_Packet& P, u16 type)
{
	inherited::OnEvent(P, type);
}

void CCustomMonster::net_Destroy()
{
	inherited::net_Destroy();
	CScriptEntity::net_Destroy();
	sound().unload();
	movement().net_Destroy();

	Device.remove_from_seq_parallel(
		fastdelegate::FastDelegate0<>(
			this,
			&CCustomMonster::update_sound_player
		)
	);
	Device.remove_from_seq_parallel(
		fastdelegate::FastDelegate0<>(
			this,
			&CCustomMonster::Exec_Visibility
		)
	);

#ifdef DEBUG
	DBG().on_destroy_object(this);
#endif

	xr_delete(m_moving_object);

	SetActorVisibility(ID(), 0.0f);
}

BOOL CCustomMonster::UsedAI_Locations()
{
	return (TRUE);
}

void CCustomMonster::PitchCorrection()
{
	CLevelGraph::SContour contour;
	ai().level_graph().contour(contour, ai_location().level_vertex_id());

	Fplane P;
	P.build(contour.v1, contour.v2, contour.v3);

	Fvector position_on_plane;
	P.project(position_on_plane, Position());

	// находим проекцию точки, лежащей на векторе текущего направления
	Fvector dir_point, proj_point;
	dir_point.mad(position_on_plane, Direction(), 1.f);
	P.project(proj_point, dir_point);

	// получаем искомый вектор направления
	Fvector target_dir;
	target_dir.sub(proj_point, position_on_plane);

	float yaw, pitch;
	target_dir.getHP(yaw, pitch);

	movement().m_body.target.pitch = -pitch;
}

bool CCustomMonster::feel_touch_on_contact(CObject* O)
{
	CCustomZone* custom_zone = smart_cast<CCustomZone*>(O);
	if (!custom_zone)
		return (true);

	Fsphere sphere;
	sphere.P = Position();
	sphere.R = EPS_L;
	if (custom_zone->inside(sphere))
		return (true);

	return (false);
}

bool CCustomMonster::feel_touch_contact(CObject* O)
{
	CCustomZone* custom_zone = smart_cast<CCustomZone*>(O);
	if (!custom_zone)
		return (true);

	Fsphere sphere;
	sphere.P = Position();
	sphere.R = 0.f;
	if (custom_zone->inside(sphere))
		return (true);

	return (false);
}

void CCustomMonster::set_ready_to_save()
{
	inherited::set_ready_to_save();
	memory().enemy().set_ready_to_save();
}

void CCustomMonster::load_killer_clsids(LPCSTR section)
{
	m_killer_clsids.clear();
	LPCSTR killers = pSettings->r_string(section, "killer_clsids");
	string16 temp;
	for (u32 i = 0, n = _GetItemCount(killers); i < n; ++i)
		m_killer_clsids.push_back(TEXT2CLSID(_GetItem(killers, i, temp)));
}

bool CCustomMonster::is_special_killer(CObject* obj)
{
	return (obj && (std::find(m_killer_clsids.begin(), m_killer_clsids.end(), obj->CLS_ID) != m_killer_clsids.end()));
}

float CCustomMonster::feel_vision_mtl_transp(CObject* O, u32 element)
{
	return (memory().visual().feel_vision_mtl_transp(O, element));
}

void CCustomMonster::feel_sound_new(CObject* who, int type, CSound_UserDataPtr user_data, const Fvector& position,
                                    float power)
{
	// Lain: added
	if (!g_Alive())
	{
		return;
	}
	if (getDestroy())
	{
		return;
	}
	memory().sound().feel_sound_new(who, type, user_data, position, power);
}

bool CCustomMonster::useful(const CItemManager* manager, const CGameObject* object) const
{
	return (memory().item().useful(object));
}

float CCustomMonster::evaluate(const CItemManager* manager, const CGameObject* object) const
{
	return (memory().item().evaluate(object));
}

bool CCustomMonster::useful(const CEnemyManager* manager, const CEntityAlive* object) const
{
	return (memory().enemy().useful(object));
}

float CCustomMonster::evaluate(const CEnemyManager* manager, const CEntityAlive* object) const
{
	return (memory().enemy().evaluate(object));
}

bool CCustomMonster::useful(const CDangerManager* manager, const CDangerObject& object) const
{
	return (memory().danger().useful(object));
}

float CCustomMonster::evaluate(const CDangerManager* manager, const CDangerObject& object) const
{
	return (memory().danger().evaluate(object));
}

CMovementManager* CCustomMonster::create_movement_manager()
{
	return (xr_new<CMovementManager>(this));
}

CSound_UserDataVisitor* CCustomMonster::create_sound_visitor()
{
	return (m_sound_user_data_visitor = xr_new<CSound_UserDataVisitor>());
}

CMemoryManager* CCustomMonster::create_memory_manager()
{
	return (xr_new<CMemoryManager>(this, create_sound_visitor()));
}

const SRotation CCustomMonster::Orientation() const
{
	return (movement().m_body.current);
};

const MonsterSpace::SBoneRotation& CCustomMonster::head_orientation() const
{
	return (movement().m_body);
}

DLL_Pure* CCustomMonster::_construct()
{
	m_memory_manager = create_memory_manager();
	m_movement_manager = create_movement_manager();
	m_sound_player = xr_new<CSoundPlayer>(this);

	inherited::_construct();
	CScriptEntity::_construct();

	return (this);
}

void CCustomMonster::net_Relcase(CObject* object)
{
	inherited::net_Relcase(object);
	memory().remove_links(object);
}

void CCustomMonster::set_fov(float new_fov)
{
	VERIFY(new_fov > 0.f);
	eye_fov = new_fov;
}

void CCustomMonster::set_range(float new_range)
{
	VERIFY(new_range > 1.f);
	eye_range = new_range;
}

void CCustomMonster::on_restrictions_change()
{
	memory().on_restrictions_change();
	movement().on_restrictions_change();
}

LPCSTR CCustomMonster::visual_name(CSE_Abstract* server_entity)
{
	return (inherited::visual_name(server_entity));
}

void CCustomMonster::on_enemy_change(const CEntityAlive* enemy)
{
}

CVisualMemoryManager* CCustomMonster::visual_memory() const
{
	return (&memory().visual());
}

void CCustomMonster::save(NET_Packet& packet)
{
	inherited::save(packet);
	if (g_Alive())
		memory().save(packet);
}

void CCustomMonster::load(IReader& packet)
{
	inherited::load(packet);
	if (g_Alive())
		memory().load(packet);
}


bool CCustomMonster::update_critical_wounded(const u16& bone_id, const float& power)
{
	// object should not be critical wounded
	VERIFY(m_critical_wound_type == u32(-1));
	// check 'multiple updates during last hit' situation
	VERIFY(Device.dwTimeGlobal >= m_last_hit_time);

	if (m_critical_wound_threshold < 0) return (false);


	float time_delta = m_last_hit_time ? float(Device.dwTimeGlobal - m_last_hit_time) / 1000.f : 0.f;
	m_critical_wound_accumulator += power - m_critical_wound_decrease_quant * time_delta;
	clamp(m_critical_wound_accumulator, 0.f, m_critical_wound_threshold);

#if 0//def _DEBUG
	Msg								(
		"%6d [%s] update_critical_wounded: %f[%f] (%f,%f) [%f]",
		Device.dwTimeGlobal,
		*cName(),
		m_critical_wound_accumulator,
		power,
		m_critical_wound_threshold,
		m_critical_wound_decrease_quant,
		time_delta
	);
#endif // DEBUG

	m_last_hit_time = Device.dwTimeGlobal;
	if (m_critical_wound_accumulator < m_critical_wound_threshold)
		return (false);

	m_last_hit_time = 0;
	m_critical_wound_accumulator = 0.f;

	if (critical_wound_external_conditions_suitable())
	{
		BODY_PART::const_iterator I = m_bones_body_parts.find(bone_id);
		if (I == m_bones_body_parts.end()) return (false);

		m_critical_wound_type = (*I).second;

		critical_wounded_state_start();

		return (true);
	}

	return (false);
}

#ifdef DEBUG

extern void dbg_draw_frustum (float FOV, float _FAR, float A, Fvector &P, Fvector &D, Fvector &U);
void draw_visiblity_rays	(CCustomMonster *self, const CObject *object, collide::rq_results& rq_storage);

void CCustomMonster::OnRender()
{
	DRender->OnFrameEnd();
	//RCache.OnFrameEnd				();

	{
		float const radius						= .075f;
		xr_vector<u32> const& path				= movement().level_path().path();
		xr_vector<u32>::const_iterator i		= path.begin();
		xr_vector<u32>::const_iterator const e	= path.end();
		for ( ; i != e; ++i )
			Level().debug_renderer().draw_aabb	( ai().level_graph().vertex_position(*i), radius, radius, radius, D3DCOLOR_XRGB(255,22,255) );
	}

	for (int i=0; i<1; ++i) {
		const xr_vector<CDetailPathManager::STravelPoint>		&keys	= !i ? movement().detail().m_key_points					: movement().detail().m_key_points;
		const xr_vector<DetailPathManager::STravelPathPoint>	&path	= !i ? movement().detail().path()	: movement().detail().path();
		u32									color0	= !i ? D3DCOLOR_XRGB(0,255,0)		: D3DCOLOR_XRGB(0,0,255);
		u32									color1	= !i ? D3DCOLOR_XRGB(255,0,0)		: D3DCOLOR_XRGB(255,255,0);
		u32									color2	= !i ? D3DCOLOR_XRGB(0,0,255)		: D3DCOLOR_XRGB(0,255,255);
		u32									color3	= !i ? D3DCOLOR_XRGB(255,255,255)	: D3DCOLOR_XRGB(255,0,255);
		float								radius0 = !i ? .1f : .15f;
		float								radius1 = !i ? .2f : .3f;
		{
			for (u32 I=1; I<path.size(); ++I) {
				const DetailPathManager::STravelPathPoint&	N1 = path[I-1];	Fvector	P1; P1.set(N1.position); P1.y+=0.1f;
				const DetailPathManager::STravelPathPoint&	N2 = path[I];	Fvector	P2; P2.set(N2.position); P2.y+=0.1f;
				if (!fis_zero(P1.distance_to_sqr(P2),EPS_L))
					Level().debug_renderer().draw_line			(Fidentity,P1,P2,color0);
				if ((path.size() - 1) == I) // песледний box?
					Level().debug_renderer().draw_aabb			(P1,radius0,radius0,radius0,color1);
				else 
					Level().debug_renderer().draw_aabb			(P1,radius0,radius0,radius0,color2);
			}

			for (u32 I=1; I<keys.size(); ++I) {
				CDetailPathManager::STravelPoint	temp;
				temp		= keys[I - 1]; 
				Fvector		P1;
				P1.set		(temp.position.x,ai().level_graph().vertex_plane_y(temp.vertex_id),temp.position.y);
				P1.y		+= 0.1f;

				temp		= keys[I]; 
				Fvector		P2;
				P2.set		(temp.position.x,ai().level_graph().vertex_plane_y(temp.vertex_id),temp.position.y);
				P2.y		+= 0.1f;

				if (!fis_zero(P1.distance_to_sqr(P2),EPS_L))
					Level().debug_renderer().draw_line		(Fidentity,P1,P2,color1);
				Level().debug_renderer().draw_aabb			(P1,radius1,radius1,radius1,color3);
			}
		}
	}
	{
		u32					node = movement().level_dest_vertex_id();
		if (node == u32(-1)) node = 0;

		Fvector				P1 = ai().level_graph().vertex_position(node);
		P1.y				+= 1.f;
		Level().debug_renderer().draw_aabb	(P1,.5f,1.f,.5f,D3DCOLOR_XRGB(255,0,0));
	}
	if (g_Alive()) {
		if (memory().enemy().selected()) {
			Fvector				P1 = memory().memory(memory().enemy().selected()).m_object_params.m_position;
			P1.y				+= 1.f;
			Level().debug_renderer().draw_aabb	(P1,1.f,1.f,1.f,D3DCOLOR_XRGB(0,0,0));
		}

		if (memory().danger().selected()) {
			Fvector				P1 = memory().danger().selected()->position();
			P1.y				+= 1.f;
			Level().debug_renderer().draw_aabb	(P1,1.f,1.f,1.f,D3DCOLOR_XRGB(0,0,0));
		}
	}

	if (psAI_Flags.test(aiFrustum)) {
		float					new_range = eye_range, new_fov = eye_fov;
		
		if (g_Alive())
			update_range_fov	(new_range, new_fov, memory().visual().current_state().m_max_view_distance*eye_range, eye_fov);

		dbg_draw_frustum		(new_fov,new_range,1,eye_matrix.c,eye_matrix.k,eye_matrix.j);
	}

	if (psAI_Flags.test(aiMotion)) 
		if (character_physics_support())
			character_physics_support()->movement()->dbg_Draw();
	
	if (bDebug)
		smart_cast<IKinematics*>(Visual())->DebugRender(XFORM());


#if 0
	DBG().get_text_tree().clear			();
	debug::text_tree& text_tree		=	DBG().get_text_tree().find_or_add("ActorView");

	Fvector collide_position;
	collide::rq_results	temp_rq_results;
	Fvector sizes			=	{ 0.2f, 0.2f, 0.2f };

	for ( u32 i=0; i<2; ++i )
	{
		Fvector start		=	{ -8.7, 1.6, -4.67 };
		Fvector end			=	{ -9.45, 1.3, -0.24 };

		bool use_p2			=	false;
		ai_dbg::get_var			("p2", use_p2);

		if ( use_p2 ^ i )
		{
			start.x			+=	-1.f;
			end.x			+=	-1.f;
		}

		Fvector velocity	=	end - start;
		float const jump_time	=	0.3f;
		TransferenceToThrowVel	(velocity,jump_time,physics_world()->Gravity());

		bool const result	=	trajectory_intersects_geometry	(jump_time, 
																 start,
																 end,
																 velocity,
																 collide_position,
																 this,
																 NULL,
																 temp_rq_results,
																 & m_jump_picks,
																 & m_jump_collide_tris,
																 sizes);

		text_tree.add_line(i ? "box1" : "box2", result);
	}
#endif // #if 0

	if (m_jump_picks.size() < 1)
		return;

	xr_vector<trajectory_pick>::const_iterator	I = m_jump_picks.begin();
	xr_vector<trajectory_pick>::const_iterator	E = m_jump_picks.end();
	for ( ; I != E; ++I )
	{
		trajectory_pick pick				=	*I;	

		float const inv_nx			=	(pick.invert_x & 1) ? -1.f : 1.f;
		float const inv_ny			=	(pick.invert_y & 1) ? -1.f : 1.f;
		float const inv_nz			=	(pick.invert_z & 1) ? -1.f : 1.f;

		float const inv_x			=	(pick.invert_x & 2) ? -1.f : 1.f;
		float const inv_y			=	(pick.invert_y & 2) ? -1.f : 1.f;
		float const inv_z			=	(pick.invert_z & 2) ? -1.f : 1.f;

		Fvector const traj_start	=	pick.center	- pick.z_axis * pick.sizes.z * 0.5f * inv_z;
		Fvector const traj_end		=	pick.center	+ pick.z_axis * pick.sizes.z * 0.5f * inv_z;

		Fvector const z_offs[]		=	{ (  pick.x_axis * pick.sizes.x * 0.5f) + (pick.y_axis * pick.sizes.y * 0.5f), 
										  (- pick.x_axis * pick.sizes.x * 0.5f) + (pick.y_axis * pick.sizes.y * 0.5f),
										  (  pick.x_axis * pick.sizes.x * 0.5f) - (pick.y_axis * pick.sizes.y * 0.5f), 
										  (- pick.x_axis * pick.sizes.x * 0.5f) - (pick.y_axis * pick.sizes.y * 0.5f), };

		Fvector const z_normal		=	- pick.z_axis * 0.1 * inv_nz;
		Level().debug_renderer().draw_line	(Fidentity,traj_start,traj_start + z_normal,D3DCOLOR_XRGB(128,255,128));
		Level().debug_renderer().draw_line	(Fidentity,traj_end,traj_end - z_normal,D3DCOLOR_XRGB(128,255,128));

		for ( u32 i=0; i<sizeof(z_offs)/sizeof(z_offs[0]); ++i )
			Level().debug_renderer().draw_line	(Fidentity,traj_start + z_offs[i],traj_end+ z_offs[i],D3DCOLOR_XRGB(255,255,128));

		Fvector const hor_start		=	pick.center	- pick.x_axis * pick.sizes.x * 0.5f * inv_x;
		Fvector const hor_end		=	pick.center	+ pick.x_axis * pick.sizes.x * 0.5f * inv_x;

		Fvector const x_offs[]		=	{ (  pick.y_axis * pick.sizes.y * 0.5f) + (pick.z_axis * pick.sizes.z * 0.5f), 
										  (- pick.y_axis * pick.sizes.y * 0.5f) + (pick.z_axis * pick.sizes.z * 0.5f),
										  (  pick.y_axis * pick.sizes.y * 0.5f) - (pick.z_axis * pick.sizes.z * 0.5f), 
										  (- pick.y_axis * pick.sizes.y * 0.5f) - (pick.z_axis * pick.sizes.z * 0.5f), };

		Fvector const x_normal		=	- pick.x_axis * 0.1 * inv_nx;
		Level().debug_renderer().draw_line	(Fidentity,hor_start,hor_start + x_normal,D3DCOLOR_XRGB(128,255,128));
		Level().debug_renderer().draw_line	(Fidentity,hor_end,hor_end - x_normal,D3DCOLOR_XRGB(128,255,128));

		for ( u32 i=0; i<sizeof(x_offs)/sizeof(x_offs[0]); ++i )
			Level().debug_renderer().draw_line	(Fidentity,hor_start + x_offs[i],hor_end+ x_offs[i],D3DCOLOR_XRGB(255,255,128));

		Fvector const ver_start		=	pick.center	- pick.y_axis * pick.sizes.y * 0.5f * inv_y;
		Fvector const ver_end		=	pick.center	+ pick.y_axis * pick.sizes.y * 0.5f * inv_y;

		Fvector const y_offs[]		=	{ (  pick.x_axis * pick.sizes.x * 0.5f) + (pick.z_axis * pick.sizes.z * 0.5f), 
										  (- pick.x_axis * pick.sizes.x * 0.5f) + (pick.z_axis * pick.sizes.z * 0.5f),
										  (  pick.x_axis * pick.sizes.x * 0.5f) - (pick.z_axis * pick.sizes.z * 0.5f), 
										  (- pick.x_axis * pick.sizes.x * 0.5f) - (pick.z_axis * pick.sizes.z * 0.5f), };

		Fvector const y_normal		=	- pick.y_axis * 0.1 * inv_ny;
		Level().debug_renderer().draw_line	(Fidentity,ver_start,ver_start + y_normal,D3DCOLOR_XRGB(128,255,128));
		Level().debug_renderer().draw_line	(Fidentity,ver_end,ver_end - y_normal,D3DCOLOR_XRGB(128,255,128));

		for ( u32 i=0; i<sizeof(y_offs)/sizeof(y_offs[0]); ++i )
			Level().debug_renderer().draw_line	(Fidentity,ver_start + y_offs[i],ver_end+ y_offs[i],D3DCOLOR_XRGB(255,255,128));

		Level().debug_renderer().draw_line	(Fidentity,traj_start,traj_end,D3DCOLOR_XRGB(255,0,0));
	}

	for ( u32 i=0; i<m_jump_collide_tris.size(); i+=3 )
	{
		Fvector const v1	=	m_jump_collide_tris[i];
		Fvector const v2	=	m_jump_collide_tris[i+1];
		Fvector const v3	=	m_jump_collide_tris[i+2];

		Fmatrix unit;
		unit.identity			();

		Level().debug_renderer().draw_line(unit, v1, v2, D3DCOLOR_XRGB(255,255,255));
		Level().debug_renderer().draw_line(unit, v1, v3, D3DCOLOR_XRGB(255,255,255));
		Level().debug_renderer().draw_line(unit, v2, v3, D3DCOLOR_XRGB(255,255,255));
	}
}
#endif // DEBUG

void CCustomMonster::spatial_move()
{
	inherited::spatial_move();

	get_moving_object()->on_object_move();
}

Fvector CCustomMonster::predict_position(const float& time_to_check) const
{
	return (movement().predict_position(time_to_check));
}

Fvector CCustomMonster::target_position() const
{
	return (movement().target_position());
}

void CCustomMonster::create_anim_mov_ctrl(CBlend* b, Fmatrix* start_pose, bool local_animation)
{
	bool already_initialized = animation_movement_controlled();
	inherited::create_anim_mov_ctrl(b, start_pose, local_animation);

	if (already_initialized)
		return;

	m_movement_enabled_before_animation_controller = movement().enabled();
	movement().enable_movement(false);
}

void CCustomMonster::destroy_anim_mov_ctrl()
{
	inherited::destroy_anim_mov_ctrl();

	movement().enable_movement(m_movement_enabled_before_animation_controller);

	float roll;
	XFORM().getHPB(movement().m_body.current.yaw, movement().m_body.current.pitch, roll);

	movement().m_body.current.yaw *= -1.f;
	movement().m_body.current.pitch *= -1.f;

	movement().m_body.target.yaw = movement().m_body.current.yaw;
	movement().m_body.target.pitch = movement().m_body.current.pitch;

	NET_Last.o_model = movement().m_body.current.yaw;
	NET_Last.o_torso.pitch = movement().m_body.current.pitch;
}

void CCustomMonster::ForceTransform(const Fmatrix& m)
{
	character_physics_support()->ForceTransform(m);
	const float block_damage_time_seconds = 2.f;
	if (!IsGameTypeSingle())
		character_physics_support()->movement()->BlockDamageSet(u64(block_damage_time_seconds / fixed_step));
}

Fvector CCustomMonster::spatial_sector_point()
{
	//if ( g_Alive() )
	//	return						inherited::spatial_sector_point( );

	//if ( !animation_movement() )
	return inherited::spatial_sector_point().add(Fvector().set(0.f, Radius() * .5f, 0.f));

	//IKinematics* const kinematics	= smart_cast<IKinematics*>(Visual());
	//VERIFY							(kinematics);
	//u16 const root_bone_id			= kinematics->LL_BoneID("bip01_spine");

	//Fmatrix local;
	//kinematics->Bone_GetAnimPos		( local, root_bone_id, u8(-1), false );

	//Fmatrix result;
	//result.mul_43					( XFORM(), local );
	//return							result.c;
}
