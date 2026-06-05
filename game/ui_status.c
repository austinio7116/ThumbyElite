/*
 * ThumbyElite — ship status sheet.
 */
#include "ui_status.h"
#include "elite_types.h"
#include "elite_player.h"
#include "elite_ships.h"
#include "elite_weapons.h"
#include "econ.h"
#include "craft_font.h"
#include "ui_icons.h"
#include <stdio.h>
#include <string.h>

#define COL_BG    RGB565C(  6,  10,  20)
#define COL_HDR   RGB565C(200, 210, 225)
#define COL_GRID  RGB565C( 28,  40,  58)
#define COL_DIM   RGB565C(110, 116, 135)
#define COL_TXT   RGB565C(120, 255, 120)
#define COL_CRED  RGB565C(255, 200,  60)
#define COL_WARN  RGB565C(255, 120,  70)

static int s_scroll;
static CraftRawButtons s_prev;

/* All-held button state for debouncing. NOTE: never memset(0xFF) over
 * _Bool fields — !x compiles as x^1, and 0xFE is still truthy. */
static CraftRawButtons buttons_all_held(void) {
    CraftRawButtons b;
    b.up = b.down = b.left = b.right = true;
    b.a = b.b = b.lb = b.rb = b.menu = true;
    return b;
}

void status_open(void) {
    s_scroll = 0;
    s_prev = buttons_all_held();   /* debounce */
}

bool status_tick(const CraftRawButtons *btn, float dt) {
    (void)dt;
    bool up = btn->up && !s_prev.up;
    bool down = btn->down && !s_prev.down;
    bool close = (btn->menu && !s_prev.menu) || (btn->b && !s_prev.b) ||
                 (btn->a && !s_prev.a);
    if (up && s_scroll > 0) s_scroll--;
    if (down && s_scroll < 10) s_scroll++;
    s_prev = *btn;
    return close;
}

static const char *k_qual_tag[5] = { "SLV", "STD", "RNF", "MIL", "PRO" };
static const char *k_skill_names[4] = {
    "GUNNERY", "TRADING", "TECH", "PILOTING",
};

void status_draw(uint16_t *fb) {
    /* Fill everything except the ship-preview window (top right). */
    for (int y = 0; y < ELITE_FB_H; y++) {
        uint16_t *row = fb + y * ELITE_FB_W;
        int skip0 = (y >= 10 && y < 54) ? 76 : ELITE_FB_W;
        for (int x = 0; x < ELITE_FB_W; x++)
            if (x < skip0) row[x] = COL_BG;
    }
    for (int y = 10; y < 54; y++) fb[y * ELITE_FB_W + 76] = COL_GRID;
    const HullDef *h = &k_hulls[g_player.hull_id];
    char buf[36];

    craft_font_draw(fb, "SHIP STATUS", 2, 2, COL_HDR);
    snprintf(buf, sizeof buf, "%dCR", g_player.credits);
    craft_font_draw(fb, buf, 128 - craft_font_width(buf) - 2, 2, COL_CRED);
    for (int x = 0; x < 128; x++) fb[9 * ELITE_FB_W + x] = COL_GRID;

    int y = 13 - s_scroll * 8;
    #define ROW(...) do { \
        if (y >= 11 && y < 120) { \
            snprintf(buf, sizeof buf, __VA_ARGS__); \
            craft_font_draw(fb, buf, 2, y, c); \
        } \
        y += 8; \
    } while (0)

    uint16_t c = COL_HDR;
    ROW("%s  S%d H%d", h->name, g_player.shield_tier, g_player.hull_tier);
    c = COL_DIM;
    ROW("SPD %d CRG %d", (int)h->max_speed, h->cargo);

    c = COL_HDR;
    ROW("MOUNTS:");
    for (int i = 0; i < h->n_slots; i++) {
        const WeaponInst *m = &g_player.mounts[i];
        if (m->in_use) {
            c = (m->integrity < 50) ? COL_WARN : COL_DIM;
            int yy = y;
            ROW("   S%d %s %d%%", h->slot_size[i],
                k_weapons[m->type].name, m->integrity);
            if (yy >= 11 && yy < 120) icon_weapon(fb, 2, yy - 1, m->type);
        } else {
            c = COL_DIM;
            ROW("S%d ----", h->slot_size[i]);
        }
    }

    c = COL_HDR;
    ROW("RACK:");
    int any = 0;
    for (int i = 0; i < MAX_SALVAGE; i++) {
        const WeaponInst *m = &g_player.salvage[i];
        if (!m->in_use) continue;
        any = 1;
        c = COL_DIM;
        int yy = y;
        ROW("   %s %s %d%%", k_weapons[m->type].name,
            k_qual_tag[m->quality], m->integrity);
        if (yy >= 11 && yy < 120) icon_weapon(fb, 2, yy - 1, m->type);
    }
    if (!any) { c = COL_DIM; ROW("(EMPTY)"); }

    c = COL_HDR;
    ROW("CARGO %d/%d:", player_cargo_total(), player_cargo_cap());
    any = 0;
    for (int i = 0; i < N_GOODS; i++) {
        if (!g_player.cargo[i]) continue;
        any = 1;
        c = COL_DIM;
        ROW("%dX %s", g_player.cargo[i], k_goods[i].name);
    }
    if (!any) { c = COL_DIM; ROW("(EMPTY)"); }

    c = COL_HDR;
    ROW("SKILLS:");
    uint16_t xs[4] = { g_player.xp_gunnery, g_player.xp_trading,
                       g_player.xp_tech, g_player.xp_piloting };
    for (int i = 0; i < 4; i++) {
        c = COL_TXT;
        ROW("%-8s LV%d (%d)", k_skill_names[i], skill_level(xs[i]), xs[i]);
    }
    c = COL_DIM;
    ROW("FUEL %d.%d/%d LY", (int)g_player.fuel,
        ((int)(g_player.fuel * 10)) % 10, (int)g_player.fuel_max);
    #undef ROW

    for (int x = 0; x < 128; x++) fb[120 * ELITE_FB_W + x] = COL_GRID;
    craft_font_draw(fb, "U/D:SCROLL A:CLOSE", 2, 122, COL_DIM);
}
