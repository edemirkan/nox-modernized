#include "draw.h"
#include <string.h>

#define FN_COLOR       0x435E10 /* int color(r, g, b) */
#define FN_SET_COLOR   0x435DD0 /* setColor(c) */
#define FN_RECT        0x484770 /* drawRectFilledOpaque(x, y, w, h) */
#define FN_TEXT_COLOR  0x435D00 /* setTextColor(c) */
#define FN_TEXT_SIZE   0x43F3C0 /* getStringSize(font, wstr, &w, &h, maxw) */
#define FN_TEXT        0x43F260 /* drawString(font, wstr, x, y) */

int onc_rgb(int r, int g, int b) { return ((int (__cdecl *)(int, int, int))FN_COLOR)(r, g, b); }
void onc_draw_color(int c) { ((void (__cdecl *)(int))FN_SET_COLOR)(c); }
void onc_draw_rect(int x, int y, int w, int h) {
    if (w > 0 && h > 0) ((void (__cdecl *)(int, int, int, int))FN_RECT)(x, y, w, h);
}
void onc_draw_frame(int x, int y, int w, int h, int c) {
    onc_draw_color(c);
    onc_draw_rect(x, y, w, 1);
    onc_draw_rect(x, y + h - 1, w, 1);
    onc_draw_rect(x, y, 1, h);
    onc_draw_rect(x + w - 1, y, 1, h);
}
void onc_draw_darken(int x, int y, int w, int h) { /* the game's translucent fill: halves the pixels (0x484850) */
    if (w > 0 && h > 0) ((void (__cdecl *)(int, int, int, int))0x484850)(x, y, w, h);
}
void onc_text_color(int c) { ((void (__cdecl *)(int))FN_TEXT_COLOR)(c); }
void onc_text_size(const wchar_t *s, int *w, int *h) {
    int tw = 0, th = 0;
    ((int (__cdecl *)(void *, const wchar_t *, int *, int *, int))FN_TEXT_SIZE)(NULL, s, &tw, &th, 0);
    if (w) *w = tw;
    if (h) *h = th;
}
void onc_text(const wchar_t *s, int x, int y) {
    ((int (__cdecl *)(void *, const wchar_t *, int, int))FN_TEXT)(NULL, s, x, y);
}

#define FN_SET_CLIP 0x48A120 /* setClip(x, y, w, h): render state +4 rect, +0x14 inclusive rect */

static int32_t g_saved[9]; /* render state +0 .. +0x24: clip on, clip rects */
static int g_begun;

void onc_draw_clip(int x, int y, int w, int h) {
    int32_t *rd = *(int32_t **)NOX_RENDER_DATA;
    if (!rd) return;
    ((void (__cdecl *)(int, int, int, int))FN_SET_CLIP)(x, y, w, h);
    rd[0] = 1;
}

void onc_draw_begin(void) {
    int32_t *rd = *(int32_t **)NOX_RENDER_DATA;
    if (!rd || g_begun) return;
    memcpy(g_saved, rd, sizeof g_saved);
    onc_draw_clip(0, 0, ONC_I32(NOX_SCREEN_W), ONC_I32(NOX_SCREEN_H)); /* the whole screen, clipped */
    g_begun = 1;
}
void onc_draw_end(void) {
    int32_t *rd = *(int32_t **)NOX_RENDER_DATA;
    if (rd && g_begun) memcpy(rd, g_saved, sizeof g_saved);
    g_begun = 0;
}
