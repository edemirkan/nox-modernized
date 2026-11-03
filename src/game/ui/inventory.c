/* Gamepad in the inventory: the D-pad / left stick move a selection over the item grid (the cursor goes onto
   the cell, so the game shows its own highlight and tooltip), the tray scrolls with it through its own scroll
   bar. A does what the game's click on the item does (equip / unequip a weapon or armour, use anything else),
   X drops it in front of the player (a short press, on release); X held 0.5 s picks the item up as the mouse would
   (the panel's own mouse-down handler: the item on the cursor, its sound) and the left stick carries it; letting X go
   is the panel's mouse-up there: in the world the item is dropped at the cursor (a stack asks how many), on the
   inventory it is moved / equipped as with the mouse; then the cursor goes back to the grid. B / Select close the inventory - all through the game's own functions
   (the network messages the tray sends), not by emulating the mouse.
   Cells and the tray position come from the game's own layout (the panel window and the scroll offset), so
   they follow the resolution. */
#include "inventory.h"
#include "inv_tabs.h"
#include "shop.h"
#include <string.h>
#include <math.h>

#define COLS 4
#define ROWS 21
#define CELL 50
#define VIS_ROWS 3 /* the tray shows 150 pixels */

static int g_col, g_row, g_init, g_recursor;
#define CARRY_MS 500     /* X held this long on an item: carried (a quick press is a drop) */
#define CLICK_GUARD_MS 400 /* the game takes a mouse down / up within 1/3 s at one spot as a click (equip / use) */
static int g_x_pend;    /* X went down on an item and is not handled yet */
static DWORD g_x_down;  /* when */
static int g_carry;     /* the item was picked up and is carried */
static DWORD g_carry_t; /* when it was picked up */
static int g_x_held;    /* X was down at the last update */
int onc_inv_carrying(void) { return g_carry; }

/* the panel's mouse handler at the cursor (GUI pixels, as the game's own mouse messages) */
static void panel_mouse(int code) {
    nox_win *panel = *(nox_win **)NOX_INV_PANEL;
    if (!panel) return;
    uint32_t xy = (uint32_t)(ONC_I32(NOX_MOUSE_X) & 0xFFFF) | ((uint32_t)ONC_I32(NOX_MOUSE_Y) << 16);
    /* through the panel's input proc (0x464720): with the inventory tabs it is wrapped so that the tab's view is
       in the game's grid while it runs (inv_tabs.c); called directly the game picked from its unfiltered grid */
    void *f = NOXW_FUNC93(panel);
    ((nox_winproc)(f ? f : (void *)NOX_FN_INV_MOUSE))(panel, code, xy, 0);
}

/* the drop at the cursor: the handler turns the cursor into a world point (0x46AB40, called at 0x46516C), which
   knows nothing of the UI scale and zoom: the GUI position is turned into the world view's first */
void onc_ui_g2w(int *x, int *y);
static void __cdecl drop_point(const int32_t *scr, int32_t *world) {
    int32_t p[2] = {scr[0], scr[1]};
    onc_ui_g2w(&p[0], &p[1]);
    ((void (__cdecl *)(const int32_t *, int32_t *))0x46AB40)(p, world);
}
void onc_inv_carry_init(void) { onc_hook_call("inventory drop at the cursor", 0x46516C, drop_point, 0x46AB40); }

/* ---- the equipment doll (left of the tray): left from the grid's first column ---- */
#define NOX_DOLL_SLOTS 0x6D25A4 /* 9 lists of the equipped items shown on the doll (+368: next) */
typedef struct { int slot, x, y; const char *name; } doll_slot;
/* points inside each slot's hit rectangle (panel + 11, 15 + point; see 0x5A5F90) */
static const doll_slot k_doll[] = {
    {7, 30, 100, "weapon"}, {5, 75, 70, "arms"}, {1, 102, 20, "head"}, {2, 106, 66, "body"},
    {3, 104, 120, "legs"}, {4, 105, 175, "feet"}, {8, 175, 100, "shield"},
};
#define NDOLL (int)(sizeof k_doll / sizeof k_doll[0])
static int g_doll, g_dsel = 3;

static void *doll_item(int i) { return ((void **)NOX_DOLL_SLOTS)[k_doll[i].slot]; }

/* the next doll slot in a direction (-1: none) */
static int doll_step(int cur, int dx, int dy) {
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < NDOLL; i++) {
        if (i == cur) continue;
        float vx = (float)(k_doll[i].x - k_doll[cur].x), vy = (float)(k_doll[i].y - k_doll[cur].y);
        float along = vx * dx + vy * dy, across = dx ? fabsf(vy) : fabsf(vx);
        if (along <= 0 || across > along * 1.5f) continue;
        float d = along + across * 2;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

int onc_inv_on_doll(void) { return g_doll; }

void *onc_inv_doll_selected(void) {
    return g_doll && onc_inv_active() ? doll_item(g_dsel) : NULL;
}

/* (col, row) are view cells: with the tabs, the tab's items come first (see inv_tabs.c) */
static int real_idx(int col, int row) { return onc_tabs_real(col, row); }

static char *cell(int col, int row) {
    int i = real_idx(col, row);
    return i < 0 ? NULL : (char *)NOX_INV_GRID + i * 148;
}

static int cell_count(int col, int row) {
    char *c = cell(col, row);
    return c && *(void **)c ? *(uint8_t *)(c + 140) : 0;
}

static int max_scroll(void) {
    nox_win *sl = *(nox_win **)NOX_INV_SLIDER;
    int32_t *d = sl ? (int32_t *)NOXW_WIDGET(sl) : NULL;
    return d ? d[1] : 0;
}

static int rows_used(void) {
    int items = onc_tabs_items();
    if (items >= 0) return items ? (items + COLS - 1) / COLS : 1; /* a tab: its items only */
    int n = (max_scroll() + VIS_ROWS * CELL) / CELL;
    return n < VIS_ROWS ? VIS_ROWS : n > ROWS ? ROWS : n;
}

/* the tray scroll, set the way its arrow buttons do (the scroll bar runs the other way) */
static void set_scroll(int v) {
    int mx = max_scroll();
    if (v > mx) v = mx;
    if (v < 0) v = 0;
    ONC_I32(NOX_INV_SCROLL) = v;
    nox_win *sl = *(nox_win **)NOX_INV_SLIDER;
    nox_winproc f = sl ? (nox_winproc)NOXW_FUNC94(sl) : NULL;
    if (f) f(sl, 0x400A, (uintptr_t)(mx - v), 0);
}

void onc_inv_scroll_top(void) { set_scroll(0); }

int onc_inv_active(void) {
    if (!ONC_I32(NOX_CLIENT_CONNECTED) || *(volatile uint8_t *)NOX_INV_STATE != 2) return 0;
    void **modal = *(void ***)NOX_GUI_MODAL;
    int shop = onc_shop_mode();
    if (shop) {
        /* a shop: the inventory is worked while selling / repairing, with nothing over the shop */
        if ((shop != SHOP_SELL && shop != SHOP_REPAIR) || !onc_shop_on_top()) return 0;
    } else if (modal && modal[0]) return 0;          /* a dialog over it (how many to drop) */
    nox_win *cap = *(nox_win **)NOX_GUI_CAPTURED;
    if (cap && !NOXW_PARENT(cap) && onc_win_visible(cap)) return 0; /* the pause menu */
    nox_win *panel = *(nox_win **)NOX_INV_PANEL;
    return panel && onc_win_visible(panel);
}

void onc_inv_update(const onc_inv_input *in, onc_inv_output *out) {
    memset(out, 0, sizeof *out);
    if (g_carry) {
        g_x_held = in->hold_x;
        if (in->hold_x && *(void **)NOX_INV_ITEM) { out->carrying = 1; return; }
        /* let go too soon after the pick-up: the game would take it as a click on the item and equip / use it;
           the mouse-up waits (let go without moving, the item goes back to its cell) */
        if (*(void **)NOX_INV_ITEM && GetTickCount() - g_carry_t < CLICK_GUARD_MS) { out->carrying = 1; return; }
        /* X let go: the mouse-up where the cursor is (dropped in the world / put back in the inventory) */
        onc_log("inventory: carry released at %d,%d", ONC_I32(NOX_MOUSE_X), ONC_I32(NOX_MOUSE_Y));
        if (*(void **)NOX_INV_ITEM) panel_mouse(6);
        g_carry = 0;
        g_recursor = 1; /* the cursor back onto the selected cell */
    }
    onc_inv_input held = *in;
    if (g_x_pend) { held.dir_x = held.dir_y = held.tab = 0; in = &held; } /* X held on an item: it stays selected */
    onc_tabs_sync();
    if (in->tab) {
        onc_tabs_step(in->tab);
        g_init = 0;
        set_scroll(0);
    }
    if (*(volatile uint8_t *)NOX_INV_JOURNAL != 0) return; /* the journal: nothing to select */
    if (g_doll) {
        nox_win *pw = *(nox_win **)NOX_INV_PANEL;
        onc_rect r0 = onc_win_rect(pw);
        int moved_d = g_recursor;
        g_recursor = 0;
        if (in->dir_x || in->dir_y) {
            int n = doll_step(g_dsel, in->dir_x, in->dir_y);
            if (n >= 0) { g_dsel = n; moved_d = 1; }
            else if (in->dir_x > 0) { g_doll = 0; g_col = 0; g_recursor = 1; } /* back to the grid */
        }
        if (g_doll) {
            out->x = r0.x0 + 11 + k_doll[g_dsel].x;
            out->y = r0.y0 + 15 + k_doll[g_dsel].y;
            out->put_cursor = moved_d;
            void *it = doll_item(g_dsel);
            if (in->press_a && it && !onc_shop_mode()) {
                onc_log("inventory: unequip (doll %s)", k_doll[g_dsel].name);
                ((void (__cdecl *)(void *))NOX_FN_DEQUIP)(it);
            }
            return;
        }
    }
    nox_win *panel = *(nox_win **)NOX_INV_PANEL;
    onc_rect pr = onc_win_rect(panel);
    int tx = pr.x0 + 254 + 60, ty = pr.y0 + 13; /* the grid's top left on the screen */
    int rows = rows_used();
    int moved = 0;
    if (!g_init || !cell_count(g_col, g_row)) {
        if (!g_init) {
            /* the first item */
            g_col = g_row = 0;
            for (int r = 0; r < rows && !g_init; r++)
                for (int c = 0; c < COLS; c++)
                    if (cell_count(c, r)) { g_col = c; g_row = r; g_init = 1; break; }
            g_init = 1;
            moved = 1;
        }
    }
    if (in->dir_x < 0 && g_col == 0 && !onc_shop_mode()) {
        /* left of the grid: the equipment doll */
        g_doll = 1;
        g_dsel = 3; /* body */
        nox_win *pw = *(nox_win **)NOX_INV_PANEL;
        onc_rect r0 = onc_win_rect(pw);
        out->x = r0.x0 + 11 + k_doll[g_dsel].x;
        out->y = r0.y0 + 15 + k_doll[g_dsel].y;
        out->put_cursor = 1;
        return;
    }
    if (in->dir_x || in->dir_y) {
        g_col += in->dir_x; g_row += in->dir_y;
        if (g_col < 0) g_col = 0;
        if (g_col >= COLS) g_col = COLS - 1;
        if (g_row < 0) g_row = 0;
        if (g_row >= rows) g_row = rows - 1;
        moved = 1;
    }
    {   /* in a tab: stay on its items */
        int items = onc_tabs_items();
        if (items > 0 && g_row * COLS + g_col >= items) {
            g_row = (items - 1) / COLS;
            if (g_row * COLS + g_col >= items) g_col = (items - 1) % COLS;
            moved = 1;
        }
    }
    /* keep the row in view */
    int sc = ONC_I32(NOX_INV_SCROLL);
    int top = g_row * CELL, bottom = top + CELL;
    if (top < sc) set_scroll(top);
    else if (bottom > sc + VIS_ROWS * CELL) set_scroll(bottom - VIS_ROWS * CELL);
    sc = ONC_I32(NOX_INV_SCROLL);
    out->x = tx + g_col * CELL + CELL / 2;
    out->y = ty + g_row * CELL - sc + CELL / 2;
    out->put_cursor = moved || g_recursor || sc != ONC_I32(NOX_INV_SCROLL);
    g_recursor = 0;
    int shop = onc_shop_mode();
    if (shop && in->press_a && cell_count(g_col, g_row)) {
        /* selling / repairing: ask the vendor (the game's price box opens with the answer) */
        if (shop == SHOP_SELL) onc_shop_sell(cell(g_col, g_row));
        else onc_shop_repair(cell(g_col, g_row));
        return;
    }
    if (shop) return; /* no equipping or dropping in a shop */
    if (in->press_a && cell_count(g_col, g_row)) {
        /* the tray's click (OpenNox sub_464BD0): weapons and armour are equipped / unequipped, the rest used */
        char *c = cell(g_col, g_row);
        void *item = *(void **)c;
        uint32_t cls = *(uint32_t *)((char *)item + 112);
        if (cls & 0x3001000) {
            if (*(int32_t *)(c + 132)) {
                onc_log("inventory: unequip %d,%d", g_col, g_row);
                ((void (__cdecl *)(void *))NOX_FN_DEQUIP)(item);
            } else {
                onc_log("inventory: equip %d,%d", g_col, g_row);
                void *was = *(void **)NOX_INV_ITEM;
                *(void **)NOX_INV_ITEM = item;
                int ri = real_idx(g_col, g_row); /* the game's grid is the real one here */
                ((void (__cdecl *)(int, int))NOX_FN_KEY_EQUIP)(ri / ROWS, ri % ROWS);
                *(void **)NOX_INV_ITEM = was;
            }
        } else {
            onc_log("inventory: use %d,%d", g_col, g_row);
            /* with the cell's own item code, as the game's tray does (see the drop below) */
            uint32_t code = *(uint32_t *)((char *)item + 128);
            *(uint32_t *)((char *)item + 128) = *(uint32_t *)(c + 4);
            ((void (__cdecl *)(void *))NOX_FN_USE)(item);
            *(uint32_t *)((char *)item + 128) = code;
        }
    }
    /* X: a short press drops the item in front of the player, held it is carried (above). The press / release are
       taken from X's own state here (g_x_held), not from the pad's press edge */
    int x_pressed = in->hold_x && !g_x_held;
    g_x_held = in->hold_x;
    /* a separate pending flag: keeping the time as GetTickCount() | 1 (0 = none) would be 1 ms ahead half of the
       time, now - then would wrap round and a fresh press would count as held */
    if (x_pressed && cell_count(g_col, g_row)) { g_x_pend = 1; g_x_down = GetTickCount(); }
    if (g_x_pend && in->hold_x && GetTickCount() - g_x_down >= CARRY_MS) {
        g_x_pend = 0;
        void *was = *(void **)NOX_INV_ITEM;
        panel_mouse(5); /* picks up the item under the cursor (the selected cell) */
        g_carry = *(void **)NOX_INV_ITEM != NULL && *(void **)NOX_INV_ITEM != was;
        g_carry_t = GetTickCount();
        onc_log("inventory: carry %d,%d %s", g_col, g_row, g_carry ? "picked up" : "nothing picked up");
        out->carrying = g_carry;
        return;
    }
    if (g_x_pend && !in->hold_x) {
        g_x_pend = 0;
        if (!cell_count(g_col, g_row)) return;
        /* drop it a little in front of the player (an item dropped right on the player pushes it back) */
        void *pl = *(void **)NOX_PLAYER_DR;
        if (pl) {
            float a = *(volatile int32_t *)NOX_FACING * (6.2831853f / 256.0f);
            int32_t pos[2] = {*(int32_t *)((char *)pl + 12) + (int32_t)lroundf(cosf(a) * 28),
                              *(int32_t *)((char *)pl + 16) + (int32_t)lroundf(sinf(a) * 28)};
            onc_log("inventory: drop %d,%d at %d,%d (code %u)", g_col, g_row, pos[0], pos[1], *(uint32_t *)(cell(g_col, g_row) + 4));
            /* the server finds the item by the net code sent (drawable +128); a cell of identical items shares one
               drawable and keeps the items' codes itself (+4 the first). The game's tray puts the cell's code on
               the drawable before it acts (OpenNox sub_464BD0: dr->field_32 = cell.field_4); without it the
               drawable could still carry the code of an item dropped before, and the server ignored the drop.
               The drop is sent at once (0x465730), so the code is given back right after (see radial.c) */
            char *c = cell(g_col, g_row);
            void *dr = *(void **)c;
            uint32_t code = *(uint32_t *)((char *)dr + 128);
            *(uint32_t *)((char *)dr + 128) = *(uint32_t *)(c + 4);
            void *was = *(void **)NOX_INV_ITEM;
            *(void **)NOX_INV_ITEM = dr;
            ((void (__cdecl *)(int32_t *))NOX_FN_DROP)(pos);
            *(void **)NOX_INV_ITEM = was;
            *(uint32_t *)((char *)dr + 128) = code;
        }
    }
}

void onc_inv_close(void) { ((int (__cdecl *)(void))NOX_FN_INV_CLOSE)(); }

/* the inventory was closed: start from the first item next time */
void onc_inv_reset(void) { g_init = 0; g_doll = 0; g_x_pend = 0; g_carry = 0; g_x_held = 0; }

/* a box over the inventory closed (price, how many): the cursor goes back onto the selected item */
void onc_inv_resume(void) { g_recursor = 1; }
