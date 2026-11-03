/* Shops. The shop window's four choices (Buy / Sell / Repair / Leave) are worked by menu_nav. Then:
   - Buy: the pad moves over the vendor's items, A calls the game's buy function for the cell (it opens the game's
     price / amount box, worked by menu_nav), B goes back to the choices.
   - Sell / Repair: the pad works the inventory (inventory.c); A sends the game's sell / repair request for the
     item (the vendor answers with the price box), B goes back to the choices. */
#pragma once
#include "gui.h"

#define NOX_SHOP_ON     0x6F32A4 /* a shop is open */
#define NOX_SHOP_MODE   0x6F32A8 /* 1 choices, 2 buy, 3 sell, 4 repair */
#define NOX_SHOP_WIN    0x6F3274 /* the shop window */
enum { SHOP_CHOICES = 1, SHOP_BUY, SHOP_SELL, SHOP_REPAIR };

int  onc_shop_mode(void);       /* 0: no shop */
int  onc_shop_on_top(void);     /* the shop window is the top modal window (no dialog over it) */
void onc_shop_choices(void);    /* back to Buy / Sell / Repair / Leave */
void onc_shop_sell(char *cell); /* inventory grid cell (real) */
void onc_shop_repair(char *cell);

typedef struct { int dir_x, dir_y, press_a, press_b; } onc_shop_input;
typedef struct { int put_cursor, x, y; } onc_shop_output;
int  onc_shop_buy_active(void); /* buying, nothing over the shop */
void onc_shop_buy_update(const onc_shop_input *in, onc_shop_output *out);
