#include "pch_script.h"
// MP fork (§19 co-op): shared quest replication
#include "../xrNetServer/xr_enet_transport.h"
#include "xrServer.h"
#include "xrMessages.h"
#include "GameObject.h"
#include "../../xrServerEntities/character_info.h"
#include "character_community.h"
#include "InventoryOwner.h"
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
#include "mp_coop_owner.h"                          // MP fork (§14 step 8 P1): acting_actor()
#include "alife_space.h"                            // MP fork (§14 step 8 P3 Q2): COOP_QUEST_CHUNK_DATA

#pragma warning(push)
#pragma warning(disable:4995)
#include <malloc.h>

// ============================================================================
// MP fork (§19 co-op): quest ownership, factions and squads. FIRST PASS.
//
// Design (Caden, 2026-07-22): quests are PER-PLAYER by default; some quests are
// faction-wide (any player of that faction sees them); players can form SQUADS
// (of players and NPCs) with a per-squad quest-SHARING toggle. This is the
// foundation - ownership tagging plus per-client routing - so the pieces above
// it (faction flagging from the netcode-plan doc, squad UI) plug in cleanly.
//
// The server keeps ONE task list (game_sv_single), so this cannot yet hold two
// independent copies of the SAME task id for two players - a known limitation of
// the single-manager design, documented for the proper per-player-manager
// rework. What it DOES give: each task is tagged with an owner, and each client
// receives only the tasks it should. Unowned tasks (owner 0) go to everyone, so
// nothing regresses below the previous shared behaviour.
// ============================================================================

// The "player currently running a dialogue action" context this file used to own
// (g_coop_dialog_actor) is now mp_coop_owner::acting_actor() — doc §6.1's "check the
// interacting player", generalised in §14 step 8 phase 1 so the flag layer can ask the
// same question. Task ownership reads it below; nothing else changed.

namespace
{
	xr_map<shared_str, u16>          s_task_owner;      // task id  -> owning player actor id
	xr_set<shared_str>               s_faction_task;    // task ids that are faction-wide

	xr_map<u16, u32>                 s_player_squad;    // player id -> squad id
	xr_map<u32, xr_vector<u16> >     s_squad_players;   // squad id  -> player ids
	xr_map<u32, xr_vector<u16> >     s_squad_npcs;      // squad id  -> npc ids
	xr_map<u32, bool>                s_squad_share;     // squad id  -> quest sharing on?

	// MP fork (§14 step 8 phase 3 Q1 / doc §7.2): the OFFER POOL — tasks that exist but are not
	// yet anybody's. This is the representation the engine was missing: before it, a task came
	// into existence at GiveGameTaskToActor and there was no such state as "offered".
	struct coop_offer
	{
		u16  offer_id;      // the NPC/trader offering it; 0 = ambient
		u16  community;     // the offerer's community, captured AT OFFER TIME — see below
		bool faction;       // §7.4: taken on behalf of the faction (world tier) vs a personal errand
		bool world_state;   // §7.3: completes on a world-state change the server already tracks

		coop_offer(): offer_id(0), community(u16(-1)), faction(false), world_state(false) {}
	};
	xr_map<shared_str, coop_offer>   s_task_pool;       // UNCLAIMED offers only; a claim removes
	xr_map<shared_str, u16>          s_task_community;  // faction task id -> community it belongs to

	// MP fork (§14 step 8 phase 3 Q2+Q3 / doc §7.3): task id -> its completion condition.
	//
	// Kept OUT of coop_offer on purpose: the pool entry dies with the claim, and this has to
	// outlive it — it is what an event is looked up by. The whole record is erased when the
	// condition reaches a terminal verdict, which is how "exactly once" is enforced with no
	// second flag to keep in step.
	//
	// Q2 stored a bare u16 here, because its one shape (one entity, its death is the completion)
	// needs no state between events. Q3's shapes do: "clear" has to know how many of the group are
	// still standing, "defend" has to know whether the deadline has passed yet. Q2's target is now
	// simply a kill condition of size 1 rather than a second mechanism kept alongside.
	struct coop_condition
	{
		u8             kind;             // coop_cond_*
		xr_vector<u16> remaining;        // kill: not yet dead. defend: the one to keep alive.
		u64            deadline;         // defend: absolute game-time ms of the verdict. 0 = none.
		u32            evaluations;      // how many events this condition has been advanced by

		coop_condition(): kind(u8(coop_cond_none)), deadline(0), evaluations(0) {}
	};
	xr_map<shared_str, coop_condition> s_task_cond;

	u16 coop_community_of(u16 actor_id)
	{
		CObject* const o = Level().Objects.net_Find(actor_id);
		CInventoryOwner* const io = o ? smart_cast<CInventoryOwner*>(o) : NULL;
		return io ? u16(io->CharacterInfo().Community().index()) : u16(-1);
	}
}

// --- squad / sharing management (called from console commands) -------------
void coop_squad_create(u32 squad_id)
{
	if (s_squad_players.find(squad_id) == s_squad_players.end())
	{
		s_squad_players[squad_id];
		s_squad_npcs[squad_id];
		s_squad_share[squad_id] = false;
	}
}
void coop_squad_add_player(u32 squad_id, u16 player_id)
{
	coop_squad_create(squad_id);
	s_player_squad[player_id] = squad_id;
	xr_vector<u16>& v = s_squad_players[squad_id];
	if (std::find(v.begin(), v.end(), player_id) == v.end()) v.push_back(player_id);
}
void coop_squad_add_npc(u32 squad_id, u16 npc_id)
{
	coop_squad_create(squad_id);
	xr_vector<u16>& v = s_squad_npcs[squad_id];
	if (std::find(v.begin(), v.end(), npc_id) == v.end()) v.push_back(npc_id);
}
void coop_squad_set_sharing(u32 squad_id, bool on) { coop_squad_create(squad_id); s_squad_share[squad_id] = on; }
void coop_mark_faction_task(const shared_str& task_id, bool on)
{
	if (on) s_faction_task.insert(task_id); else s_faction_task.erase(task_id);
}
void coop_dump_squads()
{
	Msg("- coop squads: %u squad(s)", s_squad_players.size());
	for (xr_map<u32, xr_vector<u16> >::iterator it = s_squad_players.begin(); it != s_squad_players.end(); ++it)
	{
		Msg("    squad %u  sharing=%d  players=%u  npcs=%u", it->first,
			s_squad_share[it->first] ? 1 : 0, it->second.size(), s_squad_npcs[it->first].size());
	}
	FlushLog();
}

// --- §14 step 8 phase 3 Q1: the offer pool and the claim transaction (doc §7.2) ------------

void coop_task_offer(const shared_str& task_id, u16 offer_id, bool faction, bool world_state,
                     u16 target_id)
{
	if (!task_id.size())
		return;
	if (s_task_pool.find(task_id) != s_task_pool.end())
		return;                                  // idempotent: already offered

	coop_offer o;
	o.offer_id    = offer_id;
	o.faction     = faction;
	o.world_state = world_state;
	// Captured NOW, while the offerer is a live object. It cannot be looked up later: once a
	// faction quest is claimed its owner is the world tier, which is a reserved registry key and
	// NOT an entity, so coop_community_of(owner) would resolve nothing (see mp_coop_owner.h).
	o.community   = offer_id ? coop_community_of(offer_id) : u16(-1);
	s_task_pool[task_id] = o;

	// §7.3: the world-state change that completes it, recorded separately so it survives the claim.
	// The convenience form is the n=1 kill condition — Q2's shape, expressed in Q3's record.
	if (target_id != u16(-1))
	{
		xr_vector<u16> one;
		one.push_back(target_id);
		coop_task_set_condition(task_id, u8(coop_cond_kill), one, 0);
	}

	Msg("- COOP(quest): OFFER '%s' by %u faction=%d world_state=%d community=%d target=%u pool=%u",
		task_id.c_str(), u32(offer_id), o.faction ? 1 : 0, o.world_state ? 1 : 0,
		int(short(o.community)), u32(target_id), u32(s_task_pool.size()));
}

coop_claim_result coop_task_claim(const shared_str& task_id, u16 player_id)
{
	xr_map<shared_str, coop_offer>::iterator it = s_task_pool.find(task_id);
	if (it == s_task_pool.end())
	{
		// Absent covers both "never offered" and "somebody already took it". §7.2 wants the
		// second claimant to see it GONE rather than to be refused by a lock, so the pool entry
		// is removed on claim and this is the answer a loser gets. The distinction between the
		// two only matters to a log line, so make the log line carry it.
		const bool given = (Level().GameTaskManager().HasGameTask(task_id, false) != NULL);
		Msg("- COOP(quest): CLAIM '%s' by %u -> %s", task_id.c_str(), u32(player_id),
			given ? "TAKEN (already claimed)" : "ABSENT (never offered)");
		return given ? coop_claim_taken : coop_claim_absent;
	}

	const coop_offer o = it->second;
	s_task_pool.erase(it);                       // §7.2: one claimant, and it leaves the pool

	// Give it inside an acting scope for the claimant, so the ownership tagging in
	// GiveGameTaskToActor does the routing rather than a second, divergent code path.
	CGameTask* task = xr_new<CGameTask>();
	task->m_ID = task_id;
	task->m_Title = task_id;
	task->SetTaskState(eTaskStateInProgress);
	task->m_ReceiveTime = Level().GetGameTime();
	{
		mp_coop_owner::acting_scope scope(player_id);
		Level().GameTaskManager().GiveGameTaskToActor(task, 0, false, 0);
	}

	// §7.4. A personal errand tracks the claimant. A FACTION quest was taken on behalf of the
	// community, so it lives on the world tier and its completion is member-agnostic — the doc is
	// explicit that this is a feature and not the single-actor bug coming back ("a faction-level
	// fact correctly does not care which member").
	if (o.faction)
	{
		s_task_owner[task_id]     = mp_coop_owner::world_key;
		s_task_community[task_id] = o.community;
		s_faction_task.insert(task_id);
	}

	Msg("- COOP(quest): CLAIM '%s' by %u -> OK owner=%u faction=%d pool=%u",
		task_id.c_str(), u32(player_id), u32(coop_task_owner_of(task_id)),
		o.faction ? 1 : 0, u32(s_task_pool.size()));

	Level().GameTaskManager().coop_broadcast_tasks();
	return coop_claim_ok;
}

u16 coop_task_owner_of(const shared_str& task_id)
{
	xr_map<shared_str, u16>::iterator it = s_task_owner.find(task_id);
	return (it != s_task_owner.end()) ? it->second : mp_coop_owner::none;
}

u32 coop_task_pool_size() { return u32(s_task_pool.size()); }

u16 coop_task_target_of(const shared_str& task_id)
{
	xr_map<shared_str, coop_condition>::iterator it = s_task_cond.find(task_id);
	if (it == s_task_cond.end() || it->second.kind != u8(coop_cond_kill) || it->second.remaining.empty())
		return mp_coop_owner::none;
	return it->second.remaining[0];
}

u32 coop_task_cond_remaining(const shared_str& task_id)
{
	xr_map<shared_str, coop_condition>::iterator it = s_task_cond.find(task_id);
	return (it != s_task_cond.end()) ? u32(it->second.remaining.size()) : 0u;
}

void coop_task_set_condition(const shared_str& task_id, u8 kind, const xr_vector<u16>& targets,
                             u64 deadline_game_ms)
{
	if (!task_id.size() || kind == u8(coop_cond_none))
		return;

	coop_condition c;
	c.kind     = kind;
	c.deadline = deadline_game_ms;
	// De-duplicate the target list. "Kill 3 mutants" with the same id listed twice would otherwise
	// need two deaths of one entity, which is a task nobody can finish — and it would look like a
	// completion bug rather than a bad offer, because every death would correctly decrement once.
	for (u32 i = 0; i < targets.size(); ++i)
		if (targets[i] != u16(-1) && std::find(c.remaining.begin(), c.remaining.end(), targets[i]) == c.remaining.end())
			c.remaining.push_back(targets[i]);

	s_task_cond[task_id] = c;

	Msg("- COOP(quest): COND SET '%s' kind=%u targets=%u deadline=%I64u",
		task_id.c_str(), u32(kind), u32(c.remaining.size()), deadline_game_ms);
}

void coop_dump_task_pool()
{
	Msg("- COOP(quest): pool=%u unclaimed offer(s)", u32(s_task_pool.size()));
	for (xr_map<shared_str, coop_offer>::iterator it = s_task_pool.begin(); it != s_task_pool.end(); ++it)
		Msg("    offer '%s' by %u faction=%d world_state=%d", it->first.c_str(),
			u32(it->second.offer_id), it->second.faction ? 1 : 0, it->second.world_state ? 1 : 0);
	FlushLog();
}

// --- §14 step 8 phase 3 Q2+Q3: completion on a world-state change (doc §7.3) ----------------
//
// §7.3 splits completion into the half the server can already observe and the half that needs
// bookkeeping. Q2 did the first: a death is server-authoritative here — it arrives as GE_DIE and
// lands in game_sv_Single::on_death — so a task bound to ONE entity completes when that entity
// dies, and the hook IS the algorithm. Q3 does the second: "clear" and "defend" cannot be answered
// from a single event, because the answer depends on what earlier events already did (how many of
// the group are still standing) or on a clock that no event carries (is the escort still alive NOW
// that the deadline is here). Both need a record between events, which is coop_condition.
//
// §7.2's conservation runs through all of it: the world changes ONCE, so the verdict is taken once
// and the record is spent by it.
//
// Reach a terminal verdict on `id`: set the task's state, spend the condition, and say so.
//
// EVERY caller logs through here, at the instant the verdict is taken, and that is the single most
// important property of this whole layer to preserve. A condition is CONSUMED by the verdict — the
// remaining set empties, the record is erased — so anything that reads the condition afterwards
// sees the same "nothing here" a task that never had one shows. Evidence that a task completed has
// to be emitted AT the transition or it cannot be had at all; a later reader is measuring the
// absence of state, not the presence of an outcome.
static void coop_task_verdict(const shared_str& id, bool completed, LPCSTR why, u16 actor_id)
{
	const u16 owner = coop_task_owner_of(id);
	const u32 evals = (s_task_cond.find(id) != s_task_cond.end()) ? s_task_cond[id].evaluations : 0;
	s_task_cond.erase(id);          // spent — exactly once, with no second flag to keep in step

	CGameTask* const t = Level().GameTaskManager().HasGameTask(id, true);
	if (!t)
	{
		// Bound but not in progress: still on the shelf, or already finished. Either way the
		// world-state change has now happened, so an unclaimed offer must LEAVE the pool rather
		// than stay claimable for a change nobody can repeat — the same conservation §7.2 asks
		// for, applied to an offer instead of a claim.
		const bool was_offered = (s_task_pool.erase(id) != 0);
		Msg("- COOP(quest): VERDICT '%s' %s but not in progress (was_offered=%d) — condition spent",
			id.c_str(), completed ? "COMPLETE" : "FAIL", was_offered ? 1 : 0);
		return;
	}

	Level().GameTaskManager().SetTaskState(t, completed ? eTaskStateCompleted : eTaskStateFail);

	// `actor_id` is the killer for a death and mp_coop_owner::none for a deadline. It is logged and
	// NOT tested: a faction quest's owner is world_key, which is not an entity and can never be the
	// killer, so crediting only the owner would make every faction quest uncompletable (§7.4).
	Msg("- COOP(quest): %s '%s' owner=%u killer=%u member_agnostic=%d evaluations=%u (%s)",
		completed ? "COMPLETE" : "FAILED", id.c_str(), u32(owner), u32(actor_id),
		(owner == mp_coop_owner::world_key) ? 1 : 0, evals, why);
}

u32 coop_task_on_world_death(u16 dead_id, u16 killer_id)
{
	if (!xr_enet::enabled() || !ai().get_alife() || s_task_cond.empty())
		return 0;

	// Collect first: a verdict mutates s_task_cond (and broadcasts), and iterating a map while
	// erasing out from under the iterator is how this would work in testing and crash later.
	xr_vector<shared_str> touched;
	for (xr_map<shared_str, coop_condition>::iterator it = s_task_cond.begin(); it != s_task_cond.end(); ++it)
		if (std::find(it->second.remaining.begin(), it->second.remaining.end(), dead_id) != it->second.remaining.end())
			touched.push_back(it->first);
	if (touched.empty())
		return 0;

	u32 terminal = 0;
	for (u32 i = 0; i < touched.size(); ++i)
	{
		const shared_str& id = touched[i];
		xr_map<shared_str, coop_condition>::iterator it = s_task_cond.find(id);
		if (it == s_task_cond.end())
			continue;                       // a previous verdict in this same loop spent it
		coop_condition& c = it->second;

		const u32 before = u32(c.remaining.size());
		c.remaining.erase(std::remove(c.remaining.begin(), c.remaining.end(), dead_id), c.remaining.end());
		const u32 after = u32(c.remaining.size());
		++c.evaluations;

		// Emitted BEFORE the verdict and at the instant of the transition, carrying both sides of
		// it. This is the line that makes an intermediate step observable at all: after two of
		// three deaths there is no state anywhere that says "two of three" once the third lands,
		// and an implementation that completed on the FIRST death would leave a final state
		// indistinguishable from a correct one.
		Msg("- COOP(quest): COND '%s' kind=%u event=death id=%u before=%u after=%u eval=%u verdict=%s",
			id.c_str(), u32(c.kind), u32(dead_id), before, after, c.evaluations,
			(c.kind == u8(coop_cond_defend)) ? "FAIL"
				: (after == 0 ? "COMPLETE" : "pending"));

		if (c.kind == u8(coop_cond_defend))
		{
			// §7.3 defend: the thing you were keeping alive died. This is the only outcome in this
			// layer that is not a completion, and it must land HERE rather than being left for the
			// deadline sweep to notice — a task that stays "in progress" until its deadline after
			// the escort is already dead is telling every player a lie for the whole interval.
			coop_task_verdict(id, false, "defend target died before the deadline", killer_id);
			++terminal;
		}
		else if (after == 0)
		{
			coop_task_verdict(id, true, "all kill targets down", killer_id);
			++terminal;
		}
	}

	if (terminal)
		Level().GameTaskManager().coop_broadcast_tasks();
	FlushLog();
	return terminal;
}

u32 coop_task_tick_conditions(u64 now_game_ms)
{
	if (!xr_enet::enabled() || !ai().get_alife() || s_task_cond.empty())
		return 0;

	xr_vector<shared_str> due;
	for (xr_map<shared_str, coop_condition>::iterator it = s_task_cond.begin(); it != s_task_cond.end(); ++it)
		if (it->second.kind == u8(coop_cond_defend) && it->second.deadline &&
		    now_game_ms >= it->second.deadline)
			due.push_back(it->first);
	if (due.empty())
		return 0;

	u32 terminal = 0;
	for (u32 i = 0; i < due.size(); ++i)
	{
		const shared_str& id = due[i];
		xr_map<shared_str, coop_condition>::iterator it = s_task_cond.find(id);
		if (it == s_task_cond.end())
			continue;
		coop_condition& c = it->second;
		++c.evaluations;

		// Reaching here at all means the target never died: a death would have spent the record in
		// coop_task_on_world_death, and this sweep only ever sees records that still exist. The
		// alive count is logged as read AT the deadline rather than inferred, so the log answers
		// "what did the server believe when it decided" and not merely "what did it decide".
		Msg("- COOP(quest): COND '%s' kind=%u event=deadline now=%I64u deadline=%I64u alive=%u eval=%u verdict=COMPLETE",
			id.c_str(), u32(c.kind), now_game_ms, c.deadline, u32(c.remaining.size()), c.evaluations);

		coop_task_verdict(id, true, "defend target alive at the deadline", mp_coop_owner::none);
		++terminal;
	}

	if (terminal)
		Level().GameTaskManager().coop_broadcast_tasks();
	FlushLog();
	return terminal;
}

// --- §14 step 8 phase 3 Q2: the co-op quest state rides the .scop (doc §7.2) ----------------
namespace
{
	// Bumped whenever the layout below changes. An UNKNOWN version is refused rather than parsed
	// (see the loader) — the same discipline the step-7 D3 dirty flag settled on, for the same
	// reason: refusing loses the pool, guessing corrupts ownership.
	//
	// v1 (Q2): ... + a trailing str->u16 map of single kill targets.
	// v2 (Q3): ... + a condition table (kind, target list, deadline, evaluation count).
	//
	// v1 is READ, not refused: it is a version we know, and its target map is exactly the n=1 kill
	// condition v2 stores generally, so it migrates rather than being discarded. The refusal is for
	// versions from the FUTURE, which is a different thing entirely — those we cannot interpret,
	// and Q2's rule stands for them.
	const u16 coop_quest_state_version = 2;

	void coop_write_str_u16_map(IWriter& stream, xr_map<shared_str, u16>& m)
	{
		stream.w_u32(u32(m.size()));
		for (xr_map<shared_str, u16>::iterator it = m.begin(); it != m.end(); ++it)
		{
			stream.w_stringZ(it->first);
			stream.w_u16(it->second);
		}
	}

	// Reading our own file, but reading it DEFENSIVELY. A count field is the one value that turns
	// a truncated or half-written chunk into an unbounded loop, and IReader::r_stringZ(shared_str&)
	// is unbounded by construction — it walks to the next NUL wherever that is, off the end of the
	// buffer included. So every read is checked against the bytes actually left in OUR chunk, and
	// a failure abandons the whole record set rather than keeping the prefix: half a pool is not a
	// safer pool, it is a pool that disagrees with the ownership map beside it.
	struct coop_quest_reader
	{
		IReader& s;
		int      end;      // one past the last byte of our chunk
		bool     ok;

		coop_quest_reader(IReader& _s, int _end): s(_s), end(_end), ok(true) {}

		bool room(int need)
		{
			if (ok && s.tell() + need > end)
				ok = false;
			return ok;
		}

		u32 count(u32 min_bytes_each)
		{
			if (!room(4))
				return 0;
			const u32 n = s.r_u32();
			if (u64(n) * u64(min_bytes_each) > u64(end - s.tell()))
			{
				ok = false;
				return 0;
			}
			return n;
		}

		bool str(shared_str& out)
		{
			if (!ok)
				return false;
			const char* const base = (const char*)s.pointer();
			const int avail = end - s.tell();
			int n = 0;
			while (n < avail && base[n])
				++n;
			if (n >= avail)            // no terminator inside the chunk -> refuse
			{
				ok = false;
				return false;
			}
			s.r_stringZ(out);
			return true;
		}

		u64 u64v() { return room(8) ? s.r_u64() : u64(0); }
		u32 u32v() { return room(4) ? s.r_u32() : 0u; }
		u16 u16v() { return room(2) ? s.r_u16() : u16(0); }
		u8  u8v()  { return room(1) ? s.r_u8()  : u8(0); }
	};

	void coop_read_str_u16_map(coop_quest_reader& r, xr_map<shared_str, u16>& m)
	{
		const u32 n = r.count(/*min bytes per entry: NUL + u16*/ 3);
		for (u32 i = 0; i < n && r.ok; ++i)
		{
			shared_str id;
			if (!r.str(id))
				break;
			const u16 v = r.u16v();
			if (r.ok && id.size())
				m[id] = v;
		}
	}
}

void coop_task_state_save(IWriter& stream)
{
	stream.open_chunk(COOP_QUEST_CHUNK_DATA);
	stream.w_u16(coop_quest_state_version);

	stream.w_u32(u32(s_task_pool.size()));
	for (xr_map<shared_str, coop_offer>::iterator it = s_task_pool.begin(); it != s_task_pool.end(); ++it)
	{
		stream.w_stringZ(it->first);
		stream.w_u16(it->second.offer_id);
		stream.w_u16(it->second.community);
		stream.w_u8(u8((it->second.faction ? 1 : 0) | (it->second.world_state ? 2 : 0)));
	}

	coop_write_str_u16_map(stream, s_task_owner);

	stream.w_u32(u32(s_faction_task.size()));
	for (xr_set<shared_str>::const_iterator it = s_faction_task.begin(); it != s_faction_task.end(); ++it)
		stream.w_stringZ(*it);

	coop_write_str_u16_map(stream, s_task_community);

	// v2: the condition table. `evaluations` is persisted with it rather than reset, because it is
	// the count of events this condition has already absorbed — a "kill 3 of 3" that survived a
	// restart having taken two of them is not the same condition as a fresh one, and a diagnostic
	// that said so only until the next reboot would be worse than none.
	stream.w_u32(u32(s_task_cond.size()));
	for (xr_map<shared_str, coop_condition>::iterator it = s_task_cond.begin(); it != s_task_cond.end(); ++it)
	{
		stream.w_stringZ(it->first);
		stream.w_u8(it->second.kind);
		stream.w_u64(it->second.deadline);
		stream.w_u32(it->second.evaluations);
		stream.w_u16(u16(it->second.remaining.size()));
		for (u32 i = 0; i < it->second.remaining.size(); ++i)
			stream.w_u16(it->second.remaining[i]);
	}

	stream.close_chunk();

	Msg("- COOP(quest): state saved  pool=%u owners=%u faction=%u community=%u conditions=%u (v%u)",
		u32(s_task_pool.size()), u32(s_task_owner.size()), u32(s_faction_task.size()),
		u32(s_task_community.size()), u32(s_task_cond.size()), u32(coop_quest_state_version));
}

void coop_task_state_load(IReader& stream)
{
	// Clear FIRST and unconditionally. These maps are file-static and outlive a
	// restart_simulator, so a load that found nothing must leave the server holding nothing —
	// otherwise loading an older world would silently inherit the previous world's ownership.
	s_task_pool.clear();
	s_task_owner.clear();
	s_faction_task.clear();
	s_task_community.clear();
	s_task_cond.clear();

	// find_chunk REWINDS and scans, so remember where the caller was and put the cursor back:
	// the registry read that precedes us must not be able to notice that we ran.
	const int caller_pos = stream.tell();

	const u32 chunk_size = stream.find_chunk(COOP_QUEST_CHUNK_DATA);
	if (!chunk_size)
	{
		stream.seek(caller_pos);
		Msg("- COOP(quest): no state chunk in this save (stock .scop, or written before Q2) — starting empty");
		return;
	}

	coop_quest_reader r(stream, stream.tell() + int(chunk_size));

	const u16 ver = r.u16v();
	// v1 and v2 differ ONLY in the trailing block, and everything before it is byte-identical, so
	// one reader handles both and the version decides how the tail is read. A version we do not
	// know is still refused outright — that rule was never about old files, it was about files
	// from the future, whose fields we would be guessing at.
	if (!r.ok || (ver != 1 && ver != 2))
	{
		stream.seek(caller_pos);
		Msg("! COOP(quest): state version %u is neither 1 nor %u — REFUSING to parse it; pool and ownership start empty",
			u32(ver), u32(coop_quest_state_version));
		return;
	}

	const u32 pool_n = r.count(/*min bytes per offer: NUL + u16 + u16 + u8*/ 6);
	for (u32 i = 0; i < pool_n && r.ok; ++i)
	{
		shared_str id;
		if (!r.str(id))
			break;
		coop_offer o;
		o.offer_id    = r.u16v();
		o.community   = r.u16v();
		const u8 fl   = r.u8v();
		o.faction     = (fl & 1) != 0;
		o.world_state = (fl & 2) != 0;
		if (r.ok && id.size())
			s_task_pool[id] = o;
	}

	coop_read_str_u16_map(r, s_task_owner);

	const u32 fac_n = r.count(/*min bytes per id: NUL*/ 1);
	for (u32 i = 0; i < fac_n && r.ok; ++i)
	{
		shared_str id;
		if (!r.str(id))
			break;
		if (id.size())
			s_faction_task.insert(id);
	}

	coop_read_str_u16_map(r, s_task_community);

	u32 migrated = 0;
	if (ver == 1)
	{
		// Q2's trailing block: task id -> ONE kill target. That is the n=1 case of a v2 kill
		// condition, so it is migrated rather than dropped — a save made before Q3 keeps its
		// bindings and the tasks in it stay completable. evaluations starts at 0, which is
		// honest: v1 recorded no such count, and inventing one would be a diagnostic that lies.
		xr_map<shared_str, u16> old_targets;
		coop_read_str_u16_map(r, old_targets);
		for (xr_map<shared_str, u16>::iterator it = old_targets.begin(); it != old_targets.end(); ++it)
		{
			coop_condition c;
			c.kind = u8(coop_cond_kill);
			c.remaining.push_back(it->second);
			s_task_cond[it->first] = c;
			++migrated;
		}
	}
	else
	{
		const u32 cond_n = r.count(/*min bytes per condition: NUL + u8 + u64 + u32 + u16*/ 16);
		for (u32 i = 0; i < cond_n && r.ok; ++i)
		{
			shared_str id;
			if (!r.str(id))
				break;
			coop_condition c;
			c.kind        = r.u8v();
			c.deadline    = r.u64v();
			c.evaluations = r.u32v();
			const u16 n   = r.u16v();
			if (!r.room(int(n) * 2))
				break;
			for (u16 j = 0; j < n; ++j)
				c.remaining.push_back(r.u16v());
			if (r.ok && id.size())
				s_task_cond[id] = c;
		}
	}

	stream.seek(caller_pos);

	if (!r.ok)
	{
		// Ran out of chunk mid-record. Keeping what was read would leave the pool and the
		// ownership map describing different worlds, so drop the lot and say so — loudly, because
		// the alternative is a server that quietly hands out a task somebody already owns.
		s_task_pool.clear();
		s_task_owner.clear();
		s_faction_task.clear();
		s_task_community.clear();
		s_task_cond.clear();
		Msg("! COOP(quest): state chunk is truncated or malformed (%u bytes) — DISCARDED, pool and ownership start empty",
			chunk_size);
		FlushLog();
		return;
	}

	Msg("- COOP(quest): state loaded pool=%u owners=%u faction=%u community=%u conditions=%u migrated=%u (v%u)",
		u32(s_task_pool.size()), u32(s_task_owner.size()), u32(s_faction_task.size()),
		u32(s_task_community.size()), u32(s_task_cond.size()), migrated, u32(ver));
	for (xr_map<shared_str, coop_condition>::iterator it = s_task_cond.begin(); it != s_task_cond.end(); ++it)
		Msg("    cond '%s' kind=%u remaining=%u deadline=%I64u evaluations=%u", it->first.c_str(),
			u32(it->second.kind), u32(it->second.remaining.size()), it->second.deadline,
			it->second.evaluations);
	FlushLog();
}

// Does player `who` receive task `task_id` (owned by `owner`)?
static bool coop_task_goes_to(const shared_str& task_id, u16 owner, u16 who)
{
	if (owner == 0)          return true;    // unowned/global -> everyone (safe default)
	if (who == owner)        return true;    // the owner always sees their own

	// §7.4 (step 8 phase 3): a task owned by the WORLD tier belongs to a faction, not a person.
	// The owner is a reserved registry key here, so there is no object to read a community off —
	// the community was captured at offer time. With no community recorded it is a plain world
	// task and goes to everyone, which is what member-agnostic means with no members named.
	if (owner == mp_coop_owner::world_key)
	{
		xr_map<shared_str, u16>::iterator c = s_task_community.find(task_id);
		if (c == s_task_community.end() || c->second == u16(-1))
			return true;
		return coop_community_of(who) == c->second;
	}

	// faction-wide task: any player of the owner's community
	if (s_faction_task.find(task_id) != s_faction_task.end())
	{
		const u16 c = coop_community_of(owner);
		if (c != u16(-1) && coop_community_of(who) == c)
			return true;
	}

	// squad sharing: same squad as the owner, and that squad has sharing on
	xr_map<u16, u32>::iterator os = s_player_squad.find(owner);
	xr_map<u16, u32>::iterator ws = s_player_squad.find(who);
	if (os != s_player_squad.end() && ws != s_player_squad.end() &&
		os->second == ws->second && s_squad_share[os->second])
		return true;

	return false;
}


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

	// MP fork (§19 co-op): tag ownership. If this task is being given while a player is in a
	// dialogue (xrServer::coop_run_dialog_action opened an mp_coop_owner::acting_scope), it
	// belongs to THAT player. Given outside dialogue (smart terrains, timers, level scripts) it
	// is left UNTAGGED, and an absent key routes to everyone - the safe default until faction
	// flagging lands.
	const u16 coop_acting = mp_coop_owner::acting_actor();
	if (xr_enet::enabled() && ai().get_alife() && coop_acting != mp_coop_owner::none)
		s_task_owner[t->m_ID] = coop_acting;

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
	// MP fork (quest E2E diag): log entry + guard state so we can tell if this is reached
	if (strstr(Core.Params, "-dbg"))
		Msg("* COOP_TASKS_CL: coop_apply_tasks called, alife=%p", ai().get_alife());

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

	// MP fork (§14 step 8 phase 3 Q1 / doc §7.2): the shared offer pool. Behind r_eof() so a
	// server that does not send the section cannot make this read run off the end of the packet.
	// The pool is logged unconditionally, not under -dbg: this is the client's only evidence that
	// a claim it lost actually removed the offer, and the harness reads it on the CLIENT side.
	if (!packet.r_eof())
	{
		const u16 offers = packet.r_u16();
		Msg("* COOP_POOL_CL: pool=%u offer(s)", u32(offers));
		for (u16 p = 0; p < offers; ++p)
		{
			shared_str oid;
			packet.r_stringZ(oid);
			const u16 by = packet.r_u16();
			const u8  fl = packet.r_u8();
			Msg("* COOP_POOL_CL:   offer '%s' by %u faction=%d world_state=%d",
				oid.c_str(), u32(by), (fl & 1) ? 1 : 0, (fl & 2) ? 1 : 0);
		}
	}

	// MP fork (C2/quest E2E): log task adoption for test harness
	if (strstr(Core.Params, "-dbg"))
	{
		Msg("* COOP_TASKS: adopted %u task(s) from server", tasks.size());
		for (u32 j = 0; j < tasks.size(); ++j)
		{
			CGameTask* const tj = tasks[j].game_task;
			if (tj)
				Msg("*   COOP_TASKS[%u]: id='%s' state=%d", j, tj->m_ID.c_str(), int(tj->GetTaskState()));
		}
	}

	m_flags.set(eChanged, TRUE); // makes the PDA redraw
}


// MP fork (§19 co-op): serialise the task list to every client. Reuses each task's own
// save/load - the same pair savegames use - so objectives, timers, titles and map hints all
// come along without inventing a wire format for them.
namespace
{
	// Per-client task sender: builds and ships each connected player only the tasks it should
	// receive (owner / faction / squad-share). save_task writes to an IWriter, not a packet,
	// so each task is serialised into memory once and reused across clients.
	struct coop_task_sender
	{
		IPureServer* server;
		vGameTasks*  tasks;
		xr_vector<CMemoryWriter*>* blobs; // one per task, index-aligned with *tasks

		void operator()(IClient* client)
		{
			xrClientData* const cl = static_cast<xrClientData*>(client);
			// MP fork (§19 co-op): co-op clients may never send M_CLIENTREADY (actors are
			// spawned via grace timer in coop_poll_spawns), so net_Ready can stay false.
			// Use cl->owner as the "client is in-game" signal instead.
			if (!cl || !cl->owner)
			{
				if (strstr(Core.Params, "-dbg") && cl)
					Msg("* COOP_TASKS_SV: SKIP client 0x%08x (owner=%p)",
						cl->ID.value(), cl->owner);
				return;
			}
			const u16 actor_id = cl->owner->ID;

			// Two passes so the count is written up front (NET_Packet has no seek-back write).
			// First: which task indices this player receives.
			xr_vector<u32> mine;
			for (u32 i = 0; i < tasks->size(); ++i)
			{
				CGameTask* const task = (*tasks)[i].game_task;
				if (!task)
					continue;
				xr_map<shared_str, u16>::iterator ow = s_task_owner.find(task->m_ID);
				const u16 owner = (ow != s_task_owner.end()) ? ow->second : u16(0);
				if (coop_task_goes_to(task->m_ID, owner, actor_id))
					mine.push_back(i);
			}

			NET_Packet packet;
			packet.w_begin(M_XRNET_TASKS);
			packet.w_u16(u16(mine.size()));
			for (u32 k = 0; k < mine.size(); ++k)
			{
				const u32 i = mine[k];
				CGameTask* const task = (*tasks)[i].game_task;
				CMemoryWriter* const blob = (*blobs)[i];
				packet.w_stringZ(task->m_ID);
				packet.w_u16(u16(blob->size()));
				if (blob->size())
					packet.w(blob->pointer(), blob->size());
			}

			// MP fork (§14 step 8 phase 3 Q1 / doc §7.2): the unclaimed OFFER POOL, appended as a
			// trailing section. Unfiltered on purpose — every player sees the same pool, because
			// "one player claims it and others see it's gone" only means anything if they were
			// looking at the same shelf. The reader takes this behind r_eof(), so a packet
			// without the section still parses.
			packet.w_u16(u16(s_task_pool.size()));
			for (xr_map<shared_str, coop_offer>::iterator po = s_task_pool.begin();
			     po != s_task_pool.end(); ++po)
			{
				packet.w_stringZ(po->first);
				packet.w_u16(po->second.offer_id);
				packet.w_u8(u8((po->second.faction ? 1 : 0) | (po->second.world_state ? 2 : 0)));
			}

			server->SendTo(cl->ID, packet, net_flags(TRUE, TRUE));

			// MP fork (quest E2E diag): confirm packet sent to this client
			if (strstr(Core.Params, "-dbg"))
				Msg("* COOP_TASKS_SV: SENT %u task(s) to client 0x%08x (actor id %u)",
					u32(mine.size()), cl->ID.value(), actor_id);
		}
	};
}

void CGameTaskManager::coop_broadcast_tasks()
{
	if (!xr_enet::enabled() || !ai().get_alife() || !Level().Server)
		return;

	vGameTasks& tasks = GetGameTasks();

	// serialise every task once
	xr_vector<CMemoryWriter*> blobs;
	blobs.resize(tasks.size());
	for (u32 i = 0; i < tasks.size(); ++i)
	{
		blobs[i] = xr_new<CMemoryWriter>();
		if (tasks[i].game_task)
			tasks[i].game_task->save_task(*blobs[i]);
	}

	// MP fork (quest E2E): log broadcast for test harness
	if (strstr(Core.Params, "-dbg"))
		Msg("* COOP_TASKS_SV: broadcasting %u task(s) to clients", tasks.size());

	coop_task_sender sender;
	sender.server = Level().Server;
	sender.tasks  = &tasks;
	sender.blobs  = &blobs;
	Level().Server->ForEachClientDoSender(sender);

	for (u32 i = 0; i < blobs.size(); ++i)
		xr_delete(blobs[i]);
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
