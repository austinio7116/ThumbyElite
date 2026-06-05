/*
 * ThumbyElite — purchasable hull catalogue.
 */
#include "elite_ships.h"
#include "meshes_gen.h"

const HullDef k_hulls[N_HULLS] = {
    /* name        mesh              price  ns slots      cargo sT hT  speed accel turn  hull shld  jump */
    { "SKIFF",     &mesh_shuttle,      900, 1, {1, 0, 0},   8,  1, 1,   85,  40, 1.5f,   70,  50,  6.5f, 2 },
    { "DART",      &mesh_courier,     3200, 1, {2, 0, 0},   3,  1, 1,  150,  85, 2.3f,   60,  55, 7.0f, 1 },
    { "SPARROW",   &mesh_fighter,     8500, 2, {2, 1, 0},  10,  2, 2,  120,  60, 2.1f,  100,  80,  8.0f, 3 },
    { "VIPER",     &mesh_viper,      16000, 2, {2, 2, 0},   6,  2, 2,  135,  70, 2.4f,  110,  95,  8.5f, 2 },
    { "REAVER",    &mesh_cutter,     24000, 3, {2, 2, 1},  16,  2, 3,  125,  62, 2.2f,  130, 100,  9.5f, 5 },
    { "MAULER",    &mesh_mauler,     42000, 3, {3, 2, 2},  12,  3, 3,  110,  55, 1.9f,  170, 140,  10.0f, 4 },
    { "PACK MULE", &mesh_lighthauler, 7000, 1, {1, 0, 0},  32,  1, 2,   80,  35, 1.2f,  110,  70,  7.5f, 4 },
    { "MULE",      &mesh_freighter,  21000, 2, {2, 1, 0},  64,  2, 2,   70,  30, 1.0f,  150, 100,  9.0f, 6 },
    { "ATLAS",     &mesh_hauler,     58000, 2, {2, 2, 0}, 140,  2, 3,   60,  25, 0.8f,  210, 130,  11.0f, 8 },
    { "BASILISK",  &mesh_dread,     130000, 3, {3, 3, 2},  40,  3, 3,   95,  45, 1.4f,  280, 220,  12.5f, 6 },
};

const float k_tier_mult[4] = { 1.0f, 1.3f, 1.6f, 2.0f };

int upgrade_price(int hull_id, int tier) {
    /* Tier 1/2/3 cost ~8/16/28% of the hull price. */
    static const int pct[4] = { 0, 8, 16, 28 };
    if (tier < 1 || tier > 3) return 0;
    int p = (int)((int64_t)k_hulls[hull_id].price * pct[tier] / 100);
    return p < 200 ? 200 : p;
}

/* --- procedural hull mesh cache ----------------------------------------*/
#include "ship_gen.h"
#include <string.h>

#define HULL_CACHE_N 8
#define HC_MAX_V 220
#define HC_MAX_F 400

typedef struct {
    uint8_t  used;
    uint32_t seed;
    int8_t   hint;
    MeshVert verts[HC_MAX_V];
    MeshFace faces[HC_MAX_F];
    Mesh     mesh;
} HullCacheEntry;

static HullCacheEntry s_hc[HULL_CACHE_N];
static int s_hc_next;

const Mesh *hull_mesh(uint32_t mesh_seed, int class_hint) {
    for (int i = 0; i < HULL_CACHE_N; i++)
        if (s_hc[i].used && s_hc[i].seed == mesh_seed &&
            s_hc[i].hint == (int8_t)class_hint)
            return &s_hc[i].mesh;
    /* Fill the next slot (round-robin; reset() handles live refs). */
    for (int tries = 0; tries < HULL_CACHE_N; tries++) {
        HullCacheEntry *e = &s_hc[s_hc_next];
        s_hc_next = (s_hc_next + 1) % HULL_CACHE_N;
        if (e->used == 2) continue;            /* pinned (player) */
        ship_gen_mesh_class(mesh_seed, class_hint);
        ship_gen_copy(e->verts, HC_MAX_V, e->faces, HC_MAX_F, &e->mesh);
        e->used = 1;
        e->seed = mesh_seed;
        e->hint = (int8_t)class_hint;
        return &e->mesh;
    }
    return &s_hc[0].mesh;
}

void hull_cache_reset(const Mesh *keep) {
    for (int i = 0; i < HULL_CACHE_N; i++) {
        if (!s_hc[i].used) continue;
        s_hc[i].used = (keep == &s_hc[i].mesh) ? 2 : 0;
    }
}
