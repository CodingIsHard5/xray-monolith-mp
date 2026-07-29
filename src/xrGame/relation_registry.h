//////////////////////////////////////////////////////////////////////////
// relation_registry.h: реестр для хранения данных об отношении персонажа к 
//						другим персонажам
//////////////////////////////////////////////////////////////////////////

#pragma once

#include "character_info_defs.h"

class CRelationRegistryWrapper;

class CInventoryOwner;
class CEntityAlive;

//////////////////////////////////////////////////////////////////////////

#define GAME_RELATIONS_SECT "game_relations"
#define ACTIONS_POINTS_SECT "action_points"

//////////////////////////////////////////////////////////////////////////

struct RELATION_REGISTRY
{
public:
	RELATION_REGISTRY();
	virtual ~RELATION_REGISTRY();

public:

	template <typename T>
	ALife::ERelationType GetRelationBetween(T char1, T char2) const;

	template <typename T>
	ALife::ERelationType GetRelationType(T from, T to) const;
	template <typename T>
	void SetRelationType(T from, T to, ALife::ERelationType new_relation);

	//общее отношение  одного персонажа к другому, вычисленное по формуле
	//с учетом всех факторов - величина от 
	//-100< (крайне враждебное) до >100 (очень дрюжелюбное)

	template <typename T>
	CHARACTER_GOODWILL GetAttitude(T from, T to) const;

	//личное отношение (благосклонность) одного персонажа к другому - 
	//величина от -100< (крайне враждебное) до >100 (очень дрюжелюбное)
	CHARACTER_GOODWILL GetGoodwill(u16 from, u16 to) const;
	void SetGoodwill(u16 from, u16 to, CHARACTER_GOODWILL goodwill);
	void ForceSetGoodwill(u16 from, u16 to, CHARACTER_GOODWILL goodwill);
	void ChangeGoodwill(u16 from, u16 to, CHARACTER_GOODWILL delta_goodwill);

	//отношения группировки к персонажу (именно так, а не наоборот)
	//т.е. персонаж сам помнит, как к нему какая группировка отностися
	CHARACTER_GOODWILL GetCommunityGoodwill(CHARACTER_COMMUNITY_INDEX from_community, u16 to_character) const;
	void SetCommunityGoodwill(CHARACTER_COMMUNITY_INDEX from_community, u16 to_character, CHARACTER_GOODWILL goodwill);
	void ChangeCommunityGoodwill(CHARACTER_COMMUNITY_INDEX from_community, u16 to_character,
	                             CHARACTER_GOODWILL delta_goodwill);

	void ClearRelations(u16 person_id);

	CHARACTER_GOODWILL GetCommunityRelation(CHARACTER_COMMUNITY_INDEX, CHARACTER_COMMUNITY_INDEX) const;
	void SetCommunityRelation(CHARACTER_COMMUNITY_INDEX index1, CHARACTER_COMMUNITY_INDEX index2,
	                          CHARACTER_GOODWILL goodwill);

private:
	CHARACTER_GOODWILL GetRankRelation(CHARACTER_RANK_VALUE, CHARACTER_RANK_VALUE) const;
	CHARACTER_GOODWILL GetReputationRelation(CHARACTER_REPUTATION_VALUE, CHARACTER_REPUTATION_VALUE) const;


	//реакцией на действия персонажей и соответствующее изменение отношения
public:

	//список действий актера, за которые начисляются
	//очки рейтинга, репутации или меняется отношения персонажа
	//к группировке
	enum ERelationAction
	{
		KILL = 0x00,
		//убийство персонажа
		ATTACK = 0x01,
		//атака персонажа
		FIGHT_HELP_HUMAN = 0x02,
		//помощь в драке персонажу с другим персонажем
		FIGHT_HELP_MONSTER = 0x04,
		//помощь в драке персонажу c монстром
		SOS_HELP = 0x08 //приход на помощь по сигналу SOS
	};

	void Action(CEntityAlive* from, CEntityAlive* to, ERelationAction action);

public:

	struct FIGHT_DATA
	{
		FIGHT_DATA();
		u16 attacker;
		u16 defender;
		float total_hit;
		u32 time;
		u32 time_old;

		u32 attack_time; //время фиксирования события "атака"
		ALife::ERelationType defender_to_attacker; //как относился атакованый к нападавшему во время начальной атаки
	};

	struct RELATION_MAP_SPOTS
	{
		RELATION_MAP_SPOTS();
		shared_str spot_names[ALife::eRelationTypeLast + 1];

		const shared_str& GetSpotName(ALife::ERelationType& type)
		{
			if (type < ALife::eRelationTypeLast)return spot_names[type];
			else return spot_names[ALife::eRelationTypeLast];
		};
	};

	//зарегистрировать драку (реакция на Hit в EntityAlive)
	void FightRegister(u16 attacker, u16 defender, ALife::ERelationType defender_to_attacker, float hit_amount);
	void UpdateFightRegister();

private:
	DEFINE_VECTOR(FIGHT_DATA, FIGHT_VECTOR, FIGHT_VECTOR_IT);
	static FIGHT_VECTOR* m_fight_registry;
	static FIGHT_VECTOR& fight_registry();

	FIGHT_DATA* FindFight(u16 object_id, bool by_attacker/* = true*/);
	static RELATION_MAP_SPOTS* m_spot_names;
public:
	const shared_str& GetSpotName(ALife::ERelationType& type);
	static CRelationRegistryWrapper& relation_registry();
	static void clear_relation_registry();
private:
	static CRelationRegistryWrapper* m_relation_registry;
};

////////////////////////////////////////////////////////////////////////////
// MP fork (§14 step 8 phase 4 R2 / doc §8.1): reputation's two tiers on a
// server where db.actor is nil.
//
// R1 measured the split rather than trusting the recon, because the two tiers
// fail identically to a reader: personal standing rides the .scop inside
// alife_registry_container (77 came back on the same entity id), and the
// faction<->faction table does NOT (a relation moved to -63 came back at its
// -2000 config value). So this half of the layer owns two things and neither
// is storage for the personal tier:
//
//   * coop_rep_state_save/load — the faction tier's own .scop chunk, under the
//     Q2 rules (versioned, an unknown version REFUSED rather than parsed as if
//     the fields were ours, a truncated chunk discarded whole, clear first).
//   * coop_rep_subject         — the routing seam. A script call site that
//     cannot name a subject (every stock one names db.actor:id(), which is nil
//     here) passes COOP_REP_ACTING and gets the acting player. When there is no
//     acting player the call is REFUSED — deliberately NOT redirected to the
//     world tier, because personal standing on the world tier is one player's
//     reputation silently becoming everybody's.
////////////////////////////////////////////////////////////////////////////

// The sentinel a caller passes for "the player this action is being run for,
// whoever that is". It is an int because that is what the script bindings take;
// -1 mirrors mp_coop_owner::none.
#define COOP_REP_ACTING				(-1)

// Returns the entity id to act on, or u16(-1) (mp_coop_owner::none) when the
// call must be refused. `is_write` only affects the diagnostics.
u16 coop_rep_subject(int passed_id, bool is_write);

// Counters for the harness: routed = calls that resolved through the acting
// player, refused = calls that had no subject and were dropped. A seam that is
// never exercised and a seam that works are indistinguishable without these.
u32 coop_rep_routed_count();
u32 coop_rep_routed_zero_count();   // of those, the hardcoded-`0` call sites
u32 coop_rep_refused_count();

// Re-impose what the save said the world moved. The faction table is CONFIG's at every level
// load — CLevel::Load_GameSpecific_Before resets it — so the saved state is an overlay that has
// to be re-applied after each reset, not a one-shot write at load time. Returns cells applied.
u32 coop_rep_apply_overlay(LPCSTR why);

void coop_rep_state_save(IWriter& stream);
void coop_rep_state_load(IReader& stream);

////////////////////////////////////////////////////////////////////////////
// MP fork (§14 step 8 phase 4 R3.1 / doc §8.2): the BLAST RADIUS of a kill.
//
// R3.0 measured the ground this stands on rather than assuming it, and the
// result removed a third of the work and changed the shape of the rest:
//
//   * THE SHOOTER TIER IS ALREADY STOCK. One kill moved the victim
//     community's row toward the killer by -140, plus -375 reputation and
//     +85 rank, through RELATION_REGISTRY::Action. So nothing below touches
//     it. "Exactly one shooter hit per kill" is a GATE the harness counts,
//     not a hope: there is exactly one Action(...,KILL) call site in the
//     engine (CEntityAlive::Die), and this code is called from inside it
//     rather than beside it, so the two cannot drift apart.
//   * `actor` IS NOT A FACTION. The co-op player's community is the literal
//     pseudo-community `actor` (index 0), not one of GAMMA's per-faction
//     `actor_*` ones — and [communities_relations] holds the whole `actor`
//     column flat at 0 for every faction, which is what a column that does
//     not participate in the faction matrix looks like. EVERY player is
//     `actor`, so a collective hit keyed on it would make one player's kill
//     hostile for everybody, through the faction tier — doc §8.3's
//     most-important-to-avoid failure arriving by a different door than the
//     one R2 closed, and invisible on a single-client test. So the faction
//     tier REFUSES a killer whose community is not a faction, and
//     `stalker -> actor` staying at zero is the discriminating gate.
//
// Both new tiers fire only when the KILLER IS A CONNECTED PLAYER. §8.2 is
// about the blast radius of a player's kill; letting A-Life's own NPC-on-NPC
// kills move faction relations would rewrite the world's politics from
// nothing anybody did, continuously, and is not this increment's to decide.
////////////////////////////////////////////////////////////////////////////

// The two tiers stock does not have. TWO SAMPLING POINTS, for two reasons, which is what run 1
// forced and is the real content of this design:
//
//   * POSITIONS are sampled HERE, at the kill, synchronously on the death path. A player's CSE
//     `o_Position` is written by the ENet pump thread, so any later sampling point scores the
//     bystander wherever they had walked to by then — the D1/E defect exactly. Reading at the
//     kill bounds the error to one client-update interval.
//   * THE MAGNITUDE cannot be sampled here, and run 1 measured why. R3.0 concluded stock's
//     `Action` applied the -140; it does not — `Sympathy()` is 0.0 for every community in this
//     config, so that write is multiplied to zero and guarded out, and the -140 that really
//     lands arrives AFTER `Action` returns, from a writer this code does not own. So the
//     propagation stops trying to be told the magnitude and MEASURES it: it records the
//     killer's row at the kill, and `coop_rep_propagate_tick` re-reads it once the shooter hit
//     has landed and scales the bystander term against the difference.
//
// That is strictly better than being passed a number, and not only here: the bystander term is
// now a scaled copy of what the shooter ACTUALLY took, whoever applied it and whatever config
// says, so it cannot drift away from the shooter tier the way a second formula would.
void coop_rep_propagate_kill(u16 killer_id, CHARACTER_COMMUNITY_INDEX killer_comm,
                             u16 victim_id, CHARACTER_COMMUNITY_INDEX victim_comm,
                             const Fvector& kill_pos);

// Drive the deferred half. Called every server tick; does nothing when nothing is pending.
void coop_rep_propagate_tick();

// The distance falloff and its radius, exposed so the harness can measure the CURVE at chosen
// distances instead of inferring it from one write. Exactly 0 at and beyond the radius — the
// negative gate of this increment is a movement of exactly zero outside it, because a bystander
// term that is merely "small" passes any test that only checks the shooter.
float coop_rep_bystander_scale(float dist_m);
float coop_rep_bystander_radius();

// The radius is a tunable (doc Appendix B), and the harness sets it per leg: run 3 measured the
// two co-op clients spawning ~105 m from the nearest live stalker, so a fixed 30 m makes every
// real bystander measurement a zero — correct, and useless. Setting it from the distance the run
// actually has exercises the falloff at a KNOWN scale rather than waiting for the world to place
// an NPC conveniently.
void coop_rep_set_bystander_radius(float r);

// Move a faction<->faction cell AND record it in the R2 overlay, which is the ONE representation
// of "what the world moved". R2's storage is inherited deliberately: the overlay IS the
// decayable quantity R3.2 shrinks, so a live faction write that only touched the relation table
// would be a second representation — and would be dropped by the level-load reset that R2 run 1
// found. Returns true if the cell actually moved.
bool coop_rep_faction_move(CHARACTER_COMMUNITY_INDEX from, CHARACTER_COMMUNITY_INDEX to,
                           s32 delta, LPCSTR why);

// §14 step 8 P4 R3.2 (doc §8.3) — THE BAR in front of coop_rep_faction_move.
//
// One member killing one enemy is a personal incident, so individual kills accumulate PRESSURE per
// (wronged faction -> blamed faction) and only a threshold crossing moves the relation. Returns
// true only if the relation actually moved; a `false` here is the common, correct case.
//
// This deliberately shares NO code with R2's `coop_rep_subject`, and the resemblance is the trap it
// is avoiding: R2 refuses a TYPE failure (a personal write with no subject is meaningless, and that
// is decidable from the call alone, which is why that resolver is pure). This refuses a POLICY
// failure — the write is meaningful, and the question is whether one act is sufficient GROUNDS for
// a collective consequence. A pure function of the call cannot know what came before it, so it
// would refuse every faction write or none.
bool coop_rep_pressure_add(CHARACTER_COMMUNITY_INDEX from, CHARACTER_COMMUNITY_INDEX to,
                           s32 delta, LPCSTR why);

// Decay for BOTH quantities, on ABSOLUTE GAME time (u64 — Q3 measured 63676055474670 ms). Pressure
// decays so a slow griefer never accumulates; the R2 overlay decays toward its config baseline so a
// war cools off. The overlay is the ONLY representation of a faction relation — decay shrinks it
// through coop_rep_faction_move rather than writing the table a second way.
void coop_rep_decay_tick();
u64  coop_rep_game_time_ms();

// Counters for the harness. A tier that never ran and a tier that ran correctly are
// indistinguishable from the relation values alone.
u32 coop_rep_bystanders_considered();
u32 coop_rep_bystanders_moved();
u32 coop_rep_faction_moved_count();
u32 coop_rep_faction_refused_count();

// R3.2 state, for the harness: the griefer-loop counter (`held`), the crossings, the live pressure
// on a pair, and the stored decay clock — the last so a restart can be MEASURED rather than
// assumed, because a fresh timer and a correctly-resumed one look identical from the value alone.
u32 coop_rep_pressure_cells();
u32 coop_rep_pressure_crossed();
u32 coop_rep_pressure_held();
s32 coop_rep_pressure_bar();
s32 coop_rep_pressure_of(CHARACTER_COMMUNITY_INDEX from, CHARACTER_COMMUNITY_INDEX to);
u64 coop_rep_overlay_stamp();
u64 coop_rep_pressure_halflife_ms();

// HARNESS ONLY. The shipped half-life is 6 GAME-hours, which no test can wait out, so the harness
// compresses the SCALE. It does not touch the clock: decay still advances only because game time
// advances, and the stored stamp is still what drives it. The alternative — winding a cell's stamp
// backwards to fake elapsed time — would have the probe writing the state it then measures, and
// would demonstrate the decay arithmetic rather than that the clock drives it. Values are logged.
void coop_rep_test_set_pressure_tunables(s32 bar, u64 pressure_halflife_ms, u64 overlay_halflife_ms,
                                         u64 decay_tick_ms);

// HARNESS ONLY, and it is a CONSTRUCTION rather than an observation: two headless clients cannot
// share a wine prefix ([[xray-two-headless-clients]]), so there is no second live player to stand
// near a kill. This injects one synthetic candidate into the SAME enumeration the real players go
// through — so the radius test, the same-faction test, the falloff and the write are all the
// production path — and every log line about it carries `[SYNTHETIC, no second live client]` on
// the line itself, because a prose caveat is separated from its number the moment somebody copies
// the number.
void coop_rep_test_set_bystander(bool armed, u16 id, const Fvector& pos,
                                 CHARACTER_COMMUNITY_INDEX comm);

#include "relation_registry_inline.h"
