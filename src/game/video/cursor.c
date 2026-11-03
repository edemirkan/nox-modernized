/* The mouse cursor at the full frame rate: with the high frame rate on and in a game, the cursor is not drawn into
   the GUI (which is new only once a tick) but captured into a layer of its own that ddraw.dll draws at every
   picture where the mouse is now: the position the game has plus the motion still waiting in the mouse's buffer
   (looked at, not taken: dinput_proxy.c onc_mouse_pending). The game moves its cursor by the raw mouse counts
   (0x432370, clamped to 0x697B60..0x598EFC and 0x697B64..0x598F00), so the layer lands where the game's cursor
   will be at the next tick. Menus outside a game keep the cursor in the GUI (30 a second).

   [display] cursor_layer = 1 (default) | 0

   Capture: the cursor drawing (0x4875E0: the pointer, a dragged item, the tooltip next to it) is run twice into
   buffers the size of the GUI, one black and one white (uisplit.c onc_uisplit_offscreen), clipped to a window
   around the mouse; pixels equal in both are solid, blended pixels give their alpha by the difference. Its one
   counter (0x6F7358, the animation, +1 on odd frames) is put back for the second run. */
#include "onc.h"
#include <string.h>
#include <stdlib.h>

int onc_uisplit_active(void);
int onc_uisplit_offscreen(void *rows, int pitch, int l, int t, int r, int b, void (*draw)(void));
int onc_hfr_get(void);
int onc_mouse_pending(int *dx, int *dy);

static int g_on = 1;
static uint16_t *g_a, *g_b;
static uint16_t **g_ra, **g_rb;
static int g_w, g_h;                 /* buffer size (the GUI's) */
static int g_valid;                  /* a capture to show */
static int g_bx, g_by, g_bw, g_bh;   /* its box in the buffers */
static int g_mx, g_my;               /* the mouse when it was captured */
static int g_serial;
static int g_wl, g_wt, g_wr, g_wb;   /* the box drawn into last time (cleared before the next) */
static int g_logged;

static int display_d3d(void) {
    static int (__cdecl *get)(const char *);
    static int tried;
    if (!tried) {
        tried = 1;
        HMODULE dd = GetModuleHandleA("ddraw.dll");
        if (dd && GetProcAddress(dd, "OncPresentWorld")) get = (int (__cdecl *)(const char *))GetProcAddress(dd, "OncDisplayGet");
    }
    return get && get("d3d") == 1;
}

static int alloc(int w, int h) {
    if (g_a && g_w == w && g_h == h) return 1;
    if (g_a) { VirtualFree(g_a, 0, MEM_RELEASE); VirtualFree(g_b, 0, MEM_RELEASE); VirtualFree(g_ra, 0, MEM_RELEASE); VirtualFree(g_rb, 0, MEM_RELEASE); }
    size_t px = (size_t)w * (h + 8);
    g_a = VirtualAlloc(NULL, px * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_b = VirtualAlloc(NULL, px * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_ra = VirtualAlloc(NULL, (size_t)(h + 160) * sizeof(void *), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_rb = VirtualAlloc(NULL, (size_t)(h + 160) * sizeof(void *), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_a || !g_b || !g_ra || !g_rb) { g_a = NULL; g_w = g_h = 0; return 0; }
    for (int y = 0; y < h; y++) { g_ra[y] = g_a + (size_t)y * w; g_rb[y] = g_b + (size_t)y * w; }
    for (int y = h; y < h + 160; y++) { g_ra[y] = g_a + (size_t)(h + 4) * w; g_rb[y] = g_b + (size_t)(h + 4) * w; }
    /* a: black (VirtualAlloc gives zeros); b: white */
    for (size_t i = 0; i < px; i++) g_b[i] = 0xFFFF;
    g_w = w; g_h = h;
    g_wl = g_wt = 0; g_wr = g_wb = 0;
    return 1;
}

static void clear_window(void) {
    for (int y = g_wt; y < g_wb; y++) {
        memset(g_a + (size_t)y * g_w + g_wl, 0, (size_t)(g_wr - g_wl) * 2);
        uint16_t *b = g_b + (size_t)y * g_w;
        for (int x = g_wl; x < g_wr; x++) b[x] = 0xFFFF;
    }
}

/* from the cursor drawing's detour (pad_hints.c): 1 = handled here (captured, or nothing to show) */
int onc_cursor_capture(void (*draw)(void), int hidden) {
    if (!g_on || hidden || onc_hfr_get() <= 30 || !onc_uisplit_active() || !display_d3d()) { g_valid = 0; return hidden; }
    int w = ONC_I32(NOX_SCREEN_W), h = ONC_I32(NOX_SCREEN_H);
    if (w < 320 || h < 200 || !alloc(w, h)) { g_valid = 0; return 0; }
    clear_window();
    int32_t *m = ((int32_t * (__cdecl *)(void))0x432360)();
    int mx = m[0], my = m[1];
    int l = mx - 400, t = my - 240, r = mx + 400, b = my + 240;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > w) r = w;
    if (b > h) b = h;
    if (l >= r || t >= b) { g_valid = 0; return 1; }
    int32_t v0 = ONC_I32(0x6F7358);
    if (!onc_uisplit_offscreen(g_ra, w * 2, l, t, r, b, draw)) { g_valid = 0; return 0; }
    int32_t v1 = ONC_I32(0x6F7358);
    ONC_I32(0x6F7358) = v0;
    onc_uisplit_offscreen(g_rb, w * 2, l, t, r, b, draw);
    ONC_I32(0x6F7358) = v1;
    /* the box of what was drawn */
    int bl = r, bt = b, br = l, bb = t;
    for (int y = t; y < b; y++) {
        const uint16_t *pa = g_a + (size_t)y * w, *pb = g_b + (size_t)y * w;
        int first = -1, last = -1;
        for (int x = l; x < r; x++)
            if (pa[x] != 0 || pb[x] != 0xFFFF) { if (first < 0) first = x; last = x; }
        if (first < 0) continue;
        if (first < bl) bl = first;
        if (last + 1 > br) br = last + 1;
        if (y < bt) bt = y;
        bb = y + 1;
    }
    /* drawing is clipped to the window, so everything drawn is inside this box: next time only it is cleared */
    if (br <= bl || bb <= bt) { g_wl = g_wt = g_wr = g_wb = 0; g_valid = 0; return 1; }
    g_wl = bl; g_wt = bt; g_wr = br; g_wb = bb;
    g_bx = bl; g_by = bt; g_bw = br - bl; g_bh = bb - bt;
    g_mx = mx; g_my = my;
    g_serial++;
    g_valid = 1;
    if (!g_logged) { g_logged = 1; onc_log("cursor layer: in use (%dx%d at the mouse %d,%d)", g_bw, g_bh, mx, my); }
    return 1;
}

/* for the picture shown now (uisplit.c, at every picture): where the cursor goes, from the mouse now */
void onc_cursor_layer(const uint16_t **a, const uint16_t **b, int *pitch, int *w, int *h, int *x, int *y, int *serial) {
    *w = *h = 0; *a = *b = NULL;
    if (!g_valid || !g_a) return;
    int32_t *m = ((int32_t * (__cdecl *)(void))0x432360)();
    int px = m[0], py = m[1], dx, dy;
    if (onc_mouse_pending(&dx, &dy)) {
        px += dx; py += dy;
        int x0 = ONC_I32(0x697B60), x1 = ONC_I32(0x598EFC), y0 = ONC_I32(0x697B64), y1 = ONC_I32(0x598F00);
        if (x1 > x0) { if (px < x0) px = x0; if (px > x1) px = x1; }
        if (y1 > y0) { if (py < y0) py = y0; if (py > y1) py = y1; }
    }
    *a = g_a + (size_t)g_by * g_w + g_bx;
    *b = g_b + (size_t)g_by * g_w + g_bx;
    *pitch = g_w; *w = g_bw; *h = g_bh;
    *x = g_bx + (px - g_mx); *y = g_by + (py - g_my);
    *serial = g_serial;
}

void onc_cursor_init(void) {
    g_on = onc_cfg_int("display", "cursor_layer", 1);
    onc_log("cursor layer: %s (with the high frame rate, in a game)", g_on ? "on" : "off");
}
