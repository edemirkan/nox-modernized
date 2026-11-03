# 01 Input (Game.exe, GOG 2000-07-21)

Imports: DINPUT.dll DirectInputCreateA (IAT 0x57F024, stub 0x56CE60), DDRAW, DSOUND, WINMM, mss32 ...

## Mouse (DirectInput, buffered)
- Init 0x490575: DirectInputCreateA(ver 0x300) -> 0x710AEC; CreateDevice(GUID_SysMouse) -> 0x710AF4;
  SetCooperativeLevel; DIPROP_BUFFERSIZE 0x100; SetDataFormat(c_dfDIMouse 0x581FF0); Acquire.
- 0x4907C0 reads ONE event per call (GetDeviceData, vtable +0x28, count 1) into a 0x38-byte record;
  0x490710 decodes dwOfs: 0 X delta (+0), 4 Y (+4), 8 Z (+8), 12/13/14 buttons (+0x14/+0x20/+0x2C, time +0x1C..).
- 0x432010 fills up to 256 records at 0x6942F0 (stops when no event), then 0x431C10(count) processes them:
  per event 0x432370(dx, dy, abs=0) moves the cursor and clamps it.
- Cursor: X 0x697AF0, Y 0x697AF4. Bounds: minX 0x697B60, maxX 0x598EFC, minY 0x697B64, maxY 0x598F00
  (set by 0x4323E0(minX, maxX, minY, maxY)). No sensitivity applied to the deltas here.
  (OpenNox: nox_client_changeMousePos_430A00 / nox_client_setMousePos_430B10.)

## Keyboard (DirectInput, buffered)
- Device at 0x710AF8; 0x492720 reads ONE event per call: byte 0 = DIK scan code, byte 1 = 2 pressed / 1 released,
  dword +4 = time. 0x432080 collects them into 8-byte records at 0x6932F0.
- Keys go through the key bindings in nox.cfg (default: A/S/D/F/G slots, Space jump, X/C/Z potions, V swap,
  Tab map, E/W spell sets, I inventory, B book, Esc menu, F2 autosave).
- In-game flag: 0x699328 (getter 0x43C280, called 180x; OpenNox 0x5D4594+815764 = 0x69B828). BSS here sits about 0x2500 below OpenNox addresses.
- Screen the game draws: width 0x95410C, height 0x954764. Facing (0x42F5A0..): atan2(cursor - screen/2) -> 0x68B160 (0..255).
  The cursor bounds can stay larger (2560x1440 from nox.cfg while the game draws 1024x768) - do not use them as the screen.

## World targets
- Hover: 0x47EF80 (per frame, rect around the cursor), callback 0x47F030; hovered drawable 0x6F2D48 (getter 0x47EF60),
  sprite under cursor 0x6F2D4C (getter 0x47EF70; used by the click handlers). The input packet (0x43C470) sends the hovered target.
- forEachDrawable(int rect[4] world x0 y0 x1 y1, cb(dr, arg), arg) 0x47D060. Visible 0x47A1A0(dr). My creature 0x477770(net).
- Selection: drawSelection(vp) 0x47F3A0 (hovered), circle(vp, color, dr) 0x47F450; vp +0 / +4 screen min, +0x10 / +0x14 world min.
  Colors: white 0x954794, blue 0x9737E8, cyan 0x973190, red 0x9737C0, violet 0x966830.
- talk(dr) 0x430120 (D0 01 net), trade(dr) 0x4301C0 (C9 15), pickup(dr) 0x489A50 (0x73). Player info 0x96B8D0 (+3680).
- Drawable: +12/+16 world pos, +108 type, +112 class, +116 subclass, +120 flags (0x8000 dead), +128 net code,
  +276 anim state (10 dead monster), +280 type flags (0x10 talks), +368 next on the doll.
- Doll: slot lists 0x6D25A4[9], slot rects 0x5A5F90 (9 x int4, relative to panel + (11, 15)), doll rect 0x5A6020.
