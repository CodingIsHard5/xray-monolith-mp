#include "stdafx.h"
#include <dinput.h>
#include "Actor.h"
#include "Torch.h"
#include "trade.h"
#include "../xrEngine/CameraBase.h"

#ifdef DEBUG
#	include "PHDebug.h"
#endif

#include "hit.h"
#include "PHDestroyable.h"
#include "UIGameSP.h"
#include "inventory.h"
#include "WeaponAmmo.h" // GammaVR B3a
#include "level.h"
#include "game_cl_base.h"
#include "xr_level_controller.h"
#include "UsableScriptObject.h"
#include "actorcondition.h"
#include "actor_input_handler.h"
#include "string_table.h"
#include "UI/UIStatic.h"
#include "UI/UIActorMenu.h"
#include "UI/UIDragDropReferenceList.h"
#include "CharacterPhysicsSupport.h"
#include "InventoryBox.h"
#include "player_hud.h"
#include "../xrEngine/xr_input.h"
#include "flare.h"
#include "CustomDetector.h"
#include "clsid_game.h"
#include "hudmanager.h"
#include "Weapon.h"
#include "Flashlight.h"
#include "../xrPhysics/IElevatorState.h"
#include "eatable_item.h"          // GammaVR backpack B2b
#include "ai_space.h"              // GammaVR backpack B3b2 (the test spawn)
#include "alife_simulator.h"
#include "alife_object_registry.h" // GammaVR backpack B3c (the server record of an ammo box)
#include "xrServer.h"                // GammaVR backpack B3c (a round made as create_ammo makes one)
#include "xrServer_Objects_ALife_Items.h"
#include "ai_object_location.h"
#include "xrServer_Objects_ALife.h"
#include "PhysicsShellHolder.h"
#include "../xrPhysics/PhysicsShell.h"
#include "../xrPhysics/IPHWorld.h"
#include "holder_custom.h"
#include "Weapon.h"
#include "CustomOutfit.h"

extern u32 hud_adj_mode;

void CActor::IR_OnKeyboardPress(int cmd)
{
	if (hud_adj_mode && pInput->iGetAsyncKeyState(DIK_LSHIFT)) return;

	if (Remote()) return;

	if (IsTalking()) return;
	if (m_input_external_handler && !m_input_external_handler->authorized(cmd)) return;

	switch (cmd)
	{
	case kWPN_FIRE:
		{
			if ((mstate_wishful & mcLookout) && !IsGameTypeSingle()) return;

			// Tronex: export to allow/prevent weapon fire if returned false
			::luabind::functor<bool> funct;
			if (ai().script_engine().functor("_G.CActor_Fire", funct))
			{
				if (!funct())
				{
					return;
				}
			}

			//-----------------------------
			if (OnServer())
			{
				NET_Packet P;
				P.w_begin(M_PLAYER_FIRE);
				P.w_u16(ID());
				u_EventSend(P);
			}
		}
		break;
	default:
		{
		}
		break;
	}

	if (!g_Alive()) return;

	if (m_holder && kUSE != cmd)
	{
		m_holder->OnKeyboardPress(cmd);
		if (m_holder->allowWeapon() && inventory().Action((u16)cmd, CMD_START)) return;
		return;
	}
	else
		if (inventory().Action((u16)cmd, CMD_START)) return;

#ifdef DEBUG
	if(psActorFlags.test(AF_NO_CLIP))
	{
		NoClipFly(cmd);
		return;
	}
#endif //DEBUG
	switch (cmd)
	{
	case kJUMP:
		{
			mstate_wishful |= mcJump;
		}
		break;
	case kSPRINT_TOGGLE:
		{
			if (psActorFlags.test(AF_SPRINT_TOGGLE))
			{
				if (psActorFlags.test(AF_WALK_TOGGLE)) mstate_wishful &= ~mcAccel;
				if (psActorFlags.test(AF_CROUCH_TOGGLE)) mstate_wishful &= ~mcCrouch;
				mstate_wishful ^= mcSprint;
			}
		}
		break;
	case kFREELOOK:
	{
		if (psActorFlags.test(AF_FREELOOK_TOGGLE))
		{
			if (cam_freelook == eflDisabled && CanUseFreelook())
			{
				cam_SetFreelook();
			}
			else if (cam_freelook == eflEnabled)
			{
				cam_UnsetFreelook();
			}
		}
	}
	break;
	case kCROUCH:
		{
			if (psActorFlags.test(AF_CROUCH_TOGGLE))
				mstate_wishful ^= mcCrouch;
		}
		break;
	case kACCEL:
		{
			if (psActorFlags.test(AF_WALK_TOGGLE))
				mstate_wishful ^= mcAccel;
		}
		break;
	case kL_LOOKOUT:
		{
			if (psActorFlags.test(AF_LOOKOUT_TOGGLE))
			{
				mstate_wishful &= ~mcRLookout;
				mstate_wishful ^= mcLLookout;
			}
		}
		break;
	case kR_LOOKOUT:
		{
			if (psActorFlags.test(AF_LOOKOUT_TOGGLE))
			{
				mstate_wishful &= ~mcLLookout;
				mstate_wishful ^= mcRLookout;
			}
		}
		break;
	case kCAM_1: cam_Set(eacFirstEye);
		break;
	case kCAM_2: cam_Set(eacLookAt);
		break;
	case kCAM_3: cam_Set(eacFreeLook);
		break;
	case kNIGHT_VISION:
		{
			//SwitchNightVision(); //Rezy: now it's controlled via LUA scripts for timing and animations
			break;
		}
	case kTORCH:
		{
			//SwitchTorch(); //Tronex: now it's controlled via LUA scripts for timing and animations
			break;
		}

	case kDETECTOR:
		{
			PIItem dev_active = inventory().ItemFromSlot(DETECTOR_SLOT);
			if (dev_active)
			{
				CCustomDevice* dev = smart_cast<CCustomDevice*>(dev_active);
				if (dev)
					dev->ToggleDevice(g_player_hud->attached_item(0) != NULL);
			}
		}
		break;
		/*
			case kFLARE:{
					PIItem fl_active = inventory().ItemFromSlot(FLARE_SLOT);
					if(fl_active)
					{
						CFlare* fl			= smart_cast<CFlare*>(fl_active);
						fl->DropFlare		();
						return				;
					}
		
					PIItem fli = inventory().Get(CLSID_DEVICE_FLARE, true);
					if(!fli)			return;
		
					CFlare* fl			= smart_cast<CFlare*>(fli);
					
					if(inventory().Slot(fl))
						fl->ActivateFlare	();
				}break;
		*/
	case kUSE:
		ActorUse();
		break;
	case kDROP:
		b_DropActivated = TRUE;
		f_DropPower = 0;
		break;
	case kNEXT_SLOT:
		{
			OnNextWeaponSlot();
		}
		break;
	case kPREV_SLOT:
		{
			OnPrevWeaponSlot();
		}
		break;

	case kQUICK_USE_1:
	case kQUICK_USE_2:
	case kQUICK_USE_3:
	case kQUICK_USE_4:
		{
			const shared_str& item_name = g_quick_use_slots[cmd - kQUICK_USE_1];
			if (item_name.size())
			{
				PIItem itm = inventory().GetAny(item_name.c_str());

				::luabind::functor<bool> funct;
				if (itm && ai().script_engine().functor("_G.CInventory__eat", funct))
				{
					CGameObject* GO = itm->cast_game_object();
					if (GO && funct(GO->lua_game_object()))
					{
						if (IsGameTypeSingle())
						{
							inventory().Eat(itm);
						}
						else
						{
							inventory().ClientEat(itm);
						}

						StaticDrawableWrapper* _s = CurrentGameUI()->AddCustomStatic("item_used", true);
						string1024 str;
						strconcat(sizeof(str), str, *CStringTable().translate("st_item_used"), ": ", itm->NameItem());
						_s->wnd()->TextItemControl()->SetText(str);

						//CurrentGameUI()->GetActorMenu().m_pQuickSlot->ReloadReferences(this);
					}
				}
			}
		}
		break;
	}
}

// demonized: switch to disable mouse wheel weapon change
BOOL mouseWheelChangeWeapon = TRUE;

// demonized: switch to invert mouse wheel weapon change
BOOL mouseWheelInvertChangeWeapons = FALSE;

// mbehm: switch to allow inverting mouse wheel zoom direction
BOOL mouseWheelInvertZoom = FALSE;
void CActor::IR_OnMouseWheel(int direction)
{
	if (hud_adj_mode)
	{
		g_player_hud->tune(Ivector().set(0, 0, direction));
		return;
	}

#ifdef CAR_NEW
    if (m_holder)
    {
        CCar* car = smart_cast<CCar*>(m_holder);
        if (car && car->IsScopeEnable())
        {
            car->ScopeOnMouseWheel(direction);
            return;
        }
    }
#endif

	if (mouseWheelInvertZoom) {
		if (inventory().Action((direction > 0) ? (u16)kWPN_ZOOM_DEC : (u16)kWPN_ZOOM_INC, CMD_START)) return;
	} else {
		if (inventory().Action((direction > 0) ? (u16)kWPN_ZOOM_INC : (u16)kWPN_ZOOM_DEC, CMD_START)) return;
	}

	if (mouseWheelChangeWeapon) {
		if (mouseWheelInvertChangeWeapons) {
			if (direction < 0)
				OnNextWeaponSlot();
			else
				OnPrevWeaponSlot();
		} else {
			if (direction > 0)
				OnNextWeaponSlot();
			else
				OnPrevWeaponSlot();
		}
	}
}

void CActor::IR_OnKeyboardRelease(int cmd)
{
	if (hud_adj_mode && pInput->iGetAsyncKeyState(DIK_LSHIFT)) return;

	if (Remote()) return;

	if (m_input_external_handler && !m_input_external_handler->authorized(cmd)) return;

	if (g_Alive())
	{
		if (cmd == kUSE && !psActorFlags.test(AF_MULTI_ITEM_PICKUP))
			m_bPickupMode = false;

		if (m_holder)
		{
			m_holder->OnKeyboardRelease(cmd);

			if (m_holder->allowWeapon() && inventory().Action((u16)cmd, CMD_STOP)) return;
			return;
		}
		else
			if (inventory().Action((u16)cmd, CMD_STOP)) return;


		switch (cmd)
		{
		case kJUMP: mstate_wishful &= ~mcJump;
			break;
		case kDROP: if (GAME_PHASE_INPROGRESS == Game().Phase()) g_PerformDrop();
			break;
		}
	}
}

void CActor::IR_OnKeyboardHold(int cmd)
{
	if (hud_adj_mode && pInput->iGetAsyncKeyState(DIK_LSHIFT)) return;

	if (Remote() || !g_Alive()) return;
	if (m_input_external_handler && !m_input_external_handler->authorized(cmd)) return;
	if (IsTalking()) return;

	if (m_holder)
	{
		m_holder->OnKeyboardHold(cmd);
		return;
	}

#ifdef DEBUG
	if(psActorFlags.test(AF_NO_CLIP) && (cmd==kFWD || cmd==kBACK || cmd==kL_STRAFE || cmd==kR_STRAFE 
		|| cmd==kJUMP || cmd==kCROUCH))
	{
		NoClipFly(cmd);
		return;
	}
#endif //DEBUG
	float LookFactor = GetLookFactor();
	switch (cmd)
	{
	case kUP:
	case kDOWN:
		if(cam_freelook != eflEnabling && cam_freelook != eflDisabling) cam_Active()->Move((cmd == kUP) ? kDOWN : kUP, 0, LookFactor);
		break;
	case kCAM_ZOOM_IN:
	case kCAM_ZOOM_OUT:
		cam_Active()->Move(cmd);
		break;
	case kLEFT:
	case kRIGHT:
		if (eacFreeLook != cam_active && cam_freelook != eflEnabling && cam_freelook != eflDisabling) cam_Active()->Move(cmd, 0, LookFactor);
		break;
	case kL_STRAFE: mstate_wishful |= mcLStrafe;
		break;
	case kR_STRAFE: mstate_wishful |= mcRStrafe;
		break;
	case kL_LOOKOUT:
		{
			if (!psActorFlags.test(AF_LOOKOUT_TOGGLE) && cam_freelook == eflDisabled)
				mstate_wishful |= mcLLookout;
		}
		break;
	case kR_LOOKOUT:
		{
			if (!psActorFlags.test(AF_LOOKOUT_TOGGLE) && cam_freelook == eflDisabled)
				mstate_wishful |= mcRLookout;
		}
		break;
	case kFWD: mstate_wishful |= mcFwd;
		break;
	case kBACK: mstate_wishful |= mcBack;
		break;
	case kCROUCH:
		{
			if (!psActorFlags.test(AF_CROUCH_TOGGLE))
				mstate_wishful |= mcCrouch;
		}
		break;
	case kACCEL:
		{
			if (!psActorFlags.test(AF_WALK_TOGGLE))
				mstate_wishful |= mcAccel;
		}
	break;
	case kFREELOOK:
	{
		if (!psActorFlags.test(AF_FREELOOK_TOGGLE))
		{
			if (cam_freelook == eflDisabled && CanUseFreelook())
			{
				cam_SetFreelook();
			}
		}
	}
	break;
	case kSPRINT_TOGGLE:
		{
			if (!psActorFlags.test(AF_SPRINT_TOGGLE))
			{
				if (psActorFlags.test(AF_WALK_TOGGLE)) mstate_wishful &= ~mcAccel;
				if (psActorFlags.test(AF_CROUCH_TOGGLE)) mstate_wishful &= ~mcCrouch;
				mstate_wishful |= mcSprint;
			}
		}
		break;
	}
}

void CActor::IR_OnMouseMove(int dx, int dy)
{
	if (hud_adj_mode)
	{
		g_player_hud->tune(Ivector().set(dx, dy, 0));
		return;
	}

	PIItem iitem = inventory().ActiveItem();
	if (iitem && iitem->cast_hud_item())
		iitem->cast_hud_item()->ResetSubStateTime();

	if (Remote()) return;

	if (m_holder)
	{
		m_holder->OnMouseMove(dx, dy);
		return;
	}

	if (cam_freelook == eflEnabling || cam_freelook == eflDisabling)
		return;

	const float LookFactor = GetLookFactor();

	CCameraBase* C = cameras[cam_active];
    float scale = (C->f_fov / g_fov) * (psMouseSens * sens_multiple) * psMouseSensScale / 50.f / LookFactor;
	if (dx)
	{
		float d = float(dx) * scale;
		cam_Active()->Move((d < 0) ? kLEFT : kRIGHT, _abs(d));
	}
	if (dy)
	{
		float d = ((psMouseInvert.test(1)) ? -1 : 1) * float(dy) * scale * psMouseSensVerticalK * 3.f / 4.f;
		cam_Active()->Move((d > 0) ? kUP : kDOWN, _abs(d));
	}
}

#include "HudItem.h"

bool CActor::use_Holder(CHolderCustom* holder)
{
	if (m_holder)
	{
		bool b = use_HolderEx(0, false);

		if (inventory().ActiveItem())
		{
			CHudItem* hi = smart_cast<CHudItem*>(inventory().ActiveItem());
			if (hi) hi->OnAnimationEnd(hi->GetState());
		}

		return b;
	}
	else
	{
		bool b = use_HolderEx(holder, false);

		if (b)
		{
			//used succesfully
			// switch off torch...
			CAttachableItem* I = CAttachmentOwner::attachedItem(CLSID_DEVICE_TORCH);
			if (I)
			{
				CTorch* torch = smart_cast<CTorch*>(I);
				if (torch) torch->Switch(false);
			}
		}

		if (inventory().ActiveItem())
		{
			CHudItem* hi = smart_cast<CHudItem*>(inventory().ActiveItem());
			if (hi) hi->OnAnimationEnd(hi->GetState());
		}

		return b;
	}
}

void CActor::ActorUse()
{
	if (m_holder)
	{
		CGameObject* GO = smart_cast<CGameObject*>(m_holder);
		NET_Packet P;
		CGameObject::u_EventGen(P, GEG_PLAYER_DETACH_HOLDER, ID());
		P.w_u16(GO->ID());
		CGameObject::u_EventSend(P);
		return;
	}

	if (!psActorFlags.test(AF_MULTI_ITEM_PICKUP))
		m_bPickupMode = true;

	if (character_physics_support()->movement()->PHCapture())
		character_physics_support()->movement()->PHReleaseObject();


	if (m_pUsableObject && NULL == m_pObjectWeLookingAt->cast_inventory_item())
	{
		m_pUsableObject->use(this);
	}

	if (m_pInvBoxWeLookingAt && m_pInvBoxWeLookingAt->nonscript_usable())
	{
		CUIGameSP* pGameSP = smart_cast<CUIGameSP*>(CurrentGameUI());
		if (pGameSP) //single
		{
			if (!m_pInvBoxWeLookingAt->closed())
			{
				pGameSP->StartCarBody(this, m_pInvBoxWeLookingAt);
			}
		}
		return;
	}

	if (!m_pUsableObject || m_pUsableObject->nonscript_usable())
	{
		bool bCaptured = false;

		collide::rq_result& RQ = HUD().GetRQ();
		CPhysicsShellHolder* object = smart_cast<CPhysicsShellHolder*>(RQ.O);
		u16 element = BI_NONE;
		if (object)
			element = (u16)RQ.element;

		if (object && Level().IR_GetKeyState(DIK_LSHIFT))
		{
			bool b_allow = !!pSettings->line_exist("ph_capture_visuals", object->cNameVisual());
			::luabind::functor<bool> funct;
			if (ai().script_engine().functor("_G.CActor__OnBeforePHCapture", funct))
				b_allow = funct(object->lua_game_object(), b_allow);

			if (b_allow && !character_physics_support()->movement()->PHCapture())
			{
				character_physics_support()->movement()->PHCaptureObject(object, element);
				bCaptured = true;
			}
		}
		else
		{
			if (object && smart_cast<CHolderCustom*>(object))
			{
				NET_Packet P;
				CGameObject::u_EventGen(P, GEG_PLAYER_ATTACH_HOLDER, ID());
				P.w_u16(object->ID());
				CGameObject::u_EventSend(P);
				return;
			}
		}

		if (m_pPersonWeLookingAt)
		{
			CEntityAlive* pEntityAliveWeLookingAt =
				smart_cast<CEntityAlive*>(m_pPersonWeLookingAt);

			VERIFY(pEntityAliveWeLookingAt);

			if (IsGameTypeSingle())
			{
				if (pEntityAliveWeLookingAt->g_Alive())
				{
					TryToTalk();
				}
				else if (!bCaptured)
				{
					//только если находимся в режиме single
					CUIGameSP* pGameSP = smart_cast<CUIGameSP*>(CurrentGameUI());
					if (pGameSP)
					{
						if (!m_pPersonWeLookingAt->deadbody_closed_status())
						{
							if (pEntityAliveWeLookingAt->AlreadyDie() &&
								pEntityAliveWeLookingAt->GetLevelDeathTime() + 3000 < Device.dwTimeGlobal)
								// 99.9% dead
								pGameSP->StartCarBody(this, m_pPersonWeLookingAt);
						}
					}
				}
			}
		}
	}
}
extern BOOL firstPersonDeath;
BOOL CActor::HUDview() const
{
	return IsFocused() && (cam_active == eacFirstEye) &&
		((!m_holder) || (m_holder && m_holder->allowWeapon() && m_holder->HUDView())) && (firstPersonDeath ? g_Alive() : true);
}

static u16 SlotsToCheck [] = {
	KNIFE_SLOT, // 0
	INV_SLOT_2, // 1
	INV_SLOT_3, // 2
	GRENADE_SLOT, // 3
	ARTEFACT_SLOT, // 10
	PDA_SLOT
};

void CActor::OnNextWeaponSlot()
{
	u32 ActiveSlot = inventory().GetActiveSlot();
	if (ActiveSlot == NO_ACTIVE_SLOT)
		ActiveSlot = inventory().GetPrevActiveSlot();

	if (ActiveSlot == NO_ACTIVE_SLOT)
		ActiveSlot = KNIFE_SLOT;

	u32 NumSlotsToCheck = sizeof(SlotsToCheck) / sizeof(SlotsToCheck[0]);

	u32 CurSlot = 0;
	for (; CurSlot < NumSlotsToCheck; CurSlot++)
	{
		if (SlotsToCheck[CurSlot] == ActiveSlot) break;
	};

	if (CurSlot >= NumSlotsToCheck)
		return;

	for (u32 i = CurSlot + 1; i < NumSlotsToCheck; i++)
	{
		if (inventory().ItemFromSlot(SlotsToCheck[i]))
		{
			if (SlotsToCheck[i] == ARTEFACT_SLOT)
			{
				IR_OnKeyboardPress(kARTEFACT);
			}
			else if (SlotsToCheck[i] == PDA_SLOT)
			{
				IR_OnKeyboardPress(kACTIVE_JOBS);
			}
			else
				IR_OnKeyboardPress(kWPN_1 + i);
			return;
		}
	}
};

void CActor::OnPrevWeaponSlot()
{
	u32 ActiveSlot = inventory().GetActiveSlot();
	if (ActiveSlot == NO_ACTIVE_SLOT)
		ActiveSlot = inventory().GetPrevActiveSlot();

	if (ActiveSlot == NO_ACTIVE_SLOT)
		ActiveSlot = KNIFE_SLOT;

	u32 NumSlotsToCheck = sizeof(SlotsToCheck) / sizeof(SlotsToCheck[0]);
	u32 CurSlot = 0;

	for (; CurSlot < NumSlotsToCheck; CurSlot++)
	{
		if (SlotsToCheck[CurSlot] == ActiveSlot) break;
	};

	if (CurSlot >= NumSlotsToCheck)
		CurSlot = NumSlotsToCheck - 1; //last in row

	for (s32 i = s32(CurSlot - 1); i >= 0; i--)
	{
		if (inventory().ItemFromSlot(SlotsToCheck[i]))
		{
			if (SlotsToCheck[i] == ARTEFACT_SLOT)
			{
				IR_OnKeyboardPress(kARTEFACT);
			}
			else if (SlotsToCheck[i] == PDA_SLOT)
			{
				IR_OnKeyboardPress(kACTIVE_JOBS);
			}
			else
				IR_OnKeyboardPress(kWPN_1 + i);
			return;
		}
	}
};

extern float g_AimLookFactor;

// momopate: Optional restoration of the handling gimmick from the original games
BOOL g_allow_weapon_control_inertion_factor = 0;
BOOL g_allow_outfit_control_inertion_factor = 0;

float CActor::GetLookFactor()
{
	if (m_input_external_handler)
		return m_input_external_handler->mouse_scale_factor();

	float factor = 1.f;

	if (m_bZoomAimingMode)
		factor /= g_AimLookFactor;

	if (g_allow_weapon_control_inertion_factor)
	{
		PIItem pItem = inventory().ActiveItem();
		if (pItem)
			factor *= pItem->GetControlInertionFactor();
	}

	if (g_allow_outfit_control_inertion_factor)
	{
		CCustomOutfit* outfit = GetOutfit();
		if (outfit)
			factor *= outfit->GetControlInertionFactor();
	}

    VERIFY(!fis_zero(factor));

	if (cam_freelook != eflDisabled)
		return 1.5f;

	return factor;
}

void CActor::set_input_external_handler(CActorInputHandler* handler)
{
	// clear state
	if (handler)
		mstate_wishful = 0;

	// release fire button
	if (handler)
		IR_OnKeyboardRelease(kWPN_FIRE);

	// set handler
	m_input_external_handler = handler;
}

void CActor::SwitchNightVision()
{
	SwitchNightVision(!m_bNightVisionOn);
}

void CActor::SwitchTorch()
{
	CTorch* pTorch = smart_cast<CTorch*>(inventory().ItemFromSlot(TORCH_SLOT));
	if (pTorch)
		pTorch->Switch();
}

#ifdef DEBUG
void CActor::NoClipFly(int cmd)
{
	Fvector cur_pos;// = Position();
	cur_pos.set(0,0,0);
	float scale = 1.0f;
	if(pInput->iGetAsyncKeyState(DIK_LSHIFT))
		scale = 0.25f;
	else if(pInput->iGetAsyncKeyState(DIK_LMENU))
		scale = 4.0f;

	switch(cmd)
	{
	case kJUMP:		
		cur_pos.y += 0.1f;
		break;
	case kCROUCH:	
		cur_pos.y -= 0.1f;
		break;
	case kFWD:	
		cur_pos.z += 0.1f;
		break;
	case kBACK:
		cur_pos.z -= 0.1f;
		break;
	case kL_STRAFE:
		cur_pos.x -= 0.1f;
		break;
	case kR_STRAFE:
		cur_pos.x += 0.1f;
		break;
	case kCAM_1:	
		cam_Set(eacFirstEye);				
		break;
	case kCAM_2:	
		cam_Set(eacLookAt);				
		break;
	case kCAM_3:	
		cam_Set(eacFreeLook);
		break;
	case kNIGHT_VISION:
		SwitchNightVision();
		break;
	case kTORCH:
		SwitchTorch();
		break;
	case kDETECTOR:
		{
			PIItem det_active = inventory().ItemFromSlot(DETECTOR_SLOT);
			if(det_active)
			{
				CCustomDetector* det = smart_cast<CCustomDetector*>(det_active);
					det->ToggleDetector(g_player_hud->attached_item(0)!=NULL);
				return;
			}
		}
		break;
	case kUSE:
		ActorUse();
		break;
	}
	cur_pos.mul(scale);
	Fmatrix	mOrient;
	mOrient.rotateY(-(cam_Active()->GetWorldYaw()));
	mOrient.transform_dir(cur_pos);
	Position().add(cur_pos);
	character_physics_support()->movement()->SetPosition(Position());
}
#endif //DEBUG

// GammaVR diegetic item 5 (items in the hand), PROBE, TEST ONLY, no verdict: "-vr_item_probe <frame> <slot 0-3> <skip>".
// On VR frame <frame> it logs the four quick slots (section, the item GetAny finds, its world visual, the count and
// uses of that section in the inventory), then runs the engine's own quick-use path for <slot> (IR_OnKeyboardPress of
// kQUICK_USE_1 + slot, the same code as the key). If <skip> >= 0, <skip> frames later it calls GAMMA's lam2
// stop_all_actions (FDDA's own skip). From <frame> to <frame> + 400 it logs every change in that section's count and
// uses, and the actor's health. Inert without the switch; no launcher passes it.
#include "eatable_item.h"
#include "../xrEngine/vr_hooks.h"
static void vr_item_count(CActor* A, LPCSTR sect, int& n, int& uses)
{
	n = 0;
	uses = 0;
	for (PIItem it : A->inventory().m_all)
	{
		if (!it || xr_strcmp(it->object().cNameSect(), sect))
			continue;
		++n;
		CEatableItem* e = smart_cast<CEatableItem*>(it);
		uses += e ? int(e->GetRemainingUses()) : 1;
	}
}
void vr_item_probe_update()
{
	static bool s_parsed = false;
	static int s_frame = -1, s_slot = 0, s_skip = -1;
	static int s_last_n = -1, s_last_uses = -1;
	if (!s_parsed)
	{
		s_parsed = true;
		if (LPCSTR p = strstr(Core.Params, "-vr_item_probe "))
		{
			if (sscanf(p + 15, "%d %d %d", &s_frame, &s_slot, &s_skip) != 3 || s_frame < 0 || s_slot < 0 || s_slot > 3)
				s_frame = -1;
			Msg("* [vr11p] item probe (TEST ONLY): frame %d slot %d skip %d", s_frame, s_slot, s_skip);
		}
	}
	if (s_frame < 0 || !g_vr_track.head0_set)
		return;
	CActor* A = smart_cast<CActor*>(Level().CurrentEntity());
	if (!A)
		return;
	const int f = int(g_vr_track.frame);
	if (f < s_frame || f > s_frame + 400)
		return;
	static int s_done_at = -1, s_skip_done = 0;
	LPCSTR sect = g_quick_use_slots[s_slot];
	if (s_done_at < 0)
	{
		s_done_at = f;
		for (int i = 0; i < 4; ++i)
		{
			LPCSTR s = g_quick_use_slots[i];
			PIItem it = s[0] ? A->inventory().GetAny(s) : nullptr;
			int n, u;
			vr_item_count(A, s, n, u);
			Msg("* [vr11p] frame %d slot %d section '%s' found %d id %d visual '%s' count %d uses %d", f, i, s, it ? 1 : 0,
				it ? int(it->object().ID()) : -1, it ? it->object().cNameVisual().c_str() : "", n, u);
		}
		vr_item_count(A, sect, s_last_n, s_last_uses);
		Msg("* [vr11p] frame %d quick use slot %d ('%s') through IR_OnKeyboardPress(kQUICK_USE_%d); health %.4f", f, s_slot, sect,
			s_slot + 1, A->conditions().GetHealth());
		A->IR_OnKeyboardPress(kQUICK_USE_1 + s_slot);
		return;
	}
	if (s_skip >= 0 && !s_skip_done && f >= s_done_at + s_skip)
	{
		s_skip_done = 1;
		::luabind::functor<void> fn;
		const bool ok = ai().script_engine().functor("lam2.stop_all_actions", fn);
		if (ok)
			fn();
		Msg("* [vr11p] frame %d skip: lam2.stop_all_actions %s", f, ok ? "called" : "NOT FOUND");
	}
	int n, u;
	vr_item_count(A, sect, n, u);
	if (n != s_last_n || u != s_last_uses)
	{
		Msg("* [vr11p] frame %d section '%s' count %d -> %d uses %d -> %d; health %.4f", f, sect, s_last_n, n, s_last_uses, u,
			A->conditions().GetHealth());
		s_last_n = n;
		s_last_uses = u;
	}
	if (f == s_frame + 400)
		Msg("* [vr11p] frame %d end: section '%s' count %d uses %d; health %.4f", f, sect, n, u, A->conditions().GetHealth());
}

// GammaVR backpack B, MODEL PROBE, TEST ONLY, no verdict (docs/backpack_desk.md): "-vr_model_probe <frame> <per frame>".
// On VR frame <frame> it logs the actor's inventory, one [vr13i] line per item (place, weight, grid size, world model)
// and a summary. From the next frame on it takes every config section that has inv_name, inv_weight and visual, and
// makes each distinct world model once, <per frame> models a frame: the file is looked up first (a missing file is
// logged, never asked of the model pool, which would stop the engine), the make is timed, the box and bone count are
// logged ([vr13m]) and the model is given back. A summary line ends it. Inert without the switch; no launcher passes it.
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/RenderVisual.h"
void vr_model_probe_update()
{
	static bool s_parsed = false;
	static int s_frame = -1, s_per = 20;
	if (!s_parsed)
	{
		s_parsed = true;
		if (LPCSTR p = strstr(Core.Params, "-vr_model_probe "))
		{
			if (sscanf(p + 16, "%d %d", &s_frame, &s_per) != 2 || s_frame < 0 || s_per < 0)
				s_frame = -1;
			Msg("* [vr13] model probe (TEST ONLY): frame %d, %d models a frame", s_frame, s_per);
		}
	}
	if (s_frame < 0 || !g_vr_track.head0_set)
		return;
	CActor* A = smart_cast<CActor*>(Level().CurrentEntity());
	if (!A)
		return;
	const int f = int(g_vr_track.frame);
	if (f < s_frame)
		return;
	static int s_stage = 0;
	static xr_vector<shared_str> s_vis;
	static u32 s_next = 0, s_sections = 0, s_made = 0, s_missing = 0, s_failed = 0;
	static float s_total_ms = 0.f, s_max_ms = 0.f;
	static shared_str s_max_name;
	if (s_stage == 0)
	{
		s_stage = 1;
		u32 n = 0, place[4] = { 0, 0, 0, 0 };
		for (PIItem it : A->inventory().m_all)
		{
			if (!it)
				continue;
			LPCSTR sect = it->object().cNameSect().c_str();
			const u32 pl = it->CurrPlace() < 4 ? it->CurrPlace() : 0;
			++place[pl];
			++n;
			Msg("* [vr13i] id %d section '%s' class '%s' kind '%s' place %u slot %u weight %.3f grid %ux%u visual '%s'", int(it->object().ID()), sect,
				pSettings->line_exist(sect, "class") ? pSettings->r_string(sect, "class") : "-",
				pSettings->line_exist(sect, "kind") ? pSettings->r_string(sect, "kind") : "-", pl, u32(it->CurrSlot()), it->Weight(),
				pSettings->line_exist(sect, "inv_grid_width") ? pSettings->r_u32(sect, "inv_grid_width") : 0,
				pSettings->line_exist(sect, "inv_grid_height") ? pSettings->r_u32(sect, "inv_grid_height") : 0, it->object().cNameVisual().c_str());
		}
		Msg("* [vr13i] frame %d inventory: %u items (place 0/undefined %u, 1 %u, 2 %u, 3 %u; the engine's EItemPlace: 1 slot, 2 belt, 3 ruck); weight %.2f of max carry %.2f",
			f, n, place[0], place[1], place[2], place[3], A->inventory().TotalWeight(), A->MaxCarryWeight());
		for (const auto& S : pSettings->sections())
		{
			LPCSTR sn = S.Name.c_str();
			if (!pSettings->line_exist(sn, "inv_name") || !pSettings->line_exist(sn, "inv_weight") || !pSettings->line_exist(sn, "visual"))
				continue;
			++s_sections;
			shared_str v = pSettings->r_string_wb(sn, "visual");
			if (!v.size())
				continue;
			bool seen = false;
			for (const shared_str& o : s_vis)
				if (o == v)
				{
					seen = true;
					break;
				}
			if (!seen)
				s_vis.push_back(v);
		}
		if (s_per == 0) // <per frame> 0: the inventory lines only (B1's independent list), no models are made
		{
			s_stage = 2;
			xrLogger::FlushLog();
			return;
		}
		Msg("* [vr13] frame %d: %u item sections (inv_name, inv_weight and visual), %u distinct world models", f, s_sections, u32(s_vis.size()));
		xrLogger::FlushLog();
		return;
	}
	if (s_stage != 1)
		return;
	Msg("* [vr13] frame %d batch from %u", f, s_next);
	xrLogger::FlushLog();
	for (int i = 0; i < s_per && s_next < s_vis.size(); ++i, ++s_next)
	{
		LPCSTR N = s_vis[s_next].c_str();
		string_path name, fn;
		if (0 == strext(N))
			strconcat(sizeof(name), name, N, ".ogf");
		else
			xr_strcpy(name, sizeof(name), N);
		if (!FS.exist(N) && !FS.exist(fn, "$level$", name) && !FS.exist(fn, "$game_meshes$", name))
		{
			++s_missing;
			Msg("* [vr13m] MISSING '%s' (no file)", N);
			continue;
		}
		CTimer T;
		T.Start();
		IRenderVisual* V = ::Render->model_Create(N);
		const float ms = T.GetElapsed_sec() * 1000.f;
		if (!V)
		{
			++s_failed;
			Msg("* [vr13m] FAILED '%s' (%.2f ms)", N, ms);
			continue;
		}
		++s_made;
		s_total_ms += ms;
		if (ms > s_max_ms)
		{
			s_max_ms = ms;
			s_max_name = s_vis[s_next];
		}
		const Fbox& b = V->getVisData().box;
		IKinematics* K = V->dcast_PKinematics();
		Msg("* [vr13m] made '%s' %.2f ms box %.3f x %.3f x %.3f bones %d", N, ms, b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z,
			K ? int(K->LL_BoneCount()) : 0);
		::Render->model_Delete(V);
	}
	if (s_next >= s_vis.size())
	{
		s_stage = 2;
		Msg("* [vr13] frame %d model probe end: %u sections, %u distinct models: made %u, missing %u, failed %u; make time total %.1f ms, mean %.2f ms, worst %.2f ms ('%s')",
			f, s_sections, u32(s_vis.size()), s_made, s_missing, s_failed, s_total_ms, s_made ? s_total_ms / float(s_made) : 0.f, s_max_ms,
			s_max_name.size() ? s_max_name.c_str() : "");
		xrLogger::FlushLog();
	}
}

// GammaVR milestone 11a (Amendment 37 note 5): helpers for player_hud.cpp's consumable-in-the-hand code
bool vr_lam2_skip() // GAMMA's lam2.stop_all_actions (FDDA's own skip); false if the function is not there
{
	::luabind::functor<void> fn;
	if (!ai().script_engine().functor("lam2.stop_all_actions", fn))
		return false;
	fn();
	return true;
}
void vr_item_section_count(CActor* A, LPCSTR sect, int& n, int& uses) { vr_item_count(A, sect, n, uses); }

// ---- GammaVR backpack B2b (SPIKE_PLAN Amendment 41 part 2): the game-side calls for an item taken out of the bag ----
int vr_bpt_ruck_count(CActor* A, LPCSTR sect) // items of this section in the backpack list (the list the bag shows)
{
	int n = 0;
	if (A && A->m_inventory)
		for (PIItem it : A->inventory().m_ruck)
			if (it && !xr_strcmp(it->object().cNameSect().c_str(), sect))
				++n;
	return n;
}
// B3a (log only): is object id an ammo box; is it still the actor's; its rounds (-1 = no such object); and the rounds and
// boxes of that section in the actor's whole inventory
bool vr_bpt_ammo(CActor* A, int id, LPCSTR sect, int& owned, int& rounds, int& inv_rounds, int& boxes)
{
	owned = 0;
	rounds = -1;
	inv_rounds = 0;
	boxes = 0;
	if (!A || !A->m_inventory)
		return false;
	for (PIItem it : A->inventory().m_all)
	{
		CWeaponAmmo* am = it ? smart_cast<CWeaponAmmo*>(it) : nullptr;
		if (!am || xr_strcmp(am->cNameSect().c_str(), sect))
			continue;
		++boxes;
		inv_rounds += int(am->m_boxCurr);
		if (int(am->ID()) == id)
			owned = 1;
	}
	CObject* O = Level().Objects.net_Find(u16(id));
	CWeaponAmmo* a = O ? smart_cast<CWeaponAmmo*>(O) : nullptr;
	if (a)
		rounds = int(a->m_boxCurr);
	return a != nullptr;
}
int vr_bpt_pick(CActor* A, LPCSTR sect, bool& eatable, float& weight) // the one real item: GetAny; its id or -1
{
	eatable = false;
	weight = 0.f;
	PIItem it = (A && A->m_inventory) ? A->inventory().GetAny(sect) : NULL;
	if (!it)
		return -1;
	eatable = !!smart_cast<CEatableItem*>(it);
	weight = it->Weight();
	return int(it->object().ID());
}
static PIItem vr_bpt_owned(CActor* A, int id)
{
	if (!A || !A->m_inventory || id < 0)
		return NULL;
	CObject* O = Level().Objects.net_Find(u16(id));
	if (!O || !O->H_Parent() || O->H_Parent()->ID() != A->ID())
		return NULL;
	return smart_cast<CInventoryItem*>(O);
}
bool vr_bpt_drop(CActor* A, int id, const Fvector& pos) // the game's own drop, then sent to pos (as drop_item_and_teleport)
{
	if (!vr_bpt_owned(A, id))
		return false;
	NET_Packet P;
	CGameObject::u_EventGen(P, GE_OWNERSHIP_REJECT, A->ID());
	P.w_u16(u16(id));
	CGameObject::u_EventSend(P);
	CGameObject::u_EventGen(P, GE_CHANGE_POS, u16(id));
	P.w_vec3(pos);
	CGameObject::u_EventSend(P);
	return true;
}
// the dropped object: 0 gone, 1 still owned, 2 free in the world. centre = its matrix applied to bc. With set (and free,
// with a physics shell): placed at xf and stopped (the game's drop throws it along the view; B2b lets it fall from rest).
float vr_bpt_gravity() { return physics_world() ? physics_world()->Gravity() : 19.62f; }
// mode 0: only follow; 1: place at xf and give the velocities; 2: give the velocities only (B2c)
int vr_bpt_dropped(int id, const Fvector& bc, Fvector& centre, int mode, const Fmatrix& xf, const Fvector& lv, const Fvector& av, bool& was_set, Fmatrix& att)
{
	was_set = false;
	centre.set(0.f, 0.f, 0.f);
	att.identity();
	CObject* O = id >= 0 ? Level().Objects.net_Find(u16(id)) : NULL;
	if (!O)
		return 0;
	const bool free_ = !O->H_Parent();
	if (free_ && mode)
	{
		CPhysicsShellHolder* sh = smart_cast<CPhysicsShellHolder*>(O);
		if (sh && sh->PPhysicsShell())
		{
			if (mode == 1)
			{
				// the shell is placed by its root element; the object sits in it at ObjectInRoot (object = root * object_in_root).
				// Placing the root at the object's pose (B2b did) turned the item by that offset on its first physics frame.
				Fmatrix ioir, root;
				ioir.invert(sh->PPhysicsShell()->ObjectInRoot());
				root.mul_43(xf, ioir);
				sh->XFORM().set(xf);
				sh->PPhysicsShell()->SetTransform(root, mh_clear);
			}
			else // not placed again: the game's drop has just moved the object's own matrix to its drop point; take the shell's back
				sh->PPhysicsShell()->GetGlobalTransformDynamic(&sh->XFORM());
			sh->PPhysicsShell()->set_LinearVel(lv);
			sh->PPhysicsShell()->set_AngularVel(av);
			was_set = true;
		}
	}
	att.set(O->XFORM());
	if (free_ && !was_set)
	{
		// for the log: the shell's pose as of this update (the object's own matrix is refreshed later in the frame, so here it
		// is a frame old, except on the frames it was set above, which made the log's steps uneven)
		CPhysicsShellHolder* sh = smart_cast<CPhysicsShellHolder*>(O);
		if (sh && sh->PPhysicsShell())
			sh->PPhysicsShell()->GetGlobalTransformDynamic(&att);
	}
	att.transform_tiny(centre, bc);
	return free_ ? 2 : 1;
}
// B3b: the free item whose box (grown by reach) holds p, among the objects the actor's own pick-up sees (feel_touch);
// the nearest box centre wins. Its id or -1; its section, its matrix, whether it is a consumable.
int vr_bpt_near(CActor* A, const Fvector& p, float reach, LPSTR sect, Fmatrix& xf, bool& eatable)
{
	int best = -1;
	float bd = 1000.f;
	if (!A)
		return -1;
	for (CObject* O : A->feel_touch)
	{
		CInventoryItem* it = smart_cast<CInventoryItem*>(O);
		if (!it || O->H_Parent() || O->getDestroy() || !O->Visual() || !it->CanTake())
			continue;
		if (!O->getVisible()) // B3c: a lone round's own (box) model is hidden; it is still a thing at the hand
		{
			CWeaponAmmo* am = smart_cast<CWeaponAmmo*>(O);
			if (!am || am->m_boxCurr != 1 || am->m_boxSize < 2)
				continue;
		}
		const Fbox& bb = O->Visual()->getVisData().box;
		Fmatrix ix;
		ix.invert(O->XFORM());
		Fvector q, c;
		ix.transform_tiny(q, p);
		if (q.x < bb.min.x - reach || q.x > bb.max.x + reach || q.y < bb.min.y - reach || q.y > bb.max.y + reach || q.z < bb.min.z - reach || q.z > bb.max.z + reach)
			continue;
		bb.getcenter(c);
		O->XFORM().transform_tiny(c);
		const float d = c.distance_to(p);
		if (d < bd)
		{
			bd = d;
			best = int(O->ID());
			xr_strcpy(sect, 64, O->cNameSect().c_str());
			xf.set(O->XFORM());
			eatable = !!smart_cast<CEatableItem*>(it);
		}
	}
	return best;
}
// B3b2: where is the item? 0 gone, 1 the actor's, 2 not the actor's (free in the world, or someone else's)
int vr_bpt_where(CActor* A, int id)
{
	CObject* O = (A && id >= 0) ? Level().Objects.net_Find(u16(id)) : NULL;
	if (!O || O->getDestroy())
		return 0;
	return (O->H_Parent() && O->H_Parent()->ID() == A->ID()) ? 1 : 2;
}
// B3b2: the item's own model name and its box (centre and size in its own frame)
bool vr_bpt_shape(int id, LPSTR vis, Fvector& bc, Fvector& ext)
{
	CObject* O = id >= 0 ? Level().Objects.net_Find(u16(id)) : NULL;
	if (!O || !O->Visual())
		return false;
	xr_strcpy(vis, 256, O->cNameVisual().c_str());
	const Fbox& bb = O->Visual()->getVisData().box;
	bb.getcenter(bc);
	ext.set(bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z);
	return true;
}
bool vr_bpt_take(CActor* A, int id) // the game's own pick-up (the call behind the flat game's pick-up key)
{
	if (!A || id < 0)
		return false;
	Game().SendPickUpEvent(A->ID(), u16(id));
	return true;
}

// ---- GammaVR backpack B3c (SPIKE_PLAN Amendment 41 notes 34-36): rounds out of a lying box one at a time ----
// A round taken out of a box is a new ammo object of the box's kind with a count of one, whose model is the round model of
// its calibre class (gamedata/meshes/gvr/gvr_round_*.ogf, with its own small physics box), set on its server record: the
// game's save keeps it. An object with a count of one that still has a box model (the last round of a box, a spawned
// lone round) keeps its physics; its box model is hidden and the hand code draws a round in its place.
CSE_Abstract* CALifeSimulator__spawn_ammo(CALifeSimulator* self, LPCSTR section, const Fvector& position, u32 level_vertex_id,
	GameGraph::_GRAPH_ID game_vertex_id, ALife::_OBJECT_ID id_parent, int ammo_to_spawn); // alife_simulator_script.cpp (the script call create_ammo)
struct vr_rd_obj // as in player_hud.cpp
{
	int id, n, size, vis, rv; // vis: its own model is visible; rv: its own model is a round model
	char sect[64];
	char model[128];
	Fmatrix xf;
	Fvector bc, ext;
};
static bool vr_bpt_round_vis(CObject* O) { return O && strstr(O->cNameVisual().c_str(), "gvr_round_") != nullptr; }
static CWeaponAmmo* vr_bpt_ammo_obj(int id)
{
	CObject* O = id >= 0 ? Level().Objects.net_Find(u16(id)) : NULL;
	return (O && !O->getDestroy()) ? smart_cast<CWeaponAmmo*>(O) : nullptr;
}
// the count and the box size of an ammo object, and whether its own model is a round; false when it is not an ammo
// object (or does not exist yet)
bool vr_bpt_ammo_n(int id, int& n, int& size, bool& round_vis)
{
	CWeaponAmmo* a = vr_bpt_ammo_obj(id);
	if (!a)
		return false;
	n = int(a->m_boxCurr);
	size = int(a->m_boxSize);
	round_vis = vr_bpt_round_vis(a);
	return true;
}
static void vr_bpt_ammo_set(CWeaponAmmo* a, int n) // the object's count and its server record's (the save reads the record)
{
	a->m_boxCurr = u16(n);
	if (ai().get_alife())
		if (CSE_ALifeItemAmmo* sa = smart_cast<CSE_ALifeItemAmmo*>(ai().alife().objects().object(a->ID(), true)))
			sa->a_elapsed = u16(n);
}
// one round out of the box: its count falls by one and a new object of its kind with a count of one and the model given
// is made, the actor's (as the script call create_ammo makes one for an online parent). The new id or -1; n0 = the box's
// count before.
int vr_bpt_round_out(CActor* A, int id, LPCSTR model, int& n0)
{
	CWeaponAmmo* a = vr_bpt_ammo_obj(id);
	n0 = a ? int(a->m_boxCurr) : 0;
	if (!A || !a || a->H_Parent() || a->m_boxCurr < 2 || a->m_boxSize < 2 || !ai().get_alife())
		return -1;
	CALifeSimulator& sim = const_cast<CALifeSimulator&>(ai().alife());
	NET_Packet packet;
	packet.w_begin(M_SPAWN);
	packet.w_stringZ(a->cNameSect().c_str());
	CSE_Abstract* item = sim.spawn_item(a->cNameSect().c_str(), A->Position(), A->ai_location().level_vertex_id(), A->ai_location().game_vertex_id(), A->ID(), false);
	CSE_ALifeItemAmmo* ammo = item ? smart_cast<CSE_ALifeItemAmmo*>(item) : nullptr;
	if (!ammo)
	{
		if (item)
		{
			sim.server().FreeID(item->ID, 0);
			F_entity_Destroy(item);
		}
		return -1;
	}
	ammo->a_elapsed = 1;
	if (model && model[0] && item->visual())
		item->visual()->set_visual(model);
	item->Spawn_Write(packet, FALSE);
	sim.server().FreeID(item->ID, 0);
	F_entity_Destroy(item);
	ClientID clientID;
	clientID.set(0xffff);
	u16 dummy;
	packet.r_begin(dummy);
	CSE_Abstract* e = sim.server().Process_spawn(packet, clientID);
	if (!e)
		return -1;
	vr_bpt_ammo_set(a, n0 - 1);
	return int(e->ID);
}
// the round (the actor's, count 1) back into the lying box of its kind (an object with a box model): 1 done (the round is
// destroyed), 0 the box is full, -1 not possible. n0 = the box's count before.
int vr_bpt_round_back(CActor* A, int rid, int bid, int& n0)
{
	CWeaponAmmo* r = vr_bpt_ammo_obj(rid);
	CWeaponAmmo* b = vr_bpt_ammo_obj(bid);
	n0 = b ? int(b->m_boxCurr) : 0;
	if (!A || !r || !b || r == b || b->H_Parent() || !r->H_Parent() || r->H_Parent()->ID() != A->ID() || r->m_boxCurr != 1 || vr_bpt_round_vis(b) ||
		xr_strcmp(r->cNameSect().c_str(), b->cNameSect().c_str()))
		return -1;
	if (b->m_boxCurr >= b->m_boxSize)
		return 0;
	vr_bpt_ammo_set(b, n0 + 1);
	r->DestroyObject();
	return 1;
}
// every free ammo object the actor's own pick-up sees (feel_touch), of a kind whose box holds more than one. With hide,
// the box model of an object with a count of one is hidden, and any other's is shown again. Returns how many were written.
int vr_bpt_ammo_list(CActor* A, vr_rd_obj* out, int cap, bool hide)
{
	int n = 0;
	if (!A)
		return 0;
	for (CObject* O : A->feel_touch)
	{
		CWeaponAmmo* a = O ? smart_cast<CWeaponAmmo*>(O) : nullptr;
		if (!a || O->H_Parent() || O->getDestroy() || !O->Visual() || a->m_boxSize < 2)
			continue;
		const bool rv = vr_bpt_round_vis(O);
		{ // without hide every box model is shown again (a round model that could not be made)
			// an object the list has no room for is never hidden: nothing would draw a round in its place
			const bool want = !hide || rv || a->m_boxCurr != 1 || n >= cap;
			if (want != !!O->getVisible())
				O->setVisible(want ? TRUE : FALSE);
		}
		if (n >= cap)
			continue;
		vr_rd_obj& r = out[n++];
		r.id = int(O->ID());
		r.n = int(a->m_boxCurr);
		r.size = int(a->m_boxSize);
		r.vis = O->getVisible() ? 1 : 0;
		r.rv = rv ? 1 : 0;
		xr_strcpy(r.sect, O->cNameSect().c_str());
		xr_strcpy(r.model, O->cNameVisual().c_str());
		r.xf.set(O->XFORM());
		const Fbox& bb = O->Visual()->getVisData().box;
		bb.getcenter(r.bc);
		r.ext.set(bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z);
	}
	return n;
}
int vr_bpt_actor_rounds(CActor* A, LPCSTR sect) // every round of that kind the actor owns
{
	int n = 0;
	if (A && A->m_inventory)
		for (PIItem it : A->inventory().m_all)
			if (CWeaponAmmo* am = it ? smart_cast<CWeaponAmmo*>(it) : nullptr)
				if (!xr_strcmp(am->cNameSect().c_str(), sect))
					n += int(am->m_boxCurr);
	return n;
}
int vr_bpt_use(CActor* A, int id) // the game's own use: -1 not ours any more, 0 no script gate, 1 gate false (FDDA took it), 2 gate true and Eat called
{
	PIItem it = vr_bpt_owned(A, id);
	if (!it)
		return -1;
	::luabind::functor<bool> funct;
	if (!ai().script_engine().functor("_G.CInventory__eat", funct))
		return 0;
	CGameObject* GO = it->cast_game_object();
	if (GO && funct(GO->lua_game_object()))
	{
		A->inventory().Eat(it);
		return 2;
	}
	return 1;
}

// GammaVR backpack B2b probe (SPIKE_PLAN Amendment 41), TEST ONLY, no verdict. [vr15] lines.
//   -vr_bpx_drop <frame> <section>   one item of that section (GetAny) gets the game's own drop (GE_OWNERSHIP_REJECT) and
//                                    is then sent to the gun-side hand's place (GE_CHANGE_POS), as drop_item_and_teleport does
//   -vr_bpx_use <frame> <section>    the game's own use of one item of that section: the script gate _G.CInventory__eat, then Eat
//   -vr_bpx_cancel <frame>           lam2.stop_all_actions once (11a's cancel) at that frame
// From the first of these frames on, one line per VR frame for 600 frames: the dropped object, counts, weight, FDDA's dummy.
void vr_bpx_probe_step(CActor* A, u32 frame, bool hand_valid, const Fvector& hand)
{
	{ // B3b2 (TEST ONLY): -vr_bpx_spawn <frame> <section>: one item of that section made in the world at the gun-side hand's place
		static bool sp = false;
		static u32 sf = 0;
		static string128 ss = "";
		static int sn = 0;
		if (!sp)
		{
			sp = true;
			if (LPCSTR q = strstr(Core.Params, "-vr_bpx_spawn "))
				if (sscanf(q + 14, "%u %127s", &sf, ss) != 2)
					sf = 0;
			if (LPCSTR q = strstr(Core.Params, "-vr_bpx_spawn_n ")) // B3c (TEST ONLY): the count of a spawned ammo box
				sscanf(q + 16, "%d", &sn);
			if (sf)
				Msg("* [vr15] B3b2 spawn (TEST ONLY): '%s' at frame %u, count %d (0 = full)", ss, sf, sn);
		}
		if (sf && frame == sf && A)
		{
			if (!hand_valid || !pSettings->section_exist(ss) || !ai().get_alife())
				Msg("* [vr15] frame %u spawn: NOT done (%s)", frame, !hand_valid ? "no valid hand pose" : !ai().get_alife() ? "no simulator" : "no such section");
			else
			{
				CSE_Abstract* e = sn > 0 && pSettings->line_exist(ss, "box_size") && pSettings->r_s32(ss, "box_size") >= sn
					? CALifeSimulator__spawn_ammo(&const_cast<CALifeSimulator&>(ai().alife()), ss, hand, A->ai_location().level_vertex_id(), A->ai_location().game_vertex_id(), ALife::_OBJECT_ID(-1), sn)
					: const_cast<CALifeSimulator&>(ai().alife()).spawn_item(ss, hand, A->ai_location().level_vertex_id(), A->ai_location().game_vertex_id(), ALife::_OBJECT_ID(-1));
				Msg("* [vr15] frame %u spawn: '%s' id %d at %.4f %.4f %.4f (carried before: %d)", frame, ss, e ? int(e->ID) : -1, hand.x, hand.y, hand.z,
					A->inventory().GetAny(ss) ? 1 : 0);
			}
		}
	}
	static bool parsed = false;
	static u32 df = 0, uf = 0, cf = 0, first = 0;
	static string128 ds = "", us = "";
	static int did = -1;
	if (!parsed)
	{
		parsed = true;
		if (LPCSTR q = strstr(Core.Params, "-vr_bpx_drop "))
			if (sscanf(q + 13, "%u %127s", &df, ds) != 2)
				df = 0;
		if (LPCSTR q = strstr(Core.Params, "-vr_bpx_use "))
			if (sscanf(q + 12, "%u %127s", &uf, us) != 2)
				uf = 0;
		if (LPCSTR q = strstr(Core.Params, "-vr_bpx_cancel "))
			sscanf(q + 15, "%u", &cf);
		first = df && uf ? _min(df, uf) : (df ? df : uf);
		if (first)
			Msg("* [vr15] B2b probe (TEST ONLY): drop '%s' at frame %u, use '%s' at frame %u, cancel at frame %u", ds, df, us, uf, cf);
	}
	if (!first || !A || frame < first || frame > first + 600)
		return;
	if (df && frame == df)
	{
		int n = 0, u = 0;
		vr_item_count(A, ds, n, u);
		PIItem it = A->inventory().GetAny(ds);
		if (!it || !hand_valid)
			Msg("* [vr15] frame %u drop: %s", frame, it ? "no valid hand pose" : "no such item in the inventory");
		else
		{
			did = int(it->object().ID());
			NET_Packet P;
			CGameObject::u_EventGen(P, GE_OWNERSHIP_REJECT, A->ID());
			P.w_u16(u16(did));
			CGameObject::u_EventSend(P);
			CGameObject::u_EventGen(P, GE_CHANGE_POS, u16(did));
			P.w_vec3(hand);
			CGameObject::u_EventSend(P);
			Msg("* [vr15] frame %u drop: '%s' id %d (count %d, weight %.4f) rejected and sent to %.4f %.4f %.4f", frame, ds, did, n,
				A->inventory().TotalWeight(), hand.x, hand.y, hand.z);
		}
	}
	if (uf && frame == uf)
	{
		int n = 0, u = 0;
		vr_item_count(A, us, n, u);
		PIItem it = A->inventory().GetAny(us);
		::luabind::functor<bool> funct;
		if (!it)
			Msg("* [vr15] frame %u use: no '%s' in the inventory", frame, us);
		else if (!ai().script_engine().functor("_G.CInventory__eat", funct))
			Msg("* [vr15] frame %u use: _G.CInventory__eat NOT FOUND", frame);
		else
		{
			CGameObject* GO = it->cast_game_object();
			const int id = int(it->object().ID());
			const bool gate = GO && funct(GO->lua_game_object());
			if (gate)
				A->inventory().Eat(it);
			Msg("* [vr15] frame %u use: '%s' id %d (count %d uses %d): script gate %s, Eat %s", frame, us, id, n, u, gate ? "true" : "false",
				gate ? "called" : "not called");
		}
	}
	if (cf && frame == cf)
	{
		const bool ok = vr_lam2_skip();
		Msg("* [vr15] frame %u cancel: lam2.stop_all_actions %s", frame, ok ? "called" : "NOT FOUND");
	}
	int dn = 0, du = 0, un = 0, uu = 0, mn = 0, mu = 0;
	if (ds[0])
		vr_item_count(A, ds, dn, du);
	if (us[0])
		vr_item_count(A, us, un, uu);
	vr_item_count(A, "items_anm_dummy", mn, mu);
	CObject* O = did >= 0 ? Level().Objects.net_Find(u16(did)) : NULL;
	Fvector p = O ? O->Position() : Fvector().set(0.f, 0.f, 0.f);
	Msg("* [vr15s] frame %u drop '%s' count %d obj %s parent %d at %.4f %.4f %.4f | use '%s' count %d uses %d | dummy %d weight %.4f t %.4f",
		frame, ds, dn, did < 0 ? "none" : (O ? "alive" : "GONE"), O && O->H_Parent() ? int(O->H_Parent()->ID()) : -1, p.x, p.y, p.z,
		us, un, uu, mn, A->inventory().TotalWeight(), Device.fTimeGlobal);
}
