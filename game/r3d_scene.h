/*
 * ThumbyElite — scene draw-list + dual-core rasterisation.
 *
 * Frame flow:
 *   core0:  r3d_scene_begin(cam, fov)
 *           r3d_scene_add_object(...) x N     (transform/clip/shade -> list)
 *           ... then BOTH cores:
 *   coreX:  r3d_scene_raster(fb, y0, y1)      (background + stars + tris)
 *
 * The draw-list holds final screen-space triangles, so rasterisation is
 * embarrassingly parallel: each core walks the whole list clamped to its
 * own row band — disjoint pixels, no locks, no atomics.
 */
#ifndef R3D_SCENE_H
#define R3D_SCENE_H

#include "r3d_pipe.h"
#include <stdint.h>

#define R3D_SCENE_MAX_TRIS 512

void r3d_scene_begin(const Mat3 *cam_basis, float fov_deg);

/* Returns triangles added (0 if culled or the list is full). */
int r3d_scene_add_object(const R3DObject *obj);

/* Rasterise rows [y0, y1): clears that band of fb + depth, draws the
 * starfield, then every listed triangle clamped to the band. Safe to call
 * concurrently from both cores with disjoint bands. */
void r3d_scene_raster(uint16_t *fb, int y0, int y1);

int r3d_scene_tri_count(void);

/* Starfield: regenerate the fixed direction table (e.g. on system entry). */
void r3d_starfield_init(uint32_t seed);

#endif
