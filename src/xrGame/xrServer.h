#pragma once
// xrServer.h: interface for the xrServer class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_XRSERVER_H__65728A25_16FC_4A7B_8CCE_D798CA5EC64E__INCLUDED_)
#define AFX_XRSERVER_H__65728A25_16FC_4A7B_8CCE_D798CA5EC64E__INCLUDED_
#pragma once

#include "../xrNetServer/net_server.h"
#include "game_sv_base.h"
#include "id_generator.h"
#include "../xrEngine/mp_logging.h"
#include "secure_messaging.h"
#include "xrServer_updates_compressor.h"
#include "xrClientsPool.h"

#ifdef DEBUG
//. #define SLOW_VERIFY_ENTITIES
#endif


class CSE_Abstract;

const u32 NET_Latency = 50; // time in (ms)

// t-defs
typedef xr_hash_map<u16, CSE_Abstract*> xrS_entities;

class xrClientData : public IClient
{
public:
	CSE_Abstract* owner;
	BOOL net_Ready;
	BOOL net_Accepted;

	BOOL net_PassUpdates;
	u32 net_LastMoveUpdateTime;

	game_PlayerState* ps;

	struct
	{
		u8 m_maxPingWarnings;
		u32 m_dwLastMaxPingWarningTime;
	} m_ping_warn;

	struct
	{
		BOOL m_has_admin_rights;
		u32 m_dwLoginTime;
	} m_admin_rights;

	shared_str m_cdkey_digest;
	secure_messaging::key_t m_secret_key;
	s32 m_last_key_sync_request_seed;

	xrClientData();
	virtual ~xrClientData();
	virtual void Clear();
};


// main
struct svs_respawn
{
	u32 timestamp;
	u16 phantom;
};

IC bool operator <(const svs_respawn& A, const svs_respawn& B) { return A.timestamp < B.timestamp; }

struct CheaterToKick
{
	shared_str reason;
	ClientID cheater_id;
};

typedef xr_vector<CheaterToKick> cheaters_t;

namespace file_transfer
{
	class server_site;
}; //namespace file_transfer

class clientdata_proxy;
class server_info_uploader;

class xrServer : public IPureServer
{
private:
	xrS_entities entities;
	xr_multiset<svs_respawn> q_respawn;
	xr_vector<u16> conn_spawned_ids;
	cheaters_t m_cheaters;

	file_transfer::server_site* m_file_transfers;
	clientdata_proxy* m_screenshot_proxies[MAX_PLAYERS_COUNT * 2];
	void initialize_screenshot_proxies();
	void deinitialize_screenshot_proxies();

	typedef server_updates_compressor::send_ready_updates_t::const_iterator update_iterator_t;
	update_iterator_t m_update_begin;
	update_iterator_t m_update_end;
	server_updates_compressor m_updator;

	// MP fork (§19 co-op): run a dialogue action a thin client asked us to perform.
	void coop_run_dialog_action(NET_Packet& P);

	// §3 win #2b inc3 (per-client update packets): when target_client != nullptr, build the update stream
	// for JUST that client — ambient creatures are filtered to its relevance set (m_coop_rel_known), while
	// player actors + non-creature entities still stream to everyone. nullptr = the stock global-union build
	// (byte-identical to before). See DECISION_REPLICATION_PLAN.md.
	void MakeUpdatePackets(xrClientData* target_client = nullptr);
	void SendUpdatePacketsToAll();
	void SendUpdatePacketsTo(ClientID cid, bool save_demo = true); // §3 inc3: send just-built packets to ONE client
	u32 m_last_updates_size;
	u32 m_last_update_time;
	// §3 inc3: per-client throttle bookkeeping ([clientID][creatureID] = last throttled send). Keeping the
	// throttle state per-client keeps -coop_npc_hz / -coop_update_throttle correct when they stack on the
	// per-client stream (a creature relevant to two clients is rate-limited independently for each).
	xr_map<u32, xr_map<u16, u32>> m_coop_c2_last_per_client;
	// §3 inc3: real clients to build per-client update streams for (rebuilt each update pass; loopback excluded).
	xr_vector<xrClientData*> m_coop_update_targets;
	void coop_collect_update_target_cb(IClient* C); // ForEachClientDo callback: collect one real client


	void SendServerInfoToClient(ClientID const& new_client);
	server_info_uploader& GetServerInfoUploader();

	void LoadServerInfo();

	typedef xr_vector<server_info_uploader*> info_uploaders_t;

	info_uploaders_t m_info_uploaders;
	IReader* m_server_logo;
	IReader* m_server_rules;

	struct DelayedPacket
	{
		ClientID SenderID;
		NET_Packet Packet;

		bool operator ==(const DelayedPacket& other)
		{
			return SenderID == other.SenderID;
		}
	};

	xrCriticalSection DelayedPackestCS;
	xr_deque<DelayedPacket> m_aDelayedPackets;
	void ProceedDelayedPackets();
	void AddDelayedPacket(NET_Packet& Packet, ClientID Sender);
	u32 OnDelayedMessage(NET_Packet& P, ClientID sender); // Non-Zero means broadcasting with "flags" as returned

	void SendUpdatesToAll();
	void _stdcall SendGameUpdateTo(IClient* client);
private:
	typedef
	CID_Generator<
		u32, // time identifier type
		u8, // compressed id type 
		u16, // id type
		u8, // block id type
		u16, // chunk id type
		0, // min value
		u16(-2), // max value
		256, // block size
		u16(-1) // invalid id
	> id_generator_type;

private:
	id_generator_type m_tID_Generator;
	secure_messaging::seed_generator m_seed_generator;

protected:
	void Server_Client_Check(IClient* CL);
	void PerformCheckClientsForMaxPing();
	// MP fork (§4 step 3, increment C2): ids of creatures the decision system currently OWNS
	// (a client runs their AI locally). Only these are M_UPDATE-throttled to the soft-correct
	// cadence (-coop_update_throttle); normal puppets keep dense streaming. Value = the last time
	// coop_broadcast_decision refreshed the entry; an id whose decisions stop is aged out after
	// COOP_DECISION_DRIVEN_TTL_MS so it returns to dense streaming. See DECISION_REPLICATION_PLAN.md.
	xr_map<u16, u32> m_coop_decision_driven;
	enum { COOP_DECISION_DRIVEN_TTL_MS = 5000 };

	// §3 win #2 (radius culling, -coop_cull_radius <m>): per-pass snapshot of each REAL player actor's
	// position. MakeUpdatePackets skips streaming a creature whose distance to EVERY anchor exceeds the
	// radius — a client only needs NPCs near its own actor, so far ones cost nothing. Rebuilt each pass.
	xr_vector<Fvector> m_coop_cull_anchors;
	void coop_gather_cull_anchors();          // fill m_coop_cull_anchors from connected real clients
	void coop_cull_anchor_cb(IClient* C);     // ForEachClientDo callback: push one client's actor pos

	// §3 win #2b (per-client relevance, increment 2 = WIRED): per real client, the set of creature ids
	// ACTUALLY spawned on that client (within the hysteresis band of its actor). The relevance pass now
	// really spawns near creatures + despawns far/offline ones per-client (was diagnostic-only in inc1),
	// and is the SOLE owner of per-client creature spawn/despawn under -coop_cull_radius (the global
	// creature-spawn broadcast is gated off — see coop_cull_gate_creature). Keyed by client id value.
	xr_map<u32, xr_map<u16, char>> m_coop_rel_known;
	u32 m_coop_rel_last = 0;                   // last relevance-pass time (throttled to ~2 Hz)
	void coop_relevance_diag();                // run one relevance pass (all real clients)
	void coop_relevance_client_cb(IClient* C); // ForEachClientDo callback: one client's spawn/despawn + COOP_REL log
	void coop_relevance_spawn(xrClientData* CL, CSE_Abstract* E);   // stripped/remote net_Spawn of E to one client
	void coop_relevance_despawn(xrClientData* CL, u16 id);         // SendTo(GE_DESTROY) of one id to one client
	void coop_clear_relevance(u32 client_id);  // drop a client's known-set (on disconnect — CodeRabbit inc1)
	void coop_forget_relevance_id(u16 id);     // forget one creature id across ALL clients (stock GE_DESTROY site)

	// §3 win #2b inc2: cached -coop_cull_radius on/off (-1 unparsed). Under cull, the relevance manager
	// owns per-client creature spawn/despawn, so the stock global creature-spawn broadcast is gated off.
	int m_coop_cull_on = -1;
	bool coop_cull_on();                                // is -coop_cull_radius active (cached)
	bool coop_cull_gate_creature(CSE_Abstract* E);      // true => suppress the stock spawn of this ambient creature
	bool coop_is_decision_driven(u16 id);               // decision system owns this creature's replication (TTL)

public:
	game_sv_GameState* game;

	// MP fork (§4 step 3, C2): mark a creature as decision-driven (called at the broadcast site).
	void coop_mark_decision_driven(u16 id);

	void Export_game_type(IClient* CL);
	void Perform_game_export();
	BOOL PerformRP(CSE_Abstract* E);
	void PerformMigration(CSE_Abstract* E, xrClientData* from, xrClientData* to);

	IC void clear_ids()
	{
		m_tID_Generator = id_generator_type();
	}

	IC u16 PerformIDgen(u16 ID)
	{
		return (m_tID_Generator.tfGetID(ID));
	}

	IC void FreeID(u16 ID, u32 time)
	{
		return (m_tID_Generator.vfFreeID(ID, time));
	}

	void Perform_connect_spawn(CSE_Abstract* E, xrClientData* to, NET_Packet& P);
	void Perform_transfer(NET_Packet& PR, NET_Packet& PT, CSE_Abstract* what, CSE_Abstract* from, CSE_Abstract* to);
	void Perform_reject(CSE_Abstract* what, CSE_Abstract* from, int delta);
	void Perform_destroy(CSE_Abstract* tpSE_Abstract, u32 mode);

	CSE_Abstract* Process_spawn(NET_Packet& P, ClientID sender, BOOL bSpawnWithClientsMainEntityAsParent = FALSE,
	                            CSE_Abstract* tpExistedEntity = 0);
	void Process_update(NET_Packet& P, ClientID sender);
	void Process_save(NET_Packet& P, ClientID sender);
	void Process_event(NET_Packet& P, ClientID sender);
	void Process_event_ownership(NET_Packet& P, ClientID sender, u32 time, u16 ID, BOOL bForced = FALSE);
	bool Process_event_reject(NET_Packet& P, const ClientID sender, const u32 time, const u16 id_parent,
	                          const u16 id_entity, bool send_message = true);
	void Process_event_destroy(NET_Packet& P, ClientID sender, u32 time, u16 ID, NET_Packet* pEPack);
	void Process_event_activate(NET_Packet& P, const ClientID sender, const u32 time, const u16 id_parent,
	                            const u16 id_entity, bool send_message = true);

	xrClientData* SelectBestClientToMigrateTo(CSE_Abstract* E, BOOL bForceAnother = FALSE);
	void SendConnectResult(IClient* CL, u8 res, u8 res1, char* ResultStr);
	void __stdcall SendConfigFinished(ClientID const& clientId);
	void SendProfileCreationError(IClient* CL, char const* reason);
	void AttachNewClient(IClient* CL);
	virtual void OnBuildVersionRespond(IClient* CL, NET_Packet& P);
protected:
	xrClientsPool m_disconnected_clients;
	bool CheckAdminRights(const shared_str& user, const shared_str& pass, string512& reason);
	virtual IClient* new_client(SClientConnectData* cl_data);

	virtual bool Check_ServerAccess(IClient* CL, string512& reason) { return true; }

	virtual bool NeedToCheckClient_GameSpy_CDKey(IClient* CL) { return false; }
	virtual void Check_GameSpy_CDKey_Success(IClient* CL);
	void RequestClientDigest(IClient* CL);
	void ProcessClientDigest(xrClientData* xrCL, NET_Packet* P);
	void KickCheaters();

	virtual bool NeedToCheckClient_BuildVersion(IClient* CL);
	virtual void Check_BuildVersion_Success(IClient* CL);

	void SendConnectionData(IClient* CL);
	void OnChatMessage(NET_Packet* P, xrClientData* CL);
	void OnProcessClientMapData(NET_Packet& P, ClientID const& clientID);

private:
	void PerformSecretKeysSync(xrClientData* xrCL);
	void PerformSecretKeysSyncAck(xrClientData* xrCL, NET_Packet& P);
protected:
	void OnSecureMessage(NET_Packet& P, xrClientData* xrClSender);

public:
	// constr / destr
	xrServer();
	virtual ~xrServer();

	// extended functionality
	virtual u32 OnMessage(NET_Packet& P, ClientID sender); // Non-Zero means broadcasting with "flags" as returned
	u32 OnMessageSync(NET_Packet& P, ClientID sender);
	virtual void OnCL_Connected(IClient* CL);
	virtual void OnCL_Disconnected(IClient* CL);
	virtual bool OnCL_QueryHost();
	virtual void SendTo_LL(ClientID ID, void* data, u32 size, u32 dwFlags = DPNSEND_GUARANTEED, u32 dwTimeout = 0);
	void SecureSendTo(xrClientData* xrCL, NET_Packet& P, u32 dwFlags = DPNSEND_GUARANTEED, u32 dwTimeout = 0);
	virtual void SendBroadcast(ClientID exclude, NET_Packet& P, u32 dwFlags = DPNSEND_GUARANTEED);
	void GetPooledState(xrClientData* xrCL);
	void ClearDisconnectedPool() { m_disconnected_clients.Clear(); };

	virtual IClient* client_Create(); // create client info
	virtual void client_Replicate(); // replicate current state to client
	virtual IClient* client_Find_Get(ClientID ID); // Find earlier disconnected client
	virtual void client_Destroy(IClient* C); // destroy client info

	// utilities
	CSE_Abstract* entity_Create(LPCSTR name);
	void entity_Destroy(CSE_Abstract*& P);
	u32 GetEntitiesNum() { return entities.size(); };
	CSE_Abstract* GetEntity(u32 Num);
	u32 const GetLastUpdatesSize() const { return m_last_updates_size; };

	xrClientData* ID_to_client(ClientID const& ID, bool ScanAll = false)
	{
		return (xrClientData*)(IPureServer::ID_to_client(ID, ScanAll));
	}

	CSE_Abstract* ID_to_entity(u16 ID);

	// main
	virtual EConnect Connect(shared_str& session_name, GameDescriptionData& game_descr);
	virtual void Disconnect();
	virtual void Update();
	void SLS_Default();
	void SLS_Clear();
	void SLS_Save(IWriter& fs);
	void SLS_Load(IReader& fs);
	shared_str level_name(const shared_str& server_options) const;
	shared_str level_version(const shared_str& server_options) const;
	static LPCSTR get_map_download_url(LPCSTR level_name, LPCSTR level_version);

	void create_direct_client();
	BOOL IsDedicated() const { return m_bDedicated; };

	virtual void Assign_ServerType(string512& res)
	{
	};
	virtual bool HasPassword() { return false; }
	virtual bool HasProtected() { return false; }
	void AddCheater(shared_str const& reason, ClientID const& cheaterID);
	void MakeScreenshot(ClientID const& admin_id, ClientID const& cheater_id);
	void MakeConfigDump(ClientID const& admin_id, ClientID const& cheater_id);

	virtual void GetServerInfo(CServerInfo* si);
	void SendPlayersInfo(ClientID const& to_client);
public:
	xr_string ent_name_safe(u16 eid);
#ifdef DEBUG
			bool			verify_entities		() const;
			void			verify_entity		(const CSE_Abstract *entity) const;
#endif
};


#ifdef DEBUG
		enum e_dbg_net_Draw_Flags
		{

			dbg_draw_actor_alive			=(1<<0),	
			dbg_draw_actor_dead				=(1<<1),	
			dbg_draw_customzone				=(1<<2),	
			dbg_draw_teamzone				=(1<<3),	
			dbg_draw_invitem				=(1<<4),	
			dbg_draw_actor_phys				=(1<<5),	
			dbg_draw_customdetector			=(1<<6),	
			dbg_destroy						=(1<<7),	
			dbg_draw_autopickupbox			=(1<<8),	
			dbg_draw_rp						=(1<<9),	
			dbg_draw_climbable				=(1<<10),
			dbg_draw_skeleton				=(1<<11)
		};
extern	Flags32	dbg_net_Draw_Flags;
#endif

#endif // !defined(AFX_XRSERVER_H__65728A25_16FC_4A7B_8CCE_D798CA5EC64E__INCLUDED_)
