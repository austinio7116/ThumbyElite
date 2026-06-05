/*
 * ThumbyElite — in-flight HUD.
 */
#ifndef UI_HUD_H
#define UI_HUD_H

#include <stdint.h>

#include "vec.h"

typedef struct {
    int   target;        /* locked entity index, -1 none */
    int   kills;
    float fuel01;        /* fuel fraction for the gauge */
    float render_ms;     /* pure render time (perf readout) */
    int   show_perf;
} HudInfo;

void ui_hud_draw(uint16_t *fb, const HudInfo *info);

/* Supercruise variant: dashboard + destination marker + travel readouts. */
typedef struct {
    const char *dest_name;   /* NULL = no destination */
    Vec3  dest_rel_mm;       /* destination relative to the ship, Mm */
    float speed_mms;         /* Mm per second */
    float eta_s;             /* envelope-aware ETA (game computes) */
    float throttle;
    float fuel01;
    float render_ms;
    int   show_perf;
} HudScInfo;

void ui_hud_draw_sc(uint16_t *fb, const HudScInfo *info);

#endif
