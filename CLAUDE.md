# ThumbyElite — agent notes

Bare-metal Elite/MechWarrior space game for Thumby Color (RP2350 @ 280MHz,
520KB SRAM, 128x128 RGB565). Own git repo (origin:
github.com/austinio7116/ThumbyElite). Full design: PLAN.md.

## Build

```bash
# Host (all development happens here first)
cmake -B build_host -S host && cmake --build build_host -j8
./build_host/thumbyelite_host [seed]
ELITE_SHOT=/tmp/shot.ppm ./build_host/thumbyelite_host 42   # headless screenshot

# Device (standalone)
cmake -S device -B build_device -DPICO_SDK_PATH=$HOME/mp-thumby/lib/pico-sdk
cmake --build build_device -j8
cp build_device/thumbyelite.uf2 ../firmware_thumbyelite.uf2
```

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
