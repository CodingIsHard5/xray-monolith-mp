//////////////////////////////////////////////////////////////////////////
// character_community.cpp:		структура представления группировки
//							
//////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "character_community.h"
#include "../xrNetServer/xr_enet_transport.h"   // MP fork (§19 co-op)
#include "ai_space.h"                           // MP fork (§14 step 8 P4 R5): the write instrument
#include "../xrServerEntities/script_engine.h"  //   needs the Lua state to name a script caller

//////////////////////////////////////////////////////////////////////////
COMMUNITY_DATA::COMMUNITY_DATA(CHARACTER_COMMUNITY_INDEX idx, CHARACTER_COMMUNITY_ID idn, LPCSTR team_str)
{
	index = idx;
	id = idn;
	team = (u8)atoi(team_str);
}

//////////////////////////////////////////////////////////////////////////
CHARACTER_COMMUNITY::GOODWILL_TABLE CHARACTER_COMMUNITY::m_relation_table;
CHARACTER_COMMUNITY::SYMPATHY_TABLE CHARACTER_COMMUNITY::m_sympathy_table;

//////////////////////////////////////////////////////////////////////////
CHARACTER_COMMUNITY::CHARACTER_COMMUNITY()
{
	m_current_index = NO_COMMUNITY_INDEX;
}

CHARACTER_COMMUNITY::~CHARACTER_COMMUNITY()
{
}


void CHARACTER_COMMUNITY::set(CHARACTER_COMMUNITY_ID id)
{
	m_current_index = IdToIndex(id);
}

CHARACTER_COMMUNITY_ID CHARACTER_COMMUNITY::id() const
{
	// MP fork (§19 co-op): defence in depth for the crash above. IndexToId asserts hard on an
	// out-of-range index, and this is called from cosmetic paths — the crosshair info HUD, the
	// character card, script queries — for every creature the player looks at. On a thin
	// client any replicated creature whose character data has not arrived yet is momentarily
	// index -1, and taking the whole session down for a missing HUD label is never the right
	// trade. Ask without the assert and fall back to an empty id. Plain SP/MP keep the assert,
	// where an unset community really is a content bug worth failing loudly on.
	if (xr_enet::enabled())
	{
		const COMMUNITY_DATA* data = GetByIndex(m_current_index, true);
		if (!data)
			data = GetByIndex(0, true); // first configured faction: always a real, translatable id
		if (data)
			return data->id;
	}
	return IndexToId(m_current_index);
}

u8 CHARACTER_COMMUNITY::team() const
{
	// Same reasoning as id(), except this one is a raw vector index — an unset community is
	// an out-of-bounds read, not merely an assert. Team 0 is the neutral fallback.
	const COMMUNITY_DATA* const data = GetByIndex(m_current_index, true);
	return data ? data->team : u8(0);
}


void CHARACTER_COMMUNITY::InitIdToIndex()
{
	section_name = "game_relations";
	line_name = "communities";

	m_relation_table.set_table_params("communities_relations");
	m_sympathy_table.set_table_params("communities_sympathy", 1);
}


CHARACTER_GOODWILL CHARACTER_COMMUNITY::relation(CHARACTER_COMMUNITY_INDEX to)
{
	return relation(m_current_index, to);
}

CHARACTER_GOODWILL CHARACTER_COMMUNITY::relation(CHARACTER_COMMUNITY_INDEX from, CHARACTER_COMMUNITY_INDEX to)
{
	VERIFY(from >= 0 && from <(int)m_relation_table.table().size());
	VERIFY(to >= 0 && to <(int)m_relation_table.table().size());

	if (from == NO_COMMUNITY_INDEX || to == NO_COMMUNITY_INDEX)
		return 0;

	return m_relation_table.table()[from][to];
}

// ===== COOP (§14 step 8 P4 R5): NAME THE `stalker -> actor` WRITER ==============================
//
// THE OPEN MEASUREMENT OF PHASE 4. R3.1's most important gate is that `stalker -> actor` — how a
// faction regards PLAYERS AS A CLASS — never moves, because one player's kill turning a faction
// hostile to everybody is §8.3's worst outcome. The cell has moved `-12` (run 4), `-11` (run 5),
// `-10` (run 7), `-13` (R8's run) and `0` on four others. **Intermittent AND unattributed**, and the
// spread is itself evidence it is not one deterministic cause.
//
// WHY INSTRUMENT HERE RATHER THAN SEARCH FOR IT. This is the single choke point: every faction<->
// faction write in the process reaches `m_relation_table` through this function. The table is built
// lazily from `game_relations.ltx` by `CIni_Table`, NOT through here, so there is no boot-time burst
// to filter out and every call logged below is a genuine runtime write.
//
// AND THE SEARCH HAD ALREADY FAILED, twice over, which is the argument for the choke point:
//  * `RELATION_REGISTRY::SetCommunityRelation`'s only engine caller is the Lua binding
//    `level.set_community_relation` plus this fork's own probes;
//  * a `-R` sweep of the deployed gamedata finds exactly three Lua callers of that binding, and NONE
//    writes `stalker -> actor` (`smr_civil_war` writes monolith->monolith, `mp_coop_rep` is ours).
//  * and the config baseline for `stalker -> actor` in `game_relations.ltx` is **0**, so a table
//    rebuild cannot produce a negative value — this is a write, not a re-read.
// A grep over this gamedata under-reports by construction ([[gamma-flat-grep-needs-capital-R]]), so
// "no script does this" is not a conclusion available from searching. Recording the write is.
//
// WHAT IS LOGGED, and why each part: the pair and both values (so a delta is readable without
// arithmetic); the LUA STACK, because the only non-engine caller is the Lua binding and the stack is
// what turns "a script did it" into a file and a line; and a NATIVE BACKTRACE, because this fork's
// own faction tier and R3.2's overlay also call here and must be distinguishable from a script.
//
// `get_lua_stack` + `Msg` rather than `print_stack`: the latter returns 0 in a release build unless
// `-dbg` is on the command line, which this harness passes to the CLIENTS only — a silent instrument
// that already cost this increment a diagnosis cycle once.
extern xr_vector<xr_string> get_lua_stack(lua_State* L);

namespace
{
	u32 s_coop_rel_writes = 0;      // EVERY write, so a zero below is readable as a real zero
	u32 s_coop_rel_logged = 0;
}

void coop_rel_dump()
{
	// THE POSITIVE CONTROL. Called from the R3.1 probe's arm, before any write, this prints
	// "INSTALLED, 0 writes". Without it a run with no write lines cannot be told apart from an
	// instrument that was never compiled in — and R5's gate is explicitly that a still cell must be
	// reported as NOT MEASURED, which is only meaningful if the instrument is known to be live.
	Msg("- COOP(rel): faction<->faction write instrument is INSTALLED. %u write(s) so far, %u logged. "
	    "Every write to the community relation table passes through CHARACTER_COMMUNITY::set_relation, "
	    "so a run with no COOP(relw) line below had NO faction<->faction write.",
	    s_coop_rel_writes, s_coop_rel_logged);
	FlushLog();
}

void CHARACTER_COMMUNITY::set_relation(CHARACTER_COMMUNITY_INDEX from, CHARACTER_COMMUNITY_INDEX to,
                                       CHARACTER_GOODWILL goodwill)
{
	VERIFY(from >= 0 && from <(int)m_relation_table.table().size());
	VERIFY(to >= 0 && to <(int)m_relation_table.table().size());
	VERIFY(goodwill != NO_GOODWILL);

	if (from == NO_COMMUNITY_INDEX || to == NO_COMMUNITY_INDEX)
		return;

	// ---- COOP (§14 step 8 P4 R5): record the write BEFORE it happens -----------------------------
	{
		++s_coop_rel_writes;
		const CHARACTER_GOODWILL was = m_relation_table.table()[from][to];
		// A write that changes nothing is not a writer worth naming, and the overlay re-applies
		// unchanged cells on every level load — logging those would bury the one that matters.
		if (was != goodwill)
		{
			// Bounded, in the shape used by the other co-op instruments: the first 64 in full, then
			// every 32nd, so a runaway writer stays visible without producing the multi-GB log this
			// fork has already generated once.
			const bool say = (s_coop_rel_writes <= 64u) || ((s_coop_rel_writes % 32u) == 0u);
			if (say)
			{
				++s_coop_rel_logged;
				const COMMUNITY_DATA* const df = GetByIndex(from, true);
				const COMMUNITY_DATA* const dt = GetByIndex(to, true);
				Msg("~ COOP(relw): FACTION RELATION WRITE #%u  %s(%d) -> %s(%d)  %d -> %d (delta %+d)",
					s_coop_rel_writes,
					df ? df->id.c_str() : "<unknown>", from,
					dt ? dt->id.c_str() : "<unknown>", to,
					(int)was, (int)goodwill, (int)(goodwill - was));

				lua_State* const L = ai().script_engine().lua();
				if (!L)
				{
					Msg("~ COOP(relw):   no Lua state — this write did NOT come from a script");
				}
				else
				{
					const xr_vector<xr_string> stack = get_lua_stack(L);
					if (stack.empty())
						Msg("~ COOP(relw):   Lua stack is EMPTY — called from C++, not from a script "
						    "(this fork's own faction tier and R3.2's overlay both call here)");
					for (u32 f = 0; f < stack.size() && f < 24u; ++f)
						Msg("~ COOP(relw):   %s", stack[f].c_str());
				}

				// The native frames separate OUR C++ callers from each other when the Lua stack is
				// empty — the faction tier, the overlay apply, and the decay all reach this function.
				enum { MAX_FRAMES = 16 };
				void* frames[MAX_FRAMES] = {0};
				const USHORT got = RtlCaptureStackBackTrace(1, MAX_FRAMES, frames, NULL);
				string4096 trace;
				trace[0] = 0;
				for (USHORT f = 0; f < got; ++f)
				{
					string64 one;
					xr_sprintf(one, " %p", frames[f]);
					xr_strcat(trace, one);
				}
				Msg("~ COOP(relw):   native stack (absolute; symbolicate with tools/pdb_symbols.py "
				    "against the pdb from THIS build):%s", trace);
				FlushLog();
			}
		}
	}

	m_relation_table.table()[from][to] = goodwill;
}

float CHARACTER_COMMUNITY::sympathy(CHARACTER_COMMUNITY_INDEX comm)
{
	VERIFY(comm >= 0 && comm <(int)m_sympathy_table.table().size());

	if (comm == NO_COMMUNITY_INDEX)
		return 0;

	return m_sympathy_table.table()[comm][0];
}

void CHARACTER_COMMUNITY::DeleteIdToIndexData()
{
	m_relation_table.clear();
	m_sympathy_table.clear();
	inherited::DeleteIdToIndexData();
}
