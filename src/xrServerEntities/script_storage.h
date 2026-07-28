////////////////////////////////////////////////////////////////////////////
//	Module 		: script_storage.h
//	Created 	: 01.04.2004
//  Modified 	: [1/14/2015 Andrey]
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Storage
////////////////////////////////////////////////////////////////////////////

#pragma once

#include <intrin.h>   // MP fork (§3c audit): __readgsdword for the hot-path thread check
#include "script_storage_space.h"
#include "script_space_forward.h"

struct lua_State;
class CScriptThread;

#ifndef MASTER_GOLD
#	define USE_DEBUGGER
#	define USE_LUA_STUDIO
#endif //-!MASTER_GOLD

#ifdef XRGAME_EXPORTS
#	ifndef MASTER_GOLD
#		define PRINT_CALL_STACK
#	endif //-!MASTER_GOLD
#else //!XRGAME_EXPORTS
#	ifndef NDEBUG
#		define PRINT_CALL_STACK
#	endif // #ifndef NDEBUG
#endif //-XRGAME_EXPORTS

//AVO: allow LUA debug prints (i.e.: ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CWeapon : cannot access class member Weapon_IsScopeAttached!");)
#include "..\build_config_defines.h"
#ifndef DEBUG
#   ifdef LUA_DEBUG_PRINT
#       define PRINT_CALL_STACK
#   endif
#endif //-!DEBUG
//-AVO

using namespace ScriptStorage;

// MP fork (§3c audit, dev/INSTABILITY_PLAN.md §4.4.ii): WHO touches the Lua VM, and from which
// thread. §3c found ONE pump-thread entry into the VM (the dialogue action) because that is the
// one the harness could see; the audit question is whether it is the only one, and reading
// xrServer::OnMessage's fifty cases by eye answers that with an opinion. This answers it with a
// measurement — every path is instrumented at the one place they all pass through.
//
// Off unless -coop_vm_audit is passed. The fast path is a load, a TEB read and a branch — no
// call — because the first version called out of line on EVERY touch and that cost the run: the
// server took 480 s without finishing its going-online sweep, so the audit measured the boot and
// never reached the dialogue it was taken for. A call in luabind's per-argument inner loops is
// not free, and "an audit run exists to be read, not to be fast" was the wrong trade.
//
// The thread id is read straight out of the TEB (gs:[0x48] = ClientId.UniqueThread on x64),
// which is one instruction instead of a cross-DLL GetCurrentThreadId. That constant is VERIFIED
// against GetCurrentThreadId at arm time and the audit REFUSES TO ARM if they disagree — a
// filter that silently under-triggers would report an empty site list, which reads exactly like
// a clean result.
extern u32  g_coop_vm_audit;                   // 0 = off, 1 = armed (set once at boot)
extern u32  g_coop_game_thread_id;             // stamped every frame in CLevel::OnFrame
void        coop_vm_touch_offthread();         // out of line; confirms the thread and reports

IC u32 coop_thread_id_fast()
{
	return __readgsdword(0x48);
}

IC void coop_vm_touch_check()
{
	if (g_coop_vm_audit && coop_thread_id_fast() != g_coop_game_thread_id)
		coop_vm_touch_offthread();
}

class CScriptStorage
{
private:
	lua_State* m_virtual_machine;
	CScriptThread* m_current_thread;
	BOOL m_jit;

#ifdef DEBUG
public:
    bool						m_stack_is_ready	;
#endif //-DEBUG

#ifdef LUA_DEBUG_PRINT//PRINT_CALL_STACK
protected:
    CMemoryWriter m_output;
#else
#   ifdef DEBUG
protected:
    CMemoryWriter m_output;
#   endif //-DEBUG
#endif //-LUA_DEBUG_PRINT PRINT_CALL_STACK

protected:
	static int vscript_log(ScriptStorage::ELuaMessageType tLuaMessageType, LPCSTR caFormat, va_list marker);
	bool parse_namespace(LPCSTR caNamespaceName, LPSTR b, u32 const b_size, LPSTR c, u32 const c_size);
	bool do_file(LPCSTR caScriptName, LPCSTR caNameSpaceName);
	void reinit();

public:
	//#ifdef PRINT_CALL_STACK
	void print_stack();
	//AVO: added to stop duplicate stack output prints in log
	static int __cdecl script_log_no_stack(ScriptStorage::ELuaMessageType tLuaMessageType, LPCSTR caFormat, ...);
	//-AVO
	//#endif //-PRINT_CALL_STACK

public:
	CScriptStorage();
	virtual ~CScriptStorage();
	IC lua_State* lua();
	IC void current_thread(CScriptThread* thread);
	IC CScriptThread* current_thread() const;
	bool load_buffer(lua_State* L, LPCSTR caBuffer, size_t tSize, LPCSTR caScriptName, LPCSTR caNameSpaceName = 0);
	bool load_file_into_namespace(LPCSTR caScriptName, LPCSTR caNamespaceName);
	bool namespace_loaded(LPCSTR caName, bool remove_from_stack = true);
	bool object(LPCSTR caIdentifier, int type);
	bool object(LPCSTR caNamespaceName, LPCSTR caIdentifier, int type);
	::luabind::object name_space(LPCSTR namespace_name);
	int error_log(LPCSTR caFormat, ...);
	static int __cdecl script_log(ELuaMessageType message, LPCSTR caFormat, ...);
	static bool print_output(lua_State* L, LPCSTR caScriptName, int iErorCode = 0);
	static void print_error(lua_State* L, int iErrorCode);
	virtual void on_error(lua_State* L) = 0;
    void DebuggerAttach();

#ifdef LUA_DEBUG_PRINT //DEBUG
public:
    void flush_log();
#endif //-LUA_DEBUG_PRINT DEBUG
};

#include "script_storage_inline.h"
