// S2 stage 4d-2, slice S4a (sim_tick gated mode only, mask bit SIM4D_POSE): skeletal poses drawn between ticks.
// Design: plan-facts/stage4d2-npc-animation-design-2026-11-27.md (section 12 overrides the body). Code notes:
// plan-facts/stage4d2-s4a-code-notes-2026-11-27.md.
//
// Only skeletons xrGame marked as the visual of a simulated object (IKinematics::LL_SetSimObjectVisual, set by
// CGameObject::OnChangeVisual, cleared by CKinematics::Spawn/Copy) are captured and drawn between ticks; every other
// skeleton (HUD models, VR models, anything made by a bare model_Create) is drawn stock, always.
// The slice never writes mTransform, mTransformHidden or mRenderTransform (the simulation reads them: bullet picks, IK
// rays, hit shapes). It writes only its own storage (opaque, owned by sim_pose.cpp) and, in the renderer's motion-vector
// block, mRenderTransform_temp (render-only).
//
// Include after stdafx.h (needs device.h: g_sim_render_lerp, g_sim_pose_active, g_sim_synth_pose) and with
// Include/xrRender/Kinematics.h (IKinematics::has_sim_pose).
#pragma once

#include "../Include/xrRender/Kinematics.h"

// Capture, at the end of CKinematics::CalculateBones (after Update_Callback, still under the skeleton's UCalc_Mutex):
// copies the bones' mRenderTransform into the skeleton's history, stamped with (g_sim_view_tick, g_sim_view_epoch).
// The caller tests g_sim_pose_active and the skeleton's mark first; any thread, holding K->UCalc_Mutex.
ENGINE_API void sim_pose_capture(IKinematics* K);

// Draw: the bone matrices (bind -> model, the same space as mRenderTransform) to upload for K in this frame, or nullptr
// = draw the live bones (stock). Computed once per Device.dwFrame and reused by every later pass and eye of the frame
// (recomputed only if a capture changed the history since). Render path (main thread), holding K->UCalc_Mutex
// (CSkeletonX::_Render's lock). The pointer is valid until the next capture or release of K.
ENGINE_API const Fmatrix* sim_pose_draw(IKinematics* K);

// the renderer's one-line entry: nullptr unless this frame is drawn between ticks, the slice is on and K has a history
IC const Fmatrix* sim_pose_draw_frame(IKinematics* K)
{
	return (g_sim_render_lerp && g_sim_pose_active && K->has_sim_pose()) ? sim_pose_draw(K) : nullptr;
}

// Release K's storage (CKinematics::IBoneInstances_Destroy and Spawn). Takes K->UCalc_Mutex. The caller tests
// K->has_sim_pose() first, so a skeleton that never had storage (every skeleton, if gated mode with the bit on never ran) costs one test.
ENGINE_API void sim_pose_release(IKinematics* K);

// ---- test-only (every function below does nothing unless -det_mode; see g_sim_synth_pose in device.h) ----
// switches, parsed once by the device (sim_tick_plan's once-block) under -det_mode only
ENGINE_API void sim_pose_parse_switches();
// one skinned bone matrix handed to the shader (CSkeletonX::_Render's upload loop, only while g_sim_synth_pose): mid of
// count, M = the matrix actually passed to set_ca. Feeds the "pw" hu and the "up" witness columns.
ENGINE_API void sim_pose_wit_upload(IKinematics* K, u32 mid, u32 count, const Fmatrix& M);
// witness: reset before a gated frame; print this frame's columns (p, pw, up, pn) to the witness line (lever only)
ENGINE_API void sim_pose_witness_reset();
ENGINE_API void sim_pose_witness_print(FILE* f);
