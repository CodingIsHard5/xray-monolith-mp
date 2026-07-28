////////////////////////////////////////////////////////////////////////////
//	Module 		: script_storage_inline.h
//	Created 	: 01.04.2004
//  Modified 	: 01.04.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Storage inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

IC lua_State* CScriptStorage::lua()
{
	// MP fork (§3c audit): every engine->Lua interaction asks for the VM pointer here first, so
	// this is the one chokepoint that sees paths nobody thought to read. The thread comparison
	// itself is out of line (it needs GetCurrentThreadId, which this header should not drag in);
	// the fast path when the audit is not armed is the load and branch below and nothing else.
	coop_vm_touch_check();
	return (m_virtual_machine);
}

IC void CScriptStorage::current_thread(CScriptThread* thread)
{
	VERIFY((thread && !m_current_thread) || !thread);
	m_current_thread = thread;
}

IC CScriptThread* CScriptStorage::current_thread() const
{
	return (m_current_thread);
}
