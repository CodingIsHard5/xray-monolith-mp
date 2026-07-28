//////////////////////////////////////////////////////////////////////
// inventory_owner_info.h:	для работы с сюжетной информацией
//
//////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "InventoryOwner.h"
#include "GameObject.h"
#include "xrMessages.h"
#include "ai_space.h"
#include "ai_debug.h"
#include "alife_simulator.h"
#include "alife_registry_container.h"
#include "script_game_object.h"
#include "level.h"
#include "infoportion.h"
#include "alife_registry_wrappers.h"
#include "script_callback_ex.h"
#include "game_object_space.h"
#include "xrServer.h"                              // MP fork (§14 step 8 Q4): the server's own broadcast
#include "game_sv_base.h"                          // MP fork (§14 step 8 Q4): game_sv_GameState::u_EventSend
#include "../xrNetServer/xr_enet_transport.h"      // MP fork (§14 step 8 Q4): xr_enet::enabled()

void CInventoryOwner::OnEvent(NET_Packet& P, u16 type)
{
	switch (type)
	{
	case GE_INFO_TRANSFER:
		{
			u16 id;
			shared_str info_id;
			u8 add_info;

			P.r_u16(id); //отправитель
			P.r_stringZ(info_id); //номер полученной информации
			P.r_u8(add_info); //добавление или убирание информации

			if (add_info)
				OnReceiveInfo(info_id);
			else
				OnDisableInfo(info_id);
		}
		break;
	}
}


bool CInventoryOwner::OnReceiveInfo(shared_str info_id) const
{
	VERIFY(info_id.size());
	//добавить запись в реестр
	KNOWN_INFO_VECTOR& known_info = m_known_info_registry->registry().objects();
	KNOWN_INFO_VECTOR_IT it = std::find_if(known_info.begin(), known_info.end(), CFindByIDPred(info_id));
	if (known_info.end() == it)
		known_info.push_back(/*INFO_DATA(*/info_id/*, Level().GetGameTime())*/);
	else
		return false;

#ifdef DEBUG
	if(psAI_Flags.test(aiInfoPortion))
		Msg("[%s] Received Info [%s]", Name(), *info_id);
#endif

	return true;
}
#ifdef DEBUG
void CInventoryOwner::DumpInfo() const
{
	KNOWN_INFO_VECTOR& known_info = m_known_info_registry->registry().objects();

	Msg("------------------------------------------");	
	Msg("Start KnownInfo dump for [%s]",Name());	
	KNOWN_INFO_VECTOR_IT it = known_info.begin();
	for(int i=0;it!=known_info.end();++it,++i){
		Msg("known info[%d]:%s",i,(*it).c_str());	
	}
	Msg("------------------------------------------");	

}
#endif

void CInventoryOwner::OnDisableInfo(shared_str info_id) const
{
	VERIFY(info_id.size());
	//удалить запись из реестра

#ifdef DEBUG
	if(psAI_Flags.test(aiInfoPortion))
		Msg("[%s] Disabled Info [%s]", Name(), info_id.c_str());
#endif

	KNOWN_INFO_VECTOR& known_info = m_known_info_registry->registry().objects();

	KNOWN_INFO_VECTOR_IT it = std::find_if(known_info.begin(), known_info.end(), CFindByIDPred(info_id));
	if (known_info.end() == it) return;
	known_info.erase(it);
}

void CInventoryOwner::TransferInfo(shared_str info_id, bool add_info) const
{
	VERIFY(info_id.size());
	const CObject* pThisObject = smart_cast<const CObject*>(this);
	VERIFY(pThisObject);

	//отправляем от нашему PDA пакет информации с номером
	NET_Packet P;
	CGameObject::u_EventGen(P, GE_INFO_TRANSFER, pThisObject->ID());
	P.w_u16(pThisObject->ID()); //отправитель
	P.w_stringZ(info_id); //сообщение
	P.w_u8(add_info ? 1 : 0); //добавить/удалить информацию

	// MP fork (§14 step 8 phase 3 Q4): CGameObject::u_EventSend goes through CLevel::Send, and
	// on a machine that is the server that branch reads `Game().local_svdpnid` — Game() being
	// `*Level().game`, the CLIENT-side game state, which a DEDICATED server does not have. So
	// this line kills the headless server, and it had never fired there before: the flag layer
	// (step 8 phases 1-2) writes info portions straight into the registry via alife():give_info,
	// which raises no event at all. It took the first dialogue phrase carrying a <give_info> to
	// reach it — measured 2026-07-28, the server log ending mid-write with the next line never
	// printed.
	//
	// The server does not need to send itself an event: it is the authority, and the local
	// application happens below. What the CLIENTS need is the news, so broadcast it directly
	// through the server's own game state (the same call every other server-originated event in
	// this fork uses). A thin client applying it is safe — its registry wrapper falls back to a
	// local map when there is no A-Life.
	const bool coop_server = xr_enet::enabled() && !!ai().get_alife();
	if (coop_server && Level().Server && Level().Server->game)
		Level().Server->game->u_EventSend(P);
	else
		CGameObject::u_EventSend(P);

	// Bracketing, deliberately: the phrase layer logs "info write -> subject N" before calling
	// this, so a server that dies between the two lines died in the SEND, and one that dies after
	// this line died in the portion load or the registry write. The first version of this bug was
	// diagnosed from a log that simply stopped, which cost a whole test cycle to narrow down.
	if (coop_server)
		Msg("- COOP(dialog): info '%s' %s for %u — event away, storing",
			info_id.c_str(), add_info ? "given" : "disabled", pThisObject->ID());

	CInfoPortion info_portion;
	info_portion.Load(info_id);
	{
		if (add_info)
			OnReceiveInfo(info_id);
		else
			OnDisableInfo(info_id);
	}
}

bool CInventoryOwner::HasInfo(shared_str info_id) const
{
	VERIFY(info_id.size());
	const KNOWN_INFO_VECTOR* known_info = m_known_info_registry->registry().objects_ptr();
	if (!known_info) return false;

	if (std::find_if(known_info->begin(), known_info->end(), CFindByIDPred(info_id)) == known_info->end())
		return false;

	return true;
}

/*
bool CInventoryOwner::GetInfo	(shared_str info_id, INFO_DATA& info_data) const
{
	VERIFY( info_id.size() );
	const KNOWN_INFO_VECTOR* known_info = m_known_info_registry->registry().objects_ptr ();
	if(!known_info) return false;

	KNOWN_INFO_VECTOR::const_iterator it = std::find_if(known_info->begin(), known_info->end(), CFindByIDPred(info_id));
	if(known_info->end() == it)
		return false;

	info_data = *it;
	return true;
}
*/
