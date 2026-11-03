# 04 Game notes (Game.exe, GOG 2000-07-21)

More findings about the game. The source comments stay the authority (each module's header says what it hooks).

## Player, facing, attacks
- Player server object (solo / host): drawable *0x96A6A0 -> net code (+128) -> object 0x4EBC10(code).
  Object: position floats +0x38 / +0x3C, facing +0x7C (0..255; direction table 0x5B41F8: cos, sin per entry,
  8 bytes), update data +748 (player: +88 state, 1 = attacking; +216 spell cast in progress).
- Arrows (and fireballs) leave along the facing (+0x7C) at the shot frame, late in the ~0.37 s attack; the facing
  follows the cursor only at the end of a game frame. Hence pad.c: press after the facing matches
  (onc_facing_target), hold the cursor until the attack is over (onc_player_attacking).
- Facing is computed from the cursor around the screen centre (0x42F5DA / 0x42F5E6); camera.c redirects that
  centre to the player's real position (smooth camera). Client facing copy 0x68B160.

## Spells
- Spell bar: [0x5A54F8] -> +0xCC current set, 5 slots x 8 bytes (+0 spell id, +4 "cast on oneself" mark);
  last slot used [0x5A54FC]; slot cast 0x45D5A0 -> 0x45D670(slot, 1, mark) (call at 0x45D5E0) sends message 0x79.
- Spell flags table 0x661A00 + id * 80 (has-flags 0x424610(spell, flags), any of them). Every spell's flags
  can be read from thing.bin with opennox-lib (thing.bin order, not Game.exe ids).
  0x4 targeted, 0x8 at location, 0x20 offensive, 0x200 defensive, 0x400 (on oneself), 0x200000 can't target self.
- The mark is set whenever a spell is put on the bar (0x45D730: flags 0x600 -> mark 1) and never cleared; the drop
  call 0x45D7F0(spell, mark, x, y, 0) takes it as an argument. With the mark the server casts on the caster,
  without it on the object under the cursor. So in the original game Lesser Heal & co. can never reach an ally;
  targets.c ([game] ally_spells) drops the mark for targeted + defensive spells when the cursor is aimed
  (mouse play or the pad's right stick cursor) and on the player's creature or another player.
- Blink shows the reverse: put on the bar without the mark, it goes to the object under the cursor (none: no
  effect).

## Drawables, objects under the cursor
- Hovered drawable 0x6F2D48; the player's creatures: 0x477770(net code) != 0 (blue selection circle).
- Drawable class +112 (2 monster, 4 player, 0x80000000 pickable item), subclass +116, flags +120 (0x8000 dead /
  hidden), animation +276 (10 = dying), net code +128, draw function +0x12C, position +12 / +16.
- Client drawable grid 0x6F1ECC: 47 x 47 cells of 128 units (count 0x6F1ED0), filled by 0x47CF60 (no range check
  in the game; the mod clamps it), unlink 0x47CF10; the grid searches 0x47D0A0 / 0x47D150 clamp themselves.
  Charm / heal orbs (charmup.c 0x4CC460, healup.c 0x4CC760) are put on the view's edge when they would start off
  screen: past the grid at wide screens near a map's east / south edge.
- New drawable 0x459D90(type), delete 0x45A000; freed pool memory is filled with 0xAC (0x414230), so a call or
  read at 0xACACACAC means a deleted drawable / object was used.
- Inventory use 0x4657C0(drawable) sends the drawable's net code (+128) at once (message 0x74). Lending a drawable
  another item's code must be undone right after (once the radial wheel kept it: the game later deleted that
  drawable while the grid still pointed at it, a crash).

## Display
- Brightness (Insert / Delete): level 1..10 at 0x599D30, get 0x436470, set 0x4364A0, apply 0x4364D0 (gamma ramp ->
  SetGammaRamp, which ddraw.dll folds into its colour conversion). Saved by the game in nox.cfg ("Gamma = N").
- The floor point (after the tiles, before objects / walls): 0x4C2320(vp) called at 0x46CC8E (floor softening and
  the aim cone hook there). World viewport 0x687378: +0 / +4 screen x / y, +8 / +0xC screen x2 / y2,
  +0x10 / +0x14 world x / y, +0x20 / +0x24 size. Back buffer rows: *(uint16_t ***)0x952C54 (RGB565).

## Other
- Key names 0x430250(action, index) also fill the Input Config list (0x4C5646, 0x4CAFA6); leaving that screen
  rebuilds all key bindings from the list text and saves nox.cfg. pad_labels.c must never answer it.
- Frame counter 0x9667C4; duration spells list loop 0x4FDD40 (shield fix stub at 0x4FDDA3).
- Play a UI sound 0x4528D0(id, volume).
- Winlator: a crash after minutes with "Ask to run at NULL" in the Box64 log was the emulator (the plain GOG game
  crashed too on one Android phone); the Winlator Ludashi build fixed it.

## Ending movie / VQA player
- Play loop 0x557060 (called from 0x554C6A in 0x554930, "About to play VQA"): pumps messages (0x556FF0: PeekMessage,
  GetMessage, Translate, Dispatch), steps the movie (0x559BE0(handle, 1, 0)), Esc / skip flags at 0x837C04 / 0x837C08
  (0x554200). Frames drawn by locking the primary (Lock helper 0x555400, retries on DDERR_SURFACELOST via [0x8383E0]).
- A pause path (0x5572E6 -> 0x557390, VQA pause, sets player +0x614) runs when [0x5CC5F4] == 0, but nothing in Game.exe
  writes 0x5CC5F4 (initialised to 1) and +0x614 is cleared only by the constructor (0x556C20): dead code here, and a
  paused movie would never resume.
- Sound: DirectSound directly (not Miles), a buffer fed by a 16 ms timeSetEvent callback (0x559160, set up 0x558B70,
  killed 0x558DB0) under a critical section; GetStatus / GetCurrentPosition / Restore on DSERR_BUFFERLOST. If the main
  thread stops, the buffer loops its last piece (the "repeating sound" heard when the ending movie froze under the screen saver).

## Facing while the inventory is open
- 0x42F5A0 (inside the client input builder 0x42F020): skipped when [0x68991C] == 2 or [0x689930] != 4, else the
  facing from the cursor around the screen centre -> 0x68B160; 0x42F65D sends 0x68B160 (0x42FFE0(1, facing)) either
  way. 0x68B160 is written only there, so jumping to 0x42F65D keeps the old facing. Inventory root window
  [0x6D56C8] (563 x 264), state byte 0x6D25A0.
- Inventory panel mouse handler 0x464720 (OpenNox sub_464BD0; win, code, x | y << 16 in GUI pixels): code 5 (down)
  on the tray: capture, pick 0x4653F0(panel, point) -> 0x6D258C, cursor item 0x487440(item), sound 0x4528D0(791, 100),
  down point 0x6D5740/44. Code 6 (up): outside the panel -> screen to world 0x46AB40 (at 0x46516C; no UI scale /
  zoom: the mod converts first), from the tray: a stack opens the amount box, else drop 0x465730; inside: move / equip.
- Inventory cell (148 bytes): +0 drawable, +4 the items' net codes (+4 the first), +132 equipped, +140 count. Cells of
  identical items share one drawable; before acting on a cell the game copies its code onto the drawable
  (+0x80 = cell +4: 0x461D55, 0x464919, 0x464A35, 0x465026, 0x465625, 0x466312). Do the same before 0x465730 (drop)
  / 0x4657C0 (use), which send the drawable's code; the server ignores unknown codes without a word
  (MSG_TRY_DROP 0x72; a blocked spot gives "drop.c:DropNotAllowed" and a sound instead).

## Pause menu
- The Esc menu is QuitMenu.wnd, made once by 0x445320 (called from 0x47C9DA) and kept at [0x69BA34]; shown / hidden afterwards, so onc_win_visible([0x69BA34]) = the pause menu is open.
- The round blue inventory button (bottom left): a 28 x 29 child at 5, 11 of [0x6D56D0], made at 0x465BBD, input proc 0x4660A0 (click: inventory toggle 0x4677B0; 0x4677D0 = the inventory is open / opening). Not kept in a global: found by its proc.
