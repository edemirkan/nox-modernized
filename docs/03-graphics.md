# Graphics: how Nox presents its frames

Recorded with the recon ddraw.dll (src/ddraw_recon) on the PC, GOG's wrapper underneath.

- DirectDrawCreate(NULL) twice at start: the first only for GetCaps (0x49D844), the second for EnumDisplayModes
  (callback 0x492980), then QueryInterface(IID_IDirectDraw v1) -> the game's DD object (global 0x952C2C).
- Every mode change (menu <-> game) releases everything and starts over with a new DirectDraw object:
  SetCooperativeLevel(hwnd, 0x13 = FULLSCREEN | EXCLUSIVE | ALLOWREBOOT), SetDisplayMode(w, h, 16).
  Modes seen: 640 x 480 x 16 (intro, menus), 1024 x 768 x 16 (the game).
- CreateSurface: flags 0x21 (CAPS | BACKBUFFERCOUNT), caps 0x218 (PRIMARY | FLIP | COMPLEX), 1 back buffer
  (2 with the triple buffer option). EnumAttachedSurfaces gives the back buffer (callback 0x49D5C0).
  Format: 16-bit RGB565 (masks F800 / 07E0 / 001F), pitch = width * 2.
- A clipper with SetHWnd is set on the primary. GammaControl (69C11C3E...) is queried from the primary and
  SetGammaRamp (#4) is called when the gamma setting changes (0x43660C).
- Each frame: Lock / Unlock (about 11 per frame, flags DDLOCK_WAIT), Flip(NULL, DDFLIP_WAIT) on the primary,
  GetFlipStatus(DDGFS_ISFLIPDONE) polled (0x49C99A). The intro movie locks the primary itself.
- Blt with COLORFILL | WAIT clears the primary and the back buffer at start and before a mode change.
- Also used: IsLost, GetSurfaceDesc, RestoreDisplayMode, WaitForVerticalBlank (0x49DD89), FlipToGDISurface (0x49F08C).

What our ddraw.dll must emulate: IDirectDraw v1 (QI, AddRef, Release, GetCaps, EnumDisplayModes,
SetCooperativeLevel, SetDisplayMode, RestoreDisplayMode, CreateSurface, CreateClipper, CreatePalette,
WaitForVerticalBlank, FlipToGDISurface, GetDisplayMode), surfaces (QI for gamma, Blt colour fill / copies, Lock,
Unlock, Flip, GetFlipStatus, GetAttachedSurface, EnumAttachedSurfaces, GetSurfaceDesc, IsLost, Restore,
SetClipper, GetCaps, GetPixelFormat, GetDC / ReleaseDC), the clipper (SetHWnd) and the gamma control
(Get / SetGammaRamp) - the frames in system memory, shown with Direct3D 11 on Flip.
