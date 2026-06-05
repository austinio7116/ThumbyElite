/*
 * ThumbyElite — procedural ship mesh generation.
 *
 * Loft-based: a fuselage is a chamfered-octagon cross-section swept
 * through 4-6 stations (width/height/spine curve per seed), capped by
 * a raked nose apex and an engine tail. Wings, canards, fins, nacelles
 * and gun prongs attach by family rules. Every seed is a different
 * hull; the chosen seeds become the game's ship catalogue.
 */
#ifndef SHIP_GEN_H
#define SHIP_GEN_H

#include "r3d_mesh.h"
#include <stdint.h>

/* Builds into an internal static buffer (one live ship mesh at a time,
 * same contract as station_gen). Returns NULL never; mesh valid until
 * the next call. */
const Mesh *ship_gen_mesh(uint32_t seed);

#endif
