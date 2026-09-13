////////////////////////////////////////////////////////////////////////////
//  mp_coop_ff.cpp — design doc §12: friendly-fire / PvP modes, the engine half (see mp_coop_ff.h).
//
//  §12.2 hierarchy: a GLOBAL DEFAULT that is server config (-coop_ff <mode>, default UNIVERSAL_ON, which is
//  what the server did before §12 existed) and PER-ZONE OVERRIDES keyed by level name that are WORLD STATE:
//  set at runtime (game.mp_coop_ff_set) and persisted in their own .scop chunk, so they survive a restart.
//  §12.3 resolution runs on the server in the GE_HIT handler; a client never decides whether damage applies.
////////////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include "mp_coop_ff.h"
#include "mp_coop_chunk_reader.h"
#include "alife_space.h"                          // COOP_FF_CHUNK_DATA
#include "relation_registry.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "../xrNetServer/xr_enet_transport.h"

namespace
{
typedef xr_map<shared_str, u8> zone_map;
zone_map& zones()
{
	static zone_map s;
	return s;
}

const u16 coop_ff_state_version = 1;

// A token that must be followed by a space or the end: -coop_ff must not match inside -coop_ff_zone.
LPCSTR find_flag(LPCSTR flag)
{
	const size_t n = xr_strlen(flag);
	for (LPCSTR p = strstr(Core.Params, flag); p; p = strstr(p + 1, flag))
		if (p[n] == ' ' || p[n] == 0)
			return p + n;
	return NULL;
}

// copy one space-delimited argument after a flag
bool flag_arg(LPCSTR flag, char* out, size_t cap)
{
	LPCSTR p = find_flag(flag);
	if (!p)
		return false;
	while (*p == ' ')
		++p;
	size_t i = 0;
	while (*p && *p != ' ' && i + 1 < cap)
		out[i++] = *p++;
	out[i] = 0;
	return i > 0;
}

u32 s_logged = 0;
bool log_budget() { return (++s_logged <= 50) || ((s_logged % 50) == 1); }
}

int coop_ff_global_default()
{
	static int s_mode = -1;
	if (s_mode < 0)
	{
		char buf[64];
		s_mode = eCoopFF_UniversalOn;
		if (flag_arg("-coop_ff", buf, sizeof buf))
		{
			const int m = coop_ff_parse(buf);
			if (m == eCoopFF_Invalid)
				Msg("! COOP(ff): -coop_ff '%s' is not a mode (UNIVERSAL_ON, UNIVERSAL_OFF, FRIENDLY_FACTIONS, "
					"OWN_FACTION_ONLY) — the global default stays UNIVERSAL_ON", buf);
			else
				s_mode = m;
		}
		Msg("- COOP(ff): global default %s%s", coop_ff_name(s_mode),
			find_flag("-coop_ff") ? " (from -coop_ff)" : " (built-in default)");
	}
	return s_mode;
}

int coop_ff_mode_for(const char* level_name)
{
	if (level_name && *level_name)
	{
		zone_map::const_iterator it = zones().find(shared_str(level_name));
		if (it != zones().end())
			return it->second;
	}
	return coop_ff_global_default();
}

bool coop_ff_set_zone(const char* level_name, int mode)
{
	if (!level_name || !*level_name)
	{
		Msg("! COOP(ff): a zone override needs a level name; the global default is server config (-coop_ff)");
		return false;
	}
	if (mode == eCoopFF_Invalid)
	{
		const bool had = zones().erase(shared_str(level_name)) > 0;
		Msg("- COOP(ff): zone '%s' override %s — it now inherits the global default %s", level_name,
			had ? "removed" : "was not set", coop_ff_name(coop_ff_global_default()));
		return true;
	}
	if (mode < 0 || mode >= eCoopFF_Count)
		return false;
	zones()[shared_str(level_name)] = u8(mode);
	Msg("- COOP(ff): zone '%s' override set to %s (world state; persisted by the next save)", level_name,
		coop_ff_name(mode));
	return true;
}

bool coop_ff_refuse_hit(CSE_Abstract* shooter, CSE_Abstract* victim, const char* level_name)
{
	if (!xr_enet::enabled() || !shooter || !victim || shooter == victim)
		return false;
	// §12 is about PLAYERS. Only a player body shooting another player body is ever filtered. The caller
	// has already excluded the host's save actor (not a player) and orphans (§27 refuses those itself).
	CSE_ALifeCreatureActor* const a = smart_cast<CSE_ALifeCreatureActor*>(shooter);
	CSE_ALifeCreatureActor* const b = smart_cast<CSE_ALifeCreatureActor*>(victim);
	if (!a || !b)
		return false;

	const int mode = coop_ff_mode_for(level_name);
	const bool same = (a->m_community_index == b->m_community_index);
	bool friendly = same;
	if (!same)
	{
		static const int friend_threshold = pSettings->r_s16(GAME_RELATIONS_SECT, "attitude_friend_threshold");
		friendly = RELATION_REGISTRY().GetCommunityRelation(a->m_community_index, b->m_community_index) >= friend_threshold &&
			RELATION_REGISTRY().GetCommunityRelation(b->m_community_index, a->m_community_index) >= friend_threshold;
	}
	const bool allow = coop_ff_allows(mode, same, friendly);
	if (log_budget())
		Msg("- COOP(ff): player %u -> player %u on '%s': mode %s%s, community %d -> %d (same=%d friendly=%d) => %s",
			shooter->ID, victim->ID, level_name ? level_name : "?", coop_ff_name(mode),
			(level_name && zones().count(shared_str(level_name))) ? " (zone override)" : " (global default)",
			int(a->m_community_index), int(b->m_community_index), same ? 1 : 0, friendly ? 1 : 0,
			allow ? "ALLOW" : "REFUSE");
	return !allow;
}

void coop_ff_state_save(IWriter& stream)
{
	stream.open_chunk(COOP_FF_CHUNK_DATA);
	stream.w_u16(coop_ff_state_version);
	stream.w_u32(u32(zones().size()));
	for (zone_map::const_iterator it = zones().begin(); it != zones().end(); ++it)
	{
		stream.w_stringZ(it->first);
		stream.w_u8(it->second);
	}
	stream.close_chunk();
	Msg("- COOP(ff): saved %u zone override(s)", u32(zones().size()));
}

void coop_ff_state_load(IReader& stream)
{
	// A load that finds nothing must leave no override from the previous world standing.
	zones().clear();
	const int caller_pos = stream.tell();
	const u32 chunk_size = stream.find_chunk(COOP_FF_CHUNK_DATA);
	if (!chunk_size)
	{
		stream.seek(caller_pos);
		Msg("- COOP(ff): no friendly-fire chunk in this save — every zone inherits the global default %s",
			coop_ff_name(coop_ff_global_default()));
	}
	else
	{
		coop_chunk_reader r(stream, stream.tell() + int(chunk_size));
		const u16 ver = r.u16v();
		if (!r.ok || ver != coop_ff_state_version)
			Msg("! COOP(ff): friendly-fire state version %u is not %u — REFUSING it; every zone inherits the "
				"global default", u32(ver), u32(coop_ff_state_version));
		else
		{
			zone_map pending;
			const u32 n = r.count(/* NUL + u8 */ 2);
			for (u32 i = 0; i < n && r.ok; ++i)
			{
				shared_str level;
				if (!r.str(level))
					break;
				const u8 mode = r.u8v();
				if (r.ok && mode < eCoopFF_Count && level.size())
					pending[level] = mode;
			}
			if (r.ok)
			{
				zones() = pending;
				for (zone_map::const_iterator it = zones().begin(); it != zones().end(); ++it)
					Msg("- COOP(ff): loaded zone '%s' override %s", it->first.c_str(), coop_ff_name(it->second));
				Msg("- COOP(ff): loaded %u zone override(s)", u32(zones().size()));
			}
			else
				Msg("! COOP(ff): friendly-fire chunk is truncated — REFUSING all of it rather than half");
		}
		stream.seek(caller_pos);
	}

	// Harness seam: -coop_test_ff_zone <level>=<MODE> sets one override AFTER the load, so a test can write it
	// into a save and then boot WITHOUT the flag to see it come back from the chunk.
	char buf[128];
	if (flag_arg("-coop_test_ff_zone", buf, sizeof buf))
	{
		char* eq = strchr(buf, '=');
		if (eq)
		{
			*eq = 0;
			const int m = coop_ff_parse(eq + 1);
			if (m == eCoopFF_Invalid)
				Msg("! COOP(ff): -coop_test_ff_zone mode '%s' is not a mode — nothing set", eq + 1);
			else
				coop_ff_set_zone(buf, m);
		}
		else
			Msg("! COOP(ff): -coop_test_ff_zone wants <level>=<MODE>, got '%s' — nothing set", buf);
	}
}
