#include "stdafx.h"
#pragma hdrstop

#include <intrin.h>
#pragma intrinsic(_InterlockedExchangeAdd)

// ============ COOP (§14 step 8 P4): NAME THE FAULT THAT NOBODY CATCHES ==========================
//
// THE PROBLEM THIS EXISTS TO SOLVE, stated as the confusion it removes.
//
// Every wedge this increment has chased ends the same way: the dedicated server stops writing to its
// log and stays ALIVE, so there is no crash, no assertion, and no engine-side account of what
// happened. The reason is not that the engine has no crash handler — it is that the crash handler is
// the WRONG KIND. `SetUnhandledExceptionFilter` fires only for exceptions nobody handles, and these
// are handled: an access violation is raised, LuaJIT's `lj_err_unwind_win64` catches it and unwinds,
// the code resumes, faults again, and the nesting eats the stack until the guard page goes. The
// engine's filter is never reached, so the only witness has been a wine `WINEDEBUG=+seh` trace —
// a separate file, no interleaving with the probe's own leg markers, and a hand parse of tens of
// thousands of `dispatch_exception` lines to recover three distinct addresses.
//
// A VECTORED exception handler runs FIRST-CHANCE, before any SEH frame including LuaJIT's. It sees
// exactly the faults that are about to be swallowed.
//
// WHAT THIS DOES NOT DO, because that is the property that makes it shippable: it does not change
// control flow. It returns EXCEPTION_CONTINUE_SEARCH unconditionally, so every exception continues
// to whatever would have handled it. Turning the first access violation into a hard, self-naming
// crash is a defensible NEXT step — the comment in UIActorMenu_script.cpp already argues "an AV that
// terminated would have named itself" — but it is a behaviour change on the authoritative server and
// it needs its own increment and its own regression pass. This one is an INSTRUMENT.
//
// WHAT IT REPORTS, and why each field is here rather than in the +seh log:
//  * the FAULT ADDRESS grouped into SITES with counts. The +seh capture of build 30441557143 held
//    276 access violations that collapsed to three distinct addresses; recovering that took a manual
//    pass. The site table does it in the run.
//  * a NATIVE BACKTRACE per site, first sighting only. This is the thing the wait-graph work never
//    had: the plan's standing gap is "THE DEADLOCK IS CONFIRMED, THE CALL SITES ARE UNATTRIBUTED",
//    and four separate gdb approaches failed to close it because ptrace policy is not ours to change.
//    The process that faults is ours, so the stack can be recorded rather than attached to.
//  * the THREAD ID, so a fault can be matched against the `RtlpWaitForCriticalSection ... blocked by
//    <tid>` lines that name the wedge, and against `g_coop_game_thread_id` in the game log.
//
// THE HAZARD, stated at the site rather than discovered later. This runs on the FAULTING thread, in
// whatever state that thread was in — and the measured wait graph has a thread holding the ntdll
// heap lock. `Msg` allocates. Allocating from inside a fault that happened mid-heap-operation is
// undefined, and an instrument that turns a recoverable fault into a corrupted heap has made the
// thing it measures worse. So:
//  * the COUNTING path allocates nothing and takes nothing but a leaf lock over a POD table;
//  * the reporting path writes to STDERR FIRST, formatted into a stack buffer with no allocation,
//    landing in `dedicated_stderr.log` beside the +seh trace it replaces — so the record exists
//    before anything risky is attempted;
//  * only then does it `Msg`, for the game-log interleaving that is the point of doing this
//    in-engine at all;
//  * and reporting is bounded per site, so 276 faults do not become 276 allocations.
//
// OVERFLOW IS REPORTED, never silently dropped — same rule as the lock registry next door. A table
// that quietly stopped recording would make "no other fault site" indistinguishable from "the table
// was full", which is the absence-versus-broken-instrument confusion this increment keeps paying for.

namespace
{
	enum
	{
		COOP_AV_MAX_SITES  = 32,   // the measured run had 3; 32 leaves room to be wrong
		COOP_AV_MAX_FRAMES = 24,
		COOP_AV_SKIP_FRAMES = 0,   // frame 0 is this handler; keep it, it proves which handler ran
		COOP_AV_REPEAT_EVERY = 256 // after the first sighting, how often a site says it is still going
	};

	struct coop_av_site
	{
		void* addr;      // ExceptionRecord->ExceptionAddress
		void* base;      // allocation base of the module the fault is IN — see coop_av_module_base
		u32   count;
		u32   first_tid;
	};

	coop_av_site  s_sites[COOP_AV_MAX_SITES];
	u32           s_site_count = 0;
	bool          s_sites_full = false;
	volatile long s_total = 0;      // EVERY access violation, including ones no site could be made for
	void*         s_exe_base = NULL;
	bool          s_installed = false;

	// A LEAF lock over the POD table: nothing is allocated and nothing else is acquired while it is
	// held, which is the same rule log.cpp's logCS now obeys and for the same reason — a lock taken
	// inside a fault handler must not be able to be one arm of a cycle.
	xrCriticalSection s_lock
#ifdef PROFILE_CRITICAL_SECTIONS
		(MUTEX_PROFILE_ID(coop_av_report))
#endif
		;

	// Re-entrancy guard, PER THREAD. If formatting or logging faults, the handler must not recurse —
	// that would reproduce, inside the instrument, the exact nesting failure it is here to explain.
	__declspec(thread) bool s_in_handler = false;

	// The module a given address lives in, WITHOUT the loader lock.
	//
	// The obvious call is `GetModuleHandleExA(..._FROM_ADDRESS, ...)`, and it is the wrong one here:
	// it takes the loader section, and taking the loader lock from inside a fault handler can deadlock
	// against a thread that faulted while holding it. `VirtualQuery` reads the VAD and returns the
	// same base as `AllocationBase`, with no lock at all.
	//
	// This matters more than it looks. Resolving the base from THIS translation unit's own address —
	// which is the idiom used by the off-thread VM audit next door — would yield xrCore.dll's base,
	// while the faults being chased are in the EXE (`0x140000000`) and in xrGame.dll. Every RVA
	// printed would then be silently relative to the wrong module, and an RVA that is wrong by a
	// module base symbolicates to a confident, nearby, incorrect symbol — exactly the failure mode
	// already recorded for `CScriptSound::script_register+0xfc`.
	void* coop_av_module_base(void* addr)
	{
		MEMORY_BASIC_INFORMATION mbi;
		ZeroMemory(&mbi, sizeof(mbi));
		if (VirtualQuery(addr, &mbi, sizeof(mbi)) == sizeof(mbi))
			return mbi.AllocationBase;
		return NULL;
	}

	LONG WINAPI coop_av_veh(EXCEPTION_POINTERS* xp)
	{
		// The cheap rejection comes first and is one compare. This handler is called for EVERY
		// exception in the process, and C++ exception unwinding alone raises thousands.
		if (!xp || !xp->ExceptionRecord || xp->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
			return EXCEPTION_CONTINUE_SEARCH;

		if (s_in_handler)
			return EXCEPTION_CONTINUE_SEARCH;
		s_in_handler = true;

		const EXCEPTION_RECORD* const er = xp->ExceptionRecord;
		void* const   addr = er->ExceptionAddress;
		const u32     tid = GetCurrentThreadId();
		const ULONG_PTR op = (er->NumberParameters > 0) ? er->ExceptionInformation[0] : 0;
		const ULONG_PTR target = (er->NumberParameters > 1) ? er->ExceptionInformation[1] : 0;

		// Captured OUTSIDE the lock: unwinding is not instant and holding a lock across it would put
		// this handler on the wrong side of the very rule it is documenting.
		void* frames[COOP_AV_MAX_FRAMES];
		ZeroMemory(frames, sizeof(frames));
		const USHORT got = RtlCaptureStackBackTrace(COOP_AV_SKIP_FRAMES, COOP_AV_MAX_FRAMES, frames, NULL);

		const long total = _InterlockedExchangeAdd(&s_total, 1) + 1;

		// Also outside the lock, for the same reason, and it is a plain VAD read.
		void* const base = coop_av_module_base(addr);

		bool  report = false;
		bool  first_sighting = false;
		bool  say_full = false;
		u32   idx = 0;
		u32   count = 0;

		s_lock.Enter();
		u32 i = 0;
		for (; i < s_site_count; ++i)
			if (s_sites[i].addr == addr)
				break;

		if (i < s_site_count)
		{
			idx = i;
			count = ++s_sites[i].count;
			report = ((count % COOP_AV_REPEAT_EVERY) == 0);
		}
		else if (s_site_count < COOP_AV_MAX_SITES)
		{
			idx = s_site_count++;
			s_sites[idx].addr = addr;
			s_sites[idx].base = base;
			s_sites[idx].count = 1;
			s_sites[idx].first_tid = tid;
			count = 1;
			report = true;
			first_sighting = true;
		}
		else
		{
			say_full = !s_sites_full;
			s_sites_full = true;
		}
		s_lock.Leave();

		if (say_full)
		{
			fprintf(stderr, "COOP(av): more than %u distinct fault addresses — the rest are NOT being "
			                "recorded, so read this run as INCOMPLETE rather than as a full list\n",
			        u32(COOP_AV_MAX_SITES));
			fflush(stderr);
			Msg("! COOP(av): more than %u distinct fault addresses — the rest are NOT being recorded, "
			    "so read this run as INCOMPLETE rather than as a full list", u32(COOP_AV_MAX_SITES));
		}

		if (report)
		{
			// --- stderr first: no allocation, and it lands beside the +seh trace ------------------
			string4096 trace;
			trace[0] = 0;
			for (USHORT f = 0; f < got; ++f)
			{
				string64 one;
				xr_sprintf(one, " %p", frames[f]);
				xr_strcat(trace, one);
			}

			void* const rva = base ? (void*)((char*)addr - (char*)base) : NULL;

			fprintf(stderr,
			        "COOP(av): #%ld site=%u addr=%p %s%s target=%p tid=%u count=%u mod=%p rva=%p exe=%p frames:%s\n",
			        total, idx, addr,
			        first_sighting ? "FIRST-SIGHTING " : "",
			        (op == 0) ? "read" : ((op == 1) ? "WRITE" : "execute"),
			        (void*)target, tid, count, base, rva, s_exe_base, trace);
			fflush(stderr);

			// --- and only now the game log, which is the part that can allocate -------------------
			Msg("! COOP(av): access violation #%ld — site #%u addr=%p %s target=%p thread=%u count=%u. %s",
			    total, idx, addr,
			    (op == 0) ? "reading" : ((op == 1) ? "WRITING" : "executing"),
			    (void*)target, tid, count,
			    first_sighting
			        ? "FIRST SIGHTING of this address. Nothing here has changed control flow — the "
			          "fault continues to whatever handler would have taken it."
			        : "still faulting.");
			Msg("! COOP(av):   fault module base %p, RVA %p (exe base %p). Stack, ABSOLUTE — subtract "
			    "the base of whichever module each frame is in, not this one:%s",
			    base, rva, s_exe_base, trace);
			FlushLog();
		}

		s_in_handler = false;
		return EXCEPTION_CONTINUE_SEARCH;
	}
} // namespace

XRCORE_API void coop_av_install()
{
	if (s_installed)
		return;
	// Resolved HERE, in WinMain, and not in the handler: `GetModuleHandleA(NULL)` takes the loader
	// lock, which is the one thing the fault path must not do. It is only an anchor for the reader —
	// the exe base every symbolication in this plan has been computed against.
	s_exe_base = (void*)::GetModuleHandleA(NULL);
	// first == TRUE: ahead of every other vectored handler, and ahead of all SEH frames. That is the
	// whole point — the faults of interest are caught and swallowed by a frame further down.
	if (::AddVectoredExceptionHandler(1, &coop_av_veh))
		s_installed = true;
}

XRCORE_API void coop_av_dump()
{
	// A POSITIVE CONTROL, and it is the reason this is called from the probe's arm rather than only
	// at the end. Called before any fault, it prints "0 recorded" from a handler that is demonstrably
	// installed — which is what separates "this run had no access violations" from "the instrument
	// was never registered". Those two look identical in a log, and this increment has already been
	// caught out once by a silent instrument (`print_stack` printing nothing because its logger was
	// gated, not because the caller was not Lua).
	s_lock.Enter();
	const u32 n = s_site_count;
	const bool full = s_sites_full;
	coop_av_site snapshot[COOP_AV_MAX_SITES];
	for (u32 i = 0; i < n; ++i)
		snapshot[i] = s_sites[i];
	s_lock.Leave();

	Msg("- COOP(av): first-chance access-violation reporter is %s. %ld access violations so far, in "
	    "%u distinct sites%s. exe base=%p",
	    s_installed ? "INSTALLED" : "!! NOT INSTALLED — every zero below is meaningless",
	    s_total, n, full ? " (SITE TABLE FULL — this list is PARTIAL)" : "", s_exe_base);
	for (u32 i = 0; i < n; ++i)
		Msg("- COOP(av):   site #%u addr=%p mod=%p RVA=%p count=%u first_thread=%u",
		    i, snapshot[i].addr, snapshot[i].base,
		    snapshot[i].base ? (void*)((char*)snapshot[i].addr - (char*)snapshot[i].base) : NULL,
		    snapshot[i].count, snapshot[i].first_tid);
	FlushLog();
}
