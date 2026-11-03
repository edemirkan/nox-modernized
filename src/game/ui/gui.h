/* The game's window system (Game.exe). Field offsets match OpenNox's client/gui Window struct (404 bytes). */
#pragma once
#include "onc.h"

typedef struct nox_win nox_win; /* opaque */

#define NOXW_I32(w, off) (*(volatile int32_t *)((char *)(w) + (off)))
#define NOXW_PTR(w, off) (*(nox_win *volatile *)((char *)(w) + (off)))

#define NOXW_ID(w)        NOXW_I32(w, 0x000)
#define NOXW_FLAGS(w)     ((uint32_t)NOXW_I32(w, 0x004)) /* status flags, see NOXS_* */
#define NOXW_W(w)         NOXW_I32(w, 0x008)
#define NOXW_H(w)         NOXW_I32(w, 0x00C)
#define NOXW_X(w)         NOXW_I32(w, 0x010)             /* offset from the parent */
#define NOXW_Y(w)         NOXW_I32(w, 0x014)
#define NOXW_WIDGET(w)    (*(void *volatile *)((char *)(w) + 0x020))
#define NOXW_STYLE(w)     ((uint32_t)NOXW_I32(w, 0x02C)) /* draw data style, see NOXT_* */
#define NOXW_FUNC93(w)    (*(void *volatile *)((char *)(w) + 0x174)) /* input proc */
#define NOXW_FUNC94(w)    (*(void *volatile *)((char *)(w) + 0x178)) /* window proc (notifications) */
#define NOXW_PREV(w)      NOXW_PTR(w, 0x184)
#define NOXW_NEXT(w)      NOXW_PTR(w, 0x188)
#define NOXW_PARENT(w)    NOXW_PTR(w, 0x18C)
#define NOXW_LASTCHILD(w) NOXW_PTR(w, 0x190)
#define NOXW_DRAWWIN(w)   NOXW_PTR(w, 0x034) /* draw data's window: where notifications go (usually the parent) */

/* status flags */
#define NOXS_ENABLED   0x8
#define NOXS_HIDDEN    0x10
#define NOXS_DESTROYED 0x800
/* styles */
#define NOXT_PUSHBUTTON  0x1
#define NOXT_RADIO       0x2
#define NOXT_CHECKBOX    0x4
#define NOXT_VSLIDER     0x8
#define NOXT_HSLIDER     0x10
#define NOXT_LISTBOX     0x20
#define NOXT_ENTRY       0x80

/* globals (Game.exe) */
#define NOX_GUI_LAST     0x6F73E4 /* last top-level window (list via PREV) */
#define NOX_GUI_HEAD     0x6F73E8
#define NOX_GUI_CAPTURED 0x6F73F4 /* window that captured the mouse */
#define NOX_GUI_FOCUSED  0x6F73F8
#define NOX_GUI_MODAL    0x6F73FC /* modal stack: node {window, next} */
#define NOX_GAME_FLAGS   0x5D2EB4 /* nox_common_gameFlags */
#define NOX_FLAG_PAUSE   0x40000

typedef int (__cdecl *nox_winproc)(nox_win *w, int code, uintptr_t a1, uintptr_t a2);
#define NOXE_KEY 21 /* func93: key event (a1 = DIK code, a2 = 2 pressed / 1 released) */

typedef struct { int x0, y0, x1, y1; } onc_rect;

int  onc_win_alive(nox_win *w);
int  onc_win_visible(nox_win *w);    /* it and all parents shown */
onc_rect onc_win_rect(nox_win *w);   /* screen rectangle */
int  onc_win_key(nox_win *w, int dik); /* press + release through the window's own input proc; 1 = handled */
void onc_win_press(nox_win *w);        /* press a button the way the game does */
