/*
 * ThumbyElite — host (Linux/SDL2) shell.
 *
 * Thin platform layer over the shared elite_game loop: maps keyboard to
 * CraftRawButtons, ticks, renders, presents at 5x scale.
 *
 * Keys (device keyboard-mapping convention):
 *   W/A/S/D    D-pad        .  ,     A / B
 *   LShift     LB           Space    RB
 *   Enter      MENU         ESC/F12  quit
 *
 * ELITE_SHOT=path  -> settle N frames headless, dump a PPM, exit.
 * ELITE_SETTLE=n   -> settle frame count (default 60).
 */
#include "elite_types.h"
#include "elite_game.h"
#include "elite_entity.h"
#include "craft_buttons.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SCALE 5
#define WIN_W (ELITE_FB_W * SCALE)
#define WIN_H (ELITE_FB_H * SCALE)

static uint16_t g_fb[ELITE_FB_W * ELITE_FB_H];

static void render_frame(void) {
    elite_game_render_begin();
    elite_game_render(g_fb, 0, ELITE_FB_H);
    elite_game_draw_overlay(g_fb);
}

static void dump_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", ELITE_FB_W, ELITE_FB_H);
    for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++) {
        uint16_t c = g_fb[i];
        uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
                           (uint8_t)(((c >>  5) & 0x3F) * 255 / 63),
                           (uint8_t)(( c        & 0x1F) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("[elite] wrote %s\n", path);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *shot_path = getenv("ELITE_SHOT");

    uint32_t seed = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0)
                               : (uint32_t)time(NULL);
    printf("[elite] seed = %u\n", seed);
    elite_game_init(seed);

    /* Headless autopilot: chase the scanner's nearest hostile and hold the
     * trigger for N seconds, logging the combat loop each second. */
    if (getenv("ELITE_DEMO")) {
        int secs = atoi(getenv("ELITE_DEMO"));
        if (secs < 1) secs = 20;
        for (int f = 0; f < secs * 30; f++) {
            CraftRawButtons b = {0};
            b.a = true;                       /* fire */
            if (f % 90 == 30) b.lb = true;    /* tap LB -> lock target */
            /* Crude pursuit: steer toward the first live hostile. */
            Ship *p = &g_ships[0];
            for (int i = 1; i < 16; i++) {
                if (!g_ships[i].alive) continue;
                Vec3 rel = v3_sub(g_ships[i].pos, p->pos);
                Vec3 local = m3_mul_v3_t(&p->basis, rel);
                if (local.x > 5.0f) b.right = true;
                else if (local.x < -5.0f) b.left = true;
                if (local.y > 5.0f) b.up = true;
                else if (local.y < -5.0f) b.down = true;
                break;
            }
            elite_game_tick(&b, 1.0f / 30.0f);
            if (f % 30 == 29) {
                extern int combat_kills(void);
                extern int ships_alive_hostile(void);
                printf("[demo] t=%2ds hull=%3.0f shield=%3.0f heat=%3.0f "
                       "foes=%d kills=%d v=%3.0f\n",
                       (f + 1) / 30, g_ships[0].hull, g_ships[0].shield,
                       g_ships[0].heat, ships_alive_hostile(), combat_kills(),
                       v3_len(g_ships[0].vel));
            }
            if (shot_path && f == secs * 30 - 1) {
                render_frame();
                dump_ppm(shot_path);
            }
        }
        return 0;
    }

    if (shot_path) {
        int settle = getenv("ELITE_SETTLE") ? atoi(getenv("ELITE_SETTLE")) : 60;
        CraftRawButtons none = {0};
        for (int i = 0; i < settle; i++) elite_game_tick(&none, 1.0f / 30.0f);
        render_frame();
        dump_ppm(shot_path);
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("ThumbyElite", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, ELITE_FB_W, ELITE_FB_H);

    bool running = true;
    Uint32 last_ms = SDL_GetTicks();
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN) {
                SDL_Scancode sc = ev.key.keysym.scancode;
                if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_F12)
                    running = false;
            }
        }
        Uint32 now_ms = SDL_GetTicks();
        float dt = (now_ms - last_ms) * 0.001f;
        if (dt > 0.1f) dt = 0.1f;
        last_ms = now_ms;

        const Uint8 *k = SDL_GetKeyboardState(NULL);
        CraftRawButtons btn = {
            .up    = k[SDL_SCANCODE_W],
            .down  = k[SDL_SCANCODE_S],
            .left  = k[SDL_SCANCODE_A],
            .right = k[SDL_SCANCODE_D],
            .a     = k[SDL_SCANCODE_PERIOD],
            .b     = k[SDL_SCANCODE_COMMA],
            .lb    = k[SDL_SCANCODE_LSHIFT],
            .rb    = k[SDL_SCANCODE_SPACE],
            .menu  = k[SDL_SCANCODE_RETURN],
        };

        Uint32 t0 = SDL_GetTicks();
        elite_game_tick(&btn, dt);
        render_frame();
        elite_game_set_frame_ms((float)(SDL_GetTicks() - t0));

        SDL_UpdateTexture(tex, NULL, g_fb, ELITE_FB_W * sizeof(uint16_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
