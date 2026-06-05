/*
 * ThumbyElite — top-level game module (platform-independent).
 *
 * The platform shells (host SDL2 / device RP2350) own the framebuffer and
 * the main loop; this module owns everything else. Phase 1: rotating
 * flat-shaded cube + frame-time readout to prove the renderer + builds.
 */
#ifndef ELITE_GAME_H
#define ELITE_GAME_H

#include <stdint.h>
#include "craft_buttons.h"

void elite_game_init(uint32_t seed);
void elite_game_tick(const CraftRawButtons *btn, float dt);

/* Render the 3D scene into fb, rows [y_min, y_max). Phase 1 is single
 * pass; the dual-core split (Phase 2) calls this once per core with each
 * screen half. Must be preceded by one elite_game_render_begin() call. */
void elite_game_render_begin(void);
void elite_game_render(uint16_t *fb, int y_min, int y_max);

/* 2D overlay (HUD/text) — single-core, after both render halves. */
void elite_game_draw_overlay(uint16_t *fb);

/* Platform reports the previous frame's total time for the perf readout. */
void elite_game_set_frame_ms(float ms);

/* Test/debug hooks (host harness). */
int  elite_game_state(void);    /* GState as int: 0=flight 1=sc 2=jump ... */
void elite_game_debug_spawn(int n);
void elite_game_debug_face_away_from_sun(void);
const char *elite_rank_name(int kills);

#endif
