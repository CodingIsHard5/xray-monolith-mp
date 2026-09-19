////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_dynamic_object.cpp
//	Created 	: 27.10.2005
//  Modified 	: 27.10.2005
//	Author		: Dmitriy Iassenev
//	Description : ALife dynamic object class
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "xrServer_Objects_ALife.h"
#include "xrServer_Objects_ALife_Monsters.h"   // CSE_ALifeMonsterAbstract::m_group_id (the watched population)
#include "alife_simulator.h"
#include "mp_anchors.h"
#include "alife_schedule_registry.h"
#include "alife_graph_registry.h"
#include "alife_object_registry.h"
#include "level_graph.h"
#include "game_level_cross_table.h"
#include "game_graph.h"
#include "xrServer.h"
#include "level.h"
#include "map_manager.h"

void CSE_ALifeDynamicObject::on_spawn()
{
#ifdef DEBUG
	//	Msg			("[LSS] spawning object [%d][%d][%s][%s]",ID,ID_Parent,name(),name_replace());
#endif
}

void CSE_ALifeDynamicObject::on_register()
{
	CSE_ALifeObject* object = this;
	while (object->ID_Parent != ALife::_OBJECT_ID(-1))
	{
		object = ai().alife().objects().object(object->ID_Parent);
		VERIFY(object);
	}

	if (!alife().graph().level().object(object->ID, true))
		clear_client_data();

    ::luabind::functor<void> funct;
    if (ai().script_engine().functor("_G.CSE_ALifeDynamicObject_on_register", funct))
        funct((u16)ID);
}

void CSE_ALifeDynamicObject::on_before_register()
{
}

void CSE_ALifeDynamicObject::on_unregister()
{
	::luabind::functor<void> funct;
	if (ai().script_engine().functor("_G.CSE_ALifeDynamicObject_on_unregister", funct))
		funct((u16)ID);
	Level().MapManager().OnObjectDestroyNotify(ID);
}

void CSE_ALifeDynamicObject::switch_online()
{
	R_ASSERT(!m_bOnline);
	m_bOnline = true;
	alife().add_online(this);
}

void CSE_ALifeDynamicObject::switch_offline()
{
	R_ASSERT(m_bOnline);
	m_bOnline = false;
	alife().remove_online(this);

	clear_client_data();
}

void CSE_ALifeDynamicObject::add_online(const bool& update_registries)
{
	if (!update_registries)
		return;

	alife().scheduled().remove(this);
	alife().graph().remove(this, m_tGraphID, false);
}

void CSE_ALifeDynamicObject::add_offline(const xr_vector<ALife::_OBJECT_ID>& saved_children,
                                         const bool& update_registries)
{
	if (!update_registries)
		return;

	alife().scheduled().add(this);
	alife().graph().add(this, m_tGraphID, false);
}

// WHICH OBJECT IS WATCHED.
//
// -coop_watch_id <id>   one object by id.
// -coop_watch_members   every member of an online/offline group.
//
// The id form alone is not usable for this measurement: the squad is created by the fixture at RUNTIME, so its
// member ids are not known when the server is launched, and a flag that has to be set before the subject exists
// cannot watch it. The membership form is the actual population of interest and needs nothing known in advance.
//
// Cost: with neither flag the test is one integer compare against -1, which no ID matches. The smart_cast is
// paid only when -coop_watch_members is given, because try_switch_online runs for every offline object in the
// level registry on every scan tick and this file already carries a comment about a per-call cost on this path.
static int coop_watch_id()
{
	static int s_watch = -2;
	if (s_watch == -2)
	{
		s_watch = -1;
		LPCSTR const p = strstr(Core.Params, "-coop_watch_id");
		if (p)
			sscanf(p + xr_strlen("-coop_watch_id"), "%d", &s_watch);
	}
	return s_watch;
}

static bool coop_watch_members()
{
	static int s_members = -1;
	if (s_members < 0)
		s_members = strstr(Core.Params, "-coop_watch_members") ? 1 : 0;
	return (s_members == 1);
}

// true if THIS object is the one under observation.
// Non-static: the registry trace in alife_level_registry_inline.h needs it (same pattern as coop_functor_missing).
bool coop_is_watched(const CSE_ALifeDynamicObject* object)
{
	int const wid = coop_watch_id();
	if (wid >= 0)
		return ((int)object->ID == wid);
	if (!coop_watch_members())
		return false;
	const CSE_ALifeMonsterAbstract* const monster = smart_cast<const CSE_ALifeMonsterAbstract*>(object);
	return (monster && (monster->m_group_id != 0xffff));
}

bool CSE_ALifeDynamicObject::synchronize_location()
{
	// MP fork, item (3): does A-Life OVERRULE a co-op placement? `placenear` assigns o_Position directly, and
	// switch_object runs this function BEFORE try_switch_online. If this snapped the position back to the
	// NPC's job vertex, every placement-based arm in this item has been reporting a value that was reverted
	// before anything read it. Reading says it does NOT snap back — it follows o_Position and recomputes
	// m_tNodeID/m_tGraphID from it — so this trace exists to REFUTE that hypothesis with values rather than
	// leave it standing on my reading. Each early return is named, because "returned true" is three different
	// situations with different consequences.
	bool const coop_watch = coop_is_watched(this);
	Fvector const coop_pos_in = o_Position;
	u32 const coop_node_in = m_tNodeID;
	GameGraph::_GRAPH_ID const coop_graph_in = m_tGraphID;

	if (!ai().level_graph().valid_vertex_id(m_tNodeID))
	{
		if (coop_watch)
			Msg("[SYNCLOC] %d RET-false invalid node=%d (switch_object returns early, try_switch_online NOT reached)",
				ID, (int)m_tNodeID);
		return false;
	}

	if (!ai().level_graph().valid_vertex_position(o_Position) || ai().level_graph().inside(
		ai().level_graph().vertex(m_tNodeID),
		o_Position))
	{
		if (coop_watch)
			Msg("[SYNCLOC] %d RET-true-A pos unchanged %.1f,%.1f,%.1f (valid_pos=%d inside_node=%d) node=%d graph=%d",
				ID, VPUSH(o_Position), ai().level_graph().valid_vertex_position(o_Position) ? 1 : 0,
				ai().level_graph().inside(ai().level_graph().vertex(m_tNodeID), o_Position) ? 1 : 0,
				(int)m_tNodeID, (int)m_tGraphID);
		return (true);
	}

	u32 const new_vertex_id = ai().level_graph().vertex(m_tNodeID, o_Position);
	if (!m_bOnline && !ai().level_graph().inside(new_vertex_id, o_Position))
	{
		if (coop_watch)
			Msg("[SYNCLOC] %d RET-true-B offline and pos %.1f,%.1f,%.1f not inside nearest vertex %d — "
				"node/graph LEFT STALE at %d/%d",
				ID, VPUSH(o_Position), (int)new_vertex_id, (int)coop_node_in, (int)coop_graph_in);
		return (true);
	}

	m_tNodeID = new_vertex_id;
	GameGraph::_GRAPH_ID tGraphID = ai().cross_table().vertex(m_tNodeID).game_vertex_id();
	if (tGraphID != m_tGraphID)
	{
		if (!m_bOnline)
		{
			Fvector position = o_Position;
			u32 level_vertex_id = m_tNodeID;
			alife().graph().change(this, m_tGraphID, tGraphID);
			if (ai().level_graph().inside(ai().level_graph().vertex(level_vertex_id), position))
			{
				level_vertex_id = m_tNodeID;
				o_Position = position;
			}
		}
		else
		{
			VERIFY(ai().game_graph().vertex(tGraphID)->level_id() == alife().graph().level().level_id());
			m_tGraphID = tGraphID;
		}
	}

	m_fDistance = ai().cross_table().vertex(m_tNodeID).distance();

	if (coop_watch)
	{
		bool const moved = !coop_pos_in.similar(o_Position);
		Msg("[SYNCLOC] %d RET-true-C pos %.1f,%.1f,%.1f -> %.1f,%.1f,%.1f (%s) node %d -> %d graph %d -> %d",
			ID, VPUSH(coop_pos_in), VPUSH(o_Position), moved ? "POSITION REWRITTEN BY A-LIFE" : "position kept",
			(int)coop_node_in, (int)m_tNodeID, (int)coop_graph_in, (int)m_tGraphID);
	}
	return (true);
}

// MP fork, item (3): WHICH EXIT does a grouped member take when it fails to come back online?
//
// A member switched offline by CALifeUpdateManager::teleport_object is put BACK into the scheduled and graph
// registries (remove_online defaults update_registries=true), so the ordinary individual path below should
// re-online it when a player is near. Measured, it never does: across the 27 evidence runs that sampled a
// member's online state there is not one 0->1 recovery, and across the 10 carrying [SQSPAWN] no member was
// ever spawned twice. Nothing on this route overrides anything for group membership, so the reason is one of
// the four exits — and none of them was instrumented. [SQVISIT] is the GROUP's try_switch_online, not this
// one, and reading its zero as a statement about a member is a mistake already made once in this item.
//
void CSE_ALifeDynamicObject::try_switch_online()
{
	bool const coop_watch = coop_is_watched(this);
	if (coop_watch)
	{
		Msg("[SWON] %d ENTER online=%d pos=%.1f,%.1f,%.1f graph=%d node=%d",
			ID, m_bOnline ? 1 : 0, VPUSH(o_Position), (int)m_tGraphID, (int)m_tNodeID);
	}
	CSE_ALifeSchedulable* schedulable = smart_cast<CSE_ALifeSchedulable*>(this);
	// checking if the abstract monster has just died
	if (schedulable)
	{
		if (!schedulable->need_update(this))
		{
			if (alife().scheduled().object(ID, true))
				alife().scheduled().remove(this);
		}
		else if (!alife().scheduled().object(ID, true))
			alife().scheduled().add(this);
	}

	if (!can_switch_online())
	{
		if (coop_watch)
			Msg("[SWON] %d EXIT-A can_switch_online=0 (stays offline)", ID);
		on_failed_switch_online();
		return;
	}

	if (!can_switch_offline())
	{
		if (coop_watch)
			Msg("[SWON] %d EXIT-B can_switch_offline=0 -> switch_online", ID);
		alife().switch_online(this);
		return;
	}

	// MP fork (§5.1): min distance to ANY attention anchor; with no
	// anchors registered this degrades to the legacy actor distance
	// Hoisted into a named value ONLY so it can be logged; the comparison below is unchanged.
	float const coop_d = mp_anchors::min_distance_to(o_Position, alife().graph().actor()->o_Position);
	if (coop_d > alife().online_distance())
	{
		if (coop_watch)
		{
			Msg("[SWON] %d EXIT-C too far: d=%.1f > online_dist=%.1f (anchors=%d players=%d) pos=%.1f,%.1f,%.1f",
				ID, coop_d, alife().online_distance(), mp_anchors::count(), mp_anchors::player_count(),
				VPUSH(o_Position));
		}
		on_failed_switch_online();
		return;
	}

	if (coop_watch)
		Msg("[SWON] %d EXIT-D -> switch_online d=%.1f <= online_dist=%.1f", ID, coop_d, alife().online_distance());
	alife().switch_online(this);
}

void CSE_ALifeDynamicObject::try_switch_offline()
{
	// MP fork: this trace pins down which check keeps an object online (boot 19/20: the second
	// offline flip after a round trip never happens).
	//
	// ITS ORIGINAL JUSTIFICATION WAS WRONG, AND IT WAS MEASURED WRONG ON 2026-07-31 (§5h). The
	// comment here read "online set is tiny, so tracing every offline decision under -dbg is
	// cheap". It is not tiny and it is not cheap: this call site emitted **1,488,943 lines in a
	// 900-second run — about 1650 a second — which was 98% of every line the server logged.**
	//
	// That mattered far beyond noise, because `xrCore`'s `LogFile` retains every logged line
	// forever (§5g), so this trace WAS the server's memory leak: ~29 MB/min, reaching a 12 GiB cap
	// in about six hours. The unbounded container is a real defect on its own and is being fixed
	// separately — but the rate that made it look urgent came from here.
	//
	// Two changes, and the second is the one that matters:
	//  1. The flag lookup is resolved ONCE. It was a `strstr` over the whole command line executed
	//     per object per switch tick.
	//  2. It no longer rides on the blanket `-dbg`. `-dbg` is needed for script errors to be
	//     reported at all, so every diagnostic run had to accept 1650 lines/second of this to get
	//     them. It now has its OWN flag and is off unless asked for by name.
	static int s_switch_dbg = -1;
	if (s_switch_dbg < 0)
		s_switch_dbg = strstr(Core.Params, "-coop_dbg_switch") ? 1 : 0;
	bool const dbg = (s_switch_dbg == 1);

	if (!can_switch_offline())
	{
		if (dbg)
			Msg("[MPSW] [%d][%s] stays online: can_switch_offline=0 (match_configuration=%d)", ID, name_replace(), match_configuration() ? 1 : 0);
		return;
	}

	if (!can_switch_online())
	{
		if (dbg)
			Msg("[MPSW] [%d][%s] forced offline: can_switch_online=0", ID, name_replace());
		// MP fork, item (3): NAME THE CALLER that takes a member offline. Only a group's offline->online
		// transition ever respawns a member's game object, so whoever switches a member offline individually
		// has made it unrecoverable for the session. Gated on -coop_anchordump like the rest of the set.
		if (strstr(Core.Params, "-coop_anchordump"))
			Msg("[SQCALLER] switch_offline(object %d) from CSE_ALifeDynamicObject::try_switch_offline "
				"[can_switch_online==false branch]", ID);
		alife().switch_offline(this);
		return;
	}

	float const d = mp_anchors::min_distance_to(o_Position, alife().graph().actor()->o_Position);
	if (d <= alife().offline_distance())
	{
		if (dbg)
			Msg("[MPSW] [%d][%s] stays online: d=%.0f <= offline_dist=%.0f (anchors=%d)", ID, name_replace(), d, alife().offline_distance(), mp_anchors::count());
		return;
	}

	if (dbg)
		Msg("[MPSW] [%d][%s] going offline: d=%.0f > offline_dist=%.0f (anchors=%d)", ID, name_replace(), d, alife().offline_distance(), mp_anchors::count());
	// THE PREDICTED SITE. A member that has drifted back to its smart terrain is far from every anchor and is
	// switched offline HERE as an individual object — but its group stays online, and only the group's
	// offline->online transition respawns members, so nothing ever brings it back.
	if (strstr(Core.Params, "-coop_anchordump"))
		Msg("[SQCALLER] switch_offline(object %d) from CSE_ALifeDynamicObject::try_switch_offline "
			"[distance branch] d=%.1f offline_dist=%.1f anchors=%d",
			ID, d, alife().offline_distance(), mp_anchors::count());
	alife().switch_offline(this);
}

bool CSE_ALifeDynamicObject::redundant() const
{
	return (false);
}

/// ---------------------------- CSE_ALifeInventoryBox ---------------------------------------------

void CSE_ALifeInventoryBox::add_online(const bool& update_registries)
{
	CSE_ALifeDynamicObjectVisual* object = (this);

	NET_Packet tNetPacket;
	ClientID clientID;
	clientID.set(
		object->alife().server().GetServerClient() ? object->alife().server().GetServerClient()->ID.value() : 0);

	ALife::OBJECT_IT I = object->children.begin();
	ALife::OBJECT_IT E = object->children.end();
	for (; I != E; ++I)
	{
		CSE_ALifeDynamicObject* l_tpALifeDynamicObject = ai().alife().objects().object(*I);
		CSE_ALifeInventoryItem* l_tpALifeInventoryItem = smart_cast<CSE_ALifeInventoryItem*>(l_tpALifeDynamicObject);
		R_ASSERT2(l_tpALifeInventoryItem, "Non inventory item object has parent?!");
		l_tpALifeInventoryItem->base()->s_flags.or(M_SPAWN_UPDATE);
		CSE_Abstract* l_tpAbstract = smart_cast<CSE_Abstract*>(l_tpALifeInventoryItem);
		object->alife().server().entity_Destroy(l_tpAbstract);

#ifdef DEBUG
		//		if (psAI_Flags.test(aiALife))
//			Msg					("[LSS] Spawning item [%s][%s][%d]",l_tpALifeInventoryItem->base()->name_replace(),*l_tpALifeInventoryItem->base()->s_name,l_tpALifeDynamicObject->ID);
		Msg						(
			"[LSS][%d] Going online [%d][%s][%d] with parent [%d][%s] on '%s'",
			Device.dwFrame,
			Device.dwTimeGlobal,
			l_tpALifeInventoryItem->base()->name_replace(),
			l_tpALifeInventoryItem->base()->ID,
			ID,
			name_replace(),
			"*SERVER*"
		);
#endif

		l_tpALifeDynamicObject->o_Position = object->o_Position;
		l_tpALifeDynamicObject->m_tNodeID = object->m_tNodeID;
		object->alife().server().Process_spawn(tNetPacket, clientID,FALSE, l_tpALifeInventoryItem->base());
		l_tpALifeDynamicObject->s_flags.and(u16(-1) ^ M_SPAWN_UPDATE);
		l_tpALifeDynamicObject->m_bOnline = true;
	}

	CSE_ALifeDynamicObjectVisual::add_online(update_registries);
}

void CSE_ALifeInventoryBox::add_offline(const xr_vector<ALife::_OBJECT_ID>& saved_children,
                                        const bool& update_registries)
{
	CSE_ALifeDynamicObjectVisual* object = (this);

	for (u32 i = 0, n = saved_children.size(); i < n; ++i)
	{
		CSE_ALifeDynamicObject* child = smart_cast<CSE_ALifeDynamicObject*>(
			ai().alife().objects().object(saved_children[i], true));
		// R_ASSERT(child);
		if (!child)
		{
			Msg("[DO] can't switch child [%d] offline, it's null", saved_children[i]);
			continue;
		}
		child->m_bOnline = false;

		CSE_ALifeInventoryItem* inventory_item = smart_cast<CSE_ALifeInventoryItem*>(child);
		VERIFY2(inventory_item, "Non inventory item object has parent?!");
#ifdef DEBUG
		//		if (psAI_Flags.test(aiALife))
//			Msg					("[LSS] Destroying item [%s][%s][%d]",inventory_item->base()->name_replace(),*inventory_item->base()->s_name,inventory_item->base()->ID);
		Msg						(
			"[LSS][%d] Going offline [%d][%s][%d] with parent [%d][%s] on '%s'",
			Device.dwFrame,
			Device.dwTimeGlobal,
			inventory_item->base()->name_replace(),
			inventory_item->base()->ID,
			ID,
			name_replace(),
			"*SERVER*"
		);
#endif

		ALife::_OBJECT_ID item_id = inventory_item->base()->ID;
		inventory_item->base()->ID = object->alife().server().PerformIDgen(item_id);

		if (!child->can_save())
		{
			object->alife().release(child);
			--i;
			--n;
			continue;
		}
		child->clear_client_data();
		object->alife().graph().add(child, child->m_tGraphID, false);
		//		object->alife().graph().attach	(*object,inventory_item,child->m_tGraphID,true);
		alife().graph().remove(child, child->m_tGraphID);
		children.push_back(child->ID);
		child->ID_Parent = ID;
	}


	CSE_ALifeDynamicObjectVisual::add_offline(saved_children, update_registries);
}

void CSE_ALifeDynamicObject::clear_client_data()
{
#ifdef DEBUG
	if (!client_data.empty())
		Msg						("CSE_ALifeDynamicObject::switch_offline: client_data is cleared for [%d][%s]",ID,name_replace());
#endif // DEBUG
	if (!keep_saved_data_anyway())
		client_data.clear();
}

void CSE_ALifeDynamicObject::on_failed_switch_online()
{
	clear_client_data();
}
