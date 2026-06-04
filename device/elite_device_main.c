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

int main(void) {
    set_sys_clock_khz(280000, true);
    craft_lcd_init();
    for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++) g_fb[i] = 0;
    craft_lcd_present(g_fb);
    craft_buttons_init();

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

        elite_game_tick(&btn, dt);

        uint32_t r0 = to_ms_since_boot(get_absolute_time());
        elite_game_render_begin();          /* core0: build draw-list */

        /* Single-buffered fb + async DMA present: wait for the previous
         * frame's transfer before either core writes g_fb (tears otherwise). */
        craft_lcd_wait_idle();
        s_core1_done = false;
        s_core1_go   = true;                          /* core1: lower half */
        elite_game_render(g_fb, 0, ELITE_FB_H / 2);   /* core0: upper half */
        while (!s_core1_done) tight_loop_contents();

        elite_game_draw_overlay(g_fb);
        elite_game_set_frame_ms(
            (float)(to_ms_since_boot(get_absolute_time()) - r0));

        craft_lcd_present(g_fb);
    }
    return 0;
}
