/*
 * ThumbyElite — top-level game module.
 *
 * Phase 3: combat arena. Cockpit-view dogfight against waves of AI
 * hostiles — full chord controls, pulse lasers, shields/hull/heat,
 * engine trails, explosions and the Elite scanner. Dying respawns you
 * after a beat; clearing a wave spawns a bigger one.
 */
#include "elite_game.h"
#include "elite_types.h"
#include "r3d_scene.h"
#include "r3d_fx.h"
#include "elite_entity.h"
#include "elite_input.h"
#include "elite_flight.h"
#include "elite_combat.h"
#include "elite_ai.h"
#include "ui_hud.h"
#include "craft_font.h"
#include "meshes_gen.h"
#include <stdio.h>

static float s_frame_ms;
static int   s_target = -1;
static int   s_wave;
static float s_wave_timer;     /* countdown to next wave / respawn */
static float s_respawn_timer;
static uint32_t s_rng;

static uint32_t xorshift32(void) {
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return s_rng;
}
static float frand(float lo, float hi) {
    return lo + (hi - lo) * (float)(xorshift32() & 0xFFFF) * (1.0f / 65535.0f);
}

static void spawn_player(void) {
    Ship *p = &g_ships[PLAYER];
    p->alive = true;
    p->mesh = &mesh_fighter;
    p->pos = v3(0, 0, 0);
    p->basis = m3_identity();
    p->vel = v3(0, 0, 0);
    p->throttle = 0.4f;
    p->assist = true;
    p->boost_t = 0;
    p->max_speed = 120.0f;
    p->accel = 60.0f;
    p->turn_rate = 2.1f;
    p->hull_max = 100.0f;
    p->hull = p->hull_max;
    p->shield_max = 80.0f;
    p->shield = p->shield_max;
    p->heat = 0;
    p->fire_cool = 0;
    p->team = TEAM_PLAYER;
}

static void spawn_wave(int wave) {
    int n = 1 + wave;          /* wave 1 = a gentle pair */
    if (n > 7) n = 7;
    Vec3 pp = g_ships[PLAYER].pos;
    for (int i = 0; i < n; i++) {
        /* Ring around the player, outside gun range. */
        float a = frand(0, 6.2831f);
        float r = frand(500, 800);
        Vec3 pos = v3(pp.x + cosf(a) * r, pp.y + frand(-150, 150),
                      pp.z + sinf(a) * r);
        const Mesh *m = (wave >= 2 && (i & 3) == 3) ? &mesh_freighter
                       : (i & 1) ? &mesh_viper : &mesh_fighter;
        int idx = ship_spawn(m, pos, TEAM_HOSTILE);
        if (idx > 0) {
            /* Face roughly toward the player. */
            g_ships[idx].basis = m3_identity();
            g_ships[idx].throttle = 0.8f;
        }
    }
}

static void cycle_target(void) {
    /* Nearest-first cycling: order hostiles by distance, pick the one
     * after the current target (wraps). */
    Vec3 pp = g_ships[PLAYER].pos;
    int best = -1, cur_found = 0;
    float cur_d = -1.0f, best_d = 1e30f, first_d = 1e30f;
    int first = -1;
    if (s_target >= 0 && g_ships[s_target].alive)
        cur_d = v3_len(v3_sub(g_ships[s_target].pos, pp));
    for (int i = 1; i < MAX_SHIPS; i++) {
        if (!g_ships[i].alive || g_ships[i].team != TEAM_HOSTILE) continue;
        float d = v3_len(v3_sub(g_ships[i].pos, pp));
        if (d < first_d) { first_d = d; first = i; }
        if (cur_d >= 0.0f &&
            (d > cur_d || (d == cur_d && i > s_target)) && d < best_d) {
            best_d = d;
            best = i;
        }
        (void)cur_found;
    }
    s_target = (best >= 0) ? best : first;
}

void elite_game_init(uint32_t seed) {
    s_rng = seed | 1u;
    r3d_starfield_init(seed ^ 0x5EEDu);
    ships_init();
    fx_init();
    combat_init();
    elite_input_reset();
    spawn_player();
    s_wave = 1;
    s_wave_timer = 0;
    s_respawn_timer = 0;
    s_target = -1;
    spawn_wave(s_wave);
}

void elite_game_set_frame_ms(float ms) { s_frame_ms = ms; }

void elite_game_tick(const CraftRawButtons *btn, float dt) {
    FlightInput in;
    elite_input_update(btn, dt, &in);

    Ship *p = &g_ships[PLAYER];
    static bool s_dead_latch = false;
    if (p->alive) {
        s_dead_latch = false;
        flight_apply_input(&in, dt);
        if (in.fire) combat_fire_laser(PLAYER, 0.0f);
        if (in.cycle_target) cycle_target();
    } else {
        if (!s_dead_latch) { s_dead_latch = true; s_respawn_timer = 2.5f; }
        s_respawn_timer -= dt;
        if (s_respawn_timer <= 0.0f) {
            spawn_player();
            s_target = -1;
        }
    }

    flight_tick(dt);
    ai_tick(dt);
    combat_tick(dt);
    fx_tick(dt);

    /* Engine trails for every thrusting ship (incl. the player — visible
     * when looking back over the shoulder later; cheap either way). */
    for (int i = 0; i < MAX_SHIPS; i++) {
        Ship *s = &g_ships[i];
        if (!s->alive) continue;
        Vec3 rear = v3_sub(s->pos, v3_scale(s->basis.r[2],
                                            s->mesh->bound_r * 0.8f));
        fx_engine_trail(rear, s->vel, s->throttle, dt);
    }

    /* Auto-drop dead targets; wave logic. */
    if (s_target >= 0 && !g_ships[s_target].alive) s_target = -1;
    if (ships_alive_hostile() == 0) {
        if (s_wave_timer <= 0.0f) s_wave_timer = 3.0f;
        s_wave_timer -= dt;
        if (s_wave_timer <= 0.0f) {
            s_wave++;
            spawn_wave(s_wave);
            s_wave_timer = 0;
        }
    }
}

void elite_game_render_begin(void) {
    Ship *p = &g_ships[PLAYER];
    r3d_scene_begin(&p->basis, 60.0f);

    for (int i = 1; i < MAX_SHIPS; i++) {
        if (!g_ships[i].alive) continue;
        R3DObject obj;
        obj.mesh = g_ships[i].mesh;
        obj.basis = g_ships[i].basis;
        obj.pos = v3_sub(g_ships[i].pos, p->pos);   /* camera-relative */
        r3d_scene_add_object(&obj);
    }
    fx_emit_all(p->pos, p->vel);
}

void elite_game_render(uint16_t *fb, int y_min, int y_max) {
    r3d_scene_raster(fb, y_min, y_max);
}

void elite_game_draw_overlay(uint16_t *fb) {
    Ship *p = &g_ships[PLAYER];
    if (p->alive) {
        HudInfo info = {
            .target = s_target,
            .wave = s_wave,
            .kills = combat_kills(),
            .render_ms = s_frame_ms,
            .show_perf = 1,
        };
        ui_hud_draw(fb, &info);
    } else {
        craft_font_draw_2x(fb, "SHIP LOST", 28, 52, RGB565C(255, 80, 60));
        craft_font_draw(fb, "RESPAWNING...", 38, 70, RGB565C(170, 170, 180));
    }
}
