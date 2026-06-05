/*
 * ThumbyElite — in-flight HUD (128x128), Elite-style dashboard.
 *
 * Full-width console along the bottom with a raised centre housing for
 * the scanner (the iconic Elite silhouette). Speed/throttle instruments
 * live in the left panel, shield/hull/heat in the right. Canopy pillars
 * frame the view. Centre: crosshair, target brackets, hit/kill markers.
 */
#include "ui_hud.h"
#include "elite_types.h"
#include "elite_entity.h"
#include "elite_combat.h"
#include "elite_loot.h"
#include "r3d_scene.h"
#include "craft_font.h"
#include "ui_icons.h"
#include <stdio.h>

#define COL_SHIELD  RGB565C( 80, 180, 255)
#define COL_HULL    RGB565C(255, 120,  70)
#define COL_HEAT    RGB565C(255, 200,  60)
#define COL_FRAME   RGB565C( 60,  90, 115)
#define COL_CROSS   RGB565C(140, 220, 170)
#define COL_TARGET  RGB565C(255,  90,  70)
#define COL_BLIP_H  RGB565C(255,  80,  60)
#define COL_BLIP_N  RGB565C(170, 170, 180)
#define COL_BLIP_L  RGB565C(255, 210,  70)   /* loot canisters */
#define COL_TEXT    RGB565C(120, 255, 120)
#define COL_NUM     RGB565C(200, 210, 225)
#define COL_CONSOLE RGB565C( 14,  18,  30)
#define COL_STRUT   RGB565C( 95, 110, 140)
#define COL_CANOPY  RGB565C( 55,  65,  88)
#define COL_PILLAR  RGB565C( 30,  38,  55)
#define COL_GRID    RGB565C( 32,  44,  62)
#define COL_CUR_DEST RGB565C(120, 230, 255)   /* supercruise destination */

/* Dashboard geometry. */
#define DASH_TOP   101
#define BULGE_TOP   93
#define BULGE_L0    36   /* bulge foot, left */
#define BULGE_L1    44   /* bulge shoulder, left */
#define BULGE_R1    83
#define BULGE_R0    91

static inline void px(uint16_t *fb, int x, int y, uint16_t c) {
    if ((unsigned)x < ELITE_FB_W && (unsigned)y < ELITE_FB_H)
        fb[y * ELITE_FB_W + x] = c;
}
static void hline(uint16_t *fb, int x0, int x1, int y, uint16_t c) {
    for (int x = x0; x <= x1; x++) px(fb, x, y, c);
}
static void vline(uint16_t *fb, int x, int y0, int y1, uint16_t c) {
    for (int y = y0; y <= y1; y++) px(fb, x, y, c);
}
static void line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t c) {
    float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
    float adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int steps = (int)(adx > ady ? adx : ady) + 1;
    float stx = dx / steps, sty = dy / steps;
    float cx = (float)x0, cy = (float)y0;
    for (int i = 0; i <= steps; i++) {
        px(fb, (int)cx, (int)cy, c);
        cx += stx; cy += sty;
    }
}

static void bar(uint16_t *fb, int x, int y, int w, float frac, uint16_t c) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    hline(fb, x, x + w, y - 1, COL_GRID);
    hline(fb, x, x + w, y + 1, COL_GRID);
    px(fb, x - 1, y, COL_GRID);
    px(fb, x + w + 1, y, COL_GRID);
    int fill = (int)(w * frac + 0.5f);
    if (fill > 0) hline(fb, x, x + fill, y, c);
}

/* --- Dashboard ---------------------------------------------------------*/
static void dashboard(uint16_t *fb) {
    /* Console fill: bulge rows then the full-width slab. */
    for (int y = BULGE_TOP; y < DASH_TOP; y++) {
        int t = y - BULGE_TOP;                       /* shoulder slope 1:1 */
        int x0 = BULGE_L1 - t, x1 = BULGE_R1 + t;
        hline(fb, x0, x1, y, COL_CONSOLE);
    }
    for (int y = DASH_TOP; y < ELITE_FB_H; y++)
        hline(fb, 0, ELITE_FB_W - 1, y, COL_CONSOLE);

    /* Edge highlights. */
    hline(fb, 0, BULGE_L0, DASH_TOP, COL_STRUT);
    hline(fb, BULGE_R0, ELITE_FB_W - 1, DASH_TOP, COL_STRUT);
    line(fb, BULGE_L0, DASH_TOP, BULGE_L1, BULGE_TOP, COL_STRUT);
    hline(fb, BULGE_L1, BULGE_R1, BULGE_TOP, COL_STRUT);
    line(fb, BULGE_R1, BULGE_TOP, BULGE_R0, DASH_TOP, COL_STRUT);

    /* Canopy pillars + diagonals. */
    line(fb, 0, 30, 30, 6, COL_CANOPY);
    line(fb, 127, 30, 97, 6, COL_CANOPY);
    vline(fb, 0, 30, DASH_TOP - 1, COL_PILLAR);
    vline(fb, 127, 30, DASH_TOP - 1, COL_PILLAR);
}

/* --- Scanner (Elite disc, in the bulge) -------------------------------- */
#define SC_CX 63
#define SC_CY 110
#define SC_RX 24
#define SC_RY 13
#define SC_RANGE 400.0f

static void scanner(uint16_t *fb) {
    /* Dim grid cross first, then the rim. */
    hline(fb, SC_CX - SC_RX + 2, SC_CX + SC_RX - 2, SC_CY, COL_GRID);
    vline(fb, SC_CX, SC_CY - SC_RY + 1, SC_CY + SC_RY - 1, COL_GRID);
    for (int i = 0; i < 64; i++) {
        float a = (float)i * (6.2831853f / 64.0f);
        px(fb, SC_CX + (int)(SC_RX * cosf(a)),
               SC_CY + (int)(SC_RY * sinf(a)), COL_FRAME);
    }
    px(fb, SC_CX, SC_CY, RGB565C(255, 255, 255));

    const Ship *p = &g_ships[PLAYER];
    for (int i = 1; i < MAX_SHIPS; i++) {
        const Ship *s = &g_ships[i];
        if (!s->alive) continue;
        Vec3 local = m3_mul_v3_t(&p->basis, v3_sub(s->pos, p->pos));
        float dx = local.x * (SC_RX / SC_RANGE);
        float dz = -local.z * (SC_RY / SC_RANGE);
        float e = (dx * dx) / (SC_RX * SC_RX) + (dz * dz) / (SC_RY * SC_RY);
        if (e > 1.0f) {
            float k = 1.0f / sqrtf(e);
            dx *= k; dz *= k;
        }
        int fx = SC_CX + (int)dx, fy = SC_CY + (int)dz;
        int stalk = (int)(-local.y * (40.0f / SC_RANGE));
        if (stalk > 9) stalk = 9;
        if (stalk < -9) stalk = -9;
        uint16_t c = (s->team == TEAM_HOSTILE) ? COL_BLIP_H : COL_BLIP_N;
        px(fb, fx, fy, COL_FRAME);
        if (stalk != 0)
            vline(fb, fx, fy < fy + stalk ? fy : fy + stalk,
                  fy < fy + stalk ? fy + stalk : fy, c);
        px(fb, fx, fy + stalk, c);
        px(fb, fx + 1, fy + stalk, c);
    }

    /* Loot canisters: gold blips, blinking so they catch the eye.
     * Components blink brighter than commodity pods. */
    static uint8_t s_blink;
    s_blink++;
    Vec3 cans[6];
    int comp[6];
    int n = loot_positions(cans, comp, 6);
    for (int i = 0; i < n; i++) {
        if (!comp[i] && (s_blink & 16)) continue;       /* slow blink */
        if (comp[i] && (s_blink & 8)) continue;         /* fast blink */
        Vec3 local = m3_mul_v3_t(&p->basis, v3_sub(cans[i], p->pos));
        float dx = local.x * (SC_RX / SC_RANGE);
        float dz = -local.z * (SC_RY / SC_RANGE);
        float e = (dx * dx) / (SC_RX * SC_RX) + (dz * dz) / (SC_RY * SC_RY);
        if (e > 1.0f) {
            float k = 1.0f / sqrtf(e);
            dx *= k; dz *= k;
        }
        int fx = SC_CX + (int)dx, fy = SC_CY + (int)dz;
        int stalk = (int)(-local.y * (40.0f / SC_RANGE));
        if (stalk > 9) stalk = 9;
        if (stalk < -9) stalk = -9;
        if (stalk != 0)
            vline(fb, fx, fy < fy + stalk ? fy : fy + stalk,
                  fy < fy + stalk ? fy + stalk : fy,
                  RGB565C(140, 110, 40));
        px(fb, fx, fy + stalk, COL_BLIP_L);
    }
}

/* --- Target brackets ---------------------------------------------------*/
static void target_box(uint16_t *fb, int target) {
    const Ship *p = &g_ships[PLAYER];
    const Ship *t = &g_ships[target];
    float sx, sy;
    uint16_t d;
    if (r3d_scene_project(v3_sub(t->pos, p->pos), &sx, &sy, &d) &&
        sx > -20 && sx < 148 && sy > -20 && sy < 148) {
        float z = R3D_DEPTH_K / (float)d;
        int h = (int)(r3d_pipe_focal() * t->mesh->bound_r / z);
        if (h < 5) h = 5;
        if (h > 24) h = 24;
        int x0 = (int)sx - h, x1 = (int)sx + h;
        int y0 = (int)sy - h, y1 = (int)sy + h;
        int l = h / 2;
        hline(fb, x0, x0 + l, y0, COL_TARGET);
        vline(fb, x0, y0, y0 + l, COL_TARGET);
        hline(fb, x1 - l, x1, y0, COL_TARGET);
        vline(fb, x1, y0, y0 + l, COL_TARGET);
        hline(fb, x0, x0 + l, y1, COL_TARGET);
        vline(fb, x0, y1 - l, y1, COL_TARGET);
        hline(fb, x1 - l, x1, y1, COL_TARGET);
        vline(fb, x1, y1 - l, y1, COL_TARGET);
    } else {
        /* Edge arrow = the way to TURN: the view-space lateral direction
         * already encodes the shortest rotation, in front OR behind —
         * no rear-hemisphere flip (that sent players the long way). */
        Vec3 v = m3_mul_v3_t(&p->basis, v3_sub(t->pos, p->pos));
        float ax = v.x, ay = -v.y;
        float al = sqrtf(ax * ax + ay * ay);
        if (al < 1e-4f) { ax = 1; ay = 0; al = 1; }
        ax /= al; ay /= al;
        int ex = 64 + (int)(ax * 52.0f);
        int ey = 60 + (int)(ay * 44.0f);
        /* Arrowhead: tip leads, barbs flare BACK from it. */
        px(fb, ex, ey, COL_TARGET);
        px(fb, ex - (int)(ax * 2 + ay * 2), ey - (int)(ay * 2 - ax * 2),
           COL_TARGET);
        px(fb, ex - (int)(ax * 2 - ay * 2), ey - (int)(ay * 2 + ax * 2),
           COL_TARGET);
        px(fb, ex - (int)(ax * 3), ey - (int)(ay * 3), COL_TARGET);
        px(fb, ex - (int)(ax * 5), ey - (int)(ay * 5), COL_TARGET);
    }

    /* Target readout under the canopy line, top-right. */
    char buf[24];
    float dist = v3_len(v3_sub(t->pos, p->pos));
    snprintf(buf, sizeof buf, "%dM", (int)dist);
    craft_font_draw(fb, buf, 98, 12, COL_TARGET);
    bar(fb, 100, 20, 22, t->shield / (t->shield_max > 0 ? t->shield_max : 1),
        COL_SHIELD);
    bar(fb, 100, 24, 22, t->hull / (t->hull_max > 0 ? t->hull_max : 1),
        COL_HULL);
    craft_font_draw(fb, t->is_mark ? "** MARK **"
                                   : k_tier_names[t->tier > 4 ? 4 : t->tier],
                    98, 28, COL_TARGET);
}

void ui_hud_draw(uint16_t *fb, const HudInfo *info) {
    const Ship *p = &g_ships[PLAYER];

    dashboard(fb);

    /* Crosshair. */
    hline(fb, 59, 61, 64, COL_CROSS);
    hline(fb, 67, 69, 64, COL_CROSS);
    vline(fb, 64, 59, 61, COL_CROSS);
    vline(fb, 64, 67, 69, COL_CROSS);

    /* Hit marker: diagonal ticks flaring from the crosshair. */
    if (combat_hitmarker() > 0.0f) {
        for (int k = 3; k <= 5; k++) {
            px(fb, 64 - k, 64 - k, COL_TARGET);
            px(fb, 64 + k, 64 - k, COL_TARGET);
            px(fb, 64 - k, 64 + k, COL_TARGET);
            px(fb, 64 + k, 64 + k, COL_TARGET);
        }
    }
    /* Kill marker: brief confirm under the crosshair. */
    if (combat_killmarker() > 0.0f)
        craft_font_draw(fb, "KILL", 57, 74, COL_TARGET);

    /* Active weapon + ammo, top-centre under the perf line. */
    char buf[24];
    if (p->n_weapons > 0) {
        const WeaponDef *w = &k_weapons[p->weapons[p->active_w]];
        if (w->ammo_max)
            snprintf(buf, sizeof buf, "%s %d", w->name,
                     (int)p->ammo[p->active_w]);
        else
            snprintf(buf, sizeof buf, "%s", w->name);
        int wx = 64 - craft_font_width(buf) / 2 + 7;
        craft_font_draw(fb, buf, wx,
                        10, (w->ammo_max && p->ammo[p->active_w] <= 0)
                                ? COL_HULL : COL_NUM);
        icon_weapon(fb, wx - 15, 9, p->weapons[p->active_w]);
    }

    /* Left panel: speed / throttle / status lights. */
    craft_font_draw(fb, "SP", 2, 102, COL_TEXT);
    bar(fb, 13, 105, 20, v3_len(p->vel) / (p->max_speed * 1.8f), COL_TEXT);
    craft_font_draw(fb, "TH", 2, 109, COL_NUM);
    bar(fb, 13, 112, 20, p->throttle, COL_NUM);
    snprintf(buf, sizeof buf, "%3d", (int)v3_len(p->vel));
    craft_font_draw(fb, buf, 2, 118, COL_NUM);
    if (!p->assist) craft_font_draw(fb, "DR", 18, 118, COL_HEAT);
    if (p->boost_t > 0) craft_font_draw(fb, "BS", 27, 118, COL_SHIELD);

    /* Right panel: shield / hull / heat + wave/kills. */
    craft_font_draw(fb, "S", 94, 102, COL_SHIELD);
    bar(fb, 101, 105, 22, p->shield / p->shield_max, COL_SHIELD);
    craft_font_draw(fb, "H", 94, 109, COL_HULL);
    bar(fb, 101, 112, 22, p->hull / p->hull_max, COL_HULL);
    craft_font_draw(fb, "T", 94, 116, COL_HEAT);
    bar(fb, 101, 119, 22, p->heat / 100.0f, COL_HEAT);
    craft_font_draw(fb, "F", 94, 122, COL_NUM);
    bar(fb, 101, 124, 22, info->fuel01, COL_NUM);
    snprintf(buf, sizeof buf, "K%d", info->kills);
    craft_font_draw(fb, buf, 2, 124, COL_TEXT);

    scanner(fb);

    if (info->target >= 0 && g_ships[info->target].alive)
        target_box(fb, info->target);
    else if (info->loot_valid) {
        /* Salvage lock: gold brackets + distance, same edge-arrow cue. */
        Ship *p = &g_ships[PLAYER];
        float sx, sy;
        uint16_t d;
        Vec3 rel = v3_sub(info->loot_pos, p->pos);
        if (r3d_scene_project(rel, &sx, &sy, &d)) {
            int bx = (int)sx, by = (int)sy;
            uint16_t gc = RGB565C(255, 210, 70);
            for (int k = 0; k < 3; k++) {
                px(fb, bx - 5 + k, by - 5, gc); px(fb, bx + 5 - k, by - 5, gc);
                px(fb, bx - 5 + k, by + 5, gc); px(fb, bx + 5 - k, by + 5, gc);
                px(fb, bx - 5, by - 5 + k, gc); px(fb, bx - 5, by + 5 - k, gc);
                px(fb, bx + 5, by - 5 + k, gc); px(fb, bx + 5, by + 5 - k, gc);
            }
            char buf[16];
            snprintf(buf, sizeof buf, "%dM", (int)v3_len(rel));
            craft_font_draw(fb, buf, bx - 8, by + 8, gc);
        } else {
            Vec3 v = m3_mul_v3_t(&p->basis, rel);
            float ax = v.x, ay = -v.y;
            float al = sqrtf(ax * ax + ay * ay);
            if (al < 1e-4f) { ax = 1; ay = 0; al = 1; }
            ax /= al; ay /= al;
            int ex = 64 + (int)(ax * 52.0f);
            int ey = 60 + (int)(ay * 44.0f);
            uint16_t gc = RGB565C(255, 210, 70);
            px(fb, ex, ey, gc);
            px(fb, ex - (int)(ax * 2 + ay * 2), ey - (int)(ay * 2 - ax * 2), gc);
            px(fb, ex - (int)(ax * 2 - ay * 2), ey - (int)(ay * 2 + ax * 2), gc);
            px(fb, ex - (int)(ax * 3), ey - (int)(ay * 3), gc);
        }
    }

    if (info->show_perf) {
        snprintf(buf, sizeof buf, "%d.%dMS %dT",
                 (int)info->render_ms,
                 ((int)(info->render_ms * 10.0f)) % 10,
                 r3d_scene_tri_count());
        craft_font_draw(fb, buf, 40, 2, COL_TEXT);
    }
}

/* --- Supercruise HUD ---------------------------------------------------*/
void ui_hud_draw_sc(uint16_t *fb, const HudScInfo *info) {
    const Ship *p = &g_ships[PLAYER];
    char buf[32];

    dashboard(fb);

    /* Crosshair. */
    hline(fb, 59, 61, 64, COL_CROSS);
    hline(fb, 67, 69, 64, COL_CROSS);
    vline(fb, 64, 59, 61, COL_CROSS);
    vline(fb, 64, 67, 69, COL_CROSS);

    /* Destination marker: project the direction (clamped to an edge
     * arrow when off-screen), plus distance/alignment readout. */
    if (info->dest_name) {
        Vec3 rel_m = v3_scale(info->dest_rel_mm, 1.0e6f);
        float sx, sy;
        uint16_t d;
        if (r3d_scene_project(rel_m, &sx, &sy, &d) &&
            sx >= 4 && sx < 124 && sy >= 12 && sy < 92) {
            int x = (int)sx, y = (int)sy;
            /* Diamond reticle. */
            for (int k = 3; k <= 5; k++) {
                px(fb, x + k, y, COL_CUR_DEST); px(fb, x - k, y, COL_CUR_DEST);
                px(fb, x, y + k, COL_CUR_DEST); px(fb, x, y - k, COL_CUR_DEST);
            }
        } else {
            Vec3 v = m3_mul_v3_t(&p->basis, rel_m);
            float ax = v.x, ay = -v.y;
            float al = sqrtf(ax * ax + ay * ay);
            if (al < 1e-4f) { ax = 1; ay = 0; al = 1; }
            ax /= al; ay /= al;
            int ex = 64 + (int)(ax * 52.0f), ey = 60 + (int)(ay * 44.0f);
            px(fb, ex, ey, COL_CUR_DEST);
            px(fb, ex - (int)(ax * 2 + ay * 2), ey - (int)(ay * 2 - ax * 2),
               COL_CUR_DEST);
            px(fb, ex - (int)(ax * 2 - ay * 2), ey - (int)(ay * 2 + ax * 2),
               COL_CUR_DEST);
            for (int k = 3; k <= 5; k++)
                px(fb, ex - (int)(ax * k), ey - (int)(ay * k), COL_CUR_DEST);
        }
        float dist = v3_len(info->dest_rel_mm);
        snprintf(buf, sizeof buf, "%s", info->dest_name);
        craft_font_draw(fb, buf, 2, 12, COL_CUR_DEST);
        if (dist >= 100.0f)
            snprintf(buf, sizeof buf, "%dMM", (int)dist);
        else
            snprintf(buf, sizeof buf, "%d.%dMM", (int)dist,
                     ((int)(dist * 10)) % 10);
        craft_font_draw(fb, buf, 2, 19, COL_NUM);
        if (info->eta_s > 0 && info->eta_s < 999) {
            snprintf(buf, sizeof buf, "ETA %dS", (int)info->eta_s);
            craft_font_draw(fb, buf, 2, 26, COL_NUM);
        }
    }

    craft_font_draw(fb, "SUPERCRUISE", 42, 110, COL_SHIELD);

    /* Left panel: speed (Mm/s) + throttle. */
    craft_font_draw(fb, "SP", 2, 102, COL_TEXT);
    bar(fb, 13, 105, 20, info->speed_mms / 50.0f, COL_TEXT);
    craft_font_draw(fb, "TH", 2, 109, COL_NUM);
    bar(fb, 13, 112, 20, info->throttle, COL_NUM);
    if (info->speed_mms >= 1.0f)
        snprintf(buf, sizeof buf, "%dMM/S", (int)info->speed_mms);
    else
        snprintf(buf, sizeof buf, "%dKM/S", (int)(info->speed_mms * 1000.0f));
    craft_font_draw(fb, buf, 2, 118, COL_NUM);

    /* Right panel: fuel + hull (shields don't matter in SC). */
    craft_font_draw(fb, "H", 94, 102, COL_HULL);
    bar(fb, 101, 105, 22, p->hull / p->hull_max, COL_HULL);
    craft_font_draw(fb, "F", 94, 109, COL_NUM);
    bar(fb, 101, 112, 22, info->fuel01, COL_NUM);
    craft_font_draw(fb, "B:DROP", 94, 122, COL_NUM);

    if (info->show_perf) {
        snprintf(buf, sizeof buf, "%d.%dMS %dT",
                 (int)info->render_ms,
                 ((int)(info->render_ms * 10.0f)) % 10,
                 r3d_scene_tri_count());
        craft_font_draw(fb, buf, 40, 2, COL_TEXT);
    }
}
