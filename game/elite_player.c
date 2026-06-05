/*
 * ThumbyElite — persistent player state.
 */
#include "elite_player.h"
#include <string.h>

PlayerState g_player;

void player_init(void) {
    memset(&g_player, 0, sizeof g_player);
    g_player.credits = 1000;
    g_player.cargo_cap = 8;
    g_player.fuel_max = 30.0f;
    g_player.fuel = g_player.fuel_max;
}

int player_cargo_total(void) {
    int n = 0;
    for (int i = 0; i < N_GOODS; i++) n += g_player.cargo[i];
    return n;
}
