/* Fixes of the original game's bugs.

   Force shield lost after loading a save:
   The shield is an enchant on the unit (26, its timer at unit +0x158 + 2 * 26; 0 = no time limit) plus a duration
   spell (51) holding its strength (record +0x48); the spell's end frame is +0x44, equal to its start +0x3C when it
   has none. In this version ShieldDuration is 0: a cast shield has no time limit (enchant timer 0, spell without an
   end) and lasts until hits use up its strength. A save stores the enchant's timer (0); loading treats a stored 0 as
   missing and gives the enchant one second (game FPS frames), after which the timer takes the shield off.
   (Seen with [game] debug_shield: enchant ON at frame 1, timer 30, OFF at frame 31 from the unit's enchant update.)
   Fix: before the duration spells' end check (0x4FDDA3), a shield spell without an end gives its unit's shield
   enchant no time limit again. A shield with a real end is left alone. */
#include "onc.h"
#include <stdio.h>
#include <string.h>

void __cdecl onc_fix_dur_spell(uint8_t *rec) {
    if (!rec || *(uint32_t *)(rec + 4) != 51) return;
    if (*(uint32_t *)(rec + 0x44) != *(uint32_t *)(rec + 0x3C)) return; /* the spell has an end: as it is */
    uint8_t *u = *(uint8_t **)(rec + 0x30);
    if (!u || !(*(uint32_t *)(u + 0x154) & (1u << 26))) return;
    *(uint16_t *)(u + 0x158 + 2 * 26) = 0; /* no time limit, as when it was cast */
}
/* 0x4FDDA3: mov eax, [esi+0x44]; mov ecx, [esi+0x3C] */
__asm__(".globl _onc_fix_dur_stub\n_onc_fix_dur_stub:\n"
        "  pushal\n  pushl %esi\n  call _onc_fix_dur_spell\n  addl $4, %esp\n  popal\n"
        "  movl 0x44(%esi), %eax\n  movl 0x3C(%esi), %ecx\n"
        "  pushl $0x4FDDA9\n  ret\n");
void onc_fix_dur_stub(void);

/* ---------------- shield debugging ([game] debug_shield = 1) ---------------- */
static void (__cdecl *o_buff_off)(void *, int);
static void (__cdecl *o_buff_on)(void *, int, int, int);
static uint8_t *host(void) { return ONC_I32(NOX_CLIENT_CONNECTED) ? *(uint8_t **)0x97C660 : NULL; }
static void stack_trace(char *out, int n, void **sp) {
    int k = 0;
    out[0] = 0;
    for (int i = 0; i < 48 && k < 6; i++) {
        uintptr_t v = (uintptr_t)sp[i];
        if (v > 0x401000 && v < 0x57F000) {
            size_t l = strlen(out);
            snprintf(out + l, n - l, " %06X", (unsigned)v);
            k++;
        }
    }
}
static void __cdecl dbg_buff_off(void *u, int id) {
    if (id == 26) {
        char t[128];
        stack_trace(t, sizeof t, (void **)__builtin_frame_address(0) + 1);
        onc_log("shield dbg: enchant 26 OFF unit %p (host %p) frame %d, from%s", u, host(), ONC_I32(0x9667C4), t);
    }
    o_buff_off(u, id);
}
static void __cdecl dbg_buff_on(void *u, int id, int dur, int lvl) {
    if (id == 26) {
        char t[128];
        stack_trace(t, sizeof t, (void **)__builtin_frame_address(0) + 1);
        onc_log("shield dbg: enchant 26 ON unit %p (host %p) dur %d level %d frame %d, from%s", u, host(), dur, lvl,
                ONC_I32(0x9667C4), t);
    }
    o_buff_on(u, id, dur, lvl);
}
static int g_dbg;
void onc_fix_debug_frame(void) {
    if (!g_dbg) return;
    static int last_on = -1, last_left = -1, last_f = 0; static void *last_rec = (void *)1, *last_u;
    static uint32_t last44 = 1, last48 = 1, last58 = 1;
    uint8_t *u = host();
    int on = 0, left = 0;
    uint8_t *rec = NULL;
    if (u) {
        on = !!(*(uint32_t *)(u + 0x154) & (1u << 26));
        left = *(uint16_t *)(u + 0x158 + 2 * 26);
        for (uint8_t *r = *(uint8_t **)0x7513B4; r; r = *(uint8_t **)(r + 0x74))
            if (*(uint32_t *)(r + 4) == 51 && *(uint8_t **)(r + 0x30) == u) { rec = r; break; }
    }
    uint32_t r44 = rec ? *(uint32_t *)(rec + 0x44) : 0, r48 = rec ? *(uint32_t *)(rec + 0x48) : 0,
             r58 = rec ? *(uint32_t *)(rec + 0x58) : 0;
    int f = ONC_I32(0x9667C4);
    int changed = u != last_u || on != last_on || rec != last_rec || r44 != last44 || r48 != last48 || r58 != last58 ||
                  (on && f - last_f >= 30) || (left > last_left && last_left >= 0);
    if (changed && (on || rec || last_on > 0 || last_rec)) {
        onc_log("shield dbg: frame %d host %p enchant %d left %d | spell %p start %u end %u strength %d flags %X",
                f, u, on, left, rec, rec ? *(uint32_t *)(rec + 0x3C) : 0, r44, (int)r48, r58);
        last_f = f;
    }
    last_u = u; last_on = on; last_left = left; last_rec = rec; last44 = r44; last48 = r48; last58 = r58;
}

static int patch(uint32_t a, const void *b, size_t n) {
    DWORD old;
    if (!VirtualProtect((void *)a, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy((void *)a, b, n);
    VirtualProtect((void *)a, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)a, n);
    return 1;
}

/* 0x4C2450(x, y?, row): is x inside the line-of-sight spans of screen row `row` (count 0x9359E0[row], edges at
   0x936800 + row * 128; moved by light.c). Effects drawn partly above or below the screen ask for a row outside the
   table (seen: row -48 from 0x4C1590, a ray / lightning drawer, with the world zoomed out), which reads a garbage
   count before the table and runs off the end of memory. Rows outside the screen are simply not inside. */
static int (__cdecl *o_span_in)(int, int, int);
static int __cdecl span_in(int a, int b, int row) {
    int h = ONC_I32(NOX_SCREEN_H);
    if (h > 3000) h = 3000;
    if (row < 0 || row >= h) return 0;
    return o_span_in(a, b, row);
}

/* 0x47CF60(drawable): links a client drawable into the drawable grid (0x6F1ECC: 47 columns of 47 cells, 128 units
   each = 6016 units; count 0x6F1ED0; cell = drawable x / 128, y / 128; list next +0x190, prev +0x194, cell +0x18C;
   unlink 0x47CF10). The grid searches (0x47D0A0, 0x47D150...) clamp their cells to the grid, this one does not.
   Charm and heal orbs (charmup.c 0x4CC460, healup.c) that would start outside the view are moved onto the view's
   edge; with our wide screens the view reaches past the grid near the east / south edge of a map (seen: an orb at
   x 6700 = column 52 while charming a bear at 2560x1440), so the column read heap memory after the 47 pointers and
   wrote into it (crash at 0x47CFC9, or silent memory damage). Same code, with the cell clamped to the grid. */
static void __cdecl dgrid_link(uint8_t *d) {
    int n = ONC_I32(0x6F1ED0);
    int x = *(int32_t *)(d + 0x0C), y = *(int32_t *)(d + 0x10);
    int cx = x >> 7, cy = y >> 7; /* floor division, as the game's cdq / and 0x7F / sar 7 */
    if (cx < 0) cx = 0; else if (cx >= n) cx = n - 1;
    if (cy < 0) cy = 0; else if (cy >= n) cy = n - 1;
    if (*(void **)(d + 0x18C)) ((void (__cdecl *)(void *))0x47CF10)(d);
    uint8_t ***grid = *(uint8_t ****)0x6F1ECC;
    uint8_t **cell = &grid[cx][cy];
    *(uint8_t **)(d + 0x194) = NULL;
    *(uint8_t **)(d + 0x190) = *cell;
    if (*cell) *(uint8_t **)(*cell + 0x194) = d;
    *cell = d;
    *(uint8_t ***)(d + 0x18C) = cell;
}

void onc_fixes_init(void) {
    {
        static const uint8_t e_link[] = {0x53, 0x56, 0x8B, 0x74, 0x24, 0x0C};
        onc_hook_detour("fix: drawable grid cell clamp", 0x47CF60, dgrid_link, e_link, 6);
    }
    {
        static const uint8_t e_span[] = {0x8B, 0x44, 0x24, 0x0C, 0x33, 0xC9};
        o_span_in = (int (__cdecl *)(int, int, int))onc_hook_detour("fix: line-of-sight row check", 0x4C2450, span_in, e_span, 6);
    }
    g_dbg = onc_cfg_int("game", "debug_shield", 0);
    if (g_dbg) {
        static const uint8_t e_off[] = {0x56, 0x8B, 0x74, 0x24, 0x0C};
        static const uint8_t e_on[] = {0xA1, 0xC0, 0x13, 0x75, 0x00};
        o_buff_off = onc_hook_detour("shield dbg: enchant off", 0x4FE300, dbg_buff_off, e_off, 5);
        o_buff_on = onc_hook_detour("shield dbg: enchant on", 0x4FE1D0, dbg_buff_on, e_on, 5);
        if (!o_buff_off || !o_buff_on) g_dbg = 0;
    }
    static const uint8_t e_dur[] = {0x8B, 0x46, 0x44, 0x8B, 0x4E, 0x3C};
    if (onc_cfg_int("game", "fix_shield_load", 1)) {
        if (!memcmp((void *)0x4FDDA3, e_dur, 6)) {
            uint8_t b[6] = {0xE9, 0, 0, 0, 0, 0x90};
            int32_t rel = (int32_t)((uintptr_t)onc_fix_dur_stub - (0x4FDDA3 + 5));
            memcpy(b + 1, &rel, 4);
            patch(0x4FDDA3, b, 6);
            onc_log("fix: force shield kept after loading");
        } else onc_log("fix: force shield patch refused (bytes differ)");
    }
}
