/* DirectInput wrapper: Game.exe's DirectInputCreateA call (its import stub at 0x56CE60) is redirected to
   onc_DirectInputCreateA, which creates the real DirectInput and hands the game wrappers around it; the mouse
   and keyboard wrappers add the gamepad's events to what the game reads (GetDeviceData). Everything else is
   forwarded unchanged. (The same code also works as a dinput.dll proxy: loader/dinput.def.) */
#include "onc.h"
#include <string.h>

static HMODULE g_real;
typedef HRESULT (WINAPI *create_fn)(HINSTANCE, DWORD, void **, IUnknown *);

static int load_real(void) {
    if (g_real) return 1;
    char p[MAX_PATH];
    GetSystemDirectoryA(p, MAX_PATH);
    strncat(p, "\\dinput.dll", MAX_PATH - strlen(p) - 1);
    g_real = LoadLibraryA(p);
    if (!g_real) onc_log("cannot load the real dinput.dll from %s (error %lu)", p, GetLastError());
    return g_real != NULL;
}

/* COM calls through a vtable slot */
#define VT(obj) (*(void ***)(obj))
typedef uintptr_t U;
typedef HRESULT (WINAPI *f0)(void *);
typedef HRESULT (WINAPI *f1)(void *, U);
typedef HRESULT (WINAPI *f2)(void *, U, U);
typedef HRESULT (WINAPI *f3)(void *, U, U, U);
typedef HRESULT (WINAPI *f4)(void *, U, U, U, U);
#define CALL0(o, i)             ((f0)VT(o)[i])(o)
#define CALL1(o, i, a)          ((f1)VT(o)[i])(o, (U)(a))
#define CALL2(o, i, a, b)       ((f2)VT(o)[i])(o, (U)(a), (U)(b))
#define CALL3(o, i, a, b, c)    ((f3)VT(o)[i])(o, (U)(a), (U)(b), (U)(c))
#define CALL4(o, i, a, b, c, d) ((f4)VT(o)[i])(o, (U)(a), (U)(b), (U)(c), (U)(d))

/* ---- device wrapper (IDirectInputDeviceA / 2A / 7A) ---- */

enum { DEV_OTHER, DEV_MOUSE, DEV_KEYBOARD };

typedef struct {
    void **vtbl;
    LONG   ref;
    void  *real;  /* IDirectInputDeviceA */
    void  *realx; /* IDirectInputDevice2A / 7A when the game asked for it (slots 18+) */
    int    kind;
    DWORD  seq;
} wdev;

static HRESULT WINAPI dev_QueryInterface(wdev *d, REFIID riid, void **ppv) {
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirectInputDeviceA) ||
        IsEqualGUID(riid, &IID_IDirectInputDevice2A) || IsEqualGUID(riid, &IID_IDirectInputDevice7A)) {
        if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_IDirectInputDeviceA)) {
            void *x = NULL;
            HRESULT hr = CALL2(d->real, 0, riid, &x);
            if (FAILED(hr)) return hr;
            if (d->realx) CALL0(d->realx, 2);
            d->realx = x;
        }
        InterlockedIncrement(&d->ref);
        *ppv = d;
        return S_OK;
    }
    return CALL2(d->real, 0, riid, ppv);
}
static ULONG WINAPI dev_AddRef(wdev *d) { return InterlockedIncrement(&d->ref); }
static wdev *g_mouse; /* the game's mouse */
static ULONG WINAPI dev_Release(wdev *d) {
    if (d == g_mouse && d->ref <= 1) g_mouse = NULL;
    LONG r = InterlockedDecrement(&d->ref);
    if (r == 0) {
        if (d->realx) CALL0(d->realx, 2);
        CALL0(d->real, 2);
        HeapFree(GetProcessHeap(), 0, d);
    }
    return r;
}

/* the game reads one buffered event per call (Game.exe 0x4907C0 mouse, 0x492720 keyboard) */
int onc_settings_mouse(int button, int pressed);
static HRESULT WINAPI dev_GetDeviceData(wdev *d, DWORD cb, DIDEVICEOBJECTDATA *data, DWORD *inout, DWORD flags) {
    DWORD want = inout ? *inout : 0;
    HRESULT hr = CALL4(d->real, 10, cb, data, inout, flags);
    if (d->kind == DEV_OTHER || FAILED(hr) || !inout || !data || (flags & DIGDD_PEEK)) return hr;
    if (d->kind == DEV_MOUSE && *inout > 0) {
        static int swallow_up; /* the release of a click the tab strip used */
        for (DWORD i = 0; i < *inout; i++) {
            DIDEVICEOBJECTDATA *e = (DIDEVICEOBJECTDATA *)((char *)data + i * cb);
            int drop = 0;
            if ((e->dwOfs == DIMOFS_X || e->dwOfs == DIMOFS_Y) && e->dwData) {
                /* real mouse motion: the cursor belongs to the mouse again */
                onc_real_mouse_moved();
                int v = (int)e->dwData;
                if (v > 2 || v < -2) onc_real_input();
            } else if (e->dwOfs >= DIMOFS_BUTTON0 && e->dwOfs <= DIMOFS_BUTTON3 && onc_settings_mouse(e->dwOfs - DIMOFS_BUTTON0, (e->dwData & 0x80) != 0)) {
                onc_real_input();
                drop = 1; /* the settings menu has the mouse */
            } else if (e->dwOfs == DIMOFS_BUTTON0) {
                onc_real_input();
                if ((e->dwData & 0x80) && onc_tabs_mouse_click(ONC_I32(NOX_MOUSE_X), ONC_I32(NOX_MOUSE_Y))) {
                    drop = 1; swallow_up = 1;
                } else if (!(e->dwData & 0x80) && swallow_up) {
                    drop = 1; swallow_up = 0;
                }
            } else if (e->dwOfs >= DIMOFS_BUTTON1 && e->dwOfs <= DIMOFS_BUTTON3) {
                onc_real_input();
            }
            if (drop) { /* the game does not see it */
                memmove(e, (char *)e + cb, (*inout - i - 1) * cb);
                (*inout)--;
                i--;
            }
        }
    } else if (d->kind == DEV_KEYBOARD && *inout > 0) {
        for (DWORD i = 0; i < *inout; i++) {
            DIDEVICEOBJECTDATA *e = (DIDEVICEOBJECTDATA *)((char *)data + i * cb);
            if (e->dwData & 0x80) { onc_real_input(); break; }
        }
    }
    while (*inout < want) {
        DIDEVICEOBJECTDATA ev;
        int got = d->kind == DEV_MOUSE ? onc_pad_mouse_event(&ev) : onc_pad_key_event(&ev);
        if (!got) break;
        ev.dwSequence = ++d->seq + 0x40000000; /* our own range, after the real ones */
        char *dst = (char *)data + *inout * cb;
        memset(dst, 0, cb);
        memcpy(dst, &ev, cb < sizeof ev ? cb : sizeof ev);
        (*inout)++;
    }
    return hr;
}

/* plain forwards, by argument count */
#define DEVFWD0(i, name) static HRESULT WINAPI name(wdev *d) { return CALL0(d->real, i); }
#define DEVFWD1(i, name) static HRESULT WINAPI name(wdev *d, U a) { return CALL1(d->real, i, a); }
#define DEVFWD2(i, name) static HRESULT WINAPI name(wdev *d, U a, U b) { return CALL2(d->real, i, a, b); }
#define DEVFWD3(i, name) static HRESULT WINAPI name(wdev *d, U a, U b, U c) { return CALL3(d->real, i, a, b, c); }
#define DEVFWD4(i, name) static HRESULT WINAPI name(wdev *d, U a, U b, U c, U e) { return CALL4(d->real, i, a, b, c, e); }
#define DEVXFWD0(i, name) static HRESULT WINAPI name(wdev *d) { return d->realx ? CALL0(d->realx, i) : E_NOTIMPL; }
#define DEVXFWD1(i, name) static HRESULT WINAPI name(wdev *d, U a) { return d->realx ? CALL1(d->realx, i, a) : E_NOTIMPL; }
#define DEVXFWD2(i, name) static HRESULT WINAPI name(wdev *d, U a, U b) { return d->realx ? CALL2(d->realx, i, a, b) : E_NOTIMPL; }
#define DEVXFWD3(i, name) static HRESULT WINAPI name(wdev *d, U a, U b, U c) { return d->realx ? CALL3(d->realx, i, a, b, c) : E_NOTIMPL; }
#define DEVXFWD4(i, name) static HRESULT WINAPI name(wdev *d, U a, U b, U c, U e) { return d->realx ? CALL4(d->realx, i, a, b, c, e) : E_NOTIMPL; }

DEVFWD1(3, dev_GetCapabilities)
DEVFWD3(4, dev_EnumObjects)
DEVFWD2(5, dev_GetProperty)
DEVFWD2(6, dev_SetProperty)
DEVFWD0(7, dev_Acquire)
DEVFWD0(8, dev_Unacquire)
DEVFWD2(9, dev_GetDeviceState)
DEVFWD1(11, dev_SetDataFormat)
DEVFWD1(12, dev_SetEventNotification)
DEVFWD2(13, dev_SetCooperativeLevel)
DEVFWD3(14, dev_GetObjectInfo)
DEVFWD1(15, dev_GetDeviceInfo)
DEVFWD2(16, dev_RunControlPanel)
DEVFWD3(17, dev_Initialize)
DEVXFWD4(18, dev_CreateEffect)
DEVXFWD3(19, dev_EnumEffects)
DEVXFWD2(20, dev_GetEffectInfo)
DEVXFWD1(21, dev_GetForceFeedbackState)
DEVXFWD1(22, dev_SendForceFeedbackCommand)
DEVXFWD3(23, dev_EnumCreatedEffectObjects)
DEVXFWD1(24, dev_Escape)
DEVXFWD0(25, dev_Poll)
DEVXFWD4(26, dev_SendDeviceData)
DEVXFWD4(27, dev_EnumEffectsInFile)
DEVXFWD4(28, dev_WriteEffectToFile)

static void *dev_vtbl[] = {
    dev_QueryInterface, dev_AddRef, dev_Release, dev_GetCapabilities, dev_EnumObjects, dev_GetProperty,
    dev_SetProperty, dev_Acquire, dev_Unacquire, dev_GetDeviceState, dev_GetDeviceData, dev_SetDataFormat,
    dev_SetEventNotification, dev_SetCooperativeLevel, dev_GetObjectInfo, dev_GetDeviceInfo, dev_RunControlPanel,
    dev_Initialize, dev_CreateEffect, dev_EnumEffects, dev_GetEffectInfo, dev_GetForceFeedbackState,
    dev_SendForceFeedbackCommand, dev_EnumCreatedEffectObjects, dev_Escape, dev_Poll, dev_SendDeviceData,
    dev_EnumEffectsInFile, dev_WriteEffectToFile,
};

static void *wrap_device(void *real, REFGUID g) {
    int kind = IsEqualGUID(g, &GUID_SysMouse) ? DEV_MOUSE : IsEqualGUID(g, &GUID_SysKeyboard) ? DEV_KEYBOARD : DEV_OTHER;
    if (kind == DEV_OTHER) return real;
    wdev *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *d);
    if (!d) return real;
    d->vtbl = dev_vtbl;
    d->ref = 1;
    d->real = real;
    d->kind = kind;
    if (kind == DEV_MOUSE) g_mouse = d;
    onc_log("DirectInput: %s device wrapped", kind == DEV_MOUSE ? "mouse" : "keyboard");
    return d;
}

/* ---- DirectInput wrapper (IDirectInputA / 2A / 7A) ---- */

typedef struct {
    void **vtbl;
    LONG   ref;
    void  *real;
    void  *realx; /* IDirectInput2A / 7A for FindDevice / CreateDeviceEx */
} wdi;

static HRESULT WINAPI di_QueryInterface(wdi *w, REFIID riid, void **ppv) {
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirectInputA) ||
        IsEqualGUID(riid, &IID_IDirectInput2A) || IsEqualGUID(riid, &IID_IDirectInput7A)) {
        if (IsEqualGUID(riid, &IID_IDirectInput2A) || IsEqualGUID(riid, &IID_IDirectInput7A)) {
            void *x = NULL;
            HRESULT hr = CALL2(w->real, 0, riid, &x);
            if (FAILED(hr)) return hr;
            if (w->realx) CALL0(w->realx, 2);
            w->realx = x;
        }
        InterlockedIncrement(&w->ref);
        *ppv = w;
        return S_OK;
    }
    return CALL2(w->real, 0, riid, ppv);
}
static ULONG WINAPI di_AddRef(wdi *w) { return InterlockedIncrement(&w->ref); }
static ULONG WINAPI di_Release(wdi *w) {
    LONG r = InterlockedDecrement(&w->ref);
    if (r == 0) {
        if (w->realx) CALL0(w->realx, 2);
        CALL0(w->real, 2);
        HeapFree(GetProcessHeap(), 0, w);
    }
    return r;
}
static HRESULT WINAPI di_CreateDevice(wdi *w, REFGUID g, void **out, IUnknown *outer) {
    HRESULT hr = CALL3(w->real, 3, g, out, outer);
    if (SUCCEEDED(hr) && out && *out) *out = wrap_device(*out, g);
    return hr;
}
static HRESULT WINAPI di_EnumDevices(wdi *w, U a, U b, U c, U d) { return CALL4(w->real, 4, a, b, c, d); }
static HRESULT WINAPI di_GetDeviceStatus(wdi *w, U a) { return CALL1(w->real, 5, a); }
static HRESULT WINAPI di_RunControlPanel(wdi *w, U a, U b) { return CALL2(w->real, 6, a, b); }
static HRESULT WINAPI di_Initialize(wdi *w, U a, U b) { return CALL2(w->real, 7, a, b); }
static HRESULT WINAPI di_FindDevice(wdi *w, U a, U b, U c) { return w->realx ? CALL3(w->realx, 8, a, b, c) : E_NOTIMPL; }
static HRESULT WINAPI di_CreateDeviceEx(wdi *w, REFGUID g, REFIID riid, void **out, IUnknown *outer) {
    if (!w->realx) return E_NOTIMPL;
    HRESULT hr = CALL4(w->realx, 9, g, riid, out, outer);
    if (SUCCEEDED(hr) && out && *out) *out = wrap_device(*out, g);
    return hr;
}

static void *di_vtbl[] = {
    di_QueryInterface, di_AddRef, di_Release, di_CreateDevice, di_EnumDevices, di_GetDeviceStatus,
    di_RunControlPanel, di_Initialize, di_FindDevice, di_CreateDeviceEx,
};

/* ---- exports ---- */

HRESULT WINAPI onc_DirectInputCreateA(HINSTANCE inst, DWORD ver, LPDIRECTINPUTA *outp, LPUNKNOWN outer) {
    void **out = (void **)outp;
    if (!load_real()) return DIERR_GENERIC;
    create_fn real = (create_fn)GetProcAddress(g_real, "DirectInputCreateA");
    if (!real) return DIERR_GENERIC;
    HRESULT hr = real(inst, ver, out, outer);
    onc_log("DirectInputCreateA(version %04lX) = %08lX", ver, (unsigned long)hr);
    if (FAILED(hr) || !out || !*out) return hr;
    wdi *w = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *w);
    if (!w) return hr;
    w->vtbl = di_vtbl;
    w->ref = 1;
    w->real = *out;
    *out = w;
    return hr;
}

HRESULT WINAPI DirectInputCreateA(HINSTANCE inst, DWORD ver, LPDIRECTINPUTA *outp, LPUNKNOWN outer) {
    return onc_DirectInputCreateA(inst, ver, outp, outer);
}

/* Redirect Game.exe's import stub `jmp [DirectInputCreateA]` (0x56CE60) to our function. A code patch, so it
   holds whatever the loader does with the import table. */
int onc_dinput_hook(void) {
    static const uint8_t expect[6] = {0xFF, 0x25, 0x24, 0xF0, 0x57, 0x00};
    uint8_t *p = (uint8_t *)0x56CE60;
    if (memcmp(p, expect, 6) != 0) {
        onc_log("DirectInput hook REFUSED: Game.exe bytes differ (not the GOG Game.exe?)");
        return 0;
    }
    DWORD old;
    if (!VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old)) return 0;
    int32_t rel = (int32_t)((uintptr_t)onc_DirectInputCreateA - ((uintptr_t)p + 5));
    p[0] = 0xE9;
    memcpy(p + 1, &rel, 4);
    p[5] = 0x90;
    VirtualProtect(p, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 6);
    onc_log("DirectInput hooked (0x56CE60)");
    return 1;
}

/* the other exports (dinput.dll proxy only) are passed straight to the real DLL */
HRESULT WINAPI DirectInputCreateW(HINSTANCE a, DWORD b, LPDIRECTINPUTW *c, LPUNKNOWN d) {
    if (!load_real()) return DIERR_GENERIC;
    FARPROC f = GetProcAddress(g_real, "DirectInputCreateW");
    return f ? ((HRESULT(WINAPI *)(HINSTANCE, DWORD, LPDIRECTINPUTW *, LPUNKNOWN))f)(a, b, c, d) : DIERR_GENERIC;
}
HRESULT WINAPI DirectInputCreateEx(HINSTANCE a, DWORD b, REFIID c, LPVOID *d, LPUNKNOWN e) {
    if (!load_real()) return DIERR_GENERIC;
    FARPROC f = GetProcAddress(g_real, "DirectInputCreateEx");
    return f ? ((HRESULT(WINAPI *)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN))f)(a, b, c, d, e) : DIERR_GENERIC;
}
STDAPI DllCanUnloadNow(void) { return S_FALSE; }
STDAPI DllGetClassObject(REFCLSID a, REFIID b, LPVOID *c) {
    if (!load_real()) return CLASS_E_CLASSNOTAVAILABLE;
    FARPROC f = GetProcAddress(g_real, "DllGetClassObject");
    return f ? ((HRESULT(WINAPI *)(REFCLSID, REFIID, LPVOID *))f)(a, b, c) : CLASS_E_CLASSNOTAVAILABLE;
}
STDAPI DllRegisterServer(void) { return S_OK; }
STDAPI DllUnregisterServer(void) { return S_OK; }

/* the mouse motion the game has not read yet (its buffer, looked at without taking anything out of it): for the
   cursor drawn between two ticks (cursor.c) */
int onc_mouse_pending(int *dx, int *dy) {
    *dx = *dy = 0;
    if (!g_mouse || !g_mouse->real) return 0;
    struct { DWORD ofs, data, time, seq; } buf[128];
    DWORD n = 128;
    HRESULT hr = CALL4(g_mouse->real, 10, sizeof buf[0], buf, &n, DIGDD_PEEK);
    if (FAILED(hr)) return 0;
    for (DWORD i = 0; i < n && i < 128; i++) {
        if (buf[i].ofs == DIMOFS_X) *dx += (int)buf[i].data;
        else if (buf[i].ofs == DIMOFS_Y) *dy += (int)buf[i].data;
    }
    return 1;
}
