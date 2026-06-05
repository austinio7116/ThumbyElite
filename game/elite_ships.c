/*
 * ThumbyElite — purchasable hull catalogue.
 */
#include "elite_ships.h"
#include "meshes_gen.h"

const HullDef k_hulls[N_HULLS] = {
    /* name        mesh              price  ns slots      cargo sT hT  speed accel turn  hull shld */
    { "SKIFF",     &mesh_shuttle,      900, 1, {1, 0, 0},   6,  1, 1,   85,  40, 1.5f,   70,  50 },
    { "DART",      &mesh_courier,     3200, 1, {2, 0, 0},   4,  1, 1,  150,  85, 2.3f,   60,  55 },
    { "SPARROW",   &mesh_fighter,     8500, 2, {2, 1, 0},   8,  2, 2,  120,  60, 2.1f,  100,  80 },
    { "VIPER",     &mesh_viper,      16000, 2, {2, 2, 0},   6,  2, 2,  135,  70, 2.4f,  110,  95 },
    { "REAVER",    &mesh_cutter,     24000, 3, {2, 2, 1},  10,  2, 3,  125,  62, 2.2f,  130, 100 },
    { "MAULER",    &mesh_mauler,     42000, 3, {3, 2, 2},  10,  3, 3,  110,  55, 1.9f,  170, 140 },
    { "PACK MULE", &mesh_lighthauler, 7000, 1, {1, 0, 0},  22,  1, 2,   80,  35, 1.2f,  110,  70 },
    { "MULE",      &mesh_freighter,  21000, 2, {2, 1, 0},  44,  2, 2,   70,  30, 1.0f,  150, 100 },
    { "ATLAS",     &mesh_hauler,     58000, 2, {2, 2, 0},  90,  2, 3,   60,  25, 0.8f,  210, 130 },
    { "BASILISK",  &mesh_dread,     130000, 3, {3, 3, 2},  30,  3, 3,   95,  45, 1.4f,  280, 220 },
};

const float k_tier_mult[4] = { 1.0f, 1.3f, 1.6f, 2.0f };

int upgrade_price(int hull_id, int tier) {
    /* Tier 1/2/3 cost ~8/16/28% of the hull price. */
    static const int pct[4] = { 0, 8, 16, 28 };
    if (tier < 1 || tier > 3) return 0;
    int p = (int)((int64_t)k_hulls[hull_id].price * pct[tier] / 100);
    return p < 200 ? 200 : p;
}
