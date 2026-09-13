////////////////////////////////////////////////////////////////////////////
//  mp_coop_ff.h — design doc §12: friendly-fire / PvP modes, server-authoritative.
//
//  The pure part (modes, names, the resolution table) has NO engine dependency so it is unit-tested
//  natively: StalkerMPMod dev/harness/native/test_coop_ff.cpp (g++, run by test_coop_ff_offline.sh).
//  The engine part (state, persistence, the hit hook) is declared at the bottom and lives in
//  mp_coop_ff.cpp.
////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstring>

enum ECoopFFMode
{
	eCoopFF_UniversalOn = 0,     // every player can damage every other player (default; the pre-§12 behaviour)
	eCoopFF_UniversalOff = 1,    // no player can damage any other player
	eCoopFF_FriendlyFactions = 2,// players whose factions are friendly cannot damage each other
	eCoopFF_OwnFactionOnly = 3,  // a player cannot damage their own faction; any other faction can be engaged
	eCoopFF_Count = 4,
	eCoopFF_Invalid = 255
};

inline const char* coop_ff_name(int mode)
{
	switch (mode)
	{
	case eCoopFF_UniversalOn: return "UNIVERSAL_ON";
	case eCoopFF_UniversalOff: return "UNIVERSAL_OFF";
	case eCoopFF_FriendlyFactions: return "FRIENDLY_FACTIONS";
	case eCoopFF_OwnFactionOnly: return "OWN_FACTION_ONLY";
	default: return "INVALID";
	}
}

// case-insensitive, exact; eCoopFF_Invalid on anything else (never a silent default)
inline int coop_ff_parse(const char* s)
{
	if (!s)
		return eCoopFF_Invalid;
	for (int m = 0; m < eCoopFF_Count; ++m)
	{
		const char* n = coop_ff_name(m);
		size_t i = 0;
		for (; n[i] && s[i]; ++i)
		{
			char a = s[i];
			if (a >= 'a' && a <= 'z') a = char(a - 'a' + 'A');
			if (a != n[i]) break;
		}
		if (!n[i] && !s[i])
			return m;
	}
	return eCoopFF_Invalid;
}

// §12.3 resolution. true = the damage applies. An invalid mode ALLOWS (fails open to the pre-§12
// behaviour) and the caller logs it: refusing all PvP on a corrupt value would be a silent new rule.
inline bool coop_ff_allows(int mode, bool same_faction, bool factions_friendly)
{
	switch (mode)
	{
	case eCoopFF_UniversalOff: return false;
	case eCoopFF_FriendlyFactions: return !factions_friendly;
	case eCoopFF_OwnFactionOnly: return !same_faction;
	case eCoopFF_UniversalOn:
	default: return true;
	}
}

#ifndef COOP_FF_PURE_ONLY
class CSE_Abstract;
class IWriter;
class IReader;

// the mode for a level: its override if one is set, else the server's global default (-coop_ff <mode>)
int coop_ff_mode_for(const char* level_name);
int coop_ff_global_default();
// runtime change (§12.2). level_name empty/NULL = clear nothing and set nothing: the global default is
// server config, not world state. mode == eCoopFF_Invalid removes the level's override. Returns false on
// a bad argument.
bool coop_ff_set_zone(const char* level_name, int mode);
// the GE_HIT gate: true = refuse this hit. Only ever true for a player body shooting another player body.
bool coop_ff_refuse_hit(CSE_Abstract* shooter, CSE_Abstract* victim, const char* level_name);
void coop_ff_state_save(IWriter& stream);
void coop_ff_state_load(IReader& stream);
#endif
