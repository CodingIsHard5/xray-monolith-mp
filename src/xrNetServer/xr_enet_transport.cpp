////////////////////////////////////////////////////////////////////////////
// MP fork: ENet transport implementation (see xr_enet_transport.h)
////////////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include "xr_enet_transport.h"
#include "NET_Server.h"
#include "NET_Client.h"
#include "NET_Messages.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#include "../3rd party/enet/include/enet/enet.h"

namespace xr_enet
{
	static int s_enet_refs = 0;

	bool enabled()
	{
		return !!strstr(Core.Params, "-xrnet_udp");
	}

	static bool enet_ref()
	{
		if (s_enet_refs == 0 && enet_initialize() != 0)
		{
			Msg("! XRNET(enet): enet_initialize failed");
			return false;
		}
		++s_enet_refs;
		return true;
	}

	static void enet_unref()
	{
		if (s_enet_refs > 0 && --s_enet_refs == 0)
			enet_deinitialize();
	}

	// reliable -> channel 0 reliable; else channel 1, sequenced unless
	// the caller asked for non-sequential (then unsequenced)
	static void flags_to_enet(u32 dpnsend_flags, u8& channel, u32& pflags)
	{
		if (dpnsend_flags & DPNSEND_GUARANTEED)
		{
			channel = 0;
			pflags = ENET_PACKET_FLAG_RELIABLE;
		}
		else
		{
			channel = 1;
			pflags = (dpnsend_flags & DPNSEND_NONSEQUENTIAL) ? ENET_PACKET_FLAG_UNSEQUENCED : 0;
		}
	}

	//======================================================================
	// server
	//======================================================================

	server_transport::server_transport(IPureServer* owner)
		: m_owner(owner), m_host(nullptr), m_stop(false), m_thread_up(false)
	{
	}

	server_transport::~server_transport()
	{
		stop();
	}

	bool server_transport::host(u32 port, u32 max_players)
	{
		if (!enet_ref()) return false;

		ENetAddress addr;
		addr.host = ENET_HOST_ANY;
		addr.port = (enet_uint16)port;

		ENetHost* h = enet_host_create(&addr, max_players ? max_players : 32, 2, 0, 0);
		if (!h)
		{
			Msg("! XRNET(enet): server host create FAILED on udp port %d", port);
			enet_unref();
			return false;
		}
		m_host = h;
		m_stop = false;
		thread_spawn(pump_thread, "xrnet-enet-sv", 0, this);
		m_thread_up = true;
		Msg("- XRNET(enet): server hosting on udp port %d (max %d)", port, max_players);
		return true;
	}

	void server_transport::stop()
	{
		if (!m_host) return;
		m_stop = true;
		// pump thread exits within one service slice; give it time
		for (u32 i = 0; i < 50 && m_thread_up; ++i) Sleep(10);
		enet_host_destroy((ENetHost*)m_host);
		m_host = nullptr;
		enet_unref();
	}

	void server_transport::send_to(u32 client_id, void* data, u32 size, u32 dpnsend_flags)
	{
		if (!m_host) return;
		ENetHost* h = (ENetHost*)m_host;
		u8 channel; u32 pflags;
		flags_to_enet(dpnsend_flags, channel, pflags);
		for (size_t i = 0; i < h->peerCount; ++i)
		{
			ENetPeer* p = &h->peers[i];
			if (p->state == ENET_PEER_STATE_CONNECTED && (u32)(uintptr_t)p->data == client_id)
			{
				ENetPacket* pkt = enet_packet_create(data, size, pflags);
				if (pkt) enet_peer_send(p, channel, pkt);
				return;
			}
		}
	}

	void server_transport::kick(u32 client_id)
	{
		if (!m_host) return;
		ENetHost* h = (ENetHost*)m_host;
		for (size_t i = 0; i < h->peerCount; ++i)
		{
			ENetPeer* p = &h->peers[i];
			if (p->state == ENET_PEER_STATE_CONNECTED && (u32)(uintptr_t)p->data == client_id)
			{
				enet_peer_disconnect(p, 0);
				return;
			}
		}
	}

	void server_transport::pump_thread(void* self)
	{
		((server_transport*)self)->pump();
		((server_transport*)self)->m_thread_up = false;
	}

	void server_transport::pump()
	{
		ENetHost* h = (ENetHost*)m_host;
		u32 next_id = 16; // low ids feel reserved; arbitrary but stable
		ENetEvent ev;

		while (!m_stop)
		{
			int rc = enet_host_service(h, &ev, 50);
			if (rc <= 0) continue;

			switch (ev.type)
			{
			case ENET_EVENT_TYPE_CONNECT:
				// pending until the hello packet delivers SClientConnectData
				ev.peer->data = (void*)(uintptr_t)0;
				Msg("- XRNET(enet): incoming connection, awaiting hello");
				break;

			case ENET_EVENT_TYPE_RECEIVE:
				{
					const void* data = ev.packet->data;
					u32 size = (u32)ev.packet->dataLength;
					u32 id = (u32)(uintptr_t)ev.peer->data;

					if (!id)
					{
						// expect hello
						if (size == sizeof(hello_packet) + sizeof(SClientConnectData))
						{
							const hello_packet* hp = (const hello_packet*)data;
							if (hp->sign1 == hello_sign1 && hp->sign2 == hello_sign2)
							{
								SClientConnectData cl_data = *(const SClientConnectData*)((const u8*)data + sizeof(hello_packet));
								id = next_id++;
								ev.peer->data = (void*)(uintptr_t)id;
								cl_data.clientID.set(id);
								Msg("- XRNET(enet): hello from '%s' -> client id %d", cl_data.name, id);
								m_owner->new_client(&cl_data);
							}
						}
						enet_packet_destroy(ev.packet);
						break;
					}

					MSYS_PING* ping = (MSYS_PING*)data;
					if (size == sizeof(MSYS_PING) && ping->sign1 == 0x12071980 && ping->sign2 == 0x26111975)
					{
						// clock sync: stamp + reply (doc §4.2)
						ping->dwTime_Server = TimerAsync(m_owner->device_timer);
						ENetPacket* pkt = enet_packet_create(ping, sizeof(MSYS_PING), 0);
						if (pkt) enet_peer_send(ev.peer, 1, pkt);
					}
					else
					{
						m_owner->RecievePacket(data, size, id);
					}
					enet_packet_destroy(ev.packet);
				}
				break;

			case ENET_EVENT_TYPE_DISCONNECT:
				{
					u32 id = (u32)(uintptr_t)ev.peer->data;
					Msg("- XRNET(enet): client id %d disconnected", id);
					if (id)
					{
						IClient* c = m_owner->net_players.GetFoundClient(
							ClientIdSearchPredicate(ClientID(id)));
						if (c)
						{
							c->flags.bConnected = FALSE;
							c->flags.bReconnect = FALSE;
							m_owner->OnCL_Disconnected(c);
							m_owner->client_Destroy(c);
						}
					}
					ev.peer->data = nullptr;
				}
				break;

			default:
				break;
			}
		}
	}

	//======================================================================
	// client
	//======================================================================

	client_transport::client_transport(IPureClient* owner)
		: m_owner(owner), m_host(nullptr), m_peer(nullptr), m_stop(false), m_thread_up(false)
	{
	}

	client_transport::~client_transport()
	{
		stop();
	}

	bool client_transport::connect(const char* address, u32 port, const SClientConnectData& cl_data)
	{
		if (!enet_ref()) return false;

		ENetHost* h = enet_host_create(nullptr, 1, 2, 0, 0);
		if (!h)
		{
			Msg("! XRNET(enet): client host create FAILED");
			enet_unref();
			return false;
		}

		ENetAddress addr;
		if (enet_address_set_host(&addr, address) != 0)
		{
			Msg("! XRNET(enet): cannot resolve '%s'", address);
			enet_host_destroy(h);
			enet_unref();
			return false;
		}
		addr.port = (enet_uint16)port;

		ENetPeer* p = enet_host_connect(h, &addr, 2, 0);
		if (!p)
		{
			Msg("! XRNET(enet): connect() failed");
			enet_host_destroy(h);
			enet_unref();
			return false;
		}

		// wait for the connect to complete (5s)
		ENetEvent ev;
		if (enet_host_service(h, &ev, 5000) <= 0 || ev.type != ENET_EVENT_TYPE_CONNECT)
		{
			Msg("! XRNET(enet): connection to %s:%d timed out", address, port);
			enet_peer_reset(p);
			enet_host_destroy(h);
			enet_unref();
			return false;
		}

		m_host = h;
		m_peer = p;

		// hello: deliver SClientConnectData (DirectPlay's "client info")
		u8 buf[sizeof(hello_packet) + sizeof(SClientConnectData)];
		hello_packet* hp = (hello_packet*)buf;
		hp->sign1 = hello_sign1;
		hp->sign2 = hello_sign2;
		memcpy(buf + sizeof(hello_packet), &cl_data, sizeof(SClientConnectData));
		ENetPacket* pkt = enet_packet_create(buf, sizeof(buf), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(p, 0, pkt);
		enet_host_flush(h);

		Msg("- XRNET(enet): connected to %s:%d as '%s'", address, port, cl_data.name);

		m_stop = false;
		thread_spawn(pump_thread, "xrnet-enet-cl", 0, this);
		m_thread_up = true;
		return true;
	}

	void client_transport::stop()
	{
		if (!m_host) return;
		m_stop = true;
		for (u32 i = 0; i < 50 && m_thread_up; ++i) Sleep(10);
		if (m_peer) enet_peer_disconnect_now((ENetPeer*)m_peer, 0);
		enet_host_destroy((ENetHost*)m_host);
		m_host = nullptr;
		m_peer = nullptr;
		enet_unref();
	}

	void client_transport::send(void* data, u32 size, u32 dpnsend_flags)
	{
		if (!m_host || !m_peer) return;
		u8 channel; u32 pflags;
		flags_to_enet(dpnsend_flags, channel, pflags);
		ENetPacket* pkt = enet_packet_create(data, size, pflags);
		if (pkt) enet_peer_send((ENetPeer*)m_peer, channel, pkt);
	}

	void client_transport::pump_thread(void* self)
	{
		((client_transport*)self)->pump();
		((client_transport*)self)->m_thread_up = false;
	}

	void client_transport::pump()
	{
		ENetHost* h = (ENetHost*)m_host;
		ENetEvent ev;

		while (!m_stop)
		{
			int rc = enet_host_service(h, &ev, 50);
			if (rc <= 0) continue;

			switch (ev.type)
			{
			case ENET_EVENT_TYPE_RECEIVE:
				// _Recieve handles MSYS_PING replies, MSYS_CONFIG (the
				// "connection completed" handshake) and game messages —
				// the exact same entry DirectPlay's RECEIVE fed
				m_owner->RecievePacket(ev.packet->data, (u32)ev.packet->dataLength, 0);
				enet_packet_destroy(ev.packet);
				break;

			case ENET_EVENT_TYPE_DISCONNECT:
				Msg("! XRNET(enet): server closed the connection");
				m_owner->OnSessionTerminate("xrnet_udp: disconnected");
				break;

			default:
				break;
			}
		}
	}
}
