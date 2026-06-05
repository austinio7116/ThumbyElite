/*
 * ThumbyElite — wreck salvage.
 *
 * Kills drop tumbling canisters: commodities (into the cargo hold) or
 * damaged weapon components (into the salvage rack — repair, fit or
 * sell them at a station: the MechWarrior Mercenaries loop). Fly within
 * scoop range to collect.
 */
#include "elite_loot.h"
#include "elite_entity.h"
#include "elite_player.h"
#include "r3d_scene.h"
#include "r3d_fx.h"
#include "econ.h"
#include "meshes_gen.h"
#include <stdio.h>

#define MAX_CANS   6
#define SCOOP_RANGE 22.0f
#define CAN_LIFE   45.0f

typedef struct {
    bool  alive;
    Vec3  pos, vel;
    float spin, life;
    uint8_t is_component;
    WeaponInst comp;       /* component drops */
    uint8_t good, count;   /* commodity drops */
} Canister;

static Canister s_cans[MAX_CANS];
static char s_toast[28];

static uint32_t s_rng = 0x10075EEDu;
static uint32_t rnd(void) {
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return s_rng;
}

void loot_init(void) {
    for (int i = 0; i < MAX_CANS; i++) s_cans[i].alive = false;
}

void loot_on_kill(Vec3 pos, Vec3 vel, int tier) {
    if ((rnd() % 100u) >= 60) return;          /* 60% drop chance */
    for (int i = 0; i < MAX_CANS; i++) {
        if (s_cans[i].alive) continue;
        Canister *c = &s_cans[i];
        c->alive = true;
        c->pos = pos;
        c->vel = v3_add(v3_scale(vel, 0.3f),
                        v3((float)(rnd() % 11) - 5.0f,
                           (float)(rnd() % 11) - 5.0f,
                           (float)(rnd() % 11) - 5.0f));
        c->spin = 0.5f + (float)(rnd() % 100) * 0.015f;
        c->life = CAN_LIFE;
        /* Component odds rise with the victim's tier (better pilots fly
         * better-kept gear). */
        c->is_component = (rnd() % 100u) < (uint32_t)(30 + tier * 10);
        if (c->is_component) {
            c->comp.type = (uint8_t)(rnd() % WPN_COUNT);
            int q = (int)(rnd() % 100u);
            c->comp.quality = (q < 50) ? Q_SALVAGED
                            : (q < 80) ? Q_STANDARD
                            : (q < 93) ? Q_REINFORCED
                            : (q < 99) ? Q_MILITARY : Q_PROTOTYPE;
            if (tier >= 3 && c->comp.quality < Q_STANDARD)
                c->comp.quality = Q_STANDARD;
            c->comp.integrity = (uint8_t)(20 + rnd() % 55);
            c->comp.in_use = 1;
        } else {
            c->good = (uint8_t)(rnd() % 16u);   /* legal goods only */
            c->count = (uint8_t)(1 + rnd() % 3u);
        }
        return;
    }
}

const char *loot_tick(float dt) {
    const char *toast = NULL;
    Ship *p = &g_ships[PLAYER];
    for (int i = 0; i < MAX_CANS; i++) {
        Canister *c = &s_cans[i];
        if (!c->alive) continue;
        c->life -= dt;
        if (c->life <= 0) { c->alive = false; continue; }
        c->pos = v3_add(c->pos, v3_scale(c->vel, dt));

        if (!p->alive) continue;
        if (v3_len2(v3_sub(c->pos, p->pos)) > SCOOP_RANGE * SCOOP_RANGE)
            continue;

        if (c->is_component) {
            /* Into the salvage rack (takes a cargo slot). */
            int slot = -1;
            for (int s = 0; s < MAX_SALVAGE; s++)
                if (!g_player.salvage[s].in_use) { slot = s; break; }
            if (slot < 0 || player_cargo_total() >= player_cargo_cap()) {
                snprintf(s_toast, sizeof s_toast, "HOLD FULL");
                toast = s_toast;
                continue;
            }
            g_player.salvage[slot] = c->comp;
            g_player.xp_tech += 2;
            snprintf(s_toast, sizeof s_toast, "SALVAGED %s",
                     k_weapons[c->comp.type].name);
        } else {
            int room = player_cargo_cap() - player_cargo_total();
            if (room <= 0) {
                snprintf(s_toast, sizeof s_toast, "HOLD FULL");
                toast = s_toast;
                continue;
            }
            int take = c->count < room ? c->count : room;
            g_player.cargo[c->good] += (uint8_t)take;
            snprintf(s_toast, sizeof s_toast, "+%d %s", take,
                     k_goods[c->good].name);
        }
        toast = s_toast;
        c->alive = false;
        fx_spawn_spark(c->pos, p->vel);
    }
    return toast;
}

void loot_render(Vec3 cam_pos) {
    extern float elite_game_time(void);
    float t = elite_game_time();
    for (int i = 0; i < MAX_CANS; i++) {
        const Canister *c = &s_cans[i];
        if (!c->alive) continue;
        R3DObject obj;
        obj.mesh = &mesh_canister;
        obj.basis = m3_identity();
        m3_rotate_local(&obj.basis, 1, t * c->spin);
        m3_rotate_local(&obj.basis, 0, t * c->spin * 0.7f);
        obj.pos = v3_sub(c->pos, cam_pos);
        r3d_scene_add_object(&obj);
    }
}
