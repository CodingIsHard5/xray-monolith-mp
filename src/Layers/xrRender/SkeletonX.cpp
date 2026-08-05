// SkeletonX.cpp: implementation of the CSkeletonX class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#pragma warning(disable:4995)
#include <d3dx9.h>
#pragma warning(default:4995)

#ifndef _EDITOR
#include	"../../xrEngine/Render.h"
// §7d needs g_dedicated_server. Included EXPLICITLY rather than relied on transitively: it is not in
// this project's PCH chain, and ModelPool.cpp in this same directory already includes it this way.
#include	"../../xrEngine/IGame_Persistent.h"
#else
	#include "../../Include/xrAPI/xrAPI.h"
#endif

#include "SkeletonX.h"
#include "SkeletonCustom.h"
#include "../../xrEngine/fmesh.h"
#include "../../xrCPU_Pipe/xrCPU_Pipe.h"

shared_str s_bones_array_const;
shared_str s_bones_array_prev_const;

//////////////////////////////////////////////////////////////////////
// Body Part
//////////////////////////////////////////////////////////////////////
void CSkeletonX::AfterLoad(CKinematics* parent, u16 child_idx)
{
	SetParent(parent);
	ChildIDX = child_idx;
}

void CSkeletonX::_Copy(CSkeletonX* B)
{
	Parent = NULL;
	ChildIDX = B->ChildIDX;
	Vertices1W = B->Vertices1W;
	Vertices2W = B->Vertices2W;
	Vertices3W = B->Vertices3W;
	Vertices4W = B->Vertices4W;
	BonesUsed = B->BonesUsed;

	// caution - overlapped (union)
	cache_DiscardID = B->cache_DiscardID;
	cache_vCount = B->cache_vCount;
	cache_vOffset = B->cache_vOffset;
	RenderMode = B->RenderMode;
	RMS_boneid = B->RMS_boneid;
	RMS_bonecount = B->RMS_bonecount;

#if defined(USE_DX10) || defined(USE_DX11)
	m_Indices = B->m_Indices;
#endif	//	USE_DX10
}

//////////////////////////////////////////////////////////////////////
void CSkeletonX::_Render(ref_geom& hGeom, u32 vCount, u32 iOffset, u32 pCount)
{
	Fmatrix p_WV, p_WVP;

#ifdef USE_DX11 //
	if (RImplementation.o.ssfx_motionvectors)
	{
		if (Device.dwFrame > Parent->CurrentFrame)
		{
			// Save current frame
			Parent->CurrentFrame = Device.dwFrame;

			// Save prev m_W and save current m_W for the next frame
			Parent->Matrix_Prev.set(Parent->Matrix_Temp);
			Parent->Matrix_Temp.set(RCache.xforms.m_w);

			// Save bone matrix to use in the next frame
			for (u16 b = 0; b < Parent->LL_BoneCount(); b++)
			{
				CBoneInstance& Bone = Parent->LL_GetBoneInstance(b);
				Bone.mRenderTransform_prev.set(Bone.mRenderTransform_temp);
				Bone.mRenderTransform_temp.set(Bone.mRenderTransform);
			}
		}

		// Build previous WV & WVP
		if (RenderMode == 1)
		{
			// RM_SINGLE
			Fmatrix Bone_Prev;
			Bone_Prev.mul_43(Parent->Matrix_Prev, Parent->LL_GetBoneInstance(u16(RMS_boneid)).mRenderTransform_prev);
			p_WV.mul_43(RCache.xforms.m_v_prev, Bone_Prev);
			p_WVP.mul(RCache.xforms.m_p_prev, p_WV);
		}
		else
		{
			// RM_SKINNING_1B ~ RM_SKINNING_4B
			p_WV.mul_43(RCache.xforms.m_v_prev, Parent->Matrix_Prev);
			p_WVP.mul(RCache.xforms.m_p_prev, p_WV);
		}

		RCache.set_c("m_wvp_prev", p_WVP); // Apply prev matrix
	}
#endif

	RCache.stat.r.s_dynamic.add(vCount);
	switch (RenderMode)
	{
	case RM_SKINNING_SOFT:
		_Render_soft(hGeom, vCount, iOffset, pCount);
		RCache.stat.r.s_dynamic_sw.add(vCount);
		break;
	case RM_SINGLE:
		{
			Fmatrix W;
			W.mul_43(RCache.xforms.m_w, Parent->LL_GetTransform_R(u16(RMS_boneid)));
			RCache.set_xform_world(W);
			//
			RCache.set_Geometry(hGeom);
			RCache.Render(D3DPT_TRIANGLELIST, 0, 0, vCount, iOffset, pCount);
			RCache.stat.r.s_dynamic_inst.add(vCount);
		}
		break;
	case RM_SKINNING_1B:
	case RM_SKINNING_2B:
	case RM_SKINNING_3B:
	case RM_SKINNING_4B:
		{
			// Transfer matrices ( current and previous )
			R_constant* array = RCache.get_c(s_bones_array_const);
			R_constant* array_prev = RCache.get_c(s_bones_array_prev_const);

			u32 count = RMS_bonecount;
			for (u32 mid = 0; mid < count; mid++)
			{
				Fmatrix& M = Parent->LL_GetTransform_R(u16(mid));
				u32 id = mid * 3;
				RCache.set_ca(&*array, id + 0, M._11, M._21, M._31, M._41);
				RCache.set_ca(&*array, id + 1, M._12, M._22, M._32, M._42);
				RCache.set_ca(&*array, id + 2, M._13, M._23, M._33, M._43);

#ifdef USE_DX11
				if (RImplementation.o.ssfx_motionvectors) 
				{
					// Save previous transform
					Fmatrix& Mprev = Parent->LL_GetBoneInstance(u16(mid)).mRenderTransform_prev;
					RCache.set_ca(&*array_prev, id + 0, Mprev._11, Mprev._21, Mprev._31, Mprev._41);
					RCache.set_ca(&*array_prev, id + 1, Mprev._12, Mprev._22, Mprev._32, Mprev._42);
					RCache.set_ca(&*array_prev, id + 2, Mprev._13, Mprev._23, Mprev._33, Mprev._43);
				}
#endif
			}

			// render
			RCache.set_Geometry(hGeom);
			RCache.Render(D3DPT_TRIANGLELIST, 0, 0, vCount, iOffset, pCount);
			if (RM_SKINNING_1B == RenderMode)
				RCache.stat.r.s_dynamic_1B.add(vCount);
			else if (RM_SKINNING_2B == RenderMode)
				RCache.stat.r.s_dynamic_2B.add(vCount);
			else if (RM_SKINNING_3B == RenderMode)
				RCache.stat.r.s_dynamic_3B.add(vCount);
			else if (RM_SKINNING_4B == RenderMode)
				RCache.stat.r.s_dynamic_4B.add(vCount);
		}
		break;
	}
}

void CSkeletonX::_Render_soft(ref_geom& hGeom, u32 vCount, u32 iOffset, u32 pCount)
{
	u32 vOffset = cache_vOffset;

	_VertexStream& _VS = RCache.Vertex;
	if (cache_DiscardID != _VS.DiscardID() || vCount != cache_vCount)
	{
		vertRender* Dest = (vertRender*)_VS.Lock(vCount, hGeom->vb_stride, vOffset);
		cache_DiscardID = _VS.DiscardID();
		cache_vCount = vCount;
		cache_vOffset = vOffset;

		RDEVICE.Statistic->RenderDUMP_SKIN.Begin();
		if (*Vertices1W)
		{
			PSGP.skin1W(
				Dest, // dest
				*Vertices1W, // source
				vCount, // count
				Parent->bone_instances // bones
			);
		}
		else if (*Vertices2W)
		{
			PSGP.skin2W(
				Dest, // dest
				*Vertices2W, // source
				vCount, // count
				Parent->bone_instances // bones
			);
		}
		else if (*Vertices3W)
		{
			PSGP.skin3W(
				Dest, // dest
				*Vertices3W, // source
				vCount, // count
				Parent->bone_instances // bones
			);
		}
		else if (*Vertices4W)
		{
			PSGP.skin4W(
				Dest, // dest
				*Vertices4W, // source
				vCount, // count
				Parent->bone_instances // bones
			);
		}
		else
		{
			// §7d: all four arrays empty means the copy was deliberately skipped for a renderless
			// server. R_ASSERT survives release, so leaving the fatal here would turn "a dedicated
			// server should never render" -- an expectation, not a guarantee -- into a crash. Draw
			// nothing instead, and say so once.
			static bool s_said = false;
			if (!s_said)
			{
				s_said = true;
				Msg("! COOP(skin): _Render_soft reached with no vertex data (geometry skipped for a "
				    "renderless server). Drawing nothing. If this appears on a CLIENT it is a bug.");
			}
			// End() the statistic that Begin() opened above -- the normal path below does it, and an
			// early return that skips it leaves the timer permanently open.
			RDEVICE.Statistic->RenderDUMP_SKIN.End();
			_VS.Unlock(vCount, hGeom->vb_stride);
			return;
		}

		RDEVICE.Statistic->RenderDUMP_SKIN.End();
		_VS.Unlock(vCount, hGeom->vb_stride);
	}

	RCache.set_Geometry(hGeom);
	RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, vCount, iOffset, pCount);
}

// COOP §7d — the skinning-mode census. See the note at the end of CSkeletonX::_Load for why this
// exists and why it changes no behaviour. Counters only; the summary rides on the model census's
// cadence rather than adding a second one, so a quiet run stays quiet.
static u32 g_coop_skin_by_mode[8] = {0};
static u32 g_coop_skin_soft = 0;
static u64 g_coop_skin_soft_bytes = 0;
static u32 g_coop_skin_total = 0;
static bool g_coop_skin_caps_printed = false;
static u32 g_coop_skin_last_report = 0;

// §7d THE CUT. A renderless server copies character geometry into system memory for one reason: the
// stub D3D9 advertises 32 vertex-shader constant registers, so hw_bones_cnt is 2 and every model with
// more than two bones takes the software-skinning branch. Measured: 41% of skinned models, 23 MB in
// the first hundred seconds, growing for the life of the process.
//
// The copy feeds exactly three consumers, and none of them applies here:
//   * software-skinning RENDER  -- this machine renders nothing;
//   * _PickBoneSoft*            -- guarded below to return FALSE on an empty array;
//   * EnumBoneVertices          -- same data, same guard.
// Creature and actor hit detection uses CCF_Skeleton BONE SHAPES, not mesh triangles, so hit boxes
// are unaffected. PickBone's only callers are CCF_DynamicMesh (opt-in `[collide] mesh = true` physics
// objects) and ik_foot_collider (foot placement) -- both visual-fidelity refinements on a server.
//
// GATED ON g_dedicated_server, AND THE GATE ANNOUNCES ITSELF, because a gate that is false yields a
// silent no-op indistinguishable from a fix that works. If this server does not set the flag, the log
// says so rather than the saving quietly not happening.
static bool g_coop_skip_geom_announced = false;

// DISABLED 2026-08-05 AFTER IT FAILED IN THE FIRST VERIFYING RUN, and left in place rather than
// deleted because the analysis is sound and only the CONSUMER LIST was wrong.
//
// The run fataled during level load:
//     FATAL ERROR  Expression: 0  Function: CSkeletonX_ext::_CollectBoneFaces
//     FSkinned.cpp:889  Description: not implemented yet
//
// I enumerated three consumers of the vertex copies -- software-skinning render, _PickBoneSoft*,
// EnumBoneVertices -- and guarded all three. I MISSED A FOURTH, and it is the one that runs at LOAD
// time: _CollectBoneFaces walks the vertex arrays to build CBoneData::child_faces, and its
// software branch ends in R_ASSERT2(0, "not implemented yet") when they are empty. Every consumer I
// looked for was a RUNTIME consumer; I never asked what reads this data while it is being loaded.
//
// Skipping _CollectBoneFaces too would leave child_faces empty, which feeds LL_GetBoneGroups and
// CKinematics::LL_Validate's breakable check -- a widening blast radius that wants its own audit,
// not another guard bolted on under time pressure. So the cut is OFF until that audit is done.
// The guards and the census below are kept: they are correct, independently useful, and cost
// nothing.
// THE AUDIT GATE. Flip to true only after the load-time consumer audit described above.
static const bool g_coop_skin_cut_enabled = false;

static bool coop_skip_skin_geometry()
{
	if (!g_coop_skin_cut_enabled || !g_dedicated_server)
		return false;
	if (!g_coop_skip_geom_announced)
	{
		g_coop_skip_geom_announced = true;
		Msg("* COOP(skin): SKIPPING software-skinning vertex copies -- g_dedicated_server is set and "
		    "this process renders nothing. Hit boxes are unaffected (they use CCF_Skeleton bone "
		    "shapes); exact mesh picking via PickBone will return FALSE, which is what a client with "
		    "hardware skinning already does.");
	}
	return true;
}

static void coop_skeleton_note_load(LPCSTR N, u32 render_mode, bool soft, u32 bytes)
{
	if (!g_coop_skin_caps_printed)
	{
		g_coop_skin_caps_printed = true;
		// The value the whole scoping decision turns on, printed once. hw_bones_cnt is recomputed
		// here exactly as _Load computes it, so the log carries the number actually used and not a
		// second derivation that could drift from it.
		Msg("* COOP(skin): HW.Caps.geometry.dwRegisters=%u -> hw_bones_cnt=%u, "
		    "ps_r1_SoftwareSkinning=%d, g_dedicated_server=%d. Models whose bone count exceeds "
		    "hw_bones_cnt copy their whole vertex array into system memory; that copy is what §7c "
		    "measured, and g_dedicated_server is the gate that decides whether we skip it.",
		    HW.Caps.geometry.dwRegisters,
		    u32(u16((HW.Caps.geometry.dwRegisters - 22 - 3) / 3)),
		    ps_r1_SoftwareSkinning, int(g_dedicated_server));
	}
	++g_coop_skin_total;
	if (render_mode < 8) ++g_coop_skin_by_mode[render_mode];
	// Summary on the SAME 60 s rule the model census uses, but emitted from here so no symbol
	// crosses a translation unit. ModelPool.cpp and SkeletonX.cpp are compiled into EVERY render
	// layer (R1..R4), and an exe that links more than one would turn a shared non-static helper into
	// a duplicate symbol at link time. A self-contained counter cannot do that. Checked rather than
	// assumed: both files appear in all four xrRender_R*.vcxproj.
	const u32 now = Device.dwTimeGlobal;
	if (!g_coop_skin_last_report) g_coop_skin_last_report = now;
	if (now - g_coop_skin_last_report >= 60000)
	{
		g_coop_skin_last_report = now;
		Msg("* COOP(skin): skinned=%u soft=%u (%u%%) soft_bytes=%u KB. The soft fraction is what a "
		    "renderless server could stop copying, IF nothing needs it.",
		    g_coop_skin_total, g_coop_skin_soft,
		    g_coop_skin_total ? (100 * g_coop_skin_soft / g_coop_skin_total) : 0,
		    u32(g_coop_skin_soft_bytes / 1024));
	}
	if (soft)
	{
		++g_coop_skin_soft;
		g_coop_skin_soft_bytes += bytes;
		// First few only: which models are paying, by name, without turning into a per-load trace.
		if (g_coop_skin_soft <= 5)
			Msg("! COOP(skin): SOFT skinning for '%s' -- %u KB of vertices copied to system memory "
			    "on a server that renders nothing.", N, bytes / 1024);
	}
}



void CSkeletonX::_Load(const char* N, IReader* data, u32& dwVertCount)
{
	s_bones_array_const = "sbones_array";
	s_bones_array_prev_const = "sbones_array_prev";
	xr_vector<u16> bids;

	// Load vertices
	R_ASSERT(data->find_chunk(OGF_VERTICES));

	//u16			hw_bones_cnt		= u16((HW.Caps.geometry.dwRegisters-22)/3);
	//	Igor: some shaders in r1 need more free constant registers
	u16 hw_bones_cnt = u16((HW.Caps.geometry.dwRegisters - 22 - 3) / 3);

#if RENDER == R_R1
	if (ps_r1_SoftwareSkinning == 1)
		hw_bones_cnt = 0;
#endif // RENDER == R_R1

	u16 sw_bones_cnt = 0;
#ifdef _EDITOR
	hw_bones_cnt					= 0;
#endif

	u32 dwVertType, size, it, crc;
	dwVertType = data->r_u32();
	dwVertCount = data->r_u32();

	RenderMode = RM_SKINNING_SOFT;
	Render->shader_option_skinning(-1);

	switch (dwVertType)
	{
	case OGF_VERTEXFORMAT_FVF_1L: // 1-Link
	case 1:
		{
			size = dwVertCount * sizeof(vertBoned1W);
			vertBoned1W* pVO = (vertBoned1W*)data->pointer();

			for (it = 0; it < dwVertCount; ++it)
			{
				const vertBoned1W& VB = pVO[it];
				u16 mid = (u16)VB.matrix;

				if (bids.end() == std::find(bids.begin(), bids.end(), mid))
					bids.push_back(mid);

				sw_bones_cnt = _max(sw_bones_cnt, mid);
			}
#ifdef _EDITOR
			// software
			crc						= crc32	(data->pointer(),size);
			Vertices1W.create		(crc,dwVertCount,(vertBoned1W*)data->pointer());
#else
			if (1 == bids.size())
			{
				// HW- single bone
				RenderMode = RM_SINGLE;
				RMS_boneid = *bids.begin();
				Render->shader_option_skinning(0);
			}
			else if (sw_bones_cnt <= hw_bones_cnt)
			{
				// HW- one weight
				RenderMode = RM_SKINNING_1B;
				RMS_bonecount = sw_bones_cnt + 1;
				Render->shader_option_skinning(1);
			}
			else
			{
				// software
				if (!coop_skip_skin_geometry())   // §7d
				{
					crc = crc32(data->pointer(), size);
					Vertices1W.create(crc, dwVertCount, (vertBoned1W*)data->pointer());
				}
				Render->shader_option_skinning(-1);
			}
#endif
		}
		break;
	case OGF_VERTEXFORMAT_FVF_2L: // 2-Link
	case 2:
		{
			size = dwVertCount * sizeof(vertBoned2W);
			vertBoned2W* pVO = (vertBoned2W*)data->pointer();

			for (it = 0; it < dwVertCount; ++it)
			{
				const vertBoned2W& VB = pVO[it];
				sw_bones_cnt = _max(sw_bones_cnt, VB.matrix0);
				sw_bones_cnt = _max(sw_bones_cnt, VB.matrix1);

				if (bids.end() == std::find(bids.begin(), bids.end(), VB.matrix0))
					bids.push_back(VB.matrix0);

				if (bids.end() == std::find(bids.begin(), bids.end(), VB.matrix1))
					bids.push_back(VB.matrix1);
			}
			//.			R_ASSERT(sw_bones_cnt<=hw_bones_cnt);
			if (sw_bones_cnt <= hw_bones_cnt)
			{
				// HW- two weights
				RenderMode = RM_SKINNING_2B;
				RMS_bonecount = sw_bones_cnt + 1;
				Render->shader_option_skinning(2);
			}
			else
			{
				// software
				if (!coop_skip_skin_geometry())   // §7d
				{
					crc = crc32(data->pointer(), size);
					Vertices2W.create(crc, dwVertCount, (vertBoned2W*)data->pointer());
				}
				Render->shader_option_skinning(-1);
			}
		}
		break;
	case OGF_VERTEXFORMAT_FVF_3L: // 3-Link
	case 3:
		{
			size = dwVertCount * sizeof(vertBoned3W);
			vertBoned3W* pVO = (vertBoned3W*)data->pointer();

			for (it = 0; it < dwVertCount; ++it)
			{
				const vertBoned3W& VB = pVO[it];
				for (int i = 0; i < 3; ++i)
				{
					sw_bones_cnt = _max(sw_bones_cnt, VB.m[i]);

					if (bids.end() == std::find(bids.begin(), bids.end(), VB.m[i]))
						bids.push_back(VB.m[i]);
				}
			}
			//.			R_ASSERT(sw_bones_cnt<=hw_bones_cnt);
			if ((sw_bones_cnt <= hw_bones_cnt))
			{
				RenderMode = RM_SKINNING_3B;
				RMS_bonecount = sw_bones_cnt + 1;
				Render->shader_option_skinning(3);
			}
			else
			{
				if (!coop_skip_skin_geometry())   // §7d
				{
					crc = crc32(data->pointer(), size);
					Vertices3W.create(crc, dwVertCount, (vertBoned3W*)data->pointer());
				}
				Render->shader_option_skinning(-1);
			}
		}
		break;
	case OGF_VERTEXFORMAT_FVF_4L: // 4-Link
	case 4:
		{
			size = dwVertCount * sizeof(vertBoned4W);
			vertBoned4W* pVO = (vertBoned4W*)data->pointer();

			for (it = 0; it < dwVertCount; ++it)
			{
				const vertBoned4W& VB = pVO[it];

				for (int i = 0; i < 4; ++i)
				{
					sw_bones_cnt = _max(sw_bones_cnt, VB.m[i]);

					if (bids.end() == std::find(bids.begin(), bids.end(), VB.m[i]))
						bids.push_back(VB.m[i]);
				}
			}
			//.			R_ASSERT(sw_bones_cnt<=hw_bones_cnt);
			if (sw_bones_cnt <= hw_bones_cnt)
			{
				RenderMode = RM_SKINNING_4B;
				RMS_bonecount = sw_bones_cnt + 1;
				Render->shader_option_skinning(4);
			}
			else
			{
				if (!coop_skip_skin_geometry())   // §7d
				{
					crc = crc32(data->pointer(), size);
					Vertices4W.create(crc, dwVertCount, (vertBoned4W*)data->pointer());
				}
				Render->shader_option_skinning(-1);
			}
		}
		break;
	default:
		Debug.fatal(DEBUG_INFO, "Invalid vertex type in skinned model '%s'", N);
		break;
	}
#ifdef _EDITOR
	if (bids.size()>0)	
#else
	if (bids.size()>0)		//--DSR-- SilencerOverheat (1 -> 0). Why was 1 tho?
#endif
	{
		crc = crc32(&*bids.begin(), bids.size() * sizeof(u16));
		BonesUsed.create(crc, bids.size(), &*bids.begin());
	}

	// COOP §7d — MEASURE BEFORE CUTTING. §7c attributed the server's live-byte growth to base-model
	// loading and showed the lookup is sound, so the remaining question is one of SCOPE: a renderless
	// server has no use for render geometry. The four allocation sites symbolicated inside this
	// function are exactly the four `VerticesNW.create()` calls below, which run ONLY on the
	// software-skinning branch -- `sw_bones_cnt > hw_bones_cnt`, where hw_bones_cnt is derived from
	// HW.Caps.geometry.dwRegisters. On a null-D3D9 server nobody knows what that value is, so nobody
	// knows how many models take the soft path or what it costs. That is the number this prints.
	//
	// IT DELIBERATELY CUTS NOTHING. The vertex copies are consumed by _PickBoneSoft*, and PickBone
	// has two callers: CCF_DynamicMesh (opt-in `[collide] mesh` physics objects) and
	// ik_foot_collider. Creature hit detection uses CCF_Skeleton BONE SHAPES and does not touch this
	// data -- but "does not touch" is a claim from reading, and dropping geometry a server turns out
	// to need is worse than the memory it saves.
	{
		const bool soft = (*Vertices1W) || (*Vertices2W) || (*Vertices3W) || (*Vertices4W);
		coop_skeleton_note_load(N, RenderMode, soft, soft ? size : 0);
	}
}

BOOL CSkeletonX::has_visible_bones()
{
	if (RM_SINGLE == RenderMode)
	{
		return Parent->LL_GetBoneVisible((u16)RMS_boneid);
	}

	for (u32 it = 0; it < BonesUsed.size(); it++)
		if (Parent->LL_GetBoneVisible(BonesUsed[it]))
		{
			return TRUE;
		}
	return FALSE;
}


void get_pos_bones(const vertBoned1W& v, Fvector& p, CKinematics* Parent)
{
	const Fmatrix& xform = Parent->LL_GetBoneInstance((u16)v.matrix).mRenderTransform;
	xform.transform_tiny(p, v.P);
}

void get_pos_bones(const vertBoned2W& vert, Fvector& p, CKinematics* Parent)
{
	Fvector P0, P1;

	Fmatrix& xform0 = Parent->LL_GetBoneInstance(vert.matrix0).mRenderTransform;
	Fmatrix& xform1 = Parent->LL_GetBoneInstance(vert.matrix1).mRenderTransform;
	xform0.transform_tiny(P0, vert.P);
	xform1.transform_tiny(P1, vert.P);
	p.lerp(P0, P1, vert.w);
}

void get_pos_bones(const vertBoned3W& vert, Fvector& p, CKinematics* Parent)
{
	Fmatrix& M0 = Parent->LL_GetBoneInstance(vert.m[0]).mRenderTransform;
	Fmatrix& M1 = Parent->LL_GetBoneInstance(vert.m[1]).mRenderTransform;
	Fmatrix& M2 = Parent->LL_GetBoneInstance(vert.m[2]).mRenderTransform;

	Fvector P0, P1, P2;
	M0.transform_tiny(P0, vert.P);
	P0.mul(vert.w[0]);
	M1.transform_tiny(P1, vert.P);
	P1.mul(vert.w[1]);
	M2.transform_tiny(P2, vert.P);
	P2.mul(1.0f - vert.w[0] - vert.w[1]);

	p = P0;
	p.add(P1);
	p.add(P2);
}

void get_pos_bones(const vertBoned4W& vert, Fvector& p, CKinematics* Parent)
{
	Fmatrix& M0 = Parent->LL_GetBoneInstance(vert.m[0]).mRenderTransform;
	Fmatrix& M1 = Parent->LL_GetBoneInstance(vert.m[1]).mRenderTransform;
	Fmatrix& M2 = Parent->LL_GetBoneInstance(vert.m[2]).mRenderTransform;
	Fmatrix& M3 = Parent->LL_GetBoneInstance(vert.m[3]).mRenderTransform;

	Fvector P0, P1, P2, P3;
	M0.transform_tiny(P0, vert.P);
	P0.mul(vert.w[0]);
	M1.transform_tiny(P1, vert.P);
	P1.mul(vert.w[1]);
	M2.transform_tiny(P2, vert.P);
	P2.mul(vert.w[2]);
	M3.transform_tiny(P3, vert.P);
	P3.mul(1.0f - vert.w[0] - vert.w[1] - vert.w[2]);

	p = P0;
	p.add(P1);
	p.add(P2);
	p.add(P3);
}

//-----------------------------------------------------------------------------------------------------
// Wallmarks
//-----------------------------------------------------------------------------------------------------
#include "cl_intersect.h"

BOOL CSkeletonX::_PickBoneSoft1W(IKinematics::pick_result& r, float dist, const Fvector& S, const Fvector& D,
                                 u16* indices, CBoneData::FacesVec& faces)
{
	// §7d: pick_bone indexes this buffer directly, so an EMPTY array is a wild read rather
	// than a harmless miss. On a renderless server the copy is deliberately not made, and a
	// client using hardware skinning has never had it either -- FALSE is the same answer
	// both of them already give.
	if (!*Vertices1W) return FALSE;
	return pick_bone<vertBoned1W>(Vertices1W, Parent, r, dist, S, D, indices, faces);
}

BOOL CSkeletonX::_PickBoneSoft2W(IKinematics::pick_result& r, float dist, const Fvector& S, const Fvector& D,
                                 u16* indices, CBoneData::FacesVec& faces)
{
	// §7d: pick_bone indexes this buffer directly, so an EMPTY array is a wild read rather
	// than a harmless miss. On a renderless server the copy is deliberately not made, and a
	// client using hardware skinning has never had it either -- FALSE is the same answer
	// both of them already give.
	if (!*Vertices2W) return FALSE;
	return pick_bone<vertBoned2W>(Vertices2W, Parent, r, dist, S, D, indices, faces);
}

BOOL CSkeletonX::_PickBoneSoft3W(IKinematics::pick_result& r, float dist, const Fvector& S, const Fvector& D,
                                 u16* indices, CBoneData::FacesVec& faces)
{
	// §7d: pick_bone indexes this buffer directly, so an EMPTY array is a wild read rather
	// than a harmless miss. On a renderless server the copy is deliberately not made, and a
	// client using hardware skinning has never had it either -- FALSE is the same answer
	// both of them already give.
	if (!*Vertices3W) return FALSE;
	return pick_bone<vertBoned3W>(Vertices3W, Parent, r, dist, S, D, indices, faces);
}

BOOL CSkeletonX::_PickBoneSoft4W(IKinematics::pick_result& r, float dist, const Fvector& S, const Fvector& D,
                                 u16* indices, CBoneData::FacesVec& faces)
{
	// §7d: pick_bone indexes this buffer directly, so an EMPTY array is a wild read rather
	// than a harmless miss. On a renderless server the copy is deliberately not made, and a
	// client using hardware skinning has never had it either -- FALSE is the same answer
	// both of them already give.
	if (!*Vertices4W) return FALSE;
	return pick_bone<vertBoned4W>(Vertices4W, Parent, r, dist, S, D, indices, faces);
}

/*
BOOL	CSkeletonX::_PickBoneSoft1W	(Fvector& normal, float& dist, const Fvector& S, const Fvector& D, u16* indices, CBoneData::FacesVec& faces)
{
	VERIFY				(*Vertices1W);
	bool intersect		= FALSE;
	for (CBoneData::FacesVecIt it=faces.begin(); it!=faces.end(); it++){
		Fvector			p[3];
		u32 idx			= (*it)*3;
		for (u32 k=0; k<3; k++){
			vertBoned1W& vert		= Vertices1W[indices[idx+k]];
			get_pos_bones(vert, p[k], Parent);
		}
		float u,v,range	= flt_max;
		if (CDB::TestRayTri(S,D,p,u,v,range,true)&&(range<dist)){
			normal.mknormal(p[0],p[1],p[2]);
			dist		= range;
			intersect	= TRUE;
		}
	}
	return intersect;
}

BOOL CSkeletonX::_PickBoneSoft2W	(Fvector& normal, float& dist, const Fvector& S, const Fvector& D, u16* indices, CBoneData::FacesVec& faces)
{
	VERIFY				(*Vertices2W);
	bool intersect		= FALSE;
	for (CBoneData::FacesVecIt it=faces.begin(); it!=faces.end(); it++){
		Fvector			p[3];
		u32 idx			= (*it)*3;
		for (u32 k=0; k<3; k++){
			vertBoned2W& vert		= Vertices2W[indices[idx+k]];
			get_pos_bones(vert, p[k], Parent);
		}
		float u,v,range	= flt_max;
		if (CDB::TestRayTri(S,D,p,u,v,range,true)&&(range<dist)){
			normal.mknormal(p[0],p[1],p[2]);
			dist		= range;
			intersect	= TRUE;
		}
	}
	return intersect;
}
*/
// Fill Vertices
void CSkeletonX::_FillVerticesSoft1W(const Fmatrix& view, CSkeletonWallmark& wm, const Fvector& normal, float size,
                                     u16* indices, CBoneData::FacesVec& faces)
{
	VERIFY(*Vertices1W);
	for (CBoneData::FacesVecIt it = faces.begin(); it != faces.end(); it++)
	{
		Fvector p[3];
		u32 idx = (*it) * 3;
		CSkeletonWallmark::WMFace F;
		for (u32 k = 0; k < 3; k++)
		{
			vertBoned1W& vert = Vertices1W[indices[idx + k]];
			F.bone_id[k][0] = (u16)vert.matrix;
			F.bone_id[k][1] = F.bone_id[k][0];
			F.bone_id[k][2] = F.bone_id[k][0];
			F.bone_id[k][3] = F.bone_id[k][0];
			F.weight[k][0] = 0.f;
			F.weight[k][1] = 0.f;
			F.weight[k][2] = 0.f;

			const Fmatrix& xform = Parent->LL_GetBoneInstance(F.bone_id[k][0]).mRenderTransform;
			F.vert[k].set(vert.P);
			xform.transform_tiny(p[k], F.vert[k]);
		}
		Fvector test_normal;
		test_normal.mknormal(p[0], p[1], p[2]);
		float cosa = test_normal.dotproduct(normal);
		if (cosa < EPS) continue;
		if (CDB::TestSphereTri(wm.ContactPoint(), size, p))
		{
			Fvector UV;
			for (u32 k = 0; k < 3; k++)
			{
				Fvector2& uv = F.uv[k];
				view.transform_tiny(UV, p[k]);
				uv.x = (1 + UV.x) * .5f;
				uv.y = (1 - UV.y) * .5f;
			}
			wm.m_Faces.push_back(F);
		}
	}
}

void CSkeletonX::_FillVerticesSoft2W(const Fmatrix& view, CSkeletonWallmark& wm, const Fvector& normal, float size,
                                     u16* indices, CBoneData::FacesVec& faces)
{
	VERIFY(*Vertices2W);
	for (CBoneData::FacesVecIt it = faces.begin(); it != faces.end(); it++)
	{
		Fvector p[3];
		u32 idx = (*it) * 3;
		CSkeletonWallmark::WMFace F;
		for (u32 k = 0; k < 3; k++)
		{
			Fvector P0, P1;

			vertBoned2W& vert = Vertices2W[indices[idx + k]];
			F.bone_id[k][0] = vert.matrix0;
			F.bone_id[k][1] = vert.matrix1;
			F.bone_id[k][2] = F.bone_id[k][1];
			F.bone_id[k][3] = F.bone_id[k][1];
			F.weight[k][0] = vert.w;
			F.weight[k][1] = 0.f;
			F.weight[k][2] = 0.f;

			Fmatrix& xform0 = Parent->LL_GetBoneInstance(F.bone_id[k][0]).mRenderTransform;
			Fmatrix& xform1 = Parent->LL_GetBoneInstance(F.bone_id[k][1]).mRenderTransform;
			F.vert[k].set(vert.P);
			xform0.transform_tiny(P0, F.vert[k]);
			xform1.transform_tiny(P1, F.vert[k]);
			p[k].lerp(P0, P1, F.weight[k][0]);
		}
		Fvector test_normal;
		test_normal.mknormal(p[0], p[1], p[2]);
		float cosa = test_normal.dotproduct(normal);
		if (cosa < EPS) continue;
		if (CDB::TestSphereTri(wm.ContactPoint(), size, p))
		{
			Fvector UV;
			for (u32 k = 0; k < 3; k++)
			{
				Fvector2& uv = F.uv[k];
				view.transform_tiny(UV, p[k]);
				uv.x = (1 + UV.x) * .5f;
				uv.y = (1 - UV.y) * .5f;
			}
			wm.m_Faces.push_back(F);
		}
	}
}

void CSkeletonX::_FillVerticesSoft3W(const Fmatrix& view, CSkeletonWallmark& wm, const Fvector& normal, float size,
                                     u16* indices, CBoneData::FacesVec& faces)
{
	VERIFY(*Vertices3W);
	for (CBoneData::FacesVecIt it = faces.begin(); it != faces.end(); ++it)
	{
		Fvector p[3];
		u32 idx = (*it) * 3;
		CSkeletonWallmark::WMFace F;

		for (u32 k = 0; k < 3; k++)
		{
			const vertBoned3W& vert = Vertices3W[indices[idx + k]];
			F.bone_id[k][0] = vert.m[0];
			F.bone_id[k][1] = vert.m[1];
			F.bone_id[k][2] = vert.m[2];
			F.bone_id[k][3] = F.bone_id[k][2];
			F.weight[k][0] = vert.w[0];
			F.weight[k][1] = vert.w[1];
			F.weight[k][2] = 0.f;
			vert.get_pos(F.vert[k]);
			get_pos_bones(vert, p[k], Parent);
		}
		Fvector test_normal;
		test_normal.mknormal(p[0], p[1], p[2]);
		float cosa = test_normal.dotproduct(normal);
		if (cosa < EPS) continue;

		if (CDB::TestSphereTri(wm.ContactPoint(), size, p))
		{
			Fvector UV;
			for (u32 k = 0; k < 3; k++)
			{
				Fvector2& uv = F.uv[k];
				view.transform_tiny(UV, p[k]);
				uv.x = (1 + UV.x) * .5f;
				uv.y = (1 - UV.y) * .5f;
			}
			wm.m_Faces.push_back(F);
		}
	}
}

void CSkeletonX::_FillVerticesSoft4W(const Fmatrix& view, CSkeletonWallmark& wm, const Fvector& normal, float size,
                                     u16* indices, CBoneData::FacesVec& faces)
{
	VERIFY(*Vertices4W);
	for (CBoneData::FacesVecIt it = faces.begin(); it != faces.end(); ++it)
	{
		Fvector p[3];
		u32 idx = (*it) * 3;
		CSkeletonWallmark::WMFace F;

		for (u32 k = 0; k < 3; k++)
		{
			const vertBoned4W& vert = Vertices4W[indices[idx + k]];
			F.bone_id[k][0] = vert.m[0];
			F.bone_id[k][1] = vert.m[1];
			F.bone_id[k][2] = vert.m[2];
			F.bone_id[k][3] = vert.m[3];
			F.weight[k][0] = vert.w[0];
			F.weight[k][1] = vert.w[1];
			F.weight[k][2] = vert.w[2];
			vert.get_pos(F.vert[k]);
			get_pos_bones(vert, p[k], Parent);
		}
		Fvector test_normal;
		test_normal.mknormal(p[0], p[1], p[2]);
		float cosa = test_normal.dotproduct(normal);
		if (cosa < EPS) continue;

		if (CDB::TestSphereTri(wm.ContactPoint(), size, p))
		{
			Fvector UV;
			for (u32 k = 0; k < 3; k++)
			{
				Fvector2& uv = F.uv[k];
				view.transform_tiny(UV, p[k]);
				uv.x = (1 + UV.x) * .5f;
				uv.y = (1 - UV.y) * .5f;
			}
			wm.m_Faces.push_back(F);
		}
	}
}

#if defined(USE_DX10) || defined(USE_DX11)
void CSkeletonX::_DuplicateIndices(const char* N, IReader* data)
{
	//	We will have trouble with container since don't know were to take readable indices
	VERIFY(!data->find_chunk(OGF_ICONTAINER));
	//	Index buffer replica since we can't read from index buffer in DX10
	//ref_smem<u16>			Indices;
	R_ASSERT(data->find_chunk(OGF_INDICES));
	u32 iCount = data->r_u32();

	u32 size = iCount * 2;
	u32 crc = crc32(data->pointer(), size);
	m_Indices.create(crc, iCount, (u16*)data->pointer());
}
#endif	//	USE_DX10
