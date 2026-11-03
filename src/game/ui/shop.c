#include "shop.h"
#include <string.h>

#define NOX_SHOP_GRID   0x6F31B0 /* the vendor's items on the screen: x0, y0, x1, y1 */
#define NOX_SHOP_CELLS  0x6F32B0 /* 6 x 10 cells of 140 bytes, column-major; +0 drawable, +4 count, +136 price */
#define NOX_SHOP_SCROLL 0x6F5380 /* pixels */
#define NOX_SHOP_SMAX   0x6F3284 /* scroll maximum */
#define NOX_SHOP_SLIDER 0x6F3278 /* the scroll bar */
#define FN_SHOP_BUY     0x47FCF0 /* buy(int pt[2]): the cell at a screen point, opens the price box */
#define FN_REPAIR_OFF   0x4671D0 /* leaves the repair cursor mode (OpenNox sub_467680) */
#define FN_SET_CURSOR   0x4873C0
#define FN_REQ_REPAIR   0x465300 /* send "repair price?" for an item (net code) */
#define FN_REQ_SELL     0x4653C0 /* send "sell price?" for an item (net code) */
#define COLS 6
#define ROWS 10
#define CELL 50

int onc_shop_mode(void) {
    if (!ONC_I32(NOX_CLIENT_CONNECTED) || !ONC_I32(NOX_SHOP_ON)) return 0;
    nox_win *w = *(nox_win **)NOX_SHOP_WIN;
    if (!w || !onc_win_visible(w)) return 0;
    return ONC_I32(NOX_SHOP_MODE);
}

/* the price / amount box (MultMove, ids 36xx) is open */
static int amount_box(void) {
    for (nox_win *w = *(nox_win **)NOX_GUI_LAST; w; w = NOXW_PREV(w))
        if (NOXW_ID(w) / 100 == 36 && onc_win_visible(w)) return 1;
    return 0;
}

int onc_shop_on_top(void) {
    if (!onc_shop_mode()) return 0;
    void **modal = *(void ***)NOX_GUI_MODAL;
    if (modal && modal[0] && modal[0] != *(void **)NOX_SHOP_WIN && onc_win_visible((nox_win *)modal[0])) return 0;
    return !amount_box();
}

void onc_shop_choices(void) {
    if (ONC_I32(NOX_SHOP_MODE) == SHOP_REPAIR) ((void (__cdecl *)(int))FN_REPAIR_OFF)(0);
    ONC_I32(NOX_SHOP_MODE) = SHOP_CHOICES;
    ((void (__cdecl *)(int))FN_SET_CURSOR)(0);
    onc_log("shop: choices");
}

void onc_shop_sell(char *cell) {
    int n = *(uint8_t *)(cell + 140);
    if (!*(void **)cell || !n) return;
    int net = *(int32_t *)(cell + 4 * n); /* the stack's last item, as the game's click */
    onc_log("shop: sell %d", net);
    ((void (__cdecl *)(int))FN_REQ_SELL)(net);
}

void onc_shop_repair(char *cell) {
    if (!*(void **)cell || !*(uint8_t *)(cell + 140)) return;
    int net = *(int32_t *)(cell + 4);
    onc_log("shop: repair %d", net);
    ((void (__cdecl *)(int))FN_REQ_REPAIR)(net);
}

/* ---- buying ---- */
static int g_col, g_row, g_init, g_was, g_recursor, g_blocked_in_buy;

static int has_item(int col, int row) {
    if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return 0;
    char *c = (char *)NOX_SHOP_CELLS + (row + ROWS * col) * 140;
    return *(void **)c && *(int32_t *)(c + 4);
}

static int rows_used(void) {
    int n = 1;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (has_item(c, r)) n = r + 1;
    return n;
}

static void set_scroll(int v) {
    int mx = ONC_I32(NOX_SHOP_SMAX);
    if (v > mx) v = mx;
    if (v < 0) v = 0;
    ONC_I32(NOX_SHOP_SCROLL) = v;
    nox_win *sl = *(nox_win **)NOX_SHOP_SLIDER;
    nox_winproc f = sl ? (nox_winproc)NOXW_FUNC94(sl) : NULL;
    if (f) f(sl, 0x400A, (uintptr_t)(mx - v), 0); /* as the shop's arrow buttons do */
}

int onc_shop_buy_active(void) {
    int buying = onc_shop_mode() == SHOP_BUY, on = buying && onc_shop_on_top();
    if (on && !g_was) {
        /* back from the price box: the cursor back on the selected item; from the choices: the first item */
        if (g_blocked_in_buy) g_recursor = 1;
        else g_init = 0;
    }
    g_blocked_in_buy = buying && !on;
    g_was = on;
    return on;
}

void onc_shop_buy_update(const onc_shop_input *in, onc_shop_output *out) {
    memset(out, 0, sizeof *out);
    if (in->press_b) { onc_shop_choices(); return; }
    int32_t *g = (int32_t *)NOX_SHOP_GRID;
    int rows = rows_used(), moved = 0;
    if (!g_init) {
        g_col = g_row = 0;
        for (int r = 0; r < rows && !g_init; r++)
            for (int c = 0; c < COLS; c++)
                if (has_item(c, r)) { g_col = c; g_row = r; g_init = 1; break; }
        g_init = 1;
        moved = 1;
    }
    if (in->dir_x || in->dir_y) {
        g_col += in->dir_x; g_row += in->dir_y;
        if (g_col < 0) g_col = 0;
        if (g_col >= COLS) g_col = COLS - 1;
        if (g_row < 0) g_row = 0;
        if (g_row >= rows) g_row = rows - 1;
        moved = 1;
    }
    int vis = g[3] - g[1];
    int sc = ONC_I32(NOX_SHOP_SCROLL), top = g_row * CELL;
    if (top < sc) set_scroll(top);
    else if (top + CELL > sc + vis) set_scroll(top + CELL - vis);
    if (ONC_I32(NOX_SHOP_SCROLL) != sc) moved = 1;
    sc = ONC_I32(NOX_SHOP_SCROLL);
    int pt[2] = {g[0] + g_col * CELL + CELL / 2, g[1] + g_row * CELL - sc + CELL / 2};
    out->x = pt[0]; out->y = pt[1];
    out->put_cursor = moved || g_recursor;
    g_recursor = 0;
    if (in->press_a && has_item(g_col, g_row)) {
        onc_log("shop: buy %d,%d", g_col, g_row);
        ((void (__cdecl *)(int *))FN_SHOP_BUY)(pt);
    }
}
