/* High frame rate: the game keeps its 30 ticks a second; between two ticks extra pictures are drawn, with the
   camera eased and the drawn objects slid between their last two tick positions.

   [display] frame_rate = 30 (the original: off) | 60 | 90 | 120   (also in the F7 menu)

   Game.exe:
   - The main loop 0x43DE10: one tick (the client update draws the world: 0x42D4A0 -> 0x46C9E0), then the GUI block
     (skipped while 0x9737B8 & 0x80000000): bubbles 0x42E520, GUI 0x489BF0 (if [0x74FA4C] == 0), overlays
     0x433090(0x687378) (if [0x6990B0] == 0), the cursor 0x4875E0 and the flip 0x49C960 / 0x4AC480 / 0x49C9D0 (both if
     !(flags & 0x40000) || (flags & 0x100) || [0x6990B0]); then, in a game, 0x416CD0 (call at 0x43E395) sleeps until
     the next tick: deadline = [0x62CCD8] + (frame 0x9667C4 - [0x62CCE0]) * ms per frame (float 0x59249C).
   - Drawables (client objects): list 0x6D18C0, next at +0x170, position int32 at +0x0C / +0x10; the player's is
     0x96A6A0. The viewport's world max (0x687390 / 0x687394) is the player position the line of sight is cast from.
   Extra pictures are drawn in that wait (0x43E395) while the next tick is far enough, then the game sleeps the rest
   as before. Positions: recorded once a tick; while the world is drawn every drawable is put between its previous
   and current position by the time within the tick (one tick of delay on screen; the game's logic, the mouse and
   the network only ever see the real positions, which are put back right after drawing). Jumps over 64 pixels are
   not slid. timeBeginPeriod(1) also makes the ticks themselves even (Windows' default timer wakes up in ~15 ms
   steps, so ticks took 30..48 ms instead of 33). */
#include "onc.h"
#include <stdlib.h>
#include <string.h>
#include <mmsystem.h>

static int g_rate = 30;
static LARGE_INTEGER g_freq;
static double now_ms(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / g_freq.QuadPart; }

/* ---------------- positions between ticks ---------------- */
typedef struct { uint8_t *d; int px, py, cx, cy; } slot_t;
static slot_t *g_tab, *g_old;
static int g_cap, g_oldcap, g_n;
static int g_tick = -1;          /* the game frame the positions were recorded for */
static double g_tick_t;          /* when */
static double g_alpha = 1;       /* where between the previous and the current position the picture is drawn */
static int g_slid;               /* positions are slid now (to put back) */
static int g_player_set; static int32_t g_wmax[2];

static unsigned hsh(uint8_t *d, int cap) { return (unsigned)(((uintptr_t)d >> 3) * 2654435761u) & (cap - 1); }
static slot_t *find(slot_t *t, int cap, uint8_t *d) {
    if (!t) return NULL;
    for (unsigned i = hsh(d, cap), k = 0; k < (unsigned)cap; i = (i + 1) & (cap - 1), k++) {
        if (t[i].d == d) return &t[i];
        if (!t[i].d) return NULL;
    }
    return NULL;
}

static void record(int frame) {
    int n = 0;
    for (uint8_t *d = *(uint8_t **)0x6D18C0; d && n < 100000; d = *(uint8_t **)(d + 0x170)) n++;
    int cap = 64;
    while (cap < n * 2 + 16) cap <<= 1;
    /* the old table becomes the previous tick's */
    slot_t *t = g_old; int tc = g_oldcap;
    g_old = g_tab; g_oldcap = g_cap;
    if (tc < cap) { free(t); t = calloc(cap, sizeof *t); tc = cap; if (!t) { g_tab = NULL; g_cap = 0; return; } }
    else memset(t, 0, tc * sizeof *t);
    int consecutive = frame == g_tick + 1;
    for (uint8_t *d = *(uint8_t **)0x6D18C0; d; d = *(uint8_t **)(d + 0x170)) {
        int x = *(int32_t *)(d + 0x0C), y = *(int32_t *)(d + 0x10);
        slot_t *o = consecutive ? find(g_old, g_oldcap, d) : NULL;
        unsigned i = hsh(d, tc);
        while (t[i].d) i = (i + 1) & (tc - 1);
        t[i].d = d; t[i].cx = x; t[i].cy = y;
        if (o && abs(o->cx - x) <= 64 && abs(o->cy - y) <= 64) { t[i].px = o->cx; t[i].py = o->cy; }
        else { t[i].px = x; t[i].py = y; }
    }
    g_tab = t; g_cap = tc; g_n = n;
    g_tick = frame;
    g_tick_t = now_ms();
}

static int lerp_i(int a, int b, double t) { return a + (int)((b - a) * t + (b >= a ? 0.5 : -0.5)); }

/* around the world drawing (0x46C9E0, spritefx.c) */
void onc_hfr_draw_begin(void) {
    g_slid = 0;
    if (g_rate <= 30 || !ONC_I32(NOX_CLIENT_CONNECTED)) return;
    int frame = ONC_I32(0x9667C4);
    if (frame != g_tick) record(frame);
    if (!g_tab) return;
    double period = 1000.0 / 30;
    double a = (now_ms() - g_tick_t) / period;
    g_alpha = a < 0 ? 0 : a > 1 ? 1 : a;
    for (int i = 0; i < g_cap; i++) {
        slot_t *s = &g_tab[i];
        if (!s->d || (s->px == s->cx && s->py == s->cy)) continue;
        *(int32_t *)(s->d + 0x0C) = lerp_i(s->px, s->cx, g_alpha);
        *(int32_t *)(s->d + 0x10) = lerp_i(s->py, s->cy, g_alpha);
    }
    g_slid = 1;
    /* the line of sight is cast from the viewport's world max: from where the player is drawn */
    uint8_t *pl = *(uint8_t **)0x96A6A0;
    slot_t *ps = pl ? find(g_tab, g_cap, pl) : NULL;
    g_player_set = 0;
    if (ps && ONC_I32(0x687390) == ps->cx && ONC_I32(0x687394) == ps->cy) {
        g_wmax[0] = ONC_I32(0x687390); g_wmax[1] = ONC_I32(0x687394);
        ONC_I32(0x687390) = *(int32_t *)(pl + 0x0C);
        ONC_I32(0x687394) = *(int32_t *)(pl + 0x10);
        g_player_set = 1;
    }
}
void onc_hfr_draw_end(void) {
    if (!g_slid) return;
    for (int i = 0; i < g_cap; i++) {
        slot_t *s = &g_tab[i];
        if (!s->d || (s->px == s->cx && s->py == s->cy)) continue;
        *(int32_t *)(s->d + 0x0C) = s->cx;
        *(int32_t *)(s->d + 0x10) = s->cy;
    }
    if (g_player_set) { ONC_I32(0x687390) = g_wmax[0]; ONC_I32(0x687394) = g_wmax[1]; g_player_set = 0; }
    g_slid = 0;
}

/* ---------------- extra pictures ---------------- */
void onc_uisplit_world_begin(void), onc_uisplit_world_end(void);
int onc_camera_extra_step(void);
static struct { int n; double world, gui, flip, total; } st;
static double g_last_log;
static int g_drawn_frame = -1; /* the game frame whose picture the game drew (the world was drawn) */

static double deadline_ms(void) {
    /* 0x416CD0: [0x62CCD8] + (frame - [0x62CCE0]) * ms per frame, on the 0x416AB0 clock */
    int frames = ONC_I32(0x9667C4) - ONC_I32(0x62CCE0);
    float mspf = *(float *)0x59249C;
    return (double)(uint32_t)ONC_I32(0x62CCD8) + frames * (double)mspf;
}
static double game_clock(void) { return (double)(uint32_t)((uint32_t (__cdecl *)(void))0x416AB0)(); }

int onc_uisplit_extra_world(void (*draw)(void));
/* the world pass draws more than the world: right after it (0x42D6F8) 0x47CB10 draws the map (when open) and the
   centred messages. With the split rendering they go into the GUI layer (uisplit.c), which the extra pictures keep;
   a full extra picture (no split) draws them again itself. */
static void world_overlays(void) { ((void (__cdecl *)(void))0x47CB10)(); }
static void draw_world_extra(void) {
    onc_camera_extra_step();
    extern void onc_world_draw_extra(void);
    onc_world_draw_extra();
}
static void extra_picture(void) {
    uint32_t f = (uint32_t)ONC_I32(0x9737B8);
    if (f & 0x80000000u) return;
    double t0 = now_ms();
    /* with the split rendering only the world is drawn again, under the last GUI layer (much cheaper) */
    if (onc_uisplit_extra_world(draw_world_extra)) {
        double t1 = now_ms();
        st.n++; st.world += t1 - t0; st.total += t1 - t0;
        return;
    }
    onc_uisplit_world_begin();
    onc_camera_extra_step();
    extern void onc_world_draw_extra(void);
    onc_world_draw_extra(); /* 0x46C9E0 through the smoothing wrapper */
    world_overlays();
    onc_uisplit_world_end();
    double t1 = now_ms();
    ((void (__cdecl *)(void))0x42E520)();                                     /* speech bubbles */
    if (!ONC_I32(0x74FA4C)) ((void (__cdecl *)(void))0x489BF0)();              /* GUI (and our overlays) */
    if (!ONC_I32(0x6990B0)) ((void (__cdecl *)(void *))0x433090)((void *)0x687378);
    int show = !(f & 0x40000) || (f & 0x100) || ONC_I32(0x6990B0);
    if (show) ((void (__cdecl *)(void))0x4875E0)();                           /* cursor */
    double t2 = now_ms();
    if (show) {
        ((void (__cdecl *)(void))0x49C960)();
        ((void (__cdecl *)(void))0x4AC480)();                                 /* flip */
        ((void (__cdecl *)(void))0x49C9D0)();
    }
    double t3 = now_ms();
    st.n++; st.world += t1 - t0; st.gui += t2 - t1; st.flip += t3 - t2; st.total += t3 - t0;
}

/* 0x43E395: call 0x416CD0 (sleep until the next tick), in a game */
void onc_profile_tick(void);
static void __cdecl hfr_wait(void) {
    onc_profile_tick();
    if (g_rate > 30 && ONC_I32(NOX_CLIENT_CONNECTED) && g_drawn_frame == ONC_I32(0x9667C4)) {
        double interval = 1000.0 / g_rate;
        double avg = st.n ? st.total / st.n : 6.0;
        double start = now_ms(), clock0 = game_clock(), dl = deadline_ms();
        for (int k = 1; k < 8; k++) {
            double target = g_tick_t + k * interval;             /* evenly after the tick's own picture */
            double left = dl - (clock0 + (now_ms() - start));    /* ms until the next tick */
            if (target - g_tick_t > 1000.0 / 30 - 1 || left < avg + 2) break;
            double wait = target - now_ms();
            if (wait > left - avg - 1) wait = left - avg - 1;
            if (wait > 1.5) Sleep((DWORD)(wait - 1));
            while (now_ms() < target && (dl - (clock0 + (now_ms() - start))) > avg + 1) YieldProcessor();
            extra_picture();
            avg = st.total / st.n;
        }
    }
    ((void (__cdecl *)(void))0x416CD0)();
    double t = now_ms();
    if (g_rate > 30 && t - g_last_log > 10000) {
        if (st.n) onc_log("frame rate %d: %.1f extra pictures/s, %.2f ms each (world + show %.2f, gui %.2f, flip %.2f)", g_rate,
                          st.n * 1000.0 / (t - g_last_log), st.total / st.n, st.world / st.n, st.gui / st.n, st.flip / st.n);
        memset(&st, 0, sizeof st);
        g_last_log = t;
    }
}

/* the world was drawn for this game frame (called from the world wrapper, uisplit / spritefx) */
void onc_hfr_world_drawn(void) { g_drawn_frame = ONC_I32(0x9667C4); }

int onc_hfr_get(void) { return g_rate; }
void onc_hfr_set(int r) { if (r == 30 || r == 60 || r == 90 || r == 120 || r == 144) g_rate = r; }

void onc_hfr_init(void) {
    QueryPerformanceFrequency(&g_freq);
    timeBeginPeriod(1); /* even ticks: Sleep wakes up within ~1 ms */
    g_rate = onc_cfg_int("display", "frame_rate", 30);
    if (g_rate != 60 && g_rate != 90 && g_rate != 120 && g_rate != 144) g_rate = 30;
    g_last_log = now_ms();
    onc_hook_call("frame rate: game wait", 0x43E395, hfr_wait, 0x416CD0);
    onc_log("frame rate: %d (timer resolution 1 ms)", g_rate);
}
