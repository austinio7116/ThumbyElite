/*
 * ThumbyElite — save game (versioned blob via plat_save/plat_load).
 *
 * Saved automatically on every successful dock; loading restores you
 * docked at that station. Death reverts to the last save (insurance
 * recovers the ship, the journey since is lost).
 */
#ifndef ELITE_SAVE_H
#define ELITE_SAVE_H

#include "galaxy_gen.h"
#include <stdbool.h>

typedef struct {
    SysAddr addr;          /* docked system */
    uint8_t station;       /* docked station index */
    int32_t kills;
} SaveMeta;

bool save_exists(void);
bool save_write(SysAddr addr, int station, int kills);
/* Restores galaxy seed + player + missions + rep. Fills meta. */
bool save_load(SaveMeta *out);
bool save_matches_galaxy(uint32_t seed);

#endif
