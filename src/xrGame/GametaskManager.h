#pragma once

#include "GameTaskDefs.h"
#include "object_interfaces.h"

class CGameTaskWrapper;
class CGameTask;
class CMapLocation;

class CGameTaskManager
{
	CGameTaskWrapper* m_gametasks_wrapper;
	vGameTasks* m_gametasks;

	enum { eChanged = (1 << 0), };

	Flags8 m_flags;
	u32 m_actual_frame;
protected:
	void UpdateActiveTask();
public:

	CGameTaskManager();
	~CGameTaskManager();

	vGameTasks& GetGameTasks();
	CGameTask* HasGameTask(const CMapLocation* ml, bool only_inprocess);
	CGameTask* HasGameTask(const shared_str& id, bool only_inprocess);
	CGameTask* GiveGameTaskToActor(CGameTask* t, u32 timeToComplete, bool bCheckExisting, u32 timer_ttl);
	void SetTaskState(const shared_str& id, ETaskState state);
	void SetTaskState(CGameTask* t, ETaskState state);

	void RPC_UpdateTaskName();

	void __stdcall UpdateTasks();

	CGameTask* ActiveTask();
	//	void					SetActiveTask					(const shared_str& id);
	void SetActiveTask(CGameTask* task);
	u32 ActualFrame() const { return m_actual_frame; }

	CGameTask* IterateGet(CGameTask* t, ETaskState state, bool bForward);
	u32 GetTaskIndex(CGameTask* t, ETaskState state);
	u32 GetTaskCount(ETaskState state);
	void MapLocationRelcase(CMapLocation* ml);

	// MP fork (§19 co-op): push each player its own quest list (server) / adopt it (client).
	void coop_broadcast_tasks();
	void coop_apply_tasks(NET_Packet& packet);

	void ResetStorage() { m_gametasks = NULL; };
	void DumpTasks();
};


// MP fork (§19 co-op): quest ownership / faction / squad management (server-side). See
// GametaskManager.cpp. Console commands and dialogue drive these.
void coop_squad_create(u32 squad_id);
void coop_squad_add_player(u32 squad_id, u16 player_id);
void coop_squad_add_npc(u32 squad_id, u16 npc_id);
void coop_squad_set_sharing(u32 squad_id, bool on);
void coop_mark_faction_task(const shared_str& task_id, bool on);
void coop_dump_squads();

// MP fork (§14 step 8 phase 3 Q1 / doc §7.2): tasks as a FINITE SHARED RESOURCE.
//
// §7.2's whole argument is that per-player copies fight a shared world — if everyone gets their
// own "kill the mutants" and one player kills them, the mutants are dead in the ONE world, so
// every other copy is either a free reward or uncompletable. So a task is world state: one task
// object, one claimant, completion maps to a world-state change exactly once.
//
// That needs a thing the engine did not have: an OFFER that exists before anyone owns it. A task
// only came into being when GiveGameTaskToActor was called, so "offered but unclaimed" had no
// representation and "first claim wins" had nothing to win.
enum coop_claim_result
{
	coop_claim_ok = 0,        // claimed; the offer has left the pool
	coop_claim_taken,         // somebody else got there first
	coop_claim_absent,        // never offered, or already gone
};

// Add an offer to the pool. `faction` is §7.4's authored classification — a faction quest is
// taken on behalf of the community and ends up owned by the WORLD tier, member-agnostic; a
// personal errand ends up owned by the claimant. `world_state` is §7.3's category (kill / fetch /
// clear / defend complete on a world-state change the server already tracks — do these first).
// `target_id` is the entity whose DEATH is that world-state change (u16(-1) = none): §7.3's
// "kill" shape, the half the server can already observe without new bookkeeping. The binding
// outlives the claim — it is what completion is looked up by — so it is kept apart from the
// pool entry, which is destroyed by the claim.
// Idempotent: re-offering an id already in the pool changes nothing.
void coop_task_offer(const shared_str& task_id, u16 offer_id, bool faction, bool world_state,
                     u16 target_id = u16(-1));

// First claim wins. On success the offer LEAVES the pool — §7.2's "others see it's gone" is a
// removal, not a lock — and the task is given inside an acting_scope for `player_id` so the
// existing ownership tagging routes it.
coop_claim_result coop_task_claim(const shared_str& task_id, u16 player_id);

// The owning entity id recorded for a claimed task: the claimant for a personal errand, or
// mp_coop_owner::world_key for a faction quest. mp_coop_owner::none if the task is unknown.
u16  coop_task_owner_of(const shared_str& task_id);
u32  coop_task_pool_size();     // unclaimed offers
void coop_dump_task_pool();

// MP fork (§14 step 8 phase 3 Q2+Q3 / doc §7.3): completion driven by a world-state change.
//
// Q2 did the shape that needs no bookkeeping at all: ONE entity, and its death IS the completion,
// so the whole condition fits in a u16 and the hook is the algorithm. §7.3's other listed shapes
// (clear, defend) do not fit that, because the answer depends on state accumulated ACROSS events —
// how many of the group are still standing, whether the escort is still alive when the clock runs
// out. Those need a record that survives between events, and Q3 is that record.
//
// The doc's own example is the plural one ("kill the mutants"), so Q2's single target is really the
// n=1 case of a kill condition, and it is stored as exactly that rather than kept as a parallel
// mechanism — one path to get wrong instead of two.
enum coop_cond_kind
{
	coop_cond_none   = 0,
	coop_cond_kill   = 1,   // §7.3 kill/clear: EVERY bound entity must die. Completes when none left.
	coop_cond_defend = 2,   // §7.3 defend: a bound entity must still be alive at a game-time deadline.
};

// Attach a completion condition to a task. Replaces any condition already on it.
//  kill   — `targets` all have to die; `deadline_game_ms` is ignored.
//  defend — `targets` is the single entity to keep alive; the verdict is taken at
//           `deadline_game_ms` (absolute Level().GetGameTime() ms). It FAILS the moment the entity
//           dies before then, which is the first outcome in this layer that is not a completion.
void coop_task_set_condition(const shared_str& task_id, u8 kind, const xr_vector<u16>& targets,
                             u64 deadline_game_ms);

// How many bound entities are still outstanding (kill: not yet dead; defend: still to be kept
// alive), or 0 if the task carries no condition — INCLUDING the case where it carried one that has
// already been spent. Those two are deliberately indistinguishable through this accessor, because
// a caller that could tell them apart would be tempted to use "0" as evidence of completion. It is
// not: a task that never had a condition also reads 0. Completion is evidenced by the COND log
// line emitted AT the transition, never by reading this afterwards.
u32  coop_task_cond_remaining(const shared_str& task_id);

// The first outstanding entity of a kill condition (Q2's accessor, kept working on the Q3 record so
// the Q2 harness stays a live regression test). mp_coop_owner::none when there is none.
u16  coop_task_target_of(const shared_str& task_id);

// Called from game_sv_Single::on_death — the server-authoritative death hook — for every entity
// that dies. Advances every condition `dead_id` appears in and returns how many tasks reached a
// TERMINAL verdict (completed or failed) as a result.
//
// It never consults `killer_id` except to log it, and that is §7.4's member-agnostic completion
// rather than an omission: a faction quest is owned by mp_coop_owner::world_key, which is not an
// entity and can therefore never be the killer, so a rule that credited only the owner would make
// every faction quest uncompletable. The doc is explicit that "a faction-level fact correctly does
// not care which member".
u32  coop_task_on_world_death(u16 dead_id, u16 killer_id);

// Called on a timer from game_sv_Single::Update(). Takes the verdict on every defend condition
// whose deadline has passed. Returns how many tasks reached a terminal verdict.
//
// The deadline is in GAME time, not wall time, which is what makes it survive a restart: the game
// clock rides the .scop through time_manager(). A wall-clock deadline would silently restart with
// the process and hand every defend task a fresh timer for free.
//
// u64 and not u32 because ALife::_TIME_ID is u64 and holds an ABSOLUTE date in ms — a u32 wraps
// after 49.7 days of it, which on a game clock running at x6 is under nine real days of server
// uptime, and the failure would be a deadline that silently moves into the past.
u32  coop_task_tick_conditions(u64 now_game_ms);

// MP fork (§14 step 8 phase 3 Q2 / doc §7.2): the co-op quest state rides the .scop.
//
// Q1 left the pool, the ownership tags and the faction/community tags as file-static maps, so a
// restart forgot who owned what and put every claimed offer back on the shelf — which is exactly
// the state §7.2 says a shared world must not have, since two players could each claim "the same"
// task across a reboot while the mutants only die once.
//
// These go into the .scop and NOT into the <save>.coop sidecar, deliberately: what is stored here
// ANNOTATES the task list, and the task list is CGameTaskRegistry riding registry().save into the
// .scop. Split across two files, a crash between the two writes leaves tasks whose owner is
// unknown — a corruption with no repair. In one file it is atomic by construction.
void coop_task_state_save(IWriter& stream);
void coop_task_state_load(IReader& stream);
