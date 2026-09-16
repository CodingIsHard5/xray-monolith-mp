#include "stdafx.h"
#include "feel_vision.h"
#include "render.h"
#include "xr_object.h"
#include "xr_collide_form.h"
#include "igame_level.h"
#include "cl_intersect.h"
#include "device.h"
#include "../Include/xrRender/RenderVisual.h"
#include "../Include/xrRender/Kinematics.h"

ENGINE_API u16 g_coop_vistrace_owner = u16(-1);
ENGINE_API u16 g_coop_vistrace_owner2 = u16(-1);

static void coop_vistrace_obj(const char* what, CObject const* owner, CObject* O, u16 bone_id, const Fvector& cp)
{
	IKinematics* const K = O->Visual() ? O->Visual()->dcast_PKinematics() : nullptr;
	Msg("- COOP(vistrace): owner %u t %u %s target %u visual %d bones %d cform %d bone_id %d cp %.2f,%.2f,%.2f dist %.1f",
		owner->ID(), Device.dwTimeGlobal, what, O->ID(), O->Visual() ? 1 : 0, K ? int(K->LL_BoneCount()) : -1, O->CFORM() ? 1 : 0,
		bone_id == u16(-1) ? -1 : int(bone_id), cp.x, cp.y, cp.z, owner->Position().distance_to(O->Position()));
}

namespace Feel
{
	Vision::Vision(CObject const* owner) :
		pure_relcase(&Vision::feel_vision_relcase),
		m_owner(owner)
	{
	}

	Vision::~Vision()
	{
	}

	struct SFeelParam
	{
		Vision* parent;
		Vision::feel_visible_Item* item;
		float vis;
		float vis_threshold;
		CObject* coop_blocker = nullptr;   // MP fork (§3.4 trace): the first object whose transparency cut vis (nullptr = geometry)
		bool coop_geometry = false;

		SFeelParam(Vision* _parent, Vision::feel_visible_Item* _item, float _vis_threshold) : parent(_parent),
		                                                                                      item(_item), vis(1.f),
		                                                                                      vis_threshold(
			                                                                                      _vis_threshold)
		{
		}
	};

	IC BOOL feel_vision_callback(collide::rq_result& result, LPVOID params)
	{
		SFeelParam* fp = (SFeelParam*)params;
		float vis = fp->parent->feel_vision_mtl_transp(result.O, result.element);
		fp->vis *= vis;
		// the hit that takes visibility to or below the threshold names the blocker (CodeRabbit: not merely the first partial one)
		if (fp->vis <= fp->vis_threshold && !fp->coop_blocker && !fp->coop_geometry)
		{
			if (result.O) fp->coop_blocker = result.O;
			else fp->coop_geometry = true;
		}
		if (NULL == result.O && fis_zero(vis))
		{
			CDB::TRI* T = g_pGameLevel->ObjectSpace.GetStaticTris() + result.element;
			Fvector* V = g_pGameLevel->ObjectSpace.GetStaticVerts();
			fp->item->Cache.verts[0].set(V[T->verts[0]]);
			fp->item->Cache.verts[1].set(V[T->verts[1]]);
			fp->item->Cache.verts[2].set(V[T->verts[2]]);
		}
		return (fp->vis > fp->vis_threshold);
	}

#ifdef SPATIAL_CHANGE
	IC BOOL feel_vision_test_callback(const collide::ray_defs &rd, CObject *object, LPVOID user_data)
	{
		/* Return FALSE to see through object. */
		if (object->spatial.type & STYPE_FEELVISIONIGNORE)
		{
			return FALSE;
		}
		return TRUE;
	}
#endif

	void Vision::o_new(CObject* O)
	{
		feel_visible.push_back(feel_visible_Item());
		feel_visible_Item& I = feel_visible.back();
		I.O = O;
		I.Cache_vis = 1.f;
		I.Cache.verts[0].set(0, 0, 0);
		I.Cache.verts[1].set(0, 0, 0);
		I.Cache.verts[2].set(0, 0, 0);
		I.fuzzy = -EPS_S;
		I.cp_LP = O->get_new_local_point_on_mesh(I.bone_id);
		I.cp_LAST = O->get_last_local_point_on_mesh(I.cp_LP, I.bone_id);
		if (m_owner && coop_vistrace_is(m_owner->ID()))
			coop_vistrace_obj("new", m_owner, O, I.bone_id, I.cp_LP);
	}

	void Vision::o_delete(CObject* O)
	{
		if (m_owner && coop_vistrace_is(m_owner->ID()))
			Msg("- COOP(vistrace): owner %u t %u delete target %u", m_owner->ID(), Device.dwTimeGlobal, O->ID());
		xr_vector<feel_visible_Item>::iterator I = feel_visible.begin(), TE = feel_visible.end();
		for (; I != TE; I++)
			if (I->O == O)
			{
				feel_visible.erase(I);
				return;
			}
	}

	void Vision::feel_vision_clear()
	{
		seen.clear();
		query.clear();
		diff.clear();
		feel_visible.clear();
	}

	void Vision::feel_vision_relcase(CObject* object)
	{
		xr_vector<CObject*>::iterator Io;
		Io = std::find(seen.begin(), seen.end(), object);
		if (Io != seen.end()) seen.erase(Io);
		Io = std::find(query.begin(), query.end(), object);
		if (Io != query.end())query.erase(Io);
		Io = std::find(diff.begin(), diff.end(), object);
		if (Io != diff.end()) diff.erase(Io);
		xr_vector<feel_visible_Item>::iterator Ii = feel_visible.begin(), IiE = feel_visible.end();
		for (; Ii != IiE; ++Ii)if (Ii->O == object)
		{
			feel_visible.erase(Ii);
			break;
		}
	}

	void Vision::feel_vision_query(Fmatrix& mFull, Fvector& P)
	{
		CFrustum Frustum;
		Frustum.CreateFromMatrix(mFull, FRUSTUM_P_LRTB | FRUSTUM_P_FAR);

		// Traverse object database
		r_spatial.clear_not_free();
		g_SpatialSpace->q_frustum
		(
			r_spatial,
			0,
			STYPE_VISIBLEFORAI,
			Frustum
		);

		// Determine visibility for dynamic part of scene
		seen.clear_and_reserve();
		for (u32 o_it = 0; o_it < r_spatial.size(); o_it++)
		{
			ISpatial* spatial = r_spatial[o_it];
			CObject* object = spatial->dcast_CObject();
			if (object && feel_vision_isRelevant(object)) seen.push_back(object);
		}
		if (seen.size() > 1)
		{
			std::sort(seen.begin(), seen.end());
			xr_vector<CObject*>::iterator end = std::unique(seen.begin(), seen.end());
			if (end != seen.end()) seen.erase(end, seen.end());
		}
	}

	void Vision::feel_vision_update(CObject* parent, Fvector& P, float dt, float vis_threshold)
	{
		// B-A = objects, that become visible
		if (!seen.empty())
		{
			xr_vector<CObject*>::iterator E = std::remove(seen.begin(), seen.end(), parent);
			seen.resize(E - seen.begin());

			{
				diff.resize(_max(seen.size(), query.size()));
				xr_vector<CObject*>::iterator E = std::set_difference(
					seen.begin(), seen.end(),
					query.begin(), query.end(),
					diff.begin());
				diff.resize(E - diff.begin());
				for (u32 i = 0; i < diff.size(); i++)
					o_new(diff[i]);
			}
		}

		// A-B = objects, that are invisible
		if (!query.empty())
		{
			diff.resize(_max(seen.size(), query.size()));
			xr_vector<CObject*>::iterator E = std::set_difference(
				query.begin(), query.end(),
				seen.begin(), seen.end(),
				diff.begin());
			diff.resize(E - diff.begin());
			for (u32 i = 0; i < diff.size(); i++)
				o_delete(diff[i]);
		}

		// Copy results and perform traces
		query = seen;
		o_trace(P, dt, vis_threshold);
	}

	void Vision::o_trace(Fvector& P, float dt, float vis_threshold)
	{
		struct coop_trace_dump
		{
			Vision* v;
			~coop_trace_dump()
			{
				static u32 s_next[2] = {0, 0};   // one rate limit per traced owner
				if (!v->m_owner || !coop_vistrace_is(v->m_owner->ID()))
					return;
				const int k = v->m_owner->ID() == g_coop_vistrace_owner ? 0 : 1;
				if (Device.dwTimeGlobal < s_next[k])
					return;
				s_next[k] = Device.dwTimeGlobal + 1000;
				Msg("- COOP(vistrace): owner %u t %u items %u seen %u query %u", v->m_owner->ID(), Device.dwTimeGlobal,
					u32(v->feel_visible.size()), u32(v->seen.size()), u32(v->query.size()));
				for (xr_vector<feel_visible_Item>::iterator it = v->feel_visible.begin(); it != v->feel_visible.end(); ++it)
					Msg("- COOP(vistrace): owner %u t %u item target %u fuzzy %.3f cform %d dist %.1f", v->m_owner->ID(), Device.dwTimeGlobal,
						it->O->ID(), it->fuzzy, it->O->CFORM() ? 1 : 0, v->m_owner->Position().distance_to(it->O->Position()));
			}
		} coop_dump = { this };
		RQR.r_clear();
		xr_vector<feel_visible_Item>::iterator I = feel_visible.begin(), E = feel_visible.end();
		for (; I != E; I++)
		{
			if (0 == I->O->CFORM())
			{
				I->fuzzy = -1;
				continue;
			}

			// verify relation
			// if (positive(I->fuzzy) && I->O->Position().similar(I->cp_LR_dst,lr_granularity) && P.similar(I->cp_LR_src,lr_granularity))
			// continue;

			I->cp_LR_dst = I->O->Position();
			I->cp_LR_src = P;
			I->cp_LAST = I->O->get_last_local_point_on_mesh(I->cp_LP, I->bone_id);

			//
			Fvector D, OP = I->cp_LAST;
			D.sub(OP, P);
			if (fis_zero(D.magnitude()))
			{
				I->fuzzy = 1.f;
				continue;
			}

			float f = D.magnitude() + .2f;
			if (f > fuzzy_guaranteed)
			{
				D.div(f);
				// setup ray defs & feel params
				collide::ray_defs RD(P, D, f, CDB::OPT_CULL,
				                     collide::rq_target(
					                     collide::rqtStatic | /**/collide::rqtObject | /**/collide::rqtObstacle));
				SFeelParam feel_params(this, &*I, vis_threshold);
				// check cache
				if (I->Cache.result && I->Cache.similar(P, D, f))
				{
					// similar with previous query
					feel_params.vis = I->Cache_vis;
					// Log("cache 0");
				}
				else
				{
					float _u, _v, _range;
					if (CDB::TestRayTri(P, D, I->Cache.verts, _u, _v, _range, false) && (_range > 0 && _range < f))
					{
						feel_params.vis = 0.f;
						// Log("cache 1");
					}
					else
					{
						// cache outdated. real query.
						VERIFY(!fis_zero(RD.dir.magnitude()));

#ifdef SPATIAL_CHANGE
					    if (g_pGameLevel->ObjectSpace.RayQuery(RQR, RD, feel_vision_callback, &feel_params, feel_vision_test_callback, const_cast<CObject*>(m_owner)))
#else
					    if (g_pGameLevel->ObjectSpace.RayQuery(RQR, RD, feel_vision_callback, &feel_params, NULL, const_cast<CObject*>(m_owner)))
#endif
						{
							I->Cache_vis = feel_params.vis;
							I->Cache.set(P, D, f, TRUE);
						}
						else
						{
							// feel_params.vis = 0.f;
							// I->Cache_vis = feel_params.vis ;
							I->Cache.set(P, D, f, FALSE);
						}
						// Log("query");
					}
				}
				// Log("Vis",feel_params.vis);
				r_spatial.clear_not_free();
				g_SpatialSpace->q_ray(r_spatial, 0, STYPE_VISIBLEFORAI, P, D, f);

				RD.flags = CDB::OPT_ONLYFIRST;

				bool collision_found = false;
				CObject const* coop_collider = nullptr;
				xr_vector<ISpatial*>::const_iterator i = r_spatial.begin();
				xr_vector<ISpatial*>::const_iterator e = r_spatial.end();
				for (; i != e; ++i)
				{
					if (*i == m_owner)
						continue;

					if (*i == I->O)
						continue;

					CObject const* object = (*i)->dcast_CObject();

#ifdef SPATIAL_CHANGE
					if (object && object->spatial.type & STYPE_FEELVISIONIGNORE)
					{
						/* See through objects that have the flag. */
						continue;
					}
#endif

					RQR.r_clear();
					if (object && object->collidable.model && !object->collidable.model->_RayQuery(RD, RQR))
						continue;

					collision_found = true;
					coop_collider = object;
					break;
				}

				if (collision_found)
					feel_params.vis = 0.f;

				if (feel_params.vis < feel_params.vis_threshold && m_owner && coop_vistrace_is(m_owner->ID()))
				{
					// MP fork (§3.4 trace): name what blocked this ray — the q_ray collision object, else the RayQuery object whose
					// material cut visibility, else level geometry, else the cached triangle from an earlier blocked ray
					string128 by;
					if (coop_collider)
						xr_sprintf(by, "object %u %s (collision)", coop_collider->ID(), coop_collider->cNameSect_str());
					else if (feel_params.coop_blocker)
						xr_sprintf(by, "object %u %s (material)", feel_params.coop_blocker->ID(), feel_params.coop_blocker->cNameSect_str());
					else if (feel_params.coop_geometry)
						xr_strcpy(by, "level geometry");
					else
						xr_strcpy(by, "cached triangle (earlier geometry hit)");
					Msg("- COOP(vistrace): owner %u t %u ray target %u vis %.3f threshold %.3f dist %.1f blocked by %s", m_owner->ID(),
						Device.dwTimeGlobal, I->O->ID(), feel_params.vis, feel_params.vis_threshold, f, by);
				}
				if (feel_params.vis < feel_params.vis_threshold)
				{
					// INVISIBLE, choose next point
					I->fuzzy -= fuzzy_update_novis * dt;
					clamp(I->fuzzy, -.5f, 1.f);
					I->cp_LP = I->O->get_new_local_point_on_mesh(I->bone_id);
				}
				else
				{
					// VISIBLE
					I->fuzzy += fuzzy_update_vis * dt;
					clamp(I->fuzzy, -.5f, 1.f);
				}
			}
			else
			{
				// VISIBLE, 'cause near
				I->fuzzy += fuzzy_update_vis * dt;
				clamp(I->fuzzy, -.5f, 1.f);
			}
		}
	}
};
