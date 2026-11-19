#pragma once
// GammaVR: hooks between the level render loop (xrEngine) and the OpenXR code in the DX11 renderer (vr_openxr.cpp).
// Both pointers stay null unless the DX11 renderer is linked; the functions do nothing without -vr_openxr.
struct vr_eye_view
{
	Fvector offset;                    // eye position relative to the head, engine axes (x right, y up, z forward), metres
	float tan_l, tan_r, tan_u, tan_d;  // tangents of the runtime's field-of-view angles (left and down are negative)
};
// Starts the OpenXR frame and fills views[2]. False = no VR frame this engine frame (render the normal way).
extern ENGINE_API bool (*g_vr_begin_frame)(vr_eye_view* views);
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
