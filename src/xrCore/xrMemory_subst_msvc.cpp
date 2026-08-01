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

    // NOTHING in here may allocate. It is called from inside mem_alloc; a logger that allocates
    // would recurse without bound. Fixed storage, interlocked index, no Msg, no shared_str.
    void coop_ba_record(u32 op, size_t size, size_t oldsize, void* ra)
    {
        if (op == 2)
        {
            _InterlockedExchangeAdd64(&s_ba_free_bytes, (LONG64)oldsize);
            _InterlockedExchangeAdd64(&s_ba_free_count, 1);
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

void coop_bigalloc_arm(size_t min_bytes)
{
    g_coop_bigalloc_min = min_bytes;
    if (min_bytes)
        Msg("* COOP(bigalloc): ARMED at %u bytes (%u MB). Every xrMemory alloc/realloc/free at or "
            "above this is recorded with its return address. Totals are printed alongside so they "
            "can be cross-footed against the harness RSS curve — if they do not account for the "
            "growth, the growth is not xrMemory's.",
            u32(min_bytes), u32(min_bytes / (1024 * 1024)));
}

void coop_bigalloc_tick()
{
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
            Msg("* COOP(bigalloc) #%u t=%u REALLOC %u MB <- %u MB (grew %d MB) ra=%p",
                r.seq, r.ms, u32(r.size / (1024 * 1024)), u32(r.oldsize / (1024 * 1024)),
                int((LONG64(r.size) - LONG64(r.oldsize)) / (1024 * 1024)), r.ra);
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
    Msg("* COOP(bigalloc) TOTALS: allocated %u MB in %u calls, freed %u MB in %u calls, net %d MB "
        "— cross-foot this net against the RSS curve; a net far below the observed growth means the "
        "leak is NOT going through xrMemory and no record above can explain it.",
        u32(s_ba_alloc_bytes / (1024 * 1024)), u32(s_ba_alloc_count),
        u32(s_ba_free_bytes / (1024 * 1024)), u32(s_ba_free_count),
        int((s_ba_alloc_bytes - s_ba_free_bytes) / (1024 * 1024)));
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
		if (g_coop_bigalloc_min && size >= g_coop_bigalloc_min && result)
			coop_ba_record(0, size, 0, _ReturnAddress());
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
		if (g_coop_bigalloc_min && P)
		{
			const size_t coop_ba_sz = _aligned_msize(P, PURE_MEMORY_ALIGNMENT, 0);
			if (coop_ba_sz >= g_coop_bigalloc_min)
				coop_ba_record(2, 0, coop_ba_sz, _ReturnAddress());
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
		if (g_coop_bigalloc_min && P)
			coop_ba_old = _aligned_msize(P, PURE_MEMORY_ALIGNMENT, 0);
		void* result = _aligned_realloc(P, size, PURE_MEMORY_ALIGNMENT);
		// Either side crossing the threshold is interesting: a big block shrinking is as much a
		// clue as one growing, and recording only growth would make the log agree with the
		// hypothesis by construction.
		if (g_coop_bigalloc_min && result &&
		    (size >= g_coop_bigalloc_min || coop_ba_old >= g_coop_bigalloc_min))
			coop_ba_record(1, size, coop_ba_old, _ReturnAddress());

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
