/* A few worker threads for work split in bands of rows (the world copy into the GUI layer, uisplit.c).
   [display] threads = 0 (auto: the processors less one, at most 7) | N (1..7 helpers) | -1 (none: all on the main thread) */
#include "onc.h"

#define MAXT 7
static int g_n;
static HANDLE g_go[MAXT], g_done[MAXT];
static void (*g_fn)(int, int, void *);
static void *g_arg;
static int g_lo[MAXT], g_hi[MAXT];

static DWORD WINAPI worker(LPVOID p) {
    int i = (int)(intptr_t)p;
    for (;;) {
        WaitForSingleObject(g_go[i], INFINITE);
        g_fn(g_lo[i], g_hi[i], g_arg);
        SetEvent(g_done[i]);
    }
    return 0;
}

/* fn(lo, hi, arg) over [0, n), split between the main thread and the helpers; returns when all are done */
void onc_parallel(int n, void (*fn)(int, int, void *), void *arg) {
    if (g_n <= 0 || n < 32) { fn(0, n, arg); return; }
    int parts = g_n + 1, band = (n + parts - 1) / parts;
    g_fn = fn; g_arg = arg;
    int used = 0;
    for (int i = 0; i < g_n; i++) {
        int a = (i + 1) * band, b = a + band;
        if (a >= n) break;
        if (b > n) b = n;
        g_lo[i] = a; g_hi[i] = b;
        SetEvent(g_go[i]);
        used++;
    }
    fn(0, band < n ? band : n, arg);
    if (used) WaitForMultipleObjects(used, g_done, TRUE, INFINITE);
}

void onc_workers_init(void) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    int n = onc_cfg_int("display", "threads", 0);
    if (n < 0) n = 0;                                    /* off */
    else if (n == 0) n = (int)si.dwNumberOfProcessors - 1; /* automatic */
    if (n > MAXT) n = MAXT;
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) {
        g_go[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
        g_done[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
        HANDLE t = CreateThread(NULL, 0, worker, (LPVOID)(intptr_t)i, 0, NULL);
        if (!g_go[i] || !g_done[i] || !t) { n = i; break; }
        CloseHandle(t);
    }
    g_n = n;
    onc_log("workers: %d helper threads (%lu processors)", g_n, si.dwNumberOfProcessors);
}
