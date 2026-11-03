/* The gamepad actions and button combos, shared by the game (binds.c, pad.c) and the settings tool
   (launcher/launcher.c): names as written in the settings file, titles, the game event / default key per action,
   and the default combos. Pure data: needs only <windows.h>.

   Shared actions are the same for every class ([shared] in the settings file); the others are per class
   ([warrior] / [wizard] / [conjurer]). One combo does one thing: a combo held by a shared action is not used by
   a class action. */
#pragma once
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "binds_slots.h"

enum { /* how an action works (pad.c) */
    K_KEY,       /* the game key of its event (held while the button is) */
    K_ATTACK,    /* the left mouse button (at the locked-on enemy) */
    K_AIM,       /* held: the left stick turns the player without walking */
    K_INTERACT,  /* talk / trade / pick up what is in reach */
    K_WHEEL,     /* the consumables wheel */
    K_INVENTORY, /* the inventory key on release; held: the controls list */
    K_MENU,      /* Esc */
    K_CREATURES, /* the conjurer's creature order menu */
};
typedef struct { const char *name; int event; BYTE def_key; int aim; const wchar_t *title; int shared, kind; } action;
static const action k_actions[] = {
    {"none", 0, 0, 0, L"-", 0, K_KEY},
    /* shared */
    {"attack", 0, 0, 0, L"attack / use", 1, K_ATTACK},
    {"aim", 0, 0, 0, L"aim in place (hold)", 1, K_AIM},
    {"interact", 0, 0, 0, L"talk / trade / pick up", 1, K_INTERACT},
    {"wheel", 0, 0, 0, L"consumables wheel", 1, K_WHEEL},
    {"inventory", 0x07, 0x17, 0, L"inventory (hold: controls)", 1, K_INVENTORY},
    {"book", 0x0A, 0x30, 0, L"book of knowledge", 1, K_KEY},
    {"menu", 0, 0x01, 0, L"pause menu", 1, K_MENU},
    /* per class */
    {"slot1", 0x1C, 0x1E, 1, L"slot 1", 0, K_KEY}, {"slot2", 0x1D, 0x1F, 1, L"slot 2", 0, K_KEY},
    {"slot3", 0x1E, 0x20, 1, L"slot 3", 0, K_KEY}, {"slot4", 0x1F, 0x21, 1, L"slot 4", 0, K_KEY},
    {"slot5", 0x20, 0x22, 1, L"slot 5", 0, K_KEY}, {"cast_recent", 0x1B, 0, 1, L"last spell", 0, K_KEY},
    {"jump", 0x06, 0x39, 0, L"jump", 0, K_KEY}, {"health", 0x24, 0x2D, 0, L"health potion", 0, K_KEY},
    {"mana", 0x25, 0x2E, 0, L"mana potion", 0, K_KEY}, {"cure", 0x26, 0x2C, 0, L"cure poison", 0, K_KEY},
    {"swap", 0x23, 0x2F, 0, L"swap weapons", 0, K_KEY}, {"map", 0x10, 0x0F, 0, L"map", 0, K_KEY},
    {"next_set", 0x27, 0x12, 0, L"next spell set", 0, K_KEY}, {"prev_set", 0x28, 0x11, 0, L"previous spell set", 0, K_KEY},
    {"trap", 0x2A, 0x14, 1, L"trap / bomber", 0, K_KEY}, {"quicksave", 0x35, 0x3C, 0, L"quick save", 0, K_KEY},
    {"taunt", 0x2E, 0x24, 0, L"taunt", 0, K_KEY}, {"laugh", 0x2F, 0x26, 0, L"laugh", 0, K_KEY},
    {"point", 0x30, 0x25, 0, L"point", 0, K_KEY},
    {"creatures", 0, 0, 0, L"creature orders", 0, K_CREATURES},
};
#define NACT (int)(sizeof k_actions / sizeof k_actions[0])

/* combo names in the settings file ("x", "l1_x", ...) and labels */
static const char *k_btn_names[C_BASE_N] = {
    "a", "b", "x", "y", "lt", "rt", "r1", "l3", "r3", "select", "start", "dpad_up", "dpad_down", "dpad_left", "dpad_right",
};
static const wchar_t *k_btn_short[C_BASE_N] = { /* on the HUD */
    L"A", L"B", L"X", L"Y", L"LT", L"RT", L"R1", L"L3", L"R3", L"Sel", L"St", L"DU", L"DD", L"DL", L"DR",
};
static const wchar_t *k_btn_long[C_BASE_N] = { /* in lists */
    L"A", L"B", L"X", L"Y", L"LT", L"RT", L"R1", L"L3", L"R3", L"Select", L"Start",
    L"D-pad up", L"D-pad down", L"D-pad left", L"D-pad right",
};
static inline void combo_name(int c, char *out, int n) {
    if (c < C_BASE_N) snprintf(out, n, "%s", k_btn_names[c]);
    else snprintf(out, n, "l1_%s", k_btn_names[c - C_BASE_N]);
}
static inline void combo_label(int c, int lng, wchar_t *out, int n) {
    const wchar_t *const *t = lng ? k_btn_long : k_btn_short;
    if (c < C_BASE_N) swprintf(out, n, L"%ls", t[c]);
    else swprintf(out, n, lng ? L"L1 + %ls" : L"L+%ls", t[c - C_BASE_N]);
}
static inline int combo_by_name(const char *s) {
    char b[24];
    for (int c = 0; c < S_N; c++) { combo_name(c, b, sizeof b); if (!_stricmp(b, s)) return c; }
    return -1;
}

static const char *const k_class_names[3] = {"warrior", "wizard", "conjurer"};

/* the default combos of an action (up to 2; "" none) for a class (-1: shared) */
static inline void default_combos(int cl, const char *a, const char **c1, const char **c2) {
    static const struct { const char *a, *c1, *c2; } t[] = {
        {"attack", "a", ""}, {"aim", "r1", ""}, {"interact", "r3", ""}, {"wheel", "l3", ""},
        {"inventory", "select", ""}, {"book", "l1_select", ""}, {"menu", "start", ""},
        {"slot1", "x", ""}, {"slot2", "y", ""}, {"slot3", "b", "l1_x"}, {"slot4", "l1_y", ""}, {"slot5", "l1_b", ""},
        {"jump", "lt", ""}, {"quicksave", "rt", ""}, {"health", "dpad_up", ""}, {"swap", "dpad_down", ""},
        {"map", "dpad_left", ""}, {"next_set", "dpad_right", "l1_rt"}, {"prev_set", "l1_r1", ""},
        {"mana", "l1_dpad_up", ""}, {"cure", "l1_dpad_down", ""}, {"trap", "l1_a", ""},
    };
    *c1 = *c2 = "";
    if (cl == 0 && (!strcmp(a, "next_set") || !strcmp(a, "prev_set") || !strcmp(a, "mana") || !strcmp(a, "trap"))) return; /* warrior */
    if (cl == 2 && !strcmp(a, "creatures")) { *c1 = "dpad_right"; return; }              /* conjurer */
    if (cl == 2 && !strcmp(a, "next_set")) { *c1 = "l1_rt"; return; }
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcmp(t[i].a, a)) { *c1 = t[i].c1; *c2 = t[i].c2; return; }
}

/* the actions a class can use (the warrior has no mana or spells; the creature orders are the conjurer's) */
static inline int class_has(int cl, const char *n) {
    if (!strcmp(n, "creatures")) return cl == 2;
    if (cl == 0 && (!strcmp(n, "mana") || !strcmp(n, "next_set") || !strcmp(n, "prev_set") || !strcmp(n, "trap") || !strcmp(n, "cast_recent"))) return 0;
    return 1;
}
