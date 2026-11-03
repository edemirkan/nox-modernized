/* Sampling profiler (for development: [display] profile = 1): a thread looks at where the game's main thread is about
   1000 times a second (SuspendThread / GetThreadContext / ResumeThread) and counts the places (64-byte blocks of
   Game.exe; our mss32.dll; anything else by module). Every 10 s the log gets the busiest places with the world
   view size, so zoomed in and zoomed out can be compared. Time spent waiting (Sleep) shows as other modules. */
#include "onc.h"
#include <string.h>
#include <stdlib.h>

static HANDLE g_main;
static volatile LONG g_started;
#define NB 65536
static uint32_t g_key[NB], g_cnt[NB];
static uint32_t g_mods[32]; static HMODULE g_modh[32]; static int g_nmod;
static uint32_t g_total, g_game, g_ours, g_other;
static uintptr_t g_self_lo, g_self_hi; /* our mss32.dll (the game's Miles is also called mss32.dll) */
static int g_interval = 1;
int onc_uisplit_view(int *w, int *h);

static void count(uint32_t eip) {
    g_total++;
    if (eip >= 0x401000 && eip < 0x600000) {
        g_game++;
        uint32_t k = eip >> 6, i = (k * 2654435761u) >> 16;
        for (int n = 0; n < NB; n++, i = (i + 1) & (NB - 1)) {
            if (g_key[i] == k) { g_cnt[i]++; return; }
            if (!g_key[i]) { g_key[i] = k; g_cnt[i] = 1; return; }
        }
        return;
    }
    if (eip >= g_self_lo && eip < g_self_hi) {
        g_ours++;
        uint32_t k = 0x80000000u | (uint32_t)((eip - g_self_lo) >> 5), i = (k * 2654435761u) >> 16;
        for (int n = 0; n < NB; n++, i = (i + 1) & (NB - 1)) {
            if (g_key[i] == k) { g_cnt[i]++; return; }
            if (!g_key[i]) { g_key[i] = k; g_cnt[i] = 1; return; }
        }
        return;
    }
    g_other++;
    HMODULE m = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(uintptr_t)eip, &m);
    for (int i = 0; i < g_nmod; i++) if (g_modh[i] == m) { g_mods[i]++; return; }
    if (g_nmod < 32) { g_modh[g_nmod] = m; g_mods[g_nmod++] = 1; }
}

static void report(void) {
    if (!g_total) return;
    int vw = 0, vh = 0;
    onc_uisplit_view(&vw, &vh);
    onc_log("profile: %u samples, world view %dx%d: Game.exe %.1f%%, Nox Modernized %.1f%%, other %.1f%%", g_total, vw, vh,
            g_game * 100.0 / g_total, g_ours * 100.0 / g_total, g_other * 100.0 / g_total);
    /* the busiest blocks: Game.exe (address) and ours (onc+offset) */
    for (int r = 0; r < 60; r++) {
        int best = -1;
        for (int i = 0; i < NB; i++) if (g_key[i] && g_cnt[i] && (best < 0 || g_cnt[i] > g_cnt[best])) best = i;
        if (best < 0 || g_cnt[best] * 1000 < g_total) break;
        if (g_key[best] & 0x80000000u) onc_log("profile:   onc+%05X %5.1f%%", (g_key[best] & 0x7FFFFFFFu) << 5, g_cnt[best] * 100.0 / g_total);
        else onc_log("profile:   %08X %5.1f%%", g_key[best] << 6, g_cnt[best] * 100.0 / g_total);
        g_cnt[best] = 0;
    }
    for (int i = 0; i < g_nmod; i++) {
        if (g_mods[i] * 200 < g_total) continue;
        char name[MAX_PATH] = "?";
        if (g_modh[i]) GetModuleFileNameA(g_modh[i], name, sizeof name);
        char *b = strrchr(name, '\\');
        onc_log("profile:   module %s %.1f%%", b ? b + 1 : name, g_mods[i] * 100.0 / g_total);
    }
    memset(g_key, 0, sizeof g_key); memset(g_cnt, 0, sizeof g_cnt);
    memset(g_mods, 0, sizeof g_mods); memset(g_modh, 0, sizeof g_modh); g_nmod = 0;
    g_total = g_game = g_ours = g_other = 0;
}

static DWORD WINAPI sampler(LPVOID p) {
    (void)p;
    DWORD last = GetTickCount();
    for (;;) {
        Sleep(g_interval);
        if (SuspendThread(g_main) == (DWORD)-1) continue;
        CONTEXT c;
        memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_CONTROL;
        BOOL ok = GetThreadContext(g_main, &c);
        ResumeThread(g_main);
        if (ok) count((uint32_t)c.Eip);
        if (GetTickCount() - last >= 10000) { report(); last = GetTickCount(); }
    }
    return 0;
}

static int g_on;
/* from the main loop (hfr.c): the thread to look at */
void onc_profile_tick(void) {
    if (!g_on || g_started) return;
    g_started = 1;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    HANDLE t = CreateThread(NULL, 0, sampler, NULL, 0, NULL);
    if (t) { SetThreadPriority(t, THREAD_PRIORITY_HIGHEST); CloseHandle(t); }
    onc_log("profile: sampling the main thread every %d ms (log every 10 s; ours at %08X)", g_interval, (unsigned)g_self_lo);
}

void onc_profile_init(void) {
    g_on = onc_cfg_int("display", "profile", 0);
    g_interval = g_on < 1 ? 1 : g_on > 50 ? 50 : g_on; /* profile = N: a look every N ms */
    HMODULE self = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)onc_profile_init, &self);
    if (self) {
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((uint8_t *)self + ((IMAGE_DOS_HEADER *)self)->e_lfanew);
        g_self_lo = (uintptr_t)self; g_self_hi = g_self_lo + nt->OptionalHeader.SizeOfImage;
    }
}
