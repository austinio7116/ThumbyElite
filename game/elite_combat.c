/*
 * ThumbyElite — weapons fire, damage, death.
 *
 * Hitscan weapons (lasers/beam) resolve instantly with a visible beam;
 * projectile weapons hand off to the proj pool. Shields absorb before
 * hull, regen slowly after a quiet spell; heat gates sustained fire.
 */
#include "elite_combat.h"
#include "elite_proj.h"
#include "elite_player.h"
#include "elite_loot.h"
#include "mission.h"
#include "r3d_fx.h"
#include "elite_audio.h"
#include "elite_platform.h"
#include "elite_types.h"

#define HEAT_MAX       100.0f
#define HEAT_DISSIPATE 22.0f
#define SHIELD_REGEN   3.0f     /* slow — a collapsed shield matters */
#define SHIELD_DELAY   4.5f     /* s after a hit before regen resumes */

static float s_regen_hold[MAX_SHIPS];
static int   s_kills;
static float s_hitmark, s_killmark;
static int   s_kill_pay;

void combat_init(void) {
    for (int i = 0; i < MAX_SHIPS; i++) s_regen_hold[i] = 0;
    s_kills = 0;
    s_hitmark = s_killmark = 0;
    proj_init();
}

int combat_kills(void) { return s_kills; }
void combat_set_kills(int n) { s_kills = n; }

int combat_take_kill_pay(void) {
    int p = s_kill_pay;
    s_kill_pay = 0;
    return p;
}
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
    bool had_shield = v->shield > 0.0f;
    if (v->shield > 0.0f) {
        v->shield -= dmg;
        if (v->shield < 0.0f) { v->hull += v->shield; v->shield = 0.0f; }
    } else {
        v->hull -= dmg;
    }
    fx_spawn_spark(hit_pos, v->vel);
    if (victim == PLAYER) {
        /* Feel it: soft buzz for shields, hard thump for hull. */
        if (had_shield) {
            plat_rumble(0.28f, 0.08f);
            sfx_hit_shield();
        } else {
            plat_rumble(0.60f, 0.16f);
            sfx_hit_hull();
        }
    }
    if (shooter == PLAYER) s_hitmark = 0.12f;
    if (v->hull <= 0.0f) {
        v->alive = false;
        fx_spawn_explosion(v->pos, v->vel);
        {
            float d = v3_len(v3_sub(v->pos, g_ships[PLAYER].pos));
            float amp = 1.0f - d / 700.0f;
            if (victim == PLAYER) amp = 1.0f;
            sfx_explosion(amp, v->mesh->bound_r / 15.0f);
        }
        if (victim == PLAYER) plat_rumble(1.0f, 0.7f);
        if (victim != PLAYER) {
            s_kills++;
            loot_on_kill(v->pos, v->vel, v->tier);
            if (shooter == PLAYER)
                mission_on_kill(v->tier, v->is_mark != 0);
        }
        if (shooter == PLAYER) {
            s_killmark = 0.7f;
            if (victim != PLAYER) {
                g_player.xp_gunnery++;
                /* Lawful kill bounty: dangerous pilots pay big. */
                static const int k_pay[5] = { 25, 80, 220, 600, 1600 };
                int t = v->tier > 4 ? 4 : v->tier;
                s_kill_pay += k_pay[t];
                g_player.credits += k_pay[t];
            }
        }
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

    /* Player: component quality/integrity + gunnery skill modify output. */
    float dmg_mult = 1.0f, heat_mult = 1.0f;
    if (shooter == PLAYER) {
        const WeaponInst *wi = player_mount_for_ship_slot(s->active_w);
        if (wi) dmg_mult = mount_dmg_mult(wi);
        heat_mult = skill_heat_mult();
    }

    s->fire_cool = w->cooldown;
    s->heat += w->heat * heat_mult;
    if (w->ammo_max) {
        s->ammo[s->active_w]--;
        if (shooter == PLAYER)
            player_sync_ammo(s->active_w, s->ammo[s->active_w]);
    }

    {
        float amp = 1.0f;
        if (shooter != PLAYER) {
            float d = v3_len(v3_sub(s->pos, g_ships[PLAYER].pos));
            amp = 0.6f - d / 600.0f;
        }
        sfx_weapon(s->weapons[s->active_w], amp);
    }

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
        proj_spawn_ex((WeaponType)s->weapons[s->active_w], shooter,
                      (int8_t)target, muzzle, dir, s->vel, dmg_mult);
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
    if (best >= 0)
        combat_direct_damage(shooter, best, w->dmg * dmg_mult, end);
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
