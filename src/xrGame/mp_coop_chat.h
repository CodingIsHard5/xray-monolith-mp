////////////////////////////////////////////////////////////////////////////
//  mp_coop_chat.h — design doc §13.4 PDA zone chat: zone-wide player text, routed through the server,
//  broadcast to all. The pure part (sanitise, rate limit) is header-only so it can be tested natively
//  (dev/harness/native/test_coop_chat.cpp, COOP_CHAT_PURE_ONLY).
//
//  Wire: client -> server M_XRNET_COOP_REQUEST kind 2 + stringZ text. Server -> ALL M_XRNET_COOP_CHAT
//  u16 sender entity (0xffff = none), stringZ sender name, stringZ text. The name is the server's record of
//  the connection, never read from the payload, so a client cannot speak as someone else.
////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstddef>

enum
{
	COOP_CHAT_MAX_BYTES   = 200,     // after sanitising; longer lines are cut, not refused
	COOP_CHAT_WINDOW_MS   = 10000,
	COOP_CHAT_PER_WINDOW  = 5,
	COOP_CHAT_REQUEST_KIND = 2,
	COOP_CONSENT_REQUEST_KIND = 3,
	COOP_QUOTE_REQUEST_KIND = 4,     // §10.3 S2c: u16 trader id (quote me this trader's prices)   // §10.3 item 4: u32 request id, u8 yes (the holder's answer)
};

// Control characters (newlines included) become spaces, runs of spaces collapse to one, the ends are
// trimmed, and the result is cut at COOP_CHAT_MAX_BYTES (and at cap-1). Returns the output length;
// 0 means there is nothing to say. `in` may be NULL.
inline size_t coop_chat_sanitize(const char* in, char* out, size_t cap)
{
	if (!out || cap == 0)
		return 0;
	size_t n = 0;
	const size_t limit = (cap - 1 < size_t(COOP_CHAT_MAX_BYTES)) ? cap - 1 : size_t(COOP_CHAT_MAX_BYTES);
	bool pending_space = false;
	for (const char* p = in; p && *p && n < limit; ++p)
	{
		const unsigned char c = (unsigned char)*p;
		const bool space = (c < 0x20 || c == 0x7f || c == ' ');
		if (space)
		{
			pending_space = (n > 0);
			continue;
		}
		if (pending_space)
		{
			if (n + 1 >= limit)
				break;
			out[n++] = ' ';
			pending_space = false;
		}
		out[n++] = char(c);
	}
	out[n] = 0;
	return n;
}

// Fixed-window limiter, one per connection: at most COOP_CHAT_PER_WINDOW lines per COOP_CHAT_WINDOW_MS.
struct coop_chat_bucket
{
	unsigned window_start_ms;
	unsigned count;
	bool started;
	coop_chat_bucket() : window_start_ms(0), count(0), started(false) {}
};

inline bool coop_chat_allow(coop_chat_bucket& b, unsigned now_ms)
{
	// unsigned subtraction also handles the 49-day wrap of a millisecond clock
	if (!b.started || (now_ms - b.window_start_ms) >= unsigned(COOP_CHAT_WINDOW_MS))
	{
		b.started = true;
		b.window_start_ms = now_ms;
		b.count = 0;
	}
	if (b.count >= unsigned(COOP_CHAT_PER_WINDOW))
		return false;
	++b.count;
	return true;
}
