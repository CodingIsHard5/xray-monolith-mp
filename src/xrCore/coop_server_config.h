////////////////////////////////////////////////////////////////////////////
//  coop_server_config.h — design doc Appendix B: every co-op tunable in ONE server config file.
//
//  The fork reads ~100 -coop_* / -mp_* / -xrnet_* flags straight from Core.Params, each at its own call site.
//  Rather than rewrite every call site, xrCore reads [coop_server] from $app_data_root$/coop_server.ltx right
//  after the filesystem comes up and MERGES it into Core.Params, so every existing lookup honours the file:
//
//      [coop_server]
//      coop_autosave          = 120          ; value flag  -> " -coop_autosave 120"
//      coop_no_time_halt      =              ; switch, empty or on/1/true/yes -> " -coop_no_time_halt"
//      coop_empty_keeps_actor_region = off   ; switch off -> nothing appended
//
//  Rules (all logged, one line per key): the command line wins over the file; a key that is not a known flag is
//  refused (a typo must not silently do nothing); a test seam is refused (a server config must not arm a harness
//  hook that mutates saves); a value with whitespace is refused (flags are space-delimited); a value flag with an
//  empty value is refused. The known-flag table is coop_server_config_keys.h, and an offline test checks it
//  against every flag literal in the engine source so it cannot drift.
//
//  Flags read inside xrCore BEFORE the filesystem (the memory instruments) cannot come from the file; they are
//  marked early in the table and refused with that reason.
//
//  Pure (std only) so dev/harness/native/test_coop_server_config.cpp can test it without the engine.
////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cctype>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

enum coop_cfg_kind
{
	coop_cfg_switch,
	coop_cfg_value,
};

enum coop_cfg_class
{
	coop_cfg_tunable,     // a real server setting
	coop_cfg_control,     // turns a fix off for an A/B arm; allowed, but logged as such
	coop_cfg_diag,        // logging / tracing
	coop_cfg_test,        // harness seam: refused from the file
	coop_cfg_early,       // read before the filesystem exists: refused from the file
};

struct coop_cfg_key
{
	const char* name;     // without the leading dash
	coop_cfg_kind kind;
	coop_cfg_class cls;
};

// "-flag" present as a whole token (followed by whitespace or the end), not as a prefix of a longer flag
inline bool coop_cfg_has_token(const std::string& params, const std::string& flag)
{
	for (size_t p = params.find(flag); p != std::string::npos; p = params.find(flag, p + 1))
	{
		const bool starts = (p == 0) || std::isspace((unsigned char)params[p - 1]);
		const size_t e = p + flag.size();
		const bool ends = (e == params.size()) || std::isspace((unsigned char)params[e]);
		if (starts && ends)
			return true;
	}
	return false;
}

inline std::string coop_cfg_trim_lower(const std::string& s)
{
	size_t b = 0, e = s.size();
	while (b < e && std::isspace((unsigned char)s[b])) ++b;
	while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
	std::string out = s.substr(b, e - b);
	for (size_t i = 0; i < out.size(); ++i)
		out[i] = char(std::tolower((unsigned char)out[i]));
	return out;
}

// Merge config entries into a command line. Returns the new command line; one log line per entry in `log`.
// `applied` counts what was appended.
inline std::string coop_cfg_merge(const std::string& params, const std::vector<std::pair<std::string, std::string> >& entries,
	const coop_cfg_key* known, size_t nknown, std::vector<std::string>& log, unsigned& applied, std::string* appended_names = 0)
{
	std::string out = params;
	applied = 0;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		std::string key = coop_cfg_trim_lower(entries[i].first);
		const std::string val = coop_cfg_trim_lower(entries[i].second);
		if (!key.empty() && key[0] == '-')
			key.erase(0, 1);
		const coop_cfg_key* k = 0;
		for (size_t j = 0; j < nknown && !k; ++j)
			if (key == known[j].name)
				k = &known[j];
		const std::string flag = "-" + key;
		if (!k)
		{
			log.push_back("! " + key + ": not a known co-op flag — REFUSED (typo?)");
			continue;
		}
		if (k->cls == coop_cfg_test)
		{
			log.push_back("! " + key + ": a test seam — REFUSED from the server config (use the command line)");
			continue;
		}
		if (k->cls == coop_cfg_early)
		{
			log.push_back("! " + key + ": read before the filesystem exists — REFUSED from the file (use the command line)");
			continue;
		}
		if (coop_cfg_has_token(out, flag))
		{
			log.push_back("- " + key + ": on the command line, which wins over the file");
			continue;
		}
		// a value that is itself a flag ("coop_ff = -coop_test_kill") would put that flag on the command line as a
		// bare switch, past every rule above; negative numbers stay allowed
		if (val.size() >= 2 && val[0] == '-' && !std::isdigit((unsigned char)val[1]) && val[1] != '.')
		{
			log.push_back("! " + key + ": value '" + val + "' looks like a flag — REFUSED");
			continue;
		}
		for (size_t c = 0; c < val.size(); ++c)
			if (std::isspace((unsigned char)val[c]))
			{
				log.push_back("! " + key + ": value '" + val + "' contains whitespace — REFUSED (flags are space-delimited)");
				goto next;
			}
		if (k->kind == coop_cfg_switch)
		{
			if (val.empty() || val == "on" || val == "1" || val == "true" || val == "yes")
			{
				out += " " + flag;
				++applied;
				if (appended_names) *appended_names += " " + flag;
				log.push_back(std::string("- ") + key + ": on" + (k->cls == coop_cfg_control ? " (a CONTROL flag: this turns a fix off)" : ""));
			}
			else if (val == "off" || val == "0" || val == "false" || val == "no")
				log.push_back("- " + key + ": off");
			else
				log.push_back("! " + key + ": switch value '" + val + "' is not on/off — REFUSED");
		}
		else
		{
			if (val.empty())
			{
				log.push_back("! " + key + ": needs a value — REFUSED");
				continue;
			}
			out += " " + flag + " " + val;
			++applied;
			if (appended_names) *appended_names += " " + flag;
			log.push_back("- " + key + " = " + val);
		}
	next:;
	}
	return out;
}
