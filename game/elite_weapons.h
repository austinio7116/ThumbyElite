/*
 * ThumbyElite — weapon catalogue.
 *
 * Hitscan (speed=0): lasers — instant beam, energy-fed (heat only).
 * Projectile: photons/gauss/autocannon/missiles — travel time, ammo.
 * Homing missiles steer toward the shooter's locked target.
 *
 * Slot sizes (S/M/L = 1/2/3) gate what a hull can mount — a size-2
 * hardpoint takes any weapon of size <= 2 (MechWarrior rules).
 */
#ifndef ELITE_WEAPONS_H
#define ELITE_WEAPONS_H

#include <stdint.h>

typedef enum {
    WPN_PULSE_S = 0,   /* light laser */
    WPN_PULSE_M,       /* medium laser */
    WPN_PULSE_L,       /* heavy laser */
    WPN_BEAM,          /* continuous beam laser */
    WPN_PHOTON,        /* photon cannon: slow bright bolt, big hit */
    WPN_GAUSS,         /* gauss gun: hypervelocity slug, low RoF */
    WPN_AUTOCANNON,    /* rapid ballistic stream */
    WPN_MISSILE,       /* dumbfire rocket, AoE */
    WPN_HOMING,        /* seeker missile, needs a target lock */
    WPN_FLAK,          /* 5-pellet cone, brutal point-blank */
    WPN_RAILGUN,       /* hold-to-charge hypervelocity lance */
    WPN_ION,           /* shield-stripper; full strip scrambles systems */
    WPN_MINE,          /* proximity mine dropped astern */
    WPN_TRACTOR,       /* salvage beam: reels locked canisters in */
    WPN_COUNT,
    /* Equipment shares the instance/rack/icon machinery: */
    EQ_SHIELD = WPN_COUNT,
    EQ_ARMOR,
    ITEM_COUNT
} WeaponType;

/* Weapon affixes: factory modifications on an instance. Multipliers
 * fold into effective stats everywhere (fire, detail sheets, compare,
 * prices). AFX_TUNED is the no-downside jackpot, PRO drops only. */
typedef enum {
    AFX_NONE = 0, AFX_OVERCLOCKED, AFX_VENTED, AFX_CALIBRATED,
    AFX_RAPID, AFX_SURPLUS, AFX_TUNED, AFX_COUNT
} Affix;
typedef struct {
    const char *name;       /* full, for detail sheets */
    const char *tag;        /* 2-3 chars, for rows */
    float dmg, heat, cooldown, range, price;
} AffixDef;
extern const AffixDef k_affixes[AFX_COUNT];

/* Equipment catalogue (indexed EQ_x - WPN_COUNT). */
typedef struct {
    const char *name;
    int16_t base_price;     /* tier 1; higher tiers scale x2 / x3.6 */
} EquipDef;
extern const EquipDef k_equip[2];
const char *item_name(int type);

typedef struct {
    const char *name;       /* HUD label, <= 8 chars */
    float dmg;
    float cooldown;         /* s between shots */
    float heat;             /* per shot */
    float speed;            /* m/s; 0 = hitscan */
    float range;            /* hitscan range / projectile life*speed */
    float turn;             /* homing turn rate rad/s (0 = ballistic) */
    float aoe;              /* blast radius (missiles), 0 = direct */
    uint8_t size;           /* slot size 1..3 */
    uint8_t ammo_max;       /* 0 = energy weapon */
    uint16_t color;
} WeaponDef;

extern const WeaponDef k_weapons[WPN_COUNT];

#endif
