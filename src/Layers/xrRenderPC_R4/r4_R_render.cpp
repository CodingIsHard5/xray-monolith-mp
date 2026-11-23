#include "stdafx.h"
#include "../../xrEngine/vr_hooks.h"
extern ENGINE_API int g_stereo_bench_eye;
#include "../../xrEngine/igame_persistent.h"
#include "../xrRender/FBasicVisual.h"
#include "../../xrEngine/customhud.h"
#include "../../xrEngine/xr_object.h"
#include "../xrRender/SkeletonCustom.h"
#include "../../xrEngine/fmesh.h"
#include "../../xrParticles/ParticlesAsyncManager.h"

#include "../xrRender/QueryHelper.h"

#include	"../xrRender/dxRenderDeviceRender.h"
namespace
{
bool ui_3d_icon_atlas_active = false;
Fmatrix ui_3d_icon_project_saved;
Fmatrix ui_3d_icon_full_transform_saved;
}


void CRender::render_menu()
{
	PIX_EVENT(render_menu);
	//	Globals
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	// Main Render
	{
		Target->u_setrt(Target->rt_Generic_0, 0, 0, HW.pBaseZB); // LDR RT
		g_pGamePersistent->OnRenderPPUI_main(); // PP-UI
	}

	// Distort
	{
		FLOAT ColorRGBA[4] = {127.0f / 255.0f, 127.0f / 255.0f, 0.0f, 127.0f / 255.0f};
		Target->u_setrt(Target->rt_Generic_1, 0, 0, HW.pBaseZB); // Now RT is a distortion mask
		HW.pContext->ClearRenderTargetView(Target->rt_Generic_1->pRT, ColorRGBA);
		g_pGamePersistent->OnRenderPPUI_PP(); // PP-UI
	}

	// Actual Display
	Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
	RCache.set_Shader(Target->s_menu);
	RCache.set_Geometry(Target->g_menu);

	Fvector2 p0, p1;
	u32 Offset;
	auto C = color_rgba(255, 255, 255, 255);
	float _w = float(Device.dwWidth);
	float _h = float(Device.dwHeight);
	float d_Z = EPS_S;
	float d_W = 1.f;
	p0.set(.5f / _w, .5f / _h);
	p1.set((_w + .5f) / _w, (_h + .5f) / _h);

	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, Target->g_menu->vb_stride, Offset);
	pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y);
	pv++;
	pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y);
	pv++;
	pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y);
	pv++;
	pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y);
	pv++;
	RCache.Vertex.Unlock(4, Target->g_menu->vb_stride);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

extern u32 g_r;

void CRender::Render()
{
	PIX_EVENT_C(CRender_Render, dx10_marker_frame);
	dx10_annotate_frame();

	if (Target) Target->vr_history_select(g_stereo_bench_eye);
	rmNormal();

	bool _menu_pp = g_pGamePersistent ? g_pGamePersistent->OnRenderPPUI_query() : false;
	if (_menu_pp)
	{
		render_menu();
		return;
	};

	IMainMenu* pMainMenu = g_pGamePersistent ? g_pGamePersistent->m_pMainMenu : 0;
	bool bMenu = pMainMenu ? pMainMenu->CanSkipSceneRendering() : false;

	if (!(g_pGameLevel && g_hud)
		|| bMenu)
	{
		Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
		return;
	}

	if (m_bFirstFrameAfterReset)
	{
		for (light* L : v_all_lights)//critical!!!
			L->m_moving_frames = 0;

		m_bFirstFrameAfterReset = false;
		return;
	}

	if ((Device.dwFrame % (u32)ps_r__tex_evict_interval) == 0)
		dxRenderDeviceRender::Instance().Resources->EvictStalledTextures();

	//.	VERIFY					(g_pGameLevel && g_pGameLevel->pHUD);

	// Configure
	RImplementation.o.distortion = FALSE; // disable distorion
	Fcolor sun_color = ((light*)Lights.sun_adapted._get())->color;
	BOOL bSUN = ps_r2_ls_flags.test(R2FLAG_SUN) && (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b)>EPS) && !Core.ParamsData.test(ECoreParams::r4_dev);
	if (o.sunstatic) bSUN = FALSE;
	// Msg						("sstatic: %s, sun: %s",o.sunstatic?;"true":"false", bSUN?"true":"false");

	// HOM
	ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
    HOM.MT_RENDER();

	Target->phase_scene_prepare();


	//******* Main calc - DEFERRER RENDERER
	phase = PHASE_NORMAL;
	
	/*if (RImplementation.o.ssfx_core) // SSS23: DEPRECATED
	{
		// HUD Masking rendering
		FLOAT ColorRGBA[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_hud->pRT, ColorRGBA);

		Target->u_setrt(Target->rt_ssfx_hud, NULL, NULL, HW.pBaseZB);
		r_dsgraph_render_hud(true);

		// Reset Depth
		HW.pContext->ClearDepthStencilView(HW.pBaseZB, D3D_CLEAR_DEPTH, 1.0f, 0);
	}*/

    GMBase.traverse(RImplementation.pLastSector, ViewBase, Device.vCameraPosition, Device.mFullTransform);
    GMBase.r_dsgraph_capture_static();
    GMBase.r_dsgraph_capture_dynamic();

    if (RImplementation.o.ssfx_motionvectors)
    {
        Target->u_setrt(Device.dwWidth, Device.dwHeight, 0, 0, Target->rt_ssfx_motion_vectors->pRT, 0);

        FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        HW.pContext->ClearRenderTargetView(Target->rt_ssfx_motion_vectors->pRT, ColorRGBA);

        RCache.set_Stencil(FALSE);
        g_pGamePersistent->Environment().RenderSky(true);

        RCache.Index.Flush();
        RCache.Vertex.Flush();

        RCache.set_xform_world(Fidentity);
    }

	if (ps_r2_ls_flags.test(R2FLAG_TERRAIN_PREPASS))
	{
		Target->u_setrt(Device.dwWidth, Device.dwHeight, NULL, NULL, NULL, !RImplementation.o.dx10_msaa ? HW.pBaseZB : Target->rt_MSAADepth->pZRT);
	}

	//******* Main render :: PART-0	-- first
	{
		PIX_EVENT_C(DEFER_PART0_SPLIT, dx10_marker_gbuffer);
		// level, SPLIT
		Target->phase_scene_begin();
		GMBase.r_dsgraph_render_static(0);
		GMBase.r_dsgraph_render_dynamic(0);
		Target->disable_aniso();
	}

	//  Redotix99: for 3D Shader Based Scopes 	
	if (scope_3D_fake_enabled)
	{
		ID3D11Resource* zbuffer_res;
		HW.pBaseZB->GetResource(&zbuffer_res);
		HW.pContext->CopyResource(RImplementation.Target->rt_tempzb->pSurface, zbuffer_res);
	}

	if (RImplementation.o.dx10_msaa)
		RCache.set_ZB(RImplementation.Target->rt_MSAADepth->pZRT);

	{
		PIX_EVENT(DEFER_TEST_LIGHT_VIS);
		//******* Occlusion testing of volume-limited light-sources
		Target->phase_occq();
		LP_normal.clear();
		LP_pending.clear();
		GMBase.r_dsgraph_capture_lights();
	}

	//******* Main render :: PART-1 (second)
	{
		PIX_EVENT_C(DEFER_PART1_SPLIT, dx10_marker_gbuffer);
		// level
		Target->phase_scene_begin();
		GMBase.r_dsgraph_capture_hud();
		GMBase.r_dsgraph_render_hud();
		GMBase.r_dsgraph_render_lods(true,true);
		if (Details) Details->Render();
		Target->phase_scene_end();
	}

	// Wall marks
	if (Wallmarks)
	{
		PIX_EVENT(DEFER_WALLMARKS);
		Target->phase_wallmarks();

		Wallmarks->Render(); // wallmarks has priority as normal geometry
	}

	// full screen pass to mark msaa-edge pixels in highest stencil bit
	if (RImplementation.o.dx10_msaa)
	{
		PIX_EVENT(MARK_MSAA_EDGES);
		Target->mark_msaa_edges();
	}

	//	TODO: DX10: Implement DX10 rain.
	if (ps_r2_ls_flags.test(R3FLAG_DYN_WET_SURF))
	{
		PIX_EVENT(DEFER_RAIN);
		render_rain();
	}

	{
		// Save previus and current matrices
		{
			static Fmatrix mm_saved_viewproj_eye[2];
			Fmatrix& mm_saved_viewproj = mm_saved_viewproj_eye[Target->vr_hist_count && g_stereo_bench_eye == 1 ? 1 : 0]; // GammaVR: own history per eye

			if (!Device.m_SecondViewport.IsSVPFrame())
			{
				Target->Matrix_previous.mul(mm_saved_viewproj, Device.mInvView);
				Target->Matrix_current.set(Device.mProject);
				mm_saved_viewproj.set(Device.mFullTransform);
			}
		}

		if (RImplementation.o.ssfx_sss && !Device.m_SecondViewport.IsSVPFrame())
		{
			static bool sss_rendered, sss_extended_rendered;

			// SSS Shadows
			if (ps_ssfx_sss_quality.z > 0 && !vr_plain())
			{
				Target->phase_ssfx_sss();
				sss_rendered = true;
			}
			else
			{
				if (sss_rendered) // Clear buffer
				{
					sss_rendered = false;
					FLOAT ColorRGBA[4] = { 1,1,1,1 };
					HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss->pRT, ColorRGBA);
				}
			}

			if (ps_ssfx_sss_quality.w > 0 && !vr_plain())
			{
				// Extra lights
				Target->phase_ssfx_sss_ext(RImplementation.LP_normal);
				sss_extended_rendered = true;
			}
			else
			{
				if (sss_extended_rendered) // Clear buffer
				{
					sss_extended_rendered = false;
					FLOAT ColorRGBA[4] = { 1,1,1,1 };
					HW.pContext->ClearRenderTargetView(Target->rt_ssfx_sss_tmp->pRT, ColorRGBA);
				}
			}
		}
	}

	// Directional light - fucking sun
	if (bSUN) //bSUN && Device.dwFrame & 1 --Delayed sun update. Worth to check it in future
	{
		PIX_EVENT_C(DEFER_SUN, dx10_marker_lights);
		RImplementation.stats.l_visible ++;
		render_sun_cascades();
		Target->increment_light_marker();
		Target->accum_direct_blend();
	}

	phase = PHASE_NORMAL;

	{
		PIX_EVENT_C(DEFER_SELF_ILLUM, dx10_marker_lights);
		Target->phase_accumulator();
		// Render emissive geometry, stencil - write 0x0 at pixel pos
		RCache.set_xform_project(Device.mProject);
		RCache.set_xform_view(Device.mView);
		// Stencil - write 0x1 at pixel pos - 
		if (!RImplementation.o.dx10_msaa)
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		else
			RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
			                   D3DSTENCILOP_KEEP);
		//RCache.set_Stencil				(TRUE,D3DCMP_ALWAYS,0x00,0xff,0xff,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_ColorWriteEnable();
		GMBase.r_dsgraph_render_emissive(RImplementation.o.ssfx_bloom ? false : true);
	}

	if (RImplementation.o.ssfx_bloom)
	{
		// Render Emissive on `rt_ssfx_bloom_emissive`
		FLOAT ColorRGBA[4] = { 0,0,0,0 };
		HW.pContext->ClearRenderTargetView(Target->rt_ssfx_bloom_emissive->pRT, ColorRGBA);
		Target->u_setrt(Target->rt_ssfx_bloom_emissive, NULL, NULL, !RImplementation.o.dx10_msaa ? HW.pBaseZB : Target->rt_MSAADepth->pZRT);
		// Keep regular HUD geometry out of the emissive target. It is already
		// present in the scene texture sampled by the bloom build pass; drawing it
		// here again treats every strict-sorted HUD surface as an emissive source.
		GMBase.r_dsgraph_render_emissive(true, false);
	}

	// Lighting, non dependant on OCCQ
	{
		PIX_EVENT_C(DEFER_LIGHT_NO_OCCQ, dx10_marker_lights);
		Target->phase_accumulator();
		{
			// GammaVR: dynamic light count per render pass, every 100th frame (series S13 pre-registration needs it)
			extern ENGINE_API int g_stereo_bench_eye;
			if (Device.dwFrame % 100 == 0)
				Msg("* [lights] frame %u eye %d: normal point %u spot %u shadowed %u, pending point %u spot %u shadowed %u",
					Device.dwFrame, g_stereo_bench_eye, (u32)LP_normal.v_point.size(), (u32)LP_normal.v_spot.size(), (u32)LP_normal.v_shadowed.size(),
					(u32)LP_pending.v_point.size(), (u32)LP_pending.v_spot.size(), (u32)LP_pending.v_shadowed.size());
		}
		render_lights(LP_normal);
	}

	// Lighting, dependant on OCCQ
	{
		PIX_EVENT_C(DEFER_LIGHT_OCCQ, dx10_marker_lights);
		render_lights(LP_pending);
	}

	{
		if (RImplementation.o.ssfx_volumetric && !vr_plain() && !vr_preset())
			Target->phase_ssfx_volumetric_blur();
	}

	phase = PHASE_NORMAL;

	// Postprocess
	{
		PIX_EVENT_C(DEFER_LIGHT_COMBINE, dx10_marker_lights);
		Target->phase_combine();
	}

	if (Details)
		Details->details_clear();

}
#include "../xrRender/CHudInitializer.h"

void CRender::RenderUI3DDirect()
{
	HW.pContext->ClearDepthStencilView(HW.pBaseZB, D3D_CLEAR_DEPTH | D3D_CLEAR_STENCIL, 1.0f, 0);

	RCache.set_Z(FALSE);
	RCache.set_RT(HW.pBaseRT, 0);
	GMUI3D.r_dsgraph_render_ui_3d();
}

bool CRender::GetUI3DIconAtlasInfo(u32& width, u32& height, float& resolution)
{
	if (!ps_r4_atlas || !Target || !Target->rt_ui_3d_icons)
		return false;
	width = Target->rt_ui_3d_icons->dwWidth;
	height = Target->rt_ui_3d_icons->dwHeight;
	resolution = clampr(ps_r4_atlas_resolution, 0.1f, 1.f);
	return true;
}

bool CRender::BeginUI3DIconAtlas()
{
	if (ui_3d_icon_atlas_active || !ps_r4_atlas || !Target || !Target->rt_ui_3d_icons)
		return false;

	ui_3d_icon_atlas_active = true;
	ui_3d_icon_project_saved = Device.mProject3DIcons;
	ui_3d_icon_full_transform_saved = Device.mFullTransform3DIcons;

	const u32 width = Target->rt_ui_3d_icons->dwWidth;
	const u32 height = Target->rt_ui_3d_icons->dwHeight;
	// Match the allocated atlas aspect ratio to keep model proportions intact.
	Device.mProject3DIcons.build_projection_ortho(UI_3D_ICON_ORTHO_HEIGHT * float(width) / float(height),
		UI_3D_ICON_ORTHO_HEIGHT, UI_3D_ICON_NEAR_PLANE, UI_3D_ICON_FAR_PLANE);
	Device.mFullTransform3DIcons.mul(Device.mProject3DIcons, Device.mView);

	const ref_rt& raw = Target->rt_ui_3d_icons_raw_msaa ?
		Target->rt_ui_3d_icons_raw_msaa : Target->rt_ui_3d_icons_raw;
	Target->u_setrt(raw, nullptr, nullptr, Target->rt_ui_3d_icons_depth->pZRT);
	Target->set_viewport_size(HW.pContext, width, height);

	const float clear_color[4] = {0.f, 0.f, 0.f, 0.f};
	HW.pContext->ClearRenderTargetView(raw->pRT, clear_color);
	HW.pContext->ClearDepthStencilView(Target->rt_ui_3d_icons_depth->pZRT,
		D3D_CLEAR_DEPTH | D3D_CLEAR_STENCIL, 1.f, 0);
	return true;
}

void CRender::BeginUI3DIconAtlasItem()
{
	// The MT graph retains every submission, including repeated visuals in different cells.
}

void CRender::EndUI3DIconAtlas()
{
	if (!ui_3d_icon_atlas_active)
		return;

	RCache.set_Z(FALSE);
	GMUI3D.r_dsgraph_render_ui_3d();

	// The postprocess expects an ordinary Texture2D. Resolve the multisampled
	// model atlas after all cells have been rendered, before sampling it.
	Target->u_setrt(Target->rt_ui_3d_icons->dwWidth, Target->rt_ui_3d_icons->dwHeight,
		nullptr, nullptr, nullptr, nullptr);
	if (Target->rt_ui_3d_icons_raw_msaa)
		HW.pContext->ResolveSubresource(Target->rt_ui_3d_icons_raw->pSurface, 0,
			Target->rt_ui_3d_icons_raw_msaa->pSurface, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
	Target->phase_ui_3d_icons_postprocess();

	Device.mProject3DIcons = ui_3d_icon_project_saved;
	Device.mFullTransform3DIcons = ui_3d_icon_full_transform_saved;
	Target->u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT, nullptr, nullptr, HW.pBaseZB);
	Target->set_viewport_size(HW.pContext, float(Device.dwWidth), float(Device.dwHeight));
	RCache.set_Z(FALSE);
	ui_3d_icon_atlas_active = false;
}

void CRender::render_forward()
{
	RImplementation.o.distortion = RImplementation.o.distortion_enabled; // enable distorion

	//******* Main render - second order geometry (the one, that doesn't support deffering)
	//.todo: should be done inside "combine" with estimation of of luminance, tone-mapping, etc.
	{
		// level
		phase = PHASE_NORMAL;
		//	Igor: we don't want to render old lods on next frame.
		GMBase.r_dsgraph_render_static(1); // normal level, secondary priority
		CParticlesAsync::Wait();
		GMBase.r_dsgraph_render_dynamic(1);
		GMBase.fade_render(); // faded-portals
		GMBase.r_dsgraph_render_sorted(false); // strict-sorted geoms
		g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts

		// Item and camera attachment UI draws before the sorted HUD so nearer glass and the post chain cover it
		if (g_hud)
		{
			PROF_EVENT("render_hud");
			if (g_hud->RenderActiveItemUIQuery())
				GMBase.r_dsgraph_render_hud_ui();
			if (g_hud->RenderCamAttachedUIQuery())
				GMBase.r_dsgraph_render_cam_ui();
		}

		GMBase.r_dsgraph_render_sorted_hud();
	}

	RImplementation.o.distortion = FALSE; // disable distorion
}

// Redotix99: for 3D Shader Based Scopes
void CRender::render_Reticle()
{
	VERIFY(0 == GMBase.RGraph.mapHUDSorted.Distort.size() + GMBase.RGraph.mapStaticSorted.Distort.size() + GMBase.RGraph.mapDynamicSorted.Distort.size());
	RImplementation.o.distortion = RImplementation.o.distortion_enabled;

	GMBase.r_dsgraph_render_ScopeSorted();

	RImplementation.o.distortion = FALSE;
}

void CRender::RenderToTarget(RRT target)
{
	ref_rt* RT = nullptr;

	switch (target)
	{
	case rtPDA:
		RT = &Target->rt_ui_pda;
		break;
	case rtSVP:
		RT = &Target->rt_secondVP;
		break;
	case rtWRIST:
		RT = &Target->rt_ui_wrist;
		break;
	default:
		Debug.fatal(DEBUG_INFO, "None or wrong Target specified: %i", target);
		break;
	}

	ID3DTexture2D* pBuffer = nullptr;
	HW.m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBuffer);
	HW.pContext->CopyResource((*RT)->pSurface, pBuffer);
	pBuffer->Release();
}

// GammaVR 10c probe: read a target back and write it as GVRIMG1 (header "GVRIMG1 w h dxgi_format", then rows of 4-byte texels)
void CRender::DumpTarget(RRT target, LPCSTR name)
{
	ref_rt* RT = target == rtWRIST ? &Target->rt_ui_wrist : target == rtPDA ? &Target->rt_ui_pda : nullptr;
	if (!RT || !*RT || !(*RT)->pSurface)
	{
		Msg("! [vr3wst] dump: no target %d", (int)target);
		return;
	}
	ID3D11Texture2D* src = (ID3D11Texture2D*)(*RT)->pSurface;
	D3D11_TEXTURE2D_DESC d;
	src->GetDesc(&d);
	d.Usage = D3D11_USAGE_STAGING;
	d.BindFlags = 0;
	d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	d.MiscFlags = 0;
	d.SampleDesc.Count = 1, d.SampleDesc.Quality = 0;
	ID3D11Texture2D* st = nullptr;
	if (FAILED(HW.pDevice->CreateTexture2D(&d, nullptr, &st)) || !st)
	{
		Msg("! [vr3wst] dump: staging texture failed");
		return;
	}
	HW.pContext->CopyResource(st, src);
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(HW.pContext->Map(st, 0, D3D11_MAP_READ, 0, &m)))
	{
		string_path path;
		FS.update_path(path, "$logs$", name);
		if (FILE* f = fopen(path, "wb"))
		{
			fprintf(f, "GVRIMG1 %u %u %d\n", d.Width, d.Height, (int)d.Format);
			for (u32 y = 0; y < d.Height; ++y)
				fwrite((const u8*)m.pData + (size_t)y * m.RowPitch, 4, d.Width, f);
			fclose(f);
			Msg("* [vr3wst] dump: wrote %s (%ux%u, format %d, read back from the target)", name, d.Width, d.Height, (int)d.Format);
		}
		else
			Msg("! [vr3wst] dump: cannot open %s", path);
		HW.pContext->Unmap(st, 0);
	}
	else
		Msg("! [vr3wst] dump: Map failed");
	st->Release();
}

// GammaVR 10c: shader swap on one model instance (the wrist's copy of the PDA model): the screen surface (its pass samples
// from_tex, e.g. "$user$ui") gets to_shader; only this instance's visuals change (the model pool gives each instance its own)
static int vr_retarget_visual(dxRender_Visual* v, LPCSTR from_tex, LPCSTR to_shader)
{
	if (!v)
		return 0;
	int n = 0;
	if (xr_vector<IRenderVisual*>* ch = v->get_children())
		for (IRenderVisual* c : *ch)
			n += vr_retarget_visual((dxRender_Visual*)c, from_tex, to_shader);
	Shader* S = v->shader._get();
	if (!S)
		return n;
	for (u32 e = 0; e < SHADER_ELEMENTS_MAX; ++e)
	{
		ShaderElement* E = S->E[e]._get();
		if (!E)
			continue;
		for (u32 p = 0; p < E->passes.size(); ++p)
		{
			SPass* P = E->passes[p]._get();
			if (!P || !P->T._get())
				continue;
			bool hit = false;
			string_path base = "";
			for (const auto& t : *P->T)
			{
				CTexture* tx = t.second._get();
				LPCSTR nm = tx ? tx->cName.c_str() : nullptr;
				if (!nm)
					continue;
				if (!xr_strcmp(nm, from_tex))
					hit = true;
				else if (!base[0])
					xr_strcpy(base, nm);
			}
			if (hit)
			{
				// made the way the visual's own shader was made at load: its skinning mode (the vertex shader is compiled per
				// SKIN_n; a shader made later with the global mode gets the unskinned variant, which does not match the skinned
				// vertex layout, and the draw is dropped) and its hud flag. The global skinning mode is put back after.
				const s32 prev_skin = Engine.External.GetSkinningMode();
				ref_shader old = v->shader;
				Msg("* [vr3wst] screen surface skinning mode %d, hud %d", (int)v->skinning, v->hud ? 1 : 0);
				v->SetShaderTexture(to_shader, base);
				Engine.External.SetSkinningMode(prev_skin);
				ref_shader ns = v->shader;
				v->shader = old;
				// 10c build check: the old and new shader's elements (present, priority, strict, distort, emissive, passes)
				auto dump = [](LPCSTR tag, Shader* X) {
					for (u32 k = 0; X && k < SHADER_ELEMENTS_MAX; ++k)
					{
						ShaderElement* Q = X->E[k]._get();
						if (!Q)
						{
							Msg("* [vr3wstsh] %s E[%u] none", tag, k);
							continue;
						}
						Msg("* [vr3wstsh] %s E[%u] prio %u strict %u distort %u emissive %u passes %u tex %u", tag, k, (u32)Q->flags.iPriority,
							(u32)Q->flags.bStrictB2F, (u32)Q->flags.bDistort, (u32)Q->flags.bEmissive, (u32)Q->passes.size(),
							(Q->passes.size() && Q->passes[0]->T._get()) ? (u32)Q->passes[0]->T->size() : 0u);
					}
				};
				dump("old", S);
				dump("new", ns._get());
				if (ns._get())
				{
					v->shader = ns;
					Msg("* [vr3wst] screen surface retargeted: shader %s, base texture %s", to_shader, base);
					return n + 1;
				}
				Msg("! [vr3wst] shader %s could not be made", to_shader);
				return n;
			}
		}
	}
	return n;
}
int CRender::model_RetargetScreen(IRenderVisual* V, LPCSTR from_tex, LPCSTR to_shader)
{
	return vr_retarget_visual((dxRender_Visual*)V, from_tex, to_shader);
}
extern float r_ssaLOD_A;
// 10c build check: the drawn child list of a model (a skeleton keeps hidden-bone children out of it), each child's type,
// sphere and whether its shader samples the named texture; the skeleton's LOD and the SSA test add_Dynamic makes against it
void CRender::model_DebugTree(IRenderVisual* V, const Fmatrix& xf, LPCSTR tex, LPCSTR tag)
{
	dxRender_Visual* v = (dxRender_Visual*)V;
	if (!v)
		return;
	xr_vector<IRenderVisual*>* ch = v->get_children();
	if (v->Type == MT_SKELETON_ANIM || v->Type == MT_SKELETON_RIGID)
	{
		CKinematics* K = (CKinematics*)v;
		Fvector T;
		xf.transform_tiny(T, K->vis.sphere.P);
		float D = Device.vCameraPosition.distance_to(T);
		Msg("* [vr3wstt] %s type %u skeleton lod %s sphere R %.4f dist %.4f (LOD used when ssa < r_ssaLOD_A %.6f)", tag, (u32)v->Type,
			K->m_lod ? "yes" : "no", K->vis.sphere.R, D, r_ssaLOD_A);
	}
	Msg("* [vr3wstt] %s drawn children %u", tag, ch ? (u32)ch->size() : 0u);
	for (u32 i = 0; ch && i < ch->size(); ++i)
	{
		dxRender_Visual* c = (dxRender_Visual*)(*ch)[i];
		bool has = false;
		if (Shader* S = c->shader._get())
			if (ShaderElement* E = S->E[0]._get())
				for (u32 p = 0; p < E->passes.size(); ++p)
					if (E->passes[p]->T._get())
						for (const auto& t : *E->passes[p]->T)
							if (t.second._get() && !xr_strcmp(t.second->cName.c_str(), tex))
								has = true;
		Msg("* [vr3wstt] %s child %u type %u sphere %.4f %.4f %.4f R %.4f samples %s %d", tag, i, (u32)c->Type, c->vis.sphere.P.x,
			c->vis.sphere.P.y, c->vis.sphere.P.z, c->vis.sphere.R, tex, has ? 1 : 0);
	}
}
