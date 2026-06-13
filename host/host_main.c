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
#include "elite_collide.h"
#include "elite_rocks.h"
#include "ui_icons.h"
#include "elite_save.h"
#include "elite_proj.h"
#include "elite_input.h"
#include "elite_ctrl.h"
#include "elite_loot.h"
#include "elite_ships.h"
#include "r3d_scene.h"
#include "r3d_pipe.h"
#include "vec.h"
#include "events.h"
#include "ui_event.h"
#include "ui_station.h"
#include "r3d_face.h"
#include "r3d_planet.h"
#include "ship_gen.h"
#include "station_gen.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#define EV_MKDIR(p) _mkdir(p)
#else
#define EV_MKDIR(p) mkdir(p, 0777)
#endif

#ifdef ELITE_OVERLAY_SPLIT
/* Hi-res desktop / Android-preview build: 3D renders physical (R3D_SS x),
 * the 2D overlay draws into its own logical key-colour buffer, and we
 * composite — the same path the Android shell uses. Window scale shrinks
 * as the frame grows so the window stays ~768px on a side regardless of
 * supersample factor (SS=2 -> 3x/768, SS=4 -> 2x/1024). */
#if R3D_SS >= 4
#define SCALE 2
#else
#define SCALE 3
#endif
#define OUT_W R3D_FB_W
#define OUT_H R3D_FB_H
static uint16_t g_fb3d[R3D_FB_W * R3D_FB_H];
static uint16_t g_fbui[ELITE_FB_W * ELITE_FB_H];
static uint16_t g_fb[R3D_FB_W * R3D_FB_H];      /* composited */
#else
#define SCALE 5
#define OUT_W ELITE_FB_W
#define OUT_H ELITE_FB_H
static uint16_t g_fb[ELITE_FB_W * ELITE_FB_H];
#endif
#define WIN_W (OUT_W * SCALE)
#define WIN_H (OUT_H * SCALE)

/* --- platform hooks ----------------------------------------------------*/
/* 0 volume, 1 brightness, 2 gamepad sens (x0.1), 3 touch-stick sens. */
/* [4] = input override (0 AUTO/1 HOTAS/2 GAMEPAD/3 KEYBOARD); [5] = fullscreen. */
#define HOST_NSETTINGS 6
static int s_host_settings[HOST_NSETTINGS] = { 10, 255, 10, 10, 0, 0 };
static SDL_Window *s_win;          /* set in main; toggled for fullscreen */
static void host_apply_fullscreen(void) {
    if (s_win) SDL_SetWindowFullscreen(
        s_win, s_host_settings[5] ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}
int plat_setting_get(int which) {
    if (which < 0 || which >= HOST_NSETTINGS) return 0;
    return s_host_settings[which];
}
void plat_setting_set(int which, int value) {
    if (which < 0 || which >= HOST_NSETTINGS) return;
    s_host_settings[which] = value;
    if (which == 0) audio_set_master((float)value / 20.0f);
    if (which == 5) host_apply_fullscreen();
    /* brightness (1): no-op on host. sensitivity (2,3): read each frame
     * when applying analog input — no side effect here. Persisted on quit. */
}

void plat_rumble(float intensity, float seconds) {
    (void)intensity; (void)seconds;     /* no motor on the desk */
}
int plat_save(const uint8_t *data, int len) {
    FILE *f = fopen("indemnityrun.sav", "wb");
    if (!f) return 0;
    fwrite(data, 1, (size_t)len, f);
    fclose(f);
    return 1;
}
int plat_load(uint8_t *data, int max_len) {
    FILE *f = fopen("indemnityrun.sav", "rb");
    if (!f) return 0;
    int n = (int)fread(data, 1, (size_t)max_len, f);
    fclose(f);
    return n;
}

static void audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    audio_render((int16_t *)stream, len / (int)sizeof(int16_t));
}

#ifdef ELITE_OVERLAY_SPLIT
static void render_frame(void) {
    elite_game_render_begin();
    elite_game_render(g_fb3d, 0, ELITE_FB_H);
    for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++)
        g_fbui[i] = ELITE_KEY_T;
    elite_game_draw_overlay(g_fbui);
    /* Composite: UI pixel-doubled over the physical 3D frame. */
    for (int y = 0; y < R3D_FB_H; y++) {
        const uint16_t *ur = g_fbui + (y / R3D_SS) * ELITE_FB_W;
        const uint16_t *dr = g_fb3d + y * R3D_FB_W;
        uint16_t *o = g_fb + y * R3D_FB_W;
        for (int x = 0; x < R3D_FB_W; x++) {
            uint16_t u = ur[x / R3D_SS];
            if (u == ELITE_KEY_T)        o[x] = dr[x];
            else if (u == ELITE_KEY_DIM) o[x] = (uint16_t)((dr[x] >> 1) & 0x7BEF);
            else                         o[x] = u;
        }
    }
}
#else
static void render_frame(void) {
    elite_game_render_begin();
    elite_game_render(g_fb, 0, ELITE_FB_H);
    elite_game_draw_overlay(g_fb);
}
#endif

static void dump_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", OUT_W, OUT_H);
    for (int i = 0; i < OUT_W * OUT_H; i++) {
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

/* ====================================================================== *
 *  Controllers: SDL game controllers (Xbox/PS) + raw joysticks (HOTAS).
 *
 *  Gamepad  — left stick fly, right stick X roll + Y throttle, A/B fire/
 *             secondary, LB/RB the device chords (tap cycle-target / assist,
 *             RB double-tap boost), Start menu, RT also fires, d-pad menus.
 *  HOTAS    — stick X yaw, stick Y pitch, TWIST roll ("twist to rotate"),
 *             throttle lever = absolute throttle. Axis indices vary by
 *             device, so they're env-overridable; ELITE_HOTAS_DEBUG=1 prints
 *             live axes/buttons so you can discover yours.
 * ====================================================================== */
static SDL_GameController *s_pad;
static SDL_Joystick       *s_joy;          /* a non-mapped stick = HOTAS */
static SDL_JoystickID      s_joy_id = -1;
/* Active flight device (last actuated wins; see host_input_apply). */
enum { DEV_NONE, DEV_HOTAS, DEV_PAD };
static int s_active_dev = DEV_NONE;

/* HOTAS bindings, indexed by CtrlAxis / CtrlButton (-1 button = unbound).
 * Defaults suit a twist stick with a combined throttle and reproduce the old
 * behaviour (fire=0, weapon=1, target=3, assist=2, menu=4). */
static int s_hx[CTRL_AX_N] = {            /* joystick axis per flight axis */
    [CTRL_AX_ROLL] = 3, [CTRL_AX_PITCH] = 1,
    [CTRL_AX_YAW]  = 0, [CTRL_AX_THROTTLE] = 2 };
static int s_hi[CTRL_AX_N] = {            /* invert: roll + pitch + throttle on */
    [CTRL_AX_ROLL] = 1, [CTRL_AX_PITCH] = 1, [CTRL_AX_THROTTLE] = 1 };
static int s_btn[CTRL_BTN_N] = {
    [CTRL_BTN_FIRE] = 0, [CTRL_BTN_FIRE2] = -1, [CTRL_BTN_FIRE3] = -1,
    [CTRL_BTN_CYCLE_WEAPON] = 1, [CTRL_BTN_CYCLE_TARGET] = 3,
    [CTRL_BTN_TARGET_MODE] = -1,
    [CTRL_BTN_ASSIST] = 2, [CTRL_BTN_BOOST] = -1, [CTRL_BTN_CHAFF] = -1,
    [CTRL_BTN_CLOAK] = -1, [CTRL_BTN_DOCK] = -1, [CTRL_BTN_MENU] = 4,
    [CTRL_BTN_MENU_SELECT] = -1, [CTRL_BTN_MENU_BACK] = -1,
    [CTRL_BTN_MENU_INFO] = -1 };
static bool s_hotas_dbg;       /* stdout axis dump (Linux: ELITE_HOTAS_DEBUG) */

/* Config-file key tables (1:1 with the enums). */
static const char *k_ax_key[CTRL_AX_N]  = { "roll", "pitch", "yaw", "throttle" };
static const char *k_btn_key[CTRL_BTN_N] = {
    "btn_fire", "btn_fire2", "btn_fire3", "btn_cycle", "btn_target", "btn_tgtmode",
    "btn_assist", "btn_boost", "btn_chaff", "btn_cloak", "btn_dock", "btn_menu",
    "btn_menusel", "btn_menuback", "btn_menuinfo" };

/* HOTAS config file (next to the exe). The in-game CONTROLLER SETUP screen
 * reads/writes this; you can also hand-edit it. */
#define HOTAS_CFG "indemnityrun_hotas.cfg"
static void hotas_cfg_write(void) {
    FILE *f = fopen(HOTAS_CFG, "w");
    if (!f) return;
    fprintf(f, "# ThumbyElite HOTAS configuration — written by the in-game\n"
               "# CONTROLLER SETUP screen (SETTINGS). Axis/button numbers are\n"
               "# device-specific; edit in-game (press-to-bind) or here.\n");
    for (int i = 0; i < CTRL_AX_N; i++)
        fprintf(f, "%s=%d\n%s_invert=%d\n", k_ax_key[i], s_hx[i],
                k_ax_key[i], s_hi[i]);
    for (int i = 0; i < CTRL_BTN_N; i++)
        fprintf(f, "%s=%d\n", k_btn_key[i], s_btn[i]);
    fclose(f);
}
static void hotas_cfg_load(void) {
    FILE *f = fopen(HOTAS_CFG, "r");
    if (!f) { hotas_cfg_write(); return; }    /* write a template */
    char line[160], key[64]; int val;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (sscanf(line, " %63[a-z_0-9] = %d", key, &val) != 2) continue;
        for (int i = 0; i < CTRL_AX_N; i++) {
            char inv[40]; snprintf(inv, sizeof inv, "%s_invert", k_ax_key[i]);
            if (!strcmp(key, k_ax_key[i])) s_hx[i] = val;
            else if (!strcmp(key, inv))    s_hi[i] = val;
        }
        for (int i = 0; i < CTRL_BTN_N; i++)
            if (!strcmp(key, k_btn_key[i])) s_btn[i] = val;
    }
    fclose(f);
}

static void host_input_reset_axes(void) {
    elite_input_set_analog(0.0f, 0.0f);
    elite_input_set_analog_roll(0.0f);
    elite_input_set_throttle_abs(-1.0f);
    elite_input_set_throttle_delta(0.0f);
}

/* ====================================================================== *
 *  In-game CONTROLLER SETUP backend (plat_ctrl_*). Binds the raw joystick
 *  (HOTAS); a connected gamepad is shown read-only (standard mapping).
 * ====================================================================== */
static struct {
    int active, kind, which, armed;
    float base[24];
    uint32_t btn_base;
    Uint32 t0;
} s_cap;

int  plat_ctrl_present(void)  { return (s_joy || s_pad) ? 1 : 0; }
int  plat_ctrl_editable(void) { return s_joy ? 1 : 0; }
const char *plat_ctrl_device_name(void) {
    if (s_joy) return SDL_JoystickName(s_joy);
    if (s_pad) return SDL_GameControllerName(s_pad);
    return "NO DEVICE";
}
static const char *pad_axis_label(int ax) {
    static const char *n[CTRL_AX_N] =
        { "R-STK X", "L-STK Y", "L-STK X", "R-STK Y" };
    return (ax >= 0 && ax < CTRL_AX_N) ? n[ax] : "?";
}
static const char *pad_btn_label(int b) {     /* Scheme A */
    switch (b) {
    case CTRL_BTN_FIRE: return "RT"; case CTRL_BTN_FIRE2: return "LT";
    case CTRL_BTN_FIRE3: return "RB"; case CTRL_BTN_CYCLE_WEAPON: return "LB";
    case CTRL_BTN_CYCLE_TARGET: return "B"; case CTRL_BTN_TARGET_MODE: return "Y";
    case CTRL_BTN_ASSIST: return "X";
    case CTRL_BTN_BOOST: return "A"; case CTRL_BTN_CHAFF: return "BACK";
    case CTRL_BTN_CLOAK: return "L3"; case CTRL_BTN_DOCK: return "LB+RB";
    case CTRL_BTN_MENU: return "START";
    case CTRL_BTN_MENU_SELECT: return "A"; case CTRL_BTN_MENU_BACK: return "B";
    case CTRL_BTN_MENU_INFO: return "Y";
    default: return "—";
    }
}
void plat_ctrl_axis_label(CtrlAxis ax, char *out, int cap) {
    if (!out || cap <= 0) return;
    if (s_joy) snprintf(out, cap, "AXIS %d%s", s_hx[ax], s_hi[ax] ? " INV" : "");
    else       snprintf(out, cap, "%s", pad_axis_label(ax));
}
void plat_ctrl_btn_label(CtrlButton b, char *out, int cap) {
    if (!out || cap <= 0) return;
    if (s_joy) { if (s_btn[b] < 0) snprintf(out, cap, "—");
                 else snprintf(out, cap, "BTN %d", s_btn[b]); }
    else       snprintf(out, cap, "%s", pad_btn_label(b));
}
void plat_ctrl_capture_begin(int kind, int which) {
    if (!s_joy) { s_cap.active = 0; return; }   /* pad = read-only */
    s_cap.active = 1; s_cap.kind = kind; s_cap.which = which;
    s_cap.armed = 0; s_cap.t0 = SDL_GetTicks();
}
void plat_ctrl_capture_cancel(void) { s_cap.active = 0; }
static void cap_snapshot(void) {
    int n = SDL_JoystickNumAxes(s_joy); if (n > 24) n = 24;
    for (int i = 0; i < n; i++)
        s_cap.base[i] = SDL_JoystickGetAxis(s_joy, i) / 32767.0f;
    uint32_t bm = 0; int nb = SDL_JoystickNumButtons(s_joy); if (nb > 32) nb = 32;
    for (int i = 0; i < nb; i++)
        if (SDL_JoystickGetButton(s_joy, i)) bm |= (1u << i);
    s_cap.btn_base = bm;
}
int plat_ctrl_capture_poll(void) {
    if (!s_cap.active || !s_joy) return 0;
    if (!s_cap.armed) { cap_snapshot(); s_cap.armed = 1; return 0; }  /* settle 1 frame */
    if (SDL_GetTicks() - s_cap.t0 > 6000) { s_cap.active = 0; return -1; }  /* timeout */
    if (s_cap.kind == CTRL_KIND_BUTTON) {
        int nb = SDL_JoystickNumButtons(s_joy); if (nb > 32) nb = 32;
        for (int i = 0; i < nb; i++) {                 /* rising edge binds */
            bool now = SDL_JoystickGetButton(s_joy, i);
            bool was = (s_cap.btn_base >> i) & 1u;
            if (now && !was) { s_btn[s_cap.which] = i; s_cap.active = 0; return 1; }
        }
    } else {
        int n = SDL_JoystickNumAxes(s_joy); if (n > 24) n = 24;
        int best = -1; float bd = 0.0f, sd = 0.0f;     /* largest delta from baseline */
        for (int i = 0; i < n; i++) {
            float d = fabsf(SDL_JoystickGetAxis(s_joy, i) / 32767.0f - s_cap.base[i]);
            if (d > bd) { sd = bd; bd = d; best = i; } else if (d > sd) sd = d;
        }
        if (best >= 0 && bd > 0.5f && bd > sd + 0.25f) {
            s_hx[s_cap.which] = best; s_hi[s_cap.which] = 0;
            s_cap.active = 0; return 1;
        }
    }
    return 0;
}
void plat_ctrl_axis_invert(CtrlAxis ax) { if (s_joy) s_hi[ax] = !s_hi[ax]; }
void plat_ctrl_clear(int kind, int which) {
    if (s_joy && kind == CTRL_KIND_BUTTON) s_btn[which] = -1;
}
void plat_ctrl_save(void) { if (s_joy) hotas_cfg_write(); }

/* Live "what am I touching" monitor: a held button wins; else the axis moving
 * furthest from its slow-following rest (so a swept throttle reads even though
 * it rests at an extreme). Sticky — keeps the last hit so you can read it. */
static char  s_last_in[16];
static float s_axrest[24];
void plat_ctrl_monitor(void) {
    if (!s_joy) { s_last_in[0] = 0; return; }
    int nb = SDL_JoystickNumButtons(s_joy); if (nb > 32) nb = 32;
    for (int i = 0; i < nb; i++)
        if (SDL_JoystickGetButton(s_joy, i)) {
            snprintf(s_last_in, sizeof s_last_in, "BTN %d", i);
            return;
        }
    int n = SDL_JoystickNumAxes(s_joy); if (n > 24) n = 24;
    int best = -1; float bd = 0.0f;
    for (int i = 0; i < n; i++) {
        float v = SDL_JoystickGetAxis(s_joy, i) / 32767.0f;
        float d = fabsf(v - s_axrest[i]);
        if (d > bd) { bd = d; best = i; }
        s_axrest[i] += (v - s_axrest[i]) * 0.03f;   /* slow follow = rest */
    }
    if (best >= 0 && bd > 0.35f)
        snprintf(s_last_in, sizeof s_last_in, "AXIS %d", best);
}
const char *plat_ctrl_last_input(void) { return s_last_in; }

/* Menu-hint button label for the active device. HOTAS shows the configured
 * button number (or "—" if unbound); gamepad/keyboard show device-style names
 * (the gamepad's LB bumper and Y both drive Info in menus). */
const char *plat_menu_btn(int action) {
    /* Rotating buffers: a hint calls this 2-3 times in one snprintf, so each
     * call must return a distinct buffer (a single static would alias them). */
    static char buf[4][6];
    static unsigned bi;
    if (s_active_dev == DEV_HOTAS) {
        int b;
        switch (action) {
        case MB_A:    b = s_btn[CTRL_BTN_MENU_SELECT] >= 0
                          ? s_btn[CTRL_BTN_MENU_SELECT] : s_btn[CTRL_BTN_FIRE]; break;
        case MB_B:    b = s_btn[CTRL_BTN_MENU_BACK] >= 0
                          ? s_btn[CTRL_BTN_MENU_BACK] : s_btn[CTRL_BTN_CYCLE_WEAPON]; break;
        case MB_INFO: b = s_btn[CTRL_BTN_MENU_INFO]; break;
        default:      b = s_btn[CTRL_BTN_MENU]; break;
        }
        if (b < 0) return "--";
        char *out = buf[bi++ & 3];
        snprintf(out, 6, "B%d", b);
        return out;
    }
    switch (action) {     /* gamepad + keyboard: device-style names */
    case MB_A:    return "A";
    case MB_B:    return "B";
    case MB_INFO: return "LB";
    default:      return s_active_dev == DEV_PAD ? "STRT" : "MENU";
    }
}

static void host_input_open(int index) {
    if (SDL_IsGameController(index)) {
        if (!s_pad) {
            s_pad = SDL_GameControllerOpen(index);
            if (s_pad) printf("[pad] %s\n", SDL_GameControllerName(s_pad));
        }
    } else if (!s_joy) {                    /* raw joystick = treat as HOTAS */
        s_joy = SDL_JoystickOpen(index);
        if (s_joy) {
            s_joy_id = SDL_JoystickInstanceID(s_joy);
            printf("[hotas] %s — %d axes, %d buttons, %d hats\n",
                   SDL_JoystickName(s_joy), SDL_JoystickNumAxes(s_joy),
                   SDL_JoystickNumButtons(s_joy), SDL_JoystickNumHats(s_joy));
            printf("[hotas] roll(twist)=axis%d pitch=axis%d yaw=axis%d "
                   "throttle=axis%d — configure in-game (SETTINGS > CONTROLLER)\n",
                   s_hx[CTRL_AX_ROLL], s_hx[CTRL_AX_PITCH],
                   s_hx[CTRL_AX_YAW], s_hx[CTRL_AX_THROTTLE]);
        }
    }
}

static void host_input_init(void) {
    hotas_cfg_load();          /* file bindings (writes a template if missing) */
    s_hotas_dbg = getenv("ELITE_HOTAS_DEBUG") != NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) host_input_open(i);
}

static float jaxis(SDL_Joystick *j, int idx, int inv) {
    if (idx < 0 || idx >= SDL_JoystickNumAxes(j)) return 0.0f;
    float v = SDL_JoystickGetAxis(j, idx) / 32767.0f;
    if (v < -1.0f) v = -1.0f; if (v > 1.0f) v = 1.0f;
    return inv ? -v : v;
}
static float dz(float v, float d) { return (v > -d && v < d) ? 0.0f : v; }

/* When both a HOTAS and a gamepad are connected, the one you last actuated
 * (stick deflected or a button pressed — the resting throttle lever doesn't
 * count) becomes the active flight device. Just grab the other and move it. */
static bool joy_has_input(void) {
    if (!s_joy) return false;
    for (int i = 0; i < SDL_JoystickNumButtons(s_joy); i++)
        if (SDL_JoystickGetButton(s_joy, i)) return true;
    if (fabsf(jaxis(s_joy, s_hx[CTRL_AX_YAW],   0)) > 0.5f) return true;
    if (fabsf(jaxis(s_joy, s_hx[CTRL_AX_PITCH], 0)) > 0.5f) return true;
    if (fabsf(jaxis(s_joy, s_hx[CTRL_AX_ROLL],  0)) > 0.5f) return true;
    if (SDL_JoystickNumHats(s_joy) > 0 &&
        SDL_JoystickGetHat(s_joy, 0) != SDL_HAT_CENTERED) return true;
    return false;
}
static bool pad_has_input(void) {
    if (!s_pad) return false;
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++)
        if (SDL_GameControllerGetButton(s_pad, (SDL_GameControllerButton)b)) return true;
    if (fabsf(SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTX)  / 32767.0f) > 0.5f) return true;
    if (fabsf(SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTY)  / 32767.0f) > 0.5f) return true;
    if (fabsf(SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f) > 0.5f) return true;
    if (SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000) return true;
    return false;
}

/* Gamepad button mapping — Scheme A (dedicated buttons, no chords). Flight:
 * RT fire, LT fire-2, RB fire-3, LB cycle-weapon, A boost, B cycle-target,
 * X flight-assist, Y dock, Back chaff, L3 cloak, Start menu. Menus: A select,
 * B back, D-pad/stick nav, Start menu. The analog axes are set by the caller. */
static void gamepad_apply(CraftRawButtons *btn, bool inmenu) {
#define GB(b) SDL_GameControllerGetButton(s_pad, b)
    if (GB(SDL_CONTROLLER_BUTTON_DPAD_UP))    btn->up = true;
    if (GB(SDL_CONTROLLER_BUTTON_DPAD_DOWN))  btn->down = true;
    if (GB(SDL_CONTROLLER_BUTTON_DPAD_LEFT))  btn->left = true;
    if (GB(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) btn->right = true;
    if (GB(SDL_CONTROLLER_BUTTON_START))      btn->menu = true;
    if (inmenu) {
        if (GB(SDL_CONTROLLER_BUTTON_A)) btn->a = true;   /* select */
        if (GB(SDL_CONTROLLER_BUTTON_B)) btn->b = true;   /* back */
        if (GB(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ||
            GB(SDL_CONTROLLER_BUTTON_Y)) btn->lb = true;  /* Info (LB or Y) */
        if (GB(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) btn->rb = true;
        return;
    }
    bool rt = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000;
    bool lt = SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > 12000;
    bool lb = GB(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    bool rb = GB(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    if (rt) btn->a = true;                         /* FIRE (primary) */
    elite_input_set_fire2(lt);                     /* LT = FIRE 2 */
    elite_input_set_fire3(rb && !lb);              /* RB = FIRE 3 (alone) */
    if (lb && rb)        elite_input_action(CTRL_BTN_DOCK);   /* LB+RB = dock */
    else if (lb)         btn->b = true;            /* LB = cycle weapon */
    /* Edge-triggered dedicated actions (Y is free in flight; it's Info in menus). */
    static const struct { int sdl, act; } k_g[] = {
        { SDL_CONTROLLER_BUTTON_A,         CTRL_BTN_BOOST },
        { SDL_CONTROLLER_BUTTON_B,         CTRL_BTN_CYCLE_TARGET },
        { SDL_CONTROLLER_BUTTON_Y,         CTRL_BTN_TARGET_MODE },
        { SDL_CONTROLLER_BUTTON_X,         CTRL_BTN_ASSIST },
        { SDL_CONTROLLER_BUTTON_BACK,      CTRL_BTN_CHAFF },
        { SDL_CONTROLLER_BUTTON_LEFTSTICK, CTRL_BTN_CLOAK },
    };
    static bool prev[6];
    for (unsigned i = 0; i < 6; i++) {
        bool now = GB(k_g[i].sdl);
        if (now && !prev[i]) elite_input_action(k_g[i].act);
        prev[i] = now;
    }
#undef GB
}

/* Augment the keyboard-built btn with controller/HOTAS state, and drive the
 * analog/throttle hooks. gpad_sens scales the aim axes (settings slider). */
static void host_input_apply(CraftRawButtons *btn, float gpad_sens) {
    /* While binding in CONTROLLER SETUP, the stick drives the capture, not the
     * ship — suppress all analog so sweeping the throttle doesn't fly you. The
     * keyboard (built before this) still works to cancel. */
    if (s_cap.active) { host_input_reset_axes(); return; }
    /* Device selection. SETTINGS > INPUT can force one (1 HOTAS / 2 GAMEPAD /
     * 3 KEYBOARD); 0 = AUTO (last actuated wins, default to whatever's there). */
    int sel = s_host_settings[4];
    if (sel == 1)      s_active_dev = s_joy ? DEV_HOTAS : DEV_NONE;
    else if (sel == 2) s_active_dev = s_pad ? DEV_PAD   : DEV_NONE;
    else if (sel == 3) s_active_dev = DEV_NONE;          /* keyboard only */
    else {                                               /* AUTO */
        if (s_joy && joy_has_input())      s_active_dev = DEV_HOTAS;
        else if (s_pad && pad_has_input()) s_active_dev = DEV_PAD;
        if (s_active_dev == DEV_HOTAS && !s_joy) s_active_dev = DEV_NONE;
        if (s_active_dev == DEV_PAD   && !s_pad) s_active_dev = DEV_NONE;
        if (s_active_dev == DEV_NONE)
            s_active_dev = s_joy ? DEV_HOTAS : (s_pad ? DEV_PAD : DEV_NONE);
    }
    static int s_prev_dev = DEV_NONE;
    if (s_active_dev != s_prev_dev && s_active_dev != DEV_NONE) {
        printf("[input] active device: %s\n",
               s_active_dev == DEV_HOTAS ? "HOTAS" : "gamepad");
        s_prev_dev = s_active_dev;
    }
    /* In menus, controller buttons act as nav/select/back, not flight actions
     * (so a HOTAS trigger doesn't 'fire-select' and a gamepad's A/B select). */
    int gst = elite_game_state();
    /* Menu context: HOTAS trigger / gamepad RT must NOT 'fire-select'. The
     * death/insurance screen lives inside flight but is a menu prompt, so the
     * claim takes the menu-select button (matching its on-screen label). */
    bool inmenu = (gst != 0 && gst != 1) || elite_game_is_dead();

    if (s_active_dev == DEV_HOTAS) {        /* --- HOTAS --- */
        float roll  = dz(jaxis(s_joy, s_hx[CTRL_AX_ROLL],  s_hi[CTRL_AX_ROLL]),  0.10f);
        float pitch = dz(jaxis(s_joy, s_hx[CTRL_AX_PITCH], s_hi[CTRL_AX_PITCH]), 0.08f);
        float yaw   = dz(jaxis(s_joy, s_hx[CTRL_AX_YAW],   s_hi[CTRL_AX_YAW]),   0.08f);
        float lever = jaxis(s_joy, s_hx[CTRL_AX_THROTTLE], s_hi[CTRL_AX_THROTTLE]);
        elite_input_set_analog(yaw * gpad_sens, pitch * gpad_sens);
        elite_input_set_analog_roll(roll * gpad_sens);
        elite_input_set_throttle_abs((lever + 1.0f) * 0.5f);   /* -1..1 -> 0..1 */
        /* Stick also drives menu navigation (the hat does too, below). */
        if (yaw   < -0.55f) btn->left = true;
        if (yaw   >  0.55f) btn->right = true;
        if (pitch >  0.55f) btn->up = true;
        if (pitch < -0.55f) btn->down = true;
        /* Bound-button helper: pressed state of a CtrlButton's joystick button. */
        int nb = SDL_JoystickNumButtons(s_joy);
#define HB(act) (s_btn[act] >= 0 && s_btn[act] < nb && \
                 SDL_JoystickGetButton(s_joy, s_btn[act]))
        if (HB(CTRL_BTN_MENU)) btn->menu = true;
        if (elite_game_in_ctrlsetup()) {
            /* The binding screen uses FIXED controls (joystick button 0 =
             * bind/select, button 1 = back/save) independent of the bindings,
             * so rebinding menu buttons can never lock you out. Keyboard
             * always works too. Nav is the stick/hat (set above/below). */
            if (nb > 0 && SDL_JoystickGetButton(s_joy, 0)) btn->a = true;
            if (nb > 1 && SDL_JoystickGetButton(s_joy, 1)) btn->b = true;
        } else if (inmenu) {
            /* Select/back/info on the bound menu buttons; fall back to FIRE/
             * CYCLE so menus still work before you bind dedicated buttons. */
            bool sel = (s_btn[CTRL_BTN_MENU_SELECT] >= 0)
                       ? HB(CTRL_BTN_MENU_SELECT) : HB(CTRL_BTN_FIRE);
            bool bak = (s_btn[CTRL_BTN_MENU_BACK] >= 0)
                       ? HB(CTRL_BTN_MENU_BACK) : HB(CTRL_BTN_CYCLE_WEAPON);
            if (sel) btn->a = true;
            if (bak) btn->b = true;
            if (HB(CTRL_BTN_MENU_INFO)) btn->lb = true;   /* Info (if bound) */
        } else {
            if (HB(CTRL_BTN_FIRE))         btn->a = true;     /* held */
            if (HB(CTRL_BTN_CYCLE_WEAPON)) btn->b = true;     /* B edge in input.c */
            elite_input_set_fire2(HB(CTRL_BTN_FIRE2));        /* held extra weapons */
            elite_input_set_fire3(HB(CTRL_BTN_FIRE3));
            /* Dedicated chord actions: rising edge -> one-shot event. */
            static bool s_eprev[CTRL_BTN_N];
            static const int k_edge[] = { CTRL_BTN_CYCLE_TARGET, CTRL_BTN_TARGET_MODE,
                CTRL_BTN_ASSIST, CTRL_BTN_BOOST, CTRL_BTN_CHAFF, CTRL_BTN_CLOAK,
                CTRL_BTN_DOCK };
            for (unsigned i = 0; i < sizeof k_edge / sizeof k_edge[0]; i++) {
                int a = k_edge[i]; bool now = HB(a);
                if (now && !s_eprev[a]) elite_input_action(a);
                s_eprev[a] = now;
            }
        }
#undef HB
        if (SDL_JoystickNumHats(s_joy) > 0) {                  /* hat = menu nav */
            Uint8 h = SDL_JoystickGetHat(s_joy, 0);
            if (h & SDL_HAT_UP)    btn->up = true;
            if (h & SDL_HAT_DOWN)  btn->down = true;
            if (h & SDL_HAT_LEFT)  btn->left = true;
            if (h & SDL_HAT_RIGHT) btn->right = true;
        }
        if (s_hotas_dbg) {
            static int f; if ((f++ % 30) == 0) {
                printf("[hotas]");
                for (int i = 0; i < SDL_JoystickNumAxes(s_joy); i++)
                    printf(" a%d=%+.2f", i, SDL_JoystickGetAxis(s_joy, i) / 32767.0f);
                int bm = 0;
                for (int i = 0; i < SDL_JoystickNumButtons(s_joy); i++)
                    if (SDL_JoystickGetButton(s_joy, i)) bm |= (1 << i);
                printf("  btns=0x%x\n", bm);
            }
        }
    } else if (s_active_dev == DEV_PAD) {   /* --- gamepad (Scheme A) --- */
        float lx = dz(SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTX)  / 32767.0f, 0.15f);
        float ly = dz(SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_LEFTY)  / 32767.0f, 0.15f);
        float rx = dz(SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f, 0.15f);
        float ry = dz(SDL_GameControllerGetAxis(s_pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f, 0.15f);
        elite_input_set_analog(lx * gpad_sens, -ly * gpad_sens);   /* fly */
        elite_input_set_analog_roll(-rx * gpad_sens);              /* roll (stick R = roll R) */
        elite_input_set_throttle_delta(-ry);                       /* throttle */
        elite_input_set_throttle_abs(-1.0f);
        /* Left stick also navigates menus. */
        if (ly < -0.55f) btn->up = true;
        if (ly >  0.55f) btn->down = true;
        if (lx < -0.55f) btn->left = true;
        if (lx >  0.55f) btn->right = true;
        gamepad_apply(btn, inmenu);
    }
}

/* --- host settings persistence (4 ints) --------------------------------*/
/* s_host_settings is the file-scope array from the platform-hooks block. */
static const char *HOST_SETTINGS_FILE = "indemnityrun_settings.dat";
static void host_settings_load(void) {
    FILE *f = fopen(HOST_SETTINGS_FILE, "rb");
    if (!f) return;
    int tmp[HOST_NSETTINGS];
    /* Read as many ints as the file holds (older files have 4); leave the
     * rest at their defaults. */
    int n = (int)fread(tmp, sizeof(int), HOST_NSETTINGS, f);
    for (int i = 0; i < n; i++) plat_setting_set(i, tmp[i]);
    fclose(f);
}
static void host_settings_save(void) {
    FILE *f = fopen(HOST_SETTINGS_FILE, "wb");
    if (!f) return;
    fwrite(s_host_settings, sizeof(int), HOST_NSETTINGS, f);
    fclose(f);
}

#ifdef ELITE_STYLE_LAB
/* In-game A/B: F8 (or ELITE_STYLE=1 at launch) flips every generator to
 * the proposal look. Planets re-bake on the spot; the nebula/band are
 * per-frame; STARS and SHIP/STATION meshes restyle on the next arrival
 * (tints and hulls are baked at spawn). */
static int s_lab_style;
static void lab_apply_style(int st) {
    face_set_style(st);
    r3d_planet_set_style(st);
    r3d_scene_set_style(st);
    ship_gen_set_style(st);
    station_gen_set_style(st);
    if (system_info()) r3d_planet_bake(system_info());
    printf("[stylelab] in-game style = %s\n", st ? "PROPOSED" : "CURRENT");
}

/* --- contact-sheet canvas (style lab — proposal renders only) ----------- */
#define SHEET_MAX 1000
static uint16_t s_sheet[SHEET_MAX * SHEET_MAX];
static int s_sheet_w, s_sheet_h;
static void sheet_clear(int w, int h) {
    s_sheet_w = w; s_sheet_h = h;
    for (int i = 0; i < w * h; i++) s_sheet[i] = RGB565C(10, 12, 18);
}
static void sheet_blit(const uint16_t *src, int src_stride,
                       int sw, int sh, int dx, int dy, int down) {
    for (int y = 0; y < sh / down; y++)
        for (int x = 0; x < sw / down; x++) {
            int r = 0, g = 0, b = 0;
            for (int yy = 0; yy < down; yy++)
                for (int xx = 0; xx < down; xx++) {
                    uint16_t c = src[(y * down + yy) * src_stride +
                                     x * down + xx];
                    r += (c >> 11) & 31; g += (c >> 5) & 63; b += c & 31;
                }
            int n2 = down * down;
            uint16_t o = (uint16_t)(((r / n2) << 11) |
                                    ((g / n2) << 5) | (b / n2));
            int px = dx + x, py = dy + y;
            if (px >= 0 && py >= 0 && px < s_sheet_w && py < s_sheet_h)
                s_sheet[py * s_sheet_w + px] = o;
        }
}
static void sheet_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", s_sheet_w, s_sheet_h);
    for (int i = 0; i < s_sheet_w * s_sheet_h; i++) {
        uint16_t c = s_sheet[i];
        uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) * 255 / 31),
                           (uint8_t)(((c >> 5) & 63) * 255 / 63),
                           (uint8_t)((c & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("[stylelab] wrote %s (%dx%d)\n", path, s_sheet_w, s_sheet_h);
}

/* Lit-sphere render of a baked planet tile (mirrors the impostor
 * shader: terminator, limb darkening, world-up texture anchor). */
static void sheet_planet_disc(const uint8_t *tex, const uint16_t *pal,
                              int dx, int dy, int r_px) {
    const float lx = -0.52f, ly = -0.34f, lz = 0.78f;
    for (int py = -r_px; py <= r_px; py++)
        for (int px = -r_px; px <= r_px; px++) {
            float nx = (float)px / (float)r_px;
            float ny = (float)py / (float)r_px;
            float nz2 = 1.0f - nx * nx - ny * ny;
            if (nz2 < 0) continue;
            float nz = sqrtf(nz2);
            float light = nx * lx + ny * ly + nz * lz;
            if (light < 0) light = 0;
            float shade = 0.07f + 0.93f * light;
            shade *= 0.55f + 0.45f * nz;
            int tu = (int)((nx * 0.5f + 0.5f) * 31.0f);
            int tv = (int)((ny * 0.5f + 0.5f) * 31.0f);
            uint16_t c = pal[tex[tv * 32 + tu]];
            int cr = (int)(((c >> 11) & 31) * shade);
            int cg = (int)(((c >> 5) & 63) * shade);
            int cb = (int)((c & 31) * shade);
            int X = dx + r_px + px, Y = dy + r_px + py;
            if (X >= 0 && Y >= 0 && X < s_sheet_w && Y < s_sheet_h)
                s_sheet[Y * s_sheet_w + X] =
                    (uint16_t)((cr << 11) | (cg << 5) | cb);
        }
}

/* One mesh rendered to g_fb via the real scene pipe (flat backdrop). */
static void sheet_render_mesh(const Mesh *mesh, float yaw, float pitch) {
    Mat3 cam = m3_identity();
    r3d_scene_begin(&cam, 50.0f);
    r3d_pipe_set_sun(v3_norm(v3(0.45f, 0.55f, -0.72f)));
    R3DObject obj;
    obj.mesh = mesh;
    obj.basis = m3_identity();
    m3_rotate_local(&obj.basis, 1, yaw);
    m3_rotate_local(&obj.basis, 0, pitch);
    /* Frame by the ROTATED on-screen silhouette, not the bounding
     * sphere — otherwise compact bulky hulls (TIE pods) fill the cell
     * while sparse X-wings look tiny inside the same sphere (user). */
    float ext = 0.001f;
    for (int i = 0; i < mesh->nverts; i++) {
        Vec3 v = v3((float)mesh->verts[i].x, (float)mesh->verts[i].y,
                    (float)mesh->verts[i].z);
        v = v3_scale(v, mesh->scale / 127.0f);
        Vec3 r = m3_mul_v3(&obj.basis, v);
        float lat = sqrtf(r.x * r.x + r.y * r.y);   /* screen-plane radius */
        if (lat > ext) ext = lat;
    }
    obj.pos = v3(0, 0, ext * 3.0f);   /* silhouette fills ~the cell */
    r3d_scene_add_object(&obj);
    r3d_scene_raster(g_fb, 0, ELITE_FB_H);
}
#endif

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *shot_path = getenv("ELITE_SHOT");

    uint32_t seed = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0)
                               : (uint32_t)time(NULL);
    printf("[elite] seed = %u\n", seed);
#ifdef ELITE_STYLE_LAB
    /* ELITE_STYLE=1: boot straight into the proposal look (everything,
     * including first-spawn ships/stars, generates restyled). F8 flips
     * live in-game. */
    if (getenv("ELITE_STYLE") && atoi(getenv("ELITE_STYLE"))) {
        s_lab_style = 1;
        face_set_style(1);
        r3d_planet_set_style(1);
        r3d_scene_set_style(1);
        ship_gen_set_style(1);
        station_gen_set_style(1);
        printf("[stylelab] booting with PROPOSED style (F8 toggles)\n");
    }
#endif
    elite_game_init(seed);
    /* LB: taps cycle target, 3s STILL hold shifts mode, hold+roll rolls. */
    if (getenv("ELITE_LBTEST")) {
        FlightInput fi;
        CraftRawButtons b;
        int cyc = 0, mode = 0;
        /* five quick taps */
        for (int t = 0; t < 5; t++) {
            b = (CraftRawButtons){0}; b.lb = true;
            elite_input_update(&b, 1.0f/30.0f, &fi);
            if (fi.tgt_class_cycle) mode++;
            b = (CraftRawButtons){0};
            for (int k=0;k<4;k++) {
                elite_input_update(&b,1.0f/30.0f,&fi);
                if (fi.cycle_target) cyc++;
                if (fi.tgt_class_cycle) mode++;
            }
        }
        printf("[lb] 5 quick taps: cycle=%d mode=%d (want 5/0)\n",
               cyc, mode);
        /* 3s still hold */
        int mode2 = 0;
        for (int f = 0; f < 30 * 4; f++) {
            b = (CraftRawButtons){0}; b.lb = true;
            elite_input_update(&b, 1.0f/30.0f, &fi);
            if (fi.tgt_class_cycle) mode2++;
        }
        b = (CraftRawButtons){0}; elite_input_update(&b,1.0f/30.0f,&fi);
        printf("[lb] 4s still hold: mode shifts=%d (want 1)\n", mode2);
        /* 3s hold WHILE rolling (dpad) */
        int mode3 = 0;
        for (int f = 0; f < 30 * 4; f++) {
            b = (CraftRawButtons){0}; b.lb = true; b.left = true;
            elite_input_update(&b, 1.0f/30.0f, &fi);
            if (fi.tgt_class_cycle) mode3++;
        }
        printf("[lb] 4s hold+roll: mode shifts=%d (want 0)\n", mode3);
        return 0;
    }

    /* 100-ship weapon census across all tiers/classes. */
    if (getenv("ELITE_SHIPCENSUS")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        int wcount[WPN_COUNT];
        for (int i = 0; i < WPN_COUNT; i++) wcount[i] = 0;
        int nguns = 0, withturret = 0, ships = 0;
        int per_tier[5][WPN_COUNT];
        for (int a=0;a<5;a++) for (int b=0;b<WPN_COUNT;b++) per_tier[a][b]=0;
        for (int n = 0; n < 500; n++) {
            int tier = n % 5;               /* 100 per tier */
            int cls = 1 + (n * 7 + tier) % 9;
            int e = ship_spawn(hull_mesh(0x1000u + n * 131u, cls),
                               v3(0, 0, 500), TEAM_HOSTILE);
            if (e <= 0) continue;
            ship_set_tier(e, tier, cls);
            Ship *p = &g_ships[e];
            ships++;
            nguns += p->n_weapons;
            if (p->turret_type) withturret++;
            for (int w = 0; w < p->n_weapons; w++)
                if (p->weapons[w] < WPN_COUNT) {
                    wcount[p->weapons[w]]++;
                    per_tier[tier][p->weapons[w]]++;
                }
            p->alive = false;
        }
        printf("[census] %d ships, %d guns (%.1f avg), %d with turret\n",
               ships, nguns, (float)nguns / ships, withturret);
        printf("[census] per-tier gun counts (100 ships each):\n");
        printf("[census] %-9s  T0  T1  T2  T3  T4   ALL\n", "WEAPON");
        for (int i = 0; i < WPN_COUNT; i++)
            if (wcount[i])
                printf("[census] %-9s %3d %3d %3d %3d %3d  %4d\n",
                       k_weapons[i].name, per_tier[0][i], per_tier[1][i],
                       per_tier[2][i], per_tier[3][i], per_tier[4][i],
                       wcount[i]);
        return 0;
    }

    /* Title RB-x10 cheat -> 100k start. */
    if (getenv("ELITE_CHEATTEST")) {
        CraftRawButtons none = {0}, b;
        for (int k = 0; k < 5; k++) elite_game_tick(&none, 1.0f/30.0f);
        for (int tap = 0; tap < 10; tap++) {
            b = none; b.rb = true;
            elite_game_tick(&b, 1.0f / 30.0f);
            elite_game_tick(&none, 1.0f / 30.0f);
        }
        /* cursor to NEW GAME, fire */
        b = none; b.down = true; elite_game_tick(&b, 1.0f/30.0f);
        elite_game_tick(&none, 1.0f/30.0f);
        b = none; b.a = true; elite_game_tick(&b, 1.0f/30.0f);
        for (int k = 0; k < 5; k++) elite_game_tick(&none, 1.0f/30.0f);
        printf("[cheat] credits after 10xRB + NEW GAME: %d (%s)\n",
               g_player.credits,
               g_player.credits == 100000 ? "CHEAT ON" : "FAIL");
        return 0;
    }

    if (getenv("ELITE_EVENTTEST")) {
        /* Headless event-engine asserts. Sandboxed: plat_save writes
         * indemnityrun.sav in cwd, so run from a scratch dir. */
        (void)EV_MKDIR("/tmp/elite_evtest");
        if (chdir("/tmp/elite_evtest") != 0) { perror("chdir"); return 1; }
        int fails = 0;
#define EVCHECK(cond, name) do { \
        int ev_ok_ = !!(cond);   /* evaluate ONCE — conds have side effects */ \
        printf("[evtest] %-34s %s\n", name, ev_ok_ ? "PASS" : "FAIL"); \
        if (!ev_ok_) fails++; } while (0)

        galaxy_set_seed(42);
        player_init();
        SystemInfo si_any, si_law;
        bool got_any = false, got_law = false;
        for (int sy = -14; sy <= 14 && !(got_any && got_law); sy++)
            for (int sx = -14; sx <= 14 && !(got_any && got_law); sx++) {
                int ns = galaxy_sector_stars(sx, sy);
                for (int i = 0; i < ns; i++) {
                    SysAddr a = { sx, sy, (uint8_t)i };
                    SystemInfo si;
                    galaxy_generate(a, &si);
                    if (si.n_stations == 0) continue;
                    if (!got_any) { si_any = si; got_any = true; }
                    if (!got_law && si.gov >= GOV_CONFED) {
                        si_law = si; got_law = true;
                    }
                }
            }
        EVCHECK(got_any && got_law, "found test systems");

        /* find a specific event id by scanning visit salts */
#define FIND_EV(SI, IDWANT, EVOUT) do { EVOUT = NULL; \
        for (uint32_t s2 = 0; s2 < 200000u && !EVOUT; s2++) { \
            events_init(); events_set_salt(s2); \
            const Event *e2 = events_roll_dock(&(SI), 0); \
            if (e2 && e2->id == (IDWANT)) EVOUT = e2; } } while (0)

        /* determinism: same salt -> same pick + same npc */
        {
            events_init(); events_set_salt(1234u);
            const Event *a = NULL;
            uint32_t na = 0, salt = 0;
            for (uint32_t s2 = 0; s2 < 9999u && !a; s2++) {
                events_init(); events_set_salt(s2);
                a = events_roll_dock(&si_any, 0);
                salt = s2;
            }
            na = events_npc_seed();
            events_init(); events_set_salt(salt);
            const Event *b = events_roll_dock(&si_any, 0);
            EVCHECK(a && b && a->id == b->id && na == events_npc_seed(),
                    "pick deterministic per salt");
        }

        /* distress: fuel gate on the GIVE choice */
        {
            const Event *ev;
            FIND_EV(si_any, 1, ev);
            EVCHECK(ev != NULL, "distress hail reachable");
            if (ev) {
                g_player.fuel = 0.5f;
                bool low = events_choice_enabled(ev, 0);
                g_player.fuel = 5.0f;
                bool ok = events_choice_enabled(ev, 0);
                EVCHECK(!low && ok, "fuel gate on GIVE choice");
            }
        }

        /* wager: cost gate + deterministic +-150 outcome */
        {
            const Event *ev;
            FIND_EV(si_any, 10, ev);
            EVCHECK(ev != NULL, "wager reachable");
            if (ev) {
                g_player.credits = 10;
                bool broke = events_choice_enabled(ev, 0);
                g_player.credits = 1000;
                bool rich = events_choice_enabled(ev, 0);
                EVCHECK(!broke && rich, "cost gates TAKE THE BET");
                events_run_choice(ev, 0);
                int after1 = g_player.credits;
                EVCHECK(after1 == 850 || after1 == 1150,
                        "wager pays +-150");
                /* same salt -> same fate (no reroll scumming) */
                FIND_EV(si_any, 10, ev);
                g_player.credits = 1000;
                events_run_choice(ev, 0);
                /* NB: FIND_EV rescans from salt 0, so this is the same
                 * pick seed -> the branch must land the same way */
                EVCHECK(g_player.credits == after1,
                        "branch outcome visit-stable");
            }
        }

        /* preacher: lore bit + oneshot suppression */
        {
            const Event *ev;
            FIND_EV(si_any, 7, ev);
            EVCHECK(ev != NULL, "preacher reachable");
            if (ev) {
                events_run_choice(ev, 0);
                EVCHECK(events_lore_seen(5), "LISTEN reveals lore 5");
                int again = 0;
                for (int k = 0; k < 3000; k++) {
                    const Event *e2 = events_roll_dock(&si_any, 0);
                    if (e2 && e2->id == 7) again++;
                }
                EVCHECK(again == 0, "oneshot never re-offered");
            }
        }

        /* customs: gated on illegal cargo; SUBMIT confiscates + fines */
        {
            for (int g = 0; g < N_GOODS; g++) g_player.cargo[g] = 0;
            int hits = 0;
            events_init();
            for (int k = 0; k < 2000; k++) {
                const Event *e2 = events_roll_dock(&si_law, 0);
                if (e2 && e2->id == 2) hits++;
            }
            EVCHECK(hits == 0, "customs needs illegal cargo");
            g_player.cargo[16] = 3;          /* narcotics */
            const Event *ev;
            FIND_EV(si_law, 2, ev);
            EVCHECK(ev != NULL, "customs fires when dirty");
            if (ev) {
                g_player.credits = 1000;
                events_run_choice(ev, 0);    /* SUBMIT */
                EVCHECK(g_player.cargo[16] == 0 && g_player.credits == 850,
                        "SUBMIT confiscates + 150 fine");
            }
        }

        /* bribe branch distribution: 70% +-15 over 60 fresh picks */
        {
            int wins = 0, runs = 0;
            for (uint32_t s2 = 0; s2 < 400000u && runs < 60; s2++) {
                events_init(); events_set_salt(s2);
                g_player.cargo[16] = 3;
                g_player.credits = 1000;
                g_player.legal = 0;
                const Event *e2 = events_roll_dock(&si_law, 0);
                if (!e2 || e2->id != 2) continue;
                runs++;
                events_run_choice(e2, 1);    /* BRIBE 300 */
                if (g_player.legal == 0 && g_player.cargo[16] == 3) wins++;
            }
            printf("[evtest] bribe runs=%d wins=%d\n", runs, wins);
            EVCHECK(runs == 60 && wins > 33 && wins < 51,
                    "bribe branch ~70%");
        }

        /* token expansion leaves no $ behind */
        {
            const Event *ev;
            FIND_EV(si_any, 1, ev);
            char out[300];
            events_expand(ev->body, out, sizeof out);
            bool dollar = false;
            for (const char *p = out; *p; p++) if (*p == '$') dollar = true;
            EVCHECK(!dollar && out[0], "tokens fully expanded");
        }

        /* triggers are separate pools: dock never deals bar events */
        {
            events_init();
            int cross = 0;
            for (int k = 0; k < 2500; k++) {
                const Event *e2 = events_roll_dock(&si_any, 0);
                if (e2 && e2->trig != TRIG_DOCK) cross++;
                e2 = events_roll_bar(&si_any, 0);
                if (e2 && e2->trig != TRIG_BAR) cross++;
            }
            EVCHECK(cross == 0, "dock/bar pools separate");
        }

        /* the Adjuster arc: ordered by flags, one stable face */
        {
            events_init();
            g_player.credits = 1000;
            int early = 0;
            for (int k = 0; k < 3000; k++) {
                const Event *e2 = events_roll_bar(&si_any, 0);
                if (e2 && (e2->id == 12 || e2->id == 13)) early++;
            }
            EVCHECK(early == 0, "arc steps 2/3 wait for step 1");
            /* meet the stranger */
            const Event *ev = NULL;
            for (uint32_t s2 = 0; s2 < 200000u && !ev; s2++) {
                events_init(); events_set_salt(s2);
                const Event *e2 = events_roll_bar(&si_any, 0);
                if (e2 && e2->id == 11) ev = e2;
            }
            EVCHECK(ev != NULL, "stranger in grey reachable");
            uint32_t npc1 = events_npc_seed();
            events_run_choice(ev, 0);            /* HEAR -> flag 8 */
            EVCHECK(events_lore_seen(0), "step 1 reveals lore 0");
            /* retainer now offerable, same face */
            const Event *ev2 = NULL;
            for (int k = 0; k < 30000 && !ev2; k++) {
                const Event *e2 = events_roll_bar(&si_any, 0);
                if (e2 && e2->id == 12) ev2 = e2;
            }
            EVCHECK(ev2 != NULL, "retainer unlocked by flag 8");
            EVCHECK(events_npc_seed() == npc1, "recurring NPC keeps face");
            events_run_choice(ev2, 0);           /* TAKE -> flag 10 */
            EVCHECK(events_lore_seen(1), "step 2 reveals lore 1");
            int again12 = 0;
            const Event *ev3 = NULL;
            for (int k = 0; k < 30000; k++) {
                const Event *e2 = events_roll_bar(&si_any, 0);
                if (e2 && e2->id == 12) again12++;
                e2 = events_roll_dock(&si_any, 0);
                if (e2 && e2->id == 13 && !ev3) ev3 = e2;
            }
            EVCHECK(again12 == 0, "retainer retires on flag 10");
            EVCHECK(ev3 != NULL, "claim adjusted fires at dock");
            if (ev3) {
                events_run_choice(ev3, 0);       /* DEMAND -> lore 2 */
                EVCHECK(events_lore_seen(2), "step 3 reveals lore 2");
                /* flag 11 now set: GREY PAINT joins the derelict pool */
                const Event *gp = NULL;
                for (int k = 0; k < 4000 && !gp; k++) {
                    const Event *e2 = events_roll_space(&si_any);
                    if (e2 && e2->id == 21) gp = e2;
                }
                EVCHECK(gp != NULL, "grey paint unlocked post-arc");
            }
        }

        /* space + arrival pools: separate, gated, reachable */
        {
            events_init();
            g_player.credits = 1000;
            for (int g = 0; g < N_GOODS; g++) g_player.cargo[g] = 0;
            int cross = 0, hull18 = 0, grey21 = 0;
            for (int k = 0; k < 2500; k++) {
                const Event *e2 = events_roll_space(&si_any);
                if (e2 && e2->trig != TRIG_SPACE) cross++;
                if (e2 && e2->id == 18) hull18++;
                if (e2 && e2->id == 21) grey21++;
                e2 = events_roll_arrival(&si_any);
                if (e2 && e2->trig != TRIG_ARRIVAL) cross++;
            }
            EVCHECK(cross == 0, "space/arrival pools separate");
            EVCHECK(hull18 > 0, "cold hull deals");
            EVCHECK(grey21 == 0, "grey paint locked pre-arc");
            /* lawful arrivals split by cargo state */
            int dirty22 = 0, clean23 = 0, wrong = 0;
            g_player.cargo[16] = 3;
            for (int k = 0; k < 4000; k++) {
                const Event *e2 = events_roll_arrival(&si_law);
                if (e2 && e2->id == 23) wrong++;
                if (e2 && e2->id == 22) dirty22++;
            }
            g_player.cargo[16] = 0;
            for (int k = 0; k < 4000; k++) {
                const Event *e2 = events_roll_arrival(&si_law);
                if (e2 && e2->id == 22) wrong++;
                if (e2 && e2->id == 23) clean23++;
            }
            EVCHECK(wrong == 0 && dirty22 > 0 && clean23 > 0,
                    "patrol picks scan vs sweep by cargo");
        }

        /* continuity: the stowaway comes back if you let them ride */
        {
            events_init();
            int early = 0;
            for (int k = 0; k < 3000; k++) {
                const Event *e2 = events_roll_bar(&si_any, 0);
                if (e2 && e2->id == 26) early++;
            }
            EVCHECK(early == 0, "familiar face needs flag 1");
            const Event *st = NULL;
            FIND_EV(si_any, 4, st);
            EVCHECK(st != NULL, "stowaway reachable");
            if (st) {
                events_run_choice(st, 1);        /* LET THEM RIDE */
                const Event *ff = NULL;
                for (int k = 0; k < 30000 && !ff; k++) {
                    const Event *e2 = events_roll_bar(&si_any, 0);
                    if (e2 && e2->id == 26) ff = e2;
                }
                EVCHECK(ff != NULL, "familiar face returns");
            }
        }

        /* receipt + tangible outcomes (A+B) */
        {
            events_init();
            g_player.credits = 1000;
            g_player.fuel = 5.0f;
            g_ships[PLAYER].hull_max = 100.0f;
            g_ships[PLAYER].hull = 50.0f;
            /* clinic: rep AND a hull patch now, and the receipt says so */
            const Event *ev;
            FIND_EV(si_any, 8, ev);
            EVCHECK(ev != NULL, "clinic reachable");
            if (ev) {
                int8_t rep0 = g_rep[system_faction(si_any.addr)];
                events_run_choice(ev, 1);        /* FUND THE WARD */
                const EvReceipt *r = events_receipt();
                EVCHECK(r->cr == -100 && r->hull_pct == 25 &&
                        g_ships[PLAYER].hull > 50.0f &&
                        g_rep[system_faction(si_any.addr)] == rep0 + 3,
                        "fund ward: -100cr +25% hull +3 rep");
            }
            /* distress GIVE: fuel out now, credits at next dock */
            FIND_EV(si_any, 1, ev);
            if (ev) {
                events_run_choice(ev, 0);        /* GIVE THEM FUEL */
                const EvReceipt *r = events_receipt();
                EVCHECK(r->fuel < -0.9f && r->later_cr == 150 &&
                        *events_save_pending() == 150,
                        "give fuel: -1.0ly +150cr deferred");
                EVCHECK(events_pending_take() == 150 &&
                        *events_save_pending() == 0,
                        "pending transfer pays out once");
            }
            /* cold hull: hardware salvage lands in the rack eventually */
            int item_runs = 0, item_hits = 0;
            for (uint32_t s2 = 0; s2 < 400000u && item_runs < 40; s2++) {
                events_init(); events_set_salt(s2);
                const Event *e2 = events_roll_space(&si_any);
                if (!e2 || e2->id != 18) continue;
                item_runs++;
                memset(g_player.salvage, 0, sizeof g_player.salvage);
                for (int g = 0; g < N_GOODS; g++) g_player.cargo[g] = 0;
                events_run_choice(e2, 0);        /* STRIP THE HOLD */
                if (events_receipt()->item_type >= 0) {
                    item_hits++;
                    bool racked = false;
                    for (int sl = 0; sl < MAX_SALVAGE; sl++)
                        if (g_player.salvage[sl].in_use) racked = true;
                    if (!racked) item_hits = -999;   /* receipt lied */
                }
            }
            printf("[evtest] item runs=%d hardware=%d\n",
                   item_runs, item_hits);
            EVCHECK(item_runs == 40 && item_hits > 0,
                    "derelict strips can pay in hardware");
        }

        /* THE POLICY acts 2-3: ordered, gated, recurring faces */
        {
            events_init();
            g_player.credits = 5000;
            /* act 2 locked before act 1 completes */
            int early = 0;
            for (int k = 0; k < 2500; k++) {
                const Event *e2 = events_roll_bar(&si_any, 0);
                if (e2 && (e2->id == 28 || e2->id == 29)) early++;
                e2 = events_roll_dock(&si_any, 0);
                if (e2 && e2->id == 30) early++;
            }
            EVCHECK(early == 0, "act 2 waits for act 1");
            /* fast-forward act 1 */
            uint8_t *bits = events_save_bits();
            bits[(128 + 11) >> 3] |= 1 << ((128 + 11) & 7);   /* flag 11 */
            const Event *ev = NULL;
            for (int k = 0; k < 30000 && !ev; k++) {
                const Event *e2 = events_roll_bar(&si_any, 0);
                if (e2 && e2->id == 28) ev = e2;
            }
            EVCHECK(ev != NULL, "archivist appears post-act-1");
            uint32_t vessa = events_npc_seed();
            events_run_choice(ev, 0);            /* flag 14, lore 8 */
            EVCHECK(events_lore_seen(8), "act 2 step 1 reveals lore 8");
            ev = NULL;
            for (int k = 0; k < 30000 && !ev; k++) {
                const Event *e2 = events_roll_bar(&si_any, 0);
                if (e2 && e2->id == 29) ev = e2;
            }
            EVCHECK(ev && events_npc_seed() == vessa,
                    "vessa keeps her face");
            events_run_choice(ev, 0);            /* flag 15, lore 9 */
            ev = NULL;
            for (int k = 0; k < 30000 && !ev; k++) {
                const Event *e2 = events_roll_dock(&si_any, 0);
                if (e2 && e2->id == 30) ev = e2;
            }
            EVCHECK(ev != NULL, "beneficiary finale at dock");
            events_run_choice(ev, 0);            /* flag 16, lore 10 */
            EVCHECK(events_lore_seen(10), "act 2 complete");
            /* act 3 chain: arrival -> space -> dock climax */
            ev = NULL;
            for (int k = 0; k < 30000 && !ev; k++) {
                const Event *e2 = events_roll_arrival(&si_any);
                if (e2 && e2->id == 31) ev = e2;
            }
            EVCHECK(ev != NULL, "recall notice on arrival");
            events_run_choice(ev, 0);
            ev = NULL;
            for (int k = 0; k < 30000 && !ev; k++) {
                const Event *e2 = events_roll_space(&si_any);
                if (e2 && e2->id == 32) ev = e2;
            }
            EVCHECK(ev != NULL, "the audit at a derelict");
            events_run_choice(ev, 0);
            ev = NULL;
            for (int k = 0; k < 30000 && !ev; k++) {
                const Event *e2 = events_roll_dock(&si_any, 0);
                if (e2 && e2->id == 33) ev = e2;
            }
            EVCHECK(ev != NULL, "the terms reachable");
            events_run_choice(ev, 0);
            EVCHECK(events_lore_seen(13) && events_flag(19),
                    "campaign climax: the terms read");
            /* climax is oneshot — never again */
            int again = 0;
            for (int k = 0; k < 3000; k++) {
                const Event *e2 = events_roll_dock(&si_any, 0);
                if (e2 && e2->id == 33) again++;
            }
            EVCHECK(again == 0, "the terms never repeat");

            /* acts 4-5: the ledger bleeds -> the Collection -> the run */
            SystemInfo si_warf;
            bool got_wf = false;
            for (int sy = -20; sy <= 20 && !got_wf; sy++)
                for (int sx = -20; sx <= 20 && !got_wf; sx++) {
                    int ns2 = galaxy_sector_stars(sx, sy);
                    for (int i2 = 0; i2 < ns2 && !got_wf; i2++) {
                        SysAddr a2 = { sx, sy, (uint8_t)i2 };
                        SystemInfo s2;
                        galaxy_generate(a2, &s2);
                        if (s2.n_stations > 0 && mission_near_front(a2)) {
                            si_warf = s2;
                            got_wf = true;
                        }
                    }
                }
            EVCHECK(got_wf, "front system for act 4");
            const Event *e4 = NULL;
            for (int k = 0; k < 60000 && !e4; k++) {
                const Event *e2 = events_roll_dock(&si_warf, 0);
                if (e2 && e2->id == 45) e4 = e2;
            }
            EVCHECK(e4 != NULL, "act 4 opens at the front");
            events_run_choice(e4, 0);            /* flag 22, lore 18 */
            e4 = NULL;
            for (int k = 0; k < 30000 && !e4; k++) {
                const Event *e2 = events_roll_arrival(&si_any);
                if (e2 && e2->id == 46) e4 = e2;
            }
            EVCHECK(e4 != NULL, "the origination witnessed");
            events_run_choice(e4, 0);            /* flag 23 */
            e4 = NULL;
            for (int k = 0; k < 30000 && !e4; k++) {
                const Event *e2 = events_roll_bar(&si_any, 0);
                if (e2 && e2->id == 47) e4 = e2;
            }
            EVCHECK(e4 && events_npc_seed() == vessa,
                    "vessa explains the misprint");
            events_run_choice(e4, 0);            /* flag 24 */
            e4 = NULL;
            for (int k = 0; k < 30000 && !e4; k++) {
                const Event *e2 = events_roll_space(&si_any);
                if (e2 && e2->id == 48) e4 = e2;
            }
            EVCHECK(e4 != NULL, "the write-off found");
            events_run_choice(e4, 0);            /* flag 25 */
            e4 = NULL;
            for (int k = 0; k < 30000 && !e4; k++) {
                const Event *e2 = events_roll_space(&si_any);
                if (e2 && e2->id == 49) e4 = e2;
            }
            EVCHECK(e4 != NULL, "the threshold (Collection seen)");
            events_run_choice(e4, 0);            /* flag 31 */
            e4 = NULL;
            for (int k = 0; k < 30000 && !e4; k++) {
                const Event *e2 = events_roll_dock(&si_any, 0);
                if (e2 && e2->id == 50) e4 = e2;
            }
            EVCHECK(e4 != NULL, "the triage offered");
            events_run_choice(e4, 1);            /* REFUSE, flag 26 */
            e4 = NULL;
            for (int k = 0; k < 30000 && !e4; k++) {
                const Event *e2 = events_roll_bar(&si_any, 0);
                if (e2 && e2->id == 51) e4 = e2;
            }
            EVCHECK(e4 != NULL, "the network surfaces");
            events_run_choice(e4, 0);            /* flag 27 */
            e4 = NULL;
            for (int k = 0; k < 30000 && !e4; k++) {
                const Event *e2 = events_roll_dock(&si_any, 0);
                if (e2 && e2->id == 52) e4 = e2;
            }
            EVCHECK(e4 != NULL, "INDEMNITY RUN reachable");
            events_run_choice(e4, 1);            /* LAPSE */
            EVCHECK(events_flag(28) && events_lore_seen(26),
                    "lapsed: flag 28 + closing lore");
            /* the lapse survives the save round-trip (death honors it) */
            save_write(si_any.addr, 0, 0);
            events_init();
            SaveMeta meta2;
            EVCHECK(save_load(&meta2) && events_flag(28),
                    "lapse persists through the save");
        }

        /* save round-trip carries lore bits (v5) */
        {
            events_init();
            const Event *ev;
            FIND_EV(si_any, 7, ev);
            events_run_choice(ev, 0);        /* lore 5 set */
            g_player.legal = 0;
            EVCHECK(save_write(si_any.addr, 0, 0), "save_write v5");
            events_init();                   /* wipe live bits */
            EVCHECK(!events_lore_seen(5), "bits cleared pre-load");
            SaveMeta meta;
            EVCHECK(save_load(&meta) && events_lore_seen(5),
                    "lore bit survives save/load");
        }

        printf("[evtest] %s (%d failures)\n",
               fails ? "FAILED" : "ALL PASS", fails);
        return fails ? 1 : 0;
    }

    if (getenv("ELITE_FACEGALLERY")) {
        /* 4x4 portrait grid, one archetype row each, to eyeball variety
         * and confirm seed-stability (same seed = same face). */
        const char *path = getenv("ELITE_FACEGALLERY");
        uint32_t base = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0)
                                   : 42u;
        static const int kinds[4] = { NK_CIVILIAN, NK_OFFICIAL,
                                      NK_PIRATE, NK_MYSTIC };
        for (int i = 0; i < OUT_W * OUT_H; i++) g_fb[i] = 0;
        for (int row = 0; row < 4; row++)
            for (int col = 0; col < 4; col++)
                face_draw(g_fb, col * 32, row * 32, 32,
                          base + (uint32_t)(row * 4 + col) * 2654435761u,
                          kinds[row]);
        dump_ppm(path);
        return 0;
    }

#ifdef ELITE_STYLE_LAB
    if (getenv("ELITE_STYLELAB")) {
        /* Contact sheets: current vs proposed look for faces, planets,
         * space, ships and stations. NOTHING here changes the live
         * game — styles reset to 0 on the way out. */
        const char *pre = getenv("ELITE_STYLELAB");
        char path[300];
        uint32_t base = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0)
                                   : 42u;

        /* A) faces: 10x10, kinds cycling by row */
        static const int kinds[5] = { NK_CIVILIAN, NK_OFFICIAL, NK_PIRATE,
                                      NK_MYSTIC, NK_DOCKHAND };
        for (int st = 0; st <= 1; st++) {
            face_set_style(st);
            sheet_clear(2 + 10 * 34, 2 + 10 * 34);
            for (int i = 0; i < 100; i++) {
                int row = i / 10, col = i % 10;
                for (int p = 0; p < OUT_W * OUT_H; p++) g_fb[p] = 0;
                face_draw(g_fb, 0, 0, 32, base + (uint32_t)i * 2654435761u,
                          kinds[row % 5]);
                sheet_blit(g_fb, ELITE_FB_W, 32, 32,
                           2 + col * 34, 2 + row * 34, 1);
            }
            snprintf(path, sizeof path, "%s_faces_%s.ppm", pre,
                     st ? "new" : "cur");
            sheet_save(path);
        }
        face_set_style(0);

        /* B) planets: 6 types x 10 seeds, lit-sphere renders */
        for (int st = 0; st <= 1; st++) {
            r3d_planet_set_style(st);
            sheet_clear(2 + 6 * 46, 2 + 10 * 46);
            for (int row = 0; row < 10; row++) {
                SystemInfo si;
                memset(&si, 0, sizeof si);
                si.n_planets = 6;
                for (int t = 0; t < 6; t++) {
                    si.planets[t].type = (PlanetType)t;
                    si.planets[t].tex_seed =
                        base * 977u + (uint32_t)row * 131u +
                        (uint32_t)t * 7919u;
                    si.planets[t].radius_mm = 6.0f;
                    si.planets[t].orbit_mm = 1000.0f + t;
                    si.planets[t].station = -1;
                }
                r3d_planet_bake(&si);
                for (int t = 0; t < 6; t++) {
                    const uint8_t *tex;
                    const uint16_t *pal;
                    if (r3d_planet_art_peek(t, &tex, &pal))
                        sheet_planet_disc(tex, pal,
                                          2 + t * 46, 2 + row * 46, 21);
                }
            }
            snprintf(path, sizeof path, "%s_planets_%s.ppm", pre,
                     st ? "new" : "cur");
            sheet_save(path);
        }
        r3d_planet_set_style(0);

        /* C) space: open sky (starfield + nebula), current vs proposed,
         * two seeds x two view angles in one 2x2 sheet per style. */
        for (int st = 0; st <= 1; st++) {
            r3d_scene_set_style(st);
            sheet_clear(2 + 2 * 130, 2 + 2 * 130);
            for (int s2 = 0; s2 < 2; s2++) {
                uint32_t sd = base + (uint32_t)s2 * 7777u;
                r3d_starfield_init(sd);
                r3d_scene_set_nebula(sd * 31u, 0.85f);
                for (int ang = 0; ang < 2; ang++) {
                    Mat3 cam = m3_identity();
                    m3_rotate_local(&cam, 1, 0.9f + ang * 2.1f);
                    m3_rotate_local(&cam, 0, ang ? -0.5f : 0.25f);
                    r3d_scene_begin(&cam, 60.0f);
                    r3d_scene_raster(g_fb, 0, ELITE_FB_H);
                    sheet_blit(g_fb, ELITE_FB_W, 128, 128,
                               2 + ang * 130, 2 + s2 * 130, 1);
                }
            }
            snprintf(path, sizeof path, "%s_space_%s.ppm", pre,
                     st ? "new" : "cur");
            sheet_save(path);
        }
        r3d_scene_set_style(0);
        r3d_scene_set_nebula(0, 0);

        /* D) ships: 10 classes x 10 seeds, 96px cells (center crop) */
        r3d_scene_set_icon_bg(RGB565C(7, 9, 15));
        for (int st = 0; st <= 1; st++) {
            ship_gen_set_style(st);
            sheet_clear(2 + 10 * 98, 2 + 10 * 98);
            for (int cls = 0; cls < 10; cls++)
                for (int col = 0; col < 10; col++) {
                    const Mesh *m = ship_gen_mesh_class(
                        base + (uint32_t)col * 2654435761u +
                        (uint32_t)cls * 97u, cls);
                    sheet_render_mesh(m, 0.75f, 0.28f);
                    sheet_blit(g_fb + 16 * ELITE_FB_W + 16, ELITE_FB_W,
                               96, 96, 2 + col * 98, 2 + cls * 98, 1);
                }
            snprintf(path, sizeof path, "%s_ships_%s.ppm", pre,
                     st ? "new" : "cur");
            sheet_save(path);
        }
        ship_gen_set_style(0);

        /* E) stations: 3x3, full resolution */
        for (int st = 0; st <= 1; st++) {
            station_gen_set_style(st);
            sheet_clear(2 + 3 * 130, 2 + 3 * 130);
            for (int i = 0; i < 9; i++) {
                const Mesh *m =
                    station_gen_mesh(base * 31u + (uint32_t)i * 2654435761u);
                sheet_render_mesh(m, 0.6f, 0.35f);
                sheet_blit(g_fb, ELITE_FB_W, 128, 128,
                           2 + (i % 3) * 130, 2 + (i / 3) * 130, 1);
            }
            snprintf(path, sizeof path, "%s_stations_%s.ppm", pre,
                     st ? "new" : "cur");
            sheet_save(path);
        }
        station_gen_set_style(0);
        r3d_scene_set_icon_bg(0);
        return 0;
    }
#endif

#ifdef ELITE_STYLE_LAB
    if (getenv("ELITE_BANDPAN")) {
        /* Disc-asymmetry audit: 6 systems x 4 compass headings of the
         * SAME sky — the band should swell toward the core and thin
         * to a whisper on the anti-core side. */
        const char *path = getenv("ELITE_BANDPAN");
        sheet_clear(2 + 4 * 130, 2 + 6 * 130);
        for (int r = 0; r < 6; r++) {
            uint32_t sd = 7777777u * (uint32_t)(r + 1) + 311u;
            r3d_starfield_init(sd);
            r3d_scene_set_nebula(sd, 0.0f);     /* band only */
            for (int c2 = 0; c2 < 4; c2++) {
                Mat3 cam = m3_identity();
                m3_rotate_local(&cam, 1, (float)c2 * 1.5708f);
                r3d_scene_begin(&cam, 60.0f);
                r3d_scene_raster(g_fb, 0, ELITE_FB_H);
                sheet_blit(g_fb, ELITE_FB_W, 128, 128,
                           2 + c2 * 130, 2 + r * 130, 1);
            }
        }
        sheet_save(path);
        r3d_scene_set_nebula(0, 0);
        return 0;
    }

    if (getenv("ELITE_GUIDESHOT")) {
        /* Regenerate the pilot-guide screenshots that changed look or
         * are new features. Writes 128x128 frames scaled x3 PNG-ready
         * PPMs straight under docs/img via a scratch .ppm + convert. */
        const char *root = "/home/maustin/thumby-color/ThumbyElite/docs/img";
        char cmd[512], ppm[256];
        #define GSAVE(name) do { \
            snprintf(ppm, sizeof ppm, "/tmp/gs_%s.ppm", name); \
            dump_ppm(ppm); \
            snprintf(cmd, sizeof cmd, \
                "convert %s -scale 300%% %s/%s.png", ppm, root, name); \
            if (system(cmd) != 0) printf("[guideshot] convert failed %s\n", name); \
            else printf("[guideshot] %s/%s.png\n", root, name); \
        } while (0)

        events_set_chance(0);                /* no surprise hails mid-capture */
        remove("indemnityrun.sav");
        /* Enter real flight: title -> NEW GAME -> skip the intro crawl. */
        #define ENTER_FLIGHT() do { \
            CraftRawButtons _n = {0}, _b; float _dt = 1.0f/30.0f; \
            elite_game_init(42); \
            for (int k=0;k<4;k++) elite_game_tick(&_n,_dt); \
            _b=_n; _b.down=true; elite_game_tick(&_b,_dt); \
            elite_game_tick(&_n,_dt); \
            _b=_n; _b.a=true; elite_game_tick(&_b,_dt);  /* NEW GAME */ \
            elite_game_tick(&_n,_dt); \
            _b=_n; _b.a=true; elite_game_tick(&_b,_dt);  /* skip crawl */ \
            for (int k=0;k<30;k++) elite_game_tick(&_n,_dt); \
        } while (0)

        /* find a near-front station system */
        SystemInfo si_front; bool gotf = false;
        for (int sy = -20; sy <= 20 && !gotf; sy++)
            for (int sx = -20; sx <= 20 && !gotf; sx++) {
                int ns = galaxy_sector_stars(sx, sy);
                for (int i = 0; i < ns && !gotf; i++) {
                    SysAddr a = { sx, sy, (uint8_t)i };
                    galaxy_generate(a, &si_front);
                    if (si_front.n_stations > 0 && mission_near_front(a))
                        gotf = true;
                }
            }
        /* war_battle: in flight, grant + jump + anchor the beacon. */
        if (gotf) {
            ENTER_FLIGHT();
            for (int f = 0; f < N_FACTIONS; f++) g_rep[f] = 25;
            if (mission_grant_warzone(&si_front)) {
                Mission *m = NULL;
                for (int i = 0; i < MAX_MISSIONS; i++)
                    if (g_missions[i].type == MIS_WARZONE) m = &g_missions[i];
                if (m) {
                    elite_game_debug_jump(m->target);
                    elite_game_debug_goto_poi(0);
                    CraftRawButtons none = {0};
                    for (int k = 0; k < 20; k++)
                        elite_game_tick(&none, 1.0f/30.0f);
                    render_frame();
                    GSAVE("war_battle");
                }
            }
        }

        /* war_contract: dock at the front station and open MISSIONS with
         * a guaranteed war offer (g_force_war_offer). Mirrors the BARSHOT
         * dock path: enter the system, station_open, drive the real UI. */
        if (gotf) {
            extern int g_force_war_offer;
            galaxy_set_seed(42);
            player_init();
            for (int f = 0; f < N_FACTIONS; f++) g_rep[f] = 25;
            system_enter(si_front.addr);
            g_force_war_offer = 1;
            station_open(0);
            CraftRawButtons none = {0}, b;
            float dt2 = 1.0f / 30.0f;
            station_tick(&none, dt2);            /* release debounce */
            for (int k = 0; k < 3; k++) {        /* home cursor -> MISSIONS */
                b = none; b.down = true; station_tick(&b, dt2);
                station_tick(&none, dt2);
            }
            b = none; b.a = true; station_tick(&b, dt2);  /* enter MISSIONS */
            station_tick(&none, dt2);
            station_draw(g_fb);
            g_force_war_offer = 0;
            GSAVE("war_contract");
        }

        /* station_home: the docked services menu (home screen) */
        if (gotf) {
            galaxy_set_seed(42);
            player_init();
            system_enter(si_front.addr);
            station_open(0);
            CraftRawButtons none = {0};
            station_tick(&none, 1.0f/30.0f);
            station_draw(g_fb);
            GSAVE("station_home");
        }

        /* civ_lock: a lone civilian hauler locked (green friend-or-foe) */
        {
            extern const Mesh *hull_mesh(uint32_t, int);
            ENTER_FLIGHT();
            CraftRawButtons none = {0}, b;
            for (int k = 0; k < 100; k++)         /* let any leftover toast expire */
                elite_game_tick(&none, 1.0f/30.0f);
            for (int i = 1; i < MAX_SHIPS; i++) g_ships[i].alive = false;
            Ship *pl = &g_ships[0];
            pl->pos = v3(0, 0, 0);
            pl->basis = m3_identity();
            pl->vel = v3(0, 0, 0);
            int e = ship_spawn(hull_mesh(0x5EED1u, 6), v3(34, 20, 190),
                               TEAM_NEUTRAL);
            if (e > 0) {
                g_ships[e].is_civilian = 1;
                g_ships[e].team = TEAM_NEUTRAL;
                g_ships[e].civ_kind = 1;
            }
            b = none; b.lb = true; elite_game_tick(&b, 1.0f/30.0f);  /* lock */
            for (int k = 0; k < 6; k++) elite_game_tick(&none, 1.0f/30.0f);
            render_frame();
            GSAVE("civ_lock");
        }

        /* flight: a clean in-flight beat with the new sky. Long settle so
         * any arrival toast expires (toasts leaked into earlier shots). */
        {
            ENTER_FLIGHT();
            CraftRawButtons none = {0};
            for (int k = 0; k < 120; k++) elite_game_tick(&none, 1.0f/30.0f);
            render_frame();
            GSAVE("flight");
        }

        /* planet_approach: seek a RINGED planet and frame it close */
        {
            int rp = -1; SysAddr ra = {0,0,0};
            for (int sy = -12; sy <= 12 && rp < 0; sy++)
                for (int sx = -12; sx <= 12 && rp < 0; sx++) {
                    int ns = galaxy_sector_stars(sx, sy);
                    for (int i = 0; i < ns && rp < 0; i++) {
                        SysAddr a = { sx, sy, (uint8_t)i };
                        SystemInfo si; galaxy_generate(a, &si);
                        for (int p = 0; p < si.n_planets; p++)
                            if (si.planets[p].rings) {
                                ra = a; rp = p; break;
                            }
                    }
                }
            ENTER_FLIGHT();
            CraftRawButtons none = {0};
            for (int k = 0; k < 120; k++) elite_game_tick(&none, 1.0f/30.0f);
            if (rp >= 0) {
                elite_game_debug_jump(ra);
                elite_game_debug_view_planet(rp);
            }
            for (int k = 0; k < 120; k++) elite_game_tick(&none, 1.0f/30.0f);
            render_frame();
            GSAVE("planet_approach");
        }
        #undef ENTER_FLIGHT
        #undef GSAVE
        return 0;
    }

    if (getenv("ELITE_MAULSHOT")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        extern int g_force_gunship;
        (void)system("mkdir -p /tmp/sg");
        galaxy_set_seed(42);
        g_force_gunship = 1;
        char p[64];
        for (int i = 0; i < 12; i++) {
            const Mesh *m = hull_mesh(0xBEEF01u + (uint32_t)i * 2654435761u, 5);
            sheet_render_mesh(m, 3.9f - 0.0f, 0.34f);
            snprintf(p, sizeof p, "/tmp/sg/m%02d.ppm", i);
            dump_ppm(p);
        }
        g_force_gunship = 0;
        printf("[maulshot] wrote 12\n");
        return 0;
    }

    if (getenv("ELITE_STNFAM")) {
        /* Contact sheet for the 4 candidate station families that replace
         * the rejected cuboid drydock. Each family x 6 varied seeds ->
         * /tmp/sf/f<fam>_<col>.ppm; a shell montage labels the rows. */
        extern int g_force_station_fam;
        (void)system("mkdir -p /tmp/sf");
        station_gen_set_style(1);
        r3d_scene_set_icon_bg(RGB565C(7, 9, 15));
        char p[64];
        for (int fam = 0; fam < 4; fam++) {
            g_force_station_fam = fam;
            for (int col = 0; col < 6; col++) {
                const Mesh *m = station_gen_mesh(0x5747A1u
                    + (uint32_t)col * 2654435761u + (uint32_t)fam * 131u);
                sheet_render_mesh(m, 0.6f, 0.35f);
                snprintf(p, sizeof p, "/tmp/sf/f%d_%d.ppm", fam, col);
                dump_ppm(p);
            }
        }
        g_force_station_fam = -1;
        r3d_scene_set_icon_bg(0);
        station_gen_set_style(0);
        printf("[stnfam] wrote 24 cells\n");
        return 0;
    }

    if (getenv("ELITE_STNSHOW")) {
        /* Station-exterior showcase for the pilot guide: 12 random
         * style-1 stations over the live sky (the natural archetype
         * mix). Dumps /tmp/ss/s<NN>.ppm; a shell montage assembles it. */
        (void)system("mkdir -p /tmp/ss");
        station_gen_set_style(1);
        char p[64];
        for (int i = 0; i < 12; i++) {
            uint32_t sd = 0xA17E5u + (uint32_t)i * 2654435761u;
            r3d_starfield_init(sd);
            r3d_scene_set_nebula(sd * 31u, 0.85f);
            const Mesh *m = station_gen_mesh(sd);
            sheet_render_mesh(m, 0.6f + 0.12f * (float)(i % 3), 0.35f);
            snprintf(p, sizeof p, "/tmp/ss/s%02d.ppm", i);
            dump_ppm(p);
        }
        r3d_scene_set_nebula(0, 0);
        station_gen_set_style(0);
        printf("[stnshow] wrote 12\n");
        return 0;
    }

    if (getenv("ELITE_SHIPGRID")) {
        /* Per-class catalogue cells for the guide's ship grids. Dumps
         * /tmp/sg/c<cls>_<col>.ppm (10 classes x 4 varied seeds); a
         * shell montage assembles the two labelled grids. */
        extern const Mesh *hull_mesh(uint32_t, int);
        (void)system("mkdir -p /tmp/sg");
        galaxy_set_seed(42);
        char p[64];
        for (int cls = 0; cls < 10; cls++)
            for (int col = 0; col < 4; col++) {
                const Mesh *m = hull_mesh(0xC0FFEEu + (uint32_t)col * 2654435761u
                                          + (uint32_t)cls * 97u, cls);
                /* 3/4 view coming TOWARDS the camera (user): nose
                 * rotated to face us, slight top-down. */
                sheet_render_mesh(m, 3.9f - 0.30f * col, 0.40f);
                snprintf(p, sizeof p, "/tmp/sg/c%d_%d.ppm", cls, col);
                dump_ppm(p);
            }
        printf("[shipgrid] wrote 40 cells\n");
        return 0;
    }

    if (getenv("ELITE_GAMEPLAYVIDEO")) {
        /* A montage of the current build for the pilot guide: title ->
         * intro -> flight -> handling a random event -> a faction
         * battle. Frames -> /tmp/gvid for ffmpeg. Driven through the
         * real game where practical; the event modal is drawn directly
         * (the same UI the game uses). */
        events_set_chance(0);
        remove("indemnityrun.sav");
        int fr = 0; char path[96];
        CraftRawButtons none = {0}, b;
        const float dt = 1.0f / 30.0f;
        #define VDUMP() do { \
            snprintf(path, sizeof path, "/tmp/gvid/%04d.ppm", fr++); \
            dump_ppm(path); } while (0)

        elite_game_init(42);
        for (int k = 0; k < 4; k++) elite_game_tick(&none, dt);
        /* 1. title (~2.5s) */
        for (int k = 0; k < 75; k++) { elite_game_tick(&none, dt);
            render_frame(); VDUMP(); }
        /* 2. NEW GAME + intro crawl (~3s) */
        b = none; b.down = true; elite_game_tick(&b, dt);
        elite_game_tick(&none, dt);
        b = none; b.a = true; elite_game_tick(&b, dt);
        for (int k = 0; k < 95; k++) { elite_game_tick(&none, dt);
            render_frame(); VDUMP(); }
        b = none; b.a = true; elite_game_tick(&b, dt);   /* into flight */
        for (int k = 0; k < 4; k++) elite_game_tick(&none, dt);
        /* 3. flight: throttle up, a little traffic (~4s) */
        for (int k = 0; k < 60; k++) { b = none; b.up = true;
            elite_game_tick(&b, dt); render_frame(); VDUMP(); }
        elite_game_debug_spawn(2);
        for (int k = 0; k < 60; k++) { elite_game_tick(&none, dt);
            render_frame(); VDUMP(); }
        /* 4. handling a random event — the distress hail modal, for real */
        {
            galaxy_set_seed(42);
            SystemInfo si; bool got = false;
            for (int sy = -14; sy <= 14 && !got; sy++)
                for (int sx = -14; sx <= 14 && !got; sx++) {
                    int ns = galaxy_sector_stars(sx, sy);
                    for (int i = 0; i < ns && !got; i++) {
                        SysAddr a = { sx, sy, (uint8_t)i };
                        galaxy_generate(a, &si);
                        if (si.n_stations > 0) got = true;
                    }
                }
            g_player.fuel = 5.0f; g_player.credits = 1200;
            events_set_chance(100);              /* the roll was suppressed */
            const Event *ev = NULL;
            for (uint32_t s2 = 0; s2 < 200000u && !ev; s2++) {
                events_init(); events_set_salt(s2);
                const Event *e2 = events_roll_dock(&si, 0);
                if (e2 && e2->id == 1) ev = e2;   /* DISTRESS HAIL */
            }
            events_set_chance(0);
            if (ev) {
                ui_event_open(ev);
                for (int k = 0; k < 55; k++) { ui_event_tick(&none, dt);
                    for (int i=0;i<OUT_W*OUT_H;i++) g_fb[i]=RGB565C(8,10,18);
                    ui_event_draw(g_fb); VDUMP(); }
                b = none; b.down = true; ui_event_tick(&b, dt);
                for (int k = 0; k < 18; k++) { ui_event_tick(&none, dt);
                    for (int i=0;i<OUT_W*OUT_H;i++) g_fb[i]=RGB565C(8,10,18);
                    ui_event_draw(g_fb); VDUMP(); }
                b = none; b.a = true; ui_event_tick(&b, dt);
                for (int k = 0; k < 55; k++) { ui_event_tick(&none, dt);
                    for (int i=0;i<OUT_W*OUT_H;i++) g_fb[i]=RGB565C(8,10,18);
                    ui_event_draw(g_fb); VDUMP(); }
            }
        }
        /* 5. a faction battle (~4s) */
        {
            events_set_chance(0);
            remove("indemnityrun.sav");
            elite_game_init(42);
            for (int k = 0; k < 4; k++) elite_game_tick(&none, dt);
            b = none; b.down = true; elite_game_tick(&b, dt);
            elite_game_tick(&none, dt);
            b = none; b.a = true; elite_game_tick(&b, dt);
            elite_game_tick(&none, dt);
            b = none; b.a = true; elite_game_tick(&b, dt);
            for (int k = 0; k < 20; k++) elite_game_tick(&none, dt);
            for (int f = 0; f < N_FACTIONS; f++) g_rep[f] = 25;
            SystemInfo si2; bool gf = false;
            for (int sy = -20; sy <= 20 && !gf; sy++)
                for (int sx = -20; sx <= 20 && !gf; sx++) {
                    int ns = galaxy_sector_stars(sx, sy);
                    for (int i = 0; i < ns && !gf; i++) {
                        SysAddr a = { sx, sy, (uint8_t)i };
                        galaxy_generate(a, &si2);
                        if (si2.n_stations > 0 && mission_near_front(a)) gf = true;
                    }
                }
            if (gf && mission_grant_warzone(&si2)) {
                Mission *m = NULL;
                for (int i = 0; i < MAX_MISSIONS; i++)
                    if (g_missions[i].type == MIS_WARZONE) m = &g_missions[i];
                if (m) {
                    elite_game_debug_jump(m->target);
                    elite_game_debug_goto_poi(0);
                    for (int k = 0; k < 120; k++) {
                        b = none; if (k > 20 && k < 60) b.up = true;
                        elite_game_tick(&b, dt); render_frame(); VDUMP();
                    }
                }
            }
        }
        printf("[gvid] wrote %d frames\n", fr);
        #undef VDUMP
        return 0;
    }

    if (getenv("ELITE_SKYBENCH")) {
        /* Microbench the per-frame sky cost. Host CPU is far faster than
         * the RP2350, but the RELATIVE breakdown transfers — it shows
         * which layer eats the frame. */
        extern int g_sky_band, g_sky_clouds, g_sky_galaxies;
        galaxy_set_seed(42);
        Mat3 cam = m3_identity();
        m3_rotate_local(&cam, 1, 0.7f);
        const int N = 4000;
        struct { const char *name; int band, clouds, gal, style; uint32_t neb; } cfg[] = {
            { "black (memset, style 0)", 0, 0, 0, 0, 0 },
            { "band only",               1, 0, 0, 1, 909091u },
            { "galaxies only",           0, 0, 1, 1, 0 },
            { "FULL (band + galaxies)",  1, 0, 1, 1, 909091u },
        };  /* clouds removed 2026-06-12 — g_sky_clouds is now a no-op */
        printf("[skybench] %d iters/config, 128x128, worst-case seed\n", N);
        for (unsigned c = 0; c < sizeof cfg / sizeof cfg[0]; c++) {
            g_sky_band = cfg[c].band;
            g_sky_clouds = cfg[c].clouds;
            g_sky_galaxies = cfg[c].gal;
            r3d_scene_set_style(cfg[c].style);
            r3d_starfield_init(909091u);
            r3d_scene_set_nebula(cfg[c].neb, cfg[c].neb ? 0.85f : 0.0f);
            Uint64 t0 = SDL_GetPerformanceCounter();
            for (int i = 0; i < N; i++) {
                Mat3 c2 = cam;
                m3_rotate_local(&c2, 1, (float)i * 0.0003f);
                r3d_scene_begin(&c2, 60.0f);
                r3d_scene_raster(g_fb, 0, ELITE_FB_H);
            }
            Uint64 t1 = SDL_GetPerformanceCounter();
            double us = (double)(t1 - t0) * 1e6 /
                        (double)SDL_GetPerformanceFrequency() / N;
            printf("[skybench] %-26s %7.2f us/frame\n", cfg[c].name, us);
        }
        g_sky_band = g_sky_clouds = g_sky_galaxies = 1;
        return 0;
    }

    if (getenv("ELITE_GALSHOT")) {
        /* zoomed look at each background galaxy of a few seeds */
        const char *path = getenv("ELITE_GALSHOT");
        sheet_clear(2 + 4 * 130, 2 + 3 * 130);
        for (int r = 0; r < 3; r++) {
            uint32_t sd = 909091u * (uint32_t)(r + 1) + 7u;
            r3d_starfield_init(sd);
            r3d_scene_set_nebula(sd, 0.0f);
            for (int i = 0; i < 4; i++) {
                Vec3 gd;
                if (!r3d_scene_galaxy_dir(i, &gd)) continue;
                /* camera looking straight down gd, zoomed (fov 18) */
                Mat3 cam;
                Vec3 ref = (gd.y > 0.9f || gd.y < -0.9f) ? v3(1, 0, 0)
                                                         : v3(0, 1, 0);
                cam.r[2] = gd;
                cam.r[0] = v3_norm(v3_cross(ref, gd));
                cam.r[1] = v3_cross(gd, cam.r[0]);
                r3d_scene_begin(&cam, 18.0f);
                r3d_scene_raster(g_fb, 0, ELITE_FB_H);
                sheet_blit(g_fb, ELITE_FB_W, 128, 128,
                           2 + i * 130, 2 + r * 130, 1);
            }
        }
        sheet_save(path);
        r3d_scene_set_nebula(0, 0);
        return 0;
    }

    if (getenv("ELITE_BANDSHEET")) {
        /* 100 skies, one per seed: band orientation/width/gain/core +
         * nebula clouds on every third — the variety audit. */
        const char *path = getenv("ELITE_BANDSHEET");
        sheet_clear(2 + 10 * 98, 2 + 10 * 98);
        for (int i = 0; i < 100; i++) {
            uint32_t sd = 1000003u * (uint32_t)(i + 1) + 17u;
            r3d_starfield_init(sd);
            r3d_scene_set_nebula(sd, (i % 3 == 0) ? 0.85f : 0.0f);
            Mat3 cam = m3_identity();
            m3_rotate_local(&cam, 1, (float)(i * 37 % 360) * 0.01745f);
            m3_rotate_local(&cam, 0, ((float)(i % 7) - 3.0f) * 0.12f);
            r3d_scene_begin(&cam, 60.0f);
            r3d_scene_raster(g_fb, 0, ELITE_FB_H);
            sheet_blit(g_fb + 16 * ELITE_FB_W + 16, ELITE_FB_W, 96, 96,
                       2 + (i % 10) * 98, 2 + (i / 10) * 98, 1);
        }
        sheet_save(path);
        r3d_scene_set_nebula(0, 0);
        return 0;
    }
#endif

    if (getenv("ELITE_WARTEST")) {
        /* Faction war: contested detection, offers, grant, quota,
         * rep swing, and the real beacon battle via the debug jump. */
        int fails = 0;
#define WCHECK(cond, name) do { \
        int ok_ = !!(cond); \
        printf("[wartest] %-34s %s\n", name, ok_ ? "PASS" : "FAIL"); \
        if (!ok_) fails++; } while (0)
        galaxy_set_seed(42);
        player_init();
        missions_init();
        events_init();
        /* find a near-front system with a station */
        SystemInfo si_front;
        bool got = false;
        for (int sy = -20; sy <= 20 && !got; sy++)
            for (int sx = -20; sx <= 20 && !got; sx++) {
                int ns = galaxy_sector_stars(sx, sy);
                for (int i = 0; i < ns && !got; i++) {
                    SysAddr a = { sx, sy, (uint8_t)i };
                    SystemInfo si;
                    galaxy_generate(a, &si);
                    if (si.n_stations > 0 && mission_near_front(a)) {
                        si_front = si;
                        got = true;
                    }
                }
            }
        WCHECK(got, "near-front station system found");
        /* contested coherence: an enemy is never your own faction */
        {
            Faction en;
            int bad = 0, n = 0;
            for (int sy = -20; sy <= 20; sy++)
                for (int sx = -20; sx <= 20; sx++) {
                    SysAddr a = { sx, sy, 0 };
                    if (!faction_contested(a, &en)) continue;
                    n++;
                    if (en == system_faction(a)) bad++;
                }
            printf("[wartest] contested systems in scan: %d\n", n);
            WCHECK(n > 0 && bad == 0, "fronts exist, enemy != own side");
        }
        /* offers near the front include WAR contracts — but only for
         * proven friends (rep gate), and the tier ladder needs rep */
        {
            Mission offers[MISSION_OFFERS];
            int war = -1;
            /* rep 0: the faction doesn't trust you with war work */
            for (int v = 0; v < 40; v++) {
                mission_on_docked(&si_front, 0);
                mission_make_offers(&si_front, 0, offers);
                for (int i = 0; i < MISSION_OFFERS; i++)
                    if (offers[i].type == MIS_WARZONE) war = i;
            }
            WCHECK(war < 0, "no war contracts at rep 0");
            /* rep 25: full ladder open */
            for (int f = 0; f < N_FACTIONS; f++) g_rep[f] = 25;
            war = -1;
            for (int v = 0; v < 40 && war < 0; v++) {
                mission_on_docked(&si_front, 0);
                mission_make_offers(&si_front, 0, offers);
                for (int i = 0; i < MISSION_OFFERS; i++)
                    if (offers[i].type == MIS_WARZONE) war = i;
                if (war >= 0) {
                    WCHECK(mission_accept(&offers[war]),
                           "war contract accepted");
                }
            }
            WCHECK(war >= 0, "trusted pilots get WAR contracts");
        }
        /* the battle: jump to the zone, anchor the beacon, count sides */
        {
            Mission *m = NULL;
            for (int i = 0; i < MAX_MISSIONS; i++)
                if (g_missions[i].type == MIS_WARZONE) m = &g_missions[i];
            WCHECK(m != NULL, "contract in the log");
            if (m) {
                int quota = m->count;
                elite_game_debug_jump(m->target);
                elite_game_debug_goto_poi(0);       /* beacon */
                int allies = 0, hostiles = 0, tagged = 0;
                for (int i = 1; i < MAX_SHIPS; i++) {
                    if (!g_ships[i].alive) continue;
                    if (g_ships[i].war_fac) tagged++;
                    if (g_ships[i].team == TEAM_HOSTILE) hostiles++;
                    if (g_ships[i].team == TEAM_NEUTRAL &&
                        g_ships[i].is_police) allies++;
                }
                printf("[wartest] battle: %d allies vs %d hostiles, "
                       "quota %d, faction-tagged %d\n",
                       allies, hostiles, quota, tagged);
                int wt = mission_warzone_tier(m->target);
                printf("[wartest] tier %d (%s), reward %d\n",
                       wt, k_tier_names[wt < 0 ? 0 : wt], (int)m->reward);
                WCHECK(hostiles == quota,
                       "whole enemy force spawns up front");
                WCHECK(allies == 3 + (wt >= 2) + (wt >= 4),
                       "allied wing scales with tier");
                /* blended ranks: ceiling = leader only; the grunt
                 * wall steps down (no all-ELITE force) */
                int at_max = 0, ldr_rank = wt < 4 ? wt + 1 : 4;
                for (int i = 1; i < MAX_SHIPS; i++)
                    if (g_ships[i].alive &&
                        g_ships[i].team == TEAM_HOSTILE &&
                        g_ships[i].tier >= ldr_rank) at_max++;
                printf("[wartest] enemies at leader rank: %d of %d\n",
                       at_max, hostiles);
                WCHECK(at_max <= 2, "ranks blend, no wall of aces");
                static const int k_lo[5] = { 2000, 3200, 5000, 7500, 19000 };
                static const int k_hi[5] = { 3500, 5500, 8500, 12500, 27000 };
                WCHECK(wt >= 0 && m->reward >= k_lo[wt] &&
                       m->reward <= k_hi[wt],
                       "pay matches the tier band");
                WCHECK(tagged == allies + hostiles,
                       "every combatant carries a faction tag");
                /* the zone counts ANY war kill (allies too) */
                for (int k = 0; k < quota; k++)
                    mission_warzone_enemy_down();
                WCHECK(m->done && m->count == 0,
                       "zone won when the force is dead");
                /* payday: rep swings BOTH ways (enemy faction comes
                 * from the front itself — tier is intensity now) */
                Faction wen = (Faction)0;
                faction_contested(m->target, &wen);
                int8_t own0 = g_rep[m->faction], en0 = g_rep[wen];
                int32_t cr0 = g_player.credits;
                SystemInfo zi;
                galaxy_generate(m->target, &zi);
                int paid = mission_collect(&zi, 0);
                WCHECK(paid > 0 && g_player.credits == cr0 + paid &&
                       g_rep[m->faction] == own0 + 7 &&
                       g_rep[wen] == en0 - 7,
                       "war pay + rep swing both ways");
            }
        }
        printf("[wartest] %s (%d failures)\n",
               fails ? "FAILED" : "ALL PASS", fails);
        return fails ? 1 : 0;
    }

    if (getenv("ELITE_BARSHOT") || getenv("ELITE_CODEXSHOT")) {
        /* Drive the station UI headless: bar encounter row + modal, or
         * the DATABASE list + read view. */
        const char *barp = getenv("ELITE_BARSHOT");
        const char *codp = getenv("ELITE_CODEXSHOT");
        events_set_chance(100);
        galaxy_set_seed(42);
        player_init();
        g_player.credits = 1000;
        SystemInfo si;
        bool got = false;
        for (int sy = -14; sy <= 14 && !got; sy++)
            for (int sx = -14; sx <= 14 && !got; sx++) {
                int ns = galaxy_sector_stars(sx, sy);
                for (int i = 0; i < ns && !got; i++) {
                    SysAddr a = { sx, sy, (uint8_t)i };
                    galaxy_generate(a, &si);
                    if (si.n_stations > 0) got = true;
                }
            }
        if (!got) { printf("[barshot] no system\n"); return 1; }
        system_enter(si.addr);
        events_init();
        if (codp) {
            /* unlock two fragments so the list has content (bit layout:
             * lore 0..127 lives at the front of the save bits) */
            uint8_t *bits = events_save_bits();
            bits[0] |= (1u << 0) | (1u << 5);
        }
        station_open(0);
        CraftRawButtons none = {0}, b = {0};
        float dt2 = 1.0f / 30.0f;
        station_tick(&none, dt2);                /* release the debounce */
        char p1[256];
        station_draw(g_fb);                      /* home menu, for layout */
        snprintf(p1, sizeof p1, "%s_home.ppm", codp ? codp : barp);
        dump_ppm(p1);
        int moves = codp ? 6 : 4;                /* DATABASE row vs BAR */
        for (int k = 0; k < moves; k++) {
            b = none; b.down = true;
            station_tick(&b, dt2);
            station_tick(&none, dt2);
        }
        b = none; b.a = true;
        DockAction act = station_tick(&b, dt2);
        station_tick(&none, dt2);
        station_draw(g_fb);
        snprintf(p1, sizeof p1, "%s_list.ppm", codp ? codp : barp);
        dump_ppm(p1);
        b = none; b.a = true;
        act = station_tick(&b, dt2);
        station_tick(&none, dt2);
        if (codp) {
            station_draw(g_fb);                  /* read view */
            snprintf(p1, sizeof p1, "%s_read.ppm", codp);
            dump_ppm(p1);
        } else if (act == DOCK_EVENT) {
            const Event *ev = station_pending_event();
            if (ev) {
                ui_event_open(ev);
                ui_event_tick(&none, dt2);
                for (int i = 0; i < OUT_W * OUT_H; i++)
                    g_fb[i] = RGB565C(8, 10, 18);
                ui_event_draw(g_fb);
                snprintf(p1, sizeof p1, "%s_modal.ppm", barp);
                dump_ppm(p1);
            }
        } else {
            printf("[barshot] no DOCK_EVENT (act=%d)\n", (int)act);
        }
        return 0;
    }

    if (getenv("ELITE_EVENTSHOT")) {
        /* Render the modal both phases: <path>_choose.ppm + _result.ppm */
        const char *path = getenv("ELITE_EVENTSHOT");
        int want = 1;
        if (getenv("ELITE_EVENTID")) want = atoi(getenv("ELITE_EVENTID"));
        galaxy_set_seed(42);
        player_init();
        g_player.credits = 1000;
        g_player.fuel = 5.0f;
        g_player.cargo[16] = 3;
        SystemInfo si, si_fall;
        bool got = false, fall = false;
        for (int sy = -14; sy <= 14 && !got; sy++)
            for (int sx = -14; sx <= 14 && !got; sx++) {
                int ns = galaxy_sector_stars(sx, sy);
                for (int i = 0; i < ns && !got; i++) {
                    SysAddr a = { sx, sy, (uint8_t)i };
                    galaxy_generate(a, &si);
                    if (si.n_stations == 0) continue;
                    if (!fall) { si_fall = si; fall = true; }
                    if (si.gov >= GOV_CONFED && si.threat >= 2)
                        got = true;
                }
            }
        if (!got && fall) si = si_fall;          /* any station system */
        if (!got && !fall) { printf("[evshot] no system\n"); return 1; }
        int wtrig = TRIG_DOCK;
        for (int i = 0; i < k_n_events; i++)
            if (k_events[i].id == want) wtrig = k_events[i].trig;
        const Event *ev = NULL;
        for (uint32_t s2 = 0; s2 < 200000u && !ev; s2++) {
            events_init(); events_set_salt(s2);
            const Event *e2 =
                (wtrig == TRIG_BAR)     ? events_roll_bar(&si, 0)
              : (wtrig == TRIG_SPACE)   ? events_roll_space(&si)
              : (wtrig == TRIG_ARRIVAL) ? events_roll_arrival(&si)
                                        : events_roll_dock(&si, 0);
            if (e2 && e2->id == want) ev = e2;
        }
        if (!ev) { printf("[evshot] event %d not reachable\n", want); return 1; }
        ui_event_open(ev);
        CraftRawButtons none = {0}, ba = {0};
        ba.a = true;
        ui_event_tick(&none, 1.0f / 30.0f);
        char p1[256];
        for (int i = 0; i < OUT_W * OUT_H; i++) g_fb[i] = RGB565C(8, 10, 18);
        ui_event_draw(g_fb);
        snprintf(p1, sizeof p1, "%s_choose.ppm", path);
        dump_ppm(p1);
        ui_event_tick(&ba, 1.0f / 30.0f);    /* select -> aftermath */
        for (int i = 0; i < OUT_W * OUT_H; i++) g_fb[i] = RGB565C(8, 10, 18);
        ui_event_draw(g_fb);
        snprintf(p1, sizeof p1, "%s_result.ppm", path);
        dump_ppm(p1);
        return 0;
    }

    if (getenv("ELITE_DEMO") || getenv("ELITE_FIRETEST") ||
        getenv("ELITE_KILLTEST") || getenv("ELITE_TRAVELTEST") ||
        getenv("ELITE_TRADETEST") || getenv("ELITE_JUMPTEST") ||
        getenv("ELITE_LOOTTEST") || getenv("ELITE_SHOPTEST") ||
        getenv("ELITE_MISTEST") || getenv("ELITE_STATUSTEST") ||
        getenv("ELITE_BTEST") ||
        getenv("ELITE_STARTCHECK") ||
        getenv("ELITE_ACTION") ||
        getenv("ELITE_INTELTEST") ||
        getenv("ELITE_SIEGE") ||
        getenv("ELITE_TAPTEST") ||
        getenv("ELITE_DISTRESS") ||
        getenv("ELITE_NEWDEATH") ||
        getenv("ELITE_DRONETEST") ||
        getenv("ELITE_PIRGEN") ||
        getenv("ELITE_JITTERTEST") ||
        getenv("ELITE_ICONSHOT") ||
        getenv("ELITE_PLASMATEST") ||
        getenv("ELITE_COLTEST") ||
        getenv("ELITE_CLOAKTEST") ||
        getenv("ELITE_ROCKCYCLE") ||
        getenv("ELITE_TAILTEST") ||
        getenv("ELITE_TIERMOVIE") ||
        getenv("ELITE_WPNMOVIE") ||
        getenv("ELITE_FINETEST") ||
        getenv("ELITE_ORBITPROBE") ||
        getenv("ELITE_REDISTRESS") ||
        getenv("ELITE_ARMOURYSHOT") ||
        getenv("ELITE_DPSTEST") ||
        getenv("ELITE_CMTEST") ||
        getenv("ELITE_DODGETEST") ||
        getenv("ELITE_BENDTEST") ||
        getenv("ELITE_SPEEDKILL") ||
        getenv("ELITE_BENCH") ||
        getenv("ELITE_HPMATRIX") ||
        getenv("ELITE_STARTSAFE") ||
        getenv("ELITE_TURRETTEST") ||
        getenv("ELITE_NPCHP") ||
        getenv("ELITE_ASSASSTEST") ||
        getenv("ELITE_KILLSCREEN") ||
        getenv("ELITE_FURBALL") ||
        getenv("ELITE_CIVMOVE") ||
        getenv("ELITE_LANCESHOT") ||
        getenv("ELITE_ENGTEST") ||
        getenv("ELITE_SFXTEST") ||
        getenv("ELITE_SETSHOT") ||
        getenv("ELITE_FLAKTEST") ||
        getenv("ELITE_FLAKVIS") ||
        getenv("ELITE_RAMKILL") ||
        getenv("ELITE_STATUSHIDE") ||
        getenv("ELITE_SWEEPTEST") ||
        getenv("ELITE_RAMTEST") ||
        getenv("ELITE_DASHTEST") ||
        getenv("ELITE_CRITTEST") ||
        getenv("ELITE_PLANETSHEET") ||
        getenv("ELITE_STOLENTEST") ||
        getenv("ELITE_YARDCOLOR") ||
        getenv("ELITE_SWAPTEST") ||
        getenv("ELITE_POPSHOT") ||
        getenv("ELITE_SHOT")) {
        /* Harnesses start in-game: skip the title via NEW GAME. Arrival
         * hails would eat the scripted button streams — disable them. */
        events_set_chance(0);
        remove("indemnityrun.sav");
        CraftRawButtons tb = {0};
        elite_game_tick(&tb, 1.0f / 30.0f);
        tb.down = true; elite_game_tick(&tb, 1.0f / 30.0f);
        tb.down = false; elite_game_tick(&tb, 1.0f / 30.0f);
        tb.a = true; elite_game_tick(&tb, 1.0f / 30.0f);
        tb.a = false; elite_game_tick(&tb, 1.0f / 30.0f);
    }

    /* Playtest aid: ELITE_EVENTFORCE=1 -> an arrival hail fires on EVERY
     * dock (normally 35%). Applied last so it wins over the harness off. */
    if (getenv("ELITE_EVENTFORCE"))
        events_set_chance(100);

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

    /* Planet variety sheet: render planet 0 of N systems up close. */
    if (getenv("ELITE_PLANETSHEET")) {
        int count = atoi(getenv("ELITE_PLANETSHEET"));
        if (count < 1) count = 100;
        uint32_t rs = 0x91A7u;
        int made = 0;
        CraftRawButtons none = {0};
        for (int t = 0; made < count && t < count * 8; t++) {
            rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
            SysAddr a2 = { (int16_t)((rs % 200u) - 100),
                           (int16_t)(((rs >> 9) % 200u) - 100), 0 };
            int n2 = galaxy_sector_stars(a2.sx, a2.sy);
            if (n2 <= 0) continue;
            a2.idx = (uint8_t)((rs >> 18) % (uint32_t)n2);
            elite_game_debug_jump(a2);
            const SystemInfo *si = system_info();
            if (si->n_planets <= 0) continue;
            /* anchor at a random planet POI: list pois, find planets */
            Poi pois[MAX_POIS];
            int np = system_pois(pois, MAX_POIS);
            int planets[12], npl = 0;
            for (int i = 0; i < np; i++)
                if (pois[i].kind == POI_PLANET) planets[npl++] = i;
            if (npl <= 0) continue;
            elite_game_debug_view_planet(planets[(rs >> 22) % npl]);
            render_frame();
            char pp[64];
            snprintf(pp, sizeof pp, "/tmp/planets/p_%03d.ppm", made);
            dump_ppm(pp);
            made++;
        }
        printf("[planets] wrote %d\n", made);
        return 0;
    }

    /* Popup UI shots: A on the fitted mount, then SWAP's picker. */
    if (getenv("ELITE_POPSHOT")) {
        /* rack a second gun so SWAP has a counterpart */
        g_player.salvage[0] = (WeaponInst){ .type = WPN_AUTOCANNON,
            .quality = 2, .integrity = 90, .in_use = 1,
            .ammo_flag = 1, .ammo_lo = 80 };
        CraftRawButtons none = {0}, b;
        #define TP(field, settle) do { \
            b = none; b.field = true; \
            elite_game_tick(&b, 1.0f / 30.0f); \
            for (int k = 0; k < (settle); k++) \
                elite_game_tick(&none, 1.0f / 30.0f); \
        } while (0)
        /* dock at the station (same dance as TRADETEST) */
        for (int k = 0; k < 30; k++) elite_game_tick(&none, 1.0f/30.0f);
        TP(menu, 12); TP(right, 3); TP(a, 4);
        for (int i = 0; i < 5; i++) TP(down, 2);
        TP(a, 4);
        int f = 0;
        while (elite_game_state() == 1 && f++ < 30 * 240)
            elite_game_tick(&none, 1.0f / 30.0f);
        b = none; b.lb = b.rb = true;
        for (int k = 0; k < 8; k++) elite_game_tick(&b, 1.0f / 30.0f);
        f = 0;
        while (elite_game_state() == 6 && f++ < 200)
            elite_game_tick(&none, 1.0f / 30.0f);
        for (int k = 0; k < 12; k++) elite_game_tick(&none, 1.0f/30.0f);
        if (elite_game_state() != 7) { printf("[pop] no dock\n"); return 1; }
        TP(down, 2); TP(down, 2);            /* HOME -> OUTFITTING */
        TP(a, 6);
        TP(a, 4);                            /* popup on first mount */
        render_frame(); dump_ppm("/tmp/pop_menu.ppm");
        TP(down, 2);                         /* cursor to SWAP */
        TP(a, 4);                            /* open picker */
        render_frame(); dump_ppm("/tmp/pop_pick.ppm");
        TP(a, 4);                            /* confirm swap */
        render_frame(); dump_ppm("/tmp/pop_done.ppm");
        printf("[pop] mount now=%d rack now=%d ammo=%d\n",
               g_player.mounts[0].type, g_player.salvage[0].type,
               g_player.ammo[0]);
        return 0;
    }

    /* Magazine conservation through unfit/refit (the free-reload
     * exploit): fit AC, fire it down, unfit, refit, count rounds. */
    if (getenv("ELITE_SWAPTEST")) {
        ship_fit_weapon(0, 0, WPN_AUTOCANNON);
        g_player.mounts[0] = (WeaponInst){ .type = WPN_AUTOCANNON,
            .quality = 1, .integrity = 100, .in_use = 1 };
        player_fit_restore_ammo(0);
        int full = g_player.ammo[0];
        g_player.ammo[0] = 37;               /* fired most of the mag */
        player_stash_mount_ammo(0);          /* unfit path stashes */
        WeaponInst saved = g_player.mounts[0];
        g_player.mounts[0].in_use = 0;
        g_player.mounts[0] = saved;          /* refit the same gun */
        player_fit_restore_ammo(0);
        printf("[swap] full=%d fired-down=37 after-refit=%d (%s)\n",
               full, g_player.ammo[0],
               g_player.ammo[0] == 37 ? "CONSERVED" : "FREE RELOAD BUG");
        /* factory gun still arrives sealed */
        g_player.mounts[0] = (WeaponInst){ .type = WPN_AUTOCANNON,
            .quality = 1, .integrity = 100, .in_use = 1 };
        player_fit_restore_ammo(0);
        printf("[swap] factory seal: %d/%d (%s)\n", g_player.ammo[0],
               full, g_player.ammo[0] == full ? "SEALED" : "WRONG");
        return 0;
    }

    /* Magazine conservation through unfit/refit (the free-reload
     * exploit): fit AC, fire it down, unfit, refit, count rounds. */
    if (getenv("ELITE_SWAPTEST")) {
        ship_fit_weapon(0, 0, WPN_AUTOCANNON);
        g_player.mounts[0] = (WeaponInst){ .type = WPN_AUTOCANNON,
            .quality = 1, .integrity = 100, .in_use = 1 };
        player_fit_restore_ammo(0);
        int full = g_player.ammo[0];
        g_player.ammo[0] = 37;               /* fired most of the mag */
        player_stash_mount_ammo(0);          /* unfit path stashes */
        WeaponInst saved = g_player.mounts[0];
        g_player.mounts[0].in_use = 0;
        g_player.mounts[0] = saved;          /* refit the same gun */
        player_fit_restore_ammo(0);
        printf("[swap] full=%d fired-down=37 after-refit=%d (%s)\n",
               full, g_player.ammo[0],
               g_player.ammo[0] == 37 ? "CONSERVED" : "FREE RELOAD BUG");
        /* factory gun still arrives sealed */
        g_player.mounts[0] = (WeaponInst){ .type = WPN_AUTOCANNON,
            .quality = 1, .integrity = 100, .in_use = 1 };
        player_fit_restore_ammo(0);
        printf("[swap] factory seal: %d/%d (%s)\n", g_player.ammo[0],
               full, g_player.ammo[0] == full ? "SEALED" : "WRONG");
        return 0;
    }

    if (getenv("ELITE_YARDCOLOR")) {
        /* REAL flow: dock, open the shipyard, B for the detail sheet —
         * the live rotating hull fills the right pane. */
        CraftRawButtons none = {0}, b;
        #define TY(field, settle) do { \
            b = none; b.field = true; \
            elite_game_tick(&b, 1.0f / 30.0f); \
            for (int k = 0; k < (settle); k++) \
                elite_game_tick(&none, 1.0f / 30.0f); \
        } while (0)
        for (int k = 0; k < 30; k++) elite_game_tick(&none, 1.0f/30.0f);
        TY(menu, 12); TY(right, 3); TY(a, 4);
        for (int i = 0; i < 5; i++) TY(down, 2);
        TY(a, 4);
        int f = 0;
        while (elite_game_state() == 1 && f++ < 30 * 240)
            elite_game_tick(&none, 1.0f / 30.0f);
        b = none; b.lb = b.rb = true;
        for (int k = 0; k < 8; k++) elite_game_tick(&b, 1.0f / 30.0f);
        f = 0;
        while (elite_game_state() == 6 && f++ < 200)
            elite_game_tick(&none, 1.0f / 30.0f);
        for (int k = 0; k < 12; k++) elite_game_tick(&none, 1.0f/30.0f);
        if (elite_game_state() != 7) { printf("[yc] no dock\n"); return 1; }
        TY(down, 2);
        TY(a, 6);
        printf("[yc] own hull=%d\n", g_player.hull_id);
        TY(b, 8);
        for (int tries = 0; tries < 6; tries++) {
            extern int station_preview2(uint32_t *, int *);
            uint32_t seed2; int cls2;
            station_preview2(&seed2, &cls2);
            if (cls2 != g_player.hull_id) break;
            TY(rb, 8);
        }
        for (int k = 0; k < 20; k++) elite_game_tick(&none, 1.0f/30.0f);
        render_frame(); dump_ppm("/tmp/yc_live_detail.ppm");
        TY(rb, 8);
        for (int k = 0; k < 20; k++) elite_game_tick(&none, 1.0f/30.0f);
        render_frame(); dump_ppm("/tmp/yc_live_detail2.ppm");
        TY(down, 14);   /* drill into the included-kit view */
        render_frame(); dump_ppm("/tmp/yc_kit.ppm");
        #undef TY
        return 0;
    }

    /* Stolen-goods chain: black-market-only sale + criminal status. */
    if (getenv("ELITE_STOLENTEST")) {
        g_player.cargo[19] = 4;                 /* hot contraband */
        const SystemInfo *si = system_info();
        int lawful = econ_has_black_market(si) ? 0 : 1;
        int sell = econ_price(si, 0, 19, false);
        printf("[stolen] system lawful=%d sell_price=%d (%s)\n",
               lawful, sell,
               (lawful && sell == 0) ? "BLOCKED" :
               (!lawful && sell > 0) ? "BLACK MARKET OK" : "CHECK");
        /* police scan: park a patrol next to us and wait */
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        int e = ship_spawn(hull_mesh(0x70110CEu, 3),
                           v3_add(pl->pos, v3(60, 0, 60)), TEAM_NEUTRAL);
        if (e > 0) {
            ship_set_tier(e, 3, 3);
            g_ships[e].is_police = 1;
            g_ships[e].vel = v3(0, 0, 0);
            CraftRawButtons none = {0};
            pl->vel = v3(0, 0, 0);
            for (int f = 0; f < 120 && g_player.legal == 0; f++) {
                g_ships[e].pos = v3_add(pl->pos, v3(60, 0, 60));
                elite_game_tick(&none, 1.0f / 30.0f);
            }
            printf("[stolen] after scan: legal=%d fine=%d "
                   "police_team=%d\n", g_player.legal, g_player.fine,
                   g_ships[e].alive ? g_ships[e].team : -1);
        }
        return 0;
    }

    /* Critical-hit chain test. */
    if (getenv("ELITE_CRITTEST")) {
        CraftRawButtons none = {0};
        Ship *pl = &g_ships[0];
        pl->shield = 0;                       /* shields down: crit land */
        pl->hull = pl->hull_max = 10000.0f;   /* survive the barrage */
        int crits = 0;
        int w0_integ0 = (int)g_player.mounts[0].integrity;
        for (int k = 0; k < 60; k++) {
            combat_set_shot_type(WPN_PULSE_S);
            combat_direct_damage(1, 0, 30.0f, pl->pos);
            for (int f = 0; f < 70; f++)      /* ride out the cooldown */
                elite_game_tick(&none, 1.0f / 30.0f);
            pl->shield = 0;
        }
        int w0_integ1 = (int)g_player.mounts[0].integrity;
        printf("[crit] wpn0 integrity %d -> %d engine_crit=%d\n",
               w0_integ0, w0_integ1,
               (pl->crits & CRIT_ENGINE) ? 1 : 0);
        /* offline weapon must refuse to fire */
        g_player.mounts[0].integrity = 0;
        pl->active_w = 0;
        pl->fire_cool = 0;
        pl->heat = 0;
        int r = combat_fire(0, 0, -1);
        printf("[crit] offline mount fire -> %d (want -1)\n", r);
        /* smashed generator: shield must not regen */
        g_player.shield_eq.integrity = 0;
        pl->shield = 0;
        for (int f = 0; f < 90; f++) elite_game_tick(&none, 1.0f/30.0f);
        printf("[crit] regen w/ dead gen: shield=%.1f (want 0)\n",
               pl->shield);
        /* NPC: crit all weapons -> it should flee (distance grows) */
        extern const Mesh *hull_mesh(uint32_t, int);
        int e = ship_spawn(hull_mesh(0xACE1u, 2),
                           v3_add(pl->pos, v3(0, 0, 200)), TEAM_HOSTILE);
        if (e > 0) {
            ship_set_tier(e, 2, 2);
            g_ships[e].crits |= CRIT_WPN0 | CRIT_WPN1 | CRIT_WPN2;
            float d0 = 200;
            for (int f = 0; f < 150; f++)
                elite_game_tick(&none, 1.0f / 30.0f);
            float d1 = v3_len(v3_sub(g_ships[e].pos, pl->pos));
            printf("[crit] disarmed pirate: %0.f -> %.0fm (%s)\n", d0,
                   d1, d1 > d0 + 50 ? "FLEES" : "stays");
        }
        return 0;
    }

    /* Dashboard: open, sim-running check, navigate, screenshots. */
    if (getenv("ELITE_DASHTEST")) {
        CraftRawButtons none = {0}, b;
        /* spawn a hostile so the scanner shows peril */
        g_player.util_eq[0] = (WeaponInst){ .type = EQ_CHAFF,
            .quality = 1, .integrity = 100, .in_use = 1 };
        g_player.chaff_charges = 4;
        elite_game_debug_spawn(2);
        for (int f = 0; f < 10; f++) elite_game_tick(&none, 1.0f/30.0f);
        b = none; b.menu = true;
        elite_game_tick(&b, 1.0f/30.0f);
        b = none;
        elite_game_tick(&b, 1.0f/30.0f);
        printf("[dash] state=%d (12=DASH)\n", elite_game_state());
        /* mid-rise frame */
        for (int f = 0; f < 3; f++) elite_game_tick(&none, 1.0f/30.0f);
        render_frame(); dump_ppm("/tmp/dash_rising.ppm");
        for (int f = 0; f < 12; f++) elite_game_tick(&none, 1.0f/30.0f);
        render_frame(); dump_ppm("/tmp/dash_full.ppm");
        /* sim runs: hostile distance must change over 60 ticks */
        float d0 = -1, d1 = -1;
        for (int i = 1; i < MAX_SHIPS; i++)
            if (g_ships[i].alive && g_ships[i].team == TEAM_HOSTILE) {
                d0 = v3_len(v3_sub(g_ships[i].pos, g_ships[0].pos));
                break;
            }
        for (int f = 0; f < 60; f++) elite_game_tick(&none, 1.0f/30.0f);
        for (int i = 1; i < MAX_SHIPS; i++)
            if (g_ships[i].alive && g_ships[i].team == TEAM_HOSTILE) {
                d1 = v3_len(v3_sub(g_ships[i].pos, g_ships[0].pos));
                break;
            }
        printf("[dash] sim runs: hostile %5.0fm -> %5.0fm (%s)\n", d0,
               d1, (d0 != d1) ? "LIVE" : "frozen");
        /* settings region: right+down then A */
        b = none; b.right = true; elite_game_tick(&b, 1.0f/30.0f);
        b = none; elite_game_tick(&b, 1.0f/30.0f);
        b = none; b.down = true; elite_game_tick(&b, 1.0f/30.0f);
        b = none; elite_game_tick(&b, 1.0f/30.0f);
        b = none; b.a = true; elite_game_tick(&b, 1.0f/30.0f);
        b = none; elite_game_tick(&b, 1.0f/30.0f);
        /* volume slider: cursor down x2, left x3 -> 70% */
        for (int k = 0; k < 2; k++) {
            b = none; b.down = true; elite_game_tick(&b, 1.0f/30.0f);
            b = none; elite_game_tick(&b, 1.0f/30.0f);
        }
        for (int k = 0; k < 3; k++) {
            b = none; b.left = true; elite_game_tick(&b, 1.0f/30.0f);
            b = none; elite_game_tick(&b, 1.0f/30.0f);
        }
        printf("[dash] volume after 3x left: %d/20 master=%.2f\n",
               plat_setting_get(0), audio_get_master());
        render_frame(); dump_ppm("/tmp/dash_settings.ppm");
        b = none; b.b = true; elite_game_tick(&b, 1.0f/30.0f);
        b = none; elite_game_tick(&b, 1.0f/30.0f);
        /* open the chart from the dash (sel back to 0) */
        b = none; b.up = true; elite_game_tick(&b, 1.0f/30.0f);
        b = none; elite_game_tick(&b, 1.0f/30.0f);
        b = none; b.left = true; elite_game_tick(&b, 1.0f/30.0f);
        b = none; elite_game_tick(&b, 1.0f/30.0f);
        b = none; b.a = true; elite_game_tick(&b, 1.0f/30.0f);
        b = none; elite_game_tick(&b, 1.0f/30.0f);
        printf("[dash] chart open: state=%d (3=GALAXY)\n",
               elite_game_state());
        /* B closes back to dash; MENU resumes flight */
        b = none; b.b = true; elite_game_tick(&b, 1.0f/30.0f);
        b = none; elite_game_tick(&b, 1.0f/30.0f);
        printf("[dash] back: state=%d (12=DASH)\n", elite_game_state());
        b = none; b.menu = true; elite_game_tick(&b, 1.0f/30.0f);
        b = none;
        elite_game_tick(&b, 1.0f/30.0f);
        render_frame(); dump_ppm("/tmp/dash_closing.ppm");
        for (int f = 0; f < 12; f++) elite_game_tick(&none, 1.0f/30.0f);
        printf("[dash] resume: state=%d (0=FLIGHT)\n",
               elite_game_state());
        return 0;
    }

    /* Resolved distress must not respawn at the same POI. */
    if (getenv("ELITE_REDISTRESS")) {
        extern int elite_game_debug_distress_civ(void);
        /* find a distress POI in this system, fly there */
        CraftRawButtons none = {0};
        for (int k = 0; k < 10; k++) elite_game_tick(&none, 1.0f/30.0f);
        int civ0 = -1, poi_idx = -1;
        for (int pi = 0; pi < 8 && civ0 <= 0; pi++) {
            elite_game_debug_goto_poi(pi);
            for (int k = 0; k < 10; k++)
                elite_game_tick(&none, 1.0f / 30.0f);
            civ0 = elite_game_debug_distress_civ();
            poi_idx = pi;
        }
        printf("[redis] event live: civ=%d at poi %d\n", civ0, poi_idx);
        if (civ0 > 0) {
            /* resolve it: kill the wing via harness */
            for (int i = 1; i < MAX_SHIPS; i++)
                if (g_ships[i].alive && g_ships[i].team == TEAM_HOSTILE)
                    g_ships[i].alive = false;
            for (int k = 0; k < 30; k++)
                elite_game_tick(&none, 1.0f / 30.0f);
            extern const char *elite_game_debug_toast(void);
            printf("[redis] resolve toast: %s\n",
                   elite_game_debug_toast());
            /* re-arrive at the SAME poi */
            elite_game_debug_goto_poi(poi_idx);
            for (int k = 0; k < 10; k++)
                elite_game_tick(&none, 1.0f / 30.0f);
            printf("[redis] after re-arrival: civ=%d (%s)\n",
                   elite_game_debug_distress_civ(),
                   elite_game_debug_distress_civ() <= 0
                       ? "NOT FARMABLE" : "RESPAWNED - BUG");
        }
        return 0;
    }

    /* Orbit spread probe. */
    if (getenv("ELITE_ORBITPROBE")) {
        float worst = 0, sum = 0; int n = 0;
        for (int sx = -3; sx <= 3; sx++)
            for (int sy = -3; sy <= 3; sy++) {
                SysAddr a = { (int16_t)sx, (int16_t)sy, 0 };
                SystemInfo si;
                galaxy_generate(a, &si);
                if (si.n_planets < 2) continue;
                float o = si.planets[si.n_planets - 1].orbit_mm;
                if (o > worst) worst = o;
                sum += o; n++;
            }
        printf("[orbit] outermost: avg=%.0f worst=%.0f Mm over %d\n",
               sum / (n ? n : 1), worst, n);
        return 0;
    }

    /* Civilian motion: do they travel, or hover? */
    if (getenv("ELITE_CIVMOVE")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        rocks_spawn_field(0xBEEF, 5);
        Ship *pl = &g_ships[0];
        int miner = ship_spawn(hull_mesh(0x11, 6),
                               v3_add(pl->pos, v3(120, 0, 300)),
                               TEAM_NEUTRAL);
        g_ships[miner].is_civilian = 1; g_ships[miner].civ_kind = 0;
        int haul = ship_spawn(hull_mesh(0x22, 7),
                              v3_add(pl->pos, v3(-120, 0, 300)),
                              TEAM_NEUTRAL);
        g_ships[haul].is_civilian = 1; g_ships[haul].civ_kind = 1;
        CraftRawButtons none = {0};
        for (int k = 0; k < 5; k++) elite_game_tick(&none, 1.0f/30.0f);
        Vec3 m0 = g_ships[miner].pos, h0 = g_ships[haul].pos;
        float mpath = 0, hpath = 0;
        Vec3 mp = m0, hp = h0;
        for (int f = 0; f < 30 * 20; f++) {
            elite_game_tick(&none, 1.0f / 30.0f);
            mpath += v3_len(v3_sub(g_ships[miner].pos, mp));
            hpath += v3_len(v3_sub(g_ships[haul].pos, hp));
            mp = g_ships[miner].pos; hp = g_ships[haul].pos;
        }
        printf("[civ] miner: net %.0fm, path %.0fm, speed~%.0f\n",
               v3_len(v3_sub(g_ships[miner].pos, m0)), mpath, mpath / 20);
        printf("[civ] hauler: net %.0fm, path %.0fm, speed~%.0f\n",
               v3_len(v3_sub(g_ships[haul].pos, h0)), hpath, hpath / 20);
        return 0;
    }

    /* Furball: peak projectile + particle load with many shooters. */
    if (getenv("ELITE_FURBALL")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        pl->shield = 1e9f; pl->hull = 1e9f;   /* invincible observer */
        for (int k = 0; k < 6; k++) {
            int e = ship_spawn(hull_mesh(0xA0u + k, 2 + (k % 4)),
                               v3_add(pl->pos,
                                      v3(40 * (k - 3), 0, 180)),
                               TEAM_HOSTILE);
            ship_set_tier(e, 2 + (k % 3), 2 + (k % 4));
        }
        int peak = 0;
        CraftRawButtons none = {0};
        for (int f = 0; f < 30 * 8; f++) {
            elite_game_tick(&none, 1.0f / 30.0f);
            pl->shield = 1e9f; pl->hull = 1e9f;
            int pc = proj_count();
            if (pc > peak) peak = pc;
        }
        printf("[furball] 6 shooters, peak projectiles: %d / 72\n", peak);
        return 0;
    }

    /* RAM test: disarmed attackers vs a cruising player — any player
     * damage is purely collision. Reports closest approach + ram dmg. */
    if (getenv("ELITE_RAMTEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        elite_game_debug_face_away_from_sun();
        pl->shield = pl->shield_max; pl->hull = pl->hull_max;
        for (int k = 0; k < 4; k++) {
            int e = ship_spawn(hull_mesh(0xACE1u + k, 2 + k),
                               v3_add(pl->pos,
                                      v3(60.0f*(k-2), 20.0f, 150.0f+40*k)),
                               TEAM_HOSTILE);
            ship_set_tier(e, k % 5, 2 + k);
            g_ships[e].n_weapons = 0;          /* rams only */
            g_ships[e].turret_type = 0;
        }
        float start_hp = pl->shield + pl->hull;
        float closest = 1e9f; int contacts = 0;
        CraftRawButtons none = {0};
        for (int f = 0; f < 30 * 25; f++) {
            float before = pl->shield + pl->hull;
            /* player flies a gentle weave at cruise (a real target) */
            CraftRawButtons b = none;
            int charge = atoi(getenv("ELITE_RAMTEST")) >= 2;
            if (charge) {
                int nr2=-1; float nd=1e9f;
                for (int i=1;i<MAX_SHIPS;i++){ if(!g_ships[i].alive)continue;
                  float d=v3_len(v3_sub(g_ships[i].pos,pl->pos));
                  if(d<nd){nd=d;nr2=i;} }
                if (nr2>0){ Vec3 to=v3_norm(v3_sub(g_ships[nr2].pos,pl->pos));
                  pl->basis.r[2]=to;
                  pl->basis.r[0]=v3_norm(v3_cross(v3(0,1,0),to));
                  pl->basis.r[1]=v3_cross(to,pl->basis.r[0]);
                  pl->vel=v3_scale(to,70.0f); }
            } else if (charge) {
                /* handled above */
            } else {
                /* REALISTIC dogfight: turn toward nearest enemy at a
                 * LIMITED rate (like real flying), cruise speed */
                int nr2=-1; float nd=1e9f;
                for (int i=1;i<MAX_SHIPS;i++){ if(!g_ships[i].alive)continue;
                  float d=v3_len(v3_sub(g_ships[i].pos,pl->pos));
                  if(d<nd){nd=d;nr2=i;} }
                if (nr2>0){
                  Vec3 to=v3_norm(v3_sub(g_ships[nr2].pos,pl->pos));
                  Vec3 ax=v3_cross(pl->basis.r[2],to);
                  float sa=v3_len(ax);
                  if(sa>1e-4f){ float ang=asinf(sa>1?1:sa);
                    float step=ang<0.06f?ang:0.06f;
                    m3_rotate_world(&pl->basis,v3_scale(ax,1.0f/sa),step);
                    m3_orthonormalize(&pl->basis); }
                  pl->vel=v3_scale(pl->basis.r[2], pl->max_speed*0.7f);
                }
            }
            elite_game_tick(&b, 1.0f / 30.0f);
            for (int i = 1; i < MAX_SHIPS; i++) {
                if (!g_ships[i].alive) continue;
                float d = v3_len(v3_sub(g_ships[i].pos, pl->pos));
                if (d < closest && f > 45) closest = d;
            }
            if (pl->shield + pl->hull < before - 0.5f) contacts++;
            if (!pl->alive) break;
        }
        printf("[ram] closest approach %.0fm, %d damage frames, "
               "ram dmg taken %.0f (%s)\n", closest, contacts,
               start_hp - (pl->shield + pl->hull),
               contacts == 0 ? "NO RAMS" : "RAMMED");
        return 0;
    }

    /* Evade->sweep + no-ram: tail each tier, watch for the sweep and
     * track closest approach (collision check). */
    if (getenv("ELITE_SWEEPTEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        extern int elite_game_debug_ai_state(int idx);
        Ship *pl = &g_ships[0];
        elite_game_debug_face_away_from_sun();
        for (int tier = 0; tier <= 4; tier++) {
            int e = ship_spawn(hull_mesh(0xACE1u, 1 + tier),
                               v3_add(pl->pos,
                                      v3_scale(pl->basis.r[2], 120.0f)),
                               TEAM_HOSTILE);
            ship_set_tier(e, tier, 1 + tier);
            Ship *en = &g_ships[e];
            en->basis = pl->basis;           /* same heading: we're on its six */
            pl->vel = v3_scale(pl->basis.r[2], 40.0f);
            int swept = 0; float closest = 1e9f; float t_sweep = -1;
            CraftRawButtons none = {0};
            for (int f = 0; f < 30 * 10; f++) {
                elite_game_tick(&none, 1.0f/30.0f);
                /* keep chasing its six */
                Vec3 six = v3_sub(en->pos,
                                  v3_scale(en->basis.r[2], 110.0f));
                pl->pos = v3_lerp(pl->pos, six, 0.3f);
                pl->basis = en->basis;
                float d = v3_len(v3_sub(en->pos, pl->pos));
                if (d < closest) closest = d;
                if (elite_game_debug_ai_state(e) == 3 && !swept) {
                    swept = 1; t_sweep = f / 30.0f;
                }
            }
            printf("[sweep] tier %d: swept=%s @%.1fs  closest=%.0fm "
                   "(%s)\n", tier, swept?"YES":"no", t_sweep, closest,
                   closest > 14.0f ? "no ram" : "RAMMED");
            en->alive = false;
        }
        return 0;
    }

    /* In-flight status view: text shown, then LB-hidden. */
    if (getenv("ELITE_STATUSHIDE")) {
        CraftRawButtons none = {0}, b;
        for (int k = 0; k < 30; k++) elite_game_tick(&none, 1.0f/30.0f);
        b = none; b.menu = true; elite_game_tick(&b, 1.0f/30.0f); /* dash */
        for (int k=0;k<14;k++) elite_game_tick(&none,1.0f/30.0f);
        /* dash sel 2 = STATUS: down (sets bit2), A */
        b=none; b.down=true; elite_game_tick(&b,1.0f/30.0f);
        elite_game_tick(&none,1.0f/30.0f);
        b=none; b.a=true; elite_game_tick(&b,1.0f/30.0f);
        for(int k=0;k<10;k++) elite_game_tick(&none,1.0f/30.0f);
        printf("[sh] state=%d (7=status? actually ST_STATUS)\n",
               elite_game_state());
        render_frame(); dump_ppm("/tmp/status_shown.ppm");
        b=none;b.lb=true;elite_game_tick(&b,1.0f/30.0f);
        for(int k=0;k<10;k++) elite_game_tick(&none,1.0f/30.0f);
        render_frame(); dump_ppm("/tmp/status_hidden.ppm");
        return 0;
    }

    /* Collision kill counts. */
    if (getenv("ELITE_RAMKILL")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        int k0 = combat_kills();
        int c0 = g_player.credits;
        int e = ship_spawn(hull_mesh(0xACE1u, 1),
                           v3_add(pl->pos, v3_scale(pl->basis.r[2], 6.0f)),
                           TEAM_HOSTILE);   /* overlapping -> collides */
        ship_set_tier(e, 1, 1);
        g_ships[e].hull = 1; g_ships[e].shield = 0;   /* one bump kills */
        pl->vel = v3_scale(pl->basis.r[2], 60.0f);
        g_ships[e].vel = v3_scale(pl->basis.r[2], -60.0f);
        collide_tick(0, 0, 1);
        printf("[ramkill] enemy alive=%d kills %d->%d credits %d->%d (%s)\n",
               g_ships[e].alive, k0, combat_kills(), c0, g_player.credits,
               (combat_kills() > k0) ? "COUNTS" : "NOT COUNTED");
        return 0;
    }

    /* FLAK burst visual. */
    if (getenv("ELITE_FLAKVIS")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        elite_game_debug_face_away_from_sun();
        int e = ship_spawn(hull_mesh(0xACE1u, 2),
                           v3_add(pl->pos, v3_scale(pl->basis.r[2],
                                                    FLAK_FUZE)),
                           TEAM_HOSTILE);
        ship_set_tier(e, 2, 2);
        g_player.mounts[0] = (WeaponInst){ .type = WPN_FLAK,
            .quality = Q_STANDARD, .integrity = 100, .in_use = 1 };
        player_apply_to_ship();
        pl->active_w = 0; pl->fire_cool = 0; pl->heat = 0;
        combat_set_shot_type(WPN_FLAK);
        combat_fire(0, 0, e);
        CraftRawButtons none = {0};
        for (int f = 0; f < 6; f++) elite_game_tick(&none, 1.0f/30.0f);
        render_frame(); dump_ppm("/tmp/flakvis.ppm");
        return 0;
    }

    /* FLAK airburst: damage at correct fuze range vs mistimed. */
    if (getenv("ELITE_FLAKTEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        elite_game_debug_face_away_from_sun();
        float dists[3] = { FLAK_FUZE, FLAK_FUZE - 130.0f, FLAK_FUZE + 130.0f };
        const char *lbl[3] = { "ON fuze (timed)", "too CLOSE", "too FAR" };
        for (int c = 0; c < 3; c++) {
            int e = ship_spawn(hull_mesh(0xACE1u, 4),
                               v3_add(pl->pos,
                                      v3_scale(pl->basis.r[2], dists[c])),
                               TEAM_HOSTILE);
            ship_set_tier(e, 4, 4);
            Ship *t2 = &g_ships[e];
            t2->n_weapons = 0;
            Vec3 hold = t2->pos;
            float hp0 = t2->shield + t2->hull;
            g_player.mounts[0] = (WeaponInst){ .type = WPN_FLAK,
                .quality = Q_STANDARD, .integrity = 100, .in_use = 1 };
            g_player.ammo[0] = 60;
            player_apply_to_ship();
            pl->active_w = 0; pl->fire_cool = 0; pl->heat = 0;
            combat_set_shot_type(WPN_FLAK);
            { CraftRawButtons lb = {0}; lb.lb = true;
              elite_game_tick(&lb, 1.0f/30.0f);
              for (int k=0;k<6;k++){CraftRawButtons n={0};elite_game_tick(&n,1.0f/30.0f);} }
            pl->fire_cool = 0;
            combat_fire(0, 0, e);
            CraftRawButtons none = {0};
            for (int f = 0; f < 60; f++) {
                elite_game_tick(&none, 1.0f/30.0f);
                t2->pos = hold; t2->vel = v3(0,0,0); pl->vel = v3(0,0,0);
                if (!t2->alive) break;
            }
            printf("[flak] %-16s one shot dealt %.0f damage\n",
                   lbl[c], hp0 - (t2->alive ? t2->shield + t2->hull : 0));
            if (t2->alive) t2->alive = false;
        }
        return 0;
    }

    /* Settings overlay screenshot (laser-sfx row). */
    if (getenv("ELITE_SETSHOT")) {
        CraftRawButtons none = {0}, b;
        for (int k=0;k<10;k++) elite_game_tick(&none,1.0f/30.0f);
        b=none; b.menu=true; elite_game_tick(&b,1.0f/30.0f);  /* dash */
        for (int k=0;k<20;k++) elite_game_tick(&none,1.0f/30.0f);
        /* navigate dash to SETTINGS + open: down+down, A */
        b=none; b.right=true; elite_game_tick(&b,1.0f/30.0f);
        elite_game_tick(&none,1.0f/30.0f);
        b=none; b.down=true; elite_game_tick(&b,1.0f/30.0f);
        elite_game_tick(&none,1.0f/30.0f);
        b=none; b.a=true; elite_game_tick(&b,1.0f/30.0f);
        for (int k=0;k<10;k++) elite_game_tick(&none,1.0f/30.0f);
        render_frame(); dump_ppm("/tmp/setshot.ppm");
        return 0;
    }

    /* Controller setup screen layout (read-only pad labels with no joystick). */
    if (getenv("ELITE_CTRLSHOT")) {
        extern void ctrlsetup_open(void);
        extern void ctrlsetup_draw(uint16_t *);
        CraftRawButtons none = {0};
        for (int k = 0; k < 6; k++) elite_game_tick(&none, 1.0f / 30.0f);
        ctrlsetup_open();
        extern bool ctrlsetup_tick(const CraftRawButtons *, float);
        if (getenv("ELITE_CTRLSHOT_BOT")) {     /* scroll to the bottom rows */
            CraftRawButtons dn = {0}; dn.down = true;
            ctrlsetup_tick(&none, 1.0f / 30.0f);            /* arm */
            for (int k = 0; k < 17; k++) {
                ctrlsetup_tick(&dn, 1.0f / 30.0f);
                ctrlsetup_tick(&none, 1.0f / 30.0f);
            }
        }
#ifdef ELITE_OVERLAY_SPLIT
        elite_game_render_begin(); elite_game_render(g_fb3d, 0, ELITE_FB_H);
        for (int i = 0; i < ELITE_FB_W * ELITE_FB_H; i++) g_fbui[i] = ELITE_KEY_T;
        ctrlsetup_draw(g_fbui);
        for (int yy = 0; yy < R3D_FB_H; yy++) {
            const uint16_t *ur = g_fbui + (yy / R3D_SS) * ELITE_FB_W;
            const uint16_t *dr = g_fb3d + yy * R3D_FB_W;
            uint16_t *o = g_fb + yy * R3D_FB_W;
            for (int x = 0; x < R3D_FB_W; x++) {
                uint16_t u = ur[x / R3D_SS];
                o[x] = (u == ELITE_KEY_T) ? dr[x]
                     : (u == ELITE_KEY_DIM) ? (uint16_t)((dr[x] >> 1) & 0x7BEF) : u;
            }
        }
#else
        elite_game_render_begin(); elite_game_render(g_fb, 0, ELITE_FB_H);
        ctrlsetup_draw(g_fb);
#endif
        dump_ppm("/tmp/ctrlshot.ppm");
        return 0;
    }

    /* Verify a weapon's sfx produces audio (non-silent render). */
    if (getenv("ELITE_ENGTEST")) {
        extern void audio_engine_set(float, float);
        audio_engine_set(0.7f, 0.7f);
        int16_t buf[512]; int peak=0; long sumsq=0; int nn=0;
        for (int b=0;b<40;b++){ audio_render(buf,512);
          for(int i=0;i<512;i++){int a=buf[i]<0?-buf[i]:buf[i];
            if(a>peak)peak=a; sumsq+=(long)buf[i]*buf[i]; nn++; } }
        printf("[eng] peak=%d rms=%d (audible, not a pure tone)\n",
               peak, (int)(sumsq/nn>0?__builtin_sqrtl((double)(sumsq/nn)):0));
        return 0;
    }
    if (getenv("ELITE_SFXTEST")) {
        const char *nm = getenv("ELITE_SFXTEST");
        int wt = WPN_PULSE_S;
        for (const char *c = nm; c && *c; c++) {
            if (*c == 'P') wt = WPN_PLASMA;
            else if (*c == 'L') wt = WPN_LANCE;
            else if (*c == 'B') wt = WPN_BLASTER;
            else if (*c == 'M') wt = WPN_PULSE_M;
            else if (*c == 'G') wt = WPN_PULSE_L;
            else if (*c == 'H') wt = WPN_PHOTON;
        }
        extern void sfx_weapon(int wpn_type, float amp);
        sfx_weapon(wt, 1.0f);
        int16_t buf[512]; int peak = 0; int last_loud = 0;
        for (int b = 0; b < 60; b++) {
            audio_render(buf, 512);
            for (int i = 0; i < 512; i++) {
                int a = buf[i] < 0 ? -buf[i] : buf[i];
                if (a > peak) peak = a;
                if (a > 800) last_loud = b;
            }
        }
        printf("[sfx] tail: audible through block %d/60 (~%.2fs)\n",
               last_loud, last_loud * 512.0f / 22050.0f);
        printf("[sfx] wt=%d peak amplitude: %d (%s)\n",
               wt, peak, peak > 200 ? "AUDIBLE" : "SILENT");
        return 0;
    }

    /* Lance visual shot. */
    if (getenv("ELITE_LANCESHOT")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        elite_game_debug_face_away_from_sun();
        int e = ship_spawn(hull_mesh(0xACE1u, 4),
                           v3_add(pl->pos, v3_scale(pl->basis.r[2], 280.0f)),
                           TEAM_HOSTILE);
        ship_set_tier(e, 3, 4);
        g_player.mounts[0] = (WeaponInst){ .type = WPN_LANCE,
            .quality = Q_STANDARD, .integrity = 100, .in_use = 1 };
        player_apply_to_ship();
        pl->active_w = 0; pl->fire_cool = 0; pl->heat = 0;
        combat_set_shot_type(WPN_LANCE);
        combat_fire(0, 0, e);
        CraftRawButtons none = {0};
        elite_game_tick(&none, 1.0f / 60.0f);   /* one short tick */
        render_frame();
        dump_ppm("/tmp/lanceshot.ppm");
        return 0;
    }

    /* Kill report screenshot. */
    if (getenv("ELITE_KILLSCREEN")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        int e = ship_spawn(hull_mesh(0xACE1u, 4),
                           v3_add(pl->pos, v3(0, 0, 200)), TEAM_HOSTILE);
        ship_set_tier(e, 3, 4);
        pl->shield = 1; pl->hull = 1;
        CraftRawButtons none = {0};
        for (int f = 0; f < 30 * 12 && g_ships[0].alive; f++)
            elite_game_tick(&none, 1.0f / 30.0f);
        for (int f = 0; f < 50; f++)
            elite_game_tick(&none, 1.0f / 30.0f);
        render_frame();
        dump_ppm("/tmp/killscreen.ppm");
        return 0;
    }

    /* Kill-speed matrix (user req): each weapon in the hands of each
     * tier vs a player flying STRAIGHT at 66%% throttle, standard
     * SKIFF shield, no evasion. Cell = seconds to kill, AMMO = ran
     * dry, ->60s = survived the window. */
    if (getenv("ELITE_ASSASSTEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        extern Mission g_missions[];
        CraftRawButtons none = {0};
        for (int k = 0; k < 20; k++) elite_game_tick(&none, 1.0f/30.0f);
        const SystemInfo *si = system_info();
        /* force an assassinate contract targeting THIS system */
        g_missions[0] = (Mission){ 0 };
        g_missions[0].type = MIS_ASSASSINATE;
        g_missions[0].target = si->addr;
        g_missions[0].reward = 3700;
        g_missions[0].faction = 0;
        /* spawn the marked civilian + a plain civilian (control) */
        Ship *pl = &g_ships[0];
        int mk = ship_spawn(hull_mesh(0xC1771Eu, 7),
                            v3_add(pl->pos, v3_scale(pl->basis.r[2], 60.0f)),
                            TEAM_NEUTRAL);
        g_ships[mk].is_civilian = 1; g_ships[mk].is_mark = 1;
        g_ships[mk].team = TEAM_NEUTRAL;
        int legal0 = g_player.legal, cr0 = g_player.credits;
        /* player murders the marked target */
        extern void combat_finalize_kill(int, int);
        g_ships[mk].hull = 0;
        combat_finalize_kill(PLAYER, mk);
        printf("[assass] mission.done=%d (want 1)  legal %d->%d (want fugitive=2)\n",
               g_missions[0].done, legal0, g_player.legal);
        /* collect at dock */
        int paid = mission_collect(si, 0);
        printf("[assass] collected=%d credits %d->%d\n", paid, cr0,
               g_player.credits);
        return 0;
    }

    if (getenv("ELITE_NPCHP")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        extern void loot_on_kill(Vec3, Vec3, int, const Ship *);
        extern int loot_positions(Vec3 *, int *, int);
        printf("[npchp] SPARROW hull/shield per rank (was bare ~56/44 @T0):\n");
        for (int tr = 0; tr <= 4; tr++) {
            int e = ship_spawn(hull_mesh(0xABCDu + tr*7, 2), v3(0,0,300),
                               TEAM_HOSTILE);
            ship_set_tier(e, tr, 2);
            printf("[npchp] T%d  hull=%4.0f shield=%4.0f  (armZ%d shdZ%d)\n",
                   tr, g_ships[e].hull_max, g_ships[e].shield_max,
                   g_ships[e].armor_tier, g_ships[e].shield_tier);
            g_ships[e].alive = false;
        }
        /* drop check: kill a tier-3 NPC, count armour/shield canisters */
        extern void loot_init(void);
        int armsh = 0, wpn = 0, commod = 0, nodrop = 0;
        for (int trial = 0; trial < 300; trial++) {
            loot_init();
            int e = ship_spawn(hull_mesh(0x1234u + trial*13, 4), v3(0,0,300),
                               TEAM_HOSTILE);
            ship_set_tier(e, 3, 4);
            loot_on_kill(v3(trial*40,0,0), v3(0,0,0), 3, &g_ships[e]);
            g_ships[e].alive = false;
            extern int loot_dbg_comp_type(int);
            int found = -2;
            for (int sl = 0; sl < 6; sl++) {
                int t2 = loot_dbg_comp_type(sl);
                if (t2 >= 0) { found = t2; break; }
            }
            Vec3 cans[8]; int comp[8];
            int n = loot_positions(cans, comp, 8);
            if (n == 0) { nodrop++; continue; }
            if (found >= WPN_COUNT) armsh++;
            else if (found >= 0) wpn++;
            else commod++;
        }
        printf("[npchp] 300 REAVER kills: %d armour/shield, %d weapon, %d commodity, %d no-drop\n",
               armsh, wpn, commod, nodrop);
        return 0;
    }

    if (getenv("ELITE_TURRETTEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        extern void combat_set_player_target(int);
        extern void combat_tick(float);
        CraftRawButtons none = {0};
        for (int k = 0; k < 20; k++) elite_game_tick(&none, 1.0f/30.0f);
        Ship *pl = &g_ships[0];
        g_player.hull_id = 9;   /* BASILISK has a turret hardpoint */
        /* fit a PULSE-S turret, force PROTOTYPE calibration via seed */
        g_player.turret_eq = (WeaponInst){ .type = WPN_PULSE_S,
            .quality = Q_PROTOTYPE, .integrity = 100, .in_use = 1 };
        for (uint32_t sd = 1; sd < 200000; sd++) {
            extern int turret_cal_for_seed(uint32_t);
            if (turret_cal_for_seed(sd) == 3) { g_player.hull_seed = sd; break; }
        }
        player_apply_to_ship();
        printf("[turret] type=%d turret_type=%d gunner=%d\n",
               g_player.turret_eq.type, pl->turret_type,
               (int)({ extern int player_turret_gunner_tier(void);
                       player_turret_gunner_tier(); }));
        int e = ship_spawn(hull_mesh(0xACE1u, 2),
                           v3_add(pl->pos, v3_scale(pl->basis.r[2], 150.0f)),
                           TEAM_HOSTILE);
        ship_set_tier(e, 1, 2);
        g_ships[e].vel = v3(0,0,0);
        combat_set_player_target(e);
        float h0 = g_ships[e].hull, s0 = g_ships[e].shield;
        for (int f = 0; f < 30*4; f++) { combat_tick(1.0f/30.0f);
            g_ships[e].vel = v3(0,0,0); }   /* hold it still */
        printf("[turret] 4s: enemy shield %.0f->%.0f hull %.0f->%.0f (%s)\n",
               s0, g_ships[e].shield, h0, g_ships[e].hull,
               (g_ships[e].shield < s0 || g_ships[e].hull < h0)
                   ? "DAMAGING" : "NO DAMAGE");
        return 0;
    }

    if (getenv("ELITE_STARTSAFE")) {
        CraftRawButtons none = {0};
        for (int k = 0; k < 12; k++) elite_game_tick(&none, 1.0f/30.0f);
        const SystemInfo *si = system_info();
        char nm[14]; galaxy_system_name(si->addr, nm);
        printf("[startsafe] %-12s threat=%d stations=%d\n",
               nm, si->threat, si->n_stations);
        return 0;
    }

    if (getenv("ELITE_HPMATRIX")) {
        /* Average effective HP (shield + hull) per hull class, bare vs
         * fitted armour+shield at each tier (STANDARD quality, 100%%).
         * Shows the spread of total damage needed to kill each ship. */
        extern float equip_mult(const WeaponInst *);
        WeaponInst fit = { .in_use = 1, .quality = Q_STANDARD,
                           .integrity = 100 };
        WeaponInst bare = { .in_use = 0 };
        float emf = equip_mult(&fit), emb = equip_mult(&bare);
        printf("[hp] %-9s baseH baseS |  BARE   Z1    Z2    Z3   "
               "(eff HP = shield+hull, STD qual)\n", "HULL");
        for (int hc = 0; hc < N_HULLS; hc++) {
            const HullDef *h = &k_hulls[hc];
            float ah = 0, as = 0; int N = 300;
            for (int sd = 0; sd < N; sd++) {
                HullRoll rv;
                hull_roll(hc, (uint32_t)(0x100u + sd) * 2654435761u, &rv);
                ah += rv.hull; as += rv.shd;
            }
            ah /= N; as /= N;
            float bh = h->hull_base * ah, bs = h->shield_base * as;
            float bareHP = bh * emb + bs * emb;
            printf("[hp] %-9s %5.0f %5.0f | %5.0f", h->name, bh, bs, bareHP);
            for (int t = 1; t <= 3; t++) {
                int ht = t <= h->max_hull_tier ? t : h->max_hull_tier;
                int st = t <= h->max_shield_tier ? t : h->max_shield_tier;
                float hp = bh * k_tier_mult[ht] * emf
                         + bs * k_tier_mult[st] * emf;
                int capped = (t > h->max_hull_tier || t > h->max_shield_tier);
                printf(" %5.0f%s", hp, capped ? "*" : " ");
            }
            printf("  [maxA Z%d maxS Z%d]\n",
                   h->max_hull_tier, h->max_shield_tier);
        }
        printf("[hp] * = hull can't reach that tier (shown at its cap). "
               "PROTOTYPE quality adds ~+125%%.\n");
        return 0;
    }

    if (getenv("ELITE_BENCH")) {
        /* REALISTIC engagement matrix: the player ACTIVELY fights --
         * pursues the enemy (steers at it, holds combat range) but
         * jinks imperfectly (~45% of the time), like a competent human.
         * The enemy is immortal and unarmed-player, so we measure how
         * fast each tier/weapon kills a real maneuvering target -- not
         * SPEEDKILL's fly-straight-away nor a forced point-blank orbit
         * that trips the anti-ram break-off. */
        extern const Mesh *hull_mesh(uint32_t, int);
        int NT = atoi(getenv("ELITE_BENCH")); if (NT < 1) NT = 6;
        extern uint32_t g_dbg_npc_shots, g_dbg_player_hits;
        /* store per (weapon,tier): avg shots fired + avg hit% */
        static float g_shots[WPN_COUNT][5], g_hitpc[WPN_COUNT][5];
        static float g_ttk[WPN_COUNT][5];   /* <0 = no kill (>cap) */
        static float g_hits[WPN_COUNT][5];
        uint32_t br = 0x1234567u;
        #define BR() (br ^= br<<13, br ^= br>>17, br ^= br<<5, br)
        #define BRF(a,b) ((a) + ((b)-(a)) * ((BR() & 0xFFFF) / 65535.0f))
        for (int wt = 0; wt < WPN_COUNT; wt++) {
            if (wt == WPN_MINE || wt == WPN_TRACTOR) continue;
            for (int tier = 0; tier <= 4; tier++) {
                float tsum = 0; int tn = 0, surv = 0;
                float csh = 0, chit = 0;
                for (int trial = 0; trial < NT; trial++) {
                    g_player.hull_id = 0; g_player.hull_seed = 0x5EEDu;
                    memset(g_player.mounts, 0, sizeof g_player.mounts);
                    memset(g_player.util_eq, 0, sizeof g_player.util_eq);
                    g_player.shield_eq = (WeaponInst){ .type = WPN_COUNT,
                        .quality = Q_STANDARD, .integrity = 100,
                        .in_use = 1, .tier = 1 };
                    player_apply_to_ship();
                    Ship *pl = &g_ships[0];
                    pl->hull = pl->hull_max; pl->shield = pl->shield_max;
                    pl->pos = v3(0, 0, -180); pl->basis = m3_identity();
                    pl->vel = v3_scale(pl->basis.r[2], pl->max_speed*0.5f);
                    float ea = BRF(0, 6.2831f);
                    int e = ship_spawn(hull_mesh(0xACE1u + trial, 1+tier),
                                       v3(cosf(ea)*70.0f, BRF(-30,30),
                                          0.0f), TEAM_HOSTILE);
                    if (e <= 0) { printf(" spawn!"); continue; }
                    ship_set_tier(e, tier, 1 + tier);
                    Ship *t2 = &g_ships[e];
                    t2->weapons[0] = (uint8_t)wt; t2->n_weapons = 1;
                    t2->active_w = 0; t2->turret_type = 0;
                    t2->ammo[0] = k_weapons[wt].ammo_max
                                      ? k_weapons[wt].ammo_max : -1;
                    t2->hull = t2->hull_max = 1e9f;   /* immortal enemy */
                    t2->ai_target = 0;
                    float tkill = -1; CraftRawButtons none = {0};
                    g_dbg_npc_shots = 0; g_dbg_player_hits = 0;
                    /* RACETRACK FIGURE-8: long STRAIGHT runs (66% speed,
                     * predictable -> the enemy gets a clean continuous
                     * firing solution so the shield-regen delay never
                     * resets) joined by SLOW half-loop turns (50% speed),
                     * alternating direction = a figure-8. Phase/dir vary
                     * per trial. */
                    float maxv = pl->max_speed;
                    const float L_STRAIGHT = 270.0f;   /* long straights */
                    const float TURN_RATE = 1.10f;     /* 180 deg in ~2.9s < shield delay */
                    float psi = BRF(0, 6.2831f);
                    Vec3 pos = v3(0, 0, -120);
                    int st8 = 0; float seg = 0, tacc = 0;
                    float tsign = (BR() & 1) ? 1.0f : -1.0f;
                    Vec3 prev = pos;
                    for (int f = 0; f < 30 * 120; f++) {
                        float speed;
                        if (st8 == 0) {                /* STRAIGHT */
                            speed = maxv * 0.50f;
                            seg += speed / 30.0f;
                            if (seg >= L_STRAIGHT) { st8 = 1; tacc = 0; }
                        } else {                       /* SLOW TURN */
                            speed = maxv * 0.40f;
                            float dpsi = TURN_RATE / 30.0f * tsign;
                            psi += dpsi; tacc += dpsi < 0 ? -dpsi : dpsi;
                            if (tacc >= 3.14159f) {     /* half loop done */
                                st8 = 0; seg = 0; tsign = -tsign;
                            }
                        }
                        Vec3 dir = v3(sinf(psi), 0, cosf(psi));
                        pos = v3_add(pos, v3_scale(dir, speed / 30.0f));
                        pl->vel = v3_scale(v3_sub(pos, prev), 30.0f);
                        prev = pos; pl->pos = pos;
                        pl->basis.r[2] = dir;
                        pl->basis.r[0] = v3(dir.z, 0, -dir.x);
                        pl->basis.r[1] = v3(0, 1, 0);
                        elite_game_tick(&none, 1.0f / 30.0f);
                        if (!pl->alive || pl->hull <= 0) {
                            tkill = (float)f / 30.0f; break;
                        }
                    }
                                        if (tkill >= 0) { tsum += tkill; tn++; } else surv++;
                    csh += (float)g_dbg_npc_shots;
                    chit += (float)g_dbg_player_hits;
                    g_ships[e].alive = false; g_ships[0].alive = true;
                    proj_clear_all();
                }
                g_shots[wt][tier] = csh / (NT > 0 ? NT : 1);
                g_hitpc[wt][tier] = csh > 0 ? 100.0f * chit / csh : 0.0f;
                g_hits[wt][tier] = chit / (NT > 0 ? NT : 1);
                g_ttk[wt][tier] = (tn > NT/2) ? tsum/tn : -1.0f;
            }
        }
        /* ONE combined table: time-to-kill (s) and hit-rate %% per cell */
        printf("[bench] === kill-time(s)/hit%%/shots/hits -- racetrack-8 50%%-40%% ===\n");
        printf("[bench] %-9s   T0         T1         T2         T3         T4\n",
               "WEAPON");
        for (int wt = 0; wt < WPN_COUNT; wt++) {
            if (wt == WPN_MINE || wt == WPN_TRACTOR) continue;
            printf("[bench] %-9s", k_weapons[wt].name);
            for (int tier = 0; tier <= 4; tier++) {
                if (g_ttk[wt][tier] < 0)
                    printf("  >cap/%2.0f%%/%4.0fsh/%3.0fhit", g_hitpc[wt][tier],
                           g_shots[wt][tier], g_hits[wt][tier]);
                else
                    printf(" %5.1f/%2.0f%%/%4.0fsh/%3.0fhit", g_ttk[wt][tier],
                           g_hitpc[wt][tier], g_shots[wt][tier], g_hits[wt][tier]);
            }
            printf("\n");
        }
        #undef BR
        #undef BRF
        return 0;
    }

    if (getenv("ELITE_SPEEDKILL")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        printf("[sk] %-9s", "WEAPON");
        for (int t = 0; t <= 4; t++) printf("  T%d   ", t);
        printf("\n");
        for (int wt = 0; wt < WPN_COUNT; wt++) {
            if (wt == WPN_MINE || wt == WPN_TRACTOR) continue;
            printf("[sk] %-9s", k_weapons[wt].name);
            for (int tier = 0; tier <= 4; tier++) {
              float tsum = 0; int tn = 0, surv = 0, ammo = 0;
              int NT = atoi(getenv("ELITE_SPEEDKILL"));
              if (NT < 1) NT = 1;
              for (int trial = 0; trial < NT; trial++) {
                /* fresh standard player */
                g_player.hull_id = 0;
                g_player.hull_seed = 0x5EEDu;
                memset(g_player.mounts, 0, sizeof g_player.mounts);
                memset(g_player.util_eq, 0, sizeof g_player.util_eq);
                g_player.shield_eq = (WeaponInst){ .type = WPN_COUNT,
                    .quality = Q_STANDARD, .integrity = 100,
                    .in_use = 1, .tier = 1 };
                player_apply_to_ship();
                Ship *pl = &g_ships[0];
                pl->hull = pl->hull_max;
                pl->shield = pl->shield_max;
                pl->pos = v3(0, 0, -600);
                pl->basis = m3_identity();
                pl->vel = v3_scale(pl->basis.r[2],
                                   pl->max_speed * 0.66f);
                pl->throttle = 0.66f;
                float ang = 0.5f + (float)trial * 1.7f;
                int e = ship_spawn(hull_mesh(0xACE1u + trial, 1 + tier),
                                   v3_add(pl->pos,
                                          v3(60.0f * cosf(ang), 0,
                                             -250.0f)),
                                   TEAM_HOSTILE);
                if (e <= 0) { printf(" spawn!"); continue; }
                ship_set_tier(e, tier, 1 + tier);
                Ship *t2 = &g_ships[e];
                t2->weapons[0] = (uint8_t)wt;
                t2->n_weapons = 1;
                t2->active_w = 0;
                t2->ammo[0] = k_weapons[wt].ammo_max
                                  ? k_weapons[wt].ammo_max : -1;
                t2->turret_type = 0;
                float tkill = -1;
                CraftRawButtons none = {0};
                for (int f = 0; f < 30 * 45; f++) {
                    elite_game_tick(&none, 1.0f / 30.0f);
                    /* hold the straight line: re-pin heading+speed */
                    pl->basis = m3_identity();
                    pl->vel = v3_scale(v3(0, 0, 1),
                                       pl->max_speed * 0.66f);
                    pl->throttle = 0.66f;
                    if (!pl->alive || pl->hull <= 0) {
                        tkill = (float)f / 30.0f;
                        break;
                    }
                    if (k_weapons[wt].ammo_max && t2->ammo[0] <= 0 &&
                        proj_count() == 0) {
                        tkill = -2;
                        break;
                    }
                    if (!t2->alive) { tkill = -3; break; }
                }
                if (tkill >= 0) { tsum += tkill; tn++; }
                else if (tkill == -2) ammo++;
                else surv++;
                if (t2->alive) t2->alive = false;
                g_ships[0].alive = true;
                proj_clear_all();
              }
              if (tn > NT / 2) printf(" %5.1f", tsum / tn);
              else if (surv >= ammo) printf("  >45s");
              else printf("  AMMO");
            }
            printf("\n");
        }
        return 0;
    }

    /* BLASTER bend: off-axis target that a straight bolt misses. */
    if (getenv("ELITE_BENDTEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        elite_game_debug_face_away_from_sun();
        for (int w = 0; w < 2; w++) {
            int wt = w ? WPN_BLASTER : WPN_PLASMA;
            int e = ship_spawn(hull_mesh(0xACE1u, 2),
                               v3_add(pl->pos,
                                      v3_add(v3_scale(pl->basis.r[2],
                                                      300.0f),
                                             v3_scale(pl->basis.r[0],
                                                      28.0f))),
                               TEAM_HOSTILE);
            ship_set_tier(e, 0, 2);
            Ship *t2 = &g_ships[e];
            t2->n_weapons = 0;
            t2->vel = v3(0, 0, 0);
            Vec3 hold = t2->pos;
            float s0 = t2->shield + t2->hull;
            g_player.mounts[0] = (WeaponInst){ .type = (uint8_t)wt,
                .quality = Q_STANDARD, .integrity = 100, .in_use = 1 };
            g_player.ammo[0] = -1;
            player_apply_to_ship();
            pl->active_w = 0;
            CraftRawButtons none = {0};
            { CraftRawButtons lb = none; lb.lb = true;
              elite_game_tick(&lb, 1.0f / 30.0f);
              for (int k = 0; k < 8; k++)
                  elite_game_tick(&none, 1.0f / 30.0f); }
            for (int f = 0; f < 90; f++) {
                pl->fire_cool = 0; pl->heat = 0;
                CraftRawButtons b = none; b.a = true;
                elite_game_tick(&b, 1.0f / 30.0f);
                t2->pos = hold; t2->vel = v3(0, 0, 0);
                pl->vel = v3(0, 0, 0);
            }
            for (int f = 0; f < 60; f++) {
                elite_game_tick(&none, 1.0f / 30.0f);
                t2->pos = hold;
            }
            printf("[bend] %-8s off-axis dmg: %.0f (%s)\n",
                   k_weapons[wt].name, s0 - (t2->shield + t2->hull),
                   wt == WPN_BLASTER ? "should HIT via bend"
                                     : "straight: should miss");
            t2->alive = false;
        }
        return 0;
    }

    /* Last-ditch break success rates (chaff stripped): want ~50%% at
     * ELITE per user spec, worse below. */
    if (getenv("ELITE_DODGETEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        elite_game_debug_face_away_from_sun();
        float launch_d = (float)atoi(getenv("ELITE_DODGETEST"));
        if (launch_d < 50) launch_d = 350;
        for (int tier = 2; tier <= 4; tier++) {
            int hits = 0, shots = 20;
            for (int n = 0; n < shots; n++) {
                int e = ship_spawn(hull_mesh(0xACE1u + n, 1 + tier),
                                   v3_add(pl->pos,
                                          v3_scale(pl->basis.r[2],
                                                   launch_d)),
                                   TEAM_HOSTILE);
                if (e <= 0) continue;
                ship_set_tier(e, tier, 1 + tier);
                Ship *t2 = &g_ships[e];
                t2->chaff_n = 0;              /* isolate the run */
                t2->n_weapons = 0;
                /* realistic: already at fight speed, flying across */
                t2->vel = v3_scale(t2->basis.r[2],
                                   t2->max_speed * 0.6f);
                float hp0 = t2->shield + t2->hull;
                g_player.mounts[0] = (WeaponInst){
                    .type = WPN_HOMING, .quality = Q_STANDARD,
                    .integrity = 100, .in_use = 1 };
                g_player.ammo[0] = 1;
                player_apply_to_ship();
                pl->active_w = 0;
                pl->fire_cool = 0; pl->heat = 0;
                combat_set_shot_type(WPN_HOMING);
                combat_fire(0, 0, e);
                CraftRawButtons none = {0};
                for (int f = 0; f < 30 * 10; f++) {
                    elite_game_tick(&none, 1.0f / 30.0f);
                    pl->shield = pl->shield_max;
                    pl->hull = pl->hull_max;
                    if (getenv("ELITE_DODGEDBG") && n == 0 &&
                        tier == 4 && (f % 15) == 0)
                        printf("[ddbg] f=%d md=%.0f thr=%.2f spd=%.0f\n",
                               f, proj_nearest_homing(e), t2->throttle,
                               v3_len(t2->vel));
                    if (!t2->alive) break;
                    if (proj_nearest_homing(e) > 9e8f && f > 20) break;
                }
                if (!t2->alive || t2->shield + t2->hull < hp0 - 1.0f)
                    hits++;
                if (getenv("ELITE_DODGEDBG") && n < 3 && tier == 4)
                    printf("[ddbg] n=%d hit=%d\n", n,
                           (!t2->alive ||
                            t2->shield + t2->hull < hp0 - 1.0f));
                if (t2->alive) t2->alive = false;
            }
            printf("[dodge] tier %d: %d/%d hit (%d%%%% dodge)\n", tier,
                   hits, shots, 100 - hits * 100 / shots);
        }
        return 0;
    }

    /* NPC missile countermeasures ladder. */
    if (getenv("ELITE_CMTEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        elite_game_debug_face_away_from_sun();
        for (int tier = 1; tier <= 3; tier++) {
            int e = ship_spawn(hull_mesh(0xACE1u, 1 + tier),
                               v3_add(pl->pos,
                                      v3_scale(pl->basis.r[2], 500.0f)),
                               TEAM_HOSTILE);
            ship_set_tier(e, tier, 1 + tier);
            Ship *t2 = &g_ships[e];
            int chaff0 = t2->chaff_n;
            Vec3 head0 = t2->basis.r[2];
            g_player.mounts[0] = (WeaponInst){ .type = WPN_HOMING,
                .quality = Q_STANDARD, .integrity = 100, .in_use = 1 };
            g_player.ammo[0] = 10;
            player_apply_to_ship();
            pl->active_w = 0;
            pl->fire_cool = 0; pl->heat = 0;
            combat_set_shot_type(WPN_HOMING);
            combat_fire(0, 0, e);
            CraftRawButtons none = {0};
            int blinded = 0;
            for (int f = 0; f < 30 * 6 && t2->alive; f++) {
                elite_game_tick(&none, 1.0f / 30.0f);
                if (proj_nearest_homing(e) > 9e8f && f > 30) break;
            }
            float turn_amt = 1.0f - v3_dot(head0, t2->basis.r[2]);
            printf("[cm] tier %d: alive=%d chaff %d->%d heading-change="
                   "%.2f %s\n", tier, t2->alive, chaff0, t2->chaff_n,
                   turn_amt,
                   tier >= 3 ? (t2->chaff_n < chaff0 ? "(CHAFFED)" : "(hit?)")
                 : tier >= 2 ? (turn_amt > 0.05f ? "(BROKE)" : "(no react)")
                             : "(oblivious ok)");
            if (t2->alive) t2->alive = false;
        }
        return 0;
    }

    /* Measured DPS rig (user req): every shot hits a huge stationary
     * target 200m ahead. BURST = heat zeroed (cooldown-limited);
     * SUSTAINED = real heat. Damage read off combined shield+hull
     * delta so ION's split and P.LANCE's bypass both count. */
    if (getenv("ELITE_DPSTEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        /* ELITE_DPSTEST=<fps>: cooldowns quantize to frames, and the
         * DEVICE runs 80-90fps uncapped (user) — 30fps rig numbers
         * undersell fast weapons. */
        int fps = atoi(getenv("ELITE_DPSTEST"));
        if (fps < 30) fps = 30;
        float rdt = 1.0f / (float)fps;
        elite_game_debug_face_away_from_sun();
        printf("[dps] %-9s %7s %9s\n", "WEAPON", "BURST", "SUSTAINED");
        for (int wt = 0; wt < WPN_COUNT; wt++) {
            if (wt == WPN_MINE || wt == WPN_TRACTOR) continue;
            float res[2] = { 0, 0 };
            for (int mode = 0; mode < 2; mode++) {
                int e = ship_spawn(hull_mesh(0xACE1u, 5),
                                   v3_add(pl->pos,
                                          v3_scale(pl->basis.r[2],
                                                   200.0f)),
                                   TEAM_HOSTILE);
                if (e <= 0) continue;
                ship_set_tier(e, 0, 5);
                Ship *t2 = &g_ships[e];
                t2->shield_max = 30000.0f; t2->shield = 30000.0f;
                t2->hull_max = 30000.0f; t2->hull = 30000.0f;
                t2->shield_regen = 0;
                t2->n_weapons = 0;          /* the target doesn't shoot
                                               back — an armed MAULER was
                                               killing the RIG (insurance
                                               respawn read as 7.5k dps) */
                t2->turret_type = 0;
                Vec3 hold = t2->pos;
                /* a clean STANDARD 100% instance — dmg_mult reads the
                 * PLAYER mount, not the ship slot */
                memset(g_player.mounts, 0, sizeof g_player.mounts);
                g_player.mounts[0] = (WeaponInst){ .type = (uint8_t)wt,
                    .quality = Q_STANDARD, .integrity = 100,
                    .in_use = 1 };
                g_player.ammo[0] = k_weapons[wt].ammo_max
                                       ? k_weapons[wt].ammo_max : -1;
                player_apply_to_ship();
                pl->active_w = 0;
                pl->heat = 0; pl->fire_cool = 0;
                float d0 = t2->shield + t2->hull;
                CraftRawButtons b = { 0 };
                int frames = fps * 8;
                for (int f = 0; f < frames; f++) {
                    /* charge weapons need press-release cycling */
                    if (wt == WPN_RAILGUN || wt == WPN_GAUSS)
                        b.a = (f % (fps * 4 / 3)) < (fps * 6 / 5);
                    else
                        b.a = true;
                    if (mode == 0) pl->heat = 0;       /* burst */
                    if (k_weapons[wt].ammo_max) pl->ammo[0] = 500;
                    elite_game_tick(&b, rdt);
                    t2->pos = hold; t2->vel = v3(0, 0, 0);
                    t2->alive = true;
                    pl->vel = v3(0, 0, 0);
                    pl->shield = pl->shield_max;   /* rig armour */
                    pl->hull = pl->hull_max;
                }
                /* let projectiles in flight land */
                b.a = false;
                for (int f = 0; f < fps * 3 / 2; f++) {
                    elite_game_tick(&b, rdt);
                    t2->pos = hold; t2->vel = v3(0, 0, 0);
                    t2->alive = true;
                }
                res[mode] = (d0 - (t2->shield + t2->hull)) / 8.0f;
                t2->alive = false;
            }
            printf("[dps] %-9s %7.1f %9.1f\n", k_weapons[wt].name,
                   res[0], res[1]);
        }
        return 0;
    }

    /* Paying the fine must calm hostile police. */
    if (getenv("ELITE_FINETEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        int e = ship_spawn(hull_mesh(0xC0Bu, 3),
                           v3_add(g_ships[0].pos, v3(0, 0, 300)),
                           TEAM_HOSTILE);
        ship_set_tier(e, 2, 3);
        g_ships[e].is_police = 1;
        g_ships[e].ai_target = 0;
        g_player.legal = 1; g_player.fine = 500;
        elite_game_police_stand_down();      /* what PAY FINE now calls */
        printf("[fine] after stand-down: team=%d (%s) ai_target=%d\n",
               g_ships[e].team,
               g_ships[e].team == TEAM_NEUTRAL ? "CALMED" : "STILL HOSTILE",
               g_ships[e].ai_target);
        return 0;
    }

    /* Tier showcase movie: one duel per tier, captions in post. */
    if (getenv("ELITE_TIERMOVIE")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        CraftRawButtons none = {0};
        int frame = 0;
        char fn[64];
        #define TMV(btns) do { \
            CraftRawButtons _b2 = (btns); \
            elite_game_tick(&_b2, 1.0f / 30.0f); \
            render_frame(); \
            snprintf(fn, sizeof fn, "/tmp/tmovie/f_%05d.ppm", frame++); \
            dump_ppm(fn); \
        } while (0)
        #define TMV_STEER(aimpt, rate) do { \
            Vec3 _want = v3_norm(v3_sub((aimpt), pl->pos)); \
            Vec3 _cur = pl->basis.r[2]; \
            Vec3 _ax = v3_cross(_cur, _want); \
            float _sa = v3_len(_ax); \
            if (_sa > 1e-5f) { \
                float _ang = asinf(_sa > 1.0f ? 1.0f : _sa); \
                float _step = _ang < (rate) ? _ang : (rate); \
                m3_rotate_world(&pl->basis, v3_scale(_ax, 1.0f / _sa), \
                                _step); \
                m3_orthonormalize(&pl->basis); \
            } \
        } while (0)
        Ship *pl = &g_ships[0];
        /* hero: a VIPER with one REINFORCED PULSE-M — fights last long
         * enough to watch them fly */
        g_player.hull_id = 3;
        g_player.hull_seed = 0x77AA1u;
        memset(g_player.mounts, 0, sizeof g_player.mounts);
        g_player.mounts[0] = (WeaponInst){ .type = WPN_PULSE_S,
            .quality = Q_STANDARD, .integrity = 100, .in_use = 1 };
        g_player.ammo[0] = -1;
        g_player.shield_eq = (WeaponInst){ .type = WPN_COUNT,
            .quality = Q_MILITARY, .integrity = 100, .in_use = 1,
            .tier = 2 };
        player_apply_to_ship();
        elite_game_debug_face_away_from_sun();
        for (int tier = 0; tier <= 4; tier++) {
            pl->hull = pl->hull_max;
            pl->shield = pl->shield_max;
            pl->vel = v3_scale(pl->basis.r[2], 20.0f);
            int e = ship_spawn(hull_mesh(0xACE1u * (tier + 3), 1 + tier),
                               v3_add(pl->pos,
                                      v3_scale(pl->basis.r[2], 420.0f)),
                               TEAM_HOSTILE);
            if (e <= 0) break;
            ship_set_tier(e, tier, 1 + tier);
            printf("[tmovie] tier %d starts at frame %d\n", tier, frame);
            CraftRawButtons bb = none;
            bb.lb = true;
            TMV(bb);                                  /* lock */
            /* ELITE_TIERMOVIE=2: HONEST HANDS (user req) — no basis
             * surgery. D-pad only, 0.2s reaction lag, a deadband, one
             * axis at a time: the way a thumb actually flies. Fights
             * become joust-and-reacquire, like real play. */
            int mode = atoi(getenv("ELITE_TIERMOVIE"));
            int honest = mode == 2;
            if (mode == 3) {
                /* missile showcase: 10 rounds, one every 5s */
                memset(g_player.mounts, 0, sizeof g_player.mounts);
                g_player.mounts[0] = (WeaponInst){ .type = WPN_HOMING,
                    .quality = Q_STANDARD, .integrity = 100,
                    .in_use = 1 };
                g_player.ammo[0] = 10;
                player_apply_to_ship();
                pl->active_w = 0;
            }
            int lagN = 6;
            float lag_x[8] = { 0 }, lag_y[8] = { 0 };
            int li = 0;
            int fmax = 30 * ((atoi(getenv("ELITE_TIERMOVIE")) == 3)
                                 ? 58 : 26);
            for (int f = 0; f < fmax && g_ships[e].alive; f++) {
                Ship *t2 = &g_ships[e];
                float d = v3_len(v3_sub(t2->pos, pl->pos));
                CraftRawButtons b3 = none;
                Vec3 l = m3_mul_v3_t(&pl->basis,
                                     v3_sub(t2->pos, pl->pos));
                int watching = ((f + 180) % 240) >= 45;
                if (mode == 3) {
                    /* film the missiles: gentle framing, volley every
                     * 5s until the magazine runs dry */
                    TMV_STEER(t2->pos, 0.05f);
                    pl->throttle = (d > 320.0f) ? 0.9f
                                 : (d > 140.0f) ? 0.5f : 0.25f;
                    CraftRawButtons b4 = none;
                    if (f % 150 == 20 && pl->ammo[0] > 0) {
                        pl->fire_cool = 0;
                        pl->heat = 0;
                        b4.a = true;
                    }
                    if (pl->shield < pl->shield_max * 0.3f)
                        pl->shield = pl->shield_max;
                    TMV(b4);
                    if (pl->ammo[0] <= 0 &&
                        proj_nearest_homing(e) > 9e8f && (f % 150) > 120)
                        break;             /* dry and nothing in flight */
                    continue;
                }
                if (honest) {
                    /* see where it was lagN frames ago */
                    lag_x[li % 8] = l.x; lag_y[li % 8] = l.y; li++;
                    float sx = lag_x[(li + 8 - lagN) % 8];
                    float sy = lag_y[(li + 8 - lagN) % 8];
                    float dead = d * 0.06f + 2.0f;
                    /* one axis at a time, biggest error first */
                    if (l.z < 0) {
                        /* behind us: pick a turn and hold it */
                        b3.up = true;
                    } else if (sx < -dead || sx > dead) {
                        b3.left = sx < 0; b3.right = sx > 0;
                    } else if (sy < -dead || sy > dead) {
                        /* flight-stick: UP = nose DOWN */
                        b3.up = sy > 0; b3.down = sy < 0;
                    }
                    pl->throttle = 0.85f;
                    if (!watching && l.z > 0 && d < 500.0f &&
                        l.x * l.x + l.y * l.y < d * d * 0.012f)
                        b3.a = true;
                } else {
                    pl->throttle = (d > 250.0f) ? 1.0f
                                 : (d > 120.0f) ? 0.72f : 0.5f;
                    TMV_STEER(t2->pos, 0.07f);
                    if (!watching && l.z > 0 && d < 650.0f &&
                        l.x * l.x + l.y * l.y < d * d * 0.018f)
                        b3.a = true;
                }
                if (pl->shield < pl->shield_max * 0.3f)
                    pl->shield = pl->shield_max;      /* camera rig armor */
                TMV(b3);
            }
            printf("[tmovie] tier %d kill at frame %d\n", tier, frame);
            for (int f = 0; f < 50; f++) TMV(none);   /* explosion linger */
        }
        printf("[tmovie] %d frames\n", frame);
        #undef TMV
        #undef TMV_STEER
        return 0;
    }

    /* Armoury showcase movie: every weapon in the catalogue, one
     * segment each. Frames -> /tmp/movie/f_*.ppm, game audio ->
     * /tmp/guide_audio.raw (735 samples/frame), [card]/[cap] markers
     * on stdout for tools/guide/compose_weapons.py. ELITE_WPNMOVIE=1. */
    if (getenv("ELITE_WPNMOVIE")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        CraftRawButtons none = {0};
        static int wf = 0;
        char wfn[64];
        FILE *gwav = fopen("/tmp/guide_audio.raw", "wb");
        #define WMV(btns) do { \
            CraftRawButtons _wb = (btns); \
            elite_game_tick(&_wb, 1.0f / 30.0f); \
            if (gwav) { int16_t _ab[735]; audio_render(_ab, 735); \
                        fwrite(_ab, 2, 735, gwav); } \
            render_frame(); \
            snprintf(wfn, sizeof wfn, "/tmp/movie/f_%05d.ppm", wf++); \
            dump_ppm(wfn); \
        } while (0)
        #define WMV_IDLE(n) \
            do { for (int _i = 0; _i < (n); _i++) WMV(none); } while (0)
        /* Camera-rig steering: rotate SHIP s's basis toward an aim
         * point, capped at rate rad/frame (same trick as TIERMOVIE). */
        #define WMV_STEERS(s, aimpt, rate) do { \
            Vec3 _want = v3_norm(v3_sub((aimpt), (s)->pos)); \
            Vec3 _cur = (s)->basis.r[2]; \
            Vec3 _ax = v3_cross(_cur, _want); \
            float _sa = v3_len(_ax); \
            if (_sa > 1e-5f) { \
                float _ang = asinf(_sa > 1.0f ? 1.0f : _sa); \
                float _step = _ang < (rate) ? _ang : (rate); \
                m3_rotate_world(&(s)->basis, \
                                v3_scale(_ax, 1.0f / _sa), _step); \
                m3_orthonormalize(&(s)->basis); \
            } \
        } while (0)
        /* Lead-aim at ship tgt and fire when inside the cone. */
        #define WMV_AIM(tgt, wspd, cone, press_a) do { \
            Ship *_t = &g_ships[tgt]; \
            float _d = v3_len(v3_sub(_t->pos, pl->pos)); \
            float _tt = (wspd) > 0 ? _d / (wspd) : 0; \
            Vec3 _aim = v3_add(_t->pos, \
                               v3_scale(v3_sub(_t->vel, pl->vel), _tt)); \
            WMV_STEERS(pl, _aim, 0.075f); \
            Vec3 _l = m3_mul_v3_t(&pl->basis, v3_sub(_aim, pl->pos)); \
            CraftRawButtons _b2 = none; \
            if ((press_a) && _l.z > 0 && \
                _l.x * _l.x + _l.y * _l.y < _d * _d * (cone)) \
                _b2.a = true; \
            WMV(_b2); \
        } while (0)
        #define WCARD(name, sub) printf("[card] %d %s|%s\n", wf, name, sub)
        #define WCAP(txt) printf("[cap] %d %s\n", wf, txt)

        Ship *pl = &g_ships[0];
        g_player.hull_id = 5;                 /* MAULER: size-3 slot 0 */
        g_player.hull_seed = 0x5EED77u;
        g_player.shield_eq = (WeaponInst){ .type = WPN_COUNT,
            .quality = Q_MILITARY, .integrity = 100, .in_use = 1,
            .tier = 2 };
        elite_game_debug_face_away_from_sun();

        enum { K_STD, K_RAIL, K_FLAK, K_HOMING, K_MINE, K_TRACTOR,
               K_MINING, K_BEND, K_JOUST };
        static const struct {
            uint8_t type, kind;
            int16_t ammo;          /* -1 = energy */
            float dist;            /* spawn range */
            float hp;              /* target hull override, 0 = keep */
            float sh;              /* target shield override */
            const char *card_sub;
            const char *cap;
        } segs[] = {
            { WPN_PULSE_S, K_STD, -1, 330, 115, 0, "light laser",
              "PULSE-S: the starter gun. No ammo -- just heat" },
            { WPN_PULSE_M, K_STD, -1, 380, 145, 0, "medium laser",
              "PULSE-M: the size-2 workhorse of the lanes" },
            { WPN_PULSE_L, K_STD, -1, 420, 170, 0, "heavy laser",
              "PULSE-L: heavy mounts only -- big punch per shot" },
            { WPN_BEAM, K_STD, -1, 300, 140, 0, "continuous beam",
              "BEAM: hold the line on target -- and watch the heat" },
            { WPN_PHOTON, K_STD, -1, 420, 150, 0, "photon cannon",
              "PHOTON: slow bright bolts. Lead the shot, land a truck" },
            { WPN_GAUSS, K_STD, 24, 650, 130, 0, "hypervelocity slug",
              "GAUSS: a railhead sniper -- 24 rounds, lead the dot" },
            { WPN_AUTOCANNON, K_STD, 200, 330, 140, 0, "ballistic stream",
              "AUTOCANNON: a 200-round hose. Walk it onto the hull" },
            { WPN_MISSILE, K_JOUST, 8, 460, 100, 0, "dumbfire rocket",
              "MISSILE: unguided, 22m blast -- forgiving aim" },
            { WPN_HOMING, K_HOMING, 10, 480, 85, 0, "seeker missile",
              "HOMING: lock with LB, fire and forget" },
            { WPN_FLAK, K_FLAK, 60, 380, 120, 0, "200m airburst",
              "FLAK: every shell bursts at 200m. Timing IS the skill" },
            { WPN_RAILGUN, K_RAIL, 12, 700, 160, 0, "charged lance",
              "RAILGUN: hold A to charge -- release sends the lance" },
            { WPN_ION, K_STD, -1, 330, 30, 170, "shield stripper",
              "ION: melts shields. A full strip scrambles their systems" },
            { WPN_MINE, K_MINE, 6, 450, 90, 0, "proximity mine",
              "MINE: dropped astern, proximity-fuzed. Shake your tail" },
            { WPN_TRACTOR, K_TRACTOR, -1, 200, 0, 0, "salvage beam",
              "TRACTOR: reel in floating cargo -- no lock needed" },
            { WPN_MINING, K_MINING, -1, 260, 0, 0, "ore laser",
              "MINING LASER: crack asteroids, scoop the ore" },
            { WPN_PLASMA, K_STD, -1, 330, 150, 0, "plasma stream",
              "PLASMA: a rapid stream of starfire" },
            { WPN_LANCE, K_STD, -1, 330, 130, 220, "shield-phasing beam",
              "PLASMA LANCE: phases clean through shields to bare hull" },
            { WPN_BLASTER, K_BEND, -1, 380, 95, 0, "lock-bending bolts",
              "BLASTER: its bolts BEND toward your lock" },
        };
        const int NSEG = (int)(sizeof segs / sizeof segs[0]);

        for (int si = 0; si < NSEG; si++) {
            /* clear the stage: despawn leftovers, fresh paint */
            for (int i = 1; i < MAX_SHIPS; i++) g_ships[i].alive = false;
            memset(g_player.mounts, 0, sizeof g_player.mounts);
            g_player.mounts[0] = (WeaponInst){ .type = segs[si].type,
                .quality = Q_STANDARD, .integrity = 100, .in_use = 1 };
            g_player.ammo[0] = segs[si].ammo;
            player_apply_to_ship();
            pl->active_w = 0;
            pl->heat = 0;
            pl->fire_cool = 0;
            pl->hull = pl->hull_max;
            pl->shield = pl->shield_max;
            pl->vel = v3_scale(pl->basis.r[2], 20.0f);
            pl->throttle = 0.5f;
            WCARD(k_weapons[segs[si].type].name, segs[si].card_sub);
            WCAP(segs[si].cap);
            printf("[wpn] %s start=%d\n",
                   k_weapons[segs[si].type].name, wf);
            Vec3 fwd = pl->basis.r[2];

            if (segs[si].kind == K_TRACTOR) {
                /* beat 1: canisters adrift ahead -- pull them in */
                extern void loot_spawn_good(Vec3, Vec3, int, int);
                Vec3 rgt = pl->basis.r[0], up = pl->basis.r[1];
                for (int c = 0; c < 3; c++) {
                    Vec3 cp = v3_add(pl->pos,
                              v3_add(v3_scale(fwd, 130.0f + 35.0f * c),
                              v3_add(v3_scale(rgt, (c - 1) * 26.0f),
                                     v3_scale(up, (c & 1) ? 14.0f
                                                          : -10.0f))));
                    loot_spawn_good(cp, v3(0, 0, 0), c, 1);
                }
                pl->throttle = 0.15f;
                CraftRawButtons hold = none; hold.a = true;
                for (int f = 0; f < 200; f++) WMV(hold);
                /* beat 2: grapple a runner -- lock and hold the beam */
                WCAP("...or GRAPPLE a locked ship and pin it in place");
                int e2 = ship_spawn(hull_mesh(0xC0FFEEu, 1),
                                    v3_add(pl->pos,
                                           v3_scale(pl->basis.r[2],
                                                    170.0f)),
                                    TEAM_HOSTILE);
                if (e2 > 0) {
                    ship_set_tier(e2, 0, 1);
                    CraftRawButtons lb = none; lb.lb = true;
                    WMV(lb);
                    for (int f = 0; f < 170; f++) {
                        WMV_STEERS(pl, g_ships[e2].pos, 0.07f);
                        pl->shield = pl->shield_max;
                        WMV(hold);
                    }
                }
                WMV_IDLE(20);
                continue;
            }

            if (segs[si].kind == K_MINING) {
                /* drop the camera rig next to a fresh belt */
                rocks_spawn_field(0xBEEFu + (uint32_t)si, 6);
                Vec3 rp; float rr;
                int ri = 0;
                for (int r = 0; r < 6; r++)
                    if (rocks_get(r, &rp, &rr) && rr > 20.0f) {
                        ri = r; break;
                    }
                rocks_get(ri, &rp, &rr);
                pl->pos = v3_sub(rp, v3_scale(fwd, segs[si].dist));
                pl->vel = v3_scale(fwd, 8.0f);
                pl->throttle = 0.12f;
                int cracked = 0;
                for (int f = 0; f < 400; f++) {
                    if (!rocks_get(ri, &rp, &rr)) {
                        /* rock down -- swing to the next one */
                        if (++cracked >= 2) break;
                        WCAP("Cracked rocks spill ore -- "
                             "free money in the void");
                        int next = -1;
                        for (int r = 0; r < 6; r++)
                            if (r != ri && rocks_get(r, &rp, &rr)) {
                                next = r; break;
                            }
                        if (next < 0) break;
                        ri = next;
                        pl->throttle = 0.3f;
                    }
                    WMV_STEERS(pl, rp, 0.06f);
                    Vec3 l = m3_mul_v3_t(&pl->basis,
                                         v3_sub(rp, pl->pos));
                    CraftRawButtons b = none;
                    if (l.z > 0 &&
                        l.x * l.x + l.y * l.y < rr * rr * 0.5f)
                        b.a = true;
                    WMV(b);
                }
                WMV_IDLE(60);
                continue;
            }

            /* All remaining kinds duel a live target. */
            float dist = segs[si].dist;
            Vec3 at = (segs[si].kind == K_MINE)
                          ? v3_sub(pl->pos, v3_scale(fwd, dist))
                          : v3_add(pl->pos, v3_scale(fwd, dist));
            int e = ship_spawn(hull_mesh(0xACE1u * (si + 7), 2), at,
                               TEAM_HOSTILE);
            if (e <= 0) { printf("[wpn] SPAWN FAIL si=%d\n", si); break; }
            ship_set_tier(e, (segs[si].sh > 0) ? 3 : 1, 2);
            Ship *t = &g_ships[e];
            if (segs[si].hp > 0) t->hull = t->hull_max = segs[si].hp;
            t->shield = t->shield_max = segs[si].sh;
            CraftRawButtons lb = none; lb.lb = true;
            WMV(lb);                                /* lock it up */
            WMV_IDLE(10);                           /* hold on the box */

            int fmax = 30 * 16;
            float wspd = k_weapons[segs[si].type].speed;
            for (int f = 0; f < fmax && t->alive; f++) {
                float d = v3_len(v3_sub(t->pos, pl->pos));
                pl->shield = pl->shield_max;        /* camera rig armor */
                switch (segs[si].kind) {
                case K_HOMING: {
                    WMV_STEERS(pl, t->pos, 0.05f);
                    pl->throttle = (d > 320.0f) ? 0.8f : 0.35f;
                    CraftRawButtons b = none;
                    if (f % 75 == 20 && pl->ammo[0] > 0) {
                        pl->fire_cool = 0;
                        pl->heat = 0;
                        b.a = true;
                    }
                    WMV(b);
                    break;
                }
                case K_RAIL: {
                    pl->throttle = (d > 450.0f) ? 0.45f : 0.2f;
                    /* hold A 32 frames (full charge), release 10 */
                    int ph = f % 42;
                    Ship *_t = t;
                    float tt = d / wspd;
                    Vec3 aim = v3_add(_t->pos,
                                      v3_scale(v3_sub(_t->vel, pl->vel),
                                               tt));
                    WMV_STEERS(pl, aim, 0.075f);
                    CraftRawButtons b = none;
                    b.a = ph < 32;
                    WMV(b);
                    break;
                }
                case K_FLAK: {
                    /* hover the burst envelope: fire 175..255m */
                    pl->throttle = (d > 260.0f) ? 0.85f
                                 : (d > 160.0f) ? 0.45f : 0.0f;
                    int inwin = d > 175.0f && d < 255.0f;
                    WMV_AIM(e, wspd, 0.012f, inwin);
                    break;
                }
                case K_MINE: {
                    /* he's on our six: lay the field, then turn and
                     * watch him eat it */
                    pl->throttle = 0.55f;
                    /* puppet the pursuer down our wake */
                    t->vel = v3_scale(v3_norm(v3_sub(pl->pos, t->pos)),
                                      105.0f);
                    WMV_STEERS(t, pl->pos, 0.2f);
                    CraftRawButtons b = none;
                    if (f == 25 || f == 70 || f == 115) {
                        pl->fire_cool = 0;
                        b.a = true;
                    }
                    if (f > 135) {
                        WMV_STEERS(pl, t->pos, 0.06f);
                        pl->throttle = 0.1f;
                    }
                    WMV(b);
                    break;
                }
                case K_BEND: {
                    /* aim OFF the hull so the bend is visible; the
                     * target drifts disarmed -- the curve is the star
                     * (same setup BENDTEST validated) */
                    t->n_weapons = 0;
                    t->vel = v3_scale(t->basis.r[0], 6.0f);
                    Vec3 off = v3_add(t->pos,
                                      v3_scale(pl->basis.r[0], 30.0f));
                    WMV_STEERS(pl, off, 0.075f);
                    pl->throttle = (d > 280.0f) ? 0.6f
                                 : (d > 180.0f) ? 0.3f : 0.0f;
                    Vec3 l = m3_mul_v3_t(&pl->basis,
                                         v3_sub(t->pos, pl->pos));
                    CraftRawButtons b = none;
                    if (l.z > 0 && d < 500.0f) b.a = true;
                    WMV(b);
                    break;
                }
                case K_JOUST: {
                    /* dumbfire needs a head-on pass: puppet him
                     * straight down our throat, volley into the
                     * closure */
                    t->vel = v3_scale(v3_norm(v3_sub(pl->pos, t->pos)),
                                      70.0f);
                    WMV_STEERS(t, pl->pos, 0.2f);
                    pl->throttle = 0.3f;
                    float tt = d / wspd;
                    Vec3 aim = v3_add(t->pos,
                                      v3_scale(v3_sub(t->vel, pl->vel),
                                               tt));
                    WMV_STEERS(pl, aim, 0.08f);
                    Vec3 l = m3_mul_v3_t(&pl->basis,
                                         v3_sub(aim, pl->pos));
                    CraftRawButtons b = none;
                    if (l.z > 0 && d < 430.0f && d > 110.0f &&
                        l.x * l.x + l.y * l.y < d * d * 0.004f)
                        b.a = true;
                    WMV(b);
                    break;
                }
                default: {
                    pl->throttle = (d > 300.0f) ? 0.8f
                                 : (d > 150.0f) ? 0.5f : 0.3f;
                    float cone = (segs[si].type == WPN_MISSILE) ? 0.006f
                               : (wspd > 0) ? 0.010f : 0.012f;
                    WMV_AIM(e, wspd, cone, 1);
                    break;
                }
                }
            }
            printf("[wpn] %s %s at frame %d\n",
                   k_weapons[segs[si].type].name,
                   t->alive ? "TIMEOUT (still alive)" : "kill", wf);
            WMV_IDLE(45);                           /* fireball linger */
        }
        printf("[wpnmovie] %d frames total\n", wf);
        if (gwav) fclose(gwav);
        #undef WMV
        #undef WMV_IDLE
        #undef WMV_STEERS
        #undef WMV_AIM
        #undef WCARD
        #undef WCAP
        return 0;
    }

    /* Dogfight rework: blue zone + tail responses. */
    if (getenv("ELITE_TAILTEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        /* envelope: turn authority at half vs full speed */
        pl->vel = v3_scale(pl->basis.r[2], pl->max_speed * 0.4f);
        float e_half = turn_envelope(pl);
        pl->vel = v3_scale(pl->basis.r[2], pl->max_speed);
        float e_full = turn_envelope(pl);
        printf("[tail] envelope: 40%%spd=%.2f full=%.2f (blue zone %s)\n",
               e_half, e_full,
               (e_half == 1.0f && e_full < 0.65f) ? "OK" : "BUG");
        /* tail a VETERAN: expect a throttle chop within 2s */
        for (int tier = 1; tier <= 4; tier += 3) {
            int e = ship_spawn(hull_mesh(0xACE1u, 1 + tier),
                               v3_add(pl->pos,
                                      v3_scale(pl->basis.r[2], 120.0f)),
                               TEAM_HOSTILE);
            ship_set_tier(e, tier, 1 + tier);
            Ship *s2 = &g_ships[e];
            s2->basis = pl->basis;           /* same heading: tailed */
            s2->vel = v3_scale(pl->basis.r[2], 30.0f);
            pl->vel = v3_scale(pl->basis.r[2], 30.0f);
            float min_thr = 1.0f;
            CraftRawButtons none = {0};
            for (int f = 0; f < 60; f++) {
                elite_game_tick(&none, 1.0f / 30.0f);
                /* hold the tail geometry */
                s2->pos = v3_add(pl->pos,
                                 v3_scale(pl->basis.r[2], 120.0f));
                if (s2->throttle < min_thr) min_thr = s2->throttle;
            }
            printf("[tail] tier %d tailed: min throttle %.2f (%s)\n",
                   tier, min_thr,
                   tier >= 3 ? (min_thr < 0.2f ? "CHOPS" : "no chop?")
                             : (min_thr > 0.5f ? "runs/jinks" : "odd"));
            s2->alive = false;
        }
        return 0;
    }

    /* Rock-mode target stepping (user: lock stuck on one rock). */
    if (getenv("ELITE_ROCKCYCLE")) {
        extern int elite_game_debug_rock_target(void);
        rocks_spawn_field(0xBEEF, 6);
        CraftRawButtons none = {0}, b;
        /* LB double-tap twice: AUTO -> SALVAGE -> ROCKS */
        for (int k = 0; k < 2; k++) {
            b = none; b.lb = true;
            elite_game_tick(&b, 1.0f / 30.0f);
            elite_game_tick(&none, 1.0f / 30.0f);
            b = none; b.lb = true;
            elite_game_tick(&b, 1.0f / 30.0f);
            for (int f = 0; f < 25; f++)   /* clear the double window */
                elite_game_tick(&none, 1.0f / 30.0f);
        }
        {
            extern const char *elite_game_debug_toast(void);
            printf("[rockcycle] class toast: %s\n",
                   elite_game_debug_toast());
        }
        int seen[6], n = 0;
        for (int taps = 0; taps < 6; taps++) {
            b = none; b.lb = true;
            elite_game_tick(&b, 1.0f / 30.0f);
            for (int f = 0; f < 20; f++)
                elite_game_tick(&none, 1.0f / 30.0f);
            seen[n++] = elite_game_debug_rock_target();
        }
        printf("[rockcycle] taps -> rocks: %d %d %d %d %d %d\n",
               seen[0], seen[1], seen[2], seen[3], seen[4], seen[5]);
        int distinct = 0;
        for (int i = 0; i < n; i++) {
            int dup = 0;
            for (int j = 0; j < i; j++) if (seen[j] == seen[i]) dup = 1;
            if (!dup && seen[i] >= 0) distinct++;
        }
        printf("[rockcycle] distinct rocks visited: %d (%s)\n", distinct,
               distinct >= 3 ? "STEPPING" : "STUCK");
        return 0;
    }

    /* Cloak: engage, heat climb, AI blind, expiry, one-use. */
    if (getenv("ELITE_CLOAKTEST")) {
        g_player.util_eq[0] = (WeaponInst){ .type = EQ_CLOAK,
            .quality = 1, .integrity = 100, .in_use = 1 };
        Ship *pl = &g_ships[0];
        CraftRawButtons none = {0}, b;
        elite_game_debug_face_away_from_sun();
        float h0 = pl->heat;
        b = none; b.rb = true; b.b = true;
        elite_game_tick(&b, 1.0f / 30.0f);
        printf("[cloak] engaged: %s\n",
               elite_game_cloaked() ? "YES" : "NO");
        for (int f = 0; f < 60; f++) elite_game_tick(&none, 1.0f/30.0f);
        printf("[cloak] heat %.0f -> %.0f (climbing: %s) still on: %s\n",
               h0, pl->heat, pl->heat > h0 + 5 ? "YES" : "no",
               elite_game_cloaked() ? "YES" : "no");
        for (int f = 0; f < 30 * 7; f++)
            elite_game_tick(&none, 1.0f / 30.0f);
        printf("[cloak] after 9s: %s\n",
               elite_game_cloaked() ? "still on (BUG)" : "EXPIRED");
        b = none; b.rb = true; b.b = true;
        elite_game_tick(&b, 1.0f / 30.0f);
        printf("[cloak] re-engage same flight: %s (one-use: %s)\n",
               elite_game_cloaked() ? "ON (BUG)" : "refused",
               elite_game_cloaked() ? "NO" : "YES");
        return 0;
    }

    /* Collision physics: bounce, shield-block, size split, ram-mining.
     * Unit-style: drive collide_tick directly (the flight sim's assist
     * and AI evasion mask contacts in a scripted scenario). */
    if (getenv("ELITE_COLTEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        Ship *pl = &g_ships[0];
        int e = ship_spawn(hull_mesh(0xACE1u, 1),
                           v3_add(pl->pos, v3(0, 0, 8)), TEAM_HOSTILE);
        ship_set_tier(e, 0, 1);
        pl->vel = v3(0, 0, 14); g_ships[e].vel = v3(0, 0, -14);
        float ps0 = pl->shield, ph0 = pl->hull;
        float es0 = g_ships[e].shield, eh0 = g_ships[e].hull;
        collide_tick(0, 0, 1);
        printf("[col] head-on 28m/s: me S%.0f->%.0f H%.0f->%.0f | "
               "them S%.0f->%.0f H%.0f->%.0f\n",
               ps0, pl->shield, ph0, pl->hull, es0, g_ships[e].shield,
               eh0, g_ships[e].alive ? g_ships[e].hull : -1);
        printf("[col] shield blocked hull: %s | deflected: me vz %.0f "
               "them vz %.0f | size split (smaller hurt worse): %s\n",
               (pl->hull >= ph0 - 0.01f && pl->shield < ps0) ? "YES"
                                                             : "NO",
               pl->vel.z, g_ships[e].vel.z,
               (es0 - g_ships[e].shield) > (ps0 - pl->shield) ? "YES"
                                                              : "no");
        if (g_ships[e].alive) g_ships[e].alive = false;
        /* unshielded ram into a rock: hull pays + ore chips */
        rocks_spawn_field(0xBEEF, 4);
        Vec3 rp = v3(0,0,0); float rrad = 0;
        for (int r = 0; r < 8; r++)
            if (rocks_get(r, &rp, &rrad)) break;
        pl->shield = 0;
        pl->pos = v3_add(rp, v3(0, 0, -(rrad + 2)));
        pl->vel = v3(0, 0, 45);
        float h1 = pl->hull;
        collide_tick(0, 0, 1);
        printf("[col] rock ram 45m/s: hull %.0f -> %.0f (%s) vz %.0f\n",
               h1, pl->hull, pl->hull < h1 ? "HULL PAYS" : "no damage",
               pl->vel.z);
        /* autodock exemption: pressing INTO the ring tube, manual=0 */
        pl->pos = v3(0.70f * 100.0f + 6.0f, 0, 0);  /* 100m station */
        pl->vel = v3(-30, 0, 0);
        pl->hull = 50;
        collide_tick(1, 100.0f, 0);
        float ha = pl->hull;
        pl->shield = 0;
        collide_tick(1, 100.0f, 1);
        printf("[col] station: autodock immune %s | manual contact %s\n",
               (ha >= 50) ? "YES" : "NO",
               (pl->hull < ha || pl->vel.x > -29.9f) ? "YES" : "no");
        return 0;
    }

    /* Plasma + mining-nerf verification. */
    if (getenv("ELITE_PLASMATEST")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        int e = ship_spawn(hull_mesh(0xACE1u, 2),
                           v3_add(g_ships[0].pos, v3(0, 0, 120)),
                           TEAM_HOSTILE);
        ship_set_tier(e, 0, 2);
        g_ships[e].vel = v3(0, 0, 0);
        elite_game_debug_face_away_from_sun();
        Ship *pl = &g_ships[0];
        /* face the target */
        Vec3 f4 = v3_norm(v3_sub(g_ships[e].pos, pl->pos));
        Vec3 u4 = v3(0, 1, 0);
        Vec3 r4 = v3_norm(v3_cross(u4, f4));
        pl->basis.r[0] = r4; pl->basis.r[1] = v3_cross(f4, r4);
        pl->basis.r[2] = f4;
        ship_fit_weapon(0, 0, WPN_PLASMA);
        pl->active_w = 0;
        CraftRawButtons none = {0}, b;
        float h0 = g_ships[e].shield + g_ships[e].hull;
        for (int f = 0; f < 90; f++) {
            pl->fire_cool = 0; pl->heat = 0;
            b = none; b.a = true;
            elite_game_tick(&b, 1.0f / 30.0f);
            g_ships[e].pos = v3_add(pl->pos, v3(0, 0, 120));
            g_ships[e].vel = v3(0, 0, 0);
        }
        float h1 = g_ships[e].alive ? g_ships[e].shield + g_ships[e].hull
                                    : -1;
        printf("[plasma] target %0.f -> %.0f (%s)\n", h0, h1,
               (h1 < h0) ? "BALLS CONNECT" : "no damage");
        /* mining laser vs the same ship class: feeble */
        int e2 = ship_spawn(hull_mesh(0xBEEFu, 2),
                            v3_add(pl->pos, v3(0, 0, 120)),
                            TEAM_HOSTILE);
        ship_set_tier(e2, 0, 2);
        ship_fit_weapon(0, 0, WPN_MINING);
        pl->active_w = 0;
        float m0 = g_ships[e2].shield;
        for (int f = 0; f < 30; f++) {
            pl->fire_cool = 0; pl->heat = 0;
            b = none; b.a = true;
            elite_game_tick(&b, 1.0f / 30.0f);
            g_ships[e2].pos = v3_add(pl->pos, v3(0, 0, 120));
        }
        printf("[plasma] mining/frame-fire vs ship: %.0f (per-shot 2)\n",
               (m0 - g_ships[e2].shield));
        /* P.LANCE: full shield must stay full while hull drops */
        int e3 = ship_spawn(hull_mesh(0xCAFEu, 2),
                            v3_add(pl->pos, v3(0, 0, 120)),
                            TEAM_HOSTILE);
        ship_set_tier(e3, 1, 2);
        ship_fit_weapon(0, 0, WPN_LANCE);
        pl->active_w = 0;
        float s0 = g_ships[e3].shield, hl0 = g_ships[e3].hull;
        for (int f = 0; f < 30; f++) {
            pl->fire_cool = 0; pl->heat = 0;
            b = none; b.a = true;
            elite_game_tick(&b, 1.0f / 30.0f);
            if (!g_ships[e3].alive) break;
            g_ships[e3].pos = v3_add(pl->pos, v3(0, 0, 120));
        }
        printf("[plasma] LANCE: shield %.0f->%.0f hull %.0f->%.0f (%s)\n",
               s0, g_ships[e3].alive ? g_ships[e3].shield : -1, hl0,
               g_ships[e3].alive ? g_ships[e3].hull : -1,
               (!g_ships[e3].alive ||
                (g_ships[e3].shield >= s0 - 0.5f &&
                 g_ships[e3].hull < hl0))
                   ? "PHASES THROUGH" : "blocked");
        return 0;
    }

    /* Icon strip render (drone et al). */
    if (getenv("ELITE_ARMOURYSHOT")) {
        /* The guide's armoury sheets, in-engine (game font + icons,
         * the style the user liked): rows of [icon] NAME / tag, two
         * columns, paged into fb dumps for the compositor. */
        static const char *wtag[3] = { "Z1", "Z2", "Z3" };
        /* one item per 25px row, single column — the compositor packs
         * the cells into 3 columns */
        int item = 0, page = 0;
        while (item < WPN_COUNT) {
            memset(g_fb, 0, sizeof g_fb);
            for (int row = 0; row < 5 && item < WPN_COUNT; row++) {
                int y = row * 25 + 2;
                icon_weapon_2x(g_fb, 1, y + 2, item);
                craft_font_draw(g_fb, k_weapons[item].name, 28, y,
                                0xFFFF);
                craft_font_draw(g_fb, wtag[k_weapons[item].size - 1],
                                28, y + 9, RGB565C(245, 200, 80));
                item++;
            }
            char nm[40];
            snprintf(nm, sizeof nm, "/tmp/armoury_w_%d.ppm", page++);
            dump_ppm(nm);
        }
        /* equipment + gadgets page */
        static const struct { int type; const char *tag; } eq[] = {
            { WPN_COUNT + 0, "Z1-Z3" },   /* shield */
            { WPN_COUNT + 1, "Z1-Z3" },   /* armor */
            { EQ_HEATSINK, "UTIL" }, { EQ_SCANNER, "UTIL" },
            { EQ_TANK, "UTIL" }, { EQ_FUELSCOOP, "UTIL" },
            { EQ_TARGETCOMP, "UTIL" }, { EQ_CHAFF, "UTIL" },
            { EQ_DRONE, "UTIL" }, { EQ_CLOAK, "UTIL" },
            { EQ_MANIFEST, "UTIL" },
        };
        int ne = (int)(sizeof eq / sizeof eq[0]);
        item = 0; page = 0;
        while (item < ne) {
            memset(g_fb, 0, sizeof g_fb);
            for (int row = 0; row < 5 && item < ne; row++) {
                int y = row * 25 + 2;
                icon_weapon_2x(g_fb, 1, y + 2, eq[item].type);
                craft_font_draw(g_fb,
                                k_equip[eq[item].type - WPN_COUNT].name,
                                28, y, 0xFFFF);
                craft_font_draw(g_fb, eq[item].tag, 28, y + 9,
                                RGB565C(245, 200, 80));
                item++;
            }
            char nm[40];
            snprintf(nm, sizeof nm, "/tmp/armoury_e_%d.ppm", page++);
            dump_ppm(nm);
        }
        printf("[armoury] done\n");
        return 0;
    }

    if (getenv("ELITE_ICONSHOT")) {
        /* full armoury grid: every weapon at (col*21+2, row*12+2),
         * 6 per row, enum order — the guide compositor crops these */
        memset(g_fb, 0, sizeof g_fb);
        for (int i = 0; i < WPN_COUNT; i++)
            icon_weapon(g_fb, 2 + (i % 6) * 21, 2 + (i / 6) * 12, i);
        dump_ppm("/tmp/icons_weapons.ppm");
        memset(g_fb, 0, sizeof g_fb);
        for (int i = WPN_COUNT; i < ITEM_COUNT; i++) {
            int j = i - WPN_COUNT;
            icon_weapon(g_fb, 2 + (j % 6) * 21, 2 + (j / 6) * 12, i);
        }
        dump_ppm("/tmp/icons_equip.ppm");
        return 0;
    }

    /* Launcher-icon render: a hero ship, 3/4 view, lit on a starfield.
     * Run under the hires build for a crisp 256x256 source. */
    if (getenv("ELITE_APPICON")) {
        int cls = getenv("ELITE_ICONCLS") ? atoi(getenv("ELITE_ICONCLS")) : 3;
        extern uint32_t g_force_xfoil_seed;
        uint32_t hseed = 0x5A17u;
        g_force_xfoil_seed = hseed;                 /* iconic X-wing hull */
        r3d_scene_set_icon_bg(0xF81Fu);             /* magenta key, no sky */
        Mat3 cam = m3_identity();
        r3d_scene_begin(&cam, 50.0f);
        r3d_pipe_set_sun(v3_norm(v3(0.45f, 0.55f, -0.70f)));
        R3DObject obj;
        obj.mesh  = hull_mesh(hseed, cls);          /* a real game hull */
        obj.basis = m3_identity();
        /* Nose toward the camera, banked, tilted to splay the X foils —
         * a clear, recognisable front 3/4 (user: face it at the camera). */
        m3_rotate_local(&obj.basis, 1, 3.20f);      /* yaw: nose at viewer, slight 3/4 */
        m3_rotate_local(&obj.basis, 0, 0.48f);      /* pitch: nose down, see the top */
        m3_rotate_local(&obj.basis, 2, -0.07f);     /* slight bank */
        /* Frame by the ROTATED on-screen silhouette so the splayed hull
         * fills the tile (bound_r left sparse foils tiny — same fix as the
         * ship grids). */
        float ext = 0.001f;
        for (int i = 0; i < obj.mesh->nverts; i++) {
            Vec3 v = v3((float)obj.mesh->verts[i].x, (float)obj.mesh->verts[i].y,
                        (float)obj.mesh->verts[i].z);
            v = v3_scale(v, obj.mesh->scale / 127.0f);
            Vec3 r = m3_mul_v3(&obj.basis, v);
            float lat = sqrtf(r.x * r.x + r.y * r.y);
            if (lat > ext) ext = lat;
        }
        obj.pos   = v3(0.0f, 0.0f, ext * 2.5f);
        r3d_scene_add_object(&obj);
        r3d_scene_raster(g_fb, 0, ELITE_FB_H);
        g_force_xfoil_seed = 0;
        dump_ppm("/tmp/elite_icon.ppm");
        return 0;
    }

    /* Icon background: a real game world (lit, low) + the distant star on dark
     * space with a faint nebula — composited under the hull by make_icon.py. */
    if (getenv("ELITE_ICONBG")) {
        const SystemInfo *si = system_info();
        Mat3 cam = m3_identity();
        Vec3 vista = v3(0, 0, 0);
        if (si->n_planets > 0) {
            Vec3 P = system_planet_pos_mm(0);
            float pr = si->planets[0].radius_mm;
            Vec3 up0 = v3(0, 1, 0);
            Vec3 toStar = v3_norm(v3_scale(P, -1.0f));     /* lit side */
            Vec3 side = v3_norm(v3_cross(up0, toStar));
            /* Off to the lit side so we see a crescent AND the star beyond. */
            vista = v3_add(P, v3_add(v3_scale(toStar, pr * 2.3f),
                                     v3_scale(side, pr * 2.4f)));
            Vec3 fwd = v3_norm(v3_sub(P, vista));
            cam.r[2] = fwd;
            cam.r[0] = v3_norm(v3_cross(up0, fwd));
            cam.r[1] = v3_cross(fwd, cam.r[0]);
            m3_rotate_local(&cam, 0, -0.55f);              /* world sits low */
            m3_rotate_local(&cam, 1, 0.30f);               /* star into frame */
        }
        r3d_scene_set_nebula((uint32_t)si->seed | 1u, 0.34f);  /* accent, not wash */
        r3d_scene_begin(&cam, 55.0f);
        r3d_pipe_set_sun(v3_norm(v3(0.4f, 0.5f, -0.76f)));
        r3d_planet_emit(vista);
        r3d_scene_raster(g_fb, 0, ELITE_FB_H);
        r3d_scene_set_nebula(0, 0.0f);
        dump_ppm("/tmp/elite_iconbg.ppm");
        return 0;
    }

    /* Hull-roll variety + determinism. */
    if (getenv("ELITE_JITTERTEST")) {
        HullRoll a, b, c;
        hull_roll(3, 0x1111, &a);
        hull_roll(3, 0x2222, &b);
        hull_roll(3, 0x1111, &c);
        printf("[jit] VIPER r1: spd=%.2f hull=%.2f slots=%d utils=%d\n",
               a.spd, a.hull, a.n_slots, a.utils);
        printf("[jit] VIPER r2: spd=%.2f hull=%.2f slots=%d utils=%d\n",
               b.spd, b.hull, b.n_slots, b.utils);
        printf("[jit] determinism: %s; differs: %s\n",
               (a.spd == c.spd && a.utils == c.utils) ? "STABLE" : "BUG",
               (a.spd != b.spd || a.utils != b.utils) ? "YES" : "no");
        int ucount[5] = { 0 };
        for (int i = 0; i < 200; i++) {
            HullRoll r;
            hull_roll(4, 0xABC0 + i * 7, &r);
            if (r.utils <= 4) ucount[r.utils]++;
        }
        printf("[jit] REAVER utils dist (200): 1=%d 2=%d 3=%d 4=%d\n",
               ucount[1], ucount[2], ucount[3], ucount[4]);
        return 0;
    }

    /* Pirate generation census: real ship_set_tier loadouts. */
    if (getenv("ELITE_PIRGEN")) {
        extern const Mesh *hull_mesh(uint32_t, int);
        static const uint8_t k_tier_class[5] = { 1, 2, 3, 4, 5 };
        static const char *hn[10] = { "SKIFF","DART","SPARROW","VIPER",
            "REAVER","MAULER","PACKMULE","MULE","ATLAS","BASILISK" };
        static const char *sv[4] = { "STD","REGEN","BULWARK","PHASE" };
        for (int tier = 0; tier <= 4; tier++) {
            for (int idx = 1; idx <= 6; idx++) {
                int cls = k_tier_class[tier];
                int e = ship_spawn(hull_mesh(0xACE1u ^ idx, cls),
                                   v3(0, 0, 500), TEAM_HOSTILE);
                if (e <= 0) continue;
                /* force the entity index parity the spawner would get */
                ship_set_tier(e, tier, cls);
                Ship *p2 = &g_ships[e];
                char guns[40] = "";
                for (int w = 0; w < p2->n_weapons; w++) {
                    strcat(guns, k_weapons[p2->weapons[w]].name);
                    strcat(guns, " ");
                }
                printf("[pirgen] T%d idx%%6=%d %-8s hull=%3.0f shd=%3.0f "
                       "%-7s turret=%d guns= %s\n",
                       tier, e % 6, hn[cls], p2->hull_max,
                       p2->shield_max, sv[p2->shield_var],
                       p2->turret_type, guns);
                p2->alive = false;
            }
        }
        return 0;
    }

    /* Repair drone: hull rises, critted mount returns to service. */
    if (getenv("ELITE_DRONETEST")) {
        g_player.util_eq[0] = (WeaponInst){ .type = EQ_DRONE,
            .quality = 1, .integrity = 100, .in_use = 1 };
        Ship *pl = &g_ships[0];
        pl->hull = pl->hull_max * 0.5f;
        g_player.mounts[0].integrity = 0;     /* critted: OFFLINE */
        pl->active_w = 0;
        pl->fire_cool = 0;
        pl->heat = 0;
        combat_set_shot_type(WPN_PULSE_S);
        combat_fire(0, 0, -1);
        /* used-market drone must NOT self-repair (user report) */
        g_player.util_eq[0].integrity = 80;
        {
            CraftRawButtons n2 = {0};
            for (int f = 0; f < 90; f++) elite_game_tick(&n2, 1.0f/30.0f);
            extern const char *elite_game_debug_toast(void);
            printf("[drone] self-repair check: integ=%d toast='%s' (%s)\n",
                   g_player.util_eq[0].integrity,
                   elite_game_debug_toast(),
                   g_player.util_eq[0].integrity == 80 ? "LEAVES ITSELF"
                                                       : "self-licking");
            /* damaged chaff in bay 2: must announce BY NAME */
            g_ships[0].hull = g_ships[0].hull_max;   /* hull job done */
            for (int m = 0; m < HULL_SLOTS; m++)
                if (g_player.mounts[m].in_use)
                    g_player.mounts[m].integrity = 100;
            g_player.shield_eq.integrity = 100;
            g_player.armor_eq.integrity = 100;
            g_player.util_eq[1] = (WeaponInst){ .type = EQ_CHAFF,
                .quality = 1, .integrity = 60, .in_use = 1 };
            for (int f = 0; f < 90; f++) elite_game_tick(&n2, 1.0f/30.0f);
            printf("[drone] named toast: '%s'\n",
                   elite_game_debug_toast());
            g_player.util_eq[1].in_use = 0;
        }
        printf("[drone] offline mount: heat after fire = %.1f "
               "(want 0 = refused)\n", pl->heat);
        float h0 = pl->hull;
        CraftRawButtons none = {0};
        for (int f = 0; f < 30 * 60; f++)      /* one minute of repairs */
            elite_game_tick(&none, 1.0f / 30.0f);
        printf("[drone] hull %.0f -> %.0f / %.0f\n", h0, pl->hull,
               pl->hull_max);
        for (int f = 0; f < 30 * 240 &&
                        g_player.mounts[0].integrity < 100; f++)
            elite_game_tick(&none, 1.0f / 30.0f);
        pl->fire_cool = 0;
        pl->heat = 0;
        combat_fire(0, 0, -1);
        printf("[drone] repaired mount: integrity=%d heat after "
               "fire=%.1f (%s)\n", g_player.mounts[0].integrity,
               pl->heat, pl->heat > 0 ? "FIRES AGAIN" : "still dead");
        return 0;
    }

    /* Cross-campaign insurance guard: a NEW game dying pre-dock must
     * respawn fresh, never load the previous campaign's save. */
    if (getenv("ELITE_NEWDEATH")) {
        /* fabricate an 'old campaign' save in another galaxy */
        uint32_t cur = galaxy_get_seed();
        galaxy_set_seed(cur ^ 0xDEADBEEF);
        g_player.credits = 16000;
        save_write((SysAddr){ 0, 0, 0 }, 0, 0);
        galaxy_set_seed(cur);
        g_player.credits = 1000;
        printf("[newdeath] old-save galaxy differs, credits now %d\n",
               g_player.credits);
        /* die */
        g_ships[0].hull = 0;
        g_ships[0].alive = false;
        CraftRawButtons none = {0};
        for (int f = 0; f < 30 * 2; f++)
            elite_game_tick(&none, 1.0f / 30.0f);
        {   /* kill screen waits for A now */
            CraftRawButtons a = none; a.a = true;
            for (int f = 0; f < 30; f++)
                elite_game_tick(&a, 1.0f / 30.0f);
        }
        printf("[newdeath] after respawn: credits=%d galaxy=%s "
               "state=%d (%s)\n", g_player.credits,
               galaxy_get_seed() == cur ? "SAME" : "OLD CAMPAIGN!",
               elite_game_state(),
               (g_player.credits == 1000 && galaxy_get_seed() == cur)
                   ? "FRESH RESPAWN" : "BODY-SNATCHED");
        return 0;
    }

    /* Distress event end-to-end. */
    if (getenv("ELITE_DISTRESS")) {
        /* find a distress POI per intel, fly the anchor there directly */
        Poi pois[MAX_POIS];
        int np = system_pois(pois, MAX_POIS);
        int target = -1;
        PoiIntel di;
        for (int i = 0; i < np; i++) {
            elite_game_poi_intel(&pois[i], &di);
            if (di.distress) { target = i; break; }
        }
        printf("[distress] poi=%d\n", target);
        if (target < 0) return 0;
        elite_game_debug_goto_poi(target);
        int civ = -1, npir = 0;
        for (int i = 1; i < MAX_SHIPS; i++) {
            if (!g_ships[i].alive) continue;
            /* the distress victim FIGHTS (ai_target set); traffic
             * civilians idle — pick the right one */
            if (g_ships[i].is_civilian && g_ships[i].ai_target > 0 &&
                civ < 0) civ = i;
            else if (g_ships[i].team == TEAM_HOSTILE &&
                     g_ships[i].ai_target > 0) npir++;
        }
        printf("[distress] civ=%d pirates_on_civ=%d\n", civ, npir);
        if (civ < 0 || npir == 0) return 1;
        /* watch the NPC fight for 5s: civ should take damage */
        CraftRawButtons none = {0};
        float h0 = g_ships[civ].hull;
        for (int f = 0; f < 150; f++) elite_game_tick(&none, 1.0f/30.0f);
        printf("[distress] civ hull %.0f -> %.0f (fight=%s)\n", h0,
               g_ships[civ].alive ? g_ships[civ].hull : -1.0f,
               (!g_ships[civ].alive || g_ships[civ].hull < h0) ? "YES"
                                                               : "no");
        /* GRAZE the victim once (easy with flak mid-furball) — does
         * the rescue still pay? */
        if (getenv("ELITE_GRAZE")) {
            combat_set_shot_type(WPN_PULSE_S);
            combat_direct_damage(0, civ, 4.0f, g_ships[civ].pos);
            printf("[distress] grazed victim: team=%d legal=%d\n",
                   g_ships[civ].team, g_player.legal);
        }
        /* player engages: wing must switch to us */
        for (int i = 1; i < MAX_SHIPS; i++)
            if (g_ships[i].alive && g_ships[i].team == TEAM_HOSTILE) {
                combat_set_shot_type(WPN_PULSE_S);
                combat_direct_damage(0, i, 5.0f, g_ships[i].pos);
                break;
            }
        int on_player = 0, alive_pir = 0;
        for (int i = 1; i < MAX_SHIPS; i++)
            if (g_ships[i].alive && g_ships[i].team == TEAM_HOSTILE) {
                alive_pir++;
                if (g_ships[i].ai_target == 0) on_player++;
            }
        printf("[distress] engaged: %d/%d on player\n", on_player,
               alive_pir);
        /* kill the wing; rescue should pay */
        int cr0 = g_player.credits;
        for (int i = 1; i < MAX_SHIPS; i++)
            if (g_ships[i].alive && g_ships[i].team == TEAM_HOSTILE)
                { combat_set_shot_type(WPN_PULSE_S);
                  combat_direct_damage(0, i, 99999.0f, g_ships[i].pos); }
        for (int f = 0; f < 5; f++) elite_game_tick(&none, 1.0f/30.0f);
        printf("[distress] rescue: civ_alive=%d credits +%d "
               "hostiles=%d state=%d legal=%d\n",
               g_ships[civ].alive, g_player.credits - cr0,
               ships_alive_hostile(), elite_game_state(),
               g_player.legal);
        /* post-rescue: LB must lock the surviving civilian */
        {
            CraftRawButtons lb2 = {0};
            lb2.lb = true;
            elite_game_tick(&lb2, 1.0f / 30.0f);
            CraftRawButtons n4 = {0};
            for (int f = 0; f < 8; f++)
                elite_game_tick(&n4, 1.0f / 30.0f);
            extern int elite_game_debug_target(void);
            render_frame();
            dump_ppm("/tmp/civ_lock.ppm");
            printf("[post] LB lock -> %d (civ=%d) %s\n",
                   elite_game_debug_target(), civ,
                   elite_game_debug_target() == civ ? "LOCKED"
                                                    : "MISS");
        }
        /* post-rescue: watch the survivor for pathological motion */
        for (int f = 0; f < 300; f++) {
            elite_game_tick(&none, 1.0f / 30.0f);
            if (f % 60 == 0 && g_ships[civ].alive) {
                Ship *cv = &g_ships[civ];
                Ship *pl = &g_ships[0];
                Vec3 local = m3_mul_v3_t(&pl->basis,
                                         v3_sub(cv->pos, pl->pos));
                printf("[post] t=%2.0fs dist=%6.0f local.y=%7.1f "
                       "vel=%5.1f pos=(%.0f %.0f %.0f)\n",
                       f / 30.0f, v3_len(local), local.y,
                       v3_len(cv->vel), cv->pos.x, cv->pos.y,
                       cv->pos.z);
            }
        }
        return 0;
    }

    /* Double-tap LB test: tap-tap, expect TGT: SALVAGE toast. */
    if (getenv("ELITE_TAPTEST")) {
        CraftRawButtons none = {0}, lb = {0};
        lb.lb = true;
        const char *toast;
        extern const char *elite_game_debug_toast(void);
        /* tap 1: 2 frames down, 2 up; tap 2 same — releases ~130ms apart */
        elite_game_tick(&lb, 1.0f / 30.0f);
        elite_game_tick(&lb, 1.0f / 30.0f);
        elite_game_tick(&none, 1.0f / 30.0f);
        elite_game_tick(&none, 1.0f / 30.0f);
        elite_game_tick(&lb, 1.0f / 30.0f);
        elite_game_tick(&lb, 1.0f / 30.0f);
        elite_game_tick(&none, 1.0f / 30.0f);
        toast = elite_game_debug_toast();
        printf("[tap] quick double: '%s'\n", toast);
        /* lazy double: releases ~400ms apart (12 frames) */
        for (int f = 0; f < 40; f++) elite_game_tick(&none, 1.0f / 30.0f);
        elite_game_tick(&lb, 1.0f / 30.0f);
        elite_game_tick(&lb, 1.0f / 30.0f);
        for (int f = 0; f < 10; f++) elite_game_tick(&none, 1.0f / 30.0f);
        elite_game_tick(&lb, 1.0f / 30.0f);
        elite_game_tick(&lb, 1.0f / 30.0f);
        elite_game_tick(&none, 1.0f / 30.0f);
        printf("[tap] lazy double: '%s'\n", elite_game_debug_toast());
        /* singles far apart must NOT switch class */
        for (int f = 0; f < 40; f++) elite_game_tick(&none, 1.0f / 30.0f);
        elite_game_tick(&lb, 1.0f / 30.0f);
        elite_game_tick(&none, 1.0f / 30.0f);
        printf("[tap] single after: '%s' (should be unchanged)\n",
               elite_game_debug_toast());
        return 0;
    }

    /* Siege sim: stationary player vs one attacker of tier N — does
     * the shield ever collapse, and how fast? (user: regen out-raced
     * pass damage). ELITE_SIEGE=<tier> */
    if (getenv("ELITE_SIEGE")) {
        int tier = atoi(getenv("ELITE_SIEGE"));
        for (int i = 1; i < MAX_SHIPS; i++) g_ships[i].alive = false;
        Ship *pl = &g_ships[0];
        pl->vel = v3(0, 0, 0);
        pl->throttle = 0;
        extern const Mesh *hull_mesh(uint32_t, int);
        int e = ship_spawn(hull_mesh(0xACE1u, 2 + tier),
                           v3_add(pl->pos, v3(80, 20, 320)),
                           TEAM_HOSTILE);
        if (e > 0) ship_set_tier(e, tier, 2 + tier);
        if (e > 0 && getenv("ELITE_SIEGE_NOTURRET"))
            g_ships[e].turret_type = 0;
        CraftRawButtons none = {0};
        float t_collapse = -1;
        for (int f = 0; f < 60 * 30; f++) {
            elite_game_tick(&none, 1.0f / 30.0f);
            if (t_collapse < 0 && pl->shield <= 0.5f)
                t_collapse = (float)f / 30.0f;
            if (f % 150 == 149)
                printf("[siege] t=%2ds shield=%5.1f hull=%5.1f\n",
                       f / 30, pl->shield, pl->hull);
            if (!pl->alive) break;
        }
        printf("[siege] tier=%d collapse=%.1fs dead=%d\n", tier,
               t_collapse, !pl->alive);
        return 0;
    }

    /* Intel-vs-arrival consistency check (option-C contract). */
    if (getenv("ELITE_INTELTEST")) {
        Poi pois[MAX_POIS];
        int np = system_pois(pois, MAX_POIS);
        PoiIntel in3;
        elite_game_poi_intel(&pois[0], &in3);     /* beacon = anchor */
        Vec3 rk[8];
        int nr = rocks_positions(rk, 8);
        if (getenv("ELITE_INTELSHOT")) {
            for (int i = 1; i < MAX_SHIPS; i++)
                g_ships[i].alive = false;     /* clear sky: rocks only */
            CraftRawButtons b2 = {0}, n2 = {0};
            b2.lb = true;
            elite_game_tick(&b2, 1.0f / 30.0f);
            for (int f = 0; f < 8; f++) elite_game_tick(&n2, 1.0f / 30.0f);
            /* face the belt and fly halfway in for the beauty shot */
            Vec3 rk2[8];
            int nr2 = rocks_positions(rk2, 8);
            if (nr2 > 0) {
                Vec3 c2 = v3(0, 0, 0);
                for (int i = 0; i < nr2; i++) c2 = v3_add(c2, rk2[i]);
                c2 = v3_scale(c2, 1.0f / (float)nr2);
                Ship *pl2 = &g_ships[0];
                Vec3 fwd2 = v3_norm(v3_sub(c2, pl2->pos));
                Vec3 up3 = (fwd2.y > -0.9f && fwd2.y < 0.9f)
                               ? v3(0, 1, 0) : v3(1, 0, 0);
                Vec3 rt2 = v3_norm(v3_cross(up3, fwd2));
                pl2->basis.r[0] = rt2;
                pl2->basis.r[1] = v3_cross(fwd2, rt2);
                pl2->basis.r[2] = fwd2;
                pl2->pos = v3_sub(c2, v3_scale(fwd2, 320.0f));
                for (int f = 0; f < 4; f++)
                    elite_game_tick(&n2, 1.0f / 30.0f);
            }
            render_frame();
            dump_ppm("/tmp/rock_lock.ppm");
            /* autocannon the nearest boulder: shells must chip it */
            ship_fit_weapon(0, 0, WPN_AUTOCANNON);
            g_ships[0].active_w = 0;
            {   /* face the NEAREST ROCK dead-on (not the centroid) */
                Vec3 rk3[8];
                int nr3 = rocks_positions(rk3, 8);
                if (nr3 > 0) {
                    Ship *pl3 = &g_ships[0];
                    int bi3 = 0; float bd3 = 1e30f;
                    for (int i = 0; i < nr3; i++) {
                        float d4 = v3_len(v3_sub(rk3[i], pl3->pos));
                        if (d4 < bd3) { bd3 = d4; bi3 = i; }
                    }
                    Vec3 f4 = v3_norm(v3_sub(rk3[bi3], pl3->pos));
                    Vec3 u4 = (f4.y > -0.9f && f4.y < 0.9f)
                                  ? v3(0, 1, 0) : v3(1, 0, 0);
                    Vec3 r4 = v3_norm(v3_cross(u4, f4));
                    pl3->basis.r[0] = r4;
                    pl3->basis.r[1] = v3_cross(f4, r4);
                    pl3->basis.r[2] = f4;
                }
            }
            extern int loot_positions(Vec3 *, int *, int);
            int ore_before;
            { Vec3 c3[6]; int k3[6];
              ore_before = loot_positions(c3, k3, 6); }
            CraftRawButtons fb2 = {0};
            fb2.a = true;
            for (int f = 0; f < 90; f++) {
                g_ships[0].fire_cool = 0;
                elite_game_tick(&fb2, 1.0f / 30.0f);
            }
            int ore_after;
            { Vec3 c3[6]; int k3[6];
              ore_after = loot_positions(c3, k3, 6); }
            printf("[minetest] ore canisters %d -> %d (%s)\n",
                   ore_before, ore_after,
                   ore_after > ore_before ? "AUTOCANNON CHIPS"
                                          : "NO EFFECT");
        }
        {
            int nciv = 0, npol = 0;
            for (int i = 1; i < MAX_SHIPS; i++) {
                if (!g_ships[i].alive) continue;
                if (g_ships[i].is_civilian) nciv++;
                if (g_ships[i].is_police) npol++;
            }
            printf("[intel] civilians=%d police=%d\n", nciv, npol);
            /* crime check: shoot a civilian, verify legal + flip */
            if (getenv("ELITE_CRIMETEST")) {
                for (int i = 1; i < MAX_SHIPS; i++)
                    if (g_ships[i].alive && g_ships[i].is_civilian) {
                        combat_set_shot_type(WPN_PULSE_S);
                        combat_direct_damage(0, i, 10.0f, g_ships[i].pos);
                        printf("[crime] after hit: legal=%d fine=%d "
                               "civ_team=%d civ_target=%d\n",
                               g_player.legal, g_player.fine,
                               g_ships[i].team, g_ships[i].ai_target);
                        combat_direct_damage(0, i, 9999.0f,
                                             g_ships[i].pos);
                        printf("[crime] after kill: legal=%d fine=%d "
                               "alive=%d\n", g_player.legal,
                               g_player.fine, g_ships[i].alive);
                        break;
                    }
            }
        }
        printf("[intel] beacon belt=%d rocks_present=%d %s\n",
               in3.belt, nr,
               (in3.belt == (nr > 0)) ? "MATCH" : "MISMATCH");
        for (int i = 0; i < np; i++) {
            elite_game_poi_intel(&pois[i], &in3);
            printf("[intel] %-14s belt=%d pol=%d pir=%d salv=%d\n",
                   pois[i].name, in3.belt, in3.police, in3.pirate_pct,
                   in3.debris_pct);
        }
        return 0;
    }

    /* Full gameplay movie: title -> dogfight -> loot -> supercruise ->
     * dock -> trade/outfit/missions -> hyperjump. Dumps every frame to
     * /tmp/movie/ for ffmpeg. ELITE_MOVIE=1. */
    if (getenv("ELITE_MOVIE")) {
        static int mf = 0;
        CraftRawButtons none = {0};
        /* Capture the game's audio synced to frames: 22050/30 = 735
         * samples per frame, streamed to a WAV the compositor muxes. */
        FILE *gwav = fopen("/tmp/guide_audio.raw", "wb");
        #define MV(btns) do { \
            CraftRawButtons _mv_b = (btns); \
            elite_game_tick(&_mv_b, 1.0f / 30.0f); \
            if (gwav) { int16_t _ab[735]; audio_render(_ab, 735); \
                        fwrite(_ab, 2, 735, gwav); } \
            render_frame(); \
            char _p[64]; \
            snprintf(_p, sizeof _p, "/tmp/movie/f_%05d.ppm", mf++); \
            dump_ppm(_p); \
        } while (0)
        #define MV_IDLE(n) do { for (int _i = 0; _i < (n); _i++) MV(none); } while (0)
        #define MV_TAP(field, settle) do { \
            CraftRawButtons _t = none; _t.field = true; MV(_t); \
            MV_IDLE(settle); } while (0)
        /* Caption marker: the compositor renders these in a bar BELOW
         * the game frame (never obscuring the gameplay). */
        #define CAP(txt) printf("[cap] %d %s\n", mf, txt)

        CAP("INDEMNITY RUN  --  an infinite galaxy in your pocket");
        /* Phase 0: the title drifts, NEW GAME, then SKIP the intro crawl
         * so the captions land on real gameplay, not the story text. */
        MV_IDLE(140);
        MV_TAP(down, 4);
        MV_TAP(a, 6);                            /* NEW GAME -> intro crawl */
        MV_TAP(a, 6);                            /* skip the crawl -> flight */
        MV_IDLE(20);                             /* settle into the void */

        CAP("Every commander starts with a ship, 1000cr, and the void");
        MV_IDLE(55);                             /* the starter hull in open space */
        /* Phase 1: a proper hero ship — MAULER with PULSE-M, GAUSS and
         * HOMING, war chest for the shopping act. */
        Ship *pl = &g_ships[0];
        g_player.hull_id = 5;                    /* MAULER stats (3 slots) */
        g_player.hull_seed = 0x5EED77u;
        /* Force the hero hull to an X-wing silhouette (only this seed is
         * matched, so enemies keep their own shapes). Stays set for the
         * whole shoot incl. the death-beat re-mesh. */
        extern uint32_t g_force_xfoil_seed;
        g_force_xfoil_seed = 0x5EED77u;
        g_player.credits = 30000;
        g_player.mounts[0] = (WeaponInst){ .type = WPN_PULSE_S,
            .quality = Q_STANDARD, .integrity = 100, .in_use = 1 };
        g_player.mounts[1] = (WeaponInst){ .type = WPN_GAUSS,
            .quality = Q_STANDARD, .integrity = 100, .in_use = 1 };
        g_player.mounts[2] = (WeaponInst){ .type = WPN_HOMING,
            .quality = Q_STANDARD, .integrity = 100, .in_use = 1 };
        for (int i = 0; i < HULL_SLOTS; i++) g_player.ammo[i] = -1;
        /* Guide props: a CULL mission that completes during the fight,
         * and CLOAK + MANIFEST gadgets for the utility demo. */
        extern Mission g_missions[];
        g_missions[0] = (Mission){ 0 };
        g_missions[0].type = MIS_CULL;
        g_missions[0].count = 2;
        g_missions[0].reward = 1800;
        g_missions[0].faction = 0;
        g_missions[0].done = false;
        snprintf(g_missions[0].label, sizeof g_missions[0].label,
                 "CULL 2 PIRATES");
        g_player.util_eq[0] = (WeaponInst){ .type = EQ_CLOAK,
            .quality = 1, .integrity = 100, .in_use = 1 };
        g_player.util_eq[1] = (WeaponInst){ .type = EQ_MANIFEST,
            .quality = 1, .integrity = 100, .in_use = 1 };
        player_apply_to_ship();
        pl->hull = pl->hull_max;
        pl->shield = pl->shield_max;
        elite_game_debug_face_away_from_sun();
        extern const Mesh *hull_mesh(uint32_t, int);
        Vec3 fwd = pl->basis.r[2], rgt = pl->basis.r[0];
        int p1 = ship_spawn(hull_mesh(0xACE1u, 2),
                            v3_add(pl->pos, v3_add(v3_scale(fwd, 220.0f),
                                                   v3_scale(rgt, 40.0f))),
                            TEAM_HOSTILE);
        if (p1 > 0) ship_set_tier(p1, 0, 2);
        int p2 = ship_spawn(hull_mesh(0xBEE5u, 1),
                            v3_add(pl->pos, v3_add(v3_scale(fwd, 300.0f),
                                                   v3_scale(rgt, -70.0f))),
                            TEAM_HOSTILE);
        if (p2 > 0) ship_set_tier(p2, 0, 1);
        /* tankier so the weapon showcase lingers (slower combat) */
        if (p1 > 0) g_ships[p1].hull = g_ships[p1].hull_max =
                        g_ships[p1].hull_max * 2.4f;
        if (p2 > 0) g_ships[p2].hull = g_ships[p2].hull_max =
                        g_ships[p2].hull_max * 2.4f;
        pl->hull = pl->hull_max;                /* fresh paint */
        CAP("Two pirates inbound -- read the radar, close the gap");
        pl->throttle = 0.25f;
        MV_IDLE(75);                            /* establishing approach */
        CAP("Lock the nearest threat -- the radar tracks every contact");
        MV_TAP(lb, 2);                          /* lock */
        MV_IDLE(35);                            /* hold on the lock box */

        /* Phase 2: the weapons showcase. The button-flip autopilot
         * fought the 1%-floor turn ramp (every direction change reset
         * it) and tracked nothing — the camera rig now steers the
         * basis DIRECTLY: capped smooth rotation toward the lead
         * point, fire through the real input when on target. */
        #define MV_STEER(aimpt, rate) do { \
            Vec3 _want = v3_norm(v3_sub((aimpt), pl->pos)); \
            Vec3 _cur = pl->basis.r[2]; \
            Vec3 _ax = v3_cross(_cur, _want); \
            float _sa = v3_len(_ax); \
            if (_sa > 1e-5f) { \
                float _ang = asinf(_sa > 1.0f ? 1.0f : _sa); \
                float _step = _ang < (rate) ? _ang : (rate); \
                m3_rotate_world(&pl->basis, v3_scale(_ax, 1.0f / _sa), \
                                _step); \
                m3_orthonormalize(&pl->basis); \
            } \
        } while (0)
        #define MV_AIM(tgt, wspd, dead, fire_cone, press_a) do { \
            Ship *_t = &g_ships[tgt]; \
            float _d = v3_len(v3_sub(_t->pos, pl->pos)); \
            float _tt = (wspd) > 0 ? _d / (wspd) : 0; \
            Vec3 _aim = v3_add(_t->pos, \
                               v3_scale(v3_sub(_t->vel, pl->vel), _tt)); \
            MV_STEER(_aim, 0.075f); \
            Vec3 _l = m3_mul_v3_t(&pl->basis, v3_sub(_aim, pl->pos)); \
            CraftRawButtons _b = none; \
            (void)(dead); \
            if ((press_a) && _l.z > 0 && \
                _l.x * _l.x + _l.y * _l.y < _d * _d * (fire_cone)) \
                _b.a = true; \
            MV(_b); \
        } while (0)

        CAP("HOMING missiles: fire and forget -- they chase the lock");
        /* 2a: open with a homing volley — lock, loose two, watch. */
        pl->active_w = 2;                        /* HOMING */
        for (int f = 0; f < 120 && p1 > 0 && g_ships[p1].alive; f++) {
            int fire = (f == 25 || f == 70);
            if (fire) pl->fire_cool = 0;
            MV_AIM(p1, 190.0f, 6.0f, 0.05f, fire);
        }
        MV_IDLE(45);                             /* missiles fly */
        CAP("Pulse lasers run on heat, not ammo -- no magazine to empty");
        /* 2b: switch to the medium laser, press the attack. */
        MV_TAP(b, 2);                            /* HOMING -> PULSE-M */
        pl->shield = pl->shield_max;             /* continuity polish */
        for (int f = 0; f < 600 && p1 > 0 && g_ships[p1].alive; f++) {
            MV_AIM(p1, 0.0f, 3.0f, 0.010f, 1);
            if (f % 120 == 60) {
                Vec3 l2 = m3_mul_v3_t(&pl->basis,
                                      v3_sub(g_ships[p1].pos, pl->pos));
                printf("[2b] f=%d st=%d w=%d cool=%.2f heat=%.0f "
                       "l=(%.1f %.1f %.1f) canfire=%d nw=%d aw=%d\n",
                       f, elite_game_state(),
                       pl->weapons[pl->active_w], pl->fire_cool,
                       pl->heat, l2.x, l2.y, l2.z,
                       combat_can_fire(pl), pl->n_weapons, pl->active_w);
            }
        }
        MV_IDLE(40);                             /* fireball */
        printf("[movie] p1 alive=%d hull=%.0f shield=%.0f myheat=%.0f "
               "frame=%d\n",
               p1 > 0 ? g_ships[p1].alive : -1,
               p1 > 0 ? g_ships[p1].hull : 0,
               p1 > 0 ? g_ships[p1].shield : 0, pl->heat, mf);
        CAP("GAUSS: a charged rail slug -- devastating, ammo-limited");
        /* 2c: the gauss finish on raider two — helix + kill. */
        MV_TAP(lb, 2);                           /* lock next */
        MV_TAP(b, 2);                            /* PULSE-M -> GAUSS */
        pl->shield = pl->shield_max;
        for (int f = 0; f < 700 && p2 > 0 && g_ships[p2].alive; f++)
            MV_AIM(p2, 1400.0f, 3.0f, 0.006f, 1);
        MV_IDLE(55);
        printf("[movie] p2 alive=%d frame=%d\n",
               p2 > 0 ? g_ships[p2].alive : -1, mf);

        CAP("MISSION COMPLETE -- two pirates culled. Collect at any dock");
        MV_IDLE(45);
        CAP("Wrecks drop loot -- fly through a canister to scoop it");
        /* Phase 3: salvage run — lock loot, fly to ONE can, scoop, move
         * on. (Was a two-can chase that could run a full minute when the
         * second can drifted away — user. Now: first scoop ends it, with
         * a hard ~7s cap so a fleeing can never drags the beat.) */
        extern int loot_positions(Vec3 *, int *, int);
        loot_on_kill(v3_add(pl->pos, v3_scale(pl->basis.r[2], 90.0f)),
                     v3(0, 0, 0), 2, NULL);           /* guaranteed wreckage */
        int loot_start = 0;
        { Vec3 c0[6]; int q0[6]; loot_start = loot_positions(c0, q0, 6); }
        MV_TAP(lb, 2);                          /* salvage lock */
        for (int f = 0; f < 210; f++) {
            Vec3 cans[6]; int comp[6];
            int n = loot_positions(cans, comp, 6);
            if (n == 0 || n < loot_start) break;   /* first can scooped -> done */
            int bi = 0; float bd = 1e30f;
            for (int i = 0; i < n; i++) {
                float d3 = v3_len(v3_sub(cans[i], pl->pos));
                if (d3 < bd) { bd = d3; bi = i; }
            }
            MV_STEER(cans[bi], 0.07f);
            pl->throttle = (bd > 120.0f) ? 0.9f
                         : (bd > 40.0f) ? 0.45f : 0.22f;
            MV(none);
        }
        pl->throttle = 0.2f;
        MV_IDLE(25);

        /* Phase 3b: MINING loop — crack a rock, scoop the ore. */
        CAP("Mining: a mining laser cracks asteroids open");
        {
            extern void rocks_spawn_field(uint32_t, int);
            extern int rocks_positions(Vec3 *, int);
            /* fit a mining laser to mount 0 for the demo, select it */
            g_player.mounts[0] = (WeaponInst){ .type = WPN_MINING,
                .quality = Q_STANDARD, .integrity = 100, .in_use = 1 };
            g_player.ammo[0] = -1;
            player_apply_to_ship();
            pl->active_w = 0;
            extern int loot_positions(Vec3 *, int *, int);
            extern int rocks_get(int, Vec3 *, float *);
            rocks_spawn_field(0xB17B17u, 6);
            /* drop the player AT the belt (no long drive): put the
             * biggest rock ~120m dead ahead along the current nose */
            {
                Vec3 rk[8]; int nr = rocks_positions(rk, 8);
                int big = 0; float bigr = -1.0f;
                for (int i = 0; i < nr; i++) {
                    Vec3 rp; float rr;
                    if (rocks_get(i, &rp, &rr) && rr > bigr) {
                        bigr = rr; big = i;
                    }
                }
                if (nr > 0)
                    pl->pos = v3_sub(rk[big],
                                     v3_scale(pl->basis.r[2], 120.0f));
            }
            pl->vel = v3(0, 0, 0);
            MV_IDLE(20);
            CAP("Close on a rock and hold fire -- the beam chews it open");
            /* approach the nearest boulder, then SUSTAIN fire on it */
            int target_rock = -1;
            for (int f = 0; f < 30 * 11; f++) {
                Vec3 rk[8]; int nr = rocks_positions(rk, 8);
                if (nr == 0) break;
                /* lock the first chosen rock; keep mining it till gone */
                int bi = -1; float bd = 1e30f;
                for (int i = 0; i < nr; i++) {
                    float d = v3_len(v3_sub(rk[i], pl->pos));
                    if (d < bd) { bd = d; bi = i; }
                }
                if (bi < 0) break;
                MV_STEER(rk[bi], 0.05f);
                pl->throttle = (bd > 90.0f) ? 0.55f
                             : (bd > 45.0f) ? 0.22f : 0.0f;
                Vec3 l = m3_mul_v3_t(&pl->basis, v3_sub(rk[bi], pl->pos));
                CraftRawButtons b = none;
                /* hold fire whenever roughly on the rock and close */
                if (bd < 130.0f && l.z > 0 &&
                    l.x*l.x + l.y*l.y < bd*bd*0.03f) {
                    b.a = true;   /* hold trigger -> real cadence */
                }
                MV(b);
            }
            CAP("The rock shatters -- ore canisters spill out, scoop them");
            pl->throttle = 0.0f;
            for (int c2 = 0; c2 < 8; c2++) {
                Vec3 cans[6]; int comp[6];
                int n = loot_positions(cans, comp, 6);
                if (n == 0) { MV_IDLE(10); continue; }
                int bi = 0; float bd = 1e30f;
                for (int i = 0; i < n; i++) {
                    float d = v3_len(v3_sub(cans[i], pl->pos));
                    if (d < bd) { bd = d; bi = i; }
                }
                MV_STEER(cans[bi], 0.07f);
                pl->throttle = (bd > 90.0f) ? 0.6f : 0.25f;
                MV(none);
            }
            MV_IDLE(20);
            /* restore the combat loadout for the rest of the guide */
            g_player.mounts[0] = (WeaponInst){ .type = WPN_PULSE_S,
                .quality = Q_STANDARD, .integrity = 100, .in_use = 1 };
            player_apply_to_ship();
            pl->active_w = 0;
        }
        pl->throttle = 0.2f;
        MV_IDLE(20);

        /* Phase 3c: DISTRESS CALL rescue. */
        CAP("DISTRESS CALL: a trader is under attack");
        {
            extern const Mesh *hull_mesh(uint32_t, int);
            extern void elite_game_debug_set_distress_civ(int);
            Vec3 fwd = pl->basis.r[2], rgt = pl->basis.r[0];
            Vec3 cpos = v3_add(pl->pos, v3_scale(fwd, 360.0f));
            int civ = ship_spawn(hull_mesh(0xC1771Eu, 7), cpos,
                                 TEAM_NEUTRAL);
            int pa = -1, pb = -1;
            if (civ > 0) {
                ship_set_tier(civ, 1, 7);
                g_ships[civ].is_civilian = 1;
                g_ships[civ].team = TEAM_NEUTRAL;
                g_ships[civ].hull = g_ships[civ].hull_max * 0.55f;
                g_ships[civ].shield = 0;
                elite_game_debug_set_distress_civ(civ);
                pa = ship_spawn(hull_mesh(0x9A9Au, 2),
                                v3_add(cpos, v3_scale(rgt, 60.0f)),
                                TEAM_HOSTILE);
                pb = ship_spawn(hull_mesh(0x7B7Bu, 2),
                                v3_add(cpos, v3_scale(rgt, -55.0f)),
                                TEAM_HOSTILE);
                if (pa > 0) { ship_set_tier(pa, 1, 2);
                              g_ships[pa].ai_target = (uint8_t)civ; }
                if (pb > 0) { ship_set_tier(pb, 1, 2);
                              g_ships[pb].ai_target = (uint8_t)civ; }
            }
            pl->hull = pl->hull_max; pl->shield = pl->shield_max;
            MV_IDLE(25);
            CAP("Kill the pirates before they finish the trader");
            MV_TAP(lb, 3);                        /* lock an attacker */
            for (int f = 0; f < 30 * 11; f++) {
                int tgt = (pa > 0 && g_ships[pa].alive) ? pa
                        : (pb > 0 && g_ships[pb].alive) ? pb : -1;
                if (tgt < 0) break;
                MV_AIM(tgt, 0.0f, 3.0f, 0.012f, 1);
                pl->shield = pl->shield_max;
                if (f == 90) MV_TAP(lb, 3);       /* retarget */
            }
            CAP("Trader saved -- rescues pay credits and reputation");
            MV_IDLE(70);
        }
        pl->throttle = 0.2f;
        MV_IDLE(15);

        /* Phase 3b: ENCOUNTERS. A hail opens a seeded modal -- a portrait,
         * a situation, and choices that branch into credits, cargo,
         * reputation or a fight. */
        events_set_chance(100);
        if (elite_game_debug_open_event()) {
            CAP("ENCOUNTERS: a hail opens -- a face, a situation, a choice");
            MV_IDLE(95);                        /* read portrait + situation */
            MV_TAP(down, 2);                    /* weigh the options */
            MV_IDLE(45);
            CAP("Comply, ignore or fight -- every call has consequences");
            MV_TAP(a, 4);                       /* commit to a choice */
            MV_IDLE(80);                        /* the aftermath text */
            MV_TAP(a, 4);                       /* dismiss the hail */
            MV_IDLE(20);
        }
        events_set_chance(0);

        CAP("The dashboard rises -- and the galaxy never pauses while you read it");
        /* Phase 4: dashboard TOUR -> SYSTEM map -> supercruise. The
         * dash panels are live MFDs: 0 GALAXY, 1 SYSTEM, 2 STATUS. */
        MV_TAP(menu, 12);                       /* dash rises (in shot) */
        MV_IDLE(70);                            /* watch it slide up */
        pl->throttle = 0.18f;
        CAP("GALAXY panel: your place in the star map");
        for (int _u = 0; _u < 3; _u++) MV_TAP(left, 1);
        for (int _u = 0; _u < 3; _u++) MV_TAP(up, 1);   /* sel 0 */
        MV_IDLE(80);
        CAP("SYSTEM panel: planets, belts and stations, live");
        MV_TAP(right, 4);                       /* sel 1 */
        MV_IDLE(85);
        CAP("STATUS panel: hull, fuel and cargo at a glance");
        MV_TAP(left, 3); MV_TAP(down, 4);       /* sel 2 */
        MV_IDLE(85);
        CAP("Pick a destination on the SYSTEM map and cruise to it");
        MV_TAP(up, 3); MV_TAP(right, 3);        /* back to SYSTEM (1) */
        MV_TAP(a, 12);                          /* open system map */
        MV_IDLE(40);
        for (int k = 0; k < 5; k++) MV_TAP(down, 3);  /* POI #5 station */
        MV_IDLE(40);
        MV_TAP(a, 10);                          /* engage */

        CAP("SUPERCRUISE: cross the system in seconds");
        /* Phase 5: the cruise — dust, swelling planet, auto-drop. */
        for (int f = 0; f < 30 * 30 && elite_game_state() == 1; f++)
            MV(none);
        MV_IDLE(40);

        CAP("Approach the station -- the docking computer brings you in");
        /* Phase 6: approach + dock. */
        for (int f = 0; f < 1400; f++) {
            float d3 = v3_len(pl->pos);         /* station at origin */
            if (d3 < 420.0f) break;
            MV_STEER(v3(0, 0, 0), 0.05f);     /* station at origin */
            pl->throttle = (d3 > 700.0f) ? 0.85f : 0.4f;
            MV(none);
        }
        pl->throttle = 0.05f;
        printf("[movie] dock attempt at %.0fm state=%d\n",
               v3_len(pl->pos), elite_game_state());
        for (int k = 0; k < 3; k++) {           /* hold the dock chord */
            CraftRawButtons b = none;
            b.lb = true; b.rb = true;
            MV(b);
        }
        for (int f = 0; f < 200 && elite_game_state() != 7; f++) MV(none);
        printf("[movie] post-dock state=%d (7=DOCKED) frame=%d\n",
               elite_game_state(), mf);
        if (elite_game_state() != 7) {
            printf("[movie] DOCK FAILED -- aborting shoot\n");
            return 1;
        }
        MV_IDLE(45);

        /* Phase 7: station services. STATE-VERIFIED navigation — we read
         * the open screen (station_debug_screen) and only caption once it
         * is actually up, instead of guessing button timing (the old fixed
         * nav drifted badly: wrong LAUNCH row, B/MENU mixed). NOTE: B or
         * MENU at the HOME menu LAUNCHES, so ST_HOME only presses back
         * while NOT already home. Captions describe features, not buttons. */
        extern int station_debug_screen(void);
        /* SCR: 0 HOME 1 MARKET 2 SHIPYARD 3 OUTFIT 4 STATUS 5 MISSIONS */
        #define ST_SCR() station_debug_screen()
        #define ST_HOME() do { \
            for (int _g = 0; _g < 16 && ST_SCR() != 0; _g++) MV_TAP(b, 4); \
        } while (0)
        #define ST_OPEN(row, scr) do { \
            ST_HOME(); \
            for (int _u = 0; _u < 12; _u++) MV_TAP(up, 2); \
            for (int _d = 0; _d < (row); _d++) MV_TAP(down, 3); \
            for (int _t = 0; _t < 5 && ST_SCR() != (scr); _t++) MV_TAP(a, 6); \
        } while (0)

        ST_OPEN(0, 1);                          /* MARKET */
        CAP("Trade goods between worlds -- prices swing with each economy");
        MV_IDLE(55);
        MV_TAP(down, 8); MV_TAP(down, 8);       /* scroll the commodity table */
        MV_IDLE(28);

        ST_OPEN(1, 2);                          /* SHIPYARD */
        CAP("The shipyard stocks hulls, each a different trade-off");
        MV_IDLE(55);
        MV_TAP(down, 38); MV_TAP(down, 38);     /* browse the offers */
        MV_TAP(lb, 4);                          /* Info: full spec sheet */
        CAP("A full spec sheet weighs every stat against your own ship");
        MV_IDLE(68);

        ST_OPEN(2, 3);                          /* OUTFITTING */
        CAP("Outfitting: weapons, shields, armour and gadgets");
        MV_IDLE(55);
        MV_TAP(down, 30);                       /* page through the stock */
        MV_TAP(lb, 4);                          /* Info on an item */
        CAP("Every item has a full spec -- quality, wear and traits all vary");
        MV_IDLE(62);

        ST_OPEN(3, 5);                          /* MISSIONS */
        CAP("The mission board -- contracts for credits and reputation");
        MV_IDLE(72);

        ST_OPEN(5, 4);                          /* STATUS */
        CAP("Ship status -- every stat, and your fighter itself in the bay");
        MV_IDLE(48);
        MV_TAP(lb, 55);                         /* clean view of the hull */
        MV_IDLE(12);

        ST_HOME();                              /* back to the home menu */
        CAP("Refuel, repair, pay fines -- then launch back into the black");
        MV_IDLE(22);
        MV_TAP(menu, 6);                        /* MENU at HOME = launch */
        for (int f = 0; f < 220 && elite_game_state() == 8; f++) MV(none);
        MV_IDLE(30);
        #undef ST_OPEN
        #undef ST_HOME
        #undef ST_SCR

        printf("[movie] station phase done, state=%d frame=%d\n",
               elite_game_state(), mf);
        CAP("THE GALAXY CHART: every star is a place you can go");
        /* Phase 8: dashboard -> GALAXY -> pan -> filter tour -> fuel/
         * range explainer -> survey -> hyperjump. */
        MV_TAP(menu, 12);
        MV_TAP(a, 12);                          /* GALAXY CHART */
        MV_IDLE(45);
        CAP("Thousands of real stars to explore");
        /* wander the field to show the depth of the map */
        MV_TAP(right, 16); MV_TAP(right, 16); MV_TAP(up, 16);
        MV_TAP(up, 16); MV_TAP(left, 16); MV_TAP(down, 16);
        MV_TAP(right, 16); MV_TAP(up, 16); MV_TAP(left, 16);
        MV_IDLE(22);
        /* recenter on home so the jump target is in range: close to the
         * dash and reopen the chart (map_galaxy_open re-snaps to home) */
        MV_TAP(b, 8);
        for (int _u = 0; _u < 3; _u++) MV_TAP(left, 1);
        for (int _u = 0; _u < 3; _u++) MV_TAP(up, 1);   /* dash sel 0 */
        MV_TAP(a, 12);                           /* reopen, recentered */
        MV_IDLE(20);
        CAP("The ring shows your jump range -- fuel and drive set how far");
        MV_IDLE(75);
        /* caption goes BEFORE the RB that shows the layer, so each
         * comment matches the layer on screen during its dwell */
        CAP("STAR TYPE layer: each sun coloured by its class");
        MV_IDLE(60);                            /* dwell on spectral */
        CAP("Threat layer: green systems are safe, red is pirate country");
        MV_TAP(lb, 78);                         /* -> threat (LB cycles layers) */
        CAP("FACTION layer: who controls each system");
        MV_TAP(lb, 78);                         /* -> faction */
        CAP("ECONOMY layer: every station's trade -- the money map");
        MV_TAP(lb, 78);                         /* -> economy */
        CAP("Pick a destination within jump range");
        MV_TAP(lb, 55);                         /* -> back to star type */
        MV_IDLE(20);
        {
            /* aim the snap at the nearest in-range neighbour */
            const SystemInfo *si = system_info();
            float px2, py2;
            galaxy_star_pos(si->addr, &px2, &py2);
            float bx = 0, by = 0, bd2 = 1e9f;
            for (int sy2 = si->addr.sy - 2; sy2 <= si->addr.sy + 2; sy2++)
                for (int sx2 = si->addr.sx - 2; sx2 <= si->addr.sx + 2;
                     sx2++) {
                    int n2 = galaxy_sector_stars(sx2, sy2);
                    for (int i = 0; i < n2; i++) {
                        SysAddr a2 = { sx2, sy2, (uint8_t)i };
                        if (sysaddr_eq(a2, si->addr)) continue;
                        float x2, y2;
                        galaxy_star_pos(a2, &x2, &y2);
                        float dd = sqrtf((x2 - px2) * (x2 - px2) +
                                         (y2 - py2) * (y2 - py2));
                        if (dd < bd2) { bd2 = dd; bx = x2 - px2;
                                        by = y2 - py2; }
                    }
                }
            CraftRawButtons b = none;
            if (bx * bx > by * by) {
                if (bx > 0) b.right = true; else b.left = true;
            } else {
                if (by > 0) b.down = true; else b.up = true;
            }
            MV(b); MV_IDLE(20);
        }
        CAP("A system survey lists its economy, threat level and stations");
        MV_TAP(a, 85);                          /* survey sheet, linger */
        CAP("Each jump burns fuel from the tank");
        MV_IDLE(30);
        MV_TAP(a, 6);                           /* commit the jump */

        printf("[movie] jump committed, state=%d frame=%d\n",
               elite_game_state(), mf);
        CAP("HYPERSPACE: the drive lights and the stars streak past");
        /* Phase 9: recede + tunnel + arrival. */
        for (int f = 0; f < 30 * 8 && elite_game_state() != 0; f++)
            MV(none);
        /* Phase 10: gadgets, then the hunt that ends you. */
        elite_game_debug_face_away_from_sun();
        pl->throttle = 0.35f;
        CAP("Gadgets: the cloak makes you vanish for a few seconds");
        { CraftRawButtons b = none; b.rb = true; MV(b);
          b.b = true; MV(b); MV(b); }            /* RB held, then B */
        for (int f = 0; f < 95; f++) MV(none);   /* CLOAKED readout */
        CAP("Plus scanners, repair drones, chaff and more");
        for (int f = 0; f < 55; f++) MV(none);

        /* Phase 10b: FACTION WARS. Jump to a contested front and fight at
         * the beacon -- two fleets clash, and your kills bank credits and
         * reputation with the side you back. */
        {
            SystemInfo si_w; bool gotw = false;
            for (int sy = -20; sy <= 20 && !gotw; sy++)
                for (int sx = -20; sx <= 20 && !gotw; sx++) {
                    int ns = galaxy_sector_stars(sx, sy);
                    for (int i = 0; i < ns && !gotw; i++) {
                        SysAddr a = { sx, sy, (uint8_t)i };
                        galaxy_generate(a, &si_w);
                        if (si_w.n_stations > 0 && mission_near_front(a))
                            gotw = true;
                    }
                }
            for (int fc = 0; fc < N_FACTIONS; fc++) g_rep[fc] = 25;
            Mission *wm = NULL;
            if (gotw && mission_grant_warzone(&si_w)) {
                for (int i = 0; i < MAX_MISSIONS; i++)
                    if (g_missions[i].type == MIS_WARZONE) wm = &g_missions[i];
            }
            if (wm) {
                CAP("FACTION WARS: a contested beacon, two fleets clashing");
                elite_game_debug_jump(wm->target);
                elite_game_debug_goto_poi(0);
                for (int f = 0; f < 35; f++) MV(none);   /* warzone spins up */
                pl->active_w = 0;                        /* pulse, sustained */
                pl->hull = pl->hull_max; pl->shield = pl->shield_max;
                CAP("Back a side -- every kill banks credits and reputation");
                MV_TAP(lb, 3);                           /* lock a combatant */
                for (int f = 0; f < 30 * 9; f++) {
                    int tgt = -1; float best = 1e30f;
                    for (int i = 1; i < MAX_SHIPS; i++)
                        if (g_ships[i].alive &&
                            g_ships[i].team == TEAM_HOSTILE) {
                            float d = v3_len(v3_sub(g_ships[i].pos, pl->pos));
                            if (d < best) { best = d; tgt = i; }
                        }
                    if (tgt < 0) { MV(none); continue; }
                    MV_AIM(tgt, 0.0f, 3.0f, 0.016f, 1);
                    /* let enemy fire land -- shield flickers, hull dips and
                     * floors at ~45% so we win the engagement but visibly
                     * take hits (not an untouchable gun platform). */
                    if (pl->hull < pl->hull_max * 0.45f)
                        pl->hull = pl->hull_max * 0.45f;
                    if (f % 90 == 89) MV_TAP(lb, 2);     /* retarget */
                }
                MV_IDLE(20);
            }
        }

        CAP("A bounty MARK -- a DEADLY ace. The hunt is on");
        player_apply_to_ship();                  /* drop the plot armour */
        pl->hull = pl->hull_max; pl->shield = pl->shield_max;
        extern const Mesh *hull_mesh(uint32_t, int);
        Vec3 mfwd = pl->basis.r[2], mrgt = pl->basis.r[0];
        int mk = ship_spawn(hull_mesh(0xDEAD77u, 9),  /* BASILISK */
                            v3_add(pl->pos,
                                   v3_add(v3_scale(mfwd, 320.0f),
                                          v3_scale(mrgt, 50.0f))),
                            TEAM_HOSTILE);
        if (mk > 0) {
            ship_set_tier(mk, 4, 9);
            g_ships[mk].is_mark = 1;
            g_ships[mk].ai_target = 0;
            g_ships[mk].hull = g_ships[mk].hull_max = 9000.0f; /* survives */
            /* cool weapons (user): a purple LANCE, bending PHOTON bolts
             * and a PLASMA stream make the incoming fire spectacular */
            g_ships[mk].weapons[0] = WPN_LANCE;
            g_ships[mk].weapons[1] = WPN_PHOTON;
            g_ships[mk].weapons[2] = WPN_PLASMA;
            g_ships[mk].n_weapons = 3;
            g_ships[mk].turret_type = WPN_BLASTER;   /* bending turret */
        }
        pl->throttle = 0.35f;
        MV_IDLE(40);                             /* close the distance */
        MV_TAP(lb, 4);                           /* lock the mark */
        /* A REAL dogfight, not a nose-glued stare. Two states:
         *   APPROACH (far): aim the lead point, throttle up, fire.
         *   MERGE (close): STOP tracking -- fly straight through the
         *     pass so the mark whips across the screen and ends up
         *     behind, which forces a proper banked wheel-around as we
         *     re-acquire. That pass/break/re-engage IS the dogfight. */
        int merging = 0; float merge_t = 0.0f;
        for (int f = 0; f < 30 * 9 && g_ships[0].alive; f++) {
            for (int i = 1; i < MAX_SHIPS; i++)
                if (i != mk && g_ships[i].alive &&
                    g_ships[i].team == TEAM_HOSTILE)
                    g_ships[i].alive = false;
            CraftRawButtons b = none;
            if (mk > 0 && g_ships[mk].alive) {
                Ship *m = &g_ships[mk];
                Vec3 to = v3_sub(m->pos, pl->pos);
                float d = v3_len(to);
                if (!merging && d < 95.0f) { merging = 1; merge_t = 0.6f; }
                if (merging) {
                    /* fly straight THROUGH the merge (mark whips past) */
                    merge_t -= 1.0f / 30.0f;
                    pl->throttle = 1.0f;
                    if (merge_t <= 0.0f && d > 150.0f) merging = 0;
                } else {
                    /* APPROACH: lead the target, close, fire on line.
                     * Human wobble: track slightly off the lead so the
                     * pilot isn't a perfect shot -- some bolts miss (user:
                     * perfect aim isn't representative). */
                    Vec3 lead = v3_add(m->pos,
                                       v3_scale(v3_sub(m->vel, pl->vel),
                                                d / 900.0f));
                    float wob = sinf((float)f * 0.21f) * d * 0.06f;
                    Vec3 aim = v3_add(lead, v3_scale(pl->basis.r[0], wob));
                    MV_STEER(aim, 0.05f);
                    pl->throttle = (d > 300.0f) ? 1.0f
                                 : (d > 150.0f) ? 0.8f : 0.95f;
                    Vec3 l = m3_mul_v3_t(&pl->basis, v3_sub(aim, pl->pos));
                    if (d < 380.0f && l.z > 0.0f &&
                        l.x*l.x + l.y*l.y < d*d*0.014f) {
                        b.a = true;   /* hold trigger -> real cadence */
                    }
                }
            }
            MV(b);
            /* Take real damage so the duel reads as a losing fight: hull
             * dips under the mark's fire and is floored at ~30% for most
             * of it; in the closing seconds the floor falls away and the
             * ace finishes us -- attrition, not a scripted snap. */
            float fl = (f > 30 * 6) ? 0.0f : pl->hull_max * 0.30f;
            if (pl->hull < fl) pl->hull = fl;
        }
        /* safety net: if the ace hasn't landed the kill by the end, make
         * sure the death (and the kill report naming the mark) still lands */
        CAP("...and the better pilot wins");
        if (g_ships[0].alive) { g_ships[0].hull = -1.0f;
                                g_ships[0].alive = false; }
        MV(none);                                /* death path -> report */
        MV_IDLE(15);                             /* the explosion */
        CAP("The KILL REPORT names who got you -- study it, hunt smarter");
        for (int f = 0; f < 130; f++) MV(none);  /* read the report */
        CAP("The Indemnity makes you whole");
        MV_TAP(a, 6);
        for (int f = 0; f < 40; f++) MV(none);
        CAP("It rebuilds your ship at your last dock -- but the cargo");
        for (int f = 0; f < 70; f++) MV(none);
        CAP("and credits you earned since are gone");
        for (int f = 0; f < 90; f++) MV(none);
        if (gwav) fclose(gwav);
        printf("[movie] %d frames (player alive=%d)\n", mf,
               g_ships[0].alive);
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
        int nf = getenv("ELITE_TFRAMES") ? atoi(getenv("ELITE_TFRAMES")) : 45;
        if (getenv("ELITE_INTRO")) {           /* tap A -> NEW GAME -> lore crawl */
            CraftRawButtons a = {0}; a.a = true;
            elite_game_tick(&a, 1.0f / 30.0f);
            elite_game_tick(&none, 1.0f / 30.0f);
        }
        for (int k = 0; k < nf; k++) elite_game_tick(&none, 1.0f / 30.0f);  /* ELITE_TFRAMES: crawl frame */
        render_frame();
        dump_ppm("/tmp/title.ppm");
        return 0;
    }

    /* Title -> NEW GAME -> intro crawl -> launch, dumped frame by frame to
     * /tmp/vid/NNNN.ppm for encoding into a clip. */
    if (getenv("ELITE_TITLEVIDEO")) {
        CraftRawButtons none = {0};
        int fr = 0; char path[80];
        for (int k = 0; k < 150; k++) {            /* ~5s on the title */
            elite_game_tick(&none, 1.0f/30.0f); render_frame();
            snprintf(path, sizeof path, "/tmp/vid/%04d.ppm", fr++); dump_ppm(path);
        }
        { CraftRawButtons a = {0}; a.a = true;     /* select NEW GAME */
          elite_game_tick(&a, 1.0f/30.0f); render_frame();
          snprintf(path, sizeof path, "/tmp/vid/%04d.ppm", fr++); dump_ppm(path); }
        elite_game_tick(&none, 1.0f/30.0f);        /* release A */
        int extra = 0;
        while (fr < 1600) {                        /* intro until launch + a bit */
            elite_game_tick(&none, 1.0f/30.0f); render_frame();
            snprintf(path, sizeof path, "/tmp/vid/%04d.ppm", fr++); dump_ppm(path);
            if (elite_game_state() != 9 && ++extra >= 30) break;
        }
        printf("[vid] wrote %d frames\n", fr);
        return 0;
    }

    /* Title wordmark style explorer: render INDEMNITY RUN in a spread of
     * colour/treatment options -> /tmp/tf_N.ppm. Run the SS=1 host. */
    if (getenv("ELITE_TITLEFONTS")) {
        extern int craft_font_draw_title(uint16_t *, const char *, int, int, int,
                                         uint16_t, uint16_t, uint16_t);
        extern int craft_font_width(const char *);
        #define CC(r,g,b) RGB565C(r,g,b)
        enum { GRAD, SHADOW, GLOW };
        struct { uint16_t top, bot, outl, acc; int treat, s1, s2, single; } st[] = {
            { CC(238,244,255), CC(48,104,236),  CC(6,9,26),   CC(232,120,32),  GRAD,   3,4,0 },
            { CC(240,242,250), CC(150,90,215),  CC(10,8,22),  CC(205,175,255), GRAD,   3,4,0 },
            { CC(255,238,170), CC(210,140,30),  CC(28,14,4),  CC(255,205,70),  GLOW,   3,4,0 },
            { CC(238,250,255), CC(120,185,238), CC(8,16,30),  CC(80,200,255),  GLOW,   3,4,0 },
            { CC(255,212,120), CC(225,120,30),  CC(18,10,4),  CC(0,0,0),       SHADOW, 3,4,0 },
            { CC(255,180,170), CC(200,30,42),   CC(22,4,6),   CC(255,120,120), GRAD,   3,4,0 },
            { CC(225,232,242), CC(95,115,145),  CC(8,12,20),  CC(0,0,0),       SHADOW, 3,4,0 },
            { CC(180,255,180), CC(40,185,60),   CC(4,20,6),   CC(120,255,120), GRAD,   3,4,0 },
            { CC(238,244,255), CC(48,104,236),  CC(6,9,26),   CC(232,120,32),  GRAD,   2,2,1 },
        };
        int n = (int)(sizeof(st) / sizeof(st[0]));
        for (int i = 0; i < n; i++) {
            for (int p = 0; p < ELITE_FB_W * ELITE_FB_H; p++) g_fb[p] = CC(10,12,22);
            uint16_t tp = st[i].top, bt = st[i].bot, ol = st[i].outl, ac = st[i].acc;
            if (st[i].single) {
                const char *L = "INDEMNITY RUN"; int s = st[i].s1;
                int x = (128 - craft_font_width(L) * s) / 2, y = 54;
                if (st[i].treat == SHADOW)
                    craft_font_draw_title(g_fb, L, x+2, y+2, s, ol, ol, ol);
                if (st[i].treat == GLOW)
                    for (int o=0;o<4;o++){int dx=(o&1)?2:-2,dy=(o&2)?2:-2;
                        craft_font_draw_title(g_fb,L,x+dx,y+dy,s,ac,ac,ac);}
                craft_font_draw_title(g_fb, L, x, y, s, tp, bt, ol);
            } else {
                const char *A="INDEMNITY",*B="RUN"; int s1=st[i].s1,s2=st[i].s2;
                int ax=(128-craft_font_width(A)*s1)/2, bx=(128-craft_font_width(B)*s2)/2;
                int ay=36, by=62;
                if (st[i].treat == SHADOW){
                    craft_font_draw_title(g_fb,A,ax+3,ay+3,s1,ol,ol,ol);
                    craft_font_draw_title(g_fb,B,bx+3,by+3,s2,ol,ol,ol); }
                if (st[i].treat == GLOW)
                    for (int o=0;o<4;o++){int dx=(o&1)?2:-2,dy=(o&2)?2:-2;
                        craft_font_draw_title(g_fb,A,ax+dx,ay+dy,s1,ac,ac,ac);
                        craft_font_draw_title(g_fb,B,bx+dx,by+dy,s2,ac,ac,ac);}
                craft_font_draw_title(g_fb,A,ax,ay,s1,tp,bt,ol);
                craft_font_draw_title(g_fb,B,bx,by,s2,tp,bt,ol);
            }
            char path[48]; snprintf(path,sizeof path,"/tmp/tf_%d.ppm",i+1);
            dump_ppm(path);
        }
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
        TAP(menu, 12);                      /* dashboard (rises) */
        TAP(right, 3);                      /* -> SYSTEM region */
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
        TAPB(menu, 12);                 /* dashboard */
        TAPB(right, 3);                 /* SYSTEM region */
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
        extern void loot_on_kill(Vec3 pos, Vec3 vel, int tier, const Ship *);
        CraftRawButtons none = {0};
        for (int k = 0; k < 10; k++) elite_game_tick(&none, 1.0f / 30.0f);
        Ship *p = &g_ships[0];
        p->vel = v3(0, 0, 0);
        p->throttle = 0;
        for (int i = 0; i < 8; i++)
            loot_on_kill(v3_add(p->pos, v3(5, 0, 14)), v3(0, 0, 0), 4, NULL);
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
        TAPS(menu, 12); TAPS(right, 3); TAPS(a, 4);
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
        TAPM(menu, 12); TAPM(right, 3); TAPM(a, 4);
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
        for (int k = 0; k < 12; k++) elite_game_tick(&none, 1.0f / 30.0f);
        printf("[st] in dash: state=%d (want 12)\n", elite_game_state());
        b = none; b.down = true; elite_game_tick(&b, 1.0f / 30.0f);
        elite_game_tick(&none, 1.0f / 30.0f);   /* sel = STATUS */
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
        for (int k = 0; k < 12; k++) elite_game_tick(&none, 1.0f / 30.0f);
        b = none; b.a = true; elite_game_tick(&b, 1.0f / 30.0f);   /* galaxy map */
        for (int k = 0; k < 4; k++) elite_game_tick(&none, 1.0f / 30.0f);
        render_frame(); dump_ppm("/tmp/jump_0_map.ppm");
        if (getenv("ELITE_LAYERSHOT")) {
            b = none; b.rb = true; elite_game_tick(&b, 1.0f / 30.0f);
            b = none; elite_game_tick(&b, 1.0f / 30.0f);
            render_frame(); dump_ppm("/tmp/chart_threat.ppm");
            b = none; b.rb = true; elite_game_tick(&b, 1.0f / 30.0f);
            b = none; elite_game_tick(&b, 1.0f / 30.0f);
            render_frame(); dump_ppm("/tmp/chart_faction.ppm");
            b = none; b.rb = true; elite_game_tick(&b, 1.0f / 30.0f);
            b = none; elite_game_tick(&b, 1.0f / 30.0f);
            render_frame(); dump_ppm("/tmp/chart_econ.ppm");
            b = none; b.rb = true; elite_game_tick(&b, 1.0f / 30.0f);
            b = none; elite_game_tick(&b, 1.0f / 30.0f);
        }
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS |
                 SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    host_input_init();         /* open controllers/HOTAS, read env axis map */
    host_settings_load();      /* restore volume + sensitivity sliders */
    SDL_Window *win = SDL_CreateWindow("ThumbyElite", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    s_win = win;
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)                  /* WSLg / headless: fall back to software */
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    /* Aspect-correct scaling: letterbox the square frame in any window /
     * fullscreen size instead of stretching it. */
    SDL_RenderSetLogicalSize(ren, OUT_W, OUT_H);
    SDL_RenderSetIntegerScale(ren, SDL_FALSE);
    host_apply_fullscreen();   /* honour the persisted choice on launch */
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING, OUT_W, OUT_H);

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
                if (sc == SDL_SCANCODE_F11) {           /* toggle fullscreen */
                    s_host_settings[5] = !s_host_settings[5];
                    host_apply_fullscreen();
                }
#ifdef ELITE_STYLE_LAB
                if (sc == SDL_SCANCODE_F8) {            /* style lab A/B */
                    s_lab_style ^= 1;
                    lab_apply_style(s_lab_style);
                }
#endif
            }
            /* Controller / HOTAS hotplug. */
            if (ev.type == SDL_CONTROLLERDEVICEADDED ||
                ev.type == SDL_JOYDEVICEADDED)
                host_input_open(ev.cdevice.which);
            if (ev.type == SDL_CONTROLLERDEVICEREMOVED && s_pad) {
                SDL_GameControllerClose(s_pad); s_pad = NULL;
                host_input_reset_axes();
            }
            if (ev.type == SDL_JOYDEVICEREMOVED &&
                s_joy && ev.jdevice.which == s_joy_id) {
                SDL_JoystickClose(s_joy); s_joy = NULL; s_joy_id = -1;
                host_input_reset_axes();
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
        /* Controllers/HOTAS augment the keyboard and drive the analog hooks.
         * Aim axes scale by the in-game "GAMEPAD" sensitivity slider. */
        host_input_apply(&btn, (float)plat_setting_get(2) * 0.1f);

        Uint32 t0 = SDL_GetTicks();
        elite_game_tick(&btn, dt);
        render_frame();
        elite_game_set_frame_ms((float)(SDL_GetTicks() - t0));

        SDL_UpdateTexture(tex, NULL, g_fb, OUT_W * sizeof(uint16_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }

    host_settings_save();
    if (s_pad) SDL_GameControllerClose(s_pad);
    if (s_joy) SDL_JoystickClose(s_joy);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
