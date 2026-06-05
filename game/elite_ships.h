/*
 * ThumbyElite — purchasable hull catalogue.
 *
 * Size/cost gates everything (user requirement): weapon slot count AND
 * per-slot max size, cargo capacity, and the highest shield/hull upgrade
 * tier the frame can take. Combat hulls fly better; freight hulls carry.
 */
#ifndef ELITE_SHIPS_H
#define ELITE_SHIPS_H

#include "r3d_mesh.h"
#include <stdint.h>

#define N_HULLS 10
#define HULL_SLOTS 3            /* hardpoint array size (some unused) */

typedef struct {
    const char *name;
    const Mesh *mesh;
    int32_t price;
    uint8_t n_slots;
    uint8_t slot_size[HULL_SLOTS];  /* max weapon size per mount, 0=none */
    uint8_t cargo;
    uint8_t max_shield_tier;        /* 0..3 */
    uint8_t max_hull_tier;          /* 0..3 */
    float max_speed, accel, turn_rate;
    float hull_base, shield_base;
    float jump_range;               /* light-years per hop */
    uint8_t rack;                   /* salvage workbench slots (<=8) */
    uint8_t util_slots;             /* gadget bays (1; big iron 2) */
    uint8_t has_turret;             /* auto-turret hardpoint (Z1) */
} HullDef;

extern const HullDef k_hulls[N_HULLS];   /* class STAT templates; the
                                          * look comes from a mesh seed */

/* Procedural hull mesh cache: ships are (class, mesh_seed) pairs now.
 * Entries persist until hull_cache_reset (anchor changes), which keeps
 * any mesh still referenced by the live player ship. */
const Mesh *hull_mesh(uint32_t mesh_seed, int class_hint);
void hull_cache_reset(const Mesh *keep);

/* Upgrade tiers: multiplier + price scale with the hull price. */
extern const float k_tier_mult[4];      /* 1.0 / 1.3 / 1.6 / 2.0 */
int upgrade_price(int hull_id, int tier);

#endif
