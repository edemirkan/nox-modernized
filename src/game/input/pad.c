/* Gamepad (XInput) for the original game, without touching its code: the pad becomes mouse and keyboard
   events that the DirectInput proxy hands to the game together with the real ones.

   Left stick: the game moves the player while the right mouse button is held, towards the cursor; it walks
   when the cursor is close to the player and runs when it is far. So the stick holds the right button and
   puts the cursor on a ring around the player (the screen centre - the camera follows the player): a small
   ring to walk, a larger one to run. The cursor position is read from the game (NOX_MOUSE_X/Y), so the
   motion we send lands exactly on the ring.
   Right stick: moves the cursor freely (menus, aiming by hand). Every button alone or with L1 does the action
   it is mapped to (binds.c: attack = the left click, aim, talk / pick up, the consumables wheel, inventory, the
   game's keys...). Menus, the inventory, shops, the book and the wheel keep A / B / Start / Select / D-pad. */
#include "onc.h"
#include <math.h>
#include <stdlib.h>
#include <xinput.h>
#include "../ui/menu_nav.h"
#include "../ui/inventory.h"
#include "../ui/shop.h"
#include "../world/targets.h"
#include "binds.h"
#include "binds_table.h"
#include "../ui/radial.h"
#include "../ui/book.h"
#include "../ui/summon.h"

typedef DWORD (WINAPI *xinput_get_fn)(DWORD, XINPUT_STATE *);
static xinput_get_fn g_xget;
static int g_pad = -1;          /* connected pad index */
static DWORD g_last_poll, g_last_scan;
static WORD g_buttons;          /* last button state */
static BYTE g_lt, g_rt;
static int g_moving;            /* the left stick holds the right button */
static int g_rclick;            /* right mouse held by us */
static int g_lclick;            /* left mouse held by us (A) */
static int g_mod_keys[16];
static int g_enabled, g_xinit;
static void xinput_load(void);      /* key sent for each button on press (so its release matches) */

/* settings (nox-modernized.ini, [pad]) */
static int c_dead = 25, c_walk = 60;         /* percent of stick travel */
static int c_walk_r = 60, c_run_r = 30;      /* walk ring in pixels at 768 lines, run ring in percent of screen height */
static int c_speed = 14;                     /* right stick cursor speed, pixels per poll at 768 lines */
static int c_attack_repeat = 450;             /* held attack interval, ms */
static int c_hide_cursor = 1;                /* hide the cursor while the left stick plays */
static int c_cur_start = 50;                 /* right stick cursor mode starts past this (% of its travel) */
static int c_zoom_hold = 250;                /* R3 + right stick zoom: R3 held this long (ms) first */
static int c_zoom_dead = 50;                 /* ... and the right stick pushed past this (% of its travel) */

/* ---- event queues ---- */
#define QN 64
static DIDEVICEOBJECTDATA g_mq[QN], g_kq[QN];
static int g_mh, g_mt, g_kh, g_kt;
static int g_pend_x, g_pend_y; /* motion queued but not read by the game yet */

static void push(DIDEVICEOBJECTDATA *q, int *t, int h, DWORD ofs, DWORD data) {
    int n = (*t + 1) % QN;
    if (n == h) return; /* full: drop */
    q[*t].dwOfs = ofs;
    q[*t].dwData = data;
    q[*t].dwTimeStamp = GetTickCount();
    q[*t].dwSequence = 0;
    *t = n;
}
static void mouse_ev(DWORD ofs, DWORD data) {
    int n = (g_mt + 1) % QN;
    if (n == g_mh) return;
    if (ofs == DIMOFS_X) g_pend_x += (int)data;
    if (ofs == DIMOFS_Y) g_pend_y += (int)data;
    push(g_mq, &g_mt, g_mh, ofs, data);
}
static void key_ev(BYTE dik, int down) { push(g_kq, &g_kt, g_kh, dik, down ? 0x80 : 0); }

/* a key tap: pressed now, released a few frames later (the game reads all queued events in one frame, and a
   press with its release in the same frame does not register as a press) */
static BYTE g_tap_key;
static DWORD g_tap_release;
static void key_tap(BYTE dik) {
    if (g_tap_key) key_ev(g_tap_key, 0);
    key_ev(dik, 1);
    g_tap_key = dik;
    g_tap_release = GetTickCount() + 80;
}
static void key_tap_tick(DWORD now) {
    if (g_tap_key && (int)(now - g_tap_release) >= 0) { key_ev(g_tap_key, 0); g_tap_key = 0; }
}

/* ---- hints: the inventory combo held shows the controls list, L1 held what the L1 combinations do ---- */
static DWORD g_inv_down, g_lb_down; /* when the inventory combo / L1 went down in the game (0: up) */
static int g_inv_help;              /* the list was shown: releasing the combo does not open the inventory */
static int g_inv_combo = -1;        /* the combo held */
int onc_pad_help_shown(void) { return g_inv_down && GetTickCount() - g_inv_down >= 500 && onc_pad_in_use(); }
int onc_pad_help_combo(void) { return g_inv_combo; }
int onc_pad_l1_held(void) { return g_lb_down && GetTickCount() - g_lb_down >= 400 && onc_pad_in_use(); }

/* ---- lock-on: A and the spell buttons aim at the locked-on enemy (world/targets.c) ---- */
static int g_manual;          /* the right stick aims by hand (until the left stick is used again) */
static volatile int g_hide_cursor; /* the cursor is not drawn: the left stick plays (cursor only aims) */
int onc_pad_cursor_hidden(void) { return g_hide_cursor && onc_pad_in_use(); }
static int c_r3_click = 1;    /* R3 = left click in right stick cursor mode */
/* right stick cursor mode with R3 as a click: the pad aims by hand (until the left stick moves again) */
int onc_pad_cursor_mode(void) { return c_r3_click && g_manual; }
int onc_pad_r3_click(void) { return c_r3_click; }
/* the cursor is really being aimed: mouse / keyboard play, or the pad's right stick cursor (targets.c, ally spells) */
/* the aim cone (aimcone.c): 1 while the left stick plays in a game (pad used, no right stick cursor); r1 = the aim
   combo is held; the direction of the left stick (last push while the aim combo is held), 0 / 0 = use the facing */
static float g_aim_dx, g_aim_dy;
static int g_aim_btn; /* (declared again below with the other button masks) */
int onc_pad_aim_state(int *r1, float *fx, float *fy) {
    *r1 = g_aim_btn != 0;
    *fx = *r1 ? g_aim_dx : 0; *fy = *r1 ? g_aim_dy : 0;
    return onc_pad_in_use() && !g_manual;
}
int onc_pad_cursor_aimed(void) { return !onc_pad_in_use() || g_manual; }
static int g_aim_a;           /* A is held with the cursor on the enemy */
static DWORD g_aim_until;     /* after a spell button: the cursor stays on the enemy until then (casting) */
static DWORD g_cast_cap;      /* ... at most until then, while the game says the player is casting */
static BYTE g_pend_key;       /* a spell key pressed one poll after the cursor was put on the enemy */
static int g_pend_b, g_pend_released;
static DWORD g_pend_at;
static int att_wait;          /* an attack press waits for the player to face the enemy */
static DWORD att_until, att_hold, att_cap, att_start;
static DWORD att_repeat_at;
static int att_repeat_wait;
static int att_seen;          /* the attack the pad started has begun */
static int g_held[16];        /* the action each button started when pressed (it ends with the same one) */
static int g_attack_btn, g_aim_btn; /* buttons held doing attack / aim (bits by B_*) */

/* ---- button -> key table (default Nox keys) ---- */
enum { B_DU, B_DD, B_DL, B_DR, B_START, B_BACK, B_LS, B_RS, B_LB, B_RB, B_A, B_B, B_X, B_Y, B_LT, B_RT, B_N };
static const WORD xbits[B_N] = {
    XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
    XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
    XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER, XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B,
    XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y, 0, 0,
};
/* DirectInput scan codes */
#define K_ESC 0x01
#define K_TAB 0x0F
#define K_W 0x11
#define K_E 0x12
#define K_I 0x17
#define K_A 0x1E
#define K_S 0x1F
#define K_D 0x20
#define K_F 0x21
#define K_G 0x22
#define K_Z 0x2C
#define K_X 0x2D
#define K_C 0x2E
#define K_V 0x2F
#define K_B 0x30
#define K_SPACE 0x39
#define K_F2 0x3C

/* the combo of a button, plain or with LB held (-1: LB itself, the modifier) */
static int combo_for(int b, int lb) {
    static const int8_t k[B_N] = {C_DU, C_DD, C_DL, C_DR, C_START, C_SEL, C_L3, C_R3, -1, C_R1, C_A, C_B, C_X, C_Y, C_LT, C_RT};
    int c = k[b];
    return c < 0 ? -1 : lb ? S_L1(c) : c;
}

static void cursor_to(int tx, int ty);
/* the conjurer's creature order menu, on whichever combo it is mapped to */
static void creatures_open(void) {
    int sx, sy;
    extern int onc_summon_pending_xy(int *x, int *y);
    if (onc_summon_open() && onc_summon_pending_xy(&sx, &sy)) cursor_to(sx, sy);
}

static void stick(SHORT sx, SHORT sy, float *dx, float *dy, float *mag) {
    float x = sx / 32767.0f, y = -sy / 32767.0f; /* screen y grows downwards */
    float m = sqrtf(x * x + y * y);
    float dz = c_dead / 100.0f;
    if (m <= dz) { *dx = *dy = *mag = 0; return; }
    *dx = x / m; *dy = y / m;
    *mag = fminf((m - dz) / (1 - dz), 1.0f);
}

/* which was used last: the pad or the mouse / keyboard */
static volatile int g_pad_used;
void onc_real_input(void) { g_pad_used = 0; }
int onc_pad_in_use(void) { return g_pad_used && g_pad >= 0; }

void onc_real_mouse_moved(void) {
    /* nothing yet: the left stick puts the cursor back on the ring as soon as it is used again */
}

/* a navigation step from a direction that is held: at once, then repeated */
static int g_nav_dx, g_nav_dy;
static DWORD g_nav_next;
static void nav_repeat(int dx, int dy, DWORD now, int *ox, int *oy) {
    *ox = *oy = 0;
    if (dx != g_nav_dx || dy != g_nav_dy) {
        g_nav_dx = dx; g_nav_dy = dy;
        if (dx || dy) { *ox = dx; *oy = dy; g_nav_next = now + 320; }
        return;
    }
    if ((dx || dy) && (int)(now - g_nav_next) >= 0) { *ox = dx; *oy = dy; g_nav_next = now + 110; }
}

/* move the cursor to a point (screen pixels), counting motion that is still queued */
static void cursor_to(int tx, int ty) {
    /* only for the pad: with the mouse (or keyboard) last used, nothing moves the cursor by itself (menus opening,
       the vendor's price box, the inventory coming back...) */
    if (!onc_pad_in_use()) return;
    int cx = ONC_I32(NOX_MOUSE_X) + g_pend_x, cy = ONC_I32(NOX_MOUSE_Y) + g_pend_y;
    if (tx != cx) mouse_ev(DIMOFS_X, (DWORD)(tx - cx));
    if (ty != cy) mouse_ev(DIMOFS_Y, (DWORD)(ty - cy));
}

/* a button's game key let go (a pending aimed key that was not sent yet goes as a tap) */
static void key_up(int b) {
    if (!g_mod_keys[b]) return;
    if (g_pend_key && g_pend_b == b) g_pend_released = 1;
    else key_ev((BYTE)g_mod_keys[b], 0);
    g_mod_keys[b] = 0;
}
/* the game keys, the click and the held actions let go (a menu takes the pad, the pad is lost...) */
static void release_all(void) {
    for (int b = 0; b < B_N; b++) {
        key_up(b);
        g_held[b] = 0;
    }
    if (g_lclick) { mouse_ev(DIMOFS_BUTTON0, 0); g_lclick = 0; }
    att_wait = 0;
    att_cap = att_seen = 0;
    att_repeat_wait = 0;
    g_attack_btn = g_aim_btn = 0;
    g_inv_down = 0; g_inv_combo = -1;
}

static void stop_moving(void) {
    if (g_rclick) { mouse_ev(DIMOFS_BUTTON1, 0); g_rclick = 0; }
    g_moving = 0;
}

static void poll(void) {
    DWORD now = GetTickCount();
    if (now - g_last_poll < 8) return; /* the game asks many times per frame */
    g_last_poll = now;
    if (!g_enabled) return;
    if (!g_xinit) xinput_load(); /* not in DllMain (loader lock) */
    if (!g_xget) return;
    XINPUT_STATE st;
    if (g_pad < 0) {
        if (now - g_last_scan < 1000) return;
        g_last_scan = now;
        for (int i = 0; i < 4; i++) {
            if (g_xget(i, &st) == ERROR_SUCCESS) { g_pad = i; onc_log("gamepad %d connected", i); break; }
        }
        if (g_pad < 0) return;
    }
    if (g_xget(g_pad, &st) != ERROR_SUCCESS) {
        onc_log("gamepad %d disconnected", g_pad);
        g_pad = -1;
        onc_targets_enable(0);
        stop_moving();
        if (g_lclick) { mouse_ev(DIMOFS_BUTTON0, 0); g_lclick = 0; }
        return;
    }
    key_tap_tick(now);
    XINPUT_GAMEPAD *p = &st.Gamepad;
    WORD btn = p->wButtons, was = g_buttons;
    {   /* any button, trigger or stick push: the pad is in use */
        int dz = 32767 * c_dead / 100;
        if ((btn & ~was) || (p->bLeftTrigger > 100 && g_lt <= 100) || (p->bRightTrigger > 100 && g_rt <= 100) ||
            abs(p->sThumbLX) > dz || abs(p->sThumbLY) > dz || abs(p->sThumbRX) > dz || abs(p->sThumbRY) > dz)
            g_pad_used = 1;
    }
    /* Select + Start: the settings menu (F7). Both buttons are then left out until both are released. */
    {
        static int combo;
        extern void onc_settings_toggle(void);
        const WORD both = XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK;
        if ((btn & both) == both && (btn & both) != (was & both) && !combo) {
            combo = 1;
            release_all(); g_inv_help = 1; /* the inventory (on Select) does not open */
            onc_settings_toggle();
        }
        if (combo) {
            if (!(btn & both)) combo = 0;
            btn &= ~both; was &= ~both;
        }
    }
#define PRESSED(bit) ((btn & (bit)) && !(was & (bit)))
    int lb = (btn & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    int ingame = ONC_I32(NOX_CLIENT_CONNECTED) != 0;
    float ldx, ldy, lm, rdx, rdy, rm;
    stick(p->sThumbLX, p->sThumbLY, &ldx, &ldy, &lm);
    stick(p->sThumbRX, p->sThumbRY, &rdx, &rdy, &rm);

    /* ---- menus ---- */
    int ddx = ((btn & XINPUT_GAMEPAD_DPAD_RIGHT) != 0) - ((btn & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
    int ddy = ((btn & XINPUT_GAMEPAD_DPAD_DOWN) != 0) - ((btn & XINPUT_GAMEPAD_DPAD_UP) != 0);
    if (!ddx && !ddy && lm > 0.5f) { /* the left stick navigates menus too */
        if (ldx * ldx > ldy * ldy) ddx = ldx > 0 ? 1 : -1; else ddy = ldy > 0 ? 1 : -1;
    }
    onc_menu_input mi = {0};
    mi.ingame = ingame;
    nav_repeat(ddx, ddy, now, &mi.dir_x, &mi.dir_y);
    mi.press_a = PRESSED(XINPUT_GAMEPAD_A);
    mi.press_b = PRESSED(XINPUT_GAMEPAD_B);
    onc_rect sr;
    if (onc_menu_rect(&sr)) {
        int cx = ONC_I32(NOX_MOUSE_X) + g_pend_x, cy = ONC_I32(NOX_MOUSE_Y) + g_pend_y;
        mi.cursor_on_sel = cx >= sr.x0 && cx <= sr.x1 && cy >= sr.y0 && cy <= sr.y1;
    }
    extern int onc_settings_open(void);
    extern void onc_settings_pad(int dx, int dy, int a, int b, int tab);
    if (onc_settings_open()) {
        /* the settings menu has the pad: no game input */
        stop_moving();
        release_all();
        onc_targets_enable(0);
        onc_settings_pad(mi.dir_x, mi.dir_y, mi.press_a, mi.press_b,
                         PRESSED(XINPUT_GAMEPAD_RIGHT_SHOULDER) - PRESSED(XINPUT_GAMEPAD_LEFT_SHOULDER)); /* R1 / L1: tabs */
        g_buttons = btn; g_lt = p->bLeftTrigger; g_rt = p->bRightTrigger;
        return;
    }
    onc_menu_output mo;
    int menu = onc_menu_update(&mi, &mo);
    /* what the pad works besides menus, followed every frame (also while a box covers it, so that the cursor
       goes back onto the selection when the box closes) */
    int shop_buy = onc_shop_buy_active();
    static int inv_was;
    int inv = onc_inv_active();
    /* closed: start from the first item next time; only covered (a box over it): back to the same item */
    if (!inv && inv_was && *(volatile uint8_t *)NOX_INV_STATE != 2) onc_inv_reset();
    if (inv && !inv_was) onc_inv_resume();
    inv_was = inv;
    g_hide_cursor = 0; /* set again below while playing */
    if (menu || shop_buy || inv || !ingame) { onc_targets_enable(0); g_inv_down = g_lb_down = 0; g_inv_combo = -1; }
    if (onc_radial_active() || onc_book_active() || onc_summon_menu()) menu = 0; /* the radial menu / the book have the pad */
    if (menu) {
        /* the pad works the menu: no game keys (Start still opens / closes the menu), no movement */
        stop_moving();
        release_all();
        if (PRESSED(XINPUT_GAMEPAD_START)) key_tap(K_ESC);
        if (mo.put_cursor) cursor_to(mo.sel_x, mo.sel_y);
        if (mo.click) { mouse_ev(DIMOFS_BUTTON0, 0x80); mouse_ev(DIMOFS_BUTTON0, 0); }
        if (mo.esc) key_tap(K_ESC);
        /* the right stick is a free cursor */
        if (rm > 0) {
            float v = rm * rm * c_speed * ONC_I32(NOX_SCREEN_H) / 768.0f;
            int dx = (int)lroundf(rdx * v), dy = (int)lroundf(rdy * v);
            if (dx) mouse_ev(DIMOFS_X, (DWORD)dx);
            if (dy) mouse_ev(DIMOFS_Y, (DWORD)dy);
        }
        g_buttons = btn; g_lt = p->bLeftTrigger; g_rt = p->bRightTrigger;
        return;
    }

    /* ---- the consumables radial menu (L3) ---- */
    if (onc_radial_active()) {
        stop_moving();
        release_all();
        onc_targets_enable(0);
        onc_radial_stick(ldx, ldy, lm > 0.3f);
        if (PRESSED(XINPUT_GAMEPAD_A)) onc_radial_a();
        if (PRESSED(XINPUT_GAMEPAD_B) || PRESSED(XINPUT_GAMEPAD_LEFT_THUMB) || PRESSED(XINPUT_GAMEPAD_START))
            onc_radial_close(0);
        g_buttons = btn; g_lt = p->bLeftTrigger; g_rt = p->bRightTrigger;
        return;
    }

    /* ---- the conjurer's creature order menu ---- */
    onc_summon_tick();
    if (onc_summon_menu()) {
        stop_moving();
        release_all();
        onc_targets_enable(0);
        onc_summon_input si = {mi.dir_y, PRESSED(XINPUT_GAMEPAD_A), PRESSED(XINPUT_GAMEPAD_B) || PRESSED(XINPUT_GAMEPAD_START)};
        onc_summon_output so;
        onc_summon_update(&si, &so);
        if (so.put_cursor) cursor_to(so.x, so.y);
        g_buttons = btn; g_lt = p->bLeftTrigger; g_rt = p->bRightTrigger;
        return;
    }

    /* ---- the book of knowledge ---- */
    if (onc_book_active()) {
        stop_moving();
        release_all();
        onc_targets_enable(0);
        onc_book_input bi = {mi.dir_x, mi.dir_y, PRESSED(XINPUT_GAMEPAD_A), lb, PRESSED(XINPUT_GAMEPAD_B),
                             PRESSED(XINPUT_GAMEPAD_RIGHT_SHOULDER), PRESSED(XINPUT_GAMEPAD_LEFT_SHOULDER),
                             PRESSED(XINPUT_GAMEPAD_BACK)};
        onc_book_output bo;
        onc_book_update(&bi, &bo);
        if (bo.put_cursor) cursor_to(bo.x, bo.y);
        if (rm > 0) {
            float v = rm * rm * c_speed * ONC_I32(NOX_SCREEN_H) / 768.0f;
            int dx = (int)lroundf(rdx * v), dy = (int)lroundf(rdy * v);
            if (dx) mouse_ev(DIMOFS_X, (DWORD)dx);
            if (dy) mouse_ev(DIMOFS_Y, (DWORD)dy);
        }
        g_buttons = btn; g_lt = p->bLeftTrigger; g_rt = p->bRightTrigger;
        return;
    }

    /* ---- a vendor's items ---- */
    if (shop_buy) {
        stop_moving();
        release_all();
        onc_shop_input si = {mi.dir_x, mi.dir_y, mi.press_a, mi.press_b || PRESSED(XINPUT_GAMEPAD_START)};
        onc_shop_output so;
        onc_shop_buy_update(&si, &so);
        if (so.put_cursor) cursor_to(so.x, so.y);
        if (rm > 0) {
            float v = rm * rm * c_speed * ONC_I32(NOX_SCREEN_H) / 768.0f;
            int dx = (int)lroundf(rdx * v), dy = (int)lroundf(rdy * v);
            if (dx) mouse_ev(DIMOFS_X, (DWORD)dx);
            if (dy) mouse_ev(DIMOFS_Y, (DWORD)dy);
        }
        g_buttons = btn; g_lt = p->bLeftTrigger; g_rt = p->bRightTrigger;
        return;
    }

    /* ---- inventory ---- */
    if (inv) {
        stop_moving();
        release_all();
        onc_inv_input ii = {mi.dir_x, mi.dir_y, mi.press_a, PRESSED(XINPUT_GAMEPAD_X),
                            PRESSED(XINPUT_GAMEPAD_RIGHT_SHOULDER) - PRESSED(XINPUT_GAMEPAD_LEFT_SHOULDER),
                            (btn & XINPUT_GAMEPAD_X) != 0};
        onc_inv_output io;
        onc_inv_update(&ii, &io);
        if (io.put_cursor) cursor_to(io.x, io.y);
        if (io.carrying) {
            /* an item carried with X held: the left (or right) stick moves the cursor, nothing else */
            float m = lm > rm ? lm : rm, sx = lm > rm ? ldx : rdx, sy = lm > rm ? ldy : rdy;
            if (m > 0) {
                float v = m * m * c_speed * ONC_I32(NOX_SCREEN_H) / 768.0f;
                int dx = (int)lroundf(sx * v), dy = (int)lroundf(sy * v);
                if (dx) mouse_ev(DIMOFS_X, (DWORD)dx);
                if (dy) mouse_ev(DIMOFS_Y, (DWORD)dy);
            }
            g_buttons = btn; g_lt = p->bLeftTrigger; g_rt = p->bRightTrigger;
            return;
        }
        if (onc_shop_mode()) {
            /* selling / repairing: B / Select / Start go back to the shop's choices */
            if (PRESSED(XINPUT_GAMEPAD_B) || PRESSED(XINPUT_GAMEPAD_BACK) || PRESSED(XINPUT_GAMEPAD_START))
                onc_shop_choices();
        } else {
            /* B / Select close it, Start opens the menu */
            if (PRESSED(XINPUT_GAMEPAD_B) || PRESSED(XINPUT_GAMEPAD_BACK)) onc_inv_close();
            if (PRESSED(XINPUT_GAMEPAD_START)) key_tap(K_ESC);
        }
        if (rm > 0) {
            float v = rm * rm * c_speed * ONC_I32(NOX_SCREEN_H) / 768.0f;
            int dx = (int)lroundf(rdx * v), dy = (int)lroundf(rdy * v);
            if (dx) mouse_ev(DIMOFS_X, (DWORD)dx);
            if (dy) mouse_ev(DIMOFS_Y, (DWORD)dy);
        }
        g_buttons = btn; g_lt = p->bLeftTrigger; g_rt = p->bRightTrigger;
        return;
    }

    /* ---- playing: each button's combo does its action (binds.c) ---- */
    onc_targets_enable(ingame);
    if (lb && !g_lb_down) g_lb_down = now ? now : 1;
    if (!lb) g_lb_down = 0;
    if (g_inv_down && now - g_inv_down >= 500) g_inv_help = 1; /* the inventory combo held: the controls list */
    /* R3 held + the right stick up / down: world zoom (the cursor does not move meanwhile); so R3's own combo
       does its action when released, if it did not zoom */
    static int r3_held, r3_zoomed, r3_lb;
    static DWORD r3_last, r3_down;
    int r3 = (btn & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
    extern int onc_zoom_ready(void);
    extern void onc_zoom_add(double v);
    if (r3 && !(was & XINPUT_GAMEPAD_RIGHT_THUMB)) { r3_held = 1; r3_zoomed = 0; r3_last = r3_down = now; r3_lb = lb; }
    if (r3_held && r3) {
        /* so that it is not finicky: R3 must be held [pad] zoom_hold_ms first, then the stick pushed past
           zoom_dead_zone_percent (of its travel past the stick dead zone) and mostly up / down (within ~30 degrees);
           the speed grows from 0 at the zoom dead zone; once zooming, the direction may lean up to 45 degrees */
        double dt = (now - r3_last) / 1000.0;
        r3_last = now;
        float zdz = c_zoom_dead / 100.0f;
        int armed = (int)(now - r3_down) >= c_zoom_hold;
        if (onc_zoom_ready() && armed && rm > zdz && rdy * rdy > (r3_zoomed ? 1 : 3) * rdx * rdx) {
            float v = (rm - zdz) / (1 - zdz);
            onc_zoom_add(-rdy * v * 1.5 * dt);
            r3_zoomed = 1;
        }
        rm = 0; /* no cursor movement / manual aim while R3 is held */
    }
    int r3_tap = 0;
    static DWORD r3_up;
    if (r3_held && !r3) { r3_held = 0; r3_tap = !r3_zoomed; r3_up = now; }
    /* in a game the right stick cursor mode starts only with the stick pushed past [pad] cursor_start_percent
       (of its travel past the dead zone) for 80 ms, not while R3 is held or within 250 ms after it (pressing R3 tilts
       the stick); the cursor then starts on the player. Once on, the normal dead zone moves it */
    static DWORD rs_since;
    if (ingame && !g_manual) {
        int quiet = r3_held || (int)(now - r3_up) < 250;
        if (!quiet && rm >= c_cur_start / 100.0f) {
            if (!rs_since) rs_since = now ? now : 1;
            if ((int)(now - rs_since) >= 80) {
                int px, py;
                extern int onc_player_screen(int *x, int *y);
                if (onc_player_screen(&px, &py)) cursor_to(px, py);
                g_manual = 1;
            }
        } else rs_since = 0;
        if (!g_manual) rm = 0;
    } else rs_since = 0;
    if (rm > 0) g_manual = 1;      /* aiming by hand */
    if (lm > 0) g_manual = 0;
    g_hide_cursor = ingame && !g_manual && c_hide_cursor;
    if ((int)(now - g_cast_cap) < 0 && onc_player_casting() && (int)(g_aim_until - (now + 100)) < 0)
        g_aim_until = now + 100; /* casting: keep aiming until the spell goes off */
    if (att_cap && (int)(now - att_cap) < 0) {
        /* attacking at the enemy: keep aiming until that attack is over (the arrow leaves late in it). Only the
           attack the pad started: once it is over (or if it has not begun within 400 ms) the hold ends, so a later
           attack state the game enters by itself does not keep the cursor on the enemy */
        if (onc_player_attacking()) {
            att_seen = 1;
            if ((int)(g_aim_until - (now + 100)) < 0) g_aim_until = now + 100;
        } else if (att_seen || (int)(now - att_start) > 400) {
            att_cap = now; att_seen = 0;
        }
    }
    onc_targets_set_aiming(g_aim_a || (int)(now - g_aim_until) < 0);
    int ax = 0, ay = 0;
    int aim = !g_manual && onc_aim_screen(&ax, &ay); /* an enemy is locked on */
    /* a spell key aimed at the enemy: once the cursor has been on it and the player faces it (at most 150 ms
       more), so missiles leave towards it too */
    if (g_pend_key && (int)(now - g_pend_at) >= 0 && (onc_facing_target() || (int)(now - g_pend_at) >= 150)) {
        key_ev(g_pend_key, 1);
        if (g_pend_released) { /* the button is already up: a tap */
            if (g_tap_key) key_ev(g_tap_key, 0);
            g_tap_key = g_pend_key; g_tap_release = now + 80;
        }
        g_pend_key = 0;
    }
    if (r3_tap && !r3_lb && onc_pad_cursor_mode()) {
        /* right stick cursor mode (the cursor shown, until the left stick is used): R3 = a left click where
           the cursor is, held 80 ms (a press and release in one frame can be missed) */
        if (!g_lclick) mouse_ev(DIMOFS_BUTTON0, 0x80);
        g_lclick = 1; att_hold = now + 80;
        r3_tap = 0;
    }
    if (r3_tap) { /* R3 released without zooming: its combo's action, as a tap */
        int c = combo_for(B_RS, r3_lb), act = onc_bind_action(c);
        switch (onc_bind_kind(act)) {
        case K_KEY: case K_INVENTORY: case K_MENU: { BYTE k = onc_bind_key_of(act, NULL); if (k) key_tap(k); break; }
        case K_ATTACK: mouse_ev(DIMOFS_BUTTON0, 0x80); mouse_ev(DIMOFS_BUTTON0, 0); break;
        case K_INTERACT: onc_interact(); break;
        case K_WHEEL: onc_radial_open(); break;
        case K_CREATURES: creatures_open(); break;
        }
    }
    for (int b = 0; b < B_N; b++) {
        int down, wasd;
        if (b == B_LT) { down = p->bLeftTrigger > 100; wasd = g_lt > 100; }
        else if (b == B_RT) { down = p->bRightTrigger > 100; wasd = g_rt > 100; }
        else { down = (btn & xbits[b]) != 0; wasd = (was & xbits[b]) != 0; }
        if (b == B_LB || b == B_RS) continue; /* L1: the modifier; R3: above */
        if (down && !wasd) {
            int c = combo_for(b, lb), act = onc_bind_action(c);
            g_held[b] = act;
            switch (onc_bind_kind(act)) {
            case K_KEY: {
                int aims = 0;
                BYTE k = onc_bind_key_of(act, &aims);
                g_mod_keys[b] = k;
                if (k && aim && aims) {
                    /* a spell / ability slot: the cursor on the enemy first, the key one poll later */
                    cursor_to(ax, ay);
                    g_aim_until = now + 300;   /* then as long as the spell is being cast */
                    g_cast_cap = now + 4000;
                    if (g_pend_key) key_ev(g_pend_key, 1);
                    g_pend_key = k; g_pend_b = b; g_pend_released = 0; g_pend_at = now + 30;
                } else if (k) key_ev(k, 1);
                break;
            }
            case K_ATTACK: g_attack_btn |= 1 << b; break;
            case K_AIM: g_aim_btn |= 1 << b; break;
            case K_INTERACT: onc_interact(); break;
            case K_WHEEL: onc_radial_open(); break;
            case K_INVENTORY: g_inv_down = now ? now : 1; g_inv_help = 0; g_inv_combo = c; break;
            case K_MENU: key_tap(K_ESC); break;
            case K_CREATURES: creatures_open(); break;
            }
        } else if (!down && wasd) {
            int act = g_held[b];
            g_held[b] = 0;
            switch (onc_bind_kind(act)) {
            case K_KEY: key_up(b); break;
            case K_ATTACK: g_attack_btn &= ~(1 << b); break;
            case K_AIM: g_aim_btn &= ~(1 << b); break;
            case K_INVENTORY:
                if (g_inv_down && !g_inv_help) { BYTE k = onc_bind_key_of(act, NULL); if (k) key_tap(k); }
                g_inv_down = 0; g_inv_combo = -1;
                break;
            }
        }
    }
    /* Attack with short clicks, repeated at a steady interval while held. */
    /* With an enemy locked on, the cursor goes onto it first and the button follows once the player faces it
       (onc_facing_target; at most 150 ms): shots and swings leave along the facing, which turns to the cursor
       only at the end of a game frame, so pressing at once sent the first arrow where the left stick pointed.
       A short tap meanwhile still clicks (held at least 80 ms). */
    int a = g_attack_btn != 0;
    if (!a) att_repeat_wait = 0;
    if (a && !g_lclick && !att_wait && (!att_repeat_wait || (int)(now - att_repeat_at) >= 0)) {
        if (aim) {
            cursor_to(ax, ay); g_aim_a = 1;
            att_wait = 1; att_until = now + 150;
        } else {
            mouse_ev(DIMOFS_BUTTON0, 0x80); g_lclick = 1; att_hold = now + 80;
            att_cap = now + 2000; att_start = now; att_seen = 0;
            att_repeat_at = now + c_attack_repeat; att_repeat_wait = 1;
        }
    }
    if (att_wait && (!aim || onc_facing_target() || (int)(now - att_until) >= 0)) {
        att_wait = 0;
        mouse_ev(DIMOFS_BUTTON0, 0x80); g_lclick = 1; att_hold = now + 80;
        att_cap = now + 2000; att_start = now; att_seen = 0;
        att_repeat_at = now + c_attack_repeat; att_repeat_wait = 1;
    }
    if (!a && g_aim_a && !att_wait && !g_lclick) { g_aim_a = 0; g_aim_until = now + 150; }
    if (!att_wait) {
        int want = g_lclick && (int)(now - att_hold) < 0;
        if (want != g_lclick) { mouse_ev(DIMOFS_BUTTON0, want ? 0x80 : 0); g_lclick = want; }
        if (!g_lclick && !a && g_aim_a) { g_aim_a = 0; g_aim_until = now + 150; }
    }
    g_buttons = btn;
    g_lt = p->bLeftTrigger;
    g_rt = p->bRightTrigger;

    /* ---- sticks -> cursor ---- */
    if (!ingame) {
        /* outside a game: the left stick moves the cursor like the right one */
        if (lm > rm) { rdx = ldx; rdy = ldy; rm = lm; }
        lm = 0;
    }
    /* the game faces the player towards the cursor relative to the centre of the screen it draws
       (NOX_SCREEN_W/H, 0x42F5DA); the cursor bounds can be larger than that */
    int sw = ONC_I32(NOX_SCREEN_W), sh = ONC_I32(NOX_SCREEN_H);
    int pcx, pcy; /* the player on the screen (not always the centre with the smooth camera) */
    extern int onc_player_screen(int *x, int *y);
    onc_player_screen(&pcx, &pcy);
    if (!g_aim_btn) g_aim_dx = g_aim_dy = 0; /* the cone follows the facing until the stick turns it */
    int smx, smy;
    extern int onc_summon_pending_xy(int *x, int *y);
    if (onc_summon_pending_xy(&smx, &smy)) {
        /* opening the creature order menu: the cursor stays on the panel's button */
        cursor_to(smx, smy);
        if (g_moving) stop_moving();
    } else if ((g_aim_a || (int)(now - g_aim_until) < 0) && ingame) {
        /* attacking / casting at the locked-on enemy: the cursor stays on it, the player stands */
        if (aim) cursor_to(ax, ay);
        if (g_moving) stop_moving();
    } else if (lm > 0 && sw > 0 && sh > 0 && ingame && g_aim_btn) {
        /* the aim combo held: the left stick only turns the player (and the lock-on cone), it does not walk */
        g_aim_dx = ldx; g_aim_dy = ldy;
        float r = (float)c_walk_r * sh / 768.0f;
        cursor_to(pcx + (int)lroundf(ldx * r), pcy + (int)lroundf(ldy * r));
        if (g_moving) stop_moving();
    } else if (lm > 0 && sw > 0 && sh > 0) {
        float r = lm * 100 < c_walk ? (float)c_walk_r * sh / 768.0f : sh * c_run_r / 100.0f;
        cursor_to(pcx + (int)lroundf(ldx * r), pcy + (int)lroundf(ldy * r));
        if (!g_rclick) { mouse_ev(DIMOFS_BUTTON1, 0x80); g_rclick = 1; }
        g_moving = 1;
    } else if (g_moving) {
        stop_moving();
    }
    if (rm > 0) {
        float v = rm * rm * c_speed * sh / 768.0f;
        int dx = (int)lroundf(rdx * v), dy = (int)lroundf(rdy * v);
        if (dx) mouse_ev(DIMOFS_X, (DWORD)dx);
        if (dy) mouse_ev(DIMOFS_Y, (DWORD)dy);
    }
#undef PRESSED
}

int onc_pad_mouse_event(DIDEVICEOBJECTDATA *out) {
    if (g_mh == g_mt) poll();
    if (g_mh == g_mt) return 0;
    *out = g_mq[g_mh];
    g_mh = (g_mh + 1) % QN;
    if (out->dwOfs == DIMOFS_X) g_pend_x -= (int)out->dwData;
    if (out->dwOfs == DIMOFS_Y) g_pend_y -= (int)out->dwData;
    return 1;
}

int onc_pad_key_event(DIDEVICEOBJECTDATA *out) {
    if (g_kh == g_kt) poll();
    if (g_kh == g_kt) return 0;
    *out = g_kq[g_kh];
    g_kh = (g_kh + 1) % QN;
    return 1;
}

static void xinput_load(void) {
    g_xinit = 1;
    static const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (int i = 0; i < 3 && !g_xget; i++) {
        HMODULE m = LoadLibraryA(dlls[i]);
        if (m) {
            g_xget = (xinput_get_fn)GetProcAddress(m, "XInputGetState");
            if (g_xget) onc_log("XInput: %s", dlls[i]);
        }
    }
    if (!g_xget) onc_log("XInput not found: no gamepad");
}

/* Movies (the intro, cut scenes) play without the game reading DirectInput, so the pad is not polled then.
   A watcher thread notices that and turns A / B / Start into Esc for the game window, which skips the movie. */
static BOOL CALLBACK find_game_window(HWND h, LPARAM lp) {
    DWORD pid;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) { *(HWND *)lp = h; return FALSE; }
    return TRUE;
}

static DWORD WINAPI movie_watch(LPVOID arg) {
    (void)arg;
    WORD was = 0;
    for (;;) {
        Sleep(33);
        if (!g_xinit) xinput_load();
        if (!g_xget) return 0;
        XINPUT_STATE st;
        int idx = g_pad >= 0 ? g_pad : 0;
        if (g_xget(idx, &st) != ERROR_SUCCESS) { was = 0; continue; }
        WORD b = st.Gamepad.wButtons, pressed = b & ~was;
        was = b;
        if (GetTickCount() - g_last_poll < 400) continue; /* the game reads input: the normal path handles the pad */
        if (pressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_START)) {
            HWND h = NULL;
            EnumWindows(find_game_window, (LPARAM)&h);
            if (h) {
                PostMessageA(h, WM_KEYDOWN, VK_ESCAPE, 0x00010001);
                PostMessageA(h, WM_KEYUP, VK_ESCAPE, 0xC0010001);
                onc_log("movie: skip (Esc)");
            }
        }
    }
}

/* The game's own joystick support: Game.exe reads controllers itself through winmm (joyGetNumDevs / joyGetPosEx /
   joyGetDevCapsA, import slots 0x57F318..0x57F320; nox.cfg can bind JOYSTICK_BUTTON_1..16). With two readers of
   the same pad a button could act twice, so while our gamepad support is on, the game is told that
   no joystick is connected ([pad] game_joystick = 1 keeps the game's own). The slots are checked first. */
static UINT WINAPI no_joy_num(void) { return 0; }
static MMRESULT WINAPI no_joy_pos(UINT id, LPJOYINFOEX ji) { (void)id; (void)ji; return JOYERR_UNPLUGGED; }
static MMRESULT WINAPI no_joy_caps(UINT_PTR id, LPJOYCAPSA jc, UINT n) { (void)id; (void)jc; (void)n; return JOYERR_UNPLUGGED; }
static void game_joystick_off(void) {
    HMODULE wm = GetModuleHandleA("winmm.dll");
    static const struct { uintptr_t slot; const char *name; void *repl; } k[] = {
        {0x57F318, "joyGetNumDevs", (void *)no_joy_num}, {0x57F31C, "joyGetPosEx", (void *)no_joy_pos},
        {0x57F320, "joyGetDevCapsA", (void *)no_joy_caps},
    };
    int n = 0;
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++) {
        void **slot = (void **)k[i].slot, *real = wm ? (void *)GetProcAddress(wm, k[i].name) : NULL;
        if (!real || *slot != real) { onc_log("game joystick: %s slot %08X not as expected, left alone", k[i].name, (unsigned)k[i].slot); continue; }
        DWORD old;
        if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) continue;
        *slot = k[i].repl;
        VirtualProtect(slot, sizeof *slot, old, &old);
        n++;
    }
    onc_log("game joystick: %s (the pad is ours; [pad] game_joystick = 1 keeps the game's own)", n == 3 ? "off" : "partly off");
}

void onc_pad_init(void) {
    onc_binds_init();
    c_dead = onc_cfg_int("pad", "dead_zone_percent", c_dead);
    c_walk = onc_cfg_int("pad", "walk_below_percent", c_walk);
    c_walk_r = onc_cfg_int("pad", "walk_ring_pixels", c_walk_r);
    c_run_r = onc_cfg_int("pad", "run_ring_percent", c_run_r);
    c_speed = onc_cfg_int("pad", "cursor_speed", c_speed);
    c_attack_repeat = onc_cfg_int("pad", "attack_repeat_ms", c_attack_repeat);
    if (c_attack_repeat < 100) c_attack_repeat = 100; else if (c_attack_repeat > 2000) c_attack_repeat = 2000;
    c_hide_cursor = onc_cfg_int("pad", "hide_cursor", 1);
    c_r3_click = onc_cfg_int("pad", "r3_click", 1);
    c_zoom_hold = onc_cfg_int("pad", "zoom_hold_ms", c_zoom_hold);
    c_cur_start = onc_cfg_int("pad", "cursor_start_percent", c_cur_start);
    if (c_cur_start < 0) c_cur_start = 0; else if (c_cur_start > 95) c_cur_start = 95;
    c_zoom_dead = onc_cfg_int("pad", "zoom_dead_zone_percent", c_zoom_dead);
    if (c_zoom_dead < 0) c_zoom_dead = 0; else if (c_zoom_dead > 90) c_zoom_dead = 90;
    g_enabled = onc_cfg_int("pad", "enabled", 1);
    if (!g_enabled) { onc_log("gamepad off (nox-modernized.ini)"); return; }
    if (!onc_cfg_int("pad", "game_joystick", 0)) game_joystick_off();
    CloseHandle(CreateThread(NULL, 0, movie_watch, NULL, 0, NULL));
}
