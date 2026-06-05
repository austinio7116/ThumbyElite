/*
 * ThumbyElite — save backend, FatFs (ThumbyOne slot mode).
 *
 * Same API as elite_save_flash.c (plat_save / plat_load), but the
 * dock-checkpoint blob lives in the shared FAT volume at
 *   /thumbyelite/run.sav
 * instead of a dedicated flash sector, so it survives reflashing the
 * slot and is visible over the lobby's USB MSC. The volume is mounted
 * once by the slot device main (thumbyone_fs_mount_or_format) before
 * the game starts.
 *
 * plat_rumble lives here too (the flash backend file carries it in the
 * standalone build).
 */
#include <stdint.h>
#include "ff.h"
#include "elite_platform.h"
#include "craft_rumble.h"

#define ELITE_SAVE_DIR  "/thumbyelite"
#define ELITE_SAVE_PATH ELITE_SAVE_DIR "/run.sav"

int plat_save(const uint8_t *data, int len) {
    if (len <= 0) return 0;
    f_mkdir(ELITE_SAVE_DIR);   /* idempotent — FR_EXIST is fine */
    FIL fp;
    if (f_open(&fp, ELITE_SAVE_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return 0;
    UINT bw = 0;
    f_write(&fp, data, (UINT)len, &bw);
    f_close(&fp);
    return (bw == (UINT)len) ? len : 0;
}

int plat_load(uint8_t *data, int max) {
    if (max <= 0) return 0;
    FIL fp;
    if (f_open(&fp, ELITE_SAVE_PATH, FA_READ) != FR_OK) return 0;
    UINT br = 0;
    f_read(&fp, data, (UINT)max, &br);
    f_close(&fp);
    return (int)br;
}

void plat_rumble(float intensity, float dur_s) {
    craft_rumble_pulse(intensity, dur_s);
}
