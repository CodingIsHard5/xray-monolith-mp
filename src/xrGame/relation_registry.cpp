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

// MP fork (§14 step 8 phase 4 R3.1): the blast radius needs the connected players — who they
// are, where they are, and which faction they picked. Level().Server is the only server-side
// source for the first two, and a player's CSE is the only LIVE source for the position.
#include "Level.h"
#include "xrServer.h"


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

	// MP fork (§14 step 8 P4 R3.1 run 1): NAME THE WRITER.
	//
	// R3.0 measured a kill moving the killer's community-goodwill row by -140 and R3.1 was built
	// on the assumption that RELATION_REGISTRY::Action wrote it. It cannot have: that write is
	// `Sympathy() * community_member_kill_goodwill`, Sympathy comes from [communities_sympathy],
	// and in this GAMMA config EVERY community's sympathy is 0.0 — so the stock write is
	// multiplied to zero and guarded out by `if (community_goodwill)`. R3.1 run 1 measured the
	// consequence directly: the value reaching the propagation was 0 while the row still moved
	// -140. Something else writes this row, and every write to it lands HERE, so this is where
	// the question gets answered rather than argued.
	//
	// Rate-limited and co-op only, like the Action tracer beside it: this is on the path of every
	// goodwill change in the game.
	if (xr_enet::enabled())
	{
		static u32 s_gw_lines = 0;
		if (s_gw_lines < 40)
		{
			++s_gw_lines;
			const shared_str cname = (from_community >= 0)
				? CHARACTER_COMMUNITY::IndexToId(from_community, NULL, true) : shared_str("<none>");
			Msg("~ COOP(rep3w): SetCommunityGoodwill community=%s(%d) character=%u <- %d  frame=%u%s",
				cname.c_str() ? cname.c_str() : "<none>", int(from_community), u32(to_character),
				int(goodwill), Device.dwFrame,
				(s_gw_lines == 40) ? "   [further goodwill writes suppressed]" : "");
		}
	}
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

	// WHAT THE SAVE SAID THE WORLD MOVED, kept after it has been applied.
	//
	// R2 run 1 failed here, and the failure is worth the space: the loader reported applied=2 and
	// the relation still read its config value afterwards. Both were true. `CLevel::Load_GameSpecific_Before`
	// calls CHARACTER_COMMUNITY::Reset() (Level_load.cpp) AFTER the .scop is read, which drops the
	// table, and the next read rebuilds it from ltx — so anything written into it during the alife
	// load is discarded by the level load that follows.
	//
	// That reset is not a bug to route around; it is the stock invariant that CONFIG owns this
	// table at every level load, and it is precisely why faction relations never persisted (R1).
	// So the saved state is an OVERLAY on top of config rather than a one-shot write: it is kept
	// here and re-applied every time the table is reset. That also buys the case a one-shot write
	// would have silently lost — a faction war surviving a LEVEL CHANGE, not just a restart.
	//
	// Stored by NAME, like the file, so the overlay cannot be re-pointed at a different faction by
	// anything that renumbers communities between a load and a reset.
	struct rep_cell
	{
		shared_str from, to;
		s32        goodwill;
	};
	xr_vector<rep_cell> s_rep_overlay;

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

u32 coop_rep_apply_overlay(LPCSTR why)
{
	if (s_rep_overlay.empty())
		return 0;

	u32 applied = 0, unknown = 0;
	for (size_t i = 0; i < s_rep_overlay.size(); ++i)
	{
		const CHARACTER_COMMUNITY_INDEX from =
			CHARACTER_COMMUNITY::IdToIndex(s_rep_overlay[i].from, CHARACTER_COMMUNITY_INDEX(-1), true);
		const CHARACTER_COMMUNITY_INDEX to =
			CHARACTER_COMMUNITY::IdToIndex(s_rep_overlay[i].to, CHARACTER_COMMUNITY_INDEX(-1), true);
		if (from < 0 || to < 0)
		{
			++unknown;
			continue;
		}
		CHARACTER_COMMUNITY::set_relation(from, to, CHARACTER_GOODWILL(s_rep_overlay[i].goodwill));
		++applied;
	}

	// Says WHEN as well as how many, because the whole defect this exists for was an apply that
	// happened at the wrong moment and reported success. A run where the level-load line never
	// appears is a run where the overlay is not surviving the reset, and the log will show it.
	Msg("- COOP(rep): faction overlay applied at '%s': %u cell(s), %u unknown community",
		why, applied, unknown);
	return applied;
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
	// Clear FIRST and unconditionally — BOTH the table and the overlay. They are static and
	// outlive restart_simulator, so a load that finds nothing must leave the server holding the
	// CONFIG baseline and not the last world's faction war. Clearing the overlay matters more
	// than clearing the table, because the overlay is what the level-load reset re-applies: an
	// overlay left standing would keep re-imposing world A's war on world B forever.
	// (This is also what makes "no chunk" a correct, complete outcome.)
	s_rep_overlay.clear();
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

	// Read into a staging list first: a truncated chunk must not leave half a faction war
	// standing, and the table cannot be rolled back once written.
	xr_vector<rep_cell> pending;

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
		rep_cell c;
		c.from = from_name;
		c.to = to_name;
		c.goodwill = goodwill;
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

	s_rep_overlay.swap(pending);
	const u32 applied = coop_rep_apply_overlay("load");

	Msg("- COOP(rep): faction state loaded moved=%u applied=%u unknown_community=%u (v%u)",
		cells, applied, unknown, u32(ver));
	FlushLog();
}

////////////////////////////////////////////////////////////////////////////////////////////////
// MP fork (§14 step 8 PHASE 4 increment R3.1, dev/RPG_LAYER_PLAN.md, doc §8.2/§8.3):
// THE BLAST RADIUS OF A KILL — the two tiers stock does not have.
//
// The header carries the design; this carries the decisions that only exist in code.
////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	// ---- R3.1 tunables (doc Appendix B) --------------------------------------------------
	//
	// The radius is a "you were standing right there" distance, not a line of sight: a witness
	// model belongs to a later increment and would make the co-op feel-bad depend on whether the
	// server happened to have the NPC's vision ready.
	const float COOP_REP_BYSTANDER_RADIUS_M = 30.f;

	// The collective hit is SMALL by construction rather than by taste: it is the shooter's own
	// stock magnitude divided down, so it tracks config instead of sitting beside it. With the
	// measured -140 that is -7 per kill against a table whose hostile pole is -2000 — i.e. a
	// griefer needs a loop, which is exactly the loop §8.3's pressure bar (R3.2) has to stop.
	const s32 COOP_REP_COLLECTIVE_DIVISOR = 20;

	// The config's own extremes (`monolith -> monolith = 2000`, `-2000` for the hostile pole).
	// Clamping there means a relation already at war absorbs further kills without moving, which
	// is the correct answer rather than a saturating counter nobody can read.
	const s32 COOP_REP_RELATION_FLOOR = -2000;
	const s32 COOP_REP_RELATION_CEIL  =  2000;

	u32 s_bystanders_considered = 0;
	u32 s_bystanders_moved      = 0;
	u32 s_faction_moved         = 0;
	u32 s_faction_refused       = 0;

	// The synthetic stand-in for a second player. See the header: it is injected into the SAME
	// enumeration real players go through, so what it measures is the production path.
	bool                      s_test_bys_armed = false;
	u16                       s_test_bys_id    = u16(-1);
	Fvector                   s_test_bys_pos   = { 0.f, 0.f, 0.f };
	CHARACTER_COMMUNITY_INDEX s_test_bys_comm  = CHARACTER_COMMUNITY_INDEX(-1);

	// `actor` is the pseudo-community EVERY player shares, so it is not a faction and must never
	// be an endpoint of a faction<->faction move. Decided by NAME, like R2's chunk and for the
	// same reason: an index is a position in a config line, and a community added to
	// [game_relations] communities would silently re-point this test at a different faction.
	bool coop_rep_is_faction(CHARACTER_COMMUNITY_INDEX c)
	{
		if (c < 0)
			return false;
		const shared_str name = CHARACTER_COMMUNITY::IndexToId(c, NULL, true);
		if (!name.c_str() || !name.size())
			return false;
		return (xr_strcmp(name.c_str(), "actor") != 0);
	}

	// Returns by VALUE, and callers hold the result in a local before taking c_str(): IndexToId
	// hands back a shared_str by value, so a `LPCSTR name = IndexToId(...)` would point into a
	// temporary that is gone by the time Msg formats it.
	shared_str coop_rep_comm_name(CHARACTER_COMMUNITY_INDEX c)
	{
		if (c < 0)
			return shared_str("<none>");
		const shared_str name = CHARACTER_COMMUNITY::IndexToId(c, NULL, true);
		return (name.c_str() && name.size()) ? name : shared_str("<unknown>");
	}

	// One candidate for the bystander tier: WHO, WHERE, and WHICH FACTION. Collected in one pass
	// over the connected clients, which is also the pass that answers "is the killer a player at
	// all" — the gate on both new tiers.
	struct coop_rep_bystander
	{
		u16                       id;
		Fvector                   pos;
		float                     dist;   // resolved AT the kill; see coop_rep_propagate_kill
		CHARACTER_COMMUNITY_INDEX comm;
		bool                      synthetic;
	};

	struct coop_rep_bystander_collector
	{
		u16 killer_id;
		bool killer_is_player;
		u32 clients_seen;
		xr_vector<coop_rep_bystander>* out;

		void operator()(IClient* client)
		{
			xrClientData* const cd = static_cast<xrClientData*>(client);
			if (!cd || !cd->owner)
				return;
			if (Level().Server && cd == Level().Server->GetServerClient())
				return;   // the dedicated server's own loopback client is not a player
			++clients_seen;

			if (cd->owner->ID == killer_id)
			{
				killer_is_player = true;
				return;       // the shooter is not their own bystander
			}

			// WHERE, and this is the P4 E hazard named where it bites. A player's CSE
			// `o_Position` is written by the ENet PUMP THREAD peeking M_CL_UPDATE, not by the
			// game loop — the server never decodes a player's movement and its own copy of the
			// body is a record, not a simulation (D2 run 7), so the CSE is the only live source
			// and the CObject transform would be stale by the whole length of a walk.
			//
			// SAMPLING POINT, chosen deliberately: HERE, synchronously, on the death path — i.e.
			// at the kill. The alternative (sample on a later Update tick) is the D1/E defect
			// exactly: it would score the bystander at wherever they had walked to by then, which
			// is unbounded. Reading at the kill bounds the error to one client-update interval.
			// The read is NOT atomic against the pump thread's three-float assign; `_valid()` is
			// the cheap guard, and the sampled position is LOGGED, so a torn or absurd coordinate
			// shows up as a nonsense distance in the record rather than as a silently wrong scale.
			if (!_valid(cd->owner->o_Position))
				return;

			coop_rep_bystander b;
			b.id        = cd->owner->ID;
			b.pos       = cd->owner->o_Position;
			b.dist      = 0.f;
			b.comm      = CHARACTER_COMMUNITY_INDEX(-1);
			b.synthetic = false;

			CSE_ALifeTraderAbstract* const trader = smart_cast<CSE_ALifeTraderAbstract*>(cd->owner);
			if (trader)
				b.comm = CHARACTER_COMMUNITY_INDEX(trader->m_community_index);

			out->push_back(b);
		}
	};
}

float coop_rep_bystander_radius() { return COOP_REP_BYSTANDER_RADIUS_M; }

// Linear falloff: full weight at the muzzle, EXACTLY zero at and beyond the radius. The equality
// matters — "outside the radius moves nothing" is this increment's negative gate, and a curve that
// merely tends to zero (an inverse square, say) would leave a rounding-sized hit at every distance
// and pass a test that only checked the shooter.
float coop_rep_bystander_scale(float dist_m)
{
	if (!_valid(dist_m) || dist_m < 0.f)
		return 0.f;
	if (dist_m >= COOP_REP_BYSTANDER_RADIUS_M)
		return 0.f;
	return 1.f - (dist_m / COOP_REP_BYSTANDER_RADIUS_M);
}

u32 coop_rep_bystanders_considered()  { return s_bystanders_considered; }
u32 coop_rep_bystanders_moved()       { return s_bystanders_moved; }
u32 coop_rep_faction_moved_count()    { return s_faction_moved; }
u32 coop_rep_faction_refused_count()  { return s_faction_refused; }

void coop_rep_test_set_bystander(bool armed, u16 id, const Fvector& pos,
                                 CHARACTER_COMMUNITY_INDEX comm)
{
	s_test_bys_armed = armed;
	s_test_bys_id    = id;
	s_test_bys_pos   = pos;
	s_test_bys_comm  = comm;
}

bool coop_rep_faction_move(CHARACTER_COMMUNITY_INDEX from, CHARACTER_COMMUNITY_INDEX to,
                           s32 delta, LPCSTR why)
{
	if (!delta || from < 0 || to < 0)
		return false;

	const s32 cur = s32(CHARACTER_COMMUNITY::relation(from, to));
	s32 next = cur + delta;
	if (next < COOP_REP_RELATION_FLOOR) next = COOP_REP_RELATION_FLOOR;
	if (next > COOP_REP_RELATION_CEIL)  next = COOP_REP_RELATION_CEIL;
	if (next == cur)
		return false;                       // already at the pole: absorbed, not accumulated
	if (next == NO_GOODWILL)
		return false;                       // the table's "unset" sentinel is not a value

	CHARACTER_COMMUNITY::set_relation(from, to, CHARACTER_GOODWILL(next));

	// ...and into the OVERLAY, which is the same and only representation R2 persists and R3.2
	// decays. Keeping it here rather than in a parallel accumulator is the whole reason R3
	// inherits R2's storage: there is nothing to keep in step, and "decay toward baseline" is
	// literally "shrink this number toward its config value".
	const shared_str from_name = CHARACTER_COMMUNITY::IndexToId(from, NULL, true);
	const shared_str to_name   = CHARACTER_COMMUNITY::IndexToId(to,   NULL, true);

	// A cell that has come back to its config value is not an overlay cell — it is the absence of
	// one. Dropping it keeps the overlay from growing forever and makes "the war is over" a real
	// state rather than a stored zero difference.
	rep_baseline& b = baseline();
	const bool at_baseline = b.ok
		&& from < s32(b.t.size()) && to < s32(b.t.size())
		&& next == b.t[from][to];

	size_t i = 0;
	for (; i < s_rep_overlay.size(); ++i)
		if (s_rep_overlay[i].from == from_name && s_rep_overlay[i].to == to_name)
			break;

	if (at_baseline)
	{
		if (i < s_rep_overlay.size())
			s_rep_overlay.erase(s_rep_overlay.begin() + i);
	}
	else if (i < s_rep_overlay.size())
	{
		s_rep_overlay[i].goodwill = next;
	}
	else
	{
		rep_cell c;
		c.from = from_name;
		c.to = to_name;
		c.goodwill = next;
		s_rep_overlay.push_back(c);
	}

	++s_faction_moved;
	Msg("- COOP(rep3): faction %s -> %s  %d -> %d (delta %+d, %s); overlay now %u cell(s)",
		from_name.c_str(), to_name.c_str(), cur, next, delta, why,
		u32(s_rep_overlay.size()));
	return true;
}

// The pending half of a kill: everything sampled AT the kill, waiting for the magnitude that
// only exists after it. See the header for why the two sampling points differ.
namespace
{
	struct coop_rep_pending_kill
	{
		u16                            killer_id;
		CHARACTER_COMMUNITY_INDEX      killer_comm;
		u16                            victim_id;
		CHARACTER_COMMUNITY_INDEX      victim_comm;
		s32                            shooter_row_at_kill;
		u32                            due_ms;
		xr_vector<coop_rep_bystander>  cands;
	};
	xr_vector<coop_rep_pending_kill> s_pending;

	// How long to wait for the shooter hit to land. Run 2 measured it arriving in the SAME frame
	// as the death, so this is slack rather than a guess — but it is slack on a quantity nobody
	// owns, so it is generous and the actual wait is reported with the result.
	const u32 COOP_REP_SETTLE_MS = 1000;
}

void coop_rep_propagate_kill(u16 killer_id, CHARACTER_COMMUNITY_INDEX killer_comm,
                             u16 victim_id, CHARACTER_COMMUNITY_INDEX victim_comm,
                             const Fvector& kill_pos)
{
	if (!xr_enet::enabled() || !Level().Server)
		return;
	if (victim_comm < 0 || !_valid(kill_pos))
		return;

	// One pass over the connected clients: it collects the bystander candidates AND answers
	// whether the killer is a player, which is the gate on both tiers below.
	xr_vector<coop_rep_bystander> cands;
	coop_rep_bystander_collector col;
	col.killer_id = killer_id;
	col.killer_is_player = false;
	col.clients_seen = 0;
	col.out = &cands;
	Level().Server->ForEachClientDo(col);

	if (!col.killer_is_player)
	{
		// A-Life killing its own is not a player's blast radius. Silent by design: this is the
		// common case on a living server and a log line here would be the whole log.
		return;
	}

	if (s_test_bys_armed)
	{
		// The construction, injected into the real list so it goes through the real tests.
		coop_rep_bystander b;
		b.id        = s_test_bys_id;
		b.pos       = s_test_bys_pos;
		b.dist      = 0.f;
		b.comm      = s_test_bys_comm;
		b.synthetic = true;
		cands.push_back(b);
	}

	// DISTANCE IS RESOLVED NOW, not later. This is the whole reason positions are sampled here:
	// once the distance is a number, the pump thread can move every player as much as it likes
	// and the blast radius still describes the moment of the kill.
	for (size_t i = 0; i < cands.size(); ++i)
		cands[i].dist = kill_pos.distance_to(cands[i].pos);

	coop_rep_pending_kill pk;
	pk.killer_id           = killer_id;
	pk.killer_comm         = killer_comm;
	pk.victim_id           = victim_id;
	pk.victim_comm         = victim_comm;
	pk.shooter_row_at_kill = RELATION_REGISTRY().GetCommunityGoodwill(victim_comm, killer_id);
	pk.due_ms              = Device.dwTimeGlobal + COOP_REP_SETTLE_MS;
	pk.cands.swap(cands);
	s_pending.push_back(pk);
}

void coop_rep_propagate_tick()
{
	if (s_pending.empty())
		return;

	for (size_t k = 0; k < s_pending.size(); )
	{
		coop_rep_pending_kill& pk = s_pending[k];
		if (s32(Device.dwTimeGlobal - pk.due_ms) < 0)
		{
			++k;
			continue;
		}

		// THE MAGNITUDE, MEASURED. Whatever moved the shooter's row — stock Action, a config the
		// engine does not read, or a script — this is what it did, and the bystander term is a
		// scaled copy of exactly that. A zero here is not a failure to propagate: it is the
		// truthful statement that the shooter took nothing, and then neither does anyone else.
		const s32 now = RELATION_REGISTRY().GetCommunityGoodwill(pk.victim_comm, pk.killer_id);
		const s32 shooter_delta = now - pk.shooter_row_at_kill;

		const shared_str k_comm_name = coop_rep_comm_name(pk.killer_comm);
		const shared_str v_comm_name = coop_rep_comm_name(pk.victim_comm);
		Msg("- COOP(rep3): kill blast radius: killer=%u(%s) victim=%u(%s) shooter row %d -> %d "
			"(MEASURED delta %+d) candidates=%u",
			u32(pk.killer_id), k_comm_name.c_str(), u32(pk.victim_id), v_comm_name.c_str(),
			pk.shooter_row_at_kill, now, shooter_delta, u32(pk.cands.size()));

		// ---- tier 2: the bystanders ---------------------------------------------------------
		//
		// The shooter is NOT here. Their row is what everything below is measured FROM, so
		// touching it would both double the hit and corrupt the next kill's baseline.
		for (size_t i = 0; i < pk.cands.size(); ++i)
		{
			const coop_rep_bystander& b = pk.cands[i];
			++s_bystanders_considered;

			const float scale = coop_rep_bystander_scale(b.dist);
			// doc §8.2's "same-faction bystanders": the blast radius is guilt by association with
			// the SHOOTER, so a player of another faction standing nearby is a witness, not an
			// accomplice. On this build every player is the pseudo-community `actor`, so the test
			// is presently vacuous — which is why both communities are printed, not a verdict.
			const bool same_faction = (b.comm >= 0) && (b.comm == pk.killer_comm);
			// Rounded AWAY from zero, so a bystander inside the radius never quietly rounds to a
			// no-op: the only zero this tier produces should be the one the radius test produced.
			// Done on the MAGNITUDE and re-signed, because the obvious one-liner is not symmetric
			// — `iFloor(raw + 0.5f)` on an exact -70.0 gives -71, which is how an off-by-one
			// hides inside a line everyone reads as "rounding".
			const float raw = float(shooter_delta) * scale;
			const s32 mag = iFloor(_abs(raw) + 0.5f);
			const s32 delta = (scale > 0.f && same_faction) ? ((raw < 0.f) ? -mag : mag) : 0;

			const shared_str b_comm_name = coop_rep_comm_name(b.comm);
			Msg("- COOP(rep3): bystander%s id=%u at %.1f m (r=%.0f) scale=%.3f "
				"comm=%s vs killer_comm=%s same_faction=%d shooter=%+d -> delta %+d%s",
				b.synthetic ? "[SYNTHETIC, no second live client]" : "", u32(b.id), b.dist,
				COOP_REP_BYSTANDER_RADIUS_M, scale, b_comm_name.c_str(),
				k_comm_name.c_str(), same_faction ? 1 : 0, shooter_delta, delta,
				(scale <= 0.f) ? "   [outside the radius: exactly zero]" : "");

			if (!delta)
				continue;
			RELATION_REGISTRY().ChangeCommunityGoodwill(pk.victim_comm, b.id,
			                                            CHARACTER_GOODWILL(delta));
			++s_bystanders_moved;
		}

		// ---- tier 3: the collective faction hit ---------------------------------------------
		//
		// R3.0's finding, enforced: the killer's community is `actor` for every player who has
		// not chosen a faction, and `actor` is not a faction — so there is no faction<->faction
		// pair to move and the write is REFUSED rather than aimed at the nearest available cell.
		// A refusal here is not a failure; it is the only correct answer to "which faction did
		// this, on whose behalf" when the answer is "none".
		if (!coop_rep_is_faction(pk.killer_comm))
		{
			++s_faction_refused;
			Msg("- COOP(rep3): collective hit REFUSED — killer's community '%s' is not a faction "
				"(every player shares it, so this write would move %s -> %s and make one player's "
				"kill hostile for everybody). victim=%u killer=%u; refusals=%u",
				k_comm_name.c_str(), v_comm_name.c_str(), k_comm_name.c_str(),
				u32(pk.victim_id), u32(pk.killer_id), s_faction_refused);
		}
		else if (!coop_rep_is_faction(pk.victim_comm) || pk.killer_comm == pk.victim_comm)
		{
			++s_faction_refused;   // no pair, or a faction cannot go to war with itself
		}
		else
		{
			// The wronged faction's opinion of the killer's faction. One direction on purpose:
			// killing a Duty man does not make Duty's enemies think better of anyone, and a
			// symmetric write would quietly double the reach of every kill.
			const s32 collective = shooter_delta / COOP_REP_COLLECTIVE_DIVISOR;
			if (!collective)
				Msg("- COOP(rep3): collective hit is ZERO — the shooter took %+d, and %+d/%d "
					"rounds to nothing. Nothing is written; a faction war is not started by a "
					"rounding error.", shooter_delta, shooter_delta, COOP_REP_COLLECTIVE_DIVISOR);
			else
				coop_rep_faction_move(pk.victim_comm, pk.killer_comm, collective, "kill");
		}

		s_pending.erase(s_pending.begin() + k);
	}
}
