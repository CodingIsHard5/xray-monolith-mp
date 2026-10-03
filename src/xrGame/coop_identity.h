#pragma once
// MP fork (mod enforcement B, 2026-10-03 — dev/MOD-ENFORCEMENT-SCOPE.md, option B in option C's message format).
//
// What a co-op install IS, as a short list of categories, each a 64-bit hash. A client sends its list in
// M_XRNET_COOP_IDENTITY at join; the server compares it with its own and refuses (or, under -coop_identity_warn, warns)
// on a difference, naming what differs. Format version 1:
//   u16 format_version | stringZ build commit | u8 count | count x { stringZ category, u64 hash, u8 present, u32 files }
// A later option C adds categories (and a per-file follow-up) without changing this layout.
//
// Categories (v1):
//   build    the commit the engine was built from (coop_build_commit.h, stamped by CI)          must match
//   coopset  CONTENTS of the co-op scripts (mp_*.script, zzz_mp_*.script)                       must match
//   scripts  path + size of every root-level *.script under $game_scripts$ (as fsgame.ltx declares it)  must match
//   configs  path + size of every file under $game_config$ (item, weapon, ammo, NPC sections)    must match
//   modlist  the enabled mods, in order, of the MO2 profile given by -coop_modlist <path>        must match when both
//            sides have one; a side without one is a WARNING, never a refusal (Overseer: degrade gracefully)
// path + size is cheap (the FS index is in memory) and misses a same-size edit; option C's content hashes close that.
#include "../xrCore/coop_build_commit.h"
#include "../xrCore/FS.h"   // IReader (a forward declaration is not enough)
#include <algorithm>
#include <stdio.h>

namespace coop_identity
{
static const u16 format_version = 1;
static const u64 fnv0 = 14695981039346656037ULL;

struct entry
{
	shared_str cat;
	u64 hash;
	u8 present;
	u32 files;
};

inline u64 fnv(const void* p, size_t n, u64 h)
{
	const u8* b = (const u8*)p;
	for (size_t i = 0; i < n; ++i)
	{
		h ^= b[i];
		h *= 1099511628211ULL;
	}
	return h;
}

inline u64 fnv_str(LPCSTR s, u64 h) { return fnv(s, xr_strlen(s), h); }

inline bool coop_script_name(LPCSTR n)
{
	LPCSTR base = strrchr(n, '\\');
	if (!base) base = strrchr(n, '/');
	base = base ? base + 1 : n;
	return (0 == strncmp(base, "mp_", 3)) || (0 == strncmp(base, "zzz_mp_", 7));
}

// every file under an FS alias, sorted by lower-cased name; hash = name + (content | size)
// The FS index is NOT a stable picture of an install: $game_scripts$ is declared non-recursive with mask *.script
// (fsgame.ltx), yet a mod's loader can mount a subfolder at runtime (illish\lib\*.lua, 29 entries), and whether that has
// happened depends on process state. iddump-match (2026-10-03): the client listed 1358 entries, the server 1329 — every
// difference one of those runtime-mounted .lua files — so an identical install was REFUSED. "scripts" therefore hashes the
// alias AS DECLARED: root-level *.script files only. What a mod mounts at runtime is outside B; option C (content hashes
// of the physical tree) covers it.
inline bool coop_root_script(LPCSTR n)
{
	if (strchr(n, '\\') || strchr(n, '/'))
		return false;
	const size_t l = xr_strlen(n);
	return (l > 7) && (0 == _stricmp(n + l - 7, ".script"));
}

inline entry list_entry(LPCSTR cat, LPCSTR alias, bool coop_only, bool content, bool root_scripts_only = false)
{
	entry e;
	e.cat = cat;
	e.hash = fnv0;
	e.present = 0;
	e.files = 0;
	if (!FS.path_exist(alias))
		return e;
	xr_vector<LPSTR>* lst = FS.file_list_open(alias, FS_ListFiles);
	if (!lst)
		return e;
	xr_vector<xr_string> names;
	for (LPSTR n : *lst)
		if ((!coop_only || coop_script_name(n)) && (!root_scripts_only || coop_root_script(n)))
		{
			xr_string s = n;
			for (char& c : s) c = (char)tolower((unsigned char)c);
			names.push_back(s);
		}
	FS.file_list_close(lst);
	std::sort(names.begin(), names.end());
	// -coop_identity_dump (diagnostic): this category's list, as hashed, to <logs>/coop_identity_<cat>.txt
	IWriter* dump = strstr(Core.Params, "-coop_identity_dump") ? FS.w_open("$logs$", (xr_string("coop_identity_") + cat + ".txt").c_str()) : nullptr;
	for (const xr_string& n : names)
	{
		if (dump)
		{
			string_path ln;
			const CLocatorAPI::file* df = content ? nullptr : FS.exist(alias, n.c_str());
			xr_sprintf(ln, "%s %u", n.c_str(), df ? df->size_real : 0u);
			dump->w_string(ln);
		}
		e.hash = fnv_str(n.c_str(), e.hash);
		if (content)
		{
			IReader* r = FS.r_open(alias, n.c_str());
			if (r)
			{
				e.hash = fnv(r->pointer(), r->length(), e.hash);
				FS.r_close(r);
			}
		}
		else if (const CLocatorAPI::file* f = FS.exist(alias, n.c_str()))
		{
			const u32 sz = f->size_real;
			e.hash = fnv(&sz, sizeof(sz), e.hash);
		}
		++e.files;
	}
	if (dump)
		FS.w_close(dump);
	e.present = e.files ? 1 : 0;
	return e;
}

// the launch parameter's value, e.g. -coop_modlist Z:\path\modlist.txt (no spaces in the path)
inline bool param_value(LPCSTR key, string_path& out)
{
	out[0] = 0;
	LPCSTR p = strstr(Core.Params, key);
	if (!p)
		return false;
	p += xr_strlen(key);
	while (*p == ' ')
		++p;
	u32 i = 0;
	while (*p && *p != ' ' && i + 1 < sizeof(out))
		out[i++] = *p++;
	out[i] = 0;
	return i > 0;
}

// the MO2 profile's enabled mods ('+' lines), in order
inline entry modlist_entry()
{
	entry e;
	e.cat = "modlist";
	e.hash = fnv0;
	e.present = 0;
	e.files = 0;
	string_path path;
	if (!param_value("-coop_modlist", path))
		return e;
	FILE* f = fopen(path, "rb");
	if (!f)
	{
		Msg("! COOP(identity): -coop_modlist %s could not be opened; the mod list is not checked", path);
		return e;
	}
	char line[1024];
	while (fgets(line, sizeof(line), f))
	{
		size_t n = strlen(line);
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if (n && line[0] == '+')
		{
			e.hash = fnv(line, n, e.hash);
			++e.files;
		}
	}
	fclose(f);
	e.present = 1;
	return e;
}

inline void compute(xr_vector<entry>& out)
{
	out.clear();
	entry b;
	b.cat = "build";
	b.hash = fnv_str(COOP_BUILD_COMMIT, fnv0);
	b.present = 1;
	b.files = 0;
	out.push_back(b);
	out.push_back(list_entry("coopset", "$game_scripts$", true, true));
	out.push_back(list_entry("scripts", "$game_scripts$", false, false, true));
	out.push_back(list_entry("configs", "$game_config$", false, false));
	out.push_back(modlist_entry());
	// TEST HOOK (clearly a test): -coop_test_identity_skew <category> flips that category's hash, to stand in for a
	// modified install on a machine where client and server share one game tree
	string_path skew;
	if (param_value("-coop_test_identity_skew", skew))
		for (entry& e : out)
			if (e.cat == skew)
			{
				e.hash ^= 0x5eed5eed5eed5eedULL;
				Msg("! COOP(identity): TEST -coop_test_identity_skew: category '%s' reported as modified", skew);
			}
}

inline void write(NET_Packet& P, const xr_vector<entry>& v)
{
	P.w_u16(format_version);
	P.w_stringZ(COOP_BUILD_COMMIT);
	P.w_u8(u8(v.size()));
	for (const entry& e : v)
	{
		P.w_stringZ(e.cat.c_str());
		P.w_u64(e.hash);
		P.w_u8(e.present);
		P.w_u32(e.files);
	}
}
} // namespace coop_identity
