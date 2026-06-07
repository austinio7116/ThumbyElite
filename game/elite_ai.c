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
#include "elite_rocks.h"
#include "elite_game.h"
#include "elite_types.h"
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
            m3_rotate_world(&s->basis, s->basis.r[1],
                            s->turn_rate * turn_envelope(s) * dt);
        return;
    }
    float want = asinf(sin_a > 1.0f ? 1.0f : sin_a);
    if (v3_dot(fwd, dir) < 0.0f) want = 3.14159265f - want;
    float step = s->turn_rate * turn_envelope(s) * dt;
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
        if (s->crits & (uint8_t)(CRIT_WPN0 << i)) continue;  /* smashed */
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
    /* Generalised targeting (distress events: pirates fight civilians,
     * civilians fight back) — falls back to the player. */
    int ti = s->ai_target;
    if (ti <= 0 || ti >= MAX_SHIPS || !g_ships[ti].alive ||
        ti == idx)
        ti = PLAYER;
    Ship *t = &g_ships[ti];
    if (!t->alive) { s->throttle = 0.25f; return; }
    /* Cloaked player: invisible to everyone beyond knife range. */
    if (ti == PLAYER && elite_game_cloaked() &&
        v3_len2(v3_sub(t->pos, s->pos)) > 80.0f * 80.0f) {
        s->throttle = 0.35f;
        return;
    }

    Vec3 rel = v3_sub(t->pos, s->pos);
    float dist = v3_len(rel);
    Vec3 dir = (dist > 1e-3f) ? v3_scale(rel, 1.0f / dist) : s->basis.r[2];

    /* Rocks are solid now: steer off any boulder filling the windscreen
     * (cheap avoidance — without it belt fights grind NPCs to dust). */
    for (int rk = 0; rk < 8; rk++) {
        Vec3 rp; float rrad;
        if (!rocks_get(rk, &rp, &rrad)) continue;
        Vec3 toR = v3_sub(rp, s->pos);
        float rd = v3_len(toR);
        float danger = rrad + 18.0f;
        if (rd > danger * 2.2f || rd < 1e-3f) continue;
        Vec3 rdir = v3_scale(toR, 1.0f / rd);
        if (v3_dot(rdir, s->basis.r[2]) < 0.55f) continue;  /* not ahead */
        /* push the steering goal away from the rock centre */
        Vec3 away = v3_sub(dir, v3_scale(rdir,
                                         1.6f * (1.0f - rd /
                                                 (danger * 2.2f))));
        float al = v3_len(away);
        if (al > 1e-3f) dir = v3_scale(away, 1.0f / al);
    }
    int tier = s->tier > 4 ? 4 : s->tier;

    /* THE TAIL FIGHT (user req: position should be earned and owned).
     * When the target sits behind us with guns on, respond by tier:
     * prey runs, CAPABLE jinks, VETERAN throttle-chops to force the
     * overshoot (full agility at low speed — the blue zone cuts both
     * ways), DEADLY mixes chops with scissors. */
    {
        static float s_chop_cd[MAX_SHIPS], s_chop_t[MAX_SHIPS];
        static float s_scissor_t[MAX_SHIPS];
        static int8_t s_scissor_side[MAX_SHIPS];
        static float s_evade_t[MAX_SHIPS];
        s_chop_cd[idx] -= dt;
        int tailed = dist < 200.0f &&
                     v3_dot(dir, s->basis.r[2]) < -0.35f &&
                     v3_dot(t->basis.r[2], v3_scale(dir, -1.0f)) > 0.70f;
        /* evasion has a clock: after ~2.2s of dancing they accept the
         * duel and wheel back in (also stops a stationary gunline from
         * reading as an eternal tail — the siege stalemate) */
        if (tailed) {
            s_evade_t[idx] += dt;
            if (s_evade_t[idx] > 2.2f) {
                tailed = 0;
                if (s_evade_t[idx] > 3.6f) s_evade_t[idx] = 0;
            }
        } else if (s_evade_t[idx] > 0) {
            s_evade_t[idx] -= dt * 2.0f;
            if (s_evade_t[idx] < 0) s_evade_t[idx] = 0;
        }
        if (s_chop_t[idx] > 0.0f) {
            /* mid-chop: dead slow, whip the nose around */
            s_chop_t[idx] -= dt;
            s->throttle = 0.10f;
            turn_toward(s, dir, dt);
            return;
        }
        if (tailed) {
            if (tier <= 1) {
                /* prey: run flat out with a shallow weave */
                Vec3 weave = v3_scale(s->basis.r[0],
                                      0.35f * sinf(s->ai_timer * 5.0f +
                                                   (float)idx));
                s->ai_timer += dt;
                turn_toward(s, v3_norm(v3_add(v3_scale(dir, -1.0f),
                                              weave)), dt);
                s->throttle = 1.0f;
                return;
            }
            if (tier >= 3 && s_chop_cd[idx] <= 0.0f) {
                /* the ED move: chop throttle, force the overshoot */
                s_chop_t[idx] = 1.1f;
                s_chop_cd[idx] = 4.5f;
                return;
            }
            /* jink / scissors: hard alternating lateral breaks */
            s_scissor_t[idx] -= dt;
            if (s_scissor_t[idx] <= 0.0f) {
                s_scissor_t[idx] = (tier >= 4) ? 0.7f : 1.0f;
                s_scissor_side[idx] = (s_scissor_side[idx] >= 0) ? -1 : 1;
            }
            Vec3 brk = v3_add(v3_scale(dir, -0.35f),
                              v3_scale(s->basis.r[0],
                                       (float)s_scissor_side[idx]));
            brk = v3_add(brk, v3_scale(s->basis.r[1],
                                       0.4f * (float)s_scissor_side[idx]));
            turn_toward(s, v3_norm(brk), dt);
            s->throttle = 0.85f;
            return;
        }
    }

    /* Fully disarmed (all mounts critted): break and RUN — a fleeing
     * cripple is a story, and a free kill if you chase it down. */
    if (s->team == TEAM_HOSTILE && s->n_weapons > 0) {
        int usable = 0;
        for (int i = 0; i < s->n_weapons; i++)
            if (!(s->crits & (uint8_t)(CRIT_WPN0 << i))) usable++;
        if (usable == 0) {
            turn_toward(s, v3_scale(dir, -1.0f), dt);
            s->throttle = 1.0f;
            return;
        }
    }

    switch (s->ai_state) {
    default:
    case AI_NONE:
        s->ai_state = AI_ATTACK;
        /* fallthrough */
    case AI_ATTACK: {
        turn_toward(s, dir, dt);
        /* corner-speed combat (ED blue zone): full speed only to CLOSE,
         * then fight slow enough to actually turn */
        s->throttle = (dist > 400.0f) ? 1.0f
                    : (dist > 150.0f) ? 0.68f
                    : (dist > 90.0f)  ? 0.55f : 0.45f;
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
            if (s->crits & CRIT_AIM) sp *= 2.2f;   /* targeting smashed */
            combat_fire(idx, sp, ti);
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
        if (g_ships[i].team == TEAM_NEUTRAL && g_ships[i].is_civilian) {
            /* A civilian under attack fights its attacker. */
            if (g_ships[i].ai_target > 0 &&
                g_ships[g_ships[i].ai_target].alive) {
                ai_ship(i, dt);
                continue;
            }
            /* Working traffic: miners hover by the rocks and chip them
             * (visible beams); cargo ships cruise a slow lane. */
            Ship *cv = &g_ships[i];
            if (cv->civ_kind == 0) {
                Vec3 rk[8];
                int nr = rocks_positions(rk, 8);
                if (nr > 0) {
                    Vec3 want = v3_add(rk[i % nr], v3(30, 18, -25));
                    Vec3 d2 = v3_sub(want, cv->pos);
                    float dl = v3_len(d2);
                    if (dl > 12.0f)
                        cv->vel = v3_lerp(cv->vel,
                                          v3_scale(d2, 14.0f / dl),
                                          0.8f * dt);
                    else
                        cv->vel = v3_scale(cv->vel, 1.0f - 0.8f * dt);
                    turn_toward(cv, v3_norm(v3_sub(rk[i % nr], cv->pos)),
                                dt * 0.5f);
                    cv->fire_cool -= dt;
                    if (dl < 90.0f && cv->fire_cool <= 0.0f) {
                        cv->fire_cool = 1.3f;
                        Vec3 mz = v3_add(cv->pos,
                                         v3_scale(cv->basis.r[2],
                                                  cv->mesh->bound_r));
                        fx_beam(mz, rk[i % nr],
                                RGB565C(255, 200, 90));
                    }
                } else {
                    cv->vel = v3_scale(cv->vel, 1.0f - 0.4f * dt);
                }
            } else {
                /* slow lane between two offsets */
                m3_rotate_local(&cv->basis, 1, 0.04f * dt);
                cv->vel = v3_lerp(cv->vel,
                                  v3_scale(cv->basis.r[2], 22.0f),
                                  0.4f * dt);
            }
            cv->pos = v3_add(cv->pos, v3_scale(cv->vel, dt));
            continue;
        }
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
