/*
 * ThumbyElite — detail sheets.
 */
#include "ui_detail.h"
#include "elite_types.h"
#include "elite_ships.h"
#include "elite_weapons.h"
#include "ui_icons.h"
#include "econ.h"
#include "craft_font.h"
#include <stdio.h>

#define COL_BG    RGB565C(  6,  10,  20)
#define COL_HDR   RGB565C(200, 210, 225)
#define COL_GRID  RGB565C( 28,  40,  58)
#define COL_DIM   RGB565C(110, 116, 135)
#define COL_VAL   RGB565C(120, 255, 120)
#define COL_CRED  RGB565C(255, 200,  60)
#define COL_WARN  RGB565C(255, 120,  70)
#define COL_ILL   RGB565C(220, 100, 200)

static void fill(uint16_t *fb, uint16_t c) {
    for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++) fb[i] = c;
}
static void hl(uint16_t *fb, int y, uint16_t c) {
    for (int x = 0; x < ELITE_FB_W; x++) fb[y * ELITE_FB_W + x] = c;
}
static void stat(uint16_t *fb, int y, const char *k, const char *v,
                 uint16_t vc) {
    craft_font_draw(fb, k, 4, y, COL_DIM);
    craft_font_draw(fb, v, 60, y, vc);
}

static const char *k_qual_long[5] = {
    "SALVAGED", "STANDARD", "REINFORCED", "MILITARY", "PROTOTYPE",
};

void detail_draw_weapon(uint16_t *fb, const WeaponInst *wi,
                        int price, const char *price_label,
                        const char *footer) {
    fill(fb, COL_BG);
    char buf[28];

    if (wi->type >= WPN_COUNT) {
        /* Equipment sheet: protection rather than firepower. */
        icon_weapon_2x(fb, 4, 3, wi->type);
        craft_font_draw(fb, item_name(wi->type), 32, 4, COL_HDR);
        craft_font_draw(fb, k_qual_long[wi->quality > 4 ? 4 : wi->quality],
                        32, 11,
                        (wi->quality >= Q_MILITARY) ? COL_CRED : COL_DIM);
        hl(fb, 19, COL_GRID);
        int y = 24;
        snprintf(buf, sizeof buf, "Z%d", wi->tier);
        stat(fb, y, "SIZE", buf, COL_VAL); y += 8;
        float mult = k_tier_mult[wi->tier > 3 ? 3 : wi->tier] *
                     quality_dmg_mult(wi->quality) *
                     (0.6f + 0.4f * wi->integrity * 0.01f);
        snprintf(buf, sizeof buf, "X%d.%d", (int)mult,
                 ((int)(mult * 10)) % 10);
        stat(fb, y, "PROTECTION", buf, COL_VAL); y += 8;
        snprintf(buf, sizeof buf, "%d%%", wi->integrity);
        stat(fb, y, "INTEGRITY", buf,
             wi->integrity < 60 ? COL_WARN : COL_VAL); y += 8;
        if (price >= 0) {
            hl(fb, y + 1, COL_GRID);
            snprintf(buf, sizeof buf, "%s %dCR", price_label, price);
            craft_font_draw(fb, buf, 4, y + 5, COL_CRED);
        }
        hl(fb, 118, COL_GRID);
        craft_font_draw(fb, footer, 2, 121, COL_DIM);
        return;
    }

    const WeaponDef *w = &k_weapons[wi->type];

    icon_weapon_2x(fb, 4, 3, wi->type);
    craft_font_draw(fb, w->name, 32, 4, COL_HDR);
    craft_font_draw(fb, k_qual_long[wi->quality > 4 ? 4 : wi->quality],
                    32, 11, (wi->quality >= Q_MILITARY) ? COL_CRED : COL_DIM);
    hl(fb, 19, COL_GRID);

    float dm = mount_dmg_mult(wi);
    int y = 24;
    snprintf(buf, sizeof buf, "Z%d", w->size);
    stat(fb, y, "SLOT SIZE", buf, COL_VAL); y += 8;
    snprintf(buf, sizeof buf, "%d.%d", (int)(w->dmg * dm),
             ((int)(w->dmg * dm * 10)) % 10);
    stat(fb, y, "DAMAGE", buf, COL_VAL); y += 8;
    float dps = w->dmg * dm / w->cooldown;
    snprintf(buf, sizeof buf, "%d.%d", (int)dps, ((int)(dps * 10)) % 10);
    stat(fb, y, "DPS", buf, COL_VAL); y += 8;
    snprintf(buf, sizeof buf, "%d.%d/S", (int)(w->heat / w->cooldown),
             ((int)(w->heat / w->cooldown * 10)) % 10);
    stat(fb, y, "HEAT", buf,
         (w->heat / w->cooldown > 30) ? COL_WARN : COL_VAL); y += 8;
    if (w->speed > 0)
        snprintf(buf, sizeof buf, "%dM/S", (int)w->speed);
    else
        snprintf(buf, sizeof buf, "HITSCAN");
    stat(fb, y, "VELOCITY", buf, COL_VAL); y += 8;
    snprintf(buf, sizeof buf, "%dM", (int)w->range);
    stat(fb, y, "RANGE", buf, COL_VAL); y += 8;
    if (w->ammo_max)
        snprintf(buf, sizeof buf, "%d RNDS", w->ammo_max);
    else
        snprintf(buf, sizeof buf, "ENERGY");
    stat(fb, y, "AMMO", buf, COL_VAL); y += 8;
    if (w->aoe > 0) {
        snprintf(buf, sizeof buf, "%dM BLAST", (int)w->aoe);
        stat(fb, y, "WARHEAD", buf, COL_WARN); y += 8;
    }
    if (w->turn > 0) {
        stat(fb, y, "GUIDANCE", "SEEKER", COL_ILL); y += 8;
    }
    snprintf(buf, sizeof buf, "%d%%", wi->integrity);
    stat(fb, y, "INTEGRITY", buf,
         wi->integrity < 60 ? COL_WARN : COL_VAL); y += 8;

    if (price >= 0) {
        hl(fb, y + 1, COL_GRID);
        snprintf(buf, sizeof buf, "%s %dCR", price_label, price);
        craft_font_draw(fb, buf, 4, y + 5, COL_CRED);
    }

    hl(fb, 118, COL_GRID);
    craft_font_draw(fb, footer, 2, 121, COL_DIM);
}

void detail_draw_hull(uint16_t *fb, int hull_id, int cost,
                      const char *footer) {
    /* Left column only — the shipyard's rotating 3D pane stays live. */
    for (int y = 0; y < ELITE_FB_H; y++) {
        int xmax = (y >= 10 && y < 95) ? 64 : ELITE_FB_W;
        uint16_t *row = fb + y * ELITE_FB_W;
        for (int x = 0; x < xmax; x++) row[x] = COL_BG;
    }
    for (int y = 10; y < 95; y++) fb[y * ELITE_FB_W + 64] = COL_GRID;

    const HullDef *h = &k_hulls[hull_id];
    char buf[28];
    craft_font_draw(fb, h->name, 2, 2, COL_HDR);
    hl(fb, 9, COL_GRID);

    int y = 13;
    #define HSTAT(label, fmt, ...) do { \
        craft_font_draw(fb, label, 2, y, COL_DIM); \
        snprintf(buf, sizeof buf, fmt, __VA_ARGS__); \
        craft_font_draw(fb, buf, 34, y, COL_VAL); \
        y += 8; \
    } while (0)
    HSTAT("SPD", "%d", (int)h->max_speed);
    HSTAT("ACC", "%d", (int)h->accel);
    HSTAT("TRN", "%d.%d", (int)h->turn_rate,
          ((int)(h->turn_rate * 10)) % 10);
    HSTAT("CRG", "%dT", h->cargo);
    HSTAT("HUL", "%d", (int)h->hull_base);
    HSTAT("SHD", "%d", (int)h->shield_base);
    HSTAT("TIER", "S%d H%d", h->max_shield_tier, h->max_hull_tier);
    {
        char slots[12];
        int sl = 0;
        for (int i = 0; i < h->n_slots; i++) {
            slots[sl++] = 'Z';
            slots[sl++] = (char)('0' + h->slot_size[i]);
            slots[sl++] = ' ';
        }
        slots[sl] = 0;
        HSTAT("GUNS", "%s", slots);
    }
    #undef HSTAT

    hl(fb, 95, COL_GRID);
    if (cost < 0)
        craft_font_draw(fb, "OWNED", 2, 99, COL_CRED);
    else {
        snprintf(buf, sizeof buf, "COST %dCR (TRADE-IN)", cost);
        craft_font_draw(fb, buf, 2, 99, COL_CRED);
    }
    snprintf(buf, sizeof buf, "LIST %dCR", h->price);
    craft_font_draw(fb, buf, 2, 107, COL_DIM);
    hl(fb, 118, COL_GRID);
    craft_font_draw(fb, footer, 2, 121, COL_DIM);
}

void detail_draw_good(uint16_t *fb, int good, int held,
                      const char *footer) {
    fill(fb, COL_BG);
    const GoodDef *g = &k_goods[good];
    char buf[28];
    craft_font_draw(fb, g->name, 4, 4, COL_HDR);
    if (g->flags & GOOD_ILLEGAL)
        craft_font_draw(fb, "ILLEGAL", 90, 4, COL_ILL);
    hl(fb, 12, COL_GRID);
    int y = 18;
    snprintf(buf, sizeof buf, "%d UNITS HELD", held);
    craft_font_draw(fb, buf, 4, y, COL_VAL); y += 9;
    snprintf(buf, sizeof buf, "GALACTIC AVG %dCR", g->base);
    craft_font_draw(fb, buf, 4, y, COL_DIM); y += 9;
    if (g->flags & GOOD_ILLEGAL) {
        craft_font_draw(fb, "ONLY BLACK MARKETS", 4, y, COL_ILL); y += 7;
        craft_font_draw(fb, "WILL TOUCH THIS", 4, y, COL_ILL); y += 9;
    } else {
        craft_font_draw(fb, "SELL WHERE THE", 4, y, COL_DIM); y += 7;
        craft_font_draw(fb, "ECONOMY IMPORTS IT", 4, y, COL_DIM); y += 9;
    }
    hl(fb, 118, COL_GRID);
    craft_font_draw(fb, footer, 2, 121, COL_DIM);
}
