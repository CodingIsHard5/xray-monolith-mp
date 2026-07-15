////////////////////////////////////////////////////////////////////////////
// MP fork (step 4 / dev/CLOCK_NETCODE_PLAN.md, N0'): ENet-based UDP
// transport replacing DirectPlay, which cannot host under wine (and is
// a 2004 relic besides). Opt-in via -xrnet_udp on BOTH sides; without
// the flag the DirectPlay path is byte-identical.
//
// Mapping onto the engine's existing machinery:
//  - every received game packet -> owner->RecievePacket(...) (the same
//    entry the DPN_MSGID_RECEIVE handler feeds)
//  - MSYS_PING requests are stamped+replied inline server-side (clock
//    sync, doc §4.2); replies flow to the client's _Recieve as normal
//  - connect carries SClientConnectData in a hello packet (DirectPlay
//    carried it as "client info"); the server calls new_client() once
//    the hello arrives
//  - reliable sends -> channel 0 (ENET_PACKET_FLAG_RELIABLE),
//    everything else -> channel 1 (sequenced-unreliable)
////////////////////////////////////////////////////////////////////////////
#pragma once

class IPureServer;
class IPureClient;
class xrCriticalSection;
struct SClientConnectData;

namespace xr_enet
{
	bool enabled(); // -xrnet_udp present

	static const u32 hello_sign1 = 0x4D50484C; // 'MPHL'
	static const u32 hello_sign2 = 0x4F480001;

	struct hello_packet
	{
		u32 sign1;
		u32 sign2;
		// SClientConnectData payload follows (name/pass/process_id)
	};

	class server_transport
	{
	public:
		server_transport(IPureServer* owner);
		~server_transport();

		bool host(u32 port, u32 max_players);
		void stop();
		void send_to(u32 client_id, void* data, u32 size, u32 dpnsend_flags);
		void kick(u32 client_id);
		bool running() const { return m_host != nullptr; }
		bool owns(u32 client_id) const; // is this id one of our ENet peers

	private:
		static void pump_thread(void* self);
		void pump();

		IPureServer* m_owner;
		void* m_host; // ENetHost*
		xrCriticalSection* m_lock; // serializes ENetHost access (not thread-safe)
		volatile bool m_stop;
		bool m_thread_up;
	};

	class client_transport
	{
	public:
		client_transport(IPureClient* owner);
		~client_transport();

		bool connect(const char* address, u32 port, const SClientConnectData& cl_data);
		void stop();
		void send(void* data, u32 size, u32 dpnsend_flags);
		bool running() const { return m_host != nullptr; }

	private:
		static void pump_thread(void* self);
		void pump();

		IPureClient* m_owner;
		void* m_host; // ENetHost*
		void* m_peer; // ENetPeer*
		xrCriticalSection* m_lock; // serializes ENetHost access (not thread-safe)
		volatile bool m_stop;
		bool m_thread_up;
	};
}
