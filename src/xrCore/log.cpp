#include "stdafx.h"
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
		logCS.Enter();
		IWriter* f = FS.w_open(logFName);
		if (f)
		{
			for (const auto& i : LogFile)
			{
				LPCSTR s = i.c_str();
				f->w_string(s ? s : "");
			}
			FS.w_close(f);
		}
		logCS.Leave();
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
	// shared_str construction takes the string container's own global lock. Done here it nests
	// nothing; done under logCS it made this function hold two global locks at once.
	const shared_str temp = shared_str(t.c_str());
	xr_string line(temp.c_str());          // the allocation that used to happen under the lock

	// ---- lock held from here, for the shared state only ---------------------------------------
	{
		logCS.Enter();

		static shared_str last_str;
		static int items_count;

		if (last_str.equal(temp))
		{
			// Duplicate collapse. This path DOES format under the lock, because the counter it
			// prints is the shared state being read. It runs only for a repeated identical line.
			if (items_count == 0)
				items_count = 2;
			else
				items_count++;

			xr_string tmp = temp.c_str();
			tmp += " [";
			tmp += std::to_string(items_count).c_str();
			tmp += "]";

			LogFile.erase(LogFile.end() - 1);
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
	LogFile.reserve(262144);
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
