/*
 * ThumbyElite — device entry point (standalone, RP2350).
 *
 * Thin platform shell over the shared elite_game loop. Dual-core
 * screen-half rendering: core0 builds the frame draw-list (transform/
 * clip/shade), then both cores rasterise it — core0 rows [0,64),
 * core1 rows [64,128). Disjoint pixels, no locks; the same go/done
 * volatile handshake as ThumbyRogue.
 */
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "hardware/clocks.h"

#include "craft_lcd_gc9107.h"
#include "craft_buttons.h"
#include "craft_audio_pwm.h"
#include "craft_rumble.h"
#ifdef THUMBYONE_SLOT_MODE
#include "ff.h"
#include "thumbyone_fs.h"
#include "thumbyone_handoff.h"
#include "thumbyone_led.h"
static FATFS   g_fs;
static uint8_t g_fs_work[FF_MAX_SS] __attribute__((aligned(4)));
#endif
#include "elite_audio.h"
#ifdef THUMBYONE_SLOT_MODE
#include "thumbyone_settings.h"
#include "thumbyone_backlight.h"
#endif
#include "elite_types.h"
#include "elite_game.h"

static uint16_t g_fb[ELITE_FB_W * ELITE_FB_H];

static volatile bool s_core1_go   = false;
static volatile bool s_core1_done = false;

static void core1_entry(void) {
    multicore_lockout_victim_init();   /* core0 parks us during flash saves */
    while (true) {
        while (!s_core1_go) tight_loop_contents();
        s_core1_go = false;
        elite_game_render(g_fb, ELITE_FB_H / 2, ELITE_FB_H);
        s_core1_done = true;
    }
}

/* Settings bridge: slot mode reads/writes the ThumbyOne shared store
 * (/.volume 0-20, /.brightness 0-255 on the shared FAT) so the lobby
 * and every other slot see the same values; standalone keeps volume
 * session-only and has no brightness control. */
int plat_setting_get(int which) {
    if (which >= 2) return 10;   /* analog sens: no gamepad/touch on device */
#ifdef THUMBYONE_SLOT_MODE
    return which == 0 ? (int)thumbyone_settings_load_volume()
                      : (int)thumbyone_settings_load_brightness();
#else
    return which == 0 ? (int)(audio_get_master() * 20.0f) : 255;
#endif
}

void plat_setting_set(int which, int value) {
    if (which >= 2) return;      /* no analog input source on device */
    if (which == 0) {
        audio_set_master((float)value / 20.0f);
#ifdef THUMBYONE_SLOT_MODE
        thumbyone_settings_save_volume((uint8_t)value);
#endif
    } else {
#ifdef THUMBYONE_SLOT_MODE
        thumbyone_backlight_set((uint8_t)value);
        thumbyone_settings_save_brightness((uint8_t)value);
#endif
    }
}

int main(void) {
    set_sys_clock_khz(280000, true);
    craft_lcd_init();
    for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++) g_fb[i] = 0;
    craft_lcd_present(g_fb);
    craft_buttons_init();
    craft_rumble_init();
    craft_audio_pwm_init();

#ifdef THUMBYONE_SLOT_MODE
    /* Honour the lobby's brightness + LED, then mount the shared FAT so
     * plat_save/plat_load reach /thumbyelite/run.sav. Must run BEFORE
     * elite_game_init, whose title screen checks save_exists(). */
    thumbyone_slot_init_brightness_and_led(true);
    (void)thumbyone_fs_mount_or_format(&g_fs, g_fs_work, sizeof g_fs_work);
    /* The lobby's volume, honoured from the first sound. */
    audio_set_master((float)thumbyone_settings_load_volume() / 20.0f);
#endif
    elite_game_init(get_rand_32());

    multicore_launch_core1(core1_entry);

    uint32_t last_ms = to_ms_since_boot(get_absolute_time());
    while (true) {
        CraftRawButtons btn;
        craft_buttons_read(&btn);
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        float dt = (now_ms - last_ms) * 0.001f;
        if (dt > 0.1f) dt = 0.1f;
        last_ms = now_ms;

#ifdef THUMBYONE_SLOT_MODE
        /* Hold MENU ~1.2s to return to the lobby (short taps still pause).
         * Progress = the last dock save, same as a power cycle — the
         * insurance model applies. */
        static uint32_t s_menu_held_ms = 0;
        if (btn.menu) {
            s_menu_held_ms += (uint32_t)(dt * 1000.0f);
            if (s_menu_held_ms >= 1200u) {
                craft_lcd_wait_idle();
                thumbyone_handoff_request_lobby();   /* reboots; no return */
            }
        } else {
            s_menu_held_ms = 0;
        }
#endif
        elite_game_tick(&btn, dt);
        craft_rumble_tick(dt);

        /* Keep the PWM ring fed. */
        int room = craft_audio_pwm_room();
        while (room > 0) {
            int16_t abuf[128];
            int n = room < 128 ? room : 128;
            audio_render(abuf, n);
            craft_audio_pwm_push(abuf, n);
            room -= n;
        }

        /* Perf readout = pure work time (draw-list build + raster),
         * EXCLUDING the wait for the previous frame's DMA (~6.5ms fixed). */
        uint64_t t0 = to_us_since_boot(get_absolute_time());
        elite_game_render_begin();          /* core0: build draw-list */
        uint64_t t1 = to_us_since_boot(get_absolute_time());

        /* Single-buffered fb + async DMA present: wait for the previous
         * frame's transfer before either core writes g_fb (tears otherwise). */
        craft_lcd_wait_idle();
        uint64_t t2 = to_us_since_boot(get_absolute_time());
        s_core1_done = false;
        s_core1_go   = true;                          /* core1: lower half */
        elite_game_render(g_fb, 0, ELITE_FB_H / 2);   /* core0: upper half */
        while (!s_core1_done) tight_loop_contents();
        uint64_t t3 = to_us_since_boot(get_absolute_time());

        elite_game_draw_overlay(g_fb);
        elite_game_set_frame_ms((float)((t1 - t0) + (t3 - t2)) * 0.001f);

        craft_lcd_present(g_fb);
    }
    return 0;
}
