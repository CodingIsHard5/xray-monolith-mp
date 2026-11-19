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
		bool views_logged = false;

		// milestone 3: controller grip poses (action set with one pose action, subaction paths left/right)
		bool actions_ok = false;
		XrActionSet action_set = XR_NULL_HANDLE;
		XrAction grip_action = XR_NULL_HANDLE;
		XrPath hand_path[2] = { XR_NULL_PATH, XR_NULL_PATH }; // 0 = left, 1 = right
		XrSpace grip_space[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
		vr_pose grip[2];          // located this frame, engine axes, LOCAL space
		vr_pose head_e;           // head pose this frame, engine axes, LOCAL space
		bool grip_located = false;
		int gun_hand = 1;         // -vr_gun_hand left|right
		bool gun_on_head = false; // -vr_gun_on_head
		bool profiles_logged = false;

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

		XrPath grip_path[2] = { XR_NULL_PATH, XR_NULL_PATH };
		if (!vr_check(vr.StringToPath(vr.instance, "/user/hand/left/input/grip/pose", &grip_path[0]), "xrStringToPath(left grip)") ||
			!vr_check(vr.StringToPath(vr.instance, "/user/hand/right/input/grip/pose", &grip_path[1]), "xrStringToPath(right grip)"))
			return false;
		XrActionSuggestedBinding b[2];
		for (int h = 0; h < 2; ++h)
		{
			b[h].action = vr.grip_action;
			b[h].binding = grip_path[h];
		}
		const char* profiles[] = { "/interaction_profiles/khr/simple_controller", "/interaction_profiles/microsoft/motion_controller" };
		u32 suggested = 0;
		for (int p = 0; p < 2; ++p)
		{
			XrPath prof = XR_NULL_PATH;
			if (!vr_check(vr.StringToPath(vr.instance, profiles[p], &prof), "xrStringToPath(profile)"))
				continue;
			XrInteractionProfileSuggestedBinding sb = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
			sb.interactionProfile = prof;
			sb.countSuggestedBindings = 2;
			sb.suggestedBindings = b;
			const XrResult r = vr.SuggestInteractionProfileBindings(vr.instance, &sb);
			Msg("* [vr3] suggested bindings for %s: XrResult %d", profiles[p], (int)r);
			if (XR_SUCCEEDED(r))
				suggested++;
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

		const char* exts[] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME };
		XrInstanceCreateInfo ici = { XR_TYPE_INSTANCE_CREATE_INFO };
		xr_strcpy(ici.applicationInfo.applicationName, "GammaVR");
		xr_strcpy(ici.applicationInfo.engineName, "X-Ray Monolith");
		ici.applicationInfo.applicationVersion = 1;
		ici.applicationInfo.engineVersion = 1;
		ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
		ici.enabledExtensionCount = 1;
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
#undef VR_BIND

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
			if (LPCSTR m = strstr(Core.Params, "-vr_mono_eye "))
			{
				int e = -1;
				if (sscanf(m + 13, "%d", &e) == 1 && (e == 0 || e == 1))
					g_vr_mono_eye = e;
			}
			vr.gun_hand = strstr(Core.Params, "-vr_gun_hand left") ? 0 : 1;
			vr.gun_on_head = !!strstr(Core.Params, "-vr_gun_on_head");
			Msg("* [vr3] gun hand %c, nogun %d, gun on head %d (DELIBERATELY BROKEN test mode if 1)",
				vr.gun_hand ? 'R' : 'L', vr_nogun() ? 1 : 0, vr.gun_on_head ? 1 : 0);
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
		const XrSpaceLocationFlags need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
		for (int h = 0; h < 2; ++h)
		{
			XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
			const XrResult r = vr.LocateSpace(vr.grip_space[h], vr.local, vr.fs.predictedDisplayTime, &loc);
			const bool valid = XR_SUCCEEDED(r) && (loc.locationFlags & need) == need;
			if (XR_SUCCEEDED(r))
				vr_to_engine(loc.pose, valid, vr.grip[h]);
		}
		vr.grip_located = true;

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
			out->gun = out->runtime;
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
				if (vr.dump && (vr.frames_run == 200 || vr.frames_run == 300))
					vr_dump_image(e, E.tex[idx]);
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
