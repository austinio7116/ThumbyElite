/*
 * ThumbyElite — scene draw-list + starfield + band rasterisation.
 *
 * core0 fills the draw-list (via r3d_pipe -> r3d_emit_tri); then both
 * cores call r3d_scene_raster with disjoint row bands. Per band: clear
 * colour + depth, plot the starfield, rasterise every listed triangle
 * (r3d_tri clamps rows to the band). Triangles overwrite stars, so star
 * occlusion is free.
 */
#include "r3d_scene.h"
#include "r3d_raster.h"
#include "elite_types.h"
#include <string.h>

typedef struct {
    float    x0, y0, x1, y1, x2, y2;
    uint16_t d0, d1, d2;
    uint16_t color;
} SceneTri;                       /* 32 bytes */

static SceneTri s_tris[R3D_SCENE_MAX_TRIS];
static int      s_ntris;

void r3d_scene_begin(const Mat3 *cam_basis, float fov_deg) {
    r3d_pipe_set_camera(cam_basis, fov_deg);
    s_ntris = 0;
}

int r3d_scene_add_object(const R3DObject *obj) {
    return r3d_pipe_draw_object(obj);
}

int r3d_scene_tri_count(void) { return s_ntris; }

void r3d_emit_tri(float ax, float ay, uint16_t az,
                  float bx, float by, uint16_t bz,
                  float cx, float cy, uint16_t cz, uint16_t color) {
    if (s_ntris >= R3D_SCENE_MAX_TRIS) return;
    SceneTri *t = &s_tris[s_ntris++];
    t->x0 = ax; t->y0 = ay; t->d0 = az;
    t->x1 = bx; t->y1 = by; t->d1 = bz;
    t->x2 = cx; t->y2 = cy; t->d2 = cz;
    t->color = color;
}

/* --- Starfield ---------------------------------------------------------
 * Fixed unit directions at infinity: rotate by the camera transpose,
 * project, plot. Rotation-only parallax falls out naturally. */
#define STAR_COUNT 120
typedef struct { Vec3 dir; uint16_t color; uint8_t big; } Star;
static Star s_stars[STAR_COUNT];

static uint32_t s_star_rng;
static uint32_t star_rand(void) {
    s_star_rng ^= s_star_rng << 13;
    s_star_rng ^= s_star_rng >> 17;
    s_star_rng ^= s_star_rng << 5;
    return s_star_rng;
}
static float star_frand(void) {
    return (float)(star_rand() & 0xFFFF) * (1.0f / 65535.0f);
}

void r3d_starfield_init(uint32_t seed) {
    s_star_rng = seed | 1u;
    for (int i = 0; i < STAR_COUNT; i++) {
        /* Uniform direction: normalise a cube-distributed vector (good
         * enough for scenery; rejection-free). */
        Vec3 d = v3(star_frand() * 2 - 1, star_frand() * 2 - 1,
                    star_frand() * 2 - 1);
        if (v3_len2(d) < 1e-4f) d = v3(0, 0, 1);
        s_stars[i].dir = v3_norm(d);
        int tier = (int)(star_rand() % 8);
        if (tier == 0) {            /* bright, slightly tinted */
            uint8_t warm = (uint8_t)(star_rand() & 1);
            s_stars[i].color = warm ? RGB565C(255, 240, 210)
                                    : RGB565C(215, 230, 255);
            s_stars[i].big = 1;
        } else if (tier < 4) {
            s_stars[i].color = RGB565C(190, 190, 200);
            s_stars[i].big = 0;
        } else {
            s_stars[i].color = RGB565C(110, 110, 125);
            s_stars[i].big = 0;
        }
    }
}

static void starfield_raster(uint16_t *fb, int y0, int y1) {
    const Mat3 *cam = r3d_pipe_camera();
    const float focal = r3d_pipe_focal();
    for (int i = 0; i < STAR_COUNT; i++) {
        Vec3 v = m3_mul_v3_t(cam, s_stars[i].dir);
        if (v.z < 0.05f) continue;
        float inv_z = 1.0f / v.z;
        int sx = (int)(64.0f + focal * v.x * inv_z);
        int sy = (int)(64.0f - focal * v.y * inv_z);
        if ((unsigned)sx >= ELITE_FB_W) continue;
        if (sy < y0 || sy >= y1) {
            /* A big star's second row may still land in-band. */
            if (!(s_stars[i].big && sy + 1 >= y0 && sy + 1 < y1)) continue;
        }
        uint16_t c = s_stars[i].color;
        if (sy >= y0 && sy < y1) fb[sy * ELITE_FB_W + sx] = c;
        if (s_stars[i].big) {
            if (sx + 1 < ELITE_FB_W && sy >= y0 && sy < y1)
                fb[sy * ELITE_FB_W + sx + 1] = c;
            if (sy + 1 >= y0 && sy + 1 < y1)
                fb[(sy + 1) * ELITE_FB_W + sx] = c;
        }
    }
}

void r3d_scene_raster(uint16_t *fb, int y0, int y1) {
    if (y0 < 0) y0 = 0;
    if (y1 > ELITE_FB_H) y1 = ELITE_FB_H;
    if (y0 >= y1) return;

    memset(fb + y0 * ELITE_FB_W, 0,
           (size_t)(y1 - y0) * ELITE_FB_W * sizeof(uint16_t));
    r3d_raster_set_fb(fb);
    r3d_depth_clear(y0, y1);
    starfield_raster(fb, y0, y1);

    int n = s_ntris;
    for (int i = 0; i < n; i++) {
        const SceneTri *t = &s_tris[i];
        r3d_tri(t->x0, t->y0, t->d0, t->x1, t->y1, t->d1,
                t->x2, t->y2, t->d2, t->color, y0, y1);
    }
}
