/*
 * ThumbyElite — in-flight HUD (128x128).
 *
 * Layout: shield/hull bars top-left, heat bar + speed top-right, centre
 * crosshair + target brackets (with off-screen direction arrow), and the
 * classic Elite scanner disc bottom-centre: contacts as blips with
 * vertical stalks showing above/below the flight plane.
 */
#include "ui_hud.h"
#include "elite_types.h"
#include "elite_entity.h"
#include "r3d_scene.h"
#include "craft_font.h"
#include <stdio.h>

#define COL_SHIELD RGB565C( 80, 180, 255)
#define COL_HULL   RGB565C(255, 120,  70)
#define COL_HEAT   RGB565C(255, 200,  60)
#define COL_FRAME  RGB565C( 70, 110, 140)
#define COL_CROSS  RGB565C(140, 220, 170)
#define COL_TARGET RGB565C(255,  90,  70)
#define COL_BLIP_H RGB565C(255,  80,  60)
#define COL_BLIP_N RGB565C(170, 170, 180)
#define COL_TEXT   RGB565C(120, 255, 120)

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

static void bar(uint16_t *fb, int x, int y, int w, float frac, uint16_t c) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    hline(fb, x, x + w, y - 1, COL_FRAME);
    hline(fb, x, x + w, y + 1, COL_FRAME);
    px(fb, x - 1, y, COL_FRAME);
    px(fb, x + w + 1, y, COL_FRAME);
    int fill = (int)(w * frac + 0.5f);
    if (fill > 0) hline(fb, x, x + fill, y, c);
}

/* --- Scanner (Elite disc) --------------------------------------------- */
#define SC_CX 64
#define SC_CY 109
#define SC_RX 26
#define SC_RY 12
#define SC_RANGE 400.0f

static void scanner(uint16_t *fb) {
    /* Ellipse outline (parametric, 40 segments worth of pixels). */
    for (int i = 0; i < 64; i++) {
        float a = (float)i * (6.2831853f / 64.0f);
        px(fb, SC_CX + (int)(SC_RX * cosf(a)),
               SC_CY + (int)(SC_RY * sinf(a)), COL_FRAME);
    }
    /* Centre tick = us. */
    px(fb, SC_CX, SC_CY, RGB565C(255, 255, 255));

    const Ship *p = &g_ships[PLAYER];
    for (int i = 1; i < MAX_SHIPS; i++) {
        const Ship *s = &g_ships[i];
        if (!s->alive) continue;
        Vec3 local = m3_mul_v3_t(&p->basis, v3_sub(s->pos, p->pos));
        float dx = local.x * (SC_RX / SC_RANGE);
        float dz = -local.z * (SC_RY / SC_RANGE);
        /* Clamp into the disc. */
        float e = (dx * dx) / (SC_RX * SC_RX) + (dz * dz) / (SC_RY * SC_RY);
        if (e > 1.0f) {
            float k = 1.0f / sqrtf(e);
            dx *= k; dz *= k;
        }
        int fx = SC_CX + (int)dx, fy = SC_CY + (int)dz;
        int stalk = (int)(-local.y * (10.0f / SC_RANGE) * 4.0f);
        if (stalk > 9) stalk = 9;
        if (stalk < -9) stalk = -9;
        uint16_t c = (s->team == TEAM_HOSTILE) ? COL_BLIP_H : COL_BLIP_N;
        /* Foot on the plane, stalk up/down, blip at the head. */
        px(fb, fx, fy, COL_FRAME);
        if (stalk != 0)
            vline(fb, fx, fy < fy + stalk ? fy : fy + stalk,
                  fy < fy + stalk ? fy + stalk : fy, c);
        px(fb, fx, fy + stalk, c);
        px(fb, fx + 1, fy + stalk, c);
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
        /* Box half-size from projected bounding radius:
         * r_px = focal * r / z, and d = K/z -> z = K/d. */
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
        /* Off-screen: edge arrow along the view-space direction. */
        Vec3 v = m3_mul_v3_t(&p->basis, v3_sub(t->pos, p->pos));
        float ax = v.x, ay = -v.y;
        if (v.z < 0) { ax = -ax; ay = -ay; }   /* behind: flip */
        float al = sqrtf(ax * ax + ay * ay);
        if (al < 1e-4f) { ax = 1; ay = 0; al = 1; }
        ax /= al; ay /= al;
        int ex = 64 + (int)(ax * 56.0f);
        int ey = 64 + (int)(ay * 56.0f);
        px(fb, ex, ey, COL_TARGET);
        px(fb, ex - (int)(ax * 2), ey - (int)(ay * 2), COL_TARGET);
        px(fb, ex - (int)(ax * 4), ey - (int)(ay * 4), COL_TARGET);
        px(fb, ex + (int)(-ay * 2), ey + (int)(ax * 2), COL_TARGET);
        px(fb, ex + (int)(ay * 2), ey + (int)(-ax * 2), COL_TARGET);
    }

    /* Target readout: shield/hull, distance. */
    char buf[24];
    float dist = v3_len(v3_sub(t->pos, p->pos));
    snprintf(buf, sizeof buf, "%dM", (int)dist);
    craft_font_draw(fb, buf, 96, 14, COL_TARGET);
    bar(fb, 98, 22, 24, t->shield / (t->shield_max > 0 ? t->shield_max : 1),
        COL_SHIELD);
    bar(fb, 98, 26, 24, t->hull / (t->hull_max > 0 ? t->hull_max : 1),
        COL_HULL);
}

void ui_hud_draw(uint16_t *fb, const HudInfo *info) {
    const Ship *p = &g_ships[PLAYER];

    /* Crosshair. */
    hline(fb, 59, 61, 64, COL_CROSS);
    hline(fb, 67, 69, 64, COL_CROSS);
    vline(fb, 64, 59, 61, COL_CROSS);
    vline(fb, 64, 67, 69, COL_CROSS);

    /* Player bars (top-left): shield, hull. */
    bar(fb, 4, 4, 28, p->shield / p->shield_max, COL_SHIELD);
    bar(fb, 4, 9, 28, p->hull / p->hull_max, COL_HULL);
    /* Heat (below, fills as it heats). */
    bar(fb, 4, 14, 28, p->heat / 100.0f, COL_HEAT);

    /* Speed + throttle (bottom-left). */
    char buf[24];
    snprintf(buf, sizeof buf, "%3d", (int)v3_len(p->vel));
    craft_font_draw(fb, buf, 4, 116, COL_TEXT);
    craft_font_draw(fb, "M/S", 18, 116, COL_FRAME);
    bar(fb, 4, 124, 28, p->throttle, COL_TEXT);
    if (!p->assist) craft_font_draw(fb, "DRIFT", 4, 108, COL_HEAT);
    if (p->boost_t > 0) craft_font_draw(fb, "BOOST", 4, 100, COL_SHIELD);

    /* Wave / kills (bottom-right). */
    snprintf(buf, sizeof buf, "W%d K%d", info->wave, info->kills);
    craft_font_draw(fb, buf, 96, 116, COL_TEXT);

    scanner(fb);

    if (info->target >= 0 && g_ships[info->target].alive)
        target_box(fb, info->target);

    if (info->show_perf) {
        snprintf(buf, sizeof buf, "%d.%dMS %dT",
                 (int)info->render_ms,
                 ((int)(info->render_ms * 10.0f)) % 10,
                 r3d_scene_tri_count());
        craft_font_draw(fb, buf, 40, 2, COL_TEXT);
    }
}
