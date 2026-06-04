/*
 * ThumbyElite — device entry point (standalone, RP2350).
 *
 * Thin platform shell over the shared elite_game loop: read buttons, tick,
 * render, present via async DMA. Phase 1 renders single-core to establish
 * the baseline; the dual-core screen-half split arrives with the Phase 2
 * draw-list (core1 is launched but parked so the lockout plumbing and
 * timing are realistic from day one).
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

/* Core1: parked for Phase 1. Lockout-victim so core0 can stop us during
 * future flash saves; the render split takes this loop over in Phase 2. */
static void core1_entry(void) {
    multicore_lockout_victim_init();
    while (true) tight_loop_contents();
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

        /* Single-buffered fb + async DMA present: wait for the previous
         * frame's transfer before writing g_fb again (tearing otherwise). */
        uint32_t r0 = to_ms_since_boot(get_absolute_time());
        elite_game_render_begin();
        craft_lcd_wait_idle();
        elite_game_render(g_fb, 0, ELITE_FB_H);
        elite_game_draw_overlay(g_fb);
        elite_game_set_frame_ms(
            (float)(to_ms_since_boot(get_absolute_time()) - r0));

        craft_lcd_present(g_fb);
    }
    return 0;
}
