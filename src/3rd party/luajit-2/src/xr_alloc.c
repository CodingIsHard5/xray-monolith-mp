#include "xr_alloc.h"
#include "lj_def.h"
#include "lj_arch.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

// Balloon campaign step 1 (2026-08-18). The specimen died with "not enough memory" while the Lua
// heap held only ~50 MB, so the question the log could not answer was: what did the ARENA look
// like at that moment? Exhausted, or merely fragmented past a contiguous run of the needed
// length? Every allocation failure now says so in one line, in the engine log, whether or not the
// emergency GC goes on to recover it.
//
// The log entrance is xrCore's C-linkage shim (xrCore/log.cpp) rather than a function pointer:
// this file lives in a StaticLibrary that both xrEngine and xrGame pull in, so there can be more
// than one copy of the globals below and a pointer set by one module would not reach the other.
// For the same reason every line carries &g_heap — two different addresses in one log is the
// direct evidence of a second arena, and one address is the direct evidence against it.
extern void __cdecl xr_alloc_msg(const char* s);

typedef long (*PNTAVM)(HANDLE handle, void **addr, ULONG zbits,
		       size_t *size, ULONG alloctype, ULONG prot);
extern PNTAVM ntavm;
/* Number of top bits of the lower 32 bits of an address that must be zero.
** Apparently 0 gives us full 64 bit addresses and 1 gives us the lower 2GB.
*/
#define NTAVM_ZEROBITS		1

#define MAX_SIZE_T		(~(size_t)0)
#define MFAIL			((void *)(MAX_SIZE_T))

// Луаджит выделяет память кусками, кратными 128К
// Поэтому сделаю два пула по эти размеры
#define CHUNK_SIZE (64 * 1024)
#define CHUNK_COUNT 8192

#define CHUNKS_FROM_SIZE(x) ((x + CHUNK_SIZE - 1) / CHUNK_SIZE)

static int inited = 0;
void* g_heap;
char g_heapMap[CHUNK_COUNT + 1];
char* g_firstFreeChunk;
char* find_free(int size);

static long g_initStatus = 0;        /* NTSTATUS from the one reservation, was ignored entirely */
static size_t g_initSize = 0;        /* what the kernel actually handed back                    */
static unsigned int g_failCount = 0; /* allocation failures since process start                 */
static int g_reported = 0;           /* has the arena been described once, post log-init?       */
static char g_msgBuf[512];

/* Occupancy of the arena right now: free chunks, and the longest contiguous free run — the run
** is the number that decides an allocation, because find_free wants one unbroken stretch. */
static void arena_stats(int* freeChunks, int* largestRun)
{
	int fc = 0, best = 0, cur = 0, i;
	for (i = 0; i < CHUNK_COUNT; i++)
	{
		if (g_heapMap[i] == 'x')
		{
			fc++;
			cur++;
			if (cur > best) best = cur;
		}
		else
			cur = 0;
	}
	*freeChunks = fc;
	*largestRun = best;
}

static void arena_report(const char* why, int wantChunks, size_t wantBytes)
{
	int fc = 0, run = 0;
	char want[96];
	arena_stats(&fc, &run);
	if (wantChunks > 0)
		snprintf(want, sizeof(want), " want %d chunk(s) (%llu bytes) |",
			wantChunks, (unsigned long long)wantBytes);
	else
		want[0] = 0;
	/* One line, self-contained, greppable on "COOP(luajit-arena)". The largest contiguous run is
	** the number that actually decides an allocation — a big free total next to a small run is
	** fragmentation, a small free total is exhaustion, and the two want different fixes. */
	snprintf(g_msgBuf, sizeof(g_msgBuf),
		"%s COOP(luajit-arena): %s |%s free %d/%d chunk(s) (%d KiB), largest contiguous run "
		"%d chunk(s) (%d KiB) | base %p size %llu status 0x%08lx | map@%p failures %u",
		(fc == 0 || (wantChunks > 0 && run < wantChunks)) ? "!" : "-",
		why, want,
		fc, (int)CHUNK_COUNT, (int)((long long)fc * CHUNK_SIZE / 1024),
		run, (int)((long long)run * CHUNK_SIZE / 1024),
		g_heap, (unsigned long long)g_initSize, (unsigned long)g_initStatus,
		(void*)&g_heap, g_failCount);
	g_msgBuf[sizeof(g_msgBuf) - 1] = 0;
	xr_alloc_msg(g_msgBuf);
}

/* Called by the engine once logging exists — XR_INIT itself runs at the top of WinMain, before
** xrCore is initialised, so it cannot log a thing at the moment it learns the answer. */
void XR_ARENA_REPORT()
{
	if (g_reported)
		return;
	g_reported = 1;
	arena_report(g_heap ? "reserved" : "RESERVATION FAILED, no arena", 0, 0);
}

//#define DEBUG_MEM
#ifdef DEBUG_MEM
static char buf[100];
void dump_map(void* ptr, size_t size, char c);
#endif

void XR_INIT()
{
	if (inited)
		return;
	g_heap = NULL;
	size_t size = CHUNK_SIZE * CHUNK_COUNT;
	long st = ntavm(INVALID_HANDLE_VALUE, &g_heap, NTAVM_ZEROBITS, &size,
	                MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
	g_initStatus = st;
	g_initSize = size;

	for (int i = 0; i < CHUNK_COUNT; i++)
		g_heapMap[i] = 'x';
	g_heapMap[CHUNK_COUNT] = 0;
	g_firstFreeChunk = g_heapMap;

	// The status used to be read only by the DEBUG_MEM sprintf, i.e. never in a shipping build.
	// A failed reservation leaves g_heap NULL and every chunk marked free, so XR_MMAP would hand
	// LuaJIT pointers computed off NULL — wild addresses instead of a clean out-of-memory. Mark
	// the whole map allocated so find_free fails honestly, and say so at the first report.
	if (st < 0 || g_heap == NULL)
	{
		for (int i = 0; i < CHUNK_COUNT; i++)
			g_heapMap[i] = 'a';
		g_firstFreeChunk = NULL;
	}

#ifdef DEBUG_MEM	
	sprintf(buf, "XR_INIT create_block %p result=%X\r\n", g_heap, st);
	OutputDebugString(buf);
#endif
	inited = 1;
}

void* XR_MMAP(size_t size)
{
#ifdef DEBUG_MEM
	sprintf(buf, "XR_MMAP(%Iu)", size);
	OutputDebugString(buf);
#endif
	int chunks = CHUNKS_FROM_SIZE(size);
	char* s = find_free(chunks);
	void* ptr = MFAIL;
	if (s == NULL) {
		/* This is the exact moment LuaJIT is told "not enough memory". Report the first 64 in
		** full and then one in every 256, so a retry storm cannot bury the fatal one but also
		** cannot flood the log. The ordinal makes any suppression visible. */
		g_failCount++;
		if (g_failCount <= 64 || (g_failCount % 256) == 0)
			arena_report("ALLOCATION FAILED", chunks, size);
	}
	if (s != NULL) {
		ptr = (char*)g_heap + CHUNK_SIZE * (s - g_heapMap);
		for (int i = 0; i < chunks; i++)
			s[i] = 'a';
		if (s == g_firstFreeChunk)
			g_firstFreeChunk = find_free(1);
	}
#ifdef DEBUG_MEM
	sprintf(buf, " ptr=%p chunks %d\r\n", ptr, chunks);
	OutputDebugString(buf);
	dump_map(ptr, size, 'U');
#endif
	return ptr;
}

// Судя по комментарию к CALL_MUNMAP, луаджит может объединять выделенные ему чанки
// и освобождать их как один. Надеюсь он не слепит вместе чанки из разных пулов
void XR_DESTROY(void* ptr, size_t size)
{
#ifdef DEBUG_MEM
	sprintf(buf, "XR_DESTROY(ptr=%p, size=%Iu)", ptr, size);
	OutputDebugString(buf);
#endif
	char* s = g_heapMap + ((char*)ptr - (char*)g_heap) / CHUNK_SIZE;
	int count = CHUNKS_FROM_SIZE(size);
	for (int i = 0; i < count; i++)
		s[i] = 'x';
	if (s < g_firstFreeChunk || !g_firstFreeChunk)
		g_firstFreeChunk = s;
#ifdef DEBUG_MEM	
	dump_map(ptr, size, 'X');
#endif
}

// Находит подряд идущие свободные чанки количеством size, начиная с первого свободного
// Возвращает указатель на группу из heapMap или NULL, если найти не удалось
char* find_free(int size)
{
	char* p = g_firstFreeChunk;
	if (!p) return NULL;

	int count = 0;
	while (*p != '\0') {
		if (*p == 'x')
			count++;
		else
			count = 0;
		p++;
		if (count >= size)
			return p - count;
	}
	return NULL;
}

void XR_EARLY_INIT()
{
	ntavm = (PNTAVM)GetProcAddress(GetModuleHandleA("ntdll.dll"),
	                               "NtAllocateVirtualMemory");
	XR_INIT();
}

static 	char temp[1025];
void dump_map(void* ptr, size_t size, char c)
{
#ifdef DEBUG_MEM
	OutputDebugString("heap:\r\n|-------|-------|-------|-------|-------|-------|-------|-------|-------|-------|-------|-------|-------|-------|-------|-------\r\n");
	strcpy(temp, g_heapMap);
	char *cur = temp + ((char*)ptr - (char*)g_heap) / CHUNK_SIZE;
	for (int i = 0; i < size / CHUNK_SIZE; i++)
		cur[i] = c;
	
	for (int i = 0; i < 8; i++)
	{
		char a = temp[i * 128 + 128];
		temp[i * 128 + 128] = '\0';
		OutputDebugString(temp + i * 128);
		temp[i * 128 + 128] = a;
		OutputDebugString("\r\n");
	}
	OutputDebugString("--------------------------------------------------------------------------------------------------------------------------------\r\n");
#endif
}
