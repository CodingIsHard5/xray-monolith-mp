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
	coop_claim_no_actor,      // Q4: nobody is acting — a personal errand cannot be owned by nobody
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
	coop_cond_none    = 0,
	coop_cond_kill    = 1,   // §7.3 kill/clear: EVERY bound entity must die. Completes when none left.
	coop_cond_defend  = 2,   // §7.3 defend: a bound entity must still be alive at a game-time deadline.
	// §7.3 fetch/turn-in (Q4). The one shape whose completion is NOT a world-state change the
	// server already observes: nothing happens in the world when a player is carrying the right
	// items, and nothing happens when they walk up to the right NPC. The completion is the ACT of
	// handing them over, which only exists inside a dialogue — which is why the doc files it under
	// the DIALOGUE-ROUTED category it calls HARD rather than beside kill/clear/defend.
	coop_cond_deliver = 3,
};

// Attach a completion condition to a task. Replaces any condition already on it.
//  kill   — `targets` all have to die; `deadline_game_ms` is ignored.
//  defend — `targets` is the single entity to keep alive; the verdict is taken at
//           `deadline_game_ms` (absolute Level().GetGameTime() ms). It FAILS the moment the entity
//           dies before then, which is the first outcome in this layer that is not a completion.
void coop_task_set_condition(const shared_str& task_id, u8 kind, const xr_vector<u16>& targets,
                             u64 deadline_game_ms);

// Arm a DELIVER condition (§7.3's fetch/turn-in shape). `count` of `section` must be handed to
// `recipient` (mp_coop_owner::none = whichever NPC the player is actually talking to). Replaces
// any condition already on the task, exactly like coop_task_set_condition.
//
// The section and the count are the whole condition: WHICH items is deliberately not tracked, so a
// player who drops the quest medkits and picks up three others still turns the task in. Tracking
// identity here would make a fetch task a set of entity ids, and the doc's conservation argument
// (§7.2, "same conservation logic as loot") is about the ITEMS being finite, not about these ones.
void coop_task_set_deliver(const shared_str& task_id, const shared_str& section, u16 count,
                           u16 recipient);

// §7.4's authored classification, set explicitly rather than inferred from whoever offered the
// task. coop_task_offer captures the offerer's community, which is right when an NPC hands out its
// own faction's work and wrong the moment content wants to say otherwise.
void coop_task_set_community(const shared_str& task_id, u16 community);

// How many bound entities are still outstanding (kill: not yet dead; defend: still to be kept
// alive; deliver: items still owed), or 0 if the task carries no condition — INCLUDING the case
// where it carried one that has already been spent. Those two are deliberately indistinguishable
// through this accessor, because
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

// MP fork (§14 step 8 phase 3 Q4 / doc §7.3): DIALOGUE-ROUTED acquisition and turn-in.
//
// §7.3 splits tasks into the category whose completion the server can already see (kill / clear /
// defend — Q2 and Q3) and the category the doc calls HARD: "anything acquired or turned in through
// dialogue", which it says "requires the actor rework (Section 6) to be solid" and calls the
// launch-critical foundational work. The reason it is hard is not the transaction, it is the
// SUBJECT: a dialogue action runs on the server, on behalf of one player, in a process where
// db.actor is nil and Actor() means "whichever player actor spawned last". Phases 1 and 2 built
// the answer (mp_coop_owner::acting_actor()); Q4 is the first consumer that would be silently
// WRONG without it rather than merely dead.
enum coop_turnin_result
{
	coop_turnin_ok = 0,
	coop_turnin_no_actor,      // no acting player — a turn-in is an act by a person
	coop_turnin_no_task,       // not in progress for anyone (never claimed, or already finished)
	coop_turnin_no_condition,  // nothing to turn in: no condition, or one already spent
	coop_turnin_not_owner,     // §7.4: somebody else's errand, or not a member of the owning faction
	coop_turnin_wrong_npc,     // the goods are owed to a different recipient
	coop_turnin_short,         // the player does not have them — and NOTHING is taken
	coop_turnin_failed,        // the hand-over itself could not be performed; nothing is taken
};

// Turn `task_id` in: hand the owed items to `npc_id` and take the verdict. ALL-OR-NOTHING — every
// check runs before a single item moves, because a refusal that has already eaten two of the three
// medkits is worse than no turn-in at all, and it is the one failure in this layer that destroys a
// player's property rather than merely mis-reporting a task.
coop_turnin_result coop_task_turn_in(const shared_str& task_id, u16 player_id, u16 npc_id);

// §7.4's turn-in authority. Deliberately NOT coop_task_goes_to (which decides who SEES a task):
// visibility and authority are different questions, and a squadmate who can see your errand on
// their PDA must not be able to finish it for you.
bool coop_task_may_turn_in(const shared_str& task_id, u16 player_id);

// The DIALOGUE-ROUTED entry points: the subject is mp_coop_owner::acting_actor(), i.e. the player
// the server is currently executing script on behalf of. These are what gamedata calls; the
// player_id forms above are what the engine and the harness call.
//
// Both REFUSE when there is no acting player, and that refusal is the point rather than a guard:
// the server reads flags and runs script autonomously all the time (phase 2 measured one stock
// smart terrain doing it 46,318 times in five minutes), and a claim taken in that context would
// hand a personal errand to nobody, or to whoever happened to be resolvable.
coop_claim_result  coop_task_claim_acting(const shared_str& task_id);
coop_turnin_result coop_task_turn_in_acting(const shared_str& task_id, u16 npc_id);

// Is this task still on the shelf? Answers from the SERVER's pool on the server, and from the last
// pool the client received on a client — which is what makes it usable as a dialogue PRECONDITION,
// since preconditions stay client-local (they build the phrase list synchronously, before anything
// is sent). The client's copy can be stale by design; the server refusing a claim is what actually
// enforces §7.2, and the harness measures exactly that case.
bool coop_task_offered(const shared_str& task_id);

// Item plumbing, implemented in game_sv_single.cpp where the server's CSE tree and IPureServer
// live. Both read and write the SERVER's own record of what a player is carrying — a turn-in that
// believed the client's claim about its inventory would be a duplication exploit with a dialogue
// box in front of it.
u32 coop_items_count(u16 owner_id, const shared_str& section);
u32 coop_items_hand_over(u16 from_id, u16 to_id, const shared_str& section, u16 count);

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
