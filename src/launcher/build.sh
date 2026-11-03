#!/bin/sh
# Builds NoxModernized-Settings.exe (the settings editor) for 32-bit Windows with MinGW-w64.
set -e
cd "$(dirname "$0")"
mkdir -p ../out
i686-w64-mingw32-windres launcher.rc -O coff -o ../out/launcher-res.o
i686-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-const-variable -Wno-unused-variable -municode -mwindows -s \
  -static-libgcc -o ../out/NoxModernized-Settings.exe launcher.c ../out/launcher-res.o -lcomctl32 -lshell32 -lole32 -ladvapi32
echo "built ../out/NoxModernized-Settings.exe"
