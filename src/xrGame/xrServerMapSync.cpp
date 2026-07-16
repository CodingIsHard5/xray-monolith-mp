#include "stdafx.h"
#include "Level.h"
#include "xrServer.h"
#include "xrServerMapSync.h"

void xrServer::OnProcessClientMapData(NET_Packet& P, ClientID const& clientID)
{
#ifdef DEBUG
	Msg("--- Sending map data to client 0x%08x", clientID);
#endif // #ifdef DEBUG
	NET_Packet responseP;
	string128 client_map_name;
	string128 client_map_version;
	u32 client_geom_crc32;

	P.r_stringZ_s(client_map_name);
	P.r_stringZ_s(client_map_version);
	P.r_u32(client_geom_crc32);

	LPCSTR server_map_name = Level().get_net_DescriptionData().map_name;
	LPCSTR server_map_version = Level().get_net_DescriptionData().map_version;

	// MP fork: on an ENet -mp_host server the in-process host-client's
	// m_game_description is never populated (no DirectPlay host-enum), so it is
	// empty and mismatches every remote client -> endless "Incorect map"
	// reconnect. Fall back to the actual loaded level, matching what the
	// server sends a joining ENet client (Level().name(), ver "1.0").
	if (!server_map_name || !server_map_name[0])
		server_map_name = Level().name().c_str();
	if (!server_map_version || !server_map_version[0])
		server_map_version = "1.0";

	responseP.w_begin(M_SV_MAP_NAME);

	// MP fork diag: this is where the server decides a joining client's fate,
	// and stock says nothing about it outside #ifdef DEBUG — so a rejected
	// client just stalls silently on its loading screen with no clue on either
	// side. Log the actual comparison unconditionally (one line per join).
	LPCSTR verdict = "SuccessSync";

	if ((xr_strcmp(server_map_name, client_map_name)) ||
		(xr_strcmp(server_map_version, client_map_version)))
	{
		responseP.w_u8(static_cast<u8>(YouHaveOtherMap));
		verdict = "YouHaveOtherMap";
		//here we can make hard disconnect of this client...
	}
	else if (!Level().IsChecksumsEqual(client_geom_crc32))
	{
		responseP.w_u8(static_cast<u8>(InvalidChecksum));
		verdict = "InvalidChecksum";
	}
	else
	{
		responseP.w_u8(static_cast<u8>(SuccessSync));
	}

	Msg("- XRNET(dbg): map-sync from client 0x%08x -> %s | map: client='%s' ver='%s' "
		"server='%s' ver='%s' | geom crc32: client=0x%08x server=0x%08x",
		clientID.value(), verdict, client_map_name, client_map_version,
		server_map_name, server_map_version,
		client_geom_crc32, Level().GetLevelGeomCrc32());

	SendTo(clientID, responseP, net_flags(TRUE, TRUE));
}
