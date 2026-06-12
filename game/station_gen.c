/*
 * ThumbyElite — procedural station mesh generation.
 *
 * Growth algorithm (accretion, CA-flavoured):
 *   1. Core: box or octagonal drum, seeded proportions.
 *   2. 5-9 growth steps: pick an existing module, attach a new module
 *      to one of its free faces (habitat box / solar wing / spur /
 *      antenna fin), mirrored across X when the attachment is lateral.
 *   3. Docking bay: the core's +z face gets the glass bay + frame.
 *   4. Trim lights: accent blocks at module corners.
 *
 * Geometry is built in floats then quantised to the int8 mesh format
 * (same rules as obj2mesh). Static buffers — no allocation.
 */
#include "station_gen.h"
#include "elite_types.h"
#include <math.h>
#include <string.h>

#define MAX_SV 200
#define MAX_SF 340
#define MAX_MODULES 24

static MeshVert s_verts[MAX_SV];
static MeshFace s_faces[MAX_SF];
static Mesh     s_mesh;

/* Proposal-look switch (style lab — sheets only). */
static int s_style;
void station_gen_set_style(int s) { s_style = s; }

/* Float-space build buffers. */
static float s_fx[MAX_SV], s_fy[MAX_SV], s_fz[MAX_SV];
static int   s_nv, s_nf;

typedef struct { float cx, cy, cz, hx, hy, hz; } Module;
static Module s_mods[MAX_MODULES];
static int    s_nmods;

static uint32_t s_rng;
static uint32_t rnd(void) {
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return s_rng;
}
static float rndf(float lo, float hi) {
    return lo + (hi - lo) * (float)(rnd() & 0xFFFF) * (1.0f / 65535.0f);
}
static int rndi(int lo, int hi) {
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

static int add_vert(float x, float y, float z) {
    if (s_nv >= MAX_SV) return s_nv - 1;
    s_fx[s_nv] = x; s_fy[s_nv] = y; s_fz[s_nv] = z;
    return s_nv++;
}

static void add_face(int a, int b, int c, uint16_t color) {
    if (s_nf >= MAX_SF) return;
    MeshFace *f = &s_faces[s_nf++];
    f->a = (uint8_t)a; f->b = (uint8_t)b; f->c = (uint8_t)c;
    f->color = color;
    /* Normal from the float verts (quantised later with everything). */
    float ux = s_fx[b] - s_fx[a], uy = s_fy[b] - s_fy[a], uz = s_fz[b] - s_fz[a];
    float vx = s_fx[c] - s_fx[a], vy = s_fy[c] - s_fy[a], vz = s_fz[c] - s_fz[a];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float l = sqrtf(nx * nx + ny * ny + nz * nz);
    if (l < 1e-9f) l = 1;
    f->nx = (int8_t)(nx / l * 127.0f);
    f->ny = (int8_t)(ny / l * 127.0f);
    f->nz = (int8_t)(nz / l * 127.0f);
}

/* Box with CCW-from-outside winding (gen_ships.py template).
 * face_colors: NULL or 6 entries: front back right left top bottom. */
static void box(float cx, float cy, float cz, float hx, float hy, float hz,
                uint16_t mtl, const uint16_t *fc) {
    if (s_nv + 8 > MAX_SV || s_nf + 12 > MAX_SF) return;
    int i0 = add_vert(cx - hx, cy - hy, cz - hz);
    int i1 = add_vert(cx + hx, cy - hy, cz - hz);
    int i2 = add_vert(cx + hx, cy + hy, cz - hz);
    int i3 = add_vert(cx - hx, cy + hy, cz - hz);
    int i4 = add_vert(cx - hx, cy - hy, cz + hz);
    int i5 = add_vert(cx + hx, cy - hy, cz + hz);
    int i6 = add_vert(cx + hx, cy + hy, cz + hz);
    int i7 = add_vert(cx - hx, cy + hy, cz + hz);
    uint16_t cf = fc ? fc[0] : mtl, cb = fc ? fc[1] : mtl;
    uint16_t cr = fc ? fc[2] : mtl, cl = fc ? fc[3] : mtl;
    uint16_t ct = fc ? fc[4] : mtl, cd = fc ? fc[5] : mtl;
    add_face(i4, i5, i6, cf); add_face(i4, i6, i7, cf);   /* front +z */
    add_face(i1, i0, i3, cb); add_face(i1, i3, i2, cb);   /* back  -z */
    add_face(i5, i1, i2, cr); add_face(i5, i2, i6, cr);   /* right +x */
    add_face(i0, i4, i7, cl); add_face(i0, i7, i3, cl);   /* left  -x */
    add_face(i7, i6, i2, ct); add_face(i7, i2, i3, ct);   /* top   +y */
    add_face(i0, i1, i5, cd); add_face(i0, i5, i4, cd);   /* bottom-y */
    if (s_nmods < MAX_MODULES)
        s_mods[s_nmods++] = (Module){ cx, cy, cz, hx, hy, hz };
}

/* Octagonal drum along z (the classic station core silhouette). */
static void drum(float r, float hz, uint16_t mtl, uint16_t face_col) {
    if (s_nv + 16 > MAX_SV || s_nf + 28 > MAX_SF) return;
    int ring0[8], ring1[8];
    for (int i = 0; i < 8; i++) {
        float a = (float)i * (6.2831853f / 8.0f) + 0.3927f;
        ring0[i] = add_vert(cosf(a) * r, sinf(a) * r, -hz);
        ring1[i] = add_vert(cosf(a) * r, sinf(a) * r, hz);
    }
    for (int i = 0; i < 8; i++) {
        int j = (i + 1) & 7;
        /* Side quad, outward. */
        add_face(ring0[i], ring0[j], ring1[j], mtl);
        add_face(ring0[i], ring1[j], ring1[i], mtl);
    }
    /* Caps (fans). +z cap CCW from +z; -z cap reversed. */
    for (int i = 1; i < 7; i++) {
        add_face(ring1[0], ring1[i], ring1[i + 1], face_col);
        add_face(ring0[0], ring0[i + 1], ring0[i], mtl);
    }
    if (s_nmods < MAX_MODULES)
        s_mods[s_nmods++] = (Module){ 0, 0, 0, r, r, hz };
}

/* Faceted ball: three octagonal rings + polar caps. */
static void ball(float r, uint16_t mtl, uint16_t face_col) {
    if (s_nv + 26 > MAX_SV || s_nf + 48 > MAX_SF) return;
    int rings[3][8];
    float zs[3] = { -r * 0.55f, 0, r * 0.55f };
    float rs[3] = { r * 0.68f, r, r * 0.68f };
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < 8; i++) {
            float a = (float)i * (6.2831853f / 8.0f) + 0.3927f;
            rings[k][i] = add_vert(cosf(a) * rs[k], sinf(a) * rs[k], zs[k]);
        }
    int south = add_vert(0, 0, -r * 0.95f);
    int north = add_vert(0, 0, r * 0.95f);
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 8; i++) {
            int j = (i + 1) & 7;
            add_face(rings[k][i], rings[k][j], rings[k + 1][j], mtl);
            add_face(rings[k][i], rings[k + 1][j], rings[k + 1][i],
                     (i & 1) ? mtl : face_col);
        }
    for (int i = 0; i < 8; i++) {
        int j = (i + 1) & 7;
        add_face(rings[0][i], south, rings[0][j], mtl);
        add_face(rings[2][i], rings[2][j], north, face_col);
    }
    if (s_nmods < MAX_MODULES)
        s_mods[s_nmods++] = (Module){ 0, 0, 0, r, r, r };
}

/* Ring station: octagonal torus + hub drum + four spokes. */
static void ring_core(float R, float tube, uint16_t mtl, uint16_t mtl2,
                      uint16_t face_col) {
    if (s_nv + 32 > MAX_SV || s_nf + 64 > MAX_SF) return;
    int fo[8], fi[8], bo[8], bi[8];
    for (int i = 0; i < 8; i++) {
        float a = (float)i * (6.2831853f / 8.0f);
        float co = cosf(a), si = sinf(a);
        fo[i] = add_vert(co * (R + tube), si * (R + tube), tube);
        fi[i] = add_vert(co * (R - tube), si * (R - tube), tube);
        bo[i] = add_vert(co * (R + tube), si * (R + tube), -tube);
        bi[i] = add_vert(co * (R - tube), si * (R - tube), -tube);
    }
    for (int i = 0; i < 8; i++) {
        int j = (i + 1) & 7;
        add_face(fo[i], fo[j], fi[j], face_col);   /* front +z */
        add_face(fo[i], fi[j], fi[i], face_col);
        add_face(bo[j], bo[i], bi[i], mtl2);       /* back -z */
        add_face(bo[j], bi[i], bi[j], mtl2);
        add_face(bo[i], bo[j], fo[j], mtl);        /* outer wall */
        add_face(bo[i], fo[j], fo[i], mtl);
        add_face(bi[j], bi[i], fi[i], mtl2);       /* inner wall */
        add_face(bi[j], fi[i], fi[j], mtl2);
    }
    /* Register rim segments as accretion hosts — without these, every
     * module grew off the hub and the wheel looked like a core with a
     * hoop (user report). Eight anchor blocks around the rim. */
    for (int i = 0; i < 8; i++) {
        float a = (float)i * (6.2831853f / 8.0f) + 0.3927f;
        if (s_nmods < MAX_MODULES)
            s_mods[s_nmods++] = (Module){ cosf(a) * R, sinf(a) * R, 0,
                                          tube * 1.2f, tube * 1.2f,
                                          tube };
    }

    /* Hub + spokes. */
    drum(R * 0.32f, tube * 1.4f, mtl, face_col);
    /* Axis-aligned spokes: at 45 deg the |cos|x|sin| extents both went
     * large and each spoke became a square plate over the hole (user
     * report). On-axis they collapse to thin radial arms. */
    for (int k = 0; k < 4; k++) {
        float a = (float)k * 1.5707963f;
        float mx = cosf(a) * R * 0.62f, my = sinf(a) * R * 0.62f;
        box(mx, my, 0, fabsf(cosf(a)) * R * 0.34f + tube * 0.35f,
            fabsf(sinf(a)) * R * 0.34f + tube * 0.35f, tube * 0.30f,
            mtl2, NULL);
    }
}

/* Spindle: stretched octahedral bipyramid + equator band. */
static void spindle(float r, float hz, uint16_t mtl, uint16_t mtl2,
                    uint16_t face_col) {
    if (s_nv + 18 > MAX_SV || s_nf + 32 > MAX_SF) return;
    int eq[8];
    for (int i = 0; i < 8; i++) {
        float a = (float)i * (6.2831853f / 8.0f) + 0.3927f;
        eq[i] = add_vert(cosf(a) * r, sinf(a) * r, 0);
    }
    int tip_f = add_vert(0, 0, hz);
    int tip_b = add_vert(0, 0, -hz);
    for (int i = 0; i < 8; i++) {
        int j = (i + 1) & 7;
        add_face(eq[i], eq[j], tip_f, (i & 1) ? mtl : face_col);
        add_face(eq[j], eq[i], tip_b, (i & 1) ? mtl2 : mtl);
    }
    /* Equator band: thin drum overlapping the waist. */
    drum(r * 1.04f, hz * 0.14f, mtl2, mtl2);
}

const Mesh *station_gen_mesh(uint32_t seed) {
    s_rng = seed * 2654435761u;
    s_rng ^= s_rng >> 15;
    if (!s_rng) s_rng = 1;
    s_nv = s_nf = s_nmods = 0;

    /* Palette: hull greys tinted per station. */
    int tint = rndi(-10, 22);
    uint16_t HULL = rgb565((uint8_t)(182 + tint), (uint8_t)(184 + tint),
                           (uint8_t)(192 + tint));
    uint16_t HULL2 = rgb565((uint8_t)(140 + tint), (uint8_t)(142 + tint),
                            (uint8_t)(154 + tint));
    uint16_t DARK = RGB565C(70, 76, 92);
    uint16_t GLASS = RGB565C(80, 170, 200);
    uint16_t WIN = RGB565C(34, 60, 96);       /* window bands */
    uint16_t PANEL = RGB565C(60, 120, 195);
    uint16_t ACCENT = (rnd() & 1) ? RGB565C(220, 90, 60)
                                  : RGB565C(230, 175, 60);

    /* 1. Core: six silhouettes (user req: the box core read dull).
     * drum 25 / ring 20 / ball 15 / spindle 15 / box 15 / cross 10. */
    int core_roll = rndi(0, 99);
    float core_r;
    if (core_roll < 25) {
        core_r = rndf(11, 15);
        float core_hz = rndf(8, 16);
        drum(core_r, core_hz, HULL, GLASS);   /* +z cap = docking face */
    } else if (core_roll < 45) {
        /* Ring station: the wheel — slim tube, large radius. */
        core_r = rndf(18, 24);
        ring_core(core_r, rndf(1.8f, 2.8f), HULL, HULL2, GLASS);
    } else if (core_roll < 60) {
        core_r = rndf(12, 16);
        ball(core_r, HULL, WIN);
    } else if (core_roll < 75) {
        core_r = rndf(9, 12);
        spindle(core_r, rndf(18, 26), HULL, HULL2, WIN);
    } else if (core_roll < 90) {
        core_r = rndf(10, 14);
        float chy = rndf(7, 12), chz = rndf(9, 15);
        uint16_t fc[6] = { GLASS, DARK, HULL, HULL, HULL2, HULL2 };
        box(0, 0, 0, core_r, chy, chz, HULL, fc);
    } else {
        /* Cross truss: three interlocked slabs. */
        core_r = rndf(11, 14);
        uint16_t fc[6] = { GLASS, DARK, HULL, HULL, HULL2, HULL2 };
        box(0, 0, 0, core_r, core_r * 0.32f, core_r * 0.32f, HULL, fc);
        box(0, 0, 0, core_r * 0.32f, core_r, core_r * 0.32f, HULL2, NULL);
        box(0, 0, 0, core_r * 0.3f, core_r * 0.3f, core_r * 1.1f, HULL,
            fc);
    }

    /* 2. Accretion: attach modules to existing structure. */
    int steps = rndi(5, 9);
    for (int s = 0; s < steps && s_nmods < MAX_MODULES - 2; s++) {
        const Module *host = &s_mods[rndi(0, s_nmods - 1)];
        int kind = rndi(0, 9);
        if (kind < 4) {
            /* Habitat box on a lateral face (mirrored across x). */
            float hx = rndf(2.5f, 6.0f), hy = rndf(2.5f, 5.0f),
                  hz = rndf(3.0f, 8.0f);
            float oz = rndf(-host->hz * 0.6f, host->hz * 0.6f);
            float oy = rndf(-host->hy * 0.5f, host->hy * 0.5f);
            float cx = host->cx + host->hx + hx * 0.9f;
            /* Window bands on the long faces; occasional accent roof. */
            uint16_t roof = (rnd() & 3) ? HULL : ACCENT;
            uint16_t fc[6] = { HULL2, HULL2, WIN, WIN, roof, HULL2 };
            box(cx, host->cy + oy, host->cz + oz, hx, hy, hz, HULL2, fc);
            box(-cx, host->cy + oy, host->cz + oz, hx, hy, hz, HULL2, fc);
        } else if (kind < 6) {
            /* Solar wing pair: arm + thin blue panel. */
            float ay = rndf(-host->hy * 0.4f, host->hy * 0.4f);
            float az = rndf(-host->hz * 0.5f, host->hz * 0.5f);
            float arm = rndf(4, 7), pw = rndf(8, 14), pl = rndf(7, 12);
            float ax = host->cx + host->hx;
            box(ax + arm * 0.5f, host->cy + ay, host->cz + az,
                arm * 0.5f, 1.0f, 1.0f, HULL2, NULL);
            box(-(ax + arm * 0.5f), host->cy + ay, host->cz + az,
                arm * 0.5f, 1.0f, 1.0f, HULL2, NULL);
            box(ax + arm + pw * 0.5f, host->cy + ay, host->cz + az,
                pw * 0.5f, 0.4f, pl, PANEL, NULL);
            box(-(ax + arm + pw * 0.5f), host->cy + ay, host->cz + az,
                pw * 0.5f, 0.4f, pl, PANEL, NULL);
        } else if (kind < 8) {
            /* Dorsal/ventral spur. */
            float dir = (rnd() & 1) ? 1.0f : -1.0f;
            float hy = rndf(3, 7);
            float cy = host->cy + dir * (host->hy + hy * 0.9f);
            box(host->cx, cy, host->cz + rndf(-host->hz * 0.5f, host->hz * 0.5f),
                rndf(2, 4.5f), hy, rndf(2, 4.5f), HULL2, NULL);
        } else {
            /* Antenna fin (thin, accent tip). */
            float dir = (rnd() & 1) ? 1.0f : -1.0f;
            float hy = rndf(5, 9);
            float cy = host->cy + dir * (host->hy + hy);
            uint16_t fc[6] = { HULL2, HULL2, HULL2, HULL2, ACCENT, ACCENT };
            box(host->cx, cy, host->cz, 0.5f, hy, 0.5f, HULL2, fc);
        }
    }

    /* 3. Docking bay frame on the core +z face. */
    {
        float bz = s_mods[0].hz;
        uint16_t fc[6] = { GLASS, DARK, ACCENT, ACCENT, ACCENT, ACCENT };
        box(0, 0, bz + 0.8f, core_r * 0.45f, core_r * 0.35f, 0.9f, ACCENT, fc);
    }

    /* 4. Quantise to the int8 mesh format. */
    float maxc = 1.0f, bound2 = 1.0f;
    for (int i = 0; i < s_nv; i++) {
        float ax = fabsf(s_fx[i]), ay = fabsf(s_fy[i]), az = fabsf(s_fz[i]);
        if (ax > maxc) maxc = ax;
        if (ay > maxc) maxc = ay;
        if (az > maxc) maxc = az;
        float d2 = s_fx[i] * s_fx[i] + s_fy[i] * s_fy[i] + s_fz[i] * s_fz[i];
        if (d2 > bound2) bound2 = d2;
    }
    float q = 127.0f / maxc;
    for (int i = 0; i < s_nv; i++) {
        s_verts[i].x = (int8_t)(s_fx[i] * q);
        s_verts[i].y = (int8_t)(s_fy[i] * q);
        s_verts[i].z = (int8_t)(s_fz[i] * q);
    }

#ifdef ELITE_STYLE_LAB
    if (s_style == 1) {
        /* PROPOSAL: per-panel weathering, hotter window glow, and
         * hazard-orange accents around the docking face. */
        uint32_t h = seed * 2654435761u ^ 0x5747u;
        h ^= h >> 13;
        for (int i = 0; i < s_nf; i++) {
            MeshFace *f = &s_faces[i];
            if (f->color == RGB565C(34, 60, 96))
                f->color = RGB565C(70, 130, 175);    /* lit windows */
            else if (f->nz > 100 && (i & 3) == 0)
                f->color = RGB565C(225, 130, 40);    /* bay hazard ring */
            else {
                uint32_t ph = (uint32_t)i * 2654435761u ^ h;
                ph ^= ph >> 13;
                int pv = (int)(ph % 6u);
                if (pv < 2) {
                    int pc = pv ? 110 : 86;          /* weathered panels */
                    int r2 = ((f->color >> 11) & 31) * pc / 100;
                    int g2 = ((f->color >> 5) & 63) * pc / 100;
                    int b2 = (f->color & 31) * pc / 100;
                    if (r2 > 31) r2 = 31;
                    if (g2 > 63) g2 = 63;
                    if (b2 > 31) b2 = 31;
                    f->color = (uint16_t)((r2 << 11) | (g2 << 5) | b2);
                }
            }
        }
    }
#endif
    s_mesh.verts = s_verts;
    s_mesh.faces = s_faces;
    s_mesh.nverts = (uint16_t)s_nv;
    s_mesh.nfaces = (uint16_t)s_nf;
    s_mesh.scale = maxc;
    s_mesh.bound_r = sqrtf(bound2);
    s_mesh.lod_lo = 0;
    return &s_mesh;
}
