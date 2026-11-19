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
