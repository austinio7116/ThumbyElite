/*
 * ThumbyElite — weapons fire, damage, death.
 *
 * Hitscan weapons (lasers/beam) resolve instantly with a visible beam;
 * projectile weapons hand off to the proj pool. Shields absorb before
 * hull, regen slowly after a quiet spell; heat gates sustained fire.
 */
#include "elite_combat.h"
#include "elite_proj.h"
#include "r3d_fx.h"
#include "elite_types.h"

#define HEAT_MAX       100.0f
#define HEAT_DISSIPATE 22.0f
#define SHIELD_REGEN   3.0f     /* slow — a collapsed shield matters */
#define SHIELD_DELAY   4.5f     /* s after a hit before regen resumes */

static float s_regen_hold[MAX_SHIPS];
static int   s_kills;
static float s_hitmark, s_killmark;

void combat_init(void) {
    for (int i = 0; i < MAX_SHIPS; i++) s_regen_hold[i] = 0;
    s_kills = 0;
    s_hitmark = s_killmark = 0;
    proj_init();
}

int combat_kills(void) { return s_kills; }
float combat_hitmarker(void) { return s_hitmark; }
float combat_killmarker(void) { return s_killmark; }

bool combat_can_fire(const Ship *s) {
    if (s->fire_cool > 0.0f || s->heat >= HEAT_MAX) return false;
    if (s->n_weapons == 0) return false;
    const WeaponDef *w = &k_weapons[s->weapons[s->active_w]];
    if (w->ammo_max && s->ammo[s->active_w] <= 0) return false;
    return true;
}

/* Ray (o, dir unit) vs sphere: nearest positive t, or -1. */
static float ray_sphere(Vec3 o, Vec3 dir, Vec3 c, float r) {
    Vec3 oc = v3_sub(c, o);
    float tca = v3_dot(oc, dir);
    if (tca < 0.0f) return -1.0f;
    float d2 = v3_len2(oc) - tca * tca;
    if (d2 > r * r) return -1.0f;
    float thc = sqrtf(r * r - d2);
    float t = tca - thc;
    return (t >= 0.0f) ? t : tca + thc;
}

void combat_direct_damage(int shooter, int victim, float dmg, Vec3 hit_pos) {
    Ship *v = &g_ships[victim];
    if (!v->alive) return;
    s_regen_hold[victim] = SHIELD_DELAY;
    if (v->shield > 0.0f) {
        v->shield -= dmg;
        if (v->shield < 0.0f) { v->hull += v->shield; v->shield = 0.0f; }
    } else {
        v->hull -= dmg;
    }
    fx_spawn_spark(hit_pos, v->vel);
    if (shooter == PLAYER) s_hitmark = 0.12f;
    if (v->hull <= 0.0f) {
        v->alive = false;
        fx_spawn_explosion(v->pos, v->vel);
        if (victim != PLAYER) s_kills++;
        if (shooter == PLAYER) s_killmark = 0.7f;
    }
}

void combat_explosion_damage(int shooter, Vec3 centre, float radius,
                             float dmg) {
    for (int i = 0; i < MAX_SHIPS; i++) {
        Ship *v = &g_ships[i];
        if (!v->alive) continue;
        float d = v3_len(v3_sub(v->pos, centre)) - v->mesh->bound_r * 0.5f;
        if (d < 0) d = 0;
        if (d > radius) continue;
        float k = 1.0f - 0.7f * (d / radius);    /* 100% .. 30% falloff */
        combat_direct_damage(shooter, i, dmg * k, v->pos);
    }
}

int combat_fire(int shooter, float spread, int target) {
    Ship *s = &g_ships[shooter];
    if (!combat_can_fire(s)) return -1;
    const WeaponDef *w = &k_weapons[s->weapons[s->active_w]];

    s->fire_cool = w->cooldown;
    s->heat += w->heat;
    if (w->ammo_max) s->ammo[s->active_w]--;

    Vec3 dir = s->basis.r[2];
    if (spread > 0.0f) {
        uint32_t r = (uint32_t)(s->pos.x * 131.0f + s->pos.z * 17.0f)
                     ^ (uint32_t)(s->heat * 1e3f) ^ (uint32_t)shooter;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        float a = ((r & 0xFF) / 255.0f - 0.5f) * 2.0f * spread;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        float b = ((r & 0xFF) / 255.0f - 0.5f) * 2.0f * spread;
        dir = v3_norm(v3_add(dir, v3_add(v3_scale(s->basis.r[0], a),
                                         v3_scale(s->basis.r[1], b))));
    }

    /* Visible muzzle: player's wing mounts alternate; AI fires from the
     * nose. */
    Vec3 muzzle;
    if (shooter == PLAYER) {
        static int gun = 0;
        gun ^= 1;
        Vec3 off = v3(gun ? 1.6f : -1.6f, -2.0f, 4.0f);
        muzzle = v3_add(s->pos, m3_mul_v3(&s->basis, off));
    } else {
        muzzle = v3_add(s->pos, v3_scale(dir, s->mesh->bound_r * 0.9f));
    }

    /* Projectile weapons: hand off and we're done. */
    if (w->speed > 0.0f) {
        proj_spawn((WeaponType)s->weapons[s->active_w], shooter,
                   (int8_t)target, muzzle, dir, s->vel);
        return -1;
    }

    /* Hitscan: aim ray from the ship centre (fair), beam from the gun. */
    int best = -1;
    float best_t = w->range;
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (i == shooter || !g_ships[i].alive) continue;
        float t = ray_sphere(s->pos, dir, g_ships[i].pos,
                             g_ships[i].mesh->bound_r * 0.85f);
        if (t >= 0.0f && t < best_t) { best_t = t; best = i; }
    }
    Vec3 end = v3_add(s->pos, v3_scale(dir, best >= 0 ? best_t : w->range));
    fx_beam(muzzle, end, w->color);
    if (best >= 0) combat_direct_damage(shooter, best, w->dmg, end);
    return best;
}

void combat_tick(float dt) {
    if (s_hitmark > 0) s_hitmark -= dt;
    if (s_killmark > 0) s_killmark -= dt;
    proj_tick(dt);
    for (int i = 0; i < MAX_SHIPS; i++) {
        Ship *s = &g_ships[i];
        if (!s->alive) continue;
        if (s->fire_cool > 0.0f) s->fire_cool -= dt;
        s->heat -= HEAT_DISSIPATE * dt;
        if (s->heat < 0.0f) s->heat = 0.0f;
        if (s_regen_hold[i] > 0.0f) {
            s_regen_hold[i] -= dt;
        } else if (s->shield < s->shield_max) {
            s->shield += SHIELD_REGEN * dt;
            if (s->shield > s->shield_max) s->shield = s->shield_max;
        }
    }
}
