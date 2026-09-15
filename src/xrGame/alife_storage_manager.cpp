////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_storage_manager.cpp
//	Created 	: 25.12.2002
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator storage manager
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "alife_storage_manager.h"
#include "alife_simulator_header.h"
#include "alife_time_manager.h"
#include "alife_spawn_registry.h"
#include "alife_object_registry.h"
#include "alife_graph_registry.h"
#include "alife_group_registry.h"
#include "alife_registry_container.h"
#include "xrserver.h"
#include "level.h"
#include "../xrEngine/x_ray.h"
#include "saved_game_wrapper.h"
#include "string_table.h"
#include "../xrEngine/igame_persistent.h"
#include "autosave_manager.h"
// MP fork (§14 step 8 phase 3 Q2 / doc §7.2): coop_task_state_save/load — the offer pool and the
// ownership tags ride the .scop, in the same write as the task list they annotate.
#include "GametaskManager.h"
#include "mp_coop_ff.h"
#include "mp_coop_chunk_reader.h"                   // MP fork (§10.3 S3b): bounded chunk reads
#include "pch_script.h"                             // MP fork (§10.3 S3b): the gamedata state functor
#include "ai_space.h"
#include "script_engine.h"
#include "../xrNetServer/xr_enet_transport.h"       // MP fork (§10.3 S3b): xr_enet::enabled()
// MP fork (§14 step 8 phase 4 R2 / doc §8.1): coop_rep_state_save/load — the FACTION tier of
// reputation, which R1 measured as the one part of this layer that nothing persisted.
#include "relation_registry.h"
//Alundaio
#ifdef ENGINE_LUA_ALIFE_STORAGE_MANAGER_CALLBACKS
#include "pch_script.h"
#include "../../xrServerEntities/script_engine.h"
#endif
//-Alundaio

extern XRCORE_API string_path g_bug_report_file;

using namespace ALife;
#ifdef ENGINE_LUA_ALIFE_STORAGE_MANAGER_CALLBACKS
 //Alundaio
#endif

extern string_path g_last_saved_game;

// MP fork (design doc §10.3 S3b): server GAMEDATA's world state, in the same save as the engine's co-op chunks. Gamedata owns the
// format; the engine only carries it. On save it asks gamedata (_G.mp_coop_state_save, which hands its string to
// level.coop_state_put); on load it keeps the string for gamedata to read back (level.coop_state_loaded). The string is copied
// into engine memory by coop_state_put, so no pointer into the Lua heap outlives the call.
namespace
{
	const u16 coop_gamedata_state_version = 1;
	const u32 coop_gamedata_state_max = 256 * 1024;
	xr_string& coop_state_out() { static xr_string s; return s; }
	xr_string& coop_state_in() { static xr_string s; return s; }
}

static bool s_coop_state_failed = false;

void coop_state_put(LPCSTR blob)
{
	coop_state_out() = blob ? blob : "";
	if (coop_state_out().size() > coop_gamedata_state_max)
	{
		Msg("! COOP(state): gamedata state is %u bytes, over the %u cap — this save carries the last loaded state instead",
			u32(coop_state_out().size()), coop_gamedata_state_max);
		s_coop_state_failed = true;
	}
}

LPCSTR coop_state_loaded() { return coop_state_in().c_str(); }

static void coop_gamedata_state_save(IWriter& stream)
{
	if (!xr_enet::enabled())
		return;
	coop_state_out().clear();
	s_coop_state_failed = false;
	luabind::functor<void> f;
	if (ai().script_engine().functor("_G.mp_coop_state_save", f))
	{
		bool raised = false;
		try
		{
			f();
		}
		catch (...)
		{
			raised = true;
		}
		if (raised)
		{
			Msg("! COOP(state): _G.mp_coop_state_save raised — this save carries the last loaded state instead");
			s_coop_state_failed = true;
		}
	}
	// A failed capture must not ERASE gamedata state in the save that replaces the last one: carry the last loaded blob forward.
	// (Not aborting the save: a world that cannot be saved at all because of a gamedata error loses far more.)
	if (s_coop_state_failed)
		coop_state_out() = coop_state_in();
	stream.open_chunk(COOP_GAMEDATA_CHUNK_DATA);
	stream.w_u16(coop_gamedata_state_version);
	stream.w_u32(u32(coop_state_out().size()));
	if (!coop_state_out().empty())
		stream.w(coop_state_out().data(), u32(coop_state_out().size()));
	stream.close_chunk();
	Msg("- COOP(state): saved %u byte(s) of gamedata state", u32(coop_state_out().size()));
}

static void coop_gamedata_state_load(IReader& stream)
{
	coop_state_in().clear();   // a load that finds nothing must leave nothing of the previous world
	const int caller_pos = stream.tell();
	const u32 chunk_size = stream.find_chunk(COOP_GAMEDATA_CHUNK_DATA);
	if (!chunk_size)
	{
		stream.seek(caller_pos);
		Msg("- COOP(state): no gamedata state chunk in this save");
		return;
	}
	coop_chunk_reader r(stream, stream.tell() + int(chunk_size));
	const u16 ver = r.u16v();
	u32 n = 0;
	if (r.ok && ver == coop_gamedata_state_version && r.room(4))
	{
		n = stream.r_u32();
		if (n > coop_gamedata_state_max || !r.room(int(n)))
			r.ok = false;
		else if (n)
		{
			coop_state_in().resize(n);
			stream.r(&coop_state_in()[0], n);
		}
	}
	if (!r.ok || ver != coop_gamedata_state_version)
	{
		coop_state_in().clear();
		Msg("! COOP(state): gamedata state chunk version %u / truncated — REFUSING all of it", u32(ver));
	}
	else
		Msg("- COOP(state): loaded %u byte(s) of gamedata state", n);
	stream.seek(caller_pos);
}

CALifeStorageManager::~CALifeStorageManager()
{
	*g_last_saved_game = 0;
}

void CALifeStorageManager::save(LPCSTR save_name_no_check, bool update_name)
{
	PROF_EVENT();
	LPCSTR game_saves_path = FS.get_path("$game_saves$")->m_Path;

	string_path save_name;
	strncpy_s(save_name, sizeof(save_name), save_name_no_check,
	          sizeof(save_name) - 5 - xr_strlen(SAVE_EXTENSION) - xr_strlen(game_saves_path));

	xr_strcpy(g_last_saved_game, save_name);

	string_path save;
	xr_strcpy(save, m_save_name);
	if (save_name)
	{
		strconcat(sizeof(m_save_name), m_save_name, save_name, SAVE_EXTENSION);
	}
	else
	{
		if (!xr_strlen(m_save_name))
		{
			Log("There is no file name specified!");
			return;
		}
	}

	//Alundaio: To get the savegame fname to make our own custom save states
#ifdef ENGINE_LUA_ALIFE_STORAGE_MANAGER_CALLBACKS
	::luabind::functor<void> funct1;
	if (ai().script_engine().functor("alife_storage_manager.CALifeStorageManager_before_save", funct1))
		funct1((LPCSTR)m_save_name);
#endif
	//-Alundaio

	u32 source_count;
	u32 dest_count;
	void* dest_data;
	{
		CMemoryWriter stream;
		header().save(stream);
		time_manager().save(stream);
		spawns().save(stream);
		objects().save(stream);
		registry().save(stream);
		// MP fork (§14 step 8 phase 3 Q2): the co-op quest layer's annotations to the task list
		// registry().save just wrote — same stream, same compressed payload, so a claim and the
		// record of who claimed it can never be half-written relative to each other.
		coop_task_state_save(stream);
		// MP fork (§14 step 8 phase 4 R2): and the faction tier of reputation, for the same
		// reason and in the same write — a faction war that moved during play is world state,
		// and until this chunk existed the next restart put every point back.
		coop_rep_state_save(stream);
		// MP fork (design doc §12.2): per-zone friendly-fire overrides are world state too.
		coop_ff_state_save(stream);
		// MP fork (design doc §10.3 S3b): server gamedata's own world state (e.g. the trader restock schedule).
		coop_gamedata_state_save(stream);

		source_count = stream.tell();
		void* source_data = stream.pointer();
		dest_count = rtc_csize(source_count);
		dest_data = xr_malloc(dest_count);
		dest_count = rtc_compress(dest_data, dest_count, source_data, source_count);
	}

	string_path temp;
	FS.update_path(temp, "$game_saves$", m_save_name);
	IWriter* writer = FS.w_open(temp);
	writer->w_u32(u32(-1));
	writer->w_u32(ALIFE_VERSION);

	writer->w_u32(source_count);
	writer->w(dest_data, dest_count);
	xr_free(dest_data);
	FS.w_close(writer);
#ifdef DEBUG
	Msg							("* Game %s is successfully saved to file '%s' (%d bytes compressed to %d)",m_save_name,temp,source_count,dest_count + 4);
#else // DEBUG
	Msg("* Game %s is successfully saved to file '%s'", m_save_name, temp);
#endif // DEBUG

	//Alundaio: To get the savegame fname to make our own custom save states
#ifdef ENGINE_LUA_ALIFE_STORAGE_MANAGER_CALLBACKS
	::luabind::functor<void> funct2;
	if (ai().script_engine().functor("alife_storage_manager.CALifeStorageManager_save", funct2))
		funct2((LPCSTR)m_save_name);
#endif
	//-Alundaio

	if (!update_name)
		xr_strcpy(m_save_name, save);
}

void CALifeStorageManager::load(void* buffer, const u32& buffer_size, LPCSTR file_name)
{
	//Alundaio: So we can get the fname to make our own custom save states
#ifdef ENGINE_LUA_ALIFE_STORAGE_MANAGER_CALLBACKS
	::luabind::functor<void> funct;
	if (ai().script_engine().functor("alife_storage_manager.CALifeStorageManager_load", funct))
		funct(file_name);
#endif
	//-Alundaio

	IReader source(buffer, buffer_size);
	header().load(source);
	time_manager().load(source);
	spawns().load(source, file_name);
	graph().on_load();
	objects().load(source);

	VERIFY(can_register_objects());
	can_register_objects(false);
	CALifeObjectRegistry::OBJECT_REGISTRY::iterator B = objects().objects().begin();
	CALifeObjectRegistry::OBJECT_REGISTRY::iterator E = objects().objects().end();
	CALifeObjectRegistry::OBJECT_REGISTRY::iterator I;
	for (I = B; I != E; ++I)
	{
		ALife::_OBJECT_ID id = (*I).second->ID;
		(*I).second->ID = server().PerformIDgen(id);
		VERIFY(id == (*I).second->ID);
		register_object((*I).second, false);
	}

	registry().load(source);
	// MP fork (§14 step 8 phase 3 Q2): read the co-op quest annotations back. Always called, even
	// for a save that has no such chunk — the loader CLEARS the maps first, so booting an older
	// world cannot leave the previous world's ownership standing in these file-static maps.
	coop_task_state_load(source);
	// MP fork (§14 step 8 phase 4 R2): the faction tier. Always called, even for a save that has
	// no such chunk — the loader RESETS the static relation table to its config baseline before
	// it looks, so booting an older world cannot leave the previous world's faction war standing.
	coop_rep_state_load(source);
	// MP fork (design doc §12.2): always called; clears the previous world's overrides before it looks.
	coop_ff_state_load(source);
	// MP fork (design doc §10.3 S3b): always called; clears the previous world's gamedata blob before it looks.
	coop_gamedata_state_load(source);

	can_register_objects(true);

	for (I = B; I != E; ++I)
		(*I).second->on_register();

	if (!g_pGameLevel)
		return;

	// MP fork: autosave manager is client-only (its save path drives
	// MainMenu()/CurrentGameUI()); boot 22's jump_to_level crash was here
	if (!g_dedicated_server)
		Level().autosave_manager().on_game_loaded();
}

bool CALifeStorageManager::load(LPCSTR save_name_no_check)
{
	LPCSTR game_saves_path = FS.get_path("$game_saves$")->m_Path;

	string_path save_name;
	strncpy_s(save_name, sizeof(save_name), save_name_no_check,
	          sizeof(save_name) - 5 - xr_strlen(SAVE_EXTENSION) - xr_strlen(game_saves_path));

	CTimer timer;
	timer.Start();

	string_path save;
	xr_strcpy(save, m_save_name);
	if (!save_name)
	{
		if (!xr_strlen(m_save_name))
			R_ASSERT2(false, "There is no file name specified!");
	}
	else
	{
		strconcat(sizeof(m_save_name), m_save_name, save_name, SAVE_EXTENSION);
	}
	string_path file_name;
	FS.update_path(file_name, "$game_saves$", m_save_name);

	xr_strcpy(g_last_saved_game, save_name);
	xr_strcpy(g_bug_report_file, file_name);

	IReader* stream;
	stream = FS.r_open(file_name);
	if (!stream)
	{
		Msg("* Cannot find saved game %s", file_name);
		xr_strcpy(m_save_name, save);
		return (false);
	}

	CHECK_OR_EXIT(CSavedGameWrapper::valid_saved_game(*stream),
	              make_string("%s\nSaved game version mismatch or saved game is corrupted",file_name));
	/*
		string512					temp;
		strconcat					(sizeof(temp),temp,CStringTable().translate("st_loading_saved_game").c_str()," \"",save_name,SAVE_EXTENSION,"\"");
		g_pGamePersistent->LoadTitle(temp);
	*/
	g_pGamePersistent->LoadTitle();

	unload();
	reload(m_section);

	u32 source_count = stream->r_u32();
	void* source_data = xr_malloc(source_count);
	rtc_decompress(source_data, source_count, stream->pointer(), stream->length() - 3 * sizeof(u32));
	FS.r_close(stream);
	load(source_data, source_count, file_name);
	xr_free(source_data);

	groups().on_after_game_load();

	VERIFY(graph().actor());

	Msg("* Game %s is successfully loaded from file '%s' (%.3fs)", save_name, file_name, timer.GetElapsed_sec());

	return (true);
}

void CALifeStorageManager::save(NET_Packet& net_packet)
{
	PROF_EVENT();
	prepare_objects_for_save();

	shared_str game_name;
	net_packet.r_stringZ(game_name);
	save(*game_name, !!net_packet.r_u8());
}

void CALifeStorageManager::prepare_objects_for_save()
{
	PROF_EVENT();
	Level().ClientSend();
	Level().ClientSave();
}
