/* Smooth camera, for a calmer picture at high resolutions: the view eases after the player
   (time constant camera_ease_ms) with a dead zone around him (camera_dead_zone, part of the smaller screen side)
   and never lets him get further than camera_max_lag (part of the screen size) from the centre. The view snaps
   on teleports and map changes and stays on whole pixels.
   Viewport 0x687378: +0 screen min, +0x10 world min (what is drawn), +0x18 world max (the player position, game
   logic relies on it), +0x20 size, +0x30 jiggle. 0x42C9A0(x, y) sets it from the player; we only move world min.
   Everything the game puts at the screen centre because the player used to be there follows the player:
     facing towards the cursor 0x42F5DA, spell gesture hands 0x48C9D3, the bars beside the player 0x476666,
     the line-of-sight origin 0x477EF9 (and its reach to the four screen edges, 0x4782CA..0x4785F6) and the
     sight origin on screen 0x47A25E. */
#include "onc.h"
#include <math.h>
#include <string.h>

#define VP_SMINX 0x687378
#define VP_SMINY 0x68737C
#define VP_WMINX 0x687388
#define VP_WMINY 0x68738C
#define VP_WMAXX 0x687390
#define VP_WMAXY 0x687394
#define VP_SIZEX 0x687398
#define VP_SIZEY 0x68739C

static int g_on;
static float g_ease = 120, g_dz = 0.15f, g_lag = 0.25f;
static int g_valid;
static double g_pos[2];
static LARGE_INTEGER g_last, g_freq;

/* read by the patched game code: twice the player position on the screen (the game halves "the screen size"),
   the player on the screen, and twice the sight origin's distance to each edge */
int32_t g_cam_cx2, g_cam_cy2, g_cam_px, g_cam_py, g_cam_gx2, g_cam_gy2;
int32_t g_sight_top2, g_sight_bottom2, g_sight_left2, g_sight_right2;

static void clamp_lag(void) {
    double mx = g_lag * ONC_I32(VP_SIZEX), my = g_lag * ONC_I32(VP_SIZEY);
    double tx = ONC_I32(VP_WMAXX), ty = ONC_I32(VP_WMAXY);
    if (tx - g_pos[0] > mx) g_pos[0] = tx - mx; else if (tx - g_pos[0] < -mx) g_pos[0] = tx + mx;
    if (ty - g_pos[1] > my) g_pos[1] = ty - my; else if (ty - g_pos[1] < -my) g_pos[1] = ty + my;
}

static void apply(void) {
    int sx = ONC_I32(VP_SIZEX), sy = ONC_I32(VP_SIZEY);
    if (g_on && g_valid && ONC_I32(NOX_CLIENT_CONNECTED)) {
        ONC_I32(VP_WMINX) = (int)lround(g_pos[0]) - sx / 2;
        ONC_I32(VP_WMINY) = (int)lround(g_pos[1]) - sy / 2;
    }
    /* the player on the screen the world is drawn on (with ui_scale: the full size world buffer) */
    int smx = ONC_I32(VP_SMINX), smy = ONC_I32(VP_SMINY);
    int px = ONC_I32(VP_WMAXX) - ONC_I32(VP_WMINX) + smx;
    int py = ONC_I32(VP_WMAXY) - ONC_I32(VP_WMINY) + smy;
    if (!ONC_I32(NOX_CLIENT_CONNECTED) || px < smx || py < smy || px >= smx + sx || py >= smy + sy) { px = smx + sx / 2; py = smy + sy / 2; }
    g_cam_px = px; g_cam_py = py;
    g_cam_cx2 = px * 2; g_cam_cy2 = py * 2;
    /* and on the GUI */
    extern void onc_ui_w2g(int *x, int *y);
    int gx = px, gy = py;
    onc_ui_w2g(&gx, &gy);
    g_cam_gx2 = gx * 2; g_cam_gy2 = gy * 2;
}

/* the player on the GUI (where the cursor lives) */
int onc_player_screen(int *x, int *y) {
    *x = g_cam_gx2 ? g_cam_gx2 / 2 : ONC_I32(NOX_SCREEN_W) / 2;
    *y = g_cam_gy2 ? g_cam_gy2 / 2 : ONC_I32(NOX_SCREEN_H) / 2;
    return 1;
}

/* 0x42C9A0: the camera follows the player (a new position from the server) */
static void *g_cam_orig;
static void __cdecl camera_set(int x, int y) {
    ((void(__cdecl *)(int, int))g_cam_orig)(x, y);
    if (!g_on) return;
    double tx = ONC_I32(VP_WMAXX), ty = ONC_I32(VP_WMAXY);
    if (!g_valid || fabs(tx - g_pos[0]) > ONC_I32(VP_SIZEX) || fabs(ty - g_pos[1]) > ONC_I32(VP_SIZEY)) {
        g_pos[0] = tx; g_pos[1] = ty; /* first use, teleport, new map: snap */
        g_valid = 1;
        QueryPerformanceCounter(&g_last);
    }
    clamp_lag();
    apply();
}

/* once a drawn frame, before the world is drawn (0x42D5CD calls 0x4324C0 with the view rectangle) */
static int g_last_rect[4], g_have_rect;
static void __cdecl frame_step(int a, int b, int c, int d);
/* the high frame rate's extra pictures: the camera (and zoom) step and the viewport, as for a drawn frame */
int onc_camera_extra_step(void) {
    if (!g_have_rect) return 0;
    frame_step(g_last_rect[0], g_last_rect[1], g_last_rect[2], g_last_rect[3]);
    return 1;
}
static void __cdecl frame_step(int a, int b, int c, int d) {
    g_last_rect[0] = a; g_last_rect[1] = b; g_last_rect[2] = c; g_last_rect[3] = d; g_have_rect = 1;
    /* world zoom: a smaller world rectangle (uisplit.c), before the camera works with its size */
    extern void onc_zoom_viewport(int *r);
    int r[4] = {a, b, c, d};
    onc_zoom_viewport(r);
    a = r[0]; b = r[1]; c = r[2]; d = r[3];
    if (g_on && g_valid && ONC_I32(NOX_CLIENT_CONNECTED)) {
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double dt = (now.QuadPart - g_last.QuadPart) * 1000.0 / g_freq.QuadPart;
        g_last = now;
        if (dt > 100) dt = 100;
        double ex = ONC_I32(VP_WMAXX) - g_pos[0], ey = ONC_I32(VP_WMAXY) - g_pos[1];
        double dist = sqrt(ex * ex + ey * ey);
        int side = ONC_I32(VP_SIZEX) < ONC_I32(VP_SIZEY) ? ONC_I32(VP_SIZEX) : ONC_I32(VP_SIZEY);
        double dz = g_dz * side;
        if (dist > dz) {
            double k = g_ease > 0 ? 1 - exp(-dt / g_ease) : 1;
            double move = (dist - dz) * k;
            g_pos[0] += ex / dist * move;
            g_pos[1] += ey / dist * move;
        }
        clamp_lag();
    } else if (!ONC_I32(NOX_CLIENT_CONNECTED)) {
        g_valid = 0;
    }
    apply();
    ((void(__cdecl *)(int, int, int, int))0x4324C0)(a, b, c, d);
}

/* 0x477EF9: the line-of-sight origin was set to the view centre; it is the player */
void __cdecl onc_sight_c(void) {
    int sx = ONC_I32(VP_SIZEX), sy = ONC_I32(VP_SIZEY);
    g_sight_top2 = g_sight_bottom2 = sy;
    g_sight_left2 = g_sight_right2 = sx;
    if (!g_on || !g_valid || !ONC_I32(NOX_CLIENT_CONNECTED)) return;
    int minx = ONC_I32(VP_WMINX), miny = ONC_I32(VP_WMINY), cx = ONC_I32(VP_WMAXX), cy = ONC_I32(VP_WMAXY);
    if (cx < minx || cy < miny || cx >= minx + sx || cy >= miny + sy) return;
    ONC_I32(0x6DD4E8) = cx;
    ONC_I32(0x6DD4EC) = cy;
    g_sight_top2 = 2 * (cy - miny);
    g_sight_bottom2 = 2 * (miny + sy - cy);
    g_sight_left2 = 2 * (cx - minx);
    g_sight_right2 = 2 * (minx + sx - cx);
}
__asm__(".globl _onc_sight_stub\n_onc_sight_stub:\n"
        "  movl %eax, 0x6DD4EC\n  pushal\n  pushfl\n  call _onc_sight_c\n  popfl\n  popal\n  ret\n");
void onc_sight_stub(void);

static int g_ok;
static void put(uint32_t a, const void *expect, const void *v, size_t n) {
    if (memcmp((void *)a, expect, n)) { onc_log("camera: patch at %08X refused: bytes differ", a); g_ok = 0; return; }
    DWORD old;
    VirtualProtect((void *)a, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)a, v, n);
    VirtualProtect((void *)a, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)a, n);
}
/* "mov eax, [abs]" (A1 imm32) reading another address */
static void redirect(uint32_t a, uint32_t from, void *to) {
    uint8_t o[5] = {0xA1}, v[5] = {0xA1};
    memcpy(o + 1, &from, 4);
    uint32_t t = (uint32_t)to;
    memcpy(v + 1, &t, 4);
    put(a, o, v, 5);
}
/* the size/2 for a sight edge: "mov eax, <size>; cdq; sub eax, edx" -> "mov eax, [twice the distance]" */
static void sight_edge(uint32_t a, int from_ebp, int ofs, int32_t *to) {
    uint8_t o6[6] = {0x8B, 0x47, (uint8_t)ofs, 0x99, 0x2B, 0xC2}, o5[5] = {0x8B, 0xC5, 0x99, 0x2B, 0xC2};
    uint8_t v[6] = {0xA1, 0, 0, 0, 0, 0x90};
    uint32_t t = (uint32_t)to;
    memcpy(v + 1, &t, 4);
    if (from_ebp) put(a, o5, v, 5); else put(a, o6, v, 6);
}

void onc_camera_init(void) {
    QueryPerformanceFrequency(&g_freq);
    g_on = onc_cfg_int("display", "camera_smooth", 1);
    g_ease = (float)onc_cfg_int("display", "camera_ease_ms", 120);
    g_dz = onc_cfg_float100("display", "camera_dead_zone", 15) / 100.0f;
    g_lag = onc_cfg_float100("display", "camera_max_lag", 25) / 100.0f;
    if (g_ease < 0) g_ease = 0;
    if (g_dz < 0) g_dz = 0;
    if (g_lag <= 0 || g_lag > 0.4f) g_lag = 0.25f;
    /* once a frame before the world is drawn: the zoom and the camera */
    if (!onc_hook_call("camera frame", 0x42D5CD, frame_step, 0x4324C0)) { onc_log("camera / zoom frame hook NOT installed"); g_on = 0; return; }
    int want = g_on;
    g_on = 1; /* the patches always go in: the F7 menu turns the smooth camera on and off */
    g_ok = 1;
    static const uint8_t pro[] = {0xA1, 0x98, 0x73, 0x68, 0x00};
    g_cam_orig = onc_hook_detour("camera", 0x42C9A0, camera_set, pro, sizeof pro);
    if (!g_cam_orig) { onc_log("smooth camera NOT installed"); g_on = 0; return; }
    /* things drawn or aimed around the screen centre follow the player */
    redirect(0x42F5DA, NOX_SCREEN_H, &g_cam_cy2); /* facing */
    redirect(0x42F5E6, NOX_SCREEN_W, &g_cam_cx2);
    redirect(0x48C9D3, NOX_SCREEN_H, &g_cam_gy2); /* gesture hands (drawn with the GUI) */
    redirect(0x48C9E4, NOX_SCREEN_W, &g_cam_gx2);
    redirect(0x487A66, NOX_SCREEN_W, &g_cam_gx2); /* the walk cursor's arrow points away from the player */
    redirect(0x487A74, NOX_SCREEN_H, &g_cam_gy2);
    redirect(0x476666, NOX_SCREEN_W, &g_cam_gx2); /* bars beside the player */
    redirect(0x476685, NOX_SCREEN_H, &g_cam_gy2);
    /* line of sight: origin, reach to the edges, origin on the screen */
    { uint8_t o[5] = {0xA3, 0xEC, 0xD4, 0x6D, 0x00}, v[5] = {0xE8};
      int32_t rel = (int32_t)((uint32_t)onc_sight_stub - (0x477EF9 + 5));
      memcpy(v + 1, &rel, 4);
      put(0x477EF9, o, v, 5); }
    sight_edge(0x4782CA, 0, 0x24, &g_sight_top2);
    sight_edge(0x478332, 0, 0x24, &g_sight_top2);
    sight_edge(0x4783A7, 1, 0, &g_sight_bottom2);
    sight_edge(0x47841D, 1, 0, &g_sight_bottom2);
    sight_edge(0x4784AE, 1, 0, &g_sight_right2);
    sight_edge(0x47851C, 0, 0x20, &g_sight_right2);
    sight_edge(0x47858E, 0, 0x20, &g_sight_left2);
    sight_edge(0x4785F6, 0, 0x20, &g_sight_left2);
    { static const uint8_t o[12] = {0x8B, 0x41, 0x20, 0x99, 0x2B, 0xC2, 0x8B, 0x11, 0xD1, 0xF8, 0x03, 0xC2};
      uint8_t v[12] = {0xA1, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
      uint32_t t = (uint32_t)&g_cam_px; memcpy(v + 1, &t, 4);
      put(0x47A25E, o, v, 12); }
    { static const uint8_t o[18] = {0x8B, 0x41, 0x24, 0x99, 0x2B, 0xC2, 0x8B, 0x51, 0x30, 0xD1, 0xF8, 0x03, 0xC2,
                                    0x8B, 0x51, 0x04, 0x03, 0xC2};
      uint8_t v[18] = {0xA1, 0, 0, 0, 0, 0x8B, 0x51, 0x04, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
      uint32_t t = (uint32_t)&g_cam_py; memcpy(v + 1, &t, 4);
      put(0x47A272, o, v, 18); }
    g_on = want;
    onc_log("smooth camera %s: ease %d ms, dead zone %d%%, max lag %d%%%s", g_on ? "on" : "off", (int)g_ease, (int)(g_dz * 100), (int)(g_lag * 100),
            g_ok ? "" : " - SOME PATCHES REFUSED");
}

/* the F7 menu */
int onc_camera_get(void) { return g_on; }
/* the F7 menu: the dead zone and the farthest the player gets from the centre, in percent */
int onc_camera_dz_get(void) { return (int)(g_dz * 100 + 0.5f); }
void onc_camera_dz_set(int pc) { if (pc >= 0 && pc <= 40) g_dz = pc / 100.0f; }
int onc_camera_lag_get(void) { return (int)(g_lag * 100 + 0.5f); }
void onc_camera_lag_set(int pc) { if (pc >= 5 && pc <= 40) g_lag = pc / 100.0f; }
void onc_camera_set(int on) { g_on = on != 0; if (g_on) g_valid = 0; /* snap to the player first */ }
