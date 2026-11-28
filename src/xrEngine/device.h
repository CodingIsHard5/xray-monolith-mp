#pragma once

// Note:
// ZNear - always 0.0f
// ZFar - always 1.0f

//class ENGINE_API CResourceManager;
//class ENGINE_API CGammaControl;

#include "pure.h"
#include "../xrCore/xrSyncronize.h"
//#include "hw.h"
#include "../xrcore/ftimer.h"
#include "stats.h"
//#include "shader.h"
//#include "R_Backend.h"

#include "../build_config_defines.h"

#define VIEWPORT_NEAR  Device.ViewportNear //0.2f
#define R_VIEWPORT_NEAR 0.005f

// Shared projection volume for 3D UI icons. X/Y placement and model fitting
// use the same height, so the value defines a coordinate system rather than
// the final on-screen size.
constexpr float UI_3D_ICON_ORTHO_HEIGHT = 2.f;
constexpr float UI_3D_ICON_NEAR_PLANE = 0.01f;
constexpr float UI_3D_ICON_FAR_PLANE = 100.f;
constexpr float UI_3D_ICON_DISTANCE = 10.f;

#define DEVICE_RESET_PRECACHE_FRAME_COUNT 10

// demonized: toggle bone optimization
//#define OPTIMIZE_CALCULATE_BONES

#include "../Include/xrRender/FactoryPtr.h"
#include "../Include/xrRender/RenderDeviceRender.h"
#include "imgui_base.h"

#ifdef INGAME_EDITOR
# include "../Include/editor/interfaces.hpp"
#endif // #ifdef INGAME_EDITOR

class engine_impl;

class IRenderDevice
{
public:
	virtual CStatsPhysics* _BCL StatPhysics() = 0;
	virtual void _BCL AddSeqFrame(pureFrame* f, bool mt) = 0;
	virtual void _BCL RemoveSeqFrame(pureFrame* f) = 0;
};

class ENGINE_API CRenderDeviceData
{
public:
	u32 dwWidth;
	u32 dwHeight;
	u32 clientWidth;
	u32 clientHeight;

	u32 dwPrecacheFrame;
	BOOL b_is_Ready;
	BOOL b_is_Active;
	BOOL b_hide_cursor;
public:

	// Engine flow-control
	u32 dwFrame;

	float fTimeDelta;
	float fTimeGlobal;
	u32 dwTimeDelta;
	u32 dwTimeGlobal;
	u32 dwTimeContinual;

	Fvector vCameraPosition;
	Fvector vCameraDirection;
	Fvector vCameraTop;
	Fvector vCameraRight;

	// demonized: Cam has fixed 83 fov, Hud is psHUD_FOV * 83.f
	Fmatrix mView;
	Fmatrix mViewHud;
	Fmatrix mViewCam;
	Fmatrix mProject;
	Fmatrix mProjectHud;
	Fmatrix mProjectCam;
	Fmatrix mFullTransform;
	Fmatrix mFullTransformHud;
	Fmatrix mFullTransformCam;
	Fmatrix mProjectCamAttached;
	Fmatrix mProject3DIcons;
	Fmatrix mFullTransformCamAttached;
	Fmatrix mFullTransform3DIcons;

	Fmatrix mView_prev;
	Fmatrix mViewHud_prev;
	Fmatrix mViewCam_prev;
	Fmatrix mProject_prev;
	Fmatrix mProjectHud_prev;
	Fmatrix mProjectCam_prev;
	Fmatrix mFullTransform_prev;
	Fmatrix mFullTransformHud_prev;
	Fmatrix mFullTransformCam_prev;

	Fvector4 wind_anim_prev;
	Fvector4 wind_anim_saved;

	// Copies of corresponding members. Used for synchronization.
	Fvector vCameraPosition_saved;

	Fmatrix mView_saved;
	Fmatrix mProject_saved;
	Fmatrix mFullTransform_saved;

	float fFOV;
	float fASPECT;
	float ViewportNear = 0.2f;
protected:

	u32 Timer_MM_Delta;
	CTimer_paused Timer;
	CTimer_paused TimerGlobal;

	//AVO: 
	CTimer frame_timer; //TODO: ïðîâåðèòü, íå äóáëèðóåòñÿ-ëè ñõîæèé òàéìåð (alpet)
	//-AVO

public:

	// Registrators
	CRegistrator<pureRender> seqRender;
	CRegistrator<pureAppActivate> seqAppActivate;
	CRegistrator<pureAppDeactivate> seqAppDeactivate;
	CRegistrator<pureAppStart> seqAppStart;
	CRegistrator<pureAppEnd> seqAppEnd;
	CRegistrator<pureFrame> seqFrame;
	CRegistrator<pureScreenResolutionChanged> seqResolutionChanged;

	HWND m_hWnd;
	// CStats* Statistic;
};

class ENGINE_API CRenderDeviceBase :
	public IRenderDevice,
	public CRenderDeviceData
{
public:
};

// refs
class ENGINE_API CRenderDevice : public CRenderDeviceBase
{
public:
	class ENGINE_API CSecondVPParams //--#SM+#-- +SecondVP+
	{
		bool isActive; // Oeaa aeoeaaoee ?aiaa?a ai aoi?ie au?ii?o
		u8 frameDelay;  // Ia eaeii eaa?a n iiiaioa i?ioeiai ?aiaa?a ai aoi?ie au?ii?o iu ia?i?i iiaue
						  //(ia ii?ao auou iaiuoa 2 - ea?aue aoi?ie eaa?, ?ai aieuoa oai aieaa ieceee FPS ai aoi?ii au?ii?oa)

	public:
		bool isCamReady; // Oeaa aioiaiinoe eaia?u (FOV, iiceoey, e o.i) e ?aiaa?o aoi?iai au?ii?oa

		IC bool IsSVPActive() { return isActive; }
		void SetSVPActive(bool bState);
		bool    IsSVPFrame();

		IC u8 GetSVPFrameDelay() { return frameDelay; }
		void  SetSVPFrameDelay(u8 iDelay)
		{
			frameDelay = iDelay;
			clamp<u8>(frameDelay, 2, u8(-1));
		}
	};	
	
private:
	// Main objects used for creating and rendering the 3D scene
	u32 m_dwWindowStyle;
	RECT m_rcWindowBounds;
	RECT m_rcWindowClient;

	//u32 Timer_MM_Delta;
	//CTimer_paused Timer;
	//CTimer_paused TimerGlobal;
	CTimer TimerMM;

	void _Create(LPCSTR shName);
	void _Destroy(BOOL bKeepTextures);
	void _SetupStates();
public:
	// HWND m_hWnd;
	LRESULT MsgProc(HWND, UINT, WPARAM, LPARAM);

	// u32 dwFrame;
	// u32 dwPrecacheFrame;
	u32 dwPrecacheTotal;

	// u32 dwWidth, dwHeight;
	float fWidth_2, fHeight_2;
	// BOOL b_is_Ready;
	// BOOL b_is_Active;
	void OnWM_Activate(WPARAM wParam, LPARAM lParam);
public:
	//ref_shader m_WireShader;
	//ref_shader m_SelectionShader;

	IRenderDeviceRender* m_pRender;

	BOOL m_bNearer;

	void SetNearer(BOOL enabled)
	{
		if (enabled && !m_bNearer)
		{
			m_bNearer = TRUE;
			mProject._43 -= EPS_L;
		}
		else if (!enabled && m_bNearer)
		{
			m_bNearer = FALSE;
			mProject._43 += EPS_L;
		}
		m_pRender->SetCacheXform(mView, mProject);
		//R_ASSERT(0);
		// TODO: re-implement set projection
		//RCache.set_xform_project (mProject);
	}

	void DumpResourcesMemoryUsage() { m_pRender->ResourcesDumpMemoryUsage(); }
public:
	// Registrators
	//CRegistrator <pureRender > seqRender;
	// CRegistrator <pureAppActivate > seqAppActivate;
	// CRegistrator <pureAppDeactivate > seqAppDeactivate;
	// CRegistrator <pureAppStart > seqAppStart;
	// CRegistrator <pureAppEnd > seqAppEnd;
	//CRegistrator <pureFrame > seqFrame;
	CRegistrator<pureFrame> seqFrameMT;
	CRegistrator<pureDeviceReset> seqDeviceReset;
	xr_vector<xr_delegate<void()>> seqParallel;

	// ForserX: Pre-Render sequence
	xr_vector<xr_delegate<void()>> seqParallelRender;
	xr_vector<xr_delegate<void()>> seqParallelBeforRender;

	// seqParallelBeforRender is filled from the particle worker thread
	// (CParticleGroup::SItem::OnFrame) and drained on the main thread in
	// CRenderDevice::on_idle(). Guard every access with this section.
	// Same pattern as CEventAPI::CS and CEffect_Rain::rainCS.
	xrCriticalSection seqParallelBeforRenderCS;

	xr_delegate<void()> ParticleWorkerCallback;
	xr_delegate<void()> ModelDefferClear;

	bool isRendering;

	// LuaGC
	int LuaGCCount;
	bool LuaGCDone;
    xr_delegate<int()> LuaGC;
    xr_delegate<void()> LuaGCDebug;

	// Dependent classes
	//CResourceManager* Resources;

	CStats* Statistic;

	// Engine flow-control
	//float fTimeDelta;
	//float fTimeGlobal;
	//u32 dwTimeDelta;
	//u32 dwTimeGlobal;
	//u32 dwTimeContinual;

	// Cameras & projection
	//Fvector vCameraPosition;
	//Fvector vCameraDirection;
	//Fvector vCameraTop;
	//Fvector vCameraRight;

	//Fmatrix mView;
	//Fmatrix mProject;
	//Fmatrix mFullTransform;

	Fmatrix mInvView;
	Fmatrix mInvProject;
	Fmatrix mInvProjectHud;
	Fmatrix mInvFullTransform;

	CSecondVPParams m_SecondViewport;	//--#SM+#-- +SecondVP+

	//float fFOV;
	//float fASPECT;

	CRenderDevice()
		:
		m_pRender(0)
#ifdef INGAME_EDITOR
        , m_editor_module(0),
        m_editor_initialize(0),
        m_editor_finalize(0),
        m_editor(0),
        m_engine(0)
#endif // #ifdef INGAME_EDITOR
#ifdef PROFILE_CRITICAL_SECTIONS
        
#endif // #ifdef PROFILE_CRITICAL_SECTIONS
	{
		m_hWnd = NULL;
		b_is_Active = FALSE;
		b_is_Ready = FALSE;
		b_hide_cursor = FALSE;
		Timer.Start();
		m_bNearer = FALSE;
		
		m_SecondViewport.SetSVPActive(false);
		m_SecondViewport.SetSVPFrameDelay(2);
		m_SecondViewport.isCamReady = false;
	};

	void Pause(BOOL bOn, BOOL bTimer, BOOL bSound, LPCSTR reason);
	bool Paused();

	// Scene control
	void PreCache(u32 amount, bool b_draw_loadscreen, bool b_wait_user_input);
	BOOL Begin();
	void Clear();
	void End();
	void FrameMove();

	void overdrawBegin();
	void overdrawEnd();

	//Console Screenshot
	void Screenshot();

	// Mode control
	void DumpFlags();
	IC CTimer_paused* GetTimerGlobal() { return &TimerGlobal; }
#ifdef XRCORE_PROFILER_AGG
	// -det_mode (test-only): the "real time" readers used by gameplay (server id recycling, script
	// time_global_async, a few timers) follow the fixed-step game clock instead of the wall clock.
	u32 TimerAsync() { return xr_det_mode() ? dwTimeGlobal : TimerGlobal.GetElapsed_ms(); }
	u32 TimerAsync_MMT() { return xr_det_mode() ? dwTimeGlobal : TimerMM.GetElapsed_ms() + Timer_MM_Delta; }
#else
	u32 TimerAsync() { return TimerGlobal.GetElapsed_ms(); }
	u32 TimerAsync_MMT() { return TimerMM.GetElapsed_ms() + Timer_MM_Delta; }
#endif

	// Creation & Destroying
	void ConnectToRender();
	void Create(void);
	void Run(void);
	void Destroy(void);
	void Reset(bool precache = true);

	bool ChangeOutputMonitor(HMONITOR hTargetMon);

	void Initialize(void);
	void ShutDown(void);

public:
	void time_factor(const float& time_factor)
	{
		Timer.time_factor(time_factor);
		TimerGlobal.time_factor(time_factor);
	}

	IC const float& time_factor() const
	{
		VERIFY(Timer.time_factor() == TimerGlobal.time_factor());
		return (Timer.time_factor());
	}

	Fvector& hud_to_world(Fvector& v, const Fmatrix& p)
	{
		mView.transform_tiny(v);
		p.transform_tiny(v);

		v.z -= ViewportNear;

		mInvProject.transform_tiny(v);
		mInvView.transform_tiny(v);

		return v;
	}

	Fvector& hud_to_world(Fvector& v)
	{
		return hud_to_world(v, mProjectHud);
	}

	Fvector& hud_to_world_dir(Fvector& v, const Fmatrix& p)
	{
		mView.transform_dir(v);
		p.transform_dir(v);

		mInvProject.transform_dir(v);
		mInvView.transform_dir(v);

		return v;
	}

	Fvector& hud_to_world_dir(Fvector& v)
	{
		return hud_to_world_dir(v, mProjectHud);
	}

	Fmatrix& hud_to_world(Fmatrix& m, const Fmatrix& p)
	{
		hud_to_world(m.c, p);
		hud_to_world_dir(m.i, p).normalize();
		hud_to_world_dir(m.j, p).normalize();
		hud_to_world_dir(m.k, p).normalize();
		return m;
	}

	Fmatrix& hud_to_world(Fmatrix& m)
	{
		return hud_to_world(m, mProjectHud);
	}

	Fmatrix& cam_to_world(Fmatrix& m)
	{
		return hud_to_world(m, mProjectCamAttached);
	}
	Fvector& world_to_hud(Fvector& v, const Fmatrix& p)
	{
		mInvView.transform_tiny(v);
		mInvProject.transform_tiny(v);

		v.z += ViewportNear;

		p.transform_tiny(v);
		mView.transform_tiny(v);
		return v;
	}

	Fvector& world_to_hud(Fvector& v)
	{
		return world_to_hud(v, mProjectHud);
	}

	Fvector& world_to_hud_dir(Fvector& v, const Fmatrix& p)
	{
		mInvView.transform_dir(v);
		mInvProject.transform_dir(v);

		p.transform_dir(v);
		mView.transform_dir(v);

		return v;
	}

	Fvector& world_to_hud_dir(Fvector& v)
	{
		return world_to_hud_dir(v, mProjectHud);
	}

	Fmatrix& world_to_hud(Fmatrix& m, const Fmatrix& p)
	{
		world_to_hud(m.c, p);
		world_to_hud_dir(m.i, p).normalize();
		world_to_hud_dir(m.j, p).normalize();
		world_to_hud_dir(m.k, p).normalize();
		return m;
	}

	Fmatrix& world_to_hud(Fmatrix& m)
	{
		return world_to_hud(m, mProjectHud);
	}

	// Multi-threading
	xr_task_group secondary_tasks;

	ICF void remove_from_seq_parallel(const xr_delegate<void()>& delegate)
	{
		xr_vector<xr_delegate<void()>>::iterator I = std::find(
			seqParallel.begin(),
			seqParallel.end(),
			delegate
		);
		if (I != seqParallel.end())
			seqParallel.erase(I);
	}

	//AVO: elapsed famed counter (by alpet)
	IC u32 frame_elapsed()
	{
		return frame_timer.GetElapsed_ms();
	}

	// demonized: Perceivable distance depending on FOV, so that objects will behave normal in binoculars
	IC float GetPerceivedDist(const Fvector& p, float* real_dist = nullptr)
	{
		float dist = vCameraPosition.distance_to(p);
		float fov_rad = deg2rad(fFOV);
		float perceived_dist = dist * tanf(fov_rad * 0.5f);
		if (real_dist) *real_dist = dist;
		return perceived_dist;
	}

	IC float CalcSSADynamic(const Fvector& C, float R)
	{
		Fvector4 v_res1, v_res2;
		mFullTransform.transform(v_res1, C);
		mFullTransform.transform(v_res2, Fvector(C).mad(vCameraRight, R));
		return v_res1.sub(v_res2).magnitude();
	}

public:
	void xr_stdcall on_idle();
	bool xr_stdcall on_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& result);

private:
	void message_loop();
	virtual void _BCL AddSeqFrame(pureFrame* f, bool mt);
	virtual void _BCL RemoveSeqFrame(pureFrame* f);
	virtual CStatsPhysics* _BCL StatPhysics() { return Statistic; }

private:
	xr_imgui::ide m_imgui;

public:
	xr_imgui::ide& imgui() { return m_imgui; }
	bool imgui_shown() const { return m_imgui.is_shown(); }
#ifdef INGAME_EDITOR
public:
    IC editor::ide* editor() const { return m_editor; }

private:
    void initialize_editor();
    void message_loop_editor();

private:
    typedef editor::initialize_function_ptr initialize_function_ptr;
    typedef editor::finalize_function_ptr finalize_function_ptr;

private:
    HMODULE m_editor_module;
    initialize_function_ptr m_editor_initialize;
    finalize_function_ptr m_editor_finalize;
    editor::ide* m_editor;
    engine_impl* m_engine;
#endif // #ifdef INGAME_EDITOR
};

extern ENGINE_API CRenderDevice Device;
extern ENGINE_API CRenderDevice* DevicePtr;

#ifndef _EDITOR
#define RDEVICE Device
#else
#define RDEVICE EDevice
#endif

#ifdef ECO_RENDER
extern ENGINE_API float refresh_rate;
#endif // ECO_RENDER

extern ENGINE_API bool g_bBenchmark;

// S2 stage 4a (device.cpp, sim_tick gated mode only): the view every frame. g_sim_view_gate is TRUE only while a
// gated frame runs its ticks and its presentation step; it is never TRUE with sim_tick 0, det_mode or rsConstantFPS.
// xrGame sets g_sim_view_present while its actor exists; the engine calls it once per gated frame, after the ticks
// and before the render, with the real frame time and the position between the last two ticks (0..1).
// g_sim_view_epoch changes on every resync of the tick clock: tick history from another epoch is not to be used.
extern ENGINE_API BOOL g_sim_view_gate;
extern ENGINE_API u32 g_sim_view_epoch;
typedef void (*sim_view_present_fn)(float dt, float alpha);
extern ENGINE_API sim_view_present_fn g_sim_view_present;
// S2 stage 4b (device.cpp, sim_tick gated mode only): objects drawn between their last two ticks. g_sim_view_tick counts
// the gated ticks (+1 before each one), so a capture can tell whether the previous capture was the tick directly before.
// g_sim_render_lerp is TRUE from the presentation step of a gated frame to the end of that frame's render, and only then;
// it is FALSE on every stock frame (sim_tick 0, det_mode, rsConstantFPS, paused, loading) and on a gated frame paused
// inside its ticks. g_sim_render_alpha is that frame's alpha (the value the presentation step computed), read only while
// g_sim_render_lerp is TRUE.
extern ENGINE_API u32 g_sim_view_tick;
extern ENGINE_API BOOL g_sim_render_lerp;
extern ENGINE_API float g_sim_render_alpha;
// S2 stage 4c (device.cpp, sim_tick gated mode only): the first-person HUD animation every rendered frame.
// g_sim_hud_ms is the simulated time the presentation step shows, in ms on the dwTimeGlobal time base: the newest
// tick's time plus alpha of one tick (never behind the newest tick, never past the next one, never going back within
// an epoch). Written by the presentation step only; read only while g_sim_render_lerp is TRUE, by the animated models
// xrGame marked with IKinematicsAnimated::LL_SetSimHudClock (UpdateTracks in SkeletonAnimated.cpp).
extern ENGINE_API double g_sim_hud_ms;
// S2 stage 4d-1 (device.cpp, sim_tick gated mode only): the real frame time of the presentation step (s_sv_dt), for
// render-side easing that would otherwise see Device.fTimeDelta = 0 on a render-only frame. Written by the presentation
// step only; read only while g_sim_render_lerp is TRUE. Device.fTimeDelta itself is never changed for the render.
extern ENGINE_API float g_sim_render_dt;
// S2 stage 4d-1: per-slice switch, console `sim_4d_mask` or start switch `-sim_4d_mask <m>` (read once at start).
// Read only while g_sim_render_lerp is TRUE, so with sim_tick 0 it is never read. Bit 8: see SIM4D_NUDGE.
enum
{
	SIM4D_BODY = 1, // S2: player body, shadow root, legs
	SIM4D_TRACERS = 2, // S3: tracers drawn between ticks
	SIM4D_XHAIR = 4, // S1: crosshair easing with the render dt
	SIM4D_NUDGE = 8, // EXPERIMENT, default off: the shadow pass's held-item nudge only on frames in which a tick ran
};
extern ENGINE_API int g_sim_4d_mask; // default 7 (SIM4D_NUDGE off until measured)
// TRUE while g_sim_render_lerp is TRUE and this frame ran no tick (render-only frame); written by the presentation step,
// cleared with g_sim_render_lerp. Read by the local player's shadow pass (Actor.cpp) under SIM4D_NUDGE.
extern ENGINE_API BOOL g_sim_render_notick;
// S2 stage 4d-1, TEST ONLY, every value below is 0 / FALSE / nullptr unless the engine was started with -det_mode:
// g_sim_synth_k    -det_synth_k <K> (1..8): the "synthetic gated" lever; under det_mode the gate runs a fixed pattern
//                  (one 33 ms tick, then K-1 render-only frames, alpha = j/K) instead of the wall clock
// g_sim_synth_tw   -det_synth_tw: tripwires (checksum of the simulation fields a slice's new code could reach, taken
//                  before and after it; a difference is logged as "* tw <slice> BAD <frame>")
// g_sim_4d_break   -sim_4d_break <mask>: must-fail breaks (SIM4D_BODY: the actor's draw matrix is written into its
//                  XFORM at render; SIM4D_TRACERS: the drawn tracer head is written into bullet_pos)
extern ENGINE_API int g_sim_synth_k;
extern ENGINE_API BOOL g_sim_synth_tw;
extern ENGINE_API int g_sim_4d_break;
// the witness of one gated frame under the lever (written to $logs$/synth_<det_tag>.txt by the device after the
// render); xrGame fills its slice columns during the render, only while g_sim_synth_k != 0. Reset by the device
// before every gated frame. Never read by the game.
struct SimSynthWitness
{
	u32 tw_bad; // tripwire BAD count in this frame
	bool s2; // the s2 columns are set
	Fvector s2_draw, s2_xform, s2_cam; // actor: drawn position, XFORM().c, camera position between ticks (4a)
	u32 s3_drawn; // tracers drawn in this frame; u32(-1) = CBulletManager::Render not called
	Fvector s3_head, s3_pos, s3_start; // first drawn tracer: drawn head, bullet_pos, tracer_start_position
	bool s3_stamp_ok; // the bullets were stepped in the newest tick
	bool s1; // the s1 columns are set (first crosshair eased in this frame)
	float s1_dt; // the dt the crosshair easing used
	Fvector s1_pos; // that crosshair's position after easing
	float s1_opacity; // and its opacity
	bool s4; // the s4 column is set: the local player has an active item
	Fvector s4_item; // its XFORM().c (simulation state) after this frame's render
};
extern ENGINE_API SimSynthWitness g_sim_synth_wit;
typedef void (*sim_synth_witness_fn)(); // fills the s2 columns of g_sim_synth_wit (set by xrGame while its actor exists)
extern ENGINE_API sim_synth_witness_fn g_sim_synth_witness_s2;
ENGINE_API void sim_4d_tw_bad(const char* slice); // tripwire report: count it in the witness, log one line
// FNV-1a over raw bytes (tripwire checksums)
static const u64 sim_4d_hash_seed = 14695981039346656037ull;
IC u64 sim_4d_hash(u64 h, const void* p, size_t n)
{
	const u8* b = static_cast<const u8*>(p);
	for (size_t i = 0; i < n; ++i)
	{
		h ^= b[i];
		h *= 1099511628211ull;
	}
	return h;
}

typedef xr_delegate<bool()> LOADING_EVENT;
extern ENGINE_API xr_list<LOADING_EVENT> g_loading_events;

class ENGINE_API CLoadScreenRenderer : public pureRender
{
public:
	CLoadScreenRenderer();
	void start(bool b_user_input);
	void stop();
	virtual void OnRender();

	bool b_registered;
	bool b_need_user_input;

	bool IsActive() const {
		return b_registered;
	}
};

extern ENGINE_API CLoadScreenRenderer load_screen_renderer;
