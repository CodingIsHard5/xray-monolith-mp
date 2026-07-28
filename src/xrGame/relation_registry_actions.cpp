#include "pch_script.h"
#include "relation_registry.h"
#include "../xrNetServer/xr_enet_transport.h"   // MP fork (§14 step 8 P4 R3.0): xr_enet::enabled()
#include "alife_registry_wrappers.h"

#include "actor.h"
#include "ai/stalker/ai_stalker.h"

#include "seniority_hierarchy_holder.h"
#include "team_hierarchy_holder.h"
#include "squad_hierarchy_holder.h"
#include "group_hierarchy_holder.h"

#include "memory_manager.h"
#include "enemy_manager.h"

struct SAttackGoodwillStorage
{
	CHARACTER_GOODWILL friend_attack_goodwill;
	CHARACTER_GOODWILL neutral_attack_goodwill;
	CHARACTER_GOODWILL enemy_attack_goodwill;
	CHARACTER_GOODWILL community_member_attack_goodwill;

	CHARACTER_GOODWILL friend_attack_reputation;
	CHARACTER_GOODWILL neutral_attack_reputation;
	CHARACTER_GOODWILL enemy_attack_reputation;

	void load(LPCSTR prefix)
	{
		string128 s;
		strconcat(sizeof(s), s, prefix, "friend_attack_goodwill");
		friend_attack_goodwill = pSettings->r_s32(ACTIONS_POINTS_SECT, s);

		strconcat(sizeof(s), s, prefix, "neutral_attack_goodwill");
		neutral_attack_goodwill = pSettings->r_s32(ACTIONS_POINTS_SECT, s);

		strconcat(sizeof(s), s, prefix, "enemy_attack_goodwill");
		enemy_attack_goodwill = pSettings->r_s32(ACTIONS_POINTS_SECT, s);

		strconcat(sizeof(s), s, prefix, "community_member_attack_goodwill");
		community_member_attack_goodwill = pSettings->r_s32(ACTIONS_POINTS_SECT, s);


		strconcat(sizeof(s), s, prefix, "friend_attack_reputation");
		friend_attack_reputation = pSettings->r_s32(ACTIONS_POINTS_SECT, s);

		strconcat(sizeof(s), s, prefix, "neutral_attack_reputation");
		neutral_attack_reputation = pSettings->r_s32(ACTIONS_POINTS_SECT, s);

		strconcat(sizeof(s), s, prefix, "enemy_attack_reputation");
		enemy_attack_reputation = pSettings->r_s32(ACTIONS_POINTS_SECT, s);
	}
};

SAttackGoodwillStorage gw_danger, gw_free;

void load_attack_goodwill()
{
	gw_danger.load("danger_");
	gw_free.load("free_");
}

// MP fork (§14 step 8 phase 4 R3.0, dev/RPG_LAYER_PLAN.md): say whether this path was REACHED,
// and where it stopped.
//
// R3's recon found that the whole of stock kill propagation runs through here, behind four gates
// that all fail the same silent way: IsGameTypeSingle() at the caller, a victim that must cast to
// CAI_Stalker (so killing a MONSTER moves nothing), a killer that must be an inventory owner and
// not a base monster, and a group loop over the client-shaped seniority_holder. Every one of them
// reads as "the feature did nothing" rather than as an error, and R3.1 must not be built on a
// guess about which of them is true on a dedicated co-op server. So the path narrates itself:
// entered, and if it returned early, why. Co-op only, and rate-limited — Action runs on every hit
// in the game, and an instrument that floods the log is one that gets turned off.
static void coop_rep_action_trace(LPCSTR what, CEntityAlive* from, CEntityAlive* to, int action)
{
	if (!xr_enet::enabled())
		return;
	static u32 s_lines = 0;
	if (s_lines >= 40)
		return;
	++s_lines;
	Msg("~ COOP(rep3): Action %s from=%u to=%u action=%d%s", what,
		from ? u32(from->ID()) : 0xffffu, to ? u32(to->ID()) : 0xffffu, action,
		(s_lines == 40) ? "   [further Action traces suppressed]" : "");
}

void RELATION_REGISTRY::Action(CEntityAlive* from, CEntityAlive* to, ERelationAction action)
{
	coop_rep_action_trace("ENTERED", from, to, int(action));
	static CHARACTER_GOODWILL friend_kill_goodwill = pSettings->r_s32(ACTIONS_POINTS_SECT, "friend_kill_goodwill");
	static CHARACTER_GOODWILL neutral_kill_goodwill = pSettings->r_s32(ACTIONS_POINTS_SECT, "neutral_kill_goodwill");
	static CHARACTER_GOODWILL enemy_kill_goodwill = pSettings->r_s32(ACTIONS_POINTS_SECT, "enemy_kill_goodwill");
	static CHARACTER_GOODWILL community_member_kill_goodwill = pSettings->r_s32(
		ACTIONS_POINTS_SECT, "community_member_kill_goodwill");

	static CHARACTER_REPUTATION_VALUE friend_kill_reputation = pSettings->r_s32(
		ACTIONS_POINTS_SECT, "friend_kill_reputation");
	static CHARACTER_REPUTATION_VALUE neutral_kill_reputation = pSettings->r_s32(
		ACTIONS_POINTS_SECT, "neutral_kill_reputation");
	static CHARACTER_REPUTATION_VALUE enemy_kill_reputation = pSettings->r_s32(
		ACTIONS_POINTS_SECT, "enemy_kill_reputation");

	//(с) мин. время через которое снова будет зарегестрировано сообщение об атаке на персонажа
	static u32 min_attack_delta_time = u32(1000.f * pSettings->r_float(ACTIONS_POINTS_SECT, "min_attack_delta_time"));

	static CHARACTER_GOODWILL friend_fight_help_goodwill = pSettings->r_s32(
		ACTIONS_POINTS_SECT, "friend_fight_help_goodwill");
	static CHARACTER_GOODWILL neutral_fight_help_goodwill = pSettings->r_s32(
		ACTIONS_POINTS_SECT, "neutral_fight_help_goodwill");
	static CHARACTER_GOODWILL enemy_fight_help_goodwill = pSettings->r_s32(
		ACTIONS_POINTS_SECT, "enemy_fight_help_goodwill");
	static CHARACTER_GOODWILL community_member_fight_help_goodwill = pSettings->r_s32(
		ACTIONS_POINTS_SECT, "community_member_fight_help_goodwill");

	static CHARACTER_REPUTATION_VALUE friend_fight_help_reputation = pSettings->r_s32(
		ACTIONS_POINTS_SECT, "friend_fight_help_reputation");
	static CHARACTER_REPUTATION_VALUE neutral_fight_help_reputation = pSettings->r_s32(
		ACTIONS_POINTS_SECT, "neutral_fight_help_reputation");
	static CHARACTER_REPUTATION_VALUE enemy_fight_help_reputation = pSettings->r_s32(
		ACTIONS_POINTS_SECT, "enemy_fight_help_reputation");


	CActor* actor = smart_cast<CActor*>(from);
	CInventoryOwner* inv_owner_from = smart_cast<CInventoryOwner*>(from);
	CAI_Stalker* stalker_from = smart_cast<CAI_Stalker*>(from);
	CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(to);

	//вычисление изменения репутации и рейтинга пока ведется 
	//только для актера
	if (!inv_owner_from || from->cast_base_monster())
	{
		// Gate 3. A monster killer moves nothing, and neither does anything that is not an
		// inventory owner — reported rather than inferred from an absence of movement.
		coop_rep_action_trace(!inv_owner_from ? "LEFT: killer is not an inventory owner"
		                                      : "LEFT: killer is a base monster", from, to, int(action));
		return;
	}
	// Gate 2, and the one most likely to make a harness measure nothing: everything below is
	// inside `if (stalker)`, so a victim that is not a CAI_Stalker leaves with no effect at all.
	if (!stalker)
		coop_rep_action_trace("victim is NOT a CAI_Stalker — this call will move nothing",
		                      from, to, int(action));

	ALife::ERelationType relation = ALife::eRelationTypeDummy;
	if (stalker)
	{
		stalker->m_actor_relation_flags.set(action, TRUE);
		relation = GetRelationType(smart_cast<CInventoryOwner*>(stalker), inv_owner_from);
	}

	switch (action)
	{
	case ATTACK:
		{
			if (actor)
			{
				//учитывать ATTACK и FIGHT_HELP, только если прошло время
				//min_attack_delta_time 
				FIGHT_DATA* fight_data_from = FindFight(from->ID(), true);
				if (Device.dwTimeGlobal - fight_data_from->attack_time < min_attack_delta_time)
					break;

				fight_data_from->attack_time = Device.dwTimeGlobal;

				//если мы атаковали персонажа или монстра, который 
				//кого-то атаковал, то мы помогли тому, кто защищался
				FIGHT_DATA* fight_data = FindFight(to->ID(), true);
				if (fight_data)
				{
					CAI_Stalker* defending_stalker = smart_cast<CAI_Stalker*>(
						Level().Objects.net_Find(fight_data->defender));
					if (defending_stalker)
					{
						CAI_Stalker* attacking_stalker = smart_cast<CAI_Stalker*>(
							Level().Objects.net_Find(fight_data->attacker));
						Action(actor, defending_stalker, attacking_stalker ? FIGHT_HELP_HUMAN : FIGHT_HELP_MONSTER);
					}
				}
			}

			if (stalker)
			{
				bool bDangerScheme = false;
				const CEntityAlive* stalker_enemy = stalker->memory().enemy().selected();
				if (actor && stalker_enemy)
				{
					const CInventoryOwner* const_inv_owner_from = inv_owner_from;
					if (stalker_enemy->human_being())
					{
						const CInventoryOwner* const_inv_owner_stalker_enemy = smart_cast<const CInventoryOwner*>(
							stalker_enemy);
						ALife::ERelationType relation_to_actor = GetRelationType(
							const_inv_owner_stalker_enemy, const_inv_owner_from);

						if (relation_to_actor == ALife::eRelationTypeEnemy)
							bDangerScheme = true;
					}
				}
				SAttackGoodwillStorage* st = bDangerScheme ? &gw_danger : &gw_free;

				CHARACTER_GOODWILL delta_goodwill = 0;
				CHARACTER_REPUTATION_VALUE delta_reputation = 0;
				switch (relation)
				{
				case ALife::eRelationTypeEnemy:
					{
						delta_goodwill = st->enemy_attack_goodwill;
						delta_reputation = st->enemy_attack_reputation;
					}
					break;
				case ALife::eRelationTypeNeutral:
					{
						delta_goodwill = st->neutral_attack_goodwill;
						delta_reputation = st->neutral_attack_reputation;
					}
					break;
				case ALife::eRelationTypeFriend:
					{
						delta_goodwill = st->friend_attack_goodwill;
						delta_reputation = st->friend_attack_reputation;
					}
					break;
				};

				//сталкер при нападении на членов своей же группировки отношения не меняют
				//(считается, что такое нападение всегда случайно)
				// change relation only for pairs actor->stalker, do not use pairs stalker->stalker
				bool stalker_attack_team_mate = stalker && stalker_from;
				if (delta_goodwill && !stalker_attack_team_mate)
				{
					//изменить отношение ко всем членам атакованой группы (если такая есть)
					//как к тому кого атаковали
					CGroupHierarchyHolder& group = Level()
					                               .seniority_holder().team(stalker->g_Team()).squad(stalker->g_Squad())
					                               .group(stalker->g_Group());
					for (std::size_t i = 0; i < group.members().size(); i++)
					{
						ChangeGoodwill(group.members()[i]->ID(), from->ID(), delta_goodwill);
					}

					//*(CHARACTER_GOODWILL)( stalker->Sympathy() * (float)(delta_goodwill));
					CHARACTER_GOODWILL community_goodwill = (CHARACTER_GOODWILL)(stalker->Sympathy() * (float)(st->
						community_member_attack_goodwill));
					if (community_goodwill)
					{
						ChangeCommunityGoodwill(stalker->Community(), from->ID(), community_goodwill);
					}
				}
				if (delta_reputation)
				{
					inv_owner_from->ChangeReputation(delta_reputation);
				}
			}
		}
		break;
	case KILL:
		{
			if (stalker)
			{
				//FIGHT_DATA* fight_data_from = FindFight (from->ID(), true);	

				//мы помним то, какое отношение обороняющегося к атакующему
				//было перед началом драки
				ALife::ERelationType relation_before_attack = ALife::eRelationTypeDummy;
				//if(fight_data_from)
				//	relation_before_attack = fight_data_from->defender_to_attacker;
				//else
				relation_before_attack = relation;

				CHARACTER_GOODWILL delta_goodwill = 0;
				CHARACTER_REPUTATION_VALUE delta_reputation = 0;

				switch (relation_before_attack)
				{
				case ALife::eRelationTypeEnemy:
					{
						delta_goodwill = enemy_kill_goodwill;
						delta_reputation = enemy_kill_reputation;
					}
					break;
				case ALife::eRelationTypeNeutral:
					{
						delta_goodwill = neutral_kill_goodwill;
						delta_reputation = neutral_kill_reputation;
					}
					break;
				case ALife::eRelationTypeFriend:
					{
						delta_goodwill = friend_kill_goodwill;
						delta_reputation = friend_kill_reputation;
					}
					break;
				};

				//сталкер при нападении на членов своей же группировки отношения не меняют
				//(считается, что такое нападение всегда случайно)
				bool stalker_kills_team_mate = stalker_from && (stalker_from->Community() == stalker->Community());

				if (delta_goodwill && !stalker_kills_team_mate)
				{
					//изменить отношение ко всем членам группы (если такая есть)
					//убитого, кроме него самого
					CGroupHierarchyHolder& group = Level()
					                               .seniority_holder().team(stalker->g_Team()).squad(stalker->g_Squad())
					                               .group(stalker->g_Group());
					for (std::size_t i = 0; i < group.members().size(); i++)
					{
						if (stalker->ID() != group.members()[i]->ID())
						{
							ChangeGoodwill(group.members()[i]->ID(), from->ID(), delta_goodwill);
						}
					}

					//(CHARACTER_GOODWILL)( stalker->Sympathy() * (float)(delta_goodwill+community_member_kill_goodwill));
					CHARACTER_GOODWILL community_goodwill = (CHARACTER_GOODWILL)(stalker->Sympathy() * (float)(
						community_member_kill_goodwill));
					if (community_goodwill)
					{
						ChangeCommunityGoodwill(stalker->Community(), from->ID(), community_goodwill);
					}
				}

				if (delta_reputation)
				{
					inv_owner_from->ChangeReputation(delta_reputation);
				}

				CHARACTER_RANK_VALUE delta_rank = 0;
				delta_rank = CHARACTER_RANK::rank_kill_points(CHARACTER_RANK::ValueToIndex(stalker->Rank()));
				if (delta_rank)
					inv_owner_from->ChangeRank(delta_rank);

				// MP fork (§14 step 8 PHASE 4 increment R3.1 / doc §8.2): the blast radius —
				// the bystander and collective tiers, which stock does not have.
				//
				// PLACEMENT IS A DECISION, and it is this one for a reason that is measurable
				// rather than stylistic. Everything above is the SHOOTER tier and R3.0 measured
				// it as already stock, so not a line of it is touched; the new tiers are called
				// from INSIDE the stock path instead of beside it, because there is exactly one
				// Action(...,KILL) call site in the engine (CEntityAlive::Die) and R3.0's trace
				// showed exactly one entry per kill. Hooking Die separately would have meant a
				// second copy of four gates that can drift out of step with these ones, and
				// "exactly one shooter hit per kill" would have become a hope. Here it is a
				// gate the harness counts: the killer's personal row must move by the stock
				// -140 and no more.
				//
				// The victim's position IS the kill position — it is an NPC the server itself
				// simulates, so its transform is live and authoritative here, unlike a player's.
				//
				// The magnitude is NOT passed. Run 1 measured that `community_goodwill` above is
				// 0 on this config (sympathy is 0.0 for every community) while the shooter's row
				// still moves -140 from a writer that runs after this returns — so the
				// propagation measures the shooter hit instead of being told about it, and the
				// bystander term becomes a scaled copy of whatever actually happened.
				coop_rep_propagate_kill(from->ID(), CHARACTER_COMMUNITY_INDEX(inv_owner_from->Community()),
				                        stalker->ID(), CHARACTER_COMMUNITY_INDEX(stalker->Community()),
				                        stalker->Position());
			}
		}
		break;
	case FIGHT_HELP_HUMAN:
	case FIGHT_HELP_MONSTER:
		{
			if (stalker && stalker->g_Alive())
			{
				CHARACTER_GOODWILL delta_goodwill = 0;
				CHARACTER_REPUTATION_VALUE delta_reputation = 0;

				switch (relation)
				{
				case ALife::eRelationTypeEnemy:
					{
						delta_goodwill = enemy_fight_help_goodwill;
						delta_reputation = enemy_fight_help_reputation;
					}
					break;
				case ALife::eRelationTypeNeutral:
					{
						delta_goodwill = neutral_fight_help_goodwill;
						delta_reputation = neutral_fight_help_reputation;
					}
					break;
				case ALife::eRelationTypeFriend:
					{
						delta_goodwill = friend_fight_help_goodwill;
						delta_reputation = friend_fight_help_reputation;
					}
					break;
				};

				if (delta_goodwill)
				{
					//изменить отношение ко всем членам атакованой группы (если такая есть)
					//как к тому кого атаковали
					CGroupHierarchyHolder& group = Level()
					                               .seniority_holder().team(stalker->g_Team()).squad(stalker->g_Squad())
					                               .group(stalker->g_Group());
					for (std::size_t i = 0; i < group.members().size(); i++)
					{
						ChangeGoodwill(group.members()[i]->ID(), from->ID(), delta_goodwill);
					}

					//*					ChangeCommunityGoodwill(stalker->Community(), from->ID(), (CHARACTER_GOODWILL)( stalker->Sympathy() * (float)delta_goodwill ));
					CHARACTER_GOODWILL community_goodwill = (CHARACTER_GOODWILL)(stalker->Sympathy() * (float)(
						community_member_fight_help_goodwill));
					if (community_goodwill)
					{
						ChangeCommunityGoodwill(stalker->Community(), from->ID(), community_goodwill);
					}
				}

				if (delta_reputation)
				{
					inv_owner_from->ChangeReputation(delta_reputation);
				}
			}
		}
		break;
	}
}
