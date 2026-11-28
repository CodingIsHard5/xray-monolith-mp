#include "pch_script.h"
#include "GameObject.h"
//#include "../Include/xrRender/RenderVisual.h"
#include "../Include/xrRender/RenderVisual.h"
#include "../xrphysics/PhysicsShell.h"
#include "ai_space.h"
#include "CustomMonster.h"
#include "physicobject.h"
#include "HangingLamp.h"
#include "../xrphysics/PhysicsShell.h"
#include "game_sv_single.h"
#include "level_graph.h"
#include "ph_shell_interface.h"
#include "script_game_object.h"
#include "xrserver_objects_alife.h"
#include "xrServer_Objects_ALife_Items.h"
#include "game_cl_base.h"
#include "object_factory.h"
#include "../Include/xrRender/Kinematics.h"
#include "ai_object_location_impl.h"
#include "game_graph.h"
#include "ai_debug.h"
#include "../xrEngine/igame_level.h"
#include "level.h"
#include "script_callback_ex.h"
#include "../xrphysics/MathUtils.h"
#include "game_cl_base_weapon_usage_statistic.h"
#include "game_cl_mp.h"
#include "reward_event_generator.h"
#include "game_level_cross_table.h"
#include "ai_obstacle.h"
#include "magic_box3.h"
#include "animation_movement_controller.h"
#include "../xrengine/xr_collide_form.h"
#include "script_attachment_manager.h"
#include "player_hud.h"
#include "CharacterPhysicsSupport.h"
#include "debug_renderer.h"
#include "../xrPhysics/PHElement.h"
#include "Actor.h" // S2 stage 4b: g_actor, CActor::Holder
#include "Inventory.h" // S2 stage 4d-2 S4b witness: the witness NPC's active item
#include "../xrEngine/sim_pose.h" // S2 stage 4d-2 S4b: sim_pose_bone_drawn

extern MagicBox3 MagicMinBox(int iQuantity, const Fvector* akPoint);

#pragma warning(push)
#pragma warning(disable:4995)
#include <malloc.h>
#pragma warning(pop)

#ifdef DEBUG
#	include "debug_renderer.h"
#	include "PHDebug.h"
#endif

extern ENGINE_API bool g_dedicated_server;

CGameObject::CGameObject()
{
	m_ai_obstacle = 0;

	init();
	//-----------------------------------------
	m_bCrPr_Activated = false;
	m_dwCrPr_ActivationStep = 0;
	m_spawn_time = 0;
	m_ai_location = !g_dedicated_server ? xr_new<CAI_ObjectLocation>() : 0;
	m_server_flags.one();

	m_callbacks = xr_new<CALLBACK_MAP>();
	m_anim_mov_ctrl = 0;
	m_instance_holder = nullptr;
}

CGameObject::~CGameObject()
{
	VERIFY(!animation_movement( ));
	VERIFY(!m_ini_file);
	VERIFY(!m_lua_game_object);
	VERIFY(!m_spawned);
	xr_delete(m_ai_location);
	xr_delete(m_callbacks);
	xr_delete(m_ai_obstacle);
	delete_data(m_script_attachments);
}

void CGameObject::init()
{
	m_lua_game_object = 0;
	m_script_clsid = -1;
	m_ini_file = 0;
	m_spawned = false;
}

void CGameObject::Load(LPCSTR section)
{
	inherited::Load(section);
	SpatialComponent->spatial.type &= ~STYPE_REACTTOSOUND;
}

void CGameObject::reinit()
{
	m_visual_callback.clear();
	if (!g_dedicated_server)
		ai_location().reinit();

	// clear callbacks	
	for (CALLBACK_MAP_IT it = m_callbacks->begin(); it != m_callbacks->end(); ++it) it->second.clear();
}

void CGameObject::reload(LPCSTR section)
{
	m_script_clsid = object_factory().script_clsid(CLS_ID);
}

void CGameObject::net_Destroy()
{
#ifdef DEBUG
	if (psAI_Flags.test(aiDestroy))
		Msg					("Destroying client object [%d][%s][%x]",ID(),*cName(),this);
#endif

	VERIFY(m_spawned);

	::luabind::functor<void> funct;
	if (ai().script_engine().functor("_G.CGameObject_NetDestroy", funct))
	{
		funct(this->lua_game_object());
	}

	if (m_anim_mov_ctrl)
		destroy_anim_mov_ctrl();

	xr_delete(m_ini_file);

	m_script_clsid = -1;
    if (Visual() && Visual()->dcast_PKinematics())
    {
        IKinematics* K = Visual()->dcast_PKinematics();
        K->Callback(0, 0);
        ::Render->remove_SkeletonWallmarksFromObject(K);
    }

	if (m_instance_holder) m_instance_holder->ClearModelInstance();

	inherited::net_Destroy();
	setReady(FALSE);

	if (Level().IsDemoPlayStarted() && ID() == u16(-1))
	{
		Msg("Destroying demo_spectator object");
	}
	else
	{
		g_pGameLevel->Objects.net_Unregister(this);
	}

	if (this == Level().CurrentEntity())
	{
		if (!Level().IsDemoPlayStarted())
		{
			Level().SetControlEntity(0);
		}
		Level().SetEntity(0); // do not switch !!!
	}

	Level().RemoveObject_From_4CrPr(this);

	//.	Parent									= 0;

	CScriptBinder::net_Destroy();

	xr_delete(m_lua_game_object);
	m_spawned = false;
}

void CGameObject::OnEvent(NET_Packet& P, u16 type)
{
	switch (type)
	{
	case GE_HIT:
	case GE_HIT_STATISTIC:
		{
			/*
						u16				id,weapon_id;
						Fvector			dir;
						float			power, impulse;
						s16				element;
						Fvector			position_in_bone_space;
						u16				hit_type;
						float			ap = 0.0f;
			
						P.r_u16			(id);
						P.r_u16			(weapon_id);
						P.r_dir			(dir);
						P.r_float		(power);
						P.r_s16			(element);
						P.r_vec3		(position_in_bone_space);
						P.r_float		(impulse);
						P.r_u16			(hit_type);	//hit type
						if ((ALife::EHitType)hit_type == ALife::eHitTypeFireWound)
						{
							P.r_float	(ap);
						}
			
						CObject*	Hitter = Level().Objects.net_Find(id);
						CObject*	Weapon = Level().Objects.net_Find(weapon_id);
			
						SHit	HDS = SHit(power, dir, Hitter, element, position_in_bone_space, impulse, (ALife::EHitType)hit_type, ap);
			*/
			SHit HDS;
			HDS.PACKET_TYPE = type;
			HDS.Read_Packet_Cont(P);
			//			Msg("Hit received: %d[%d,%d]", HDS.whoID, HDS.weaponID, HDS.BulletID);
			CObject* Hitter = Level().Objects.net_Find(HDS.whoID);
			CObject* Weapon = Level().Objects.net_Find(HDS.weaponID);
			HDS.who = Hitter;
			if (!HDS.who)
			{
				Msg("! ERROR: hitter object [%d] is NULL on client.", HDS.whoID);
			}
			//-------------------------------------------------------
			switch (HDS.PACKET_TYPE)
			{
			case GE_HIT_STATISTIC:
				{
					if (GameID() != eGameIDSingle)
						Game().m_WeaponUsageStatistic->OnBullet_Check_Request(&HDS);
				}
				break;
			default:
				{
				}
				break;
			}
			SetHitInfo(Hitter, Weapon, HDS.bone(), HDS.p_in_bone_space, HDS.dir);
			Hit(&HDS);
			//---------------------------------------------------------------------------
			if (GameID() != eGameIDSingle)
			{
				Game().m_WeaponUsageStatistic->OnBullet_Check_Result(false);
				game_cl_mp* mp_game = smart_cast<game_cl_mp*>(&Game());
				if (mp_game->get_reward_generator())
					mp_game->get_reward_generator()->OnBullet_Hit(Hitter, this, Weapon, HDS.boneID);
			}
			//---------------------------------------------------------------------------
		}
		break;
	case GE_DESTROY:
		{
			if (H_Parent())
			{
				Msg("! ERROR (GameObject): GE_DESTROY arrived to object[%d][%s], that has parent[%d][%s], frame[%d]",
				    ID(), cNameSect().c_str(),
				    H_Parent()->ID(), H_Parent()->cName().c_str(), Device.dwFrame);

				// This object will be destroy on call function <H_Parent::Destroy>
				// or it will be call <H_Parent::Reject>  ==>  H_Parent = NULL
				// !!! ___ it is necessary to be check!
				break;
			}
#ifdef MP_LOGGING
			Msg("--- Object: GE_DESTROY of [%d][%s]", ID(), cNameSect().c_str());
#endif // MP_LOGGING

			setDestroy(TRUE);
			//			MakeMeCrow		();
		}
		break;
	}
}

void VisualCallback(IKinematics* tpKinematics);

BOOL CGameObject::net_Spawn(CSE_Abstract* DC)
{
	VERIFY(!m_spawned);
	m_spawned = true;
	m_spawn_time = Device.dwFrame;
	m_ai_obstacle = xr_new<ai_obstacle>(this);

	CSE_Abstract* E = (CSE_Abstract*)DC;
	VERIFY(E);

	const CSE_Visual* visual = smart_cast<const CSE_Visual*>(E);
	if (visual)
	{
		auto visualName = visual_name(E);

		// If model doesn't exist, and object config has "visual" field, try to update the model
		if(!Render->models_Exists(visualName) && pSettings->line_exist(cNameSect(), "visual")) {
			auto visualFromSec = pSettings->r_string(cNameSect(), "visual");
			Msg("~WARNING : Failed to load '%s', falling back to '%s'", visualName, visualFromSec);
			cNameVisual_set(visualFromSec);
		}
		// Otherwise... proceed as normal (will CTD if model doesn't exist)
		else {
			cNameVisual_set(visualName);
		}

		if (visual->flags.test(CSE_Visual::flObstacle))
		{
			SpatialComponent->spatial.type |= STYPE_OBSTACLE;
		}
	}

	// Naming
	cName_set(E->s_name);
	cNameSect_set(E->s_name);
	if (E->name_replace()[0])
		cName_set(E->name_replace());

	bool demo_spectator = false;

	//Alundaio:
	if (Level().IsDemoPlayStarted() && E->ID == u16(-1))
	{
		Msg("* Spawning demo spectator ...");
		demo_spectator = true;
	}
	else
	{
		//R_ASSERT(Level().Objects.net_Find(E->ID) == NULL);
		CObject* o = Level().Objects.net_Find(E->ID);
		if (o != NULL)
		{
			Msg("ERROR: CGameObject:net_spawn() Object with ID already exists! ID=%d self=%s other=%s", E->ID,
			    *(cName()), *(o->cName()));
			//ai().script_engine().script_log(eLuaMessageTypeError, "CGameObject:net_Spawn() | Level().Objects.net_Find(E->ID) != NULL (This mean object already exist on level by this ID) ID=%d s_name=%s", E->ID, *(E->s_name));
			return false;
		}
	}
	//-Alundaio


	setID(E->ID);
	//	if (GameID() != eGameIDSingle)
	//		Msg ("CGameObject::net_Spawn -- object %s[%x] setID [%d]", *(E->s_name), this, E->ID);

	// XForm
	XFORM().setXYZ(E->o_Angle);
	Position().set(E->o_Position);
#ifdef DEBUG
	if(ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject)&&stricmp(PH_DBG_ObjectTrackName(),*cName())==0)
	{
		Msg("CGameObject::net_Spawn obj %s Position set from CSE_Abstract %f,%f,%f",PH_DBG_ObjectTrackName(),Position().x,Position().y,Position().z);
	}
#endif
	VERIFY(_valid(renderable.xform));
	VERIFY(!fis_zero(DET(renderable.xform)));
	CSE_ALifeObject* O = smart_cast<CSE_ALifeObject*>(E);
	if (O && xr_strlen(O->m_ini_string))
	{
#pragma warning(push)
#pragma warning(disable:4238)
		m_ini_file = xr_new<CInifile>(
			&IReader(
				(void*)(*(O->m_ini_string)),
				O->m_ini_string.size()
			),
			FS.get_path("$game_config$")->m_Path
		);
#pragma warning(pop)
	}

	m_story_id = ALife::_STORY_ID(-1);
	if (O)
		m_story_id = O->m_story_id;

	// Net params
	setLocal(E->s_flags.is(M_SPAWN_OBJECT_LOCAL));
	if (Level().IsDemoPlay()) //&& OnClient())
	{
		if (!demo_spectator)
		{
			setLocal(FALSE);
		}
	};

	setReady(TRUE);
	if (!demo_spectator)
		g_pGameLevel->Objects.net_Register(this);

	m_server_flags.one();
	if (O)
	{
		m_server_flags = O->m_flags;
		if (O->m_flags.is(CSE_ALifeObject::flVisibleForAI))
			SpatialComponent->spatial.type |= STYPE_VISIBLEFORAI;
		else
			SpatialComponent->spatial.type = (SpatialComponent->spatial.type | STYPE_VISIBLEFORAI) ^ STYPE_VISIBLEFORAI;
	}

	reload(*cNameSect());
	if (!g_dedicated_server)
		CScriptBinder::reload(*cNameSect());

	reinit();
	if (!g_dedicated_server)
		CScriptBinder::reinit();
#ifdef DEBUG
	if(ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject)&&stricmp(PH_DBG_ObjectTrackName(),*cName())==0)
	{
		Msg("CGameObject::net_Spawn obj %s After Script Binder reinit %f,%f,%f",PH_DBG_ObjectTrackName(),Position().x,Position().y,Position().z);
	}
#endif
	//load custom user data from server
	if (!E->client_data.empty())
	{
		//		Msg				("client data is present for object [%d][%s], load is processed",ID(),*cName());
		IReader ireader = IReader(&*E->client_data.begin(), E->client_data.size());
		net_Load(ireader);
	}
	else
	{
		//		Msg				("no client data for object [%d][%s], load is skipped",ID(),*cName());
	}

	// if we have a parent
	if (ai().get_level_graph())
	{
		if (E->ID_Parent == 0xffff)
		{
			CSE_ALifeObject* l_tpALifeObject = smart_cast<CSE_ALifeObject*>(E);
			if (l_tpALifeObject && ai().level_graph().valid_vertex_id(l_tpALifeObject->m_tNodeID))
				ai_location().level_vertex(l_tpALifeObject->m_tNodeID);
			else
			{
				CSE_Temporary* l_tpTemporary = smart_cast<CSE_Temporary*>(E);
				if (l_tpTemporary && ai().level_graph().valid_vertex_id(l_tpTemporary->m_tNodeID))
					ai_location().level_vertex(l_tpTemporary->m_tNodeID);
			}

			if (l_tpALifeObject && ai().game_graph().valid_vertex_id(l_tpALifeObject->m_tGraphID))
				ai_location().game_vertex(l_tpALifeObject->m_tGraphID);

			if (!_valid(Position()))
			{
				Fvector vertex_pos = ai().level_graph().vertex_position(ai_location().level_vertex_id());
				Msg("! [%s]: %s has invalid Position[%f,%f,%f] level_vertex_id[%u][%f,%f,%f]", __FUNCTION__, cName().c_str(), Position().x, Position().y, Position().z, ai_location().level_vertex_id(), vertex_pos.x, vertex_pos.y, vertex_pos.z);
				Position().set(vertex_pos);
				CSE_ALifeObject* const se_obj = smart_cast<CSE_ALifeObject*>(E);
				if (se_obj != nullptr)
					se_obj->o_Position.set(Position());
			}

			validate_ai_locations(false);

			// validating position
			if (
				UsedAI_Locations() &&
				ai().level_graph().inside(
					ai_location().level_vertex_id(),
					Position()
				) &&
				can_validate_position_on_spawn()
			)
				Position().y = EPS_L + ai().level_graph().vertex_plane_y(
					*ai_location().level_vertex(), Position().x, Position().z);
		}
		else
		{
			CSE_ALifeObject* const alife_object = smart_cast<CSE_ALifeObject*>(E);
			if (alife_object && ai().level_graph().valid_vertex_id(alife_object->m_tNodeID))
			{
				ai_location().level_vertex(alife_object->m_tNodeID);
				ai_location().game_vertex(alife_object->m_tGraphID);
			}
		}
	}
	inherited::net_Spawn(DC);

	m_bObjectRemoved = false;

	spawn_supplies();
#ifdef DEBUG
	if(ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject)&&stricmp(PH_DBG_ObjectTrackName(),*cName())==0)
	{
		Msg("CGameObject::net_Spawn obj %s Before CScriptBinder::net_Spawn %f,%f,%f",PH_DBG_ObjectTrackName(),Position().x,Position().y,Position().z);
	}
	BOOL ret =CScriptBinder::net_Spawn(DC);
#else
	::luabind::functor<void> funct;
	if (ai().script_engine().functor("_G.CGameObject_NetSpawn", funct))
	{
		funct(this->lua_game_object());
	}
	return (CScriptBinder::net_Spawn(DC));
#endif

#ifdef DEBUG
	if(ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject)&&stricmp(PH_DBG_ObjectTrackName(),*cName())==0)
	{
		Msg("CGameObject::net_Spawn obj %s Before CScriptBinder::net_Spawn %f,%f,%f",PH_DBG_ObjectTrackName(),Position().x,Position().y,Position().z);
	}
	return ret;
#endif
}

void CGameObject::net_Save(NET_Packet& net_packet)
{
	u32 position;
	net_packet.w_chunk_open16(position);
	save(net_packet);

	// Script Binder Save ---------------------------------------
#ifdef DEBUG
	if (psAI_Flags.test(aiSerialize))	{
		Msg(">> **** Save script object [%s] *****", *cName());
		Msg(">> Before save :: packet position = [%u]", net_packet.w_tell());
	}

#endif

	CScriptBinder::save(net_packet);

#ifdef DEBUG

	if (psAI_Flags.test(aiSerialize))	{
		Msg(">> After save :: packet position = [%u]", net_packet.w_tell());
	}
#endif

	// ----------------------------------------------------------

	net_packet.w_chunk_close16(position);
}

void CGameObject::net_Load(IReader& ireader)
{
	load(ireader);

	// Script Binder Load ---------------------------------------
#ifdef DEBUG
	if (psAI_Flags.test(aiSerialize))	{
		Msg(">> **** Load script object [%s] *****", *cName());
		Msg(">> Before load :: reader position = [%i]", ireader.tell());
	}

#endif

	CScriptBinder::load(ireader);


#ifdef DEBUG

	if (psAI_Flags.test(aiSerialize))	{
		Msg(">> After load :: reader position = [%i]", ireader.tell());
	}
#endif
	// ----------------------------------------------------------
#ifdef DEBUG
	if(ph_dbg_draw_mask1.test(ph_m1_DbgTrackObject)&&stricmp(PH_DBG_ObjectTrackName(),*cName())==0)
	{
		Msg("CGameObject::net_Load obj %s (loaded) %f,%f,%f",PH_DBG_ObjectTrackName(),Position().x,Position().y,Position().z);
	}

#endif
}

void CGameObject::save(NET_Packet& output_packet)
{
}

void CGameObject::load(IReader& input_packet)
{
}

void CGameObject::spawn_supplies()
{
	if (!spawn_ini() || ai().get_alife())
		return;

	if (!spawn_ini()->section_exist("spawn"))
		return;

	LPCSTR N, V;
	float p;
	bool bScope = false;
	bool bSilencer = false;
	bool bLauncher = false;

	for (u32 k = 0, j; spawn_ini()->r_line("spawn", k, &N, &V); k++)
	{
		VERIFY(xr_strlen(N));
		if (pSettings->section_exist(N)) //Alundaio: Validate section exists
		{
			j = 1;
			p = 1.f;

			float f_cond = 1.0f;
			if (V && xr_strlen(V))
			{
				int n = _GetItemCount(V);
				string16 temp;
				if (n > 0)
					j = atoi(_GetItem(V, 0, temp)); //count

				if (NULL != strstr(V, "prob="))
					p = (float)atof(strstr(V, "prob=") + 5);
				if (fis_zero(p))p = 1.f;
				if (!j) j = 1;
				if (NULL != strstr(V, "cond="))
					f_cond = (float)atof(strstr(V, "cond=") + 5);
				bScope = (NULL != strstr(V, "scope"));
				bSilencer = (NULL != strstr(V, "silencer"));
				bLauncher = (NULL != strstr(V, "launcher"));
			}
			for (u32 i = 0; i < j; ++i)
			{
				if (::Random.randF(1.f) < p)
				{
					CSE_Abstract* A = Level().spawn_item(N, Position(), ai_location().level_vertex_id(), ID(), true);

					CSE_ALifeInventoryItem* pSE_InventoryItem = smart_cast<CSE_ALifeInventoryItem*>(A);
					if (pSE_InventoryItem)
						pSE_InventoryItem->m_fCondition = f_cond;

					CSE_ALifeItemWeapon* W = smart_cast<CSE_ALifeItemWeapon*>(A);
					if (W)
					{
						if (W->m_scope_status == ALife::eAddonAttachable)
							W->m_addon_flags.set(CSE_ALifeItemWeapon::eWeaponAddonScope, bScope);
						if (W->m_silencer_status == ALife::eAddonAttachable)
							W->m_addon_flags.set(CSE_ALifeItemWeapon::eWeaponAddonSilencer, bSilencer);
						if (W->m_grenade_launcher_status == ALife::eAddonAttachable)
							W->m_addon_flags.set(CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher, bLauncher);
					}

					NET_Packet P;
					A->Spawn_Write(P, TRUE);
					Level().Send(P, net_flags(TRUE));
					F_entity_Destroy(A);
				}
			}
		}
	}
}

void CGameObject::setup_parent_ai_locations(bool assign_position)
{
	//	CGameObject				*l_tpGameObject	= static_cast<CGameObject*>(H_Root());
	VERIFY(H_Parent());
	CGameObject* l_tpGameObject = H_Parent()->cast_game_object();
	VERIFY(l_tpGameObject);

	// get parent's position
	if (assign_position && use_parent_ai_locations())
		Position().set(l_tpGameObject->Position());

	//if ( assign_position && 
	//		( use_parent_ai_locations() &&
	//		!( cast_attachable_item() && cast_attachable_item()->enabled() )
	//		 ) 
	//	)
	//	Position().set		(l_tpGameObject->Position());

	// setup its ai locations
	if (!UsedAI_Locations())
		return;

	if (!ai().get_level_graph())
		return;

	if (l_tpGameObject->UsedAI_Locations() && ai().level_graph().valid_vertex_id(
		l_tpGameObject->ai_location().level_vertex_id()))
		ai_location().level_vertex(l_tpGameObject->ai_location().level_vertex_id());
	else
		validate_ai_locations(false);
	//	VERIFY2						(l_tpGameObject->UsedAI_Locations(),*l_tpGameObject->cNameSect());
	//	VERIFY2						(ai().level_graph().valid_vertex_id(l_tpGameObject->ai_location().level_vertex_id()),*cNameSect());
	//	ai_location().level_vertex	(l_tpGameObject->ai_location().level_vertex_id());

	if (ai().game_graph().valid_vertex_id(l_tpGameObject->ai_location().game_vertex_id()))
		ai_location().game_vertex(l_tpGameObject->ai_location().game_vertex_id());
	else
		ai_location().game_vertex(ai().cross_table().vertex(ai_location().level_vertex_id()).game_vertex_id());
	//	VERIFY2						(ai().game_graph().valid_vertex_id(l_tpGameObject->ai_location().game_vertex_id()),*cNameSect());
	//	ai_location().game_vertex	(l_tpGameObject->ai_location().game_vertex_id());
}

u32 CGameObject::new_level_vertex_id() const
{
	Fvector center;
	Center(center);
	center.x = Position().x;
	center.z = Position().z;
	return (ai().level_graph().vertex(ai_location().level_vertex_id(), center));
}

void CGameObject::update_ai_locations(bool decrement_reference)
{
	u32 l_dwNewLevelVertexID = new_level_vertex_id();
	VERIFY(ai().level_graph().valid_vertex_id(l_dwNewLevelVertexID));
	if (decrement_reference && (ai_location().level_vertex_id() == l_dwNewLevelVertexID))
		return;

	ai_location().level_vertex(l_dwNewLevelVertexID);

	if (!ai().get_game_graph() && ai().get_cross_table())
		return;

	ai_location().game_vertex(ai().cross_table().vertex(ai_location().level_vertex_id()).game_vertex_id());
	VERIFY(ai().game_graph().valid_vertex_id(ai_location().game_vertex_id()));
}

void CGameObject::validate_ai_locations(bool decrement_reference)
{
	if (!ai().get_level_graph())
		return;

	if (!UsedAI_Locations())
		return;

	update_ai_locations(decrement_reference);
}

void CGameObject::spatial_move()
{
	if (H_Parent() && !getVisible() && !getEnabled())
		setup_parent_ai_locations();
	else if (Visual())
		validate_ai_locations();

	inherited::spatial_move();
}

#ifdef DEBUG
void			CGameObject::dbg_DrawSkeleton	()
{
	CCF_Skeleton* Skeleton = smart_cast<CCF_Skeleton*>(collidable.model);
	if (!Skeleton) return;
	Skeleton->_dbg_refresh();

	const CCF_Skeleton::ElementVec& Elements = Skeleton->_GetElements();
	for (CCF_Skeleton::ElementVec::const_iterator I=Elements.begin(); I!=Elements.end(); I++){
		if (!I->valid())		continue;
		switch (I->type){
			case SBoneShape::stBox:{
				Fmatrix M;
				M.invert			(I->b_IM);
				Fvector h_size		= I->b_hsize;
				Level().debug_renderer().draw_obb	(M, h_size, color_rgba(0, 255, 0, 255));
								   }break;
			case SBoneShape::stCylinder:{
				Fmatrix M;
				M.c.set				(I->c_cylinder.m_center);
				M.k.set				(I->c_cylinder.m_direction);
				Fvector				h_size;
				h_size.set			(I->c_cylinder.m_radius,I->c_cylinder.m_radius,I->c_cylinder.m_height*0.5f);
				Fvector::generate_orthonormal_basis(M.k,M.j,M.i);
				Level().debug_renderer().draw_obb	(M, h_size, color_rgba(0, 127, 255, 255));
										}break;
			case SBoneShape::stSphere:{
				Fmatrix				l_ball;
				l_ball.scale		(I->s_sphere.R, I->s_sphere.R, I->s_sphere.R);
				l_ball.translate_add(I->s_sphere.P);
				Level().debug_renderer().draw_ellipse(l_ball, color_rgba(0, 255, 0, 255));
									  }break;
		};
	};	
}
#endif

void CGameObject::renderable_Render(IDSGraphManager* DM)
{
	inherited::renderable_Render(DM);
	if (m_instance_holder && !m_instance_holder->ModelInstanceMultirender()) return;
	DM->add_Dynamic(Visual(), sim_render_XFORM(DM)); // S2 stage 4b: &XFORM() on every stock frame
	Visual()->getVisData().hom_frame = Device.dwFrame;
	RenderAttachments(DM);
}

void CGameObject::RenderAttachments(IDSGraphManager* DM)
{
    for (auto& pair : m_script_attachments)
    {
        script_attachment* att = pair.second;
        if (att->GetType() != eSA_World) continue;
        att->Render(Visual() ? Visual()->dcast_PKinematics() : nullptr, *sim_render_XFORM(DM), DM); // S2 stage 4b
    }
}

// ---------------------------------------------------------------------------------------------------------------
// S2 stage 4b, slice 1: objects drawn between their last two ticks (sim_tick gated mode only; see g_sim_render_lerp in
// device.h). Only reached through sim_render_XFORM while g_sim_render_lerp is TRUE. XFORM() is never written: the
// renderer is handed a pointer to sim_render.draw, which stays unchanged until the next frame (computed once per
// Device.dwFrame, on the main thread, from the first renderable_Render of the frame; the later passes of the frame
// reuse it).

// out = between a and b at t (position lerp, rotation slerp, axis scale lerp); false = not a plain rotation+scale
// (mirrored or degenerate): the caller draws XFORM()
static bool sim_render_interpolate(Fmatrix& out, const Fmatrix& a, const Fmatrix& b, float t)
{
	const Fvector sa = {a.i.magnitude(), a.j.magnitude(), a.k.magnitude()};
	const Fvector sb = {b.i.magnitude(), b.j.magnitude(), b.k.magnitude()};
	if (sa.x < EPS_S || sa.y < EPS_S || sa.z < EPS_S || sb.x < EPS_S || sb.y < EPS_S || sb.z < EPS_S)
		return false;
	Fmatrix ra, rb; // a and b without their axis scale: pure rotations
	ra.identity();
	ra.i.div(a.i, sa.x);
	ra.j.div(a.j, sa.y);
	ra.k.div(a.k, sa.z);
	rb.identity();
	rb.i.div(b.i, sb.x);
	rb.j.div(b.j, sb.y);
	rb.k.div(b.k, sb.z);
	Fvector n;
	if (n.crossproduct(ra.i, ra.j).dotproduct(ra.k) <= 0.f || n.crossproduct(rb.i, rb.j).dotproduct(rb.k) <= 0.f)
		return false; // mirrored: no quaternion for it
	Fquaternion qa, qb, q;
	qa.set(ra);
	qb.set(rb);
	q.slerp(qa, qb, t);
	q.normalize();
	Fvector p;
	p.lerp(a.c, b.c, t);
	out.mk_xform(q, p);
	Fvector s;
	s.lerp(sa, sb, t);
	out.i.mul(s.x);
	out.j.mul(s.y);
	out.k.mul(s.z);
	return true;
}

// ---- S2 stage 4d-2 S4b: items on a bone of a skeleton drawn between ticks (design note
// plan-facts/stage4d2-s4b-design-and-code-2026-11-27.md). Matrices: mul_43(A, B) = A * B applies B first (row vectors,
// v * M); X = P * F * O for a held item (P parent XFORM, F the model-space placement frame, O the item's offset).

// the model-space two-hand frame of CWeapon::UpdateXForm (unit_r false) and CMissile/CArtefact::UpdateXForm (unit_r
// true) from the right hand mR and the left hand mL (bone -> model); false = degenerate here (hands closer than EPS_L,
// or the right hand's j axis within EPS_L of the line between them). The sites' own thresholds are smaller (CWeapon:
// fis_zero, the others none), so between the two the caller falls back to the right hand alone.
static bool sim_attach_hands(Fmatrix& F, const Fmatrix& mR, const Fmatrix& mL, bool unit_r)
{
	Fvector R, D, N;
	D.sub(mL.c, mR.c);
	if (D.magnitude() < EPS_L)
		return false;
	D.normalize();
	R.crossproduct(mR.j, D);
	if (R.magnitude() < EPS_L)
		return false;
	if (unit_r)
		R.normalize();
	N.crossproduct(D, R);
	N.normalize();
	F.set(R, N, D, mR.c);
	return true;
}

// the record's writer. Any thread that places the item (the tick, the render, CalculateBones' callback on a worker).
// Unlocked pre-test (review P1): an unmarked skeleton (the local actor's body, bare model_Create visuals) is never drawn
// between ticks (sim_pose_draw returns nullptr for it), so its record could never be used: skip it without the lock.
// Then takes K's lock (recursive: AttachmentCallback already holds it; the caller passes the K it holds, review L1).
// Writes only m_sim_attach.
void CGameObject::sim_attach_note(u8 kind, CObject* parent, IKinematics* K, u16 bone_a, const Fmatrix& ta, u16 bone_b,
	const Fmatrix* tb)
{
	if (!parent || !K || !K->LL_GetSimObjectVisual())
		return;
	xrCriticalSectionGuard guard(&K->UCalc_Mutex);
	SimAttachRecord& r = m_sim_attach;
	r.xform.set(XFORM());
	r.parent.set(parent->XFORM());
	r.ta.set(ta);
	if (tb)
		r.tb.set(*tb);
	r.K = K;
	r.parent_id = parent->ID();
	r.bone_a = bone_a;
	r.bone_b = tb ? bone_b : u16(-1);
	r.kind = kind;
}

// see GameObject.h. Main thread (the render). Reads the record, XFORM(), the parent's ID and visual, and sim_pose's
// drawn bones; writes only its outputs. The derivation and the cases: design note, section "after review".
bool CGameObject::sim_attach_correction(CObject* P, Fmatrix& C, Fmatrix& L, Fmatrix& A_d, Fmatrix& A_c,
	SimAttachInfo& info)
{
	const SimAttachRecord& r = m_sim_attach;
	if (r.kind == SIM_ATTACH_NONE)
		return false;
	static const char kinds[] = {'-', 'B', 'T', 'W', 'N'};
	IRenderVisual* V = P->Visual();
	IKinematics* K = V ? V->dcast_PKinematics() : nullptr;
	// unlocked pre-test (review P1): false means sim_pose_bone_drawn below would return 0 (no correction), so it only
	// skips the lock; the witness fields are filled from an unlocked read (test-only, informational)
	if (!K || !sim_pose_maybe_drawn(K))
	{
		info.kind = r.kind < sizeof(kinds) ? kinds[r.kind] : '?';
		info.ba = r.bone_a;
		info.bb = r.bone_b;
		return false;
	}
	xrCriticalSectionGuard guard(&K->UCalc_Mutex); // the record's writers hold it
	info.kind = r.kind < sizeof(kinds) ? kinds[r.kind] : '?';
	info.ba = r.bone_a;
	info.bb = r.bone_b;
	if (r.K != K || r.parent_id != P->ID() || 0 != memcmp(&r.xform, &XFORM(), sizeof(Fmatrix)))
	{
		info.kind = 'X'; // XFORM() was placed some other way since, or for another parent or skeleton: the 4b rule
		return false;
	}
	Fmatrix da;
	float u = 1.f;
	const int ca = sim_pose_bone_drawn(K, r.bone_a, r.ta, da, u);
	if (!ca)
		return false; // the parent's pose is drawn live in this frame, or bone_a is hidden: nothing to correct
	info.have_u = true;
	info.u = u;
	info.hr.set(r.ta.c);
	if (ca == SIM_POSE_BONE_OTHER)
	{
		info.cls = 'o';
		info.form = 'O'; // the placement read neither capture the mesh is blended from: the 4b rule (see the note)
		info.hd.set(r.ta.c); // no drawn bone for it
		return false;
	}
	info.hd.set(da.c);
	int cls = ca;
	bool framed = false;
	if (r.kind == SIM_ATTACH_HANDS || r.kind == SIM_ATTACH_HANDS_N)
	{
		Fmatrix db;
		float ub = 1.f;
		const int cb = sim_pose_bone_drawn(K, r.bone_b, r.tb, db, ub);
		if (cb == SIM_POSE_BONE_OTHER || (cb && !(cb & ca)))
		{
			// the two hands read from different captures (a recalculation between the two reads), or the left from
			// neither: no pose the mesh shows
			info.cls = 'o';
			info.form = 'O';
			return false;
		}
		if (cb)
			cls &= cb;
		const bool unit_r = r.kind == SIM_ATTACH_HANDS_N;
		framed = cb && sim_attach_hands(A_d, da, db, unit_r) && sim_attach_hands(A_c, r.ta, r.tb, unit_r);
		// not framed (left hand hidden, or degenerate here): the right hand alone carries the item (below, form R)
	}
	info.cls = (cls & SIM_POSE_BONE_CUR) ? 'c' : 'p';
	if (!framed)
	{
		if (r.kind == SIM_ATTACH_POS)
		{
			A_d.translate(da.c);
			A_c.translate(r.ta.c);
		}
		else
		{
			A_d.set(da);
			A_c.set(r.ta);
		}
	}
	if (0 == memcmp(&A_d, &A_c, sizeof(Fmatrix)))
		return false; // the bones are drawn as the placement read them: the 4b rule alone (bitwise S4a's result)
	info.form = (r.kind == SIM_ATTACH_HANDS || r.kind == SIM_ATTACH_HANDS_N) && !framed ? 'R' : 'C';
	Fmatrix inv;
	inv.invert(A_c);
	C.mul_43(A_d, inv);
	inv.invert(r.parent); // the placement's own parent matrix (review C3): L = F(read) * O exactly, whatever P did since
	L.mul_43(inv, XFORM());
	return true;
}

// test-only (-det_synth_pose): is this child the witness NPC's active item?
static bool sim_attach_wit_child(CGameObject* PG, CGameObject* child)
{
	const shared_str& name = sim_pose_wit_npc();
	if (!name.size() || PG->cName() != name)
		return false;
	CInventoryOwner* io = PG->cast_inventory_owner();
	PIItem a = io ? io->inventory().ActiveItem() : nullptr;
	return a && static_cast<CGameObject*>(&a->object()) == child;
}

// test-only: the "pa" witness of this child's placement (sim_pose.h, SimPoseAttachWit)
static void sim_attach_wit_fill(CGameObject* child, const CGameObject::SimAttachInfo& info, bool corrected, bool stock,
	const Fmatrix& parent_draw, const Fmatrix& drawn, const Fmatrix& plain, const Fmatrix& L, const Fmatrix& A_d,
	const Fmatrix& A_c, u64 hx)
{
	SimPoseAttachWit w;
	w.kind = info.kind;
	w.form = stock ? 'S' : (corrected ? info.form : (info.form == 'O' ? 'O' : 'P'));
	w.cls = info.cls;
	w.ba = info.ba;
	w.bb = info.bb;
	w.have_u = info.have_u;
	w.u = info.u;
	w.wd.set(drawn.c);
	w.ws.set(plain.c);
	if (info.have_u)
	{
		parent_draw.transform_tiny(w.hd, info.hd);
		parent_draw.transform_tiny(w.hr, info.hr);
	}
	else
	{
		w.hd.set(0.f, 0.f, 0.f);
		w.hr.set(0.f, 0.f, 0.f);
	}
	w.have_l = corrected;
	w.ld.set(0.f, 0.f, 0.f);
	w.ls.set(0.f, 0.f, 0.f);
	if (corrected)
	{
		// the child's origin in its placement frame, each by its own route: the drawn position wd in
		// parent_draw * A_d, and the recorded placement (parent_draw * L) in parent_draw * A_c. Equal to rounding when
		// the item is the same placement redone from the drawn bones.
		Fmatrix fd, fc, ifd, ifc, placed;
		fd.mul_43(parent_draw, A_d);
		fc.mul_43(parent_draw, A_c);
		placed.mul_43(parent_draw, L);
		ifd.invert(fd);
		ifc.invert(fc);
		ifd.transform_tiny(w.ld, w.wd);
		ifc.transform_tiny(w.ls, placed.c);
	}
	w.hx = hx;
	w.child_id = child->ID();
	sim_pose_wit_attach(w);
}

// the renderer's matrix for this object in this frame: &XFORM() unless every condition in sim_render_world_calc holds.
// S2 stage 4d-1 tripwire (test-only; g_sim_synth_tw is FALSE unless -det_mode -det_synth_tw): for the local actor and
// its direct children, a checksum of the actor-side simulation fields S2 could reach (CActor::sim_4d_tw_hash) is taken
// before and after; any difference is reported. Without the switch this is one not-taken branch.
Fmatrix* CGameObject::sim_render_world()
{
	if (!g_sim_synth_tw || !g_actor || (cast_actor() != g_actor && H_Parent() != g_actor))
		return sim_render_world_calc();
	const u64 before = g_actor->sim_4d_tw_hash();
	Fmatrix* m = sim_render_world_calc();
	if (g_actor->sim_4d_tw_hash() != before)
		sim_4d_tw_bad("s2");
	return m;
}

Fmatrix* CGameObject::sim_render_world_calc()
{
	CObject::SimRenderState& s = sim_render;
	if (s.ptr && s.frame == Device.dwFrame)
		return s.ptr; // computed in this frame already: every pass of the frame draws the same matrix
	s.frame = Device.dwFrame;
	s.ptr = &XFORM();

	// slice 1: the local player's vehicle or mounted gun stays stock (the view rides on it at tick rate: 4a has no
	// presentation step there), and so does the local player in one. S2 stage 4d-1: the local player on foot (body,
	// shadow, and through the parent rule below everything it holds) is drawn between ticks as every other object,
	// unless the mask's body bit is off (then stock, as in 4b). This line is reached only while g_sim_render_lerp.
	if (g_actor && ((cast_actor() == g_actor && (g_actor->Holder() || !(g_sim_4d_mask & SIM4D_BODY)))
		|| (g_actor->Holder() && cast_holder_custom() == g_actor->Holder())))
		return s.ptr;

	if (CObject* P = H_Parent())
	{
		// a child (a weapon in an NPC's hands, an attached torch): its XFORM() was computed during the tick (or by
		// UpdateXForm just now) from the parent's XFORM() and bones; it follows the parent's drawn matrix:
		// draw = parent_draw * inverse(parent XFORM()) * XFORM(). A parent drawn at its XFORM() means the child is too,
		// unless (S2 stage 4d-2 S4b) the parent's pose is drawn between ticks: then the bones the child hangs on are
		// drawn elsewhere than the placement read them, and the child is the same placement redone from the drawn
		// bones: draw = parent_draw * C * inverse(recorded parent XFORM()) * XFORM() (sim_attach_correction; design
		// note S4b, section "after review").
		CGameObject* PG = P->cast_game_object();
		Fmatrix* parent_draw = PG ? PG->sim_render_world() : &P->XFORM();
		// S4b. With sim_tick 0 or bit 16 off, g_sim_pose_active is FALSE and every S4b line below is a not-taken test
		// (of it, or of the test-only switches, which are 0 without -det_mode): the 4b rule runs as before.
		const bool wit = g_sim_synth_pose && PG && sim_attach_wit_child(PG, this); // test-only (-det_synth_pose)
		const u64 wit_hx = wit ? sim_4d_hash(sim_4d_hash_seed, &XFORM(), sizeof(Fmatrix)) : 0;
		// test-only tripwire (-det_synth_tw). Like build 1's tripwires it is not limited to serialized workers: the
		// harness runs it on the AGG profiler build, where -det_mode runs the worker tasks inline (device.cpp
		// DET_SERIAL); in other builds a lock-free writer on another thread can trip it falsely (review T1)
		const bool tw = g_sim_synth_tw && g_sim_pose_active;
		IKinematics* PK = (tw && P->Visual()) ? P->Visual()->dcast_PKinematics() : nullptr;
		if (PK && !sim_pose_maybe_drawn(PK))
			PK = nullptr; // nothing below can correct this child (review P1): no bracket, no lock
		if (PK)
		{
			// test-only: evaluate the parent's draw pose before the bracket, so S4a's own break (16) and tripwire stay
			// outside it, then hold the parent's lock over the bracket (no bone calculation can run inside it)
			PK->UCalc_Mutex.Enter();
			sim_pose_draw_frame(PK);
		}
		const u64 tw_before = PK ? sim_4d_hash(sim_pose_tw_bones(PK), &XFORM(), sizeof(Fmatrix)) : 0;
		Fmatrix C, L, A_d, A_c;
		SimAttachInfo info;
		const bool corrected = g_sim_pose_active && sim_attach_correction(P, C, L, A_d, A_c, info);
		const bool stock = parent_draw == &P->XFORM() && !corrected;
		Fmatrix local, plain; // plain: the 4b rule alone (also the witness's stock placement)
		if (!stock)
		{
			const Fmatrix parent_xform = P->XFORM();
			Fmatrix parent_inv;
			parent_inv.invert(parent_xform); // 4x3 invert: handles scale
			local.mul_43(parent_inv, XFORM());
			if (corrected)
			{
				Fmatrix moved; // the placement redone from the drawn bones, in model space
				moved.mul_43(C, L);
				s.draw.mul_43(*parent_draw, moved);
			}
			else
				s.draw.mul_43(*parent_draw, local);
			if (wit)
				plain.mul_43(*parent_draw, local);
			// S4b must-fail break (test-only; g_sim_4d_break is 0 unless -det_mode): the corrected draw matrix is
			// written into the item's XFORM() at render, a Rule One breach the bracket below must catch
			if (corrected && (g_sim_4d_break & SIM4D_BREAK_ATTACH))
				XFORM().set(s.draw);
		}
		if (PK)
		{
			if (sim_4d_hash(sim_pose_tw_bones(PK), &XFORM(), sizeof(Fmatrix)) != tw_before)
				sim_4d_tw_bad("attach");
			PK->UCalc_Mutex.Leave();
		}
		if (wit)
			sim_attach_wit_fill(this, info, corrected, stock, *parent_draw, stock ? XFORM() : s.draw,
				stock ? XFORM() : plain, L, A_d, A_c, wit_hx);
		if (stock)
			return s.ptr;
	}
	else
	{
		// only an object captured in the newest tick, with a history (prev == cur means: no history, or not moving)
		if (!s.have || s.epoch != g_sim_view_epoch || s.tick != g_sim_view_tick)
			return s.ptr;
		if (0 == memcmp(&s.prev, &s.cur, sizeof(Fmatrix)))
			return s.ptr;
		if (!sim_render_interpolate(s.draw, s.prev, s.cur, g_sim_render_alpha))
			return s.ptr;
		// S2 stage 4d-1 must-fail break (test-only; g_sim_4d_break is 0 unless -det_mode): the actor's draw matrix is
		// written into its XFORM at render, a deliberate Rule One breach the tripwire above must catch
		if ((g_sim_4d_break & SIM4D_BODY) && g_actor && cast_actor() == g_actor)
			XFORM().set(s.draw);
	}
	s.ptr = &s.draw;
	return s.ptr;
}

// the hand-over from renderable_Render: the HUD pass (first-person hands, stage 4a's) is never changed
Fmatrix* CGameObject::sim_render_lerp(IDSGraphManager* DM)
{
	if (DM->get_HUD() || DM->get_UI3D())
		return &XFORM();
	Fmatrix* m = sim_render_world();
	if (m == &sim_render.draw && sim_render.frame_counted != Device.dwFrame)
	{
		sim_render.frame_counted = Device.dwFrame;
		PROF_EVENT("sim_render_lerp"); // once per (object, frame) drawn at an interpolated matrix
	}
	return m;
}
// ---------------------------------------------------------------------------------------------------------------

static void update_visbox(IKinematics* k)
{
	CGameObject* obj = static_cast<CGameObject*>(k->GetUpdateCallbackParam());
	if (!obj) return;

	if (k->NeedUCalc())
	{
		for (auto& pair : *obj->GetAttachments())
		{
			script_attachment* att = pair.second;
			if (att->GetType() != eSA_World) continue;

			Fbox Box = att->Box();

			Fmatrix offset;
			offset.mul(att->BoneTransform(k), att->GetOffset());
			Box.xform(offset);

			// Update Box
			Fbox& kbox = const_cast<Fbox&>(k->GetBox());
			kbox.merge(Box);

			// Update Sphere
			Fsphere& kshere = k->dcast_RenderVisual()->getVisData().sphere;
			kbox.getsphere(kshere.P, kshere.R);
		}
	}

	if (psDeviceFlags2.test(rsDBGDrawVisBoxes))
	{
	Fmatrix box, cent;
	cent.translate(k->dcast_RenderVisual()->getVisData().sphere.P);
	box.mul(obj->XFORM(), cent);

	const Fbox& kbox = k->GetBox();
	Fvector radius;
	kbox.getradius(radius);
	CDebugRenderer& render = Level().debug_renderer();
	render.draw_obb(box, radius, D3DCOLOR_XRGB(0, 255, 0), false);
	}
}

static void update_visbox_hud(IKinematics* k)
{
	CHudItem* obj = static_cast<CHudItem*>(k->GetUpdateCallbackParam());
	if (!obj) return;

	if (k->NeedUCalc())
	{
		for (auto& pair : *obj->object().GetAttachments())
		{
			script_attachment* att = pair.second;
			if (att->GetType() != eSA_HUD) continue;

			Fbox Box = att->Box();

			Fmatrix offset;
			offset.mul(att->BoneTransform(k), att->GetOffset());
			Box.xform(offset);

			// Update Box
			Fbox& kbox = const_cast<Fbox&>(k->GetBox());
			kbox.merge(Box);

			// Update Sphere
			Fsphere& kshere = k->dcast_RenderVisual()->getVisData().sphere;
			kbox.getsphere(kshere.P, kshere.R);
		}
	}
	
	if (psDeviceFlags2.test(rsDBGDrawVisBoxes))
	{
	Fmatrix box, cent;
	cent.translate(k->dcast_RenderVisual()->getVisData().sphere.P);
	box.mul(obj->HudItemData()->m_item_transform, cent);

	const Fbox& kbox = k->GetBox();
	Fvector radius;
	kbox.getradius(radius);
	CDebugRenderer& render = Level().debug_renderer();
	render.draw_obb(box, radius, D3DCOLOR_XRGB(100, 100, 0), true);
	}
}

void CGameObject::RefreshAttachmentVisBox(u16 type)
{
	IKinematics* kinematics = nullptr;

	if (type == eSA_World)
	{
		if (Visual())
			kinematics = Visual()->dcast_PKinematics();
	}
	else if (type == eSA_HUD)
	{
		CHudItem* hud = smart_cast<CHudItem*>(this);
		attachable_hud_item* hud_data = hud ? hud->HudItemData() : nullptr;
		if (hud_data)
			kinematics = hud_data->m_model;
	}

	if (!kinematics)
		return;

	kinematics->CalculateBones_Invalidate();
	kinematics->CalculateBones(TRUE);
}

script_attachment* CGameObject::add_attachment(LPCSTR name, script_attachment* att)
{
	R_ASSERT(att);
	remove_child(name, true);

	if (!m_script_attachments.size())
	{
		add_visual_callback(update_visbox);

		CHudItem* hud = smart_cast<CHudItem*>(this);
		if (hud) hud->add_visual_callback(update_visbox_hud);
	}

	m_script_attachments.emplace(mk_pair(name, att));
	RefreshAttachmentVisBox(att->GetType());
	return att;
}

script_attachment* CGameObject::get_attachment(LPCSTR name)
{
	if (m_script_attachments.size())
	{
		auto& att = m_script_attachments.find(name);
		if (att != m_script_attachments.end())
			return att->second;
	}

	return nullptr;
}

void CGameObject::remove_child(LPCSTR name, bool destroy)
{
	script_attachment* attachment = get_attachment(name);
	if (!attachment)
		return;
	const u16 attachment_type = attachment->GetType();

	attachment->OnParentDestroyPhysicsShell();

	if (destroy)
		xr_delete(attachment);

	m_script_attachments.erase(name);

	if (!m_script_attachments.size())
	{
		remove_visual_callback(update_visbox);

		CHudItem* hud = smart_cast<CHudItem*>(this);
		if (hud) hud->remove_visual_callback(update_visbox_hud);
	}

	RefreshAttachmentVisBox(attachment_type);
}

void CGameObject::remove_attachment(script_attachment* child)
{
	if (!child) return;
	if (m_script_attachments.size())
	{
		script_attachment* attachment = get_attachment(child->GetName());
		if (!attachment || attachment != child)
			return;

		remove_child(child->GetName(), true);
	}
}

void CGameObject::iterate_attachments(::luabind::functor<bool> functor)
{
	if (!m_script_attachments.size())
		return;

	for (auto& pair : m_script_attachments)
		if (functor(pair.first.c_str(), pair.second) == true)
			return;
}

/*
float CGameObject::renderable_Ambient	()
{
	return (ai().get_level_graph() && ai().level_graph().valid_vertex_id(level_vertex_id()) ? float(level_vertex()->light()/15.f) : 1.f);
}
*/

CObject::SavedPosition CGameObject::ps_Element(u32 ID) const
{
	VERIFY(ID<ps_Size());
	inherited::SavedPosition SP = PositionStack[ID];
	SP.dwTime += Level().timeServer_Delta();
	return SP;
}

void CGameObject::u_EventGen(NET_Packet& P, u32 type, u32 dest)
{
	P.w_begin(M_EVENT);
	P.w_u32(Level().timeServer());
	P.w_u16(u16(type & 0xffff));
	P.w_u16(u16(dest & 0xffff));
}

void CGameObject::u_EventSend(NET_Packet& P, u32 dwFlags)
{
	Level().Send(P, dwFlags);
}

#include "bolt.h"

void CGameObject::OnH_B_Chield()
{
	inherited::OnH_B_Chield();
	///PHSetPushOut();????
}

void CGameObject::OnH_B_Independent(bool just_before_destroy)
{
	inherited::OnH_B_Independent(just_before_destroy);

	setup_parent_ai_locations(false);

	CGameObject* parent = smart_cast<CGameObject*>(H_Parent());
	VERIFY(parent);
	if (ai().get_level_graph() && ai().level_graph().valid_vertex_id(parent->ai_location().level_vertex_id()))
		validate_ai_locations(false);
}

BOOL CGameObject::UsedAI_Locations()
{
	return (m_server_flags.test(CSE_ALifeObject::flUsedAI_Locations));
}

BOOL CGameObject::TestServerFlag(u32 Flag) const
{
	return (m_server_flags.test(Flag));
}

void CGameObject::add_visual_callback(visual_callback* callback)
{
	if (!smart_cast<IKinematics*>(Visual())) return;
	CALLBACK_VECTOR_IT I = std::find(visual_callbacks().begin(), visual_callbacks().end(), callback);
	if (I != visual_callbacks().end()) return;
	if (m_visual_callback.empty()) SetKinematicsCallback(true);
	m_visual_callback.push_back(callback);
}

void CGameObject::remove_visual_callback(visual_callback* callback)
{
	if (!smart_cast<IKinematics*>(Visual())) return;
	CALLBACK_VECTOR_IT I = std::find(m_visual_callback.begin(), m_visual_callback.end(), callback);
	if (I == m_visual_callback.end()) return;
	m_visual_callback.erase(I);
	if (m_visual_callback.empty()) SetKinematicsCallback(false);
}

void CGameObject::SetKinematicsCallback(bool set)
{
	if (!Visual()) return;
	if (set)
		smart_cast<IKinematics*>(Visual())->Callback(VisualCallback, this);
	else
		smart_cast<IKinematics*>(Visual())->Callback(0, 0);
}

void VisualCallback(IKinematics* tpKinematics)
{
	if (!tpKinematics) return;

	auto cobj = static_cast<CObject*>(tpKinematics->GetUpdateCallbackParam());
	if (!cobj) return;

	CGameObject* game_object = static_cast<CGameObject*>(cobj);
	if (!game_object) return;

	VERIFY(game_object);

	CGameObject::CALLBACK_VECTOR_IT I = game_object->visual_callbacks().begin();
	CGameObject::CALLBACK_VECTOR_IT E = game_object->visual_callbacks().end();
	for (; I != E; ++I)
		(*I)(tpKinematics);
}

CScriptGameObject* CGameObject::lua_game_object() const
{
	if (!this) return NULL;
	if (!m_spawned)
	{
		Msg("! you are trying to use a destroyed object [%i]", ID());
		ai().script_engine().print_stack();
		return NULL;
	}
	if (!m_lua_game_object)
		m_lua_game_object = xr_new<CScriptGameObject>(const_cast<CGameObject*>(this));
	return (m_lua_game_object);
}

bool CGameObject::NeedToDestroyObject() const
{
	return false;
}

void CGameObject::DestroyObject()
{
	if (m_bObjectRemoved) return;
	m_bObjectRemoved = true;
	if (getDestroy()) return;

	if (Local())
	{
		NET_Packet P;
		u_EventGen(P, GE_DESTROY, ID());
		u_EventSend(P);
	}
}

void CGameObject::shedule_Update(u32 dt)
{
	PROF_EVENT();
	//уничтожить
	if (NeedToDestroyObject())
	{
#ifndef MASTER_GOLD
		Msg("--NeedToDestroyObject for [%d][%d]", ID(), Device.dwFrame);
#endif // #ifndef MASTER_GOLD
		DestroyObject();
	}

	// Msg							("-SUB-:[%x][%s] CGameObject::shedule_Update",smart_cast<void*>(this),*cName());
	inherited::shedule_Update(dt);

	if (!g_dedicated_server)
		CScriptBinder::shedule_Update(dt);
}

BOOL CGameObject::net_SaveRelevant()
{
	return (CScriptBinder::net_SaveRelevant());
}

//игровое имя объекта
LPCSTR CGameObject::Name() const
{
	return (*cName());
}

u32 CGameObject::ef_creature_type() const
{
	string16 temp;
	CLSID2TEXT(CLS_ID, temp);
	R_ASSERT3(false, "Invalid creature type request, virtual function is not properly overridden!", temp);
	return (u32(-1));
}

u32 CGameObject::ef_equipment_type() const
{
	string16 temp;
	CLSID2TEXT(CLS_ID, temp);
	R_ASSERT3(false, "Invalid equipment type request, virtual function is not properly overridden!", temp);
	return (u32(-1));
	//	return		(6);
}

u32 CGameObject::ef_main_weapon_type() const
{
	string16 temp;
	CLSID2TEXT(CLS_ID, temp);
	R_ASSERT3(false, "Invalid main weapon type request, virtual function is not properly overridden!", temp);
	return (u32(-1));
	//	return		(5);
}

u32 CGameObject::ef_anomaly_type() const
{
	string16 temp;
	CLSID2TEXT(CLS_ID, temp);
	R_ASSERT3(false, "Invalid anomaly type request, virtual function is not properly overridden!", temp);
	return (u32(-1));
}

u32 CGameObject::ef_weapon_type() const
{
	string16 temp;
	CLSID2TEXT(CLS_ID, temp);
	R_ASSERT3(false, "Invalid weapon type request, virtual function is not properly overridden!", temp);
	return (u32(-1));
	//	return		(u32(0));
}

u32 CGameObject::ef_detector_type() const
{
	string16 temp;
	CLSID2TEXT(CLS_ID, temp);
	R_ASSERT3(false, "Invalid detector type request, virtual function is not properly overridden!", temp);
	return (u32(-1));
}

void CGameObject::net_Relcase(CObject* O)
{
	inherited::net_Relcase(O);
	// demonized: Causes more allocations for lua gc. Pretty much all scripts never use this and should never.
	/*
	if (!g_dedicated_server)
		CScriptBinder::net_Relcase(O);
	*/
}

CGameObject::CScriptCallbackExVoid& CGameObject::callback(GameObject::ECallbackType type) const
{
	return ((*m_callbacks)[type]);
}

LPCSTR CGameObject::visual_name(CSE_Abstract* server_entity)
{
	const CSE_Visual* visual = smart_cast<const CSE_Visual*>(server_entity);
	VERIFY(visual);
	return (visual->get_visual());
}

bool CGameObject::animation_movement_controlled() const
{
	return !!animation_movement() && animation_movement()->IsActive();
}

void CGameObject::update_animation_movement_controller()
{
	if (!m_anim_mov_ctrl)
		return;

	if (m_anim_mov_ctrl->IsActive())
	{
		m_anim_mov_ctrl->OnFrame();
		return;
	}

	destroy_anim_mov_ctrl();
}

bool CGameObject::is_ai_obstacle() const
{
	return (false);
}

void CGameObject::OnChangeVisual()
{
	inherited::OnChangeVisual();
	// S2 stage 4d-2 S4a: this skeleton is the visual of a simulated object, so it may be drawn between ticks (only in
	// sim_tick gated mode with mask bit SIM4D_POSE; the flag is read nowhere else). Every visual change of a game object
	// comes here (CObject::cNameVisual_set); the new instance came from model_Create, whose Spawn cleared the flag.
	// First-person HUD models and other bare model_Create visuals (legs, VR items) never pass here: never marked.
	// Not a player's body (review 2026-11-27): the local player's root is deliberately drawn stock in a vehicle, on a
	// mounted gun or with SIM4D_BODY off (sim_render_world_calc), and a blended pose on a stock root would disagree.
	// Player bodies stay stock (S4b decision, plan-facts/stage4d2-s4b-design-and-code-2026-11-27.md section 7: revisit
	// with S4d, the first-person legs, which copy the body's live bones).
	if (!cast_actor())
		if (IKinematics* K = Visual() ? Visual()->dcast_PKinematics() : nullptr)
			K->LL_SetSimObjectVisual(true);
	if (m_anim_mov_ctrl)
		destroy_anim_mov_ctrl();
}

bool CGameObject::shedule_Needed()
{
	return (!getDestroy());
}

void CGameObject::create_anim_mov_ctrl(CBlend* b, Fmatrix* start_pose, bool local_animation)
{
	if (animation_movement_controlled())
	{
		m_anim_mov_ctrl->NewBlend(
			b,
			start_pose ? *start_pose : XFORM(),
			local_animation
		);
	}
	else
	{
		//		start_pose		= &renderable.xform;
		if (m_anim_mov_ctrl)
			destroy_anim_mov_ctrl();
#ifdef DEBUG
		VERIFY2(
			start_pose,
			make_string(
				"start pose hasn't been specified for animation [%s][%s]",
				smart_cast<IKinematicsAnimated&>(*Visual()).LL_MotionDefName_dbg(b->motionID).first,
				smart_cast<IKinematicsAnimated&>(*Visual()).LL_MotionDefName_dbg(b->motionID).second
			)
		);

		VERIFY2(
			!animation_movement(),
			make_string(
				"start pose hasn't been specified for animation [%s][%s]",
				smart_cast<IKinematicsAnimated&>(*Visual()).LL_MotionDefName_dbg(b->motionID).first,
				smart_cast<IKinematicsAnimated&>(*Visual()).LL_MotionDefName_dbg(b->motionID).second
			)
		);
#endif
		VERIFY(Visual());
		IKinematics* K = Visual()->dcast_PKinematics();
		VERIFY(K);

		m_anim_mov_ctrl = xr_new<animation_movement_controller>(&XFORM(), *start_pose, K, b);
	}
}

void CGameObject::destroy_anim_mov_ctrl()
{
	xr_delete(m_anim_mov_ctrl);
}

IC bool similar(const Fmatrix& _0, const Fmatrix& _1, const float& epsilon = EPS)
{
	if (!_0.i.similar(_1.i, epsilon))
		return (false);

	if (!_0.j.similar(_1.j, epsilon))
		return (false);

	if (!_0.k.similar(_1.k, epsilon))
		return (false);

	if (!_0.c.similar(_1.c, epsilon))
		return (false);

	// note: we do not compare projection here
	return (true);
}

void CGameObject::UpdateCL()
{
	inherited::UpdateCL();

    for (auto& pair : m_script_attachments)
	{
        script_attachment* att = pair.second;
        att->Update();
		if (att->GetType() == eSA_World && !m_instance_holder)
			att->UpdateXFORM(Visual() ? Visual()->dcast_PKinematics() : nullptr, &XFORM());
	}

	if (H_Parent())
		return;

	on_matrix_change(m_previous_matrix);
	m_previous_matrix = XFORM();
}

void CGameObject::on_matrix_change(const Fmatrix& previous)
{
	obstacle().on_move();
}

void CGameObject::FootStepCallback(float power, bool b_play, bool b_on_ground, bool b_hud_view)
{
	this->callback(GameObject::eOnFootStep)(this->lua_game_object(), power, b_play, b_on_ground, b_hud_view);
}

extern Flags32 psDeviceFlags2;

void CGameObject::DBGDrawPhysicsShell()
{
	CPhysicsShell* shell;
	CPhysicsShellHolder* holder = cast_physics_shell_holder();
	if (!holder) return;

	shell = holder->m_pPhysicsShell;
	if (!shell) return;

	if (psDeviceFlags2.test(rsDBGDrawPHShapes))
	{
		for (auto& elem : shell->Elements())
		{
			Fmatrix cent_mat;
			cent_mat.translate(elem->mass_Center());
			cent_mat.mulB_43(Fmatrix().scale(0.025, 0.025, 0.025));
			Level().debug_renderer().draw_ellipse(cent_mat, color_xrgb(213, 61, 165), false);

			for (int i = 0; i < elem->numberOfGeoms(); ++i)
			{
				CODEGeom* geom = elem->Geom(i);
				if (!geom) continue;

				CBoxGeom* box = smart_cast<CBoxGeom*>(geom);
				CSphereGeom* sphere = smart_cast<CSphereGeom*>(geom);
				CCylinderGeom* cylinder = smart_cast<CCylinderGeom*>(geom);

				if (box)
				{
					Fmatrix xform;
					Fvector sz;
					box->get_Box(xform, sz);
					sz.div(2);
					Level().debug_renderer().draw_obb(xform, sz, color_xrgb(255, 0, 0), false);
					continue;
				}

				if (sphere)
				{
					Fmatrix xform;
					sphere->get_xform(xform);
					float rad = sphere->radius();
					xform.mulB_43(Fmatrix().scale(rad,rad,rad));
					Level().debug_renderer().draw_ellipse(xform, color_xrgb(0, 0, 255), false);
					continue;
				}

				if (cylinder)
				{
					Fmatrix xform;
					cylinder->get_Box(xform, Fvector());
					Level().debug_renderer().draw_cylinder(xform, cylinder->height(), cylinder->radius(), color_xrgb(255, 0, 255), false);
					continue;
				}
			}
		}
	}

	if (psDeviceFlags2.test(rsDBGDrawPHBones))
	{
		for (const auto& bone : *Visual()->dcast_PKinematics()->LL_Bones())
		{
			Fmatrix bone_mat;
			bone_mat.mul_43(XFORM(), Visual()->dcast_PKinematics()->LL_GetTransform(bone.second));
			bone_mat.mulB_43(Fmatrix().scale(0.025, 0.025, 0.025));
			Level().debug_renderer().draw_ellipse(bone_mat, color_xrgb(0, 255, 0), false);
		}
	}
}

#ifdef DEBUG

void render_box						(IRenderVisual *visual, const Fmatrix &xform, const Fvector &additional, bool draw_child_boxes, const u32 &color)
{
	CDebugRenderer			&renderer = Level().debug_renderer();
	IKinematics				*kinematics = smart_cast<IKinematics*>(visual);
	VERIFY					(kinematics);
	u16						bone_count = kinematics->LL_BoneCount();
	VERIFY					(bone_count);
	u16						visible_bone_count = kinematics->LL_VisibleBoneCount();
	if (!visible_bone_count)
		return;

	Fmatrix					matrix;
	Fvector					*points = (Fvector*)_alloca(visible_bone_count*8*sizeof(Fvector));
	Fvector					*I = points;
	for (u16 i=0; i<bone_count; ++i) {
		if (!kinematics->LL_GetBoneVisible(i))
			continue;
		
		const Fobb			&obb = kinematics->LL_GetData(i).obb;
		if (fis_zero(obb.m_halfsize.square_magnitude())) {
			VERIFY			(visible_bone_count > 1);
			--visible_bone_count;
			continue;
		}

		Fmatrix				Mbox;
		obb.xform_get		(Mbox);

		const Fmatrix		&Mbone = kinematics->LL_GetBoneInstance(i).mTransform;
		Fmatrix				X;
		matrix.mul_43		(xform,X.mul_43(Mbone,Mbox));

		Fvector				half_size = Fvector().add(obb.m_halfsize,additional);
		matrix.mulB_43		(Fmatrix().scale(half_size));

		if (draw_child_boxes)
			renderer.draw_obb	(matrix,color);

		static const Fvector	local_points[8] = {
			Fvector().set(-1.f,-1.f,-1.f),
			Fvector().set(-1.f,-1.f,+1.f),
			Fvector().set(-1.f,+1.f,+1.f),
			Fvector().set(-1.f,+1.f,-1.f),
			Fvector().set(+1.f,+1.f,+1.f),
			Fvector().set(+1.f,+1.f,-1.f),
			Fvector().set(+1.f,-1.f,+1.f),
			Fvector().set(+1.f,-1.f,-1.f)
		};
		
		for (u32 i=0; i<8; ++i, ++I)
			matrix.transform_tiny	(*I,local_points[i]);
	}

	VERIFY						(visible_bone_count);
	if (visible_bone_count == 1) {
		renderer.draw_obb		(matrix,color);
		return;
	}

	VERIFY						((I - points) == (visible_bone_count*8));
	MagicBox3					box = MagicMinBox(visible_bone_count*8,points);
	box.ComputeVertices			(points);
	
	Fmatrix						result;
	result.identity				();

	result.c					= box.Center();

	result.i.sub(points[3],points[2]).normalize();
	result.j.sub(points[2],points[1]).normalize();
	result.k.sub(points[2],points[6]).normalize();

	Fvector						scale;
	scale.x						= points[3].distance_to(points[2])*.5f;
	scale.y						= points[2].distance_to(points[1])*.5f;
	scale.z						= points[2].distance_to(points[6])*.5f;
	result.mulB_43				(Fmatrix().scale(scale));

	renderer.draw_obb			(result,color);
}

void CGameObject::OnRender			()
{
	if (!ai().get_level_graph())
		return;

	CDebugRenderer					&renderer = Level().debug_renderer();
	if (/**bDebug && /**/Visual()) {
		float						half_cell_size = 1.f*ai().level_graph().header().cell_size()*.5f;
		Fvector						additional = Fvector().set(half_cell_size,half_cell_size,half_cell_size);

		render_box					(Visual(),XFORM(),Fvector().set(0.f,0.f,0.f),true,color_rgba(0,0,255,255));
		render_box					(Visual(),XFORM(),additional,false,color_rgba(0,255,0,255));
	}

	if (0) {
		Fvector						bc,bd; 
		Visual()->getVisData().box.get_CD	(bc,bd);
		Fmatrix						M = Fidentity;
		float						half_cell_size = ai().level_graph().header().cell_size()*.5f;
		bd.add						(Fvector().set(half_cell_size,half_cell_size,half_cell_size));
		M.scale						(bd);
		Fmatrix						T = XFORM();
		T.c.add						(bc);
		renderer.draw_obb			(T,bd,color_rgba(255,255,255,255));
	}
}
#endif // DEBUG
