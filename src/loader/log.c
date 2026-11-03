#include "onc.h"
#include <stdio.h>
#include <stdarg.h>

static FILE *g_log;
static char g_ini[MAX_PATH];

static void exe_dir_file(char *out, const char *file) {
    GetModuleFileNameA(NULL, out, MAX_PATH);
    char *s = strrchr(out, '\\');
    if (s) s[1] = 0; else out[0] = 0;
    strncat(out, file, MAX_PATH - strlen(out) - 1);
}

/* the settings file: nox-modernized.ini next to Game.exe, or the one the environment variable ONC_INI names
   (Launch-Winlator.bat: nox-modernized-winlator.ini, Winlator's own defaults) when that file is there */
static char g_ini_name[64];
static void ini_path(void) {
    if (g_ini[0]) return;
    char name[64];
    DWORD n = GetEnvironmentVariableA("ONC_INI", name, sizeof name);
    if (n > 0 && n < sizeof name && !strpbrk(name, "\\/:")) {
        exe_dir_file(g_ini, name);
        if (GetFileAttributesA(g_ini) != INVALID_FILE_ATTRIBUTES) { strcpy(g_ini_name, name); return; }
    }
    strcpy(g_ini_name, "nox-modernized.ini");
    exe_dir_file(g_ini, g_ini_name);
}
const char *onc_cfg_name(void) { ini_path(); return g_ini_name; }

/* the log: kept small on players' machines. Earlier sessions are kept, but a file past 1 MB at the start starts over,
   and one session writes at most 2 MB (then one last line), so nothing can grow without end */
#define LOG_START_OVER (1u << 20)
#define LOG_SESSION_MAX (2u << 20)
static unsigned g_log_bytes;
static int g_log_full;
static FILE *log_open(const char *name) {
    char p[MAX_PATH];
    exe_dir_file(p, name);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    int big = GetFileAttributesExA(p, GetFileExInfoStandard, &fa) && (fa.nFileSizeHigh || fa.nFileSizeLow > LOG_START_OVER);
    return fopen(p, big ? "w" : "a");
}
void onc_log(const char *fmt, ...) {
    if (g_log_full) return;
    if (!g_log) {
        g_log = log_open("nox-modernized.log");
        /* a log copied from another device may not be writable (Android): use a second one, same rules */
        if (!g_log) g_log = log_open("nox-modernized-2.log");
        if (!g_log) return;
        SYSTEMTIME d; GetLocalTime(&d);
        fprintf(g_log, "\n======== session %04d-%02d-%02d %02d:%02d:%02d (pid %lu) ========\n",
                d.wYear, d.wMonth, d.wDay, d.wHour, d.wMinute, d.wSecond, GetCurrentProcessId());
    }
    SYSTEMTIME t; GetLocalTime(&t);
    int n = fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); n += vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log);
    if (n > 0) g_log_bytes += (unsigned)n + 1;
    if (g_log_bytes > LOG_SESSION_MAX) { fputs("(log limit reached: nothing more is logged in this session)\n", g_log); g_log_full = 1; }
    fflush(g_log);
}

int onc_cfg_int(const char *section, const char *key, int def) {
    ini_path();
    return (int)GetPrivateProfileIntA(section, key, def, g_ini);
}

int onc_cfg_float100(const char *section, const char *key, int def) {
    ini_path();
    char b[32];
    GetPrivateProfileStringA(section, key, "", b, sizeof b, g_ini);
    if (!b[0]) return def;
    return (int)(atof(b) * 100 + 0.5);
}

void onc_cfg_str(const char *section, const char *key, const char *def, char *out, int n) {
    ini_path();
    GetPrivateProfileStringA(section, key, def, out, n, g_ini);
}

void onc_cfg_write(const char *section, const char *key, const char *value) {
    ini_path();
    WritePrivateProfileStringA(section, key, value, g_ini);
}
