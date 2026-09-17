///////////////////////////////////////////////////////////////
// AI_PhraseDialogManager.cpp
// Класс, от которого наследуются NPC персонажи, ведущие диалог
// с актером
//
///////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "AI_PhraseDialogManager.h"
#include "PhraseDialog.h"
#include "inventoryowner.h"
#include "character_info.h"
#include "gameobject.h"
#include "relation_registry.h"

CAI_PhraseDialogManager::CAI_PhraseDialogManager(void)
{
	m_sStartDialog = m_sDefaultStartDialog = NULL;
}

CAI_PhraseDialogManager::~CAI_PhraseDialogManager(void)
{
}

//PhraseDialogManager
void CAI_PhraseDialogManager::ReceivePhrase(DIALOG_SHARED_PTR& phrase_dialog)
{
	AnswerPhrase(phrase_dialog);
	CPhraseDialogManager::ReceivePhrase(phrase_dialog);
}

#include "uigamesp.h"
#include "level.h"
#include "ui/UItalkWnd.h"

void CAI_PhraseDialogManager::AnswerPhrase(DIALOG_SHARED_PTR& phrase_dialog)
{
	CInventoryOwner* pInvOwner = smart_cast<CInventoryOwner*>(this);
	THROW(pInvOwner);
	CGameObject* pOthersGO = smart_cast<CGameObject*>(phrase_dialog->OurPartner(this));
	THROW(pOthersGO);
	CInventoryOwner* pOthersIO = smart_cast<CInventoryOwner*>(pOthersGO);
	THROW(pOthersIO);

	if (!phrase_dialog->IsFinished())
	{
		CHARACTER_GOODWILL attitude = RELATION_REGISTRY().GetAttitude(pOthersIO, pInvOwner);

		xr_vector<int> phrases;
		CHARACTER_GOODWILL phrase_goodwill = NO_GOODWILL;
		//если не найдем более подходяещей выводим фразу
		//последнюю из списка (самую грубую)
		int phrase_num = phrase_dialog->PhraseList().size() - 1;
		for (u32 i = 0; i < phrase_dialog->PhraseList().size(); ++i)
		{
			phrase_goodwill = phrase_dialog->PhraseList()[phrase_num]->GoodwillLevel();
			if (attitude >= phrase_goodwill)
			{
				phrase_num = i;
				break;
			}
		}

		for (u32 i = 0; i < phrase_dialog->PhraseList().size(); i++)
		{
			if (phrase_goodwill == phrase_dialog->PhraseList()[phrase_num]->GoodwillLevel())
				phrases.push_back(i);
		}

		phrase_num = phrases[Random.randI(0, phrases.size())];

		shared_str phrase_id = phrase_dialog->PhraseList()[phrase_num]->GetID();

		CUIGameSP* pGameSP = smart_cast<CUIGameSP*>(CurrentGameUI());
		pGameSP->TalkMenu->AddAnswer(phrase_dialog->GetPhraseText(phrase_id), pInvOwner->Name());

		CPhraseDialogManager::SayPhrase(phrase_dialog, phrase_id);
	}
}


void CAI_PhraseDialogManager::SetStartDialog(shared_str phrase_dialog)
{
	m_sStartDialog = phrase_dialog;
}

void CAI_PhraseDialogManager::SetDefaultStartDialog(shared_str phrase_dialog)
{
	m_sDefaultStartDialog = phrase_dialog;
}

void CAI_PhraseDialogManager::RestoreDefaultStartDialog()
{
	m_sStartDialog = m_sDefaultStartDialog;
}


void CAI_PhraseDialogManager::UpdateAvailableDialogs(CPhraseDialogManager* partner)
{
	m_AvailableDialogs.clear();
	m_CheckedDialogs.clear();

	// MP fork (2026-09-17, Caden's live crash on 5c0566c3): test the CONTENT, not the pointer. `*m_sStartDialog` is the string's
	// pointer, which is non-null for an EMPTY start dialog, so an empty id reached CPhraseDialog::Load("") and
	// CXML_IdToIndex<CPhraseDialog>::GetById aborted with an empty Arguments line — the client died when a conversation opened.
	// A script sets an empty start dialog whenever a meet condlist picks nothing (xr_meet rejects only nil and "nil"), which co-op
	// makes likely because those condlists are evaluated against db.actor = object 0 on a thin client.
	if (m_sStartDialog.size())
		inherited::AddAvailableDialog(*m_sStartDialog, partner);
	inherited::AddAvailableDialog("hello_dialog", partner);

	inherited::UpdateAvailableDialogs(partner);
}
