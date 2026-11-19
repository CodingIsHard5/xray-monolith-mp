#include "stdafx.h"

// Frame-aggregated CSV profiler backend (XRCORE_PROFILER == PROFILER_AGG).
// Reuses the existing PROF_EVENT call sites: each site accumulates inclusive and
// self time, split into "main thread" and "all other threads", over a fixed number
// of frames, then the totals are written as CSV. Meant for scripted, repeatable
// before/after measurements where an interactive profiler UI is not available.

#if XRCORE_PROFILER == PROFILER_AGG

#include <vector>
#include <stdio.h>

namespace xrprof
{
	std::atomic<bool> g_active{false};

	static std::atomic<Site*> g_sites{nullptr};
	static std::vector<float> g_frame_ms;
	static unsigned g_frames_left = 0;
	static bool g_finished = false;
	static char g_tag[64] = "";
	static ticks_t g_tsc_start = 0, g_tsc_last = 0;
	static LARGE_INTEGER g_qpc_start;

	Site::Site(const char* func_, const char* file_, int line_, const char* name_)
		: name(name_ ? name_ : func_), func(func_), file(file_), line(line_)
	{
		for (int i = 0; i < 2; ++i)
		{
			incl[i].store(0, std::memory_order_relaxed);
			self[i].store(0, std::memory_order_relaxed);
			calls[i].store(0, std::memory_order_relaxed);
		}
		next = g_sites.load(std::memory_order_relaxed);
		while (!g_sites.compare_exchange_weak(next, this, std::memory_order_release, std::memory_order_relaxed))
		{
		}
	}

	bool capturing() { return g_active.load(std::memory_order_relaxed); }
	bool finished() { return g_finished; }

	void start(unsigned frames, const char* tag)
	{
		if (capturing() || !frames)
			return;
		for (Site* s = g_sites.load(std::memory_order_acquire); s; s = s->next)
			for (int i = 0; i < 2; ++i)
			{
				s->incl[i].store(0, std::memory_order_relaxed);
				s->self[i].store(0, std::memory_order_relaxed);
				s->calls[i].store(0, std::memory_order_relaxed);
			}
		xr_strcpy(g_tag, tag && *tag ? tag : "capture");
		g_frame_ms.clear();
		g_frame_ms.reserve(frames);
		g_frames_left = frames;
		g_finished = false;
		t_state.slot = 0; // caller is the main thread
		QueryPerformanceCounter(&g_qpc_start);
		g_tsc_start = g_tsc_last = __rdtsc();
		g_active.store(true, std::memory_order_release);
		Msg("* [xrprof] capture '%s' started: %u frames", g_tag, frames);
	}

	// CSV fields never contain quotes in practice; commas inside names are replaced.
	static void csv_field(FILE* f, const char* s)
	{
		for (; s && *s; ++s)
			fputc((*s == ',' || *s == '\n' || *s == '\r') ? ';' : *s, f);
	}

	static void dump()
	{
		g_active.store(false, std::memory_order_release);

		LARGE_INTEGER qpc_end, qpc_freq;
		QueryPerformanceCounter(&qpc_end);
		QueryPerformanceFrequency(&qpc_freq);
		const ticks_t tsc_end = __rdtsc();
		const double total_ms = double(qpc_end.QuadPart - g_qpc_start.QuadPart) * 1000.0 / double(qpc_freq.QuadPart);
		const double ms_per_tick = (tsc_end > g_tsc_start) ? total_ms / double(tsc_end - g_tsc_start) : 0.0;

		string_path fn, path;
		xr_sprintf(fn, "perf_sites_%s.csv", g_tag);
		FS.update_path(path, "$logs$", fn);
		if (FILE* f = fopen(path, "wt"))
		{
			fprintf(f, "#frames,%u\n#total_ms,%.3f\n", (unsigned)g_frame_ms.size(), total_ms);
			fprintf(f, "name,file,line,calls_main,incl_main_ms,self_main_ms,calls_other,incl_other_ms,self_other_ms\n");
			for (Site* s = g_sites.load(std::memory_order_acquire); s; s = s->next)
			{
				const ticks_t c0 = s->calls[0].load(), c1 = s->calls[1].load();
				if (!c0 && !c1)
					continue;
				csv_field(f, s->name);
				fputc(',', f);
				csv_field(f, s->file);
				fprintf(f, ",%d,%llu,%.4f,%.4f,%llu,%.4f,%.4f\n", s->line,
				        c0, double(s->incl[0].load()) * ms_per_tick, double(s->self[0].load()) * ms_per_tick,
				        c1, double(s->incl[1].load()) * ms_per_tick, double(s->self[1].load()) * ms_per_tick);
			}
			fclose(f);
		}
		else
			Msg("! [xrprof] cannot write %s", path);

		xr_sprintf(fn, "perf_frames_%s.csv", g_tag);
		FS.update_path(path, "$logs$", fn);
		if (FILE* f = fopen(path, "wt"))
		{
			fprintf(f, "frame,ms\n");
			for (size_t i = 0; i < g_frame_ms.size(); ++i)
				fprintf(f, "%u,%.4f\n", (unsigned)i, g_frame_ms[i] * ms_per_tick);
			fclose(f);
		}

		Msg("* [xrprof] capture '%s' finished: %u frames, %.1f ms total, %.3f ms/frame avg", g_tag,
		    (unsigned)g_frame_ms.size(), total_ms, g_frame_ms.empty() ? 0.0 : total_ms / double(g_frame_ms.size()));
		g_finished = true;
	}

	void frame()
	{
		if (!g_active.load(std::memory_order_relaxed))
			return;
		const ticks_t now = __rdtsc();
		// stored as raw ticks (float keeps ~7 digits: enough for one frame), scaled in dump()
		g_frame_ms.push_back(float(now - g_tsc_last));
		g_tsc_last = now;
		if (!--g_frames_left)
			dump();
	}
}

#endif
