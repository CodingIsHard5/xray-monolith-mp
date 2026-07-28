////////////////////////////////////////////////////////////////////////////
// mp_coop_chunk_reader: bounded reads for the co-op layer's own .scop chunks
// (§14 step 8 phase 3 Q2, extracted for phase 4 R2).
//
// Every co-op chunk is OUR file, and it is still read defensively, for two
// reasons that have nothing to do with malice:
//
//   * a COUNT field is the one value that turns a truncated or half-written
//     chunk into an unbounded loop, and
//   * IReader::r_stringZ(shared_str&) is unbounded by construction — it walks
//     to the next NUL wherever that is, off the end of the buffer included.
//
// So every read is checked against the bytes left in OUR chunk, and a failure
// sets ok=false for good. The caller's contract on !ok is Q2's: discard the
// whole record set rather than keep the prefix — half a table is not a safer
// table, it is a table that disagrees with whatever was read beside it.
//
// This lived as `coop_quest_reader`, file-static in GametaskManager.cpp, until
// the reputation chunk needed the same thing. Two subtly different defensive
// readers is exactly the shape that rots, so there is one.
////////////////////////////////////////////////////////////////////////////
#pragma once

struct coop_chunk_reader
{
	IReader& s;
	int      end;      // one past the last byte of our chunk
	bool     ok;

	coop_chunk_reader(IReader& _s, int _end): s(_s), end(_end), ok(true) {}

	bool room(int need)
	{
		if (ok && s.tell() + need > end)
			ok = false;
		return ok;
	}

	// A count, checked against the bytes actually left: `n` records of at least
	// `min_bytes_each` cannot fit, so refuse before allocating or looping.
	u32 count(u32 min_bytes_each)
	{
		if (!room(4))
			return 0;
		const u32 n = s.r_u32();
		if (u64(n) * u64(min_bytes_each) > u64(end - s.tell()))
		{
			ok = false;
			return 0;
		}
		return n;
	}

	bool str(shared_str& out)
	{
		if (!ok)
			return false;
		const char* const base = (const char*)s.pointer();
		const int avail = end - s.tell();
		int n = 0;
		while (n < avail && base[n])
			++n;
		if (n >= avail)            // no terminator inside the chunk -> refuse
		{
			ok = false;
			return false;
		}
		s.r_stringZ(out);
		return true;
	}

	u64 u64v() { return room(8) ? s.r_u64() : u64(0); }
	u32 u32v() { return room(4) ? s.r_u32() : 0u; }
	s32 s32v() { return room(4) ? s32(s.r_u32()) : s32(0); }
	u16 u16v() { return room(2) ? s.r_u16() : u16(0); }
	u8  u8v()  { return room(1) ? s.r_u8()  : u8(0); }
};
