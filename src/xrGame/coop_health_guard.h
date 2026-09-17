#pragma once
// MP fork (design doc §3.4, Overseer after PVB2): a SCRIPT health raise on a dead claimed player body, on the dedicated server, is refused.
// The respawn path revives a body in C++ (GAME_EVENT_COOP_RESPAWN -> SetfHealth) and is untouched; a script heal (GAMMA's scripts act on
// db.actor, one player's body) must not lift a dead server body's health outside it — PVB2's harness did exactly that and the body read
// 1.00 on the server while its client was dead. Pure, so dev/harness/test_coop_health_guard_offline.cpp compiles and checks it on Linux.
inline bool coop_should_refuse_health_raise(bool on_dedicated_server, bool claimed_player_body, bool dead, float before, float after)
{
	return on_dedicated_server && claimed_player_body && dead && after > before;
}
