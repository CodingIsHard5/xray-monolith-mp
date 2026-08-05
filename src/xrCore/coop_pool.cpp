////////////////////////////////////////////////////////////////////////////////////////////////
//  coop_pool.cpp — MP fork, §6c. The discriminator, then the pool behind it.
//
//  Read coop_pool.h first: it carries the argument for why ownership is decided by a reserved
//  address range rather than by a per-block header, and the two invariants (never disarm, never
//  decommit) that the exactness rests on.
//
//  THREE RULES THIS FILE FOLLOWS, each one a scar from this project's own history:
//
//    * NOTHING IS LOGGED UNDER THE POOL LOCK. `log.cpp` deadlocked this engine by taking two locks
//      in two orders; the pool takes exactly one lock, holds it across nothing but pointer
//      arithmetic and one VirtualAlloc, and calls no engine subsystem while holding it.
//    * NO `VERIFY`, NO `R_ASSERT`, ON ANY PATH THAT MATTERS. Those compile out in release, which is
//      the configuration the server actually runs, and this project has already lost five
//      increments to code that was "guarded" only in a build nobody ships. Every check here is an
//      ordinary `if` that is present in every configuration, and every check that fails takes the
//      conservative branch and increments a counter rather than trusting itself.
//    * A REFUSED OPERATION LEAKS; IT NEVER GUESSES. A pointer inside the region that is not a valid
//      block start is counted and dropped. Sixteen bytes lost is a rounding error. A corrupted free
//      list is a server that dies on Thursday for a reason nobody can reconstruct.
////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef COOP_POOL_STANDALONE
#	include "stdafx.h"
#	pragma hdrstop
#endif

#include "coop_pool.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef COOP_POOL_STANDALONE
#	include <sys/mman.h>
#else
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <intrin.h>
#endif

// ---------------------------------------------------------------------------------------------
// Geometry.
//
// CHUNK_BYTES is 64 KiB because that is Windows' reservation granularity, so a chunk boundary is a
// boundary the OS already respects and the chunk index is a shift of the region offset.
//
// Two size classes, 16 and 32 bytes. §6a measured the entire dominant population in the 8-15 byte
// band -- `_vector3<float>` at 12 bytes and `xrTime` -- so class 0 alone covers the target; class 1
// is there so the boundary case (a 17-32 byte luabind object) does not fall back and reintroduce
// the fragmentation the pool exists to remove. Both are powers of two, which makes the "is this
// offset a block start?" test a mask instead of a modulo.
namespace
{
	const unsigned CHUNK_SHIFT = 16;
	const size_t   CHUNK_BYTES = size_t(1) << CHUNK_SHIFT;
	const size_t   CHUNK_MASK  = CHUNK_BYTES - 1;

	const unsigned NUM_CLASSES = 2;
	const size_t   CLASS_SIZE[NUM_CLASSES]  = { 16, 32 };
	const unsigned CLASS_SHIFT[NUM_CLASSES] = { 4, 5 };
	const size_t   MAX_POOLED = 32;

	// The free bitmap has one bit per 16 bytes of region, which is the finest class. A 32-byte
	// block therefore owns two bits and only the first is ever set; that costs a bit of bitmap and
	// buys one uniform index expression on both classes.
	const unsigned BITMAP_GRAIN_SHIFT = 4;

	inline unsigned class_of(size_t size)
	{
		return (size <= CLASS_SIZE[0]) ? 0u : 1u;
	}

	inline void cpu_relax()
	{
#ifdef COOP_POOL_STANDALONE
		__builtin_ia32_pause();
#else
		_mm_pause();
#endif
	}

	// One lock, no recursion, no nesting with any other lock in the engine. Built from a plain
	// atomic rather than std::atomic_flag so it does not depend on ATOMIC_FLAG_INIT, which is
	// deprecated in C++20 and this tree is built with more than one standard setting.
	class spin_lock
	{
		std::atomic<int> m_held;
	public:
		spin_lock() : m_held(0) {}
		void lock()
		{
			for (;;)
			{
				int expected = 0;
				if (m_held.compare_exchange_weak(expected, 1, std::memory_order_acquire,
				                                 std::memory_order_relaxed))
					return;
				cpu_relax();
			}
		}
		void unlock() { m_held.store(0, std::memory_order_release); }
	};

	struct pool_state
	{
		spin_lock lock;

		// The raw reservation, kept only so the region is never mistaken for something that can be
		// released. It is never unmapped: see invariant 1 in the header.
		void*     raw_base;
		size_t    raw_bytes;

		uintptr_t base;        // aligned, published to g_coop_pool_base
		size_t    span;        // published to g_coop_pool_span
		size_t    num_chunks;

		unsigned char* chunk_class;   // per chunk: class index, or 0xFF when the chunk is not in use
		unsigned short* chunk_bumped; // per chunk: blocks ever handed out by the bump pointer
		unsigned char* free_bits;     // per 16 bytes of region: 1 when that block is on a free list

		size_t    next_chunk;                 // lowest chunk index never yet committed
		void*     free_head[NUM_CLASSES];     // intrusive free lists
		uintptr_t bump[NUM_CLASSES];
		uintptr_t bump_end[NUM_CLASSES];

		pool_state()
			: raw_base(0), raw_bytes(0), base(0), span(0), num_chunks(0),
			  chunk_class(0), chunk_bumped(0), free_bits(0), next_chunk(0)
		{
			for (unsigned c = 0; c < NUM_CLASSES; ++c)
			{
				free_head[c] = 0;
				bump[c] = 0;
				bump_end[c] = 0;
			}
		}
	};

	pool_state g_pool;

	std::atomic<unsigned long long> g_allocs(0);
	std::atomic<unsigned long long> g_frees(0);
	std::atomic<unsigned long long> g_fallbacks(0);
	std::atomic<unsigned long long> g_chunks(0);
	std::atomic<unsigned long long> g_bad_frees(0);
	std::atomic<unsigned long long> g_foreign_frees(0);
	std::atomic<unsigned long long> g_reclaimed(0);

	// -----------------------------------------------------------------------------------------
	// Platform: reserve address space without committing it, and commit one chunk at a time.
	// Reservation is the whole basis of the discriminator, so a failure here disarms the pool
	// rather than degrading it to something approximate.

	void* sys_reserve(size_t bytes)
	{
#ifdef COOP_POOL_STANDALONE
		void* p = mmap(0, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		return (p == MAP_FAILED) ? 0 : p;
#else
		return VirtualAlloc(0, bytes, MEM_RESERVE, PAGE_READWRITE);
#endif
	}

	bool sys_commit(void* at, size_t bytes)
	{
#ifdef COOP_POOL_STANDALONE
		return mprotect(at, bytes, PROT_READ | PROT_WRITE) == 0;
#else
		return VirtualAlloc(at, bytes, MEM_COMMIT, PAGE_READWRITE) != 0;
#endif
	}

	// -----------------------------------------------------------------------------------------
	// The free bitmap. Indexed in 16-byte grains from the region base; only ever consulted for a
	// pointer already proven to be in the region.

	inline size_t bit_index(uintptr_t p) { return (p - g_pool.base) >> BITMAP_GRAIN_SHIFT; }

	inline bool bit_get(size_t i)  { return (g_pool.free_bits[i >> 3] & (1u << (i & 7))) != 0; }
	inline void bit_set(size_t i)  { g_pool.free_bits[i >> 3] |= (unsigned char)(1u << (i & 7)); }
	inline void bit_clear(size_t i){ g_pool.free_bits[i >> 3] &= (unsigned char)~(1u << (i & 7)); }

	// -----------------------------------------------------------------------------------------
	// Take a fresh chunk for `cls`. Caller holds the lock. Returns false when the region is used
	// up, which is not an error: the caller falls back to the engine allocator and the block it
	// gets back is correctly identified as foreign forever after.
	bool take_chunk(unsigned cls)
	{
		if (g_pool.next_chunk >= g_pool.num_chunks)
			return false;

		size_t const ci = g_pool.next_chunk;
		void* const  at = (void*)(g_pool.base + (ci << CHUNK_SHIFT));
		if (!sys_commit(at, CHUNK_BYTES))
			return false;

		g_pool.next_chunk = ci + 1;
		g_pool.chunk_class[ci]  = (unsigned char)cls;
		g_pool.chunk_bumped[ci] = 0;
		g_pool.bump[cls]     = (uintptr_t)at;
		g_pool.bump_end[cls] = (uintptr_t)at + CHUNK_BYTES;
		g_chunks.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	// -----------------------------------------------------------------------------------------
	// Decide whether an in-region pointer is a block this pool actually handed out. Every clause
	// is a way a wild pointer can land inside the region and none of them are hypothetical: a
	// stale pointer into a freed object, an interior pointer from a cast, an off-by-one from a
	// container. Caller holds the lock; caller has already established owns(p).
	//
	// Returns the class on success, or NUM_CLASSES on rejection.
	unsigned classify_block(uintptr_t p)
	{
		size_t const off = p - g_pool.base;
		size_t const ci  = off >> CHUNK_SHIFT;

		unsigned const cls = g_pool.chunk_class[ci];
		if (cls >= NUM_CLASSES)
			return NUM_CLASSES;          // chunk never committed, or not carved for any class

		size_t const in_chunk = off & CHUNK_MASK;
		if (in_chunk & (CLASS_SIZE[cls] - 1))
			return NUM_CLASSES;          // interior pointer: not on a block boundary

		size_t const slot = in_chunk >> CLASS_SHIFT[cls];
		if (slot >= g_pool.chunk_bumped[ci])
			return NUM_CLASSES;          // a slot this chunk has never handed out

		return cls;
	}
}

// ---------------------------------------------------------------------------------------------
// The two globals the inline discriminator reads. Zero until arming succeeds.
XRCORE_API std::atomic<uintptr_t> g_coop_pool_base(0);
XRCORE_API std::atomic<size_t>    g_coop_pool_span(0);

XRCORE_API size_t coop_pool_max_pooled() { return MAX_POOLED; }
XRCORE_API size_t coop_pool_chunk_bytes() { return CHUNK_BYTES; }

// ---------------------------------------------------------------------------------------------
XRCORE_API bool coop_pool_arm(size_t region_mib)
{
	if (g_coop_pool_span.load(std::memory_order_acquire) != 0)
		return true;                     // already armed; arming is idempotent and one-way

	if (region_mib == 0)
		return false;

	size_t const span = region_mib << 20;

	// Over-reserve by one chunk and align up. On Windows the reservation is already 64 KiB
	// aligned and the align-up is a no-op; the standalone control gets page-granular mmap and
	// needs it. Doing it unconditionally means both builds exercise the same index arithmetic,
	// which is the point of having a control at all.
	size_t const raw_bytes = span + CHUNK_BYTES;
	void* const  raw       = sys_reserve(raw_bytes);
	if (!raw)
		return false;

	uintptr_t const base = (uintptr_t(raw) + CHUNK_MASK) & ~uintptr_t(CHUNK_MASK);

	size_t const num_chunks = span >> CHUNK_SHIFT;
	unsigned char*  chunk_class  = (unsigned char*)malloc(num_chunks);
	unsigned short* chunk_bumped = (unsigned short*)malloc(num_chunks * sizeof(unsigned short));
	size_t const    bitmap_bytes = (span >> BITMAP_GRAIN_SHIFT) >> 3;
	unsigned char*  free_bits    = (unsigned char*)malloc(bitmap_bytes);

	if (!chunk_class || !chunk_bumped || !free_bits)
	{
		// Leave the reservation in place but stay disarmed: an unarmed pool claims nothing, and
		// releasing the reservation here would be the one code path that unmaps the region.
		free(chunk_class);
		free(chunk_bumped);
		free(free_bits);
		return false;
	}

	memset(chunk_class,  0xFF, num_chunks);                          // 0xFF = chunk not in use
	memset(chunk_bumped, 0,    num_chunks * sizeof(unsigned short));
	memset(free_bits,    0,    bitmap_bytes);

	g_pool.raw_base     = raw;
	g_pool.raw_bytes    = raw_bytes;
	g_pool.base         = base;
	g_pool.span         = span;
	g_pool.num_chunks   = num_chunks;
	g_pool.chunk_class  = chunk_class;
	g_pool.chunk_bumped = chunk_bumped;
	g_pool.free_bits    = free_bits;
	g_pool.next_chunk   = 0;

	// Publish base BEFORE span. A reader that sees a non-zero span is then guaranteed to see the
	// matching base, and a reader that sees span==0 rejects every pointer -- so there is no window
	// in which the discriminator can claim a block using a stale base.
	g_coop_pool_base.store(base, std::memory_order_relaxed);
	g_coop_pool_span.store(span, std::memory_order_release);
	return true;
}

// ---------------------------------------------------------------------------------------------
XRCORE_API void* coop_pool_alloc(size_t size)
{
	if (size == 0 || size > MAX_POOLED || g_coop_pool_span.load(std::memory_order_acquire) == 0)
	{
		g_fallbacks.fetch_add(1, std::memory_order_relaxed);
		return 0;
	}

	unsigned const cls = class_of(size);

	g_pool.lock.lock();

	void* result = g_pool.free_head[cls];
	if (result)
	{
		g_pool.free_head[cls] = *(void**)result;
		bit_clear(bit_index((uintptr_t)result));
	}
	else
	{
		if (g_pool.bump[cls] >= g_pool.bump_end[cls] && !take_chunk(cls))
		{
			g_pool.lock.unlock();
			g_fallbacks.fetch_add(1, std::memory_order_relaxed);
			return 0;                    // region exhausted: caller falls back, and correctly
		}
		result = (void*)g_pool.bump[cls];
		g_pool.bump[cls] += CLASS_SIZE[cls];
		g_pool.chunk_bumped[((uintptr_t)result - g_pool.base) >> CHUNK_SHIFT]++;
	}

	g_pool.lock.unlock();
	g_allocs.fetch_add(1, std::memory_order_relaxed);
	return result;
}

// ---------------------------------------------------------------------------------------------
XRCORE_API bool coop_pool_free(void* p)
{
	if (!coop_pool_owns(p))
	{
		g_foreign_frees.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	uintptr_t const up = (uintptr_t)p;

	g_pool.lock.lock();

	unsigned const cls = classify_block(up);
	if (cls >= NUM_CLASSES)
	{
		g_pool.lock.unlock();
		g_bad_frees.fetch_add(1, std::memory_order_relaxed);
		return false;                    // wild or interior: leak it, do not touch the free list
	}

	size_t const bi = bit_index(up);
	if (bit_get(bi))
	{
		g_pool.lock.unlock();
		g_bad_frees.fetch_add(1, std::memory_order_relaxed);
		return false;                    // double free: the block is already on a free list
	}

	bit_set(bi);
	*(void**)p = g_pool.free_head[cls];
	g_pool.free_head[cls] = p;

	g_pool.lock.unlock();
	g_frees.fetch_add(1, std::memory_order_relaxed);
	return true;
}

// ---------------------------------------------------------------------------------------------
XRCORE_API size_t coop_pool_block_size(void const* p)
{
	if (!coop_pool_owns(p))
		return 0;

	g_pool.lock.lock();
	unsigned const cls = classify_block((uintptr_t)p);
	size_t const   sz  = (cls < NUM_CLASSES) ? CLASS_SIZE[cls] : 0;
	g_pool.lock.unlock();
	return sz;
}

XRCORE_API void coop_pool_note_foreign_free()
{
	g_reclaimed.fetch_add(1, std::memory_order_relaxed);
}

XRCORE_API void coop_pool_get_stats(coop_pool_stats_t& out)
{
	out.allocs        = g_allocs.load(std::memory_order_relaxed);
	out.frees         = g_frees.load(std::memory_order_relaxed);
	out.fallbacks     = g_fallbacks.load(std::memory_order_relaxed);
	out.chunks        = g_chunks.load(std::memory_order_relaxed);
	out.live          = out.allocs - out.frees;
	out.bad_frees     = g_bad_frees.load(std::memory_order_relaxed);
	out.foreign_frees = g_foreign_frees.load(std::memory_order_relaxed);
	out.reclaimed     = g_reclaimed.load(std::memory_order_relaxed);
}

// =============================================================================================
// THE CONTROL (§6c).
//
// The Overseer's instruction, and the right one: build the discriminator first, prove it against
// pointers whose provenance is known BY CONSTRUCTION, and only then wire it into a path. A pool
// whose ownership test is wrong does not fail loudly -- it corrupts a heap and the symptom appears
// hours later somewhere unrelated. So this runs before the pool is trusted with a single real
// allocation, and the run that enables the pool logs its verdict and its counts.
//
// The foreign population deliberately comes from FOUR different provenances, because a test that
// only feeds it `malloc` blocks proves only that malloc and the pool do not overlap:
//
//   1. malloc, at sizes on both sides of the pooled range (a heap allocator's own arenas are the
//      pointers most likely to sit near ours in the address space)
//   2. the stack (a completely different region, and where a stale pointer most often comes from)
//   3. statics and code addresses (link-time addresses, far from any heap)
//   4. THE TWO ADDRESSES IMMEDIATELY OUTSIDE THE REGION'S BOUNDS -- base-1 and base+span. These
//      are the only two addresses in the entire space where an off-by-one in the compare shows up,
//      and an off-by-one there is exactly the bug that would hand a neighbour's block to our free
//      list. They are tested by address only and never dereferenced.
//
// The own population spans several chunks and both size classes, and includes the first and last
// block of every chunk it touches, because a masking scheme's failures live at chunk boundaries.
// =============================================================================================

namespace
{
	// Written through every pooled block so a returned block that overlaps another shows up as
	// corrupted content and not merely as a duplicate address.
	inline unsigned long long pattern_for(size_t i) { return 0x5AA5000000000000ull | (i * 2654435761ull); }
}

XRCORE_API bool coop_pool_selftest(char* out_report, size_t report_bytes)
{
	if (out_report && report_bytes)
		out_report[0] = 0;

	if (g_coop_pool_span.load(std::memory_order_acquire) == 0)
	{
		if (out_report && report_bytes)
			snprintf(out_report, report_bytes, "POOL SELFTEST: FAIL (pool not armed)");
		return false;
	}

	unsigned long long false_pos = 0;   // foreign pointer claimed as ours -- the fatal direction
	unsigned long long false_neg = 0;   // our own pointer disowned -- the other fatal direction
	unsigned long long bad_class = 0;   // ours, but classify/block_size disagreed
	unsigned long long corrupt   = 0;   // two live blocks overlapped
	unsigned long long interior  = 0;   // an interior pointer that classify_block failed to reject

	uintptr_t const base = g_coop_pool_base.load(std::memory_order_relaxed);
	size_t const    span = g_coop_pool_span.load(std::memory_order_relaxed);

	// ----- FOREIGN, part 1: the boundary pair. Address arithmetic only; never dereferenced.
	if (coop_pool_owns((void const*)(base - 1)))          ++false_pos;
	if (coop_pool_owns((void const*)(base + span)))       ++false_pos;
	if (coop_pool_owns((void const*)(base + span + 1)))   ++false_pos;
	if (coop_pool_owns((void const*)0))                   ++false_pos;
	if (coop_pool_owns((void const*)~uintptr_t(0)))       ++false_pos;
	// ...and the two addresses that MUST be ours, for the same reason.
	if (!coop_pool_owns((void const*)base))               ++false_neg;
	if (!coop_pool_owns((void const*)(base + span - 1)))  ++false_neg;

	// ----- FOREIGN, part 2: real blocks from a different allocator, at sizes above and below the
	// pooled range so neither "too small to pool" nor "too big to pool" is the reason it passes.
	{
		size_t const sizes[] = { 1, 8, 12, 16, 32, 33, 64, 4096, 1 << 20 };
		void* held[sizeof(sizes) / sizeof(sizes[0])];
		for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
		{
			held[i] = malloc(sizes[i]);
			if (held[i] && coop_pool_owns(held[i]))
				++false_pos;
		}
		for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
			free(held[i]);
	}

	// ----- FOREIGN, part 3: stack, statics, code.
	{
		volatile int on_stack = 0;
		static int  in_bss    = 0;
		static int  in_data   = 7;
		if (coop_pool_owns((void const*)&on_stack))                       ++false_pos;
		if (coop_pool_owns((void const*)&in_bss))                         ++false_pos;
		if (coop_pool_owns((void const*)&in_data))                        ++false_pos;
		if (coop_pool_owns((void const*)&coop_pool_selftest))             ++false_pos;
		if (coop_pool_owns((void const*)g_pool.chunk_class))              ++false_pos;
		if (coop_pool_owns((void const*)g_pool.free_bits))                ++false_pos;
	}

	// ----- OWN: enough blocks to cross several chunks in both classes.
	// 4096 blocks of class 0 fill exactly one 64 KiB chunk, so 10,000 guarantees boundaries are
	// crossed in class 0 and 2048-block boundaries in class 1.
	size_t const N = 10000;
	void** own = (void**)malloc(N * 2 * sizeof(void*));
	if (!own)
	{
		if (out_report && report_bytes)
			snprintf(out_report, report_bytes, "POOL SELFTEST: FAIL (control could not allocate)");
		return false;
	}
	memset(own, 0, N * 2 * sizeof(void*));

	size_t taken = 0;
	for (size_t i = 0; i < N * 2; ++i)
	{
		size_t const req = (i & 1) ? 12 : 28;             // one of each class, interleaved
		void* p = coop_pool_alloc(req);
		if (!p)
			break;                                        // exhaustion is legal; stop and check what we have
		own[taken++] = p;

		if (!coop_pool_owns(p))                           ++false_neg;
		size_t const bs = coop_pool_block_size(p);
		if (bs != ((req <= 16) ? 16u : 32u))              ++bad_class;
		// An interior pointer is inside the region -- owns() says yes, correctly -- but it is not
		// a block start, and the free path must refuse it. This is the check that separates the
		// range test from the validity test, and it is the one a header scheme gets wrong.
		if (bs > 8 && coop_pool_block_size((void const*)((char*)p + 1)) != 0)
			++interior;

		*(unsigned long long*)p = pattern_for(taken);
	}

	// Every live block must still hold its own pattern: if any two allocations overlapped, at
	// least one is now wrong.
	for (size_t i = 0; i < taken; ++i)
		if (*(unsigned long long*)own[i] != pattern_for(i + 1))
			++corrupt;

	// ----- Churn: free half, reallocate, and require that nothing handed back collides with a
	// block still live. This is what catches a free list that has been threaded wrongly.
	for (size_t i = 0; i < taken; i += 2)
	{
		if (!coop_pool_free(own[i]))
			++bad_class;
		own[i] = 0;
	}
	// A second free of the same block must be REFUSED, not silently accepted -- an accepted double
	// free is a cycle in the free list and the same address handed to two owners later.
	{
		void* d = coop_pool_alloc(12);
		if (d)
		{
			if (!coop_pool_free(d))  ++bad_class;
			if (coop_pool_free(d))   ++corrupt;           // must refuse
			void* again = coop_pool_alloc(12);            // and the block must still be reusable
			if (again) coop_pool_free(again); else ++bad_class;
		}
	}
	for (size_t i = 0; i < taken; i += 2)
	{
		void* p = coop_pool_alloc((i & 2) ? 12 : 28);
		if (!p) break;
		if (!coop_pool_owns(p)) ++false_neg;
		own[i] = p;
		*(unsigned long long*)p = pattern_for(i + 1);
	}
	for (size_t i = 0; i < taken; ++i)
		if (own[i] && *(unsigned long long*)own[i] != pattern_for(i + 1))
			++corrupt;

	for (size_t i = 0; i < taken; ++i)
		if (own[i]) coop_pool_free(own[i]);
	free(own);

	bool const pass = (false_pos == 0) && (false_neg == 0) && (bad_class == 0)
	               && (corrupt == 0) && (interior == 0);

	if (out_report && report_bytes)
	{
		snprintf(out_report, report_bytes,
		         "POOL SELFTEST: %s  own=%llu false_pos=%llu false_neg=%llu bad_class=%llu "
		         "corrupt=%llu interior=%llu  region=%lluMiB chunks=%llu",
		         pass ? "PASS" : "FAIL",
		         (unsigned long long)taken, false_pos, false_neg, bad_class, corrupt, interior,
		         (unsigned long long)(span >> 20),
		         (unsigned long long)g_chunks.load(std::memory_order_relaxed));
	}
	return pass;
}
