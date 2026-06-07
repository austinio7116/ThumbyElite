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
#include "elite_rocks.h"
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
#include "econ.h"
#include "galaxy_gen.h"
#include "craft_font.h"
#include "meshes_gen.h"
#include <stdio.h>
#include <string.h>

typedef enum {
    ST_FLIGHT = 0, ST_SUPERCRUISE, ST_HYPERJUMP,
    ST_GALAXY_MAP, ST_SYSTEM_MAP, ST_PAUSE,
    ST_DOCKING, ST_DOCKED, ST_STATUS, ST_TITLE,
    ST_DASH = 12,   /* appended LAST — inserting mid-enum shifted
                       DOCKED & friends and broke every state check */
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
static Vec3    s_hyper_from_mm;   /* departure point: system recedes */

static int   s_target = -1;      /* combat lock */
static int   s_loot_target = -1; /* canister lock (no hostiles about) */
static int   s_rock_target = -1; /* prospector lock (belt finding aid) */
static uint32_t s_entry_salt;    /* per-system-entry, salts transient
                                    events (distress) so revisits differ */
static int   s_distress_civ = -1; /* live distress event: the victim */
static bool  s_distress_paid;
static int   s_tgt_class = 0;    /* 0 AUTO, 1 SALVAGE, 2 ROCKS — LB
                                    double-tap demotes the class so you
                                    can mine through floating salvage or
                                    loot mid-fight; single-tap still
                                    cycles WITHIN the class only */
static bool  s_station_lock;     /* station nav lock (nothing else) */
static float s_rail_charge01;    /* railgun charge for the HUD arc */
static bool  s_incoming;         /* seeker tracking the player */
static bool  s_in_settings;      /* SETTINGS submenu over the pause */
static int   s_dash_sel;         /* dashboard region 0..3 */
static float s_dash_anim;        /* 0 closed .. 1 fully risen */
static bool  s_dash_closing;     /* sliding back down before resume */
static uint8_t s_dash_from;      /* state to resume (flight/SC) */
static bool  s_menus_live;       /* chart/map/status keep the sim running */
static int   s_settings_cursor;
static float s_fps;              /* smoothed, for the toggle readout */
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

const char *elite_game_debug_toast(void) { return s_scoop_toast; }

/* The classic ladder: nine ranks earned with kills. */
const char *elite_rank_name(int kills) {
    static const struct { int k; const char *n; } R[9] = {
        { 0, "HARMLESS" }, { 5, "MOSTLY HARMLESS" }, { 12, "POOR" },
        { 25, "AVERAGE" }, { 50, "ABOVE AVERAGE" }, { 90, "COMPETENT" },
        { 150, "DANGEROUS" }, { 250, "DEADLY" }, { 400, "ELITE" },
    };
    const char *n = R[0].n;
    for (int i = 0; i < 9; i++)
        if (kills >= R[i].k) n = R[i].n;
    return n;
}

/* Wall-clock-ish for ambient animation (sum of frame steps). */
static float s_time;
float elite_game_time(void) { return s_time; }

int elite_game_state(void) { return (int)s_state; }

static void drop_anchor(Vec3 pos_mm, const Poi *poi);
static void spawn_poi_content(void);
static void arrive_in_system(SysAddr addr);

/* Debug: hard-jump to a system (harness only). */
void elite_game_debug_jump(SysAddr addr) {
    arrive_in_system(addr);
}

int elite_game_debug_target(void) { return s_target; }

/* Debug: jump the anchor straight to POI n (harness only). */
void elite_game_debug_goto_poi(int n) {
    Poi pois[MAX_POIS];
    int np = system_pois(pois, MAX_POIS);
    if (n < 0 || n >= np) return;
    drop_anchor(pois[n].pos_mm, &pois[n]);
    g_ships[PLAYER].pos = v3(0, 0, -700.0f);
    spawn_poi_content();
}

void elite_game_crit_toast(const char *msg, bool mine) {
    snprintf(s_scoop_toast, sizeof s_scoop_toast, "%s", msg);
    s_scoop_toast_t = mine ? 2.6f : 1.8f;
    if (mine) sfx_lock_warn();
}

/* Debug: frame planet POI n from 2.6 radii out, sunward side, facing
 * it (planet-variety sheets). */
void elite_game_debug_view_planet(int n) {
    Poi pois[MAX_POIS];
    int np = system_pois(pois, MAX_POIS);
    if (n < 0 || n >= np || pois[n].kind != POI_PLANET) return;
    const SystemInfo *si = system_info();
    float r = si->planets[pois[n].index].radius_mm;
    Vec3 ppos = pois[n].pos_mm;
    float pd = v3_len(ppos);
    if (pd < 1.0f) return;
    float k = 1.0f - (r * 2.6f) / pd;     /* sunward of the planet */
    Vec3 anchor = v3_scale(ppos, k);
    drop_anchor(anchor, &pois[n]);
    g_ships[PLAYER].pos = v3(0, 0, 0);
    elite_game_debug_face_away_from_sun();   /* away from sun = at it */
    s_scoop_toast_t = 0;                     /* clean frame */
}

/* Crossfire forgiveness: while the distress wing is still shooting at
 * the victim, stray player hits on it don't flip it or flag you — it
 * knows who the real enemy is. Normal crime rules resume once the
 * wing is dead. */
bool elite_game_distress_protected(int idx) {
    return idx == s_distress_civ && s_distress_civ > 0 &&
           !s_distress_paid && ships_alive_hostile() > 0;
}

/* The player damaged a hostile: any distress wing drops the civilian
 * and turns on the player (user spec: they fight us once engaged). */
void elite_game_player_engaged(void) {
    if (s_distress_civ < 0) return;
    for (int i = 1; i < MAX_SHIPS; i++)
        if (g_ships[i].alive && g_ships[i].team == TEAM_HOSTILE)
            g_ships[i].ai_target = 0;
}

/* Debug (host harness): face the player directly away from the star
 * so staged screenshots aren't photobombed by the sun. */
void elite_game_debug_face_away_from_sun(void) {
    Ship *p = &g_ships[PLAYER];
    Vec3 cm = v3_add(s_anchor_mm, v3_scale(p->pos, 1.0e-6f));
    Vec3 fwd = v3_norm(cm);                  /* away from origin/star */
    Vec3 up = (fwd.y > -0.9f && fwd.y < 0.9f) ? v3(0, 1, 0) : v3(1, 0, 0);
    Vec3 right = v3_norm(v3_cross(up, fwd));
    p->basis.r[0] = right;
    p->basis.r[1] = v3_cross(fwd, right);
    p->basis.r[2] = fwd;
}

/* Debug/demo: force-spawn hostiles around the player (host harness). */
void elite_game_debug_spawn(int n) {
    extern const Mesh mesh_viper, mesh_fighter;
    for (int i = 0; i < n; i++) {
        float a = frand(0, 6.2831f);
        float r = frand(500, 800);
        Vec3 pos = v3(cosf(a) * r, frand(-150, 150), sinf(a) * r);
        uint32_t mseed = 0xDEB06u ^ (uint32_t)((i & 1) * 77u);
        int cls = 2 + (i % 3);
        int idx = ship_spawn(hull_mesh(mseed, cls), pos, TEAM_HOSTILE);
        if (idx > 0) ship_set_tier(idx, i % 5, cls);
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
/* Per-POI intel — the system map's scan strip reads this, and
 * spawn_poi_content uses the SAME belt hash, so what the map promises
 * is what arrival delivers. Belts are permanent geography (no visit
 * salt); pirates/salvage are odds, not facts. */
void elite_game_poi_intel(const Poi *poi, PoiIntel *out) {
    const SystemInfo *si = system_info();
    /* Persistent belt: deterministic per system+POI. */
    int mining_sys = 0;
    for (int st2 = 0; st2 < si->n_stations; st2++)
        if (si->stations[st2].econ == ECON_EXTRACT ||
            si->stations[st2].econ == ECON_REFINE)
            mining_sys = 1;
    uint32_t h = (uint32_t)(si->seed >> 16) ^
                 (uint32_t)(poi->kind * 73u) ^
                 (uint32_t)(poi->index * 0x9E37u);
    h *= 2654435761u; h ^= h >> 13;
    int belt_pct = (poi->kind == POI_STATION) ? 10 : 25;
    if (mining_sys) belt_pct += 35;
    out->belt = (h % 100u) < (uint32_t)belt_pct;
    out->belt_seed = h;
    out->belt_rocks = mining_sys ? 5 + (int)((h >> 8) % 4u)
                                 : 3 + (int)((h >> 8) % 3u);
    /* Police: deterministic from government. */
    out->police = (poi->kind == POI_STATION && si->gov >= GOV_CONFED);
    /* Pirate odds: the live arrival formula, contraband included. */
    int illegal = 0;
    for (int g2 = 0; g2 < N_GOODS; g2++)
        if (k_goods[g2].flags & GOOD_ILLEGAL) illegal += g_player.cargo[g2];
    int chance = 0;
    if (si->threat >= 1) {
        chance = (poi->kind == POI_STATION) ? 25 : 55;
        if (illegal > 0) {
            chance += 15 + (illegal > 10 ? 20 : illegal * 2);
            if (chance > 92) chance = 92;
        }
    }
    out->pirate_pct = (uint8_t)chance;
    /* Salvage odds: the debris formula. */
    int dch = ((poi->kind == POI_STATION) ? 12 : 30) + (int)si->threat * 8;
    out->debris_pct = (uint8_t)(dch > 99 ? 99 : dch);
    /* Distress calls: transient (salted per system entry), planets and
     * beacons in dangerous space. Same map<->arrival contract as
     * belts: what the list shows is what you find. */
    uint32_t dh = h ^ (s_entry_salt * 0x9E3779B9u) ^ 0xD157u;
    dh *= 2654435761u; dh ^= dh >> 15;
    out->distress = (poi->kind != POI_STATION) && si->threat >= 1 &&
                    (dh % 100u) < 22;
}

static void spawn_poi_content(void) {
    const SystemInfo *si = system_info();
    /* Contraband heat (user req): illegal cargo draws ambushes — every
     * unit of narcotics/weapons/slaves/contraband raises the odds, and
     * a serious load brings a bigger wing. */
    int illegal = 0;
    for (int g2 = 0; g2 < N_GOODS; g2++)
        if (k_goods[g2].flags & GOOD_ILLEGAL) illegal += g_player.cargo[g2];
    int pirates = 0;
    if (si->threat >= 1 && s_anchor_has_poi) {
        /* Beacons and planets attract trouble; stations are patrolled. */
        int roll = (int)(xorshift32() % 100u);
        int chance = (s_anchor_poi.kind == POI_STATION) ? 25 : 55;
        if (illegal > 0) {
            chance += 15 + (illegal > 10 ? 20 : illegal * 2);
            if (chance > 92) chance = 92;
        }
        if (roll < chance) {
            pirates = 1 + (int)(xorshift32() % si->threat);
            if (illegal >= 5) pirates++;
        }
        if (pirates > 4) pirates = 4;
    }
    for (int i = 0; i < pirates; i++) {
        float a = frand(0, 6.2831f);
        float r = frand(600, 1000);
        Vec3 pos = v3(cosf(a) * r, frand(-200, 200), sinf(a) * r);
        int tier = (int)si->threat - 1 + (int)(xorshift32() % 3u) - 1;
        if (tier < 0) tier = 0;
        /* Local pirate styling: this system's wings share two looks. */
        static const uint8_t k_tier_class[5] = { 1, 2, 3, 4, 5 };
        int cls = k_tier_class[tier > 4 ? 4 : tier];
        uint32_t mseed = (uint32_t)(si->seed >> 24) ^
                         (uint32_t)(cls * 0x9E3779B9u) ^
                         (uint32_t)(i % 3);   /* 3 looks per wing */
        int idx = ship_spawn(hull_mesh(mseed, cls), pos, TEAM_HOSTILE);
        if (idx > 0) ship_set_tier(idx, tier, cls);
    }

    /* Police patrol lawful station space: a Viper pair that minds its
     * own business — unless you're flagged, or you shoot first. */
    if (s_anchor_has_poi && s_anchor_poi.kind == POI_STATION &&
        si->gov >= GOV_CONFED) {
        int n_pol = 1 + (si->gov >= GOV_DEMOCRACY ? 1 : 0);
        for (int k = 0; k < n_pol; k++) {
            float a = frand(0, 6.2831f);
            Vec3 pos = v3(cosf(a) * 420.0f, frand(-80, 80),
                          sinf(a) * 420.0f);
            uint32_t pseed = galaxy_get_seed() ^ 0x70110CEu;  /* one livery */
            int idx = ship_spawn(hull_mesh(pseed, 3), pos, TEAM_NEUTRAL);
            if (idx > 0) {
                ship_set_tier(idx, 3, 3);
                g_ships[idx].is_police = 1;
                g_ships[idx].team = TEAM_NEUTRAL;
            }
        }
    }

    /* Distress call: a civilian under pirate attack — the wing fights
     * THEM until the player engages. Rescue pays credits and rep. */
    s_distress_civ = -1;
    s_distress_paid = false;
    if (s_anchor_has_poi) {
        PoiIntel di;
        elite_game_poi_intel(&s_anchor_poi, &di);
        if (di.distress) {
            uint32_t cseed = (uint32_t)(si->seed >> 20) ^ 0xD15Cu;
            int cls = (xorshift32() & 1) ? 7 : 6;
            Vec3 cpos = v3(frand(-80, 80), frand(-40, 40), 420.0f);
            int civ = ship_spawn(hull_mesh(cseed, cls), cpos,
                                 TEAM_NEUTRAL);
            if (civ > 0) {
                ship_set_tier(civ, 1, cls);
                Ship *cv = &g_ships[civ];
                cv->is_civilian = 1;
                cv->civ_kind = (uint8_t)(cls == 6 ? 0 : 1);
                cv->team = TEAM_NEUTRAL;
                cv->turret_type = 0;
                cv->hull = cv->hull_max * 0.6f;   /* already hurting */
                cv->shield = cv->shield_max * 0.3f;
                s_distress_civ = civ;
                int npir = 1 + (int)si->threat / 2;
                if (npir > 3) npir = 3;
                int first_pir = -1;
                for (int k = 0; k < npir; k++) {
                    float a2 = frand(0, 6.2831f);
                    Vec3 pp = v3_add(cpos, v3(cosf(a2) * 160.0f,
                                              frand(-60, 60),
                                              sinf(a2) * 160.0f));
                    int tier = (int)si->threat - 1;
                    if (tier < 0) tier = 0;
                    int pcls = 1 + tier;
                    uint32_t ms = (uint32_t)(si->seed >> 24) ^
                                  (uint32_t)(pcls * 0x9E3779B9u) ^ k;
                    int idx = ship_spawn(hull_mesh(ms, pcls), pp,
                                         TEAM_HOSTILE);
                    if (idx > 0) {
                        ship_set_tier(idx, tier, pcls);
                        g_ships[idx].ai_target = (uint8_t)civ;
                        if (first_pir < 0) first_pir = idx;
                    }
                }
                if (first_pir > 0)
                    cv->ai_target = (uint8_t)first_pir;  /* fights back */
                snprintf(s_scoop_toast, sizeof s_scoop_toast,
                         "DISTRESS CALL!");
                s_scoop_toast_t = 2.5f;
            }
        }
    }

    /* Civilian traffic: a miner works any belt; cargo ships run lanes
     * near stations and sometimes beacons. Green on the scanner. */
    {
        PoiIntel ci;
        if (s_anchor_has_poi) {
            elite_game_poi_intel(&s_anchor_poi, &ci);
            int want_miner = ci.belt && (int)(xorshift32() % 100u) < 65;
            int want_cargo =
                (s_anchor_poi.kind == POI_STATION &&
                 (int)(xorshift32() % 100u) < 70) ||
                (s_anchor_poi.kind != POI_STATION &&
                 (int)(xorshift32() % 100u) < 25);
            for (int k = 0; k < want_miner + want_cargo; k++) {
                int kind = (k == 0 && want_miner) ? 0 : 1;
                float a = frand(0, 6.2831f);
                float r = kind ? frand(350, 650) : frand(450, 800);
                Vec3 pos = v3(cosf(a) * r, frand(-120, 120),
                              sinf(a) * r);
                uint32_t cseed = (uint32_t)(si->seed >> 20) ^
                                 (uint32_t)(0xC1B1u + k * 77u);
                int cls = kind ? 7 : 6;          /* MULE / PACK MULE */
                int idx = ship_spawn(hull_mesh(cseed, cls), pos,
                                     TEAM_NEUTRAL);
                if (idx > 0) {
                    ship_set_tier(idx, 1, cls);
                    g_ships[idx].is_civilian = 1;
                    g_ships[idx].civ_kind = (uint8_t)kind;
                    g_ships[idx].team = TEAM_NEUTRAL;
                    g_ships[idx].turret_type = 0;
                }
            }
        }
    }

    /* Asteroid fields: PERSISTENT geography — the same belt hash the
     * system map's scan strip reports (option-C design). A belt is
     * always at its POI, with a familiar field shape per visit. */
    if (s_anchor_has_poi) {
        PoiIntel intel;
        elite_game_poi_intel(&s_anchor_poi, &intel);
        if (intel.belt)
            rocks_spawn_field(intel.belt_seed, intel.belt_rocks);
    }

    /* Derelict debris (user req): some sites have loot just floating —
     * old wrecks, jettisoned cargo. More in lawless space; beacons and
     * planets are picked-over less often than patrolled stations. */
    {
        int chance = (s_anchor_poi.kind == POI_STATION) ? 12 : 30;
        chance += (int)si->threat * 8;
        if (s_anchor_has_poi && (int)(xorshift32() % 100u) < chance) {
            int n = 1 + (int)(xorshift32() % 3u);
            for (int i = 0; i < n; i++) {
                float a = frand(0, 6.2831f);
                float r = frand(250, 700);
                Vec3 pos = v3(cosf(a) * r, frand(-150, 150),
                              sinf(a) * r);
                loot_on_kill(pos, v3(frand(-2, 2), frand(-2, 2),
                                     frand(-2, 2)),
                             (int)si->threat);
            }
        }
    }

    /* Bounty mark: a flagged pilot at the mission's tier. ACE marks
     * bring an escort. */
    int btier = (s_anchor_has_poi && s_anchor_poi.kind == POI_BEACON)
                    ? mission_bounty_tier_here(s_addr) : -1;
    if (btier > 0) {
        float a = frand(0, 6.2831f);
        Vec3 pos = v3(cosf(a) * 800.0f, frand(-150, 150), sinf(a) * 800.0f);
        uint32_t mseed = (uint32_t)(si->seed >> 20) ^ 0xB011B011u;
        int cls = 2 + btier;          /* bigger marks at higher tier */
        int idx = ship_spawn(hull_mesh(mseed, cls), pos, TEAM_HOSTILE);
        if (idx > 0) {
            ship_set_tier(idx, btier, cls);
            g_ships[idx].is_mark = 1;
            snprintf(s_scoop_toast, sizeof s_scoop_toast,
                     "BOUNTY MARK DETECTED");
            s_scoop_toast_t = 3.0f;
        }
        if (btier >= 4) {
            int e2 = ship_spawn(hull_mesh(mseed ^ 0x55u, 3),
                                v3_add(pos, v3(120, 30, 60)), TEAM_HOSTILE);
            if (e2 > 0) ship_set_tier(e2, 2, 3);
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
    hull_cache_reset(g_ships[PLAYER].mesh);
    fx_init();
    loot_init();
    rocks_init();
    s_target = -1;
    s_tgt_class = 0;                 /* fresh site, AUTO priorities */
}

static void arrive_in_system(SysAddr addr) {
    s_addr = addr;
    s_entry_salt++;
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
    s_loot_target = -1;
    if (s_tgt_class >= 1) {
        /* Forced class: skip the hostile scan entirely. */
        s_target = -1;
        s_station_lock = false;
        s_rock_target = -1;
        if (s_tgt_class == 1) {
            s_loot_target = loot_nearest(pp, NULL);
        } else {
            Vec3 rk[8];
            int nr = rocks_positions(rk, 8);
            float bd2 = 1e30f;
            for (int i = 0; i < nr; i++) {
                float d2 = v3_len(v3_sub(rk[i], pp));
                if (d2 < bd2) { bd2 = d2; s_rock_target = i; }
            }
        }
        return;
    }
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
    /* Nothing hostile: NEUTRAL ships first (find the rescued civilian
     * — user report: the green ship was untargetable), then salvage,
     * rocks, station compass. */
    s_station_lock = false;
    s_rock_target = -1;
    if (s_target < 0) {
        int nbest = -1, nfirst = -1;
        float nbest_d = 1e30f, nfirst_d = 1e30f;
        for (int i = 1; i < MAX_SHIPS; i++) {
            if (!g_ships[i].alive || g_ships[i].team == TEAM_HOSTILE)
                continue;
            if (!g_ships[i].is_civilian && !g_ships[i].is_police)
                continue;
            float d = v3_len(v3_sub(g_ships[i].pos, pp));
            if (d < nfirst_d) { nfirst_d = d; nfirst = i; }
            if (cur_d >= 0.0f && d > cur_d && d < nbest_d) {
                nbest_d = d; nbest = i;
            }
        }
        s_target = (nbest >= 0) ? nbest : nfirst;
    }
    if (s_target < 0) {
        s_loot_target = loot_nearest(pp, NULL);
        if (s_loot_target < 0) {
            /* Prospector lock: nearest belt rock (user req: BELT! on
             * the map needs a way to FIND the rocks). */
            Vec3 rk[8];
            int nr = rocks_positions(rk, 8);
            float bd2 = 1e30f;
            for (int i = 0; i < nr; i++) {
                float d2 = v3_len(v3_sub(rk[i], pp));
                if (d2 < bd2) { bd2 = d2; s_rock_target = i; }
            }
        }
        if (s_loot_target < 0 && s_rock_target < 0 &&
            s_anchor_has_poi && s_anchor_poi.kind == POI_STATION)
            s_station_lock = true;
    }
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
    s_rng = seed * 2654435761u;
    s_rng ^= s_rng >> 15;
    if (!s_rng) s_rng = 1;
    loot_seed(seed ^ 0x100Du);
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
    player_init();
    /* Every commander starts at the bottom (user spec): a random cheap
     * hull, a battered low-grade loadout (always at least one weapon,
     * launchers very rare) and 1,000 credits. Earn the rest. */
    {
        uint32_t h = seed * 2654435761u;
        h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
        /* Hull: SKIFF 50% / DART 25% / PACK MULE 15% / SPARROW 10%. */
        int r = (int)(h % 100u);
        g_player.hull_id = (r < 50) ? 0 : (r < 75) ? 1 : (r < 90) ? 6 : 2;
        h ^= h << 13; h ^= h >> 17; h ^= h << 5;
        g_player.hull_seed = h;
        const HullDef *hd = &k_hulls[g_player.hull_id];
        for (int i = 0; i < hd->n_slots; i++) {
            h ^= h << 13; h ^= h >> 17; h ^= h << 5;
            if (i > 0 && (h & 1)) {            /* extra slots often empty */
                g_player.mounts[i].in_use = 0;
                continue;
            }
            h ^= h << 13; h ^= h >> 17; h ^= h << 5;
            int wr = (int)(h % 100u);
            WeaponType w = (wr < 45) ? WPN_PULSE_S
                         : (wr < 70) ? WPN_AUTOCANNON
                         : (wr < 80) ? WPN_PULSE_M
                         : (wr < 88) ? WPN_BEAM
                         : (wr < 93) ? WPN_PHOTON
                         : (wr < 97) ? WPN_GAUSS
                         : (wr < 99) ? WPN_MISSILE : WPN_HOMING;
            if (k_weapons[w].size > hd->slot_size[i]) w = WPN_PULSE_S;
            h ^= h << 13; h ^= h >> 17; h ^= h << 5;
            int q = ((h >> 4) % 100u < 70) ? Q_SALVAGED : Q_STANDARD;
            g_player.mounts[i] = (WeaponInst){
                .type = (uint8_t)w, .quality = (uint8_t)q,
                .integrity = (uint8_t)(55 + (h % 36u)), .in_use = 1,
            };
        }
        g_player.credits = 1000;
    }
    spawn_player();    /* AFTER player state is final */
    missions_init();
    s_state = ST_FLIGHT;

    /* Find a starting system: spiral out from the origin for a system
     * that (a) has a station and (b) sits in a REACHABLE CLUSTER — at
     * least 2 neighbours within a starter ship's jump range, one of
     * them with its own station (user req: never strand a new game
     * 12+ ly from everything). Best candidate wins; barren-galaxy
     * fallbacks keep the old behaviour. */
    SysAddr start = {0, 0, 0};
    SysAddr station_fallback = {0, 0, 0};
    SysAddr fallback = {0, 0, 0};
    const float START_JUMP = 6.0f;     /* SKIFF range with margin */
    bool found = false, have_st_fb = false, have_fallback = false;
    for (int ring = 0; ring < 14 && !found; ring++)
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
                    if (probe.n_stations == 0) continue;
                    if (!have_st_fb) { station_fallback = a; have_st_fb = true; }
                    /* Count starter-range neighbours. */
                    float px, py;
                    galaxy_star_pos(a, &px, &py);
                    int near = 0, near_station = 0;
                    for (int ny = a.sy - 1; ny <= a.sy + 1; ny++)
                        for (int nx = a.sx - 1; nx <= a.sx + 1; nx++) {
                            int nn = galaxy_sector_stars(nx, ny);
                            for (int j = 0; j < nn; j++) {
                                SysAddr b = { nx, ny, (uint8_t)j };
                                if (sysaddr_eq(b, a)) continue;
                                float bx, by;
                                galaxy_star_pos(b, &bx, &by);
                                float dx = bx - px, dy = by - py;
                                if (dx * dx + dy * dy >
                                    START_JUMP * START_JUMP) continue;
                                near++;
                                if (near_station == 0) {
                                    SystemInfo nb;
                                    galaxy_generate(b, &nb);
                                    if (nb.n_stations > 0) near_station = 1;
                                }
                            }
                        }
                    if (near >= 2 && near_station) { start = a; found = true; }
                }
            }
    if (!found) start = have_st_fb ? station_fallback : fallback;
    arrive_in_system(start);
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
        /* RAILGUN charges while A is held, fires on release at full
         * charge (rising arm tones + HUD arc). Everything else fires
         * on hold as usual. */
        if (p->weapons[p->active_w] == WPN_RAILGUN) {
            static float charge;
            static int charge_step;
            if (in.fire && combat_can_fire(p)) {
                charge += dt;
                int st2 = (int)(charge / 0.2f);
                if (st2 > charge_step && st2 <= 4) {
                    charge_step = st2;
                    sfx_charge_step(st2 - 1);
                }
                s_rail_charge01 = charge / 0.8f;
                if (s_rail_charge01 > 1.0f) s_rail_charge01 = 1.0f;
            } else {
                if (charge >= 0.8f)
                    combat_fire(PLAYER, 0.0f, s_target);
                charge = 0;
                charge_step = 0;
                s_rail_charge01 = 0;
            }
        } else if (in.fire) {
            combat_fire(PLAYER, 0.0f, s_target);
        }
        if (in.secondary && p->n_weapons > 1)       /* B = next weapon */
            p->active_w = (uint8_t)((p->active_w + 1) % p->n_weapons);
        if (in.chaff && player_has_util(EQ_CHAFF) &&
            g_player.chaff_charges > 0) {
            g_player.chaff_charges--;
            int broke = proj_break_locks(PLAYER);
            fx_chaff_burst(v3_sub(p->pos, v3_scale(p->basis.r[2],
                                                   p->mesh->bound_r)),
                           p->vel);
            sfx_chaff();
            snprintf(s_scoop_toast, sizeof s_scoop_toast,
                     broke ? "CHAFF! %d LOCKS BROKEN" : "CHAFF AWAY",
                     broke);
            s_scoop_toast_t = 1.5f;
        }
        if (in.tgt_class_cycle) {
            /* LB double-tap: demote the lock class (input layer
             * classifies the double; 0.5s window). */
            s_tgt_class = (s_tgt_class + 1) % 3;
            static const char *k_tc[3] = { "TGT: AUTO", "TGT: SALVAGE",
                                           "TGT: ROCKS" };
            snprintf(s_scoop_toast, sizeof s_scoop_toast, "%s",
                     k_tc[s_tgt_class]);
            s_scoop_toast_t = 1.4f;
            cycle_target();              /* re-lock within the class */
        } else if (in.cycle_target) {
            int before = s_target;
            cycle_target();
            if (s_target >= 0 && s_target != before) sfx_lock_acquire();
        }
        /* Seeker tracking you: repeating alarm + HUD INCOMING flash. */
        {
            static float warn_cd;
            if (proj_homing_on(PLAYER)) {
                s_incoming = true;
                warn_cd -= dt;
                if (warn_cd <= 0.0f) {
                    sfx_lock_warn();
                    warn_cd = 0.55f;
                }
            } else {
                s_incoming = false;
                warn_cd = 0;
            }
        }
        combat_set_player_target(
            (s_target >= 0 && g_ships[s_target].alive) ? s_target : -1);
        /* Police scans: a neutral patrol inside 300m sweeps your hold —
         * carrying contraband gets you FLAGGED, and flagged pilots get
         * engaged. Shooting first does too (see combat). */
        {
            static float scan_t;
            int illegal2 = 0;
            for (int g2 = 0; g2 < N_GOODS; g2++)
                if (k_goods[g2].flags & GOOD_ILLEGAL)
                    illegal2 += g_player.cargo[g2];
            int near_police = -1;
            for (int i = 1; i < MAX_SHIPS; i++)
                if (g_ships[i].alive && g_ships[i].is_police &&
                    g_ships[i].team == TEAM_NEUTRAL &&
                    v3_len(v3_sub(g_ships[i].pos, p->pos)) < 300.0f) {
                    near_police = i;
                    break;
                }
            if (near_police >= 0 && illegal2 > 0 &&
                g_player.legal == 0) {
                scan_t += dt;
                if (scan_t > 0.8f && scan_t < 0.9f) {
                    snprintf(s_scoop_toast, sizeof s_scoop_toast,
                             "POLICE SCAN...");
                    s_scoop_toast_t = 1.6f;
                }
                if (scan_t > 2.6f) {
                    g_player.legal = 1;
                    g_player.fine += 120 + illegal2 * 30;
                    snprintf(s_scoop_toast, sizeof s_scoop_toast,
                             "FLAGGED: SMUGGLER");
                    s_scoop_toast_t = 3.0f;
                    sfx_lock_warn();
                }
            } else {
                scan_t = 0;
            }
            /* Flagged pilots get engaged on sight. */
            if (g_player.legal >= 1)
                for (int i = 1; i < MAX_SHIPS; i++)
                    if (g_ships[i].alive && g_ships[i].is_police &&
                        g_ships[i].team == TEAM_NEUTRAL)
                        g_ships[i].team = TEAM_HOSTILE;
        }
        /* Miners attract vultures: while a rock field is live, an
         * ambush clock runs — one threat-scaled pirate jump per visit,
         * announced a beat before they arrive. */
        {
            static float ambush_t;
            static bool ambushed;
            Vec3 rk[8];
            if (!ambushed && rocks_positions(rk, 8) > 0) {
                const SystemInfo *si2 = system_info();
                ambush_t += dt;
                float due = 50.0f - (float)si2->threat * 8.0f;
                if (si2->threat > 0 && ambush_t > due) {
                    ambushed = true;
                    snprintf(s_scoop_toast, sizeof s_scoop_toast,
                             "PIRATES INBOUND");
                    s_scoop_toast_t = 2.5f;
                    sfx_lock_warn();
                    int n2 = 1 + (int)(xorshift32() % 2u);
                    for (int k = 0; k < n2; k++) {
                        float a2 = frand(0, 6.2831f);
                        Vec3 pp2 = v3_add(g_ships[PLAYER].pos,
                                          v3(cosf(a2) * 900.0f,
                                             frand(-200, 200),
                                             sinf(a2) * 900.0f));
                        int tier2 = (int)si2->threat - 1 +
                                    (int)(xorshift32() % 2u);
                        if (tier2 < 0) tier2 = 0;
                        int cls2 = 1 + tier2;
                        if (cls2 > 5) cls2 = 5;
                        uint32_t ms2 = (uint32_t)(si2->seed >> 24) ^
                                       (uint32_t)(cls2 * 0x9E3779B9u) ^ k;
                        int idx2 = ship_spawn(hull_mesh(ms2, cls2), pp2,
                                              TEAM_HOSTILE);
                        if (idx2 > 0) ship_set_tier(idx2, tier2, cls2);
                    }
                }
            } else if (rocks_positions(rk, 8) == 0) {
                ambush_t = 0;     /* reset between fields */
                ambushed = false;
            }
        }
        /* REPAIR DRONE (R2 unit): given time it patches the hull, then
         * works through damaged items one by one — a critted mount can
         * come back ONLINE mid-fight. Toasts on job start + finish. */
        if (player_has_util(EQ_DRONE)) {
            static float dr_acc;
            static int dr_job = -1;       /* 0 hull, 1.. = item index */
            WeaponInst *items[10];
            const char *names[10];
            int ni = 0;
            for (int i = 0; i < HULL_SLOTS; i++) {
                static char wn[3][8];
                snprintf(wn[i], sizeof wn[i], "WPN %d", i + 1);
                items[ni] = &g_player.mounts[i]; names[ni++] = wn[i];
            }
            items[ni] = &g_player.shield_eq; names[ni++] = "SHIELD GEN";
            items[ni] = &g_player.armor_eq;  names[ni++] = "ARMOR";
            for (int u = 0; u < 4; u++) {
                items[ni] = &g_player.util_eq[u];
                names[ni++] = "GADGET";
            }
            int want = -1;
            if (p->hull < p->hull_max - 0.5f) want = 0;
            else
                for (int i = 0; i < ni; i++)
                    if (items[i]->in_use && items[i]->integrity < 100) {
                        want = 1 + i;
                        break;
                    }
            if (want != dr_job) {
                dr_job = want;
                dr_acc = 0;
                if (want == 0) {
                    snprintf(s_scoop_toast, sizeof s_scoop_toast,
                             "DRONE: REPAIRING HULL..");
                    s_scoop_toast_t = 2.0f;
                } else if (want > 0) {
                    snprintf(s_scoop_toast, sizeof s_scoop_toast,
                             "DRONE: REPAIRING %s..", names[want - 1]);
                    s_scoop_toast_t = 2.0f;
                }
            }
            if (dr_job == 0) {
                p->hull += 1.2f * dt;     /* slow: a full hull is minutes */
                if (p->hull >= p->hull_max) {
                    p->hull = p->hull_max;
                    snprintf(s_scoop_toast, sizeof s_scoop_toast,
                             "DRONE: HULL REPAIRED");
                    s_scoop_toast_t = 2.5f;
                    dr_job = -1;
                }
            } else if (dr_job > 0) {
                WeaponInst *it = items[dr_job - 1];
                dr_acc += dt;
                if (dr_acc >= 1.6f) {     /* +1 integrity / 1.6 s */
                    dr_acc -= 1.6f;
                    if (it->integrity < 100) it->integrity++;
                    if (it->integrity >= 100) {
                        snprintf(s_scoop_toast, sizeof s_scoop_toast,
                                 "DRONE: %s REPAIRED",
                                 names[dr_job - 1]);
                        s_scoop_toast_t = 2.5f;
                        player_apply_to_ship();
                        dr_job = -1;
                    } else if ((it->integrity % 25) == 0) {
                        player_apply_to_ship();   /* caps track repair */
                    }
                }
            }
        }
        /* Distress rescue: wing dead, victim alive -> hail + reward. */
        if (s_distress_civ > 0 && !s_distress_paid) {
            Ship *cv = &g_ships[s_distress_civ];
            if (!cv->alive) {
                s_distress_civ = -1;
                snprintf(s_scoop_toast, sizeof s_scoop_toast,
                         "THE VICTIM IS LOST");
                s_scoop_toast_t = 3.0f;
            } else if (ships_alive_hostile() == 0) {
                s_distress_paid = true;
                const SystemInfo *si3 = system_info();
                int pay = 250 + (int)si3->threat * 300;
                g_player.credits += pay;
                extern void mission_rep_add_public(int fac, int amt);
                mission_rep_add_public(
                    (int)system_faction(si3->addr), 2);
                snprintf(s_scoop_toast, sizeof s_scoop_toast,
                         "\"THANK YOU CMDR\" +%dCR REP+", pay);
                s_scoop_toast_t = 5.0f;
                sfx_lock_acquire();
            }
        }
        /* Rank-up fanfare. */
        {
            static const char *last_rank;
            const char *r = elite_rank_name(combat_kills());
            if (last_rank && r != last_rank) {
                snprintf(s_scoop_toast, sizeof s_scoop_toast,
                         "RANK: %s", r);
                s_scoop_toast_t = 3.0f;
                sfx_lock_acquire();
            }
            last_rank = r;
        }
    } else {
        if (!dead_latch) { dead_latch = true; respawn_t = 3.0f; }
        respawn_t -= dt;
        if (respawn_t <= 0.0f) {
            /* Insurance: revert to the last dock save (journey since is
             * lost). No save yet -> fresh hull at the local beacon. */
            SaveMeta meta;
            if (save_exists() &&
                save_matches_galaxy(galaxy_get_seed()) &&
                save_load(&meta)) {
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
    combat_crit_cooldown_tick(dt);
    fx_tick(dt);
    rocks_tick(dt);

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
        int pay = combat_take_kill_pay();
        if (pay > 0) {
            snprintf(s_scoop_toast, sizeof s_scoop_toast,
                     "BOUNTY %dCR", pay);
            s_scoop_toast_t = 1.6f;
        }
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
    /* Star proximity HEAT — for everyone, scoop or not. Builds faster
     * than dissipation inside ~6 star radii (the old 9/s build lost to
     * the 12/s passive cooling and never registered — user-caught);
     * past redline the hull itself starts to burn. Sun-skimming is a
     * real risk now, exactly like the old game. */
    {
        const SystemInfo *si = system_info();
        float d = v3_len(s_sc_pos_mm);
        float hot_r = si->star_radius_mm * 6.0f;
        if (d < hot_r) {
            float k = 1.0f - d / hot_r;               /* 0 edge..1 core */
            g_ships[PLAYER].heat += (14.0f + 38.0f * k) * dt;
            if (g_ships[PLAYER].heat > 100.0f) {
                g_ships[PLAYER].heat = 100.0f + 0.0f;
                g_ships[PLAYER].hull -= 5.0f * dt;     /* burning */
                snprintf(s_scoop_toast, sizeof s_scoop_toast,
                         "HULL BURNING!");
                s_scoop_toast_t = 0.5f;
                if (g_ships[PLAYER].hull <= 0.0f) {
                    g_ships[PLAYER].alive = false;     /* flew too close */
                    g_ships[PLAYER].hull = 0.0f;
                }
            }
        }
        /* FUELSCOOP: free fuel while inside the heat zone. */
        if (player_has_util(EQ_FUELSCOOP) &&
            d < si->star_radius_mm * 4.5f &&
            g_player.fuel < g_player.fuel_max) {
            g_player.fuel += 1.6f * dt;
            if (g_player.fuel > g_player.fuel_max)
                g_player.fuel = g_player.fuel_max;
            if (((int)(s_time * 2.0f) & 1) == 0 &&
                g_ships[PLAYER].heat < 95.0f) {
                snprintf(s_scoop_toast, sizeof s_scoop_toast,
                         "SCOOPING %d.%d LY", (int)g_player.fuel,
                         ((int)(g_player.fuel * 10)) % 10);
                s_scoop_toast_t = 0.6f;
            }
        }
    }
    {
        /* Drive drone follows cruise speed (lower band than flight). */
        float k = s_sc_speed * (1.0f / 3000.0f);
        if (k > 1.0f) k = 1.0f;
        audio_engine_set(k * 0.55f, k);
    }
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
    if (dt > 1e-4f)
        s_fps += (1.0f / dt - s_fps) * 0.08f;     /* smoothed FPS */
    bool menu_edge = btn->menu && !s_prev_menu;
    s_prev_menu = btn->menu;
    bool a_edge = btn->a && !s_prev_a;
    s_prev_a = btn->a;

    /* Engine hum only exists in flight states; everything else mutes
     * it (prevents any residual tone on title/menus). */
    if (s_state != ST_FLIGHT && s_state != ST_SUPERCRUISE &&
        !(s_state == ST_DASH && s_dash_anim < 1.0f))
        audio_engine_set(0, 0);

    switch (s_state) {
    case ST_TITLE: {
        bool has_save = save_exists();
        static bool tu, td;
        if (btn->up && !tu && s_title_cursor > 0) { s_title_cursor--; sfx_ui_move(); }
        if (btn->down && !td && s_title_cursor < 1) { s_title_cursor++; sfx_ui_move(); }
        tu = btn->up; td = btn->down;
        if (a_edge) {
            /* No chime on game start (user pref) — straight in. */
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
        if (menu_edge) {
            s_state = ST_DASH;
            s_dash_from = ST_FLIGHT;
            s_dash_sel = 0;
            s_dash_anim = 0;
            s_dash_closing = false;
            s_in_settings = false;
            break;
        }
        tick_flight(btn, dt);
        break;

    case ST_SUPERCRUISE:
        if (menu_edge) {
            s_state = ST_DASH;
            s_dash_from = ST_SUPERCRUISE;
            s_dash_sel = 0;
            s_dash_anim = 1.0f;          /* SC: no slide, straight in */
            s_dash_closing = false;
            s_in_settings = false;
            break;
        }
        tick_supercruise(btn, dt);
        if (!g_ships[PLAYER].alive) {
            /* Burned up at the star: drop to flight, whose death path
             * runs the insurance respawn. */
            fx_spawn_explosion(g_ships[PLAYER].pos, v3(0, 0, 0));
            s_state = ST_FLIGHT;
        }
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
        if (s_menus_live) {
            CraftRawButtons none3 = {0};
            if (s_dash_from == ST_SUPERCRUISE) tick_supercruise(&none3, dt);
            else tick_flight(&none3, dt);
            if (!g_ships[PLAYER].alive) { s_state = s_dash_from; break; }
        }
        if (status_tick(btn, dt)) {
            elite_input_reset();
            s_state = s_menus_live ? ST_DASH : ST_FLIGHT;
        }
        break;

    case ST_DASH: {
        /* THE GAME PLAYS ON. Neutral stick; peril remains — taking
         * hull damage kicks you back to the cockpit. */
        CraftRawButtons none2 = {0};
        if (s_dash_from == ST_SUPERCRUISE)
            tick_supercruise(&none2, dt);
        else
            tick_flight(&none2, dt);
        if (!g_ships[PLAYER].alive) {
            s_state = (uint8_t)s_dash_from;
            break;
        }
        if (s_dash_closing) {
            /* Slide back down, then hand the stick back (user req: no
             * teleporting console). MENU mid-close reopens. */
            if (menu_edge) { s_dash_closing = false; break; }
            s_dash_anim -= dt * 4.0f;
            if (s_dash_anim <= 0.0f) {
                s_dash_anim = 0;
                s_dash_closing = false;
                elite_input_reset();
                s_state = (uint8_t)s_dash_from;
            }
            break;
        }
        if (s_dash_anim < 1.0f) {
            s_dash_anim += dt * 4.0f;
            if (s_dash_anim > 1.0f) s_dash_anim = 1.0f;
        }
        if (s_in_settings) {
            static bool pu2, pd2, pb2, pl3, pr3;
            if (btn->up && !pu2 && s_settings_cursor > 0)
                s_settings_cursor--;
            if (btn->down && !pd2 && s_settings_cursor < 3)
                s_settings_cursor++;
            pu2 = btn->up; pd2 = btn->down;
            int dir = 0;
            if (btn->right && !pr3) dir = 1;
            if (btn->left && !pl3) dir = -1;
            pl3 = btn->left; pr3 = btn->right;
            if (a_edge) {
                if (s_settings_cursor == 0)
                    g_player.invert_y = !g_player.invert_y;
                else if (s_settings_cursor == 1)
                    g_player.show_fps = !g_player.show_fps;
                else
                    dir = 1;                 /* A nudges sliders up */
            }
            if (dir && s_settings_cursor == 2) {
                int v = plat_setting_get(0) + dir * 2;     /* 0..20 */
                if (v < 0) v = 0;
                if (v > 20) v = 20;
                plat_setting_set(0, v);
                sfx_ui_move();
            } else if (dir && s_settings_cursor == 3) {
                int b2 = plat_setting_get(1) + dir * 32;   /* 0..255 */
                if (b2 < 31) b2 = 31;        /* never fully dark */
                if (b2 > 255) b2 = 255;
                plat_setting_set(1, b2);
            }
            if ((btn->b && !pb2) || menu_edge) s_in_settings = false;
            pb2 = btn->b;
            break;
        }
        {
            static bool pl2, pr2, pu3, pd3, pb3;
            if (btn->left && !pl2) s_dash_sel &= ~1;
            if (btn->right && !pr2) s_dash_sel |= 1;
            if (btn->up && !pu3) s_dash_sel &= ~2;
            if (btn->down && !pd3) s_dash_sel |= 2;
            pl2 = btn->left; pr2 = btn->right;
            pu3 = btn->up; pd3 = btn->down;
            if (a_edge) {
                s_menus_live = true;
                if (s_dash_sel == 0) {
                    map_galaxy_open(s_addr, g_player.fuel,
                                    (k_hulls[g_player.hull_id].jump_range * player_roll()->jmp));
                    s_state = ST_GALAXY_MAP;
                } else if (s_dash_sel == 1) {
                    map_system_open(cam_pos_mm());
                    s_state = ST_SYSTEM_MAP;
                } else if (s_dash_sel == 2) {
                    status_open();
                    s_state = ST_STATUS;
                } else {
                    s_in_settings = true;
                    s_settings_cursor = 0;
                }
            }
            if ((btn->b && !pb3) || menu_edge)
                s_dash_closing = true;       /* animated exit */
            pb3 = btn->b;
        }
        break;
    }

    case ST_GALAXY_MAP: {
        if (s_menus_live) {
            CraftRawButtons none3 = {0};
            if (s_dash_from == ST_SUPERCRUISE) tick_supercruise(&none3, dt);
            else tick_flight(&none3, dt);
            if (!g_ships[PLAYER].alive) { s_state = s_dash_from; break; }
        }
        SysAddr target;
        float dist;
        MapAction act = map_galaxy_tick(btn, dt, &target, &dist);
        if (act == MAP_CLOSE) {
            elite_input_reset();
            s_state = s_menus_live ? ST_DASH : ST_FLIGHT;
        }
        else if (act == MAP_ENGAGE_JUMP) {
            sfx_jump();
            s_jump_target = target;
            s_jump_dist = dist;
            s_hyper_from_mm = v3_add(s_anchor_mm,
                                     v3_scale(g_ships[PLAYER].pos, 1.0e-6f));
            s_hyper_t = 0;
            s_hyper_seed = xorshift32();
            ships_despawn_npcs();
            s_menus_live = false;
            s_state = ST_HYPERJUMP;
        }
        break;
    }

    case ST_SYSTEM_MAP: {
        if (s_menus_live) {
            CraftRawButtons none3 = {0};
            if (s_dash_from == ST_SUPERCRUISE) tick_supercruise(&none3, dt);
            else tick_flight(&none3, dt);
            if (!g_ships[PLAYER].alive) { s_state = s_dash_from; break; }
        }
        Poi dest;
        MapAction act = map_system_tick(btn, dt, &dest);
        if (act == MAP_CLOSE) {
            elite_input_reset();
            s_state = s_menus_live ? ST_DASH : ST_FLIGHT;
        }
        else if (act == MAP_ENGAGE_SC) {
            s_menus_live = false;
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
            sfx_sc_engage();
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
        obj.mesh = hull_mesh(s_boot_seed ^ 0x44E5Au, 2);   /* hero of the day */
        obj.basis = m3_identity();
        m3_rotate_local(&obj.basis, 1, s_time * 0.3f);
        m3_rotate_local(&obj.basis, 0, 0.25f);
        obj.pos = m3_mul_v3(&cam, v3(8.0f, -4.0f, 26.0f));
        r3d_scene_add_object(&obj);
        break;
    }

    case ST_HYPERJUMP: {
        /* The departure system stays on screen and RECEDES (user req):
         * a virtual camera accelerates exponentially along the nose, so
         * planets and the sun shrink away as the drive spools, then the
         * starline tunnel takes over. */
        Mat3 cam = p->basis;
        m3_rotate_local(&cam, 2, s_hyper_t * 0.4f);
        r3d_scene_begin(&cam, 60.0f);
        float d_mm = 60.0f * (expf(s_hyper_t * 3.4f) - 1.0f);
        Vec3 vcam = v3_add(s_hyper_from_mm,
                           v3_scale(p->basis.r[2], d_mm));
        r3d_pipe_set_sun(v3_norm(v3_scale(vcam, -1.0f)));
        r3d_planet_emit(vcam);
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
        uint32_t pv_seed;
        int pv_cls;
        int pv = station_preview2(&pv_seed, &pv_cls);
        if (pv == 3) {
            /* Hangar bay: your ship parked over a deck grid, dimmed by
             * the status sheet into a backdrop (user req). */
            R3DObject obj;
            obj.mesh = hull_mesh(g_player.hull_seed, g_player.hull_id);
            obj.basis = m3_identity();
            m3_rotate_local(&obj.basis, 1, s_time * 0.25f);
            m3_rotate_local(&obj.basis, 0, 0.22f);
            float dist = obj.mesh->bound_r * 2.4f;
            obj.pos = v3(0, 0, dist);
            r3d_scene_add_object(&obj);
            /* Deck grid under the ship. */
            float fy = -obj.mesh->bound_r * 1.05f;
            uint16_t gc = RGB565C(50, 70, 100);
            for (int k = -2; k <= 2; k++) {
                float sx0, sy0, sx1, sy1;
                uint16_t d0, d1;
                Vec3 a = v3(k * dist * 0.30f, fy, dist * 0.45f);
                Vec3 b = v3(k * dist * 0.30f, fy, dist * 1.8f);
                if (r3d_scene_project(a, &sx0, &sy0, &d0) &&
                    r3d_scene_project(b, &sx1, &sy1, &d1))
                    r3d_scene_add_line(sx0, sy0, 1, sx1, sy1, 1, gc);
                Vec3 c2 = v3(-dist * 0.62f, fy, dist * (0.6f + 0.3f * (k + 2)));
                Vec3 e2 = v3(dist * 0.62f, fy, dist * (0.6f + 0.3f * (k + 2)));
                if (r3d_scene_project(c2, &sx0, &sy0, &d0) &&
                    r3d_scene_project(e2, &sx1, &sy1, &d1))
                    r3d_scene_add_line(sx0, sy0, 1, sx1, sy1, 1, gc);
            }
        } else if (pv != 0) {
            const Mesh *m = (pv == 1)
                ? (s_station_mesh ? s_station_mesh : &mesh_station)
                : hull_mesh(pv_seed, pv_cls);
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
        obj.mesh = hull_mesh(g_player.hull_seed, g_player.hull_id);
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
        rocks_render(p->pos, s_time);
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
    /* Z-space starlines: each star is a fixed direction with a cycling
     * depth; radius = F/z, so streaks are born dim near the centre and
     * accelerate past the edges — continuous, no banding, no popping.
     * Streak length = one frame of travel, stretched as speed builds. */
    float t = s_hyper_t;
    float spool = t < 0.6f ? t / 0.6f : 1.0f;
    for (int i = 0; i < 90; i++) {
        uint32_t h = s_hyper_seed ^ (uint32_t)(i * 2654435761u);
        h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
        float ang = (float)(h & 0x3FF) * (6.2831853f / 1024.0f);
        float spd = 0.5f + (float)((h >> 10) & 0xFF) * (1.0f / 255.0f);
        float z0 = (float)((h >> 18) & 0x3FF) * (2.2f / 1024.0f);
        float zz = z0 - t * spd * (0.6f + spool * 1.4f);
        zz = zz - 2.2f * floorf(zz / 2.2f);          /* wrap 0..2.2 */
        zz += 0.10f;
        float dz = spd * (0.6f + spool * 1.4f) * 0.06f * (1.0f + spool);
        float r1 = 9.5f / zz;
        float r0 = 9.5f / (zz + dz);
        if (r0 > 100.0f) continue;
        if (r1 < 5.0f) continue;                     /* skip centre clump */
        if (r1 > 100.0f) r1 = 100.0f;
        float ca = cosf(ang), sa = sinf(ang) * 0.92f;
        int steps = (int)(r1 - r0) + 1;
        if (steps > 30) steps = 30;
        for (int s2 = 0; s2 <= steps; s2++) {
            float rr = r0 + (r1 - r0) * (float)s2 / (float)steps;
            int x = 64 + (int)(ca * rr);
            int y = 60 + (int)(sa * rr);
            if ((unsigned)x >= ELITE_FB_W || (unsigned)y >= ELITE_FB_H)
                continue;
            /* Head (far end of the streak) brightest. */
            float k = (float)s2 / (float)steps;
            uint16_t c = (k > 0.8f) ? RGB565C(240, 245, 255)
                       : (k > 0.45f) ? RGB565C(150, 175, 245)
                                     : RGB565C(60, 80, 165);
            fb[y * ELITE_FB_W + x] = c;
        }
    }
    char name[14];
    galaxy_system_name(s_jump_target, name);
    char buf[28];
    snprintf(buf, sizeof buf, "JUMPING: %s", name);
    craft_font_draw(fb, buf, 30, 100, RGB565C(150, 170, 255));
}

/* The flight dashboard instruments. The real console rows are blitted
 * to the top of the screen; below them: two live MFD screens (mini
 * galaxy chart + mini system schematic) and two small buttons,
 * cockpit-bezel styled (user req: instruments, not grey rectangles). */
static void dash_bezel(uint16_t *fb, int x0, int y0, int x1, int y1,
                       bool sel, int cut) {
    /* chamfered MFD bezel: corners cut by `cut` px */
    uint16_t bc = sel ? RGB565C(120, 255, 120) : RGB565C(70, 86, 115);
    uint16_t fill = RGB565C(6, 9, 16);
    for (int y = y0; y <= y1; y++) {
        int inset = 0;
        if (y - y0 < cut) inset = cut - (y - y0);
        if (y1 - y < cut) inset = cut - (y1 - y);
        for (int x = x0 + inset; x <= x1 - inset; x++)
            fb[y * ELITE_FB_W + x] = fill;
        fb[y * ELITE_FB_W + x0 + inset] = bc;
        fb[y * ELITE_FB_W + x1 - inset] = bc;
    }
    for (int x = x0 + cut; x <= x1 - cut; x++) {
        fb[y0 * ELITE_FB_W + x] = bc;
        fb[y1 * ELITE_FB_W + x] = bc;
    }
}

static void dash_mini_galaxy(uint16_t *fb, int x0, int y0, int w, int h) {
    /* a LIVE little chart: stars around us, range ring, us centred */
    float scale = 2.6f;                       /* px per ly */
    float cxl, cyl;
    galaxy_star_pos(s_addr, &cxl, &cyl);
    int cx = x0 + w / 2, cy = y0 + h / 2;
    float half_ly = (float)w * 0.5f / scale;
    int sx0 = (int)floorf((cxl - half_ly) / SECTOR_LY);
    int sx1 = (int)floorf((cxl + half_ly) / SECTOR_LY);
    int sy0 = (int)floorf((cyl - half_ly) / SECTOR_LY);
    int sy1 = (int)floorf((cyl + half_ly) / SECTOR_LY);
    for (int sy2 = sy0; sy2 <= sy1; sy2++)
        for (int sx2 = sx0; sx2 <= sx1; sx2++) {
            int n = galaxy_sector_stars(sx2, sy2);
            for (int i = 0; i < n; i++) {
                SysAddr a2 = { sx2, sy2, (uint8_t)i };
                float px2, py2;
                galaxy_star_pos(a2, &px2, &py2);
                int x = cx + (int)((px2 - cxl) * scale);
                int y = cy + (int)((py2 - cyl) * scale);
                if (x <= x0 + 1 || x >= x0 + w - 2 ||
                    y <= y0 + 1 || y >= y0 + h - 2)
                    continue;
                fb[y * ELITE_FB_W + x] = RGB565C(165, 175, 205);
            }
        }
    /* jump-range ring */
    float rr = (k_hulls[g_player.hull_id].jump_range * player_roll()->jmp) * scale;
    for (int a2 = 0; a2 < 28; a2++) {
        float th = (float)a2 * (6.2831853f / 28.0f);
        int x = cx + (int)(cosf(th) * rr);
        int y = cy + (int)(sinf(th) * rr);
        if (x > x0 + 1 && x < x0 + w - 2 && y > y0 + 1 && y < y0 + h - 2)
            fb[y * ELITE_FB_W + x] = RGB565C(70, 150, 90);
    }
    /* us */
    fb[cy * ELITE_FB_W + cx] = RGB565C(120, 255, 255);
    fb[(cy - 1) * ELITE_FB_W + cx] = RGB565C(60, 140, 150);
    fb[(cy + 1) * ELITE_FB_W + cx] = RGB565C(60, 140, 150);
    fb[cy * ELITE_FB_W + cx - 1] = RGB565C(60, 140, 150);
    fb[cy * ELITE_FB_W + cx + 1] = RGB565C(60, 140, 150);
}

static void dash_mini_system(uint16_t *fb, int x0, int y0, int w, int h) {
    /* the system schematic: star + planets, station ticks, our anchor */
    const SystemInfo *si = system_info();
    int cy = y0 + h / 2;
    /* star */
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            if (dx * dx + dy * dy <= 5)
                fb[(cy + dy) * ELITE_FB_W + x0 + 6 + dx] = si->star_color;
    int n = si->n_planets > 0 ? si->n_planets : 1;
    int step = (w - 18) / n;
    if (step > 13) step = 13;
    for (int i = 0; i < si->n_planets; i++) {
        int x = x0 + 14 + i * step;
        int r = si->planets[i].type == PT_GAS ? 3 : 2;
        uint16_t c = (si->planets[i].type == PT_LAVA)
                         ? RGB565C(200, 90, 30)
                   : (si->planets[i].type == PT_ICE)
                         ? RGB565C(210, 225, 240)
                   : (si->planets[i].type == PT_GAS)
                         ? RGB565C(200, 170, 120)
                   : (si->planets[i].type == PT_ROCK)
                         ? RGB565C(150, 130, 105)
                         : RGB565C(60, 130, 180);
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
                if (dx * dx + dy * dy <= r * r)
                    fb[(cy + dy) * ELITE_FB_W + x + dx] = c;
        if (si->planets[i].station >= 0) {
            fb[(cy - 6) * ELITE_FB_W + x] = RGB565C(120, 130, 155);
            fb[(cy - 7) * ELITE_FB_W + x - 1] = RGB565C(120, 130, 155);
            fb[(cy - 7) * ELITE_FB_W + x + 1] = RGB565C(120, 130, 155);
        }
        /* our anchor: cyan chevron under the body we're at */
        if (s_anchor_has_poi && s_anchor_poi.kind == POI_PLANET &&
            s_anchor_poi.index == i) {
            fb[(cy + 6) * ELITE_FB_W + x] = RGB565C(120, 255, 255);
            fb[(cy + 7) * ELITE_FB_W + x - 1] = RGB565C(120, 255, 255);
            fb[(cy + 7) * ELITE_FB_W + x + 1] = RGB565C(120, 255, 255);
        }
    }
    if (s_anchor_has_poi && s_anchor_poi.kind == POI_BEACON) {
        fb[(cy + 6) * ELITE_FB_W + x0 + 6] = RGB565C(120, 255, 255);
        fb[(cy + 7) * ELITE_FB_W + x0 + 5] = RGB565C(120, 255, 255);
        fb[(cy + 7) * ELITE_FB_W + x0 + 7] = RGB565C(120, 255, 255);
    }
}

static void dash_draw_panels(uint16_t *fb, int y0) {
    if (y0 >= ELITE_FB_H) return;
    /* console-coloured backing so it reads as one piece of cockpit */
    for (int y = y0; y < ELITE_FB_H; y++)
        for (int x = 0; x < 128; x++)
            fb[y * ELITE_FB_W + x] = RGB565C(13, 17, 27);
    int avail = ELITE_FB_H - y0;
    if (avail < 24) return;                  /* still mostly closed */
    /* MFD pair */
    int mh = avail - 26;
    if (mh > 58) mh = 58;
    int my0 = y0 + 2, my1 = my0 + mh;
    dash_bezel(fb, 2, my0, 62, my1, s_dash_sel == 0, 4);
    dash_bezel(fb, 65, my0, 125, my1, s_dash_sel == 1, 4);
    if (mh > 20) {
        dash_mini_galaxy(fb, 4, my0 + 9, 57, mh - 11);
        dash_mini_system(fb, 67, my0 + 9, 57, mh - 11);
    }
    craft_font_draw(fb, "GALAXY", 9, my0 + 2,
                    s_dash_sel == 0 ? RGB565C(120, 255, 120)
                                    : RGB565C(110, 120, 140));
    craft_font_draw(fb, "SYSTEM", 72, my0 + 2,
                    s_dash_sel == 1 ? RGB565C(120, 255, 120)
                                    : RGB565C(110, 120, 140));
    /* button row: small pills */
    int by = my1 + 3;
    if (by + 10 < ELITE_FB_H) {
        dash_bezel(fb, 10, by, 58, by + 10, s_dash_sel == 2, 3);
        dash_bezel(fb, 70, by, 118, by + 10, s_dash_sel == 3, 3);
        craft_font_draw(fb, "STATUS", 17, by + 2,
                        s_dash_sel == 2 ? RGB565C(120, 255, 120)
                                        : RGB565C(110, 120, 140));
        craft_font_draw(fb, "SETTINGS", 73, by + 2,
                        s_dash_sel == 3 ? RGB565C(120, 255, 120)
                                        : RGB565C(110, 120, 140));
    }
    if (by + 20 < ELITE_FB_H)
        craft_font_draw(fb, "A:OPEN B:RESUME", 22, by + 13,
                        RGB565C(80, 95, 120));
}

static void dash_settings_overlay(uint16_t *fb) {
    for (int y = 38; y < 112; y++)
        for (int x = 14; x < 114; x++)
            fb[y * ELITE_FB_W + x] = RGB565C(8, 11, 20);
    craft_font_draw(fb, "SETTINGS", 33, 42, RGB565C(200, 210, 225));
    char vrow[20], brow[20];
    snprintf(vrow, sizeof vrow, "VOLUME    %3d%%",
             plat_setting_get(0) * 5);
    snprintf(brow, sizeof brow, "BRIGHT    %3d%%",
             (plat_setting_get(1) * 100) / 255);
    const char *si2[4] = {
        g_player.invert_y ? "INVERT Y: ON" : "INVERT Y: OFF",
        g_player.show_fps ? "SHOW FPS: ON" : "SHOW FPS: OFF",
        vrow, brow,
    };
    for (int i = 0; i < 4; i++) {
        uint16_t c = (i == s_settings_cursor) ? RGB565C(120, 255, 120)
                                              : RGB565C(120, 126, 145);
        if (i == s_settings_cursor)
            craft_font_draw(fb, ">", 20, 56 + i * 9, c);
        craft_font_draw(fb, si2[i], 27, 56 + i * 9, c);
    }
    craft_font_draw(fb, "</>:ADJUST B:BACK", 20, 100,
                    RGB565C(95, 110, 140));
}

void elite_game_draw_overlay(uint16_t *fb) {
    if (g_player.show_fps) {
        char fbuf[12];
        snprintf(fbuf, sizeof fbuf, "%d FPS", (int)(s_fps + 0.5f));
        craft_font_draw(fb, fbuf, 64 - craft_font_width(fbuf) / 2, 1,
                        RGB565C(110, 255, 110));
    }

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
            .show_perf = 0,   /* perf validated on device; readout off */
        };
        ui_hud_draw_sc(fb, &info);
        return;
    }

    if (p->alive) {
        Vec3 lpos = v3(0, 0, 0);
        if (s_loot_target >= 0 &&
            loot_nearest(g_ships[PLAYER].pos, &lpos) < 0)
            s_loot_target = -1;          /* scooped/expired */
        Vec3 rpos = v3(0, 0, 0);
        if (s_rock_target >= 0) {
            Vec3 rk[8];
            int nr = rocks_positions(rk, 8);
            if (s_rock_target < nr) rpos = rk[s_rock_target];
            else s_rock_target = -1;     /* cracked it */
        }
        HudInfo info = {
            .target = s_target,
            .loot_valid = (s_target < 0 && s_loot_target >= 0),
            .loot_pos = lpos,
            .rock_valid = (s_target < 0 && s_loot_target < 0 &&
                           s_rock_target >= 0),
            .rock_pos = rpos,
            .station_valid = (s_target < 0 && s_loot_target < 0 &&
                              s_station_lock),
            .rail_charge01 = s_rail_charge01,
            .incoming = s_incoming,
            .kills = combat_kills(),
            .fuel01 = g_player.fuel / g_player.fuel_max,
            .render_ms = s_frame_ms,
            .show_perf = 0,   /* perf validated on device; readout off */
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

    if (s_state == ST_DASH) {
        /* Move the REAL console (the rows the HUD just painted) up the
         * screen; the instrument panels slide into view beneath it. */
        int dtop = ui_hud_dash_top();
        int dash_h = ELITE_FB_H - dtop;
        int up = (int)(s_dash_anim * (float)dtop);
        if (up > 0) {
            memmove(&fb[(dtop - up) * ELITE_FB_W],
                    &fb[dtop * ELITE_FB_W],
                    (size_t)dash_h * ELITE_FB_W * 2);
            dash_draw_panels(fb, dtop - up + dash_h);
            /* Toasts must survive the panels (rescue hails, warnings —
             * the sim is live under here). */
            if (s_scoop_toast_t > 0)
                craft_font_draw(fb, s_scoop_toast,
                                64 - craft_font_width(s_scoop_toast) / 2,
                                dtop - up + dash_h + 2,
                                RGB565C(255, 200, 60));
        }
        if (s_in_settings) dash_settings_overlay(fb);
    }
}
