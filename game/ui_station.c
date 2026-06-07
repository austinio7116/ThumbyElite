/*
 * ThumbyElite — docked station services.
 *
 * HOME menu -> MARKET (scrollable commodity table, A buy / B sell with
 * hold-autorepeat) / REFUEL / LAUNCH. Stubs list the Phase 7/8 services
 * so the player can see what's coming.
 */
#include "ui_station.h"
#include "elite_types.h"
#include "elite_player.h"
#include "system_sim.h"
#include "econ.h"
#include "elite_ships.h"
#include "ui_status.h"
#include "ui_icons.h"
#include "elite_audio.h"
#include "ui_detail.h"
#include "elite_entity.h"
#include "mission.h"
#include "enames.h"
#include "elite_weapons.h"
#include "craft_font.h"
#include <stdio.h>
#include <string.h>

#define COL_BG     RGB565C(  6,  10,  20)
#define COL_HDR    RGB565C(200, 210, 225)
#define COL_GRID   RGB565C( 28,  40,  58)
#define COL_TXT    RGB565C(120, 255, 120)
#define COL_DIM    RGB565C(110, 116, 135)
#define COL_CUR    RGB565C(120, 255, 120)
#define COL_CRED   RGB565C(255, 200,  60)
#define COL_WARN   RGB565C(255, 120,  70)
#define COL_ILL    RGB565C(220, 100, 200)

typedef enum {
    SCR_HOME = 0, SCR_MARKET, SCR_SHIPYARD, SCR_OUTFIT, SCR_STATUS,
    SCR_MISSIONS, SCR_BAR
} Screen;

static Screen s_screen;
static int s_station;
static int s_cursor;
static int s_scroll;
static int s_bought[N_GOODS];     /* session purchases (depletes stock) */
static CraftRawButtons s_prev;
static float s_hold_a, s_hold_b, s_repeat;
static char s_toast[24];
static float s_toast_t;
static int s_detail;       /* 0 = list, 1 = detail sheet open */

#define HOME_ITEMS 10
static const char *k_home[HOME_ITEMS] = {
    "MARKET", "SHIPYARD", "OUTFITTING", "MISSIONS", "BAR", "STATUS",
    "REFUEL", "REARM", "PAY FINE", "LAUNCH",
};
static Mission s_offers[MISSION_OFFERS];

/* Shipyard stock: each dockyard rolls its own 5 ships. */
#define YARD_OFFERS 5
typedef struct {
    uint8_t cls;
    uint32_t seed;
    char name[16];
} YardOffer;
static YardOffer s_yard[YARD_OFFERS];

static void yard_build(void) {
    const SystemInfo *si = system_info();
    uint32_t h = (uint32_t)(si->seed >> 16) ^
                 (uint32_t)((s_station + 1) * 2654435761u);
    uint8_t used[N_HULLS] = {0};
    for (int i = 0; i < YARD_OFFERS; i++) {
        h ^= h << 13; h ^= h >> 17; h ^= h << 5;
        /* Tech gates the big stuff; always at least one small hull. */
        int max_cls = 2 + (si->stations[s_station].tech * 8) / 15;
        if (max_cls >= N_HULLS) max_cls = N_HULLS - 1;
        int cls = (i == 0) ? (int)(h % 3u) : (int)(h % (uint32_t)(max_cls + 1));
        if (used[cls]) cls = (cls + 1) % (max_cls + 1);
        used[cls] = 1;
        s_yard[i].cls = (uint8_t)cls;
        h ^= h << 13; h ^= h >> 17; h ^= h << 5;
        s_yard[i].seed = h;
        /* "SPARROW-K4": class name + seed-derived mark. */
        snprintf(s_yard[i].name, sizeof s_yard[i].name, "%s-%c%d",
                 k_hulls[cls].name, (char)('A' + (h >> 8) % 26u),
                 (int)(1 + (h >> 16) % 9u));
    }
}

void station_open(int station_idx) {
    s_detail = 0;
    s_screen = SCR_HOME;
    s_station = station_idx;
    s_cursor = 0;
    s_scroll = 0;
    memset(s_bought, 0, sizeof s_bought);
    /* Debounce: everything counts as held until released once.
     * (Per-field true — memset(0xFF) breaks _Bool negation.) */
    s_prev.up = s_prev.down = s_prev.left = s_prev.right = true;
    s_prev.a = s_prev.b = s_prev.lb = s_prev.rb = s_prev.menu = true;
    s_hold_a = s_hold_b = s_repeat = 0;
    s_toast[0] = 0;
    s_toast_t = 0;
}

static void toast(const char *msg) {
    snprintf(s_toast, sizeof s_toast, "%s", msg);
    s_toast_t = 1.6f;
}

int station_preview2(uint32_t *mesh_seed, int *class_hint) {
    if (s_screen == SCR_HOME) return 1;
    if (s_screen == SCR_SHIPYARD && s_cursor < YARD_OFFERS) {
        *mesh_seed = s_yard[s_cursor].seed;
        *class_hint = s_yard[s_cursor].cls;
        return 2;
    }
    if (s_screen == SCR_STATUS) return 3;   /* own ship in the bay */
    return 0;
}

void station_toast(const char *msg) {
    snprintf(s_toast, sizeof s_toast, "%s", msg);
    s_toast_t = 2.8f;
}

static void try_buy(int good) {
    const SystemInfo *si = system_info();
    int price = econ_price(si, s_station, good, true);
    int stock = econ_stock(si, s_station, good) - s_bought[good];
    if (price <= 0) { toast("NO TRADE"); return; }
    if (stock <= 0) { toast("NO STOCK"); return; }
    if (g_player.credits < price) { toast("NO CREDITS"); return; }
    if (player_cargo_total() >= player_cargo_cap()) { toast("HOLD FULL"); return; }
    g_player.credits -= price;
    g_player.cargo[good]++;
    s_bought[good]++;
}

static void try_sell(int good) {
    const SystemInfo *si = system_info();
    int price = econ_price(si, s_station, good, false);
    if (g_player.cargo[good] == 0) { toast("NONE HELD"); return; }
    if (price <= 0) { toast("NO TRADE"); return; }
    g_player.cargo[good]--;
    g_player.credits += price;
}

static void try_refuel(void) {
    float need = g_player.fuel_max - g_player.fuel;
    if (need < 0.1f) { toast("TANK FULL"); return; }
    int cost = (int)(need * 12.0f) + 1;
    if (g_player.credits < cost) {
        /* Partial refuel with whatever credits allow. */
        float ly = (float)g_player.credits / 12.0f;
        if (ly < 0.1f) { toast("NO CREDITS"); return; }
        g_player.fuel += ly;
        g_player.credits = 0;
        toast("PART REFUEL");
        return;
    }
    g_player.credits -= cost;
    g_player.fuel = g_player.fuel_max;
    toast("REFUELLED");
}

static void try_rearm(void) {
    int cost = player_rearm_cost();
    if (cost <= 0) { toast("MAGAZINES FULL"); return; }
    if (g_player.credits < cost) { toast("NO CREDITS"); return; }
    g_player.credits -= cost;
    player_rearm();
    player_apply_to_ship();
    char buf[24];
    snprintf(buf, sizeof buf, "REARMED -%dCR", cost);
    toast(buf);
}

/* --- shipyard ----------------------------------------------------------*/
static void shipyard_buy(int offer) {
    int hull_id = s_yard[offer].cls;
    if (hull_id == g_player.hull_id &&
        s_yard[offer].seed == g_player.hull_seed) {
        toast("CURRENT SHIP");
        return;
    }
    const HullDef *h = &k_hulls[hull_id];
    int tradein = (k_hulls[g_player.hull_id].price * 7) / 10;
    int cost = h->price - tradein;
    if (cost < 0) cost = 0;
    if (g_player.credits < cost) { toast("NO CREDITS"); return; }
    /* Cargo must fit the new hold. */
    if (player_cargo_total() > h->cargo) { toast("CARGO WONT FIT"); return; }
    g_player.credits -= cost;
    g_player.hull_id = (uint8_t)hull_id;
    g_player.hull_seed = s_yard[offer].seed;
    /* Turret: keep it if the new frame has a hardpoint; rack or sell
     * otherwise. */
    if (g_player.turret_eq.in_use && !h->has_turret) {
        int sl = -1;
        for (int t = 0; t < h->rack && t < MAX_SALVAGE; t++)
            if (!g_player.salvage[t].in_use) { sl = t; break; }
        if (sl >= 0 && player_cargo_total() < h->cargo)
            g_player.salvage[sl] = g_player.turret_eq;
        else
            g_player.credits += (int)(instance_price(&g_player.turret_eq) *
                                      0.35f);
        g_player.turret_eq.in_use = 0;
    }
    /* Fitted equipment over the frame's tier cap drops a tier. */
    if (g_player.shield_eq.in_use &&
        g_player.shield_eq.tier > h->max_shield_tier)
        g_player.shield_eq.tier = h->max_shield_tier;
    if (g_player.armor_eq.in_use &&
        g_player.armor_eq.tier > h->max_hull_tier)
        g_player.armor_eq.tier = h->max_hull_tier;
    /* Mounts that don't fit (count or size) drop to salvage, else sell. */
    for (int i = 0; i < HULL_SLOTS; i++) {
        WeaponInst *m = &g_player.mounts[i];
        if (!m->in_use) continue;
        bool fits = i < h->n_slots &&
                    k_weapons[m->type].size <= h->slot_size[i];
        if (fits) continue;
        int sl = -1;
        for (int t = 0; t < h->rack && t < MAX_SALVAGE; t++)
            if (!g_player.salvage[t].in_use) { sl = t; break; }
        if (sl >= 0 && player_cargo_total() < h->cargo) {
            g_player.salvage[sl] = *m;
        } else {
            g_player.credits += weapon_price(m->type, m->quality) / 2;
        }
        m->in_use = 0;
    }
    player_apply_to_ship();
    g_ships[PLAYER].hull = g_ships[PLAYER].hull_max;   /* delivered fresh */
    g_ships[PLAYER].shield = g_ships[PLAYER].shield_max;
    toast("SHIP DELIVERED");
}

/* --- outfitting ----------------------------------------------------------
 * Row model: mounts, then upgrades, then the salvage rack, then the
 * shop list. Rebuilt every tick (cheap; counts change under actions). */
typedef enum {
    ROW_MOUNT, ROW_EQUIP, ROW_UTIL, ROW_TURRET, ROW_SALV, ROW_SHOP,
    ROW_EQSHOP, ROW_UTILSHOP, ROW_HDR
} RowKind;
typedef struct { uint8_t kind, index; uint8_t tier; } OutfitRow;
static OutfitRow s_rows[HULL_SLOTS + 4 + MAX_SALVAGE + WPN_COUNT + 17];
static int s_n_rows;

/* Per-terminal armoury (user req): each station stocks a seeded,
 * tech-gated subset of the catalogue at economy-biased prices, plus
 * the occasional FEATURED non-standard instance (RNF/MIL/PRO) — the
 * legendary-gun reward for exploring far terminals. */
#define ARMORY_MAX 9
typedef struct {
    uint8_t type, quality, featured, affix;
    int32_t price;
} ArmoryItem;
static ArmoryItem s_armory[ARMORY_MAX];
static int s_n_armory;

static float econ_weapon_mult(int econ) {
    static const float k_m[8] = {
        /* AGRI */ 1.15f, /* INDUST */ 0.95f, /* HITECH */ 0.93f,
        /* EXTRACT */ 1.05f, /* REFINE */ 1.00f, /* TOURISM */ 1.10f,
        /* MILITARY */ 0.85f, /* SERVICE */ 1.00f,
    };
    return k_m[econ & 7];
}

static void armory_build(void) {
    const SystemInfo *si = system_info();
    const StationInfo *st = &si->stations[s_station];
    uint32_t h = (uint32_t)(si->seed >> 18) ^
                 (uint32_t)((s_station + 3) * 2654435761u) ^ 0xA4A4u;
    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    float mult = econ_weapon_mult(st->econ) *
                 (0.95f + 0.10f * (float)(h & 0xFF) * (1.0f / 255.0f));

    /* Stock pool by tech: basics everywhere, exotics need a real yard. */
    uint8_t pool[WPN_COUNT];
    int pn = 0;
    pool[pn++] = WPN_PULSE_S;
    pool[pn++] = WPN_AUTOCANNON;
    pool[pn++] = WPN_PULSE_M;
    pool[pn++] = WPN_MISSILE;
    pool[pn++] = WPN_TRACTOR;
    pool[pn++] = WPN_MINING;
    if (st->tech >= 4) {
        pool[pn++] = WPN_FLAK;
        pool[pn++] = WPN_MINE;
    }
    if (st->tech >= 6) {
        pool[pn++] = WPN_BEAM;
        pool[pn++] = WPN_PULSE_L;
        pool[pn++] = WPN_HOMING;
    }
    if (st->tech >= 8)
        pool[pn++] = WPN_ION;
    if (st->tech >= 11) {
        pool[pn++] = WPN_PHOTON;
        pool[pn++] = WPN_GAUSS;
    }
    if (st->tech >= 12)
        pool[pn++] = WPN_RAILGUN;
    int want = 4 + (int)((h >> 8) % 3u);          /* 4-6 lines */
    if (want > pn) want = pn;
    int startp = (int)((h >> 16) % (uint32_t)pn);
    s_n_armory = 0;
    for (int k = 0; k < want; k++) {
        ArmoryItem *it = &s_armory[s_n_armory++];
        it->type = pool[(startp + k * 2 + (k > 2)) % pn];
        /* avoid dupes from the stride */
        for (int j = 0; j < s_n_armory - 1; j++)
            if (s_armory[j].type == it->type) { s_n_armory--; break; }
    }
    for (int i = 0; i < s_n_armory; i++) {
        s_armory[i].quality = Q_STANDARD;
        s_armory[i].featured = 0;
        s_armory[i].affix = AFX_NONE;
        h ^= h << 13; h ^= h >> 17; h ^= h << 5;
        if ((h % 10u) == 0) s_armory[i].affix = AFX_SURPLUS;  /* bargain bin */
        s_armory[i].price =
            (int32_t)(weapon_price(s_armory[i].type, Q_STANDARD) * mult *
                      k_affixes[s_armory[i].affix].price);
    }

    /* Featured offers: 0-2, likelier at high tech. ANY weapon can
     * appear — a PRO gauss at a backwater is the exploration jackpot. */
    int n_feat = 0;
    uint32_t f = h * 0x9E3779B9u;
    f ^= f >> 15;
    if ((int)(f % 100u) < 25 + st->tech * 4) n_feat++;
    if (st->tech >= 9 && (int)((f >> 8) % 100u) < 30) n_feat++;
    for (int k = 0; k < n_feat && s_n_armory < ARMORY_MAX; k++) {
        f ^= f << 13; f ^= f >> 17; f ^= f << 5;
        ArmoryItem *it = &s_armory[s_n_armory++];
        it->type = (uint8_t)(f % WPN_COUNT);
        int qr = (int)((f >> 8) % 100u);
        it->quality = (qr < 60) ? Q_REINFORCED
                    : (qr < 90) ? Q_MILITARY : Q_PROTOTYPE;
        it->featured = 1;
        f ^= f << 13; f ^= f >> 17; f ^= f << 5;
        it->affix = (f & 1) ? (uint8_t)(AFX_OVERCLOCKED + f % 4u)
                            : AFX_NONE;
        if (it->affix >= AFX_COUNT) it->affix = AFX_VENTED;
        it->price = (int32_t)(weapon_price(it->type, it->quality) * mult *
                              k_affixes[it->affix].price);
    }
}

static void outfit_build_rows(void) {
    const HullDef *h = &k_hulls[g_player.hull_id];
    s_n_rows = 0;
    /* Sections: YOUR SHIP / YOUR HOLD / STATION SHOP (user req). */
    s_rows[s_n_rows++] = (OutfitRow){ ROW_HDR, 0, 0 };
    for (int i = 0; i < h->n_slots; i++)
        s_rows[s_n_rows++] = (OutfitRow){ ROW_MOUNT, (uint8_t)i, 0 };
    s_rows[s_n_rows++] = (OutfitRow){ ROW_EQUIP, 0, 0 };   /* shield */
    s_rows[s_n_rows++] = (OutfitRow){ ROW_EQUIP, 1, 0 };   /* armor */
    for (int u = 0; u < player_util_slots(); u++)
        s_rows[s_n_rows++] = (OutfitRow){ ROW_UTIL, (uint8_t)u, 0 };
    if (h->has_turret)
        s_rows[s_n_rows++] = (OutfitRow){ ROW_TURRET, 0, 0 };
    s_rows[s_n_rows++] = (OutfitRow){ ROW_HDR, 1, 0 };
    for (int i = 0; i < MAX_SALVAGE; i++)
        if (g_player.salvage[i].in_use)
            s_rows[s_n_rows++] = (OutfitRow){ ROW_SALV, (uint8_t)i, 0 };
    s_rows[s_n_rows++] = (OutfitRow){ ROW_HDR, 2, 0 };
    for (int i = 0; i < s_n_armory; i++)
        s_rows[s_n_rows++] = (OutfitRow){ ROW_SHOP, (uint8_t)i, 0 };
    /* Equipment shop: tiers the frame can take. */
    for (int t = 1; t <= h->max_shield_tier; t++)
        s_rows[s_n_rows++] = (OutfitRow){ ROW_EQSHOP, 0, (uint8_t)t };
    for (int t = 1; t <= h->max_hull_tier; t++)
        s_rows[s_n_rows++] = (OutfitRow){ ROW_EQSHOP, 1, (uint8_t)t };
    /* Gadgets: heatsink/scanner/tank everywhere; the fancy ones need
     * tech. index = EQ type offset from EQ_HEATSINK. */
    {
        const SystemInfo *si2 = system_info();
        int tech = si2->stations[s_station].tech;
        s_rows[s_n_rows++] = (OutfitRow){ ROW_UTILSHOP, 0, 0 }; /* heatsink */
        s_rows[s_n_rows++] = (OutfitRow){ ROW_UTILSHOP, 1, 0 }; /* scanner */
        s_rows[s_n_rows++] = (OutfitRow){ ROW_UTILSHOP, 2, 0 }; /* tank */
        if (tech >= 7)
            s_rows[s_n_rows++] = (OutfitRow){ ROW_UTILSHOP, 3, 0 }; /* scoop */
        if (tech >= 9)
            s_rows[s_n_rows++] = (OutfitRow){ ROW_UTILSHOP, 4, 0 }; /* tcomp */
        if (tech >= 5)
            s_rows[s_n_rows++] = (OutfitRow){ ROW_UTILSHOP, 5, 0 }; /* chaff */
    }
}

static WeaponInst *equip_slot(int which) {
    return which ? &g_player.armor_eq : &g_player.shield_eq;
}

/* Buy overflow: no compatible slot free -> the purchase goes to the
 * hold rack instead (user req: full mounts must not block buying). */
static bool buy_to_rack(const WeaponInst *inst) {
    int sl = player_free_rack_slot();
    if (sl < 0 || player_cargo_total() >= player_cargo_cap()) return false;
    g_player.salvage[sl] = *inst;
    return true;
}

/* Can this row open a detail sheet? Single source of truth for the LB
 * open gate AND the in-sheet LB/RB cycle (they had drifted apart and
 * the new row types couldn't open at all — user report). */
static bool row_detailable(const OutfitRow *r) {
    switch (r->kind) {
    case ROW_MOUNT:  return g_player.mounts[r->index].in_use;
    case ROW_EQUIP:  return equip_slot(r->index)->in_use;
    case ROW_UTIL:   return g_player.util_eq[r->index].in_use;
    case ROW_TURRET: return g_player.turret_eq.in_use;
    case ROW_SALV:
    case ROW_SHOP:
    case ROW_EQSHOP:
    case ROW_UTILSHOP:
        return true;
    default:
        return false;
    }
}

static int repair_cost(const WeaponInst *w) {
    int base = weapon_price(w->type, w->quality);
    return (int)((100 - w->integrity) * base / 100 * 0.6f *
                 skill_repair_mult()) + 1;
}

static int free_slot_for(int wpn_type) {
    const HullDef *h = &k_hulls[g_player.hull_id];
    for (int i = 0; i < h->n_slots; i++)
        if (!g_player.mounts[i].in_use &&
            k_weapons[wpn_type].size <= h->slot_size[i])
            return i;
    return -1;
}

static void outfit_action_a(int row) {
    if (row >= s_n_rows) return;
    const OutfitRow *r = &s_rows[row];
    const HullDef *h = &k_hulls[g_player.hull_id];
    switch (r->kind) {
    case ROW_MOUNT: {
        WeaponInst *m = &g_player.mounts[r->index];
        if (!m->in_use) { toast("EMPTY MOUNT"); return; }
        if (m->integrity >= 100) { toast("NO DAMAGE"); return; }
        int cost = repair_cost(m);
        if (g_player.credits < cost) { toast("NO CREDITS"); return; }
        g_player.credits -= cost;
        m->integrity = 100;
        g_player.xp_tech += 1;
        player_apply_to_ship();
        toast("REPAIRED");
        break;
    }
    case ROW_EQUIP: {
        WeaponInst *e = equip_slot(r->index);
        if (!e->in_use) { toast("NOT FITTED"); return; }
        if (e->integrity >= 100) { toast("NO DAMAGE"); return; }
        int cost = (int)((100 - e->integrity) *
                         equip_price(e->type, e->tier, e->quality) / 100 *
                         0.6f * skill_repair_mult()) + 1;
        if (g_player.credits < cost) { toast("NO CREDITS"); return; }
        g_player.credits -= cost;
        e->integrity = 100;
        g_player.xp_tech += 1;
        player_apply_to_ship();
        toast("REPAIRED");
        break;
    }
    case ROW_UTIL: {
        WeaponInst *e = &g_player.util_eq[r->index];
        if (!e->in_use) { toast("EMPTY BAY"); return; }
        if (e->integrity >= 100) { toast("NO DAMAGE"); return; }
        int cost = (int)((100 - e->integrity) * instance_price(e) / 100 *
                         0.6f * skill_repair_mult()) + 1;
        if (g_player.credits < cost) { toast("NO CREDITS"); return; }
        g_player.credits -= cost;
        e->integrity = 100;
        g_player.xp_tech += 1;
        toast("REPAIRED");
        break;
    }
    case ROW_UTILSHOP: {
        int type = EQ_HEATSINK + r->index;
        const SystemInfo *sie = system_info();
        int price = (int)(k_equip[type - WPN_COUNT].base_price *
                          econ_weapon_mult(sie->stations[s_station].econ) *
                          skill_price_mult());
        if (g_player.credits < price) { toast("NO CREDITS"); return; }
        int slot = -1;
        for (int u = 0; u < player_util_slots(); u++)
            if (!g_player.util_eq[u].in_use) { slot = u; break; }
        WeaponInst inst = { .type = (uint8_t)type, .quality = Q_STANDARD,
                            .integrity = 100, .in_use = 1 };
        if (slot >= 0) {
            g_player.credits -= price;
            g_player.util_eq[slot] = inst;
            if (type == EQ_CHAFF) g_player.chaff_charges = 4;
            toast("FITTED");
        } else if (buy_to_rack(&inst)) {
            g_player.credits -= price;
            toast("TO HOLD");
        } else {
            toast("NO SPACE");
        }
        break;
    }
    case ROW_TURRET: {
        if (g_player.turret_eq.in_use) { toast("ALREADY FITTED"); return; }
        for (int t = 0; t < MAX_SALVAGE; t++) {
            WeaponInst *sv = &g_player.salvage[t];
            if (!sv->in_use || sv->type >= WPN_COUNT) continue;
            if (k_weapons[sv->type].size > 1) continue;
            g_player.turret_eq = *sv;
            sv->in_use = 0;
            player_apply_to_ship();
            toast("TURRET ARMED");
            return;
        }
        toast("NEED Z1 ON RACK");
        break;
    }
    case ROW_EQSHOP: {
        int type = WPN_COUNT + r->index;
        const SystemInfo *sie = system_info();
        /* This station's variant for this tier (seeded). */
        uint32_t vh = (uint32_t)(sie->seed >> 22) ^
                      (uint32_t)((s_station + 1) * 7129u) ^
                      (uint32_t)(r->index * 31u + r->tier);
        vh ^= vh >> 13; vh *= 1274126177u; vh ^= vh >> 16;
        uint8_t variant = (uint8_t)(vh % 4u);
        float vprice = (variant == SHV_BULWARK || variant == ARV_ABLATIVE)
                           ? 1.25f : (variant ? 1.15f : 1.0f);
        int price = (int)(equip_price(type, r->tier, Q_STANDARD) *
                          econ_weapon_mult(sie->stations[s_station].econ) *
                          vprice * skill_price_mult());
        if (g_player.credits < price) { toast("NO CREDITS"); return; }
        WeaponInst *e = equip_slot(r->index);
        WeaponInst inst = { .type = (uint8_t)type, .quality = Q_STANDARD,
                            .integrity = 100, .in_use = 1,
                            .tier = (uint8_t)r->tier, .affix = variant };
        if (!e->in_use) {
            g_player.credits -= price;
            *e = inst;
            player_apply_to_ship();
            toast("FITTED");
        } else if (buy_to_rack(&inst)) {
            /* Slot occupied: the new unit goes to the hold — swap it in
             * from the rack later; no more forced trade-in. */
            g_player.credits -= price;
            toast("TO HOLD");
        } else {
            toast("NO SPACE");
        }
        break;
    }
    case ROW_SALV: {
        WeaponInst *sv = &g_player.salvage[r->index];
        if (sv->type >= EQ_HEATSINK) {
            /* Gadget from the rack: into a free utility bay. */
            int slot2 = -1;
            for (int u = 0; u < player_util_slots(); u++)
                if (!g_player.util_eq[u].in_use) { slot2 = u; break; }
            if (slot2 < 0) { toast("BAYS FULL"); return; }
            g_player.util_eq[slot2] = *sv;
            sv->in_use = 0;
            if (g_player.util_eq[slot2].type == EQ_CHAFF)
                g_player.chaff_charges = 4;
            toast("FITTED");
            return;
        }
        if (sv->type >= WPN_COUNT) {
            /* Equipment from the rack: swap into its slot. */
            int which = sv->type - WPN_COUNT;
            const HullDef *hh = &k_hulls[g_player.hull_id];
            int cap = which ? hh->max_hull_tier : hh->max_shield_tier;
            if (sv->tier > cap) { toast("FRAME LIMIT"); return; }
            WeaponInst *e = equip_slot(which);
            WeaponInst old = *e;
            *e = *sv;
            if (old.in_use) *sv = old; else sv->in_use = 0;
            player_apply_to_ship();
            toast("FITTED");
            return;
        }
        int slot = free_slot_for(sv->type);
        if (slot < 0) { toast("NO FREE SLOT"); return; }
        g_player.mounts[slot] = *sv;
        sv->in_use = 0;
        /* Factory-fresh (100%) racked guns come sealed with a full
         * magazine; battle salvage arrives part-loaded. */
        player_load_mount_ammo(slot,
                               g_player.mounts[slot].integrity >= 100
                                   ? 1.0f : 0.4f);
        player_apply_to_ship();
        toast("FITTED");
        break;
    }
    case ROW_SHOP: {
        /* r->index is an ARMOURY list index, not a weapon type — the
         * pre-armoury code here checked/bought by list position
         * (user-reported: Z1 autocannon refused 'NO FREE SLOT'). */
        const ArmoryItem *it = &s_armory[r->index];
        int price = (int)(it->price * skill_price_mult());
        if (g_player.credits < price) { toast("NO CREDITS"); return; }
        WeaponInst inst = { .type = it->type, .quality = it->quality,
                            .integrity = 100, .in_use = 1,
                            .affix = it->affix };
        int slot = free_slot_for(it->type);
        if (slot >= 0) {
            g_player.credits -= price;
            g_player.mounts[slot] = inst;
            player_load_mount_ammo(slot, 1.0f);   /* sold fully loaded */
            player_apply_to_ship();
            toast("FITTED");
        } else if (buy_to_rack(&inst)) {
            g_player.credits -= price;
            toast("TO HOLD");
        } else {
            toast("NO SPACE");
        }
        break;
    }
    }
}

static void outfit_action_b(int row) {
    if (row >= s_n_rows) return;
    const OutfitRow *r = &s_rows[row];
    switch (r->kind) {
    case ROW_TURRET: {
        if (!g_player.turret_eq.in_use) return;
        int sl = player_free_rack_slot();
        if (sl < 0 || player_cargo_total() >= player_cargo_cap()) {
            toast("NO RACK SPACE");
            return;
        }
        g_player.salvage[sl] = g_player.turret_eq;
        g_player.turret_eq.in_use = 0;
        player_apply_to_ship();
        toast("UNFITTED");
        break;
    }
    case ROW_UTIL: {
        WeaponInst *e = &g_player.util_eq[r->index];
        if (!e->in_use) return;
        int sl = player_free_rack_slot();
        if (sl < 0 || player_cargo_total() >= player_cargo_cap()) {
            toast("NO RACK SPACE");
            return;
        }
        g_player.salvage[sl] = *e;
        e->in_use = 0;
        toast("UNFITTED");
        break;
    }
    case ROW_EQUIP: {
        WeaponInst *e = equip_slot(r->index);
        if (!e->in_use) return;
        int sl = player_free_rack_slot();
        if (sl < 0 || player_cargo_total() >= player_cargo_cap()) {
            toast("NO RACK SPACE");
            return;
        }
        g_player.salvage[sl] = *e;
        e->in_use = 0;
        player_apply_to_ship();
        toast("UNFITTED");
        break;
    }
    case ROW_MOUNT: {
        /* Unmount into the salvage rack. */
        WeaponInst *m = &g_player.mounts[r->index];
        if (!m->in_use) return;
        int sl = player_free_rack_slot();
        if (sl < 0 || player_cargo_total() >= player_cargo_cap()) {
            toast("NO RACK SPACE");
            return;
        }
        g_player.salvage[sl] = *m;
        m->in_use = 0;
        player_apply_to_ship();
        toast("UNMOUNTED");
        break;
    }
    case ROW_SALV: {
        /* Sell from the rack: value scales with quality + integrity. */
        WeaponInst *sv = &g_player.salvage[r->index];
        int v = (int)(instance_price(sv) *
                      (0.35f + 0.30f * sv->integrity * 0.01f));
        g_player.credits += v;
        sv->in_use = 0;
        toast("SOLD");
        break;
    }
    default:
        break;
    }
}

DockAction station_tick(const CraftRawButtons *btn, float dt) {
    DockAction act = DOCK_NONE;
    bool a_edge = btn->a && !s_prev.a;
    bool b_edge = btn->b && !s_prev.b;
    bool lb_edge = btn->lb && !s_prev.lb;
    bool rb_edge = btn->rb && !s_prev.rb;
    /* Hold-to-scroll (user req): edge fires immediately, then repeats
     * after 0.35s at ~8/s. */
    static float s_rep_up, s_rep_dn;
    bool up = false, down = false;
    if (btn->up) {
        if (!s_prev.up) { up = true; s_rep_up = 0; }
        else {
            s_rep_up += dt;
            if (s_rep_up > 0.35f) { s_rep_up -= 0.12f; up = true; }
        }
    } else s_rep_up = 0;
    if (btn->down) {
        if (!s_prev.down) { down = true; s_rep_dn = 0; }
        else {
            s_rep_dn += dt;
            if (s_rep_dn > 0.35f) { s_rep_dn -= 0.12f; down = true; }
        }
    } else s_rep_dn = 0;
    bool back = btn->menu && !s_prev.menu;     /* B stays free for SELL */

    if (s_toast_t > 0) s_toast_t -= dt;
    if (up || down) sfx_ui_move();
    if (a_edge) sfx_ui_select();

    /* Hold-to-repeat for market trading. */
    bool a_rep = false, b_rep = false;
    s_hold_a = btn->a ? s_hold_a + dt : 0;
    s_hold_b = btn->b ? s_hold_b + dt : 0;
    if (s_hold_a > 0.45f || s_hold_b > 0.45f) {
        s_repeat += dt;
        if (s_repeat >= 0.09f) {
            s_repeat = 0;
            if (s_hold_a > 0.45f) a_rep = true;
            if (s_hold_b > 0.45f) b_rep = true;
        }
    }

    switch (s_screen) {
    case SCR_HOME:
        if (up && s_cursor > 0) s_cursor--;
        if (down && s_cursor < HOME_ITEMS - 1) s_cursor++;
        if (a_edge) {
            if (s_cursor == 0) { s_screen = SCR_MARKET; s_cursor = 0; s_scroll = 0; }
            else if (s_cursor == 1) {
                yard_build();
                s_screen = SCR_SHIPYARD; s_cursor = 0; s_scroll = 0;
            }
            else if (s_cursor == 2) {
                armory_build();
                s_screen = SCR_OUTFIT; s_cursor = 1; s_scroll = 0;
            }
            else if (s_cursor == 3) {
                mission_make_offers(system_info(), s_station, s_offers);
                s_screen = SCR_MISSIONS; s_cursor = 0;
            }
            else if (s_cursor == 4) { s_screen = SCR_BAR; }
            else if (s_cursor == 5) { s_screen = SCR_STATUS; status_open(); }
            else if (s_cursor == 6) try_refuel();
            else if (s_cursor == 7) try_rearm();
            else if (s_cursor == 8) {
                if (g_player.fine <= 0) toast("RECORD CLEAN");
                else if (g_player.credits < g_player.fine)
                    toast("NO CREDITS");
                else {
                    g_player.credits -= g_player.fine;
                    g_player.fine = 0;
                    g_player.legal = 0;
                    toast("RECORD CLEARED");
                }
            }
            else if (s_cursor == 9) act = DOCK_LAUNCH;
        }
        if (back) act = DOCK_LAUNCH;           /* MENU = leave */
        break;

    case SCR_SHIPYARD:
        if (s_detail) {
            if (rb_edge) s_cursor = (s_cursor + 1) % YARD_OFFERS;
            if (lb_edge) s_cursor = (s_cursor + YARD_OFFERS - 1) % YARD_OFFERS;
            if (a_edge) { shipyard_buy(s_cursor); s_detail = 0; }
            if (b_edge || back) s_detail = 0;
            break;
        }
        if (up && s_cursor > 0) s_cursor--;
        if (down && s_cursor < YARD_OFFERS - 1) s_cursor++;
        if (b_edge || lb_edge) s_detail = 1;
        if (a_edge) shipyard_buy(s_cursor);
        if (back) { s_screen = SCR_HOME; s_cursor = 1; }
        break;

    case SCR_OUTFIT:
        outfit_build_rows();
        if (s_detail) {
            /* LB/RB loop through every row WITH a sheet, wrapping at
             * the ends (user req) — headers, empty mounts and bare
             * equipment slots are skipped, never exit the view. */
            if (rb_edge || lb_edge) {
                int dir = rb_edge ? 1 : -1;
                int n = s_cursor;
                for (int tries = 0; tries < s_n_rows; tries++) {
                    n = (n + dir + s_n_rows) % s_n_rows;
                    if (row_detailable(&s_rows[n])) { s_cursor = n; break; }
                }
            }
            if (a_edge) { outfit_action_a(s_cursor); s_detail = 0; }
            if (b_edge || back) s_detail = 0;
            break;
        }
        if (up && s_cursor > 0) {
            s_cursor--;
            while (s_cursor > 0 && s_rows[s_cursor].kind == ROW_HDR)
                s_cursor--;
            if (s_rows[s_cursor].kind == ROW_HDR) s_cursor++;
        }
        if (down && s_cursor < s_n_rows - 1) {
            s_cursor++;
            while (s_cursor < s_n_rows - 1 && s_rows[s_cursor].kind == ROW_HDR)
                s_cursor++;
            if (s_rows[s_cursor].kind == ROW_HDR) s_cursor--;
        }
        if (s_cursor < s_scroll) s_scroll = s_cursor;
        if (s_cursor > s_scroll + 8) s_scroll = s_cursor - 8;
        if (lb_edge && row_detailable(&s_rows[s_cursor]))
            s_detail = 1;
        if (a_edge) outfit_action_a(s_cursor);
        if (b_edge) outfit_action_b(s_cursor);
        if (back) { s_screen = SCR_HOME; s_cursor = 2; }
        break;

    case SCR_MISSIONS: {
        if (up && s_cursor > 0) s_cursor--;
        if (down && s_cursor < MISSION_OFFERS - 1) s_cursor++;
        if (a_edge) {
            const Mission *m = &s_offers[s_cursor];
            if (m->type == MIS_NONE) toast("NO OFFER");
            else if (mission_accept(m)) {
                toast("ACCEPTED");
                s_offers[s_cursor].type = MIS_NONE;
            } else toast("LOG/HOLD FULL");
        }
        if (back) { s_screen = SCR_HOME; s_cursor = 3; }
        break;
    }

    case SCR_BAR:
        if (back || a_edge) { s_screen = SCR_HOME; s_cursor = 4; }
        break;

    case SCR_STATUS:
        if (status_tick(btn, dt)) { s_screen = SCR_HOME; s_cursor = 5; }
        s_prev = *btn;
        return act;

    case SCR_MARKET:
        if (up && s_cursor > 0) s_cursor--;
        if (down && s_cursor < N_GOODS - 1) s_cursor++;
        if (s_cursor < s_scroll) s_scroll = s_cursor;
        if (s_cursor > s_scroll + 8) s_scroll = s_cursor - 8;
        if (a_edge || a_rep) try_buy(s_cursor);
        if (b_edge || b_rep) try_sell(s_cursor);
        if (back) { s_screen = SCR_HOME; s_cursor = 0; }
        break;
    }

    s_prev = *btn;
    return act;
}

/* --- drawing ------------------------------------------------------------*/
static void fill(uint16_t *fb, uint16_t c) {
    for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++) fb[i] = c;
}
/* Fill everything EXCEPT the 3D preview pane (right column, body rows) —
 * the scene behind shows the rotating station/ship there. */
static void fill_with_pane(uint16_t *fb, uint16_t c, int body_y0, int body_y1) {
    for (int y = 0; y < ELITE_FB_H; y++) {
        int xmax = (y >= body_y0 && y < body_y1) ? 64 : ELITE_FB_W;
        uint16_t *row = fb + y * ELITE_FB_W;
        for (int x = 0; x < xmax; x++) row[x] = c;
    }
    for (int y = body_y0; y < body_y1; y++)
        fb[y * ELITE_FB_W + 64] = COL_GRID;
}
static void hl(uint16_t *fb, int y, uint16_t c) {
    for (int x = 0; x < ELITE_FB_W; x++) fb[y * ELITE_FB_W + x] = c;
}

static void draw_header(uint16_t *fb) {
    const SystemInfo *si = system_info();
    craft_font_draw(fb, si->stations[s_station].name, 2, 2, COL_HDR);
    char buf[24];
    snprintf(buf, sizeof buf, "%dCR", g_player.credits);
    craft_font_draw(fb, buf, 128 - craft_font_width(buf) - 2, 2, COL_CRED);
    hl(fb, 9, COL_GRID);
}

static void draw_home(uint16_t *fb) {
    draw_header(fb);
    static const char *k_econ[8] = {
        "AGRICULTURE", "INDUSTRIAL", "HIGH TECH", "EXTRACTION",
        "REFINERY", "TOURISM", "MILITARY", "SERVICE",
    };
    const SystemInfo *si = system_info();
    char buf[32];
    snprintf(buf, sizeof buf, "%s T%d", k_econ[si->stations[s_station].econ],
             si->stations[s_station].tech);
    craft_font_draw(fb, buf, 2, 13, COL_DIM);

    for (int i = 0; i < HOME_ITEMS; i++) {
        uint16_t c = (i == s_cursor) ? COL_CUR : COL_DIM;
        if (i == s_cursor) craft_font_draw(fb, ">", 8, 23 + i * 9, COL_CUR);
        craft_font_draw(fb, k_home[i], 16, 23 + i * 9, c);
        /* Live price hints tucked right beside the service rows (the
         * right side belongs to the station render). */
        int hx = 16 + craft_font_width(k_home[i]) + 5;
        if (i == 6) {
            float need = g_player.fuel_max - g_player.fuel;
            if (need >= 0.1f) {
                snprintf(buf, sizeof buf, "%d", (int)(need * 12.0f) + 1);
                craft_font_draw(fb, buf, hx, 23 + i * 9, COL_CRED);
            }
        } else if (i == 7) {
            int rc = player_rearm_cost();
            if (rc > 0) {
                snprintf(buf, sizeof buf, "%d", rc);
                craft_font_draw(fb, buf, hx, 23 + i * 9, COL_CRED);
            }
        } else if (i == 8 && g_player.fine > 0) {
            snprintf(buf, sizeof buf, "%d", g_player.fine);
            craft_font_draw(fb, buf, hx, 23 + i * 9,
                            RGB565C(255, 120, 70));
        }
    }

    /* Fuel + cargo live under the station pane on the right — the
     * 10-row service list (PAY FINE) reclaimed their old left slot. */
    char fuel[24];
    snprintf(fuel, sizeof fuel, "FUEL %d.%d/%dLY",
             (int)g_player.fuel, ((int)(g_player.fuel * 10)) % 10,
             (int)g_player.fuel_max);
    craft_font_draw(fb, fuel, 66, 100, COL_DIM);
    snprintf(fuel, sizeof fuel, "CARGO %d/%d", player_cargo_total(),
             player_cargo_cap());
    craft_font_draw(fb, fuel, 66, 108, COL_DIM);
    hl(fb, 118, COL_GRID);
    craft_font_draw(fb, "A:SELECT MENU:LEAVE MENU", 2, 121, COL_DIM);
}

static void draw_market(uint16_t *fb) {
    draw_header(fb);
    const SystemInfo *si = system_info();
    craft_font_draw(fb, "GOODS", 8, 12, COL_DIM);
    craft_font_draw(fb, "BUY", 56, 12, COL_DIM);
    craft_font_draw(fb, "SELL", 76, 12, COL_DIM);
    craft_font_draw(fb, "ST", 100, 12, COL_DIM);
    craft_font_draw(fb, "HL", 114, 12, COL_DIM);
    hl(fb, 19, COL_GRID);

    int y = 22;
    for (int i = s_scroll; i < N_GOODS && i < s_scroll + 9; i++, y += 10) {
        bool illegal = (k_goods[i].flags & GOOD_ILLEGAL) != 0;
        int buy = econ_price(si, s_station, i, true);
        int sell = econ_price(si, s_station, i, false);
        int stock = econ_stock(si, s_station, i) - s_bought[i];
        uint16_t c = (i == s_cursor) ? COL_CUR : illegal ? COL_ILL : COL_DIM;
        if (i == s_cursor) craft_font_draw(fb, ">", 2, y, COL_CUR);
        craft_font_draw(fb, k_goods[i].name, 8, y, c);
        char buf[12];
        if (buy > 0) {
            /* Value colours: green = well under galactic base (buy
             * here), gold = well over (sell here). The trade matrix,
             * finally visible in the UI. */
            int base = (int)k_goods[i].base;
            /* Good-buy is CYAN: the old green vanished into the green
             * cursor highlight (user report). Cool cyan = bargain in,
             * warm gold = payout out — distinct from each other and
             * from the cursor. */
            uint16_t bc = (buy * 100 < base * 95)
                              ? RGB565C(90, 200, 255) : c;
            uint16_t sc2 = (sell * 100 > base * 108)
                               ? RGB565C(245, 200, 80) : c;
            snprintf(buf, sizeof buf, "%d", buy);
            craft_font_draw(fb, buf, 56, y, bc);
            snprintf(buf, sizeof buf, "%d", sell);
            craft_font_draw(fb, buf, 76, y, sc2);
            snprintf(buf, sizeof buf, "%d", stock > 0 ? stock : 0);
            craft_font_draw(fb, buf, 100, y, c);
        } else {
            craft_font_draw(fb, "--", 56, y, RGB565C(60, 66, 84));
        }
        if (g_player.cargo[i]) {
            snprintf(buf, sizeof buf, "%d", g_player.cargo[i]);
            craft_font_draw(fb, buf, 114, y, COL_CRED);
        }
    }
    hl(fb, 113, COL_GRID);
    char buf[32];
    snprintf(buf, sizeof buf, "HOLD %d/%d", player_cargo_total(),
             player_cargo_cap());
    craft_font_draw(fb, buf, 2, 116, COL_DIM);
    craft_font_draw(fb, "A:BUY B:SELL", 70, 116, COL_DIM);
    craft_font_draw(fb, "MENU:BACK", 2, 123, COL_DIM);
}

static const char *k_qtag[5] = { "SLV", "STD", "RNF", "MIL", "PRO" };

static void draw_shipyard(uint16_t *fb) {
    draw_header(fb);
    craft_font_draw(fb, "SHIPYARD", 2, 12, COL_DIM);
    int y = 24;
    for (int i = 0; i < YARD_OFFERS; i++, y += 11) {
        uint16_t c = (i == s_cursor) ? COL_CUR : COL_DIM;
        if (i == s_cursor) craft_font_draw(fb, ">", 2, y, COL_CUR);
        craft_font_draw(fb, s_yard[i].name, 8, y, c);
    }
    /* Selected offer: price + stat strip in the full-width footer. */
    const HullDef *sel = &k_hulls[s_yard[s_cursor].cls];
    hl(fb, 95, COL_GRID);
    char buf[36];
    {
        int tradein = (k_hulls[g_player.hull_id].price * 7) / 10;
        int cost = sel->price - tradein;
        if (cost < 0) cost = 0;
        snprintf(buf, sizeof buf, "%s COST %dCR", s_yard[s_cursor].name,
                 cost);
    }
    craft_font_draw(fb, buf, 2, 98, COL_CRED);
    /* Label/value colour pairs: "SPD85 CRG6 H70 S50 SL1" */
    {
        char slots[8];
        int sl = 0;
        for (int i = 0; i < sel->n_slots; i++)
            slots[sl++] = (char)('0' + sel->slot_size[i]);
        slots[sl] = 0;
        char vals[5][8];
        snprintf(vals[0], 8, "%d", (int)sel->max_speed);
        snprintf(vals[1], 8, "%d", sel->cargo);
        snprintf(vals[2], 8, "%d", (int)sel->hull_base);
        snprintf(vals[3], 8, "%d", (int)sel->shield_base);
        snprintf(vals[4], 8, "%s", slots);
        static const char *labs[5] = { "SPD", "CRG", "H", "S", "SL" };
        int x = 2;
        for (int i = 0; i < 5; i++) {
            x = craft_font_draw(fb, labs[i], x, 105, RGB565C(80, 95, 120));
            x = craft_font_draw(fb, vals[i], x, 105, RGB565C(140, 255, 140));
            x += 3;
        }
    }
    hl(fb, 113, COL_GRID);
    craft_font_draw(fb, "A:BUY B:DETAILS MENU:BACK", 2, 117, COL_DIM);
}

static void draw_outfit(uint16_t *fb) {
    draw_header(fb);
    craft_font_draw(fb, "OUTFITTING", 2, 12, COL_DIM);
    hl(fb, 19, COL_GRID);
    const HullDef *h = &k_hulls[g_player.hull_id];
    outfit_build_rows();
    if (s_cursor >= s_n_rows) s_cursor = s_n_rows - 1;
    int y = 22;
    char buf[36];
    for (int i = s_scroll; i < s_n_rows && i < s_scroll + 9; i++, y += 10) {
        const OutfitRow *r = &s_rows[i];
        uint16_t c = (i == s_cursor) ? COL_CUR : COL_DIM;
        if (i == s_cursor) craft_font_draw(fb, ">", 2, y, COL_CUR);
        switch (r->kind) {
        case ROW_MOUNT: {
            const WeaponInst *m = &g_player.mounts[r->index];
            if (m->in_use) {
                icon_weapon(fb, 7, y - 1, m->type);
                snprintf(buf, sizeof buf, "Z%d %s%s%s %s %d%%",
                         h->slot_size[r->index], k_weapons[m->type].name,
                         m->affix ? "-" : "",
                         m->affix ? k_affixes[m->affix].tag : "",
                         k_qtag[m->quality], m->integrity);
            } else
                snprintf(buf, sizeof buf, "Z%d EMPTY",
                         h->slot_size[r->index]);
            craft_font_draw(fb, buf, 21, y, c);
            if (m->in_use && m->integrity < 100) {
                snprintf(buf, sizeof buf, "%d", repair_cost(m));
                craft_font_draw(fb, buf, 104, y, COL_CRED);
            }
            break;
        }
        case ROW_EQUIP: {
            const WeaponInst *e = r->index ? &g_player.armor_eq
                                           : &g_player.shield_eq;
            icon_weapon(fb, 7, y - 1, WPN_COUNT + r->index);
            if (e->in_use) {
                const char *vn = (e->type == EQ_ARMOR)
                                     ? k_armor_var_names[e->affix & 3]
                                     : k_shield_var_names[e->affix & 3];
                snprintf(buf, sizeof buf, "%s%s%s Z%d %d%%", vn,
                         e->affix ? " " : "", item_name(e->type),
                         e->tier, e->integrity);
                craft_font_draw(fb, buf, 21, y, c);
                if (e->integrity < 100) {
                    int cost = (int)((100 - e->integrity) *
                                     equip_price(e->type, e->tier,
                                                 e->quality) / 100 * 0.6f *
                                     skill_repair_mult()) + 1;
                    snprintf(buf, sizeof buf, "%d", cost);
                    craft_font_draw(fb, buf, 104, y, COL_CRED);
                }
            } else {
                snprintf(buf, sizeof buf, "%s ----",
                         item_name(WPN_COUNT + r->index));
                craft_font_draw(fb, buf, 21, y, c);
            }
            break;
        }
        case ROW_TURRET: {
            const WeaponInst *t2 = &g_player.turret_eq;
            if (t2->in_use) {
                icon_weapon(fb, 7, y - 1, t2->type);
                snprintf(buf, sizeof buf, "TURRET %s %d%%",
                         k_weapons[t2->type].name, t2->integrity);
            } else
                snprintf(buf, sizeof buf, "TURRET ---- (Z1)");
            craft_font_draw(fb, buf, 21, y, c);
            break;
        }
        case ROW_UTIL: {
            const WeaponInst *e = &g_player.util_eq[r->index];
            if (e->in_use) {
                icon_weapon(fb, 7, y - 1, e->type);
                snprintf(buf, sizeof buf, "%s %d%%",
                         item_name(e->type), e->integrity);
                craft_font_draw(fb, buf, 21, y, c);
            } else {
                snprintf(buf, sizeof buf, "UTIL BAY %d ----", r->index + 1);
                craft_font_draw(fb, buf, 21, y, c);
            }
            break;
        }
        case ROW_UTILSHOP: {
            int ty = EQ_HEATSINK + r->index;
            icon_weapon(fb, 7, y - 1, ty);
            snprintf(buf, sizeof buf, "BUY %s", item_name(ty));
            craft_font_draw(fb, buf, 21, y, c);
            {
                const SystemInfo *sie = system_info();
                snprintf(buf, sizeof buf, "%d",
                         (int)(k_equip[ty - WPN_COUNT].base_price *
                               econ_weapon_mult(
                                   sie->stations[s_station].econ) *
                               skill_price_mult()));
            }
            craft_font_draw(fb, buf, 104, y, COL_CRED);
            break;
        }
        case ROW_EQSHOP: {
            icon_weapon(fb, 7, y - 1, WPN_COUNT + r->index);
            {
                const SystemInfo *sie = system_info();
                uint32_t vh = (uint32_t)(sie->seed >> 22) ^
                              (uint32_t)((s_station + 1) * 7129u) ^
                              (uint32_t)(r->index * 31u + r->tier);
                vh ^= vh >> 13; vh *= 1274126177u; vh ^= vh >> 16;
                uint8_t variant = (uint8_t)(vh % 4u);
                const char *vn = r->index ? k_armor_var_names[variant]
                                          : k_shield_var_names[variant];
                snprintf(buf, sizeof buf, "BUY %s%s%s Z%d", vn,
                         variant ? " " : "",
                         item_name(WPN_COUNT + r->index), r->tier);
            }
            craft_font_draw(fb, buf, 21, y, c);
            {
                const SystemInfo *sie = system_info();
                snprintf(buf, sizeof buf, "%d",
                         (int)(equip_price(WPN_COUNT + r->index, r->tier,
                                           Q_STANDARD) *
                               econ_weapon_mult(
                                   sie->stations[s_station].econ) *
                               skill_price_mult()));
            }
            craft_font_draw(fb, buf, 104, y, COL_CRED);
            break;
        }
        case ROW_HDR: {
            static const char *k_hdr[3] = { "-YOUR SHIP-", "-YOUR HOLD-",
                                            "-STATION SHOP-" };
            craft_font_draw(fb, k_hdr[r->index], 4, y,
                            RGB565C(90, 140, 190));
            break;
        }
        case ROW_SALV: {
            const WeaponInst *m = &g_player.salvage[r->index];
            icon_weapon(fb, 7, y - 1, m->type);
            snprintf(buf, sizeof buf, "%s%s%s %s %d%%",
                     item_name(m->type),
                     m->affix ? "-" : "",
                     m->affix ? k_affixes[m->affix].tag : "",
                     k_qtag[m->quality], m->integrity);
            craft_font_draw(fb, buf, 21, y, c);
            /* What the shop pays (B sells). */
            snprintf(buf, sizeof buf, "+%d",
                     (int)(instance_price(m) *
                           (0.35f + 0.30f * m->integrity * 0.01f)));
            craft_font_draw(fb, buf, 100, y, RGB565C(120, 200, 120));
            break;
        }
        case ROW_SHOP: {
            const ArmoryItem *it = &s_armory[r->index];
            icon_weapon(fb, 7, y - 1, it->type);
            if (it->featured) {
                /* Featured rare: starred, quality-tagged, gold name. */
                snprintf(buf, sizeof buf, "*%s %s%s%s",
                         k_qtag[it->quality], k_weapons[it->type].name,
                         it->affix ? "-" : "",
                         it->affix ? k_affixes[it->affix].tag : "");
                craft_font_draw(fb, buf, 21, y,
                                (i == s_cursor) ? COL_CUR
                                                : RGB565C(255, 200, 90));
            } else {
                snprintf(buf, sizeof buf, "BUY %s Z%d",
                         k_weapons[it->type].name, k_weapons[it->type].size);
                craft_font_draw(fb, buf, 21, y, c);
            }
            snprintf(buf, sizeof buf, "%d",
                     (int)(it->price * skill_price_mult()));
            craft_font_draw(fb, buf, 104, y, COL_CRED);
            break;
        }
        }
    }
    hl(fb, 113, COL_GRID);
    craft_font_draw(fb, "A:ACT B:UNFIT/SELL LB:INFO", 2, 116, COL_DIM);
    craft_font_draw(fb, "MENU:BACK", 2, 123, COL_DIM);
}

static void draw_missions(uint16_t *fb) {
    draw_header(fb);
    craft_font_draw(fb, "MISSIONS", 2, 12, COL_DIM);
    char buf[34];
    Faction fac = system_faction(system_info()->addr);
    snprintf(buf, sizeof buf, "%s REP %d", k_faction_names[fac], g_rep[fac]);
    craft_font_draw(fb, buf, 56, 12, COL_DIM);
    hl(fb, 19, COL_GRID);

    /* Log. */
    int y = 22;
    craft_font_draw(fb, "LOG:", 2, y, COL_HDR);
    y += 8;
    int any = 0;
    for (int i = 0; i < MAX_MISSIONS; i++) {
        const Mission *m = &g_missions[i];
        if (m->type == MIS_NONE) continue;
        any = 1;
        uint16_t c = m->done ? COL_CRED : COL_DIM;
        snprintf(buf, sizeof buf, "%s%s", m->label, m->done ? " DONE" : "");
        craft_font_draw(fb, buf, 6, y, c);
        y += 8;
    }
    if (!any) { craft_font_draw(fb, "(EMPTY)", 6, y, COL_DIM); y += 8; }

    /* Offers. */
    y += 3;
    craft_font_draw(fb, "OFFERS:", 2, y, COL_HDR);
    y += 8;
    for (int i = 0; i < MISSION_OFFERS; i++) {
        const Mission *m = &s_offers[i];
        uint16_t c = (i == s_cursor) ? COL_CUR : COL_DIM;
        if (i == s_cursor) craft_font_draw(fb, ">", 2, y, COL_CUR);
        if (m->type == MIS_NONE) {
            craft_font_draw(fb, "----", 6, y, c);
        } else {
            craft_font_draw(fb, m->label, 6, y, c);
            snprintf(buf, sizeof buf, "%d", m->reward);
            craft_font_draw(fb, buf, 128 - craft_font_width(buf) - 2, y,
                            COL_CRED);
        }
        y += 9;
    }
    hl(fb, 113, COL_GRID);
    craft_font_draw(fb, "A:ACCEPT MENU:BACK", 2, 116, COL_DIM);
    craft_font_draw(fb, "PAY ON RETURN TO ANY DOCK", 2, 123, COL_DIM);
}

static void draw_bar(uint16_t *fb) {
    draw_header(fb);
    craft_font_draw(fb, "THE BAR", 2, 12, COL_DIM);
    hl(fb, 19, COL_GRID);
    const SystemInfo *si = system_info();
    char buf[34];
    int y = 26;
    /* Rumours: seeded flavour + a genuine trade tip. */
    uint32_t h = (uint32_t)(si->seed >> 20) ^ (uint32_t)(s_station * 131);
    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    static const char *k_chatter[6] = {
        "\"PIRATES THICK OUT BY",
        "\"DOMINION PATROLS ARE",
        "\"SAW A DERELICT NEAR",
        "\"FUEL PRICES CLIMBING",
        "\"LOST A WING AT THE",
        "\"KEEP YOUR SCANNER ON",
    };
    craft_font_draw(fb, k_chatter[h % 6u], 2, y, COL_DIM);
    craft_font_draw(fb, " THE BEACON...\"", 2, y + 7, COL_DIM);
    y += 20;
    /* Trade tip: what this economy exports cheap. */
    int best = 0;
    int best_price = 999999;
    for (int g = 0; g < 16; g++) {
        int p = econ_price(si, s_station, g, true);
        if (p > 0 && p * 100 / (k_goods[g].base + 1) < best_price) {
            best_price = p * 100 / (k_goods[g].base + 1);
            best = g;
        }
    }
    snprintf(buf, sizeof buf, "TIP: %s IS CHEAP HERE", k_goods[best].name);
    craft_font_draw(fb, buf, 2, y, COL_TXT);
    y += 12;
    if (econ_has_black_market(si)) {
        craft_font_draw(fb, "BACK ROOM: ILLEGAL GOODS", 2, y, COL_ILL);
        craft_font_draw(fb, "TRADE AT THE MARKET", 2, y + 7, COL_ILL);
    } else {
        craft_font_draw(fb, "(LAWFUL STATION -", 2, y, COL_DIM);
        craft_font_draw(fb, " NO BLACK MARKET)", 2, y + 7, COL_DIM);
    }
    y += 20;
    for (int f = 0; f < N_FACTIONS; f++) {
        snprintf(buf, sizeof buf, "%-10s REP %d", k_faction_names[f],
                 g_rep[f]);
        craft_font_draw(fb, buf, 2, y, COL_DIM);
        y += 8;
    }
    hl(fb, 118, COL_GRID);
    craft_font_draw(fb, "A/MENU:BACK", 2, 121, COL_DIM);
}

void station_draw(uint16_t *fb) {
    /* Status renders over the live hangar-bay scene: route it BEFORE
     * the fill chain below (which was wiping the 3D backdrop). */
    if (s_screen == SCR_STATUS) {
        status_draw(fb);
        return;
    }

    /* Detail sheets replace the list view. */
    if (s_detail && s_screen == SCR_SHIPYARD) {
        int cls = s_yard[s_cursor].cls;
        int tradein = (k_hulls[g_player.hull_id].price * 7) / 10;
        int cost = k_hulls[cls].price - tradein;
        if (cost < 0) cost = 0;
        detail_draw_hull(fb, cls, cost, "LB/RB:NEXT A:BUY B:BACK");
        return;
    }
    if (s_detail && s_screen == SCR_OUTFIT) {
        outfit_build_rows();
        const OutfitRow *r = &s_rows[s_cursor];
        WeaponInst tmp;
        const WeaponInst *wi = NULL;
        const WeaponInst *cmp = NULL;
        int price = -1;
        const char *plabel = "COST";
        const char *foot = "LB/RB:NEXT A:ACT B:BACK";
        if (r->kind == ROW_TURRET && g_player.turret_eq.in_use) {
            wi = &g_player.turret_eq;
            foot = "LB/RB:NEXT B:BACK";
        } else if (r->kind == ROW_UTIL) {
            const WeaponInst *e = &g_player.util_eq[r->index];
            if (e->in_use) {
                wi = e;
                if (e->integrity < 100) {
                    price = (int)((100 - e->integrity) *
                                  instance_price(e) / 100 * 0.6f *
                                  skill_repair_mult()) + 1;
                    plabel = "REPAIR";
                    foot = "LB/RB:NEXT A:RPR B:BACK";
                } else foot = "LB/RB:NEXT B:BACK";
            }
        } else if (r->kind == ROW_UTILSHOP) {
            const SystemInfo *sie = system_info();
            tmp = (WeaponInst){ .type = (uint8_t)(EQ_HEATSINK + r->index),
                                .quality = Q_STANDARD, .integrity = 100,
                                .in_use = 1 };
            wi = &tmp;
            price = (int)(k_equip[EQ_HEATSINK + r->index - WPN_COUNT]
                              .base_price *
                          econ_weapon_mult(sie->stations[s_station].econ) *
                          skill_price_mult());
            foot = "LB/RB:NEXT A:BUY B:BACK";
        } else if (r->kind == ROW_EQUIP) {
            const WeaponInst *e = equip_slot(r->index);
            if (e->in_use) {
                wi = e;
                if (e->integrity < 100) {
                    price = (int)((100 - e->integrity) *
                                  equip_price(e->type, e->tier, e->quality) /
                                  100 * 0.6f * skill_repair_mult()) + 1;
                    plabel = "REPAIR";
                    foot = "LB/RB:NEXT A:RPR B:BACK";
                } else foot = "LB/RB:NEXT B:BACK";
            }
        } else if (r->kind == ROW_EQSHOP) {
            const SystemInfo *sie = system_info();
            tmp = (WeaponInst){ .type = (uint8_t)(WPN_COUNT + r->index),
                                .quality = Q_STANDARD, .integrity = 100,
                                .in_use = 1, .tier = r->tier };
            wi = &tmp;
            price = (int)(equip_price(WPN_COUNT + r->index, r->tier,
                                      Q_STANDARD) *
                          econ_weapon_mult(sie->stations[s_station].econ) *
                          skill_price_mult());
            foot = "LB/RB:NEXT A:BUY B:BACK";
        } else if (r->kind == ROW_MOUNT && g_player.mounts[r->index].in_use) {
            wi = &g_player.mounts[r->index];
            if (wi->integrity < 100) {
                price = repair_cost(wi);
                plabel = "REPAIR";
                foot = "LB/RB:NEXT A:RPR B:BACK";
            } else foot = "B:BACK";
        } else if (r->kind == ROW_SALV) {
            wi = &g_player.salvage[r->index];
            price = (int)(weapon_price(wi->type, wi->quality) *
                          (0.35f + 0.30f * wi->integrity * 0.01f));
            plabel = "SELLS FOR";
            foot = "LB/RB:NEXT A:FIT B:BACK";
        } else if (r->kind == ROW_SHOP) {
            const ArmoryItem *it = &s_armory[r->index];
            tmp = (WeaponInst){ .type = it->type, .quality = it->quality,
                                .integrity = 100, .in_use = 1,
                                .affix = it->affix };
            wi = &tmp;
            price = (int)(it->price * skill_price_mult());
            foot = "LB/RB:NEXT A:BUY B:BACK";
        }
        if (wi) {
            /* Comparator (user spec): the fitted weapon of the SAME
             * type if you have one, else your most expensive fitted
             * weapon. Equipment compares to its fitted counterpart.
             * Viewing an already-fitted item itself: no diff. */
            if (wi->type >= WPN_COUNT) {
                const WeaponInst *e = equip_slot(wi->type - WPN_COUNT);
                if (e->in_use && e != wi) cmp = e;
            } else if (r->kind != ROW_MOUNT) {
                const HullDef *hh = &k_hulls[g_player.hull_id];
                int best_price = -1;
                for (int m = 0; m < hh->n_slots; m++) {
                    const WeaponInst *mw = &g_player.mounts[m];
                    if (!mw->in_use || mw->type >= WPN_COUNT) continue;
                    if (mw->type == wi->type) { cmp = mw; best_price = -2; break; }
                    int pr = weapon_price(mw->type, mw->quality);
                    if (best_price >= -1 && pr > best_price) {
                        best_price = pr;
                        cmp = mw;
                    }
                }
            }
            detail_draw_weapon(fb, wi, cmp, price, plabel, foot);
            return;
        }
        s_detail = 0;
    }

    if (s_screen == SCR_HOME) fill_with_pane(fb, COL_BG, 10, 119);
    else if (s_screen == SCR_SHIPYARD) fill_with_pane(fb, COL_BG, 10, 95);
    else fill(fb, COL_BG);
    if (s_screen == SCR_MARKET) draw_market(fb);
    else if (s_screen == SCR_SHIPYARD) draw_shipyard(fb);
    else if (s_screen == SCR_OUTFIT) draw_outfit(fb);
    else if (s_screen == SCR_MISSIONS) draw_missions(fb);
    else if (s_screen == SCR_BAR) draw_bar(fb);
    else if (s_screen == SCR_STATUS) { status_draw(fb); return; }
    else draw_home(fb);

    if (s_toast_t > 0) {
        int w = craft_font_width(s_toast) + 8;
        int x0 = 64 - w / 2;
        for (int y = 58; y < 70; y++)
            for (int x = x0; x < x0 + w; x++)
                if ((unsigned)x < ELITE_FB_W)
                    fb[y * ELITE_FB_W + x] = RGB565C(30, 20, 12);
        craft_font_draw(fb, s_toast, x0 + 4, 61, COL_WARN);
    }
}
