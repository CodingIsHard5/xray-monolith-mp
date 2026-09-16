#pragma once
// MP fork (design doc §3.4, object-0 scope): on the dedicated co-op server the SAVE ACTOR (object 0 — the server's Actor(), standing at
// the spawn) is a real, targetable body that no player drives. With -coop_saveactor_exclude it is taken out of every creature's and
// NPC's perception and enemy selection, and hits on it are refused. It is NOT moved and NOT deleted: every single-actor script and
// the server's own Actor() read it. Defined in game_sv_single.cpp; true only for the excluded id while the flag is on.
bool coop_ghost_excluded(u16 id);
