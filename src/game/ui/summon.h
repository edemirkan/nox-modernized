/* Conjurer creature orders with the pad: the "creatures" action (D-pad right for the
   conjurer) opens the order menu of the creature panel (upper right) through the panel button's own handler;
   up / down choose BANISH / GUARD / ESCORT / HUNT, A gives the order (the menu item's own handler), B closes it. */
#pragma once
#include "gui.h"

int  onc_summon_open(void);       /* 1: opening (the panel is shown and there are creatures) */
void onc_summon_tick(void);       /* every poll: opens a pending menu once the cursor is on the button */
int  onc_summon_menu(void);       /* the order menu is open */
typedef struct { int dir_y, a, b; } onc_summon_input;
typedef struct { int put_cursor, x, y; } onc_summon_output;
void onc_summon_update(const onc_summon_input *in, onc_summon_output *out);
void onc_summon_draw(void);       /* hints, after the GUI */
nox_win *onc_summon_popup(void);
