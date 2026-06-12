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
#include "r3d_planet.h"
#include "elite_types.h"
#include <string.h>

typedef struct {
    float    x0, y0, x1, y1, x2, y2;
    uint16_t d0, d1, d2;
    uint16_t color;
} SceneTri;                       /* 32 bytes */

static SceneTri s_tris[R3D_SCENE_MAX_TRIS];
static int      s_ntris;

typedef struct { float x, y; uint16_t d, color; uint8_t size; } ScenePoint;
typedef struct {
    float x0, y0, x1, y1;
    uint16_t d0, d1, color;
} SceneLine;
typedef struct { float x, y; uint16_t d, color; int16_t r; } SceneDisc;
static ScenePoint s_points[R3D_SCENE_MAX_POINTS];
static SceneLine  s_lines[R3D_SCENE_MAX_LINES];
static SceneDisc  s_discs[R3D_SCENE_MAX_DISCS];
static int        s_npoints, s_nlines, s_ndiscs;

void r3d_scene_begin(const Mat3 *cam_basis, float fov_deg) {
    r3d_pipe_set_camera(cam_basis, fov_deg);
    s_ntris = 0;
    s_npoints = 0;
    s_nlines = 0;
    s_ndiscs = 0;
}

void r3d_scene_add_disc(float sx, float sy, uint16_t d, int r_px,
                        uint16_t color) {
    if (s_ndiscs >= R3D_SCENE_MAX_DISCS) return;
    SceneDisc *p = &s_discs[s_ndiscs++];
    p->x = sx; p->y = sy; p->d = d; p->color = color;
    p->r = (int16_t)(r_px > 64 ? 64 : r_px);
}

void r3d_scene_add_point(float sx, float sy, uint16_t d, uint16_t color,
                         uint8_t size) {
    if (s_npoints >= R3D_SCENE_MAX_POINTS) return;
    ScenePoint *p = &s_points[s_npoints++];
    p->x = sx; p->y = sy; p->d = d; p->color = color; p->size = size;
}

void r3d_scene_add_line(float x0, float y0, uint16_t d0,
                        float x1, float y1, uint16_t d1, uint16_t color) {
    if (s_nlines >= R3D_SCENE_MAX_LINES) return;
    SceneLine *l = &s_lines[s_nlines++];
    l->x0 = x0; l->y0 = y0; l->d0 = d0;
    l->x1 = x1; l->y1 = y1; l->d1 = d1;
    l->color = color;
}

int r3d_scene_project(Vec3 cam_rel, float *sx, float *sy, uint16_t *d) {
    Vec3 v = m3_mul_v3_t(r3d_pipe_camera(), cam_rel);
    if (v.z <= R3D_NEAR) return 0;
    float inv_z = 1.0f / v.z;
    float focal = r3d_pipe_focal();
    *sx = 64.0f + focal * v.x * inv_z;
    *sy = 64.0f - focal * v.y * inv_z;
    float dd = R3D_DEPTH_K * inv_z;
    /* Floor at 1: Mm-scale points quantise to 0 = the sky-clear value
     * and silently lose every depth test (SC dust was invisible). */
    *d = (dd >= 65535.0f) ? 65535u : (dd < 1.0f ? 1u : (uint16_t)dd);
    return 1;
}

int r3d_scene_add_object(const R3DObject *obj) {
    return r3d_pipe_draw_object(obj);
}

int r3d_scene_add_object_scaled(const R3DObject *obj, float scale) {
    return r3d_pipe_draw_object_scaled(obj, scale);
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

/* Proposal-look switch (style lab — sheets only, live look untouched). */
static int s_style;
void r3d_scene_set_style(int s) { s_style = s; }

void r3d_starfield_init(uint32_t seed) {
    s_star_rng = seed | 1u;
    for (int i = 0; i < STAR_COUNT; i++) {
        /* Uniform direction: normalise a cube-distributed vector (good
         * enough for scenery; rejection-free). */
        Vec3 d = v3(star_frand() * 2 - 1, star_frand() * 2 - 1,
                    star_frand() * 2 - 1);
        if (v3_len2(d) < 1e-4f) d = v3(0, 0, 1);
        s_stars[i].dir = v3_norm(d);
#ifdef ELITE_STYLE_LAB
        if (s_style == 1) {
            /* PROPOSAL: real temperature tints (O/B blue .. M red) and
             * a handful of hero stars that read as foreground suns. */
            static const uint16_t k_tint[5] = {
                RGB565C(185, 205, 255),   /* blue-white */
                RGB565C(230, 235, 255),   /* white      */
                RGB565C(255, 244, 214),   /* yellow     */
                RGB565C(255, 214, 170),   /* orange     */
                RGB565C(255, 178, 150),   /* red        */
            };
            int tier = (int)(star_rand() % 10);
            uint16_t tint = k_tint[star_rand() % 5u];
            if (tier == 0) {             /* hero: bright + tinted */
                s_stars[i].color = tint;
                s_stars[i].big = 1;
            } else if (tier < 5) {
                int r = ((tint >> 11) & 31) * 24 / 31;
                int g = ((tint >> 5) & 63) * 48 / 63;
                int b = (tint & 31) * 24 / 31;
                s_stars[i].color = (uint16_t)((r << 11) | (g << 5) | b);
                s_stars[i].big = 0;
            } else {
                int r = ((tint >> 11) & 31) * 13 / 31;
                int g = ((tint >> 5) & 63) * 26 / 63;
                int b = (tint & 31) * 14 / 31;
                s_stars[i].color = (uint16_t)((r << 11) | (g << 5) | b);
                s_stars[i].big = 0;
            }
            continue;
        }
#endif
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

/* Physical-space band (y0p..y1p in R3D pixels). Stars keep their device
 * apparent size (R3D_SS x R3D_SS blocks) but gain subpixel placement. */
static void starfield_raster(uint16_t *fb, int y0p, int y1p) {
    const Mat3 *cam = r3d_pipe_camera();
    const float focal = r3d_pipe_focal();
    for (int i = 0; i < STAR_COUNT; i++) {
        Vec3 v = m3_mul_v3_t(cam, s_stars[i].dir);
        if (v.z < 0.05f) continue;
        float inv_z = 1.0f / v.z;
        int sx = (int)((64.0f + focal * v.x * inv_z) * R3D_SS);
        int sy = (int)((64.0f - focal * v.y * inv_z) * R3D_SS);
        if (sx < 0 || sy + R3D_SS * 2 <= y0p || sy >= y1p) continue;
        uint16_t c = s_stars[i].color;
        /* big: 2x1 + 1 below (device pattern), scaled. */
        int w = s_stars[i].big ? R3D_SS * 2 : R3D_SS;
        int h = R3D_SS;
        for (int pass = 0; pass < (s_stars[i].big ? 2 : 1); pass++) {
            for (int dy = 0; dy < h; dy++) {
                int py = sy + pass * R3D_SS + dy;
                if (py < y0p || py >= y1p) continue;
                int pw = pass ? R3D_SS : w;       /* second row: 1 block */
                for (int dx = 0; dx < pw; dx++) {
                    int px = sx + dx;
                    if ((unsigned)px >= R3D_FB_W) continue;
                    fb[py * R3D_FB_W + px] = c;
                }
            }
        }
    }
}

/* Optional nebula background (title) — the galaxy chart's blue/red value-noise
 * wash rendered behind the stars. 0 = plain black (flight). */
static uint32_t s_nebula;
static float    s_neb_str;          /* 0 = off, up to ~1 = thick */
void r3d_scene_set_nebula(uint32_t seed, float strength) {
    s_nebula = seed; s_neb_str = strength;
}
static uint16_t s_icon_bg;          /* nonzero = flat key bg (icon render) */
void r3d_scene_set_icon_bg(uint16_t c) { s_icon_bg = c; }
static uint32_t nb_hash(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16);
}
static float nb_noise(float x, float y) {
    int xi = (int)x, yi = (int)y; if (x < 0) xi--; if (y < 0) yi--;
    float fx = x - xi, fy = y - yi;
    float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    float v00 = (nb_hash(xi, yi)     & 0xFFFF) * (1.0f / 65535.0f);
    float v10 = (nb_hash(xi + 1, yi) & 0xFFFF) * (1.0f / 65535.0f);
    float v01 = (nb_hash(xi, yi + 1) & 0xFFFF) * (1.0f / 65535.0f);
    float v11 = (nb_hash(xi + 1, yi + 1) & 0xFFFF) * (1.0f / 65535.0f);
    float a = v00 + (v10 - v00) * sx, b = v01 + (v11 - v01) * sx;
    return a + (b - a) * sy;
}
/* Direction-based so the wash is fixed in space and rotates with the view
 * (sampled along each pixel's world ray), in coarse blocks to stay cheap. */
static void nebula_fill(uint16_t *fb, int y0p, int y1p) {
    const Mat3 *cam = r3d_pipe_camera();
    float focal = r3d_pipe_focal() * (float)R3D_SS;
    float cx = 64.0f * R3D_SS, cy = 64.0f * R3D_SS;
    float ox = (float)(s_nebula & 0xFF) * 0.6f;
    int STEP = 2 * R3D_SS;
    for (int y = y0p; y < y1p; y += STEP) {
        float vy = -((float)y + 0.5f * STEP - cy) / focal;
        for (int x = 0; x < R3D_FB_W; x += STEP) {
            float vx = ((float)x + 0.5f * STEP - cx) / focal;
            Vec3 d = v3_norm(m3_mul_v3(cam, v3(vx, vy, 1.0f)));
            const float F = 1.2f;          /* low freq = large clouds */
            float n = nb_noise(d.x * F + 40.0f + ox, d.y * F + 40.0f) * 0.5f +
                      nb_noise(d.z * F + 17.0f, d.x * F) * 0.3f +
                      nb_noise(d.y * F + 7.0f,  d.z * F + 23.0f) * 0.2f;
            uint16_t c = 0;
#ifdef ELITE_STYLE_LAB
            if (s_style == 1) {
                /* PROPOSAL: two-layer nebula (broad wash + bright cores
                 * in a second hue) over a faint galactic band. */
                int r = 0, g = 0, b = 0;
                float band = 1.0f - (d.y < 0 ? -d.y : d.y) * 2.6f;
                if (band > 0) {            /* the milky stripe */
                    float bb = band * band * 0.9f;
                    float gr = nb_noise(d.x * 5.0f + 3.0f, d.z * 5.0f);
                    bb *= 0.55f + 0.45f * gr;
                    r += (int)(bb * 5); g += (int)(bb * 9); b += (int)(bb * 6);
                }
                if (n > 0.50f) {
                    float k = (n - 0.50f) * 2.6f * s_neb_str;
                    if (k > 1.0f) k = 1.0f;
                    float w = nb_noise(d.x * F + 77.0f, d.z * F - 19.0f);
                    float core = nb_noise(d.x * F * 2.3f - 9.0f,
                                          d.y * F * 2.3f + 31.0f);
                    if (w > 0.55f) { r += (int)(k * 13); g += (int)(k * 4);
                                     b += (int)(k * 8); }
                    else           { r += (int)(k * 4);  g += (int)(k * 6);
                                     b += (int)(k * 16); }
                    if (core > 0.62f) {    /* bright knots inside clouds */
                        float kk = k * (core - 0.62f) * 2.6f;
                        r += (int)(kk * 10); g += (int)(kk * 9);
                        b += (int)(kk * 12);
                    }
                }
                if (r > 31) r = 31;
                if (g > 63) g = 63;
                if (b > 31) b = 31;
                c = (uint16_t)((r << 11) | (g << 5) | b);
                goto neb_fill;
            }
#endif
            if (n > 0.52f) {               /* patchy, but visible where it is */
                float k = (n - 0.52f) * 2.4f * s_neb_str; if (k > 1.0f) k = 1.0f;
                float w = nb_noise(d.x * F + 77.0f, d.z * F - 19.0f);
                int r = (int)(k * (w > 0.55f ? 12 : 5));   /* red patches in a blue wash */
                int g = (int)(k * 4);
                int b = (int)(k * (w > 0.55f ? 9 : 15));
                c = (uint16_t)(((r & 31) << 11) | ((g & 63) << 5) | (b & 31));
            }
#ifdef ELITE_STYLE_LAB
neb_fill:;
#endif
            for (int yy = y; yy < y + STEP && yy < y1p; yy++)
                for (int xx = x; xx < x + STEP && xx < R3D_FB_W; xx++)
                    fb[yy * R3D_FB_W + xx] = c;
        }
    }
}

void r3d_scene_raster(uint16_t *fb, int y0, int y1) {
    if (y0 < 0) y0 = 0;
    if (y1 > ELITE_FB_H) y1 = ELITE_FB_H;
    if (y0 >= y1) return;
    /* Callers pass logical rows; everything below runs physical. */
    int y0p = y0 * R3D_SS, y1p = y1 * R3D_SS;

    if (s_icon_bg) {                     /* icon render: flat key colour, no sky */
        for (int y = y0p; y < y1p; y++) {
            uint16_t *row = fb + y * R3D_FB_W;
            for (int x = 0; x < R3D_FB_W; x++) row[x] = s_icon_bg;
        }
        r3d_raster_set_fb(fb);
        r3d_depth_clear(y0p, y1p);
    } else {
        if (s_nebula && s_neb_str > 0.01f) nebula_fill(fb, y0p, y1p);
        else memset(fb + y0p * R3D_FB_W, 0,
                    (size_t)(y1p - y0p) * R3D_FB_W * sizeof(uint16_t));
        r3d_raster_set_fb(fb);
        r3d_depth_clear(y0p, y1p);
        starfield_raster(fb, y0p, y1p);
        r3d_planet_raster(fb, y0p, y1p); /* writes depth: ships pass behind */
    }

    const float SS = (float)R3D_SS;
    int n = s_ntris;
    for (int i = 0; i < n; i++) {
        const SceneTri *t = &s_tris[i];
        r3d_tri(t->x0 * SS, t->y0 * SS, t->d0, t->x1 * SS, t->y1 * SS, t->d1,
                t->x2 * SS, t->y2 * SS, t->d2, t->color, y0p, y1p);
    }

    /* FX pass: depth-tested, no depth write — ships occlude them.
     * Discs first (fireballs), so sparks/debris draw over them. */
    for (int i = 0; i < s_ndiscs; i++) {
        const SceneDisc *p = &s_discs[i];
        r3d_disc((int)(p->x * SS), (int)(p->y * SS), p->d,
                 p->r * R3D_SS, p->color, y0p, y1p);
    }
    for (int i = 0; i < s_npoints; i++) {
        const ScenePoint *p = &s_points[i];
        r3d_point((int)(p->x * SS), (int)(p->y * SS), p->d, p->color,
                  p->size * R3D_SS, y0p, y1p);
    }
    for (int i = 0; i < s_nlines; i++) {
        const SceneLine *l = &s_lines[i];
        r3d_line(l->x0 * SS, l->y0 * SS, l->d0,
                 l->x1 * SS, l->y1 * SS, l->d1, l->color, y0p, y1p);
    }
}
