#include "stdafx.h"
#pragma hdrstop

#include "xrMemory_align.h"
#include "xrMemory_pure.h"

#ifndef __BORLANDC__

#ifndef DEBUG_MEMORY_MANAGER
# define debug_mode 0
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG_MEMORY_MANAGER
XRCORE_API void* g_globalCheckAddr = NULL;
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG_MEMORY_MANAGER
extern void save_stack_trace();
#endif // DEBUG_MEMORY_MANAGER

MEMPOOL mem_pools[mem_pools_count];

// MSVC
ICF u8* acc_header(void* P)
{
	u8* _P = (u8*)P;
	return _P - 1;
}

ICF u32 get_header(void* P) { return (u32)*acc_header(P); }
ICF u32 get_pool(size_t size)
{
	u32 pid = u32(size / mem_pools_ebase);
	if (pid >= mem_pools_count) return mem_generic;
	else return pid;
}

#ifdef PURE_ALLOC
const bool g_use_pure_alloc = true;
#endif // PURE_ALLOC

// ---------------------------------------------------------------------------------------------
// MP fork, §5g — big-allocation tracing. Rationale and the coverage control: xrMemory.h.
#include <intrin.h>

XRCORE_API size_t g_coop_bigalloc_min = 0;   // 0 = disarmed
XRCORE_API size_t g_coop_bigalloc_max = 0;   // 0 = no upper bound (§5j)
XRCORE_API size_t g_coop_allocsites_min = 0; // 0 = disarmed (§5j)
// The single fast-path compare for both instruments: the smaller armed floor, 0 when both are off.
XRCORE_API size_t g_coop_mem_track_min = 0;

namespace
{
    // A power of two so the wrap is a mask rather than a modulo — this runs inside the allocator.
    const u32 COOP_BA_RING = 512;
    coop_bigalloc_rec  s_ba_ring[COOP_BA_RING];
    volatile LONG      s_ba_seq = 0;          // total records EVER banked; also the ring cursor
    volatile LONG      s_ba_emitted = 0;      // how many the dump has already printed

    // The coverage control. 64-bit so a long run cannot wrap them; guarded by the same interlock
    // discipline as the ring. These are the numbers to compare against the harness RSS curve.
    volatile LONG64    s_ba_alloc_bytes = 0;
    volatile LONG64    s_ba_free_bytes = 0;
    volatile LONG64    s_ba_alloc_count = 0;
    volatile LONG64    s_ba_free_count = 0;
    // §5j FIX. A REALLOC used to be booked as an allocation of its NEW size with the old block it
    // released booked nowhere, so a container doubling 0->1->2->4->8 MB was recorded as 15 MB
    // allocated for 8 MB taken. Over §5i's seven autosave cycles that manufactured **+49 MB of net
    // growth that never happened** — the same order as the residual the run was sent to find, at
    // the one site whose repetition the pre-registration said would count as naming a leak. The
    // released side is now booked separately: net is alloc + realloc_new - realloc_old - free, and
    // the two realloc figures are printed rather than folded, so a reader can see the correction
    // instead of trusting it.
    volatile LONG64    s_ba_realloc_new_bytes = 0;
    volatile LONG64    s_ba_realloc_old_bytes = 0;
    volatile LONG64    s_ba_realloc_count = 0;

    // ---------------------------------------------------------------------------------------
    // §5j — the size-class histogram. Rationale: xrMemory.h.
    //
    // 48 classes covers 1 byte to 128 TB, so no allocation this process can make falls off the end
    // and gets silently attributed to the last bucket. Each is padded to its own cache line: every
    // thread in the engine allocates, and three hot counters sharing a line would turn this into a
    // contention experiment measuring itself.
    const u32 COOP_SC_CLASSES = 48;
    struct alignas(64) coop_sizeclass
    {
        volatile LONG64 net_bytes;
        volatile LONG64 n_alloc;
        volatile LONG64 n_free;
        char            _pad[64 - 3 * sizeof(LONG64)];
    };
    coop_sizeclass s_sc[COOP_SC_CLASSES];
    LONG64         s_sc_prev_net[COOP_SC_CLASSES];   // read only by the tick, which is one thread
    u32            s_sc_interval_ms = 30000;
    u32            s_sc_last_print = 0;

    // floor(log2(size)), i.e. the class whose range is [2^i, 2^(i+1)). Size 0 cannot occur on the
    // paths that call this (a zero-byte allocation never reaches the threshold compare), but it is
    // handled rather than assumed: a shift by 63 of a zero would index class 0 anyway.
    ICF u32 coop_sc_index(size_t size)
    {
        unsigned long idx = 0;
        if (!_BitScanReverse64(&idx, (unsigned __int64)size)) return 0;
        return (idx < COOP_SC_CLASSES) ? u32(idx) : (COOP_SC_CLASSES - 1);
    }

    // Called from inside the allocator, so the same rule as the ring: this allocates NOTHING.
    ICF void coop_sc_note(size_t size, size_t oldsize)
    {
        if (size)
        {
            coop_sizeclass& b = s_sc[coop_sc_index(size)];
            _InterlockedExchangeAdd64(&b.net_bytes, (LONG64)size);
            _InterlockedExchangeAdd64(&b.n_alloc, 1);
        }
        if (oldsize)
        {
            coop_sizeclass& b = s_sc[coop_sc_index(oldsize)];
            _InterlockedExchangeAdd64(&b.net_bytes, -(LONG64)oldsize);
            _InterlockedExchangeAdd64(&b.n_free, 1);
        }
    }

    // NOTHING in here may allocate. It is called from inside mem_alloc; a logger that allocates
    // would recurse without bound. Fixed storage, interlocked index, no Msg, no shared_str.
    void coop_ba_record(u32 op, size_t size, size_t oldsize, void* ra)
    {
        if (op == 2)
        {
            _InterlockedExchangeAdd64(&s_ba_free_bytes, (LONG64)oldsize);
            _InterlockedExchangeAdd64(&s_ba_free_count, 1);
        }
        else if (op == 1)
        {
            // §5j: BOTH sides of a realloc. Booking only the new size is what produced §5i's
            // phantom +7 MB per autosave cycle.
            _InterlockedExchangeAdd64(&s_ba_realloc_new_bytes, (LONG64)size);
            _InterlockedExchangeAdd64(&s_ba_realloc_old_bytes, (LONG64)oldsize);
            _InterlockedExchangeAdd64(&s_ba_realloc_count, 1);
        }
        else
        {
            _InterlockedExchangeAdd64(&s_ba_alloc_bytes, (LONG64)size);
            _InterlockedExchangeAdd64(&s_ba_alloc_count, 1);
        }
        const LONG n = _InterlockedIncrement(&s_ba_seq) - 1;
        coop_bigalloc_rec& r = s_ba_ring[u32(n) & (COOP_BA_RING - 1)];
        r.seq = u32(n);
        r.op = op;
        r.ms = GetTickCount();
        r.size = size;
        r.oldsize = oldsize;
        r.ra = ra;
    }
}

// §5j: the allocator's single entry point into both instruments. Kept out of line deliberately —
// the hot path is the `g_coop_mem_track_min` compare at the call site, and everything past it has
// already qualified. NOTHING here allocates: it is called from inside mem_alloc, and a logger that
// allocates would recurse without bound.
void coop_mem_note(u32 op, size_t size, size_t oldsize, void* ra)
{
    if (g_coop_allocsites_min)
    {
        // The histogram takes both sides of every operation, so a realloc's released block lands
        // in ITS class rather than being netted against the new one. Sizes below the floor are
        // dropped on each side independently — a 2 MB block shrinking to 512 bytes with a 1 KB
        // floor must still book its release, or the class would grow forever on paper.
        const size_t s = (size >= g_coop_allocsites_min) ? size : 0;
        const size_t o = (oldsize >= g_coop_allocsites_min) ? oldsize : 0;
        if (s || o) coop_sc_note(s, o);
    }
    if (g_coop_bigalloc_min)
    {
        const size_t hi = g_coop_bigalloc_max;
        const bool in_new = size >= g_coop_bigalloc_min && (!hi || size <= hi);
        const bool in_old = oldsize >= g_coop_bigalloc_min && (!hi || oldsize <= hi);
        if (in_new || in_old) coop_ba_record(op, size, oldsize, ra);
    }
}

// §5j: the fast path at every hook is one compare against this, so arming either instrument costs
// the other nothing. It is the SMALLER of the two armed floors, treating a disarmed one as
// infinite — get this backwards and the lower instrument silently never fires.
static void coop_mem_recompute_floor()
{
    const size_t a = g_coop_bigalloc_min ? g_coop_bigalloc_min : ~size_t(0);
    const size_t b = g_coop_allocsites_min ? g_coop_allocsites_min : ~size_t(0);
    const size_t lo = (a < b) ? a : b;
    g_coop_mem_track_min = (lo == ~size_t(0)) ? 0 : lo;
}

void coop_allocsites_arm(size_t min_bytes, u32 print_interval_ms)
{
    g_coop_allocsites_min = min_bytes;
    if (print_interval_ms) s_sc_interval_ms = print_interval_ms;
    s_sc_last_print = GetTickCount();
    coop_mem_recompute_floor();
    if (min_bytes)
        Msg("* COOP(allocsites): ARMED at %u bytes, printing every %u ms. Bytes are NETTED per "
            "power-of-two size class (alloc adds, free subtracts, realloc does both) — a leak of "
            "many small blocks is ONE CLASS whose net climbs every interval. It cannot name a call "
            "site by design (that needs a live-block map on the hot path); once a class is named, "
            "point -coop_bigalloc/-coop_bigalloc_max at that band to get return addresses for a "
            "population small enough to print.",
            u32(min_bytes), print_interval_ms ? print_interval_ms : s_sc_interval_ms);
}

void coop_bigalloc_arm(size_t min_bytes, size_t max_bytes)
{
    g_coop_bigalloc_min = min_bytes;
    g_coop_bigalloc_max = max_bytes;
    coop_mem_recompute_floor();
    if (min_bytes && max_bytes)
        Msg("* COOP(bigalloc): the per-event ring is bounded ABOVE at %u bytes (%u MB) as well — "
            "only blocks in [%u, %u] are recorded.",
            u32(max_bytes), u32(max_bytes / (1024 * 1024)), u32(min_bytes), u32(max_bytes));
    if (min_bytes)
        Msg("* COOP(bigalloc): ARMED at %u bytes (%u MB). Every xrMemory alloc/realloc/free at or "
            "above this is recorded with its return address. Totals are printed alongside so they "
            "can be cross-footed against the harness RSS curve — if they do not account for the "
            "growth, the growth is not xrMemory's.",
            u32(min_bytes), u32(min_bytes / (1024 * 1024)));
}

// §5j. The DELTA is the reading and the cumulative is context: a cumulative net is dominated by
// boot and says nothing about steady state, which is the mistake every whole-run MB/min figure on
// this axis made before §5f. A class is printed when it moved in this interval OR is non-zero
// overall, so a class that leaks and then stops does not vanish from the report.
static void coop_sc_tick()
{
    const u32 now = GetTickCount();
    if (u32(now - s_sc_last_print) < s_sc_interval_ms) return;
    const u32 elapsed = now - s_sc_last_print;
    s_sc_last_print = now;

    LONG64 total_delta = 0, total_net = 0;
    for (u32 i = 0; i < COOP_SC_CLASSES; ++i)
    {
        const LONG64 net = s_sc[i].net_bytes;
        total_delta += net - s_sc_prev_net[i];
        total_net += net;
    }
    Msg("* COOP(allocsites) t=%u interval=%u ms — NET %d KB this interval, %d KB since armed "
        "(per size class below; the interval column is the one that names a leak)",
        now, elapsed, int(total_delta / 1024), int(total_net / 1024));

    for (u32 i = 0; i < COOP_SC_CLASSES; ++i)
    {
        const LONG64 net = s_sc[i].net_bytes;
        const LONG64 delta = net - s_sc_prev_net[i];
        s_sc_prev_net[i] = net;
        if (!delta && !net) continue;
        Msg("*   [%12I64u .. %12I64u B] delta %+9d KB | net %+9d KB | allocs %I64d frees %I64d",
            (u64(1) << i), (u64(2) << i) - 1,
            int(delta / 1024), int(net / 1024), s_sc[i].n_alloc, s_sc[i].n_free);
    }
}

void coop_bigalloc_tick()
{
    // Ordered so the histogram runs even when the per-event ring is disarmed — the two are
    // independent instruments and §5j's whole point is being able to run the cheap one alone.
    if (g_coop_allocsites_min) coop_sc_tick();
    if (!g_coop_bigalloc_min) return;

    const LONG banked = s_ba_seq;
    LONG shown = s_ba_emitted;
    if (banked == shown) return;

    // If more than the ring's worth arrived since the last tick, records were overwritten. Say so
    // rather than printing the survivors as though they were all of them — a silently truncated
    // log is the same defect as a silently truncated sweep.
    if (u32(banked - shown) > COOP_BA_RING)
    {
        Msg("! COOP(bigalloc): %u records OVERWRITTEN before they could be printed (ring holds %u). "
            "The list below is the tail, not the set.", u32(banked - shown) - COOP_BA_RING, COOP_BA_RING);
        shown = banked - LONG(COOP_BA_RING);
    }

    for (LONG i = shown; i < banked; ++i)
    {
        const coop_bigalloc_rec& r = s_ba_ring[u32(i) & (COOP_BA_RING - 1)];
        switch (r.op)
        {
        case 0:
            Msg("* COOP(bigalloc) #%u t=%u ALLOC   %u MB (%u bytes) ra=%p",
                r.seq, r.ms, u32(r.size / (1024 * 1024)), u32(r.size), r.ra);
            break;
        case 1:
            // The pairing that separates "a fresh block nobody frees" from "a container growing and
            // the old block not going back to the OS".
            // §5j: the MB fields are kept so existing readers and banked logs still parse, and
            // the EXACT bytes are appended because rounding a chain's first step to "0 MB" is
            // what made dev/harness/bigalloc_net.py carry a sub-MB error it could bound but not
            // remove. A grow of 900 KB and a grow of nothing printed identically.
            Msg("* COOP(bigalloc) #%u t=%u REALLOC %u MB <- %u MB (grew %d MB) "
                "[exact %u <- %u bytes] ra=%p",
                r.seq, r.ms, u32(r.size / (1024 * 1024)), u32(r.oldsize / (1024 * 1024)),
                int((LONG64(r.size) - LONG64(r.oldsize)) / (1024 * 1024)),
                u32(r.size), u32(r.oldsize), r.ra);
            break;
        default:
            Msg("* COOP(bigalloc) #%u t=%u FREE    %u MB (%u bytes) ra=%p",
                r.seq, r.ms, u32(r.oldsize / (1024 * 1024)), u32(r.oldsize), r.ra);
            break;
        }
    }
    s_ba_emitted = banked;

    // THE CONTROL LINE. Printed on every tick that had records, deliberately next to them: a reader
    // must not be able to quote a call site without also seeing whether the traced allocations
    // account for the process's actual growth.
    //
    // §5j CORRECTION. This line used to book a REALLOC as an allocation of its new size and the
    // block it released as nothing, so net drifted upward by the whole history of every growing
    // container: §5i's seven autosave cycles booked +49 MB that never happened, and the
    // pre-registered refutation fired on it. The realloc figures are now printed SEPARATELY rather
    // than folded in, so the correction is visible in the log instead of having to be trusted.
    Msg("* COOP(bigalloc) TOTALS: allocated %u MB in %u calls, freed %u MB in %u calls, "
        "realloc %u MB <- %u MB in %u calls, NET %d MB "
        "— cross-foot this net against the RSS curve; a net far below the observed growth means the "
        "leak is NOT going through xrMemory and no record above can explain it. NET counts a realloc "
        "as (new - old), which is the memory it actually took.",
        u32(s_ba_alloc_bytes / (1024 * 1024)), u32(s_ba_alloc_count),
        u32(s_ba_free_bytes / (1024 * 1024)), u32(s_ba_free_count),
        u32(s_ba_realloc_new_bytes / (1024 * 1024)), u32(s_ba_realloc_old_bytes / (1024 * 1024)),
        u32(s_ba_realloc_count),
        int((s_ba_alloc_bytes + s_ba_realloc_new_bytes - s_ba_realloc_old_bytes - s_ba_free_bytes)
            / (1024 * 1024)));
}

#define PURE_MEMORY_FILL_ZERO
#define PURE_MEMORY_ALIGNMENT 1 << 4

void* xrMemory::mem_alloc(size_t size
# ifdef DEBUG_MEMORY_NAME
                          , const char* _name
# endif // DEBUG_MEMORY_NAME
)
{
	stat_calls++;

#ifdef PURE_ALLOC
	if (g_use_pure_alloc)
	{
		//void* result = malloc(size);
		void* result = _aligned_malloc(size, PURE_MEMORY_ALIGNMENT);
		// §5g. Recorded AFTER the allocation so a failed one is not counted as growth.
		// §5j: one compare against the shared floor, then both instruments decide for themselves.
		if (g_coop_mem_track_min && size >= g_coop_mem_track_min && result)
			coop_mem_note(0, size, 0, _ReturnAddress());
#ifdef PURE_MEMORY_FILL_ZERO
		if (result)
			memset(result, 0, size);
#endif // PURE_MEMORY_FILL_ZERO

#ifdef USE_MEMORY_MONITOR
        memory_monitor::monitor_alloc(result, size, _name);
#endif // USE_MEMORY_MONITOR
		return (result);
	}
#endif // PURE_ALLOC

#ifdef DEBUG_MEMORY_MANAGER
    if (mem_initialized) debug_cs.Enter();
#endif // DEBUG_MEMORY_MANAGER

	u32 _footer = debug_mode ? 4 : 0;
	void* _ptr = 0;

	//
	if (!mem_initialized /*|| debug_mode*/)
	{
		// generic
		// Igor: Reserve 1 byte for xrMemory header
		void* _real = xr_aligned_offset_malloc(1 + size + _footer, 16, 0x1);
		//void* _real = xr_aligned_offset_malloc (size + _footer, 16, 0x1);
		_ptr = (void*)(((u8*)_real) + 1);
		*acc_header(_ptr) = mem_generic;
	}
	else
	{
#ifdef DEBUG_MEMORY_MANAGER
        save_stack_trace();
#endif // DEBUG
		// accelerated
		// Igor: Reserve 1 byte for xrMemory header
		u32 pool = get_pool(1 + size + _footer);
		//u32 pool = get_pool (size+_footer);
		if (mem_generic == pool)
		{
			// generic
			// Igor: Reserve 1 byte for xrMemory header
			void* _real = xr_aligned_offset_malloc(1 + size + _footer, 16, 0x1);
			//void* _real = xr_aligned_offset_malloc (size + _footer,16,0x1);
			_ptr = (void*)(((u8*)_real) + 1);
			*acc_header(_ptr) = mem_generic;
		}
		else
		{
			// pooled
			// Igor: Reserve 1 byte for xrMemory header
			// Already reserved when getting pool id
			void* _real = mem_pools[pool].create();
			_ptr = (void*)(((u8*)_real) + 1);
			*acc_header(_ptr) = (u8)pool;
		}
	}

#ifdef DEBUG_MEMORY_MANAGER
    if (debug_mode) dbg_register(_ptr, size, _name);
    if (mem_initialized) debug_cs.Leave();
    //if(g_globalCheckAddr==_ptr){
	// __asm int 3;
	//}
	//if (_name && (0==strcmp(_name,"class ISpatial *")) && (size==376))
	//{
	// __asm int 3;
	//}
#endif // DEBUG_MEMORY_MANAGER
#ifdef USE_MEMORY_MONITOR
    memory_monitor::monitor_alloc(_ptr, size, _name);
#endif // USE_MEMORY_MONITOR
	memset(_ptr, 0, size);
	return _ptr;
}

void xrMemory::mem_free(void* P)
{
	stat_calls++;
#ifdef USE_MEMORY_MONITOR
    memory_monitor::monitor_free(P);
#endif // USE_MEMORY_MONITOR

#ifdef PURE_ALLOC
	if (g_use_pure_alloc)
	{
		// §5g: the size must be read BEFORE the free — afterwards the block is gone and any
		// number taken from it is whatever the allocator left behind.
		if (g_coop_mem_track_min && P)
		{
			const size_t coop_ba_sz = _aligned_msize(P, PURE_MEMORY_ALIGNMENT, 0);
			if (coop_ba_sz >= g_coop_mem_track_min)
				coop_mem_note(2, 0, coop_ba_sz, _ReturnAddress());
		}
		//free(P);
		_aligned_free(P);
		return;
	}
#endif // PURE_ALLOC

#ifdef DEBUG_MEMORY_MANAGER
    if (g_globalCheckAddr == P)
        __asm int 3;
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG_MEMORY_MANAGER
    if (mem_initialized) debug_cs.Enter();
#endif // DEBUG_MEMORY_MANAGER
	if (debug_mode) dbg_unregister(P);
	u32 pool = get_header(P);
	void* _real = (void*)(((u8*)P) - 1);
	if (mem_generic == pool)
	{
		// generic
		xr_aligned_free(_real);
	}
	else
	{
		// pooled
		VERIFY2(pool < mem_pools_count, "Memory corruption");
		mem_pools[pool].destroy(_real);
	}
#ifdef DEBUG_MEMORY_MANAGER
    if (mem_initialized) debug_cs.Leave();
#endif // DEBUG_MEMORY_MANAGER
}

extern BOOL g_bDbgFillMemory;

void* xrMemory::mem_realloc(void* P, size_t size
#ifdef DEBUG_MEMORY_NAME
                            , const char* _name
#endif // DEBUG_MEMORY_NAME
)
{
	stat_calls++;

	if (0 == P)
	{
		return mem_alloc(size
# ifdef DEBUG_MEMORY_NAME
			, _name
# endif // DEBUG_MEMORY_NAME
		);
	}

#ifdef PURE_ALLOC
	if (g_use_pure_alloc)
	{
#ifdef PURE_MEMORY_FILL_ZERO
		size_t old_size = P ? _aligned_msize(P, PURE_MEMORY_ALIGNMENT, 0) : 0;
#endif // PURE_MEMORY_FILL_ZERO

		//void* result = realloc(P, size);
		// §5g needs the OLD size, and only the arm path pays for reading it.
		size_t coop_ba_old = 0;
		if (g_coop_mem_track_min && P)
			coop_ba_old = _aligned_msize(P, PURE_MEMORY_ALIGNMENT, 0);
		void* result = _aligned_realloc(P, size, PURE_MEMORY_ALIGNMENT);
		// Either side crossing the threshold is interesting: a big block shrinking is as much a
		// clue as one growing, and recording only growth would make the log agree with the
		// hypothesis by construction.
		if (g_coop_mem_track_min && result &&
		    (size >= g_coop_mem_track_min || coop_ba_old >= g_coop_mem_track_min))
			coop_mem_note(1, size, coop_ba_old, _ReturnAddress());

#ifdef PURE_MEMORY_FILL_ZERO
		if (result && size > old_size)
			memset((u8*)result + old_size, 0, size - old_size);
#endif // PURE_MEMORY_FILL_ZERO

# ifdef USE_MEMORY_MONITOR
        memory_monitor::monitor_free(P);
        memory_monitor::monitor_alloc(result, size, _name);
# endif // USE_MEMORY_MONITOR
		return (result);
	}
#endif // PURE_ALLOC

#ifdef DEBUG_MEMORY_MANAGER
    if (g_globalCheckAddr == P)
        __asm int 3;
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG_MEMORY_MANAGER
    if (mem_initialized) debug_cs.Enter();
#endif // DEBUG_MEMORY_MANAGER
	u32 p_current = get_header(P);
	// Igor: Reserve 1 byte for xrMemory header
	u32 p_new = get_pool(1 + size + (debug_mode ? 4 : 0));
	//u32 p_new = get_pool (size+(debug_mode?4:0));
	u32 p_mode;

	if (mem_generic == p_current)
	{
		if (p_new < p_current) p_mode = 2;
		else p_mode = 0;
	}
	else p_mode = 1;

	void* _real = (void*)(((u8*)P) - 1);
	void* _ptr = NULL;
	if (0 == p_mode)
	{
		u32 _footer = debug_mode ? 4 : 0;
#ifdef DEBUG_MEMORY_MANAGER
        if (debug_mode)
        {
            g_bDbgFillMemory = false;
            dbg_unregister(P);
            g_bDbgFillMemory = true;
        }
#endif // DEBUG_MEMORY_MANAGER
		// Igor: Reserve 1 byte for xrMemory header
		void* _real2 = xr_aligned_offset_realloc(_real, 1 + size + _footer, 16, 0x1);
		//void* _real2 = xr_aligned_offset_realloc (_real,size+_footer,16,0x1);
		_ptr = (void*)(((u8*)_real2) + 1);
		*acc_header(_ptr) = mem_generic;
#ifdef DEBUG_MEMORY_MANAGER
        if (debug_mode) dbg_register(_ptr, size, _name);
#endif // DEBUG_MEMORY_MANAGER
#ifdef USE_MEMORY_MONITOR
        memory_monitor::monitor_free(P);
        memory_monitor::monitor_alloc(_ptr, size, _name);
#endif // USE_MEMORY_MONITOR
	}
	else if (1 == p_mode)
	{
		// pooled realloc
		R_ASSERT2(p_current < mem_pools_count, "Memory corruption");
		u32 s_current = mem_pools[p_current].get_element();
		u32 s_dest = (u32)size;
		void* p_old = P;

		void* p_new = mem_alloc(size
#ifdef DEBUG_MEMORY_NAME
                                , _name
#endif // DEBUG_MEMORY_NAME
		);
		// Igor: Reserve 1 byte for xrMemory header
		// Don't bother in this case?
		mem_copy(p_new, p_old, _min(s_current - 1, s_dest));
		//mem_copy (p_new,p_old,_min(s_current,s_dest));
		mem_free(p_old);
		_ptr = p_new;
	}
	else if (2 == p_mode)
	{
		// relocate into another mmgr(pooled) from real
		void* p_old = P;
		void* p_new = mem_alloc(size
# ifdef DEBUG_MEMORY_NAME
                                , _name
# endif // DEBUG_MEMORY_NAME
		);
		mem_copy(p_new, p_old, (u32)size);
		mem_free(p_old);
		_ptr = p_new;
	}

#ifdef DEBUG_MEMORY_MANAGER
    if (mem_initialized) debug_cs.Leave();

    if (g_globalCheckAddr == _ptr)
        __asm int 3;
#endif // DEBUG_MEMORY_MANAGER

	return _ptr;
}

#endif // __BORLANDC__
