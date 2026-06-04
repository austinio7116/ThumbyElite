/*
 * ThumbyElite — in-flight HUD.
 */
#ifndef UI_HUD_H
#define UI_HUD_H

#include <stdint.h>

typedef struct {
    int   target;        /* locked entity index, -1 none */
    int   wave;
    int   kills;
    float render_ms;     /* pure render time (perf readout) */
    int   show_perf;
} HudInfo;

void ui_hud_draw(uint16_t *fb, const HudInfo *info);

#endif
