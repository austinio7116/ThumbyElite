/*
 * ThumbyElite — weapons fire, damage, death.
 */
#ifndef ELITE_COMBAT_H
#define ELITE_COMBAT_H

#include "elite_entity.h"
#include "vec.h"
#include <stdbool.h>

void combat_init(void);
void combat_tick(float dt);

/* Fire the shooter's ACTIVE weapon along its nose (+- spread radians).
 * target feeds homing seekers (-1 = none -> dumbfire). Returns the
 * entity hit for hitscan weapons, else -1. */
int combat_fire(int shooter, float spread, int target);

bool combat_can_fire(const Ship *s);

/* Damage entry points (also used by the projectile pool). */
void combat_direct_damage(int shooter, int victim, float dmg, Vec3 hit_pos);
void combat_explosion_damage(int shooter, Vec3 centre, float radius,
                             float dmg);

/* Stats / HUD feedback. */
int   combat_kills(void);
float combat_hitmarker(void);
float combat_killmarker(void);

#endif
