#include "stdafx.h"
#include "level.h"
#include "xrServerMapSync.h"
#include "../xrCore/stream_reader.h"
#include "MainMenu.h"
#include "string_table.h"
#include "../xrEngine/xr_ioconsole.h"

static const u32 r_buffer_size = 131072; //128 Kb
void CLevel::CalculateLevelCrc32()
{
	void* read_buffer = _alloca(r_buffer_size);
	Msg("* calculating checksum of level.geom");
	CStreamReader* geom = FS.rs_open("$level$", "level.geom");
	R_ASSERT2(geom, "failed to open level.geom file");
	u32 remaind = geom->elapsed();
	map_data.m_level_geom_crc32 = 0;
	while (remaind)
	{
		u32 to_read = remaind;
		if (remaind > r_buffer_size)
		{
			to_read = r_buffer_size;
		}
		geom->r(read_buffer, to_read);
		map_data.m_level_geom_crc32 ^= crc32(read_buffer, to_read);
		remaind = geom->elapsed();
	}
	FS.r_close(geom);
}

bool CLevel::IsChecksumsEqual(u32 check_sum) const
{
	return check_sum == map_data.m_level_geom_crc32;
}

bool CLevel::synchronize_map_data()
{
	if (!OnClient() && !IsDemoSave())
	{
		deny_m_spawn = FALSE;
		map_data.m_map_sync_received = true;
		return synchronize_client();
	}

#ifndef MASTER_GOLD
	Msg					("* synchronizing map data...");
#endif // #ifndef MASTER_GOLD

	map_data.CheckToSendMapSync();

#ifdef DEBUG
	Msg("--- Waiting for server map name...");
#endif // #ifdef DEBUG
	ClientReceive();

	if ((map_data.m_wait_map_time >= 1000) && (!map_data.m_map_sync_received) && !IsDemoPlay()) //about 5 seconds
	{
		Msg("Wait map data time out: reconnecting...");
		MakeReconnect();
		g_loading_events.erase(++g_loading_events.begin(), g_loading_events.end());
		return true;
	}

	if (!map_data.m_map_sync_received)
	{
		Sleep(5);
		++map_data.m_wait_map_time;
		return false;
	}

	if (map_data.IsInvalidMapOrVersion())
	{
		Msg("! Incorect map or version, reconnecting...");
		MakeReconnect();
		g_loading_events.erase(++g_loading_events.begin(), g_loading_events.end());
		return true;
	}
	if (map_data.IsInvalidClientChecksum())
	{
		// MP fork diag: this path is a SILENT infinite stall in stock — no
		// message, no timeout, just `return false` forever, which presents as
		// a client parked on the loading screen at idle CPU with a dead log.
		// Say so once (MASTER_GOLD compiles out every other Msg around here).
		if (!m_xrnet_dbg_bad_crc_logged)
		{
			m_xrnet_dbg_bad_crc_logged = true;
			Msg("! XRNET(dbg): map-sync REJECTED by server: invalid geom checksum "
				"(client crc32=0x%08x, map='%s' ver='%s'). Client will stall here.",
				map_data.m_level_geom_crc32, map_data.m_name.c_str(),
				map_data.m_map_version.c_str());
		}
		connected_to_server = FALSE;
		return false; //!!!
	}
	if (!m_xrnet_dbg_sync_ok_logged)
	{
		m_xrnet_dbg_sync_ok_logged = true;
		Msg("- XRNET(dbg): map-sync ACCEPTED (client crc32=0x%08x, map='%s' ver='%s') "
			"-> waiting for server configuration",
			map_data.m_level_geom_crc32, map_data.m_name.c_str(),
			map_data.m_map_version.c_str());
	}
	return synchronize_client();
}

bool CLevel::synchronize_client()
{
	//---------------------------------------------------------------------------
	if (!sended_request_connection_data)
	{
		NET_Packet P;
		P.w_begin(M_CLIENT_REQUEST_CONNECTION_DATA);

		Send(P, net_flags(TRUE, TRUE, TRUE, TRUE));
		sended_request_connection_data = TRUE;
	}
	//---------------------------------------------------------------------------
	if (game_configured)
	{
		if (!m_xrnet_dbg_configured_logged)
		{
			m_xrnet_dbg_configured_logged = true;
			Msg("- XRNET(dbg): game_configured -> proceeding to spawn");
		}
		deny_m_spawn = FALSE;
		return true;
	}
#ifdef DEBUG
	Msg("--- Waiting for server configuration...");
#endif // #ifdef DEBUG
	// MP fork diag: the other SILENT stall. If the server never answers
	// M_CLIENT_REQUEST_CONNECTION_DATA, game_configured stays false and this
	// spins forever with no message under MASTER_GOLD. Heartbeat it so the log
	// distinguishes "stuck waiting for config" from "stuck on checksum".
	if ((m_xrnet_dbg_cfg_waits++ % 400) == 0) // ~ every 2s at Sleep(5)
	{
		Msg("- XRNET(dbg): waiting for server configuration... (waits=%u, Server=%s)",
			m_xrnet_dbg_cfg_waits, Server ? "yes" : "no");
	}
	if (Server)
	{
		ClientReceive();
		Server->Update();
	} // if OnClient ClientReceive method called in upper invokation
	//Sleep(5);
	return !!game_configured;
}


void LevelMapSyncData::CheckToSendMapSync()
{
	if (!m_sended_map_name_request)
	{
		NET_Packet P;
		P.w_begin(M_SV_MAP_NAME);
		P.w_stringZ(m_name);
		P.w_stringZ(m_map_version);
		P.w_u32(m_level_geom_crc32);
		Level().Send(P, net_flags(TRUE, TRUE, TRUE, TRUE));
		m_sended_map_name_request = true;
		invalid_geom_checksum = false;
		m_map_sync_received = false;
		m_wait_map_time = 0;
	}
}

void LevelMapSyncData::ReceiveServerMapSync(NET_Packet& P)
{
	m_map_sync_received = true;
	MapSyncResponse server_resp = static_cast<MapSyncResponse>(P.r_u8());
	if (server_resp == InvalidChecksum)
	{
		invalid_geom_checksum = true;
	}
	else if (server_resp == YouHaveOtherMap)
	{
		invalid_map_or_version = true;
	}
}
