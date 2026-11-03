/* Bigger light grids and scanline tables, so screens above 1024x768 work (OpenNox enlarges the same arrays:
   lightGridW / lightGridH = 57 * 4 / 45 * 4 and nox_arr_956A00 = [maxHeight + 150]).
   The renderer keeps the light of the screen in fixed arrays of 57 columns x 45 rows of 23 pixels:
     0x966840  57 x 280 bytes     per column: a row index and 23 interpolated colours (the tile drawers)
     0x96AEC0  57 x 45 bytes      light levels (soft shadows, walls)          index x * 45 + y
     0x96B8E0  57 x 45 x 12       RGB light, 16.16 fixed point                index (x * 45 + y) * 12
   and the line-of-sight shape per screen row (count + 32 edges) for 900 rows:
     0x9359E0  904 ints, 0x936800  900 x 128 bytes
   A bigger screen writes past them into the globals behind (the player info pointer at 0x96B8D0, 0x97311C...).
   They move to bigger memory: 128 columns x 120 rows (2944 x 2760 pixels) and 3000 screen rows. For the rows,
   the "x * 45" index calculations are rewritten to "x * 120" in place (imul fits where the lea pair was), the
   column steps (+45 bytes, +540 bytes) change with them, and the row and column limits grow.
   The cone / polygon lights keep their 45-row edge lists (on the stack of 0x469200 / 0x498170): they stop
   at row 44 on very tall screens. */
#include "onc.h"
#include <string.h>

enum { G_COL, G_BYTE, G_RGB, G_CNT, G_SPAN, G_COLEND };
typedef struct { uint32_t addr; uint8_t off; uint32_t old; uint8_t grid; } ref_t;
static const ref_t k_refs[] = {
#include "light_refs.h"
};

#define NEW_W 128
#define NEW_H 120
#define NEW_ROWS 3000
static const uint32_t k_old_base[5] = {0x966840, 0x96AEC0, 0x96B8E0, 0x9359E0, 0x936800};

static int g_ok;
static void put(uint32_t a, const void *expect, const void *v, size_t n, const char *what) {
    if (memcmp((void *)a, expect, n)) { onc_log("light: %s at %08X refused: bytes differ", what, a); g_ok = 0; return; }
    DWORD old;
    VirtualProtect((void *)a, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)a, v, n);
    VirtualProtect((void *)a, n, old, &old);
}
static void put32(uint32_t a, uint32_t expect, uint32_t v, const char *what) { put(a, &expect, &v, 4, what); }
static void put8(uint32_t a, uint8_t expect, uint8_t v, const char *what) { put(a, &expect, &v, 1, what); }

/* where an old address inside a table lands in the new one */
static uint32_t remap(const ref_t *r, const uint32_t *nb) {
    int32_t off = (int32_t)(r->old - k_old_base[r->grid]);
    switch (r->grid) {
    case G_BYTE: return nb[G_BYTE] + (off / 45) * NEW_H + off % 45;                 /* [x][45] bytes */
    case G_RGB:  return nb[G_RGB] + (off / 540) * (NEW_H * 12) + off % 540;        /* [x][45] x 12 bytes */
    case G_COLEND: return nb[G_COL] + NEW_W * 280;
    default: return nb[r->grid] + off;                                             /* same layout */
    }
}

void onc_light_init(void) {
    if (!onc_cfg_int("display", "wide_light", 1)) return;
    unsigned n = sizeof k_refs / sizeof k_refs[0];
    for (unsigned i = 0; i < n; i++)
        if (*(uint32_t *)(k_refs[i].addr + k_refs[i].off) != k_refs[i].old) {
            onc_log("light: %08X does not match: bigger light grids not used", k_refs[i].addr);
            return;
        }
    uint32_t size[5] = {NEW_W * 280 + 64, NEW_W * NEW_H + 64, NEW_W * NEW_H * 12 + 7 * 12 + 64, (NEW_ROWS + 4) * 4,
                        NEW_ROWS * 128 + 256};
    uint32_t total = 0, nb[5];
    for (int i = 0; i < 5; i++) total += (size[i] + 63) & ~63u;
    uint8_t *mem = VirtualAlloc(NULL, total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) return;
    for (int i = 0, o = 0; i < 5; i++) { nb[i] = (uint32_t)mem + o; o += (size[i] + 63) & ~63u; }
    g_ok = 1;
    for (unsigned i = 0; i < n; i++) put32(k_refs[i].addr + k_refs[i].off, k_refs[i].old, remap(&k_refs[i], nb), "address");

    /* x * 45 + y  ->  x * NEW_H + y */
    { static const uint8_t o[] = {0x8D, 0x0C, 0x89, 0x8D, 0x14, 0xCA, 0x03, 0xCA};          /* 0x469179 ecx = 45 ecx + edx */
      uint8_t v[] = {0x69, 0xC9, NEW_H, 0, 0, 0, 0x03, 0xCA};                               /* imul ecx, ecx, H ; add ecx, edx */
      put(0x469179, o, v, 8, "index"); }
    { static const uint8_t o[] = {0x8D, 0x04, 0x89, 0x8D, 0x0C, 0xC3, 0x03, 0xC1};          /* 0x4694FF eax = 45 ecx + ebx */
      uint8_t v[] = {0x69, 0xC1, NEW_H, 0, 0, 0, 0x03, 0xC3};                               /* imul eax, ecx, H ; add eax, ebx */
      put(0x4694FF, o, v, 8, "index"); }
    { static const uint8_t o[] = {0x8D, 0x04, 0x80, 0x8D, 0x04, 0xC0};                      /* 0x49411E / 0x4941A1 eax *= 45 */
      uint8_t v[] = {0x69, 0xC0, NEW_H, 0, 0, 0};                                           /* imul eax, eax, H */
      put(0x49411E, o, v, 6, "index");
      put(0x4941A1, o, v, 6, "index"); }
    { static const uint8_t o1[] = {0x8D, 0x44, 0xAD, 0x00}, v1[] = {0x6B, 0xC5, NEW_H, 0x90}; /* 0x497E83 eax = ebp * H */
      static const uint8_t o2[] = {0x8D, 0x6C, 0xC5, 0x00}, v2[] = {0x90, 0x90, 0x90, 0x90}; /* 0x497E8F (ebp += 8 eax) dropped */
      put(0x497E83, o1, v1, 4, "index");
      put(0x497E8F, o2, v2, 4, "index"); }                                                  /* 0x497E99 ebp = eax + ebp + grid */
    { static const uint8_t o1[] = {0x8D, 0x04, 0xB6}, v1[] = {0x6B, 0xC6, NEW_H};           /* 0x498329 eax = esi * H */
      static const uint8_t o2[] = {0x8D, 0x0C, 0xC3}, v2[] = {0x8B, 0xCB, 0x90};            /* 0x49832E ecx = ebx */
      put(0x498329, o1, v1, 3, "index");
      put(0x49832E, o2, v2, 3, "index"); }                                                  /* 0x498335 ebx = eax + ecx + grid */
    { static const uint8_t o1[] = {0x8D, 0x04, 0x9B}, v1[] = {0x6B, 0xC3, NEW_H};           /* 0x49846B eax = ebx * H */
      static const uint8_t o2[] = {0x8D, 0x54, 0xC5, 0x00}, v2[] = {0x8B, 0xD5, 0x90, 0x90}; /* 0x498476 edx = ebp */
      put(0x49846B, o1, v1, 3, "index");
      put(0x498476, o2, v2, 4, "index"); }                                                  /* 0x49847A esi = eax + edx */
    put8(0x497F79, 0x2D, NEW_H, "column step");                                             /* add ebp, 45 */
    put8(0x4983A8, 0x2D, NEW_H, "column step");                                             /* add ebx, 45 */

    /* column limits: last column 56 -> W - 1, 55 -> W - 2 */
    put8(0x468DF2, 0x38, NEW_W - 1, "limit");
    put32(0x468DFD, 0x38, NEW_W - 1, "limit");
    put8(0x4694D8, 0x38, NEW_W - 1, "limit");
    put8(0x497AE8, 0x38, NEW_W - 1, "limit");
    put32(0x497AED, 0x37, NEW_W - 2, "limit");
    put8(0x497DD4, 0x38, NEW_W - 1, "limit");
    put32(0x497DDF, 0x38, NEW_W - 1, "limit");
    put8(0x498449, 0x38, NEW_W - 1, "limit");
    /* row limits: last row 44 -> H - 1 (not the polygon edge lists at 0x497AAC, see above) */
    put8(0x468E2F, 0x2C, NEW_H - 1, "limit");
    put32(0x468E3A, 0x2C, NEW_H - 1, "limit");
    put8(0x4694E1, 0x2C, NEW_H - 1, "limit");
    put8(0x497E31, 0x2C, NEW_H - 1, "limit");
    put32(0x497E3C, 0x2C, NEW_H - 1, "limit");
    put8(0x498452, 0x2C, NEW_H - 1, "limit");
    /* clear loops: the RGB grid (7 entries a pass) and the light levels (dwords + a byte) */
    put32(0x468B3F, 0x9A0, NEW_W * NEW_H, "clear");
    put32(0x468BF2, 0x9A0, NEW_W * NEW_H, "clear");
    put32(0x497B6C, 0x281, NEW_W * NEW_H / 4, "clear");
    put32(0x497BD0, 0x281, NEW_W * NEW_H / 4, "clear");
    /* the line-of-sight object list (0x477DA0) is sized from the screen at the start of a game; the world can be
       drawn bigger than the game's screen (ui_scale): size it for the biggest screen */
    { static int32_t max_w = NEW_W * 23, max_h = NEW_H * 23;
      put32(0x477DA2, 0x954764, (uint32_t)&max_h, "sight size");
      put32(0x477DB1, 0x95410C, (uint32_t)&max_w, "sight size"); }
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    onc_log("light grids %dx%d (was 57x45), scanline rows %d (was 900), %u addresses moved%s", NEW_W, NEW_H, NEW_ROWS, n,
            g_ok ? "" : " - SOME PATCHES REFUSED");
}
