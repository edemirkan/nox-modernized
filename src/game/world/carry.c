/* Carry weight limit, adjustable for players who prefer less inventory juggling: the game computes a player's carry capacity as
   (strength / class max strength * 1250 + 750) * k (0x4EDD00; k = 1.5, a double at 0x581C38 read by the fmul at
   0x4EDF58). Above it the player is overweight and slowed down. This option multiplies k.

   [game] carry_weight = 1 | 1.25 | 1.5 | 2 | 3 | 5 | 10 (also in the F7 menu)

   Game.exe: the fmul's address (0x4EDF5A) is pointed at our own k. The capacity is stored in the unit (+0x1EA) and in
   the player (+0xE44, shown in the inventory); the player's overweight flag is +0xE48. The host's unit is at
   0x97C660, its player at unit +0x2EC -> +0x114. Inventory: unit +0x1F8 first item, item +0x1F0 next, +0x1E8 weight.
   A change in the menu rescales the current capacity at once; later recalculations (level up, loading) use k. */
#include "onc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static double g_k = 1.5, g_base = 1.5, g_mult = 1;
static int g_ok;
static const double k_steps[] = {1, 1.25, 1.5, 2, 3, 5, 10};
#define NSTEPS 7

static uint8_t *host_unit(void) {
    if (!ONC_I32(NOX_CLIENT_CONNECTED)) return NULL;
    return *(uint8_t **)0x97C660;
}
static uint8_t *unit_player(uint8_t *u) {
    uint8_t *ud = u ? *(uint8_t **)(u + 0x2EC) : NULL;
    return ud ? *(uint8_t **)(ud + 0x114) : NULL;
}

/* the host's capacity and inventory weight; 0 if not in a game as the host */
int onc_carry_status(int *capacity, int *weight) {
    uint8_t *u = host_unit(), *pl = unit_player(u);
    if (!u || !pl) return 0;
    *capacity = *(uint16_t *)(u + 0x1EA);
    int w = 0, guard = 0;
    for (uint8_t *it = *(uint8_t **)(u + 0x1F8); it && guard < 10000; it = *(uint8_t **)(it + 0x1F0), guard++)
        w += *(uint8_t *)(it + 0x1E8);
    *weight = w;
    return 1;
}

static void apply(double m) {
    double old = g_mult;
    g_mult = m;
    g_k = g_base * m;
    uint8_t *u = host_unit(), *pl = unit_player(u);
    if (!u || !pl || old == m) return;
    uint16_t *cap = (uint16_t *)(u + 0x1EA);
    if (!*cap) return;
    double c = floor(*cap / old * m + 0.5);
    if (c > 65535) c = 65535;
    *cap = (uint16_t)c;
    *(uint16_t *)(pl + 0xE44) = (uint16_t)c;
    int capacity, weight;
    if (onc_carry_status(&capacity, &weight)) *(uint32_t *)(pl + 0xE48) = weight > capacity;
}

double onc_carry_get(void) { return g_mult; }
void onc_carry_step(int dir) {
    if (!g_ok) return;
    int i = 0;
    for (int k = 0; k < NSTEPS; k++) if (g_mult >= k_steps[k] - 0.001) i = k;
    i = (i + dir + NSTEPS) % NSTEPS;
    apply(k_steps[i]);
}
int onc_carry_available(void) { return g_ok; }

void onc_carry_init(void) {
    char s[32];
    onc_cfg_str("game", "carry_weight", "1", s, sizeof s);
    double m = atof(s);
    if (m < 1) m = 1;
    if (m > 10) m = 10;
    uint32_t *imm = (uint32_t *)0x4EDF5A;
    if (*(uint16_t *)0x4EDF58 != 0x0DDC || *imm != 0x581C38) { onc_log("carry weight: Game.exe differs, not installed"); return; }
    g_base = *(double *)0x581C38;
    if (!(g_base > 0 && g_base < 100)) g_base = 1.5;
    g_mult = m;
    g_k = g_base * m;
    uint32_t v = (uint32_t)(uintptr_t)&g_k;
    DWORD old;
    VirtualProtect(imm, 4, PAGE_EXECUTE_READWRITE, &old);
    *imm = v;
    VirtualProtect(imm, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), imm, 4);
    g_ok = 1;
    onc_log("carry weight: x%g (k %g)", g_mult, g_k);
}
