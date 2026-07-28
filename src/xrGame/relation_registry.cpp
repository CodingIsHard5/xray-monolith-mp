//////////////////////////////////////////////////////////////////////////
// relation_registry.cpp:	реестр для хранения данных об отношении персонажа к 
//							другим персонажам
//////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "relation_registry.h"
#include "alife_registry_wrappers.h"

#include "character_community.h"
#include "character_reputation.h"
#include "character_rank.h"

// MP fork (§14 step 8 phase 4 R2): the routing seam + the faction tier's .scop chunk.
#include "mp_coop_owner.h"
#include "../xrNetServer/xr_enet_transport.h"   // xr_enet::enabled() — the id-0 route is co-op only
#include "mp_coop_chunk_reader.h"
#include "alife_space.h"                    // COOP_REP_CHUNK_DATA
#include "alife_object_registry.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "script_engine.h"


//////////////////////////////////////////////////////////////////////////

SRelation::SRelation()
{
	m_iGoodwill = NEUTRAL_GOODWILL;
}

SRelation::~SRelation()
{
}

//////////////////////////////////////////////////////////////////////////

void RELATION_DATA::clear()
{
	personal.clear();
	communities.clear();
}

void RELATION_DATA::load(IReader& stream)
{
	load_data(personal, stream);
	load_data(communities, stream);
}

void RELATION_DATA::save(IWriter& stream)
{
	save_data(personal, stream);
	save_data(communities, stream);
}

//////////////////////////////////////////////////////////////////////////

RELATION_REGISTRY::RELATION_MAP_SPOTS::RELATION_MAP_SPOTS()
{
	spot_names[ALife::eRelationTypeFriend] = "friend_location";
	spot_names[ALife::eRelationTypeNeutral] = "neutral_location";
	spot_names[ALife::eRelationTypeEnemy] = "enemy_location";
	spot_names[ALife::eRelationTypeWorstEnemy] = "enemy_location";
	//spot_names[ALife::eRelationTypeWorstEnemy]	= "enemy_location";
	spot_names[ALife::eRelationTypeLast] = "neutral_location";
}

//////////////////////////////////////////////////////////////////////////

CRelationRegistryWrapper* RELATION_REGISTRY::m_relation_registry = NULL;
RELATION_REGISTRY::FIGHT_VECTOR* RELATION_REGISTRY::m_fight_registry = NULL;
RELATION_REGISTRY::RELATION_MAP_SPOTS* RELATION_REGISTRY::m_spot_names = NULL;


//////////////////////////////////////////////////////////////////////////


RELATION_REGISTRY::RELATION_REGISTRY()
{
}

RELATION_REGISTRY::~RELATION_REGISTRY()
{
}

//////////////////////////////////////////////////////////////////////////

extern void load_attack_goodwill();
extern bool IsGameTypeSingle();

CRelationRegistryWrapper& RELATION_REGISTRY::relation_registry()
{
	if (!m_relation_registry)
	{
		VERIFY(IsGameTypeSingle());

		m_relation_registry = xr_new<CRelationRegistryWrapper>();
		load_attack_goodwill();
	}

	return *m_relation_registry;
}


RELATION_REGISTRY::FIGHT_VECTOR& RELATION_REGISTRY::fight_registry()
{
	if (!m_fight_registry)
		m_fight_registry = xr_new<FIGHT_VECTOR>();

	return *m_fight_registry;
}

void RELATION_REGISTRY::clear_relation_registry()
{
	xr_delete(m_relation_registry);
	xr_delete(m_fight_registry);
	xr_delete(m_spot_names);
}

const shared_str& RELATION_REGISTRY::GetSpotName(ALife::ERelationType& type)
{
	if (!m_spot_names)
		m_spot_names = xr_new<RELATION_MAP_SPOTS>();
	return m_spot_names->GetSpotName(type);
}

//////////////////////////////////////////////////////////////////////////

void RELATION_REGISTRY::ClearRelations(u16 person_id)
{
	const RELATION_DATA* relation_data = relation_registry().registry().objects_ptr(person_id);
	if (relation_data)
	{
		relation_registry().registry().objects(person_id).clear();
	}
}


//////////////////////////////////////////////////////////////////////////
CHARACTER_GOODWILL RELATION_REGISTRY::GetGoodwill(u16 from, u16 to) const
{
	const RELATION_DATA* relation_data = relation_registry().registry().objects_ptr(from);

	if (relation_data)
	{
		PERSONAL_RELATION_MAP::const_iterator it = relation_data->personal.find(to);
		if (relation_data->personal.end() != it)
		{
			const SRelation& relation = (*it).second;
			return relation.Goodwill();
		}
	}
	//если отношение еще не задано, то возвращаем нейтральное
	return NEUTRAL_GOODWILL;
}

void RELATION_REGISTRY::SetGoodwill(u16 from, u16 to, CHARACTER_GOODWILL goodwill)
{
	RELATION_DATA& relation_data = relation_registry().registry().objects(from);

	static Ivector2 gw_limits = pSettings->r_ivector2(ACTIONS_POINTS_SECT, "personal_goodwill_limits");
	clamp(goodwill, gw_limits.x, gw_limits.y);

	relation_data.personal[to].SetGoodwill(goodwill);
}

void RELATION_REGISTRY::ForceSetGoodwill(u16 from, u16 to, CHARACTER_GOODWILL goodwill)
{
	RELATION_DATA& relation_data = relation_registry().registry().objects(from);

	// MP fork (§19 co-op): a thin client has no A-Life simulator, and ai().alife() derefs a
	// null one (0xC0000005 accessing 0x18). Goodwill is server-authoritative anyway — the
	// server runs the same script and replicates the result — so on a client just apply the
	// personal goodwill without the community corrections it cannot look up.
	if (!ai().get_alife())
	{
		relation_data.personal[to].SetGoodwill(goodwill);
		return;
	}

	CSE_ALifeTraderAbstract* from_obj = smart_cast<CSE_ALifeTraderAbstract*>(ai().alife().objects().object(from));
	CSE_ALifeTraderAbstract* to_obj = smart_cast<CSE_ALifeTraderAbstract*>(ai().alife().objects().object(to));

	if (!from_obj || !to_obj)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "RELATION_REGISTRY::ForceSetGoodwill  : cannot convert obj to CSE_ALifeTraderAbstract!");
		return;
	}
	CHARACTER_GOODWILL community_to_obj_goodwill = GetCommunityGoodwill(from_obj->Community(), to);
	CHARACTER_GOODWILL community_to_community_goodwill = GetCommunityRelation(
		from_obj->Community(), to_obj->Community());

	relation_data.personal[to].SetGoodwill(goodwill - community_to_obj_goodwill - community_to_community_goodwill);
}


void RELATION_REGISTRY::ChangeGoodwill(u16 from, u16 to, CHARACTER_GOODWILL delta_goodwill)
{
	CHARACTER_GOODWILL new_goodwill = GetGoodwill(from, to) + delta_goodwill;
	SetGoodwill(from, to, new_goodwill);
}

//////////////////////////////////////////////////////////////////////////
CHARACTER_GOODWILL RELATION_REGISTRY::GetCommunityGoodwill(CHARACTER_COMMUNITY_INDEX from_community,
                                                           u16 to_character) const
{
	const RELATION_DATA* relation_data = relation_registry().registry().objects_ptr(to_character);

	if (relation_data)
	{
		COMMUNITY_RELATION_MAP::const_iterator it = relation_data->communities.find(from_community);
		if (relation_data->communities.end() != it)
		{
			const SRelation& relation = (*it).second;
			return relation.Goodwill();
		}
	}
	//если отношение еще не задано, то возвращаем нейтральное
	return NEUTRAL_GOODWILL;
}

void RELATION_REGISTRY::SetCommunityGoodwill(CHARACTER_COMMUNITY_INDEX from_community, u16 to_character,
                                             CHARACTER_GOODWILL goodwill)
{
	static Ivector2 gw_limits = pSettings->r_ivector2(ACTIONS_POINTS_SECT, "community_goodwill_limits");
	clamp(goodwill, gw_limits.x, gw_limits.y);
	RELATION_DATA& relation_data = relation_registry().registry().objects(to_character);

	relation_data.communities[from_community].SetGoodwill(goodwill);
}

void RELATION_REGISTRY::ChangeCommunityGoodwill(CHARACTER_COMMUNITY_INDEX from_community, u16 to_character,
                                                CHARACTER_GOODWILL delta_goodwill)
{
	CHARACTER_GOODWILL gw = GetCommunityGoodwill(from_community, to_character) + delta_goodwill;
	SetCommunityGoodwill(from_community, to_character, gw);
}

//////////////////////////////////////////////////////////////////////////

CHARACTER_GOODWILL RELATION_REGISTRY::GetCommunityRelation(CHARACTER_COMMUNITY_INDEX index1,
                                                           CHARACTER_COMMUNITY_INDEX index2) const
{
	return CHARACTER_COMMUNITY::relation(index1, index2);
}

CHARACTER_GOODWILL RELATION_REGISTRY::GetRankRelation(CHARACTER_RANK_VALUE rank1, CHARACTER_RANK_VALUE rank2) const
{
	CHARACTER_RANK rank_from, rank_to;
	rank_from.set(rank1);
	rank_to.set(rank2);
	return CHARACTER_RANK::relation(rank_from.index(), rank_to.index());
}

CHARACTER_GOODWILL RELATION_REGISTRY::GetReputationRelation(CHARACTER_REPUTATION_VALUE rep1,
                                                            CHARACTER_REPUTATION_VALUE rep2) const
{
	CHARACTER_REPUTATION rep_from, rep_to;
	rep_from.set(rep1);
	rep_to.set(rep2);
	return CHARACTER_REPUTATION::relation(rep_from.index(), rep_to.index());
}

//////////////////////////////////////////////////////////////////////////

void RELATION_REGISTRY::SetCommunityRelation(CHARACTER_COMMUNITY_INDEX index1, CHARACTER_COMMUNITY_INDEX index2,
                                             CHARACTER_GOODWILL goodwill)
{
	CHARACTER_COMMUNITY::set_relation(index1, index2, goodwill);
}

//////////////////////////////////////////////////////////////////////////
// MP fork (§14 step 8 phase 4 R2 / doc §8.1): the routing seam and the faction
// tier's persistence. See relation_registry.h for why these two live together
// and why the personal tier needs neither.
//////////////////////////////////////////////////////////////////////////

namespace
{
	u32 s_rep_routed  = 0;
	u32 s_rep_routed_zero = 0;   // of those, the hardcoded-0 shape
	u32 s_rep_refused = 0;
	u32 s_rep_refuse_logged = 0;

	const u16 coop_rep_state_version = 1;

	// The ltx baseline, read straight out of pSettings rather than off the live table — which is
	// the whole point: by the time a save is taken the live table may have MOVED, and the file
	// is meant to record exactly that difference. Built once; config does not change under us.
	//
	// Storing the delta rather than the whole table is a decision, not an optimisation: a
	// relation nobody touched keeps taking its value from config, so a later config patch still
	// reaches it, while a relation the world moved stays moved. A full-table dump would freeze
	// every faction pair at whatever the config said the day the save was written.
	struct rep_baseline
	{
		xr_vector<xr_vector<int> > t;
		bool built;
		bool ok;

		rep_baseline(): built(false), ok(false) {}
	};

	rep_baseline& baseline()
	{
		static rep_baseline b;
		if (b.built)
			return b;
		b.built = true;

		const int n = int(CHARACTER_COMMUNITY::GetMaxIndex()) + 1;
		if (n <= 0 || !pSettings->section_exist("communities_relations"))
		{
			Msg("! COOP(rep): no [communities_relations] baseline (%d communities) — faction "
				"persistence is OFF for this session; a saved table could not be told from config",
				n);
			return b;
		}

		b.t.resize(n);
		for (int i = 0; i < n; ++i)
			b.t[i].assign(size_t(n), 0);

		CInifile::Sect& sect = pSettings->r_section("communities_relations");
		u32 rows = 0;
		for (CInifile::SectCIt i = sect.Data.begin(); sect.Data.end() != i; ++i)
		{
			const CHARACTER_COMMUNITY_INDEX from =
				CHARACTER_COMMUNITY::IdToIndex((*i).first, CHARACTER_COMMUNITY_INDEX(-1), true);
			if (from < 0 || from >= n)
				continue;                       // a row naming a community that is not in the list
			string64 buffer;
			for (int j = 0; j < n; ++j)
				b.t[from][j] = atoi(_GetItem(*(*i).second, j, buffer));
			++rows;
		}
		b.ok = (rows > 0);
		if (!b.ok)
			Msg("! COOP(rep): [communities_relations] resolved 0 usable rows — faction persistence OFF");
		return b;
	}
}

u32 coop_rep_routed_count()  { return s_rep_routed; }
u32 coop_rep_routed_zero_count() { return s_rep_routed_zero; }
u32 coop_rep_refused_count() { return s_rep_refused; }

u16 coop_rep_subject(int passed_id, bool is_write)
{
	// A caller that NAMED a subject keeps it. An NPC's id is a perfectly good subject and
	// hijacking it would be a far worse bug than the one this seam fixes — the routing exists
	// only for the call sites that had no subject to name.
	if (passed_id != COOP_REP_ACTING)
	{
		// ...with ONE exception, and it is measured rather than assumed. A survey of the goodwill
		// call sites in the deployed gamedata found three ways stock code names "the actor" and
		// only one of them is a name: `AC_ID` (7 sites, and undefined in this build's script set,
		// so it arrives as nil and the Lua wrapper turns it into the sentinel), `db.actor:id()`
		// (3 sites, which raise before they ever reach here), and a HARDCODED 0 — the id the
		// single-player actor always had. On this server entity 0 is the loopback self-client's
		// fake host actor, so a literal 0 asks about a body no player owns, and every player
		// shares the answer. Inside an acting scope that is unambiguously "the actor", so it is
		// routed; with no acting player it is passed through exactly as before, because then
		// there is no better answer and a refusal would change stock behaviour for nothing.
		if (passed_id == 0 && xr_enet::enabled())
		{
			const u16 acting_for_zero = mp_coop_owner::acting_actor();
			if (acting_for_zero != mp_coop_owner::none)
			{
				++s_rep_routed;
				++s_rep_routed_zero;
				return acting_for_zero;
			}
		}
		if (passed_id < 0 || passed_id > int(u16(-1)))
			return mp_coop_owner::none;
		return u16(passed_id);
	}

	const u16 acting = mp_coop_owner::acting_actor();
	if (acting != mp_coop_owner::none)
	{
		++s_rep_routed;
		return acting;
	}

	// No acting player. This is autonomous world simulation, and personal standing has no
	// subject here — so the call is dropped. It is NOT redirected to the world tier: doc §8.3's
	// worst outcome is one player's actions landing on everyone, and a world-tier row read back
	// through GetCommunityGoodwill would do exactly that, silently and forever.
	++s_rep_refused;
	if (s_rep_refuse_logged < 8)
	{
		++s_rep_refuse_logged;
		Msg("~ COOP(rep): %s with no acting player and no named subject — REFUSED (not routed to "
			"the world tier). refusals so far=%u%s", is_write ? "goodwill write" : "goodwill read",
			s_rep_refused, (s_rep_refuse_logged == 8) ? "  [further refusals not logged]" : "");
	}
	return mp_coop_owner::none;
}

void coop_rep_state_save(IWriter& stream)
{
	stream.open_chunk(COOP_REP_CHUNK_DATA);

	// The harness needs the "version we do not know" path MEASURED rather than argued: Q2 and Q3
	// both shipped that refusal reasoned-about only. -coop_test_rep2_badver writes a version from
	// the future so the next boot has to refuse it.
	const bool bad_version = (strstr(Core.Params, "-coop_test_rep2_badver") != NULL);
	stream.w_u16(bad_version ? u16(0xFFFF) : coop_rep_state_version);

	rep_baseline& b = baseline();
	const int n = b.ok ? int(b.t.size()) : 0;

	// Two passes: the count has to precede the records, and only the cells that MOVED are cells.
	u32 moved = 0;
	int from, to;
	for (from = 0; from < n; ++from)
		for (to = 0; to < n; ++to)
			if (CHARACTER_COMMUNITY::relation(from, to) != b.t[from][to])
				++moved;

	stream.w_u32(moved);
	for (from = 0; from < n; ++from)
	{
		for (to = 0; to < n; ++to)
		{
			const CHARACTER_GOODWILL live = CHARACTER_COMMUNITY::relation(from, to);
			if (live == b.t[from][to])
				continue;
			// Names, not indices. An index is a position in a config line, so a community added
			// to or removed from [game_relations] communities would silently re-point every
			// stored cell at a different faction — the one corruption in this chunk that would
			// look like a plausible world rather than a broken file.
			stream.w_stringZ(CHARACTER_COMMUNITY::IndexToId(from, NULL, true));
			stream.w_stringZ(CHARACTER_COMMUNITY::IndexToId(to, NULL, true));
			stream.w_u32(u32(live));
		}
	}

	stream.close_chunk();

	Msg("- COOP(rep): faction state saved  moved=%u of %dx%d (v%u)%s", moved, n, n,
		u32(bad_version ? 0xFFFF : coop_rep_state_version),
		bad_version ? "   !! -coop_test_rep2_badver: version deliberately unreadable" : "");
}

void coop_rep_state_load(IReader& stream)
{
	// Clear FIRST and unconditionally — the table is static and outlives restart_simulator, so a
	// load that finds nothing must leave the server holding the CONFIG baseline and not the last
	// world's faction war. (This is also what makes "no chunk" a correct, complete outcome.)
	CHARACTER_COMMUNITY::coop_reset_relations();

	// find_chunk REWINDS and scans, so put the cursor back: the reads that precede us must not
	// be able to notice that we ran.
	const int caller_pos = stream.tell();

	const u32 chunk_size = stream.find_chunk(COOP_REP_CHUNK_DATA);
	if (!chunk_size)
	{
		stream.seek(caller_pos);
		Msg("- COOP(rep): no faction chunk in this save (stock .scop, or written before R2) — "
			"faction relations start at their config baseline");
		return;
	}

	coop_chunk_reader r(stream, stream.tell() + int(chunk_size));

	const u16 ver = r.u16v();
	if (!r.ok || ver != coop_rep_state_version)
	{
		stream.seek(caller_pos);
		Msg("! COOP(rep): faction state version %u is not %u — REFUSING to parse it; faction "
			"relations start at their config baseline", u32(ver), u32(coop_rep_state_version));
		FlushLog();
		return;
	}

	// Applied into a staging list first: a truncated chunk must not leave half a faction war
	// standing, and the table cannot be rolled back once written.
	struct pending_cell { CHARACTER_COMMUNITY_INDEX from, to; CHARACTER_GOODWILL goodwill; };
	xr_vector<pending_cell> pending;

	u32 unknown = 0;
	const u32 cells = r.count(/*min bytes per cell: NUL + NUL + s32*/ 6);
	for (u32 i = 0; i < cells && r.ok; ++i)
	{
		shared_str from_name, to_name;
		if (!r.str(from_name) || !r.str(to_name))
			break;
		const s32 goodwill = r.s32v();
		if (!r.ok)
			break;

		const CHARACTER_COMMUNITY_INDEX from =
			CHARACTER_COMMUNITY::IdToIndex(from_name, CHARACTER_COMMUNITY_INDEX(-1), true);
		const CHARACTER_COMMUNITY_INDEX to =
			CHARACTER_COMMUNITY::IdToIndex(to_name, CHARACTER_COMMUNITY_INDEX(-1), true);
		if (from < 0 || to < 0)
		{
			// A faction the config no longer has. Not a corruption — a content change — so it is
			// dropped and COUNTED, never dropped quietly.
			++unknown;
			continue;
		}
		if (goodwill == NO_GOODWILL)
		{
			// The one value the table treats as "unset". It cannot be produced by a legitimate
			// write (set_relation asserts against it), so a chunk carrying one is not a chunk we
			// wrote — count it with the other unusable cells rather than pushing it through.
			++unknown;
			continue;
		}
		pending_cell c;
		c.from = from;
		c.to = to;
		c.goodwill = CHARACTER_GOODWILL(goodwill);
		pending.push_back(c);
	}

	stream.seek(caller_pos);

	if (!r.ok)
	{
		Msg("! COOP(rep): faction state chunk is truncated or malformed (%u bytes) — DISCARDED "
			"WHOLE, faction relations start at their config baseline", chunk_size);
		FlushLog();
		return;
	}

	for (size_t i = 0; i < pending.size(); ++i)
		CHARACTER_COMMUNITY::set_relation(pending[i].from, pending[i].to, pending[i].goodwill);

	Msg("- COOP(rep): faction state loaded moved=%u applied=%u unknown_community=%u (v%u)",
		cells, u32(pending.size()), unknown, u32(ver));
	FlushLog();
}
