#include "summon.h"
#include "draw.h"

#define NOX_SUMN_BUTTON 0x714A04 /* the creature panel's top button (shows the current order) */
#define NOX_SUMN_POPUP  0x714A08 /* the order menu while it is open */
#define NOX_SUMN_TARGET 0x714AA8 /* 0: the menu is for all the creatures */
#define NOX_SUMN_COUNT  0x714AA0 /* the player's creatures */
#define NOX_SUMN_STATE  0x714AA4 /* byte: 2 = the panel is shown */
#define FN_SUMN_BUTTON  0x4C2E10 /* the button's input proc (7: open the menu at the packed point) */
#define FN_SUMN_HIDE    0x4C2DD0
#define FN_ORDER_PROC   0x4C33C0 /* an order item's input proc (7: give the order) */
#define NOX_PLAYER_INFO 0x96B8D0

static int g_pending; /* polls left to open the menu */

nox_win *onc_summon_popup(void) {
    nox_win *w = *(nox_win **)NOX_SUMN_POPUP;
    return w && onc_win_visible(w) ? w : NULL;
}
int onc_summon_menu(void) { return ONC_I32(NOX_CLIENT_CONNECTED) && onc_summon_popup() != NULL; }

static nox_win *button(void) {
    nox_win *b = *(nox_win **)NOX_SUMN_BUTTON;
    return b && onc_win_visible(b) ? b : NULL;
}
static int panel_ready(void) {
    void *info = *(void **)NOX_PLAYER_INFO;
    return ONC_I32(NOX_CLIENT_CONNECTED) && info && *(uint8_t *)((char *)info + 0x8CB) == 2 && button() &&
           *(volatile uint8_t *)NOX_SUMN_STATE == 2 && ONC_I32(NOX_SUMN_COUNT) > 0;
}

/* the menu closes when the cursor is not on it and opens around the cursor: the caller moves the cursor onto the
   button, the menu opens once the game has the cursor there */
int onc_summon_open(void) {
    if (!panel_ready()) return 0;
    if (onc_summon_menu()) return 1;
    g_pending = 12;
    return 1;
}

static void open_now(void) {
    nox_win *b = button();
    if (!b) return;
    onc_rect r = onc_win_rect(b);
    int x = (r.x0 + r.x1) / 2, y = (r.y0 + r.y1) / 2;
    ((int (__cdecl *)(nox_win *, int, uint32_t))FN_SUMN_BUTTON)(b, 7, (uint32_t)(x & 0xFFFF) | ((uint32_t)y << 16));
    onc_log("creatures: order menu");
}

void onc_summon_tick(void) {
    if (!g_pending) return;
    g_pending--;
    nox_win *b = button();
    if (!b) { g_pending = 0; return; }
    onc_rect r = onc_win_rect(b);
    int mx = ONC_I32(NOX_MOUSE_X), my = ONC_I32(NOX_MOUSE_Y);
    if ((mx >= r.x0 && mx < r.x1 && my >= r.y0 && my < r.y1) || !g_pending) { g_pending = 0; open_now(); }
}

int onc_summon_pending_xy(int *x, int *y) { /* where the cursor must go while opening */
    nox_win *b = g_pending ? button() : NULL;
    if (!b) return 0;
    onc_rect r = onc_win_rect(b);
    *x = (r.x0 + r.x1) / 2; *y = (r.y0 + r.y1) / 2;
    return 1;
}

/* the orders, top to bottom (IDLE is not an order, OBSERVE only for one creature) */
static int items(nox_win *menu, nox_win **out) {
    int n = 0, all = ONC_I32(NOX_SUMN_TARGET) == 0;
    for (nox_win *ch = NOXW_LASTCHILD(menu); ch && n < 8; ch = NOXW_PREV(ch)) {
        if (!onc_win_visible(ch)) continue;
        int cmd = NOXW_I32(ch, 0x20);
        if (cmd < 0 || cmd == 2 || (cmd == 1 && all)) continue;
        out[n++] = ch;
    }
    for (int i = 1; i < n; i++) /* by height */
        for (int j = i; j > 0 && onc_win_rect(out[j]).y0 < onc_win_rect(out[j - 1]).y0; j--) {
            nox_win *t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
    return n;
}

void onc_summon_update(const onc_summon_input *in, onc_summon_output *out) {
    out->put_cursor = 0;
    nox_win *menu = onc_summon_popup();
    if (!menu) return;
    if (in->b) { ((int (__cdecl *)(void))FN_SUMN_HIDE)(); return; }
    nox_win *it[8];
    int n = items(menu, it);
    if (!n) return;
    int cur = -1, mx = ONC_I32(NOX_MOUSE_X), my = ONC_I32(NOX_MOUSE_Y);
    for (int i = 0; i < n; i++) {
        onc_rect r = onc_win_rect(it[i]);
        if (mx >= r.x0 && mx < r.x1 && my >= r.y0 && my < r.y1) cur = i;
    }
    if (cur < 0) { cur = 0; out->put_cursor = 1; }
    else if (in->dir_y) {
        int c = cur + in->dir_y;
        cur = c < 0 ? 0 : c >= n ? n - 1 : c;
        out->put_cursor = 1;
    }
    onc_rect r = onc_win_rect(it[cur]);
    out->x = (r.x0 + r.x1) / 2; out->y = (r.y0 + r.y1) / 2;
    if (in->a) { /* the item's own click handler: the order goes to the creatures */
        onc_log("creatures: order %d", NOXW_I32(it[cur], 0x20));
        ((int (__cdecl *)(nox_win *, int, uint32_t))FN_ORDER_PROC)(it[cur], 7,
            (uint32_t)(out->x & 0xFFFF) | ((uint32_t)out->y << 16));
        out->put_cursor = 0;
    }
}

const wchar_t *onc_bind_label_action(const char *name);

void onc_summon_draw(void) {
    if (!onc_pad_in_use() || !ONC_I32(NOX_CLIENT_CONNECTED)) return;
    nox_win *menu = onc_summon_popup();
    onc_draw_begin();
    int fh = 0;
    onc_text_size(L"Ag", NULL, &fh);
    if (menu) {
        onc_rect r = onc_win_rect(menu);
        const wchar_t *l[] = {L"Up / Down: choose", L"A: give the order", L"B: close"};
        int w = 0;
        for (int i = 0; i < 3; i++) { int tw = 0; onc_text_size(l[i], &tw, NULL); if (tw > w) w = tw; }
        int x = r.x0 - w - 16, y = r.y0;
        if (x < 2) x = r.x1 + 8;
        for (int k = 0; k < 2; k++) ((void (__cdecl *)(int, int, int, int))0x484850)(x, y, w + 8, 3 * (fh + 2) + 4);
        onc_text_color(ONC_I32(NOX_COLOR_YELLOW));
        for (int i = 0; i < 3; i++) onc_text(l[i], x + 4, y + 2 + i * (fh + 2));
    } else if (panel_ready()) {
        /* the button that opens the menu, left of the panel's button */
        const wchar_t *s = onc_bind_label_action("creatures");
        if (s) {
            onc_rect r = onc_win_rect(button());
            int tw = 0;
            onc_text_size(s, &tw, NULL);
            onc_text_color(onc_rgb(255, 255, 255));
            onc_text(s, r.x0 - tw - 3, (r.y0 + r.y1) / 2 - fh / 2);
        }
    }
    onc_draw_end();
}
