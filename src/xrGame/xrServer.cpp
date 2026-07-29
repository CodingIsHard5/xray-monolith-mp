// xrServer.cpp: implementation of the xrServer class.
//
//////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "xrServer.h"
#include "../xrNetServer/xr_enet_transport.h"
#include "xrMessages.h"
#include "xrServer_Objects_ALife_All.h"
#include "level.h"
#include "game_cl_base.h"
#include "game_sv_mp.h"
#include "game_cl_base_weapon_usage_statistic.h"
#include "ai_space.h"
#include "../xrEngine/IGame_Persistent.h"
#include "string_table.h"
#include "object_broker.h"

#include "../xrEngine/XR_IOConsole.h"
#include "ui/UIInventoryUtilities.h"
#include "file_transfer.h"
#include "screenshot_server.h"
#include "xrServer_info.h"
#include "PhraseDialog.h"                         // MP fork (§19 co-op): server-run dialog actions
#include "Phrase.h"
#include "PhraseScript.h"
#include "mp_coop_owner.h"                        // MP fork (§14 step 8 P1): the acting-player context
#include <functional>

#pragma warning(push)
#pragma warning(disable:4995)
#include <malloc.h>
#pragma warning(pop)

u32 g_sv_traffic_optimization_level = eto_none;

xrClientData::xrClientData() :
	IClient(Device.GetTimerGlobal())
{
	ps = NULL;
	Clear();
}

void xrClientData::Clear()
{
	owner = NULL;
	net_Ready = FALSE;
	net_Accepted = FALSE;
	net_PassUpdates = TRUE;
	m_ping_warn.m_maxPingWarnings = 0;
	m_ping_warn.m_dwLastMaxPingWarningTime = 0;
	m_admin_rights.m_has_admin_rights = FALSE;
	m_coop_cl_update_count = 0;   // MP fork (§14 step 7 P4 D2): "has this client ever driven its body?"
	m_coop_last_health = -1.f;    // MP fork (§14 step 7 P4 D3.3): no health seen yet
	m_coop_last_damage_time = 0;  // MP fork (§14 step 7 P4 D3.3): never seen it drop
};


xrClientData::~xrClientData()
{
	xr_delete(ps);
}


xrServer::xrServer() : IPureServer(Device.GetTimerGlobal(), g_dedicated_server)
{
	m_file_transfers = NULL;
	m_aDelayedPackets.clear();
	m_server_logo = NULL;
	m_server_rules = NULL;
	m_last_updates_size = 0;
	m_last_update_time = 0;
}

xrServer::~xrServer()
{
	struct ClientDestroyer
	{
		static bool true_generator(IClient*)
		{
			return true;
		}
	};
	IClient* tmp_client = net_players.GetFoundClient(&ClientDestroyer::true_generator);
	while (tmp_client)
	{
		client_Destroy(tmp_client);
		tmp_client = net_players.GetFoundClient(&ClientDestroyer::true_generator);
	}
	m_aDelayedPackets.clear();
	entities.clear();
	delete_data(m_info_uploaders);
	xr_delete(m_server_logo);
	xr_delete(m_server_rules);
}

//--------------------------------------------------------------------

CSE_Abstract* xrServer::ID_to_entity(u16 ID)
{
	// #pragma todo("??? to all : ID_to_entity - must be replaced to 'game->entity_from_eid()'")	
	if (0xffff == ID) return 0;
	xrS_entities::iterator I = entities.find(ID);
	if (entities.end() != I) return I->second;
	else return 0;
}

//--------------------------------------------------------------------
IClient* xrServer::client_Create()
{
	return xr_new<xrClientData>();
}

void xrServer::client_Replicate()
{
}

IClient* xrServer::client_Find_Get(ClientID ID)
{
	DWORD dwPort = 0;
	ip_address tmp_ip_address;


	if (!psNET_direct_connect)
		GetClientAddress(ID, tmp_ip_address, &dwPort);
	else
		tmp_ip_address.set("127.0.0.1");

	IClient* newCL = client_Create();
	newCL->ID = ID;
	if (!psNET_direct_connect)
	{
		newCL->m_cAddress = tmp_ip_address;
		newCL->m_dwPort = dwPort;
	}

	newCL->server = this;
	net_players.AddNewClient(newCL);

#ifndef MASTER_GOLD
	Msg		("# New player created.");
#endif // #ifndef MASTER_GOLD
	return newCL;
};

u32 g_sv_Client_Reconnect_Time = 3;

void xrServer::client_Destroy(IClient* C)
{
	// Delete assosiated entity
	// xrClientData*	D = (xrClientData*)C;
	// CSE_Abstract* E = D->owner;
	IClient* alife_client = net_players.FindAndEraseClient(
		[C](IClient* client) { return client == C; }
	);
	//VERIFY(alife_client);
	if (alife_client)
	{
		CSE_Abstract* pOwner = static_cast<xrClientData*>(alife_client)->owner;
		CSE_Spectator* pS = smart_cast<CSE_Spectator*>(pOwner);
		if (pS)
		{
			NET_Packet P;
			P.w_begin(M_EVENT);
			P.w_u32(Level().timeServer()); //Device.TimerAsync());
			P.w_u16(GE_DESTROY);
			P.w_u16(pS->ID);
			SendBroadcast(C->ID, P, net_flags(TRUE,TRUE));
		};

		DelayedPacket pp;
		pp.SenderID = alife_client->ID;
		xr_deque<DelayedPacket>::iterator it;
		// MP fork (§14 step 8 QR-D): INSTRUMENTED, and the numbers matter for two separate reasons.
		//
		// (1) purged= / dlg= is what licenses reading a disconnect test at all. "The queued claim
		//     was thrown away" and "the queued claim had already run" produce the same end state
		//     from outside, and only a count taken HERE tells them apart. purged=0 means the
		//     injection missed its window and the run must report NOT MEASURED, not a pass.
		//
		// (2) tid= vs game= is a DEFECT REPORT, not diagnostics. This loop walks and erases
		//     m_aDelayedPackets while holding NOTHING, and on the ENet transport it is reached
		//     from the pump thread (xr_enet_transport.cpp calls client_Destroy straight out of the
		//     enet_host_service loop). ProceedDelayedPackets mutates that same deque under
		//     DelayedPackestCS — a lock this side never takes, so it buys no mutual exclusion
		//     against this side at all. Worse, the drain holds a REFERENCE into the deque
		//     (DelayedPacket& DPacket) across OnDelayedMessage, which for a dialogue action runs
		//     Lua; an erase of that element mid-handler is a use-after-free.
		//
		//     The lock is deliberately NOT added here in this increment. Making the pump thread
		//     block on DelayedPackestCS makes it wait for however long the game thread spends
		//     inside Lua, which is the lock-across-a-slow-call shape this codebase has already
		//     been burned by twice (the logCS/allocator ABBA deadlock, and why that invariant did
		//     not transfer to mt_csEnter). A fix wants that question answered first, on purpose,
		//     rather than a two-line change shipped on the strength of it looking obvious.
		u32 purged = 0, purged_dlg = 0;
		do
		{
			it = std::find(m_aDelayedPackets.begin(), m_aDelayedPackets.end(), pp);
			if (it != m_aDelayedPackets.end())
			{
				if (it->MsgType == M_XRNET_DIALOG_ACTION) ++purged_dlg;
				++purged;
				m_aDelayedPackets.erase(it);
				Msg("removing packet from delayed event storage");
			}
			else
				break;
		}
		while (true);
		{
			extern u32 g_coop_game_thread_id;
			const u32 tid = GetCurrentThreadId();
			Msg("- COOP(disc): PURGE for client 0x%08x purged=%u dlg=%u queue_left=%u "
				"tid=%u game=%u on_game_thread=%d",
				pp.SenderID.value(), purged, purged_dlg, u32(m_aDelayedPackets.size()),
				tid, g_coop_game_thread_id,
				(g_coop_game_thread_id != 0 && tid == g_coop_game_thread_id) ? 1 : 0);
			FlushLog();
		}

		if (pOwner)
		{
			game->CleanDelayedEventFor(pOwner->ID);
		}

		//.		if (!alife_client->flags.bVerified)
		xrClientData* xr_client = static_cast<xrClientData*>(alife_client);
		m_disconnected_clients.Add(xr_client); //xr_delete(alife_client);				
	}
}

void xrServer::GetPooledState(xrClientData* xrCL)
{
	xrClientData* pooled_client = m_disconnected_clients.Get(xrCL);
	if (!pooled_client)
		return;

	NET_Packet tmp_packet;
	u16 tmp_fake;
	tmp_packet.w_begin(M_SPAWN);
	pooled_client->ps->net_Export(tmp_packet, TRUE);
	tmp_packet.r_begin(tmp_fake);
	xrCL->ps->net_Import(tmp_packet);
	xrCL->flags.bReconnect = TRUE;
	xr_delete(pooled_client);
}

//--------------------------------------------------------------------
int g_Dump_Update_Write = 0;

#ifdef DEBUG
INT g_sv_SendUpdate = 0;
#endif

// MP fork (§3c): stamped by CLevel::OnFrame, which IS the game loop. It used to be stamped here,
// in xrServer::Update, and that was wrong in a way only the verifying run caught — Update has
// four call sites and one of them ran on a second thread, so the gate was comparing a thread to
// whichever thread had most recently run Update rather than to the game thread.
extern u32 g_coop_game_thread_id;

void xrServer::Update()
{
	if (Level().IsDemoPlayStarted() || Level().IsDemoPlayFinished())
		return; //diabling server when demo is playing

	NET_Packet Packet;
#ifdef DEBUG
	VERIFY(verify_entities());
#endif
	ProceedDelayedPackets();
	// game update
	game->ProcessDelayedEvent();
	game->Update();

	// spawn queue
	u32 svT = Device.TimerAsync();
	while (!(q_respawn.empty() || (svT < q_respawn.begin()->timestamp)))
	{
		// get
		svs_respawn R = *q_respawn.begin();
		q_respawn.erase(q_respawn.begin());

		// 
		CSE_Abstract* E = ID_to_entity(R.phantom);
		E->Spawn_Write(Packet,FALSE);
		u16 ID;
		Packet.r_begin(ID);
		R_ASSERT(M_SPAWN==ID);
		ClientID clientID;
		clientID.set(0xffff);
		Process_spawn(Packet, clientID);
	}


	SendUpdatesToAll();


	if (game->sv_force_sync) Perform_game_export();
#ifdef DEBUG
	VERIFY(verify_entities());
#endif
	//-----------------------------------------------------

	PerformCheckClientsForMaxPing();
	Flush_Clients_Buffers();

	if (0 == (Device.dwFrame % 100)) //once per 100 frames
	{
		UpdateBannedList();
	}
}

void _stdcall xrServer::SendGameUpdateTo(IClient* client)
{
	xrClientData* xr_client = static_cast<xrClientData*>(client);
	VERIFY(xr_client);
	if (!xr_client->net_Ready)
	{
		return;
	}

	if (!HasBandwidth(client)
#ifdef DEBUG
			&& !g_sv_SendUpdate
#endif
	)
	{
		return;
	}

	NET_Packet Packet;
	u16 PacketType = M_UPDATE;
	Packet.w_begin(PacketType);
	game->net_Export_Update(Packet, xr_client->ID, xr_client->ID);
	SendTo(xr_client->ID, Packet, net_flags(FALSE,TRUE));
}

// §4 step 3 (increment C2): record that a creature is decision-driven. Called from
// CLevel::coop_broadcast_decision each time a decision is broadcast for the subject, so a subject
// under active decisions keeps a fresh timestamp; MakeUpdatePackets ages the entry out after
// COOP_DECISION_DRIVEN_TTL_MS and only throttles ids that are still live.
void xrServer::coop_mark_decision_driven(u16 id)
{
	m_coop_decision_driven[id] = Device.dwTimeGlobal;
}

// §3 win #2 (radius culling): push one connected client's actor position as a cull anchor. Only REAL
// player actors count (owner set, id != 0) — the dedicated server's loopback self-client (fake host,
// owner id 0) is not a real viewer. o_Position is kept live by the M_CL_UPDATE peek (see OnMessage).
void xrServer::coop_cull_anchor_cb(IClient* C)
{
	xrClientData* CL = static_cast<xrClientData*>(C);
	if (CL && CL->owner && CL->owner->ID != 0 && _valid(CL->owner->o_Position))
		m_coop_cull_anchors.push_back(CL->owner->o_Position);
}

void xrServer::coop_gather_cull_anchors()
{
	m_coop_cull_anchors.clear();
	fastdelegate::FastDelegate1<IClient*, void> fd;
	fd.bind(this, &xrServer::coop_cull_anchor_cb);
	ForEachClientDo(fd);
}

// §3 win #2b inc2: is -coop_cull_radius active (cached). Under cull, the relevance manager owns
// per-client creature spawn/despawn, so the stock global creature-spawn broadcast is gated off.
bool xrServer::coop_cull_on()
{
	if (m_coop_cull_on < 0)
		m_coop_cull_on = strstr(Core.Params, "-coop_cull_radius ") ? 1 : 0;
	return m_coop_cull_on == 1;
}

// §3 win #2b inc2: an AMBIENT creature (any creature EXCEPT a player actor) whose stock spawn to real
// clients must be suppressed under cull, so the relevance manager is the sole per-client spawner. This
// covers monsters, human NPCs, AND lightweight creatures like crows (CSE_ALifeCreatureCrow derives from
// CSE_ALifeCreatureAbstract but is neither monster_ nor human_abstract — an earlier monster||human gate
// let crows slip to the stock path and re-broadcast every respawn). The player ACTOR is a creature too
// but must ALWAYS replicate (peer bodies), so it is explicitly excluded. Zero cost (false) when cull off.
bool xrServer::coop_cull_gate_creature(CSE_Abstract* E)
{
	return coop_cull_on() && E && E->cast_creature_abstract()
		&& !smart_cast<CSE_ALifeCreatureActor*>(E);
}

// §3 win #2b inc2: does the decision system currently own this creature's replication (within TTL)?
// Decision-driven creatures are always relevant — the relevance manager never despawns them.
bool xrServer::coop_is_decision_driven(u16 id)
{
	xr_map<u16, u32>::iterator it = m_coop_decision_driven.find(id);
	return it != m_coop_decision_driven.end()
		&& (Device.dwTimeGlobal - it->second) < COOP_DECISION_DRIVEN_TTL_MS;
}

// §3 win #2b inc2: net_Spawn creature E on ONE client as a stripped/remote copy (server stays
// authoritative). Mirrors Perform_connect_spawn's owner!=0 branch + the §9.3 reconnection primitive.
void xrServer::coop_relevance_spawn(xrClientData* CL, CSE_Abstract* E)
{
	NET_Packet P;
	Flags16 save = E->s_flags;
	E->s_flags.set(M_SPAWN_UPDATE, TRUE);
	E->Spawn_Write(P, FALSE);   // FALSE = remote/stripped (client does NOT take authoritative control)
	E->UPDATE_Write(P);
	E->s_flags = save;
	SendTo(CL->ID, P, net_flags(TRUE, TRUE));
}

// §3 win #2b inc2: destroy one creature's copy on ONE client (SendTo bypasses server event processing,
// so only that client acts). Mirrors the §9.3 reconnection despawn primitive (game_sv_single.cpp).
void xrServer::coop_relevance_despawn(xrClientData* CL, u16 id)
{
	NET_Packet P;
	P.w_begin(M_EVENT);
	P.w_u32(Level().timeServer());
	P.w_u16(GE_DESTROY);
	P.w_u16(id);
	SendTo(CL->ID, P, net_flags(TRUE, TRUE));
}

// §3 win #2b inc2: drop a client's known-set (CodeRabbit inc1 — call on disconnect so a reused client id
// never inherits stale relevance state).
void xrServer::coop_clear_relevance(u32 client_id)
{
	m_coop_rel_known.erase(client_id);
	m_coop_c2_last_per_client.erase(client_id); // §3 inc3: drop this client's per-client throttle state too
}

// §3 win #2b inc2: forget one creature id everywhere its co-op replication state is tracked. Called at the
// stock GE_DESTROY broadcast site (Perform_destroy) so state stays in lockstep with what the client holds —
// this closes an id-reuse race: an id freed on offline and re-used <500ms later by a new creature would
// otherwise linger and mis-drive replication. The decision-driven TTL is cleared UNCONDITIONALLY (a reused
// id must not inherit the previous occupant's throttle/relevance for up to COOP_DECISION_DRIVEN_TTL_MS —
// CodeRabbit); the per-client known-sets only exist under cull, so that pass is cull-gated.
void xrServer::coop_forget_relevance_id(u16 id)
{
	m_coop_decision_driven.erase(id);
	if (!coop_cull_on()) return;
	for (auto& kv : m_coop_rel_known)
		kv.second.erase(id);
}

// §3 win #2b increment 2 (WIRED): for one real client, spawn near ambient creatures + despawn far/offline
// ones per-client, keeping m_coop_rel_known[CL] == the set actually spawned on that client. R_in/R_out
// hysteresis kills boundary flicker; decision-driven creatures are never despawned (the decision system
// owns them). Runs at ~2 Hz under -coop_cull_radius; the stock global creature-spawn broadcast is gated
// off (coop_cull_gate_creature) so this is the sole owner of per-client creature spawn/despawn.
void xrServer::coop_relevance_client_cb(IClient* C)
{
	xrClientData* CL = static_cast<xrClientData*>(C);
	if (!CL || !CL->owner || CL->owner->ID == 0 || !_valid(CL->owner->o_Position))
		return;

	// R_in = cull radius; R_out = 1.33x (hysteresis band kills boundary flicker).
	static float s_rin2 = -1.f, s_rout2 = -1.f;
	if (s_rin2 < 0.f)
	{
		float r = 150.f;
		if (const char* p = strstr(Core.Params, "-coop_cull_radius "))
		{
			const float v = (float)atof(p + xr_strlen("-coop_cull_radius "));
			if (v > 0.f) r = v;
		}
		s_rin2 = r * r;
		s_rout2 = (r * 1.33f) * (r * 1.33f);
	}

	const Fvector apos = CL->owner->o_Position;
	xr_map<u16, char>& known = m_coop_rel_known[CL->ID.value()];
	int spawned = 0, despawned = 0;

	// SPAWN pass: near ambient creatures not yet spawned on this client.
	for (xrS_entities::iterator I = entities.begin(); I != entities.end(); ++I)
	{
		CSE_Abstract& E = *(I->second);
		if (0 == E.owner || !coop_cull_gate_creature(&E)) continue; // ambient (non-actor) creatures only
		if (E.owner == CL) continue;                                // client already owns an authoritative copy
		if (known.find(E.ID) != known.end()) continue;             // already spawned on this client
		// Spawn near creatures, OR any decision-driven creature regardless of distance — the decision
		// system resolves subject_id via object_by_id on the client, so it MUST exist there even when the
		// player is far (with the global broadcast now gated, this pass is its only spawn path under cull).
		if (coop_is_decision_driven(E.ID) || apos.distance_to_sqr(E.o_Position) <= s_rin2)
		{
			coop_relevance_spawn(CL, &E);
			known[E.ID] = 1;
			++spawned;
		}
	}
	// DESPAWN pass: known creatures now beyond R_out, or gone offline.
	for (xr_map<u16, char>::iterator it = known.begin(); it != known.end(); )
	{
		CSE_Abstract* E = ID_to_entity(it->first);
		// "gone" = no longer a cullable creature entity (offline/destroyed, or an id reused by a non-creature
		// / a player actor). A transiently OWNERLESS but still online+near creature is NOT gone — despawning
		// on that would flicker (creatures are ownerless for ~1 frame on switch); real removal is handled at
		// the Perform_destroy GE_DESTROY site instead. The gone branch only forgets it (no GE_DESTROY re-send),
		// so a reused id that became a peer actor never gets a stray despawn.
		const bool gone = (!E || !coop_cull_gate_creature(E));
		if (!gone && coop_is_decision_driven(E->ID)) { ++it; continue; } // decision system owns it — keep
		if (gone)
		{
			// Already destroyed on the client by the stock offline GE_DESTROY broadcast (Perform_destroy);
			// just forget it — do NOT re-send a GE_DESTROY for a now-unknown id.
			it = known.erase(it);
			++despawned;
		}
		else if (apos.distance_to_sqr(E->o_Position) > s_rout2)
		{
			coop_relevance_despawn(CL, it->first);
			it = known.erase(it);
			++despawned;
		}
		else ++it;
	}

	if (spawned || despawned)
	{
		Msg("~ COOP_REL: [client 0x%08x] relevant=%u spawned=%d despawned=%d",
			CL->ID.value(), (u32)known.size(), spawned, despawned);
		FlushLog();
	}
}

void xrServer::coop_relevance_diag()
{
	fastdelegate::FastDelegate1<IClient*, void> fd;
	fd.bind(this, &xrServer::coop_relevance_client_cb);
	ForEachClientDo(fd);
}

void xrServer::MakeUpdatePackets(xrClientData* target_client)
{
	NET_Packet tmpPacket;
	u32 position;

	// §3 win #2b inc3 (per-client update packets): when target_client is set, this pass builds the update
	// stream for THAT client only — ambient creatures are filtered to its relevance set (m_coop_rel_known),
	// so spread players stop paying for each other's NPCs. Player actors + non-creature entities are NOT
	// filtered (they stream to everyone). target_client == nullptr keeps the stock global-union build.
	const bool per_client = (target_client != nullptr);
	xr_map<u16, char>* known = per_client ? &m_coop_rel_known[target_client->ID.value()] : nullptr;

	// §3 inc3: throttle bookkeeping — per-client in per-client mode (so -coop_npc_hz / -coop_update_throttle
	// stay correct when a creature is relevant to several clients), the shared static map otherwise.
	static xr_map<u16, u32> s_c2_last_sent_global; // last throttled send per creature (global-union mode)
	xr_map<u16, u32>& c2_last = per_client
		? m_coop_c2_last_per_client[target_client->ID.value()]
		: s_c2_last_sent_global;

	m_updator.begin_updates();

	// MP fork (§ world-NPC-replication Phase 1): -coop_npcdiag — localize why ambient online
	// A-Life creatures don't reach co-op clients. Per pass, categorize every creature CSE by the
	// SAME gate it hits in this loop (owner==0 / !net_Ready / phantom / !relevant / eligible-to-
	// stream), and flush ONE aggregate line every ~3s. Pure diagnostic (no behavior change), gated
	// so stock/normal co-op is untouched. See dev/WORLD_NPC_REPLICATION_PLAN.md.
	static int s_npcdiag = -2; // -2 unparsed, -1 off, 1 on
	if (s_npcdiag == -2)
		s_npcdiag = strstr(Core.Params, "-coop_npcdiag") ? 1 : -1;
	int nd_creatures = 0, nd_owner0 = 0, nd_notready = 0, nd_phantom = 0, nd_notrel = 0, nd_eligible = 0;
	int nd_culled = 0;

	// §3 win #2: -coop_cull_radius <m> — stream a creature only if it is within <m> of SOME real player
	// actor; skip the rest (a client only needs NPCs near it, so far ones cost nothing). Gathered ONCE
	// per pass. If no real actor is anchored (empty), culling is disabled (stream all, as today) so the
	// no-client / loopback-only case is never starved. Gated; zero cost when off.
	static float s_cull_r2 = -2.f; // -2 unparsed, -1 off, else radius^2
	if (s_cull_r2 == -2.f)
	{
		s_cull_r2 = -1.f;
		if (const char* p = strstr(Core.Params, "-coop_cull_radius "))
		{
			const float r = (float)atof(p + xr_strlen("-coop_cull_radius "));
			if (r > 0.f) s_cull_r2 = r * r;
		}
	}
	const bool cull_on = (s_cull_r2 > 0.f);
	// Global-union radius culling only applies to the stock (nullptr) build; the per-client build filters by
	// the relevance set instead, so it needs no anchors.
	if (cull_on && !per_client) coop_gather_cull_anchors();
	const bool cull_active = cull_on && !per_client && !m_coop_cull_anchors.empty();

	xrS_entities::iterator I = entities.begin();
	xrS_entities::iterator E = entities.end();
	for (; I != E; ++I)
	{
		//all entities
		CSE_Abstract& Test = *(I->second);

		if (s_npcdiag == 1 && Test.cast_creature_abstract())
		{
			nd_creatures++;
			if (0 == Test.owner)                                   nd_owner0++;
			else if (!Test.net_Ready)                              nd_notready++;
			else if (Test.s_flags.is(M_SPAWN_OBJECT_PHANTOM))      nd_phantom++;
			else if (!Test.Net_Relevant() && !xr_enet::enabled())  nd_notrel++;
			else                                                   nd_eligible++;
		}

		if (0 == Test.owner) continue;
		if (!Test.net_Ready) continue;
		if (Test.s_flags.is(M_SPAWN_OBJECT_PHANTOM)) continue; // Surely: phantom

		// MP fork (§14 step 7 P4 D2, run 3): a PLAYER's body never belongs in the generic entity
		// update. Two authorities were fighting over it and the server's copy always won: the
		// server streams this CSE at psNET_ServerUpdate Hz, and `CActor::net_Import_Base` applies
		// `SetfHealth(health)` on any client — there is NO Local() guard on that path (there is
		// one on the M_CL_UPDATE relay, which is why this went unnoticed). So a save-restored
		// body, whose CSE the server's own unplaced object rots to hp 0.00 (P2 §3b), was killing
		// the returning player's actor ten times a second: dead => `net_Relevant()` false =>
		// no M_CL_UPDATE => the CSE never gets corrected => dead again. The reclaim's health
		// restore could not win against a broadcast that reinstates the zero every tick.
		// Player bodies are already replicated by the §19 M_CL_UPDATE relay — position AND
		// health, same net_Export format — so this stream is pure redundancy for them, and
		// removing it leaves exactly one authority over a player's body: that player.
		if (xr_enet::enabled() && smart_cast<CSE_ALifeCreatureActor*>(&Test))
			continue;

		// MP fork (§19 co-op): among creatures, stock X-Ray declares ONLY the actor
		// net-relevant (CSE_ALifeCreatureActor::Net_Relevant is the single override) —
		// vanilla MP has no A-Life, so NPCs and mutants never needed replicating and
		// CSE_Abstract::Net_Relevant()'s FALSE default was never a problem. Under co-op
		// the dedicated server simulates the whole population and must stream it, or every
		// NPC stands frozen in a T-pose forever: CCustomMonster::UpdateCL bails out at
		// "if (NET.empty()) return;" and only animates (SelectAnimation) once it has two
		// network updates to interpolate between. Creatures already serialise everything a
		// puppet needs (position, model yaw, torso yaw/pitch/roll, health) in
		// CSE_ALifeCreatureAbstract::UPDATE_Write, so just let them through here rather
		// than changing Net_Relevant() itself (which is multiply inherited by the human /
		// monster CSE classes and would need per-class disambiguation).
		if (!Test.Net_Relevant() && !(xr_enet::enabled() && Test.cast_creature_abstract()))
			continue;

		// §3 win #2b inc3 (per-client update packets): stream an AMBIENT creature to THIS client only if it
		// is in the client's relevance set (i.e. actually spawned there — see coop_relevance_client_cb) or
		// is decision-driven (always relevant). Player ACTORS (creature_abstract but smart_cast-actor) and
		// non-creature entities are never filtered — they replicate to everyone. This drops the global-union
		// waste: with spread players, client A no longer receives client B's NPCs' updates.
		if (per_client)
		{
			if (Test.cast_creature_abstract() && !smart_cast<CSE_ALifeCreatureActor*>(&Test))
			{
				const bool have = (known->find(Test.ID) != known->end());
				if (!have && !coop_is_decision_driven(Test.ID)) { if (s_npcdiag == 1) nd_culled++; continue; }
			}
		}
		// §3 win #2 (radius culling): a creature that no real player is near is skipped — the client
		// can't see it, so streaming its position/health is pure waste. Only cull CREATURES (players
		// always stream); a decision-driven NPC is EXEMPT (the decision system owns its replication, and
		// it's a tiny set). If it's within radius of ANY anchor, keep it.
		else if (cull_active && Test.cast_creature_abstract())
		{
			const bool decision_driven = (m_coop_decision_driven.find(Test.ID) != m_coop_decision_driven.end())
				&& ((Device.dwTimeGlobal - m_coop_decision_driven[Test.ID]) < COOP_DECISION_DRIVEN_TTL_MS);
			if (!decision_driven)
			{
				bool in_range = false; // NB: 'near' is a legacy Windows macro (windef.h) — do not use it
				for (const Fvector& a : m_coop_cull_anchors)
					if (a.distance_to_sqr(Test.o_Position) <= s_cull_r2) { in_range = true; break; }
				if (!in_range) { if (s_npcdiag == 1) nd_culled++; continue; }
			}
		}

		// MP fork (§4 step 3 recon): optional soft-correction CADENCE PROBE. Under
		// -coop_update_throttle <ms>, cap each replicated creature's M_UPDATE to at most once per
		// <ms> (instead of every pass) so we can measure how sparse the stream can get before the
		// client's puppet interpolation visibly degrades — that empirically sizes the
		// M_COOP_CORRECTION cadence the client-local-AI inversion will need (see
		// dev/DECISION_REPLICATION_PLAN.md, Step 3). Gated + creature-only: stock/normal co-op is
		// untouched (throttle stays off). This does NOT change the puppet AI-skip; it only thins the
		// stream on the existing interpolation path.
		static int s_throttle_ms = -2; // -2 = unparsed, -1 = disabled
		if (s_throttle_ms == -2)
		{
			s_throttle_ms = -1;
			if (const char* p = strstr(Core.Params, "-coop_update_throttle "))
				s_throttle_ms = atoi(p + xr_strlen("-coop_update_throttle "));
		}
		// §3 bandwidth win #1: -coop_npc_hz <ms> caps EVERY replicated creature's M_UPDATE to at most
		// once per <ms> (a rate throttle for ALL creatures, not just the decision-driven set the C2
		// throttle covers). COOP_BW measured ~210 KB/s/client at 30 Hz dense-stream; at 100 ms (~10 Hz)
		// this is ~3x less, well within the interpolation budget (step-3 probe: ~1 Hz holds sub-metre
		// for 1-2 m/s NPCs). Gated (default -1 = off) so stock/normal co-op is untouched.
		static int s_npc_hz_ms = -2; // -2 = unparsed, -1 = disabled
		if (s_npc_hz_ms == -2)
		{
			s_npc_hz_ms = -1;
			if (const char* p = strstr(Core.Params, "-coop_npc_hz "))
				s_npc_hz_ms = atoi(p + xr_strlen("-coop_npc_hz "));
		}
		// §4 step 3 (increment C2): the throttle now applies ONLY to creatures the decision system
		// currently owns (a live, non-expired m_coop_decision_driven entry) — these run their AI on
		// the client and need only a sparse soft-correction. Normal puppets are NOT in the set and
		// keep dense streaming (no regression). (The step-3 cadence PROBE threw the throttle at all
		// creatures; that measurement is done — see DECISION_REPLICATION_PLAN.md — so it is now
		// membership-gated per the inversion plan.)
		// throttle bookkeeping deferred to AFTER the write below (CodeRabbit): the timestamp must be
		// stamped only once a NON-EMPTY update is actually queued, else an ObjectSize==0 pass (packet
		// dropped) would still consume a throttle window and suppress the next real correction.
		bool c2_throttled_send = false;         // any throttled creature (C2 driven OR §3 -coop_npc_hz)
		bool c2_is_driven = false;              // was the interval the decision-driven C2 window?
		u32  c2_gap = 0;
		int  c2_eff_ms = 0;                     // the interval actually applied (for the -dbg log)
		if ((s_throttle_ms > 0 || s_npc_hz_ms > 0) && xr_enet::enabled() && Test.cast_creature_abstract())
		{
			const u32 now = Device.dwTimeGlobal;
			auto dd = m_coop_decision_driven.find(Test.ID);
			const bool driven = (dd != m_coop_decision_driven.end())
				&& ((now - dd->second) < COOP_DECISION_DRIVEN_TTL_MS);
			// Effective throttle for THIS creature: a decision-driven NPC uses the C2 soft-correct window
			// (-coop_update_throttle); every other creature uses the §3 global rate cap (-coop_npc_hz).
			const int eff_ms = (driven && s_throttle_ms > 0) ? s_throttle_ms : s_npc_hz_ms;
			if (eff_ms > 0)
			{
				c2_eff_ms = eff_ms; c2_is_driven = driven;
				auto it = c2_last.find(Test.ID);
				if (it == c2_last.end())
				{
					// First time throttled: seed a per-id PHASE (id % interval) so creatures don't all
					// align on the same window boundary. Without this the whole population sends on the
					// same pass every <interval> ms (thundering herd) — the average drops but the PEAK
					// payload stays full; the phase spreads sends across passes, flattening the peak
					// toward the ideal rate. Its first update lands on its staggered window.
					c2_last[Test.ID] = now - (Test.ID % u32(eff_ms));
					continue;
				}
				if ((now - it->second) < u32(eff_ms))
					continue; // within the throttle window — skip this creature's update this pass
				c2_throttled_send = true;
				c2_gap = now - it->second;
			}
		}

		tmpPacket.B.count = 0;
		// write specific data
		{
			tmpPacket.w_u16(Test.ID);
			tmpPacket.w_chunk_open8(position);
			Test.UPDATE_Write(tmpPacket);
			u32 ObjectSize = u32(tmpPacket.w_tell() - position) - sizeof(u8);
			tmpPacket.w_chunk_close8(position);

			if (ObjectSize == 0) continue;
#ifdef DEBUG
			if (g_Dump_Update_Write) Msg("* %s : %d", Test.name(), ObjectSize);
#endif
			// §4 step 3 (C2): a real update is going out — NOW stamp the throttle time (see above).
			if (c2_throttled_send)
			{
				// PHASE-PRESERVING: advance the schedule by exactly one interval, not to `now`. Stamping
				// `now` re-aligns every creature that happened to send on the same pass — the per-id phase
				// jitter then decays after one cycle and the thundering-herd peak returns. Advancing by
				// the interval keeps each creature on its own staggered cadence. Resync to `now` only after
				// a big gap (creature just came online / server hitch) so we never burst-catch-up.
				u32& ls = c2_last[Test.ID];
				ls = (Device.dwTimeGlobal - ls > 2u * u32(c2_eff_ms)) ? Device.dwTimeGlobal
				                                                      : ls + u32(c2_eff_ms);
				if (strstr(Core.Params, "-dbg"))
					Msg("* COOP_THROTTLE: id=%u SENT gap=%ums (interval=%dms, %s)",
					    Test.ID, c2_gap, c2_eff_ms, c2_is_driven ? "driven" : "npc_hz");
			}
			m_updator.write_update_for(Test.ID, tmpPacket);
		}
	} //all entities

	// §world-NPC-replication Phase 1: flush the per-pass creature tally at most once per ~3s. Under inc3
	// per-client builds this logs ONE client's view per window (population is the same; eligible/culled are
	// that client's streamed / filtered-out counts) — the 3s static guard keeps it to one line per window.
	if (s_npcdiag == 1)
	{
		static u32 s_nd_last = 0;
		if ((Device.dwTimeGlobal - s_nd_last) >= 3000)
		{
			s_nd_last = Device.dwTimeGlobal;
			Msg("~ COOP_NPCDIAG: creatures=%d eligible=%d culled=%d | skip: owner0=%d notready=%d phantom=%d notrel=%d (clients=%u anchors=%u)",
				nd_creatures, nd_eligible, nd_culled, nd_owner0, nd_notready, nd_phantom, nd_notrel,
				GetClientsCount(), (u32)m_coop_cull_anchors.size());
			FlushLog();
		}
	}

	m_updator.end_updates(m_update_begin, m_update_end);
}

void xrServer::SendUpdatePacketsToAll()
{
	m_last_updates_size = 0;
	for (update_iterator_t i = m_update_begin; i != m_update_end; ++i)
	{
		NET_Packet& to_send = **i;
		if (to_send.B.count > 2)
		{
			m_last_updates_size += to_send.B.count;
			SendBroadcast(GetServerClient()->ID, to_send, net_flags(FALSE,TRUE));
			if (Level().IsDemoSave())
			{
				Level().SavePacket(to_send);
			}
		}
	}
}

// §3 win #2b inc3: collect one REAL client (owner set, id != 0 — the loopback self-client is excluded, as
// in coop_gather_cull_anchors) as a per-client update target.
void xrServer::coop_collect_update_target_cb(IClient* C)
{
	xrClientData* CL = static_cast<xrClientData*>(C);
	if (CL && CL->owner && CL->owner->ID != 0)
		m_coop_update_targets.push_back(CL);
}

// §3 win #2b inc3: send the packets built by MakeUpdatePackets(target) to ONE client (the per-client
// update path). Mirrors SendUpdatePacketsToAll but SendTo a single client instead of SendBroadcast.
void xrServer::SendUpdatePacketsTo(ClientID cid, bool save_demo)
{
	m_last_updates_size = 0; // this client's size (the caller aggregates across clients for the bw diag)
	for (update_iterator_t i = m_update_begin; i != m_update_end; ++i)
	{
		NET_Packet& to_send = **i;
		if (to_send.B.count > 2)
		{
			m_last_updates_size += to_send.B.count;
			SendTo(cid, to_send, net_flags(FALSE, TRUE));
			// Demo recording must capture the pass ONCE, not once per client (CodeRabbit): the per-client
			// dispatch calls this for every real client, so only the first target saves.
			if (save_demo && Level().IsDemoSave())
				Level().SavePacket(to_send);
		}
	}
}

void xrServer::SendUpdatesToAll()
{
	// MP fork (§19 co-op): the co-op server runs game_sv_single, so IsGameTypeSingle() is TRUE
	// and this returned immediately — meaning the server has never sent the generic entity
	// UPDATE stream at all. That is why replicated creatures spawn correctly and then stand
	// frozen forever: nothing ever delivers their position/orientation to clients.
	// Player actors were unaffected because the fork relays those through its own M_CL_UPDATE
	// path, which is exactly why peers move but NPCs do not.
	// Under ENet co-op the server owns the world and MUST feed entity updates to its clients.
	if (IsGameTypeSingle() && !xr_enet::enabled())
		return;

	KickCheaters();


	//sending game_update 
	fastdelegate::FastDelegate1<IClient*, void> sendtofd;
	sendtofd.bind(this, &xrServer::SendGameUpdateTo);
	ForEachClientDoSender(sendtofd);

	if ((Device.dwTimeGlobal - m_last_update_time) >= u32(1000 / psNET_ServerUpdate))
	{
		// §3 win #2b inc3 (per-client update packets): under -coop_cull_radius, build+send a SEPARATE update
		// stream per real client — each filtered to that client's relevance set — so spread players no longer
		// pay for each other's NPCs. If there is no real client (loopback-only), or cull is off, fall back to
		// the stock single global-union build broadcast to all. Gated; zero cost when the flag is off.
		// NOTE: builds run sequentially (build client A → send A → build B → send B), reusing one compressor;
		// this is safe ONLY while g_sv_traffic_optimization_level == eto_none (the co-op default — no
		// cross-build delta cache / equal-update suppression that would share per-entity state across the
		// per-client builds). If traffic optimization is ever enabled, we fall back to the stock global
		// broadcast rather than risk cross-client corruption (guarded at runtime, not just documented).
		bool per_client_sent = false;
		if (coop_cull_on() && g_sv_traffic_optimization_level == eto_none)
		{
			m_coop_update_targets.clear();
			fastdelegate::FastDelegate1<IClient*, void> cfd;
			cfd.bind(this, &xrServer::coop_collect_update_target_cb);
			ForEachClientDo(cfd);
			if (!m_coop_update_targets.empty())
			{
				u32 total_sent = 0;
				bool first = true;
				for (xrClientData* CL : m_coop_update_targets)
				{
					MakeUpdatePackets(CL);              // build THIS client's filtered stream
					SendUpdatePacketsTo(CL->ID, first);  // send only to it; demo-save the pass once (first only)
					total_sent += m_last_updates_size;
					first = false;
				}
				// bw diag samples m_last_updates_size as one client's rate — report the per-client AVERAGE
				// across this pass, not just the last client's size (CodeRabbit).
				m_last_updates_size = total_sent / (u32)m_coop_update_targets.size();
				per_client_sent = true;
			}
		}
		if (!per_client_sent)
		{
			MakeUpdatePackets();
			SendUpdatePacketsToAll();
		}

		// §3 bandwidth quantification (-coop_npcdiag): the co-op server dense-streams the whole online
		// creature population's M_UPDATE to EVERY client (Phase-1 diag: ~84 creatures, eligible==all). §3
		// ("replicate decisions, not the simulation") exists to avoid exactly that, so measure the real
		// per-client cost: m_last_updates_size is the update payload bytes THIS pass, sent to each client
		// at psNET_ServerUpdate Hz, so per-client rate ≈ size × Hz. Averaged + flushed every ~3s. Gated;
		// zero cost when off. Sizes whether §3 radius-culling / the decision-inversion is urgent.
		{
			static int s_bw = -2; // -2 unparsed, -1 off, 1 on
			if (s_bw == -2) s_bw = strstr(Core.Params, "-coop_npcdiag") ? 1 : -1;
			if (s_bw == 1)
			{
				static u32 s_acc = 0, s_n = 0, s_max = 0, s_last = 0;
				s_acc += m_last_updates_size; s_n++;
				if (m_last_updates_size > s_max) s_max = m_last_updates_size;
				if ((Device.dwTimeGlobal - s_last) >= 3000 && s_n > 0)
				{
					const u32 avg = s_acc / s_n;
					const u32 hz  = psNET_ServerUpdate ? psNET_ServerUpdate : 10;
					Msg("~ COOP_BW: update payload avg=%u max=%u bytes/pass | ~%u bytes/s per client "
						"(%.1f KB/s, %u Hz) | clients=%u", avg, s_max, avg * hz,
						(avg * hz) / 1024.f, hz, GetClientsCount());
					FlushLog();
					s_acc = 0; s_n = 0; s_max = 0; s_last = Device.dwTimeGlobal;
				}
			}
		}

#ifdef DEBUG
		g_sv_SendUpdate = 0;
#endif
		if (game->sv_force_sync) Perform_game_export();
#ifdef DEBUG
		VERIFY(verify_entities());
#endif
		m_last_update_time = Device.dwTimeGlobal;
	}

	// §3 win #2b increment 2 (WIRED): run the per-client relevance pass at ~2 Hz (relevance changes at
	// player speed, not per 30 Hz update pass) when -coop_cull_radius is on. Spawns near ambient creatures
	// + despawns far/offline ones per-client (the sole owner of per-client creature spawn/despawn — the
	// stock global creature-spawn broadcast is gated off). Gated; zero cost when the flag is off.
	if (coop_cull_on() && (Device.dwTimeGlobal - m_coop_rel_last) >= 500)
	{
		m_coop_rel_last = Device.dwTimeGlobal;
		coop_relevance_diag();
	}

	if (m_file_transfers)
	{
		m_file_transfers->update_transfer();
		m_file_transfers->stop_obsolete_receivers();
	}
}

xr_vector<shared_str> _tmp_log;

void console_log_cb(LPCSTR text)
{
	_tmp_log.push_back(text);
}

u32 xrServer::OnDelayedMessage(NET_Packet& P, ClientID sender) // Non-Zero means broadcasting with "flags" as returned
{
	u16 type;
	P.r_begin(type);

	//csPlayers.Enter			();
#ifdef DEBUG
	VERIFY(verify_entities());
#endif
	xrClientData* CL = ID_to_client(sender);
	//R_ASSERT2						(CL, make_string("packet type [%d]",type).c_str());

	switch (type)
	{
	case M_CLIENT_REQUEST_CONNECTION_DATA:
		{
			IClient* tmp_client = net_players.GetFoundClient(
				ClientIdSearchPredicate(sender));
			VERIFY(tmp_client);
			OnCL_Connected(tmp_client);
			//OnCL_Connected				(CL);
		}
		break;
	case M_REMOTE_CONTROL_CMD:
		{
			if (CL->m_admin_rights.m_has_admin_rights)
			{
				string1024 buff;
				P.r_stringZ(buff);
				Msg("* Radmin [%s] is running command: %s", CL->ps->getName(), buff);
				SetLogCB(console_log_cb);
				_tmp_log.clear();
				LPSTR result_command;
				string64 tmp_number_str;
				xr_sprintf(tmp_number_str, " raid:%u", CL->ID.value());
				STRCONCAT(result_command, buff, tmp_number_str);
				Console->Execute(result_command);
				SetLogCB(NULL);

				NET_Packet P_answ;
				for (u32 i = 0; i < _tmp_log.size(); ++i)
				{
					P_answ.w_begin(M_REMOTE_CONTROL_CMD);
					P_answ.w_stringZ(_tmp_log[i]);
					SendTo(sender, P_answ, net_flags(TRUE,TRUE));
				}
			}
			else
			{
				NET_Packet P_answ;
				P_answ.w_begin(M_REMOTE_CONTROL_CMD);
				P_answ.w_stringZ("you dont have admin rights");
				SendTo(sender, P_answ, net_flags(TRUE,TRUE));
			}
		}
		break;
	case M_FILE_TRANSFER:
		{
			m_file_transfers->on_message(&P, sender);
		}
		break;
	// MP fork (§3c FIX): the dialogue action, now that it arrives here instead of on the pump
	// thread. This function is reached from xrServer::Update -> ProceedDelayedPackets, i.e. the
	// GAME thread — the only thread allowed to be in the Lua VM. The handler is unchanged; all
	// that moved is where it runs. r_begin above rewound the packet, so the payload reads
	// exactly as it did on arrival.
	case M_XRNET_DIALOG_ACTION:
		{
			coop_run_dialog_action(P);
		}
		break;
	}
#ifdef DEBUG
	VERIFY(verify_entities());
#endif
	//csPlayers.Leave					();
	return 0;
}

u32 xrServer::OnMessageSync(NET_Packet& P, ClientID sender)
{
	csMessage.Enter();
	u32 ret = OnMessage(P, sender);
	csMessage.Leave();
	return ret;
}

extern float g_fCatchObjectTime;

// MP fork (§19 co-op): execute a dialogue action a client asked us to run. Everything the
// script needs is addressable from the wire: the two speakers by object id, and the phrase by
// (dialog id, phrase id). CPhraseDialog::Load shares the already-parsed dialog data, so this
// is a lookup rather than a parse.
void xrServer::coop_run_dialog_action(NET_Packet& P)
{
	const u16 speaker_id = P.r_u16();
	const u16 partner_id = P.r_u16();
	string512 dialog_id = {0};
	string512 phrase_id = {0};
	P.r_stringZ_s(dialog_id); // bounds-checked: this is attacker-reachable input
	P.r_stringZ_s(phrase_id);

	// The client that sent this is the player the action runs on behalf of. There is no other
	// candidate and there must not be one: it is read off the packet's speaker rather than from
	// anything ambient, which is the whole §6.1 seam in one argument.
	coop_run_dialog_phrase(speaker_id, speaker_id, partner_id, dialog_id, phrase_id);
}

// MP fork (§14 step 8 phase 3 Q4): run one phrase's ACTION on behalf of `acting_id`.
//
// Split out of the wire handler above so the acting player is an argument rather than a
// coincidence. Two callers want that: the handler (where the actor is the speaker) and the Q4
// harness, which replays ONE phrase under two different acting scopes to show that the info write
// follows the acting player and not Actor() — the same phrase, the same speakers, opposite
// outcomes, which is the only way that routing is observable with a single client connected.
void xrServer::coop_run_dialog_phrase(u16 acting_id, u16 speaker_id, u16 partner_id,
                                      LPCSTR dialog_id, LPCSTR phrase_id)
{
	if (!g_pGameLevel || !dialog_id || !phrase_id || !dialog_id[0] || !phrase_id[0])
		return;

	// MP fork (§3c FIX, the gate). Everything below enters the Lua VM, and LuaJIT is
	// single-threaded, so the one thing that must be true here is that we are the game thread.
	// Printed on EVERY phrase, pass or fail, because a gate that only speaks when it is unhappy
	// is indistinguishable from a gate that was never reached — and this line is what the
	// harness asserts on instead of asserting on the absence of a crash.
	{
		const u32 tid = GetCurrentThreadId();
		const bool same = (g_coop_game_thread_id != 0) && (tid == g_coop_game_thread_id);
		// A mismatch is a '!' line, not a quieter shade of the same line: this is the exact
		// defect §3c spent a session finding, and if it ever comes back it should be greppable
		// as an error rather than as a field on a routine one.
		Msg("%s COOP(dlg-thread): phrase '%s/%s' runs on thread %u, game thread is %u, same=%s%s",
			same ? "-" : "!", dialog_id, phrase_id, tid, g_coop_game_thread_id,
			same ? "YES" : "NO",
			same ? "" : " — THE DIALOGUE ACTION IS OFF THE GAME THREAD (§3c has regressed)");
	}

	CGameObject* const speaker = smart_cast<CGameObject*>(Level().Objects.net_Find(speaker_id));
	CGameObject* const partner = smart_cast<CGameObject*>(Level().Objects.net_Find(partner_id));
	if (!speaker || !partner)
	{
		Msg("! XRNET: dialog action '%s/%s' dropped - speaker %u or partner %u not on the server",
			dialog_id, phrase_id, speaker_id, partner_id);
		FlushLog();
		return;
	}

	// Validate before touching the loaders. Both ids came off the wire, and the stock lookups
	// are assert-on-miss (CPhraseDialog::Load asserts an unknown dialog id, GetPhrase THROWs
	// an unknown phrase id) — which on a server means one malformed or merely out-of-date
	// packet takes the whole session down for everyone. Ask with no_assert and drop quietly.
	if (!CPhraseDialog::GetById(dialog_id, true))
	{
		Msg("! XRNET: dialog action dropped - unknown dialog id '%s'", dialog_id);
		FlushLog();
		return;
	}

	DIALOG_SHARED_PTR dialog(xr_new<CPhraseDialog>());
	dialog->Load(dialog_id);

	CPhrase* const phrase = dialog->coop_find_phrase(phrase_id);
	if (!phrase)
	{
		Msg("! XRNET: dialog action dropped - phrase '%s' not in dialog '%s'", phrase_id, dialog_id);
		FlushLog();
		return;
	}

	// MP fork (§19 co-op; §14 step 8 phase 1): mark which player is talking. Doc §6.1's
	// "check the interacting player" — every per-player fact this action touches resolves
	// against it: the task it may give (CGameTaskManager::GiveGameTaskToActor), the claim and
	// turn-in of §7.2's shared pool (step 8 phase 3 Q4), and any info portion the script layer
	// or the phrase's own <give_info> tags read or write. The scope restores the previous value
	// on EVERY exit path, including the script throwing: a context left set would silently
	// attribute the server's next autonomous world read to whichever player last talked to
	// someone.
	mp_coop_owner::acting_scope coop_acting(acting_id);

	// Same call the single-player path makes, just with the server's own objects.
	phrase->GetScriptHelper()->Action(speaker, partner, dialog_id, phrase_id);
}

// MP fork (§3c reproduction): defined in Level.cpp next to the game-thread half, so the two
// halves of the matched pair sit together and neither can drift from the other.
extern void coop_repro_bait(LPCSTR who);

// MP fork (§3c audit, dev/INSTABILITY_PLAN.md §4.4.ii): which network message this thread is
// dispatching, for the VM-touch audit in script_storage.cpp to name. Thread-local because that is
// exactly the question — "the pump thread was in M_EVENT when it entered the VM" — and a shared
// global would answer with whatever the OTHER thread was doing.
//
// 0xFFFFFFFF rather than 0: M_UPDATE is a real message type with a small value, so a sentinel of
// zero would report "no message" and "message 0" identically.
static const u32 coop_pump_msg_none = 0xFFFFFFFF;
static thread_local u32 t_coop_pump_msg = coop_pump_msg_none;

u32 coop_current_pump_message()
{
	return t_coop_pump_msg;
}

LPCSTR coop_pump_message_name(u32 type)
{
	// Only the cases that can plausibly reach script: the audit's reader wants to recognise the
	// path, not to decode the whole protocol, and an unnamed type still prints its number.
	switch (type)
	{
	case coop_pump_msg_none:      return "none (not in OnMessage)";
	case M_UPDATE:                return "M_UPDATE";
	case M_SPAWN:                 return "M_SPAWN";
	case M_EVENT:                 return "M_EVENT";
	case M_EVENT_PACK:            return "M_EVENT_PACK";
	case M_CL_UPDATE:             return "M_CL_UPDATE";
	case M_CLIENTREADY:           return "M_CLIENTREADY";
	case M_CHANGE_LEVEL:          return "M_CHANGE_LEVEL";
	case M_SAVE_GAME:             return "M_SAVE_GAME";
	case M_LOAD_GAME:             return "M_LOAD_GAME";
	case M_SAVE_PACKET:           return "M_SAVE_PACKET";
	case M_CHAT_MESSAGE:          return "M_CHAT_MESSAGE";
	case M_GAMEMESSAGE:           return "M_GAMEMESSAGE";
	case M_SWITCH_DISTANCE:       return "M_SWITCH_DISTANCE";
	case M_CL_AUTH:               return "M_CL_AUTH";
	case M_CREATE_PLAYER_STATE:   return "M_CREATE_PLAYER_STATE";
	case M_PLAYER_FIRE:           return "M_PLAYER_FIRE";
	case M_REMOTE_CONTROL_CMD:    return "M_REMOTE_CONTROL_CMD";
	case M_XRNET_DIALOG_ACTION:   return "M_XRNET_DIALOG_ACTION (§3c: should be QUEUED, not here)";
	default:                      return "<other>";
	}
}

// Restores rather than clears on the way out, because OnMessage RECURSES: M_EVENT_PACK unpacks
// and re-dispatches each inner message through this same function, and a scope that reset to
// 'none' would make every event after the first inner one report the wrong context.
namespace
{
struct coop_pump_msg_scope
{
	u32 prev;
	coop_pump_msg_scope(u32 type) : prev(t_coop_pump_msg) { t_coop_pump_msg = type; }
	~coop_pump_msg_scope() { t_coop_pump_msg = prev; }
};
}

u32 xrServer::OnMessage(NET_Packet& P, ClientID sender) // Non-Zero means broadcasting with "flags" as returned
{
	// MP fork (§3c REPRODUCTION): this function runs on the ENet PUMP thread — see the
	// pump-thread peek's own comment further down this file — and M_XRNET_DIALOG_ACTION already
	// walks from here into the Lua VM. Bait it here rather than at the dialogue case, so the
	// reproduction is about WHERE the VM is entered and not about anything the dialogue does.
	// Off unless -coop_repro_pumplua is passed.
	if (strstr(Core.Params, "-coop_repro_pumplua"))
		coop_repro_bait("pump");

	u16 type;
	P.r_begin(type);
	const coop_pump_msg_scope coop_msg_ctx(u32(type));
#ifdef DEBUG
	VERIFY(verify_entities());
#endif
	xrClientData* CL = ID_to_client(sender);

	switch (type)
	{
	case M_UPDATE:
		{
			Process_update(P, sender); // No broadcast
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_XRNET_DIALOG_ACTION:
		{
			// MP fork (§19 co-op): a client told us which dialogue action its player
			// triggered. The client cannot run it itself — it would change only its own copy
			// of a world it does not own — so it arrives here as (speaker, partner, dialog,
			// phrase) and we run the very same script function against the real objects.
			//
			// MP fork (§3c FIX, dev/INSTABILITY_PLAN.md §3d): but NOT HERE. This function runs
			// on the ENet pump thread, and running the phrase here walks straight into the Lua
			// VM while the game thread is in it too — LuaJIT is single-threaded, and that race
			// is the chronic headless-server instability. Measured, not argued: with the same
			// bait called 3,500 times from this thread the server died in 88 s, while 10,500
			// calls from the game thread survived the full cap (-coop_repro_pumplua /
			// -coop_repro_gamelua).
			//
			// Defer it to the game thread with the mechanism the engine already has for exactly
			// this: AddDelayedPacket queues under DelayedPackestCS, and xrServer::Update ->
			// ProceedDelayedPackets -> OnDelayedMessage drains it on the game thread. Nothing
			// about the action changes — same packet, same handler, same acting scope — only
			// which thread is holding the VM when it runs. It also inherits the queue's existing
			// correctness: a client that disconnects has its queued packets purged.
			AddDelayedPacket(P, sender);
		}
		break;
	case M_SPAWN:
		{
			if (CL->flags.bLocal)
				Process_spawn(P, sender);
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_EVENT:
		{
			Process_event(P, sender);
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_EVENT_PACK:
		{
			NET_Packet tmpP;
			while (!P.r_eof())
			{
				tmpP.B.count = P.r_u8();
				P.r(&tmpP.B.data, tmpP.B.count);

				OnMessage(tmpP, sender);
			};
		}
		break;
	case M_CL_UPDATE:
		{
			xrClientData* CL = ID_to_client(sender);
			if (!CL) break;
			CL->net_Ready = TRUE;
			const u32 cl_update_body = P.r_tell(); // start of [u16 id][u32 ping][net_Export]

			if (!CL->net_PassUpdates)
				break;
			//-------------------------------------------------------------------
			u32 ClientPing = CL->stats.getPing();
			P.w_seek(P.r_tell() + 2, &ClientPing, 4);
			//-------------------------------------------------------------------
			if (SV_Client)
				SendTo(SV_Client->ID, P, net_flags(TRUE, TRUE));
			// MP fork (§14 co-op player-to-player): a dedicated server has no
			// SV_Client to bounce a player's movement through, and nothing else
			// relays it to the OTHER clients — so without this each player sees the
			// others frozen at spawn. Fan this client's update out to every other
			// connected client (SendBroadcast excludes the sender); their
			// M_CL_UPDATE handler applies net_Import to that player's remote actor.
			// Unreliable/sequenced (newest-wins) like the normal object update path.
			if (xr_enet::enabled())
			{
				SendBroadcast(sender, P, net_flags(FALSE, TRUE));

				// MP fork (§15 co-op): the dedicated server keeps only a CSE for each
				// player and never decodes their movement, so CL->owner->o_Position
				// would stay pinned at the spawn point. A-Life attention anchoring (and
				// level saves) need the LIVE position, so peek it out of the update
				// without disturbing the relay. Body layout (CActor::net_Export):
				// [u16 id][u32 ping][f32 health][u32 timestamp][u8 flags][vec3 pos]...
				if (CL->owner)
				{
					const u32 save_cursor = P.r_tell();
					P.r_seek(cl_update_body);
					P.r_u16();               // entity id
					P.r_u32();               // ping (reserved)
					const float hp = P.r_float();
					P.r_u32();               // timestamp
					P.r_u8();                // flags
					Fvector pos;
					P.r_vec3(pos);
					// D2 run 5: name this write. It runs on the ENet PUMP THREAD and assigns straight
					// into a CSE the game thread may be serialising at that very instant, and a
					// client that has not yet been given a body sends zeros — which is exactly the
					// `pos 0,0,0 / hp 0.00` a reclaimed body arrived with. Log the first writes for
					// each client (cheap, then silent) so the sequence is visible against the
					// reclaim's own before/after lines.
					if (_valid(pos))
					{
						if (CL->m_coop_cl_update_count < 3)
							Msg("- XRNET(diag): pump-thread peek writes id %u <- %.1f,%.1f,%.1f "
								"(client 0x%08x, update #%u)", CL->owner->ID, pos.x, pos.y, pos.z,
								sender.value(), CL->m_coop_cl_update_count + 1);
						CL->owner->o_Position = pos;
					}
					// MP fork (§14 step 7 phase 4 D2, run 2): this client is DRIVING its body —
					// the position above came from the player, not from the server's own copy.
					// coop_sample_recoveries() refuses to persist a body that has never got here.
					++CL->m_coop_cl_update_count;

					// MP fork (§14 step 7 phase 4 D3.3 / doc §9.4): remember when this player's
					// health last FELL. The first update only establishes the baseline — a client
					// that connects already wounded has not just been hurt. Only a decrease is a
					// damage event; regeneration and healing walk it back up and are not.
					// Read by the disconnect path, which stamps the age into the orphan record.
					if (_valid(hp))
					{
						if (CL->m_coop_last_health >= 0.f && hp < CL->m_coop_last_health - 0.001f)
							CL->m_coop_last_damage_time = Device.dwTimeGlobal;
						CL->m_coop_last_health = hp;
					}
					P.r_seek(save_cursor);
				}

				if (Device.dwFrame % 120 == 0)
				{
					Msg("- XRNET(diag): SV relayed M_CL_UPDATE from client 0x%08x (clients=%u)",
						sender.value(), GetClientsCount());
					FlushLog();
				}
			}
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_MOVE_PLAYERS_RESPOND:
		{
			xrClientData* CL = ID_to_client(sender);
			if (!CL) break;
			CL->net_Ready = TRUE;
			CL->net_PassUpdates = TRUE;
		}
		break;
		//-------------------------------------------------------------------
	case M_CL_INPUT:
		{
			xrClientData* CL = ID_to_client(sender);
			if (CL) CL->net_Ready = TRUE;
			if (SV_Client) SendTo(SV_Client->ID, P, net_flags(TRUE, TRUE));
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_GAMEMESSAGE:
		{
			SendBroadcast(BroadcastCID, P, net_flags(TRUE,TRUE));
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_CLIENTREADY:
		{
			game->OnPlayerConnectFinished(sender);
			//game->signal_Syncronize	();
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_SWITCH_DISTANCE:
		{
			game->switch_distance(P, sender);
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_CHANGE_LEVEL:
		{
			if (game->change_level(P, sender))
			{
				SendBroadcast(BroadcastCID, P, net_flags(TRUE,TRUE));
			}
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_SAVE_GAME:
		{
			game->save_game(P, sender);
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_LOAD_GAME:
		{
			game->load_game(P, sender);
			SendBroadcast(BroadcastCID, P, net_flags(TRUE,TRUE));
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_RELOAD_GAME:
		{
			SendBroadcast(BroadcastCID, P, net_flags(TRUE,TRUE));
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_SAVE_PACKET:
		{
			Process_save(P, sender);
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case M_CLIENT_REQUEST_CONNECTION_DATA:
		{
			AddDelayedPacket(P, sender);
		}
		break;
	case M_CHAT_MESSAGE:
		{
			xrClientData* l_pC = ID_to_client(sender);
			OnChatMessage(&P, l_pC);
		}
		break;
	case M_SV_MAP_NAME:
		{
			xrClientData* l_pC = ID_to_client(sender);
			OnProcessClientMapData(P, l_pC->ID);
		}
		break;
	case M_SV_DIGEST:
		{
			R_ASSERT(CL);
			ProcessClientDigest(CL, &P);
		}
		break;
	case M_CHANGE_LEVEL_GAME:
		{
			ClientID CID;
			CID.set(0xffffffff);
			SendBroadcast(CID, P, net_flags(TRUE,TRUE));
		}
		break;
	case M_CL_AUTH:
		{
			game->AddDelayedEvent(P, GAME_EVENT_PLAYER_AUTH, 0, sender);
		}
		break;
	case M_CREATE_PLAYER_STATE:
		{
			game->AddDelayedEvent(P, GAME_EVENT_CREATE_PLAYER_STATE, 0, sender);
		}
		break;
	case M_STATISTIC_UPDATE:
		{
			SendBroadcast(BroadcastCID, P, net_flags(TRUE,TRUE));
		}
		break;
	case M_STATISTIC_UPDATE_RESPOND:
		{
			//client method for collecting statistics are called from two places : 1 - this, 2 - game_sv_mp::WritePlayerStats
			if (GameID() != eGameIDSingle)
			{
				game_sv_mp* my_game = static_cast<game_sv_mp*>(game);
				if (CL)
				{
					my_game->m_async_stats.set_responded(CL->ID);
					if (static_cast<IClient*>(CL) != GetServerClient())
					{
						game_PlayerState* tmp_ps = CL->ps;
						u32 tmp_pid = tmp_ps != NULL ? tmp_ps->m_account.profile_id() : 0;
						Game().m_WeaponUsageStatistic->OnUpdateRespond(&P, CL->m_cdkey_digest, tmp_pid);
					}
				}
				else
				{
					Msg("! ERROR: SV: update respond received from unknown sender");
				}
			}
			//if (SV_Client) SendTo	(SV_Client->ID, P, net_flags(TRUE, TRUE));
		}
		break;
	case M_PLAYER_FIRE:
		{
			if (game)
				game->OnPlayerFire(sender, P);
		}
		break;
	case M_REMOTE_CONTROL_AUTH:
		{
			string512 reason;
			shared_str user;
			shared_str pass;
			P.r_stringZ(user);
			if (0 == stricmp(user.c_str(), "logoff"))
			{
				CL->m_admin_rights.m_has_admin_rights = FALSE;
				if (CL->ps)
				{
					CL->ps->resetFlag(GAME_PLAYER_HAS_ADMIN_RIGHTS);
				}
				xr_strcpy(reason, "logged off");
				Msg("# Remote administrator logged off.");
			}
			else
			{
				P.r_stringZ(pass);
				bool res = CheckAdminRights(user, pass, reason);
				if (res)
				{
					CL->m_admin_rights.m_has_admin_rights = TRUE;
					CL->m_admin_rights.m_dwLoginTime = Device.dwTimeGlobal;
					if (CL->ps)
					{
						CL->ps->setFlag(GAME_PLAYER_HAS_ADMIN_RIGHTS);
					}
					Msg("# User [%s] logged as remote administrator.", user.c_str());
				}
				else
					Msg("# User [%s] tried to login as remote administrator. Access denied.", user.c_str());
			}
			NET_Packet P_answ;
			P_answ.w_begin(M_REMOTE_CONTROL_AUTH);
			P_answ.w_stringZ(reason);
			SendTo(CL->ID, P_answ, net_flags(TRUE,TRUE));
		}
		break;

	case M_REMOTE_CONTROL_CMD:
		{
			AddDelayedPacket(P, sender);
		}
		break;
	case M_BATTLEYE:
		{
		}
		break;
	case M_FILE_TRANSFER:
		{
			AddDelayedPacket(P, sender);
		}
		break;
	case M_SECURE_KEY_SYNC:
		{
			PerformSecretKeysSyncAck(CL, P);
		}
		break;
	case M_SECURE_MESSAGE:
		{
			OnSecureMessage(P, CL);
		}
		break;
	}
#ifdef DEBUG
	VERIFY(verify_entities());
#endif
	return IPureServer::OnMessage(P, sender);
}

bool xrServer::CheckAdminRights(const shared_str& user, const shared_str& pass, string512& reason)
{
	bool res = false;
	string_path fn;
	FS.update_path(fn, "$app_data_root$", "radmins.ltx");
	if (FS.exist(fn))
	{
		CInifile ini(fn);
		if (ini.line_exist("radmins", user.c_str()))
		{
			if (ini.r_string("radmins", user.c_str()) == pass)
			{
				xr_strcpy(reason, sizeof(reason), "Access permitted.");
				res = true;
			}
			else
			{
				xr_strcpy(reason, sizeof(reason), "Access denied. Wrong password.");
			}
		}
		else
			xr_strcpy(reason, sizeof(reason), "Access denied. No such user.");
	}
	else
		xr_strcpy(reason, sizeof(reason), "Access denied.");

	return res;
}

void xrServer::SendTo_LL(ClientID ID, void* data, u32 size, u32 dwFlags, u32 dwTimeout)
{
	// MP fork: a -mp_host server keeps psNET_direct_connect TRUE for its
	// in-process host-client, which makes the "optimize local traffic" branch
	// below swallow EVERY client's messages into the local Level().OnMessage —
	// including a REMOTE ENet client's, so the remote client never gets them
	// and times out. A remote client must take the network (framed SendTo_Buf)
	// path instead: the client's MultipacketReciever expects a MultipacketHeader
	// on every packet, so raw sends get dropped; SendTo_Buf frames them and the
	// base IPureServer::SendTo_LL then routes the framed bytes over ENet.
	const bool is_remote = m_enet && m_enet->running() && m_enet->owns(ID.value());
	if (!is_remote && ((SV_Client && SV_Client->ID == ID) || (psNET_direct_connect)))
	{
		// optimize local traffic
		Level().OnMessage(data, size);
	}
	else
	{
		if (!is_remote)
		{
			IClient* pClient = ID_to_client(ID);
			VERIFY2(pClient && pClient->flags.bConnected, "trying to send packet to disconnected client");
			if (!pClient || !pClient->flags.bConnected)
				return;
		}

		IPureServer::SendTo_Buf(ID, data, size, dwFlags, dwTimeout);
	}
}

void xrServer::SendBroadcast(ClientID exclude, NET_Packet& P, u32 dwFlags)
{
	struct ClientExcluderPredicate
	{
		ClientID id_to_exclude;

		ClientExcluderPredicate(ClientID exclude) :
			id_to_exclude(exclude)
		{
		}

		bool operator()(IClient* client)
		{
			xrClientData* tmp_client = static_cast<xrClientData*>(client);
			if (client->ID == id_to_exclude)
				return false;
			if (!client->flags.bConnected)
				return false;
			if (!tmp_client->net_Accepted)
				return false;
			return true;
		}
	};
	struct ClientSenderFunctor
	{
		xrServer* m_owner;
		void* m_data;
		u32 m_size;
		u32 m_dwFlags;

		ClientSenderFunctor(xrServer* owner, void* data, u32 size, u32 dwFlags) :
			m_owner(owner), m_data(data), m_size(size), m_dwFlags(dwFlags)
		{
		}

		void operator()(IClient* client)
		{
			m_owner->SendTo_LL(client->ID, m_data, m_size, m_dwFlags);
		}
	};
	ClientSenderFunctor temp_functor(this, P.B.data, P.B.count, dwFlags);
	net_players.ForFoundClientsDo(ClientExcluderPredicate(exclude), temp_functor);
}

//--------------------------------------------------------------------
CSE_Abstract* xrServer::entity_Create(LPCSTR name)
{
	return F_entity_Create(name);
}

void xrServer::entity_Destroy(CSE_Abstract*& P)
{
#ifdef DEBUG
if( dbg_net_Draw_Flags.test( dbg_destroy ) )
		Msg	("xrServer::entity_Destroy : [%d][%s][%s]",P->ID,P->name(),P->name_replace());
#endif
	R_ASSERT(P);
	entities.erase(P->ID);
	m_tID_Generator.vfFreeID(P->ID, Device.TimerAsync());

	if (P->owner && P->owner->owner == P)
		P->owner->owner = NULL;

	P->owner = NULL;
	if (!ai().get_alife() || !P->m_bALifeControl)
	{
		F_entity_Destroy(P);
	}
}

//--------------------------------------------------------------------
void xrServer::Server_Client_Check(IClient* CL)
{
	if (SV_Client && SV_Client->ID == CL->ID)
	{
		if (!CL->flags.bConnected)
		{
			SV_Client = NULL;
		};
		return;
	};

	if (SV_Client && SV_Client->ID != CL->ID)
	{
		return;
	};


	if (!CL->flags.bConnected)
	{
		return;
	};

	if (CL->process_id == GetCurrentProcessId())
	{
		CL->flags.bLocal = 1;
		SV_Client = (xrClientData*)CL;
		Msg("New SV client 0x%08x", SV_Client->ID.value());
	}
	else
	{
		CL->flags.bLocal = 0;
	}
};

bool xrServer::OnCL_QueryHost()
{
	if (game->Type() == eGameIDSingle) return false;
	return (GetClientsCount() != 0);
};

CSE_Abstract* xrServer::GetEntity(u32 Num)
{
	xrS_entities::iterator I = entities.begin(), E = entities.end();
	for (u32 C = 0; I != E; ++I, ++C)
	{
		if (C == Num) return I->second;
	};
	return NULL;
};


void xrServer::OnChatMessage(NET_Packet* P, xrClientData* CL)
{
	if (!CL->net_Ready)
		return;

	struct MessageSenderController
	{
		xrServer* m_owner;
		s16 m_team;
		game_PlayerState* m_sender_ps;
		NET_Packet* m_packet;

		MessageSenderController(xrServer* owner) :
			m_owner(owner)
		{
		}

		void operator()(IClient* client)
		{
			xrClientData* xr_client = static_cast<xrClientData*>(client);
			game_PlayerState* ps = xr_client->ps;
			if (!ps)
				return;
			if (!xr_client->net_Ready)
				return;
			if (m_team != -1 && ps->team != m_team)
				return;
			if (m_sender_ps->testFlag(GAME_PLAYER_FLAG_VERY_VERY_DEAD) &&
				!ps->testFlag(GAME_PLAYER_FLAG_VERY_VERY_DEAD))
			{
				return;
			}
			m_owner->SendTo(client->ID, *m_packet);
		}
	};
	MessageSenderController mesenger(this);
	mesenger.m_team = P->r_s16();
	mesenger.m_sender_ps = CL->ps;
	mesenger.m_packet = P;
	ForEachClientDoSender(mesenger);
};

#ifdef DEBUG

static	BOOL	_ve_initialized			= FALSE;
static	BOOL	_ve_use					= TRUE;

bool xrServer::verify_entities				() const
{
	if (!_ve_initialized)	{
		_ve_initialized					= TRUE;
		if (strstr(Core.Params,"-~ve"))	_ve_use=FALSE;
	}
	if (!_ve_use)						return true;

	xrS_entities::const_iterator		I = entities.begin();
	xrS_entities::const_iterator		E = entities.end();
	for ( ; I != E; ++I) {
		VERIFY2							((*I).first != 0xffff,"SERVER : Invalid entity id as a map key - 0xffff");
		VERIFY2							((*I).second,"SERVER : Null entity object in the map");
		VERIFY3							((*I).first == (*I).second->ID,"SERVER : ID mismatch - map key doesn't correspond to the real entity ID",(*I).second->name_replace());
		verify_entity					((*I).second);
	}
	return								(true);
}

void xrServer::verify_entity				(const CSE_Abstract *entity) const
{
	VERIFY(entity->m_wVersion!=0);
	if (entity->ID_Parent != 0xffff) {
		xrS_entities::const_iterator	J = entities.find(entity->ID_Parent);
		VERIFY2							(J != entities.end(),
			make_string("SERVER : Cannot find parent in the map [%s][%s]",entity->name_replace(),
			entity->name()).c_str());
		VERIFY3							((*J).second,"SERVER : Null entity object in the map",entity->name_replace());
		VERIFY3							((*J).first == (*J).second->ID,"SERVER : ID mismatch - map key doesn't correspond to the real entity ID",(*J).second->name_replace());
		VERIFY3							(std::find((*J).second->children.begin(),(*J).second->children.end(),entity->ID) != (*J).second->children.end(),"SERVER : Parent/Children relationship mismatch - Object has parent, but corresponding parent doesn't have children",(*J).second->name_replace());
	}

	xr_vector<u16>::const_iterator		I = entity->children.begin();
	xr_vector<u16>::const_iterator		E = entity->children.end();
	for ( ; I != E; ++I) {
		VERIFY3							(*I != 0xffff,"SERVER : Invalid entity children id - 0xffff",entity->name_replace());
		xrS_entities::const_iterator	J = entities.find(*I);
		VERIFY3							(J != entities.end(),"SERVER : Cannot find children in the map",entity->name_replace());
		VERIFY3							((*J).second,"SERVER : Null entity object in the map",entity->name_replace());
		VERIFY3							((*J).first == (*J).second->ID,"SERVER : ID mismatch - map key doesn't correspond to the real entity ID",(*J).second->name_replace());
		VERIFY3							((*J).second->ID_Parent == entity->ID,"SERVER : Parent/Children relationship mismatch - Object has children, but children doesn't have parent",(*J).second->name_replace());
	}
}

#endif // DEBUG

shared_str xrServer::level_name(const shared_str& server_options) const
{
	return (game->level_name(server_options));
}

shared_str xrServer::level_version(const shared_str& server_options) const
{
	return (game_sv_GameState::parse_level_version(server_options));
}

void xrServer::create_direct_client()
{
	SClientConnectData cl_data;
	cl_data.clientID.set(1);
	xr_strcpy(cl_data.name, "single_player");
	cl_data.process_id = GetCurrentProcessId();

	new_client(&cl_data);
}


void xrServer::ProceedDelayedPackets()
{
	// MP fork (§3c): the §3c fix defers the dialogue action into this queue precisely so it runs
	// on the game thread, which makes "who drains the queue" a load-bearing fact rather than an
	// implementation detail. xrServer::Update has four call sites, so assert nothing and MEASURE
	// it: say so, once, if a drain ever happens anywhere else. Said once because a per-frame line
	// would be noise, and said at all because the alternative is finding out from a crash.
	{
		extern u32 g_coop_game_thread_id;
		static bool s_warned = false;
		const u32 tid = GetCurrentThreadId();
		if (!s_warned && g_coop_game_thread_id != 0 && tid != g_coop_game_thread_id)
		{
			s_warned = true;
			Msg("! COOP(delayed): the delayed-packet queue is being drained on thread %u, but the "
				"game thread is %u — anything queued here that touches Lua is back in the §3c race",
				tid, g_coop_game_thread_id);
		}
	}

	// MP fork (§14 step 8 QR-D, harness): -coop_test_disc_hold <ms> holds DIALOGUE ACTIONS in this
	// queue for N ms after they were queued, and nothing else about them changes.
	//
	// WHY A KNOB AT ALL, stated because a synthetic delay in a test needs a reason: the window
	// between "the packet was queued" and "the game thread drained it" is normally ONE FRAME, tens
	// of milliseconds. Every claim about what happens when a client disconnects *inside* that window
	// — including this codebase's own "the queue purges a departed client's packets, so it is fine
	// by construction" — is therefore a claim about a window no test can hit by aiming at it. The
	// HOLD is synthetic; the purge, the disconnect and the claim it exposes are all the real paths.
	//
	// It stops at the FRONT of the queue rather than skipping past held packets, so ordering is
	// preserved exactly as an undelayed drain would see it. The cost is that an administrative
	// packet queued behind a held dialogue action waits too — acceptable in a flag that is off
	// unless a harness asks for it, and stated here rather than discovered.
	static u32 s_hold_ms = 0;
	static bool s_hold_init = false;
	if (!s_hold_init)
	{
		s_hold_init = true;
		LPCSTR h = strstr(Core.Params, "-coop_test_disc_hold");
		if (h)
		{
			h += sizeof("-coop_test_disc_hold") - 1;
			while (*h == ' ') ++h;
			const float ms = (float)atof(h);
			if (ms > 0.f && ms <= 120000.f) s_hold_ms = (u32)ms;
			Msg("- COOP(disc): dialogue actions will be HELD %ums in the delayed queue "
				"(harness knob; the purge and the claim are the real paths)", s_hold_ms);
		}
	}

	DelayedPackestCS.Enter();
	while (!m_aDelayedPackets.empty())
	{
		DelayedPacket& DPacket = *m_aDelayedPackets.begin();
		if (s_hold_ms && DPacket.MsgType == M_XRNET_DIALOG_ACTION &&
		    (Device.dwTimeGlobal - DPacket.QueuedAt) < s_hold_ms)
		{
			static u32 s_said = 0;
			if (s_said < 4)
			{
				++s_said;
				Msg("- COOP(disc): HOLDING a dialogue action from client 0x%08x, queued %ums ago "
					"(hold=%ums) — the queue-to-drain window is open",
					DPacket.SenderID.value(), Device.dwTimeGlobal - DPacket.QueuedAt, s_hold_ms);
			}
			break;                               // leave it at the front; ordering unchanged
		}
		OnDelayedMessage(DPacket.Packet, DPacket.SenderID);
		//		OnMessage(DPacket.Packet, DPacket.SenderID);
		m_aDelayedPackets.pop_front();
	}
	DelayedPackestCS.Leave();
};

void xrServer::AddDelayedPacket(NET_Packet& Packet, ClientID Sender)
{
	DelayedPackestCS.Enter();

	m_aDelayedPackets.push_back(DelayedPacket());
	DelayedPacket* NewPacket = &(m_aDelayedPackets.back());
	NewPacket->SenderID = Sender;
	CopyMemory(&(NewPacket->Packet), &Packet, sizeof(NET_Packet));
	// MP fork (§14 step 8 QR-D): stamp what and when. r_begin is safe to call here precisely
	// because it REWINDS (r_pos = 0, then re-reads the type) — OnDelayedMessage calls it again on
	// the way out, so moving the read cursor now cannot change how the payload parses later.
	{
		u16 t = 0;
		NewPacket->Packet.r_begin(t);
		NewPacket->MsgType  = t;
		NewPacket->QueuedAt = Device.dwTimeGlobal;
	}

	DelayedPackestCS.Leave();
}

u32 g_sv_dwMaxClientPing = 2000;
u32 g_sv_time_for_ping_check = 15000; // 15 sec
u8 g_sv_maxPingWarningsCount = 5;

void xrServer::PerformCheckClientsForMaxPing()
{
	struct MaxPingClientDisconnector
	{
		xrServer* m_owner;

		MaxPingClientDisconnector(xrServer* owner) :
			m_owner(owner)
		{
		}

		void operator()(IClient* client)
		{
			xrClientData* Client = static_cast<xrClientData*>(client);
			game_PlayerState* ps = Client->ps;
			if (!ps)
				return;

			if (client == m_owner->GetServerClient())
				return;

			if (ps->ping > g_sv_dwMaxClientPing &&
				Client->m_ping_warn.m_dwLastMaxPingWarningTime + g_sv_time_for_ping_check < Device.dwTimeGlobal)
			{
				++Client->m_ping_warn.m_maxPingWarnings;
				Client->m_ping_warn.m_dwLastMaxPingWarningTime = Device.dwTimeGlobal;

				if (Client->m_ping_warn.m_maxPingWarnings >= g_sv_maxPingWarningsCount)
				{
					//kick
					LPSTR reason;
					STRCONCAT(reason, CStringTable().translate("st_kicked_by_server").c_str());
					Level().Server->DisconnectClient(Client, reason);
				}
				else
				{
					//send warning
					NET_Packet P;
					P.w_begin(M_CLIENT_WARN);
					P.w_u8(1); // 1 means max-ping-warning
					P.w_u16(ps->ping);
					P.w_u8(Client->m_ping_warn.m_maxPingWarnings);
					P.w_u8(g_sv_maxPingWarningsCount);
					m_owner->SendTo(Client->ID, P, net_flags(FALSE,TRUE));
				}
			}
		}
	};
	MaxPingClientDisconnector temp_functor(this);
	ForEachClientDoSender(temp_functor);
}

extern s32 g_sv_dm_dwFragLimit;
extern s32 g_sv_ah_dwArtefactsNum;
extern s32 g_sv_dm_dwTimeLimit;
extern int g_sv_ah_iReinforcementTime;
extern int g_sv_mp_iDumpStatsPeriod;
extern BOOL g_bCollectStatisticData;

//xr_token game_types[];
LPCSTR GameTypeToString(EGameIDs gt, bool bShort);

void xrServer::GetServerInfo(CServerInfo* si)
{
	string32 tmp;
	string256 tmp256;

	si->AddItem("Server port", itoa(GetPort(), tmp, 10), RGB(128, 128, 255));
	LPCSTR time = InventoryUtilities::GetTimeAsString(Device.dwTimeGlobal, InventoryUtilities::etpTimeToSecondsAndDay).
		c_str();
	si->AddItem("Uptime", time, RGB(255, 228, 0));

	//	xr_strcpy( tmp256, get_token_name(game_types, game->Type() ) );
	xr_strcpy(tmp256, GameTypeToString(game->Type(), true));
	if (game->Type() == eGameIDDeathmatch || game->Type() == eGameIDTeamDeathmatch)
	{
		xr_strcat(tmp256, " [");
		xr_strcat(tmp256, itoa(g_sv_dm_dwFragLimit, tmp, 10));
		xr_strcat(tmp256, "] ");
	}
	else if (game->Type() == eGameIDArtefactHunt || game->Type() == eGameIDCaptureTheArtefact)
	{
		xr_strcat(tmp256, " [");
		xr_strcat(tmp256, itoa(g_sv_ah_dwArtefactsNum, tmp, 10));
		xr_strcat(tmp256, "] ");
		g_sv_ah_iReinforcementTime;
	}

	//if ( g_sv_dm_dwTimeLimit > 0 )
	{
		xr_strcat(tmp256, " time limit [");
		xr_strcat(tmp256, itoa(g_sv_dm_dwTimeLimit, tmp, 10));
		xr_strcat(tmp256, "] ");
	}
	if (game->Type() == eGameIDArtefactHunt || game->Type() == eGameIDCaptureTheArtefact)
	{
		xr_strcat(tmp256, " RT [");
		xr_strcat(tmp256, itoa(g_sv_ah_iReinforcementTime, tmp, 10));
		xr_strcat(tmp256, "]");
	}
	si->AddItem("Game type", tmp256, RGB(128, 255, 255));

	if (g_pGameLevel)
	{
		time = InventoryUtilities::GetGameTimeAsString(InventoryUtilities::etpTimeToMinutes).c_str();

		xr_strcpy(tmp256, time);
		if (g_sv_mp_iDumpStatsPeriod > 0)
		{
			xr_strcat(tmp256, " statistic [");
			xr_strcat(tmp256, itoa(g_sv_mp_iDumpStatsPeriod, tmp, 10));
			xr_strcat(tmp256, "]");
			if (g_bCollectStatisticData)
			{
				xr_strcat(tmp256, "[weapons]");
			}
		}
		si->AddItem("Game time", tmp256, RGB(205, 228, 178));
	}
}

void xrServer::AddCheater(shared_str const& reason, ClientID const& cheaterID)
{
	CheaterToKick new_cheater;
	new_cheater.reason = reason;
	new_cheater.cheater_id = cheaterID;
	m_cheaters.push_back(new_cheater);
}

void xrServer::KickCheaters()
{
	for (cheaters_t::iterator i = m_cheaters.begin(),
	                          ie = m_cheaters.end(); i != ie; ++i)
	{
		IClient* tmp_client = GetClientByID(i->cheater_id);
		if (!tmp_client)
		{
			Msg("! ERROR: KickCheaters: client [%u] not found", i->cheater_id);
			continue;
		}
		ClientID tmp_client_id = tmp_client->ID;
		DisconnectClient(tmp_client, i->reason.c_str());

		NET_Packet P;
		P.w_begin(M_GAMEMESSAGE);
		P.w_u32(GAME_EVENT_SERVER_STRING_MESSAGE);
		P.w_stringZ(i->reason.c_str() + 2);
		Level().Server->SendBroadcast(tmp_client_id, P);
	}
	m_cheaters.clear();
}

void xrServer::MakeScreenshot(ClientID const& admin_id, ClientID const& cheater_id)
{
	if ((cheater_id == SV_Client->ID) && g_dedicated_server)
	{
		return;
	}
	for (int i = 0; i < sizeof(m_screenshot_proxies) / sizeof(clientdata_proxy*); ++i)
	{
		if (!m_screenshot_proxies[i]->is_active())
		{
			m_screenshot_proxies[i]->make_screenshot(admin_id, cheater_id);
			Msg("* admin [%d] is making screeshot of client [%d]", admin_id, cheater_id);
			return;
		}
	}
	Msg("! ERROR: SV: not enough file transfer proxies for downloading screenshot, please try later ...");
}

void xrServer::MakeConfigDump(ClientID const& admin_id, ClientID const& cheater_id)
{
	if ((cheater_id == SV_Client->ID) && g_dedicated_server)
	{
		return;
	}
	for (int i = 0; i < sizeof(m_screenshot_proxies) / sizeof(clientdata_proxy*); ++i)
	{
		if (!m_screenshot_proxies[i]->is_active())
		{
			m_screenshot_proxies[i]->make_config_dump(admin_id, cheater_id);
			Msg("* admin [%d] is making config dump of client [%d]", admin_id, cheater_id);
			return;
		}
	}
	Msg("! ERROR: SV: not enough file transfer proxies for downloading file, please try later ...");
}


void xrServer::initialize_screenshot_proxies()
{
	for (int i = 0; i < sizeof(m_screenshot_proxies) / sizeof(clientdata_proxy*); ++i)
	{
		m_screenshot_proxies[i] = xr_new<clientdata_proxy>(m_file_transfers);
	}
}

void xrServer::deinitialize_screenshot_proxies()
{
	for (int i = 0; i < sizeof(m_screenshot_proxies) / sizeof(clientdata_proxy*); ++i)
	{
		xr_delete(m_screenshot_proxies[i]);
	}
}

struct PlayerInfoWriter
{
	NET_Packet* dest;

	void operator()(IClient* C)
	{
		xrClientData* tmp_client = smart_cast<xrClientData*>(C);
		if (!tmp_client)
			return;

		dest->w_clientID(tmp_client->ID);
		dest->w_stringZ(tmp_client->m_cAddress.to_string().c_str());
		dest->w_stringZ(tmp_client->m_cdkey_digest);
	}
}; //struct PlayerInfoWriter

void xrServer::SendPlayersInfo(ClientID const& to_client)
{
	PlayerInfoWriter tmp_functor;
	NET_Packet tmp_packet;
	tmp_packet.w_begin(M_GAMEMESSAGE);
	tmp_packet.w_u32(GAME_EVENT_PLAYERS_INFO_REPLY);
	tmp_functor.dest = &tmp_packet;
	ForEachClientDo(tmp_functor);
	SendTo(to_client, tmp_packet, net_flags(TRUE, TRUE));
}
