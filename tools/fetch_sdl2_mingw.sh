#!/usr/bin/env bash
# Fetch the official SDL2 MinGW development libraries for the Windows
# cross-build (host/win/SDL2/). Not committed — like the Android SDL vendor,
# it's large and reproducible from this script.
#
#   tools/fetch_sdl2_mingw.sh
#
set -euo pipefail
SDL_VER="${SDL_VER:-2.32.10}"
URL="https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VER}/SDL2-devel-${SDL_VER}-mingw.tar.gz"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/host/win"
mkdir -p "$DEST"
cd "$DEST"

if [ -d "SDL2/x86_64-w64-mingw32" ]; then
    echo "SDL2 mingw libs already present at host/win/SDL2 — delete to refresh."
    exit 0
fi

echo "Downloading SDL2 $SDL_VER mingw devel libs..."
curl -fL "$URL" -o sdl2-mingw.tar.gz
tar xzf sdl2-mingw.tar.gz
rm -f sdl2-mingw.tar.gz
# Normalise to host/win/SDL2/{i686,x86_64}-w64-mingw32/...
rm -rf SDL2
mv "SDL2-${SDL_VER}" SDL2
echo "SDL2 $SDL_VER -> host/win/SDL2/x86_64-w64-mingw32"
ls SDL2/x86_64-w64-mingw32/bin/SDL2.dll
