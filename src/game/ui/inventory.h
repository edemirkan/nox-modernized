#pragma once
#include "gui.h"

/* Game.exe inventory (OpenNox guiinv addresses minus 0x2500) */
#define NOX_INV_STATE   0x6D25A0 /* byte: 0 closed, 1 opening, 2 open, 3 closing */
#define NOX_INV_JOURNAL 0x6D25A1 /* byte: 0 the items, 1 the journal */
#define NOX_INV_GRID    0x6D2638 /* 4 x 21 cells of 148 bytes, column-major; +0 drawable, +140 count */
#define NOX_INV_PANEL   0x6D56CC /* the inventory panel window (its draw places the tray at +254,+13) */
#define NOX_INV_SLIDER  0x6D5700 /* the tray's scroll bar */
#define NOX_INV_SCROLL  0x6D5704 /* the tray's scroll offset, pixels */
#define NOX_INV_ITEM    0x6D258C /* the item a click / drag acts on (set by the tray on mouse down) */
#define NOX_FACING      0x68B160 /* the player's facing the client computes, 0..255 (0 = +x, clockwise) */
#define NOX_PLAYER_DR   0x96A6A0 /* the local player's drawable (world position at +12 / +16) */
/* game functions (cdecl) */
#define NOX_FN_INV_CLOSE   0x467760 /* close the inventory (OpenNox sub_467C10) */
#define NOX_FN_KEY_EQUIP   0x465780 /* equip NOX_INV_ITEM from cell (col, row) (nox_xxx_clientKeyEquip) */
#define NOX_FN_DEQUIP      0x4646C0 /* dequip(item) (nox_xxx_clientDequip) */
#define NOX_FN_USE         0x4657C0 /* use(item): potions, food, scrolls (nox_xxx_clientUse) */
#define NOX_FN_DROP        0x465730 /* drop NOX_INV_ITEM at a world point (int x, int y) (nox_xxx_clientDrop) */
#define NOX_FN_INV_MOUSE   0x464720 /* the inventory panel's mouse handler (win, 5 down / 6 up, x | y << 16) (sub_464BD0) */

typedef struct {
    int dir_x, dir_y;  /* navigation step */
    int press_a, press_x;
    int tab;           /* -1 / +1: previous / next tab (L1 / R1) */
    int hold_x;        /* X is held down now */
} onc_inv_input;

typedef struct {
    int put_cursor, x, y; /* the selected cell's centre */
    int carrying;         /* an item is carried (X held): the left stick moves the cursor freely */
} onc_inv_output;

void onc_inv_close(void);

int onc_inv_active(void);                       /* the item tray is open and nothing is over it */
void onc_inv_update(const onc_inv_input *in, onc_inv_output *out);
void onc_inv_reset(void);
void onc_inv_resume(void);
void onc_inv_scroll_top(void);
int  onc_inv_on_doll(void);
int  onc_inv_carrying(void);       /* an item is being carried out with X held */
void *onc_inv_doll_selected(void); /* the equipped item selected on the doll, if any */
