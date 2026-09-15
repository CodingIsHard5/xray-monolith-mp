#include "stdafx.h"
#include "xrserver.h"
#include "xrserver_objects.h"
#include "xrserver_objects_alife_monsters.h"
#include "game_sv_single.h"                        // MP fork (§10.3 S2d): the pending-sale record
#include "xrServer_svclient_validation.h"
#include "../xrNetServer/xr_enet_transport.h"   // MP fork (§10.5): co-op claim logging

void ReplaceOwnershipHeader(NET_Packet& P)
{
	//способ очень грубый, но на данный момент иного выбора нет. Заранее приношу извинения
	u16 NewType = GE_OWNERSHIP_TAKE;
	CopyMemory(&P.B.data[6], &NewType, 2);
};

void xrServer::Process_event_ownership(NET_Packet& P, ClientID sender, u32 time, u16 ID, BOOL bForced)
{
	u32 MODE = net_flags(TRUE,TRUE, FALSE, TRUE);

	u16 id_parent = ID, id_entity;
	P.r_u16(id_entity);
	CSE_Abstract* e_parent = game->get_entity_from_eid(id_parent);
	CSE_Abstract* e_entity = game->get_entity_from_eid(id_entity);


#ifdef MP_LOGGING
	Msg( "--- SV: Process ownership take: parent [%d][%s], item [%d][%s]", 
		id_parent, e_parent ? e_parent->name_replace() : "null_parent",
		id_entity, e_entity ? e_entity->name() : "null_entity");
#endif // MP_LOGGING

	if (!e_parent)
	{
		Msg("! ERROR on ownership: parent not found. parent_id = [%d], entity_id = [%d], frame = [%d].", id_parent,
		    id_entity, Device.dwFrame);
		return;
	}
	if (!e_entity)
	{
		//		Msg( "! ERROR on ownership: entity not found. parent_id = [%d], entity_id = [%d], frame = [%d].", id_parent, id_entity, Device.dwFrame );
		return;
	}

	if (!is_object_valid_on_svclient(id_parent))
	{
		Msg(
			"! ERROR on ownership: parent object is not valid on sv client. parent_id = [%d], entity_id = [%d], frame = [%d]",
			id_parent, id_entity, Device.dwFrame);
		return;
	}

	if (!is_object_valid_on_svclient(id_entity))
	{
		Msg(
			"! ERROR on ownership: entity object is not valid on sv client. parent_id = [%d], entity_id = [%d], frame = [%d]",
			id_parent, id_entity, Device.dwFrame);
		return;
	}

	// MP fork (design doc §10.5 "an artifact is a shared world object ONE player can claim"): this guard is what keeps a
	// claim race conserved — the first take the server processes wins, a second take of an item that already has a parent
	// is dropped. Logged for co-op so the race is visible; -coop_ownership_allow_double removes the guard (control arm only:
	// it lets the second take reparent an item that is still listed as the first owner's child).
	if (0xffff != e_entity->ID_Parent)
	{
		static int s_allow_double = -1;
		if (s_allow_double < 0)
			s_allow_double = strstr(Core.Params, "-coop_ownership_allow_double") ? 1 : 0;
		const bool log = xr_enet::enabled();
		if (!s_allow_double)
		{
			if (log)
				Msg("- COOP(claim): take of item %u by %u REFUSED — already held by %u", u32(id_entity), u32(id_parent),
					u32(e_entity->ID_Parent));
			return;
		}
		if (log)
			Msg("! COOP(claim): take of item %u by %u while held by %u ALLOWED (-coop_ownership_allow_double, control)",
				u32(id_entity), u32(id_parent), u32(e_entity->ID_Parent));
	}

	xrClientData* c_parent = e_parent->owner;
	xrClientData* c_entity = e_entity->owner;
	xrClientData* c_from = ID_to_client(sender);

	// MP fork (design doc §10.3 S2d): a SALE to an NPC trader. The trade menu sends the trader's take from the SELLER's
	// client, which is neither the server client nor the trader's owner, so this rule silently dropped every sale in co-op
	// and left the item on the floor (measured: trade-sell). Trusted when a living NPC trader takes an item that the
	// sending client's own actor put down in a GE_TRADE_SELL moments ago.
	bool coop_sale = false;
	if (xr_enet::enabled() && c_from && GetServerClient() != c_from && c_parent != c_from && c_from->owner &&
		!smart_cast<CSE_ALifeCreatureActor*>(e_parent) && smart_cast<CSE_ALifeTraderAbstract*>(e_parent))
	{
		CSE_ALifeCreatureAbstract* const trader_creature = smart_cast<CSE_ALifeCreatureAbstract*>(e_parent);
		game_sv_Single* const single = smart_cast<game_sv_Single*>(game);
		coop_sale = single && (!trader_creature || trader_creature->g_Alive()) &&
			single->coop_trade_sale_pending(id_entity, c_from->owner->ID);
		if (coop_sale)
			Msg("- COOP(claim): take of item %u by trader %u from the seller's client (%u) — trusted as a sale", u32(id_entity),
				u32(id_parent), u32(c_from->owner->ID));
	}
	if ((GetServerClient() != c_from) && (c_parent != c_from) && !coop_sale)
	{
		// trust only ServerClient or new_ownerClient
		if (xr_enet::enabled())
		{
			static u32 s_untrusted = 0;
			if (++s_untrusted <= 100 || (s_untrusted % 100) == 1)
				Msg("- COOP(claim): take of item %u by %u REFUSED — sent by a client that is neither the server nor the new owner's [%u]",
					u32(id_entity), u32(id_parent), s_untrusted);
		}
		return;
	}

	CSE_ALifeCreatureAbstract* alife_entity = smart_cast<CSE_ALifeCreatureAbstract*>(e_parent);
	if (alife_entity && !alife_entity->g_Alive() && game->Type() != eGameIDSingle)
	{
#ifdef MP_LOGGING
		Msg("--- SV: WARNING: dead player [%d] tries to take item [%d]", id_parent, id_entity);
#endif //#ifdef MP_LOGGING
		return;
	};

	// Game allows ownership of entity
	if (game->OnTouch(id_parent, id_entity, bForced))
	{
		// Perform migration if needed
		if (c_parent != c_entity) PerformMigration(e_entity, c_entity, c_parent);

		if (xr_enet::enabled())
			Msg("- COOP(claim): item %u [%s] taken by %u (was parent %u)", u32(id_entity), e_entity->s_name.c_str(),
				u32(id_parent), u32(e_entity->ID_Parent));
		// Rebuild parentness
		e_entity->ID_Parent = id_parent;
		e_parent->children.push_back(id_entity);

		if (bForced)
		{
			ReplaceOwnershipHeader(P);
		}
		// Signal to everyone (including sender)
		SendBroadcast(BroadcastCID, P, MODE);
	}
}
