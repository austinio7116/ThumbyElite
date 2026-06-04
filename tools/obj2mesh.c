/*
 * ThumbyElite — OBJ -> meshes_gen.c/h baker (native host tool).
 *
 * Parses Wavefront OBJ (v / f / mtllib / usemtl; f may use a/b/c or
 * a/t/n forms; polygons are fan-triangulated) plus the .mtl Kd colours,
 * quantises vertices to int8 (scale = max |coord|), precomputes face
 * normals from the winding, and emits const C tables in the engine's
 * MeshVert/MeshFace format (see game/r3d_mesh.h).
 *
 * Winding check: warns when a face normal points against (centroid -
 * mesh-centre) — a likely inverted face on a mostly-convex hull.
 *
 * Usage: obj2mesh <out.c> <out.h> <name>=<file.obj> ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define MAX_V 1024
#define MAX_F 2048
#define MAX_MTL 32

typedef struct { float x, y, z; } V3;
typedef struct { int a, b, c; int mtl; } Face;
typedef struct { char name[64]; float r, g, b; } Mtl;

static V3   verts[MAX_V];
static Face faces[MAX_F];
static Mtl  mtls[MAX_MTL];
static int  nv, nf, nmtl;

static int mtl_find(const char *name) {
    for (int i = 0; i < nmtl; i++)
        if (!strcmp(mtls[i].name, name)) return i;
    return -1;
}

static void load_mtl(const char *objpath, const char *mtlname) {
    char path[512];
    const char *slash = strrchr(objpath, '/');
    if (slash)
        snprintf(path, sizeof path, "%.*s/%s",
                 (int)(slash - objpath), objpath, mtlname);
    else
        snprintf(path, sizeof path, "%s", mtlname);
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "warn: no mtl %s\n", path); return; }
    char line[256];
    Mtl *cur = NULL;
    while (fgets(line, sizeof line, f)) {
        char name[64];
        float r, g, b;
        if (sscanf(line, "newmtl %63s", name) == 1) {
            if (nmtl < MAX_MTL) {
                cur = &mtls[nmtl++];
                snprintf(cur->name, sizeof cur->name, "%s", name);
                cur->r = cur->g = cur->b = 0.6f;
            }
        } else if (cur && sscanf(line, "Kd %f %f %f", &r, &g, &b) == 3) {
            cur->r = r; cur->g = g; cur->b = b;
        }
    }
    fclose(f);
}

static int parse_index(const char *tok) {
    /* "12", "12/3", "12//5", "12/3/5" -> 12. Negative = relative. */
    int idx = atoi(tok);
    if (idx < 0) idx = nv + 1 + idx;
    return idx - 1;
}

static int load_obj(const char *path) {
    nv = nf = nmtl = 0;
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "error: cannot open %s\n", path); return 0; }
    char line[512];
    int cur_mtl = -1;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "mtllib ", 7)) {
            char name[256];
            if (sscanf(line, "mtllib %255s", name) == 1) load_mtl(path, name);
        } else if (!strncmp(line, "usemtl ", 7)) {
            char name[64];
            if (sscanf(line, "usemtl %63s", name) == 1)
                cur_mtl = mtl_find(name);
        } else if (line[0] == 'v' && line[1] == ' ') {
            if (nv >= MAX_V) { fprintf(stderr, "too many verts\n"); return 0; }
            sscanf(line + 2, "%f %f %f",
                   &verts[nv].x, &verts[nv].y, &verts[nv].z);
            nv++;
        } else if (line[0] == 'f' && line[1] == ' ') {
            int idx[16], n = 0;
            char *tok = strtok(line + 2, " \t\r\n");
            while (tok && n < 16) {
                idx[n++] = parse_index(tok);
                tok = strtok(NULL, " \t\r\n");
            }
            for (int i = 2; i < n; i++) {       /* fan-triangulate */
                if (nf >= MAX_F) { fprintf(stderr, "too many faces\n"); return 0; }
                faces[nf].a = idx[0];
                faces[nf].b = idx[i - 1];
                faces[nf].c = idx[i];
                faces[nf].mtl = cur_mtl;
                nf++;
            }
        }
    }
    fclose(f);
    return 1;
}

static V3 v3sub(V3 a, V3 b) { V3 r = {a.x-b.x, a.y-b.y, a.z-b.z}; return r; }
static V3 v3cross(V3 a, V3 b) {
    V3 r = {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
    return r;
}
static float v3dot(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static float v3len(V3 a) { return sqrtf(v3dot(a, a)); }

static uint16_t kd565(int mtl) {
    float r = 0.6f, g = 0.6f, b = 0.65f;
    if (mtl >= 0) { r = mtls[mtl].r; g = mtls[mtl].g; b = mtls[mtl].b; }
    int ri = (int)(r * 255.0f + 0.5f), gi = (int)(g * 255.0f + 0.5f),
        bi = (int)(b * 255.0f + 0.5f);
    if (ri > 255) ri = 255;
    if (gi > 255) gi = 255;
    if (bi > 255) bi = 255;
    return (uint16_t)(((ri & 0xF8) << 8) | ((gi & 0xFC) << 3) | (bi >> 3));
}

static int emit_mesh(FILE *c, FILE *h, const char *name, const char *path) {
    if (!load_obj(path)) return 0;
    if (nv == 0 || nf == 0) {
        fprintf(stderr, "error: %s has no geometry\n", path);
        return 0;
    }

    /* Quantisation scale + bounding radius + centre (for winding check). */
    float maxc = 0, bound_r = 0;
    V3 centre = {0, 0, 0};
    for (int i = 0; i < nv; i++) {
        float ax = fabsf(verts[i].x), ay = fabsf(verts[i].y),
              az = fabsf(verts[i].z);
        if (ax > maxc) maxc = ax;
        if (ay > maxc) maxc = ay;
        if (az > maxc) maxc = az;
        float l = v3len(verts[i]);
        if (l > bound_r) bound_r = l;
        centre.x += verts[i].x; centre.y += verts[i].y; centre.z += verts[i].z;
    }
    centre.x /= nv; centre.y /= nv; centre.z /= nv;
    if (maxc < 1e-6f) maxc = 1.0f;
    float q = 127.0f / maxc;

    fprintf(c, "/* %s: %d verts, %d faces (from %s) */\n", name, nv, nf, path);
    fprintf(c, "static const MeshVert %s_verts[%d] = {\n", name, nv);
    for (int i = 0; i < nv; i++) {
        int x = (int)lrintf(verts[i].x * q);
        int y = (int)lrintf(verts[i].y * q);
        int z = (int)lrintf(verts[i].z * q);
        fprintf(c, "    {%4d,%4d,%4d},%s", x, y, z,
                (i % 4 == 3 || i == nv - 1) ? "\n" : "");
    }
    fprintf(c, "};\n");
    fprintf(c, "static const MeshFace %s_faces[%d] = {\n", name, nf);

    int warned = 0;
    for (int i = 0; i < nf; i++) {
        V3 a = verts[faces[i].a], b = verts[faces[i].b], cc = verts[faces[i].c];
        V3 n = v3cross(v3sub(b, a), v3sub(cc, a));
        float l = v3len(n);
        if (l < 1e-9f) { n.x = 0; n.y = 0; n.z = 1; l = 1; }
        n.x /= l; n.y /= l; n.z /= l;

        V3 fc = {(a.x + b.x + cc.x) / 3, (a.y + b.y + cc.y) / 3,
                 (a.z + b.z + cc.z) / 3};
        if (v3dot(n, v3sub(fc, centre)) < -1e-4f && warned < 8) {
            fprintf(stderr,
                "warn: %s face %d (%d,%d,%d) may be inward-wound\n",
                name, i, faces[i].a, faces[i].b, faces[i].c);
            warned++;
        }

        fprintf(c, "    {%3d,%3d,%3d, %4d,%4d,%4d, 0x%04X},\n",
                faces[i].a, faces[i].b, faces[i].c,
                (int)lrintf(n.x * 127.0f), (int)lrintf(n.y * 127.0f),
                (int)lrintf(n.z * 127.0f), kd565(faces[i].mtl));
    }
    fprintf(c, "};\n");
    fprintf(c, "const Mesh mesh_%s = { %s_verts, %s_faces, %d, %d, "
               "%.6ff, %.6ff, 0 };\n\n",
            name, name, name, nv, nf, maxc, bound_r);
    fprintf(h, "extern const Mesh mesh_%s;\n", name);
    printf("[obj2mesh] %s: %d verts %d tris scale=%.2fm r=%.2fm\n",
           name, nv, nf, maxc, bound_r);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s out.c out.h name=file.obj ...\n", argv[0]);
        return 1;
    }
    FILE *c = fopen(argv[1], "w");
    FILE *h = fopen(argv[2], "w");
    if (!c || !h) { perror("output"); return 1; }
    fprintf(c, "/* GENERATED by obj2mesh — do not edit. */\n"
               "#include \"r3d_mesh.h\"\n\n");
    fprintf(h, "/* GENERATED by obj2mesh — do not edit. */\n"
               "#ifndef MESHES_GEN_H\n#define MESHES_GEN_H\n"
               "#include \"r3d_mesh.h\"\n\n");
    for (int i = 3; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq) { fprintf(stderr, "bad arg %s\n", argv[i]); return 1; }
        *eq = 0;
        if (!emit_mesh(c, h, argv[i], eq + 1)) return 1;
    }
    fprintf(h, "\n#endif\n");
    fclose(c);
    fclose(h);
    return 0;
}
