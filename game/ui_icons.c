/*
 * ThumbyElite — tiny procedural icons.
 *
 * 12x7 weapon glyphs drawn with pixel ops — distinct silhouettes per
 * family: laser emitters (barrel mass by size), beam dashes, photon orb,
 * gauss rail, autocannon twin barrels, missile darts (homing adds a
 * seeker arc). Bodies grey, muzzle/payload in the weapon's colour.
 */
#include "ui_icons.h"
#include "elite_types.h"
#include "elite_weapons.h"

#define BODY  RGB565C(150, 155, 170)
#define BODY2 RGB565C(100, 105, 122)

static inline void px(uint16_t *fb, int x, int y, uint16_t c) {
    if ((unsigned)x < ELITE_FB_W && (unsigned)y < ELITE_FB_H)
        fb[y * ELITE_FB_W + x] = c;
}
static void hbar(uint16_t *fb, int x0, int x1, int y, uint16_t c) {
    for (int x = x0; x <= x1; x++) px(fb, x, y, c);
}

void icon_weapon(uint16_t *fb, int x, int y, int wpn_type) {
    uint16_t hot = k_weapons[wpn_type].color;
    switch (wpn_type) {
    case WPN_PULSE_S:
        hbar(fb, x + 2, x + 7, y + 3, BODY);
        px(fb, x + 8, y + 3, hot);
        px(fb, x + 2, y + 4, BODY2);
        break;
    case WPN_PULSE_M:
        hbar(fb, x + 1, x + 7, y + 2, BODY);
        hbar(fb, x + 1, x + 7, y + 4, BODY);
        hbar(fb, x + 1, x + 7, y + 3, BODY2);
        px(fb, x + 8, y + 3, hot);
        px(fb, x + 9, y + 3, hot);
        break;
    case WPN_PULSE_L:
        for (int r = 1; r <= 5; r++) hbar(fb, x, x + 7, y + r, BODY);
        hbar(fb, x + 1, x + 6, y + 3, BODY2);
        px(fb, x + 8, y + 2, hot); px(fb, x + 9, y + 3, hot);
        px(fb, x + 8, y + 4, hot); px(fb, x + 10, y + 3, hot);
        break;
    case WPN_BEAM:
        hbar(fb, x, x + 5, y + 3, BODY);
        px(fb, x + 7, y + 3, hot); px(fb, x + 9, y + 3, hot);
        px(fb, x + 11, y + 3, hot);
        break;
    case WPN_PHOTON:
        hbar(fb, x, x + 4, y + 3, BODY);
        px(fb, x + 8, y + 2, hot); px(fb, x + 7, y + 3, hot);
        px(fb, x + 8, y + 3, RGB565C(255, 255, 255));
        px(fb, x + 9, y + 3, hot); px(fb, x + 8, y + 4, hot);
        break;
    case WPN_GAUSS:
        hbar(fb, x, x + 10, y + 3, BODY2);
        px(fb, x + 2, y + 2, BODY); px(fb, x + 5, y + 2, BODY);
        px(fb, x + 8, y + 2, BODY);
        px(fb, x + 2, y + 4, BODY); px(fb, x + 5, y + 4, BODY);
        px(fb, x + 8, y + 4, BODY);
        px(fb, x + 11, y + 3, hot);
        break;
    case WPN_AUTOCANNON:
        hbar(fb, x + 2, x + 8, y + 2, BODY);
        hbar(fb, x + 2, x + 8, y + 4, BODY);
        px(fb, x, y + 3, BODY2); px(fb, x + 1, y + 3, BODY2);
        px(fb, x + 9, y + 2, hot); px(fb, x + 9, y + 4, hot);
        break;
    case WPN_MISSILE:
        hbar(fb, x + 2, x + 8, y + 3, BODY);
        px(fb, x + 2, y + 2, BODY2); px(fb, x + 2, y + 4, BODY2);
        px(fb, x + 3, y + 2, BODY2); px(fb, x + 3, y + 4, BODY2);
        px(fb, x + 9, y + 3, hot); px(fb, x + 10, y + 3, hot);
        break;
    case WPN_HOMING:
        hbar(fb, x + 2, x + 7, y + 3, BODY);
        px(fb, x + 2, y + 2, BODY2); px(fb, x + 2, y + 4, BODY2);
        px(fb, x + 8, y + 3, hot);
        /* Seeker arc. */
        px(fb, x + 10, y + 1, hot); px(fb, x + 11, y + 3, hot);
        px(fb, x + 10, y + 5, hot);
        break;
    default:
        break;
    }
}

void icon_weapon_2x(uint16_t *fb, int x, int y, int wpn_type) {
    /* Rasterise the 1x glyph into a scratch strip (fb-width stride so
     * the px() maths inside icon_weapon stays valid), then 2x upscale. */
    static uint16_t strip[ELITE_FB_W * 8];
    const uint16_t SENTINEL = 0x0821;
    for (int i = 0; i < ELITE_FB_W * 8; i++) strip[i] = SENTINEL;
    icon_weapon(strip, 0, 0, wpn_type);
    for (int sy = 0; sy < 7; sy++) {
        for (int sx = 0; sx < 12; sx++) {
            uint16_t c = strip[sy * ELITE_FB_W + sx];
            if (c == SENTINEL) continue;
            int dx = x + sx * 2, dy = y + sy * 2;
            if ((unsigned)(dx + 1) >= ELITE_FB_W) continue;
            if ((unsigned)(dy + 1) >= ELITE_FB_H) continue;
            fb[dy * ELITE_FB_W + dx] = c;
            fb[dy * ELITE_FB_W + dx + 1] = c;
            fb[(dy + 1) * ELITE_FB_W + dx] = c;
            fb[(dy + 1) * ELITE_FB_W + dx + 1] = c;
        }
    }
}
