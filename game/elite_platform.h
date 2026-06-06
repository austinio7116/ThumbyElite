/*
 * ThumbyElite — platform hooks the game calls, shells implement.
 */
#ifndef ELITE_PLATFORM_H
#define ELITE_PLATFORM_H

#include <stdint.h>

/* Rumble: intensity 0..1 for seconds. No-op on host. */
void plat_rumble(float intensity, float seconds);

/* Persistent save blob. Return bytes read / nonzero on success. */
int plat_save(const uint8_t *data, int len);
int plat_load(uint8_t *data, int max_len);

/* System settings bridge (ThumbyOne shared store in slot mode).
 * which: 0 = volume (0..20), 1 = brightness (0..255). Set applies the
 * value immediately AND persists it where the platform supports it. */
int  plat_setting_get(int which);
void plat_setting_set(int which, int value);

#endif
