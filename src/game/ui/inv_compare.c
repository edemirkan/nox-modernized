#include "inv_compare.h"
#include "inv_tabs.h"
#include "inventory.h"
#include "draw.h"
#include "shop.h"
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

#define COLS 4
#define ROWS 21
#define CELL 50

/* the identify panel (OpenNox sub_4627F0): builds the item's text into a list box when the hovered item changes */
#define FN_IDENTIFY      0x462340 /* identify(int pos[2]): draws a 200x200 black box at pos + (11, 15) */
#define NOX_ID_ITEM      0x6D5960 /* the item to identify (drawable) */
#define NOX_ID_SHOWN     0x6D5964 /* the item the list box shows */
#define NOX_ID_NAME      0x6D5968 /* wchar[256]: the item's name */
#define NOX_ID_PANEL     0x6D56E0 /* the identify window; child 9156 is the text list box */
#define ID_TEXT_LIST     9156
#define NOX_CURSOR_TYPE  0x6F7300 /* the identify code sets the cursor type */
#define FN_CHILD_BY_ID   0x4889D0 /* window *childById(win, id) */
#define FN_LOAD_STRING   0x40F0D0 /* wchar *loadString(name, 0, file, line) */
#define LB_ADD 0x400D
#define LB_CLEAR 0x400F

#define CLASS_WAND   0x1000
#define CLASS_WEAPON 0x1000000
#define CLASS_ARMOR  0x2000000
#define WEAPON_QUIVER 0x2

#define PANEL_W 220
#define GAP 6
#define PAD 8
#define MAXL 40

static int g_on = 1;

int onc_compare_enabled(void) { return g_on; }
void onc_compare_init(void) { g_on = onc_cfg_int("inventory", "compare", 1); }

/* ---- the identify text, captured from the game ---- */
typedef struct {
    void *dr;
    int netcode, equipped;
    wchar_t name[128];
    int n;
    wchar_t line[MAXL][160];
} info;

static info *g_cap;

static int __cdecl capture_proc(nox_win *w, int code, uintptr_t a1, uintptr_t a2) {
    if (!g_cap) return 0;
    if (code == LB_CLEAR) g_cap->n = 0;
    else if (code == LB_ADD && a1 && g_cap->n < MAXL) {
        wcsncpy(g_cap->line[g_cap->n], (const wchar_t *)a1, 159);
        g_cap->line[g_cap->n][159] = 0;
        g_cap->n++;
    }
    return 0;
}

static int identify_dr(void *dr, int netcode, int equipped, info *out, int px, int py) {
    memset(out, 0, sizeof *out);
    nox_win *idw = *(nox_win **)NOX_ID_PANEL;
    if (!dr || !idw) return 0;
    nox_win *lb = ((nox_win *(__cdecl *)(nox_win *, int))FN_CHILD_BY_ID)(idw, ID_TEXT_LIST);
    if (!lb) return 0;
    out->dr = dr;
    out->netcode = netcode;
    out->equipped = equipped;
    /* everything the identify code changes is put back afterwards */
    static wchar_t name_save[256];
    memcpy(name_save, (void *)NOX_ID_NAME, sizeof name_save);
    void *was_item = *(void **)NOX_ID_ITEM, *was_shown = *(void **)NOX_ID_SHOWN;
    int32_t was_cursor = ONC_I32(NOX_CURSOR_TYPE), was_net = *(int32_t *)((char *)dr + 128);
    void *was_proc = NOXW_FUNC94(lb);
    *(int32_t *)((char *)dr + 128) = out->netcode; /* as the tray does before identifying */
    *(void **)NOX_ID_ITEM = dr;
    *(void **)NOX_ID_SHOWN = NULL;
    NOXW_FUNC94(lb) = (void *)capture_proc;
    g_cap = out;
    int pos[2] = {px - 11, py - 15};
    onc_draw_clip(px, py, 1, 1); /* its black box: one pixel, under our panel's frame */
    ((int (__cdecl *)(int *))FN_IDENTIFY)(pos);
    onc_draw_clip(0, 0, ONC_I32(NOX_SCREEN_W), ONC_I32(NOX_SCREEN_H));
    g_cap = NULL;
    NOXW_FUNC94(lb) = was_proc;
    wcsncpy(out->name, (const wchar_t *)NOX_ID_NAME, 127);
    memcpy((void *)NOX_ID_NAME, name_save, sizeof name_save);
    *(int32_t *)((char *)dr + 128) = was_net;
    ONC_I32(NOX_CURSOR_TYPE) = was_cursor;
    *(void **)NOX_ID_ITEM = was_item;
    /* the identify panel's own list box was not updated: it rebuilds the next time it is shown */
    *(void **)NOX_ID_SHOWN = was_shown == was_item ? (void *)-1 : was_shown;
    while (out->n && !out->line[out->n - 1][0]) out->n--;
    return 1;
}

/* an inventory grid cell: +0 drawable, +4 net code, +132 equipped */
static int identify(char *cell, info *out, int px, int py) {
    return identify_dr(*(void **)cell, *(int32_t *)(cell + 4), *(int32_t *)(cell + 132) != 0, out, px, py);
}

/* ---- what is under the selection ---- */
static char *grid_cell(int idx) { return idx < 0 ? NULL : (char *)NOX_INV_GRID + idx * 148; }
static int has_item(char *c) { return c && *(void **)c && *(uint8_t *)(c + 140); }

static int hover_cell(void) {
    nox_win *panel = *(nox_win **)NOX_INV_PANEL;
    if (!panel) return -1;
    onc_rect pr = onc_win_rect(panel);
    int rx = ONC_I32(NOX_MOUSE_X) - (pr.x0 + 314), ry = ONC_I32(NOX_MOUSE_Y) - (pr.y0 + 13);
    if (rx < 0 || ry < 0 || rx >= COLS * CELL || ry >= 3 * CELL) return -1;
    int idx = onc_tabs_real(rx / CELL, (ry + ONC_I32(NOX_INV_SCROLL)) / CELL);
    return has_item(grid_cell(idx)) ? idx : -1;
}

static int equipped_for_item(void *it, int idx);
static int equipped_for(int idx) { return equipped_for_item(*(void **)grid_cell(idx), idx); }

/* the equipped inventory item of the same kind as an item (skipping inventory cell idx, -1: none) */
static int equipped_for_item(void *it, int idx) {
    uint32_t cls = *(uint32_t *)((char *)it + 112), sub = *(uint32_t *)((char *)it + 116);
    for (int i = 0; i < COLS * ROWS; i++) {
        char *c = grid_cell(i);
        if (i == idx || !has_item(c) || !*(int32_t *)(c + 132)) continue;
        void *e = *(void **)c;
        uint32_t ec = *(uint32_t *)((char *)e + 112), es = *(uint32_t *)((char *)e + 116);
        if ((cls & (CLASS_WEAPON | CLASS_WAND)) && (ec & (CLASS_WEAPON | CLASS_WAND))) {
            int q1 = (cls & CLASS_WEAPON) && (sub & WEAPON_QUIVER), q2 = (ec & CLASS_WEAPON) && (es & WEAPON_QUIVER);
            if (q1 == q2) return i;
        } else if ((cls & CLASS_ARMOR) && (ec & CLASS_ARMOR) && (sub & es)) {
            return i;
        }
    }
    return -1;
}

/* ---- comparing lines: "Label: 12", "Label: 12.5", "Durability: 30/40" ---- */
static wchar_t g_weight_label[64];

static void label_of(const wchar_t *s, wchar_t *out, int n) {
    int k = 0;
    while (*s == L' ') s++;
    for (; *s && k < n - 1; s++) {
        if ((*s >= L'0' && *s <= L'9') || *s == L'%') break;
        out[k++] = *s;
    }
    while (k && (out[k - 1] == L' ' || out[k - 1] == L'+' || out[k - 1] == L'-')) k--;
    out[k] = 0;
}

static int value_of(const wchar_t *s, double *v) {
    const wchar_t *p = s;
    while (*p && !(*p >= L'0' && *p <= L'9')) p++;
    if (!*p) return 0;
    wchar_t *end;
    double a = wcstod(p, &end);
    if (*end == L'/') { /* current / maximum: the maximum */
        double b = wcstod(end + 1, &end);
        a = b;
    }
    *v = a;
    return 1;
}

static int compare_line(const wchar_t *s, const info *eq, double *diff, int *better) {
    if (!eq) return 0;
    wchar_t lab[96], lab2[96];
    double v, v2;
    label_of(s, lab, 96);
    if (!lab[0] || !value_of(s, &v)) return 0;
    for (int i = 0; i < eq->n; i++) {
        label_of(eq->line[i], lab2, 96);
        if (wcscmp(lab, lab2) || !value_of(eq->line[i], &v2)) continue;
        *diff = v - v2;
        if (*diff > -0.05 && *diff < 0.05) return 0;
        int lower_better = g_weight_label[0] && !wcscmp(lab, g_weight_label);
        *better = lower_better ? *diff < 0 : *diff > 0;
        return 1;
    }
    return 0;
}

/* ---- drawing ---- */
static int g_white, g_green, g_red, g_dim, g_yellow, g_gold, g_bg, g_fh;

/* draws (or only measures) a wrapped text; returns its height */
static int text_wrapped(const wchar_t *s, int x, int y, int w, int draw) {
    wchar_t buf[200];
    int h = 0, k = 0, ind = 0;
    while (s[ind] == L' ') ind++; /* indented lines keep their indent on every row */
    int indent = 0;
    if (ind) onc_text_size(L"  ", &indent, NULL);
    const wchar_t *p = s;
    while (*p) {
        /* the longest run of words that fits */
        int best = 0, i = 0;
        k = 0;
        while (p[i]) {
            int j = i;
            while (p[j] == L' ') j++;
            while (p[j] && p[j] != L' ') j++;
            if (j >= 199) break;
            wmemcpy(buf, p, j);
            buf[j] = 0;
            int tw = 0;
            onc_text_size(buf, &tw, NULL);
            if (tw > w - (h ? indent : 0) && best) break;
            best = j;
            i = j;
            if (tw > w) break;
        }
        if (!best) best = (int)wcslen(p) < 199 ? (int)wcslen(p) : 199;
        wmemcpy(buf, p, best);
        buf[best] = 0;
        if (draw) onc_text(buf, x + (h ? indent : 0), y + h);
        h += g_fh;
        p += best;
        while (*p == L' ') p++;
        (void)k;
    }
    return h ? h : g_fh / 2;
}

static int draw_panel(const info *it, const wchar_t *title, const info *eq, int x, int y, int draw) {
    int tw = PANEL_W - 2 * PAD, cy = y + PAD;
    if (draw) { onc_text_color(g_yellow); onc_text(title, x + PAD, cy); }
    cy += g_fh + 2;
    if (draw) onc_text_color(g_white);
    cy += text_wrapped(it->name, x + PAD, cy, tw, draw) + g_fh / 2;
    for (int i = 0; i < it->n; i++) {
        const wchar_t *s = it->line[i];
        if (!s[0]) { cy += g_fh / 2; continue; }
        double d;
        int better;
        if (compare_line(s, eq, &d, &better)) {
            wchar_t ds[32];
            double a = d < 0 ? -d : d;
            if (a - (int)(a + 0.5) > -0.05 && a - (int)(a + 0.5) < 0.05) swprintf(ds, 32, L" (%lc%d)", d < 0 ? L'-' : L'+', (int)(a + 0.5));
            else swprintf(ds, 32, L" (%lc%.1f)", d < 0 ? L'-' : L'+', a);
            int lw = 0;
            onc_text_size(s, &lw, NULL);
            if (draw) {
                onc_text_color(g_white);
                onc_text(s, x + PAD, cy);
                onc_text_color(better ? g_green : g_red);
                onc_text(ds, x + PAD + lw, cy);
            }
            cy += g_fh;
        } else {
            if (draw) onc_text_color(i == it->n - 1 ? g_dim : g_white); /* the last line: the description */
            cy += text_wrapped(s, x + PAD, cy, tw, draw);
        }
    }
    return cy + PAD - y;
}

static void box(int x, int y, int h) {
    onc_draw_color(g_bg);
    onc_draw_rect(x, y, PANEL_W, h);
    onc_draw_frame(x, y, PANEL_W, h, g_gold);
}

/* where the panels go: right of the open inventory, or under it when the screen is too narrow (the same place
   while buying, when the inventory may be closed) */
static void panel_origin(int *x, int *y) {
    nox_win *root = *(nox_win **)(NOX_INV_PANEL - 4);
    onc_rect rr = {0, 0, 563, 264};
    if (root && onc_win_visible(root) && *(volatile uint8_t *)NOX_INV_STATE == 2) rr = onc_win_rect(root);
    else {
        int t = onc_tabs_enabled() ? 22 : 0; /* the tab strip (inv_tabs.c STRIP_H) */
        rr.y0 += t; rr.y1 += t;
    }
    *x = rr.x0 + 563 + GAP; *y = rr.y0;
    if (*x + 2 * PANEL_W + GAP > ONC_I32(NOX_SCREEN_W)) { *x = rr.x0 + GAP; *y = rr.y1 + GAP + 18; } /* under the hint line */
}

/* ---- per frame ---- */
static info g_sel, g_eq;
static int g_has_eq, g_last_idx = -2, g_last_eq = -2;
static DWORD g_last_time;

void onc_compare_draw(void) {
    if (!g_on || !ONC_I32(NOX_CLIENT_CONNECTED) || *(volatile uint8_t *)NOX_INV_STATE != 2) return;
    if (*(volatile uint8_t *)NOX_INV_JOURNAL) return;
    nox_win *root = *(nox_win **)(NOX_INV_PANEL - 4);
    nox_win *panel = *(nox_win **)NOX_INV_PANEL;
    if (!root || !panel || !onc_win_visible(panel)) return;
    int idx = hover_cell();
    void *doll = idx < 0 ? onc_inv_doll_selected() : NULL;
    if (idx < 0 && !doll) { g_last_idx = -2; return; }
    if (!g_weight_label[0]) {
        const wchar_t *f = ((const wchar_t *(__cdecl *)(const char *, int, const char *, int))FN_LOAD_STRING)(
            "IdentifyWeight", 0, "guiinv.c", 2620);
        if (f) label_of(f, g_weight_label, 64);
    }
    int sh = ONC_I32(NOX_SCREEN_H);
    int x, y;
    panel_origin(&x, &y);
    onc_draw_begin();
    g_white = onc_rgb(230, 230, 230); g_green = onc_rgb(80, 220, 80); g_red = onc_rgb(235, 70, 60);
    g_dim = onc_rgb(160, 160, 160); g_gold = onc_rgb(150, 120, 60); g_bg = onc_rgb(0, 0, 0);
    g_yellow = ONC_I32(NOX_COLOR_YELLOW);
    onc_text_size(L"Ag", NULL, &g_fh);
    if (g_fh <= 0) g_fh = 10;
    if (doll) {
        /* an item on the equipment doll: its own panel */
        DWORD now = GetTickCount();
        if (g_last_idx != -3 || doll != g_sel.dr || now - g_last_time > 500) {
            identify_dr(doll, *(int32_t *)((char *)doll + 128), 1, &g_sel, x, y);
            g_last_idx = -3; g_last_time = now;
        }
        int h = draw_panel(&g_sel, L"Equipped", NULL, x, y, 0);
        if (y + h > sh) h = sh - y;
        box(x, y, h);
        draw_panel(&g_sel, L"Equipped", NULL, x, y, 1);
        onc_draw_end();
        return;
    }
    int eq = equipped_for(idx);
    DWORD now = GetTickCount();
    char *c = grid_cell(idx);
    if (idx != g_last_idx || eq != g_last_eq || *(void **)c != g_sel.dr || now - g_last_time > 500) {
        /* the identify code draws a black box: inside our panel, which is drawn over it */
        identify(c, &g_sel, x, y);
        g_has_eq = eq >= 0 && identify(grid_cell(eq), &g_eq, x, y);
        g_last_idx = idx; g_last_eq = eq; g_last_time = now;
    }
    const info *cmp = g_has_eq && !g_sel.equipped ? &g_eq : NULL;
    int h1 = draw_panel(&g_sel, g_sel.equipped ? L"Selected (equipped)" : L"Selected", cmp, x, y, 0);
    if (y + h1 > sh) h1 = sh - y;
    box(x, y, h1);
    draw_panel(&g_sel, g_sel.equipped ? L"Selected (equipped)" : L"Selected", cmp, x, y, 1);
    if (cmp) {
        int x2 = x + PANEL_W + GAP;
        int h2 = draw_panel(&g_eq, L"Equipped", NULL, x2, y, 0);
        if (y + h2 > sh) h2 = sh - y;
        box(x2, y, h2);
        draw_panel(&g_eq, L"Equipped", NULL, x2, y, 1);
    }
    onc_draw_end();
}

/* ---- a vendor's item while buying (drawn after the whole GUI) ---- */
#define NOX_SHOP_GRID   0x6F31B0
#define NOX_SHOP_CELLS  0x6F32B0 /* 6 x 10 x 140, column-major: +0 drawable, +4 count, +8 net codes, +136 price */
#define NOX_SHOP_SCROLL 0x6F5380

static info g_vsel, g_veq;
static int g_vhas_eq, g_vlast = -2, g_vlast_eq = -2, g_vprice;
static DWORD g_vtime;

void onc_compare_draw_shop(void) {
    if (!g_on || onc_shop_mode() != SHOP_BUY || !onc_shop_on_top()) { g_vlast = -2; return; }
    int32_t *g = (int32_t *)NOX_SHOP_GRID;
    int mx = ONC_I32(NOX_MOUSE_X), my = ONC_I32(NOX_MOUSE_Y);
    if (mx < g[0] || mx >= g[2] || my < g[1] || my >= g[3]) { g_vlast = -2; return; }
    int col = (mx - g[0]) / CELL, row = (my - g[1] + ONC_I32(NOX_SHOP_SCROLL)) / CELL;
    if (col > 5) col = 5;
    if (row > 9) row = 9;
    int vi = row + 10 * col;
    char *c = (char *)NOX_SHOP_CELLS + vi * 140;
    void *dr = *(void **)c;
    if (!dr || !*(int32_t *)(c + 4)) { g_vlast = -2; return; }
    if (!g_weight_label[0]) {
        const wchar_t *f = ((const wchar_t *(__cdecl *)(const char *, int, const char *, int))FN_LOAD_STRING)(
            "IdentifyWeight", 0, "guiinv.c", 2620);
        if (f) label_of(f, g_weight_label, 64);
    }
    /* the same place as the inventory's panels */
    int sh = ONC_I32(NOX_SCREEN_H);
    int x, y;
    panel_origin(&x, &y);
    onc_draw_begin();
    g_white = onc_rgb(230, 230, 230); g_green = onc_rgb(80, 220, 80); g_red = onc_rgb(235, 70, 60);
    g_dim = onc_rgb(160, 160, 160); g_gold = onc_rgb(150, 120, 60); g_bg = onc_rgb(0, 0, 0);
    g_yellow = ONC_I32(NOX_COLOR_YELLOW);
    onc_text_size(L"Ag", NULL, &g_fh);
    if (g_fh <= 0) g_fh = 10;
    int eq = equipped_for_item(dr, -1);
    DWORD now = GetTickCount();
    if (vi != g_vlast || eq != g_vlast_eq || dr != g_vsel.dr || now - g_vtime > 500) {
        identify_dr(dr, *(int32_t *)(c + 8), 0, &g_vsel, x, y);
        g_vhas_eq = eq >= 0 && identify(grid_cell(eq), &g_veq, x, y);
        g_vprice = *(int32_t *)(c + 136);
        g_vlast = vi; g_vlast_eq = eq; g_vtime = now;
    }
    wchar_t title[64];
    swprintf(title, 64, L"Vendor - %d gold", g_vprice);
    const info *cmp = g_vhas_eq ? &g_veq : NULL;
    int h1 = draw_panel(&g_vsel, title, cmp, x, y, 0);
    if (y + h1 > sh) h1 = sh - y;
    box(x, y, h1);
    draw_panel(&g_vsel, title, cmp, x, y, 1);
    if (cmp) {
        int x2 = x + PANEL_W + GAP;
        int h2 = draw_panel(&g_veq, L"Equipped", NULL, x2, y, 0);
        if (y + h2 > sh) h2 = sh - y;
        box(x2, y, h2);
        draw_panel(&g_veq, L"Equipped", NULL, x2, y, 1);
    }
    onc_draw_end();
}
