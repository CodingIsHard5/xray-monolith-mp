#include "stdafx.h"
#include "LevelGameDef.h"
#include "ai_space.h"
#include "ParticlesObject.h"
#include "script_process.h"
#include "script_engine.h"
#include "script_engine_space.h"
#include "level.h"
#include "game_cl_base.h"
#include "../xrEngine/x_ray.h"
#include "../xrEngine/gamemtllib.h"
#include "../xrphysics/PhysicsCommon.h"
#include "level_sounds.h"
#include "GamePersistent.h"
#include "../xrEngine/Rain.h"
#include "character_community.h"
#include "character_rank.h"
#include "character_reputation.h"
#include "monster_community.h"
#include "HudManager.h"
#include "game_graph.h"                            // MP fork (§19): standalone game graph
#include "../xrNetServer/xr_enet_transport.h"      // MP fork (§19): xr_enet::enabled()

extern ENGINE_API bool g_dedicated_server;

// MP fork (§19 co-op): the standalone game graph a thin client builds for itself.
// CGameGraph does NOT copy the stream — it stores raw pointers into the chunk's buffer
// (game_graph_inline.h: m_nodes = stream.pointer()), which is why the server keeps its
// spawn file and chunk alive as members. Mirror that: these must outlive the graph.
static IReader* s_coop_spawn_file = nullptr;
static IReader* s_coop_gg_chunk = nullptr;
static CGameGraph* s_coop_game_graph = nullptr;

static void coop_release_game_graph()
{
	if (s_coop_gg_chunk) { s_coop_gg_chunk->close(); s_coop_gg_chunk = nullptr; }
	if (s_coop_spawn_file) { FS.r_close(s_coop_spawn_file); s_coop_spawn_file = nullptr; }
	xr_delete(s_coop_game_graph);
}

bool CLevel::Load_GameSpecific_Before()
{
	// AI space
	//	g_pGamePersistent->LoadTitle		("st_loading_ai_objects");
	g_pGamePersistent->LoadTitle();
	string_path fn_game;

	// MP fork (§19 co-op thin client): replicated creatures never rendered because the AI
	// space was never loaded here, for two reasons:
	//  1) the gate below required GameType()==eGameIDSingle, but a co-op client only learns
	//     its game type from M_SV_CONFIG_NEW_CLIENT (Export_game_type), which the server
	//     sends from OnCL_Connected — i.e. AFTER this runs. It is not Single yet.
	//  2) ai().load() VERIFYs a game graph, but the graph is built inside A-Life's spawn
	//     registry, which a thin client (no simulator) never runs.
	// Without level_graph/cross_table, CCustomMonster::net_Spawn fails (movement=1,
	// inherited=0) and every NPC is dropped. Build the graph ourselves from the same spawn
	// file chunk the server uses, then let the AI space load.
	const bool coop_thin = xr_enet::enabled() && !ai().get_alife();
	if (coop_thin && !ai().get_game_graph())
	{
		coop_release_game_graph();
		string_path spawn_fn;
		if (FS.exist(spawn_fn, "$game_spawn$", "all", ".spawn"))
		{
			s_coop_spawn_file = FS.r_open(spawn_fn);
			if (s_coop_spawn_file)
			{
				s_coop_gg_chunk = s_coop_spawn_file->open_chunk(4); // chunk 4 == game graph
				if (s_coop_gg_chunk)
				{
					s_coop_game_graph = xr_new<CGameGraph>(*s_coop_gg_chunk);
					ai().game_graph(s_coop_game_graph);
					Msg("- XRNET(dbg): co-op client loaded standalone game graph (%u vertices, %u levels)",
						(u32)s_coop_game_graph->header().vertex_count(),
						(u32)s_coop_game_graph->header().level_count());
				}
				else
					Msg("! XRNET(dbg): co-op game graph: spawn chunk 4 missing in '%s'", spawn_fn);
			}
			else
				Msg("! XRNET(dbg): co-op game graph: cannot open spawn file '%s'", spawn_fn);
		}
		else
			Msg("! XRNET(dbg): co-op game graph: no $game_spawn$ all.spawn found");
	}

	const bool want_ai_space =
		(GamePersistent().GameType() == eGameIDSingle && !net_Hosts.empty()) ||
		(coop_thin && ai().get_game_graph());

	if (want_ai_space && !ai().get_alife() && FS.exist(fn_game, "$level$", "level.ai"))
	{
		ai().load(net_SessionName());
		if (coop_thin)
			Msg("- XRNET(dbg): co-op client AI space loaded for '%s' (creatures can now spawn)",
				net_SessionName());
	}

	if (!g_dedicated_server && !ai().get_alife() && ai().get_game_graph() && FS.exist(fn_game, "$level$", "level.game"))
	{
		IReader* stream = FS.r_open(fn_game);
		ai().patrol_path_storage_raw(*stream);
		FS.r_close(stream);
	}

	CHARACTER_COMMUNITY::Reset();
	CHARACTER_RANK::Reset();
	CHARACTER_REPUTATION::Reset();
	MONSTER_COMMUNITY::Reset();

	return (TRUE);
}

bool CLevel::Load_GameSpecific_After()
{
	R_ASSERT(m_StaticParticles.empty());
	// loading static particles
	string_path fn_game;
	if (FS.exist(fn_game, "$level$", "level.ps_static"))
	{
		IReader* F = FS.r_open(fn_game);
		CParticlesObject* pStaticParticles;
		u32 chunk = 0;
		string256 ref_name;
		Fmatrix transform;
		Fvector zero_vel = {0.f, 0.f, 0.f};
		u32 ver = 0;
		for (IReader* OBJ = F->open_chunk_iterator(chunk); OBJ; OBJ = F->open_chunk_iterator(chunk, OBJ))
		{
			if (chunk == 0)
			{
				if (OBJ->length() == sizeof(u32))
				{
					ver = OBJ->r_u32();
#ifndef MASTER_GOLD
					Msg		("PS new version, %d", ver);
#endif // #ifndef MASTER_GOLD
					continue;
				}
			}
			u16 gametype_usage = 0;
			if (ver > 0)
			{
				gametype_usage = OBJ->r_u16();
			}
			OBJ->r_stringZ(ref_name, sizeof(ref_name));
			OBJ->r(&transform, sizeof(Fmatrix));
			transform.c.y += 0.01f;


			if ((g_pGamePersistent->m_game_params.m_e_game_type & EGameIDs(gametype_usage)) || (ver == 0))
			{
				pStaticParticles = CParticlesObject::Create(ref_name,FALSE, false);
				pStaticParticles->UpdateParent(transform, zero_vel);
				pStaticParticles->Play(false);
				m_StaticParticles.push_back(pStaticParticles);
			}
		}
		FS.r_close(F);
	}

	if (!g_dedicated_server)
	{
		// loading static sounds
		VERIFY(m_level_sound_manager);
		m_level_sound_manager->Load();

		// loading sound environment
		if (FS.exist(fn_game, "$level$", "level.snd_env"))
		{
			IReader* F = FS.r_open(fn_game);
			::Sound->set_geometry_env(F);
			FS.r_close(F);
		}
		else
		{
			// demonized: reset sound environment if the map doesn't have it, so that the next map won't be using environment of the previous one
			::Sound->set_geometry_env(nullptr);
		}
		// loading SOM
		if (FS.exist(fn_game, "$level$", "level.som"))
		{
			IReader* F = FS.r_open(fn_game);
			::Sound->set_geometry_som(F);
			FS.r_close(F);
		}
		else
		{
			// demonized: same here
			::Sound->set_geometry_som(nullptr);
		}

		// loading random (around player) sounds
		if (pSettings->section_exist("sounds_random"))
		{
			CInifile::Sect& S = pSettings->r_section("sounds_random");
			Sounds_Random.reserve(S.Data.size());
			for (CInifile::SectCIt I = S.Data.begin(); S.Data.end() != I; ++I)
			{
				Sounds_Random.push_back(ref_sound());
				Sound->create(Sounds_Random.back(), *I->first, st_Effect, sg_SourceType);
			}
			Sounds_Random_dwNextTime = Device.TimerAsync() + 50000;
			Sounds_Random_Enabled = FALSE;
		}

		if (g_pGamePersistent->pEnvironment)
		{
			if (CEffect_Rain* rain = g_pGamePersistent->pEnvironment->eff_Rain)
			{
				rain->InvalidateState();
			}
		}

		if (FS.exist(fn_game, "$level$", "level.fog_vol"))
		{
			IReader* F = FS.r_open(fn_game);
			u16 version = F->r_u16();
			if (version == 2)
			{
				u32 cnt = F->r_u32();

				Fmatrix volume_matrix;
				for (u32 i = 0; i < cnt; ++i)
				{
					F->r(&volume_matrix, sizeof(volume_matrix));
					u32 sub_cnt = F->r_u32();
					for (u32 is = 0; is < sub_cnt; ++is)
					{
						F->r(&volume_matrix, sizeof(volume_matrix));
					}
				}
			}
			FS.r_close(F);
		}
	}

	// dedicated too: level scripts are the A-Life logic this server exists to run
	{
		// loading scripts
		ai().script_engine().remove_script_process(ScriptEngine::eScriptProcessorLevel);

		if (pLevel->section_exist("level_scripts") && pLevel->line_exist("level_scripts", "script"))
			ai().script_engine().add_script_process(ScriptEngine::eScriptProcessorLevel,
			                                        xr_new<CScriptProcess>(
				                                        "level", pLevel->r_string("level_scripts", "script")));
		else
			ai().script_engine().add_script_process(ScriptEngine::eScriptProcessorLevel,
			                                        xr_new<CScriptProcess>("level", ""));
	}

	BlockCheatLoad();

	g_pGamePersistent->Environment().SetGameTime(GetEnvironmentGameDayTimeSec(), game->GetEnvironmentGameTimeFactor());

	HUD().SetRenderable(true);

	return TRUE;
}

struct translation_pair
{
	u32 m_id;
	u16 m_index;

	IC translation_pair(u32 id, u16 index)
	{
		m_id = id;
		m_index = index;
	}

	IC bool operator==(const u16& id) const
	{
		return (m_id == id);
	}

	IC bool operator<(const translation_pair& pair) const
	{
		return (m_id < pair.m_id);
	}

	IC bool operator<(const u16& id) const
	{
		return (m_id < id);
	}
};

void CLevel::Load_GameSpecific_CFORM(CDB::TRI* tris, u32 count)
{
	typedef xr_vector<translation_pair> ID_INDEX_PAIRS;
	ID_INDEX_PAIRS translator;
	translator.reserve(GMLib.CountMaterial());
	u16 default_id = (u16)GMLib.GetMaterialIdx("default");
	translator.push_back(translation_pair(u32(-1), default_id));

	u16 index = 0, static_mtl_count = 1;
	int max_ID = 0;
	int max_static_ID = 0;
	for (GameMtlIt I = GMLib.FirstMaterial(); GMLib.LastMaterial() != I; ++I, ++index)
	{
		if (!(*I)->Flags.test(SGameMtl::flDynamic))
		{
			++static_mtl_count;
			translator.push_back(translation_pair((*I)->GetID(), index));
			if ((*I)->GetID() > max_static_ID) max_static_ID = (*I)->GetID();
		}
		if ((*I)->GetID() > max_ID) max_ID = (*I)->GetID();
	}
	// Msg("* Material remapping ID: [Max:%d, StaticMax:%d]",max_ID,max_static_ID);
	VERIFY(max_static_ID<0xFFFF);

	if (static_mtl_count < 128)
	{
		CDB::TRI* I = tris;
		CDB::TRI* E = tris + count;
		for (; I != E; ++I)
		{
			ID_INDEX_PAIRS::iterator i = std::find(translator.begin(), translator.end(), (u16)(*I).material);
			if (i != translator.end())
			{
				(*I).material = (*i).m_index;
				SGameMtl* mtl = GMLib.GetMaterialByIdx((*i).m_index);
				(*I).suppress_shadows = mtl->Flags.is(SGameMtl::flSuppressShadows);
				(*I).suppress_wm = mtl->Flags.is(SGameMtl::flSuppressWallmarks);
				continue;
			}

			Debug.fatal(DEBUG_INFO, "Game material '%d' not found", (*I).material);
		}
		return;
	}

	std::sort(translator.begin(), translator.end());
	{
		CDB::TRI* I = tris;
		CDB::TRI* E = tris + count;
		for (; I != E; ++I)
		{
			ID_INDEX_PAIRS::iterator i = std::lower_bound(translator.begin(), translator.end(), (u16)(*I).material);
			if ((i != translator.end()) && ((*i).m_id == (*I).material))
			{
				(*I).material = (*i).m_index;
				SGameMtl* mtl = GMLib.GetMaterialByIdx((*i).m_index);
				(*I).suppress_shadows = mtl->Flags.is(SGameMtl::flSuppressShadows);
				(*I).suppress_wm = mtl->Flags.is(SGameMtl::flSuppressWallmarks);
				continue;
			}

			Debug.fatal(DEBUG_INFO, "Game material '%d' not found", (*I).material);
		}
	}
}

void CLevel::BlockCheatLoad()
{
#ifndef	DEBUG
	if (game && (GameID() != eGameIDSingle)) phTimefactor = 1.f;
#endif
}
