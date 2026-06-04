/*
 * ThumbyElite — shared types and framebuffer constants.
 *
 * The vendored craft_font.c (ThumbyCraft lineage) expects CRAFT_FB_W/H and
 * CRAFT_HUD_SCALE from a craft_types.h — this header stands in for it so the
 * font compiles unmodified.
 */
#ifndef ELITE_TYPES_H
#define ELITE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define ELITE_FB_W 128
#define ELITE_FB_H 128

/* Aliases the vendored font uses. */
#define CRAFT_FB_W ELITE_FB_W
#define CRAFT_FB_H ELITE_FB_H
#define CRAFT_HUD_SCALE 1

#define ELITE_AUDIO_RATE 22050

/* Compile-time constant variant (static initializers / const tables). */
#define RGB565C(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

#endif
