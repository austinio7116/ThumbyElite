/*
 * ThumbyElite — top-level game module.
 *
 * Phase 2: mesh + draw-list + dual-core proof. The three baked ship
 * hulls (fighter / viper / freighter) tumble slowly in a starfield;
 * free-look on the d-pad, A/B to thrust. Frame time + triangle count
 * in the corner so device fps is measurable from a photo.
 */
#include "elite_game.h"
#include "elite_types.h"
#include "r3d_scene.h"
#include "craft_font.h"
#include "meshes_gen.h"
#include <stdio.h>

#define DEMO_SHIPS 7
static R3DObject s_ships[DEMO_SHIPS];
static float     s_spin[DEMO_SHIPS];

static Vec3  s_cam_pos;       /* logical camera position (world) */
static Mat3  s_cam_basis;
static float s_frame_ms;

static uint32_t s_rng;
static uint32_t xorshift32(void) {
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return s_rng;
}
static float frand(float lo, float hi) {
    return lo + (hi - lo) * (float)(xorshift32() & 0xFFFF) * (1.0f / 65535.0f);
}

void elite_game_init(uint32_t seed) {
    s_rng = seed | 1u;
    s_cam_pos = v3(0, 0, 0);
    s_cam_basis = m3_identity();
    r3d_starfield_init(seed ^ 0x5EEDu);

    static const Mesh *kinds[3];
    kinds[0] = &mesh_fighter;
    kinds[1] = &mesh_viper;
    kinds[2] = &mesh_freighter;

    /* A loose line-abreast of ships ahead of the camera. */
    for (int i = 0; i < DEMO_SHIPS; i++) {
        s_ships[i].mesh = kinds[i % 3];
        s_ships[i].basis = m3_identity();
        s_ships[i].pos = v3((i - DEMO_SHIPS / 2) * 14.0f,
                            frand(-5, 5), 28.0f + frand(-6, 14));
        s_spin[i] = frand(0.15f, 0.5f);
        m3_rotate_local(&s_ships[i].basis, 1, frand(0, 6.28f));
    }
}

void elite_game_set_frame_ms(float ms) { s_frame_ms = ms; }

void elite_game_tick(const CraftRawButtons *btn, float dt) {
    /* Free-look: d-pad pitches/yaws the camera; A/B thrust along forward. */
    const float turn = 1.6f * dt;
    if (btn->left)  m3_rotate_local(&s_cam_basis, 1, -turn);
    if (btn->right) m3_rotate_local(&s_cam_basis, 1,  turn);
    if (btn->up)    m3_rotate_local(&s_cam_basis, 0, -turn);
    if (btn->down)  m3_rotate_local(&s_cam_basis, 0,  turn);
    m3_orthonormalize(&s_cam_basis);

    const float speed = 12.0f * dt;
    if (btn->a) s_cam_pos = v3_add(s_cam_pos, v3_scale(s_cam_basis.r[2],  speed));
    if (btn->b) s_cam_pos = v3_add(s_cam_pos, v3_scale(s_cam_basis.r[2], -speed));

    for (int i = 0; i < DEMO_SHIPS; i++) {
        m3_rotate_local(&s_ships[i].basis, 1, s_spin[i] * dt);
        m3_rotate_local(&s_ships[i].basis, 0, s_spin[i] * 0.35f * dt);
    }
}

void elite_game_render_begin(void) {
    r3d_scene_begin(&s_cam_basis, 60.0f);
    for (int i = 0; i < DEMO_SHIPS; i++) {
        R3DObject obj = s_ships[i];
        obj.pos = v3_sub(obj.pos, s_cam_pos);   /* camera-relative */
        r3d_scene_add_object(&obj);
    }
}

void elite_game_render(uint16_t *fb, int y_min, int y_max) {
    r3d_scene_raster(fb, y_min, y_max);
}

void elite_game_draw_overlay(uint16_t *fb) {
    char buf[32];
    snprintf(buf, sizeof buf, "%d.%dMS %dTRI",
             (int)s_frame_ms, ((int)(s_frame_ms * 10.0f)) % 10,
             r3d_scene_tri_count());
    craft_font_draw(fb, buf, 2, 2, rgb565(120, 255, 120));
}
