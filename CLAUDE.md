# ThumbyElite — agent notes

Bare-metal Elite/MechWarrior space game for Thumby Color (RP2350 @ 280MHz,
520KB SRAM, 128x128 RGB565). Own git repo (origin:
github.com/austinio7116/ThumbyElite). Full design: PLAN.md.

## Build

```bash
# Host (all development happens here first) — one source set, three targets:
cmake -B build_host -S host && cmake --build build_host -j8
./build_host/thumbyelite_host       [seed]   # the game — native 128x128 (SS=1)
./build_host/thumbyelite_host_quad  [seed]   # 512x512 (SS=4) — app-icon tool only
ELITE_SHOT=/tmp/shot.ppm ./build_host/thumbyelite_host 42   # headless screenshot

# Every platform (PC, Android, device) renders at native 128 (SS=1) — the SDL
# surface scales the 128 frame up to the window. This SDL2 host is the
# cross-platform "PC" build — same source compiles on Linux and Windows
# (MSVC/MinGW + SDL2). The SS=4 quad target survives only to render the app
# icon (tools/build_icon.sh); it is not a game build.

# Device (standalone)
cmake -S device -B build_device -DPICO_SDK_PATH=$HOME/mp-thumby/lib/pico-sdk
cmake --build build_device -j8
cp build_device/thumbyelite.uf2 ../firmware_thumbyelite.uf2

# Android (SDL2 APK, arm64) — see android/README.md
(cd android && ANDROID_HOME=/path/to/android-sdk ./gradlew assembleDebug)
```

PC desktop build, Windows instructions, and gamepad/HOTAS controls +
sensitivity: see docs/PC.md.

## Rules

- Host build first; the user flashes for device testing. NEVER claim device
  performance from host runs. Frame-time readout is on screen (top-left).
- Never push without user approval. No Co-Authored-By in commits.
- Renderer convention: camera-relative world (camera = origin), view z
  forward, depth u16 LARGER = NEARER (d = K/z), meshes CCW-from-outside.
- Dual-core (Phase 2+): core0 builds the screen-space draw-list, both cores
  rasterise it banded to their screen half. Park core1 before flash writes.
- SRAM budget table lives in PLAN.md — re-check .bss in the .map when adding
  buffers; framebuffer 32KB + depth 32KB are the fixed renderer cost.

## Current state

Phase 1 complete when: rotating cube field renders on host AND device with
frame-time readout. See PLAN.md "Phases" for the full roadmap.
