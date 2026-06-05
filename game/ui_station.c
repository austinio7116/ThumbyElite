/*
 * ThumbyElite — docked station services.
 *
 * HOME menu -> MARKET (scrollable commodity table, A buy / B sell with
 * hold-autorepeat) / REFUEL / LAUNCH. Stubs list the Phase 7/8 services
 * so the player can see what's coming.
 */
#include "ui_station.h"
#include "elite_types.h"
#include "elite_player.h"
#include "system_sim.h"
#include "econ.h"
#include "craft_font.h"
#include <stdio.h>
#include <string.h>

#define COL_BG     RGB565C(  6,  10,  20)
#define COL_HDR    RGB565C(200, 210, 225)
#define COL_GRID   RGB565C( 28,  40,  58)
#define COL_TXT    RGB565C(120, 255, 120)
#define COL_DIM    RGB565C(110, 116, 135)
#define COL_CUR    RGB565C(120, 255, 120)
#define COL_CRED   RGB565C(255, 200,  60)
#define COL_WARN   RGB565C(255, 120,  70)
#define COL_ILL    RGB565C(220, 100, 200)

typedef enum { SCR_HOME = 0, SCR_MARKET } Screen;

static Screen s_screen;
static int s_station;
static int s_cursor;
static int s_scroll;
static int s_bought[N_GOODS];     /* session purchases (depletes stock) */
static CraftRawButtons s_prev;
static float s_hold_a, s_hold_b, s_repeat;
static char s_toast[24];
static float s_toast_t;

#define HOME_ITEMS 6
static const char *k_home[HOME_ITEMS] = {
    "MARKET", "REFUEL", "LAUNCH", "SHIPYARD", "OUTFITTING", "MISSIONS",
};
static const bool k_home_enabled[HOME_ITEMS] = {
    true, true, true, false, false, false,
};

void station_open(int station_idx) {
    s_screen = SCR_HOME;
    s_station = station_idx;
    s_cursor = 0;
    s_scroll = 0;
    memset(s_bought, 0, sizeof s_bought);
    /* Debounce: everything counts as held until released once. */
    memset(&s_prev, 0xFF, sizeof s_prev);
    s_hold_a = s_hold_b = s_repeat = 0;
    s_toast[0] = 0;
    s_toast_t = 0;
}

static void toast(const char *msg) {
    snprintf(s_toast, sizeof s_toast, "%s", msg);
    s_toast_t = 1.6f;
}

static void try_buy(int good) {
    const SystemInfo *si = system_info();
    int price = econ_price(si, s_station, good, true);
    int stock = econ_stock(si, s_station, good) - s_bought[good];
    if (price <= 0) { toast("NO TRADE"); return; }
    if (stock <= 0) { toast("NO STOCK"); return; }
    if (g_player.credits < price) { toast("NO CREDITS"); return; }
    if (player_cargo_total() >= g_player.cargo_cap) { toast("HOLD FULL"); return; }
    g_player.credits -= price;
    g_player.cargo[good]++;
    s_bought[good]++;
}

static void try_sell(int good) {
    const SystemInfo *si = system_info();
    int price = econ_price(si, s_station, good, false);
    if (g_player.cargo[good] == 0) { toast("NONE HELD"); return; }
    if (price <= 0) { toast("NO TRADE"); return; }
    g_player.cargo[good]--;
    g_player.credits += price;
}

static void try_refuel(void) {
    float need = g_player.fuel_max - g_player.fuel;
    if (need < 0.1f) { toast("TANK FULL"); return; }
    int cost = (int)(need * 12.0f) + 1;
    if (g_player.credits < cost) {
        /* Partial refuel with whatever credits allow. */
        float ly = (float)g_player.credits / 12.0f;
        if (ly < 0.1f) { toast("NO CREDITS"); return; }
        g_player.fuel += ly;
        g_player.credits = 0;
        toast("PART REFUEL");
        return;
    }
    g_player.credits -= cost;
    g_player.fuel = g_player.fuel_max;
    toast("REFUELLED");
}

DockAction station_tick(const CraftRawButtons *btn, float dt) {
    DockAction act = DOCK_NONE;
    bool a_edge = btn->a && !s_prev.a;
    bool b_edge = btn->b && !s_prev.b;
    bool up = btn->up && !s_prev.up;
    bool down = btn->down && !s_prev.down;
    bool back = btn->menu && !s_prev.menu;     /* B stays free for SELL */

    if (s_toast_t > 0) s_toast_t -= dt;

    /* Hold-to-repeat for market trading. */
    bool a_rep = false, b_rep = false;
    s_hold_a = btn->a ? s_hold_a + dt : 0;
    s_hold_b = btn->b ? s_hold_b + dt : 0;
    if (s_hold_a > 0.45f || s_hold_b > 0.45f) {
        s_repeat += dt;
        if (s_repeat >= 0.09f) {
            s_repeat = 0;
            if (s_hold_a > 0.45f) a_rep = true;
            if (s_hold_b > 0.45f) b_rep = true;
        }
    }

    switch (s_screen) {
    case SCR_HOME:
        if (up && s_cursor > 0) s_cursor--;
        if (down && s_cursor < HOME_ITEMS - 1) s_cursor++;
        if (a_edge && k_home_enabled[s_cursor]) {
            if (s_cursor == 0) { s_screen = SCR_MARKET; s_cursor = 0; s_scroll = 0; }
            else if (s_cursor == 1) try_refuel();
            else if (s_cursor == 2) act = DOCK_LAUNCH;
        } else if (a_edge) {
            toast("COMING SOON");
        }
        if (back) act = DOCK_LAUNCH;           /* MENU = leave */
        break;

    case SCR_MARKET:
        if (up && s_cursor > 0) s_cursor--;
        if (down && s_cursor < N_GOODS - 1) s_cursor++;
        if (s_cursor < s_scroll) s_scroll = s_cursor;
        if (s_cursor > s_scroll + 8) s_scroll = s_cursor - 8;
        if (a_edge || a_rep) try_buy(s_cursor);
        if (b_edge || b_rep) try_sell(s_cursor);
        if (back) { s_screen = SCR_HOME; s_cursor = 0; }
        break;
    }

    s_prev = *btn;
    return act;
}

/* --- drawing ------------------------------------------------------------*/
static void fill(uint16_t *fb, uint16_t c) {
    for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++) fb[i] = c;
}
static void hl(uint16_t *fb, int y, uint16_t c) {
    for (int x = 0; x < ELITE_FB_W; x++) fb[y * ELITE_FB_W + x] = c;
}

static void draw_header(uint16_t *fb) {
    const SystemInfo *si = system_info();
    craft_font_draw(fb, si->stations[s_station].name, 2, 2, COL_HDR);
    char buf[24];
    snprintf(buf, sizeof buf, "%dCR", g_player.credits);
    craft_font_draw(fb, buf, 128 - craft_font_width(buf) - 2, 2, COL_CRED);
    hl(fb, 9, COL_GRID);
}

static void draw_home(uint16_t *fb) {
    draw_header(fb);
    static const char *k_econ[8] = {
        "AGRICULTURE", "INDUSTRIAL", "HIGH TECH", "EXTRACTION",
        "REFINERY", "TOURISM", "MILITARY", "SERVICE",
    };
    const SystemInfo *si = system_info();
    char buf[32];
    snprintf(buf, sizeof buf, "%s T%d", k_econ[si->stations[s_station].econ],
             si->stations[s_station].tech);
    craft_font_draw(fb, buf, 2, 13, COL_DIM);

    for (int i = 0; i < HOME_ITEMS; i++) {
        uint16_t c = !k_home_enabled[i] ? RGB565C(60, 66, 84)
                   : (i == s_cursor) ? COL_CUR : COL_DIM;
        if (i == s_cursor) craft_font_draw(fb, ">", 8, 28 + i * 11, COL_CUR);
        craft_font_draw(fb, k_home[i], 16, 28 + i * 11, c);
        if (!k_home_enabled[i])
            craft_font_draw(fb, "SOON", 90, 28 + i * 11, RGB565C(60, 66, 84));
    }

    char fuel[24];
    snprintf(fuel, sizeof fuel, "FUEL %d.%d/%d LY",
             (int)g_player.fuel, ((int)(g_player.fuel * 10)) % 10,
             (int)g_player.fuel_max);
    craft_font_draw(fb, fuel, 2, 103, COL_DIM);
    snprintf(fuel, sizeof fuel, "CARGO %d/%d", player_cargo_total(),
             g_player.cargo_cap);
    craft_font_draw(fb, fuel, 2, 110, COL_DIM);
    hl(fb, 118, COL_GRID);
    craft_font_draw(fb, "A:SELECT MENU:LEAVE MENU", 2, 121, COL_DIM);
}

static void draw_market(uint16_t *fb) {
    draw_header(fb);
    const SystemInfo *si = system_info();
    craft_font_draw(fb, "GOODS", 8, 12, COL_DIM);
    craft_font_draw(fb, "BUY", 56, 12, COL_DIM);
    craft_font_draw(fb, "SELL", 76, 12, COL_DIM);
    craft_font_draw(fb, "ST", 100, 12, COL_DIM);
    craft_font_draw(fb, "HL", 114, 12, COL_DIM);
    hl(fb, 19, COL_GRID);

    int y = 22;
    for (int i = s_scroll; i < N_GOODS && i < s_scroll + 9; i++, y += 10) {
        bool illegal = (k_goods[i].flags & GOOD_ILLEGAL) != 0;
        int buy = econ_price(si, s_station, i, true);
        int sell = econ_price(si, s_station, i, false);
        int stock = econ_stock(si, s_station, i) - s_bought[i];
        uint16_t c = (i == s_cursor) ? COL_CUR : illegal ? COL_ILL : COL_DIM;
        if (i == s_cursor) craft_font_draw(fb, ">", 2, y, COL_CUR);
        craft_font_draw(fb, k_goods[i].name, 8, y, c);
        char buf[12];
        if (buy > 0) {
            snprintf(buf, sizeof buf, "%d", buy);
            craft_font_draw(fb, buf, 56, y, c);
            snprintf(buf, sizeof buf, "%d", sell);
            craft_font_draw(fb, buf, 76, y, c);
            snprintf(buf, sizeof buf, "%d", stock > 0 ? stock : 0);
            craft_font_draw(fb, buf, 100, y, c);
        } else {
            craft_font_draw(fb, "--", 56, y, RGB565C(60, 66, 84));
        }
        if (g_player.cargo[i]) {
            snprintf(buf, sizeof buf, "%d", g_player.cargo[i]);
            craft_font_draw(fb, buf, 114, y, COL_CRED);
        }
    }
    hl(fb, 113, COL_GRID);
    char buf[32];
    snprintf(buf, sizeof buf, "HOLD %d/%d", player_cargo_total(),
             g_player.cargo_cap);
    craft_font_draw(fb, buf, 2, 116, COL_DIM);
    craft_font_draw(fb, "A:BUY B:SELL", 70, 116, COL_DIM);
    craft_font_draw(fb, "MENU:BACK", 2, 123, COL_DIM);
}

void station_draw(uint16_t *fb) {
    fill(fb, COL_BG);
    if (s_screen == SCR_MARKET) draw_market(fb);
    else draw_home(fb);

    if (s_toast_t > 0) {
        int w = craft_font_width(s_toast) + 8;
        int x0 = 64 - w / 2;
        for (int y = 58; y < 70; y++)
            for (int x = x0; x < x0 + w; x++)
                if ((unsigned)x < ELITE_FB_W)
                    fb[y * ELITE_FB_W + x] = RGB565C(30, 20, 12);
        craft_font_draw(fb, s_toast, x0 + 4, 61, COL_WARN);
    }
}
