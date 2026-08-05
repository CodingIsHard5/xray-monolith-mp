#include "stdafx.h"
#pragma hdrstop

#include "xrMemory_align.h"
#include "xrMemory_pure.h"
#include "coop_pool.h"		// §6c: the ownership discriminator guarding mem_free/mem_realloc

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
#include <limits.h>

XRCORE_API size_t g_coop_bigalloc_min = 0;   // 0 = disarmed
XRCORE_API size_t g_coop_bigalloc_max = 0;   // 0 = no upper bound (§5j)
XRCORE_API size_t g_coop_allocsites_min = 0; // 0 = disarmed (§5j)
XRCORE_API size_t g_coop_rasites_min = 0;    // 0 = disarmed (§5k)
XRCORE_API size_t g_coop_rasites_max = 0;    // 0 = no upper bound (§5k)
XRCORE_API size_t g_coop_addrmap_min = 0;    // 0 = disarmed (§5p)
// The single fast-path compare for all three instruments: the smallest armed floor, 0 when they
// are all off.
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

    // ---------------------------------------------------------------------------------------
    // §5p — NET BYTES PER 1 MiB OF ADDRESS SPACE. Rationale: xrMemory.h.
    //
    // The one thing §5k needed a live-block map for — remembering something about a block so the
    // free can undo it — this does not need at all, and that is the whole reason it is cheap: the
    // bucket is a function of the POINTER, and the free path has the pointer. A block's address
    // does not change between its allocation and its release, so `alloc adds to bucket(ptr)` and
    // `free subtracts from bucket(oldptr)` net exactly, with no bookkeeping in between.
    const u32 COOP_AM_SLOTS = 8192;         // ~8 GB of touched address space at 1 MiB granularity
    const u32 COOP_AM_PROBE = 16;
    struct alignas(64) coop_ambucket
    {
        volatile LONG64 key;                // (address >> 20) + 1, so 0 can mean empty
        volatile LONG64 net_bytes;
        volatile LONG64 n_alloc;
        volatile LONG64 n_free;
        char            _pad[64 - 4 * sizeof(LONG64)];
    };
    coop_ambucket s_am[COOP_AM_SLOTS];
    LONG64        s_am_prev_net[COOP_AM_SLOTS];
    volatile LONG64 s_am_overflow = 0;
    u32           s_am_interval_ms = 30000;
    u32           s_am_last_print = 0;
    u32           s_am_top_n = 16;
    // §5s — the CENSUS. Every n-th interval the tick prints EVERY non-empty bucket instead of the
    // top N. Rationale (xrMemory.h): six runs have shown this system's run-to-run variance exceeds
    // the effects being chased, so the next reading must be a static census rather than a rate.
    u32           s_am_census_every = 0;   // 0 = never
    u32           s_am_ticks = 0;

    ICF u32 coop_am_hash(LONG64 key)
    {
        u64 h = (u64)key * 0x9E3779B97F4A7C15ull;
        return u32(h >> 45) & (COOP_AM_SLOTS - 1);
    }

    // Called from inside the allocator: fixed storage, no logging, no allocation.
    ICF void coop_am_add(void* p, LONG64 delta, bool is_alloc)
    {
        if (!p) return;
        const LONG64 key = (LONG64)((u64)p >> 20) + 1;
        u32 h = coop_am_hash(key);
        for (u32 i = 0; i < COOP_AM_PROBE; ++i)
        {
            coop_ambucket& b = s_am[(h + i) & (COOP_AM_SLOTS - 1)];
            LONG64 cur = b.key;
            if (cur != key)
            {
                if (cur != 0) continue;
                cur = _InterlockedCompareExchange64(&b.key, key, 0);
                if (cur != 0 && cur != key) continue;
            }
            _InterlockedExchangeAdd64(&b.net_bytes, delta);
            _InterlockedExchangeAdd64(is_alloc ? &b.n_alloc : &b.n_free, 1);
            return;
        }
        // The table is full for this key's probe run. Counted rather than folded into a
        // neighbour: a bucket that absorbed someone else's bytes is worse than a missing one,
        // because it looks like a region that grew.
        _InterlockedExchangeAdd64(&s_am_overflow, 1);
    }

    // ---------------------------------------------------------------------------------------
    // §5k — the live-block map and the per-return-address table. Rationale: xrMemory.h.
    //
    // Both are fixed storage. The site table is static (256 KB, one cache line per site so two hot
    // allocation sites cannot false-share). The block map is VirtualAlloc'd at arm time, because
    // it is sized for the band's live population and a 6 MB static array would be paid for by
    // every build whether or not anyone ever arms this.
    const u32 COOP_RA_SITES = 4096;                 // power of two: probe mask, not modulo
    const u32 COOP_RA_PROBE = 32;                   // give up after this many; count the give-up
    struct alignas(64) coop_rasite
    {
        volatile LONG64 ra;         // 0 = empty. Claimed by CAS; never released.
        volatile LONG64 net_bytes;
        volatile LONG64 n_alloc;
        volatile LONG64 n_free;
        char            _pad[64 - 4 * sizeof(LONG64)];
    };
    coop_rasite s_ra[COOP_RA_SITES];
    LONG64      s_ra_prev_net[COOP_RA_SITES];       // read only by the tick, which is one thread

    // The block map. `ptr` 0 = empty, 1 = tombstone (a slot whose block was freed). Tombstones are
    // required rather than tidy: nulling a slot would cut the probe chain of every key that hashed
    // before it, and those keys would then look like blocks that were never allocated.
    const LONG64 COOP_RA_TOMB = 1;
    struct coop_blk
    {
        volatile LONG64 ptr;
        void*           ra;
        u32             size;
        u32             _pad;
    };
    coop_blk*       s_rb = NULL;
    u32             s_rb_mask = 0;
    u32             s_rb_capacity = 0;
    volatile LONG64 s_rb_live = 0;                  // slots currently holding a block
    volatile LONG64 s_rb_live_hw = 0;               // high-water mark of the above
    // Tombstones are NOT free space: a probe walks through them, so the load factor that decides
    // whether inserts start overflowing is live+tombstones, not live. Counting only `live` would
    // report a map that is 12% full when it is 34% occupied — an occupancy gate that cannot fire
    // is worse than none, because it reads as a passed check.
    volatile LONG64 s_rb_tomb = 0;
    volatile LONG64 s_rb_insert_overflow = 0;       // probe ran out: the block is NOT tracked
    volatile LONG64 s_rb_unknown_free = 0;          // freed a block the map never held
    volatile LONG64 s_rb_unknown_bytes = 0;
    volatile LONG64 s_ra_site_overflow = 0;         // site table full: the bytes are NOT attributed
    u32             s_ra_interval_ms = 30000;
    u32             s_ra_last_print = 0;
    u32             s_ra_top_n = 12;

    // Fibonacci hashing on the pointer, shifted past the allocator's alignment bits — the low four
    // bits of every block are zero here (PURE_MEMORY_ALIGNMENT is 16), and hashing them would put
    // every key into one sixteenth of the table.
    ICF u32 coop_ra_hash(LONG64 key, u32 mask)
    {
        u64 h = (u64)key >> 4;
        h *= 0x9E3779B97F4A7C15ull;
        return u32(h >> 40) & mask;
    }

    // Find-or-claim a site slot. Returns NULL when the table is full, and the caller counts that
    // rather than silently folding the bytes into a neighbour.
    coop_rasite* coop_ra_site(void* ra)
    {
        const LONG64 key = (LONG64)ra;
        u32 h = coop_ra_hash(key, COOP_RA_SITES - 1);
        for (u32 i = 0; i < COOP_RA_PROBE; ++i)
        {
            coop_rasite& s = s_ra[(h + i) & (COOP_RA_SITES - 1)];
            const LONG64 cur = s.ra;
            if (cur == key) return &s;
            if (cur == 0)
            {
                const LONG64 won = _InterlockedCompareExchange64(&s.ra, key, 0);
                if (won == 0 || won == key) return &s;
            }
        }
        _InterlockedExchangeAdd64(&s_ra_site_overflow, 1);
        return NULL;
    }

    void coop_ra_add(void* ra, LONG64 delta, bool is_alloc)
    {
        coop_rasite* s = coop_ra_site(ra);
        if (!s) return;
        _InterlockedExchangeAdd64(&s->net_bytes, delta);
        _InterlockedExchangeAdd64(is_alloc ? &s->n_alloc : &s->n_free, 1);
    }

    // Insert a live block. The slot is claimed by CAS and ra/size written after — a block cannot be
    // freed before the allocator has returned it to its caller, so any thread that can reach the
    // erase side has already synchronised with these stores. That is an argument about the
    // allocator's contract rather than a memory-model proof, and it is written down as such: this
    // is a diagnostic, and the failure it could produce (one block attributed to the wrong site) is
    // visible as a site that allocates without ever freeing.
    void coop_rb_insert(void* ptr, void* ra, size_t size)
    {
        if (!s_rb || !ptr) return;
        const LONG64 key = (LONG64)ptr;
        u32 h = coop_ra_hash(key, s_rb_mask);
        for (u32 i = 0; i < COOP_RA_PROBE; ++i)
        {
            coop_blk& b = s_rb[(h + i) & s_rb_mask];
            // Re-read and retry the SAME slot a few times before moving on: losing the CAS means
            // another thread claimed it this instant, and it may have claimed it for a key that
            // makes this slot unusable — or it may have released it again. Walking on immediately
            // would spend the probe budget on contention rather than on collisions.
            for (u32 tries = 0; tries < 4; ++tries)
            {
                const LONG64 cur = b.ptr;
                if (cur != 0 && cur != COOP_RA_TOMB && cur != key) break;
                if (_InterlockedCompareExchange64(&b.ptr, key, cur) != cur) continue;
                b.ra = ra;
                b.size = u32(size);
                if (cur != key)
                {
                    if (cur == COOP_RA_TOMB) _InterlockedExchangeAdd64(&s_rb_tomb, -1);
                    const LONG64 live = _InterlockedExchangeAdd64(&s_rb_live, 1) + 1;
                    // High-water is a read-modify-write without a CAS loop, so under contention it
                    // can miss a peak by a block or two. It is a capacity warning, not a
                    // measurement, and an occupancy figure that is one short never changes what a
                    // reader does with it.
                    if (live > s_rb_live_hw) s_rb_live_hw = live;
                }
                return;
            }
        }
        _InterlockedExchangeAdd64(&s_rb_insert_overflow, 1);
    }

    // Erase and return the ALLOCATING site. Returns NULL when the block was never tracked, which
    // is normal for everything allocated before arming and must be counted, not ignored.
    void* coop_rb_erase(void* ptr, u32* out_size)
    {
        if (!s_rb || !ptr) return NULL;
        const LONG64 key = (LONG64)ptr;
        u32 h = coop_ra_hash(key, s_rb_mask);
        for (u32 i = 0; i < COOP_RA_PROBE; ++i)
        {
            coop_blk& b = s_rb[(h + i) & s_rb_mask];
            const LONG64 cur = b.ptr;
            if (cur == 0) return NULL;                       // empty ends the chain; tombstone does not
            if (cur == key)
            {
                void* ra = b.ra;
                if (out_size) *out_size = b.size;
                if (_InterlockedCompareExchange64(&b.ptr, COOP_RA_TOMB, key) == key)
                {
                    _InterlockedExchangeAdd64(&s_rb_live, -1);
                    _InterlockedExchangeAdd64(&s_rb_tomb, 1);
                    return ra;
                }
                return NULL;                                 // someone else took it; do not double-book
            }
        }
        return NULL;
    }

    ICF bool coop_ra_in_band(size_t s)
    {
        return s && s >= g_coop_rasites_min && (!g_coop_rasites_max || s <= g_coop_rasites_max);
    }

    // NOTHING in here may allocate. It is called from inside mem_alloc; a logger that allocates
    // would recurse without bound. Fixed storage, interlocked index, no Msg, no shared_str.
    void coop_ra_note(u32 op, size_t size, size_t oldsize, void* ra, void* ptr, void* oldptr)
    {
        // The release side FIRST, so a realloc that reuses its own pointer erases before it
        // re-inserts. Doing it the other way round would erase the entry just written.
        //
        // THE ERASE IS TRIED ABOVE THE FLOOR, NOT INSIDE THE BAND, and the difference is a leak
        // this would otherwise manufacture. The two sides do not see the same number: the alloc
        // hook has the REQUESTED size, the free hook has `_aligned_msize`, which rounds up to the
        // allocator's granularity. A block requested at 250 with a band of [128, 255] is freed
        // reporting 256 — outside the band — so a band-gated erase would never debit it, and the
        // site that allocated it would climb forever on paper. Above the floor the probe is
        // essentially free anyway: §5j's histogram counted ~525,000 allocations at >= 128 B over
        // its 900 s run (~580/second) against ~280,000 PER SECOND overall — the floor is where
        // the traffic stops being hot, which is why the erase can afford to ignore the ceiling.
        //
        // The DEBIT uses the size stored at insertion, never the size the free reports, so the
        // rounding cannot leak into the arithmetic either.
        if (oldptr && (op == 1 || op == 2) && oldsize >= g_coop_rasites_min)
        {
            u32 sz = 0;
            void* owner = coop_rb_erase(oldptr, &sz);
            if (owner)
                coop_ra_add(owner, -(LONG64)sz, false);
            else if (coop_ra_in_band(oldsize))
            {
                // Normal for every block allocated before arming, and for anything an insert
                // overflow lost. Counted rather than ignored: if this number keeps climbing at
                // steady state, the map is dropping live blocks and every site total is a
                // lower bound.
                _InterlockedExchangeAdd64(&s_rb_unknown_free, 1);
                _InterlockedExchangeAdd64(&s_rb_unknown_bytes, (LONG64)oldsize);
            }
        }
        if (ptr && (op == 0 || op == 1) && coop_ra_in_band(size))
        {
            coop_ra_add(ra, (LONG64)size, true);
            coop_rb_insert(ptr, ra, size);
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
void coop_mem_note(u32 op, size_t size, size_t oldsize, void* ra, void* ptr, void* oldptr)
{
    if (g_coop_rasites_min) coop_ra_note(op, size, oldsize, ra, ptr, oldptr);
    if (g_coop_addrmap_min)
    {
        // Both sides, each against its own pointer: a realloc that moved its block must debit the
        // region it left and credit the one it landed in, or a heap that relocates would read as
        // growth everywhere it went.
        if (oldptr && (op == 1 || op == 2) && oldsize >= g_coop_addrmap_min)
            coop_am_add(oldptr, -(LONG64)oldsize, false);
        if (ptr && (op == 0 || op == 1) && size >= g_coop_addrmap_min)
            coop_am_add(ptr, (LONG64)size, true);
    }
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
    const size_t c = g_coop_rasites_min ? g_coop_rasites_min : ~size_t(0);
    const size_t d = g_coop_addrmap_min ? g_coop_addrmap_min : ~size_t(0);
    size_t lo = (a < b) ? a : b;
    if (c < lo) lo = c;
    if (d < lo) lo = d;
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

// §5k. The block map is sized here, once, and never grown: growing it would mean rehashing from
// inside the allocator. `want_slots` is chosen by the caller from the live population the histogram
// measured, and the banner prints what was actually reserved so a run cannot silently be smaller
// than it was asked to be. VirtualAlloc rather than xrMemory — this map is consulted from inside
// xrMemory, and allocating it there would be a recursion waiting for the first grow.
void coop_rasites_arm(size_t min_bytes, size_t max_bytes, u32 print_interval_ms, u32 top_n)
{
    if (max_bytes && min_bytes && max_bytes < min_bytes)
    {
        Msg("! COOP(rasites): the band [%u, %u] is EMPTY (max below min), so nothing could ever be "
            "recorded — NOT armed. Reported rather than corrected: a run that records nothing must "
            "not look like a run that found nothing.", u32(min_bytes), u32(max_bytes));
        return;
    }
    if (!min_bytes)
    {
        Msg("! COOP(rasites): needs a positive floor in BYTES — NOT armed.");
        return;
    }
    // 2^19 slots x 24 B = 12 MB. §5j measured ~65,000 live blocks in the 128-255 B band over 900 s,
    // so this is ~8x that population: the map must not be the thing that fills up first, and its
    // occupancy is printed every interval so "it did not" is a measurement rather than a hope.
    const u32 slots = 1u << 19;
    if (!s_rb)
    {
        void* mem = VirtualAlloc(NULL, size_t(slots) * sizeof(coop_blk), MEM_COMMIT | MEM_RESERVE,
                                 PAGE_READWRITE);
        if (!mem)
        {
            Msg("! COOP(rasites): could not reserve the %u-slot live-block map (%u KB) — NOT armed. "
                "Arming without the map would attribute every free to 'unknown' and read as a leak "
                "at every site at once.", slots, u32(size_t(slots) * sizeof(coop_blk) / 1024));
            return;
        }
        s_rb = (coop_blk*)mem;                 // VirtualAlloc zeroes: every slot starts empty
        s_rb_capacity = slots;
        s_rb_mask = slots - 1;
    }
    if (print_interval_ms) s_ra_interval_ms = print_interval_ms;
    if (top_n) s_ra_top_n = top_n;
    s_ra_last_print = GetTickCount();
    g_coop_rasites_min = min_bytes;
    g_coop_rasites_max = max_bytes;
    coop_mem_recompute_floor();
    Msg("* COOP(rasites): ARMED on the band [%u, %u] bytes, printing the top %u sites every %u ms. "
        "Bytes are netted per ALLOCATING return address: a free is debited to the site that "
        "allocated the block, looked up in a %u-slot live-block map (%u KB), because a free's own "
        "return address is the deallocation site and netting against it would say nothing about "
        "leaking. Map occupancy, insert overflows, site-table overflows and frees of untracked "
        "blocks are printed every interval — a table that silently drops sites reports a subset as "
        "if it were the set.",
        u32(min_bytes), u32(max_bytes), s_ra_top_n,
        print_interval_ms ? print_interval_ms : s_ra_interval_ms,
        s_rb_capacity, u32(size_t(s_rb_capacity) * sizeof(coop_blk) / 1024));
}

// §5k. Runs on the game thread from a normal frame, so it may Msg and may call into the loader —
// neither of which is safe from inside the allocator. The DELTA is the reading, for the same reason
// it is in §5j: a cumulative net is dominated by boot.
static void coop_ra_tick()
{
    const u32 now = GetTickCount();
    if (u32(now - s_ra_last_print) < s_ra_interval_ms) return;
    const u32 elapsed = now - s_ra_last_print;
    s_ra_last_print = now;

    LONG64 total_delta = 0, total_net = 0;
    u32 used = 0;
    for (u32 i = 0; i < COOP_RA_SITES; ++i)
    {
        if (!s_ra[i].ra) continue;
        ++used;
        total_delta += s_ra[i].net_bytes - s_ra_prev_net[i];
        total_net += s_ra[i].net_bytes;
    }
    Msg("* COOP(rasites) t=%u interval=%u ms — NET %d KB this interval, %d KB since armed across "
        "%u site(s). Map: %I64d live + %I64d tombstones = %I64d occupied of %u slots (high-water "
        "%I64d live), insert overflow %I64d, site-table overflow %I64d, frees of untracked blocks "
        "%I64d (%I64d KB).",
        now, elapsed, int(total_delta / 1024), int(total_net / 1024), used,
        s_rb_live, s_rb_tomb, s_rb_live + s_rb_tomb, s_rb_capacity, s_rb_live_hw,
        s_rb_insert_overflow, s_ra_site_overflow, s_rb_unknown_free, s_rb_unknown_bytes / 1024);

    // Top N by |interval delta|, selected without sorting the table: N passes over 4096 entries on
    // one frame every 30 s. A sort would need scratch storage, and this runs where allocating is
    // merely undesirable rather than forbidden — but the simpler thing that cannot allocate is the
    // one to write.
    for (u32 rank = 0; rank < s_ra_top_n; ++rank)
    {
        u32 best = COOP_RA_SITES;
        LONG64 best_abs = 0;
        for (u32 i = 0; i < COOP_RA_SITES; ++i)
        {
            if (!s_ra[i].ra || s_ra_prev_net[i] == LLONG_MIN) continue;   // MIN marks "already shown"
            const LONG64 d = s_ra[i].net_bytes - s_ra_prev_net[i];
            const LONG64 a = d < 0 ? -d : d;
            if (a > best_abs) { best_abs = a; best = i; }
        }
        if (best == COOP_RA_SITES || !best_abs) break;
        const coop_rasite& s = s_ra[best];
        void* const ra = (void*)s.ra;
        // Symbolication support: the base of whichever module the site is in, resolved HERE rather
        // than in the allocator, and per site rather than once — allocations come from xrCore,
        // xrGame and the CRT, and one base printed for all of them makes most of the RVAs wrong.
        HMODULE h = NULL;
        const char* modname = "?";
        char path[MAX_PATH] = {0};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)ra, &h) && h &&
            GetModuleFileNameA(h, path, MAX_PATH))
        {
            const char* slash = strrchr(path, '\\');
            modname = slash ? slash + 1 : path;
        }
        Msg("*   ra=%p  %s+0x%I64X  delta %+9d KB | net %+9d KB | allocs %I64d frees %I64d live %I64d",
            ra, modname, h ? (LONG64)((char*)ra - (char*)h) : (LONG64)0,
            int((s.net_bytes - s_ra_prev_net[best]) / 1024), int(s.net_bytes / 1024),
            s.n_alloc, s.n_free, s.n_alloc - s.n_free);
        s_ra_prev_net[best] = LLONG_MIN;    // temporarily, so the next rank picks a different site
    }
    // Restore the previous-net snapshot for every site, including the ones just shown.
    for (u32 i = 0; i < COOP_RA_SITES; ++i)
        if (s_ra[i].ra) s_ra_prev_net[i] = s_ra[i].net_bytes;
}

// §5p. Runs on the game thread from a normal frame. The DELTA is the reading, as everywhere else
// on this axis, and the ADDRESS is printed as a 1 MiB-aligned range so it can be laid straight
// against `dev/harness/smaps_arenas.py`'s mapping table without arithmetic in the reader's head.
static void coop_am_tick()
{
    const u32 now = GetTickCount();
    if (u32(now - s_am_last_print) < s_am_interval_ms) return;
    const u32 elapsed = now - s_am_last_print;
    s_am_last_print = now;

    LONG64 total_delta = 0, total_net = 0;
    u32 used = 0;
    for (u32 i = 0; i < COOP_AM_SLOTS; ++i)
    {
        if (!s_am[i].key) continue;
        ++used;
        total_delta += s_am[i].net_bytes - s_am_prev_net[i];
        total_net += s_am[i].net_bytes;
    }
    Msg("* COOP(addrmap) t=%u interval=%u ms -- NET %d KB this interval, %d KB since armed "
        "across %u region(s) of 1 MiB. Table overflow %I64d. Each line is a 1 MiB slice of ADDRESS "
        "SPACE, so it lays directly against the smaps arena table: a region whose xrMemory net "
        "climbs while its arena commits is that arena's owner, and one whose arena commits while "
        "this stays flat is NOT.",
        now, elapsed, int(total_delta / 1024), int(total_net / 1024), used, s_am_overflow);

    // §5s: on a census tick, walk the table in address order and print everything. Address order
    // rather than rank order on purpose -- the output is meant to be laid against a mapping table,
    // and a list sorted by size is the wrong shape for that join.
    ++s_am_ticks;
    const bool census = s_am_census_every && (s_am_ticks % s_am_census_every) == 0;
    if (census)
    {
        u32 shown = 0;
        LONG64 shown_net = 0;
        // Selection sort by address over a fixed table: O(n^2) on 8192 slots is ~30 ms once every
        // few minutes on the game thread, and it allocates nothing. A sort needing scratch storage
        // would have to allocate, on a path whose whole discipline is that it does not.
        LONG64 after = 0;
        for (;;)
        {
            LONG64 lowest = 0;
            u32 idx = COOP_AM_SLOTS;
            for (u32 i = 0; i < COOP_AM_SLOTS; ++i)
            {
                const LONG64 k = s_am[i].key;
                if (!k || k <= after) continue;
                if (idx == COOP_AM_SLOTS || k < lowest) { lowest = k; idx = i; }
            }
            if (idx == COOP_AM_SLOTS) break;
            after = lowest;
            const coop_ambucket& b = s_am[idx];
            const u64 base = (u64(lowest) - 1) << 20;
            Msg("*  census %016I64X-%016I64X  net %+9d KB | allocs %I64d frees %I64d",
                base, base + (u64(1) << 20), int(b.net_bytes / 1024), b.n_alloc, b.n_free);
            ++shown;
            shown_net += b.net_bytes;
        }
        Msg("* COOP(addrmap) CENSUS complete: %u regions, %d KB of live bytes accounted. This is "
            "a SNAPSHOT and says nothing about rate -- that is the point of it, and also its "
            "limit.", shown, int(shown_net / 1024));
    }

    for (u32 rank = 0; rank < s_am_top_n; ++rank)
    {
        u32 best = COOP_AM_SLOTS;
        LONG64 best_abs = 0;
        for (u32 i = 0; i < COOP_AM_SLOTS; ++i)
        {
            if (!s_am[i].key || s_am_prev_net[i] == LLONG_MIN) continue;
            const LONG64 d = s_am[i].net_bytes - s_am_prev_net[i];
            const LONG64 a = d < 0 ? -d : d;
            if (a > best_abs) { best_abs = a; best = i; }
        }
        if (best == COOP_AM_SLOTS || !best_abs) break;
        const coop_ambucket& b = s_am[best];
        const u64 base = (u64(b.key) - 1) << 20;
        Msg("*   %016I64X-%016I64X  delta %+9d KB | net %+9d KB | allocs %I64d frees %I64d",
            base, base + (u64(1) << 20),
            int((b.net_bytes - s_am_prev_net[best]) / 1024), int(b.net_bytes / 1024),
            b.n_alloc, b.n_free);
        s_am_prev_net[best] = LLONG_MIN;
    }
    for (u32 i = 0; i < COOP_AM_SLOTS; ++i)
        if (s_am[i].key) s_am_prev_net[i] = s_am[i].net_bytes;
}

void coop_addrmap_arm(size_t min_bytes, u32 print_interval_ms, u32 top_n, u32 census_every)
{
    if (!min_bytes)
    {
        Msg("! COOP(addrmap): needs a positive floor in BYTES -- NOT armed.");
        return;
    }
    g_coop_addrmap_min = min_bytes;
    if (print_interval_ms) s_am_interval_ms = print_interval_ms;
    if (top_n) s_am_top_n = top_n;
    s_am_census_every = census_every;
    s_am_last_print = GetTickCount();
    coop_mem_recompute_floor();
    Msg("* COOP(addrmap): ARMED at %u bytes, printing the top %u regions every %u ms. Bytes are "
        "netted per 1 MiB of ADDRESS SPACE. It needs no live-block map and that is why it is "
        "cheap: the bucket is a function of the POINTER, and the free path has the pointer, so "
        "alloc-adds and free-subtracts land in the same bucket by construction. It answers the "
        "question smaps cannot -- /proc does not record which allocator owns an anonymous "
        "page, and this says which pages xrMemory is holding live. ARMED FROM xrCore::_initialize "
        "(§5s), so NOTHING it could see is invisible to it -- an arena's census figure is a "
        "measurement rather than a lower bound, whatever the arena's age.%s",
        u32(min_bytes), s_am_top_n, print_interval_ms ? print_interval_ms : s_am_interval_ms,
        census_every ? " A CENSUS of every non-empty region is printed on every n-th interval."
                     : "");
    if (census_every)
        Msg("* COOP(addrmap): census every %u intervals -- a full table in ADDRESS order, meant to "
            "be laid against a mapping table. It is a snapshot and says nothing about rate.",
            census_every);
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
    // Ordered so the histogram runs even when the per-event ring is disarmed — the three are
    // independent instruments and §5j's whole point is being able to run the cheap one alone.
    if (g_coop_allocsites_min) coop_sc_tick();
    if (g_coop_rasites_min) coop_ra_tick();
    if (g_coop_addrmap_min) coop_am_tick();
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
			coop_mem_note(0, size, 0, _ReturnAddress(), result, NULL);
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

	// §6c SAFETY NET, and it must be the FIRST thing done with P. A pooled block carries no
	// xrMemory header, so `get_header(P)` below would read the byte in front of it -- which belongs
	// to another block, or to nothing -- and dispatch on garbage.
	//
	// It also closes the one hazard §6b named that the pool cannot close from the luabind side: if
	// any engine code frees a block that `luabind_new` allocated, it arrives HERE and not at the
	// luabind free path. The discriminator is exact, so routing it is safe rather than a guess --
	// and it is COUNTED SEPARATELY, so the run tells us whether cross-allocator frees actually
	// happen instead of us assuming they do not. `reclaimed > 0` in the stats line is that answer.
	if (coop_pool_owns(P))
	{
		coop_pool_note_reclaimed();
		coop_pool_free(P);
		return;
	}
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
				coop_mem_note(2, 0, coop_ba_sz, _ReturnAddress(), NULL, P);
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

	// §6c SAFETY NET -- same reason as mem_free, and same placement rule: every path below reads a
	// header the pooled block does not have. Growing out of the pool means taking a block from
	// wherever the new size belongs, copying only what the OLD CLASS actually held, and returning
	// the pooled block. `old` is the class size and never the requested size, because the requested
	// size is not recorded -- copying `size` bytes from a 16-byte block would read past it.
	if (coop_pool_owns(P))
	{
		coop_pool_note_reclaimed();
		size_t const old = coop_pool_block_size(P);
		void* const result = mem_alloc(size
# ifdef DEBUG_MEMORY_NAME
			, _name
# endif // DEBUG_MEMORY_NAME
		);
		// old == 0 means an in-region pointer that is not a valid block start. Nothing can be
		// safely copied from it and coop_pool_free will refuse it and count it, which is the
		// conservative branch: leak 16 bytes rather than corrupt a free list.
		if (result && old)
			memcpy(result, P, (size < old) ? size : old);
		coop_pool_free(P);
		return result;
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
			coop_mem_note(1, size, coop_ba_old, _ReturnAddress(), result, P);

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
