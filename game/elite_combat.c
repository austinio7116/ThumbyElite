/*
 * ThumbyElite — weapons, damage, death.
 *
 * Phase 3 weapon: pulse laser. Hitscan ray vs ship bounding spheres,
 * shields absorb before hull, beams + sparks for feedback, explosion +
 * pool release on kill. Heat: each shot adds heat; above HEAT_MAX the
 * weapon cuts out until it cools.
 */
#include "elite_combat.h"
#include "r3d_fx.h"
#include "elite_types.h"

#define LASER_RANGE   600.0f
#define LASER_DMG     12.0f    /* player guns */
#define LASER_DMG_AI   5.0f    /* hostile guns hit softer (survivability) */
#define LASER_COOL    0.16f
#define LASER_HEAT    5.5f
#define HEAT_MAX      100.0f
#define HEAT_DISSIPATE 22.0f
#define SHIELD_REGEN  6.0f
#define SHIELD_DELAY  3.0f     /* s after a hit before regen resumes */

static float s_regen_hold[MAX_SHIPS];
static int   s_kills;

void combat_init(void) {
    for (int i = 0; i < MAX_SHIPS; i++) s_regen_hold[i] = 0;
    s_kills = 0;
}

int combat_kills(void) { return s_kills; }

bool combat_can_fire(const Ship *s) {
    return s->fire_cool <= 0.0f && s->heat < HEAT_MAX;
}

/* Ray (o, dir unit) vs sphere (c, r): nearest positive t, or -1. */
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

static void apply_damage(int victim, float dmg, Vec3 hit_pos) {
    Ship *v = &g_ships[victim];
    s_regen_hold[victim] = SHIELD_DELAY;
    if (v->shield > 0.0f) {
        v->shield -= dmg;
        if (v->shield < 0.0f) { v->hull += v->shield; v->shield = 0.0f; }
    } else {
        v->hull -= dmg;
    }
    fx_spawn_spark(hit_pos, v->vel);
    if (v->hull <= 0.0f) {
        v->alive = false;
        fx_spawn_explosion(v->pos, v->vel);
        if (victim != PLAYER) s_kills++;
    }
}

int combat_fire_laser(int shooter, float spread) {
    Ship *s = &g_ships[shooter];
    if (!combat_can_fire(s)) return -1;
    s->fire_cool = LASER_COOL;
    s->heat += LASER_HEAT;

    Vec3 dir = s->basis.r[2];
    if (spread > 0.0f) {
        /* Perturb within a cone using the ship's own frame. */
        uint32_t r = (uint32_t)(s->pos.x * 131.0f + s->pos.z * 17.0f)
                     ^ (uint32_t)(s->fire_cool * 1e6f) ^ (uint32_t)shooter;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        float a = ((r & 0xFF) / 255.0f - 0.5f) * 2.0f * spread;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        float b = ((r & 0xFF) / 255.0f - 0.5f) * 2.0f * spread;
        dir = v3_norm(v3_add(dir, v3_add(v3_scale(s->basis.r[0], a),
                                         v3_scale(s->basis.r[1], b))));
    }

    /* Aim ray from the ship centre (fair hit detection)... */
    int best = -1;
    float best_t = LASER_RANGE;
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (i == shooter || !g_ships[i].alive) continue;
        float t = ray_sphere(s->pos, dir, g_ships[i].pos,
                             g_ships[i].mesh->bound_r * 0.85f);
        if (t >= 0.0f && t < best_t) { best_t = t; best = i; }
    }
    Vec3 end = v3_add(s->pos, v3_scale(dir, best >= 0 ? best_t : LASER_RANGE));

    /* ...but the VISIBLE beam starts at a gun on the hull. The player's
     * fixed mounts alternate left/right wing — on screen the bolts climb
     * from the lower corners and converge on the crosshair (Elite-style).
     * AI beams just leave the nose. */
    Vec3 muzzle;
    if (shooter == PLAYER) {
        /* Offsets chosen so the muzzles project to the lower screen
         * corners (~(20,120)/(108,120) at 60deg FOV). */
        static int gun = 0;
        gun ^= 1;
        Vec3 off = v3(gun ? 1.6f : -1.6f, -2.0f, 4.0f);
        muzzle = v3_add(s->pos, m3_mul_v3(&s->basis, off));
    } else {
        muzzle = v3_add(s->pos, v3_scale(dir, s->mesh->bound_r * 0.9f));
    }

    uint16_t col = (s->team == TEAM_PLAYER) ? RGB565C(255, 80, 60)
                                            : RGB565C(80, 255, 90);
    fx_beam(muzzle, end, col);
    float dmg = (s->team == TEAM_PLAYER) ? LASER_DMG : LASER_DMG_AI;
    if (best >= 0) apply_damage(best, dmg, end);
    return best;
}

void combat_tick(float dt) {
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
