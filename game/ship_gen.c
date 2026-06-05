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
            quad(r3, r2, r1, r0, col);          /* root cap (+x... inner) */
            quad(r3, t1, t0, r3, col);          /* placeholder removed */
            s_nf--; s_nf--;                     /* drop the bad quad */
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

const Mesh *ship_gen_mesh(uint32_t seed) {
    s_rng = seed | 1u;
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

    /* --- family + fuselage genes -------------------------------------- */
    int family = rndi(0, 5);    /* 0 dart 1 fighter 2 intercept 3 gunship
                                   4 cruiser 5 hauler */
    float len = (family == 0) ? rndf(8, 12)
              : (family == 4) ? rndf(18, 26)
              : (family == 5) ? rndf(14, 20)
              : rndf(10, 16);
    float zb = -len * 0.5f, zf = len * 0.5f;
    float w_mid = len * ((family == 5) ? rndf(0.16f, 0.22f)
                                       : rndf(0.09f, 0.14f));
    float flat = (family == 5) ? rndf(0.7f, 0.95f) : rndf(0.42f, 0.68f);
    float ch = rndf(0.30f, 0.60f);           /* section chamfer */
    float rake = rndf(0.0f, 0.18f) * len;    /* tail-up spine curve */
    float nose_len = len * ((family == 2) ? rndf(0.30f, 0.42f)
                                          : rndf(0.18f, 0.30f));

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
