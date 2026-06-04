/*
 * ThumbyElite — weapons, damage, death (Phase 3: hitscan pulse laser).
 */
#ifndef ELITE_COMBAT_H
#define ELITE_COMBAT_H

#include "elite_entity.h"
#include <stdbool.h>

void combat_init(void);
void combat_tick(float dt);

/* Fire shooter's laser along its nose (with optional aim error in
 * radians). Returns the entity hit, or -1. */
int combat_fire_laser(int shooter, float spread);

/* Heat / cooldown housekeeping is in combat_tick; this just asks. */
bool combat_can_fire(const Ship *s);

/* Stats for HUD/scoring. */
int combat_kills(void);

/* HUD hit feedback: seconds remaining on the hit / kill markers
 * (set when a PLAYER shot connects / destroys). */
float combat_hitmarker(void);
float combat_killmarker(void);

#endif
