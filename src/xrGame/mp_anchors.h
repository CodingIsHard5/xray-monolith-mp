////////////////////////////////////////////////////////////////////////////
// mp_anchors: attention anchors for the multiplayer A-Life server
// (design doc §5.1 — online/offline switching centres on "minimum
// distance to ANY real player", not THE actor).
//
// Empty registry = legacy behavior (distances measured to the actor
// entity), so single-player builds are unaffected. Anchors are fed by
// connected clients later; today a Lua export moves them for testing
// on the headless server. Implementation lives in
// alife_switch_manager.cpp (no new translation unit).
////////////////////////////////////////////////////////////////////////////
#pragma once

#include "../xrCore/_vector3d.h"

namespace mp_anchors
{
	static const u32 max_anchors = 16;

	void set(u32 idx, const Fvector& position); // add or move an anchor
	void clear(u32 idx);
	void clear_all();
	u32 count();

	// min distance from pos to any anchor; falls back to fallback_pos
	// (the actor entity) when no anchors are registered
	float min_distance_to(const Fvector& pos, const Fvector& fallback_pos);
}
