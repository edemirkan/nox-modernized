/* Short gamepad hints, only while the pad is in use: what R3 does above its target, and the buttons of the
   inventory / shop screen under it. Drawn with the game's own text functions after the whole GUI. */
#include "gui.h"
#include "draw.h"
#include "inventory.h"
#include "inv_tabs.h"
#include "shop.h"
#include "../world/targets.h"
#include <wchar.h>

static int g_on = 1;

/* the mouse cursor is not drawn while the left stick plays (pad.c decides) */
int onc_pad_cursor_hidden(void);
static void (__cdecl *g_draw_cursor)(void);
int onc_cursor_capture(void (*draw)(void), int hidden);
static void __cdecl draw_cursor(void) {
    /* with the high frame rate the cursor is a layer of its own (cursor.c) */
    if (onc_cursor_capture(g_draw_cursor, onc_pad_cursor_hidden())) return;
    g_draw_cursor();
}

void onc_hints_init(void) {
    g_on = onc_cfg_int("pad", "hints", 1);
    static const uint8_t start[] = {0xA1, 0x14, 0x73, 0x6F, 0x00}; /* mov eax, [cursor init] */
    g_draw_cursor = (void (__cdecl *)(void))onc_hook_detour("cursor draw", 0x4875E0, draw_cursor, start, 5);
}

static void hint_box(const wchar_t *s, int x, int y, int centred) {
    int w = 0, h = 0;
    onc_text_size(s, &w, &h);
    if (centred) x -= w / 2;
    int sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    if (x + w + 8 > sw) x = sw - w - 8;
    if (x < 2) x = 2;
    if (y + h + 4 > sh) y = sh - h - 4;
    if (y < 2) y = 2;
    onc_draw_color(onc_rgb(0, 0, 0));
    onc_draw_rect(x, y, w + 8, h + 4);
    onc_text_color(ONC_I32(NOX_COLOR_YELLOW));
    onc_text(s, x + 4, y + 2);
}

/* ---- while the game's pause menu (Esc: QuitMenu.wnd) is open: the mod's controls that have no tooltip ----
   The menu window is made once by 0x445320 (kept at 0x69BA34) and shown / hidden. Keyboard keys always (they work
   for everyone); the gamepad's when the pad is in use and gamepad hints are on. Beside the menu, never over it. */
#define NOX_QUIT_MENU 0x69BA34
double onc_zoom_max(void);
const wchar_t *onc_bind_label_action(const char *name); /* binds.h */
static void key_name(int vk, wchar_t *out, int n) {
    if (vk >= VK_F1 && vk <= VK_F24) swprintf(out, n, L"F%d", vk - VK_F1 + 1);
    else if (vk == VK_SNAPSHOT) wcsncpy(out, L"Print Screen", n);
    else if (vk == VK_PAUSE) wcsncpy(out, L"Pause", n);
    else if (vk == VK_SCROLL) wcsncpy(out, L"Scroll Lock", n);
    else if (vk > 0) swprintf(out, n, L"key %d", vk);
    else out[0] = 0;
}
static void pause_help(void) {
    nox_win *qm = *(nox_win **)NOX_QUIT_MENU;
    if (!qm || !onc_win_visible(qm)) return;
    enum { MAXR = 24 };
    const wchar_t *rows[MAXR][2]; /* key, what; a NULL what: a heading */
    static wchar_t buf[6][48];
    int n = 0, zoom = onc_zoom_max() > 1.001;
    rows[n][0] = L"Keyboard and mouse"; rows[n++][1] = NULL;
    rows[n][0] = L"F7"; rows[n++][1] = L"Nox Modernized settings";
    if (zoom) {
        rows[n][0] = L"Numpad + / -"; rows[n++][1] = L"zoom in / out";
        rows[n][0] = L"Numpad *"; rows[n++][1] = L"zoom back to 1x";
    }
    key_name(onc_cfg_int("display", "fps_key", VK_F8), buf[0], 48);
    if (buf[0][0]) { rows[n][0] = buf[0]; rows[n++][1] = L"frame counter"; }
    key_name(onc_cfg_int("display", "screenshot_key", VK_SNAPSHOT), buf[1], 48);
    if (buf[1][0]) { rows[n][0] = buf[1]; rows[n++][1] = L"screenshot"; }
    rows[n][0] = L"Insert / Delete"; rows[n++][1] = L"brightness";
    if (g_on && onc_pad_in_use()) {
        rows[n][0] = L"Gamepad"; rows[n++][1] = NULL;
        rows[n][0] = L"Select + Start"; rows[n++][1] = L"settings (L1 / R1: tabs)";
        const wchar_t *inv = onc_bind_label_action("inventory");
        swprintf(buf[2], 48, L"Hold %ls", inv ? inv : L"Select");
        rows[n][0] = buf[2]; rows[n++][1] = L"all gamepad controls";
        rows[n][0] = L"Hold L1"; rows[n++][1] = L"the L1 combinations";
        if (zoom) { rows[n][0] = L"R3 held + right stick"; rows[n++][1] = L"zoom (up / down)"; }
        rows[n][0] = L"Right stick, pushed firmly"; rows[n++][1] = L"mouse cursor";
        int onc_pad_r3_click(void);
        if (onc_pad_r3_click()) { rows[n][0] = L"R3 (cursor shown)"; rows[n++][1] = L"left click"; }
    }
    int fh = 0, kw = 0, vw = 0;
    onc_text_size(L"Ag", NULL, &fh);
    int lh = fh + 2;
    for (int i = 0; i < n; i++) {
        if (!rows[i][1]) continue;
        int a = 0, b = 0;
        onc_text_size(rows[i][0], &a, NULL); onc_text_size(rows[i][1], &b, NULL);
        if (a > kw) kw = a;
        if (b > vw) vw = b;
    }
    const int pad = 10;
    int heads = 0;
    for (int i = 0; i < n; i++) if (!rows[i][1]) heads++;
    int w = pad * 2 + kw + 16 + vw, h = pad * 2 + lh * (n + 1) + heads * lh / 2;
    int sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    onc_rect m = onc_win_rect(qm);
    /* right of the menu, left of it, under it, above it: the first place on the screen that does not cover it;
       else the top left corner */
    int cx[4] = {m.x1 + 16, m.x0 - 16 - w, (m.x0 + m.x1 - w) / 2, (m.x0 + m.x1 - w) / 2};
    int cy[4] = {m.y0, m.y0, m.y1 + 8, m.y0 - 8 - h};
    int x = 4, y = 4;
    for (int k = 0; k < 4; k++) {
        int px = cx[k], py = cy[k];
        if (k < 2 && py + h > sh - 4) py = sh - 4 - h;
        if (py < 4 && k < 2) py = 4;
        if (px < 4 || py < 4 || px + w > sw - 4 || py + h > sh - 4) continue;
        onc_rect b = {px, py, px + w, py + h};
        if (b.x0 < m.x1 && m.x0 < b.x1 && b.y0 < m.y1 && m.y0 < b.y1) continue;
        x = px; y = py;
        break;
    }
    onc_draw_begin();
    for (int k = 0; k < 2; k++) ((void (__cdecl *)(int, int, int, int))0x484850)(x, y, w, h);
    onc_draw_frame(x, y, w, h, onc_rgb(120, 100, 60));
    onc_text_color(onc_rgb(255, 220, 90));
    onc_text(L"NOX MODERNIZED CONTROLS", x + pad, y + pad);
    int yy = y + pad + lh;
    for (int i = 0; i < n; i++) {
        if (!rows[i][1]) {
            yy += lh / 2;
            onc_text_color(onc_rgb(170, 170, 170));
            onc_text(rows[i][0], x + pad, yy);
        } else {
            onc_text_color(ONC_I32(NOX_COLOR_YELLOW));
            onc_text(rows[i][0], x + pad, yy);
            onc_text_color(onc_rgb(255, 255, 255));
            onc_text(rows[i][1], x + pad + kw + 16, yy);
        }
        yy += lh;
    }
    onc_draw_end();
}

void onc_hints_game(void);
void onc_hints_draw(void) {
    if (ONC_I32(NOX_CLIENT_CONNECTED)) pause_help();
    onc_hints_game();
    if (!g_on || !onc_pad_in_use() || !ONC_I32(NOX_CLIENT_CONNECTED)) return;
    onc_draw_begin();
    int x, y;
    const wchar_t *t;
    int onc_pad_cursor_mode(void);
    if (!onc_pad_cursor_mode() && onc_target_hint(&x, &y, &t)) hint_box(t, x, y, 1); /* cursor mode: R3 clicks */
    int shop = onc_shop_mode();
    if (shop == SHOP_BUY && onc_shop_on_top()) {
        int32_t *g = (int32_t *)0x6F31B0; /* the vendor's items */
        hint_box(L"A: buy    B: back", g[0], g[3] + 4, 0);
    } else if (onc_inv_active()) {
        nox_win *root = *(nox_win **)(NOX_INV_PANEL - 4);
        onc_rect rr = onc_win_rect(root);
        const wchar_t *s;
        if (shop == SHOP_SELL) s = L"A: sell    B: back    L1 / R1: tabs";
        else if (shop == SHOP_REPAIR) s = L"A: repair    B: back    L1 / R1: tabs";
        else if (*(volatile uint8_t *)NOX_INV_JOURNAL) s = L"L1 / R1: tabs    B: close";
        else if (onc_inv_doll_selected() || onc_inv_on_doll()) s = L"A: unequip    Right: items    L1 / R1: tabs    B: close";
        else s = L"A: use / equip    X: drop (hold: carry)    Left: equipment    L1 / R1: tabs    B: close";
        hint_box(s, rr.x0 + 8, rr.y1 + 2, 0);
    }
    onc_draw_end();
}

/* ---- the controls list (the inventory combo held), the L1 box, the hint after loading, more HUD labels ---- */
#include "../input/binds.h"
int onc_pad_help_shown(void), onc_pad_l1_held(void);
int onc_book_active(void), onc_radial_active(void), onc_summon_menu(void);

#define NOX_SPELLBAR   0x5A54F8 /* -> the spell bar: +212 the slot windows */
#define NOX_QUICKBAR   0x6D2438 /* the spell bar's frame window (children: the spell set gems) */
#define NOX_BOOK_BTN   0x6D2448
#define NOX_TRAP_BTN   0x6D2444 /* the "lay trap" button (proc 0x45F2F0 -> build trap 0x45DB90) */
#define NOX_POISON_WIN 0x6D8310
#define NOX_TRAP_TRAY  0x6D2420 /* 1: shown */
#define NOX_TRAY_WIN   0x6D1EE8
#define NOX_HEALTH_WIN 0x6D56D0 /* the bottom left panel; its child with the input proc 0x4660A0 (made at 0x465BBD,
                                   28 x 29 at 5, 11) is the round blue inventory button (click: toggle 0x4677B0) */
#define FN_INV_BTN_PROC 0x4660A0
#define FN_GEM_PROC    0x45F180 /* the spell set gems' input proc (+0x170: 0 previous, 1 next) */
#define NOX_PLAYER_DR  0x96A6A0
#define NOX_PLAYER_INF 0x96B8D0

static void box_lines(const wchar_t *const *l, int n, int x, int y) { /* bottom left at x, y */
    int fh = 0, w = 0;
    onc_text_size(L"Ag", NULL, &fh);
    for (int i = 0; i < n; i++) { int tw = 0; onc_text_size(l[i], &tw, NULL); if (tw > w) w = tw; }
    int h = n * (fh + 2) + 4, sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    y -= h;
    if (x > sw - w - 10) x = sw - w - 10;
    if (x < 2) x = 2;
    if (y > sh - h - 2) y = sh - h - 2;
    if (y < 2) y = 2;
    for (int k = 0; k < 2; k++) ((void (__cdecl *)(int, int, int, int))0x484850)(x, y, w + 8, h);
    onc_text_color(ONC_I32(NOX_COLOR_YELLOW));
    for (int i = 0; i < n; i++) onc_text(l[i], x + 4, y + 2 + i * (fh + 2));
}

/* above the spell bar (and the trap tray when it is shown) */
static void above_bar(int *x, int *y) {
    *x = ONC_I32(NOX_SCREEN_W) / 2 - 120; *y = ONC_I32(NOX_SCREEN_H) - 90;
    char *bar = *(char **)NOX_SPELLBAR;
    nox_win *s0 = bar ? *(nox_win **)(bar + 212) : NULL;
    if (s0 && onc_win_visible(s0)) { onc_rect r = onc_win_rect(s0); *x = r.x0; *y = r.y0 - 24; }
    nox_win *tray = *(nox_win **)NOX_TRAY_WIN;
    if (ONC_I32(NOX_TRAP_TRAY) == 1 && tray && onc_win_visible(tray)) {
        onc_rect r = onc_win_rect(tray);
        if (r.y0 - 6 < *y) *y = r.y0 - 6;
    }
}

/* the controls list (the inventory combo held): from the mapping itself, so it always says what the buttons do */
int onc_pad_help_combo(void);
static void controls_list(void) {
    enum { MAXR = 48 };
    static wchar_t keys[MAXR][24];
    const wchar_t *rows[MAXR][2];
    int n = 0;
    rows[n][0] = L"Left stick"; rows[n++][1] = L"move (push lightly to walk)";
    rows[n][0] = L"Right stick"; rows[n++][1] = L"cursor";
    rows[n][0] = L"R3 (hold) + right stick"; rows[n++][1] = L"zoom";
    int onc_pad_r3_click(void);
    if (onc_pad_r3_click()) { rows[n][0] = L"R3 (cursor shown)"; rows[n++][1] = L"left click"; }
    for (int c = 0; c < S_N && n < MAXR - 1; c++) {
        int a = onc_bind_action(c);
        if (!a) continue;
        wcsncpy(keys[n], onc_combo_label(c, 1), 23); keys[n][23] = 0;
        rows[n][0] = keys[n]; rows[n++][1] = onc_bind_title(a);
    }
    rows[n][0] = L"Select + Start"; rows[n++][1] = L"settings menu";
    int fh = 0, kw = 0, vw = 0;
    onc_text_size(L"Ag", NULL, &fh);
    int lh = fh + 2;
    for (int i = 0; i < n; i++) {
        int a = 0, b = 0;
        onc_text_size(rows[i][0], &a, NULL); onc_text_size(rows[i][1], &b, NULL);
        if (a > kw) kw = a;
        if (b > vw) vw = b;
    }
    const int pad = 10;
    int sh = ONC_I32(NOX_SCREEN_H), cols = pad * 2 + lh * (n + 2) > sh - 8 ? 2 : 1; /* two columns when tall */
    int per = (n + cols - 1) / cols, cw = kw + 16 + vw;
    int w = pad * 2 + cols * cw + (cols - 1) * 24, h = pad * 2 + lh * (per + 2);
    int x = (ONC_I32(NOX_SCREEN_W) - w) / 2, y = (sh - h) / 3;
    if (y < 4) y = 4;
    for (int k = 0; k < 2; k++) ((void (__cdecl *)(int, int, int, int))0x484850)(x, y, w, h);
    onc_draw_frame(x, y, w, h, onc_rgb(120, 120, 120));
    onc_text_color(ONC_I32(NOX_COLOR_YELLOW));
    onc_text(L"GAMEPAD CONTROLS", x + pad, y + pad);
    for (int i = 0; i < n; i++) {
        int col = i / per, yy = y + pad + lh * (i % per + 1) + lh / 2, xx = x + pad + col * (cw + 24);
        onc_text_color(ONC_I32(NOX_COLOR_YELLOW));
        onc_text(rows[i][0], xx, yy);
        onc_text_color(onc_rgb(255, 255, 255));
        onc_text(rows[i][1], xx + kw + 16, yy);
    }
    wchar_t foot[64];
    swprintf(foot, 64, L"(release %ls to close)", onc_combo_label(onc_pad_help_combo(), 1));
    onc_text_color(onc_rgb(170, 170, 170));
    onc_text(foot, x + pad, y + pad + lh * (per + 1) + lh / 2);
}

/* L1 held: what the L1 combos do */
static void l1_box(void) {
    static wchar_t lines[C_BASE_N][64];
    const wchar_t *l[C_BASE_N];
    int n = 0;
    for (int c = 0; c < C_BASE_N; c++) {
        int a = onc_bind_action(S_L1(c));
        if (!a) continue;
        swprintf(lines[n], 64, L"%ls: %ls", onc_combo_label(S_L1(c), 1), onc_bind_title(a));
        l[n] = lines[n]; n++;
    }
    if (!n) return;
    int x, y;
    above_bar(&x, &y);
    box_lines(l, n, x, y);
}

static void label_at(nox_win *w, const wchar_t *s, int where) { /* where: 0 over the bottom edge, 1 above, 2 under */
    if (!w || !s || !onc_win_visible(w)) return;
    onc_rect r = onc_win_rect(w);
    void *font = *(void **)0x6D24E8; /* the slot labels' font */
    int tw = 0, th = 0;
    ((int (__cdecl *)(void *, const wchar_t *, int *, int *, int))0x43F3C0)(font, s, &tw, &th, 0);
    int y = where == 0 ? r.y1 - th / 2 : where == 1 ? r.y0 - th - 1 : r.y1 + 1;
    if (y > ONC_I32(NOX_SCREEN_H) - th - 1) y = ONC_I32(NOX_SCREEN_H) - th - 1;
    onc_text_color(onc_rgb(255, 255, 255));
    ((int (__cdecl *)(void *, const wchar_t *, int, int))0x43F260)(font, s, (r.x0 + r.x1) / 2 - tw / 2, y);
}

static void hud_labels(void) {
    label_at(*(nox_win **)NOX_BOOK_BTN, onc_bind_label_action("book"), 0);
    /* the inventory button: its combo written out ("Select") above it */
    nox_win *hp = *(nox_win **)NOX_HEALTH_WIN;
    if (hp)
        for (nox_win *ch = NOXW_LASTCHILD(hp); ch; ch = NOXW_PREV(ch))
            if (NOXW_FUNC93(ch) == (void *)FN_INV_BTN_PROC) { label_at(ch, onc_bind_label_action_long("inventory"), 1); break; }
    /* the "lay trap" button (wizard / conjurer) */
    const wchar_t *trap = onc_bind_label_action("trap");
    char *avail = *(char **)0x6D2430;
    void *inf = *(void **)NOX_PLAYER_INF;
    int cl = inf ? *(uint8_t *)((char *)inf + 0x8CB) : 0;
    if (trap && avail && (cl == 1 || cl == 2) && (*(uint32_t *)(avail + 4) & 8)) {
        label_at(*(nox_win **)NOX_TRAP_BTN, trap, 0);
    }
    /* the spell set gems: the upper one's label above it, the lower one's under it */
    nox_win *q = *(nox_win **)NOX_QUICKBAR;
    if (q)
        for (nox_win *ch = NOXW_LASTCHILD(q); ch; ch = NOXW_PREV(ch)) {
            if (NOXW_FUNC93(ch) != (void *)FN_GEM_PROC || NOXW_W(ch) != 15) continue;
            int kind = NOXW_I32(ch, 0x170);
            const wchar_t *l = onc_bind_label_action(kind == 0 ? "prev_set" : "next_set");
            if (kind == 0 || kind == 1) label_at(ch, l, kind == 0 ? 1 : 2);
        }
    /* the consumables wheel's combo left of the cure poison potion */
    nox_win *pw = *(nox_win **)NOX_POISON_WIN;
    const wchar_t *wl = onc_bind_label_action("wheel");
    if (pw && wl && onc_win_visible(pw) && !onc_radial_active()) {
        onc_rect r = onc_win_rect(pw);
        int tw = 0, th = 0;
        void *font = *(void **)0x6D24E8;
        ((int (__cdecl *)(void *, const wchar_t *, int *, int *, int))0x43F3C0)(font, wl, &tw, &th, 0);
        onc_text_color(onc_rgb(255, 255, 255));
        ((int (__cdecl *)(void *, const wchar_t *, int, int))0x43F260)(font, wl, r.x0 - tw - 3, r.y1 - th - 2);
    }
}

void onc_hints_game(void) {
    static void *player;
    static DWORD load_at;
    void *pl = *(void **)NOX_PLAYER_DR;
    DWORD now = GetTickCount();
    if (pl != player) { player = pl; if (pl) load_at = now; }
    if (!g_on || !onc_pad_in_use() || !ONC_I32(NOX_CLIENT_CONNECTED) || !pl || !*(void **)NOX_PLAYER_INF) return;
    int busy = onc_inv_active() || onc_shop_mode() || onc_book_active() || onc_radial_active() || onc_summon_menu() ||
               (ONC_I32(NOX_GAME_FLAGS) & NOX_FLAG_PAUSE);
    onc_draw_begin();
    hud_labels();
    if (!busy) {
        if (onc_pad_help_shown()) controls_list();
        else if (onc_pad_l1_held()) l1_box();
        else if (now - load_at > 1500 && now - load_at < 9500) {
            const wchar_t *inv = onc_bind_label_action("inventory");
            wchar_t t[64];
            swprintf(t, 64, L"Hold %ls: gamepad controls", inv ? inv : L"Select");
            const wchar_t *l[] = {t};
            int x, y;
            above_bar(&x, &y);
            box_lines(l, 1, x, y);
        }
    }
    onc_draw_end();
}

/* the F7 menu */
int onc_hints_get(void) { return g_on; }
void onc_hints_set(int on) { g_on = on != 0; }
