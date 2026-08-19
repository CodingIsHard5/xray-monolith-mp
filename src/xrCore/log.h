#ifndef logH
#define logH

#define VPUSH(a) ((a).x), ((a).y), ((a).z)

void XRCORE_API __cdecl Msg(LPCSTR format, ...);
void XRCORE_API Log(LPCSTR msg);
void XRCORE_API Log(LPCSTR msg);
void XRCORE_API Log(LPCSTR msg, LPCSTR dop);
void XRCORE_API Log(LPCSTR msg, u32 dop);
void XRCORE_API Log(LPCSTR msg, int dop);
void XRCORE_API Log(LPCSTR msg, float dop);
void XRCORE_API Log(LPCSTR msg, const Fvector& dop);
void XRCORE_API Log(LPCSTR msg, const Fmatrix& dop);
void XRCORE_API LogWinErr(LPCSTR msg, long err_code);

// Balloon campaign step 1b (2026-08-19). The LuaJIT low-arena allocator (xr_alloc.c) can now say
// exactly how full the arena was when an allocation failed, but not WHO asked — and the witness
// run made that the whole question: one request for 256.1 MiB out of a 512 MiB arena that had
// 243.6 MiB free. The engine's own Lua stack dump is useless at that moment, because
// get_lua_stack() builds xr_strings and allocating is precisely what has just failed.
//
// So the script engine registers an ALLOCATION-FREE stack describer here, and the allocator calls
// it through xr_alloc_lua_context(). This lives in xrCore, and it is a function pair rather than
// an exported pointer, for two reasons that are easy to get wrong:
//   * xr_alloc.c is compiled into a StaticLibrary that BOTH xrEngine and xrGame pull in, while the
//     script engine exists only in xrGame — so a pointer set in one module must be reachable from
//     the other, and xrCore is the only module both of them already link.
//   * exported DATA needs a dllimport declaration to resolve across a DLL boundary, whereas an
//     exported FUNCTION resolves through the import library's thunk either way. A setter costs
//     nothing and removes a whole class of link-time surprise.
typedef void (*xr_alloc_lua_ctx_fn)(char* out, size_t out_size);
extern "C" {
void XRCORE_API xr_alloc_set_lua_context_fn(xr_alloc_lua_ctx_fn fn);
void XRCORE_API __cdecl xr_alloc_lua_context(char* out, size_t out_size);
void XRCORE_API __cdecl xr_alloc_msg(const char* s);
}

typedef void (*LogCallback)(LPCSTR string);
LogCallback XRCORE_API SetLogCB(LogCallback cb);
void XRCORE_API CreateLog(BOOL no_log = FALSE);
void InitLog();
void CloseLog();
void XRCORE_API FlushLog();

extern XRCORE_API xr_vector<xr_string> LogFile;
extern XRCORE_API BOOL LogExecCB;

xr_string FormatString(LPCSTR fmt, ...);

#endif
