# Indemnity Run — Random Events, Procedural NPCs & Lore Drip

Design locked 2026-06-12. Inspirations: FTL (beacon hails, blue options,
risk/reward, trap branches), Slay the Spire (weighted pool, rarity, seen-suppression,
codex), Cobalt Core (recurring named NPCs, authored arcs gated on prior choices).

User decisions:
- **Faces:** parametric vector portraits (zero assets, seed-stable, recurring-safe).
- **Content model:** BOTH a random pool AND authored arcs with recurring NPCs.
- **First trigger:** dock-arrival hails (FTL beacon moment).

Two distinct axes of variety (don't conflate them — see §10):
- **Narrative / modal** — dock & bar hails: text + a choice, resolved in a popup.
- **Ambient / environmental** — what you fly *into* at a planet/POI: salvage,
  derelicts, rocks, patrols, anomalies; resolved in-world by shooting/scooping/
  scanning/ignoring. This fixes the "empty planet arrival feels dead" problem.
They share seed selection + lore plumbing but resolve in different places; a
derelict/distress can *bridge* into a modal event when scanned.

Hard constraint: must not push the ELITE device slot past the 256 KB cap
(`device/check_size.cmake` hard-fails). Content is the only real risk, so the
engine is data-driven (fixed code cost) and content is build-time budgeted.

---

## 1. Architecture — one interpreter, many data rows

The whole point: **events are flash data run by a single small VM.** Adding events
costs only their data bytes, never code. Mirrors how `mission.c` and `r3d_planet.c`
already trade code for seeded determinism.

### `game/events.h` — types

```c
typedef enum { OP_END, OP_CR, OP_CARGO, OP_REP, OP_FUEL, OP_DMG,
               OP_AMBUSH, OP_MISSION, OP_LORE, OP_FLAG, OP_BRANCH,
               OP_TOAST } OpCode;          /* 1 byte */

typedef struct { uint8_t op; int8_t a; int8_t b; } Op;   /* 3 bytes */

typedef struct {
    const char *label;        /* "TOW THEM IN"                       */
    const char *gate_tag;     /* "[50 CR]" / "[SCOOP]" or NULL       */
    uint16_t    gate;         /* GATE_* bits required to ENABLE      */
    const Op   *ops;          /* outcome op-list, OP_END terminated  */
} Choice;

typedef struct {
    uint8_t       weight;     /* base selection weight               */
    uint8_t       rarity;     /* COMMON/UNCOMMON/RARE/ONESHOT        */
    uint16_t      gate;       /* GATE_* required to OFFER at all     */
    uint8_t       arc;        /* ARC_NONE or arc id (Cobalt-Core)    */
    uint8_t       arc_step;   /* position in arc; gates on prior     */
    const char   *title;
    const char   *body;       /* template w/ {NAME}{SYSTEM}{GOOD}... */
    const Choice *choices;
    uint8_t       n_choices;
    uint8_t       npc_kind;   /* face archetype / faction tint       */
} Event;
```

`gate` bits (cheap predicates evaluated against live state): `GATE_CARGO_SPACE`,
`GATE_HAS_FUEL_SCOOP`, `GATE_REP_LOW/HIGH(f)`, `GATE_GOV_ANARCHY`, `GATE_ECON_*`,
`GATE_LORE_SEEN(id)`, `GATE_LORE_UNSEEN(id)`, `GATE_HULL_DMG`. Same idea as the
FTL "blue option" — a choice can be visible-but-disabled (greyed, shows `gate_tag`)
or hidden entirely.

`OP_BRANCH a b` = with `a`% chance jump to op index `b`, else continue → the FTL
"help them / it's a trap" payoff-or-backfire beat, all in data.

### `game/events.c` — the VM (fixed code cost)

- `int events_pick(uint64_t seed, Trigger t, const SystemInfo *si)` —
  weighted-random over the pool, filtered by `gate` + trigger + seen-suppression
  ring + arc-step ordering. Seed = `system_seed ^ visit_salt ^ EVENT_SALT(t)` so
  the result is **stable for a given visit** (no save-scum re-docking) but rerolls
  as you travel — same contract as `mission_make_offers`.
- `events_run_choice(const Event*, int choice)` — execute the op-list:
  apply credits/cargo/rep/fuel/damage, spawn ambush, grant mission, reveal lore,
  set flags, branch. Returns a result tag for the toast/aftermath text.
- `events_text(char *out, n, const char *tmpl, const NpcId*)` — token
  substitution: `{NAME}`→`ename_*`, `{SYSTEM}`/`{GOOD}`/`{CR}`/`{FACTION}`/`{SHIP}`.
  One template renders many strings (FTL variable-list trick → fewer flash bytes).

### `game/events_data.c` — the content (the budgeted part)

The const `Event` pool + `Lore` table. This is what grows; everything above is
fixed. Split pool vs arcs:
- **Pool events** (StS): self-contained, weighted, some `rarity=ONESHOT`.
- **Arc events** (Cobalt Core): share an `arc` id; `arc_step` + `GATE_LORE_SEEN`
  enforce ordering and recurring-NPC continuity.

---

## 2. Procedural NPCs — names (free) + parametric faces (free)

### Names
Already solved: `enames.c` digraph chaining (96 B tables) → deterministic name from
any seed. An NPC's `NpcId` seed yields a stable name with zero storage. Recurring
arc NPCs carry a fixed `NpcId` so the name never changes.

### `game/r3d_face.{c,h}` — parametric vector portraits
From one `NpcId` seed, extract bitfields and draw **layered filled primitives**
straight to the framebuffer in a portrait box — "identicon, but face-shaped".

Seed-derived features:
- `species/archetype` → head silhouette + palette family (human / insectoid /
  synthetic / heavyworlder …)
- skin/carapace tone (small palette table, faction-tinted)
- eyes: count (2 / compound), shape, colour, spacing; brow / expression
- mouth / mandible
- markings: scars, tattoos, war-paint (faction accent)
- headgear: visor / breather / hood / bare (faction-gated)
- optional subtle `fbm` skin shading borrowed from `r3d_planet.c` for depth

Zero baked assets, a few hundred bytes of draw code, infinite deterministic faces.
Because the face is a pure function of the seed, recurring characters look
identical every encounter — the continuity hook the arcs need. Drawn with existing
primitives (filled ellipse/rect/tri/line) into the modal's portrait box.

---

## 3. Lore drip + Codex

- `OP_LORE id` marks a fragment seen → bit set in a save bitfield.
- `game/lore_data.c` (or alongside events): const `Lore { title, body }` table.
- New **"DATABASE"** entry on the station HOME menu (`ui_station.c` home list) →
  `ui_codex` screen: scrollable list of unlocked fragments, A to read full text
  via `craft_font_draw_frac_aa`. StS-style compendium; "more of the world slowly
  revealed."

---

## 4. Trigger & flow (increment 1: dock-arrival hails)

Hook in the `ST_DOCKING` completion block (`elite_game.c:~1784`), **before**
`station_open()`:

```
on dock complete:
    seed = system_seed ^ visit_salt ^ EVENT_SALT(TRIG_DOCK)
    ev = events_pick(seed, TRIG_DOCK, si)
    if ev: s_state = ST_EVENT; s_event = ev;   // modal first
    else:  station_open(); s_state = ST_DOCKED // normal
```

New `ST_EVENT` state: ticks `ui_event`, which returns a chosen index (or -1 while
open). On choice → `events_run_choice`, push result toast, then either continue to
`station_open()`/`ST_DOCKED`, or — if an op spawned an ambush — `ST_FLIGHT` with
hostiles (FTL trap). World stays neutralized while the modal is up
(`elite_input_neutralize()` each tick, like the other overlay states).

Later increments (same engine): `TRIG_BAR` (recurring characters in `SCR_BAR`),
`TRIG_ARRIVAL` (in-space hyperspace hail).

---

## 5. UI — reuse almost everything

`game/ui_event.{c,h}` (new, small): combine
- the existing **action-popup** choice list (`ui_station.c` popup model) for 2–4
  options, cursor, A confirm / B = "ignore" if allowed,
- a **portrait box** drawn by `r3d_face`,
- **`craft_font_draw_frac_aa`** for the body/flavor text,
- the existing **toast** for the aftermath line.

`gate`d choices render greyed with their `gate_tag` (blue-option feel). New code is
just the modal + face renderer + codex; the rest is composition.

---

## 6. Save state (tiny)

Extend the save staging:
- `lore_seen[ ]` bitfield — 32 B (256 fragments headroom).
- `npc_rep[ ]` — small table for individual-NPC standing used by arcs (e.g. 16 × i8).
- `event_recent[ ]` — ring of last-N event ids for seen-suppression (~16 B).
- arc progress folds into `lore_seen` flags (no separate structure).

Total < 100 B added to the save. Versioned with the existing save-version bump.

---

## 7. Size discipline — make overflow impossible, not unlikely

Content is the only growth vector, so budget it like the binary already is:
- **Data-driven VM** → fixed code cost regardless of event count.
- **Template + token text** → one body string → many rendered strings.
- **Procedural names + faces** → 0 asset bytes.
- **`CONTENT_MAX` build-time guard**: a tiny report of the `events_data`/`lore_data`
  section size, hard-failing the build past a cap (mirror `device/check_size.cmake`).
  Adding content can then never silently blow the 256 KB slot — it fails first.

Rough cost: ~400–500 B/event, ~150 B/lore fragment. 40 events + 30 fragments
≈ ~25 KB — comfortable, and now measured + capped.

---

## 8. Build & verification

- Add `events.c events_data.c lore_data.c r3d_face.c ui_event.c ui_codex.c` to
  `host/CMakeLists.txt`, `device/CMakeLists.txt`, `android/.../Android.mk`.
- **Host headless tests** (mirror `ELITE_*TEST` env harnesses in `host_main.c`):
  - `events_pick` determinism: same seed+visit → same event; visit_salt change → reroll.
  - Gate filtering: a choice needing `GATE_HAS_FUEL_SCOOP` is disabled without it.
  - `OP_BRANCH` payoff/backfire honours the `a`% split over many seeds.
  - `events_run_choice` op effects: CR/CARGO/REP/FUEL/LORE deltas land; ambush sets
    the spawn request; lore bit flips and persists through save round-trip.
  - Token substitution renders `{NAME}{SYSTEM}{GOOD}` correctly and never overruns.
- **Screenshot harness** (`ELITE_EVENTSHOT`, like `ELITE_STATUSTEST`): dock → modal,
  dump PPM to eyeball portrait + choices + greyed gated option.
- **Face gallery harness** (`ELITE_FACEGALLERY`): render a grid of N seeded faces to
  confirm variety + that recurring seeds reproduce identical portraits.
- Build all four shells; content-size guard must pass; user validates on device.

---

## 9. Ambient POI encounters — fixing dead planet arrivals

A planet with no station currently has *nothing* when you arrive. This is the
second axis of variety, and it's **environmental, not modal** — you resolve it by
flying, not by reading a popup. It reuses systems that already exist (loot
containers ~1.3 KB pool, the entity/ship pool, combat, the impostor planet), so
the new code is mostly a **seeded populator table**, not new mechanics.

### `game/poi_encounter.{c,h}` (new)
On arrival at a flyable POI, `poi_populate(seed, si, poi)` seeds a scene from
`addr ^ visit_salt ^ POI_SALT` (stable-per-visit, same contract as missions) by
weighted-rolling an **encounter archetype** against the system's profile
(government/threat/econ — anarchy & high-threat lean hostile; industrial leans
salvage; ice/gas lean rocks):

| Archetype        | What spawns / how you resolve it                              | Reuses |
|------------------|---------------------------------------------------------------|--------|
| `SALVAGE_FIELD`  | debris + scoopable loot containers (cargo / fuel / data)      | loot pool, scoop |
| `DERELICT`       | silent dead hull — scan → lore log, or *bridge* to a modal (board? trap?) | entity pool, §1 VM, §3 lore |
| `ROCK_CLUSTER`   | asteroids to mine → ore into cargo                            | asteroid mesh, loot |
| `PATROL`/`AMBUSH`| pirate or authority ships (hostile in anarchy, scan-you in hi-gov) | entity pool, combat |
| `DISTRESS`       | a wreck/pod that, when approached, fires a modal event       | §1 VM bridge |
| `ANOMALY`        | sensor contact → fly to it + scan → reveals a lore fragment  | §3 lore (in-world reveal) |
| `QUIET`          | genuinely empty (low weight) — space should still breathe    | — |

### How it threads in
- Hook in the supercruise/arrival path where the POI scene is set up (same place
  the planet impostor & any station are emitted in `elite_game.c`), gated to POIs
  that aren't already a busy station.
- Spawns go through the **existing** loot/entity/combat pools — no new buffers, so
  no SRAM-budget hit (re-check the .map only if a pool size needs bumping).
- **Bridge to §1:** `DERELICT`/`DISTRESS` archetypes carry an `Event*`; flying
  within scan range raises the same `ST_EVENT` modal (FTL "approach the derelict?"
  → board / scan / leave, with a trap branch). This is the one seam between the
  two axes, and it's why both must share the event VM and lore bitfield.
- **Lore in-world:** `ANOMALY`/`DERELICT` scans call `OP_LORE` directly → the slow
  world-reveal also happens out in space, not only at docks.

Content cost is tiny: the archetype weight table + a handful of spawn recipes
(~a few hundred bytes) plus whatever bridged events/lore you author (already
budgeted in §7). No new assets — ships, rocks, loot and the planet are all
existing.

### Phasing note
Increment 1 stays dock-hail-only to land the VM cleanly. The POI populator is
**Phase 2** alongside the Bar trigger (both are "more triggers feeding the same
engine"), except `SALVAGE_FIELD` + `ROCK_CLUSTER` + `PATROL` — the pure-spawn
archetypes with no modal — can land earlier and standalone, since they need only
the populator table and existing pools, not the event VM. That gives empty
planets life immediately while the narrative side matures.

---

## 10. Phasing

- **Phase 1 — engine + faces + one trigger: DONE 2026-06-12.**
  events VM + op interpreter (`events.{c,h}`), 10 pool events + 6 lore fragments
  (`events_data.c`), parametric portraits (`r3d_face.{c,h}`), modal (`ui_event.{c,h}`),
  `ST_EVENT` + dock hook + `elite_game_event_ambush`, save v5 (event bits, v3/v4
  migrate), 19 headless asserts (`ELITE_EVENTTEST`) all passing, `ELITE_EVENTSHOT`/
  `ELITE_FACEGALLERY` renders, `ELITE_EVENTFORCE=1` playtest aid. Slot build
  202,368 B of 262,144 (58 KB headroom).
- **Phase 1b — pure-spawn POI life (can land right after / beside Phase 1):**
  `poi_encounter` populator + the no-modal archetypes `SALVAGE_FIELD`,
  `ROCK_CLUSTER`, `PATROL`/`AMBUSH` over the existing loot/entity/combat pools.
  Empty planets stop feeling dead — no event VM needed for these.
- **Phase 2 — Codex + Bar trigger + first authored arc: DONE 2026-06-12.**
  Event schema grew `trig`/`need_flag`/`not_flag`/`fixed_npc`; `events_roll_bar`
  (60% per visit, rolled once on first BAR entry); DATABASE station screen
  (lore titles + read view, ENCRYPTED rows for locked); bar encounter row +
  `DOCK_EVENT` handshake (modal returns to the station, outcome saved);
  the Adjuster 3-step arc (bar → bar → dock, one campaign-stable face,
  lore 0→1→2) + 4 bar pool events (card game, navigator, war story, fixer).
  30 EVENTTEST asserts green; BARSHOT/CODEXSHOT renders; slot 207,032 B
  (53.7 KB headroom). The `DERELICT`/`DISTRESS`/`ANOMALY` modal bridges moved
  to Phase 3 — they belong with the in-space work.
- **Phase 3 — in-space: DONE 2026-06-12 (first cut).**
  Derelict hulks (24% at non-station POIs, inert TEAM_NEUTRAL ship, board by
  flying within 180 m with hostiles cleared → TRIG_SPACE modal, one per POI
  per visit); arrival hails (TRIG_ARRIVAL, 14% per drop, never over a live
  fight); `s_event_return` routing (dock-finish / docked / flight); 9 new
  events (#18-26: cold hull, last pod, still warm, grey paint post-arc,
  patrol challenge/routine sweep, waylaid, drifting trader, the stowaway
  repaid) + lore 6/7. 38 EVENTTEST asserts green. Full table of all events
  + outcomes: EVENTS_TABLE.md. Deferred: individual-NPC rep; outcome
  visibility/buffs pending user direction (EVENTS_TABLE.md "thin spots").

## 11. Backlog (user, 2026-06-12 — after the planned phases)

1. **Faction war events** — missions to support factions in major battles the
   player can join and take sides in (large multi-wing engagements, rep stakes).
2. **Ship + station model glow-up** — improve the look and ADD variety.
   RULE: change nothing until the user has seen contact sheets of ~100
   examples of the proposed improved models and approved a direction.
3. **Face variety** — more helmet/hair variety, more species/races.
   Same rule: 100-example contact sheets before changing the live look.
