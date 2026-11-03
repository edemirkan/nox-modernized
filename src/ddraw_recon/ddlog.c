/* Recon ddraw.dll: passes every DirectDraw call to GOG's wrapper (ddraw_gog.dll next to it) and logs what the game
   does - interfaces, cooperative level, display mode, surfaces and their formats, locks, blits, flips - into
   ddraw-recon.log. Only for finding out how Nox presents its frames; it changes nothing. */
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

typedef HRESULT (WINAPI *create_fn)(GUID *, void **, void *);
static HMODULE g_real;
static FILE *g_log;
static CRITICAL_SECTION g_cs;
static DWORD g_start, g_last_summary;

static void lg(const char *fmt, ...) {
    if (!g_log) {
        char p[MAX_PATH];
        GetModuleFileNameA(NULL, p, MAX_PATH);
        char *s = strrchr(p, '\\');
        if (s) s[1] = 0;
        strcat(p, "ddraw-recon.log");
        g_log = fopen(p, "a");
        if (!g_log) return;
        fprintf(g_log, "\n======== ddraw recon session (pid %lu) ========\n", GetCurrentProcessId());
    }
    fprintf(g_log, "[%7lu] ", GetTickCount() - g_start);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

/* ---- wrappers ---- */
enum { K_DD, K_SURF, K_PAL, K_CLIP, K_OTHER, K_N };
static const char *k_kind[K_N] = {"DD", "Surf", "Pal", "Clip", "Obj"};
#define MAGIC 0x5752444Eu
typedef struct wrap { void **vtbl; void *real; int kind; uint32_t magic; int id; } wrap;
extern void *ddw_thunks[64];
static void *g_vt[K_N][64];
static unsigned g_count[K_N][64];
static int g_next_id = 1;

static const char *dd_names[] = {"QueryInterface","AddRef","Release","Compact","CreateClipper","CreatePalette",
    "CreateSurface","DuplicateSurface","EnumDisplayModes","EnumSurfaces","FlipToGDISurface","GetCaps",
    "GetDisplayMode","GetFourCCCodes","GetGDISurface","GetMonitorFrequency","GetScanLine","GetVerticalBlankStatus",
    "Initialize","RestoreDisplayMode","SetCooperativeLevel","SetDisplayMode","WaitForVerticalBlank",
    "GetAvailableVidMem","GetSurfaceFromDC","RestoreAllSurfaces","TestCooperativeLevel","GetDeviceIdentifier",
    "StartModeTest","EvaluateMode"};
static const char *surf_names[] = {"QueryInterface","AddRef","Release","AddAttachedSurface","AddOverlayDirtyRect",
    "Blt","BltBatch","BltFast","DeleteAttachedSurface","EnumAttachedSurfaces","EnumOverlayZOrders","Flip",
    "GetAttachedSurface","GetBltStatus","GetCaps","GetClipper","GetColorKey","GetDC","GetFlipStatus",
    "GetOverlayPosition","GetPalette","GetPixelFormat","GetSurfaceDesc","Initialize","IsLost","Lock","ReleaseDC",
    "Restore","SetClipper","SetColorKey","SetOverlayPosition","SetPalette","Unlock","UpdateOverlay",
    "UpdateOverlayDisplay","UpdateOverlayZOrder","GetDDInterface","PageLock","PageUnlock","SetSurfaceDesc",
    "SetPrivateData","GetPrivateData","FreePrivateData","GetUniquenessValue","ChangeUniquenessValue","SetPriority",
    "GetPriority","SetLOD","GetLOD"};
static const char *pal_names[] = {"QueryInterface","AddRef","Release","GetCaps","GetEntries","Initialize","SetEntries"};
static const char *clip_names[] = {"QueryInterface","AddRef","Release","GetClipList","GetHWnd","Initialize",
    "IsClipListChanged","SetClipList","SetHWnd"};

static const char *mname(int kind, int i) {
    static char b[16];
    const char **t = NULL; int n = 0;
    if (kind == K_DD) { t = dd_names; n = sizeof dd_names / sizeof *dd_names; }
    if (kind == K_SURF) { t = surf_names; n = sizeof surf_names / sizeof *surf_names; }
    if (kind == K_PAL) { t = pal_names; n = sizeof pal_names / sizeof *pal_names; }
    if (kind == K_CLIP) { t = clip_names; n = sizeof clip_names / sizeof *clip_names; }
    if (t && i < n) return t[i];
    sprintf(b, "#%d", i);
    return b;
}

static int verbose(int kind, int i) { return ++g_count[kind][i] <= 200 || (g_count[kind][i] % 1000) == 0; }

static void summary(void) {
    lg("---- call counts ----");
    for (int k = 0; k < K_N; k++)
        for (int i = 0; i < 64; i++)
            if (g_count[k][i]) lg("  %s.%s: %u", k_kind[k], mname(k, i), g_count[k][i]);
}

static void *unwrap(void *p) {
    wrap *w = p;
    return (w && !IsBadReadPtr(w, sizeof *w) && w->magic == MAGIC) ? w->real : p;
}

static wrap *g_all[512];
static int g_nall;
static void *wrapit(void *real, int kind) {
    if (!real) return NULL;
    for (int i = 0; i < g_nall; i++) if (g_all[i] && g_all[i]->real == real) return g_all[i];
    wrap *w = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *w);
    w->vtbl = g_vt[kind]; w->real = real; w->kind = kind; w->magic = MAGIC; w->id = g_next_id++;
    if (g_nall < 512) g_all[g_nall++] = w;
    lg("new %s#%d (real %p)", k_kind[kind], w->id, real);
    return w;
}

/* generic: log, then the thunk jumps into the real method */
void __cdecl ddw_note(uint32_t *sp) {
    EnterCriticalSection(&g_cs);
    int i = sp[0];
    wrap *w = (wrap *)sp[2];
    uint32_t *a = sp + 3;
    if (verbose(w->kind, i))
        lg("%s#%d.%s(%08X %08X %08X %08X %08X) from %08X", k_kind[w->kind], w->id, mname(w->kind, i),
           a[0], a[1], a[2], a[3], a[4], sp[1]);
    DWORD now = GetTickCount();
    if (now - g_last_summary > 15000) { g_last_summary = now; summary(); }
    LeaveCriticalSection(&g_cs);
}

#define REAL(w) ((void *)((wrap *)(w))->real)
#define VT(p) (*(void ***)(p))
typedef HRESULT (WINAPI *f1)(void *, uintptr_t);
typedef HRESULT (WINAPI *f2)(void *, uintptr_t, uintptr_t);
typedef HRESULT (WINAPI *f3)(void *, uintptr_t, uintptr_t, uintptr_t);
typedef HRESULT (WINAPI *f4)(void *, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef HRESULT (WINAPI *f5)(void *, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
#define CALL1(w, i, a) ((f1)VT(REAL(w))[i])(REAL(w), (uintptr_t)(a))
#define CALL2(w, i, a, b) ((f2)VT(REAL(w))[i])(REAL(w), (uintptr_t)(a), (uintptr_t)(b))
#define CALL3(w, i, a, b, c) ((f3)VT(REAL(w))[i])(REAL(w), (uintptr_t)(a), (uintptr_t)(b), (uintptr_t)(c))
#define CALL4(w, i, a, b, c, d) ((f4)VT(REAL(w))[i])(REAL(w), (uintptr_t)(a), (uintptr_t)(b), (uintptr_t)(c), (uintptr_t)(d))
#define CALL5(w, i, a, b, c, d, e) ((f5)VT(REAL(w))[i])(REAL(w), (uintptr_t)(a), (uintptr_t)(b), (uintptr_t)(c), (uintptr_t)(d), (uintptr_t)(e))

static void guidstr(const GUID *g, char *o) {
    if (!g) { strcpy(o, "null"); return; }
    sprintf(o, "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", g->Data1, g->Data2, g->Data3, g->Data4[0],
            g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

static void desc_str(const DDSURFACEDESC *d, char *o) {
    if (!d) { strcpy(o, "null"); return; }
    sprintf(o, "size %lu flags %08lX caps %08lX %lux%lu pitch %ld backbuf %lu surface %p pf{flags %08lX fourcc %08lX "
               "bits %lu masks %08lX %08lX %08lX %08lX}",
            d->dwSize, d->dwFlags, d->ddsCaps.dwCaps, d->dwWidth, d->dwHeight, d->lPitch, d->dwBackBufferCount,
            d->lpSurface, d->ddpfPixelFormat.dwFlags, d->ddpfPixelFormat.dwFourCC, d->ddpfPixelFormat.dwRGBBitCount,
            d->ddpfPixelFormat.dwRBitMask, d->ddpfPixelFormat.dwGBitMask, d->ddpfPixelFormat.dwBBitMask,
            d->ddpfPixelFormat.dwRGBAlphaBitMask);
}

static int kind_for_iid(const GUID *g, int from) {
    if (!g) return K_OTHER;
    /* IDirectDraw* : 6C14DB80, B3A6F3E0 (2), 9C59509A (4), 15E65EC0 (7); surfaces 6C14DB81, 57805885 (2),
       DA044E00 (3), 0B2B8630 (4), 06675A80 (7) */
    switch (g->Data1) {
    case 0x6C14DB80: case 0xB3A6F3E0: case 0x9C59509A: case 0x15E65EC0: return K_DD;
    case 0x6C14DB81: case 0x57805885: case 0xDA044E00: case 0x0B2B8630: case 0x06675A80: return K_SURF;
    case 0x6C14DB84: return K_PAL;
    case 0x6C14DB85: return K_CLIP;
    }
    (void)from;
    return K_OTHER;
}

/* ---- specials (interfaces in / out) ---- */
static HRESULT WINAPI w_QI(wrap *w, REFIID iid, void **out) {
    HRESULT hr = CALL2(w, 0, iid, out);
    char g[64]; guidstr(iid, g);
    if (SUCCEEDED(hr) && out && *out) *out = wrapit(*out, kind_for_iid(iid, w->kind));
    lg("%s#%d.QueryInterface(%s) = %08lX", k_kind[w->kind], w->id, g, hr);
    return hr;
}
static ULONG WINAPI w_Release(wrap *w) {
    ULONG r = ((ULONG (WINAPI *)(void *))VT(w->real)[2])(w->real);
    if (verbose(w->kind, 2)) lg("%s#%d.Release = %lu", k_kind[w->kind], w->id, r);
    if (w->kind == K_DD && r == 0) summary();
    return r;
}
static HRESULT WINAPI dd_CreateSurface(wrap *w, DDSURFACEDESC *d, void **out, void *outer) {
    char s[400]; desc_str(d, s);
    HRESULT hr = CALL3(w, 6, d, out, outer);
    if (SUCCEEDED(hr) && out) *out = wrapit(*out, K_SURF);
    lg("DD#%d.CreateSurface(%s) = %08lX -> Surf#%d", w->id, s, hr, (SUCCEEDED(hr) && out && *out) ? ((wrap *)*out)->id : 0);
    return hr;
}
static HRESULT WINAPI dd_CreatePalette(wrap *w, DWORD flags, PALETTEENTRY *e, void **out, void *outer) {
    HRESULT hr = CALL4(w, 5, flags, e, out, outer);
    if (SUCCEEDED(hr) && out) *out = wrapit(*out, K_PAL);
    lg("DD#%d.CreatePalette(flags %08lX) = %08lX", w->id, flags, hr);
    return hr;
}
static HRESULT WINAPI dd_CreateClipper(wrap *w, DWORD flags, void **out, void *outer) {
    HRESULT hr = CALL3(w, 4, flags, out, outer);
    if (SUCCEEDED(hr) && out) *out = wrapit(*out, K_CLIP);
    lg("DD#%d.CreateClipper(%08lX) = %08lX", w->id, flags, hr);
    return hr;
}
static HRESULT WINAPI dd_GetGDISurface(wrap *w, void **out) {
    HRESULT hr = CALL1(w, 14, out);
    if (SUCCEEDED(hr) && out) *out = wrapit(*out, K_SURF);
    lg("DD#%d.GetGDISurface = %08lX", w->id, hr);
    return hr;
}
static HRESULT WINAPI dd_DuplicateSurface(wrap *w, void *src, void **out) {
    HRESULT hr = CALL2(w, 7, unwrap(src), out);
    if (SUCCEEDED(hr) && out) *out = wrapit(*out, K_SURF);
    lg("DD#%d.DuplicateSurface = %08lX", w->id, hr);
    return hr;
}
static HRESULT WINAPI dd_SetCooperativeLevel(wrap *w, HWND h, DWORD flags) {
    HRESULT hr = CALL2(w, 20, h, flags);
    lg("DD#%d.SetCooperativeLevel(hwnd %p, flags %08lX) = %08lX", w->id, h, flags, hr);
    return hr;
}
static HRESULT WINAPI dd_SetDisplayMode(wrap *w, DWORD wd, DWORD ht, DWORD bpp) {
    /* IDirectDraw (v1, what Nox uses): 3 arguments */
    HRESULT hr = CALL3(w, 21, wd, ht, bpp);
    lg("DD#%d.SetDisplayMode(%lu x %lu x %lu) = %08lX", w->id, wd, ht, bpp, hr);
    return hr;
}

static HRESULT WINAPI s_Blt(wrap *w, RECT *dr, void *src, RECT *sr, DWORD flags, DDBLTFX *fx) {
    HRESULT hr = CALL5(w, 5, dr, unwrap(src), sr, flags, fx);
    if (verbose(K_SURF, 5))
        lg("Surf#%d.Blt(dst %ld,%ld-%ld,%ld src Surf#%d %ld,%ld-%ld,%ld flags %08lX fill %08lX) = %08lX", w->id,
           dr ? dr->left : -1, dr ? dr->top : -1, dr ? dr->right : -1, dr ? dr->bottom : -1,
           src ? ((wrap *)src)->magic == MAGIC ? ((wrap *)src)->id : -1 : 0, sr ? sr->left : -1, sr ? sr->top : -1,
           sr ? sr->right : -1, sr ? sr->bottom : -1, flags, fx ? fx->dwFillColor : 0, hr);
    return hr;
}
static HRESULT WINAPI s_BltFast(wrap *w, DWORD x, DWORD y, void *src, RECT *sr, DWORD flags) {
    HRESULT hr = CALL5(w, 7, x, y, unwrap(src), sr, flags);
    if (verbose(K_SURF, 7))
        lg("Surf#%d.BltFast(%lu,%lu src Surf#%d %ld,%ld-%ld,%ld flags %08lX) = %08lX", w->id, x, y,
           src && ((wrap *)src)->magic == MAGIC ? ((wrap *)src)->id : -1, sr ? sr->left : -1, sr ? sr->top : -1,
           sr ? sr->right : -1, sr ? sr->bottom : -1, flags, hr);
    return hr;
}
static HRESULT WINAPI s_Flip(wrap *w, void *target, DWORD flags) {
    HRESULT hr = CALL2(w, 11, unwrap(target), flags);
    if (verbose(K_SURF, 11)) lg("Surf#%d.Flip(target %p, flags %08lX) = %08lX", w->id, target, flags, hr);
    return hr;
}
static HRESULT WINAPI s_GetAttachedSurface(wrap *w, DDSCAPS *caps, void **out) {
    HRESULT hr = CALL2(w, 12, caps, out);
    if (SUCCEEDED(hr) && out) *out = wrapit(*out, K_SURF);
    lg("Surf#%d.GetAttachedSurface(caps %08lX) = %08lX", w->id, caps ? caps->dwCaps : 0, hr);
    return hr;
}
typedef HRESULT (WINAPI *enum_cb)(void *, DDSURFACEDESC *, void *);
static enum_cb g_enum_cb;
static HRESULT WINAPI enum_tramp(void *surf, DDSURFACEDESC *d, void *ctx) {
    char s[400]; desc_str(d, s);
    void *w = wrapit(surf, K_SURF);
    lg("  attached: Surf#%d %s", ((wrap *)w)->id, s);
    return g_enum_cb(w, d, ctx);
}
static HRESULT WINAPI s_EnumAttachedSurfaces(wrap *w, void *ctx, enum_cb cb) {
    g_enum_cb = cb;
    lg("Surf#%d.EnumAttachedSurfaces", w->id);
    return CALL2(w, 9, ctx, enum_tramp);
}
static HRESULT WINAPI s_AddAttachedSurface(wrap *w, void *s) { lg("Surf#%d.AddAttachedSurface", w->id); return CALL1(w, 3, unwrap(s)); }
static HRESULT WINAPI s_DeleteAttachedSurface(wrap *w, DWORD f, void *s) { lg("Surf#%d.DeleteAttachedSurface", w->id); return CALL2(w, 8, f, unwrap(s)); }
static HRESULT WINAPI s_SetClipper(wrap *w, void *c) { lg("Surf#%d.SetClipper", w->id); return CALL1(w, 28, unwrap(c)); }
static HRESULT WINAPI s_SetPalette(wrap *w, void *p) { lg("Surf#%d.SetPalette", w->id); return CALL1(w, 31, unwrap(p)); }
static HRESULT WINAPI s_UpdateOverlay(wrap *w, RECT *a, void *s, RECT *b, DWORD f, void *fx) { return CALL5(w, 33, a, unwrap(s), b, f, fx); }
static HRESULT WINAPI s_UpdateOverlayZOrder(wrap *w, DWORD f, void *s) { return CALL2(w, 35, f, unwrap(s)); }
static HRESULT WINAPI s_Lock(wrap *w, RECT *r, DDSURFACEDESC *d, DWORD flags, HANDLE ev) {
    HRESULT hr = CALL4(w, 25, r, d, flags, ev);
    if (verbose(K_SURF, 25)) {
        char s[400]; desc_str(SUCCEEDED(hr) ? d : NULL, s);
        lg("Surf#%d.Lock(rect %p flags %08lX) = %08lX %s", w->id, r, flags, hr, s);
    }
    return hr;
}
static HRESULT WINAPI s_GetSurfaceDesc(wrap *w, DDSURFACEDESC *d) {
    HRESULT hr = CALL1(w, 22, d);
    if (verbose(K_SURF, 22)) { char s[400]; desc_str(SUCCEEDED(hr) ? d : NULL, s); lg("Surf#%d.GetSurfaceDesc = %08lX %s", w->id, hr, s); }
    return hr;
}
static HRESULT WINAPI s_GetDC(wrap *w, HDC *dc) {
    HRESULT hr = CALL1(w, 17, dc);
    if (verbose(K_SURF, 17)) lg("Surf#%d.GetDC = %08lX", w->id, hr);
    return hr;
}

static void init_vtables(void) {
    for (int k = 0; k < K_N; k++) {
        for (int i = 0; i < 64; i++) g_vt[k][i] = ddw_thunks[i];
        g_vt[k][0] = (void *)w_QI;
        g_vt[k][2] = (void *)w_Release;
    }
    void **d = g_vt[K_DD];
    d[4] = (void *)dd_CreateClipper; d[5] = (void *)dd_CreatePalette; d[6] = (void *)dd_CreateSurface;
    d[7] = (void *)dd_DuplicateSurface; d[14] = (void *)dd_GetGDISurface; d[20] = (void *)dd_SetCooperativeLevel;
    d[21] = (void *)dd_SetDisplayMode;
    void **s = g_vt[K_SURF];
    s[3] = (void *)s_AddAttachedSurface; s[5] = (void *)s_Blt; s[7] = (void *)s_BltFast;
    s[8] = (void *)s_DeleteAttachedSurface; s[9] = (void *)s_EnumAttachedSurfaces; s[11] = (void *)s_Flip;
    s[12] = (void *)s_GetAttachedSurface; s[17] = (void *)s_GetDC; s[22] = (void *)s_GetSurfaceDesc;
    s[25] = (void *)s_Lock; s[28] = (void *)s_SetClipper; s[31] = (void *)s_SetPalette;
    s[33] = (void *)s_UpdateOverlay; s[35] = (void *)s_UpdateOverlayZOrder;
}

static FARPROC real_proc(const char *name) {
    if (!g_real) {
        char p[MAX_PATH];
        GetModuleFileNameA(NULL, p, MAX_PATH); /* next to Game.exe */
        char *s = strrchr(p, '\\');
        if (s) s[1] = 0;
        strcat(p, "ddraw_gog.dll");
        g_real = LoadLibraryA(p);
        if (!g_real) {
            GetSystemDirectoryA(p, MAX_PATH);
            strcat(p, "\\ddraw.dll");
            g_real = LoadLibraryA(p);
        }
        lg("real ddraw: %s (%p)", p, g_real);
    }
    return g_real ? GetProcAddress(g_real, name) : NULL;
}

HRESULT WINAPI ddw_DirectDrawCreate(GUID *g, void **out, void *outer) {
    char gs[64]; guidstr(g, gs);
    create_fn f = (create_fn)real_proc("DirectDrawCreate");
    HRESULT hr = f ? f(g, out, outer) : E_FAIL;
    if (SUCCEEDED(hr) && out) *out = wrapit(*out, K_DD);
    lg("DirectDrawCreate(driver %s) = %08lX", gs, hr);
    return hr;
}
HRESULT WINAPI ddw_DirectDrawEnumerateA(void *cb, void *ctx) {
    HRESULT (WINAPI *f)(void *, void *) = (void *)real_proc("DirectDrawEnumerateA");
    HRESULT hr = f ? f(cb, ctx) : E_FAIL;
    lg("DirectDrawEnumerateA = %08lX", hr);
    return hr;
}
HRESULT WINAPI ddw_DirectDrawCreateEx(GUID *g, void **out, REFIID iid, void *outer) {
    HRESULT (WINAPI *f)(GUID *, void **, REFIID, void *) = (void *)real_proc("DirectDrawCreateEx");
    HRESULT hr = f ? f(g, out, iid, outer) : E_FAIL;
    if (SUCCEEDED(hr) && out) *out = wrapit(*out, K_DD);
    lg("DirectDrawCreateEx = %08lX", hr);
    return hr;
}
HRESULT WINAPI ddw_DirectDrawEnumerateExA(void *cb, void *ctx, DWORD fl) {
    HRESULT (WINAPI *f)(void *, void *, DWORD) = (void *)real_proc("DirectDrawEnumerateExA");
    return f ? f(cb, ctx, fl) : E_FAIL;
}
HRESULT WINAPI ddw_DirectDrawCreateClipper(DWORD fl, void **out, void *outer) {
    HRESULT (WINAPI *f)(DWORD, void **, void *) = (void *)real_proc("DirectDrawCreateClipper");
    HRESULT hr = f ? f(fl, out, outer) : E_FAIL;
    if (SUCCEEDED(hr) && out) *out = wrapit(*out, K_CLIP);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&g_cs);
        g_start = g_last_summary = GetTickCount();
        init_vtables();
        lg("ddraw recon loaded");
    } else if (reason == DLL_PROCESS_DETACH) {
        summary();
        lg("process exit");
    }
    return TRUE;
}
