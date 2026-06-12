/*
 * ThumbyElite — event modal (see ui_event.h).
 *
 * Layout: title bar, 34px portrait (seeded — same NPC, same face), body
 * text wrapped beside/below it, choice list anchored to the bottom.
 * Two phases: choosing, then the aftermath text with A to continue.
 * There is no back-out — a hail must be answered (FTL rules).
 */
#include "ui_event.h"
#include "r3d_face.h"
#include "craft_font.h"
#include "elite_types.h"
#include "elite_player.h"
#include "elite_platform.h"
#include <stdio.h>
#include <string.h>

#define COL_HDR   RGB565C(200, 210, 225)
#define COL_BODY  RGB565C(168, 178, 198)
#define COL_TXT   RGB565C(120, 255, 120)
#define COL_DIM   RGB565C(96, 102, 122)
#define COL_CRED  RGB565C(255, 200, 60)
#define COL_GRID  RGB565C(28, 40, 58)

#define PORTRAIT 34

static const Event *s_ev;
static char s_body[300];
static char s_result[300];
static int  s_cursor;
static int  s_phase;          /* 0 choosing, 1 aftermath */
static CraftRawButtons s_prev;

static bool enabled(int i) { return events_choice_enabled(s_ev, i); }

static int first_enabled(void) {
    for (int i = 0; i < s_ev->n_choices; i++)
        if (enabled(i)) return i;
    return 0;
}

void ui_event_open(const Event *ev) {
    s_ev = ev;
    events_expand(ev->body, s_body, sizeof s_body);
    s_result[0] = 0;
    s_cursor = first_enabled();
    s_phase = 0;
    memset(&s_prev, 1, sizeof s_prev);     /* debounce the opening press */
}

bool ui_event_tick(const CraftRawButtons *btn, float dt) {
    (void)dt;
    bool up = btn->up && !s_prev.up;
    bool down = btn->down && !s_prev.down;
    bool a = btn->a && !s_prev.a;
    bool any_close = a || (btn->b && !s_prev.b) || (btn->menu && !s_prev.menu);
    s_prev = *btn;
    if (!s_ev) return true;

    if (s_phase == 1) {
        if (any_close) { s_ev = NULL; return true; }
        return false;
    }
    if (up)
        for (int i = s_cursor - 1; i >= 0; i--)
            if (enabled(i)) { s_cursor = i; break; }
    if (down)
        for (int i = s_cursor + 1; i < s_ev->n_choices; i++)
            if (enabled(i)) { s_cursor = i; break; }
    if (a && enabled(s_cursor)) {
        int t = events_run_choice(s_ev, s_cursor);
        const char *raw = (t >= 0) ? s_ev->texts[t] : "...";
        events_expand(raw, s_result, sizeof s_result);
        s_phase = 1;
        plat_rumble(0.2f, 0.06f);
    }
    return false;
}

/* Word-wrapped 1x text in [x0,x1]; returns the next free y. */
static int draw_wrapped(uint16_t *fb, const char *text, int x0, int x1,
                        int y, uint16_t col) {
    int maxc = (x1 - x0) / CRAFT_FONT_CELL_W;
    if (maxc < 4) maxc = 4;
    const char *p = text;
    char line[34];
    while (*p && y < 120) {
        int n = 0, last_sp = -1;
        while (p[n] && n < maxc && (int)(n) < (int)sizeof line - 1) {
            if (p[n] == ' ') last_sp = n;
            n++;
        }
        if (p[n] && last_sp > 0) n = last_sp;
        memcpy(line, p, n);
        line[n] = 0;
        craft_font_draw(fb, line, x0, y, col);
        y += 7;
        p += n;
        while (*p == ' ') p++;
    }
    return y;
}

void ui_event_draw(uint16_t *fb) {
    if (!s_ev) return;

    /* dim the docked scene behind us */
#ifdef ELITE_OVERLAY_SPLIT
    for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++)
        if (fb[i] == ELITE_KEY_T) fb[i] = ELITE_KEY_DIM;
#else
    for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++)
        fb[i] = (uint16_t)((fb[i] >> 1) & 0x7BEF);
#endif

    /* title */
    int tw = craft_font_width(s_ev->title);
    craft_font_draw(fb, s_ev->title, (128 - tw) / 2, 3, COL_HDR);
    for (int x = 2; x < 126; x++) fb[10 * ELITE_FB_W + x] = COL_GRID;

    /* portrait + body */
    int ty = 14;
    bool face = s_ev->npc_kind != NK_NONE;
    if (face)
        face_draw(fb, 4, ty, PORTRAIT, events_npc_seed(), s_ev->npc_kind);
    const char *text = s_phase ? s_result : s_body;
    uint16_t bcol = s_phase ? COL_TXT : COL_BODY;
    if (face) {
        /* beside the portrait, then full width below it */
        int y = ty;
        int maxc = (124 - 42) / CRAFT_FONT_CELL_W;
        const char *p = text;
        char line[34];
        while (*p && y < ty + PORTRAIT - 5) {
            int n = 0, last_sp = -1;
            while (p[n] && n < maxc && n < (int)sizeof line - 1) {
                if (p[n] == ' ') last_sp = n;
                n++;
            }
            if (p[n] && last_sp > 0) n = last_sp;
            memcpy(line, p, n); line[n] = 0;
            craft_font_draw(fb, line, 42, y, bcol);
            y += 7;
            p += n;
            while (*p == ' ') p++;
        }
        draw_wrapped(fb, p, 4, 124, ty + PORTRAIT + 3, bcol);
    } else {
        draw_wrapped(fb, text, 4, 124, ty + 2, bcol);
    }

    /* footer */
    for (int x = 2; x < 126; x++) fb[118 * ELITE_FB_W + x] = COL_GRID;

    if (s_phase == 1) {
        char h[20];
        snprintf(h, sizeof h, "%s:CONTINUE", plat_menu_btn(MB_A));
        craft_font_draw(fb, h, 4, 121, COL_DIM);
        return;
    }

    /* choices, bottom-anchored */
    int y0 = 116 - 8 * s_ev->n_choices;
    for (int i = 0; i < s_ev->n_choices; i++) {
        const Choice *ch = &s_ev->choices[i];
        int y = y0 + 8 * i;
        bool en = enabled(i);
        uint16_t col = !en ? COL_DIM : (i == s_cursor ? COL_TXT : COL_BODY);
        if (i == s_cursor && en) craft_font_draw(fb, ">", 4, y, COL_TXT);
        int x = craft_font_draw(fb, ch->label, 10, y, col);
        if (ch->cost > 0) {
            char cc[12];
            snprintf(cc, sizeof cc, " %dCR", ch->cost);
            craft_font_draw(fb, cc, x, y, en ? COL_CRED : COL_DIM);
        }
    }
    {
        char h[16];
        snprintf(h, sizeof h, "%s:SELECT", plat_menu_btn(MB_A));
        craft_font_draw(fb, h, 4, 121, COL_DIM);
    }
}
