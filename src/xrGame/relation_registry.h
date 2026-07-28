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

#include "relation_registry_inline.h"
