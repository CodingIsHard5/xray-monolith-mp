#include "stdafx.h"
#include "xrServer.h"
#include "xrServer_Objects.h"
#include "xrServer_Objects_ALife_Monsters.h"    // MP fork (§14 step 7 P4 D2): CSE_ALifeCreatureActor
#include "../xrNetServer/xr_enet_transport.h"   // MP fork (§14 step 7 P4 D2): xr_enet::enabled()

int g_Dump_Update_Read = 0;

void xrServer::Process_update(NET_Packet& P, ClientID sender)
{
	xrClientData* CL = ID_to_client(sender);
	R_ASSERT2(CL, "Process_update client not found");

#ifndef MASTER_GOLD
	if (g_Dump_Update_Read) Msg("---- UPDATE_Read --- ");
#endif // #ifndef MASTER_GOLD

	R_ASSERT(CL->flags.bLocal);
	// while has information
	while (!P.r_eof())
	{
		// find entity
		u16 ID;
		u8 size;

		P.r_u16(ID);
		P.r_u8(size);
		u32 _pos = P.r_tell();
		CSE_Abstract* E = ID_to_entity(ID);

		// MP fork (§14 step 7 P4 D2, run 4): THIS is where a reserved body rots. The dedicated
		// server's own loopback client exports every online object it simulates and the server
		// writes that straight onto the CSE — so an UNCLAIMED player body, whose server-side
		// actor object nobody placed and nobody drives, overwrites its own record with where
		// that object ended up: caught mid-fall at Y=-1.02e8 (run 3) and parked dead at the
		// world origin with hp 0.00 (runs 2 and 4). P2 §3b called this "uninitialized memory";
		// it is not, it is a real object being simulated with no driver, and the record it
		// destroys is the one a returning player is restored from.
		//
		// A reserved body is a RECORD, not a simulation. Skip the writeback for it — the bytes
		// are still consumed, exactly as for an unknown id — and the CSE keeps the position and
		// health the save (or the reclaim) put there. Claimed bodies are untouched: those are
		// maintained by their owner's M_CL_UPDATE, which is the co-op authority for a player.
		// …and a CLAIMED body is its PLAYER's, which is the same rule from the other end. D2 run 7
		// measured the two writers fighting over one field: three recovery samples read the server
		// object's stale -220.6 and one caught the player's real -205.6, so the position that
		// reached the logged-off binding record was where the body was HANDED OVER, not where its
		// owner walked to. The player's M_CL_UPDATE is the co-op authority for their own body
		// (§15) — the server's own copy of it must not compete. Same principle as keeping player
		// actors out of the outgoing update stream, applied to the incoming writeback.
		const bool coop_player_body = E && xr_enet::enabled()
			&& (E->m_coop_orphaned
				|| (smart_cast<CSE_ALifeCreatureActor*>(E) && E->owner && E->owner != GetServerClient()));
		if (coop_player_body)
		{
			E->net_Ready = TRUE;   // still "seen"; only the state write is refused
			static u32 s_skipped = 0;
			if ((++s_skipped % 600) == 1)
				Msg("- COOP(bindings): refusing the server's own update for body id %u (%u so far) "
					"— %s", E->ID, s_skipped, E->m_coop_orphaned
						? "an unclaimed body is a record, not a simulation"
						: "a claimed body belongs to its player, not to the server's copy");
			P.r_advance(size);
			continue;
		}

		if (E)
		{
			//Msg				("sv_import: %d '%s'",E->ID,E->name_replace());
			E->net_Ready = TRUE;
			E->UPDATE_Read(P);

			if (g_Dump_Update_Read) Msg("* %s : %d - %d", E->name(), size, P.r_tell() - _pos);

			if ((P.r_tell() - _pos) != size)
			{
				string16 tmp;
				CLSID2TEXT(E->m_tClassID, tmp);
				Debug.fatal(DEBUG_INFO,
				            "Beer from the creator of '%s'; initiator: 0x%08x, r_tell() = %d, pos = %d, objectID = %d, size = %d",
				            tmp,
				            CL->ID.value(),
				            P.r_tell(),
				            _pos,
				            E->ID,
							size
				);
			}
		}
		else
			P.r_advance(size);
	}
#ifndef MASTER_GOLD
	if (g_Dump_Update_Read) Msg("-------------------- ");
#endif // #ifndef MASTER_GOLD
}

void xrServer::Process_save(NET_Packet& P, ClientID sender)
{
	xrClientData* CL = ID_to_client(sender);
	R_ASSERT2(CL, "Process_save client not found");
	CL->net_Ready = TRUE;

	R_ASSERT(CL->flags.bLocal);
	// while has information
	while (!P.r_eof())
	{
		// find entity
		u16 ID;
		u16 size;

		P.r_u16(ID);
		P.r_u16(size);
		s32 _pos_start = P.r_tell();
		CSE_Abstract* E = ID_to_entity(ID);

		if (E)
		{
			E->net_Ready = TRUE;
			E->load(P);
		}
		else
			P.r_advance(size);
		s32 _pos_end = P.r_tell();
		s32 _size = size;
		if (_size != (_pos_end - _pos_start))
		{
			Msg("! load/save mismatch, object: '%s'", E ? E->name_replace() : "unknown");
			s32 _rollback = _pos_start + _size;
			P.r_seek(_rollback);
		}
	}
}
