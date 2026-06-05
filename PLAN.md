# ThumbyElite — Bare-Metal Elite/MechWarrior Space Game

## Context

A new flagship bare-metal game for the Thumby Color (RP2350 @ 280MHz dual Cortex-M33+FPU, 520KB SRAM, ~16MB flash XIP, 128×128 RGB565 GC9107, 22050Hz PWM audio). Goal: the best game on the platform — an Elite-style infinite galaxy with deep trading, MechWarrior Mercenaries-style ship/component outfitting + salvage/refurbish loop, real-time 3D dogfighting with per-subsystem damage and heat, and procedural paid missions.

**Locked decisions (user-confirmed):**
- **Combat**: real-time 3D dogfighting + MechWarrior subsystem depth (per-component HP, heat, subsystem targeting, pilot skills)
- **Visuals**: flat-shaded polygon 3D (Frontier: Elite II look) — shaded ships/stations, starfield, planet impostors, sun glare, trails
- **Worlds**: stations + orbital flybys; menu-driven station interiors; no surface flight
- **Name**: **ThumbyElite**, own git repo at `/home/maustin/thumby-color/ThumbyElite/`

**Foundation (verified by exploration):**
- Vendor platform layer from **ThumbyRogue/device/** (most recent, production-tested): `craft_lcd_gc9107.c/h` (async DMA present — `craft_lcd_wait_idle()` before rewriting fb), `craft_buttons.c/h`, `craft_audio_pwm.c/h` (2048-sample ring), `rogue_save_flash.c` + `rogue_save_fatfs.c` patterns, `rogue_device_main.c` (280MHz, dual-core go/done handshake + `multicore_lockout_victim_init`, MENU long-hold 1200ms → `thumbyone_handoff_request_lobby()`), `device/CMakeLists.txt` (standalone + `THUMBYONE_SLOT_MODE` w/ "absolute" UF2 family, heap 0x4000/0x2000)
- Port (not link) the perspective-correct depth-tested triangle rasterizer from `TinyCircuits-Tiny-Game-Engine/src/draw/engine_display_draw.c:745` (`engine_draw_filled_triangle_depth` — edge functions, barycentric, signed-area backface cull)
- Vendor `craft_font.c` (8×8 + 2x), hash3/fbm/value-noise from `ThumbyRogue/engine/src/craft_gen.c`, xorshift from `rogue_gen.c`, synth-voice audio path from craft_audio, `cdl_to_c.py` MIDI converter from `ThumbyCraft/tools/`, baker `ExternalProject_Add` pattern from `bake_textures.c`
- All-float math is fine (-ffast-math, fpv5-sp-d16); ThumbyCraft's all-float per-pixel raycaster hits 30fps dual-core

## Repo structure

```
ThumbyElite/
├── PLAN.md / README.md / CLAUDE.md / .gitignore
├── device/        # platform shell: CMakeLists, elite_device_main.c, vendored craft_lcd/buttons/audio_pwm, elite_save_flash.c, elite_save_fatfs.c
├── host/          # SDL2 build: CMakeLists, host_main.c (keyboard→buttons, SDL audio, screenshot env hook)
├── game/          # ALL game+engine logic, platform-independent C:
│   ├── r3d_raster, r3d_pipe, r3d_scene, r3d_starfield, r3d_fx      # renderer
│   ├── meshes_gen.c (baked), enames.c (baked), music_seq.c (baked)
│   ├── elite_game, elite_flight, elite_combat, elite_ai, elite_ship, elite_entity
│   ├── galaxy_gen, system_sim, econ, mission_gen, pilot, outfit
│   ├── ui_station, ui_map, ui_hud, elite_audio, elite_save
│   └── vec.h, craft_font.c/h
├── tools/         # obj2mesh.c (OBJ→meshes_gen.c, precomputed int8 normals, winding check, LODs), bake_assets.c, cdl_to_c.py, models/*.obj
└── build_host/ build_device/   (gitignored)
```

UF2: `build_device/thumbyelite.uf2` → copy to workspace root `firmware_thumbyelite.uf2`. ThumbyOne slot via a `THUMBYONE_WITH_ELITE` block in `ThumbyOne/CMakeLists.txt` (mirrors the rogue block ~line 428); **the combined firmware_thumbyone.uf2 is the real deliverable** alongside standalone.

## 3D renderer

- **Pipeline**: int8-quantized model verts → rotate/scale → **camera-relative world (camera is always origin — kills f32 precision blowup at system scale)** → view → near-plane-only Sutherland-Hodgman clip (screen-edge clip is implicit in raster bbox clamp) → project → raster
- **Flat shading**: per-face precomputed int8 normal, rotate by object orientation, dot sun dir, `shade = 0.25 + 0.75*ndotl`, RGB565 scale
- **Depth**: uint16 128×128 (32KB), memset-cleared per frame. Backface cull = signed screen area (free)
- **Dual-core**: core0 builds the full screen-space draw-list (transform+clip+shade), then **both cores rasterize the same list, each clamped to its screen half** (core0 y0–63, core1 y64–127) — disjoint pixels, zero atomics, same go/done handshake as ThumbyRogue
- **Budget**: ~250–400 visible tris/frame, ~32–48K filled px (2–3× the raycaster's 16K px which holds 30fps). **Lock the real budget on device in Phase 2**
- **Starfield**: ~120 baked unit-direction stars, rotate+project, 1–2px plots (correct rotational parallax for free)
- **Planets**: 2D shaded-disc impostors — per-pixel sphere-normal reconstruction for day/night terminator + limb darkening, per-planet 256B fbm texture + palette generated on system entry; gas-giant latitude banding; 1–2px atmosphere rim. Sun = bright disc + radial glare LUT + ghost flares
- **FX** (`r3d_fx`): one particle pool — engine trails (throttle-colored), muzzle/beam lines, explosions (burst + ring + 3–6 spinning low-LOD debris chunks), shield-hit flicker. Depth-tested, no depth write
- **Nebula skybox**: fbm baked once per system into 64×32 RGB565 equirect (4KB), sampled per 8×8 block as background; toggleable cut knob

## Ships & assets

- Vertex `{int8 x,y,z}` ×scale; face `{u8 a,b,c; int8 nx,ny,nz; u16 color}` = 8B. A 60-tri fighter ≈ 600B flash. Optional int16 verts for big stations
- **10 base hulls**: Shuttle (starter junk), Courier, LightFighter (30–50 tris), HeavyFighter (50–80), Viper-police, PirateCutter, Light/Medium/HeavyFreighter (60–160), endgame Dreadnought. Station 150–300 tris + 80-tri LOD. Asteroids, canisters, beacons
- **Variant system**: base hull + loadout descriptor + per-instance faction color tint = many distinct opponents per mesh, zero extra flash

## Infinite galaxy (deterministic, zero storage)

- 3D int32 **sector grid** (8 ly cubes), 0–4 stars/sector by hash; `sys_seed = splitmix64(hash3(sx,sy,sz) ^ rotl(star_idx*GOLDEN, 17))` — everything derives from the seed, generated lazily; save stores only player overlays (visited flags, price knowledge ~128 entries)
- **Per system**: star class/color/luminosity → habitable zone; 0–7 planets (type gated by orbit), rings/moons; 0–3 stations w/ economy type (8 types), tech level 1–15, faction + govt (Anarchy…Corporate), black-market flag; nav beacon; asteroid fields; threat level (pirates vs police)
- **Names**: Elite-1984 digraph/syllable tables baked to flash, deterministic per seed (systems, factions, pilots, stations)
- **Economy**: 20 commodities (incl. ★Narcotics/★Weapons/★Slaves/★StolenGoods); price = base ± economy bias ± tech factor ± seed jitter ± dynamic drift ± player-induced supply/demand (current station only). Producers sell cheap / buy dear → emergent trade routes. Rare goods, black markets, price-knowledge discovery
- **Factions/rep**: 3–4 majors + procedural local minors; int8 rep per major + per-jurisdiction wanted/bounty level; gates missions, docking, prices
- **Travel**: hyperspace jumps gated by fuel/jump-range (drive + mass derived); 2–3s witchspace cinematic. In-system = supercruise time compression (15–40s hops, pirate interdiction pulls you to combat)
- **Maps**: galaxy map (2D pan/select, jump-range ring), system map (schematic orbits, set-destination)

## Phase 6 requirements (user-specified 2026-06-05)

- **Weapon roster**: light/medium/heavy lasers, photon cannons, dumbfire
  missiles, homing missiles, gauss guns, autocannons — plus beam laser, mines;
  each with quality variations (damage/heat/cooldown/projectile speed).
- **Weapon switching**: multiple weapons fitted simultaneously, player cycles
  the ACTIVE weapon in flight (group/select UI on the HUD).
- **Slots by hull**: ship size/cost determines weapon slots available; same
  gating for shield generators and hull upgrades (cost/size tiers).
- **Shields recharge slowly** (meaningful downtime after collapse).
- **AI tiers** determine speed, accuracy, weapons fitted, and power.

## Station variety (user-specified 2026-06-05)

- Stations must not be dull grey cubes: procedural station-shape generation
  (module accretion / CA-like growth from the station seed) — habitat boxes,
  panel wings, antennae, docking bay face; mirror/rotational symmetry;
  per-station palette tints. Every station unique, deterministic, zero baked
  storage.

## Combat & flight

- **Flight model**: Newtonian-lite + flight assist (velocity bleeds toward nose; assist-off = full drift toggle). Speed/turn/accel derived from engine HP/quality, mass, power
- **Controls** (validate chords on device Phase 3): d-pad = pitch/yaw; A = fire primary; B = secondary/missile; **LB hold** = roll modifier (d-pad L/R rolls), LB tap = cycle target, LB+A = cycle subsystem; **RB hold** = throttle modifier (d-pad U/D), RB tap = flight-assist/match speed, RB double-tap = boost; LB+RB = gear/dock request; MENU short = pause, long-hold = lobby
- **Subsystems**: 4-facing directional shields (independent HP+regen) → hull → components, each with HP + failure effect: Powerplant (power budget→dead ship), Engines (speed/turn), each weapon hardpoint, Shield gen, Life support (O2 countdown!), FSD (can't flee), Cargo hold (spills canisters). Subsystem targeting for player and high-tier AI
- **Heat**: weapons/thrust generate, radiators dissipate, >100% damages components + weapon cutout; heat-sink consumables; cold-running lowers scanner signature
- **Weapons**: pulse + beam lasers (hitscan, beam viz), ballistic cannons (projectile, ammo, leads), lock-on missiles, mines. Defenses: shields, armor, chaff. (Turrets/point-defense = v1.1)
- **AI**: PATROL→DETECT→INTERCEPT→ATTACK→EVADE→FLEE state machine, 5 skill tiers (Harmless…Elite) scaling lead accuracy, reaction, jink, subsystem smarts
- **Targeting UI**: classic Elite 3D radar disc with stalks, target box + off-screen arrow, lead reticle, target readout w/ selected subsystem

## RPG systems

- **Pilot skills** (XP from related actions): Piloting, Gunnery, Engineering, Trading, Salvage/Tech, Charisma — each with concrete effects; perk nodes at milestones (e.g. Cold Blooded −20% heat)
- **Outfitting**: sized weapon hardpoints (S/M/L), utility mounts, core internals (powerplant/thrusters/FSD/life support/sensors/fuel), optional internals (shields/cargo/armor) — constrained by **power budget + mass** (the MW "can't fit everything" tension); live derived-stat preview
- **Components**: `{type, size, quality 0–4 (Salvaged…Prototype), integrity, wear}` — salvaged parts spawn damaged, repair/refurbish for credits (skill-modified success). **The MW Mercs loop**
- **Salvage**: wrecks drop cargo canisters + component pods; scoop into hold; sell / install-as-is / refurbish
- **Missions** (seeded from `sys_seed ^ day ^ station`, rep-gated, ~8 active cap): cargo delivery, smuggling, bounty hunt, assassination, pirate cull, escort, mining survey, rescue, salvage retrieval; timers, distance/danger-scaled rewards
- **Arc**: junk Shuttle + 1000cr → trade/missions/salvage → Dreadnought; open-ended with Elite combat rank milestones

## Memory budget (~232KB allocated, >240KB headroom)

| Item | KB | | Item | KB |
|---|---:|---|---|---:|
| SDK+stacks+heap | 40 | | Entity pool 24 ships×~1KB | 24 |
| Framebuffer u16 | 32 | | Particle pool 512×32B | 16 |
| Depth u16 | 32 | | Projectiles 96×40B | 4 |
| Audio ring+synth | 8 | | System cache (lazy) | 12 |
| Draw-list 512 tris×48B | 24 | | Market/missions/maps/UI | 14 |
| Vertex scratch | 8 | | Pilot+ship+save staging | 9 |
| Nebula LUT | 4 | | Loot containers | 1.3 |

Cut knobs if needed: nebula LUT, depth→u8 (−16KB), draw-list 512→384, pool sizes. Flash total << 1MB (code ~400KB, meshes ~30KB, tables/music ~60KB).

## Audio

- **SFX**: vendored craft_audio procedural synth voices — laser zap, explosion (noise+rumble), throttle-pitched engine hum loop, UI bleeps, docking clunk, distinct two-tone klaxons (overheat/lock/O2/shields), shield-tick vs hull-thud, jump whoosh, scoop bleep
- **Music**: CDL sequence synth (NOT OPL2 — emu8950 would steal core1, which is our render half). 2–4 ambient tracks via `cdl_to_c.py`: cruise pad, combat, station lounge. **Blue Danube easter egg on auto-dock**

## State machine

`TITLE → { FLIGHT | SUPERCRUISE | HYPERSPACE | GALAXY_MAP | SYSTEM_MAP | DOCKED(market/shipyard/outfit/missions/bar/status) | PAUSE | DEATH→REBUY (insurance rebuy of last config) }` — each with enter/tick/draw. HUD: shield pips + hull (TL), heat + speed (TR), center reticle/target box, radar disc (bottom), warning banners; center kept clear.

## Phases (each ends host-playable; renderer perf validated ON DEVICE — never claim perf from host)

1. **Repo + builds + renderer proof** — scaffold, vendor platform files, host SDL2 + device CMake, port triangle rasterizer + pipe + near clip. Verify: rotating shaded cube on host AND device with frame-time readout
2. **Mesh pipeline + dual-core + ships** — obj2mesh baker, 2–3 ship meshes, draw-list + screen-half split, starfield. **Lock device polygon budget here**
3. **Flight + controls + first combat** — flight model, chord controls (validate ergonomics on device), entity pool, hitscan lasers, fx pool, dumb enemy, HUD v0. Verify: dogfight 4–8 ships at 30fps on device
4. **Galaxy procgen + maps + hyperspace** — galaxy_gen, names, system cache, planet/sun impostors, both maps, jump cinematic, supercruise. Verify: deterministic galaxy, fly to bodies
5. **Stations + docking + trading** — station meshes+LOD, docking flow + Blue Danube, DOCKED market, econ model, buy/sell. Verify: profitable trade route; device station-close-up fill-rate (worst case)
6. **Combat depth** — directional shields, component damage+failures, heat, missiles/ballistics/mines, chaff, AI tiers, subsystem targeting UI. Verify: snipe engines, overheat cutout, lock+chaff
7. **Outfitting + salvage + RPG** — slots + power/mass budget, quality/integrity/wear, shipyard/outfit screens, wreck pods + scoop + refurbish, skills/perks
8. **Missions + rep + factions + bar** — all mission types, board UI, rep/wanted, black market, rumors
9. **Audio + save + polish** — full SFX + music, versioned save (magic/version/len/crc) on flash+FatFs, death/rebuy, pause, nebula, sun flares
10. **ThumbyOne slot + balance** — slot build, `THUMBYONE_WITH_ELITE` in ThumbyOne/CMakeLists.txt, handoff/brightness/LED wiring, `/thumbyelite/*.sav`, full-loop balance pass. Verify: boots from lobby, MENU-hold returns, save resumes

## Risks & mitigations

- **Fill-rate (station close-up)**: distance LOD + coverage early-out (bbox >60% screen → lowest LOD); profile on device every renderer phase
- **f32 precision at system scale**: camera-relative rendering; per-system local coordinate frames; never feed million-km absolutes into f32 transforms
- **SRAM creep**: budget table tracked in CLAUDE.md, re-measured from .map each phase; explicit cut knobs
- **Chord ergonomics**: device-validate Phase 3; fallback auto-bank roll
- **Out of v1**: multiplayer, planet landings, walkable interiors, wingmen, turrets/point-defense, OPL2, boarding, engineering mods beyond refurbish

## Conventions

Host build first for all dev; user flashes for device testing — **never push or claim device perf untested**. No Co-Authored-By. Commits in ThumbyElite's own repo. After slot work, rebuild ThumbyOne (`firmware_thumbyone.uf2` is the deliverable).

## Verification (overall)

- Host: `cmake -B build_host -S host && cmake --build build_host -j8 && ./build_host/thumbyelite_host [seed]` — full loop playable with keyboard
- Device: build `build_device/thumbyelite.uf2`, copy to `firmware_thumbyelite.uf2`, user flashes via BOOTSEL; 30fps validated with on-screen frame time at Phases 1/2/3/5
- Determinism: same seed → identical galaxy/names/prices across host and device
- ThumbyOne: combined UF2 boots Elite from lobby, MENU-hold returns, FAT save resumes
