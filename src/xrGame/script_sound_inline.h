////////////////////////////////////////////////////////////////////////////
//	Module 		: script_sound_inline.h
//	Created 	: 06.02.2004
//  Modified 	: 06.02.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script sound class inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

IC u32 CScriptSound::Length()
{
	VERIFY(m_sound._handle());
	return iFloor(m_sound.get_length_sec() * 1000.0f);
}

IC void CScriptSound::Play(CScriptGameObject* object)
{
	Play(object, 0.f, 0);
}

IC void CScriptSound::Play(CScriptGameObject* object, float delay)
{
	Play(object, delay, 0);
}

IC void CScriptSound::PlayAtPos(CScriptGameObject* object, const Fvector& position)
{
	PlayAtPos(object, position, 0.f, 0);
}

IC void CScriptSound::PlayAtPos(CScriptGameObject* object, const Fvector& position, float delay)
{
	PlayAtPos(object, position, delay, 0);
}

IC void CScriptSound::SetMinDistance(const float fMinDistance)
{
	VERIFY(m_sound._handle());
	m_sound.set_range(fMinDistance, GetMaxDistance());
}

IC void CScriptSound::SetMaxDistance(const float fMaxDistance)
{
	VERIFY(m_sound._handle());
	m_sound.set_range(GetMinDistance(), fMaxDistance);
}

// COOP §6f — THESE FOUR DEREFERENCED NULL IN EVERY RELEASE BUILD.
//
// ref_sound::get_params() RETURNS NULL when the sound has no feedback object (Sound.h:554), and the
// only thing standing between that and the dereference was VERIFY(), which compiles out in release
// (see the xray-verify-macros-compile-out note). The symbolicated proof, from CScriptSound::GetVolume
// in build 30977983541:
//
//     14076b446:  xor    %eax,%eax          <- get_params() returned NULL, inlined
//     14076b448:  movss  0x4c(%rax),%xmm0   <- and it read it anyway; 0x4C is CSound_params::volume
//
// That first-chance access violation fires on the dedicated server in six of six recorded runs. It is
// swallowed downstream and is NOT the crash — but it is undefined behaviour on a path Lua can reach
// from a physics script condition, and it costs an exception dispatch every time.
//
// The handling is not invented here: CScriptSound::GetPosition (script_sound.cpp:39) already
// null-checks the same pointer, logs, and returns a neutral value. These four are its siblings that
// never got the same treatment. The log is once-per-getter because a script condition can call these
// every frame, and a per-call Msg would be its own denial of service.
#define COOP_SOUND_NO_PARAMS(what, dflt)                                                       \
	do {                                                                                       \
		static bool s_reported = false;                                                        \
		if (!s_reported) {                                                                     \
			s_reported = true;                                                                 \
			Msg("! COOP(sound): %s on a sound with no feedback ('%s') -- returning %g. "         \
			    "Reported once per getter.", what, m_caSoundToPlay.c_str() ? m_caSoundToPlay.c_str() : "?", (double)(dflt)); \
		}                                                                                      \
		return (dflt);                                                                         \
	} while (0)

IC const float CScriptSound::GetFrequency() const
{
	const CSound_params* p = m_sound.get_params();
	if (!p)
		COOP_SOUND_NO_PARAMS("get_frequency", 1.0f); // 1.0 == unmodified pitch
	return (p->freq);
}

IC const float CScriptSound::GetMinDistance() const
{
	const CSound_params* p = m_sound.get_params();
	if (!p)
		COOP_SOUND_NO_PARAMS("get_min_distance", 0.0f);
	return (p->min_distance);
}

IC const float CScriptSound::GetMaxDistance() const
{
	const CSound_params* p = m_sound.get_params();
	if (!p)
		COOP_SOUND_NO_PARAMS("get_max_distance", 0.0f);
	return (p->max_distance);
}

IC const float CScriptSound::GetVolume() const
{
	const CSound_params* p = m_sound.get_params();
	if (!p)
		COOP_SOUND_NO_PARAMS("get_volume", 0.0f); // a sound that is not playing has no volume
	return (p->volume);
}

#undef COOP_SOUND_NO_PARAMS

IC bool CScriptSound::IsPlaying() const
{
	//  commented for comfort work with -nosound command line option
	//	VERIFY				(m_sound._handle());
	return (!!m_sound._feedback());
}

IC void CScriptSound::AttachTail(LPCSTR caSoundName)
{
	m_sound.attach_tail(caSoundName);
}

IC void CScriptSound::Stop()
{
	VERIFY(m_sound._handle());
	m_sound.stop();
}

IC void CScriptSound::StopDeffered()
{
	VERIFY(m_sound._handle());
	m_sound.stop_deffered();
}

IC void CScriptSound::SetPosition(const Fvector& position)
{
	VERIFY(m_sound._handle());
	m_sound.set_position(position);
}

IC void CScriptSound::SetFrequency(float frequency)
{
	VERIFY(m_sound._handle());
	m_sound.set_frequency(frequency);
}

IC void CScriptSound::SetVolume(float volume)
{
	VERIFY(m_sound._handle());
	m_sound.set_volume(volume);
}

IC const CSound_params* CScriptSound::GetParams()
{
	VERIFY(m_sound._handle());
	return (m_sound.get_params());
}

IC void CScriptSound::SetParams(CSound_params* sound_params)
{
	VERIFY(m_sound._handle());
	m_sound.set_params(sound_params);
}
