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
#include "elite_player.h"
#include "elite_combat.h"
#include "mission.h"
#include "system_sim.h"
#include <math.h>
#include "meshes_gen.h"
#include "craft_buttons.h"
#include "elite_platform.h"
#include "elite_audio.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SCALE 5
#define WIN_W (ELITE_FB_W * SCALE)
#define WIN_H (ELITE_FB_H * SCALE)

static uint16_t g_fb[ELITE_FB_W * ELITE_FB_H];

/* --- platform hooks ----------------------------------------------------*/
void plat_rumble(float intensity, float seconds) {
    (void)intensity; (void)seconds;     /* no motor on the desk */
}
int plat_save(const uint8_t *data, int len) {
    FILE *f = fopen("thumbyelite.sav", "wb");
    if (!f) return 0;
    fwrite(data, 1, (size_t)len, f);
    fclose(f);
    return 1;
}
int plat_load(uint8_t *data, int max_len) {
    FILE *f = fopen("thumbyelite.sav", "rb");
    if (!f) return 0;
    int n = (int)fread(data, 1, (size_t)max_len, f);
    fclose(f);
    return n;
}

static void audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    audio_render((int16_t *)stream, len / (int)sizeof(int16_t));
}

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

extern int g_dbg_dust[2];
extern float g_dbg_dustf[4];

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *shot_path = getenv("ELITE_SHOT");

    uint32_t seed = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0)
                               : (uint32_t)time(NULL);
    printf("[elite] seed = %u\n", seed);
    elite_game_init(seed);
    if (getenv("ELITE_DEMO") || getenv("ELITE_FIRETEST") ||
        getenv("ELITE_KILLTEST") || getenv("ELITE_TRAVELTEST") ||
        getenv("ELITE_TRADETEST") || getenv("ELITE_JUMPTEST") ||
        getenv("ELITE_LOOTTEST") || getenv("ELITE_SHOPTEST") ||
        getenv("ELITE_MISTEST") || getenv("ELITE_STATUSTEST") ||
        getenv("ELITE_BTEST") ||
        getenv("ELITE_STARTCHECK") ||
        getenv("ELITE_ACTION") ||
        getenv("ELITE_SHOT")) {
        /* Harnesses start in-game: skip the title via NEW GAME. */
        remove("thumbyelite.sav");
        CraftRawButtons tb = {0};
        elite_game_tick(&tb, 1.0f / 30.0f);
        tb.down = true; elite_game_tick(&tb, 1.0f / 30.0f);
        tb.down = false; elite_game_tick(&tb, 1.0f / 30.0f);
        tb.a = true; elite_game_tick(&tb, 1.0f / 30.0f);
        tb.a = false; elite_game_tick(&tb, 1.0f / 30.0f);
    }

    /* Start-cluster audit: roll N seeds, report each start's
     * starter-range neighbourhood. */
    if (getenv("ELITE_STARTCHECK")) {
        CraftRawButtons none = {0};
        for (int k = 0; k < 8; k++) elite_game_tick(&none, 1.0f / 30.0f);
        const SystemInfo *si = system_info();
        float px, py;
        galaxy_star_pos(si->addr, &px, &py);
        float best = 1e9f;
        int in6 = 0, in8 = 0;
        for (int ny = si->addr.sy - 2; ny <= si->addr.sy + 2; ny++)
            for (int nx = si->addr.sx - 2; nx <= si->addr.sx + 2; nx++) {
                int nn = galaxy_sector_stars(nx, ny);
                for (int j = 0; j < nn; j++) {
                    SysAddr b = { nx, ny, (uint8_t)j };
                    if (sysaddr_eq(b, si->addr)) continue;
                    float bx, by;
                    galaxy_star_pos(b, &bx, &by);
                    float d = sqrtf((bx - px) * (bx - px) +
                                    (by - py) * (by - py));
                    if (d < best) best = d;
                    if (d <= 6.0f) in6++;
                    if (d <= 8.0f) in8++;
                }
            }
        printf("[startcheck] %s nearest=%.1f in6=%d in8=%d stations=%d\n",
               si->name, best, in6, in8, si->n_stations);
        return 0;
    }

    /* Staged combat captures for the guide: hostiles close ahead,
     * lock, pulse volleys, a gauss helix, the kill. */
    if (getenv("ELITE_ACTION")) {
        CraftRawButtons none = {0}, b;
        Ship *pl = &g_ships[0];
        pl->vel = v3(0, 0, 0);
        pl->throttle = 0.2f;
        /* Face away from the sun so it doesn't photobomb the shots. */
        elite_game_debug_face_away_from_sun();
        /* One BIG close target + a mid-range ship for the gauss run. */
        extern const Mesh *hull_mesh(uint32_t, int);
        Vec3 fwd = pl->basis.r[2];
        Vec3 rgt = pl->basis.r[0];
        Vec3 up2 = pl->basis.r[1];
        int e1 = ship_spawn(hull_mesh(0xACE1u, 5),
                            v3_add(pl->pos, v3_add(v3_scale(fwd, 50.0f),
                                                   v3_scale(rgt, 6.0f))),
                            TEAM_HOSTILE);
        if (e1 > 0) ship_set_tier(e1, 0, 5);   /* HARMLESS: we survive */
        int e2 = ship_spawn(hull_mesh(0xBEE5u, 3),
                            v3_add(pl->pos,
                                   v3_add(v3_scale(fwd, 130.0f),
                                          v3_add(v3_scale(rgt, -28.0f),
                                                 v3_scale(up2, 12.0f)))),
                            TEAM_HOSTILE);
        if (e2 > 0) ship_set_tier(e2, 0, 3);
        /* Plot armour for the cameraman. */
        pl->hull_max = pl->hull = 100000.0f;
        pl->shield_max = pl->shield = 100000.0f;
        /* Lock the close one. */
        b = none; b.lb = true; elite_game_tick(&b, 1.0f / 30.0f);
        b = none; elite_game_tick(&b, 1.0f / 30.0f);
        elite_game_tick(&none, 1.0f / 30.0f);
        /* Pulse volley: dump ON a fire frame so the beam is lit. */
        for (int f = 0; f < 3; f++) {
            b = none; b.a = true; elite_game_tick(&b, 1.0f / 30.0f);
            if (f == 0) { render_frame(); dump_ppm("/tmp/act_pulse.ppm"); }
            b = none; elite_game_tick(&b, 1.0f / 30.0f);
            elite_game_tick(&none, 1.0f / 30.0f);
        }
        render_frame(); dump_ppm("/tmp/act_shield.ppm");
        /* Finish the close target with pulses; dump the boom. */
        for (int f = 0; f < 240; f++) {
            pl->fire_cool = 0;
            b = none; b.a = true; elite_game_tick(&b, 1.0f / 30.0f);
            b = none; elite_game_tick(&b, 1.0f / 30.0f);
            if (e1 > 0 && !g_ships[e1].alive) break;
        }
        elite_game_tick(&none, 1.0f / 30.0f);
        render_frame(); dump_ppm("/tmp/act_boom.ppm");
        for (int f = 0; f < 6; f++) elite_game_tick(&none, 1.0f / 30.0f);
        render_frame(); dump_ppm("/tmp/act_debris.ppm");
        /* Let the fireball burn out, then the gauss run. */
        for (int f = 0; f < 90; f++) elite_game_tick(&none, 1.0f / 30.0f);
        {
            extern int fx_alive_count(void);
            Vec3 f3 = pl->basis.r[2];
            printf("[act] fwd=(%.2f %.2f %.2f) fx=%d alive:", f3.x, f3.y,
                   f3.z, fx_alive_count());
            for (int k = 0; k < MAX_SHIPS; k++)
                if (g_ships[k].alive) printf(" %d", k);
            printf("\n");
        }
        b = none; b.lb = true; elite_game_tick(&b, 1.0f / 30.0f);
        b = none; elite_game_tick(&b, 1.0f / 30.0f);
        ship_fit_weapon(0, 0, WPN_GAUSS);
        pl->active_w = 0;
        pl->fire_cool = 0;
        b = none; b.a = true; elite_game_tick(&b, 1.0f / 30.0f);
        render_frame(); dump_ppm("/tmp/act_gauss.ppm");
        b = none; elite_game_tick(&b, 1.0f / 30.0f);
        render_frame(); dump_ppm("/tmp/act_gauss2.ppm");
        return 0;
    }

    /* Title screen capture. */
    if (getenv("ELITE_TITLESHOT")) {
        CraftRawButtons none = {0};
        for (int k = 0; k < 45; k++) elite_game_tick(&none, 1.0f / 30.0f);
        render_frame();
        dump_ppm("/tmp/title.ppm");
        return 0;
    }

    /* Headless autopilot: chase the scanner's nearest hostile and hold the
     * trigger for N seconds, logging the combat loop each second. */
    if (getenv("ELITE_DEMO")) {
        int secs = atoi(getenv("ELITE_DEMO"));
        if (secs < 1) secs = 20;
        if (ships_alive_hostile() == 0) elite_game_debug_spawn(3);
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

    /* Deterministic gun-visual test: park one hostile dead ahead, fire,
     * render the same frame. ELITE_KILLTEST=n: keep firing until it dies,
     * then advance n more frames and render (explosion stages). */
    if (getenv("ELITE_FIRETEST") || getenv("ELITE_KILLTEST")) {
        for (int i = 1; i < 16; i++) g_ships[i].alive = false;
        if (getenv("ELITE_FITWPN")) {
            ship_fit_weapon(0, 0, (WeaponType)atoi(getenv("ELITE_FITWPN")));
            g_ships[0].active_w = 0;
        }
        Ship *pl = &g_ships[0];
        pl->vel = v3(0, 0, 0);
        pl->throttle = 0;
        Vec3 ahead = v3_add(pl->pos, v3_scale(pl->basis.r[2], 120.0f));
        int e = ship_spawn(&mesh_viper, ahead, TEAM_HOSTILE);
        if (getenv("ELITE_KILLTEST")) {
            /* One-shot kill, parked target (neutral = no AI movement). */
            g_ships[e].team = TEAM_NEUTRAL;
            g_ships[e].shield = 0;
            g_ships[e].hull = 1;
        }
        CraftRawButtons b = {0};
        b.a = true;
        elite_game_tick(&b, 1.0f / 30.0f);   /* fire frame */
        if (getenv("ELITE_KILLTEST")) {
            int after = atoi(getenv("ELITE_KILLTEST"));
            CraftRawButtons none = {0};
            for (int f = 0; f < after; f++)
                elite_game_tick(&none, 1.0f / 30.0f);
        }
        render_frame();
        dump_ppm(shot_path ? shot_path : "firetest.ppm");
        printf("[fire] target alive=%d hull=%.0f kills=%d\n",
               g_ships[e].alive, g_ships[e].hull, combat_kills());
        return 0;
    }

    /* Travel test: pause menu -> system map -> pick a POI -> supercruise
     * to arrival, dumping shots along the way. ELITE_TRAVELTEST=poi_index. */
    if (getenv("ELITE_TRAVELTEST")) {
        int poi = atoi(getenv("ELITE_TRAVELTEST"));
        CraftRawButtons none = {0}, b;
        char path[64];
        int shot = 0;
        #define TAP(field, settle_n) do { \
            b = none; b.field = true; \
            elite_game_tick(&b, 1.0f / 30.0f); \
            for (int k = 0; k < (settle_n); k++) \
                elite_game_tick(&none, 1.0f / 30.0f); \
        } while (0)
        #define SNAP(tag) do { \
            render_frame(); \
            snprintf(path, sizeof path, "/tmp/travel_%d_%s.ppm", shot++, tag); \
            dump_ppm(path); \
        } while (0)
        for (int k = 0; k < 30; k++) elite_game_tick(&none, 1.0f / 30.0f);
        SNAP("start");
        TAP(menu, 4);                       /* pause */
        TAP(down, 3); TAP(down, 3);         /* -> SYSTEM MAP */
        TAP(a, 4);
        SNAP("sysmap");
        for (int i = 0; i < poi; i++) TAP(down, 2);
        TAP(a, 4);                          /* engage supercruise */
        SNAP("sc0");
        printf("[travel] state after engage: %d (1=SC)\n", elite_game_state());
        int f = 0;
        while (elite_game_state() == 1 && f < 30 * 240) {
            elite_game_tick(&none, 1.0f / 30.0f);
            f++;
            if (f == 120) { SNAP("mid");
                printf("[dust] projected=%d onscreen=%d spd=%.1f R=%.1f "
                       "rel0=(%.1f,%.1f)\n",
                       g_dbg_dust[0], g_dbg_dust[1], g_dbg_dustf[0],
                       g_dbg_dustf[1], g_dbg_dustf[2], g_dbg_dustf[3]); }
            if (f == 300 || f == 1800) SNAP("cruise");
        }
        printf("[travel] arrived after %ds, state=%d\n", f / 30,
               elite_game_state());
        for (int k = 0; k < 20; k++) elite_game_tick(&none, 1.0f / 30.0f);
        SNAP("arrived");
        return 0;
        #undef TAP
        #undef SNAP
    }

    /* Trade test: supercruise to a station POI, dock with LB+RB, browse
     * the market, buy a couple of units, launch. ELITE_TRADETEST=poi. */
    if (getenv("ELITE_TRADETEST")) {
        int poi = atoi(getenv("ELITE_TRADETEST"));
        CraftRawButtons none = {0}, b;
        #define TAPB(field, settle_n) do { \
            b = none; b.field = true; \
            elite_game_tick(&b, 1.0f / 30.0f); \
            for (int k = 0; k < (settle_n); k++) \
                elite_game_tick(&none, 1.0f / 30.0f); \
        } while (0)
        for (int k = 0; k < 30; k++) elite_game_tick(&none, 1.0f / 30.0f);
        TAPB(menu, 4);
        TAPB(down, 3); TAPB(down, 3);
        TAPB(a, 4);
        for (int i = 0; i < poi; i++) TAPB(down, 2);
        TAPB(a, 4);
        int f = 0;
        while (elite_game_state() == 1 && f++ < 30 * 240)
            elite_game_tick(&none, 1.0f / 30.0f);
        printf("[trade] at station, state=%d\n", elite_game_state());
        /* Dock: hold LB+RB. */
        b = none; b.lb = b.rb = true;
        for (int k = 0; k < 8; k++) elite_game_tick(&b, 1.0f / 30.0f);
        f = 0;
        while (elite_game_state() == 6 && f++ < 30 * 5)   /* 6 = DOCKING */
            elite_game_tick(&none, 1.0f / 30.0f);
        printf("[trade] docked, state=%d (7=DOCKED)\n", elite_game_state());
        /* Let the dock debounce clear (everything reads as held on open). */
        for (int k = 0; k < 4; k++) elite_game_tick(&none, 1.0f / 30.0f);
        render_frame(); dump_ppm("/tmp/trade_0_home.ppm");
        TAPB(a, 4);                       /* open MARKET */
        render_frame(); dump_ppm("/tmp/trade_1_market.ppm");
        TAPB(a, 2); TAPB(a, 2);           /* buy 2 of FOOD (row 0) */
        TAPB(down, 2); TAPB(down, 2);     /* down to WATER */
        TAPB(a, 2);                       /* buy 1 */
        render_frame(); dump_ppm("/tmp/trade_2_bought.ppm");
        TAPB(menu, 4);                    /* back to home */
        TAPB(down, 3); TAPB(down, 3);     /* cursor to LAUNCH */
        TAPB(a, 8);
        printf("[trade] launched, state=%d (0=FLIGHT)\n", elite_game_state());
        for (int k = 0; k < 30; k++) elite_game_tick(&none, 1.0f / 30.0f);
        render_frame(); dump_ppm("/tmp/trade_3_launched.ppm");
        return 0;
        #undef TAPB
    }

    /* Salvage test: force component drops next to the player, scoop. */
    if (getenv("ELITE_LOOTTEST")) {
        extern void loot_on_kill(Vec3 pos, Vec3 vel, int tier);
        CraftRawButtons none = {0};
        for (int k = 0; k < 10; k++) elite_game_tick(&none, 1.0f / 30.0f);
        Ship *p = &g_ships[0];
        p->vel = v3(0, 0, 0);
        p->throttle = 0;
        for (int i = 0; i < 8; i++)
            loot_on_kill(v3_add(p->pos, v3(5, 0, 14)), v3(0, 0, 0), 4);
        for (int k = 0; k < 90; k++) elite_game_tick(&none, 1.0f / 30.0f);
        int rack = 0;
        for (int i = 0; i < MAX_SALVAGE; i++)
            if (g_player.salvage[i].in_use) rack++;
        printf("[loot] rack=%d cargo=%d xp_tech=%d\n",
               rack, player_cargo_total(), g_player.xp_tech);
        render_frame(); dump_ppm("/tmp/loot.ppm");
        return 0;
    }

    /* Shop test: rich pilot docks, buys a dreadnought + gear. */
    if (getenv("ELITE_SHOPTEST")) {
        CraftRawButtons none = {0}, b;
        #define TAPS(field, settle_n) do { \
            b = none; b.field = true; \
            elite_game_tick(&b, 1.0f / 30.0f); \
            for (int k = 0; k < (settle_n); k++) \
                elite_game_tick(&none, 1.0f / 30.0f); \
        } while (0)
        g_player.credits = 250000;
        for (int k = 0; k < 30; k++) elite_game_tick(&none, 1.0f / 30.0f);
        TAPS(menu, 4); TAPS(down, 3); TAPS(down, 3); TAPS(a, 4);
        int target_poi = atoi(getenv("ELITE_SHOPTEST"));
        for (int i = 0; i < target_poi; i++) TAPS(down, 2);
        TAPS(a, 4);
        int f = 0;
        while (elite_game_state() == 1 && f++ < 30 * 240)
            elite_game_tick(&none, 1.0f / 30.0f);
        b = none; b.lb = b.rb = true;
        for (int k = 0; k < 8; k++) elite_game_tick(&b, 1.0f / 30.0f);
        while (elite_game_state() == 6) elite_game_tick(&none, 1.0f / 30.0f);
        for (int k = 0; k < 4; k++) elite_game_tick(&none, 1.0f / 30.0f);
        TAPS(down, 2); TAPS(a, 4);                    /* SHIPYARD */
        for (int i = 0; i < 9; i++) TAPS(down, 2);    /* BASILISK */
        render_frame(); dump_ppm("/tmp/shop_0_yard.ppm");
        TAPS(a, 4);
        printf("[shop] hull=%d credits=%d\n", g_player.hull_id,
               g_player.credits);
        TAPS(menu, 3);
        TAPS(down, 2); TAPS(a, 4);                    /* OUTFITTING */
        render_frame(); dump_ppm("/tmp/shop_1_outfit.ppm");
        TAPS(down, 2); TAPS(down, 2); TAPS(down, 2);  /* shield upgrade row */
        TAPS(a, 4);
        printf("[shop] shield_tier=%d credits=%d\n", g_player.shield_eq.tier,
               g_player.credits);
        render_frame(); dump_ppm("/tmp/shop_2_upgrade.ppm");
        TAPS(menu, 3);
        TAPS(down, 2); TAPS(down, 2); TAPS(down, 2);  /* -> STATUS (idx 5) */
        TAPS(a, 4);
        render_frame(); dump_ppm("/tmp/shop_3_status.ppm");
        return 0;
        #undef TAPS
    }

    /* Mission test: dock, accept a cull, launch, slaughter, re-dock,
     * verify the payout + rep. */
    if (getenv("ELITE_MISTEST")) {
        extern Mission g_missions[];
        extern int8_t g_rep[];
        extern void combat_direct_damage(int, int, float, Vec3);
        CraftRawButtons none = {0}, b;
        #define TAPM(field, settle_n) do { \
            b = none; b.field = true; \
            elite_game_tick(&b, 1.0f / 30.0f); \
            for (int k = 0; k < (settle_n); k++) \
                elite_game_tick(&none, 1.0f / 30.0f); \
        } while (0)
        for (int k = 0; k < 30; k++) elite_game_tick(&none, 1.0f / 30.0f);
        /* travel to station + dock */
        TAPM(menu, 4); TAPM(down, 3); TAPM(down, 3); TAPM(a, 4);
        int poi = atoi(getenv("ELITE_MISTEST"));
        for (int i = 0; i < poi; i++) TAPM(down, 2);
        TAPM(a, 4);
        int f = 0;
        while (elite_game_state() == 1 && f++ < 30 * 240)
            elite_game_tick(&none, 1.0f / 30.0f);
        b = none; b.lb = b.rb = true;
        for (int k = 0; k < 8; k++) elite_game_tick(&b, 1.0f / 30.0f);
        while (elite_game_state() == 6) elite_game_tick(&none, 1.0f / 30.0f);
        for (int k = 0; k < 4; k++) elite_game_tick(&none, 1.0f / 30.0f);
        /* MISSIONS = home row 3 */
        TAPM(down, 2); TAPM(down, 2); TAPM(down, 2); TAPM(a, 4);
        render_frame(); dump_ppm("/tmp/mis_0_board.ppm");
        /* Find the CULL row in this visit's deterministic offers. Offers
         * were generated when the screen opened; regenerating with the
         * same visit salt would advance nothing — but make_offers uses
         * s_visit_salt which on_docked bumped BEFORE the screen opened,
         * so a direct call now matches what's displayed. */
        /* Engine-level accept: reroll offers (visit salt) until a cull
         * appears, then accept directly (the UI path is the same code). */
        Mission probe[MISSION_OFFERS];
        int cull_row = -1;
        for (int reroll = 0; reroll < 10 && cull_row < 0; reroll++) {
            mission_make_offers(system_info(), 0, probe);
            for (int r = 0; r < MISSION_OFFERS; r++)
                if (probe[r].type == MIS_CULL) { cull_row = r; break; }
            if (cull_row < 0) mission_on_docked(system_info(), 0);
        }
        printf("[mis] cull offer at row %d\n", cull_row);
        if (cull_row < 0) return 1;
        mission_accept(&probe[cull_row]);
        int accepted = -1;
        for (int m = 0; m < 4; m++)
            if (g_missions[m].type == MIS_CULL) accepted = m;
        printf("[mis] accepted=%d label=%s count=%d reward=%d\n",
               accepted, accepted >= 0 ? g_missions[accepted].label : "-",
               accepted >= 0 ? g_missions[accepted].count : 0,
               accepted >= 0 ? g_missions[accepted].reward : 0);
        render_frame(); dump_ppm("/tmp/mis_1_log.ppm");
        if (accepted < 0) return 1;
        int need = g_missions[accepted].count;
        /* launch (home row 7) */
        TAPM(menu, 3);
        for (int i = 0; i < 7; i++) TAPM(down, 2);
        TAPM(a, 8);
        printf("[mis] launched state=%d\n", elite_game_state());
        /* spawn + execute pirates */
        elite_game_debug_spawn(need);
        for (int i = 1; i < 16; i++)
            if (g_ships[i].alive)
                combat_direct_damage(0, i, 9999.0f, g_ships[i].pos);
        elite_game_tick(&none, 1.0f / 30.0f);
        printf("[mis] after cull: done=%d credits_before_pay=%d\n",
               g_missions[accepted].done, g_player.credits);
        /* re-dock */
        b = none; b.lb = b.rb = true;
        for (int k = 0; k < 8; k++) elite_game_tick(&b, 1.0f / 30.0f);
        while (elite_game_state() == 6) elite_game_tick(&none, 1.0f / 30.0f);
        for (int k = 0; k < 4; k++) elite_game_tick(&none, 1.0f / 30.0f);
        printf("[mis] paid: credits=%d rep=[%d %d %d]\n", g_player.credits,
               g_rep[0], g_rep[1], g_rep[2]);
        render_frame(); dump_ppm("/tmp/mis_2_paid.ppm");
        return 0;
        #undef TAPM
    }

    /* Status-bounce repro: pause -> down x3 -> A. Expect ST_STATUS (8). */
    if (getenv("ELITE_STATUSTEST")) {
        CraftRawButtons none = {0}, b;
        for (int k = 0; k < 10; k++) elite_game_tick(&none, 1.0f / 30.0f);
        b = none; b.menu = true; elite_game_tick(&b, 1.0f / 30.0f);
        for (int k = 0; k < 3; k++) elite_game_tick(&none, 1.0f / 30.0f);
        printf("[st] in pause: state=%d (want 5)\n", elite_game_state());
        for (int i = 0; i < 3; i++) {
            b = none; b.down = true; elite_game_tick(&b, 1.0f / 30.0f);
            elite_game_tick(&none, 1.0f / 30.0f);
            printf("[st] down %d: state=%d\n", i, elite_game_state());
        }
        b = none; b.a = true;
        elite_game_tick(&b, 1.0f / 30.0f);          /* select SHIP STATUS */
        printf("[st] after A1: state=%d (want 8)\n", elite_game_state());
        elite_game_tick(&b, 1.0f / 30.0f);          /* A held one more frame */
        printf("[st] after A2: state=%d (want 8)\n", elite_game_state());
        for (int k = 0; k < 5; k++) elite_game_tick(&none, 1.0f / 30.0f);
        printf("[st] after release: state=%d (want 8)\n", elite_game_state());
        b = none; b.a = true; elite_game_tick(&b, 1.0f / 30.0f);
        elite_game_tick(&none, 1.0f / 30.0f);
        printf("[st] after A tap (detail open): state=%d (want 8)\n",
               elite_game_state());
        render_frame(); dump_ppm("/tmp/status_detail.ppm");
        b = none; b.b = true; elite_game_tick(&b, 1.0f / 30.0f);
        elite_game_tick(&none, 1.0f / 30.0f);
        render_frame(); dump_ppm("/tmp/status_list.ppm");
        b = none; b.b = true; elite_game_tick(&b, 1.0f / 30.0f);
        elite_game_tick(&none, 1.0f / 30.0f);
        printf("[st] after B B: state=%d (want 0)\n", elite_game_state());
        return 0;
    }

    /* Continue test: boot with an existing save, pick CONTINUE. */
    if (getenv("ELITE_CONTEST")) {
        CraftRawButtons none = {0}, b;
        for (int k = 0; k < 5; k++) elite_game_tick(&none, 1.0f / 30.0f);
        b = none; b.a = true; elite_game_tick(&b, 1.0f / 30.0f);
        for (int k = 0; k < 5; k++) elite_game_tick(&none, 1.0f / 30.0f);
        printf("[cont] state=%d (7=DOCKED) credits=%d hull=%d\n",
               elite_game_state(), g_player.credits, g_player.hull_id);
        render_frame(); dump_ppm("/tmp/continue.ppm");
        return 0;
    }

    /* B-cycle check: roll test-mode ships until multi-slot, press B. */
    if (getenv("ELITE_BTEST")) {
        CraftRawButtons none = {0}, b;
        for (int k = 0; k < 8; k++) elite_game_tick(&none, 1.0f / 30.0f);
        Ship *p = &g_ships[0];
        printf("[b] hull=%d n_weapons=%d active=%d\n",
               g_player.hull_id, p->n_weapons, p->active_w);
        for (int i = 0; i < 3; i++) {
            b = none; b.b = true; elite_game_tick(&b, 1.0f / 30.0f);
            elite_game_tick(&none, 1.0f / 30.0f);
            printf("[b] after B tap %d: active=%d\n", i, p->active_w);
        }
        return 0;
    }

    /* Hyperjump test: galaxy map, nudge cursor right until a new system
     * highlights in range, engage, ride the tunnel. */
    if (getenv("ELITE_JUMPTEST")) {
        CraftRawButtons none = {0}, b;
        for (int k = 0; k < 10; k++) elite_game_tick(&none, 1.0f / 30.0f);
        b = none; b.menu = true; elite_game_tick(&b, 1.0f / 30.0f);
        for (int k = 0; k < 4; k++) elite_game_tick(&none, 1.0f / 30.0f);
        b = none; b.down = true; elite_game_tick(&b, 1.0f / 30.0f);
        for (int k = 0; k < 3; k++) elite_game_tick(&none, 1.0f / 30.0f);
        b = none; b.a = true; elite_game_tick(&b, 1.0f / 30.0f);   /* galaxy map */
        for (int k = 0; k < 4; k++) elite_game_tick(&none, 1.0f / 30.0f);
        render_frame(); dump_ppm("/tmp/jump_0_map.ppm");
        /* Deterministic aim: find the nearest in-range neighbour and tap
         * its dominant axis (the snap wedge is +-60deg, so one tap lands
         * on it), then engage. */
        {
            const SystemInfo *si = system_info();
            float px, py;
            galaxy_star_pos(si->addr, &px, &py);
            float bx = 0, by = 0, bd = 1e9f;
            for (int sy = si->addr.sy - 2; sy <= si->addr.sy + 2; sy++)
                for (int sx = si->addr.sx - 2; sx <= si->addr.sx + 2; sx++) {
                    int n = galaxy_sector_stars(sx, sy);
                    for (int i = 0; i < n; i++) {
                        SysAddr a2 = { sx, sy, (uint8_t)i };
                        if (sysaddr_eq(a2, si->addr)) continue;
                        float x, y;
                        galaxy_star_pos(a2, &x, &y);
                        float d = sqrtf((x - px) * (x - px) +
                                        (y - py) * (y - py));
                        if (d < bd) { bd = d; bx = x - px; by = y - py; }
                    }
                }
            printf("[jump] nearest %.1f ly (dx %.1f dy %.1f)\n", bd, bx, by);
            b = none;
            if (bx * bx > by * by) {
                if (bx > 0) b.right = true; else b.left = true;
            } else {
                if (by > 0) b.down = true; else b.up = true;
            }
            elite_game_tick(&b, 1.0f / 30.0f);
            b = none; elite_game_tick(&b, 1.0f / 30.0f);
            b = none; b.a = true; elite_game_tick(&b, 1.0f / 30.0f);
            b = none; elite_game_tick(&b, 1.0f / 30.0f);
            render_frame(); dump_ppm("/tmp/jump_survey.ppm");
            b = none; b.a = true; elite_game_tick(&b, 1.0f / 30.0f);
            b = none; elite_game_tick(&b, 1.0f / 30.0f);
        }
        printf("[jump] state=%d after pan (2=hyperjump)\n", elite_game_state());
        render_frame(); dump_ppm("/tmp/jump_1_engaged.ppm");
        for (int k = 0; k < 40; k++) elite_game_tick(&none, 1.0f / 30.0f);
        render_frame(); dump_ppm("/tmp/jump_2_tunnel.ppm");
        while (elite_game_state() == 2)
            elite_game_tick(&none, 1.0f / 30.0f);
        for (int k = 0; k < 10; k++) elite_game_tick(&none, 1.0f / 30.0f);
        render_frame(); dump_ppm("/tmp/jump_3_arrived.ppm");
        printf("[jump] arrived, state=%d\n", elite_game_state());
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

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = ELITE_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_cb;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (adev) SDL_PauseAudioDevice(adev, 0);

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
