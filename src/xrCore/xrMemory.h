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
// §5j: an UPPER bound on the per-event ring, so a band can be traced once one is named. 0 = no
// upper bound. Without it, narrowing the search means lowering the floor, which drowns the log in
// the very traffic the leak has to be measured against (§5i gate D).
XRCORE_API extern size_t g_coop_bigalloc_max;
XRCORE_API void coop_bigalloc_arm(size_t min_bytes, size_t max_bytes);

// -------------------------------------------------------------------------------------------
// §5j — THE SIZE-CLASS HISTOGRAM, and why the per-event ring above cannot answer what is left.
//
// §5i measured, on a corrected reading of that ring, that **no allocation of 1 MB or more leaks at
// all** in 720 s of steady state: every record is matched byte-for-byte by its free. Meanwhile the
// server still grows ~1.6 MB/min, of which the LogFile container explains ~0.19. So the remaining
// ~1.4 MB/min is sub-megabyte, and the ring is the wrong instrument for it by construction — one
// printed line per event, against a population that allocates constantly, would make the log
// larger than the leak (§5i's gate D exists precisely because the tracer's own output feeds the
// LogFile leak it is measuring).
//
// So: no per-event output at all. Bytes are NETTED into a fixed table of power-of-two size classes
// — allocation adds, free subtracts, realloc does both into its two classes — and the table is
// printed on an interval. **A leak of many small blocks shows up as one size class whose net
// climbs every interval**, which is a shape no amount of RSS curve can produce, and it needs no
// per-block bookkeeping: the free path already knows the block's exact size (`_aligned_msize`),
// so the subtraction lands in the same class the addition did.
//
// WHAT IT DELIBERATELY CANNOT DO. It cannot name a call site — netting per site would need a
// map from every live block to its allocating return address, which is a per-allocation hash
// insert and erase on the hot path, and this project has already had one instrument cost its own
// measurement (§4d, the VM audit at ~10x slowdown). Naming the SIZE is enough to then point the
// ring at that band with `-coop_bigalloc`/`-coop_bigalloc_max` and get return addresses for a
// population small enough to print. Two cheap steps instead of one expensive one.
//
// BOTH HALVES OF THAT PARAGRAPH WERE WRONG, AND THE RUN THAT USED IT SAID SO. See §5k below.
//
// THE INTERVAL DELTA IS THE READING, not the cumulative. A cumulative net is dominated by boot
// and says nothing about steady state, which is the same mistake the whole-run MB/min figure made
// in §5e/§5f/§5h. Both are printed, with the delta first.
XRCORE_API extern size_t g_coop_allocsites_min;
XRCORE_API void coop_allocsites_arm(size_t min_bytes, u32 print_interval_ms);

// -------------------------------------------------------------------------------------------
// §5k — NET BYTES PER RETURN ADDRESS, inside an armed band. Built because §5j's own run refuted
// both halves of the paragraph above.
//
// §5j armed the histogram at ONE BYTE — every allocation in the engine taking two atomics — and
// the server did the same work per second as the unarmed control on all four proxies (RSS samples
// 0.99, vmwatch 0.99, MP_INV 1.01, log lines 1.02). So the §4d fear was misplaced: what cost 10x
// there was per-event PRINTING, not per-event RECORDING. The per-allocation budget is real.
//
// And the two-step's second step turned out to be impossible as designed. §5j named the band —
// `128 .. 255 B`, +2.17 MB and +12,062 live blocks over a 630 s steady window, climbing on 20 of
// 21 intervals — and that band takes ~240 allocations/second. Pointing the per-event ring at it
// would print ~14,000 lines per 30 s interval against a log that writes 12 lines/s: §5i's gate D
// confound multiplied by forty. **The ring cannot be pointed at the band the histogram found.**
//
// So the live-block map is built after all, but only inside the band, where it is cheap:
//
//   * a fixed open-addressed map from live block POINTER -> (allocating return address, size),
//     sized once at arm time and never grown;
//   * a fixed table of RETURN ADDRESSES, each netting bytes and counting allocs/frees;
//   * on free, the block's ALLOCATING site is looked up and debited — which is the whole point,
//     because a free's own return address is the deallocation site and netting against it would
//     produce a table of large positives and large negatives that says nothing about leaking.
//
// WHAT MUST BE PRINTED FOR THE READING TO MEAN ANYTHING, and is therefore printed every interval:
// map occupancy and its high-water mark, insert overflows, site-table overflows, and frees whose
// block was NOT in the map (every block allocated before arming, plus anything an overflow lost).
// A table that silently drops sites reports a subset as if it were the set — and the subset always
// looks tidier than the truth.
XRCORE_API extern size_t g_coop_rasites_min;
XRCORE_API extern size_t g_coop_rasites_max;
XRCORE_API void coop_rasites_arm(size_t min_bytes, size_t max_bytes, u32 print_interval_ms, u32 top_n);

// -------------------------------------------------------------------------------------------
// §5p — NET BYTES PER 1 MiB OF ADDRESS SPACE, and it exists to answer the one question `/proc`
// cannot.
//
// §5l-§5o measured, from per-mapping smaps, that essentially all of the server's steady RSS growth
// lands in ONE anonymous arena at a time, from a family of identical 16,580,608-byte reservations
// that are filled and retired in turn. §5n then measured that only 26% of that growth is
// `xrMemory` live bytes with the audit probe off — the other 0.40-0.60 MB/min is committed,
// RESIDENT, written memory that no allocator instrument accounts for. `/proc` does not record
// which allocator owns an anonymous page, so smaps can say WHERE the growth is and never WHOSE.
//
// This says whose, for the only allocator we can instrument: it nets bytes per 1 MiB slice of
// address space, so a region's `xrMemory` net can be laid straight against that region's committed
// growth in the smaps table. **A region whose arena commits while this stays flat is not
// `xrMemory`'s.**
//
// It is cheap for a reason worth stating, because §5k's design note said the opposite about return
// addresses: the bucket is a function of the POINTER, and the free path HAS the pointer. A block's
// address does not change between allocation and release, so alloc-adds and free-subtracts land in
// the same bucket by construction — no live-block map, no per-block bookkeeping, one hash and two
// atomics per allocation. That is the same budget §5j measured as free at a one-byte floor.
XRCORE_API extern size_t g_coop_addrmap_min;
XRCORE_API void coop_addrmap_arm(size_t min_bytes, u32 print_interval_ms, u32 top_n,
                                 u32 census_every);

// The single fast-path compare shared by all three instruments: the SMALLEST armed floor, 0 when
// they are all disarmed. One global read per allocation whether or not anything is armed, which is
// what the hot path pays in a normal build.
XRCORE_API extern size_t g_coop_mem_track_min;
// The one entry point the allocator calls. Dispatches to the histogram, the per-event ring and/or
// the return-address table according to what is armed and what the sizes are.
// op: 0 alloc, 1 realloc, 2 free. `ptr` is the resulting block (alloc/realloc), `oldptr` the one
// being released (free/realloc) — §5k needs the pointers, the older two instruments ignore them.
XRCORE_API void coop_mem_note(u32 op, size_t size, size_t oldsize, void* ra, void* ptr, void* oldptr);

// Call from a normal frame (NOT from inside an allocation). Emits any records banked since the last
// call, plus the running totals that serve as the coverage control, plus the §5j histogram when
// its interval is up. Cheap and safe when disarmed.
XRCORE_API void coop_bigalloc_tick();

#endif // xrMemoryH
