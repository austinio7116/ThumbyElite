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

#endif
