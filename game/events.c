/*
 * ThumbyElite — random event engine (see events.h).
 */
#include "events.h"
#include "enames.h"
#include "mission.h"
#include "econ.h"
#include "elite_player.h"
#include "elite_entity.h"
#include <string.h>
#include <stdio.h>

/* elite_game.c: spawn a hostile wing outside the station (they wait). */
void elite_game_event_ambush(int n, int tier);

#define EVENT_DOCK_PCT 35
static int s_chance = EVENT_DOCK_PCT;
void events_set_chance(int pct) { s_chance = pct < 0 ? EVENT_DOCK_PCT : pct; }

/* Persistent bits: lore 0..127, story flags 128..159, oneshot-seen
 * 160 + event id. Carried by the save (events_save_bits). */
static uint8_t  s_bits[EVENTS_BITS_LEN];
static uint8_t  s_recent[EVENTS_RECENT_LEN];   /* last picks, anti-repeat */
static uint8_t  s_recent_at;
static uint32_t s_salt;

/* Current pick (the modal + run_choice operate on this). */
static const SystemInfo *s_si;
static int      s_station;
static uint32_t s_npc_seed;
static uint32_t s_outcome_seed;     /* branch rng — fixed at pick time */

static uint32_t mix32(uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16; return x;
}

static bool bit(int i)     { return (s_bits[(i >> 3) & 31] >> (i & 7)) & 1; }
static void bit_set(int i) { s_bits[(i >> 3) & 31] |= (uint8_t)(1 << (i & 7)); }

bool events_lore_seen(int id) { return bit(id & 127); }
bool events_flag(int id)      { return bit(128 + (id & 31)); }
uint8_t *events_save_bits(void)   { return s_bits; }
uint8_t *events_save_recent(void) { return s_recent; }
void events_set_salt(uint32_t salt) { s_salt = salt; }
uint32_t events_npc_seed(void) { return s_npc_seed; }

void events_init(void) {
    memset(s_bits, 0, sizeof s_bits);
    memset(s_recent, 0xFF, sizeof s_recent);
    s_recent_at = 0;
    s_salt = 0;
}

/* --- gates -------------------------------------------------------------- */
static int illegal_units(void) {
    int n = 0;
    for (int g = 0; g < N_GOODS; g++)
        if (k_goods[g].flags & GOOD_ILLEGAL) n += g_player.cargo[g];
    return n;
}

static bool gate_ok(uint16_t gate, const SystemInfo *si) {
    if ((gate & GATE_LAWFUL) && si->gov < GOV_CONFED) return false;
    if ((gate & GATE_ROUGH) && si->gov > GOV_FEUDAL) return false;
    if ((gate & GATE_THREAT) && si->threat < 2) return false;
    if ((gate & GATE_ILLEGAL) && illegal_units() == 0) return false;
    if ((gate & GATE_CARGO_SPACE) &&
        player_cargo_total() + 2 > player_cargo_cap()) return false;
    if ((gate & GATE_FUEL_SPARE) && g_player.fuel < 2.0f) return false;
    if ((gate & GATE_CLEAN) && g_player.legal != 0) return false;
    if ((gate & GATE_WANTED) && g_player.legal == 0) return false;
    if ((gate & GATE_HAS_MEDS) && g_player.cargo[5] == 0) return false;
    return true;
}

bool events_choice_enabled(const Event *ev, int choice) {
    if (choice < 0 || choice >= ev->n_choices) return false;
    const Choice *c = &ev->choices[choice];
    if (c->cost > 0 && g_player.credits < c->cost) return false;
    return gate_ok(c->gate, s_si);
}

/* --- selection ----------------------------------------------------------- */
static bool in_recent(uint8_t id) {
    for (int i = 0; i < EVENTS_RECENT_LEN; i++)
        if (s_recent[i] == id) return true;
    return false;
}

const Event *events_roll_dock(const SystemInfo *si, int station) {
    s_salt += 0x9E3779B9u;
    uint32_t h = mix32((uint32_t)(si->seed >> 8) ^ s_salt ^
                       (uint32_t)((station + 1) * 0x85EBCA6Bu));
    if ((int)(h % 100u) >= s_chance) return NULL;

    /* Weighted pick over the eligible pool. */
    int total = 0;
    for (int i = 0; i < k_n_events; i++) {
        const Event *e = &k_events[i];
        if ((e->flags & EV_ONESHOT) && bit(160 + e->id)) continue;
        if (in_recent(e->id)) continue;
        if (!gate_ok(e->gate, si)) continue;
        total += e->weight;
    }
    if (total <= 0) return NULL;
    int pick = (int)(mix32(h ^ 0xC2B2AE35u) % (uint32_t)total);
    const Event *ev = NULL;
    for (int i = 0; i < k_n_events; i++) {
        const Event *e = &k_events[i];
        if ((e->flags & EV_ONESHOT) && bit(160 + e->id)) continue;
        if (in_recent(e->id)) continue;
        if (!gate_ok(e->gate, si)) continue;
        pick -= e->weight;
        if (pick < 0) { ev = e; break; }
    }
    if (!ev) return NULL;

    s_si = si;
    s_station = station;
    s_npc_seed = mix32(h ^ (uint32_t)ev->id * 0x9E3779B9u);
    s_outcome_seed = mix32(s_npc_seed ^ 0x6A09E667u);
    if (ev->flags & EV_ONESHOT) bit_set(160 + ev->id);
    s_recent[s_recent_at] = ev->id;
    s_recent_at = (uint8_t)((s_recent_at + 1) % EVENTS_RECENT_LEN);
    return ev;
}

/* --- text ----------------------------------------------------------------
 * $N npc name, $S system, $T station, $F local faction, $G trade good. */
static int seeded_good(void) { return (int)(s_npc_seed % 16u); /* legal */ }

void events_expand(const char *tmpl, char *out, int cap) {
    int o = 0;
    for (const char *p = tmpl; *p && o < cap - 1; p++) {
        if (*p != '$' || !p[1]) { out[o++] = *p; continue; }
        char tok[20] = "";
        switch (*++p) {
        case 'N': ename_system(s_npc_seed | 1u, tok); break;
        case 'S': if (s_si) memcpy(tok, s_si->name, sizeof s_si->name); break;
        case 'T': if (s_si && s_station >= 0 && s_station < s_si->n_stations)
                      memcpy(tok, s_si->stations[s_station].name, 20);
                  break;
        case 'F': if (s_si) snprintf(tok, sizeof tok, "%s",
                      k_faction_names[system_faction(s_si->addr)]);
                  break;
        case 'G': snprintf(tok, sizeof tok, "%s",
                      k_goods[seeded_good()].name);
                  break;
        default:  tok[0] = *p; tok[1] = 0; break;
        }
        tok[19] = 0;
        for (const char *t = tok; *t && o < cap - 1; t++) out[o++] = *t;
    }
    out[o] = 0;
}

/* --- outcome interpreter -------------------------------------------------*/
int events_run_choice(const Event *ev, int choice) {
    if (!events_choice_enabled(ev, choice)) return -1;
    const Choice *c = &ev->choices[choice];
    if (c->cost > 0) g_player.credits -= c->cost;

    /* Branch rng: seeded at pick time + choice — same visit, same fate. */
    uint32_t rng = mix32(s_outcome_seed ^ (uint32_t)(choice * 0x9E3779B9u));
    int result = -1;
    const Op *ops = c->ops;
    for (int pc = 0; ops && pc < 32; pc++) {
        const Op *op = &ops[pc];
        switch (op->op) {
        case OP_END: return result;
        case OP_CR: {
            g_player.credits += (int32_t)op->a * 25;
            if (g_player.credits < 0) g_player.credits = 0;
            break;
        }
        case OP_CARGO: {
            int g = (op->a < 0) ? seeded_good() : op->a;
            if (g < 0 || g >= N_GOODS) break;
            int n = g_player.cargo[g] + op->b;
            if (n < 0) n = 0;
            int over = (player_cargo_total() - g_player.cargo[g] + n) -
                       player_cargo_cap();
            if (over > 0) n -= over;
            if (n < 0) n = 0;
            g_player.cargo[g] = (uint8_t)n;
            break;
        }
        case OP_REP: {
            int f = (op->a < 0) ? (int)system_faction(s_si->addr) : op->a;
            int r = g_rep[f] + op->b;
            if (r < -100) r = -100;
            if (r > 100) r = 100;
            g_rep[f] = (int8_t)r;
            break;
        }
        case OP_FUEL: {
            float fu = g_player.fuel + (float)op->a * 0.1f;
            if (fu < 0) fu = 0;
            if (fu > g_player.fuel_max) fu = g_player.fuel_max;
            g_player.fuel = fu;
            break;
        }
        case OP_DMG: {
            Ship *p = &g_ships[PLAYER];
            p->hull -= p->hull_max * (float)op->a * 0.01f;
            if (p->hull < 1.0f) p->hull = 1.0f;   /* events never kill */
            break;
        }
        case OP_AMBUSH:
            elite_game_event_ambush(op->a, op->b);
            break;
        case OP_LORE:
            bit_set(op->a & 127);
            break;
        case OP_FLAG:
            bit_set(128 + (op->a & 31));
            break;
        case OP_BRANCH:
            rng = mix32(rng);
            if ((int)(rng % 100u) < op->a) pc = op->b - 1;
            break;
        case OP_RESULT:
            result = op->a;
            break;
        case OP_LEGAL: {
            int l = (int)g_player.legal + op->a;
            if (l < 0) l = 0;
            if (l > 2) l = 2;
            g_player.legal = (uint8_t)l;
            break;
        }
        case OP_CONTRA:
            for (int g = 0; g < N_GOODS; g++)
                if (k_goods[g].flags & GOOD_ILLEGAL) g_player.cargo[g] = 0;
            break;
        default:
            return result;
        }
    }
    return result;
}
