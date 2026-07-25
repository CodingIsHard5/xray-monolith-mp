#include "stdafx.h"
#include "xrserver.h"
#include "game_sv_single.h"
#include "alife_simulator.h"
#include "xrserver_objects.h"
#include "level.h"
#include "../xrNetServer/xr_enet_transport.h"      // MP fork: xr_enet::enabled()

void xrServer::OnCL_Disconnected(IClient* CL)
{
	//csPlayers.Enter			();

	// Game config (all, info includes deleted player now, excludes at the next cl-update)
	NET_Packet P;
	P.B.count = 0;
	P.w_clientID(CL->ID);
	xrClientData* xrCData = (xrClientData*)(CL);
	VERIFY(xrCData);

	// §3 win #2b inc2 (CodeRabbit inc1): drop this client's per-client relevance known-set so a reused
	// client id never inherits stale spawn state. Safe to call unconditionally (no-op if absent / cull off).
	coop_clear_relevance(CL->ID.value());

	if (!xrCData->ps)
		return;

	P.w_stringZ(xrCData->ps->getName());
	P.w_u16(xrCData->ps->GameID);
	P.r_pos = 0;

	ClientID clientID;
	clientID.set(0);

	game->AddDelayedEvent(P, GAME_EVENT_PLAYER_DISCONNECTED, 0, clientID);

	// MP fork (§9.3/9.4 co-op reconnection): in co-op mode, DON'T migrate or destroy
	// the disconnecting player's actor entity. Instead, orphan it — the entity stays in
	// the world at its last position with no owner. If the player reconnects within the
	// timeout window, it gets re-associated. game_sv_Single handles the orphan lifecycle.
	if (xr_enet::enabled() && !CL->flags.bLocal)
	{
		game_sv_Single* sv_single = smart_cast<game_sv_Single*>(game);
		if (sv_single)
		{
			sv_single->OnCoopClientDisconnected(xrCData);
			// coop_orphan_actor already nulled ownership on actor + children and
			// set m_coop_orphaned. No migration needed — orphans stay in the entity map.
			Server_Client_Check(CL);
			return;
		}
	}

	//
	xrS_entities::iterator I = entities.begin(), E = entities.end();
	if (GetClientsCount() > 1 && !CL->flags.bLocal)
	{
		// Migrate entities
		for (; I != E; ++I)
		{
			CSE_Abstract* entity = I->second;
			if (entity->owner == CL) PerformMigration(entity, (xrClientData*)CL,
			                                          SelectBestClientToMigrateTo(entity,TRUE));
		}
	}
	else
	{
		// Destroy entities
		while (!entities.empty())
		{
			CSE_Abstract* entity = entities.begin()->second;
			entity_Destroy(entity);
		}
	}
	//csPlayers.Leave			();

	Server_Client_Check(CL);
}
