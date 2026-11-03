/* Gamepad button mapping, with every action assignable: shared actions (attack,
   aim, talk / pick up, the consumables wheel, inventory, book, pause menu) the same for all classes, the others
   per class (warrior, wizard, conjurer). In nox-modernized.ini, as action = combo[, combo]:
       [shared]  attack = a   wheel = l3   ...
       [wizard]  jump = lt    slot3 = b, l1_x   trap = l1_a   ...   (NoxModernized-Settings.exe, Buttons page)
   Game actions are sent as the key they are bound to in the game's own key configuration (so a changed key
   configuration is followed), else the default Nox key. The class is read from the player's info. */
#pragma once
#include "onc.h"
#include "binds_slots.h"

int  onc_player_class(void);                  /* 0 warrior, 1 wizard, 2 conjurer (-1: not in a game) */
void onc_binds_init(void);
int  onc_bind_action(int combo);              /* the action (index in k_actions, binds_table.h) of a combo, 0 none */
int  onc_bind_kind(int a);                    /* how it works: K_KEY, K_ATTACK, ... */
BYTE onc_bind_key_of(int a, int *aim);        /* the key a K_KEY / K_INVENTORY / K_MENU action sends; aim: at the lock-on */
int  onc_bind_has_kind(int kind);             /* some combo does it (the consumables wheel is optional) */
int  onc_bind_buttons_of_kind(int kind, int l1); /* the buttons (C_* bits) whose combo, with / without L1, does it */
const wchar_t *onc_bind_title(int a);         /* what an action does (controls list) */
const wchar_t *onc_bind_label(int action_event); /* the pad combo shown for a game action, NULL: none */
const wchar_t *onc_bind_label_action(const char *name); /* the pad combo of an action, NULL: none */
const wchar_t *onc_bind_label_action_long(const char *name); /* the same, long: "Select", "L1 + X" */
const wchar_t *onc_combo_label(int combo, int lng);     /* "L+X" / "L1 + X" */
