#pragma once
// MP fork (design doc §3.4, pre-flip step 4, Overseer-approved): the co-op player-body features ship DEFAULT ON on the co-op server, each with
// an _off switch as its control arm:
//   -coop_player_proxy_off       redesign (A): one-way health sync, single hit delivery, server-revive reset, CSE health writeback,
//                                kill guarantees, the actor_on_before_hit quarantine and the per-victim balancer (gamedata)
//   -coop_player_posfeed_off     the server's copy of a player follows its player (IMPLIED by the proxy: off only with the proxy off too)
//   -coop_saveactor_exclude_off  the server's save actor (object 0) out of perception, enemy selection and hits
//   -coop_player_community_off   §3.4 scope 2 (Overseer-confirmed flip steps 1-3): player bodies get actor_stalker instead of plain "actor"
// Token-exact matching (a flag is followed by the end or a space), so an _off flag never reads as its feature's name and no feature
// reads a longer flag. The previous opt-in flags (-coop_player_proxy, ...) are no longer read: the features are on unless switched off.
#include <cstring>
inline bool coop_token_present(const char* flag)
{
	const size_t n = strlen(flag);
	const char* p = Core.Params;
	while ((p = strstr(p, flag)) != nullptr)
	{
		const char a = p[n];
		if (!a || a == ' ' || a == '\t') return true;
		p += n;
	}
	return false;
}
inline bool coop_player_proxy_on()
{
	static int s = -1;
	if (s < 0) s = coop_token_present("-coop_player_proxy_off") ? 0 : 1;   // plain literal: the App. B key drift check reads flags from source literals
	return s == 1;
}
inline bool coop_player_posfeed_on()
{
	static int s = -1;
	if (s < 0) s = (coop_token_present("-coop_player_posfeed_off") && !coop_player_proxy_on()) ? 0 : 1;   // plain literal: App. B key drift check
	return s == 1;
}
inline bool coop_saveactor_exclude_on()
{
	static int s = -1;
	if (s < 0) s = coop_token_present("-coop_saveactor_exclude_off") ? 0 : 1;   // plain literal: App. B key drift check
	return s == 1;
}
inline bool coop_player_community_on()
{
	static int s = -1;
	if (s < 0) s = coop_token_present("-coop_player_community_off") ? 0 : 1;   // plain literal: App. B key drift check
	return s == 1;
}
