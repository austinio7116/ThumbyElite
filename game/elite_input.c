/*
 * ThumbyElite — flight input chords.
 *
 * Tap detection: a modifier (LB/RB) that is pressed and released within
 * TAP_MS without any d-pad use while held counts as a tap. Using the
 * d-pad during the hold "consumes" the modifier so releasing it doesn't
 * also fire the tap action. RB double-tap (two taps within DOUBLE_MS)
 * fires boost instead of the second toggle.
 */
#include "elite_input.h"
#include <string.h>

#define TAP_MS    0.30f
#define DOUBLE_MS 0.35f

typedef struct {
    bool  down;
    float held_s;
    bool  consumed;       /* d-pad used during hold */
    float since_tap_s;    /* time since last completed tap */
} Mod;

static Mod   s_lb, s_rb;
static bool  s_prev_b;
static float s_rb_pending = -1.0f;   /* >=0: single-tap action armed, waiting
                                      * out the double-tap window */

void elite_input_reset(void) {
    memset(&s_lb, 0, sizeof s_lb);
    memset(&s_rb, 0, sizeof s_rb);
    s_lb.since_tap_s = s_rb.since_tap_s = 10.0f;
    s_prev_b = false;
    s_rb_pending = -1.0f;
}

/* Returns 1 on tap, 2 on double-tap, 0 otherwise. use_consume: d-pad
 * activity during the hold cancels the tap (RB — accidental assist
 * toggles confuse; LB taps must work mid-turn, so no consume there:
 * the brief roll blip is harmless). */
static int mod_update(Mod *m, bool down, bool dpad_used, bool use_consume,
                      float dt) {
    int ev = 0;
    m->since_tap_s += dt;
    if (down) {
        if (!m->down) { m->down = true; m->held_s = 0; m->consumed = false; }
        m->held_s += dt;
        if (dpad_used && use_consume) m->consumed = true;
    } else if (m->down) {
        m->down = false;
        if (!m->consumed && m->held_s < TAP_MS) {
            ev = (m->since_tap_s < DOUBLE_MS) ? 2 : 1;
            m->since_tap_s = 0;
        }
    }
    return ev;
}

void elite_input_update(const CraftRawButtons *btn, float dt, FlightInput *out) {
    memset(out, 0, sizeof *out);

    bool dpad = btn->up || btn->down || btn->left || btn->right;

    /* Axes, redirected by held modifiers. */
    float ud = (btn->up ? 1.0f : 0.0f) - (btn->down ? 1.0f : 0.0f);
    float lr = (btn->right ? 1.0f : 0.0f) - (btn->left ? 1.0f : 0.0f);

    if (s_lb.down) { out->roll = lr; } else { out->yaw = lr; }
    if (s_rb.down) { out->throttle_delta = ud; } else { out->pitch = ud; }

    int lb_ev = mod_update(&s_lb, btn->lb, dpad, false, dt);
    int rb_ev = mod_update(&s_rb, btn->rb, dpad, true, dt);
    if (lb_ev >= 1) out->cycle_target = true;

    /* RB tap action is deferred one double-tap window so a double-tap is
     * pure boost (no spurious assist toggle on the first tap). */
    if (rb_ev == 2) { out->boost = true; s_rb_pending = -1.0f; }
    else if (rb_ev == 1) s_rb_pending = DOUBLE_MS;
    else if (s_rb_pending >= 0.0f) {
        s_rb_pending -= dt;
        if (s_rb_pending < 0.0f) out->assist_toggle = true;
    }

    out->fire = btn->a;
    out->secondary = btn->b && !s_prev_b;
    s_prev_b = btn->b;
}
