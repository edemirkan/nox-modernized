/* Nox Modernized settings (NoxModernized-Settings.exe): a small settings editor for nox-modernized.ini (or Winlator's
   nox-modernized-winlator.ini), plain Win32 with the standard controls (no runtime to install; runs under Wine /
   Winlator). It does not start the game: the .bat files do that.

   Pages: Display, World, Game, Gamepad, Buttons, Advanced. Every setting is a drop-down list; the line at the
   bottom explains whatever the pointer is over (or has the focus: touch screens have no hover).
   Buttons: per class, the button combo of each action (the game's binds_table.h is shared, so the two agree).
   "Detect" waits for the combo on the gamepad (XInput); "Choose" picks it from a list. Saved per class as
   action = combo[, combo]. */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <xinput.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../game/input/binds_table.h"

/* ---------------- settings ---------------- */
enum { P_DISPLAY, P_WORLD, P_GAME, P_PAD, P_BUTTONS, P_ADV, P_N };
static const wchar_t *k_pages[P_N] = {L"Display", L"World", L"Game", L"Gamepad", L"Buttons", L"Advanced"};

typedef struct {
    int page;
    const char *sec, *key, *def;
    const wchar_t *label;
    const char *opts; /* "value|label;value|label;..." (a label may be left out: the value is shown) */
    const wchar_t *help;
} setting;

#define ONOFF "1|On;0|Off"
#define LEVELS "0|Off;1|Low;2|Medium;3|High"
static const setting k_set[] = {
    /* Display */
    {P_DISPLAY, "display", "renderer", "d3d11", L"Renderer", "d3d11|Nox Modernized (Direct3D 11);gog|GOG's renderer",
     L"Nox Modernized's own renderer is needed for the UI scale, zoom, filters and the high frame rate."},
    {P_DISPLAY, "display", "mode", "borderless", L"Window", "borderless|Borderless full screen;window|Window",
     L"Borderless covers the whole monitor without changing the desktop resolution."},
    {P_DISPLAY, "display", "window_scale", "0", L"Window size", "0|Biggest that fits;1|640x480 x1;2|x2;3|x3;4|x4",
     L"Only for the window mode: the window's size as a multiple of 640x480."},
    {P_DISPLAY, "display", "resolution", "1920x1080", L"Resolution",
     "desktop|Desktop size;1280x720;1280x800;1366x768;1440x900;1600x900;1920x1080;1920x1200;2560x1440;3840x2160;1024x768|1024x768 (original)",
     L"The size the game draws its world at (menus stay 640x480). Smaller is faster: on Winlator try 1280x800 or 1152x720."},
    {P_DISPLAY, "display", "ui_scale", "1", L"UI scale", "" /* uiscale_opts: up to what the resolution allows */,
     L"Draws the HUD, inventory, book and in-game menus bigger; the world keeps the full resolution. The game needs 640x480 for them, so the list goes up to the resolution / 640x480 (1920x1080: 2.25x). Desktop size in the Winlator file: the size is not known here; the game takes the largest that fits."},
    {P_DISPLAY, "display", "filter", "sharp", L"Filter",
     "nearest|Nearest (raw pixels);sharp|Sharp;linear|Linear (soft);bicubic|Bicubic;lanczos|Lanczos (sharpest)",
     L"How the picture is scaled to the screen. Sharp keeps the pixels; bicubic and lanczos are smooth and detailed."},
    {P_DISPLAY, "display", "scale", "fit", L"Scaling", "fit|Fit (shape kept);integer|Whole multiples only;stretch|Stretch to fill",
     L"Fit fills the screen keeping the shape; whole multiples are crisp but may leave a border."},
    {P_DISPLAY, "display", "vsync", "1", L"VSync", ONOFF, L"Waits for the monitor's refresh: no tearing, a little more delay."},
    {P_DISPLAY, "display", "frame_rate", "30", L"Frame rate", "30|30 (original);60;90;120",
     L"Extra pictures drawn between the game's 30 ticks a second: smoother movement and camera. Animations stay as drawn."},
    {P_DISPLAY, "display", "fps", "0", L"Frame counter", "1|Shown at start;0|Hidden", L"The frames per second in the top right corner; F8 shows or hides it in the game."},
    /* World */
    {P_WORLD, "display", "zoom", "1", L"Zoom at start", "" /* zoom_opts: up to "Zoom up to" */,
     L"The world zoom a game starts with. In the game: numpad + / - / *, or hold R3 and push the right stick."},
    {P_WORLD, "display", "zoom_max", "2", L"Zoom up to", "" /* zoom_opts */, L"How far in the zoom goes."},
    {P_WORLD, "display", "camera_smooth", "1", L"Smooth camera", ONOFF, L"The view eases after the player instead of being locked to him."},
    {P_WORLD, "display", "camera_dead_zone", "0.15", L"Camera dead zone", "0|None;0.05|5%;0.1|10%;0.15|15%;0.2|20%;0.25|25%;0.3|30%",
     L"Smooth camera: how far the player can move from the centre before the view follows (part of the smaller screen side)."},
    {P_WORLD, "display", "camera_max_lag", "0.25", L"Camera max distance", "0.1|10%;0.15|15%;0.2|20%;0.25|25%;0.3|30%;0.35|35%;0.4|40%",
     L"Smooth camera: the farthest the player gets from the centre before the view keeps him in (part of the screen size)."},
    {P_WORLD, "display", "light_flicker", "gentle", L"Fire light flicker", "original|Original (throbbing);gentle|Gentle;off|Steady",
     L"Torches and fires: the original picks a new random brightness every frame."},
    {P_WORLD, "display", "map_background", "25", L"Map background", "0|Clear;10|10%;25|25%;40|40%;60|60%;80|80%;100|Black",
     L"How dark the screen gets behind the map."},
    {P_WORLD, "display", "sprite_edges", "0", L"Sprite edge smoothing", LEVELS, L"Blends the outlines of characters and objects with what is behind them. Costs speed."},
    {P_WORLD, "display", "sprite_soften", "0", L"Sprite softening", LEVELS, L"A light blur inside characters and objects."},
    {P_WORLD, "display", "wall_edges", "0", L"Wall edge smoothing", LEVELS, L"Blends the outlines of walls."},
    {P_WORLD, "display", "wall_soften", "0", L"Wall softening", LEVELS, L"A light blur inside walls."},
    {P_WORLD, "display", "floor_soften", "0", L"Floor softening", LEVELS, L"A light blur of the floor tiles."},
    /* Game */
    {P_GAME, "game", "carry_weight", "1", L"Carry weight limit", "1|x1 (original);1.25|x1.25;1.5|x1.5;2|x2;3|x3;5|x5;10|x10",
     L"How much you can carry before you are slowed down."},
    {P_GAME, "save", "rolling_autosave", "1", L"Rolling autosaves", ONOFF,
     L"Every autosave keeps the two before it (AUTOSAV1, AUTOSAV2 in the save and load lists)."},
    {P_GAME, "game", "fix_shield_load", "1", L"Keep force shield on load", ONOFF, L"Fixes the force shield vanishing about a second after loading a save."},
    {P_GAME, "game", "ally_spells", "1", L"Spells on allies", ONOFF, L"Lesser Heal, Haste, Shield and other spells that can be aimed go to the creature or player under the cursor when you aim with the mouse or the right stick; otherwise (and with nobody friendly under the cursor) on yourself, as in the original game."},
    {P_GAME, "display", "keep_screen_on", "1", L"Keep the screen on", ONOFF,
     L"No screen saver or monitor power saving while the game is in front (Windows does not count the gamepad as activity)."},
    {P_GAME, "inventory", "tabs", "1", L"Inventory tabs", ONOFF, L"Tabs above the inventory (L1 / R1 on the gamepad) that sort it by kind."},
    {P_GAME, "inventory", "keep_facing", "1", L"Keep facing in the inventory", ONOFF,
     L"The player keeps the direction he faced when the inventory opened (instead of turning towards it), so a dropped item lands in front of him."},
    {P_GAME, "inventory", "compare", "1", L"Item comparison", ONOFF, L"Shows the item under the cursor next to what you are wearing."},
    /* Gamepad */
    {P_PAD, "pad", "enabled", "1", L"Gamepad", ONOFF, L"Play with an XInput gamepad (Xbox layout)."},
    {P_PAD, "pad", "dead_zone_percent", "25", L"Stick dead zone", "10|10%;15|15%;20|20%;25|25%;30|30%;40|40%", L"How far a stick must move before it counts."},
    {P_PAD, "pad", "walk_below_percent", "60", L"Walk below", "40|40%;50|50%;60|60%;70|70%;80|80%", L"Pushing the left stick less than this walks, more runs."},
    {P_PAD, "pad", "cursor_speed", "14", L"Cursor speed", "8|Slow;11;14|Normal;18;24|Fast", L"The right stick's cursor speed."},
    {P_PAD, "pad", "walk_ring_pixels", "60", L"Walk distance", "40|Short;60|Normal;90|Long", L"How far ahead of the player the cursor goes while walking."},
    {P_PAD, "pad", "run_ring_percent", "30", L"Run distance", "20|Short;30|Normal;40|Long", L"How far ahead the cursor goes while running (part of the screen height)."},
    {P_PAD, "pad", "cursor_start_percent", "50", L"Right stick cursor: start push", "0|Any;25|Light;50|Normal;65;80|Firm", L"In a game, how far the right stick must be pushed (for a moment) to switch to the cursor; the cursor then starts on the player. Keeps a tilt while pressing R3 from switching."},
    {P_PAD, "pad", "r3_click", "1", L"R3 clicks with the cursor", ONOFF, L"After the right stick has moved the cursor (until the left stick is used again), R3 is a left click where the cursor is instead of its own action (L1 + R3 keeps its action)."},
    {P_PAD, "pad", "zoom_hold_ms", "250", L"R3 zoom: hold first", "0|No wait;150|0.15 s;250|0.25 s;400|0.4 s;600|0.6 s", L"Hold R3 this long before the right stick zooms (up / down), so a quick R3 press never zooms by accident."},
    {P_PAD, "pad", "zoom_dead_zone_percent", "50", L"R3 zoom: stick push", "25|Light;35;50|Normal;65;80|Firm", L"How far the right stick must be pushed up / down (with R3 held) before zooming starts."},
    {P_PAD, "pad", "hide_cursor", "1", L"Hide the cursor", ONOFF, L"Hides the mouse cursor while the left stick plays; the right stick shows it again."},
    {P_PAD, "pad", "lock_on", "1", L"Lock-on", ONOFF, L"A and the spell buttons aim at the closest visible enemy in front of the player."},
    {P_PAD, "pad", "lock_on_range", "400", L"Lock-on range", "250|Short;400|Normal;600|Long;800|Very long", L"How far away an enemy can be locked on."},
    {P_PAD, "pad", "lock_on_cone_degrees", "30", L"Lock-on cone", "15|Narrow;30|Normal;45|Wide;60|Very wide", L"How far to the side of where the player faces lock-on looks."},
    {P_PAD, "pad", "aim_cone", "1", L"Aim cone", "0|Off;1|While aiming (R1);2|Always, brighter with R1", L"Brightens the floor in the lock-on cone in front of the player: only while the aim button (R1) is held, or always while the left stick plays."},
    {P_PAD, "pad", "aim_cone_strength", "20", L"Aim cone brightness", "10|Faint;15;20|Normal;30;40|Bright", L"How much the aim cone brightens the floor (with R1; \"always\" shows half of it without R1)."},
    {P_PAD, "pad", "button_labels", "1", L"Pad buttons on the HUD", ONOFF, L"While the pad is in use the HUD shows its buttons instead of keys."},
    {P_PAD, "pad", "hints", "1", L"Control hints", ONOFF, L"Short hints for the gamepad (what R3 does, the inventory buttons)."},
    /* Advanced */
    {P_ADV, "display", "wide_light", "1", L"Big light tables", ONOFF, L"Needed above 1024x768: light and line of sight for big screens."},
    {P_ADV, "display", "join_clamp", "0", L"Tell the server 1024x768", "0|Off (real size);1|On",
     L"For servers that refuse big screens; off shows the whole world at big resolutions."},
    {P_ADV, "display", "cursor_layer", "1", L"Cursor at full frame rate", ONOFF, L"With the high frame rate the mouse cursor is drawn at every picture where the mouse is now."},
    {P_ADV, "display", "gpu_convert", "1", L"GPU colour conversion", ONOFF, L"Converts the game's 16-bit pixels on the graphics card (faster). Off: on the processor."},
    {P_ADV, "display", "threads", "0", L"Helper threads", "0|Automatic;-1|None (main thread only);1;2;3;4;7", L"Threads for the smoothing and the world copy into the HUD layer."},
    {P_ADV, "display", "camera_ease_ms", "120", L"Camera ease", "60|Quick;120|Normal;200|Slow;300|Very slow", L"How slowly the smooth camera follows."},
    {P_ADV, "display", "zoom_step", "0.1", L"Zoom step", "0.1|0.1;0.25|0.25;0.5|0.5", L"How much numpad + / - zooms."},
    {P_ADV, "display", "zoom_ease_ms", "100", L"Zoom ease", "0|None;100|Normal;250|Slow", L"How smoothly a zoom step happens."},
    {P_ADV, "display", "screenshot_key", "44", L"Screenshot key", "44|Print Screen;123|F12;0|Off", L"PNG screenshots go to the Screenshots folder next to Game.exe."},
    {P_ADV, "display", "fps_key", "119", L"Frame counter key", "119|F8;120|F9;121|F10", L"The key that shows or hides the frame counter."},
};
#define NSET (int)(sizeof k_set / sizeof k_set[0])

/* ---------------- state ---------------- */
static HINSTANCE g_inst;
static HWND g_wnd, g_tab, g_help, g_file, g_save;
static HFONT g_font;
static int g_fh, g_page;
static char g_dir[MAX_PATH], g_ini[MAX_PATH];
static int g_dirty;
static int g_top;          /* the banner strip's height at the top of the window */
static HBITMAP g_banner;    /* resource 2: the banner strip (1344 x 120) */
typedef struct { HWND w; int page; const wchar_t *help; } ctl;
static ctl g_ctl[256]; static int g_nctl;
static HWND g_combo[NSET];

/* buttons page */
static int g_shared[S_N];   /* action per combo for every class */
static int g_map[3][S_N];   /* action per class and combo */
static int g_view;          /* 0 shared, 1..3 warrior / wizard / conjurer */
static HWND g_cls, g_list, g_bset, g_badd, g_bclear, g_bdef, g_choose, g_dstat;
static int g_rows[NACT]; static int g_nrows;

static void set_dirty(int d) {
    g_dirty = d;
    wchar_t t[128];
    swprintf(t, 128, L"Nox Modernized settings%s", d ? L" *" : L"");
    SetWindowTextW(g_wnd, t);
}

/* ---------------- the settings file ---------------- */
static int file_exists(const char *p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }
static void ini_for(char *out, const char *name) { snprintf(out, MAX_PATH, "%s%s", g_dir, name); }
static const char *k_files[2] = {"nox-modernized.ini", "nox-modernized-winlator.ini"};
static int g_fileidx;
/* reads come from the chosen file, or the PC one while the chosen one does not exist yet */
static const char *read_path(void) {
    static char pc[MAX_PATH];
    if (file_exists(g_ini)) return g_ini;
    ini_for(pc, k_files[0]);
    return pc;
}
static void cfg_get(const char *sec, const char *key, const char *def, char *out, int n) {
    GetPrivateProfileStringA(sec, key, def, out, n, read_path());
    /* trim spaces */
    char *s = out; while (*s == ' ' || *s == '\t') s++;
    memmove(out, s, strlen(s) + 1);
    for (int i = (int)strlen(out) - 1; i >= 0 && (out[i] == ' ' || out[i] == '\t'); i--) out[i] = 0;
}

/* ---------------- lists that depend on other settings ----------------
   UI scale: 0.1 steps up to what the resolution allows (and that largest one, e.g. 2.25 at 1920x1080); the game works
   it out the same way (uisplit.c, onc_uiscale_max) and lowers a bigger value to it. Zoom at start: 0.1 steps up to
   "Zoom up to"; Zoom up to: 0.1 steps up to 4. */
static char g_dyn[NSET][1024];
static const char *opts_of(int i) { return g_dyn[i][0] ? g_dyn[i] : k_set[i].opts; }
static int set_index(const char *key) {
    for (int i = 0; i < NSET; i++) if (!strcmp(k_set[i].key, key)) return i;
    return -1;
}
/* 1.0 .. max (x 100) in 0.1 steps, max added when it falls between two; first and last get these labels */
static void steps_opts(char *o, int n, int max, const char *first, const char *last) {
    int k = snprintf(o, n, "1|1x%s", first);
    for (int v = 110; v <= max && k < n - 40; v += 10)
        k += snprintf(o + k, n - k, ";%d.%d|%d.%dx%s", v / 100, v / 10 % 10, v / 100, v / 10 % 10, v == max ? last : "");
    if (max % 10 && max > 100) snprintf(o + k, n - k, ";%d.%02d|%d.%02dx%s", max / 100, max % 100, max / 100, max % 100, last);
}

/* option i of a setting: value and label */
static int opt_get(const char *opts, int i, char *val, int nv, wchar_t *lab, int nl) {
    const char *p = opts;
    for (int k = 0; k < i; k++) { p = strchr(p, ';'); if (!p) return 0; p++; }
    const char *e = strchr(p, ';'); if (!e) e = p + strlen(p);
    const char *bar = memchr(p, '|', e - p);
    int vl = (int)((bar ? bar : e) - p); if (vl >= nv) vl = nv - 1;
    memcpy(val, p, vl); val[vl] = 0;
    char lb[128]; const char *ls = bar ? bar + 1 : p; int ll = (int)(e - ls); if (ll > 127) ll = 127;
    memcpy(lb, ls, ll); lb[ll] = 0;
    MultiByteToWideChar(CP_UTF8, 0, lb, -1, lab, nl);
    return 1;
}
static int same_value(const char *a, const char *b) {
    char *ea, *eb;
    double x = strtod(a, &ea), y = strtod(b, &eb);
    if (*a && *b && !*ea && !*eb) return x - y < 1e-6 && y - x < 1e-6;
    return !_stricmp(a, b);
}

/* fill setting i's list and select cur; a number over a numeric list's last value selects that last one
   (returns 1 then), another value not in the list is kept as it is (custom) */
static int fill_combo(int i, const char *cur) {
    HWND c = g_combo[i];
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    char val[64], last[64] = ""; wchar_t lab[128];
    int sel = -1, n = 0, lowered = 0;
    for (; opt_get(opts_of(i), n, val, sizeof val, lab, 128); n++) {
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)lab);
        if (sel < 0 && same_value(cur, val)) sel = n;
        snprintf(last, sizeof last, "%s", val);
    }
    if (sel < 0 && g_dyn[i][0] && n > 0 && atof(cur) > atof(last)) { sel = n - 1; lowered = 1; }
    if (sel < 0) { /* a value not in the list: kept as it is */
        wchar_t w[96]; swprintf(w, 96, L"%hs (custom)", cur);
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)w);
        sel = n;
    }
    SendMessageW(c, CB_SETCURSEL, sel, 0);
    return lowered;
}
static void value_of(int i, char *out, int n);
/* the screen size the chosen resolution gives (as the game's resolution.c): 0 = not known here (Desktop size
   for the other system), -1 = no UI scale (1024x768 / off: the game keeps its own modes) */
static int res_size(int *w, int *h) {
    char r[64]; value_of(set_index("resolution"), r, sizeof r);
    if (!r[0] || !_stricmp(r, "off") || !_stricmp(r, "1024x768")) return -1;
    if (!_stricmp(r, "desktop")) {
        int wine = GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") != NULL;
        if (wine != (g_fileidx == 1)) return 0; /* the PC file under Winlator, or the Winlator file on the PC */
        DEVMODEA dm; memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
        if (!EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)) return 0;
        *w = dm.dmPelsWidth; *h = dm.dmPelsHeight;
    } else if (sscanf(r, "%dx%d", w, h) != 2) return 0;
    *w &= ~7;
    return *w >= 640 && *h >= 480 ? 1 : -1;
}
/* the largest UI scale x 100 (450 = 3840x2160's when the size is not known) */
static int uiscale_max(void) {
    int w, h, k = res_size(&w, &h);
    if (k < 0) return 100;
    if (k == 0) return 450;
    int a = w * 100 / 640, b = h * 100 / 480, m = a < b ? a : b;
    return m < 100 ? 100 : m;
}
/* rebuild the UI scale and Zoom at start lists, keeping their values (lowered to the new largest if needed) */
static void update_lists(int from_file) {
    static const char *keys[2] = {"ui_scale", "zoom"};
    for (int k = 0; k < 2; k++) {
        int i = set_index(keys[k]);
        char cur[64];
        if (from_file) cfg_get(k_set[i].sec, k_set[i].key, k_set[i].def, cur, sizeof cur);
        else value_of(i, cur, sizeof cur);
        if (k == 0) steps_opts(g_dyn[i], sizeof g_dyn[i], uiscale_max(), " (off)", " (largest)");
        else {
            char zm[64]; value_of(set_index("zoom_max"), zm, sizeof zm);
            int m = (int)(atof(zm) * 100 + 0.5); if (m < 100) m = 100; if (m > 400) m = 400;
            steps_opts(g_dyn[i], sizeof g_dyn[i], m, " (none)", "");
        }
        if (fill_combo(i, cur) && !from_file) set_dirty(1);
    }
}

static void load_settings(void) {
    steps_opts(g_dyn[set_index("zoom_max")], sizeof g_dyn[0], 400, " (zoom off)", "");
    for (int i = 0; i < NSET; i++) {
        char cur[64];
        cfg_get(k_set[i].sec, k_set[i].key, k_set[i].def, cur, sizeof cur);
        if (i != set_index("ui_scale") && i != set_index("zoom")) fill_combo(i, cur); /* those two: update_lists */
    }
    update_lists(1);
}
static void value_of(int i, char *out, int n) {
    int sel = (int)SendMessageW(g_combo[i], CB_GETCURSEL, 0, 0);
    char val[64]; wchar_t lab[128];
    if (sel >= 0 && opt_get(opts_of(i), sel, val, sizeof val, lab, 128)) { snprintf(out, n, "%s", val); return; }
    cfg_get(k_set[i].sec, k_set[i].key, k_set[i].def, out, n); /* the custom value */
}

/* ---------------- button combos ---------------- */
static int action_by_name(const char *n) {
    for (int i = 0; i < NACT; i++) if (!_stricmp(k_actions[i].name, n)) return i;
    return -1;
}
static int view_has(int v, int a) {
    if (a <= 0) return 0;
    if (v == 0) return k_actions[a].shared;
    return !k_actions[a].shared && class_has(v - 1, k_actions[a].name);
}
static int *view_map(int v) { return v == 0 ? g_shared : g_map[v - 1]; }
/* one action's combos into map (the defaults when not in the file), as the game reads them (binds.c) */
static void read_action(int *map, const char *sec, int cl, int a) {
    char v[128];
    cfg_get(sec, k_actions[a].name, "\x01", v, sizeof v);
    if (v[0] == 1) { const char *c1, *c2; default_combos(cl, k_actions[a].name, &c1, &c2); snprintf(v, sizeof v, "%s %s", c1, c2); }
    for (char *t = strtok(v, ", \t"); t; t = strtok(NULL, ", \t")) {
        int c = combo_by_name(t);
        if (c >= 0 && !map[c]) map[c] = a;
    }
}
static void view_defaults(int v) {
    int *map = view_map(v);
    memset(map, 0, sizeof(int) * S_N);
    for (int a = 1; a < NACT; a++) {
        if (!view_has(v, a)) continue;
        const char *c1, *c2;
        default_combos(v - 1, k_actions[a].name, &c1, &c2);
        int k1 = combo_by_name(c1), k2 = combo_by_name(c2);
        if (k1 >= 0) map[k1] = a;
        if (k2 >= 0) map[k2] = a;
    }
}
/* a combo the shared actions hold is not a class's (one combo, one thing); returns how many were taken */
static int drop_class_overlaps(void) {
    int n = 0;
    for (int cl = 0; cl < 3; cl++) for (int c = 0; c < S_N; c++) if (g_shared[c] && g_map[cl][c]) { g_map[cl][c] = 0; n++; }
    return n;
}
static void load_binds(void) {
    memset(g_shared, 0, sizeof g_shared);
    for (int a = 1; a < NACT; a++) if (k_actions[a].shared) read_action(g_shared, "shared", -1, a);
    char w[8];
    cfg_get("shared", "wheel", "\x01", w, sizeof w);
    if (w[0] == 1) { char r[8]; cfg_get("pad", "radial_menu", "1", r, sizeof r); if (atoi(r) == 0) for (int c = 0; c < S_N; c++) if (k_actions[g_shared[c]].kind == K_WHEEL) g_shared[c] = 0; }
    for (int cl = 0; cl < 3; cl++) {
        int *map = g_map[cl];
        memset(map, 0, sizeof(int) * S_N);
        for (int a = 1; a < NACT; a++) if (view_has(cl + 1, a)) read_action(map, k_class_names[cl], cl, a);
        for (int c = 0; c < S_N; c++) { /* the older form: combo = action */
            char key[24], val[32];
            combo_name(c, key, sizeof key);
            cfg_get(k_class_names[cl], key, "", val, sizeof val);
            int a = val[0] ? action_by_name(val) : -1;
            if (a > 0 && view_has(cl + 1, a)) map[c] = a;
        }
    }
    drop_class_overlaps();
}
static void combo_text(int c, wchar_t *out, int n) { combo_label(c, 1, out, n); }
static void combos_text(int v, int a, wchar_t *out, int n) {
    int *map = view_map(v);
    out[0] = 0;
    for (int c = 0; c < S_N; c++)
        if (map[c] == a) {
            wchar_t l[32]; combo_text(c, l, 32);
            if (out[0]) wcsncat(out, L",  ", n - wcslen(out) - 1);
            wcsncat(out, l, n - wcslen(out) - 1);
        }
    if (!out[0]) wcsncpy(out, L"-", n);
}
static void title_of(int a, wchar_t *out, int n) {
    wcsncpy(out, k_actions[a].title, n);
    if (out[0]) out[0] = towupper(out[0]);
}
static void fill_list(void) {
    int sel = (int)SendMessageW(g_list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    SendMessageW(g_list, LVM_DELETEALLITEMS, 0, 0);
    g_nrows = 0;
    for (int a = 1; a < NACT; a++) {
        if (!view_has(g_view, a)) continue;
        wchar_t t[64], c[160];
        title_of(a, t, 64);
        combos_text(g_view, a, c, 160);
        LVITEMW it; memset(&it, 0, sizeof it);
        it.mask = LVIF_TEXT; it.iItem = g_nrows; it.pszText = t;
        SendMessageW(g_list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        it.iSubItem = 1; it.pszText = c;
        SendMessageW(g_list, LVM_SETITEMTEXTW, g_nrows, (LPARAM)&it);
        g_rows[g_nrows++] = a;
    }
    if (sel < 0) sel = 0;
    if (sel >= g_nrows) sel = g_nrows - 1;
    LVITEMW st; memset(&st, 0, sizeof st);
    st.stateMask = LVIS_SELECTED | LVIS_FOCUSED; st.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(g_list, LVM_SETITEMSTATE, sel, (LPARAM)&st);
}
static int sel_action(void) {
    int sel = (int)SendMessageW(g_list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    return sel >= 0 && sel < g_nrows ? g_rows[sel] : -1;
}
static void dstat(const wchar_t *fmt, ...) {
    wchar_t b[300]; va_list ap; va_start(ap, fmt); vswprintf(b, 300, fmt, ap); va_end(ap);
    RECT r; GetWindowRect(g_dstat, &r); MapWindowPoints(NULL, g_tab, (POINT *)&r, 2);
    InvalidateRect(g_tab, &r, TRUE); UpdateWindow(g_tab); /* see-through: the old text goes first */
    SetWindowTextW(g_dstat, b);
}
static const wchar_t *k_view_names[4] = {L"Shared", L"Warrior", L"Wizard", L"Conjurer"};
/* give combo c to action a in the view shown (replace: the action's other combos go). If another action has the
   combo (in this class, among the shared actions, or for a class when it is a shared action), ask first. */
static void assign(int a, int c, int replace) {
    wchar_t ta[64], tc[32], who[400] = L"", t[64];
    title_of(a, ta, 64);
    combo_text(c, tc, 32);
    int n = 0;
    if (g_view == 0) {
        if (g_shared[c] && g_shared[c] != a) { title_of(g_shared[c], t, 64); swprintf(who + wcslen(who), 400 - wcslen(who), L"\n  %ls (shared)", t); n++; }
        for (int cl = 0; cl < 3; cl++)
            if (g_map[cl][c]) { title_of(g_map[cl][c], t, 64); swprintf(who + wcslen(who), 400 - wcslen(who), L"\n  %ls (%ls)", t, k_view_names[cl + 1]); n++; }
    } else {
        int cl = g_view - 1;
        if (g_shared[c]) { title_of(g_shared[c], t, 64); swprintf(who + wcslen(who), 400 - wcslen(who), L"\n  %ls (shared: every class loses it)", t); n++; }
        else if (g_map[cl][c] && g_map[cl][c] != a) { title_of(g_map[cl][c], t, 64); swprintf(who + wcslen(who), 400 - wcslen(who), L"\n  %ls", t); n++; }
    }
    if (n) {
        wchar_t m[700];
        swprintf(m, 700, L"%ls is already used by:%ls\n\nGive it to %ls%ls instead?", tc, who, ta, g_view == 0 ? L" (for every class)" : L"");
        if (MessageBoxW(g_wnd, m, L"Button conflict", MB_YESNO | MB_ICONWARNING) != IDYES) { dstat(L"Nothing changed."); return; }
    }
    int *map = view_map(g_view);
    if (replace) for (int k = 0; k < S_N; k++) if (map[k] == a) map[k] = 0;
    if (g_view == 0) for (int cl = 0; cl < 3; cl++) g_map[cl][c] = 0;
    else g_shared[c] = 0;
    map[c] = a;
    dstat(n ? L"%ls: %ls (moved)" : L"%ls: %ls", ta, tc);
    fill_list();
    set_dirty(1);
}

/* ---------------- gamepad combo detection (XInput) ---------------- */
typedef DWORD (WINAPI *xget_t)(DWORD, XINPUT_STATE *);
static xget_t g_xget;
static int g_det;            /* 0 off, 1 set, 2 add */
static int g_det_ready;      /* all buttons were up once */
static DWORD g_det_t0;
#define DET_TIMER 2
static void det_stop(void) {
    g_det = 0;
    KillTimer(g_wnd, DET_TIMER);
    SetWindowTextW(g_bset, L"Detect (set)");
    SetWindowTextW(g_badd, L"Detect (add)");
}
static DWORD pad_bits(void) {
    for (DWORD i = 0; i < 4; i++) {
        XINPUT_STATE st;
        if (g_xget && g_xget(i, &st) == ERROR_SUCCESS) {
            DWORD b = st.Gamepad.wButtons;
            if (st.Gamepad.bLeftTrigger > 100) b |= 0x10000;
            if (st.Gamepad.bRightTrigger > 100) b |= 0x20000;
            if (b) return b;
        }
    }
    return 0;
}
static int any_pad(void) {
    for (DWORD i = 0; i < 4; i++) { XINPUT_STATE st; if (g_xget && g_xget(i, &st) == ERROR_SUCCESS) return 1; }
    return 0;
}
/* the combo of the buttons down (-1: none yet; -2: Select + Start, the settings menu) */
static int combo_of(DWORD b) {
    static const struct { DWORD bit; int c; } k[] = {
        {XINPUT_GAMEPAD_A, C_A}, {XINPUT_GAMEPAD_B, C_B}, {XINPUT_GAMEPAD_X, C_X}, {XINPUT_GAMEPAD_Y, C_Y},
        {0x10000, C_LT}, {0x20000, C_RT}, {XINPUT_GAMEPAD_RIGHT_SHOULDER, C_R1},
        {XINPUT_GAMEPAD_LEFT_THUMB, C_L3}, {XINPUT_GAMEPAD_RIGHT_THUMB, C_R3},
        {XINPUT_GAMEPAD_DPAD_UP, C_DU}, {XINPUT_GAMEPAD_DPAD_DOWN, C_DD}, {XINPUT_GAMEPAD_DPAD_LEFT, C_DL},
        {XINPUT_GAMEPAD_DPAD_RIGHT, C_DR}, {XINPUT_GAMEPAD_BACK, C_SEL}, {XINPUT_GAMEPAD_START, C_START},
    };
    if ((b & (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START)) == (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START)) return -2;
    int lb = (b & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++) if (b & k[i].bit) return lb ? S_L1(k[i].c) : k[i].c;
    return -1; /* nothing, or L1 alone (held for an L1 combo) */
}
static void det_tick(void) {
    DWORD b = pad_bits();
    if (GetTickCount() - g_det_t0 > 10000) { det_stop(); dstat(L"No button pressed: nothing changed."); return; }
    if (!g_det_ready) { if (!b) g_det_ready = 1; return; }
    int s = combo_of(b);
    if (s == -1) return;
    int a = sel_action(), mode = g_det;
    det_stop();
    if (s == -2) { dstat(L"Select + Start together open the settings menu: nothing changed."); return; }
    if (a > 0) assign(a, s, mode == 1);
}
static void det_start(int mode) {
    if (g_det) { det_stop(); dstat(L"Cancelled."); return; }
    int a = sel_action();
    if (a <= 0) return;
    if (!g_xget) { dstat(L"No XInput here: use Choose instead."); return; }
    if (!any_pad()) { dstat(L"No gamepad found: connect one, or use Choose."); return; }
    g_det = mode; g_det_ready = 0; g_det_t0 = GetTickCount();
    wchar_t t[64]; title_of(a, t, 64);
    dstat(L"%ls: press the button (hold L1 first for an L1 combo)... (10 s)", t);
    SetWindowTextW(mode == 1 ? g_bset : g_badd, L"Cancel");
    SetTimer(g_wnd, DET_TIMER, 20, NULL);
}

/* ---------------- load / save ---------------- */
static void load_all(void) {
    ini_for(g_ini, k_files[g_fileidx]);
    load_settings();
    load_binds();
    fill_list();
    set_dirty(0);
    wchar_t m[200];
    if (file_exists(g_ini)) swprintf(m, 200, L"Editing %hs.", k_files[g_fileidx]);
    else swprintf(m, 200, L"%hs does not exist yet: showing %hs; Save creates it.", k_files[g_fileidx], k_files[0]);
    SetWindowTextW(g_help, m);
}
static int save_all(void) {
    {
        static const char *need[] = {"attack", "interact", "inventory", "menu"};
        wchar_t miss[200] = L"";
        for (unsigned i = 0; i < 4; i++) {
            int a = action_by_name(need[i]), has = 0;
            for (int c = 0; c < S_N; c++) if (g_shared[c] == a) has = 1;
            if (!has) { wchar_t t[64]; title_of(a, t, 64); swprintf(miss + wcslen(miss), 200 - wcslen(miss), L"\n  %ls", t); }
        }
        if (miss[0]) {
            wchar_t m[300];
            swprintf(m, 300, L"These have no button on the gamepad:%ls\n\nSave anyway?", miss);
            if (MessageBoxW(g_wnd, m, L"Nox Modernized settings", MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
        }
    }
    if (!file_exists(g_ini)) {
        char pc[MAX_PATH]; ini_for(pc, k_files[0]);
        if (file_exists(pc)) CopyFileA(pc, g_ini, TRUE);
    }
    int ok = 1;
    for (int i = 0; i < NSET; i++) {
        char v[64]; value_of(i, v, sizeof v);
        ok &= WritePrivateProfileStringA(k_set[i].sec, k_set[i].key, v, g_ini) != 0;
    }
    for (int v = 0; v < 4; v++) {
        const char *sec = v == 0 ? "shared" : k_class_names[v - 1];
        int *map = view_map(v);
        for (int c = 0; c < S_N; c++) { char k[24]; combo_name(c, k, sizeof k); WritePrivateProfileStringA(sec, k, NULL, g_ini); } /* the older form */
        for (int a = 1; a < NACT; a++) {
            if (!view_has(v, a)) { if (v) WritePrivateProfileStringA(sec, k_actions[a].name, NULL, g_ini); continue; }
            char val[160] = "";
            for (int c = 0; c < S_N; c++)
                if (map[c] == a) { char k[24]; combo_name(c, k, sizeof k); if (val[0]) strcat(val, ", "); strcat(val, k); }
            ok &= WritePrivateProfileStringA(sec, k_actions[a].name, val[0] ? val : "none", g_ini) != 0;
        }
    }
    WritePrivateProfileStringA("pad", "radial_menu", NULL, g_ini); /* the wheel is a shared action now */
    WritePrivateProfileStringA(NULL, NULL, NULL, g_ini); /* flush */
    wchar_t m[200];
    swprintf(m, 200, ok ? L"Saved to %hs. The game reads it when it starts." : L"Could not write %hs (read-only?).", k_files[g_fileidx]);
    SetWindowTextW(g_help, m);
    if (ok) set_dirty(0);
    return ok;
}

/* ---------------- layout ---------------- */
static HWND mk(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id, int page, const wchar_t *help) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | style, x, y, w, h, g_wnd, (HMENU)(INT_PTR)id, g_inst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, FALSE);
    if (g_nctl < 256) { g_ctl[g_nctl].w = c; g_ctl[g_nctl].page = page; g_ctl[g_nctl].help = help; g_nctl++; }
    return c;
}
/* a label on a page: drawn see-through over the tab's background */
static HWND see_through(HWND w) { SetWindowLongPtrW(w, GWLP_USERDATA, 1); return w; }
static void show_page(int p) {
    g_page = p;
    if (g_det) det_stop();
    for (int i = 0; i < g_nctl; i++) if (g_ctl[i].page >= 0) ShowWindow(g_ctl[i].w, g_ctl[i].page == p ? SW_SHOW : SW_HIDE);
}
static const wchar_t *help_of(HWND w) {
    for (int k = 0; w && k < 3; k++, w = GetParent(w))
        for (int i = 0; i < g_nctl; i++) if (g_ctl[i].w == w) return g_ctl[i].help;
    return NULL;
}

enum { ID_FILE = 100, ID_SAVE, ID_CLOSE, ID_NOXDIR, ID_PLAY, ID_CLASS, ID_SET, ID_ADD, ID_CLEAR, ID_DEF, ID_CHOOSE, ID_LIST, ID_SETTING = 1000 };

static void build(int cw, int ch) {
    int m = g_fh, rh = g_fh * 2 + g_fh / 2, ctlh = g_fh * 2 - 2;
    /* top: the file */
    mk(L"STATIC", L"Settings file:", SS_LEFT | WS_VISIBLE, m, g_top + m + 4, g_fh * 7, g_fh + 4, 0, -1, NULL);
    g_file = mk(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_VISIBLE, m + g_fh * 7, g_top + m, g_fh * 26, g_fh * 10, ID_FILE, -1,
                L"Which file to edit: Launch.bat (PC) uses nox-modernized.ini, Launch-Winlator.bat uses nox-modernized-winlator.ini.");
    SendMessageW(g_file, CB_ADDSTRING, 0, (LPARAM)L"nox-modernized.ini (PC)");
    SendMessageW(g_file, CB_ADDSTRING, 0, (LPARAM)L"nox-modernized-winlator.ini (Winlator)");
    mk(L"BUTTON", L"Nox folder...", BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, cw - m - g_fh * 9, g_top + m - 2, g_fh * 9, ctlh + 4, ID_NOXDIR, -1,
       L"Your Nox (GOG) folder: the game's data and saves. Game.exe is copied from it next to Nox Modernized, and the .bat of this system gets the folder.");
    /* tabs */
    int ty = g_top + m + ctlh + m / 2;
    int helph = g_fh * 3, by = ch - m - ctlh, hy = by - m / 2 - helph;
    g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, m, ty, cw - 2 * m, hy - ty - m / 2, g_wnd, (HMENU)50, g_inst, NULL);
    SendMessageW(g_tab, WM_SETFONT, (WPARAM)g_font, FALSE);
    for (int p = 0; p < P_N; p++) { TCITEMW ti; ti.mask = TCIF_TEXT; ti.pszText = (LPWSTR)k_pages[p]; SendMessageW(g_tab, TCM_INSERTITEMW, p, (LPARAM)&ti); }
    RECT r = {m, ty, cw - m, hy - m / 2};
    SendMessageW(g_tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&r);
    int px = r.left + m / 2, py = r.top + m / 2, pw = r.right - r.left - m, ph = r.bottom - r.top - m;
    /* settings: two columns of label + list */
    int colw = (pw - m) / 2, labw = colw * 46 / 100, maxrows = ph / rh;
    int cnt[P_N] = {0}, tot[P_N] = {0};
    for (int i = 0; i < NSET; i++) tot[k_set[i].page]++;
    for (int i = 0; i < NSET; i++) {
        int p = k_set[i].page, k = cnt[p]++;
        int rows = (tot[p] + 1) / 2; if (rows > maxrows) rows = maxrows; if (rows < 1) rows = 1; /* balanced columns */
        int col = k / rows, row = k % rows;
        int x = px + col * (colw + m), y = py + row * rh;
        HWND lab = mk(L"STATIC", k_set[i].label, SS_LEFT | SS_NOTIFY, x, y + 4, labw - 4, g_fh + 6, 0, p, k_set[i].help);
        see_through(lab);
        g_combo[i] = mk(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x + labw, y, colw - labw, g_fh * 14, ID_SETTING + i, p, k_set[i].help);
    }
    /* buttons page */
    int P = P_BUTTONS;
    const wchar_t *hc = L"Shared: the actions every class has (attack, aim, talk, wheel, inventory, book, menu). Then each class's own. Pick an action, then Detect a combo on the gamepad or Choose one.";
    see_through(mk(L"STATIC", L"Class:", SS_LEFT, px, py + 4, g_fh * 4, g_fh + 6, 0, P, hc));
    g_cls = mk(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, px + g_fh * 4, py, g_fh * 10, g_fh * 8, ID_CLASS, P, hc);
    for (int v = 0; v < 4; v++) SendMessageW(g_cls, CB_ADDSTRING, 0, (LPARAM)k_view_names[v]);
    SendMessageW(g_cls, CB_SETCURSEL, 0, 0);
    int lw = pw * 58 / 100, ly = py + rh, lh = ph - rh - g_fh * 4;
    g_list = mk(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | WS_BORDER | WS_TABSTOP, px, ly, lw, lh, ID_LIST, P,
                L"The actions and their button combos. An action can have more than one combo (Detect (add)).");
    SendMessageW(g_list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    LVCOLUMNW col; memset(&col, 0, sizeof col);
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = L"Action"; col.cx = lw * 45 / 100; SendMessageW(g_list, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
    col.pszText = L"Buttons"; col.cx = lw - lw * 45 / 100 - GetSystemMetrics(SM_CXVSCROLL) - 6; SendMessageW(g_list, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
    int bx = px + lw + m, bw = pw - lw - m, bh = ctlh + 4, y = ly;
    g_bset = mk(L"BUTTON", L"Detect (set)", BS_PUSHBUTTON | WS_TABSTOP, bx, y, bw, bh, ID_SET, P,
                L"Press the combo on the gamepad: it becomes the action's only combo. Hold L1 first for an L1 combo.");
    y += bh + m / 2;
    g_badd = mk(L"BUTTON", L"Detect (add)", BS_PUSHBUTTON | WS_TABSTOP, bx, y, bw, bh, ID_ADD, P, L"Press a combo on the gamepad: added to the action's combos.");
    y += bh + m / 2;
    see_through(mk(L"STATIC", L"Choose:", SS_LEFT, bx, y + 4, g_fh * 4, g_fh + 6, 0, P, L"Without a gamepad: pick the combo from the list."));
    g_choose = mk(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, bx + g_fh * 4, y, bw - g_fh * 4, g_fh * 20, ID_CHOOSE, P,
                  L"Without a gamepad: pick the combo from the list (it becomes the action's only combo).");
    SendMessageW(g_choose, CB_ADDSTRING, 0, (LPARAM)L"(pick a combo)");
    for (int c = 0; c < S_N; c++) { wchar_t l[32]; combo_text(c, l, 32); SendMessageW(g_choose, CB_ADDSTRING, 0, (LPARAM)l); }
    SendMessageW(g_choose, CB_SETCURSEL, 0, 0);
    y += bh + m / 2;
    g_bclear = mk(L"BUTTON", L"Clear", BS_PUSHBUTTON | WS_TABSTOP, bx, y, bw, bh, ID_CLEAR, P, L"The action gets no button.");
    y += bh + m / 2;
    g_bdef = mk(L"BUTTON", L"Defaults", BS_PUSHBUTTON | WS_TABSTOP, bx, y, bw, bh, ID_DEF, P, L"Back to the original buttons of the shared actions or of this class.");
    g_dstat = see_through(mk(L"STATIC", L"", SS_LEFT, px, ly + lh + m / 2, pw, g_fh * 2 + 4, 0, P, NULL));
    see_through(mk(L"STATIC", L"Always: L1 is the modifier for the L1 combos;  Select + Start open the settings menu;  hold R3 + right stick to zoom "
                  L"(an action on R3 happens when R3 is released).  In menus and the inventory, A / B / Start / Select / D-pad work the menu.",
       SS_LEFT, px, ly + lh + m / 2 + g_fh * 2 + 4, pw, g_fh * 2 + 4, 0, P, NULL));
    /* bottom */
    g_help = mk(L"STATIC", L"", SS_LEFT | WS_VISIBLE | SS_SUNKEN, m, hy, cw - 2 * m, helph, 0, -1, NULL);
    int bw2 = g_fh * 7;
    mk(L"BUTTON", L"Play", BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, m, by, bw2 + g_fh * 2, ctlh + 4, ID_PLAY, -1,
       L"Starts the game (asks to save changes first): Launch.bat, or Launch-Winlator.bat in Winlator.");
    g_save = mk(L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, cw - m - 2 * bw2 - m, by, bw2, ctlh + 4, ID_SAVE, -1, L"Writes the settings file.");
    mk(L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, cw - m - bw2, by, bw2, ctlh + 4, ID_CLOSE, -1, NULL);
}

/* ---------------- the Nox folder (the GOG install) ----------------
   The game is started from this folder: Windows loads mss32.dll (ours) from the folder of the exe it starts, so the
   mod needs a copy of the game's Game.exe next to it; the game's data, saves and nox.cfg stay in the Nox folder,
   which the .bat files make the working directory (GAMEDIR). On the first start (no Game.exe here, or GAMEDIR not a
   Nox folder) the tool asks for the Nox folder, checks it (Game.exe + gamedata.bin, Game.exe's MD5 against the GOG
   build the mod was made for), copies Game.exe (and GOG's ddraw.dll as ddraw_gog.dll + dxcfg.ini, for the "GOG's
   renderer" setting) and writes GAMEDIR into the .bat of this system (Launch.bat, or Launch-Winlator.bat under
   Wine / Winlator). Nothing in the Nox folder is changed. */
#define GOG_EXE_MD5 "62cce922af5f6b5a02109d45859a738d" /* Game.exe, GOG, 2000-07-21 */
static wchar_t g_wdir[MAX_PATH]; /* this folder, with the backslash */
static int g_wine, g_launch_told;
static const wchar_t *bat_name(void) { return g_wine ? L"Launch-Winlator.bat" : L"Launch.bat"; }
static void here(wchar_t *out, const wchar_t *name) { swprintf(out, MAX_PATH, L"%ls%ls", g_wdir, name); }
static int wexists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
static int wfile_in(const wchar_t *dir, const wchar_t *name) {
    wchar_t p[MAX_PATH]; swprintf(p, MAX_PATH, L"%ls\\%ls", dir, name); return wexists(p);
}
static int is_nox_dir(const wchar_t *d) { return d[0] && wfile_in(d, L"Game.exe") && wfile_in(d, L"gamedata.bin"); }

static int md5_file(const wchar_t *path, char *hex) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    HCRYPTPROV prov = 0; HCRYPTHASH hash = 0; int ok = 0;
    if (CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) && CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) {
        BYTE buf[65536]; DWORD n;
        ok = 1;
        while (ReadFile(f, buf, sizeof buf, &n, NULL) && n) if (!CryptHashData(hash, buf, n, 0)) { ok = 0; break; }
        BYTE d[16]; DWORD dl = 16;
        if (ok && CryptGetHashParam(hash, HP_HASHVAL, d, &dl, 0)) for (int i = 0; i < 16; i++) sprintf(hex + 2 * i, "%02x", d[i]);
        else ok = 0;
    }
    if (hash) CryptDestroyHash(hash);
    if (prov) CryptReleaseContext(prov, 0);
    CloseHandle(f);
    return ok;
}

/* GAMEDIR in this system's .bat: read / write the "set GAMEDIR=" line (the .bat files are ANSI, CRLF) */
static int bat_read(char *buf, int n) {
    wchar_t p[MAX_PATH]; here(p, bat_name());
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    DWORD got = 0; ReadFile(f, buf, n - 1, &got, NULL); CloseHandle(f);
    buf[got] = 0;
    return (int)got;
}
static void gamedir_get(wchar_t *out) {
    char b[8192]; out[0] = 0;
    if (bat_read(b, sizeof b) < 0) return;
    char *s = strstr(b, "set GAMEDIR=");
    if (!s) return;
    s += 12;
    char v[MAX_PATH]; int k = 0;
    while (s[k] && s[k] != '\r' && s[k] != '\n' && k < MAX_PATH - 1) { v[k] = s[k]; k++; }
    v[k] = 0;
    MultiByteToWideChar(CP_ACP, 0, v, -1, out, MAX_PATH);
}
static int gamedir_set(const wchar_t *dir) {
    char b[8192], out[9000], d[MAX_PATH];
    if (bat_read(b, sizeof b) < 0) return 0;
    WideCharToMultiByte(CP_ACP, 0, dir, -1, d, MAX_PATH, NULL, NULL);
    char *s = strstr(b, "set GAMEDIR=");
    if (!s) return 0;
    char *e = s + 12; while (*e && *e != '\r' && *e != '\n') e++;
    snprintf(out, sizeof out, "%.*sset GAMEDIR=%s%s", (int)(s - b), b, d, e);
    wchar_t p[MAX_PATH]; here(p, bat_name());
    HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0; int ok = WriteFile(f, out, (DWORD)strlen(out), &w, NULL) && w == strlen(out);
    CloseHandle(f);
    return ok;
}

/* where GOG put Nox: GOG Galaxy / the GOG installer's registry entries, then the usual folders */
static int guess_nox(wchar_t *out) {
    static const wchar_t *keys[] = {L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", L"SOFTWARE\\GOG.com\\Games"};
    for (int k = 0; k < 2; k++) {
        HKEY root;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keys[k], 0, KEY_READ, &root) != ERROR_SUCCESS) continue;
        wchar_t sub[128];
        for (DWORD i = 0; RegEnumKeyW(root, i, sub, 128) == ERROR_SUCCESS; i++) {
            HKEY g; wchar_t name[128] = L"", path[MAX_PATH] = L""; DWORD t, n;
            if (RegOpenKeyExW(root, sub, 0, KEY_READ, &g) != ERROR_SUCCESS) continue;
            n = sizeof name; RegQueryValueExW(g, L"gameName", NULL, &t, (BYTE *)name, &n);
            n = sizeof path; RegQueryValueExW(g, L"path", NULL, &t, (BYTE *)path, &n);
            RegCloseKey(g);
            if (!_wcsicmp(name, L"Nox") && is_nox_dir(path)) { wcscpy(out, path); RegCloseKey(root); return 1; }
        }
        RegCloseKey(root);
    }
    static const wchar_t *dirs[] = {L"C:\\GOG Games\\Nox", L"C:\\Program Files (x86)\\GOG Galaxy\\Games\\Nox", L"C:\\Program Files (x86)\\GOG.com\\Nox",
                                    L"C:\\Program Files\\GOG Galaxy\\Games\\Nox", L"C:\\Games\\Nox", L"D:\\GOG Games\\Nox", L"D:\\Games\\Nox", L"D:\\Nox\\Nox", L"D:\\Nox"};
    for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; i++) if (is_nox_dir(dirs[i])) { wcscpy(out, dirs[i]); return 1; }
    return 0;
}
static int CALLBACK browse_cb(HWND w, UINT msg, LPARAM lp, LPARAM data) {
    if (msg == BFFM_INITIALIZED && data) SendMessageW(w, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}
static int browse_nox(wchar_t *out, const wchar_t *start) {
    BROWSEINFOW bi; memset(&bi, 0, sizeof bi);
    bi.hwndOwner = g_wnd;
    bi.lpszTitle = L"Choose your Nox folder (the GOG install, with Game.exe and gamedata.bin):";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = browse_cb; bi.lParam = (LPARAM)(start && start[0] ? start : NULL);
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi);
    if (!id) return 0;
    int ok = SHGetPathFromIDListW(id, out);
    CoTaskMemFree(id);
    return ok;
}
static int same_dir(const wchar_t *a, const wchar_t *b) {
    wchar_t x[MAX_PATH], y[MAX_PATH];
    if (!GetFullPathNameW(a, MAX_PATH, x, NULL) || !GetFullPathNameW(b, MAX_PATH, y, NULL)) return 0;
    size_t n = wcslen(x); if (n && x[n - 1] == '\\') x[n - 1] = 0;
    n = wcslen(y); if (n && y[n - 1] == '\\') y[n - 1] = 0;
    return !_wcsicmp(x, y);
}
/* ready: Game.exe here and GAMEDIR is a Nox folder */
static int game_ready(void) {
    wchar_t ge[MAX_PATH], gd[MAX_PATH];
    here(ge, L"Game.exe"); gamedir_get(gd);
    return wexists(ge) && is_nox_dir(gd);
}
/* ask for the Nox folder, check it, copy the game files, write GAMEDIR; 1 = done */
static int setup_game(int first) {
    const wchar_t *T = L"Nox Modernized - Nox folder";
    wchar_t dir[MAX_PATH] = L"", cur[MAX_PATH];
    gamedir_get(cur);
    if (first) {
        wchar_t m[700];
        int found = guess_nox(dir);
        if (found) {
            swprintf(m, 700, L"Nox Modernized runs the game from its own folder and needs your Nox (GOG) folder for the game's data and saves.\n\n"
                             L"Found Nox at:\n%ls\n\nUse this folder?\n(No: choose another one.)", dir);
            int r = MessageBoxW(g_wnd, m, T, MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDCANCEL) return 0;
            if (r == IDNO) dir[0] = 0;
        } else {
            MessageBoxW(g_wnd, L"Nox Modernized runs the game from its own folder and needs your Nox (GOG) folder for the game's data and saves.\n\n"
                               L"Choose the Nox folder in the next window: the one with Game.exe and gamedata.bin.", T, MB_OK | MB_ICONINFORMATION);
        }
    }
    for (;;) {
        if (!dir[0] && !browse_nox(dir, is_nox_dir(cur) ? cur : NULL)) return 0;
        if (same_dir(dir, g_wdir)) {
            MessageBoxW(g_wnd, L"That is Nox Modernized's own folder. Choose the folder where Nox (GOG) is installed; keep Nox Modernized in a folder of its own.", T, MB_OK | MB_ICONWARNING);
            dir[0] = 0; continue;
        }
        if (!is_nox_dir(dir)) {
            wchar_t m[600];
            swprintf(m, 600, L"%ls\n\ndoes not look like the Nox folder: Game.exe and gamedata.bin should both be in it.\n\nChoose another folder?", dir);
            if (MessageBoxW(g_wnd, m, T, MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
            dir[0] = 0; continue;
        }
        break;
    }
    /* the check: the GOG Game.exe (2000-07-21) the mod's patches were made for */
    wchar_t src[MAX_PATH], dst[MAX_PATH];
    char md5[40] = "";
    swprintf(src, MAX_PATH, L"%ls\\Game.exe", dir);
    if (!md5_file(src, md5) || strcmp(md5, GOG_EXE_MD5)) {
        wchar_t m[700];
        swprintf(m, 700, L"This Game.exe is not the GOG version Nox Modernized was made for (MD5 %hs, expected %hs).\n\n"
                         L"The mod checks the game's code before every change and leaves out what does not match, so features may be missing "
                         L"or the game may not start. Use it anyway?", md5[0] ? md5 : "unreadable", GOG_EXE_MD5);
        if (MessageBoxW(g_wnd, m, T, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return 0;
    }
    /* copy what the mod needs next to it; the Nox folder is not changed */
    here(dst, L"Game.exe");
    if (!CopyFileW(src, dst, FALSE)) {
        wchar_t m[400]; swprintf(m, 400, L"Could not copy Game.exe into\n%ls\n\n(error %lu)", g_wdir, GetLastError());
        MessageBoxW(g_wnd, m, T, MB_OK | MB_ICONERROR);
        return 0;
    }
    swprintf(src, MAX_PATH, L"%ls\\ddraw.dll", dir); here(dst, L"ddraw_gog.dll");
    if (wexists(src)) CopyFileW(src, dst, FALSE);
    swprintf(src, MAX_PATH, L"%ls\\dxcfg.ini", dir); here(dst, L"dxcfg.ini");
    if (wexists(src) && !wexists(dst)) CopyFileW(src, dst, FALSE);
    if (!gamedir_set(dir)) {
        wchar_t m[400]; swprintf(m, 400, L"Could not write the Nox folder into %ls (is it there, and not read-only?).", bat_name());
        MessageBoxW(g_wnd, m, T, MB_OK | MB_ICONERROR);
        return 0;
    }
    wchar_t m[600];
    swprintf(m, 600, L"Ready. Nox folder:\n%ls\n\nGame.exe was copied here and %ls now starts the game with your Nox folder's data and saves.", dir, bat_name());
    MessageBoxW(g_wnd, m, T, MB_OK | MB_ICONINFORMATION);
    return 1;
}
/* after Save / on Close: how to start the game (once a run) */
static void tell_launch(void) {
    if (g_launch_told) return;
    g_launch_told = 1;
    wchar_t m[500];
    if (g_wine) swprintf(m, 500, L"Start the game with Play in this window, or with Launch-Winlator.bat (in Winlator), not with Game.exe: they set the Nox folder and this system's settings file.");
    else if (g_fileidx == 1) swprintf(m, 500, L"Start the game with Play in this window, or with Launch.bat.\n\nThe Winlator settings you edited are used by Launch-Winlator.bat: copy this folder to the Android device and start the game there (Play in this tool under Winlator, or Launch-Winlator.bat).");
    else swprintf(m, 500, L"Start the game with Play in this window, or with Launch.bat, not with Game.exe: they set the Nox folder the game's data and saves come from.");
    MessageBoxW(g_wnd, m, L"Nox Modernized", MB_OK | MB_ICONINFORMATION);
}
/* Play: save first if asked, make sure the Nox folder is set up, start this system's .bat and close */
static void play(void) {
    if (g_dirty) {
        int r = MessageBoxW(g_wnd, L"Save the changes before playing?", L"Nox Modernized", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL || (r == IDYES && !save_all())) return;
    }
    if (!game_ready() && !setup_game(1)) return;
    wchar_t bat[MAX_PATH]; here(bat, bat_name());
    HINSTANCE h = ShellExecuteW(g_wnd, L"open", bat, NULL, g_wdir, SW_HIDE);
    if ((INT_PTR)h <= 32) {
        wchar_t m[400]; swprintf(m, 400, L"Could not start %ls (error %d).", bat_name(), (int)(INT_PTR)h);
        MessageBoxW(g_wnd, m, L"Nox Modernized", MB_OK | MB_ICONERROR);
        return;
    }
    g_launch_told = 1;
    set_dirty(0);
    DestroyWindow(g_wnd);
}

static int confirm_discard(void) {
    if (!g_dirty) return 1;
    int r = MessageBoxW(g_wnd, L"Save the changes?", L"Nox Modernized settings", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return 0;
    if (r == IDYES) return save_all();
    return 1;
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id >= ID_SETTING && id < ID_SETTING + NSET) {
            if (code == CBN_SELCHANGE) {
                set_dirty(1);
                const char *key = k_set[id - ID_SETTING].key;
                if (!strcmp(key, "resolution") || !strcmp(key, "zoom_max")) update_lists(0);
            }
            if (code == CBN_SETFOCUS) SetWindowTextW(g_help, k_set[id - ID_SETTING].help);
        } else if (id == ID_FILE && code == CBN_SELCHANGE) {
            int idx = (int)SendMessageW(g_file, CB_GETCURSEL, 0, 0);
            if (idx != g_fileidx && confirm_discard()) { g_fileidx = idx; load_all(); }
            else SendMessageW(g_file, CB_SETCURSEL, g_fileidx, 0);
        } else if (id == ID_CLASS && code == CBN_SELCHANGE) {
            if (g_det) det_stop();
            g_view = (int)SendMessageW(g_cls, CB_GETCURSEL, 0, 0);
            fill_list();
            dstat(L"");
        } else if (id == ID_CHOOSE && code == CBN_SELCHANGE) {
            int s = (int)SendMessageW(g_choose, CB_GETCURSEL, 0, 0) - 1, a = sel_action();
            if (s >= 0 && a > 0) assign(a, s, 1);
            SendMessageW(g_choose, CB_SETCURSEL, 0, 0);
        } else if (id == ID_SET) det_start(1);
        else if (id == ID_ADD) det_start(2);
        else if (id == ID_CLEAR) {
            int a = sel_action();
            if (a > 0) {
                int *map = view_map(g_view);
                for (int c = 0; c < S_N; c++) if (map[c] == a) map[c] = 0;
                fill_list(); set_dirty(1); dstat(L"");
            }
        } else if (id == ID_DEF) {
            view_defaults(g_view);
            int n = g_view == 0 ? drop_class_overlaps() : 0;
            if (g_view) for (int c = 0; c < S_N; c++) if (g_shared[c]) g_map[g_view - 1][c] = 0;
            fill_list(); set_dirty(1);
            if (n) dstat(L"Original buttons again; %d class button(s) on the same combos were cleared.", n);
            else dstat(L"Original buttons again.");
        }
        else if (id == ID_SAVE) { if (save_all()) tell_launch(); }
        else if (id == ID_NOXDIR) setup_game(0);
        else if (id == ID_PLAY) play();
        else if (id == ID_CLOSE) PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *n = (NMHDR *)lp;
        if (n->hwndFrom == g_tab && n->code == TCN_SELCHANGE) show_page((int)SendMessageW(g_tab, TCM_GETCURSEL, 0, 0));
        if (n->hwndFrom == g_list && n->code == LVN_ITEMCHANGED && g_det) det_stop();
        return 0;
    }
    case WM_TIMER:
        if (wp == DET_TIMER) det_tick();
        else if (wp == 1) { /* the help line: what the pointer is over */
            static HWND last;
            POINT pt; GetCursorPos(&pt);
            HWND w = WindowFromPoint(pt);
            if (w != last) {
                last = w;
                const wchar_t *t = help_of(w);
                if (t) SetWindowTextW(g_help, t);
            }
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        if (g_banner && g_top) {
            RECT rc; GetClientRect(h, &rc);
            HDC mem = CreateCompatibleDC(dc); HGDIOBJ old = SelectObject(mem, g_banner);
            BITMAP bm; GetObjectW(g_banner, sizeof bm, &bm);
            SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, NULL);
            StretchBlt(dc, 0, 0, rc.right, g_top, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            SelectObject(mem, old); DeleteDC(mem);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        if (GetWindowLongPtrW((HWND)lp, GWLP_USERDATA) == 1) {
            if ((HWND)lp == g_dstat) SetTextColor((HDC)wp, RGB(0, 90, 170));
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)GetStockObject(HOLLOW_BRUSH);
        }
        break;
    case WM_CLOSE:
        if (confirm_discard()) { tell_launch(); DestroyWindow(h); }
        return 0;
    case WM_APP: /* the first start: the Nox folder */
        if (!game_ready() && !setup_game(1))
            MessageBoxW(h, L"No Nox folder chosen: the game cannot start yet. Use \"Nox folder...\" at the top right to choose it.",
                        L"Nox Modernized - Nox folder", MB_OK | MB_ICONWARNING);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
    (void)prev; (void)cmd;
    g_inst = inst;
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    char *sl = strrchr(g_dir, '\\'); if (sl) sl[1] = 0; else g_dir[0] = 0;
    static const char *xd[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (int i = 0; i < 3 && !g_xget; i++) { HMODULE x = LoadLibraryA(xd[i]); if (x) g_xget = (xget_t)GetProcAddress(x, "XInputGetState"); }

    /* the file: the one Launch-Winlator.bat names (ONC_INI), else the Winlator one under Wine when it exists */
    char env[64];
    DWORD n = GetEnvironmentVariableA("ONC_INI", env, sizeof env);
    char wl[MAX_PATH]; ini_for(wl, k_files[1]);
    int wine = GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") != NULL;
    g_wine = wine;
    GetModuleFileNameW(NULL, g_wdir, MAX_PATH);
    { wchar_t *b = wcsrchr(g_wdir, L'\\'); if (b) b[1] = 0; else g_wdir[0] = 0; }
    g_fileidx = (n > 0 && n < sizeof env && !_stricmp(env, k_files[1])) || (wine && file_exists(wl)) ? 1 : 0;

    NONCLIENTMETRICSW nm; nm.cbSize = sizeof nm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof nm, &nm, 0);
    g_font = CreateFontIndirectW(&nm.lfMessageFont);
    HDC dc = GetDC(NULL); HFONT of = SelectObject(dc, g_font);
    TEXTMETRICW tm; GetTextMetricsW(dc, &tm); g_fh = tm.tmHeight; if (g_fh < 12) g_fh = 12;
    SelectObject(dc, of); ReleaseDC(NULL, dc);

    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = L"OncLauncher";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    g_banner = (HBITMAP)LoadImageW(inst, MAKEINTRESOURCEW(2), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    int cw = g_fh * 56;
    g_top = g_banner ? cw * 120 / 1344 : 0; /* the strip keeps its shape across the window's width */
    int ch = g_fh * 36 + g_top;
    RECT wr = {0, 0, cw, ch};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&wr, style, FALSE);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    g_wnd = CreateWindowExW(0, wc.lpszClassName, L"Nox Modernized settings", style,
                            wa.left + (wa.right - wa.left - ww) / 2, wa.top + (wa.bottom - wa.top - wh) / 2, ww, wh, NULL, NULL, inst, NULL);
    SendMessageW(g_wnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    SendMessageW(g_wnd, WM_SETICON, ICON_BIG, (LPARAM)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    build(cw, ch);
    SendMessageW(g_file, CB_SETCURSEL, g_fileidx, 0);
    load_all();
    show_page(P_DISPLAY);
    SetTimer(g_wnd, 1, 150, NULL);
    ShowWindow(g_wnd, show);
    PostMessageW(g_wnd, WM_APP, 0, 0); /* the Nox folder check, once the window is up */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (IsDialogMessageW(g_wnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
