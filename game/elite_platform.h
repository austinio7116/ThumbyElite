/*
 * ThumbyElite — platform hooks the game calls, shells implement.
 */
#ifndef ELITE_PLATFORM_H
#define ELITE_PLATFORM_H

#include <stdint.h>
#include "elite_ctrl.h"

/* Rumble: intensity 0..1 for seconds. No-op on host. */
void plat_rumble(float intensity, float seconds);

/* Persistent save blob. Return bytes read / nonzero on success. */
int plat_save(const uint8_t *data, int len);
int plat_load(uint8_t *data, int max_len);

/* System settings bridge (ThumbyOne shared store in slot mode).
 * which: 0 = volume (0..20), 1 = brightness (0..255). Set applies the
 * value immediately AND persists it where the platform supports it. */
int  plat_setting_get(int which);
void plat_setting_set(int which, int value);

/* Controller binding (in-game CONTROLLER SETUP screen). The shell owns all
 * device specifics; the game UI is generic. present()=any controller,
 * editable()=the device can be rebound (HOTAS; a gamepad is read-only). */
int  plat_ctrl_present(void);
int  plat_ctrl_editable(void);
const char *plat_ctrl_device_name(void);
void plat_ctrl_axis_label(CtrlAxis ax, char *out, int cap);
void plat_ctrl_btn_label(CtrlButton b, char *out, int cap);
/* Press-to-bind: begin listening, poll each frame (1=bound, 0=waiting,
 * -1=timeout), or cancel. kind = CTRL_KIND_AXIS/BUTTON, which = CtrlAxis/Button. */
void plat_ctrl_capture_begin(int kind, int which);
int  plat_ctrl_capture_poll(void);
void plat_ctrl_capture_cancel(void);
void plat_ctrl_axis_invert(CtrlAxis ax);
void plat_ctrl_clear(int kind, int which);
void plat_ctrl_save(void);
/* Live input monitor for the setup screen: call each frame, then read the
 * label of the button/axis being pressed/moved right now ("BTN 5","AXIS 3",
 * or "" if idle/none) so the user can identify a control before binding. */
void plat_ctrl_monitor(void);
const char *plat_ctrl_last_input(void);

#endif
