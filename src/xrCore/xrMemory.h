#ifndef xrMemoryH
#define xrMemoryH
#pragma once

#include "memory_monitor.h"

#ifdef USE_MEMORY_MONITOR
# define DEBUG_MEMORY_NAME
#endif // USE_MEMORY_MONITOR

#ifndef M_BORLAND
# if 0//def DEBUG
# define DEBUG_MEMORY_MANAGER
# endif // DEBUG
#endif // M_BORLAND

#ifdef DEBUG_MEMORY_MANAGER
XRCORE_API extern BOOL g_bMEMO;
# ifndef DEBUG_MEMORY_NAME
# define DEBUG_MEMORY_NAME
# endif // DEBUG_MEMORY_NAME
extern XRCORE_API void dump_phase ();
# define DUMP_PHASE do {dump_phase();} while (0)
#else // DEBUG_MEMORY_MANAGER
# define DUMP_PHASE do {} while (0)
#endif // DEBUG_MEMORY_MANAGER

#include "xrMemory_pso.h"
#include "xrMemory_POOL.h"

class XRCORE_API xrMemory
{
public:
	struct mdbg
	{
		void* _p;
		size_t _size;
		const char* _name;
		u32 _dummy;
	};

public:
	xrMemory();
	void _initialize(BOOL _debug_mode = FALSE);
	void _destroy();

#ifdef DEBUG_MEMORY_MANAGER
    BOOL debug_mode;
    xrCriticalSection debug_cs;
    std::vector<mdbg> debug_info;
    u32 debug_info_update;
    u32 stat_strcmp ;
    u32 stat_strdock ;
#endif // DEBUG_MEMORY_MANAGER

	u32 stat_calls;
	s32 stat_counter;
public:
	void dbg_register(void* _p, size_t _size, const char* _name);
	void dbg_unregister(void* _p);
	void dbg_check();

	size_t mem_usage();
	void mem_compact();
	void mem_counter_set(u32 _val) { stat_counter = _val; }
	u32 mem_counter_get() { return stat_counter; }

#ifdef DEBUG_MEMORY_NAME
    void mem_statistic(LPCSTR fn);
    void* mem_alloc(size_t size, const char* _name);
    void* mem_realloc(void* p, size_t size, const char* _name);
#else // DEBUG_MEMORY_NAME
	void* mem_alloc(size_t size);
	void* mem_realloc(void* p, size_t size);
#endif // DEBUG_MEMORY_NAME
	void mem_free(void* p);

	pso_MemCopy* mem_copy;
	pso_MemFill* mem_fill;
	pso_MemFill32* mem_fill32;
};

extern XRCORE_API xrMemory Memory;

#undef ZeroMemory
#undef CopyMemory
#undef FillMemory
#define ZeroMemory(a,b) Memory.mem_fill(a,0,b)
#define CopyMemory(a,b,c) memcpy(a,b,c) //. CopyMemory(a,b,c)
#define FillMemory(a,b,c) Memory.mem_fill(a,c,b)

// delete
#ifdef __BORLANDC__
#include "xrMemory_subst_borland.h"
#else
#include "xrMemory_subst_msvc.h"
#endif

// generic "C"-like allocations/deallocations
#ifdef DEBUG_MEMORY_NAME
#include "typeinfo"

template <class T>
IC T* xr_alloc(u32 count) { return (T*)Memory.mem_alloc(count*sizeof(T), typeid(T).name()); }
template <class T>
IC void xr_free(T*& P) { if (P) { Memory.mem_free((void*)P); P = NULL; }; }
IC void* xr_malloc(size_t size) { return Memory.mem_alloc(size, "xr_malloc"); }
IC void* xr_realloc(void* P, size_t size) { return Memory.mem_realloc(P, size, "xr_realloc"); }
#else // DEBUG_MEMORY_NAME
template <class T>
IC T* xr_alloc(u32 count) { return (T*)Memory.mem_alloc(count * sizeof(T)); }

template <class T>
IC void xr_free(T*& P)
{
	if (P)
	{
		Memory.mem_free((void*)P);
		P = NULL;
	};
}

IC void* xr_malloc(size_t size) { return Memory.mem_alloc(size); }
IC void* xr_realloc(void* P, size_t size) { return Memory.mem_realloc(P, size); }
#endif // DEBUG_MEMORY_NAME

XRCORE_API char* xr_strdup(const char* string);

#ifdef DEBUG_MEMORY_NAME
// Global new/delete override
# if !(defined(__BORLANDC__) || defined(NO_XRNEW))
IC void* operator new (size_t size) {return Memory.mem_alloc(size ? size : 1, "C++ NEW");}
IC void operator delete (void* p) { xr_free(p); }
IC void* operator new[](size_t size) { return Memory.mem_alloc(size ? size : 1, "C++ NEW"); }
IC void operator delete[](void* p) { xr_free(p); }
# endif
#else // DEBUG_MEMORY_NAME
# if !(defined(__BORLANDC__) || defined(NO_XRNEW))
IC void* operator new(size_t size) { return Memory.mem_alloc(size ? size : 1); }
IC void operator delete(void* p) { xr_free(p); }
IC void* operator new[](size_t size) { return Memory.mem_alloc(size ? size : 1); }
IC void operator delete[](void* p) { xr_free(p); }
# endif
#endif // DEBUG_MEMORY_MANAGER


// POOL-ing
const u32 mem_pools_count = 64;
const u32 mem_pools_ebase = 32;
const u32 mem_generic = mem_pools_count + 1;
extern MEMPOOL mem_pools[mem_pools_count];
extern BOOL mem_initialized;

XRCORE_API void vminfo(size_t* _free, size_t* reserved, size_t* committed);
XRCORE_API void log_vminfo();

// ---------------------------------------------------------------------------------------------
// MP fork, §5g — BIG-ALLOCATION TRACING. Naming the site behind the leak, not re-measuring it.
//
// §5b-§5f established the shape and stopped being able to say anything more: the dedicated server
// grows ~29 MB/min, 100% anonymous, and it arrives as a handful of DISCRETE events whose sizes on
// one 900 s run were 35, 36, 48, 66, 70 and **146 MB**, with the median 3-second sample growing
// **0 MB**. Six events carried 92% of the run. No further curve can improve on that — a curve
// cannot name a container — so the question becomes which allocation call produces a 146 MB block.
//
// WHY THIS HOOK CAN SEE THEM. Every `new`, `xr_malloc`, `xr_alloc` and `xr_realloc` in every module
// routes through xrMemory, and in this build `PURE_ALLOC` is defined in all five xrCore configs, so
// each one reaches `_aligned_malloc`/`_aligned_realloc`/`_aligned_free` at the top of its function
// with the pool machinery dead. One hook per operation covers the engine.
//
// WHY IT LOGS **FREES** TOO, WHICH IS THE POINT AND NOT AN EXTRA. The events are consistent with two
// different mechanisms that want different fixes: (a) a fresh 146 MB block that is simply never
// freed, or (b) a container GROWING 73 -> 146 MB, where the old block *is* freed but the allocator
// does not return it to the OS. MSVC's std::vector does not realloc — it allocates the new buffer,
// copies, frees the old — so under (b) a 146 MB alloc is accompanied by a ~73 MB free within
// microseconds, and under (a) it is not. RSS alone cannot tell these apart at 3-second resolution.
// The paired log can, on the first event it catches.
//
// THE CONTROL, BUILT IN RATHER THAN BOLTED ON (class 0). This hook can only see allocations that go
// through xrMemory. Anything using the CRT directly, a third-party library with its own allocator,
// or a raw VirtualAlloc is invisible to it — and an empty log would then read as "no big
// allocations happen", which is a confident wrong answer about the wrong population. So the dump
// reports RUNNING TOTALS of bytes allocated and freed above the threshold. Those totals are meant
// to be compared against the RSS curve the harness already produces: if they account for the
// observed growth, this hook covers the path and its records name the site. If they come back near
// zero while RSS climbs by hundreds of MB, then the growth is NOT xrMemory's and every hypothesis
// resting on an engine container is wrong before it is investigated further. Either answer is worth
// the build; the failure mode to avoid is reading silence as absence.
//
// Recording allocates NOTHING (a fixed static ring, an interlocked index) because a logger that
// allocates, called from inside the allocator, is unbounded recursion. Printing happens later, from
// coop_bigalloc_tick(), on a normal frame.
struct coop_bigalloc_rec
{
    u32    seq;         // monotonic, so a dump can tell "nothing new" from "the ring wrapped"
    u32    op;          // 0 = alloc, 1 = realloc, 2 = free
    u32    ms;          // Device timestamp is unavailable here; this is GetTickCount()
    size_t size;        // new size (0 for a free)
    size_t oldsize;     // previous size, for realloc/free — this is what pairs a grow with its drop
    void*  ra;          // _ReturnAddress() of the xrMemory entry point: the call site to symbolicate
};

// 0 disarms. Set from the command line by the game layer; the disarmed cost is one compare of a
// global against a size_t already in a register.
XRCORE_API extern size_t g_coop_bigalloc_min;
XRCORE_API void coop_bigalloc_arm(size_t min_bytes);
// Call from a normal frame (NOT from inside an allocation). Emits any records banked since the last
// call, plus the running totals that serve as the coverage control. Cheap and safe when disarmed.
XRCORE_API void coop_bigalloc_tick();

#endif // xrMemoryH
