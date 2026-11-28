#include "stdafx.h"

// GammaVR Phase B, milestone 1: OpenXR bring-up (DX11 only).
// Enabled by the command line switch -vr_openxr. Without it nothing here runs and openxr_loader.dll is never loaded.
// What it does: creates an OpenXR instance, system and D3D11 session, one swapchain per eye, and every engine frame
// runs xrWaitFrame/xrBeginFrame/xrEndFrame with one projection layer of two views, logging the head pose.
// Milestone 2: the level render loop (IGame_Level::OnRender) asks vr_begin_frame() for the two eye views, renders the
// scene once per eye and calls vr_eye_done(), which copies the finished back buffer into that eye's swapchain image.
// -vr_eye_scale <f> scales the eye offset (test control: 0 = both eyes at the head). -vr_dump reads both swapchain
// images back on stereo frames 200 and 300 and writes them to the logs folder.
// The loader is bound at run time (LoadLibrary), so the engine build needs only the OpenXR headers.
#ifdef USE_DX11

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include "../../3rd party/openxr/include/openxr/openxr.h"
#include "../../3rd party/openxr/include/openxr/openxr_platform.h"
#include "../../xrEngine/vr_hooks.h"

namespace
{
	struct vr_eye
	{
		XrSwapchain chain = XR_NULL_HANDLE;
		int32_t w = 0, h = 0;
		xr_vector<ID3D11Texture2D*> tex;
	};

	struct vr_state
	{
		bool tried = false;
		bool ok = false;
		bool running = false;
		XrInstance instance = XR_NULL_HANDLE;
		XrSystemId system = XR_NULL_SYSTEM_ID;
		XrSession session = XR_NULL_HANDLE;
		XrSpace local = XR_NULL_HANDLE;
		XrSpace head = XR_NULL_HANDLE;
		XrSessionState state = XR_SESSION_STATE_UNKNOWN;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		vr_eye eye[2];
		u32 frames_ok = 0;       // xrEndFrame calls that returned XR_SUCCESS with the projection layer
		u32 frames_run = 0;      // consecutive such calls
		u32 frames_fail = 0;
		// per-frame state
		bool frame_begun = false;
		u32 eyes_done = 0;
		XrFrameState fs = { XR_TYPE_FRAME_STATE };
		XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
		XrSpaceLocation head_loc = { XR_TYPE_SPACE_LOCATION };
		XrResult head_r = XR_SUCCESS;
		float eye_scale = 1.f;
		bool dump = false;
		u32 dump_at[8] = {};     // -vr_dump_at "f1 f2 ...": further VR frames whose eye pictures and masks are written (with -vr_dump)
		int dump_at_n = 0;
		bool views_logged = false;

		// milestone 3: controller grip poses (action set with one pose action, subaction paths left/right)
		bool actions_ok = false;
		XrActionSet action_set = XR_NULL_HANDLE;
		XrAction grip_action = XR_NULL_HANDLE;
		XrPath hand_path[2] = { XR_NULL_PATH, XR_NULL_PATH }; // 0 = left, 1 = right
		XrSpace grip_space[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
		// H1 (Amendment 42): the AIM pose (where the controller points); the gun's direction comes from it
		XrAction aim_action = XR_NULL_HANDLE;
		XrSpace aim_space[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
		vr_pose aim[2];           // located this frame, engine axes, LOCAL space
		bool aim_old = false;     // -vr_aim_old: the gun's direction is the grip pose's, as before (control)
		vr_pose grip[2];          // located this frame, engine axes, LOCAL space
		vr_pose head_e;           // head pose this frame, engine axes, LOCAL space
		bool grip_located = false;
		int gun_hand = 1;         // -vr_gun_hand left|right
		bool gun_on_head = false; // -vr_gun_on_head
		bool grip_rot = false;    // -vr_grip_rot <yaw> <pitch> (milestone 3b, criterion 7e: engine-injected rotation)
		float grip_rot_q[4] = { 0.f, 0.f, 0.f, 1.f }; // q_inject x, y, z, w (engine axes)
		// milestone 3d: raw grip locations of this frame, and a ring of converted grip poses for -vr_grip_lag
		XrPosef grip_raw[2];
		XrTime grip_time = 0;
		int grip_lag = 0;                // -vr_grip_lag <n>, 0..8
		static const int GRIP_RING = 9;
		vr_pose grip_ring[GRIP_RING][2]; // converted poses, newest at grip_ring_head
		int grip_ring_head = -1, grip_ring_count = 0;
		bool period_logged = false;
		bool profiles_logged = false;
		// milestone 6 (Amendment 19): button actions, read once per VR frame after xrSyncActions
		XrAction fire_action = XR_NULL_HANDLE;   // float, both hands
		XrAction grab_action = XR_NULL_HANDLE;   // float, both hands
		bool ext_touch_plus = false;             // XR_META_touch_controller_plus offered and enabled
		bool ext_frame = false;                  // XR_VALVE_frame_controller_interaction offered and enabled
		bool input_read = false;                 // at least one read so far
		u32 input_frame = 0, input_seq = 0;
		float in_trigger[2] = { 0.f, 0.f }, in_grab[2] = { 0.f, 0.f };
		bool in_trigger_active[2] = { false, false }, in_grab_active[2] = { false, false };
		// milestone 7 (Amendment 20): thumbstick (vector2f, both hands) and the poses of the same read
		XrAction stick_action = XR_NULL_HANDLE;
		float in_stick[2][2] = { { 0.f, 0.f }, { 0.f, 0.f } };
		bool in_stick_active[2] = { false, false };
		vr_pose in_grip[2];
		vr_pose in_head;
		// milestone 7b (Amendment 23): "next_weapon" (boolean, both hands) and the head's height above the STAGE floor
		XrAction next_action = XR_NULL_HANDLE;
		bool in_next[2] = { false, false };
		bool in_next_active[2] = { false, false };
		XrAction crouch_action = XR_NULL_HANDLE;    // "crouch_toggle" (boolean, both hands), no default binding
		bool in_crouch[2] = { false, false };
		bool in_crouch_active[2] = { false, false };
		XrSpace stage = XR_NULL_HANDLE;          // STAGE space, used only to measure the head's height above the floor
		float in_floor_y = 0.f;
		bool in_floor_valid = false;
		bool profile_seen[2] = { false, false }; // current interaction profile of the hand logged at least once
		XrPath profile_cur[2] = { XR_NULL_PATH, XR_NULL_PATH };

		PFN_xrGetInstanceProcAddr GetInstanceProcAddr = nullptr;
		PFN_xrCreateInstance CreateInstance = nullptr;
		PFN_xrGetInstanceProperties GetInstanceProperties = nullptr;
		PFN_xrGetSystem GetSystem = nullptr;
		PFN_xrGetSystemProperties GetSystemProperties = nullptr;
		PFN_xrGetD3D11GraphicsRequirementsKHR GetD3D11GraphicsRequirementsKHR = nullptr;
		PFN_xrCreateSession CreateSession = nullptr;
		PFN_xrCreateReferenceSpace CreateReferenceSpace = nullptr;
		PFN_xrEnumerateViewConfigurationViews EnumerateViewConfigurationViews = nullptr;
		PFN_xrEnumerateSwapchainFormats EnumerateSwapchainFormats = nullptr;
		PFN_xrCreateSwapchain CreateSwapchain = nullptr;
		PFN_xrEnumerateSwapchainImages EnumerateSwapchainImages = nullptr;
		PFN_xrPollEvent PollEvent = nullptr;
		PFN_xrBeginSession BeginSession = nullptr;
		PFN_xrEndSession EndSession = nullptr;
		PFN_xrWaitFrame WaitFrame = nullptr;
		PFN_xrBeginFrame BeginFrame = nullptr;
		PFN_xrEndFrame EndFrame = nullptr;
		PFN_xrLocateViews LocateViews = nullptr;
		PFN_xrLocateSpace LocateSpace = nullptr;
		PFN_xrAcquireSwapchainImage AcquireSwapchainImage = nullptr;
		PFN_xrWaitSwapchainImage WaitSwapchainImage = nullptr;
		PFN_xrReleaseSwapchainImage ReleaseSwapchainImage = nullptr;
		PFN_xrStringToPath StringToPath = nullptr;
		PFN_xrPathToString PathToString = nullptr;
		PFN_xrCreateActionSet CreateActionSet = nullptr;
		PFN_xrCreateAction CreateAction = nullptr;
		PFN_xrSuggestInteractionProfileBindings SuggestInteractionProfileBindings = nullptr;
		PFN_xrAttachSessionActionSets AttachSessionActionSets = nullptr;
		PFN_xrCreateActionSpace CreateActionSpace = nullptr;
		PFN_xrSyncActions SyncActions = nullptr;
		PFN_xrGetCurrentInteractionProfile GetCurrentInteractionProfile = nullptr;
		PFN_xrGetActionStateFloat GetActionStateFloat = nullptr;
		PFN_xrGetActionStateVector2f GetActionStateVector2f = nullptr;
		PFN_xrGetActionStateBoolean GetActionStateBoolean = nullptr;     // milestone 7b, optional (null = no next_weapon)
		PFN_xrEnumerateReferenceSpaces EnumerateReferenceSpaces = nullptr; // milestone 7b, optional (null = no STAGE)
	};

	vr_state vr;

	bool vr_check(XrResult r, LPCSTR what)
	{
		if (XR_SUCCEEDED(r))
			return true;
		Msg("! [vr] %s failed: XrResult %d", what, (int)r);
		return false;
	}

	template <typename T>
	bool vr_bind(LPCSTR name, T& fn)
	{
		XrResult r = vr.GetInstanceProcAddr(vr.instance, name, (PFN_xrVoidFunction*)&fn);
		if (XR_FAILED(r) || !fn)
		{
			Msg("! [vr] xrGetInstanceProcAddr(%s) failed: XrResult %d", name, (int)r);
			return false;
		}
		return true;
	}

	// milestone 3: one action set with a grip pose action for both hands; suggested bindings for the KHR simple
	// controller and the Microsoft motion controller (the profiles of Monado's simulated "simple" and "wmr" controllers).
	// Must run after xrCreateSession and before the first xrSyncActions. False = no controller poses (VR still runs).
	bool vr_init_actions()
	{
		if (!vr_check(vr.StringToPath(vr.instance, "/user/hand/left", &vr.hand_path[0]), "xrStringToPath(left)") ||
			!vr_check(vr.StringToPath(vr.instance, "/user/hand/right", &vr.hand_path[1]), "xrStringToPath(right)"))
			return false;

		XrActionSetCreateInfo asi = { XR_TYPE_ACTION_SET_CREATE_INFO };
		xr_strcpy(asi.actionSetName, "gammavr");
		xr_strcpy(asi.localizedActionSetName, "GammaVR");
		asi.priority = 0;
		if (!vr_check(vr.CreateActionSet(vr.instance, &asi, &vr.action_set), "xrCreateActionSet"))
			return false;

		XrActionCreateInfo aci = { XR_TYPE_ACTION_CREATE_INFO };
		xr_strcpy(aci.actionName, "grip_pose");
		xr_strcpy(aci.localizedActionName, "Grip pose");
		aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
		aci.countSubactionPaths = 2;
		aci.subactionPaths = vr.hand_path;
		if (!vr_check(vr.CreateAction(vr.action_set, &aci, &vr.grip_action), "xrCreateAction(grip_pose)"))
			return false;
		xr_strcpy(aci.actionName, "aim_pose");
		xr_strcpy(aci.localizedActionName, "Aim pose");
		if (!vr_check(vr.CreateAction(vr.action_set, &aci, &vr.aim_action), "xrCreateAction(aim_pose)"))
			return false;

		// milestone 6 (Amendment 19): button actions "fire" and "grab" (float) for both hands. A failure here leaves the
		// action null (no button input, the grip pose still works).
		XrActionCreateInfo fci = { XR_TYPE_ACTION_CREATE_INFO };
		xr_strcpy(fci.actionName, "fire");
		xr_strcpy(fci.localizedActionName, "Fire");
		fci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
		fci.countSubactionPaths = 2;
		fci.subactionPaths = vr.hand_path;
		if (!vr_check(vr.CreateAction(vr.action_set, &fci, &vr.fire_action), "xrCreateAction(fire)"))
			vr.fire_action = XR_NULL_HANDLE;
		XrActionCreateInfo gci = { XR_TYPE_ACTION_CREATE_INFO };
		xr_strcpy(gci.actionName, "grab");
		xr_strcpy(gci.localizedActionName, "Grab");
		gci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
		gci.countSubactionPaths = 2;
		gci.subactionPaths = vr.hand_path;
		if (!vr_check(vr.CreateAction(vr.action_set, &gci, &vr.grab_action), "xrCreateAction(grab)"))
			vr.grab_action = XR_NULL_HANDLE;
		// milestone 7 (Amendment 20): thumbstick "stick" (vector2f) for both hands; a failure leaves it null (no stick input)
		XrActionCreateInfo tci = { XR_TYPE_ACTION_CREATE_INFO };
		xr_strcpy(tci.actionName, "stick");
		xr_strcpy(tci.localizedActionName, "Stick");
		tci.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
		tci.countSubactionPaths = 2;
		tci.subactionPaths = vr.hand_path;
		if (!vr_check(vr.CreateAction(vr.action_set, &tci, &vr.stick_action), "xrCreateAction(stick)"))
			vr.stick_action = XR_NULL_HANDLE;
		// milestone 7b (Amendment 23): "next_weapon" (boolean) for both hands; a failure (or no xrGetActionStateBoolean)
		// leaves it null (no weapon switch input, everything else unchanged)
		if (vr.GetActionStateBoolean)
		{
			XrActionCreateInfo nci = { XR_TYPE_ACTION_CREATE_INFO };
			xr_strcpy(nci.actionName, "next_weapon");
			xr_strcpy(nci.localizedActionName, "Next firearm");
			nci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			nci.countSubactionPaths = 2;
			nci.subactionPaths = vr.hand_path;
			if (!vr_check(vr.CreateAction(vr.action_set, &nci, &vr.next_action), "xrCreateAction(next_weapon)"))
				vr.next_action = XR_NULL_HANDLE;
			// "crouch_toggle" (boolean), opt-in: NO suggested binding in any profile (an unbound action only reads as
			// inactive); test switch -vr_dbg_crouch_bind adds left menu click on the WMR profile (below)
			XrActionCreateInfo cci = { XR_TYPE_ACTION_CREATE_INFO };
			xr_strcpy(cci.actionName, "crouch_toggle");
			xr_strcpy(cci.localizedActionName, "Crouch toggle");
			cci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			cci.countSubactionPaths = 2;
			cci.subactionPaths = vr.hand_path;
			if (!vr_check(vr.CreateAction(vr.action_set, &cci, &vr.crouch_action), "xrCreateAction(crouch_toggle)"))
				vr.crouch_action = XR_NULL_HANDLE;
		}
		const bool crouch_test_bind = !!strstr(Core.Params, "-vr_dbg_crouch_bind");
		Msg(crouch_test_bind ? "* [vr3] crouch button: test binding left menu" : "* [vr3] crouch button: unbound (opt-in)");

		// One xrSuggestInteractionProfileBindings call per profile: the grip pose (same path as before) plus fire/grab.
		// A refused suggestion is logged and skipped. ext: 0 = core profile, 1 = needs XR_META_touch_controller_plus,
		// 2 = needs XR_VALVE_frame_controller_interaction (suggested only when that extension was enabled).
		struct vr_profile_def
		{
			const char* path;
			const char* fire; // input path below /user/hand/<side>, null = no fire binding
			const char* grab; // null = no grab binding
			const char* stick; // null = no thumbstick binding (milestone 7)
			int ext;
			const char* next[2]; // milestone 7b: next_weapon per hand (0 = left, 1 = right), null = no binding
		};
		// milestone 7b: next_weapon = the upper face button (Touch / Touch Plus: Y left, B right; Index: B; WMR: menu). The
		// Frame controller gets none: its button paths are not confirmed (an unknown path would refuse the whole profile).
		static const vr_profile_def profiles[] = {
			{ "/interaction_profiles/khr/simple_controller", "/input/select/click", nullptr, nullptr, 0, { nullptr, nullptr } },
			{ "/interaction_profiles/microsoft/motion_controller", "/input/trigger/value", "/input/squeeze/click", "/input/thumbstick", 0, { "/input/menu/click", "/input/menu/click" } },
			{ "/interaction_profiles/oculus/touch_controller", "/input/trigger/value", "/input/squeeze/value", "/input/thumbstick", 0, { "/input/y/click", "/input/b/click" } },
			{ "/interaction_profiles/valve/index_controller", "/input/trigger/value", "/input/squeeze/value", "/input/thumbstick", 0, { "/input/b/click", "/input/b/click" } },
			{ "/interaction_profiles/meta/touch_controller_plus", "/input/trigger/value", "/input/squeeze/value", "/input/thumbstick", 1, { "/input/y/click", "/input/b/click" } },
			{ "/interaction_profiles/valve/frame_controller_valve", "/input/trigger/value", "/input/squeeze/value", "/input/thumbstick", 2, { nullptr, nullptr } },
		};
		const char* side[2] = { "/user/hand/left", "/user/hand/right" };
		u32 suggested = 0;
		for (const vr_profile_def& P : profiles)
		{
			if ((P.ext == 1 && !vr.ext_touch_plus) || (P.ext == 2 && !vr.ext_frame))
			{
				Msg("* [vr3] bindings %s: skipped (extension not offered)", P.path);
				continue;
			}
			XrPath prof = XR_NULL_PATH;
			if (!vr_check(vr.StringToPath(vr.instance, P.path, &prof), "xrStringToPath(profile)"))
				continue;
			XrActionSuggestedBinding b[14]; // 2 hands x (grip pose, fire, grab, stick, next_weapon, crouch_toggle, aim pose)
			u32 nb = 0;
			bool path_ok = true;
			for (int h = 0; h < 2 && path_ok; ++h)
			{
				// crouch_toggle: only the test binding (-vr_dbg_crouch_bind: WMR left menu click), never by default
				const char* crouch = (crouch_test_bind && h == 0 &&
					!xr_strcmp(P.path, "/interaction_profiles/microsoft/motion_controller")) ? "/input/menu/click" : nullptr;
				const char* what[7] = { "/input/grip/pose", P.fire, P.grab, P.stick, P.next[h], crouch, "/input/aim/pose" };
				const XrAction act[7] = { vr.grip_action, vr.fire_action, vr.grab_action, vr.stick_action, vr.next_action, vr.crouch_action, vr.aim_action };
				for (int k = 0; k < 7; ++k)
				{
					if (!what[k] || act[k] == XR_NULL_HANDLE)
						continue;
					string256 full;
					xr_sprintf(full, "%s%s", side[h], what[k]);
					XrPath bp = XR_NULL_PATH;
					if (!vr_check(vr.StringToPath(vr.instance, full, &bp), "xrStringToPath(binding)"))
					{
						path_ok = false;
						break;
					}
					b[nb].action = act[k];
					b[nb].binding = bp;
					++nb;
				}
			}
			if (!path_ok)
				continue;
			XrInteractionProfileSuggestedBinding sb = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
			sb.interactionProfile = prof;
			sb.countSuggestedBindings = nb;
			sb.suggestedBindings = b;
			const XrResult r = vr.SuggestInteractionProfileBindings(vr.instance, &sb);
			if (XR_SUCCEEDED(r))
			{
				Msg("* [vr3] bindings %s: accepted", P.path);
				suggested++;
			}
			else
				Msg("* [vr3] bindings %s: refused (%d)", P.path, (int)r);
		}
		if (!suggested)
			return false;

		XrSessionActionSetsAttachInfo ati = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
		ati.countActionSets = 1;
		ati.actionSets = &vr.action_set;
		if (!vr_check(vr.AttachSessionActionSets(vr.session, &ati), "xrAttachSessionActionSets"))
			return false;

		for (int h = 0; h < 2; ++h)
		{
			XrActionSpaceCreateInfo sci = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
			sci.action = vr.grip_action;
			sci.subactionPath = vr.hand_path[h];
			sci.poseInActionSpace.orientation.w = 1.f;
			if (!vr_check(vr.CreateActionSpace(vr.session, &sci, &vr.grip_space[h]), "xrCreateActionSpace(grip)"))
				return false;
			sci.action = vr.aim_action;
			if (!vr_check(vr.CreateActionSpace(vr.session, &sci, &vr.aim_space[h]), "xrCreateActionSpace(aim)"))
				return false;
		}
		return true;
	}

	bool vr_init()
	{
		HMODULE loader = LoadLibraryA("openxr_loader.dll");
		if (!loader)
		{
			Msg("! [vr] openxr_loader.dll not loaded (error %u)", GetLastError());
			return false;
		}
		vr.GetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)GetProcAddress(loader, "xrGetInstanceProcAddr");
		vr.CreateInstance = (PFN_xrCreateInstance)GetProcAddress(loader, "xrCreateInstance");
		if (!vr.GetInstanceProcAddr || !vr.CreateInstance)
		{
			Msg("! [vr] openxr_loader.dll has no xrGetInstanceProcAddr/xrCreateInstance");
			return false;
		}

		// milestone 6: the controller-profile extensions are enabled only when the runtime offers them
		static const char* EXT_TOUCH_PLUS = "XR_META_touch_controller_plus";
		static const char* EXT_FRAME = "XR_VALVE_frame_controller_interaction";
		PFN_xrEnumerateInstanceExtensionProperties EnumExt = nullptr;
		if (XR_FAILED(vr.GetInstanceProcAddr(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", (PFN_xrVoidFunction*)&EnumExt)) || !EnumExt)
			EnumExt = (PFN_xrEnumerateInstanceExtensionProperties)GetProcAddress(loader, "xrEnumerateInstanceExtensionProperties");
		if (EnumExt)
		{
			uint32_t ne = 0;
			if (XR_SUCCEEDED(EnumExt(nullptr, 0, &ne, nullptr)) && ne)
			{
				xr_vector<XrExtensionProperties> ep(ne);
				for (uint32_t i = 0; i < ne; ++i)
				{
					ep[i].type = XR_TYPE_EXTENSION_PROPERTIES;
					ep[i].next = nullptr;
				}
				if (XR_SUCCEEDED(EnumExt(nullptr, ne, &ne, ep.data())))
					for (uint32_t i = 0; i < ne && i < ep.size(); ++i)
					{
						if (!xr_strcmp(ep[i].extensionName, EXT_TOUCH_PLUS))
							vr.ext_touch_plus = true;
						else if (!xr_strcmp(ep[i].extensionName, EXT_FRAME))
							vr.ext_frame = true;
					}
			}
		}
		else
			Msg("! [vr3] xrEnumerateInstanceExtensionProperties not found; optional controller extensions not enabled");
		Msg("* [vr3] extensions offered: %s %d, %s %d", EXT_TOUCH_PLUS, vr.ext_touch_plus ? 1 : 0, EXT_FRAME, vr.ext_frame ? 1 : 0);

		const char* exts[3] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME, nullptr, nullptr };
		uint32_t n_exts = 1;
		if (vr.ext_touch_plus)
			exts[n_exts++] = EXT_TOUCH_PLUS;
		if (vr.ext_frame)
			exts[n_exts++] = EXT_FRAME;
		XrInstanceCreateInfo ici = { XR_TYPE_INSTANCE_CREATE_INFO };
		xr_strcpy(ici.applicationInfo.applicationName, "GammaVR");
		xr_strcpy(ici.applicationInfo.engineName, "X-Ray Monolith");
		ici.applicationInfo.applicationVersion = 1;
		ici.applicationInfo.engineVersion = 1;
		ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
		ici.enabledExtensionCount = n_exts;
		ici.enabledExtensionNames = exts;
		if (!vr_check(vr.CreateInstance(&ici, &vr.instance), "xrCreateInstance"))
			return false;
		Msg("* [vr] xrCreateInstance: XR_SUCCESS");

#define VR_BIND(n) if (!vr_bind("xr" #n, vr.n)) return false
		VR_BIND(GetInstanceProperties);
		VR_BIND(GetSystem);
		VR_BIND(GetSystemProperties);
		VR_BIND(GetD3D11GraphicsRequirementsKHR);
		VR_BIND(CreateSession);
		VR_BIND(CreateReferenceSpace);
		VR_BIND(EnumerateViewConfigurationViews);
		VR_BIND(EnumerateSwapchainFormats);
		VR_BIND(CreateSwapchain);
		VR_BIND(EnumerateSwapchainImages);
		VR_BIND(PollEvent);
		VR_BIND(BeginSession);
		VR_BIND(EndSession);
		VR_BIND(WaitFrame);
		VR_BIND(BeginFrame);
		VR_BIND(EndFrame);
		VR_BIND(LocateViews);
		VR_BIND(LocateSpace);
		VR_BIND(AcquireSwapchainImage);
		VR_BIND(WaitSwapchainImage);
		VR_BIND(ReleaseSwapchainImage);
		VR_BIND(StringToPath);
		VR_BIND(PathToString);
		VR_BIND(CreateActionSet);
		VR_BIND(CreateAction);
		VR_BIND(SuggestInteractionProfileBindings);
		VR_BIND(AttachSessionActionSets);
		VR_BIND(CreateActionSpace);
		VR_BIND(SyncActions);
		VR_BIND(GetCurrentInteractionProfile);
		VR_BIND(GetActionStateFloat);
		VR_BIND(GetActionStateVector2f);
#undef VR_BIND
		// milestone 7b: optional (a failure only loses the weapon switch input / the floor measurement)
		if (!vr_bind("xrGetActionStateBoolean", vr.GetActionStateBoolean))
			vr.GetActionStateBoolean = nullptr;
		if (!vr_bind("xrEnumerateReferenceSpaces", vr.EnumerateReferenceSpaces))
			vr.EnumerateReferenceSpaces = nullptr;

		XrInstanceProperties ip = { XR_TYPE_INSTANCE_PROPERTIES };
		if (XR_SUCCEEDED(vr.GetInstanceProperties(vr.instance, &ip)))
			Msg("* [vr] runtime: %s %u.%u.%u", ip.runtimeName, (u32)XR_VERSION_MAJOR(ip.runtimeVersion),
				(u32)XR_VERSION_MINOR(ip.runtimeVersion), (u32)XR_VERSION_PATCH(ip.runtimeVersion));

		XrSystemGetInfo sgi = { XR_TYPE_SYSTEM_GET_INFO };
		sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
		if (!vr_check(vr.GetSystem(vr.instance, &sgi, &vr.system), "xrGetSystem"))
			return false;
		XrSystemProperties sp = { XR_TYPE_SYSTEM_PROPERTIES };
		if (XR_SUCCEEDED(vr.GetSystemProperties(vr.instance, vr.system, &sp)))
			Msg("* [vr] xrGetSystem: XR_SUCCESS, system: %s", sp.systemName);

		XrGraphicsRequirementsD3D11KHR req = { XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
		if (!vr_check(vr.GetD3D11GraphicsRequirementsKHR(vr.instance, vr.system, &req), "xrGetD3D11GraphicsRequirementsKHR"))
			return false;

		XrGraphicsBindingD3D11KHR bind = { XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
		bind.device = HW.pDevice;
		XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
		sci.next = &bind;
		sci.systemId = vr.system;
		if (!vr_check(vr.CreateSession(vr.instance, &sci, &vr.session), "xrCreateSession"))
			return false;
		Msg("* [vr] xrCreateSession (D3D11): XR_SUCCESS");

		XrReferenceSpaceCreateInfo rsi = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
		rsi.poseInReferenceSpace.orientation.w = 1.f;
		rsi.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
		if (!vr_check(vr.CreateReferenceSpace(vr.session, &rsi, &vr.local), "xrCreateReferenceSpace(LOCAL)"))
			return false;
		rsi.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
		if (!vr_check(vr.CreateReferenceSpace(vr.session, &rsi, &vr.head), "xrCreateReferenceSpace(VIEW)"))
			return false;
		// milestone 7b (Amendment 23): a STAGE (floor-level) space, ONLY to measure the head's height above the floor for the
		// standing calibration; LOCAL stays the tracking space. Created only when the runtime lists STAGE.
		{
			bool has_stage = false;
			uint32_t ns = 0;
			if (vr.EnumerateReferenceSpaces && XR_SUCCEEDED(vr.EnumerateReferenceSpaces(vr.session, 0, &ns, nullptr)) && ns)
			{
				xr_vector<XrReferenceSpaceType> st(ns);
				if (XR_SUCCEEDED(vr.EnumerateReferenceSpaces(vr.session, ns, &ns, st.data())))
					for (uint32_t i = 0; i < ns && i < st.size(); ++i)
						if (st[i] == XR_REFERENCE_SPACE_TYPE_STAGE)
							has_stage = true;
			}
			if (has_stage)
			{
				rsi.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
				if (!vr_check(vr.CreateReferenceSpace(vr.session, &rsi, &vr.stage), "xrCreateReferenceSpace(STAGE)"))
					vr.stage = XR_NULL_HANDLE;
			}
			Msg("* [vr3] floor-level space (STAGE): %s", !has_stage ? "not offered" : vr.stage != XR_NULL_HANDLE ? "created (floor height only)" : "creation failed");
		}

		vr.actions_ok = vr_init_actions();
		Msg(vr.actions_ok ? "* [vr3] controller actions ready (grip pose, both hands)" : "! [vr3] controller actions NOT available; the gun keeps its normal HUD place");

		XrViewConfigurationView vcv[2] = { { XR_TYPE_VIEW_CONFIGURATION_VIEW }, { XR_TYPE_VIEW_CONFIGURATION_VIEW } };
		uint32_t n = 0;
		if (!vr_check(vr.EnumerateViewConfigurationViews(vr.instance, vr.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &n, vcv),
			"xrEnumerateViewConfigurationViews") || n != 2)
			return false;

		int64_t formats[64];
		uint32_t nf = 0;
		if (!vr_check(vr.EnumerateSwapchainFormats(vr.session, 64, &nf, formats), "xrEnumerateSwapchainFormats") || !nf)
			return false;
		// the eye pictures are copied from the engine's back buffer, so the swapchain must be copy-compatible with it
		const DXGI_FORMAT bb = HW.m_ChainDesc.Format;
		const DXGI_FORMAT bb_srgb = bb == DXGI_FORMAT_R8G8B8A8_UNORM ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : bb;
		int64_t pick = 0;
		for (uint32_t i = 0; i < nf && !pick; ++i)
			if (formats[i] == bb) pick = formats[i];
		for (uint32_t i = 0; i < nf && !pick; ++i)
			if (formats[i] == bb_srgb) pick = formats[i];
		if (!pick)
		{
			Msg("! [vr] the runtime offers no swapchain format compatible with the back buffer (format %d)", (int)bb);
			return false;
		}
		vr.format = (DXGI_FORMAT)pick;
		Msg("* [vr] runtime recommends %ux%u per eye (max %ux%u); using the back buffer size %ux%u, format %d (back buffer format %d)",
			vcv[0].recommendedImageRectWidth, vcv[0].recommendedImageRectHeight, vcv[0].maxImageRectWidth, vcv[0].maxImageRectHeight,
			(u32)HW.m_ChainDesc.Width, (u32)HW.m_ChainDesc.Height, (int)pick, (int)bb);
		if (HW.m_ChainDesc.Width > vcv[0].maxImageRectWidth || HW.m_ChainDesc.Height > vcv[0].maxImageRectHeight)
		{
			Msg("! [vr] back buffer is larger than the runtime's maximum eye image");
			return false;
		}

		for (int e = 0; e < 2; ++e)
		{
			vr_eye& E = vr.eye[e];
			E.w = (int32_t)HW.m_ChainDesc.Width;
			E.h = (int32_t)HW.m_ChainDesc.Height;
			XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
			ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
			ci.format = pick;
			ci.sampleCount = 1;
			ci.width = E.w;
			ci.height = E.h;
			ci.faceCount = 1;
			ci.arraySize = 1;
			ci.mipCount = 1;
			if (!vr_check(vr.CreateSwapchain(vr.session, &ci, &E.chain), "xrCreateSwapchain"))
				return false;
			uint32_t ni = 0;
			if (!vr_check(vr.EnumerateSwapchainImages(E.chain, 0, &ni, nullptr), "xrEnumerateSwapchainImages") || !ni)
				return false;
			xr_vector<XrSwapchainImageD3D11KHR> img(ni);
			for (uint32_t i = 0; i < ni; ++i)
			{
				img[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
				img[i].next = nullptr;
				img[i].texture = nullptr;
			}
			if (!vr_check(vr.EnumerateSwapchainImages(E.chain, ni, &ni, (XrSwapchainImageBaseHeader*)img.data()), "xrEnumerateSwapchainImages"))
				return false;
			for (uint32_t i = 0; i < ni; ++i)
				E.tex.push_back(img[i].texture);
			Msg("* [vr] eye %d swapchain: %dx%d, format %d, %u images", e, E.w, E.h, (int)pick, ni);
		}
		return true;
	}

	void vr_events()
	{
		for (;;)
		{
			XrEventDataBuffer ev = { XR_TYPE_EVENT_DATA_BUFFER };
			if (vr.PollEvent(vr.instance, &ev) != XR_SUCCESS)
				break;
			if (ev.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED)
			{
				Msg("* [vr3] interaction profile changed (runtime event)");
				vr.profiles_logged = false; // log the bound profiles again on the next VR frame
				continue;
			}
			if (ev.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
				continue;
			const XrEventDataSessionStateChanged* sc = (const XrEventDataSessionStateChanged*)&ev;
			vr.state = sc->state;
			Msg("* [vr] session state -> %d", (int)vr.state);
			if (vr.state == XR_SESSION_STATE_READY)
			{
				XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
				bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				if (vr_check(vr.BeginSession(vr.session, &bi), "xrBeginSession"))
				{
					vr.running = true;
					Msg("* [vr] xrBeginSession: XR_SUCCESS");
				}
			}
			else if (vr.state == XR_SESSION_STATE_STOPPING)
			{
				vr.EndSession(vr.session);
				vr.running = false;
			}
			else if (vr.state == XR_SESSION_STATE_EXITING || vr.state == XR_SESSION_STATE_LOSS_PENDING)
			{
				vr.running = false;
				vr.ok = false;
			}
		}
	}
}

namespace
{
	bool vr_ready()
	{
		static const bool enabled = !!strstr(Core.Params, "-vr_openxr");
		if (!enabled)
			return false;
		if (!vr.tried)
		{
			vr.tried = true;
			if (LPCSTR p = strstr(Core.Params, "-vr_eye_scale "))
				sscanf(p + 14, "%f", &vr.eye_scale);
			vr.dump = !!strstr(Core.Params, "-vr_dump");
			if (LPCSTR q = strstr(Core.Params, "-vr_dump_at "))
			{
				int f[8];
				const int n = sscanf(q + 12, "%d %d %d %d %d %d %d %d", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7]);
				for (int i = 0; i < n && i < 8; ++i)
					if (f[i] > 0)
						vr.dump_at[vr.dump_at_n++] = u32(f[i]);
				Msg("* [vr] dump: %d further frames (-vr_dump_at)", vr.dump_at_n);
			}
			if (LPCSTR m = strstr(Core.Params, "-vr_mono_eye "))
			{
				int e = -1;
				if (sscanf(m + 13, "%d", &e) == 1 && (e == 0 || e == 1))
					g_vr_mono_eye = e;
			}
			vr.gun_hand = strstr(Core.Params, "-vr_gun_hand left") ? 0 : 1;
			vr.gun_on_head = !!strstr(Core.Params, "-vr_gun_on_head");
			vr.aim_old = !!strstr(Core.Params, "-vr_aim_old");
			Msg("* [vr3] gun hand %c, nogun %d, gun on head %d (DELIBERATELY BROKEN test mode if 1)",
				vr.gun_hand ? 'R' : 'L', vr_nogun() ? 1 : 0, vr.gun_on_head ? 1 : 0);
			// -vr_grip_rot <yaw_deg> <pitch_deg>: the gun's grip orientation becomes q_inject * q_runtime, where q_inject
			// (engine axes, x right, y up, z forward) pitches UP by <pitch> about x first, then yaws RIGHT by <yaw> about y.
			// With vr_quat_rotate (v' = q v q*): q = (sin(a/2), 0, 0, cos(a/2)) takes (0,0,1) to (0, -sin a, cos a), so
			// pitch up = a = -pitch; q = (0, sin(b/2), 0, cos(b/2)) takes (0,0,1) to (sin b, 0, cos b), so yaw right = b = yaw.
			// q_inject = q_yaw * q_pitch = (-cy*sp, sy*cp, sy*sp, cy*cp) with cy/sy = cos/sin(yaw/2), cp/sp = cos/sin(pitch/2).
			// Checked numerically with the same formula as vr_quat_rotate: yaw 30, pitch 15 gives
			// q = (-0.12608, 0.25660, 0.03378, 0.95766) and R*(0,0,1) = (0.48296, 0.25882, 0.83652) as registered.
			// not applied in the -vr_gun_on_head mode (the gun pose there is the head pose)
			LPCSTR r = vr.gun_on_head ? nullptr : strstr(Core.Params, "-vr_grip_rot ");
			if (r)
			{
				float yaw = 0.f, pitch = 0.f;
				if (sscanf(r + 13, "%f %f", &yaw, &pitch) == 2)
				{
					const float hy = deg2rad(yaw) * 0.5f, hp = deg2rad(pitch) * 0.5f;
					const float cy = _cos(hy), sy = _sin(hy), cp = _cos(hp), sp = _sin(hp);
					vr.grip_rot_q[0] = -cy * sp;
					vr.grip_rot_q[1] = sy * cp;
					vr.grip_rot_q[2] = sy * sp;
					vr.grip_rot_q[3] = cy * cp;
					vr.grip_rot = true;
					Fvector f, rf;
					f.set(0.f, 0.f, 1.f);
					vr_quat_rotate(vr.grip_rot_q[0], vr.grip_rot_q[1], vr.grip_rot_q[2], vr.grip_rot_q[3], f, rf);
					Msg("* [vr3] grip rotation injected: yaw %.2f pitch %.2f deg, R*(0,0,1) = %.5f %.5f %.5f", yaw, pitch, rf.x, rf.y, rf.z);
				}
				else
					Msg("! [vr3] -vr_grip_rot needs two numbers (yaw pitch, degrees); ignored");
			}
			if (!vr.grip_rot)
				Msg("* [vr3] grip rotation injected: none");
			// -vr_grip_lag <n>: deliberately broken test mode, the gun uses the grip pose of n VR frames ago
			if (LPCSTR g = strstr(Core.Params, "-vr_grip_lag "))
			{
				int n = 0;
				if (sscanf(g + 13, "%d", &n) == 1)
					vr.grip_lag = _max(0, _min(8, n));
			}
			Msg("* [vr3] grip lag (deliberately broken test mode): %d frames", vr.grip_lag);
			vr.ok = vr_init();
			Msg(vr.ok ? "* [vr] OpenXR bring-up complete" : "! [vr] OpenXR bring-up FAILED; VR path disabled for this run");
			Msg("* [vr] eye offset scale %.3f, dump %d, mono twin eye %d", vr.eye_scale, vr.dump ? 1 : 0, g_vr_mono_eye);
		}
		return vr.ok;
	}

	// rotate v by the inverse of unit quaternion q
	XrVector3f vr_inv_rotate(const XrQuaternionf& q, const XrVector3f& v)
	{
		const float x = -q.x, y = -q.y, z = -q.z, w = q.w;
		const float tx = 2.f * (y * v.z - z * v.y), ty = 2.f * (z * v.x - x * v.z), tz = 2.f * (x * v.y - y * v.x);
		XrVector3f r;
		r.x = v.x + w * tx + (y * tz - z * ty);
		r.y = v.y + w * ty + (z * tx - x * tz);
		r.z = v.z + w * tz + (x * ty - y * tx);
		return r;
	}

	// OpenXR pose (x right, y up, -z forward, right-handed) -> engine axes (x right, y up, +z forward).
	// Mirroring z: position (x, y, -z); a rotation R becomes S R S with S = diag(1, 1, -1), i.e. the quaternion
	// (x, y, z, w) becomes (-x, -y, z, w).
	void vr_to_engine(const XrPosef& p, bool valid, vr_pose& out)
	{
		out.pos.set(p.position.x, p.position.y, -p.position.z);
		out.qx = -p.orientation.x;
		out.qy = -p.orientation.y;
		out.qz = p.orientation.z;
		out.qw = p.orientation.w;
		out.valid = valid;
	}

	// milestone 3: xrSyncActions and the grip pose of both hands at the frame's predicted display time
	void vr_locate_grips()
	{
		vr.grip_located = false;
		for (int h = 0; h < 2; ++h)
		{
			vr.grip[h].pos.set(0.f, 0.f, 0.f);
			vr.grip[h].qx = vr.grip[h].qy = vr.grip[h].qz = 0.f;
			vr.grip[h].qw = 1.f;
			vr.grip[h].valid = false;
			vr.aim[h] = vr.grip[h];
		}
		if (!vr.actions_ok)
			return;
		XrActiveActionSet active = { vr.action_set, XR_NULL_PATH };
		XrActionsSyncInfo si = { XR_TYPE_ACTIONS_SYNC_INFO };
		si.countActiveActionSets = 1;
		si.activeActionSets = &active;
		const XrResult rs = vr.SyncActions(vr.session, &si); // XR_SESSION_NOT_FOCUSED is a success code: poses may be invalid
		if (XR_FAILED(rs))
			return;
		// milestone 6 (Amendment 19): button values of this VR frame (vr.frames_run = the number this frame's eyes get as
		// vr_frame_info::frame). An inactive action or a failed call reads as value 0, inactive.
		for (int h = 0; h < 2; ++h)
		{
			const XrAction act[2] = { vr.fire_action, vr.grab_action };
			float* val[2] = { &vr.in_trigger[h], &vr.in_grab[h] };
			bool* on[2] = { &vr.in_trigger_active[h], &vr.in_grab_active[h] };
			for (int k = 0; k < 2; ++k)
			{
				*val[k] = 0.f;
				*on[k] = false;
				if (act[k] == XR_NULL_HANDLE)
					continue;
				XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
				gi.action = act[k];
				gi.subactionPath = vr.hand_path[h];
				XrActionStateFloat st = { XR_TYPE_ACTION_STATE_FLOAT };
				if (XR_SUCCEEDED(vr.GetActionStateFloat(vr.session, &gi, &st)) && st.isActive)
				{
					*val[k] = st.currentState;
					*on[k] = true;
				}
			}
		}
		// milestone 7 (Amendment 20): thumbsticks of this VR frame; inactive / failed / no action = 0 0, inactive
		for (int h = 0; h < 2; ++h)
		{
			vr.in_stick[h][0] = vr.in_stick[h][1] = 0.f;
			vr.in_stick_active[h] = false;
			if (vr.stick_action == XR_NULL_HANDLE)
				continue;
			XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
			gi.action = vr.stick_action;
			gi.subactionPath = vr.hand_path[h];
			XrActionStateVector2f st = { XR_TYPE_ACTION_STATE_VECTOR2F };
			if (XR_SUCCEEDED(vr.GetActionStateVector2f(vr.session, &gi, &st)) && st.isActive)
			{
				vr.in_stick[h][0] = st.currentState.x;
				vr.in_stick[h][1] = st.currentState.y;
				vr.in_stick_active[h] = true;
			}
		}
		// milestone 7b (Amendment 23): next_weapon of this VR frame; inactive / failed / no action = false, inactive
		// and crouch_toggle (unbound unless -vr_dbg_crouch_bind: then inactive = not pressed)
		for (int h = 0; h < 2; ++h)
		{
			const XrAction bact[2] = { vr.next_action, vr.crouch_action };
			bool* bval[2] = { &vr.in_next[h], &vr.in_crouch[h] };
			bool* bon[2] = { &vr.in_next_active[h], &vr.in_crouch_active[h] };
			for (int k = 0; k < 2; ++k)
			{
				*bval[k] = false;
				*bon[k] = false;
				if (bact[k] == XR_NULL_HANDLE || !vr.GetActionStateBoolean)
					continue;
				XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
				gi.action = bact[k];
				gi.subactionPath = vr.hand_path[h];
				XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
				if (XR_SUCCEEDED(vr.GetActionStateBoolean(vr.session, &gi, &st)) && st.isActive)
				{
					*bval[k] = st.currentState == XR_TRUE;
					*bon[k] = true;
				}
			}
		}
		vr.input_frame = vr.frames_run;
		vr.input_seq++;
		vr.input_read = true;
		// current interaction profile of each hand: one line whenever it becomes known or changes
		for (int h = 0; h < 2; ++h)
		{
			XrInteractionProfileState ps = { XR_TYPE_INTERACTION_PROFILE_STATE };
			if (XR_FAILED(vr.GetCurrentInteractionProfile(vr.session, vr.hand_path[h], &ps)))
				continue;
			if (vr.profile_seen[h] && ps.interactionProfile == vr.profile_cur[h])
				continue;
			vr.profile_seen[h] = true;
			vr.profile_cur[h] = ps.interactionProfile;
			char name[XR_MAX_PATH_LENGTH];
			xr_strcpy(name, "none");
			if (ps.interactionProfile != XR_NULL_PATH)
			{
				uint32_t n = 0;
				if (XR_FAILED(vr.PathToString(vr.instance, ps.interactionProfile, XR_MAX_PATH_LENGTH, &n, name)))
					xr_strcpy(name, "?");
			}
			Msg("* [vr3] interaction profile %c: %s", h ? 'R' : 'L', name);
		}
		const XrSpaceLocationFlags need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
		vr.grip_time = vr.fs.predictedDisplayTime; // the time passed to xrLocateSpace below (milestone 3d log)
		for (int h = 0; h < 2; ++h)
		{
			XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
			const XrResult r = vr.LocateSpace(vr.grip_space[h], vr.local, vr.fs.predictedDisplayTime, &loc);
			const bool valid = XR_SUCCEEDED(r) && (loc.locationFlags & need) == need;
			if (XR_SUCCEEDED(r))
				vr_to_engine(loc.pose, valid, vr.grip[h]);
			vr.grip_raw[h] = XR_SUCCEEDED(r) ? loc.pose : XrPosef{ { 0.f, 0.f, 0.f, 1.f }, { 0.f, 0.f, 0.f } };
			// H1: the aim pose of the same hand at the same time
			XrSpaceLocation al = { XR_TYPE_SPACE_LOCATION };
			const XrResult ra = vr.LocateSpace(vr.aim_space[h], vr.local, vr.fs.predictedDisplayTime, &al);
			if (XR_SUCCEEDED(ra))
				vr_to_engine(al.pose, (al.locationFlags & need) == need, vr.aim[h]);
		}
		{
			// H1 log, every 100th read: the angle between the grip and the aim axes per hand (a real controller's differ;
			// the next headset session measures it), and the tilt setting
			static u32 s_n = 0;
			if (s_n++ % 100 == 0)
			{
				float ang[2] = { -1.f, -1.f };
				for (int h = 0; h < 2; ++h)
					if (vr.grip[h].valid && vr.aim[h].valid)
					{
						Fvector g, a;
						vr_quat_rotate(vr.grip[h].qx, vr.grip[h].qy, vr.grip[h].qz, vr.grip[h].qw, Fvector().set(0.f, 0.f, 1.f), g);
						vr_quat_rotate(vr.aim[h].qx, vr.aim[h].qy, vr.aim[h].qz, vr.aim[h].qw, Fvector().set(0.f, 0.f, 1.f), a);
						ang[h] = rad2deg(acosf(_max(-1.f, _min(1.f, g.dotproduct(a)))));
					}
				Msg("* [vr23a] read %u grip-to-aim angle L %.3f R %.3f deg (-1 = not located); gun follows %s; vr_gun_tilt %.2f",
					s_n - 1, ang[0], ang[1], vr.aim_old ? "GRIP (-vr_aim_old)" : "aim", g_vr_gun_tilt);
			}
		}
		vr.grip_located = true;
		// milestone 7: the poses that belong to this read (vr_input_info::grip / head)
		vr.in_grip[0] = vr.grip[0];
		vr.in_grip[1] = vr.grip[1];
		vr.in_head = vr.head_e;
		// milestone 7b: the head's height above the floor (VIEW in STAGE), same time as the head pose; y is the same axis in
		// OpenXR and engine axes
		vr.in_floor_valid = false;
		vr.in_floor_y = 0.f;
		if (vr.stage != XR_NULL_HANDLE)
		{
			XrSpaceLocation fl = { XR_TYPE_SPACE_LOCATION };
			if (XR_SUCCEEDED(vr.LocateSpace(vr.head, vr.stage, vr.fs.predictedDisplayTime, &fl)) &&
				(fl.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
			{
				vr.in_floor_y = fl.pose.position.y;
				vr.in_floor_valid = true;
			}
		}
		// milestone 3d: ring of converted poses (one entry per located VR frame) for -vr_grip_lag
		vr.grip_ring_head = (vr.grip_ring_head + 1) % vr_state::GRIP_RING;
		vr.grip_ring[vr.grip_ring_head][0] = vr.grip[0];
		vr.grip_ring[vr.grip_ring_head][1] = vr.grip[1];
		if (vr.grip_ring_count < vr_state::GRIP_RING)
			++vr.grip_ring_count;

		if (!vr.profiles_logged)
		{
			vr.profiles_logged = true;
			for (int h = 0; h < 2; ++h)
			{
				XrInteractionProfileState ps = { XR_TYPE_INTERACTION_PROFILE_STATE };
				char name[XR_MAX_PATH_LENGTH];
				xr_strcpy(name, "none");
				const XrResult r = vr.GetCurrentInteractionProfile(vr.session, vr.hand_path[h], &ps);
				if (XR_SUCCEEDED(r) && ps.interactionProfile != XR_NULL_PATH)
				{
					uint32_t n = 0;
					if (XR_FAILED(vr.PathToString(vr.instance, ps.interactionProfile, XR_MAX_PATH_LENGTH, &n, name)))
						xr_strcpy(name, "?");
				}
				Msg("* [vr3] interaction profile hand %c: %s (XrResult %d, sync %d)", h ? 'R' : 'L', name, (int)r, (int)rs);
			}
		}
	}

	// hook: grip pose of the chosen hand, and the pose the gun uses
	bool vr_get_grip(vr_grip_info* out)
	{
		if (!vr.ok || !vr.grip_located || !out)
			return false;
		out->hand = vr.gun_hand ? 'R' : 'L';
		out->runtime = vr.grip[vr.gun_hand];
		out->t_ns = (long long)vr.grip_time;
		const XrPosef& raw = vr.grip_raw[vr.gun_hand];
		out->xr_pos[0] = raw.position.x;
		out->xr_pos[1] = raw.position.y;
		out->xr_pos[2] = raw.position.z;
		out->xr_quat[0] = raw.orientation.x;
		out->xr_quat[1] = raw.orientation.y;
		out->xr_quat[2] = raw.orientation.z;
		out->xr_quat[3] = raw.orientation.w;
		out->lag = vr.grip_lag;
		out->off = vr.grip[1 - vr.gun_hand]; // milestone 5: off hand, current frame
		// -vr_grip_lag: the pose the gun is placed with is the one of grip_lag frames ago (the oldest kept if fewer)
		vr_pose placed = out->runtime;
		if (vr.grip_lag > 0 && vr.grip_ring_count > 0)
		{
			const int back = _min(vr.grip_lag, vr.grip_ring_count - 1);
			const int idx = (vr.grip_ring_head - back + vr_state::GRIP_RING) % vr_state::GRIP_RING;
			placed = vr.grip_ring[idx][vr.gun_hand];
		}
		if (vr.gun_on_head)
		{
			// DELIBERATELY BROKEN test mode: head pose plus a fixed offset (0.2 m right, 0.2 m down, 0.4 m forward)
			Fvector off, o;
			off.set(0.2f, -0.2f, 0.4f);
			vr_quat_rotate(vr.head_e.qx, vr.head_e.qy, vr.head_e.qz, vr.head_e.qw, off, o);
			out->gun = vr.head_e;
			out->gun.pos.add(vr.head_e.pos, o);
		}
		else
		{
			out->gun = placed;
			// H1 (Amendment 42): the gun's DIRECTION is the aim pose's (current frame), turned by the tilt setting about
			// the hand's own side axis; its place stays the grip pose's. Not with -vr_aim_old, -vr_grip_lag or an aim
			// pose that was not located (then the grip pose's direction, as before).
			if (!vr.aim_old && vr.grip_lag == 0 && vr.aim[vr.gun_hand].valid)
			{
				Fmatrix m;
				vr_pose_matrix(vr.aim[vr.gun_hand], m);
				float t = g_vr_gun_tilt;
				clamp(t, -90.f, 90.f);
				const float c = cosf(deg2rad(t)), s = sinf(deg2rad(t));
				Fvector k2, j2;
				k2.mul(m.k, c);
				k2.mad(m.j, s);   // forward turned up by t
				j2.mul(m.j, c);
				j2.mad(m.k, -s);
				vr_matrix_quat(m.i, j2, k2, out->gun.qx, out->gun.qy, out->gun.qz, out->gun.qw);
			}
			if (vr.grip_rot)
			{
				// test switch -vr_grip_rot: orientation = q_inject * q_runtime (Hamilton product), position untouched
				const float x1 = vr.grip_rot_q[0], y1 = vr.grip_rot_q[1], z1 = vr.grip_rot_q[2], w1 = vr.grip_rot_q[3];
				const vr_pose R = out->gun; // (H1: on top of the aim direction; the same as before when aim = grip)
				out->gun.qx = w1 * R.qx + x1 * R.qw + y1 * R.qz - z1 * R.qy;
				out->gun.qy = w1 * R.qy - x1 * R.qz + y1 * R.qw + z1 * R.qx;
				out->gun.qz = w1 * R.qz + x1 * R.qy - y1 * R.qx + z1 * R.qw;
				out->gun.qw = w1 * R.qw - x1 * R.qx - y1 * R.qy - z1 * R.qz;
			}
		}
		return true;
	}

	// hook (milestone 6): button values of the last read (see vr_input_info). Written in vr_locate_grips (render path,
	// main thread), read by xrGame in CLevel::OnFrame (FrameMove, main thread): never at the same time.
	bool vr_get_input(vr_input_info* out)
	{
		if (!vr.ok || !vr.input_read || !out)
			return false;
		out->frame = vr.input_frame;
		out->seq = vr.input_seq;
		out->gun = vr.gun_hand;
		for (int h = 0; h < 2; ++h)
		{
			out->trigger[h] = vr.in_trigger[h];
			out->grab[h] = vr.in_grab[h];
			out->trigger_active[h] = vr.in_trigger_active[h];
			out->grab_active[h] = vr.in_grab_active[h];
			out->stick[h][0] = vr.in_stick[h][0];
			out->stick[h][1] = vr.in_stick[h][1];
			out->stick_active[h] = vr.in_stick_active[h];
			out->grip[h] = vr.in_grip[h];
			out->next[h] = vr.in_next[h];
			out->next_active[h] = vr.in_next_active[h];
			out->crouch_btn[h] = vr.in_crouch[h];
			out->crouch_btn_active[h] = vr.in_crouch_active[h];
		}
		out->head = vr.in_head;
		out->floor_head_y = vr.in_floor_y;
		out->floor_valid = vr.in_floor_valid;
		return true;
	}

	// xrWaitFrame + xrBeginFrame + view and head poses. True if a frame is now open.
	bool vr_open_frame()
	{
		vr.frame_begun = false;
		vr.eyes_done = 0;
		XrFrameWaitInfo wi = { XR_TYPE_FRAME_WAIT_INFO };
		vr.fs = { XR_TYPE_FRAME_STATE };
		XrFrameBeginInfo bi = { XR_TYPE_FRAME_BEGIN_INFO };
		if (XR_FAILED(vr.WaitFrame(vr.session, &wi, &vr.fs)) || XR_FAILED(vr.BeginFrame(vr.session, &bi)))
		{
			vr.frames_fail++; vr.frames_run = 0;
			return false;
		}
		vr.frame_begun = true;
		if (!vr.period_logged)
		{
			vr.period_logged = true;
			Msg("* [vr3] frame period (runtime) %lld ns", (long long)vr.fs.predictedDisplayPeriod);
		}
		return true;
	}

	void vr_dump_image(int eye, ID3D11Texture2D* image)
	{
		D3D11_TEXTURE2D_DESC d;
		image->GetDesc(&d);
		d.Format = vr.format;
		d.Usage = D3D11_USAGE_STAGING;
		d.BindFlags = 0;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		d.MiscFlags = 0;
		ID3D11Texture2D* st = nullptr;
		if (FAILED(HW.pDevice->CreateTexture2D(&d, nullptr, &st)) || !st)
		{
			Msg("! [vr] dump: staging texture failed");
			return;
		}
		HW.pContext->CopyResource(st, image);
		D3D11_MAPPED_SUBRESOURCE m;
		if (SUCCEEDED(HW.pContext->Map(st, 0, D3D11_MAP_READ, 0, &m)))
		{
			string_path name, path;
			xr_sprintf(name, "vr_eye%d_%u.raw", eye, vr.frames_run);
			FS.update_path(path, "$logs$", name);
			if (FILE* f = fopen(path, "wb"))
			{
				fprintf(f, "GVRIMG1 %u %u %d\n", d.Width, d.Height, (int)d.Format);
				for (u32 y = 0; y < d.Height; ++y)
					fwrite((const u8*)m.pData + (size_t)y * m.RowPitch, 4, d.Width, f);
				fclose(f);
				Msg("* [vr] dump: wrote %s (%ux%u, read back from the swapchain image)", name, d.Width, d.Height);
			}
			else
				Msg("! [vr] dump: cannot open %s", path);
			HW.pContext->Unmap(st, 0);
		}
		else
			Msg("! [vr] dump: Map failed");
		st->Release();
	}

	// Milestone 3, -vr_dump: the HUD layer mask of this eye. The HUD (held item + arms) is drawn into the G-buffer with
	// the depth range [0, 0.02] (CRender::rmNear) and the world with [0, 1]; world depth 0.02 is 1.02 x the near plane,
	// so depth <= 0.02 marks exactly the pixels where the HUD layer is the front surface. Post-processing (bloom, DOF,
	// TAA, ...) never writes depth, so the mask is the gun's own silhouette. Depth target: HW.pBaseZB, the one
	// phase_scene_begin() binds for the HUD pass without MSAA (with MSAA it is rt_MSAADepth: not supported here).
	// It is only cleared in phase_scene_prepare(), i.e. at the start of the next eye, so it is intact in vr_eye_done().
	void vr_dump_hud_mask(int eye)
	{
		static bool s_format_logged = false;
		if (RImplementation.o.dx10_msaa)
		{
			Msg("! [vr3] mask: MSAA is on, the HUD depth is in rt_MSAADepth; no mask written");
			return;
		}
		ID3D11Resource* zr = nullptr;
		HW.pBaseZB->GetResource(&zr);
		if (!zr)
		{
			Msg("! [vr3] mask: no depth resource");
			return;
		}
		D3D11_TEXTURE2D_DESC d;
		((ID3D11Texture2D*)zr)->GetDesc(&d);
		if (!s_format_logged)
		{
			s_format_logged = true;
			Msg("* [vr3] mask: read from the base depth buffer, DXGI format %d, %ux%u, samples %u", (int)d.Format, d.Width, d.Height, d.SampleDesc.Count);
		}
		if (d.Format != DXGI_FORMAT_D24_UNORM_S8_UINT && d.Format != DXGI_FORMAT_R24G8_TYPELESS)
		{
			Msg("! [vr3] mask: depth format %d is not D24S8; no mask written", (int)d.Format);
			zr->Release();
			return;
		}
		d.Usage = D3D11_USAGE_STAGING;
		d.BindFlags = 0;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		d.MiscFlags = 0;
		ID3D11Texture2D* st = nullptr;
		if (d.SampleDesc.Count != 1 || FAILED(HW.pDevice->CreateTexture2D(&d, nullptr, &st)) || !st)
		{
			Msg("! [vr3] mask: staging depth texture failed");
			zr->Release();
			return;
		}
		HW.pContext->CopyResource(st, zr);
		zr->Release();
		D3D11_MAPPED_SUBRESOURCE m;
		if (SUCCEEDED(HW.pContext->Map(st, 0, D3D11_MAP_READ, 0, &m)))
		{
			const u32 thr = u32(0.02 * 16777215.0) + 1; // D24 depth in the low 24 bits of each texel
			u32 n = 0, x0 = d.Width, y0 = d.Height, x1 = 0, y1 = 0;
			string_path name, path;
			xr_sprintf(name, "vr_eye%d_%u_hudmask.raw", eye, vr.frames_run);
			FS.update_path(path, "$logs$", name);
			FILE* f = fopen(path, "wb");
			if (f)
				fprintf(f, "GVRIMG1 %u %u %d\n", d.Width, d.Height, (int)DXGI_FORMAT_R8G8B8A8_UNORM); // same reader: 4 bytes/pixel
			else
				Msg("! [vr3] mask: cannot open %s", path);
			xr_vector<u32> row(d.Width);
			for (u32 y = 0; y < d.Height; ++y)
			{
				const u32* src = (const u32*)((const u8*)m.pData + (size_t)y * m.RowPitch);
				for (u32 x = 0; x < d.Width; ++x)
				{
					const bool hud = (src[x] & 0xFFFFFFu) <= thr;
					row[x] = hud ? 0xFFFFFFFFu : 0xFF000000u;
					if (hud)
					{
						++n;
						x0 = _min(x0, x);
						y0 = _min(y0, y);
						x1 = _max(x1, x);
						y1 = _max(y1, y);
					}
				}
				if (f)
					fwrite(row.data(), 4, d.Width, f);
			}
			if (f)
				fclose(f);
			HW.pContext->Unmap(st, 0);
			if (n == 0)
				x0 = y0 = 0;
			Msg("* [vr3] frame %u eye %d hud_mask pixels %u bbox %u %u %u %u file %s", vr.frames_run, eye, n, x0, y0, x1, y1, name);
		}
		else
			Msg("! [vr3] mask: Map failed");
		st->Release();
	}

	// hook: called by the level render loop before the eyes are rendered
	bool vr_begin_frame(vr_eye_view* out, vr_frame_info* info)
	{
		if (!vr_ready())
			return false;
		vr_events();
		if (!vr.running || !vr_open_frame())
			return false;

		XrViewState vs = { XR_TYPE_VIEW_STATE };
		XrViewLocateInfo li = { XR_TYPE_VIEW_LOCATE_INFO };
		li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		li.displayTime = vr.fs.predictedDisplayTime;
		li.space = vr.local;
		uint32_t nv = 0;
		vr.views[0] = { XR_TYPE_VIEW };
		vr.views[1] = { XR_TYPE_VIEW };
		XrResult rv = vr.LocateViews(vr.session, &li, &vs, 2, &nv, vr.views);
		vr.head_loc = { XR_TYPE_SPACE_LOCATION };
		vr.head_r = vr.LocateSpace(vr.head, vr.local, vr.fs.predictedDisplayTime, &vr.head_loc);
		const XrSpaceLocationFlags need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
		if (!vr.fs.shouldRender || XR_FAILED(rv) || nv != 2 || XR_FAILED(vr.head_r) || (vr.head_loc.locationFlags & need) != need)
			return false; // the open frame is ended without a layer in vr_openxr_frame()

		// milestone 3: head pose and controller grip poses, engine axes, tracking (LOCAL) space
		vr_to_engine(vr.head_loc.pose, true, vr.head_e);
		vr_locate_grips();
		if (info)
		{
			info->head = vr.head_e;
			info->frame = vr.frames_run;
		}

		for (int e = 0; e < 2; ++e)
		{
			XrVector3f d;
			d.x = vr.views[e].pose.position.x - vr.head_loc.pose.position.x;
			d.y = vr.views[e].pose.position.y - vr.head_loc.pose.position.y;
			d.z = vr.views[e].pose.position.z - vr.head_loc.pose.position.z;
			const XrVector3f h = vr_inv_rotate(vr.head_loc.pose.orientation, d);
			// OpenXR: x right, y up, -z forward. Engine: x right, y up, +z forward.
			out[e].offset.set(h.x * vr.eye_scale, h.y * vr.eye_scale, -h.z * vr.eye_scale);
			out[e].tan_l = tanf(vr.views[e].fov.angleLeft);
			out[e].tan_r = tanf(vr.views[e].fov.angleRight);
			out[e].tan_u = tanf(vr.views[e].fov.angleUp);
			out[e].tan_d = tanf(vr.views[e].fov.angleDown);
		}
		if (!vr.views_logged)
		{
			vr.views_logged = true;
			for (int e = 0; e < 2; ++e)
				Msg("* [vr] view %d: offset from head (engine axes, scaled) %.5f %.5f %.5f, fov tan l %.5f r %.5f u %.5f d %.5f, image %dx%d",
					e, out[e].offset.x, out[e].offset.y, out[e].offset.z, out[e].tan_l, out[e].tan_r, out[e].tan_u, out[e].tan_d, vr.eye[e].w, vr.eye[e].h);
			const float sx = vr.views[1].pose.position.x - vr.views[0].pose.position.x;
			const float sy = vr.views[1].pose.position.y - vr.views[0].pose.position.y;
			const float sz = vr.views[1].pose.position.z - vr.views[0].pose.position.z;
			Msg("* [vr] eye separation from the runtime %.5f m, rendered separation %.5f m",
				_sqrt(sx * sx + sy * sy + sz * sz), _abs(out[1].offset.x - out[0].offset.x));
		}
		return true;
	}

	// hook: the scene for one eye is finished in the back buffer; copy it into that eye's swapchain image
	void vr_eye_done(int e)
	{
		if (!vr.frame_begun || e < 0 || e > 1)
			return;
		vr_eye& E = vr.eye[e];
		uint32_t idx = 0;
		XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
		XrSwapchainImageWaitInfo swi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		swi.timeout = XR_INFINITE_DURATION;
		XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		if (XR_FAILED(vr.AcquireSwapchainImage(E.chain, &ai, &idx)))
			return;
		bool ok = XR_SUCCEEDED(vr.WaitSwapchainImage(E.chain, &swi)) && idx < E.tex.size();
		if (ok)
		{
			ID3D11Resource* bb = nullptr;
			HW.pBaseRT->GetResource(&bb);
			if (bb)
			{
				HW.pContext->CopyResource(E.tex[idx], bb);
				bb->Release();
				bool dump_now = vr.frames_run == 200 || vr.frames_run == 300;
				for (int i = 0; i < vr.dump_at_n; ++i)
					dump_now = dump_now || vr.frames_run == vr.dump_at[i];
				if (vr.dump && dump_now)
				{
					vr_dump_image(e, E.tex[idx]);
					vr_dump_hud_mask(e);
				}
			}
			else
				ok = false;
		}
		if (XR_FAILED(vr.ReleaseSwapchainImage(E.chain, &ri)))
			ok = false;
		if (ok)
			vr.eyes_done |= 1u << e;
	}

	struct vr_hook_registrar
	{
		vr_hook_registrar()
		{
			g_vr_begin_frame = &vr_begin_frame;
			g_vr_eye_done = &vr_eye_done;
			g_vr_get_grip = &vr_get_grip;
			g_vr_get_input = &vr_get_input;
		}
	} s_vr_hook_registrar;
}

// Called once per engine frame from dxRenderDeviceRender::End(), on the thread that owns the D3D11 context.
// Ends the OpenXR frame the level render loop opened; if none was opened (menus, loading), submits an empty frame
// so the session keeps running.
void vr_openxr_frame()
{
	if (!vr_ready())
		return;
	if (!vr.frame_begun)
	{
		vr_events();
		if (!vr.running || !vr_open_frame())
			return;
	}

	const bool layer_ok = vr.eyes_done == 3;
	XrCompositionLayerProjectionView pv[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
	for (int e = 0; e < 2; ++e)
	{
		pv[e].pose = vr.views[e].pose;
		pv[e].fov = vr.views[e].fov;
		pv[e].subImage.swapchain = vr.eye[e].chain;
		pv[e].subImage.imageRect.offset.x = 0;
		pv[e].subImage.imageRect.offset.y = 0;
		pv[e].subImage.imageRect.extent.width = vr.eye[e].w;
		pv[e].subImage.imageRect.extent.height = vr.eye[e].h;
		pv[e].subImage.imageArrayIndex = 0;
	}
	XrCompositionLayerProjection proj = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	proj.space = vr.local;
	proj.viewCount = 2;
	proj.views = pv;
	const XrCompositionLayerBaseHeader* layers[1] = { (const XrCompositionLayerBaseHeader*)&proj };
	XrFrameEndInfo ei = { XR_TYPE_FRAME_END_INFO };
	ei.displayTime = vr.fs.predictedDisplayTime;
	ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	ei.layerCount = layer_ok ? 1 : 0;
	ei.layers = layer_ok ? layers : nullptr;
	XrResult re = vr.EndFrame(vr.session, &ei);
	vr.frame_begun = false;

	if (re == XR_SUCCESS && layer_ok)
	{
		vr.frames_ok++; vr.frames_run++;
	}
	else
	{
		vr.frames_fail++; vr.frames_run = 0;
	}

	// one line per frame: the milestone tests read these
	const XrSpaceLocation& hl = vr.head_loc;
	Msg("* [vr] frame ok=%u run=%u fail=%u end=%d layer=%d t=%lld head_r=%d flags=0x%x pos %.5f %.5f %.5f quat %.6f %.6f %.6f %.6f",
		vr.frames_ok, vr.frames_run, vr.frames_fail, (int)re, layer_ok ? 1 : 0, (long long)vr.fs.predictedDisplayTime, (int)vr.head_r,
		(u32)hl.locationFlags, hl.pose.position.x, hl.pose.position.y, hl.pose.position.z,
		hl.pose.orientation.x, hl.pose.orientation.y, hl.pose.orientation.z, hl.pose.orientation.w);
}

#endif // USE_DX11
