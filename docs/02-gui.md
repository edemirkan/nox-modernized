# 02 Window system (Game.exe)

Window struct: 0x194 bytes, same layout as OpenNox client/gui Window: id +0, status flags +4, w/h +8/+C,
x/y (relative to parent) +10/+14, widget data +20, draw data +24 (style +2C, draw-data window +34), func93
(input proc) +174, func94 (window proc) +178, draw +17C, prev +184, next +188, parent +18C, last child +190.
Procs are cdecl int f(win, code, a1, a2). Key event to func93: code 21, a1 = DIK code, a2 = 2 pressed / 1 released.
A push button's input proc presses on Enter and notifies its draw-data window with func94(0x4007, button, 0).
Sliders (OpenNox): widget data min +0, max +4, value +12; func94(slider, 0x400A, value) sets it, parent gets 0x4009.

Globals: top-level list last 0x6F73E4 / head 0x6F73E8 (0x488230 adds a root); window pool 0x6F73E0 (0x489CF0
creates a window); captured 0x6F73F4 (0x4886D0 sets); focused 0x6F73F8 (0x488E10); modal stack 0x6F73FC
(node {window, next}; pop 0x489FF0). Game flags 0x5D2EB4 (check 0x40A5C0), pause = 0x40000.
Conversation root ids 39xx (3901-3904 text + scroll, 3906 Done, 3908 Yes); shop 38xx (3801-3804 choices).

## Inventory (OpenNox guiinv addresses - 0x2500)
State byte 0x6D25A0 (0 closed, 1 opening, 2 open, 3 closing), journal shown 0x6D25A1. Grid 0x6D2638: 4 x 21
cells x 148 bytes, column-major (cell = row + 21*col), +0 drawable, +140 count. Panel window 0x6D56CC (root
0x6D56C8); its draw puts the tray at panel + (254, 13), grid columns from +60, 50 px cells, 150 px visible.
Scroll offset 0x6D5704, scroll bar 0x6D5700 (widget max at +4; func94(0x400A, max - offset) moves it).
Tray click (OpenNox sub_464BD0): item class (drawable +112) & 0x3001000 -> cell +132 set (equipped) ? dequip :
key-equip(col, row); otherwise use. Functions (cdecl): equip(item) 0x461F00, key-equip(col,row) 0x465780 (acts on
the item in 0x6D258C), dequip(item) 0x4646C0, use(item) 0x4657C0, drop(int2 *world) 0x465730 (item in 0x6D258C),
close inventory 0x467760. Net message send 0x40EAC0(31, 0, buf, len); unit code of a drawable 0x5777E0.
Local player drawable: *0x96A6A0 (world x/y at +12/+16).

## Inventory tabs
- Panel draw 0x462F80 detoured (first 5 bytes `A1 88 5F 5A 00`, absolute); in it the call at 0x462F9B
  (setPos 0x4882C0(root, 0, slide)) is redirected: y += 22 * (slide + 225) / 225, so the closed inventory stays where it was.
- Inventory window tree (root 0x6D56C8): root draw 0x465D10; 40 px screen-wide strip at y 224 (func93 0x4646F0 returns 0,
  tooltip 0x466330); panel 0x6D56CC (func93 0x464720, draw 0x462F80, tooltip 0x466170, func94 0x465D70);
  0x6D56DC (func93 0x464720); doll slot 0x6D56D8 (func93 0x4642C0, draw 0x462120, tooltip 0x465D20); more.
- All procs (0x174/0x178/0x17C/0x180) of the root's subtree are replaced by generic wrappers that swap a filtered view
  into the grid 0x6D2638 for the call (tab items then empty cells, row by row) and write changes back by the map.
- Panel func94 0x465D70: 0x4007 by button id - 9102 (jump table 0x466070); the journal switch 0x6D5714 toggles
  0x6D25A1 (and swaps its id 9104 / 9105). Scroll bar max is fixed at 850 (0x400B range), 0x400A sets the position.
- Render state *0x952FA0: +0 clip on, +4 clip rect, +0xEC text color, +0xF4 rect color. UI sound 0x4528D0(id, vol), click 0x2FE.

## Item comparison
- Identify panel 0x462340 (OpenNox sub_4627F0)(int pos[2]): draws a black 200x200 box at pos+(11,15), sets the cursor
  type 0x6F7300, and when the item 0x6D5960 differs from the shown one 0x6D5964 rebuilds list box 9156 of window
  0x6D56E0 (0x400F clear, 0x400D add line); name into wchar[256] 0x6D5968; icon child 9155 via 0x488770.
  Item type at drawable +0x6C; pretty image 0x44CAE0(type); thing description 0x44CAB0(type).
- We call it with our item, the list box's func94 swapped for a capture proc, clip set to 1 px, everything restored.
- loadString 0x40F0D0(name, 0, file, line). Hit rects: 0x5A6020 / 0x5A6030 (grid) / 0x5A6040 (side column).

## Shops
- Shop on 0x6F32A4, mode 0x6F32A8 (1 choices, 2 buy, 3 sell, 4 repair), window 0x6F3274 (root id 3800, NOT on the modal stack), proc func94 0x47FA40
  (ids 3801 Leave .. 3804 Buy, 3808 / 3809 scroll), input proc buy click -> 0x47FCF0(int pt[2]).
- Vendor grid rect 0x6F31B0 (x0, y0, x1, y1), cells 0x6F32B0: 6 cols x 10 rows x 140 bytes, column-major
  (+0 drawable, +4 count, +136 price). Scroll 0x6F5380, max 0x6F3284, slider 0x6F3278 (0x400A with max - scroll).
- Requests: repair price 0x465300(net) (C9 1E), sell price 0x4653C0(net) (C9 1C); the server answers and the client
  opens the price box (MultMove, 3604 yes / 3605 no). Repair cursor mode: 0x6D259C = 6 (0x4671A0 on, 0x4671D0 off).
- Mapping note: shop BSS = OpenNox 0x5D4594 offsets + 0x11380 (scroll var differs).
