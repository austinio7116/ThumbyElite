# ThumbyElite on PC (Windows / Linux)

The SDL2 host build is a full desktop port. It comes in three resolutions —
all the same game, different render scale:

| Target | Frame | Window |
|--------|-------|--------|
| `thumbyelite_host`       | 128×128 | device-faithful |
| `thumbyelite_host_hires` | 256×256 | Android-preview |
| `thumbyelite_host_quad`  | 512×512 | **desktop quad-res** |

## Build & run — Linux

```bash
sudo apt install libsdl2-dev cmake build-essential   # once
cmake -B build_host -S host && cmake --build build_host -j8
./build_host/thumbyelite_host_quad
```

**Running under WSL2:** the window shows via WSLg. If you get a running process
but no window, it's usually a stale/custom SDL2 in `/usr/local` taking
precedence over the stock package — check with `ldd build_host/thumbyelite_host_quad | grep -i sdl`
and prefer Ubuntu's `libsdl2-dev`. The build now also falls back to a software
renderer when WSLg can't give an accelerated context. If WSLg windows misbehave
generally, `wsl --shutdown` (from Windows) and reopen. The native Windows build
below sidesteps WSLg entirely.

## Build & run — Windows

### Distributable build (recommended) — cross-compile from Linux/WSL2

This is how the shipped Windows package is built. It cross-compiles with
MinGW-w64 and produces a self-contained, **Steam-ready** folder — no Windows
machine or Visual Studio needed.

```bash
sudo apt install mingw-w64 cmake build-essential   # once
tools/build_win.sh                                  # fetches SDL2, builds, packages
```

Output: **`build_win/dist/`** containing `ThumbyElite.exe` (GUI subsystem — no
console window, embedded icon + version info) + `SDL2.dll` + `README.txt`. The
`.exe`'s only runtime dependencies are `SDL2.dll` and standard Windows system
DLLs (`-static-libgcc`, so no MinGW runtime stragglers). Hand that folder to
any 64-bit Windows user, or use it directly as a Steam depot.

Under the hood: `host/toolchain-mingw-w64.cmake` drives the cross-build; the
mesh baker (`obj2mesh`) is forced to the native compiler so it still runs
during the build; `host/win/thumbyelite.rc` supplies the icon + version
resource; `tools/fetch_sdl2_mingw.sh` vendors the official SDL2 MinGW devel
libs (pinned, gitignored).

#### Steam notes

- A MinGW + dynamic-SDL2 build is fine for Steam (many SDL games ship exactly
  this). The depot is just the `dist/` folder; set the launch executable to
  `ThumbyElite.exe`.
- The save (`thumbyelite.sav`) and settings (`thumbyelite_settings.dat`) are
  written next to the `.exe`. For Steam Cloud, point it at those two files (or
  switch the save path to `%USERPROFILE%/Saved Games` before release).
- Version info is in `host/win/thumbyelite.rc` (currently `1.0.0.0`).

### Build *on* Windows instead

If you'd rather build natively on a Windows box, you need CMake, a compiler,
and SDL2. Two routes:

### MSVC (Visual Studio)

1. Install **Visual Studio** (Desktop C++) and **CMake** (bundled with VS, or
   standalone).
2. Get SDL2: the simplest is **vcpkg**:
   ```
   vcpkg install sdl2:x64-windows
   ```
3. Configure + build (from the repo root, in a *Developer* prompt):
   ```
   cmake -B build_host -S host -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
   cmake --build build_host --config Release
   ```
4. Run `build_host\Release\thumbyelite_host_quad.exe`. (If Windows can't find
   `SDL2.dll`, copy it next to the .exe — vcpkg puts it under
   `vcpkg\installed\x64-windows\bin`.)

### MinGW / MSYS2

```
pacman -S mingw-w64-x86_64-{gcc,cmake,SDL2}      # in an MSYS2 MinGW64 shell
cmake -B build_host -S host -G "MinGW Makefiles"
cmake --build build_host -j8
./build_host/ThumbyElite.exe            # quad-res; copy SDL2.dll alongside it
```

On Windows only the quad-res target is built (it's the shippable game); the
128/256 preview builds are Linux-only. The save (`thumbyelite.sav`) and
settings (`thumbyelite_settings.dat`) are written to the working directory.

## Controls

### Keyboard

| Key | Action |
|-----|--------|
| `W` `A` `S` `D` | pitch / yaw (d-pad) |
| `.` | A — fire primary |
| `,` | B — secondary / missile |
| `Shift` | LB (hold = roll mod, tap = cycle target) |
| `Space` | RB (hold = throttle mod, tap = flight-assist, double-tap = boost) |
| `Enter` | MENU (pause / dashboard) |
| `Esc` / `F12` | quit |

### Game controller (Xbox / PlayStation / etc.)

Auto-detected, hot-pluggable. Analog aim with the device chords intact:

| Control | Action |
|---------|--------|
| Left stick | pitch / yaw (analog) |
| Right stick X | roll |
| Right stick Y | throttle (up = faster, holds) |
| A | fire primary &nbsp;·&nbsp; B | secondary |
| Right trigger | fire (alt) |
| LB | hold = roll mod · tap = cycle target |
| RB | hold = throttle mod · tap = flight-assist · double-tap = boost |
| Start | MENU |
| D-pad | menu navigation |

### HOTAS (flight stick + throttle)

Also auto-detected (any joystick that isn't a standard game-controller is
treated as a HOTAS). Default mapping:

| Axis | Action |
|------|--------|
| Stick X | yaw |
| Stick Y | pitch |
| **Twist** | **roll** (rotate) |
| Throttle lever | absolute throttle |

| Button | Action |
|--------|--------|
| Trigger (btn 0) | fire |
| btn 1 | secondary |
| btn 2 | RB · btn 3 | LB |
| btn 4 | MENU |
| Hat / POV | menu navigation |

**Axis numbers vary by device.** If pitch/roll/throttle are on the wrong axes,
run once with `ELITE_HOTAS_DEBUG=1` — it prints every axis value live so you
can read off the indices — then set them:

```bash
ELITE_HOTAS_YAW=0 ELITE_HOTAS_PITCH=1 ELITE_HOTAS_ROLL=3 ELITE_HOTAS_THROTTLE=2 \
  ./build_host/thumbyelite_host_quad
```

Append `_INV` to flip an axis: `ELITE_HOTAS_PITCH_INV=1`,
`ELITE_HOTAS_THROTTLE_INV=1`, etc. (Pitch defaults to inverted — stick forward
= nose down. The in-game **INVERT Y** setting flips it too.)

## Sensitivity

Pause (MENU) → **SETTINGS** has a **GAMEPAD** slider (30–200%) that scales the
controller/HOTAS aim axes. `</>`  adjusts; it persists across sessions. (The
**STICK** slider next to it is the Android touch-stick sensitivity and has no
effect on PC.)
