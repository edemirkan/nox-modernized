/* What the pad acts on in the world, found in the game's own object lists every frame:
   - the character in front of the player that can be talked to / traded with (R3), else
   - the closest item in reach (R3 picks it up);
   - the closest visible enemy in a narrow cone in the facing direction (lock-on: A and the spell buttons aim at it).
   They get the game's own selection circles. R3 and the pickup / talk / trade go through the game's functions. */
#pragma once
#include "onc.h"

void onc_targets_init(void);         /* patches the game (from DllMain) */
void onc_targets_enable(int on);     /* the pad is connected and playing (no menu / inventory) */
int  onc_interact(void);             /* R3: 1 = talked / traded / picked up */
int  onc_aim_screen(int *x, int *y); /* the locked-on enemy's screen position; 0 = none */
int  onc_target_hint(int *x, int *y, const wchar_t **text); /* what R3 would do, above the target (screen) */
int  onc_facing_target(void);         /* the player faces the locked-on enemy (or it cannot be known) */
void onc_targets_set_aiming(int on);  /* attacking / casting: the lock-on stays on its enemy */
int  onc_player_casting(void);
int  onc_player_attacking(void);       /* the player is in an attack (the shot / hit comes later in it) */         /* the player is casting a spell */
