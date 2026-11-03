/* The game's own 2D drawing (Game.exe, cdecl). Everything drawn with these goes through the game's renderer,
   so it scales and moves with whatever the renderer does (custom resolutions, a future Direct3D 11 back end).
   Coordinates are game screen pixels. Call only while the game draws (inside a window's draw proc). */
#pragma once
#include "onc.h"

#define NOX_RENDER_DATA   0x952FA0 /* -> render state: +0 clip on, +4 clip rect, +0xEC text color, +0xF4 color */
#define NOX_COLOR_YELLOW  0x9647A0 /* the game's yellow (a color value) */

int  onc_rgb(int r, int g, int b);                         /* a color in the screen's pixel format */
void onc_draw_color(int c);                                /* color of rectangles */
void onc_draw_rect(int x, int y, int w, int h);            /* filled, opaque */
void onc_draw_frame(int x, int y, int w, int h, int c);    /* 1 pixel outline */
void onc_draw_darken(int x, int y, int w, int h);          /* translucent black (halves the pixels) */
void onc_text_color(int c);
void onc_text_size(const wchar_t *s, int *w, int *h);      /* default font */
void onc_text(const wchar_t *s, int x, int y);             /* default font, top left */

/* drawing outside the current window: clipped to the whole screen until onc_draw_end */
void onc_draw_begin(void);
void onc_draw_clip(int x, int y, int w, int h); /* between begin and end */
void onc_draw_end(void);
