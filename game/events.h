/*
 * ThumbyElite — random events: one tiny op-list interpreter, many const
 * data rows (events_data.c). Adding content costs flash data bytes only,
 * never code. NPC names + faces derive from the pick seed (zero assets).
 *
 * Selection is seeded per dock visit (system seed ^ visit salt), so a
 * given arrival always offers the same event — no save-scumming — but
 * the next visit rerolls. One-shot events and lore reveals persist in a
 * 256-bit field carried by the save.
 */
#ifndef EVENTS_H
#define EVENTS_H

#include "galaxy_gen.h"
#include <stdint.h>
#include <stdbool.h>

/* --- outcome bytecode -------------------------------------------------- */
enum {
    OP_END = 0,
    OP_CR,        /* credits += a*25                                       */
    OP_CARGO,     /* good a (-1 = seeded trade good) count += b (clamped)  */
    OP_REP,       /* faction a (-1 = local) rep += b                       */
    OP_FUEL,      /* fuel += a tenths of LY (clamped 0..max)               */
    OP_DMG,       /* hull -= a percent of max (never lethal)               */
    OP_AMBUSH,    /* a pirates of tier b spawned outside — wait for launch */
    OP_LORE,      /* reveal lore fragment a (sets bit, result shows text)  */
    OP_FLAG,      /* set story flag a (gate later events on it)            */
    OP_BRANCH,    /* a% chance: jump to op index b, else fall through      */
    OP_RESULT,    /* aftermath text = event->texts[a] (last one wins)      */
    OP_LEGAL,     /* legal status += a (0 clean / 1 offender / 2 fugitive) */
    OP_CONTRA,    /* confiscate all illegal cargo                          */
};

typedef struct { uint8_t op; int8_t a; int8_t b; } Op;

/* --- gates (event must pass all set bits to be offered; a CHOICE that
 *     fails its gate shows greyed — the FTL "blue option" inverted) ----- */
#define GATE_LAWFUL      0x0001   /* gov >= CONFED                  */
#define GATE_ROUGH       0x0002   /* anarchy/feudal                 */
#define GATE_THREAT      0x0004   /* threat >= 2                    */
#define GATE_ILLEGAL     0x0008   /* carrying illegal cargo         */
#define GATE_CARGO_SPACE 0x0010   /* >= 2 free cargo                */
#define GATE_FUEL_SPARE  0x0020   /* fuel >= 2.0 ly                 */
#define GATE_CLEAN       0x0040   /* legal == CLEAN                 */
#define GATE_WANTED      0x0080   /* legal > CLEAN                  */
#define GATE_HAS_MEDS    0x0100   /* carrying MEDICINE              */

typedef struct {
    const char *label;        /* "GIVE THEM FUEL"                          */
    uint16_t    gate;         /* bits required to enable (0 = always)      */
    int16_t     cost;         /* credits required AND deducted (0 = free)  */
    const Op   *ops;          /* OP_END-terminated                         */
} Choice;

#define EV_ONESHOT 0x01       /* never offered again once seen            */

/* NPC portrait archetype hint (biases r3d_face). */
enum { NK_CIVILIAN = 0, NK_OFFICIAL, NK_PIRATE, NK_MYSTIC, NK_DOCKHAND,
       NK_NONE = 0xFF };

typedef struct {
    uint8_t  id;              /* stable, unique — seen/suppression key     */
    uint8_t  weight;
    uint8_t  flags;           /* EV_*                                      */
    uint8_t  npc_kind;        /* NK_*                                      */
    uint16_t gate;            /* bits required to offer at all             */
    const char *title;
    const char *body;         /* tokens: $N name $S system $T station
                                         $F faction $G trade good          */
    const char *const *texts; /* OP_RESULT strings (token-expanded too)    */
    const Choice *choices;
    uint8_t n_choices;
} Event;

/* --- pool (events_data.c) ---------------------------------------------- */
extern const Event k_events[];
extern const int   k_n_events;
extern const char *const k_lore[];
extern const int   k_n_lore;

/* --- engine ------------------------------------------------------------ */
void events_init(void);                       /* new game: clear all bits */

/* Roll the dock-arrival hail. NULL = quiet arrival (most docks). */
const Event *events_roll_dock(const SystemInfo *si, int station);

bool events_choice_enabled(const Event *ev, int choice);
/* Deduct cost, run ops. Returns texts[] index for the aftermath panel,
 * or -1 (generic). Outcome is deterministic per visit (branch rng is
 * seeded by the pick) — choosing, reloading and rechoosing can't reroll. */
int events_run_choice(const Event *ev, int choice);

/* Expand $-tokens of this event's pick (NPC name etc.) into out. */
void events_expand(const char *tmpl, char *out, int cap);
uint32_t events_npc_seed(void);               /* current pick's NPC        */

bool events_lore_seen(int id);
bool events_flag(int id);

/* Save bridge: 32B bit-field (lore 0..127, flags 128..159, seen 160..255)
 * + 8-id recent ring. Pointers into live state — memcpy in/out. */
uint8_t *events_save_bits(void);              /* EVENTS_BITS_LEN          */
uint8_t *events_save_recent(void);            /* EVENTS_RECENT_LEN        */
#define EVENTS_BITS_LEN   32
#define EVENTS_RECENT_LEN 8

/* Tests: pin the visit salt for deterministic asserts. */
void events_set_salt(uint32_t salt);
/* Tests/harnesses: override the hail chance (0 disables, -1 restores
 * the default). Scripted button-drivers must not hit surprise modals. */
void events_set_chance(int pct);

#endif
