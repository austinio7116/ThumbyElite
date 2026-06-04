/*
 * ThumbyElite — top-level game module.
 *
 * Phase 1: renderer proof. A field of rotating flat-shaded cubes around
 * the camera, free-look on the d-pad, A/B to thrust forward/back. Frame
 * time in the corner so device fps is measurable from a photo.
 */
#include "elite_game.h"
#include "elite_types.h"
#include "r3d_pipe.h"
#include "r3d_raster.h"
#include "craft_font.h"
#include <stdio.h>

/* --- Phase 1 demo mesh: unit cube, CCW from outside ------------------- */
static const MeshVert cube_verts[8] = {
    {-100, -100, -100}, { 100, -100, -100}, { 100,  100, -100}, {-100,  100, -100},
    {-100, -100,  100}, { 100, -100,  100}, { 100,  100,  100}, {-100,  100,  100},
};
#define CUBE_GREY  RGB565C(168, 170, 178)
#define CUBE_DARK  RGB565C(120, 122, 132)
#define CUBE_RED   RGB565C(196,  60,  50)
static const MeshFace cube_faces[12] = {
    { 4, 5, 6,    0,    0,  127, CUBE_GREY }, { 4, 6, 7,    0,    0,  127, CUBE_GREY },
    { 0, 3, 2,    0,    0, -127, CUBE_GREY }, { 0, 2, 1,    0,    0, -127, CUBE_GREY },
    { 1, 2, 6,  127,    0,    0, CUBE_DARK }, { 1, 6, 5,  127,    0,    0, CUBE_DARK },
    { 0, 4, 7, -127,    0,    0, CUBE_DARK }, { 0, 7, 3, -127,    0,    0, CUBE_DARK },
    { 2, 3, 7,    0,  127,    0, CUBE_RED  }, { 2, 7, 6,    0,  127,    0, CUBE_RED  },
    { 0, 1, 5,    0, -127,    0, CUBE_DARK }, { 0, 5, 4,    0, -127,    0, CUBE_DARK },
};
static const Mesh cube_mesh = {
    cube_verts, cube_faces, 8, 12,
    1.0f,        /* half-extent 1 m */
    1.74f,       /* bounding sphere */
    0,
};

#define DEMO_CUBES 24
static R3DObject s_cubes[DEMO_CUBES];
static float     s_spin[DEMO_CUBES];

static Vec3  s_cam_pos;       /* logical camera position (world) */
static Mat3  s_cam_basis;
static float s_frame_ms;
static int   s_tris_drawn;

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
    for (int i = 0; i < DEMO_CUBES; i++) {
        s_cubes[i].mesh = &cube_mesh;
        s_cubes[i].basis = m3_identity();
        s_cubes[i].pos = v3(frand(-12, 12), frand(-12, 12), frand(4, 30));
        s_spin[i] = frand(0.3f, 1.6f);
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

    const float speed = 6.0f * dt;
    if (btn->a) s_cam_pos = v3_add(s_cam_pos, v3_scale(s_cam_basis.r[2],  speed));
    if (btn->b) s_cam_pos = v3_add(s_cam_pos, v3_scale(s_cam_basis.r[2], -speed));

    /* Spin the cubes. */
    for (int i = 0; i < DEMO_CUBES; i++) {
        m3_rotate_local(&s_cubes[i].basis, 1, s_spin[i] * dt);
        m3_rotate_local(&s_cubes[i].basis, 0, s_spin[i] * 0.6f * dt);
    }
}

void elite_game_render_begin(void) {
    r3d_pipe_set_camera(&s_cam_basis, 60.0f);
    s_tris_drawn = 0;
}

void elite_game_render(uint16_t *fb, int y_min, int y_max) {
    /* Background + depth clear for this band. */
    for (int y = y_min; y < y_max; y++) {
        uint16_t *row = fb + y * ELITE_FB_W;
        for (int x = 0; x < ELITE_FB_W; x++) row[x] = 0x0000;
    }
    r3d_raster_set_fb(fb);
    r3d_depth_clear(y_min, y_max);

    int tris = 0;
    for (int i = 0; i < DEMO_CUBES; i++) {
        R3DObject obj = s_cubes[i];
        obj.pos = v3_sub(obj.pos, s_cam_pos);   /* camera-relative */
        tris += r3d_pipe_draw_object(&obj, y_min, y_max);
    }
    s_tris_drawn += tris;
}

void elite_game_draw_overlay(uint16_t *fb) {
    char buf[32];
    snprintf(buf, sizeof buf, "%d.%dMS %dTRI",
             (int)s_frame_ms, ((int)(s_frame_ms * 10.0f)) % 10, s_tris_drawn);
    craft_font_draw(fb, buf, 2, 2, rgb565(120, 255, 120));
}
