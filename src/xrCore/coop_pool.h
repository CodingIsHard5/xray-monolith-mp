////////////////////////////////////////////////////////////////////////////////////////////////
//  coop_pool.h — MP fork, §6c: a size-segregated pool for the tiny luabind objects §6a named,
//  and — first, and more important than the pool — an EXACT test for "did this pointer come
//  from me?".
//
//  WHY THE DISCRIMINATOR IS THE SAFETY-CRITICAL PART, AND THE POOL IS NOT.
//
//  luabind's free path is `call_allocator(p, 0)` (luabind_delete.h:20) and carries NO SIZE and no
//  provenance. It runs for every luabind deallocation there is: blocks from `luabind_new`, blocks
//  from the container allocator, and every block allocated before a pool existed. So the pool sits
//  behind a fork in the road it must get right on every single free:
//
//      ours    -> return to a free list
//      foreign -> xr_free
//
//  Get that backwards in either direction and the heap is corrupted silently. The symptom then
//  surfaces somewhere unrelated, hours later, on a server that is meant to run for DAYS. That is
//  the worst failure this project can ship, and it outranks every byte the pool might save.
//
//  A magic number in a per-block header cannot do this: a magic word is a PROBABILISTIC test. Any
//  foreign block whose preceding bytes happen to hold the magic value is misread as ours, and
//  "unlikely" is not a property you want protecting a heap for a week of uptime.
//
//  THE ANSWER IS TO MAKE THE TEST EXACT BY CONSTRUCTION RATHER THAN BY INSPECTION.
//
//  Reserve ONE contiguous region of address space up front (VirtualAlloc MEM_RESERVE). From that
//  moment the operating system will not hand any part of that range to any other allocator, so:
//
//      p is ours   <==>   p lies inside the region
//
//  is not a heuristic, it is an invariant the OS maintains for us. It costs one subtract and one
//  unsigned compare, touches no memory the caller owns, and — this is the part a header scheme
//  cannot match — it NEVER DEREFERENCES A POINTER IT HAS NOT ALREADY PROVEN IS OURS. A header
//  scheme must read `p[-8]` to decide, which on a foreign pointer near the base of a mapping is a
//  read of unmapped memory: the test itself can fault.
//
//  Zero per-block overhead falls out of the same decision. Size class lives in a side table indexed
//  by chunk, so a 12-byte object costs 12 bytes of the 16-byte class and not one byte more — which
//  matters, because per-block overhead is the exact thing §6a set out to remove.
//
//  TWO INVARIANTS THAT MUST NEVER BE BROKEN, stated here because they are not local to any one
//  function:
//
//    1. THE POOL IS NEVER DISARMED, and the region is never released. Disarming would strand every
//       live pooled block: its next free would see `owns() == false` and hand it to `xr_free`.
//       There is no "off" switch after arming, only "never armed".
//    2. THE REGION IS NEVER DECOMMITTED. A chunk that empties stays committed and stays ours.
//       Handing pages back would make the region's ownership time-varying, and the discriminator's
//       exactness rests on it being permanent.
//
//  See dev/INSTABILITY_PLAN.md §6b for the obstacle this design answers and the gates it will be
//  scored against, and §6c for the control that proves the discriminator before it is wired to
//  anything.
////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// The control (dev/harness/pool_control) compiles this module on Linux with mmap standing in for
// VirtualAlloc, so the discriminator's logic can be proven in seconds instead of in a 21-minute
// CI build. The engine build takes the other branch.
#include <stddef.h>
#include <stdint.h>

// Empty in every branch of xrCore.h in this tree (the dllexport/dllimport pair is commented out
// for the static build), so defining it when absent changes no linkage -- it only lets this header
// compile standalone and in any include order.
#ifndef XRCORE_API
#	define XRCORE_API
#endif

#include <atomic>

// ---------------------------------------------------------------------------------------------
// THE DISCRIMINATOR. Inline on purpose: it is called on every luabind free and (as a safety net)
// on every `xr_free`, so it must cost a compare and not a call.
//
// Both globals are zero until `coop_pool_arm` succeeds, and a span of 0 makes `owns()` return
// false for every pointer in the address space including null — a disarmed pool cannot claim
// anything. `arm` publishes the span LAST with a release store, so a thread that observes a
// non-zero span necessarily observes the matching base.
XRCORE_API extern std::atomic<uintptr_t> g_coop_pool_base;
XRCORE_API extern std::atomic<size_t>    g_coop_pool_span;

inline bool coop_pool_owns(void const* p)
{
	// Unsigned wrap does both bounds in one compare: any p below base underflows to a huge value.
	size_t const span = g_coop_pool_span.load(std::memory_order_acquire);
	return (uintptr_t(p) - g_coop_pool_base.load(std::memory_order_relaxed)) < span;
}

// ---------------------------------------------------------------------------------------------
// Arming. `region_mib` is address space RESERVED, not committed — the cost of a large reservation
// is address space and a page-table entry or two, not memory. Chunks commit on demand.
//
// Call once, at boot, before any Lua state exists. Returns false if the reservation failed, in
// which case the pool stays disarmed and every path falls back to xrMemory unchanged.
XRCORE_API bool   coop_pool_arm(size_t region_mib);

// Open the pool to real callers. SEPARATE FROM ARMING ON PURPOSE, and the separation is what lets
// a failed selftest be survivable.
//
// `arm` reserves the region and makes `owns()` live; it does NOT let the pool serve anybody. Only
// this does. So the boot sequence is: arm, run the selftest, and call this ONLY if it passed. If
// it did not, the pool never hands out a block, no pointer outside the pool is ever inside the
// region, `owns()` is therefore false for every pointer any caller will present, and the server
// behaves exactly as an unflagged build — with a loud line in the log saying so.
//
// This is also why the invariant "never disarm" is not violated by a failing selftest: there is
// nothing live to strand, because nothing was ever served.
XRCORE_API void   coop_pool_enable();

// Allocation. Returns nullptr when the size is not pooled, the pool is disarmed, the pool has not
// been enabled, or the region is exhausted — in every one of those cases the caller must fall back
// to the engine allocator, and doing so stays correct because `owns()` will report false for that
// block for the rest of its life.
XRCORE_API void*  coop_pool_alloc(size_t size);

// Release. PRECONDITION: `coop_pool_owns(p)`. Returns false if p is inside the region but is not a
// valid block start (a wild or interior pointer); such a block is counted and LEAKED rather than
// pushed onto a free list, because corrupting a free list is worse than losing 16 bytes.
XRCORE_API bool   coop_pool_free(void* p);

// Usable bytes of a pooled block — the size class, not the requested size. Used by the realloc
// path to know how much to copy. PRECONDITION: `coop_pool_owns(p)`.
XRCORE_API size_t coop_pool_block_size(void const* p);

// The largest request the pool will serve. Sizes above this always fall back.
XRCORE_API size_t coop_pool_max_pooled();

// Chunk granularity. Exposed so the control can construct an address that is inside a COMMITTED
// chunk but past everything ever handed out from it -- the one shape that exercises the pool's
// "never handed out this slot" check and nothing else.
XRCORE_API size_t coop_pool_chunk_bytes();

// ---------------------------------------------------------------------------------------------
// THE CONTROL (§6c). Feeds the discriminator a population of pointers whose provenance is known by
// construction — foreign ones from three separate allocators plus the stack, statics, code, and
// the two addresses immediately outside the region's boundaries; own ones spanning several chunks
// and both size classes, including the first and last block of each chunk. Requires ZERO false
// positives and ZERO false negatives.
//
// Returns true on pass. `out_report` (optional, >= 512 bytes) receives a one-line summary for the
// log so the run's evidence carries the counts rather than just the verdict.
XRCORE_API bool   coop_pool_selftest(char* out_report, size_t report_bytes);

// ---------------------------------------------------------------------------------------------
// Counters, for the interval printer. All monotonic, all relaxed.
struct coop_pool_stats_t
{
	unsigned long long allocs;         // requests served from the pool
	unsigned long long frees;          // blocks returned to a free list
	unsigned long long fallbacks;      // requests the pool declined (size or exhaustion)
	unsigned long long chunks;         // chunks committed
	unsigned long long live;           // allocs - frees
	unsigned long long bad_frees;      // in-region pointers that were not valid block starts
	unsigned long long foreign_frees;  // frees routed away because owns() said not ours
	unsigned long long reclaimed;      // frees that arrived via the xrMemory safety net
};
XRCORE_API void coop_pool_get_stats(coop_pool_stats_t& out);
XRCORE_API void coop_pool_note_reclaimed();
