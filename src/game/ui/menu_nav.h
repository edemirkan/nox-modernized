#pragma once
#include "gui.h"

typedef struct {
    int ingame;
    int dir_x, dir_y;     /* a navigation step (-1/0/1), already repeated by the caller */
    int press_a, press_b;
    int cursor_on_sel;    /* the cursor is still on the selected item */
} onc_menu_input;

typedef struct {
    int active;           /* a menu is open: the pad works the menu */
    int put_cursor;       /* move the cursor onto the selection */
    int sel_x, sel_y;
    int click;            /* left click where the cursor is */
    int esc;              /* send Esc */
} onc_menu_output;

int onc_menu_update(const onc_menu_input *in, onc_menu_output *out);
int onc_menu_rect(onc_rect *r);
void onc_menu_draw_overlay(void); /* after the GUI: frames the entered list / slider, darkens the other items */
