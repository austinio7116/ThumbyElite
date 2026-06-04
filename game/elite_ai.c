/*
 * ThumbyElite — enemy AI.
 *
 * Phase 3 brain: ATTACK turns toward the player, closes to gun range,
 * fires inside a tight cone; BREAK peels off for a couple of seconds
 * when it overshoots (gets too close) so hostiles orbit rather than
 * ram. Skill tiers + subsystem smarts arrive in Phase 6.
 */
#include "elite_ai.h"
#include "elite_entity.h"
#include "elite_combat.h"

#define AI_FIRE_RANGE  420.0f
#define AI_FIRE_COS    0.985f
#define AI_BREAK_DIST  28.0f
#define AI_BREAK_TIME  2.2f
#define AI_SPREAD      0.035f
#define AI_REFIRE      0.55f

/* Steer s so its nose tips toward world-space dir (unit). */
static void turn_toward(Ship *s, Vec3 dir, float dt) {
    Vec3 fwd = s->basis.r[2];
    Vec3 axis = v3_cross(fwd, dir);
    float sin_a = v3_len(axis);
    if (sin_a < 1e-4f) {
        if (v3_dot(fwd, dir) < 0.0f)   /* dead astern: kick over the up axis */
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

static void ai_ship(int idx, float dt) {
    Ship *s = &g_ships[idx];
    Ship *t = &g_ships[PLAYER];
    if (!t->alive) { s->throttle = 0.25f; return; }

    Vec3 rel = v3_sub(t->pos, s->pos);
    float dist = v3_len(rel);
    Vec3 dir = (dist > 1e-3f) ? v3_scale(rel, 1.0f / dist) : s->basis.r[2];

    switch (s->ai_state) {
    default:
    case AI_NONE:
        s->ai_state = AI_ATTACK;
        /* fallthrough */
    case AI_ATTACK: {
        /* Lead pursuit: aim at where the player will be when the laser
         * arrives (hitscan -> tiny lead, but it keeps the nose honest
         * against a strafing target). */
        turn_toward(s, dir, dt);
        s->throttle = (dist > 220.0f) ? 1.0f : (dist > 90.0f ? 0.7f : 0.45f);
        if (dist < AI_BREAK_DIST) {
            s->ai_state = AI_BREAK;
            s->ai_timer = AI_BREAK_TIME;
            break;
        }
        if (dist < AI_FIRE_RANGE &&
            v3_dot(s->basis.r[2], dir) > AI_FIRE_COS &&
            combat_can_fire(s)) {
            combat_fire_laser(idx, AI_SPREAD);
            s->fire_cool = AI_REFIRE;       /* slower trigger than the player */
        }
        break;
    }
    case AI_BREAK:
        /* Climb away relative to the target to set up another pass. */
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
        if (!g_ships[i].alive || g_ships[i].team != TEAM_HOSTILE) continue;
        ai_ship(i, dt);
    }
}
