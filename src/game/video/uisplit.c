/* UI scale with the world at full resolution (split rendering).
   [display] ui_scale = 1.5 (for example) runs the game, and so its GUI, at screen / 1.5: the GUI is drawn with
   bigger pixels. The world is drawn into a buffer of its own at the full screen size: around the world drawing
   (0x42D4A0, called at 0x42D43A) the screen size, the video mode size, the pitch, the line table of the frame
   buffer (0x952C54) and the clipping of the drawing state (*0x952FA0) are switched to that buffer. Then the GUI
   buffer gets a reduced copy of the world (translucent GUI parts blend with it) and is remembered; the GUI is drawn
   as usual. When the frame is shown (ddraw.dll's frame hook), the pixels the GUI changed are the GUI: the shown
   frame is the full size world with the GUI pixels scaled up on top.
   Mouse positions used for the world (0x42D321 the input packet / facing, 0x42EB9D clicks, 0x47EFA2 hovering) are
   scaled from the GUI to the world buffer. */
#include "onc.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static int g_scale100 = 100;      /* ui_scale x 100 */
static int g_W, g_H;              /* full screen (world buffer) */
static int g_G, g_GH;             /* GUI (the game's mode) */
static int g_ready;               /* set up (ddraw.dll has the frame hook) */
static int g_active;              /* this frame: the world was drawn into its buffer */
static uint16_t *g_world;         /* W x H */
static uint16_t **g_world_rows;   /* H + slack */
static uint16_t *g_snap;          /* GUI buffer right after the reduced world was put in */
/* a frame in layers for ddraw.dll (the same as its dsp_layers, display/display.h) */
typedef struct {
    const uint16_t *world; int wpitch, wx, wy, ww, wh;  /* the world: a rectangle of a 565 buffer */
    int fw, fh;                                          /* shown over the whole frame */
    const uint16_t *gui; int gpitch, gw, gh;            /* the GUI on top */
    const uint8_t *mask;                                 /* NULL: the GUI = the pixels that differ from snap */
    const uint16_t *snap;
    const uint16_t *cur_a, *cur_b; int cur_pitch, cur_w, cur_h, cur_x, cur_y, cur_serial; /* the cursor (cursor.c) */
} dsp_layers;
static void fill_world(dsp_layers *L);
void onc_cursor_layer(const uint16_t **a, const uint16_t **b, int *pitch, int *w, int *h, int *x, int *y, int *serial);

int onc_uisplit_active(void) { return g_ready && g_active && ONC_I32(NOX_CLIENT_CONNECTED); }
int onc_uisplit_size(int *w, int *h) {
    if (!g_ready) return 0;
    if (w) *w = g_W;
    if (h) *h = g_H;
    return 1;
}

static double g_zoom = 1, g_zoom_target = 1, g_zoom_max = 2, g_zoom_step = 0.1, g_zoom_ease = 100;
static LARGE_INTEGER g_zfreq, g_zlast;
static int g_zactive, g_zoff_x, g_zoff_y, g_zs_w, g_zs_h; /* this frame: the world rectangle in the buffer */
static void zoom_set(double v);
static void view_to_full(int *x, int *y);
static void full_to_view(int *x, int *y);
static int g_in_pass;
/* world view <-> GUI positions */
void onc_ui_w2g(int *x, int *y) {
    if (!onc_uisplit_active()) return;
    view_to_full(x, y);
    *x = (int)((long long)*x * g_G / g_W);
    *y = (int)((long long)*y * g_GH / g_H);
}
void onc_ui_g2w(int *x, int *y) {
    if (!onc_uisplit_active()) return;
    *x = (int)((long long)*x * g_W / g_G);
    *y = (int)((long long)*y * g_H / g_GH);
    full_to_view(x, y);
}

/* the largest UI scale (x 100) the screen allows: the game's screen (screen / scale, its width rounded down to 32)
   must stay at least 640x480, so scale <= w / 640 and <= h / 480 (1920x1080: 2.25, 2560x1440: 3, 3840x2160: 4.5).
   The settings tool works it out the same way (launcher.c, uiscale_max). */
static int g_full_w, g_full_h;
int onc_uiscale_max(void) {
    if (!g_full_w) return 100;
    int a = g_full_w * 100 / 640, b = g_full_h * 100 / 480, m = a < b ? a : b;
    return m < 100 ? 100 : m;
}

int onc_uisplit_setup(int w, int h, int *gw, int *gh) {
    g_scale100 = onc_cfg_float100("display", "ui_scale", 100);
    if (g_scale100 < 100) g_scale100 = 100;
    g_zoom_max = onc_cfg_float100("display", "zoom_max", 200) / 100.0;
    g_zoom_step = onc_cfg_float100("display", "zoom_step", 10) / 100.0;
    g_zoom_ease = onc_cfg_int("display", "zoom_ease_ms", 100);
    if (g_zoom_max < 1) g_zoom_max = 1;
    if (g_zoom_max > 4) g_zoom_max = 4;
    if (g_zoom_step <= 0) g_zoom_step = 0.1;
    zoom_set(onc_cfg_float100("display", "zoom", 100) / 100.0);
    g_zoom = g_zoom_target;
    QueryPerformanceFrequency(&g_zfreq);
    g_full_w = w; g_full_h = h;
    if (g_scale100 > onc_uiscale_max()) {
        onc_log("ui_scale %d.%02d: more than %dx%d allows (the game needs 640x480): %d.%02d used", g_scale100 / 100, g_scale100 % 100,
                w, h, onc_uiscale_max() / 100, onc_uiscale_max() % 100);
        g_scale100 = onc_uiscale_max();
    }
    if (g_scale100 == 100 && g_zoom_max <= 1) return 0; /* neither: the world needs no buffer of its own */
    HMODULE dd = GetModuleHandleA("ddraw.dll");
    void (__cdecl *set)(void *) = dd ? (void (__cdecl *)(void *))GetProcAddress(dd, "OncSetFrameHook") : NULL;
    if (!set) { onc_log("ui_scale: needs Nox Modernized's ddraw.dll (renderer = d3d11): not used"); return 0; }
    g_W = w; g_H = h;
    g_G = (w * 100 / g_scale100) & ~31; /* the game rounds its screen width down to 32 pixels */
    g_GH = h * 100 / g_scale100;
    if (g_G < 640 || g_GH < 480) { onc_log("ui_scale: %dx%d / %d.%02d is under 640x480: not used", w, h, g_scale100 / 100, g_scale100 % 100); return 0; }
    g_world = VirtualAlloc(NULL, (size_t)w * (h + 8) * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_world_rows = VirtualAlloc(NULL, (size_t)(h + 160) * sizeof(void *), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_snap = VirtualAlloc(NULL, (size_t)w * h * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); /* room for any GUI size */
    if (!g_world || !g_world_rows || !g_snap) return 0;
    for (int y = 0; y < h; y++) g_world_rows[y] = g_world + (size_t)y * w;
    for (int y = h; y < h + 160; y++) g_world_rows[y] = g_world + (size_t)(h + 4) * w; /* stray rows: a dump row */
    extern int __cdecl onc_uisplit_frame();
    set((void *)onc_uisplit_frame);
    g_ready = 1;
    *gw = g_G; *gh = g_GH;
    onc_log("ui_scale %d.%02d: GUI %dx%d, world %dx%d", g_scale100 / 100, g_scale100 % 100, g_G, g_GH, w, h);
    return 1;
}

/* ---------------- the world pass ---------------- */
#define RD (*(uint8_t **)0x952FA0)
static int32_t s_scr_w, s_scr_h, s_mode_w, s_mode_h, s_pitch;
static void *s_rows;
static uint8_t s_rd[0x34];

static void set_target(int w, int h, void *rows, int pitch) {
    ONC_I32(NOX_SCREEN_W) = w; ONC_I32(NOX_SCREEN_H) = h;
    ONC_I32(0x95384C) = w; ONC_I32(0x953850) = h;
    ONC_I32(0x953864) = pitch;
    *(void **)0x952C54 = rows;
    int32_t *rd = (int32_t *)RD;
    rd[0] = 1;                                     /* clipping on (sprites half off the buffer: 0x4C8632) */
    rd[1] = 0; rd[2] = 0; rd[3] = w; rd[4] = h;     /* +4 clip rectangle */
    rd[5] = 0; rd[6] = 0; rd[7] = w - 1; rd[8] = h - 1; /* +0x14 the same, inclusive */
    rd[9] = 0; rd[10] = 0; rd[11] = w; rd[12] = h;  /* +0x24 the screen */
}

/* the clip rectangle = the whole screen (within +0x24), as OpenNox SetRectFullScreen (sub_437290) */
void onc_clip_full(void) {
    int32_t *rd = (int32_t *)RD;
    if (!rd) return;
    int sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    int l = rd[9] > 0 ? rd[9] : 0, t = rd[10] > 0 ? rd[10] : 0;
    int r = rd[11] < sw ? rd[11] : sw, b = rd[12] < sh ? rd[12] : sh;
    if (l < r && t < b) {
        rd[1] = l; rd[2] = t; rd[3] = r; rd[4] = b;
        rd[5] = l; rd[6] = t; rd[7] = r - 1; rd[8] = b - 1;
    }
}

/* each top-level GUI window (0x489C80, called from 0x489BF0's three layers) is drawn with the whole screen as the
   clip rectangle: the game never sets it per window and something drawn before can leave a small one behind,
   which cut away the in-game menu's and the save list's darkened backgrounds */
static void __cdecl gui_window_draw(void *win) {
    onc_clip_full();
    ((void (__cdecl *)(void *))0x489C80)(win);
}

static void begin_world(void) {
    g_in_pass = 0;
    int gw = ONC_I32(NOX_SCREEN_W), gh = ONC_I32(NOX_SCREEN_H);
    if (!g_ready || !ONC_I32(NOX_CLIENT_CONNECTED) || !RD || gw < 320 || gh < 200 || gw > g_W || gh > g_H) {
        g_active = 0;
        return;
    }
    if (gw != g_G || gh != g_GH) { onc_log("ui split: the GUI is %dx%d (expected %dx%d)", gw, gh, g_G, g_GH); g_G = gw; g_GH = gh; }
    s_scr_w = ONC_I32(NOX_SCREEN_W); s_scr_h = ONC_I32(NOX_SCREEN_H);
    s_mode_w = ONC_I32(0x95384C); s_mode_h = ONC_I32(0x953850); s_pitch = ONC_I32(0x953864);
    s_rows = *(void **)0x952C54;
    memcpy(s_rd, RD, sizeof s_rd);
    set_target(g_W, g_H, g_world_rows, g_W * 2);
    g_in_pass = 1;
    g_active = 1;
}

/* the world as shown, reduced to the GUI size, into the GUI buffer and the snapshot: split in bands of rows over
   the worker threads (workers.c), column positions from a table (no division per pixel); each GUI pixel averages
   2x2 world pixels (zoomed out) or takes one (zoomed in: the world is enlarged anyway) */
void onc_parallel(int n, void (*fn)(int, int, void *), void *arg);
static int *g_sx; static int g_sx_cap, g_sx_vx = -1, g_sx_vw = -1, g_sx_g = -1, g_sx_near = -1;
static struct { uint16_t **rows; int vy, vh, nearest; } g_rd;
static void reduce_band(int y0, int y1, void *arg) {
    (void)arg;
    const int *sx = g_sx;
    int G = g_G, GH = g_GH, vy = g_rd.vy, vh = g_rd.vh;
    for (int y = y0; y < y1; y++) {
        uint16_t *d = g_rd.rows[y], *sn = g_snap + (size_t)y * G;
        if (g_rd.nearest) {
            const uint16_t *r0 = g_world_rows[vy + y * vh / GH];
            for (int x = 0; x < G; x++) d[x] = sn[x] = r0[sx[x]];
            continue;
        }
        int sy = vy + y * vh / GH; if (sy > vy + vh - 2) sy = vy + vh - 2;
        const uint16_t *r0 = g_world_rows[sy], *r1 = g_world_rows[sy + 1];
        for (int x = 0; x < G; x++) {
            int i = sx[x];
            /* 565 spread out with room between the fields, four added, rounded, halved twice, packed again */
            uint32_t a = r0[i], b = r0[i + 1], c = r1[i], e = r1[i + 1];
            a = (a | a << 16) & 0x07E0F81Fu; b = (b | b << 16) & 0x07E0F81Fu;
            c = (c | c << 16) & 0x07E0F81Fu; e = (e | e << 16) & 0x07E0F81Fu;
            uint32_t t = ((a + b + c + e + 0x00401002u) >> 2) & 0x07E0F81Fu;
            d[x] = sn[x] = (uint16_t)(t | t >> 16);
        }
    }
}
static void reduce_world(uint16_t **rows, int vx, int vy, int vw, int vh) {
    int nearest = vw <= g_G && vh <= g_GH;
    if (g_sx_cap < g_G) { free(g_sx); g_sx = malloc(sizeof(int) * g_G); g_sx_cap = g_sx ? g_G : 0; g_sx_vw = -1; }
    if (!g_sx) return;
    if (vx != g_sx_vx || vw != g_sx_vw || g_G != g_sx_g || nearest != g_sx_near) {
        for (int x = 0; x < g_G; x++) {
            int i = vx + x * vw / g_G;
            if (!nearest && i > vx + vw - 2) i = vx + vw - 2;
            g_sx[x] = i;
        }
        g_sx_vx = vx; g_sx_vw = vw; g_sx_g = g_G; g_sx_near = nearest;
    }
    g_rd.rows = rows; g_rd.vy = vy; g_rd.vh = vh; g_rd.nearest = nearest;
    onc_parallel(g_GH, reduce_band, NULL);
}

static int g_world_only; /* the high frame rate's extra pictures: no copy of the world into the GUI buffer */
static void end_world(void) {
    if (!g_in_pass) return;
    g_in_pass = 0;
    ONC_I32(NOX_SCREEN_W) = s_scr_w; ONC_I32(NOX_SCREEN_H) = s_scr_h;
    ONC_I32(0x95384C) = s_mode_w; ONC_I32(0x953850) = s_mode_h; ONC_I32(0x953864) = s_pitch;
    *(void **)0x952C54 = s_rows;
    memcpy(RD, s_rd, sizeof s_rd);
    /* the world drawing ends with the clip rectangle set to the whole screen (OpenNox SetRectFullScreen, after
       0x46C9E0): the GUI relies on it (the menus' darkened backgrounds, the save list). The state saved above is
       whatever the GUI left last frame (often a small clip), so set the whole screen again, within +0x24. */
    onc_clip_full();
    if (g_world_only) return;
    /* the GUI buffer gets the world as it is shown (the zoomed rectangle when zoomed, else the whole buffer),
       reduced to the GUI size (each pixel averages 2x2 world pixels), and is remembered: the GUI pixels that still
       equal it show the world. (From the whole buffer while zoomed, the black margins outside the zoomed rectangle
       made black / darkened menu backgrounds over them equal it: see-through.) */
    uint16_t **rows = (uint16_t **)s_rows;
    int vx = 0, vy = 0, vw = g_W, vh = g_H;
    if (g_zactive) { vx = g_zoff_x; vy = g_zoff_y; vw = g_zs_w; vh = g_zs_h; }
    reduce_world(rows, vx, vy, vw, vh);
}

/* something drawn into another buffer the size of the GUI (rows: its line table, gh + 160 entries), clipped to
   (l, t)-(r, b): the mouse cursor's layer (cursor.c) */
int onc_uisplit_offscreen(void *rows, int pitch, int l, int t, int r, int b, void (*draw)(void)) {
    if (!g_ready || g_in_pass || !RD) return 0;
    int32_t sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H), mw = ONC_I32(0x95384C), mh = ONC_I32(0x953850), p = ONC_I32(0x953864);
    void *ro = *(void **)0x952C54;
    uint8_t rd[0x34];
    memcpy(rd, RD, sizeof rd);
    set_target(sw, sh, rows, pitch);
    int32_t *c = (int32_t *)RD;
    c[1] = l; c[2] = t; c[3] = r; c[4] = b;
    c[5] = l; c[6] = t; c[7] = r - 1; c[8] = b - 1;
    draw();
    ONC_I32(NOX_SCREEN_W) = sw; ONC_I32(NOX_SCREEN_H) = sh;
    ONC_I32(0x95384C) = mw; ONC_I32(0x953850) = mh; ONC_I32(0x953864) = p;
    *(void **)0x952C54 = ro;
    memcpy(RD, rd, sizeof rd);
    return 1;
}

/* the world rectangle drawn now (the profiler's log) */
int onc_uisplit_view(int *w, int *h) {
    if (g_zactive) { *w = g_zs_w; *h = g_zs_h; } else { *w = g_W; *h = g_H; }
    return g_ready;
}

/* for the high frame rate's extra pictures (hfr.c) */
void onc_uisplit_world_begin(void) { begin_world(); }
void onc_uisplit_world_end(void) { end_world(); }
/* an extra picture with only the world new: drawn into the world buffer, shown under the last GUI layer
   (ddraw.dll OncPresentWorld); 0 if the split is not working (the caller then draws a full picture) */
int onc_uisplit_extra_world(void (*draw)(void)) {
    static void (__cdecl *present)(const dsp_layers *);
    if (!present) {
        HMODULE dd = GetModuleHandleA("ddraw.dll");
        present = dd ? (void (__cdecl *)(const dsp_layers *))GetProcAddress(dd, "OncPresentWorld") : NULL;
        if (!present) return 0;
    }
    begin_world();
    if (!g_in_pass) return 0;
    draw();
    g_world_only = 1;
    end_world();
    g_world_only = 0;
    dsp_layers L;
    memset(&L, 0, sizeof L);
    fill_world(&L);
    L.gw = g_G; L.gh = g_GH;
    present(&L);
    return 1;
}

/* 0x42D43A: the world drawing */
/* The map (when open) and the centred messages are drawn by 0x47CB10, right after the world (call at 0x42D6F8)
   and placed by the world viewport. Drawn into the world buffer they did not show (the open sound, no map), so
   they are drawn after the world pass into the GUI buffer, with the viewport's
   screen rectangle in GUI coordinates. The GUI layer is kept for the high frame rate's extra pictures. */
static int g_overlays_due;
static void __cdecl world_overlays(void) {
    if (g_in_pass) { g_overlays_due = 1; return; } /* after the world pass */
    ((void (__cdecl *)(void))0x47CB10)();
}
static void draw_overlays_gui(void) {
    int32_t *vp = (int32_t *)0x687378, real[16];
    memcpy(real, vp, sizeof real);
    vp[0] = 0; vp[1] = 0; vp[2] = g_G - 1; vp[3] = g_GH - 1; vp[8] = g_G; vp[9] = g_GH;
    onc_clip_full();
    ((void (__cdecl *)(void))0x47CB10)();
    memcpy(vp, real, sizeof real);
}

static void __cdecl world_draw(void) {
    begin_world();
    ((void(__cdecl *)(void))0x42D4A0)();
    end_world();
    if (g_overlays_due) { g_overlays_due = 0; draw_overlays_gui(); }
}

/* ---------------- world zoom ---------------- */
/* numpad + / - / * (and R3 + the right stick on the pad): the world is drawn 1:1 into a smaller rectangle in the
   middle of the world buffer (the viewport, 0x687378) and ddraw.dll scales that rectangle to the whole screen with
   the display filter (lanczos, bicubic...), under the GUI. */

static void zoom_set(double v) {
    if (v < 1) v = 1;
    if (v > g_zoom_max) v = g_zoom_max;
    g_zoom_target = v;
}
void onc_zoom_step(int dir) {
    double t = (double)(long long)(g_zoom_target / g_zoom_step + 0.5) * g_zoom_step;
    zoom_set(t + dir * g_zoom_step);
}
void onc_zoom_add(double v) { zoom_set(g_zoom_target + v); g_zoom = g_zoom_target; }
void onc_zoom_reset(void) { zoom_set(1); }
int onc_zoom_ready(void) { return g_ready; }

static void zoom_keys(void) {
    static int was[3];
    /* window and key state are calls into the window server (slow under Wine): ~30 times a second is plenty */
    static DWORD last;
    DWORD t = GetTickCount();
    if (t - last < 30) return;
    last = t;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    int mine = pid == GetCurrentProcessId();
    static const int vk[3] = {VK_ADD, VK_SUBTRACT, VK_MULTIPLY};
    for (int i = 0; i < 3; i++) {
        int down = mine && (GetAsyncKeyState(vk[i]) & 0x8000);
        if (down && !was[i]) { if (i == 0) onc_zoom_step(1); else if (i == 1) onc_zoom_step(-1); else onc_zoom_reset(); }
        was[i] = down;
    }
}

/* called once a drawn frame with the world viewport set up (camera.c, before the camera step): the zoomed
   rectangle. Returns the rectangle for 0x4324C0. */
void onc_zoom_viewport(int *r) {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    double dt = g_zlast.QuadPart ? (now.QuadPart - g_zlast.QuadPart) * 1000.0 / g_zfreq.QuadPart : 0;
    g_zlast = now;
    if (dt > 100) dt = 100;
    zoom_keys();
    if (g_zoom != g_zoom_target) {
        if (g_zoom_ease <= 0) g_zoom = g_zoom_target;
        else {
            g_zoom += (g_zoom_target - g_zoom) * (1 - exp(-dt / g_zoom_ease));
            if (fabs(g_zoom_target - g_zoom) < 0.002) g_zoom = g_zoom_target;
        }
    }
    g_zactive = 0;
    if (!g_in_pass) return;
    if (g_zoom <= 1.0001) { /* the whole world buffer (every frame: the game sets the viewport up for the GUI size) */
        ONC_I32(0x687378) = 0; ONC_I32(0x68737C) = 0;
        ONC_I32(0x687380) = g_W - 1; ONC_I32(0x687384) = g_H - 1;
        ONC_I32(0x687398) = g_W; ONC_I32(0x68739C) = g_H;
        r[0] = 0; r[1] = 0; r[2] = g_W - 1; r[3] = g_H - 1;
        return;
    }
    int bw = g_W, bh = g_H;
    int sw = (int)(bw / g_zoom + 0.5), sh = (int)(bh / g_zoom + 0.5);
    int ox = ((bw - sw) / 2) & ~3, oy = (bh - sh) / 2;
    sw = bw - 2 * ox; sh = bh - 2 * oy;
    if (sw < 64 || sh < 64) return;
    g_zactive = 1;
    g_zoff_x = ox; g_zoff_y = oy; g_zs_w = sw; g_zs_h = sh;
    ONC_I32(0x687378) = ox; ONC_I32(0x68737C) = oy;
    ONC_I32(0x687380) = ox + sw - 1; ONC_I32(0x687384) = oy + sh - 1;
    ONC_I32(0x687398) = sw; ONC_I32(0x68739C) = sh;
    r[0] = ox; r[1] = oy; r[2] = ox + sw - 1; r[3] = oy + sh - 1;
}

/* view (world drawing) <-> full screen */
static void view_to_full(int *x, int *y) {
    if (!g_zactive) return;
    double cx = g_W / 2.0, cy = g_H / 2.0;
    *x = (int)lround(cx + (*x - cx) * ((double)g_W / g_zs_w));
    *y = (int)lround(cy + (*y - cy) * ((double)g_H / g_zs_h));
}
static void full_to_view(int *x, int *y) {
    if (!g_zactive) return;
    double cx = g_W / 2.0, cy = g_H / 2.0;
    *x = (int)floor(cx + (*x - cx) / ((double)g_W / g_zs_w));
    *y = (int)floor(cy + (*y - cy) / ((double)g_H / g_zs_h));
}

/* ---------------- the shown frame: two layers for ddraw.dll ---------------- */

int __cdecl onc_uisplit_frame(const uint16_t *gui, int pitch, int w, int h, dsp_layers *L) {
    if (!onc_uisplit_active() || w != g_G || h != g_GH) return 0;
    /* the GUI = the pixels that differ from the snapshot: compared by ddraw.dll (on the GPU when it can) */
    fill_world(L);
    L->gui = gui; L->gpitch = pitch; L->gw = w; L->gh = h;
    return 1;
}
/* the world rectangle shown now, the snapshot and the cursor */
static void fill_world(dsp_layers *L) {
    L->world = g_world; L->wpitch = g_W * 2;
    if (g_zactive) { L->wx = g_zoff_x; L->wy = g_zoff_y; L->ww = g_zs_w; L->wh = g_zs_h; }
    else { L->wx = 0; L->wy = 0; L->ww = g_W; L->wh = g_H; }
    L->fw = g_W; L->fh = g_H;
    L->mask = NULL;
    L->snap = g_snap;
    onc_cursor_layer(&L->cur_a, &L->cur_b, &L->cur_pitch, &L->cur_w, &L->cur_h, &L->cur_x, &L->cur_y, &L->cur_serial);
}

/* ---------------- mouse positions for the world ---------------- */
/* the mouse block (0x697AF0) holds the position and the button states: a copy with the position for the world */
static int32_t g_mouse_w[0x70 / 4];
static int32_t *__cdecl world_mouse(void) {
    int32_t *m = ((int32_t * (__cdecl *)(void))0x432360)();
    if (!onc_uisplit_active()) return m;
    memcpy(g_mouse_w, m, sizeof g_mouse_w);
    onc_ui_g2w(&g_mouse_w[0], &g_mouse_w[1]);
    return g_mouse_w;
}

/* 0x42EB90 (called at 0x42D376) works the buttons (it also writes their states back): it gets the real block,
   with the world position in it while it runs */
static void __cdecl controls(void) {
    int32_t *m = ((int32_t * (__cdecl *)(void))0x432360)();
    if (!onc_uisplit_active()) { ((void(__cdecl *)(void))0x42EB90)(); return; }
    int ox = m[0], oy = m[1], wx = ox, wy = oy;
    onc_ui_g2w(&wx, &wy);
    m[0] = wx; m[1] = wy;
    ((void(__cdecl *)(void))0x42EB90)();
    if (m[0] == wx && m[1] == wy) { m[0] = ox; m[1] = oy; }
}

/* the floor tiles are drawn through a buffer sized for the screen (0x432720(w, h) at the mode change): for the
   world buffer */
static int __cdecl tile_init(int w, int h) {
    if (g_ready) { if (w < g_W) w = g_W; if (h < g_H) h = g_H; }
    return ((int(__cdecl *)(int, int))0x432720)(w, h);
}

/* ---------------- speech bubbles ---------------- */
/* 0x46E440(vp) draws the speech bubbles (list 0x6D6EFC) with the GUI, but places them by the world viewport:
   x = vp.screen.x + speaker.x - vp.world.x (0x46E568..0x46E592), and keeps them inside vp's screen rectangle. With
   the split rendering (and the zoom) that is a world buffer position: the anchor (64 px above the speaker) is mapped
   to the GUI, and the drawer gets a viewport covering the whole GUI screen. */
static int32_t *g_bubble_vp; /* the real viewport while the bubbles are drawn */
int32_t onc_bubble_x, onc_bubble_y;
void __cdecl onc_bubble_pos(int32_t *vp, int wx, int wy) {
    int32_t *v = g_bubble_vp ? g_bubble_vp : vp;
    int x = v[0] + wx - v[4], y = v[1] + wy - v[5];
    if (g_bubble_vp) { int ay = y - 64; onc_ui_w2g(&x, &ay); y = ay + 64; }
    onc_bubble_x = x; onc_bubble_y = y;
}
/* in place of 0x46E568..0x46E592 (ebp = speaker x, esi = the bubble, edi = vp; esp as there) */
__asm__(".globl _onc_bubble_stub\n_onc_bubble_stub:\n"
        "  movzwl 0x286(%esi), %eax\n  pushl %eax\n  pushl %ebp\n  pushl %edi\n  call _onc_bubble_pos\n  addl $12, %esp\n"
        "  movl _onc_bubble_x, %ebp\n  movl _onc_bubble_y, %esi\n"
        "  movl %ebp, 0x48(%esp)\n  movl 0x1c(%esp), %eax\n  leal 0x14(%esp), %edx\n"
        "  pushl $0x46E592\n  ret\n");
void onc_bubble_stub(void);
/* The drawer (and the clip it sets for the bubbles) also reads the viewport global 0x687378 directly, so the global
   itself covers the GUI screen while the bubbles are drawn (with only the argument changed, the clip stayed at the
   zoomed world rectangle in world buffer pixels: bubbles above / left of it were cut off). */
static void __cdecl bubble_draw(int32_t *vp) {
    if (!onc_uisplit_active() || !vp) { ((void (__cdecl *)(void *))0x46E440)(vp); return; }
    int32_t real[16];
    memcpy(real, vp, sizeof real);
    vp[0] = 0; vp[1] = 0; vp[2] = g_G - 1; vp[3] = g_GH - 1; vp[8] = g_G; vp[9] = g_GH;
    g_bubble_vp = real;
    onc_clip_full();
    ((void (__cdecl *)(void *))0x46E440)(vp);
    g_bubble_vp = NULL;
    memcpy(vp, real, sizeof real);
}

void onc_uisplit_init(void) {
    if (!g_ready) return;
    onc_hook_call("ui split world", 0x42D43A, world_draw, 0x42D4A0);
    onc_hook_call("ui split map and messages", 0x42D6F8, world_overlays, 0x47CB10);
    onc_hook_call("gui window clip (below)", 0x489C0E, gui_window_draw, 0x489C80);
    onc_hook_call("gui window clip (middle)", 0x489C38, gui_window_draw, 0x489C80);
    onc_hook_call("gui window clip (above)", 0x489C62, gui_window_draw, 0x489C80);
    onc_hook_call("world mouse (input)", 0x42D321, world_mouse, 0x432360);
    onc_hook_call("world mouse (controls)", 0x42D376, controls, 0x42EB90);
    onc_hook_call("floor buffer", 0x43260C, tile_init, 0x432720);
    onc_hook_call("floor buffer 2", 0x4329F9, tile_init, 0x432720);
    onc_hook_call("world mouse (hover)", 0x47EFA2, world_mouse, 0x432360);
    static const uint8_t e_bub[] = {0x8B, 0x4F, 0x10, 0x8B, 0x07, 0x8B, 0x57, 0x14, 0x2B, 0xE9};
    if (!memcmp((void *)0x46E568, e_bub, sizeof e_bub)) {
        uint8_t b[5] = {0xE9};
        int32_t rel = (int32_t)((uintptr_t)onc_bubble_stub - (0x46E568 + 5));
        memcpy(b + 1, &rel, 4);
        DWORD old;
        VirtualProtect((void *)0x46E568, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)0x46E568, b, 5);
        VirtualProtect((void *)0x46E568, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)0x46E568, 5);
        onc_hook_call("speech bubbles", 0x42E525, bubble_draw, 0x46E440);
    } else onc_log("speech bubbles patch refused: bytes differ");
}

/* the F7 menu */
double onc_zoom_get(void) { return g_zoom_target; }
double onc_zoom_max(void) { return g_zoom_max; }
void onc_zoom_set(double v) { zoom_set(v); }
int onc_uiscale_active(void) { return g_ready ? g_scale100 : 100; }
