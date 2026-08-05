// xrCore.cpp : Defines the entry point for the DLL application.
//
#include "stdafx.h"
#pragma hdrstop

#include <mmsystem.h>
#include <objbase.h>
#include "xrCore.h"
#include "coop_pool.h"		// §6c: two-phase arm of the luabind pool, gated on its selftest

#pragma comment(lib,"winmm.lib")

#ifdef DEBUG
# include <malloc.h>
#endif // DEBUG

#include<fstream>
#include <iostream>
#include <string>

XRCORE_API xrCore Core;
extern XRCORE_API u32 build_id;
extern XRCORE_API LPCSTR build_date;

namespace CPU
{
	extern void Detect();
};

static u32 init_counter = 0;

//extern char g_application_path[256];

//. extern xr_vector<shared_str>* LogFile;

// demonized: print modded exes version
extern int get_modded_exes_version();
extern xr_string get_modded_exes_version_string();
extern LPCSTR get_modded_exes_name();
extern std::string timeInDMYHMSMMM();

// §5s — a delimiter-safe command-line lookup for xrCore. `strstr` alone would match
// `-coop_addrmap` inside `-coop_addrmap_top` and then parse that flag's argument as the floor,
// which is the defect the game layer's `coop_param` was written to avoid; this is its twin, living
// here because xrCore cannot see a static defined in xrGame.
static LPCSTR coop_core_param(LPCSTR flag)
{
	if (!Core.Params) return NULL;
	const size_t n = xr_strlen(flag);
	LPCSTR p = Core.Params;
	while ((p = strstr(p, flag)) != NULL)
	{
		LPCSTR after = p + n;
		if (!*after || *after == ' ' || *after == '\t')
		{
			while (*after == ' ' || *after == '\t') ++after;
			return after;
		}
		p += n;
	}
	return NULL;
}

void xrCore::_initialize(LPCSTR _ApplicationName, LogCallback cb, BOOL init_fs, LPCSTR fs_fname)
{
	xr_strcpy(ApplicationName, _ApplicationName);
	if (0 == init_counter)
	{
#ifdef XRCORE_STATIC
        _clear87();
        _control87(_PC_53, MCW_PC);
        _control87(_RC_CHOP, MCW_RC);
        _control87(_RC_NEAR, MCW_RC);
        _control87(_MCW_EM, MCW_EM);
#endif
		// Init COM so we can use CoCreateInstance
		// HRESULT co_res =
		Params = xr_strdup(GetCommandLine());
		xr_strlwr(Params);
		if (!strstr(Params, "-editor"))
			CoInitializeEx(NULL, COINIT_MULTITHREADED);

		string_path fn, dr, di;

		// application path
		GetModuleFileName(GetModuleHandle(MODULE_NAME), fn, sizeof(fn));
		_splitpath(fn, dr, di, 0, 0);
		strconcat(sizeof(ApplicationPath), ApplicationPath, dr, di);

#ifndef _EDITOR
		//        xr_strcpy(g_application_path, sizeof(g_application_path), ApplicationPath);
#endif

#ifdef _EDITOR
        // working path
        if (strstr(Params, "-wf"))
        {
            string_path c_name;
            sscanf(strstr(Core.Params, "-wf ") + 4, "%[^ ] ", c_name);
            SetCurrentDirectory(c_name);
        }
#endif

		GetCurrentDirectory(sizeof(WorkingPath), WorkingPath);

		// User/Comp Name
		DWORD sz_user = sizeof(UserName);
		GetUserName(UserName, &sz_user);

		DWORD sz_comp = sizeof(CompName);
		GetComputerName(CompName, &sz_comp);

		// Mathematics & PSI detection
		CPU::Detect();

		Memory._initialize(strstr(Params, "-mem_debug") ? TRUE : FALSE);

		DUMP_PHASE;

		InitLog();

		// §5s — ARM THE ADDRESS MAP HERE, NOT IN THE GAME LAYER, AND THE REASON IS A MEASUREMENT.
		//
		// `-coop_addrmap` used to be armed from `game_sv_Single::Update`, alongside every other
		// coop probe. That is after the level loads, so every allocation made during boot was
		// INVISIBLE to it — and §5s's arena census read the consequence without at first seeing
		// it: occupancy tracked arena AGE (arenas born during load read 55-87%, ones born after
		// read 27-50%) purely because the older an arena was, the more of its contents predated
		// the instrument. The census could not answer the question it was built for in a 900 s
		// window, since the only unambiguously post-arming arena was the one still filling.
		//
		// Correcting for that downstream would need the arming instant expressed on the smaps
		// sampler's clock, and nothing relates the two. Arming before anything allocates removes
		// the bias at its source instead: there is then no such thing as an invisible block.
		//
		// WHY THIS SPECIFIC LINE. `Params` is populated (and lowercased) further up, so the flag
		// is readable; `InitLog()` has run, so `Msg` has somewhere to go; and `Memory._initialize`
		// has run, so the allocator is live. Anything earlier would arm an instrument that cannot
		// report and might not have an allocator to hook.
		//
		// It is worth more than convenience: this machine is shared with its owner and with the
		// fleet, four census attempts were already lost to load, and an instrument that only
		// yields a reading after 900 quiet seconds is one you rarely get to use. Making SHORT
		// censuses readable is what makes the measurement repeatable here at all.
		{
			LPCSTR pa = coop_core_param("-coop_addrmap");
			if (pa)
			{
				LPCSTR pams = coop_core_param("-coop_addrmap_ms");
				LPCSTR patop = coop_core_param("-coop_addrmap_top");
				LPCSTR pacen = coop_core_param("-coop_addrmap_census");
				const int bytes = atoi(pa);
				const int ivl = pams ? atoi(pams) : 0;
				const int top = patop ? atoi(patop) : 0;
				const int cen = pacen ? atoi(pacen) : 0;
				if (bytes > 0)
					coop_addrmap_arm(size_t(bytes), ivl > 0 ? u32(ivl) : 0,
					                 top > 0 ? u32(top) : 0, cen > 0 ? u32(cen) : 0);
				else
					Msg("! COOP(addrmap): -coop_addrmap needs a positive floor in BYTES (got "
					    "'%s') -- NOT armed.", pa);
			}
		}

		// §6c — ARM THE LUABIND POOL HERE, FOR THE SAME REASON THE ADDRESS MAP IS ARMED HERE, and
		// one more that is specific to it.
		//
		// The shared reason: arming after the level loads leaves every boot allocation outside the
		// pool. Unlike the address map that is not a CORRECTNESS problem -- the discriminator is
		// exact, so a block allocated before arming is simply foreign forever and takes the
		// unchanged path -- but it does mean a large, long-lived population of exactly the objects
		// the pool exists for never sees it, and GATE B (arena occupancy, mean live block size)
		// would then be measuring a fraction of the intended change.
		//
		// The specific reason: THE SELFTEST GATES THE ARM. It runs here, before a single Lua state
		// exists and therefore before any pooled block can be live, which is the only point where
		// "the discriminator is wrong" can still be answered by refusing to arm rather than by
		// corrupting a heap. If it fails, the pool stays disarmed, `owns()` is false for every
		// pointer in the address space, and the server runs exactly as an unflagged build does.
		//
		// The verdict and its counts go to the log either way. A selftest whose result is not
		// recorded is a selftest that cannot be cited afterwards, and this run's whole claim rests
		// on it.
		{
			LPCSTR pp = coop_core_param("-coop_pool");
			if (pp)
			{
				const int mib = atoi(pp);
				if (mib > 0)
				{
					if (coop_pool_arm(size_t(mib)))
					{
						char report[512];
						const bool ok = coop_pool_selftest(report, sizeof(report));
						Msg("* COOP(pool): %s", report);
						if (ok)
						{
							// The ONLY call to this in the engine, and it is downstream of a
							// passing selftest by construction.
							coop_pool_enable();
							Msg("* COOP(pool): ENABLED, region %d MiB, pooling luabind "
							    "allocations <= %u bytes.",
							    mib, (unsigned)coop_pool_max_pooled());
						}
						else
							Msg("! COOP(pool): SELFTEST FAILED -- NOT enabled. The region stays "
							    "reserved and the pool serves nobody, so the server runs exactly "
							    "as an unflagged build. Do not read a memory result from this "
							    "run: the flag was passed and the pool did not take effect.");
					}
					else
						Msg("! COOP(pool): could not reserve %d MiB of address space -- NOT "
						    "armed, the server runs unchanged.", mib);
				}
				else
					Msg("! COOP(pool): -coop_pool needs a positive region size in MiB (got '%s') "
					    "-- NOT armed.", pp);
			}
		}

		_initialize_cpu();

		// Debug._initialize ();

		rtc_initialize();

		time_t _time = time(NULL);
		tm* time = localtime(&_time);
		april1 = time ? (time->tm_mday == 1 && time->tm_mon == 3) : false;

		xr_FS = xr_new<CLocatorAPI>();

		xr_EFS = xr_new<EFS_Utils>();
		//. R_ASSERT (co_res==S_OK);

		//Load cmd line from file if it exists
		std::ifstream cmdlineTxt;
		char path_A[MAX_PATH];
		strcpy(path_A, Core.ApplicationPath);
		strcat(path_A, "\\..\\commandline.txt");
		cmdlineTxt.open(path_A);
		
		if (!cmdlineTxt)
		{
			cmdlineTxt.close();
			strcpy(path_A, Core.WorkingPath);
			strcat(path_A, "\\commandline.txt");
			cmdlineTxt.open(path_A);
		}

		if (cmdlineTxt)
		{
			Msg("Found commandline file!");
			std::string line;
			char temp[2048];
			sprintf(temp, Params);
			strcat(temp, " ");
			while (std::getline(cmdlineTxt, line))
			{
				strcat(temp, line.c_str());
				strcat(temp, " ");
			}
			Params = xr_strdup(temp);
		}
		cmdlineTxt.close();
	}
	if (init_fs)
	{
		u32 flags = 0;
		if (0 != strstr(Params, "-build")) flags |= CLocatorAPI::flBuildCopy;
		if (0 != strstr(Params, "-ebuild")) flags |= CLocatorAPI::flBuildCopy | CLocatorAPI::flEBuildCopy;
#ifdef DEBUG
        if (strstr(Params, "-cache")) flags |= CLocatorAPI::flCacheFiles;
        else flags &= ~CLocatorAPI::flCacheFiles;
#endif // DEBUG
#ifdef _EDITOR // for EDITORS - no cache
        flags &= ~CLocatorAPI::flCacheFiles;
#endif // _EDITOR
		flags |= CLocatorAPI::flScanAppRoot;

#ifndef _EDITOR
#ifndef ELocatorAPIH
		if (0 != strstr(Params, "-file_activity")) flags |= CLocatorAPI::flDumpFileActivity;
#endif
#endif
		FS._initialize(flags, 0, fs_fname);
		Msg("'%s' build %d, %s\n", "xrCore", build_id, build_date);

		// demonized: Print modded exes version
		Msg("%s version %s\n", get_modded_exes_name(), get_modded_exes_version_string().c_str());
		Msg("Game started: %s\n", timeInDMYHMSMMM().c_str());
		EFS._initialize();
#ifdef DEBUG
#ifndef _EDITOR
        Msg("Process heap 0x%08x", GetProcessHeap());
#endif
#endif // DEBUG
	}
	SetLogCB(cb);
	init_counter++;
}

#ifndef _EDITOR
#include "compression_ppmd_stream.h"
extern compression::ppmd::stream* trained_model;
#endif
void xrCore::_destroy()
{
	--init_counter;
	if (0 == init_counter)
	{
		FS._destroy();
		EFS._destroy();
		xr_delete(xr_FS);
		xr_delete(xr_EFS);

#ifndef _EDITOR
		if (trained_model)
		{
			void* buffer = trained_model->buffer();
			xr_free(buffer);
			xr_delete(trained_model);
		}
#endif
		xr_free(Params);
		Memory._destroy();
	}
}

#ifndef XRCORE_STATIC

//. why ???
#ifdef _EDITOR
BOOL WINAPI DllEntryPoint(HINSTANCE hinstDLL, DWORD ul_reason_for_call, LPVOID lpvReserved)
#else
//BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD ul_reason_for_call, LPVOID lpvReserved)
BOOL DllMainXrCore(HANDLE hinstDLL, DWORD ul_reason_for_call, LPVOID lpvReserved)
#endif
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		{
			_clear87();
			_control87(_PC_53, MCW_PC);
			_control87(_RC_CHOP, MCW_RC);
			_control87(_RC_NEAR, MCW_RC);
			_control87(_MCW_EM, MCW_EM);
		}
		//. LogFile.reserve (256);
		break;
	case DLL_THREAD_ATTACH:
		if (!strstr(GetCommandLine(), "-editor"))
			CoInitializeEx(NULL, COINIT_MULTITHREADED);
		timeBeginPeriod(1);
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
#ifdef USE_MEMORY_MONITOR
        memory_monitor::flush_each_time(true);
#endif // USE_MEMORY_MONITOR
		break;
	}
	return TRUE;
}
#endif // XRCORE_STATIC
