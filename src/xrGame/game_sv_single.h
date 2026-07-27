#pragma once

#include "game_sv_base.h"

class xrServer;
class CALifeSimulator;
class xrClientData;
class CSE_ALifeCreatureActor;
class CSE_Abstract;

class game_sv_Single : public game_sv_GameState
{
private:
	typedef game_sv_GameState inherited;

protected:
	CALifeSimulator* m_alife_simulator;

public:
	game_sv_Single();
	virtual ~game_sv_Single();

	virtual LPCSTR type_name() const { return "single"; };
	virtual void Create(shared_str& options);
	//	virtual		CSE_Abstract*		get_entity_from_eid		(u16 id);


	virtual void OnCreate(u16 id_who);
	// MP fork (§14 co-op): spawn a per-client actor so each player controls their own
	// character (the single game type otherwise has only the save's one actor, which
	// all clients collide on). The M_CLIENTREADY hook is unreliable for co-op clients,
	// so we also POLL from Update() for ready clients that still lack an actor.
	virtual void OnPlayerConnectFinished(ClientID id_who);
private:
	void coop_poll_spawns();                 // spawn actors for ready, actorless clients
	void coop_spawn_actor_for(xrClientData* CL);
	void coop_clone_inventory_for(CSE_ALifeCreatureActor* base, CSE_Abstract* owner, xrClientData* CL);
	void coop_give_starting_kit(CSE_Abstract* owner, xrClientData* CL);
	void coop_update_anchors();              // feed player positions to A-Life anchors
	xr_map<u32, u32> m_coop_seen;            // client id -> first-seen time (grace for load)

	// MP fork (§9.3/9.4 co-op reconnection): when a co-op client disconnects, its actor
	// entity stays alive in the world (orphaned) for up to RECONNECT_TIMEOUT_MS. If the
	// same player name reconnects within that window, the orphaned actor is re-associated
	// instead of spawning a fresh one — preserving position, health, inventory.
	struct coop_orphan
	{
		u16         entity_id;   // server entity ID of the orphaned actor
		shared_str  player_name; // name used to match the reconnecting client
		u32         disconnect_time; // Device.dwTimeGlobal when disconnected
		// MP fork (§14 step 7 phase 2): restored from a save's ownership sidecar rather
		// than from a live disconnect. A persisted binding must survive an arbitrarily
		// long server downtime, so it is NEVER expired by coop_cleanup_orphans().
		bool        persistent;
		// The body's position as it came out of the .scop. A restored body switches online
		// owned by the server's loopback client (it must, or Process_event asserts), and
		// the server then writes its own unplaced object state back over the CSE — first
		// run measured the saved position -235.6,27.9,253.9 becoming -0.1,0.2,0.7 before
		// the owner reconnected. Keep the authoritative value here and restore it on
		// reclaim so a player always resumes where they logged off (§9.3).
		Fvector     saved_pos;
		bool        have_saved_pos;
		// MP fork (§14 step 7 phase 4 D2, run 2): the SAME overwrite takes the health with it —
		// a body waiting for its owner was measured at hp 0.00 as well as at -0.1,0.2,0.7, and
		// the reclaim ships the CSE's health to the client, so the returning player was handed a
		// CORPSE. Snapshot the health at the moment the body comes online (still correct then,
		// same instant the position snapshot is taken) and restore it on reclaim.
		float       saved_health;
		bool        have_saved_health;
		bool        frozen;      // ownership already detached (server no longer updates it)
		// MP fork (§14 step 7 phase 4 D3.3 / doc §9.4): how long before this disconnect the
		// player's health last went down, or COOP_NO_DAMAGE if it never did within this process.
		// Stamped at the disconnect (from the M_CL_UPDATE peek) because that is the only moment
		// both facts are still in hand. Deliberately NOT persisted to the sidecar: D3.4 is the
		// only D3 item allowed to touch a format, and the question this answers — did someone
		// pull the plug in a losing fight and come straight back — is about a live reconnect
		// inside one process anyway. A server restart resets it, and that is the honest answer.
		u32         damage_age_ms;
	};
	static const u32 COOP_NO_DAMAGE = u32(-1);
	// The window that makes a disconnect worth a log line. Not a rule, not a timer, not tunable
	// on purpose: it is the bucket real data gets collected into before anyone writes a rule.
	static const u32 COOP_POLICY_DAMAGE_WINDOW_MS = 30 * 1000;
	xr_vector<coop_orphan> m_coop_orphans;
	static const u32 RECONNECT_TIMEOUT_MS = 5 * 60 * 1000; // 5 minutes

	void coop_orphan_actor(xrClientData* CL);    // called on co-op client disconnect
	void coop_cleanup_orphans();                  // expire stale orphans
	void coop_freeze_restored_bodies();           // detach server ownership of restored bodies
	// out_pos/out_have_pos (optional) report the .scop position of a RESTORED body.
	// out_damage_age_ms (optional, D3.3) reports the record's damage_age_ms — the matching
	// record is ERASED by this call, so anything the reclaim wants from it must leave here.
	CSE_Abstract* coop_find_orphan(LPCSTR name, Fvector* out_pos = NULL, bool* out_have_pos = NULL,
	                               float* out_health = NULL, u32* out_damage_age_ms = NULL);

	// MP fork (§9.4/9.5 co-op save/load — step 7 phase 1): the dedicated server owns the
	// world and must snapshot it to disk on its own — no client, no console, no live actor.
	// coop_autosave() drives the existing atomic ALife save directly (prepare + save).
	void coop_autosave();                         // server-authoritative atomic world save
	u32  m_coop_autosave_interval_ms;             // 0 = disabled; else save period
	u32  m_coop_autosave_last;                    // Device.dwTimeGlobal of last save (0 = never)
	bool m_coop_autosave_init;                    // one-shot flag parse

	// MP fork (§14 step 7 phase 2): player identity persistence. The actor ENTITY already
	// survives in the .scop (phase-1 measurement: alife_reg=1), but the player_name ->
	// entity_id BINDING lives only in runtime state (CL->owner / m_coop_orphans), so after
	// a restart nothing says which persisted actor belongs to which player. Snapshot every
	// binding into a "<save>.coop" sidecar next to the .scop, and on load seed m_coop_orphans
	// so the existing reconnection path (coop_find_orphan) re-claims the right body.
	void coop_save_bindings(LPCSTR save_name);    // write <save>.coop next to the .scop
	void coop_load_bindings(LPCSTR save_name);    // read it back, seed persistent orphans
	static const u32 COOP_BINDINGS_MAGIC = 0x50434F43;  // 'COCP' (LE) — sidecar file magic
	// v1 = ownership bindings only. v2 (phase 3 C2) appends the checkpoint block;
	// v3 (phase 4 D1/E) widens each binding record with the logged-off position and appends
	// the recovery block. The loader still accepts v1 and v2 — an older sidecar just means
	// nobody has a checkpoint / recovery record yet, never a load failure.
	static const u32 COOP_BINDINGS_VERSION = 3;

	// MP fork (§14 step 7 phase 3, gap C): per-player CHECKPOINT — the doc's §9.1/9.2
	// person-state snapshot that death rolls back to. Three constraints from phases 1-2
	// shape it: it is recorded from ENGINE CSE state only (P1 §3a: driving the GAMMA per-
	// object Lua save callbacks on the dedicated server corrupts the LuaJIT VM), the
	// position is a value the server records DELIBERATELY (P2 §3b: a reloaded actor's live
	// o_Position is garbage), and it is keyed on coop_player_name() like every other
	// per-player record (P2: the player-state account name is empty on thin clients).
	struct coop_checkpoint_item
	{
		shared_str section;      // s_name — what to re-create on rollback
		float      condition;    // CSE_ALifeInventoryItem::m_fCondition
		u16        ammo_elapsed; // weapons: rounds left in the magazine
		u8         ammo_type;    // weapons: which ammo section that magazine holds
		u8         slot;         // weapons: inventory slot
	};
	struct coop_checkpoint
	{
		shared_str player_name;
		Fvector    pos;          // where the player rolls back TO
		Fvector    angle;
		float      health;
		u32        node_id;      // level vertex, so the rollback lands on the nav mesh
		u16        graph_id;
		u32        banked_time;  // Device.dwTimeGlobal when banked
		xr_vector<coop_checkpoint_item> items;
	};
	xr_vector<coop_checkpoint> m_coop_checkpoints;
	coop_checkpoint* coop_find_checkpoint(LPCSTR player_name);

	// MP fork (§14 step 7 phase 4 D1, gap D / doc §9.4): per-player RECOVERY record — where a
	// player actually WAS, so a process that dies mid-session does not cost them their walk.
	// D0 measured that a client-owned actor's M_CL_UPDATE stream keeps the server CSE current
	// (restored 0.00 m from the post-walk point), so this is PERSISTENCE, not a new sampling
	// subsystem: the server copies that already-authoritative value on the autosave tick, at
	// the same instant the .scop is written. It is still recorded DELIBERATELY rather than read
	// back off an entity after a reload (P2 §3b rule 2: a restored actor's live o_Position is
	// garbage), and keyed on coop_player_name() like every other per-player record (rule 3).
	// Which of recovery / logged-off / checkpoint a boot hands back is increment D2's decision;
	// D1 only makes the value survive.
	struct coop_recovery
	{
		shared_str player_name;
		Fvector    pos;
		u32        node_id;      // level vertex, so a recovery lands on the nav mesh
		u16        graph_id;
		float      health;
		u32        sampled_time; // Device.dwTimeGlobal when sampled (runtime-only; re-stamped on load)
		// MP fork (§14 step 7 phase 4 D2): true only for a record read back out of a sidecar,
		// i.e. one describing the PREVIOUS process. That is the whole boot decision: a record
		// exists for a player only if they were still CONNECTED when that process ended (a
		// graceful logoff drops it — coop_orphan_actor), so its presence, not the dirty flag,
		// is what says "hand this player back where they actually were" (§9.4).
		bool       persisted;
	};
	xr_vector<coop_recovery> m_coop_recoveries;
	coop_recovery* coop_find_recovery(LPCSTR player_name);
	void coop_sample_recoveries();   // snapshot every connected player, on the autosave tick
	void coop_drop_recovery(LPCSTR player_name);  // graceful logoff: this player is not "in the world"

	// MP fork (§14 step 7 phase 4 D2, gap D / doc §9.4): how the LAST process ended, and how
	// THIS one can be asked to stop.
	//
	// The flag is a separate file, not a byte in the sidecar, so a process that died while the
	// sidecar was being written is still detectable. It is deliberately NOT load-bearing for
	// the resume POSITION — the per-player recovery lifecycle above already resolves all four
	// crash/clean x connected/logged-off cases — it is the record of how the last process ended,
	// which §9.4 policy, logging and any later anti-abuse rule will want.
	void coop_mark_dirty();          // boot: report the previous process, then claim the flag
	void coop_clear_dirty();         // clean stop: the world on disk is complete
	void coop_check_stop_request();  // poll the stop file (the only clean stop this server has)
	void coop_clean_shutdown(LPCSTR reason);
	// MP fork (§14 step 7 phase 4 D3.2 / doc §9.4): tell ONE player something about their
	// session. The server holds the only copy of "how the last process ended"; a returning
	// player currently learns nothing at all, which means a resume at the WRONG position looks
	// exactly like a resume at the right one from where they are standing.
	void coop_send_notice(xrClientData* CL, u8 code, LPCSTR text);
	// The notice codes are a wire contract with gamedata (_G.mp_coop_on_notice) — append only.
	static const u8 COOP_NOTICE_CRASH_RESUME = 1;
	static const u32 COOP_DIRTY_MAGIC = 0x54524944;   // 'DIRT' (LE)
	// MP fork (§14 step 7 phase 4 D3.4): v2 appends `consecutive_dirty`. v1 (4 words: magic,
	// version, pid, unix time) is still READ — a flag left by a D2-era process simply carries no
	// count, which is the truthful answer, not a load failure. An UNKNOWN version is the case D2
	// never handled: the magic already made an unreadable flag read as dirty (the safe reading),
	// but a v3 file from a future build would have had its fields parsed as if they were ours.
	static const u32 COOP_DIRTY_VERSION = 2;
	static const u32 COOP_DIRTY_WORDS   = 5;   // magic, version, pid, unix time, consecutive_dirty
	static const u32 COOP_DIRTY_LOOP_WARN = 3; // consecutive dirty boots that earn a loud line
	bool m_coop_prev_crash;          // a dirty flag was present at boot => the last process died
	bool m_coop_dirty_checked;       // one-shot: the flag is claimed once per process
	// MP fork (§14 step 7 phase 4 D3.4): consecutive dirty boots INCLUDING this one — 0 when the
	// last process stopped cleanly. Carried in the flag so it survives the process that would
	// otherwise have to remember it, which is precisely the process that keeps dying. A clean stop
	// deletes the flag, and that deletion IS the reset: a counter that only ever climbs is
	// indistinguishable from a broken one. Diagnostics, not policy — nothing decides on it.
	u32  m_coop_dirty_streak;
	u32  m_coop_stop_poll_last;      // Device.dwTimeGlobal of the last stop-file probe
	bool coop_spawn_checkpoint_item(CSE_Abstract* owner, xrClientData* CL,
	                                const coop_checkpoint_item& rec);
	void coop_test_drop_world_item();    // harness: §9.2 negative case (see below)
	// MP fork (§14 step 7 phase 3 C3, harness): -coop_test_worlditem drops one item into the
	// WORLD (unparented) right after the auto-bank. §9.2's whole point is that a death
	// rewinds the PERSON and leaves the WORLD alone, so the rollback re-checks this entity
	// and logs whether it is still there, unmoved. Without it a position-only test would
	// happily pass a rollback that had wiped everything the player left lying around.
	u16  m_coop_test_worlditem_id;       // 0xffff = none
	Fvector m_coop_test_worlditem_pos;
	u32  m_coop_test_checkpoint_ms;      // -coop_test_checkpoint <seconds>: auto-bank (harness)
	u32  m_coop_test_checkpoint_armed;   // when the flag was parsed — the delay runs from here
	u32  m_coop_test_checkpoint_retry;   // last attempt, so retries are 5s apart not per-frame
	bool m_coop_test_checkpoint_init;
	bool m_coop_test_checkpoint_done;

	// MP fork (§14 step 8 phase 1, harness): -coop_test_rpg [<seconds>] / -coop_test_rpg_verify.
	// Drives the ownership-tier probe. The acting-player context is set ONLY by the engine (a
	// scoped setter around a player-initiated action), so a Lua-side harness could not forge it
	// and a test that forged it would be testing the harness. This runs gamedata's probe three
	// times against the REAL primitive — outside a scope, inside one, and after it closed — so
	// the routing and the RAII restore are both measured. `_verify` re-reads only: leg 2 boots
	// from the save and must not re-write what it is checking survived.
	u32  m_coop_test_rpg_ms;             // 0 = disabled
	u32  m_coop_test_rpg_armed;          // delay measured from the parse, not from engine start
	u32  m_coop_test_rpg_retry;          // retries 5s apart until a player has an actor
	bool m_coop_test_rpg_init;
	bool m_coop_test_rpg_done;
	bool m_coop_test_rpg_verify_only;
	// Runs the probe sequence; false = no player with an actor yet, try again later.
	bool coop_test_rpg_probe();
	// One call into gamedata's _G.mp_coop_rpg_probe(phase, world_id, player_id).
	void coop_rpg_probe_call(LPCSTR phase, u16 world_id, u16 player_id);
public:
	// Server-side bank path. Exposed so gamedata can trigger it at a campfire/base via the
	// `game.mp_set_checkpoint(name)` luabind export; the engine does not care what triggered it.
	bool coop_bank_checkpoint(LPCSTR player_name);

	// MP fork (§14 step 7 phase 3 C3 / §9.1-9.2): death rollback. Restores the PERSON —
	// checkpoint position, health and banked inventory — and leaves the WORLD untouched.
	// true => io_pos/io_health hold the checkpoint values the client must be told about;
	// false => the caller keeps its existing death-position behaviour (no checkpoint).
	bool coop_checkpoint_respawn(u16 actor_id, xrClientData* CL, Fvector& io_pos, float& io_health);

	void OnCoopClientDisconnected(xrClientData* CL); // game-layer co-op disconnect handler

	// MP fork (§14 step 7 phase 4 D2, reclaim delivery): is E — or anything it hangs off — an
	// orphaned body reserved for THIS client's player name? `xrServer::Perform_connect_spawn` asks
	// so it can leave that body OUT of the connection snapshot: the reclaim hands the same entity
	// over as LOCAL+ASPLAYER seconds later, and a client that already holds a copy drops the real
	// one as a duplicate and then FATALs destroying the ghost. Measured 2026-07-26, see the plan.
	bool coop_is_own_orphan(CSE_Abstract* E, xrClientData* CL);
public:
	virtual BOOL OnTouch(u16 eid_who, u16 eid_what, BOOL bForced = FALSE);
	virtual void OnDetach(u16 eid_who, u16 eid_what);

	// Main
	virtual void Update();
	virtual ALife::_TIME_ID GetStartGameTime();
	virtual ALife::_TIME_ID GetGameTime();
	virtual float GetGameTimeFactor();
	virtual void SetGameTimeFactor(const float fTimeFactor);

	virtual ALife::_TIME_ID GetEnvironmentGameTime();
	virtual float GetEnvironmentGameTimeFactor();
	virtual void SetEnvironmentGameTimeFactor(const float fTimeFactor);

	virtual bool change_level(NET_Packet& net_packet, ClientID sender);
	virtual void save_game(NET_Packet& net_packet, ClientID sender);
	virtual bool load_game(NET_Packet& net_packet, ClientID sender);
	virtual void reload_game(NET_Packet& net_packet, ClientID sender);
	virtual void switch_distance(NET_Packet& net_packet, ClientID sender);
	virtual BOOL CanHaveFriendlyFire() { return FALSE; }
	virtual void teleport_object(NET_Packet& packet, u16 id);
	virtual void add_restriction(NET_Packet& packet, u16 id);
	virtual void remove_restriction(NET_Packet& packet, u16 id);
	virtual void remove_all_restrictions(NET_Packet& packet, u16 id);
	virtual bool custom_sls_default() { return !!m_alife_simulator; };
	virtual void sls_default();
	virtual shared_str level_name(const shared_str& server_options) const;
	virtual void on_death(CSE_Abstract* e_dest, CSE_Abstract* e_src);
	void restart_simulator(LPCSTR saved_game_name);

	IC xrServer& server() const
	{
		VERIFY(m_server);
		return (*m_server);
	}

	IC CALifeSimulator& alife() const
	{
		VERIFY(m_alife_simulator);
		return (*m_alife_simulator);
	}
};
