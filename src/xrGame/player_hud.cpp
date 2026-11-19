#include "stdafx.h"
#include "player_hud.h"
#include "Level.h"
#include "HudItem.h"
#include "ui_base.h"
#include "actor.h"
#include "physic_item.h"
#include "static_cast_checked.hpp"
#include "actoreffector.h"
#include "../xrEngine/IGame_Persistent.h"
#include "inventory_item.h"
#include "weapon.h"
#include "script_attachment_manager.h"
#include "../xrEngine/CameraBase.h"
#include "../xrEngine/vr_hooks.h"
#include "../xrEngine/bone.h"
#include "Inventory.h"
#include "../xrEngine/Effector.h"
#include "../xrEngine/CameraManager.h"
#include <typeinfo>

extern int g_nearwall;

int ps_hud_ik_compensation = 0;

player_hud* g_player_hud = NULL;
Fvector _ancor_pos;
Fvector _wpn_root_pos;

player_hud_motion* player_hud_motion_container::find_motion(const shared_str& name)
{
	xr_vector<player_hud_motion>::iterator it = m_anims.begin();
	xr_vector<player_hud_motion>::iterator it_e = m_anims.end();
	for (; it != it_e; ++it)
	{
		const shared_str& s = (true) ? (*it).m_alias_name : (*it).m_base_name;
		if (s == name)
			return &(*it);
	}
	return NULL;
}

void player_hud_motion_container::load(IKinematicsAnimated* model, const shared_str& sect)
{
	CInifile::Sect& _sect = pSettings->r_section(sect);
	CInifile::SectCIt _b = _sect.Data.begin();
	CInifile::SectCIt _e = _sect.Data.end();
	player_hud_motion* pm = NULL;

	string512 buff;
	MotionID motion_ID;

	for (; _b != _e; ++_b)
	{
		if (strstr(_b->first.c_str(), "anm_") == _b->first.c_str())
		{
			const shared_str& anm = _b->second;
			m_anims.resize(m_anims.size() + 1);
			pm = &m_anims.back();
			//base and alias name
			pm->m_alias_name = _b->first;

			string512 str_item;
			int item_count = _GetItemCount(anm.c_str());

			switch (item_count)
			{
			case 1:
				pm->m_base_name = anm;
				pm->m_additional_name = anm;
				pm->m_anim_speed = 1.f;
				break;
			case 2:
				_GetItem(anm.c_str(), 0, str_item);
				pm->m_base_name = str_item;

				_GetItem(anm.c_str(), 1, str_item);
				pm->m_additional_name = str_item;

				pm->m_anim_speed = 1.f;
				break;
			case 3:
			default:
				_GetItem(anm.c_str(), 0, str_item);
				pm->m_base_name = str_item;

				_GetItem(anm.c_str(), 1, str_item);
				pm->m_additional_name = str_item;

				_GetItem(anm.c_str(), 2, str_item);
				pm->m_anim_speed = atof(str_item);
				if (item_count >= 4)
				{
					_GetItem(anm.c_str(), 3, str_item);
					pm->m_anim_end = atof(str_item);
				}
				break;
			}

			//and load all motions for it

			for (u32 i = 0; i <= 8; ++i)
			{
				if (i == 0)
					xr_strcpy(buff, pm->m_base_name.c_str());
				else
					xr_sprintf(buff, "%s%d", pm->m_base_name.c_str(), i);

				motion_ID = model->ID_Cycle_Safe(buff);
				if (!motion_ID.valid() && i == 0)
				{
					motion_ID = model->ID_Cycle_Safe("hand_idle_doun");
				}
				if (motion_ID.valid())
				{
					pm->m_animations.resize(pm->m_animations.size() + 1);
					pm->m_animations.back().mid = motion_ID;
					pm->m_animations.back().name = buff;
#ifdef DEBUG
					//					Msg(" alias=[%s] base=[%s] name=[%s]",pm->m_alias_name.c_str(), pm->m_base_name.c_str(), buff);
#endif // #ifdef DEBUG
				}
			}
			VERIFY2(pm->m_animations.size(), make_string("motion not found [%s]", pm->m_base_name.c_str()).c_str());
		}
	}
}

Fvector& attachable_hud_item::hands_attach_pos()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_offset[0][0];
	return m_measures.m_hands_attach[0];
}

Fvector& attachable_hud_item::hands_attach_rot()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_offset[1][0];
	return m_measures.m_hands_attach[1];
}

Fvector& attachable_hud_item::hands_offset_pos()
{
	u8 idx = m_parent_hud_item->GetCurrentHudOffsetIdx();
	return m_measures.m_hands_offset[0][idx];
}

Fvector& attachable_hud_item::hands_offset_rot()
{
	u8 idx = m_parent_hud_item->GetCurrentHudOffsetIdx();
	return m_measures.m_hands_offset[1][idx];
}

Fvector& attachable_hud_item::aim_offset_pos()
{
	if (g_player_hud->m_adjust_mode) {
		if (m_attach_place_idx == SCOPE_ATTACH_IDX)
			return g_player_hud->m_adjust_offset[0][8];
		return g_player_hud->m_adjust_offset[0][1];
	}
	return m_measures.m_hands_offset[0][1];
}

Fvector& attachable_hud_item::aim_offset_rot()
{
	if (g_player_hud->m_adjust_mode) {
		if (m_attach_place_idx == SCOPE_ATTACH_IDX)
			return g_player_hud->m_adjust_offset[1][8];
		return g_player_hud->m_adjust_offset[1][1];
	}
	return m_measures.m_hands_offset[1][1];
}

Fvector& attachable_hud_item::alt_aim_offset_pos()
{
	if (g_player_hud->m_adjust_mode) {
		if (m_attach_place_idx == SCOPE_ATTACH_IDX)
			return g_player_hud->m_adjust_offset[0][9];
		return g_player_hud->m_adjust_offset[0][3];
	}
	return m_measures.m_hands_offset[0][3];
}

Fvector& attachable_hud_item::alt_aim_offset_rot()
{
	if (g_player_hud->m_adjust_mode) {
		if (m_attach_place_idx == SCOPE_ATTACH_IDX)
			return g_player_hud->m_adjust_offset[1][9];
		return g_player_hud->m_adjust_offset[1][3];
	}
	return m_measures.m_hands_offset[1][3];
}

Fvector& attachable_hud_item::attach_base_offset_pos()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_offset[0][6];
	return m_measures.m_hands_offset[0][6];
}

Fvector& attachable_hud_item::attach_base_offset_rot()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_offset[1][6];
	return m_measures.m_hands_offset[1][6];
}

Fvector& attachable_hud_item::attach_mount_offset_pos()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_offset[0][7];
	return m_measures.m_hands_offset[0][7];
}

Fvector& attachable_hud_item::attach_mount_offset_rot()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_offset[1][7];
	return m_measures.m_hands_offset[1][7];
}

float attachable_hud_item::attach_scale()
{
	if (g_player_hud->m_adjust_mode)
		return g_player_hud->m_adjust_scale;
	return m_measures.m_attach_scale;
}

void attachable_hud_item::set_bone_visible(const shared_str& bone_name, BOOL bVisibility, BOOL bSilent)
{
	u16 bone_id;
	BOOL bVisibleNow;
	bone_id = m_model->LL_BoneID(bone_name);
	if (bone_id == BI_NONE)
	{
		if (bSilent) return;
		R_ASSERT2(
			0, make_string("model [%s] has no bone [%s]", pSettings->r_string(m_sect_name, "item_visual"), bone_name.
				c_str()).c_str());
	}
	bVisibleNow = m_model->LL_GetBoneVisible(bone_id);
	if (bVisibleNow != bVisibility)
		m_model->LL_SetBoneVisible(bone_id, bVisibility, TRUE);
}

void attachable_hud_item::update(bool bForce)
{
	if (!bForce && m_upd_firedeps_frame == Device.dwFrame) return;
	bool is_16x9 = UI().is_widescreen();

	if (!!m_measures.m_prop_flags.test(hud_item_measures::e_16x9_mode_now) != is_16x9)
		m_measures.load(m_sect_name, m_model);

	Fvector ypr = m_parent->m_adjust_mode ? m_parent->m_adjust_obj[1] : m_measures.m_item_attach[1];
	ypr.mul(PI / 180.f);
	m_attach_offset.setHPB(ypr.x, ypr.y, ypr.z);
	m_attach_offset.translate_over(m_parent->m_adjust_mode ? m_parent->m_adjust_obj[0] : m_measures.m_item_attach[0]);

	if (m_attach_place_idx == SCOPE_ATTACH_IDX) {
		m_item_transform.set(m_parent->attached_item(0)->m_item_transform);

		Fmatrix hud_rotation;
		hud_rotation.identity();
		hud_rotation.rotateX(m_parent->attached_item(0)->attach_mount_offset_rot().x);

		Fmatrix hud_rotation_y;
		hud_rotation_y.identity();
		hud_rotation_y.rotateY(m_parent->attached_item(0)->attach_mount_offset_rot().y);
		hud_rotation.mulA_43(hud_rotation_y);

		hud_rotation_y.identity();
		hud_rotation_y.rotateZ(m_parent->attached_item(0)->attach_mount_offset_rot().z);
		hud_rotation.mulA_43(hud_rotation_y);

		hud_rotation.translate_over(m_parent->attached_item(0)->attach_mount_offset_pos());
		m_item_transform.mulB_43(hud_rotation);
	}
	else {
        m_item_transform_prev = m_item_transform;
		m_parent->calc_transform(m_attach_place_idx, m_attach_offset, m_item_transform, m_measures.m_bLeadGunLeftHand);
		m_upd_firedeps_frame = Device.dwFrame;
	}

	IKinematicsAnimated* ka = m_model->dcast_PKinematicsAnimated();
	if (ka)
	{
		ka->UpdateTracks();
		ka->dcast_PKinematics()->CalculateBones_Invalidate();
		ka->dcast_PKinematics()->CalculateBones(TRUE);
	}
}

void attachable_hud_item::update_attachments_xform()
{
	if (!m_parent_hud_item->has_object()) return;

	for (auto& pair : *m_parent_hud_item->object().GetAttachments())
	{
		script_attachment* att = pair.second;
		if (att->GetType() == eSA_HUD)
			att->UpdateXFORM(m_model, &m_item_transform);
	}
}

void attachable_hud_item::setup_firedeps(firedeps& fd)
{
	update(false);
	// fire point&direction
	if (m_measures.m_prop_flags.test(hud_item_measures::e_fire_point))
	{
		Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone);
		fire_mat.transform_tiny(fd.vLastFP, m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[0][0] : m_measures.m_fire_point_offset);
		m_item_transform.transform_tiny(fd.vLastFP);

		fd.vLastFD.set(m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[1][0] : m_measures.m_fire_direction);
		m_item_transform.transform_dir(fd.vLastFD);
		VERIFY(_valid(fd.vLastFD));

		fd.m_FireParticlesXForm.identity();
		fd.m_FireParticlesXForm.k.set(fd.vLastFD);
		Fvector::generate_orthonormal_basis_normalized(fd.m_FireParticlesXForm.k,
			fd.m_FireParticlesXForm.j,
			fd.m_FireParticlesXForm.i);

		VERIFY(_valid(fd.m_FireParticlesXForm));

		// demonized: transforms for fire bone/point silencer, they should be identical to above if they dont exist
		{
			Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone_silencer);
			fire_mat.transform_tiny(fd.vLastFPSilencer, m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[0][0] : m_measures.m_fire_point_silencer);
			m_item_transform.transform_tiny(fd.vLastFPSilencer);
			fd.vLastFD.set(m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[1][0] : m_measures.m_fire_direction);
			VERIFY(_valid(fd.vLastFPSilencer));
		}
	}

	if (m_measures.m_prop_flags.test(hud_item_measures::e_fire_point2))
	{
		Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone2);
		fire_mat.transform_tiny(fd.vLastFP2, m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[0][1] : m_measures.m_fire_point2_offset);
		m_item_transform.transform_tiny(fd.vLastFP2);
		fd.vLastFD.set(m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[1][0] : m_measures.m_fire_direction);
		VERIFY(_valid(fd.vLastFP2));
	}

	if (m_measures.m_prop_flags.test(hud_item_measures::e_shell_point))
	{
		if (m_measures.m_shell_bone == BI_NONE)
		{
			return;
		}
		Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_shell_bone);
		fire_mat.transform_tiny(fd.vLastSP, m_parent->m_adjust_mode ? m_parent->m_adjust_firepoint_shell[1][1] : m_measures.m_shell_point_offset);
		m_item_transform.transform_tiny(fd.vLastSP);
		VERIFY(_valid(fd.vLastSP));
	}
}

bool attachable_hud_item::need_renderable()
{
	return m_parent_hud_item->need_renderable();
}

void attachable_hud_item::render(IDSGraphManager* DM, Fmatrix* draw_xform)
{
	m_parent_hud_item->render_hud_mode();
	if (m_parent_hud_item->m_instance_holder_hud && !m_parent_hud_item->m_instance_holder_hud->ModelInstanceMultirender()) return;
	// draw_xform (GammaVR -vr_vis_rot test control) must stay valid until the pass is drawn: the HUD graph keeps the pointer
	DM->add_Dynamic(m_model->dcast_RenderVisual(), draw_xform ? draw_xform : &m_item_transform);
	if (!m_parent_hud_item->has_object()) return;

		for (auto& pair : *m_parent_hud_item->object().GetAttachments())
		{
			if (pair.second->GetType() == eSA_HUD)
			pair.second->renderable_Render(DM);
	}
}

bool attachable_hud_item::render_item_ui_query()
{
	return m_parent_hud_item->render_item_3d_ui_query();
}

void attachable_hud_item::render_item_ui()
{
	m_parent_hud_item->render_item_3d_ui();

	if (!m_parent_hud_item->has_object()) return;

		for (auto& pair : *m_parent_hud_item->object().GetAttachments())
		{
			if (pair.second->GetType() == eSA_HUD)
				pair.second->RenderUI();
	}
}

void hud_item_measures::load(const shared_str& sect_name, IKinematics* K)
{
	bool is_16x9 = UI().is_widescreen();
	string64 _prefix;
	xr_sprintf(_prefix, "%s", is_16x9 ? "_16x9" : "");
	string128 val_name;

	strconcat(sizeof(val_name), val_name, "hands_position", _prefix);
	m_hands_attach[0] = pSettings->r_fvector3(sect_name, val_name);
	strconcat(sizeof(val_name), val_name, "hands_orientation", _prefix);
	m_hands_attach[1] = pSettings->r_fvector3(sect_name, val_name);

	m_item_attach[0] = pSettings->r_fvector3(sect_name, "item_position");
	m_item_attach[1] = pSettings->r_fvector3(sect_name, "item_orientation");

	shared_str bone_name;
	m_prop_flags.set(e_fire_point, pSettings->line_exist(sect_name, "fire_bone"));
	if (m_prop_flags.test(e_fire_point))
	{
		// demonized: replacing r_string with READ_IF_EXISTS
		bone_name = READ_IF_EXISTS(pSettings, r_string, sect_name, "fire_bone", "wpn_body");
		//bone_name = pSettings->r_string(sect_name, "fire_bone");
		m_fire_bone = K->LL_BoneID(bone_name);

		// Print warning if bone doesn't exist
		if (m_fire_bone == BI_NONE)
		{
			Msg("![%s] Invalid or not found fire_bone %s", sect_name.c_str(), bone_name.c_str());
		}

		m_fire_point_offset = pSettings->r_fvector3(sect_name, "fire_point");
		m_fire_direction = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, "fire_direction",
			Fvector().set(0.f, 0.f, 1.0f));
	}
	else
		m_fire_point_offset.set(0, 0, 0);

	m_prop_flags.set(e_fire_point2, pSettings->line_exist(sect_name, "fire_bone2"));
	if (m_prop_flags.test(e_fire_point2))
	{
		bone_name = READ_IF_EXISTS(pSettings, r_string, sect_name, "fire_bone2", "wpn_body");
		//bone_name = pSettings->r_string(sect_name, "fire_bone2");
		m_fire_bone2 = K->LL_BoneID(bone_name);

		// Print warning if bone doesn't exist
		if (m_fire_bone2 == BI_NONE)
		{
			Msg("![%s] Invalid or not found fire_bone2 %s", sect_name.c_str(), bone_name.c_str());
		}

		m_fire_point2_offset = pSettings->r_fvector3(sect_name, "fire_point2");
	}
	else
		m_fire_point2_offset.set(0, 0, 0);

	// demonized: fire point for silencer
	m_prop_flags.set(e_fire_point_silencer, pSettings->line_exist(sect_name, "fire_bone_silencer"));
	bone_name = READ_IF_EXISTS(pSettings, r_string, sect_name, "fire_bone_silencer", READ_IF_EXISTS(pSettings, r_string, sect_name, "fire_bone", "wpn_body"));
	m_fire_bone_silencer = K->LL_BoneID(bone_name);
	m_fire_point_silencer.set(pSettings->line_exist(sect_name, "fire_point_silencer") ? pSettings->r_fvector3(sect_name, "fire_point_silencer") : m_fire_point_offset);

	m_prop_flags.set(e_shell_point, pSettings->line_exist(sect_name, "shell_bone"));
	if (m_prop_flags.test(e_shell_point))
	{
		bone_name = READ_IF_EXISTS(pSettings, r_string, sect_name, "shell_bone", "wpn_body");
		//bone_name = pSettings->r_string(sect_name, "shell_bone");
		m_shell_bone = K->LL_BoneID(bone_name);

		// Print warning if bone doesn't exist
		if (m_shell_bone == BI_NONE)
		{
			Msg("![%s] Invalid or not found shell_bone %s", sect_name.c_str(), bone_name.c_str());
		}

		m_shell_point_offset = pSettings->r_fvector3(sect_name, "shell_point");
	}
	else
		m_shell_point_offset.set(0, 0, 0);

	m_hands_offset[0][0].set(0, 0, 0);
	m_hands_offset[1][0].set(0, 0, 0);

	//DaimeneX: base_hud_offset_pos and base_hud_offset_rot. Editable variables that allow weapon position adjustements that don't require aim tweaks
	strconcat(sizeof(val_name), val_name, "base_hud_offset_pos", _prefix);
	if (pSettings->line_exist(sect_name, val_name))
		m_hands_offset[0][5] = pSettings->r_fvector3(sect_name, val_name);
	else
		m_hands_offset[0][5].set(0, 0, 0);

	strconcat(sizeof(val_name), val_name, "base_hud_offset_rot", _prefix);
	if (pSettings->line_exist(sect_name, val_name))
		m_hands_offset[1][5] = pSettings->r_fvector3(sect_name, val_name);
	else
		m_hands_offset[1][5].set(0, 0, 0);

	strconcat(sizeof(val_name), val_name, "aim_hud_offset_pos", _prefix);
	m_hands_offset[0][1] = pSettings->r_fvector3(sect_name, val_name);
	strconcat(sizeof(val_name), val_name, "aim_hud_offset_rot", _prefix);
	m_hands_offset[1][1] = pSettings->r_fvector3(sect_name, val_name);

	strconcat(sizeof(val_name), val_name, "gl_hud_offset_pos", _prefix);
	m_hands_offset[0][2] = pSettings->r_fvector3(sect_name, val_name);
	strconcat(sizeof(val_name), val_name, "gl_hud_offset_rot", _prefix);
	m_hands_offset[1][2] = pSettings->r_fvector3(sect_name, val_name);

	// demonized: removing asserts
	/*R_ASSERT2(pSettings->line_exist(sect_name,"fire_point")==pSettings->line_exist(sect_name,"fire_bone"),
			  sect_name.c_str());
	R_ASSERT2(pSettings->line_exist(sect_name,"fire_point2")==pSettings->line_exist(sect_name,"fire_bone2"),
			  sect_name.c_str());
	R_ASSERT2(pSettings->line_exist(sect_name,"shell_point")==pSettings->line_exist(sect_name,"shell_bone"),
			  sect_name.c_str());*/

	m_prop_flags.set(e_16x9_mode_now, is_16x9);

	////////////////////////////////////////////
	//--#SM+# Begin--
	const Fvector vZero = { 0.f, 0.f, 0.f };

	// Альтернативное прицеливание
	strconcat(sizeof(val_name), val_name, "aim_hud_offset_alt_pos", _prefix);
	m_hands_offset[0][3] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);
	strconcat(sizeof(val_name), val_name, "aim_hud_offset_alt_rot", _prefix);
	m_hands_offset[1][3] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);

	// Safemode / lowered weapon position
	strconcat(sizeof(val_name), val_name, "lowered_hud_offset_pos", _prefix);
	m_hands_offset[0][4] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);
	strconcat(sizeof(val_name), val_name, "lowered_hud_offset_rot", _prefix);
	m_hands_offset[1][4] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);

	// Настройки стрейфа (боковая ходьба)
	Fvector vDefStrafeValue;
	vDefStrafeValue.set(vZero);

	//--> Смещение в стрейфе
	strconcat(sizeof(val_name), val_name, "strafe_hud_offset_pos", _prefix);
	m_strafe_offset[0][0] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vDefStrafeValue);
	strconcat(sizeof(val_name), val_name, "strafe_hud_offset_rot", _prefix);
	m_strafe_offset[1][0] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vDefStrafeValue);

	//--> Поворот в стрейфе
	strconcat(sizeof(val_name), val_name, "strafe_aim_hud_offset_pos", _prefix);
	m_strafe_offset[0][1] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vDefStrafeValue);
	strconcat(sizeof(val_name), val_name, "strafe_aim_hud_offset_rot", _prefix);
	m_strafe_offset[1][1] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vDefStrafeValue);

	//--> Параметры стрейфа
	bool bStrafeEnabled = READ_IF_EXISTS(pSettings, r_bool, sect_name, "strafe_enabled", false);
	bool bStrafeEnabled_aim = READ_IF_EXISTS(pSettings, r_bool, sect_name, "strafe_aim_enabled", false);
	float fFullStrafeTime = READ_IF_EXISTS(pSettings, r_float, sect_name, "strafe_transition_time", .5f);
	float fFullStrafeTime_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "strafe_aim_transition_time", .5f);
	float fStrafeCamLFactor = READ_IF_EXISTS(pSettings, r_float, sect_name, "strafe_cam_limit_factor", 0.5f);
	float fStrafeCamLFactor_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "strafe_cam_limit_aim_factor", 1.0f);
	float fStrafeMinAngle = READ_IF_EXISTS(pSettings, r_float, sect_name, "strafe_cam_min_angle", 0.0f);
	float fStrafeMinAngle_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "strafe_cam_aim_min_angle", 7.0f);

	//--> (Data 1)
	m_strafe_offset[2][0].set((bStrafeEnabled ? 1.0f : 0.0f), fFullStrafeTime, NULL); // normal
	m_strafe_offset[2][1].set((bStrafeEnabled_aim ? 1.0f : 0.0f), fFullStrafeTime_aim, NULL); // aim-GL

	//--> (Data 2)
	m_strafe_offset[3][0].set(fStrafeCamLFactor, fStrafeMinAngle, NULL); // normal
	m_strafe_offset[3][1].set(fStrafeCamLFactor_aim, fStrafeMinAngle_aim, NULL); // aim-GL

	// Загрузка параметров смещения / инерции
	m_inertion_params.m_tendto_speed = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_tendto_speed",
		TENDTO_SPEED);
	m_inertion_params.m_tendto_speed_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_tendto_aim_speed",
		TENDTO_SPEED_AIM);
	m_inertion_params.m_tendto_ret_speed = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_tendto_ret_speed",
		TENDTO_SPEED_RET);
	m_inertion_params.m_tendto_ret_speed_aim = READ_IF_EXISTS(pSettings, r_float, sect_name,
		"inertion_tendto_ret_aim_speed", TENDTO_SPEED_RET_AIM);

	m_inertion_params.m_min_angle =
		READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_min_angle", INERT_MIN_ANGLE);
	m_inertion_params.m_min_angle_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_min_angle_aim",
		INERT_MIN_ANGLE_AIM);

	m_inertion_params.m_offset_LRUD = READ_IF_EXISTS(pSettings, r_fvector4, sect_name, "inertion_offset_LRUD",
		Fvector4().set(ORIGIN_OFFSET));
	m_inertion_params.m_offset_LRUD_aim = READ_IF_EXISTS(pSettings, r_fvector4, sect_name, "inertion_offset_LRUD_aim",
		Fvector4().set(ORIGIN_OFFSET_AIM));

	// Загрузка параметров смещения при стрельбе
	m_shooting_params.bShootShake = READ_IF_EXISTS(pSettings, r_bool, sect_name, "shooting_hud_effect", false);
	m_shooting_params.m_shot_max_offset_LRUD = READ_IF_EXISTS(pSettings, r_fvector4, sect_name, "shooting_max_LRUD",
		Fvector4().set(0, 0, 0, 0));
	m_shooting_params.m_shot_max_offset_LRUD_aim = READ_IF_EXISTS(pSettings, r_fvector4, sect_name,
		"shooting_max_LRUD_aim", Fvector4().set(0, 0, 0, 0));
	m_shooting_params.m_shot_offset_BACKW = READ_IF_EXISTS(pSettings, r_fvector2, sect_name, "shooting_backward_offset",
		Fvector2().set(0, 0));
	m_shooting_params.m_ret_speed = READ_IF_EXISTS(pSettings, r_float, sect_name, "shooting_ret_speed", 1.0f);
	m_shooting_params.m_ret_speed_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "shooting_ret_aim_speed", 1.0f);
	m_shooting_params.m_min_LRUD_power = READ_IF_EXISTS(pSettings, r_float, sect_name, "shooting_min_LRUD_power", 0.0f);

	//--#SM+# End--

	strconcat(sizeof(val_name), val_name, "attach_base_hud_offset_pos", _prefix);
	m_hands_offset[0][6] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);
	strconcat(sizeof(val_name), val_name, "attach_base_hud_offset_rot", _prefix);
	m_hands_offset[1][6] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);

	strconcat(sizeof(val_name), val_name, "attach_mount_hud_offset_pos", _prefix);
	m_hands_offset[0][7] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);
	strconcat(sizeof(val_name), val_name, "attach_mount_hud_offset_rot", _prefix);
	m_hands_offset[1][7] = READ_IF_EXISTS(pSettings, r_fvector3, sect_name, val_name, vZero);

    //custom HUD offset
    m_hands_offset[0][8] = Fvector().set(0, 0, 0);
    m_hands_offset[1][8] = Fvector().set(0, 0, 0);

	m_fFreelookZOffset = READ_IF_EXISTS(pSettings, r_float, sect_name, "freelook_z_offset_mul", 0.f);
	m_bLeadGunLeftHand = READ_IF_EXISTS(pSettings, r_bool, sect_name, "lh_lead_gun", false);

	m_attach_scale = READ_IF_EXISTS(pSettings, r_float, sect_name, "attach_scale", 1);
}

attachable_hud_item::~attachable_hud_item()
{
	if (m_parent_hud_item->m_instance_holder_hud) m_parent_hud_item->m_instance_holder_hud->ClearModelInstance();

	IRenderVisual* v = m_model->dcast_RenderVisual();
	::Render->model_Delete(v);
	m_model = nullptr;
}

void attachable_hud_item::load(const shared_str& sect_name)
{
	m_sect_name = sect_name;

	// Visual
	LPCSTR visual_name = pSettings->r_string(sect_name, "item_visual");
	::Render->hud_loading = true;
	IKinematicsAnimated* visual = ::Render->model_Create(visual_name)->dcast_PKinematicsAnimated();
	::Render->hud_loading = false;
	R_ASSERT2(visual, make_string("could not create model %s, section %s", visual_name, sect_name.c_str()));
	m_model = smart_cast<IKinematics*>(visual);
    if (m_model)
    {
        IRenderVisual* pVisual = m_model->dcast_RenderVisual();
        if (pVisual)
            pVisual->MarkIgnoreOptimization(TRUE);
    }    

	m_attach_place_idx = pSettings->r_u16(sect_name, "attach_place_idx");
	m_measures.load(sect_name, m_model);
	m_hand_motions = m_parent->get_hand_motions(*sect_name);

    // demonized: shell particles for hud
    // An explicitly empty setting disables particles instead of using the weapon default.
    m_shell_particles_override = pSettings->line_exist(sect_name, "shell_particles");
    m_shell_particles = m_shell_particles_override ? pSettings->r_string(sect_name, "shell_particles") : nullptr;
}

player_hud_motion* attachable_hud_item::find_motion(const shared_str& anm_name)
{
	R_ASSERT(strstr(anm_name.c_str(), "anm_") == anm_name.c_str());
	string256 anim_name_r;
	bool is_16x9 = UI().is_widescreen();
	xr_sprintf(anim_name_r, "%s%s", anm_name.c_str(), ((m_attach_place_idx == 1) && is_16x9) ? "_16x9" : "");

	player_hud_motion* anm = m_hand_motions->find_motion(anim_name_r);

	if (!anm)
		anm = m_hand_motions->find_motion(anm_name);

	R_ASSERT2(anm, make_string("model [%s] has no motion alias defined [%s]", m_sect_name.c_str(), anm_name).c_str())
		;
	VERIFY2(anm->m_animations.size(),
		make_string("model [%s] has no motion defined in motion_alias [%s]", pSettings->r_string(m_sect_name,
			"item_visual"), anim_name_r).c_str());

	return anm;
}

u32 attachable_hud_item::anim_play(const shared_str& anm_name_b, BOOL bMixIn, const CMotionDef*& md, u8& rnd_idx,
                                   float& speed, bool bMixIn2)
{
	player_hud_motion* anm = find_motion(anm_name_b);
	rnd_idx = (u8)Random.randI(anm->m_animations.size());
	const motion_descr& M = anm->m_animations[rnd_idx];
	if (speed == 1.f)
		speed = anm->m_anim_speed != 0.f ? anm->m_anim_speed : 1.f;

	u32 ret = 0;
	if (m_attach_place_idx != SCOPE_ATTACH_IDX) {
		ret = g_player_hud->anim_play(m_attach_place_idx, M.mid, bMixIn, md, speed);
	}

	if (m_model->dcast_PKinematicsAnimated())
	{
		IKinematicsAnimated* ka = m_model->dcast_PKinematicsAnimated();

		shared_str item_anm_name;
		if (anm->m_base_name != anm->m_additional_name)
			item_anm_name = anm->m_additional_name;
		else
			item_anm_name = M.name;

		MotionID M2 = ka->ID_Cycle_Safe(item_anm_name);
		if (!M2.valid())
			M2 = ka->ID_Cycle_Safe("idle");
		else if (bDebug)
			Msg("playing item animation [%s]", item_anm_name.c_str());

		R_ASSERT3(M2.valid(), make_string("model has no motion [idle], section %s", m_sect_name.c_str()).c_str(), pSettings->r_string(m_sect_name, "item_visual"));

		u16 root_id = m_model->LL_GetBoneRoot();
		CBoneInstance& root_binst = m_model->LL_GetBoneInstance(root_id);
		root_binst.set_callback_overwrite(TRUE);
		root_binst.mTransform.identity();
		if (m_attach_place_idx == SCOPE_ATTACH_IDX) {
			float s = m_parent->attached_item(0)->attach_scale();
			root_binst.mTransform.scale(s, s, s);
		}

		u16 pc = ka->partitions().count();
		for (u16 pid = 0; pid < pc; ++pid)
			CBlend* B = ka->PlayCycle(pid, M2, (!!bMixIn && bMixIn2), 0, 0, 0, speed);

		m_model->CalculateBones_Invalidate();
	}

	if (m_attach_place_idx == SCOPE_ATTACH_IDX) {
		return ret;
	}

	R_ASSERT2(m_parent_hud_item, "parent hud item is NULL");
	if (m_parent_hud_item->has_object()) {
		CPhysicItem& parent_object = m_parent_hud_item->object();
		//R_ASSERT2		(parent_object, "object has no parent actor");
		//CObject*		parent_object = static_cast_checked<CObject*>(&m_parent_hud_item->object());

		if (IsGameTypeSingle() && parent_object.H_Parent() == Level().CurrentControlEntity())
		{
			CActor* current_actor = static_cast_checked<CActor*>(Level().CurrentControlEntity());
			VERIFY(current_actor);

			string_path ce_path;
			string_path anm_name;
			strconcat(sizeof(anm_name), anm_name, "camera_effects\\weapon\\", M.name.c_str(), ".anm");
			if (FS.exist(ce_path, "$game_anims$", anm_name))
			{
				int rand = ::Random.randI(5000, 10000);
				CAnimatorCamEffector* e = xr_new<CHudMotionCamEffector>();
				e->SetType(ECamEffectorType(rand));
				e->SetHudAffect(false);
				e->SetCyclic(false);
				e->Start(anm_name);
				current_actor->Cameras().AddCamEffector(e);
			}
		}
	}
	return ret;
}

player_hud::player_hud()
{
	m_model = nullptr;
	m_model_2 = nullptr;

	m_attached_items[0] = nullptr;
	m_attached_items[1] = nullptr;
	m_attached_items[SCOPE_ATTACH_IDX] = nullptr;
	m_attach_offset.identity();
	m_attach_offset_2.identity();
	m_transform.identity();
	m_transform_prev.identity();
	m_transform_2.identity();
	m_ik_shoulder_reference[0].identity();
	m_ik_shoulder_reference[1].identity();
	m_transform_2_prev.identity();
	m_adjust_mode = false;
	script_anim_part = u8(-1);
	script_anim_offset_factor = 0.f;
	script_anim_lock_camera = 0.f;
	m_item_pos.identity();
	script_override_arms = false;
	script_anim_item_model = nullptr;
	override_hand_pose[0] = false;
	override_hand_pose[1] = false;

	//Bone Callback Params
	m_bone_callback_params.insert(mk_pair(r_finger0, xr_new<BoneCallbackParams>()));
	m_bone_callback_params.insert(mk_pair(r_finger01, xr_new<BoneCallbackParams>()));
	m_bone_callback_params.insert(mk_pair(r_finger02, xr_new<BoneCallbackParams>()));
	//m_bone_callback_params.insert(mk_pair(bip01_r_finger1, xr_new<BoneCallbackParams>()));
	//m_bone_callback_params.insert(mk_pair(bip01_r_finger11, xr_new<BoneCallbackParams>()));
	//m_bone_callback_params.insert(mk_pair(bip01_r_finger12, xr_new<BoneCallbackParams>()));

	for (int i = 0; i < 8; ++i)
	{
		m_ik_transforms[i].first.identity();
		m_ik_transforms[i].second = false;
	}

	//Movement Layers
	m_movement_layers.reserve(move_anms_end);

	for (int i = 0; i < move_anms_end; i++)
	{
		movement_layer* anm = xr_new<movement_layer>();

		char temp[20];
		string512 tmp;
		strconcat(sizeof(temp), temp, "movement_layer_", std::to_string(i).c_str());

		if (pSettings->line_exist("hud_movement_layers", temp))
		{
			LPCSTR layer_def = pSettings->r_string("hud_movement_layers", temp);
			R_ASSERT2(_GetItemCount(layer_def) > 0, make_string("Wrong definition for [hud_movement_layers] %s", temp));

			_GetItem(layer_def, 0, tmp);
			anm->Load(tmp);
			_GetItem(layer_def, 1, tmp);
			anm->anm->Speed() = (atof(tmp) ? atof(tmp) : 1.f);
			_GetItem(layer_def, 2, tmp);
			anm->m_power = (atof(tmp) ? atof(tmp) : 1.f);
		}

		m_movement_layers.push_back(anm);
	}
}

player_hud::~player_hud()
{
	IRenderVisual* v = m_model->dcast_RenderVisual();
	::Render->model_Delete(v);
	m_model = nullptr;

	v = m_model_2->dcast_RenderVisual();
	::Render->model_Delete(v);
	m_model_2 = nullptr;

	delete_script_anim_item_model();

	delete_data(m_hand_motions);
	delete_data(m_script_layers);
	delete_data(m_movement_layers);
	delete_data(m_bone_callback_params);
}

void player_hud::FingerCallback(CBoneInstance* B)
{
	BoneCallbackParams* params = static_cast<BoneCallbackParams*>(B->callback_param());

	Fvector& target = params->m_target;
	Fvector& current = params->m_current;

	if (!target.similar(current))
	{
		Fvector diff[2];
		diff[0] = target;
		diff[0].sub(current);
		diff[0].mul(Device.fTimeDelta / .1f);
		current.add(diff[0]);
	}
	else
		current.set(target);

	Fmatrix rotation;
	rotation.identity();
	rotation.rotateX(current.x);

	Fmatrix rotation_y;
	rotation_y.identity();
	rotation_y.rotateY(current.y);
	rotation.mulA_43(rotation_y);

	rotation_y.identity();
	rotation_y.rotateZ(current.z);
	rotation.mulA_43(rotation_y);

	B->mTransform.mulB_43(rotation);
}

void IKHandCallback(CBoneInstance* B)
{
	xr_pair<Fmatrix, bool>* pair = static_cast<xr_pair<Fmatrix, bool>*>(B->callback_param());
	if (g_player_hud->attached_item(1) && pair < &g_player_hud->m_ik_transforms[4]) return;
	const int arm = int(pair - g_player_hud->m_ik_transforms) / 4;
	if (!g_player_hud->m_arm_ik[arm].enabled) return;
	if (!pair->second) return;

	B->mTransform = pair->first;
}

bool force_hands_bone_parts = false;

void player_hud::load(const shared_str& player_hud_sect, bool force)
{
	if (!force && player_hud_sect == m_sect_name) return;

	m_sect_name = player_hud_sect;

	if (script_override_arms) return;

	if (m_model)
	{
		IRenderVisual* v = m_model->dcast_RenderVisual();
		::Render->model_Delete(v);
	}
	if (m_model_2)
	{
		IRenderVisual* v = m_model_2->dcast_RenderVisual();
		::Render->model_Delete(v);
	}

	const shared_str& model_name = pSettings->r_string(player_hud_sect, "visual");
	::Render->hud_loading = true;
	force_hands_bone_parts = true;
	m_model = smart_cast<IKinematicsAnimated*>(::Render->model_Create(model_name.c_str()));
	m_model_2 = smart_cast<IKinematicsAnimated*>(::Render->model_Create(pSettings->line_exist(player_hud_sect, "visual_2") ? pSettings->r_string(player_hud_sect, "visual_2") : model_name.c_str()));

    if (m_model)
    {
        IRenderVisual* pVisual = m_model->dcast_RenderVisual();
        if (pVisual)
            pVisual->MarkIgnoreOptimization(TRUE);
    }
    if (m_model_2)
    {
        IRenderVisual* pVisual = m_model_2->dcast_RenderVisual();
        if (pVisual)
            pVisual->MarkIgnoreOptimization(TRUE);
    }

	bool b_reload = (m_attached_items[0] != nullptr || m_attached_items[1] != nullptr);
	force_hands_bone_parts = false;
	::Render->hud_loading = false;
	u16 l_arm = m_model->dcast_PKinematics()->LL_BoneID("l_clavicle");
	u16 r_arm = m_model_2->dcast_PKinematics()->LL_BoneID("r_clavicle");

	u16 l_upperarm = m_model_2->dcast_PKinematics()->LL_BoneID("l_upperarm");
	u16 r_upperarm = m_model->dcast_PKinematics()->LL_BoneID("r_upperarm");

	u16 l_forearm = m_model_2->dcast_PKinematics()->LL_BoneID("l_forearm");
	u16 r_forearm = m_model->dcast_PKinematics()->LL_BoneID("r_forearm");

	u16 l_forearm_twist = m_model_2->dcast_PKinematics()->LL_BoneID("l_forearm_twist");
	u16 r_forearm_twist = m_model->dcast_PKinematics()->LL_BoneID("r_forearm_twist");

	u16 l_hand = m_model_2->dcast_PKinematics()->LL_BoneID("l_hand");
	u16 r_hand = m_model->dcast_PKinematics()->LL_BoneID("r_hand");

	m_arm_ik[0].initialized = false;
	m_arm_ik[1].initialized = false;
	m_ik_frame = u32(-1);
	m_arm_ik[0].bones[0] = l_upperarm;
	m_arm_ik[0].bones[1] = l_forearm;
	m_arm_ik[0].bones[2] = l_forearm_twist;
	m_arm_ik[0].bones[3] = l_hand;
	m_arm_ik[1].bones[0] = r_upperarm;
	m_arm_ik[1].bones[1] = r_forearm;
	m_arm_ik[1].bones[2] = r_forearm_twist;
	m_arm_ik[1].bones[3] = r_hand;

	u16 bone_r_finger0 = m_model->dcast_PKinematics()->LL_BoneID("r_finger0");
	u16 bone_r_finger01 = m_model->dcast_PKinematics()->LL_BoneID("r_finger01");
	u16 bone_r_finger02 = m_model->dcast_PKinematics()->LL_BoneID("r_finger02");

	//u16 bone_r_triggerfinger0 = m_model->dcast_PKinematics()->LL_BoneID("bip01_r_finger1");
	//u16 bone_r_triggerfinger01 = m_model->dcast_PKinematics()->LL_BoneID("bip01_r_finger11");
	//u16 bone_r_triggerfinger02 = m_model->dcast_PKinematics()->LL_BoneID("bip01_r_finger12");

	m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_r_finger0).set_callback(bctCustom, FingerCallback, m_bone_callback_params[r_finger0]);
	m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_r_finger01).set_callback(bctCustom, FingerCallback, m_bone_callback_params[r_finger01]);
	m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_r_finger02).set_callback(bctCustom, FingerCallback, m_bone_callback_params[r_finger02]);

	//m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_r_triggerfinger0).set_callback(bctCustom, FingerCallback, m_bone_callback_params[bip01_r_finger1]);
	//m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_r_triggerfinger01).set_callback(bctCustom, FingerCallback, m_bone_callback_params[bip01_r_finger11]);
	//m_model->dcast_PKinematics()->LL_GetBoneInstance(bone_r_triggerfinger02).set_callback(bctCustom, FingerCallback, m_bone_callback_params[bip01_r_finger12]);

	m_model_2->dcast_PKinematics()->LL_GetBoneInstance(l_upperarm).set_callback(bctCustom, IKHandCallback, &m_ik_transforms[0]);
	m_model_2->dcast_PKinematics()->LL_GetBoneInstance(l_forearm).set_callback(bctCustom, IKHandCallback, &m_ik_transforms[1]);
	m_model_2->dcast_PKinematics()->LL_GetBoneInstance(l_forearm_twist).set_callback(bctCustom, IKHandCallback, &m_ik_transforms[2]);
	m_model_2->dcast_PKinematics()->LL_GetBoneInstance(l_hand).set_callback(bctCustom, IKHandCallback, &m_ik_transforms[3]);

	m_model->dcast_PKinematics()->LL_GetBoneInstance(r_upperarm).set_callback(bctCustom, IKHandCallback, &m_ik_transforms[4]);
	m_model->dcast_PKinematics()->LL_GetBoneInstance(r_forearm).set_callback(bctCustom, IKHandCallback, &m_ik_transforms[5]);
	m_model->dcast_PKinematics()->LL_GetBoneInstance(r_forearm_twist).set_callback(bctCustom, IKHandCallback, &m_ik_transforms[6]);
	m_model->dcast_PKinematics()->LL_GetBoneInstance(r_hand).set_callback(bctCustom, IKHandCallback, &m_ik_transforms[7]);

	// hides the unused arm meshes
	m_model->dcast_PKinematics()->LL_SetBoneVisible(l_arm, FALSE, TRUE);
	m_model_2->dcast_PKinematics()->LL_SetBoneVisible(r_arm, FALSE, TRUE);

	CInifile::Sect& _sect = pSettings->r_section(player_hud_sect);
	CInifile::SectCIt _b = _sect.Data.begin();
	CInifile::SectCIt _e = _sect.Data.end();
	for (; _b != _e; ++_b)
	{
		if (strstr(_b->first.c_str(), "ancor_") == _b->first.c_str())
		{
			const shared_str& _bone = _b->second;
			m_ancors.push_back(m_model->dcast_PKinematics()->LL_BoneID(_bone));
		}
	}

	if (!b_reload)
	{
		m_model->PlayCycle("hand_idle_doun");
		m_model_2->PlayCycle("hand_idle_doun");
	}
	else
	{
		if (m_attached_items[1])
			m_attached_items[1]->m_parent_hud_item->on_outfit_changed();

		if (m_attached_items[0])
			m_attached_items[0]->m_parent_hud_item->on_outfit_changed();
	}

	m_model->dcast_PKinematics()->CalculateBones_Invalidate();
	m_model->dcast_PKinematics()->CalculateBones(TRUE);
	m_model_2->dcast_PKinematics()->CalculateBones_Invalidate();
	m_model_2->dcast_PKinematics()->CalculateBones(TRUE);

	//--DSR-- HeatVision_start
	m_model->dcast_RenderVisual()->MarkAsHot(true);
	m_model_2->dcast_RenderVisual()->MarkAsHot(true);
	//--DSR-- HeatVision_end
}

void player_hud::load_script(LPCSTR section)
{
	script_override_arms = false;
	load(section, true);
	script_override_arms = true;
}

bool player_hud::render_item_ui_query()
{
    if (m_attached_items[0] && m_attached_items[0]->render_item_ui_query()) return true;
    if (m_attached_items[1] && m_attached_items[1]->render_item_ui_query()) return true;

    if (m_attached_items[SCOPE_ATTACH_IDX] && m_attached_items[SCOPE_ATTACH_IDX]->render_item_ui_query()) return true;

    for (auto& pair : *g_actor->GetAttachments())
    {
        script_attachment* att = pair.second;
        if (att->GetType() == eSA_HUD && att->HasUI()) return true;
    }

	return false;
}

void player_hud::render_item_ui()
{
	IUIRender::ePointType bk = UI().m_currentPointType;
	UI().m_currentPointType = IUIRender::pttLIT;
	UIRender->CacheSetCullMode(IUIRender::cmNONE);

	if (m_attached_items[0])
		m_attached_items[0]->render_item_ui();

	if (m_attached_items[1])
		m_attached_items[1]->render_item_ui();

	if (m_attached_items[SCOPE_ATTACH_IDX])
		m_attached_items[SCOPE_ATTACH_IDX]->render_item_ui();

	UIRender->CacheSetCullMode(IUIRender::cmCCW);
	UI().m_currentPointType = bk;

	for (auto& pair : *g_actor->GetAttachments())
	{
		script_attachment* att = pair.second;
		if (att->GetType() == eSA_HUD)
			att->RenderUI();
	}
}

// IK helper: takes an animated bone and direction/position resolved by IK, returns transform that matches
// the IK direction/position and having the closest possible rotation to the animated bone, to avoid twists
static Fmatrix apply_ik_pos_dir(const Fmatrix& animated, const Fvector& from, const Fvector& to, const Fvector& position)
{
	Fvector axis;
	axis.crossproduct(from, to);
	const float cosine = clampr(from.dotproduct(to), -1.f, 1.f);
	float sine = axis.magnitude();
	if (sine > EPS_S)
		axis.div(sine);
	else if (cosine < 0.f)
	{
		Fvector fallback;
		fallback.set(_abs(from.y) < .9f ? 0.f : 1.f, _abs(from.y) < .9f ? 1.f : 0.f, 0.f);
		axis.crossproduct(from, fallback).normalize_safe();
		sine = 0.f;
	}
	else
	{
		Fmatrix result = animated;
		result.c = position;
		return result;
	}

	auto rotate = [&](const Fvector& v)
	{
		Fvector cross, result;
		cross.crossproduct(axis, v);
		result.mul(v, cosine);
		result.mad(cross, sine);
		result.mad(axis, axis.dotproduct(v) * (1.f - cosine));
		return result;
	};
	Fmatrix result = animated;
	result.i = rotate(animated.i);
	result.j = rotate(animated.j);
	result.k = rotate(animated.k);
	result.c = position;
	return result;
}

// IK helper: build pole target from the current animated shoulder, elbow and hand.
static bool animation_ik_pole(const Fmatrix* animated, Fvector& cached_direction,
	bool& direction_valid, Fvector& pole)
{
	Fvector axis, upper;
	axis.sub(animated[3].c, animated[0].c);
	const float distance = axis.magnitude();
	if (distance < EPS_S) return false;
	axis.div(distance);
	upper.sub(animated[1].c, animated[0].c);
	Fvector origin, bend;
	origin.mad(animated[0].c, axis, upper.dotproduct(axis));
	bend.sub(animated[1].c, origin);
	// Near extension the measured bend is unreliable; retain the last valid direction.
	if (bend.magnitude() > _max(EPS_S, upper.magnitude() * .001f))
	{
		cached_direction = bend.normalize_safe();
		direction_valid = true;
	}
	else
	{
		if (!direction_valid) return false;
		bend = cached_direction;
		bend.mad(axis, -bend.dotproduct(axis));
		if (bend.square_magnitude() < EPS_S * EPS_S) return false;
		bend.normalize_safe();
	}
	pole.mad(origin, bend, 1.5f);
	return true;
}

void player_hud::set_arm_ik_enabled(u8 hand, bool enabled)
{
	if (hand > 2) return;
	for (int arm = 0; arm < 2; ++arm)
	{
		if (hand != 2 && arm != 1 - hand) continue;
		ArmIK& state = m_arm_ik[arm];
		if (state.enabled == enabled) continue;
		state.enabled = enabled;
		state.initialized = false;
		for (int bone = 0; bone < 4; ++bone)
			m_ik_transforms[arm * 4 + bone].second = false;
		IKinematicsAnimated* model = arm == 0 ? m_model_2 : m_model;
		if (model) model->dcast_PKinematics()->CalculateBones_Invalidate();
	}
}

// IK helper: solve one arm and apply its bone transforms to the skeleton.
void player_hud::solve_hand_ik(int arm)
{
	ArmIK& state = m_arm_ik[arm];
	const int first = arm * 4;
	IKinematics* K = (arm == 0 ? m_model_2 : m_model)->dcast_PKinematics();
	for (int i = 0; i < 3; ++i)
		m_ik_transforms[first + i].second = false;

	if (state.enabled && m_attached_items[0] && (arm == 1 || !m_attached_items[1]))
	{
		for (int i = 0; i < 4; ++i)
			if (state.bones[i] == BI_NONE)
				return;

		Fmatrix animated[4];
		for (int i = 0; i < 4; ++i)
			K->Bone_GetAnimPos(animated[i], state.bones[i], u8(-1), true);

		const Fmatrix animated_hand = animated[3];

		if (!state.initialized)
		{
			// Fixed bone-frame relationships must not follow the animation under an IK target.
			xr_vector<Fmatrix> bind_pose;
			K->LL_GetBindTransform(bind_pose);
			Fmatrix inverse_bind_forearm;
			inverse_bind_forearm.invert(bind_pose[state.bones[1]]);
			state.wrist_from_forearm.mul_43(inverse_bind_forearm, bind_pose[state.bones[2]]);
			state.hand_from_forearm.mul_43(inverse_bind_forearm, bind_pose[state.bones[3]]);
			state.animation_pole_direction_valid = false;
			state.lengths[0] = animated[0].c.distance_to(animated[1].c);
			state.lengths[1] = animated[1].c.distance_to(animated[3].c);
			state.lengths[2] = animated[1].c.distance_to(animated[2].c);
			if (state.lengths[0] < EPS_S || state.lengths[1] < EPS_S)
				return;

			state.pole.set(0.f, -1.f, 0.f);
			state.initialized = true;
		}

		Fvector animation_pole;
		const bool animation_pole_valid = animation_ik_pole(animated, state.animation_pole_direction,
			state.animation_pole_direction_valid, animation_pole);

		if (ps_hud_ik_compensation)
		{
			// Hand targets follow the final HUD transform; shoulders follow the camera and idle offset.
			const Fmatrix& hud_transform = arm == 0 ? m_transform_2 : m_transform;
			Fmatrix inverse_hud, reference;
			inverse_hud.invert(hud_transform);
			reference.mul_43(inverse_hud, m_ik_shoulder_reference[arm]);
			for (auto& bone : animated) bone.mulA_43(reference);
			if (animation_pole_valid) reference.transform_tiny(animation_pole);
		}

		const Fmatrix target = m_ik_transforms[first + 3].second ? m_ik_transforms[first + 3].first : animated_hand;
		Fvector shoulder = animated[0].c;
		const float upper = state.lengths[0], lower = state.lengths[1];

		Fvector direction, pole, normal;
		direction.sub(target.c, shoulder);
		const float distance = direction.magnitude();
		if (distance < EPS_S)
			return;
		direction.div(distance);
		// Use the animated pole directly. Degenerate poses retain the previous bend plane.
		pole = state.pole;
		if (animation_pole_valid)
			pole.sub(animation_pole, shoulder);
		normal.crossproduct(direction, pole);
		if (normal.square_magnitude() < EPS_S * EPS_S)
		{
			normal.crossproduct(direction, state.pole);
			if (normal.square_magnitude() < EPS_S * EPS_S)
			{
				pole.set(_abs(direction.y) < .9f ? 0.f : 1.f, _abs(direction.y) < .9f ? 1.f : 0.f, 0.f);
				normal.crossproduct(direction, pole);
			}
		}
		normal.normalize_safe();
		state.pole.crossproduct(normal, direction).normalize_safe();

		Fvector elbow, hand;
		if (distance > upper + lower - 1e-5f)
		{
			elbow.mad(shoulder, direction, upper);
			hand.mad(elbow, direction, lower);
		}
		else
		{
			const float cosine = clampr((distance * distance + upper * upper - lower * lower) / (2.f * upper * distance), -1.f, 1.f);
			const float angle = acosf(cosine);
			Fvector cross, rotated;
			cross.crossproduct(normal, direction);
			rotated.mul(direction, cosf(angle));
			rotated.mad(cross, sinf(angle));
			rotated.mad(normal, normal.dotproduct(direction) * (1.f - cosf(angle)));
			rotated.normalize_safe();
			elbow.mad(shoulder, rotated, upper);
			hand = target.c;
		}

		Fmatrix pose[4];
		Fvector directions[2];
		directions[0].sub(elbow, shoulder).normalize_safe();
		directions[1].sub(hand, elbow).normalize_safe();
		Fvector original_upper, original_forearm;
		original_upper.sub(animated[1].c, animated[0].c).normalize_safe();
		original_forearm.sub(animated[3].c, animated[1].c).normalize_safe();
		pose[0] = apply_ik_pos_dir(animated[0], original_upper, directions[0], shoulder);
		pose[1] = apply_ik_pos_dir(animated[1], original_forearm, directions[1], elbow);
		pose[3] = target;
		pose[3].c = hand;

		Fvector wrist;
		wrist.mad(elbow, directions[1], state.lengths[2]);
		// Use the skeleton's fixed wrist/hand frames, not the underlying animation's
		// magazine-grab or reload pose, as the reference for target-driven roll.
		Fmatrix reference_hand;
		pose[2].mul_43(pose[1], state.wrist_from_forearm);
		pose[2].c = wrist;
		reference_hand.mul_43(pose[1], state.hand_from_forearm);
		reference_hand.c = hand;
		const Fvector reference_axes[3] = {reference_hand.i, reference_hand.j, reference_hand.k};
		const Fvector target_axes[3] = {target.i, target.j, target.k};
		float sine = 0.f, cosine = 0.f;
		for (int axis = 0; axis < 3; ++axis)
		{
			Fvector cross;
			cross.crossproduct(reference_axes[axis], target_axes[axis]);
			sine += directions[1].dotproduct(cross);
			cosine += reference_axes[axis].dotproduct(target_axes[axis])
				- directions[1].dotproduct(reference_axes[axis]) * directions[1].dotproduct(target_axes[axis]);
		}
		// atan2 wraps the hand's extra roll to [-PI, PI]; the twist bone receives half.
		const float roll = sine * sine + cosine * cosine > EPS_S * EPS_S
			? atan2f(sine, cosine) * .5f : 0.f;
		auto roll_axis = [&](Fvector& axis)
		{
			Fvector cross, rotated;
			cross.crossproduct(directions[1], axis);
			rotated.mul(axis, cosf(roll));
			rotated.mad(cross, sinf(roll));
			rotated.mad(directions[1], directions[1].dotproduct(axis) * (1.f - cosf(roll)));
			axis = rotated;
		};
		roll_axis(pose[2].i);
		roll_axis(pose[2].j);
		roll_axis(pose[2].k);

		for (int i = 0; i < 4; ++i)
		{
			m_ik_transforms[first + i].first = pose[i];
			m_ik_transforms[first + i].second = true;
		}
	}

	// Re-evaluate without advancing tracks: callbacks update descendants and render matrices.
	K->CalculateBones_Invalidate();
	K->CalculateBones(TRUE);
}

void player_hud::finalize_hands_ik()
{
	if (m_ik_frame == Device.dwFrame)
		return;
	m_ik_frame = Device.dwFrame;

	// Lua registers this after game start; lookup must not auto-load IK scripts during loading.
	::luabind::functor<void> update_target;
	if (ai().script_engine().functor("_G.COnHudIKTarget", update_target))
		update_target();

	solve_hand_ik(0);
	solve_hand_ik(1);
}

// GammaVR milestone 3 (Amendment 9, 3b). Called from render_hud() inside the eye loop of a VR frame, i.e. after the
// OpenXR frame has located the grip pose for this frame's predicted display time and after update() has placed the HUD
// rig the normal way. Moves the whole rig (both arm models, the attached items, the script item) by one rigid transform
// C so that the held item's model origin lands on the grip pose mapped to the world through the same tracking-to-world
// mapping as the camera (g_vr_track.track_to_world). C maps the item's current transform onto the target, so a second
// call in the same frame (second eye) gives C = identity. The arms stay attached to the item as they are.
// GammaVR milestone 3b (Amendment 10), logging only. The held item's muzzle (fire point) and barrel axis (direction of
// fire) in the world, from the item's CURRENT transform (i.e. after the grip placement of this frame). Same derivation as
// attachable_hud_item::setup_firedeps(), done locally because setup_firedeps() calls update() and runs from the weapon
// update, not after the placement: muzzle = m_item_transform * (fire bone transform * m_fire_point_offset),
// axis = m_item_transform rotation * m_fire_direction (item model space, default (0,0,1)), normalized.
// False when the item has no fire bone data (no fire_bone line, or the bone is missing).
static bool vr_gun_fire_world(attachable_hud_item* item, Fvector& muzzle, Fvector& axis)
{
	const hud_item_measures& M = item->m_measures;
	if (!M.m_prop_flags.test(hud_item_measures::e_fire_point) || M.m_fire_bone == BI_NONE || !item->m_model)
		return false;
	const Fmatrix& fire_mat = item->m_model->LL_GetTransform(M.m_fire_bone);
	fire_mat.transform_tiny(muzzle, M.m_fire_point_offset);
	item->m_item_transform.transform_tiny(muzzle);
	item->m_item_transform.transform_dir(axis, M.m_fire_direction);
	if (axis.magnitude() < EPS)
		return false;
	axis.normalize();
	return true;
}

// pixel (x right, y down, origin top-left) of world point p in this eye's render target, projected with the matrix the
// HUD pass draws with (CHudInitializer::SetHudMode: Device.mFullTransformHud = mProjectHud * mView of this eye).
// False when the point is behind the camera (w <= 0).
static bool vr_hud_pixel(const Fvector& p, float& px, float& py)
{
	const Fmatrix& T = Device.mFullTransformHud;
	const float w = p.x * T._14 + p.y * T._24 + p.z * T._34 + T._44;
	if (!(w > 0.f))
		return false;
	const float x = (p.x * T._11 + p.y * T._21 + p.z * T._31 + T._41) / w;
	const float y = (p.x * T._12 + p.y * T._22 + p.z * T._32 + T._42) / w;
	px = (x * 0.5f + 0.5f) * float(Device.dwWidth);
	py = (0.5f - y * 0.5f) * float(Device.dwHeight);
	return true;
}

extern ENGINE_API int g_stereo_bench_eye;

// Milestone 3b: on stereo frames 200 and 300 (same counter as the -vr_dump pictures and masks), for each eye pass,
// log the pixels of the item's origin (the point placed on the grip) and of its muzzle, after the placement.
void player_hud::vr_log_gun_pixels()
{
	if (!g_vr_track.active || (g_vr_track.frame != 200 && g_vr_track.frame != 300))
		return;
	attachable_hud_item* item = m_attached_items[0] ? m_attached_items[0] : m_attached_items[1];
	if (!item)
		return;
	static u32 s_logged_frame[2] = { u32(-1), u32(-1) };
	const int eye = g_stereo_bench_eye;
	const int slot = eye == 1 ? 1 : 0;
	if (s_logged_frame[slot] == g_vr_track.frame)
		return;
	s_logged_frame[slot] = g_vr_track.frame;
	string64 o_txt, m_txt;
	float ox, oy, mx, my;
	if (vr_hud_pixel(item->m_item_transform.c, ox, oy))
		xr_sprintf(o_txt, "%.2f %.2f", ox, oy);
	else
		xr_strcpy(o_txt, "nan nan");
	Fvector muzzle, axis;
	if (vr_gun_fire_world(item, muzzle, axis) && vr_hud_pixel(muzzle, mx, my))
		xr_sprintf(m_txt, "%.2f %.2f", mx, my);
	else
		xr_strcpy(m_txt, "nan nan");
	Msg("* [vr3] frame %u eye %d gun_px origin %s muzzle %s size %u %u", g_vr_track.frame, eye, o_txt, m_txt,
		Device.dwWidth, Device.dwHeight);
}

// GammaVR milestone 3c: grip transform of the held item (see vr_grip_lookup below)
static bool s_vr_grip_on = false;      // an offset is applied to the held item
static Fmatrix s_vr_grip_frame = Fidentity; // translate(grip_pos) * Rframe, weapon model space

// GammaVR milestone 3b (Amendment 10 note 3) test switches, parsed once.
struct vr_3b_switches
{
	float vis_rot_deg = 0.f;  // -vr_vis_rot <deg>
	bool vis_rot = false;
	string64 rear, front;     // -vr_barrel_bones <rear> <front>
	bool barrel = false;
	vr_3b_switches()
	{
		rear[0] = front[0] = 0;
		if (LPCSTR p = strstr(Core.Params, "-vr_vis_rot "))
			vis_rot = sscanf(p + 12, "%f", &vis_rot_deg) == 1;
		if (LPCSTR p = strstr(Core.Params, "-vr_barrel_bones "))
			barrel = sscanf(p + 17, "%63s %63s", rear, front) == 2;
	}
};
static const vr_3b_switches& vr_3b()
{
	static const vr_3b_switches s;
	return s;
}

// bone id by name, case-insensitive (BI_NONE if absent)
static u16 vr_find_bone(IKinematics* K, LPCSTR name)
{
	for (u16 i = 0, n = K->LL_BoneCount(); i < n; ++i)
	{
		LPCSTR b = K->LL_BoneName_dbg(i);
		if (b && !stricmp(b, name))
			return i;
	}
	return BI_NONE;
}

// -vr_vis_rot <deg>: test control. Only the DRAWN item models are rotated: yaw to the right by <deg> about the grip pose's
// vertical axis through the grip point (3c: the grip frame item * s_vr_grip_frame; without an offset the item origin and axes), so
// this is the item-local rotation about y: draw = m_item_transform * rotateY (rotateY takes (0,0,1) to (sin a, 0, cos a),
// i.e. to the right for a > 0). The same world rotation is applied to every attached item (scope) so they stay together.
// m_item_transform itself is not changed, so gun_axis / gun_muzzle / gun_px / mask origin come from the unrotated
// transform. The arms (m_transform, m_transform_2) and script HUD attachments are NOT rotated.
// True when the draw transforms m_vr_draw_xform[] are to be used this pass.
bool player_hud::vr_update_vis_rot()
{
	const vr_3b_switches& S = vr_3b();
	static bool s_logged = false;
	if (!s_logged)
	{
		s_logged = true;
		if (S.vis_rot)
			Msg("* [vr3] visual-only rotation: %.2f deg", S.vis_rot_deg);
		else
			Msg("* [vr3] visual-only rotation: none");
	}
	if (!S.vis_rot)
		return false;
	attachable_hud_item* item = m_attached_items[0] ? m_attached_items[0] : m_attached_items[1];
	if (!item)
		return false;
	Fmatrix rot, F, F_inv, W;
	rot.rotateY(deg2rad(S.vis_rot_deg));
	// pivot frame F = the grip frame in the world: item * grip frame (milestone 3c; identity offset = the item origin/axes)
	F.mul_43(item->m_item_transform, s_vr_grip_on ? s_vr_grip_frame : Fidentity);
	F_inv.invert(F);
	W.mul_43(F, rot);
	W.mulB_43(F_inv); // W = F * rot * inverse(F): world rotation about the grip's y axis through the grip point
	for (int i = 0; i < 3; ++i)
		if (m_attached_items[i])
			m_vr_draw_xform[i].mul_43(W, m_attached_items[i]->m_item_transform);
	return true;
}

// Milestone 3b: (a) once, and again when the held item's section changes, the bone list (names and parents only) of
// the held item's own model; (b) with -vr_barrel_bones, every 100 frames the visible barrel axis from two bones of the
// model AS DRAWN: world position of a bone = draw transform (m_item_transform, or m_vr_draw_xform under -vr_vis_rot)
// * LL_GetTransform(bone).c, where LL_GetTransform is the bone's model-space matrix from the last CalculateBones(),
// done in attachable_hud_item::update() this frame; the HUD draw does not recalculate bones
// (r__dsgraph_build.cpp, add_Dynamic, MT_SKELETON_ANIM: CalculateBones only for UI3D).
void player_hud::vr_log_gun_bones()
{
	const int idx = m_attached_items[0] ? 0 : 1;
	attachable_hud_item* item = m_attached_items[idx];
	if (!item || !item->m_model)
		return;
	IKinematics* K = item->m_model;
	static shared_str s_listed;
	if (s_listed != item->m_sect_name)
	{
		s_listed = item->m_sect_name;
		const u16 n = K->LL_BoneCount();
		Msg("* [vr3] gun_bones section %s count %u", item->m_sect_name.c_str(), (u32)n);
		for (u16 i = 0; i < n; ++i)
		{
			LPCSTR name = K->LL_BoneName_dbg(i);
			const u16 par = K->LL_GetData(i).GetParentID();
			LPCSTR pname = par == BI_NONE ? "none" : K->LL_BoneName_dbg(par);
			Msg("* [vr3] gun_bone %u name %s parent %s", (u32)i, name ? name : "?", pname ? pname : "?");
		}
	}

	const vr_3b_switches& S = vr_3b();
	static u32 s_logged_frame = u32(-1);
	if (!S.barrel || g_vr_track.frame % 100 != 0 || s_logged_frame == g_vr_track.frame)
		return;
	s_logged_frame = g_vr_track.frame;
	const u16 br = vr_find_bone(K, S.rear), bf = vr_find_bone(K, S.front);
	static bool s_warned_r = false, s_warned_f = false;
	if (br == BI_NONE && !s_warned_r)
	{
		s_warned_r = true;
		Msg("! [vr3] barrel bone not found: %s", S.rear);
	}
	if (bf == BI_NONE && !s_warned_f)
	{
		s_warned_f = true;
		Msg("! [vr3] barrel bone not found: %s", S.front);
	}
	if (br == BI_NONE || bf == BI_NONE)
	{
		Msg("* [vr3] frame %u gun_vis_axis track nan nan nan world nan nan nan rear %s front %s", g_vr_track.frame, S.rear, S.front);
		Msg("* [vr3] frame %u gun_vis_pts track nan nan nan nan nan nan", g_vr_track.frame);
		return;
	}
	const Fmatrix& D = vr_3b().vis_rot ? m_vr_draw_xform[idx] : item->m_item_transform;
	Fvector rw, fw, aw, rt, ft, at;
	D.transform_tiny(rw, K->LL_GetTransform(br).c);
	D.transform_tiny(fw, K->LL_GetTransform(bf).c);
	aw.sub(fw, rw);
	Fmatrix world_to_track;
	world_to_track.invert(g_vr_track.track_to_world);
	world_to_track.transform_tiny(rt, rw);
	world_to_track.transform_tiny(ft, fw);
	at.sub(ft, rt);
	if (aw.magnitude() < EPS || at.magnitude() < EPS)
	{
		Msg("* [vr3] frame %u gun_vis_axis track nan nan nan world nan nan nan rear %s front %s", g_vr_track.frame, S.rear, S.front);
	}
	else
	{
		aw.normalize();
		at.normalize();
		Msg("* [vr3] frame %u gun_vis_axis track %.5f %.5f %.5f world %.5f %.5f %.5f rear %s front %s", g_vr_track.frame,
			at.x, at.y, at.z, aw.x, aw.y, aw.z, S.rear, S.front);
	}
	Msg("* [vr3] frame %u gun_vis_pts track %.4f %.4f %.4f %.4f %.4f %.4f", g_vr_track.frame, rt.x, rt.y, rt.z, ft.x, ft.y, ft.z);
}

// ---------------------------------------------------------------------------------------------------------------------
// GammaVR milestone 3c (Amendment 11): per-weapon grip transform from gamedata/configs/gvr_grips.ltx ($game_config$).
//   [<section>]  family = ak|ar|pistol|shotgun|bolt (or family_xx)   grip_pos = x, y, z   grip_rot = pitch, yaw, roll
// grip_pos: metres, in the weapon item's MODEL space (the HUD model drawn with m_item_transform): placed on the controller.
// grip_rot (degrees) defines the grip frame's orientation in model space, Rframe = Ry(yaw) * Rx(pitch) * Rz(roll), applied
// to a vector as roll about z first, then pitch UP about x, then yaw RIGHT about y (engine axes x right, y up, z forward):
// Rframe * (0,0,1) = (sin yaw cos pitch, sin pitch, cos yaw cos pitch). That model-space direction is put on the
// controller's forward (z) axis. Calibration from a measured barrel direction b (model space, unit): pitch = asin(b.y),
// yaw = atan2(b.x, b.z), roll = 0. Fmatrix: Ry = rotateY(yaw), Rx = rotateX(-pitch), Rz = rotateZ(roll) (+x toward +y).
// Item world transform = grip pose * inverse(translate(grip_pos) * Rframe) (with the item's own scale kept).
// Lookup (or the -vr_grip_as section instead of the held one): the weapon's object section (e.g. wpn_sks) with grip_pos,
// else the HUD section (e.g. wpn_sks_hud, as gun_bones prints) with grip_pos, else the family_<f> section named by the
// first of those that has a 'family' line, else none (old behaviour). -vr_grip_off: never any offset.
struct vr_grip_cfg
{
	bool off = false;
	string128 as;
	string64 hand_bone;
	bool has_hand_bone = false;
	CInifile* ini = nullptr;
	vr_grip_cfg()
	{
		as[0] = hand_bone[0] = 0;
		off = !!strstr(Core.Params, "-vr_grip_off");
		if (LPCSTR p = strstr(Core.Params, "-vr_grip_as "))
			sscanf(p + 12, "%127s", as);
		if (LPCSTR p = strstr(Core.Params, "-vr_grip_bones "))
			has_hand_bone = sscanf(p + 15, "%63s", hand_bone) == 1;
		string_path path;
		FS.update_path(path, "$game_config$", "gvr_grips.ltx");
		if (FS.exist(path))
			ini = xr_new<CInifile>(path, TRUE, TRUE, FALSE);
		Msg("* [vr3] grip config %s: %s; -vr_grip_off %d, -vr_grip_as %s, -vr_grip_bones %s", path, ini ? "loaded" : "MISSING (no offsets)",
			off ? 1 : 0, as[0] ? as : "-", has_hand_bone ? hand_bone : "-");
	}
};
static const vr_grip_cfg& vr_grip_config()
{
	static const vr_grip_cfg c;
	return c;
}


// looks the held item up (once per change of section) and sets s_vr_grip_on / s_vr_grip_frame
static void vr_grip_lookup(attachable_hud_item* item)
{
	static shared_str s_hud_sect, s_obj_sect;
	const shared_str obj_sect = (item->m_parent_hud_item && item->m_parent_hud_item->has_object())
		? item->m_parent_hud_item->object().cNameSect() : shared_str("none");
	if (s_hud_sect == item->m_sect_name && s_obj_sect == obj_sect)
		return;
	s_hud_sect = item->m_sect_name;
	s_obj_sect = obj_sect;
	s_vr_grip_on = false;
	s_vr_grip_frame.identity();
	const vr_grip_cfg& C = vr_grip_config();
	Msg("* [vr3] grip lookup: weapon %s hud %s as %s", obj_sect.c_str(), item->m_sect_name.c_str(), C.as[0] ? C.as : "-");
	LPCSTR source = nullptr;
	if (!C.off && C.ini)
	{
		LPCSTR cand[2] = { C.as[0] ? C.as : obj_sect.c_str(), C.as[0] ? nullptr : item->m_sect_name.c_str() };
		LPCSTR fam_from = nullptr;
		for (int n = 0; n < 2 && !source; ++n)
		{
			if (!cand[n] || !C.ini->section_exist(cand[n]))
				continue;
			if (C.ini->line_exist(cand[n], "grip_pos"))
				source = cand[n];
			else if (!fam_from && C.ini->line_exist(cand[n], "family"))
				fam_from = cand[n];
		}
		static string64 fam_sect;
		if (!source && fam_from)
		{
			LPCSTR f = C.ini->r_string(fam_from, "family");
			if (strstr(f, "family_") == f)
				xr_strcpy(fam_sect, f);
			else
				xr_sprintf(fam_sect, "family_%s", f);
			if (C.ini->section_exist(fam_sect) && C.ini->line_exist(fam_sect, "grip_pos"))
				source = fam_sect;
		}
	}
	if (!source)
	{
		Msg("* [vr3] grip offset for %s: none", obj_sect.c_str());
		return;
	}
	const Fvector pos = C.ini->r_fvector3(source, "grip_pos");
	const Fvector rot = C.ini->line_exist(source, "grip_rot") ? C.ini->r_fvector3(source, "grip_rot") : Fvector().set(0.f, 0.f, 0.f);
	Fmatrix Ry, Rx, Rz, R;
	Ry.rotateY(deg2rad(rot.y));
	Rx.rotateX(-deg2rad(rot.x));
	Rz.rotateZ(deg2rad(rot.z));
	R.mul_43(Ry, Rx);
	R.mulB_43(Rz); // R = Ry * Rx * Rz: roll first, then pitch, then yaw
	s_vr_grip_frame.set(R);
	s_vr_grip_frame.c.set(pos);
	s_vr_grip_on = true;
	Msg("* [vr3] grip offset for %s: source %s pos %.4f %.4f %.4f rot %.3f %.3f %.3f", obj_sect.c_str(), source,
		pos.x, pos.y, pos.z, rot.x, rot.y, rot.z);
	Msg("* [vr3] grip frame (model space, rows i j k = frame x y z axes, Rframe*(0,0,1) = k): i %.5f %.5f %.5f j %.5f %.5f %.5f k %.5f %.5f %.5f",
		R.i.x, R.i.y, R.i.z, R.j.x, R.j.y, R.j.z, R.k.x, R.k.y, R.k.z);
}

// Milestone 3c logging: the -vr_grip_bones bone of the first-person HANDS model as drawn, the barrel bones in weapon model
// space. The arms are drawn as their own models with their own transforms (render_hud: add_Dynamic(m_model, &m_transform),
// add_Dynamic(m_model_2, &m_transform_2)); the bone's world position = m_transform * m_model bone (LL_GetTransform(b).c)
// (m_model_2 / m_transform_2 if the bone is only found there... both share the skeleton, so m_model is searched first).
// The weapon is attached to the arms (calc_transform: m_item_transform = m_transform * anchor bone * item offset), and the
// grip placement moves arms and weapon by the same rigid C, so the hand stays where it was relative to the weapon.
// Weapon model space = inverse(m_item_transform) (the unrotated item transform; -vr_vis_rot does not rotate the arms).
static bool vr_hand_bone_world(player_hud* H, Fvector& w)
{
	const vr_grip_cfg& C = vr_grip_config();
	if (!C.has_hand_bone)
		return false;
	IKinematics* K1 = H->m_model->dcast_PKinematics();
	u16 b = vr_find_bone(K1, C.hand_bone);
	if (b != BI_NONE)
	{
		H->m_transform.transform_tiny(w, K1->LL_GetTransform(b).c);
		return true;
	}
	IKinematics* K2 = H->m_model_2->dcast_PKinematics();
	b = vr_find_bone(K2, C.hand_bone);
	if (b != BI_NONE)
	{
		H->m_transform_2.transform_tiny(w, K2->LL_GetTransform(b).c);
		return true;
	}
	static bool s_warned = false;
	if (!s_warned)
	{
		s_warned = true;
		Msg("! [vr3] grip bone not found in the hands models: %s", C.hand_bone);
	}
	return false;
}

void player_hud::vr_log_grip_point()
{
	attachable_hud_item* item = m_attached_items[0] ? m_attached_items[0] : m_attached_items[1];
	if (!item)
		return;
	static bool s_hands_listed = false;
	if (!s_hands_listed)
	{
		s_hands_listed = true;
		IKinematics* K = m_model->dcast_PKinematics();
		const u16 n = K->LL_BoneCount();
		Msg("* [vr3] hand_bones count %u", (u32)n);
		for (u16 i = 0; i < n; ++i)
		{
			LPCSTR name = K->LL_BoneName_dbg(i);
			const u16 par = K->LL_GetData(i).GetParentID();
			LPCSTR pname = par == BI_NONE ? "none" : K->LL_BoneName_dbg(par);
			Msg("* [vr3] hand_bone %u name %s parent %s", (u32)i, name ? name : "?", pname ? pname : "?");
		}
	}
	Fmatrix world_to_model, world_to_track;
	world_to_model.invert(item->m_item_transform);
	world_to_track.invert(g_vr_track.track_to_world);
	const vr_grip_cfg& C = vr_grip_config();

	// frames 200/300, once per eye pass: grip point pixel (same matrix as gun_px)
	if (C.has_hand_bone && (g_vr_track.frame == 200 || g_vr_track.frame == 300))
	{
		static u32 s_px_frame[2] = { u32(-1), u32(-1) };
		const int eye = g_stereo_bench_eye;
		const int slot = eye == 1 ? 1 : 0;
		if (s_px_frame[slot] != g_vr_track.frame)
		{
			s_px_frame[slot] = g_vr_track.frame;
			Fvector w;
			float x, y;
			if (vr_hand_bone_world(this, w) && vr_hud_pixel(w, x, y))
				Msg("* [vr3] frame %u eye %d grip_px %.2f %.2f size %u %u", g_vr_track.frame, eye, x, y, Device.dwWidth, Device.dwHeight);
			else
				Msg("* [vr3] frame %u eye %d grip_px nan nan size %u %u", g_vr_track.frame, eye, Device.dwWidth, Device.dwHeight);
		}
	}

	static u32 s_logged_frame = u32(-1);
	if (g_vr_track.frame % 100 != 0 || s_logged_frame == g_vr_track.frame)
		return;
	s_logged_frame = g_vr_track.frame;
	if (C.has_hand_bone)
	{
		Fvector w, t, m;
		if (vr_hand_bone_world(this, w))
		{
			world_to_track.transform_tiny(t, w);
			world_to_model.transform_tiny(m, w);
			Msg("* [vr3] frame %u grip_pt track %.4f %.4f %.4f world %.4f %.4f %.4f model %.4f %.4f %.4f bone %s", g_vr_track.frame,
				t.x, t.y, t.z, w.x, w.y, w.z, m.x, m.y, m.z, C.hand_bone);
		}
		else
			Msg("* [vr3] frame %u grip_pt track nan nan nan world nan nan nan model nan nan nan bone %s", g_vr_track.frame, C.hand_bone);
	}
	const vr_3b_switches& S = vr_3b();
	if (S.barrel && item->m_model)
	{
		IKinematics* K = item->m_model;
		const u16 br = vr_find_bone(K, S.rear), bf = vr_find_bone(K, S.front);
		if (br == BI_NONE || bf == BI_NONE)
			Msg("* [vr3] frame %u barrel_model rear nan nan nan front nan nan nan", g_vr_track.frame);
		else
		{
			// bone positions in model space directly: m_item_transform * bone, mapped back by its inverse = the bone itself
			const Fvector& r = K->LL_GetTransform(br).c;
			const Fvector& f = K->LL_GetTransform(bf).c;
			Msg("* [vr3] frame %u barrel_model rear %.4f %.4f %.4f front %.4f %.4f %.4f", g_vr_track.frame, r.x, r.y, r.z, f.x, f.y, f.z);
		}
	}
}

// Milestone 3d (Amendment 14), -vr_dbg_grip <from> <to>: one line per VR frame in the window (first eye pass, after the
// placement): the current frame's grip location (time asked for, raw runtime pose, converted pose), the gun origin,
// the drawn grip point (-vr_grip_bones) and the drawn barrel line (-vr_barrel_bones) in tracking space. Logging only.
void player_hud::vr_log_grip_frame()
{
	static int s_from = -1, s_to = -1;
	static bool s_parsed = false;
	if (!s_parsed)
	{
		s_parsed = true;
		if (LPCSTR p = strstr(Core.Params, "-vr_dbg_grip "))
			if (sscanf(p + 13, "%d %d", &s_from, &s_to) != 2)
				s_from = s_to = -1;
	}
	if (s_from < 0 || int(g_vr_track.frame) < s_from || int(g_vr_track.frame) > s_to)
		return;
	static u32 s_logged = u32(-1);
	if (s_logged == g_vr_track.frame)
		return;
	attachable_hud_item* item = m_attached_items[0] ? m_attached_items[0] : m_attached_items[1];
	vr_grip_info G;
	if (!item || !g_vr_get_grip || !g_vr_get_grip(&G))
		return;
	s_logged = g_vr_track.frame;
	Fmatrix world_to_track;
	world_to_track.invert(g_vr_track.track_to_world);
	Fvector gt;
	world_to_track.transform_tiny(gt, item->m_item_transform.c);
	string64 gp_txt, bar_txt;
	Fvector hw;
	if (vr_hand_bone_world(this, hw))
	{
		Fvector ht;
		world_to_track.transform_tiny(ht, hw);
		xr_sprintf(gp_txt, "%.5f %.5f %.5f", ht.x, ht.y, ht.z);
	}
	else
		xr_strcpy(gp_txt, "nan nan nan");
	xr_strcpy(bar_txt, "nan nan nan");
	const vr_3b_switches& S = vr_3b();
	if (S.barrel && item->m_model)
	{
		IKinematics* K = item->m_model;
		const u16 br = vr_find_bone(K, S.rear), bf = vr_find_bone(K, S.front);
		if (br != BI_NONE && bf != BI_NONE)
		{
			const int idx = m_attached_items[0] ? 0 : 1;
			const Fmatrix& D = S.vis_rot ? m_vr_draw_xform[idx] : item->m_item_transform;
			Fvector rw, fw, rt, ft, a;
			D.transform_tiny(rw, K->LL_GetTransform(br).c);
			D.transform_tiny(fw, K->LL_GetTransform(bf).c);
			world_to_track.transform_tiny(rt, rw);
			world_to_track.transform_tiny(ft, fw);
			a.sub(ft, rt);
			if (a.magnitude() > EPS)
			{
				a.normalize();
				xr_sprintf(bar_txt, "%.6f %.6f %.6f", a.x, a.y, a.z);
			}
		}
	}
	const vr_pose& R = G.runtime;
	Msg("* [vr3grip] frame %u t_ns %lld hand %c valid %d grip_rt_pos %.5f %.5f %.5f grip_rt_quat %.6f %.6f %.6f %.6f xr_pos %.5f %.5f %.5f xr_quat %.6f %.6f %.6f %.6f gun_track %.5f %.5f %.5f grip_pt %s barrel %s lag %d",
		g_vr_track.frame, G.t_ns, G.hand, R.valid ? 1 : 0, R.pos.x, R.pos.y, R.pos.z, R.qx, R.qy, R.qz, R.qw,
		G.xr_pos[0], G.xr_pos[1], G.xr_pos[2], G.xr_quat[0], G.xr_quat[1], G.xr_quat[2], G.xr_quat[3],
		gt.x, gt.y, gt.z, gp_txt, bar_txt, G.lag);
}

bool player_hud::vr_place_at_grip()
{
	attachable_hud_item* item = m_attached_items[0] ? m_attached_items[0] : m_attached_items[1];
	vr_grip_info G;
	if (!item || !g_vr_get_grip || !g_vr_get_grip(&G) || !G.gun.valid)
		return false;

	Fmatrix gun_track, target;
	vr_pose_matrix(G.gun, gun_track);
	target.mul_43(g_vr_track.track_to_world, gun_track); // grip pose first, then tracking space -> world

	// keep the item's own scale (if its transform has one): only position and orientation are replaced
	const Fmatrix& cur = item->m_item_transform;
	const float si = cur.i.magnitude(), sj = cur.j.magnitude(), sk = cur.k.magnitude();
	if (si < EPS || sj < EPS || sk < EPS)
		return false;
	// milestone 3c: grip transform. Item = grip pose * inverse(grip frame), i.e. rotation = grip rotation * Rframe^T and
	// position such that the model point grip_pos lands on the grip position: c = grip.c - (scaled rotation) * grip_pos.
	vr_grip_lookup(item);
	if (s_vr_grip_on)
	{
		Fmatrix rot_target = target, frame_rot = s_vr_grip_frame, frame_rot_inv, R;
		rot_target.c.set(0.f, 0.f, 0.f);
		frame_rot.c.set(0.f, 0.f, 0.f);
		frame_rot_inv.transpose(frame_rot); // pure rotation: inverse = transpose (4x4 transpose keeps c = 0, w = 1)
		R.mul_43(rot_target, frame_rot_inv);
		R.i.mul(si);
		R.j.mul(sj);
		R.k.mul(sk);
		Fvector off;
		R.transform_dir(off, s_vr_grip_frame.c);
		R.c.sub(target.c, off);
		target.set(R);
	}
	else
	{
		target.i.mul(si);
		target.j.mul(sj);
		target.k.mul(sk);
	}

	Fmatrix cur_inv, C;
	cur_inv.invert(cur);
	C.mul_43(target, cur_inv); // C * cur = target

	m_transform.mulA_43(C);
	m_transform_2.mulA_43(C);
	for (int i = 0; i < 3; ++i)
		if (m_attached_items[i])
			m_attached_items[i]->m_item_transform.mulA_43(C);
	if (script_anim_item_model)
		m_item_pos.mulA_43(C);

	// attachments follow the moved transforms (same as the end of OnFrame())
	if (m_attached_items[0])
		m_attached_items[0]->update_attachments_xform();
	if (m_attached_items[1])
		m_attached_items[1]->update_attachments_xform();
	if (g_actor && m_model && m_model_2)
	{
		xr_map<shared_str, script_attachment*>& actor_hud_attachments = *g_actor->GetAttachments();
		for (auto& pair : actor_hud_attachments)
		{
			script_attachment* att = pair.second;
			if (att->GetType() == eSA_HUD)
			{
				bool m2 = att->GetParentBone() < 21;
				att->UpdateXFORM(m2 ? m_model_2->dcast_PKinematics() : m_model->dcast_PKinematics(), m2 ? &m_transform_2 : &m_transform);
			}
		}
	}

	// log once per logged frame: the gun origin in the world, and mapped back into the tracking space
	static u32 s_logged_frame = u32(-1);
	if (g_vr_track.frame % 100 == 0 && s_logged_frame != g_vr_track.frame)
	{
		s_logged_frame = g_vr_track.frame;
		const Fvector& w = item->m_item_transform.c;
		Fmatrix world_to_track;
		world_to_track.invert(g_vr_track.track_to_world);
		Fvector t;
		world_to_track.transform_tiny(t, w);
		Msg("* [vr3] frame %u gun_world pos %.4f %.4f %.4f", g_vr_track.frame, w.x, w.y, w.z);
		Msg("* [vr3] frame %u gun_track pos %.4f %.4f %.4f", g_vr_track.frame, t.x, t.y, t.z);
		// milestone 3b: barrel axis and muzzle, after the placement above (world and tracking space, engine axes)
		Fvector muzzle, axis;
		if (vr_gun_fire_world(item, muzzle, axis))
		{
			Fvector axis_t, muzzle_t;
			world_to_track.transform_dir(axis_t, axis);
			axis_t.normalize_safe();
			world_to_track.transform_tiny(muzzle_t, muzzle);
			Msg("* [vr3] frame %u gun_axis track %.5f %.5f %.5f world %.5f %.5f %.5f", g_vr_track.frame,
				axis_t.x, axis_t.y, axis_t.z, axis.x, axis.y, axis.z);
			Msg("* [vr3] frame %u gun_muzzle track %.4f %.4f %.4f world %.4f %.4f %.4f", g_vr_track.frame,
				muzzle_t.x, muzzle_t.y, muzzle_t.z, muzzle.x, muzzle.y, muzzle.z);
		}
		else
		{
			Msg("* [vr3] frame %u gun_axis track nan nan nan world nan nan nan", g_vr_track.frame);
			Msg("* [vr3] frame %u gun_muzzle track nan nan nan world nan nan nan", g_vr_track.frame);
		}
	}
	return true;
}

// GammaVR diagnostic (-vr_m3 with -vr_dbg_base <from> <to>): one line per VR frame in [from, to], logged once per frame
// (first eye pass), to find what moves the game camera that track_to_world is re-derived from every frame
// (IGame_Level.cpp: track_to_world = game camera * inverse(head start pose)). Logging only.
//  cam     = final game camera after the camera effectors (CCameraManager::Position/Direction, what Device got = pos0/dir0)
//  cambase = the actor's active camera before effectors (cam_Active()->Position())
//  t2w     = g_vr_track.track_to_world.c
//  effs    = active camera effectors: count, then type id, class name, 'H' if a HUD motion camera effector
// prints "x y z" with the given precision, or "nan nan nan" (portable, MSVC prints NaN as -nan(ind))
static void vr_dbg_vec(string64& out, const Fvector* v, int prec)
{
	if (v)
		xr_sprintf(out, "%.*f %.*f %.*f", prec, v->x, prec, v->y, prec, v->z);
	else
		xr_strcpy(out, "nan nan nan");
}

void vr_dbg_base_log(bool flat, u32 frame)
{
	static int s_from = -1, s_to = -1;
	static bool s_parsed = false;
	if (!s_parsed)
	{
		s_parsed = true;
		if (LPCSTR p = strstr(Core.Params, "-vr_dbg_base "))
			if (sscanf(p + 13, "%d %d", &s_from, &s_to) != 2)
				s_from = s_to = -1;
		Msg("* [vr3dbg] base diagnostic window %d %d (%s)", s_from, s_to, flat ? "FLAT, VR path off" : "VR path");
	}
	if (s_from < 0 || !g_actor || int(frame) < s_from || int(frame) > s_to)
		return;
	static u32 s_logged = u32(-1);
	if (s_logged == frame)
		return;
	s_logged = frame;
	CCameraManager& CM = g_actor->Cameras();
	const Fvector cp = CM.Position(), cd = CM.Direction();
	CCameraBase* cb = g_actor->cam_Active();
	const Fvector bp = cb ? cb->Position() : Fvector().set(0.f, 0.f, 0.f);
	const Fvector& ap = g_actor->Position();
	string1024 effs;
	effs[0] = 0;
	u32 n = 0;
	for (CEffectorCam* e : CM.vr_dbg_effectors())
	{
		if (!e)
			continue;
		++n;
		string256 one;
		string128 cls;
		xr_strcpy(cls, typeid(*e).name()); // MSVC: "class CEffectorBobbing": spaces become '_' so each entry is one token
		for (char* c = cls; *c; ++c)
			if (*c == ' ')
				*c = '_';
		xr_sprintf(one, " %d:%s%s", (int)e->GetType(), cls, e->IsHudMotionEffector() ? ":H" : "");
		if (xr_strlen(effs) + xr_strlen(one) < sizeof(effs) - 1)
			xr_strcat(effs, one);
	}
	u32 hud_st = u32(-1);
	LPCSTR motion = "none";
	int ammo = -1;
	if (CHudItem* hi = smart_cast<CHudItem*>(g_actor->inventory().ActiveItem()))
	{
		hud_st = hi->GetState();
		if (hi->m_current_motion.size())
			motion = hi->m_current_motion.c_str();
	}
	if (CWeapon* w = smart_cast<CWeapon*>(g_actor->inventory().ActiveItem()))
		ammo = w->GetAmmoElapsed();
	// VR-only fields (nan in a flat run): tracking origin t2w, the tracking-space point (0,0,2) in the world (p2),
	// the VR base position and yaw
	string64 t_txt, p2_txt, base_txt, yaw_txt;
	if (!flat)
	{
		Fvector p2;
		g_vr_track.track_to_world.transform_tiny(p2, Fvector().set(0.f, 0.f, 2.f));
		vr_dbg_vec(t_txt, &g_vr_track.track_to_world.c, 4);
		vr_dbg_vec(p2_txt, &p2, 4);
	}
	else
	{
		vr_dbg_vec(t_txt, nullptr, 4);
		vr_dbg_vec(p2_txt, nullptr, 4);
	}
	if (!flat && g_vr_base.valid && g_vr_base.yaw_set)
	{
		vr_dbg_vec(base_txt, &g_vr_base.pos, 4);
		xr_sprintf(yaw_txt, "%.4f", g_vr_base.yaw);
	}
	else
	{
		vr_dbg_vec(base_txt, nullptr, 4);
		xr_strcpy(yaw_txt, "nan");
	}
	// actor aim (first-eye camera direction, before effects): yaw = atan2(x, z), pitch = asin(y), radians
	const Fvector& ad = g_actor->cam_FirstEye()->vDirection;
	const float aim_yaw = atan2f(ad.x, ad.z), aim_pitch = asinf(_max(-1.f, _min(1.f, ad.y)));
	Msg("* [vr3dbg] frame %u cam %.4f %.4f %.4f dir %.5f %.5f %.5f cambase %.4f %.4f %.4f actor %.4f %.4f %.4f t2w %s mstate 0x%08x hud_state %u motion %s effs %u%s p2 %s base %s yaw %s aim %.5f %.5f ammo %d health %.4f lookout %.4f camh %.4f precol %.4f %.4f %.4f",
		frame, cp.x, cp.y, cp.z, cd.x, cd.y, cd.z, bp.x, bp.y, bp.z, ap.x, ap.y, ap.z, t_txt,
		g_actor->MovingState(), hud_st, motion, n, effs, p2_txt, base_txt, yaw_txt, aim_yaw, aim_pitch, ammo, g_actor->GetfHealth(),
		g_actor->m_fLookoutFactor, g_actor->CurrentHeight, g_vr_base.cam_point.x, g_vr_base.cam_point.y, g_vr_base.cam_point.z);
}

void player_hud::render_hud(IDSGraphManager* DM)
{
	if (g_vr_track.active)
		vr_dbg_base_log(false, g_vr_track.frame); // GammaVR diagnostic, only with -vr_dbg_base
	bool b_r0 = ((m_attached_items[0] && m_attached_items[0]->need_renderable()) || script_anim_part == 0 || script_anim_part == 2);
	bool b_r1 = ((m_attached_items[1] && m_attached_items[1]->need_renderable()) || script_anim_part == 1 || script_anim_part == 2);

	if (!b_r0 && !b_r1) return;

	// GammaVR milestone 3: in the eyes of a VR frame the held item is drawn at the controller grip pose
	bool vis_rot = false;
	if (g_vr_track.active)
	{
		if (vr_nogun())
			return;
		if (!vr_place_at_grip())
		{
			static bool s_warned = false;
			if (!s_warned)
			{
				s_warned = true;
				Msg("! [vr3] frame %u held item NOT placed at the grip (no item in hand 0/1, no grip pose, or bad scale)", g_vr_track.frame);
			}
		}
		else
		{
			vr_log_gun_pixels(); // milestone 3b: in every eye pass, with the item's final transform of this frame
			vis_rot = vr_update_vis_rot(); // milestone 3b test control: rotates only what is drawn
			vr_log_gun_bones();
			vr_log_grip_point(); // milestone 3c
			vr_log_grip_frame(); // milestone 3d, only with -vr_dbg_grip
		}
	}

	DM->add_Dynamic(m_model->dcast_RenderVisual(), &m_transform);
	DM->add_Dynamic(m_model_2->dcast_RenderVisual(), &m_transform_2);

	if (m_attached_items[0])
		m_attached_items[0]->render(DM, vis_rot ? &m_vr_draw_xform[0] : nullptr);

	if (m_attached_items[1])
		m_attached_items[1]->render(DM, vis_rot ? &m_vr_draw_xform[1] : nullptr);

	if (m_attached_items[SCOPE_ATTACH_IDX])
		m_attached_items[SCOPE_ATTACH_IDX]->render(DM, vis_rot ? &m_vr_draw_xform[SCOPE_ATTACH_IDX] : nullptr);

	if (script_anim_item_model)
	{
		DM->add_Dynamic(script_anim_item_model->dcast_RenderVisual(), &m_item_pos);
	}

	auto& actor_hud_attachments = *g_actor->GetAttachments();
	for (auto& pair : actor_hud_attachments)
		{
			script_attachment* att = pair.second;

			if (att->GetType() == eSA_HUD)
            att->renderable_Render(DM);
	}
}

#include "../xrEngine/motion.h"

u32 player_hud::motion_length_script(LPCSTR section, LPCSTR anm_name, float speed)
{
	if (!pSettings->section_exist(section))
	{
		Msg("!script motion section [%s] does not exist", section);
		return 0;
	}

	player_hud_motion_container* pm = get_hand_motions(section);
	if (!pm)
		return 0;

	player_hud_motion* phm = pm->find_motion(anm_name);
	if (!phm)
	{
		Msg("!script motion [%s] not found in section [%s]", anm_name, section);
		return 0;
	}

	const CMotionDef* temp;
	return motion_length(phm->m_animations[0].mid, temp, speed);
}

bool player_hud::motion_is_stop_at_end(const shared_str& anim_name, const shared_str& hud_name)
{
	player_hud_motion_container* pc = get_hand_motions(*hud_name);
	if (!pc) return true;
	player_hud_motion* pm = pc->find_motion(anim_name);
	if (!pm || !pm->m_animations.size())
		return true;

	CMotionDef* md = m_model->LL_GetMotionDef(pm->m_animations[0].mid);
	return md->flags & esmStopAtEnd;
}

u32 player_hud::motion_length(const shared_str& anim_name, const shared_str& hud_name, const CMotionDef*& md, u8 anim_idx, bool ignore_stop_at_end)
{
	player_hud_motion_container* pc = get_hand_motions(*hud_name);
	if (!pc) return 0;
	player_hud_motion* pm = pc->find_motion(anim_name);
	if (!pm || !pm->m_animations.size())
		return 100; // ms UNTEMPORARY

	return motion_length(pm->m_animations[anim_idx].mid, md, 1.f, ignore_stop_at_end);
}

u32 player_hud::motion_length(const MotionID& M, const CMotionDef*& md, float speed, bool ignore_stop_at_end)
{
	md = m_model->LL_GetMotionDef(M);
	VERIFY(md);
	if (md != nullptr && (ignore_stop_at_end || md->flags & esmStopAtEnd))
	{
		CMotion* motion = m_model->LL_GetRootMotion(M);
		return iFloor(0.5f + 1000.f * motion->GetLength() / (md->Dequantize(md->speed) * speed));
	}
	return 0;
}

const Fvector player_hud::attach_rot(u8 part) const
{
	if (m_attached_items[part])
		return m_attached_items[part]->hands_attach_rot();
	else if (m_attached_items[!part])
		return m_attached_items[!part]->hands_attach_rot();

	return { 0.f, 0.f, 0.f };
}

const Fvector player_hud::attach_pos(u8 part) const
{
	if (m_attached_items[part])
		return m_attached_items[part]->hands_attach_pos();
	else if (m_attached_items[!part])
		return m_attached_items[!part]->hands_attach_pos();

	return { 0.f, 0.f, 0.f };
}

#include "Inventory.h"
extern float g_freelook_z_offset;
extern float psHUD_FOV;

// Build an independent reference before inertia, recoil, script layers and near-wall offsets.
void player_hud::update_ik_shoulder_reference(const Fmatrix& camera)
{
	for (int slot = 0; slot < 2; ++slot)
	{
		const attachable_hud_item* item = m_attached_items[slot] ? m_attached_items[slot] : m_attached_items[!slot];
		Fmatrix reference = camera;
		if (item && smart_cast<CWeapon*>(item->m_parent_hud_item))
		{
			const Fvector& rotation = m_adjust_mode ? m_adjust_offset[1][5] : item->m_measures.m_hands_offset[1][5];
			const Fvector& position = m_adjust_mode ? m_adjust_offset[0][5] : item->m_measures.m_hands_offset[0][5];
			Fmatrix offset, axis;
			offset.rotateX(rotation.x);
			axis.rotateY(rotation.y);
			offset.mulA_43(axis);
			axis.rotateZ(rotation.z);
			offset.mulA_43(axis);
			offset.translate_over(position);
			reference.mulB_43(offset);
		}

		Fvector rotation = attach_rot(slot);
		rotation.mul(PI / 180.f);
		Fmatrix attachment;
		attachment.setHPB(rotation.x, rotation.y, rotation.z);
		attachment.translate_over(attach_pos(slot));
		// HUD slots are right/left; the IK array is left/right.
		m_ik_shoulder_reference[1 - slot].mul_43(reference, attachment);
	}
}

void player_hud::update(const Fmatrix& cam_trans)
{
	if (ps_hud_ik_compensation)
		update_ik_shoulder_reference(cam_trans);
	Fmatrix trans = cam_trans;
	Fmatrix trans_b = cam_trans;
	CWeapon* wep = smart_cast<CWeapon*>(Actor()->inventory().ActiveItem());

	float& control_factor = Actor()->freelook_cam_control;
	u8 cam_freelook = Actor()->cam_freelook;
	float sub_z = 0.f;

	if (control_factor > 0)
	{
		Fvector new_k;
		float old_pitch = trans.k.getP();
		float new_pitch = old_pitch > 0.f ? old_pitch * (1.f - psHUD_FOV) : old_pitch * (1.f - (psHUD_FOV / 2.f));
		float final_pitch = angle_lerp(old_pitch, new_pitch, control_factor);

		float body_yaw = -angle_normalize_signed(Actor()->old_torso_yaw);
		float cam_yaw = -angle_normalize_signed(Actor()->cam_FirstEye()->yaw);
		float diff_yaw = angle_difference_signed(body_yaw, cam_yaw);

		if (final_pitch < 0.f)
			sub_z += final_pitch * .35f;

		sub_z -= abs(diff_yaw) * .1f;
		clamp(sub_z, -.2f, 0.f);

		float new_yaw = trans.k.getH() + diff_yaw * psHUD_FOV;
		float final_yaw = angle_lerp(body_yaw, new_yaw, control_factor);

		new_k.setHP(final_yaw, final_pitch);
		trans.k.lerp(trans.k, new_k, control_factor);
		Fvector::generate_orthonormal_basis_normalized(trans.k, trans.j, trans.i);
	}

	if (cam_freelook == eflEnabling || cam_freelook == eflEnabled)
		control_factor += Device.fTimeDelta / .3f;
	else
		control_factor -= Device.fTimeDelta / .3f;

	clamp(control_factor, 0.f, 1.f);

	Fvector m1pos = attach_pos(0);
	Fvector m2pos = attach_pos(1);

	Fvector m1rot = attach_rot(0);
	Fvector m2rot = attach_rot(1);

	// freelook pitch compensation (avoid seeing missing arm parts or weapon parts that are not meant to be visible)
	if ((m_attached_items[0] || m_attached_items[1]) && control_factor > 0 && sub_z < 0)
	{
		float z1, z2, z_factor;
		z_factor = (sub_z * control_factor);
		z1 = (g_freelook_z_offset ? g_freelook_z_offset : m_attached_items[m_attached_items[0] ? 0 : 1]->m_measures.m_fFreelookZOffset);
		z2 = (g_freelook_z_offset ? g_freelook_z_offset : m_attached_items[m_attached_items[1] ? 1 : 0]->m_measures.m_fFreelookZOffset);

		if (Actor()->is_safemode())
		{
			z1 = fmaxf(z1, .5f);
			if (!m_attached_items[1])
				z2 = fmaxf(z2, .5f);
		}

		z1 *= z_factor;
		z2 *= z_factor;

		m1pos.z += z1;
		m2pos.z += z2;
		m1pos.y += z1 / 2.f;
		m2pos.y += z2 / 2.f;
	}

	Fmatrix trans_2 = trans;

	if (m_attached_items[0])
		m_attached_items[0]->m_parent_hud_item->UpdateHudAdditional(trans);

	if (m_attached_items[1])
	{
		m_attached_items[1]->m_parent_hud_item->UpdateHudAdditional(trans_2);
		if (wep && wep->IsZoomed())
			trans_2.mulB_43(wep->m_shoot_shake_mat);
	}

	if (m_attached_items[0] && !m_attached_items[1])
		trans_2 = trans;
	else if (m_attached_items[1] && !m_attached_items[0])
		trans = trans_2;

	// override hand offset for single hand animation
	if (script_anim_part == 2 || (script_anim_part && !m_attached_items[0] && !m_attached_items[1]))
	{
		m1pos = script_anim_offset[0];
		m2pos = script_anim_offset[0];
		m1rot = script_anim_offset[1];
		m2rot = script_anim_offset[1];
		trans = trans_b;
		trans_2 = trans_b;
	}
	else if (script_anim_offset_factor != 0.f)
	{
		Fvector& hand_pos = script_anim_part == 0 ? m1pos : m2pos;
		Fvector& hand_rot = script_anim_part == 0 ? m1rot : m2rot;

		hand_pos.lerp(script_anim_part == 0 ? m1pos : m2pos, script_anim_offset[0], script_anim_offset_factor);
		hand_rot.lerp(script_anim_part == 0 ? m1rot : m2rot, script_anim_offset[1], script_anim_offset_factor);

		if (script_anim_part == 0)
		{
			trans_b.inertion(trans, script_anim_offset_factor);
			trans = trans_b;
		}
		else
		{
			trans_b.inertion(trans_2, script_anim_offset_factor);
			trans_2 = trans_b;
		}
	}

	m1rot.mul(PI / 180.f);
	m_attach_offset.setHPB(m1rot.x, m1rot.y, m1rot.z);
	m_attach_offset.translate_over(m1pos);

	m2rot.mul(PI / 180.f);
	m_attach_offset_2.setHPB(m2rot.x, m2rot.y, m2rot.z);
	m_attach_offset_2.translate_over(m2pos);

	m_transform_prev = m_transform;
	m_transform.mul(trans, m_attach_offset);
	m_transform_2_prev = m_transform_2;
	m_transform_2.mul(trans_2, m_attach_offset_2);

	for (int i = 0; i < 8; ++i)
		m_ik_transforms[i].second = false;
	m_model->UpdateTracks();
	m_model->dcast_PKinematics()->CalculateBones_Invalidate();
	m_model->dcast_PKinematics()->CalculateBones(TRUE);

	m_model_2->UpdateTracks();
	m_model_2->dcast_PKinematics()->CalculateBones_Invalidate();
	m_model_2->dcast_PKinematics()->CalculateBones(TRUE);

	for (script_layer* anm : m_script_layers)
	{
		if (!anm || !anm->anm || (!anm->active && anm->blend_amount == 0.f))
			continue;

		if (anm->active)
			anm->blend_amount += Device.fTimeDelta / .4f;
		else
			anm->blend_amount -= Device.fTimeDelta / .4f;

		clamp(anm->blend_amount, 0.f, 1.f);

		if (anm->blend_amount > 0.f)
		{
			if (anm->anm->bLoop || anm->anm->anim_param().t_current < anm->anm->anim_param().max_t)
				anm->anm->Update(Device.fTimeDelta);
			else
				anm->Stop(false);
		}
		else
		{
			anm->Stop(true);
			continue;
		}

		Fmatrix blend = anm->XFORM();

		if (anm->m_part == 0 || anm->m_part == 2)
        {
			IKinematics* K = m_model->dcast_PKinematics();
			u16 bone_id = K ? K->LL_BoneID(anm->m_pivot_bone) : u16(-1);
			if (bone_id != u16(-1))
			{
				Fmatrix B = K->LL_GetTransform(bone_id);
				Fmatrix invB; invB.invert(B);
				Fmatrix tmp; tmp.mul_43(B, blend);
				tmp.mulB_43(invB);
				m_transform.mulB_43(tmp);
			}
			else
				m_transform.mulB_43(blend);
        }

		if (anm->m_part == 1 || anm->m_part == 2)
        {
			IKinematics* K = m_model_2->dcast_PKinematics();
			u16 bone_id = K ? K->LL_BoneID(anm->m_pivot_bone) : u16(-1);
			if (bone_id != u16(-1))
			{
				Fmatrix B = K->LL_GetTransform(bone_id);
				Fmatrix invB; invB.invert(B);
				Fmatrix tmp; tmp.mul_43(B, blend);
				tmp.mulB_43(invB);
				m_transform_2.mulB_43(tmp);
			}
			else
				m_transform_2.mulB_43(blend);
        }
	}

	bool need_blend[2];
	need_blend[0] = ((script_anim_part == 0 || script_anim_part == 2) || (m_attached_items[0] && m_attached_items[0]->m_parent_hud_item->NeedBlendAnm()));
	need_blend[1] = ((script_anim_part == 1 || script_anim_part == 2) || (m_attached_items[1] && m_attached_items[1]->m_parent_hud_item->NeedBlendAnm()));

	for (movement_layer* anm : m_movement_layers)
	{
		if (!anm || !anm->anm || (!anm->active && anm->blend_amount[0] == 0.f && anm->blend_amount[1] == 0.f))
			continue;

		if (anm->active && (need_blend[0] || need_blend[1]))
		{
			if (need_blend[0])
			{
				anm->blend_amount[0] += Device.fTimeDelta / .4f;

				if (!m_attached_items[1])
					anm->blend_amount[1] += Device.fTimeDelta / .4f;
				else if (!need_blend[1])
					anm->blend_amount[1] -= Device.fTimeDelta / .4f;
			}

			if (need_blend[1])
			{
				anm->blend_amount[1] += Device.fTimeDelta / .4f;

				if (!m_attached_items[0])
					anm->blend_amount[0] += Device.fTimeDelta / .4f;
				else if (!need_blend[0])
					anm->blend_amount[0] -= Device.fTimeDelta / .4f;
			}
		}
		else
		{
			anm->blend_amount[0] -= Device.fTimeDelta / .4f;
			anm->blend_amount[1] -= Device.fTimeDelta / .4f;
		}

		clamp(anm->blend_amount[0], 0.f, 1.f);
		clamp(anm->blend_amount[1], 0.f, 1.f);

		if (anm->blend_amount[0] == 0.f && anm->blend_amount[1] == 0.f)
		{
			anm->Stop(true);
			continue;
		}

		anm->anm->Update(Device.fTimeDelta);

		if (anm->blend_amount[0] == anm->blend_amount[1])
		{
			Fmatrix blend = anm->XFORM(0);
			m_transform.mulB_43(blend);
			m_transform_2.mulB_43(blend);
		}
		else
		{
			if (anm->blend_amount[0] > 0.f)
				m_transform.mulB_43(anm->XFORM(0));

			if (anm->blend_amount[1] > 0.f)
				m_transform_2.mulB_43(anm->XFORM(1));
		}
	}

	if (script_anim_lock_camera > 0.f) // lock_to_camera, animated arm(s) blend toward render camera without script and movement layers.
	{
		Fmatrix cam, locked;
		Actor()->Cameras().camera_Matrix(cam);
		if (script_anim_part == 0 || script_anim_part == 2)
			m_transform.inertion(locked.mul(cam, m_attach_offset), 1.f - script_anim_lock_camera);
		if (script_anim_part == 1 || script_anim_part == 2)
			m_transform_2.inertion(locked.mul(cam, m_attach_offset_2), 1.f - script_anim_lock_camera);
	}

	if (m_attached_items[0])
		m_attached_items[0]->update(true);

	if (m_attached_items[1])
		m_attached_items[1]->update(true);

	if (m_attached_items[SCOPE_ATTACH_IDX])
		m_attached_items[SCOPE_ATTACH_IDX]->update(true);

	if (script_anim_item_attached && script_anim_item_model)
		update_script_item();

	// single hand offset smoothing + syncing back to other hand animation on end
	if (script_anim_part != u8(-1))
	{
		script_anim_offset_factor += Device.fTimeDelta * 2.5f;
		float lock_step = script_anim_lock_camera_time > 0.f ? Device.fTimeDelta / script_anim_lock_camera_time : 1.f;
		script_anim_lock_camera += clampr(script_anim_lock_camera_target - script_anim_lock_camera, -lock_step, lock_step);

		if (m_bStopAtEndAnimIsRunning && Device.dwTimeGlobal >= script_anim_end)
			StopScriptAnim();
	}
	else
		script_anim_offset_factor -= Device.fTimeDelta * 5.f;

	clamp(script_anim_offset_factor, 0.f, 1.f);
}

void player_hud::updateMovementLayerState()
{
	CActor* pActor = Actor();

	if (!pActor)
		return;

	for (movement_layer* anm : m_movement_layers)
	{
		anm->Stop(false);
	}

	bool need_blend = (script_anim_part != u8(-1) || (m_attached_items[0] && m_attached_items[0]->m_parent_hud_item->NeedBlendAnm()) || (m_attached_items[1] && m_attached_items[1]->m_parent_hud_item->NeedBlendAnm()));

	if (need_blend)
	{
		CWeapon* wep = nullptr;

		if (m_attached_items[0] && m_attached_items[0]->m_parent_hud_item->has_object() && m_attached_items[0]->m_parent_hud_item->object().cast_weapon())
			wep = m_attached_items[0]->m_parent_hud_item->object().cast_weapon();

		if (wep && wep->IsZoomed()) {
			m_movement_layers[eAimIdle]->Play();
		}
		else {
			m_movement_layers[eIdle]->Play();
		}

		if (pActor->AnyMove())
		{
			CEntity::SEntityState state;
			pActor->g_State(state);

			if (wep && wep->IsZoomed()) {
				state.bCrouch ? m_movement_layers[eAimCrouch]->Play() : m_movement_layers[eAimWalk]->Play();
			}
			else if (state.bCrouch) {
				m_movement_layers[eCrouch]->Play();
			}
			else if (state.bSprint) {
				m_movement_layers[eSprint]->Play();
			}
			else if (!isActorAccelerated(pActor->MovingState(), false)) {
				m_movement_layers[eWalk]->Play();
			}
			else {
				m_movement_layers[eRun]->Play();
			}
		}
	}
}

void player_hud::PlayBlendAnm(LPCSTR name, u8 part, float speed, float power, bool bLooped, bool no_restart, LPCSTR pivot_bone)
{
	for (script_layer* anm : m_script_layers)
	{
		if (!xr_strcmp(*anm->m_name, name))
		{
			if (!no_restart)
			{
				anm->anm->Stop();
				anm->blend_amount = 0.f;
				anm->blend.identity();
			}

			if (!anm->anm->IsPlaying())
				anm->anm->Play(bLooped);

			anm->anm->bLoop = bLooped;
			anm->m_part = part;
			anm->anm->Speed() = speed;
			anm->m_power = power;
			anm->active = true;
            anm->m_pivot_bone = pivot_bone;
			return;
		}
	}

	script_layer* anm = xr_new<script_layer>(name, part, speed, power, bLooped, pivot_bone);
	m_script_layers.push_back(anm);
}

void player_hud::StopBlendAnm(LPCSTR name, bool bForce)
{
	for (script_layer* anm : m_script_layers)
	{
		if (!xr_strcmp(*anm->m_name, name))
		{
			anm->Stop(bForce);
			return;
		}
	}
}

void player_hud::StopAllBlendAnms(bool bForce)
{
	for (script_layer* anm : m_script_layers)
	{
		anm->Stop(bForce);
	}
}

float player_hud::SetBlendAnmTime(LPCSTR name, float time)
{
	for (script_layer* anm : m_script_layers)
	{
		if (!xr_strcmp(*anm->m_name, name))
		{
			if (!anm->anm->IsPlaying())
				return 0;

			float speed = (anm->anm->anim_param().max_t - anm->anm->anim_param().t_current) / time;
			anm->anm->Speed() = speed;
			return speed;
		}
	}

	return 0;
}

//0 = both, 1 = left, 2 = right
void play_blend(player_hud* hud, u8 pid, const MotionID& M, BOOL bMixIn, float speed, bool script_anim = false)
{
	switch (pid)
	{
	case 0:
	{
		if (!script_anim && hud->script_anim_part == 2) return;
		play_blend(hud, 1, M, bMixIn, speed);
		play_blend(hud, 2, M, bMixIn, speed);
		break;
	}
	case 1:
		if (!script_anim && hud->script_anim_part == 1) return;
		hud->m_model_2->PlayCycle(0, M, bMixIn, 0, 0, 0, speed);
		hud->m_model_2->PlayCycle(1, M, bMixIn, 0, 0, 0, speed);
		hud->m_model_2->PlayCycle(2, M, bMixIn, 0, 0, 0, speed);
		if (!hud->override_hand_pose[0] || g_player_hud->attached_item(1))
			hud->m_model_2->PlayCycle(3, M, bMixIn, 0, 0, 0, speed);
		hud->m_model_2->dcast_PKinematics()->CalculateBones_Invalidate();
		break;
	case 2:
		if (!script_anim && hud->script_anim_part == 0) return;
		hud->m_model->PlayCycle(0, M, bMixIn, 0, 0, 0, speed);
		hud->m_model->PlayCycle(2, M, bMixIn, 0, 0, 0, speed);
		if (!hud->override_hand_pose[1] || g_player_hud->attached_item(1))
			hud->m_model->PlayCycle(4, M, bMixIn, 0, 0, 0, speed);
		hud->m_model->dcast_PKinematics()->CalculateBones_Invalidate();
		break;
	}
}

void player_hud::create_script_anim_item_model(LPCSTR section)
{
	::Render->hud_loading = true;
	script_anim_item_model = ::Render->model_Create(pSettings->r_string(section, "item_visual"))->dcast_PKinematicsAnimated();
	::Render->hud_loading = false;

	::luabind::functor<void> funct;
	if (ai().script_engine().functor("_G.player_hud__OnCreateHudMotionItem", funct))
		funct(section);
}

void player_hud::delete_script_anim_item_model()
{
	if (!script_anim_item_model)
		return;

	IRenderVisual* v = script_anim_item_model->dcast_RenderVisual();
	::Render->model_Delete(v);
	script_anim_item_model = nullptr;
}

// 0 = right hand, 1 = left hand, 2 = both
void player_hud::set_hand_pose(u8 hand, LPCSTR sect, LPCSTR anm, float accrue, float falloff, bool mixin)
{
	if (!pSettings->section_exist(sect))
	{
		Msg("!script motion section [%s] does not exist", sect);
		return;
	}

	player_hud_motion_container* pm = get_hand_motions(sect);
	player_hud_motion* phm = pm->find_motion(anm);

	if (!phm)
	{
		Msg("!script motion [%s] not found in section [%s]", anm, sect);
		return;
	}

	const motion_descr& M = phm->m_animations[Random.randI(phm->m_animations.size())];

	switch (hand)
	{
	case 0:
	{
		if (falloff) set_current_blend_falloff(4, falloff);
		CBlend* B = m_model->PlayCycle(4, M.mid, mixin);
		if (accrue)
			B->blendAccrue = accrue;
		override_hand_pose[1] = true;
	}
	break;
	case 1:
	{
		if (falloff) set_current_blend_falloff(3, falloff);
		CBlend* B = m_model_2->PlayCycle(3, M.mid, mixin);
		if (accrue)
			B->blendAccrue = accrue;
		override_hand_pose[0] = true;
	}
	break;
	case 2:
	{
		if (falloff) set_current_blend_falloff(4, falloff);
		CBlend* B = m_model->PlayCycle(4, M.mid, mixin);
		if (accrue)
			B->blendAccrue = accrue;
		override_hand_pose[1] = true;

		if (falloff) set_current_blend_falloff(3, falloff);
		B = m_model_2->PlayCycle(3, M.mid, mixin);
		if (accrue)
			B->blendAccrue = accrue;
		override_hand_pose[0] = true;
	}
	break;
	}
}

// 0 = right hand, 1 = left hand, 2 = both
void player_hud::clear_hand_pose(u8 hand, float accrue, float falloff)
{
	// Clearing an already released pose must not enqueue another set of blends.
	if ((hand == 0 || hand == 2) && override_hand_pose[1])
	{
		override_hand_pose[1] = false;
		re_sync_hand(false, accrue, falloff);
	}
	if ((hand == 1 || hand == 2) && override_hand_pose[0])
	{
		override_hand_pose[0] = false;
		re_sync_hand(true, accrue, falloff);
	}
}

extern BOOL print_bone_warnings;
void player_hud::StopScriptAnim()
{
	u8 part = script_anim_part;
	script_anim_part = u8(-1);
	delete_script_anim_item_model();
	script_anim_lead_gun = false;
	script_anim_lock_camera = 0.f;

	updateMovementLayerState();

    if (part > 2)
    {
        if (print_bone_warnings)
        {
            Msg("![player_hud::StopScriptAnim()] invalid script_anim_part %d, must be < 3", part);
            ai().script_engine().print_stack();
        }
    }
        
	if (part < 2 && !m_attached_items[part])
		re_sync_anim(part + 1);
	else
		OnMovementChanged((ACTOR_DEFS::EMoveCommand)0);
}

//part: 0 = right arm; 1 = left arm
u32 player_hud::anim_play(u16 part, const MotionID& M, BOOL bMixIn, const CMotionDef*& md, float speed, u16 override_part)
{
	u16 part_id = u16(-1);
	if (attached_item(0) && attached_item(1))
		part_id = ((part == 0) ? 2 : 1);
	else
		part_id = 0;

	if (override_part != u16(-1))
		part_id = override_part;

	play_blend(this, part_id, M, bMixIn, speed);

	return motion_length(M, md, speed);
}

player_hud_motion_container* player_hud::get_hand_motions(LPCSTR section)
{
	for (hand_motions* phm : m_hand_motions)
	{
		if (phm->section == section)
			return &phm->pm;
	}

	hand_motions* res = xr_new<hand_motions>();
	res->section = section;
	res->pm.load(m_model, section);
	m_hand_motions.push_back(res);

	return &res->pm;
}

void player_hud::update_script_item()
{
	Fvector ypr = item_pos[1];
	ypr.mul(PI / 180.f);
	m_attach_offset.setHPB(ypr.x, ypr.y, ypr.z);
	m_attach_offset.translate_over(item_pos[0]);

	calc_transform(m_attach_idx, m_attach_offset, m_item_pos, script_anim_lead_gun);

	if (script_anim_item_model)
	{
		script_anim_item_model->UpdateTracks();
		script_anim_item_model->dcast_PKinematics()->CalculateBones_Invalidate();
		script_anim_item_model->dcast_PKinematics()->CalculateBones(TRUE);
	}
}

// 0 = right arm + hand, 1 = left arm + hand, 2 = both
u32 player_hud::script_anim_play(u8 hand, LPCSTR section, LPCSTR anm_name, bool bMixIn, float speed)
{
	if (!pSettings->section_exist(section))
	{
		Msg("!script motion section [%s] does not exist", section);
		m_bStopAtEndAnimIsRunning = true;
		script_anim_end = Device.dwTimeGlobal;

		return 0;
	}

	xr_string pos = "hands_position";
	xr_string rot = "hands_orientation";

	if (UI().is_widescreen())
	{
		pos.append("_16x9");
		rot.append("_16x9");
	}

	Fvector def = { 0.f, 0.f, 0.f };
	Fvector offs = READ_IF_EXISTS(pSettings, r_fvector3, section, pos.c_str(), def);
	Fvector rrot = READ_IF_EXISTS(pSettings, r_fvector3, section, rot.c_str(), def);

	if (pSettings->line_exist(section, "item_visual"))
	{
		delete_script_anim_item_model();
		create_script_anim_item_model(section);

		item_pos[0] = READ_IF_EXISTS(pSettings, r_fvector3, section, "item_position", def);
		item_pos[1] = READ_IF_EXISTS(pSettings, r_fvector3, section, "item_orientation", def);
		script_anim_item_attached = READ_IF_EXISTS(pSettings, r_bool, section, "item_attached", true);
		m_attach_idx = READ_IF_EXISTS(pSettings, r_u8, section, "attach_place_idx", 0);
		script_anim_lead_gun = READ_IF_EXISTS(pSettings, r_bool, section, "lh_lead_gun", false);

		if (!script_anim_item_attached)
		{
			Fmatrix attach_offs;
			Fvector ypr = item_pos[1];
			ypr.mul(PI / 180.f);
			attach_offs.setHPB(ypr.x, ypr.y, ypr.z);
			attach_offs.translate_over(item_pos[0]);
			m_item_pos = attach_offs;
		}
	}

	script_anim_offset[0] = offs;
	script_anim_offset[1] = rrot;
	script_anim_lock_camera_target = clampr(READ_IF_EXISTS(pSettings, r_float, section, "lock_to_camera", 0.f), 0.f, 1.f); // lock_to_camera target strength, 0 = normal hud, 1 = locked to render
	script_anim_lock_camera_time = READ_IF_EXISTS(pSettings, r_float, section, "lock_to_camera_time", 0.f); // seconds for a 0-1 ramp, starting from 0.
	script_anim_lock_camera = script_anim_lock_camera_time > 0.f ? 0.f : script_anim_lock_camera_target;
	if (script_anim_lock_camera > 0.f)
		script_anim_offset_factor = 1.f;
	script_anim_part = hand;

	player_hud_motion_container* pm = get_hand_motions(section);
	player_hud_motion* phm = pm->find_motion(anm_name);

	if (!phm)
	{
		Msg("!script motion [%s] not found in section [%s]", anm_name, section);
		m_bStopAtEndAnimIsRunning = true;
		script_anim_end = Device.dwTimeGlobal;

		return 0;
	}

	const motion_descr& M = phm->m_animations[Random.randI(phm->m_animations.size())];

	if (script_anim_item_model)
	{
		shared_str item_anm_name;
		if (phm->m_base_name != phm->m_additional_name)
			item_anm_name = phm->m_additional_name;
		else
			item_anm_name = M.name;

		MotionID M2 = script_anim_item_model->ID_Cycle_Safe(item_anm_name);
		if (!M2.valid())
			M2 = script_anim_item_model->ID_Cycle_Safe("idle");

		R_ASSERT3(M2.valid(), "model %s has no motion [idle] ", pSettings->r_string(m_sect_name, "item_visual"));

		u16 root_id = script_anim_item_model->dcast_PKinematics()->LL_GetBoneRoot();
		CBoneInstance& root_binst = script_anim_item_model->dcast_PKinematics()->LL_GetBoneInstance(root_id);
		root_binst.set_callback_overwrite(TRUE);
		root_binst.mTransform.identity();

		u16 pc = script_anim_item_model->partitions().count();
		for (u16 pid = 0; pid < pc; ++pid)
			CBlend* B = script_anim_item_model->PlayCycle(pid, M2, bMixIn, 0, 0, 0, speed);

		script_anim_item_model->dcast_PKinematics()->CalculateBones_Invalidate();
	}

	play_blend(this, (hand == 2 ? 0 : hand == 0 ? 2 : 1), M.mid, bMixIn, speed, true);

	const CMotionDef* md;
	u32 length = motion_length(M.mid, md, speed);

	if (length > 0)
	{
		m_bStopAtEndAnimIsRunning = true;
		script_anim_end = Device.dwTimeGlobal + length;
	}
	else
		m_bStopAtEndAnimIsRunning = false;

	updateMovementLayerState();

	return length;
}

bool player_hud::allow_activation(CHudItem* item)
{
	if (script_anim_part != u8(-1))
		return false;
	else if (m_attached_items[1])
		return m_attached_items[1]->m_parent_hud_item->CheckCompatibility(item);
	else
		return true;
}

shared_str current_player_hud_sect;
void player_hud::attach_item(CHudItem* item)
{
	attachable_hud_item* pi = item->HudItemData();
	int item_idx = pi->m_attach_place_idx;

	if (m_attached_items[item_idx] != pi)
	{
		if (m_attached_items[item_idx])
			m_attached_items[item_idx]->m_parent_hud_item->on_b_hud_detach();

		m_attached_items[item_idx] = pi;

		if (item_idx == 0 && m_attached_items[1])
			m_attached_items[1]->m_parent_hud_item->CheckCompatibility(item);

		item->on_a_hud_attach();

		updateMovementLayerState();
	}
}

//sync anim of other part to selected part (1 = sync to left hand anim; 2 = sync to right hand anim)
void player_hud::re_sync_anim(u8 part)
{
	u32 bc = part == 1 ? m_model_2->LL_PartBlendsCount(part) : m_model->LL_PartBlendsCount(part);
	for (u32 bidx = 0; bidx < bc; ++bidx)
	{
		CBlend* BR = part == 1 ? m_model_2->LL_PartBlend(part, bidx) : m_model->LL_PartBlend(part, bidx);
		if (!BR)
			continue;

		MotionID M = BR->motionID;

		u16 pc = m_model->partitions().count(); //same on both armatures
		for (u16 pid = 0; pid < pc; ++pid)
		{
			if (pid == 0)
			{
				CBlend* B = m_model->PlayCycle(0, M, TRUE);
				B->timeCurrent = BR->timeCurrent;
				B->speed = BR->speed;
				B = m_model_2->PlayCycle(0, M, TRUE);
				B->timeCurrent = BR->timeCurrent;
				B->speed = BR->speed;
			}
			else if (pid != part)
			{
				CBlend* B = part == 1 ? m_model->PlayCycle(pid, M, TRUE) : m_model_2->PlayCycle(pid, M, TRUE);
				B->timeCurrent = BR->timeCurrent;
				B->speed = BR->speed;
			}
		}
	}
}

void player_hud::set_current_blend_falloff(u8 part, float falloff)
{
	IKinematicsAnimated* target = (part == 1 || part == 3) ? m_model_2 : m_model;

	if (falloff)
	{
		u32 bc = target->LL_PartBlendsCount(part);
		for (u32 bidx = 0; bidx < bc; ++bidx)
		{
			CBlend* BR = target->LL_PartBlend(part, bidx);
			if (!BR)
				continue;

			BR->blendFalloff = falloff;
		}
	}
}

//sync hand animation back to the animation currently playing on that arm
void player_hud::re_sync_hand(bool left, float accrue, float falloff)
{
	IKinematicsAnimated* target = left ? m_model_2 : m_model;

	if (falloff) set_current_blend_falloff(left ? 3 : 4, falloff);

	// PlayCycle may update tracks and retire source blends; snapshot before creating any.
	struct SourceBlend
	{
		MotionID motion;
		float time;
		float speed;
	};
	xr_vector<SourceBlend> sources;
	const u32 count = target->LL_PartBlendsCount(left ? 1 : 2);
	for (u32 i = 0; i < count; ++i)
	{
		const CBlend* blend = target->LL_PartBlend(left ? 1 : 2, i);
		if (blend && blend->motionID.valid())
			sources.push_back({blend->motionID, blend->timeCurrent, blend->speed});
	}
	for (const SourceBlend& source : sources)
	{
		CBlend* blend = target->PlayCycle(left ? 3 : 4, source.motion, TRUE);
		if (!blend)
			continue;
		blend->timeCurrent = source.time;
		blend->speed = source.speed;
		if (accrue)
			blend->blendAccrue = accrue;
	}
}

//set cycle time (0...1) part: 0 = root; 1 = left hand; 2 = right hand
void player_hud::set_part_cycle_time(u8 part, float time, float set_time)
{
	if (part == 0)
	{
		IKinematicsAnimated* target = m_attached_items[0] && m_attached_items[0]->m_model
			? m_attached_items[0]->m_model->dcast_PKinematicsAnimated() : nullptr;
		if (target)
		{
			const u32 count = target->LL_PartBlendsCount(0);
			for (u32 i = 0; i < count; ++i)
			{
				CBlend* blend = target->LL_PartBlend(0, i);
				if (blend)
					blend->timeCurrent = set_time ? set_time : blend->timeTotal * time;
			}
		}

		set_part_cycle_time(1, time, set_time);
		set_part_cycle_time(2, time, set_time);
		return;
	}

	IKinematicsAnimated* target = part == 1 ? m_model_2 : m_model;

	u32 bc = target->LL_PartBlendsCount(part);
	for (u32 bidx = 0; bidx < bc; ++bidx)
	{
		CBlend* BR = target->LL_PartBlend(part, bidx);
		if (!BR)
			continue;

		if (set_time)
			BR->timeCurrent = set_time;
		else
			BR->timeCurrent = BR->timeTotal * time;
	}

	bc = target->LL_PartBlendsCount(part == 1 ? 3 : 4);
	for (u32 bidx = 0; bidx < bc; ++bidx)
	{
		CBlend* BR = target->LL_PartBlend(part == 1 ? 3 : 4, bidx);
		if (!BR)
			continue;

		if (set_time)
			BR->timeCurrent = set_time;
		else
			BR->timeCurrent = BR->timeTotal * time;
	}
}

//part: 0 = root; 1 = left hand; 2 = right hand
void player_hud::set_part_cycle_speed(u8 part, float speed)
{
	if (part == 0)
	{
		set_part_cycle_speed(1, speed);
		set_part_cycle_speed(2, speed);
	}
	u32 bc = part == 1 ? m_model_2->LL_PartBlendsCount(part) : m_model->LL_PartBlendsCount(part);
	for (u32 bidx = 0; bidx < bc; ++bidx)
	{
		CBlend* BR = part == 1 ? m_model_2->LL_PartBlend(part, bidx) : m_model->LL_PartBlend(part, bidx);
		if (!BR)
			continue;

		BR->speed = speed;
	}
}

void player_hud::detach_item_idx(u16 idx)
{
	if (NULL == m_attached_items[idx]) return;

	m_attached_items[idx]->m_parent_hud_item->on_b_hud_detach();
	m_attached_items[idx] = NULL;

	if (idx == 1)
	{
		if (m_attached_items[0])
			re_sync_anim(2);
		else
		{
			m_model_2->PlayCycle("hand_idle_doun");
		}
	}
	else if (idx == 0)
	{
		if (m_attached_items[1])
		{
			//fix for a rare case where the right hand stays visible on screen after detaching the right hand's attached item
			player_hud_motion* pm = m_attached_items[1]->m_hand_motions->find_motion("anm_idle");
			const motion_descr& M = pm->m_animations[0];
			m_model->PlayCycle(0, M.mid, false);
			m_model->PlayCycle(2, M.mid, false);
		}
		else
		{
			m_model->PlayCycle("hand_idle_doun");
		}
	}

	if (!m_attached_items[0] && !m_attached_items[1])
	{
		m_model->PlayCycle("hand_idle_doun");
		m_model_2->PlayCycle("hand_idle_doun");
	}
}

void player_hud::detach_item(CHudItem* item)
{
	if (!item->IsAttachedToHUD()) return;
	u16 item_idx = item->HudItemData()->m_attach_place_idx;

	if (m_attached_items[item_idx] == item->HudItemData())
	{
		detach_item_idx(item_idx);
	}
}

bool player_hud::allow_script_anim()
{
	if (m_attached_items[0] && (m_attached_items[0]->m_parent_hud_item->IsPending() || m_attached_items[0]->m_parent_hud_item->GetState() == CHudItem::EHudStates::eBore))
		return false;
	else if (m_attached_items[1] && (m_attached_items[1]->m_parent_hud_item->IsPending() || m_attached_items[1]->m_parent_hud_item->GetState() == CHudItem::EHudStates::eBore))
		return false;
	else if (script_anim_part != u8(-1))
		return false;

	return true;
}

void player_hud::calc_transform(u16 attach_slot_idx, const Fmatrix& offset, Fmatrix& result, bool leadGun)
{
	IKinematics* kin = (attach_slot_idx == 0) ? m_model->dcast_PKinematics() : m_model_2->dcast_PKinematics();
	Fmatrix ancor_m = kin->LL_GetTransform(m_ancors[(leadGun ? 0 : attach_slot_idx)]);
	result.mul((attach_slot_idx == 0) ? m_transform : m_transform_2, ancor_m);
	result.mulB_43(offset);
}

bool player_hud::inertion_allowed()
{
	attachable_hud_item* hi = m_attached_items[0];
	if (hi)
	{
		bool res = (hi->m_parent_hud_item->HudInertionEnabled() && hi->m_parent_hud_item->HudInertionAllowed());
		return res;
	}
	return true;
}

void player_hud::OnMovementChanged(ACTOR_DEFS::EMoveCommand cmd)
{
	if (cmd == 0)
	{
		if (m_attached_items[0])
		{
			if (m_attached_items[0]->m_parent_hud_item->GetState() == CHUDState::eIdle)
				m_attached_items[0]->m_parent_hud_item->PlayAnimIdle();
		}
		if (m_attached_items[1])
		{
			if (m_attached_items[1]->m_parent_hud_item->GetState() == CHUDState::eIdle)
				m_attached_items[1]->m_parent_hud_item->PlayAnimIdle();
		}
		if (m_attached_items[SCOPE_ATTACH_IDX])
		{
			if (m_attached_items[SCOPE_ATTACH_IDX]->m_parent_hud_item->GetState() == CHUDState::eIdle)
				m_attached_items[SCOPE_ATTACH_IDX]->m_parent_hud_item->PlayAnimIdle();
		}
	}
	else
	{
		if (m_attached_items[0])
			m_attached_items[0]->m_parent_hud_item->OnMovementChanged(cmd);

		if (m_attached_items[1])
			m_attached_items[1]->m_parent_hud_item->OnMovementChanged(cmd);

		if (m_attached_items[SCOPE_ATTACH_IDX])
			m_attached_items[SCOPE_ATTACH_IDX]->m_parent_hud_item->OnMovementChanged(cmd);
	}

	::luabind::functor<void> func;
	if (ai().script_engine().functor("_g.player_hud__OnMovementChanged", func))
	{
		func(cmd);
	}

	updateMovementLayerState();
}

bool nearwall_callback(int target, float ofs, const Fvector& dir, Fmatrix& mat)
{
	::luabind::functor<void> on_nearwall;
	if (!ai().script_engine().functor("_G.CActorHudOnNearWall", on_nearwall))
	{
		return false;
	}

	::luabind::object table = ::luabind::newtable(ai().script_engine().lua());
	table["target"] = target;
	table["offset"] = ofs;
	table["direction"] = dir;
	table["matrix"] = mat;
	table["override"] = false;
	on_nearwall(table);
	mat = ::luabind::object_cast<Fmatrix>(table["matrix"]);
	return ::luabind::object_cast<bool>(table["override"]);
}

void update_nearwall(int target, const attachable_hud_item* item, Fmatrix& nearwall)
{
	CHudItem* parent = item->m_parent_hud_item;
	const SPickParam& pp = parent->GetPick();
	float ofs = parent->GetNearWallOffset();
	nearwall = Fmatrix().identity();
	Fvector dir = Fvector().mul(pp.barrel_matrix.k, -1);
	Device.mView.transform_dir(dir);

	if (!nearwall_callback(target, ofs, dir, nearwall))
	{
		dir.mul(ofs);
		nearwall.translate_add(dir);
	}
}

void apply_nearwall(Fmatrix& transform, Fmatrix& nearwall, attachable_hud_item* item)
{
	transform.mulB_43(nearwall);

	Fmatrix mInv = transform;
	mInv.invert();

	Fmatrix delta = mInv;
	delta.mulB_43(item->m_item_transform);
	Fmatrix deltaInv = delta;
	deltaInv.invert();

	nearwall.mulA_43(deltaInv);
	nearwall.mulB_43(delta);
	item->m_item_transform.mulB_43(nearwall);
	item->m_parent_hud_item->UpdatePick();
}

void player_hud::OnFrame()
{
	// If we have a main-hand item...
	if (m_attached_items[0])
		// Delegate control
		m_attached_items[0]->m_parent_hud_item->OnFrame();

	// If we have an off-hand item...
	if (m_attached_items[1])
		// Delegate control
		m_attached_items[1]->m_parent_hud_item->OnFrame();

	// If near-wall is in position mode...
	if (g_nearwall == NW_POS)
	{
		Fmatrix nearwall_0;

		// If we have a main-hand item...
		if (m_attached_items[0])
		{
			update_nearwall(1, m_attached_items[0], nearwall_0);

			// If we have no off-hand item
			if (!m_attached_items[1])
			{
				// Apply the main hand item's nearwall to the left arm
				m_transform_2.mulB_43(nearwall_0);
			}

			apply_nearwall(m_transform, nearwall_0, m_attached_items[0]);
		}

		// If we have an off-hand item...
		if (m_attached_items[1])
		{
			Fmatrix nearwall_1 = Fmatrix().identity();
			update_nearwall(2, m_attached_items[1], nearwall_1);

			// If we have a main-hand item...
			if (m_attached_items[0])
			{
				// And it is a weapon...
				CWeapon* pWeapon = smart_cast<CWeapon*>(m_attached_items[0]->m_parent_hud_item);
				if (pWeapon)
				{
					// Interpolate between main and off -hand transforms based on aim factor
					float fac = pWeapon->GetZRotatingFactor();
					nearwall_1.i.lerp(nearwall_1.i, nearwall_0.i, fac);
					nearwall_1.i.normalize();
					nearwall_1.j.lerp(nearwall_1.j, nearwall_0.j, fac);
					nearwall_1.j.normalize();
					nearwall_1.k.lerp(nearwall_1.k, nearwall_0.k, fac);
					nearwall_1.k.normalize();
					nearwall_1.c.lerp(nearwall_1.c, nearwall_0.c, fac);
				}
			}

			// Apply nearwall to left arm
			apply_nearwall(m_transform_2, nearwall_1, m_attached_items[1]);
		}

		if (m_attached_items[SCOPE_ATTACH_IDX])
		{
			CHudItem* parent = m_attached_items[SCOPE_ATTACH_IDX]->m_parent_hud_item;
			m_attached_items[SCOPE_ATTACH_IDX]->m_item_transform.mulB_43(nearwall_0);
		}
	}

	if (g_actor && g_actor->IsFocused() && m_model && m_model_2)
	{
        // Update weapon transforms
		if (m_attached_items[0])
			m_attached_items[0]->update_attachments_xform();

		if (m_attached_items[1])
			m_attached_items[1]->update_attachments_xform();

        // Call Lua to update targets and solve IK
		finalize_hands_ik();

        // Update hud attachments transforms
		xr_map<shared_str, script_attachment*>& actor_hud_attachments = *g_actor->GetAttachments();
		for (auto& pair : actor_hud_attachments)
		{
			script_attachment* att = pair.second;
			if (att->GetType() == eSA_HUD)
			{
				bool m2 = att->GetParentBone() < 21;
				att->UpdateXFORM(m2 ? m_model_2->dcast_PKinematics() : m_model->dcast_PKinematics(), m2 ? &m_transform_2 : &m_transform);
			}
		}
	}
}

void player_hud::net_Relcase(CObject* obj)
{
	if (m_attached_items[0])
		m_attached_items[0]->m_parent_hud_item->net_Relcase(obj);

	if (m_attached_items[1])
		m_attached_items[1]->m_parent_hud_item->net_Relcase(obj);

	if (m_attached_items[SCOPE_ATTACH_IDX])
		m_attached_items[SCOPE_ATTACH_IDX]->m_parent_hud_item->net_Relcase(obj);
}
