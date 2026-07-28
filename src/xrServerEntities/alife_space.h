////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_space.h
//	Created 	: 08.01.2002
//  Modified 	: 08.01.2003
//	Author		: Dmitriy Iassenev
//	Description : ALife space
////////////////////////////////////////////////////////////////////////////

#ifndef XRAY_ALIFE_SPACE
#define XRAY_ALIFE_SPACE
//#include "../xrcore/_std_extensions.h"

// ALife objects, events and tasks
#define ALIFE_VERSION				0x0006
#define ALIFE_CHUNK_DATA			0x0000
#define SPAWN_CHUNK_DATA			0x0001
#define OBJECT_CHUNK_DATA			0x0002
#define GAME_TIME_CHUNK_DATA		0x0005
#define REGISTRY_CHUNK_DATA			0x0009
// MP fork (§14 step 8 phase 3 Q2 / doc §7.2): the co-op quest layer's own top-level chunk —
// the offer pool, the ownership tags and the world-state bindings that ANNOTATE the task list
// CGameTaskRegistry already writes into REGISTRY_CHUNK_DATA. Its own chunk, and appended after
// the registry, so that a save WITHOUT it (every stock .scop, and every co-op save written
// before Q2) reads back as "no co-op quest state" instead of desynchronising anything: IReader
// find_chunk rewinds and scans, and returns 0 for a chunk that is not there. Listed here rather
// than hidden in GametaskManager.cpp so the next chunk id added cannot silently collide with it.
#define COOP_QUEST_CHUNK_DATA		0x00C0
// MP fork (§14 step 8 phase 4 R2 / doc §8.1): the FACTION tier of reputation. Unlike personal
// standing — which is CRelationRegistry, already inside alife_registry_container and already
// riding REGISTRY_CHUNK_DATA — faction<->faction relations live in a static table loaded from
// ltx (CHARACTER_COMMUNITY::m_relation_table), so nothing persisted them at all. R1 measured
// that rather than assuming it: a relation moved to -63 came back as its -2000 config value.
// Its own chunk for the same reason as the quest one: a save without it must read back as "no
// co-op faction state" instead of desynchronising the chunks behind it.
#define COOP_REP_CHUNK_DATA			0x00C1
#define SECTION_HEADER				"location_"
#define SAVE_EXTENSION				".scop"
#define SPAWN_NAME					"game.spawn"
// inventory rukzak size
#define MAX_ITEM_VOLUME				100
#define INVALID_STORY_ID			ALife::_STORY_ID(-1)
#define INVALID_SPAWN_STORY_ID		ALife::_SPAWN_STORY_ID(-1)

class CSE_ALifeDynamicObject;
class CSE_ALifeMonsterAbstract;
class CSE_ALifeTrader;
class CSE_ALifeInventoryItem;
class CSE_ALifeItemWeapon;
class CSE_ALifeSchedulable;
class CGameGraph;

namespace ALife
{
	typedef u64 _CLASS_ID; // Class ID
	typedef u16 _OBJECT_ID; // Object ID
	typedef u64 _TIME_ID; // Time  ID
	typedef u32 _EVENT_ID; // Event ID
	typedef u32 _TASK_ID; // Event ID
	typedef u16 _SPAWN_ID; // Spawn ID
	typedef u16 _TERRAIN_ID; // Terrain ID
	typedef u32 _STORY_ID; // Story ID
	typedef u32 _SPAWN_STORY_ID; // Spawn Story ID

	struct SSumStackCell
	{
		int i1;
		int i2;
		int iCurrentSum;
	};

	enum ECombatResult
	{
		eCombatResultRetreat1 = u32(0),
		eCombatResultRetreat2,
		eCombatResultRetreat12,
		eCombatResult1Kill2,
		eCombatResult2Kill1,
		eCombatResultBothKilled,
		eCombatDummy = u32(-1),
	};

	enum ECombatAction
	{
		eCombatActionAttack = u32(0),
		eCombatActionRetreat,
		eCombatActionDummy = u32(-1),
	};

	enum EMeetActionType
	{
		eMeetActionTypeAttack = u32(0),
		eMeetActionTypeInteract,
		eMeetActionTypeIgnore,
		eMeetActionSmartTerrain,
		eMeetActionTypeDummy = u32(-1),
	};

	enum ERelationType
	{
		eRelationTypeFriend = u32(0),
		eRelationTypeNeutral,
		eRelationTypeEnemy,
		eRelationTypeWorstEnemy,
		eRelationTypeLast,
		eRelationTypeDummy = u32(-1),
	};

	enum EHitType
	{
		eHitTypeBurn = u32(0),
		eHitTypeShock,
		eHitTypeChemicalBurn,
		eHitTypeRadiation,
		eHitTypeTelepatic,
		eHitTypeWound,
		eHitTypeFireWound,
		eHitTypeStrike,
		eHitTypeExplosion,
		eHitTypeWound_2,
		//knife's alternative fire
		//		eHitTypePhysicStrike,
		eHitTypeLightBurn,
		eHitTypeMax,
	};

	enum EInfluenceType
	{
		infl_rad = u32(0),
		infl_fire,
		infl_acid,
		infl_psi,
		infl_electra,
		infl_max_count
	};

	enum EConditionRestoreType
	{
		eHealthRestoreSpeed = u32(0),
		eSatietyRestoreSpeed,
		ePowerRestoreSpeed,
		eBleedingRestoreSpeed,
		eRadiationRestoreSpeed,
		eRestoreTypeMax,
	};

	enum ETakeType
	{
		eTakeTypeAll,
		eTakeTypeMin,
		eTakeTypeRest,
	};

	enum EWeaponPriorityType
	{
		eWeaponPriorityTypeKnife = u32(0),
		eWeaponPriorityTypeSecondary,
		eWeaponPriorityTypePrimary,
		eWeaponPriorityTypeGrenade,
		eWeaponPriorityTypeDummy = u32(-1),
	};

	enum ECombatType
	{
		eCombatTypeMonsterMonster = u32(0),
		eCombatTypeMonsterAnomaly,
		eCombatTypeAnomalyMonster,
		eCombatTypeSmartTerrain,
		eCombatTypeDummy = u32(-1),
	};

	//возможность подключения аддонов
	enum EWeaponAddonStatus
	{
		eAddonDisabled = 0,
		//нельзя присоеденить
		eAddonPermanent = 1,
		//постоянно подключено по умолчанию
		eAddonAttachable = 2 //можно присоединять
	};

	IC EHitType g_tfString2HitType(LPCSTR caHitType)
	{
		if (!_stricmp(caHitType, "burn"))
			return (eHitTypeBurn);
		else if (!_stricmp(caHitType, "light_burn"))
			return (eHitTypeLightBurn);
		else if (!_stricmp(caHitType, "shock"))
			return (eHitTypeShock);
		else if (!_stricmp(caHitType, "strike"))
			return (eHitTypeStrike);
		else if (!_stricmp(caHitType, "wound"))
			return (eHitTypeWound);
		else if (!_stricmp(caHitType, "radiation"))
			return (eHitTypeRadiation);
		else if (!_stricmp(caHitType, "telepatic"))
			return (eHitTypeTelepatic);
		else if (!_stricmp(caHitType, "fire_wound"))
			return (eHitTypeFireWound);
		else if (!_stricmp(caHitType, "chemical_burn"))
			return (eHitTypeChemicalBurn);
		else if (!_stricmp(caHitType, "explosion"))
			return (eHitTypeExplosion);
		else if (!_stricmp(caHitType, "wound_2"))
			return (eHitTypeWound_2);
		else
			FATAL("Unsupported hit type!");
		NODEFAULT;
#ifdef DEBUG
		return(eHitTypeMax);
#endif
	}
#ifndef	_EDITOR
	extern xr_token hit_types_token [ ];

	IC LPCSTR g_cafHitType2String(EHitType tHitType)
	{
		return get_token_name(hit_types_token, tHitType);
	}
#endif
	DEFINE_VECTOR(int, INT_VECTOR, INT_IT);
	DEFINE_VECTOR(_OBJECT_ID, OBJECT_VECTOR, OBJECT_IT);
	DEFINE_VECTOR(CSE_ALifeInventoryItem*, ITEM_P_VECTOR, ITEM_P_IT);
	DEFINE_VECTOR(CSE_ALifeItemWeapon*, WEAPON_P_VECTOR, WEAPON_P_IT);
	DEFINE_VECTOR(CSE_ALifeSchedulable*, SCHEDULE_P_VECTOR, SCHEDULE_P_IT);

	DEFINE_MAP(_OBJECT_ID, CSE_ALifeDynamicObject*, D_OBJECT_P_MAP, D_OBJECT_P_PAIR_IT);
	DEFINE_MAP(_STORY_ID, CSE_ALifeDynamicObject*, STORY_P_MAP, STORY_P_PAIR_IT);
};

#endif //XRAY_ALIFE_SPACE
