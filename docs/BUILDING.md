# Building Nox Modernized

Nox Modernized is plain C (C11) for 32-bit Windows, built with MinGW-w64. It needs no other libraries than the ones
that come with Windows (Direct3D 11, DirectInput, XInput, comctl32).

## Toolchain

- Linux (Debian / Ubuntu): `sudo apt install mingw-w64` (the `i686-w64-mingw32-*` tools are used).
- Windows: MSYS2 with the `mingw-w64-i686-gcc` package, run from a shell that has `sh`.

## Build

From the repository's root:

```sh
sh src/build.sh            # src/out/mss32.dll (and mss32-syms.dll: the same with symbols, for reading crash logs)
sh src/display/build.sh    # src/out/ddraw.dll
sh src/launcher/build.sh   # src/out/NoxModernized-Settings.exe
```

To try a build, copy `mss32.dll`, `ddraw.dll` and `NoxModernized-Settings.exe` over the ones of an installed copy
of Nox Modernized and start the game with `Launch.bat` as usual.

## What is where

| Folder | |
|---|---|
| `src/loader` | `mss32.dll`: a proxy of the Miles Sound System DLL that `Game.exe` loads from its own folder. It forwards the Miles calls to the game's real `mss32.dll` (in the Nox folder, the working directory), and patches `Game.exe` in memory: every patch checks the original bytes first and is left out if they differ. Also the DirectInput proxy (the gamepad is fed to the game as keyboard / mouse input), the settings file and log, the crash logger. |
| `src/game/input` | gamepad polling, the button mapping, lock-on aiming |
| `src/game/ui` | menus, inventory, shops, book, radial wheel, hints and labels, the F7 menu, autosaves |
| `src/game/video` | resolution, UI scale and zoom (split rendering), smooth camera, high frame rate, lights, edge smoothing |
| `src/game/world` | lock-on targets and facing, carry weight, game fixes |
| `src/display` | `ddraw.dll`: an emulation of the DirectDraw calls Nox makes, presenting with Direct3D 11 |
| `src/launcher` | `NoxModernized-Settings.exe` (Win32) |
| `src/ddraw_recon` | a logging DirectDraw wrapper used once to learn how the game presents its frames (not part of the release) |
| `src/tools` | `gen_mss32.py`: generates the Miles forwarding from `loader/mss32-exports.txt` |
| `docs` | notes on the game: addresses, structures and findings (`01-input`, `02-gui`, `03-graphics`, `04-game-notes`) |
| `tools/art`, `media` | the icon and banners: HTML / SVG sources and the rendered images (the settings tool's icon and banner strip are in `src/launcher`) |
| `tools/re-tools` | small Python scripts (capstone + pefile) to disassemble `Game.exe` and find callers and data uses |

Each source file starts with a comment that says what it hooks in `Game.exe` and why; those comments are the most
exact description of the addresses used. All addresses are those of the GOG `Game.exe` of 2000-07-21
(MD5 `62cce922af5f6b5a02109d45859a738d`).
