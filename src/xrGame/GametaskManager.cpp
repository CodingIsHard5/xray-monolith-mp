#include "pch_script.h"
// MP fork (§19 co-op): shared quest replication
#include "../xrNetServer/xr_enet_transport.h"
#include "xrServer.h"
#include "xrMessages.h"
#include "GameTaskManager.h"
#include "alife_registry_wrappers.h"
#include "ui/xrUIXmlParser.h"
#include "GameTask.h"
#include "Level.h"
#include "map_manager.h"
#include "map_location.h"
#include "actor.h"
#include "UIGameSP.h"
#include "ui/UIPDAWnd.h"
#include "encyclopedia_article.h"
#include "ui/UIMapWnd.h"
#include "..\..\xrEngine\x_ray.h"
#include "string_table.h"

#pragma warning(push)
#pragma warning(disable:4995)
#include <malloc.h>
#pragma warning(pop)

shared_str g_active_task_id;

struct FindTaskByID
{
	shared_str id;
	bool b_only_inprocess;

	FindTaskByID(const shared_str& s, bool search_only_inprocess): id(s), b_only_inprocess(search_only_inprocess)
	{
	}

	bool operator ()(const SGameTaskKey& key)
	{
		if (b_only_inprocess)
			return (id == key.task_id && key.game_task->GetTaskState() == eTaskStateInProgress);
		else
			return (id == key.task_id);
	}
};

bool task_prio_pred(const SGameTaskKey& k1, const SGameTaskKey& k2)
{
	return k1.game_task->m_priority > k2.game_task->m_priority;
}

CGameTaskManager::CGameTaskManager()
{
	m_gametasks_wrapper = xr_new<CGameTaskWrapper>();
	m_gametasks_wrapper->registry().init(0); // actor's id
	m_flags.zero();
	m_flags.set(eChanged, TRUE);
	m_gametasks = NULL;

	if (g_active_task_id.size())
	{
		CGameTask* t = HasGameTask(g_active_task_id, true);
		if (t)
		{
			SetActiveTask(t);
		}
	}
}

CGameTaskManager::~CGameTaskManager()
{
	delete_data(m_gametasks_wrapper);
	g_active_task_id = NULL;
}

vGameTasks& CGameTaskManager::GetGameTasks()
{
	if (!m_gametasks)
	{
		m_gametasks = &m_gametasks_wrapper->registry().objects();
#ifdef DEBUG
		Msg("m_gametasks size=%d",m_gametasks->size());
#endif // #ifdef DEBUG
	}

	return *m_gametasks;
}

CGameTask* CGameTaskManager::HasGameTask(const shared_str& id, bool only_inprocess)
{
	FindTaskByID key(id, only_inprocess);
	vGameTasks_it it = std::find_if(GetGameTasks().begin(), GetGameTasks().end(), key);
	if (it != GetGameTasks().end())
		return (*it).game_task;

	return 0;
}

CGameTask* CGameTaskManager::GiveGameTaskToActor(CGameTask* t, u32 timeToComplete, bool bCheckExisting, u32 timer_ttl)
{
	t->CommitScriptHelperContents();
	if (/* bCheckExisting &&*/ HasGameTask(t->m_ID, true))
	{
		Msg("! task [%s] already inprocess", t->m_ID.c_str());
		VERIFY2(0, make_string( "give_task : Task [%s] already inprocess!", t->m_ID.c_str()));
		return NULL;
	}

	m_flags.set(eChanged, TRUE);

	GetGameTasks().push_back(SGameTaskKey(t->m_ID));
	GetGameTasks().back().game_task = t;
	t->m_ReceiveTime = Level().GetGameTime();
	t->m_TimeToComplete = t->m_ReceiveTime + timeToComplete * 1000; //ms
	t->m_timer_finish = t->m_ReceiveTime + timer_ttl * 1000; //ms

	std::stable_sort(GetGameTasks().begin(), GetGameTasks().end(), task_prio_pred);

	t->OnArrived();

	//CGameTask* active_task			= ActiveTask();

	//if ( (active_task == NULL) || (active_task->m_priority < t->m_priority) )
	//{
	//	SetActiveTask( t );
	//}

	SetActiveTask(t);

	//установить флажок необходимости прочтения тасков в PDA
	if (CurrentGameUI())
		CurrentGameUI()->UpdatePda();

	t->ChangeStateCallback();

	return t;
}

void CGameTaskManager::SetTaskState(CGameTask* t, ETaskState state)
{
	m_flags.set(eChanged, TRUE);

	t->SetTaskState(state);

	if (ActiveTask() == t)
	{
		//SetActiveTask	("");
		g_active_task_id = "";
	}

	if (CurrentGameUI())
		CurrentGameUI()->UpdatePda();
}

void CGameTaskManager::SetTaskState(const shared_str& id, ETaskState state)
{
	CGameTask* t = HasGameTask(id, true);
	if (NULL == t)
	{
		Msg("actor does not has task [%s] or it is completed", *id);
		return;
	}
	SetTaskState(t, state);
}

void CGameTaskManager::UpdateTasks()
{
	if (Device.Paused()) return;

	Level().MapManager().DisableAllPointers();

	u32 task_count = GetGameTasks().size();
	if (0 == task_count) return;

	{
		typedef buffer_vector<SGameTaskKey> Tasks;
		Tasks tasks(
			_alloca(task_count * sizeof(SGameTaskKey)),
			task_count,
			GetGameTasks().begin(),
			GetGameTasks().end()
		);

		Tasks::const_iterator I = tasks.begin();
		Tasks::const_iterator E = tasks.end();
		for (; I != E; ++I)
		{
			CGameTask* const t = (*I).game_task;
			if (t->GetTaskState() != eTaskStateInProgress)
				continue;

			ETaskState const state = t->UpdateState();

			if ((state == eTaskStateFail) || (state == eTaskStateCompleted))
				SetTaskState(t, state);
		}
	}

	CGameTask* t = ActiveTask();
	if (t)
	{
		CMapLocation* ml = t->LinkedMapLocation();
		if (ml && !ml->PointerEnabled())
		{
			ml->EnablePointer();
		}
	}

	if (m_flags.test(eChanged))
		UpdateActiveTask();
}

void CGameTaskManager::UpdateActiveTask()
{
	std::stable_sort(GetGameTasks().begin(), GetGameTasks().end(), task_prio_pred);

	CGameTask* t = ActiveTask();
	if (!t)
	{
		CGameTask* front = IterateGet(NULL, eTaskStateInProgress, true);
		if (front)
		{
			SetActiveTask(front);
		}
	}

	//Discord
	if (psDeviceFlags2.test(rsDiscord))
		RPC_UpdateTaskName();

	// MP fork (§19 co-op): tasks are given by dialogue actions, and those run on the SERVER so
	// their effects are real - which means the quest lands in the SERVER's list. A joining
	// player reads its OWN list, which is a private in-memory one (CALifeRegistryWrapper falls
	// back to a local registry when there is no A-Life simulator), so accepting a quest looked
	// like it did nothing at all. Push the list out whenever it changes.
	//
	// The host has ONE list, not one per player, so this makes quests SHARED: both players see
	// the same objectives and see them complete together. For co-op that is the wanted
	// behaviour, and it is also the only behaviour the existing storage can express.
	coop_broadcast_tasks();

	m_flags.set(eChanged, FALSE);
	m_actual_frame = Device.dwFrame;
}

// MP fork (§19 co-op): adopt the server's quest list. Clears ours and rebuilds from the wire,
// so a task the server dropped disappears here too rather than lingering.
void CGameTaskManager::coop_apply_tasks(NET_Packet& packet)
{
	if (ai().get_alife()) // servers own the list; only a thin client adopts one
		return;

	vGameTasks& tasks = GetGameTasks();
	delete_data(tasks);
	tasks.clear();

	const u16 count = packet.r_u16();
	for (u16 i = 0; i < count; ++i)
	{
		shared_str id;
		packet.r_stringZ(id);

		// Mirror of the writer: pull the blob out and hand load_task a reader over it. Reading
		// a fixed number of bytes also means a task whose format ever drifts cannot
		// desynchronise every task after it.
		const u16 size = packet.r_u16();
		xr_vector<u8> blob;
		blob.resize(size);
		if (size)
			packet.r(&blob[0], size);

		CGameTask* const task = xr_new<CGameTask>();
		task->m_ID = id;
		if (size)
		{
			IReader reader(&blob[0], int(size));
			task->load_task(reader);
		}

		SGameTaskKey key;
		key.task_id = id;
		key.game_task = task;
		tasks.push_back(key);
	}

	m_flags.set(eChanged, TRUE); // makes the PDA redraw
}

// MP fork (§19 co-op): serialise the task list to every client. Reuses each task's own
// save/load - the same pair savegames use - so objectives, timers, titles and map hints all
// come along without inventing a wire format for them.
void CGameTaskManager::coop_broadcast_tasks()
{
	if (!xr_enet::enabled() || !ai().get_alife() || !Level().Server)
		return;

	vGameTasks& tasks = GetGameTasks();

	NET_Packet packet;
	packet.w_begin(M_XRNET_TASKS);
	packet.w_u16(u16(tasks.size()));

	for (u32 i = 0; i < tasks.size(); ++i)
	{
		CGameTask* const task = tasks[i].game_task;
		if (!task)
			continue;

		// save_task writes to an IWriter (the savegame path), not to a packet, so serialise
		// into memory and carry the bytes. Length-prefixed, so a client that cannot make sense
		// of one task can skip it rather than losing the rest of the list.
		CMemoryWriter blob;
		task->save_task(blob);

		packet.w_stringZ(task->m_ID);
		packet.w_u16(u16(blob.size()));
		if (blob.size())
			packet.w(blob.pointer(), blob.size());
	}

	Level().Server->SendBroadcast(BroadcastCID, packet, net_flags(TRUE, TRUE));
}

void CGameTaskManager::RPC_UpdateTaskName()
{
	CGameTask* tr = ActiveTask();
	if (tr)
		snprintf(discord_gameinfo.task_name, 128, xr_ToUTF8(*CStringTable().translate(tr->m_Title)));
}

CGameTask* CGameTaskManager::ActiveTask()
{
	const shared_str& t_id = g_active_task_id;
	if (!t_id.size()) return NULL;
	return HasGameTask(t_id, true);
}

/*
void CGameTaskManager::SetActiveTask(const shared_str& id)
{
	g_active_task_id			= id;
	m_flags.set					(eChanged, TRUE);
	m_read						= true;
}*/

void CGameTaskManager::SetActiveTask(CGameTask* task)
{
	VERIFY(task);
	if (task)
	{
		g_active_task_id = task->m_ID;
		m_flags.set(eChanged, TRUE);
		task->m_read = true;
	}
}

CUIMapWnd* GetMapWnd();

void CGameTaskManager::MapLocationRelcase(CMapLocation* ml)
{
	CUIMapWnd* mwnd = GetMapWnd();
	if (mwnd)
		mwnd->MapLocationRelcase(ml);

	CGameTask* gt = HasGameTask(ml, false);
	if (gt)
		gt->RemoveMapLocations(true);
}

CGameTask* CGameTaskManager::HasGameTask(const CMapLocation* ml, bool only_inprocess)
{
	vGameTasks_it it = GetGameTasks().begin();
	vGameTasks_it it_e = GetGameTasks().end();

	for (; it != it_e; ++it)
	{
		CGameTask* gt = (*it).game_task;
		if (gt->LinkedMapLocation() == ml)
		{
			if (only_inprocess && gt->GetTaskState() != eTaskStateInProgress)
				continue;

			return gt;
		}
	}
	return NULL;
}

CGameTask* CGameTaskManager::IterateGet(CGameTask* t, ETaskState state, bool bForward)
{
	vGameTasks& v = GetGameTasks();
	u32 cnt = v.size();
	for (u32 i = 0; i < cnt; ++i)
	{
		CGameTask* gt = v[i].game_task;
		if (gt == t || NULL == t)
		{
			bool allow;
			if (bForward)
			{
				if (t) ++i;
				allow = i < cnt;
			}
			else
			{
				allow = (i > 0) && (--i >= 0);
			}
			if (allow)
			{
				CGameTask* found = v[i].game_task;
				if (found->GetTaskState() == state)
					return found;
				else
					return IterateGet(found, state, bForward);
			}
			else
				return NULL;
		}
	}
	return NULL;
}

u32 CGameTaskManager::GetTaskIndex(CGameTask* t, ETaskState state)
{
	if (!t)
	{
		return 0;
	}

	vGameTasks& v = GetGameTasks();
	u32 cnt = v.size();
	u32 res = 0;
	for (u32 i = 0; i < cnt; ++i)
	{
		CGameTask* gt = v[i].game_task;
		if (gt->GetTaskState() == state)
		{
			++res;
			if (gt == t)
			{
				return res;
			}
		}
	}
	return 0;
}

u32 CGameTaskManager::GetTaskCount(ETaskState state)
{
	vGameTasks& v = GetGameTasks();
	u32 cnt = v.size();
	u32 res = 0;
	for (u32 i = 0; i < cnt; ++i)
	{
		CGameTask* gt = v[i].game_task;
		if (gt->GetTaskState() == state)
		{
			++res;
		}
	}
	return res;
}

char* sTaskStates[] =
{
	"eTaskStateFail",
	"TaskStateInProgress",
	"TaskStateCompleted",
	"TaskStateDummy"
};

void CGameTaskManager::DumpTasks()
{
	vGameTasks_it it = GetGameTasks().begin();
	vGameTasks_it it_e = GetGameTasks().end();
	for (; it != it_e; ++it)
	{
		const CGameTask* gt = (*it).game_task;
		Msg(" ID=[%s] state=[%s] prio=[%d] ",
		    gt->m_ID.c_str(),
		    sTaskStates[gt->GetTaskState()],
		    gt->m_priority);
	}
}
