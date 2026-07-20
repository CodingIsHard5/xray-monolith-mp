//////////////////////////////////////////////////////////////////////////
// character_community.cpp:		структура представления группировки
//							
//////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "character_community.h"
#include "../xrNetServer/xr_enet_transport.h"   // MP fork (§19 co-op)

//////////////////////////////////////////////////////////////////////////
COMMUNITY_DATA::COMMUNITY_DATA(CHARACTER_COMMUNITY_INDEX idx, CHARACTER_COMMUNITY_ID idn, LPCSTR team_str)
{
	index = idx;
	id = idn;
	team = (u8)atoi(team_str);
}

//////////////////////////////////////////////////////////////////////////
CHARACTER_COMMUNITY::GOODWILL_TABLE CHARACTER_COMMUNITY::m_relation_table;
CHARACTER_COMMUNITY::SYMPATHY_TABLE CHARACTER_COMMUNITY::m_sympathy_table;

//////////////////////////////////////////////////////////////////////////
CHARACTER_COMMUNITY::CHARACTER_COMMUNITY()
{
	m_current_index = NO_COMMUNITY_INDEX;
}

CHARACTER_COMMUNITY::~CHARACTER_COMMUNITY()
{
}


void CHARACTER_COMMUNITY::set(CHARACTER_COMMUNITY_ID id)
{
	m_current_index = IdToIndex(id);
}

CHARACTER_COMMUNITY_ID CHARACTER_COMMUNITY::id() const
{
	// MP fork (§19 co-op): defence in depth for the crash above. IndexToId asserts hard on an
	// out-of-range index, and this is called from cosmetic paths — the crosshair info HUD, the
	// character card, script queries — for every creature the player looks at. On a thin
	// client any replicated creature whose character data has not arrived yet is momentarily
	// index -1, and taking the whole session down for a missing HUD label is never the right
	// trade. Ask without the assert and fall back to an empty id. Plain SP/MP keep the assert,
	// where an unset community really is a content bug worth failing loudly on.
	if (xr_enet::enabled())
	{
		const COMMUNITY_DATA* data = GetByIndex(m_current_index, true);
		if (!data)
			data = GetByIndex(0, true); // first configured faction: always a real, translatable id
		if (data)
			return data->id;
	}
	return IndexToId(m_current_index);
}

u8 CHARACTER_COMMUNITY::team() const
{
	// Same reasoning as id(), except this one is a raw vector index — an unset community is
	// an out-of-bounds read, not merely an assert. Team 0 is the neutral fallback.
	const COMMUNITY_DATA* const data = GetByIndex(m_current_index, true);
	return data ? data->team : u8(0);
}


void CHARACTER_COMMUNITY::InitIdToIndex()
{
	section_name = "game_relations";
	line_name = "communities";

	m_relation_table.set_table_params("communities_relations");
	m_sympathy_table.set_table_params("communities_sympathy", 1);
}


CHARACTER_GOODWILL CHARACTER_COMMUNITY::relation(CHARACTER_COMMUNITY_INDEX to)
{
	return relation(m_current_index, to);
}

CHARACTER_GOODWILL CHARACTER_COMMUNITY::relation(CHARACTER_COMMUNITY_INDEX from, CHARACTER_COMMUNITY_INDEX to)
{
	VERIFY(from >= 0 && from <(int)m_relation_table.table().size());
	VERIFY(to >= 0 && to <(int)m_relation_table.table().size());

	if (from == NO_COMMUNITY_INDEX || to == NO_COMMUNITY_INDEX)
		return 0;

	return m_relation_table.table()[from][to];
}

void CHARACTER_COMMUNITY::set_relation(CHARACTER_COMMUNITY_INDEX from, CHARACTER_COMMUNITY_INDEX to,
                                       CHARACTER_GOODWILL goodwill)
{
	VERIFY(from >= 0 && from <(int)m_relation_table.table().size());
	VERIFY(to >= 0 && to <(int)m_relation_table.table().size());
	VERIFY(goodwill != NO_GOODWILL);

	if (from == NO_COMMUNITY_INDEX || to == NO_COMMUNITY_INDEX)
		return;

	m_relation_table.table()[from][to] = goodwill;
}

float CHARACTER_COMMUNITY::sympathy(CHARACTER_COMMUNITY_INDEX comm)
{
	VERIFY(comm >= 0 && comm <(int)m_sympathy_table.table().size());

	if (comm == NO_COMMUNITY_INDEX)
		return 0;

	return m_sympathy_table.table()[comm][0];
}

void CHARACTER_COMMUNITY::DeleteIdToIndexData()
{
	m_relation_table.clear();
	m_sympathy_table.clear();
	inherited::DeleteIdToIndexData();
}
