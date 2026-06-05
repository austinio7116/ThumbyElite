/*
 * ThumbyElite — ship entity pool.
 */
#include "elite_entity.h"
#include "elite_ships.h"
#include <string.h>

Ship g_ships[MAX_SHIPS];

void ships_init(void) {
    memset(g_ships, 0, sizeof g_ships);
    for (int i = 0; i < MAX_SHIPS; i++) g_ships[i].target = -1;
}

static void ship_defaults(Ship *s, const Mesh *mesh) {
    s->mesh = mesh;
    s->basis = m3_identity();
    s->vel = v3(0, 0, 0);
    s->throttle = 0;
    s->assist = true;
    s->boost_t = 0;
    /* Stats roughly by hull size (placeholder until outfitting, Phase 7):
     * bigger bounding radius -> slower, tougher. */
    float k = mesh->bound_r / 5.0f;        /* fighter ~1.3, freighter ~1.4 */
    s->max_speed = 110.0f / k;
    s->accel = 55.0f / k;
    s->turn_rate = 2.2f / k;
    s->hull_max = 45.0f * k;
    s->shield_max = 30.0f * k;
    s->heat = 0;
    s->fire_cool = 0;
    s->ai_state = AI_NONE;
    s->ai_timer = 0;
    s->target = -1;
}

int ship_spawn(const Mesh *mesh, Vec3 pos, uint8_t team) {
    for (int i = 1; i < MAX_SHIPS; i++) {
        if (g_ships[i].alive) continue;
        Ship *s = &g_ships[i];
        memset(s, 0, sizeof *s);
        ship_defaults(s, mesh);
        s->alive = true;
        s->pos = pos;
        s->team = team;
        s->hull = s->hull_max;
        s->shield = s->shield_max;
        return i;
    }
    return -1;
}

void ships_despawn_npcs(void) {
    for (int i = 1; i < MAX_SHIPS; i++) g_ships[i].alive = false;
}

const char *k_tier_names[5] = {
    "HARMLESS", "NOVICE", "CAPABLE", "DEADLY", "ELITE",
};

void ship_fit_weapon(int idx, int mount, WeaponType w) {
    Ship *s = &g_ships[idx];
    if (mount >= MAX_HARDPOINTS) return;
    s->weapons[mount] = (uint8_t)w;
    s->ammo[mount] = k_weapons[w].ammo_max ? (int16_t)k_weapons[w].ammo_max
                                           : -1;
    if (mount >= s->n_weapons) s->n_weapons = (uint8_t)(mount + 1);
}

void ship_set_tier(int idx, int tier, int hull_class) {
    Ship *s = &g_ships[idx];
    if (tier < 0) tier = 0;
    if (tier > 4) tier = 4;
    if (hull_class < 0) hull_class = 0;
    if (hull_class >= N_HULLS) hull_class = N_HULLS - 1;
    s->tier = (uint8_t)tier;
    /* Class template stats, scaled down for NPCs and up with tier. */
    const HullDef *h = &k_hulls[hull_class];
    float k = 1.0f + 0.13f * (float)tier;
    s->max_speed = h->max_speed * (0.82f + 0.10f * (float)tier);
    s->accel = h->accel;
    s->turn_rate = h->turn_rate * (0.80f + 0.13f * (float)tier);
    s->hull_max = h->hull_base * 0.55f * k;
    s->hull = s->hull_max;
    s->shield_max = h->shield_base * 0.55f * k;
    s->shield = s->shield_max;
    /* Loadout by tier. */
    s->n_weapons = 0;
    s->active_w = 0;
    switch (tier) {
    case 0:
    case 1: ship_fit_weapon(idx, 0, WPN_PULSE_S); break;
    case 2:
        ship_fit_weapon(idx, 0, (idx & 1) ? WPN_AUTOCANNON : WPN_PULSE_M);
        break;
    case 3:
        ship_fit_weapon(idx, 0, WPN_PULSE_M);
        ship_fit_weapon(idx, 1, WPN_GAUSS);
        break;
    default:
        ship_fit_weapon(idx, 0, WPN_PULSE_L);
        ship_fit_weapon(idx, 1, (idx & 1) ? WPN_PHOTON : WPN_HOMING);
        break;
    }
}

int ships_alive_hostile(void) {
    int n = 0;
    for (int i = 1; i < MAX_SHIPS; i++)
        if (g_ships[i].alive && g_ships[i].team == TEAM_HOSTILE) n++;
    return n;
}
