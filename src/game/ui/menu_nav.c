/* Gamepad menu navigation, through the game's own window system: while a menu is open, the D-pad (or the left
   stick, in a game) moves a selection between the menu's buttons, A presses the selected one with the game's
   own keyboard handling of that button (its input proc gets Enter, as with the keyboard), B goes back.
   The selection is shown by putting the cursor on the button (the game's own hover highlight and tooltip).
   Everything is computed from the windows' rectangles, so it follows any resolution or UI scale.

   Which windows are "a menu":
   - the top of the modal stack (message boxes, ...);
   - a top-level window that captured the mouse (pause menu, in-game save / load, ...);
   - in a game: the conversation window (ids 39xx) and the shop's choices (38xx);
   - in a game while it is paused: a shown top-level window that is not part of the HUD (in-game options);
   - outside a game: every shown top-level window (main menu screens).
   The HUD is learned: top-level windows that are shown while playing with no menu. */
#include "gui.h"
#include "menu_nav.h"
#include "shop.h"
#include "draw.h"
#include <string.h>
#include <stdlib.h>

#define MAXROOTS 8
#define MAXITEMS 128
#define MAXHUD 64

static nox_win *g_hud[MAXHUD];
static int g_nhud;

static nox_win *g_sel;      /* selected item */
static nox_win *g_list;     /* list box / slider entered with A (up / down move inside it) */
static int g_lastsig;       /* to log menu changes */
static int g_dump;

static int rects_overlap(onc_rect a, onc_rect b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

static int in_hud(nox_win *w) {
    for (int i = 0; i < g_nhud; i++) if (g_hud[i] == w) return 1;
    return 0;
}

static int root_id_class(nox_win *w) { return NOXW_ID(w) / 100; }

static int collect_roots(nox_win **out, int ingame) {
    int n = 0;
    void **modal = *(void ***)NOX_GUI_MODAL;
    if (modal && modal[0] && onc_win_visible((nox_win *)modal[0])) {
        /* the shop: menu_nav works only its choices (buying: shop.c, selling / repairing: the inventory) */
        if (modal[0] == *(void **)NOX_SHOP_WIN && onc_shop_mode() > SHOP_CHOICES) return 0;
        if (modal[0] == *(void **)0x714A08) return 0; /* the creature order menu: ui/summon.c */
        out[n++] = (nox_win *)modal[0];
        return n;
    }
    nox_win *cap = *(nox_win **)NOX_GUI_CAPTURED;
    if (cap && !NOXW_PARENT(cap) && onc_win_visible(cap)) { out[n++] = cap; return n; }
    nox_win *last = *(nox_win **)NOX_GUI_LAST;
    if (ingame) {
        for (nox_win *w = last; w && n < MAXROOTS; w = NOXW_PREV(w)) {
            int c = root_id_class(w);
            if (c == 38 && onc_shop_mode() > SHOP_CHOICES) continue; /* buying: shop.c, selling: the inventory */
            if ((c == 39 || c == 38 || c == 36) && onc_win_visible(w) && (NOXW_FLAGS(w) & NOXS_ENABLED)) out[n++] = w;
        }
        if (n) return n;
        if (ONC_I32(NOX_GAME_FLAGS) & NOX_FLAG_PAUSE) {
            for (nox_win *w = last; w && n < 1; w = NOXW_PREV(w))
                if (!in_hud(w) && onc_win_visible(w) && (NOXW_FLAGS(w) & NOXS_ENABLED)) out[n++] = w;
            return n;
        }
        /* playing: remember the HUD */
        for (nox_win *w = last; w; w = NOXW_PREV(w))
            if (onc_win_visible(w) && !in_hud(w) && g_nhud < MAXHUD) g_hud[g_nhud++] = w;
        return 0;
    }
    for (nox_win *w = last; w && n < MAXROOTS; w = NOXW_PREV(w))
        if (onc_win_visible(w) && (NOXW_FLAGS(w) & NOXS_ENABLED)) out[n++] = w;
    return n;
}

/* the deepest enabled, shown child at p (the window that really gets a click there) */
static nox_win *child_at(nox_win *root, int px, int py) {
    nox_win *cur = root;
again:
    for (nox_win *c = NOXW_LASTCHILD(cur); c; c = NOXW_PREV(c)) {
        onc_rect r = onc_win_rect(c);
        if (px >= r.x0 && px <= r.x1 && py >= r.y0 && py <= r.y1 &&
            (NOXW_FLAGS(c) & NOXS_ENABLED) && !(NOXW_FLAGS(c) & NOXS_HIDDEN)) {
            cur = c;
            goto again;
        }
    }
    return cur;
}

static int is_child_of(nox_win *w, nox_win *parent) {
    for (nox_win *it = w; it; it = NOXW_PARENT(it)) if (it == parent) return 1;
    return 0;
}

#define ITEM_STYLES (NOXT_PUSHBUTTON | NOXT_RADIO | NOXT_CHECKBOX | NOXT_VSLIDER | NOXT_HSLIDER | NOXT_LISTBOX | NOXT_ENTRY)

typedef struct { nox_win *w; onc_rect r; } item;

static void walk(nox_win *root, nox_win *w, item *items, int *n, int sw, int sh) {
    for (nox_win *c = NOXW_LASTCHILD(w); c && *n < MAXITEMS; c = NOXW_PREV(c)) {
        uint32_t f = NOXW_FLAGS(c);
        if ((f & (NOXS_HIDDEN | NOXS_DESTROYED)) || !(f & NOXS_ENABLED)) continue;
        if (NOXW_STYLE(c) & ITEM_STYLES) {
            /* a shop shows many buttons; only its four choices are items */
            if (root_id_class(root) == 38 && (NOXW_ID(c) < 3801 || NOXW_ID(c) > 3804)) continue;
            /* the conversation text and its scroll bar are not items */
            if (root_id_class(root) == 39 && NOXW_ID(c) >= 3901 && NOXW_ID(c) <= 3904) continue;
            onc_rect r = onc_win_rect(c);
            if (r.x1 - r.x0 < 4 || r.y1 - r.y0 < 4) continue;
            int cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2;
            if (cx < 0 || cy < 0 || cx >= sw || cy >= sh) continue;
            nox_win *hit = child_at(root, cx, cy);
            if (hit != c && !is_child_of(hit, c)) {
                /* covered: fine for a column of a multi-column list if it is the list on top that is hit */
                if (!((NOXW_STYLE(c) & NOXT_LISTBOX) && (NOXW_STYLE(hit) & NOXT_LISTBOX))) continue;
                int dup = 0;
                for (int k = 0; k < *n; k++) if (items[k].w == hit || rects_overlap(items[k].r, r)) dup = 1;
                if (dup) continue;
                items[(*n)++] = (item){hit, onc_win_rect(hit)};
                continue;
            }
            int seen = 0;
            for (int k = 0; k < *n; k++) if (items[k].w == c) seen = 1;
            if (!seen) items[(*n)++] = (item){c, r};
            continue;
        }
        walk(root, c, items, n, sw, sh);
    }
}

static item g_ov[MAXITEMS]; /* the items of the last update, for the overlay */
static int g_nov;

static int collect_items(item *items, int ingame, int sw, int sh, nox_win **roots, int *nroots) {
    *nroots = collect_roots(roots, ingame);
    int n = 0;
    for (int i = 0; i < *nroots; i++) walk(roots[i], roots[i], items, &n, sw, sh);
    return n;
}

static void centre(onc_rect r, float *x, float *y) { *x = (r.x0 + r.x1) / 2.0f; *y = (r.y0 + r.y1) / 2.0f; }

static int first_item(item *it, int n) {
    int best = -1;
    for (int i = 0; i < n; i++) {
        /* conversation: Yes, else Done */
        if (NOXW_ID(it[i].w) == 3908) return i;
        /* price / amount box: the check mark; shop: Buy */
        if (NOXW_ID(it[i].w) == 3604 || NOXW_ID(it[i].w) == 3804) return i;
        if (best < 0 || it[i].r.y0 < it[best].r.y0 - 4 || (abs(it[i].r.y0 - it[best].r.y0) <= 4 && it[i].r.x0 < it[best].r.x0))
            best = i;
    }
    return best;
}

/* the next item in a direction (scored by the distance along it plus 2.5x the distance sideways); wraps to the far side */
static int next_item(item *it, int n, int cur, int dx, int dy) {
    float cx, cy;
    centre(it[cur].r, &cx, &cy);
    int best = -1, wrap = -1;
    float bs = 1e30f, ws = 1e30f;
    for (int i = 0; i < n; i++) {
        if (i == cur) continue;
        float x, y;
        centre(it[i].r, &x, &y);
        float vx = x - cx, vy = y - cy;
        float along = vx * dx + vy * dy;
        float perp = vx * dy - vy * dx;
        if (perp < 0) perp = -perp;
        if (dx == 0 && it[i].r.x0 < it[cur].r.x1 && it[i].r.x1 > it[cur].r.x0) perp *= 0.25f;
        else if (dy == 0 && it[i].r.y0 < it[cur].r.y1 && it[i].r.y1 > it[cur].r.y0) perp *= 0.25f;
        if (along > 2) { float s = along + 2.5f * perp; if (s < bs) { bs = s; best = i; } }
        else if (along < -2) { float s = 2.5f * perp - 0.5f * (-along); if (s < ws) { ws = s; wrap = i; } }
    }
    return best >= 0 ? best : wrap >= 0 ? wrap : cur;
}

/* the window's text (draw data, wide chars at +0x6C), as ASCII */
static void win_text(nox_win *w, char *out, int n) {
    const uint16_t *t = (const uint16_t *)((char *)w + 0x6C);
    int i = 0;
    for (; i < n - 1 && i < 63 && t[i]; i++) out[i] = t[i] < 128 ? (char)t[i] : '?';
    out[i] = 0;
}

static void log_items(item *it, int n) {
    for (int i = 0; i < n; i++) {
        char t[40];
        win_text(it[i].w, t, sizeof t);
        onc_log("  item %d: id %d style %X at %d,%d %dx%d \"%s\"", i, NOXW_ID(it[i].w), NOXW_STYLE(it[i].w),
                it[i].r.x0, it[i].r.y0, it[i].r.x1 - it[i].r.x0, it[i].r.y1 - it[i].r.y0, t);
    }
}

static void log_menu(nox_win **roots, int nroots, int nitems, int ingame) {
    int sig = nroots * 1000003 + nitems;
    for (int i = 0; i < nroots; i++) sig = sig * 31 + NOXW_ID(roots[i]);
    if (sig == g_lastsig) return;
    g_lastsig = sig;
    if (!nroots) { onc_log("menu: none (%s)", ingame ? "playing" : "out of a game"); return; }
    char b[256]; int o = 0;
    for (int i = 0; i < nroots && o < 200; i++) o += wsprintfA(b + o, " %d", NOXW_ID(roots[i]));
    onc_log("menu: windows%s, %d items%s", b, nitems, ingame ? " (in a game)" : "");
    g_dump = 1;
}

/* ---- list boxes (OpenNox listBoxState): count int16 +44, selection int32 +48, line height uint16 +2,
   scroll int16 +54 (pixels), items +24 (524 bytes each, first dword = the line's bottom) ---- */
#define LB_COUNT(d)  (*(int16_t *)((char *)(d) + 44))
#define LB_SEL(d)    (*(int32_t *)((char *)(d) + 48))
#define LB_LINEH(d)  (*(uint16_t *)((char *)(d) + 2))
#define LB_SCROLL(d) (*(int16_t *)((char *)(d) + 54))
#define LB_ITEMS(d)  (*(char **)((char *)(d) + 24))

/* A multi-column list (the save lists: marker, date, name) is several list boxes on top of each other.
   Collect them: the list itself, its list box children and the list box siblings it overlaps. */
static int list_group(nox_win *w, nox_win **out, int max) {
    int n = 0;
    out[n++] = w;
    onc_rect r = onc_win_rect(w);
    for (nox_win *c = NOXW_LASTCHILD(w); c && n < max; c = NOXW_PREV(c))
        if ((NOXW_STYLE(c) & NOXT_LISTBOX) && NOXW_WIDGET(c)) out[n++] = c;
    nox_win *par = NOXW_PARENT(w);
    if (par)
        for (nox_win *c = NOXW_LASTCHILD(par); c && n < max; c = NOXW_PREV(c))
            if (c != w && (NOXW_STYLE(c) & NOXT_LISTBOX) && NOXW_WIDGET(c) && rects_overlap(onc_win_rect(c), r)) out[n++] = c;
    return n;
}

/* the list box of the group that holds the entries (has lines, preferably a selection) */
static nox_win *data_list(nox_win *w) {
    nox_win *g[8];
    int n = list_group(w, g, 8), best = -1, bs = -1;
    for (int i = 0; i < n; i++) {
        void *d = NOXW_WIDGET(g[i]);
        if (!d) continue;
        int cnt = LB_COUNT(d), sel = LB_SEL(d), sc = 0;
        if (cnt > 0 && LB_ITEMS(d)) sc = (sel >= 0 && sel < cnt) ? 2 : 1;
        if (sc > bs) { bs = sc; best = i; }
    }
    return best >= 0 ? g[best] : w;
}

static void list_step(nox_win *w, int dir) {
    nox_win *dl = data_list(w);
    onc_win_key(dl, dir < 0 ? 0xC8 : 0xD0); /* the list's own up / down keys: selection, scrolling, notification */
    /* scroll the other columns along (the game does that only for its scroll buttons) */
    void *d = NOXW_WIDGET(dl);
    if (!d) return;
    nox_win *g[8];
    int n = list_group(w, g, 8);
    for (int i = 0; i < n; i++)
        if (g[i] != dl && NOXW_WIDGET(g[i])) LB_SCROLL(NOXW_WIDGET(g[i])) = LB_SCROLL(d);
}

/* where the cursor goes on a list: on its selected line */
static int list_cursor(nox_win *w, onc_rect r, int *y) {
    void *d = NOXW_WIDGET(data_list(w));
    if (!d || !LB_ITEMS(d)) return 0;
    int sel = LB_SEL(d), cnt = LB_COUNT(d);
    if (sel < 0 || sel >= cnt) return 0;
    int bottom = *(int32_t *)(LB_ITEMS(d) + 524 * sel);
    int yy = r.y0 + bottom - LB_SCROLL(d) - LB_LINEH(d) / 2;
    if (yy <= r.y0 || yy >= r.y1) return 0;
    *y = yy;
    return 1;
}

/* B: the screen's own way back (a Back / Cancel / Done button), else Esc */
static nox_win *back_button(item *it, int n, nox_win **roots, int nroots) {
    static const char *words[] = {"resume", "cancel", "back", "done", "close", "return"};
    for (int k = 0; k < 6; k++)
        for (int i = 0; i < n; i++) {
            if (!(NOXW_STYLE(it[i].w) & NOXT_PUSHBUTTON)) continue;
            char t[40];
            win_text(it[i].w, t, sizeof t);
            for (char *c = t; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
            if (strstr(t, words[k])) return it[i].w;
        }
    /* a small picture button (no text) in the top right corner of the screen's window: its close button
       (the options' X) */
    nox_win *best = NULL;
    int bs = 0;
    for (int i = 0; i < n; i++) {
        nox_win *w = it[i].w;
        onc_rect r = it[i].r;
        if (!(NOXW_STYLE(w) & NOXT_PUSHBUTTON) || r.x1 - r.x0 > 48 || r.y1 - r.y0 > 48) continue;
        char t[8];
        win_text(w, t, sizeof t);
        if (t[0]) continue;
        for (int k = 0; k < nroots; k++) {
            if (!is_child_of(w, roots[k])) continue;
            onc_rect rr = onc_win_rect(roots[k]);
            int rw = rr.x1 - rr.x0, rh = rr.y1 - rr.y0;
            if (r.x1 < rr.x1 - rw / 4 || r.y0 > rr.y0 + rh / 6) continue;
            int sc = (r.x1 - rr.x0) - (r.y0 - rr.y0);
            if (!best || sc > bs) { best = w; bs = sc; }
        }
    }
    return best;
}

/* slider internals (OpenNox: min, max at +0/+4, value at +12; set value with 0x400A, notify parent 0x4009) */
static void slider_step(nox_win *w, int dir) {
    int32_t *d = (int32_t *)NOXW_WIDGET(w);
    if (!d || d[1] <= d[0]) return;
    int step = (d[1] - d[0]) / 20;
    if (step < 1) step = 1;
    int v = d[3] + dir * step;
    if (v < d[0]) v = d[0];
    if (v > d[1]) v = d[1];
    if (v == d[3]) return;
    nox_winproc f94 = (nox_winproc)NOXW_FUNC94(w);
    if (f94) f94(w, 0x400A, (uintptr_t)v, 0);
    nox_win *p = NOXW_DRAWWIN(w);
    if (p && (f94 = (nox_winproc)NOXW_FUNC94(p))) f94(p, 0x4009, (uintptr_t)w, (uintptr_t)v);
}

int onc_menu_update(const onc_menu_input *in, onc_menu_output *out) {
    memset(out, 0, sizeof *out);
    int sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    item items[MAXITEMS];
    nox_win *roots[MAXROOTS];
    int nroots;
    int n = collect_items(items, in->ingame, sw, sh, roots, &nroots);
    log_menu(roots, nroots, n, in->ingame);
    if (g_dump) { g_dump = 0; log_items(items, n); }
    if (!nroots || !n) { g_sel = g_list = NULL; g_nov = 0; return 0; }
    memcpy(g_ov, items, n * sizeof items[0]);
    g_nov = n;
    out->active = 1;

    int cur = -1;
    for (int i = 0; i < n; i++) if (items[i].w == g_sel) cur = i;
    if (g_list && (cur < 0 || items[cur].w != g_list)) g_list = NULL;
    int moved = 0;
    if (cur < 0) { cur = first_item(items, n); moved = 1; }

    if (in->dir_x || in->dir_y) {
        if (g_list) {
            uint32_t st = NOXW_STYLE(g_list);
            if (st & NOXT_LISTBOX) {
                if (in->dir_y) list_step(g_list, in->dir_y);
            } else if (st & NOXT_HSLIDER) {
                if (in->dir_x) slider_step(g_list, in->dir_x);
            } else if (st & NOXT_VSLIDER) {
                if (in->dir_y) slider_step(g_list, -in->dir_y); /* up = more */
            }
        } else {
            cur = next_item(items, n, cur, in->dir_x, in->dir_y);
        }
        moved = 1;
    }
    g_sel = items[cur].w;
    float cx, cy;
    centre(items[cur].r, &cx, &cy);
    if (NOXW_STYLE(g_sel) & NOXT_LISTBOX) {
        /* on the selected line, near the left side (away from the scroll bar) */
        int ly, w = items[cur].r.x1 - items[cur].r.x0;
        cx = items[cur].r.x0 + (w / 4 < 24 ? w / 4 : 24);
        if (list_cursor(g_sel, items[cur].r, &ly)) cy = (float)ly;
    }
    out->sel_x = (int)cx; out->sel_y = (int)cy;
    out->put_cursor = moved;

    if (in->press_a) {
        /* the cursor was moved away (right stick / mouse): a plain click where it is */
        onc_rect r = items[cur].r;
        if (!in->cursor_on_sel && !moved) { out->click = 1; return 1; }
        (void)r;
        uint32_t st = NOXW_STYLE(g_sel);
        if (g_list) g_list = NULL;
        else if (st & (NOXT_LISTBOX | NOXT_HSLIDER | NOXT_VSLIDER)) g_list = g_sel;
        else if (st & NOXT_ENTRY) out->click = 1; /* text field: give it the focus with a click */
        else {
            onc_log("menu: press %d", NOXW_ID(g_sel));
            onc_win_press(g_sel);
        }
    }
    if (in->press_b) {
        if (g_list) { g_list = NULL; return 1; }
        /* conversation: Done */
        for (int i = 0; i < n; i++) if (NOXW_ID(items[i].w) == 3906) { onc_win_press(items[i].w); return 1; }
        /* price / amount box: the cross; shop choices: Leave */
        for (int i = 0; i < n; i++)
            if (NOXW_ID(items[i].w) == 3605 || NOXW_ID(items[i].w) == 3801) { onc_win_press(items[i].w); return 1; }
        nox_win *bk = back_button(items, n, roots, nroots);
        if (bk) { onc_log("menu: back %d", NOXW_ID(bk)); onc_win_press(bk); return 1; }
        out->esc = 1; /* the game's own Esc handling */
    }
    return 1;
}

/* ---- list mode overlay, so that it is clear where up / down go: while a list or a slider is entered with A, it gets a
   yellow frame (all the columns of a multi-column list) and the other items are darkened ---- */
void onc_menu_draw_overlay(void) {
    if (!g_list || !onc_win_alive(g_list)) return;
    int found = 0;
    for (int i = 0; i < g_nov; i++) if (g_ov[i].w == g_list) found = 1;
    if (!found) return;
    nox_win *grp[8];
    int ng = (NOXW_STYLE(g_list) & NOXT_LISTBOX) ? list_group(g_list, grp, 8) : 0;
    if (!ng) { grp[0] = g_list; ng = 1; }
    onc_rect u = onc_win_rect(grp[0]);
    for (int k = 1; k < ng; k++) {
        onc_rect r = onc_win_rect(grp[k]);
        if (r.x0 < u.x0) u.x0 = r.x0;
        if (r.y0 < u.y0) u.y0 = r.y0;
        if (r.x1 > u.x1) u.x1 = r.x1;
        if (r.y1 > u.y1) u.y1 = r.y1;
    }
    onc_draw_begin();
    for (int i = 0; i < g_nov; i++) {
        nox_win *w = g_ov[i].w;
        int mine = 0;
        for (int k = 0; k < ng; k++) if (w == grp[k] || is_child_of(w, grp[k])) mine = 1;
        if (mine || !onc_win_alive(w)) continue;
        onc_rect r = onc_win_rect(w);
        onc_draw_darken(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
        onc_draw_darken(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
    }
    int yellow = ONC_I32(NOX_COLOR_YELLOW);
    onc_draw_frame(u.x0 - 2, u.y0 - 2, u.x1 - u.x0 + 4, u.y1 - u.y0 + 4, yellow);
    onc_draw_frame(u.x0 - 3, u.y0 - 3, u.x1 - u.x0 + 6, u.y1 - u.y0 + 6, yellow);
    onc_draw_end();
}

int onc_menu_rect(onc_rect *r) {
    if (!g_sel || !onc_win_alive(g_sel)) return 0;
    *r = onc_win_rect(g_sel);
    return 1;
}
