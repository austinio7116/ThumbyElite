/*
 * ThumbyElite — Android (SDL2) platform shell.
 *
 * Mirrors host/host_main.c: the game (../../../../game) is byte-identical
 * to the host and RP2350 device builds; only this platform layer differs.
 *
 * Two compile-time overrides (set in Android.mk) shape the port:
 *   R3D_SS=2            — the 3D world rasterises at 256x256 (4x the device
 *                         pixel count) for smooth edges.
 *   ELITE_OVERLAY_SPLIT — the 2D HUD/menus render into a separate 128-logical
 *                         key-colour layer; we composite it (pixel-doubled,
 *                         so text is a crisp 2x) over the 3D frame.
 *
 * Input: a fixed pixel-art thumbstick (lower-left) drives analog flight AND
 * digital menu navigation; pixel-art A/B (lower-right), LB/RB (top corners)
 * and MENU (top-centre) buttons. A connected game controller takes over
 * (left stick = fly, right stick X = roll, d-pad = menus, A/B/L1/R1/Start,
 * triggers fire) and the touch overlay fades out while it's in use.
 *
 * Ship meshes are pre-baked to const data (generated/meshes_gen.c) the same
 * way the host/device builds bake them. Saves live in app-private storage.
 */
#include "elite_types.h"
#include "elite_game.h"
#include "elite_audio.h"
#include "elite_input.h"
#include "elite_platform.h"
#include "craft_buttons.h"

#include <SDL.h>
#include <SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* ---- framebuffers --------------------------------------------------- */
static uint16_t g_fb3d[R3D_FB_W * R3D_FB_H];          /* physical 3D frame */
static uint16_t g_fbui[ELITE_FB_W * ELITE_FB_H];      /* logical overlay   */
static uint16_t g_comp[R3D_FB_W * R3D_FB_H];          /* composited        */

static SDL_Window       *win;
static SDL_Renderer     *ren;
static SDL_Texture      *tex;            /* the composited game frame */
static SDL_AudioDeviceID audio_dev;
static char              g_sav_path[1024];

/* ---- engine platform hooks (same contract as host) ------------------ */
/* 0 volume(0..20), 1 brightness, 2 gamepad sens(x0.1), 3 touch-stick sens. */
static int  s_settings[4] = { 10, 255, 10, 10 };
static const char *SETTINGS_FILES[4] =
    { "volume.dat", "brightness.dat", "sens_pad.dat", "sens_stick.dat" };

int plat_setting_get(int which) { return s_settings[which & 3]; }
void plat_setting_set(int which, int value) {
    which &= 3;
    s_settings[which] = value;
    if (which == 0) audio_set_master((float)value / 20.0f);
    /* brightness/sensitivity have no immediate side effect here; the touch +
     * gamepad paths read the sensitivity each frame. Persist so it survives
     * a relaunch. */
    SDL_RWops *f = SDL_RWFromFile(SETTINGS_FILES[which], "wb");
    if (f) { SDL_RWwrite(f, &value, sizeof value, 1); SDL_RWclose(f); }
}
static void settings_restore(void) {
    for (int i = 0; i < 4; i++) {
        SDL_RWops *f = SDL_RWFromFile(SETTINGS_FILES[i], "rb");
        if (f) { SDL_RWread(f, &s_settings[i], sizeof(int), 1); SDL_RWclose(f); }
    }
}

static SDL_GameController *s_pad;     /* set below; used for rumble */
void plat_rumble(float intensity, float seconds) {
    if (!s_pad) return;
    Uint16 v = (Uint16)(intensity * 0xFFFF);
    SDL_GameControllerRumble(s_pad, v, v, (Uint32)(seconds * 1000.0f));
}

/* ---- controller binding (read-only on Android) ---------------------- *
 * Android has a standard SDL gamepad mapping — nothing to rebind — so the
 * CONTROLLER SETUP screen shows it read-only. */
int  plat_ctrl_present(void)  { return s_pad ? 1 : 0; }
int  plat_ctrl_editable(void) { return 0; }
const char *plat_ctrl_device_name(void) {
    return s_pad ? SDL_GameControllerName(s_pad) : "NO PAD";
}
void plat_ctrl_axis_label(CtrlAxis ax, char *out, int cap) {
    static const char *n[CTRL_AX_N] = { "R-STK X", "L-STK Y", "L-STK X", "R-STK Y" };
    if (out && cap > 0) SDL_snprintf(out, cap, "%s",
        (ax >= 0 && ax < CTRL_AX_N) ? n[ax] : "?");
}
void plat_ctrl_btn_label(CtrlButton b, char *out, int cap) {
    const char *s;
    switch (b) {       /* Scheme A */
    case CTRL_BTN_FIRE: s = "RT"; break; case CTRL_BTN_FIRE2: s = "LT"; break;
    case CTRL_BTN_FIRE3: s = "RB"; break; case CTRL_BTN_CYCLE_WEAPON: s = "LB"; break;
    case CTRL_BTN_CYCLE_TARGET: s = "B"; break; case CTRL_BTN_ASSIST: s = "X"; break;
    case CTRL_BTN_BOOST: s = "A"; break; case CTRL_BTN_CHAFF: s = "BACK"; break;
    case CTRL_BTN_CLOAK: s = "L3"; break; case CTRL_BTN_DOCK: s = "LB+RB"; break;
    case CTRL_BTN_MENU: s = "START"; break;
    case CTRL_BTN_MENU_SELECT: s = "A"; break; case CTRL_BTN_MENU_BACK: s = "B"; break;
    case CTRL_BTN_MENU_INFO: s = "Y"; break;
    default: s = "—"; break;
    }
    if (out && cap > 0) SDL_snprintf(out, cap, "%s", s);
}
void plat_ctrl_capture_begin(int kind, int which) { (void)kind; (void)which; }
int  plat_ctrl_capture_poll(void) { return -1; }
void plat_ctrl_capture_cancel(void) {}
void plat_ctrl_axis_invert(CtrlAxis ax) { (void)ax; }
void plat_ctrl_clear(int kind, int which) { (void)kind; (void)which; }
void plat_ctrl_save(void) {}
void plat_ctrl_monitor(void) {}
const char *plat_ctrl_last_input(void) { return ""; }

int plat_save(const uint8_t *data, int len) {
    SDL_RWops *f = SDL_RWFromFile(g_sav_path, "wb");
    if (!f) { SDL_Log("[elite] save open failed"); return 0; }
    SDL_RWwrite(f, data, 1, (size_t)len);
    SDL_RWclose(f);
    SDL_Log("[elite] saved %d bytes", len);
    return 1;
}
int plat_load(uint8_t *data, int max_len) {
    SDL_RWops *f = SDL_RWFromFile(g_sav_path, "rb");
    if (!f) return 0;
    size_t n = SDL_RWread(f, data, 1, (size_t)max_len);
    SDL_RWclose(f);
    return (int)n;
}

/* ---- audio ---------------------------------------------------------- */
static void audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    audio_render((int16_t *)stream, len / (int)sizeof(int16_t));
}

/* ====================================================================== *
 *  Pixel-art on-screen controls
 *
 *  Each control is authored as a tiny low-res RGBA bitmap with hard
 *  (un-antialiased) pixels and uploaded once; at runtime we blit it scaled
 *  with NEAREST filtering, so it reads as crisp scaled pixel-art that
 *  matches the doubled HUD. Chunky 1-cell bevel: light top/left, dark
 *  bottom/right over a flat body, plus a white pixel glyph.
 * ====================================================================== */
enum { BTN_A, BTN_B, BTN_LB, BTN_RB, BTN_MENU, BTN_COUNT };

typedef struct { Uint8 r, g, b; } RGB;
static const RGB BTN_COL[BTN_COUNT] = {
    { 220,  72,  60 },   /* A   — fire (warm red)   */
    {  72, 150, 220 },   /* B   — secondary (blue)  */
    { 210, 176,  70 },   /* LB  — gold              */
    { 210, 176,  70 },   /* RB  — gold              */
    { 150, 158, 170 },   /* MENU— grey              */
};

#define CELL 16          /* low-res button is CELL x CELL "big pixels" */
static SDL_Texture *g_btn_tex[BTN_COUNT];
static SDL_Texture *g_stick_base, *g_stick_knob;

/* 5x7 pixel glyphs for A B L R (column-major bitfield, bit0 = top row). */
/* Columns left->right, bit0 = TOP row. */
static const uint8_t GLYPH_A[5] = {0x7E,0x09,0x09,0x09,0x7E};
static const uint8_t GLYPH_B[5] = {0x7F,0x49,0x49,0x49,0x36};
static const uint8_t GLYPH_L[5] = {0x7F,0x40,0x40,0x40,0x40};
static const uint8_t GLYPH_R[5] = {0x7F,0x09,0x19,0x29,0x46};

static void put(uint32_t *px, int W, int H, int x, int y, uint32_t c) {
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H)
        px[y * W + x] = c;
}
static uint32_t argb(Uint8 a, Uint8 r, Uint8 g, Uint8 b) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* Draw a 5x7 glyph centred in a CELL x CELL field. */
static void glyph(uint32_t *px, int W, int H, const uint8_t *g) {
    int ox = (CELL - 5) / 2, oy = (CELL - 7) / 2;
    for (int cx = 0; cx < 5; cx++)
        for (int ry = 0; ry < 7; ry++)
            if (g[cx] & (1 << ry))
                put(px, W, H, ox + cx, oy + ry, argb(255, 255, 255, 255));
}

/* "LB"/"RB" wide pill: two glyphs side by side in a CELL*1.6 x CELL field. */
static SDL_Texture *gen_shoulder(int which) {
    int W = (int)(CELL * 1.7f), H = CELL;
    uint32_t *px = calloc((size_t)W * H, 4);
    RGB c = BTN_COL[which];
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        bool edge = (x == 0 || y == 0 || x == W - 1 || y == H - 1);
        bool lt = (x == 0 || y == 0), db = (x == W - 1 || y == H - 1);
        uint32_t v = edge
            ? (lt ? argb(255, c.r + (255 - c.r) / 2, c.g + (255 - c.g) / 2,
                              c.b + (255 - c.b) / 2)
                  : argb(255, c.r / 2, c.g / 2, c.b / 2))
            : argb(255, c.r, c.g, c.b);
        (void)db;
        px[y * W + x] = v;
    }
    /* glyphs: L/R then B, slightly inset */
    const uint8_t *g1 = (which == BTN_LB) ? GLYPH_L : GLYPH_R;
    for (int cx = 0; cx < 5; cx++) for (int ry = 0; ry < 7; ry++) {
        if (g1[cx] & (1 << ry)) put(px, W, H, 3 + cx, (H - 7) / 2 + ry,
                                     argb(255, 255, 255, 255));
        if (GLYPH_B[cx] & (1 << ry)) put(px, W, H, 11 + cx, (H - 7) / 2 + ry,
                                     argb(255, 255, 255, 255));
    }
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC, W, H);
    SDL_UpdateTexture(t, NULL, px, W * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    free(px);
    return t;
}

/* Round-ish pixel-art button (octagon-clipped square) with bevel + glyph. */
static SDL_Texture *gen_round(int which) {
    int W = CELL, H = CELL;
    uint32_t *px = calloc((size_t)W * H, 4);
    RGB c = BTN_COL[which];
    int cut = CELL / 4;                      /* corner chamfer in cells */
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        /* octagon mask */
        if (x + y < cut) continue;
        if ((W - 1 - x) + y < cut) continue;
        if (x + (H - 1 - y) < cut) continue;
        if ((W - 1 - x) + (H - 1 - y) < cut) continue;
        bool lt = (x <= 0 || y <= 0 || x + y == cut || (W - 1 - x) + y == cut);
        bool db = (x >= W - 1 || y >= H - 1 ||
                   x + (H - 1 - y) == cut || (W - 1 - x) + (H - 1 - y) == cut);
        uint32_t v;
        if (lt)      v = argb(255, c.r + (255 - c.r) / 2, c.g + (255 - c.g) / 2,
                                   c.b + (255 - c.b) / 2);
        else if (db) v = argb(255, c.r / 2, c.g / 2, c.b / 2);
        else         v = argb(255, c.r, c.g, c.b);
        px[y * W + x] = v;
    }
    if (which == BTN_A) glyph(px, W, H, GLYPH_A);
    else if (which == BTN_B) glyph(px, W, H, GLYPH_B);
    else if (which == BTN_MENU) {            /* hamburger */
        for (int k = 0; k < 3; k++)
            for (int x = 4; x < CELL - 4; x++)
                put(px, W, H, x, 4 + k * 4, argb(255, 255, 255, 255));
    }
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC, W, H);
    SDL_UpdateTexture(t, NULL, px, W * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    free(px);
    return t;
}

/* Thumbstick base ring (hollow) and knob (filled) as pixel-art discs. */
static SDL_Texture *gen_disc(int fill, Uint8 a) {
    int N = 24, R = N / 2 - 1;
    uint32_t *px = calloc((size_t)N * N, 4);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float dx = x - (N - 1) / 2.0f, dy = y - (N - 1) / 2.0f;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > R) continue;
        bool rim = (d > R - 1.6f);
        if (!fill && !rim) continue;          /* base = ring only */
        Uint8 lum = rim ? 235 : 200;
        /* simple top-light gradient */
        int sh = (int)(dy * 2.0f);
        int l = lum - sh; if (l < 90) l = 90; if (l > 255) l = 255;
        px[y * N + x] = argb(a, (Uint8)l, (Uint8)l, (Uint8)(l > 245 ? 255 : l + 10));
    }
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC, N, N);
    SDL_UpdateTexture(t, NULL, px, N * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    free(px);
    return t;
}

static void gen_controls(void) {
    g_btn_tex[BTN_A]    = gen_round(BTN_A);
    g_btn_tex[BTN_B]    = gen_round(BTN_B);
    g_btn_tex[BTN_MENU] = gen_round(BTN_MENU);
    g_btn_tex[BTN_LB]   = gen_shoulder(BTN_LB);
    g_btn_tex[BTN_RB]   = gen_shoulder(BTN_RB);
    g_stick_base = gen_disc(0, 150);
    g_stick_knob = gen_disc(1, 235);
}

/* ---- screen-space layout (recomputed each frame from output size) ---- */
typedef struct { float cx, cy, r; } Ctrl;   /* normalised centre + radius */
static Ctrl s_ctrl[BTN_COUNT];
static float s_stick_cx, s_stick_cy, s_stick_r;   /* fixed base, normalised */
static int   s_view_x, s_view_y, s_view_s;        /* game square, px */
static int   g_ow = 1, g_oh = 1;

static void layout(int ow, int oh) {
    g_ow = ow; g_oh = oh;
    float mind = (float)(ow < oh ? ow : oh);
    float top = oh * 0.115f;                  /* top strip for MENU/LB/RB */

    /* Largest square that fits below the top strip and between gutters. */
    float gs = oh - top - oh * 0.03f;
    if (gs > ow * 0.60f) gs = ow * 0.60f;
    s_view_s = (int)gs;
    s_view_x = (ow - s_view_s) / 2;
    s_view_y = (int)(top + ((oh - top) - gs) * 0.5f);

    /* Top strip: shoulder buttons in the corners. */
    s_ctrl[BTN_LB]   = (Ctrl){ 0.085f, top * 0.5f / oh, mind * 0.052f / mind };
    s_ctrl[BTN_RB]   = (Ctrl){ 0.915f, top * 0.5f / oh, mind * 0.052f / mind };

    /* Left gutter: fixed thumbstick, lower third. */
    float lg = (float)s_view_x / ow;          /* gutter width fraction */
    s_stick_cx = lg * 0.5f;
    s_stick_cy = 0.70f;
    s_stick_r  = mind * 0.115f / mind;         /* fraction of mind */

    /* Right gutter, bottom-to-top: A (big), B (above-left), MENU (above A,
     * mid-height — easy thumb reach without leaving the action cluster). */
    float rg0 = (float)(s_view_x + s_view_s) / ow;
    float rgc = rg0 + (1.0f - rg0) * 0.5f;
    s_ctrl[BTN_A]    = (Ctrl){ rgc + 0.04f, 0.74f, mind * 0.085f / mind };
    s_ctrl[BTN_B]    = (Ctrl){ rgc - 0.06f, 0.58f, mind * 0.066f / mind };
    s_ctrl[BTN_MENU] = (Ctrl){ rgc + 0.04f, 0.42f, mind * 0.044f / mind };
}

static void blit_ctrl(SDL_Texture *t, float cx, float cy, float r,
                      float aspect, Uint8 bright, Uint8 alpha) {
    if (!t) return;
    int mind = g_ow < g_oh ? g_ow : g_oh;
    int w = (int)(r * mind * 2.0f * aspect);
    int h = (int)(r * mind * 2.0f);
    SDL_Rect dst = { (int)(cx * g_ow) - w / 2, (int)(cy * g_oh) - h / 2, w, h };
    SDL_SetTextureColorMod(t, bright, bright, bright);
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_RenderCopy(ren, t, NULL, &dst);
}

/* ---- touch tracking ------------------------------------------------- */
#define MAX_TOUCH 8
enum { ROLE_NONE, ROLE_STICK, ROLE_BTN };
typedef struct {
    SDL_FingerID id; bool active; int role; int btn;
    float cx, cy;                  /* current, normalised */
} Touch;
static Touch s_touch[MAX_TOUCH];
static bool  s_btn_down[BTN_COUNT];
static float s_stick_dx, s_stick_dy;   /* knob offset, -1..1 of base radius */

static Touch *touch_find(SDL_FingerID id) {
    for (int i = 0; i < MAX_TOUCH; i++)
        if (s_touch[i].active && s_touch[i].id == id) return &s_touch[i];
    return NULL;
}
static Touch *touch_alloc(void) {
    for (int i = 0; i < MAX_TOUCH; i++)
        if (!s_touch[i].active) return &s_touch[i];
    return NULL;
}

/* Which on-screen button (if any) is under a normalised point. */
static int hit_button(float nx, float ny) {
    int mind = g_ow < g_oh ? g_ow : g_oh;
    float px = nx * g_ow, py = ny * g_oh;
    for (int i = 0; i < BTN_COUNT; i++) {
        float bx = s_ctrl[i].cx * g_ow, by = s_ctrl[i].cy * g_oh;
        float r  = s_ctrl[i].r * mind * 1.35f;      /* touch slop */
        float aw = (i == BTN_LB || i == BTN_RB) ? 1.7f : 1.0f;
        float dx = (px - bx) / aw, dy = py - by;
        if (dx * dx + dy * dy <= r * r) return i;
    }
    return -1;
}
static bool in_stick_zone(float nx, float ny) {
    /* the whole left gutter below the top strip grabs the stick */
    return nx < (float)s_view_x / g_ow && ny > 0.30f;
}

/* ---- gamepad -------------------------------------------------------- */
static bool s_using_pad;            /* hide touch overlay while true */
static Uint32 s_last_touch_ms;

static void scan_pads(void) {
    if (s_pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) {
            s_pad = SDL_GameControllerOpen(i);
            if (s_pad) { SDL_Log("[elite] controller: %s",
                                 SDL_GameControllerName(s_pad)); break; }
        }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS |
                 SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* nearest, crisp */

    win = SDL_CreateWindow("ThumbyElite", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, R3D_FB_W, R3D_FB_H);

    /* Save + settings in app-private internal storage. */
    const char *store = SDL_AndroidGetInternalStoragePath();
    if (store) SDL_snprintf(g_sav_path, sizeof g_sav_path,
                            "%s/thumbyelite.sav", store);
    else       SDL_strlcpy(g_sav_path, "thumbyelite.sav", sizeof g_sav_path);
    settings_restore();      /* volume + gamepad/touch sensitivity */

    /* Audio: the game's audio_init runs inside elite_game_init; we just
     * open the device and pull audio_render from the callback. */
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = ELITE_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = audio_cb;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);

    gen_controls();

    uint32_t seed = (uint32_t)(SDL_GetPerformanceCounter() & 0xFFFFFFFFu);
    elite_game_init(seed);
    audio_set_master((float)s_settings[0] / 20.0f);
    if (audio_dev) SDL_PauseAudioDevice(audio_dev, 0);

    const float STICK_DZ = 0.16f;          /* analog deadzone */
    const float MENU_TH  = 0.55f;          /* stick->digital threshold */

    bool running = true;
    Uint32 last_ms = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: running = false; break;
            case SDL_APP_WILLENTERBACKGROUND:
                if (audio_dev) SDL_PauseAudioDevice(audio_dev, 1);
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                if (audio_dev) SDL_PauseAudioDevice(audio_dev, 0);
                break;
            case SDL_RENDER_DEVICE_RESET:
            case SDL_RENDER_TARGETS_RESET:
                if (tex) SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
                    SDL_TEXTUREACCESS_STREAMING, R3D_FB_W, R3D_FB_H);
                for (int i = 0; i < BTN_COUNT; i++)
                    if (g_btn_tex[i]) { SDL_DestroyTexture(g_btn_tex[i]);
                                        g_btn_tex[i] = NULL; }
                if (g_stick_base) SDL_DestroyTexture(g_stick_base);
                if (g_stick_knob) SDL_DestroyTexture(g_stick_knob);
                gen_controls();
                break;
            case SDL_CONTROLLERDEVICEADDED: scan_pads(); break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (s_pad) { SDL_GameControllerClose(s_pad); s_pad = NULL; }
                s_using_pad = false;
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERAXISMOTION:
                s_using_pad = true;
                break;
            case SDL_FINGERDOWN: {
                s_last_touch_ms = SDL_GetTicks();
                s_using_pad = false;
                Touch *t = touch_alloc();
                if (!t) break;
                t->active = true; t->id = ev.tfinger.fingerId;
                t->cx = ev.tfinger.x; t->cy = ev.tfinger.y;
                int b = hit_button(ev.tfinger.x, ev.tfinger.y);
                if (b >= 0)               { t->role = ROLE_BTN; t->btn = b; }
                else if (in_stick_zone(ev.tfinger.x, ev.tfinger.y))
                                          { t->role = ROLE_STICK; }
                else                      { t->role = ROLE_NONE; }
                break;
            }
            case SDL_FINGERMOTION: {
                Touch *t = touch_find(ev.tfinger.fingerId);
                if (t) { t->cx = ev.tfinger.x; t->cy = ev.tfinger.y; }
                break;
            }
            case SDL_FINGERUP: {
                Touch *t = touch_find(ev.tfinger.fingerId);
                if (t) { t->active = false; t->role = ROLE_NONE; }
                break;
            }
            default: break;
            }
        }

        Uint32 now_ms = SDL_GetTicks();
        float dt = (now_ms - last_ms) * 0.001f;
        if (dt > 0.1f) dt = 0.1f;
        last_ms = now_ms;

        int ow, oh;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        layout(ow, oh);

        /* ---- assemble input ---------------------------------------- */
        CraftRawButtons btn = {0};
        float ana_x = 0.0f, ana_y = 0.0f, ana_roll = 0.0f;
        float sens = 1.0f;             /* aim multiplier (settings slider) */
        memset(s_btn_down, 0, sizeof s_btn_down);
        s_stick_dx = s_stick_dy = 0.0f;

        if (s_using_pad && s_pad) {
            /* Left stick = fly (analog) + d-pad threshold for menus. */
            float lx = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTX)  / 32767.0f;
            float ly = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTY)  / 32767.0f;
            float rx = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
            float ry = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
            if (fabsf(lx) < STICK_DZ) lx = 0; if (fabsf(ly) < STICK_DZ) ly = 0;
            if (fabsf(rx) < STICK_DZ) rx = 0; if (fabsf(ry) < STICK_DZ) ry = 0;
            ana_x = lx; ana_y = -ly; ana_roll = rx;
            sens = (float)s_settings[2] * 0.1f;       /* GAMEPAD slider */
            elite_input_set_throttle_delta(-ry);      /* right stick up = throttle up */
            elite_input_set_throttle_abs(-1.0f);      /* keep the RB chord */
            s_stick_dx = lx; s_stick_dy = ly;
            if (ly < -MENU_TH) btn.up = true;
            if (ly >  MENU_TH) btn.down = true;
            if (lx < -MENU_TH) btn.left = true;
            if (lx >  MENU_TH) btn.right = true;
            if (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_UP))    btn.up = true;
            if (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  btn.down = true;
            if (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  btn.left = true;
            if (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) btn.right = true;
            if (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_START)) btn.menu = true;
            /* Scheme A: dedicated buttons. In menus A=select B=back; in flight
             * the full mapping (matches the host gamepad). */
            int gst = elite_game_state();
            bool inmenu = (gst != 0 && gst != 1);
#define GBTN(b) SDL_GameControllerGetButton(s_pad, b)
            if (inmenu) {
                if (GBTN(SDL_CONTROLLER_BUTTON_A)) btn.a = true;
                if (GBTN(SDL_CONTROLLER_BUTTON_B)) btn.b = true;
                if (GBTN(SDL_CONTROLLER_BUTTON_Y)) btn.lb = true;  /* Info */
            } else {
                bool plb = GBTN(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
                bool prb = GBTN(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
                if (SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000) btn.a = true;
                elite_input_set_fire2(SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000);
                elite_input_set_fire3(prb && !plb);
                if (plb && prb)      elite_input_action(CTRL_BTN_DOCK);   /* LB+RB dock */
                else if (plb)        btn.b = true;                        /* cycle weapon */
                static const struct { int sdl, act; } k_g[] = {
                    { SDL_CONTROLLER_BUTTON_A, CTRL_BTN_BOOST },
                    { SDL_CONTROLLER_BUTTON_B, CTRL_BTN_CYCLE_TARGET },
                    { SDL_CONTROLLER_BUTTON_X, CTRL_BTN_ASSIST },
                    { SDL_CONTROLLER_BUTTON_BACK, CTRL_BTN_CHAFF },
                    { SDL_CONTROLLER_BUTTON_LEFTSTICK, CTRL_BTN_CLOAK },
                };
                static bool gprev[5];
                for (unsigned i = 0; i < 5; i++) {
                    bool now = GBTN(k_g[i].sdl);
                    if (now && !gprev[i]) elite_input_action(k_g[i].act);
                    gprev[i] = now;
                }
            }
#undef GBTN
        } else {
            /* Touch: walk active fingers. */
            sens = (float)s_settings[3] * 0.1f;       /* STICK slider */
            elite_input_set_throttle_delta(0.0f);     /* clear gamepad residue */
            elite_input_set_throttle_abs(-1.0f);
            for (int i = 0; i < MAX_TOUCH; i++) {
                Touch *t = &s_touch[i];
                if (!t->active) continue;
                if (t->role == ROLE_BTN) {
                    s_btn_down[t->btn] = true;
                } else if (t->role == ROLE_STICK) {
                    float dx = (t->cx - s_stick_cx) * g_ow;
                    float dy = (t->cy - s_stick_cy) * g_oh;
                    float R  = s_stick_r * (g_ow < g_oh ? g_ow : g_oh);
                    float nx = dx / R, ny = dy / R;
                    float l = sqrtf(nx * nx + ny * ny);
                    if (l > 1.0f) { nx /= l; ny /= l; }
                    s_stick_dx = nx; s_stick_dy = ny;
                    ana_x = (fabsf(nx) < STICK_DZ) ? 0 : nx;
                    ana_y = (fabsf(ny) < STICK_DZ) ? 0 : -ny;
                    if (ny < -MENU_TH) btn.up = true;
                    if (ny >  MENU_TH) btn.down = true;
                    if (nx < -MENU_TH) btn.left = true;
                    if (nx >  MENU_TH) btn.right = true;
                }
            }
            btn.a    = s_btn_down[BTN_A];
            btn.b    = s_btn_down[BTN_B];
            btn.lb   = s_btn_down[BTN_LB];
            btn.rb   = s_btn_down[BTN_RB];
            btn.menu = s_btn_down[BTN_MENU];
        }

        elite_input_set_analog(ana_x * sens, ana_y * sens);
        elite_input_set_analog_roll(ana_roll * sens);

        /* ---- tick + render ----------------------------------------- */
        elite_game_tick(&btn, dt);

        elite_game_render_begin();
        elite_game_render(g_fb3d, 0, ELITE_FB_H);
        for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++) g_fbui[i] = ELITE_KEY_T;
        elite_game_draw_overlay(g_fbui);
        /* composite: UI pixel-doubled over the physical 3D frame */
        for (int y = 0; y < R3D_FB_H; y++) {
            const uint16_t *ur = g_fbui + (y / R3D_SS) * ELITE_FB_W;
            const uint16_t *dr = g_fb3d + y * R3D_FB_W;
            uint16_t *o = g_comp + y * R3D_FB_W;
            for (int x = 0; x < R3D_FB_W; x++) {
                uint16_t u = ur[x / R3D_SS];
                if (u == ELITE_KEY_T)        o[x] = dr[x];
                else if (u == ELITE_KEY_DIM) o[x] = (uint16_t)((dr[x] >> 1) & 0x7BEF);
                else                         o[x] = u;
            }
        }

        /* ---- present ----------------------------------------------- */
        SDL_UpdateTexture(tex, NULL, g_comp, R3D_FB_W * sizeof(uint16_t));
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_Rect dst = { s_view_x, s_view_y, s_view_s, s_view_s };
        SDL_RenderCopy(ren, tex, NULL, &dst);

        /* ---- touch overlay (fades out under controller use) -------- */
        Uint8 ov = 255;
        if (s_using_pad) {
            Uint32 since = SDL_GetTicks() - s_last_touch_ms;
            ov = (since > 1500) ? 0 : (Uint8)(255 - since * 255 / 1500);
        }
        if (ov > 0) {
            int mind = ow < oh ? ow : oh;
            /* stick base + knob */
            blit_ctrl(g_stick_base, s_stick_cx, s_stick_cy, s_stick_r, 1.0f,
                      255, (Uint8)(ov * 0.62f));
            float kx = s_stick_cx + s_stick_dx * s_stick_r * (float)mind / ow;
            float ky = s_stick_cy + s_stick_dy * s_stick_r * (float)mind / oh;
            blit_ctrl(g_stick_knob, kx, ky, s_stick_r * 0.5f, 1.0f, 255, ov);
            /* buttons */
            for (int i = 0; i < BTN_COUNT; i++) {
                float aw = (i == BTN_LB || i == BTN_RB) ? 1.7f : 1.0f;
                bool dn = s_btn_down[i];
                blit_ctrl(g_btn_tex[i], s_ctrl[i].cx, s_ctrl[i].cy, s_ctrl[i].r,
                          aw, dn ? 255 : 220, dn ? ov : (Uint8)(ov * 0.88f));
            }
        }

        SDL_RenderPresent(ren);
    }

    for (int i = 0; i < BTN_COUNT; i++) if (g_btn_tex[i]) SDL_DestroyTexture(g_btn_tex[i]);
    if (g_stick_base) SDL_DestroyTexture(g_stick_base);
    if (g_stick_knob) SDL_DestroyTexture(g_stick_knob);
    if (s_pad) SDL_GameControllerClose(s_pad);
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
