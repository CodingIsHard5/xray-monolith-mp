#include "stdafx.h"
#include "vr_hooks.h"
#include "igame_level.h"
#include "igame_persistent.h"

#include "x_ray.h"
#include "std_classes.h"
#include "customHUD.h"
#include "render.h"
#include "gamefont.h"
#include "xrLevel.h"
#include "CameraManager.h"
#include "xr_object.h"
#include "feel_sound.h"
#include "shader_bus.h"

#include "../xrCore/profiler.h"
#include <d3dx9.h>

//#include "securom_api.h"

ENGINE_API IGame_Level* g_pGameLevel = NULL;
extern BOOL g_bLoaded;

IGame_Level::IGame_Level()
{
	PROF_EVENT("IGame_Level::IGame_Level");
	m_pCameras = xr_new<CCameraManager>(true);
	g_pGameLevel = this;
	pLevel = NULL;
	bReady = false;
	pCurrentEntity = NULL;
	pCurrentViewEntity = NULL;
	Device.DumpResourcesMemoryUsage();
}

//#include "resourcemanager.h"

IGame_Level::~IGame_Level()
{
	Device.secondary_tasks.wait();

	if (Core.ParamsData.test(ECoreParams::nes_texture_storing))
		Device.m_pRender->ResourcesStoreNecessaryTextures();
	xr_delete(pLevel);

	// Render-level unload
	Render->level_Unload();
	xr_delete(m_pCameras);
	// Unregister
	Device.seqParallel.clear_not_free();
	Device.seqRender.Remove(this);
	Device.seqFrame.Remove(this);
	CCameraManager::ResetPP();
	///////////////////////////////////////////
	Sound->set_geometry_occ(NULL);
	Sound->set_handler(NULL);
	Device.DumpResourcesMemoryUsage();

	u32 m_base = 0, c_base = 0, m_lmaps = 0, c_lmaps = 0;
	if (Device.m_pRender)
		Device.m_pRender->ResourcesGetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);

	Msg("* [ D3D ]: textures[%d K]", (m_base + m_lmaps) / 1024);
}

void IGame_Level::net_Stop()
{
    for (int i = 0; i < 6; i++)
    {
        Objects.Update(false);
        Objects.ProcessDestroyQueue();
    }
		
	// Destroy all objects
	Objects.Unload();
	IR_Release();

	bReady = false;
}

//-------------------------------------------------------------------------------------------
//extern CStatTimer tscreate;
void __stdcall _sound_event(ref_sound_data_ptr S, float range)
{
	if (g_pGameLevel && S && S->feedback) g_pGameLevel->SoundEvent_Register(S, range);
}

static void __stdcall build_callback(Fvector* V, int Vcnt, CDB::TRI* T, int Tcnt, void* params)
{
	g_pGameLevel->Load_GameSpecific_CFORM(T, Tcnt);
}

xrCriticalSection lloadcs;
bool IGame_Level::Load(u32 dwNum)
{
	PROF_EVENT("IGame_Level::Load");
	xrCriticalSectionGuard guard(&lloadcs);
	if (bReady) return TRUE;
	extern xr_task_group prefetch_task;
	prefetch_task.wait();
	//SECUROM_MARKER_PERFORMANCE_ON(10)

	// Initialize level data
	pApp->Level_Set(dwNum);
	string_path temp;
	if (!FS.exist(temp, "$level$", "level.ltx"))
		Debug.fatal(DEBUG_INFO, "Can't find level configuration file '%s'.", temp);
	pLevel = xr_new<CInifile>(temp);

	// Open
	// g_pGamePersistent->LoadTitle ("st_opening_stream");
	g_pGamePersistent->LoadTitle();
	IReader* LL_Stream = FS.r_open("$level$", "level");
	IReader& fs = *LL_Stream;

	// Header
	hdrLEVEL H;
	fs.r_chunk_safe(fsL_HEADER, &H, sizeof(H));
	R_ASSERT2(XRCL_PRODUCTION_VERSION == H.XRLC_version, "Incompatible level version.");

	// CForms
	// g_pGamePersistent->LoadTitle ("st_loading_cform");
	g_pGamePersistent->LoadTitle();
	ObjectSpace.Load( [](Fvector* V, int Vcnt, CDB::TRI* T, int Tcnt, void* params){g_pGameLevel->Load_GameSpecific_CFORM(T, Tcnt);});
	//Sound->set_geometry_occ ( &Static );
	Sound->set_geometry_occ(ObjectSpace.GetStaticModel());
	Sound->set_handler(_sound_event);

	pApp->LoadSwitch();


	// HUD + Environment
	if (!g_hud)
		g_hud = (CCustomHUD*)NEW_INSTANCE(CLSID_HUDMANAGER);

	// Render-level Load
	Render->level_Load(LL_Stream);
	// tscreate.FrameEnd ();
	// Msg ("* S-CREATE: %f ms, %d times",tscreate.result,tscreate.count);

	// Objects
	g_pGamePersistent->Environment().mods_load();
	R_ASSERT(Load_GameSpecific_Before());
	Objects.Load();
	//. ANDY R_ASSERT (Load_GameSpecific_After ());

	// Done
	FS.r_close(LL_Stream);
	bReady = true;
	if (!g_dedicated_server) IR_Capture();
#ifndef DEDICATED_SERVER
	Device.seqRender.Add(this);
#endif

	Device.seqFrame.Add(this);

	ShaderBus::dump();

	//SECUROM_MARKER_PERFORMANCE_OFF(10)

	return true;
}

#ifndef _EDITOR
#include "../xrCPU_Pipe/ttapi.h"
#endif

int psNET_DedicatedSleep = 5;

void IGame_Level::OnRender()
{
#ifndef DEDICATED_SERVER
	// if (_abs(Device.fTimeDelta)<EPS_S) return;

#ifdef _GPA_ENABLED
    TAL_ID rtID = TAL_MakeID( 1 , Core.dwFrame , 0);
    TAL_CreateID( rtID );
    TAL_BeginNamedVirtualTaskWithID( "GameRenderFrame" , rtID );
    TAL_Parami( "Frame#" , Device.dwFrame );
    TAL_EndVirtualTask();
#endif // _GPA_ENABLED

	// Level render, only when no client output required
	if (!g_dedicated_server)
	{
		extern ENGINE_API int ps_r_stereo_bench;
		extern ENGINE_API float ps_r_stereo_ipd;
		extern ENGINE_API int g_stereo_bench_eye;
		// GammaVR Phase B: with -vr_openxr and a running session, the eyes come from the OpenXR runtime
		vr_eye_view vr_views[2];
		vr_frame_info vr_info;
		vr_info.head.valid = false;
		vr_info.frame = 0;
		const bool vr_frame = g_vr_begin_frame && g_pGameLevel && !Device.m_SecondViewport.IsSVPFrame() && g_vr_begin_frame(vr_views, &vr_info);
		if ((vr_frame || ps_r_stereo_bench >= 1) && g_pGameLevel && !Device.m_SecondViewport.IsSVPFrame())
		{
			// VR spike, naive stereo: the full scene pipeline runs once per eye.
			// Each eye is offset half the IPD along camera right and gets an off-centre projection.
			// The second eye overwrites the first in the back buffer; this measures cost, not picture.
			const Fvector pos0 = Device.vCameraPosition;
			const Fvector right0 = Device.vCameraRight, top0 = Device.vCameraTop, dir0 = Device.vCameraDirection;
			const Fmatrix view0 = Device.mView, proj0 = Device.mProject, projHud0 = Device.mProjectHud;
			const Fmatrix invView0 = Device.mInvView, viewPrev0 = Device.mView_prev, projPrev0 = Device.mProject_prev, fullPrev0 = Device.mFullTransform_prev;
			static Fmatrix eye_view_saved[2], eye_proj_saved[2]; // each eye's own previous-frame camera (motion vectors)
			static u32 eye_saved_frame[2] = {0, 0};
			const Fmatrix viewHud0 = Device.mViewHud;
			// GammaVR milestone 3 (3a): the head pose drives the camera. The tracking space is mapped to the world so
			// that the head pose of the first VR frame lands on this frame's game camera:
			// track_to_world = game camera * inverse(head at the first VR frame); head camera = track_to_world * head.
			Fvector head_pos = pos0, head_right = right0, head_top = top0, head_dir = dir0;
			if (vr_frame && vr_info.head.valid)
			{
				static vr_pose s_head0;
				static bool s_have_head0 = false;
				if (!s_have_head0)
				{
					s_head0 = vr_info.head;
					s_have_head0 = true;
					Msg("* [vr3] head start pose (tracking space, engine axes) pos %.3f %.3f %.3f quat %.4f %.4f %.4f %.4f",
						s_head0.pos.x, s_head0.pos.y, s_head0.pos.z, s_head0.qx, s_head0.qy, s_head0.qz, s_head0.qw);
				}
				Fmatrix game_cam, head0, head0_inv, head_now, head_world;
				game_cam.set(right0, top0, dir0, pos0);
				vr_pose_matrix(s_head0, head0);
				head0_inv.invert(head0);
				g_vr_track.track_to_world.mul_43(game_cam, head0_inv); // head0_inv first, then the game camera
				vr_pose_matrix(vr_info.head, head_now);
				head_world.mul_43(g_vr_track.track_to_world, head_now);
				head_right = head_world.i;
				head_top = head_world.j;
				head_dir = head_world.k;
				head_pos = head_world.c;
				g_vr_track.frame = vr_info.frame;
				g_vr_track.active = true;

				if (vr_info.frame % 100 == 0)
				{
					const vr_pose& H = vr_info.head;
					Msg("* [vr3] frame %u head_rt pos %.4f %.4f %.4f quat %.5f %.5f %.5f %.5f",
						vr_info.frame, H.pos.x, H.pos.y, H.pos.z, H.qx, H.qy, H.qz, H.qw);
					// camera relative to the game camera, measured from the vectors actually given to the renderer
					Fvector d, ri, rj, rk, rp;
					d.sub(head_pos, pos0);
					rp.set(d.dotproduct(right0), d.dotproduct(top0), d.dotproduct(dir0));
					ri.set(head_right.dotproduct(right0), head_right.dotproduct(top0), head_right.dotproduct(dir0));
					rj.set(head_top.dotproduct(right0), head_top.dotproduct(top0), head_top.dotproduct(dir0));
					rk.set(head_dir.dotproduct(right0), head_dir.dotproduct(top0), head_dir.dotproduct(dir0));
					float qx, qy, qz, qw;
					vr_matrix_quat(ri, rj, rk, qx, qy, qz, qw);
					Msg("* [vr3] frame %u cam_rel pos %.4f %.4f %.4f quat %.5f %.5f %.5f %.5f",
						vr_info.frame, rp.x, rp.y, rp.z, qx, qy, qz, qw);
					vr_grip_info G;
					if (g_vr_get_grip && g_vr_get_grip(&G))
						Msg("* [vr3] frame %u grip_rt hand %c valid %d pos %.4f %.4f %.4f quat %.5f %.5f %.5f %.5f",
							vr_info.frame, G.hand, G.runtime.valid ? 1 : 0, G.runtime.pos.x, G.runtime.pos.y, G.runtime.pos.z,
							G.runtime.qx, G.runtime.qy, G.runtime.qz, G.runtime.qw);
					else
						Msg("* [vr3] frame %u grip_rt hand ? valid 0 pos 0 0 0 quat 0 0 0 1", vr_info.frame);
				}
			}
			for (int eye = 0; eye < 2; ++eye)
			{
				const int mono_eye = vr_frame ? g_vr_mono_eye : -1;
				if (mono_eye >= 0 && eye != mono_eye)
					continue;
				g_stereo_bench_eye = mono_eye >= 0 ? -1 : eye; // a mono twin is an ordinary single pass
				const float side = eye ? 1.f : -1.f;
				Device.mProject = proj0;
				Device.mProjectHud = projHud0;
				if (vr_frame)
				{
					// camera = head pose in the world (milestone 3; equals the game camera if the runtime gave no head pose)
					Device.vCameraRight = head_right;
					Device.vCameraTop = head_top;
					Device.vCameraDirection = head_dir;
					// eye position from the runtime (relative to the head), projection from the runtime's field of view
					const vr_eye_view& V = vr_views[eye];
					Device.vCameraPosition.mad(head_pos, Device.vCameraRight, V.offset.x);
					Device.vCameraPosition.mad(Device.vCameraTop, V.offset.y);
					Device.vCameraPosition.mad(Device.vCameraDirection, V.offset.z);
					const float w = V.tan_r - V.tan_l, h = V.tan_u - V.tan_d;
					Device.mProject._11 = 2.f / w;
					Device.mProject._22 = 2.f / h;
					Device.mProject._31 = -(V.tan_r + V.tan_l) / w;
					Device.mProject._32 = -(V.tan_u + V.tan_d) / h;
					// milestone 3: the HUD (held weapon) is placed in the world, so it gets the eye's field of view too
					Device.mProjectHud._11 = Device.mProject._11;
					Device.mProjectHud._22 = Device.mProject._22;
					Device.mProjectHud._31 = Device.mProject._31;
					Device.mProjectHud._32 = Device.mProject._32;
				}
				else
				{
					Device.vCameraPosition.mad(pos0, Device.vCameraRight, side * ps_r_stereo_ipd * 0.5f);
					Device.mProject._31 = -side * 0.08f;
					Device.mProjectHud._31 = -side * 0.08f;
				}
				Device.mView.build_camera_dir(Device.vCameraPosition, Device.vCameraDirection, Device.vCameraTop);
				Device.mInvView.invert(Device.mView);
				{
					const bool have_prev = eye_saved_frame[eye] && eye_saved_frame[eye] + 1 == Device.dwFrame;
					Device.mView_prev = have_prev ? eye_view_saved[eye] : Device.mView;
					Device.mProject_prev = have_prev ? eye_proj_saved[eye] : Device.mProject;
					Device.mFullTransform_prev.mul(Device.mProject_prev, Device.mView_prev);
					Device.m_pRender->SetCacheXform_prev(Device.mView_prev, Device.mProject_prev);
					eye_view_saved[eye] = Device.mView; eye_proj_saved[eye] = Device.mProject; eye_saved_frame[eye] = Device.dwFrame;
				}
				if (vr_frame)
				{
					// milestone 3: the HUD pass sets Device.mView from mViewHud (CHudInitializer::SetHudMode), which
					// otherwise stays the mono camera; the gun must be seen from this eye
					Device.mViewHud = Device.mView;
				}
				Device.mFullTransform.mul(Device.mProject, Device.mView);
				Device.mFullTransformHud.mul(Device.mProjectHud, Device.mView);
				Device.mFullTransformCamAttached.mul(Device.mProjectCamAttached, Device.mView);
				Device.mFullTransform3DIcons.mul(Device.mProject3DIcons, Device.mView);
				D3DXMatrixInverse((D3DXMATRIX*)&Device.mInvFullTransform, 0, (D3DXMATRIX*)&Device.mFullTransform);
				Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
				{
					PROF_EVENT("stereo eye: Calculate");
					Render->Calculate();
				}
				{
					PROF_EVENT("stereo eye: Render");
					Render->Render();
				}
				if (vr_frame && g_vr_eye_done)
				{
					if (mono_eye >= 0)
					{
						g_vr_eye_done(0);
						g_vr_eye_done(1);
					}
					else
						g_vr_eye_done(eye);
				}
			}
			g_stereo_bench_eye = -1;
			g_vr_track.active = false;
			// restore the centre camera for everything drawn after the scene (UI, debug)
			{
				Device.vCameraPosition = pos0;
				Device.vCameraRight = right0;
				Device.vCameraTop = top0;
				Device.vCameraDirection = dir0;
				Device.mInvView = invView0;
				Device.mViewHud = viewHud0;
				Device.mView = view0;
				Device.mInvView = invView0;
				Device.mView_prev = viewPrev0; Device.mProject_prev = projPrev0; Device.mFullTransform_prev = fullPrev0;
				Device.m_pRender->SetCacheXform_prev(Device.mView_prev, Device.mProject_prev);
				Device.mProject = proj0;
				Device.mProjectHud = projHud0;
				Device.mFullTransform.mul(Device.mProject, Device.mView);
				Device.mFullTransformHud.mul(Device.mProjectHud, Device.mView);
				Device.mFullTransformCamAttached.mul(Device.mProjectCamAttached, Device.mView);
				Device.mFullTransform3DIcons.mul(Device.mProject3DIcons, Device.mView);
				D3DXMatrixInverse((D3DXMATRIX*)&Device.mInvFullTransform, 0, (D3DXMATRIX*)&Device.mFullTransform);
				Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
			}
		}
		else
		{
			{
				PROF_EVENT("IGame_Level::OnRender: Calculate");
				Render->Calculate();
			}
			{
				PROF_EVENT("IGame_Level::OnRender: Render");
				Render->Render();
			}
		}
	}
	else
	{
		Sleep(psNET_DedicatedSleep);
	}

#ifdef _GPA_ENABLED
    TAL_RetireID( rtID );
#endif // _GPA_ENABLED

	// Font
	// pApp->pFontSystem->SetSizeI(0.023f);
	// pApp->pFontSystem->OnRender ();
#endif
}

void IGame_Level::OnFrame()
{
	PROF_EVENT("IGame_Level::OnFrame");
	// Log ("- level:on-frame: ",u32(Device.dwFrame));
	// if (_abs(Device.fTimeDelta)<EPS_S) return;

	// Update all objects
	VERIFY(bReady);
	Objects.Update(false);
	g_hud->OnFrame();

	// Ambience
	if (Sounds_Random.size() && (Device.dwTimeGlobal > Sounds_Random_dwNextTime))
	{
		Sounds_Random_dwNextTime = Device.dwTimeGlobal + ::Random.randI(10000, 20000);
		Fvector pos;
		pos.random_dir().normalize().mul(::Random.randF(30, 100)).add(Device.vCameraPosition);
		int id = ::Random.randI(Sounds_Random.size());
		if (Sounds_Random_Enabled)
		{
			Sounds_Random[id].play_at_pos(0, pos, 0);
			Sounds_Random[id].set_volume(1.f);
			Sounds_Random[id].set_range(10, 200);
		}
	}
}

// ==================================================================================================

void CServerInfo::AddItem(LPCSTR name_, LPCSTR value_, u32 color_)
{
	shared_str s_name(name_);
	AddItem(s_name, value_, color_);
}

void CServerInfo::AddItem(shared_str& name_, LPCSTR value_, u32 color_)
{
	SItem_ServerInfo it;
	// shared_str s_name = CStringTable().translate( name_ );

	// xr_strcpy( it.name, s_name.c_str() );
	xr_strcpy(it.name, name_.c_str());
	xr_strcat(it.name, " = ");
	xr_strcat(it.name, value_);
	it.color = color_;

	if (data.size() < max_item)
	{
		data.push_back(it);
	}
}

void IGame_Level::SetEntity(CObject* O)
{
	if (pCurrentEntity)
		pCurrentEntity->On_LostEntity();

	if (O)
		O->On_SetEntity();

	pCurrentEntity = pCurrentViewEntity = O;
}

void IGame_Level::SetViewEntity(CObject* O)
{
	if (pCurrentViewEntity)
		pCurrentViewEntity->On_LostEntity();

	if (O)
		O->On_SetEntity();

	pCurrentViewEntity = O;
}

void IGame_Level::SoundEvent_Register(ref_sound_data_ptr S, float range)
{
	PROF_EVENT("IGame_Level::SoundEvent_Register");
	if (!g_bLoaded) return;
	if (!S) return;
	if (S->g_object && S->g_object->getDestroy())
	{
		S->g_object = 0;
		return;
	}
	if (0 == S->feedback) return;

	clamp(range, 0.1f, 500.f);

	const CSound_params* p = S->feedback->get_params();
	Fvector snd_position = p->position;
	if (S->feedback->is_2D())
	{
		snd_position.add(Sound->listener_position());
	}

	VERIFY(p && _valid(range));
	range = _min(range, p->max_ai_distance);
	VERIFY(_valid(snd_position));
	VERIFY(_valid(p->max_ai_distance));
	VERIFY(_valid(p->volume));

	// Query objects
	Fvector bb_size = {range, range, range};
	{
		PROF_EVENT("Sound: hearing spatial query");
		g_SpatialSpace->q_box(snd_ER, 0, STYPE_REACTTOSOUND, snd_position, bb_size);
	}

	// Iterate
	auto it = snd_ER.begin();
	auto end = snd_ER.end();
	for (; it != end; it++)
	{
		Feel::Sound* L = (*it)->dcast_FeelSound();
		if (0 == L) continue;
		CObject* CO = (*it)->dcast_CObject();
		VERIFY(CO);
		if (CO->getDestroy()) continue;

		// Energy and signal
		VERIFY(_valid((*it)->spatial.sphere.P));
		float dist = snd_position.distance_to((*it)->spatial.sphere.P);
		if (dist > p->max_ai_distance) continue;
		VERIFY(_valid(dist));
		VERIFY2(!fis_zero(p->max_ai_distance), S->handle->file_name());
		float Power = (1.f - dist / p->max_ai_distance) * p->volume;
		VERIFY(_valid(Power));
		if (Power > EPS_S)
		{
			float occ = Sound->get_occlusion_to((*it)->spatial.sphere.P, snd_position);
			VERIFY(_valid(occ));
			Power *= occ;
			if (Power > EPS_S)
			{
				_esound_delegate D = {L, S, Power};
				snd_Events.push_back(D);
			}
		}
	}
	snd_ER.clear_not_free();
}

void IGame_Level::SoundEvent_Dispatch()
{
	PROF_EVENT("IGame_Level::SoundEvent_Dispatch");
	while (!snd_Events.empty())
	{
		_esound_delegate& D = snd_Events.back();
		VERIFY(D.dest && D.source);
		if (D.source->feedback)
		{
			D.dest->feel_sound_new(
				D.source->g_object,
				D.source->g_type,
				D.source->g_userdata,

				D.source->feedback->is_2D() ? Device.vCameraPosition : D.source->feedback->get_params()->position,
				D.power
			);
		}
		snd_Events.pop_back();
	}
}

// Lain: added
void IGame_Level::SoundEvent_OnDestDestroy(Feel::Sound* obj)
{
	PROF_EVENT("IGame_Level::SoundEvent_OnDestDestroy");
	struct rem_pred
	{
		rem_pred(Feel::Sound* obj) : m_obj(obj)
		{
		}

		bool operator ()(const _esound_delegate& d)
		{
			return d.dest == m_obj;
		}

	private:
		Feel::Sound* m_obj;
	};

	snd_Events.erase(std::remove_if(snd_Events.begin(), snd_Events.end(), rem_pred(obj)),
	                 snd_Events.end());
}
