#!/usr/bin/env bash
# One-command Windows build from Linux/WSL2 (MinGW-w64 cross-compile).
# Produces a self-contained, distributable folder:
#   build_win/dist/{ThumbyElite.exe, SDL2.dll, README.txt}
#
# Prereqs (once):
#   sudo apt install mingw-w64 cmake build-essential
#   tools/fetch_sdl2_mingw.sh        # vendors the SDL2 MinGW devel libs
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v x86_64-w64-mingw32-gcc >/dev/null; then
    echo "MinGW-w64 not found. Install it:  sudo apt install mingw-w64" >&2
    exit 1
fi
if [ ! -f host/win/SDL2/x86_64-w64-mingw32/include/SDL2/SDL.h ]; then
    echo "Fetching SDL2 MinGW devel libs..."
    bash tools/fetch_sdl2_mingw.sh
fi

cmake -B build_win -S host \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/host/toolchain-mingw-w64.cmake"
cmake --build build_win -j"$(nproc)"

echo
echo "Windows build ready:  build_win/dist/"
ls -la build_win/dist/
