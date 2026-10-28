#include "log.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <share.h>

static FILE* g_file = nullptr;
static CRITICAL_SECTION g_cs;
static bool g_csInit = false;
static char g_path[MAX_PATH] = {};

void LogInit(const char* path) {
    if (g_file) return;
    if (!g_csInit) {
        InitializeCriticalSection(&g_cs);
        g_csInit = true;
    }
    strncpy_s(g_path, path, _TRUNCATE);
    g_file = _fsopen(path, "a", _SH_DENYWR);  // readable while the game runs
}

void LogClose() {
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
}

void LogPrintf(const char* fmt, ...) {
    if (!g_csInit) {
        InitializeCriticalSection(&g_cs);
        g_csInit = true;
    }
    char msg[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    SYSTEMTIME st;
    GetLocalTime(&st);

    EnterCriticalSection(&g_cs);
    if (!g_file && g_path[0]) g_file = _fsopen(g_path, "a", _SH_DENYWR);
    if (g_file) {
        fprintf(g_file, "[%02d:%02d:%02d.%03d][%5lu] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                (unsigned long)GetCurrentThreadId(), msg);
        fflush(g_file);
    }
    LeaveCriticalSection(&g_cs);

    char dbg[4300];
    sprintf_s(dbg, "[Rez3D] %s\n", msg);
    OutputDebugStringA(dbg);
}
