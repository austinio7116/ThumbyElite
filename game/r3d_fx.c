/*
 * ThumbyElite — particle / beam effects.
 */
#include "r3d_fx.h"
#include "r3d_scene.h"
#include "elite_types.h"
#include <string.h>

#define MAX_PARTICLES 192
#define MAX_BEAMS     8

typedef struct {
    Vec3  pos, vel;
    float life, max_life;
    uint16_t color0, color1;   /* lerp colour over life (hot -> cool) */
} Particle;

typedef struct {
    Vec3  a, b;
    float life;
    uint16_t color;
} Beam;

static Particle s_parts[MAX_PARTICLES];
static Beam     s_beams[MAX_BEAMS];
static int      s_nparts;
static float    s_trail_accum;

static uint32_t s_rng = 0xC0FFEE11u;
static uint32_t frnd(void) {
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return s_rng;
}
static float frand(float lo, float hi) {
    return lo + (hi - lo) * (float)(frnd() & 0xFFFF) * (1.0f / 65535.0f);
}
static Vec3 rnd_dir(void) {
    Vec3 d = v3(frand(-1, 1), frand(-1, 1), frand(-1, 1));
    return (v3_len2(d) < 1e-4f) ? v3(0, 1, 0) : v3_norm(d);
}

void fx_init(void) {
    memset(s_parts, 0, sizeof s_parts);
    memset(s_beams, 0, sizeof s_beams);
    s_nparts = 0;
    s_trail_accum = 0;
}

/* Defined below fireball storage; cleared via dur<=t sentinel at init —
 * static zero init means t==dur==0, i.e. inactive. */

int fx_alive_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_PARTICLES; i++) if (s_parts[i].life > 0) n++;
    return n;
}

static Particle *alloc_part(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        int k = (s_nparts + i) % MAX_PARTICLES;
        if (s_parts[k].life <= 0) { s_nparts = k + 1; return &s_parts[k]; }
    }
    return NULL;   /* pool full: drop, never steal (explosions stay whole) */
}

static void spawn(Vec3 pos, Vec3 vel, float life, uint16_t c0, uint16_t c1) {
    Particle *p = alloc_part();
    if (!p) return;
    p->pos = pos; p->vel = vel;
    p->life = p->max_life = life;
    p->color0 = c0; p->color1 = c1;
}

/* --- Fireballs: expanding depth-tested discs — the BIG readable part of
 * an explosion at any distance (debris particles alone vanish at range). */
#define MAX_FIREBALLS 6
typedef struct {
    Vec3  pos, vel;
    float t, dur;        /* age / duration */
    float r_max;         /* world-space radius at peak, meters */
} Fireball;
static Fireball s_fire[MAX_FIREBALLS];

static void fireball(Vec3 pos, Vec3 vel, float r_max, float dur) {
    int oldest = 0;
    for (int i = 0; i < MAX_FIREBALLS; i++) {
        if (s_fire[i].t >= s_fire[i].dur) { oldest = i; break; }
        if (s_fire[i].t > s_fire[oldest].t) oldest = i;
    }
    s_fire[oldest].pos = pos;
    s_fire[oldest].vel = vel;
    s_fire[oldest].t = 0;
    s_fire[oldest].dur = dur;
    s_fire[oldest].r_max = r_max;
}

void fx_spawn_explosion(Vec3 pos, Vec3 base_vel) {
    /* Core flash + fireball + secondary pops. */
    fireball(pos, base_vel, 14.0f, 0.9f);
    fireball(v3_add(pos, v3_scale(rnd_dir(), 4.0f)),
             base_vel, 7.0f, 0.65f);
    fireball(v3_add(pos, v3_scale(rnd_dir(), 6.0f)),
             base_vel, 5.0f, 1.1f);
    /* Debris: fast, plentiful, long-lived. */
    for (int i = 0; i < 48; i++) {
        Vec3 v = v3_add(base_vel, v3_scale(rnd_dir(), frand(8, 45)));
        float life = frand(0.6f, 1.8f);
        uint16_t hot = (i & 3) ? RGB565C(255, 200, 80) : RGB565C(255, 255, 220);
        spawn(pos, v, life, hot, RGB565C(90, 30, 10));
    }
}

void fx_spawn_spark(Vec3 pos, Vec3 base_vel) {
    /* A visible impact: brief flash disc + a spray of embers. */
    fireball(pos, base_vel, 2.6f, 0.18f);
    for (int i = 0; i < 10; i++) {
        Vec3 v = v3_add(base_vel, v3_scale(rnd_dir(), frand(6, 20)));
        spawn(pos, v, frand(0.2f, 0.45f),
              RGB565C(255, 240, 160), RGB565C(180, 80, 30));
    }
}

void fx_engine_trail(Vec3 rear_pos, Vec3 ship_vel, float throttle, float dt) {
    if (throttle < 0.15f) return;
    /* Emission rate scales with throttle; accumulate fractional spawns. */
    s_trail_accum += throttle * 40.0f * dt;
    while (s_trail_accum >= 1.0f) {
        s_trail_accum -= 1.0f;
        Vec3 jitter = v3_scale(rnd_dir(), 0.25f);
        spawn(v3_add(rear_pos, jitter),
              v3_add(v3_scale(ship_vel, 0.2f), v3_scale(rnd_dir(), 1.5f)),
              frand(0.25f, 0.5f),
              RGB565C(120, 190, 255), RGB565C(30, 50, 110));
    }
}

void fx_beam(Vec3 from, Vec3 to, uint16_t color) {
    for (int i = 0; i < MAX_BEAMS; i++) {
        if (s_beams[i].life > 0) continue;
        s_beams[i].a = from;
        s_beams[i].b = to;
        s_beams[i].life = 0.07f;
        s_beams[i].color = color;
        return;
    }
}

void fx_tick(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &s_parts[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
    }
    for (int i = 0; i < MAX_BEAMS; i++)
        if (s_beams[i].life > 0) s_beams[i].life -= dt;
    for (int i = 0; i < MAX_FIREBALLS; i++) {
        Fireball *f = &s_fire[i];
        if (f->t >= f->dur) continue;
        f->t += dt;
        f->pos = v3_add(f->pos, v3_scale(f->vel, dt));
    }
}

static uint16_t lerp565(uint16_t a, uint16_t b, float t) {
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    int r = ar + (int)((br - ar) * t);
    int g = ag + (int)((bg - ag) * t);
    int bl = ab + (int)((bb - ab) * t);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

/* --- Space dust --------------------------------------------------------
 * Two wrapping shells around the camera give translation cues the
 * at-infinity starfield can't:
 *   near shell: bright motes, +-60m, drawn as velocity streaks at speed
 *   far shell:  dim points, +-500m — distant scenery that drifts slowly
 * Positions wrap per-axis so the cloud always surrounds the player. */
#define DUST_NEAR_N 30
#define DUST_FAR_N  20
#define DUST_NEAR_R 60.0f
#define DUST_FAR_R  500.0f
static Vec3 s_dust_near[DUST_NEAR_N];
static Vec3 s_dust_far[DUST_FAR_N];
static int  s_dust_seeded;

static void dust_seed(Vec3 c) {
    for (int i = 0; i < DUST_NEAR_N; i++)
        s_dust_near[i] = v3_add(c, v3(frand(-DUST_NEAR_R, DUST_NEAR_R),
                                      frand(-DUST_NEAR_R, DUST_NEAR_R),
                                      frand(-DUST_NEAR_R, DUST_NEAR_R)));
    for (int i = 0; i < DUST_FAR_N; i++)
        s_dust_far[i] = v3_add(c, v3(frand(-DUST_FAR_R, DUST_FAR_R),
                                     frand(-DUST_FAR_R, DUST_FAR_R),
                                     frand(-DUST_FAR_R, DUST_FAR_R)));
    s_dust_seeded = 1;
}

static void dust_wrap(Vec3 *p, Vec3 c, float r) {
    if (p->x - c.x >  r) p->x -= 2 * r;
    if (p->x - c.x < -r) p->x += 2 * r;
    if (p->y - c.y >  r) p->y -= 2 * r;
    if (p->y - c.y < -r) p->y += 2 * r;
    if (p->z - c.z >  r) p->z -= 2 * r;
    if (p->z - c.z < -r) p->z += 2 * r;
}

static void dust_emit(Vec3 cam_pos, Vec3 cam_vel) {
    if (!s_dust_seeded) dust_seed(cam_pos);
    float speed = v3_len(cam_vel);
    /* Streak = where this mote appeared a short moment ago, relative to
     * us — i.e. offset by +vel*dt_streak (we moved, the dust didn't). */
    Vec3 streak = v3_scale(cam_vel, 0.045f);
    int do_streaks = speed > 25.0f;
    uint16_t cn = RGB565C(150, 160, 175);
    for (int i = 0; i < DUST_NEAR_N; i++) {
        dust_wrap(&s_dust_near[i], cam_pos, DUST_NEAR_R);
        Vec3 rel = v3_sub(s_dust_near[i], cam_pos);
        float sx, sy;
        uint16_t d;
        if (!r3d_scene_project(rel, &sx, &sy, &d)) continue;
        if (sx < -4 || sx > 132 || sy < -4 || sy > 132) continue;
        if (do_streaks) {
            float ex, ey;
            uint16_t ed;
            if (r3d_scene_project(v3_add(rel, streak), &ex, &ey, &ed))
                r3d_scene_add_line(sx, sy, d, ex, ey, ed, cn);
        } else {
            r3d_scene_add_point(sx, sy, d, cn, 1);
        }
    }
    uint16_t cf = RGB565C(90, 95, 110);
    for (int i = 0; i < DUST_FAR_N; i++) {
        dust_wrap(&s_dust_far[i], cam_pos, DUST_FAR_R);
        float sx, sy;
        uint16_t d;
        if (!r3d_scene_project(v3_sub(s_dust_far[i], cam_pos), &sx, &sy, &d))
            continue;
        if (sx < 0 || sx > 127 || sy < 0 || sy > 127) continue;
        r3d_scene_add_point(sx, sy, d, cf, 1);
    }
}

/* Fireball colour ramp: white flash -> yellow -> orange -> dark ember. */
static uint16_t fireball_color(float t01) {
    if (t01 < 0.15f) return RGB565C(255, 255, 235);
    if (t01 < 0.40f) return RGB565C(255, 215, 90);
    if (t01 < 0.70f) return RGB565C(245, 130, 40);
    return RGB565C(140, 50, 18);
}

static void fireballs_emit(Vec3 cam_pos) {
    float focal = r3d_pipe_focal();
    for (int i = 0; i < MAX_FIREBALLS; i++) {
        const Fireball *f = &s_fire[i];
        if (f->t >= f->dur) continue;
        float t01 = f->t / f->dur;
        /* Fast expansion, brief hold, slight shrink as it gutters out. */
        float grow = t01 < 0.35f ? (t01 / 0.35f)
                                 : 1.0f - 0.35f * ((t01 - 0.35f) / 0.65f);
        float r_world = f->r_max * grow;
        float sx, sy;
        uint16_t d;
        if (!r3d_scene_project(v3_sub(f->pos, cam_pos), &sx, &sy, &d)) continue;
        if (sx < -40 || sx > 168 || sy < -40 || sy > 168) continue;
        float z = R3D_DEPTH_K / (float)(d > 0 ? d : 1);
        int r_px = (int)(focal * r_world / z);
        if (r_px < 1) r_px = 1;
        r3d_scene_add_disc(sx, sy, d, r_px, fireball_color(t01));
    }
}

/* --- Supercruise debris -------------------------------------------------
 * Mm-scale wrapping motes. The wrap box grows with speed so there is
 * always *something* streaming past; streaks stretch with velocity. */
#define SC_DUST_N 26
static Vec3 s_sc_dust[SC_DUST_N];
static int  s_sc_seeded;

void fx_sc_dust_emit(Vec3 cam_pos_mm, Vec3 vel_mms) {
    float speed = v3_len(vel_mms);
    float R = speed * 0.35f;
    if (R < 1.2f) R = 1.2f;
    if (R > 260.0f) R = 260.0f;
    if (!s_sc_seeded) {
        s_sc_seeded = 1;
        for (int i = 0; i < SC_DUST_N; i++)
            s_sc_dust[i] = v3_add(cam_pos_mm,
                                  v3(frand(-R, R), frand(-R, R), frand(-R, R)));
    }
    Vec3 streak = v3_scale(vel_mms, 0.05f);
    uint16_t c = RGB565C(130, 140, 165);
    for (int i = 0; i < SC_DUST_N; i++) {
        dust_wrap(&s_sc_dust[i], cam_pos_mm, R);
        Vec3 rel = v3_scale(v3_sub(s_sc_dust[i], cam_pos_mm), 1.0e6f);
        float sx, sy;
        uint16_t d;
        if (!r3d_scene_project(rel, &sx, &sy, &d)) continue;
        if (sx < -8 || sx > 136 || sy < -8 || sy > 136) continue;
        if (speed > 0.02f) {
            float ex, ey;
            uint16_t ed;
            Vec3 rel2 = v3_add(rel, v3_scale(streak, 1.0e6f));
            if (r3d_scene_project(rel2, &ex, &ey, &ed))
                r3d_scene_add_line(sx, sy, d, ex, ey, ed, c);
        } else {
            r3d_scene_add_point(sx, sy, d, c, 1);
        }
    }
}

void fx_emit_all(Vec3 cam_pos, Vec3 cam_vel) {
    dust_emit(cam_pos, cam_vel);
    fireballs_emit(cam_pos);
    for (int i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &s_parts[i];
        if (p->life <= 0) continue;
        float sx, sy;
        uint16_t d;
        if (!r3d_scene_project(v3_sub(p->pos, cam_pos), &sx, &sy, &d)) continue;
        if (sx < -2 || sx > 130 || sy < -2 || sy > 130) continue;
        float t = 1.0f - p->life / p->max_life;
        /* Near particles get a 2x2 block (d ~ K/z: bigger = nearer). */
        uint8_t size = (d > 800) ? 2 : 1;
        r3d_scene_add_point(sx, sy, d, lerp565(p->color0, p->color1, t), size);
    }
    for (int i = 0; i < MAX_BEAMS; i++) {
        const Beam *bm = &s_beams[i];
        if (bm->life <= 0) continue;
        float x0, y0, x1, y1;
        uint16_t d0, d1;
        if (!r3d_scene_project(v3_sub(bm->a, cam_pos), &x0, &y0, &d0)) continue;
        if (!r3d_scene_project(v3_sub(bm->b, cam_pos), &x1, &y1, &d1)) continue;
        r3d_scene_add_line(x0, y0, d0, x1, y1, d1, bm->color);
    }
}
