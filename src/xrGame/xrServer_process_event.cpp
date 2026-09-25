#include "stdafx.h"
#include "coop_ghost.h"   // MP fork (§3.4 object-0 scope)
#include "xrServer.h"
#include "game_sv_single.h"
#include "alife_simulator.h"
#include "xrserver_objects.h"
#include "game_base.h"
#include "game_cl_base.h"
#include "ai_space.h"
#include "alife_object_registry.h"
#include "xrServer_Objects_ALife_Items.h"
#include "mp_coop_ff.h"
#include "Level.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "../xrNetServer/xr_enet_transport.h"   // MP fork (§20): xr_enet::enabled()

void xrServer::Process_event(NET_Packet& P, ClientID sender)
{
#	ifdef SLOW_VERIFY_ENTITIES
			VERIFY					(verify_entities());
#	endif

	u32 timestamp;
	u16 type;
	u16 destination;
	u32 MODE = net_flags(TRUE,TRUE);

	// correct timestamp with server-unique-time (note: direct message correction)
	P.r_u32(timestamp);

	// read generic info
	P.r_u16(type);
	P.r_u16(destination);

	CSE_Abstract* receiver = game->get_entity_from_eid(destination);
	if (receiver)
	{
		// MP fork (§9.3 co-op reconnection): an ORPHANED body — a disconnected player's
		// actor kept alive in the world for reconnection — deliberately has no owning
		// client (coop_orphan_actor nulls it). Anything addressing that body (a peer
		// shooting the corpse, an item event) would otherwise trip this assert and take
		// the whole server down. Handling the event is fine; only the assertion is wrong.
		R_ASSERT(receiver->owner || receiver->m_coop_orphaned);
		receiver->OnEvent(P, type, timestamp, sender);
	};

	switch (type)
	{
	case GE_GAME_EVENT:
		{
			u16 game_event_type;
			P.r_u16(game_event_type);
			// MP fork, 2026-09-25 — MEASUREMENT for security-07's optional gate (refuse remote game-event types other
			// than the ones clients legitimately send). Before refusing anything, census what normal remote clients
			// actually send: one line per (type) the first time, then every 100th. Counts only; nothing is refused.
			if (m_enet && m_enet->running() && m_enet->owns(sender.value()))
			{
				static xr_map<u16, u32> s_ge;
				u32 const c = ++s_ge[game_event_type];
				if (c == 1 || (c % 100) == 0)
					Msg("[GEVENT] remote client 0x%08x sent game event type %u (count %u)", sender.value(), (u32)game_event_type, c);
			}
			game->AddDelayedEvent(P, game_event_type, timestamp, sender);
		}
		break;
	case GE_INFO_TRANSFER:
	case GE_WPN_STATE_CHANGE:
	case GE_ZONE_STATE_CHANGE:
	case GE_ACTOR_JUMPING:
	case GEG_PLAYER_PLAY_HEADSHOT_PARTICLE:
	case GEG_PLAYER_ATTACH_HOLDER:
	case GEG_PLAYER_DETACH_HOLDER:
	case GEG_PLAYER_ITEM2SLOT:
	case GEG_PLAYER_ITEM2BELT:
	case GEG_PLAYER_ITEM2RUCK:
	case GE_GRENADE_EXPLODE:
		{
			SendBroadcast(BroadcastCID, P, MODE);
		}
		break;
	case GEG_PLAYER_ACTIVATEARTEFACT:
		{
			Process_event_activate(P, sender, timestamp, destination, P.r_u16(), true);
			break;
		};
	case GE_INV_ACTION:
		{
			xrClientData* CL = ID_to_client(sender);
			if (CL) CL->net_Ready = TRUE;
			if (SV_Client) SendTo(SV_Client->ID, P, net_flags(TRUE, TRUE));
		}
		break;
	case GE_RESPAWN:
		{
			CSE_Abstract* E = receiver;
			if (E)
			{
				R_ASSERT(E->s_flags.is(M_SPAWN_OBJECT_PHANTOM));

				svs_respawn R;
				R.timestamp = timestamp + E->RespawnTime * 1000;
				R.phantom = destination;
				q_respawn.insert(R);
			}
		}
		break;
	case GE_TRADE_BUY:
		// MP fork (design doc §10.3 S2b): the hand-over that completes a purchase or a sale is where the server moves money.
		if (xr_enet::enabled() && receiver && (P.B.count - P.r_tell()) >= sizeof(u16))
		{
			const u32 r_save = P.r_tell();
			const u16 item_id = P.r_u16();
			P.r_seek(r_save);
			Process_event_ownership(P, sender, timestamp, destination);
			CSE_Abstract* const e_item = game->get_entity_from_eid(item_id);
			game_sv_Single* const single = smart_cast<game_sv_Single*>(game);
			if (single && e_item && e_item->ID_Parent == destination)
				single->coop_trade_settle(receiver, item_id);
			break;
		}
		// fall through
	case GE_OWNERSHIP_TAKE:
		{
			Process_event_ownership(P, sender, timestamp, destination);
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case GE_OWNERSHIP_TAKE_MP_FORCED:
		{
			Process_event_ownership(P, sender, timestamp, destination,TRUE);
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case GE_TRADE_SELL:
		// MP fork (design doc §10.3 S2a): a player taking an item out of a living trader's stock is asked about first.
		// A refusal drops this event, so the item stays with the trader and the GE_TRADE_BUY after it is refused by the
		// ownership guard. Peeked, not consumed: the reject path below reads the same id.
		if (xr_enet::enabled() && receiver && (P.B.count - P.r_tell()) >= sizeof(u16))
		{
			const u32 r_save = P.r_tell();
			const u16 item_id = P.r_u16();
			P.r_seek(r_save);
			game_sv_Single* const single = smart_cast<game_sv_Single*>(game);
			// §10.3 item 4: out of ANOTHER player's inventory only with that player's yes
			if (single && single->coop_consent_intercept(ID_to_client(sender), receiver, item_id))
				break;
			if (single && !single->coop_trade_allow(ID_to_client(sender), receiver, item_id))
				break;
			// §10.3 S2b: a player putting down an item they hold may be SELLING it; the trader's take pays them
			if (single && smart_cast<CSE_ALifeCreatureActor*>(receiver))
				single->coop_trade_note_sale(ID_to_client(sender), receiver, item_id);
		}
		// fall through
	case GE_OWNERSHIP_REJECT:
		// MP fork (design doc §10.3 gap 4c): a player's client may not DROP an item out of ANOTHER player's inventory (or a
		// disconnected player's reserved body). Stock trusts every reject; the server client and the owner's own client still do.
		if (type == GE_OWNERSHIP_REJECT && xr_enet::enabled() && receiver && smart_cast<CSE_ALifeCreatureActor*>(receiver))
		{
			xrClientData* const from = ID_to_client(sender);
			if (from && from != GetServerClient() && from->owner && receiver->owner != from)
			{
				static int s_off = -1;
				if (s_off < 0)
				{
					LPCSTR q = strstr(Core.Params, "-coop_drop_guard_off");
					s_off = (q && (q[sizeof("-coop_drop_guard_off") - 1] == 0 || q[sizeof("-coop_drop_guard_off") - 1] == ' ')) ? 1 : 0;
					Msg("- COOP(drop): drops out of another player's inventory are %s", s_off ? "ALLOWED (-coop_drop_guard_off, control)" : "refused");
				}
				if (!s_off)
				{
					u16 item_id = 0xffff;
					if ((P.B.count - P.r_tell()) >= sizeof(u16))
					{
						const u32 r_save = P.r_tell();
						item_id = P.r_u16();
						P.r_seek(r_save);
					}
					static u32 s_refused = 0;
					if (++s_refused <= 200 || (s_refused % 100) == 1)
						Msg("- COOP(drop): drop of item %u out of %u's inventory by client %u (player %u) REFUSED — not the owner [%u]",
							u32(item_id), u32(destination), sender.value(), u32(from->owner->ID), s_refused);
					break;
				}
			}
		}
		// fall through
	case GE_LAUNCH_ROCKET:
		{
			Process_event_reject(P, sender, timestamp, destination, P.r_u16());
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case GE_DESTROY:
		{
			Process_event_destroy(P, sender, timestamp, destination, NULL);
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case GE_TRANSFER_AMMO:
		{
			u16 id_entity;
			P.r_u16(id_entity);
			CSE_Abstract* e_parent = receiver; // кто забирает (для своих нужд)
			CSE_Abstract* e_entity = game->get_entity_from_eid(id_entity); // кто отдает
			if (!e_entity) break;
			if (0xffff != e_entity->ID_Parent) break; // this item already taken
			xrClientData* c_parent = e_parent->owner;
			xrClientData* c_from = ID_to_client(sender);
			// MP fork (§20 co-op): client-authoritative reload/ammo transfer on the thin
			// client emits GE_TRANSFER_AMMO whose ownership need not match the sender the
			// way stock MP guarantees. Don't let a client crash the server — skip on
			// mismatch instead of the fatal assert.
			if (xr_enet::enabled() && c_from != c_parent)
			{
				Msg("! MP co-op: ge_transfer_ammo ownership mismatch [%d] (skipped)", id_entity);
				break;
			}
			R_ASSERT(c_from == c_parent); // assure client ownership of event

			// Signal to everyone (including sender)
			SendBroadcast(BroadcastCID, P, MODE);

			// Perfrom real destroy
			entity_Destroy(e_entity);
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case GE_HIT:
	case GE_HIT_STATISTIC:
		{
			// MP fork (§27 co-op): AN ORPHAN ABSORBS FIRE AND DOES NOT DIE.
			//
			// Found by Caden, 2026-08-05: he shot a disconnected player's body, watched it die,
			// and the server discarded the kill entirely — the player reconnected alive at the
			// death position with 48 inventory items. THE RULE IS RIGHT. A reserved body is a
			// RECORD, not a simulation (15f71736): its CSE refuses writeback so an in-absentia
			// kill cannot cost a disconnected player their state. What was wrong was the
			// FEEDBACK — the shooter got every confirmation a real kill gives, for an event that
			// did not happen, and two players could then correctly disagree about whether
			// something occurred.
			//
			// The fix is therefore NOT to make the orphan killable, which would trade a cosmetic
			// inconsistency for exactly the state loss the discard exists to prevent. It is to
			// stop the simulation contradicting the record: refuse the hit, so the body visibly
			// absorbs fire and stays standing. That reads as PROTECTED rather than as dead.
			//
			// HERE rather than in the GAME_EVENT_ON_HIT handler because `receiver` is already
			// resolved above — one authority, one test, and no second place that has to agree
			// about what "orphaned" means. m_coop_orphaned is server-side only and is never
			// replicated, so a client-side variant would need a second source of truth for it.
			//
			// Only the DAMAGE is refused. The event was already delivered to the CSE above, and
			// client-side impact effects are predicted locally and will still play — the rounds
			// land, the body does not fall. If the body still DIES after this, the hit is
			// reaching the object by another path and this insertion point is wrong.
			// CONTROL ARM (§27e). `-coop_orphan_hit_allow` restores the OLD behaviour on the SAME
			// binary, so a test can demonstrate that it OBSERVES a killable-looking orphan before it
			// claims to observe a protected one. Without it the verification is one-sided: a run
			// where the orphan survives is equally consistent with the fix working and with the hit
			// never arriving. Bug 1 took seven runs to learn that; this arm is the lesson applied
			// before the first run rather than after the sixth.
			//
			// Exact-token match, not strstr on a prefix: `-coop_test_rpg` once matched inside
			// `-coop_test_rpg2` on this project and silently armed the wrong probe.
			static int s_allow = -1;
			if (s_allow < 0)
			{
				LPCSTR q = strstr(Core.Params, "-coop_orphan_hit_allow");
				s_allow = (q && (q[sizeof("-coop_orphan_hit_allow") - 1] == 0 ||
				                 q[sizeof("-coop_orphan_hit_allow") - 1] == ' ')) ? 1 : 0;
				if (s_allow)
					Msg("! COOP(orphan): -coop_orphan_hit_allow SET — hits on reserved bodies are "
						"NOT refused. This is the CONTROL arm; it reproduces the pre-fix behaviour.");
			}
			if (receiver && receiver->m_coop_orphaned && !s_allow)
			{
				// UNTHROTTLED FOR THE FIRST 50, THEN 1-IN-50. This logged only the 1st, 51st, ...
				// which is why the verified treatment run reads "server refused the hit: 1" against
				// EIGHT shots: that is a LOG-LINE count of a throttled message, not a refusal count,
				// and it invited exactly the reading that seven hits went unaccounted for. They did
				// not — the unthrottled damage-path marker showed 0 of 8 getting through — but the
				// refusal side could only name one of them, so seven were provable by absence
				// instead of by attribution.
				//
				// This is the same throttle-vs-probe defect found in game_sv_base.cpp on the damage
				// path, left standing here because only the other side had bitten yet. A refusal on
				// a reserved body is rare by construction; a firefight cannot raise these, because
				// an orphan is not a participant in one.
				static u32 s_orphan_hits = 0;
				if (++s_orphan_hits <= 50 || (s_orphan_hits % 50) == 1)
					Msg("- COOP(orphan): refusing hit on reserved body id %u (%u so far) — an "
						"unclaimed body absorbs fire; the record it restores from cannot be shot",
						destination, s_orphan_hits);
				break;
			}

			// MP fork (§3.4 object-0 scope, -coop_saveactor_exclude): the server's save actor is not a participant — refuse the hit
			if (receiver && coop_ghost_excluded(receiver->ID))
			{
				static u32 s_ghost_hits = 0;
				if (++s_ghost_hits <= 50 || (s_ghost_hits % 50) == 1)
					Msg("- COOP(ghost): refusing hit on the save actor %u (%u so far)", u32(destination), s_ghost_hits);
				break;
			}

			// MP fork (design doc §12): friendly-fire / PvP modes. Only a PLAYER body shooting another PLAYER body is
			// ever filtered; the host's save actor (owned by the server client) is not a player, and orphans were
			// refused just above. The shooter is SHit::whoID, the first field after the destination the dispatcher
			// already read, so peek it without moving the cursor the delayed event re-reads from.
			if (xr_enet::enabled() && receiver && (P.r_pos + sizeof(u16) <= P.B.count))
			{
				u16 who = 0;
				CopyMemory(&who, &P.B.data[P.r_pos], sizeof(u16));
				CSE_Abstract* const shooter = game->get_entity_from_eid(who);
				if (shooter && shooter->owner && receiver->owner &&
				    shooter->owner != GetServerClient() && receiver->owner != GetServerClient() &&
				    coop_ff_refuse_hit(shooter, receiver, (g_pGameLevel ? Level().name().c_str() : "")))
					break;
			}

			P.r_pos -= 2;
			if (type == GE_HIT_STATISTIC)
			{
				P.B.count -= 4;
				P.w_u32(sender.value());
			};
			game->AddDelayedEvent(P, GAME_EVENT_ON_HIT, 0, ClientID());
		}
		break;
	case GE_ASSIGN_KILLER:
		{
			u16 id_src;
			P.r_u16(id_src);

			CSE_Abstract* e_dest = receiver; // кто умер
			// this is possible when hit event is sent before destroy event
			if (!e_dest)
				break;

			CSE_ALifeCreatureAbstract* creature = smart_cast<CSE_ALifeCreatureAbstract*>(e_dest);
			if (creature)
				creature->set_killer_id(id_src);

			//		Msg							("[%d][%s] killed [%d][%s]",id_src,id_src==u16(-1) ? "UNKNOWN" : game->get_entity_from_eid(id_src)->name_replace(),id_dest,e_dest->name_replace());

			break;
		}
	case GE_CHANGE_VISUAL:
		{
			CSE_Visual* visual = smart_cast<CSE_Visual*>(receiver);
			VERIFY(visual);
			string256 tmp;
			P.r_stringZ(tmp);
			visual->set_visual(tmp);
		}
		break;
	case GE_DIE:
		{
			// Parse message
			u16 id_dest = destination, id_src;
			P.r_u16(id_src);


			xrClientData* l_pC = ID_to_client(sender);
			VERIFY(game && l_pC);
#ifndef MASTER_GOLD
			if ((game->Type() != eGameIDSingle) && l_pC && l_pC->owner)
			{
				Msg					("* [%2d] killed by [%2d] - sended by [0x%08x]", id_dest, id_src, l_pC->ID.value());
			}
#endif // #ifndef MASTER_GOLD

			CSE_Abstract* e_dest = receiver; // кто умер
			// this is possible when hit event is sent before destroy event
			if (!e_dest)
				break;

#ifndef MASTER_GOLD
			if (game->Type() != eGameIDSingle)
				Msg				("* [%2d] is [%s:%s]", id_dest, *e_dest->s_name, e_dest->name_replace());
#endif // #ifndef MASTER_GOLD

			CSE_Abstract* e_src = game->get_entity_from_eid(id_src); // кто убил
			if (!e_src)
			{
				xrClientData* C = (xrClientData*)game->get_client(id_src);
				if (C) e_src = C->owner;
			};
			VERIFY(e_src);
			if (!e_src)
			{
				Msg("! ERROR: SV: src killer not exist.");
				return;
			}
			//			R_ASSERT2			(e_dest && e_src, "Killer or/and being killed are offline or not exist at all :(");
#ifndef MASTER_GOLD
			if (game->Type() != eGameIDSingle)
				Msg				("* [%2d] is [%s:%s]", id_src, *e_src->s_name, e_src->name_replace());
#endif // #ifndef MASTER_GOLD

			game->on_death(e_dest, e_src);

			xrClientData* c_src = e_src->owner; // клиент, чей юнит убил

			// MP fork (co-op, found by the §10.2 emission control arm): the killer can be an ORPHANED player body — its client
			// crashed or left while the GE_DIE waited in the delayed-packet queue — and then e_src->owner is NULL. The stock
			// code read c_src->owner unconditionally (server AV at Process_event+0xd31, NULL+0x8190, from
			// ProceedDelayedPackets). With no owning client there is nobody to credit or notify: broadcast the death as is.
			if (!c_src || !c_src->owner)
			{
				static u32 s_orphan_killer = 0;
				if (++s_orphan_killer <= 20)
					Msg("- COOP(die): [%u] killed by [%u] whose client is gone — death broadcast without killer credit",
						u32(id_dest), u32(id_src));
				SendBroadcast(BroadcastCID, P, MODE);
				break;
			}

			if (c_src->owner->ID == id_src)
			{
				// Main unit
				P.w_begin(M_EVENT);
				P.w_u32(timestamp);
				P.w_u16(type);
				P.w_u16(destination);
				P.w_u16(id_src);
				P.w_clientID(c_src->ID);
			}

			SendBroadcast(BroadcastCID, P, MODE);

			//////////////////////////////////////////////////////////////////////////
			// 
			if (game->Type() == eGameIDSingle)
			{
				P.w_begin(M_EVENT);
				P.w_u32(timestamp);
				P.w_u16(GE_KILL_SOMEONE);
				P.w_u16(id_src);
				P.w_u16(destination);
				SendTo(c_src->ID, P, net_flags(TRUE, TRUE));
			}
			//////////////////////////////////////////////////////////////////////////
#ifdef DEBUG
			VERIFY(verify_entities());
#endif
		}
		break;
	case GE_ADDON_ATTACH:
	case GE_ADDON_DETACH:
		{
			SendBroadcast(BroadcastCID, P, net_flags(TRUE, TRUE));
		}
		break;
	case GE_CHANGE_POS:
		{
			SendTo(SV_Client->ID, P, net_flags(TRUE, TRUE));
		}
		break;
	case GE_INSTALL_UPGRADE:
		{
			shared_str upgrade_id;
			P.r_stringZ(upgrade_id);
			CSE_ALifeInventoryItem* iitem = smart_cast<CSE_ALifeInventoryItem*>(receiver);
			if (!iitem)
			{
				break;
			}
			iitem->add_upgrade(upgrade_id);
		}
		break;
	case GE_INV_BOX_STATUS:
		{
			u8 can_take, closed;
			P.r_u8(can_take);
			P.r_u8(closed);
			shared_str tip_text;
			P.r_stringZ(tip_text);

			CSE_ALifeInventoryBox* box = smart_cast<CSE_ALifeInventoryBox*>(receiver);
			if (!box)
			{
				break;
			}
			box->m_can_take = (can_take == 1);
			box->m_closed = (closed == 1);
			box->m_tip_text._set(tip_text);
		}
		break;
	case GE_INV_OWNER_STATUS:
		{
			u8 can_take, closed;
			P.r_u8(can_take);
			P.r_u8(closed);

			CSE_ALifeTraderAbstract* iowner = smart_cast<CSE_ALifeTraderAbstract*>(receiver);
			if (!iowner)
			{
				break;
			}
			iowner->m_deadbody_can_take = (can_take == 1);
			iowner->m_deadbody_closed = (closed == 1);
		}
		break;

	case GEG_PLAYER_DISABLE_SPRINT:
	case GEG_PLAYER_WEAPON_HIDE_STATE:
		{
			SendTo(SV_Client->ID, P, net_flags(TRUE, TRUE));

#	ifdef SLOW_VERIFY_ENTITIES
			VERIFY					(verify_entities());
#	endif
		}
		break;
	case GEG_PLAYER_ACTIVATE_SLOT:
	case GEG_PLAYER_ITEM_EAT:
		{
			SendTo(SV_Client->ID, P, net_flags(TRUE, TRUE));
#	ifdef SLOW_VERIFY_ENTITIES
			VERIFY					(verify_entities());
#	endif
		}
		break;
	case GEG_PLAYER_USE_BOOSTER:
		{
			if (receiver && receiver->owner && (receiver->owner != SV_Client))
			{
				NET_Packet tmp_packet;
				CGameObject::u_EventGen(tmp_packet, GEG_PLAYER_USE_BOOSTER, receiver->ID);
				SendTo(receiver->owner->ID, P, net_flags(TRUE, TRUE));
			}
		}
		break;
	case GEG_PLAYER_ITEM_SELL:
		{
			game->OnPlayer_Sell_Item(sender, P);
		}
		break;
	case GE_TELEPORT_OBJECT:
		{
			game->teleport_object(P, destination);
		}
		break;
	case GE_ADD_RESTRICTION:
		{
			game->add_restriction(P, destination);
		}
		break;
	case GE_REMOVE_RESTRICTION:
		{
			game->remove_restriction(P, destination);
		}
		break;
	case GE_REMOVE_ALL_RESTRICTIONS:
		{
			game->remove_all_restrictions(P, destination);
		}
		break;
	case GE_MONEY:
		{
			CSE_Abstract* e_dest = receiver;
			CSE_ALifeTraderAbstract* pTa = smart_cast<CSE_ALifeTraderAbstract*>(e_dest);
			const u32 amount = P.r_u32();
			// MP fork (design doc §10.3 S2b): in co-op the server owns money. A client's GE_MONEY (the trade menu's, a
			// script's give_money on a client) is refused and answered with the ledger; the server's own is applied and,
			// for a player, broadcast so every client shows the balance the server holds.
			game_sv_Single* const single = smart_cast<game_sv_Single*>(game);
			if (pTa && single && game_sv_Single::coop_money_server_owned())
			{
				xrClientData* const from = ID_to_client(sender);
				if (from != GetServerClient())
				{
					static u32 s_refused = 0;
					if (++s_refused <= 200 || (s_refused % 100) == 1)
						Msg("- COOP(money): GE_MONEY from a client for %u (%u) REFUSED — the ledger holds %u [%u]",
							u32(destination), amount, pTa->m_dwMoney, s_refused);
					single->coop_money_send(from, destination);
					break;
				}
				pTa->m_dwMoney = amount;
				if (smart_cast<CSE_ALifeCreatureActor*>(receiver))
					single->coop_money_send(NULL, destination);
				break;
			}
			if (pTa)
				pTa->m_dwMoney = amount;
		}
		break;
	case GE_TRADER_FLAGS:
		{
			CSE_ALifeTraderAbstract* pTa = smart_cast<CSE_ALifeTraderAbstract*>(receiver);
			if (pTa)
			{
				pTa->m_trader_flags.assign(P.r_u32());
			}
		}
		break;
	case GE_FREEZE_OBJECT:
		break;
	case GE_REQUEST_PLAYERS_INFO:
		{
			SendPlayersInfo(sender);
		}
		break;
	default:
		R_ASSERT2(0, "Game Event not implemented!!!");
		break;
	}
}
