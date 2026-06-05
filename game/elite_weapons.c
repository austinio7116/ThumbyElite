/*
 * ThumbyElite — weapon catalogue.
 *
 * Roles: pulse lasers = reliable energy DPS by size tier; beam = sustained
 * melt at high heat; photon = slow heavy slug you must lead; gauss =
 * sniper alpha; autocannon = close-range shredder; missiles = burst AoE,
 * homing trades damage for guidance.
 */
#include "elite_weapons.h"
#include "elite_types.h"

const WeaponDef k_weapons[WPN_COUNT] = {
    /*                name       dmg  cool  heat  speed  range turn aoe  sz ammo color */
    [WPN_PULSE_S]   = { "PULSE-S",  9, 0.16f, 4.5f,    0,  600,  0,   0, 1,  0, RGB565C(255,  80,  60) },
    [WPN_PULSE_M]   = { "PULSE-M", 16, 0.22f, 7.5f,    0,  700,  0,   0, 2,  0, RGB565C(255, 120,  50) },
    [WPN_PULSE_L]   = { "PULSE-L", 28, 0.30f, 12.0f,   0,  800,  0,   0, 3,  0, RGB565C(255, 160,  40) },
    [WPN_BEAM]      = { "BEAM",     4, 0.05f, 3.2f,    0,  500,  0,   0, 2,  0, RGB565C(255, 220, 120) },
    [WPN_PHOTON]    = { "PHOTON",  34, 0.55f, 14.0f, 260, 1100,  0,   0, 2,  0, RGB565C(120, 220, 255) },
    [WPN_GAUSS]     = { "GAUSS",   40, 0.90f, 6.0f, 1400, 1700,  0,   0, 2, 24, RGB565C(200, 230, 255) },
    [WPN_AUTOCANNON]= { "AUTOCAN",  5, 0.07f, 1.2f,  750,  900,  0,   0, 1,160, RGB565C(255, 210, 140) },
    [WPN_MISSILE]   = { "MISSILE", 45, 0.80f, 2.0f,  220, 2000,  0,  22, 1,  8, RGB565C(255, 170,  90) },
    [WPN_HOMING]    = { "HOMING",  32, 1.10f, 2.0f,  190, 2600, 1.7f, 18, 2,  6, RGB565C(255, 120, 200) },
};

const EquipDef k_equip[2] = {
    { "SHIELD", 1400 },
    { "ARMOR", 1100 },
};

const char *item_name(int type) {
    if (type >= WPN_COUNT && type < ITEM_COUNT)
        return k_equip[type - WPN_COUNT].name;
    return k_weapons[type].name;
}
