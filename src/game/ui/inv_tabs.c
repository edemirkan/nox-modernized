#include "inv_tabs.h"
#include "menu_nav.h"
#include "inventory.h"
#include "draw.h"
#include "inv_compare.h"
#include <string.h>

#define COLS 4
#define ROWS 21
#define CELLS (COLS * ROWS)
#define CELL_SIZE 148
#define STRIP_H 22             /* height of the tab strip, game pixels (the game's UI is not scaled) */
#define INV_W 563              /* the inventory's width */

#define NOX_INV_ROOT    0x6D56C8 /* the inventory's top window (the panel's parent) */
#define NOX_INV_JBUTTON 0x6D5714 /* the journal / items switch next to the portrait */
#define NOX_INV_SLIDE   0x5A5F88 /* the inventory's slide in / out: -225 (closed) .. 0 (open) */
#define FN_PANEL_DRAW   0x462F80 /* the panel's draw proc */
#define CALL_ROOT_POS   0x462F9B /* in it: setPos(root, 0, slide) */
#define FN_SET_POS      0x4882C0
#define FN_SOUND        0x4528D0 /* play a UI sound (id, volume) */
#define SND_CLICK       0x2FE

static const wchar_t *k_names[TAB_N] = {L"Journal", L"Weapons", L"Head & Body", L"Legs & Feet", L"Consumables", L"Misc"};

static int g_on = 1;
static int g_tab = TAB_WEAPONS, g_last_item_tab = TAB_WEAPONS;
static int g_patched;

/* ---- which tab an item belongs to ---- */
#define CLASS_FOOD   0x10
#define CLASS_WAND   0x1000
#define CLASS_WEAPON 0x1000000
#define CLASS_ARMOR  0x2000000
#define ARMOR_HELMET 0x1
#define ARMOR_SHIELD 0x2
#define ARMOR_BREAST 0x4
#define ARMOR_ARMS   0x8
#define ARMOR_PANTS  0x10
#define ARMOR_BOOTS  0x20
#define ARMOR_SHIRT  0x40
#define ARMOR_LEGS   0x80
#define ARMOR_BACK   0x100

static int item_tab(void *dr) {
    uint32_t cls = *(uint32_t *)((char *)dr + 112), sub = *(uint32_t *)((char *)dr + 116);
    if (cls & (CLASS_WEAPON | CLASS_WAND)) return TAB_WEAPONS;
    if (cls & CLASS_ARMOR) {
        if (sub & ARMOR_SHIELD) return TAB_WEAPONS; /* weapons and shields */
        if (sub & (ARMOR_HELMET | ARMOR_BREAST | ARMOR_ARMS | ARMOR_SHIRT | ARMOR_BACK)) return TAB_UPPER;
        if (sub & (ARMOR_PANTS | ARMOR_BOOTS | ARMOR_LEGS)) return TAB_LOWER;
        return TAB_MISC;
    }
    if (cls & CLASS_FOOD) return TAB_CONSUMABLES; /* food and potions */
    return TAB_MISC;
}

static char *real_cell(int idx) { return (char *)NOX_INV_GRID + idx * CELL_SIZE; }
static int has_item(const char *c) { return *(void *const *)c && *(const uint8_t *)(c + 140); }

/* ---- the filtered view ---- */
static int16_t g_map[CELLS]; /* view cell -> real cell (-1: nothing) */
static int g_items;

int onc_tabs_enabled(void) { return g_on && g_patched; }

int onc_tabs_filtering(void) {
    return onc_tabs_enabled() && g_tab != TAB_JOURNAL && ONC_I32(NOX_CLIENT_CONNECTED) &&
           *(volatile uint8_t *)NOX_INV_STATE != 0;
}

/* view positions are filled row by row (4 per row): the tab's items in their grid order, then the empty cells */
#define NOX_INV_DRAGGED 0x6D258C /* the item being dragged out of the grid (set at 0x4655C6), 0 = none */
static int g_map_valid, g_map_tab = -1;

static void build_view(void) {
    /* while an item is dragged the view stays as it was when it was lifted: the game remembers the cell it came
       from (0x6D2558 / 0x6D255C, view cells) and puts it back there or moves it, and the cells must not shift
       meanwhile (an item lifted, the next one moved up into its place, and the dragged one was lost) */
    if (ONC_I32(NOX_INV_DRAGGED) && g_map_valid && g_map_tab == g_tab) return;
    int16_t items[CELLS], empty[CELLS];
    int ni = 0, ne = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            int idx = r + ROWS * c;
            char *cl = real_cell(idx);
            if (!has_item(cl)) empty[ne++] = (int16_t)idx;
            else if (item_tab(*(void **)cl) == g_tab) items[ni++] = (int16_t)idx;
        }
    for (int i = 0; i < CELLS; i++) g_map[i] = -1;
    int k = 0;
    for (int i = 0; i < ni; i++, k++) g_map[(k / COLS) + ROWS * (k % COLS)] = items[i];
    for (int i = 0; i < ne; i++, k++) g_map[(k / COLS) + ROWS * (k % COLS)] = empty[i];
    g_items = ni;
    g_map_valid = 1;
    g_map_tab = g_tab;
}

int onc_tabs_items(void) {
    if (!onc_tabs_filtering()) return -1;
    build_view();
    return g_items;
}

int onc_tabs_real(int col, int row) {
    if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return -1;
    if (!onc_tabs_filtering()) return row + ROWS * col;
    build_view();
    return g_map[row + ROWS * col];
}

/* the view is in the game's grid only while one of the inventory's procs runs */
static uint8_t g_real[CELLS * CELL_SIZE], g_view[CELLS * CELL_SIZE];
static int g_depth, g_swapped;

static void enter(void) {
    if (g_depth++ || !onc_tabs_filtering()) return;
    build_view();
    char *grid = (char *)NOX_INV_GRID;
    memcpy(g_real, grid, sizeof g_real);
    for (int v = 0; v < CELLS; v++) {
        if (g_map[v] >= 0) memcpy(grid + v * CELL_SIZE, g_real + g_map[v] * CELL_SIZE, CELL_SIZE);
        else memset(grid + v * CELL_SIZE, 0, CELL_SIZE);
    }
    g_swapped = 1;
}

static void leave(void) {
    if (--g_depth || !g_swapped) return;
    g_swapped = 0;
    char *grid = (char *)NOX_INV_GRID;
    memcpy(g_view, grid, sizeof g_view);
    memcpy(grid, g_real, sizeof g_real);
    for (int v = 0; v < CELLS; v++)
        if (g_map[v] >= 0) memcpy(grid + g_map[v] * CELL_SIZE, g_view + v * CELL_SIZE, CELL_SIZE);
    /* something the game put in a view cell that shows nothing (past the tab's items and the empty cells):
       into a shown cell that is empty now, so nothing is lost */
    for (int v = 0; v < CELLS; v++) {
        if (g_map[v] >= 0 || !has_item((char *)g_view + v * CELL_SIZE)) continue;
        int put = 0;
        for (int u = 0; u < CELLS && !put; u++) {
            int r = g_map[u];
            if (r >= 0 && !has_item(grid + r * CELL_SIZE)) {
                memcpy(grid + r * CELL_SIZE, g_view + v * CELL_SIZE, CELL_SIZE);
                put = 1;
            }
        }
        if (!put) onc_log("tabs: an item in view cell %d has no place", v);
    }
}

/* ---- every proc of the inventory's windows runs with the view in the grid ---- */
typedef int (__cdecl *proc4)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
#define NWRAP 32
static void *g_orig[NWRAP];
static int g_norig;

static int call_wrapped(int i, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    enter();
    int r = ((proc4)g_orig[i])(a, b, c, d);
    leave();
    return r;
}
#define W(i) static int __cdecl w##i(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) { return call_wrapped(i, a, b, c, d); }
W(0) W(1) W(2) W(3) W(4) W(5) W(6) W(7) W(8) W(9) W(10) W(11) W(12) W(13) W(14) W(15)
W(16) W(17) W(18) W(19) W(20) W(21) W(22) W(23) W(24) W(25) W(26) W(27) W(28) W(29) W(30) W(31)
static void *const g_wrap[NWRAP] = {w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15,
                                    w16, w17, w18, w19, w20, w21, w22, w23, w24, w25, w26, w27, w28, w29, w30, w31};

static void *wrapper_for(void *orig) {
    for (int i = 0; i < NWRAP; i++)
        if (g_wrap[i] == orig) return orig; /* already ours */
    for (int i = 0; i < g_norig; i++)
        if (g_orig[i] == orig) return g_wrap[i];
    if (g_norig >= NWRAP) return orig;
    g_orig[g_norig] = orig;
    return g_wrap[g_norig++];
}

static void wrap_window(nox_win *w, int depth) {
    if (!w || depth > 6) return;
    static const int fields[] = {0x174, 0x178, 0x17C, 0x180}; /* input, notifications, draw, tooltip */
    for (int i = 0; i < 4; i++) {
        void **f = (void **)((char *)w + fields[i]);
        if (*f && *f != (void *)FN_PANEL_DRAW) *f = wrapper_for(*f); /* the panel draw is hooked itself */
    }
    for (nox_win *ch = NOXW_LASTCHILD(w); ch; ch = NOXW_PREV(ch)) wrap_window(ch, depth + 1);
}

/* ---- the tab strip ---- */
/* geometry: the strip above the inventory, hint space for "L1" / "R1" at both ends, equal tabs between */
static int strip_layout(int *x0, int *y0, int *hint, int *tw) {
    nox_win *root = *(nox_win **)NOX_INV_ROOT;
    if (!root || !onc_win_visible(root)) return 0;
    onc_rect rr = onc_win_rect(root);
    *x0 = rr.x0; *y0 = rr.y0 - STRIP_H;
    int hw = 0;
    onc_text_size(L"R1", &hw, NULL);
    *hint = hw + 12;
    *tw = (INV_W - 2 * *hint) / TAB_N;
    return 1;
}

static void draw_strip(void) {
    int x0, y0, hint, tw;
    if (!strip_layout(&x0, &y0, &hint, &tw) || y0 + STRIP_H <= 0) return;
    onc_draw_begin();
    int black = onc_rgb(0, 0, 0), gold = onc_rgb(150, 120, 60), dim = onc_rgb(150, 150, 150);
    int sel_bg = onc_rgb(70, 52, 22), yellow = ONC_I32(NOX_COLOR_YELLOW);
    onc_draw_color(black);
    onc_draw_rect(x0, y0, INV_W, STRIP_H);
    onc_draw_color(gold);
    onc_draw_rect(x0, y0 + STRIP_H - 1, INV_W, 1);
    int th = 0;
    onc_text_size(L"L1", NULL, &th);
    int ty = y0 + (STRIP_H - th) / 2;
    if (onc_pad_in_use()) { /* the pad's buttons for switching, only while the pad is used */
        onc_text_color(dim);
        onc_text(L"L1", x0 + 6, ty);
        onc_text(L"R1", x0 + INV_W - hint + 6, ty);
    }
    for (int i = 0; i < TAB_N; i++) {
        int x = x0 + hint + i * tw, w = 0;
        if (i == g_tab) {
            onc_draw_color(sel_bg);
            onc_draw_rect(x + 1, y0 + 2, tw - 2, STRIP_H - 3);
            onc_draw_frame(x + 1, y0 + 2, tw - 2, STRIP_H - 2, gold);
        }
        onc_text_size(k_names[i], &w, NULL);
        onc_text_color(i == g_tab ? yellow : dim);
        onc_text(k_names[i], x + (tw - w) / 2, ty);
    }
    onc_draw_end();
}

static void set_tab(int t);

/* a real left click (dinput proxy): on a tab, switch to it; anywhere on the strip, the game does not get it */
int onc_tabs_mouse_click(int x, int y) {
    if (!onc_tabs_enabled() || !ONC_I32(NOX_CLIENT_CONNECTED) || *(volatile uint8_t *)NOX_INV_STATE != 2) return 0;
    int x0, y0, hint, tw;
    if (!strip_layout(&x0, &y0, &hint, &tw)) return 0;
    if (x < x0 || x >= x0 + INV_W || y < y0 || y >= y0 + STRIP_H) return 0;
    int i = (x - x0 - hint) / tw;
    if (x >= x0 + hint && i >= 0 && i < TAB_N && i != g_tab) set_tab(i);
    return 1;
}

/* ---- hooks ---- */
typedef int (__cdecl *draw_fn)(nox_win *, void *);
static draw_fn g_panel_draw;

static int __cdecl panel_draw(nox_win *w, void *dd) {
    if (onc_tabs_enabled()) {
        nox_win *root = *(nox_win **)NOX_INV_ROOT;
        if (root) wrap_window(root, 0);
    }
    enter();
    int r = g_panel_draw(w, dd);
    leave();
    if (onc_tabs_enabled()) draw_strip();
    onc_compare_draw();
    return r;
}

/* the whole GUI: drawn, then our overlays on top */
#define FN_GUI_DRAW_ALL 0x489BF0
static void (__cdecl *g_gui_draw)(void);
void onc_labels_frame(void), onc_hints_draw(void), onc_radial_draw(void), onc_book_draw(void), onc_summon_draw(void);
static void __cdecl gui_draw_all(void) {
    g_gui_draw();
    onc_menu_draw_overlay(); /* gamepad list mode: frame and darkening */
    onc_compare_draw_shop();
    onc_labels_frame();
    onc_hints_draw();
    onc_radial_draw();
    onc_book_draw();
    onc_summon_draw();
    void onc_settings_draw(void);
    onc_settings_draw(); /* the F7 menu, on top */
}

/* the panel places the inventory at (0, slide): it goes down by the strip height as it slides in */
static int __cdecl root_set_pos(nox_win *w, int x, int y) {
    if (onc_tabs_enabled()) {
        int s = y + 225;
        if (s < 0) s = 0;
        if (s > 225) s = 225;
        y += STRIP_H * s / 225;
    }
    return ((int (__cdecl *)(nox_win *, int, int))FN_SET_POS)(w, x, y);
}

void onc_tabs_init(void) {
    g_on = onc_cfg_int("inventory", "tabs", 1);
    onc_compare_init();
    {   /* overlays after the whole GUI: vendor comparison, label refresh, gamepad hints */
        static const uint8_t gui_start[] = {0x8B, 0x0D, 0xE8, 0x73, 0x6F, 0x00}; /* mov ecx, [window list head] */
        g_gui_draw = (void (__cdecl *)(void))onc_hook_detour("GUI draw", FN_GUI_DRAW_ALL, gui_draw_all, gui_start, 6);
    }
    if (!g_on && !onc_compare_enabled()) return;
    static const uint8_t draw_start[] = {0xA1, 0x88, 0x5F, 0x5A, 0x00}; /* mov eax, [slide] */
    g_panel_draw = (draw_fn)onc_hook_detour("inventory panel draw", FN_PANEL_DRAW, panel_draw, draw_start, 5);
    if (!g_panel_draw || !g_on) return;
    if (!onc_hook_call("inventory position", CALL_ROOT_POS, root_set_pos, FN_SET_POS)) return;
    g_patched = 1;
}

/* ---- switching tabs ---- */
static void journal_show(int on) {
    if ((*(volatile uint8_t *)NOX_INV_JOURNAL != 0) == on) return;
    nox_win *panel = *(nox_win **)NOX_INV_PANEL, *btn = *(nox_win **)NOX_INV_JBUTTON;
    nox_winproc f = panel ? (nox_winproc)NOXW_FUNC94(panel) : NULL;
    if (f && btn) f(panel, 0x4007, (uintptr_t)btn, 0);
}

void onc_tabs_sync(void) {
    if (!onc_tabs_enabled()) return;
    int j = *(volatile uint8_t *)NOX_INV_JOURNAL != 0;
    if (j && g_tab != TAB_JOURNAL) g_tab = TAB_JOURNAL;
    else if (!j && g_tab == TAB_JOURNAL) g_tab = g_last_item_tab;
}

void onc_tabs_step(int d) {
    if (!onc_tabs_enabled() || !d) return;
    set_tab(((g_tab + d) % TAB_N + TAB_N) % TAB_N);
}

static void set_tab(int t) {
    g_tab = t;
    if (t != TAB_JOURNAL) g_last_item_tab = t;
    journal_show(t == TAB_JOURNAL);
    if (t != TAB_JOURNAL) onc_inv_scroll_top();
    ((void (__cdecl *)(int, int))FN_SOUND)(SND_CLICK, 100);
    onc_log("inventory tab %d", t);
}
