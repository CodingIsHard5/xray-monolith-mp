#ifndef LUABIND_COOP_VM_AUDIT_HPP_INCLUDED
#define LUABIND_COOP_VM_AUDIT_HPP_INCLUDED

// MP fork (§3c audit, dev/INSTABILITY_PLAN.md §4.4.ii and §4f) — THE PROBE, IN ONE PLACE.
//
// WHY THIS HEADER EXISTS, and it is a correction rather than a tidy-up.
// §4b placed the audit at two accessors and argued the pair was complete: `CScriptStorage::lua()`
// for the engine's own asks, and `luabind::functor<>::lua_state()` for the blind spot that leaves
// (a functor resolved once holds its own `lua_State*` and never asks the engine again). On
// 2026-07-30 a client-driven save produced a symbolicated fault stack that passes NEITHER:
//
//     lj_vmeta_call
//     luabind::detail::pcall                     <-- here
//     CScriptBinderObjectWrapper::save
//     CScriptBinder::save
//     game_sv_Single::save_game
//     xr_enet::server_transport::pump_thread     <-- the ENet pump thread
//
// `CScriptBinderObjectWrapper::save` reaches `pcall` with a `lua_State*` it already holds, so the
// audit reported ZERO sites inside `OnMessage` for a run that demonstrably ran Lua from the pump
// thread. That silence was a property of where the probes were, not of the engine — which means
// §4.4.ii's answer was never as wide as it read.
//
// `luabind::detail::pcall` is the accessor every luabind call must pass, so instrumenting it is
// what makes a zero mean something. It is called once per Lua CALL, not once per pushed argument
// — which is the distinction that matters here: §4b's first attempt cost a whole run because it
// called out of line inside luabind's per-argument inner loops and made the server ~10x slower.
// One predicted-not-taken branch per call is a different order of cost.
//
// The declarations are spelled out rather than pulled from the engine header, exactly as
// functor.hpp did: luabind is 3rd-party and must not acquire an engine include. `unsigned int`
// rather than `u32` for the same reason — it is what u32 is, and a mismatch would fail to LINK
// rather than silently read a different variable.
//
// gs:[0x48] is ClientId.UniqueThread in the x64 TEB — one instruction where GetCurrentThreadId is
// a cross-DLL call. The constant is VERIFIED against GetCurrentThreadId at arm time and the audit
// REFUSES TO ARM if they disagree, because a filter that silently under-triggers prints an empty
// site list, and an empty site list reads exactly like a clean result.

#include <intrin.h>

extern unsigned int g_coop_vm_audit;        // 0 = off, 1 = armed (set once at boot)
extern unsigned int g_coop_game_thread_id;  // stamped every frame in CLevel::OnFrame
void coop_vm_touch_offthread();             // out of line; confirms the thread and reports

inline void coop_vm_touch_check_luabind()
{
	if (g_coop_vm_audit && __readgsdword(0x48) != g_coop_game_thread_id)
		coop_vm_touch_offthread();
}

#endif // LUABIND_COOP_VM_AUDIT_HPP_INCLUDED
