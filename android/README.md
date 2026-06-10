# ThumbyElite — Android port

An SDL2 shell around the same shared game (`../game/*.c`) used by the host and
RP2350 device builds. The game logic, renderer, audio and save format are
byte-identical to the device; only this platform layer differs.

The whole port is two compile-time `-D` overrides in `app/jni/src/Android.mk`
(the RP2350 build is untouched):

| Override | Effect |
|----------|--------|
| `R3D_SS=2` | The 3D world rasterises at **256×256** (4× the device's pixel count) for smooth ship, planet and starfield edges. |
| `ELITE_OVERLAY_SPLIT` | The 2D HUD/menus render into a separate 128-logical key-colour layer, composited **pixel-doubled** over the 3D frame — so text stays a crisp 2× while the world is full-resolution, and the status-screen dim works without reading the 3D buffer. |

So: the 3D scene is true double-resolution; HUD/text is doubled. Exactly the
brief.

## Controls

**Touch** (pixel-art overlay):

- **Thumbstick** — fixed, lower-left. Analog pitch/yaw in flight (finer than the
  device d-pad, capped at the same max turn rates); thresholds to d-pad presses
  for menu navigation.
- **A** (lower-right, large) — fire primary. **B** (above-left of A) — secondary.
- **LB / RB** — top-left / top-right corners (hold for the roll / throttle
  chords, tap for cycle-target / flight-assist, exactly as on device).
- **MENU** — right gutter, above A (mid-height, easy thumb reach).

**Game controller** (auto-detected; the touch overlay fades out while it's in
use): left stick = fly, right stick X = roll, right stick Y = throttle, d-pad =
menus, **A**/**B** = fire/secondary, **L1/R1** = LB/RB, **Start** = menu,
**right trigger** = fire. Rumble is forwarded to the pad.

### Sensitivity

Pause (MENU) → **SETTINGS**: a **GAMEPAD** slider scales controller aim and a
**STICK** slider scales the touch-thumbstick aim (both 30–200%, persisted).

The square game view is centred with controls in the side gutters; held
landscape (`sensorLandscape`).

## Prerequisites

- Android SDK (platform 35, build-tools 34+), NDK r26+ (`26.3.11579264` used),
  JDK 17+.
- `local.properties` with `sdk.dir=/path/to/android-sdk` (not committed).

## One-time setup

Vendor the SDL2 source (not committed — it's large):

```bash
git clone --depth 1 --branch SDL2 https://github.com/libsdl-org/SDL.git app/jni/SDL
```

## Build

```bash
ANDROID_HOME=/path/to/android-sdk ./gradlew assembleDebug      # or assembleRelease
# → app/build/outputs/apk/debug/app-debug.apk
```

A pre-built debug APK is checked in at the repo root as `ThumbyElite-debug.apk`
(arm64-v8a). Sideload it onto an arm64 phone to play without building.

### Ship meshes

`jni/src/generated/meshes_gen.c` (the baked ship geometry) is committed so the
APK builds standalone. The `bakeMeshes` gradle task re-bakes it from the same
host `obj2mesh` tool the host/device builds use before each native build, so it
never goes stale when the `.obj` models in `../tools/models` change. If
cmake/gcc aren't available it silently falls back to the committed copy.
