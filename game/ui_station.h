/*
 * ThumbyElite — docked station services (market, refuel, launch).
 */
#ifndef UI_STATION_H
#define UI_STATION_H

#include "craft_buttons.h"
#include <stdint.h>

typedef enum { DOCK_NONE = 0, DOCK_LAUNCH } DockAction;

void station_open(int station_idx);
void station_toast(const char *msg);
/* 3D preview pane request: -2 none, -1 the station, >=0 a hull id. */
int station_preview(void);
DockAction station_tick(const CraftRawButtons *btn, float dt);
void station_draw(uint16_t *fb);

#endif
