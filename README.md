# ThumbyElite

An open-galaxy space sim for the [Thumby Color](https://color.thumby.us/) —
Elite-style trading and dogfighting with MechWarrior Mercenaries-style ship
outfitting, component salvage and procedural missions, across an infinite
deterministic galaxy. Bare-metal RP2350: flat-shaded polygon 3D at 30fps,
dual-core rendering, 520KB SRAM.

**Status: Phase 1** — renderer proof (flat-shaded depth-tested triangle
pipeline, host + device builds). Roadmap in [PLAN.md](PLAN.md).

## Building

### Host (Linux, SDL2)

```bash
sudo apt install libsdl2-dev cmake build-essential
cmake -B build_host -S host
cmake --build build_host -j8
./build_host/thumbyelite_host [seed]
```

### Device (RP2350)

Needs the pico-sdk (e.g. from the mp-thumby checkout) and
`gcc-arm-none-eabi`.

```bash
cmake -S device -B build_device -DPICO_SDK_PATH=$HOME/mp-thumby/lib/pico-sdk
cmake --build build_device -j8
cp build_device/thumbyelite.uf2 ../firmware_thumbyelite.uf2
```

Flash: power off → hold D-pad DOWN → power on → copy the .uf2 to the
RPI-RP2350 drive.

## Controls (Phase 1 demo)

| Input | Action |
|---|---|
| D-pad | Pitch / yaw the camera |
| A / B | Thrust forward / back |

Host keys: `W/A/S/D` d-pad, `.` A, `,` B, `LShift` LB, `Space` RB,
`Enter` MENU, `ESC` quit.

## Planned controls (flight)

D-pad pitch/yaw • A fire • B secondary • LB-hold roll / LB-tap cycle target
• RB-hold throttle / RB-tap flight assist • LB+RB docking • MENU pause
(long-hold: return to ThumbyOne lobby).
