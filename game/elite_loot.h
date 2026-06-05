/*
 * ThumbyElite — wreck salvage: canisters, scooping.
 */
#ifndef ELITE_LOOT_H
#define ELITE_LOOT_H

#include "vec.h"
#include <stdint.h>

void loot_init(void);
/* Roll a drop at a kill site (tier raises component odds/quality). */
void loot_on_kill(Vec3 pos, Vec3 vel, int tier);
/* Tumble + scoop check. Returns a toast string for this frame or NULL. */
const char *loot_tick(float dt);
/* Add live canisters to the scene (camera-relative). */
void loot_render(Vec3 cam_pos);

#endif
