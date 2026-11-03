# Under the hood: how Nox Modernized works

A short tour of how the mod works and of what was learned about Nox along the way, for the curious and for anyone
who wants to work on the project. The exact addresses and structures are in the source comments and in `docs`.

## Getting inside the game

Nox Modernized does not change `Game.exe` on disk. Windows loads a program's DLLs from the program's own folder
first, so the mod ships its own `mss32.dll`, the Miles Sound System library the game uses for sound. It passes every
Miles call on to the real library and, once loaded, patches the game in memory. Every patch first checks that the
bytes it replaces are the ones expected and leaves itself out if they are not, so an unexpected version of the game
loses features instead of crashing.

Working on the original executable, rather than on a re-implementation, keeps the game exactly as it was and lets it
run wherever the original runs, Android through Winlator included. OpenNox, the open-source re-implementation of
Nox, was the map: its names for the game's functions and its structure layouts made it possible to find the same
places in the original executable.

## The gamepad

**Moving.** The gamepad reaches the game as mouse and keyboard input through a DirectInput proxy: the left stick
places the cursor on a ring around the player and holds the move button, exactly as a mouse player would. The ring
has to be centred on the screen the game draws, which is not the same rectangle as the cursor's limits.

**Menus.** Rather than steering a cursor by hand, the pad works the game's own window system: it reads the buttons,
lists and sliders of the open menu and moves a selection between them by their positions on the screen, pressing the
selected one the way the keyboard does. The shop window is the one box that is not "modal" like the others, so it is
recognised separately.

**The inventory** is worked through the game's own functions for equipping, using and dropping, and the tabs are a
filtered view placed in the game's own grid while the inventory is open. A few things about the game matter here:

- While the cursor is on the inventory, the player turns to face it, so anything dropped lands towards the
  inventory. The mod keeps the facing the player had when the inventory opened.
- A mouse press and release in the same spot within about a third of a second is a click to the game, which equips
  or uses the item. Carrying an item with the pad has to keep its press and release further apart.
- Several identical items share one picture in the grid, and before acting on an item the game copies that item's
  network code onto the shared picture. The server silently ignores a code it does not expect, so the mod copies
  the code the same way, and gives the picture its own code back right after: otherwise, once the borrowed item is
  used up, the game deletes the shared picture while the grid still uses it.

**Lock-on aiming.** A and the spell buttons aim at the nearest visible enemy in a cone in front of the player, shown
with the game's own selection circle. Two details of the game shape how this works. The player's facing turns
towards the cursor only at the end of a game frame, and arrows and thrown weapons leave along the facing, not
towards the cursor; so the attack waits until the player faces the enemy. And a bow shoots late in its attack
animation, so the cursor stays on the enemy until that attack is over.

**Spells and the "cast on yourself" mark.** When a spell goes on the spell bar, the game gives it a mark that makes
the server cast it on the caster instead of on whatever is under the cursor. The pad's spell book sets this mark as
the game does. The mark also explains an oddity of the original game: Lesser Heal and similar spells can never be
aimed at an ally. The mod leaves the mark off when you aim at your creature or another player.

**Labels and hints.** The HUD shows the pad's buttons where the game shows key names. The game uses the same
function to fill its Input Config screen, and leaving that screen rebuilds every key binding from the names shown,
so the real key names are always kept there.

## Graphics

**A renderer of its own.** Nox draws 16-bit pictures and shows them with DirectDraw. A logging DirectDraw DLL
(`src/ddraw_recon`) recorded exactly which calls the game makes; the mod's `ddraw.dll` imitates those calls and
presents the frames with Direct3D 11, with filters (sharp, bicubic, Lanczos), scaling and PNG screenshots.

**High resolutions.** The game's 1024x768 mode is replaced with the chosen size. Wider screens need more than that:
the game's three light grids and its line-of-sight tables are sized for the original screens, and a wider screen
overflows them into the data stored behind them (the player's own information among it). The mod moves them to
bigger memory. Some effects drawn partly off screen ask the line-of-sight code about rows outside the screen, which
the game never expected, so those rows are answered safely. A sound function that runs during loading also assumes
the player already exists, so it waits.

**UI scale.** To make the HUD bigger without making the world blurry, the game runs at a smaller size and only the
world is drawn at full resolution, into a buffer of its own; the two are put together when the frame is shown. The
game rounds its screen width down to a multiple of 32 pixels and needs at least 640x480 for its menus, which sets
the largest UI scale for each resolution. The world pass needs its own copy of the mouse (with the button states,
or the player cannot walk), its own floor buffer, and clipping switched on, as sprites reaching past the buffer's
edge crash the game's sprite drawer.

**World zoom** draws the world 1:1 into a smaller rectangle that the renderer enlarges with the chosen filter, under
the HUD. Speech bubbles are placed and clipped through the zoom and the UI scale.

**High frame rate.** The game runs at 30 ticks a second. The mod draws extra pictures in the time the game spends
waiting for its next tick, with the camera and every moving unit slid between their last two positions. The map and
on-screen messages are drawn by the game together with the world, so the mod draws them afterwards, on top of the
extra pictures. A profiler built into the mod found the most expensive step, copying the world under the HUD; a
table-driven copy split over helper threads keeps it fast enough for Android.

**Smaller touches.** A smooth camera that eases after the player (the gesture hands, line of sight and walk ring
follow it), gentler fire flicker, map darkness, and sprite, wall and floor edge smoothing.

## Fixes to the original game

- **Force shield.** A cast force shield has no time limit, but loading a saved game turned that "no limit" into one
  second, so the shield vanished right after loading.

## Things the original game did not expect

- **Laser traps while zoomed.** The game checks where a sentry ray starts against the screen's corner, which in the
  original game is always where the view starts. With the world zoom the view sits elsewhere, so some rays were not
  drawn, or drawn wrongly; the mod checks against the view's own edges.
- **Effects at the edge of a map.** The game places some effects (charm and heal orbs) at the edge of the view when
  they would start off screen, into a grid it never range-checks. On wide screens near a map's edge that lies
  outside the grid and crashes the game; the mod clamps it.
- **The game's own joystick support.** Nox can read controllers itself through Windows' old joystick functions.
  With two programs reading the same gamepad a button could act twice, so while the mod's gamepad support is on,
  the game is told that no joystick is connected.
- **The screen saver.** Windows does not count a gamepad as activity, so a screen saver can start while you play
  with one, and starting during the ending movie freezes it. The mod keeps the screen awake while the game is in
  front.

## Tools

The in-game settings menu (F7) shows every change at once. The settings tool explains every setting, maps the
gamepad's buttons per class with button detection, keeps separate settings for Winlator, offers UI scale steps up to
what the chosen resolution allows, on its first start finds the Nox installation, checks it and sets everything up,
and starts the game with its Play button.
