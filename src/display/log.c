#include "display.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static FILE *g_log;
static char g_ini[MAX_PATH];

static void exe_file(char *out, const char *name) {
    GetModuleFileNameA(NULL, out, MAX_PATH);
    char *s = strrchr(out, '\\');
    if (s) s[1] = 0; else out[0] = 0;
    strncat(out, name, MAX_PATH - strlen(out) - 1);
}

/* a new log at every start; one session writes at most 2 MB (then one last line) */
#define LOG_SESSION_MAX (2u << 20)
static unsigned g_log_bytes;
static int g_log_full;
void dsp_log(const char *fmt, ...) {
    if (g_log_full) return;
    if (!g_log) {
        char p[MAX_PATH];
        exe_file(p, "nox-modernized-display.log");
        g_log = fopen(p, "w");
        if (!g_log) return;
    }
    SYSTEMTIME t; GetLocalTime(&t);
    int n = fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); n += vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log);
    if (n > 0) g_log_bytes += (unsigned)n + 1;
    if (g_log_bytes > LOG_SESSION_MAX) { fputs("(log limit reached: nothing more is logged in this session)\n", g_log); g_log_full = 1; }
    fflush(g_log);
}

/* the settings file: as mss32.dll (loader/log.c): ONC_INI names another file next to Game.exe, if it is there */
static void ini_path(void) {
    if (g_ini[0]) return;
    char name[64];
    DWORD n = GetEnvironmentVariableA("ONC_INI", name, sizeof name);
    if (n > 0 && n < sizeof name && !strpbrk(name, "\\/:")) {
        exe_file(g_ini, name);
        if (GetFileAttributesA(g_ini) != INVALID_FILE_ATTRIBUTES) return;
    }
    exe_file(g_ini, "nox-modernized.ini");
}

int dsp_cfg_int(const char *key, int def) {
    ini_path();
    return (int)GetPrivateProfileIntA("display", key, def, g_ini);
}

void dsp_cfg_str(const char *key, const char *def, char *out, int n) {
    ini_path();
    GetPrivateProfileStringA("display", key, def, out, n, g_ini);
}
