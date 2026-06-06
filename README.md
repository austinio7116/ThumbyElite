# ThumbyElite

An open-galaxy space sim for the [Thumby Color](https://color.thumby.us/) —
Elite-style trading and dogfighting with MechWarrior Mercenaries-style ship
outfitting, component salvage and procedural missions, across an infinite
deterministic galaxy. Bare-metal RP2350: flat-shaded polygon 3D at 30fps,
dual-core rendering, 520KB SRAM.

**Status: Phase 1** — renderer proof (flat-shaded depth-tested triangle
pipeline, host + device builds). Roadmap in [PLAN.md](PLAN.md).

## Changelog

### 1.22 (in development)

**Critical hits** — MechWarrior-style: hull hits (shields down) can
smash systems, rare and heavy, scaled by the blow. Player systems take
−40 integrity per crit and go OFFLINE at zero (dead mounts won't fire,
smashed generators stop regenerating, wrecked targetcomps lose the
pip); engines −40% speed/turn for the fight, fixed free on docking,
everything else repaired for credits. NPCs lose weapons, turrets,
regen, engines and targeting the same way — a fully disarmed pirate
turns and flees. Fights end in more states than "dead".

**The flight dashboard** — MENU no longer pauses: a dashboard slides up
over the live game (top strip = mini scanner + bars, so you can watch
trouble close in) with d-pad regions for GALAXY / SYSTEM / STATUS /
SETTINGS. The sim keeps running inside every menu screen — picking an
escape mid-dogfight is now a race, not a pause. No safety net by
design.

**Civilian traffic** — green-blip miners work the belts (visible
chip-beams) and cargo haulers run lanes near stations. They shoot back,
weakly. Attacking one = OFFENDER; destroying one = FUGITIVE, and its
cargo spills as STOLEN contraband (black-market only, full police/
pirate heat). Piracy is now a career with consequences.

**Distress calls** — planets/beacons in dangerous space can show
DISTRESS CALL! on the system map (red !). A civilian under pirate
attack: the wing fights them until you engage, then it's on you. Save
the victim for credits + faction rep; the kill tally stays honest
(NPC-vs-NPC kills don't count toward your rank).

### 1.21 (ThumbyOne v1.21)

**The Law**
- Lawful systems (Confederacy and up) post police Vipers — pale-blue
  scanner blips, `POLICE` lock tag, slow patrol circuits of station space
- Carrying contraband within 300 m of a patrol triggers a 2.5 s cargo
  scan → `FLAGGED: SMUGGLER`: OFFENDER status, a fine, patrols engage on
  sight. Shooting police = instant FUGITIVE (+800 cr fine); killing one
  pays no bounty and adds 1,500
- `PAY FINE` station service clears the record; legal status
  (CLEAN / OFFENDER / FUGITIVE) shows in SHIP STATUS

**Mining**
- Asteroid belts are persistent geography — deterministic per system+POI,
  so good mining spots are knowledge. Belts spawn as one visible clump
  (boulders up to ~42 m) — fixed a units bug that had every rock
  rendering at 33 cm (the renderer gained a per-instance scale path)
- MINING laser (Z1, 700 cr, every armoury) chips rocks at 4×; every
  weapon damages rocks now (projectiles included — shells used to fly
  straight through), and rocks block shots: boulders are cover
- Ore spills every 20 chip damage: minerals (55%, 25 cr) / metals (37%,
  45 cr) / rare gems (8%, 400 cr), 1–2 units; scooping feeds Tech XP
- Working a field runs an ambush clock — `PIRATES INBOUND`, sooner in
  dangerous systems
- Prospector lock: with no hostiles/salvage about, LB locks the nearest
  rock (amber ticks + distance, edge arrow off-frame)
- LB **double-tap** forces the target class (`AUTO → SALVAGE → ROCKS`);
  single taps cycle within the class, so combat never steps through rocks

**Combat rank**
- The classic nine-rank ladder, HARMLESS → ELITE (400 kills), with kill
  count in SHIP STATUS and a promotion fanfare in flight

**System map**
- Scan strip (bottom): the cursor POI's economy/tech or world type,
  `BELT!` certainty, police presence, live pirate/salvage odds —
  including the extra heat your own contraband adds. The map and the
  arrival spawner read the same per-POI hash, so what it promises is
  what you find
- The schematic strip highlights the selected body as you move down the
  list (green chevron; station ticks light up)

**AI & balance**
- Attackers break off at 120 m (was 28 m — they read as flying straight
  through the player) and joust in passes
- NPC gunnery aims at the target (was: along the nose, with a ~10°
  fire-gate — a ~44 m miss at 250 m; they almost never hit). Per-tier
  spread is the real accuracy knob now, and your lateral speed widens it:
  flying hard dodges, sitting still gets you killed
- Smooth difficulty curve, siege-sim verified vs a standard shield:
  HARMLESS collapses it in ~51 s (and won't kill you), then 26 / 12 / 7 /
  ~4 s for DEADLY. Turrets no longer bypass tier scaling, tier-3 mains
  trade gauss for dodgeable autocannon streams
- Star heat is real: every ship heats inside ~6 star radii (the old
  scoop heat lost to passive cooling and never registered); past the
  redline the hull burns 5/s — you can die in the corona

**Balance pass** (income rates computed from the live tables)
- Mining trimmed to ~7–9k cr/hr (ore spill threshold 20→24 chip damage,
  gems 8%→6%) — still the best broke-pilot bootstrap; trade overtakes
  with the first hauler (~50k/hr in a MULE), missions pay ~15–20k/hr
  mid-game
- The MINING laser is now a real investment: crude blasting vaporizes
  ore (standard weapons recover ~45% per chip; the tool recovers 100%,
  4× faster) — rocks are finite, so the tool roughly doubles a belt's
  value (lasers used to mine nearly as well)
- Every hull now has at least two weapon mounts (SKIFF/DART/PACK MULE
  gain a Z1) — laser + autocannon is the natural early pairing instead
  of being forced into a laser build by dry-tank risk
- Ammo weapons: HOMING 6→10 rounds at 40 cr/round (was 55), FLAK 40→60,
  AUTOCANNON 160→200
- Defense gear gets its own steeper quality curve ({0.80, 1.00, 1.20,
  1.40, 1.65}) — HIGH-TECH +40% cap, PROTOTYPE +65%; quality upgrades
  were +12% and read as worthless
- Galaxy chart: hold-to-scroll autorepeat snapping (star to star, same
  cadence as the lists) replaces the smooth pan; the system map's POI
  list scrolls on hold too
- The galaxy is 40% closer together: SECTOR_LY 8→4.8 with the chart
  pinned pixel-identical — same map, smaller LY numbers, range circles
  sweep much wider, typically 2–3 systems inside a starter jump (was
  1–2). Delivery pay per LY rebalanced so mission income is unchanged

**QoL & fixes**
- SETTINGS submenu in the pause menu: INVERT Y + SHOW FPS (smoothed
  green readout, top middle, works on every screen)
- Dock home: FUEL/CARGO moved under the station pane (the 10-row service
  list had LAUNCH overlapping the fuel line)
- HUD: in-front-but-off-frame lock targets now fall through to the edge
  arrow (brackets used to draw off-screen and vanish silently)
- Pilot's Handbook: gameplay-video link, real per-class ship galleries,
  ring-station and X-foil dockyard shots, staged combat screenshots,
  Law / Mining / Rank sections, survey-sheet figure

### 1.20 (ThumbyOne v1.20)

Initial public release as ThumbyOne's eighth system — the full game:
infinite procedural galaxy, trading, missions, bounties, salvage,
outfitting with quality/affix/variant gear, 14 weapon families,
procedural ships/stations/planets, dock-checkpoint saves, and the
[Pilot's Handbook](https://austinio7116.github.io/ThumbyElite/).

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
