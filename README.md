# ThumbyElite

An open-galaxy space sim for the [Thumby Color](https://color.thumby.us/) —
Elite-style trading and dogfighting with MechWarrior Mercenaries-style ship
outfitting, component salvage and procedural missions, across an infinite
deterministic galaxy. Bare-metal RP2350: flat-shaded polygon 3D at 30fps,
dual-core rendering, 520KB SRAM.

**Status: Phase 1** — renderer proof (flat-shaded depth-tested triangle
pipeline, host + device builds). Roadmap in [PLAN.md](PLAN.md).

## Changelog

### 1.23 (in development)

- **PLASMA cannon** (Z2, T6+, 2,400 cr): a rapid stream of energy
  bolts — autocannon tempo with no ammo bill, paid in heat, balls need
  leading
- **PLASMA LANCE** (Z3, T11+, 6,400 cr): phases through shields
  straight to hull — can crit a shielded target; the BULWARK-tank
  killer, countered by thick armor
- **PHOTON BLASTER** (Z2, T8+, 3,400 cr): slower plasma-style bolts
  that bend gently toward your lock — forgiveness, not homing (chaff
  ignores them)
- **Missile rework** — HOMING slowed (172 m/s, gentler 1.0 rad/s
  arcs). Pirates answer by rank: VETERAN+ chaffs first (1-2 charges),
  turns and runs (the fastest hulls can outpace a long stern chase),
  and slips the terminal lock (CAPABLE ~25% / ELITE ~50%); save on
  LAUNCH as well as dock; measured-DPS weapon table in the guide
- **Confidence is speed** — pilot tier sets fighting pace
  (HARMLESS ~55% of envelope up to DEADLY at 100%): green pilots are
  slow, trackable prey; aces duel at the edge. Distress call rate
  halved
- **Dogfight rework: the blue zone** — turn authority peaks at half
  throttle for ALL ships; pirates fight at corner speed and answer a
  tail by rank (run / jink / throttle-chop / scissors). Position is
  earned and owned; jousting is over. Difficulty ladder re-anchored
  (52/27/18/11/4s siege collapse)
- **Collision physics** — ships, rocks and the station deflect and
  damage on contact: shields block hull entirely, size sets the split,
  autodock is exempt, rams chip ore, NPCs steer around boulders
- **CLOAK** (T9+, 5,800 cr): RB+B, 8s invisible, one charge per
  launch, heat climbs, firing breaks it
- **MANIFEST SCANNER** (T7+, 2,200 cr): hold a lock on a civilian to
  read its cargo before you commit to the crime
- Mining laser rebalanced as a tool: weaker than a small laser but not
  useless (ship DPS ~22 vs PULSE-S 56; rock yield unchanged)

### 1.22.1 (ThumbyOne v1.22.1)

- **Slot overflow fix** — the game had outgrown its 256 KB ThumbyOne
  partition (blank boot); the slot now builds size-optimised (95 KB
  headroom) and the build hard-fails if it ever overflows again
- **Every hull is an individual** — stats roll ±8–15% around class
  book values, weapon-slot layouts vary, utility bays run 1/2–4/3–4 by
  class, and used ships arrive with rolled part-worn kit. Spec sheets
  show the instance (with compare colours), so shopping is a treasure
  hunt. SAVE v4 (v3 saves migrate automatically)
- **Friend-or-foe lock colours** — green brackets/arrows for
  civilians, blue for police, red for hostiles; the readout names the
  contact (PIRATE/POLICE/CIVILIAN) with pilot tier beneath

- **SERVICE** (was REARM): one bill now refills magazines AND patches
  hull damage (~2 cr/point, Tech-skill discounted) — current hull HP
  was unrepairable anywhere, contrary to the guide
- **REPAIR DRONE** (T10+ util, 3,600 cr): slowly patches hull in
  flight, then works through damaged systems — a critted mount can
  come back online mid-fight, with DRONE: toasts narrating
- Cargo and component-rack pools separated (racked salvage no longer
  eats trade capacity)
- Insurance can no longer resurrect you into a PREVIOUS campaign
  (NEW GAME + death before first dock loaded the old save — the
  mysterious 16k)

### 1.22 (ThumbyOne v1.22)

**The flight dashboard** — MENU no longer pauses: the real cockpit
console slides up the screen (and back down on exit), scanner still
live inside it, over two working MFD instruments — a mini galaxy chart
with your jump ring and the system schematic with your anchor marked —
plus STATUS/SETTINGS buttons. The sim keeps running inside every menu
screen; there is deliberately no safety net. Escapes are decisions
made under fire.

**Critical hits** — MechWarrior-style: hull hits (shields down) can
smash systems, rare and heavy, scaled by the blow. Player systems take
−40 integrity per crit and go OFFLINE at zero (dead mounts won't fire,
smashed generators stop regenerating, wrecked targetcomps lose the
pip); engine crits cut speed/turn 40% until you dock. NPCs lose
weapons, turrets, regen, engines and targeting the same way — a fully
disarmed pirate turns and flees.

**Civilian traffic & distress calls** — green-blip miners work the
belts (visible chip-beams) and cargo haulers run lanes. They shoot
back, weakly. Attacking one = OFFENDER; destroying one = FUGITIVE,
and its cargo spills as stolen contraband (black-market only, full
police/pirate heat). Planets and beacons in dangerous space can show
DISTRESS CALL! on the system map — a civilian under pirate attack;
the wing fights them until you engage, then it's on you. Save the
victim for credits + faction rep. Rescued ships are lockable (LB
cycles neutral ships when no hostiles are about) and beyond-range
contacts show as dim rim ticks instead of a flipping altitude stalk.

**Galaxy chart data layers** — RB cycles SPECTRAL / THREAT (every
star green→red by danger) / FACTION (territory by empire) / ECONOMY
(one hue per type with an Ag·In·Hi·Ex·Re·To·Mi·Sv key; white = mixed
ports, near-dark = no stations). Route planning without opening a
single survey.

**Trade economy pass** — market prices colour-code against galactic
base (cyan = cheap here / gold = dear here: the trade matrix finally
visible at the counter), bulk staples re-based so their margins mean
something, local price jitter widened ±12%. Saves unchanged.

**Shipyard compare colours** — every spec-sheet stat colours against
your CURRENT ship (bright green ≥+30% better → red ≤−30% worse, grey
when matching; TIER/GUNS compare totals across slots). A yard reads
in seconds.

**Settings, properly wired** — VOLUME and BRIGHTNESS sliders in the
dashboard settings, bridged to the ThumbyOne shared store: the lobby
and every slot see the same values, applied live. (Volume was
completely unwired before — the mixer had no master gain.)

**More planet colourways** — 22 realistic palettes across the six
planet types (mercury greys, mars rusts, io sulfur, pluto cream,
storm-navy oceans, saturn butterscotch…), same patterns.

**Polish & fixes**
- HUD left panel: three clean rows (the kills counter and chaff count
  drew half off-screen); chaff lives top-left; SC HUD drops it
- RANK + LEGAL standing on the status sheet's first screen
- Auto-turrets fire on hostiles only (locking a civilian to find them
  no longer lets your turret commit crimes)
- Police kills still count toward rank; killing police pays nothing
  and costs 2,300+ in fines
- Mining: the tool is a real investment — crude blasting vaporizes
  ~55% of ore; the MINING laser recovers all of it, 4× faster
- LB double-tap window relaxed to 0.5 s (physical buttons missed it)
- Per-economy trade bias table + expanded government/threat/factions
  detail in the Pilot's Handbook

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
