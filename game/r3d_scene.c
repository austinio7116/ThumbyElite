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

/* ADOPTED 2026-06-12: space v3 (dithered nebula, galactic band, star
 * tints, hero halos) is the live look. Setter kept for harness compat;
 * style 0 still selects the old sky for archival sheet comparisons. */
static int s_style = 1;
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
        if (s_style == 1) {
            /* PROPOSAL v3: same brightness ladder as live, but each
             * star leans gently warm or cool — variation you only
             * notice when you look for it. */
            int tier = (int)(star_rand() % 8);
            int warm = (int)(star_rand() % 3);     /* 0 cool 1 wt 2 warm */
            if (tier == 0) {
                s_stars[i].color = warm == 2 ? RGB565C(255, 238, 205)
                                 : warm == 0 ? RGB565C(208, 226, 255)
                                             : RGB565C(240, 240, 245);
                s_stars[i].big = 1;
            } else if (tier < 4) {
                s_stars[i].color = warm == 2 ? RGB565C(198, 188, 172)
                                 : warm == 0 ? RGB565C(172, 186, 205)
                                             : RGB565C(188, 188, 196);
                s_stars[i].big = 0;
            } else {
                s_stars[i].color = warm == 2 ? RGB565C(118, 110, 100)
                                 : warm == 0 ? RGB565C(100, 110, 128)
                                             : RGB565C(108, 108, 122);
                s_stars[i].big = 0;
            }
            continue;
        }
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
        if (s_style == 1 && s_stars[i].big) {
            /* one-pixel additive halo ring around the hero stars —
             * a faint glow, not a sprite */
            static const int hdx[4] = { -1, 2, 0, 1 };
            static const int hdy[4] = { 0, 0, -1, 2 };
            for (int hk = 0; hk < 4; hk++) {
                int px = sx + hdx[hk] * R3D_SS;
                int py = sy + hdy[hk] * R3D_SS;
                for (int by2 = 0; by2 < R3D_SS; by2++)
                    for (int bx2 = 0; bx2 < R3D_SS; bx2++) {
                        int qx = px + bx2, qy = py + by2;
                        if ((unsigned)qx >= R3D_FB_W) continue;
                        if (qy < y0p || qy >= y1p) continue;
                        uint16_t *fp = &fb[qy * R3D_FB_W + qx];
                        int r = ((*fp >> 11) & 31) + (((c >> 11) & 31) >> 2);
                        int g = ((*fp >> 5) & 63) + (((c >> 5) & 63) >> 2);
                        int b = (*fp & 31) + ((c & 31) >> 2);
                        if (r > 31) r = 31;
                        if (g > 63) g = 63;
                        if (b > 31) b = 31;
                        *fp = (uint16_t)((r << 11) | (g << 5) | b);
                    }
            }
        }
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
/* v3 nebula (ADOPTED): the failure modes of the first attempt were mud (too bright,
 * too busy) and the look of the renderer itself — 2x2 blocks and raw
 * RGB565 banding at low levels. Fixes, in order of importance:
 *   1. Bayer-dithered output: float colour + 4x4 ordered threshold
 *      before truncation — smooth gradients at 5-bit depth.
 *   2. Per-pixel bilinear interpolation between block-corner noise
 *      samples — no visible block structure, same sample budget.
 *   3. Restraint: peak channel ~45% of the old v2, soft quadratic
 *      onset, ONE hue pair (deep indigo wash, dusty rose where a slow
 *      field says so), no filaments, no galactic band.
 * Cost stays near the live path (4 corner samples per 4x4 block). */
/* unstructured grain dither — a hash, not a bayer lattice (the ordered
 * matrix read as a regular dot grid over large faint areas) */
static inline float nb_grain(int x, int y) {
    uint32_t h = (uint32_t)x * 0x9E3779B9u ^ (uint32_t)y * 0x85EBCA6Bu;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
    return (float)(h & 0xFF) * (1.0f / 256.0f);
}
static void nebula_fill_v3(uint16_t *fb, int y0p, int y1p) {
    const Mat3 *cam = r3d_pipe_camera();
    float focal = r3d_pipe_focal() * (float)R3D_SS;
    float cx = 64.0f * R3D_SS, cy = 64.0f * R3D_SS;
    float ox = (float)(s_nebula & 0xFF) * 0.6f;
    const float F = 1.05f;
    const int STEP = 4 * R3D_SS;
    /* Galactic band: a great circle of unresolved starlight with REAL
     * per-system character (user: 'too samey — the same line all the
     * way round'). The seed picks the plane's full orientation, the
     * band's width and gain (some skies barely show it), and the
     * direction of the CORE — a bright wide bulge at one point on the
     * circle, like the Milky Way toward Sagittarius. */
    Vec3 gax, gcore;
    float gwk, ggain;
    {
        uint32_t gh = (s_nebula | 1u) * 0x45D9F3Bu;
        gh ^= gh >> 13; gh *= 0x2C1B3C6Du; gh ^= gh >> 15;
        float a1 = (float)(gh & 0x3FF) * (6.2831853f / 1024.0f);
        float z1 = (float)((gh >> 10) & 0xFF) * (2.0f / 255.0f) - 1.0f;
        float r1 = sqrtf(1.0f - z1 * z1);
        gax = v3(r1 * cosf(a1), z1, r1 * sinf(a1));   /* full sphere */
        gwk = 3.2f + (float)((gh >> 18) & 7) * 0.5f;  /* width 3.2-6.7 */
        ggain = 0.45f + (float)((gh >> 21) & 7) * 0.13f; /* 0.45-1.36 */
        /* core: a seeded azimuth on the band plane */
        Vec3 ref = (gax.y > 0.9f || gax.y < -0.9f) ? v3(1, 0, 0)
                                                   : v3(0, 1, 0);
        Vec3 c1 = v3_norm(v3_cross(gax, ref));
        Vec3 c2 = v3_cross(gax, c1);
        float az = (float)((gh >> 24) & 0xFF) * (6.2831853f / 256.0f);
        gcore = v3_add(v3_scale(c1, cosf(az)), v3_scale(c2, sinf(az)));
    }
    /* corner sample of the cloud field (intensity) + hue field */
    #define NB_V3(px, py, out_n, out_w, out_g, out_h) do { \
        float vx_ = ((float)(px) - cx) / focal; \
        float vy_ = -((float)(py) - cy) / focal; \
        Vec3 d_ = v3_norm(m3_mul_v3(cam, v3(vx_, vy_, 1.0f))); \
        (out_n) = nb_noise(d_.x * F + 40.0f + ox, d_.y * F + 40.0f) * 0.5f + \
                  nb_noise(d_.z * F + 17.0f, d_.x * F) * 0.3f + \
                  nb_noise(d_.y * F * 2.1f + 7.0f, d_.z * F * 2.1f + 23.0f) \
                      * 0.2f; \
        (out_w) = nb_noise(d_.x * 0.7f + 77.0f + ox * 0.3f, \
                           d_.z * 0.7f - 19.0f); \
        float gc_ = d_.x * gax.x + d_.y * gax.y + d_.z * gax.z; \
        float ac_ = gc_ < 0 ? -gc_ : gc_; \
        /* core bulge: the band widens AND brightens toward gcore */ \
        float cb_ = d_.x * gcore.x + d_.y * gcore.y + d_.z * gcore.z; \
        if (cb_ < 0) cb_ = 0; \
        cb_ = cb_ * cb_; cb_ *= cb_;                  /* ^4 falloff */ \
        float gb_ = 1.0f - ac_ * gwk / (1.0f + 1.8f * cb_); \
        if (gb_ > 0) { \
            gb_ *= gb_; \
            float ridge_ = 1.0f - ac_ * gwk * 2.6f; \
            if (ridge_ > 0) gb_ += ridge_ * ridge_ * ridge_ * 0.5f; \
            gb_ *= 1.0f + 2.6f * cb_;                 /* core glow */ \
            gb_ *= ggain * (0.55f + 0.45f * \
                            nb_noise(d_.x * 2.6f + 5.0f, \
                                     d_.z * 2.6f - 11.0f)); \
        } else gb_ = 0; \
        (out_g) = gb_; \
        /* hue lean: slow field, most of the band stays neutral */ \
        (out_h) = nb_noise(d_.x * 0.9f - 31.0f, d_.z * 0.9f + 53.0f); \
    } while (0)
    for (int y = y0p; y < y1p; y += STEP) {
        for (int x = 0; x < R3D_FB_W; x += STEP) {
            float n00, w00, g00, h00, n10, w10, g10, h10;
            float n01, w01, g01, h01, n11, w11, g11, h11;
            NB_V3(x, y, n00, w00, g00, h00);
            NB_V3(x + STEP, y, n10, w10, g10, h10);
            NB_V3(x, y + STEP, n01, w01, g01, h01);
            NB_V3(x + STEP, y + STEP, n11, w11, g11, h11);
            /* skip fully-dark blocks outright (most of the sky) */
            float nmax = n00;
            if (n10 > nmax) nmax = n10;
            if (n01 > nmax) nmax = n01;
            if (n11 > nmax) nmax = n11;
            float gmax = g00;
            if (g10 > gmax) gmax = g10;
            if (g01 > gmax) gmax = g01;
            if (g11 > gmax) gmax = g11;
            int ylim = y + STEP < y1p ? y + STEP : y1p;
            if (nmax <= 0.48f && gmax <= 0.012f) {
                for (int yy = y; yy < ylim; yy++) {
                    uint16_t *row = fb + yy * R3D_FB_W;
                    for (int xx = x; xx < x + STEP && xx < R3D_FB_W; xx++)
                        row[xx] = 0;
                }
                continue;
            }
            float inv = 1.0f / (float)STEP;
            for (int yy = y; yy < ylim; yy++) {
                float ty = (float)(yy - y) * inv;
                float na = n00 + (n01 - n00) * ty;
                float nb = n10 + (n11 - n10) * ty;
                float wa = w00 + (w01 - w00) * ty;
                float wb = w10 + (w11 - w10) * ty;
                float ga = g00 + (g01 - g00) * ty;
                float gb2 = g10 + (g11 - g10) * ty;
                float ha = h00 + (h01 - h00) * ty;
                float hb2 = h10 + (h11 - h10) * ty;
                uint16_t *row = fb + yy * R3D_FB_W;
                for (int xx = x; xx < x + STEP && xx < R3D_FB_W; xx++) {
                    float tx = (float)(xx - x) * inv;
                    float n = na + (nb - na) * tx;
                    float gband = ga + (gb2 - ga) * tx;
                    /* the band: warm-white starlight, per-pixel grain
                     * sparkle so it reads as unresolved stars, peak
                     * ~12%% channel — DISTANT */
                    float gr = 0, gg = 0, gbl = 0;
                    if (gband > 0.012f) {
                        float sp = 0.40f + 1.05f * nb_grain(xx + 311, yy + 97);
                        float gk = gband * sp;
                        /* hue lean (user req): mostly neutral starlight,
                         * but regions drift warm-gold or dusty rose */
                        float hue = ha + (hb2 - ha) * tx;
                        float warm = (hue - 0.62f) * 4.0f;
                        float cool = (0.38f - hue) * 4.0f;
                        if (warm < 0) warm = 0;
                        if (warm > 1) warm = 1;
                        if (cool < 0) cool = 0;
                        if (cool > 1) cool = 1;
                        gr = gk * (2.7f + 1.3f * warm + 0.9f * cool);
                        gg = gk * (5.2f + 1.2f * warm);
                        gbl = gk * (2.5f - 0.9f * warm + 2.4f * cool);
                    }
                    if (n <= 0.48f) {
                        if (gband <= 0.012f) { row[xx] = 0; continue; }
                        float dth0 = nb_grain(xx, yy);
                        int r0 = (int)(gr + dth0);
                        int g0 = (int)(gg + dth0);
                        int b0 = (int)(gbl + dth0);
                        if (r0 > 31) r0 = 31;
                        if (g0 > 63) g0 = 63;
                        if (b0 > 31) b0 = 31;
                        row[xx] = (uint16_t)((r0 << 11) | (g0 << 5) | b0);
                        continue;
                    }
                    float w = wa + (wb - wa) * tx;
                    float k = (n - 0.48f) * 3.4f * s_neb_str;
                    if (k > 1.0f) k = 1.0f;
                    k = k * k * (3.0f - 2.0f * k); /* smoothstep onset */
                    /* indigo wash <-> dusty rose, gently */
                    float rose = (w - 0.45f) * 3.0f;
                    if (rose < 0) rose = 0;
                    if (rose > 1) rose = 1;
                    float fr = k * (3.2f + 7.8f * rose) + gr;
                    float fg = k * (2.3f + 1.4f * rose) + gg;
                    float fbl = k * (12.3f - 5.5f * rose) + gbl;
                    float dth = nb_grain(xx, yy);
                    int r = (int)(fr + dth);
                    int g = (int)(fg + dth);
                    int b = (int)(fbl + dth);
                    if (r > 31) r = 31;
                    if (g > 63) g = 63;
                    if (b > 31) b = 31;
                    row[xx] = (uint16_t)((r << 11) | (g << 5) | b);
                }
            }
        }
    }
    #undef NB_V3
}

static void nebula_fill(uint16_t *fb, int y0p, int y1p) {
    if (s_style == 1) { nebula_fill_v3(fb, y0p, y1p); return; }
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
            if (n > 0.52f) {               /* patchy, but visible where it is */
                float k = (n - 0.52f) * 2.4f * s_neb_str; if (k > 1.0f) k = 1.0f;
                float w = nb_noise(d.x * F + 77.0f, d.z * F - 19.0f);
                int r = (int)(k * (w > 0.55f ? 12 : 5));   /* red patches in a blue wash */
                int g = (int)(k * 4);
                int b = (int)(k * (w > 0.55f ? 9 : 15));
                c = (uint16_t)(((r & 31) << 11) | ((g & 63) << 5) | (b & 31));
            }
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
        if (s_style == 1) nebula_fill(fb, y0p, y1p);   /* band always on */
        else if (s_nebula && s_neb_str > 0.01f) nebula_fill(fb, y0p, y1p);
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
