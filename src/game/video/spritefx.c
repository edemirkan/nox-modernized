/* Edge smoothing, to soften the hard pixel outlines at high resolutions, done on the final pixels while the world is drawn:
   - edge smoothing: a sprite's outline is blended with what is behind it (Nox sprites have hard, cut-out edges),
     plus a faint 1 pixel fringe outside the outline;
   - softening: a light 4-neighbour blur inside the sprite;
   - floor softening: the same blur over the whole floor, right after the floor tiles are drawn.
   Objects (characters, monsters, items) and walls have their own levels: 0 off, 1 low, 2 medium, 3 high.

   [display] sprite_edges, sprite_soften, wall_edges, wall_soften, floor_soften = 0..3

   Game.exe:
   - 0x46C9E0(vp) draws the world (called at 0x42D6F0): only sprites drawn inside it are smoothed (never the GUI).
   - 0x480FD0 draws an image; at 0x48100A it calls the type dispatcher through [0x952CC4] (img, x, y).
     Types 3..6 are the sprites. Skipped while [0x952CB4] (a height cut) or [0x952C94] is set.
   - Walls: 0x46ADE0(vp, wall), called at 0x46CDA6 (back walls), 0x46CFEA (walls sorted with objects) and
     0x46D04C (front walls). With lighting on, walls are drawn by the lit-wall drawer [0x95476C] (img, x, y, + 7),
     called at 0x46B5ED; otherwise through 0x480FD0.
   - 0x4C2320(vp), called at 0x46CC8E, blacks out what is not floor, right after the tiles: the floor point.
   - Image data (0x4314A0(img)): width, height, offset x, offset y (int32), 1 byte, then runs of (op, count):
     op & 15 = 1/6 transparent, 2/7 16-bit pixels, 4 indexed (1 byte), 5 RGBA4444 (2 bytes).
   Pixels are RGB565. */
#include "onc.h"
#include <stdlib.h>
#include <string.h>

/* ---------------- settings ---------------- */
static int g_lv[2][2];  /* [objects, walls][edges, soften] */
static int g_floor;
static int g_world, g_walls;

static int clamp3(int v) { return v < 0 ? 0 : v > 3 ? 3 : v; }

/* ---------------- sprite shapes (cached per image) ---------------- */
typedef struct info {
    struct info *next;
    void *img;
    const uint8_t *data;
    int w, h, offx, offy; /* the extended box: the sprite plus a 1 pixel border */
    uint8_t *mask;        /* NULL: this sprite is not handled */
    int32_t *inner, *fringe; /* index << 4 | sprite pixels among the 9 around */
    int ninner, nfringe;
} info_t;

#define HBITS 12
static info_t *g_tab[1 << HBITS];
static uint8_t *g_arena; static size_t g_arena_left;
static void **g_chunks; static int g_nchunks, g_capchunks;
static size_t g_arena_total;

static void cache_flush(void) {
    for (int i = 0; i < g_nchunks; i++) free(g_chunks[i]);
    g_nchunks = 0; g_arena = NULL; g_arena_left = 0; g_arena_total = 0;
    memset(g_tab, 0, sizeof g_tab);
}
static void *arena(size_t n) {
    n = (n + 7) & ~(size_t)7;
    if (n > g_arena_left) {
        size_t sz = n > (1 << 20) ? n : (1 << 20);
        if (g_arena_total + sz > (64u << 20)) cache_flush();
        if (g_nchunks == g_capchunks) {
            int nc = g_capchunks ? g_capchunks * 2 : 64;
            void **p = realloc(g_chunks, nc * sizeof *p);
            if (!p) return NULL;
            g_chunks = p; g_capchunks = nc;
        }
        uint8_t *c = malloc(sz);
        if (!c) return NULL;
        g_chunks[g_nchunks++] = c; g_arena = c; g_arena_left = sz; g_arena_total += sz;
    }
    void *r = g_arena; g_arena += n; g_arena_left -= n;
    return r;
}

/* parsing scratch (grown as needed) */
static uint8_t *g_pm; static int32_t *g_pi, *g_pf; static int g_pmcap, g_picap, g_pfcap;
static int grow(void **p, int *cap, int need, int elem) {
    if (need <= *cap) return 1;
    int n = need * 2;
    void *q = realloc(*p, (size_t)n * elem);
    if (!q) return 0;
    *p = q; *cap = n;
    return 1;
}

/* parses the sprite into the scratch buffers; returns 1 and fills w, h, ni, nf */
static int parse(const uint8_t *d, int *pw, int *ph, int *pni, int *pnf) {
    int32_t width = ((const int32_t *)d)[0], height = ((const int32_t *)d)[1];
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048) return 0;
    int w = width + 2, h = height + 2;
    if (!grow((void **)&g_pm, &g_pmcap, w * h, 1)) return 0;
    uint8_t *mask = g_pm;
    memset(mask, 0, (size_t)w * h);
    const uint8_t *s = d + 17;
    for (int y = 0; y < height; y++) {
        uint8_t *row = mask + (size_t)(y + 1) * w + 1;
        for (int x = 0; x < width;) {
            int op = s[0] & 15, n = s[1];
            s += 2;
            if (n <= 0 || x + n > width) return 0;
            switch (op) {
            case 1: case 6: break;
            case 2: case 7: memset(row + x, 1, n); s += 2 * n; break;
            case 4: memset(row + x, 1, n); s += n; break;
            case 5:
                for (int i = 0; i < n; i++) row[x + i] = (s[2 * i] & 15) >= 8;
                s += 2 * n;
                break;
            default: return 0;
            }
            x += n;
        }
    }
    int ni = 0, nf = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int i = y * w + x, cnt = 0, edge = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx, yy = y + dy;
                    int in = xx >= 0 && yy >= 0 && xx < w && yy < h && mask[yy * w + xx];
                    if (in) cnt++;
                    else if ((dx == 0) != (dy == 0)) edge = 1;
                }
            if (mask[i]) {
                if (edge) {
                    if (!grow((void **)&g_pi, &g_picap, ni + 1, 4)) return 0;
                    g_pi[ni++] = i << 4 | cnt;
                }
            } else if (cnt > 0 && ((x > 0 && mask[i - 1]) || (x + 1 < w && mask[i + 1]) ||
                                   (y > 0 && mask[i - w]) || (y + 1 < h && mask[i + w]))) {
                if (!grow((void **)&g_pf, &g_pfcap, nf + 1, 4)) return 0;
                g_pf[nf++] = i << 4 | cnt;
            }
        }
    *pw = w; *ph = h; *pni = ni; *pnf = nf;
    return 1;
}

static info_t *lookup(void *img, const uint8_t *d) {
    unsigned hsh = ((uintptr_t)img >> 3) * 2654435761u >> (32 - HBITS);
    info_t *old = NULL;
    for (info_t *p = g_tab[hsh]; p; p = p->next)
        if (p->img == img) {
            if (p->data == d) return p->mask ? p : NULL;
            old = p; /* the image was reloaded elsewhere: parse it again */
            break;
        }
    int w = 0, h = 0, ni = 0, nf = 0;
    int ok = parse(d, &w, &h, &ni, &nf);
    size_t sz = sizeof(info_t) + (ok ? (size_t)(ni + nf) * 4 + (size_t)w * h : 0);
    size_t before = g_arena_total;
    info_t *inf = arena(sz); /* the only allocation: a flush can only happen here, before it is filled */
    if (!inf) return NULL;
    if (g_arena_total < before) old = NULL; /* flushed: the old entry is gone */
    memset(inf, 0, sizeof *inf);
    inf->img = img; inf->data = d;
    if (ok) {
        inf->inner = (int32_t *)(inf + 1);
        inf->fringe = inf->inner + ni;
        inf->mask = (uint8_t *)(inf->fringe + nf);
        memcpy(inf->inner, g_pi, (size_t)ni * 4);
        memcpy(inf->fringe, g_pf, (size_t)nf * 4);
        memcpy(inf->mask, g_pm, (size_t)w * h);
        inf->ninner = ni; inf->nfringe = nf; inf->w = w; inf->h = h;
        inf->offx = ((const int32_t *)d)[2] - 1;
        inf->offy = ((const int32_t *)d)[3] - 1;
    }
    if (old) { old->img = NULL; } /* never matched again */
    inf->next = g_tab[hsh]; g_tab[hsh] = inf;
    return inf->mask ? inf : NULL;
}

/* ---------------- blending (RGB565 spread into 32 bits with gaps) ---------------- */
#define SPREAD_MASK 0x07E0F81Fu
static inline uint32_t spread(uint16_t p) { return ((uint32_t)p | (uint32_t)p << 16) & SPREAD_MASK; }
static inline uint16_t pack(uint32_t v) { return (uint16_t)((v & 0xF81F) | ((v >> 16) & 0x07E0)); }
static inline uint32_t lerp(uint32_t a, uint32_t b, uint32_t t) { return ((a * (32 - t) + b * t) >> 5) & SPREAD_MASK; }
static inline uint32_t div3(uint32_t s, uint32_t n) {
    uint32_t b = (s & 0x7FF) / n, r = ((s >> 11) & 0x3FF) / n, g = ((s >> 21) & 0x7FF) / n;
    return (b | r << 11 | g << 21) & SPREAD_MASK;
}

static const uint32_t k_soft[4] = {0, 10, 18, 26};   /* how much of the blur is used, in 1/32 */
static const uint32_t k_inner[4] = {0, 10, 16, 22};  /* how transparent the outline may get, in 1/32 */
static const uint32_t k_fringe[4] = {0, 9, 14, 20};  /* how opaque the outer fringe may get, in 1/32 */

static uint16_t *g_bg, *g_src;
static int g_cap;

typedef struct {
    info_t *inf;
    int bx, by;       /* the extended box on screen */
    int rx, ry, rw, rh; /* its visible part */
    int edges, soft;
} job_t;

static uint16_t **rows_now(void) { return *(uint16_t ***)0x952C54; }

/* before the sprite is drawn: work out its rectangle and save what is under it */
static int fx_begin(job_t *j, void *img, const uint8_t *d, int x, int y, int edges, int soft) {
    if (!d) return 0;
    info_t *inf = lookup(img, d);
    if (!inf) return 0;
    uint16_t **rows = rows_now();
    if (!rows) return 0;
    int sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    int cl = 0, ct = 0, cr = sw, cb = sh;
    const int32_t *rd = *(const int32_t **)0x952FA0;
    if (rd && rd[0]) {
        if (rd[1] > cl) cl = rd[1];
        if (rd[2] > ct) ct = rd[2];
        if (rd[3] < cr) cr = rd[3];
        if (rd[4] < cb) cb = rd[4];
    }
    j->inf = inf;
    j->bx = x + inf->offx; j->by = y + inf->offy;
    int l = j->bx > cl ? j->bx : cl, t = j->by > ct ? j->by : ct;
    int r = j->bx + inf->w < cr ? j->bx + inf->w : cr, b = j->by + inf->h < cb ? j->by + inf->h : cb;
    if (l >= r || t >= b) return 0;
    j->rx = l; j->ry = t; j->rw = r - l; j->rh = b - t;
    int n = j->rw * j->rh;
    if (n > g_cap) {
        uint16_t *a = realloc(g_bg, (size_t)n * 2), *c = a ? realloc(g_src, (size_t)n * 2) : NULL;
        if (a) g_bg = a;
        if (!c) return 0;
        g_src = c; g_cap = n;
    }
    for (int yy = 0; yy < j->rh; yy++) memcpy(g_bg + yy * j->rw, rows[t + yy] + l, (size_t)j->rw * 2);
    j->edges = edges; j->soft = soft;
    return 1;
}

/* after it is drawn: soften inside, then blend the outline and the fringe */
static void fx_end(job_t *j) {
    uint16_t **rows = rows_now();
    info_t *inf = j->inf;
    int rw = j->rw, rh = j->rh, ox = j->rx - j->bx, oy = j->ry - j->by, W = inf->w;
    const uint8_t *mask = inf->mask;
    for (int yy = 0; yy < rh; yy++) memcpy(g_src + yy * rw, rows[j->ry + yy] + j->rx, (size_t)rw * 2);
#define INB(ex, ey) ((ex) - ox >= 0 && (ey) - oy >= 0 && (ex) - ox < rw && (ey) - oy < rh)
#define AT(ex, ey) (((ey) - oy) * rw + (ex) - ox)
#define DST(ex, ey) (rows[j->ry + (ey) - oy][j->rx + (ex) - ox])
    static const int dxy[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    uint32_t s = k_soft[j->soft];
    if (s) {
        for (int y = 0; y < rh; y++) {
            int ey = y + oy;
            for (int x = 0; x < rw; x++) {
                int ex = x + ox;
                if (!mask[ey * W + ex]) continue;
                uint32_t c = spread(g_src[y * rw + x]), sum = c * 4;
                for (int k = 0; k < 4; k++) {
                    int nx = ex + dxy[k][0], ny = ey + dxy[k][1];
                    sum += (INB(nx, ny) && mask[ny * W + nx]) ? spread(g_src[AT(nx, ny)]) : c;
                }
                uint32_t avg = (sum >> 3) & SPREAD_MASK;
                rows[j->ry + y][j->rx + x] = pack(lerp(c, avg, s));
            }
        }
    }
    if (j->edges) {
        uint32_t fl = k_fringe[j->edges];
        for (int k = 0; k < inf->nfringe; k++) {
            int v = inf->fringe[k], mi = v >> 4, cnt = v & 15, ex = mi % W, ey = mi / W;
            if (!INB(ex, ey)) continue;
            uint32_t sum = 0, num = 0;
            for (int q = 0; q < 4; q++) {
                int nx = ex + dxy[q][0], ny = ey + dxy[q][1];
                if (nx < 0 || ny < 0 || nx >= W || ny >= inf->h || !mask[ny * W + nx] || !INB(nx, ny)) continue;
                sum += spread(DST(nx, ny));
                num++;
            }
            if (!num) continue;
            uint32_t col = num == 1 ? sum : num == 2 ? (sum >> 1) & SPREAD_MASK
                         : num == 4 ? (sum >> 2) & SPREAD_MASK : div3(sum, 3);
            DST(ex, ey) = pack(lerp(spread(g_bg[AT(ex, ey)]), col, fl * cnt / 9));
        }
        uint32_t il = k_inner[j->edges];
        for (int k = 0; k < inf->ninner; k++) {
            int v = inf->inner[k], mi = v >> 4, cnt = v & 15, ex = mi % W, ey = mi / W;
            if (!INB(ex, ey)) continue;
            uint32_t t = 32 - il * (9 - cnt) / 9; /* the sprite's opacity, in 1/32 */
            DST(ex, ey) = pack(lerp(spread(g_bg[AT(ex, ey)]), spread(DST(ex, ey)), t));
        }
    }
#undef INB
#undef AT
#undef DST
}

/* ---------------- hooks ---------------- */
static int cur_edges(void) { return g_world ? g_lv[g_walls][0] : 0; }
static int cur_soft(void) { return g_world ? g_lv[g_walls][1] : 0; }

/* 0x48100A: call [0x952CC4](img, x, y) */
static void __cdecl fx_draw_image(void *img, int x, int y) {
    void (__cdecl *orig)(void *, int, int) = *(void (__cdecl **)(void *, int, int))0x952CC4;
    int e = cur_edges(), s = cur_soft();
    if ((e || s) && img && !ONC_I32(0x952CB4) && !ONC_I32(0x952C94)) {
        int t = ((uint8_t *)img)[10] & 0x3F;
        if (t >= 3 && t <= 6) {
            const uint8_t *(__cdecl *getdata)(void *) = *(const uint8_t *(__cdecl **)(void *))0x952CBC;
            job_t j;
            if (getdata && fx_begin(&j, img, getdata(img), x, y, e, s)) {
                orig(img, x, y);
                fx_end(&j);
                return;
            }
        }
    }
    orig(img, x, y);
}

/* 0x46B5ED: call [0x95476C](img, x, y, a3..a9), the lit-wall drawer; a8 is a height cut */
static int __cdecl fx_draw_litwall(void *img, int x, int y, int a3, int a4, int a5, int a6, int a7, int a8, int a9) {
    int (__cdecl *orig)(void *, int, int, int, int, int, int, int, int, int) =
        *(int (__cdecl **)(void *, int, int, int, int, int, int, int, int, int))0x95476C;
    int e = cur_edges(), s = cur_soft();
    if ((e || s) && img && a8 == 0 && (((uint8_t *)img)[10] & 0x3F) == 3) {
        job_t j;
        if (fx_begin(&j, img, ((const uint8_t *(__cdecl *)(void *))0x4314A0)(img), x, y, e, s)) {
            int r = orig(img, x, y, a3, a4, a5, a6, a7, a8, a9);
            fx_end(&j);
            return r;
        }
    }
    return orig(img, x, y, a3, a4, a5, a6, a7, a8, a9);
}

/* walls: 0x46ADE0(vp, wall) */
static void __cdecl fx_wall(void *vp, void *wall) {
    g_walls = 1;
    ((void (__cdecl *)(void *, void *))0x46ADE0)(vp, wall);
    g_walls = 0;
}

/* the world: 0x46C9E0(vp) */
void onc_fix_debug_frame(void);
void onc_hfr_draw_begin(void), onc_hfr_draw_end(void), onc_hfr_world_drawn(void);
static void __cdecl fx_world(void *vp) {
    onc_fix_debug_frame();
    g_world = 1; g_walls = 0;
    onc_hfr_draw_begin(); /* high frame rate: positions slid between ticks */
    ((void (__cdecl *)(void *))0x46C9E0)(vp);
    onc_hfr_draw_end();
    g_world = 0; g_walls = 0;
    onc_hfr_world_drawn();
}
/* the high frame rate's extra pictures draw the world through here too */
void onc_world_draw_extra(void) { fx_world((void *)0x687378); }

/* ---------------- floor softening (split over a few threads) ---------------- */
static uint16_t *g_ftmp; static int g_fcap;
static struct { int x0, y0, w, h, t; uint16_t **rows; } g_fl;
void onc_parallel(int n, void (*fn)(int, int, void *), void *arg);

static void floor_band(int y0, int y1) {
    int w = g_fl.w, h = g_fl.h; uint32_t t = (uint32_t)g_fl.t;
    const uint16_t *src = g_ftmp;
    for (int y = y0; y < y1; y++) {
        int up = y > 0 ? y - 1 : 0, dn = y + 1 < h ? y + 1 : h - 1;
        const uint16_t *r = src + (size_t)y * w, *ru = src + (size_t)up * w, *rd = src + (size_t)dn * w;
        uint16_t *out = g_fl.rows[g_fl.y0 + y] + g_fl.x0;
        for (int x = 0; x < w; x++) {
            int l = x > 0 ? x - 1 : 0, rr = x + 1 < w ? x + 1 : w - 1;
            uint32_t c = spread(r[x]);
            uint32_t sum = c * 4 + spread(r[l]) + spread(r[rr]) + spread(ru[x]) + spread(rd[x]);
            out[x] = pack(lerp(c, (sum >> 3) & SPREAD_MASK, t));
        }
    }
}

static void floor_job(int y0, int y1, void *arg) { (void)arg; floor_band(y0, y1); }

static void soften_floor(void *vp) {
    if (!g_floor || !vp) return;
    uint16_t **rows = rows_now();
    if (!rows) return;
    const int32_t *v = (const int32_t *)vp;
    int sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    int x0 = v[0] > 0 ? v[0] : 0, y0 = v[1] > 0 ? v[1] : 0;
    int x1 = v[0] + v[8] < sw ? v[0] + v[8] : sw, y1 = v[1] + v[9] < sh ? v[1] + v[9] : sh;
    int w = x1 - x0, h = y1 - y0;
    if (w < 3 || h < 3) return;
    if (w * h > g_fcap) {
        uint16_t *p = realloc(g_ftmp, (size_t)w * h * 2);
        if (!p) return;
        g_ftmp = p; g_fcap = w * h;
    }
    for (int y = 0; y < h; y++) memcpy(g_ftmp + (size_t)y * w, rows[y0 + y] + x0, (size_t)w * 2);
    g_fl.x0 = x0; g_fl.y0 = y0; g_fl.w = w; g_fl.h = h; g_fl.rows = rows; g_fl.t = (int)k_soft[g_floor];
    onc_parallel(h, floor_job, NULL); /* split over the helper threads (workers.c) */
}

/* 0x46CC8E: 0x4C2320(vp), right after the floor tiles */
static void __cdecl fx_floor(void *vp) {
    ((void (__cdecl *)(void *))0x4C2320)(vp);
    soften_floor(vp);
    void onc_aimcone_draw(void *vp, uint16_t **rows);
    onc_aimcone_draw(vp, rows_now()); /* the gamepad aim cone, on the floor only (aimcone.c) */
}

/* ---------------- install ---------------- */
static int patch_icall(const char *name, uintptr_t at, uint32_t ptr, void *repl) {
    uint8_t expect[6] = {0xFF, 0x15};
    memcpy(expect + 2, &ptr, 4);
    if (memcmp((void *)at, expect, 6)) { onc_log("%s: bytes differ, not installed", name); return 0; }
    uint8_t b[6] = {0xE8, 0, 0, 0, 0, 0x90};
    int32_t rel = (int32_t)((uintptr_t)repl - (at + 5));
    memcpy(b + 1, &rel, 4);
    DWORD old;
    VirtualProtect((void *)at, 6, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)at, b, 6);
    VirtualProtect((void *)at, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)at, 6);
    return 1;
}

void onc_spritefx_init(void) {
    g_lv[0][0] = clamp3(onc_cfg_int("display", "sprite_edges", 0));
    g_lv[0][1] = clamp3(onc_cfg_int("display", "sprite_soften", 0));
    g_lv[1][0] = clamp3(onc_cfg_int("display", "wall_edges", 0));
    g_lv[1][1] = clamp3(onc_cfg_int("display", "wall_soften", 0));
    g_floor = clamp3(onc_cfg_int("display", "floor_soften", 0));
    /* always installed: the F7 menu turns them on and off */
    onc_hook_call("smoothing: world", 0x42D6F0, fx_world, 0x46C9E0);
    onc_hook_call("smoothing: back walls", 0x46CDA6, fx_wall, 0x46ADE0);
    onc_hook_call("smoothing: walls", 0x46CFEA, fx_wall, 0x46ADE0);
    onc_hook_call("smoothing: front walls", 0x46D04C, fx_wall, 0x46ADE0);
    onc_hook_call("smoothing: floor", 0x46CC8E, fx_floor, 0x4C2320);
    patch_icall("smoothing: sprites", 0x48100A, 0x952CC4, fx_draw_image);
    patch_icall("smoothing: lit walls", 0x46B5ED, 0x95476C, fx_draw_litwall);

    onc_log("smoothing: sprites edges %d soften %d, walls edges %d soften %d, floor %d",
            g_lv[0][0], g_lv[0][1], g_lv[1][0], g_lv[1][1], g_floor);
}

/* the F7 menu: which = 0 sprite edges, 1 sprite soften, 2 wall edges, 3 wall soften, 4 floor */
int onc_smooth_get(int which) {
    if (which >= 0 && which < 4) return g_lv[which / 2][which % 2];
    return which == 4 ? g_floor : 0;
}
void onc_smooth_set(int which, int v) {
    v = clamp3(v);
    if (which >= 0 && which < 4) g_lv[which / 2][which % 2] = v;
    else if (which == 4) g_floor = v;
}
