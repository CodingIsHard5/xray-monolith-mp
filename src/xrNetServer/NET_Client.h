#pragma once

#include "net_shared.h"
#include "NET_Common.h"

struct ip_address;

class XRNETSERVER_API INetQueue
{
	xrCriticalSection cs;
	xr_deque<NET_Packet*> ready;
	xr_vector<NET_Packet*> unused;
public:
	INetQueue();
	~INetQueue();

	NET_Packet* Create();
	NET_Packet* Create(const NET_Packet& _other);
	NET_Packet* Retreive();
	void Release();
	inline void Lock() { cs.Enter(); };
	inline void Unlock() { cs.Leave(); };
};


//==============================================================================

namespace xr_enet { class client_transport; }

class XRNETSERVER_API
	IPureClient
	: private MultipacketReciever,
	  private MultipacketSender
{
	enum ConnectionState
	{
		EnmConnectionFails=0,
		EnmConnectionWait=-1,
		EnmConnectionCompleted=1
	};

	friend void sync_thread(void*);
protected:
	struct HOST_NODE //deprecated...
	{
		DPN_APPLICATION_DESC dpAppDesc;
		IDirectPlay8Address* pHostAddress;
		shared_str dpSessionName;
	};

	GameDescriptionData m_game_description;
	CTimer* device_timer;

	// MP fork: opt-in ENet transport (-xrnet_udp); null when disabled
	friend class xr_enet::client_transport;
	xr_enet::client_transport* m_enet;
protected:
	IDirectPlay8Client* NET;
	IDirectPlay8Address* net_Address_device;
	IDirectPlay8Address* net_Address_server;

	xrCriticalSection net_csEnumeration;
	xr_vector<HOST_NODE> net_Hosts;

	NET_Compressor net_Compressor;

	ConnectionState net_Connected;
	BOOL net_Syncronised;
	BOOL net_Disconnected;

	INetQueue net_Queue;
	IClientStatistic net_Statistic;

	u32 net_Time_LastUpdate;
	s32 net_TimeDelta;
	s32 net_TimeDelta_Calculated;
	s32 net_TimeDelta_User;
	// MP fork (bug 3): server-time offset observed from the update stream itself. The ping sync above came out
	// ~17.5 s LOW on the co-op client, stable for the whole session (StalkerMPMod npc-anim-clock1: the server's
	// clocks agree with each other, the client's CLOCK_SYNC delta is 40.77 s, packet stamps say 58.3 s), and
	// every buffered-position consumer played the world that far in the past. A packet cannot arrive before
	// it was stamped, so (stamp - receive time) is a lower bound on the true offset. Its maximum over a short
	// window is within one transit time of it. 0 means nothing has been observed yet.
	s32 net_TimeDelta_Stream = 0;
	s32 net_StreamWinMax = 0;
	u32 net_StreamWinStart = 0;
	bool net_StreamWinAny = false;

	void Sync_Thread();
	void Sync_Average();

	void SetClientID(ClientID const& local_client) { net_ClientID = local_client; };


	IC virtual void SendTo_LL(void* data, u32 size, u32 dwFlags = DPNSEND_GUARANTEED, u32 dwTimeout = 0);

public:
	IPureClient(CTimer* tm);
	virtual ~IPureClient();
	HRESULT net_Handler(u32 dwMessageType, PVOID pMessage);

	BOOL Connect(LPCSTR server_name);
	void Disconnect();

	void net_Syncronize();
	BOOL net_isCompleted_Connect() { return net_Connected == EnmConnectionCompleted; }
	BOOL net_isFails_Connect() { return net_Connected == EnmConnectionFails; }
	BOOL net_isCompleted_Sync() { return net_Syncronised; }
	BOOL net_isDisconnected() { return net_Disconnected; }
	IC GameDescriptionData const& get_net_DescriptionData() const { return m_game_description; }
	LPCSTR net_SessionName() { return *(net_Hosts.front().dpSessionName); }

	// receive
	IC void StartProcessQueue() { net_Queue.Lock(); }; // WARNING ! after Start mast be End !!! <-
	IC virtual NET_Packet* net_msg_Retreive() { return net_Queue.Retreive(); }; //							|
	IC void net_msg_Release() { net_Queue.Release(); }; //							|
	IC void EndProcessQueue() { net_Queue.Unlock(); }; //							<-

	// send
	virtual void Send(NET_Packet& P, u32 dwFlags = DPNSEND_GUARANTEED, u32 dwTimeout = 0);
	virtual void Flush_Send_Buffer();
	virtual void OnMessage(void* data, u32 size);

	virtual void OnInvalidHost()
	{
	};

	virtual void OnInvalidPassword()
	{
	};

	virtual void OnSessionFull()
	{
	};

	virtual void OnConnectRejected()
	{
	};
	BOOL net_HasBandwidth();
	void ClearStatistic();
	IClientStatistic& GetStatistic() { return net_Statistic; }
	void UpdateStatistic();
	ClientID const& GetClientID() { return net_ClientID; };

	bool GetServerAddress(ip_address& pAddress, DWORD* pPort);

	// time management
	IC s32 timeServer_EffectiveDelta() const
	{
		return (net_TimeDelta_Stream && (net_TimeDelta_Stream > net_TimeDelta)) ? net_TimeDelta_Stream : net_TimeDelta;
	}
	IC u32 timeServer() { return TimeGlobal(device_timer) + timeServer_EffectiveDelta() + net_TimeDelta_User; }
	IC u32 timeServer_Async() { return TimerAsync(device_timer) + timeServer_EffectiveDelta() + net_TimeDelta_User; }
	IC u32 timeServer_Delta() { return net_TimeDelta; }
	IC s32 timeServer_StreamDelta() const { return net_TimeDelta_Stream; }
	// MP fork (bug 3): feed a server-stamped update's timestamp. Game thread only (creature net_Import).
	IC void timeServer_Observe(u32 server_stamp)
	{
		const u32 now = TimerAsync(device_timer);
		const s32 cand = s32(server_stamp - now);
		if (!net_StreamWinAny)
		{
			net_StreamWinAny = true;
			net_StreamWinStart = now;
			net_StreamWinMax = cand;
			return;
		}
		if (cand > net_StreamWinMax)
			net_StreamWinMax = cand;
		if ((now - net_StreamWinStart) >= 2000)
		{
			net_TimeDelta_Stream = net_StreamWinMax ? net_StreamWinMax : 1; // 0 is the "unset" sentinel
			net_StreamWinStart = now;
			net_StreamWinMax = cand;
		}
	}
	IC void timeServer_UserDelta(s32 d) { net_TimeDelta_User = d; }
	IC void timeServer_Correct(u32 sv_time, u32 cl_time);

	virtual BOOL net_IsSyncronised();

	virtual LPCSTR GetMsgId2Name(u16 ID) { return ""; }

	virtual void OnSessionTerminate(LPCSTR reason)
	{
	};

	virtual bool TestLoadBEClient() { return false; }

private:
	ClientID net_ClientID;

	virtual void _Recieve(const void* data, u32 data_size, u32 param);
	virtual void _SendTo_LL(const void* data, u32 size, u32 flags, u32 timeout);
};
