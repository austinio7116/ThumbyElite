/*
 * ThumbyElite — persistent player state (credits, cargo, fuel).
 *
 * Grows into the full pilot/RPG sheet in Phase 7; the save system
 * (Phase 9) serialises exactly this struct.
 */
#ifndef ELITE_PLAYER_H
#define ELITE_PLAYER_H

#include "econ.h"
#include <stdint.h>

typedef struct {
    int32_t credits;
    uint8_t cargo[N_GOODS];
    uint8_t cargo_cap;
    float   fuel;          /* light-years of jump range remaining */
    float   fuel_max;
} PlayerState;

extern PlayerState g_player;

void player_init(void);
int  player_cargo_total(void);

#endif
