/* The gamepad aim cone, showing where the lock-on looks for targets: the floor inside the lock-on cone
   (lock_on_cone_degrees wide, lock_on_range long, from the player along the aim) is brightened, right after the floor
   tiles are drawn (0x46CC8E, with the floor softening), so units, items and walls are drawn over it. Multiplicative
   (c + c * k, dark floor stays dark), soft side edges, a fade in near the player and out towards the end of the
   range, ordered dithering against the banding of the 5 / 6 bit channels. Split over the helper threads.
   [pad] aim_cone: 0 off, 1 while the aim combo (R1) is held (default), 2 always while the left stick plays, brighter
   with R1; aim_cone_strength: percent (default 20 with R1; "always" shows half of it without R1).
   Direction: the left stick (what the aim combo turns), else the player's facing (0x68B160). Pixels are RGB565;
   the world viewport (0x687378): +0 / +4 screen x / y, +0x10 / +0x14 world x / y, +0x20 / +0x24 size. */
#include "onc.h"
#include <math.h>

int onc_pad_aim_state(int *r1, float *fx, float *fy); /* pad.c: 1 = the left stick plays (cone allowed) */
void onc_lockon_cone(int *range, int *half_deg);       /* targets.c */
void onc_parallel(int n, void (*fn)(int, int, void *), void *arg);

static int c_mode = 1, c_strength = 20;
int onc_aimcone_get(void) { return c_mode; }
void onc_aimcone_set(int m) { c_mode = m < 0 ? 0 : m > 2 ? 2 : m; }

static const uint8_t bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static struct {
    uint16_t **rows;
    int bx0, by0, bx1; /* box, rows by0 + band */
    float px, py, fx, fy, rng, half, soft, cos_out, inner, k;
} g_c;

static void cone_band(int y0, int y1, void *arg) {
    (void)arg;
    const float rng = g_c.rng, inner = g_c.inner, half = g_c.half, soft = g_c.soft;
    for (int yy = y0; yy < y1; yy++) {
        int y = g_c.by0 + yy;
        float dy = y - g_c.py;
        uint16_t *row = g_c.rows[y];
        for (int x = g_c.bx0; x < g_c.bx1; x++) {
            float dx = x - g_c.px, d = sqrtf(dx * dx + dy * dy);
            if (d < inner || d > rng) continue;
            float cs = (dx * g_c.fx + dy * g_c.fy) / d;
            if (cs < g_c.cos_out) continue;
            float w = 1.0f, ang = acosf(cs > 1 ? 1 : cs);
            if (ang > half) w = 1 - (ang - half) / soft;
            if (d < inner * 2) w *= (d - inner) / inner;
            float t = d / rng;
            w *= 1 - t * t;
            uint32_t k = (uint32_t)(g_c.k * w * 256);
            if (!k) continue;
            uint32_t p = row[x], r = p >> 11, g = (p >> 5) & 63, b = p & 31;
            uint32_t dth = (uint32_t)bayer4[(y & 3) << 2 | (x & 3)] << 4 | 8;
            r += (r * k + dth) >> 8; if (r > 31) r = 31;
            g += (g * k + dth) >> 8; if (g > 63) g = 63;
            b += (b * k + dth) >> 8; if (b > 31) b = 31;
            row[x] = (uint16_t)(r << 11 | g << 5 | b);
        }
    }
}

/* called right after the floor tiles (spritefx.c fx_floor) */
void onc_aimcone_draw(void *vp, uint16_t **rows) {
    if (!c_mode || !vp || !rows || !ONC_I32(NOX_CLIENT_CONNECTED)) return;
    int r1 = 0;
    float fx = 0, fy = 0;
    if (!onc_pad_aim_state(&r1, &fx, &fy)) return;
    if (c_mode == 1 && !r1) return;
    void *pl = *(void **)0x96A6A0;
    if (!pl) return;
    if (fx == 0 && fy == 0) {
        float a = ONC_I32(0x68B160) * (6.2831853f / 256.0f);
        fx = cosf(a); fy = sinf(a);
    }
    const int32_t *v = (const int32_t *)vp;
    int sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    int vx0 = v[0] > 0 ? v[0] : 0, vy0 = v[1] > 0 ? v[1] : 0;
    int vx1 = v[0] + v[8] < sw ? v[0] + v[8] : sw, vy1 = v[1] + v[9] < sh ? v[1] + v[9] : sh;
    int range, half_deg;
    onc_lockon_cone(&range, &half_deg);
    float px = (float)(v[0] + *(int32_t *)((char *)pl + 12) - v[4]);
    float py = (float)(v[1] + *(int32_t *)((char *)pl + 16) - v[5]);
    float half = half_deg * 0.0174533f, soft = 4 * 0.0174533f;
    float ho = half + soft > 3.14159f ? 3.14159f : half + soft;
    /* box of the sector */
    float a0 = atan2f(fy, fx), bx0 = px, by0 = py, bx1 = px, by1 = py;
    for (int i = -8; i <= 8; i++) {
        float a = a0 + ho * i / 8, x = px + cosf(a) * range, y = py + sinf(a) * range;
        bx0 = fminf(bx0, x); bx1 = fmaxf(bx1, x);
        by0 = fminf(by0, y); by1 = fmaxf(by1, y);
    }
    int ix0 = (int)bx0 - 2, iy0 = (int)by0 - 2, ix1 = (int)bx1 + 3, iy1 = (int)by1 + 3;
    if (ix0 < vx0) ix0 = vx0;
    if (iy0 < vy0) iy0 = vy0;
    if (ix1 > vx1) ix1 = vx1;
    if (iy1 > vy1) iy1 = vy1;
    if (ix1 <= ix0 || iy1 <= iy0) return;
    float k = c_strength / 100.0f;
    if (c_mode == 2 && !r1) k *= 0.5f;
    g_c.rows = rows; g_c.bx0 = ix0; g_c.by0 = iy0; g_c.bx1 = ix1;
    g_c.px = px; g_c.py = py; g_c.fx = fx; g_c.fy = fy; g_c.rng = (float)range;
    g_c.half = half; g_c.soft = soft; g_c.cos_out = cosf(ho); g_c.inner = 18; g_c.k = k;
    onc_parallel(iy1 - iy0, cone_band, NULL);
}

void onc_aimcone_init(void) {
    c_mode = onc_cfg_int("pad", "aim_cone", 1);
    onc_aimcone_set(c_mode);
    c_strength = onc_cfg_int("pad", "aim_cone_strength", 20);
    if (c_strength < 5) c_strength = 5; else if (c_strength > 100) c_strength = 100;
    onc_log("aim cone: %s, strength %d%%", c_mode == 0 ? "off" : c_mode == 1 ? "with the aim button" : "always",
            c_strength);
}
