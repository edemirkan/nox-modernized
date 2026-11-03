/* Inventory tabs: a strip of tabs above the inventory (the whole inventory is moved down to make room). A tab
   shows only its kind of items: while any of the inventory's own window procs runs, the game's grid holds a
   filtered view (the tab's items first, then the empty cells), and afterwards the real grid is restored with
   whatever the game changed written back. So the mouse, tooltips, drag and drop and the shops all work in every
   tab as in the normal inventory, through the game's own code. */
#pragma once
#include "gui.h"

enum { TAB_JOURNAL, TAB_WEAPONS, TAB_UPPER, TAB_LOWER, TAB_CONSUMABLES, TAB_MISC, TAB_N };

void onc_tabs_init(void);           /* patches the game (from DllMain) */
int  onc_tabs_enabled(void);
int  onc_tabs_filtering(void);      /* an item tab is shown (not the journal, tabs on) */
void onc_tabs_step(int d);          /* L1 / R1 */
void onc_tabs_sync(void);           /* follow the game's journal button; call every frame the inventory is open */
int  onc_tabs_items(void);          /* items in the current tab */
int  onc_tabs_real(int col, int row); /* the real grid cell shown at a view cell (-1: none) */
