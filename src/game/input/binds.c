#include "binds.h"
#include <stdio.h>
#include <string.h>
#include "binds_table.h"

#define NOX_PLAYER_INFO 0x96B8D0 /* +0x8CB: class */
#define NOX_BINDINGS    0x68B15C /* the key configuration: list of {keys[8] +0, nkeys +0x20, events[8] +0x24,
                                    nevents +0x44, next +0x4C} */

static int g_shared[S_N];  /* action per combo, the same for every class */
static int g_map[3][S_N];  /* action per class and combo (only where no shared action is) */
static wchar_t g_lab[2][S_N][16];

static int action_by_name(const char *n) {
    for (int i = 0; i < NACT; i++) if (!_stricmp(k_actions[i].name, n)) return i;
    return -1;
}

/* one action's combos from the settings file (or its defaults when the key is not there) into map */
static void read_action(int *map, const char *sec, int cl, int a) {
    char v[128];
    onc_cfg_str(sec, k_actions[a].name, "\x01", v, sizeof v);
    if (v[0] == 1) { /* not listed: the defaults */
        const char *c1, *c2;
        default_combos(cl, k_actions[a].name, &c1, &c2);
        snprintf(v, sizeof v, "%s %s", c1, c2);
    }
    for (char *t = strtok(v, ", \t"); t; t = strtok(NULL, ", \t")) {
        if (!_stricmp(t, "none")) continue;
        int c = combo_by_name(t);
        if (c < 0) { onc_log("binds: [%s] %s = %s: unknown button combo", sec, k_actions[a].name, t); continue; }
        if (map[c] && map[c] != a) onc_log("binds: [%s] %s and %s both on %s: %s kept", sec, k_actions[map[c]].name, k_actions[a].name, t, k_actions[map[c]].name);
        else map[c] = a;
    }
}

void onc_binds_init(void) {
    memset(g_shared, 0, sizeof g_shared);
    for (int a = 1; a < NACT; a++) if (k_actions[a].shared) read_action(g_shared, "shared", -1, a);
    /* the older switch for the wheel: off unless [shared] names its combo */
    char v[8];
    onc_cfg_str("shared", "wheel", "\x01", v, sizeof v);
    if (v[0] == 1 && !onc_cfg_int("pad", "radial_menu", 1))
        for (int c = 0; c < S_N; c++) if (k_actions[g_shared[c]].kind == K_WHEEL) g_shared[c] = 0;
    for (int cl = 0; cl < 3; cl++) {
        const char *sec = k_class_names[cl];
        int map[S_N] = {0};
        for (int a = 1; a < NACT; a++)
            if (!k_actions[a].shared && class_has(cl, k_actions[a].name)) read_action(map, sec, cl, a);
        for (int c = 0; c < S_N; c++) { /* the older form: combo = action */
            char key[24], val[32];
            combo_name(c, key, sizeof key);
            onc_cfg_str(sec, key, "", val, sizeof val);
            int a = val[0] ? action_by_name(val) : -1;
            if (a > 0 && !k_actions[a].shared) map[c] = a;
        }
        for (int c = 0; c < S_N; c++) {
            if (map[c] && g_shared[c]) { /* one combo, one thing: the shared action has it */
                char n[24]; combo_name(c, n, sizeof n);
                onc_log("binds: [%s] %s = %s: %s is the shared %s", sec, k_actions[map[c]].name, n, n, k_actions[g_shared[c]].name);
                map[c] = 0;
            }
            g_map[cl][c] = map[c];
        }
    }
    for (int c = 0; c < S_N; c++) { combo_label(c, 0, g_lab[0][c], 16); combo_label(c, 1, g_lab[1][c], 16); }
}

int onc_player_class(void) {
    void *info = *(void **)NOX_PLAYER_INFO;
    if (!ONC_I32(NOX_CLIENT_CONNECTED) || !info) return -1;
    int c = *(uint8_t *)((char *)info + 0x8CB);
    return c >= 0 && c < 3 ? c : 0;
}
static int cur_class(void) { int c = onc_player_class(); return c < 0 ? 0 : c; }

int onc_bind_action(int c) {
    if (c < 0 || c >= S_N) return 0;
    return g_shared[c] ? g_shared[c] : g_map[cur_class()][c];
}
int onc_bind_kind(int a) { return a > 0 && a < NACT ? k_actions[a].kind : -1; }
const wchar_t *onc_bind_title(int a) { return a >= 0 && a < NACT ? k_actions[a].title : L"-"; }
const wchar_t *onc_combo_label(int c, int lng) { return c >= 0 && c < S_N ? g_lab[lng != 0][c] : L"?"; }

int onc_bind_has_kind(int kind) {
    for (int c = 0; c < S_N; c++) if (onc_bind_kind(onc_bind_action(c)) == kind) return 1;
    return 0;
}
int onc_bind_buttons_of_kind(int kind, int l1) {
    int bits = 0;
    for (int b = 0; b < C_BASE_N; b++) if (onc_bind_kind(onc_bind_action(l1 ? S_L1(b) : b)) == kind) bits |= 1 << b;
    return bits;
}

/* the keyboard key the game's key configuration binds to an action (alone, not with other keys) */
static BYTE key_for_event(int ev, BYTE def) {
    char *b = *(char **)NOX_BINDINGS;
    if (!b || ev <= 0) return def;
    for (int guard = 0; b && guard < 512; b = *(char **)(b + 0x4C), guard++) {
        int nk = *(int32_t *)(b + 0x20), ne = *(int32_t *)(b + 0x44);
        if (nk != 1 || ne < 1 || ne > 8) continue;
        uint32_t k = *(uint32_t *)b;
        if (k == 0 || k > 0xFF) continue; /* mouse buttons */
        for (int i = 0; i < ne; i++)
            if (*(int32_t *)(b + 0x24 + 4 * i) == ev) return (BYTE)k;
    }
    return def;
}
BYTE onc_bind_key_of(int a, int *aim) {
    if (aim) *aim = 0;
    if (a <= 0 || a >= NACT) return 0;
    if (aim) *aim = k_actions[a].aim;
    return key_for_event(k_actions[a].event, k_actions[a].def_key);
}

/* the first combo doing it; R3 last (pressing it can zoom instead) */
static int first_combo(int (*match)(int a, intptr_t arg), intptr_t arg) {
    for (int pass = 0; pass < 2; pass++)
        for (int c = 0; c < S_N; c++) {
            int r3 = c == C_R3 || c == S_L1(C_R3);
            if ((pass == 0) == r3) continue;
            if (match(onc_bind_action(c), arg)) return c;
        }
    return -1;
}
static int match_event(int a, intptr_t ev) { return a > 0 && k_actions[a].event > 0 && k_actions[a].event == (int)ev; }
static int match_name(int a, intptr_t n) { return a > 0 && !_stricmp(k_actions[a].name, (const char *)n); }
const wchar_t *onc_bind_label(int ev) { int c = first_combo(match_event, ev); return c >= 0 ? g_lab[0][c] : NULL; }
const wchar_t *onc_bind_label_action(const char *name) { int c = first_combo(match_name, (intptr_t)name); return c >= 0 ? g_lab[0][c] : NULL; }
const wchar_t *onc_bind_label_action_long(const char *name) { int c = first_combo(match_name, (intptr_t)name); return c >= 0 ? g_lab[1][c] : NULL; }
