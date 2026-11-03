#!/bin/bash
# our ddraw.dll -> ../out/ddraw.dll
set -e
cd "$(dirname "$0")"
i686-w64-mingw32-gcc -O2 -s -shared -Wall -Wno-unused-parameter -Wno-incompatible-pointer-types \
  -o ../out/ddraw.dll ddraw_emu.c present.c png.c log.c ddraw.def \
  -static-libgcc -luser32 -lgdi32 -ldxguid -luuid
echo built ../out/ddraw.dll
