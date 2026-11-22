#include "stdafx.h"
#include "player_hud.h"
#include "Level.h"
#include "HudItem.h"
#include "ui_base.h"
#include "UICursor.h"
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
#include "../xrEngine/xr_ioconsole.h"
#include "UIGameCustom.h"
#include "xr_level_controller.h"
#include "CharacterPhysicsSupport.h" // GammaVR milestone 7b: body box index ([vr3act])
#include "PHMovementControl.h"
#include "WeaponMagazined.h" // GammaVR milestone 8a: manual reload
#include "WeaponMagazinedWGrenade.h"
#include "WeaponShotgun.h"
#include "WeaponPistol.h"
#include "WeaponRPG7.h"
#include "WeaponRG6.h"
#include "WeaponAmmo.h" // GammaVR milestone 8b
#include "PDA.h" // GammaVR milestone 10a
#include "Actor_Flags.h"
#include "ui/UIPdaWnd.h"
#include "ui/UITabControl.h" // GammaVR milestone 10a step 3: tab rects for the log
#include "ui/UITabButton.h"
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

void vr_reload_reset(); // GammaVR milestone 8b (below)

player_hud::~player_hud()
{
	IRenderVisual* v = m_model->dcast_RenderVisual();
	::Render->model_Delete(v);
	m_model = nullptr;

	v = m_model_2->dcast_RenderVisual();
	::Render->model_Delete(v);
	m_model_2 = nullptr;

	delete_script_anim_item_model();
	vr_reload_reset(); // GammaVR milestone 8b: the hand magazine's model copy, and the reload state

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
// GammaVR milestone 5 (Amendment 18): support point of the held item (metres along the barrel from the grip point), and
// the two-hand grab state (see vr_place_at_grip)
static bool s_vr_support_on = false;
static float s_vr_support_dist = 0.f;
static bool s_vr_2h_grabbed = false;

// ---------------------------------------------------------------------------------------------------------------------
// GammaVR milestone 8a (Amendment 31): manual reload, logic only. The off hand's grip press takes a fresh magazine from
// the pouch zone on the body, or takes the magazine out of the gun at the magazine point; a hand holding a magazine that
// comes to the magazine point of a gun without one puts it in. Rounds move with CWeaponMagazined::UnloadMagazine /
// ReloadMagazine, no animation. The places are measured in the HUD placement (render path, vr_place_at_grip, once per VR
// frame) and the decisions are taken in the next game update (vr_input_update); both run on the main thread.
struct vr_reload_state
{
	bool parsed, off;        // -vr_reload_auto: everything here off (the game's own reload)
	bool active;             // the held gun is reloaded by hand (set by the game update)
	bool mag_in;             // the held gun has its magazine in (set by the game update)
	bool gun_right;          // gun hand (set by the game update)
	float fwd, down;         // magazine point = grip point + barrel * fwd - gun up * down (set by the game update)
	bool cfg_set;            // mag_fwd / mag_down found in the VR grip config for the held item
	float cfg_fwd, cfg_down;
	bool geo;                // the distances below are valid (off hand located)
	u32 geo_frame;           // VR frame they were measured in
	float d_mag, d_sup, d_pouch;
	Fvector mag_track, pouch_track, sup_track;
	bool hand_mag;           // the off hand holds a magazine
	bool hand_far;           // ... and has been more than 14 cm from the magazine point since it took it
	bool prev_grab;
	bool blocked_evt;        // the game's reload was refused since the last log line
	xr_vector<u16> out;      // ids of the guns whose magazine is out
	// milestone 8b (Amendment 33): the magazine in the hand is a thing with its own rounds
	bool simple;             // -vr_mag_simple: milestone 8a's behaviour (control)
	xr_vector<CCartridge> hand_cart; // the rounds of the magazine in the off hand
	u16 hand_gun;            // the gun that magazine belongs to
	int dropped;             // rounds dropped to the ground so far
	Fvector off_world;       // the off hand in the world (measured with the distances)
	string64 cfg_bone;       // mag_bone of the VR grip config for the held item ("" = "magazine")
};
static vr_reload_state s_vr_rl = {};

// VR only: a gun (or bolt, grenade) held in the hand does not start the game's idle fidget animation ("anm_bore") by
// itself; in the flat game g_vr_track.head0_set is never set, so nothing changes there. -vr_fidget = the game's own.
bool vr_no_idle_fidget()
{
	static int s_on = -1;
	if (!g_vr_track.head0_set)
		return false;
	if (s_on < 0)
	{
		s_on = strstr(Core.Params, "-vr_fidget") ? 0 : 1;
		Msg("* [vr3] idle fidget animation %s", s_on ? "off for the held item" : "ON (-vr_fidget): the game's own");
	}
	return s_on == 1;
}

static bool vr_reload_manual(CWeaponMagazined* W)
{
	vr_reload_state& S = s_vr_rl;
	if (!S.parsed)
	{
		S.parsed = true;
		S.off = !!strstr(Core.Params, "-vr_reload_auto");
		S.simple = !!strstr(Core.Params, "-vr_mag_simple");
		Msg("* [vr3rl] manual reload %s", S.off ? "OFF (-vr_reload_auto): the game's own reload" : "on");
		Msg("* [vr3mag] magazines as objects %s", S.simple ? "OFF (-vr_mag_simple): milestone 8a's behaviour" : "on");
	}
	if (S.off || !W || !g_vr_track.head0_set)
		return false;
	if (smart_cast<CWeaponShotgun*>(W) || smart_cast<CWeaponRPG7*>(W) || smart_cast<CWeaponRG6*>(W))
		return false;
	if (CWeaponMagazinedWGrenade* g = smart_cast<CWeaponMagazinedWGrenade*>(W))
		if (g->GetGrenadeLauncherMode())
			return false;
	return true;
}

// called by CWeaponMagazined::TryReload: true = do not start the game's reload
bool vr_reload_block(CWeaponMagazined* W)
{
	CActor* A = g_actor;
	if (!A || !W || W->H_Parent() != A)
		return false;
	if (smart_cast<CWeaponMagazined*>(A->inventory().ActiveItem()) != W)
		return false;
	if (!vr_reload_manual(W) || W->IsMisfire())
		return false;
	s_vr_rl.blocked_evt = true;
	return true;
}

// milestone 9a (Amendment 32): holsters, logic only. Two zones fixed to the body (head position, head yaw only), on the
// gun-hand side: hip = firearm slot 2, shoulder = firearm slot 3. A grip press of the GUN hand inside a zone sends that
// slot's key, as a player would: draws it, puts it away when it is the one held, or changes to it. Empty slot or press
// outside both zones: nothing. All in tracking space, from the poses of the same read as the buttons.
// -vr_holster_off = no zones. -vr_holster_hip / -vr_holster_shoulder "side down forward" (m), -vr_holster_radius r.
struct vr_holster_state
{
	bool parsed, off;
	float hip[3], sho[3], radius; // side (towards the gun hand), down, forward
	bool prev_grab;
};
static vr_holster_state s_vr_hol = {false, false, {0.20f, 0.60f, 0.05f}, {0.20f, 0.10f, -0.15f}, 0.15f, false};

static void vr_holster_update(const vr_input_info& I, CActor* A, bool allowed, bool grab_gun, bool log)
{
	vr_holster_state& S = s_vr_hol;
	if (!S.parsed)
	{
		S.parsed = true;
		S.off = !!strstr(Core.Params, "-vr_holster_off");
		float a, b, c;
		if (LPCSTR q = strstr(Core.Params, "-vr_holster_hip "))
			if (sscanf(q + 16, "%f %f %f", &a, &b, &c) == 3)
				S.hip[0] = a, S.hip[1] = b, S.hip[2] = c;
		if (LPCSTR q = strstr(Core.Params, "-vr_holster_shoulder "))
			if (sscanf(q + 21, "%f %f %f", &a, &b, &c) == 3)
				S.sho[0] = a, S.sho[1] = b, S.sho[2] = c;
		if (LPCSTR q = strstr(Core.Params, "-vr_holster_radius "))
			if (sscanf(q + 19, "%f", &a) == 1 && a > 0.01f && a < 1.f)
				S.radius = a;
		if (S.off)
			Msg("* [vr3hol] holsters OFF (-vr_holster_off)");
		else
			Msg("* [vr3hol] holsters on: hip (slot 2) side %.3f down %.3f forward %.3f, shoulder (slot 3) side %.3f down %.3f forward %.3f, radius %.3f",
				S.hip[0], S.hip[1], S.hip[2], S.sho[0], S.sho[1], S.sho[2], S.radius);
	}
	const bool press = grab_gun && !S.prev_grab;
	S.prev_grab = grab_gun; // a press seen while the zones cannot act is consumed, not kept for later
	const int gun = I.gun ? 1 : 0;
	const bool geo = I.head.valid && I.grip[gun].valid;
	const bool can = !S.off && allowed && g_vr_track.head0_set && A && !A->Holder() && A->m_inventory;
	Fvector zc[2];
	zc[0].set(0.f, 0.f, 0.f);
	zc[1].set(0.f, 0.f, 0.f);
	float d[2] = {1000.f, 1000.f};
	if (geo)
	{
		Fvector hf, hr;
		vr_quat_rotate(I.head.qx, I.head.qy, I.head.qz, I.head.qw, Fvector().set(0.f, 0.f, 1.f), hf);
		hf.y = 0.f;
		if (hf.magnitude() < 0.01f)
			hf.set(0.f, 0.f, 1.f);
		hf.normalize();
		hr.set(hf.z, 0.f, -hf.x);
		const float side = gun ? 1.f : -1.f;
		for (int z = 0; z < 2; ++z)
		{
			const float* o = z ? S.sho : S.hip;
			zc[z].set(I.head.pos);
			zc[z].y -= o[1];
			zc[z].mad(hr, side * o[0]);
			zc[z].mad(hf, o[2]);
			d[z] = I.grip[gun].pos.distance_to(zc[z]);
		}
	}
	LPCSTR ev = "none";
	u16 cur = NO_ACTIVE_SLOT;
	LPCSTR sect = "none";
	bool has[2] = {false, false};
	if (A && A->m_inventory)
	{
		CInventory& inv = A->inventory();
		cur = inv.GetActiveSlot();
		if (PIItem it = inv.ActiveItem())
			sect = it->m_section_id.size() ? it->m_section_id.c_str() : "none";
		has[0] = !!inv.ItemFromSlot(INV_SLOT_2);
		has[1] = !!inv.ItemFromSlot(INV_SLOT_3);
		if (press && can && geo)
		{
			// the nearer zone when the hand is inside both (they do not overlap with the default places)
			int z = -1;
			if (d[0] <= S.radius)
				z = 0;
			if (d[1] <= S.radius && (z < 0 || d[1] < d[0]))
				z = 1;
			if (z >= 0 && has[z])
			{
				const u16 slot = z ? INV_SLOT_3 : INV_SLOT_2;
				ev = (cur == slot) ? "away" : (cur == INV_SLOT_2 || cur == INV_SLOT_3) ? "change" : "draw";
				A->IR_OnKeyboardPress(kWPN_1 + int(slot) - int(KNIFE_SLOT));
			}
			else if (z >= 0)
				ev = "empty";
		}
	}
	if (log)
		Msg("* [vr3hol] frame %u on %d slot %u section %s has %d %d grab %d geo %d d_hip %.4f d_sho %.4f event %s hip_track %.4f %.4f %.4f sho_track %.4f %.4f %.4f hand_track %.4f %.4f %.4f",
			I.frame, can ? 1 : 0, unsigned(cur), sect, has[0] ? 1 : 0, has[1] ? 1 : 0, grab_gun ? 1 : 0, geo ? 1 : 0, d[0], d[1], ev,
			zc[0].x, zc[0].y, zc[0].z, zc[1].x, zc[1].y, zc[1].z,
			geo ? I.grip[gun].pos.x : 0.f, geo ? I.grip[gun].pos.y : 0.f, geo ? I.grip[gun].pos.z : 0.f);
}

// ---------------------------------------------------------------------------------------------------------------------
// GammaVR milestone 10a step 1 (SPIKE_PLAN Amendment 34 note 7; Caden 2026-11-21: "if they keep a gun in the other hand keep
// their gun"): the PDA in the FREE (off) hand while the gun stays the active item. The off hand's grip press in the chest
// zone (head - 0.30 m down, 0.15 m forward in head yaw, radius 0.075) with no magazine in that hand shows the game's PDA
// window (CUIPdaWnd ShowDialog(false) + Enable(false), as CPda::UpdateCL does for the 3D PDA) WITHOUT Activate(PDA_SLOT);
// the screen texture rtPDA is drawn whenever that window is shown (CLevel::OnRender). A copy of the PDA's HUD model is
// drawn at the off hand (vr_pda_draw, the 8b hand-magazine pattern). The same press again hides it. While VR shows it and
// its input is NOT enabled, it does not stop the VR controls (vr_fire_allowed). The off hand holding the PDA does nothing
// else (its grip is not passed to the magazine / support logic) until the grip is let go after closing.
//   -vr_pda_off        control: the zone does nothing
//   -vr_pda_noattach   TEST ONLY (mutation witness P2w): the copy stays where it was opened, not moved with the hand
//   -vr_pda_gamepath   TEST ONLY (mutation witness P7w): the press uses the game's own kACTIVE_JOBS path (gun put away)
//   -vr_pda_grid F     TEST ONLY (build check b): on VR frame F, log a grid of PDA model points projected per eye
// Log (log window -vr_dbg_grip): [vr3pda] per VR frame, line format defined in tools/xr_m10a_check.py.
struct vr_pda_state
{
	bool parsed, off, noattach, gamepath;
	bool nomirror, passgun; // TEST ONLY witnesses of step 2 (note 17)
	// step 3 input probe (note 20), TEST ONLY: -vr_pda_cursor "F:x:y,..." puts the UI cursor at UI point (x, y) on VR frame F;
	// -vr_surf_scan F logs, on VR frame F, a 25 x 25 grid of downward ray picks (static geometry) around the head
	int cur_n, cur_f[8], surf_frame;
	float cur_x[8], cur_y[8];
	int grid_frame;
	float zone_down, zone_fwd, radius, pass_r;
	bool shown;            // VR showed the PDA window and has not hidden it
	int hand;              // step 2: the hand holding it, 0 = left, 1 = right (meaningful while shown)
	bool prev_grab[2], block[2]; // grips last update (by hand); a hand's grip is kept from the other logic
	float d_hands;
	u32 drawn, drawn_seen; // copy draws (render side, counted) and the count seen by the last log line
	bool place_ok;         // pda / want below are from a draw
	Fvector pda_w, want_w; // the copy's model origin as drawn / off hand + offset (render side, same frame)
	Fvector zone_track, off_track, head_track;
	float d_zone;
	bool in_zone, empty;
	u32 ev_frame;
	LPCSTR ev;
	// step 3 (note 26): set down on a surface, pick up, touch with two hands
	bool touchclick;       // -vr_pda_touchclick (user option): pushing the tip into the screen clicks, no trigger
	float mapshift;        // -vr_pda_mapshift DX (TEST ONLY witness T2s): DX UI units added to the touch's UI x
	int tabs_frame;        // -vr_pda_tabs F (TEST ONLY): log the tab buttons' UI rects on VR frame F
	bool placed;           // lying on a surface (both hands free)
	Fmatrix place_xf;      // its model transform (world) while placed
	Fvector place_c;       // its screen centre (world) while placed
	float surf_y;          // the surface's world height under it
	bool prev_trig[2];
	bool touching[2];      // tip in the touch band and inside the cursor box, by hand
	float ui[2][2], depth[2]; // the touch's UI point and depth (model y above the screen plane), by hand
	int touch_hand;        // the hand driving the cursor (-1: none)
	int btn_hand;          // the hand whose click is down (-1: none)
	bool pushed[2];        // touch-to-click: past the push depth
	bool above[2];         // touch-to-click: was above the screen plane, inside the box, since the last push
	bool gun_consumed;     // the gun hand's trigger went to the PDA (not the gun) this update
	Fvector tip_w[2];      // the tips (world), for the log
	float head_d;          // head to the placed screen centre (world), metres
	bool cursor_shown;     // VR showed the UI cursor (marker) and has not hidden it
	float btn_x, btn_y;    // where the click went down
};
static vr_pda_state s_vr_pda = {};
// step 3 (note 24, A): the screen's UI -> model map from the PDA model's screen mesh (dev_pda_green_hud.ogf, child 2,
// shader pda_overlay, UVs read with tools/xr_db_read.py): model = O + x * U + y * V, U along -z, V along -x
static const float VR_PDA_UI_OX = 0.03744f, VR_PDA_UI_OY = 0.01641f, VR_PDA_UI_OZ = 0.07398f;
static const float VR_PDA_UI_UZ = -0.00012835f, VR_PDA_UI_VX = -0.00009648f;
static const float VR_PDA_UI_CX = 0.00039f, VR_PDA_UI_CZ = 0.00826f; // UI (512, 384) in model x / z
static const float VR_PDA_BOTTOM_Y = 0.0021f;                         // the model's lowest point (bounding box)
static const float VR_PDA_TIP = 0.08f, VR_PDA_BAND = 0.02f, VR_PDA_PUSH = 0.005f;
static const float VR_PDA_SURF = 0.10f, VR_PDA_PICK_R = 0.12f, VR_PDA_AUTOCLOSE = 2.0f;
bool vr_pda_touch_marker(); // CLevel::OnRender: draw the UI cursor on the PDA screen while a VR touch is active

static void vr_pda_parse()
{
	vr_pda_state& S = s_vr_pda;
	if (S.parsed)
		return;
	S.parsed = true;
	S.off = !!strstr(Core.Params, "-vr_pda_off");
	S.noattach = !!strstr(Core.Params, "-vr_pda_noattach");
	S.gamepath = !!strstr(Core.Params, "-vr_pda_gamepath");
	S.nomirror = !!strstr(Core.Params, "-vr_pda_nomirror");
	S.passgun = !!strstr(Core.Params, "-vr_pda_passgun");
	S.cur_n = 0;
	if (LPCSTR q = strstr(Core.Params, "-vr_pda_cursor "))
	{
		q += 15;
		while (S.cur_n < 8)
		{
			int f, used = 0;
			float x, y;
			if (sscanf(q, "%d:%f:%f%n", &f, &x, &y, &used) != 3 || used <= 0)
				break;
			S.cur_f[S.cur_n] = f, S.cur_x[S.cur_n] = x, S.cur_y[S.cur_n] = y;
			++S.cur_n;
			q += used;
			if (*q != ',')
				break;
			++q;
		}
	}
	S.surf_frame = -1;
	if (LPCSTR q = strstr(Core.Params, "-vr_surf_scan "))
		if (sscanf(q + 14, "%d", &S.surf_frame) != 1)
			S.surf_frame = -1;
	if (S.cur_n || S.surf_frame >= 0)
		Msg("* [vr3pda] TEST ONLY step-3 probe: %d cursor placements, surface scan at frame %d", S.cur_n, S.surf_frame);
	S.grid_frame = -1;
	if (LPCSTR q = strstr(Core.Params, "-vr_pda_grid "))
		if (sscanf(q + 13, "%d", &S.grid_frame) != 1)
			S.grid_frame = -1;
	S.touchclick = !!strstr(Core.Params, "-vr_pda_touchclick");
	S.mapshift = 0.f;
	if (LPCSTR q = strstr(Core.Params, "-vr_pda_mapshift "))
		if (sscanf(q + 17, "%f", &S.mapshift) != 1)
			S.mapshift = 0.f;
	S.tabs_frame = -1;
	if (LPCSTR q = strstr(Core.Params, "-vr_pda_tabs "))
		if (sscanf(q + 13, "%d", &S.tabs_frame) != 1)
			S.tabs_frame = -1;
	S.touch_hand = S.btn_hand = -1;
	Msg("* [vr3pdat] step 3: set down / pick up / touch; click %s%s%s; tip %.3f band %.3f surface %.3f pick %.3f autoclose %.2f",
		S.touchclick ? "touch to click (-vr_pda_touchclick)" : "tip plus trigger",
		S.mapshift != 0.f ? "; -vr_pda_mapshift (TEST ONLY)" : "", S.tabs_frame >= 0 ? "; -vr_pda_tabs (TEST ONLY)" : "",
		VR_PDA_TIP, VR_PDA_BAND, VR_PDA_SURF, VR_PDA_PICK_R, VR_PDA_AUTOCLOSE);
	if (S.mapshift != 0.f)
		Msg("* [vr3pdat] mapshift %.2f UI units", S.mapshift);
	S.zone_down = 0.30f, S.zone_fwd = 0.15f, S.radius = 0.075f, S.pass_r = 0.15f;
	S.ev = "none";
	S.ev_frame = u32(-1);
	Msg("* [vr3pda] PDA in the free hand: %s%s%s%s%s%s; zone down %.3f forward %.3f radius %.3f; pass distance %.3f", S.off ? "OFF (-vr_pda_off)" : "on",
		S.noattach ? ", -vr_pda_noattach (TEST ONLY: not moved with the hand)" : "",
		S.gamepath ? ", -vr_pda_gamepath (TEST ONLY: the game's own path, gun put away)" : "",
		S.nomirror ? ", -vr_pda_nomirror (TEST ONLY: the right hand uses the left hand's offset unmirrored)" : "",
		S.passgun ? ", -vr_pda_passgun (TEST ONLY: the PDA may pass to the hand holding the gun)" : "",
		S.grid_frame >= 0 ? ", -vr_pda_grid (TEST ONLY)" : "", S.zone_down, S.zone_fwd, S.radius, S.pass_r);
}

static CUIPdaWnd* vr_pda_wnd()
{
	return (CurrentGameUI() && &CurrentGameUI()->GetPdaMenu() != nullptr) ? &CurrentGameUI()->GetPdaMenu() : nullptr;
}

// the VR-shown PDA window, input not enabled: it does not stop the VR controls (vr_fire_allowed)
static bool vr_pda_passes_input(CUIDialogWnd* top)
{
	const vr_pda_state& S = s_vr_pda;
	CUIPdaWnd* W = vr_pda_wnd();
	return S.shown && !S.gamepath && W && top == W && W->IsShown() && !W->IsEnabled();
}

// step 3: end any touch: a click that is down goes up where it went down, the marker is hidden (if VR showed it)
static void vr_pda_touch_end(CUIPdaWnd* W, u32 frame)
{
	vr_pda_state& S = s_vr_pda;
	if (S.btn_hand >= 0)
	{
		if (W && W->IsShown())
			W->OnMouseAction(S.btn_x, S.btn_y, WINDOW_LBUTTON_UP);
		Msg("* [vr3pdae] frame %u event up hand %c ui %.1f %.1f (touch ended)", frame, S.btn_hand == 1 ? 'R' : 'L', S.btn_x, S.btn_y);
		S.btn_hand = -1;
	}
	S.touch_hand = -1;
	S.touching[0] = S.touching[1] = false;
	S.pushed[0] = S.pushed[1] = false;
	if (S.cursor_shown)
	{
		GetUICursor().Hide();
		S.cursor_shown = false;
	}
}

static void vr_pda_hide(LPCSTR why)
{
	vr_pda_state& S = s_vr_pda;
	vr_pda_touch_end(vr_pda_wnd(), g_vr_track.frame);
	S.placed = false;
	if (CUIPdaWnd* W = vr_pda_wnd())
		if (W->IsShown())
			W->HideDialog();
	S.shown = false;
	if (why)
		Msg("* [vr3pda] PDA window hidden: %s", why);
}

// step 3 helpers
static bool vr_pda_held_xf(Fmatrix& xf, Fvector& screen_c); // the held copy's model transform as last drawn (below, by the draw)

// a static surface straight below 'from' within VR_PDA_SURF (|normal y| >= 0.9): its height
static bool vr_pda_surface(const Fvector& from, float& y)
{
	if (!g_pGameLevel || !Level().bReady)
		return false;
	Fvector st = from;
	st.y += 0.02f;
	collide::rq_result R;
	if (!Level().ObjectSpace.RayPick(st, Fvector().set(0.f, -1.f, 0.f), VR_PDA_SURF + 0.02f, collide::rqtStatic, R, nullptr))
		return false;
	if (R.element < 0)
		return false;
	CDB::TRI* T = Level().ObjectSpace.GetStaticTris() + R.element;
	Fvector* V = Level().ObjectSpace.GetStaticVerts();
	Fvector e1, e2, n;
	e1.sub(V[T->verts[1]], V[T->verts[0]]);
	e2.sub(V[T->verts[2]], V[T->verts[0]]);
	n.crossproduct(e1, e2);
	n.normalize_safe();
	if (_abs(n.y) < 0.9f)
		return false;
	y = st.y - R.range;
	return true;
}

// the deepest shown window under a UI point (absolute UI units), for the log
static CUIWindow* vr_pda_under(CUIWindow* w, float x, float y)
{
	for (CUIWindow* c : w->GetChildWndList())
	{
		if (!c || !c->IsShown())
			continue;
		Frect r;
		c->GetAbsoluteRect(r);
		if (r.in(x, y))
			if (CUIWindow* d = vr_pda_under(c, x, y))
				return d;
	}
	return w;
}

static void vr_pda_click(CUIPdaWnd* W, float x, float y, EUIMessages m)
{
	W->OnMouseAction(x, y, WINDOW_MOUSE_MOVE);
	W->OnMouseAction(x, y, m);
}

bool vr_pda_touch_marker()
{
	const vr_pda_state& S = s_vr_pda;
	return S.cursor_shown && S.shown;
}

// Step 2 (note 17; Caden: "if they put it away and switch hands let them"): the PDA can be held in either hand.
//   - open: a hand's grip press in the chest zone opens it in THAT hand: the off hand as step 1 (no magazine in it), the gun
//     hand only while no item is active (the gun put away);
//   - pass: the other hand's grip press within pass_r (0.15 m) of the hand holding it moves it to the other hand, unless
//     the other hand holds the gun (refused_gun_held) or a magazine (refused_hand_full);
//   - close: the holding hand's press in the chest zone;
//   - while the gun hand holds it, that hand's grip is kept from the holsters (no draw into a hand holding the PDA), and an
//     item becoming active anyway (the game's own keys) closes it.
// Returns the hands whose grip must be kept from the other logic: bit 0 = left, bit 1 = right.
static int vr_pda_update(const vr_input_info& I, CActor* A, bool allowed, const bool grab[2], const bool trig[2], bool log)
{
	vr_pda_state& S = s_vr_pda;
	vr_pda_parse();
	const int gun = I.gun ? 1 : 0, off = 1 - gun;
	bool press[2];
	for (int h = 0; h < 2; ++h)
	{
		press[h] = grab[h] && !S.prev_grab[h];
		S.prev_grab[h] = grab[h];
	}
	// per hand: opening and closing need the head and that hand; passing needs both hands
	const bool geo_h[2] = {I.head.valid && I.grip[0].valid, I.head.valid && I.grip[1].valid};
	const bool geo = geo_h[0] || geo_h[1];
	const bool geo2 = geo_h[0] && geo_h[1];
	float dz[2] = {1000.f, 1000.f};
	S.d_zone = 1000.f;
	S.d_hands = 1000.f;
	S.in_zone = false;
	if (geo)
	{
		Fvector hf;
		vr_quat_rotate(I.head.qx, I.head.qy, I.head.qz, I.head.qw, Fvector().set(0.f, 0.f, 1.f), hf);
		hf.y = 0.f;
		if (hf.magnitude() < 0.01f)
			hf.set(0.f, 0.f, 1.f);
		hf.normalize();
		S.zone_track.set(I.head.pos);
		S.zone_track.y -= S.zone_down;
		S.zone_track.mad(hf, S.zone_fwd);
		S.off_track.set(I.grip[off].pos);
		S.head_track.set(I.head.pos);
		for (int h = 0; h < 2; ++h)
			if (geo_h[h])
				dz[h] = I.grip[h].pos.distance_to(S.zone_track);
		S.d_zone = dz[off];
		S.in_zone = dz[off] <= S.radius;
		if (geo2)
			S.d_hands = I.grip[0].pos.distance_to(I.grip[1].pos);
	}
	S.empty = !s_vr_rl.hand_mag;
	CUIPdaWnd* W = vr_pda_wnd();
	// step 3 probe (TEST ONLY): UI cursor placements, and the surface scan
	for (int c = 0; c < S.cur_n; ++c)
		if (int(I.frame) == S.cur_f[c])
		{
			GetUICursor().Show(); // the game shows it only for a window with input on (note 22); TEST ONLY here
			GetUICursor().SetUICursorPosition(Fvector2().set(S.cur_x[c], S.cur_y[c]));
			const Fvector2 got = GetUICursor().GetCursorPosition();
			Msg("* [vr3pdac] frame %u cursor set %.1f %.1f read %.1f %.1f top %d", I.frame, S.cur_x[c], S.cur_y[c], got.x, got.y,
				(W && CurrentGameUI()->TopInputReceiver() == W) ? 1 : 0);
		}
	if (int(I.frame) == S.surf_frame && I.head.valid && g_vr_track.head0_set && g_pGameLevel && Level().bReady)
	{
		Fvector hw;
		g_vr_track.track_to_world.transform_tiny(hw, I.head.pos);
		Msg("* [vr3surf] frame %u head_world %.4f %.4f %.4f grid 25 x 25 step 0.10 m, rays down from head + 0.20 m", I.frame, hw.x, hw.y, hw.z);
		for (int a = -12; a <= 12; ++a)
			for (int b = -12; b <= 12; ++b)
			{
				Fvector st;
				st.set(hw.x + 0.1f * a, hw.y + 0.2f, hw.z + 0.1f * b);
				collide::rq_result R;
				float below = -1.f, ny = 0.f;
				if (Level().ObjectSpace.RayPick(st, Fvector().set(0.f, -1.f, 0.f), 3.f, collide::rqtStatic, R, nullptr))
				{
					below = R.range - 0.2f;
					if (R.element >= 0)
					{
						CDB::TRI* T = Level().ObjectSpace.GetStaticTris() + R.element;
						Fvector* V = Level().ObjectSpace.GetStaticVerts();
						Fvector e1, e2, n;
						e1.sub(V[T->verts[1]], V[T->verts[0]]);
						e2.sub(V[T->verts[2]], V[T->verts[0]]);
						n.crossproduct(e1, e2);
						n.normalize_safe();
						ny = _abs(n.y);
					}
				}
				Msg("* [vr3surf] dx %.2f dz %.2f below_head %.4f ny %.3f", 0.1f * a, 0.1f * b, below, ny);
			}
	}
	PIItem pi = (A && A->m_inventory) ? A->inventory().ItemFromSlot(PDA_SLOT) : nullptr;
	CPda* pda = smart_cast<CPda*>(pi);
	const bool gun_empty = A && A->m_inventory && !A->inventory().ActiveItem();
	// shown by VR, hidden by something else (another menu, a level change): VR forgets it
	if (S.shown && !S.gamepath && (!W || !W->IsShown()))
	{
		vr_pda_touch_end(W, I.frame);
		S.shown = false;
		S.placed = false;
		Msg("* [vr3pda] frame %u the PDA window was hidden by the game", I.frame);
	}
	// the PDA item left the PDA slot (dropped, moved) while VR shows it: close it
	if (S.shown && !S.gamepath && !pda)
		vr_pda_hide("the PDA item left the PDA slot");
	// an item became active in the gun hand while that hand holds the PDA (the game's own keys): close it
	if (S.shown && !S.gamepath && !S.passgun && S.hand == gun && !gun_empty)
		vr_pda_hide("an item became active in the hand holding the PDA");
	const bool can = !S.off && allowed && g_vr_track.head0_set && A && !A->Holder() && A->m_inventory && W;
	LPCSTR ev = "none";
	int ev_hand = -1;
	if (can && geo && S.gamepath)
	{
		// TEST ONLY witness: the game's own key path (Inventory::Action -> Activate(PDA_SLOT) / back), off hand only
		if (press[off] && dz[off] <= S.radius)
		{
			A->inventory().Action(kACTIVE_JOBS, CMD_START);
			ev = S.shown ? "close" : "open";
			ev_hand = off;
			S.shown = !S.shown;
			S.hand = off;
		}
	}
	else if (can && geo && S.shown && S.placed)
	{
		// step 3: pick up, either hand's grip press within VR_PDA_PICK_R of the placed screen centre (step-2 refusals)
		for (int k = 0; k < 2 && ev_hand < 0; ++k)
		{
			const int h = k == 0 ? off : gun;
			if (!press[h] || !I.grip[h].valid)
				continue;
			Fvector hw;
			g_vr_track.track_to_world.transform_tiny(hw, I.grip[h].pos);
			if (hw.distance_to(S.place_c) > VR_PDA_PICK_R)
				continue;
			ev_hand = h;
			if (h == gun && !gun_empty)
				ev = "refused_gun_held";
			else if (h == off && !S.empty)
				ev = "refused_hand_full";
			else
			{
				vr_pda_touch_end(W, I.frame);
				S.placed = false;
				S.hand = h;
				ev = "pickup";
			}
		}
	}
	else if (can && geo && S.shown)
	{
		const int o = 1 - S.hand;
		Fmatrix hxf;
		Fvector hsc;
		if (press[S.hand] && dz[S.hand] <= S.radius)
		{
			vr_pda_hide(nullptr);
			ev = "close";
			ev_hand = S.hand;
		}
		else if (press[S.hand] && I.grip[S.hand].valid && vr_pda_held_xf(hxf, hsc))
		{
			// step 3: set down, the holding hand's press with a surface within VR_PDA_SURF below the screen centre
			ev_hand = S.hand;
			float sy;
			if (!vr_pda_surface(hsc, sy))
				ev = "refused_no_surface";
			else
			{
				Fvector hw, fw, ft;
				g_vr_track.track_to_world.transform_tiny(hw, I.grip[S.hand].pos);
				const vr_pose& G = I.grip[S.hand];
				vr_quat_rotate(G.qx, G.qy, G.qz, G.qw, Fvector().set(0.f, 0.f, 1.f), ft);
				g_vr_track.track_to_world.transform_dir(fw, ft);
				fw.y = 0.f;
				if (fw.magnitude() < 0.01f)
				{
					vr_quat_rotate(I.head.qx, I.head.qy, I.head.qz, I.head.qw, Fvector().set(0.f, 0.f, 1.f), ft);
					g_vr_track.track_to_world.transform_dir(fw, ft);
					fw.y = 0.f;
				}
				if (fw.magnitude() < 0.01f)
					fw.set(0.f, 0.f, 1.f);
				fw.normalize();
				Fmatrix& X = S.place_xf;
				X.identity();
				X.i.set(fw);
				X.j.set(0.f, 1.f, 0.f);
				X.k.crossproduct(X.i, X.j);
				X.c.set(hw);
				X.c.mad(X.i, -VR_PDA_UI_CX);
				X.c.mad(X.k, -VR_PDA_UI_CZ);
				X.c.y = sy - VR_PDA_BOTTOM_Y;
				X.transform_tiny(S.place_c, Fvector().set(VR_PDA_UI_CX, VR_PDA_UI_OY, VR_PDA_UI_CZ));
				S.surf_y = sy;
				S.placed = true;
				vr_pda_touch_end(W, I.frame);
				ev = "setdown";
			}
		}
		else if (press[o] && geo2 && S.d_hands <= S.pass_r)
		{
			ev_hand = o;
			if (o == gun && !gun_empty && !S.passgun)
				ev = "refused_gun_held";
			else if (o == off && !S.empty)
				ev = "refused_hand_full";
			else
			{
				S.hand = o;
				ev = "pass";
			}
		}
	}
	else if (can && geo)
	{
		// the off hand first (step 1), then the gun hand when it is empty
		int h = -1;
		if (press[off] && dz[off] <= S.radius)
			h = off;
		else if (press[gun] && dz[gun] <= S.radius && gun_empty)
			h = gun;
		if (h >= 0)
		{
			ev_hand = h;
			if (h == off && !S.empty)
				ev = "refused_hand_full";
			else if (!pda)
				ev = "refused_no_pda";
			else if (!pda->hasEnoughBatteryPower())
				ev = "refused_battery";
			else if (!psActorFlags.test(AF_3D_PDA))
				ev = "refused_no_3d_pda";
			else
			{
				W->ShowDialog(false); // don't hide indicators, as CPda::UpdateCL
				W->Enable(false);     // no input: the VR controls stay alive
				S.shown = true;
				S.hand = h;
				ev = "open";
			}
		}
	}
	// step 3: auto-close a placed PDA when the head is more than VR_PDA_AUTOCLOSE from it
	S.head_d = -1.f;
	if (S.shown && S.placed && I.head.valid)
	{
		Fvector hw;
		g_vr_track.track_to_world.transform_tiny(hw, I.head.pos);
		S.head_d = hw.distance_to(S.place_c);
		if (S.head_d > VR_PDA_AUTOCLOSE)
		{
			vr_pda_hide(nullptr);
			ev = "autoclose";
			ev_hand = -1;
		}
	}
	// step 3: touch (placed, or held with the other hand touching)
	S.gun_consumed = false;
	{
		Fmatrix xf;
		Fvector hsc;
		bool have = false;
		if (S.shown && !S.gamepath && W && W->IsShown())
		{
			if (S.placed)
				xf.set(S.place_xf), have = true;
			else
				have = vr_pda_held_xf(xf, hsc);
		}
		Fmatrix inv;
		if (have)
			inv.invert(xf);
		const bool was[2] = {S.touching[0], S.touching[1]};
		for (int h = 0; h < 2; ++h)
		{
			S.touching[h] = false;
			S.depth[h] = 1000.f;
			const vr_pose& G = I.grip[h];
			if (!G.valid)
				continue;
			Fvector ft, tt;
			vr_quat_rotate(G.qx, G.qy, G.qz, G.qw, Fvector().set(0.f, 0.f, VR_PDA_TIP), ft);
			tt.add(G.pos, ft);
			g_vr_track.track_to_world.transform_tiny(S.tip_w[h], tt);
			if (!have || (!S.placed && h == S.hand))
				continue;
			Fvector m;
			inv.transform_tiny(m, S.tip_w[h]);
			S.ui[h][0] = (m.z - VR_PDA_UI_OZ) / VR_PDA_UI_UZ + S.mapshift;
			S.ui[h][1] = (m.x - VR_PDA_UI_OX) / VR_PDA_UI_VX;
			S.depth[h] = m.y - VR_PDA_UI_OY;
			const Frect& B = W->m_cursor_box;
			const bool inside = S.ui[h][0] >= B.left && S.ui[h][0] <= B.right && S.ui[h][1] >= B.top && S.ui[h][1] <= B.bottom;
			const float lo = S.touchclick ? -VR_PDA_BAND : 0.f;
			S.touching[h] = inside && S.depth[h] >= lo && S.depth[h] <= VR_PDA_BAND;
			if (S.touching[h] && !was[h])
				S.touch_hand = h; // the hand touching most recently drives the cursor
			if (S.touchclick)
			{
				if (inside && S.depth[h] > 0.f && !S.pushed[h])
					S.above[h] = true;
				if (!S.touching[h])
					S.above[h] = false;
			}
		}
		if (S.touch_hand >= 0 && !S.touching[S.touch_hand])
			S.touch_hand = S.touching[0] ? 0 : (S.touching[1] ? 1 : -1);
		if (S.touch_hand >= 0)
		{
			const float x = S.ui[S.touch_hand][0], y = S.ui[S.touch_hand][1];
			GetUICursor().SetUICursorPosition(Fvector2().set(x, y));
			W->OnMouseAction(x, y, WINDOW_MOUSE_MOVE);
			if (!S.cursor_shown)
			{
				GetUICursor().Show();
				S.cursor_shown = true;
			}
		}
		else if (S.cursor_shown && S.btn_hand < 0)
		{
			GetUICursor().Hide();
			S.cursor_shown = false;
		}
		for (int h = 0; h < 2; ++h)
		{
			const bool tpress = trig[h] && !S.prev_trig[h];
			S.prev_trig[h] = trig[h];
			if (h == gun && (S.touching[h] || S.btn_hand == h) && trig[h])
				S.gun_consumed = true; // the gun hand's trigger goes to the PDA while it touches or its click is down
			bool down = false, up = false;
			if (!S.touchclick)
			{
				down = tpress && S.touching[h] && S.btn_hand < 0;
				up = S.btn_hand == h && !trig[h];
			}
			else
			{
				down = S.touching[h] && S.above[h] && S.depth[h] < -VR_PDA_PUSH && S.btn_hand < 0;
				up = S.btn_hand == h && (!S.touching[h] || S.depth[h] > 0.f);
			}
			if (down)
			{
				S.btn_hand = h;
				S.btn_x = S.ui[h][0], S.btn_y = S.ui[h][1];
				S.pushed[h] = true;
				S.above[h] = false;
				CUIWindow* u = vr_pda_under(W, S.btn_x, S.btn_y);
				vr_pda_click(W, S.btn_x, S.btn_y, WINDOW_LBUTTON_DOWN);
				Msg("* [vr3pdae] frame %u event down hand %c ui %.1f %.1f depth %.4f under %s '%s' section %s", I.frame, h ? 'R' : 'L',
					S.btn_x, S.btn_y, S.depth[h], u ? typeid(*u).name() : "-", u ? u->WindowName().c_str() : "", W->GetActiveSection());
			}
			else if (up && W)
			{
				const float x = S.touching[h] ? S.ui[h][0] : S.btn_x, y = S.touching[h] ? S.ui[h][1] : S.btn_y;
				vr_pda_click(W, x, y, WINDOW_LBUTTON_UP);
				S.btn_hand = -1;
				S.pushed[h] = false;
				Msg("* [vr3pdae] frame %u event up hand %c ui %.1f %.1f section %s", I.frame, h ? 'R' : 'L', x, y, W->GetActiveSection());
			}
		}
	}
	if (S.shown && W && int(I.frame) == S.tabs_frame)
	{
		// TEST ONLY: the tab buttons' absolute UI rects (the step-3 targets are declared from these)
		if (CUITabControl* tc = W->GetTabControl())
		{
			int n = 0;
			for (CUITabButton* b : *tc->GetButtonsVector())
			{
				Frect r;
				b->GetAbsoluteRect(r);
				Msg("* [vr3pdab] frame %u tab %d id %s rect %.1f %.1f %.1f %.1f shown %d", I.frame, n++, b->m_btn_id.c_str(), r.left, r.top,
					r.right, r.bottom, b->IsShown() ? 1 : 0);
			}
			Msg("* [vr3pdab] frame %u active %s cursor_box %.1f %.1f %.1f %.1f", I.frame, W->GetActiveSection(), W->m_cursor_box.left,
				W->m_cursor_box.top, W->m_cursor_box.right, W->m_cursor_box.bottom);
		}
	}
	if (S.shown && !S.gamepath && W && W->IsShown())
	{
		// what CPda::UpdateCL does for the active PDA, which this one is not: content update and the screen's shader factors
		if (!W->IsEnabled())
			W->Update();
		if (pda)
			W->m_power = pda->GetCondition();
		g_pGamePersistent->pda_shader_data.pda_display_factor = 1.f;
		g_pGamePersistent->pda_shader_data.pda_displaybrightness = 1.f;
	}
	// event names other than open / close / pass are logged, but the per-frame line carries only open / close / pass / none
	const bool oc = !strcmp(ev, "open") || !strcmp(ev, "close") || !strcmp(ev, "pass");
	if (strcmp(ev, "none"))
		Msg("* [vr3pdae] frame %u event %s hand %c d_zone %.4f d_hands %.4f empty %d gun_empty %d", I.frame, ev, ev_hand == 1 ? 'R' : 'L',
			ev_hand >= 0 ? dz[ev_hand] : S.d_zone, S.d_hands, S.empty ? 1 : 0, gun_empty ? 1 : 0);
	// a hand is kept from the other logic while it holds the PDA, and after that until its grip is let go
	for (int h = 0; h < 2; ++h)
	{
		if (S.shown && !S.placed && S.hand == h)
			S.block[h] = true;
		else if (!grab[h])
			S.block[h] = false;
	}
	if (log)
	{
		const bool drawn = S.drawn != S.drawn_seen;
		S.drawn_seen = S.drawn;
		const Fvector& P = S.pda_w;
		const Fvector& Q = S.want_w;
		Msg("* [vr3pda] frame %u on %d shown %d zone %d empty %d event %s d_zone %.4f pda %.5f %.5f %.5f want %.5f %.5f %.5f place %d window %d enabled %d top %d zone_track %.4f %.4f %.4f off_track %.4f %.4f %.4f head_track %.4f %.4f %.4f hand %c held %d d_hands %.4f d_zone_lr %.4f %.4f gun_empty %d block %d %d",
			I.frame, S.off ? 0 : 1, drawn ? 1 : 0, S.in_zone ? 1 : 0, S.empty ? 1 : 0, oc ? ev : "none", S.d_zone,
			P.x, P.y, P.z, Q.x, Q.y, Q.z, S.place_ok ? 1 : 0, (W && W->IsShown()) ? 1 : 0, (W && W->IsEnabled()) ? 1 : 0,
			(W && CurrentGameUI()->TopInputReceiver() == W) ? 1 : 0,
			S.zone_track.x, S.zone_track.y, S.zone_track.z, S.off_track.x, S.off_track.y, S.off_track.z,
			S.head_track.x, S.head_track.y, S.head_track.z, S.hand ? 'R' : 'L', S.shown ? 1 : 0, S.d_hands, dz[0], dz[1],
			gun_empty ? 1 : 0, S.block[0] ? 1 : 0, S.block[1] ? 1 : 0);
		S.place_ok = false;
		const Fvector2 cp = GetUICursor().GetCursorPosition();
		Msg("* [vr3pdat] frame %u placed %d place_c %.4f %.4f %.4f surf_y %.4f head_d %.4f touch %d %d ui %.1f %.1f %.1f %.1f depth %.4f %.4f touch_hand %d btn_hand %d cursor %d %.1f %.1f section %s tip %.4f %.4f %.4f %.4f %.4f %.4f gun_consumed %d",
			I.frame, S.placed ? 1 : 0, S.place_c.x, S.place_c.y, S.place_c.z, S.surf_y, S.head_d, S.touching[0] ? 1 : 0, S.touching[1] ? 1 : 0,
			S.ui[0][0], S.ui[0][1], S.ui[1][0], S.ui[1][1], S.depth[0], S.depth[1], S.touch_hand, S.btn_hand,
			(S.cursor_shown && GetUICursor().IsVisible()) ? 1 : 0, cp.x, cp.y, (W && S.shown) ? W->GetActiveSection() : "-",
			S.tip_w[0].x, S.tip_w[0].y, S.tip_w[0].z, S.tip_w[1].x, S.tip_w[1].y, S.tip_w[1].z, S.gun_consumed ? 1 : 0);
	}
	return (S.block[0] ? 1 : 0) | (S.block[1] ? 2 : 0) | (S.gun_consumed ? 4 : 0);
}

// milestone 8b: loose ammo on the ground near the actor, as the world has it (for the [vr3mag] log)
static void vr_mag_ground(CActor* A, int& count, int& rounds, u16& last_id, Fvector& last_pos, float& ground_y)
{
	count = rounds = 0;
	last_id = u16(-1);
	last_pos.set(0.f, 0.f, 0.f);
	ground_y = -9999.f; // no ground found under it (or no loose ammo)
	if (!A)
		return;
	for (u32 i = 0, n = Level().Objects.o_count(); i < n; ++i)
	{
		CObject* O = Level().Objects.o_get_by_iterator(i);
		CWeaponAmmo* am = O ? smart_cast<CWeaponAmmo*>(O) : nullptr;
		if (!am || am->H_Parent() || am->getDestroy() || am->Position().distance_to(A->Position()) > 3.f)
			continue;
		++count;
		rounds += int(am->m_boxCurr);
		if (last_id == u16(-1) || am->ID() > last_id)
		{
			last_id = am->ID();
			last_pos.set(am->Position());
		}
	}
	if (last_id != u16(-1))
	{
		// the level's static geometry straight below it, from 1 m above (milestone 8b note 6: does it rest on the ground)
		collide::rq_result R;
		Fvector st;
		st.set(last_pos.x, last_pos.y + 1.f, last_pos.z);
		if (Level().ObjectSpace.RayPick(st, Fvector().set(0.f, -1.f, 0.f), 3.f, collide::rqtStatic, R, nullptr))
			ground_y = st.y - R.range;
	}
}

// once per new controller read, in the game update
static void vr_reload_update(const vr_input_info& I, CActor* A, bool allowed, bool grab, bool log)
{
	vr_reload_state& S = s_vr_rl;
	S.gun_right = I.gun != 0;
	CWeaponMagazined* W = (A && allowed) ? smart_cast<CWeaponMagazined*>(A->inventory().ActiveItem()) : nullptr;
	S.active = W && vr_reload_manual(W);
	const bool press = grab && !S.prev_grab, release = !grab && S.prev_grab;
	S.prev_grab = grab;
	LPCSTR ev = "none";
	if (S.blocked_evt)
	{
		ev = "reload_blocked";
		S.blocked_evt = false;
	}
	// milestone 8b: [vr3mag] event, and the log line of this update
	LPCSTR mev = "none";
	auto mag_log = [&](CWeaponMagazined* G)
	{
		if (!log)
			return;
		int gc, gr;
		u16 gid;
		Fvector gp;
		float gy;
		vr_mag_ground(A, gc, gr, gid, gp, gy);
		const int ammo = G ? G->GetAmmoElapsed() : -1;
		Msg("* [vr3mag] frame %u simple %d active %d mag %s gun_rounds %d hand_mag %d hand_rounds %d inv %d dropped %d event %s ground %d ground_rounds %d last_id %u last_pos %.4f %.4f %.4f hand_world %.4f %.4f %.4f ground_y %.4f",
			I.frame, S.simple ? 1 : 0, S.active ? 1 : 0, S.mag_in ? "in" : "out", ammo, S.hand_mag ? 1 : 0, int(S.hand_cart.size()),
			G ? G->GetSuitableAmmoTotal() - ammo : -1, S.dropped, mev, gc, gr, unsigned(gid), gp.x, gp.y, gp.z,
			S.off_world.x, S.off_world.y, S.off_world.z, gy);
	};
	// a magazine in the hand whose gun is no longer the one held (or nothing is held): its rounds go back to the stock
	if (!S.simple && S.hand_mag && (!S.active || W->ID() != S.hand_gun))
	{
		// through its gun if that still exists (in the inventory or lying on the ground), else through the gun now held
		CWeaponMagazined* G0 = smart_cast<CWeaponMagazined*>(Level().Objects.net_Find(S.hand_gun));
		if (!G0)
			G0 = W;
		if (G0 && A && (G0->H_Parent() == A ? G0->VrMagToInventory(S.hand_cart) : G0->VrMagToInventory(S.hand_cart, A->ID())))
			mev = "stow";
		else
		{
			Msg("! [vr3mag] frame %u the magazine in the hand lost its gun: %u rounds gone", I.frame, u32(S.hand_cart.size()));
			S.hand_cart.clear();
			mev = "lost";
		}
		S.hand_mag = false;
	}
	if (!S.active)
	{
		S.hand_mag = false;
		S.mag_in = true;
		if (log)
			Msg("* [vr3rl] frame %u inactive event %s ammo %d state %u", I.frame, ev, W ? W->GetAmmoElapsed() : -1, W ? W->GetState() : 0u);
		mag_log(W);
		return;
	}
	if (S.cfg_set)
	{
		S.fwd = S.cfg_fwd;
		S.down = S.cfg_down;
	}
	else if (smart_cast<CWeaponPistol*>(W))
	{
		S.fwd = 0.f;
		S.down = 0.07f;
	}
	else
	{
		S.fwd = 0.10f;
		S.down = 0.06f;
	}
	const u16 id = W->ID();
	xr_vector<u16>::iterator it = std::find(S.out.begin(), S.out.end(), id);
	bool out = it != S.out.end();
	if (out && W->GetAmmoElapsed() > 0) // rounds got in some other way: the magazine is in again
	{
		S.out.erase(it);
		out = false;
	}
	const bool geo = S.geo && S.geo_frame + 2 >= I.frame;
	const bool ready = !W->IsPending();
	if (geo)
	{
		if (S.hand_mag && S.d_mag > 0.14f)
			S.hand_far = true;
		if (press)
		{
			if (S.d_pouch <= 0.15f)
			{
				if (!S.simple && !S.hand_mag)
				{
					W->VrMagFromInventory(S.hand_cart); // 8b: the fresh magazine takes its rounds now
					S.hand_gun = id;
					mev = "pouch_take";
				}
				S.hand_mag = true;
				S.hand_far = S.d_mag > 0.14f;
				ev = "pouch";
			}
			else if (!out && !S.hand_mag && ready && S.d_mag <= 0.10f && S.d_mag < S.d_sup)
			{
				if (S.simple)
					W->UnloadMagazine(true);
				else
				{
					W->VrMagTake(S.hand_cart); // 8b: the rounds stay in the magazine
					S.hand_gun = id;
					mev = "out";
				}
				S.out.push_back(id);
				out = true;
				S.hand_mag = true;
				S.hand_far = false;
				ev = "out";
			}
		}
		else if (grab && S.hand_mag && S.hand_far && out && ready && S.d_mag <= 0.10f)
		{
			if (S.simple)
				W->VrFillMagazine();
			else
			{
				W->VrMagPut(S.hand_cart);
				mev = "in";
			}
			it = std::find(S.out.begin(), S.out.end(), id);
			if (it != S.out.end())
				S.out.erase(it);
			out = false;
			S.hand_mag = false;
			ev = "in";
		}
	}
	if (release && S.hand_mag)
	{
		S.hand_mag = false;
		ev = "put_away";
		if (!S.simple)
		{
			if (geo && S.d_pouch <= 0.15f)
			{
				W->VrMagToInventory(S.hand_cart); // into the pouch as it is: the rounds go back to the stock
				mev = "pouch_put";
			}
			else if (geo && out && ready && S.d_mag <= 0.10f)
			{
				W->VrMagPut(S.hand_cart); // let go at the gun: back in
				it = std::find(S.out.begin(), S.out.end(), id);
				if (it != S.out.end())
					S.out.erase(it);
				out = false;
				ev = "in";
				mev = "in";
			}
			else if (geo)
			{
				S.dropped += W->VrMagDrop(S.hand_cart, S.off_world); // let go anywhere else: on the ground, as ammo
				mev = "drop";
			}
			else
			{
				W->VrMagToInventory(S.hand_cart); // hand not tracked: nothing is thrown away
				mev = "stow";
			}
			if (!S.hand_cart.empty()) // nowhere to put them (not expected for a gun in the hand)
			{
				Msg("! [vr3mag] frame %u %u rounds of the magazine in the hand could not be put anywhere", I.frame, u32(S.hand_cart.size()));
				S.hand_cart.clear();
			}
		}
	}
	S.mag_in = !out;
	if (log)
	{
		const int ammo = W->GetAmmoElapsed();
		Msg("* [vr3rl] frame %u mag %s hand_mag %d ammo %d inv %d state %u event %s geo %d d_mag %.4f d_pouch %.4f d_sup %.4f mag_track %.4f %.4f %.4f pouch_track %.4f %.4f %.4f sup_track %.4f %.4f %.4f",
			I.frame, out ? "out" : "in", S.hand_mag ? 1 : 0, ammo, W->GetSuitableAmmoTotal() - ammo, W->GetState(), ev, geo ? 1 : 0,
			S.d_mag, S.d_pouch, S.d_sup, S.mag_track.x, S.mag_track.y, S.mag_track.z, S.pouch_track.x, S.pouch_track.y, S.pouch_track.z, S.sup_track.x, S.sup_track.y, S.sup_track.z);
	}
	mag_log(W);
}

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
	// milestone 5 (Amendment 18): optional support_dist, resolved like grip_pos (the weapon / -vr_grip_as section, then the
	// HUD section, then the family_<f> section named by the first of those with a 'family' line). Not affected by
	// -vr_grip_off (which only concerns the grip offset). A new item always starts one-handed.
	s_vr_support_on = false;
	s_vr_support_dist = 0.f;
	s_vr_2h_grabbed = false;
	if (C.ini)
	{
		LPCSTR cand[2] = { C.as[0] ? C.as : obj_sect.c_str(), C.as[0] ? nullptr : item->m_sect_name.c_str() };
		LPCSTR sup_src = nullptr;
		LPCSTR fam_from = nullptr;
		for (int n = 0; n < 2 && !sup_src; ++n)
		{
			if (!cand[n] || !C.ini->section_exist(cand[n]))
				continue;
			if (C.ini->line_exist(cand[n], "support_dist"))
				sup_src = cand[n];
			else if (!fam_from && C.ini->line_exist(cand[n], "family"))
				fam_from = cand[n];
		}
		string64 sup_fam;
		if (!sup_src && fam_from)
		{
			LPCSTR f = C.ini->r_string(fam_from, "family");
			if (strstr(f, "family_") == f)
				xr_strcpy(sup_fam, f);
			else
				xr_sprintf(sup_fam, "family_%s", f);
			if (C.ini->section_exist(sup_fam) && C.ini->line_exist(sup_fam, "support_dist"))
				sup_src = sup_fam;
		}
		if (sup_src)
		{
			s_vr_support_dist = C.ini->r_float(sup_src, "support_dist");
			s_vr_support_on = true;
		}
		// milestone 8a: magazine point of this item, if the config gives one (both keys, in the item's own section)
		s_vr_rl.cfg_set = false;
		for (int n = 0; n < 2 && !s_vr_rl.cfg_set; ++n)
			if (cand[n] && C.ini->section_exist(cand[n]) && C.ini->line_exist(cand[n], "mag_fwd") && C.ini->line_exist(cand[n], "mag_down"))
			{
				s_vr_rl.cfg_fwd = C.ini->r_float(cand[n], "mag_fwd");
				s_vr_rl.cfg_down = C.ini->r_float(cand[n], "mag_down");
				s_vr_rl.cfg_set = true;
			}
		// milestone 8b: the magazine bone of this item's HUD model, if it is not called "magazine"
		s_vr_rl.cfg_bone[0] = 0;
		for (int n = 0; n < 2 && !s_vr_rl.cfg_bone[0]; ++n)
			if (cand[n] && C.ini->section_exist(cand[n]) && C.ini->line_exist(cand[n], "mag_bone"))
				xr_strcpy(s_vr_rl.cfg_bone, C.ini->r_string(cand[n], "mag_bone"));
	}
	if (s_vr_support_on)
		Msg("* [vr3] support point for %s: %.3f m", obj_sect.c_str(), s_vr_support_dist);
	else
		Msg("* [vr3] support point for %s: none", obj_sect.c_str());
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

// ---------------------------------------------------------------------------------------------------------------------
// GammaVR milestone 4 (Amendment 16): firing along the barrel, recoil on the gun. VR m3 path only (see player_hud.h).
vr_fire_cache g_vr_fire;  // static storage: zero-initialised, valid = false
vr_kick_state g_vr_kick;  // zero
int g_vr_fire_src = 0;
u32 g_vr_fire_placed_frame = 0;
// milestone 4b (Amendment 17)
u16 g_vr_fire_defer_weapon = u16(-1);
bool g_vr_fire_frame_set = false;
u32 g_vr_fire_frame = 0;

// -vr_fire_old / -vr_fire_prev / -vr_recoil_old (controls), parsed once; constructed only on the VR m3 path (see vr_fire_new below), so
// the start lines appear only in VR runs
struct vr_m4_switches
{
	bool fire_old, fire_prev, recoil_old;
	vr_m4_switches()
	{
		fire_old = !!strstr(Core.Params, "-vr_fire_old");
		fire_prev = !!strstr(Core.Params, "-vr_fire_prev"); // milestone 4b control: milestone 4's previous-frame placement
		recoil_old = !!strstr(Core.Params, "-vr_recoil_old");
		Msg(fire_old ? "* [vr3] fire: OLD (flat camera pick)"
			: fire_prev ? "* [vr3] fire: from the gun barrel (VR), PREVIOUS frame (control)"
			: "* [vr3] fire: from the gun barrel (VR), same frame");
		Msg(recoil_old ? "* [vr3] recoil: OLD (actor aim)" : "* [vr3] recoil: on the gun (VR)");
	}
};
static const vr_m4_switches& vr_m4()
{
	static const vr_m4_switches s;
	return s;
}
bool vr_fire_new()
{
	return vr_m3_vr() && !vr_m4().fire_old;
}
bool vr_recoil_new()
{
	return vr_m3_vr() && !vr_m4().recoil_old;
}
bool vr_fire_same_frame()
{
	return vr_fire_new() && !vr_m4().fire_prev;
}

// called from CActor::update_camera instead of CWeaponShotEffector::ChangeHP: the same increments, kept here
void vr_kick_add(float d_pitch, float d_yaw)
{
	g_vr_kick.pitch += d_pitch;
	g_vr_kick.yaw += d_yaw;
}

// called once per actor camera update (CActor::on_weapon_shot_update). follow_effector = the shot effector exists, is
// active and returns by itself (cam_return): the part of the kick it owns (own_pitch/own_yaw = what it has handed to
// ChangeHP since its last Reset) follows its own relaxing deltas through vr_kick_add; only the rest (left over from an
// earlier effector / weapon) decays. Otherwise (no effector, effector inactive, or a weapon without cam_return) the whole
// kick decays exponentially with a time constant of 0.15 s of game time (Device.fTimeDelta, the same delta the effector
// relaxes with), so it always dies away.
void vr_kick_decay(bool follow_effector, float own_pitch, float own_yaw)
{
	if (!follow_effector)
		own_pitch = own_yaw = 0.f;
	const float dt = Device.fTimeDelta;
	if (!(dt > 0.f))
		return;
	const float k = expf(-dt / 0.15f);
	g_vr_kick.pitch = own_pitch + (g_vr_kick.pitch - own_pitch) * k;
	g_vr_kick.yaw = own_yaw + (g_vr_kick.yaw - own_yaw) * k;
	if (_abs(g_vr_kick.pitch) < 1e-7f)
		g_vr_kick.pitch = 0.f;
	if (_abs(g_vr_kick.yaw) < 1e-7f)
		g_vr_kick.yaw = 0.f;
}

// the cached placement, if it is of this weapon and from the last two engine frames (the fire path runs in the game
// update, before this frame's eyes are drawn, so normally it is the placement of the previous frame)
bool vr_fire_from_gun(u16 weapon_id, Fvector& start, Fvector& dir, u32& placed_frame)
{
	placed_frame = 0;
	const vr_fire_cache& F = g_vr_fire;
	if (!F.valid || F.item_id != weapon_id || Device.dwFrame - F.dev_frame > 2)
		return false;
	start.set(F.muzzle);
	dir.set(F.dir);
	placed_frame = F.frame;
	return true;
}

// milestone 4b (Amendment 17): the cached placement only if it is of this weapon and was made in the render of engine
// frame dev_frame (the frame whose update fired the shot). Nothing changed and false otherwise.
bool vr_fire_cache_of_frame(u16 weapon_id, u32 dev_frame, Fvector& start, Fvector& dir, u32& placed_frame)
{
	const vr_fire_cache& F = g_vr_fire;
	if (!F.valid || F.item_id != weapon_id || F.dev_frame != dev_frame)
		return false;
	start.set(F.muzzle);
	dir.set(F.dir);
	placed_frame = F.frame;
	return true;
}

// The held item's world muzzle point and barrel direction as drawn. With -vr_barrel_bones and both bones found: exactly
// the computation of gun_vis_axis / the [vr3grip] 'barrel' field (world point = D * LL_GetTransform(bone).c, D = the
// draw transform), muzzle = front bone, direction = rear -> front. Otherwise vr_gun_fire_world (fire point and fire axis
// from m_item_transform, the same as the gun_axis / gun_muzzle lines). False when neither is available.
static bool vr_gun_barrel_world(attachable_hud_item* item, const Fmatrix& D, Fvector& muzzle, Fvector& dir, bool& from_bones)
{
	from_bones = false;
	const vr_3b_switches& S = vr_3b();
	if (S.barrel && item->m_model)
	{
		IKinematics* K = item->m_model;
		const u16 br = vr_find_bone(K, S.rear), bf = vr_find_bone(K, S.front);
		if (br != BI_NONE && bf != BI_NONE)
		{
			Fvector rw, fw, a;
			D.transform_tiny(rw, K->LL_GetTransform(br).c);
			D.transform_tiny(fw, K->LL_GetTransform(bf).c);
			a.sub(fw, rw);
			if (a.magnitude() > EPS)
			{
				a.normalize();
				dir.set(a);
				muzzle.set(fw);
				from_bones = true;
				return true;
			}
		}
	}
	// fallback: fire point and fire axis (vr_gun_fire_world), but through the draw transform D (differs from
	// m_item_transform only under the -vr_vis_rot test control; identical otherwise)
	const hud_item_measures& M = item->m_measures;
	if (!M.m_prop_flags.test(hud_item_measures::e_fire_point) || M.m_fire_bone == BI_NONE || !item->m_model)
		return false;
	Fvector local;
	item->m_model->LL_GetTransform(M.m_fire_bone).transform_tiny(local, M.m_fire_point_offset);
	D.transform_tiny(muzzle, local);
	D.transform_dir(dir, M.m_fire_direction);
	if (dir.magnitude() < EPS)
		return false;
	dir.normalize();
	return true;
}

// render_hud, every eye pass of a VR frame after the placement (and after the -vr_vis_rot draw transforms): g_vr_fire
void player_hud::vr_cache_fire()
{
	const int idx = m_attached_items[0] ? 0 : 1;
	attachable_hud_item* item = m_attached_items[idx];
	g_vr_fire.valid = false;
	if (!item)
		return;
	const Fmatrix& D = vr_3b().vis_rot ? m_vr_draw_xform[idx] : item->m_item_transform;
	Fvector muzzle, dir;
	bool from_bones = false;
	if (!vr_gun_barrel_world(item, D, muzzle, dir, from_bones))
		return;
	g_vr_fire.muzzle.set(muzzle);
	g_vr_fire.dir.set(dir);
	g_vr_fire.from_bones = from_bones;
	g_vr_fire.frame = g_vr_track.frame;
	g_vr_fire.dev_frame = Device.dwFrame;
	g_vr_fire.item_id = (item->m_parent_hud_item && item->m_parent_hud_item->has_object())
		? item->m_parent_hud_item->object().ID() : u16(-1);
	g_vr_fire.valid = true;
}

// The frame number used by [vr3fire] / [vr3hit]: the VR frame the current game update leads to (the eyes of that frame
// are drawn after this update): last drawn VR frame + 1. The runtime counter restarts at 0 after a failed frame; then
// this number is one too high for the first frame after the failure.
static u32 vr_update_frame()
{
	return g_vr_track.frame + 1;
}
u32 vr_fire_update_frame()
{
	return vr_update_frame();
}

// [vr3fire]: CShootingObject::FireBullet, actor's bullets only, VR m3 path only
static xr_set<u32> s_vr_fired_bullets;
void vr_log_shot(int shot, const Fvector& pos, const Fvector& dir0, const Fvector& dir, float disp, u32 bullet)
{
	if (!vr_m3_vr())
		return;
	s_vr_fired_bullets.insert(bullet);
	Msg("* [vr3fire] frame %u shot %d start %.5f %.5f %.5f dir0 %.6f %.6f %.6f dir %.6f %.6f %.6f disp %.6f bullet %u src %d placed_frame %u",
		g_vr_fire_frame_set ? g_vr_fire_frame : vr_update_frame(), shot, pos.x, pos.y, pos.z, dir0.x, dir0.y, dir0.z, dir.x, dir.y, dir.z, disp, bullet,
		g_vr_fire_src, g_vr_fire_placed_frame);
}

// [vr3hit]: CBulletManager::CommitEvents EVENT_HIT, once per bullet logged by [vr3fire]
void vr_log_hit(u32 bullet, const Fvector& pos)
{
	if (!vr_m3_vr())
		return;
	xr_set<u32>::iterator it = s_vr_fired_bullets.find(bullet);
	if (it == s_vr_fired_bullets.end())
		return;
	s_vr_fired_bullets.erase(it);
	Msg("* [vr3hit] frame %u bullet %u pos %.5f %.5f %.5f", vr_update_frame(), bullet, pos.x, pos.y, pos.z);
}

// ---------------------------------------------------------------------------------------------------------------------
// GammaVR milestone 5 (Amendment 18): two-handed grip. VR m3 path only (vr_place_at_grip). Switches parsed once, at the
// first VR placement (so the start line appears only in VR runs):
//   -vr_2h_off                  feature disabled (control): never grabbed
//   -vr_2h_force                the off hand's grip input is held on every frame
//   -vr_2h_force <from> <to>    held only in VR frames from..to (g_vr_track.frame, as in [vr3grip]), inclusive
struct vr_2h_switches
{
	bool off = false;
	bool force = false;
	bool window = false;
	int from = -1, to = -1;
	vr_2h_switches()
	{
		off = !!strstr(Core.Params, "-vr_2h_off");
		if (LPCSTR p = strstr(Core.Params, "-vr_2h_force"))
		{
			force = true;
			int a = -1, b = -1;
			if (sscanf(p + 12, " %d %d", &a, &b) == 2)
			{
				window = true;
				from = a;
				to = b;
			}
		}
		if (off)
			Msg("* [vr3] two-hand: OFF");
		else if (force && window)
			Msg("* [vr3] two-hand: on (force %d-%d)", from, to);
		else if (force)
			Msg("* [vr3] two-hand: on (force always)");
		else
			Msg("* [vr3] two-hand: on (controller input)");
	}
};
static const vr_2h_switches& vr_2h()
{
	static const vr_2h_switches s;
	return s;
}

// Milestone 6 (Amendment 19): the off hand's grab input, as held by vr_input_update (hysteresis state after the values
// of the last processed VR frame). Written in vr_input_update (CLevel::OnFrame, FrameMove, main thread) and read by
// vr_2h_input_held from the HUD placement (render_hud, render path, main thread). Both run on the main thread, one after
// the other (FrameMove ends before the render starts; neither runs inside the GameThread secondary task), so a plain
// bool suffices.
static bool s_vr_grab_held = false;

// The off hand's grip input: the controller's grab input (s_vr_grab_held) OR the -vr_2h_force test switch; never with
// -vr_2h_off.
static bool vr_2h_input_held()
{
	const vr_2h_switches& S = vr_2h();
	if (S.off)
		return false;
	if (S.force && (!S.window || (int(g_vr_track.frame) >= S.from && int(g_vr_track.frame) <= S.to)))
		return true;
	return s_vr_grab_held;
}

// ---------------------------------------------------------------------------------------------------------------------
// GammaVR milestone 6 (Amendment 19): controller input mapping. Once per game update (CLevel::OnFrame, FrameMove, main
// thread), for each NEW read of the controller buttons (g_vr_get_input, read in the previous render after xrSyncActions):
//  - hysteresis per hand and input: pressed when value >= 0.7, released when value <= 0.5 (inactive reads as 0);
//    every edge of both hands and both inputs is logged ([vr3in]);
//  - gun hand trigger -> the actor's kWPN_FIRE key press / release (CActor::IR_OnKeyboardPress / Release);
//  - off hand grab -> s_vr_grab_held (two-handed grab, vr_2h_input_held);
//  - off hand trigger and gun hand grab: nothing (edges logged only);
//  - in the -vr_dbg_grip <from> <to> window: one [vr3inp] line per VR frame read.
extern bool g_bDisableAllInput;
static bool vr_hyst_step(bool& on, float v)
{
	if (!on && v >= 0.7f)
	{
		on = true;
		return true;
	}
	if (on && v <= 0.5f)
	{
		on = false;
		return true;
	}
	return false;
}

// The checks a real kWPN_FIRE key press passes in CLevel::IR_OnKeyboardPress before it reaches the actor, where they apply
// to an input without a key code: level ready, input not disabled by script, no dialog / menu on top, console closed,
// game not paused; plus: the actor exists, is alive and is the current entity (the one the key would go to).
static bool vr_fire_allowed(CActor* A)
{
	if (!A || !g_pGameLevel || !Level().bReady || Device.dwPrecacheFrame)
		return false;
	if (Level().CurrentEntity() != A || !A->g_Alive())
		return false;
	if (g_bDisableAllInput || Device.Paused())
		return false;
	if (CurrentGameUI() && CurrentGameUI()->TopInputReceiver() && !vr_pda_passes_input(CurrentGameUI()->TopInputReceiver()))
		return false; // milestone 10a: except the PDA window VR showed in the free hand, its input not enabled
	if (Console && Console->bVisible)
		return false;
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// GammaVR milestone 7 (Amendment 20): stick walking and turning. Only with -vr_m3 -vr_openxr (vr_m3_vr) and a frame whose
// tracking mapping hung on the VR base (g_vr_track.base_used); everything below is a no-op otherwise.
// Angles: "heading" convention throughout (engine axes x right, y up, z forward; heading = atan2(x, z); a POSITIVE angle
// turns to the RIGHT, clockwise seen from above). g_vr_base.yaw is such a heading (game_cam.rotateY(yaw)); the actor's
// camera yaw (CCameraBase::yaw, kRIGHT adds to it, movement is rotated by rotateY(-r_model_yaw) = rotateY(cam yaw)) too.

// v rotated about the world up axis by heading angle a (same as Fmatrix().rotateY(a).transform_dir(v))
static void vr_rot_y(Fvector& v, float a)
{
	const float c = _cos(a), s = _sin(a);
	const float x = v.x * c + v.z * s, z = -v.x * s + v.z * c;
	v.x = x;
	v.z = z;
}

// Walk request handed to CActor::g_cl_CheckControls. Written by vr_input_update (CLevel::OnFrame: FrameMove, main thread);
// read by the actor's shedule_Update, which with mt_Scheduler (default) runs in the GameThread task the device starts
// AFTER FrameMove of the same device frame and waits for before the next FrameMove (with mt_Scheduler 0: in
// CGamePersistent::OnFrame, same seqFrame pass on the main thread, order against CLevel::OnFrame not fixed, hence the
// one-frame tolerance below). Never written and read at the same time.
struct vr_walk_state
{
	bool on;
	u32 dev_frame;  // Device.dwFrame of the update that computed it
	Fvector dir;    // W: unit, horizontal, world
	float k;        // strength 0..1
};
static vr_walk_state s_vr_walk; // static storage: zero-initialised, off

// for CActor::g_cl_CheckControls (Actor_Movement.cpp): true = VR stick walking this update (dir = unit world direction,
// k = strength in (0, 1]). Always false in flat runs.
bool vr_walk_get(Fvector& dir, float& k)
{
	if (!vr_m3_vr() || !s_vr_walk.on)
		return false;
	if (s_vr_walk.dev_frame != Device.dwFrame && s_vr_walk.dev_frame + 1 != Device.dwFrame)
		return false; // stale: the VR path did not run this update
	dir.set(s_vr_walk.dir);
	k = s_vr_walk.k;
	return true;
}

static bool vr_walk_head()
{
	static const bool v = !!strstr(Core.Params, "-vr_walk_head");
	return v;
}
static bool vr_turn_smooth()
{
	static const bool v = !!strstr(Core.Params, "-vr_turn_smooth");
	return v;
}
// Caden 2026-11-19: stick turning is OFF by default (turn with your own head and body). Opt-in: -vr_turn_snap or
// -vr_turn_smooth. With it off the right stick does nothing.
static bool vr_turn_on()
{
	static const bool v = vr_turn_smooth() || !!strstr(Core.Params, "-vr_turn_snap");
	return v;
}

// ---------------------------------------------------------------------------------------------------------------------
// GammaVR milestone 7b (Amendment 23): physical crouch. Standing calibration (first VR read with a head pose for a new actor,
// and on the console command vr_stand_calibrate): y_calib = head tracking-space y, H = standing eye height (-vr_stand_height,
// else the head's height above the runtime's floor, else 1.66; clamped to 1.0..2.2). Ratio r = (H + y - y_calib) / H; body
// state with hysteresis: 0 stand -> 1 crouch below 0.90, 1 -> 0 above 0.93, 1 -> 2 low crouch below 0.49, 2 -> 1 above 0.52.
// Applied by CActor::g_cl_CheckControls through the wish bits (vr_crouch_bits). Same threading as s_vr_walk above.
static bool vr_crouch_off()
{
	static const bool v = !!strstr(Core.Params, "-vr_crouch_off");
	return v;
}
// physical crouch is on for this run; also tells ActorCameras.cpp to hang the VR base on the STANDING camera height
bool vr_crouch_physical()
{
	return vr_m3_vr() && !vr_crouch_off();
}
// -vr_stand_height <metres>: false = not given
static bool vr_stand_height_setting(float& h)
{
	static float s_h = 0.f;
	static int s_have = -1;
	if (s_have < 0)
	{
		s_have = 0;
		if (LPCSTR p = strstr(Core.Params, "-vr_stand_height "))
			if (sscanf(p + sizeof("-vr_stand_height ") - 1, "%f", &s_h) == 1)
				s_have = 1;
	}
	h = s_h;
	return s_have == 1;
}

struct vr_crouch_state
{
	const CActor* actor;  // the actor the calibration belongs to
	bool calibrated;
	bool calib_request;   // console command vr_stand_calibrate: calibrate at the next VR read
	float y_calib;        // head tracking-space y at calibration
	float H;              // standing eye height, metres
	float dy, r;          // head offset since calibration and ratio of the last read
	int want;             // hysteresis state: 0 stand, 1 crouch, 2 low crouch (0 with -vr_crouch_off)
	u32 dev_frame;        // Device.dwFrame of the update that set 'want'
	int applied;          // last state handed to g_cl_CheckControls: 0/1/2 as 'want', 3 = button crouch (see vr_crouch_bits)
	u32 wf;               // wish bits g_cl_CheckControls used last (log only)
	float off;            // calibration offset: y_calib - start head y (g_vr_track.head0_y); set only at calibration
	bool btn;             // crouch button toggle flag (opt-in action crouch_toggle)
	bool btn_prev[2];     // raw button state of the last read, per hand (rising edge)
	bool btn_raw;         // raw button state of the last read, either hand (log)
};
static vr_crouch_state s_vr_crouch; // static storage: zero-initialised, not calibrated

// console command vr_stand_calibrate (console_commands.cpp)
void vr_stand_calibrate_request()
{
	s_vr_crouch.calib_request = true;
	Msg("* [vr3] stand calibration requested (at the next VR read)");
}

static int vr_crouch_step(int s, float r)
{
	for (int i = 0; i < 3; ++i) // a jump of r may cross more than one threshold in one read
	{
		int n = s;
		if (s == 0 && r < 0.90f)
			n = 1;
		else if (s == 1 && r > 0.93f)
			n = 0;
		else if (s == 1 && r < 0.49f)
			n = 2;
		else if (s == 2 && r > 0.52f)
			n = 1;
		if (n == s)
			break;
		s = n;
	}
	return s;
}

// once per game update from vr_loco_update
static void vr_crouch_update(const vr_input_info& I, CActor* A, bool allowed, bool new_read)
{
	if (!vr_m3_vr())
		return;
	vr_crouch_state& C = s_vr_crouch;
	C.dev_frame = Device.dwFrame;
	if (A != C.actor)
	{
		C.actor = A; // new / replaced actor (level loaded): calibrate again
		C.calibrated = false;
		C.want = 0;
		C.applied = 0;
		C.dy = 0.f;
		C.btn = false;
	}
	if (!A || !A->g_Alive())
		C.btn = false; // dead: the button crouch is dropped
	if (!new_read)
		return;
	// crouch button (opt-in action, no default binding): a rising edge on either hand toggles the flag
	bool btn_edge = false;
	C.btn_raw = false;
	for (int h = 0; h < 2; ++h)
	{
		const bool b = I.crouch_btn_active[h] && I.crouch_btn[h];
		if (b && !C.btn_prev[h])
			btn_edge = true;
		C.btn_prev[h] = b;
		C.btn_raw = C.btn_raw || b;
	}
	if (btn_edge && allowed && A && !A->Holder())
		C.btn = !C.btn;
	if (!A || !I.head.valid)
		return;
	if (!C.calibrated || C.calib_request)
	{
		C.calib_request = false;
		C.calibrated = true;
		C.y_calib = I.head.pos.y;
		LPCSTR src = "default";
		float h = 1.66f;
		if (vr_stand_height_setting(h))
			src = "setting";
		else if (I.floor_valid)
		{
			h = I.floor_head_y;
			src = "floor";
		}
		else
			h = 1.66f;
		clamp(h, 1.f, 2.2f);
		C.H = h;
		// the view path maps the START head pose (first VR frame) onto the VR base, so the base is lowered by the head's
		// height at calibration above that start pose: at the calibrated pose the eyes are at the standing eye height
		C.off = g_vr_track.head0_set ? C.y_calib - g_vr_track.head0_y : 0.f;
		Msg("* [vr3] stand height %.3f m (source %s)", C.H, src);
	}
	C.dy = I.head.pos.y - C.y_calib;
	C.r = (C.H + C.dy) / C.H;
	C.want = vr_crouch_physical() ? vr_crouch_step(C.want, C.r) : 0;
}

// for CActor::g_cl_CheckControls (Actor_Movement.cpp): false = leave the crouch bits alone (flat run, other actor, the VR
// path did not run this update, or nothing VR-requested to undo). Else the bits to set / clear in the wish bits, by state:
// 0 = mcCrouch off (and mcAccel off when leaving a low crouch), 1 = mcCrouch on, mcAccel off (accelerated crouch = box 1),
// 2 = mcCrouch and mcAccel on (not accelerated crouch = low crouch, box 2), 3 = button crouch: mcCrouch on (the game's
// own crouch). Physical crouch (calibrated): the physical state, or 3 when it is 0 and the button flag is on. Without
// physical crouch (-vr_crouch_off or not calibrated yet): 3 while the button flag is on, one 0 when it goes off.
bool vr_crouch_bits(const CActor* A, u32& set, u32& clear)
{
	set = clear = 0;
	const vr_crouch_state& C = s_vr_crouch;
	if (!vr_m3_vr() || !A || A != C.actor)
		return false;
	if (C.dev_frame != Device.dwFrame && C.dev_frame + 1 != Device.dwFrame)
		return false; // stale
	int st;
	if (vr_crouch_physical() && C.calibrated)
		st = (C.want == 0 && C.btn) ? 3 : C.want;
	else
		st = C.btn ? 3 : (C.applied == 3 ? 0 : -1);
	if (st < 0)
		return false;
	if (st == 0)
		clear = mcCrouch | (C.applied == 2 ? mcAccel : 0);
	else if (st == 1)
	{
		set = mcCrouch;
		clear = mcAccel;
	}
	else if (st == 2)
		set = mcCrouch | mcAccel;
	else
	{
		set = mcCrouch;
		clear = C.applied == 2 ? mcAccel : 0;
	}
	s_vr_crouch.applied = st;
	return true;
}
// for ActorCameras.cpp (VR base, -vr_m3 -vr_openxr only): the base height above the feet. -vr_crouch_off: the game's eased
// camera height, as before. Physical crouch: the STANDING camera height (the head's own tracked height lowers the view
// 1:1; the game's crouch does not lower it again), or the game's eased height while the button crouch is on and the head
// is standing; both minus the calibration offset.
float vr_base_height(const CActor* A, float current_h, float standing_h)
{
	if (!vr_crouch_physical())
		return current_h;
	const vr_crouch_state& C = s_vr_crouch;
	const bool mine = A && A == C.actor && C.calibrated;
	const float h = (mine && C.btn && C.want == 0) ? current_h : standing_h;
	return h - (mine ? C.off : 0.f);
}
// for CActor::g_cl_CheckControls: the wish bits it used (log only)
void vr_crouch_note(u32 wf)
{
	if (vr_m3_vr())
		s_vr_crouch.wf = wf;
}

// milestone 7b: next firearm on the gun hand's upper face button. One switch per press (rising edge of a NEW read): as a
// player pressing the slot key (CActor::OnNextWeaponSlot does the same: kWPN_1 + slot - KNIFE_SLOT), cycling slots 2 and 3,
// wrapping, skipping an empty one, never the active one. Returns 1 when a key press was sent.
static int vr_switch_update(const vr_input_info& I, CActor* A, bool allowed, bool new_read)
{
	static bool s_prev = false;
	if (!new_read)
		return 0;
	const int gun = I.gun ? 1 : 0;
	const bool now = I.next_active[gun] && I.next[gun];
	const bool edge = now && !s_prev;
	s_prev = now; // a press seen while input is not allowed is consumed, not kept for later
	if (!edge || !allowed || !vr_m3_vr() || !A || A->Holder() || !A->m_inventory)
		return 0;
	CInventory& inv = A->inventory();
	const u16 cur = inv.GetActiveSlot();
	u16 to = NO_ACTIVE_SLOT;
	if (cur == INV_SLOT_2)
	{
		if (inv.ItemFromSlot(INV_SLOT_3))
			to = INV_SLOT_3;
	}
	else if (cur == INV_SLOT_3)
	{
		if (inv.ItemFromSlot(INV_SLOT_2))
			to = INV_SLOT_2;
	}
	else if (inv.ItemFromSlot(INV_SLOT_2))
		to = INV_SLOT_2;
	else if (inv.ItemFromSlot(INV_SLOT_3))
		to = INV_SLOT_3;
	if (to == NO_ACTIVE_SLOT)
		return 0;
	A->IR_OnKeyboardPress(kWPN_1 + int(to) - int(KNIFE_SLOT));
	return 1;
}

// flat (horizontal) unit WORLD forward of a tracking-space pose: the pose's forward axis (engine +z = OpenXR -Z, the same
// axis as ctrl_aim of the gun hand), mapped with the track_to_world of the frame the pose belongs to, then turned by the
// VR yaw added since that frame was built (yaw_since). False when the pose is invalid or (nearly) vertical (horizontal
// part below 0.1).
static bool vr_flat_fwd(const vr_pose& p, float yaw_since, Fvector& out)
{
	out.set(0.f, 0.f, 0.f);
	if (!p.valid)
		return false;
	Fmatrix m;
	vr_pose_matrix(p, m);
	Fvector w;
	g_vr_track.track_to_world.transform_dir(w, m.k);
	vr_rot_y(w, yaw_since);
	w.y = 0.f;
	const float l = w.magnitude();
	if (l < 0.1f)
		return false;
	out.set(w.x / l, 0.f, w.z / l);
	return true;
}

// Once per game update, after the button mapping. I = the last controller read (VR frame I.frame, read in the previous
// render); new_read = I is a read not seen by an earlier update; allowed = the same checks a key press passes
// (vr_fire_allowed: live current actor, not paused, no menu / console / script input lock).
static void vr_loco_update(const vr_input_info& I, CActor* A, bool allowed, bool new_read)
{
	static bool s_started = false;
	static int s_from = -1, s_to = -1;
	if (!s_started)
	{
		s_started = true;
		if (LPCSTR p = strstr(Core.Params, "-vr_dbg_base "))
			if (sscanf(p + 13, "%d %d", &s_from, &s_to) != 2)
				s_from = s_to = -1;
		Msg("* [vr3] walk: %s, left stick, dead zone 0.10", vr_walk_head() ? "head-relative" : "controller-relative");
		if (vr_turn_on())
			Msg("* [vr3] turn: %s, right stick", vr_turn_smooth() ? "smooth 120 deg/s" : "snap 45 deg, repeat 0.50 s");
		else
			Msg("* [vr3] turn: off (head and body only), right stick unused");
		Msg(vr_crouch_physical() ? "* [vr3] crouch: physical (crouch below 0.90, stand above 0.93, low below 0.49, back above 0.52)"
			: "* [vr3] crouch: physical crouch off");
		Msg("* [vr3] next firearm: gun-hand upper face button (slots 2, 3)");
	}
	static bool s_armed = true;
	static float s_timer = 0.f;

	s_vr_walk.on = false;
	s_vr_walk.dev_frame = Device.dwFrame;

	const float lx = I.stick_active[0] ? I.stick[0][0] : 0.f, ly = I.stick_active[0] ? I.stick[0][1] : 0.f;
	const float rx = I.stick_active[1] ? I.stick[1][0] : 0.f;
	const float dt = float(Device.dwTimeDelta) / 1000.f; // the actor update's step (its DT is in ms of the same clock)

	CCameraBase* cam = A ? A->cam_Active() : nullptr;
	const bool base_ok = vr_m3_vr() && g_vr_base.yaw_set && g_vr_track.base_used;
	const bool ok = allowed && base_ok && cam;
	// turning also needs a free camera (no vehicle / weapon holder, no yaw clamp as on a ladder): else no turn at all
	const bool turn_ok = vr_turn_on() && ok && !A->Holder() && !cam->bClampYaw;

	// ---- turning (right stick x, either gun hand)
	float turn = 0.f;
	int snap = 0;
	const float ax = _abs(rx);
	if (!turn_ok)
	{
		s_armed = true;
		s_timer = 0.f;
	}
	else if (vr_turn_smooth())
	{
		if (ax > 0.1f)
			turn = deg2rad(120.f) * (rx > 0.f ? 1.f : -1.f) * ((_min(ax, 1.f) - 0.1f) / 0.9f) * dt;
	}
	else
	{
		if (ax < 0.5f)
		{
			s_armed = true;
			s_timer = 0.f;
		}
		else if (s_armed)
		{
			if (ax >= 0.75f)
			{
				snap = rx > 0.f ? 1 : -1;
				s_armed = false;
				s_timer = 0.f;
			}
		}
		else
		{
			s_timer += dt;
			if (s_timer >= 0.5f)
			{
				snap = rx > 0.f ? 1 : -1;
				s_timer = 0.f;
			}
		}
		if (snap)
			turn = float(snap) * PI_DIV_4;
	}
	if (turn != 0.f)
	{
		// keep the head where it is: the view pivots about the start-head point (pivot = VR base pos + pivot offset);
		// d = head - pivot as the next render would show it before this turn (the last rendered frame's d, turned by any
		// VR yaw added since that frame was built), horizontal part; offset += d - Rot(turn) d
		Fvector d;
		d.sub(g_vr_track.head_world, g_vr_track.pivot);
		d.y = 0.f;
		vr_rot_y(d, g_vr_base.yaw - g_vr_track.base_yaw);
		Fvector rd = d;
		vr_rot_y(rd, turn);
		g_vr_base.pivot_off.x += d.x - rd.x;
		g_vr_base.pivot_off.z += d.z - rd.z;
		vr_base_set_yaw(g_vr_base.yaw + turn);
		// body / aim: the active camera's yaw; CActor::g_cl_Orientate copies it into r_torso.yaw (= -cam yaw) on the
		// actor's next update, and r_model_yaw follows r_torso (at once while moving, else by its turn lerp)
		cam->yaw += turn;
	}

	// ---- walking (left stick, either gun hand)
	const float yaw_since = base_ok ? g_vr_base.yaw - g_vr_track.base_yaw : 0.f; // includes this update's turn
	Fvector lfwd, hfwd, W;
	const bool lvalid = base_ok && vr_flat_fwd(I.grip[0], yaw_since, lfwd);
	const bool hvalid = base_ok && vr_flat_fwd(I.head, yaw_since, hfwd);
	if (!lvalid)
		lfwd.set(0.f, 0.f, 0.f);
	if (!hvalid)
		hfwd.set(0.f, 0.f, 0.f);
	W.set(0.f, 0.f, 0.f);
	float k = 0.f;
	const float m = _sqrt(lx * lx + ly * ly);
	if (ok && m > 0.1f)
	{
		Fvector F;
		bool fok = false;
		if (!vr_walk_head() && lvalid)
		{
			F.set(lfwd);
			fok = true;
		}
		else if (hvalid)
		{
			F.set(hfwd);
			fok = true;
		}
		if (fok)
		{
			Fvector R = F;
			vr_rot_y(R, PI_DIV_2); // 90 degrees clockwise seen from above
			W.set(F.x * ly + R.x * lx, 0.f, F.z * ly + R.z * lx);
			const float wl = W.magnitude();
			if (wl > EPS)
			{
				W.div(wl);
				k = _min(1.f, (m - 0.1f) / 0.9f);
				s_vr_walk.on = true;
				s_vr_walk.dir.set(W);
				s_vr_walk.k = k;
			}
			else
				W.set(0.f, 0.f, 0.f);
		}
	}

	// ---- milestone 7b (Amendment 23): physical crouch (calibration, ratio, hysteresis) and the weapon switch
	vr_crouch_update(I, A, allowed, new_read);
	const int sw = vr_switch_update(I, A, allowed, new_read);

	// ---- log: one line per VR frame read (and an extra one for an update that turned on an already logged read)
	if (s_from >= 0 && int(I.frame) >= s_from && int(I.frame) <= s_to && (new_read || turn != 0.f))
	{
		const Fvector& hw = g_vr_track.head_world;
		const Fvector& ap = g_vr_track.actor_pos;
		Msg("* [vr3loc] frame %u stickL %.3f %.3f stickR %.3f %.3f active %d %d lfwd %.5f %.5f %.5f lvalid %d hfwd %.5f %.5f %.5f head_w %.5f %.5f %.5f actor %.5f %.5f %.5f walk_dir %.5f %.5f %.5f walk_k %.4f vr_yaw %.6f body_yaw %.6f snap %d turn_deg %.4f mode %s/%s",
			I.frame, I.stick[0][0], I.stick[0][1], I.stick[1][0], I.stick[1][1], I.stick_active[0] ? 1 : 0, I.stick_active[1] ? 1 : 0,
			lfwd.x, lfwd.y, lfwd.z, lvalid ? 1 : 0, hfwd.x, hfwd.y, hfwd.z, hw.x, hw.y, hw.z, ap.x, ap.y, ap.z,
			W.x, W.y, W.z, k, g_vr_base.yaw, cam ? cam->yaw : 0.f, snap, rad2deg(turn),
			vr_walk_head() ? "head" : "ctl", !vr_turn_on() ? "off" : vr_turn_smooth() ? "smooth" : "snap");
		// milestone 7b: view_h = the view (head) height above the feet of this frame's render (head_world and the actor
		// position its VR base came from); box = the movement control's active body box; crouch_wish = mcCrouch in the wish
		// bits the actor's last update used
		const vr_crouch_state& C = s_vr_crouch;
		CPHMovementControl* mc = (A && A->character_physics_support()) ? A->character_physics_support()->movement() : nullptr;
		const u32 real = A ? A->MovingState() : 0;
		const bool inv_ok = A && A->m_inventory;
		PIItem act_item = inv_ok ? A->inventory().ActiveItem() : nullptr;
		LPCSTR act_sect = (act_item && act_item->m_section_id.size()) ? act_item->m_section_id.c_str() : "none";
		Msg("* [vr3act] frame %u head_dy %.5f stand_h %.3f ratio %.4f want %d box %d crouch_wish %d crouch_state %d accel_state %d view_h %.5f next_btn %d switch %d slot %u section %s crouch_btn %d btn_flag %d fall %u",
			I.frame, C.calibrated ? C.dy : 0.f, C.calibrated ? C.H : 0.f, C.calibrated ? C.r : 1.f, C.want,
			mc ? int(mc->BoxID()) : -1, (C.wf & mcCrouch) ? 1 : 0, (real & mcCrouch) ? 1 : 0, (real & mcAccel) ? 1 : 0,
			hw.y - ap.y, (I.next_active[I.gun ? 1 : 0] && I.next[I.gun ? 1 : 0]) ? 1 : 0, sw,
			inv_ok ? u32(A->inventory().GetActiveSlot()) : 0u, act_sect, C.btn_raw ? 1 : 0, C.btn ? 1 : 0, g_vr_base.fall_count);
	}
}

void vr_input_update()
{
	static bool s_trig[2] = { false, false }, s_grab[2] = { false, false };
	static u32 s_seq = 0;
	static bool s_seen = false;
	static bool s_fire_sent = false;    // a kWPN_FIRE press was given to s_fire_actor and not yet released
	static CActor* s_fire_actor = nullptr;
	static int s_from = -1, s_to = -1;
	static bool s_parsed = false;
	if (!s_parsed)
	{
		s_parsed = true;
		if (LPCSTR p = strstr(Core.Params, "-vr_dbg_grip "))
			if (sscanf(p + 13, "%d %d", &s_from, &s_to) != 2)
				s_from = s_to = -1;
	}

	vr_input_info I;
	if (!g_vr_get_input || !g_vr_get_input(&I))
	{
		s_vr_walk.on = false; // milestone 7: no input, no VR walking
		return;
	}
	const int gun = I.gun ? 1 : 0, off = 1 - gun;
	bool fire_press_edge = false;
	const bool new_read = !s_seen || I.seq != s_seq;
	if (new_read)
	{
		s_seen = true;
		s_seq = I.seq;
		for (int h = 0; h < 2; ++h)
		{
			const float tv = I.trigger_active[h] ? I.trigger[h] : 0.f;
			const float gv = I.grab_active[h] ? I.grab[h] : 0.f;
			if (vr_hyst_step(s_trig[h], tv))
			{
				Msg("* [vr3in] frame %u hand %c %s %s value %.3f", I.frame, h ? 'R' : 'L', "trigger", s_trig[h] ? "press" : "release", tv);
				if (h == gun && s_trig[h])
					fire_press_edge = true;
			}
			if (vr_hyst_step(s_grab[h], gv))
				Msg("* [vr3in] frame %u hand %c %s %s value %.3f", I.frame, h ? 'R' : 'L', "grab", s_grab[h] ? "press" : "release", gv);
		}
		s_vr_grab_held = s_grab[off];
		// milestone 10a steps 1-2: the PDA in either hand (grip in the chest zone, or passed); the hand holding it does nothing else
		const int pda_block = vr_pda_update(I, g_actor, vr_fire_allowed(g_actor), s_grab, s_trig, s_from >= 0 && int(I.frame) >= s_from && int(I.frame) <= s_to);
		if (pda_block & 4)
			fire_press_edge = false; // step 3: the gun hand's trigger clicked the PDA, not the gun
		if (pda_block & (1 << off))
			s_vr_grab_held = false;
		// milestone 8a (Amendment 31): manual reload
		vr_reload_update(I, g_actor, vr_fire_allowed(g_actor), s_vr_grab_held, s_from >= 0 && int(I.frame) >= s_from && int(I.frame) <= s_to);
		// milestone 9a (Amendment 32): holsters (gun hand's grip)
		vr_holster_update(I, g_actor, vr_fire_allowed(g_actor), s_grab[gun] && !(pda_block & (1 << gun)), s_from >= 0 && int(I.frame) >= s_from && int(I.frame) <= s_to);
		if (s_from >= 0 && int(I.frame) >= s_from && int(I.frame) <= s_to)
			Msg("* [vr3inp] frame %u L trigger %.3f grab %.3f active %d %d R trigger %.3f grab %.3f active %d %d gun_hand %c fire_pressed %d grab_held %d",
				I.frame, I.trigger[0], I.grab[0], I.trigger_active[0] ? 1 : 0, I.grab_active[0] ? 1 : 0,
				I.trigger[1], I.grab[1], I.trigger_active[1] ? 1 : 0, I.grab_active[1] ? 1 : 0,
				gun ? 'R' : 'L', s_trig[gun] ? 1 : 0, s_vr_grab_held ? 1 : 0);
	}

	// gun hand trigger -> kWPN_FIRE, the same actor entry point a key press reaches (CActor::IR_OnKeyboardPress/Release)
	CActor* A = g_actor;
	if (s_fire_sent && s_fire_actor != A)
	{
		s_fire_sent = false; // the actor the press went to is gone or replaced: nothing to release on it
		s_fire_actor = nullptr;
	}
	const bool allowed = vr_fire_allowed(A);
	if (s_fire_sent && (!s_trig[gun] || !allowed))
	{
		A->IR_OnKeyboardRelease(kWPN_FIRE);
		s_fire_sent = false;
		s_fire_actor = nullptr;
		Msg("* [vr3key] frame %u kWPN_FIRE release%s", I.frame, allowed ? "" : " (input no longer allowed)");
	}
	if (fire_press_edge)
	{
		if (allowed && !s_fire_sent)
		{
			A->IR_OnKeyboardPress(kWPN_FIRE);
			s_fire_sent = true;
			s_fire_actor = A;
			Msg("* [vr3key] frame %u kWPN_FIRE press", I.frame);
		}
		else if (!allowed)
			Msg("* [vr3key] frame %u kWPN_FIRE press blocked (no live current actor, paused, menu, console or input disabled)", I.frame);
	}
	// milestone 7 (Amendment 20): stick walking and turning
	vr_loco_update(I, A, allowed, new_read);
}

// out = v made perpendicular to the unit vector axis (Gram-Schmidt), unit; false when v is (nearly) parallel to axis
// (perpendicular part below 0.02 of |v|, i.e. within about 1.1 degrees)
static bool vr_perp_unit(const Fvector& v, const Fvector& axis, Fvector& out)
{
	out.mad(v, axis, -v.dotproduct(axis));
	const float m = out.magnitude();
	const float vm = v.magnitude();
	if (!(vm > EPS) || m < 0.02f * vm)
		return false;
	out.div(m);
	return true;
}

// The held item's barrel direction in its MODEL space (unit): the same choice as vr_gun_barrel_world (the -vr_barrel_bones
// bone line rear -> front, else the fire axis m_fire_direction), else the grip frame's forward axis (model z axis without
// a grip offset). A direction only: world direction = draw transform's transform_dir of it (normalized).
static void vr_gun_barrel_model(attachable_hud_item* item, Fvector& b)
{
	const vr_3b_switches& S = vr_3b();
	if (S.barrel && item->m_model)
	{
		IKinematics* K = item->m_model;
		const u16 br = vr_find_bone(K, S.rear), bf = vr_find_bone(K, S.front);
		if (br != BI_NONE && bf != BI_NONE)
		{
			b.sub(K->LL_GetTransform(bf).c, K->LL_GetTransform(br).c);
			if (b.magnitude() > EPS)
			{
				b.normalize();
				return;
			}
		}
	}
	const hud_item_measures& M = item->m_measures;
	if (M.m_prop_flags.test(hud_item_measures::e_fire_point) && M.m_fire_bone != BI_NONE && item->m_model
		&& M.m_fire_direction.magnitude() > EPS)
	{
		b.set(M.m_fire_direction);
		b.normalize();
		return;
	}
	if (s_vr_grip_on)
		b.set(s_vr_grip_frame.k);
	else
		b.set(0.f, 0.f, 1.f);
	b.normalize_safe();
}

// values of the last placement, for the [vr3grip] line (world space; see vr_place_at_grip)
struct vr_2h_log_state
{
	bool set;
	u32 frame;
	bool grabbed;     // drawn two-handed this frame
	bool off_valid;
	Fvector off_pos;  // off-hand controller position
	Fvector main_pos; // main controller position (the grip point)
	Fvector gup;      // drawn grip frame's up (y) axis, AFTER the kick
	Fvector cup;      // main controller's up axis
	Fvector bnk;      // drawn barrel direction WITHOUT the kick
};
static vr_2h_log_state s_vr_2h_log; // static storage: zero-initialised, set = false

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
	// milestone 4 fields (appended): kick (camera convention, radians), actor camera yaw/pitch (radians, the values
	// ChangeHP modifies), world muzzle and barrel direction as placed this frame (= g_vr_fire, what the next shot uses),
	// controller aim axis in the world (forward axis of the pose the gun is placed on, no grip offset, no kick)
	string64 aim_txt, mz_txt, bw_txt, ca_txt;
	CCameraBase* ac = g_actor ? g_actor->cam_Active() : nullptr;
	if (ac)
		xr_sprintf(aim_txt, "%.6f %.6f", ac->yaw, ac->pitch);
	else
		xr_strcpy(aim_txt, "nan nan");
	if (g_vr_fire.valid && g_vr_fire.frame == g_vr_track.frame)
	{
		xr_sprintf(mz_txt, "%.5f %.5f %.5f", g_vr_fire.muzzle.x, g_vr_fire.muzzle.y, g_vr_fire.muzzle.z);
		xr_sprintf(bw_txt, "%.6f %.6f %.6f", g_vr_fire.dir.x, g_vr_fire.dir.y, g_vr_fire.dir.z);
	}
	else
	{
		xr_strcpy(mz_txt, "nan nan nan");
		xr_strcpy(bw_txt, "nan nan nan");
	}
	if (G.gun.valid)
	{
		Fmatrix gm;
		vr_pose_matrix(G.gun, gm);
		Fvector ca;
		g_vr_track.track_to_world.transform_dir(ca, gm.k);
		ca.normalize_safe();
		xr_sprintf(ca_txt, "%.6f %.6f %.6f", ca.x, ca.y, ca.z);
	}
	else
		xr_strcpy(ca_txt, "nan nan nan");
	// milestone 5 fields (appended): 2h = drawn two-handed this frame; off_pos / main_pos = off-hand and main controller
	// positions (world); gup = drawn grip frame's up axis after the kick (world); cup = main controller's up axis (world);
	// bnk = drawn barrel direction without the kick (world). nan when this frame was not placed.
	string512 h2_txt;
	const vr_2h_log_state& L2 = s_vr_2h_log;
	if (L2.set && L2.frame == g_vr_track.frame)
	{
		string64 off_txt;
		if (L2.off_valid)
			xr_sprintf(off_txt, "%.5f %.5f %.5f", L2.off_pos.x, L2.off_pos.y, L2.off_pos.z);
		else
			xr_strcpy(off_txt, "nan nan nan");
		xr_sprintf(h2_txt, "2h %d off_valid %d off_pos %s main_pos %.5f %.5f %.5f gup %.6f %.6f %.6f cup %.6f %.6f %.6f bnk %.6f %.6f %.6f",
			L2.grabbed ? 1 : 0, L2.off_valid ? 1 : 0, off_txt, L2.main_pos.x, L2.main_pos.y, L2.main_pos.z,
			L2.gup.x, L2.gup.y, L2.gup.z, L2.cup.x, L2.cup.y, L2.cup.z, L2.bnk.x, L2.bnk.y, L2.bnk.z);
	}
	else
		xr_strcpy(h2_txt, "2h 0 off_valid 0 off_pos nan nan nan main_pos nan nan nan gup nan nan nan cup nan nan nan bnk nan nan nan");
	const vr_pose& R = G.runtime;
	Msg("* [vr3grip] frame %u t_ns %lld hand %c valid %d grip_rt_pos %.5f %.5f %.5f grip_rt_quat %.6f %.6f %.6f %.6f xr_pos %.5f %.5f %.5f xr_quat %.6f %.6f %.6f %.6f gun_track %.5f %.5f %.5f grip_pt %s barrel %s lag %d kick %.6f %.6f aim %s muzzle %s bworld %s ctrl_aim %s %s",
		g_vr_track.frame, G.t_ns, G.hand, R.valid ? 1 : 0, R.pos.x, R.pos.y, R.pos.z, R.qx, R.qy, R.qz, R.qw,
		G.xr_pos[0], G.xr_pos[1], G.xr_pos[2], G.xr_quat[0], G.xr_quat[1], G.xr_quat[2], G.xr_quat[3],
		gt.x, gt.y, gt.z, gp_txt, bar_txt, G.lag, g_vr_kick.pitch, g_vr_kick.yaw, aim_txt, mz_txt, bw_txt, ca_txt, h2_txt);
}

static bool vr_mag_log_window(); // below (milestone 8b); also gates the 10a [vr3flat] line

bool player_hud::vr_place_at_grip()
{
	attachable_hud_item* item = m_attached_items[0] ? m_attached_items[0] : m_attached_items[1];
	vr_grip_info G;
	if (!item || !g_vr_get_grip || !g_vr_get_grip(&G) || !G.gun.valid)
		return false;

	Fmatrix gun_track, target;
	vr_pose_matrix(G.gun, gun_track);
	target.mul_43(g_vr_track.track_to_world, gun_track); // grip pose first, then tracking space -> world
	const Fmatrix grip_world = target; // milestone 4: the controller grip pose in the world (rigid), pivot of the kick

	// milestone 10a note 9: with the game's PDA as the active item, its own (flat game) pose relative to the HUD base
	// (m_transform, the camera the flat game holds it in front of), before the VR placement replaces it
	if (vr_mag_log_window() && item->m_parent_hud_item && item->m_parent_hud_item->has_object() &&
		smart_cast<CPda*>(&item->m_parent_hud_item->object()))
	{
		static u32 s_flat_frame = u32(-1);
		if (s_flat_frame != g_vr_track.frame)
		{
			s_flat_frame = g_vr_track.frame;
			Fmatrix inv, rel;
			inv.invert(m_transform);
			rel.mul_43(inv, item->m_item_transform);
			Fvector a[3] = {rel.i, rel.j, rel.k};
			for (Fvector& v : a)
				v.normalize_safe();
			Fvector toeye = Fvector().set(rel.c).invert();
			toeye.normalize_safe();
			const float facing = rad2deg(acosf(_max(-1.f, _min(1.f, a[1].dotproduct(toeye)))));
			Msg("* [vr3flat] frame %u item %s pos %.5f %.5f %.5f i %.5f %.5f %.5f j %.5f %.5f %.5f k %.5f %.5f %.5f facing_deg %.2f",
				g_vr_track.frame, item->m_sect_name.c_str(), rel.c.x, rel.c.y, rel.c.z, a[0].x, a[0].y, a[0].z,
				a[1].x, a[1].y, a[1].z, a[2].x, a[2].y, a[2].z, facing);
		}
	}
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

	// milestone 5 (Amendment 18): two-handed grip. 'target' is now the ONE-handed pose (no kick). Grip point p = grip_world.c
	// (the main controller position, world). One-handed barrel b1 = target * barrel (model, vr_gun_barrel_model); support
	// point = p + support_dist * b1. Grab state, once per VR frame: input held (vr_2h_input_held, never with -vr_2h_off or
	// without a support point) and off-hand pose valid; begins when the off-hand controller is within 0.15 m of the
	// support point, then stays while the input is held (no distance break). While grabbed: rotation Q about p taking the
	// barrel b1 to d = unit(off - p) and the gun's up u1 (grip frame y made perpendicular to b1) to u2 = the main
	// controller's up made perpendicular to d (degenerate: previous frame's up). No blend (snap). The kick follows as before.
	vr_2h(); // parse the switches and print the two-hand start line at the first VR placement
	Fvector v2_frame_j, v2_b_model;
	if (s_vr_grip_on)
		v2_frame_j.set(s_vr_grip_frame.j);
	else
		v2_frame_j.set(0.f, 1.f, 0.f);
	vr_gun_barrel_model(item, v2_b_model);
	const Fvector v2_p = grip_world.c;
	Fvector v2_cup, v2_off;
	g_vr_track.track_to_world.transform_dir(v2_cup, gun_track.j);
	v2_cup.normalize_safe();
	const bool v2_off_valid = G.off.valid;
	if (v2_off_valid)
		g_vr_track.track_to_world.transform_tiny(v2_off, G.off.pos);
	else
		v2_off.set(0.f, 0.f, 0.f);
	bool v2_grabbed = false;
	{
		static Fvector s_prev_up;
		static bool s_prev_up_set = false;
		static u32 s_state_frame = u32(-1);
		Fvector b1, gy1, u1;
		target.transform_dir(b1, v2_b_model);
		b1.normalize_safe();
		target.transform_dir(gy1, v2_frame_j);
		gy1.normalize_safe();
		if (s_state_frame != g_vr_track.frame)
		{
			s_state_frame = g_vr_track.frame;
			// milestone 8a (Amendment 31): where the off hand is against the magazine point, the support point and the
			// pouch zone (0.60 m below the head, 0.20 m to the off-hand side, 0.05 m forward, turned with the head's yaw)
			bool rl_claims = false;
			{
				vr_reload_state& RS = s_vr_rl;
				Fvector ru, mp, sp2, hf, hr, pc;
				if (!vr_perp_unit(gy1, b1, ru) && !vr_perp_unit(v2_cup, b1, ru))
					ru.set(0.f, 1.f, 0.f);
				mp.mad(v2_p, b1, RS.fwd);
				mp.mad(ru, -RS.down);
				sp2.mad(v2_p, b1, s_vr_support_dist);
				hf.set(Device.vCameraDirection.x, 0.f, Device.vCameraDirection.z);
				if (hf.magnitude() < 0.01f)
					hf.set(0.f, 0.f, 1.f);
				hf.normalize();
				hr.set(hf.z, 0.f, -hf.x);
				pc.set(g_vr_track.head_world);
				pc.y -= 0.60f;
				pc.mad(hr, RS.gun_right ? -0.20f : 0.20f);
				pc.mad(hf, 0.05f);
				RS.geo = v2_off_valid;
				RS.geo_frame = g_vr_track.frame;
				RS.d_mag = v2_off.distance_to(mp);
				RS.d_sup = s_vr_support_on ? v2_off.distance_to(sp2) : 1000.f;
				RS.d_pouch = v2_off.distance_to(pc);
				RS.off_world.set(v2_off);
				Fmatrix inv;
				inv.invert(g_vr_track.track_to_world);
				inv.transform_tiny(RS.mag_track, mp);
				inv.transform_tiny(RS.pouch_track, pc);
				inv.transform_tiny(RS.sup_track, sp2);
				// a hand holding a magazine, or placed where a grip press belongs to the reload, does not start a two-handed grab
				rl_claims = RS.active && (RS.hand_mag || (RS.geo && (RS.d_pouch <= 0.15f || (RS.mag_in && RS.d_mag <= 0.10f && RS.d_mag < RS.d_sup))));
				if (RS.active && RS.hand_mag)
					s_vr_2h_grabbed = false;
			}
			if (!s_vr_support_on || !v2_off_valid || !vr_2h_input_held())
				s_vr_2h_grabbed = false;
			else if (!s_vr_2h_grabbed && !rl_claims)
			{
				Fvector sp;
				sp.mad(v2_p, b1, s_vr_support_dist);
				s_vr_2h_grabbed = sp.distance_to(v2_off) <= 0.15f;
			}
		}
		Fvector ax_x, ax_z;
		ax_x.set(1.f, 0.f, 0.f);
		ax_z.set(0.f, 0.f, 1.f);
		if (!vr_perp_unit(gy1, b1, u1) && !vr_perp_unit(v2_cup, b1, u1) && !vr_perp_unit(ax_x, b1, u1))
			vr_perp_unit(ax_z, b1, u1);
		Fvector d;
		d.set(0.f, 0.f, 1.f);
		v2_grabbed = s_vr_2h_grabbed;
		if (v2_grabbed)
		{
			d.sub(v2_off, v2_p);
			if (d.magnitude() < 1e-4f)
				v2_grabbed = false; // off hand on the grip point: no direction, drawn one-handed this frame
			else
				d.normalize();
		}
		if (v2_grabbed)
		{
			Fvector u2;
			if (!vr_perp_unit(v2_cup, d, u2) && !(s_prev_up_set && vr_perp_unit(s_prev_up, d, u2))
				&& !vr_perp_unit(u1, d, u2) && !vr_perp_unit(ax_x, d, u2))
				vr_perp_unit(ax_z, d, u2);
			Fvector r1, r2;
			r1.crossproduct(u1, b1);
			r2.crossproduct(u2, d);
			// Q v = r2 (r1.v) + u2 (u1.v) + d (b1.v): the rotation taking (r1, u1, b1) to (r2, u2, d)
			Fvector* rows[3] = { &target.i, &target.j, &target.k };
			Fvector rel;
			rel.sub(target.c, v2_p);
			for (int n = 0; n < 4; ++n)
			{
				Fvector& v = n < 3 ? *rows[n] : rel;
				const float a = r1.dotproduct(v), b = u1.dotproduct(v), c = b1.dotproduct(v);
				Fvector q;
				q.set(r2.x * a + u2.x * b + d.x * c, r2.y * a + u2.y * b + d.y * c, r2.z * a + u2.z * b + d.z * c);
				v.set(q);
			}
			target.c.add(v2_p, rel);
			s_prev_up.set(u2);
		}
		else
			s_prev_up.set(u1);
		s_prev_up_set = true;
	}
	Fvector v2_bnk; // drawn barrel direction, world, before the kick
	target.transform_dir(v2_bnk, v2_b_model);
	v2_bnk.normalize_safe();

	// milestone 4 (Amendment 16): the recoil kick turns the gun about the grip point, which stays on the controller
	// position. In the grip pose's own axes (x right, y up, z forward): pitch up about x first, then yaw right about y
	// (same matrix conventions as the grip frame above: rotateX(-up) pitches up, rotateY(right) yaws right). The kick is
	// kept in camera convention (pitch > 0 = down, yaw > 0 = right), so up = -kick.pitch, right = kick.yaw.
	// World: K = G * L * inverse(G), G = grip pose in the world; item = K * item. G.c is a fixed point of K. Skipped when
	// the kick is exactly zero (always, with -vr_recoil_old), so the placement is then bit-identical to before.
	if (g_vr_kick.pitch != 0.f || g_vr_kick.yaw != 0.f)
	{
		Fmatrix Ry, Rx, L, G_inv, K;
		Ry.rotateY(g_vr_kick.yaw);
		Rx.rotateX(g_vr_kick.pitch); // = rotateX(-up)
		L.mul_43(Ry, Rx);            // pitch first, then yaw
		G_inv.invert(grip_world);
		K.mul_43(grip_world, L);
		K.mulB_43(G_inv);
		target.mulA_43(K);
	}

	// milestone 5: values for the [vr3grip] line (world space)
	s_vr_2h_log.set = true;
	s_vr_2h_log.frame = g_vr_track.frame;
	s_vr_2h_log.grabbed = v2_grabbed;
	s_vr_2h_log.off_valid = v2_off_valid;
	s_vr_2h_log.off_pos.set(v2_off);
	s_vr_2h_log.main_pos.set(v2_p);
	target.transform_dir(s_vr_2h_log.gup, v2_frame_j); // after the kick
	s_vr_2h_log.gup.normalize_safe();
	s_vr_2h_log.cup.set(v2_cup);
	s_vr_2h_log.bnk.set(v2_bnk);

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

// ---------------------------------------------------------------------------------------------------------------------
// GammaVR milestone 8b (Amendment 33): what is seen of the magazine. While the held gun's magazine is out, its magazine
// bone (and the bones under it) is hidden on the gun. While the off hand holds a magazine, a second copy of the gun's
// HUD model is drawn at that hand with only those bones: every bone of the copy has an overwriting callback, the
// magazine's bones take the transforms the gun's own model had while its magazine was last shown, all others are scaled
// to nothing. Render path, once per eye pass, after the gun is placed.
struct vr_mag_vis
{
	IKinematics* gun;       // the gun model the data below belongs to
	u16 bone;               // its magazine bone (BI_NONE: none, nothing is hidden or drawn)
	u64 mask;               // the magazine bone and all bones under it
	bool cache_ok;          // rest[] holds the magazine bones' transforms (gun model space)
	u32 shown_frame, shown_n; // VR frames in a row the gun's magazine bone has been visible
	bool hid;               // the gun's magazine bone is hidden by this code (only then is it shown again here)
	bool copy_failed;       // no usable copy for this model: not tried again until the model changes
	Fmatrix rest[64];
	IRenderVisual* copy_vis;
	IKinematics* copy;
	Fmatrix copy_bone[64];  // what the copy's bone callbacks write
	Fmatrix copy_xf;        // the copy's world transform of this pass (kept: the HUD pass holds the pointer)
};
static vr_mag_vis s_vr_mv = {nullptr, BI_NONE, 0, false, u32(-1), 0, false, false};

static void _BCL vr_mag_bone_cb(CBoneInstance* P)
{
	P->mTransform.set(*(Fmatrix*)P->callback_param());
}

static void vr_mag_copy_delete()
{
	vr_mag_vis& V = s_vr_mv;
	if (V.copy_vis)
		::Render->model_Delete(V.copy_vis);
	V.copy_vis = nullptr;
	V.copy = nullptr;
}

// called when the HUD is destroyed, and when the held model changes
void vr_mag_reset()
{
	vr_mag_copy_delete();
	s_vr_mv.gun = nullptr;
	s_vr_mv.bone = BI_NONE;
	s_vr_mv.mask = 0;
	s_vr_mv.cache_ok = false;
	s_vr_mv.shown_n = 0;
	s_vr_mv.hid = false;
	s_vr_mv.copy_failed = false;
}

static void vr_pda_reset(); // milestone 10a, below

// called when the HUD is destroyed (level unload): nothing of the reload state is carried into another game
void vr_reload_reset()
{
	vr_pda_reset();
	vr_reload_state& S = s_vr_rl;
	if (S.hand_mag && !S.hand_cart.empty())
		Msg("! [vr3mag] the HUD was destroyed with a magazine of %u rounds in the hand: not saved", u32(S.hand_cart.size()));
	S.hand_cart.clear();
	S.hand_mag = false;
	S.out.clear();
	S.dropped = 0;
	vr_mag_reset();
}

static void vr_mag_mask(IKinematics* K, u16 b, u64& mask)
{
	mask |= u64(1) << b;
	const IBoneData& D = K->GetBoneData(b);
	for (u16 i = 0, n = D.GetNumChildren(); i < n; ++i)
		vr_mag_mask(K, D.GetChild(i).GetSelfID(), mask);
}

static bool vr_mag_log_window()
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
	return s_from >= 0 && int(g_vr_track.frame) >= s_from && int(g_vr_track.frame) <= s_to;
}

void player_hud::vr_mag_draw(IDSGraphManager* DM)
{
	vr_reload_state& S = s_vr_rl;
	vr_mag_vis& V = s_vr_mv;
	attachable_hud_item* item = m_attached_items[0] ? m_attached_items[0] : m_attached_items[1];
	if (!item || !item->m_model || S.off || S.simple)
		return;
	IKinematics* K = item->m_model;
	if (V.gun != K)
	{
		vr_mag_reset();
		V.gun = K;
		const u16 b = K->LL_BoneID(S.cfg_bone[0] ? S.cfg_bone : "magazine");
		if (b != BI_NONE && K->LL_BoneCount() <= 64)
		{
			V.bone = b;
			vr_mag_mask(K, b, V.mask);
			// a magazine bone found hidden on a gun reloaded by hand was hidden here earlier (the gun was put away with its magazine out)
			V.hid = S.active && !K->LL_GetBoneVisible(b);
		}
		Msg("* [vr3mag] model %s magazine bone %s: %s", item->m_sect_name.c_str(), S.cfg_bone[0] ? S.cfg_bone : "magazine",
			V.bone != BI_NONE ? "found" : "none (nothing is hidden or drawn for this item)");
	}
	const bool log = vr_mag_log_window();
	const int eye = g_stereo_bench_eye;
	// milestone 8b note 6: what the skeleton holds for the gun's magazine bone as this pass begins (after the last bone
	// calculation, before this pass's own call): the scale of its render transform, 0 = not drawn. Read back, not our call.
	const float gun_rscale = V.bone != BI_NONE ? K->LL_GetBoneInstance(V.bone).mRenderTransform.i.magnitude() : -1.f;
	static int s_nohide = -1; // -vr_mag_nohide: TEST ONLY (mutation witness of the logged check), off by default
	if (s_nohide < 0)
	{
		s_nohide = strstr(Core.Params, "-vr_mag_nohide") ? 1 : 0;
		if (s_nohide)
			Msg("! [vr3mag] -vr_mag_nohide: TEST ONLY, the gun's magazine is NOT hidden while it is out");
	}
	if (V.bone == BI_NONE)
	{
		if (log)
			Msg("* [vr3magv] frame %u eye %d bone 0 gun_shown 1 hand_shown 0", g_vr_track.frame, eye);
		return;
	}
	// the gun's own magazine: remembered while it is shown (two frames in a row, so every bone under it is calculated)
	const bool vis_now = !!K->LL_GetBoneVisible(V.bone);
	if (V.shown_frame != g_vr_track.frame)
	{
		V.shown_frame = g_vr_track.frame;
		V.shown_n = vis_now ? V.shown_n + 1 : 0;
		if (vis_now && V.shown_n >= 2 && K->LL_GetTransform(V.bone).i.magnitude() > 0.01f)
		{
			for (u16 b = 0, n = K->LL_BoneCount(); b < n; ++b)
				if (V.mask & (u64(1) << b))
					V.rest[b].set(K->LL_GetTransform(b));
			V.cache_ok = true;
		}
	}
	const bool hide = S.active && !S.mag_in && V.cache_ok && !s_nohide; // not remembered yet: shown until it is
	if (hide)
	{
		item->set_bone_visible(S.cfg_bone[0] ? S.cfg_bone : "magazine", FALSE, TRUE);
		V.hid = true;
	}
	else if (V.hid) // only what was hidden here is shown again; a bone hidden by an animation or a script is left alone
	{
		V.hid = false;
		item->set_bone_visible(S.cfg_bone[0] ? S.cfg_bone : "magazine", TRUE, TRUE);
		if (item->m_parent_hud_item && item->m_parent_hud_item->has_object())
			if (CWeapon* w = smart_cast<CWeapon*>(&item->m_parent_hud_item->object()))
				w->HUD_VisualBulletUpdate(true); // showing the bone shows every round under it: back to the rounds the gun has
	}

	// the magazine in the off hand
	bool hand = false;
	Fvector mag_w, hand_w, gun_mag_w;
	mag_w.set(0.f, 0.f, 0.f);
	hand_w.set(0.f, 0.f, 0.f);
	float scale = 0.f;
	item->m_item_transform.transform_tiny(gun_mag_w, V.cache_ok ? V.rest[V.bone].c : Fvector().set(0.f, 0.f, 0.f));
	vr_grip_info G;
	if (S.active && S.hand_mag && V.cache_ok && g_vr_get_grip && g_vr_get_grip(&G) && G.gun.valid && G.off.valid)
	{
		if (!V.copy && !V.copy_failed)
		{
			::Render->hud_loading = true;
			V.copy_vis = ::Render->model_Create(pSettings->r_string(item->m_sect_name, "item_visual"));
			::Render->hud_loading = false;
			V.copy = V.copy_vis ? V.copy_vis->dcast_PKinematics() : nullptr;
			if (V.copy && V.copy->LL_BoneCount() == K->LL_BoneCount())
			{
				for (u16 b = 0, n = V.copy->LL_BoneCount(); b < n; ++b)
				{
					V.copy_bone[b].scale(0.f, 0.f, 0.f);
					V.copy->LL_GetBoneInstance(b).set_callback(bctCustom, vr_mag_bone_cb, &V.copy_bone[b], TRUE);
				}
				V.copy->LL_SetBonesVisible(V.mask);
				Msg("* [vr3mag] hand magazine model made from %s (%u bones)", item->m_sect_name.c_str(), u32(V.copy->LL_BoneCount()));
			}
			else
			{
				Msg("! [vr3mag] no hand magazine model for %s", item->m_sect_name.c_str());
				vr_mag_copy_delete();
				V.copy_failed = true;
			}
		}
		if (V.copy)
		{
			for (u16 b = 0, n = V.copy->LL_BoneCount(); b < n; ++b)
				if (V.mask & (u64(1) << b))
					V.copy_bone[b].set(V.rest[b]);
			V.copy->CalculateBones_Invalidate();
			V.copy->CalculateBones(TRUE);
			// the gun as the off hand would hold it, moved so the magazine bone's origin is at that hand
			Fmatrix gun_track, off_track, grip_w, off_w, inv, rel;
			vr_pose_matrix(G.gun, gun_track);
			vr_pose_matrix(G.off, off_track);
			grip_w.mul_43(g_vr_track.track_to_world, gun_track);
			off_w.mul_43(g_vr_track.track_to_world, off_track);
			inv.invert(grip_w);
			rel.mul_43(inv, item->m_item_transform);
			V.copy_xf.mul_43(off_w, rel);
			Fvector at;
			V.copy_xf.transform_tiny(at, V.rest[V.bone].c);
			V.copy_xf.c.add(Fvector().sub(off_w.c, at));
			DM->add_Dynamic(V.copy_vis, &V.copy_xf);
			hand = true;
			const Fmatrix& bt = V.copy->LL_GetTransform(V.bone); // the bone as the copy really has it
			V.copy_xf.transform_tiny(mag_w, bt.c);
			scale = bt.i.magnitude();
			hand_w.set(off_w.c);
		}
	}
	if (log)
	{
		float mx = -1.f, my = -1.f, gx = -1.f, gy = -1.f;
		if (hand)
			vr_hud_pixel(mag_w, mx, my);
		if (V.cache_ok)
			vr_hud_pixel(gun_mag_w, gx, gy);
		Msg("* [vr3magv] frame %u eye %d bone 1 gun_shown %d hand_shown %d cache %d mag_world %.4f %.4f %.4f hand_world %.4f %.4f %.4f bone_scale %.4f mag_px %.2f %.2f gun_mag_px %.2f %.2f size %u %u gun_rscale %.4f",
			g_vr_track.frame, eye, K->LL_GetBoneVisible(V.bone) ? 1 : 0, hand ? 1 : 0, V.cache_ok ? 1 : 0, mag_w.x, mag_w.y, mag_w.z,
			hand_w.x, hand_w.y, hand_w.z, scale, mx, my, gx, gy, Device.dwWidth, Device.dwHeight, gun_rscale);
	}
}

// GammaVR milestone 10a step 1: the PDA's HUD model drawn at the off hand while VR shows the PDA window (see vr_pda_state).
// Placement: off-hand pose * inverse(translate(grip_pos)), grip_pos of the PDA section or its family in the VR grip config
// (Amendment 34 note 5; grip_rot is not measured for the PDA and is not applied). screen_c (optional, same section) = the
// screen centre in model space for the [vr3pdav] pixel line (model origin when absent).
struct vr_pda_vis
{
	IRenderVisual* vis;
	IKinematics* K;
	shared_str sect;       // PDA item section the copy was made for
	bool failed;
	Fvector grip_pos, screen_c;
	Fvector grip_rot;      // pitch, yaw, roll degrees, as the gun's grip_rot (Amendment 34 note 9)
	Fmatrix frame_inv;     // inverse(translate(grip_pos) * Rframe), model placement relative to the hand (measured on the left)
	Fmatrix frame_inv_r;   // step 2: the right hand's, mirrored: S * frame_inv * M (S = hand-local x mirror, M = model z mirror
	                       // about screen_c.z), so the screen centre and its normal are the exact mirror and the device is not
	                       // turned inside out
	bool screen_set;
	Fmatrix xf;            // as drawn
	bool frozen;           // -vr_pda_noattach: xf kept from the first draw after opening
};
static vr_pda_vis s_vr_pv = {};

static void vr_pda_copy_delete()
{
	vr_pda_vis& V = s_vr_pv;
	if (V.vis)
		::Render->model_Delete(V.vis);
	V.vis = nullptr;
	V.K = nullptr;
}

// HUD destroyed (level unload): the copy goes with the renderer's models, the VR-shown state is forgotten
static void vr_pda_reset()
{
	vr_pda_copy_delete();
	s_vr_pv.sect = nullptr;
	s_vr_pv.failed = false;
	s_vr_pv.frozen = false;
	s_vr_pda.shown = false;
	s_vr_pda.block[0] = s_vr_pda.block[1] = false;
}

static bool vr_pda_held_xf(Fmatrix& xf, Fvector& screen_c)
{
	const vr_pda_vis& V = s_vr_pv;
	if (!V.vis || !V.frozen || !s_vr_pda.shown || s_vr_pda.placed)
		return false;
	xf.set(V.xf);
	V.xf.transform_tiny(screen_c, V.screen_c);
	return true;
}

static void vr_pda_draw(IDSGraphManager* DM)
{
	vr_pda_state& S = s_vr_pda;
	vr_pda_vis& V = s_vr_pv;
	if (!S.parsed || S.gamepath)
		return;
	if (!S.shown)
	{
		V.frozen = false;
		return;
	}
	CActor* A = g_actor;
	PIItem pi = (A && A->m_inventory) ? A->inventory().ItemFromSlot(PDA_SLOT) : nullptr;
	vr_grip_info G;
	if (!pi || !g_vr_get_grip || !g_vr_get_grip(&G))
		return;
	// the pose of the hand holding it (step 2): the off hand's, or the gun hand's (G.gun: the converted grip pose)
	const int gun_h = G.hand == 'R' ? 1 : 0;
	const vr_pose& HP = (S.hand == gun_h) ? G.gun : G.off;
	if (!HP.valid && !S.placed)
		return;
	const bool right = S.hand == 1;
	if (V.sect != pi->m_section_id)
	{
		vr_pda_copy_delete();
		V.sect = pi->m_section_id;
		V.failed = false;
		V.frozen = false;
		LPCSTR hud = pSettings->line_exist(V.sect, "hud") ? pSettings->r_string(V.sect, "hud") : nullptr;
		LPCSTR iv = (hud && pSettings->line_exist(hud, "item_visual")) ? pSettings->r_string(hud, "item_visual") : nullptr;
		if (iv)
		{
			::Render->hud_loading = true;
			V.vis = ::Render->model_Create(iv);
			::Render->hud_loading = false;
			V.K = V.vis ? V.vis->dcast_PKinematics() : nullptr;
		}
		V.failed = !V.vis;
		// grip_pos / screen_c: the item's own section, then its family (as vr_grip_lookup)
		V.grip_pos.set(0.f, 0.f, 0.f);
		V.screen_c.set(0.f, 0.f, 0.f);
		V.screen_set = false;
		LPCSTR src = nullptr;
		const vr_grip_cfg& C = vr_grip_config();
		if (C.ini && C.ini->section_exist(V.sect.c_str()))
		{
			if (C.ini->line_exist(V.sect.c_str(), "grip_pos"))
				src = V.sect.c_str();
			else if (C.ini->line_exist(V.sect.c_str(), "family"))
			{
				static string64 fam;
				LPCSTR f = C.ini->r_string(V.sect.c_str(), "family");
				if (strstr(f, "family_") == f)
					xr_strcpy(fam, f);
				else
					xr_sprintf(fam, "family_%s", f);
				if (C.ini->section_exist(fam) && C.ini->line_exist(fam, "grip_pos"))
					src = fam;
			}
		}
		V.grip_rot.set(0.f, 0.f, 0.f);
		if (src && !C.off)
		{
			V.grip_pos = C.ini->r_fvector3(src, "grip_pos");
			if (C.ini->line_exist(src, "grip_rot"))
				V.grip_rot = C.ini->r_fvector3(src, "grip_rot");
		}
		{
			// Rframe as vr_grip_lookup builds it for the gun: R = Ry(yaw) * Rx(-pitch) * Rz(roll); frame = R with c = grip_pos
			Fmatrix Ry, Rx, Rz, R, F;
			Ry.rotateY(deg2rad(V.grip_rot.y));
			Rx.rotateX(-deg2rad(V.grip_rot.x));
			Rz.rotateZ(deg2rad(V.grip_rot.z));
			R.mul_43(Ry, Rx);
			R.mulB_43(Rz);
			F.set(R);
			F.c.set(V.grip_pos);
			V.frame_inv.invert(F);
		}
		if (src && C.ini->line_exist(src, "screen_c"))
		{
			V.screen_c = C.ini->r_fvector3(src, "screen_c");
			V.screen_set = true;
		}
		{
			Fmatrix Sm, Mm, T;
			Sm.identity();
			Sm._11 = -1.f;
			Mm.identity();
			Mm._33 = -1.f;
			Mm.c.set(0.f, 0.f, 2.f * V.screen_c.z);
			T.mul_43(V.frame_inv, Mm);
			V.frame_inv_r.mul_43(Sm, T);
		}
		Msg("* [vr3pda] copy of %s: model %s %s; grip_pos %.4f %.4f %.4f grip_rot %.3f %.3f %.3f from %s%s; screen_c %.4f %.4f %.4f (%s)", V.sect.c_str(),
			iv ? iv : "(none)", V.vis ? "made" : "NOT made", V.grip_pos.x, V.grip_pos.y, V.grip_pos.z,
			V.grip_rot.x, V.grip_rot.y, V.grip_rot.z, src ? src : "none",
			C.off ? " (-vr_grip_off)" : "", V.screen_c.x, V.screen_c.y, V.screen_c.z, V.screen_set ? "config" : "model origin");
	}
	if (!V.vis)
		return;
	Fmatrix off_track, off_w; // the holding hand's pose (named off_ from step 1)
	if (S.placed)
		V.xf.set(S.place_xf); // step 3: lying on the surface
	else
	{
		vr_pose_matrix(HP, off_track);
		off_w.mul_43(g_vr_track.track_to_world, off_track);
		if (!(S.noattach && V.frozen))
			V.xf.mul_43(off_w, (right && !S.nomirror) ? V.frame_inv_r : V.frame_inv);
	}
	V.frozen = true;
	if (V.K)
	{
		V.K->CalculateBones_Invalidate();
		V.K->CalculateBones(TRUE);
	}
	DM->add_Dynamic(V.vis, &V.xf);
	// read back for the log: the model transform as passed to the draw; what it should be, worked out separately
	S.drawn++;
	S.pda_w.set(V.xf.c);
	// want: the model origin as the off hand + offset puts it, worked out without frame_inv: origin = -(R^T * grip_pos)
	if (S.placed)
		S.want_w.set(V.xf.c); // placed: no hand offset applies
	else
	{
		Fmatrix Ry, Rx, Rz, R, Rt;
		Ry.rotateY(deg2rad(V.grip_rot.y));
		Rx.rotateX(-deg2rad(V.grip_rot.x));
		Rz.rotateZ(deg2rad(V.grip_rot.z));
		R.mul_43(Ry, Rx);
		R.mulB_43(Rz);
		Rt.transpose(R);
		Fvector o;
		if (!right)
		{
			Rt.transform_dir(o, V.grip_pos);
			o.invert();
		}
		else
		{
			// the right hand: origin = S * R^T * (M(0) - grip_pos), M(0) = (0, 0, 2 screen_c.z); -vr_pda_nomirror does not change this
			Fvector q;
			q.set(-V.grip_pos.x, -V.grip_pos.y, 2.f * V.screen_c.z - V.grip_pos.z);
			Rt.transform_dir(o, q);
			o.x = -o.x;
		}
		off_w.transform_tiny(S.want_w, o);
	}
	S.place_ok = true;
	if (vr_mag_log_window())
	{
		const int eye = g_stereo_bench_eye;
		Fvector sc;
		V.xf.transform_tiny(sc, V.screen_c);
		float px = -1.f, py = -1.f;
		if (!vr_hud_pixel(sc, px, py))
			px = py = -1.f;
		// facing: the screen normal (model +y, note 9) against the direction from the screen centre to this eye, degrees
		Fvector n, toeye;
		V.xf.transform_dir(n, Fvector().set(0.f, 1.f, 0.f));
		n.normalize_safe();
		toeye.sub(Device.vCameraPosition, sc);
		toeye.normalize_safe();
		const float facing = rad2deg(acosf(_max(-1.f, _min(1.f, n.dotproduct(toeye)))));
		Msg("* [vr3pdav] frame %u eye %d px %.2f %.2f screen_world %.4f %.4f %.4f facing_deg %.2f", g_vr_track.frame, eye, px, py, sc.x, sc.y, sc.z, facing);
		if (int(g_vr_track.frame) == S.grid_frame)
			for (int a = -4; a <= 4; ++a)
				for (int b = -4; b <= 4; ++b)
					for (int c = -4; c <= 4; ++c)
					{
						const Fvector m = Fvector().set(0.02f * a, 0.02f * b, 0.02f * c);
						Fvector w;
						V.xf.transform_tiny(w, m);
						float gx = -1.f, gy = -1.f;
						if (!vr_hud_pixel(w, gx, gy))
							gx = gy = -1.f;
						Msg("* [vr3pdag] frame %u eye %d model %.3f %.3f %.3f px %.2f %.2f", g_vr_track.frame, eye, m.x, m.y, m.z, gx, gy);
					}
	}
}

void player_hud::render_hud(IDSGraphManager* DM)
{
	if (g_vr_track.active)
		vr_dbg_base_log(false, g_vr_track.frame); // GammaVR diagnostic, only with -vr_dbg_base
	if (g_vr_track.active && g_vr_track.head0_set)
		vr_pda_draw(DM); // GammaVR milestone 10a: the PDA in the free hand (drawn with or without a held item)
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
			g_vr_fire.valid = false; // milestone 4: no placement this frame, the fire path falls back to the old pick
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
			vr_cache_fire(); // milestone 4: muzzle and barrel line as drawn, for the next shot and the [vr3grip] log
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

	if (g_vr_track.active && g_vr_track.head0_set)
		vr_mag_draw(DM); // GammaVR milestone 8b: the gun's magazine hidden while out, the magazine in the off hand

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
