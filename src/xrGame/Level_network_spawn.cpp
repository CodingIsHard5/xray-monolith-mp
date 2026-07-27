#include "pch_script.h"
#include "xrServer_Objects_ALife_All.h"
#include "level.h"
#include "game_cl_base.h"
#include "net_queue.h"
#include "ai_space.h"
#include "game_level_cross_table.h"
#include "level_graph.h"
#include "client_spawn_manager.h"
#include "alife_simulator.h"                       // MP fork: set_client_actor
#include "../xrNetServer/xr_enet_transport.h"      // MP fork: xr_enet::enabled()
#include "../xrEngine/xr_object.h"
#include "../xrEngine/IGame_Persistent.h"
#include "entity_alive.h"                          // MP fork (§14 step 7 P4 D2): actor-spawn diag

void CLevel::cl_Process_Spawn(NET_Packet& P)
{
	// Begin analysis
	shared_str s_name;
	P.r_stringZ(s_name);

	//Msg("cl_Process_Spawn spawning %s", s_name.c_str());

	// Create DC (xrSE)
	CSE_Abstract* E = F_entity_Create(*s_name);
	R_ASSERT2(E, *s_name);


	E->Spawn_Read(P);
	if (E->s_flags.is(M_SPAWN_UPDATE))
		E->UPDATE_Read(P);

	// MP fork (§ world-NPC-replication Phase 1): -coop_npcdiag — does an ambient A-Life NPC spawn
	// ever REACH this co-op client? Log every NON-actor creature spawn received (id/section/flags);
	// its net_Spawn success/failure shows up as g_sv_Spawn's own "Failed to spawn entity" line for the
	// same section. Diagnostic only, gated. See dev/WORLD_NPC_REPLICATION_PLAN.md.
	static int s_npcdiag = -2; // -2 unparsed, -1 off, 1 on
	if (s_npcdiag == -2)
		s_npcdiag = strstr(Core.Params, "-coop_npcdiag") ? 1 : -1;
	if (s_npcdiag == 1 && smart_cast<CSE_ALifeCreatureAbstract*>(E)
		&& !smart_cast<CSE_ALifeCreatureActor*>(E))
	{
		Msg("~ COOP_NPCDIAG: RX creature spawn id=%u section='%s' flags=0x%x update=%d",
			E->ID, *s_name, E->s_flags.flags, E->s_flags.is(M_SPAWN_UPDATE) ? 1 : 0);
		FlushLog();
	}

	if (!E->match_configuration())
	{
		F_entity_Destroy(E);
		return;
	}

	// MP fork (§14 co-op): the co-op actor is delivered to its OWNER twice — once as
	// our own player (LOCAL+ASPLAYER via spawn_end/Process_spawn), then again as a normal
	// A-Life online object (stripped). The second creates a static "ghost" duplicate of
	// ourselves at spawn that renders as a third-person body. Our own ASPLAYER copy
	// arrives first, so drop any later actor spawn whose id we already have.
	if (xr_enet::enabled() && !ai().get_alife()
		&& smart_cast<CSE_ALifeCreatureActor*>(E) && Objects.net_Find(E->ID))
	{
		Msg("- XRNET(diag): DROPPING duplicate actor spawn id=%u (already present)", E->ID);
		FlushLog();
		F_entity_Destroy(E);
		return;
	}
	//-------------------------------------------------
	//.	Msg ("M_SPAWN - %s[%d][%x] - %d %d", *s_name,  E->ID, E,E->ID_Parent, Device.dwFrame);
	//-------------------------------------------------
	//force object to be local for server client
	if (OnServer())
	{
		E->s_flags.set(M_SPAWN_OBJECT_LOCAL, TRUE);
	};

	// MP fork (§14 co-op thin client): snapshot the actor's CSE into the inert
	// client simulator so Lua alife():actor() resolves, BEFORE g_sv_Spawn runs
	// the actor's binders (actor_proxy, actor_binder), which call alife():actor().
	// E is fully read here and freed just below, so we must clone it now.
	// Co-op client only; set_client_actor is once-per-level. (ASPLAYER is not
	// usable — Spawn_Write strips it for remote clients — so we key on the actor
	// class; single-client N1 has exactly one actor.)
	// Snapshot an actor so Lua alife():actor() resolves before any actor binder runs
	// (actor_proxy:init derefs it). This must NOT require ASPLAYER: the world's save
	// actor arrives BEFORE our owned actor (which the server spawns only after we're
	// ready), and its binder would crash on a nil alife():actor(). set_client_actor
	// prefers an ASPLAYER (owned) actor and upgrades to it if a non-owned one was
	// snapshotted first, so alife():actor() ends up as the local player's actor.
	// (Control/g_actor/Local are gated on ASPLAYER separately, so peers stay remote.)
	if (xr_enet::enabled() && !ai().get_alife() && smart_cast<CSE_ALifeCreatureActor*>(E))
	{
		Msg("- XRNET(diag): ACTOR SPAWN received on co-op client (flags=0x%x id=%u asplayer=%d)",
			E->s_flags.flags, E->ID, E->s_flags.is(M_SPAWN_OBJECT_ASPLAYER) ? 1 : 0);
		FlushLog();
		CALifeSimulator::set_client_actor(E);
	}

	/*
	game_spawn_queue.push_back(E);
	if (g_bDebugEvents)		ProcessGameSpawns();
	/*/
	g_sv_Spawn(E);

	F_entity_Destroy(E);
	//*/
};

void CLevel::g_cl_Spawn(LPCSTR name, u8 rp, u16 flags, Fvector pos)
{
	// Create
	CSE_Abstract* E = F_entity_Create(name);
	VERIFY(E);

	// Fill
	E->s_name = name;
	E->set_name_replace("");
	//.	E->s_gameid			=	u8(GameID());
	E->s_RP = rp;
	E->ID = 0xffff;
	E->ID_Parent = 0xffff;
	E->ID_Phantom = 0xffff;
	E->s_flags.assign(flags);
	E->RespawnTime = 0;
	E->o_Position = pos;

	// Send
	NET_Packet P;
	E->Spawn_Write(P,TRUE);
	Send(P, net_flags(TRUE));

	// Destroy
	F_entity_Destroy(E);
}

#ifdef DEBUG
	extern Flags32				psAI_Flags;
	extern float				debug_on_frame_gather_stats_frequency;
#	include "ai_debug.h"
#endif // DEBUG

void CLevel::g_sv_Spawn(CSE_Abstract* E)
{
#ifdef DEBUG_MEMORY_MANAGER
	size_t							E_mem = 0;
	if (g_bMEMO)	{
		lua_gc					(ai().script_engine().lua(),LUA_GCCOLLECT,0);
		lua_gc					(ai().script_engine().lua(),LUA_GCCOLLECT,0);
		E_mem					= Memory.mem_usage();	
		Memory.stat_calls		= 0;
	}
#endif // DEBUG_MEMORY_MANAGER
	//-----------------------------------------------------------------
	//	CTimer		T(false);

#ifdef DEBUG
	//	Msg					("* CLIENT: Spawn: %s, ID=%d", *E->s_name, E->ID);
#endif

	// Optimization for single-player only	- minimize traffic between client and server
	if (GameID() == eGameIDSingle) psNET_Flags.set(NETFLAG_MINIMIZEUPDATES,TRUE);
	else psNET_Flags.set(NETFLAG_MINIMIZEUPDATES,FALSE);

	// Client spawn
	//	T.Start		();
	CObject* O = Objects.Create(*E->s_name);
	// Msg				("--spawn--CREATE: %f ms",1000.f*T.GetAsync());

	//	T.Start		();
#ifdef DEBUG_MEMORY_MANAGER
	mem_alloc_gather_stats		(false);
#endif // DEBUG_MEMORY_MANAGER
	if (0 == O || (!O->net_Spawn(E)))
	{
		O->net_Destroy();
		if (!g_dedicated_server)
			client_spawn_manager().clear(O->ID());
		Objects.Destroy(O);
		Msg("! Failed to spawn entity '%s'", *E->s_name);
#ifdef DEBUG_MEMORY_MANAGER
		mem_alloc_gather_stats	(!!psAI_Flags.test(aiDebugOnFrameAllocs));
#endif // DEBUG_MEMORY_MANAGER
	}
	else
	{
#ifdef DEBUG_MEMORY_MANAGER
		mem_alloc_gather_stats	(!!psAI_Flags.test(aiDebugOnFrameAllocs));
#endif // DEBUG_MEMORY_MANAGER
		if (!g_dedicated_server)
			client_spawn_manager().callback(O);
		//Msg			("--spawn--SPAWN: %f ms",1000.f*T.GetAsync());

		// MP fork (§14 co-op thin client): the server strips M_SPAWN_OBJECT_LOCAL
		// and _ASPLAYER for remote clients, so the stock "this is my player"
		// gate never fires and the client gets NO control entity — then anything
		// that reads Level().CurrentControlEntity() (e.g. CCustomZone::
		// shedule_Update -> ->Position()) derefs null. Also take control of the
		// actor a co-op client spawns (it has exactly one — its player).
		// Control entity must be non-null through load (CCustomZone::shedule_Update etc.
		// deref CurrentControlEntity()). Our owned actor (ASPLAYER) arrives only after
		// we're ready, so seed the control entity with the FIRST actor while none is set,
		// then let the owned actor take over when it spawns (SetControlEntity handles the
		// switch via On_B_NotCurrentEntity). A peer's actor never grabs it once seeded.
		const bool _coop_client_actor =
			xr_enet::enabled() && !ai().get_alife() && !!smart_cast<CSE_ALifeCreatureActor*>(E)
			&& (E->s_flags.is(M_SPAWN_OBJECT_ASPLAYER) || !CurrentControlEntity());
		if (((E->s_flags.is(M_SPAWN_OBJECT_LOCAL)) &&
			(E->s_flags.is(M_SPAWN_OBJECT_ASPLAYER))) || _coop_client_actor)
		{
			if (IsDemoPlayStarted())
			{
				if (E->s_flags.is(M_SPAWN_OBJECT_PHANTOM))
				{
					SetControlEntity(O);
					SetEntity(O); //do not switch !!!
					SetDemoSpectator(O);
				}
			}
			else
			{
				if (CurrentEntity() != NULL)
				{
					CGameObject* pGO = smart_cast<CGameObject*>(CurrentEntity());
					if (pGO) pGO->On_B_NotCurrentEntity();
				}
				SetControlEntity(O);
				SetEntity(O); //do not switch !!!
			}
		}

		// MP fork (§14 step 7 P4 D2, run 3): a reclaimed body that never becomes the control
		// entity and one that arrives dead fail identically — the client simply stops exporting.
		// Say which happened, once per actor spawn, right where the decision is made.
		if (xr_enet::enabled() && !ai().get_alife() && smart_cast<CSE_ALifeCreatureActor*>(E))
		{
			CObject* ctrl = CurrentControlEntity();
			CEntityAlive* alive = smart_cast<CEntityAlive*>(O);
			Msg("- XRNET(diag): co-op actor spawn id=%u local=%d asplayer=%d cse_hp=%.2f "
				"obj_hp=%.2f -> control entity is now id=%u (%s)",
				E->ID, E->s_flags.is(M_SPAWN_OBJECT_LOCAL) ? 1 : 0,
				E->s_flags.is(M_SPAWN_OBJECT_ASPLAYER) ? 1 : 0,
				smart_cast<CSE_ALifeCreatureAbstract*>(E) ? smart_cast<CSE_ALifeCreatureAbstract*>(E)->get_health() : -1.f,
				alive ? alive->GetfHealth() : -1.f,
				ctrl ? ctrl->ID() : u16(-1), (ctrl == O) ? "THIS BODY" : "someone else's");
			FlushLog();
		}

		if (0xffff != E->ID_Parent)
		{
			/*
			// Generate ownership-event
			NET_Packet			GEN;
			GEN.w_begin			(M_EVENT);
			GEN.w_u32			(E->m_dwSpawnTime);//-NET_Latency);
			GEN.w_u16			(GE_OWNERSHIP_TAKE);
			GEN.w_u16			(E->ID_Parent);
			GEN.w_u16			(u16(O->ID()));
			game_events->insert	(GEN);
			/*/
			NET_Packet GEN;
			GEN.write_start();
			GEN.read_start();
			GEN.w_u16(u16(O->ID()));
			cl_Process_Event(E->ID_Parent, GE_OWNERSHIP_TAKE, GEN);
			//*/
		}

#ifdef NET_SPAWN_AFTER_CALLBACKS
        if (smart_cast<CGameObject*>(O))
        {
            smart_cast<CGameObject*>(O)->callback(GameObject::eNetSpawnAfter)();
        }
#endif
	}

	/*if (E->s_flags.is(M_SPAWN_UPDATE)) {
		NET_Packet				temp;
		temp.B.count			= 0;
		E->UPDATE_Write			(temp);
		if (temp.B.count > 0)
		{
			temp.r_seek				(0);
			O->net_Import			(temp);
		}
		}*/ //:(

	//---------------------------------------------------------
	Game().OnSpawn(O);
	//---------------------------------------------------------
#ifdef DEBUG_MEMORY_MANAGER
	if (g_bMEMO) {
		lua_gc					(ai().script_engine().lua(),LUA_GCCOLLECT,0);
		lua_gc					(ai().script_engine().lua(),LUA_GCCOLLECT,0);
		Msg						("* %20s : %lld bytes, %d ops", *E->s_name,Memory.mem_usage()-E_mem, Memory.stat_calls );
	}
#endif // DEBUG_MEMORY_MANAGER
}

CSE_Abstract* CLevel::spawn_item(LPCSTR section, const Fvector& position, u32 level_vertex_id, u16 parent_id,
                                 bool return_item)
{
	CSE_Abstract* abstract = F_entity_Create(section);
	R_ASSERT3(abstract, "Cannot find item with section", section);
	CSE_ALifeDynamicObject* dynamic_object = smart_cast<CSE_ALifeDynamicObject*>(abstract);
	if (dynamic_object && ai().get_level_graph())
	{
		dynamic_object->m_tNodeID = level_vertex_id;
		if (ai().level_graph().valid_vertex_id(level_vertex_id) && ai().get_game_graph() && ai().get_cross_table())
			dynamic_object->m_tGraphID = ai().cross_table().vertex(level_vertex_id).game_vertex_id();
	}

	//оружие спавним с полным магазинои
	CSE_ALifeItemWeapon* weapon = smart_cast<CSE_ALifeItemWeapon*>(abstract);
	if (weapon)
		weapon->a_elapsed = weapon->get_ammo_magsize();

	// Fill
	abstract->s_name = section;
	abstract->set_name_replace(section);
	//.	abstract->s_gameid		= u8(GameID());
	abstract->o_Position = position;
	abstract->s_RP = 0xff;
	abstract->ID = 0xffff;
	abstract->ID_Parent = parent_id;
	abstract->ID_Phantom = 0xffff;
	abstract->s_flags.assign(M_SPAWN_OBJECT_LOCAL);
	abstract->RespawnTime = 0;

	if (!return_item)
	{
		NET_Packet P;
		abstract->Spawn_Write(P,TRUE);
		Send(P, net_flags(TRUE));
		F_entity_Destroy(abstract);
		return (0);
	}
	else
		return (abstract);
}

void CLevel::ProcessGameSpawns()
{
	while (!game_spawn_queue.empty())
	{
		CSE_Abstract* E = game_spawn_queue.front();

		g_sv_Spawn(E);

		F_entity_Destroy(E);

		game_spawn_queue.pop_front();
	}
}
