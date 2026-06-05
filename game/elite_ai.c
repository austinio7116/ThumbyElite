/*
 * ThumbyElite — enemy AI.
 *
 * ATTACK turns toward the player and fires inside a skill-sized cone;
 * BREAK peels off after an overshoot. Tier (0 HARMLESS .. 4 ELITE)
 * drives reaction: aim spread, trigger rate, gun range — and, for
 * multi-mount ships, weapon choice by engagement distance (snipe with
 * the long gun, switch to lasers up close).
 */
#include "elite_ai.h"
#include "elite_entity.h"
#include "elite_combat.h"
#include "elite_weapons.h"

#define AI_BREAK_DIST  120.0f   /* break off the run EARLY — 28m read
                                   as flying straight through the player
                                   (user report); real pilots joust */
#define AI_BREAK_TIME  1.5f     /* quick turnaround between passes */

/* Per-tier trigger discipline. */
static const float k_refire[5] = { 0.80f, 0.70f, 0.60f, 0.75f, 0.42f };
/* Spread compensates PAYLOAD, not just tier — tier 3 packs gauss, so
 * its number sits wider than tier 2's; the curve that matters is the
 * siege-sim collapse times (target ~55/30/14/7/3s vs standard shield). */
static const float k_spread[5] = { 0.024f, 0.020f, 0.016f, 0.024f, 0.0105f };
static const float k_cone[5]   = { 0.970f, 0.978f, 0.984f, 0.988f, 0.992f };

/* Steer s so its nose tips toward world-space dir (unit). */
static void turn_toward(Ship *s, Vec3 dir, float dt) {
    Vec3 fwd = s->basis.r[2];
    Vec3 axis = v3_cross(fwd, dir);
    float sin_a = v3_len(axis);
    if (sin_a < 1e-4f) {
        if (v3_dot(fwd, dir) < 0.0f)
            m3_rotate_world(&s->basis, s->basis.r[1], s->turn_rate * dt);
        return;
    }
    float want = asinf(sin_a > 1.0f ? 1.0f : sin_a);
    if (v3_dot(fwd, dir) < 0.0f) want = 3.14159265f - want;
    float step = s->turn_rate * dt;
    if (step > want) step = want;
    m3_rotate_world(&s->basis, v3_scale(axis, 1.0f / sin_a), step);
    m3_orthonormalize(&s->basis);
}

/* Pick the mount that suits the range (longest-reach gun that's in
 * range and still has ammo; fall back to mount 0). */
static void choose_weapon(Ship *s, float dist) {
    int best = 0;
    float best_score = -1.0f;
    for (int i = 0; i < s->n_weapons; i++) {
        const WeaponDef *w = &k_weapons[s->weapons[i]];
        if (w->ammo_max && s->ammo[i] <= 0) continue;
        if (dist > w->range) continue;
        /* Prefer the tightest range fit: big guns far, fast guns close. */
        float score = (dist > 320.0f) ? w->range : (2000.0f - w->range);
        if (score > best_score) { best_score = score; best = i; }
    }
    s->active_w = (uint8_t)best;
}

static void ai_ship(int idx, float dt) {
    Ship *s = &g_ships[idx];
    Ship *t = &g_ships[PLAYER];
    if (!t->alive) { s->throttle = 0.25f; return; }

    Vec3 rel = v3_sub(t->pos, s->pos);
    float dist = v3_len(rel);
    Vec3 dir = (dist > 1e-3f) ? v3_scale(rel, 1.0f / dist) : s->basis.r[2];
    int tier = s->tier > 4 ? 4 : s->tier;

    switch (s->ai_state) {
    default:
    case AI_NONE:
        s->ai_state = AI_ATTACK;
        /* fallthrough */
    case AI_ATTACK: {
        turn_toward(s, dir, dt);
        s->throttle = (dist > 220.0f) ? 1.0f : (dist > 90.0f ? 0.7f : 0.45f);
        if (dist < AI_BREAK_DIST) {
            s->ai_state = AI_BREAK;
            s->ai_timer = AI_BREAK_TIME;
            break;
        }
        choose_weapon(s, dist);
        const WeaponDef *w = &k_weapons[s->weapons[s->active_w]];
        if (dist < w->range &&
            v3_dot(s->basis.r[2], dir) > k_cone[tier] &&
            combat_can_fire(s)) {
            /* Evasion is real: target's lateral speed widens the
             * effective spread — flying hard across the line of fire
             * dodges; sitting still gets you hit (and killed). */
            Vec3 latv = v3_sub(t->vel, v3_scale(dir, v3_dot(t->vel, dir)));
            float sp = k_spread[tier] * (1.0f + v3_len(latv) / 90.0f);
            combat_fire(idx, sp, PLAYER);
            s->fire_cool = k_refire[tier];
        }
        break;
    }
    case AI_BREAK:
        turn_toward(s, v3_norm(v3_add(v3_scale(dir, -1.0f),
                                      v3_scale(s->basis.r[1], 0.8f))), dt);
        s->throttle = 1.0f;
        s->ai_timer -= dt;
        if (s->ai_timer <= 0.0f) s->ai_state = AI_ATTACK;
        break;
    }
}

void ai_tick(float dt) {
    for (int i = 1; i < MAX_SHIPS; i++) {
        if (!g_ships[i].alive) continue;
        if (g_ships[i].team == TEAM_NEUTRAL && g_ships[i].is_police) {
            /* Patrol drift: a slow circuit of the station approaches. */
            Ship *s2 = &g_ships[i];
            Vec3 tangent = v3_cross(v3(0, 1, 0), v3_norm(s2->pos));
            s2->vel = v3_lerp(s2->vel, v3_scale(tangent, 18.0f),
                              0.5f * dt);
            s2->pos = v3_add(s2->pos, v3_scale(s2->vel, dt));
            m3_rotate_local(&s2->basis, 1, 0.15f * dt);
            continue;
        }
        if (g_ships[i].team != TEAM_HOSTILE) continue;
        ai_ship(i, dt);
    }
}
