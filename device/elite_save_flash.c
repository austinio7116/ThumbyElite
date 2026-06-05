/*
 * ThumbyElite — standalone save backend: one 4 KB sector at the top of
 * flash. Core1 must be parked during program/erase (it's a lockout
 * victim), and XIP is unavailable mid-operation — the ThumbyRogue
 * pattern verbatim.
 */
#include "elite_platform.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

#define SAVE_BYTES FLASH_SECTOR_SIZE
#define SAVE_OFFSET (PICO_FLASH_SIZE_BYTES - SAVE_BYTES)

int plat_save(const uint8_t *data, int len) {
    if (len > (int)SAVE_BYTES) return 0;
    static uint8_t page[SAVE_BYTES];
    memset(page, 0xFF, sizeof page);
    memcpy(page, data, (size_t)len);

    multicore_lockout_start_blocking();
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(SAVE_OFFSET, SAVE_BYTES);
    flash_range_program(SAVE_OFFSET, page, SAVE_BYTES);
    restore_interrupts(irq);
    multicore_lockout_end_blocking();
    return 1;
}

int plat_load(uint8_t *data, int max_len) {
    const uint8_t *src = (const uint8_t *)(XIP_BASE + SAVE_OFFSET);
    int n = max_len > (int)SAVE_BYTES ? (int)SAVE_BYTES : max_len;
    memcpy(data, src, (size_t)n);
    return n;
}

/* Rumble lives here too (platform hook -> driver). */
#include "craft_rumble.h"
void plat_rumble(float intensity, float seconds) {
    craft_rumble_pulse(intensity, seconds);
}
