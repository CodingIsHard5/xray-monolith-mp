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
		do
		{
			it = std::find(m_aDelayedPackets.begin(), m_aDelayedPackets.end(), pp);
			if (it != m_aDelayedPackets.end())
			{
				m_aDelayedPackets.erase(it);
				Msg("removing packet from delayed event storage");
			}
			else
				break;
		}
		while (true);

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

void xrServer::MakeUpdatePackets()
{
	NET_Packet tmpPacket;
	u32 position;

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
		// §4 step 3 (increment C2): the throttle now applies ONLY to creatures the decision system
		// currently owns (a live, non-expired m_coop_decision_driven entry) — these run their AI on
		// the client and need only a sparse soft-correction. Normal puppets are NOT in the set and
		// keep dense streaming (no regression). (The step-3 cadence PROBE threw the throttle at all
		// creatures; that measurement is done — see DECISION_REPLICATION_PLAN.md — so it is now
		// membership-gated per the inversion plan.)
		// throttle bookkeeping deferred to AFTER the write below (CodeRabbit): the timestamp must be
		// stamped only once a NON-EMPTY update is actually queued, else an ObjectSize==0 pass (packet
		// dropped) would still consume a throttle window and suppress the next real correction.
		static xr_map<u16, u32> s_c2_last_sent; // per driven creature: last throttled send time
		bool c2_driven_send = false;
		u32  c2_gap = 0;
		if (s_throttle_ms > 0 && xr_enet::enabled() && Test.cast_creature_abstract())
		{
			const u32 now = Device.dwTimeGlobal;
			auto dd = m_coop_decision_driven.find(Test.ID);
			const bool driven = (dd != m_coop_decision_driven.end())
				&& ((now - dd->second) < COOP_DECISION_DRIVEN_TTL_MS);
			if (driven)
			{
				auto it = s_c2_last_sent.find(Test.ID);
				if (it != s_c2_last_sent.end() && (now - it->second) < u32(s_throttle_ms))
					continue; // within the throttle window — skip this driven creature's update
				c2_driven_send = true;
				c2_gap = (it != s_c2_last_sent.end()) ? (now - it->second) : 0;
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
			if (c2_driven_send)
			{
				s_c2_last_sent[Test.ID] = Device.dwTimeGlobal;
				if (strstr(Core.Params, "-dbg"))
					Msg("* COOP_C2_THROTTLE: id=%u SENT gap=%ums (throttle=%dms, driven)",
					    Test.ID, c2_gap, s_throttle_ms);
			}
			m_updator.write_update_for(Test.ID, tmpPacket);
		}
	} //all entities

	// §world-NPC-replication Phase 1: flush the per-pass creature tally at most once per ~3s.
	if (s_npcdiag == 1)
	{
		static u32 s_nd_last = 0;
		if ((Device.dwTimeGlobal - s_nd_last) >= 3000)
		{
			s_nd_last = Device.dwTimeGlobal;
			Msg("~ COOP_NPCDIAG: creatures=%d eligible=%d | skip: owner0=%d notready=%d phantom=%d notrel=%d (clients=%u)",
				nd_creatures, nd_eligible, nd_owner0, nd_notready, nd_phantom, nd_notrel, GetClientsCount());
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
		MakeUpdatePackets();
		SendUpdatePacketsToAll();

#ifdef DEBUG
		g_sv_SendUpdate = 0;
#endif
		if (game->sv_force_sync) Perform_game_export();
#ifdef DEBUG
		VERIFY(verify_entities());
#endif
		m_last_update_time = Device.dwTimeGlobal;
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

	if (!g_pGameLevel || !dialog_id[0] || !phrase_id[0])
		return;

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

	// MP fork (§19 co-op): mark which player is talking, so any task GIVEN by this action is
	// tagged to them (CGameTaskManager::GiveGameTaskToActor reads it). Cleared right after -
	// tasks given outside a dialogue must stay owner 0 (global).
	extern u16 g_coop_dialog_actor;
	g_coop_dialog_actor = speaker_id;

	// Same call the single-player path makes, just with the server's own objects.
	phrase->GetScriptHelper()->Action(speaker, partner, dialog_id, phrase_id);

	g_coop_dialog_actor = 0;
}

u32 xrServer::OnMessage(NET_Packet& P, ClientID sender) // Non-Zero means broadcasting with "flags" as returned
{
	u16 type;
	P.r_begin(type);
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
			coop_run_dialog_action(P);
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
					P.r_float();             // health
					P.r_u32();               // timestamp
					P.r_u8();                // flags
					Fvector pos;
					P.r_vec3(pos);
					if (_valid(pos))
						CL->owner->o_Position = pos;
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
	DelayedPackestCS.Enter();
	while (!m_aDelayedPackets.empty())
	{
		DelayedPacket& DPacket = *m_aDelayedPackets.begin();
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
