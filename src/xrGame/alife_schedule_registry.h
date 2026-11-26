////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_schedule_registry.h
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife schedule registry
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "safe_map_iterator.h"
#include "xrServer_Objects_ALife.h"
#include "ai_debug.h"
#include "profiler.h"

#ifdef XRCORE_PROFILER_AGG
// -det_mode (test-only) exact A-Life counters, defined in alife_simulator_base2.cpp, written by det_alife_counts
extern u32 g_det_alife_upd[65536];
extern volatile long g_det_alife_registered;
extern volatile long g_det_alife_unregistered;
#endif

class CALifeScheduleRegistry : public CSafeMapIterator<
		ALife::_OBJECT_ID, CSE_ALifeSchedulable, std::less<ALife::_OBJECT_ID>, false>
{
private:
	struct CUpdatePredicate
	{
		u32 m_count;
		mutable u32 m_current;

		IC CUpdatePredicate(const u32& count)
		{
			m_count = count;
			m_current = 0;
		}

		IC bool operator()(_iterator& i, u64 cycle_count, bool) const
		{
			if ((*i).second->m_schedule_counter == cycle_count)
				return (false);

			if (m_current >= m_count)
				return (false);

			++m_current;
			(*i).second->m_schedule_counter = cycle_count;

			return (true);
		}

		IC void operator()(_iterator& i, u64 cycle_count) const
		{
#ifdef XRCORE_PROFILER_AGG
			// measurement only: name every offline object whose update() takes 1 ms or more (hitch hunt, Cordon)
			CTimer perf_one;
			perf_one.Start();
			if (xr_det_mode())
				++g_det_alife_upd[(*i).first];
#endif
			START_PROFILE("ALife/scheduled/update")
				(*i).second->update();
			STOP_PROFILE
#ifdef XRCORE_PROFILER_AGG
			const float perf_ms = perf_one.GetElapsed_sec() * 1000.f;
			if (perf_ms >= 1.f)
			{
				const CSE_Abstract* perf_b = (*i).second->base();
				Msg("* alife_slow frame %u id %u %s [%s] %.2f ms", Device.dwFrame, u32((*i).first),
				    perf_b ? perf_b->name_replace() : "?", perf_b ? perf_b->s_name.c_str() : "?", perf_ms);
			}
#endif
		}
	};

protected:
	typedef CSafeMapIterator<ALife::_OBJECT_ID, CSE_ALifeSchedulable, std::less<ALife::_OBJECT_ID>, false> inherited;

protected:
	u32 m_objects_per_update;

public:
	IC CALifeScheduleRegistry();
	virtual ~CALifeScheduleRegistry();
	void add(CSE_ALifeDynamicObject* object);
	void remove(CSE_ALifeDynamicObject* object, bool no_assert = false);
	IC u32 update();
	IC u32 update(const u32& count); // B2b: one pass with an explicit object limit; returns objects updated
	IC CSE_ALifeSchedulable* object(const ALife::_OBJECT_ID& id, bool no_assert = false) const;
	IC const u32& objects_per_update() const;
	IC void objects_per_update(const u32& objects_per_update);
};

#include "alife_schedule_registry_inline.h"
