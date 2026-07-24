#pragma once

#include "game_sv_base.h"

class xrServer;
class CALifeSimulator;
class xrClientData;
class CSE_ALifeCreatureActor;
class CSE_Abstract;

class game_sv_Single : public game_sv_GameState
{
private:
	typedef game_sv_GameState inherited;

protected:
	CALifeSimulator* m_alife_simulator;

public:
	game_sv_Single();
	virtual ~game_sv_Single();

	virtual LPCSTR type_name() const { return "single"; };
	virtual void Create(shared_str& options);
	//	virtual		CSE_Abstract*		get_entity_from_eid		(u16 id);


	virtual void OnCreate(u16 id_who);
	// MP fork (§14 co-op): spawn a per-client actor so each player controls their own
	// character (the single game type otherwise has only the save's one actor, which
	// all clients collide on). The M_CLIENTREADY hook is unreliable for co-op clients,
	// so we also POLL from Update() for ready clients that still lack an actor.
	virtual void OnPlayerConnectFinished(ClientID id_who);
private:
	void coop_poll_spawns();                 // spawn actors for ready, actorless clients
	void coop_spawn_actor_for(xrClientData* CL, Fvector* pos_override = NULL, Fvector* angle_override = NULL);
	void coop_clone_inventory_for(CSE_ALifeCreatureActor* base, CSE_Abstract* owner, xrClientData* CL);
	void coop_give_starting_kit(CSE_Abstract* owner, xrClientData* CL);
	void coop_update_anchors();              // feed player positions to A-Life anchors
	xr_map<u32, u32> m_coop_seen;            // client id -> first-seen time (grace for load)

	// MP fork (§9.3/9.4 co-op reconnection): when a co-op client disconnects, its actor
	// entity stays alive in the world (orphaned) for up to RECONNECT_TIMEOUT_MS. If the
	// same player name reconnects within that window, the orphaned actor is re-associated
	// instead of spawning a fresh one — preserving position, health, inventory.
	struct coop_orphan
	{
		u16         entity_id;   // server entity ID of the orphaned actor
		shared_str  player_name; // name used to match the reconnecting client
		u32         disconnect_time; // Device.dwTimeGlobal when disconnected
	};
	xr_vector<coop_orphan> m_coop_orphans;
	static const u32 RECONNECT_TIMEOUT_MS = 5 * 60 * 1000; // 5 minutes

	void coop_orphan_actor(xrClientData* CL);    // called on co-op client disconnect
	void coop_cleanup_orphans();                  // expire stale orphans
	CSE_Abstract* coop_find_orphan(LPCSTR name);  // find orphan by player name
public:
	void OnCoopClientDisconnected(xrClientData* CL); // game-layer co-op disconnect handler
public:
	virtual BOOL OnTouch(u16 eid_who, u16 eid_what, BOOL bForced = FALSE);
	virtual void OnDetach(u16 eid_who, u16 eid_what);

	// Main
	virtual void Update();
	virtual ALife::_TIME_ID GetStartGameTime();
	virtual ALife::_TIME_ID GetGameTime();
	virtual float GetGameTimeFactor();
	virtual void SetGameTimeFactor(const float fTimeFactor);

	virtual ALife::_TIME_ID GetEnvironmentGameTime();
	virtual float GetEnvironmentGameTimeFactor();
	virtual void SetEnvironmentGameTimeFactor(const float fTimeFactor);

	virtual bool change_level(NET_Packet& net_packet, ClientID sender);
	virtual void save_game(NET_Packet& net_packet, ClientID sender);
	virtual bool load_game(NET_Packet& net_packet, ClientID sender);
	virtual void reload_game(NET_Packet& net_packet, ClientID sender);
	virtual void switch_distance(NET_Packet& net_packet, ClientID sender);
	virtual BOOL CanHaveFriendlyFire() { return FALSE; }
	virtual void teleport_object(NET_Packet& packet, u16 id);
	virtual void add_restriction(NET_Packet& packet, u16 id);
	virtual void remove_restriction(NET_Packet& packet, u16 id);
	virtual void remove_all_restrictions(NET_Packet& packet, u16 id);
	virtual bool custom_sls_default() { return !!m_alife_simulator; };
	virtual void sls_default();
	virtual shared_str level_name(const shared_str& server_options) const;
	virtual void on_death(CSE_Abstract* e_dest, CSE_Abstract* e_src);
	void restart_simulator(LPCSTR saved_game_name);

	IC xrServer& server() const
	{
		VERIFY(m_server);
		return (*m_server);
	}

	IC CALifeSimulator& alife() const
	{
		VERIFY(m_alife_simulator);
		return (*m_alife_simulator);
	}
};
