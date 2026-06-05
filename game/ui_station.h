/*
 * ThumbyElite — docked station services (market, refuel, launch).
 */
#ifndef UI_STATION_H
#define UI_STATION_H

#include "craft_buttons.h"
#include <stdint.h>

typedef enum { DOCK_NONE = 0, DOCK_LAUNCH } DockAction;

void station_open(int station_idx);
DockAction station_tick(const CraftRawButtons *btn, float dt);
void station_draw(uint16_t *fb);

#endif
