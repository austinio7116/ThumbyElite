/*
 * ThumbyElite — ship entity pool.
 */
#include "elite_entity.h"
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

int ships_alive_hostile(void) {
    int n = 0;
    for (int i = 1; i < MAX_SHIPS; i++)
        if (g_ships[i].alive && g_ships[i].team == TEAM_HOSTILE) n++;
    return n;
}
