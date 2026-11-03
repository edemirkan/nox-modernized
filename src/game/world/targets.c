#include "targets.h"
void onc_ui_w2g(int *x, int *y);
#include <math.h>

#define FN_DRAW_SELECTION 0x47F3A0 /* draws the hovered object's selection circle (viewport) */
#define FN_SEL_CIRCLE     0x47F450 /* selectionCircle(viewport, color, drawable) */
#define FN_FOR_EACH       0x47D060 /* forEachDrawable(int rect[4] world x0 y0 x1 y1, cb(dr, arg), arg) */
#define FN_VISIBLE        0x47A1A0 /* the drawable can be seen by the player (int) */
#define FN_MY_CREATURE    0x477770 /* the net code is one of the player's creatures (blue circle) */
#define FN_TALK           0x430120 /* talk(drawable) */
#define FN_TRADE          0x4301C0 /* trade(drawable) */
#define FN_PICKUP         0x489A50 /* pickup(drawable) */
#define NOX_PLAYER_DR     0x96A6A0 /* the local player's drawable */
#define NOX_PLAYER_INFO   0x96B8D0 /* the local player's info (+3680 flags) */
#define NOX_HOVERED       0x6F2D48 /* the drawable under the cursor */
#define NOX_FACING        0x68B160 /* 0..255, 0 = +x, clockwise */
#define NOX_SHOP_ON       0x6F32A4
#define COLOR_WHITE  0x954794
#define COLOR_BLUE   0x9737E8
#define COLOR_CYAN   0x973190
#define COLOR_RED    0x9737C0
#define COLOR_VIOLET 0x966830

#define PICKUP_RANGE 75   /* where the game shows the pickup cursor */
#define TALK_RANGE   125  /* where the game shows the talk cursor */
#define TALK_COS     0.57f /* 55 degrees either side of the facing */

static int g_on, c_aim = 1, c_aim_range = 400, c_aim_half = 15;
void onc_lockon_cone(int *range, int *half_deg) { *range = c_aim_range; *half_deg = c_aim_half; } /* aimcone.c */
static void *g_talk, *g_pick, *g_aim;
static int g_talk_shop;
static int32_t g_vp[8]; /* last viewport: +0 screen x, +4 screen y, +0x10 world x, +0x14 world y */
static int g_vp_ok;

#define I32(p, o) (*(int32_t *)((char *)(p) + (o)))
#define U32(p, o) (*(uint32_t *)((char *)(p) + (o)))

typedef struct {
    void *pl;
    int px, py;
    float fx, fy;
    float best_talk, best_pick, best_aim;
    void *prev; int prev_ok; /* the enemy locked on before: kept while attacking if still roughly in the cone */
} scan;

static int g_aiming; /* attacking / casting at the locked-on enemy (pad.c) */
void onc_targets_set_aiming(int on) { g_aiming = on; }

/* the player is casting a spell (solo / host game: the server player's update data) */
int onc_player_casting(void) {
    void *pl = *(void **)0x96A6A0;
    if (!pl) return 0;
    char *obj = ((char *(__cdecl *)(int))0x4EBC10)(*(int32_t *)((char *)pl + 128));
    char *ud = obj ? *(char **)(obj + 748) : NULL;
    return ud && *(uint32_t *)(ud + 216) != 0;
}

/* the player is in the middle of an attack (the player's state, update data +88, is 1): a bow shoots, a sword
   hits at a later frame of it, along the facing at that moment */
int onc_player_attacking(void) {
    void *pl = *(void **)0x96A6A0;
    if (!pl) return 0;
    char *obj = ((char *(__cdecl *)(int))0x4EBC10)(*(int32_t *)((char *)pl + 128));
    char *ud = obj ? *(char **)(obj + 748) : NULL;
    return ud && *(uint8_t *)(ud + 88) == 1;
}

static int visible(void *dr) { return ((int (__cdecl *)(void *))FN_VISIBLE)(dr) != 0; }

static void __cdecl scan_cb(void *dr, scan *s) {
    if (dr == s->pl || (U32(dr, 120) & 0x8000)) return; /* the player, dead / hidden */
    uint32_t cls = U32(dr, 112), sub = U32(dr, 116);
    float dx = (float)(I32(dr, 12) - s->px), dy = (float)(I32(dr, 16) - s->py);
    float d2 = dx * dx + dy * dy, d = sqrtf(d2);
    float cosf_ = d > 0.5f ? (dx * s->fx + dy * s->fy) / d : 1.0f;
    if (cls & 0x80000000) { /* an item that can be picked up */
        if (d <= PICKUP_RANGE && d2 < s->best_pick && visible(dr)) { s->best_pick = d2; g_pick = dr; }
        return;
    }
    if (!(cls & 6)) return; /* monsters and players only from here */
    if ((cls & 2) && ((sub & 0x4000) || I32(dr, 276) == 10)) return; /* dying / dead monster */
    int talk = (cls & 2) && (U32(dr, 280) & 0x10), shop = (cls & 2) && (sub & 8);
    if (talk || shop) {
        if (d <= TALK_RANGE && cosf_ >= TALK_COS && d2 < s->best_talk && visible(dr)) {
            s->best_talk = d2; g_talk = dr; g_talk_shop = !talk;
        }
        return;
    }
    if (!c_aim || ((int (__cdecl *)(int))FN_MY_CREATURE)(I32(dr, 128))) return; /* the player's creatures */
    float ang = d > 16 ? acosf(fmaxf(-1, fminf(1, cosf_))) * 57.29578f : 0;
    float body = d > 16 ? atanf(14.0f / d) * 57.29578f : 0;
    if (dr == s->prev && d <= c_aim_range * 1.15f && ang <= c_aim_half + body + 10 && visible(dr)) s->prev_ok = 1;
    if (d > c_aim_range) return;
    if (ang > c_aim_half + body) return; /* outside the cone (wider for close enemies: their bodies touch it) */
    if (d2 < s->best_aim && visible(dr)) { s->best_aim = d2; g_aim = dr; }
}

static void find_targets(void) {
    void *prev = g_aim;
    g_talk = g_pick = g_aim = NULL;
    void *pl = *(void **)NOX_PLAYER_DR;
    if (!g_on || !pl || !ONC_I32(NOX_CLIENT_CONNECTED)) return;
    scan s = {pl, I32(pl, 12), I32(pl, 16), 0, 0, 1e30f, 1e30f, 1e30f, prev, 0};
    float a = ONC_I32(NOX_FACING) * (6.2831853f / 256.0f);
    s.fx = cosf(a); s.fy = sinf(a);
    int r = c_aim_range + 64;
    int rect[4] = {s.px - r, s.py - r, s.px + r, s.py + r};
    ((void (__cdecl *)(int *, void *, void *))FN_FOR_EACH)(rect, (void *)scan_cb, &s);
    if (g_aiming && prev && s.prev_ok) g_aim = prev; /* sticky while attacking */
    /* no talking while the game would not show the talk cursor */
    void *info = *(void **)NOX_PLAYER_INFO;
    if ((info && (U32(info, 3680) & 0x200)) || ONC_I32(NOX_SHOP_ON)) g_talk = NULL;
    if (g_talk) g_pick = NULL; /* R3 talks first */
}

/* ---- the game's selection circles ---- */
static void (__cdecl *g_draw_selection)(int32_t *vp);

static void circle(int32_t *vp, void *dr, const int *colors, int n) {
    if (!dr || dr == *(void **)NOX_HOVERED) return; /* the game draws that one itself */
    int c = *(int32_t *)(uintptr_t)colors[(GetTickCount() / 60) % n];
    ((void (__cdecl *)(int32_t *, int, void *))FN_SEL_CIRCLE)(vp, c, dr);
}

static void __cdecl draw_selection(int32_t *vp) {
    g_draw_selection(vp);
    if (!vp) return;
    for (int i = 0; i < 8; i++) g_vp[i] = vp[i];
    g_vp_ok = 1;
    find_targets(); /* every frame, while the world is drawn: the object lists are consistent here */
    static const int pick[] = {COLOR_WHITE, COLOR_BLUE, COLOR_CYAN}, foe[] = {COLOR_RED, COLOR_VIOLET};
    circle(vp, g_talk ? g_talk : g_pick, pick, 3);
    circle(vp, g_aim, foe, 2);
}

void onc_targets_init(void) {
    c_aim = onc_cfg_int("pad", "lock_on", 1);
    c_aim_range = onc_cfg_int("pad", "lock_on_range", 400);
    c_aim_half = onc_cfg_int("pad", "lock_on_cone_degrees", 30) / 2;
    static const uint8_t start[] = {0xA1, 0x48, 0x2D, 0x6F, 0x00}; /* mov eax, [hovered] */
    g_draw_selection = (void (__cdecl *)(int32_t *))onc_hook_detour("selection circle", FN_DRAW_SELECTION,
                                                                    draw_selection, start, 5);
}

void onc_targets_enable(int on) {
    g_on = on && g_draw_selection;
    if (!g_on) g_talk = g_pick = g_aim = NULL;
}

/* a target found while drawing may have been deleted since: it must still be in the object list */
typedef struct { void *want; int found; } find_one;
static void __cdecl find_cb(void *dr, find_one *f) { if (dr == f->want) f->found = 1; }
static int still_there(void *dr) {
    void *pl = *(void **)NOX_PLAYER_DR;
    if (!dr || !pl) return 0;
    int r = c_aim_range + 128, px = I32(pl, 12), py = I32(pl, 16);
    int rect[4] = {px - r, py - r, px + r, py + r};
    find_one f = {dr, 0};
    ((void (__cdecl *)(int *, void *, void *))FN_FOR_EACH)(rect, (void *)find_cb, &f);
    return f.found;
}

int onc_interact(void) {
    if (!g_on) return 0;
    if (g_talk && still_there(g_talk)) {
        onc_log("R3: %s", g_talk_shop ? "trade" : "talk");
        ((void (__cdecl *)(void *))(g_talk_shop ? FN_TRADE : FN_TALK))(g_talk);
        return 1;
    }
    if (g_pick && still_there(g_pick)) {
        onc_log("R3: pick up");
        ((void (__cdecl *)(void *))FN_PICKUP)(g_pick);
        return 1;
    }
    return 0;
}

int onc_aim_screen(int *x, int *y) {
    if (!g_on || !g_aim || !g_vp_ok || !still_there(g_aim)) return 0;
    *x = I32(g_aim, 12) + g_vp[0] - g_vp[4];
    *y = I32(g_aim, 16) + g_vp[1] - g_vp[5];
    onc_ui_w2g(x, y);
    return 1;
}

int onc_target_hint(int *x, int *y, const wchar_t **text) {
    void *t = g_talk ? g_talk : g_pick;
    if (!g_on || !t || !g_vp_ok) return 0;
    *x = I32(t, 12) + g_vp[0] - g_vp[4];
    *y = I32(t, 16) + g_vp[1] - g_vp[5] - 40; /* above it */
    onc_ui_w2g(x, y);
    *text = g_talk ? (g_talk_shop ? L"R3: trade" : L"R3: talk") : L"R3: pick up";
    return 1;
}

/* world -> screen with the last viewport the world was drawn with */
int onc_world_to_screen(int wx, int wy, int *sx, int *sy) {
    if (!g_vp_ok) return 0;
    *sx = wx + g_vp[0] - g_vp[4];
    *sy = wy + g_vp[1] - g_vp[5];
    onc_ui_w2g(sx, sy); /* with ui_scale the world buffer is bigger than the GUI */
    return 1;
}

/* The player faces the locked-on enemy (solo / host game: the server's player object). Arrows, thrown weapons and
   missiles leave along the facing (object +0x7C, 0..255, the direction table 0x5B41F8: cos, sin), not towards the
   cursor; the facing follows the cursor at the end of a game frame. So the pad presses attack / a spell key only
   once the facing matches (pad.c). 1 also when it cannot be known (a client in someone else's game). */
int onc_facing_target(void) {
    void *pl = *(void **)NOX_PLAYER_DR;
    if (!g_aim || !pl) return 1;
    char *obj = ((char *(__cdecl *)(int))0x4EBC10)(I32(pl, 128));
    if (!obj) return 1;
    float dx = (float)I32(g_aim, 12) - *(float *)(obj + 0x38), dy = (float)I32(g_aim, 16) - *(float *)(obj + 0x3C);
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 8) return 1;
    int f = *(int16_t *)(obj + 0x7C) & 255;
    const float *v = (const float *)(0x5B41F8 + f * 8);
    float dot = (v[0] * dx + v[1] * dy) / d;
    /* within about 2 direction steps, or the body is hit anyway (14 units either side at that distance) */
    float tol = cosf(fmaxf(2.9f, atanf(14.0f / d) * 57.29578f) / 57.29578f);
    return dot >= tol;
}

/* ---- beneficial spells on allies ----
   A spell slot carries a "cast on oneself" mark (spell bar 0x5A54F8 -> +0xCC, slot +4), set by the game for every
   spell with the flags 0x600 (defensive / on oneself, 0x45D730) and never cleared, so Lesser Heal, Haste, Shield...
   always went to the caster, even with the cursor on a charmed beast (also in the original game). The slot cast
   0x45D5A0 sends it with the spell (0x45D670(slot, 1, mark), call at 0x45D5E0; message 0x79), the server then takes
   the caster instead of the object under the cursor. Here: if the cursor is really being aimed (mouse / keyboard
   play, or the pad's right stick cursor) and is on one of the player's creatures or another player, a spell that
   can be aimed (targeted 0x4 + defensive 0x200, not "not on self" 0x200000) is sent without the mark, so it goes to
   that ally. Otherwise the mark stays: on oneself, as before. */
int onc_pad_cursor_aimed(void);
static int (__cdecl *o_cast_send)(int32_t *, int, int);
static int ally_hovered(void) {
    void *dr = *(void **)NOX_HOVERED, *pl = *(void **)NOX_PLAYER_DR;
    if (!dr || dr == pl || (U32(dr, 120) & 0x8000)) return 0;
    uint32_t cls = U32(dr, 112), sub = U32(dr, 116);
    if (cls & 4) return 1;                                           /* another player */
    if (!(cls & 2) || (sub & 0x4000) || I32(dr, 276) == 10) return 0; /* not a living monster */
    return ((int (__cdecl *)(int))FN_MY_CREATURE)(I32(dr, 128)) != 0; /* the player's creature (blue circle) */
}
static int __cdecl cast_send(int32_t *slot, int n, int mark) {
    if (mark && slot && onc_pad_cursor_aimed()) {
        int sp = slot[0];
        int (__cdecl *has)(int, int) = (int (__cdecl *)(int, int))0x424610;
        if (sp > 0 && has(sp, 0x4) && has(sp, 0x200) && !has(sp, 0x200000) && ally_hovered()) {
            onc_log("cast: spell %d on the ally under the cursor", sp);
            mark = 0;
        }
    }
    return o_cast_send(slot, n, mark);
}
void onc_ally_cast_init(void) {
    if (!onc_cfg_int("game", "ally_spells", 1)) return;
    o_cast_send = (int (__cdecl *)(int32_t *, int, int))0x45D670;
    if (!onc_hook_call("ally spells: slot cast", 0x45D5E0, cast_send, 0x45D670)) o_cast_send = NULL;
}
