/*
 * ThumbyElite — procedural ship mesh generation.
 *
 * Geometry: 8-point chamfered-octagon rings lofted along z. Ring points
 * run CCW viewed from +z, so side quads wind outward and the nose fan
 * (toward +z) caps cleanly. All attachments mirror across x.
 *
 * Families steer the silhouette: DART (slim racer), FIGHTER (swept
 * wings), INTERCEPTOR (long nose + canards), GUNSHIP (prongs, twin
 * fins), CRUISER (long spine, dorsal fin), HAULER (deep slab + pods).
 */
#include "ship_gen.h"
#include "elite_types.h"
#include <math.h>

#define MAX_SV 240
#define MAX_SF 420

static MeshVert s_verts[MAX_SV];
static MeshFace s_faces[MAX_SF];
static Mesh     s_mesh;
static float    s_fx[MAX_SV], s_fy[MAX_SV], s_fz[MAX_SV];
static int      s_nv, s_nf;

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

static int vtx(float x, float y, float z) {
    if (s_nv >= MAX_SV) return s_nv - 1;
    s_fx[s_nv] = x; s_fy[s_nv] = y; s_fz[s_nv] = z;
    return s_nv++;
}

static void face(int a, int b, int c, uint16_t color) {
    if (s_nf >= MAX_SF) return;
    MeshFace *f = &s_faces[s_nf++];
    f->a = (uint8_t)a; f->b = (uint8_t)b; f->c = (uint8_t)c;
    f->color = color;
    float ux = s_fx[b] - s_fx[a], uy = s_fy[b] - s_fy[a], uz = s_fz[b] - s_fz[a];
    float vx = s_fx[c] - s_fx[a], vy = s_fy[c] - s_fy[a], vz = s_fz[c] - s_fz[a];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float l = sqrtf(nx * nx + ny * ny + nz * nz);
    if (l < 1e-9f) l = 1;
    f->nx = (int8_t)(nx / l * 127.0f);
    f->ny = (int8_t)(ny / l * 127.0f);
    f->nz = (int8_t)(nz / l * 127.0f);
}

static void quad(int a, int b, int c, int d, uint16_t col) {
    face(a, b, c, col);
    face(a, c, d, col);
}

/* Chamfered-octagon ring, CCW from +z. ch 0 = diamond, 1 = rectangle. */
static void ring(float z, float w, float h, float yo, float ch, int out[8]) {
    float wc = w * ch, hc = h * ch;
    out[0] = vtx(w, yo - hc, z);
    out[1] = vtx(w, yo + hc, z);
    out[2] = vtx(wc, yo + h, z);
    out[3] = vtx(-wc, yo + h, z);
    out[4] = vtx(-w, yo + hc, z);
    out[5] = vtx(-w, yo - hc, z);
    out[6] = vtx(-wc, yo - h, z);
    out[7] = vtx(wc, yo - h, z);
}

/* Side skin between two rings (r0 at smaller z). */
static void skin(const int r0[8], const int r1[8], uint16_t top,
                 uint16_t side, uint16_t bottom) {
    for (int k = 0; k < 8; k++) {
        int k2 = (k + 1) & 7;
        uint16_t c = (k == 2) ? top : (k == 6) ? bottom
                   : (k == 1 || k == 5) ? top : side;
        quad(r0[k], r0[k2], r1[k2], r1[k], c);
    }
}

static void cap_front(const int r[8], uint16_t col) {       /* +z fan */
    for (int k = 1; k < 7; k++) face(r[0], r[k], r[k + 1], col);
}
static void cap_back(const int r[8], uint16_t col) {        /* -z fan */
    for (int k = 1; k < 7; k++) face(r[0], r[k + 1], r[k], col);
}
static void nose_apex(const int r[8], float x, float y, float z,
                      uint16_t col) {
    int a = vtx(x, y, z);
    for (int k = 0; k < 8; k++)
        face(r[k], r[(k + 1) & 7], a, col);
}

/* Swept wing pair: thin slab from root to tip edge (mirrored). */
static void wings(float rootz0, float rootz1, float rootx, float rooty,
                  float th, float tipx, float tipz0, float tipz1,
                  float tipy, uint16_t col) {
    for (int side = 0; side < 2; side++) {
        float sx = side ? -1.0f : 1.0f;
        int r0 = vtx(sx * rootx, rooty - th, rootz0);
        int r1 = vtx(sx * rootx, rooty - th, rootz1);
        int r2 = vtx(sx * rootx, rooty + th, rootz1);
        int r3 = vtx(sx * rootx, rooty + th, rootz0);
        int t0 = vtx(sx * tipx, tipy, tipz0);
        int t1 = vtx(sx * tipx, tipy, tipz1);
        if (side == 0) {
            quad(r3, r2, r1, r0, col);          /* root cap (inner) */
            quad(r2, r3, t0, t1, col);          /* top */
            quad(r0, r1, t1, t0, col);          /* bottom */
            face(r1, r2, t1, col);              /* leading edge */
            face(r3, r0, t0, col);              /* trailing edge */
        } else {
            quad(r0, r1, r2, r3, col);
            quad(r3, r2, t1, t0, col);
            quad(r1, r0, t0, t1, col);
            face(r2, r1, t1, col);
            face(r0, r3, t0, col);
        }
    }
}

/* Vertical fin (top or bottom). dir = +1 up / -1 down. */
static void fin(float xoff, float z0, float z1, float ybase, float th,
                float height, float tipz0, float tipz1, float dir,
                uint16_t col) {
    int r0 = vtx(xoff - th, ybase, z0);
    int r1 = vtx(xoff + th, ybase, z0);
    int r2 = vtx(xoff + th, ybase, z1);
    int r3 = vtx(xoff - th, ybase, z1);
    float yt = ybase + dir * height;
    int t0 = vtx(xoff, yt, tipz0);
    int t1 = vtx(xoff, yt, tipz1);
    if (dir > 0) {
        quad(r0, r1, r2, r3, col);
        quad(t0, t1, r2, r1, col);
        quad(t1, t0, r0, r3, col);
        face(r3, r2, t1, col);
        face(r1, r0, t0, col);
    } else {
        quad(r3, r2, r1, r0, col);
        quad(r1, r2, t1, t0, col);
        quad(r3, r0, t0, t1, col);
        face(r2, r3, t1, col);
        face(r0, r1, t0, col);
    }
}

/* Small lofted pod (nacelle), mirrored when mirror!=0. */
static void nacelle(float x, float y, float z0, float z1, float r,
                    uint16_t body, uint16_t glow, int mirror) {
    for (int side = 0; side < (mirror ? 2 : 1); side++) {
        float sx = side ? -x : x;
        int a[8], b[8], c[8];
        ring(z0, r * 0.7f, r * 0.7f, y, 0.45f, a);
        ring((z0 + z1) * 0.5f, r, r, y, 0.45f, b);
        ring(z1, r * 0.75f, r * 0.75f, y, 0.45f, c);
        /* shift ring x */
        for (int k = 0; k < 8; k++) {
            s_fx[a[k]] += sx; s_fx[b[k]] += sx; s_fx[c[k]] += sx;
        }
        skin(a, b, body, body, body);
        skin(b, c, body, body, body);
        cap_back(a, glow);
        cap_front(c, body);
    }
}

/* Slim forward gun barrel (X-wing wingtips, prongs). */
static void tip_gun(float x, float y, float z, float len, float r,
                    uint16_t col, uint16_t dark) {
    int g0[8], g1[8];
    ring(z, r, r, y, 0.4f, g0);
    ring(z + len, r * 0.7f, r * 0.7f, y, 0.4f, g1);
    for (int k = 0; k < 8; k++) { s_fx[g0[k]] += x; s_fx[g1[k]] += x; }
    skin(g0, g1, col, col, col);
    cap_back(g0, col);
    cap_front(g1, dark);
}

/* class-hint state (set by ship_gen_mesh_class; -1 = free roll) */
static int s_hint = -1;

const Mesh *ship_gen_mesh(uint32_t seed) {
    /* Proper mix — seed|1 made adjacent even/odd seeds identical. */
    s_rng = seed * 2654435761u;
    s_rng ^= s_rng >> 15;
    s_rng *= 1274126177u;
    if (s_rng == 0) s_rng = 1;
    s_nv = s_nf = 0;

    /* --- palette ------------------------------------------------------ */
    int tone = rndi(0, 3);
    uint16_t HULL, HULL2;
    switch (tone) {
    case 0: HULL = RGB565C(168, 170, 178); HULL2 = RGB565C(120, 122, 132); break;
    case 1: HULL = RGB565C(150, 160, 180); HULL2 = RGB565C(104, 114, 138); break;
    case 2: HULL = RGB565C(176, 166, 148); HULL2 = RGB565C(130, 120, 102); break;
    default: HULL = RGB565C(126, 132, 142); HULL2 = RGB565C(88, 94, 104); break;
    }
    uint16_t GLASS = RGB565C(40, 120, 150);
    static const uint16_t k_glow[4] = {
        RGB565C(245, 120, 30), RGB565C(70, 150, 255),
        RGB565C(80, 230, 180), RGB565C(200, 90, 230),
    };
    uint16_t GLOW = k_glow[rndi(0, 3)];
    static const uint16_t k_acc[4] = {
        RGB565C(190, 60, 45), RGB565C(220, 170, 50),
        RGB565C(60, 140, 200), RGB565C(150, 150, 160),
    };
    uint16_t ACC = k_acc[rndi(0, 3)];

    /* --- archetype + family + fuselage genes --------------------------
     * archetype: 0 = loft families, 1 = x-foil, 2 = panel pod (TIE),
     * 3 = saucer. Class hints bias both. */
    int arch, family = 0;
    int hint = s_hint;
    s_hint = -1;
    if (hint < 0) {
        int r = rndi(0, 99);
        arch = (r < 55) ? 0 : (r < 75) ? 1 : (r < 87) ? 2 : 3;
        if (arch == 0) family = rndi(0, 5);
    } else {
        switch (hint) {
        case 0: arch = 0; family = rndi(0, 1) ? 0 : 5; break;  /* starter */
        case 1: arch = 0; family = 0; break;                   /* courier */
        case 2: arch = rndi(0, 1) ? 0 : 1; family = 1; break;  /* lt ftr */
        case 3: arch = (rndi(0, 2) == 0) ? 1 : 0;
                family = rndi(1, 2); break;                    /* patrol */
        case 4: arch = rndi(0, 1) ? 0 : 2; family = 3; break;  /* raider */
        case 5: arch = rndi(0, 1) ? 1 : 0; family = 3; break;  /* hv ftr */
        case 6: arch = (rndi(0, 2) == 0) ? 3 : 0; family = 5; break;
        case 7: arch = rndi(0, 1) ? 3 : 0; family = 5; break;  /* hauler */
        case 8: arch = 0; family = 5; break;                   /* hv haul */
        default: arch = 0; family = 4; break;                  /* dread */
        }
    }
    float len = (family == 0) ? rndf(8, 12)
              : (family == 4) ? rndf(18, 26)
              : (family == 5) ? rndf(14, 20)
              : rndf(10, 16);
    if (hint == 0) len = rndf(7, 10);
    if (hint == 9) len = rndf(20, 28);
    if (arch == 2) len = rndf(5, 8);              /* pods are compact */
    if (arch == 3) len = rndf(9, 15);
    float zb = -len * 0.5f, zf = len * 0.5f;
    float w_mid = len * ((family == 5) ? rndf(0.16f, 0.22f)
                                       : rndf(0.09f, 0.14f));
    float flat = (family == 5) ? rndf(0.7f, 0.95f) : rndf(0.42f, 0.68f);
    float ch = rndf(0.30f, 0.60f);           /* section chamfer */
    float rake = rndf(0.0f, 0.18f) * len;    /* tail-up spine curve */
    float nose_len = len * ((family == 2) ? rndf(0.30f, 0.42f)
                                          : rndf(0.18f, 0.30f));

    if (arch == 1) {
        /* --- X-FOIL: slim fuselage + 4 wings in X + tip cannons ------- */
        float zb2 = -len * 0.5f, zf2 = len * 0.5f;
        float w = len * rndf(0.06f, 0.09f);
        float h = w * rndf(0.9f, 1.2f);
        int a[8], b[8], c[8], d[8];
        ring(zb2, w * 0.8f, h * 0.8f, 0, 0.45f, a);
        ring(zb2 + len * 0.3f, w, h, 0, 0.45f, b);
        ring(zb2 + len * 0.62f, w * 0.95f, h * 1.05f, h * 0.1f, 0.5f, c);
        ring(zf2 - len * 0.22f, w * 0.55f, h * 0.6f, 0, 0.45f, d);
        skin(a, b, HULL, HULL, HULL2);
        skin(b, c, HULL, HULL, HULL2);
        skin(c, d, HULL, HULL, HULL2);
        cap_back(a, GLOW);
        nose_apex(d, 0, 0, zf2, HULL2);
        /* canopy bump */
        {
            int e[8], f2[8];
            ring(zb2 + len * 0.42f, w * 0.5f, w * 0.3f, h, 0.5f, e);
            ring(zb2 + len * 0.58f, w * 0.35f, w * 0.18f, h * 0.9f, 0.5f, f2);
            skin(e, f2, GLASS, GLASS, HULL);
            cap_back(e, HULL);
            cap_front(f2, HULL2);
        }
        /* 4 foils in X: upper + lower pairs, swept slightly back */
        float span = len * rndf(0.34f, 0.46f);
        float dihed = span * rndf(0.45f, 0.7f);
        float wz0 = zb2 + len * rndf(0.12f, 0.22f);
        float wz1 = wz0 + len * rndf(0.16f, 0.22f);
        float sweep = len * rndf(0.04f, 0.12f);
        wings(wz0, wz1, w * 0.9f, h * 0.4f, h * 0.14f,
              w + span, wz0 - sweep, wz0 - sweep + len * 0.1f,
              h * 0.4f + dihed, HULL2);
        wings(wz0, wz1, w * 0.9f, -h * 0.4f, h * 0.14f,
              w + span, wz0 - sweep, wz0 - sweep + len * 0.1f,
              -h * 0.4f - dihed, HULL2);
        /* wingtip cannons (mirrored x by tip_gun calls per side) */
        float gl = len * rndf(0.2f, 0.3f);
        tip_gun(w + span, h * 0.4f + dihed, wz0 - sweep, gl, w * 0.16f,
                ACC, RGB565C(40, 40, 48));
        tip_gun(-(w + span), h * 0.4f + dihed, wz0 - sweep, gl, w * 0.16f,
                ACC, RGB565C(40, 40, 48));
        tip_gun(w + span, -h * 0.4f - dihed, wz0 - sweep, gl, w * 0.16f,
                ACC, RGB565C(40, 40, 48));
        tip_gun(-(w + span), -h * 0.4f - dihed, wz0 - sweep, gl, w * 0.16f,
                ACC, RGB565C(40, 40, 48));
        goto finish;
    }
    if (arch == 2) {
        /* --- PANEL POD: varied body + small symmetric panels ---------- */
        float r = len * rndf(0.30f, 0.40f);
        int body = rndi(0, 3);   /* 0 ball, 1 capsule, 2 twin, 3 angular */
        float px;                /* pylon anchor x */
        if (body == 1) {
            /* capsule: stretched 4-ring pod */
            int a[8], b[8], c[8], d[8];
            ring(-r * 1.6f, r * 0.55f, r * 0.55f, 0, 0.5f, a);
            ring(-r * 0.5f, r * 0.95f, r * 0.9f, 0, 0.55f, b);
            ring(r * 0.5f, r * 0.95f, r * 0.9f, 0, 0.55f, c);
            ring(r * 1.3f, r * 0.55f, r * 0.55f, 0, 0.5f, d);
            skin(a, b, HULL, HULL, HULL2);
            skin(b, c, HULL, HULL, HULL2);
            skin(c, d, HULL, HULL, HULL2);
            cap_back(a, GLOW);
            cap_front(d, GLASS);
            px = r * 0.95f;
        } else if (body == 2) {
            /* twin: cockpit ball forward + engine block aft */
            int a[8], b[8], c[8];
            ring(-r * 1.7f, r * 0.7f, r * 0.7f, 0, 0.25f, a);
            ring(-r * 0.6f, r * 0.8f, r * 0.8f, 0, 0.3f, b);
            skin(a, b, HULL2, HULL2, HULL2);
            cap_back(a, GLOW);
            cap_front(b, HULL2);
            ring(-r * 0.3f, r * 0.6f, r * 0.6f, 0, 0.5f, a);
            ring(0.4f * r, r * 0.9f, r * 0.85f, 0, 0.55f, b);
            ring(r * 1.2f, r * 0.6f, r * 0.6f, 0, 0.5f, c);
            skin(a, b, HULL, HULL, HULL2);
            skin(b, c, HULL, HULL, HULL2);
            cap_back(a, HULL2);
            cap_front(c, GLASS);
            px = r * 0.9f;
        } else {
            /* ball (chamfer .55) or angular (chamfer .2) */
            float chb = (body == 3) ? rndf(0.15f, 0.3f) : rndf(0.5f, 0.6f);
            int a[8], b[8], c[8];
            ring(-r, r * 0.55f, r * 0.55f, 0, chb, a);
            ring(0, r, r * rndf(0.8f, 1.0f), 0, chb, b);
            ring(r * 0.85f, r * 0.6f, r * 0.6f, 0, chb, c);
            skin(a, b, HULL, HULL, HULL2);
            skin(b, c, HULL, HULL, HULL2);
            cap_back(a, GLOW);
            cap_front(c, GLASS);
            px = r;
        }
        /* pylons */
        float pylon = r * rndf(1.4f, 1.8f);
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? -1.0f : 1.0f;
            int p0 = vtx(sx * px * 0.8f, -r * 0.12f, -r * 0.18f);
            int p1 = vtx(sx * px * 0.8f, -r * 0.12f, r * 0.18f);
            int p2 = vtx(sx * px * 0.8f, r * 0.12f, r * 0.18f);
            int p3 = vtx(sx * px * 0.8f, r * 0.12f, -r * 0.18f);
            int q0 = vtx(sx * pylon, -r * 0.1f, -r * 0.15f);
            int q1 = vtx(sx * pylon, -r * 0.1f, r * 0.15f);
            int q2 = vtx(sx * pylon, r * 0.1f, r * 0.15f);
            int q3 = vtx(sx * pylon, r * 0.1f, -r * 0.15f);
            if (sd == 0) {
                quad(p1, q1, q2, p2, HULL2);
                quad(q0, p0, p3, q3, HULL2);
                quad(p2, q2, q3, p3, HULL2);
                quad(q1, p1, p0, q0, HULL2);
            } else {
                quad(q1, p1, p2, q2, HULL2);
                quad(p0, q0, q3, p3, HULL2);
                quad(q2, p2, p3, q3, HULL2);
                quad(p1, q1, q0, p0, HULL2);
            }
        }
        /* Panels: COMPACT, fore-aft SYMMETRIC shapes only. */
        {
            int style = rndi(0, 3);
            float ph = r * rndf(0.9f, 1.5f);
            float pz = r * rndf(0.7f, 1.1f);
            float cant = r * rndf(-0.3f, 0.4f);
            float zz[6], yy[6];
            int np;
            switch (style) {
            case 0:   /* classic hex */
                np = 6;
                zz[0] = 0;          yy[0] = -ph;
                zz[1] = pz * 0.85f; yy[1] = -ph * 0.45f;
                zz[2] = pz * 0.85f; yy[2] = ph * 0.45f;
                zz[3] = 0;          yy[3] = ph;
                zz[4] = -pz * 0.85f; yy[4] = ph * 0.45f;
                zz[5] = -pz * 0.85f; yy[5] = -ph * 0.45f;
                break;
            case 1:   /* chamfered rectangle (classic TIE) */
                np = 6;
                zz[0] = pz * 0.55f;  yy[0] = -ph;
                zz[1] = pz;          yy[1] = -ph * 0.55f;
                zz[2] = pz;          yy[2] = ph * 0.55f;
                zz[3] = pz * 0.55f;  yy[3] = ph;
                zz[4] = -pz * 0.55f; yy[4] = ph;
                zz[5] = -pz * 0.55f; yy[5] = -ph;
                break;
            case 2:   /* symmetric trapezoid, narrow top */
                np = 4;
                zz[0] = pz;   yy[0] = -ph;
                zz[1] = pz * 0.5f;  yy[1] = ph;
                zz[2] = -pz * 0.5f; yy[2] = ph;
                zz[3] = -pz;  yy[3] = -ph;
                break;
            default:  /* slim vertical blade */
                np = 4;
                zz[0] = pz * 0.4f;  yy[0] = -ph;
                zz[1] = pz * 0.4f;  yy[1] = ph;
                zz[2] = -pz * 0.4f; yy[2] = ph;
                zz[3] = -pz * 0.4f; yy[3] = -ph;
                break;
            }
            for (int sd = 0; sd < 2; sd++) {
                float sxn = sd ? -1.0f : 1.0f;
                int outer[6], inner[6];
                for (int k = 0; k < np; k++) {
                    float ay = yy[k] < 0 ? -yy[k] : yy[k];
                    float xo = pylon + cant * (ay / ph);
                    outer[k] = vtx(sxn * (xo + r * 0.06f), yy[k], zz[k]);
                    inner[k] = vtx(sxn * xo, yy[k], zz[k]);
                }
                for (int k = 1; k < np - 1; k++) {
                    if (sd == 0) {
                        face(outer[0], outer[k], outer[k + 1], ACC);
                        face(inner[0], inner[k + 1], inner[k], HULL2);
                    } else {
                        face(outer[0], outer[k + 1], outer[k], ACC);
                        face(inner[0], inner[k], inner[k + 1], HULL2);
                    }
                }
            }
        }
        goto finish;
    }
    if (arch == 3) {
        /* --- SAUCER: wide flat hull + mandibles + offset cockpit ------ */
        float zb2 = -len * 0.5f, zf2 = len * 0.30f;
        float w = len * rndf(0.34f, 0.46f);
        float h = w * rndf(0.18f, 0.28f);
        int a[8], b[8], c[8];
        ring(zb2, w * 0.55f, h * 0.7f, 0, 0.55f, a);
        ring(zb2 + len * 0.45f, w, h, 0, 0.6f, b);
        ring(zf2, w * 0.6f, h * 0.8f, 0, 0.55f, c);
        skin(a, b, HULL, HULL, HULL2);
        skin(b, c, HULL, HULL, HULL2);
        cap_back(a, GLOW);
        cap_front(c, HULL2);
        /* twin mandibles forward */
        float mx = w * rndf(0.3f, 0.45f);
        tip_gun(mx, 0, zf2 - len * 0.02f, len * rndf(0.2f, 0.3f),
                w * 0.09f, HULL2, RGB565C(40, 40, 48));
        tip_gun(-mx, 0, zf2 - len * 0.02f, len * rndf(0.2f, 0.3f),
                w * 0.09f, HULL2, RGB565C(40, 40, 48));
        /* offset cockpit tube */
        {
            float cx2 = w * rndf(0.45f, 0.7f);
            int e[8], f2[8];
            ring(zb2 + len * 0.5f, w * 0.12f, w * 0.10f, h * 0.8f, 0.5f, e);
            ring(zf2 + len * 0.05f, w * 0.09f, w * 0.07f, h * 0.6f, 0.5f, f2);
            for (int k = 0; k < 8; k++) {
                s_fx[e[k]] += cx2; s_fx[f2[k]] += cx2;
            }
            skin(e, f2, HULL2, HULL2, HULL2);
            cap_back(e, HULL2);
            cap_front(f2, GLASS);
        }
        goto finish;
    }

    /* Stations: tail, aft, mid, fore; then nose apex. */
    float z0 = zb, z1 = zb + len * 0.22f, z2 = zb + len * rndf(0.45f, 0.6f),
          z3 = zf - nose_len;
    float w0 = w_mid * rndf(0.55f, 0.8f), w1 = w_mid * rndf(0.9f, 1.0f);
    float w3 = w_mid * rndf(0.45f, 0.65f);
    float h0 = w0 * flat, h1 = w1 * flat, h2 = w_mid * flat, h3 = w3 * flat;
    float y0 = rake * 0.5f, y1 = rake * 0.2f, y3 = -rake * 0.1f;

    int rA[8], rB[8], rC[8], rD[8];
    ring(z0, w0, h0, y0, ch, rA);
    ring(z1, w1, h1, y1, ch, rB);
    ring(z2, w_mid, h2, 0, ch, rC);
    ring(z3, w3, h3, y3, ch * 0.8f, rD);
    skin(rA, rB, HULL, HULL, HULL2);
    skin(rB, rC, HULL, HULL, HULL2);
    skin(rC, rD, HULL, HULL, HULL2);
    cap_back(rA, GLOW);                       /* integrated engine tail */
    nose_apex(rD, 0, y3 - h3 * 0.3f, zf, HULL2);

    /* --- canopy: small glass loft on the fore-mid deck ----------------- */
    if (family != 5 || rndi(0, 1)) {
        float cz0 = z2 - len * 0.05f, cz1 = z2 + len * rndf(0.12f, 0.2f);
        float cw = w_mid * rndf(0.28f, 0.4f);
        int c0[8], c1[8];
        ring(cz0, cw, cw * 0.5f, h2 * 0.95f, 0.5f, c0);
        ring(cz1, cw * 0.6f, cw * 0.3f, h2 * 0.8f, 0.5f, c1);
        skin(c0, c1, GLASS, GLASS, HULL);
        cap_back(c0, HULL);
        cap_front(c1, HULL2);
    }

    /* --- family attachments ------------------------------------------- */
    float span = len * rndf(0.35f, 0.55f);
    switch (family) {
    case 0:   /* dart: stub fins only */
        wings(z0 + len * 0.05f, z0 + len * 0.3f, w_mid * 0.8f, 0,
              h2 * 0.12f, w_mid * 0.8f + span * 0.45f,
              z0, z0 + len * 0.12f, h2 * 0.4f, ACC);
        break;
    case 1: { /* fighter: swept main wings + tail fin */
        float sweep = rndf(0.15f, 0.4f) * len;
        wings(z1, z1 + len * rndf(0.25f, 0.35f), w1 * 0.85f, 0,
              h1 * 0.10f, w1 + span,
              z1 - sweep, z1 - sweep + len * 0.10f,
              rndf(-0.5f, 1.2f) * h1, HULL2);
        fin(0, z0 + len * 0.02f, z0 + len * 0.2f, h0 * 0.8f,
            w_mid * 0.05f, h0 * rndf(1.2f, 2.2f),
            z0, z0 + len * 0.1f, 1.0f, ACC);
        break;
    }
    case 2: { /* interceptor: canards + rear wings */
        wings(z3 - len * 0.08f, z3, w3 * 0.9f, 0, h3 * 0.12f,
              w3 + span * 0.4f, z3 - len * 0.14f, z3 - len * 0.05f,
              h3 * 0.3f, ACC);
        float sweep = rndf(0.2f, 0.35f) * len;
        wings(z1, z1 + len * 0.22f, w1 * 0.85f, 0, h1 * 0.10f,
              w1 + span * 0.8f, z1 - sweep, z1 - sweep + len * 0.09f,
              h1 * rndf(0.2f, 0.9f), HULL2);
        break;
    }
    case 3: { /* gunship: prongs + twin canted fins */
        float px = w_mid * rndf(0.5f, 0.75f);
        for (int s2 = 0; s2 < 2; s2++) {
            float sx = s2 ? -px : px;
            int g0[8], g1[8];
            ring(z3 - len * 0.05f, w_mid * 0.10f, w_mid * 0.10f, 0, 0.4f, g0);
            ring(zf + len * 0.08f, w_mid * 0.07f, w_mid * 0.07f, 0, 0.4f, g1);
            for (int k = 0; k < 8; k++) {
                s_fx[g0[k]] += sx; s_fx[g1[k]] += sx;
            }
            skin(g0, g1, ACC, ACC, ACC);
            cap_back(g0, HULL2);
            cap_front(g1, RGB565C(40, 40, 48));
        }
        fin(w_mid * 0.55f, z0, z0 + len * 0.16f, h0 * 0.6f,
            w_mid * 0.05f, h0 * 1.6f, z0, z0 + len * 0.08f, 1.0f, ACC);
        fin(-w_mid * 0.55f, z0, z0 + len * 0.16f, h0 * 0.6f,
            w_mid * 0.05f, h0 * 1.6f, z0, z0 + len * 0.08f, 1.0f, ACC);
        break;
    }
    case 4: { /* cruiser: dorsal + ventral fins, mid winglets */
        fin(0, z1, z1 + len * 0.25f, h1 * 0.9f, w_mid * 0.06f,
            h1 * rndf(1.4f, 2.4f), z1, z1 + len * 0.12f, 1.0f, HULL2);
        fin(0, z1 + len * 0.05f, z1 + len * 0.2f, -h1 * 0.9f,
            w_mid * 0.05f, h1 * 1.2f, z1 + len * 0.05f,
            z1 + len * 0.12f, -1.0f, ACC);
        wings(z2 - len * 0.05f, z2 + len * 0.1f, w_mid * 0.9f, 0,
              h2 * 0.10f, w_mid + span * 0.5f, z2 - len * 0.12f,
              z2 - len * 0.04f, 0, HULL2);
        break;
    }
    default:  /* hauler: side pods (cargo nacelles) */
        nacelle(w_mid * 1.15f, 0, z0 + len * 0.1f, z2,
                w_mid * rndf(0.35f, 0.5f), HULL2, GLOW, 1);
        break;
    }

    /* Engine nacelles for fighters/interceptors/cruisers (50%). */
    if ((family == 1 || family == 2 || family == 4) && rndi(0, 1)) {
        nacelle(w_mid * rndf(0.95f, 1.25f), -h2 * 0.2f,
                z0, z0 + len * rndf(0.3f, 0.42f),
                w_mid * rndf(0.22f, 0.34f), HULL2, GLOW, 1);
    }

finish:;
    /* --- quantise ------------------------------------------------------ */
    float maxc = 1.0f, bound2 = 1.0f;
    for (int i = 0; i < s_nv; i++) {
        float ax = fabsf(s_fx[i]), ay = fabsf(s_fy[i]), az = fabsf(s_fz[i]);
        if (ax > maxc) maxc = ax;
        if (ay > maxc) maxc = ay;
        if (az > maxc) maxc = az;
        float d2 = s_fx[i] * s_fx[i] + s_fy[i] * s_fy[i] +
                   s_fz[i] * s_fz[i];
        if (d2 > bound2) bound2 = d2;
    }
    float q = 127.0f / maxc;
    for (int i = 0; i < s_nv; i++) {
        s_verts[i].x = (int8_t)(s_fx[i] * q);
        s_verts[i].y = (int8_t)(s_fy[i] * q);
        s_verts[i].z = (int8_t)(s_fz[i] * q);
    }
    s_mesh.verts = s_verts;
    s_mesh.faces = s_faces;
    s_mesh.nverts = (uint16_t)s_nv;
    s_mesh.nfaces = (uint16_t)s_nf;
    s_mesh.scale = maxc;
    s_mesh.bound_r = sqrtf(bound2);
    s_mesh.lod_lo = 0;
    return &s_mesh;
}

const Mesh *ship_gen_mesh_class(uint32_t seed, int class_hint) {
    s_hint = class_hint;
    return ship_gen_mesh(seed);
}

int ship_gen_copy(MeshVert *verts, int max_v, MeshFace *faces, int max_f,
                  Mesh *out) {
    int nv = s_mesh.nverts < max_v ? s_mesh.nverts : max_v;
    int nf = s_mesh.nfaces < max_f ? s_mesh.nfaces : max_f;
    for (int i = 0; i < nv; i++) verts[i] = s_verts[i];
    for (int i = 0; i < nf; i++) faces[i] = s_faces[i];
    *out = s_mesh;
    out->verts = verts;
    out->faces = faces;
    out->nverts = (uint16_t)nv;
    out->nfaces = (uint16_t)nf;
    return nf;
}
