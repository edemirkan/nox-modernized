/* Consumables radial menu (L3), for quick potions and items on the pad: the game pauses and the consumables are shown in a
   ring around the player - potions on the left, safe food upper right, poisonous food lower right. The left
   stick chooses, A queues one (repeat for more), B / L3 / Start close the menu and everything queued is used.
   The HUD previews the effect (health / mana that would be restored, a cure for poison). */
#pragma once
#include "onc.h"

void onc_radial_init(void);
int  onc_radial_open(void);          /* L3 while playing: 1 = opened */
int  onc_radial_active(void);
void onc_radial_stick(float x, float y, int ok); /* left stick direction (screen: y down) */
void onc_radial_a(void);
void onc_radial_close(int cancel);
void onc_radial_draw(void);          /* after the GUI */
