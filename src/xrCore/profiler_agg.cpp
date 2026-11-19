#include "stdafx.h"

// Frame-aggregated CSV profiler backend (XRCORE_PROFILER == PROFILER_AGG).
// Reuses the existing PROF_EVENT call sites: each site accumulates inclusive and
// self time, split into "main thread" and "all other threads", over a fixed number
// of frames, then the totals are written as CSV. Meant for scripted, repeatable
// before/after measurements where an interactive profiler UI is not available.

#if XRCORE_PROFILER == PROFILER_AGG

#include <vector>
#include <utility>
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

	// -perf_slow <ms>: for every frame longer than <ms>, keep the sites with the largest self time in that
	// frame (perf_slow_<tag>.csv). Time of a scope is booked when the scope ends, so work that started on a
	// worker in an earlier frame is charged to the frame it finished in.
	struct SlowRow
	{
		unsigned frame;
		float frame_ticks;
		const Site* site;
		int slot;
		ticks_t self;
		ticks_t calls;
	};
	static std::vector<SlowRow> g_slow;
	static float g_slow_ms = 0.f;
	static double g_ms_per_tick_est = 0.0;
	static const int SLOW_TOP = 14;
	static const size_t SLOW_MAX_ROWS = 600 * SLOW_TOP;

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
				s->snap_self[i] = 0;
				s->snap_calls[i] = 0;
			}
		g_slow.clear();
		g_slow_ms = 0.f;
		if (Core.Params)
			if (LPCSTR p = strstr(Core.Params, "-perf_slow "))
				sscanf(p + xr_strlen("-perf_slow "), "%f", &g_slow_ms);
		if (g_slow_ms > 0.f)
		{
			g_slow.reserve(SLOW_MAX_ROWS);
			LARGE_INTEGER q0, q1, qf;
			QueryPerformanceFrequency(&qf);
			QueryPerformanceCounter(&q0);
			const ticks_t t0 = __rdtsc();
			do
				QueryPerformanceCounter(&q1);
			while (q1.QuadPart - q0.QuadPart < qf.QuadPart / 100); // 10 ms calibration, once per capture
			g_ms_per_tick_est = double(q1.QuadPart - q0.QuadPart) * 1000.0 / double(qf.QuadPart) / double(__rdtsc() - t0);
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

		if (g_slow_ms > 0.f)
		{
			xr_sprintf(fn, "perf_slow_%s.csv", g_tag);
			FS.update_path(path, "$logs$", fn);
			if (FILE* f = fopen(path, "wt"))
			{
				fprintf(f, "#threshold_ms,%.3f\nframe,frame_ms,thread,self_ms,calls,name,file,line\n", g_slow_ms);
				for (const SlowRow& r : g_slow)
				{
					fprintf(f, "%u,%.4f,%s,%.4f,%llu,", r.frame, r.frame_ticks * ms_per_tick, r.slot ? "other" : "main",
					        double(r.self) * ms_per_tick, r.calls);
					csv_field(f, r.site->name);
					fputc(',', f);
					csv_field(f, r.site->file);
					fprintf(f, ",%d\n", r.site->line);
				}
				fclose(f);
			}
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
		if (g_slow_ms > 0.f)
		{
			const bool slow = double(now - g_tsc_last) * g_ms_per_tick_est > g_slow_ms && g_slow.size() + SLOW_TOP <= SLOW_MAX_ROWS;
			SlowRow top[SLOW_TOP];
			int n = 0;
			for (Site* s = g_sites.load(std::memory_order_acquire); s; s = s->next)
				for (int i = 0; i < 2; ++i)
				{
					const ticks_t cur = s->self[i].load(std::memory_order_relaxed), calls = s->calls[i].load(std::memory_order_relaxed);
					const ticks_t d = cur - s->snap_self[i], dc = calls - s->snap_calls[i];
					s->snap_self[i] = cur;
					s->snap_calls[i] = calls;
					if (!slow || !d)
						continue;
					// insertion into a small descending list
					int k;
					if (n < SLOW_TOP)
						k = n++;
					else if (top[SLOW_TOP - 1].self >= d)
						continue;
					else
						k = SLOW_TOP - 1;
					top[k] = SlowRow{(unsigned)g_frame_ms.size() - 1, float(now - g_tsc_last), s, i, d, dc};
					for (; k > 0 && top[k - 1].self < top[k].self; --k)
						std::swap(top[k - 1], top[k]);
				}
			for (int k = 0; k < n; ++k)
				g_slow.push_back(top[k]);
		}
		g_tsc_last = now;
		if (!--g_frames_left)
			dump();
	}
}

#endif
