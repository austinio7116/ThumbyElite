/*
 * ThumbyElite — persistent player state.
 */
#include "elite_player.h"
#include "elite_entity.h"
#include <string.h>

PlayerState g_player;

static const float k_qual_mult[5] = { 0.80f, 1.00f, 1.12f, 1.22f, 1.35f };
static const float k_qual_price[5] = { 0.45f, 1.00f, 1.60f, 2.50f, 4.00f };

/* Base shop prices per weapon type (STANDARD quality). */
static const int16_t k_wpn_base[WPN_COUNT] = {
    [WPN_PULSE_S] = 600,  [WPN_PULSE_M] = 1800, [WPN_PULSE_L] = 4800,
    [WPN_BEAM] = 2600,    [WPN_PHOTON] = 3400,  [WPN_GAUSS] = 5200,
    [WPN_AUTOCANNON] = 900, [WPN_MISSILE] = 1200, [WPN_HOMING] = 2800,
};

void player_init(void) {
    memset(&g_player, 0, sizeof g_player);
    g_player.credits = 1000;
    g_player.hull_id = 0;               /* the SKIFF — everyone starts low */
    g_player.fuel_max = 30.0f;
    g_player.fuel = g_player.fuel_max;
    /* One battered salvaged pulse laser. The dream starts here. */
    g_player.mounts[0] = (WeaponInst){ WPN_PULSE_S, Q_SALVAGED, 70, 1 };
}

int player_cargo_total(void) {
    int n = 0;
    for (int i = 0; i < N_GOODS; i++) n += g_player.cargo[i];
    for (int i = 0; i < MAX_SALVAGE; i++)
        if (g_player.salvage[i].in_use) n++;   /* components take a slot */
    return n;
}

int player_cargo_cap(void) { return k_hulls[g_player.hull_id].cargo; }

float quality_dmg_mult(int q) { return k_qual_mult[q > 4 ? 4 : q]; }

int weapon_price(int type, int q) {
    return (int)(k_wpn_base[type] * k_qual_price[q > 4 ? 4 : q]);
}

float mount_dmg_mult(const WeaponInst *w) {
    /* Integrity below 100 bleeds output: 60% output at 0 integrity. */
    return quality_dmg_mult(w->quality) *
           (0.6f + 0.4f * (float)w->integrity * 0.01f);
}

int skill_level(uint16_t xp) {
    /* Levels at 2,5,10,18,30,50,80,120,170 - simple quadratic-ish curve. */
    static const uint16_t th[9] = { 2, 5, 10, 18, 30, 50, 80, 120, 170 };
    int lvl = 0;
    for (int i = 0; i < 9; i++)
        if (xp >= th[i]) lvl = i + 1;
    return lvl;
}

float skill_heat_mult(void) {
    return 1.0f - 0.025f * (float)skill_level(g_player.xp_gunnery);
}
float skill_turn_mult(void) {
    return 1.0f + 0.02f * (float)skill_level(g_player.xp_piloting);
}
float skill_price_mult(void) {
    return 1.0f - 0.012f * (float)skill_level(g_player.xp_trading);
}
float skill_repair_mult(void) {
    return 1.0f - 0.05f * (float)skill_level(g_player.xp_tech);
}

static uint8_t s_mount_map[HULL_SLOTS];

const WeaponInst *player_mount_for_ship_slot(int slot) {
    if (slot < 0 || slot >= HULL_SLOTS) return 0;
    return &g_player.mounts[s_mount_map[slot]];
}

void player_apply_to_ship(void) {
    Ship *p = &g_ships[PLAYER];
    const HullDef *h = &k_hulls[g_player.hull_id];
    p->mesh = h->mesh;
    p->max_speed = h->max_speed;
    p->accel = h->accel;
    p->turn_rate = h->turn_rate * skill_turn_mult();
    p->hull_max = h->hull_base * k_tier_mult[g_player.hull_tier];
    p->shield_max = h->shield_base * k_tier_mult[g_player.shield_tier];
    if (p->hull > p->hull_max) p->hull = p->hull_max;
    if (p->shield > p->shield_max) p->shield = p->shield_max;

    p->n_weapons = 0;
    p->active_w = 0;
    for (int i = 0; i < h->n_slots; i++) {
        if (!g_player.mounts[i].in_use) continue;
        int m = p->n_weapons;
        s_mount_map[m] = (uint8_t)i;
        p->weapons[m] = g_player.mounts[i].type;
        const WeaponDef *w = &k_weapons[g_player.mounts[i].type];
        /* Ammo persists per session; refilled when docked (free, v1). */
        p->ammo[m] = w->ammo_max ? (int16_t)w->ammo_max : -1;
        p->n_weapons++;
    }
}
