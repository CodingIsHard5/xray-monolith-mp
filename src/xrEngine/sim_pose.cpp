// S2 stage 4d-2, slice S4a: skeletal poses drawn between ticks (see sim_pose.h). Design:
// plan-facts/stage4d2-npc-animation-design-2026-11-27.md, sections 2-7 and 12 (12 wins).
//
// Per marked skeleton: the bone matrices (mRenderTransform, bind -> model) of the last two ticks it was calculated in
// (prev, cur), stamped with (tick, epoch) as 4b stamps an object's root. A frame drawn between ticks uploads, instead of
// the live bones, draw = per bone nlerp(prev, cur, alpha) with the bone's joint as the pivot. Every rule below falls
// back to the live bones (stock) when it cannot show a real pair.
//
// Known limits of S4a (review 2026-11-27; visual only, with the bit on):
// - an item attached to a bone (an NPC's weapon) is placed from the live hand bone, the hand is drawn blended: the item
//   would lead the hand by up to a tick of hand motion. Slice S4b corrects it (sim_pose_bone_drawn, used by
//   CGameObject::sim_render_world_calc's child rule and script_attachment::Render); see
//   plan-facts/stage4d2-s4b-design-and-code-2026-11-27.md for what it does not cover (lights, particles on bones).
// - software-skinned parts (CSkeletonX RM_SKINNING_SOFT) and wallmarks/bone-attached effects read the live bones.
// - a frame that ran two or more ticks leaves no consecutive pair: below the tick rate the slice draws stock.
// - players' bodies are not marked (GameObject.cpp OnChangeVisual): stock.
// - a bone written after the frame's first evaluation without a capture is not re-checked in that frame.
//
// Behaviour with S4b, intended (c216081fd re-review N6): when the render is clamped at alpha = 1 (it runs a tick or more
// behind real time) and the item was placed in the tick before its parent moved, the item now sits on the drawn hand.
// Stock (and S4b before c216081fd) drew it trailing the hand by the parent's in-tick root motion (about 5 cm walking,
// 17 cm running at a 30 Hz tick). That is the S4b design (review C3), not a regression.
//
// Rule One: nothing here writes a field the simulation reads. Writes: this file's storage (opaque to every other
// file: IKinematics::m_sim_pose, friend SimPoseAccess below) and the witness statics. The only exception is the
// test-only must-fail break (g_sim_4d_break & SIM4D_POSE, det_mode only).
#include "stdafx.h"

#include "sim_pose.h"
#include "bone.h"
#include "IGame_Level.h"
#include "xr_object.h"
#include "../Include/xrRender/RenderVisual.h"

#include <atomic>

// at most 64 bones (the visibility mask is a Flags64; a skeleton with more is never captured: sim_pose_capture_body)
static const u16 SIM_POSE_MAX_BONES = 64;
// skeleton-level snap: a visible bone's joint moving more than this between prev and cur (model space) draws cur
// (design 2.3, D7; the value is a guess)
static const float SIM_POSE_SNAP_SQ = 0.5f * 0.5f;

// result of the frame's draw evaluation for one skeleton (witness form letters in sim_pose_form_name)
enum SimPoseForm : u8
{
	SPF_NONE = 0, // not evaluated
	SPF_LERP, // L: the pair is drawn at u
	SPF_NOPAIR, // Sn: no pair for the newest tick (no history, stale stamp, other epoch)
	SPF_BONES, // Sb: bone count or visibility mask changed since the capture
	SPF_GUARD, // Sg: the live bones differ from cur (a writer after the capture): stock
	SPF_SNAP, // Ss: a joint jumped more than the snap distance between prev and cur: stock
};

struct SimPoseState
{
	u16 n = 0; // bones stored (0 = joints not set up yet)
	bool have = false; // cur holds a capture
	u32 epoch = 0; // g_sim_view_epoch of cur
	u32 tick_p = 0; // tick of prev; tick_p + 1 == tick_c means "a pair"; tick_p == tick_c means "no history"
	u32 tick_c = 0; // tick of cur
	u64 vis = 0; // LL_GetBonesVisible() of cur (and of prev when they are a pair)
	u32 seq = 0; // +1 on every capture that changed prev/cur: the draw cache below is valid for one seq
	u32 draw_frame = u32(-1); // Device.dwFrame of the last draw evaluation
	u32 draw_seq = 0; // seq it was evaluated for
	u8 form = SPF_NONE; // its result
	float draw_u = 1.f; // and its u
	u16 wit_bone = u16(-2); // test-only: the witness joint's bone (u16(-2) = not looked up yet)
	Fvector joint[SIM_POSE_MAX_BONES]; // each bone's joint in bind space = model-space bind position = inverse(m2b).c
	Fmatrix prev[SIM_POSE_MAX_BONES];
	Fmatrix cur[SIM_POSE_MAX_BONES];
	Fmatrix draw[SIM_POSE_MAX_BONES];
	// S4b: the same two captures' mTransform (bone -> model, what LL_GetTransform returns for a visible bone), so that a
	// placement's bone reads can be matched bitwise to the pose they came from (sim_pose_bone_drawn)
	Fmatrix tprev[SIM_POSE_MAX_BONES];
	Fmatrix tcur[SIM_POSE_MAX_BONES];
};

// the only access to IKinematics::m_sim_pose (declared friend in Include/xrRender/Kinematics.h)
struct SimPoseAccess
{
	static SimPoseState* get(IKinematics* K) { return static_cast<SimPoseState*>(K->m_sim_pose); }
	static void set(IKinematics* K, SimPoseState* s) { K->m_sim_pose = s; }
};

// ---- test-only witness state (written only while g_sim_synth_pose, i.e. -det_mode -det_synth_pose) ----
struct SimPoseWitness
{
	u32 neval = 0, nlerp = 0, nguard = 0, nsnap = 0; // skeletons evaluated this frame; of them L, Sg, Ss
	// pw: the first skeleton evaluated this frame
	bool pw = false;
	IKinematics* pw_k = nullptr;
	u8 pw_form = SPF_NONE;
	float pw_u = 0.f;
	bool pw_have = false;
	u32 pw_ptick = 0, pw_ctick = 0;
	u64 pw_hd = 0, pw_hl = 0, pw_hp = 0, pw_hc = 0;
	Fvector pw_jd, pw_jp, pw_jc;
	bool pw_up_active = false, pw_up_done = false; // its first skinned leaf's upload
	u32 pw_up_count = 0;
	u64 pw_hu = 0; // hash of the matrices handed to set_ca for that leaf
	bool pw_hdu_ok = false;
	u64 pw_hdu = 0; // hash of the draw array over the same bones (comparable with pw_hu)
	// up: the first skinned leaf uploaded this frame, any skeleton, marked or not
	bool up_active = false, up_done = false;
	IKinematics* up_k = nullptr;
	u64 up_hu = 0, up_hl = 0;
	// uu (W1): skinned-leaf uploads of UNMARKED skeletons this frame: leaves uploaded, of them handed anything but the
	// live bones, of them with storage; and unmarked skeletons whose live pose changed since their upload in the
	// previous frame (sim_pose_wit_unmarked_first)
	u32 uu_nu = 0, uu_nud = 0, uu_nus = 0, uu_nposed = 0;
	IKinematics* uu_k = nullptr; // the unmarked leaf being uploaded (from its mid 0 to its last matrix)
	bool uu_diff = false; // a matrix of that leaf was not the live bone itself
};
static SimPoseWitness s_wit;
// W1 (test-only): each unmarked skeleton's live-pose hash at its first upload of a frame. Fixed static slots, no heap
// (the off arm must not allocate where build 1 does not: LB); never reset per frame (the previous frame is the point).
// Main thread only: written from CSkeletonX::_Render's upload loop, which runs only while g_sim_synth_pose (det mode,
// serial render). A full table evicts the oldest slot, so a missed lookup can only lower nposed. The one way it can be raised: a
// freed skeleton's address (same bone count) reused by another unmarked skeleton in the very next frame, once.
struct SimPoseUnmarkedSlot
{
	IKinematics* k = nullptr;
	u32 frame = 0; // Device.dwFrame of the stored hash
	u64 h = 0; // hash of mRenderTransform over all n bones
	u16 n = 0; // LL_BoneCount() (part of the key: a reused address with another bone count is another skeleton)
};
static const u32 SIM_POSE_UU_SLOTS = 32;
static SimPoseUnmarkedSlot s_wit_uu[SIM_POSE_UU_SLOTS];
// render-only-frame captures (design 12, belt and braces): bytes differ from cur (history dropped) / byte-equal (no-op).
// Atomic: captures may run on the worker threads outside det serial mode.
static std::atomic<u32> s_wit_ro_diff{0};
static std::atomic<u32> s_wit_ro_equal{0};
static string64 s_wit_npc_name = "sim_default_duty_428175"; // -det_synth_pose_npc <name>
static shared_str s_wit_npc; // docked once at parse time, only with -det_synth_pose
// S4b witness "pa" (written only while g_sim_synth_pose, main thread)
static bool s_wit_pa = false;
static SimPoseAttachWit s_wit_pa_v;

// ---------------------------------------------------------------------------------------------------------------
// helpers (all called with K->UCalc_Mutex held)

static void sim_pose_joints(IKinematics* K, SimPoseState* s, u16 n)
{
	for (u16 b = 0; b < n; ++b)
	{
		Fmatrix bind;
		bind.invert(K->LL_GetData(b).m2b_transform); // m2b = inverse of the model-space bind matrix (bone.cpp)
		s->joint[b] = bind.c;
	}
}

// cur (mRenderTransform) and tcur (mTransform, S4b) from the live bones
static void sim_pose_copy_live(IKinematics* K, SimPoseState* s, u16 n)
{
	for (u16 b = 0; b < n; ++b)
	{
		const CBoneInstance& B = K->LL_GetBoneInstance(b);
		s->cur[b] = B.mRenderTransform;
		s->tcur[b] = B.mTransform;
	}
}

// prev = cur (both spaces)
static void sim_pose_cur_to_prev(SimPoseState* s, u16 n)
{
	memcpy(s->prev, s->cur, n * sizeof(Fmatrix));
	memcpy(s->tprev, s->tcur, n * sizeof(Fmatrix));
}

static bool sim_pose_live_equals(IKinematics* K, const Fmatrix* m, u16 n)
{
	for (u16 b = 0; b < n; ++b)
		if (0 != memcmp(&K->LL_GetBoneInstance(b).mRenderTransform, &m[b], sizeof(Fmatrix)))
			return false;
	return true;
}

static u64 sim_pose_hash_live(IKinematics* K, u16 n)
{
	u64 h = sim_4d_hash_seed;
	for (u16 b = 0; b < n; ++b)
		h = sim_4d_hash(h, &K->LL_GetBoneInstance(b).mRenderTransform, sizeof(Fmatrix));
	return h;
}

// tripwire checksum (test-only): every bone field the simulation reads, and the visibility mask (design 7.4)
static u64 sim_pose_tw_hash(IKinematics* K)
{
	u64 h = sim_4d_hash_seed;
	const u16 n = K->LL_BoneCount();
	for (u16 b = 0; b < n; ++b)
	{
		const CBoneInstance& B = K->LL_GetBoneInstance(b);
		h = sim_4d_hash(h, &B.mTransform, sizeof(Fmatrix));
		h = sim_4d_hash(h, &B.mTransformHidden, sizeof(Fmatrix));
		h = sim_4d_hash(h, &B.mRenderTransform, sizeof(Fmatrix));
		h = sim_4d_hash(h, B.param, sizeof(B.param));
	}
	const u64 vis = K->LL_GetBonesVisible();
	return sim_4d_hash(h, &vis, sizeof(vis));
}

// one bone between a (prev) and b (cur) at u in (0, 1): rotation nlerp, axis scale lerp, the joint (pa, pb = a(j), b(j))
// lerped and used as the pivot. false = not a plain rotation times an axis scale (degenerate, hidden with scale 0,
// mirrored, or skewed): the caller draws cur for this bone
static bool sim_pose_lerp_bone(Fmatrix& out, const Fmatrix& a, const Fmatrix& b, const Fvector& j, const Fvector& pa,
	const Fvector& pb, float u)
{
	const Fvector sa = {a.i.magnitude(), a.j.magnitude(), a.k.magnitude()};
	const Fvector sb = {b.i.magnitude(), b.j.magnitude(), b.k.magnitude()};
	if (sa.x < EPS_S || sa.y < EPS_S || sa.z < EPS_S || sb.x < EPS_S || sb.y < EPS_S || sb.z < EPS_S)
		return false;
	Fmatrix ra, rb; // a and b without their axis scale
	ra.identity();
	ra.i.div(a.i, sa.x);
	ra.j.div(a.j, sa.y);
	ra.k.div(a.k, sa.z);
	rb.identity();
	rb.i.div(b.i, sb.x);
	rb.j.div(b.j, sb.y);
	rb.k.div(b.k, sb.z);
	const float ortho = 1e-3f; // a non-uniform scale in the bone frame times m2b's rotation is skewed: no quaternion
	if (_abs(ra.i.dotproduct(ra.j)) > ortho || _abs(ra.i.dotproduct(ra.k)) > ortho || _abs(ra.j.dotproduct(ra.k)) > ortho ||
		_abs(rb.i.dotproduct(rb.j)) > ortho || _abs(rb.i.dotproduct(rb.k)) > ortho || _abs(rb.j.dotproduct(rb.k)) > ortho)
		return false;
	Fvector nrm;
	if (nrm.crossproduct(ra.i, ra.j).dotproduct(ra.k) <= 0.f || nrm.crossproduct(rb.i, rb.j).dotproduct(rb.k) <= 0.f)
		return false; // mirrored
	Fquaternion qa, qb, q;
	qa.set(ra);
	qb.set(rb);
	if (qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0.f)
		qb.set(-qb.w, -qb.x, -qb.y, -qb.z); // the shorter way round
	const float v = 1.f - u;
	q.set(v * qa.w + u * qb.w, v * qa.x + u * qb.x, v * qa.y + u * qb.y, v * qa.z + u * qb.z);
	q.normalize();
	Fvector zero;
	zero.set(0.f, 0.f, 0.f);
	out.mk_xform(q, zero);
	Fvector s;
	s.lerp(sa, sb, u);
	out.i.mul(s.x);
	out.j.mul(s.y);
	out.k.mul(s.z);
	// translation: the joint lands on lerp(pa, pb, u): t = p - (rotation * scale)(j)
	Fvector p, rj;
	p.lerp(pa, pb, u);
	out.transform_dir(rj, j);
	out.c.sub(p, rj);
	return true;
}

// the draw array for a valid pair at u in [0, 1]; false = skeleton-level snap (draw stock)
static bool sim_pose_blend(SimPoseState* s, float u)
{
	const u16 n = s->n;
	Fvector pp[SIM_POSE_MAX_BONES], pc[SIM_POSE_MAX_BONES];
	for (u16 b = 0; b < n; ++b)
	{
		s->prev[b].transform_tiny(pp[b], s->joint[b]);
		s->cur[b].transform_tiny(pc[b], s->joint[b]);
		if ((s->vis & (u64(1) << b)) && pp[b].distance_to_sqr(pc[b]) > SIM_POSE_SNAP_SQ)
			return false;
	}
	// endpoints exact (design 3.1): alpha 0 shows prev bitwise, alpha 1 cur (= the live bones, by the guard); a pose
	// that did not change is drawn as cur bitwise (no rounding noise, no work)
	if (u >= 1.f || 0 == memcmp(s->prev, s->cur, n * sizeof(Fmatrix)))
	{
		memcpy(s->draw, s->cur, n * sizeof(Fmatrix));
		return true;
	}
	if (u <= 0.f)
	{
		memcpy(s->draw, s->prev, n * sizeof(Fmatrix));
		return true;
	}
	for (u16 b = 0; b < n; ++b)
	{
		// a hidden bone (scale 0, its mask bit clear in both: the masks are equal) is drawn as cur
		if (!(s->vis & (u64(1) << b)) || !sim_pose_lerp_bone(s->draw[b], s->prev[b], s->cur[b], s->joint[b], pp[b], pc[b], u))
			s->draw[b] = s->cur[b];
	}
	return true;
}

static LPCSTR sim_pose_form_name(u8 form)
{
	switch (form)
	{
	case SPF_LERP: return "L";
	case SPF_NOPAIR: return "Sn";
	case SPF_BONES: return "Sb";
	case SPF_GUARD: return "Sg";
	case SPF_SNAP: return "Ss";
	default: return "-";
	}
}

// ---------------------------------------------------------------------------------------------------------------
// capture

static void sim_pose_capture_body(IKinematics* K)
{
	const u16 n = K->LL_BoneCount();
	SimPoseState* s = SimPoseAccess::get(K);
	if (n == 0 || n > SIM_POSE_MAX_BONES)
	{
		if (s)
		{
			s->have = false;
			s->seq++;
		}
		return; // nothing to keep (cannot happen for a loaded model: Load asserts <= 64)
	}
	if (!s)
	{
		s = xr_new<SimPoseState>();
		SimPoseAccess::set(K, s);
	}
	if (s->n != n)
	{
		sim_pose_joints(K, s, n);
		s->n = n;
		s->have = false;
	}
	const u64 vis = K->LL_GetBonesVisible();
	const u32 tick = g_sim_view_tick;
	const u32 epoch = g_sim_view_epoch;
	if (g_sim_render_notick && s->have)
	{
		// design 12 (C1), belt and braces: a calculation on a render-only frame. Byte-equal to cur (the F5 recalcs of
		// armed NPCs are expected to be): nothing changes. Different: a model posed between ticks, whatever caused it:
		// no history for it (prev = cur = new), so it is drawn live until two clean ticks make a pair again.
		if (vis == s->vis && sim_pose_live_equals(K, s->cur, n))
		{
			if (g_sim_synth_pose)
				s_wit_ro_equal++;
			return;
		}
		if (g_sim_synth_pose)
			s_wit_ro_diff++;
		sim_pose_copy_live(K, s, n);
		sim_pose_cur_to_prev(s, n);
		s->tick_p = tick;
	}
	else if (s->have && s->epoch == epoch && s->tick_c == tick)
	{
		// again in the same tick (another caller, or after an invalidate): the newer pose of the same tick
		sim_pose_copy_live(K, s, n);
		if (vis != s->vis)
			s->tick_p = tick; // the mask changed: prev is not comparable, no history
	}
	else if (s->have && s->epoch == epoch && s->tick_c + 1 == tick && vis == s->vis)
	{
		// the next tick: a pair
		sim_pose_cur_to_prev(s, n);
		sim_pose_copy_live(K, s, n);
		s->tick_p = s->tick_c;
	}
	else
	{
		// first capture, a gap, a resync (other epoch) or another mask: no history
		sim_pose_copy_live(K, s, n);
		sim_pose_cur_to_prev(s, n);
		s->tick_p = tick;
	}
	s->tick_c = tick;
	s->epoch = epoch;
	s->vis = vis;
	s->have = true;
	s->seq++;
}

ENGINE_API void sim_pose_capture(IKinematics* K)
{
	// test-only tripwire (g_sim_synth_tw is FALSE unless -det_mode -det_synth_tw): the capture reads the bones only
	if (!g_sim_synth_tw)
	{
		sim_pose_capture_body(K);
		return;
	}
	const u64 before = sim_pose_tw_hash(K);
	sim_pose_capture_body(K);
	if (sim_pose_tw_hash(K) != before)
		sim_4d_tw_bad("pose");
}

// ---------------------------------------------------------------------------------------------------------------
// draw

static void sim_pose_wit_eval(IKinematics* K, SimPoseState* s)
{
	SimPoseWitness& w = s_wit;
	w.neval++;
	if (s->form == SPF_LERP)
		w.nlerp++;
	else if (s->form == SPF_GUARD)
		w.nguard++;
	else if (s->form == SPF_SNAP)
		w.nsnap++;
	if (w.pw)
		return;
	w.pw = true;
	w.pw_k = K;
	w.pw_form = s->form;
	w.pw_u = s->draw_u;
	w.pw_have = s->have;
	w.pw_ptick = s->tick_p;
	w.pw_ctick = s->tick_c;
	const u16 n = s->n;
	w.pw_hl = sim_pose_hash_live(K, K->LL_BoneCount());
	w.pw_hp = sim_4d_hash(sim_4d_hash_seed, s->prev, n * sizeof(Fmatrix));
	w.pw_hc = sim_4d_hash(sim_4d_hash_seed, s->cur, n * sizeof(Fmatrix));
	if (s->form != SPF_LERP)
		return;
	w.pw_hd = sim_4d_hash(sim_4d_hash_seed, s->draw, n * sizeof(Fmatrix));
	if (s->wit_bone == u16(-2))
		s->wit_bone = K->LL_BoneID("bip01_r_hand");
	const u16 b = (s->wit_bone < n) ? s->wit_bone : u16(n - 1); // BI_NONE (absent) -> the last bone
	s->draw[b].transform_tiny(w.pw_jd, s->joint[b]);
	s->prev[b].transform_tiny(w.pw_jp, s->joint[b]);
	s->cur[b].transform_tiny(w.pw_jc, s->joint[b]);
}

static const Fmatrix* sim_pose_draw_eval(IKinematics* K, SimPoseState* s)
{
	const bool first = s->draw_frame != Device.dwFrame; // the first evaluation of this skeleton in this frame
	s->draw_frame = Device.dwFrame;
	s->draw_seq = s->seq;
	const u16 n = s->n;
	float u = 1.f;
	u8 form;
	if (!s->have || s->epoch != g_sim_view_epoch || s->tick_c != g_sim_view_tick || s->tick_p + 1 != s->tick_c)
		form = SPF_NOPAIR; // not calculated in the newest tick, or no history
	else if (K->LL_BoneCount() != n || K->LL_GetBonesVisible() != s->vis)
		form = SPF_BONES;
	else if (!sim_pose_live_equals(K, s->cur, n))
		form = SPF_GUARD; // a writer after the capture (legs copy, SetBoneVisible, shell bind, ...): stock
	else
	{
		u = g_sim_render_alpha;
		clamp(u, 0.f, 1.f);
		form = sim_pose_blend(s, u) ? SPF_LERP : SPF_SNAP;
	}
	s->form = form;
	s->draw_u = u;
	if (g_sim_synth_pose && first)
		sim_pose_wit_eval(K, s); // test-only, before the break below (hl is the live pose)
	if (form != SPF_LERP)
		return nullptr;
	// S2 stage 4d-2 must-fail break (test-only; g_sim_4d_break is 0 unless -det_mode): the drawn pose is written into
	// the bones the simulation reads (bullet picks, IK rays, ray queries read mRenderTransform)
	if (g_sim_4d_break & SIM4D_POSE)
		for (u16 b = 0; b < n; ++b)
			K->LL_GetBoneInstance(b).mRenderTransform = s->draw[b];
	return s->draw;
}

ENGINE_API const Fmatrix* sim_pose_draw(IKinematics* K)
{
	SimPoseState* s = SimPoseAccess::get(K);
	if (!s || !K->LL_GetSimObjectVisual())
		return nullptr; // unmarked: stock, always
	if (s->draw_frame == Device.dwFrame && s->draw_seq == s->seq)
		return s->form == SPF_LERP ? s->draw : nullptr; // every later pass / eye of the frame: the same array
	if (!g_sim_synth_tw)
		return sim_pose_draw_eval(K, s);
	const u64 before = sim_pose_tw_hash(K); // test-only tripwire, as in sim_pose_capture
	const Fmatrix* r = sim_pose_draw_eval(K, s);
	if (sim_pose_tw_hash(K) != before)
		sim_4d_tw_bad("pose");
	return r;
}

// S4b: see sim_pose.h. Rule One: reads K's bone data and this file's storage; writes only t_draw and u (the caller's
// locals) and, through sim_pose_draw, this file's storage (as the renderer's own call would).
ENGINE_API int sim_pose_bone_drawn(IKinematics* K, u16 b, const Fmatrix& t_read, Fmatrix& t_draw, float& u)
{
	if (!K || !sim_pose_maybe_drawn(K))
		return 0; // not drawn between ticks in this frame, slice off, K unmarked or never captured: the caller's stock rule
	xrCriticalSectionGuard guard(&K->UCalc_Mutex); // recursive: the callers may hold it already
	if (!sim_pose_draw(K))
		return 0; // this frame draws K's live bones (Sn, Sb, Sg, Ss)
	const SimPoseState* s = SimPoseAccess::get(K);
	if (b >= s->n || !(s->vis & (u64(1) << b)))
		return 0; // BI_NONE, out of range, or hidden (LL_GetTransform reads mTransformHidden): not blended
	u = s->draw_u;
	int cls = 0;
	if (0 == memcmp(&t_read, &s->tcur[b], sizeof(Fmatrix)))
		cls |= SIM_POSE_BONE_CUR;
	if (0 == memcmp(&t_read, &s->tprev[b], sizeof(Fmatrix)))
		cls |= SIM_POSE_BONE_PREV;
	if (!cls)
		return SIM_POSE_BONE_OTHER;
	// the drawn bone -> model matrix: bitwise the captured one when the blend left the bone at an endpoint, else the
	// drawn bind -> model matrix times inverse(m2b) (mRenderTransform = mTransform * m2b, SkeletonRigid.cpp CLBone)
	if (0 == memcmp(&s->draw[b], &s->cur[b], sizeof(Fmatrix)))
		t_draw = s->tcur[b];
	else if (0 == memcmp(&s->draw[b], &s->prev[b], sizeof(Fmatrix)))
		t_draw = s->tprev[b];
	else
	{
		Fmatrix bind;
		bind.invert(K->LL_GetData(b).m2b_transform);
		t_draw.mul_43(s->draw[b], bind);
	}
	return cls;
}

// ---------------------------------------------------------------------------------------------------------------
// release

ENGINE_API void sim_pose_release(IKinematics* K)
{
	xrCriticalSectionGuard guard(&K->UCalc_Mutex); // recursive; a capture holds it too
	SimPoseState* s = SimPoseAccess::get(K);
	if (!s)
		return;
	SimPoseAccess::set(K, nullptr);
	xr_delete(s);
}

// ---------------------------------------------------------------------------------------------------------------
// test-only: switches and witness

// a whole switch (not a prefix of a longer one: -det_synth_pose vs -det_synth_pose_npc)
static bool sim_pose_has_switch(LPCSTR name)
{
	const size_t len = xr_strlen(name);
	for (LPCSTR p = strstr(Core.Params, name); p; p = strstr(p + 1, name))
	{
		const char c = p[len];
		if (c == 0 || c == ' ' || c == '\t' || c == '\r' || c == '\n')
			return true;
	}
	return false;
}

ENGINE_API void sim_pose_parse_switches()
{
	if (!xr_det_mode())
		return;
	g_sim_synth_pose = sim_pose_has_switch("-det_synth_pose") ? TRUE : FALSE;
	if (!g_sim_synth_pose)
		return;
	if (LPCSTR p = strstr(Core.Params, "-det_synth_pose_npc "))
	{
		string64 t = "";
		if (sscanf(p + xr_strlen("-det_synth_pose_npc "), "%63s", t) == 1 && t[0])
			xr_strcpy(s_wit_npc_name, t);
	}
	s_wit_npc = s_wit_npc_name;
	Msg("* det_synth: pose witness on, npc %s", s_wit_npc_name);
}

// W1 (test-only): once per unmarked skeleton per frame (at the mid 0 of its first leaf uploaded in the frame): nposed++
// when its live pose differs from the one stored at its upload in the PREVIOUS frame (consecutive frames only, so on a
// render-only frame the change happened after the last tick: posed between ticks). Then stores this frame's hash.
static void sim_pose_wit_unmarked_first(IKinematics* K)
{
	const u16 n = K->LL_BoneCount();
	const u32 fr = Device.dwFrame;
	SimPoseUnmarkedSlot* hit = nullptr;
	SimPoseUnmarkedSlot* old = &s_wit_uu[0];
	for (u32 i = 0; i < SIM_POSE_UU_SLOTS; ++i)
	{
		SimPoseUnmarkedSlot& e = s_wit_uu[i];
		if (e.k == K && e.n == n)
		{
			hit = &e;
			break;
		}
		if (e.frame < old->frame)
			old = &e; // the oldest (an unused slot has frame 0)
	}
	if (hit && hit->frame == fr)
		return; // another leaf or pass of K in this frame: counted once
	const u64 h = sim_pose_hash_live(K, n);
	if (hit && hit->frame + 1 == fr && hit->h != h)
		s_wit.uu_nposed++;
	SimPoseUnmarkedSlot& e = hit ? *hit : *old;
	e.k = K;
	e.n = n;
	e.frame = fr;
	e.h = h;
}

ENGINE_API void sim_pose_wit_upload(IKinematics* K, u32 mid, u32 count, const Fmatrix& M)
{
	if (!g_sim_synth_pose)
		return;
	SimPoseWitness& w = s_wit;
	const bool last = mid + 1 == count;
	// uu (W1): an unmarked skeleton (decided once per leaf, at its mid 0)
	if (mid == 0)
	{
		w.uu_k = nullptr;
		if (!K->LL_GetSimObjectVisual())
		{
			w.uu_k = K;
			w.uu_diff = false;
			sim_pose_wit_unmarked_first(K);
		}
	}
	if (w.uu_k == K)
	{
		// stock hands set_ca the live bone itself (LL_GetTransform_R = mRenderTransform, SkeletonCustom.h:309); any other
		// address (a draw array) counts, even when its bytes happen to be equal
		if (&M != &K->LL_GetBoneInstance(u16(mid)).mRenderTransform)
			w.uu_diff = true;
		if (last)
		{
			w.uu_nu++;
			if (w.uu_diff)
				w.uu_nud++;
			if (K->has_sim_pose())
				w.uu_nus++;
			w.uu_k = nullptr;
		}
	}
	if (mid == 0)
	{
		if (!w.up_done && !w.up_active)
		{
			w.up_active = true;
			w.up_k = K;
			w.up_hu = w.up_hl = sim_4d_hash_seed;
		}
		if (w.pw && w.pw_k == K && !w.pw_up_done && !w.pw_up_active)
		{
			w.pw_up_active = true;
			w.pw_up_count = count;
			w.pw_hu = sim_4d_hash_seed;
		}
	}
	if (w.up_active && w.up_k == K)
	{
		w.up_hu = sim_4d_hash(w.up_hu, &M, sizeof(Fmatrix));
		w.up_hl = sim_4d_hash(w.up_hl, &K->LL_GetBoneInstance(u16(mid)).mRenderTransform, sizeof(Fmatrix));
		if (last)
		{
			w.up_active = false;
			w.up_done = true;
		}
	}
	if (w.pw_up_active && w.pw_k == K)
	{
		w.pw_hu = sim_4d_hash(w.pw_hu, &M, sizeof(Fmatrix));
		if (last)
		{
			w.pw_up_active = false;
			w.pw_up_done = true;
			// the draw array over the same bones, if the uploaded frame had one (caller holds K's lock)
			SimPoseState* s = SimPoseAccess::get(K);
			if (s && s->form == SPF_LERP && s->draw_frame == Device.dwFrame && count <= s->n)
			{
				w.pw_hdu_ok = true;
				w.pw_hdu = sim_4d_hash(sim_4d_hash_seed, s->draw, count * sizeof(Fmatrix));
			}
		}
	}
}

ENGINE_API void sim_pose_witness_reset()
{
	s_wit = SimPoseWitness();
	s_wit_ro_diff = 0;
	s_wit_ro_equal = 0;
	s_wit_pa = false; // S4b
}

// S4b (test-only): see sim_pose.h
ENGINE_API u64 sim_pose_tw_bones(IKinematics* K)
{
	return sim_pose_tw_hash(K);
}

ENGINE_API const shared_str& sim_pose_wit_npc()
{
	return s_wit_npc;
}

ENGINE_API void sim_pose_wit_attach(const SimPoseAttachWit& w)
{
	if (!g_sim_synth_pose || s_wit_pa)
		return; // the first placement of the frame wins (later passes reuse the child's cached matrix anyway)
	s_wit_pa = true;
	s_wit_pa_v = w;
}

static void sim_pose_print_hash(FILE* f, bool ok, u64 h)
{
	if (ok)
		fprintf(f, " %016llx", (unsigned long long)h);
	else
		fprintf(f, " -");
}

static void sim_pose_print_vec(FILE* f, bool ok, const Fvector& v)
{
	if (ok)
		fprintf(f, " %.5f %.5f %.5f", v.x, v.y, v.z);
	else
		fprintf(f, " - - -");
}

static void sim_pose_print_pn(FILE* f);
static void sim_pose_print_pa(FILE* f);
static void sim_pose_print_pj(FILE* f);

// " p <neval> <nlerp> <nguard> <nsnap> <nro_diff> <nro_equal> | pw <form> <u> <ptick> <ctick> <hd> <hl> <hp> <hc> <hu>
// <hdu> <jd xyz> <jp xyz> <jc xyz> | up <hu> <hl> | pn <form> <u> <hd> <hl> <hsim> |
// pa <kind> <form> <cls> <ba> <bb> <u> <wd xyz> <ws xyz> <hd xyz> <hr xyz> <ld xyz> <ls xyz> <hx> <hxe> |
// uu <nu> <nud> <nus> <nposed> | pj <bone> <jw xyz> |"
// (one "-" per absent value; pa is S4b's; uu (W1) and pj (W2) are the s4d2 witness additions, always printed)
ENGINE_API void sim_pose_witness_print(FILE* f)
{
	if (!g_sim_synth_pose)
		return;
	const SimPoseWitness& w = s_wit;
	fprintf(f, " p %u %u %u %u %u %u |", w.neval, w.nlerp, w.nguard, w.nsnap, s_wit_ro_diff.load(), s_wit_ro_equal.load());

	if (!w.pw)
		fprintf(f, " pw - - - - - - - - - - - - - - - - - - - |");
	else
	{
		const bool L = w.pw_form == SPF_LERP;
		fprintf(f, " pw %s", sim_pose_form_name(w.pw_form));
		if (L)
			fprintf(f, " %.6f", w.pw_u);
		else
			fprintf(f, " -");
		if (w.pw_have)
			fprintf(f, " %u %u", w.pw_ptick, w.pw_ctick);
		else
			fprintf(f, " - -");
		sim_pose_print_hash(f, L, w.pw_hd);
		sim_pose_print_hash(f, true, w.pw_hl);
		sim_pose_print_hash(f, w.pw_have, w.pw_hp);
		sim_pose_print_hash(f, w.pw_have, w.pw_hc);
		sim_pose_print_hash(f, w.pw_up_done, w.pw_hu);
		sim_pose_print_hash(f, L && w.pw_hdu_ok, w.pw_hdu);
		sim_pose_print_vec(f, L, w.pw_jd);
		sim_pose_print_vec(f, L, w.pw_jp);
		sim_pose_print_vec(f, L, w.pw_jc);
		fprintf(f, " |");
	}

	if (w.up_done)
		fprintf(f, " up %016llx %016llx |", (unsigned long long)w.up_hu, (unsigned long long)w.up_hl);
	else
		fprintf(f, " up - - |");

	sim_pose_print_pn(f);
	sim_pose_print_pa(f);
	// uu (W1): always 4 counts (0 0 0 0 when no unmarked skeleton was uploaded)
	fprintf(f, " uu %u %u %u %u |", w.uu_nu, w.uu_nud, w.uu_nus, w.uu_nposed);
	sim_pose_print_pj(f);
}

// the witness NPC and its skeleton (main thread, after the render's join; looked up by name every frame, so no pointer
// is kept across frames). K is nullptr when the NPC is not found or has no skeleton.
static CObject* sim_pose_wit_npc_object(IKinematics*& K)
{
	K = nullptr;
	CObject* O = (g_pGameLevel && s_wit_npc.size()) ? g_pGameLevel->Objects.FindObjectByName(s_wit_npc) : nullptr;
	if (O)
		if (IRenderVisual* V = O->Visual())
			K = V->dcast_PKinematics();
	return O;
}

// pj (W2, test-only): the witness NPC's bip01_r_hand joint (else its last bone, as pw) as the mesh is drawn in this
// frame, in world: the NPC's drawn matrix of this frame (sim_render.ptr, the matrix renderable_Render handed over and
// its items' child rule used as parent_draw) times draw[b] applied to the bone's joint. " pj <bone> <x y z> |"; four
// dashes unless pn is form L and the NPC's drawn matrix was set in this frame.
static void sim_pose_print_pj(FILE* f)
{
	IKinematics* K = nullptr;
	CObject* O = sim_pose_wit_npc_object(K);
	if (K)
	{
		xrCriticalSectionGuard guard(&K->UCalc_Mutex);
		SimPoseState* s = SimPoseAccess::get(K);
		const CObject::SimRenderState& r = O->sim_render;
		if (s && s->n && s->draw_frame == Device.dwFrame && s->form == SPF_LERP && r.ptr && r.frame == Device.dwFrame)
		{
			if (s->wit_bone == u16(-2))
				s->wit_bone = K->LL_BoneID("bip01_r_hand");
			const u16 b = (s->wit_bone < s->n) ? s->wit_bone : u16(s->n - 1);
			Fvector jm, jw;
			s->draw[b].transform_tiny(jm, s->joint[b]);
			r.ptr->transform_tiny(jw, jm);
			fprintf(f, " pj %u %.5f %.5f %.5f |", u32(b), jw.x, jw.y, jw.z);
			return;
		}
	}
	fprintf(f, " pj - - - - |");
}

static void sim_pose_print_pn(FILE* f)
{
	// pn: the named NPC (reported only).
	IKinematics* K = nullptr;
	sim_pose_wit_npc_object(K);
	if (!K)
	{
		fprintf(f, " pn - - - - - |");
		return;
	}
	xrCriticalSectionGuard guard(&K->UCalc_Mutex);
	const SimPoseState* s = SimPoseAccess::get(K);
	const bool evaluated = s && s->draw_frame == Device.dwFrame;
	const bool L = evaluated && s->form == SPF_LERP;
	fprintf(f, " pn %s", evaluated ? sim_pose_form_name(s->form) : "X"); // X = not drawn between ticks this frame
	if (L)
		fprintf(f, " %.6f", s->draw_u);
	else
		fprintf(f, " -");
	sim_pose_print_hash(f, L, L ? sim_4d_hash(sim_4d_hash_seed, s->draw, s->n * sizeof(Fmatrix)) : 0);
	const u16 n = K->LL_BoneCount();
	sim_pose_print_hash(f, true, sim_pose_hash_live(K, n));
	u64 hsim = sim_4d_hash_seed;
	for (u16 b = 0; b < n; ++b)
		hsim = sim_4d_hash(hsim, &K->LL_GetBoneInstance(b).mTransform, sizeof(Fmatrix));
	sim_pose_print_hash(f, true, hsim);
	fprintf(f, " |");
}

static void sim_pose_print_u16(FILE* f, u16 v)
{
	if (v == BI_NONE)
		fprintf(f, " -");
	else
		fprintf(f, " %u", u32(v));
}

// pa (S4b): the witness NPC's active item as placed by the child rule in this frame; 26 values after "pa"
static void sim_pose_print_pa(FILE* f)
{
	if (!s_wit_pa)
	{
		fprintf(f, " pa - - - - - - - - - - - - - - - - - - - - - - - - - - |");
		return;
	}
	const SimPoseAttachWit& a = s_wit_pa_v;
	fprintf(f, " pa %c %c %c", a.kind, a.form, a.cls);
	sim_pose_print_u16(f, a.ba);
	sim_pose_print_u16(f, a.bb);
	if (a.have_u)
		fprintf(f, " %.6f", a.u);
	else
		fprintf(f, " -");
	sim_pose_print_vec(f, true, a.wd);
	sim_pose_print_vec(f, true, a.ws);
	sim_pose_print_vec(f, a.have_u, a.hd);
	sim_pose_print_vec(f, a.have_u, a.hr);
	sim_pose_print_vec(f, a.have_l, a.ld);
	sim_pose_print_vec(f, a.have_l, a.ls);
	sim_pose_print_hash(f, true, a.hx);
	// hxe: the child's XFORM() again, after the render's join (main thread; looked up by ID, no pointer kept)
	CObject* O = g_pGameLevel ? g_pGameLevel->Objects.net_Find(a.child_id) : nullptr;
	sim_pose_print_hash(f, O != nullptr, O ? sim_4d_hash(sim_4d_hash_seed, &O->XFORM(), sizeof(Fmatrix)) : 0);
	fprintf(f, " |");
}
