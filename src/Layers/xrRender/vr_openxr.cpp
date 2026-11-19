#include "stdafx.h"

// GammaVR Phase B, milestone 1: OpenXR bring-up (DX11 only).
// Enabled by the command line switch -vr_openxr. Without it nothing here runs and openxr_loader.dll is never loaded.
// What it does: creates an OpenXR instance, system and D3D11 session, one swapchain per eye, and every engine frame
// runs xrWaitFrame/xrBeginFrame/xrEndFrame with one projection layer of two views, logging the head pose.
// The eye images are only cleared to a flat colour here; rendering the game into them is the next step.
// The loader is bound at run time (LoadLibrary), so the engine build needs only the OpenXR headers.
#ifdef USE_DX11

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include "../../3rd party/openxr/include/openxr/openxr.h"
#include "../../3rd party/openxr/include/openxr/openxr_platform.h"

namespace
{
	struct vr_eye
	{
		XrSwapchain chain = XR_NULL_HANDLE;
		int32_t w = 0, h = 0;
		xr_vector<ID3D11RenderTargetView*> rtv;
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

		XrViewConfigurationView vcv[2] = { { XR_TYPE_VIEW_CONFIGURATION_VIEW }, { XR_TYPE_VIEW_CONFIGURATION_VIEW } };
		uint32_t n = 0;
		if (!vr_check(vr.EnumerateViewConfigurationViews(vr.instance, vr.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &n, vcv),
			"xrEnumerateViewConfigurationViews") || n != 2)
			return false;

		int64_t formats[64];
		uint32_t nf = 0;
		if (!vr_check(vr.EnumerateSwapchainFormats(vr.session, 64, &nf, formats), "xrEnumerateSwapchainFormats") || !nf)
			return false;
		int64_t pick = formats[0];
		for (uint32_t i = 0; i < nf; ++i)
			if (formats[i] == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) { pick = formats[i]; break; }
		vr.format = (DXGI_FORMAT)pick;

		for (int e = 0; e < 2; ++e)
		{
			vr_eye& E = vr.eye[e];
			E.w = (int32_t)vcv[e].recommendedImageRectWidth;
			E.h = (int32_t)vcv[e].recommendedImageRectHeight;
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
			{
				D3D11_RENDER_TARGET_VIEW_DESC rd = {};
				rd.Format = vr.format;
				rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				ID3D11RenderTargetView* v = nullptr;
				HRESULT hr = HW.pDevice->CreateRenderTargetView(img[i].texture, &rd, &v);
				if (FAILED(hr))
				{
					Msg("! [vr] CreateRenderTargetView on eye %d image %u failed: 0x%08x", e, i, (u32)hr);
					return false;
				}
				E.rtv.push_back(v);
			}
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

// Called once per engine frame from dxRenderDeviceRender::End(), on the thread that owns the D3D11 context.
void vr_openxr_frame()
{
	static const bool enabled = !!strstr(Core.Params, "-vr_openxr");
	if (!enabled)
		return;
	if (!vr.tried)
	{
		vr.tried = true;
		vr.ok = vr_init();
		Msg(vr.ok ? "* [vr] OpenXR bring-up complete" : "! [vr] OpenXR bring-up FAILED; VR path disabled for this run");
	}
	if (!vr.ok)
		return;

	vr_events();
	if (!vr.running)
		return;

	XrFrameWaitInfo wi = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameState fs = { XR_TYPE_FRAME_STATE };
	if (XR_FAILED(vr.WaitFrame(vr.session, &wi, &fs)))
	{
		vr.frames_fail++; vr.frames_run = 0;
		return;
	}
	XrFrameBeginInfo bi = { XR_TYPE_FRAME_BEGIN_INFO };
	if (XR_FAILED(vr.BeginFrame(vr.session, &bi)))
	{
		vr.frames_fail++; vr.frames_run = 0;
		return;
	}

	XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
	XrViewState vs = { XR_TYPE_VIEW_STATE };
	XrViewLocateInfo li = { XR_TYPE_VIEW_LOCATE_INFO };
	li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	li.displayTime = fs.predictedDisplayTime;
	li.space = vr.local;
	uint32_t nv = 0;
	XrResult rv = vr.LocateViews(vr.session, &li, &vs, 2, &nv, views);
	XrSpaceLocation hl = { XR_TYPE_SPACE_LOCATION };
	XrResult rh = vr.LocateSpace(vr.head, vr.local, fs.predictedDisplayTime, &hl);

	bool layer_ok = fs.shouldRender && XR_SUCCEEDED(rv) && nv == 2;
	XrCompositionLayerProjectionView pv[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
	if (layer_ok)
	{
		for (int e = 0; e < 2; ++e)
		{
			vr_eye& E = vr.eye[e];
			uint32_t idx = 0;
			XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
			XrSwapchainImageWaitInfo swi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			swi.timeout = XR_INFINITE_DURATION;
			XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
			if (XR_FAILED(vr.AcquireSwapchainImage(E.chain, &ai, &idx)) || XR_FAILED(vr.WaitSwapchainImage(E.chain, &swi)))
			{
				layer_ok = false;
				break;
			}
			// placeholder eye image: left eye dark red, right eye dark blue
			const float col[2][4] = { { 0.25f, 0.f, 0.f, 1.f }, { 0.f, 0.f, 0.25f, 1.f } };
			if (idx < E.rtv.size())
				HW.pContext->ClearRenderTargetView(E.rtv[idx], col[e]);
			if (XR_FAILED(vr.ReleaseSwapchainImage(E.chain, &ri)))
				layer_ok = false;
			pv[e].pose = views[e].pose;
			pv[e].fov = views[e].fov;
			pv[e].subImage.swapchain = E.chain;
			pv[e].subImage.imageRect.offset.x = 0;
			pv[e].subImage.imageRect.offset.y = 0;
			pv[e].subImage.imageRect.extent.width = E.w;
			pv[e].subImage.imageRect.extent.height = E.h;
			pv[e].subImage.imageArrayIndex = 0;
		}
	}

	XrCompositionLayerProjection proj = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	proj.space = vr.local;
	proj.viewCount = 2;
	proj.views = pv;
	const XrCompositionLayerBaseHeader* layers[1] = { (const XrCompositionLayerBaseHeader*)&proj };
	XrFrameEndInfo ei = { XR_TYPE_FRAME_END_INFO };
	ei.displayTime = fs.predictedDisplayTime;
	ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	ei.layerCount = layer_ok ? 1 : 0;
	ei.layers = layer_ok ? layers : nullptr;
	XrResult re = vr.EndFrame(vr.session, &ei);

	if (re == XR_SUCCESS && layer_ok)
	{
		vr.frames_ok++; vr.frames_run++;
	}
	else
	{
		vr.frames_fail++; vr.frames_run = 0;
	}

	// one line per frame: the milestone 1 test reads these
	Msg("* [vr] frame ok=%u run=%u fail=%u end=%d layer=%d t=%lld head_r=%d flags=0x%x pos %.5f %.5f %.5f quat %.6f %.6f %.6f %.6f",
		vr.frames_ok, vr.frames_run, vr.frames_fail, (int)re, layer_ok ? 1 : 0, (long long)fs.predictedDisplayTime, (int)rh,
		(u32)hl.locationFlags, hl.pose.position.x, hl.pose.position.y, hl.pose.position.z,
		hl.pose.orientation.x, hl.pose.orientation.y, hl.pose.orientation.z, hl.pose.orientation.w);
}

#endif // USE_DX11
