/*
 * ThumbyElite — 3D geometry pipeline.
 *
 * model -> world -> view -> near-clip -> project -> r3d_tri.
 *
 * CAMERA-RELATIVE WORLD: the camera is always the origin. Callers pass
 * object positions already relative to the camera (game code subtracts the
 * camera's logical position before drawing). This keeps every float the
 * pipe touches small even though systems span millions of km.
 *
 * View space: x right, y up, z forward (depth). Projection flips y for
 * the y-down screen. Depth buffer value d = R3D_DEPTH_K / z (larger =
 * nearer), matching r3d_raster's convention.
 */
#ifndef R3D_PIPE_H
#define R3D_PIPE_H

#include "vec.h"
#include "r3d_mesh.h"
#include <stdint.h>

#define R3D_NEAR     0.5f                      /* near plane, meters */
#define R3D_DEPTH_K  (65535.0f * R3D_NEAR)     /* d=65535 at z=NEAR */
#define R3D_MAX_VERTS 320                      /* largest mesh (station) */

typedef struct {
    Vec3 pos;          /* camera-relative world position */
    Mat3 basis;        /* object orientation (rows: right/up/forward) */
    const Mesh *mesh;
} R3DObject;

void r3d_pipe_set_camera(const Mat3 *cam_basis, float fov_deg);
void r3d_pipe_set_sun(Vec3 dir_toward_light_world);

/* Transform, light, clip, project and rasterise one object. y_min/y_max
 * bound the rasterised rows (dual-core screen-half split). Returns the
 * number of triangles actually rasterised (for budget profiling). */
int r3d_pipe_draw_object(const R3DObject *obj, int y_min, int y_max);

#endif
