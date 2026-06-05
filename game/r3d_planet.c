/*
 * ThumbyElite — planet / sun impostor renderer.
 */
#include "r3d_planet.h"
#include "r3d_pipe.h"
#include "r3d_raster.h"
#include "system_sim.h"
#include "elite_types.h"
#include <math.h>
#include <string.h>

#define TEX_N 32                  /* per-planet tile is TEX_N x TEX_N */
#define PAL_N 8

typedef struct {
    uint8_t  tex[TEX_N * TEX_N];  /* palette indices */
    uint16_t pal[PAL_N];
} PlanetArt;

static PlanetArt s_art[GAL_MAX_PLANETS];
static const SystemInfo *s_info;

/* --- bake-time noise (value noise, ThumbyCraft lineage) ----------------*/
static uint32_t phash(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float pnoise(float x, float y, uint32_t seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    float v00 = (phash(xi, yi, seed) & 0xFFFF) * (1.0f / 65535.0f);
    float v10 = (phash(xi + 1, yi, seed) & 0xFFFF) * (1.0f / 65535.0f);
    float v01 = (phash(xi, yi + 1, seed) & 0xFFFF) * (1.0f / 65535.0f);
    float v11 = (phash(xi + 1, yi + 1, seed) & 0xFFFF) * (1.0f / 65535.0f);
    float a = v00 + (v10 - v00) * sx, b = v01 + (v11 - v01) * sx;
    return a + (b - a) * sy;
}
static float fbm(float x, float y, uint32_t seed) {
    return 0.55f * pnoise(x, y, seed) +
           0.30f * pnoise(x * 2.1f, y * 2.1f, seed ^ 0x9E37u) +
           0.15f * pnoise(x * 4.3f, y * 4.3f, seed ^ 0x79B9u);
}

/* Palettes: PAL_N ramp per planet type (index by height/band value). */
static void make_palette(PlanetType t, uint32_t seed, uint16_t pal[PAL_N]) {
    switch (t) {
    case PT_ROCK: {
        static const uint16_t p[8] = {
            RGB565C(58, 48, 40), RGB565C(84, 70, 56), RGB565C(105, 88, 70),
            RGB565C(126, 106, 84), RGB565C(142, 122, 100), RGB565C(158, 138, 116),
            RGB565C(172, 152, 130), RGB565C(188, 168, 146) };
        memcpy(pal, p, sizeof p);
        break;
    }
    case PT_ICE: {
        static const uint16_t p[8] = {
            RGB565C(122, 138, 160), RGB565C(150, 165, 188), RGB565C(176, 190, 210),
            RGB565C(198, 210, 226), RGB565C(214, 224, 238), RGB565C(228, 236, 246),
            RGB565C(238, 244, 252), RGB565C(248, 252, 255) };
        memcpy(pal, p, sizeof p);
        break;
    }
    case PT_LAVA: {
        static const uint16_t p[8] = {
            RGB565C(28, 16, 14), RGB565C(48, 24, 18), RGB565C(70, 30, 20),
            RGB565C(96, 38, 22), RGB565C(140, 52, 22), RGB565C(196, 84, 26),
            RGB565C(240, 130, 32), RGB565C(255, 196, 70) };
        memcpy(pal, p, sizeof p);
        break;
    }
    case PT_OCEAN: {
        static const uint16_t p[8] = {
            RGB565C(12, 36, 84), RGB565C(16, 48, 108), RGB565C(20, 62, 130),
            RGB565C(26, 78, 150), RGB565C(34, 96, 168), RGB565C(60, 124, 186),
            RGB565C(120, 168, 200), RGB565C(225, 235, 240) };
        memcpy(pal, p, sizeof p);
        break;
    }
    case PT_EARTHLIKE: {
        static const uint16_t p[8] = {
            RGB565C(16, 44, 104), RGB565C(22, 60, 130), RGB565C(30, 80, 150),
            RGB565C(46, 110, 90), RGB565C(64, 130, 70), RGB565C(96, 144, 76),
            RGB565C(140, 150, 110), RGB565C(235, 240, 245) };
        memcpy(pal, p, sizeof p);
        break;
    }
    default:
    case PT_GAS: {
        /* Two gas families: tan/orange jovian or blue ice giant. */
        if (seed & 1) {
            static const uint16_t p[8] = {
                RGB565C(118, 92, 64), RGB565C(140, 110, 76), RGB565C(164, 130, 90),
                RGB565C(186, 150, 104), RGB565C(204, 168, 120), RGB565C(218, 184, 138),
                RGB565C(230, 200, 158), RGB565C(240, 216, 180) };
            memcpy(pal, p, sizeof p);
        } else {
            static const uint16_t p[8] = {
                RGB565C(36, 60, 120), RGB565C(48, 78, 142), RGB565C(62, 96, 160),
                RGB565C(78, 116, 178), RGB565C(96, 136, 194), RGB565C(118, 156, 208),
                RGB565C(142, 176, 220), RGB565C(168, 196, 232) };
            memcpy(pal, p, sizeof p);
        }
        break;
    }
    }
}

void r3d_planet_bake(const SystemInfo *info) {
    s_info = info;
    for (int i = 0; i < info->n_planets; i++) {
        const PlanetInfo *p = &info->planets[i];
        PlanetArt *a = &s_art[i];
        make_palette(p->type, p->tex_seed, a->pal);
        for (int y = 0; y < TEX_N; y++) {
            for (int x = 0; x < TEX_N; x++) {
                float v;
                if (p->type == PT_GAS) {
                    /* Latitude bands + turbulence. */
                    float band = sinf((float)y * 0.7f +
                                      2.5f * pnoise(x * 0.15f, y * 0.15f,
                                                    p->tex_seed)) * 0.5f + 0.5f;
                    v = band * 0.8f + 0.2f * pnoise(x * 0.4f, y * 0.4f,
                                                    p->tex_seed ^ 7u);
                } else {
                    v = fbm((float)x * 0.22f, (float)y * 0.22f, p->tex_seed);
                    if (p->type == PT_OCEAN)        /* mostly sea, island tops */
                        v = v < 0.62f ? v * 0.55f : v;
                    if (p->type == PT_EARTHLIKE)    /* sea/land split at ~0.45 */
                        v = v < 0.45f ? v * 0.6f : 0.35f + (v - 0.45f) * 1.1f;
                }
                int idx = (int)(v * PAL_N);
                if (idx < 0) idx = 0;
                if (idx >= PAL_N) idx = PAL_N - 1;
                a->tex[y * TEX_N + x] = (uint8_t)idx;
            }
        }
    }
}

/* --- per-frame impostor list -------------------------------------------*/
typedef struct {
    float sx, sy;          /* projected centre */
    float r_px;
    uint16_t d;            /* depth value at centre */
    int8_t planet;         /* index, or -1 for the sun */
    float lx, ly, lz;      /* light dir in screen-normal space */
} Impostor;

#define MAX_IMPOSTORS (GAL_MAX_PLANETS + 1)
static Impostor s_imp[MAX_IMPOSTORS];
static int s_nimp;

void r3d_planet_emit(Vec3 cam_pos_mm) {
    s_nimp = 0;
    if (!s_info) return;
    const Mat3 *cam = r3d_pipe_camera();
    float focal = r3d_pipe_focal();

    for (int i = -1; i < (int)s_info->n_planets; i++) {
        Vec3 body_mm = (i < 0) ? system_star_pos_mm()
                               : system_planet_pos_mm(i);
        float radius_mm = (i < 0) ? s_info->star_radius_mm
                                  : s_info->planets[i].radius_mm;

        /* Camera-relative, converted to meters (f32 magnitude is fine;
         * relative precision is what matters and bodies are far). */
        Vec3 rel_mm = v3_sub(body_mm, cam_pos_mm);
        Vec3 rel_m = v3_scale(rel_mm, 1.0e6f);
        Vec3 v = m3_mul_v3_t(cam, rel_m);
        if (v.z <= R3D_NEAR) continue;

        float inv_z = 1.0f / v.z;
        float sx = 64.0f + focal * v.x * inv_z;
        float sy = 64.0f - focal * v.y * inv_z;
        float r_px = focal * (radius_mm * 1.0e6f) * inv_z;
        if (r_px < 0.6f) continue;                   /* sub-pixel: skip */
        if (r_px > 96.0f) r_px = 96.0f;              /* close-approach clamp */
        /* Off-screen cull (with radius margin). */
        if (sx + r_px < 0 || sx - r_px > 127 || sy + r_px < 0 || sy - r_px > 127)
            continue;
        if (s_nimp >= MAX_IMPOSTORS) break;

        Impostor *im = &s_imp[s_nimp++];
        im->sx = sx;
        im->sy = sy;
        im->r_px = r_px;
        float dd = R3D_DEPTH_K / v.z;
        im->d = (dd >= 65535.0f) ? 65535u : (dd < 1.0f ? 1u : (uint16_t)dd);
        im->planet = (int8_t)i;

        if (i >= 0) {
            /* Light: from the body toward the star (system origin), in
             * screen-normal space (x right, y down, z toward camera). */
            Vec3 lw = v3_norm(v3_scale(body_mm, -1.0f));
            Vec3 lv = m3_mul_v3_t(cam, lw);
            im->lx = lv.x;
            im->ly = -lv.y;       /* screen y is down */
            im->lz = -lv.z;       /* screen-normal z is toward the camera */
        }
    }
}

void r3d_planet_raster(uint16_t *fb, int y0, int y1) {
    uint16_t *depth = r3d_depth_buffer();
    for (int n = 0; n < s_nimp; n++) {
        const Impostor *im = &s_imp[n];
        int r = (int)im->r_px;
        if (r < 1) r = 1;
        int cy = (int)im->sy, cx = (int)im->sx;
        int ylo = cy - r, yhi = cy + r;
        if (ylo < y0) ylo = y0;
        if (yhi >= y1) yhi = y1 - 1;
        float inv_r = 1.0f / im->r_px;

        if (im->planet < 0) {
            /* Sun: white-hot core -> dithered blend through the star's
             * colour -> glow halo (saturating add over the background)
             * + faint diffraction spikes. Disc writes depth; glow doesn't
             * (ships drawn later overwrite it -> bloom hugs occluders). */
            uint16_t star = s_info ? s_info->star_color : RGB565C(255, 230, 150);
            int sr = (star >> 11) & 31, sg = (star >> 5) & 63, sb = star & 31;
            float halo = 2.1f;                       /* halo extent, x disc */
            int hr = (int)(im->r_px * halo) + 1;
            int hylo = cy - hr, hyhi = cy + hr;
            if (hylo < y0) hylo = y0;
            if (hyhi >= y1) hyhi = y1 - 1;
            static const uint8_t bayer[2][2] = { {0, 2}, {3, 1} };
            for (int py = hylo; py <= hyhi; py++) {
                float ny = (py - im->sy) * inv_r;
                uint16_t *fr = fb + py * ELITE_FB_W;
                uint16_t *dr = depth + py * ELITE_FB_W;
                int xspan = (int)(sqrtf(halo * halo - (ny < 0 ? -ny : ny) *
                                        (ny < 0 ? -ny : ny) > 0
                                            ? halo * halo - ny * ny : 0) *
                                  im->r_px);
                int x0 = cx - xspan, x1 = cx + xspan;
                if (x0 < 0) x0 = 0;
                if (x1 > 127) x1 = 127;
                for (int px = x0; px <= x1; px++) {
                    float nx = (px - im->sx) * inv_r;
                    float rr = sqrtf(nx * nx + ny * ny);
                    if (rr <= 1.0f) {
                        if (im->d <= dr[px]) continue;
                        dr[px] = im->d;
                        /* Dithered white->star colour from 0.45 out. */
                        float f = (rr - 0.45f) * (1.0f / 0.55f);
                        float th = (bayer[py & 1][px & 1] + 0.5f) * 0.25f;
                        fr[px] = (f > th) ? star : RGB565C(255, 255, 240);
                    } else if (rr <= halo) {
                        /* Quadratic glow falloff, saturating add. */
                        float g = 1.0f - (rr - 1.0f) / (halo - 1.0f);
                        g = g * g * 0.7f;
                        int r = ((fr[px] >> 11) & 31) + (int)(sr * g);
                        int gg = ((fr[px] >> 5) & 63) + (int)(sg * g);
                        int b = (fr[px] & 31) + (int)(sb * g);
                        if (r > 31) r = 31;
                        if (gg > 63) gg = 63;
                        if (b > 31) b = 31;
                        fr[px] = (uint16_t)((r << 11) | (gg << 5) | b);
                    }
                }
            }
            /* Diffraction spikes: thin horizontal/vertical rays. */
            if (im->r_px >= 3.0f) {
                int len = (int)(im->r_px * 3.2f);
                for (int k = (int)im->r_px + 1; k <= len; k++) {
                    float g = 1.0f - (float)(k - im->r_px) /
                              (float)(len - im->r_px + 1);
                    g = g * g * 0.5f;
                    int ar = (int)(sr * g), ag = (int)(sg * g), ab = (int)(sb * g);
                    static const int dxs[4] = { 1, -1, 0, 0 };
                    static const int dys[4] = { 0, 0, 1, -1 };
                    for (int s = 0; s < 4; s++) {
                        int px = cx + dxs[s] * k, py = cy + dys[s] * k;
                        if ((unsigned)px >= ELITE_FB_W) continue;
                        if (py < y0 || py >= y1) continue;
                        uint16_t *fp = &fb[py * ELITE_FB_W + px];
                        int r = ((*fp >> 11) & 31) + ar;
                        int gg = ((*fp >> 5) & 63) + ag;
                        int b = (*fp & 31) + ab;
                        if (r > 31) r = 31;
                        if (gg > 63) gg = 63;
                        if (b > 31) b = 31;
                        *fp = (uint16_t)((r << 11) | (gg << 5) | b);
                    }
                }
            }
            continue;
        }

        const PlanetArt *art = &s_art[(int)im->planet];
        for (int py = ylo; py <= yhi; py++) {
            float ny = (py - im->sy) * inv_r;
            float w2 = 1.0f - ny * ny;
            if (w2 <= 0) continue;
            int half = (int)(sqrtf(w2) * im->r_px);
            int x0 = cx - half, x1 = cx + half;
            if (x0 < 0) x0 = 0;
            if (x1 > 127) x1 = 127;
            uint16_t *fr = fb + py * ELITE_FB_W;
            uint16_t *dr = depth + py * ELITE_FB_W;
            int tv = (int)((ny * 0.5f + 0.5f) * (TEX_N - 1));
            const uint8_t *trow = &art->tex[tv * TEX_N];
            for (int px = x0; px <= x1; px++) {
                if (im->d <= dr[px]) continue;
                float nx = (px - im->sx) * inv_r;
                float nz2 = 1.0f - nx * nx - ny * ny;
                if (nz2 < 0.0f) nz2 = 0.0f;
                float nz = sqrtf(nz2);
                /* Day/night terminator + limb darkening. */
                float light = nx * im->lx + ny * im->ly + nz * im->lz;
                if (light < 0.0f) light = 0.0f;
                float shade = 0.07f + 0.93f * light;
                shade *= 0.55f + 0.45f * nz;          /* limb darkening */
                int tu = (int)((nx * 0.5f + 0.5f) * (TEX_N - 1));
                uint16_t c = art->pal[trow[tu]];
                int cr = (int)(((c >> 11) & 31) * shade);
                int cg = (int)(((c >> 5) & 63) * shade);
                int cb = (int)((c & 31) * shade);
                dr[px] = im->d;
                fr[px] = (uint16_t)((cr << 11) | (cg << 5) | cb);
            }
        }
    }
}
