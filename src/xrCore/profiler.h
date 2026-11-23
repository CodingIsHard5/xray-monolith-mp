#pragma once

// Profiler backends
#define PROFILER_NONE   (0)
#define PROFILER_OPTICK (1)
#define PROFILER_AGG    (2) // frame-aggregated per-site timers dumped to CSV (see profiler_agg.cpp)

// Build-system switch: /DXRCORE_PROFILER_AGG selects the CSV backend in every module,
// overriding per-project XRCORE_PROFILER defines.
#if defined(XRCORE_PROFILER_AGG)
#	undef XRCORE_PROFILER
#	define XRCORE_PROFILER PROFILER_AGG
#endif

// Set active profiler backend
#if !defined(XRCORE_PROFILER)
    #define XRCORE_PROFILER PROFILER_NONE
//  #define XRCORE_PROFILER PROFILER_OPTICK
#endif

// Implement profiler macro interface
#if XRCORE_PROFILER == PROFILER_OPTICK
#	include "../3rd party/optick-git/src/optick.h"
#	define PROF_THREAD(Name) OPTICK_THREAD(Name)
#	define PROF_START_CAPTURE() OPTICK_START_CAPTURE()
#	define PROF_STOP_CAPTURE() OPTICK_STOP_CAPTURE()
#	define PROF_SAVE_CAPTURE(Name) OPTICK_SAVE_CAPTURE(Name)
#	define PROF_FRAME(Name) OPTICK_FRAME(Name)
#	define PROF_EVENT(Name) OPTICK_EVENT(Name)
#	define PROF_EVENT_DYNAMIC(...)	 static ::Optick::EventDescription* OPTICK_CONCAT(autogen_description_, __LINE__) = nullptr; \
							 OPTICK_CONCAT(autogen_description_, __LINE__) = ::Optick::CreateDescription(OPTICK_FUNC, __FILE__, __LINE__, ##__VA_ARGS__); \
							 ::Optick::Event OPTICK_CONCAT(autogen_event_, __LINE__)( *(OPTICK_CONCAT(autogen_description_, __LINE__)) ); 
#	define START_PROFILE(a) { PROF_EVENT(a)
#	define STOP_PROFILE		}
#elif XRCORE_PROFILER == PROFILER_AGG
#	include <atomic>
#	include <intrin.h>
namespace xrprof
{
	typedef unsigned long long ticks_t;

	// One per PROF_EVENT call site (function-local static, registered once).
	struct Site
	{
		const char* name;
		const char* func;
		const char* file;
		int line;
		// [0] = main thread, [1] = every other thread
		std::atomic<ticks_t> incl[2];
		std::atomic<ticks_t> self[2];
		std::atomic<ticks_t> calls[2];
		// last-frame snapshot of self[] and calls[]; touched only by frame() on the main thread (-perf_slow)
		ticks_t snap_self[2] = {0, 0};
		ticks_t snap_calls[2] = {0, 0};
		Site* next;
		Site(const char* func_, const char* file_, int line_, const char* name_ = nullptr);
	};

	struct Scope;
	struct ThreadState
	{
		Scope* top = nullptr;
		int slot = 1; // 0 for the main thread
	};

	extern std::atomic<bool> g_active;
	inline thread_local ThreadState t_state;

	struct Scope
	{
		Site* site;
		Scope* parent;
		ticks_t t0;
		ticks_t child;

		__forceinline explicit Scope(Site& s)
		{
			if (!g_active.load(std::memory_order_relaxed))
			{
				site = nullptr;
				return;
			}
			site = &s;
			child = 0;
			ThreadState& t = t_state;
			parent = t.top;
			t.top = this;
			t0 = __rdtsc();
		}

		__forceinline ~Scope()
		{
			if (!site)
				return;
			const ticks_t dt = __rdtsc() - t0;
			ThreadState& t = t_state;
			t.top = parent;
			if (parent)
				parent->child += dt;
			const int i = t.slot;
			site->incl[i].fetch_add(dt, std::memory_order_relaxed);
			site->self[i].fetch_add(dt > child ? dt - child : 0, std::memory_order_relaxed);
			site->calls[i].fetch_add(1, std::memory_order_relaxed);
		}
	};

	// Called once per frame on the main thread (PROF_FRAME).
	void frame();
	// Zero all sites and capture the next `frames` frames; results go to
	// $logs$/perf_sites_<tag>.csv and perf_frames_<tag>.csv.
	void start(unsigned frames, const char* tag);
	bool capturing();
	// true once a capture has completed and been written
	bool finished();
}
#	define XRPROF_CAT2(a, b) a##b
#	define XRPROF_CAT(a, b) XRPROF_CAT2(a, b)
#	define PROF_THREAD(Name)
#	define PROF_START_CAPTURE()
#	define PROF_STOP_CAPTURE()
#	define PROF_SAVE_CAPTURE(Name)
#	define PROF_FRAME(Name) ::xrprof::frame();
#	define PROF_EVENT(...) \
		static ::xrprof::Site XRPROF_CAT(xrprof_site_, __LINE__)(__FUNCTION__, __FILE__, __LINE__, ##__VA_ARGS__); \
		::xrprof::Scope XRPROF_CAT(xrprof_scope_, __LINE__)(XRPROF_CAT(xrprof_site_, __LINE__));
#	define PROF_EVENT_DYNAMIC(...) {};
#	define START_PROFILE(a) { PROF_EVENT(a)
#	define STOP_PROFILE		}
#else
#	define START_PROFILE(a) {
#	define STOP_PROFILE		}

#	define PROF_THREAD(Name)
#	define PROF_START_CAPTURE()
#	define PROF_STOP_CAPTURE()
#	define PROF_SAVE_CAPTURE(Name)
#	define PROF_FRAME(Name)
#	define PROF_EVENT(Name)
#	define PROF_EVENT_DYNAMIC(...) {};
#endif

// PROF_EVENT_HOT: for call sites hit thousands of times per frame. /DXRCORE_PROFILER_COARSE compiles them out
// (measurement builds only, to keep probe overhead out of the per-pass totals); otherwise identical to PROF_EVENT.
#if defined(XRCORE_PROFILER_COARSE)
#	define PROF_EVENT_HOT(...)
#else
#	define PROF_EVENT_HOT(...) PROF_EVENT(__VA_ARGS__)
#endif
