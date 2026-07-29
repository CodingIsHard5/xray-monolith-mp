#include "stdafx.h"
#include "profiler.h"
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

// ---- COOP (§14 step 8 P4): address -> construction site, so a wait graph can be read ------------
//
// See the header for why this exists. Constraints that shape it:
//  * it runs from a CONSTRUCTOR during static init, before the log, before xrMemory is warm — so it
//    must not allocate, must not log, and must not depend on any other global's lifetime. A fixed
//    array of PODs with no destructor satisfies all three.
//  * it is called from multiple threads. The index is bumped with an interlocked add.
//  * OVERFLOW IS REPORTED, not silently dropped. A table that quietly stopped recording would make
//    "that address is not one of ours" indistinguishable from "the table was full" — the same
//    absence-vs-broken-instrument confusion this increment has hit repeatedly.
namespace
{
	struct coop_cs_entry { void* cs; void* site; };
	enum { COOP_CS_MAX = 1024 };
	coop_cs_entry s_coop_cs[COOP_CS_MAX];
	volatile long s_coop_cs_n = 0;
}

void coop_cs_register(void* cs, void* site)
{
	const long i = _InterlockedExchangeAdd(&s_coop_cs_n, 1);
	if (i < COOP_CS_MAX)
	{
		s_coop_cs[i].cs   = cs;
		s_coop_cs[i].site = site;
	}
}

void coop_cs_dump()
{
	const long n = s_coop_cs_n;
	const long shown = (n < COOP_CS_MAX) ? n : (long)COOP_CS_MAX;
	Msg("- COOP(cs): %ld critical sections registered, %ld recorded%s. Match these addresses against "
		"the `RtlpWaitForCriticalSection section <addr>` lines in dedicated_stderr.log; `site` is the "
		"constructor's return address and resolves through AnomalyDX8.pdb.",
		n, shown, (n > COOP_CS_MAX) ? " (TABLE FULL — the rest were DROPPED, this dump is partial)" : "");
	for (long i = 0; i < shown; ++i)
		Msg("- COOP(cs):   cs=%p site=%p", s_coop_cs[i].cs, s_coop_cs[i].site);
	FlushLog();
}

#ifdef PROFILE_CRITICAL_SECTIONS
static add_profile_portion_callback add_profile_portion = 0;
void set_add_profile_portion(add_profile_portion_callback callback)
{
    add_profile_portion = callback;
}

struct profiler
{
    u64 m_time;
    LPCSTR m_timer_id;

    IC profiler::profiler(LPCSTR timer_id)
    {
        if (!add_profile_portion)
            return;

        m_timer_id = timer_id;
        m_time = CPU::QPC();
    }

    IC profiler::~profiler()
    {
        if (!add_profile_portion)
            return;

        u64 time = CPU::QPC();
        (*add_profile_portion)(m_timer_id, time - m_time);
    }
};
#endif // PROFILE_CRITICAL_SECTIONS

#ifdef PROFILE_CRITICAL_SECTIONS
xrCriticalSection::xrCriticalSection(LPCSTR id) : m_id(id)
#else // PROFILE_CRITICAL_SECTIONS
xrCriticalSection::xrCriticalSection()
#endif // PROFILE_CRITICAL_SECTIONS
{
	pmutex = xr_alloc<CRITICAL_SECTION>(1);
	InitializeCriticalSection((CRITICAL_SECTION*)pmutex);
	coop_cs_register(pmutex, _ReturnAddress());
}

xrCriticalSection::~xrCriticalSection()
{
	DeleteCriticalSection((CRITICAL_SECTION*)pmutex);
	xr_free(pmutex);
}

#ifdef DEBUG
extern void OutputDebugStackTrace(const char* header);
#endif // DEBUG

void xrCriticalSection::Enter()
{
#ifdef PROFILE_CRITICAL_SECTIONS
# if 0//def DEBUG
    static bool show_call_stack = false;
    if (show_call_stack)
        OutputDebugStackTrace("----------------------------------------------------");
# endif // DEBUG
    profiler temp(m_id);
#endif // PROFILE_CRITICAL_SECTIONS
	EnterCriticalSection((CRITICAL_SECTION*)pmutex);
}

void xrCriticalSection::Leave()
{
	LeaveCriticalSection((CRITICAL_SECTION*)pmutex);
}

BOOL xrCriticalSection::TryEnter()
{
	return TryEnterCriticalSection((CRITICAL_SECTION*)pmutex);
}

xrCriticalSection::raii::raii(xrCriticalSection* critical_section)
	: critical_section(critical_section)
{
	VERIFY(critical_section);
	critical_section->Enter();
}

xrCriticalSection::raii::~raii()
{
	critical_section->Leave();
}

xrSRWLock::xrSRWLock()
{
    InitializeSRWLock(&smutex);
}

void xrSRWLock::AcquireExclusive()
{
	PROF_EVENT("xrSRWLock::AcquireExclusive");
    AcquireSRWLockExclusive(&smutex);
}

void xrSRWLock::ReleaseExclusive()
{
	PROF_EVENT("xrSRWLock::ReleaseExclusive");
    ReleaseSRWLockExclusive(&smutex);
}

void xrSRWLock::AcquireShared()
{
	PROF_EVENT("xrSRWLock::AcquireShared");
    AcquireSRWLockShared(&smutex);
}

void xrSRWLock::ReleaseShared()
{
	PROF_EVENT("xrSRWLock::ReleaseShared");
    ReleaseSRWLockShared(&smutex);
}

BOOL xrSRWLock::TryAcquireExclusive()
{
    return TryAcquireSRWLockExclusive(&smutex);
}

BOOL xrSRWLock::TryAcquireShared()
{
    return TryAcquireSRWLockShared(&smutex);
}


xrSRWLockGuard::xrSRWLockGuard(xrSRWLock* lock, bool shared)
    : lock(lock), shared(shared)
{
    if (shared)
        lock->AcquireShared();
    else
        lock->AcquireExclusive();
}

xrSRWLockGuard::xrSRWLockGuard(xrSRWLock& lock, bool shared)
    : lock(&lock), shared(shared)
{
    if (shared)
        lock.AcquireShared();
    else
        lock.AcquireExclusive();
}

xrSRWLockGuard::~xrSRWLockGuard()
{
    if (shared)
        lock->ReleaseShared();
    else
        lock->ReleaseExclusive();
}
