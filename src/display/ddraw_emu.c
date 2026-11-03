/* The DirectDraw objects Nox uses, imitated: IDirectDraw (v1), surfaces (IDirectDrawSurface v1), a clipper,
   palettes and the gamma control. Surfaces are plain memory; a Flip of the primary swaps the chain's memory and
   shows the new front surface (present.c). */
#include "display.h"
#include <string.h>
#include <stdlib.h>

#define HR HRESULT
#define STD __stdcall

static const GUID k_IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID k_IID_DD = {0x6C14DB80, 0xA733, 0x11CE, {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};
static const GUID k_IID_Surf = {0x6C14DB81, 0xA733, 0x11CE, {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};
static const GUID k_IID_Gamma = {0x69C11C3E, 0xB46B, 0x11D1, {0xAD, 0x7A, 0x00, 0xC0, 0x4F, 0xC2, 0x9B, 0x4E}};

static int same(REFIID a, const GUID *b) { return a && !memcmp(a, b, sizeof(GUID)); }

/* ---------------- objects ---------------- */
typedef struct dd_obj { void **vtbl; LONG ref; HWND hwnd; DWORD coop; int w, h, bpp; } dd_obj;
typedef struct surf {
    void **vtbl; LONG ref;
    int w, h, bpp, pitch;
    uint8_t *mem;            /* pixels (swapped along the flip chain) */
    DWORD caps;
    int primary;
    struct surf *back[2];    /* primary: its back buffers; a back buffer: back[0] = the primary */
    int nback;
    struct pal *pal;
    HDC dc; HBITMAP dib; void *dib_bits; HGDIOBJ dc_old;
} surf;
typedef struct clip { void **vtbl; LONG ref; HWND hwnd; } clip;
typedef struct pal { void **vtbl; LONG ref; PALETTEENTRY e[256]; } pal;
typedef struct gamma { void **vtbl; LONG ref; surf *s; } gamma;

static dd_obj *g_dd;         /* the current DirectDraw object (one at a time in Nox) */
static surf *g_primary;
static DDGAMMARAMP g_ramp;
static int g_ramp_set;

/* ---------------- helpers ---------------- */
static void pixfmt(DDPIXELFORMAT *pf, int bpp) {
    memset(pf, 0, sizeof *pf);
    pf->dwSize = sizeof *pf;
    if (bpp == 8) { pf->dwFlags = DDPF_RGB | DDPF_PALETTEINDEXED8; pf->dwRGBBitCount = 8; return; }
    pf->dwFlags = DDPF_RGB;
    pf->dwRGBBitCount = bpp;
    if (bpp == 16) { pf->dwRBitMask = 0xF800; pf->dwGBitMask = 0x07E0; pf->dwBBitMask = 0x001F; }
    else { pf->dwRBitMask = 0xFF0000; pf->dwGBitMask = 0x00FF00; pf->dwBBitMask = 0x0000FF; }
}

static void fill_desc(surf *s, DDSURFACEDESC *d) {
    DWORD size = d->dwSize ? d->dwSize : sizeof *d;
    memset(d, 0, size < sizeof *d ? size : sizeof *d);
    d->dwSize = size;
    d->dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT;
    d->dwWidth = s->w; d->dwHeight = s->h; d->lPitch = s->pitch;
    d->ddsCaps.dwCaps = s->caps;
    if (s->primary) { d->dwFlags |= DDSD_BACKBUFFERCOUNT; d->dwBackBufferCount = s->nback; }
    pixfmt(&d->ddpfPixelFormat, s->bpp);
}

static surf *new_surf(int w, int h, int bpp, DWORD caps);
/* Nox Modernized's mss32.dll can replace the frame shown (split rendering: the world at full size, zoomed, with
   the GUI on top) */
typedef int (__cdecl *frame_hook_t)(const uint16_t *px, int pitch, int w, int h, dsp_layers *out);
static frame_hook_t g_frame_hook;
__declspec(dllexport) void __cdecl OncSetFrameHook(frame_hook_t fn) { g_frame_hook = fn; dsp_log("frame hook %s", fn ? "set" : "cleared"); }
static void present_primary(void) {
    surf *s = g_primary;
    if (!s || s->bpp != 16) return;
    dsp_layers L;
    memset(&L, 0, sizeof L);
    if (g_frame_hook && g_frame_hook((const uint16_t *)s->mem, s->pitch, s->w, s->h, &L))
        dsp_present_layers(&L);
    else
        dsp_present((const uint16_t *)s->mem, s->pitch, s->w, s->h);
}

/* ---------------- IUnknown parts ---------------- */
#define REF_FUNCS(T, name, freefn) \
    static ULONG STD name##_AddRef(T *o) { return InterlockedIncrement(&o->ref); } \
    static ULONG STD name##_Release(T *o) { LONG r = InterlockedDecrement(&o->ref); if (r <= 0) { freefn(o); return 0; } return r; }

/* ---------------- gamma control ---------------- */
static void gamma_free(gamma *g) { HeapFree(GetProcessHeap(), 0, g); }
REF_FUNCS(gamma, gam, gamma_free)
static HR STD gam_QI(gamma *g, REFIID iid, void **out) { *out = NULL; return E_NOINTERFACE; }
static HR STD gam_Get(gamma *g, DWORD fl, DDGAMMARAMP *r) {
    if (!r) return DDERR_INVALIDPARAMS;
    if (!g_ramp_set) for (int i = 0; i < 256; i++) g_ramp.red[i] = g_ramp.green[i] = g_ramp.blue[i] = (WORD)(i * 257);
    *r = g_ramp;
    return DD_OK;
}
static HR STD gam_Set(gamma *g, DWORD fl, DDGAMMARAMP *r) {
    if (!r) { g_ramp_set = 0; dsp_set_gamma(NULL, NULL, NULL); return DD_OK; }
    g_ramp = *r; g_ramp_set = 1;
    dsp_set_gamma(g_ramp.red, g_ramp.green, g_ramp.blue);
    return DD_OK;
}
static void *g_gamma_vt[] = {gam_QI, gam_AddRef, gam_Release, gam_Get, gam_Set};

/* ---------------- clipper ---------------- */
static void clip_free(clip *c) { HeapFree(GetProcessHeap(), 0, c); }
REF_FUNCS(clip, clp, clip_free)
static HR STD clp_QI(clip *c, REFIID iid, void **out) { *out = NULL; return E_NOINTERFACE; }
static HR STD clp_GetClipList(clip *c, RECT *r, RGNDATA *d, DWORD *n) { if (n) *n = 0; return DDERR_NOCLIPLIST; }
static HR STD clp_GetHWnd(clip *c, HWND *h) { if (h) *h = c->hwnd; return DD_OK; }
static HR STD clp_Initialize(clip *c, void *dd, DWORD f) { return DDERR_ALREADYINITIALIZED; }
static HR STD clp_IsClipListChanged(clip *c, BOOL *b) { if (b) *b = FALSE; return DD_OK; }
static HR STD clp_SetClipList(clip *c, RGNDATA *d, DWORD f) { return DD_OK; }
static HR STD clp_SetHWnd(clip *c, DWORD f, HWND h) { c->hwnd = h; return DD_OK; }
static void *g_clip_vt[] = {clp_QI, clp_AddRef, clp_Release, clp_GetClipList, clp_GetHWnd, clp_Initialize,
                            clp_IsClipListChanged, clp_SetClipList, clp_SetHWnd};

/* ---------------- palette ---------------- */
static void pal_free(pal *p) { HeapFree(GetProcessHeap(), 0, p); }
REF_FUNCS(pal, pl, pal_free)
static HR STD pl_QI(pal *p, REFIID iid, void **out) { *out = NULL; return E_NOINTERFACE; }
static HR STD pl_GetCaps(pal *p, DWORD *c) { if (c) *c = DDPCAPS_8BIT | DDPCAPS_ALLOW256; return DD_OK; }
static HR STD pl_GetEntries(pal *p, DWORD f, DWORD base, DWORD n, PALETTEENTRY *e) {
    if (!e || base + n > 256) return DDERR_INVALIDPARAMS;
    memcpy(e, p->e + base, n * sizeof *e);
    return DD_OK;
}
static HR STD pl_Initialize(pal *p, void *dd, DWORD f, PALETTEENTRY *e) { return DDERR_ALREADYINITIALIZED; }
static HR STD pl_SetEntries(pal *p, DWORD f, DWORD base, DWORD n, PALETTEENTRY *e) {
    if (!e || base + n > 256) return DDERR_INVALIDPARAMS;
    memcpy(p->e + base, e, n * sizeof *e);
    return DD_OK;
}
static void *g_pal_vt[] = {pl_QI, pl_AddRef, pl_Release, pl_GetCaps, pl_GetEntries, pl_Initialize, pl_SetEntries};

/* ---------------- surfaces ---------------- */
static void surf_free(surf *s) {
    if (s == g_primary) g_primary = NULL;
    if (s->primary)
        for (int i = 0; i < s->nback; i++) if (s->back[i]) { s->back[i]->back[0] = NULL; s->back[i]->ref = 0; HeapFree(GetProcessHeap(), 0, s->back[i]->mem); HeapFree(GetProcessHeap(), 0, s->back[i]); }
    if (s->dib) { DeleteObject(s->dib); }
    if (s->dc) DeleteDC(s->dc);
    HeapFree(GetProcessHeap(), 0, s->mem);
    HeapFree(GetProcessHeap(), 0, s);
}
static ULONG STD sf_AddRef(surf *s) { return InterlockedIncrement(&s->ref); }
static ULONG STD sf_Release(surf *s) {
    LONG r = InterlockedDecrement(&s->ref);
    /* a back buffer belongs to its primary (freed with it) */
    if (r <= 0 && !(s->caps & DDSCAPS_BACKBUFFER && s->back[0])) { surf_free(s); return 0; }
    return r < 0 ? 0 : r;
}
static HR STD sf_QI(surf *s, REFIID iid, void **out) {
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (same(iid, &k_IID_Gamma)) {
        gamma *g = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *g);
        g->vtbl = g_gamma_vt; g->ref = 1; g->s = s;
        *out = g;
        return DD_OK;
    }
    if (same(iid, &k_IID_Surf) || same(iid, &k_IID_IUnknown)) { sf_AddRef(s); *out = s; return DD_OK; }
    return E_NOINTERFACE;
}
static HR STD sf_AddAttachedSurface(surf *s, surf *a) { return DDERR_CANNOTATTACHSURFACE; }
static HR STD sf_AddOverlayDirtyRect(surf *s, RECT *r) { return DDERR_UNSUPPORTED; }

static void clip_rect(surf *s, RECT *r) {
    if (r->left < 0) r->left = 0;
    if (r->top < 0) r->top = 0;
    if (r->right > s->w) r->right = s->w;
    if (r->bottom > s->h) r->bottom = s->h;
}
static void fill(surf *s, RECT *r, DWORD color) {
    RECT a = r ? *r : (RECT){0, 0, s->w, s->h};
    clip_rect(s, &a);
    int bp = s->bpp / 8;
    for (int y = a.top; y < a.bottom; y++) {
        uint8_t *p = s->mem + y * s->pitch + a.left * bp;
        for (int x = a.left; x < a.right; x++, p += bp) {
            if (bp == 2) *(uint16_t *)p = (uint16_t)color;
            else if (bp == 1) *p = (uint8_t)color;
            else *(uint32_t *)p = color;
        }
    }
}
static void copy(surf *d, RECT *dr, surf *s, RECT *sr, int keyed, DWORD key) {
    RECT a = dr ? *dr : (RECT){0, 0, d->w, d->h}, b = sr ? *sr : (RECT){0, 0, s->w, s->h};
    int dw = a.right - a.left, dh = a.bottom - a.top, sw = b.right - b.left, sh = b.bottom - b.top;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || d->bpp != s->bpp) return;
    int bp = d->bpp / 8;
    for (int y = 0; y < dh; y++) {
        int dy = a.top + y, sy = b.top + y * sh / dh;
        if (dy < 0 || dy >= d->h || sy < 0 || sy >= s->h) continue;
        for (int x = 0; x < dw; x++) {
            int dx = a.left + x, sx = b.left + x * sw / dw;
            if (dx < 0 || dx >= d->w || sx < 0 || sx >= s->w) continue;
            uint8_t *dp = d->mem + dy * d->pitch + dx * bp, *sp = s->mem + sy * s->pitch + sx * bp;
            if (bp == 2) { uint16_t v = *(uint16_t *)sp; if (!keyed || v != (uint16_t)key) *(uint16_t *)dp = v; }
            else if (bp == 1) { if (!keyed || *sp != (uint8_t)key) *dp = *sp; }
            else { uint32_t v = *(uint32_t *)sp; if (!keyed || v != key) *(uint32_t *)dp = v; }
        }
    }
}
static HR STD sf_Blt(surf *s, RECT *dr, surf *src, RECT *sr, DWORD fl, DDBLTFX *fx) {
    if (fl & DDBLT_COLORFILL) fill(s, dr, fx ? fx->dwFillColor : 0);
    else if (src) copy(s, dr, src, sr, (fl & DDBLT_KEYSRCOVERRIDE) != 0, fx ? fx->ddckSrcColorkey.dwColorSpaceLowValue : 0);
    if (s == g_primary) present_primary();
    return DD_OK;
}
static HR STD sf_BltBatch(surf *s, DDBLTBATCH *b, DWORD n, DWORD f) { return DDERR_UNSUPPORTED; }
static HR STD sf_BltFast(surf *s, DWORD x, DWORD y, surf *src, RECT *sr, DWORD fl) {
    if (!src) return DDERR_INVALIDPARAMS;
    RECT b = sr ? *sr : (RECT){0, 0, src->w, src->h};
    RECT a = {(LONG)x, (LONG)y, (LONG)x + b.right - b.left, (LONG)y + b.bottom - b.top};
    copy(s, &a, src, &b, 0, 0);
    if (s == g_primary) present_primary();
    return DD_OK;
}
static HR STD sf_DeleteAttachedSurface(surf *s, DWORD f, surf *a) { return DD_OK; }
static HR STD sf_EnumAttachedSurfaces(surf *s, void *ctx, LPDDENUMSURFACESCALLBACK cb) {
    if (!cb) return DDERR_INVALIDPARAMS;
    if (s->primary && s->nback) {
        /* as DirectDraw: the first back buffer (it leads to the next) */
        surf *b = s->back[0];
        DDSURFACEDESC d; d.dwSize = sizeof d; fill_desc(b, &d);
        sf_AddRef(b);
        cb((LPDIRECTDRAWSURFACE)b, &d, ctx);
    } else if ((s->caps & DDSCAPS_BACKBUFFER) && s->back[1]) {
        surf *b = s->back[1];
        DDSURFACEDESC d; d.dwSize = sizeof d; fill_desc(b, &d);
        sf_AddRef(b);
        cb((LPDIRECTDRAWSURFACE)b, &d, ctx);
    }
    return DD_OK;
}
static HR STD sf_EnumOverlayZOrders(surf *s, DWORD f, void *c, void *cb) { return DD_OK; }
static HR STD sf_Flip(surf *s, surf *target, DWORD fl) {
    if (!s->primary || !s->nback) return DDERR_NOTFLIPPABLE;
    /* the front gets the first back buffer's picture: rotate the memory along the chain */
    uint8_t *front = s->mem;
    s->mem = s->back[0]->mem;
    if (s->nback == 2) { s->back[0]->mem = s->back[1]->mem; s->back[1]->mem = front; }
    else s->back[0]->mem = front;
    present_primary();
    return DD_OK;
}
static HR STD sf_GetAttachedSurface(surf *s, DDSCAPS *caps, surf **out) {
    if (!out) return DDERR_INVALIDPARAMS;
    surf *b = s->primary ? s->back[0] : ((s->caps & DDSCAPS_BACKBUFFER) ? s->back[1] : NULL);
    if (!b) return DDERR_NOTFOUND;
    sf_AddRef(b);
    *out = b;
    return DD_OK;
}
static HR STD sf_GetBltStatus(surf *s, DWORD f) { return DD_OK; }
static HR STD sf_GetCaps(surf *s, DDSCAPS *c) { if (c) c->dwCaps = s->caps; return DD_OK; }
static HR STD sf_GetClipper(surf *s, void **c) { if (c) *c = NULL; return DDERR_NOCLIPPERATTACHED; }
static HR STD sf_GetColorKey(surf *s, DWORD f, DDCOLORKEY *k) { return DDERR_NOCOLORKEY; }
static HR STD sf_GetDC(surf *s, HDC *out) {
    if (!out) return DDERR_INVALIDPARAMS;
    if (!s->dc) {
        struct { BITMAPINFOHEADER h; DWORD m[3]; } bi;
        memset(&bi, 0, sizeof bi);
        bi.h.biSize = sizeof bi.h; bi.h.biWidth = s->pitch / (s->bpp / 8); bi.h.biHeight = -s->h; bi.h.biPlanes = 1;
        bi.h.biBitCount = (WORD)s->bpp; bi.h.biCompression = s->bpp == 16 ? BI_BITFIELDS : BI_RGB;
        bi.m[0] = 0xF800; bi.m[1] = 0x07E0; bi.m[2] = 0x001F;
        s->dc = CreateCompatibleDC(NULL);
        s->dib = CreateDIBSection(s->dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &s->dib_bits, NULL, 0);
        if (!s->dib) return DDERR_GENERIC;
        s->dc_old = SelectObject(s->dc, s->dib);
    }
    memcpy(s->dib_bits, s->mem, (size_t)s->pitch * s->h);
    *out = s->dc;
    return DD_OK;
}
static HR STD sf_GetFlipStatus(surf *s, DWORD f) { return DD_OK; }
static HR STD sf_GetOverlayPosition(surf *s, LONG *x, LONG *y) { return DDERR_NOTAOVERLAYSURFACE; }
static HR STD sf_GetPalette(surf *s, pal **p) {
    if (!p) return DDERR_INVALIDPARAMS;
    if (!s->pal) return DDERR_NOPALETTEATTACHED;
    pl_AddRef(s->pal); *p = s->pal;
    return DD_OK;
}
static HR STD sf_GetPixelFormat(surf *s, DDPIXELFORMAT *pf) { if (!pf) return DDERR_INVALIDPARAMS; pixfmt(pf, s->bpp); return DD_OK; }
static HR STD sf_GetSurfaceDesc(surf *s, DDSURFACEDESC *d) { if (!d) return DDERR_INVALIDPARAMS; fill_desc(s, d); return DD_OK; }
static HR STD sf_Initialize(surf *s, void *dd, DDSURFACEDESC *d) { return DDERR_ALREADYINITIALIZED; }
static HR STD sf_IsLost(surf *s) { return DD_OK; }
static HR STD sf_Lock(surf *s, RECT *r, DDSURFACEDESC *d, DWORD fl, HANDLE ev) {
    if (!d) return DDERR_INVALIDPARAMS;
    fill_desc(s, d);
    d->dwFlags |= DDSD_LPSURFACE;
    d->lpSurface = s->mem + (r ? r->top * s->pitch + r->left * (s->bpp / 8) : 0);
    return DD_OK;
}
static HR STD sf_ReleaseDC(surf *s, HDC dc) {
    if (s->dib_bits) memcpy(s->mem, s->dib_bits, (size_t)s->pitch * s->h);
    if (s == g_primary) present_primary();
    return DD_OK;
}
static HR STD sf_Restore(surf *s) { return DD_OK; }
static HR STD sf_SetClipper(surf *s, void *c) { return DD_OK; }
static HR STD sf_SetColorKey(surf *s, DWORD f, DDCOLORKEY *k) { return DD_OK; }
static HR STD sf_SetOverlayPosition(surf *s, LONG x, LONG y) { return DDERR_NOTAOVERLAYSURFACE; }
static HR STD sf_SetPalette(surf *s, pal *p) {
    if (p) pl_AddRef(p);
    if (s->pal) pl_Release(s->pal);
    s->pal = p;
    return DD_OK;
}
static HR STD sf_Unlock(surf *s, void *r) {
    /* the intro movie draws straight onto the primary: show it */
    if (s == g_primary) present_primary();
    return DD_OK;
}
static HR STD sf_UpdateOverlay(surf *s, RECT *a, surf *d, RECT *b, DWORD f, void *fx) { return DDERR_NOTAOVERLAYSURFACE; }
static HR STD sf_UpdateOverlayDisplay(surf *s, DWORD f) { return DDERR_NOTAOVERLAYSURFACE; }
static HR STD sf_UpdateOverlayZOrder(surf *s, DWORD f, surf *r) { return DDERR_NOTAOVERLAYSURFACE; }
static void *g_surf_vt[] = {
    sf_QI, sf_AddRef, sf_Release, sf_AddAttachedSurface, sf_AddOverlayDirtyRect, sf_Blt, sf_BltBatch, sf_BltFast,
    sf_DeleteAttachedSurface, sf_EnumAttachedSurfaces, sf_EnumOverlayZOrders, sf_Flip, sf_GetAttachedSurface,
    sf_GetBltStatus, sf_GetCaps, sf_GetClipper, sf_GetColorKey, sf_GetDC, sf_GetFlipStatus, sf_GetOverlayPosition,
    sf_GetPalette, sf_GetPixelFormat, sf_GetSurfaceDesc, sf_Initialize, sf_IsLost, sf_Lock, sf_ReleaseDC, sf_Restore,
    sf_SetClipper, sf_SetColorKey, sf_SetOverlayPosition, sf_SetPalette, sf_Unlock, sf_UpdateOverlay,
    sf_UpdateOverlayDisplay, sf_UpdateOverlayZOrder};

static surf *new_surf(int w, int h, int bpp, DWORD caps) {
    surf *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *s);
    s->vtbl = g_surf_vt; s->ref = 1;
    s->w = w; s->h = h; s->bpp = bpp;
    s->pitch = ((w * (bpp / 8)) + 3) & ~3;
    s->mem = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)s->pitch * h + 64);
    s->caps = caps;
    return s;
}

/* ---------------- IDirectDraw ---------------- */
static void dd_free(dd_obj *d) { if (g_dd == d) g_dd = NULL; HeapFree(GetProcessHeap(), 0, d); }
REF_FUNCS(dd_obj, dd, dd_free)
static HR STD dd_QI(dd_obj *d, REFIID iid, void **out) {
    if (!out) return DDERR_INVALIDPARAMS;
    *out = NULL;
    if (same(iid, &k_IID_DD) || same(iid, &k_IID_IUnknown)) { dd_AddRef(d); *out = d; return DD_OK; }
    dsp_log("IDirectDraw::QueryInterface for an unknown interface %08lX", iid ? iid->Data1 : 0);
    return E_NOINTERFACE;
}
static HR STD dd_Compact(dd_obj *d) { return DD_OK; }
static HR STD dd_CreateClipper(dd_obj *d, DWORD f, clip **out, void *o) {
    if (!out) return DDERR_INVALIDPARAMS;
    clip *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *c);
    c->vtbl = g_clip_vt; c->ref = 1;
    *out = c;
    return DD_OK;
}
static HR STD dd_CreatePalette(dd_obj *d, DWORD f, PALETTEENTRY *e, pal **out, void *o) {
    if (!out) return DDERR_INVALIDPARAMS;
    pal *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *p);
    p->vtbl = g_pal_vt; p->ref = 1;
    if (e) memcpy(p->e, e, sizeof p->e);
    *out = p;
    return DD_OK;
}
static HR STD dd_CreateSurface(dd_obj *d, DDSURFACEDESC *desc, surf **out, void *o) {
    if (!desc || !out) return DDERR_INVALIDPARAMS;
    DWORD caps = (desc->dwFlags & DDSD_CAPS) ? desc->ddsCaps.dwCaps : 0;
    if (caps & DDSCAPS_PRIMARYSURFACE) {
        int w = d->w ? d->w : 640, h = d->h ? d->h : 480, bpp = d->bpp ? d->bpp : 16;
        surf *p = new_surf(w, h, bpp, DDSCAPS_PRIMARYSURFACE | DDSCAPS_FRONTBUFFER | DDSCAPS_VISIBLE |
                                          DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM | (caps & (DDSCAPS_FLIP | DDSCAPS_COMPLEX)));
        p->primary = 1;
        int nb = (desc->dwFlags & DDSD_BACKBUFFERCOUNT) ? (int)desc->dwBackBufferCount : 0;
        if (nb > 2) nb = 2;
        for (int i = 0; i < nb; i++) {
            surf *b = new_surf(w, h, bpp, DDSCAPS_BACKBUFFER | DDSCAPS_FLIP | DDSCAPS_COMPLEX | DDSCAPS_VIDEOMEMORY |
                                              DDSCAPS_LOCALVIDMEM);
            b->back[0] = p;
            p->back[i] = b;
        }
        if (nb == 2) p->back[0]->back[1] = p->back[1];
        p->nback = nb;
        g_primary = p;
        *out = p;
        dsp_log("primary %dx%dx%d, %d back buffer(s)", w, h, bpp, nb);
        return DD_OK;
    }
    int w = (desc->dwFlags & DDSD_WIDTH) ? (int)desc->dwWidth : d->w;
    int h = (desc->dwFlags & DDSD_HEIGHT) ? (int)desc->dwHeight : d->h;
    int bpp = (desc->dwFlags & DDSD_PIXELFORMAT) ? (int)desc->ddpfPixelFormat.dwRGBBitCount : (d->bpp ? d->bpp : 16);
    if (w <= 0 || h <= 0) return DDERR_INVALIDPARAMS;
    if (bpp != 8 && bpp != 16 && bpp != 32) bpp = 16;
    *out = new_surf(w, h, bpp, (caps ? caps : DDSCAPS_OFFSCREENPLAIN) | DDSCAPS_SYSTEMMEMORY);
    dsp_log("surface %dx%dx%d caps %08lX", w, h, bpp, caps);
    return DD_OK;
}
static HR STD dd_DuplicateSurface(dd_obj *d, surf *s, surf **out) { return DDERR_UNSUPPORTED; }
static const int k_modes[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1280, 960},
                                 {1280, 1024}, {1360, 768}, {1440, 900}, {1600, 900}, {1600, 1200}, {1680, 1050},
                                 {1920, 1080}, {1920, 1200}, {2560, 1080}, {2560, 1440}, {2560, 1600},
                                 {3440, 1440}, {3840, 2160}};
static HR STD dd_EnumDisplayModes(dd_obj *d, DWORD f, DDSURFACEDESC *filter, void *ctx, LPDDENUMMODESCALLBACK cb) {
    if (!cb) return DDERR_INVALIDPARAMS;
    static const int bpps[] = {8, 16};
    for (int b = 0; b < 2; b++)
        for (unsigned i = 0; i < sizeof k_modes / sizeof k_modes[0]; i++) {
            DDSURFACEDESC m;
            memset(&m, 0, sizeof m);
            m.dwSize = sizeof m;
            m.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
            m.dwWidth = k_modes[i][0]; m.dwHeight = k_modes[i][1]; m.lPitch = m.dwWidth * bpps[b] / 8;
            m.dwRefreshRate = 60;
            pixfmt(&m.ddpfPixelFormat, bpps[b]);
            if (filter && (filter->dwFlags & DDSD_WIDTH) && filter->dwWidth != m.dwWidth) continue;
            if (filter && (filter->dwFlags & DDSD_HEIGHT) && filter->dwHeight != m.dwHeight) continue;
            if (cb(&m, ctx) == DDENUMRET_CANCEL) return DD_OK;
        }
    return DD_OK;
}
static HR STD dd_EnumSurfaces(dd_obj *d, DWORD f, DDSURFACEDESC *s, void *c, void *cb) { return DD_OK; }
static HR STD dd_FlipToGDISurface(dd_obj *d) { return DD_OK; }
static HR STD dd_GetCaps(dd_obj *d, DDCAPS *hw, DDCAPS *hel) {
    DDCAPS *c[2] = {hw, hel};
    for (int i = 0; i < 2; i++) {
        if (!c[i]) continue;
        DWORD size = c[i]->dwSize ? c[i]->dwSize : sizeof(DDCAPS);
        memset(c[i], 0, size);
        c[i]->dwSize = size;
        /* blits and colour fills, no 3D (the game prefers such a device), gamma on the primary */
        c[i]->dwCaps = DDCAPS_BLT | DDCAPS_BLTCOLORFILL | DDCAPS_BLTSTRETCH | DDCAPS_COLORKEY | DDCAPS_PALETTE;
        c[i]->dwCaps2 = DDCAPS2_PRIMARYGAMMA | DDCAPS2_WIDESURFACES;
        c[i]->dwPalCaps = DDPCAPS_8BIT;
        c[i]->ddsCaps.dwCaps = DDSCAPS_BACKBUFFER | DDSCAPS_COMPLEX | DDSCAPS_FLIP | DDSCAPS_FRONTBUFFER |
                               DDSCAPS_OFFSCREENPLAIN | DDSCAPS_PRIMARYSURFACE | DDSCAPS_SYSTEMMEMORY |
                               DDSCAPS_VIDEOMEMORY | DDSCAPS_VISIBLE;
        c[i]->dwVidMemTotal = c[i]->dwVidMemFree = 256u << 20;
    }
    return DD_OK;
}
static HR STD dd_GetDisplayMode(dd_obj *d, DDSURFACEDESC *m) {
    if (!m) return DDERR_INVALIDPARAMS;
    DWORD size = m->dwSize ? m->dwSize : sizeof *m;
    memset(m, 0, size < sizeof *m ? size : sizeof *m);
    m->dwSize = size;
    m->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
    m->dwWidth = d->w ? d->w : 1024; m->dwHeight = d->h ? d->h : 768;
    int bpp = d->bpp ? d->bpp : 16;
    m->lPitch = m->dwWidth * bpp / 8; m->dwRefreshRate = 60;
    pixfmt(&m->ddpfPixelFormat, bpp);
    return DD_OK;
}
static HR STD dd_GetFourCCCodes(dd_obj *d, DWORD *n, DWORD *c) { if (n) *n = 0; return DD_OK; }
static HR STD dd_GetGDISurface(dd_obj *d, surf **out) {
    if (!out || !g_primary) return DDERR_NOTFOUND;
    sf_AddRef(g_primary); *out = g_primary;
    return DD_OK;
}
static HR STD dd_GetMonitorFrequency(dd_obj *d, DWORD *f) { if (f) *f = 60; return DD_OK; }
static HR STD dd_GetScanLine(dd_obj *d, DWORD *l) { if (l) *l = 0; return DD_OK; }
static HR STD dd_GetVerticalBlankStatus(dd_obj *d, BOOL *b) { if (b) *b = TRUE; return DD_OK; }
static HR STD dd_Initialize(dd_obj *d, GUID *g) { return DDERR_ALREADYINITIALIZED; }
static HR STD dd_RestoreDisplayMode(dd_obj *d) { return DD_OK; }
static HR STD dd_SetCooperativeLevel(dd_obj *d, HWND h, DWORD f) {
    d->hwnd = h; d->coop = f;
    dsp_log("SetCooperativeLevel(hwnd %p, flags %08lX)", h, f);
    return DD_OK;
}
static HR STD dd_SetDisplayMode(dd_obj *d, DWORD w, DWORD h, DWORD bpp) {
    d->w = w; d->h = h; d->bpp = bpp;
    dsp_log("SetDisplayMode(%lu x %lu x %lu)", w, h, bpp);
    if (d->hwnd) dsp_window(d->hwnd, w, h);
    return DD_OK;
}
static HR STD dd_WaitForVerticalBlank(dd_obj *d, DWORD f, HANDLE e) { return DD_OK; }
static void *g_dd_vt[] = {
    dd_QI, dd_AddRef, dd_Release, dd_Compact, dd_CreateClipper, dd_CreatePalette, dd_CreateSurface,
    dd_DuplicateSurface, dd_EnumDisplayModes, dd_EnumSurfaces, dd_FlipToGDISurface, dd_GetCaps, dd_GetDisplayMode,
    dd_GetFourCCCodes, dd_GetGDISurface, dd_GetMonitorFrequency, dd_GetScanLine, dd_GetVerticalBlankStatus,
    dd_Initialize, dd_RestoreDisplayMode, dd_SetCooperativeLevel, dd_SetDisplayMode, dd_WaitForVerticalBlank};

/* ---------------- exports ---------------- */
static int g_gog = -1;
static HMODULE g_gog_dll;
static FARPROC gog(const char *name) {
    if (!g_gog_dll) {
        char p[MAX_PATH];
        GetModuleFileNameA(NULL, p, MAX_PATH);
        char *s = strrchr(p, '\\');
        if (s) s[1] = 0;
        strcat(p, "ddraw_gog.dll");
        g_gog_dll = LoadLibraryA(p);
        dsp_log("renderer = gog: %s %s", p, g_gog_dll ? "loaded" : "NOT FOUND");
    }
    return g_gog_dll ? GetProcAddress(g_gog_dll, name) : NULL;
}
static int use_gog(void) {
    if (g_gog < 0) {
        char r[32];
        dsp_cfg_str("renderer", "d3d11", r, sizeof r);
        g_gog = !_stricmp(r, "gog");
    }
    return g_gog;
}

HR STD dsp_DirectDrawCreate(GUID *g, void **out, void *outer) {
    if (use_gog()) { HR (STD *f)(GUID *, void **, void *) = (void *)gog("DirectDrawCreate"); return f ? f(g, out, outer) : DDERR_GENERIC; }
    if (!out) return DDERR_INVALIDPARAMS;
    dd_obj *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *d);
    d->vtbl = g_dd_vt; d->ref = 1;
    if (g_dd) { d->w = g_dd->w; d->h = g_dd->h; d->bpp = g_dd->bpp; d->hwnd = g_dd->hwnd; }
    g_dd = d;
    *out = d;
    return DD_OK;
}
HR STD dsp_DirectDrawEnumerateA(LPDDENUMCALLBACKA cb, void *ctx) {
    if (use_gog()) { HR (STD *f)(void *, void *) = (void *)gog("DirectDrawEnumerateA"); return f ? f(cb, ctx) : DDERR_GENERIC; }
    if (cb) cb(NULL, "Primary Display Driver", "display", ctx);
    return DD_OK;
}
HR STD dsp_DirectDrawCreateEx(GUID *g, void **out, REFIID iid, void *outer) {
    if (use_gog()) { HR (STD *f)(GUID *, void **, REFIID, void *) = (void *)gog("DirectDrawCreateEx"); return f ? f(g, out, iid, outer) : DDERR_GENERIC; }
    return DDERR_UNSUPPORTED;
}
HR STD dsp_DirectDrawEnumerateExA(LPDDENUMCALLBACKEXA cb, void *ctx, DWORD fl) {
    if (use_gog()) { HR (STD *f)(void *, void *, DWORD) = (void *)gog("DirectDrawEnumerateExA"); return f ? f(cb, ctx, fl) : DDERR_GENERIC; }
    if (cb) cb(NULL, "Primary Display Driver", "display", ctx, NULL);
    return DD_OK;
}
HR STD dsp_DirectDrawCreateClipper(DWORD fl, void **out, void *outer) {
    if (use_gog()) { HR (STD *f)(DWORD, void **, void *) = (void *)gog("DirectDrawCreateClipper"); return f ? f(fl, out, outer) : DDERR_GENERIC; }
    return dd_CreateClipper(NULL, fl, (clip **)out, outer);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        SetProcessDPIAware(); /* real pixels: no blurry stretching by Windows at 125% / 150% scaling */
        dsp_log("Nox Modernized display (ddraw.dll), renderer %s", use_gog() ? "gog" : "d3d11");
    } else if (reason == DLL_PROCESS_DETACH) {
        dsp_shutdown();
    }
    return TRUE;
}
