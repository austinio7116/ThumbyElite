/*
 * ThumbyElite — flight model.
 */
#include "elite_flight.h"

#define BOOST_TIME   2.2f
#define BOOST_MULT   1.8f
#define THROTTLE_RATE 0.9f   /* full range per ~1.1s of held throttle */

/* Turn ramp: first touch turns gently, full rate after ~0.45s held —
 * fine aim on tap, fast slew on hold (user request). */
static float s_ramp_pitch, s_ramp_yaw, s_ramp_roll;
static float ramp(float *t, float active, float dt) {
    if (active != 0.0f) *t += dt; else *t = 0.0f;
    float k = *t / 0.45f;
    if (k > 1.0f) k = 1.0f;
    return 0.30f + 0.70f * k;
}

void flight_apply_input(const FlightInput *in, float dt) {
    Ship *p = &g_ships[PLAYER];
    if (!p->alive) return;

    float tr = p->turn_rate * dt;
    float rp = ramp(&s_ramp_pitch, in->pitch, dt);
    float ry = ramp(&s_ramp_yaw, in->yaw, dt);
    float rr = ramp(&s_ramp_roll, in->roll, dt);
    /* Pitch about the ship's right axis, yaw about up, roll about forward.
     * Flight-stick convention (user-confirmed): d-pad UP = nose DOWN
     * ("push the stick forward"). An un-inverted option can join the
     * pause-menu settings later. */
    if (in->pitch != 0.0f) m3_rotate_local(&p->basis, 0, in->pitch * tr * rp);
    if (in->yaw   != 0.0f) m3_rotate_local(&p->basis, 1, in->yaw * tr * ry);
    if (in->roll  != 0.0f)
        m3_rotate_local(&p->basis, 2, in->roll * tr * 1.5f * rr);
    m3_orthonormalize(&p->basis);

    p->throttle += in->throttle_delta * THROTTLE_RATE * dt;
    if (p->throttle < 0.0f) p->throttle = 0.0f;
    if (p->throttle > 1.0f) p->throttle = 1.0f;

    if (in->assist_toggle) p->assist = !p->assist;
    if (in->boost && p->boost_t <= 0.0f) p->boost_t = BOOST_TIME;
}

static void ship_physics(Ship *s, float dt) {
    float max_v = s->max_speed, acc = s->accel;
    if (s->boost_t > 0.0f) {
        s->boost_t -= dt;
        max_v *= BOOST_MULT;
        acc *= BOOST_MULT;
        if (s->throttle < 1.0f) s->throttle = 1.0f;   /* boost floors it */
    }

    Vec3 fwd = s->basis.r[2];
    if (s->assist) {
        /* Velocity chases throttle * nose direction. */
        Vec3 desired = v3_scale(fwd, s->throttle * max_v);
        Vec3 dv = v3_sub(desired, s->vel);
        float dl = v3_len(dv);
        float step = acc * dt;
        s->vel = (dl <= step || dl < 1e-5f)
                     ? desired
                     : v3_add(s->vel, v3_scale(dv, step / dl));
    } else {
        /* Drift: thrust along the nose only; speed capped, never damped. */
        s->vel = v3_add(s->vel, v3_scale(fwd, s->throttle * acc * dt));
        float v2 = v3_len2(s->vel);
        float cap = max_v * 1.15f;
        if (v2 > cap * cap) s->vel = v3_scale(v3_norm(s->vel), cap);
    }

    s->pos = v3_add(s->pos, v3_scale(s->vel, dt));
}

void flight_tick(float dt) {
    for (int i = 0; i < MAX_SHIPS; i++)
        if (g_ships[i].alive) ship_physics(&g_ships[i], dt);
}
