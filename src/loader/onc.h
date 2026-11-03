/* Nox Modernized - shared declarations. The mod is a proxy mss32.dll that Nox's original Game.exe (GOG) loads
   from its own folder: Miles calls go to the real mss32.dll in the game folder, and our code hooks the game. */
#pragma once
#define DIRECTINPUT_VERSION 0x0700
#include <stdint.h>
#include <windows.h>
#include <dinput.h>

#define ONC_VERSION "0.9.0"

/* Game.exe (GOG, built 2000-07-21) has no ASLR: original addresses are fixed. */
#define ONC_I32(a) (*(volatile int32_t *)(uintptr_t)(a))

void onc_log(const char *fmt, ...);
int  onc_cfg_int(const char *section, const char *key, int def);
void onc_cfg_str(const char *section, const char *key, const char *def, char *out, int n);
int  onc_cfg_float100(const char *section, const char *key, int def); /* value x100, e.g. 0.30 -> 30 */
void onc_cfg_write(const char *section, const char *key, const char *value);

/* Game globals (Game.exe; see docs/01-input.md) */
#define NOX_MOUSE_X     0x697AF0  /* cursor position, game screen pixels */
#define NOX_MOUSE_Y     0x697AF4
#define NOX_MOUSE_MINX  0x697B60  /* cursor bounds = the game screen */
#define NOX_MOUSE_MAXX  0x598EFC
#define NOX_MOUSE_MINY  0x697B64
#define NOX_MOUSE_MAXY  0x598F00
#define NOX_SCREEN_W    0x95410C  /* the screen the game draws (nox_win_width / height) */
#define NOX_SCREEN_H    0x954764
#define NOX_CLIENT_CONNECTED 0x699328 /* in a game (OpenNox nox_client_isConnected; getter 0x43C280) */

/* code patches (loader/hook.c) */
int   onc_hook_jmp(const char *name, uintptr_t addr, void *repl, const uint8_t *expect, size_t n);
void *onc_hook_detour(const char *name, uintptr_t addr, void *repl, const uint8_t *expect, size_t n);
int   onc_hook_call(const char *name, uintptr_t call_addr, void *repl, uintptr_t expect_target);

/* gamepad -> mouse / keyboard events (game/input/pad.c), used by the DirectInput proxy */
void onc_pad_init(void);
int  onc_pad_mouse_event(DIDEVICEOBJECTDATA *out); /* 1 = an injected mouse event was written */
int  onc_pad_key_event(DIDEVICEOBJECTDATA *out);   /* 1 = an injected key event was written */
void onc_real_mouse_moved(void);                   /* the real mouse moved: the pad lets go of the cursor */
void onc_real_input(void);                         /* a real key / mouse button / mouse motion: not the pad */
int  onc_pad_in_use(void);                         /* the pad was used last (labels and hints show its buttons) */
int  onc_tabs_mouse_click(int x, int y);           /* a real left click there: 1 = used by the tab strip */
