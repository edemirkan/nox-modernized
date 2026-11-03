/* The settings menu (F7, or Select + Start on the pad): a panel on the left of the screen,
   so each change shows in the game at once. Mouse: left click = next value, right click = previous; while the
   menu is open the mouse buttons only work the menu. Pad: up / down choose, left / right or A change, B or
   Select + Start close. "Save as default" writes the settings file (nox-modernized.ini, or the Winlator one); otherwise changes last until the game ends. */
#include "draw.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* ---- the settings ---- */
static int (__cdecl *dd_get)(const char *);
static void (__cdecl *dd_set)(const char *, int);
static int dget(const char *k) { return dd_get ? dd_get(k) : -1; }
static void dset(const char *k, int v) { if (dd_set) dd_set(k, v); }

int onc_flicker_get(void); void onc_flicker_set(int);
int onc_mapbg_get(void); void onc_mapbg_set(int);
int onc_camera_get(void); void onc_camera_set(int);
int onc_camera_dz_get(void); void onc_camera_dz_set(int); int onc_camera_lag_get(void); void onc_camera_lag_set(int);
int onc_hints_get(void); void onc_hints_set(int);
double onc_zoom_get(void); double onc_zoom_max(void); void onc_zoom_set(double);
int onc_uiscale_active(void); int onc_uiscale_max(void);
int onc_smooth_get(int); void onc_smooth_set(int, int);
double onc_carry_get(void); void onc_carry_step(int); int onc_carry_available(void); int onc_carry_status(int *, int *);
int onc_hfr_get(void); void onc_hfr_set(int);
int onc_aimcone_get(void); void onc_aimcone_set(int);

static const wchar_t *k_filters[] = {L"Nearest", L"Sharp", L"Linear", L"Bicubic", L"Lanczos"};
static const char *k_filter_keys[] = {"nearest", "sharp", "linear", "bicubic", "lanczos"};
static const wchar_t *k_scales[] = {L"Fit", L"Integer", L"Stretch"};
static const char *k_scale_keys[] = {"fit", "integer", "stretch"};
static const wchar_t *k_flicker[] = {L"Original", L"Gentle", L"Off"};
static const char *k_flicker_keys[] = {"original", "gentle", "off"};
static const int k_mapbg[] = {100, 75, 50, 25, 10, 0};
static const int k_camdz[] = {0, 5, 10, 15, 20, 25, 30};      /* camera dead zone, percent */
static const int k_camlag[] = {10, 15, 20, 25, 30, 35, 40};   /* camera max distance, percent */
static const wchar_t *k_levels[] = {L"Off", L"Low", L"Medium", L"High"};
static const char *k_smooth_keys[] = {"sprite_edges", "sprite_soften", "wall_edges", "wall_soften", "floor_soften"};
static int g_uiscale_saved = -1; /* the value for the next start */

/* the game's own brightness (Insert / Delete): level 1..10 at 0x599D30, read 0x436470 (clamps), set 0x4364A0,
   applied by 0x4364D0 (builds the gamma ramp; ddraw.dll folds it into its colour conversion). The game keeps it in
   nox.cfg ("Gamma = N"), so it is not written to our settings file. 1 = the original picture. */
static int bright_get(void) { return ((int (__cdecl *)(void))0x436470)(); }
static void bright_step(int dir) {
    int v = bright_get() + dir;
    if (v < 1) v = 1; else if (v > 10) v = 10;
    ((void (__cdecl *)(int))0x4364A0)(v);
    ((int (__cdecl *)(void))0x4364D0)();
}

static int cyc(int v, int n, int dir) { v = (v + dir) % n; return v < 0 ? v + n : v; }

enum { R_FILTER, R_SCALE, R_BRIGHT, R_VSYNC, R_RATE, R_FPS, R_ZOOM, R_UISCALE, R_CAMERA, R_CAMDZ, R_CAMLAG, R_FLICKER, R_MAPBG, R_SM0, R_SM1, R_SM2, R_SM3, R_SM4, R_CARRY, R_HINTS, R_CONE,
       R_SAVE, R_N };
static const wchar_t *k_labels[R_N] = {L"Display filter", L"Scaling", L"Brightness", L"VSync", L"Frame rate", L"Frame counter", L"World zoom",
                                       L"UI scale", L"Smooth camera", L"Camera dead zone", L"Camera max distance", L"Fire light flicker", L"Map background",
                                       L"Sprite edge smoothing", L"Sprite softening", L"Wall edge smoothing",
                                       L"Wall softening", L"Floor softening", L"Carry weight limit",
                                       L"Gamepad hints", L"Aim cone (gamepad)", L"[ Save as default ]"};

static void value(int r, wchar_t *out, int n) {
    out[0] = 0;
    switch (r) {
    case R_FILTER: { int v = dget("filter"); if (v >= 0 && v < 5) wcscpy(out, k_filters[v]); else wcscpy(out, L"-"); break; }
    case R_SCALE: { int v = dget("scale"); if (v >= 0 && v < 3) wcscpy(out, k_scales[v]); else wcscpy(out, L"-"); break; }
    case R_BRIGHT: { int v = bright_get(); swprintf(out, n, v == 1 ? L"%d / 10 (original)" : L"%d / 10", v); break; }
    case R_VSYNC: wcscpy(out, dget("vsync") > 0 ? L"On" : L"Off"); break;
    case R_RATE: { int r = onc_hfr_get(); if (r <= 30) wcscpy(out, L"30 (original)"); else swprintf(out, n, L"%d", r); break; }
    case R_FPS: wcscpy(out, dget("fps") > 0 ? L"On" : L"Off"); break;
    case R_ZOOM: swprintf(out, n, L"%.2fx", onc_zoom_get()); break;
    case R_UISCALE: {
        int a = onc_uiscale_active(), v = g_uiscale_saved < 0 ? a : g_uiscale_saved;
        if (v == a) swprintf(out, n, L"%d.%02d", v / 100, v % 100);
        else swprintf(out, n, L"%d.%02d (restart)", v / 100, v % 100);
        break;
    }
    case R_CAMERA: wcscpy(out, onc_camera_get() ? L"On" : L"Off"); break;
    case R_CAMDZ: swprintf(out, n, L"%d%%", onc_camera_dz_get()); break;
    case R_CAMLAG: swprintf(out, n, L"%d%%", onc_camera_lag_get()); break;
    case R_FLICKER: wcscpy(out, k_flicker[onc_flicker_get() % 3]); break;
    case R_MAPBG: {
        int v = onc_mapbg_get();
        if (v >= 100) wcscpy(out, L"Black"); else if (v <= 0) wcscpy(out, L"Clear"); else swprintf(out, n, L"%d%% dark", v);
        break;
    }
    case R_HINTS: wcscpy(out, onc_hints_get() ? L"On" : L"Off"); break;
    case R_CONE: { static const wchar_t *m[] = {L"Off", L"With R1 (aim)", L"Always, brighter with R1"}; wcscpy(out, m[onc_aimcone_get() % 3]); break; }
    case R_SM0: case R_SM1: case R_SM2: case R_SM3: case R_SM4:
        wcscpy(out, k_levels[onc_smooth_get(r - R_SM0) & 3]); break;
    case R_CARRY: {
        if (!onc_carry_available()) { wcscpy(out, L"-"); break; }
        int c, w, k = swprintf(out, n, L"x%g%ls", onc_carry_get(), onc_carry_get() == 1 ? L" (default)" : L"");
        if (k > 0 && onc_carry_status(&c, &w)) swprintf(out + k, n - k, L"  %d/%d", w, c);
        break;
    }
    }
}

static wchar_t g_status[64];

static void save(void) {
    char b[32];
    int v;
    if ((v = dget("filter")) >= 0 && v < 5) onc_cfg_write("display", "filter", k_filter_keys[v]);
    if ((v = dget("scale")) >= 0 && v < 3) onc_cfg_write("display", "scale", k_scale_keys[v]);
    if ((v = dget("vsync")) >= 0) onc_cfg_write("display", "vsync", v ? "1" : "0");
    if ((v = dget("fps")) >= 0) onc_cfg_write("display", "fps", v ? "1" : "0");
    snprintf(b, sizeof b, "%d", onc_hfr_get()); onc_cfg_write("display", "frame_rate", b);
    snprintf(b, sizeof b, "%.2f", onc_zoom_get()); onc_cfg_write("display", "zoom", b);
    v = g_uiscale_saved < 0 ? onc_uiscale_active() : g_uiscale_saved;
    snprintf(b, sizeof b, "%d.%02d", v / 100, v % 100); onc_cfg_write("display", "ui_scale", b);
    onc_cfg_write("display", "camera_smooth", onc_camera_get() ? "1" : "0");
    snprintf(b, sizeof b, "%.2f", onc_camera_dz_get() / 100.0); onc_cfg_write("display", "camera_dead_zone", b);
    snprintf(b, sizeof b, "%.2f", onc_camera_lag_get() / 100.0); onc_cfg_write("display", "camera_max_lag", b);
    onc_cfg_write("display", "light_flicker", k_flicker_keys[onc_flicker_get() % 3]);
    snprintf(b, sizeof b, "%d", onc_mapbg_get()); onc_cfg_write("display", "map_background", b);
    for (int k = 0; k < 5; k++) { snprintf(b, sizeof b, "%d", onc_smooth_get(k)); onc_cfg_write("display", k_smooth_keys[k], b); }
    snprintf(b, sizeof b, "%g", onc_carry_get()); onc_cfg_write("game", "carry_weight", b);
    onc_cfg_write("pad", "hints", onc_hints_get() ? "1" : "0");
    snprintf(b, sizeof b, "%d", onc_aimcone_get()); onc_cfg_write("pad", "aim_cone", b);
    const char *onc_cfg_name(void);
    wcscpy(g_status, L"Saved to ");
    size_t k = wcslen(g_status);
    MultiByteToWideChar(CP_ACP, 0, onc_cfg_name(), -1, g_status + k, (int)(sizeof g_status / sizeof g_status[0] - k));
    onc_log("settings saved");
}

static void step(int r, int dir) {
    int v;
    switch (r) {
    case R_FILTER: if ((v = dget("filter")) >= 0) dset("filter", cyc(v, 5, dir)); break;
    case R_SCALE: if ((v = dget("scale")) >= 0) dset("scale", cyc(v, 3, dir)); break;
    case R_BRIGHT: bright_step(dir); break;
    case R_VSYNC: dset("vsync", !(dget("vsync") > 0)); break;
    case R_RATE: {
        static const int rates[] = {30, 60, 90, 120};
        int i = 0;
        for (int k = 0; k < 4; k++) if (rates[k] == onc_hfr_get()) i = k;
        onc_hfr_set(rates[cyc(i, 4, dir)]);
        break;
    }
    case R_FPS: dset("fps", !(dget("fps") > 0)); break;
    case R_ZOOM: { /* 0.1 steps (from a value between steps: to the next one), round from the end to the start */
        int cur = (int)(onc_zoom_get() * 100 + 0.5), max = (int)(onc_zoom_max() * 100 + 0.5), z;
        if (dir > 0) z = cur >= max ? 100 : (cur / 10 + 1) * 10;
        else z = cur <= 100 ? max : (cur - 1) / 10 * 10;
        if (z > max) z = max;
        onc_zoom_set(z / 100.0);
        break;
    }
    case R_UISCALE: { /* 0.1 steps up to the largest the resolution allows (that one too, e.g. 2.25 at 1080p) */
        int cur = g_uiscale_saved < 0 ? onc_uiscale_active() : g_uiscale_saved, max = onc_uiscale_max(), v;
        if (dir > 0) v = cur >= max ? 100 : (cur / 10 + 1) * 10;
        else v = cur <= 100 ? max : (cur - 1) / 10 * 10;
        if (v > max) v = max;
        g_uiscale_saved = v;
        break;
    }
    case R_CAMERA: onc_camera_set(!onc_camera_get()); break;
    case R_CAMDZ: { int cur = onc_camera_dz_get(), i = 0; for (int k = 0; k < 7; k++) if (cur >= k_camdz[k]) i = k; onc_camera_dz_set(k_camdz[cyc(i, 7, dir)]); break; }
    case R_CAMLAG: { int cur = onc_camera_lag_get(), i = 0; for (int k = 0; k < 7; k++) if (cur >= k_camlag[k]) i = k; onc_camera_lag_set(k_camlag[cyc(i, 7, dir)]); break; }
    case R_FLICKER: onc_flicker_set(cyc(onc_flicker_get(), 3, dir)); break;
    case R_MAPBG: {
        int cur = onc_mapbg_get(), i = 0;
        for (int k = 0; k < 6; k++) if (cur <= k_mapbg[k]) i = k;
        onc_mapbg_set(k_mapbg[cyc(i, 6, dir)]);
        break;
    }
    case R_HINTS: onc_hints_set(!onc_hints_get()); break;
    case R_CONE: onc_aimcone_set(cyc(onc_aimcone_get(), 3, dir)); break;
    case R_SM0: case R_SM1: case R_SM2: case R_SM3: case R_SM4:
        onc_smooth_set(r - R_SM0, cyc(onc_smooth_get(r - R_SM0), 4, dir)); break;
    case R_CARRY: onc_carry_step(dir); break;
    case R_SAVE: save(); break;
    }
}

/* ---- the panel: tabs (L1 / R1 on the pad, a click on the tab's name with the mouse) ---- */
enum { T_N = 4 };
static const wchar_t *k_tabs[T_N] = {L"Display", L"Camera", L"Effects", L"Game"};
static const int k_tab_rows[T_N][10] = { /* each ends with -1; "Save as default" is added to every tab */
    {R_FILTER, R_SCALE, R_BRIGHT, R_VSYNC, R_RATE, R_FPS, R_UISCALE, -1},
    {R_ZOOM, R_CAMERA, R_CAMDZ, R_CAMLAG, -1},
    {R_FLICKER, R_MAPBG, R_SM0, R_SM1, R_SM2, R_SM3, R_SM4, -1},
    {R_CARRY, R_HINTS, R_CONE, -1},
};
static int g_open, g_tab, g_sel, g_x0, g_y0, g_w = 380, g_row_h, g_rows_y, g_tabs_y, g_tab_x[T_N + 1];

/* the rows of the tab shown (row ids), "Save as default" last */
static int tab_rows(int *out) {
    int n = 0;
    for (const int *r = k_tab_rows[g_tab]; *r >= 0; r++) out[n++] = *r;
    out[n++] = R_SAVE;
    return n;
}
static int max_rows(void) {
    int m = 0;
    for (int t = 0; t < T_N; t++) { int n = 1; for (const int *r = k_tab_rows[t]; *r >= 0; r++) n++; if (n > m) m = n; }
    return m;
}
static void tab_step(int dir) {
    g_tab = cyc(g_tab, T_N, dir);
    g_sel = 0;
    g_status[0] = 0;
    ((void (__cdecl *)(int, int))0x4528D0)(920, 100);
}

int onc_settings_open(void) { return g_open; }
void onc_settings_toggle(void) {
    g_open = !g_open;
    g_status[0] = 0;
    if (g_open && !dd_get) {
        HMODULE dd = GetModuleHandleA("ddraw.dll");
        if (dd) {
            dd_get = (int (__cdecl *)(const char *))GetProcAddress(dd, "OncDisplayGet");
            dd_set = (void (__cdecl *)(const char *, int))GetProcAddress(dd, "OncDisplaySet");
        }
    }
    ((void (__cdecl *)(int, int))0x4528D0)(g_open ? 789 : 790, 100); /* the radial menu's open / close sounds */
}

/* the row under the mouse: its index in the tab, -1 none */
static int row_at(int mx, int my) {
    if (!g_row_h || mx < g_x0 || mx >= g_x0 + g_w) return -1;
    int rows[16], n = tab_rows(rows);
    int r = (my - g_rows_y + 3) / g_row_h;
    if (my - g_rows_y + 3 < 0) return -1;
    if (r == max_rows() - 1) return n - 1; /* Save as default, at the bottom of every tab */
    return r < n - 1 ? r : -1;
}
/* the tab name under the mouse, -1 none */
static int tab_at(int mx, int my) {
    if (!g_row_h || my < g_tabs_y - 3 || my >= g_tabs_y - 3 + g_row_h) return -1;
    for (int t = 0; t < T_N; t++) if (mx >= g_tab_x[t] - 6 && mx < g_tab_x[t + 1] - 6) return t;
    return -1;
}

void onc_settings_draw(void) {
    static int was_f7;
    int f7 = GetForegroundWindow() && (GetAsyncKeyState(VK_F7) & 0x8000);
    if (f7 && !was_f7) {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid == GetCurrentProcessId()) onc_settings_toggle();
    }
    was_f7 = f7;
    if (!g_open) return;
    int tw, th;
    onc_text_size(L"Ag", &tw, &th);
    g_row_h = th + 6;
    /* title, tabs, gap, rows (the tallest tab: the panel keeps its size), help line, status */
    int pad = 10, n = max_rows() + 5, h = n * g_row_h + 2 * pad;
    g_x0 = 12;
    g_y0 = (ONC_I32(NOX_SCREEN_H) - h) / 3;
    if (g_y0 < 12) g_y0 = 12;
    extern int onc_pad_in_use(void);
    int pad_mode = onc_pad_in_use();
    int mx = ONC_I32(NOX_MOUSE_X), my = ONC_I32(NOX_MOUSE_Y);
    onc_draw_begin();
    for (int k = 0; k < 3; k++) ((void (__cdecl *)(int, int, int, int))0x484850)(g_x0, g_y0, g_w, h);
    onc_draw_frame(g_x0, g_y0, g_w, h, onc_rgb(120, 100, 60));
    int y = g_y0 + pad;
    onc_text_color(onc_rgb(255, 220, 90));
    onc_text(L"SETTINGS   (F7 / Select + Start)", g_x0 + pad, y);
    y += g_row_h;
    /* the tabs: L1 [Display] Camera Effects Game R1 */
    g_tabs_y = y;
    int x = g_x0 + pad;
    if (pad_mode) { onc_text_color(onc_rgb(150, 150, 150)); onc_text(L"L1", x, y); onc_text_size(L"L1  ", &tw, &th); x += tw; }
    int htab = pad_mode ? -1 : tab_at(mx, my);
    for (int t = 0; t < T_N; t++) {
        int w;
        onc_text_size(k_tabs[t], &w, &th);
        g_tab_x[t] = x;
        if (t == g_tab) {
            onc_draw_color(onc_rgb(70, 55, 25)); onc_draw_rect(x - 5, y - 3, w + 10, g_row_h);
            onc_draw_frame(x - 5, y - 3, w + 10, g_row_h, onc_rgb(120, 100, 60));
        }
        onc_text_color(t == g_tab ? onc_rgb(255, 220, 90) : t == htab ? onc_rgb(255, 255, 255) : onc_rgb(170, 170, 170));
        onc_text(k_tabs[t], x, y);
        x += w + 16;
    }
    g_tab_x[T_N] = x;
    if (pad_mode) { onc_text_color(onc_rgb(150, 150, 150)); onc_text(L"R1", x, y); }
    y += g_row_h + g_row_h / 2;
    g_rows_y = y;
    int rows[16], nr = tab_rows(rows);
    if (g_sel >= nr) g_sel = nr - 1;
    int hover = row_at(mx, my);
    for (int i = 0; i < nr; i++) {
        int r = rows[i];
        int on = pad_mode ? i == g_sel : i == hover;
        int ry = r == R_SAVE ? g_rows_y + (max_rows() - 1) * g_row_h : y; /* Save as default: always at the bottom */
        if (on) { onc_draw_color(onc_rgb(70, 55, 25)); onc_draw_rect(g_x0 + 4, ry - 3, g_w - 8, g_row_h); }
        onc_text_color(on ? onc_rgb(255, 220, 90) : onc_rgb(230, 230, 230));
        onc_text(k_labels[r], g_x0 + pad, ry);
        wchar_t v[48];
        value(r, v, 48);
        if (v[0]) { onc_text_color(onc_rgb(120, 230, 120)); onc_text(v, g_x0 + 200, ry); }
        y += g_row_h;
    }
    y = g_rows_y + max_rows() * g_row_h + g_row_h / 2;
    onc_text_color(onc_rgb(150, 150, 150));
    /* (the tab line shows L1 / R1 on the pad; with the mouse the tab under it lights up) */
    onc_text(pad_mode ? L"Up / down: choose   Left / right, A: change   B: close" : L"Left click: next   Right click: previous",
             g_x0 + pad, y);
    y += g_row_h;
    if (g_status[0]) { onc_text_color(onc_rgb(255, 160, 60)); onc_text(g_status, g_x0 + pad, y); }
    onc_draw_end();
}

/* the mouse buttons while the menu is open (dinput_proxy.c): 1 = the game does not see it */
int onc_settings_mouse(int button, int pressed) {
    if (!g_open) return 0;
    if (pressed) {
        int mx = ONC_I32(NOX_MOUSE_X), my = ONC_I32(NOX_MOUSE_Y), t = tab_at(mx, my), i = row_at(mx, my);
        if (t >= 0) { if (t != g_tab) tab_step(t - g_tab); }
        else if (i >= 0) {
            int rows[16]; tab_rows(rows);
            step(rows[i], button == 1 ? -1 : 1); ((void (__cdecl *)(int, int))0x4528D0)(921, 100);
        }
    }
    return 1; /* no moving or attacking while the menu is open */
}

/* the pad while the menu is open (pad.c); tab: -1 L1, 1 R1 */
void onc_settings_pad(int dx, int dy, int a, int b, int tab) {
    if (!g_open) return;
    if (tab) { tab_step(tab); return; }
    int rows[16], n = tab_rows(rows);
    if (dy) { g_sel = cyc(g_sel, n, dy); ((void (__cdecl *)(int, int))0x4528D0)(920, 100); }
    if (dx || a) { step(rows[g_sel], dx < 0 ? -1 : 1); ((void (__cdecl *)(int, int))0x4528D0)(921, 100); }
    if (b) onc_settings_toggle();
}
