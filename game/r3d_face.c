/*
 * ThumbyElite — parametric NPC portraits (see r3d_face.h).
 *
 * Everything is drawn in box-relative pixels from float fractions of the
 * box size, so one code path serves any portrait size. Symmetry is what
 * makes a few rectangles read as a face — eyes, brows and ears mirror
 * around the centre line; asymmetry is reserved for markings (scars).
 */
#include "r3d_face.h"
#include "elite_types.h"
#include "events.h"
#include <math.h>

/* --- seed stream -------------------------------------------------------- */
typedef struct { uint32_t s; } FRng;
static uint32_t fr_u32(FRng *r) {
    r->s ^= r->s << 13; r->s ^= r->s >> 17; r->s ^= r->s << 5;
    return r->s;
}
static int fr_n(FRng *r, int n) { return (int)(fr_u32(r) % (uint32_t)n); }
static int fr_pct(FRng *r, int pct) { return fr_n(r, 100) < pct; }

/* --- tiny raster -------------------------------------------------------- */
typedef struct { uint16_t *fb; int x, y, s; } FCtx;

static void fpx(FCtx *c, int x, int y, uint16_t col) {
    if (x < 0 || y < 0 || x >= c->s || y >= c->s) return;
    c->fb[(c->y + y) * ELITE_FB_W + c->x + x] = col;
}
static void fspan(FCtx *c, int x0, int x1, int y, uint16_t col) {
    for (int x = x0; x <= x1; x++) fpx(c, x, y, col);
}
static void frect(FCtx *c, int x0, int y0, int w, int h, uint16_t col) {
    for (int y = y0; y < y0 + h; y++) fspan(c, x0, x0 + w - 1, y, col);
}

static uint16_t shade(uint16_t col, int pct) {   /* pct 100 = unchanged */
    int r = ((col >> 11) & 31) * pct / 100;
    int g = ((col >> 5) & 63) * pct / 100;
    int b = (col & 31) * pct / 100;
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* --- palettes ------------------------------------------------------------*/
static const uint16_t k_skin[6] = {
    RGB565C(232, 190, 160), RGB565C(208, 158, 120), RGB565C(176, 126, 90),
    RGB565C(130, 90, 62),   RGB565C(96, 64, 46),    RGB565C(170, 150, 120),
};
static const uint16_t k_hair[6] = {
    RGB565C(34, 28, 24),  RGB565C(86, 58, 32),  RGB565C(150, 110, 50),
    RGB565C(190, 186, 178), RGB565C(120, 40, 28), RGB565C(60, 64, 72),
};
static const uint16_t k_iris[6] = {
    RGB565C(70, 48, 28),  RGB565C(60, 100, 160), RGB565C(70, 120, 70),
    RGB565C(150, 110, 40), RGB565C(110, 70, 140), RGB565C(70, 110, 110),
};
static const uint16_t k_suit[5] = {
    RGB565C(40, 52, 78),  RGB565C(70, 60, 48),  RGB565C(58, 70, 58),
    RGB565C(120, 70, 30), RGB565C(56, 48, 64),
};
static const uint16_t COL_SCLERA = RGB565C(225, 222, 205);
static const uint16_t COL_SYNTH  = RGB565C(120, 130, 145);
static const uint16_t COL_GLOW   = RGB565C(120, 230, 255);

/* Proposal-look switch (style lab — sheets only). */
static int s_style;
void face_set_style(int s) { s_style = s; }

#ifdef ELITE_STYLE_LAB
/* PROPOSAL palettes: species skins + louder hair. */
static const uint16_t k_skin_avian[4] = {
    RGB565C(205, 170, 110), RGB565C(150, 170, 180), RGB565C(225, 215, 200),
    RGB565C(160, 120, 90),
};
static const uint16_t k_skin_saur[4] = {
    RGB565C(110, 150, 95), RGB565C(85, 130, 120), RGB565C(150, 150, 80),
    RGB565C(95, 110, 135),
};
static const uint16_t k_hair_punk[3] = {
    RGB565C(60, 200, 190), RGB565C(220, 80, 160), RGB565C(235, 235, 235),
};
#endif

void face_draw(uint16_t *fb, int bx, int by, int size, uint32_t seed,
               int kind) {
    FCtx c = { fb, bx, by, size };
    FRng r = { seed ? seed : 1u };
    float S = (float)size;

    int synth = (kind != NK_MYSTIC) && fr_pct(&r, 10);
    /* species: 0 human, 1 synth, 2 avian, 3 saurian, 4 heavyworlder */
    int species = synth ? 1 : 0;
#ifdef ELITE_STYLE_LAB
    if (s_style == 1 && kind != NK_MYSTIC) {
        int sr = fr_n(&r, 100);
        species = (sr < 54) ? 0 : (sr < 64) ? 1 : (sr < 77) ? 2
                : (sr < 89) ? 3 : 4;
        synth = (species == 1);
    }
#endif
    uint16_t skin = synth ? COL_SYNTH : k_skin[fr_n(&r, 6)];
    if (kind == NK_MYSTIC) skin = shade(skin, 88);          /* pallid */
    if (kind == NK_PIRATE) skin = shade(skin, 92);
    uint16_t skin_d = shade(skin, 72), skin_l = shade(skin, 116);
    uint16_t hair = k_hair[fr_n(&r, 6)];
    uint16_t iris = synth ? COL_GLOW : k_iris[fr_n(&r, 6)];
#ifdef ELITE_STYLE_LAB
    if (s_style == 1) {
        if (species == 2) { skin = k_skin_avian[fr_n(&r, 4)];
                            iris = RGB565C(20, 16, 12); }
        if (species == 3) { skin = k_skin_saur[fr_n(&r, 4)];
                            iris = RGB565C(230, 190, 60); }
        if (species == 4) skin = shade(k_skin[fr_n(&r, 6)], 95);
        if (species <= 1 && fr_pct(&r, 14))
            hair = k_hair_punk[fr_n(&r, 3)];
        skin_d = shade(skin, 72);
        skin_l = shade(skin, 116);
    }
#endif
    uint16_t suit = k_suit[kind == NK_OFFICIAL ? 0 : fr_n(&r, 5)];

    /* panel backdrop: deep blue-grey, faint floor glow */
    for (int y = 0; y < size; y++) {
        uint16_t bg = shade(RGB565C(16, 22, 36), 70 + 50 * y / size);
        fspan(&c, 0, size - 1, y, bg);
    }

    /* geometry (box fractions) */
    int cx = size / 2;
    int cy = (int)(S * 0.44f);
    int rx = (int)(S * (0.24f + 0.05f * (float)fr_n(&r, 4) / 3.0f));
    int ry = (int)(S * (0.30f + 0.05f * (float)fr_n(&r, 4) / 3.0f));
    float jaw = 0.10f + 0.35f * (float)fr_n(&r, 4) / 3.0f;
#ifdef ELITE_STYLE_LAB
    if (s_style == 1) {
        if (species == 2) { rx = (int)(rx * 0.85f); jaw = 0.45f; }
        if (species == 4) { rx = (int)(rx * 1.22f); jaw = 0.05f;
                            ry = (int)(ry * 0.95f); }
    }
#endif

    /* shoulders + neck under the head */
    int sh_y = cy + ry - (int)(S * 0.04f);
    for (int y = sh_y; y < size; y++) {
        int w = (int)(S * 0.18f) + (y - sh_y) * 2;
        fspan(&c, cx - w, cx + w, y, suit);
        fpx(&c, cx - w, y, shade(suit, 130));
    }
    if (kind == NK_OFFICIAL)                       /* rank stripe */
        fspan(&c, cx - (int)(S * 0.2f), cx + (int)(S * 0.2f),
              sh_y + 2, RGB565C(200, 170, 60));
    frect(&c, cx - (int)(S * 0.09f), cy + ry - (int)(S * 0.10f),
          (int)(S * 0.18f) + 1, (int)(S * 0.14f), skin_d);   /* neck */

    /* head: per-row width profile — oval with a shaped chin */
    for (int y = cy - ry; y <= cy + ry; y++) {
        float ny = (float)(y - cy) / (float)ry;
        float w = (float)rx * sqrtf(1.0f - ny * ny);
        if (ny > 0.15f) w *= 1.0f - jaw * (ny - 0.15f) / 0.85f;
        int wi = (int)w;
        if (wi < 1) continue;
        fspan(&c, cx - wi, cx + wi, y, skin);
        /* simple right-side shade + left rim light */
        for (int x = cx + wi - wi / 4; x <= cx + wi; x++)
            fpx(&c, x, y, skin_d);
        fpx(&c, cx - wi, y, skin_l);
    }
    if (synth) {                                   /* faceplate seam */
        for (int y = cy - ry + 2; y <= cy + ry - 2; y += 2)
            fpx(&c, cx, y, shade(skin, 60));
    }

    /* ears */
    int ey = cy - (int)(S * 0.02f);
    int hood = (kind == NK_MYSTIC) ? fr_pct(&r, 80) : fr_pct(&r, 6);
    int helmet = 0, breather = 0;
#ifdef ELITE_STYLE_LAB
    int mohawk = 0, longhair = 0;
    if (s_style == 1 && !hood && (species <= 1 || species == 4)) {
        int g = fr_n(&r, 100);
        helmet   = g < 14;
        breather = g >= 14 && g < 24;
        mohawk   = g >= 24 && g < 33;
        longhair = g >= 33 && g < 46;
    }
#endif
    if (!hood && !synth && !helmet && species < 2) {
        frect(&c, cx - rx - 1, ey - 1, 2, (int)(S * 0.10f) + 1, skin_d);
        frect(&c, cx + rx - 1, ey - 1, 2, (int)(S * 0.10f) + 1, skin_d);
    }

    /* hair / headgear base (drawn over the skull, under the eyes) */
    int bandana = (kind == NK_PIRATE) && fr_pct(&r, 55);
    int cap = (kind == NK_OFFICIAL || kind == NK_DOCKHAND) && fr_pct(&r, 55);
    int bald = synth || fr_pct(&r, 18);
    int hl = (int)(S * (0.08f + 0.07f * (float)fr_n(&r, 3) / 2.0f));
#ifdef ELITE_STYLE_LAB
    if (s_style == 1 && (helmet || mohawk || longhair || species >= 2)) {
        if (helmet) {
            /* full helm: dome to below the ears + bright rim; the visor
             * band lands at the eye stage. */
            uint16_t hm = (kind == NK_OFFICIAL) ? RGB565C(46, 58, 88)
                                                : RGB565C(105, 112, 122);
            int hb = cy + (int)(S * 0.06f);
            for (int y = cy - ry - 2; y <= hb; y++) {
                float nyh = (float)(y - cy) / (float)(ry + 2);
                if (nyh < -1.0f) nyh = -1.0f;
                int wi = (int)((float)(rx + 2) *
                               sqrtf(1.0f - nyh * nyh)) + 1;
                fspan(&c, cx - wi, cx + wi, y,
                      y > hb - 2 ? shade(hm, 140) : hm);
            }
            fpx(&c, cx + rx - 1, cy - ry + 2, RGB565C(255, 90, 60));
        } else if (mohawk) {
            int mh = (int)(S * 0.12f);
            for (int y = cy - ry - mh; y < cy - ry + (int)(S * 0.10f); y++)
                fspan(&c, cx - 1, cx + 1, y, hair);
        } else if (longhair) {
            for (int y = cy - ry - 1; y < cy - ry + hl; y++) {
                float nyh = (float)(y - cy) / (float)ry;
                if (nyh < -1.0f) nyh = -1.0f;
                int wi = (int)((float)rx * sqrtf(1.0f - nyh * nyh)) + 1;
                fspan(&c, cx - wi, cx + wi, y, hair);
            }
            int hb2 = cy + (int)(ry * 0.7f);
            for (int y = cy - ry + hl; y <= hb2; y++) {
                float nyh = (float)(y - cy) / (float)ry;
                if (nyh > 0.99f) nyh = 0.99f;
                int wi = (int)((float)rx * sqrtf(1.0f - nyh * nyh));
                frect(&c, cx - wi - 3, y, 3, 1, hair);
                frect(&c, cx + wi, y, 3, 1, hair);
            }
        } else if (species == 2) {
            /* feather crest: spikes off the crown */
            uint16_t fc = shade(skin, 120);
            for (int k = -2; k <= 2; k++) {
                int hgt = 3 + ((k & 1) ? 1 : 3);
                frect(&c, cx + k * 3 - 1, cy - ry - hgt, 2, hgt + 2, fc);
            }
        } else if (species == 3) {
            /* saurian crown ridges */
            for (int k = -2; k <= 2; k++)
                frect(&c, cx + k * 3 - 1, cy - ry - 1, 2, 3, skin_d);
        }
    } else
#endif
    if (bandana) {
        uint16_t bc = RGB565C(140, 40, 40);
        for (int y = cy - ry; y < cy - ry + (int)(S * 0.14f); y++) {
            float ny = (float)(y - cy) / (float)ry;
            int wi = (int)((float)rx * sqrtf(1.0f - ny * ny));
            fspan(&c, cx - wi, cx + wi, y, bc);
        }
        frect(&c, cx + rx - 2, cy - ry + 2, 3, 4, shade(RGB565C(140,40,40), 70));
    } else if (cap) {
        uint16_t cc = (kind == NK_OFFICIAL) ? RGB565C(36, 46, 70)
                                            : RGB565C(150, 110, 40);
        for (int y = cy - ry - 1; y < cy - ry + (int)(S * 0.12f); y++) {
            float ny = (float)(y - cy) / (float)ry;
            if (ny < -1.0f) ny = -1.0f;
            int wi = (int)((float)rx * sqrtf(1.0f - ny * ny)) + 1;
            fspan(&c, cx - wi, cx + wi, y, cc);
        }
        fspan(&c, cx - rx - 1, cx + rx + 1,
              cy - ry + (int)(S * 0.12f), shade(RGB565C(36,46,70), 60));
    } else if (!bald) {
        /* hairline cap + optional side hair */
        for (int y = cy - ry - 1; y < cy - ry + hl; y++) {
            float ny = (float)(y - cy) / (float)ry;
            if (ny < -1.0f) ny = -1.0f;
            int wi = (int)((float)rx * sqrtf(1.0f - ny * ny)) + 1;
            fspan(&c, cx - wi, cx + wi, y, hair);
        }
        if (fr_pct(&r, 50)) {                      /* side hair hugs the head */
            int sl = (int)(S * 0.16f);
            for (int y = cy - ry + hl; y < cy - ry + hl + sl; y++) {
                float ny = (float)(y - cy) / (float)ry;
                if (ny > 0.99f) break;
                int wi = (int)((float)rx * sqrtf(1.0f - ny * ny));
                frect(&c, cx - wi - 1, y, 2, 1, hair);
                frect(&c, cx + wi, y, 2, 1, hair);
            }
        }
    }

    /* eyes — mirrored; the single most load-bearing feature */
    int edx = (int)(S * (0.10f + 0.04f * (float)fr_n(&r, 3) / 2.0f));
    int ew = (int)(S * 0.09f); if (ew < 2) ew = 2;
    int eh = (int)(S * 0.05f); if (eh < 1) eh = 1;
    int visor = ((kind == NK_OFFICIAL) && fr_pct(&r, 45)) || helmet;
    int slit = 0;
#ifdef ELITE_STYLE_LAB
    if (s_style == 1) {
        slit = (species == 3);
        if (species == 2) { eh = ew * 2 / 3; edx = (int)(edx * 1.25f); }
    }
#endif
    if (!visor) {
        uint16_t sclera = hood ? shade(COL_SCLERA, 55) : COL_SCLERA;
        if (slit) sclera = RGB565C(215, 185, 70);
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            int ex = cx + sgn * edx - ew / 2;
            frect(&c, ex, ey, ew, eh + 1, sclera);
            if (slit) {
                frect(&c, ex + ew / 2, ey, 1, eh + 1, RGB565C(8, 8, 10));
            } else {
                frect(&c, ex + ew / 2 - 1, ey, 2, eh + 1, iris);
                fpx(&c, ex + ew / 2 - 1 + fr_n(&r, 2), ey + eh / 2,
                    RGB565C(8, 8, 10));
            }
        }
        /* brows: tilt 0 neutral, 1 angry-in, 2 raised */
        int tilt = (kind == NK_PIRATE) ? 1 : fr_n(&r, 3);
        uint16_t bcol = synth ? shade(COL_SYNTH, 55) : shade(hair, 70);
#ifdef ELITE_STYLE_LAB
        if (s_style == 1 && species == 4) {
            /* heavyworlder: one continuous brow shelf */
            frect(&c, cx - edx - ew / 2, ey - eh - 2, edx * 2 + ew, 2,
                  skin_d);
        } else if (s_style == 1 && species == 3) {
            /* saurian: bony ridge over each eye */
            for (int sgn = -1; sgn <= 1; sgn += 2)
                frect(&c, cx + sgn * edx - ew / 2, ey - eh - 2, ew, 1,
                      skin_d);
        } else
#endif
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            int ex = cx + sgn * edx - ew / 2;
            int yo = ey - eh - 1;
            for (int i = 0; i < ew; i++) {
                int dy = 0;
                if (tilt == 1) dy = (sgn < 0 ? i : ew - 1 - i) / 3;
                if (tilt == 2) dy = -((sgn < 0 ? ew - 1 - i : i) / 4);
                fpx(&c, ex + i, yo + dy, bcol);
            }
        }
    } else {
        /* mirrored visor band with a glint */
        frect(&c, cx - rx + 1, ey - 1, 2 * rx - 1, eh + 3,
              RGB565C(20, 26, 34));
        fspan(&c, cx - rx + 2, cx - rx / 3, ey, RGB565C(90, 140, 190));
    }

    /* nose + mouth */
    int nly = ey + (int)(S * 0.12f);
    int my = nly + (int)(S * 0.08f);
    int mw = (int)(S * 0.08f) + fr_n(&r, 3);
    int mood = fr_n(&r, 3);                 /* 0 flat 1 frown 2 smile */
    uint16_t mcol = shade(skin, 48);
#ifdef ELITE_STYLE_LAB
    if (s_style == 1 && species == 2) {
        /* beak: a tapering amber wedge owns the lower face */
        uint16_t bk = RGB565C(212, 156, 58), bkd = shade(bk, 70);
        for (int y = ey + 1; y <= my + 2; y++) {
            float f = (float)(y - ey) / (float)(my + 2 - ey);
            int wi = (int)((1.0f - f * 0.8f) * S * 0.10f);
            if (wi < 1) wi = 1;
            fspan(&c, cx - wi, cx + wi, y, bk);
        }
        fspan(&c, cx - (int)(S * 0.05f), cx + (int)(S * 0.05f),
              nly + 1, bkd);                /* the bill split */
    } else
#endif
    {
        frect(&c, cx - 1, ey + 2, 1, nly - ey - 2, skin_d);
#ifdef ELITE_STYLE_LAB
        if (s_style == 1 && species == 3) {
            fpx(&c, cx - 1, nly, RGB565C(8, 8, 10));   /* nostril pits */
            fpx(&c, cx + 1, nly, RGB565C(8, 8, 10));
            mw += 2;                                    /* wide thin mouth */
        } else
#endif
        fspan(&c, cx - 1, cx + 1, nly, skin_d);
        fspan(&c, cx - mw, cx + mw, my, mcol);
        if (mood == 1) { fpx(&c, cx - mw, my - 1, mcol); fpx(&c, cx + mw, my - 1, mcol); }
        if (mood == 2) { fpx(&c, cx - mw, my + 1, mcol); fpx(&c, cx + mw, my + 1, mcol); }
    }
#ifdef ELITE_STYLE_LAB
    if (s_style == 1 && breather) {
        /* sealed breather mask over the mouth, twin feed lines */
        uint16_t mk = RGB565C(58, 66, 80);
        frect(&c, cx - mw - 1, my - 3, 2 * mw + 3, 6, mk);
        frect(&c, cx - mw - 1, my - 3, 2 * mw + 3, 1, shade(mk, 140));
        fpx(&c, cx - mw - 2, my + 4, shade(mk, 70));
        fpx(&c, cx + mw + 2, my + 4, shade(mk, 70));
        fpx(&c, cx, my, RGB565C(120, 230, 255));        /* status led */
    }
#endif

    /* stubble / beard */
    if (!synth && fr_pct(&r, kind == NK_DOCKHAND ? 55 : 30) && species < 2
        && !breather) {
        uint16_t st = shade(hair, 60);
        for (int y = my - 2; y < cy + ry; y++) {
            float ny = (float)(y - cy) / (float)ry;
            if (ny > 1.0f) break;
            float w = (float)rx * sqrtf(1.0f - ny * ny);
            if (ny > 0.15f) w *= 1.0f - jaw * (ny - 0.15f) / 0.85f;
            for (int x = cx - (int)w; x <= cx + (int)w; x += 2)
                if (((x ^ y) & 3) == 0 && !(x > cx - mw && x < cx + mw &&
                                            y <= my + 1))
                    fpx(&c, x, y, st);
        }
    }

    /* markings: scar (asymmetric on purpose) */
    if (fr_pct(&r, kind == NK_PIRATE ? 70 : 12)) {
        int sx = cx + (fr_pct(&r, 50) ? -edx : edx);
        int sy0 = ey - eh - 2, sy1 = nly;
        for (int y = sy0; y <= sy1; y++)
            fpx(&c, sx + (y - sy0) / 3, y, shade(skin, 135));
    }

    /* hood overlay: frames the face last */
    if (hood) {
        uint16_t hc = RGB565C(44, 38, 58), hd = shade(RGB565C(44, 38, 58), 60);
        for (int y = 0; y < size; y++) {
            float ny = (float)(y - cy) / (float)(ry + 3);
            float w = (ny < 1.0f && ny > -1.0f)
                          ? (float)(rx + 2) * sqrtf(1.0f - ny * ny)
                          : (y > cy ? (float)(rx + 2) : 0);
            int wi = (int)w;
            if (y < cy - ry + (int)(S * 0.10f)) wi = -1;   /* cowl top */
            fspan(&c, 0, cx - wi - 1, y, y < cy ? hc : hd);
            fspan(&c, cx + wi + 1, size - 1, y, y < cy ? hc : hd);
        }
    }

    /* frame */
    fspan(&c, 0, size - 1, 0, RGB565C(70, 90, 120));
    fspan(&c, 0, size - 1, size - 1, RGB565C(70, 90, 120));
    for (int y = 0; y < size; y++) {
        fpx(&c, 0, y, RGB565C(70, 90, 120));
        fpx(&c, size - 1, y, RGB565C(70, 90, 120));
    }
}
