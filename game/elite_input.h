/*
 * ThumbyElite — flight input: raw buttons -> semantic flight controls.
 *
 * Chord scheme (9 buttons total):
 *   D-pad         pitch (U/D) + yaw (L/R)
 *   A             fire primary
 *   B             fire secondary
 *   LB held       L/R becomes ROLL (pitch unchanged)
 *   LB tap        cycle target (tap = released <300ms, no d-pad used)
 *   RB held       U/D becomes THROTTLE (yaw unchanged)
 *   RB tap        toggle flight assist
 *   RB double-tap boost
 *   MENU          handled by the platform/game layer, not here
 */
#ifndef ELITE_INPUT_H
#define ELITE_INPUT_H

#include "craft_buttons.h"
#include <stdbool.h>

typedef struct {
    float pitch;          /* -1..1 (+ = nose up) */
    float yaw;            /* -1..1 (+ = nose right) */
    float roll;           /* -1..1 (+ = roll right) */
    float throttle_delta; /* -1..1 while RB held */
    bool  fire;           /* held */
    bool  secondary;      /* just pressed */
    bool  chaff;          /* LB held + B tap: countermeasures */
    bool  cloak;          /* RB held + B tap: engage cloak */
    bool  cycle_target;   /* event */
    bool  tgt_class_cycle; /* LB double-tap: demote the lock class */
    bool  assist_toggle;  /* event */
    bool  boost;          /* event */
} FlightInput;

void elite_input_reset(void);
void elite_input_update(const CraftRawButtons *btn, float dt, FlightInput *out);

/* Optional analog stick (Android touch stick / gamepad left stick).
 * x: + = right, y: + = up, both -1..1, deadzone applied by the shell.
 * Nonzero values override the d-pad pitch/yaw (and roll/throttle under
 * the LB/RB chords) at the same max rates. Call with (0,0) when idle. */
void elite_input_set_analog(float x, float y);
/* Dedicated analog roll (gamepad right-stick X); 0 = off. */
void elite_input_set_analog_roll(float r);

#endif
