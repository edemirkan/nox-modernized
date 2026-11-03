/* Custom resolutions. Game.exe knows three modes (640x480, 800x600, 1024x768: the table at 0x59B24C and the
   Options radio buttons) and clamps the screen to a maximum of 1024x768 (0x598F58, set again at 0x437C16 and
   0x43AF88; 0x432550 clamps). [display] resolution = WxH (or "desktop") puts that size in the 1024x768 slot:
   picking "1024x768" in Options, or a nox.cfg VideoMode of 1024 768, gives the custom size, and the maximum is
   raised to match. Our ddraw.dll accepts any mode; GOG's wrapper may too. In a game the chosen size is always used,
   whatever mode nox.cfg holds (a fresh install's is 640x480); the menus keep their 640x480. */
#include "onc.h"
#include <stdio.h>
#include <string.h>

static int g_w, g_h, g_join_clamp;

static int patch(const char *name, uintptr_t addr, const void *expect, const void *repl, size_t n) {
    if (memcmp((void *)addr, expect, n)) { onc_log("PATCH %s %08X refused: original bytes differ", name, (unsigned)addr); return 0; }
    DWORD old;
    if (!VirtualProtect((void *)addr, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy((void *)addr, repl, n);
    VirtualProtect((void *)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)addr, n);
    return 1;
}
static int patch_imm(const char *name, uintptr_t addr, int32_t from, int32_t to) {
    return patch(name, addr, &from, &to, 4);
}

/* 0x43BA90 applies the game's video mode (arg 1: the menu mode 640x480, else the chosen one) */
static void *g_apply_orig;
static int __cdecl apply_mode(int menu) {
    /* the game's chosen mode is always the custom size: nox.cfg may hold 640x480 (a fresh install), 800x600 or an
       older custom size. The menu mode (menu = 1) is the game's own 640x480 and is not taken from here. */
    int w = ONC_I32(0x59B234), h = ONC_I32(0x59B238);
    if (!(w == g_w && h == g_h)) {
        ONC_I32(0x59B234) = g_w;
        ONC_I32(0x59B238) = g_h;
        onc_log("video mode %dx%d -> %dx%d", w, h, g_w, g_h);
    }
    return ((int(__cdecl *)(int))g_apply_orig)(menu);
}

/* 0x42CDC8: the join request takes the screen size from 0x43BA30 and sends it to the server, which keeps half of it
   as the player's view. A width over 1024 there leaves the player without his own unit (black screen, then a crash
   at 0x45A561), so the server is told at most 1024x768. */
static void __cdecl join_screen_size(int *w, int *h, int *bpp) {
    ((void(__cdecl *)(int *, int *, int *))0x43BA30)(w, h, bpp);
    /* ui_scale: the world is drawn bigger than the game's screen; the server must send what it shows */
    extern int onc_uisplit_size(int *w, int *h);
    onc_uisplit_size(w, h);
    if (g_join_clamp && w && *w > 1024) *w = 1024;
    if (g_join_clamp && h && *h > 768) *h = 768;
    onc_log("join: screen size sent to the server %dx%d", w ? *w : 0, h ? *h : 0);
}

/* 0x45A500(drawable, player drawable): a looping ambient sound's volume by its distance to the player. The world can
   be drawn before the player's own unit has arrived (wider screens see such sounds sooner): no player, no sound. */
static void *g_sound_orig;
static int g_sound_skips;
static int __cdecl ambient_sound(void *dr, void *player) {
    if (!player) {
        if (g_sound_skips++ < 3) onc_log("ambient sound before the player's unit exists: skipped");
        return 0;
    }
    if (g_sound_skips > 0) { onc_log("the player's unit is here (after %d skipped sounds)", g_sound_skips); g_sound_skips = -1000000; }
    return ((int(__cdecl *)(void *, void *))g_sound_orig)(dr, player);
}

void onc_resolution_init(void) {
    char s[64];
    onc_cfg_str("display", "resolution", "1920x1080", s, sizeof s);
    if (!s[0] || !_stricmp(s, "off") || !_stricmp(s, "1024x768")) return;
    if (!_stricmp(s, "desktop")) {
        DEVMODEA dm; memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
        if (!EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)) return;
        g_w = dm.dmPelsWidth; g_h = dm.dmPelsHeight;
    } else if (sscanf(s, "%dx%d", &g_w, &g_h) != 2 && sscanf(s, "%d %d", &g_w, &g_h) != 2) {
        onc_log("resolution: cannot read \"%s\" (use e.g. 1920x1080 or desktop)", s);
        return;
    }
    g_w &= ~7; /* the renderer works in 8-pixel steps in places: keep the width a multiple of 8 */
    if (g_w < 640 || g_h < 480 || g_w > 7680 || g_h > 4320) { onc_log("resolution %dx%d out of range", g_w, g_h); g_w = 0; return; }
    /* UI scale: the game (and its GUI) runs at screen / scale, the world is drawn at the full screen size
       (uisplit.c) */
    extern int onc_uisplit_setup(int w, int h, int *gw, int *gh);
    int gw, gh;
    if (onc_uisplit_setup(g_w, g_h, &gw, &gh)) { g_w = gw; g_h = gh; }
    onc_log("resolution: the 1024x768 slot is now %dx%d", g_w, g_h);

    int mw = g_w > 1024 ? g_w : 1024, mh = g_h > 768 ? g_h : 768;
    patch_imm("max w", 0x598F58, 1024, mw);
    patch_imm("max h", 0x598F5C, 768, mh);
    patch_imm("max w (0x437C1B)", 0x437C1C, 1024, mw);
    patch_imm("max h (0x437C16)", 0x437C17, 768, mh);
    patch_imm("max w (0x43AF8D)", 0x43AF8E, 1024, mw);
    patch_imm("max h (0x43AF88)", 0x43AF89, 768, mh);
    /* the mode table and the Options page */
    patch_imm("mode table w", 0x59B264, 1024, g_w);
    patch_imm("mode table h", 0x59B268, 768, g_h);
    patch_imm("options w", 0x4A9F7F, 1024, g_w);
    patch_imm("options h", 0x4A9F89, 768, g_h);
    patch_imm("options radio", 0x4A9DA6, 1024, g_w);
    patch_imm("options radio 2", 0x4AD07A, 1024, g_w);
    g_join_clamp = onc_cfg_int("display", "join_clamp", 0);
    onc_hook_call("join screen size", 0x42CDC8, join_screen_size, 0x43BA30);
    static const uint8_t snd[] = {0x83, 0xEC, 0x0C, 0x55, 0x56};
    g_sound_orig = onc_hook_detour("ambient sound", 0x45A500, ambient_sound, snd, sizeof snd);
    static const uint8_t pro[] = {0x8B, 0x44, 0x24, 0x04, 0x53, 0x56};
    g_apply_orig = onc_hook_detour("apply video mode", 0x43BA90, apply_mode, pro, sizeof pro);
}
