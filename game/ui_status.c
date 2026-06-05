/*
 * ThumbyElite — ship status sheet.
 *
 * Cursor-driven: weapons (mounted + racked) and cargo lines are
 * selectable; A drills into a full detail sheet (effective stats for
 * gear, market notes for goods). The ship itself turns in the top-right
 * window (the game renders it behind this UI).
 */
#include "ui_status.h"
#include "elite_types.h"
#include "elite_player.h"
#include "elite_ships.h"
#include "elite_weapons.h"
#include "ui_icons.h"
#include "ui_detail.h"
#include "econ.h"
#include "craft_font.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define COL_BG    RGB565C(  6,  10,  20)
#define COL_HDR   RGB565C(200, 210, 225)
#define COL_GRID  RGB565C( 28,  40,  58)
#define COL_DIM   RGB565C(110, 116, 135)
#define COL_TXT   RGB565C(120, 255, 120)
#define COL_CRED  RGB565C(255, 200,  60)
#define COL_WARN  RGB565C(255, 120,  70)

/* Row model. */
typedef enum { RK_TEXT = 0, RK_MOUNT, RK_RACK, RK_CARGO, RK_EQ } RowKind;
typedef struct {
    uint8_t kind, index;
    char text[30];
    uint16_t color;
    int8_t icon;          /* weapon type, -1 none */
} Row;

#define MAX_ROWS 44
static Row s_rows[MAX_ROWS];
static int s_n_rows;
static int s_cursor;      /* row index (selectable rows only) */
static int s_scroll;
static int s_detail;      /* 0 list, 1 weapon, 2 good */
static CraftRawButtons s_prev;

static CraftRawButtons buttons_all_held(void) {
    CraftRawButtons b;
    b.up = b.down = b.left = b.right = true;
    b.a = b.b = b.lb = b.rb = b.menu = true;
    return b;
}

static void row(RowKind k, int idx, uint16_t color, int icon,
                const char *fmt, ...) {
    if (s_n_rows >= MAX_ROWS) return;
    Row *r = &s_rows[s_n_rows++];
    r->kind = (uint8_t)k;
    r->index = (uint8_t)idx;
    r->color = color;
    r->icon = (int8_t)icon;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->text, sizeof r->text, fmt, ap);
    va_end(ap);
}

static const char *k_qual_tag[5] = { "SLV", "STD", "RNF", "MIL", "PRO" };
static const char *k_skill_names[4] = {
    "GUNNERY", "TRADING", "TECH", "PILOTING",
};

static void build_rows(void) {
    const HullDef *h = &k_hulls[g_player.hull_id];
    s_n_rows = 0;
    row(RK_TEXT, 0, COL_HDR, -1, "%s", h->name);
    row(RK_TEXT, 0, COL_DIM, -1, "SPD %d CRG %d", (int)h->max_speed,
        h->cargo);
    row(RK_TEXT, 0, COL_HDR, -1, "MOUNTS:");
    for (int i = 0; i < h->n_slots; i++) {
        const WeaponInst *m = &g_player.mounts[i];
        /* Z# = the SLOT's capacity (what it can take), matching the
         * Z-size language used in shops and detail sheets. */
        if (m->in_use)
            row(RK_MOUNT, i,
                m->integrity < 50 ? COL_WARN : COL_DIM, m->type,
                "   Z%d %s %s %d%%", h->slot_size[i],
                k_weapons[m->type].name, k_qual_tag[m->quality],
                m->integrity);
        else
            row(RK_TEXT, 0, COL_DIM, -1, "Z%d EMPTY", h->slot_size[i]);
    }
    for (int i = 0; i < 2; i++) {
        const WeaponInst *e = i ? &g_player.armor_eq : &g_player.shield_eq;
        if (e->in_use)
            row(RK_EQ, i, e->integrity < 50 ? COL_WARN : COL_DIM, e->type,
                "   %s Z%d %s %d%%", item_name(e->type), e->tier,
                k_qual_tag[e->quality], e->integrity);
        else
            row(RK_TEXT, 0, COL_WARN, -1, "%s ----",
                item_name(WPN_COUNT + i));
    }
    {
        int used = 0;
        for (int i = 0; i < MAX_SALVAGE; i++)
            if (g_player.salvage[i].in_use) used++;
        row(RK_TEXT, 0, COL_HDR, -1, "RACK %d/%d:", used, player_rack_cap());
    }
    int any = 0;
    for (int i = 0; i < MAX_SALVAGE; i++) {
        const WeaponInst *m = &g_player.salvage[i];
        if (!m->in_use) continue;
        any = 1;
        row(RK_RACK, i, COL_DIM, m->type, "   %s %s %d%%",
            item_name(m->type), k_qual_tag[m->quality], m->integrity);
    }
    if (!any) row(RK_TEXT, 0, COL_DIM, -1, "(EMPTY)");
    row(RK_TEXT, 0, COL_HDR, -1, "CARGO %d/%d:", player_cargo_total(),
        player_cargo_cap());
    any = 0;
    for (int i = 0; i < N_GOODS; i++) {
        if (!g_player.cargo[i]) continue;
        any = 1;
        row(RK_CARGO, i, COL_DIM, -1, "%dX %s", g_player.cargo[i],
            k_goods[i].name);
    }
    if (!any) row(RK_TEXT, 0, COL_DIM, -1, "(EMPTY)");
    row(RK_TEXT, 0, COL_HDR, -1, "SKILLS:");
    uint16_t xs[4] = { g_player.xp_gunnery, g_player.xp_trading,
                       g_player.xp_tech, g_player.xp_piloting };
    for (int i = 0; i < 4; i++)
        row(RK_TEXT, 0, COL_TXT, -1, "%s LV%d", k_skill_names[i],
            skill_level(xs[i]));
    row(RK_TEXT, 0, COL_DIM, -1, "FUEL %d.%d/%d LY", (int)g_player.fuel,
        ((int)(g_player.fuel * 10)) % 10, (int)g_player.fuel_max);
    row(RK_TEXT, 0, COL_CRED, -1, "%dCR", g_player.credits);
}

static bool selectable(int r) {
    return r >= 0 && r < s_n_rows && s_rows[r].kind != RK_TEXT;
}
static int next_sel(int from, int dir) {
    for (int r = from + dir; r >= 0 && r < s_n_rows; r += dir)
        if (selectable(r)) return r;
    return from;
}

void status_open(void) {
    s_scroll = 0;
    s_detail = 0;
    s_prev = buttons_all_held();   /* debounce */
    build_rows();
    s_cursor = next_sel(-1, 1);
    if (!selectable(s_cursor)) s_cursor = -1;
}

bool status_tick(const CraftRawButtons *btn, float dt) {
    (void)dt;
    bool up = btn->up && !s_prev.up;
    bool down = btn->down && !s_prev.down;
    bool a = btn->a && !s_prev.a;
    bool close_btn = (btn->menu && !s_prev.menu) || (btn->b && !s_prev.b);
    s_prev = *btn;

    if (s_detail) {
        if (a || close_btn) s_detail = 0;
        return false;
    }

    build_rows();
    if (up && s_cursor >= 0) s_cursor = next_sel(s_cursor, -1);
    if (down && s_cursor >= 0) s_cursor = next_sel(s_cursor, 1);
    if (s_cursor >= 0) {
        if (s_cursor < s_scroll) s_scroll = s_cursor;
        if (s_cursor > s_scroll + 11) s_scroll = s_cursor - 11;
    }
    if (a && selectable(s_cursor))
        s_detail = (s_rows[s_cursor].kind == RK_CARGO) ? 2 : 1;
    return close_btn;
}

void status_draw(uint16_t *fb) {
    if (s_detail && selectable(s_cursor)) {
        const Row *r = &s_rows[s_cursor];
        if (r->kind == RK_CARGO) {
            detail_draw_good(fb, r->index, g_player.cargo[r->index],
                             "A/B:BACK");
        } else {
            const WeaponInst *wi = (r->kind == RK_MOUNT)
                                       ? &g_player.mounts[r->index]
                                   : (r->kind == RK_EQ)
                                       ? (r->index ? &g_player.armor_eq
                                                   : &g_player.shield_eq)
                                       : &g_player.salvage[r->index];
            int v = (int)(weapon_price(wi->type, wi->quality) *
                          (0.35f + 0.30f * wi->integrity * 0.01f));
            detail_draw_weapon(fb, wi, NULL, v, "VALUE", "A/B:BACK");
        }
        return;
    }

    /* The rendered ship becomes a dimmed backdrop (50%) — the whole
     * screen stays free for the sheet. */
    for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++)
        fb[i] = (uint16_t)((fb[i] >> 1) & 0x7BEF);

    char buf[24];
    craft_font_draw(fb, "SHIP STATUS", 2, 2, COL_HDR);
    snprintf(buf, sizeof buf, "%dCR", g_player.credits);
    craft_font_draw(fb, buf, 128 - craft_font_width(buf) - 2, 2, COL_CRED);
    for (int x = 0; x < 128; x++) fb[9 * ELITE_FB_W + x] = COL_GRID;

    build_rows();
    int y = 13;
    for (int r = s_scroll; r < s_n_rows && y < 117; r++, y += 8) {
        const Row *rw = &s_rows[r];
        /* Don't draw under the preview window. */
        int x0 = 2;
        if (rw->icon >= 0 && y >= 11) icon_weapon(fb, x0, y - 1, rw->icon);
        if (r == s_cursor && selectable(r))
            craft_font_draw(fb, ">", 0, y, COL_TXT);
        craft_font_draw(fb, rw->text, x0 + 2, y,
                        (r == s_cursor && selectable(r)) ? COL_TXT
                                                         : rw->color);
    }

    for (int x = 0; x < 128; x++) fb[118 * ELITE_FB_W + x] = COL_GRID;
    craft_font_draw(fb, "A:DETAILS B:CLOSE", 2, 121, COL_DIM);
}
