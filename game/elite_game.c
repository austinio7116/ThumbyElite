/*
 * ThumbyElite — top-level game module.
 *
 * Phase 4: the galaxy. State machine over flight / supercruise /
 * hyperspace / maps / pause. TWO COORDINATE SCALES:
 *
 *   system space  — megameters (Mm), f32: planet/station/beacon layout
 *   local space   — meters relative to s_anchor_mm: ships, combat, FX
 *
 * Every supercruise drop RE-ANCHORS the local frame, so combat floats
 * stay tiny no matter where in the system you are. Planet impostors are
 * fed the camera's absolute Mm position and handle their own scaling.
 */
#include "elite_game.h"
#include "elite_types.h"
#include "r3d_scene.h"
#include "r3d_planet.h"
#include "r3d_fx.h"
#include "elite_entity.h"
#include "elite_input.h"
#include "elite_flight.h"
#include "elite_combat.h"
#include "elite_proj.h"
#include "elite_loot.h"
#include "mission.h"
#include "elite_ai.h"
#include "ui_hud.h"
#include "ui_map.h"
#include "ui_station.h"
#include "ui_status.h"
#include "elite_player.h"
#include "elite_audio.h"
#include "elite_save.h"
#include "elite_platform.h"
#include "system_sim.h"
#include "station_gen.h"
#include "elite_ships.h"
#include "galaxy_gen.h"
#include "craft_font.h"
#include "meshes_gen.h"
#include <stdio.h>
#include <string.h>

typedef enum {
    ST_FLIGHT = 0, ST_SUPERCRUISE, ST_HYPERJUMP,
    ST_GALAXY_MAP, ST_SYSTEM_MAP, ST_PAUSE,
    ST_DOCKING, ST_DOCKED, ST_STATUS, ST_TITLE,
} GState;

#define DOCK_RANGE 600.0f

#define JUMP_RANGE 7.5f
#define HYPER_TIME 2.6f
#define SC_DROP_MM 1.2f          /* auto-drop distance to destination */

static GState  s_state;
static SysAddr s_addr;           /* current system */
static Vec3    s_anchor_mm;      /* local-frame origin in system space */
static Poi     s_anchor_poi;     /* what we're anchored at */
static bool    s_anchor_has_poi;

static Vec3    s_sc_pos_mm;      /* supercruise position */
static Poi     s_sc_dest;
static bool    s_sc_has_dest;
static float   s_sc_speed;
static float   s_sc_eta;

static SysAddr s_jump_target;
static float   s_jump_dist;
static float   s_hyper_t;
static uint32_t s_hyper_seed;

static int   s_target = -1;      /* combat lock */
static uint32_t s_boot_seed;
static int   s_title_cursor;
static char  s_scoop_toast[28];
static float s_scoop_toast_t;
static float s_frame_ms;
static int   s_pause_cursor;
static bool  s_prev_menu, s_prev_a;

static uint32_t s_rng;
static uint32_t xorshift32(void) {
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return s_rng;
}
static float frand(float lo, float hi) {
    return lo + (hi - lo) * (float)(xorshift32() & 0xFFFF) * (1.0f / 65535.0f);
}

/* Wall-clock-ish for ambient animation (sum of frame steps). */
static float s_time;
float elite_game_time(void) { return s_time; }

int elite_game_state(void) { return (int)s_state; }

/* Debug/demo: force-spawn hostiles around the player (host harness). */
void elite_game_debug_spawn(int n) {
    extern const Mesh mesh_viper, mesh_fighter;
    for (int i = 0; i < n; i++) {
        float a = frand(0, 6.2831f);
        float r = frand(500, 800);
        Vec3 pos = v3(cosf(a) * r, frand(-150, 150), sinf(a) * r);
        int idx = ship_spawn((i & 1) ? &mesh_viper : &mesh_fighter, pos,
                             TEAM_HOSTILE);
        if (idx > 0) ship_set_tier(idx, i % 5);
    }
}

/* Camera position in system space (Mm) for planet projection. */
static Vec3 cam_pos_mm(void) {
    if (s_state == ST_SUPERCRUISE) return s_sc_pos_mm;
    return v3_add(s_anchor_mm, v3_scale(g_ships[PLAYER].pos, 1.0e-6f));
}

/* --- player ------------------------------------------------------------*/
static void spawn_player(void) {
    Ship *p = &g_ships[PLAYER];
    p->alive = true;
    p->pos = v3(0, 0, 0);
    p->basis = m3_identity();
    p->vel = v3(0, 0, 0);
    p->throttle = 0.3f;
    p->assist = true;
    p->boost_t = 0;
    p->heat = 0;
    p->fire_cool = 0;
    p->team = TEAM_PLAYER;
    player_apply_to_ship();      /* hull, tiers, mounts, skills */
    p->hull = p->hull_max;
    p->shield = p->shield_max;
}

/* --- POI content ---------------------------------------------------------
 * Spawned once per supercruise drop / jump arrival. Pirates scale with
 * system threat; high-security space is quiet. */
static void spawn_poi_content(void) {
    const SystemInfo *si = system_info();
    int pirates = 0;
    if (si->threat >= 1 && s_anchor_has_poi) {
        /* Beacons and planets attract trouble; stations are patrolled. */
        int roll = (int)(xorshift32() % 100u);
        int chance = (s_anchor_poi.kind == POI_STATION) ? 25 : 55;
        if (roll < chance) pirates = 1 + (int)(xorshift32() % si->threat);
        if (pirates > 4) pirates = 4;
    }
    for (int i = 0; i < pirates; i++) {
        float a = frand(0, 6.2831f);
        float r = frand(600, 1000);
        Vec3 pos = v3(cosf(a) * r, frand(-200, 200), sinf(a) * r);
        const Mesh *m = (i & 1) ? &mesh_viper : &mesh_fighter;
        int idx = ship_spawn(m, pos, TEAM_HOSTILE);
        if (idx > 0) {
            int tier = (int)si->threat - 1 + (int)(xorshift32() % 3u) - 1;
            ship_set_tier(idx, tier);
        }
    }

    /* Bounty mark: a flagged ace waits at the beacon. */
    if (s_anchor_has_poi && s_anchor_poi.kind == POI_BEACON &&
        mission_bounty_here(s_addr)) {
        float a = frand(0, 6.2831f);
        Vec3 pos = v3(cosf(a) * 800.0f, frand(-150, 150), sinf(a) * 800.0f);
        int idx = ship_spawn(&mesh_cutter, pos, TEAM_HOSTILE);
        if (idx > 0) {
            ship_set_tier(idx, 4);
            g_ships[idx].is_mark = 1;
        }
    }
}

/* Re-anchor the local frame at a system-space position. */
static const Mesh *s_station_mesh;   /* generated for the anchored station */

static void drop_anchor(Vec3 pos_mm, const Poi *poi) {
    s_anchor_mm = pos_mm;
    s_anchor_has_poi = (poi != NULL);
    if (poi) s_anchor_poi = *poi;
    if (poi && poi->kind == POI_STATION)
        s_station_mesh = station_gen_mesh(
            (uint32_t)(system_info()->seed >> 8) ^
            (uint32_t)(poi->index * 0x9E3779B9u));
    ships_despawn_npcs();
    fx_init();
    loot_init();
    s_target = -1;
}

static void arrive_in_system(SysAddr addr) {
    s_addr = addr;
    system_enter(addr);
    Poi beacon;
    Poi pois[MAX_POIS];
    int n = system_pois(pois, MAX_POIS);
    beacon = pois[0];                       /* beacon is always first */
    (void)n;
    drop_anchor(beacon.pos_mm, &beacon);
    Ship *p = &g_ships[PLAYER];
    p->pos = v3(frand(-300, 300), frand(-100, 100), -700.0f);
    p->vel = v3_scale(p->basis.r[2], 40.0f);
    spawn_poi_content();
}

/* --- target cycling (unchanged from Phase 3) ----------------------------*/
static void cycle_target(void) {
    Vec3 pp = g_ships[PLAYER].pos;
    int best = -1, first = -1;
    float cur_d = -1.0f, best_d = 1e30f, first_d = 1e30f;
    if (s_target >= 0 && g_ships[s_target].alive)
        cur_d = v3_len(v3_sub(g_ships[s_target].pos, pp));
    for (int i = 1; i < MAX_SHIPS; i++) {
        if (!g_ships[i].alive || g_ships[i].team != TEAM_HOSTILE) continue;
        float d = v3_len(v3_sub(g_ships[i].pos, pp));
        if (d < first_d) { first_d = d; first = i; }
        if (cur_d >= 0.0f && d > cur_d && d < best_d) { best_d = d; best = i; }
    }
    s_target = (best >= 0) ? best : first;
}

/* Resume docked at a saved station. */
static void arrive_docked(const SaveMeta *meta) {
    s_addr = meta->addr;
    system_enter(meta->addr);
    Poi pois[MAX_POIS];
    int n = system_pois(pois, MAX_POIS);
    const Poi *st = NULL;
    for (int i = 0; i < n; i++)
        if (pois[i].kind == POI_STATION && pois[i].index == meta->station)
            st = &pois[i];
    if (!st) {                       /* damaged save: fall back to beacon */
        drop_anchor(pois[0].pos_mm, &pois[0]);
        spawn_player();
        return;
    }
    drop_anchor(st->pos_mm, st);
    spawn_player();
    station_open(st->index);
    s_state = ST_DOCKED;
}

static void start_new_game(uint32_t seed);

void elite_game_init(uint32_t seed) {
    s_rng = seed | 1u;
    s_boot_seed = seed;
    galaxy_set_seed(seed);
    ships_init();
    fx_init();
    audio_init();
    combat_init();
    elite_input_reset();
    spawn_player();
    player_init();
    missions_init();
    r3d_starfield_init(seed ^ 0x7117u);
    s_state = ST_TITLE;
    s_title_cursor = save_exists() ? 0 : 1;
    s_prev_menu = s_prev_a = false;
}

static void start_new_game(uint32_t seed) {
    galaxy_set_seed(seed);
    ships_init();
    fx_init();
    combat_init();
    elite_input_reset();
    spawn_player();
    player_init();
    missions_init();
    s_state = ST_FLIGHT;

    /* Find a starting system: spiral out from the origin for the first
     * system WITH A STATION (a home dock for fuel/trade), falling back
     * to any populated sector if the neighbourhood is barren. */
    SysAddr start = {0, 0, 0};
    SysAddr fallback = {0, 0, 0};
    bool found = false, have_fallback = false;
    for (int ring = 0; ring < 10 && !found; ring++)
        for (int sy = -ring; sy <= ring && !found; sy++)
            for (int sx = -ring; sx <= ring && !found; sx++) {
                if (sx > -ring && sx < ring && sy > -ring && sy < ring)
                    continue;
                int n = galaxy_sector_stars(sx, sy);
                for (int i = 0; i < n && !found; i++) {
                    SysAddr a = { sx, sy, (uint8_t)i };
                    if (!have_fallback) { fallback = a; have_fallback = true; }
                    SystemInfo probe;
                    galaxy_generate(a, &probe);
                    if (probe.n_stations > 0) { start = a; found = true; }
                }
            }
    arrive_in_system(found ? start : fallback);
    r3d_starfield_init((uint32_t)(system_info()->seed >> 16));
}

void elite_game_set_frame_ms(float ms) { s_frame_ms = ms; }

/* --- state ticks ---------------------------------------------------------*/
/* Docking availability: anchored at a station, close, not under fire. */
static bool can_dock(void) {
    if (!s_anchor_has_poi || s_anchor_poi.kind != POI_STATION) return false;
    if (!g_ships[PLAYER].alive) return false;
    return v3_len(g_ships[PLAYER].pos) < DOCK_RANGE;
}

static float s_dock_t;

static void tick_flight(const CraftRawButtons *btn, float dt) {
    FlightInput in;
    elite_input_update(btn, dt, &in);

    Ship *p = &g_ships[PLAYER];
    static bool dead_latch = false;
    static float respawn_t;
    if (p->alive) {
        dead_latch = false;

        /* LB+RB chord near a station = engage docking computer. */
        if (btn->lb && btn->rb && can_dock()) {
            s_dock_t = 0;
            s_state = ST_DOCKING;
            return;
        }

        flight_apply_input(&in, dt);
        if (in.fire) combat_fire(PLAYER, 0.0f, s_target);
        if (in.secondary && p->n_weapons > 1)       /* B = next weapon */
            p->active_w = (uint8_t)((p->active_w + 1) % p->n_weapons);
        if (in.cycle_target) cycle_target();
    } else {
        if (!dead_latch) { dead_latch = true; respawn_t = 3.0f; }
        respawn_t -= dt;
        if (respawn_t <= 0.0f) {
            /* Insurance: revert to the last dock save (journey since is
             * lost). No save yet -> fresh hull at the local beacon. */
            SaveMeta meta;
            if (save_exists() && save_load(&meta)) {
                combat_set_kills(meta.kills);
                arrive_docked(&meta);
            } else {
                spawn_player();
                Poi pois[MAX_POIS];
                system_pois(pois, MAX_POIS);
                drop_anchor(pois[0].pos_mm, &pois[0]);
                p->pos = v3(0, 0, -700.0f);
                spawn_poi_content();
            }
        }
    }

    flight_tick(dt);
    ai_tick(dt);
    combat_tick(dt);
    fx_tick(dt);

    /* Overheat klaxon (repeats while hot) + throttle-following hum. */
    {
        static float klaxon_t;
        klaxon_t -= dt;
        if (p->alive && p->heat > 88.0f && klaxon_t <= 0.0f) {
            sfx_klaxon();
            klaxon_t = 0.7f;
        }
        audio_engine_set(p->throttle,
                         v3_len(p->vel) / (p->max_speed * 1.2f));
    }
    {
        const char *scooped = loot_tick(dt);
        if (scooped) {
            snprintf(s_scoop_toast, sizeof s_scoop_toast, "%s", scooped);
            s_scoop_toast_t = 2.0f;
        }
        if (s_scoop_toast_t > 0) s_scoop_toast_t -= dt;
    }

    for (int i = 0; i < MAX_SHIPS; i++) {
        Ship *s = &g_ships[i];
        if (!s->alive) continue;
        Vec3 rear = v3_sub(s->pos, v3_scale(s->basis.r[2],
                                            s->mesh->bound_r * 0.8f));
        fx_engine_trail(rear, s->vel, s->throttle, dt);
    }
    if (s_target >= 0 && !g_ships[s_target].alive) s_target = -1;
}

static void tick_supercruise(const CraftRawButtons *btn, float dt) {
    FlightInput in;
    elite_input_update(btn, dt, &in);
    Ship *p = &g_ships[PLAYER];

    float tr = p->turn_rate * 0.6f * dt;     /* heavier helm at SC speed */
    if (in.pitch != 0.0f) m3_rotate_local(&p->basis, 0, in.pitch * tr);
    if (in.yaw   != 0.0f) m3_rotate_local(&p->basis, 1, in.yaw * tr);
    if (in.roll  != 0.0f) m3_rotate_local(&p->basis, 2, in.roll * tr * 1.5f);
    m3_orthonormalize(&p->basis);
    p->throttle += in.throttle_delta * 0.9f * dt;
    if (p->throttle < 0.0f) p->throttle = 0.0f;
    if (p->throttle > 1.0f) p->throttle = 1.0f;

    /* Speed envelope: approach-limited near the destination so arrival
     * is automatic, opening to 500 Mm/s in deep space. Planets keep a
     * standoff so we don't cruise into the lithosphere. */
    float standoff = 0.0f;
    if (s_sc_has_dest && s_sc_dest.kind == POI_PLANET)
        standoff = system_info()->planets[s_sc_dest.index].radius_mm * 3.5f;
    float vmax = 3000.0f;
    float dist = 1e9f;
    if (s_sc_has_dest) {
        dist = v3_len(v3_sub(s_sc_dest.pos_mm, s_sc_pos_mm));
        float eff = dist - standoff;
        if (eff < 0.0f) eff = 0.0f;
        /* eff/2 decay + 2 Mm/s floor: ~20s hops (user: less waiting). */
        float lim = eff * 0.5f + 2.0f;
        if (lim < vmax) vmax = lim;
    }
    float want = p->throttle * vmax;
    /* Smooth speed chase. */
    s_sc_speed += (want - s_sc_speed) * (dt * 2.0f > 1 ? 1 : dt * 2.0f);

    /* Envelope-aware ETA: cruise at the cap, then the exponential
     * approach (v = eff/2 + 2 -> t = 2*ln ratio), scaled by throttle. */
    if (s_sc_has_dest) {
        float thr = p->throttle < 0.05f ? 0.05f : p->throttle;
        float eff = dist - standoff;
        if (eff < 0.0f) eff = 0.0f;
        float vcap = 3000.0f * thr;
        float eff_decay = 2.0f * (vcap - 2.0f);
        if (eff_decay < 1.0f) eff_decay = 1.0f;
        float t = 0.0f;
        float e0 = eff;
        if (e0 > eff_decay) {
            t += (e0 - eff_decay) / vcap;
            e0 = eff_decay;
        }
        t += (2.0f / thr) * logf((0.5f * e0 + 2.0f) / 2.6f);
        if (t < 0) t = 0;
        s_sc_eta = t + 1.0f;          /* spool-up allowance */
    } else {
        s_sc_eta = 0;
    }
    s_sc_pos_mm = v3_add(s_sc_pos_mm,
                         v3_scale(p->basis.r[2], s_sc_speed * dt));

    /* Arrival / manual drop. */
    bool arrived = s_sc_has_dest && dist < standoff + SC_DROP_MM;
    if (arrived || in.secondary) {
        Vec3 drop_mm = s_sc_pos_mm;
        const Poi *poi = NULL;
        if (arrived) {
            /* Drop at the standoff point on our approach line. */
            Vec3 in_dir = v3_norm(v3_sub(s_sc_dest.pos_mm, s_sc_pos_mm));
            drop_mm = v3_sub(s_sc_dest.pos_mm, v3_scale(in_dir, standoff));
            poi = &s_sc_dest;
        }
        drop_anchor(drop_mm, poi);
        Ship *pl = &g_ships[PLAYER];
        if (arrived) {
            /* Place the ship short of the POI, nose on it (closer for
             * man-made structures so they fill some screen). */
            float back = (s_sc_dest.kind == POI_PLANET) ? 900.0f : 350.0f;
            Vec3 in_dir = v3_norm(v3_sub(s_sc_dest.pos_mm, s_sc_pos_mm));
            pl->pos = v3_scale(in_dir, -back);
        } else {
            pl->pos = v3(0, 0, 0);
        }
        pl->vel = v3_scale(pl->basis.r[2], 60.0f);
        pl->throttle = 0.5f;
        s_state = ST_FLIGHT;
        spawn_poi_content();
    }
}

static void tick_hyperjump(float dt) {
    s_hyper_t += dt;
    if (s_hyper_t >= HYPER_TIME) {
        g_player.fuel -= s_jump_dist;
        if (g_player.fuel < 0) g_player.fuel = 0;
        arrive_in_system(s_jump_target);
        r3d_starfield_init((uint32_t)(system_info()->seed >> 16));
        g_player.xp_piloting += 2;
        elite_input_reset();
        s_state = ST_FLIGHT;
    }
}

void elite_game_tick(const CraftRawButtons *btn, float dt) {
    bool menu_edge = btn->menu && !s_prev_menu;
    s_prev_menu = btn->menu;
    bool a_edge = btn->a && !s_prev_a;
    s_prev_a = btn->a;

    switch (s_state) {
    case ST_TITLE: {
        bool has_save = save_exists();
        static bool tu, td;
        if (btn->up && !tu && s_title_cursor > 0) { s_title_cursor--; sfx_ui_move(); }
        if (btn->down && !td && s_title_cursor < 1) { s_title_cursor++; sfx_ui_move(); }
        tu = btn->up; td = btn->down;
        if (a_edge) {
            sfx_ui_select();
            if (s_title_cursor == 0 && has_save) {
                SaveMeta meta;
                if (save_load(&meta)) {
                    combat_set_kills(meta.kills);
                    arrive_docked(&meta);
                    break;
                }
            }
            start_new_game(s_boot_seed);
        }
        break;
    }

    case ST_FLIGHT:
        if (menu_edge) { s_state = ST_PAUSE; s_pause_cursor = 0; break; }
        tick_flight(btn, dt);
        break;

    case ST_SUPERCRUISE:
        if (menu_edge) { s_state = ST_PAUSE; s_pause_cursor = 0; break; }
        tick_supercruise(btn, dt);
        break;

    case ST_HYPERJUMP:
        tick_hyperjump(dt);
        break;

    case ST_DOCKING: {
        /* Docking computer: glide to the bay mouth, then services. */
        s_dock_t += dt;
        Ship *p = &g_ships[PLAYER];
        Vec3 bay = v3(0, 0, 0);
        float k = dt * 1.4f;
        if (k > 1) k = 1;
        p->pos = v3_lerp(p->pos, bay, k);
        p->vel = v3(0, 0, 0);
        /* Ease the nose onto the station. */
        Vec3 want = v3_norm(v3_sub(bay, v3_len2(p->pos) > 1 ? p->pos
                                                            : v3(0, 0, -1)));
        p->basis.r[2] = v3_norm(v3_lerp(p->basis.r[2], want, k));
        m3_orthonormalize(&p->basis);
        fx_tick(dt);
        if (s_dock_t >= 2.2f) {
            g_player.xp_piloting += 1;
            sfx_dock();
            plat_rumble(0.4f, 0.12f);
            station_open(s_anchor_poi.index);
            mission_on_docked(system_info(), s_anchor_poi.index);
            int paid = mission_collect(system_info(), s_anchor_poi.index);
            if (paid > 0) {
                char buf[24];
                snprintf(buf, sizeof buf, "MISSION PAY %dCR", paid);
                station_toast(buf);
            }
            save_write(s_addr, s_anchor_poi.index, combat_kills());
            s_state = ST_DOCKED;
        }
        break;
    }

    case ST_DOCKED: {
        audio_engine_set(0, 0);
        DockAction act = station_tick(btn, dt);
        if (act == DOCK_LAUNCH) {
            /* Emerge from the bay face (station +z, rotated by its spin),
             * nose out, gentle drift. */
            Ship *p = &g_ships[PLAYER];
            float yaw = s_time * 0.05f;
            Vec3 out = v3(sinf(yaw), 0, cosf(yaw));
            p->pos = v3_scale(out, 320.0f);
            p->basis.r[2] = out;
            p->basis.r[1] = v3(0, 1, 0);
            p->basis.r[0] = v3_norm(v3_cross(p->basis.r[1], out));
            p->basis.r[1] = v3_cross(p->basis.r[2], p->basis.r[0]);
            p->vel = v3_scale(out, 25.0f);
            p->throttle = 0.25f;
            p->shield = p->shield_max;     /* station services top you up */
            p->heat = 0;
            elite_input_reset();
            s_state = ST_FLIGHT;
        }
        break;
    }

    case ST_STATUS:
        if (status_tick(btn, dt)) {
            elite_input_reset();
            s_state = ST_FLIGHT;
        }
        break;

    case ST_PAUSE: {
        static const int N_ITEMS = 4;
        bool up = btn->up, down = btn->down;
        static bool pu, pd;
        if (up && !pu && s_pause_cursor > 0) s_pause_cursor--;
        if (down && !pd && s_pause_cursor < N_ITEMS - 1) s_pause_cursor++;
        pu = up; pd = down;
        if (menu_edge || (a_edge && s_pause_cursor == 0)) {
            elite_input_reset();
            s_state = ST_FLIGHT;
        }
        else if (a_edge && s_pause_cursor == 1) {
            map_galaxy_open(s_addr, g_player.fuel, JUMP_RANGE);
            s_state = ST_GALAXY_MAP;
        } else if (a_edge && s_pause_cursor == 2) {
            map_system_open(cam_pos_mm());
            s_state = ST_SYSTEM_MAP;
        } else if (a_edge && s_pause_cursor == 3) {
            status_open();
            s_state = ST_STATUS;
        }
        break;
    }

    case ST_GALAXY_MAP: {
        SysAddr target;
        float dist;
        MapAction act = map_galaxy_tick(btn, dt, &target, &dist);
        if (act == MAP_CLOSE) { elite_input_reset(); s_state = ST_FLIGHT; }
        else if (act == MAP_ENGAGE_JUMP) {
            sfx_jump();
            s_jump_target = target;
            s_jump_dist = dist;
            s_hyper_t = 0;
            s_hyper_seed = xorshift32();
            ships_despawn_npcs();
            s_state = ST_HYPERJUMP;
        }
        break;
    }

    case ST_SYSTEM_MAP: {
        Poi dest;
        MapAction act = map_system_tick(btn, dt, &dest);
        if (act == MAP_CLOSE) { elite_input_reset(); s_state = ST_FLIGHT; }
        else if (act == MAP_ENGAGE_SC) {
            s_sc_dest = dest;
            s_sc_has_dest = true;
            s_sc_pos_mm = cam_pos_mm();
            s_sc_speed = 0.01f;
            ships_despawn_npcs();
            fx_init();
            /* Auto-align the nose at the destination. */
            Ship *p = &g_ships[PLAYER];
            Vec3 fwd = v3_norm(v3_sub(dest.pos_mm, s_sc_pos_mm));
            Vec3 up0 = (fabsf(fwd.y) < 0.95f) ? v3(0, 1, 0) : v3(1, 0, 0);
            p->basis.r[2] = fwd;
            p->basis.r[0] = v3_norm(v3_cross(up0, fwd));
            p->basis.r[1] = v3_cross(fwd, p->basis.r[0]);
            p->throttle = 1.0f;     /* full burn by default */
            p->pos = v3(0, 0, 0);
            p->vel = v3(0, 0, 0);
            elite_input_reset();
            s_state = ST_SUPERCRUISE;
        }
        break;
    }
    }
}

/* --- rendering -----------------------------------------------------------*/
void elite_game_render_begin(void) {
    Ship *p = &g_ships[PLAYER];

    switch (s_state) {
    case ST_TITLE: {
        /* Slow drift through the stars + a hero ship. */
        Mat3 cam = m3_identity();
        m3_rotate_local(&cam, 1, s_time * 0.02f);
        r3d_scene_begin(&cam, 60.0f);
        r3d_pipe_set_sun(v3(0.35f, 0.45f, -0.82f));
        R3DObject obj;
        obj.mesh = &mesh_fighter;
        obj.basis = m3_identity();
        m3_rotate_local(&obj.basis, 1, s_time * 0.3f);
        m3_rotate_local(&obj.basis, 0, 0.25f);
        obj.pos = m3_mul_v3(&cam, v3(8.0f, -4.0f, 26.0f));
        r3d_scene_add_object(&obj);
        break;
    }

    case ST_HYPERJUMP: {
        /* Witchspace: empty scene, tumbling slowly — streaks drawn in
         * the overlay. */
        Mat3 cam = p->basis;
        m3_rotate_local(&cam, 2, s_hyper_t * 0.4f);
        r3d_scene_begin(&cam, 60.0f);
        break;
    }
    case ST_GALAXY_MAP:
    case ST_SYSTEM_MAP:
        /* Fullscreen UI: minimal empty scene (UI fills the band). */
        r3d_scene_begin(&p->basis, 60.0f);
        break;

    case ST_DOCKED: {
        /* Starfield backdrop + rotating preview (station or shipyard
         * hull) in the right-hand pane the UI leaves open. */
        Mat3 cam = m3_identity();
        r3d_scene_begin(&cam, 60.0f);
        r3d_pipe_set_sun(v3(0.35f, 0.45f, -0.82f));   /* showroom light */
        int pv = station_preview();
        if (pv != -2) {
            const Mesh *m = (pv == -1)
                ? (s_station_mesh ? s_station_mesh : &mesh_station)
                : k_hulls[pv].mesh;
            R3DObject obj;
            obj.mesh = m;
            obj.basis = m3_identity();
            m3_rotate_local(&obj.basis, 1, s_time * 0.5f);
            m3_rotate_local(&obj.basis, 0, 0.30f);
            float dist = m->bound_r * 2.5f;
            obj.pos = v3(dist * 0.29f, 0, dist);
            r3d_scene_add_object(&obj);
        }
        break;
    }

    case ST_STATUS: {
        /* Your ship turning gently in the sheet's top-right window. */
        Mat3 cam = m3_identity();
        r3d_scene_begin(&cam, 60.0f);
        r3d_pipe_set_sun(v3(0.35f, 0.45f, -0.82f));   /* showroom light */
        /* Centred backdrop, pulled back so the whole hull fits. */
        R3DObject obj;
        obj.mesh = k_hulls[g_player.hull_id].mesh;
        obj.basis = m3_identity();
        m3_rotate_local(&obj.basis, 1, s_time * 0.5f);
        m3_rotate_local(&obj.basis, 0, 0.30f);
        float dist = obj.mesh->bound_r * 2.4f;
        obj.pos = v3(0, 0, dist);
        r3d_scene_add_object(&obj);
        break;
    }

    case ST_SUPERCRUISE:
        r3d_scene_begin(&p->basis, 60.0f);
        r3d_planet_emit(cam_pos_mm());
        fx_sc_dust_emit(s_sc_pos_mm,
                        v3_scale(p->basis.r[2], s_sc_speed));
        break;

    default: {   /* FLIGHT + PAUSE render the world */
        r3d_scene_begin(&p->basis, 60.0f);
        /* Sunlight from the system star (camera-relative direction). */
        {
            Vec3 cm = cam_pos_mm();
            r3d_pipe_set_sun(v3_norm(v3_scale(cm, -1.0f)));
        }
        r3d_planet_emit(cam_pos_mm());

        /* Anchored POI structure (station / beacon). */
        if (s_anchor_has_poi && s_anchor_poi.kind != POI_PLANET) {
            R3DObject obj;
            obj.mesh = (s_anchor_poi.kind == POI_STATION && s_station_mesh)
                           ? s_station_mesh : &mesh_beacon;
            obj.basis = m3_identity();
            /* Slow majestic spin. */
            m3_rotate_local(&obj.basis, 1, s_time * 0.05f);
            obj.pos = v3_sub(v3(0, 0, 0), p->pos);
            r3d_scene_add_object(&obj);
        }

        for (int i = 1; i < MAX_SHIPS; i++) {
            if (!g_ships[i].alive) continue;
            R3DObject obj;
            obj.mesh = g_ships[i].mesh;
            obj.basis = g_ships[i].basis;
            obj.pos = v3_sub(g_ships[i].pos, p->pos);
            r3d_scene_add_object(&obj);
        }
        loot_render(p->pos);
        fx_emit_all(p->pos, p->vel);
        proj_emit(p->pos);
        break;
    }
    }
}

void elite_game_render(uint16_t *fb, int y_min, int y_max) {
    r3d_scene_raster(fb, y_min, y_max);
}

/* --- overlays ------------------------------------------------------------*/
static void draw_hyperjump_overlay(uint16_t *fb) {
    /* Radial witchspace streaks. */
    float t = s_hyper_t;
    for (int i = 0; i < 28; i++) {
        uint32_t h = s_hyper_seed ^ (uint32_t)(i * 2654435761u);
        h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
        float ang = (float)(h & 0xFF) * (6.2831853f / 256.0f);
        float phase = (float)((h >> 8) & 0xFF) * (1.0f / 256.0f);
        float r0 = fmodf(t * (30.0f + (float)((h >> 16) & 31)) + phase * 60.0f,
                         60.0f);
        float r1 = r0 + 6.0f + r0 * 0.5f;
        float ca = cosf(ang), sa = sinf(ang);
        int x0 = 64 + (int)(ca * r0), y0 = 64 + (int)(sa * r0);
        int x1 = 64 + (int)(ca * r1), y1 = 64 + (int)(sa * r1);
        uint16_t c = (r0 > 40.0f) ? RGB565C(200, 215, 255)
                   : (r0 > 18.0f) ? RGB565C(120, 140, 220)
                                  : RGB565C(60, 70, 140);
        /* simple line */
        int steps = 12;
        for (int s = 0; s <= steps; s++) {
            int x = x0 + (x1 - x0) * s / steps;
            int y = y0 + (y1 - y0) * s / steps;
            if ((unsigned)x < ELITE_FB_W && (unsigned)y < ELITE_FB_H)
                fb[y * ELITE_FB_W + x] = c;
        }
    }
    char name[14];
    galaxy_system_name(s_jump_target, name);
    char buf[28];
    snprintf(buf, sizeof buf, "JUMPING: %s", name);
    craft_font_draw(fb, buf, 30, 100, RGB565C(150, 170, 255));
}

static void draw_pause_overlay(uint16_t *fb) {
    /* Dim panel. */
    for (int y = 38; y < 100; y++)
        for (int x = 28; x < 100; x++)
            fb[y * ELITE_FB_W + x] = RGB565C(10, 14, 24);
    for (int x = 28; x < 100; x++) {
        fb[38 * ELITE_FB_W + x] = RGB565C(95, 110, 140);
        fb[99 * ELITE_FB_W + x] = RGB565C(95, 110, 140);
    }
    craft_font_draw(fb, "PAUSED", 52, 43, RGB565C(200, 210, 225));
    static const char *items[4] = { "RESUME", "GALAXY CHART", "SYSTEM MAP",
                                    "SHIP STATUS" };
    for (int i = 0; i < 4; i++) {
        uint16_t c = (i == s_pause_cursor) ? RGB565C(120, 255, 120)
                                           : RGB565C(120, 126, 145);
        if (i == s_pause_cursor) craft_font_draw(fb, ">", 34, 56 + i * 9, c);
        craft_font_draw(fb, items[i], 41, 56 + i * 9, c);
    }
}

void elite_game_draw_overlay(uint16_t *fb) {
    /* s_time advances here (called once per frame, post-render). */
    /* (dt not available; approximate from frame ms readout) */
    s_time += 0.033f;

    switch (s_state) {
    case ST_TITLE: {
        craft_font_draw_2x(fb, "THUMBY", 40, 22, RGB565C(120, 230, 255));
        craft_font_draw_2x(fb, "ELITE", 44, 36, RGB565C(255, 200, 60));
        bool has_save = save_exists();
        const char *items[2] = { "CONTINUE", "NEW GAME" };
        for (int i = 0; i < 2; i++) {
            uint16_t c = (i == 0 && !has_save) ? RGB565C(60, 66, 84)
                       : (i == s_title_cursor) ? RGB565C(120, 255, 120)
                                               : RGB565C(120, 126, 145);
            if (i == s_title_cursor)
                craft_font_draw(fb, ">", 44, 78 + i * 10, RGB565C(120, 255, 120));
            craft_font_draw(fb, items[i], 52, 78 + i * 10, c);
        }
        craft_font_draw(fb, "AN INFINITE GALAXY AWAITS", 14, 116,
                        RGB565C(70, 90, 115));
        return;
    }

    case ST_GALAXY_MAP: map_galaxy_draw(fb); return;
    case ST_SYSTEM_MAP: map_system_draw(fb); return;
    case ST_HYPERJUMP:  draw_hyperjump_overlay(fb); return;
    case ST_DOCKED:     station_draw(fb); return;
    case ST_STATUS:     status_draw(fb); return;
    default: break;
    }

    Ship *p = &g_ships[PLAYER];
    if (s_state == ST_SUPERCRUISE) {
        HudScInfo info = {
            .dest_name = s_sc_has_dest ? s_sc_dest.name : NULL,
            .dest_rel_mm = s_sc_has_dest
                ? v3_sub(s_sc_dest.pos_mm, s_sc_pos_mm) : v3(0, 0, 1),
            .speed_mms = s_sc_speed,
            .eta_s = s_sc_eta,
            .throttle = p->throttle,
            .fuel01 = g_player.fuel / g_player.fuel_max,
            .render_ms = s_frame_ms,
            .show_perf = 1,
        };
        ui_hud_draw_sc(fb, &info);
        return;
    }

    if (p->alive) {
        HudInfo info = {
            .target = s_target,
            .kills = combat_kills(),
            .fuel01 = g_player.fuel / g_player.fuel_max,
            .render_ms = s_frame_ms,
            .show_perf = 1,
        };
        ui_hud_draw(fb, &info);
        if (s_scoop_toast_t > 0)
            craft_font_draw(fb, s_scoop_toast,
                            64 - craft_font_width(s_scoop_toast) / 2, 38,
                            RGB565C(255, 200, 60));
        if (s_state == ST_DOCKING)
            craft_font_draw(fb, "DOCKING...", 44, 30, RGB565C(120, 230, 255));
        else if (can_dock())
            craft_font_draw(fb, "LB+RB: DOCK", 42, 30, RGB565C(120, 230, 255));
    } else {
        craft_font_draw_2x(fb, "SHIP LOST", 28, 52, RGB565C(255, 80, 60));
        craft_font_draw(fb, "RETURNING TO BEACON", 26, 70,
                        RGB565C(170, 170, 180));
    }

    if (s_state == ST_PAUSE) draw_pause_overlay(fb);
}
