////////////////////////////////////////////////////////////////////////////
//	Module 		: UIActorMenu_script.cpp
//	Created 	: 18.04.2008
//	Author		: Evgeniy Sokolov
//	Description : UI ActorMenu script implementation
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "UIActorMenu.h"
#include "../UIGameCustom.h"

#include "UIWindow.h"
#include "UICellItemFactory.h"
#include "UIDragDropListEx.h"
#include "UIDragDropReferenceList.h"
#include "UICellCustomItems.h"

#include "../actor.h"
#include "../inventory_item.h"
#include "UICellItem.h"
#include "../ai_space.h"
#include "../../xrServerEntities/script_engine.h"
#include "eatable_item.h"

#include "UIPdaWnd.h"
#include "UITabControl.h"

#include "UIMainIngameWnd.h"
#include "UIZoneMap.h"
#include "UIMotionIcon.h"
#include "UIHudStatesWnd.h"
#include "UIMessagesWindow.h"

using namespace luabind;

// ============ COOP: these accessors dereference a UI that a DEDICATED SERVER does not have ========
//
// Every one of them is exported to Lua below, and every one dereferences CurrentGameUI() without
// checking it. On a headless server `CurrentGameUI()` is
//
//     g_hud ? HUD().GetGameUI() : nullptr
//
// and BOTH of its null paths end at the same unguarded member read. §14 step 8 P4 R4 measured what
// that costs: `get_maingame` faulted (c0000005 reading 0xa8 — the offset of UIMainIngameWnd),
// LuaJIT's lj_err_unwind_win64 CAUGHT the access violation and unwound, so the calling script
// carried on and hit it again — 264 times — until nested __C_specific_handler frames exhausted the
// stack. The thread then died holding critical sections and three others deadlocked behind it, so
// the process stayed ALIVE AND WEDGED: no crash, no log line, just a server that stops.
//
// The swallowing is what makes this severe. An AV that terminated would have named itself.
//
// So: check, name the CALLER, and return null instead of faulting. A Lua error on a nil return is a
// diagnosable failure with a script name attached; an access violation swallowed by the VM is not.
// Declared in script_game_object.h; taken by declaration rather than by including that header,
// which drags the whole game-object binding surface into a UI translation unit.
extern xr_vector<xr_string> get_lua_stack(lua_State* L);

static CUIGameCustom* coop_ui_or_null(LPCSTR who)
{
	CUIGameCustom* const ui = CurrentGameUI();
	if (ui)
		return ui;

	// BOUNDED, because the measured failure called this 264 times in one burst and an unbounded
	// traceback per call would bury the log it is meant to explain. The count keeps rising after
	// the tracebacks stop, so the rate stays visible without the volume.
	static u32 s_seen = 0;
	++s_seen;
	if (s_seen <= 8u || (s_seen % 64u) == 0u)
	{
		Msg("!COOP(uinull): %s called with NO game UI (CurrentGameUI() == null) — returning nil "
			"instead of dereferencing it. This is a client HUD binding running somewhere it has no "
			"UI, e.g. a dedicated server. count=%u", who, s_seen);

		// NOT print_stack(). Its first run produced NOTHING and the instrument had to be validated
		// before its silence could be read: `vscript_log` returns 0 in a release build unless
		// `-dbg` is on the command line, and this harness passes `-dbg` to the CLIENTS only. So the
		// traceback was suppressed by a log gate, not absent because the caller was not Lua — two
		// states that look identical in the log. `get_lua_stack` + Msg is ungated, which is why the
		// engine's own error path already uses it, and it lands in the log the harness scrapes.
		lua_State* const L = ai().script_engine().lua();
		if (!L)
		{
			Msg("!COOP(uinull):   no Lua state — this call did NOT come from a script");
		}
		else
		{
			xr_vector<xr_string> const stack = get_lua_stack(L);
			if (stack.empty())
				Msg("!COOP(uinull):   Lua stack is EMPTY — called from C++, not from a script");
			for (u32 f = 0; f < stack.size() && f < 24u; ++f)
				Msg("!COOP(uinull):   %s", stack[f].c_str());
		}
		FlushLog();
	}
	return nullptr;
}

CUIActorMenu* GetActorMenu()
{
	CUIGameCustom* const ui = coop_ui_or_null("get_actor_menu");
	return ui ? &ui->GetActorMenu() : nullptr;
}

CUIPdaWnd* GetPDAMenu()
{
	CUIGameCustom* const ui = coop_ui_or_null("get_pda_menu");
	return ui ? &ui->GetPdaMenu() : nullptr;
}

CUIMainIngameWnd* GetMainGameMenu()
{
	CUIGameCustom* const ui = coop_ui_or_null("get_maingame");
	return ui ? ui->UIMainIngameWnd : nullptr;
}

CUIMessagesWindow* GetMessagesMenu()
{
	CUIGameCustom* const ui = coop_ui_or_null("get_messages_menu");
	return ui ? ui->m_pMessagesWnd : nullptr;
}

u8 GrabMenuMode()
{
	CUIGameCustom* const ui = coop_ui_or_null("GrabMenuMode");
	if (!ui)
		return (u8)0;
	return (u8)(ui->GetActorMenu().GetMenuMode());
}

CScriptGameObject* CUIActorMenu::GetCurrentItemAsGameObject()
{
	CGameObject* GO = smart_cast<CGameObject*>(CurrentIItem());
	if (GO)
		return GO->lua_game_object();

	return (0);
}

bool CUIActorMenu::CanRepairItem(PIItem item)
{
	if (item->GetCondition() > 0.99f)
	{
		return false;
	}
	LPCSTR item_name = item->m_section_id.c_str();

	CEatableItem* EItm = smart_cast<CEatableItem*>(item);
	if (EItm)
	{
		bool allow_repair = !!READ_IF_EXISTS(pSettings, r_bool, item_name, "allow_repair", false);
		if (!allow_repair)
			return false;
	}

	LPCSTR partner = m_pPartnerInvOwner->CharacterInfo().Profile().c_str();

	::luabind::functor<bool> funct;
	R_ASSERT2(
		ai().script_engine().functor("inventory_upgrades.can_repair_item", funct),
		make_string("Failed to get functor <inventory_upgrades.can_repair_item>, item = %s", item_name)
	);
	bool can_repair = funct(item_name, item->GetCondition(), partner);

	return can_repair;
}

LPCSTR CUIActorMenu::RepairQuestion(PIItem item, bool can_repair)
{
	LPCSTR partner = m_pPartnerInvOwner->CharacterInfo().Profile().c_str();
	LPCSTR item_name = item->m_section_id.c_str();
	::luabind::functor<LPCSTR> funct2;
	R_ASSERT2(
		ai().script_engine().functor("inventory_upgrades.question_repair_item", funct2),
		make_string("Failed to get functor <inventory_upgrades.question_repair_item>, item = %s", item_name)
	);
	LPCSTR question = funct2(item->m_section_id.c_str(), item->GetCondition(), can_repair, partner);

	return question;
}

void CUIActorMenu::TryRepairItem(CUIWindow* w, void* d)
{
	PIItem item = get_upgrade_item();
	if (!item)
	{
		return;
	}

	LPCSTR item_name = item->m_section_id.c_str();

	bool can_repair = CanRepairItem(item);

	::luabind::functor<bool> funct;
	R_ASSERT2(
		ai().script_engine().functor("inventory_upgrades.can_afford_repair_item", funct),
		make_string("Failed to get functor <inventory_upgrades.can_afford_repair_item>, item = %s", item_name)
	);
	bool enough_money = funct(item_name, item->GetCondition());

	if (can_repair)
	{
		if (enough_money)
		{
			m_repair_mode = true;
			CallMessageBoxYesNo(RepairQuestion(item, true));
		}
		else
			CallMessageBoxOK(RepairQuestion(item, true));
	}
}

void CUIActorMenu::RepairEffect_CurItem()
{
	PIItem item = CurrentIItem();
	if (!item)
	{
		return;
	}
	LPCSTR item_name = item->m_section_id.c_str();

	::luabind::functor<void> funct;
	R_ASSERT(ai().script_engine().functor( "inventory_upgrades.effect_repair_item", funct ));
	funct(item_name, item->GetCondition());

	item->SetCondition(1.0f);
	UpdateConditionProgressBars();
	SeparateUpgradeItem();
	CUICellItem* itm = CurrentItem();
	if (itm)
		itm->UpdateConditionProgressBar();
}

bool CUIActorMenu::CanUpgradeItem(PIItem item)
{
	VERIFY(item && m_pPartnerInvOwner);
	LPCSTR item_name = item->m_section_id.c_str();
	LPCSTR partner = m_pPartnerInvOwner->CharacterInfo().Profile().c_str();

	::luabind::functor<bool> funct;
	R_ASSERT2(
		ai().script_engine().functor( "inventory_upgrades.can_upgrade_item", funct ),
		make_string( "Failed to get functor <inventory_upgrades.can_upgrade_item>, item = %s, mechanic = %s", item_name,
			partner )
	);

	return funct(item_name, partner);
}

void CUIActorMenu::CurModeToScript()
{
	int mode = (int)m_currMenuMode;
	::luabind::functor<void> funct;
	R_ASSERT(ai().script_engine().functor( "actor_menu.actor_menu_mode", funct ));
	funct(mode);
}

void CUIActorMenu::HighlightSectionInSlot(LPCSTR section, u8 type, u16 slot_id)
{
	CUIDragDropListEx* slot_list = m_pInventoryBagList;
	switch (type)
	{
	case EDDListType::iActorBag:
		slot_list = m_pInventoryBagList;
		break;
	case EDDListType::iActorBelt:
		slot_list = m_pInventoryBeltList;
		break;
	case EDDListType::iActorSlot:
		slot_list = GetSlotList(slot_id);
		break;
	case EDDListType::iActorTrade:
		slot_list = m_pTradeActorBagList;
		break;
	case EDDListType::iDeadBodyBag:
		slot_list = m_pDeadBodyBagList;
		break;
	case EDDListType::iPartnerTrade:
		slot_list = m_pTradePartnerList;
		break;
	case EDDListType::iPartnerTradeBag:
		slot_list = m_pTradePartnerBagList;
		break;
	case EDDListType::iQuickSlot:
		slot_list = m_pQuickSlot;
		break;
	case EDDListType::iTrashSlot:
		slot_list = m_pTrashList;
		break;
	}

	if (!slot_list)
		return;

	u32 const cnt = slot_list->ItemsCount();
	for (u32 i = 0; i < cnt; ++i)
	{
		CUICellItem* ci = slot_list->GetItemIdx(i);
		PIItem item = (PIItem)ci->m_pData;
		if (!item)
			continue;

		if (!strcmp(section, item->m_section_id.c_str()) == 0)
			continue;

		ci->m_select_armament = true;
	}

	m_highlight_clear = false;
}


void CUIActorMenu::HighlightForEachInSlot(const ::luabind::functor<bool>& functor, u8 type, u16 slot_id)
{
	if (!functor)
		return;

	CUIDragDropListEx* slot_list = m_pInventoryBagList;
	switch (type)
	{
	case EDDListType::iActorBag:
		slot_list = m_pInventoryBagList;
		break;
	case EDDListType::iActorBelt:
		slot_list = m_pInventoryBeltList;
		break;
	case EDDListType::iActorSlot:
		slot_list = GetSlotList(slot_id);
		break;
	case EDDListType::iActorTrade:
		slot_list = m_pTradeActorBagList;
		break;
	case EDDListType::iDeadBodyBag:
		slot_list = m_pDeadBodyBagList;
		break;
	case EDDListType::iPartnerTrade:
		slot_list = m_pTradePartnerList;
		break;
	case EDDListType::iPartnerTradeBag:
		slot_list = m_pTradePartnerBagList;
		break;
	case EDDListType::iQuickSlot:
		slot_list = m_pQuickSlot;
		break;
	case EDDListType::iTrashSlot:
		slot_list = m_pTrashList;
		break;
	}

	if (!slot_list)
		return;

	u32 const cnt = slot_list->ItemsCount();
	for (u32 i = 0; i < cnt; ++i)
	{
		CUICellItem* ci = slot_list->GetItemIdx(i);
		PIItem item = (PIItem)ci->m_pData;
		if (!item)
			continue;

		if (functor(item->object().cast_game_object()->lua_game_object()) == false)
			continue;

		ci->m_select_armament = true;
	}

	m_highlight_clear = false;
}

#pragma optimize("s",on)
void CUIActorMenu::script_register(lua_State* L)
{
	module(L)
	[
		class_<enum_exporter<EDDListType>>("EDDListType")
		.enum_("EDDListType")
		[
			value("iActorBag", int(EDDListType::iActorBag)),
			value("iActorBelt", int(EDDListType::iActorBelt)),
			value("iActorSlot", int(EDDListType::iActorSlot)),
			value("iActorTrade", int(EDDListType::iActorTrade)),
			value("iDeadBodyBag", int(EDDListType::iDeadBodyBag)),
			value("iInvalid", int(EDDListType::iInvalid)),
			value("iPartnerTrade", int(EDDListType::iPartnerTrade)),
			value("iPartnerTradeBag", int(EDDListType::iPartnerTradeBag)),
			value("iQuickSlot", int(EDDListType::iQuickSlot)),
			value("iTrashSlot", int(EDDListType::iTrashSlot))
		],

		class_<CUIActorMenu, CUIDialogWnd, CUIWndCallback>("CUIActorMenu")
		.def(constructor<>())
		.def("get_drag_item", &CUIActorMenu::GetCurrentItemAsGameObject)
		.def("highlight_section_in_slot", &CUIActorMenu::HighlightSectionInSlot)
		.def("highlight_for_each_in_slot", &CUIActorMenu::HighlightForEachInSlot)
		.def("refresh_current_cell_item", &CUIActorMenu::RefreshCurrentItemCell)
		.def("IsShown", &CUIActorMenu::IsShown)
		.def("ShowDialog", &CUIActorMenu::ShowDialog)
		.def("HideDialog", &CUIActorMenu::HideDialog)
		.def("ToSlot", &CUIActorMenu::ToSlotScript)
		.def("ToBelt", &CUIActorMenu::ToBeltScript),

		class_<CUIPdaWnd, CUIDialogWnd>("CUIPdaWnd")
		.def(constructor<>())
		.def("IsShown", &CUIPdaWnd::IsShown)
		.def("ShowDialog", &CUIPdaWnd::ShowDialog)
		.def("HideDialog", &CUIPdaWnd::HideDialog)
		.def("SetActiveSubdialog", &CUIPdaWnd::SetActiveSubdialog_script)
		.def("SetActiveDialog", &CUIPdaWnd::SetActiveDialog)
		.def("GetActiveDialog", &CUIPdaWnd::GetActiveDialog)
		.def("GetActiveSection", &CUIPdaWnd::GetActiveSection)
		.def("SetPdaXml", &CUIPdaWnd::SetPdaXml)
		.def("GetPdaXml", &CUIPdaWnd::GetPdaXml)
		.def("GetTabControl", &CUIPdaWnd::GetTabControl),

		class_<CUIMainIngameWnd, CUIWindow>("CUIMainIngameWnd")
		.def(constructor<>())
		.def_readonly("UIStaticDiskIO", &CUIMainIngameWnd::UIStaticDiskIO)
		.def_readonly("UIStaticQuickHelp", &CUIMainIngameWnd::UIStaticQuickHelp)
		.def_readonly("UIMotionIcon", &CUIMainIngameWnd::UIMotionIcon)
		.def_readonly("UIZoneMap", &CUIMainIngameWnd::UIZoneMap)
		.def_readonly("m_ui_hud_states", &CUIMainIngameWnd::m_ui_hud_states)
		.def_readonly("m_ind_bleeding", &CUIMainIngameWnd::m_ind_bleeding)
		.def_readonly("m_ind_radiation", &CUIMainIngameWnd::m_ind_radiation)
		.def_readonly("m_ind_starvation", &CUIMainIngameWnd::m_ind_starvation)
		.def_readonly("m_ind_weapon_broken", &CUIMainIngameWnd::m_ind_weapon_broken)
		.def_readonly("m_ind_helmet_broken", &CUIMainIngameWnd::m_ind_helmet_broken)
		.def_readonly("m_ind_outfit_broken", &CUIMainIngameWnd::m_ind_outfit_broken)
		.def_readonly("m_ind_overweight", &CUIMainIngameWnd::m_ind_overweight)
		.def_readonly("m_ind_boost_psy", &CUIMainIngameWnd::m_ind_boost_psy)
		.def_readonly("m_ind_boost_radia", &CUIMainIngameWnd::m_ind_boost_radia)
		.def_readonly("m_ind_boost_chem", &CUIMainIngameWnd::m_ind_boost_chem)
		.def_readonly("m_ind_boost_wound", &CUIMainIngameWnd::m_ind_boost_wound)
		.def_readonly("m_ind_boost_weight", &CUIMainIngameWnd::m_ind_boost_weight)
		.def_readonly("m_ind_boost_health", &CUIMainIngameWnd::m_ind_boost_health)
		.def_readonly("m_ind_boost_power", &CUIMainIngameWnd::m_ind_boost_power)
		.def_readonly("m_ind_boost_rad", &CUIMainIngameWnd::m_ind_boost_rad)
		.def("GetQuickSlotIcons", &CUIMainIngameWnd::GetQuickSlotIconsScript)
		.def_readonly("m_QuickSlotText1", &CUIMainIngameWnd::m_QuickSlotText1)
		.def_readonly("m_QuickSlotText2", &CUIMainIngameWnd::m_QuickSlotText2)
		.def_readonly("m_QuickSlotText3", &CUIMainIngameWnd::m_QuickSlotText3)
		.def_readonly("m_QuickSlotText4", &CUIMainIngameWnd::m_QuickSlotText4),

		class_<CUIZoneMap>("CUIZoneMap")
		.def(constructor<>())
		.def_readwrite("disabled", &CUIZoneMap::disabled)
		.def_readonly("visible", &CUIZoneMap::visible)
		.def("MapFrame", &CUIZoneMap::MapFrame)
		.def("Background", &CUIZoneMap::Background),

		class_<CUIMotionIcon, CUIWindow>("CUIMotionIcon")
		.def(constructor<>()),

		class_<CUIMessagesWindow, CUIWindow>("CUIMessagesWindow")
		.def(constructor<>()),

		class_<CUIHudStatesWnd, CUIWindow>("CUIHudStatesWnd")
		.def(constructor<>())
		.def_readonly("m_back", &CUIHudStatesWnd::m_back)
		.def_readonly("m_ui_weapon_ammo_color_active", &CUIHudStatesWnd::m_ui_weapon_ammo_color_active)
		.def_readonly("m_ui_weapon_ammo_color_inactive", &CUIHudStatesWnd::m_ui_weapon_ammo_color_inactive)
		.def_readonly("m_ui_weapon_cur_ammo", &CUIHudStatesWnd::m_ui_weapon_cur_ammo)
		.def_readonly("m_ui_weapon_fmj_ammo", &CUIHudStatesWnd::m_ui_weapon_fmj_ammo)
		.def_readonly("m_ui_weapon_ap_ammo", &CUIHudStatesWnd::m_ui_weapon_ap_ammo)
		.def_readonly("m_ui_weapon_third_ammo", &CUIHudStatesWnd::m_ui_weapon_third_ammo)
		.def_readonly("m_fire_mode", &CUIHudStatesWnd::m_fire_mode)
		.def_readonly("m_ui_grenade", &CUIHudStatesWnd::m_ui_grenade)
		.def_readonly("m_ui_weapon_icon", &CUIHudStatesWnd::m_ui_weapon_icon)
		.def_readonly("m_ui_health_bar", &CUIHudStatesWnd::m_ui_health_bar)
		.def_readonly("m_ui_stamina_bar", &CUIHudStatesWnd::m_ui_stamina_bar)
		.def_readonly("m_ui_psy_bar", &CUIHudStatesWnd::m_ui_psy_bar)
		.def_readonly("m_radia_damage", &CUIHudStatesWnd::m_radia_damage)

		// Tronex
		.def_readwrite("m_ui_health_bar_show", &CUIHudStatesWnd::m_ui_health_bar_show)
		.def_readwrite("m_ui_stamina_bar_show", &CUIHudStatesWnd::m_ui_stamina_bar_show)
		.def_readwrite("m_ui_psy_bar_show", &CUIHudStatesWnd::m_ui_psy_bar_show)
	];

	module(L, "ActorMenu")
	[
		def("get_pda_menu", &GetPDAMenu),
		def("get_actor_menu", &GetActorMenu),
		def("get_menu_mode", &GrabMenuMode),
		def("get_maingame", &GetMainGameMenu),

		// NLTP_ASHES
		def("get_messages_menu", &GetMessagesMenu)
	];
}
