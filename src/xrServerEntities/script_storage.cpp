////////////////////////////////////////////////////////////////////////////
//	Module 		: script_storage.cpp
//	Created 	: 01.04.2004
//  Modified 	: [1/14/2015 Andrey]
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Storage
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_storage.h"
#include "script_thread.h"
#include "../xrCore/mezz_stringbuffer.h"
#include "../xrCore/coop_pool.h"	// §6c: the luabind small-object pool and its ownership test
#include <stdarg.h>
#include <unordered_map>
#include <set>
#include <sstream>
#include <regex>

#if !defined(DEBUG) && defined(USE_LUAJIT_ONE)
#	include "opt.lua.h"
#	include "opt_inline.lua.h"
#endif //!DEBUG && USE_LUAJIT_ONE
#ifndef USE_LUAJIT_ONE
#include "lua.hpp"
#endif

extern "C"
{
#include <lua.h>
	int luaopen_marshal(lua_State* L);

}
struct luajit
{
	static void open_lib(lua_State* L, pcstr module_name, lua_CFunction function)
	{
		lua_pushcfunction(L, function);
		lua_pushstring(L, module_name);
		lua_call(L, 1, 0);
	}
};

LPCSTR file_header_old =
	"\
                          local function script_name() \
                          return \"%s\" \
                          end \
                          local this = {} \
                          %s this %s \
                          setmetatable(this, {__index = _G}) \
                          setfenv(1, this) \
                                ";

LPCSTR file_header_new =
	"\
                          local function script_name() \
                          return \"%s\" \
                          end \
                          local this = {} \
                          this._G = _G \
                          %s this %s \
                          setfenv(1, this) \
                                ";

LPCSTR file_header = 0;

#ifndef ENGINE_BUILD
#	include "script_engine.h"
#	include "ai_space.h"
#else //ENGINE_BUILD
#	define NO_XRGAME_SCRIPT_ENGINE
#endif //!ENGINE_BUILD

#ifndef XRGAME_EXPORTS
#	define NO_XRGAME_SCRIPT_ENGINE
#endif //!XRGAME_EXPORTS

#ifndef NO_XRGAME_SCRIPT_ENGINE
#	include "ai_debug.h"
#endif //!NO_XRGAME_SCRIPT_ENGINE

#ifdef USE_DEBUGGER
#	include "script_debugger.h"
#endif

#ifndef PURE_ALLOC
//#	ifndef USE_MEMORY_MONITOR
//#		define USE_DL_ALLOCATOR
//#	endif //!USE_MEMORY_MONITOR
#endif //!PURE_ALLOC

#ifndef USE_DL_ALLOCATOR
static void* lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
	(void)ud;
	(void)osize;
	if (nsize == 0)
	{
		xr_free(ptr);
		return NULL;
	}
	else
#ifdef DEBUG_MEMORY_NAME
        return Memory.mem_realloc		(ptr, nsize, "LUA");
#else // DEBUG_MEMORY_MANAGER
		return Memory.mem_realloc(ptr, nsize);
#endif // DEBUG_MEMORY_MANAGER
}

u32 game_lua_memory_usage()
{
	return (0);
}
#else //USE_DL_ALLOCATOR

#   ifdef USE_ARENA_ALLOCATOR
        static const u32			s_arena_size = 96*1024*1024;
        static char					s_fake_array[s_arena_size];
//        static doug_lea_allocator	s_allocator( s_fake_array, s_arena_size, "lua" );
#   else //-USE_ARENA_ALLOCATOR
//        static doug_lea_allocator	s_allocator(0, 0, "lua");
#   endif //-USE_ARENA_ALLOCATOR

static void *lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
#ifndef USE_MEMORY_MONITOR
    (void)ud;
    (void) osize;
    if (!nsize)
    {
        s_allocator.free_impl(ptr);
        return 0;
    }
    if (!ptr)
        return s_allocator.malloc_impl((u32) nsize);

    return s_allocator.realloc_impl(ptr, (u32) nsize);
#else //USE_MEMORY_MONITOR
    if ( !nsize )	{
        memory_monitor::monitor_free(ptr);
        s_allocator.free_impl		(ptr);
        return						NULL;
    }

    if ( !ptr ) {
        void* const result			= s_allocator.malloc_impl((u32)nsize);
        memory_monitor::monitor_alloc (result,nsize,"LUA");
        return						result;
    }

    memory_monitor::monitor_free	(ptr);
    void* const result				= s_allocator.realloc_impl(ptr, (u32)nsize);
    memory_monitor::monitor_alloc	(result,nsize,"LUA");
    return							result;
#endif //!USE_MEMORY_MONITOR
}

u32 game_lua_memory_usage()
{
    return (s_allocator.get_allocated_size());
}
#endif //!USE_DL_ALLOCATOR

static void* __cdecl luabind_allocator(
	const void* pointer,
	size_t const size
)
{
	// §6c — THE POOL SITS HERE, on the one function every luabind allocation and free passes
	// through. §6a measured 87% of all 8-15 byte allocations arriving from two `value_converter`
	// instantiations at ~8,200/second, each one a `luabind_new<T>` that lands on the `!pointer`
	// branch below.
	//
	// The fork on the free path is the dangerous one and it is decided by an EXACT test, not a
	// guess: `coop_pool_owns` is a range check against a region the OS reserved for us, so a
	// foreign pointer cannot be inside it. When the pool is disarmed -- which is every run that
	// does not pass `-coop_pool` -- `owns()` is false for every pointer in the address space and
	// all three branches behave exactly as they did before this change.
	if (!size)
	{
		void* non_const_pointer = const_cast<void*>(pointer);
		if (coop_pool_owns(non_const_pointer))
		{
			// Deliberately NOT counted as a reclaim: this is the expected route. The reclaim
			// counter exists to reveal pooled blocks arriving at `xrMemory::mem_free` instead,
			// and it can only mean that if this path does not inflate it.
			coop_pool_free(non_const_pointer);
			return nullptr;
		}
		xr_free(non_const_pointer);
		return nullptr;
	}

	if (!pointer)
	{
		// A decline here is not a failure: the pool declines sizes outside its classes and
		// declines when the region is exhausted, and the block xrMemory then returns is correctly
		// identified as foreign for the rest of its life.
		if (void* const pooled = coop_pool_alloc(size))
			return pooled;

#ifdef DEBUG
		return	(Memory.mem_alloc(size, "luabind"));
#else //!DEBUG
		return (Memory.mem_alloc(size));
#endif //-DEBUG
	}

	void* non_const_pointer = const_cast<void*>(pointer);

	// Realloc of a pooled block. `old` is the CLASS size, never the requested size -- the request
	// is not recorded, and copying `size` bytes out of a 16-byte block would read past it.
	if (coop_pool_owns(non_const_pointer))
	{
		size_t const old = coop_pool_block_size(non_const_pointer);
		void* result = coop_pool_alloc(size);
		if (!result)
		{
#ifdef DEBUG
			result = Memory.mem_alloc(size, "luabind");
#else //!DEBUG
			result = Memory.mem_alloc(size);
#endif //-DEBUG
		}
		if (result && old)
			memcpy(result, non_const_pointer, (size < old) ? size : old);
		coop_pool_free(non_const_pointer);
		return result;
	}
#ifdef DEBUG
	return		(Memory.mem_realloc(non_const_pointer, size, "luabind"));
#else //!DEBUG
	return (Memory.mem_realloc(non_const_pointer, size));
#endif //-DEBUG
}

void setup_luabind_allocator()
{
	::luabind::set_allocator(&luabind_allocator);
}

#ifdef USE_LUAJIT_ONE //  [1/14/2015 Andrey]

/* ---- start of LuaJIT extensions */
static void l_message(lua_State* state, const char *msg)
{
    Msg("! [LUA_JIT] %s", msg);
}


static int report(lua_State *L, int status)
{
    if (status && !lua_isnil(L, -1))
    {
        const char *msg = lua_tostring(L, -1);
        if (msg == NULL) msg = "(error object is not a string)";
        l_message(L, msg);
        lua_pop(L, 1);
    }
    return status;
}

static int loadjitmodule(lua_State *L, const char *notfound)
{
    lua_getglobal(L, "require");
    lua_pushliteral(L, "jit.");
    lua_pushvalue(L, -3);
    lua_concat(L, 2);
    if (lua_pcall(L, 1, 1, 0))
    {
        const char *msg = lua_tostring(L, -1);
        if (msg && !strncmp(msg, "module ", 7))
        {
            l_message(L, notfound);
            return 1;
        }
        else
            return report(L, 1);
    }
    lua_getfield(L, -1, "start");
    lua_remove(L, -2);  /* drop module table */
    return 0;
}

/* JIT engine control command: try jit library first or load add-on module */
static int dojitcmd(lua_State *L, const char *cmd)
{
    const char *val = strchr(cmd, '=');
    lua_pushlstring(L, cmd, val ? val - cmd : xr_strlen(cmd));
    lua_getglobal(L, "jit");  /* get jit.* table */
    lua_pushvalue(L, -2);
    lua_gettable(L, -2);  /* lookup library function */
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 2);  /* drop non-function and jit.* table, keep module name */
        if (loadjitmodule(L, "unknown luaJIT command"))
            return 1;
    }
    else
    {
        lua_remove(L, -2);  /* drop jit.* table */
    }
    lua_remove(L, -2);  /* drop module name */
    if (val) lua_pushstring(L, val + 1);
    return report(L, lua_pcall(L, val ? 1 : 0, 0, 0));
}

void jit_command(lua_State* state, LPCSTR command)
{
    dojitcmd(state, command);
}

#ifndef DEBUG
/* start optimizer */
static int dojitopt(lua_State *L, const char *opt)
{
    lua_pushliteral(L, "opt");
    if (loadjitmodule(L, "LuaJIT optimizer module not installed"))
        return 1;
    lua_remove(L, -2);  /* drop module name */
    if (*opt) lua_pushstring(L, opt);
    return report(L, lua_pcall(L, *opt ? 1 : 0, 0, 0));
}

static void put_function(lua_State* state, u8 const* buffer, u32 const buffer_size, LPCSTR package_id)
{
    lua_getglobal(state, "package");
    lua_pushstring(state, "preload");
    lua_gettable(state, -2);

    lua_pushstring(state, package_id);
    luaL_loadbuffer(state, (char*) buffer, buffer_size, package_id);
    lua_settable(state, -3);
}

/* ---- end of LuaJIT extensions */
#endif //!DEBUG
#endif //-USE_LUAJIT_ONE


CScriptStorage::CScriptStorage()
{
	m_current_thread = 0;

#ifdef DEBUG
    m_stack_is_ready		= false;
#endif //-DEBUG

	m_virtual_machine = 0;

#ifdef USE_LUA_STUDIO
#	ifndef USE_DEBUGGER
    STATIC_CHECK( false, Do_Not_Define_USE_LUA_STUDIO_macro_without_USE_DEBUGGER_macro );
#	endif //!USE_DEBUGGER
#endif //-USE_LUA_STUDIO
}

CScriptStorage::~CScriptStorage()
{
	if (m_virtual_machine)
		lua_close(m_virtual_machine);
}

extern int luaopen_lua_extensions(lua_State* L, bool IsDebug = false);
extern lua_CFunction luaopen_socket_core_init();
extern void pdebug_init_init(lua_State* L);

// ---------------------------------------------------------------------------------------------
// MP fork (§3c audit, dev/INSTABILITY_PLAN.md §4.4.ii) — every entry into the Lua VM that is NOT
// on the game thread, named at the site that made it.
//
// §3d proved the mechanism on one path (M_XRNET_DIALOG_ACTION) and §3f fixed that path. The open
// half of the item is the general one: xrServer::OnMessage has ~50 cases and the connection and
// disconnection paths run on the same pump thread, so "the dialogue action was the only one" is a
// claim about code nobody has executed with the question in hand. Reading them is an opinion;
// this is the measurement.
//
// What it prints, and why each part is there:
//   * the thread, and the game thread beside it — the whole point, and useless alone
//   * the pump MESSAGE being processed when the touch happened, which is the answer to
//     "which OnMessage case?" in the form the audit actually wants. 'none' means the touch was
//     off the pump's message dispatch entirely (a connect/disconnect path, or another thread)
//   * a call stack, as absolute addresses with the module base printed once, because the whole
//     project already symbolicates addresses this way (R1-Symbolicate) and a stack answers
//     "which path" where a single return address only answers "which call"
// Each distinct site is announced ONCE — a per-touch line would bury the second site under the
// first — with a running total so a site that fires constantly is distinguishable from one that
// fired once. Sites are capped; the cap being hit is itself printed, because a silently truncated
// audit reads exactly like a complete one.
u32 g_coop_vm_audit_touches = 0;
u32 g_coop_vm_audit_outside = 0;

// Provided by xrEngine's device.cpp: 1 while the secondary thread is permitted to run. See §4d —
// a non-game-thread touch with this DOWN is outside the frame interlock, which is the actual
// §3c defect class; with it UP the touch is what this engine has always done (A-Life, bullets,
// the parallel GC) and is serialised against the game thread.
extern u32 g_coop_mt_window;

// Provided by xrServer.cpp. Returns the M_* type currently being dispatched on THIS thread, or
// coop_pump_msg_none when this thread is not inside xrServer::OnMessage.
extern u32 coop_current_pump_message();
extern LPCSTR coop_pump_message_name(u32 type);

void coop_vm_touch_offthread()
{
	// Before the first frame there is no game thread to compare against, and everything that
	// happens then (script engine init, the first loads) is by definition not a race with a loop
	// that has not started. Refuse to guess rather than report every boot-time touch as a defect.
	if (!g_coop_game_thread_id)
		return;

	const u32 tid = GetCurrentThreadId();
	if (tid == g_coop_game_thread_id)
		return;

	// Sampled BEFORE anything else: the reporting below is not instantaneous, and a window that
	// closed while we were formatting a string would be recorded as the state the touch happened
	// in. Reading it first is the difference between measuring the touch and measuring ourselves.
	const bool interlocked = (g_coop_mt_window != 0);

	// MAX_MSGS: a site's pump message used to be stamped at FIRST SIGHTING and never revisited, so a
	// site first seen outside a message kept `none` for the whole run — and one first seen during a
	// connect handshake kept THAT for the whole run. Measured (INSTABILITY_PLAN §4j, build
	// 30578875845): the moment the column started working, **19 of 20 sites reported the same
	// message, M_SECURE_KEY_SYNC**, across 85 s and 28,960 touches. Nineteen distinct call sites did
	// not all run exclusively under a key-sync handshake; they were all first SEEN during one.
	//
	// That is false-confidence class 0's THIRD STATE — a working instrument, answering honestly,
	// with a key that cannot tell the cases apart. It is not a broken instrument (wants a repair)
	// and not a real negative (wants acting on); it wants A BETTER KEY. So a site now carries the
	// SET of messages it has been seen under, and a new one is announced when it first appears.
	//
	// Bounded rather than per-touch: recording every touch would be 28,960 lines for the run above.
	//
	// MAX_MSGS WAS 8, AND THE FIRST RUN WITH THE FIX MEASURED THAT AS TOO SMALL: 19 of 21 sites hit
	// the cap and said so, making every one of those lists a FLOOR rather than a total (§4k result).
	// "Eight is well above the number any one site is expected to serve" was a guess, and it was
	// wrong by a lot.
	//
	// 64 is not a bigger guess, it is THE SIZE OF THE SPACE. xrMessages.h defines 58 distinct M_*
	// enumerators with a highest value of 0x39 (57), and a site's set may additionally hold the
	// `none` sentinel — 59 worst case. A real site therefore CANNOT overflow 64, so the cap's
	// silence becomes a meaningful statement (this list is complete) rather than an unknown.
	//
	// That silence is only readable because the cap has been SHOWN TO FIRE: the MAX_MSGS=8 build
	// announced it 19 times on this exact code path. Without that positive, "0 cap announcements"
	// would be indistinguishable from a cap that never worked — the class-1 shape where the
	// expected value is also what you get when nothing happened.
	//
	// Cost: 48 sites x 64 x 4 B = 12 KB of static table, against a server capped at 12 GB.
	enum { MAX_SITES = 48, MAX_FRAMES = 12, SKIP_FRAMES = 1, MAX_MSGS = 64 };
	struct site
	{
		void* key;
		u32   count;
		u32   outside;
		u32   msgs[MAX_MSGS];   // the distinct pump messages this site has been seen under
		u32   msg_count;
		bool  msg_capped;
	};
	static site     s_sites[MAX_SITES];
	static u32      s_site_count = 0;
	static bool     s_capped = false;
	static void*    s_module_base = NULL;
	static xrCriticalSection s_lock
#ifdef PROFILE_CRITICAL_SECTIONS
		(MUTEX_PROFILE_ID(coop_vm_audit))
#endif
		;

	void* frames[MAX_FRAMES] = {0};
	const u16 got = RtlCaptureStackBackTrace(SKIP_FRAMES, MAX_FRAMES, frames, NULL);
	void* const key = got ? frames[0] : NULL;

	s_lock.Enter();
	++g_coop_vm_audit_touches;
	if (!interlocked)
		++g_coop_vm_audit_outside;

	u32 i = 0;
	for (; i < s_site_count; ++i)
		if (s_sites[i].key == key)
			break;

	if (i < s_site_count)
	{
		++s_sites[i].count;
		if (!interlocked)
			++s_sites[i].outside;
		const u32 c = s_sites[i].count;
		const u32 out = s_sites[i].outside;
		// The FIRST un-interlocked touch of a site that had only interlocked ones is its own
		// event, and a loud one: it is the §3c defect appearing on a path that looked ordinary.
		// Announcing it only in the periodic summary would bury it under a million safe touches.
		const bool first_outside = (!interlocked && out == 1);

		// THE KEY FIX (§4j): a known site seen under a message it has not been seen under before is
		// new information, and it was previously discarded. Decide inside the lock, print outside it
		// — every Msg() in this function is deliberately outside, and a string format under a lock
		// held by a path this hot is how an instrument becomes the thing it is measuring.
		const u32 now_msg = coop_current_pump_message();
		bool  msg_is_new = false, msg_now_capped = false;
		u32   msg_seen_n = 0;
		{
			site& S = s_sites[i];
			u32 m = 0;
			for (; m < S.msg_count; ++m)
				if (S.msgs[m] == now_msg)
					break;
			if (m == S.msg_count)
			{
				if (S.msg_count < MAX_MSGS)
				{
					S.msgs[S.msg_count++] = now_msg;
					msg_is_new = true;
					msg_seen_n = S.msg_count;
				}
				else if (!S.msg_capped)
				{
					S.msg_capped = true;
					msg_now_capped = true;
				}
			}
		}
		// §4o: the module base, read under the lock so the first-OUTSIDE stack below can be
		// symbolicated. It is set by the first new-site registration, which always precedes any
		// known-site hit — but it is READ rather than assumed, because a NULL printed next to a
		// stack silently makes every RVA in it wrong.
		void* const base_known = s_module_base;
		s_lock.Leave();
		if (msg_is_new)
			Msg("! COOP(vm-audit): site #%u ALSO seen under pump message: %s (0x%X) — %u distinct "
			    "messages for this site now. thread %u, frame interlock: %s. (The first-sighting "
			    "label is NOT the whole story for this site; see INSTABILITY_PLAN §4j.)",
			    i, coop_pump_message_name(now_msg), now_msg, msg_seen_n, tid,
			    interlocked ? "INSIDE" : "OUTSIDE — THIS IS THE §3c DEFECT CLASS");
		if (msg_now_capped)
			Msg("! COOP(vm-audit): site #%u has been seen under more than %u distinct pump messages "
			    "— the message list for this site is INCOMPLETE from here on, do not read it as the "
			    "full set", i, u32(MAX_MSGS));
		if (first_outside)
		{
			Msg("! COOP(vm-audit): site #%u touched the VM OUTSIDE THE FRAME INTERLOCK — this is "
				"the §3c defect class, not the engine's ordinary MT work (see INSTABILITY_PLAN §4d)", i);
			// §4o: AND ITS STACK, which until now was never printed for a KNOWN site.
			//
			// This cost a result. §4o caught the first stock path doing this — a client's own
			// death — and could not name the caller, because a site's stack was printed at FIRST
			// SIGHTING only and site #1's first sighting is its ordinary, harmless one
			// (CALifeSwitchManager::switch_object on mt_Thread, inside the interlock). The
			// identity of the Lua entry point was therefore known and the path that reached it
			// un-interlocked was not — which is the half that decides where the fix goes.
			//
			// The frames are already captured unconditionally at the top of this function for
			// every touch, so this adds no cost to the hot path: it only formats them, on an
			// event that by definition happens at most once per site per process.
			string4096 otrace;
			otrace[0] = 0;
			for (u16 f = 0; f < got; ++f)
			{
				string64 one;
				xr_sprintf(one, " 0x%p", frames[f]);
				xr_strcat(otrace, one);
			}
			Msg("! COOP(vm-audit): site #%u OUTSIDE-stack (thread %u, pump message %s (0x%X)), "
			    "module base 0x%p, absolute — subtract the base for an RVA to symbolicate:%s",
			    i, tid, coop_pump_message_name(now_msg), now_msg, base_known, otrace);
			FlushLog();
		}
		if ((c % 1000) == 0)
			Msg("! COOP(vm-audit): site #%u has now touched the VM off the game thread %u times, "
				"%u of them OUTSIDE the interlock (%u / %u outside, in total)",
				i, c, out, g_coop_vm_audit_touches, g_coop_vm_audit_outside);
		return;
	}

	if (s_site_count >= MAX_SITES)
	{
		const bool say = !s_capped;
		s_capped = true;
		s_lock.Leave();
		if (say)
			Msg("! COOP(vm-audit): more than %u distinct off-thread sites — the rest are NOT being "
				"reported, so read this run as incomplete rather than as a full list", u32(MAX_SITES));
		return;
	}

	const u32 idx = s_site_count++;
	s_sites[idx].key = key;
	s_sites[idx].count = 1;
	s_sites[idx].outside = interlocked ? 0 : 1;
	// Seed the message SET with the first sighting, so the known-site path below has something to
	// compare against and the first message is not silently absent from the site's own list.
	// (s_sites is static and therefore zero-initialised; these are set explicitly anyway, because
	// relying on that would break the day someone makes the table reusable.)
	s_sites[idx].msg_count = 0;
	s_sites[idx].msg_capped = false;

	if (!s_module_base)
	{
		HMODULE h = NULL;
		// ...ExA explicitly: with UNICODE defined the unsuffixed name is the W form, and the cast
		// below would then be handing a char* to a function expecting wchar_t*.
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		                       (LPCSTR)&coop_vm_touch_offthread, &h))
			s_module_base = (void*)h;
	}
	void* const base = s_module_base;
	const u32 msg = coop_current_pump_message();
	s_sites[idx].msgs[0] = msg;
	s_sites[idx].msg_count = 1;
	s_lock.Leave();

	string4096 trace;
	trace[0] = 0;
	for (u16 f = 0; f < got; ++f)
	{
		string64 one;
		xr_sprintf(one, " 0x%p", frames[f]);
		xr_strcat(trace, one);
	}

	Msg("! COOP(vm-audit): site #%u — the Lua VM was touched from thread %u, and the game thread "
	    "is %u. pump message: %s (0x%X). frame interlock: %s",
	    idx, tid, g_coop_game_thread_id, coop_pump_message_name(msg), msg,
	    interlocked
	        ? "INSIDE (serialised against the game thread — the engine's ordinary MT work)"
	        : "OUTSIDE — THIS IS THE §3c DEFECT CLASS");
	Msg("! COOP(vm-audit): site #%u module base 0x%p, stack (absolute, subtract the base for an "
	    "RVA to symbolicate):%s", idx, base, trace);
	FlushLog();
}
// ---------------------------------------------------------------------------------------------

void disable_os_funcs(lua_State* L)
{
	lua_getglobal(L, "os");
	lua_pushnil(L);
	lua_setfield(L, -2, "execute");
	lua_pushnil(L);
	lua_setfield(L, -2, "rename");
	lua_pushnil(L);
	lua_setfield(L, -2, "remove");
	lua_pushnil(L);
	lua_setfield(L, -2, "exit");
	lua_pop(L, 1);

	lua_getglobal(L, "io");
	lua_pushnil(L);
	lua_setfield(L, -2, "popen");
	lua_pop(L, 1);
}

bool LoadKernelScriptToGlobal(lua_State* L, const char* name)
{
	string_path FileName;
	xr_string FixedFileName = name;
	//FixedFileName = "kernel\\" + FixedFileName; //When this line is enabled all lua files are not being loaded after loading a game

	if (FS.exist(FileName, "$game_scripts$", FixedFileName.data()))
	{
		int	start = lua_gettop(L);
		IReader* l_tpFileReader = FS.r_open(FileName);

		string_path NameSpace;
		xr_strcpy(NameSpace, name);

		if (strext(NameSpace))
			*strext(NameSpace) = 0;

		if (luaL_loadbuffer(L, (const char*)l_tpFileReader->pointer(), l_tpFileReader->length(), NameSpace))
		{
			lua_settop(L, start);
			return false;
		}
		else
		{
			int errFuncId = -1;
			int	l_iErrorCode = lua_pcall(L, 0, 0, (-1 == errFuncId) ? 0 : errFuncId);
			if (l_iErrorCode)
			{
#ifdef DEBUG
				g_pScriptEngine->print_output(L, name, l_iErrorCode);
#endif
				lua_settop(L, start);
				return false;
			}
		}


		FS.r_close(l_tpFileReader);
	}
	else
	{
		return false;
	}

	return true;
};

BOOL lua_debug = FALSE;
void CScriptStorage::reinit()
{
	if (m_virtual_machine)
		lua_close(m_virtual_machine);

#ifdef USE_GSC_MEM_ALLOC
    m_virtual_machine = lua_newstate(lua_alloc, NULL);
#else
	m_virtual_machine = luaL_newstate();
#endif //-USE_GSC_MEM_ALLOC

	if (!m_virtual_machine)
	{
		Msg("! ERROR : Cannot initialize script virtual machine!");
		return;
	}


#ifndef USE_LUAJIT_ONE
	luaL_openlibs(lua());
	if (strstr(Core.Params, "-nojit"))
		luaJIT_setmode(lua(), 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
#else // USE_LUAJIT_ONE
    // initialize lua standard library functions    

    luajit::open_lib(lua(), "", luaopen_base);
    luajit::open_lib(lua(), LUA_LOADLIBNAME, luaopen_package);
    luajit::open_lib(lua(), LUA_TABLIBNAME, luaopen_table);
    luajit::open_lib(lua(), LUA_IOLIBNAME, luaopen_io);
    luajit::open_lib(lua(), LUA_OSLIBNAME, luaopen_os);
    luajit::open_lib(lua(), LUA_MATHLIBNAME, luaopen_math);
    luajit::open_lib(lua(), LUA_STRLIBNAME, luaopen_string);
    luajit::open_lib(lua(), LUA_JITLIBNAME, luaopen_jit);
	luajit::open_lib(lua(), LUA_BITLIBNAME, luaopen_bit);
	luajit::open_lib(lua(), LUA_FFILIBNAME, luaopen_ffi);

#ifdef DEBUG
    luajit::open_lib(lua(), LUA_DBLIBNAME, luaopen_debug);
#else //!DEBUG

    if (strstr(Core.Params, "-dbg"))
        luajit::open_lib(lua(), LUA_DBLIBNAME, luaopen_debug);
#endif //-DEBUG

	if (!strstr(Core.Params, "-nojit"))
	{
		luajit::open_lib(lua(), LUA_JITLIBNAME, luaopen_jit);
#ifndef DEBUG
		put_function(lua(), opt_lua_binary, sizeof(opt_lua_binary), "jit.opt");
		put_function(lua(), opt_inline_lua_binary, sizeof(opt_lua_binary), "jit.opt_inline");
		dojitopt(lua(), "2");
#endif //!DEBUG
	}

#endif //!USE_LUAJIT_ONE
	bool isDebugEnabled = lua_debug;
	luaopen_lua_extensions(lua(), isDebugEnabled);
	disable_os_funcs(lua());

	if (isDebugEnabled)
	{
		Msg("!lua_debug 1, opening socket and initializing LuaPanda");
		LoadKernelScriptToGlobal(lua(), "global.lua");
		LoadKernelScriptToGlobal(lua(), "dynamic_callbacks.lua");

		// Sockets
		luajit::open_lib(lua(), "socket.core", luaopen_socket_core_init());
		bool SocketTest = LoadKernelScriptToGlobal(lua(), "socket.lua");

		// Panda
		if (SocketTest)
		{
			pdebug_init_init(lua());
			LoadKernelScriptToGlobal(lua(), "LuaPanda.lua");
		}
	}

	if (strstr(Core.Params, "-_g"))
		file_header = file_header_new; //AVO: I get fatal crash at the start if this is used
	else
		file_header = file_header_old;
}

void CScriptStorage::DebuggerAttach()
{
    const char* S = "debugger_attach()";
    shared_str m_script_name = "console command";
    int l_iErrorCode = luaL_loadbuffer(lua(), S, xr_strlen(S), "@console_command");

    if (!l_iErrorCode) {
        l_iErrorCode = lua_pcall(lua(), 0, 0, 0);
        if (l_iErrorCode) {
            print_output(lua(), *m_script_name, l_iErrorCode);
            return;
        }
    }

    print_output(lua(), *m_script_name, l_iErrorCode);
}

int CScriptStorage::vscript_log(ScriptStorage::ELuaMessageType tLuaMessageType, LPCSTR caFormat, va_list marker)
{
#ifndef NO_XRGAME_SCRIPT_ENGINE
#   ifdef DEBUG
        if (!psAI_Flags.test(aiLua) && (tLuaMessageType != ScriptStorage::eLuaMessageTypeError))
            return(0);
#   endif //-DEBUG
#endif //!NO_XRGAME_SCRIPT_ENGINE

	//#ifndef PRINT_CALL_STACK
	//return		(0);
	//#else //PRINT_CALL_STACK
#   ifndef NO_XRGAME_SCRIPT_ENGINE
	//AVO: allow LUA debug prints (i.e.: ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "CWeapon : cannot access class member Weapon_IsScopeAttached!");)
#       ifndef DEBUG

	if (!strstr(Core.Params, "-dbg"))
		return (0);
#       endif //!DEBUG
#       ifndef LUA_DEBUG_PRINT
#           ifdef DEBUG
                if (!psAI_Flags.test(aiLua) && (tLuaMessageType != ScriptStorage::eLuaMessageTypeError))
                    return(0);
#           endif //-DEBUG
#       else //!LUA_DEBUG_PRINT
            if (!psAI_Flags.test(aiLua) && (tLuaMessageType != ScriptStorage::eLuaMessageTypeError))
                return(0);
#       endif //-LUA_DEBUG_PRINT
#endif //-NO_XRGAME_SCRIPT_ENGINE

	LPCSTR S = "", SS = "";
	LPSTR S1;
	string4096 S2;
	switch (tLuaMessageType)
	{
	case ScriptStorage::eLuaMessageTypeInfo:
		{
			S = "* [LUA] ";
			SS = "[INFO]        ";
			break;
		}
	case ScriptStorage::eLuaMessageTypeError:
		{
			S = "! [LUA] ";
			SS = "[ERROR]       ";
			break;
		}
	case ScriptStorage::eLuaMessageTypeMessage:
		{
			S = "~ [LUA] ";
			SS = "[MESSAGE]     ";
			break;
		}
	case ScriptStorage::eLuaMessageTypeHookCall:
		{
			S = "[LUA][HOOK_CALL] ";
			SS = "[CALL]        ";
			break;
		}
	case ScriptStorage::eLuaMessageTypeHookReturn:
		{
			S = "[LUA][HOOK_RETURN] ";
			SS = "[RETURN]      ";
			break;
		}
	case ScriptStorage::eLuaMessageTypeHookLine:
		{
			S = "[LUA][HOOK_LINE] ";
			SS = "[LINE]        ";
			break;
		}
	case ScriptStorage::eLuaMessageTypeHookCount:
		{
			S = "[LUA][HOOK_COUNT] ";
			SS = "[COUNT]       ";
			break;
		}
	case ScriptStorage::eLuaMessageTypeHookTailReturn:
		{
			S = "[LUA][HOOK_TAIL_RETURN] ";
			SS = "[TAIL_RETURN] ";
			break;
		}
	default: NODEFAULT;
	}

	xr_strcpy(S2, S);
	S1 = S2 + xr_strlen(S);
	int l_iResult = vsprintf(S1, caFormat, marker);
	Msg("%s", S2);

	xr_strcpy(S2, SS);
	S1 = S2 + xr_strlen(SS);
	vsprintf(S1, caFormat, marker);
	xr_strcat(S2, "\r\n");

#ifdef LUA_DEBUG_PRINT //DEBUG
#	ifndef ENGINE_BUILD
        ai().script_engine().m_output.w(S2,xr_strlen(S2)*sizeof(char));
#	endif //!ENGINE_BUILD
#endif //-LUA_DEBUG_PRINT DEBUG

	return (l_iResult);
	//#endif //-PRINT_CALL_STACK
}

//#ifdef PRINT_CALL_STACK
void CScriptStorage::print_stack()
{
#ifdef DEBUG
    if (!m_stack_is_ready)
        return;

    m_stack_is_ready = false;
#endif //-DEBUG

	lua_State* L = lua();
	// MP fork (R1): asserts that fire before the Lua VM exists route
	// through here; lua_getstack(NULL) AVs, which re-enters the error
	// handler and recurses to a stack overflow that eats the real error
	if (!L)
	{
		script_log_no_stack(ScriptStorage::eLuaMessageTypeError,
		                    "print_stack: lua VM not initialized yet");
		return;
	}
	lua_Debug l_tDebugInfo;
	for (int i = 0; lua_getstack(L, i, &l_tDebugInfo); ++i)
	{
		lua_getinfo(L, "nSlu", &l_tDebugInfo);
		if (!l_tDebugInfo.name)
		{
			script_log_no_stack(ScriptStorage::eLuaMessageTypeError, "%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what,
			                    l_tDebugInfo.short_src, l_tDebugInfo.currentline, "");
			//script_log(ScriptStorage::eLuaMessageTypeError, "%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what, l_tDebugInfo.short_src, l_tDebugInfo.currentline, "");
		}
		else
		{
			if (!xr_strcmp(l_tDebugInfo.what, "C"))
			{
				script_log_no_stack(ScriptStorage::eLuaMessageTypeError, "%2d : [C  ] %s", i, l_tDebugInfo.name);
				//script_log(ScriptStorage::eLuaMessageTypeError, "%2d : [C  ] %s", i, l_tDebugInfo.name);
			}
			else
			{
				script_log_no_stack(ScriptStorage::eLuaMessageTypeError, "%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what,
				                    l_tDebugInfo.short_src, l_tDebugInfo.currentline, l_tDebugInfo.name);
				//script_log(ScriptStorage::eLuaMessageTypeError, "%2d : [%s] %s(%d) : %s", i, l_tDebugInfo.what, l_tDebugInfo.short_src, l_tDebugInfo.currentline, l_tDebugInfo.name);
			}
		}
	}
}

//#endif //-PRINT_CALL_STACK

//AVO: added to stop duplicate stack output prints in log
int __cdecl CScriptStorage::script_log_no_stack(ScriptStorage::ELuaMessageType tLuaMessageType, LPCSTR caFormat, ...)
{
	va_list marker;
	va_start(marker, caFormat);
	int result = vscript_log(tLuaMessageType, caFormat, marker);
	va_end(marker);
	return result;
}

//-AVO

int __cdecl CScriptStorage::script_log(ScriptStorage::ELuaMessageType tLuaMessageType, LPCSTR caFormat, ...)
{
	va_list marker;
	va_start(marker, caFormat);
	int result = vscript_log(tLuaMessageType, caFormat, marker);
	va_end(marker);

	static bool reenterability = false;
	if (!reenterability)
	{
		reenterability = true;
		if (tLuaMessageType == ScriptStorage::eLuaMessageTypeError)
			ai().script_engine().print_stack();
		reenterability = false;
	}

	// #ifdef PRINT_CALL_STACK
	// #	ifndef ENGINE_BUILD
	//     static bool	reenterability = false;
	//     if (!reenterability)
	//     {
	//         reenterability = true;
	//         if (eLuaMessageTypeError == tLuaMessageType)
	//             ai().script_engine().print_stack();
	//         reenterability = false;
	//     }
	// #	endif //!ENGINE_BUILD
	// #endif //-PRINT_CALL_STACK

	return (result);
}

bool CScriptStorage::parse_namespace(LPCSTR caNamespaceName, LPSTR b, u32 const b_size, LPSTR c, u32 const c_size)
{
	*b = 0;
	*c = 0;
	LPSTR S2;
	STRCONCAT(S2, caNamespaceName);
	LPSTR S = S2;
	for (int i = 0;; ++i)
	{
		if (!xr_strlen(S))
		{
			script_log(ScriptStorage::eLuaMessageTypeError, "the namespace name %s is incorrect!", caNamespaceName);
			return (false);
		}
		LPSTR S1 = strchr(S, '.');
		if (S1)
			*S1 = 0;

		if (i)
			xr_strcat(b, b_size, "{");
		xr_strcat(b, b_size, S);
		xr_strcat(b, b_size, "=");
		if (i)
			xr_strcat(c, c_size, "}");
		if (S1)
			S = ++S1;
		else
			break;
	}

	return (true);
}

bool CScriptStorage::load_buffer(lua_State* L, LPCSTR caBuffer, size_t tSize, LPCSTR caScriptName,
                                 LPCSTR caNameSpaceName)
{
	int l_iErrorCode;
	if (caNameSpaceName && xr_strcmp("_G", caNameSpaceName))
	{
		string512 insert, a, b;

		LPCSTR header = file_header;

		if (!parse_namespace(caNameSpaceName, a, sizeof(a), b, sizeof(b)))
			return (false);

		xr_sprintf(insert, header, caNameSpaceName, a, b);
		u32 str_len = xr_strlen(insert);
		u32 const total_size = str_len + tSize;
		LPSTR script = 0;
		bool dynamic_allocation = false;

		__try
		{
			if (total_size < 768 * 1024)
				script = (LPSTR)_alloca(total_size);
			else
			{
#ifdef DEBUG
                script = (LPSTR)Memory.mem_alloc(total_size, "lua script file");
#else //!DEBUG
				script = (LPSTR)Memory.mem_alloc(total_size);
#endif //-DEBUG
				dynamic_allocation = true;
			}
		}
		__except (GetExceptionCode() == STATUS_STACK_OVERFLOW)
		{
			int errcode = _resetstkoflw();
			R_ASSERT2(errcode, "Could not reset the stack after \"Stack overflow\" exception!");
#ifdef DEBUG
            script					= (LPSTR)Memory.mem_alloc(total_size, "lua script file (after exception)");
#else //#ifdef DEBUG
			script = (LPSTR)Memory.mem_alloc(total_size);
#endif //#ifdef DEBUG
			dynamic_allocation = true;
		};

		xr_strcpy(script, total_size, insert);
		CopyMemory(script + str_len, caBuffer, u32(tSize));

		l_iErrorCode = luaL_loadbuffer(L, script, tSize + str_len, caScriptName);

		if (dynamic_allocation)
			xr_free(script);
	}
	else
	{
		//		try
		{
			l_iErrorCode = luaL_loadbuffer(L, caBuffer, tSize, caScriptName);
		}
		//		catch(...) {
		//			l_iErrorCode= LUA_ERRSYNTAX;
		//		}
	}

	if (l_iErrorCode)
	{
//#ifdef DEBUG
		if (strstr(Core.Params, "-dbg")) print_output(L,caScriptName,l_iErrorCode);
//#endif //-DEBUG
		on_error(L);
		return (false);
	}
	return (true);
}

xr_unordered_map<std::string, std::set<std::string>> unlocalizers;
bool unlocalizerPassed = false;

static std::string join_list(const std::vector<std::string>& items_vec, std::string delim = "\n") {
	std::string ret;
	for (const auto& i : items_vec) {
		if (!ret.empty()) {
			ret += delim;
		}
		ret += i;
	}
	return ret;
};

static bool unlocalRegex(std::set<std::string>& unlocals, std::string& s, const std::regex& pattern, const int group, const std::string& replacement) {
	if (std::regex_match(s, pattern)) {
		//Msg("matching local function pattern");
		std::smatch match;
		std::regex_search(s, match, pattern);
		std::string variable = match[group];
		if (unlocals.find(variable) != unlocals.end()) {
			Msg("[unlocalRegex] found variable %s to unlocal", variable.c_str());
			s = std::regex_replace(s, pattern, replacement);
			return true;
		}
	} else {
		return false;
	}
	return false;
};

bool CScriptStorage::do_file(LPCSTR caScriptName, LPCSTR caNameSpaceName)
{
	if (!unlocalizerPassed) {
		auto file_list = FS.file_list_open("$game_config$", "unlocalizers\\", FS_RootOnly | FS_ListFiles);
		if (!file_list) {
			unlocalizerPassed = true;
		} else {
			xr_string id;
			auto i = file_list->begin();
			auto e = file_list->end();
			for (; i != e; ++i)
			{
				u32 length = xr_strlen(*i);

				if (!((length >= 4) &&
					((*i)[length - 4] == '.') &&
					((*i)[length - 3] == 'l') &&
					((*i)[length - 2] == 't') &&
					((*i)[length - 1] == 'x')))
					continue;

				id.assign(*i, length - 4);

				string_path file_name;
				FS.update_path(file_name, "$game_config$", (xr_string("unlocalizers\\") + id).c_str());
				xr_strcat(file_name, ".ltx");

				Msg("opening file %s", file_name);
				auto config = xr_new<CInifile>(file_name);

				typedef CInifile::Root sections_type;
				sections_type& sections = config->sections();

				sections_type::const_iterator i = sections.begin();
				sections_type::const_iterator e = sections.end();
				for (; i != e; ++i)
				{
					auto sectionName = std::string((*i).Name.c_str());
					toLowerCase(sectionName);
					if (unlocalizers.find(sectionName) == unlocalizers.end()) {

						// construct set that contains top level variables to delocalize by section name
						unlocalizers[sectionName].clear();
						Msg("creating unlocalizer for script %s", sectionName.c_str());
					}
					auto& data = (*i).Data;
					for (auto& item : data) {
						unlocalizers[sectionName].insert(std::string(item.first.c_str()));
						Msg("adding variable %s for unlocalizer for script %s", item.first.c_str(), sectionName.c_str());
					}
				}
				xr_delete(config);
			}
			FS.file_list_close(file_list);
			unlocalizerPassed = true;
		}
	}
	int start = lua_gettop(lua());
	string_path l_caLuaFileName;
	IReader* l_tpFileReader = FS.r_open(caScriptName);

	if (!l_tpFileReader)
	{
		script_log(eLuaMessageTypeError, "Cannot open file \"%s\"", caScriptName);
		return (false);
	}

	// Unlocalize variables in the script defined by unlocalizers map
	auto scriptContents = static_cast<LPCSTR>(l_tpFileReader->pointer());
	auto scriptLength = (size_t)l_tpFileReader->length();
	bool unlocalPerformed = false;
	std::string unlocalizerResult;
	std::string loweredNameSpaceName = caNameSpaceName;
	toLowerCase(loweredNameSpaceName);
	if (unlocalizers.find(loweredNameSpaceName) != unlocalizers.end()) {
		Msg("found script %s in unlocalizers data", caNameSpaceName);

		// Get contents of the script file and split by lines
		std::vector<std::string> tokens;
		std::string temp;
		while (!l_tpFileReader->eof())
		{
			char c = l_tpFileReader->r_u8();
			temp += c;
		}

		std::stringstream stringStream(temp);
		std::string line;
		tokens.clear();
		while (std::getline(stringStream, line)) {
			tokens.push_back(line);
		}

		// Iterate lines and unlocalize variables
		auto& unlocals = unlocalizers[loweredNameSpaceName];

		/*for (auto& u : unlocals) {
			Msg("%s", u);
		}*/

		for (auto& s : tokens) {

			//Msg("%s", s.c_str());

			trim(s, "\n\r");
			if (s.empty()) {
				continue;
			}

			std::regex pattern;

			//local function x(a,b,c)
			pattern = std::regex(R"((^local)([\t ]+)(function)([\t ]+)([_a-zA-Z].*)([\t ]*)(\(.*$))");
			if (unlocalRegex(unlocals, s, pattern, 5, "$3$4$5$6$7")) {
				unlocalPerformed = true;
				continue;
			}

			//local a = ...
			//local a
			//local a,b,c = ... (if one of a,b,c is in unlocalizers list - all of them will be unlocalized)
			//local x; local y; - unsupported yet
			pattern = std::regex(R"((^local)([\t ]+)(.*))");
			if (std::regex_match(s, pattern)) {
				std::smatch match;
				std::regex_search(s, match, pattern);
				std::string m = match[3];

				// strip comments
				std::regex r = std::regex(R"((.*)--.*)");
				if (std::regex_match(m, r)) {
					//Msg("found comments\n");
					std::smatch noncomments;
					std::regex_search(m, noncomments, r);
					m = noncomments[1];
				}

				auto variablesAndValues = splitStringLimit(m, "=", 1);
				bool hasValue = variablesAndValues.size() > 1;
				auto variables = splitStringMulti(variablesAndValues[0], ",");
				for (auto v : variables) {
					trim(v);
					//Msg("%s\n", v.c_str());
					if (unlocals.find(v) != unlocals.end()) {
						unlocalPerformed = true;
						Msg("found variable %s to unlocal", v.c_str());
						s = std::regex_replace(s, pattern, "$3");
						if (!hasValue) {

							// strip comments
							std::regex r = std::regex(R"((.*)(--.*))");
							if (std::regex_match(s, r)) {
								//Msg("found comments\n");
								std::smatch noncomments;
								std::regex_search(s, noncomments, r);
								s = std::string(noncomments[1]) + "= nil " + std::string(noncomments[2]);
							} else {
								s += " = nil";
							}
						}
						break;
					}
				}
			}
		}

		// Store result back
		/*for (auto& s : tokens) {
			Msg("%s", s.c_str());
		}*/

		unlocalizerResult = join_list(tokens);
		scriptContents = unlocalizerResult.c_str();
		scriptLength = strlen(scriptContents);
	}

	strconcat(sizeof(l_caLuaFileName), l_caLuaFileName, "@", caScriptName);

	bool bufferLoaded = false;
	if (unlocalPerformed) {
		bufferLoaded = load_buffer(lua(), scriptContents, scriptLength, l_caLuaFileName, caNameSpaceName);
	} else {
		l_tpFileReader->rewind();
		bufferLoaded = load_buffer(lua(), static_cast<LPCSTR>(l_tpFileReader->pointer()), (size_t)l_tpFileReader->length(), l_caLuaFileName, caNameSpaceName);
	}

	if (!bufferLoaded)
	{
		//		VERIFY		(lua_gettop(lua()) >= 4);
		//		lua_pop		(lua(),4);
		//		VERIFY		(lua_gettop(lua()) == start - 3);
		lua_settop(lua(), start);
		FS.r_close(l_tpFileReader);
		return (false);
	}
	FS.r_close(l_tpFileReader);

	int errFuncId = -1;
#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
    if( ai().script_engine().debugger() )
        errFuncId = ai().script_engine().debugger()->PrepareLua(lua());
#	endif // #ifndef USE_LUA_STUDIO
#endif // #ifdef USE_DEBUGGER
	if (0) //.
	{
		for (int i = 0; lua_type(lua(), -i - 1); i++)
			Msg("%2d : %s", -i - 1, lua_typename(lua(), lua_type(lua(), -i - 1)));
	}

	// because that's the first and the only call of the main chunk - there is no point to compile it
	//	luaJIT_setmode	(lua(),0,LUAJIT_MODE_ENGINE|LUAJIT_MODE_OFF);						// Oles
	int l_iErrorCode = lua_pcall(lua(), 0, 0, (-1 == errFuncId) ? 0 : errFuncId); // new_Andy
	//	luaJIT_setmode	(lua(),0,LUAJIT_MODE_ENGINE|LUAJIT_MODE_ON);						// Oles

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
    if( ai().script_engine().debugger() )
        ai().script_engine().debugger()->UnPrepareLua(lua(),errFuncId);
#	endif // #ifndef USE_LUA_STUDIO
#endif // #ifdef USE_DEBUGGER
	if (l_iErrorCode)
	{
//#ifdef DEBUG
		if (strstr(Core.Params, "-dbg")) print_output(lua(),caScriptName,l_iErrorCode);
//#endif
		on_error(lua());
		lua_settop(lua(), start);
		return (false);
	}

	return (true);
}

bool CScriptStorage::load_file_into_namespace(LPCSTR caScriptName, LPCSTR caNamespaceName)
{
	int start = lua_gettop(lua());
	if (!do_file(caScriptName, caNamespaceName))
	{
		Msg("! [ERROR] --- Failed to load script %s", caNamespaceName);
		lua_settop(lua(), start);
		return (false);
	}
	VERIFY(lua_gettop(lua()) == start);
	return (true);
}

bool CScriptStorage::namespace_loaded(LPCSTR N, bool remove_from_stack)
{
	int start = lua_gettop(lua());
	lua_pushstring(lua(), "_G");
	lua_rawget(lua(), LUA_GLOBALSINDEX);
	string256 S2;
	xr_strcpy(S2, N);
	LPSTR S = S2;
	for (;;)
	{
		if (!xr_strlen(S))
		{
			VERIFY(lua_gettop(lua()) >= 1);
			lua_pop(lua(), 1);
			VERIFY(start == lua_gettop(lua()));
			return (false);
		}
		LPSTR S1 = strchr(S, '.');
		if (S1)
			*S1 = 0;
		lua_pushstring(lua(), S);
		lua_rawget(lua(), -2);
		if (lua_isnil(lua(), -1))
		{
			//			lua_settop		(lua(),0);
			VERIFY(lua_gettop(lua()) >= 2);
			lua_pop(lua(), 2);
			VERIFY(start == lua_gettop(lua()));
			return (false); //	there is no namespace!
		}
		else if (!lua_istable(lua(), -1))
		{
			// §3c INSTRUMENT: the stock message names the failure CLASS and nothing else, which
			// is why a headless co-op server that dies here leaves a log with no lead in it —
			// one of these killed a Q4 leg-1 run and was read as "the log just stops". The three
			// facts that identify it are all on hand at this instant and none of them survive
			// the pop, so capture them first: which COMPONENT is in the way, the full dotted
			// name being resolved, and WHAT the offender actually is.
			LPCSTR const l_type = lua_typename(lua(), lua_type(lua(), -1));
			string256 l_component;
			xr_strcpy(l_component, S);
			// Msg before FATAL on purpose: the fatal handler dumps console variables and has
			// itself faulted mid-dump on this server, so a line that is already flushed is the
			// only one guaranteed to be readable afterwards.
			Msg("! [script] namespace '%s' is a %s, not a table — cannot load it as a script "
			    "module (resolving '%s')", l_component, l_type, N);
			//				lua_settop	(lua(),0);
			VERIFY(lua_gettop(lua()) >= 1);
			lua_pop(lua(), 1);
			VERIFY(start == lua_gettop(lua()));
			Debug.fatal(DEBUG_INFO,
			            " Error : the namespace name is already being used by the non-table "
			            "object!\n         namespace '%s' is a %s (resolving '%s')",
			            l_component, l_type, N);
			return (false);
		}
		lua_remove(lua(), -2);
		if (S1)
			S = ++S1;
		else
			break;
	}
	if (!remove_from_stack)
	{
		VERIFY(lua_gettop(lua()) == start + 1);
	}
	else
	{
		VERIFY(lua_gettop(lua()) >= 1);
		lua_pop(lua(), 1);
		VERIFY(lua_gettop(lua()) == start);
	}
	return (true);
}

bool CScriptStorage::object(LPCSTR identifier, int type)
{
	int start = lua_gettop(lua());
	lua_pushnil(lua());
	while (lua_next(lua(), -2))
	{
		if ((lua_type(lua(), -1) == type) && !xr_strcmp(identifier, lua_tostring(lua(), -2)))
		{
			VERIFY(lua_gettop(lua()) >= 3);
			lua_pop(lua(), 3);
			VERIFY(lua_gettop(lua()) == start - 1);
			return (true);
		}
		lua_pop(lua(), 1);
	}
	VERIFY(lua_gettop(lua()) >= 1);
	lua_pop(lua(), 1);
	VERIFY(lua_gettop(lua()) == start - 1);
	return (false);
}

bool CScriptStorage::object(LPCSTR namespace_name, LPCSTR identifier, int type)
{
	int start = lua_gettop(lua());
	if (xr_strlen(namespace_name) && !namespace_loaded(namespace_name, false))
	{
		VERIFY(lua_gettop(lua()) == start);
		return (false);
	}
	bool result = object(identifier, type);
	VERIFY(lua_gettop(lua()) == start);
	return (result);
}

::luabind::object CScriptStorage::name_space(LPCSTR namespace_name)
{
	string256 S1;
	xr_strcpy(S1, namespace_name);
	LPSTR S = S1;
	::luabind::object lua_namespace = ::luabind::get_globals(lua());
	for (;;)
	{
		if (!xr_strlen(S))
			return (lua_namespace);
		LPSTR I = strchr(S, '.');
		if (!I)
			return (lua_namespace[S]);
		*I = 0;
		lua_namespace = lua_namespace[S];
		S = I + 1;
	}
}



struct raii_guard : private xray::noncopyable
{
	int m_error_code;
	LPCSTR const& m_error_description;

	raii_guard(int error_code, LPCSTR const& m_description) : m_error_code(error_code),
	                                                          m_error_description(m_description)
	{
	}

	~raii_guard()
	{
#ifdef DEBUG
        bool lua_studio_connected = !!ai().script_engine().debugger();
        if (!lua_studio_connected)
#endif //-DEBUG
		{
#ifdef DEBUG
            static bool const break_on_assert	= !!strstr(Core.Params,"-break_on_assert");
#else //!DEBUG
			static bool const break_on_assert = false; //Alundaio: Can't get a proper stack trace with this enabled
#endif //-DEBUG
			if (!m_error_code)
				return;

			if (break_on_assert)
				R_ASSERT2(!m_error_code, m_error_description);
			else
				Msg("! [SCRIPT ERROR]: %s", m_error_description);
		}
	}
}; //-struct raii_guard

bool CScriptStorage::print_output(lua_State* L, LPCSTR caScriptFileName, int iErorCode)
{
	if (iErorCode)
		print_error(L, iErorCode);

	LPCSTR S = "see call_stack for details!";

	raii_guard guard(iErorCode, S);

	if (!lua_isstring(L, -1))
		return (false);

	S = lua_tostring(L, -1);
	if (!xr_strcmp(S, "cannot resume dead coroutine"))
	{
		VERIFY2("Please do not return any values from main!!!", caScriptFileName);
#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
        if(ai().script_engine().debugger() && ai().script_engine().debugger()->Active() ){
            ai().script_engine().debugger()->Write(S);
            ai().script_engine().debugger()->ErrorBreak();
        }
#	endif //!USE_LUA_STUDIO
#endif //-USE_DEBUGGER
	}
	else
	{
		if (!iErorCode)
			script_log(ScriptStorage::eLuaMessageTypeInfo, "Output from %s", caScriptFileName);
		script_log(iErorCode ? ScriptStorage::eLuaMessageTypeError : ScriptStorage::eLuaMessageTypeMessage, "%s", S);
#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
        if (ai().script_engine().debugger() && ai().script_engine().debugger()->Active()) {
            ai().script_engine().debugger()->Write		(S);
            ai().script_engine().debugger()->ErrorBreak	();
        }
#	endif //!USE_LUA_STUDIO
#endif //-USE_DEBUGGER
	}
	return (true);
}

void CScriptStorage::print_error(lua_State* L, int iErrorCode)
{
	switch (iErrorCode)
	{
	case LUA_ERRRUN:
		{
			script_log(ScriptStorage::eLuaMessageTypeError, "SCRIPT RUNTIME ERROR");
			break;
		}
	case LUA_ERRMEM:
		{
			script_log(ScriptStorage::eLuaMessageTypeError, "SCRIPT ERROR (memory allocation)");
			break;
		}
	case LUA_ERRERR:
		{
			script_log(ScriptStorage::eLuaMessageTypeError, "SCRIPT ERROR (while running the error handler function)");
			break;
		}
	case LUA_ERRFILE:
		{
			script_log(ScriptStorage::eLuaMessageTypeError, "SCRIPT ERROR (while running file)");
			break;
		}
	case LUA_ERRSYNTAX:
		{
			script_log(ScriptStorage::eLuaMessageTypeError, "SCRIPT SYNTAX ERROR");
			break;
		}
	case LUA_YIELD:
		{
			script_log(ScriptStorage::eLuaMessageTypeInfo, "Thread is yielded");
			break;
		}
	default: NODEFAULT;
	}
}

#ifdef LUA_DEBUG_PRINT //DEBUG
void CScriptStorage::flush_log()
{
    string_path			log_file_name;
    strconcat           (sizeof(log_file_name),log_file_name,Core.ApplicationName,"_",Core.UserName,"_lua.log");
    FS.update_path      (log_file_name,"$logs$",log_file_name);
    m_output.save_to	(log_file_name);
}
#endif //-LUA_DEBUG_PRINT DEBUG

int CScriptStorage::error_log(LPCSTR format, ...)
{
	va_list marker;
	va_start(marker, format);

	LPCSTR S = "! [LUA][ERROR] ";
	LPSTR S1;
	string4096 S2;
	xr_strcpy(S2, S);
	S1 = S2 + xr_strlen(S);

	int result = vsprintf(S1, format, marker);
	va_end(marker);

	Msg("%s", S2);

	return (result);
}
