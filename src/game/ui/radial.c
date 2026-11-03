#include "radial.h"
#include "gui.h"
#include "draw.h"
#include "inventory.h"
#include <math.h>
#include <string.h>
#include <wchar.h>

#define FN_PAUSE        0x413900 /* pause(on): solo games only */
#define FN_TYPE_BY_NAME 0x44CB10 /* thing type index by name */
#define FN_THING_NAME   0x44CA80 /* wchar *prettyName(type) */
#define FN_NEW_DRAWABLE 0x459D90 /* drawable *newDrawable(type): made only for drawing (the doll does the same) */
#define FN_DEL_DRAWABLE 0x45A000 /* deleteDrawable(dr) */
#define FN_DARKEN       0x484850 /* darkens a rectangle by half (x, y, w, h) */
#define FN_CIRCLE       0x4AFEA0 /* circle(x, y, r) in the rect color */
#define FN_USE          0x4657C0 /* use(drawable): its net code at +128 */
#define FN_SOUND        0x4528D0
#define FN_OBJ_BY_NET   0x4EBC10 /* server object by net code (solo / host game) */
#define NOX_UI_VIEWPORT 0x6D2518 /* the viewport the inventory draws items with */
#define NOX_PLAYER_INFO 0x96B8D0 /* +0x8CB class */
#define NOX_PLAYER_DR   0x96A6A0
#define NOX_HUD_TUBES   0x6D8998 /* {window, cur, max, color, color} x 7: health, mana, ... */
#define NOX_POISONED    0x6D9634
#define SND_OPEN 789   /* inventory open */
#define SND_CLOSE 790  /* inventory close */
#define SND_CLICK 921  /* shell click */
#define SND_SELECT 920 /* shell select */
#define SND_NO 925     /* no can do */

#define FOOD_SIMPLE 0x1
#define FOOD_APPLE  0x2
#define FOOD_JUG    0x4
#define FOOD_POTION 0x8
#define FOOD_HEALTH 0x10
#define FOOD_MANA   0x20
#define FOOD_CURE   0x40
#define FOOD_MUSHROOM 0x80

enum { CAT_POTIONS, CAT_FOOD, CAT_BAD, CAT_N };
static const float k_sector[CAT_N][2] = {{270, 90}, {270, 360}, {0, 90}};
static const wchar_t *k_cat_names[CAT_N] = {L"Potions", L"Food", L"Poisonous"};

#define MAXIT 8
#define MAXQ 64
typedef struct {
    int type;
    void *dr;          /* one of its inventory drawables (drawing, using) */
    void *icon;        /* a drawable made for drawing it when none is carried */
    int codes[64], n;  /* net codes of all carried */
    int pending;
    const wchar_t *name;
} item;
typedef struct { int cat, index; float a0, a1, mid; int dim; } slot;

static struct {
    int open, paused;
    item items[CAT_N][MAXIT];
    int count[CAT_N];
    slot slots[32];
    int nslots, sel;
    struct { void *dr; int code; } queue[MAXQ];
    int nq, pend_heal, pend_mana, pend_cure, pend_confuse;
} m = {.sel = -1};

static int g_on = 1;
void onc_radial_init(void) { g_on = 1; } /* whether a combo opens it: binds.c ([shared] wheel; the older [pad] radial_menu) */
int onc_radial_enabled(void) { return g_on; }

static void sound(int id, int vol) { ((void (__cdecl *)(int, int))FN_SOUND)(id, vol); }
static int left(const item *it) { return it->n - it->pending; }
static float norm(float a) { a = fmodf(a, 360); return a < 0 ? a + 360 : a; }

static int food_cat(void *dr) {
    if (!dr || !(*(uint32_t *)((char *)dr + 112) & 0x10)) return -1;
    uint32_t fc = *(uint32_t *)((char *)dr + 116);
    if (fc & (FOOD_HEALTH | FOOD_MANA | FOOD_CURE)) return CAT_POTIONS;
    if (fc & (FOOD_MUSHROOM | FOOD_JUG)) return CAT_BAD;
    if (fc & (FOOD_APPLE | FOOD_SIMPLE)) return CAT_FOOD;
    return -1;
}

static int class_(void) {
    void *info = *(void **)NOX_PLAYER_INFO;
    return info ? *(uint8_t *)((char *)info + 0x8CB) : 0;
}

static void layout(void) {
    m.nslots = 0;
    for (int c = 0; c < CAT_N; c++) {
        float s = k_sector[c][0], e = k_sector[c][1];
        int cells = m.count[c] ? m.count[c] : 1;
        float step = (e - s) / cells;
        for (int i = 0; i < cells && m.nslots < 32; i++) {
            float a = s + step * i, b = a + step;
            slot *sl = &m.slots[m.nslots++];
            sl->cat = c; sl->index = m.count[c] ? i : -1;
            sl->a0 = norm(fminf(a, b)); sl->a1 = norm(fmaxf(a, b)); sl->mid = norm((a + b) / 2);
        }
    }
}

static int slot_at(float a) {
    a = norm(a);
    for (int i = 0; i < m.nslots; i++) {
        slot *s = &m.slots[i];
        if (s->a0 <= s->a1 ? (a >= s->a0 && a < s->a1) : (a >= s->a0 || a < s->a1)) return i;
    }
    return -1;
}

static void free_icons(void) {
    for (int c = 0; c < CAT_N; c++)
        for (int i = 0; i < m.count[c]; i++)
            if (m.items[c][i].icon) {
                ((void (__cdecl *)(void *))FN_DEL_DRAWABLE)(m.items[c][i].icon);
                m.items[c][i].icon = NULL;
            }
}

static void collect(void) {
    static const char *pot_w[] = {"RedPotion", "CurePoisonPotion", NULL};
    static const char *pot_c[] = {"RedPotion", "BluePotion", "CurePoisonPotion", NULL};
    static const char *food[] = {"RedApple", "Meat", NULL};
    static const char *bad[] = {"Mushroom", "Cider", NULL};
    const char **fixed[CAT_N] = {class_() == 0 ? pot_w : pot_c, food, bad};
    free_icons();
    memset(m.items, 0, sizeof m.items);
    for (int c = 0; c < CAT_N; c++) {
        m.count[c] = 0;
        for (int i = 0; fixed[c][i]; i++) {
            int t = ((int (__cdecl *)(const char *))FN_TYPE_BY_NAME)(fixed[c][i]);
            if (t <= 0) continue;
            item *it = &m.items[c][m.count[c]++];
            it->type = t;
            it->name = ((const wchar_t *(__cdecl *)(int))FN_THING_NAME)(t);
        }
    }
    /* the carried ones (other consumables stay in the inventory) */
    for (int idx = 0; idx < 84; idx++) {
        char *cell = (char *)NOX_INV_GRID + idx * 148;
        void *dr = *(void **)cell;
        int n = *(uint8_t *)(cell + 140);
        int c = food_cat(dr);
        if (!dr || !n || c < 0) continue;
        int type = *(int32_t *)((char *)dr + 108);
        for (int i = 0; i < m.count[c]; i++) {
            item *it = &m.items[c][i];
            if (it->type != type) continue;
            if (!it->dr) it->dr = dr;
            for (int j = 0; j < n && it->n < 64; j++) it->codes[it->n++] = *(int32_t *)(cell + 4 + 4 * j);
        }
    }
    /* a picture for the ones not carried */
    for (int c = 0; c < CAT_N; c++)
        for (int i = 0; i < m.count[c]; i++)
            if (!m.items[c][i].dr) m.items[c][i].icon = ((void *(__cdecl *)(int))FN_NEW_DRAWABLE)(m.items[c][i].type);
    layout();
    m.sel = -1;
    m.nq = m.pend_heal = m.pend_mana = m.pend_cure = m.pend_confuse = 0;
}

/* ---- what an item would do now ---- */
typedef struct { int heal, mana, cure, confuse, wasted; } effect;

static void hud(int i, int *cur, int *max) {
    int32_t *t = (int32_t *)(NOX_HUD_TUBES + 20 * i);
    *cur = t[1]; *max = t[2];
}

static effect effect_of(item *it) {
    effect e = {0};
    if (!it->dr || left(it) <= 0) { e.wasted = 1; return e; }
    uint32_t fc = *(uint32_t *)((char *)it->dr + 116);
    int poisoned = ONC_I32(NOX_POISONED) && !m.pend_cure;
    int hp, hpmax, mp, mpmax;
    hud(0, &hp, &hpmax); hud(1, &mp, &mpmax);
    hp = hp + m.pend_heal > hpmax ? hpmax : hp + m.pend_heal;
    mp = mp + m.pend_mana > mpmax ? mpmax : mp + m.pend_mana;
    int full = hpmax > 0 && hp >= hpmax, fullmp = mpmax <= 0 || mp >= mpmax;
    /* the value from the item's own data (the server object of the solo game) */
    int value = 0;
    void *obj = ((void *(__cdecl *)(int))FN_OBJ_BY_NET)(it->codes[it->pending]);
    if (obj && *(void **)((char *)obj + 736)) value = **(int32_t **)((char *)obj + 736);
    int cl = class_();
    if (fc & (FOOD_HEALTH | FOOD_MANA | FOOD_CURE)) {
        /* potions: health x3, mana x3 for the casters, x1 for the warrior (the game's class multipliers) */
        if ((fc & FOOD_HEALTH) && !full) e.heal = value * 3;
        if ((fc & FOOD_MANA) && !fullmp) e.mana = value * (cl == 0 ? 1 : 3);
        e.cure = (fc & FOOD_CURE) && poisoned;
        e.wasted = !e.heal && !e.mana && !e.cure;
    } else if (fc & FOOD_MUSHROOM) {
        e.cure = poisoned;
        e.confuse = 10;
    } else {
        if (fc & FOOD_JUG) e.confuse = 5;
        if (!full) e.heal = value;
        e.wasted = full && !e.confuse;
    }
    return e;
}

static void effect_text(effect e, wchar_t *out, int n) {
    out[0] = 0;
    if (e.wasted) { wcsncpy(out, L"no effect right now", n); return; }
    wchar_t b[48];
#define ADD(...) do { swprintf(b, 48, __VA_ARGS__); if (out[0]) wcsncat(out, L", ", n - wcslen(out) - 1); wcsncat(out, b, n - wcslen(out) - 1); } while (0)
    if (e.heal > 0) ADD(L"+%d health", e.heal);
    if (e.mana > 0) ADD(L"+%d mana", e.mana);
    if (e.cure) ADD(L"cures poison");
    if (e.confuse) ADD(L"confused for %d s", e.confuse);
#undef ADD
}

/* ---- open / use / close ---- */
static item *selected(void) {
    if (m.sel < 0 || m.sel >= m.nslots) return NULL;
    slot *s = &m.slots[m.sel];
    return s->index >= 0 ? &m.items[s->cat][s->index] : NULL;
}

int onc_radial_active(void) {
    if (m.open && (!ONC_I32(NOX_CLIENT_CONNECTED) || !*(void **)NOX_PLAYER_DR)) onc_radial_close(1);
    return m.open;
}

int onc_radial_open(void) {
    void *pl = *(void **)NOX_PLAYER_DR;
    if (!g_on || m.open || !ONC_I32(NOX_CLIENT_CONNECTED) || !pl || (*(uint32_t *)((char *)pl + 120) & 0x8000)) return 0;
    if (ONC_I32(NOX_GAME_FLAGS) & NOX_FLAG_PAUSE) return 0;
    collect();
    m.open = 1;
    ((void (__cdecl *)(int))FN_PAUSE)(1);
    m.paused = (ONC_I32(NOX_GAME_FLAGS) & NOX_FLAG_PAUSE) != 0;
    sound(SND_OPEN, 100);
    onc_log("radial: open");
    return 1;
}

void onc_radial_close(int cancel) {
    if (!m.open) return;
    m.open = 0;
    if (m.paused) { ((void (__cdecl *)(int))FN_PAUSE)(0); m.paused = 0; }
    if (!cancel)
        for (int i = 0; i < m.nq; i++) {
            /* the stack's drawable is lent the queued item's net code for the use request only (0x4657C0
               sends it at once) and gets its own back. Left with the used item's code, the game would delete this
               drawable when that item is used up, while the inventory grid still points at it: the next radial
               menu would draw freed memory (0xAC pool fill) and crash */
            void *dr = m.queue[i].dr;
            int32_t own = *(int32_t *)((char *)dr + 128);
            *(int32_t *)((char *)dr + 128) = m.queue[i].code;
            ((void (__cdecl *)(void *))FN_USE)(dr);
            *(int32_t *)((char *)dr + 128) = own;
        }
    if (cancel || !m.nq) sound(SND_CLOSE, 100);
    free_icons();
    onc_log("radial: close, %d used", cancel ? 0 : m.nq);
    m.nq = 0;
}

void onc_radial_a(void) {
    item *it = selected();
    if (!it || left(it) <= 0 || m.nq >= MAXQ) { sound(SND_NO, 100); return; }
    effect e = effect_of(it);
    if (e.wasted) { sound(SND_NO, 100); return; }
    m.queue[m.nq].dr = it->dr;
    m.queue[m.nq].code = it->codes[it->pending];
    m.nq++;
    it->pending++;
    m.pend_heal += e.heal; m.pend_mana += e.mana;
    m.pend_cure |= e.cure;
    if (e.confuse > m.pend_confuse) m.pend_confuse = e.confuse;
    sound(SND_CLICK, 100);
}

void onc_radial_stick(float x, float y, int ok) {
    if (!ok || !m.open) return; /* the last choice stays while the stick is centred */
    int i = slot_at(atan2f(y, x) * 57.29578f);
    if (i >= 0 && m.slots[i].index < 0) i = -1;
    if (i != m.sel) { m.sel = i; if (i >= 0) sound(SND_SELECT, 60); }
}

/* ---- drawing (game functions only) ---- */
/* the viewport the inventory draws its items with, with the current screen size (the inventory sets it when it
   is created, so it can be stale after a resolution change) */
static void *ui_viewport(void) {
    static int32_t vp[16];
    memcpy(vp, (void *)NOX_UI_VIEWPORT, sizeof vp);
    int sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    vp[0] = vp[1] = 0; vp[2] = sw; vp[3] = sh; vp[4] = vp[5] = 0; vp[8] = sw; vp[9] = sh;
    return vp;
}

static void darken(int x, int y, int w, int h, int times) {
    for (int i = 0; i < times; i++) ((void (__cdecl *)(int, int, int, int))FN_DARKEN)(x, y, w, h);
}

static int throb_on(void) { return (GetTickCount() / 375) % 2; }

static void text_box(const wchar_t *s, int x, int y, int color, int centred) {
    int w = 0, h = 0;
    onc_text_size(s, &w, &h);
    if (centred) x -= w / 2;
    darken(x - 4, y - 2, w + 8, h + 4, 2);
    onc_text_color(color);
    onc_text(s, x, y);
}

static void preview(effect e) {
    /* health and mana tubes: queued steady, the selected item blinking */
    for (int t = 0; t < 2; t++) {
        int32_t *tube = (int32_t *)(NOX_HUD_TUBES + 20 * t);
        nox_win *w = (nox_win *)(uintptr_t)tube[0];
        int cur = tube[1], max = tube[2];
        if (!w || !onc_win_visible(w) || max <= 0) continue;
        onc_rect r = onc_win_rect(w);
        int x = r.x0 + 5, y = r.y0;
        int pend = t ? m.pend_mana : m.pend_heal, sel = t ? e.mana : e.heal;
#define LVL(v) (125 * ((v) < 0 ? 0 : (v) > max ? max : (v)) / max)
        int filled = LVL(cur), queued = LVL(cur + pend), after = LVL(cur + pend + sel);
#undef LVL
        int col = t ? onc_rgb(60, 100, 255) : onc_rgb(255, 40, 40);
        onc_draw_color(col);
        if (queued > filled) onc_draw_rect(x, y + 125 - queued, 15, queued - filled);
        if (after > queued && throb_on()) onc_draw_rect(x, y + 125 - after, 15, after - queued);
        if (!t && ONC_I32(NOX_POISONED) && filled > 0 && (m.pend_cure || (e.cure && throb_on()))) {
            onc_draw_color(onc_rgb(240, 20, 20)); /* poisoned (green) tube back to red */
            onc_draw_rect(x, y + 125 - filled, 15, filled);
        }
    }
}

void onc_radial_draw(void) {
    if (!m.open) return;
    int sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    int cx = sw / 2, cy = sh / 2;
    void *pl = *(void **)NOX_PLAYER_DR;
    extern int onc_world_to_screen(int wx, int wy, int *sx, int *sy);
    if (pl) {
        int sx, sy;
        if (onc_world_to_screen(*(int32_t *)((char *)pl + 12), *(int32_t *)((char *)pl + 16), &sx, &sy)) { cx = sx; cy = sy - 24; }
    }
    const float icon = 50, gap = 8, min_inner = 52;
    float r_icon = min_inner + icon / 2 + gap;
    for (int c = 0; c < CAT_N; c++) {
        if (!m.count[c]) continue;
        float span = fabsf(k_sector[c][1] - k_sector[c][0]) * 3.14159265f / 180;
        float need = (icon + gap) * m.count[c] / span;
        if (need > r_icon) r_icon = need;
    }
    float ri = r_icon - icon / 2 - gap, ro = r_icon + icon / 2 + gap;
    if (cx < (int)ro + 4) cx = (int)ro + 4;
    if (cx > sw - (int)ro - 4) cx = sw - (int)ro - 4;
    if (cy < (int)ro + 4) cy = (int)ro + 4;
    if (cy > sh - (int)ro - 4) cy = sh - (int)ro - 4;
    for (int i = 0; i < m.nslots; i++) {
        slot *s = &m.slots[i];
        s->dim = s->index < 0 || left(&m.items[s->cat][s->index]) <= 0;
    }
    onc_draw_begin();
    /* the ring: darkened, the selected slice light, empty slices darker */
    int R = (int)ro;
    for (int dy = -R; dy <= R; dy++) {
        int y = cy + dy, run0 = 0, runk = -1;
        for (int dx = -R; dx <= R + 1; dx++) {
            int k = -1; /* darkening for this pixel, -1: outside */
            if (dx <= R) {
                float d = sqrtf((dx + .5f) * (dx + .5f) + (dy + .5f) * (dy + .5f));
                if (d >= ri && d <= ro) {
                    int si = slot_at(atan2f(dy + .5f, dx + .5f) * 57.29578f);
                    k = si == m.sel ? 0 : (si >= 0 && m.slots[si].dim) ? 3 : 2;
                }
            }
            if (k != runk) {
                if (runk > 0) darken(cx + run0, y, dx - run0, 1, runk);
                run0 = dx; runk = k;
            }
        }
    }
    /* edges and slice borders, category colours on the outer edge */
    onc_draw_color(onc_rgb(150, 130, 90));
    ((void (__cdecl *)(int, int, int))FN_CIRCLE)(cx, cy, (int)ri);
    ((void (__cdecl *)(int, int, int))FN_CIRCLE)(cx, cy, (int)ro);
    for (int i = 0; i < m.nslots; i++) {
        float a = m.slots[i].a0 * 3.14159265f / 180;
        for (float d = ri; d <= ro; d += 1) onc_draw_rect(cx + (int)lroundf(cosf(a) * d), cy + (int)lroundf(sinf(a) * d), 1, 1);
    }
    static const int tint[CAT_N][3] = {{220, 60, 60}, {80, 200, 80}, {180, 80, 220}};
    for (int c = 0; c < CAT_N; c++) {
        onc_draw_color(onc_rgb(tint[c][0], tint[c][1], tint[c][2]));
        float s = k_sector[c][0], e = k_sector[c][1], st = s < e ? 0.5f : -0.5f;
        for (float a = s; st > 0 ? a <= e : a >= e; a += st) {
            float ar = a * 3.14159265f / 180;
            onc_draw_rect(cx + (int)lroundf(cosf(ar) * (ro - 2)), cy + (int)lroundf(sinf(ar) * (ro - 2)), 2, 2);
        }
    }
    if (m.sel >= 0) { /* the selected slice's outline */
        slot *s = &m.slots[m.sel];
        onc_draw_color(ONC_I32(NOX_COLOR_YELLOW));
        for (int b = 0; b < 2; b++) {
            float a = (b ? s->a1 : s->a0) * 3.14159265f / 180;
            for (float d = ri; d <= ro; d += 1) onc_draw_rect(cx + (int)lroundf(cosf(a) * d), cy + (int)lroundf(sinf(a) * d), 2, 2);
        }
    }
    /* the items with their counts */
    int fh = 0;
    onc_text_size(L"0", NULL, &fh);
    for (int i = 0; i < m.nslots; i++) {
        slot *s = &m.slots[i];
        if (s->index < 0) continue;
        item *it = &m.items[s->cat][s->index];
        float a = s->mid * 3.14159265f / 180;
        int px = cx + (int)lroundf(cosf(a) * r_icon), py = cy + (int)lroundf(sinf(a) * r_icon);
        void *dd = it->dr ? it->dr : it->icon;
        if (dd) {
            int32_t *pos = (int32_t *)((char *)dd + 12), ox = pos[0], oy = pos[1];
            pos[0] = px; pos[1] = py;
            void (__cdecl *drawf)(void *, void *) = *(void (__cdecl **)(void *, void *))((char *)dd + 0x12C);
            if (drawf) drawf(ui_viewport(), dd);
            pos[0] = ox; pos[1] = oy;
        }
        if (left(it) <= 0) darken(px - 22, py - 22, 44, 44, 2); /* none carried (or all queued): greyed */
        int n = left(it);
        if (n != 1 || it->pending > 0 || !it->n) {
            wchar_t b[8];
            swprintf(b, 8, L"%d", n);
            int tw = 0;
            onc_text_size(b, &tw, NULL);
            onc_text_color(n ? onc_rgb(255, 255, 255) : onc_rgb(255, 140, 0));
            onc_text(b, px + 25 - 4 - tw, py + 25 - fh - 2);
        }
    }
    /* category names outside the ring */
    static const float mids[CAT_N] = {180, 315, 45};
    for (int c = 0; c < CAT_N; c++) {
        wchar_t b[40];
        swprintf(b, 40, m.count[c] ? L"%ls" : L"%ls (none)", k_cat_names[c]);
        int tw = 0;
        onc_text_size(b, &tw, NULL);
        float a = mids[c] * 3.14159265f / 180;
        int px = cx + (int)(cosf(a) * (ro + 8)), py = cy + (int)(sinf(a) * (ro + 8)) - fh / 2;
        if (cosf(a) < -0.1f) px -= tw;
        else if (fabsf(cosf(a)) <= 0.1f) px -= tw / 2;
        text_box(b, px, py, onc_rgb(170, 170, 170), 0);
    }
    /* the selected item, its effect, the controls */
    wchar_t name[96] = L"", eff[96] = L"", hint[96];
    int wasted = 0;
    effect e = {0};
    item *it = selected();
    if (it) {
        swprintf(name, 96, L"%ls (%d)", it->name ? it->name : L"?", left(it));
        if (left(it) > 0) { e = effect_of(it); effect_text(e, eff, 96); wasted = e.wasted; if (wasted) memset(&e, 0, sizeof e); }
        else { wcscpy(eff, it->n ? L"none left" : L"none carried"); wasted = 1; }
    }
    if (m.nq) swprintf(hint, 96, L"%d queued - B: close and use them", m.nq);
    else wcscpy(hint, L"Left stick: choose   A: use (repeat for more)   B: close");
    preview(e);
    int y = cy + (int)ro + 10;
    if (y + 3 * (fh + 6) > sh) y = cy - (int)ro - 3 * (fh + 6) - 8;
    const wchar_t *lines[3] = {name, eff, hint};
    int cols[3] = {ONC_I32(NOX_COLOR_YELLOW), wasted ? onc_rgb(255, 140, 0) : onc_rgb(80, 220, 80), onc_rgb(170, 170, 170)};
    for (int i = 0; i < 3; i++) if (lines[i][0]) text_box(lines[i], cx, y + i * (fh + 6), cols[i], 1);
    onc_draw_end();
}
