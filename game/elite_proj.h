/*
 * ThumbyElite — projectile pool (photons, slugs, tracers, missiles).
 */
#ifndef ELITE_PROJ_H
#define ELITE_PROJ_H

#include "vec.h"
#include "elite_weapons.h"
#include <stdint.h>

void proj_init(void);
void proj_spawn(WeaponType type, int owner, int8_t target,
                Vec3 pos, Vec3 dir, Vec3 inherit_vel);
void proj_tick(float dt);
/* Project live rounds into the scene (camera-relative). */
void proj_emit(Vec3 cam_pos);
int  proj_count(void);

#endif
