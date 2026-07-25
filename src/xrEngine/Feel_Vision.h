#pragma once

#include "../xrcdb/xr_collide_defs.h"
#include "render.h"
#include "pure_relcase.h"

class IRender_Sector;
class CObject;
class ISpatial;

namespace Feel
{
	const float fuzzy_update_vis = 1000.f; // speed of fuzzy-logic desisions
	const float fuzzy_update_novis = 1000.f; // speed of fuzzy-logic desisions
	const float fuzzy_guaranteed = 0.001f; // distance which is supposed 100% visible
	const float lr_granularity = 0.1f; // assume similar positions

	class ENGINE_API Vision : private pure_relcase
	{
	private:
		xr_vector<CObject*> seen;
		xr_vector<CObject*> query;
		xr_vector<CObject*> diff;
		collide::rq_results RQR;
		xr_vector<ISpatial*> r_spatial;
		CObject const* m_owner;

		void o_new(CObject* E);
		void o_delete(CObject* E);
		void o_trace(Fvector& P, float dt, float vis_threshold);
	public:
		Vision(CObject const* owner);
		virtual ~Vision();

		struct feel_visible_Item
		{
			collide::ray_cache Cache;
			Fvector cp_LP;
			Fvector cp_LR_src;
			Fvector cp_LR_dst;
			Fvector cp_LAST; // last point found to be visible
			CObject* O;
			float fuzzy; // note range: (-1[no]..1[yes])
			float Cache_vis;
			u16 bone_id;
		};

		xr_vector<feel_visible_Item> feel_visible;
	public:
		void feel_vision_clear();
		void feel_vision_query(Fmatrix& mFull, Fvector& P);
		void feel_vision_update(CObject* parent, Fvector& P, float dt, float vis_threshold);
		void __stdcall feel_vision_relcase(CObject* object);

		void feel_vision_get(xr_vector<CObject*>& R)
		{
			R.clear();
			xr_vector<feel_visible_Item>::iterator I = feel_visible.begin(), E = feel_visible.end();
			for (; I != E; ++I) if (positive(I->fuzzy)) R.push_back(I->O);
		}

		// MP fork (§4C headless-visibility diag, -coop_vissdbg): read-only probes of the
		// internal frustum/trace state for one target object. `seen` is private, so expose
		// how many objects survived the q_frustum pass, whether a given target is among them,
		// and its o_trace fuzzy (potentially-visible list; -2 = never entered that list).
		u32 feel_vision_seen_count() const { return (u32)seen.size(); }
		bool feel_vision_in_frustum(CObject* O) const
		{
			for (xr_vector<CObject*>::const_iterator I = seen.begin(); I != seen.end(); ++I)
				if (*I == O) return true;
			return false;
		}
		float feel_vision_fuzzy_of(CObject* O) const
		{
			for (xr_vector<feel_visible_Item>::const_iterator I = feel_visible.begin(); I != feel_visible.end(); ++I)
				if (I->O == O) return I->fuzzy;
			return -2.f;
		}

		Fvector feel_vision_get_vispoint(CObject* _O)
		{
			xr_vector<feel_visible_Item>::iterator I = feel_visible.begin(), E = feel_visible.end();
			for (; I != E; ++I)
				if (_O == I->O)
				{
					VERIFY(positive(I->fuzzy));
					return I->cp_LAST;
				}
			VERIFY2(0, "There is no such object in the potentially visible list");
			return Fvector().set(flt_max, flt_max, flt_max);
		}

		virtual bool feel_vision_isRelevant(CObject* O) = 0;
		virtual float feel_vision_mtl_transp(CObject* O, u32 element) = 0;
	};
};
