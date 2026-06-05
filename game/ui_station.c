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
#include "elite_entity.h"
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
    SCR_HOME = 0, SCR_MARKET, SCR_SHIPYARD, SCR_OUTFIT, SCR_STATUS
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

#define HOME_ITEMS 6
static const char *k_home[HOME_ITEMS] = {
    "MARKET", "SHIPYARD", "OUTFITTING", "STATUS", "REFUEL", "LAUNCH",
};
static const bool k_home_enabled[HOME_ITEMS] = {
    true, true, true, true, true, true,
};

void station_open(int station_idx) {
    s_screen = SCR_HOME;
    s_station = station_idx;
    s_cursor = 0;
    s_scroll = 0;
    memset(s_bought, 0, sizeof s_bought);
    /* Debounce: everything counts as held until released once. */
    memset(&s_prev, 0xFF, sizeof s_prev);
    s_hold_a = s_hold_b = s_repeat = 0;
    s_toast[0] = 0;
    s_toast_t = 0;
}

static void toast(const char *msg) {
    snprintf(s_toast, sizeof s_toast, "%s", msg);
    s_toast_t = 1.6f;
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

/* --- shipyard ----------------------------------------------------------*/
static void shipyard_buy(int hull_id) {
    if (hull_id == g_player.hull_id) { toast("CURRENT SHIP"); return; }
    const HullDef *h = &k_hulls[hull_id];
    int tradein = (k_hulls[g_player.hull_id].price * 7) / 10;
    int cost = h->price - tradein;
    if (cost < 0) cost = 0;
    if (g_player.credits < cost) { toast("NO CREDITS"); return; }
    /* Cargo must fit the new hold. */
    if (player_cargo_total() > h->cargo) { toast("CARGO WONT FIT"); return; }
    g_player.credits -= cost;
    g_player.hull_id = (uint8_t)hull_id;
    /* Tiers clamp to what the frame can take. */
    if (g_player.shield_tier > h->max_shield_tier)
        g_player.shield_tier = h->max_shield_tier;
    if (g_player.hull_tier > h->max_hull_tier)
        g_player.hull_tier = h->max_hull_tier;
    /* Mounts that don't fit (count or size) drop to salvage, else sell. */
    for (int i = 0; i < HULL_SLOTS; i++) {
        WeaponInst *m = &g_player.mounts[i];
        if (!m->in_use) continue;
        bool fits = i < h->n_slots &&
                    k_weapons[m->type].size <= h->slot_size[i];
        if (fits) continue;
        int sl = -1;
        for (int t = 0; t < MAX_SALVAGE; t++)
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
typedef enum { ROW_MOUNT, ROW_SHIELD_UP, ROW_HULL_UP, ROW_SALV, ROW_SHOP } RowKind;
typedef struct { uint8_t kind, index; } OutfitRow;
static OutfitRow s_rows[HULL_SLOTS + 2 + MAX_SALVAGE + WPN_COUNT];
static int s_n_rows;

static void outfit_build_rows(void) {
    const HullDef *h = &k_hulls[g_player.hull_id];
    s_n_rows = 0;
    for (int i = 0; i < h->n_slots; i++)
        s_rows[s_n_rows++] = (OutfitRow){ ROW_MOUNT, (uint8_t)i };
    s_rows[s_n_rows++] = (OutfitRow){ ROW_SHIELD_UP, 0 };
    s_rows[s_n_rows++] = (OutfitRow){ ROW_HULL_UP, 0 };
    for (int i = 0; i < MAX_SALVAGE; i++)
        if (g_player.salvage[i].in_use)
            s_rows[s_n_rows++] = (OutfitRow){ ROW_SALV, (uint8_t)i };
    for (int i = 0; i < WPN_COUNT; i++)
        s_rows[s_n_rows++] = (OutfitRow){ ROW_SHOP, (uint8_t)i };
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
    case ROW_SHIELD_UP: {
        if (g_player.shield_tier >= h->max_shield_tier) { toast("FRAME LIMIT"); return; }
        int cost = upgrade_price(g_player.hull_id, g_player.shield_tier + 1);
        if (g_player.credits < cost) { toast("NO CREDITS"); return; }
        g_player.credits -= cost;
        g_player.shield_tier++;
        player_apply_to_ship();
        toast("SHIELD UP");
        break;
    }
    case ROW_HULL_UP: {
        if (g_player.hull_tier >= h->max_hull_tier) { toast("FRAME LIMIT"); return; }
        int cost = upgrade_price(g_player.hull_id, g_player.hull_tier + 1);
        if (g_player.credits < cost) { toast("NO CREDITS"); return; }
        g_player.credits -= cost;
        g_player.hull_tier++;
        player_apply_to_ship();
        toast("HULL UP");
        break;
    }
    case ROW_SALV: {
        WeaponInst *sv = &g_player.salvage[r->index];
        int slot = free_slot_for(sv->type);
        if (slot < 0) { toast("NO FREE SLOT"); return; }
        g_player.mounts[slot] = *sv;
        sv->in_use = 0;
        player_apply_to_ship();
        toast("FITTED");
        break;
    }
    case ROW_SHOP: {
        int price = (int)(weapon_price(r->index, Q_STANDARD) *
                          skill_price_mult());
        int slot = free_slot_for(r->index);
        if (slot < 0) { toast("NO FREE SLOT"); return; }
        if (g_player.credits < price) { toast("NO CREDITS"); return; }
        g_player.credits -= price;
        g_player.mounts[slot] =
            (WeaponInst){ (uint8_t)r->index, Q_STANDARD, 100, 1 };
        player_apply_to_ship();
        toast("FITTED");
        break;
    }
    }
}

static void outfit_action_b(int row) {
    if (row >= s_n_rows) return;
    const OutfitRow *r = &s_rows[row];
    switch (r->kind) {
    case ROW_MOUNT: {
        /* Unmount into the salvage rack. */
        WeaponInst *m = &g_player.mounts[r->index];
        if (!m->in_use) return;
        int sl = -1;
        for (int t = 0; t < MAX_SALVAGE; t++)
            if (!g_player.salvage[t].in_use) { sl = t; break; }
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
        int v = (int)(weapon_price(sv->type, sv->quality) *
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
    bool up = btn->up && !s_prev.up;
    bool down = btn->down && !s_prev.down;
    bool back = btn->menu && !s_prev.menu;     /* B stays free for SELL */

    if (s_toast_t > 0) s_toast_t -= dt;

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
            else if (s_cursor == 1) { s_screen = SCR_SHIPYARD; s_cursor = 0; s_scroll = 0; }
            else if (s_cursor == 2) { s_screen = SCR_OUTFIT; s_cursor = 0; s_scroll = 0; }
            else if (s_cursor == 3) { s_screen = SCR_STATUS; status_open(); }
            else if (s_cursor == 4) try_refuel();
            else if (s_cursor == 5) act = DOCK_LAUNCH;
        }
        if (back) act = DOCK_LAUNCH;           /* MENU = leave */
        break;

    case SCR_SHIPYARD:
        if (up && s_cursor > 0) s_cursor--;
        if (down && s_cursor < N_HULLS - 1) s_cursor++;
        if (s_cursor < s_scroll) s_scroll = s_cursor;
        if (s_cursor > s_scroll + 6) s_scroll = s_cursor - 6;
        if (a_edge) shipyard_buy(s_cursor);
        if (back) { s_screen = SCR_HOME; s_cursor = 1; }
        break;

    case SCR_OUTFIT:
        outfit_build_rows();
        if (up && s_cursor > 0) s_cursor--;
        if (down && s_cursor < s_n_rows - 1) s_cursor++;
        if (s_cursor < s_scroll) s_scroll = s_cursor;
        if (s_cursor > s_scroll + 8) s_scroll = s_cursor - 8;
        if (a_edge) outfit_action_a(s_cursor);
        if (b_edge) outfit_action_b(s_cursor);
        if (back) { s_screen = SCR_HOME; s_cursor = 2; }
        break;

    case SCR_STATUS:
        if (status_tick(btn, dt)) { s_screen = SCR_HOME; s_cursor = 3; }
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
        uint16_t c = !k_home_enabled[i] ? RGB565C(60, 66, 84)
                   : (i == s_cursor) ? COL_CUR : COL_DIM;
        if (i == s_cursor) craft_font_draw(fb, ">", 8, 28 + i * 11, COL_CUR);
        craft_font_draw(fb, k_home[i], 16, 28 + i * 11, c);
        if (!k_home_enabled[i])
            craft_font_draw(fb, "SOON", 90, 28 + i * 11, RGB565C(60, 66, 84));
    }

    char fuel[24];
    snprintf(fuel, sizeof fuel, "FUEL %d.%d/%d LY",
             (int)g_player.fuel, ((int)(g_player.fuel * 10)) % 10,
             (int)g_player.fuel_max);
    craft_font_draw(fb, fuel, 2, 103, COL_DIM);
    snprintf(fuel, sizeof fuel, "CARGO %d/%d", player_cargo_total(),
             player_cargo_cap());
    craft_font_draw(fb, fuel, 2, 110, COL_DIM);
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
            snprintf(buf, sizeof buf, "%d", buy);
            craft_font_draw(fb, buf, 56, y, c);
            snprintf(buf, sizeof buf, "%d", sell);
            craft_font_draw(fb, buf, 76, y, c);
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
    hl(fb, 19, COL_GRID);
    int y = 22;
    for (int i = s_scroll; i < N_HULLS && i < s_scroll + 7; i++, y += 10) {
        const HullDef *h = &k_hulls[i];
        uint16_t c = (i == s_cursor) ? COL_CUR
                   : (i == g_player.hull_id) ? COL_CRED : COL_DIM;
        if (i == s_cursor) craft_font_draw(fb, ">", 2, y, COL_CUR);
        craft_font_draw(fb, h->name, 8, y, c);
        char buf[16];
        if (i == g_player.hull_id)
            craft_font_draw(fb, "OWNED", 92, y, COL_CRED);
        else {
            int tradein = (k_hulls[g_player.hull_id].price * 7) / 10;
            int cost = h->price - tradein;
            if (cost < 0) cost = 0;
            snprintf(buf, sizeof buf, "%d", cost);
            craft_font_draw(fb, buf, 92, y, c);
        }
    }
    /* Selected hull stat strip. */
    const HullDef *sel = &k_hulls[s_cursor];
    hl(fb, 96, COL_GRID);
    char buf[36];
    snprintf(buf, sizeof buf, "SPD%d CRG%d HUL%d SHD%d",
             (int)sel->max_speed, sel->cargo, (int)sel->hull_base,
             (int)sel->shield_base);
    craft_font_draw(fb, buf, 2, 99, COL_DIM);
    char slots[20] = "SLOTS ";
    int sl = 6;
    for (int i = 0; i < sel->n_slots; i++) {
        slots[sl++] = (char)('0' + sel->slot_size[i]);
        slots[sl++] = ' ';
    }
    slots[sl] = 0;
    snprintf(buf, sizeof buf, "%s STIER%d HTIER%d", slots,
             sel->max_shield_tier, sel->max_hull_tier);
    craft_font_draw(fb, buf, 2, 106, COL_DIM);
    hl(fb, 115, COL_GRID);
    craft_font_draw(fb, "A:BUY(TRADE-IN) MENU:BACK", 2, 118, COL_DIM);
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
            if (m->in_use)
                snprintf(buf, sizeof buf, "S%d %-8s%s %d%%",
                         h->slot_size[r->index], k_weapons[m->type].name,
                         k_qtag[m->quality], m->integrity);
            else
                snprintf(buf, sizeof buf, "S%d ----",
                         h->slot_size[r->index]);
            craft_font_draw(fb, buf, 8, y, c);
            if (m->in_use && m->integrity < 100) {
                snprintf(buf, sizeof buf, "%d", repair_cost(m));
                craft_font_draw(fb, buf, 104, y, COL_CRED);
            }
            break;
        }
        case ROW_SHIELD_UP:
        case ROW_HULL_UP: {
            int is_sh = r->kind == ROW_SHIELD_UP;
            int tier = is_sh ? g_player.shield_tier : g_player.hull_tier;
            int max = is_sh ? h->max_shield_tier : h->max_hull_tier;
            if (tier >= max)
                snprintf(buf, sizeof buf, "%s T%d MAX",
                         is_sh ? "SHIELD" : "ARMOR", tier);
            else
                snprintf(buf, sizeof buf, "%s T%d>%d",
                         is_sh ? "SHIELD" : "ARMOR", tier, tier + 1);
            craft_font_draw(fb, buf, 8, y, c);
            if (tier < max) {
                snprintf(buf, sizeof buf, "%d",
                         upgrade_price(g_player.hull_id, tier + 1));
                craft_font_draw(fb, buf, 104, y, COL_CRED);
            }
            break;
        }
        case ROW_SALV: {
            const WeaponInst *m = &g_player.salvage[r->index];
            snprintf(buf, sizeof buf, "RK %-8s%s %d%%",
                     k_weapons[m->type].name, k_qtag[m->quality],
                     m->integrity);
            craft_font_draw(fb, buf, 8, y, c);
            break;
        }
        case ROW_SHOP: {
            snprintf(buf, sizeof buf, "BUY %-8sZ%d",
                     k_weapons[r->index].name, k_weapons[r->index].size);
            craft_font_draw(fb, buf, 8, y, c);
            snprintf(buf, sizeof buf, "%d",
                     (int)(weapon_price(r->index, Q_STANDARD) *
                           skill_price_mult()));
            craft_font_draw(fb, buf, 104, y, COL_CRED);
            break;
        }
        }
    }
    hl(fb, 113, COL_GRID);
    craft_font_draw(fb, "A:FIT/RPR/BUY B:UNFIT/SELL", 2, 116, COL_DIM);
    craft_font_draw(fb, "MENU:BACK", 2, 123, COL_DIM);
}

void station_draw(uint16_t *fb) {
    fill(fb, COL_BG);
    if (s_screen == SCR_MARKET) draw_market(fb);
    else if (s_screen == SCR_SHIPYARD) draw_shipyard(fb);
    else if (s_screen == SCR_OUTFIT) draw_outfit(fb);
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
