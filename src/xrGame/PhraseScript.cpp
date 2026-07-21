#include "pch_script.h"
#include "PhraseScript.h"
#include "script_engine.h"
#include "ai_space.h"
#include "gameobject.h"
#include "script_game_object.h"
#include "infoportion.h"
#include "inventoryowner.h"
#include "ai_debug.h"
#include "ui/xrUIXmlParser.h"
#include "actor.h"
#include "level.h"                                 // MP fork (§19 co-op): Level().Send
#include "../xrNetServer/xr_enet_transport.h"      // MP fork (§19 co-op): xr_enet::enabled
#include "../xrServerEntities/xrMessages.h"        // MP fork (§19 co-op): M_XRNET_DIALOG_ACTION


//загрузка из XML файла
void CDialogScriptHelper::Load(CUIXml* uiXml, XML_NODE* phrase_node)
{
	LoadSequence(uiXml, phrase_node, "precondition", m_Preconditions);
	LoadSequence(uiXml, phrase_node, "action", m_ScriptActions);

	LoadSequence(uiXml, phrase_node, "has_info", m_HasInfo);
	LoadSequence(uiXml, phrase_node, "dont_has_info", m_DontHasInfo);

	LoadSequence(uiXml, phrase_node, "give_info", m_GiveInfo);
	LoadSequence(uiXml, phrase_node, "disable_info", m_DisableInfo);
}

template <class T>
void CDialogScriptHelper::LoadSequence(CUIXml* uiXml, XML_NODE* phrase_node,
                                       LPCSTR tag, T& str_vector)
{
	int tag_num = uiXml->GetNodesNum(phrase_node, tag);
	str_vector.clear();
	for (int i = 0; i < tag_num; ++i)
	{
		LPCSTR tag_text = uiXml->Read(phrase_node, tag, i, NULL);
		str_vector.push_back(tag_text);
	}
}

bool CDialogScriptHelper::CheckInfo(const CInventoryOwner* pOwner) const
{
	THROW(pOwner);

	for (u32 i = 0; i < m_HasInfo.size(); ++i)
	{
		if (!Actor()->HasInfo(m_HasInfo[i]))
		{
#ifdef DEBUG
			if(psAI_Flags.test(aiDialogs) )
				Msg("----rejected: [%s] has info %s", pOwner->Name(), *m_HasInfo[i]);
#endif
			return false;
		}
	}

	for (u32 i = 0; i < m_DontHasInfo.size(); i++)
	{
		if (Actor()->HasInfo(m_DontHasInfo[i]))
		{
#ifdef DEBUG
			if(psAI_Flags.test(aiDialogs) )
				Msg("----rejected: [%s] dont has info %s", pOwner->Name(), *m_DontHasInfo[i]);
#endif
			return false;
		}
	}
	return true;
}


void CDialogScriptHelper::TransferInfo(const CInventoryOwner* pOwner) const
{
	THROW(pOwner);

	for (u32 i = 0; i < m_GiveInfo.size(); ++i)
		Actor()->TransferInfo(m_GiveInfo[i], true);

	for (u32 i = 0; i < m_DisableInfo.size(); ++i)
		Actor()->TransferInfo(m_DisableInfo[i], false);
}

LPCSTR CDialogScriptHelper::GetScriptText(LPCSTR str_to_translate, const CGameObject* pSpeakerGO1,
                                          const CGameObject* pSpeakerGO2, LPCSTR dialog_id, LPCSTR phrase_id)
{
	if (!m_sScriptTextFunc.size())
		return str_to_translate;

	::luabind::functor<LPCSTR> lua_function;
#ifdef DIALOG_UPGRADE
	::luabind::object parameters_table = ::luabind::newtable(ai().script_engine().lua());
	string256 str = {0};
	xr_sprintf(str, sizeof(str), m_sScriptTextFunc.c_str());
	LPCSTR v1 = strchr(str, '(');
	LPCSTR v2 = strchr(str, ')');
	if (v1 && v2 && (v1 < v2))
	{
		str[v1 - str] = '\0';
		str[v2 - str] = '\0';
		LPCSTR parameters_table_str = v1 + 1;
		int n = _GetItemCount(parameters_table_str, ':');
		for (int k = 0; k < n; k++)
		{
			string64 tmp;
			_GetItem(parameters_table_str, k, tmp, sizeof(tmp), ':');
			parameters_table[k + 1] = tmp;
		}
	}
	bool functor_exists = ai().script_engine().functor(str, lua_function);
	THROW3(functor_exists, "Cannot find phrase script text ", m_sScriptTextFunc.c_str());
	return lua_function(pSpeakerGO1->lua_game_object(), pSpeakerGO2->lua_game_object(), dialog_id, phrase_id, "", parameters_table);
#else
	bool functor_exists = ai().script_engine().functor(m_sScriptTextFunc.c_str(), lua_function);
	THROW3(functor_exists, "Cannot find phrase script text ", m_sScriptTextFunc.c_str());

	LPCSTR res = lua_function(pSpeakerGO1->lua_game_object(),
	                          pSpeakerGO2->lua_game_object(),
	                          dialog_id,
	                          phrase_id);

	return res;
#endif
}

bool CDialogScriptHelper::Precondition(const CGameObject* pSpeakerGO, LPCSTR dialog_id, LPCSTR phrase_id) const
{
	bool predicate_result = true;

	if (!CheckInfo(smart_cast<const CInventoryOwner*>(pSpeakerGO)))
	{
#ifdef DEBUG
			if (psAI_Flags.test(aiDialogs))
				Msg("dialog [%s] phrase[%s] rejected by CheckInfo",dialog_id,phrase_id);
#endif
		return false;
	}

	for (u32 i = 0; i < Preconditions().size(); ++i)
	{
		::luabind::functor<bool> lua_function;
		THROW(*Preconditions()[i]);
		bool functor_exists = ai().script_engine().functor(*Preconditions()[i], lua_function);
		THROW3(functor_exists, "Cannot find precondition", *Preconditions()[i]);
		predicate_result = lua_function(pSpeakerGO->lua_game_object());
		if (!predicate_result)
		{
#ifdef DEBUG
			if (psAI_Flags.test(aiDialogs))
				Msg("dialog [%s] phrase[%s] rejected by script predicate", dialog_id, phrase_id);
#endif
			break;
		}
	}
	return predicate_result;
}

// MP fork (§19 co-op): not every dialogue action changes the world. Logging what phrases
// actually carry settled two bugs at once:
//   "Goodbye" -> dialogs.break_dialog      (closes the conversation window)
//   "[Trade]" -> dialogs.npc_is_trader     (turns the trade UI on)
// Both are things that happen to the PLAYER, and both were being shipped to a server that has
// no conversation window and no trade UI, so they silently did nothing. Meanwhile money,
// items, info portions and tasks genuinely must run on the server or they evaporate.
//
// So route per action rather than per phrase: UI actions run on the client that asked, world
// actions run on the server, and each runs in exactly one place. Matched on the function name
// so a module prefix does not matter, and kept as a list because more will surface.
static bool coop_is_client_side_action(LPCSTR action)
{
	if (!action)
		return false;

	static LPCSTR const client_actions[] =
	{
		"break_dialog",   // ends the conversation - Goodbye
		"npc_is_trader",  // switches the talk window into trade mode
		"start_trade",    // opens the trade menu directly
		"disable_ui",
		"enable_ui",
	};

	for (u32 i = 0; i < (sizeof(client_actions) / sizeof(client_actions[0])); ++i)
		if (strstr(action, client_actions[i]))
			return true;

	return false;
}

void CDialogScriptHelper::Action(const CGameObject* pSpeakerGO, LPCSTR dialog_id, LPCSTR phrase_id) const
{
	for (u32 i = 0; i < Actions().size(); ++i)
	{
		::luabind::functor<void> lua_function;
		THROW(*Actions()[i]);
		bool functor_exists = ai().script_engine().functor(*Actions()[i], lua_function);
		THROW3(functor_exists, "Cannot find phrase dialog script function", *Actions()[i]);
		lua_function(pSpeakerGO->lua_game_object(), dialog_id);
	}
	TransferInfo(smart_cast<const CInventoryOwner*>(pSpeakerGO));
}

bool CDialogScriptHelper::Precondition(const CGameObject* pSpeakerGO1,
                                       const CGameObject* pSpeakerGO2,
                                       LPCSTR dialog_id,
                                       LPCSTR phrase_id,
                                       LPCSTR next_phrase_id) const
{
	bool predicate_result = true;

	if (!CheckInfo(smart_cast<const CInventoryOwner*>(pSpeakerGO1)))
	{
#ifdef DEBUG
		if (psAI_Flags.test(aiDialogs))
			Msg("dialog [%s] phrase[%s] rejected by CheckInfo",dialog_id,phrase_id);
#endif
		return false;
	}
	for (u32 i = 0; i < Preconditions().size(); ++i)
	{
		::luabind::functor<bool> lua_function;
		THROW(*Preconditions()[i]);

#ifdef DIALOG_UPGRADE
		::luabind::object parameters_table = ::luabind::newtable(ai().script_engine().lua());
		string256 str = {0};
		xr_sprintf(str, sizeof(str), Preconditions()[i].c_str());
		LPCSTR v1 = strchr(str, '(');
		LPCSTR v2 = strchr(str, ')');
		if (v1 && v2 && (v1 < v2))
		{
			str[v1 - str] = '\0';
			str[v2 - str] = '\0';
			LPCSTR parameters_table_str = v1 + 1;
			int n = _GetItemCount(parameters_table_str, ':');
			for (int k = 0; k < n; k++)
			{
				string64 tmp;
				_GetItem(parameters_table_str, k, tmp, sizeof(tmp), ':');
				parameters_table[k + 1] = tmp;
			}
		}
		bool is_positive = true;
		string256 lua_function_str = {0};
		GetLuaFunctionStringAndHeaderFlag(str, lua_function_str, sizeof(lua_function_str), is_positive);
		bool functor_exists = ai().script_engine().functor(lua_function_str, lua_function);
		THROW3(functor_exists, "Cannot find phrase precondition", Preconditions()[i].c_str());
		predicate_result = lua_function(pSpeakerGO1->lua_game_object(), pSpeakerGO2->lua_game_object(), dialog_id, phrase_id, next_phrase_id, parameters_table);
		predicate_result = (is_positive == true) ? predicate_result : !predicate_result;
#else
		bool functor_exists = ai().script_engine().functor(*Preconditions()[i], lua_function);
		THROW3(functor_exists, "Cannot find phrase precondition", *Preconditions()[i]);
		predicate_result = lua_function(pSpeakerGO1->lua_game_object(), pSpeakerGO2->lua_game_object(), dialog_id,
		                                phrase_id, next_phrase_id);
#endif
		if (!predicate_result)
		{
#ifdef DEBUG
			if (psAI_Flags.test(aiDialogs))
				Msg("dialog [%s] phrase[%s] rejected by script predicate",dialog_id,phrase_id);
#endif
			break;
		}
	}
	return predicate_result;
}


void CDialogScriptHelper::Action(const CGameObject* pSpeakerGO1, const CGameObject* pSpeakerGO2, LPCSTR dialog_id,
                                 LPCSTR phrase_id) const
{
	// MP fork (§19 co-op): a dialogue ACTION is a change to the world — money and items
	// changing hands, info portions, task state. Run it on a thin client and only that
	// client's copy changes; the server, which owns the world, never learns, so the effect
	// evaporates on the next update or the next session. Hand it to the server instead: it
	// has both speakers and the same scripts, so it can run exactly this function for real.
	// PRECONDITIONS deliberately stay local — they only read state and the phrase list has to
	// be built synchronously to draw the menu.
	const bool coop_client = xr_enet::enabled() && !ai().get_alife();
	const bool coop_server = xr_enet::enabled() && !!ai().get_alife();

	if (coop_client && pSpeakerGO1 && pSpeakerGO2)
	{
		// Hand the phrase to the server so its world-changing actions are real. It skips the
		// client-side ones below, and we run those here — each action happens exactly once.
		NET_Packet packet;
		packet.w_begin(M_XRNET_DIALOG_ACTION);
		packet.w_u16(pSpeakerGO1->ID());
		packet.w_u16(pSpeakerGO2->ID());
		packet.w_stringZ(dialog_id ? dialog_id : "");
		packet.w_stringZ(phrase_id ? phrase_id : "");
		Level().Send(packet, net_flags(TRUE, TRUE));
	}

	// Info portions are world state: the server owns them.
	if (!coop_client)
		TransferInfo(smart_cast<const CInventoryOwner*>(pSpeakerGO1));

	for (u32 i = 0; i < Actions().size(); ++i)
	{
		// Each action belongs to exactly one side — see coop_is_client_side_action.
		const bool client_side = coop_is_client_side_action(*Actions()[i]);
		if ((coop_client && !client_side) || (coop_server && client_side))
			continue;

		::luabind::functor<void> lua_function;
		THROW(*Actions()[i]);

#ifdef DIALOG_UPGRADE
		::luabind::object parameters_table = ::luabind::newtable(ai().script_engine().lua());
		string256 str = {0};
		xr_sprintf(str, sizeof(str), Actions()[i].c_str());
		LPCSTR v1 = strchr(str, '(');
		LPCSTR v2 = strchr(str, ')');
		if (v1 && v2 && (v1 < v2))
		{
			str[v1 - str] = '\0';
			str[v2 - str] = '\0';
			LPCSTR parameters_table_str = v1 + 1;
			int n = _GetItemCount(parameters_table_str, ':');
			for (int k = 0; k < n; k++)
			{
				string64 tmp;
				_GetItem(parameters_table_str, k, tmp, sizeof(tmp), ':');
				parameters_table[k + 1] = tmp;
			}
		}
		bool is_positive = true;
		string256 lua_function_str = {0};
		GetLuaFunctionStringAndHeaderFlag(str, lua_function_str, sizeof(lua_function_str), is_positive);
		bool functor_exists = ai().script_engine().functor(lua_function_str, lua_function);
		THROW3(functor_exists, "Cannot find phrase dialog script function", Actions()[i].c_str());
		try
		{
			lua_function(pSpeakerGO1->lua_game_object(), pSpeakerGO2->lua_game_object(), dialog_id, phrase_id, "", parameters_table);
		}
		catch (...)
		{
		}
#else
		bool functor_exists = ai().script_engine().functor(*Actions()[i], lua_function);
		THROW3(functor_exists, "Cannot find phrase dialog script function", *Actions()[i]);
		try
		{
			lua_function(pSpeakerGO1->lua_game_object(), pSpeakerGO2->lua_game_object(), dialog_id, phrase_id);
		}
		catch (...)
		{
		}
#endif
	}
}

#ifdef DIALOG_UPGRADE
void CDialogScriptHelper::GetLuaFunctionStringAndHeaderFlag(char *str, char *dst, int dst_size, bool &is_positive) const
{
	if (str && (str[0] != 0))
	{
		switch (str[0])
		{
		case '!':
			xr_sprintf(dst, dst_size, str + 1);
			is_positive = false;
			break;
		case '=':
			xr_sprintf(dst, dst_size, str + 1);
			is_positive = true;
			break;
		default:
			is_positive = true;
			xr_sprintf(dst, dst_size, str);
			break;
		}
	}
}
#endif
