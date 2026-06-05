/*
 * ThumbyElite — projectile pool.
 *
 * Segment-vs-bounding-sphere collision per tick (fast rounds can't skip
 * through hulls). Homing missiles chase the shooter's locked target with
 * a capped turn rate and leave an ember trail. AoE rounds detonate via
 * combat_explosion_damage (missiles hurt everyone nearby — including
 * careless owners at point blank).
 */
#include "elite_proj.h"
#include "elite_entity.h"
#include "elite_combat.h"
#include "r3d_scene.h"
#include "r3d_fx.h"
#include "elite_audio.h"
#include "elite_types.h"

#define MAX_PROJ 28

typedef struct {
    bool  alive;
    uint8_t type;
    int8_t owner;
    int8_t target;        /* homing only */
    Vec3  pos, vel;
    float life;
    float trail_accum;
    float dmg_mult;
} Proj;

static Proj s_proj[MAX_PROJ];

void proj_init(void) {
    for (int i = 0; i < MAX_PROJ; i++) s_proj[i].alive = false;
}

int proj_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROJ; i++) if (s_proj[i].alive) n++;
    return n;
}

void proj_spawn(WeaponType type, int owner, int8_t target,
                Vec3 pos, Vec3 dir, Vec3 inherit_vel) {
    proj_spawn_ex(type, owner, target, pos, dir, inherit_vel, 1.0f);
}

void proj_spawn_ex(WeaponType type, int owner, int8_t target,
                   Vec3 pos, Vec3 dir, Vec3 inherit_vel, float dmg_mult) {
    const WeaponDef *w = &k_weapons[type];
    for (int i = 0; i < MAX_PROJ; i++) {
        if (s_proj[i].alive) continue;
        Proj *p = &s_proj[i];
        p->alive = true;
        p->type = (uint8_t)type;
        p->owner = (int8_t)owner;
        p->target = target;
        p->pos = pos;
        p->vel = v3_add(v3_scale(dir, w->speed), inherit_vel);
        p->life = w->range / w->speed;
        p->trail_accum = 0;
        p->dmg_mult = dmg_mult;
        return;
    }
}

/* Segment (a -> a+seg) vs sphere: smallest hit t in [0,1], or -1. */
static float seg_sphere(Vec3 a, Vec3 seg, Vec3 c, float r) {
    Vec3 m = v3_sub(a, c);
    float A = v3_dot(seg, seg);
    if (A < 1e-9f) return -1.0f;
    float B = 2.0f * v3_dot(m, seg);
    float C = v3_dot(m, m) - r * r;
    float disc = B * B - 4.0f * A * C;
    if (disc < 0.0f) return -1.0f;
    float t = (-B - sqrtf(disc)) / (2.0f * A);
    if (t < 0.0f || t > 1.0f) return -1.0f;
    return t;
}

static void detonate(Proj *p) {
    const WeaponDef *w = &k_weapons[p->type];
    if (w->aoe > 0) {
        fx_spawn_explosion(p->pos, v3(0, 0, 0));
        float d = v3_len(v3_sub(p->pos, g_ships[PLAYER].pos));
        sfx_explosion(1.0f - d / 700.0f, 0.5f);
        combat_explosion_damage(p->owner, p->pos, w->aoe,
                                w->dmg * p->dmg_mult);
    } else {
        fx_spawn_spark(p->pos, v3(0, 0, 0));
    }
    p->alive = false;
}

void proj_tick(float dt) {
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *p = &s_proj[i];
        if (!p->alive) continue;
        const WeaponDef *w = &k_weapons[p->type];

        p->life -= dt;
        if (p->life <= 0.0f) { p->alive = false; continue; }

        /* Homing guidance: steer velocity toward the target. */
        if (w->turn > 0 && p->target >= 0 && g_ships[p->target].alive) {
            Vec3 want = v3_norm(v3_sub(g_ships[p->target].pos, p->pos));
            Vec3 cur = v3_norm(p->vel);
            Vec3 axis = v3_cross(cur, want);
            float sin_a = v3_len(axis);
            if (sin_a > 1e-4f) {
                float step = w->turn * dt;
                float ang = asinf(sin_a > 1 ? 1 : sin_a);
                if (step > ang) step = ang;
                Mat3 b = { { cur, v3(0, 0, 0), v3(0, 0, 0) } };
                m3_rotate_world(&b, v3_scale(axis, 1.0f / sin_a), step);
                p->vel = v3_scale(b.r[0], w->speed);
            }
        }

        Vec3 seg = v3_scale(p->vel, dt);

        /* Collision against every other ship. */
        float best_t = 2.0f;
        int hit = -1;
        for (int s = 0; s < MAX_SHIPS; s++) {
            if (s == p->owner || !g_ships[s].alive) continue;
            float t = seg_sphere(p->pos, seg, g_ships[s].pos,
                                 g_ships[s].mesh->bound_r * 0.9f);
            if (t >= 0.0f && t < best_t) { best_t = t; hit = s; }
        }
        if (hit >= 0) {
            p->pos = v3_add(p->pos, v3_scale(seg, best_t));
            if (w->aoe > 0) {
                detonate(p);
            } else {
                combat_direct_damage(p->owner, hit,
                                     w->dmg * p->dmg_mult, p->pos);
                p->alive = false;
            }
            continue;
        }

        p->pos = v3_add(p->pos, seg);

        /* Missile exhaust trail. */
        if (w->aoe > 0) {
            p->trail_accum += dt * 28.0f;
            while (p->trail_accum >= 1.0f) {
                p->trail_accum -= 1.0f;
                fx_engine_trail(p->pos, v3_scale(p->vel, 0.1f), 1.0f, 0.04f);
            }
        }
    }
}

void proj_emit(Vec3 cam_pos) {
    for (int i = 0; i < MAX_PROJ; i++) {
        const Proj *p = &s_proj[i];
        if (!p->alive) continue;
        const WeaponDef *w = &k_weapons[p->type];
        float sx, sy;
        uint16_t d;
        if (!r3d_scene_project(v3_sub(p->pos, cam_pos), &sx, &sy, &d))
            continue;
        if (sx < -6 || sx > 134 || sy < -6 || sy > 134) continue;

        if (p->type == WPN_GAUSS || p->type == WPN_AUTOCANNON) {
            /* Tracer: short line back along the velocity. */
            Vec3 tail = v3_sub(p->pos, v3_scale(p->vel, 0.02f));
            float tx, ty;
            uint16_t td;
            if (r3d_scene_project(v3_sub(tail, cam_pos), &tx, &ty, &td))
                r3d_scene_add_line(sx, sy, d, tx, ty, td, w->color);
            else
                r3d_scene_add_point(sx, sy, d, w->color, 1);
        } else {
            /* Bolt: chunky glowing point (nearer = bigger). */
            uint8_t size = (d > 1500) ? 3 : (d > 400) ? 2 : 1;
            r3d_scene_add_point(sx, sy, d, w->color, size);
        }
    }
}
