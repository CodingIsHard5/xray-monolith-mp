#include "stdafx.h"
#include "../xrCDB/frustum.h"
#include "xr_ioconsole.h"
#include "xr_input.h"
#include "../xrCore/profiler.h"

#pragma warning(disable:4995)
// mmsystem.h
#define MMNOSOUND
#define MMNOMIDI
#define MMNOAUX
#define MMNOMIXER
#define MMNOJOY
#include <mmsystem.h>
// d3dx9.h
#include <d3dx9.h>
#pragma warning(default:4995)

#include "x_ray.h"
#include "discord\discord.h"
#include "render.h"
#include "renderdoc_integration.h"
#include <chrono>

// must be defined before include of FS_impl.h
#define INCLUDE_FROM_ENGINE
#include "../xrCore/FS_impl.h"

#ifdef INGAME_EDITOR
# include "../include/editor/ide.hpp"
# include "engine_impl.hpp"
#endif // #ifdef INGAME_EDITOR

#include "xrSash.h"
#include "igame_persistent.h"
#include "shader_bus.h"

#include "CustomHUD.h"
#include "EngineThreading.h"
#include "IGame_Level.h"

#include "Rain.h"

#pragma comment( lib, "d3dx9.lib" )

ENGINE_API CRenderDevice Device;
ENGINE_API CLoadScreenRenderer load_screen_renderer;
ENGINE_API CRenderDevice* DevicePtr = nullptr;

ENGINE_API xr_atomic_bool g_bRendering = false;
extern ENGINE_API float psHUD_FOV;
extern float psCAM_ATTACHED_FOV;
extern ENGINE_API float psCamAttachedFOV;

BOOL g_bLoaded = FALSE;
ref_light precache_light = 0;

BOOL mt_calc_bones = TRUE;
BOOL psLua_ParallelGC = TRUE;
BOOL psLua_ParallelGC_debug = FALSE;

extern discord::Core* discord_core;
extern bool use_discord;

extern Fvector4 ps_ssfx_grass_interactive;

#ifdef ECO_RENDER
std::chrono::high_resolution_clock::time_point tlastf = std::chrono::high_resolution_clock::now(), tcurrentf = std::
	                                               chrono::high_resolution_clock::now();
std::chrono::duration<float> time_span;
ENGINE_API float refresh_rate = 0;
#endif // ECO_RENDER


BOOL CRenderDevice::Begin()
{
	PROF_EVENT("Render: Begin");

#ifndef DEDICATED_SERVER
	switch (m_pRender->GetDeviceState())
	{
	case IRenderDeviceRender::dsOK:
		break;

	case IRenderDeviceRender::dsLost:
		// If the device was lost, do not render until we get it back
		Sleep(33);
		return FALSE;
		break;

	case IRenderDeviceRender::dsNeedReset:
		// Check if the device is ready to be reset
		Reset();
		break;

	default:
		R_ASSERT(0);
	}

	m_pRender->Begin();

	FPU::m24r();
	g_bRendering = true;
#endif
	return TRUE;
}

void CRenderDevice::Clear()
{
	m_pRender->Clear();
}


void CRenderDevice::End(void)
{
	PROF_EVENT("Render: End");

#ifndef DEDICATED_SERVER


#ifdef INGAME_EDITOR
    bool load_finished = false;
#endif // #ifdef INGAME_EDITOR
	if (dwPrecacheFrame)
	{
		::Sound->set_master_volume(0.f);
		dwPrecacheFrame--;

		if (!dwPrecacheFrame)
		{
#ifdef INGAME_EDITOR
            load_finished = true;
#endif // #ifdef INGAME_EDITOR

			m_pRender->updateGamma();

			if (precache_light)
			{
				precache_light->set_active(false);
				precache_light.destroy();
			}
			::Sound->set_master_volume(1.f);

			m_pRender->ResourcesDestroyNecessaryTextures();

			Msg("* [x-ray]: Handled Necessary Textures Destruction");
			Memory.mem_compact();
			//Msg("* MEMORY USAGE: %lld K", Memory.mem_usage() / 1024);
			//Msg("* End of synchronization A[%d] R[%d]", b_is_Active, b_is_Ready);

#ifdef FIND_CHUNK_BENCHMARK_ENABLE
            g_find_chunk_counter.flush();
#endif // FIND_CHUNK_BENCHMARK_ENABLE
		}
	}

	g_bRendering = false;
	// end scene
	// Present goes here, so call OA Frame end.
	if (g_SASH.IsBenchmarkRunning())
		g_SASH.DisplayFrame(Device.fTimeGlobal);
	m_pRender->End();

# ifdef INGAME_EDITOR
    if (load_finished && m_editor)
        m_editor->on_load_finished();
# endif // #ifdef INGAME_EDITOR
#endif
}

void CRenderDevice::PreCache(u32 amount, bool b_draw_loadscreen, bool b_wait_user_input)
{
#ifdef DEDICATED_SERVER
    amount = 0;
#else
	if (m_pRender->GetForceGPU_REF())
		amount = 0;
#endif

	dwPrecacheFrame = dwPrecacheTotal = amount;
	if (amount && !precache_light && g_pGameLevel && g_loading_events.empty())
	{
		precache_light = ::Render->light_create();
		precache_light->set_shadow(false);
		precache_light->set_position(vCameraPosition);
		precache_light->set_color(255, 255, 255);
		precache_light->set_range(5.0f);
		precache_light->set_active(true);
	}

	if (amount && b_draw_loadscreen && !load_screen_renderer.b_registered)
	{
		load_screen_renderer.start(b_wait_user_input);
	}
}

int g_svDedicateServerUpdateReate = 100;

ENGINE_API xr_list<LOADING_EVENT> g_loading_events;

extern bool IsMainMenuActive(); //ECO_RENDER add

static HMONITOR g_StartupMonitor = NULL;

#include "MonitorList.h"

ENGINE_API int g_freeze_absorb_ms = 0;    // B1-T2 freeze: 0 = off (default)
ENGINE_API int g_freeze_absorb_armed = 0; // B1-T2 freeze: armed by script for one absorb

static void InitMonitor()
{
	if (g_StartupMonitor)
		return;

	HMONITOR chosen = ResolveSelectedMonitor();
	if (chosen)
	{
		MONITORINFO mi;
		mi.cbSize = sizeof(mi);
		if (GetMonitorInfoA(chosen, &mi))
		{
			g_StartupMonitor = chosen;
			return;
		}
		Msg("! vid_monitor: resolved handle is invalid, using Auto");
	}

	POINT cursorPos;
	GetCursorPos(&cursorPos);
	g_StartupMonitor = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTOPRIMARY);
}

ENGINE_API void ResetStartupMonitor()
{
	g_StartupMonitor = NULL;
}

ENGINE_API void SetStartupMonitor(HMONITOR h)
{
	g_StartupMonitor = h;
}

ENGINE_API HMONITOR GetStartupMonitor()
{
	InitMonitor();
	return g_StartupMonitor;
}

void GetMonitorResolution(u32& horizontal, u32& vertical)
{
	InitMonitor();

	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoA(g_StartupMonitor, &mi))
	{
		horizontal = mi.rcMonitor.right - mi.rcMonitor.left;
		vertical = mi.rcMonitor.bottom - mi.rcMonitor.top;
	}
	else
	{
		RECT desktop;
		const HWND hDesktop = GetDesktopWindow();
		GetWindowRect(hDesktop, &desktop);
		horizontal = desktop.right - desktop.left;
		vertical = desktop.bottom - desktop.top;
	}
}

void GetMonitorPosition(int& x, int& y)
{
	InitMonitor();

	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoA(g_StartupMonitor, &mi))
	{
		x = mi.rcMonitor.left;
		y = mi.rcMonitor.top;
	}
	else
	{
		x = 0;
		y = 0;
	}
}

float GetMonitorRefresh()
{
	DEVMODE lpDevMode;
	memset(&lpDevMode, 0, sizeof(DEVMODE));
	lpDevMode.dmSize = sizeof(DEVMODE);
	lpDevMode.dmDriverExtra = 0;

	if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &lpDevMode) == 0)
	{
		return 1.f / 60.f;
	}
	else
		return 1.f / lpDevMode.dmDisplayFrequency;
}

extern int ps_framelimiter;
extern u32 g_screenmode;

CTimer FreezeTimer;
void mt_FreezeThread(void *ptr) {
	float freezetime = 0.f;
	float repeatcheck = 500.f;

	while (true)
	{
		PROF_EVENT();

		if (g_loading_events.size())
			freezetime = 25000.0f;
		else
			freezetime = 5000.0f;

		repeatcheck = 500.f;

		START_PROFILE("Check timer");
		if (FreezeTimer.GetElapsed_sec()*1000.f > freezetime)
		{
			xrLogger::FlushLog();
			repeatcheck = 5000.f;
		}
		STOP_PROFILE;

		Sleep(repeatcheck);
	}
}

// -det_mode (test-only, Overseer ruling 2026-11-19): the exact equivalence check runs the per-frame worker tasks
// inline on the main thread, in their launch order, so gameplay is serial. Threading is judged separately.
#ifdef XRCORE_PROFILER_AGG
#define DET_SERIAL() xr_det_mode()
#else
#define DET_SERIAL() false
#endif

#if XRCORE_PROFILER == PROFILER_AGG
// Scripted capture for repeatable measurements:
//   -perf_capture <warmup_frames>,<frames>[,<tag>]  start automatically once a level is loaded and running
//   -perf_fixed_dt                                  fixed 33 ms simulation step during the capture
//   -perf_quit                                      quit when the capture has been written
// A capture can also be started by hand with the console command `prof_capture <frames> [tag]`.
static void perf_autocapture()
{
	static int state = -1; // -1 unparsed, 0 off, 1 warmup, 2 capturing, 3 done
	static u32 warmup = 0, frames = 0;
	static string64 tag = "auto";
	static bool fixed_dt = false, quit = false, saved_const_fps = false;

	if (state == -1)
	{
		state = 0;
		if (LPCSTR p = strstr(Core.Params, "-perf_capture "))
		{
			string256 args;
			if (sscanf(p + xr_strlen("-perf_capture "), "%255s", args) == 1)
			{
				string64 t = "";
				if (sscanf(args, "%u,%u,%63s", &warmup, &frames, t) >= 2 && frames)
				{
					if (t[0])
						xr_strcpy(tag, t);
					state = 1;
				}
			}
			fixed_dt = !!strstr(Core.Params, "-perf_fixed_dt");
			quit = !!strstr(Core.Params, "-perf_quit");
		}
	}

	switch (state)
	{
	case 1:
		if (!g_pGameLevel || !g_bLoaded || Device.dwPrecacheFrame || Device.Paused())
			break;
		if (warmup)
		{
			--warmup;
			break;
		}
		if (fixed_dt)
		{
			saved_const_fps = !!psDeviceFlags.test(rsConstantFPS);
			psDeviceFlags.set(rsConstantFPS, TRUE);
		}
		xrprof::start(frames, tag);
		state = 2;
		break;
	case 2:
		if (!xrprof::finished())
			break;
		if (fixed_dt)
			psDeviceFlags.set(rsConstantFPS, saved_const_fps);
		if (quit)
			Console->Execute("quit");
		state = 3;
		break;
	}
}
#endif

// ---------------------------------------------------------------------------------------------------------------
// S2 prototype (Caden 2026-11-25: "Experiment with 60 and 30 fps"): a FIXED SIMULATION TICK apart from the render frame.
// Console `sim_tick <hz>` or start switch `-sim_tick <hz>`; 0 = OFF = the stock path below, untouched.
// ON (in a running, unpaused level, not in det_mode, not with rsConstantFPS): real time (TimerGlobal) is compared with
// the simulated time; for every whole tick that is due, the whole per-frame update of today (seqFrame, and the game
// thread's half: scheduler, seqParallel, seqFrameMT) runs ONCE with a fixed step of 1/hz. The frame is rendered every
// time. A frame with no tick due only renders again: the frame counter moves (the renderer's per-frame guards need
// it), time does not, nothing is updated, input is not polled. No interpolation: between ticks the world stands still.
// At most 0.1 s of game time per frame (the stock clamp); what is over that is dropped and counted.
// With rsConstantFPS (the bench's -perf_fixed_dt) and sim_tick > 0, every frame is one tick of 1/hz instead of 33 ms.
// Known limits, named in plan-facts/S2-sim-tick-plan.md: code that reads the real timer inside the simulation
// (timeServer, the scheduler's wall-clock budget) is not changed; the freeze-absorb lever is not supported with it;
// particles are advanced once per RENDERED frame with one tick's step, so they run slow on frames with several ticks.
// Rates under 10 Hz are refused (a step over the stock 0.1 s clamp).
int g_sim_tick_hz = 0;
static int s_sim_hz = 0;             // the rate latched for this frame (a console change acts from the next frame)
static double s_sim_off = 0.0;       // dwTimeGlobal minus the real timer at the last sync (an earlier fixed-step
                                     // capture can leave simulated time ahead of real time)
static double s_sim_ms = 0.0;        // simulated time in ms, same origin as dwTimeGlobal
static bool s_sim_synced = false;    // false after any stock frame: resync to dwTimeGlobal on the next tick frame
static bool s_sim_tick_now = false;  // FrameMove: this call is one fixed tick
static u32 s_st_frames = 0, s_st_ticks = 0, s_st_n[4] = {0, 0, 0, 0}, s_st_dropped = 0, s_st_age_max = 0, s_st_last_log = 0;
static u64 s_st_age_sum = 0;
static u32 s_st_tick_real_ms = 0;    // real time (TimerGlobal) at the start of the newest tick: its input poll
// S2 stage 4a: the view every frame (declared in device.h)
ENGINE_API BOOL g_sim_view_gate = FALSE;
ENGINE_API u32 g_sim_view_epoch = 0;
ENGINE_API sim_view_present_fn g_sim_view_present = nullptr;
// S2 stage 4b: objects drawn between two ticks (declared in device.h)
ENGINE_API u32 g_sim_view_tick = 0;
ENGINE_API BOOL g_sim_render_lerp = FALSE;
ENGINE_API float g_sim_render_alpha = 1.f;
// S2 stage 4c: the time the first-person HUD animation shows (declared in device.h)
ENGINE_API double g_sim_hud_ms = 0.0;
static u64 s_sv_last_ticks = 0;      // TimerGlobal ticks at the previous presentation step
static u32 s_sv_last_epoch = 0;      // g_sim_view_epoch at the previous presentation step
static float s_sv_dt = 0.f;          // smoothed real frame time handed to the presentation step

// simulated time advances by one tick; sets the four time values the update reads
static void sim_tick_advance()
{
	if (!s_sim_tick_now)
	{
		// every-frame mode (rsConstantFPS): keep the fraction of a millisecond from tick to tick, resync only when
		// something else has moved dwTimeGlobal
		if (u32(s_sim_ms) != Device.dwTimeGlobal)
			s_sim_ms = double(Device.dwTimeGlobal);
	}
	else if (!s_sim_synced)
	{
		s_sim_ms = double(Device.dwTimeGlobal);
		s_sim_synced = true;
	}
	const double step = 1000.0 / double(s_sim_tick_now ? s_sim_hz : g_sim_tick_hz);
	const u32 before = u32(s_sim_ms); // not dwTimeGlobal: time dropped by the clamp must not show in this tick's step
	s_sim_ms += step;
	Device.fTimeDelta = float(step / 1000.0);
	Device.dwTimeGlobal = u32(s_sim_ms);
	Device.dwTimeDelta = Device.dwTimeGlobal - before;
	Device.fTimeGlobal = float(s_sim_ms / 1000.0);
}

// -1 = off (stock path); otherwise the number of ticks due in this frame (0 = render only)
static int sim_tick_plan()
{
	static bool parsed = false;
	if (!parsed)
	{
		parsed = true;
		if (LPCSTR p = strstr(Core.Params, "-sim_tick "))
		{
			int hz = 0;
			if (sscanf(p + xr_strlen("-sim_tick "), "%d", &hz) == 1 && hz > 0 && hz <= 240)
				g_sim_tick_hz = hz;
		}
	}
	s_sim_hz = g_sim_tick_hz;
	if (s_sim_hz < 10 || xr_det_mode() || psDeviceFlags.test(rsConstantFPS) || Device.Paused() || !g_pGameLevel ||
		!g_bLoaded || Device.dwPrecacheFrame)
	{
		s_sim_synced = false;
		return -1;
	}
	if (!s_sim_synced)
	{
		s_st_tick_real_ms = Device.GetTimerGlobal()->GetElapsed_ms();
		g_sim_view_epoch++;
		s_sim_ms = double(Device.dwTimeGlobal);
		s_sim_off = s_sim_ms - double(s_st_tick_real_ms);
		s_sim_synced = true;
	}
	const double step = 1000.0 / double(s_sim_hz);
	const double lag = double(Device.GetTimerGlobal()->GetElapsed_ms()) + s_sim_off - s_sim_ms;
	int n = lag > 0.0 ? int(lag / step) : 0;
	const int cap = _max(1, int(100.0 / step));
	if (n > cap)
	{
		s_st_dropped += u32(n - cap);
		s_sim_ms += double(n - cap) * step; // time jumps on, as the stock clock does when the step is clamped
		n = cap;
	}
	return n;
}

static void sim_tick_run(int n)
{
	for (int i = 0; i < n; ++i)
	{
		if (i > 0 && Device.Paused())
			break; // an earlier tick of this frame paused the game: no more game time in this frame
		if (i > 0)
		{
			// what on_idle does before every update: last update's deferred destroy queue
			xrCriticalSectionGuard guard(&Device.seqParallelBeforRenderCS);
			for (size_t it = 0; it < Device.seqParallelBeforRender.size(); ++it)
			{
				auto Callback = Device.seqParallelBeforRender[it];
				if (Callback)
					Callback();
			}
			Device.seqParallelBeforRender.clear();
		}
		s_st_tick_real_ms = Device.GetTimerGlobal()->GetElapsed_ms();
		g_sim_view_tick++; // S2 stage 4b: the number of this tick, read by the object captures inside it
		s_sim_tick_now = true;
		Device.FrameMove();
		s_sim_tick_now = false;
		if (i < n - 1)
		{
			// not the last tick of this frame: the game thread's half runs here, inline, and is joined
			XRay::Engine::GameThread();
			Device.secondary_tasks.wait();
		}
	}
	if (n == 0)
	{
		// render only: a new frame number for the renderer, no time, no update
		Device.dwFrame++;
		Core.dwFrame = Device.dwFrame;
		Device.fTimeDelta = 0.0f;
		Device.dwTimeDelta = 0;
	}
}

// S2 stage 4a: the presentation step, once per gated frame after the ticks: the actor's camera and hands with the REAL
// frame time (smoothed and clamped as the stock timer does, never 0), and alpha = how far real time has gone past the
// newest tick on the tick clock (0..1; the same lag sim_tick_plan counts ticks from, so it runs on evenly from frame to
// frame whenever the tick itself ran). On a frame with no tick the mouse motion is read here too, inside the same
// real frame time, so nothing a mouse-move handler calls sees a zero step. Simulated time (dwTimeGlobal, fTimeGlobal)
// is not touched; the two deltas are set for the calls only and put back, so the render and the next tick see what
// they saw before this stage.
static void sim_view_present(int n)
{
	if (Device.Paused())
		return; // paused inside this frame's ticks: no view step, the frame shows the last camera
	const u64 now = Device.GetTimerGlobal()->GetElapsed_ticks();
	const double step = 1.0 / double(s_sim_hz);
	float frame = float(step); // first step after a resync: one tick's time, as a guess
	if (s_sv_last_ticks && s_sv_last_epoch == g_sim_view_epoch && now > s_sv_last_ticks)
		frame = float(double(now - s_sv_last_ticks) / double(CPU::qpc_freq));
	const bool first = s_sv_last_epoch != g_sim_view_epoch;
	s_sv_last_ticks = now;
	s_sv_last_epoch = g_sim_view_epoch;
	s_sv_dt = first ? frame : 0.1f * s_sv_dt + 0.9f * frame;
	clamp(s_sv_dt, 0.001f, 0.1f);
	const double now_ms = double(now) * 1000.0 / double(CPU::qpc_freq);
	float alpha = float((now_ms + s_sim_off - s_sim_ms) / (step * 1000.0));
	clamp(alpha, 0.f, 1.f);
	// S2 stage 4b: the same alpha for the objects drawn in this frame's render, with or without an actor hook (not
	// reached when paused: that frame renders stock, as the paused frames after it do)
	g_sim_render_alpha = alpha;
	g_sim_render_lerp = TRUE;
	// S2 stage 4c: the same point in simulated time for the HUD animation (s_sim_ms is the newest tick's time, the
	// unrounded dwTimeGlobal) = clamp(real time on the tick clock, newest tick, newest tick + one step): it does not go
	// back while the epoch stays (beyond float rounding of alpha; the reader ignores a step back)
	g_sim_hud_ms = s_sim_ms + double(alpha) * step * 1000.0;

	const float saved_f = Device.fTimeDelta;
	const u32 saved_dw = Device.dwTimeDelta;
	Device.fTimeDelta = s_sv_dt;
	Device.dwTimeDelta = _max(1u, u32(s_sv_dt * 1000.f + 0.5f));
	if (n == 0 && pInput)
		pInput->MouseMoveOnly(); // no tick polled the input this frame: mouse motion only, buttons wait
	if (g_sim_view_present)
		g_sim_view_present(s_sv_dt, alpha);
	Device.fTimeDelta = saved_f;
	Device.dwTimeDelta = saved_dw;
}

// statistics for the experiment, taken where the render starts (after the frame limiter): ticks per frame, dropped
// ticks, age of the world state (time since the newest tick polled the input)
static void sim_tick_stat(int n)
{
	const u32 now = Device.GetTimerGlobal()->GetElapsed_ms();
	const u32 age = now - s_st_tick_real_ms;
	s_st_frames++;
	s_st_ticks += u32(n);
	s_st_n[n > 3 ? 3 : n]++;
	s_st_age_sum += age;
	if (age > s_st_age_max)
		s_st_age_max = age;
	if (now - s_st_last_log >= 10000)
	{
		Msg("* [SIMTICK] hz %d frames %u ticks %u n0 %u n1 %u n2 %u n3p %u dropped %u age_avg_ms %.2f age_max_ms %u time_global %u real_ms %u",
			s_sim_hz, s_st_frames, s_st_ticks, s_st_n[0], s_st_n[1], s_st_n[2], s_st_n[3], s_st_dropped,
			s_st_frames ? float(double(s_st_age_sum) / double(s_st_frames)) : 0.f, s_st_age_max, Device.dwTimeGlobal, now);
		s_st_frames = s_st_ticks = s_st_dropped = s_st_age_max = 0;
		s_st_n[0] = s_st_n[1] = s_st_n[2] = s_st_n[3] = 0;
		s_st_age_sum = 0;
		s_st_last_log = now;
	}
}
// ---------------------------------------------------------------------------------------------------------------

void CRenderDevice::on_idle()
{

	FreezeTimer.Start();

	if (!b_is_Ready)
	{
		Sleep(100);
		return;
	}

	PROF_FRAME("Main Thread");

	// -det_mode: test-only determinism switch for the equivalence harness. Every frame advances the
	// simulation by the same fixed step, so two runs see the same sequence of frames whatever the machine load.
	if (xr_det_mode())
		psDeviceFlags.set(rsConstantFPS, TRUE);
#if XRCORE_PROFILER == PROFILER_AGG
	perf_autocapture();
#endif

#ifdef DEDICATED_SERVER
    u32 FrameStartTime = TimerGlobal.GetElapsed_ms();
#endif

	START_PROFILE("Set stat gathering");
	if (psDeviceFlags.test(rsStatistic))
		g_bEnableStatGather = TRUE;
	else g_bEnableStatGather = FALSE;
	STOP_PROFILE;

	if (g_loading_events.size())
	{
		{
			PROF_EVENT("Loading...");
			if (g_loading_events.front()())
				g_loading_events.pop_front();
		}
		PROF_EVENT("LoadDraw");
		pApp->LoadDraw();
		return;
	}

	if (!Device.dwPrecacheFrame && !g_SASH.IsBenchmarkRunning() && g_bLoaded)
	{
		PROF_EVENT("Start xrSASH Benchmark");
		g_SASH.StartBenchmark();
	}

	if (Device.ModelDefferClear)
	{
		Device.ModelDefferClear();
	}

	{
		PROF_EVENT("seqParallelBeforRender");
		xrCriticalSectionGuard guard(&Device.seqParallelBeforRenderCS);

		// A callback invoked here can destroy an object that owns a particle
		// group, and PS::CParticleGroup::SItem::~SItem cancels its own pending
		// entry in this very vector. That cancellation clears the entry in
		// place instead of erasing it, so the vector never changes size while
		// we walk it. Skip the cleared entries; the clear() below drops them.
		// Index by position and copy each delegate before invoking it, so an
		// entry appended by a callback is still handled safely.
		for (size_t it = 0; it < Device.seqParallelBeforRender.size(); ++it)
		{
			auto Callback = Device.seqParallelBeforRender[it];
			if (Callback)
				Callback();
		}

		Device.seqParallelBeforRender.clear();
	}

#ifdef XRCORE_PROFILER_AGG
	xr_det_set_role(1); // gameplay phase of the main thread (see _random.h, test-only stream split)
#endif
	const int sim_n = sim_tick_plan(); // S2 prototype: -1 = off, the stock call below
	if (sim_n < 0)
		FrameMove();
	else
	{
		// S2 stage 4a, the view every frame: only on this branch (sim_n >= 0) is g_sim_view_gate TRUE, and only
		// until the presentation step is done; with sim_n < 0 nothing below runs and the actor does what it did.
		// S2 stage 4b: g_sim_render_lerp is FALSE here (cleared at the end of every frame) and only sim_view_present
		// sets it, so a stock frame (sim_n < 0) renders every object at &XFORM()
		g_sim_view_gate = TRUE;
		sim_tick_run(sim_n);
		sim_view_present(sim_n);
		g_sim_view_gate = FALSE;
	}
#ifdef XRCORE_PROFILER_AGG
	xr_det_set_role(2); // render phase from here to the end of the frame
#endif

    if (g_pGamePersistent != nullptr)
    {
        PROF_EVENT("Update Particles");
        g_pGamePersistent->UpdateParticles();
    }
    if (DET_SERIAL())
    	XRay::Engine::PreRenderThread();
    else
    	secondary_tasks.run(&XRay::Engine::PreRenderThread);

	// Precache
	if (dwPrecacheFrame)
	{
		PROF_EVENT("Precache frame");
		float factor = float(dwPrecacheFrame) / float(dwPrecacheTotal);
		float angle = PI_MUL_2 * factor;
		vCameraDirection.set(_sin(angle), 0, _cos(angle));
		vCameraDirection.normalize();
		vCameraTop.set(0, 1, 0);
		vCameraRight.crossproduct(vCameraTop, vCameraDirection);

		mView.build_camera_dir(vCameraPosition, vCameraDirection, vCameraTop);
	}

	// Matrices
	START_PROFILE("Matrices");
	mFullTransform.mul(mProject, mView);
	mFullTransformHud.mul(mProjectHud, mView);
	mFullTransformCam.mul(mProjectCam, mView);
	mFullTransformCamAttached.mul(mProjectCamAttached, mView);
	mFullTransform3DIcons.mul(mProject3DIcons, mView);
	m_pRender->SetCacheXform(mView, mProject);

	mViewHud_prev = mViewHud;
	mProjectHud_prev = mProjectHud;
	mFullTransformHud_prev = mFullTransformHud;
	mViewCam_prev = mViewCam;
	mProjectCam_prev = mProjectCam;
	mFullTransformCam_prev = mFullTransformCam;

	// Previous frame data -- 
	mView_prev = mView_saved;
	mProject_prev = mProject_saved;
	mFullTransform_prev = mFullTransform_saved; // Unused?

	m_pRender->SetCacheXform_prev(mView_prev, mProject_prev);

	mProjectHud.build_projection(deg2rad(psHUD_FOV * 83.f), fASPECT, R_VIEWPORT_NEAR, g_pGamePersistent->Environment().CurrentEnv->far_plane);
	mProjectCam.build_projection(deg2rad(psCamAttachedFOV * psCAM_ATTACHED_FOV), fASPECT, R_VIEWPORT_NEAR, g_pGamePersistent->Environment().CurrentEnv->far_plane);
	mProjectCamAttached = mProjectCam;
	
	mViewHud.set(mView);
	mViewCam.set(mView);
	mFullTransformHud.mul(mProjectHud, mViewHud);
	mFullTransformCam.mul(mProjectCam, mViewCam);
	mFullTransformCamAttached = mFullTransformCam;

	// Save previous frame grass benders data
	IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;

	GData.prev_pos[0].set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1);
	GData.prev_dir[0].set(0.0f, -99.0f, 0.0f, 1.0f);

	for (int pBend = 1; pBend < _min(16, ps_ssfx_grass_interactive.y + 1); pBend++)
	{
		GData.prev_pos[pBend].set(GData.pos[pBend].x, GData.pos[pBend].y, GData.pos[pBend].z, GData.radius_curr[pBend]);
		GData.prev_dir[pBend].set(GData.dir[pBend].x, GData.dir[pBend].y, GData.dir[pBend].z, GData.str[pBend]);
	}

	// Save wind animation position
	wind_anim_prev = wind_anim_saved;
	wind_anim_saved = g_pGamePersistent->Environment().wind_anim;

	//RCache.set_xform_view ( mView );
	//RCache.set_xform_project ( mProject );
	D3DXMatrixInverse((D3DXMATRIX*)&mInvFullTransform, 0, (D3DXMATRIX*)&mFullTransform);

	vCameraPosition_saved = vCameraPosition;
	mFullTransform_saved = mFullTransform;
	mView_saved = mView;
	mProject_saved = mProject;

	STOP_PROFILE;

    // TODO: Try to move this upper
    if (DET_SERIAL())
    	XRay::Engine::PreRenderPostTransformsThread();
    else
    	secondary_tasks.run(&XRay::Engine::PreRenderPostTransformsThread);
	if (mt_calc_bones && !DET_SERIAL())
		secondary_tasks.run(&XRay::Engine::CalculateBonesThread);
	else
		XRay::Engine::CalculateBonesThread();

	Device.isRendering = true;
	Device.LuaGCDone = false;
	Device.LuaGCCount = 0;

	if (sim_n == 0)
	{
		// S2 prototype, render-only frame: the game thread's half belongs to the tick, nothing to run
	}
	else if (DET_SERIAL())
		XRay::Engine::GameThread();
	else
		secondary_tasks.run(&XRay::Engine::GameThread);
	
#ifdef ECO_RENDER // ECO_RENDER START
	if (Device.Paused() || IsMainMenuActive() || ps_framelimiter)
	{
		PROF_EVENT("Eco Render");

		if (refresh_rate == 0)
			refresh_rate = GetMonitorRefresh();

		float rr;

		if (ps_framelimiter)
			rr = 1.f / ps_framelimiter;
		else
			rr = refresh_rate;

		time_span = std::chrono::duration_cast<std::chrono::duration<float>>(tcurrentf - tlastf);
		while (time_span.count() < rr)
		{
			tcurrentf = std::chrono::high_resolution_clock::now();
			time_span = std::chrono::duration_cast<std::chrono::duration<float>>(tcurrentf - tlastf);
		}
		tlastf = std::chrono::high_resolution_clock::now();
	}
#endif // ECO_RENDER END

	if (sim_n >= 0)
		sim_tick_stat(sim_n);
#ifndef DEDICATED_SERVER
	Statistic->RenderTOTAL_Real.FrameStart();
	Statistic->RenderTOTAL_Real.Begin();

	if (b_is_Active && Begin())
	{
		START_PROFILE("Process seqRender");
		seqRender.Process(rp_Render);
		STOP_PROFILE;

		if (psDeviceFlags.test(rsCameraPos) || psDeviceFlags.test(rsStatistic) || Statistic->errors.size())
		{
			PROF_EVENT("Draw statistics");
			Statistic->Show();
		}

		End();
	}
	Statistic->RenderTOTAL_Real.End();
	Statistic->RenderTOTAL_Real.FrameEnd();
	Statistic->RenderTOTAL.accum = Statistic->RenderTOTAL_Real.accum;
#endif 
	Device.isRendering = false;

	secondary_tasks.wait();
	g_sim_render_lerp = FALSE; // S2 stage 4b: set by sim_view_present for this frame's render only; cleared after the join
#ifdef XRCORE_PROFILER_AGG
	xr_det_set_role(1); // render phase over: anything the main thread does outside it (level load, spawns) is gameplay
#endif

	if (psLua_ParallelGC_debug && psLua_ParallelGC && Device.LuaGCDebug)
	{
		Device.LuaGCDebug();
	}

#ifdef DEDICATED_SERVER
    u32 FrameEndTime = TimerGlobal.GetElapsed_ms();
    u32 FrameTime = (FrameEndTime - FrameStartTime);
    u32 DSUpdateDelta = 1000 / g_svDedicateServerUpdateReate;
    if (FrameTime < DSUpdateDelta)
        Sleep(DSUpdateDelta - FrameTime);
#endif
	if (!b_is_Active)
		Sleep(1);
}

#ifdef INGAME_EDITOR
void CRenderDevice::message_loop_editor()
{
    m_editor->run();
    m_editor_finalize(m_editor);
    xr_delete(m_engine);
}
#endif // #ifdef INGAME_EDITOR

void CRenderDevice::Screenshot()
{
	PROF_EVENT();
	Render->Screenshot();
}

void CRenderDevice::message_loop()
{
#ifdef INGAME_EDITOR
    if (editor())
    {
        message_loop_editor();
        return;
    }
#endif
	MSG msg;
	PeekMessage(&msg, NULL, 0U, 0U, PM_NOREMOVE);
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
		on_idle();
	}
}

void mt_DiscordThread(void*)
{
	while (true)
	{
		if (!pApp)
		{
			Msg("[Discord] pApp destroyed, killing thread");
			return;
		}

		//Discord
		if (use_discord && psDeviceFlags2.test(rsDiscord))
		{
			START_PROFILE("Discord");
			discord_core->RunCallbacks();
			updateDiscordPresence();
			STOP_PROFILE;
			Sleep(int(discord_update_rate * 1000));
		}
		else
		{
			Sleep(1000); // Sleep for 1 second if Discord is not used or disabled
		}
	}
}

void CRenderDevice::Run()
{
	// DUMP_PHASE;
	g_bLoaded = FALSE;
	Log("Starting engine...");
	thread_name("X-RAY Primary thread");
	// Startup timers and calculate timer delta
	dwTimeGlobal = 0;
	Timer_MM_Delta = 0;
	{
		u32 time_mm = timeGetTime();
		while (timeGetTime() == time_mm); // wait for next tick
		u32 time_system = timeGetTime();
		u32 time_local = TimerAsync();
		Timer_MM_Delta = time_system - time_local;
	}

	// Start extra threads
	thread_spawn(mt_FreezeThread, "Freeze detecting thread", 0, 0);
	thread_spawn(mt_DiscordThread, "X-RAY Discord thread", 0, 0);

	// Message cycle
	seqAppStart.Process(rp_AppStart);

	//m_pRender->ClearTarget();
	SetForegroundWindow(m_hWnd);
	message_loop();

	seqAppEnd.Process(rp_AppEnd);

	secondary_tasks.wait();
	ParticleWorkerCallback.clear();
}

u32 app_inactive_time = 0;
u32 app_inactive_time_start = 0;

void CRenderDevice::FrameMove()
{
	PROF_EVENT("Render: Frame Move");

	if (InterlockedExchange(&g_monitor_list_dirty, 0))
		refresh_vid_monitor_list();

	dwFrame++;
	Core.dwFrame = dwFrame;
	renderdoc_poll_captures();
	dwTimeContinual = TimerMM.GetElapsed_ms() - app_inactive_time;
	if (xr_det_mode())
		dwTimeContinual = dwTimeGlobal + 33; // det_mode: no wall clock in script-visible time
	if (s_sim_tick_now || (g_sim_tick_hz >= 10 && psDeviceFlags.test(rsConstantFPS) && !xr_det_mode()))
	{
		// S2 prototype: one fixed tick of 1/hz (set by sim_tick_run, or every frame under rsConstantFPS)
		PROF_EVENT("Sim tick");
		Timer.Start(); // keep the stock frame timer fresh for the frame on which the switch goes off
		sim_tick_advance();
	}
	else if (psDeviceFlags.test(rsConstantFPS))
	{
		PROF_EVENT("Constant FPS");

		// 20ms = 50fps
		//fTimeDelta = 0.020f;
		//fTimeGlobal += 0.020f;
		//dwTimeDelta = 20;
		//dwTimeGlobal += 20;
		// 33ms = 30fps
		fTimeDelta = 0.033f;
		fTimeGlobal += 0.033f;
		dwTimeDelta = 33;
		dwTimeGlobal += 33;
	}
	else
	{
		PROF_EVENT("Timer FPS");

		// Timer
		float fPreviousFrameTime = Timer.GetElapsed_sec();
		Timer.Start(); // previous frame
		fTimeDelta = 0.1f * fTimeDelta + 0.9f * fPreviousFrameTime;
		// smooth random system activity - worst case ~7% error
		//fTimeDelta = 0.7f * fTimeDelta + 0.3f*fPreviousFrameTime; // smooth random system activity
		if (fTimeDelta > .1f)
			fTimeDelta = .1f; // limit to 15fps minimum
		if (fTimeDelta <= 0.f)
			fTimeDelta = EPS_S + EPS_S; // limit to 15fps minimum
		if (Paused())
			fTimeDelta = 0.0f;
		// u64 qTime = TimerGlobal.GetElapsed_clk();
		// B1-T2 freeze (Caden: "make a setting to freeze it"): while the bridge has ARMED a freeze
		// (g_freeze_absorb_armed, set by script just before the bridge suspends the process) and absorbing is on
		// (g_freeze_absorb_ms > 0), a wall gap longer than g_freeze_absorb_ms since the last frame is removed from
		// TimerGlobal, minus one 33 ms frame, so time_global, game time and every timer see one normal frame. Each
		// absorb is logged and disarms itself, so a later hitch is never swallowed. Both default 0: engine unchanged.
		if (g_freeze_absorb_ms > 0 && g_freeze_absorb_armed)
		{
			const u32 now_ms = TimerGlobal.GetElapsed_ms();
			const u32 gap_ms = now_ms - dwTimeGlobal;
			if (gap_ms > (u32)g_freeze_absorb_ms)
			{
				const u32 drop_ms = gap_ms - 33;
				TimerGlobal.AbsorbGap(u64(drop_ms) * CPU::qpc_freq / 1000);
				g_freeze_absorb_armed = 0;
				Msg("* [FREEZE] absorbed %u ms of a %u ms gap (time_global %u)", drop_ms, gap_ms, dwTimeGlobal);
			}
		}
		fTimeGlobal = TimerGlobal.GetElapsed_sec(); //float(qTime)*CPU::cycles2seconds;
		u32 _old_global = dwTimeGlobal;
		dwTimeGlobal = TimerGlobal.GetElapsed_ms();
		dwTimeDelta = dwTimeGlobal - _old_global;
	}

	// Frame move
	Statistic->EngineTOTAL.Begin();

	ShaderBus::frame_latch();
	
	START_PROFILE("Process seqFrame");
	Device.seqFrame.Process(rp_Frame);
	STOP_PROFILE;
	
	g_bLoaded = TRUE;
	
	Statistic->EngineTOTAL.End();
}

ENGINE_API BOOL bShowPauseString = TRUE;

void CRenderDevice::Pause(BOOL bOn, BOOL bTimer, BOOL bSound, LPCSTR reason)
{
	PROF_EVENT();

	static int snd_emitters_ = -1;

	if (g_bBenchmark)
		return;
#ifndef DEDICATED_SERVER
	if (bOn)
	{
		if (!Paused())
			bShowPauseString =
#ifdef INGAME_EDITOR
                editor() ? FALSE :
#endif // #ifdef INGAME_EDITOR
#ifdef DEBUG
                !xr_strcmp(reason, "li_pause_key_no_clip") ? FALSE :
#endif // DEBUG
				TRUE;

		if (bTimer && (!g_pGamePersistent || g_pGamePersistent->CanBePaused()))
		{
			g_pauseMngr().Pause(true);
#ifdef DEBUG
            if (!xr_strcmp(reason, "li_pause_key_no_clip"))
                TimerGlobal.Pause(FALSE);
#endif // DEBUG
		}

		if (bSound && ::Sound)
		{
			snd_emitters_ = ::Sound->pause_emitters(true);
#ifdef DEBUG
			// Log("snd_emitters_[true]",snd_emitters_);
#endif // DEBUG
		}
	}
	else
	{
		if (bTimer && g_pauseMngr().Paused())
		{
			fTimeDelta = EPS_S + EPS_S;
			g_pauseMngr().Pause(false);
		}

		if (bSound)
		{
			if (snd_emitters_ > 0) //avoid crash
			{
				snd_emitters_ = ::Sound->pause_emitters(false);
#ifdef DEBUG
				// Log("snd_emitters_[false]",snd_emitters_);
#endif
			}
			else
			{
#ifdef DEBUG
                Log("Sound->pause_emitters underflow");
#endif
			}
		}
	}

#endif
}

bool CRenderDevice::Paused()
{
	return g_pauseMngr().Paused();
}

void CRenderDevice::OnWM_Activate(WPARAM wParam, LPARAM lParam)
{
	u16 fActive = LOWORD(wParam);
	BOOL fMinimized = (BOOL)HIWORD(wParam);
	BOOL bActive = ((fActive != WA_INACTIVE) && (!fMinimized)) ? TRUE : FALSE;

	if (psDeviceFlags2.test(rsAlwaysActive) && g_screenmode != 2)
	{
		Device.b_is_Active = TRUE;

		if (Device.b_hide_cursor != bActive)
		{
			Device.b_hide_cursor = bActive;

			if (Device.b_hide_cursor)
			{
				ShowCursor(FALSE);
				if (m_hWnd)
				{
					RECT winRect;
					GetClientRect(m_hWnd, &winRect);
					MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<LPPOINT>(&winRect), 2);
					ClipCursor(&winRect);
				}
				pInput->OnAppActivate();
			}
			else
			{
				ShowCursor(TRUE);
				ClipCursor(NULL);
				pInput->OnAppDeactivate();
			}
		}

		return;
	}

	if (bActive != Device.b_is_Active)
	{
		Device.b_is_Active = bActive;

		if (Device.b_is_Active)
		{
			Device.seqAppActivate.Process(rp_AppActivate);
			app_inactive_time += TimerMM.GetElapsed_ms() - app_inactive_time_start;

#ifndef DEDICATED_SERVER
# ifdef INGAME_EDITOR
            if (!editor())
# endif // #ifdef INGAME_EDITOR
			ShowCursor(FALSE);
			if (m_hWnd)
			{
				RECT winRect;
				GetClientRect(m_hWnd, &winRect);
				MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<LPPOINT>(&winRect), 2);
				ClipCursor(&winRect);
			}
#endif // #ifndef DEDICATED_SERVER
		}
		else
		{
			app_inactive_time_start = TimerMM.GetElapsed_ms();
			Device.seqAppDeactivate.Process(rp_AppDeactivate);
			ShowCursor(TRUE);
			ClipCursor(NULL);
		}
	}
}

void CRenderDevice::AddSeqFrame(pureFrame* f, bool mt)
{
	PROF_EVENT();

	if (mt)
		seqFrameMT.Add(f, REG_PRIORITY_HIGH);
	else
		seqFrame.Add(f, REG_PRIORITY_LOW);
}

void CRenderDevice::RemoveSeqFrame(pureFrame* f)
{
	PROF_EVENT();

	seqFrameMT.Remove(f);
	seqFrame.Remove(f);
}

CLoadScreenRenderer::CLoadScreenRenderer()
	: b_registered(false)
{
}

void CLoadScreenRenderer::start(bool b_user_input)
{
	PROF_EVENT();

	Device.seqRender.Add(this, 0);
	b_registered = true;
	b_need_user_input = b_user_input;
}

void CLoadScreenRenderer::stop()
{
	PROF_EVENT();

	if (!b_registered)
		return;
	Device.seqRender.Remove(this);
	pApp->destroy_loading_shaders();
	b_registered = false;
	b_need_user_input = false;
}

void CLoadScreenRenderer::OnRender()
{
	PROF_EVENT();

	pApp->load_draw_internal();
}

void CRenderDevice::CSecondVPParams::SetSVPActive(bool bState) //--#SM+#-- +SecondVP+
{
	isActive = bState;
	if (g_pGamePersistent != NULL)
		g_pGamePersistent->m_pGShaderConstants->m_blender_mode.z = (isActive ? 1.0f : 0.0f);
}

bool CRenderDevice::CSecondVPParams::IsSVPFrame() //--#SM+#-- +SecondVP+
{
	return IsSVPActive() && Device.dwFrame % frameDelay == 0;
}
