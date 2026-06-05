/*
 * ThumbyElite — ship entity pool.
 *
 * Fixed pool, no allocation. Entity 0 is always the player. Positions are
 * SYSTEM-LOCAL world coords (floats are fine at combat scale; the camera-
 * relative subtraction happens at render time).
 */
#ifndef ELITE_ENTITY_H
#define ELITE_ENTITY_H

#include "vec.h"
#include "r3d_mesh.h"
#include "elite_weapons.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_SHIPS 16
#define PLAYER 0
#define MAX_HARDPOINTS 3

typedef enum { TEAM_PLAYER = 0, TEAM_HOSTILE = 1, TEAM_NEUTRAL = 2 } Team;
typedef enum { AI_NONE = 0, AI_ATTACK, AI_BREAK } AiState;

typedef struct {
    bool  alive;
    const Mesh *mesh;
    Vec3  pos;            /* system-local world, meters */
    Mat3  basis;          /* rows: right/up/forward */
    Vec3  vel;            /* m/s */
    float throttle;       /* 0..1 of max_speed */
    bool  assist;         /* flight assist (velocity chases the nose) */
    float boost_t;        /* seconds of boost remaining */

    /* Phase 3 flat stats (component-derived from Phase 7). */
    float max_speed;      /* m/s */
    float accel;          /* m/s^2 */
    float turn_rate;      /* rad/s */
    float hull, hull_max;
    float shield, shield_max;
    float heat;           /* 0..100, >100 blocks weapons */
    float fire_cool;
    /* Status effects (D1): timers tick down in ship_tick. */
    float sys_offline_t;     /* weapons scrambled (ion strips) */
    float engine_drag_t;     /* thrust halved */
    float shield_regen;      /* pts/s (variant-dependent) */
    float shield_delay;      /* s after a hit before regen */
    uint8_t shield_var, armor_var;   /* SHV_* / ARV_* */
    uint8_t turret_type;     /* weapon type + 1; 0 = no turret */
    float   turret_cool;      /* s until next shot */

    /* Hardpoints: fitted weapons + per-mount ammo; A fires the active
     * mount, B cycles it. Slot counts/sizes come from the hull (full
     * shipyard gating lands with Phase 7 outfitting). */
    uint8_t weapons[MAX_HARDPOINTS];   /* WeaponType per mount */
    int16_t ammo[MAX_HARDPOINTS];      /* -1 = energy weapon */
    uint8_t n_weapons;
    uint8_t active_w;

    uint8_t team;
    uint8_t tier;         /* AI skill 0..4 (HARMLESS..ELITE) */
    uint8_t is_mark;      /* bounty-mission target */
    uint8_t ai_state;
    float   ai_timer;
    int8_t  target;       /* entity index, -1 none */
} Ship;

extern Ship g_ships[MAX_SHIPS];

void ships_init(void);
/* Returns index or -1 (pool full). Slot 0 is reserved for the player. */
int ship_spawn(const Mesh *mesh, Vec3 pos, uint8_t team);
int ships_alive_hostile(void);
/* Remove every NPC (anchor change: they live in the old local frame). */
void ships_despawn_npcs(void);

/* Fit a tier-appropriate loadout + stat scaling (AI "power").
 * hull_class indexes the stat templates (k_hulls). */
void ship_set_tier(int idx, int tier, int hull_class);
/* Fit one mount. */
void ship_fit_weapon(int idx, int mount, WeaponType w);

extern const char *k_tier_names[5];

#endif
