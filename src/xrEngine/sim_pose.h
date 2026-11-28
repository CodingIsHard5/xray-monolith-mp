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

// S2 stage 4d-2 S4b: unlocked pre-test: K's pose may be drawn between ticks in this frame (gated frame, slice on, K
// marked and captured at least once). False means sim_pose_bone_drawn would return 0; callers use it to skip their
// lock. The values it reads (globals written by the main thread, the mark, the storage slot) only decide whether the
// locked path runs, which re-checks everything under the lock.
IC bool sim_pose_maybe_drawn(IKinematics* K)
{
	return g_sim_render_lerp && g_sim_pose_active && K->has_sim_pose() && K->LL_GetSimObjectVisual();
}

// S4b: which captured pose a bone matrix a placement READ (t_read, bone -> model, as LL_GetTransform returned it) came
// from, and bone b as drawn in this frame (t_draw, bone -> model). Returns 0 when K's pose is not drawn between ticks in
// this frame (form not L) or b is not a captured visible bone (t_draw, u untouched); else u = the pose's draw u and
// a mask: SIM_POSE_BONE_CUR (t_read == the newest capture's mTransform bitwise) | SIM_POSE_BONE_PREV (== the previous
// one's), or SIM_POSE_BONE_OTHER alone (neither; t_draw untouched). t_draw is bitwise the captured mTransform when the
// blend left b at that endpoint. For placing an item that hangs on b (CGameObject::sim_render_world_calc,
// script_attachment::Render). Render path, main thread; takes K->UCalc_Mutex (recursive). Reads only; evaluates K's
// draw pose for this frame if nothing did yet (sim_pose_draw, the same evaluation CSkeletonX::_Render would make).
enum
{
	SIM_POSE_BONE_CUR = 1,
	SIM_POSE_BONE_PREV = 2,
	SIM_POSE_BONE_OTHER = 4,
};
ENGINE_API int sim_pose_bone_drawn(IKinematics* K, u16 b, const Fmatrix& t_read, Fmatrix& t_draw, float& u);

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
// S4b (test-only): tripwire checksum of every bone field of K the simulation reads (as the S4a tripwire); caller holds
// K->UCalc_Mutex
ENGINE_API u64 sim_pose_tw_bones(IKinematics* K);
// S4b (test-only): the witness NPC's name (-det_synth_pose_npc; empty without -det_synth_pose)
ENGINE_API const shared_str& sim_pose_wit_npc();
// S4b witness "pa": the witness NPC's active item as the child rule placed it in this frame (filled by xrGame, the first
// call of the frame wins). Main thread.
struct SimPoseAttachWit
{
	char kind; // the child's record: B one bone, T bone position only, W two hands (CWeapon), N two hands, unit axes
	           // (CMissile, CArtefact); X = record stale (XFORM() placed some other way since); '-' = no record
	char form; // C corrected (the placement redone from the drawn bones), R corrected by the right hand alone (two-hand
	           // frame unavailable), O not corrected: the placement read bones of neither drawn capture, P the 4b rule
	           // alone (no correction needed or possible), S stock (drawn at its XFORM())
	char cls; // which capture the placement read: c the newest (or both equal), p the previous one, o neither,
	          // '-' not classified (no valid record, or the parent's pose is not drawn between ticks)
	u16 ba, bb; // the record's bones (BI_NONE = none)
	bool have_u; // the parent's pose is drawn between ticks in this frame and ba is a captured bone (u, hd, hr set)
	float u;
	Fvector wd; // where the child is drawn (world)
	Fvector ws; // where the 4b rule alone would draw it (world): the stock placement relative to the drawn root
	Fvector hd; // bone ba's joint as drawn, through the parent's drawn root (world)
	Fvector hr; // bone ba's joint as the placement read it, through the parent's drawn root (world)
	bool have_l; // forms C and R: ld, ls set
	Fvector ld, ls; // the child's origin in its placement frame: drawn (wd in parent_draw * drawn frame) and as placed
	                // (the recorded placement in parent_draw * recorded frame); equal when the item follows the frame
	u64 hx; // checksum of the child's XFORM() at the child rule
	u16 child_id; // its ID (the print hashes XFORM() again after the render: hxe)
};
ENGINE_API void sim_pose_wit_attach(const SimPoseAttachWit& w);
