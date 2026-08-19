#include "stdafx.h"
#include <stdio.h>   // §5x: fopen/fputs/fclose for the append-only flush
#pragma hdrstop

#include <time.h>
#include "resource.h"
#include "log.h"
#ifdef _EDITOR
#include "malloc.h"
#endif

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

#include "profiler.h"

extern BOOL LogExecCB = TRUE;
static string_path logFName = "engine.log";
static string_path log_file_name = "engine.log";
static BOOL no_log = TRUE;
#ifdef PROFILE_CRITICAL_SECTIONS
static xrCriticalSection logCS(MUTEX_PROFILE_ID(log));
#else // PROFILE_CRITICAL_SECTIONS
static xrCriticalSection logCS;
#endif // PROFILE_CRITICAL_SECTIONS
xr_vector<xr_string> LogFile;
static LogCallback LogCB = 0;

// §5x — serialises WRITERS so two concurrent flushes cannot interleave their lines in the file.
// Lock order is logWriteCS -> logCS and never the reverse: AddOne takes logCS alone, FlushLog takes
// this one and then logCS briefly. Stated because log.cpp has already cost this project one
// two-lock deadlock.
static xrCriticalSection logWriteCS;
static bool s_log_appended = false;   // has anything been appended since CreateLog truncated it?

void FlushLog()
{
	PROF_EVENT();

	if (!no_log)
	{
		PROF_EVENT("Flushing");
		// SECOND A-SIDE SITE for the invariant on AddOne below: this holds logCS across file I/O,
		// and the filesystem layer allocates. It is left as-is DELIBERATELY, with the reason
		// recorded rather than silently accepted:
		//
		// FlushLog rewrites the WHOLE file from LogFile every call, so the lock cannot simply be
		// dropped around the I/O — a concurrent AddOne would mutate the vector mid-write. Making it
		// safe means snapshotting under the lock (an allocation of its own, though one that does not
		// nest a second lock) or switching to append-only writes with a persistent handle. Both are
		// real changes to how the log file is produced, and this defect does not license them at
		// 3am on the back of a deadlock fix.
		//
		// It is also the far colder path: AddOne runs per line, FlushLog runs at explicit flushes.
		// The measured cycle went through AddOne. Recorded here so the next worker finds a named
		// follow-up instead of a trap.
		// §5x — THE LOG IS NOW APPEND-ONLY AND THE IN-MEMORY BUFFER IS DRAINED. This replaces a
		// whole-file rewrite of an unbounded vector, and it fixes three things the old shape had:
		//
		//  1. MEMORY. `LogFile` used to hold every line for the life of the process -- §5k named
		//     `AddOne` as the leading allocation band on the whole leak axis, at two retained
		//     allocations per line, never freed. A 24 h session at the rate these runs log would
		//     retain a few hundred MB of strings. Draining on flush bounds it by the flush
		//     interval instead (3 s on the dedicated server), so the steady-state residency is
		//     tens of lines rather than millions.
		//  2. QUADRATIC FILE I/O. Rewriting the WHOLE file every flush meant a 20,000-line log was
		//     written ~300 times in a 900 s run -- hundreds of MB of I/O to produce one MB of log.
		//  3. I/O UNDER `logCS`. The old code held the log lock across `w_open`/`w_close`, which
		//     this file's own comment flags as a named follow-up needing "snapshotting under the
		//     lock (an allocation of its own)". A SWAP is that snapshot and costs no allocation:
		//     take the lock, swap the vector out, release, then write with the lock not held.
		//
		// What does NOT change: every line still reaches the file, in order, and the file lags by
		// at most one flush interval exactly as before. That is the property every instrument on
		// the §5 axis depends on, and it is gated on rather than assumed (§5x gate A).
		// PING-PONG, and it is not a flourish. A plain local `pending` would take LogFile's
		// STORAGE with it on the swap, leaving LogFile with zero capacity -- so the very next
		// AddOne would reallocate INSIDE logCS, which is the exact edge InitLog's reserve exists
		// to keep away from and which this file has already paid for once. Two buffers that swap
		// back and forth keep a reserved allocation on both sides forever: LogFile always receives
		// the other buffer's storage, already reserved and already empty.
		static xr_vector<xr_string> s_pending;
		logWriteCS.Enter();
		if (s_pending.capacity() == 0) s_pending.reserve(65536);   // once, outside logCS
		s_pending.clear();                                         // keeps capacity
		logCS.Enter();
		s_pending.swap(LogFile);        // O(1), allocation-free, nothing acquired underneath
		logCS.Leave();
		xr_vector<xr_string>& pending = s_pending;
		if (!pending.empty())
		{
			// Plain C append: CFileWriter always truncates (`_O_TRUNC`/"wb"), and `logFName` is
			// already an absolute path by the time CreateLog has run. CreateLog creates/truncates
			// the file, so the first append here lands in an empty file.
			FILE* f = ::fopen(logFName, "ab");
			if (f)
			{
				for (const auto& i : pending)
				{
					LPCSTR s = i.c_str();
					::fputs(s ? s : "", f);
					::fputs("\r\n", f);      // matches IWriter::w_string's CR LF exactly
				}
				::fclose(f);
				s_log_appended = true;
			}
			else
			{
				// Could not open the file: put the lines BACK rather than dropping them. A log
				// that silently loses its tail when a handle fails is the failure mode this whole
				// axis has been bitten by (harness RULE 10).
				logCS.Enter();
				if (LogFile.empty()) pending.swap(LogFile);
				else LogFile.insert(LogFile.begin(), pending.begin(), pending.end());
				logCS.Leave();
				// pending keeps whatever storage it now holds; the next call clears it.
			}
		}
		logWriteCS.Leave();
	}
}

std::string getCurrentTimeStamp(LPCSTR format = "%d.%m.%Y %H:%M:%S") {
	using namespace std::chrono;

	// get current time
	auto now = system_clock::now();

	// get number of milliseconds for the current second
	// (remainder after division into seconds)
	auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

	// convert to std::time_t in order to convert to std::tm (broken time)
	auto timer = system_clock::to_time_t(now);

	// convert to broken time
	std::tm bt = *std::localtime(&timer);

	std::ostringstream oss;

	oss << std::put_time(&bt, format); // HH:MM:SS
	oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

	return oss.str();
}

std::string timeInDMYHMSMMM()
{
	return getCurrentTimeStamp("%d.%m.%Y %H:%M:%S");
}

std::string timeInHMSMMM()
{
	return getCurrentTimeStamp("%H:%M:%S");
}

BOOL logTimestamps = FALSE;
enum Console_mark;
extern bool is_console_mark(Console_mark type);

// ============================ INVARIANT: logCS IS NOT HELD ACROSS AN ALLOCATION ==================
//
// §14 step 8 P4 measured a real deadlock on the co-op dedicated server and named both locks from an
// in-engine registry:
//
//     thread A  holds logCS          and waits on  ntdll heap.cs      (it allocated while logging)
//     thread B  holds ntdll heap.cs  and waits on  logCS              (it logged while allocating)
//
// A cycle. The process stays ALIVE and WEDGED — every other thread piles up behind the two, no
// crash is produced and nothing reaches the log, because the log is precisely what is jammed.
//
// This function was the A side, and it was generous about it: under the lock it built a std::string,
// concatenated a timestamp, constructed a `shared_str` — which takes the string container's OWN
// global lock, so two global locks were nested here — grew `LogFile`, and then called an arbitrary
// user callback that may do all of the above again.
//
// THE RULE, stated so a future caller has something to violate rather than a silent trap to fall
// into: **build first, then lock; do not allocate, do not take another lock, and do not call out to
// unknown code while logCS is held.** The lock exists to serialise the append and the de-dup state,
// nothing more.
//
// Said as the property it is really buying: **logCS IS A LEAF LOCK.** Nothing is acquired while it
// is held — not the heap, not the string container, not whatever a callback feels like taking. A
// leaf cannot be one arm of a cycle, so this holds no matter what the OTHER thread is doing, which
// is what makes it a rule rather than a fix for the one inversion that was caught.
//
// It is also why the same rule is NOT applied to CRenderDevice::mt_csEnter, the other lock the wait
// graph named: that one is a ping-pong handoff between the main and secondary threads, and whichever
// thread holds it is running its whole frame underneath, allocation included. Forbidding allocation
// there would forbid the workload. The cycle is prevented from this side instead. See the long note
// at the top of the secondary thread's loop in xrEngine/device.cpp.
//
// The residue, stated rather than glossed: `LogFile.push_back` can still grow the vector under the
// lock, and the rare duplicate-collapse path formats under it. Both are bounded and neither nests a
// second lock. Removing the growth entirely needs a different container for `LogFile` (a fixed ring,
// or a per-thread staging buffer), which is a bigger change than this defect justifies — the cycle
// is broken once the common path stops allocating under the lock. `FlushLog` still holds logCS
// across file I/O and is the remaining A-side site; see the note there.
void AddOne(const char* split)
{
#ifdef DEBUG
    OutputDebugString(split);
    OutputDebugString("\n");
#endif

	// ---- everything below happens with NO LOCK HELD ------------------------------------------
	// demonized: add timestamps to log
	std::string t = split;
	if (logTimestamps) {
		std::string c = "";
		if (t.length() > 0 && is_console_mark((Console_mark)t[0])) {
			c += t[0];
			c += " ";
			t.erase(0, 1);
		}
		t = c + "[" + timeInHMSMMM() + "] " + t;
	}
	// §5x-b — NOT a shared_str, and the reason is measured. `-coop_rasites 8..255` on the capped
	// build shows this line's allocation site at **937 allocs / 0 frees, every block still live**,
	// while the sibling site (the xr_string below) now frees normally after the §5x drain. A
	// `shared_str` is interned in xrCore's global string container, so every DISTINCT log line the
	// server ever emits is retained there for the life of the process -- the same unbounded-growth
	// defect §5x just removed from `LogFile`, one layer along, and invisible until the log itself
	// stopped dominating the band.
	//
	// Nothing here needs interning. `temp` exists only to compare against the previous line and to
	// build `line`; a plain std::string does both, allocates once, and frees on scope exit. The
	// comment this replaces described the LOCKING rationale, which is unaffected: constructing a
	// std::string takes no global lock at all, so the property it was protecting is strictly
	// improved rather than preserved.
	const std::string& temp = t;
	xr_string line(temp.c_str());          // the allocation that used to happen under the lock

	// ---- lock held from here, for the shared state only ---------------------------------------
	{
		logCS.Enter();

		static std::string last_str;   // §5x-b: was shared_str -- see the note at `temp`
		static int items_count;

		if (last_str == temp)
		{
			// Duplicate collapse. This path DOES format under the lock, because the counter it
			// prints is the shared state being read. It runs only for a repeated identical line.
			if (items_count == 0)
				items_count = 2;
			else
				items_count++;

			xr_string tmp = temp.c_str();   // §5x-b: temp is a std::string now; c_str() unchanged
			tmp += " [";
			tmp += std::to_string(items_count).c_str();
			tmp += "]";

			// §5x: the buffer is DRAINED on every flush now, so the line this collapse wants to
			// replace may already have gone to the file and be gone from memory. Erasing
			// `end()-1` of an empty vector is undefined behaviour, so the emptiness is checked.
			// The file content is identical either way: previously the flushed "msg" stayed in the
			// file and the in-memory copy was replaced by "msg [2]"; now "msg" is in the file and
			// "msg [2]" is appended after it.
			if (!LogFile.empty()) LogFile.erase(LogFile.end() - 1);
			LogFile.push_back(tmp);
		}
		else
		{
			LogFile.push_back(std::move(line));   // moved, not rebuilt
			last_str = temp;
			items_count = 0;
		}

		logCS.Leave();
	}

	// The callback is UNKNOWN CODE: it can allocate, log, or take any lock it likes. Calling it
	// under logCS is how a single misbehaving subscriber turns into the wedge above. Outside.
	if (LogExecCB && LogCB)
		LogCB(split);
}

void Log(const char* s)
{
	int i, j;

	u32 length = xr_strlen(s);
#ifndef _EDITOR
	PSTR split = (PSTR)_alloca((length + 1) * sizeof(char));
#else
    PSTR split = (PSTR)alloca((length + 1) * sizeof(char));
#endif
	for (i = 0, j = 0; s[i] != 0; i++)
	{
		if (s[i] == '\n')
		{
			split[j] = 0; // end of line
			if (split[0] == 0)
			{
				split[0] = ' ';
				split[1] = 0;
			}
			AddOne(split);
			j = 0;
		}
		else
		{
			split[j++] = s[i];
		}
	}
	split[j] = 0;
	AddOne(split);
}

void __cdecl Msg(const char* format, ...)
{
	va_list mark;
	string2048 buf;
	va_start(mark, format);
	int sz = _vsnprintf(buf, sizeof(buf) - 1, format, mark);
	buf[sizeof(buf) - 1] = 0;
	va_end(mark);
	if (sz) Log(buf);
}

// Balloon campaign step 1 (2026-08-18): the C-linkage log entrance for the LuaJIT low-address
// arena allocator ("src/3rd party/luajit-2/src/xr_alloc.c"). That file is C, and it is compiled
// into a static lib that is pulled into more than one module, so it can neither name the
// C++-mangled Msg nor rely on a function pointer some other module happened to set. One export,
// no formatting of its own — the caller has already built the line.
extern "C" XRCORE_API void __cdecl xr_alloc_msg(const char* s)
{
	if (s && *s) Log(s);
}

// Step 1b: see log.h for why this is a setter and not an exported pointer. Null until the script
// engine registers, which means an arena failure before the VM exists prints no context rather
// than inventing one.
static xr_alloc_lua_ctx_fn s_xr_alloc_lua_ctx = NULL;

extern "C" XRCORE_API void xr_alloc_set_lua_context_fn(xr_alloc_lua_ctx_fn fn)
{
	s_xr_alloc_lua_ctx = fn;
}

extern "C" XRCORE_API void __cdecl xr_alloc_lua_context(char* out, size_t out_size)
{
	if (!out || !out_size) return;
	out[0] = 0;
	if (s_xr_alloc_lua_ctx) s_xr_alloc_lua_ctx(out, out_size);
}

void Log(const char* msg, const char* dop)
{
	if (!dop)
	{
		Log(msg);
		return;
	}

	u32 buffer_size = (xr_strlen(msg) + 1 + xr_strlen(dop) + 1) * sizeof(char);
	PSTR buf = (PSTR)_alloca(buffer_size);
	strconcat(buffer_size, buf, msg, " ", dop);
	Log(buf);
}

void Log(const char* msg, u32 dop)
{
	u32 buffer_size = (xr_strlen(msg) + 1 + 10 + 1) * sizeof(char);
	PSTR buf = (PSTR)_alloca(buffer_size);

	xr_sprintf(buf, buffer_size, "%s %d", msg, dop);
	Log(buf);
}

void Log(const char* msg, int dop)
{
	u32 buffer_size = (xr_strlen(msg) + 1 + 11 + 1) * sizeof(char);
	PSTR buf = (PSTR)_alloca(buffer_size);

	xr_sprintf(buf, buffer_size, "%s %i", msg, dop);
	Log(buf);
}

void Log(const char* msg, float dop)
{
	// actually, float string representation should be no more, than 40 characters,
	// but we will count with slight overhead
	u32 buffer_size = (xr_strlen(msg) + 1 + 64 + 1) * sizeof(char);
	PSTR buf = (PSTR)_alloca(buffer_size);

	xr_sprintf(buf, buffer_size, "%s %f", msg, dop);
	Log(buf);
}

void Log(const char* msg, const Fvector& dop)
{
	u32 buffer_size = (xr_strlen(msg) + 2 + 3 * (64 + 1) + 1) * sizeof(char);
	PSTR buf = (PSTR)_alloca(buffer_size);

	xr_sprintf(buf, buffer_size, "%s (%f,%f,%f)", msg, VPUSH(dop));
	Log(buf);
}

void Log(const char* msg, const Fmatrix& dop)
{
	u32 buffer_size = (xr_strlen(msg) + 2 + 4 * (4 * (64 + 1) + 1) + 1) * sizeof(char);
	PSTR buf = (PSTR)_alloca(buffer_size);

	xr_sprintf(buf, buffer_size, "%s:\n%f,%f,%f,%f\n%f,%f,%f,%f\n%f,%f,%f,%f\n%f,%f,%f,%f\n",
	           msg,
	           dop.i.x, dop.i.y, dop.i.z, dop._14_,
	           dop.j.x, dop.j.y, dop.j.z, dop._24_,
	           dop.k.x, dop.k.y, dop.k.z, dop._34_,
	           dop.c.x, dop.c.y, dop.c.z, dop._44_
	);
	Log(buf);
}

void LogWinErr(const char* msg, long err_code)
{
	Msg("%s: %s", msg, Debug.error2string(err_code));
}

LogCallback SetLogCB(LogCallback cb)
{
	LogCallback result = LogCB;
	LogCB = cb;
	return (result);
}

LPCSTR log_name()
{
	return (log_file_name);
}

void InitLog()
{
	// THE LAST ALLOCATION UNDER logCS. AddOne's append is the only thing left inside the lock that
	// can touch the heap, and it only does so when the vector grows. A co-op R3.1 run already writes
	// ~7400 lines, so 10000 was one busy session away from reallocating INSIDE the critical section
	// — which is the exact edge the deadlock needs. 262144 xr_strings of headroom costs a few MB
	// once and makes the steady-state append allocation-free, so logCS behaves as a LEAF lock: taken,
	// used, released, with nothing acquired underneath it.
	//
	// This does not make growth impossible, and it is not claimed to. A run long enough to exceed it
	// reallocates once and then has twice the headroom; the amortised count over a whole session is
	// a handful. Removing the possibility entirely needs a different container for LogFile, which is
	// noted at AddOne and is not what this defect justifies.
	// §5x: the buffer is DRAINED on every flush now (every 3 s on the dedicated server), so its
	// steady-state residency is tens of lines rather than the whole session. The reserve is kept --
	// smaller, because it no longer has to cover a whole run -- for the reason it was added: so
	// AddOne's push_back cannot reallocate inside logCS. 65536 covers the pre-flush boot burst,
	// which is the only time this fills, and FlushLog's ping-pong keeps a reserved buffer on both
	// sides so the capacity survives every swap.
	LogFile.reserve(65536);
}

void CreateLog(BOOL nl)
{
	no_log = nl;
	strconcat(sizeof(log_file_name), log_file_name, Core.ApplicationName, "_", Core.UserName, ".log");
	if (FS.path_exist("$logs$"))
		FS.update_path(logFName, "$logs$", log_file_name);
	if (!no_log)
	{
		//Alun: Backup existing log
		xr_string backup_logFName = EFS.ChangeFileExt(logFName, ".bkp");
		FS.file_rename(logFName, backup_logFName.c_str(), true);
		//-Alun
		IWriter* f = FS.w_open(logFName);
		if (f == NULL)
		{
			MessageBox(NULL, "Can't create log file.", "Error", MB_ICONERROR);
			abort();
		}
		FS.w_close(f);
	}
}

void CloseLog(void)
{
	FlushLog();
	LogFile.clear();
}

xr_string FormatString(LPCSTR fmt, ...)
{
	va_list mark;
	string2048 buf;
	va_start(mark, fmt);
	int sz = _vsnprintf(buf, sizeof(buf) - 1, fmt, mark);
	buf[sizeof(buf) - 1] = 0;
	va_end(mark);
	if (sz) return xr_string(buf);
	return xr_string("");
}
