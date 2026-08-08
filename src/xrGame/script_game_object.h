////////////////////////////////////////////////////////////////////////////
//	Module 		: script_game_object.h
//	Created 	: 25.09.2003
//  Modified 	: 29.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Script game object class
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "script_space_forward.h"
#include "script_bind_macroses.h"
#include "script_export_space.h"
#include "xr_time.h"
#include "character_info_defs.h"
#include "game_graph_space.h"
#include "game_location_selector.h"
#include "Actor.h"
#include "Car.h"
#include "helicopter.h"
#include "InventoryOwner.h"
#include "InventoryBox.h"
#include "CustomZone.h"
#include "TorridZone.h"
#include "MosquitoBald.h"
#include "ZoneCampfire.h"
#include "CustomOutfit.h"
#include "ActorHelmet.h"
#include "Artefact.h"
#include "Weapon.h"
#include "WeaponAmmo.h"
#include "WeaponMagazined.h"
#include "WeaponMagazinedWGrenade.h"
#include "eatable_item.h"
#include "FoodItem.h"
#include "medkit.h"
#include "antirad.h"
#include "BottleItem.h"
#include "Missile.h"
#include "WeaponKnife.h"

#ifdef PROJECTOR_NEW
#include "searchlight.h"
#endif

enum EPdaMsg;
enum ESoundTypes;
enum ETaskState;

namespace ALife { enum ERelationType; }
namespace ScriptEntity { enum EActionType; }
namespace MovementManager { enum EPathType; }
namespace DetailPathManager { enum EDetailPathType; }
namespace SightManager { enum ESightType; }
namespace smart_cover { class object; }
namespace doors { class door; }

class NET_Packet;
class CGameTask;

namespace PatrolPathManager {
	enum EPatrolStartType;
	enum EPatrolRouteType;
};

namespace MemorySpace {
	struct CMemoryInfo;
	struct CVisibleObject;
	struct CSoundObject;
	struct CHitObject;
	struct CNotYetVisibleObject;
};

namespace MonsterSpace {
	enum EBodyState;
	enum EMovementType;
	enum EMovementDirection;
	enum EDirectionType;
	enum EPathState;
	enum EObjectAction;
	enum EMentalState;
	enum EScriptMonsterMoveAction;
	enum EScriptMonsterSpeedParam;
	enum EScriptMonsterAnimAction;
	enum EScriptMonsterGlobalAction;
	enum EScriptSoundAnim;
	enum EMonsterSounds;
	enum EMonsterHeadAnimType;
	struct SBoneRotation;
};

namespace GameObject {
	enum ECallbackType;
};

class CGameObject;
class CScriptHit;
class CScriptEntityAction;
class CScriptTask;
class CScriptSoundInfo;
class CScriptMonsterHitInfo;
class CScriptBinderObject;
class CCoverPoint;
class CScriptIniFile;
class cphysics_shell_scripted;
class CHelicopter;
class CHangingLamp;
class CHolderCustom;
struct ScriptCallbackInfo;
struct STasks;
class CCar;
class CDangerObject;
class CScriptGameObject;
class CZoneCampfire;
class CPhysicObject;
class CArtefact;
class script_attachment;

#ifdef STATIONARYMGUN_NEW
class CWeaponStatMgun;
#endif
#ifdef PROJECTOR_NEW
class CProjector;
#endif

#ifdef DEBUG
    template <typename _object_type>
    class CActionBase;

    template <typename _object_type>
    class CPropertyEvaluator;

    template <
        typename _object_type,
        bool	 _reverse_search,
        typename _world_operator,
        typename _condition_evaluator,
        typename _world_operator_ptr,
        typename _condition_evaluator_ptr
    >
    class CActionPlanner;

    typedef CActionPlanner<
        CScriptGameObject,
        false,
        CActionBase<CScriptGameObject>,
        CPropertyEvaluator<CScriptGameObject>,
        CActionBase<CScriptGameObject>*,
        CPropertyEvaluator<CScriptGameObject>*
    >								script_planner;
#endif // DEBUG

class CScriptGameObject;

namespace SightManager
{
	enum ESightType;
}

struct CSightParams
{
	SightManager::ESightType m_sight_type;
	CScriptGameObject* m_object;
	Fvector m_vector;
};

namespace luabind
{
	template <typename return_type>
	class functor;

	class object;
} // namespace luabind

class CScriptGameObject
{
	mutable CGameObject* m_game_object;
	CScriptGameObject(CScriptGameObject const& game_object);

public:

	CScriptGameObject(CGameObject* tpGameObject);
	virtual ~CScriptGameObject();
	operator CObject*();

    // MP fork: the raw backing pointer, for the guard's decision log. DELIBERATELY DOES NOT
    // DEREFERENCE — printing what is_valid() saw must not repeat the fault it is diagnosing.
    // A pointer VALUE is safe to read and print no matter what it points at.
    IC const void* coop_raw_backing() const { return (const void*)m_game_object; }

    // MP fork (orphan-destroy crash): retire this proxy WITHOUT freeing it.
    //
    // CGameObject::net_Destroy used to `xr_delete(m_lua_game_object)`, which nulls only the OWNER's
    // pointer — every luabind userdata in Lua still held a raw pointer to the freed block. So
    // `instance->is_valid()` inside SafeWrap was a member call on freed memory, reading
    // m_game_object out of a dead 24-byte allocation before it could decide anything. An object
    // cannot validate its own existence from inside itself.
    //
    // Abandoning it instead keeps every Lua-held pointer pointing at readable memory whose
    // m_game_object is genuinely NULL, so is_valid()'s FIRST check — `if (!m_game_object)` — is
    // reached with NO dereference of anything. That is the only check in the chain that was ever
    // safe, and it is now the one that fires.
    //
    // Deliberate leak: 24 bytes (two pointers + vtable) per destroyed object, ~24 KB per 1000.
    // Recorded in dev/ORPHAN_DESTROY_CRASH.md so the leak axis does not rediscover it and
    // misattribute it. A freelist is the obvious refinement and is deliberately not in v1.
    void coop_abandon();

    // MP fork — READ THIS BEFORE TRUSTING is_valid().
    //
    // It detects an UNSPAWNED object. It does NOT reliably detect a FREED one, and the two are
    // different states with the same name in conversation:
    //
    //   still allocated, m_spawned == false -> lua_game_object() returns NULL -> caught. This is
    //     the state that printed "you are trying to use a destroyed object" 8 times on 2026-08-07.
    //   FREED -> m_game_object dangles -> reading m_spawned reads freed memory. If that garbage is
    //     nonzero it falls through and returns m_lua_game_object, which — while the allocation has
    //     not been reused — STILL EQUALS `this`. So the back-reference check passes and this
    //     returns TRUE for an object that no longer exists.
    //
    // The comment below claims the back-reference catches a dangling pointer. It only does so once
    // something has OVERWRITTEN the freed block; on not-yet-reused memory it reads back the old,
    // self-consistent values and passes. Measured: build 31232366798 crashed reading
    // 0xFFFFFFFFFFFFFFFF — a read through freed memory, not the *(CGameObject*)NULL path — which
    // means object() took `return (*m_game_object)`, which means this returned TRUE.
    IC bool is_valid() const
    {
        // If the pointer was never set, it's obviously invalid.
        if (!m_game_object) return false;

        // Check lua game object pointer back-reference. If it doesn't point to this, then the object was likely deleted and the pointer is now dangling.
        if (m_game_object->lua_game_object() != this) return false;

        return true;
    }

	CGameObject& object() const;
	CScriptGameObject* Parent() const;
	void Hit(CScriptHit* tLuaHit);
	int clsid() const;
	void play_cycle(LPCSTR anim, bool mix_in);
	void play_cycle(LPCSTR anim);
	Fvector Center(bool bHud = false);
	Fmatrix Xform(bool bHud = false);
	Fbox bounding_box(bool bHud);
	_DECLARE_FUNCTION10(Position, Fvector);
	_DECLARE_FUNCTION10(Direction, Fvector);
	_DECLARE_FUNCTION10(Mass, float);
	_DECLARE_FUNCTION10(ID, u16);
	_DECLARE_FUNCTION10(getVisible, BOOL);
	_DECLARE_FUNCTION10(getEnabled, BOOL);
	_DECLARE_FUNCTION10(story_id, ALife::_STORY_ID);

	LPCSTR Name() const;
	shared_str cName() const;
	LPCSTR Section() const;
	// CInventoryItem
	u32 Cost() const;
	float GetCondition() const;
	void SetCondition(float val);
	float GetPowerCritical() const;
	float GetPsyFactor() const;
	void SetPsyFactor(float val);

	// Added by Ncenka - allow turn on/off devices
	_DECLARE_FUNCTION10(IsDeviceEnabled, bool);
	_DECLARE_FUNCTION11(SetDeviceEnabled, void, bool);

	// CEntity
	_DECLARE_FUNCTION10(DeathTime, u32);
	_DECLARE_FUNCTION10(MaxHealth, float);
	_DECLARE_FUNCTION10(Accuracy, float);
	_DECLARE_FUNCTION10(Team, int);
	_DECLARE_FUNCTION10(Squad, int);
	_DECLARE_FUNCTION10(Group, int);

	void Kill(CScriptGameObject* who, bool bypass_actor_check = false /*AVO: added for actor before death callback*/);

	// CEntityAlive
	_DECLARE_FUNCTION10(GetFOV, float);
	_DECLARE_FUNCTION10(GetRange, float);

	_DECLARE_FUNCTION10(GetHealth, float);
	_DECLARE_FUNCTION11(SetHealth, void, float);
	_DECLARE_FUNCTION11(ChangeHealth, void, float);

	_DECLARE_FUNCTION10(GetPsyHealth, float);
	_DECLARE_FUNCTION11(SetPsyHealth, void, float);
	_DECLARE_FUNCTION11(ChangePsyHealth, void, float);

	_DECLARE_FUNCTION10(GetPower, float);
	_DECLARE_FUNCTION11(SetPower, void, float);
	_DECLARE_FUNCTION11(ChangePower, void, float);

	_DECLARE_FUNCTION10(GetSatiety, float);
	_DECLARE_FUNCTION11(SetSatiety, void, float);
	_DECLARE_FUNCTION11(ChangeSatiety, void, float);

	_DECLARE_FUNCTION10(GetRadiation, float);
	_DECLARE_FUNCTION11(SetRadiation, void, float);
	_DECLARE_FUNCTION11(ChangeRadiation, void, float);

	_DECLARE_FUNCTION10(GetMorale, float);
	_DECLARE_FUNCTION11(SetMorale, void, float);
	_DECLARE_FUNCTION11(ChangeMorale, void, float);

	_DECLARE_FUNCTION10(GetBleeding, float);
	_DECLARE_FUNCTION11(ChangeBleeding, void, float);

	_DECLARE_FUNCTION11(ChangeCircumspection, void, float);

	void set_fov(float new_fov);
	void set_range(float new_range);
	bool Alive() const;
	bool coop_is_dead() const;
	float coop_respawn_timer() const;
	bool coop_locally_driven() const;
	void coop_set_locally_driven(bool value);
	ALife::ERelationType GetRelationType(CScriptGameObject* who);

	// CScriptEntity

	_DECLARE_FUNCTION12(SetScriptControl, void, bool, LPCSTR);
	_DECLARE_FUNCTION10(GetScriptControl, bool);
	_DECLARE_FUNCTION10(GetScriptControlName, LPCSTR);
	_DECLARE_FUNCTION10(GetEnemyStrength, int);
	_DECLARE_FUNCTION10(can_script_capture, bool);


	CScriptEntityAction* GetCurrentAction() const;
	void AddAction(const CScriptEntityAction* tpEntityAction, bool bHighPriority = false);
	void ResetActionQueue();
	// Actor only
	void SetActorPosition(Fvector pos, bool bskip_collision_correct = false, bool bkeep_speed = false);	// momopate: allow movespeed to be kept if bskip_collision_correct == true
	void SetActorDirection(float dir);
	void SetActorDirection(float dir, float pitch);
	void SetActorDirection(float dir, float pitch, float roll);
	void SetActorDirection(const Fvector& dir);
	void SetNpcPosition(Fvector pos);
	void DisableHitMarks(bool disable);
	bool DisableHitMarks() const;
	Fvector GetMovementSpeed() const;
	void SetMovementSpeed(Fvector vel);		// momopate: db.actor:set_movement_speed(vector vel)

	// CCustomMonster
	bool CheckObjectVisibility(const CScriptGameObject* tpLuaGameObject);
	bool CheckTypeVisibility(const char* section_name);
	// MP fork (§4C headless-visibility diag): dump this NPC's feel_vision internals vs an explicit
	// target (no enemy-selection dependency). Bound as game_object:vissdbg(target).
	void VisDbg(const CScriptGameObject* tpLuaGameObject);
	LPCSTR WhoHitName();
	LPCSTR WhoHitSectionName();

	void ChangeTeam(u8 team, u8 squad, u8 group);
	void SetVisualMemoryEnabled(bool enabled);
    float GetObjectVisibleDistance(const CScriptGameObject* obj);
    float GetObjectLuminocity(const CScriptGameObject* obj);

	// CAI_Stalker
	CScriptGameObject* GetCurrentWeapon() const;
	CScriptGameObject* GetFood() const;
	CScriptGameObject* GetMedikit() const;
	void SetPlayShHdRldSounds(bool val);

	void set_force_anti_aim(bool force);
	bool get_force_anti_aim();

	// Burer
	void burer_set_force_gravi_attack(bool force);
	bool burer_get_force_gravi_attack();

	// Poltergeist
	void poltergeist_set_actor_ignore(bool ignore);
	bool poltergeist_get_actor_ignore();

	// CAI_Bloodsucker
	void force_visibility_state(int state);
	int get_visibility_state();

	// CBaseMonster
	void set_override_animation(pcstr anim_name);
	void set_override_animation(u32 AnimType, u32 AnimIndex);
	void clear_override_animation();

	void force_stand_sleep_animation(u32 index);
	void release_stand_sleep_animation();

	void set_invisible(bool val);
	bool get_invisible();
	void set_manual_invisibility(bool val);
	void set_alien_control(bool val);
	void set_enemy(CScriptGameObject* e);
	CScriptGameObject* monster_enemy(); // MP fork (§4C): CBaseMonster::EnemyMan.get_enemy() (best_enemy() is stalker-only)
	void set_vis_state(float value);
	void off_collision(bool val);
	void bloodsucker_drag_jump(CScriptGameObject* e, LPCSTR e_str, const Fvector& position, float factor);

	// Zombie
	bool fake_death_fall_down();
	void fake_death_stand_up();

	// CBaseMonster
	void skip_transfer_enemy(bool val);
	void set_home(LPCSTR name, float r_min, float r_max, bool aggressive, float r_mid);
	void set_home(u32 lv_ID, float r_min, float r_max, bool aggressive, float r_mid);
	void remove_home();
	void berserk();
	void set_custom_panic_threshold(float value);
	void set_default_panic_threshold();

	// CAI_Trader
	void set_trader_global_anim(LPCSTR anim);
	void set_trader_head_anim(LPCSTR anim);
	void set_trader_sound(LPCSTR sound, LPCSTR anim);
	void external_sound_start(LPCSTR sound);
	void external_sound_stop();


	template <typename T>
	IC T* action_planner();

	// CProjector
	Fvector GetCurrentDirection();

	bool IsInvBoxEmpty();
	bool inv_box_closed(bool status, LPCSTR reason);
	bool inv_box_closed_status();
	bool inv_box_can_take(bool status);
	bool inv_box_can_take_status();

	//передача порции информации InventoryOwner
	bool GiveInfoPortion(LPCSTR info_id);
	bool DisableInfoPortion(LPCSTR info_id);
	void GiveGameNews(LPCSTR caption, LPCSTR news, LPCSTR texture_name, int delay, int show_time);
	void GiveGameNews(LPCSTR caption, LPCSTR news, LPCSTR texture_name, int delay, int show_time, int type);

	void AddIconedTalkMessage_old(LPCSTR text, LPCSTR texture_name, LPCSTR templ_name)
	{
	};
	void AddIconedTalkMessage(LPCSTR caption, LPCSTR text, LPCSTR texture_name, LPCSTR templ_name);
	//предикаты наличия/отсутствия порции информации у персонажа
	bool HasInfo(LPCSTR info_id);
	bool DontHasInfo(LPCSTR info_id);
	//работа с заданиями
	ETaskState GetGameTaskState(LPCSTR task_id);
	void SetGameTaskState(ETaskState state, LPCSTR task_id);
	void GiveTaskToActor(CGameTask* t, u32 dt, bool bCheckExisting, u32 t_timer);
	void SetActiveTask(CGameTask* t);
	bool IsActiveTask(CGameTask* t);
	CGameTask* GetTask(LPCSTR id, bool only_inprocess);


	bool IsTalking();
	void StopTalk();
	void EnableTalk();
	void DisableTalk();
	bool IsTalkEnabled();

	void EnableTrade();
	void DisableTrade();
	bool IsTradeEnabled();

	void EnableInvUpgrade();
	void DisableInvUpgrade();
	bool IsInvUpgradeEnabled();


	void ActorLookAtPoint(Fvector point);
	void ActorStopLookAtPoint();
	void IterateInventory(::luabind::functor<bool> functor, ::luabind::object object);
	void IterateRuck(::luabind::functor<bool> functor, ::luabind::object object);
	void IterateBelt(::luabind::functor<bool> functor, ::luabind::object object);
	void IterateInventoryBox(::luabind::functor<bool> functor, ::luabind::object object);
	void MarkItemDropped(CScriptGameObject* item, bool flag);
	bool MarkedDropped(CScriptGameObject* item);
	void UnloadMagazine(bool bKeepAmmo);
	void ForceUnloadMagazine(bool bKeepAmmo);

	void SetCanBeHarmed(bool state);
	bool CanBeHarmed();

	void DropItem(CScriptGameObject* pItem);
	void DropItemAndTeleport(CScriptGameObject* pItem, Fvector position);
	void ForEachInventoryItems(const ::luabind::functor<bool>& functor);
	void TransferItem(CScriptGameObject* pItem, CScriptGameObject* pForWho);
	void TakeItem(CScriptGameObject* pItem);
	void TransferMoney(int money, CScriptGameObject* pForWho);
	void GiveMoney(int money);
	u32 Money();
	void MakeItemActive(CScriptGameObject* pItem);
	void MoveItemToRuck(CScriptGameObject* pItem);
	void MoveItemToSlot(CScriptGameObject* pItem, u16 slot_id);
	void MoveItemToBelt(CScriptGameObject* pItem);
	void ItemAllowTrade(CScriptGameObject* pItem);
	void ItemDenyTrade(CScriptGameObject* pItem);

	void SetRelation(ALife::ERelationType relation, CScriptGameObject* pWhoToSet);

	float GetSympathy();
	void SetSympathy(float sympathy);

	int GetCommunityGoodwill_obj(LPCSTR community);
	void SetCommunityGoodwill_obj(LPCSTR community, int goodwill);

	int GetAttitude(CScriptGameObject* pToWho);

	int GetGoodwill(CScriptGameObject* pToWho);
	void SetGoodwill(int goodwill, CScriptGameObject* pWhoToSet);
	void ForceSetGoodwill(int goodwill, CScriptGameObject* pWhoToSet);
	void ChangeGoodwill(int delta_goodwill, CScriptGameObject* pWhoToSet);


	void SetStartDialog(LPCSTR dialog_id);
	void GetStartDialog();
	void RestoreDefaultStartDialog();

	void SwitchToTrade();
	void SwitchToUpgrade();
	void SwitchToTalk();
	void RunTalkDialog(CScriptGameObject* pToWho, bool disable_break);
	void AllowBreakTalkDialog(bool disable_break);

	void HideWeapon();
	void RestoreWeapon();
	void AllowSprint(bool b);

	bool Weapon_IsGrenadeLauncherAttached();
	bool Weapon_IsScopeAttached();
	bool Weapon_IsSilencerAttached();

	int Weapon_GrenadeLauncher_Status();
	int Weapon_Scope_Status();
	int Weapon_Silencer_Status();

	LPCSTR ProfileName();
	LPCSTR CharacterName();
	LPCSTR CharacterIcon();
	LPCSTR CharacterCommunity();
	::luabind::object CharacterDialogs();
	int CharacterRank();
	int CharacterReputation();


	void SetCharacterRank(int);
	void ChangeCharacterRank(int);
	void ChangeCharacterReputation(int);
	void SetCharacterReputation(int);
	void SetCharacterCommunity(LPCSTR, int, int);


	u32 GetInventoryObjectCount() const;

	CScriptGameObject* GetActiveItem();

	CScriptGameObject* GetObjectByName(LPCSTR caObjectName) const;
	CScriptGameObject* GetObjectByIndex(int iIndex) const;
	CScriptGameObject * GetObjectById(u16 id) const;


	// Callbacks			
	void SetCallback(GameObject::ECallbackType type, const ::luabind::functor<void>& functor);
	void SetCallback(GameObject::ECallbackType type, const ::luabind::functor<void>& functor,
	                 const ::luabind::object& object);
	void SetCallback(GameObject::ECallbackType type);

	void set_patrol_extrapolate_callback(const ::luabind::functor<bool>& functor);
	void set_patrol_extrapolate_callback(const ::luabind::functor<bool>& functor, const ::luabind::object& object);
	void set_patrol_extrapolate_callback();

	void set_enemy_callback(const ::luabind::functor<bool>& functor);
	void set_enemy_callback(const ::luabind::functor<bool>& functor, const ::luabind::object& object);
	void set_enemy_callback();

	//////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////use calback///////////////////////////////////////////////
	void SetTipText(LPCSTR tip_text);
	void SetTipTextDefault();
	void SetNonscriptUsable(bool nonscript_usable);
	///////////////////////////////////////////////////////////////////////////////////////////
	void set_fastcall(const ::luabind::functor<bool>& functor, const ::luabind::object& object);
	void set_const_force(const Fvector& dir, float value, u32 time_interval);
	//////////////////////////////////////////////////////////////////////////

	LPCSTR GetPatrolPathName();
	u32 GetAmmoElapsed();
	void SetAmmoElapsed(int ammo_elapsed);
	u32 GetSuitableAmmoTotal() const;
	void SetQueueSize(u32 queue_size);
	CScriptGameObject* GetBestEnemy();
	const CDangerObject* GetBestDanger();
	CScriptGameObject* GetBestItem();

	_DECLARE_FUNCTION10(GetActionCount, u32);

	const CScriptEntityAction* GetActionByIndex(u32 action_index = 0);

	//////////////////////////////////////////////////////////////////////////
	// Inventory Owner
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	Flags32 get_actor_relation_flags() const;
	void set_actor_relation_flags(Flags32);
	LPCSTR sound_voice_prefix() const;

	//////////////////////////////////////////////////////////////////////////
	u32 memory_time(const CScriptGameObject& lua_game_object);
	Fvector memory_position(const CScriptGameObject& lua_game_object);
	CScriptGameObject* best_weapon();
	void explode(u32 level_time);
	CScriptGameObject* GetEnemy() const;
	CScriptGameObject* GetCorpse() const;
	CScriptSoundInfo GetSoundInfo();
	CScriptMonsterHitInfo GetMonsterHitInfo();
	void bind_object(CScriptBinderObject* object);
	CScriptGameObject* GetCurrentOutfit() const;
	float GetCurrentOutfitProtection(int hit_type);

	bool IsOnBelt(CScriptGameObject* obj) const;
	CScriptGameObject* ItemOnBelt(u32 item_id) const;
	u32 BeltSize() const;

	void deadbody_closed(bool status);
	bool deadbody_closed_status();
	void deadbody_can_take(bool status);
	bool deadbody_can_take_status();

	void can_select_weapon(bool status);
	bool can_select_weapon() const;
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	void set_body_state(MonsterSpace::EBodyState body_state);
	void set_movement_type(MonsterSpace::EMovementType movement_type);
	void set_mental_state(MonsterSpace::EMentalState mental_state);
	void set_path_type(MovementManager::EPathType path_type);
	void set_detail_path_type(DetailPathManager::EDetailPathType detail_path_type);

	MonsterSpace::EBodyState body_state() const;
	MonsterSpace::EBodyState target_body_state() const;
	MonsterSpace::EMovementType movement_type() const;
	MonsterSpace::EMovementType target_movement_type() const;
	MonsterSpace::EMentalState mental_state() const;
	MonsterSpace::EMentalState target_mental_state() const;
	MovementManager::EPathType path_type() const;
	DetailPathManager::EDetailPathType detail_path_type() const;

	u32 add_sound(LPCSTR prefix, u32 max_count, ESoundTypes type, u32 priority, u32 mask, u32 internal_type,
	              LPCSTR bone_name);
	u32 add_sound(LPCSTR prefix, u32 max_count, ESoundTypes type, u32 priority, u32 mask, u32 internal_type);
	u32 add_sound(LPCSTR prefix, u32 max_count, ESoundTypes type, u32 priority, u32 mask, u32 internal_type,
	              LPCSTR bone_name, LPCSTR head_anim);
	u32 add_combat_sound(LPCSTR prefix, u32 max_count, ESoundTypes type, u32 priority, u32 mask, u32 internal_type,
	                     LPCSTR bone_name);
	void remove_sound(u32 internal_type);
	void set_sound_mask(u32 sound_mask);
	void set_sight(SightManager::ESightType sight_type, Fvector* vector3d, u32 dwLookOverDelay);
	void set_sight(SightManager::ESightType sight_type, bool torso_look, bool path);
	void set_sight(SightManager::ESightType sight_type, Fvector& vector3d, bool torso_look);
	void set_sight(SightManager::ESightType sight_type, Fvector* vector3d);
	void set_sight(CScriptGameObject* object_to_look);
	void set_sight(CScriptGameObject* object_to_look, bool torso_look);
	void set_sight(CScriptGameObject* object_to_look, bool torso_look, bool fire_object);
	void set_sight(CScriptGameObject* object_to_look, bool torso_look, bool fire_object, bool no_pitch);
	void set_sight(const MemorySpace::CMemoryInfo* memory_object, bool torso_look);
	CHARACTER_RANK_VALUE GetRank();
	LPCSTR GetRankName();
	bool affect_cover() const;
	void best_cover_invalidate();
	LPCSTR GetCurrentSmartCoverName();
	LPCSTR GetCurrentLoopholeId();
	void play_sound(u32 internal_type);
	void play_sound(u32 internal_type, u32 max_start_time);
	void play_sound(u32 internal_type, u32 max_start_time, u32 min_start_time);
	void play_sound(u32 internal_type, u32 max_start_time, u32 min_start_time, u32 max_stop_time);
	void play_sound(u32 internal_type, u32 max_start_time, u32 min_start_time, u32 max_stop_time, u32 min_stop_time);
	void play_sound(u32 internal_type, u32 max_start_time, u32 min_start_time, u32 max_stop_time, u32 min_stop_time,
	                u32 id);

	void set_item(MonsterSpace::EObjectAction object_action);
	void set_item(MonsterSpace::EObjectAction object_action, CScriptGameObject* game_object);
	void set_item(MonsterSpace::EObjectAction object_action, CScriptGameObject* game_object, u32 queue_size);
	void set_item(MonsterSpace::EObjectAction object_action, CScriptGameObject* game_object, u32 queue_size,
	              u32 queue_interval);
	void set_desired_position();
	void set_desired_position(const Fvector* desired_position);
	void set_desired_direction();
	void set_desired_direction(const Fvector* desired_direction);
	void set_patrol_path(LPCSTR path_name, const PatrolPathManager::EPatrolStartType patrol_start_type,
	                     const PatrolPathManager::EPatrolRouteType patrol_route_type, bool random);
	void inactualize_patrol_path();
	void set_dest_level_vertex_id(u32 level_vertex_id);
	void set_dest_game_vertex_id(GameGraph::_GRAPH_ID game_vertex_id);
	void set_movement_selection_type(ESelectionType selection_type);
	u32 level_vertex_id() const;
	u32 game_vertex_id() const;
	void add_animation(LPCSTR animation, bool hand_usage, bool use_movement_controller);
	void add_animation(LPCSTR animation, bool hand_usage, Fvector position, Fvector rotation, bool local_animation);
	void clear_animations();
	int animation_count() const;
	int animation_slot() const;
	CScriptBinderObject* binded_object();
	void set_previous_point(int point_index);
	void set_start_point(int point_index);
	u32 get_current_patrol_point_index();
	bool path_completed() const;
	void patrol_path_make_inactual();
	void extrapolate_length(float extrapolate_length);
	float extrapolate_length() const;
	void enable_memory_object(CScriptGameObject* object, bool enable);
	int active_sound_count();
	int active_sound_count(bool only_playing);
	const CCoverPoint* best_cover(const Fvector& position, const Fvector& enemy_position, float radius,
	                              float min_enemy_distance, float max_enemy_distance);
	const CCoverPoint* safe_cover(const Fvector& position, float radius, float min_distance);
	CScriptIniFile* spawn_ini() const;
	bool active_zone_contact(u16 id);

	///
	void add_restrictions(LPCSTR out, LPCSTR in);
	void remove_restrictions(LPCSTR out, LPCSTR in);
	void remove_all_restrictions();
	LPCSTR in_restrictions();
	LPCSTR out_restrictions();
	LPCSTR base_in_restrictions();
	LPCSTR base_out_restrictions();
	bool accessible_position(const Fvector& position);
	bool accessible_vertex_id(u32 level_vertex_id);
	u32 accessible_nearest(const Fvector& position, Fvector& result);

	const xr_vector<MemorySpace::CVisibleObject>& memory_visible_objects() const;
	const xr_vector<MemorySpace::CSoundObject>& memory_sound_objects() const;
	const xr_vector<MemorySpace::CHitObject>& memory_hit_objects() const;
	const xr_vector<MemorySpace::CNotYetVisibleObject>& not_yet_visible_objects() const;
	float visibility_threshold() const;
	void enable_vision(bool value);
	bool vision_enabled() const;
	void set_sound_threshold(float value);
	void restore_sound_threshold();
	//////////////////////////////////////////////////////////////////////////
	void enable_attachable_item(bool value);
	bool attachable_item_enabled() const;
	void enable_night_vision(bool value);
	void night_vision_allowed(bool value);
	bool night_vision_enabled() const;
	void enable_torch(bool value);
	bool torch_enabled() const;

	// VodoXleb: add force update for torch for npc
	void update_torch();

	void attachable_item_load_attach(LPCSTR section);
	// CustomZone
	void EnableAnomaly();
	void DisableAnomaly();
    bool IsEnabledAnomaly();
	void ChangeAnomalyIdlePart(LPCSTR name, bool bIdleLight);
	float GetAnomalyPower();
	void SetAnomalyPower(float p);
	float GetAnomalyRadius();
	void SetAnomalyRadius(float p);
	void MoveAnomaly(Fvector pos);

	// HELICOPTER
	CHelicopter* get_helicopter();
	//CAR
	CCar* get_car();
#ifdef STATIONARYMGUN_NEW
	CWeaponStatMgun *get_stmgun();
#endif
#ifdef PROJECTOR_NEW
	CProjector *get_projector();
#endif
	//LAMP
	CHangingLamp* get_hanging_lamp();

	//Torch
	void set_color_animator(LPCSTR name, bool bFlicker, int flickerChance, float flickerDelay, int framerate);
	void reset_color_animator();

	CHolderCustom* get_custom_holder();
	CHolderCustom* get_current_holder(); //actor only

	void start_particles(LPCSTR pname, LPCSTR bone);
	void stop_particles(LPCSTR pname, LPCSTR bone);

	bool is_body_turning() const;
	cphysics_shell_scripted* get_physics_shell() const;
	bool weapon_strapped() const;
	bool weapon_unstrapped() const;
	void eat(CScriptGameObject* item);
	bool inside(const Fvector& position, float epsilon) const;
	bool inside(const Fvector& position) const;

	Fvector head_orientation() const;
	u32 vertex_in_direction(u32 level_vertex_id, Fvector direction, float max_distance) const;

	void info_add(LPCSTR text);
	void info_clear();

	// Monster Jumper
	void jump(const Fvector& position, float factor);

	void set_ignore_monster_threshold(float ignore_monster_threshold);
	void restore_ignore_monster_threshold();
	float ignore_monster_threshold() const;
	void set_max_ignore_monster_distance(const float& max_ignore_monster_distance);
	void restore_max_ignore_monster_distance();
	float max_ignore_monster_distance() const;

	void make_object_visible_somewhen(CScriptGameObject* object);

	CScriptGameObject* item_in_slot(u32 slot_id) const;
	CScriptGameObject* active_device() const;
	void show_device(bool bFast);
	void hide_device(bool bFast);
	void force_hide_device();
	u32 active_slot();
	void activate_slot(u32 slot_id);
	void enable_level_changer(bool b);
	bool is_level_changer_enabled();
	void set_level_changer_invitation(LPCSTR str);
#ifdef DEBUG
            void				debug_planner						(const script_planner *planner);
#endif

	void sell_condition(CScriptIniFile* ini_file, LPCSTR section);
	void sell_condition(float friend_factor, float enemy_factor);
	void buy_condition(CScriptIniFile* ini_file, LPCSTR section);
	void buy_condition(float friend_factor, float enemy_factor);
	void show_condition(CScriptIniFile* ini_file, LPCSTR section);
	void buy_supplies(CScriptIniFile* ini_file, LPCSTR section);
	void buy_item_condition_factor(float factor);
	void buy_item_exponent(float factor);
	void sell_item_exponent(float factor);

	LPCSTR sound_prefix() const;
	void sound_prefix(LPCSTR sound_prefix);

	u32 location_on_path(float distance, Fvector* location);
	bool is_there_items_to_pickup() const;

	bool wounded() const;
	void wounded(bool value);

	void set_enable_movement_collision(bool value);

	CSightParams sight_params();

	void enable_movement(bool enable);
	bool movement_enabled();

	bool critically_wounded();

	bool invulnerable() const;
	void invulnerable(bool invulnerable);
	LPCSTR get_smart_cover_description() const;
	void set_visual_name(LPCSTR visual, bool bForce);
	float get_current_weight();
	float get_max_weight();
	LPCSTR get_visual_name() const;

	void reload_weapon();

	bool can_throw_grenades() const;
	void can_throw_grenades(bool can_throw_grenades);

	u32 throw_time_interval() const;
	void throw_time_interval(u32 throw_time_interval);

	u32 group_throw_time_interval() const;
	void group_throw_time_interval(u32 throw_time_interval);
	CArtefact* get_artefact();
	CZoneCampfire* get_campfire();
	CPhysicObject* get_physics_object();

	void aim_time(CScriptGameObject* weapon, u32 time);
	u32 aim_time(CScriptGameObject* weapon);

	void special_danger_move(bool value);
	bool special_danger_move();

	void sniper_update_rate(bool value);
	bool sniper_update_rate() const;

	void sniper_fire_mode(bool value);
	bool sniper_fire_mode() const;

	void set_aim_params(float max_angle, float min_angle, float min_speed, float predict_time);
	void set_fire_queue_scale(float size_k, float interval_k);
	void set_vision_speed(float value);
	bool can_kill_enemy();
	bool can_kill_member();
	bool fire_make_sense();

	void aim_bone_id(LPCSTR value);
	LPCSTR aim_bone_id() const;

	void register_in_combat();
	void unregister_in_combat();
	CCoverPoint const* find_best_cover(Fvector position_to_cover_from);

	// approved by Dima smart covers functions
	bool use_smart_covers_only() const;
	void use_smart_covers_only(bool value);

	bool in_smart_cover() const;

	void set_dest_smart_cover(LPCSTR cover_id);
	void set_dest_smart_cover();
	CCoverPoint const* get_dest_smart_cover();
	LPCSTR get_dest_smart_cover_name();

	void set_dest_loophole(LPCSTR loophole_id);
	void set_dest_loophole();

	void set_smart_cover_target(Fvector position);
	void set_smart_cover_target(CScriptGameObject* object);
	void set_smart_cover_target();

	void set_smart_cover_target_selector();
	void set_smart_cover_target_selector(::luabind::functor<void> functor);
	void set_smart_cover_target_selector(::luabind::functor<void> functor, ::luabind::object object);

	void set_smart_cover_target_idle();
	void set_smart_cover_target_lookout();
	void set_smart_cover_target_fire();
	void set_smart_cover_target_fire_no_lookout();
	void set_smart_cover_target_default(bool value);

	float const idle_min_time() const;
	void idle_min_time(float value);
	float const idle_max_time() const;
	void idle_max_time(float value);
	float const lookout_min_time() const;
	void lookout_min_time(float value);
	float const lookout_max_time() const;
	void lookout_max_time(float value);

	bool in_loophole_fov(LPCSTR cover_id, LPCSTR loophole_id, Fvector object_position) const;
	bool in_current_loophole_fov(Fvector object_position) const;
	bool in_loophole_range(LPCSTR cover_id, LPCSTR loophole_id, Fvector object_position) const;
	bool in_current_loophole_range(Fvector object_position) const;

	float apply_loophole_direction_distance() const;
	void apply_loophole_direction_distance(float value);

	bool movement_target_reached();
	bool suitable_smart_cover(CScriptGameObject* object);

	void take_items_enabled(bool value);
	bool take_items_enabled() const;

	void death_sound_enabled(bool value);
	bool death_sound_enabled() const;

	void register_door();
	void unregister_door();
	void on_door_is_open();
	void on_door_is_closed();
	bool is_door_locked_for_npc() const;
	void lock_door_for_npc();
	void unlock_door_for_npc();
	bool is_door_blocked_by_npc() const;
	bool is_weapon_going_to_be_strapped(CScriptGameObject const* object) const;

    ::luabind::object g_fireParams();

#ifdef GAME_OBJECT_TESTING_EXPORTS
	//AVO: functions for object testing
	//_DECLARE_FUNCTION10(IsGameObject, bool);
	//_DECLARE_FUNCTION10(IsCar, bool);
	//_DECLARE_FUNCTION10(IsHeli, bool);
	//_DECLARE_FUNCTION10(IsHolderCustom, bool);
	_DECLARE_FUNCTION10(IsEntityAlive, bool);
	_DECLARE_FUNCTION10(IsInventoryItem, bool);
	_DECLARE_FUNCTION10(IsInventoryOwner, bool);
	_DECLARE_FUNCTION10(IsActor, bool);
	_DECLARE_FUNCTION10(IsCustomMonster, bool);
	_DECLARE_FUNCTION10(IsWeapon, bool);
	//_DECLARE_FUNCTION10(IsMedkit, bool);
	//_DECLARE_FUNCTION10(IsEatableItem, bool);
	//_DECLARE_FUNCTION10(IsAntirad, bool);
	_DECLARE_FUNCTION10(IsCustomOutfit, bool);
	_DECLARE_FUNCTION10(IsHelmet, bool);
	_DECLARE_FUNCTION10(IsScope, bool);
	_DECLARE_FUNCTION10(IsSilencer, bool);
	_DECLARE_FUNCTION10(IsGrenadeLauncher, bool);
	_DECLARE_FUNCTION10(IsWeaponMagazined, bool);
	_DECLARE_FUNCTION10(IsSpaceRestrictor, bool);
	_DECLARE_FUNCTION10(IsStalker, bool);
	_DECLARE_FUNCTION10(IsAnomaly, bool);
	_DECLARE_FUNCTION10(IsMonster, bool);
	//_DECLARE_FUNCTION10(IsExplosive, bool);
	//_DECLARE_FUNCTION10(IsScriptZone, bool);
	//_DECLARE_FUNCTION10(IsProjector, bool);
	_DECLARE_FUNCTION10(IsTrader, bool);
	_DECLARE_FUNCTION10(IsHudItem, bool);
	//_DECLARE_FUNCTION10(IsFoodItem, bool);
	_DECLARE_FUNCTION10(IsArtefact, bool);
	_DECLARE_FUNCTION10(IsAmmo, bool);
	//_DECLARE_FUNCTION10(IsMissile, bool);
	//_DECLARE_FUNCTION10(IsPhysicsShellHolder, bool);
	//_DECLARE_FUNCTION10(IsGrenade, bool);
	//_DECLARE_FUNCTION10(IsBottleItem, bool);
	//_DECLARE_FUNCTION10(IsTorch, bool);
	_DECLARE_FUNCTION10(IsWeaponGL, bool);
	_DECLARE_FUNCTION10(IsInventoryBox, bool);
#endif
	//Alundaio
#ifdef GAME_OBJECT_EXTENDED_EXPORTS
	_DECLARE_FUNCTION14(cast_Actor, CActor);
	_DECLARE_FUNCTION14(cast_Car, CCar);
	_DECLARE_FUNCTION14(cast_Heli, CHelicopter);
	_DECLARE_FUNCTION14(cast_InventoryOwner, CInventoryOwner);
	_DECLARE_FUNCTION14(cast_InventoryBox, CInventoryBox);
	_DECLARE_FUNCTION14(cast_CustomZone, CCustomZone);
	_DECLARE_FUNCTION14(cast_TorridZone, CTorridZone);
	_DECLARE_FUNCTION14(cast_MosquitoBald, CMosquitoBald);
	_DECLARE_FUNCTION14(cast_ZoneCampfire, CZoneCampfire);
	_DECLARE_FUNCTION14(cast_InventoryItem, CInventoryItem);
	_DECLARE_FUNCTION14(cast_CustomOutfit, CCustomOutfit);
	_DECLARE_FUNCTION14(cast_Helmet, CHelmet);
	_DECLARE_FUNCTION14(cast_Artefact, CArtefact);
	_DECLARE_FUNCTION14(cast_Ammo, CWeaponAmmo);
	_DECLARE_FUNCTION14(cast_Weapon, CWeapon);
	_DECLARE_FUNCTION14(cast_Knife, CWeaponKnife);
	_DECLARE_FUNCTION14(cast_WeaponMagazined, CWeaponMagazined);
	_DECLARE_FUNCTION14(cast_WeaponMagazinedWGrenade, CWeaponMagazinedWGrenade);
	_DECLARE_FUNCTION14(cast_EatableItem, CEatableItem);
	_DECLARE_FUNCTION14(cast_Medkit, CMedkit);
	_DECLARE_FUNCTION14(cast_Antirad, CAntirad);
	_DECLARE_FUNCTION14(cast_FoodItem, CFoodItem);
	_DECLARE_FUNCTION14(cast_BottleItem, CBottleItem);
	_DECLARE_FUNCTION14(cast_Missile, CMissile);
	_DECLARE_FUNCTION14(cast_Explosive, CExplosive);

	void SetHealthEx(float hp); //AVO
	float GetLuminocityHemi();
	float GetLuminocity();
	bool Use(CScriptGameObject* obj);
	void StartTrade(CScriptGameObject* obj);
	void StartUpgrade(CScriptGameObject* obj);
	void SetWeight(float w);
	void IterateFeelTouch(::luabind::functor<void> functor);
	u32 GetSpatialType();
	void DestroyObject();
	void SetSpatialType(u32 sptype);
	u8 GetRestrictionType();
	void SetRestrictionType(u8 typ);

	// demonized: SetRestrictionType with unregistering restrictor if type is 0
	void ForceSetRestrictionType(u8 typ);
	void InvalidateRestrictions();

	// demonized: add getters and setters for pathfinding for npcs around anomalies and damage for npcs
	bool get_enable_anomalies_pathfinding();
	void set_enable_anomalies_pathfinding(bool v);
	bool get_enable_anomalies_damage();
	void set_enable_anomalies_damage(bool v);

	// priler: returns true if a non-radioactive restrictor zone is currently touching this character
	bool inside_anomaly();

	//Weapon
	void Weapon_AddonAttach(CScriptGameObject* item);
	void Weapon_AddonDetach(LPCSTR item_section, bool b_spawn_item = true);
	bool HasAmmoType(u8 type);
	int GetAmmoCount(u8 type);
	void SetAmmoType(u8 type);
	void SetMainWeaponType(u32 type);
	void SetWeaponType(u32 type);
	u32 GetMainWeaponType();
	u32 GetWeaponType();
	u8 GetWeaponSubstate();
	u8 GetAmmoType();

	//Scope
	void Weapon_SetCurrentScope(u8 type);
	u8 Weapon_GetCurrentScope();

	//CWeaponAmmo
	u16 AmmoGetCount();
	void AmmoSetCount(u16 count);
	u16 AmmoBoxSize();

	//Weapon & Outfit
	bool InstallUpgrade(LPCSTR upgrade);
	bool HasUpgrade(LPCSTR upgrade);
	void IterateInstalledUpgrades(const ::luabind::functor<bool>& functor);
	bool WeaponInGrenadeMode();

	//Car
	CScriptGameObject* GetAttachedVehicle();
	void AttachVehicle(CScriptGameObject* veh, bool bForce = false);
	void DetachVehicle(bool bForce = false);

	//Any class that is derived from CHudItem
	u32 PlayHudMotion(LPCSTR M, bool bMixIn, u32 state, float speed = 0.f, float end = 0.f);
	void SwitchState(u32 state);
	u32 GetState();
	Fvector hud_fire_point();
	Fvector hud_fire_point2();
	Fvector hud_fire_point_silencer();
	void set_hud_fire_point(Fvector value);
	void set_hud_fire_point2(Fvector value);
	void set_hud_fire_point_silencer(Fvector value);
	u16 hud_fire_bone();
	u16 hud_fire_bone2();
	u16 hud_fire_bone_silencer();
	LPCSTR hud_fire_bone_name();
	LPCSTR hud_fire_bone2_name();
	LPCSTR hud_fire_bone_silencer_name();
	void set_hud_fire_bone(u16 bone_id);
	void set_hud_fire_bone(LPCSTR bone_name);
	void set_hud_fire_bone2(u16 bone_id);
	void set_hud_fire_bone2(LPCSTR bone_name);
	void set_hud_fire_bone_silencer(u16 bone_id);
	void set_hud_fire_bone_silencer(LPCSTR bone_name);
	//Works for anything with visual
	u16 bone_id(LPCSTR bone_name, bool bHud);
	u16 bone_id(LPCSTR bone_name) { return bone_id(bone_name, false); }
	LPCSTR bone_name(u16 bone_id, bool bHud);
	LPCSTR bone_name(u16 bone_id) { return bone_name(bone_id, false); }

	bool is_bone_visible(u16 bone_id, bool bHud);
	bool is_bone_visible(u16 bone_id) { return is_bone_visible(bone_id, false); }
	bool is_bone_visible(LPCSTR bone_name, bool bHud) { return is_bone_visible(bone_id(bone_name, bHud), bHud); }
	bool is_bone_visible(LPCSTR bone_name) { return is_bone_visible(bone_id(bone_name), false); }

	void set_bone_visible(u16 bone_id, bool bVisibility, bool bRecursive, bool bHud);
	void set_bone_visible(u16 bone_id, bool bVisibility, bool bRecursive) { set_bone_visible(bone_id, bVisibility, bRecursive, false); }
	void set_bone_visible(LPCSTR bone_name, bool bVisibility, bool bRecursive, bool bHud) { set_bone_visible(bone_id(bone_name, bHud), bVisibility, bRecursive, bHud); }
	void set_bone_visible(LPCSTR bone_name, bool bVisibility, bool bRecursive) { set_bone_visible(bone_id(bone_name), bVisibility, bRecursive, false); }

	Fmatrix bone_transform(u16 bone_id, bool bHud);
	Fmatrix bone_transform(u16 bone_id) { return bone_transform(bone_id, false); }
	Fmatrix bone_transform(LPCSTR bone_name, bool bHud) { return bone_transform(bone_id(bone_name, bHud), bHud); }
	Fmatrix bone_transform(LPCSTR bone_name) { return bone_transform(bone_id(bone_name), false); }

	Fvector bone_position(u16 bone_id, bool bHud);
	Fvector bone_position(u16 bone_id) { return bone_position(bone_id, false); }
	Fvector bone_position(LPCSTR bone_name, bool bHud) { return bone_position(bone_id(bone_name, bHud), bHud); }
	Fvector bone_position(LPCSTR bone_name) { return bone_position(bone_id(bone_name), false); }

	Fvector bone_direction(u16 bone_id, bool bHud);
	Fvector bone_direction(u16 bone_id) { return bone_direction(bone_id, false); }
	Fvector bone_direction(LPCSTR bone_name, bool bHud) { return bone_direction(bone_id(bone_name, bHud), bHud); }
	Fvector bone_direction(LPCSTR bone_name) { return bone_direction(bone_id(bone_name), false); }

	u16 bone_parent(u16 bone_id, bool bHud);
	u16 bone_parent(u16 bone_id) { return bone_parent(bone_id, false); }
	u16 bone_parent(LPCSTR bone_name, bool bHud) { return bone_parent(bone_id(bone_name, bHud), bHud); }
	u16 bone_parent(LPCSTR bone_name) { return bone_parent(bone_id(bone_name), false); }

	::luabind::object list_bones(bool bHud = false);

	bool IsBoneVisible(LPCSTR bone_name, bool bHud = false);	
	void SetBoneVisible(LPCSTR bone_name, bool bVisibility, bool bRecursive = true, bool bHud = false);	
	//CAI_Stalker
	void ResetBoneProtections(LPCSTR imm_sect, LPCSTR bone_sect);
	//Anything with PPhysicShell (ie. car, actor, stalker, monster, heli)
	void ForceSetPosition(Fvector pos, bool enable = true);
	void ForceSetRotation(Fvector rot, bool enable = true);
	void ForceSetAngle(Fvector ang, bool bActivate);
	Fvector Angle();

	//Artifacts
	float GetArtefactHealthRestoreSpeed();
	float GetArtefactRadiationRestoreSpeed();
	float GetArtefactSatietyRestoreSpeed();
	float GetArtefactPowerRestoreSpeed();
	float GetArtefactBleedingRestoreSpeed();
	float GetArtefactImmunity(ALife::EHitType hit_type);

	void SetArtefactHealthRestoreSpeed(float value);
	void SetArtefactRadiationRestoreSpeed(float value);
	void SetArtefactSatietyRestoreSpeed(float value);
	void SetArtefactPowerRestoreSpeed(float value);
	void SetArtefactBleedingRestoreSpeed(float value);
	void SetArtefactImmunity(ALife::EHitType hit_type, float value);

	float GetArtefactAdditionalInventoryWeight();
	void SetArtefactAdditionalInventoryWeight(float value);

	//Eatable items
	void SetRemainingUses(u8 value);
	u8 GetRemainingUses();
	u8 GetMaxUses();

	//Phantom
	void PhantomSetEnemy(CScriptGameObject*);

	//Actor

	float GetActorMaxWeight() const;
	void SetActorMaxWeight(float max_weight);
	float GetActorMaxWalkWeight() const;
	void SetActorMaxWalkWeight(float max_walk_weight);
	float GetAdditionalMaxWeight() const;
	void SetAdditionalMaxWeight(float add_max_weight);
	float GetAdditionalMaxWalkWeight() const;
	void SetAdditionalMaxWalkWeight(float add_max_walk_weight);
	float GetTotalWeight() const;

	// demonized: force update of weight
	float GetTotalWeightForceUpdate() const;
	void UpdateWeight() const;

	float Weight() const;

	// demonized: get luminosity as displayed in ui
	float GetActorUILuminosity();

	float GetActorJumpSpeed() const;
	void SetActorJumpSpeed(float jump_speed);
	float GetActorSprintKoef() const;
	void SetActorSprintKoef(float sprint_koef);
	float GetActorRunCoef() const;
	void SetActorRunCoef(float run_coef);
	float GetActorRunBackCoef() const;
	void SetActorRunBackCoef(float run_back_coef);
	void SetActorCamBoxYOffset(u32 box_num, float offset);
	float GetActorWalkAccel() const;
	void SetActorWalkAccel(float val);
	float GetActorWalkBackCoef() const;
	void SetActorWalkBackCoef(float val);

	// demonized: Adjust Lookout coeff
	float GetActorLookoutCoef() const;
	void SetActorLookoutCoef(float val);

	float GetActorCrouchCoef() const;
	void SetActorCrouchCoef(float val);
	float GetActorClimbCoef() const;
	void SetActorClimbCoef(float val);
	float GetActorWalkStrafeCoef() const;
	void SetActorWalkStrafeCoef(float val);
	float GetActorRunStrafeCoef() const;
	void SetActorRunStrafeCoef(float val);
	float GetActorSprintStrafeCoef() const;
	void SetActorSprintStrafeCoef(float val);

	CScriptGameObject* GetActorObjectLookingAt();
	CScriptGameObject* GetActorPersonLookingAt();
	LPCSTR GetActorDefaultActionForObject();

	void SetCharacterIcon(LPCSTR iconName);

	// demonized: get talking npc
	CScriptGameObject* get_talking_npc();

	// demonized: get and set scope UI
	::luabind::object get_scope_ui();
	void set_scope_ui(LPCSTR scope_texture);
#endif
	//-Alundaio

	::luabind::object GetShaders(bool bHud = false);
	::luabind::object GetDefaultShaders(bool bHud = false);
	void SetShaderTexture(int id, LPCSTR shader, LPCSTR texture, bool bHud = false);
	void ResetShaderTexture(int id, bool bHud = false);

	script_attachment* AddAttachment(LPCSTR name, LPCSTR model_name);
	script_attachment* GetAttachment(LPCSTR name);
	void RemoveAttachment(LPCSTR name);
	void RemoveAttachment(script_attachment* child);
	void IterateAttachments(::luabind::functor<bool> functor);
	void memory_remove_links(const CScriptGameObject* tpLuaGameObject);

	doors::door* m_door;

DECLARE_SCRIPT_REGISTER_FUNCTION
};

extern BOOL lua_busy_hands_debug;
// MP fork: gates SafeWrap REFUSING a call with an invalid object-typed argument. FALSE by default;
// see console_commands.cpp for why it must stay that way until the handle_invalid strategy is settled.
extern BOOL coop_safewrap_block_bad_args;
extern BOOL coop_safewrap_block_bad_receiver;   // DEFAULT ON — see console_commands.cpp for why the two defaults differ
extern u32 g_coop_arg_log_budget;   // re-armed at the orphan expiry; see console_commands.cpp
extern xr_vector<xr_string> get_lua_stack(lua_State* L);

// Default: Assume the class DOES NOT have is_valid()
template <typename T, typename = void>
struct has_is_valid : std::false_type {};

// Specialization: If T->is_valid() compiles, this becomes true
template <typename T>
struct has_is_valid<T, std::void_t<decltype(std::declval<T>()->is_valid())>> : std::true_type {};

// MP fork: same trait for the guard's decision log, so instances without a backing pointer to
// report still compile.
template <typename T, typename = void>
struct has_coop_raw_backing : std::false_type {};
template <typename T>
struct has_coop_raw_backing<T, std::void_t<decltype(std::declval<T>()->coop_raw_backing())>> : std::true_type {};

struct SafeWrapBase
{
    // MP fork: this is now REACHED. Upstream defined it, commented it "never reached because we
    // crash the game", and never called it from anywhere — `execute()` detected the invalid
    // instance, logged, and then made the call regardless. See the note on execute() below.
    template <typename Ret>
    static Ret handle_invalid()
    {
        if constexpr (std::is_reference_v<Ret>)
        {
            // A reference return has nothing honest to bind to. Bind it to a shared
            // default-constructed referent rather than to NULL: every SAFE_WRAP'd reference
            // return today is a `const xr_vector<...>&` memory list, for which "empty" is the
            // right answer for an object that no longer exists. A referent that cannot be
            // default-constructed fails to COMPILE here, which is the loud failure we want
            // rather than a silent null reference.
            static std::remove_const_t<std::remove_reference_t<Ret>> s_empty{};
            return s_empty;
        }
        else
        {
            // Scalars, pointers and void. For the LPCSTR returns (Name/Section/...) this is
            // nullptr, and LuaJIT's lua_pushstring maps a NULL const char* to nil
            // ("3rd party/luajit-2/src/lj_api.c":594) — so Lua sees nil, not a fault.
            return Ret();
        }
    }

    static void log(LPCSTR error)
    {
        // MP fork: ALSO emit through Msg. This message routed only through script_log, and no
        // "[BusyHandsDebug]" line has ever been observed in a dedicated-server log — while
        // "you are trying to use a destroyed object", which uses plain Msg, appears reliably in
        // the same logs. So a guard reporting only through script_log is indistinguishable from a
        // guard that never fired, which is exactly the ambiguity that made run 31232366798
        // uninterpretable. Same channel as the message we know arrives.
        Msg("! COOP(safewrap): %s", error);
        ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "[BusyHandsDebug] Error: %s", error);
    }

    // MP fork — THE DECISION LOG. Emit WHAT THE GUARD SAW, not whether it passed.
    //
    // A booleanised "guard OK" cannot separate the three states that demand opposite work:
    //
    //   no REACHED line at all  -> execute() is not on this path. The call came through one of the
    //                              ~279 CScriptGameObject bindings bound RAW rather than SAFE_WRAP'd,
    //                              or lua_busy_hands_debug is off. Fix = coverage.
    //   REACHED but never SKIP  -> the guard runs and is_valid() keeps saying VALID. Detection is
    //                              failing, not coverage. Wrapping more bindings would change
    //                              NOTHING. Fix = make is_valid able to see a freed object.
    //   SKIP lines present      -> the guard fires and returns. Any remaining crash is on some
    //                              other path. Fix = coverage, and now with evidence.
    //
    // Bounded on purpose: these bindings are called thousands of times a second, so an unbounded
    // Msg would bury the log it is meant to make readable (and this project has already measured
    // its own trace dominating a memory-rate reading). The REACHED cap only has to prove the code
    // is live, so it is small; SKIP is the interesting one, so it gets more.
    static void log_decision(const void* instance, const void* backing, bool valid)
    {
        if (valid)
        {
            static u32 s_reached = 0;
            if (++s_reached <= 20)
                Msg("- COOP(safewrap): REACHED #%u instance=%p backing=%p decision=CALL "
                    "(is_valid said VALID)", s_reached, instance, backing);
            return;
        }
        static u32 s_skipped = 0;
        if (++s_skipped <= 200)
            Msg("! COOP(safewrap): SKIP #%u instance=%p backing=%p decision=RETURN-WITHOUT-CALLING "
                "(is_valid said INVALID)", s_skipped, instance, backing);
    }

    static void log_and_callback(LPCSTR error)
    {
        log(error);
        ai().script_engine().lua_error_not_crash(ai().script_engine().lua());
    }

    // MP fork — ARGUMENT validation. SafeWrap guarded the RECEIVER and forwarded args... untouched,
    // so a dead object arriving as an ARGUMENT walked straight past a guard built for a dead object
    // arriving as `this`. Measured live: build 31234649782 survived its receiver checks and still
    // faulted at 0xE4 inside GetRelationType, on `&who->object()`
    // (script_game_object_use.cpp:200) — a SAFE_WRAP'd binding, guard passed, dead argument.
    //
    // This is a strictly easier question than the receiver one was. is_valid() was impossible
    // because it asked an object about itself from inside itself; here we ask the CALLER about a
    // pointer it is holding, before the callee runs. And it is only answerable at all because
    // coop_abandon() leaves retired proxies READABLE with a NULL backing — validating an argument
    // against a freed block would have been the same undefined behaviour as before.
    //
    // Overloads, not `if constexpr`, so any argument type that is not a game object compiles to a
    // literal `true` and costs nothing.
    // UNTESTED AS OF 2026-08-07 23:50 — built, not yet run. See dev/ORPHAN_DESTROY_CRASH.md.
    //
    // v1 of this was `p->coop_raw_backing() != nullptr` and it MEASURABLY DID NOTHING: build
    // 31236417639 fired this guard 3729 times and still faulted at the identical address as the
    // build before it (GetRelationType+0x311, 0xE4, same stack, same single SAFE_WRAP'd binding).
    //
    // The reason is that a non-null backing is a WEAKER condition than the one object() requires.
    // object() returns *(CGameObject*)NULL when EITHER `!m_game_object` OR
    // `m_game_object->lua_game_object() != this` — and v1 only tested the first. A proxy whose
    // backing is live but no longer points back at it sailed through the guard and faulted anyway.
    //
    // is_valid() tests exactly the pair object() tests, so use it. It is no more dangerous than the
    // call it prevents: is_valid() dereferences m_game_object once, and object() performs that SAME
    // read before going on to the second, fatal one. Strictly dominating — same exposure, stops
    // earlier.
    //
    // The lesson, for the next person tempted to hand-roll a cheaper check: I wrote a weaker
    // predicate than the one already sitting next to it, out of caution about a dereference that
    // was going to happen anyway one line later.
    // Logs EVERY object-typed argument and what was seen of it — on the good branch as well as the
    // bad one. The previous version printed the RECEIVER's pointers on both branches, so an
    // argument-branch line said nothing about the argument, and this run could not separate
    // "is_valid() said VALID here and object() disagreed a moment later" (TOCTOU) from "this call
    // never went through execute()" (a second path). Those want opposite work, so the log has to
    // make them look different: TOCTOU shows an ARG line with valid=1 immediately before the fault;
    // a second path shows NO ARG line for the faulting call at all.
    static bool coop_arg_ok(const CScriptGameObject* p)
    {
        const bool ok = !p || p->is_valid();
        static u32 s_n = 0;
        ++s_n;
        if (g_coop_arg_log_budget > 0)
        {
            --g_coop_arg_log_budget;
            Msg("%c COOP(safewrap): ARG #%u arg=%p backing=%p valid=%d", ok ? '-' : '!', s_n,
                (const void*)p, p ? p->coop_raw_backing() : nullptr, ok ? 1 : 0);
            if (g_coop_arg_log_budget == 0)
                Msg("! COOP(safewrap): ARG LOG BUDGET EXHAUSTED at #%u — any ABSENCE of ARG lines "
                    "after this point is THIS CEILING, not evidence the guard was absent", s_n);
        }
        return ok;
    }
    template <typename T> static bool coop_arg_ok(const T&) { return true; }

    // This generic function accepts ANY instance type (const or non-const)
    // and ANY member function pointer type.
    template <typename InstanceT, typename FuncT, typename... Args>
    static auto execute(InstanceT instance, FuncT memFunc, Args&&... args)
        -> decltype((instance->*memFunc)(std::forward<Args>(args)...))
    {
        if (lua_busy_hands_debug)
        {
            bool is_valid = false;

            if (instance != nullptr)
            {
                if constexpr (has_is_valid<InstanceT>::value)
                    // The class has is_valid(), so we use it
                    is_valid = instance->is_valid();
                else
                    // The class does NOT have is_valid(), so being non-null is good enough
                    is_valid = true;
            }

            // MP fork (orphan-destroy crash, 2026-08-07): upstream logged here and then FELL
            // THROUGH to the call. `is_valid()` is exactly right — it caught our case seven
            // times in the run that died — but detecting is not preventing, and the call it
            // then made was the crash.
            //
            // CScriptGameObject::object() returns `*(CGameObject*)NULL` for a destroyed object
            // in a RELEASE build (the THROW2 that would stop it is #ifdef DEBUG), so the
            // member read that follows faults at a small offset off zero. Measured: Name()
            // faulted at 0x00E4 and Section() at 0x00EC — a delta of exactly 8, matching the
            // adjacent `shared_str NameObject; shared_str NameSection;` pair declared in that
            // order in xrEngine/xr_object.h. That killed the dedicated server when the fault
            // landed on a path with no handler above it (coop_autosave -> ALife save -> Lua).
            //
            // So: return the invalid-instance value instead of making the call. Lua gets nil /
            // 0 / an empty list and can be wrong; the process stays up and says why.
            // Report the DECISION before acting on it, so "the guard never ran" and "the guard ran
            // and said VALID" stop being the same silence. See log_decision() for the three states.
            {
                const void* backing = nullptr;
                if constexpr (has_coop_raw_backing<InstanceT>::value)
                    if (instance != nullptr)
                        backing = instance->coop_raw_backing();
                log_decision((const void*)instance, backing, is_valid);
            }

            if (!is_valid)
            {
                log_and_callback("Accessing destroyed object");
                // Gated so BOTH halves of this guard are switchable rather than one being invisible.
                // Default ON: it is what makes the server survive the destroy. The residual hazard
                // (the returned nil reaching an unguarded script site) is real and documented, but
                // turning this off restores a RELIABLE crash in place of an occasional one.
                if (coop_safewrap_block_bad_receiver)
                    return handle_invalid<decltype((instance->*memFunc)(std::forward<Args>(args)...))>();
            }

            // A live receiver can still be handed a dead ARGUMENT.
            //
            // COMMA fold, not `&&`: `&&` short-circuits, so the first bad argument would suppress
            // the log line for every argument after it — and the log is the whole point of this
            // pass. Every argument is evaluated and reported.
            bool args_ok = true;
            ((args_ok = coop_arg_ok(args) && args_ok), ...);

            // REFUSING THE CALL IS GATED OFF BY DEFAULT — this is the v2 regression, contained.
            // Returning handle_invalid<Ret>() here hands Lua nil; on build 31239986429 that nil
            // reached a GAMMA script, which raised a script error, which our own handler
            // (script_engine.cpp:327) treats as FATAL for an unprotected luabind call. A single
            // recoverable AV became a truncated process death with NO AV — quieter and harder to
            // diagnose than the defect it was meant to fix.
            //
            // With the flag off, behaviour is identical to build 31234649782, the best-known
            // configuration (PASS: server survived the destroy, one post-expiry AV). Detection and
            // logging above still run, so the next run yields the evidence without the regression.
            if (!args_ok && coop_safewrap_block_bad_args)
            {
                log_and_callback("Passing a destroyed object as an argument");
                return handle_invalid<decltype((instance->*memFunc)(std::forward<Args>(args)...))>();
            }
        }

        return (instance->*memFunc)(std::forward<Args>(args)...);
    }
};

// The primary template (just a declaration)
template <typename FuncSignature, FuncSignature MemFunc>
struct SafeWrap;

// 1. Generalized specialization for NON-CONST methods
template <typename T, typename Ret, typename... Args, Ret(T::* MemFunc)(Args...)>
struct SafeWrap<Ret(T::*)(Args...), MemFunc> : SafeWrapBase
{
    // T is deduced as the class (e.g., CScriptGameObject)
    using type = Ret(*)(T*, Args...);

    static Ret call(T* instance, Args... args)
    {
        return execute(instance, MemFunc, std::forward<Args>(args)...);
    }
};

// 2. Generalized specialization for CONST methods
template <typename T, typename Ret, typename... Args, Ret(T::* MemFunc)(Args...) const>
struct SafeWrap<Ret(T::*)(Args...) const, MemFunc> : SafeWrapBase
{
    // T is deduced as the class, but we use const T* for the instance
    using type = Ret(*)(const T*, Args...);

    static Ret call(const T* instance, Args... args)
    {
        return execute(instance, MemFunc, std::forward<Args>(args)...);
    }
};

#define SAFE_WRAP(func) static_cast<typename SafeWrap<decltype(func), func>::type>(SafeWrap<decltype(func), func>::call)

extern void sell_condition(CScriptIniFile* ini_file, LPCSTR section);
extern void sell_condition(float friend_factor, float enemy_factor);
extern void buy_condition(CScriptIniFile* ini_file, LPCSTR section);
extern void buy_condition(float friend_factor, float enemy_factor);
extern void show_condition(CScriptIniFile* ini_file, LPCSTR section);
