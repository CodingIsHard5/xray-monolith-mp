#pragma once
// GammaVR: hooks between the level render loop (xrEngine) and the OpenXR code in the DX11 renderer (vr_openxr.cpp).
// Both pointers stay null unless the DX11 renderer is linked; the functions do nothing without -vr_openxr.
struct vr_eye_view
{
	Fvector offset;                    // eye position relative to the head, engine axes (x right, y up, z forward), metres
	float tan_l, tan_r, tan_u, tan_d;  // tangents of the runtime's field-of-view angles (left and down are negative)
};
// Milestone 3: a pose in the tracking space (OpenXR LOCAL space), converted to engine axes (x right, y up, z forward).
// Conversion from OpenXR (x right, y up, -z forward): position (x, y, -z), quaternion (-x, -y, z, w).
// The quaternion rotates engine-axis vectors as v' = q v q* (Hamilton product).
struct vr_pose
{
	Fvector pos;             // metres
	float qx, qy, qz, qw;    // unit quaternion, engine axes
	bool valid;              // position and orientation both valid
};

// rotate v by unit quaternion q
inline void vr_quat_rotate(float qx, float qy, float qz, float qw, const Fvector& v, Fvector& out)
{
	// t = 2 (u x v); v' = v + w t + u x t
	const float tx = 2.f * (qy * v.z - qz * v.y), ty = 2.f * (qz * v.x - qx * v.z), tz = 2.f * (qx * v.y - qy * v.x);
	out.x = v.x + qw * tx + (qy * tz - qz * ty);
	out.y = v.y + qw * ty + (qz * tx - qx * tz);
	out.z = v.z + qw * tz + (qx * ty - qy * tx);
}

// pose -> engine matrix (rows i, j, k = the rotated x, y, z axes; c = position): transform_tiny maps pose-local to tracking space
inline void vr_pose_matrix(const vr_pose& p, Fmatrix& m)
{
	Fvector ex, ey, ez, i, j, k;
	ex.set(1.f, 0.f, 0.f);
	ey.set(0.f, 1.f, 0.f);
	ez.set(0.f, 0.f, 1.f);
	vr_quat_rotate(p.qx, p.qy, p.qz, p.qw, ex, i);
	vr_quat_rotate(p.qx, p.qy, p.qz, p.qw, ey, j);
	vr_quat_rotate(p.qx, p.qy, p.qz, p.qw, ez, k);
	m.set(i, j, k, p.pos);
}

// rotation part of an orthonormal engine matrix (rows i, j, k as above) -> unit quaternion (inverse of vr_pose_matrix)
inline void vr_matrix_quat(const Fvector& i, const Fvector& j, const Fvector& k, float& qx, float& qy, float& qz, float& qw)
{
	// column-vector rotation matrix R has columns i, j, k: R[row][col]
	const float r00 = i.x, r10 = i.y, r20 = i.z;
	const float r01 = j.x, r11 = j.y, r21 = j.z;
	const float r02 = k.x, r12 = k.y, r22 = k.z;
	const float tr = r00 + r11 + r22;
	if (tr > 0.f)
	{
		const float s = 2.f * _sqrt(tr + 1.f);
		qw = 0.25f * s; qx = (r21 - r12) / s; qy = (r02 - r20) / s; qz = (r10 - r01) / s;
	}
	else if (r00 > r11 && r00 > r22)
	{
		const float s = 2.f * _sqrt(1.f + r00 - r11 - r22);
		qw = (r21 - r12) / s; qx = 0.25f * s; qy = (r01 + r10) / s; qz = (r02 + r20) / s;
	}
	else if (r11 > r22)
	{
		const float s = 2.f * _sqrt(1.f + r11 - r00 - r22);
		qw = (r02 - r20) / s; qx = (r01 + r10) / s; qy = 0.25f * s; qz = (r12 + r21) / s;
	}
	else
	{
		const float s = 2.f * _sqrt(1.f + r22 - r00 - r11);
		qw = (r10 - r01) / s; qx = (r02 + r20) / s; qy = (r12 + r21) / s; qz = 0.25f * s;
	}
	if (qw < 0.f) { qx = -qx; qy = -qy; qz = -qz; qw = -qw; } // one sign for the logs
}

struct vr_frame_info
{
	vr_pose head;   // runtime head pose (VIEW located in LOCAL), engine axes
	u32 frame;      // stereo frame number: the runtime's consecutive good-frame counter, the same one -vr_dump uses
};

// Starts the OpenXR frame and fills views[2] and *info. False = no VR frame this engine frame (render the normal way).
extern ENGINE_API bool (*g_vr_begin_frame)(vr_eye_view* views, vr_frame_info* info);

// Milestone 3: grip pose of the chosen controller (-vr_gun_hand left|right, default right), located at the frame's
// predicted display time in the tracking space. 'gun' is the pose the held weapon uses: the runtime grip pose, or with
// -vr_gun_on_head the head pose plus a fixed offset (deliberately broken test mode). False = not available.
struct vr_grip_info
{
	vr_pose runtime;
	vr_pose gun;
	char hand;      // 'L' or 'R'
	// milestone 3d (-vr_dbg_grip): the CURRENT frame's grip location call, for the per-frame grip log
	long long t_ns;   // XrTime passed to xrLocateSpace for this grip (the frame's predictedDisplayTime)
	float xr_pos[3];  // raw pose from the runtime: grip space in LOCAL space, OpenXR axes, no conversion
	float xr_quat[4]; // x, y, z, w
	int lag;          // -vr_grip_lag: the gun pose is the converted grip pose of this many VR frames ago (0 = current)
};
extern ENGINE_API bool (*g_vr_get_grip)(vr_grip_info* out);

// Milestone 3: the mapping from the tracking space (engine axes) to the world, shared by the camera and the gun.
// track_to_world = game camera of this frame * inverse(head pose of the first VR frame). Set by the level render loop
// before the eyes are rendered, active only while the eyes of a VR frame are being rendered.
struct vr_track_state
{
	bool active;
	u32 frame;               // vr_frame_info::frame of this frame
	Fmatrix track_to_world;
};
extern ENGINE_API vr_track_state g_vr_track;

// -vr_nogun: the weapon HUD model (with the arms) is not drawn in VR frames
inline bool vr_nogun()
{
	static const bool v = !!strstr(Core.Params, "-vr_nogun");
	return v;
}
// Called after the scene has been rendered for one eye: copies the finished picture into that eye's swapchain image.
extern ENGINE_API void (*g_vr_eye_done)(int eye);
// Test switch -vr_mono_eye <0|1>: render ONE ordinary mono pass per frame from that eye's camera and give that
// picture to both eye images ("mono twin" check). -1 = off.
extern ENGINE_API int g_vr_mono_eye;

// Test switch -vr_plain ("plain" content control): every pass that blends with a previous-frame picture or draws random
// values per call is skipped, in both eyes, so two passes with the same camera must give the same picture.
inline bool vr_plain()
{
	static const bool v = !!strstr(Core.Params, "-vr_plain");
	return v;
}

// "VR preset" (Overseer ruling 2026-11-19): full-screen effects that are a comfort problem in a headset are off
// (film grain, motion blur, volumetric blur). On with -vr_openxr, or with -vr_preset for the flat benchmark.
inline bool vr_preset()
{
	static const bool v = strstr(Core.Params, "-vr_openxr") || strstr(Core.Params, "-vr_preset");
	return v;
}

// Diagnostic switches for milestone 2b attempt 2: each turns ONE of its four changes back off.
// -vr_dbg_noseed (a), -vr_dbg_lat (b), -vr_dbg_sky (c), -vr_dbg_prev (d). Not for normal use.
inline bool vr_dbg(const char* sw)
{
	return !!strstr(Core.Params, sw);
}

// Milestone 3 paths (head pose drives the camera, per-eye HUD view, gun at the controller) are OFF unless -vr_m3
// is given, so builds that carry them still render milestone 2 tests exactly as before.
inline bool vr_m3()
{
	static const bool v = !!strstr(Core.Params, "-vr_m3");
	return v;
}

// Milestone 3s (Amendment 13): the VR BASE the tracking frame hangs on, instead of the game camera after effects.
// Filled by xrGame (CActor::cam_Update) each frame when the VR m3 path is configured (-vr_m3 and -vr_openxr):
// pos = the actor's eye position before the camera-effector loop, with crouch-height smoothing, stair smoothing and the
// IK height shift kept, and with lean (lookout pivot and roll) and the wall push-out (collide_camera) left out.
// aim_heading/aim_pitch = the actor's first-eye camera direction this frame (heading = atan2(x, z), pitch = asin(y),
// radians, engine axes x right, y up, z forward), used ONLY to initialise 'yaw' once and for logging.
// yaw = the VR-owned facing (heading as above): set once at the first VR frame from aim_heading, afterwards only by
// vr_base_set_yaw() (deliberate turning / teleports), never again from the actor camera.
struct vr_base_state
{
	bool valid;        // pos/aim filled this engine frame (dev_frame == Device.dwFrame)
	u32 dev_frame;
	Fvector pos;
	float aim_heading, aim_pitch;
	bool yaw_set;
	float yaw;
	Fvector cam_point; // diagnostic: the flat first-eye camera point BEFORE the wall push-out (collide_camera), lean included
};
extern ENGINE_API vr_base_state g_vr_base;
// -vr_dbg_base_flat: the -vr_dbg_base line is logged from the flat frame (flat run S3); logging only
inline bool vr_dbg_base_flat()
{
	static const bool v = !!strstr(Core.Params, "-vr_dbg_base_flat");
	return v;
}
inline void vr_base_set_yaw(float heading)
{
	g_vr_base.yaw = heading;
	g_vr_base.yaw_set = true;
}
// the VR m3 path is configured for this run (flat runs never take any branch guarded by this)
inline bool vr_m3_vr()
{
	static const bool v = vr_m3() && !!strstr(Core.Params, "-vr_openxr");
	return v;
}
// -vr_base_old: control mode, the tracking frame hangs on the game camera after effects (milestone 3 behaviour)
inline bool vr_base_old()
{
	static const bool v = !!strstr(Core.Params, "-vr_base_old");
	return v;
}
