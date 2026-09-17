////////////////////////////////////////////////////////////////////////////
//  coop_server_config_keys.h — design doc Appendix B: every flag the co-op fork reads, in one table.
//  Generated from a sweep of the engine source (2026-09-13) and kept honest by
//  dev/harness/test_coop_config_keys_drift.sh, which fails when a flag literal in the source is missing here
//  or a key here is read nowhere. Kind: switch (presence) or value. Class: see coop_server_config.h.
//  Columns in the comment: default when absent | side | what it does.
////////////////////////////////////////////////////////////////////////////
#pragma once

static const coop_cfg_key coop_server_config_keys[] = {
	// ---- early (read before the filesystem)
	{"coop_addrmap", coop_cfg_value, coop_cfg_early},   // off | server | Arms the allocation address map for blocks >= N bytes right after InitLog so boot allocations are visible.
	{"coop_addrmap_census", coop_cfg_value, coop_cfg_early},   // 0 | server | Arena census parameter for -coop_addrmap.
	{"coop_addrmap_ms", coop_cfg_value, coop_cfg_early},   // 0 (instrument default) | server | Report interval for -coop_addrmap.
	{"coop_addrmap_top", coop_cfg_value, coop_cfg_early},   // 0 (instrument default) | server | Top-N entries reported by -coop_addrmap.
	{"coop_config", coop_cfg_value, coop_cfg_early},   // $app_data_root$/coop_server.ltx | both | Path of this config file (read before the file, so command line only).
	{"coop_pool", coop_cfg_value, coop_cfg_early},   // off | server | Arms the luabind allocation pool of N MiB, enabled only if its selftest passes.
	// ---- tunable
	{"coop_autosave", coop_cfg_value, coop_cfg_tunable},   // 120 (0 disables; clamped 1..86400) | server | Server-authoritative world autosave interval in seconds (Appendix B); overridden by -coop_test_autosave.
	{"coop_checkpoint_anywhere", coop_cfg_switch, coop_cfg_tunable},   // off | server | Lets players bank a checkpoint anywhere, disabling the campfire-proximity requirement.
	{"coop_checkpoint_radius", coop_cfg_value, coop_cfg_tunable},   // 8 (accepted 0<v<=500) | server | Max distance to a campfire for a player checkpoint request (flag literal includes trailing space).
	{"coop_correction", coop_cfg_switch, coop_cfg_tunable},   // off | client | Enables smooth position correction of locally-driven monster puppets toward the server's authoritative samples (exact-token match).
	{"coop_cull_radius", coop_cfg_value, coop_cfg_tunable},   // off; 150 if flag given with non-positive value | server | Relevance culling: stream/spawn creatures to a client only within this radius of a player (1.33x hysteresis out); presence also gates the stock creature-spawn broadcast.
	{"coop_ff", coop_cfg_value, coop_cfg_tunable},   // UNIVERSAL_ON | server | Global friendly-fire mode (UNIVERSAL_ON, UNIVERSAL_OFF, FRIENDLY_FACTIONS, OWN_FACTION_ONLY); per-zone overrides persist in the save.
	{"coop_npc_hz", coop_cfg_value, coop_cfg_tunable},   // -1 (off) | server | Caps every replicated creature's M_UPDATE to at most once per N ms (bandwidth throttle).
	{"coop_respawn_delay", coop_cfg_value, coop_cfg_tunable},   // 10 (min 1) | client | Spectate countdown before a dead co-op player respawns (Appendix B).
	{"coop_server_tick", coop_cfg_switch, coop_cfg_tunable},   // off | server | Enables the server-side gamedata decision-origination tick (mp_coop_decision_server.script).
	{"coop_update_throttle", coop_cfg_value, coop_cfg_tunable},   // -1 (off) | server | Caps M_UPDATE per decision-driven creature to once per N ms (soft-correction cadence; began as a probe).
	{"mp_host", coop_cfg_switch, coop_cfg_tunable},   // off | server | Makes the single/alife server open an ENet listen socket for remote co-op clients (requires -xrnet_udp).
	{"mp_noauth", coop_cfg_switch, coop_cfg_tunable},   // off | both | Skips the file-CRC auth: server ignores the digest mismatch for non-local clients, client skips auth_generate (Level_network.cpp:408).
	{"mp_port", coop_cfg_value, coop_cfg_tunable},   // server START_PORT_LAN_SV or options portsv=; client psSV_Port from port= | both | ENet UDP port for the listen socket (server) and connect target (client, NET_Client.cpp:444); literal has trailing space.
	{"mp_trusted", coop_cfg_switch, coop_cfg_tunable},   // off | server | Accepts a client level-geometry checksum mismatch anyway (trusted-peer debugging only, not a default).
	{"xrnet_udp", coop_cfg_switch, coop_cfg_tunable},   // off (DirectPlay/stock transport) | both | Selects the ENet UDP transport; xr_enet::enabled() gates almost every co-op path.
	// ---- control
	{"coop_ownership_allow_double", coop_cfg_switch, coop_cfg_control},   // off | server | Lets a second take reparent an item that already has a parent (removes the §10.5 claim guard; control only).
	{"coop_no_server_drive", coop_cfg_switch, coop_cfg_control},   // off | server | Gamedata mp_coop_decision_server: the server tick does not drive decisions (control).
	{"coop_emission_stock", coop_cfg_switch, coop_cfg_control},   // off | server | Gamedata mp_coop_emission: surges judge the save actor only, as before §10.2.
	{"coop_allow_time_skip", coop_cfg_switch, coop_cfg_control},   // off | server | Lets scripts on the co-op server skip world time (change_game_time); the fix refuses it (§10.1/§10.6).
	{"coop_break_inventory", coop_cfg_switch, coop_cfg_control},   // off | server | Negative control: orphan reclaim succeeds but the returning player's items are dropped (run must FAIL); exact-token match.
	{"coop_break_reclaim", coop_cfg_switch, coop_cfg_control},   // off | server | Negative control: disables orphan lookup so every returning player gets a fresh spawn (run must FAIL); exact-token match.
	{"coop_clone_from_graph_actor", coop_cfg_switch, coop_cfg_control},   // off | server | Restores the old new-player clone source (graph().actor(), i.e. the last spawned player) instead of the host save actor.
	{"coop_empty_keeps_actor_region", coop_cfg_switch, coop_cfg_control},   // off | server | Restores pre-§5.2 behaviour: an emptied region keeps the save actor's region live instead of falling back to offline simulation.
	{"coop_no_corpse_pile", coop_cfg_switch, coop_cfg_control},   // off | server | Restores pre-§9.2 rollback: death destroys carried items and restores the bank, no corpse pile of gains.
	{"coop_no_empty_save", coop_cfg_switch, coop_cfg_control},   // off | server | Disables the world save when the last player leaves.
	{"coop_no_ik", coop_cfg_switch, coop_cfg_control},   // off | server | Suppresses IK controller creation on the dedicated server (§8 treatment arm for IK/animation faults; one binary one bit apart).
	{"coop_no_spawn_guard", coop_cfg_switch, coop_cfg_control},   // off | server | Disables the fresh-spawn relocation guard (level-changer/nav-mesh check) and places players at the wanted position.
	{"coop_no_time_halt", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: world time keeps running with no players (edges still logged) instead of halting.
	{"coop_npc_cap", coop_cfg_value, coop_cfg_tunable},   // 24 | server | §16.3: at most <n> non-story NPCs within the radius of players run at the full think rate; the rest are throttled.
	{"coop_npc_cap_disable", coop_cfg_switch, coop_cfg_tunable},   // off | server | §16.3: turn the NPC cap off entirely (stock camera-distance think rates, i.e. every NPC near players at the slowest rate on a dedicated server).
	{"coop_npc_cap_radius", coop_cfg_value, coop_cfg_tunable},   // 150 | server | §16.3: the radius (m) around players the NPC cap considers.
	{"coop_tele_local", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: a telekinesis-thrown object's physics is not exported; each client simulates it alone (§3.4 inc 2).
	{"coop_cloak_local", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: the server does not send mutant cloak state; each client computes it locally (§3.4).
	{"coop_npc_cap_off", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: the NPC cap classifies and measures but does not throttle (§16.3).
	{"coop_trade_schedule_noload", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: the restock schedule ignores its saved per-trader record at boot (§10.3 S3b persistence).
	{"coop_trade_no_schedule", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: the server does not run the shared-trader restock schedule (§10.3 S3b).
	{"coop_drop_guard_off", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: a client may drop an item out of another player's inventory (§10.3 gap 4c); exact-token match.
	{"coop_trade_no_quotes", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: the server does not answer a client's price-quote request, so the trade UI shows local prices (§10.3 S2c).
	{"coop_consent_off", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: a client's take out of another player's inventory is not asked about (§10.3 item 4).
	{"coop_consent_timeout", coop_cfg_value, coop_cfg_tunable},   // 15 | server | Seconds a player has to answer another player's ask for an item they hold; no answer is a no (§10.3 item 4).
	{"coop_money_client_authority", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: clients write their own money (GE_MONEY accepted, no server price or balance check) (§10.3 S2b); exact-token match.
	{"coop_trade_no_authority", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: a player's take from a trader's stock is not checked against their standing (§10.3 S2a); exact-token match.
	{"coop_orphan_hit_allow", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: hits on reserved (orphaned) player bodies are not refused; exact-token match.
	{"coop_test_widelock", coop_cfg_switch, coop_cfg_control},   // off | server | Restores the old wide delayed-packet lock held across the Lua handler (A/B vs the pop-a-copy fix).
	// ---- diag
	{"coop_inv_census", coop_cfg_switch, coop_cfg_diag},   // off | server | Gamedata mp_coop_decision_server: inventory census logging.
	{"coop_animdiag", coop_cfg_switch, coop_cfg_diag},   // off | both | Bug-3 animation/clock diagnostics: COOP_CLOCK every 10 s and COOP_ANIMX flag-change lines (ai_stalker.cpp:1014).
	{"coop_correction_debug", coop_cfg_switch, coop_cfg_diag},   // off | client | Per-call witness logging for -coop_correction (exact-token match).
	{"coop_correction_probe", coop_cfg_switch, coop_cfg_diag},   // off | client | Logs correction source candidates (NET.back vs NET_Last) and their age for locally-driven puppets; applies nothing.
	{"coop_dbg_online_count", coop_cfg_switch, coop_cfg_diag},   // off | server | Logs the number of online A-Life objects every 5 s.
	{"coop_dbg_switch", coop_cfg_switch, coop_cfg_diag},   // off | server | [MPSW] per-object online/offline switch trace (very high log volume).
	{"coop_local_ai", coop_cfg_switch, coop_cfg_diag},   // off | client | Logs the locally-driven monster movement pipeline (C1b) and decision-stream expiry; logging only.
	{"coop_npcdeath", coop_cfg_switch, coop_cfg_diag},   // off | client | Client-side logs of NPC puppet health->0, GE_DIE receipt and net_Destroy (also Entity.cpp:75, ai_stalker.cpp:1113, CustomMonster.cpp:1580).
	{"coop_npcdiag", coop_cfg_switch, coop_cfg_diag},   // off | both | World-NPC replication diag: server per-pass creature gate counts and COOP_BW bandwidth (xrServer.cpp:938); client RX creature spawns (Level_network_spawn.cpp:39).
	{"coop_rpg_census", coop_cfg_value, coop_cfg_diag},   // off; 300 when given without a valid value | server | Calls gamedata mp_coop_rpg_census on a timer to derive the world-fact info-id classification.
	{"coop_sqsw", coop_cfg_switch, coop_cfg_diag},   // off | server | [SQSW] logs why anchored squads do or do not switch online.
	{"coop_vm_audit", coop_cfg_switch, coop_cfg_diag},   // off | both | Audits Lua VM touches from off the game thread (TEB thread-id check, self-disarms on mismatch).
	{"xrnet_facelog", coop_cfg_switch, coop_cfg_diag},   // off | both | Facing diagnostic: server export (XRNET face-sv) and client applied yaw (CustomMonster.cpp:1084), throttled.
	{"xrnet_trace", coop_cfg_switch, coop_cfg_diag},   // off | both | Traces ENet transport sends/receives and SendTo_LL routing (also xr_enet_transport.cpp:136,434 and NET_Server.cpp:650).
	// ---- instrument
	{"coop_allocsites", coop_cfg_value, coop_cfg_diag},   // off | server | Arms the per-size-class allocation histogram from N bytes.
	{"coop_allocsites_ms", coop_cfg_value, coop_cfg_diag},   // 0 (instrument default) | server | Report interval for -coop_allocsites.
	{"coop_bigalloc", coop_cfg_value, coop_cfg_diag},   // off | server | Arms big-allocation ring tracing for allocations >= N MB.
	{"coop_bigalloc_max", coop_cfg_value, coop_cfg_diag},   // 0 (no upper bound) | server | Optional upper bound of the -coop_bigalloc traced band.
	{"coop_rasites", coop_cfg_value, coop_cfg_diag},   // off | server | Net bytes per allocating return address inside a band starting at N bytes.
	{"coop_rasites_max", coop_cfg_value, coop_cfg_diag},   // 0 (no upper bound) | server | Upper bound of the -coop_rasites band.
	{"coop_rasites_ms", coop_cfg_value, coop_cfg_diag},   // 0 (instrument default) | server | Report interval for -coop_rasites.
	{"coop_rasites_top", coop_cfg_value, coop_cfg_diag},   // 0 (instrument default) | server | Top-N return addresses reported by -coop_rasites.
	{"coop_skin_release", coop_cfg_switch, coop_cfg_diag},   // off | server | Releases skinned vertex arrays at the end of AfterLoad; refused unless -coop_smem is also given.
	{"coop_smem", coop_cfg_value, coop_cfg_diag},   // off | server | Read-only shared-memory (smem_container) census every N ms.
	{"coop_smem_clean", coop_cfg_switch, coop_cfg_diag},   // off | server | Also calls smem_container::clean() during the census; refused unless -coop_smem is also given.
	// ---- test
	{"coop_test_trade_arm", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_trade_server: poll $app_data_root$/coop_trade_sv_arm for harness commands (goodwill, top-tier restock) (§10.3 S2a harness).
	{"coop_test_restock", coop_cfg_value, coop_cfg_test},   // off | server | Gamedata mp_coop_trade_server: restock esc_m_trader <s> seconds after boot (§10.3 harness).
	{"coop_test_claim_item", coop_cfg_value, coop_cfg_test},   // off | server | Spawns <section> beside the first player <s> seconds after they bind (§10.5 claim race harness).
	{"coop_test_psi_storm", coop_cfg_value, coop_cfg_test},   // off | server | Gamedata mp_coop_emission: start a psi storm <s> seconds after boot (§10.2 harness).
	{"coop_squad_move", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (squad move).
	{"coop_real_move", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (real move).
	{"coop_real_combat", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (real combat).
	{"coop_populate", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (populate).
	{"coop_patrol", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (patrol).
	{"coop_move_obj", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (move to object).
	{"coop_monster_combat", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (monster combat).
	{"coop_flee", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (flee).
	{"coop_enum_squads", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (enumerate squads).
	{"coop_dog", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (spawn a dog instead of a flesh).
	{"coop_decision_burst", coop_cfg_value, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (burst of N decisions).
	{"coop_combat", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (combat).
	{"coop_autolocal_n", coop_cfg_value, coop_cfg_test},   // off | server | Gamedata mp_coop_scenario_autolocal: how many creatures to hand to local AI.
	{"coop_autolocal", coop_cfg_switch, coop_cfg_test},   // off | server | Gamedata mp_coop_decision_server scenario mode (autolocal).
	{"coop_test_surge", coop_cfg_value, coop_cfg_test},   // off | server | Gamedata mp_coop_emission: start a surge <s> seconds after boot (§10.2 harness).
	{"coop_test_server_timeskip", coop_cfg_value, coop_cfg_test},   // off | server | Calls change_game_time(0, 5 h, 0) on the server <s> seconds after boot and logs the clock (§10.6 harness).
	{"coop_rep32_resume", coop_cfg_switch, coop_cfg_test},   // off | server | Resume leg (second boot) of -coop_test_rep32: reads restored pressure stamp instead of killing.
	{"coop_repro_gamelua", coop_cfg_switch, coop_cfg_test},   // off | server | Regression control: enters the Lua VM as bait from the game-thread decision tick (§3c reproduction).
	{"coop_repro_pumplua", coop_cfg_switch, coop_cfg_test},   // off | server | Regression control: enters the Lua VM from the ENet pump thread in OnMessage; must still kill the server.
	{"coop_test_autosave", coop_cfg_value, coop_cfg_test},   // off (non-finite -> 1 s; clamped 1..86400) | server | Harness fast-path autosave interval; takes precedence over -coop_autosave.
	{"coop_test_checkpoint", coop_cfg_value, coop_cfg_test},   // off; 30 when given without a valid value | server | Banks a checkpoint for every connected player once, N seconds after arming.
	{"coop_test_decision", coop_cfg_switch, coop_cfg_test},   // off | server | Broadcasts a synthetic decision (subject 0xC0DE, kind 200, 1 s lead) every 5 s until a real client receives it.
	{"coop_test_dialog", coop_cfg_value, coop_cfg_test},   // off; 30 when given without a valid value | client | Client probe that runs scripted dialogue phrase actions after N seconds (delimiter-checked).
	{"coop_test_dialog_step", coop_cfg_value, coop_cfg_test},   // 15 | client | Interval between successive phrases of the -coop_test_dialog probe.
	{"coop_test_disc_hold", coop_cfg_value, coop_cfg_test},   // 0 (off; accepted 0<ms<=120000) | server | Holds dialogue actions in the delayed-packet queue for N ms so a disconnect can land between queue and drain.
	{"coop_test_drop_banked", coop_cfg_switch, coop_cfg_test},   // off | server | After the test auto-bank, drops one banked item into the world beside each player.
	{"coop_test_ff_zone", coop_cfg_value, coop_cfg_test},   // off | server | Sets one per-level friendly-fire override after save load so it can be written into a save.
	{"coop_test_gain", coop_cfg_value, coop_cfg_test},   // off | server | After the test auto-bank, gives each player one GAIN item of this section (literal has trailing space).
	{"coop_test_hit", coop_cfg_value, coop_cfg_test},   // off; 20 when given without a valid value | client | Client fires hits at the first other actor after N seconds (delimiter-checked loop).
	{"coop_test_hit_count", coop_cfg_value, coop_cfg_test},   // 2 | client | Number of shots the -coop_test_hit probe fires (~0.5 s apart).
	{"coop_test_hit_idfile", coop_cfg_value, coop_cfg_test},   // off | client | File holding the target actor id for -coop_test_hit (read at fire time).
	{"coop_test_hit_target", coop_cfg_value, coop_cfg_test},   // off | client | Target name for the -coop_test_hit probe.
	{"coop_test_kill", coop_cfg_value, coop_cfg_test},   // off; 15 when given without a valid value | client | Thin client self-kills its own actor once after N seconds.
	{"coop_test_quest", coop_cfg_switch, coop_cfg_test},   // off | server | Creates and broadcasts a synthetic task 10 s after start (quest replication E2E).
	{"coop_test_quest3", coop_cfg_value, coop_cfg_test},   // off; 30 when given without a valid value | server | §7.2 claim-transaction probe with a synthetic second claimant.
	{"coop_test_quest3_replay", coop_cfg_value, coop_cfg_test},   // 0 (off) | server | Delay before replaying the target's death in the -coop_test_quest3 probe (exactly-once check).
	{"coop_test_quest3_verify", coop_cfg_value, coop_cfg_test},   // off; 30 when given without a valid value | server | Read-only survival probe for quest3 state after reload.
	{"coop_test_quest4", coop_cfg_value, coop_cfg_test},   // off; 30 when given without a valid value | server | Quest condition probe (Q4).
	{"coop_test_quest4_replay", coop_cfg_value, coop_cfg_test},   // 0 (off) | server | Delay of the forced tick in the -coop_test_quest4 probe.
	{"coop_test_quest5", coop_cfg_value, coop_cfg_test},   // off; 30 when given without a valid value | server | Dialogue/fetch quest probe.
	{"coop_test_quest5_item", coop_cfg_value, coop_cfg_test},   // conserva | server | Fetch item section for the -coop_test_quest5 probe.
	{"coop_test_quest5_replay", coop_cfg_value, coop_cfg_test},   // 0 (off) | server | Delay of the verify step in the -coop_test_quest5 probe.
	{"coop_test_quest6", coop_cfg_value, coop_cfg_test},   // off; 30 when given without a valid value | server | §7.5 escort-target probe.
	{"coop_test_race", coop_cfg_value, coop_cfg_test},   // off; server 60 / client 60 when given without a valid value | both | §7.2 two-claimant race: server sets up tasks and broadcasts the start; client (Actor.cpp:1792) lands its control claim after N s.
	{"coop_test_race_grace", coop_cfg_value, coop_cfg_test},   // 10 (accepted 0<v<=300) | server | Verdict grace window for the race probe (lets held claims drain).
	{"coop_test_race_lead", coop_cfg_value, coop_cfg_test},   // 3000 (accepted 0<v<=60000) | server | Lead time between the race start broadcast and the verdict.
	{"coop_test_race_side", coop_cfg_value, coop_cfg_test},   // a | client | Which race side ('a' or 'b') this client plays.
	{"coop_test_race_wait", coop_cfg_value, coop_cfg_test},   // 240 (accepted 0<v<=3600) | server | Per-stage wait bound for the race probe before a harness give-up.
	{"coop_test_rep1", coop_cfg_value, coop_cfg_test},   // off; 25 when given without a valid value | server | R1 write pass: stamps a distinctive personal standing and faction relation to test persistence.
	{"coop_test_rep1_verify", coop_cfg_switch, coop_cfg_test},   // off | server | R1 read-only verify pass (delay 25 s unless -coop_test_rep1 also given).
	{"coop_test_rep2", coop_cfg_value, coop_cfg_test},   // off; 25 when given without a valid value | server | R2 reputation routing write probe.
	{"coop_test_rep2_badver", coop_cfg_switch, coop_cfg_test},   // off | server | Writes a future (0xFFFF) reputation-state version so the next boot must refuse it.
	{"coop_test_rep2_verify", coop_cfg_value, coop_cfg_test},   // off; 25 when given without a valid value | server | R2 read-only verify pass.
	{"coop_test_rep3", coop_cfg_value, coop_cfg_test},   // off; 30 when given without a valid value | server | R3 kill-attribution reputation probe with a seeded bystander.
	{"coop_test_rep31", coop_cfg_value, coop_cfg_test},   // off; 30 when given without a valid value | server | R3.1 collective reputation falloff/threshold probe.
	// ---- §3.4 co-op player bodies (registered 2026-09-17 with the default-ON flip; the drift check had 17 unregistered flags)
	{"coop_player_proxy_off", coop_cfg_switch, coop_cfg_control},   // off (the proxy is ON) | server | Control arm: disables redesign (A) — health sync, single hit delivery, server-revive reset, CSE health writeback, kill guarantees, the before-hit quarantine and the per-victim balancer.
	{"coop_player_posfeed_off", coop_cfg_switch, coop_cfg_control},   // off (the feed is ON) | server | Control arm: the server's copy of a player no longer follows its player; honoured only with -coop_player_proxy_off (the proxy implies the feed).
	{"coop_saveactor_exclude_off", coop_cfg_switch, coop_cfg_control},   // off (the exclusion is ON) | server | Control arm: the server's save actor (object 0) is perceivable, targetable and hittable again.
	{"coop_player_balancer_off", coop_cfg_switch, coop_cfg_control},   // off (the balancer is ON) | server | Gamedata control arm: GAMMA's damage balancer stays quarantined instead of running for each hit's victim.
	{"coop_balancer_noswap", coop_cfg_switch, coop_cfg_control},   // off | server | Gamedata control arm: the per-victim balancer swaps db.actor only, not the balancer's per-actor state (shows the cross-player coupling).
	{"coop_hpsync_noadopt", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: the first health sync of a connection does not adopt the claim-restored CSE health (returning-player check).
	{"coop_bodylog", coop_cfg_switch, coop_cfg_diag},   // off | server | Player server-body hit, health-change, hit-model, script-health-write and balancer-shadow logs.
	{"coop_jumpdiag", coop_cfg_switch, coop_cfg_diag},   // off | server | Per-jump phase and parameter logs for monster leaps.
	{"coop_jumpdiag_off", coop_cfg_switch, coop_cfg_control},   // off (capture ON) | server | Disables the default-on stuck-jump capture sweep.
	{"coop_jumpfix_off", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: the monster jump fix is disabled.
	{"coop_jumpthrottle_off", coop_cfg_switch, coop_cfg_control},   // off | server | Control arm: no scheduler hold while a monster jump runs (slow-jump fix off).
	{"coop_jump_provoke", coop_cfg_switch, coop_cfg_test},   // off | server | Harness seam: provokes monster jumps for the leap measurements.
	{"coop_meleediag", coop_cfg_switch, coop_cfg_diag},   // off | server | Monster melee timing, miss and animation logs.
	{"coop_melee_perframe", coop_cfg_switch, coop_cfg_test},   // off | server | TEST-ONLY per-frame hold for the crow-list melee measurement.
	{"coop_noimgui", coop_cfg_switch, coop_cfg_tunable},   // off | client | Skips ImGui on a headless client (the null-D3D9 stub AV at connect).
	{"coop_bind", coop_cfg_value, coop_cfg_tunable},   // off (any address) | both | Binds the ENet socket to the given address (flag literal includes trailing space).
	{"coop_test_rep32", coop_cfg_value, coop_cfg_test},   // off; 60 when given (atoi*1000) | server | R3.2 kill-pressure crossing and decay probe.
	{"coop_test_rpg", coop_cfg_value, coop_cfg_test},   // off; 30 when given without a valid value | server | §14 step 8 ownership-tier probe (write+verify).
	{"coop_test_rpg2", coop_cfg_value, coop_cfg_test},   // off; 30 when given without a valid value | server | §6.3 bridge-case probe.
	{"coop_test_rpg_verify", coop_cfg_value, coop_cfg_test},   // off; 30 when given without a valid value | server | Verify-only half of the tier probe for the leg that boots from the save.
	{"coop_test_walk", coop_cfg_value, coop_cfg_test},   // off; 10 when given without a valid value | client | Displaces the client's own actor N metres once (does the server CSE follow). PREFIX-COLLISION with -coop_test_walk_after
	{"coop_test_walk_after", coop_cfg_value, coop_cfg_test},   // 30 | client | Delay before the -coop_test_walk displacement.
	{"coop_test_worlditem", coop_cfg_value, coop_cfg_test},   // off; medkit when given without a section | server | Drops one item into the world beside a player after a checkpoint bank, to check death rollback leaves the world untouched.
};
