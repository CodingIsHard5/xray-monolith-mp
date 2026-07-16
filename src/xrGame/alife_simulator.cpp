////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_simulator.cpp
//	Created 	: 25.12.2002
//  Modified 	: 13.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "alife_simulator.h"
#include "xrServer_Objects_ALife.h"
#include "ai_space.h"
#include "../xrEngine/IGame_Persistent.h"
#include "script_engine.h"
#include "mainmenu.h"
#include "object_factory.h"
#include "alife_object_registry.h"
#include "alife_graph_registry.h"                 // MP fork: graph().set_actor()
#include "xrServer_Objects_ALife_Monsters.h"      // MP fork: CSE_ALifeCreatureActor
#include "../xrEngine/xr_ioconsole.h"

#ifdef DEBUG
#	include "moving_objects.h"
#endif // DEBUG

LPCSTR alife_section = "alife";

extern void destroy_lua_wpn_params();

void restart_all()
{
	if (strstr(Core.Params, "-keep_lua"))
		return;

	destroy_lua_wpn_params();
	MainMenu()->DestroyInternal(true);
	xr_delete(g_object_factory);
	ai().script_engine().init();

#ifdef DEBUG
	ai().moving_objects().clear	();
#endif // DEBUG
}

CALifeSimulator::CALifeSimulator(xrServer* server, shared_str* command_line) :
	CALifeUpdateManager(server, alife_section),
	CALifeInteractionManager(server, alife_section),
	CALifeSimulatorBase(server, alife_section)
{
	restart_all();

	ai().set_alife(this);

	setup_command_line(command_line);

	typedef IGame_Persistent::params params;
	params& p = g_pGamePersistent->m_game_params;

	R_ASSERT2(
		xr_strlen(p.m_game_or_spawn) &&
		!xr_strcmp(p.m_alife,"alife") &&
		!xr_strcmp(p.m_game_type,"single"),
		"Invalid server options!"
	);

	string256 temp;
	xr_strcpy(temp, p.m_game_or_spawn);
	xr_strcat(temp, "/");
	xr_strcat(temp, p.m_game_type);
	xr_strcat(temp, "/");
	xr_strcat(temp, p.m_alife);
	*command_line = temp;

	LPCSTR start_game_callback = pSettings->r_string(alife_section, "start_game_callback");
	::luabind::functor<void> functor;
	R_ASSERT2(ai().script_engine().functor(start_game_callback,functor), "failed to get start game callback");
	functor();

	load(p.m_game_or_spawn, !xr_strcmp(p.m_new_or_load, "load") ? false : true, !xr_strcmp(p.m_new_or_load, "new"));
}

// MP fork (§14 co-op thin client): the inert client simulator. See the header.
// Runs only the cheap base ctors + reload() (empty registries, m_initialized
// = true). Crucially does NOT call restart_all()/setup_command_line()/load()
// (no world) and does NOT call ai().set_alife() (kept out of C++ ai(), so the
// 84 engine sites that gate on ai().get_alife() see the client as world-less,
// exactly as a stock MP client). Only the Lua alife() binding exposes it.
CALifeSimulator::CALifeSimulator(xrServer* server, EClientInert) :
	CALifeUpdateManager(server, alife_section),
	CALifeInteractionManager(server, alife_section),
	CALifeSimulatorBase(server, alife_section)
{
	reload(alife_section);
}

static CALifeSimulator* g_client_inert_alife = nullptr;
// A persistent snapshot of the co-op client's actor CSE. The real actor CSE is
// transient (cl_Process_Spawn F_entity_Destroys it right after net_Spawn), so
// we clone it once into this owned copy for Lua alife():actor() to read.
static CSE_Abstract*	g_client_actor_stub = nullptr;

void CALifeSimulator::set_client_actor(CSE_Abstract* actor_cse)
{
	if (!g_client_inert_alife || !actor_cse)
		return;
	if (g_client_actor_stub)                    // once per level
		return;

	// Clone actor_cse into an owned entity via the spawn wire-format (the same
	// path cl_Process_Spawn uses). bLocal=TRUE preserves the ASPLAYER flag so
	// the clone is recognised as the actor. Spawn_Read does its own r_begin, so
	// no manual name handling is needed.
	CSE_Abstract* stub = F_entity_Create(actor_cse->s_name.c_str());
	if (!stub)
		return;
	NET_Packet packet;
	actor_cse->Spawn_Write(packet, TRUE);
	stub->Spawn_Read(packet);

	CSE_ALifeCreatureActor* actor = smart_cast<CSE_ALifeCreatureActor*>(stub);
	if (!actor)
	{
		F_entity_Destroy(stub);
		return;
	}
	g_client_actor_stub = stub;
	g_client_inert_alife->graph().set_actor(actor);
	Msg("- XRNET(dbg): co-op client actor snapshot registered (id=%u)", actor->ID);
}

void CALifeSimulator::create_client_inert()
{
	if (g_client_inert_alife)
		return;
	// server may be null on a pure network client — the base ctor only stores
	// it, and the inert sim's read-only methods never dereference it.
	g_client_inert_alife = xr_new<CALifeSimulator>((xrServer*)nullptr, client_inert);
	// The CALifeUpdateManager ctor shedule_register()s the sim, and reload()
	// left m_initialized=true, so shedule_Update() would NOT early-return and
	// would drive update()/update_switch() on a client with a null server ->
	// crash. Unregister immediately: the inert sim must never simulate; the
	// server owns simulation. (dtor's shedule_unregister() then no-ops.)
	g_client_inert_alife->shedule_unregister();
	Msg("- XRNET(dbg): co-op client inert alife() created (empty, world lives on server)");
}

void CALifeSimulator::destroy_client_inert()
{
	if (!g_client_inert_alife)
		return;
	// Drop the actor snapshot first (clear the dangling-able pointer in the
	// graph registry, then free the owned clone).
	g_client_inert_alife->graph().set_actor(nullptr);
	if (g_client_actor_stub)
		F_entity_Destroy(g_client_actor_stub);
	// inert sim was never registered in ai(); tear down its empty registries
	// directly (unload() clears m_initialized so the base dtor assert passes).
	g_client_inert_alife->unload();
	xr_delete(g_client_inert_alife);
}

CALifeSimulator* CALifeSimulator::client_inert_instance()
{
	return g_client_inert_alife;
}

CALifeSimulator::~CALifeSimulator()
{
	VERIFY(!ai().get_alife());

	configs_type::iterator i = m_configs_lru.begin();
	configs_type::iterator const e = m_configs_lru.end();
	for (; i != e; ++i)
		FS.r_close((*i).second);
}

void CALifeSimulator::destroy()
{
	//	validate					();
	CALifeUpdateManager::destroy();
	VERIFY(ai().get_alife());
	ai().set_alife(0);
}

void CALifeSimulator::setup_simulator(CSE_ALifeObject* object)
{
	//	VERIFY2						(!object->m_alife_simulator,object->s_name_replace);
	object->m_alife_simulator = this;
}

void CALifeSimulator::reload(LPCSTR section)
{
	CALifeUpdateManager::reload(section);
}

struct string_prdicate
{
	shared_str m_value;

	inline string_prdicate(shared_str const& value) :
		m_value(value)
	{
	}

	inline bool operator( )(std::pair<shared_str, IReader*> const& value) const
	{
		return !xr_strcmp(m_value, value.first);
	}
}; // struct string_prdicate

IReader const* CALifeSimulator::get_config(shared_str config) const
{
	configs_type::iterator const found = std::find_if(m_configs_lru.begin(), m_configs_lru.end(),
	                                                  string_prdicate(config));
	if (found != m_configs_lru.end())
	{
		configs_type::value_type temp = *found;
		m_configs_lru.erase(found);
		m_configs_lru.insert(m_configs_lru.begin(), std::make_pair(temp.first, temp.second));
		return temp.second;
	}

	string_path file_name;
	FS.update_path(file_name, "$game_config$", config.c_str());
	if (!FS.exist(file_name))
		return 0;

	m_configs_lru.insert(m_configs_lru.begin(), std::make_pair(config, FS.r_open(file_name)));
	return m_configs_lru.front().second;
}

namespace detail
{
	bool object_exists_in_alife_registry(u32 id)
	{
		if (ai().get_alife())
		{
			return ai().alife().objects().object((ALife::_OBJECT_ID)id, true) != 0;
		}
		return false;
	}
} // detail
