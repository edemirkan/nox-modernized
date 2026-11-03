/* Shows the game's frames with Direct3D 11 (GDI / StretchDIBits if Direct3D 11 or the shader compiler is not
   available), scaled into the window with the chosen filter. Also owns the window: borderless over the whole
   monitor, or a normal window.
   - One layer (menus, dsp_present): RGB565 -> BGRA through a table (the game's gamma ramp folded in) on the CPU.
   - Layers (in a game with Nox Modernized's split rendering, dsp_present_layers / OncPresentWorld): the world, the
     GUI on top (the pixels that differ from the snapshot of the world under it), the mouse cursor and the frame
     counter. On feature level 10+ the 565 pixels are uploaded as they are and converted on the GPU (k_conv);
     else on the CPU. The high frame rate's extra pictures (OncPresentWorld) bring only a new world.
   Every 10 s the display log gets the time per picture (conversion, upload, draw, Present). */
#include "display.h"
#include <d3d11.h>
#include <dxgi.h>
#include <string.h>

typedef HRESULT(WINAPI *PFN_COMPILE)(const void *, SIZE_T, const char *, const void *, void *, const char *,
                                     const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);

static HWND g_hwnd;
static WNDPROC g_old_proc;
enum { F_NEAREST, F_LINEAR, F_SHARP, F_BICUBIC, F_LANCZOS };
enum { S_FIT, S_INTEGER, S_STRETCH };
static const char *k_filters[] = {"nearest", "linear", "sharp", "bicubic", "lanczos"};
static const char *k_scales[] = {"fit", "integer", "stretch"};
static int g_borderless = 1, g_vsync = 1, g_filter = F_SHARP, g_scalemode = S_FIT, g_scale = 2, g_shot_key = VK_SNAPSHOT;
static volatile LONG g_shot_req;
static int g_fps_on, g_fps_key = VK_F8;
static volatile LONG g_fps_toggle;
static int g_mode_w = 640, g_mode_h = 480;
static volatile LONG g_resized;
static int g_cfg_read;
static int g_keep_on = 1; /* [display] keep_screen_on: turn down the screen saver / monitor off */

static uint32_t g_lut[65536];
static int g_lut_ok;
static WORD g_gr[256], g_gg[256], g_gb[256];
static int g_has_gamma;

static uint32_t *g_rgb; static int g_rgb_w, g_rgb_h;

/* d3d11 */
static int g_d3d_state;      /* 0 not tried, 1 ready, -1 failed (GDI) */
static ID3D11Device *g_dev;
static ID3D11DeviceContext *g_ctx;
static IDXGISwapChain *g_sc;
static ID3D11RenderTargetView *g_rtv;
static ID3D11Texture2D *g_tex; static int g_tex_w, g_tex_h;
static ID3D11ShaderResourceView *g_srv;
static ID3D11VertexShader *g_vs;
static ID3D11PixelShader *g_ps;
static ID3D11SamplerState *g_samp;
static ID3D11Buffer *g_cb;
static ID3D11PixelShader *g_psg;
static ID3D11SamplerState *g_samp_lin;
static ID3D11BlendState *g_blend;
static ID3D11Texture2D *g_texg; static int g_texg_w, g_texg_h;
static ID3D11ShaderResourceView *g_srvg;
static uint32_t *g_gui; static int g_gui_w, g_gui_h;    /* the GUI layer, BGRA with coverage in alpha */
static uint32_t *g_comp; static int g_comp_w, g_comp_h; /* both layers on the CPU (GDI, screenshots) */
static float g_cb_last[8];
static int g_sc_w, g_sc_h, g_flip;
static PFN_COMPILE g_compile; static const char *g_prof_ps; static D3D_FEATURE_LEVEL g_fl;
static volatile LONG g_filter_dirty;  /* the filter changed: its shader again (same device) */
static int g_dev_lost, g_d3d_retries; /* Present said the device was lost: Direct3D again, a few times, then GDI */
/* the colour conversion on the GPU (feature level 10+): the 565 pixels are uploaded as they are (R16_UINT, half
   the bytes of BGRA), a pass turns them into BGRA through the colour table (a 256 x 256 texture, gamma included)
   into a texture of the same size, which the filters then read as before; the GUI pass also compares each pixel
   with the snapshot of the world under it (the GUI = what differs): no conversion or mask loop on the CPU */
static int g_gpu;                                      /* in use */
static int g_lut_dirty = 1;                            /* the table texture needs the table again */
static ID3D11PixelShader *g_pscw, *g_pscg;
static ID3D11Texture2D *g_lut_tex; static ID3D11ShaderResourceView *g_lut_srv;
typedef struct { ID3D11Texture2D *t; ID3D11ShaderResourceView *s; ID3D11RenderTargetView *r; int w, h; } tex_t;
static tex_t g_w16, g_g16, g_s16;                      /* uploads: world, GUI, snapshot (565) */
static tex_t g_rtw, g_rtg;                             /* converted: world, GUI (BGRA, coverage in alpha) */
static tex_t g_tcur, g_tfps;                           /* the cursor, the frame counter */
static ID3D11SamplerState *g_samp_pt;
static uint32_t *g_curpx; static int g_curpx_n; static int g_cur_serial = -1, g_cur_gamma = -1;
static uint32_t g_fpspx[64 * 1024]; static int g_fps_w, g_fps_h, g_fps_x, g_fps_y, g_fps_vis;
static uint8_t *g_cmask; static int g_cmask_n;         /* the mask on the CPU (GDI, screenshots) */
static int g_gamma_gen;
static LARGE_INTEGER g_pf;
static double tnow(void) { LARGE_INTEGER t; if (!g_pf.QuadPart) QueryPerformanceFrequency(&g_pf); QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / g_pf.QuadPart; }
static struct { int full, wonly; double conv, upload, draw, present; double t0; } g_st;

static void read_cfg(void) {
    if (g_cfg_read) return;
    g_cfg_read = 1;
    char s[32];
    dsp_cfg_str("mode", "borderless", s, sizeof s);
    g_borderless = _stricmp(s, "window") != 0;
    g_vsync = dsp_cfg_int("vsync", 1);
    dsp_cfg_str("filter", "sharp", s, sizeof s);
    for (int i = 0; i < 5; i++) if (!_stricmp(s, k_filters[i])) g_filter = i;
    dsp_cfg_str("scale", "fit", s, sizeof s);
    for (int i = 0; i < 3; i++) if (!_stricmp(s, k_scales[i])) g_scalemode = i;
    g_scale = dsp_cfg_int("window_scale", 0);
    g_shot_key = dsp_cfg_int("screenshot_key", VK_SNAPSHOT);
    g_fps_on = dsp_cfg_int("fps", 0);
    g_fps_key = dsp_cfg_int("fps_key", VK_F8);
    g_keep_on = dsp_cfg_int("keep_screen_on", 1);
    dsp_log("display: %s, vsync %d, scale %s, filter %s, window_scale %d, screenshot key %d",
            g_borderless ? "borderless" : "window", g_vsync, k_scales[g_scalemode], k_filters[g_filter], g_scale, g_shot_key);
}

/* ---------------- colour table ---------------- */
static void build_lut(void) {
    for (int i = 0; i < 65536; i++) {
        int r = (i >> 11) & 31, g = (i >> 5) & 63, b = i & 31;
        r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
        if (g_has_gamma) { r = g_gr[r] >> 8; g = g_gg[g] >> 8; b = g_gb[b] >> 8; }
        g_lut[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    g_lut_ok = 1;
    g_lut_dirty = 1;
    g_gamma_gen++;
}
void dsp_set_gamma(const WORD *r, const WORD *g, const WORD *b) {
    int identity = 1;
    if (r && g && b)
        for (int i = 0; i < 256; i++)
            if (r[i] >> 8 != i || g[i] >> 8 != i || b[i] >> 8 != i) { identity = 0; break; }
    g_has_gamma = !identity;
    if (g_has_gamma) { memcpy(g_gr, r, sizeof g_gr); memcpy(g_gg, g, sizeof g_gg); memcpy(g_gb, b, sizeof g_gb); }
    g_lut_ok = 0;
}

/* ---------------- window ---------------- */
static void target_rect(RECT *r) {
    HMONITOR m = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoA(m, &mi);
    if (g_borderless) { *r = mi.rcMonitor; return; }
    RECT wa = mi.rcWork;
    int aw = wa.right - wa.left, ah = wa.bottom - wa.top, s = g_scale;
    if (s <= 0) { /* the biggest whole size that fits */
        s = 1;
        while ((s + 1) * 640 <= aw - 40 && (s + 1) * 480 <= ah - 60) s++;
    }
    int cw = 640 * s, ch = 480 * s; /* the window keeps one size for all game modes */
    RECT c = {0, 0, cw, ch};
    AdjustWindowRect(&c, WS_OVERLAPPEDWINDOW, FALSE);
    int w = c.right - c.left, h = c.bottom - c.top;
    r->left = wa.left + (aw - w) / 2; r->top = wa.top + (ah - h) / 2;
    if (r->left < wa.left) r->left = wa.left;
    if (r->top < wa.top) r->top = wa.top;
    r->right = r->left + w; r->bottom = r->top + h;
}

static void clip_cursor(int on) {
    if (on && GetForegroundWindow() == g_hwnd) {
        RECT c; GetClientRect(g_hwnd, &c);
        MapWindowPoints(g_hwnd, NULL, (POINT *)&c, 2);
        ClipCursor(&c);
    } else ClipCursor(NULL);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(NULL); return TRUE; }
        break;
    case WM_SIZE:
        InterlockedExchange(&g_resized, 1);
        break;
    case WM_ACTIVATEAPP:
        clip_cursor(wp != 0);
        break;
    case WM_ACTIVATE:
        clip_cursor(LOWORD(wp) != WA_INACTIVE);
        break;
    case WM_WINDOWPOSCHANGING:
        if (g_borderless) { /* the game moves its window for a mode change: it stays over the monitor */
            WINDOWPOS *p = (WINDOWPOS *)lp;
            RECT r; target_rect(&r);
            p->x = r.left; p->y = r.top; p->cx = r.right - r.left; p->cy = r.bottom - r.top;
            p->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        }
        break;
    case WM_WINDOWPOSCHANGED:
        CallWindowProcA(g_old_proc, h, msg, wp, lp);
        clip_cursor(1);
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if ((int)wp == g_shot_key) InterlockedExchange(&g_shot_req, 1);
        if ((int)wp == g_fps_key) InterlockedExchange(&g_fps_toggle, 1);
        break;
    case WM_SYSKEYDOWN:
        if (wp == VK_RETURN) return 0; /* no Alt+Enter */
        break;
    case WM_SYSCOMMAND: /* a gamepad is not activity for Windows: no screen saver / monitor off over the game */
        if (g_keep_on && ((wp & 0xFFF0) == SC_SCREENSAVE || (wp & 0xFFF0) == SC_MONITORPOWER)) return 0;
        break;
    case WM_NCDESTROY:
        ClipCursor(NULL);
        break;
    }
    return CallWindowProcA(g_old_proc, h, msg, wp, lp);
}

void dsp_window(HWND hwnd, int w, int h) {
    read_cfg();
    g_mode_w = w; g_mode_h = h;
    if (hwnd != g_hwnd) {
        g_hwnd = hwnd;
        g_old_proc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)wnd_proc);
    }
    DWORD style = g_borderless ? (WS_POPUP | WS_VISIBLE) : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    SetWindowLongA(hwnd, GWL_STYLE, style);
    SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) & ~WS_EX_TOPMOST);
    RECT r; target_rect(&r);
    SetWindowPos(hwnd, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    InterlockedExchange(&g_resized, 1);
    clip_cursor(1);
}

/* where the picture goes in the client area: fit (shape kept), integer (whole multiples, shape kept), stretch */
static void fit(int cw, int ch, int w, int h, int *x, int *y, int *vw, int *vh) {
    if (g_scalemode == S_STRETCH) { *vw = cw; *vh = ch; }
    else {
        if ((long long)cw * h > (long long)ch * w) { *vh = ch; *vw = ch * w / h; }
        else { *vw = cw; *vh = cw * h / w; }
        if (g_scalemode == S_INTEGER) {
            int k = cw / w < ch / h ? cw / w : ch / h;
            if (k >= 1) { *vw = w * k; *vh = h * k; }
        }
    }
    *x = (cw - *vw) / 2; *y = (ch - *vh) / 2;
}

/* ---------------- Direct3D 11 ---------------- */
/* FILTER: 0 nearest, 1 linear, 2 sharp (nearest with blended texel edges), 3 bicubic (Catmull-Rom), 4 lanczos (3 lobes) */
static const char k_shader[] =
    "Texture2D t : register(t0); SamplerState s : register(s0);\n"
    "cbuffer C : register(b0) { float4 sz; float4 sc; };\n" /* sz: w, h, 1/w, 1/h; sc: screen px per texel x, y */
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) { V o; float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o; }\n"
    "#if FILTER >= 3\n"
    "float3 px(int2 c) { return t.Load(int3(clamp(c, int2(0, 0), int2(sz.xy) - 1), 0)).rgb; }\n"
    "#endif\n"
    "#if FILTER == 3\n"
    "float4 cr(float x) { float4 d = float4(1 + x, x, 1 - x, 2 - x);\n"
    "  float4 a = abs(d); return a < 1 ? (1.5 * a - 2.5) * a * a + 1 : ((-0.5 * a + 2.5) * a - 4) * a + 2; }\n"
    "#endif\n"
    "#if FILTER == 4\n"
    "float lz(float x) { x = abs(x); if (x < 1e-4) return 1; if (x >= 3) return 0;\n"
    "  float p = 3.14159265 * x; return 3 * sin(p) * sin(p / 3) / (p * p); }\n"
    "#endif\n"
    "float4 psg(V i) : SV_Target { return t.Sample(s, i.uv); }\n" /* the GUI layer: colour and coverage */
    "float4 ps(V i) : SV_Target {\n"
    "#if FILTER == 2\n"
    "  float2 p = i.uv * sz.xy; float2 b = floor(p) + 0.5;\n"
    "  float2 q = b + clamp((p - b) * sc.xy, -0.5, 0.5);\n"
    "  return float4(t.Sample(s, q * sz.zw).rgb, 1);\n"
    "#elif FILTER == 3\n"
    "  float2 p = i.uv * sz.xy - 0.5; float2 b = floor(p); float2 f = p - b; int2 c = int2(b);\n"
    "  float4 wx = cr(f.x), wy = cr(f.y); float3 r = 0;\n"
    "  [unroll] for (int y = 0; y < 4; y++) { float3 row = 0;\n"
    "    [unroll] for (int x = 0; x < 4; x++) row += px(c + int2(x - 1, y - 1)) * wx[x];\n"
    "    r += row * wy[y]; }\n"
    "  return float4(saturate(r), 1);\n"
    "#elif FILTER == 4\n"
    "  float2 p = i.uv * sz.xy - 0.5; float2 b = floor(p); float2 f = p - b; int2 c = int2(b);\n"
    "  float wx[6], wy[6]; float sx = 0, sy = 0;\n"
    "  [unroll] for (int k = 0; k < 6; k++) { wx[k] = lz(k - 2 - f.x); wy[k] = lz(k - 2 - f.y); sx += wx[k]; sy += wy[k]; }\n"
    "  float3 r = 0;\n"
    "  [unroll] for (int y = 0; y < 6; y++) { float3 row = 0;\n"
    "    [unroll] for (int x = 0; x < 6; x++) row += px(c + int2(x - 2, y - 2)) * wx[x];\n"
    "    r += row * wy[y]; }\n"
    "  return float4(saturate(r / (sx * sy)), 1);\n"
    "#else\n"
    "  return float4(t.Sample(s, i.uv).rgb, 1);\n"
    "#endif\n"
    "}\n";

/* the colour conversion passes (into a texture the size of the source; i.p = the pixel's centre) */
static const char k_conv[] =
    "Texture2D<uint> src : register(t0); Texture2D<uint> snp : register(t1); Texture2D lut : register(t2);\n"
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float3 col(uint v) { return lut.Load(int3(v & 255, v >> 8, 0)).rgb; }\n"
    "float4 cvw(V i) : SV_Target { return float4(col(src.Load(int3(i.p.xy, 0))), 1); }\n"
    "float4 cvg(V i) : SV_Target { int3 c = int3(i.p.xy, 0); uint v = src.Load(c);\n"
    "  return float4(col(v), v != snp.Load(c) ? 1 : 0); }\n";

#define REL(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)

static void tex_rel(tex_t *x) { REL(x->r); REL(x->s); REL(x->t); x->w = x->h = 0; }
static void d3d_release(void) {
    if (g_ctx) { g_ctx->lpVtbl->ClearState(g_ctx); g_ctx->lpVtbl->Flush(g_ctx); } /* the swap chain really goes */
    tex_rel(&g_w16); tex_rel(&g_g16); tex_rel(&g_s16); tex_rel(&g_rtw); tex_rel(&g_rtg); tex_rel(&g_tcur); tex_rel(&g_tfps);
    REL(g_lut_srv); REL(g_lut_tex); REL(g_pscw); REL(g_pscg); REL(g_samp_pt);
    g_gpu = 0; g_lut_dirty = 1; g_cur_serial = -1;
    REL(g_srvg); REL(g_texg); REL(g_blend); REL(g_samp_lin); REL(g_psg); g_texg_w = g_texg_h = 0;
    REL(g_srv); REL(g_tex); REL(g_rtv); REL(g_cb); REL(g_samp); REL(g_ps); REL(g_vs); REL(g_sc); REL(g_ctx); REL(g_dev);
    g_tex_w = g_tex_h = 0;
}

static int make_rtv(void) {
    ID3D11Texture2D *bb = NULL;
    if (FAILED(g_sc->lpVtbl->GetBuffer(g_sc, 0, &IID_ID3D11Texture2D, (void **)&bb))) return 0;
    HRESULT hr = g_dev->lpVtbl->CreateRenderTargetView(g_dev, (ID3D11Resource *)bb, NULL, &g_rtv);
    bb->lpVtbl->Release(bb);
    return SUCCEEDED(hr);
}

/* the world's pixel shader and sampler for the chosen filter (at start and when the filter changes: the device and
   swap chain stay; making them again right away could lose the device under DXVK) */
static int filter_shader(void) {
    if (g_filter >= F_BICUBIC && g_fl < D3D_FEATURE_LEVEL_10_0) { dsp_log("%s needs feature level 10: sharp", k_filters[g_filter]); g_filter = F_SHARP; }
    ID3DBlob *bp = NULL, *err = NULL;
    for (;;) { /* if a filter's shader does not compile here, fall back: lanczos/bicubic -> sharp -> linear */
        char fdef[2] = {(char)('0' + g_filter), 0};
        D3D_SHADER_MACRO defs[] = {{"FILTER", fdef}, {NULL, NULL}};
        HRESULT hr = g_compile(k_shader, sizeof k_shader - 1, "present", defs, NULL, "ps", g_prof_ps, 1u << 15, 0, &bp, &err);
        if (SUCCEEDED(hr)) break;
        dsp_log("shader (%s) compile failed: %s", k_filters[g_filter], err ? (char *)err->lpVtbl->GetBufferPointer(err) : "?");
        REL(err); REL(bp);
        if (g_filter == F_LINEAR || g_filter == F_NEAREST) return 0;
        g_filter = g_filter == F_SHARP ? F_LINEAR : F_SHARP;
    }
    REL(g_ps); REL(g_samp);
    g_dev->lpVtbl->CreatePixelShader(g_dev, bp->lpVtbl->GetBufferPointer(bp), bp->lpVtbl->GetBufferSize(bp), NULL, &g_ps);
    REL(bp); REL(err);
    D3D11_SAMPLER_DESC smp;
    memset(&smp, 0, sizeof smp);
    smp.Filter = g_filter == F_NEAREST ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp.MaxLOD = D3D11_FLOAT32_MAX;
    g_dev->lpVtbl->CreateSamplerState(g_dev, &smp, &g_samp);
    return g_ps && g_samp;
}
static int filter_check(void) {
    if (!InterlockedExchange(&g_filter_dirty, 0)) return 1;
    int ok = filter_shader();
    dsp_log("filter %s%s", k_filters[g_filter], ok ? "" : ": shader failed");
    return ok;
}

static int d3d_init(void) {
    HMODULE d3d = LoadLibraryA("d3d11.dll");
    if (!d3d) { dsp_log("d3d11.dll not found: GDI"); return 0; }
    PFN_D3D11_CREATE_DEVICE create = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(d3d, "D3D11CreateDevice");
    PFN_COMPILE compile = NULL;
    static const char *comp[] = {"d3dcompiler_47.dll", "d3dcompiler_46.dll", "d3dcompiler_43.dll"};
    for (int i = 0; i < 3 && !compile; i++) {
        HMODULE c = LoadLibraryA(comp[i]);
        if (c) { compile = (PFN_COMPILE)GetProcAddress(c, "D3DCompile"); if (compile) dsp_log("shader compiler %s", comp[i]); }
    }
    if (!create || !compile) { dsp_log("no D3D11CreateDevice or D3DCompile: GDI"); return 0; }

    static const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                                           D3D_FEATURE_LEVEL_9_3};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fl, 4,
                        D3D11_SDK_VERSION, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) { dsp_log("D3D11CreateDevice failed %08lX: GDI", hr); return 0; }

    IDXGIDevice *xd = NULL; IDXGIAdapter *ad = NULL; IDXGIFactory *fac = NULL;
    g_dev->lpVtbl->QueryInterface(g_dev, &IID_IDXGIDevice, (void **)&xd);
    if (xd) xd->lpVtbl->GetAdapter(xd, &ad);
    if (ad) ad->lpVtbl->GetParent(ad, &IID_IDXGIFactory, (void **)&fac);
    if (!fac) { REL(ad); REL(xd); d3d_release(); dsp_log("no DXGI factory: GDI"); return 0; }

    RECT c; GetClientRect(g_hwnd, &c);
    g_sc_w = c.right > 0 ? c.right : 1; g_sc_h = c.bottom > 0 ? c.bottom : 1;
    DXGI_SWAP_CHAIN_DESC sd;
    memset(&sd, 0, sizeof sd);
    sd.BufferDesc.Width = g_sc_w; sd.BufferDesc.Height = g_sc_h;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = g_hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = 4; /* DXGI_SWAP_EFFECT_FLIP_DISCARD (Windows 10) */
    hr = fac->lpVtbl->CreateSwapChain(fac, (IUnknown *)g_dev, &sd, &g_sc);
    g_flip = SUCCEEDED(hr);
    if (FAILED(hr)) {
        sd.BufferCount = 1;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = fac->lpVtbl->CreateSwapChain(fac, (IUnknown *)g_dev, &sd, &g_sc);
    }
    if (SUCCEEDED(hr)) fac->lpVtbl->MakeWindowAssociation(fac, g_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    REL(fac); REL(ad); REL(xd);
    if (FAILED(hr)) { dsp_log("CreateSwapChain failed %08lX: GDI", hr); d3d_release(); return 0; }

    const char *prof_vs = got >= D3D_FEATURE_LEVEL_10_0 ? "vs_4_0" : "vs_4_0_level_9_3";
    const char *prof_ps = got >= D3D_FEATURE_LEVEL_10_0 ? "ps_4_0" : "ps_4_0_level_9_3";
    /* bicubic and lanczos read texels directly: not on 9.x hardware */
    if (g_filter >= F_BICUBIC && got < D3D_FEATURE_LEVEL_10_0) { dsp_log("%s needs feature level 10: sharp", k_filters[g_filter]); g_filter = F_SHARP; }
    g_compile = compile; g_prof_ps = prof_ps; g_fl = got;
    ID3DBlob *bv = NULL, *err = NULL;
    {
        D3D_SHADER_MACRO defs[] = {{"FILTER", "1"}, {NULL, NULL}};
        hr = compile(k_shader, sizeof k_shader - 1, "present", defs, NULL, "vs", prof_vs, 0, 0, &bv, &err);
        if (FAILED(hr)) { dsp_log("vertex shader compile failed: %s", err ? (char *)err->lpVtbl->GetBufferPointer(err) : "?"); REL(err); d3d_release(); return 0; }
        g_dev->lpVtbl->CreateVertexShader(g_dev, bv->lpVtbl->GetBufferPointer(bv), bv->lpVtbl->GetBufferSize(bv), NULL, &g_vs);
    }
    if (!filter_shader()) { REL(bv); REL(err); d3d_release(); return 0; }
    {   /* the GUI layer's shader, sampler and blending */
        D3D_SHADER_MACRO d1[] = {{"FILTER", "1"}, {NULL, NULL}};
        ID3DBlob *bg = NULL, *eg = NULL;
        if (SUCCEEDED(compile(k_shader, sizeof k_shader - 1, "present", d1, NULL, "psg", prof_ps, 0, 0, &bg, &eg)))
            g_dev->lpVtbl->CreatePixelShader(g_dev, bg->lpVtbl->GetBufferPointer(bg), bg->lpVtbl->GetBufferSize(bg), NULL, &g_psg);
        else dsp_log("GUI shader compile failed: %s", eg ? (char *)eg->lpVtbl->GetBufferPointer(eg) : "?");
        REL(bg); REL(eg);
        D3D11_SAMPLER_DESC sl;
        memset(&sl, 0, sizeof sl);
        sl.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sl.AddressU = sl.AddressV = sl.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sl.MaxLOD = D3D11_FLOAT32_MAX;
        g_dev->lpVtbl->CreateSamplerState(g_dev, &sl, &g_samp_lin);
        D3D11_BLEND_DESC bd2;
        memset(&bd2, 0, sizeof bd2);
        bd2.RenderTarget[0].BlendEnable = TRUE;
        bd2.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd2.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd2.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd2.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd2.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd2.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd2.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        g_dev->lpVtbl->CreateBlendState(g_dev, &bd2, &g_blend);
    }
    REL(bv); REL(err);
    {   /* the conversion on the GPU */
        D3D11_SAMPLER_DESC sp;
        memset(&sp, 0, sizeof sp);
        sp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sp.AddressU = sp.AddressV = sp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sp.MaxLOD = D3D11_FLOAT32_MAX;
        g_dev->lpVtbl->CreateSamplerState(g_dev, &sp, &g_samp_pt);
        g_gpu = 0;
        if (got >= D3D_FEATURE_LEVEL_10_0 && dsp_cfg_int("gpu_convert", 1)) {
            ID3DBlob *b1 = NULL, *b2 = NULL, *e1 = NULL;
            if (SUCCEEDED(compile(k_conv, sizeof k_conv - 1, "convert", NULL, NULL, "cvw", "ps_4_0", 1u << 15, 0, &b1, &e1)) &&
                SUCCEEDED(compile(k_conv, sizeof k_conv - 1, "convert", NULL, NULL, "cvg", "ps_4_0", 1u << 15, 0, &b2, &e1))) {
                g_dev->lpVtbl->CreatePixelShader(g_dev, b1->lpVtbl->GetBufferPointer(b1), b1->lpVtbl->GetBufferSize(b1), NULL, &g_pscw);
                g_dev->lpVtbl->CreatePixelShader(g_dev, b2->lpVtbl->GetBufferPointer(b2), b2->lpVtbl->GetBufferSize(b2), NULL, &g_pscg);
            } else dsp_log("conversion shader compile failed: %s", e1 ? (char *)e1->lpVtbl->GetBufferPointer(e1) : "?");
            REL(b1); REL(b2); REL(e1);
            D3D11_TEXTURE2D_DESC td;
            memset(&td, 0, sizeof td);
            td.Width = 256; td.Height = 256; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (SUCCEEDED(g_dev->lpVtbl->CreateTexture2D(g_dev, &td, NULL, &g_lut_tex)))
                g_dev->lpVtbl->CreateShaderResourceView(g_dev, (ID3D11Resource *)g_lut_tex, NULL, &g_lut_srv);
            g_gpu = g_pscw && g_pscg && g_lut_srv && g_samp_pt;
            g_lut_dirty = 1;
        }
        dsp_log("colour conversion: %s", g_gpu ? "GPU (565 uploads)" : "CPU");
    }

    D3D11_BUFFER_DESC bd;
    memset(&bd, 0, sizeof bd);
    bd.ByteWidth = 32; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_dev->lpVtbl->CreateBuffer(g_dev, &bd, NULL, &g_cb);
    memset(g_cb_last, 0, sizeof g_cb_last);
    if (!g_vs || !g_ps || !g_samp || !g_cb || !make_rtv()) { dsp_log("D3D11 setup failed: GDI"); d3d_release(); return 0; }
    dsp_log("Direct3D 11 ready (feature level %X, %s swap chain, %dx%d, filter %s)", got, g_flip ? "flip" : "blit", g_sc_w, g_sc_h, k_filters[g_filter]);
    return 1;
}

/* the window changed size: the swap chain follows */
static int check_resize(void) {
    if (!InterlockedExchange(&g_resized, 0)) return 1;
    RECT c; GetClientRect(g_hwnd, &c);
    if (c.right <= 0 || c.bottom <= 0 || (c.right == g_sc_w && c.bottom == g_sc_h)) return 1;
    g_ctx->lpVtbl->OMSetRenderTargets(g_ctx, 0, NULL, NULL);
    REL(g_rtv);
    HRESULT hr = g_sc->lpVtbl->ResizeBuffers(g_sc, 0, c.right, c.bottom, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr) || !make_rtv()) { dsp_log("ResizeBuffers failed %08lX", hr); return 0; }
    g_sc_w = c.right; g_sc_h = c.bottom;
    return 1;
}

static int d3d_frame(int w, int h) {
    if (!filter_check()) return 0;
    if (!check_resize()) return 0;
    if (w != g_tex_w || h != g_tex_h) {
        REL(g_srv); REL(g_tex);
        D3D11_TEXTURE2D_DESC td;
        memset(&td, 0, sizeof td);
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_dev->lpVtbl->CreateTexture2D(g_dev, &td, NULL, &g_tex))) return 0;
        if (FAILED(g_dev->lpVtbl->CreateShaderResourceView(g_dev, (ID3D11Resource *)g_tex, NULL, &g_srv))) return 0;
        g_tex_w = w; g_tex_h = h;
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(g_ctx->lpVtbl->Map(g_ctx, (ID3D11Resource *)g_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return 0;
    for (int y = 0; y < h; y++) memcpy((uint8_t *)m.pData + y * m.RowPitch, g_rgb + y * w, w * 4);
    g_ctx->lpVtbl->Unmap(g_ctx, (ID3D11Resource *)g_tex, 0);

    static const float black[4] = {0, 0, 0, 1};
    g_ctx->lpVtbl->OMSetRenderTargets(g_ctx, 1, &g_rtv, NULL);
    g_ctx->lpVtbl->ClearRenderTargetView(g_ctx, g_rtv, black);
    int x, yy, vw, vh;
    fit(g_sc_w, g_sc_h, w, h, &x, &yy, &vw, &vh);
    D3D11_VIEWPORT vp = {(float)x, (float)yy, (float)vw, (float)vh, 0, 1};
    g_ctx->lpVtbl->RSSetViewports(g_ctx, 1, &vp);
    float cb[8] = {(float)w, (float)h, 1.0f / w, 1.0f / h, (float)vw / w, (float)vh / h, 0, 0};
    if (memcmp(cb, g_cb_last, sizeof cb)) {
        g_ctx->lpVtbl->UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb, 0, NULL, cb, 0, 0);
        memcpy(g_cb_last, cb, sizeof cb);
    }
    g_ctx->lpVtbl->PSSetConstantBuffers(g_ctx, 0, 1, &g_cb);
    g_ctx->lpVtbl->IASetInputLayout(g_ctx, NULL);
    g_ctx->lpVtbl->IASetPrimitiveTopology(g_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->lpVtbl->VSSetShader(g_ctx, g_vs, NULL, 0);
    g_ctx->lpVtbl->PSSetShader(g_ctx, g_ps, NULL, 0);
    g_ctx->lpVtbl->PSSetShaderResources(g_ctx, 0, 1, &g_srv);
    g_ctx->lpVtbl->PSSetSamplers(g_ctx, 0, 1, &g_samp);
    g_ctx->lpVtbl->Draw(g_ctx, 3, 0);
    HRESULT hr = g_sc->lpVtbl->Present(g_sc, g_vsync ? 1 : 0, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) { dsp_log("device lost %08lX", hr); g_dev_lost = 1; return 0; }
    return 1;
}

/* ---------------- GDI ---------------- */
static void gdi_frame(int w, int h) {
    HDC dc = GetDC(g_hwnd);
    if (!dc) return;
    RECT c; GetClientRect(g_hwnd, &c);
    int x, y, vw, vh;
    fit(c.right, c.bottom, w, h, &x, &y, &vw, &vh);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HBRUSH br = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT bars[4] = {{0, 0, c.right, y}, {0, y + vh, c.right, c.bottom}, {0, y, x, y + vh}, {x + vw, y, c.right, y + vh}};
    for (int i = 0; i < 4; i++) if (bars[i].right > bars[i].left && bars[i].bottom > bars[i].top) FillRect(dc, &bars[i], br);
    SetStretchBltMode(dc, g_filter == F_NEAREST ? COLORONCOLOR : HALFTONE);
    StretchDIBits(dc, x, y, vw, vh, 0, 0, w, h, g_rgb, &bi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_hwnd, dc);
}

/* ---------------- frame counter ---------------- */
static const uint16_t k_glyph[] = { /* 3x5, rows top to bottom, 3 bits each */
    075557, 026227, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717, /* 0-9 */
    074744, 075744, 074717, 000000, 000002};                                       /* F P S space . */
static uint32_t *g_fb; /* what the counter is drawn into */
static void glyph(int gi, int x0, int y0, int sc, int w, int h, uint32_t col) {
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++)
            if ((k_glyph[gi] >> ((4 - r) * 3 + (2 - c))) & 1)
                for (int yy = 0; yy < sc; yy++)
                    for (int xx = 0; xx < sc; xx++) {
                        int x = x0 + c * sc + xx, y = y0 + r * sc + yy;
                        if (x >= 0 && x < w && y >= 0 && y < h) g_fb[y * w + x] = col;
                    }
}
/* "60 FPS 16.7" (frames per second, milliseconds per frame) in the top right corner of a w x h picture: drawn into
   g_fb at (ox, oy) offset (the picture itself: 0, 0; a small layer: minus its corner); returns 0 if not shown */
static int g_fps_shown = -1; static double g_fps_ms;
static void fps_count(void) {
    static LARGE_INTEGER freq, t0; static int frames;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); t0 = now; }
    frames++;
    double el = (double)(now.QuadPart - t0.QuadPart) / freq.QuadPart;
    if (el >= 0.5) { g_fps_shown = (int)(frames / el + 0.5); g_fps_ms = el * 1000.0 / frames; frames = 0; t0 = now; }
}
typedef struct { int g[16], n, sc, x0, y0, tw, th, pad; uint32_t col; } fps_lay;
static int fps_layout(int w, int h, fps_lay *f) {
    if (!g_fps_on || g_fps_shown < 0) return 0;
    char num[12]; wsprintfA(num, "%d", g_fps_shown);
    f->n = 0;
    for (char *p = num; *p; p++) f->g[f->n++] = *p - '0';
    f->g[f->n++] = 13; f->g[f->n++] = 10; f->g[f->n++] = 11; f->g[f->n++] = 12; f->g[f->n++] = 13;
    int tenths = (int)(g_fps_ms * 10 + 0.5);
    wsprintfA(num, "%d", tenths / 10);
    for (char *p = num; *p && f->n < 14; p++) f->g[f->n++] = *p - '0';
    f->g[f->n++] = 14; f->g[f->n++] = tenths % 10;
    f->sc = h >= 960 ? 3 : h >= 600 ? 2 : 1;
    f->tw = f->n * 4 * f->sc - f->sc; f->th = 5 * f->sc; f->pad = 2 * f->sc;
    f->x0 = w - f->tw - f->pad * 3; f->y0 = f->pad * 2;
    f->col = g_fps_shown >= 50 ? 0xFF80FF80u : g_fps_shown >= 28 ? 0xFFFFE070u : 0xFFFF7070u;
    return 1;
}
static void draw_fps(int w, int h, int count_only) {
    fps_count();
    fps_lay f;
    if (count_only || !fps_layout(w, h, &f)) return;
    for (int y = f.y0 - f.pad; y < f.y0 + f.th + f.pad; y++)
        for (int x = f.x0 - f.pad; x < f.x0 + f.tw + f.pad; x++)
            if (x >= 0 && x < w && y >= 0 && y < h) { uint32_t v = g_fb[y * w + x]; g_fb[y * w + x] = 0xFF000000u | ((v >> 2) & 0x3F3F3F); }
    for (int i = 0; i < f.n; i++) glyph(f.g[i], f.x0 + i * 4 * f.sc, f.y0, f.sc, w, h, f.col);
}
/* the counter as a small layer of its own (GPU path): g_fpspx, g_fps_w x g_fps_h at g_fps_x, g_fps_y */
static void build_fps_layer(int w, int h) {
    fps_count();
    fps_lay f;
    g_fps_vis = 0;
    if (!fps_layout(w, h, &f)) return;
    int bw = f.tw + 2 * f.pad, bh = f.th + 2 * f.pad;
    if (bw * bh > (int)(sizeof g_fpspx / 4)) return;
    for (int i = 0; i < bw * bh; i++) g_fpspx[i] = 0xC0000000u; /* three quarters black */
    g_fb = g_fpspx;
    for (int i = 0; i < f.n; i++) glyph(f.g[i], f.pad + i * 4 * f.sc, f.pad, f.sc, bw, bh, f.col);
    g_fps_w = bw; g_fps_h = bh; g_fps_x = f.x0 - f.pad; g_fps_y = f.y0 - f.pad; g_fps_vis = 1;
}

/* ---------------- screenshots ---------------- */
int dsp_write_png(const char *path, const uint32_t *px, int w, int h);
typedef struct { uint32_t *px; int w, h; char path[MAX_PATH]; } shot_t;
static DWORD WINAPI shot_thread(void *arg) {
    shot_t *s = arg;
    int ok = dsp_write_png(s->path, s->px, s->w, s->h);
    dsp_log("screenshot %s: %s", ok ? "saved" : "FAILED", s->path);
    HeapFree(GetProcessHeap(), 0, s->px);
    HeapFree(GetProcessHeap(), 0, s);
    return 0;
}
static void take_shot_buf(const uint32_t *buf, int w, int h);
static void take_shot(int w, int h) { take_shot_buf(g_rgb, w, h); }
static void take_shot_buf(const uint32_t *buf, int w, int h) {
    static DWORD last;
    if (last && GetTickCount() - last < 400) return; /* the key message and the key watch can both report one press */
    last = GetTickCount();
    shot_t *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *s);
    s->px = HeapAlloc(GetProcessHeap(), 0, (size_t)w * h * 4);
    if (!s->px) { HeapFree(GetProcessHeap(), 0, s); return; }
    memcpy(s->px, buf, (size_t)w * h * 4);
    s->w = w; s->h = h;
    GetModuleFileNameA(NULL, s->path, MAX_PATH);
    char *sl = strrchr(s->path, '\\');
    if (sl) sl[1] = 0;
    strcat(s->path, "Screenshots");
    CreateDirectoryA(s->path, NULL);
    SYSTEMTIME t; GetLocalTime(&t);
    char *e = s->path + strlen(s->path);
    wsprintfA(e, "\\nox-%04d%02d%02d-%02d%02d%02d-%03d.png", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    HANDLE th = CreateThread(NULL, 0, shot_thread, s, 0, NULL);
    if (th) CloseHandle(th); else shot_thread(s);
}
/* the window message can be missed while DirectInput has the keyboard: also watch the keys themselves (the
   screenshot and the frame counter keys); window and key state are calls into the window server, slow under
   Wine, so ~30 times a second */
static void poll_keys(void) {
    static int was_shot, was_fps;
    static DWORD last;
    DWORD t = GetTickCount();
    if (t - last < 30) return;
    last = t;
    int fg = GetForegroundWindow() == g_hwnd;
    int down = fg && g_shot_key && (GetAsyncKeyState(g_shot_key) & 0x8000);
    if (was_shot && !down) InterlockedExchange(&g_shot_req, 1);
    was_shot = down;
    down = fg && (GetAsyncKeyState(g_fps_key) & 0x8000);
    if (was_fps && !down) InterlockedExchange(&g_fps_toggle, 1);
    was_fps = down;
}

/* after a failed frame: if the device was lost, Direct3D is set up again at the next frame (up to 3 times) */
static int d3d_lost(void) {
    if (!g_dev_lost) return 0;
    g_dev_lost = 0;
    d3d_release();
    if (g_d3d_retries >= 3) { g_d3d_state = -1; dsp_log("Direct3D 11: device lost again: GDI"); return 0; }
    g_d3d_retries++;
    g_d3d_state = 0;
    dsp_log("Direct3D 11: device lost, setting it up again (%d)", g_d3d_retries);
    return 1;
}

void dsp_present(const uint16_t *px, int pitch, int w, int h) {
    if (!g_hwnd || !px || w <= 0 || h <= 0) return;
    read_cfg();
    if (!g_lut_ok) build_lut();
    if (w != g_rgb_w || h != g_rgb_h) {
        HeapFree(GetProcessHeap(), 0, g_rgb);
        g_rgb = HeapAlloc(GetProcessHeap(), 0, (size_t)w * h * 4);
        g_rgb_w = w; g_rgb_h = h;
    }
    for (int y = 0; y < h; y++) {
        const uint16_t *s = (const uint16_t *)((const uint8_t *)px + y * pitch);
        uint32_t *d = g_rgb + y * w;
        for (int x = 0; x < w; x++) d[x] = g_lut[s[x]];
    }
    poll_keys();
    if (InterlockedExchange(&g_shot_req, 0)) take_shot(w, h); /* before the counter: not in screenshots */
    {
        static DWORD last;
        if (InterlockedExchange(&g_fps_toggle, 0) && GetTickCount() - last > 300) { g_fps_on = !g_fps_on; last = GetTickCount(); }
    }
    g_fb = g_rgb;
    draw_fps(w, h, 0);
    if (g_d3d_state == 0) g_d3d_state = d3d_init() ? 1 : -1;
    if (g_d3d_state == 1 && !d3d_frame(w, h) && !d3d_lost()) {
        dsp_log("Direct3D 11 frame failed: switching to GDI");
        d3d_release();
        g_d3d_state = -1;
    }
    if (g_d3d_state == -1) gdi_frame(w, h);
}

/* ---------------- two layers: the world (scaled with the chosen filter) and the GUI on top ---------------- */
static int ensure_buf(uint32_t **b, int *bw, int *bh, int w, int h) {
    if (*b && *bw == w && *bh == h) return 1;
    HeapFree(GetProcessHeap(), 0, *b);
    *b = HeapAlloc(GetProcessHeap(), 0, (size_t)w * h * 4);
    *bw = w; *bh = h;
    return *b != NULL;
}
/* a texture of the right size: dynamic (CPU writes) or a render target */
static int tex_ensure(tex_t *x, int w, int h, DXGI_FORMAT fmt, int rt) {
    if (x->t && x->w == w && x->h == h) return 1;
    tex_rel(x);
    D3D11_TEXTURE2D_DESC td;
    memset(&td, 0, sizeof td);
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = fmt; td.SampleDesc.Count = 1;
    if (rt) { td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; }
    else { td.Usage = D3D11_USAGE_DYNAMIC; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; }
    if (FAILED(g_dev->lpVtbl->CreateTexture2D(g_dev, &td, NULL, &x->t))) return 0;
    if (FAILED(g_dev->lpVtbl->CreateShaderResourceView(g_dev, (ID3D11Resource *)x->t, NULL, &x->s))) { tex_rel(x); return 0; }
    if (rt && FAILED(g_dev->lpVtbl->CreateRenderTargetView(g_dev, (ID3D11Resource *)x->t, NULL, &x->r))) { tex_rel(x); return 0; }
    x->w = w; x->h = h;
    return 1;
}
/* rows of bpp-byte pixels (pitch in bytes) into a dynamic texture */
static int tex_fill(tex_t *x, const void *px, int pitch, int w, int h, int bpp, DXGI_FORMAT fmt) {
    if (!tex_ensure(x, w, h, fmt, 0)) return 0;
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(g_ctx->lpVtbl->Map(g_ctx, (ID3D11Resource *)x->t, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return 0;
    for (int y = 0; y < h; y++) memcpy((uint8_t *)m.pData + y * m.RowPitch, (const uint8_t *)px + (size_t)y * pitch, (size_t)w * bpp);
    g_ctx->lpVtbl->Unmap(g_ctx, (ID3D11Resource *)x->t, 0);
    return 1;
}
static int upload(ID3D11Texture2D **tex, ID3D11ShaderResourceView **srv, int *tw, int *th, const uint32_t *px, int w, int h) {
    tex_t x = {*tex, *srv, NULL, *tw, *th};
    int ok = tex_fill(&x, px, w * 4, w, h, 4, DXGI_FORMAT_B8G8R8A8_UNORM);
    *tex = x.t; *srv = x.s; *tw = x.w; *th = x.h;
    return ok;
}

/* the cursor captures (over black and over white) -> BGRA with coverage: where both are equal the pixel is solid;
   where they differ by the whole range it is not there; in between it is blended (alpha from the difference) */
static void cursor_convert(const dsp_layers *L) {
    int n = L->cur_w * L->cur_h;
    if (n > g_curpx_n) { HeapFree(GetProcessHeap(), 0, g_curpx); g_curpx = HeapAlloc(GetProcessHeap(), 0, (size_t)n * 4); g_curpx_n = g_curpx ? n : 0; }
    if (!g_curpx) return;
    for (int y = 0; y < L->cur_h; y++) {
        const uint16_t *a = L->cur_a + (size_t)y * L->cur_pitch, *b = L->cur_b + (size_t)y * L->cur_pitch;
        uint32_t *d = g_curpx + (size_t)y * L->cur_w;
        for (int x = 0; x < L->cur_w; x++) {
            uint16_t va = a[x], vb = b[x];
            if (va == vb) { d[x] = g_lut[va]; continue; }
            int ga = (va >> 5) & 63, gb = (vb >> 5) & 63;
            int al = 255 - (gb - ga) * 255 / 63;
            if (al <= 4) { d[x] = 0; continue; }
            if (al > 255) al = 255;
            /* over black the pixel is colour * alpha: undo it */
            uint32_t c = g_lut[va];
            int r = ((c >> 16) & 255) * 255 / al, g = ((c >> 8) & 255) * 255 / al, bl = (c & 255) * 255 / al;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (bl > 255) bl = 255;
            d[x] = ((uint32_t)al << 24) | (r << 16) | (g << 8) | bl;
        }
    }
}

/* a layer texture drawn over the GUI rectangle (x, y, w, h in GUI pixels) */
static void draw_overlay(tex_t *t, ID3D11SamplerState *smp, int vx, int vy, int vw, int vh, int gw, int gh, int x, int y, int w, int h) {
    D3D11_VIEWPORT v = {vx + (float)x * vw / gw, vy + (float)y * vh / gh, (float)w * vw / gw, (float)h * vh / gh, 0, 1};
    g_ctx->lpVtbl->RSSetViewports(g_ctx, 1, &v);
    g_ctx->lpVtbl->PSSetShaderResources(g_ctx, 0, 1, &t->s);
    g_ctx->lpVtbl->PSSetSamplers(g_ctx, 0, 1, &smp);
    g_ctx->lpVtbl->Draw(g_ctx, 3, 0);
}

/* gui: the GUI is new this frame (else the last one is shown again); gpu: the conversion on the GPU */
static int d3d_frame_layers(const dsp_layers *L, int gui, int gpu) {
    if (!g_psg || !g_samp_lin || !g_blend || !filter_check()) return 0;
    if (!check_resize()) return 0;
    double t0 = tnow();
    ID3D11ShaderResourceView *wsrv, *gsrv, *none[3] = {NULL, NULL, NULL};
    g_ctx->lpVtbl->PSSetShaderResources(g_ctx, 0, 3, none);
    g_ctx->lpVtbl->IASetInputLayout(g_ctx, NULL);
    g_ctx->lpVtbl->IASetPrimitiveTopology(g_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->lpVtbl->VSSetShader(g_ctx, g_vs, NULL, 0);
    g_ctx->lpVtbl->OMSetBlendState(g_ctx, NULL, NULL, 0xFFFFFFFF);
    if (gpu) {
        if (g_lut_dirty) { g_ctx->lpVtbl->UpdateSubresource(g_ctx, (ID3D11Resource *)g_lut_tex, 0, NULL, g_lut, 256 * 4, 0); g_lut_dirty = 0; }
        if (!tex_fill(&g_w16, L->world + (size_t)L->wy * (L->wpitch / 2) + L->wx, L->wpitch, L->ww, L->wh, 2, DXGI_FORMAT_R16_UINT)) return 0;
        if (gui) {
            if (!tex_fill(&g_g16, L->gui, L->gpitch, L->gw, L->gh, 2, DXGI_FORMAT_R16_UINT)) return 0;
            if (!tex_fill(&g_s16, L->snap, L->gw * 2, L->gw, L->gh, 2, DXGI_FORMAT_R16_UINT)) return 0;
        }
        double t1 = tnow(); g_st.upload += t1 - t0; t0 = t1;
        if (!tex_ensure(&g_rtw, L->ww, L->wh, DXGI_FORMAT_B8G8R8A8_UNORM, 1)) return 0;
        if (gui && !tex_ensure(&g_rtg, L->gw, L->gh, DXGI_FORMAT_B8G8R8A8_UNORM, 1)) return 0;
        D3D11_VIEWPORT cv = {0, 0, (float)L->ww, (float)L->wh, 0, 1};
        ID3D11ShaderResourceView *in[3] = {g_w16.s, NULL, g_lut_srv};
        g_ctx->lpVtbl->OMSetRenderTargets(g_ctx, 1, &g_rtw.r, NULL);
        g_ctx->lpVtbl->RSSetViewports(g_ctx, 1, &cv);
        g_ctx->lpVtbl->PSSetShader(g_ctx, g_pscw, NULL, 0);
        g_ctx->lpVtbl->PSSetShaderResources(g_ctx, 0, 3, in);
        g_ctx->lpVtbl->Draw(g_ctx, 3, 0);
        if (gui) {
            D3D11_VIEWPORT gv = {0, 0, (float)L->gw, (float)L->gh, 0, 1};
            in[0] = g_g16.s; in[1] = g_s16.s;
            g_ctx->lpVtbl->OMSetRenderTargets(g_ctx, 1, &g_rtg.r, NULL);
            g_ctx->lpVtbl->RSSetViewports(g_ctx, 1, &gv);
            g_ctx->lpVtbl->PSSetShader(g_ctx, g_pscg, NULL, 0);
            g_ctx->lpVtbl->PSSetShaderResources(g_ctx, 0, 3, in);
            g_ctx->lpVtbl->Draw(g_ctx, 3, 0);
        }
        g_ctx->lpVtbl->PSSetShaderResources(g_ctx, 0, 3, none);
        wsrv = g_rtw.s; gsrv = g_rtg.s;
        if (!gsrv || g_rtg.w != L->gw || g_rtg.h != L->gh) return 0;
    } else {
        if (!upload(&g_tex, &g_srv, &g_tex_w, &g_tex_h, g_rgb, L->ww, L->wh)) return 0;
        if (gui && !upload(&g_texg, &g_srvg, &g_texg_w, &g_texg_h, g_gui, L->gw, L->gh)) return 0;
        if (!g_srvg) return 0;
        double t1 = tnow(); g_st.upload += t1 - t0; t0 = t1;
        wsrv = g_srv; gsrv = g_srvg;
    }
    /* the cursor: converted again only when its picture changed (or the gamma) */
    int cur = L->cur_w > 0 && L->cur_h > 0 && L->cur_a && L->cur_b;
    if (cur && (L->cur_serial != g_cur_serial || g_cur_gamma != g_gamma_gen || !g_tcur.t)) {
        cursor_convert(L);
        if (!g_curpx || !tex_fill(&g_tcur, g_curpx, L->cur_w * 4, L->cur_w, L->cur_h, 4, DXGI_FORMAT_B8G8R8A8_UNORM)) cur = 0;
        else { g_cur_serial = L->cur_serial; g_cur_gamma = g_gamma_gen; }
    }
    if (cur && (g_tcur.w != L->cur_w || g_tcur.h != L->cur_h)) cur = 0;
    if (gpu && g_fps_vis && !tex_fill(&g_tfps, g_fpspx, g_fps_w * 4, g_fps_w, g_fps_h, 4, DXGI_FORMAT_B8G8R8A8_UNORM)) g_fps_vis = 0;

    static const float black[4] = {0, 0, 0, 1};
    g_ctx->lpVtbl->OMSetRenderTargets(g_ctx, 1, &g_rtv, NULL);
    g_ctx->lpVtbl->ClearRenderTargetView(g_ctx, g_rtv, black);
    int x, yy, vw, vh;
    fit(g_sc_w, g_sc_h, L->fw, L->fh, &x, &yy, &vw, &vh);
    D3D11_VIEWPORT vp = {(float)x, (float)yy, (float)vw, (float)vh, 0, 1};
    g_ctx->lpVtbl->RSSetViewports(g_ctx, 1, &vp);
    /* the world: one pass from its own size to the screen, with the chosen filter */
    float cb[8] = {(float)L->ww, (float)L->wh, 1.0f / L->ww, 1.0f / L->wh, (float)vw / L->ww, (float)vh / L->wh, 0, 0};
    if (memcmp(cb, g_cb_last, sizeof cb)) {
        g_ctx->lpVtbl->UpdateSubresource(g_ctx, (ID3D11Resource *)g_cb, 0, NULL, cb, 0, 0);
        memcpy(g_cb_last, cb, sizeof cb);
    }
    g_ctx->lpVtbl->PSSetConstantBuffers(g_ctx, 0, 1, &g_cb);
    g_ctx->lpVtbl->PSSetShader(g_ctx, g_ps, NULL, 0);
    g_ctx->lpVtbl->PSSetShaderResources(g_ctx, 0, 1, &wsrv);
    g_ctx->lpVtbl->PSSetSamplers(g_ctx, 0, 1, &g_samp);
    g_ctx->lpVtbl->Draw(g_ctx, 3, 0);
    /* the GUI: smooth, blended by its coverage */
    static const float bf[4] = {0, 0, 0, 0};
    g_ctx->lpVtbl->OMSetBlendState(g_ctx, g_blend, bf, 0xFFFFFFFF);
    g_ctx->lpVtbl->PSSetShader(g_ctx, g_psg, NULL, 0);
    g_ctx->lpVtbl->PSSetShaderResources(g_ctx, 0, 1, &gsrv);
    g_ctx->lpVtbl->PSSetSamplers(g_ctx, 0, 1, &g_samp_lin);
    g_ctx->lpVtbl->Draw(g_ctx, 3, 0);
    /* the cursor where the mouse is now, then the frame counter */
    if (cur) draw_overlay(&g_tcur, g_samp_lin, x, yy, vw, vh, L->gw, L->gh, L->cur_x, L->cur_y, L->cur_w, L->cur_h);
    if (gpu && g_fps_vis && g_tfps.t)
        draw_overlay(&g_tfps, g_samp_pt ? g_samp_pt : g_samp_lin, x, yy, vw, vh, L->gw, L->gh, g_fps_x, g_fps_y, g_fps_w, g_fps_h);
    g_ctx->lpVtbl->OMSetBlendState(g_ctx, NULL, NULL, 0xFFFFFFFF);
    double t1 = tnow(); g_st.draw += t1 - t0; t0 = t1;
    HRESULT hr = g_sc->lpVtbl->Present(g_sc, g_vsync ? 1 : 0, 0);
    g_st.present += tnow() - t0;
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) { dsp_log("device lost %08lX", hr); g_dev_lost = 1; return 0; }
    return 1;
}

/* the layers converted on the CPU: the world into g_rgb, the GUI (unless world_only) into g_gui with its coverage */
static int cpu_convert(const dsp_layers *L, int world_only) {
    if (!ensure_buf(&g_rgb, &g_rgb_w, &g_rgb_h, L->ww, L->wh) || !ensure_buf(&g_gui, &g_gui_w, &g_gui_h, L->gw, L->gh)) return 0;
    int wp = L->wpitch / 2, gp = L->gpitch / 2;
    for (int y = 0; y < L->wh; y++) {
        const uint16_t *s = L->world + (size_t)(L->wy + y) * wp + L->wx;
        uint32_t *d = g_rgb + (size_t)y * L->ww;
        for (int x = 0; x < L->ww; x++) d[x] = g_lut[s[x]];
    }
    if (world_only) return 1;
    const uint8_t *mask = L->mask;
    if (!mask) {
        int n = L->gw * L->gh;
        if (n > g_cmask_n) { HeapFree(GetProcessHeap(), 0, g_cmask); g_cmask = HeapAlloc(GetProcessHeap(), 0, n); g_cmask_n = g_cmask ? n : 0; }
        if (!g_cmask || !L->snap) return 0;
        for (int y = 0; y < L->gh; y++) {
            const uint16_t *c = L->gui + (size_t)y * gp, *sn = L->snap + (size_t)y * L->gw;
            uint8_t *m = g_cmask + (size_t)y * L->gw;
            for (int x = 0; x < L->gw; x++) m[x] = c[x] != sn[x];
        }
        mask = g_cmask;
    }
    for (int y = 0; y < L->gh; y++) {
        const uint16_t *s = L->gui + (size_t)y * gp;
        const uint8_t *m = mask + (size_t)y * L->gw;
        uint32_t *d = g_gui + (size_t)y * L->gw;
        for (int x = 0; x < L->gw; x++) d[x] = m[x] ? g_lut[s[x]] : (g_lut[s[x]] & 0x00FFFFFFu);
    }
    return 1;
}

/* both layers into one picture on the CPU (nearest): for GDI and screenshots */
static uint32_t *compose_cpu(const dsp_layers *L) {
    if (!ensure_buf(&g_comp, &g_comp_w, &g_comp_h, L->fw, L->fh)) return NULL;
    for (int y = 0; y < L->fh; y++) {
        const uint32_t *wr = g_rgb + (size_t)(y * L->wh / L->fh) * L->ww;
        int gy = y * L->gh / L->fh;
        const uint32_t *gr = g_gui + (size_t)gy * L->gw;
        uint32_t *o = g_comp + (size_t)y * L->fw;
        for (int x = 0; x < L->fw; x++) {
            uint32_t g = gr[x * L->gw / L->fw];
            o[x] = (g >> 24) ? (g | 0xFF000000u) : wr[x * L->ww / L->fw];
        }
    }
    return g_comp;
}

static void stats(int world_only) {
    double t = tnow();
    if (!g_st.t0) g_st.t0 = t;
    if (world_only) g_st.wonly++; else g_st.full++;
    if (t - g_st.t0 >= 10000) {
        int n = g_st.full + g_st.wonly;
        dsp_log("present (%s): %.1f pictures/s (%d full, %d world only), per picture: conversion %.2f ms, upload %.2f, draw %.2f, Present %.2f",
                g_gpu ? "GPU conversion" : "CPU conversion", n * 1000.0 / (t - g_st.t0), g_st.full, g_st.wonly,
                g_st.conv / n, g_st.upload / n, g_st.draw / n, g_st.present / n);
        memset(&g_st, 0, sizeof g_st);
        g_st.t0 = t;
    }
}

/* world_only: the high frame rate's extra pictures: only the world (and the cursor's place) is new, the GUI layer
   of the last full frame is shown again over it; the frame counter only counts */
static void present_layers(const dsp_layers *L, int world_only) {
    if (!g_hwnd || !L || !L->world || L->ww <= 0 || L->wh <= 0 || L->gw <= 0 || L->gh <= 0) return;
    if (!world_only && (!L->gui || (!L->mask && !L->snap))) return;
    read_cfg();
    if (!g_lut_ok) build_lut();
    if (g_d3d_state == 0) g_d3d_state = d3d_init() ? 1 : -1;
    int gpu = g_d3d_state == 1 && g_gpu && (world_only || L->snap);
    if (world_only && gpu && (!g_rtg.t || g_rtg.w != L->gw || g_rtg.h != L->gh)) return;
    if (world_only && !gpu && (!g_gui || g_gui_w != L->gw || g_gui_h != L->gh)) return;
    poll_keys();
    int shot = !world_only && InterlockedExchange(&g_shot_req, 0);
    double t0 = tnow();
    if ((!gpu || shot) && !cpu_convert(L, world_only)) return;
    if (shot) { uint32_t *c = compose_cpu(L); if (c) take_shot_buf(c, L->fw, L->fh); }
    {
        static DWORD last;
        if (InterlockedExchange(&g_fps_toggle, 0) && GetTickCount() - last > 300) { g_fps_on = !g_fps_on; last = GetTickCount(); }
    }
    if (gpu) build_fps_layer(L->gw, L->gh);
    else { g_fb = g_gui; draw_fps(L->gw, L->gh, world_only); }
    g_st.conv += tnow() - t0;
    if (g_d3d_state == 1 && !d3d_frame_layers(L, !world_only, gpu)) {
        if (d3d_lost()) return;
        if (g_d3d_state == 1 && gpu) { /* first the conversion on the CPU again (the next full frame), then GDI */
            dsp_log("Direct3D 11 frame with the GPU conversion failed: CPU conversion from now on");
            g_gpu = 0;
            return;
        }
        if (g_d3d_state == 1) {
            dsp_log("Direct3D 11 layered frame failed: switching to GDI");
            d3d_release();
            g_d3d_state = -1;
        }
        if (gpu) return; /* nothing converted on the CPU for this one */
    }
    if (g_d3d_state == -1) {
        uint32_t *c = compose_cpu(L);
        if (!c) return;
        /* gdi_frame shows g_rgb: show the composed picture instead */
        uint32_t *keep = g_rgb; g_rgb = c;
        gdi_frame(L->fw, L->fh);
        g_rgb = keep;
    }
    stats(world_only);
}

void dsp_present_layers(const dsp_layers *L) { present_layers(L, 0); }
__declspec(dllexport) void __cdecl OncPresentWorld(const dsp_layers *L) { present_layers(L, 1); }

/* ---------------- settings from Nox Modernized's in-game menu (mss32.dll) ---------------- */
__declspec(dllexport) int __cdecl OncDisplayGet(const char *k) {
    read_cfg();
    if (!strcmp(k, "filter")) return g_filter;
    if (!strcmp(k, "scale")) return g_scalemode;
    if (!strcmp(k, "vsync")) return g_vsync;
    if (!strcmp(k, "fps")) return g_fps_on;
    if (!strcmp(k, "d3d")) return g_d3d_state == 1;
    if (!strcmp(k, "gpu")) return g_d3d_state == 1 && g_gpu;
    return -1;
}
__declspec(dllexport) void __cdecl OncDisplaySet(const char *k, int v) {
    read_cfg();
    if (!strcmp(k, "filter") && v >= 0 && v <= F_LANCZOS && v != g_filter) {
        g_filter = v;
        InterlockedExchange(&g_filter_dirty, 1); /* the shader again, at the next frame */
    } else if (!strcmp(k, "scale") && v >= 0 && v <= S_STRETCH) {
        g_scalemode = v;
    } else if (!strcmp(k, "vsync")) {
        g_vsync = v != 0;
    } else if (!strcmp(k, "fps")) {
        g_fps_on = v != 0;
    }
}

void dsp_shutdown(void) {
    ClipCursor(NULL);
    /* the process is ending: the driver cleans up; releasing here can deadlock in DllMain */
}
