#ifndef _LOCAL_RAND
#define _LOCAL_RAND

/*
u32 dwRandSeed;
IC u32 dwfRandom(u32 dwRange)
{
u32 dwResult;
__asm {
mov eax,dwRange _eax = dwRange
imul edx,dwRandSeed,08088405H
inc edx
mov dwRandSeed,edx dwRandSeed = (dwRandSeed * 08088405H)+1
mul edx return (u64(dwRange) * u64(dwRandSeed)) >> 32
mov dwResult,edx
}
return(dwResult);
}
*/

#ifdef XRCORE_PROFILER_AGG
// Harness builds only: record who draws from the global ::Random (frame, thread, return address), so the
// first draw that differs between two -det_mode runs can be named. See _math.cpp.
#include <intrin.h>
class CRandom;
XRCORE_API extern CRandom Random;
XRCORE_API extern volatile long xr_det_trace_armed;
XRCORE_API void xr_det_trace_draw(void* return_address);
XRCORE_API void xr_det_trace_start();
XRCORE_API void xr_det_trace_dump(const char* path);
// Scheduler execution trace (test-only): which scheduled object was updated in which frame, with its interval.
XRCORE_API void xr_det_sched_trace(const char* name, u32 elapsed, u32 next_interval, float scale);
// Stream split (test-only, armed by det_reseed): draws on ::Random that are not gameplay (render phase of the
// main thread, cosmetic worker threads) are served from separate generators, so their run-to-run interleaving
// cannot shift the gameplay stream. Roles: 0 = worker default (own per-thread stream), 1 = gameplay (::Random
// itself), 2 = main-thread render phase (one shared render stream).
XRCORE_API extern volatile long xr_det_split_armed;
XRCORE_API int xr_det_set_role(int role); // returns the previous role of this thread
XRCORE_API bool xr_det_alt_draw(s32& result);
#endif

class CRandom
{
private:
	volatile s32 holdrand;
public:
	CRandom() : holdrand(1)
	{
	};

	CRandom(s32 _seed) : holdrand(_seed)
	{
	};

	IC void seed(s32 val) { holdrand = val; }
	IC s32 seed() const { return holdrand; }
	IC s32 maxI() { return 32767; }

	ICN s32 randI()
	{
#ifdef XRCORE_PROFILER_AGG
		if (this == &Random)
		{
			if (xr_det_trace_armed == 2) // early diagnosis trace: every stream
				xr_det_trace_draw(_ReturnAddress());
			if (xr_det_split_armed)
			{
				s32 alt;
				if (xr_det_alt_draw(alt))
					return alt;
			}
			if (xr_det_trace_armed == 1)
				xr_det_trace_draw(_ReturnAddress());
		}
#endif
		return (((holdrand = holdrand * 214013L + 2531011L) >> 16) & 0x7fff);
	}
	IC s32 randI(s32 max)
	{
        if (max <= 0)
            return 0;

		return randI() % max;
	}

	IC s32 randI(s32 min, s32 max) { return min + randI(max - min); }
	IC s32 randIs(s32 range) { return randI(-range, range); }
	IC s32 randIs(s32 range, s32 offs) { return offs + randIs(range); }

	IC float maxF() { return 32767.f; }
	IC float randF() { return float(randI()) / maxF(); }
	IC float randF(float max) { return randF() * max; }
	IC float randF(float min, float max) { return min + randF(max - min); }
	IC float randFs(float range) { return randF(-range, range); }
	IC float randFs(float range, float offs) { return offs + randFs(range); }
};

XRCORE_API extern CRandom Random;

// -det_mode (test-only, off by default): every generator that is normally seeded from a clock gets a
// fixed seed instead, so two runs of the equivalence harness draw the same numbers.
XRCORE_API bool xr_det_mode();
IC u32 xr_det_seed(u32 clock_seed, u32 salt = 0) { return xr_det_mode() ? (0x12071980u ^ salt) : clock_seed; }

#endif
