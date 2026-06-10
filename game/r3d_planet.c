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
    /* Each type carries several REALISTIC colourways (user req: more
     * variety, nothing garish); the seed picks one, the patterns are
     * unchanged. */
    uint32_t pick = (seed >> 3);
    switch (t) {
    case PT_ROCK: {
        static const uint16_t p[4][8] = {
            { RGB565C(58, 48, 40), RGB565C(84, 70, 56), RGB565C(105, 88, 70),
              RGB565C(126, 106, 84), RGB565C(142, 122, 100), RGB565C(158, 138, 116),
              RGB565C(172, 152, 130), RGB565C(188, 168, 146) },   /* tan */
            { RGB565C(46, 46, 50), RGB565C(64, 64, 68), RGB565C(82, 82, 86),
              RGB565C(100, 100, 104), RGB565C(118, 118, 122), RGB565C(136, 136, 140),
              RGB565C(154, 154, 158), RGB565C(172, 172, 176) },   /* mercury grey */
            { RGB565C(72, 38, 26), RGB565C(96, 50, 32), RGB565C(120, 62, 38),
              RGB565C(142, 76, 46), RGB565C(162, 92, 56), RGB565C(180, 110, 70),
              RGB565C(196, 130, 88), RGB565C(210, 150, 108) },    /* mars rust */
            { RGB565C(52, 50, 36), RGB565C(72, 68, 48), RGB565C(92, 86, 60),
              RGB565C(112, 104, 72), RGB565C(130, 122, 86), RGB565C(148, 138, 100),
              RGB565C(164, 154, 116), RGB565C(180, 170, 132) },   /* olive dust */
        };
        memcpy(pal, p[pick % 4u], sizeof p[0]);
        break;
    }
    case PT_ICE: {
        static const uint16_t p[3][8] = {
            { RGB565C(122, 138, 160), RGB565C(150, 165, 188), RGB565C(176, 190, 210),
              RGB565C(198, 210, 226), RGB565C(214, 224, 238), RGB565C(228, 236, 246),
              RGB565C(238, 244, 252), RGB565C(248, 252, 255) },   /* blue-white */
            { RGB565C(150, 134, 122), RGB565C(176, 158, 142), RGB565C(198, 180, 162),
              RGB565C(216, 200, 182), RGB565C(230, 216, 200), RGB565C(240, 230, 216),
              RGB565C(248, 240, 230), RGB565C(255, 250, 242) },   /* pluto cream */
            { RGB565C(96, 120, 124), RGB565C(122, 146, 150), RGB565C(146, 170, 172),
              RGB565C(168, 190, 192), RGB565C(188, 208, 210), RGB565C(206, 222, 224),
              RGB565C(222, 234, 236), RGB565C(238, 246, 247) },   /* europa teal */
        };
        memcpy(pal, p[pick % 3u], sizeof p[0]);
        break;
    }
    case PT_LAVA: {
        static const uint16_t p[3][8] = {
            { RGB565C(28, 16, 14), RGB565C(48, 24, 18), RGB565C(70, 30, 20),
              RGB565C(96, 38, 22), RGB565C(140, 52, 22), RGB565C(196, 84, 26),
              RGB565C(240, 130, 32), RGB565C(255, 196, 70) },     /* orange */
            { RGB565C(20, 10, 12), RGB565C(38, 14, 16), RGB565C(60, 18, 20),
              RGB565C(86, 22, 22), RGB565C(122, 28, 24), RGB565C(164, 40, 28),
              RGB565C(206, 60, 34), RGB565C(240, 96, 48) },       /* crimson */
            { RGB565C(56, 42, 22), RGB565C(82, 62, 28), RGB565C(110, 84, 34),
              RGB565C(138, 106, 40), RGB565C(166, 130, 46), RGB565C(192, 154, 54),
              RGB565C(216, 180, 66), RGB565C(238, 208, 84) },     /* io sulfur */
        };
        memcpy(pal, p[pick % 3u], sizeof p[0]);
        break;
    }
    case PT_OCEAN: {
        static const uint16_t p[3][8] = {
            { RGB565C(12, 36, 84), RGB565C(16, 48, 108), RGB565C(20, 62, 130),
              RGB565C(26, 78, 150), RGB565C(34, 96, 168), RGB565C(60, 124, 186),
              RGB565C(120, 168, 200), RGB565C(225, 235, 240) },   /* deep blue */
            { RGB565C(10, 56, 64), RGB565C(14, 74, 84), RGB565C(18, 94, 104),
              RGB565C(24, 114, 124), RGB565C(34, 134, 142), RGB565C(58, 156, 160),
              RGB565C(110, 186, 186), RGB565C(220, 240, 238) },   /* tropic teal */
            { RGB565C(16, 24, 48), RGB565C(22, 32, 64), RGB565C(28, 42, 80),
              RGB565C(36, 54, 96), RGB565C(46, 68, 112), RGB565C(64, 88, 130),
              RGB565C(100, 118, 152), RGB565C(200, 210, 224) },   /* storm navy */
        };
        memcpy(pal, p[pick % 3u], sizeof p[0]);
        break;
    }
    case PT_EARTHLIKE: {
        static const uint16_t p[3][8] = {
            { RGB565C(16, 44, 104), RGB565C(22, 60, 130), RGB565C(30, 80, 150),
              RGB565C(46, 110, 90), RGB565C(64, 130, 70), RGB565C(96, 144, 76),
              RGB565C(140, 150, 110), RGB565C(235, 240, 245) },   /* temperate */
            { RGB565C(18, 48, 96), RGB565C(26, 64, 118), RGB565C(36, 84, 138),
              RGB565C(96, 110, 64), RGB565C(128, 124, 64), RGB565C(156, 138, 72),
              RGB565C(180, 158, 96), RGB565C(238, 242, 244) },    /* savanna */
            { RGB565C(12, 40, 92), RGB565C(18, 54, 116), RGB565C(24, 72, 138),
              RGB565C(30, 96, 72), RGB565C(40, 116, 54), RGB565C(58, 132, 56),
              RGB565C(96, 142, 82), RGB565C(232, 238, 242) },     /* jungle */
        };
        memcpy(pal, p[pick % 3u], sizeof p[0]);
        break;
    }
    default:
    case PT_GAS: {
        /* Four gas families: jovian tan, neptune blue, saturn
         * butterscotch, uranus pale teal. */
        static const uint16_t p[4][8] = {
            { RGB565C(118, 92, 64), RGB565C(140, 110, 76), RGB565C(164, 130, 90),
              RGB565C(186, 150, 104), RGB565C(204, 168, 120), RGB565C(218, 184, 138),
              RGB565C(230, 200, 158), RGB565C(240, 216, 180) },
            { RGB565C(36, 60, 120), RGB565C(48, 78, 142), RGB565C(62, 96, 160),
              RGB565C(78, 116, 178), RGB565C(96, 136, 194), RGB565C(118, 156, 208),
              RGB565C(142, 176, 220), RGB565C(168, 196, 232) },
            { RGB565C(150, 122, 82), RGB565C(172, 142, 96), RGB565C(192, 162, 112),
              RGB565C(208, 180, 130), RGB565C(222, 196, 148), RGB565C(234, 212, 168),
              RGB565C(244, 226, 188), RGB565C(252, 238, 208) },
            { RGB565C(96, 142, 150), RGB565C(116, 160, 168), RGB565C(136, 178, 184),
              RGB565C(156, 194, 200), RGB565C(176, 208, 214), RGB565C(196, 222, 226),
              RGB565C(214, 234, 238), RGB565C(230, 244, 246) },
        };
        memcpy(pal, p[(seed >> 3) % 4u], sizeof p[0]);
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
    float dist;            /* view z, metres — painter's ordering */
    float lx, ly, lz;      /* light dir in screen-normal space */
} Impostor;

#define MAX_IMPOSTORS (GAL_MAX_PLANETS + 1)
static Impostor s_imp[MAX_IMPOSTORS];
static int s_nimp;

/* Screen-space direction of world "up" — the roll reference so planet
 * textures stay anchored to the world (roll with the ship) instead of the
 * screen. Recomputed each frame in emit, consumed per-pixel in raster. */
static float s_up_sx = 0.0f, s_up_sy = -1.0f;

void r3d_planet_emit(Vec3 cam_pos_mm) {
    s_nimp = 0;
    if (!s_info) return;
    const Mat3 *cam = r3d_pipe_camera();
    float focal = r3d_pipe_focal();

    /* World up (0,1,0) projected into screen space (x right, y down): when the
     * ship is level this is straight up; it rotates as the ship rolls. */
    float ux = cam->r[0].y, uy = -cam->r[1].y;
    float ul = sqrtf(ux * ux + uy * uy);
    if (ul > 1e-4f) { s_up_sx = ux / ul; s_up_sy = uy / ul; }
    else            { s_up_sx = 0.0f;    s_up_sy = -1.0f; }   /* pole-on: keep screen-up */

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
        im->dist = v.z;

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

    /* Painter's sort, far -> near: at Mm distances every disc quantises
     * to the same u16 depth, so draw order IS the z-order (the sun was
     * winning ties and floating in front of planets). */
    for (int a = 1; a < s_nimp; a++) {
        Impostor tmp = s_imp[a];
        int b = a - 1;
        while (b >= 0 && s_imp[b].dist < tmp.dist) {
            s_imp[b + 1] = s_imp[b];
            b--;
        }
        s_imp[b + 1] = tmp;
    }
}

void r3d_planet_raster(uint16_t *fb, int y0, int y1) {
    /* y0/y1 and every coordinate below are PHYSICAL pixels; the impostor
     * list itself stays logical (HUD/emit reuse it), so scale up here. */
    uint16_t *depth = r3d_depth_buffer();
    for (int n = 0; n < s_nimp; n++) {
        const Impostor *im = &s_imp[n];
        const float imx = im->sx * (float)R3D_SS;
        const float imy = im->sy * (float)R3D_SS;
        const float imr = im->r_px * (float)R3D_SS;
        int r = (int)imr;
        if (r < 1) r = 1;
        int cy = (int)imy, cx = (int)imx;
        int ylo = cy - r, yhi = cy + r;
        if (ylo < y0) ylo = y0;
        if (yhi >= y1) yhi = y1 - 1;
        float inv_r = 1.0f / imr;

        if (im->planet < 0) {
            /* Sun: white-hot core -> dithered blend through the star's
             * colour -> glow halo (saturating add over the background)
             * + faint diffraction spikes. Disc writes depth; glow doesn't
             * (ships drawn later overwrite it -> bloom hugs occluders). */
            uint16_t star = s_info ? s_info->star_color : RGB565C(255, 230, 150);
            int sr = (star >> 11) & 31, sg = (star >> 5) & 63, sb = star & 31;
            float halo = 2.1f;                       /* halo extent, x disc */
            int hr = (int)(imr * halo) + 1;
            int hylo = cy - hr, hyhi = cy + hr;
            if (hylo < y0) hylo = y0;
            if (hyhi >= y1) hyhi = y1 - 1;
            static const uint8_t bayer[2][2] = { {0, 2}, {3, 1} };
            for (int py = hylo; py <= hyhi; py++) {
                float ny = (py - imy) * inv_r;
                uint16_t *fr = fb + py * R3D_FB_W;
                uint16_t *dr = depth + py * R3D_FB_W;
                int xspan = (int)(sqrtf(halo * halo - (ny < 0 ? -ny : ny) *
                                        (ny < 0 ? -ny : ny) > 0
                                            ? halo * halo - ny * ny : 0) *
                                  imr);
                int x0 = cx - xspan, x1 = cx + xspan;
                if (x0 < 0) x0 = 0;
                if (x1 > R3D_FB_W - 1) x1 = R3D_FB_W - 1;
                for (int px = x0; px <= x1; px++) {
                    float nx = (px - imx) * inv_r;
                    float rr = sqrtf(nx * nx + ny * ny);
                    if (rr <= 1.0f) {
                        if (im->d < dr[px]) continue;   /* ties: painter order wins */
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
            if (imr >= 3.0f * R3D_SS) {
                int len = (int)(imr * 3.2f);
                for (int k = (int)imr + 1; k <= len; k++) {
                    float g = 1.0f - (float)(k - imr) /
                              (float)(len - imr + 1);
                    g = g * g * 0.5f;
                    int ar = (int)(sr * g), ag = (int)(sg * g), ab = (int)(sb * g);
                    static const int dxs[4] = { 1, -1, 0, 0 };
                    static const int dys[4] = { 0, 0, 1, -1 };
                    for (int s = 0; s < 4; s++) {
                        int px = cx + dxs[s] * k, py = cy + dys[s] * k;
                        if ((unsigned)px >= R3D_FB_W) continue;
                        if (py < y0 || py >= y1) continue;
                        uint16_t *fp = &fb[py * R3D_FB_W + px];
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
        const float ux = s_up_sx, uy = s_up_sy;        /* world-up on screen */
        for (int py = ylo; py <= yhi; py++) {
            float ny = (py - imy) * inv_r;
            float w2 = 1.0f - ny * ny;
            if (w2 <= 0) continue;
            int half = (int)(sqrtf(w2) * imr);
            int x0 = cx - half, x1 = cx + half;
            if (x0 < 0) x0 = 0;
            if (x1 > R3D_FB_W - 1) x1 = R3D_FB_W - 1;
            uint16_t *fr = fb + py * R3D_FB_W;
            uint16_t *dr = depth + py * R3D_FB_W;
            for (int px = x0; px <= x1; px++) {
                if (im->d < dr[px]) continue;   /* ties: painter order wins */
                float nx = (px - imx) * inv_r;
                float nz2 = 1.0f - nx * nx - ny * ny;
                if (nz2 < 0.0f) nz2 = 0.0f;
                float nz = sqrtf(nz2);
                /* Day/night terminator + limb darkening. */
                float light = nx * im->lx + ny * im->ly + nz * im->lz;
                if (light < 0.0f) light = 0.0f;
                float shade = 0.07f + 0.93f * light;
                shade *= 0.55f + 0.45f * nz;          /* limb darkening */
                /* Rotate the sample into the world-up frame so the texture
                 * rolls with the world (u = longitude, v = latitude). */
                float nx2 = nx * (-uy) + ny * ux;
                float ny2 = -(nx * ux + ny * uy);
                int tu = (int)((nx2 * 0.5f + 0.5f) * (TEX_N - 1));
                int tv = (int)((ny2 * 0.5f + 0.5f) * (TEX_N - 1));
                if (tu < 0) tu = 0; else if (tu >= TEX_N) tu = TEX_N - 1;
                if (tv < 0) tv = 0; else if (tv >= TEX_N) tv = TEX_N - 1;
                uint16_t c = art->pal[art->tex[tv * TEX_N + tu]];
                int cr = (int)(((c >> 11) & 31) * shade);
                int cg = (int)(((c >> 5) & 63) * shade);
                int cb = (int)((c & 31) * shade);
                dr[px] = im->d;
                fr[px] = (uint16_t)((cr << 11) | (cg << 5) | cb);
            }
        }
    }
}
