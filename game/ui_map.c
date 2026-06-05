/*
 * ThumbyElite — galaxy + system map screens.
 *
 * Galaxy map: 2D chart (4 px/ly), d-pad pans a cursor that snaps its
 * highlight to the nearest star; jump-range ring around the current
 * system; A engages hyperspace on an in-range highlight.
 *
 * System map: schematic orbit strip — star at the left, planets spaced
 * by orbit with their stations; up/down cycles POIs (beacon first),
 * A engages supercruise.
 */
#include "ui_map.h"
#include "elite_types.h"
#include "craft_font.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define COL_BG      RGB565C(  4,   8,  18)
#define COL_GRID    RGB565C( 18,  28,  46)
#define COL_STAR    RGB565C(210, 215, 230)
#define COL_DIM     RGB565C(120, 126, 145)
#define COL_CUR     RGB565C(120, 255, 120)
#define COL_SELF    RGB565C( 80, 180, 255)
#define COL_RING    RGB565C( 50,  90,  70)
#define COL_TXT     RGB565C(120, 255, 120)
#define COL_WARN    RGB565C(255, 120,  70)
#define COL_TITLE   RGB565C(200, 210, 225)

static inline void px(uint16_t *fb, int x, int y, uint16_t c) {
    if ((unsigned)x < ELITE_FB_W && (unsigned)y < ELITE_FB_H)
        fb[y * ELITE_FB_W + x] = c;
}
static void fill(uint16_t *fb, uint16_t c) {
    for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++) fb[i] = c;
}

/* --- shared edge detection --------------------------------------------*/
typedef struct { CraftRawButtons prev; } Edges;
static Edges s_edge;
/* Debounce: treat every button as already-held on open, so whatever
 * press navigated INTO the screen must be released before it can act.
 * NOTE: never memset(0xFF) over _Bool — !x compiles as x^1 and 0xFE
 * stays truthy; assign true per field. */
static void edges_reset(void) {
    CraftRawButtons b;
    b.up = b.down = b.left = b.right = true;
    b.a = b.b = b.lb = b.rb = b.menu = true;
    s_edge.prev = b;
}
#define JUST(btn, field) ((btn)->field && !s_edge.prev.field)

/* ====================== GALAXY MAP ===================================== */
#define GMAP_SCALE 4.0f          /* px per ly */

static SysAddr s_cur_sys;
static float s_fuel, s_range;
static float s_cx_ly, s_cy_ly;   /* cursor (and view centre) */
static SysAddr s_hl;             /* highlighted star */
static bool s_hl_valid;
static float s_hl_dist;

void map_galaxy_open(SysAddr current, float fuel_ly, float range_ly) {
    s_cur_sys = current;
    s_fuel = fuel_ly;
    s_range = range_ly;
    galaxy_star_pos(current, &s_cx_ly, &s_cy_ly);
    s_hl = current;
    s_hl_valid = true;
    s_hl_dist = 0;
    edges_reset();
}

static void gmap_highlight(void) {
    /* Nearest star to the cursor within 2.5 ly, scanning nearby sectors. */
    int sx0 = (int)floorf((s_cx_ly - 4) / SECTOR_LY);
    int sx1 = (int)floorf((s_cx_ly + 4) / SECTOR_LY);
    int sy0 = (int)floorf((s_cy_ly - 4) / SECTOR_LY);
    int sy1 = (int)floorf((s_cy_ly + 4) / SECTOR_LY);
    float best = 2.5f * 2.5f;
    s_hl_valid = false;
    for (int sy = sy0; sy <= sy1; sy++)
        for (int sx = sx0; sx <= sx1; sx++) {
            int n = galaxy_sector_stars(sx, sy);
            for (int i = 0; i < n; i++) {
                SysAddr a = { sx, sy, (uint8_t)i };
                float x, y;
                galaxy_star_pos(a, &x, &y);
                float d2 = (x - s_cx_ly) * (x - s_cx_ly) +
                           (y - s_cy_ly) * (y - s_cy_ly);
                if (d2 < best) {
                    best = d2;
                    s_hl = a;
                    s_hl_valid = true;
                }
            }
        }
    if (s_hl_valid) {
        float hx, hy, px_, py_;
        galaxy_star_pos(s_hl, &hx, &hy);
        galaxy_star_pos(s_cur_sys, &px_, &py_);
        s_hl_dist = sqrtf((hx - px_) * (hx - px_) + (hy - py_) * (hy - py_));
    }
}

/* Snap the cursor to the nearest star in a +-60deg wedge along (dx,dy). */
static void gmap_snap(float dx, float dy) {
    int sx0 = (int)floorf((s_cx_ly - 30) / SECTOR_LY);
    int sx1 = (int)floorf((s_cx_ly + 30) / SECTOR_LY);
    int sy0 = (int)floorf((s_cy_ly - 30) / SECTOR_LY);
    int sy1 = (int)floorf((s_cy_ly + 30) / SECTOR_LY);
    float best = 1e9f, bx = s_cx_ly, by = s_cy_ly;
    for (int sy = sy0; sy <= sy1; sy++)
        for (int sx = sx0; sx <= sx1; sx++) {
            int n = galaxy_sector_stars(sx, sy);
            for (int i = 0; i < n; i++) {
                SysAddr a = { sx, sy, (uint8_t)i };
                float x, y;
                galaxy_star_pos(a, &x, &y);
                float ex = x - s_cx_ly, ey = y - s_cy_ly;
                float d = sqrtf(ex * ex + ey * ey);
                if (d < 0.05f) continue;                /* the one we're on */
                if (ex * dx + ey * dy < d * 0.5f) continue;   /* wedge */
                if (d < best) { best = d; bx = x; by = y; }
            }
        }
    s_cx_ly = bx;
    s_cy_ly = by;
}

MapAction map_galaxy_tick(const CraftRawButtons *btn, float dt,
                          SysAddr *out_addr, float *out_dist_ly) {
    /* Tap = snap to the next star that way; hold >0.35s = smooth pan. */
    static float s_hold;
    bool any = btn->left || btn->right || btn->up || btn->down;
    if (any) s_hold += dt; else s_hold = 0;

    if (JUST(btn, left))  gmap_snap(-1, 0);
    if (JUST(btn, right)) gmap_snap(1, 0);
    if (JUST(btn, up))    gmap_snap(0, -1);
    if (JUST(btn, down))  gmap_snap(0, 1);
    if (s_hold > 0.35f) {
        float spd = 12.0f * dt;
        if (btn->left)  s_cx_ly -= spd;
        if (btn->right) s_cx_ly += spd;
        if (btn->up)    s_cy_ly -= spd;
        if (btn->down)  s_cy_ly += spd;
    }
    gmap_highlight();

    MapAction act = MAP_NONE;
    if (JUST(btn, a) && s_hl_valid && !sysaddr_eq(s_hl, s_cur_sys) &&
        s_hl_dist <= s_range && s_hl_dist <= s_fuel) {
        *out_addr = s_hl;
        *out_dist_ly = s_hl_dist;
        act = MAP_ENGAGE_JUMP;
    }
    if (JUST(btn, b) || JUST(btn, menu)) act = MAP_CLOSE;
    s_edge.prev = *btn;
    return act;
}

void map_galaxy_draw(uint16_t *fb) {
    fill(fb, COL_BG);

    /* Sector grid lines. */
    float x0_ly = s_cx_ly - 64.0f / GMAP_SCALE;
    float y0_ly = s_cy_ly - 64.0f / GMAP_SCALE;
    for (float gx = floorf(x0_ly / SECTOR_LY) * SECTOR_LY;; gx += SECTOR_LY) {
        int sx = (int)((gx - x0_ly) * GMAP_SCALE);
        if (sx > 127) break;
        if (sx >= 0)
            for (int y = 10; y < 118; y += 2) px(fb, sx, y, COL_GRID);
    }
    for (float gy = floorf(y0_ly / SECTOR_LY) * SECTOR_LY;; gy += SECTOR_LY) {
        int sy = (int)((gy - y0_ly) * GMAP_SCALE);
        if (sy > 117) break;
        if (sy >= 10)
            for (int x = 0; x < 128; x += 2) px(fb, x, sy, COL_GRID);
    }

    /* Jump-range ring around the current system. */
    float pxl, pyl;
    galaxy_star_pos(s_cur_sys, &pxl, &pyl);
    int rcx = (int)((pxl - x0_ly) * GMAP_SCALE);
    int rcy = (int)((pyl - y0_ly) * GMAP_SCALE);
    float rr = (s_range < s_fuel ? s_range : s_fuel) * GMAP_SCALE;
    for (int i = 0; i < 72; i++) {
        float a = (float)i * (6.2831853f / 72.0f);
        px(fb, rcx + (int)(rr * cosf(a)), rcy + (int)(rr * sinf(a)), COL_RING);
    }

    /* Stars in view. */
    int sx0 = (int)floorf(x0_ly / SECTOR_LY) - 1;
    int sx1 = (int)floorf((x0_ly + 128 / GMAP_SCALE) / SECTOR_LY) + 1;
    int sy0 = (int)floorf(y0_ly / SECTOR_LY) - 1;
    int sy1 = (int)floorf((y0_ly + 128 / GMAP_SCALE) / SECTOR_LY) + 1;
    for (int sy = sy0; sy <= sy1; sy++)
        for (int sx = sx0; sx <= sx1; sx++) {
            int n = galaxy_sector_stars(sx, sy);
            for (int i = 0; i < n; i++) {
                SysAddr a = { sx, sy, (uint8_t)i };
                float x, y;
                galaxy_star_pos(a, &x, &y);
                int dx = (int)((x - x0_ly) * GMAP_SCALE);
                int dy = (int)((y - y0_ly) * GMAP_SCALE);
                px(fb, dx, dy, COL_STAR);
                if (sysaddr_eq(a, s_cur_sys)) {     /* our marker */
                    px(fb, dx - 2, dy, COL_SELF);
                    px(fb, dx + 2, dy, COL_SELF);
                    px(fb, dx, dy - 2, COL_SELF);
                    px(fb, dx, dy + 2, COL_SELF);
                }
            }
        }

    /* Highlight + cursor. */
    if (s_hl_valid) {
        float hx, hy;
        galaxy_star_pos(s_hl, &hx, &hy);
        int dx = (int)((hx - x0_ly) * GMAP_SCALE);
        int dy = (int)((hy - y0_ly) * GMAP_SCALE);
        uint16_t c = (s_hl_dist <= s_range && s_hl_dist <= s_fuel)
                         ? COL_CUR : COL_WARN;
        for (int k = 2; k <= 3; k++) {
            px(fb, dx - k, dy - k, c); px(fb, dx + k, dy - k, c);
            px(fb, dx - k, dy + k, c); px(fb, dx + k, dy + k, c);
        }
    }

    /* Header + footer info. */
    craft_font_draw(fb, "GALAXY CHART", 2, 2, COL_TITLE);
    char buf[32];
    snprintf(buf, sizeof buf, "FUEL %d.%d LY", (int)s_fuel,
             ((int)(s_fuel * 10)) % 10);
    craft_font_draw(fb, buf, 76, 2, COL_TXT);
    for (int x = 0; x < 128; x++) px(fb, x, 9, COL_GRID);
    for (int x = 0; x < 128; x++) px(fb, x, 118, COL_GRID);
    if (s_hl_valid) {
        char name[14];
        galaxy_system_name(s_hl, name);
        if (sysaddr_eq(s_hl, s_cur_sys))
            snprintf(buf, sizeof buf, "%s  <HERE>", name);
        else
            snprintf(buf, sizeof buf, "%s  %d.%dLY %s", name,
                     (int)s_hl_dist, ((int)(s_hl_dist * 10)) % 10,
                     (s_hl_dist <= s_range && s_hl_dist <= s_fuel)
                         ? "A:JUMP" : "RANGE!");
        craft_font_draw(fb, buf, 2, 121, COL_TXT);
    } else {
        craft_font_draw(fb, "B:BACK", 2, 121, COL_DIM);
    }
}

/* ====================== SYSTEM MAP ===================================== */
static Poi  s_pois[MAX_POIS];
static int  s_npois, s_cursor;
static Vec3 s_player_mm;

void map_system_open(Vec3 player_pos_mm) {
    s_npois = system_pois(s_pois, MAX_POIS);
    s_cursor = 0;
    s_player_mm = player_pos_mm;
    edges_reset();
}

MapAction map_system_tick(const CraftRawButtons *btn, float dt, Poi *out_poi) {
    (void)dt;
    MapAction act = MAP_NONE;
    if (JUST(btn, down) && s_cursor < s_npois - 1) s_cursor++;
    if (JUST(btn, up) && s_cursor > 0) s_cursor--;
    if (JUST(btn, a) && s_npois > 0) {
        *out_poi = s_pois[s_cursor];
        act = MAP_ENGAGE_SC;
    }
    if (JUST(btn, b) || JUST(btn, menu)) act = MAP_CLOSE;
    s_edge.prev = *btn;
    return act;
}

void map_system_draw(uint16_t *fb) {
    fill(fb, COL_BG);
    const SystemInfo *si = system_info();

    char buf[36];
    snprintf(buf, sizeof buf, "%s SYSTEM", si->name);
    craft_font_draw(fb, buf, 2, 2, COL_TITLE);
    for (int x = 0; x < 128; x++) px(fb, x, 9, COL_GRID);

    /* Schematic strip: star at left, planets by orbit order. */
    int strip_y = 22;
    /* Star. */
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++)
            if (dx * dx + dy * dy <= 16)
                px(fb, 8 + dx, strip_y + dy, si->star_color);
    for (int i = 0; i < si->n_planets; i++) {
        int x = 24 + i * 14;
        int r = si->planets[i].type == PT_GAS ? 4 : 2;
        uint16_t c = (si->planets[i].type == PT_LAVA) ? RGB565C(200, 90, 30)
                   : (si->planets[i].type == PT_ICE) ? RGB565C(210, 225, 240)
                   : (si->planets[i].type == PT_GAS) ? RGB565C(200, 170, 120)
                   : (si->planets[i].type == PT_ROCK) ? RGB565C(150, 130, 105)
                   : RGB565C(60, 130, 180);
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
                if (dx * dx + dy * dy <= r * r) px(fb, x + dx, strip_y + dy, c);
        if (si->planets[i].station >= 0) {     /* station tick above */
            px(fb, x, strip_y - 7, COL_DIM);
            px(fb, x - 1, strip_y - 8, COL_DIM);
            px(fb, x + 1, strip_y - 8, COL_DIM);
        }
    }

    /* POI list with cursor. */
    int list_y = 36;
    int first = s_cursor - 4 > 0 ? s_cursor - 4 : 0;
    for (int i = first; i < s_npois && list_y < 110; i++, list_y += 9) {
        const Poi *p = &s_pois[i];
        float dist = v3_len(v3_sub(p->pos_mm, s_player_mm));
        const char *icon = (p->kind == POI_BEACON) ? "@"
                         : (p->kind == POI_STATION) ? "#" : "O";
        snprintf(buf, sizeof buf, "%s %-13s %5dMM", icon, p->name, (int)dist);
        craft_font_draw(fb, buf, 8, list_y,
                        (i == s_cursor) ? COL_CUR : COL_DIM);
        if (i == s_cursor) craft_font_draw(fb, ">", 2, list_y, COL_CUR);
    }

    for (int x = 0; x < 128; x++) px(fb, x, 118, COL_GRID);
    craft_font_draw(fb, "A:SUPERCRUISE B:BACK", 2, 121, COL_TXT);
}
