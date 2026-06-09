LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := main

SDL_PATH := ../SDL
# Shared, platform-independent game + engine sources (identical to the
# host and RP2350 device builds).
GAME   := $(LOCAL_PATH)/../../../../game
DEVICE := $(LOCAL_PATH)/../../../../device

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/$(SDL_PATH)/include \
    $(GAME) \
    $(DEVICE) \
    $(LOCAL_PATH)/generated

# Android has far more headroom than the RP2350. The two compile-time
# overrides below are the whole port (everything else is the device code):
#   - R3D_SS=2          : the 3D world rasterises at 256x256 (4x the device
#                         pixels) for smooth ship/planet/starfield edges.
#   - ELITE_OVERLAY_SPLIT: the 2D HUD/menus draw into a separate 128-logical
#                         key-colour layer, composited (pixel-doubled) over
#                         the 3D frame — so text stays crisp 2x and the
#                         status-screen dim works without reading the 3D buf.
# craft_buttons.h (DEVICE dir) supplies the CraftRawButtons type only; the
# .c reader isn't linked — android_main.c builds the struct from input.
LOCAL_CFLAGS := -DR3D_SS=2 -DELITE_OVERLAY_SPLIT=1 -DNDEBUG \
                -O3 -ffast-math -std=c11 \
                -Wall -Wno-unused-parameter -Wno-unused-function \
                -Wno-implicit-function-declaration
# The shared game compiles under gcc (host/device) with a few missing-include
# warnings for cross-module SFX/UI helpers; all return void/bool/int (no float,
# so no ABI hazard). NDK clang promotes those to errors, so we match gcc here
# rather than edit shared code for a build-only difference.

LOCAL_SRC_FILES := \
    android_main.c \
    generated/meshes_gen.c \
    $(GAME)/elite_game.c \
    $(GAME)/r3d_raster.c \
    $(GAME)/r3d_pipe.c \
    $(GAME)/r3d_scene.c \
    $(GAME)/r3d_fx.c \
    $(GAME)/r3d_planet.c \
    $(GAME)/elite_entity.c \
    $(GAME)/elite_input.c \
    $(GAME)/elite_flight.c \
    $(GAME)/elite_combat.c \
    $(GAME)/elite_weapons.c \
    $(GAME)/elite_proj.c \
    $(GAME)/elite_loot.c \
    $(GAME)/elite_rocks.c \
    $(GAME)/elite_collide.c \
    $(GAME)/mission.c \
    $(GAME)/elite_audio.c \
    $(GAME)/elite_save.c \
    $(GAME)/elite_ai.c \
    $(GAME)/ui_hud.c \
    $(GAME)/ui_map.c \
    $(GAME)/ui_station.c \
    $(GAME)/ui_status.c \
    $(GAME)/ui_icons.c \
    $(GAME)/ui_detail.c \
    $(GAME)/econ.c \
    $(GAME)/elite_player.c \
    $(GAME)/galaxy_gen.c \
    $(GAME)/enames.c \
    $(GAME)/system_sim.c \
    $(GAME)/station_gen.c \
    $(GAME)/ship_gen.c \
    $(GAME)/elite_ships.c \
    $(GAME)/craft_font.c

LOCAL_SHARED_LIBRARIES := SDL2

LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid

include $(BUILD_SHARED_LIBRARY)
