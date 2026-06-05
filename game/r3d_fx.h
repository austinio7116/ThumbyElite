/*
 * ThumbyElite — particle / beam effects.
 *
 * One world-space particle pool (engine trails, explosions, sparks) plus
 * short-lived beam segments (laser shots). fx_emit_all() projects live
 * effects into the scene's point/line lists during the frame build.
 */
#ifndef R3D_FX_H
#define R3D_FX_H

#include "vec.h"
#include <stdint.h>

void fx_init(void);
void fx_tick(float dt);

void fx_spawn_explosion(Vec3 pos, Vec3 base_vel);
/* Gauss wake: twin-helix points along [prev..cur], persisting + fading.
 * traveled = total metres flown at cur (phase continuity). */
void fx_gauss_helix(Vec3 prev, Vec3 cur, Vec3 dir, float traveled);
void fx_spawn_spark(Vec3 pos, Vec3 base_vel);
/* Per-frame engine trail emission for a thrusting ship. */
void fx_engine_trail(Vec3 rear_pos, Vec3 ship_vel, float throttle, float dt);
/* A laser shot: visible for a few frames. */
void fx_beam(Vec3 from, Vec3 to, uint16_t color);

/* Project everything into the scene (camera-relative). Call between
 * r3d_scene_begin and rasterisation, on core0. cam_vel drives the
 * space-dust streaks (sense of speed). */
void fx_emit_all(Vec3 cam_pos, Vec3 cam_vel);

/* Supercruise debris: a wrapping mote field in SYSTEM (Mm) coordinates
 * whose streak length follows the cruise velocity — warp lines at full
 * tilt, drifting sparks near drop speed. */
void fx_sc_dust_emit(Vec3 cam_pos_mm, Vec3 vel_mms);

int fx_alive_count(void);

#endif
