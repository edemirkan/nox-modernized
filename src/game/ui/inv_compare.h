/* Item comparison: right of the inventory (or below it when the screen is too narrow), the identify text of the
   item under the selection - produced by the game's own identify panel code - and next to it the equipped item
   of the same kind; numbers are compared (green: better, red: worse). */
#pragma once
#include "gui.h"

void onc_compare_init(void);
int  onc_compare_enabled(void);
void onc_compare_draw(void); /* from the inventory panel's draw */
void onc_compare_draw_shop(void); /* after the whole GUI is drawn */
