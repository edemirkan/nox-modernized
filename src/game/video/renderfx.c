/* Small renderer options:
   [display] light_flicker = original | gentle | off
     The game adds a random amount (0 to 1/4 of the intensity) to every flickering light (torches, braziers,
     fires) each frame (0x468D22, random 0x415EF0); at higher resolutions the lit floor and walls throb.
     Gentle eases each light towards its random value (180 ms); off keeps the average.
   [display] map_background = 0..100 (percent darkness behind the map; the original is solid black / 50%)
     0x48B99A draws the map background: replaced by darkening the screen behind the map by that amount. */
#include "onc.h"
#include <math.h>
#include <string.h>

/* ---------------- light flicker ---------------- */
static int g_flicker; /* 0 original, 1 gentle, 2 off */
typedef struct { void *dr; float v; LARGE_INTEGER t; } flick_t;
static flick_t g_fl[128];
static LARGE_INTEGER g_freq;

int __cdecl onc_flicker_c(void *dr, int maxv) {
    int rnd = ((int(__cdecl *)(int, int, const char *, int))0x415EF0)(0, maxv, (const char *)0x5A779C, 0x10B);
    if (g_flicker == 2) return maxv / 2;
    if (g_flicker != 1) return rnd;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    flick_t *st = NULL, *old = &g_fl[0];
    for (int i = 0; i < 128; i++) {
        if (g_fl[i].dr == dr) { st = &g_fl[i]; break; }
        if (g_fl[i].t.QuadPart < old->t.QuadPart) old = &g_fl[i];
    }
    if (!st) { st = old; st->dr = dr; st->v = maxv / 2.0f; st->t = now; return (int)(st->v + 0.5f); }
    float dt = (float)((now.QuadPart - st->t.QuadPart) * 1000.0 / g_freq.QuadPart);
    if (dt > 100) dt = 100;
    st->t = now;
    st->v += (rnd - st->v) * (1.0f - expf(-dt / 180.0f));
    return (int)(st->v + 0.5f);
}
/* the call passes (0, max, file, line); the light's drawable is in esi */
__asm__(".globl _onc_flicker_stub\n_onc_flicker_stub:\n"
        "  movl 8(%esp), %eax\n  pushl %eax\n  pushl %esi\n  call _onc_flicker_c\n  addl $8, %esp\n  ret\n");
void onc_flicker_stub(void);

/* ---------------- map background ---------------- */
static int g_map_dark;
void __cdecl onc_map_background(int x, int y, int w, int h) {
    if (g_map_dark <= 0) return;
    uint8_t **rows = *(uint8_t ***)0x952C54;
    int sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    if (!rows) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    unsigned keep = (unsigned)(100 - g_map_dark) * 32 / 100; /* brightness left, in 1/32 */
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *p = (uint16_t *)(rows[yy] + x * 2);
        for (int i = 0; i < w; i++) {
            unsigned v = p[i];
            unsigned r = ((v >> 11) * keep) >> 5, g = (((v >> 5) & 63) * keep) >> 5, b = ((v & 31) * keep) >> 5;
            p[i] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

/* ---------------- sentry rays ---------------- */
/* 0x4C1E80 draws the sentry rays; a ray starting outside the view starts hidden. It checked the start (screen
   pixels, esi / edi) against 0 .. view size, i.e. the view's corner at the screen's corner. With the world zoom the
   view sits in the middle of the world buffer, so rays starting in its right / bottom part started hidden and were
   drawn inverted. 0x4C1EF9..0x4C1F1B: checked against the
   view's own edges (vp +0 / +4 the corner, +0x20 / +0x24 the size); ebx = 0 hides the start. eax and ecx are kept. */
__asm__(".globl _onc_ray_edge_stub\n_onc_ray_edge_stub:\n"
        "  pushl %eax\n"
        "  movl 0x3C(%esp), %edx\n"            /* the viewport (the function's argument) */
        "  movl (%edx), %eax\n  cmpl %eax, %esi\n  jle 1f\n"
        "  addl 0x20(%edx), %eax\n  decl %eax\n  cmpl %eax, %esi\n  jge 1f\n"
        "  movl 4(%edx), %eax\n  cmpl %eax, %edi\n  jle 1f\n"
        "  addl 0x24(%edx), %eax\n  decl %eax\n  cmpl %eax, %edi\n  jge 1f\n"
        "  popl %eax\n  jmp 2f\n"
        "1:\n  popl %eax\n  xorl %ebx, %ebx\n"
        "2:\n  pushl $0x4C1F1B\n  ret\n");
void onc_ray_edge_stub(void);

static void write(uint32_t a, const void *b, size_t n) {
    DWORD old;
    VirtualProtect((void *)a, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)a, b, n);
    VirtualProtect((void *)a, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)a, n);
}

void onc_renderfx_init(void) {
    char s[32];
    QueryPerformanceFrequency(&g_freq);
    onc_cfg_str("display", "light_flicker", "gentle", s, sizeof s);
    g_flicker = !_stricmp(s, "off") ? 2 : !_stricmp(s, "original") ? 0 : 1;
    onc_hook_call("light flicker", 0x468D22, onc_flicker_stub, 0x415EF0); /* always: the F7 menu can change it */

    g_map_dark = onc_cfg_int("display", "map_background", 25);
    if (g_map_dark < 0) g_map_dark = 0;
    if (g_map_dark > 100) g_map_dark = 100;
    static const uint8_t expect[] = {0x8B, 0x5D, 0x00, 0x83, 0xC4, 0x14};
    if (!memcmp((void *)0x48B99A, expect, sizeof expect)) {
        uint8_t b[24] = {0x8B, 0x5D, 0x00,             /* mov ebx, [ebp] (as before) */
                         0x83, 0xC4, 0x14,             /* add esp, 0x14 (as before) */
                         0x56, 0x56, 0x57, 0x6A, 0x00, /* push esi, esi, edi, 0 */
                         0xE8, 0, 0, 0, 0,             /* call onc_map_background */
                         0x83, 0xC4, 0x10,             /* add esp, 0x10 */
                         0xE9, 0, 0, 0, 0};            /* jmp 0x48B9F7 */
        int32_t rel = (int32_t)((uintptr_t)onc_map_background - (0x48B99A + 16));
        memcpy(b + 12, &rel, 4);
        rel = (int32_t)(0x48B9F7 - (0x48B99A + 24));
        memcpy(b + 20, &rel, 4);
        write(0x48B99A, b, sizeof b);
        onc_log("map background: %d%% dark", g_map_dark);
    } else onc_log("map background patch refused: bytes differ");
    {
        static const uint8_t e[] = {0x85, 0xF6, 0x7E, 0x1C, 0x8B, 0x54, 0x24, 0x38, 0x8B, 0x52, 0x20, 0x4A, 0x3B, 0xF2,
                                    0x7D, 0x10, 0x85, 0xFF, 0x7E, 0x0C, 0x8B, 0x54, 0x24, 0x38, 0x8B, 0x52, 0x24, 0x4A,
                                    0x3B, 0xFA, 0x7C, 0x02, 0x33, 0xDB};
        if (!memcmp((void *)0x4C1EF9, e, sizeof e)) {
            uint8_t b[5] = {0xE9};
            int32_t rel = (int32_t)((uintptr_t)onc_ray_edge_stub - (0x4C1EF9 + 5));
            memcpy(b + 1, &rel, 4);
            write(0x4C1EF9, b, 5);
            onc_log("sentry rays: start checked against the view's edges");
        } else onc_log("sentry rays patch refused: bytes differ");
    }
    onc_log("light flicker: %s", g_flicker == 2 ? "off" : g_flicker ? "gentle" : "original");
}

/* the F7 menu */
int onc_flicker_get(void) { return g_flicker; }
void onc_flicker_set(int v) { if (v >= 0 && v <= 2) g_flicker = v; }
int onc_mapbg_get(void) { return g_map_dark; }
void onc_mapbg_set(int v) { if (v >= 0 && v <= 100) g_map_dark = v; }
