/* Nox Modernized display: our own ddraw.dll. It imitates the part of DirectDraw that Nox uses (see
   docs/03-graphics.md) with surfaces in system memory, and shows the finished frames with Direct3D 11
   (GDI if Direct3D 11 is not available) in a borderless full-screen window or a normal window. The game thinks
   it has an exclusive full-screen mode; the desktop resolution never changes. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <stdint.h>

void dsp_log(const char *fmt, ...);
int  dsp_cfg_int(const char *key, int def);
void dsp_cfg_str(const char *key, const char *def, char *out, int n);

/* presenter (present.c) */
void dsp_window(HWND hwnd, int w, int h);                     /* the game's window and its mode (SetDisplayMode) */
void dsp_present(const uint16_t *px, int pitch, int w, int h); /* a finished RGB565 frame */
void dsp_set_gamma(const WORD *r, const WORD *g, const WORD *b); /* NULL: none */
void dsp_shutdown(void);

/* a frame in two layers (Nox Modernized's split rendering): the world (a rectangle of a 565 buffer, scaled to the
   whole frame fw x fh) and the GUI (gw x gh, shown where mask is set) on top of it */
typedef struct {
    const uint16_t *world; int wpitch, wx, wy, ww, wh;
    int fw, fh;
    const uint16_t *gui; int gpitch, gw, gh;
    const uint8_t *mask; /* gw x gh, 1 = GUI; or NULL, then: */
    const uint16_t *snap; /* gw x gh (pitch gw): the GUI pixels that still equal it show the world */
    /* the mouse cursor as a layer of its own (drawn at every picture where the mouse is now): two captures of the
       same drawing, over black (a) and over white (b), cur_w x cur_h, cur_x / cur_y on the GUI; cur_w 0 = none */
    const uint16_t *cur_a, *cur_b; int cur_pitch, cur_w, cur_h, cur_x, cur_y, cur_serial;
} dsp_layers;
void dsp_present_layers(const dsp_layers *L);
