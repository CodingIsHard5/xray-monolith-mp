////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_simulator.h
//	Created 	: 25.12.2002
//  Modified 	: 13.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "alife_interaction_manager.h"
#include "alife_update_manager.h"
#include "script_export_space.h"

#pragma warning(push)
#pragma warning(disable:4005)

class CALifeSimulator :
	public CALifeUpdateManager,
	public CALifeInteractionManager
{
protected:
	virtual void setup_simulator(CSE_ALifeObject* object);
	virtual void reload(LPCSTR section);

public:
	CALifeSimulator(xrServer* server, shared_str* command_line);
	// MP fork (§14 co-op thin client): an INERT, EMPTY simulator for a co-op
	// CLIENT. The authoritative world lives on the SERVER; the client never
	// simulates. This ctor allocates empty registries (so gamedata's alife()
	// reads resolve to "nothing here" instead of crashing on nil) but does NOT
	// call restart_all()/load()/set_alife() — no world is loaded, and it is
	// deliberately kept OUT of ai() so C++ `ai().get_alife()` stays null and
	// engine MP behaviour is unchanged. Only the Lua alife() sees it (see
	// alife_simulator_script.cpp). The tag disambiguates from the real ctor.
	enum EClientInert { client_inert };
	CALifeSimulator(xrServer* server, EClientInert);
	virtual ~CALifeSimulator();
	virtual void destroy();
	IReader const* get_config(shared_str config) const;

	// Lifecycle for the Lua-visible client-inert simulator (co-op client only).
	// Owned here, separate from ai()'s real simulator.
	static void			create_client_inert	();
	static void			destroy_client_inert	();
	static CALifeSimulator*	client_inert_instance	();

#if 0//def DEBUG
			void	validate			();
#endif //DEBUG

private:
	typedef xr_list<std::pair<shared_str, IReader*>> configs_type;
	mutable configs_type m_configs_lru;

DECLARE_SCRIPT_REGISTER_FUNCTION
};

#pragma warning(pop)


#include "alife_simulator_inline.h"
