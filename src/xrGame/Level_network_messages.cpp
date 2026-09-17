#include "stdafx.h"
#include "../xrEngine/xr_object.h"             // MP fork (§9 death delivery/revive): CObject is used complete here
// MP fork (§19 co-op): M_XRNET_OPEN_MENU
#include "UIGameSP.h"
#include "InventoryOwner.h"
#include "trade.h"                             // MP fork (§10.3 S2a.1): CTrade::coop_refund_refused
#include "ai/monsters/bloodsucker/bloodsucker.h"   // MP fork (§3.4): server-authored cloak
#include "GametaskManager.h"                  // MP fork (§19 co-op): M_XRNET_TASKS
#include "entity.h"
#include "xrserver_objects.h"
#include "level.h"
#include "xrmessages.h"
#include "game_cl_base.h"
#include "net_queue.h"
//#include "Physics.h"
#include "xrServer.h"
#include "../xrNetServer/xr_enet_transport.h"      // MP fork: xr_enet::enabled()
#include "Actor.h"
#include "Artefact.h"
#include "game_cl_base_weapon_usage_statistic.h"
#include "ai_space.h"
#include "script_engine.h"                    // MP fork (§14 step 7 P4 D3.2): mp_coop_on_notice
#include "saved_game_wrapper.h"
#include "level_graph.h"
#include "file_transfer.h"
#include "message_filter.h"
#include "../xrphysics/iphworld.h"

extern LPCSTR map_ver_string;

LPSTR remove_version_option(LPCSTR opt_str, LPSTR new_opt_str, u32 new_opt_str_size)
{
	LPCSTR temp_substr = strstr(opt_str, map_ver_string);
	if (!temp_substr)
	{
		xr_strcpy(new_opt_str, new_opt_str_size, opt_str);
		return new_opt_str;
	}
	strncpy_s(new_opt_str, new_opt_str_size, opt_str, static_cast<size_t>(temp_substr - opt_str - 1));
	temp_substr = strchr(temp_substr, '/');
	if (!temp_substr)
		return new_opt_str;

	xr_strcat(new_opt_str, new_opt_str_size, temp_substr);
	return new_opt_str;
}

#ifdef DEBUG
s32 lag_simmulator_min_ping	= 0;
s32 lag_simmulator_max_ping	= 0;
static bool SimmulateNetworkLag()
{
	static u32 max_lag_time	= 0;

	if (!lag_simmulator_max_ping && !lag_simmulator_min_ping)
		return false;
	
	if (!max_lag_time || (max_lag_time <= Device.dwTimeGlobal))
	{
		CRandom				tmp_random(Device.dwTimeGlobal);
		max_lag_time		= Device.dwTimeGlobal + tmp_random.randI(lag_simmulator_min_ping, lag_simmulator_max_ping);
		return false;
	}
	return true;
}
#endif

void CLevel::ClientReceive()
{
	m_dwRPC = 0;
	m_dwRPS = 0;

	if (IsDemoPlayStarted())
	{
		SimulateServerUpdate();
	}
#ifdef DEBUG
	if (SimmulateNetworkLag())
		return;
#endif
	StartProcessQueue();
	for (NET_Packet* P = net_msg_Retreive(); P; P = net_msg_Retreive())
	{
		if (IsDemoSaveStarted())
		{
			SavePacket(*P);
		}
		//-----------------------------------------------------
		m_dwRPC++;
		m_dwRPS += P->B.count;
		//-----------------------------------------------------
		u16 m_type;
		u16 ID;
		P->r_begin(m_type);
		switch (m_type)
		{
		case M_SPAWN:
			{
				if (!bReady) //!m_bGameConfigStarted || 
				{
					Msg("! Unconventional M_SPAWN received : map_data[%s] | bReady[%s] | deny_m_spawn[%s]",
					    (map_data.m_map_sync_received) ? "true" : "false",
					    (bReady) ? "true" : "false",
					    deny_m_spawn ? "true" : "false");
					break;
				}
				/*/
				cl_Process_Spawn(*P);
				/*/
				//Msg("--- Client received M_SPAWN message...");
                xrSRWLockGuard g(prefetch_lock);
				game_events->insert(*P);
				if (g_bDebugEvents) ProcessGameEvents();
				//*/
			}
			break;
		case M_EVENT:
            {
                xrSRWLockGuard g(prefetch_lock);
                game_events->insert(*P);
                if (g_bDebugEvents) ProcessGameEvents();
            }
            break;
		case M_EVENT_PACK:
			{
				/*if (!game_configured)
				{
					Msg("! WARNING: ignoring game event [%d] - game not configured...", m_type);
					break;
				}*/
				NET_Packet tmpP;
                xrSRWLockGuard g(prefetch_lock);
				while (!P->r_eof())
				{
					tmpP.B.count = P->r_u8();
					P->r(&tmpP.B.data, tmpP.B.count);
					tmpP.timeReceive = P->timeReceive;

					game_events->insert(tmpP);
					if (g_bDebugEvents) ProcessGameEvents();
				};
			}
			break;
		case M_UPDATE:
			{
				game->net_import_update(*P);
			}
			break;
		case M_XRNET_TASKS:
			{
				// MP fork (§19 co-op): the server's shared quest list. See
				// CGameTaskManager::coop_broadcast_tasks.
				GameTaskManager().coop_apply_tasks(*P);
			}
			break;
		case M_XRNET_DECISION:
			{
				// MP fork (§3/§4 decision replication): a scheduled server decision. Queue it;
				// CLevel::coop_dispatch_due_decisions fires it when our clock reaches exec_tick.
				coop_recv_decision(*P);
			}
			break;
		case M_XRNET_COOP_RESPAWN:
			{
				// MP fork (§14 step 7 phase 3 C3 / §9.1): the server rolled this death back to
				// the player's checkpoint and is telling us where that is. Sent to ONE client,
				// but check the actor id anyway — we only move the body we control.
				const u16 actor_id = P->r_u16();
				Fvector pos;
				P->r_vec3(pos);
				const float health = P->r_float();

				CObject* const controlled = CurrentControlEntity();
				CActor* const actor = controlled ? smart_cast<CActor*>(controlled) : NULL;
				if (actor && controlled->ID() == actor_id)
					actor->coop_set_respawn_position(pos, health);
			}
			break;
		case M_XRNET_COOP_DEATH:
			{
				// MP fork (§9 death delivery, 2026-09-17): the server says this player's body is dead and is making sure
				// every side knows. Idempotent by AlreadyDie(): the common case is that GE_DIE already arrived and this
				// changes nothing, which is why it says so as a value rather than treating it as an error.
				const u16 actor_id = P->r_u16();
				const u16 killer_id = P->r_u16();

				CObject* const obj = Objects.net_Find(actor_id);
				CActor* const actor = obj ? smart_cast<CActor*>(obj) : NULL;
				if (!actor)
				{
					Msg("! COOP(death-cl): death for body %u arrived but this client has no such actor object", u32(actor_id));
					break;
				}
				const bool mine = (CurrentControlEntity() && CurrentControlEntity()->ID() == actor_id);
				if (!actor->g_Alive() || actor->AlreadyDie())
				{
					Msg("- COOP(death-cl): death received for body %u (%s) — already dead here, nothing to do",
						u32(actor_id), mine ? "our own body" : "a peer's body");
					break;
				}
				Msg("- COOP(death-cl): death received for body %u (%s) killer %u — this client still had it ALIVE; killing it "
					"now (without this the server's death never reaches here)", u32(actor_id), mine ? "our own body" : "a peer's body",
					u32(killer_id));
				actor->KillEntity(killer_id);
			}
			break;
		case M_XRNET_COOP_REVIVE:
			{
				// MP fork (§9 revive, 2026-09-17): a player body somewhere has been revived, and THIS process's copy
				// of it still carries the death it performed on GE_DIE. Broadcast, so it reaches the owner too — the
				// owner has already revived itself and coop_revive is idempotent, which is why it says so as a value
				// rather than treating the second call as an error.
				const u16 actor_id = P->r_u16();
				Fvector pos;
				P->r_vec3(pos);
				const float health = P->r_float();

				CObject* const obj = Objects.net_Find(actor_id);
				CActor* const actor = obj ? smart_cast<CActor*>(obj) : NULL;
				if (!actor)
				{
					Msg("! COOP(revive-cl): revive for body %u arrived but this client has no such actor object", u32(actor_id));
					break;
				}
				const bool mine = (CurrentControlEntity() && CurrentControlEntity()->ID() == actor_id);
				Msg("- COOP(revive-cl): revive received for body %u (%s) health %.2f at (%.1f,%.1f,%.1f)",
					u32(actor_id), mine ? "our own body" : "a peer's body", health, VPUSH(pos));
				// A peer's body is driven by net_Import, so move it; our own has already placed itself.
				actor->coop_revive_body(pos, health, !mine, mine ? "broadcast, our own body" : "broadcast, peer body");
			}
			break;
		case M_XRNET_COOP_NOTICE:
			{
				// MP fork (§14 step 7 phase 4 D3.2 / doc §9.4): one line about the session we
				// just resumed into, sent to THIS client only. Logged unconditionally — the
				// point of the notice is that it is visible without -dbg, in a log a player
				// can be asked for — and then handed to gamedata through the mp_api seam for
				// actual display. No handler registered is not an error: the log line is the
				// floor, gamedata's UI is the ceiling.
				const u8 code = P->r_u8();
				string512 text;
				P->r_stringZ_s(text);
				Msg("* COOP(notice)[%u]: %s", u32(code), text);

				luabind::functor<void> f;
				if (ai().script_engine().functor("_G.mp_coop_on_notice", f))
					f(u32(code), text);
			}
			break;
		case M_XRNET_COOP_ROSTER:
			{
				// MP fork (design doc §13.1): the connected players by name. Handed to gamedata's mp_api
				// (_G.mp_coop_on_roster); no handler registered is not an error on a stock client.
				string4096 text;
				P->r_stringZ_s(text);
				static u32 s_logged = 0;
				if (++s_logged <= 3)
					Msg("* COOP(roster): %s", text);
				luabind::functor<void> f;
				if (ai().script_engine().functor("_G.mp_coop_on_roster", f))
					f(text);
			}
			break;
		case M_XRNET_COOP_STATE:
			{
				// MP fork (design doc §3.4): server-authored mutant state; a client never computes it itself
				if (P->B.count - P->r_tell() >= sizeof(u16) + 2 * sizeof(u8) && !ai().get_alife())
				{
					const u16 id = P->r_u16();
					const u8 kind = P->r_u8();
					const u8 value = P->r_u8();
					if (kind == 1)
						if (CAI_Bloodsucker* const b = smart_cast<CAI_Bloodsucker*>(Objects.net_Find(id)))
							b->coop_apply_server_visibility(value);
				}
			}
			break;
		case M_XRNET_COOP_TRADE_QUOTES:
			{
				if (P->B.count - P->r_tell() >= sizeof(u16) + sizeof(u8) + sizeof(u16))
				{
					const u16 trader = P->r_u16();
					const u8 first = P->r_u8();
					const u16 n = P->r_u16();
					CTrade::coop_quotes_store(trader, first != 0, *P, n);
				}
			}
			break;
		case M_XRNET_COOP_CONSENT_ASK:
			{
				// MP fork (design doc §10.3 item 4): another player asks for an item this player holds. Read bounded, logged,
				// handed to gamedata (_G.mp_coop_on_consent_ask), which decides how to ask the player. No answer is a no.
				if (P->B.count - P->r_tell() < sizeof(u32) + 2 * sizeof(u16))
					break;
				const u32 request = P->r_u32();
				const u16 item = P->r_u16();
				const u16 taker = P->r_u16();
				string128 name, sec;
				char* const bufs[2] = {name, sec};
				for (int k = 0; k < 2; ++k)
				{
					u32 i = 0;
					while (P->r_pos < P->B.count)
					{
						const char c = char(P->B.data[P->r_pos++]);
						if (!c)
							break;
						if (i + 1 < sizeof(name))
							bufs[k][i++] = c;
					}
					bufs[k][i] = 0;
				}
				const u32 secs = (P->B.count - P->r_tell() >= sizeof(u16)) ? u32(P->r_u16()) : 0;
				Msg("* COOP(consent): '%s' (%u) asks for item %u [%s] — request %u, %u s to answer", name, u32(taker), u32(item), sec,
					request, secs);
				luabind::functor<void> f;
				if (ai().script_engine().functor("_G.mp_coop_on_consent_ask", f))
					f(request, u32(item), u32(taker), name, sec, secs);
			}
			break;
		case M_XRNET_COOP_MONEY:
			{
				// MP fork (design doc §10.3 S2b): the server's ledger says this owner has this much. Display only.
				if (P->B.count - P->r_tell() >= sizeof(u16) + sizeof(u32))
				{
					const u16 id = P->r_u16();
					const u32 amount = P->r_u32();
					CInventoryOwner* const o = smart_cast<CInventoryOwner*>(Objects.net_Find(id));
					static u32 s_logged = 0;
					if (++s_logged <= 500 || (s_logged % 100) == 1)
						Msg("* COOP(money): %u = %u%s", u32(id), amount, o ? "" : " (not here)");
					if (o)
						o->set_money(amount, false);
				}
			}
			break;
		case M_XRNET_COOP_HEALTH:
			{
				// MP fork (design doc §3.4 player health, redesign (A)): the server body's health for OUR actor. Applied only to the
				// controlled actor, only while it is alive, and only above zero — a death arrives as GE_DIE, as before.
				if (P->B.count - P->r_tell() >= sizeof(u16) + sizeof(float) + sizeof(u32))
				{
					const u16 id = P->r_u16();
					const float h = P->r_float();
					const u32 sent_st = P->r_u32();
					CActor* const a = smart_cast<CActor*>(CurrentEntity());
					const bool apply = a && a->ID() == id && a->g_Alive() && _valid(h) && h > 0.f;
					const float before = a ? a->GetfHealth() : -2.f;
					if (apply && _abs(before - h) > 0.0005f)
					{
						a->SetfHealth(h);
						static u32 s_n = 0;
						if (++s_n <= 2000 || (s_n % 100) == 1)
							Msg("~ COOP(hpsync-cl): actor %u t %u st %u health %.4f (was %.4f) sent st %u", u32(id), Device.dwTimeGlobal,
								timeServer(), h, before, sent_st);
					}
					else if (!apply)
					{
						static u32 s_skip = 0;
						if (++s_skip <= 50 || (s_skip % 200) == 1)
							Msg("~ COOP(hpsync-cl): skipped id %u health %.4f (controlled %d, alive %d)", u32(id), h,
								a ? int(a->ID()) : -1, a ? int(a->g_Alive()) : -1);
					}
				}
			}
			break;
		case M_XRNET_COOP_TRADE_REFUSED:
			{
				// MP fork (design doc §10.3 S2a.1): give back the money of a purchase the server refused.
				if (P->B.count - P->r_tell() >= sizeof(u16))
					CTrade::coop_refund_refused(P->r_u16());
			}
			break;
		case M_XRNET_COOP_CHAT:
			{
				// MP fork (design doc §13.4): one PDA zone-chat line, already sanitised by the server. Read bounded
				// anyway (r_stringZ_s R_ASSERTs on an over-long string), logged through %s, handed to gamedata.
				const u16 sender = P->r_u16();
				string128 name;
				string512 text;
				char* const bufs[2] = {name, text};
				const u32 caps[2] = {sizeof(name), sizeof(text)};
				for (int k = 0; k < 2; ++k)
				{
					u32 i = 0;
					while (P->r_pos < P->B.count)
					{
						const char c = char(P->B.data[P->r_pos++]);
						if (!c)
							break;
						if (i + 1 < caps[k])
							bufs[k][i++] = c;
					}
					bufs[k][i] = 0;
				}
				static u32 s_logged = 0;
				if (++s_logged <= 500 || (s_logged % 100) == 1)
					Msg("* COOP(chat): %s: %s", name, text);
				luabind::functor<void> f;
				if (ai().script_engine().functor("_G.mp_coop_on_chat", f))
					f(name, text, u32(sender));
			}
			break;
		case M_XRNET_OPEN_MENU:
			{
				// MP fork (§19 co-op): the server ran a dialogue action that wanted to open a
				// menu for a player. It has no UI, so it asked us. Broadcast, so check the
				// actor named is the one WE control before opening anything.
				const u8 menu = P->r_u8();
				const u16 actor_id = P->r_u16();

				CObject* const controlled = CurrentControlEntity();
				CUIGameSP* const ui = smart_cast<CUIGameSP*>(CurrentGameUI());
				if (!ui || !controlled || (controlled->ID() != actor_id))
					break;

				CInventoryOwner* const us = smart_cast<CInventoryOwner*>(controlled);
				CInventoryOwner* const partner = us ? us->GetTalkPartner() : NULL;
				if (!us || !partner)
					break;

				if (menu == 0)
					ui->StartTrade(us, partner);
				else
					ui->StartUpgrade(us, partner);
			}
			break;
		case M_UPDATE_OBJECTS:
			{
				Objects.net_Import(P);

				if (OnClient()) UpdateDeltaUpd(timeServer());
				IClientStatistic pStat = Level().GetStatistic();
				u32 dTime = 0;

				if ((Level().timeServer() + pStat.getPing()) < P->timeReceive)
				{
					dTime = pStat.getPing();
				}
				else
					dTime = Level().timeServer() - P->timeReceive + pStat.getPing();

				u32 NumSteps = physics_world()->CalcNumSteps(dTime);
				SetNumCrSteps(NumSteps);
			}
			break;
		case M_COMPRESSED_UPDATE_OBJECTS:
			{
				u8 compression_type = P->r_u8();
				ProcessCompressedUpdate(*P, compression_type);
			}
			break;
		case M_CL_UPDATE:
			{
				/*if (!game_configured)
				{
					Msg("! WARNING: ignoring game event [%d] - game not configured...", m_type);
					break;
				}*/
				// MP fork (§14 co-op player-to-player): stock clients never receive
				// M_CL_UPDATE (only the listen-server host does, to apply peer
				// updates). On a co-op dedicated setup the server relays each
				// player's update to the OTHER clients (xrServer.cpp), so a co-op
				// client MUST run this handler to see peers move. The sender is
				// excluded server-side, and our own actor is Local() (net_Import
				// early-returns for it), so this only moves remote peers.
				if (OnClient() && !xr_enet::enabled()) break;
				P->r_u16(ID);
				u32 Ping = P->r_u32();
				CGameObject* O = smart_cast<CGameObject*>(Objects.net_Find(ID));
				if (0 == O)
				{
					if (xr_enet::enabled() && (Device.dwFrame % 120 == 0))
					{
						Msg("- XRNET(diag): peer M_CL_UPDATE id=%u NOT FOUND on this client", ID);
						FlushLog();
					}
					break;
				}
				if (xr_enet::enabled() && (Device.dwFrame % 120 == 0))
				{
					Msg("- XRNET(diag): APPLYING peer M_CL_UPDATE id=%u (%s)", ID, O->cName().c_str());
					FlushLog();
				}
				O->net_Import(*P);
				//---------------------------------------------------
				UpdateDeltaUpd(timeServer());
				if (pObjects4CrPr.empty() && pActors4CrPr.empty())
					break;
				if (!smart_cast<CActor*>(O))
					break;

				u32 dTime = 0;
				if ((Level().timeServer() + Ping) < P->timeReceive)
				{
#ifdef DEBUG
					//					Msg("! TimeServer[%d] < TimeReceive[%d]", Level().timeServer(), P->timeReceive);
#endif
					dTime = Ping;
				}
				else
					dTime = Level().timeServer() - P->timeReceive + Ping;
				u32 NumSteps = physics_world()->CalcNumSteps(dTime);
				SetNumCrSteps(NumSteps);

				O->CrPr_SetActivationStep(u32(physics_world()->StepsNum()) - NumSteps);
				AddActor_To_Actors4CrPr(O);
			}
			break;
		case M_MOVE_PLAYERS:
			{
				/*if (!game_configured)
				{
					Msg("! WARNING: ignoring game event [%d] - game not configured...", m_type);
					break;
				}*/
                xrSRWLockGuard g(prefetch_lock);
				game_events->insert(*P);
				if (g_bDebugEvents) ProcessGameEvents();
			}
			break;
			// [08.11.07] Alexander Maniluk: added new message handler for moving artefacts.
		case M_MOVE_ARTEFACTS:
			{
				/*if (!game_configured)
				{
					Msg("! WARNING: ignoring game event [%d] - game not configured...", m_type);
					break;
				}*/
				u8 Count = P->r_u8();
				for (u8 i = 0; i < Count; ++i)
				{
					u16 ID = P->r_u16();
					Fvector NewPos;
					P->r_vec3(NewPos);
					CArtefact* OArtefact = smart_cast<CArtefact*>(Objects.net_Find(ID));
					if (!OArtefact) break;
					OArtefact->MoveTo(NewPos);
					//destroy_physics_shell(OArtefact->PPhysicsShell());
				};
				/*NET_Packet PRespond;
				PRespond.w_begin(M_MOVE_ARTEFACTS_RESPOND);
				Send(PRespond, net_flags(TRUE, TRUE));*/
			}
			break;
			//------------------------------------------------
		case M_CL_INPUT:
			{
				/*if (!game_configured)
				{
					Msg("! WARNING: ignoring game event [%d] - game not configured...", m_type);
					break;
				}*/
				P->r_u16(ID);
				CObject* O = Objects.net_Find(ID);
				if (0 == O) break;
				O->net_ImportInput(*P);
			}
			break;
			//---------------------------------------------------
		case M_SV_CONFIG_NEW_CLIENT:
			InitializeClientGame(*P);
			break;
		case M_SV_CONFIG_GAME:
			game->net_import_state(*P);
			break;
		case M_SV_CONFIG_FINISHED:
			{
				game_configured = TRUE;
#ifdef DEBUG
				Msg("- Game configuring : Finished ");
#endif // #ifdef DEBUG
				if (IsDemoPlayStarted() && !m_current_spectator)
				{
					SpawnDemoSpectator();
				}
			}
			break;
		case M_MIGRATE_DEACTIVATE: // TO:   Changing server, just deactivate
			{
				NODEFAULT;
			}
			break;
		case M_MIGRATE_ACTIVATE: // TO:   Changing server, full state
			{
				NODEFAULT;
			}
			break;
		case M_CHAT:
			{
				/*if (!game_configured)
				{
					Msg("! WARNING: ignoring game event [%d] - game not configured...", m_type);
					break;
				}*/
				char buffer[256];
				P->r_stringZ(buffer);
				Msg("- %s", buffer);
			}
			break;
		case M_GAMEMESSAGE:
			{
				/*if (!game_configured)
				{
					Msg("! WARNING: ignoring game event [%d] - game not configured...", m_type);
					break;
				}*/
				if (!game) break;
                xrSRWLockGuard g(prefetch_lock);
				game_events->insert(*P);
				if (g_bDebugEvents) ProcessGameEvents();
			}
			break;
		case M_RELOAD_GAME:
		case M_LOAD_GAME:
		case M_CHANGE_LEVEL:
			{
#ifdef DEBUG
				Msg("--- Changing level message received...");
#endif // #ifdef DEBUG
                Msg("Device.LuaGC clear");
                Device.LuaGC.clear();
                Device.LuaGCDebug.clear();

				if (m_type == M_LOAD_GAME)
				{
					string256 saved_name;
					P->r_stringZ_s(saved_name);
					if (xr_strlen(saved_name) && ai().get_alife())
					{
						CSavedGameWrapper wrapper(saved_name);
						if (wrapper.level_id() == ai().level_graph().level_id())
						{
							Engine.Event.Defer("Game:QuickLoad", size_t(xr_strdup(saved_name)), 0);

							break;
						}
					}
				}
				MakeReconnect();
			}
			break;
		case M_SAVE_GAME:
			{
				ClientSave();
			}
			break;
		case M_GAMESPY_CDKEY_VALIDATION_CHALLENGE:
			{
				OnGameSpyChallenge(P);
			}
			break;
		case M_AUTH_CHALLENGE:
			{
				ClientSendProfileData();
				OnBuildVersionChallenge();
			}
			break;
		case M_CLIENT_CONNECT_RESULT:
			{
				OnConnectResult(P);
			}
			break;
		case M_CHAT_MESSAGE:
			{
				/*if (!game_configured)
				{
					Msg("! WARNING: ignoring game event [%d] - game not configured...", m_type);
					break;
				}*/
				if (!game) break;
				Game().OnChatMessage(P);
			}
			break;
		case M_CLIENT_WARN:
			{
				if (!game) break;
				Game().OnWarnMessage(P);
			}
			break;
		case M_REMOTE_CONTROL_AUTH:
		case M_REMOTE_CONTROL_CMD:
			{
				Game().OnRadminMessage(m_type, P);
			}
			break;
		case M_SV_MAP_NAME:
			{
				map_data.ReceiveServerMapSync(*P);
			}
			break;
		case M_SV_DIGEST:
			{
				SendClientDigestToServer();
			}
			break;
		case M_CHANGE_LEVEL_GAME:
			{
				Msg("- M_CHANGE_LEVEL_GAME Received");

				if (OnClient())
				{
					MakeReconnect();
				}
				else
				{
					const char* m_SO = m_caServerOptions.c_str();
					//					const char* m_CO = m_caClientOptions.c_str();

					m_SO = strchr(m_SO, '/');
					if (m_SO) m_SO++;
					m_SO = strchr(m_SO, '/');

					shared_str LevelName;
					shared_str LevelVersion;
					shared_str GameType;

					P->r_stringZ(LevelName);
					P->r_stringZ(LevelVersion);
					P->r_stringZ(GameType);

					/*
					u32 str_start = P->r_tell();
					P->skip_stringZ();
					u32 str_end = P->r_tell();

					u32 temp_str_size = str_end - str_start;
					R_ASSERT2(temp_str_size < 256, "level name too big");
					LevelName = static_cast<char*>(_alloca(temp_str_size + 1));
					P->r_seek(str_start);
					P->r_stringZ(LevelName);

										
					str_start = P->r_tell();
					P->skip_stringZ();
					str_end = P->r_tell();
					temp_str_size = str_end - str_start;
					R_ASSERT2(temp_str_size < 256, "incorect game type");
					GameType = static_cast<char*>(_alloca(temp_str_size + 1));
					P->r_seek(str_start);
					P->r_stringZ(GameType);*/

					string4096 NewServerOptions = "";
					xr_sprintf(NewServerOptions, "%s/%s/%s%s",
					           LevelName.c_str(),
					           GameType.c_str(),
					           map_ver_string,
					           LevelVersion.c_str()
					);

					if (m_SO)
					{
						string4096 additional_options;
						xr_strcat(NewServerOptions, sizeof(NewServerOptions),
						          remove_version_option(m_SO, additional_options, sizeof(additional_options))
						);
					}
					m_caServerOptions = NewServerOptions;
					MakeReconnect();
				};
			}
			break;
		case M_CHANGE_SELF_NAME:
			{
				net_OnChangeSelfName(P);
			}
			break;
		case M_BULLET_CHECK_RESPOND:
			{
				if (!game) break;
				if (GameID() != eGameIDSingle)
					Game().m_WeaponUsageStatistic->On_Check_Respond(P);
			}
			break;
		case M_STATISTIC_UPDATE:
			{
				Msg("--- CL: On Update Request");
				if (!game) break;
                xrSRWLockGuard g(prefetch_lock);
				game_events->insert(*P);
				if (g_bDebugEvents) ProcessGameEvents();
			}
			break;
		case M_STATISTIC_UPDATE_RESPOND: //deprecated, see  xrServer::OnMessage
			{
				/*Msg("--- CL: On Update Respond");
				if (!game) break;
				if (GameID() != eGameIDSingle)
					Game().m_WeaponUsageStatistic->OnUpdateRespond(P);*/
			}
			break;
		case M_FILE_TRANSFER:
			{
                xrSRWLockGuard g(prefetch_lock);
				game_events->insert(*P);
				if (g_bDebugEvents) ProcessGameEvents();
			}
			break;
		case M_SECURE_KEY_SYNC:
			{
				OnSecureKeySync(*P);
			}
			break;
		case M_SECURE_MESSAGE:
			{
				OnSecureMessage(*P);
			}
			break;
		}

		net_msg_Release();
	}
	EndProcessQueue();

	if (g_bDebugEvents) ProcessGameSpawns();
}

void CLevel::OnMessage(void* data, u32 size)
{
	IPureClient::OnMessage(data, size);
};
