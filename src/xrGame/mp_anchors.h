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
//
// §5.2 graceful anchor loss: the registry is rebuilt from the connected
// players every frame, so when one of several anchors vanishes the others
// keep their regions live by construction. What the doc also asks is that
// an EMPTIED region falls back to offline simulation: once a player has
// been an anchor and the last one is gone, the fallback is no longer the
// save actor's spot (which is not a player) but "no one is near anything".
// Before the first player ever joins, the stock actor fallback stands, so
// boot is unchanged. The logic is the template below so it can be tested
// natively (dev/harness/native/test_mp_anchors.cpp, MP_ANCHORS_PURE_ONLY).
////////////////////////////////////////////////////////////////////////////
#pragma once

#ifndef MP_ANCHORS_PURE_ONLY
#include "../xrCore/_vector3d.h"
#endif

namespace mp_anchors
{
	static const unsigned max_anchors = 16;
	// MP fork (§4 A-Life squad decisions): indices [0, gamedata_anchor_base) are the per-actor anchors
	// that coop_update_anchors rewrites every frame; [gamedata_anchor_base, max_anchors) are PERSISTENT
	// anchors set from gamedata (e.g. to pin a populated smart online for the decision system). They
	// survive coop_update_anchors, which now clears only the actor range.
	static const unsigned gamedata_anchor_base = 8;

	// V needs distance_to(const V&) returning float
	template <class V>
	struct registry
	{
		// MP fork (item (3) diagnostics, 2026-09-18): `source` and `stale` are DIAGNOSTIC ONLY and take no part
		// in any decision — min_distance_to ignores them. source: -1 unknown/gamedata, else the client id that
		// fed the slot. stale: the feeding client's owner had no resolvable game object when the slot was written.
		struct anchor { V position; bool used; int source; bool stale; };
		anchor slots[max_anchors];
		unsigned n;
		bool had_player;      // a player anchor has existed at some point
		bool empty_offline;   // §5.2 policy on (the control flag turns it off)

		registry() : n(0), had_player(false), empty_offline(true)
		{
			for (unsigned i = 0; i < max_anchors; ++i) { slots[i].used = false; slots[i].source = -1; slots[i].stale = false; }
		}
		void set(unsigned idx, const V& p)
		{
			if (idx >= max_anchors) return;
			if (!slots[idx].used) ++n;
			slots[idx].position = p;
			slots[idx].used = true;
			if (idx < gamedata_anchor_base) had_player = true;
		}
		// diagnostic overload: same anchor, plus where it came from. Kept separate so the existing signature
		// (and dev/harness/native/test_mp_anchors.cpp, which drives it) is untouched.
		void set(unsigned idx, const V& p, int source, bool stale)
		{
			set(idx, p);
			if (idx >= max_anchors) return;
			slots[idx].source = source;
			slots[idx].stale = stale;
		}
		void clear(unsigned idx)
		{
			if (idx >= max_anchors || !slots[idx].used) return;
			slots[idx].used = false;
			--n;
		}
		void clear_range(unsigned from, unsigned to)
		{
			if (to > max_anchors) to = max_anchors;
			for (unsigned i = from; i < to; ++i) clear(i);
		}
		void clear_below(unsigned k) { clear_range(0, k); }
		void clear_all() { clear_below(max_anchors); }
		unsigned player_count() const
		{
			unsigned c = 0;
			for (unsigned i = 0; i < gamedata_anchor_base; ++i) c += slots[i].used ? 1 : 0;
			return c;
		}
		// true when the emptied-region rule applies: nothing registered, and a player anchor existed before
		bool emptied() const { return n == 0 && had_player && empty_offline; }
		// min distance from pos to any anchor; with none: "infinitely far" once emptied, else the fallback
		float min_distance_to(const V& pos, const V& fallback, float far_away) const
		{
			if (!n)
				return emptied() ? far_away : fallback.distance_to(pos);
			float best = far_away;
			for (unsigned i = 0; i < max_anchors; ++i)
				if (slots[i].used)
				{
					const float d = slots[i].position.distance_to(pos);
					if (d < best) best = d;
				}
			return best;
		}
	};

#ifndef MP_ANCHORS_PURE_ONLY
	void set(u32 idx, const Fvector& position); // add or move an anchor
	void set(u32 idx, const Fvector& position, int source, bool stale); // diagnostic: record where it came from
	// DIAGNOSTIC-ONLY readers for the anchor dump; none of these is consulted by any switch decision.
	bool slot_used(u32 idx);
	Fvector slot_position(u32 idx);
	int slot_source(u32 idx);
	bool slot_stale(u32 idx);
	void clear(u32 idx);
	void clear_all();
	void clear_below(u32 n);                     // clear only indices [0, n) — preserves persistent anchors
	void clear_range(u32 from, u32 to);          // clear [from, to): drop the slots a rebuild did not rewrite
	u32 count();
	u32 player_count();
	bool emptied();                              // §5.2: the last player anchor is gone
	void set_empty_offline(bool on);             // §5.2 control: false keeps the save actor's region live

	// min distance from pos to any anchor; falls back to fallback_pos
	// (the actor entity) when no anchors are registered — except once
	// emptied (§5.2), where it is flt_max so everything switches offline
	float min_distance_to(const Fvector& pos, const Fvector& fallback_pos);
#endif
}

#ifndef MP_ANCHORS_PURE_ONLY
// MP fork, item (3) diagnostics — DUMP ONLY, gated on -coop_anchordump, consulted by no decision.
// Defined in alife_switch_manager.cpp next to the registry it reads.
void coop_anchor_dump(const char* tag, u16 id, const Fvector& pos, const Fvector& fallback);
#endif
