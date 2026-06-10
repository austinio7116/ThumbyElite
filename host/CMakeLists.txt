#
# ThumbyElite — host (Linux/SDL2) build.
#
# Build:
#   cmake -B build_host -S host
#   cmake --build build_host -j8
#   ./build_host/thumbyelite_host [seed]
#
cmake_minimum_required(VERSION 3.16)
project(thumbyelite_host C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(ELITE_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(GAME       ${ELITE_ROOT}/game)
set(MODELS     ${ELITE_ROOT}/tools/models)

# SDL2: native via find_package; Windows cross-build (MinGW) uses the
# vendored devel libs (tools/fetch_sdl2_mingw.sh).
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    enable_language(RC)
    set(SDL2_WIN ${ELITE_ROOT}/host/win/SDL2/x86_64-w64-mingw32)
    if(NOT EXISTS ${SDL2_WIN}/include/SDL2/SDL.h)
        message(FATAL_ERROR "SDL2 MinGW libs missing — run tools/fetch_sdl2_mingw.sh")
    endif()
    set(SDL2_INCLUDE_DIRS ${SDL2_WIN}/include)
    # Link order matters: mingw32 -> SDL2main (provides WinMain) -> SDL2.
    set(SDL2_LIBRARIES mingw32
        ${SDL2_WIN}/lib/libSDL2main.a
        ${SDL2_WIN}/lib/libSDL2.dll.a)
    set(SDL2_DLL ${SDL2_WIN}/bin/SDL2.dll)
else()
    find_package(SDL2 REQUIRED)
endif()

# Bake ship meshes (OBJ -> const C tables in flash) with the native
# obj2mesh tool — mirrors the ThumbyRogue texture-baker pattern. Forced to
# the NATIVE compiler (-DCMAKE_C_COMPILER=cc) so it still runs during a
# Windows cross-build.
include(ExternalProject)
ExternalProject_Add(thumbyelite_baker
    SOURCE_DIR ${ELITE_ROOT}/tools
    BINARY_DIR ${CMAKE_BINARY_DIR}/baker
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cc
    INSTALL_COMMAND ""
    BUILD_ALWAYS 1
    BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/baker/obj2mesh
)
set(MESHES_GEN_C ${CMAKE_BINARY_DIR}/generated/meshes_gen.c)
set(MESH_ARGS
    shuttle=${MODELS}/shuttle.obj
    courier=${MODELS}/courier.obj
    fighter=${MODELS}/fighter.obj
    mauler=${MODELS}/mauler.obj
    viper=${MODELS}/viper.obj
    cutter=${MODELS}/cutter.obj
    lighthauler=${MODELS}/lighthauler.obj
    freighter=${MODELS}/freighter.obj
    hauler=${MODELS}/hauler.obj
    dread=${MODELS}/dread.obj
    canister=${MODELS}/canister.obj
    station=${MODELS}/station.obj
    beacon=${MODELS}/beacon.obj
)
add_custom_command(
    OUTPUT ${MESHES_GEN_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
    COMMAND ${CMAKE_BINARY_DIR}/baker/obj2mesh ${MESHES_GEN_C}
            ${CMAKE_BINARY_DIR}/generated/meshes_gen.h ${MESH_ARGS}
    DEPENDS thumbyelite_baker ${MODELS}/shuttle.obj ${MODELS}/courier.obj ${MODELS}/fighter.obj ${MODELS}/mauler.obj ${MODELS}/viper.obj ${MODELS}/cutter.obj ${MODELS}/lighthauler.obj ${MODELS}/freighter.obj ${MODELS}/hauler.obj ${MODELS}/dread.obj ${MODELS}/canister.obj ${MODELS}/station.obj ${MODELS}/beacon.obj ${MODELS}/ships.mtl
    COMMENT "Baking ship meshes"
)
add_custom_target(generate_meshes DEPENDS ${MESHES_GEN_C})

set(ELITE_SRC
    ${CMAKE_CURRENT_LIST_DIR}/host_main.c
    ${GAME}/elite_game.c
    ${GAME}/r3d_raster.c
    ${GAME}/r3d_pipe.c
    ${GAME}/r3d_scene.c
    ${GAME}/r3d_fx.c
    ${GAME}/elite_entity.c
    ${GAME}/elite_input.c
    ${GAME}/elite_flight.c
    ${GAME}/elite_combat.c
    ${GAME}/elite_weapons.c
    ${GAME}/elite_proj.c
    ${GAME}/elite_loot.c
    ${GAME}/elite_rocks.c
    ${GAME}/elite_collide.c
    ${GAME}/mission.c
    ${GAME}/elite_audio.c
    ${GAME}/elite_save.c
    ${GAME}/elite_ai.c
    ${GAME}/ui_hud.c
    ${GAME}/ui_map.c
    ${GAME}/ui_station.c
    ${GAME}/ui_status.c
    ${GAME}/ui_ctrlsetup.c
    ${GAME}/ui_icons.c
    ${GAME}/ui_detail.c
    ${GAME}/econ.c
    ${GAME}/elite_player.c
    ${GAME}/galaxy_gen.c
    ${GAME}/enames.c
    ${GAME}/system_sim.c
    ${GAME}/station_gen.c
    ${GAME}/ship_gen.c
    ${GAME}/elite_ships.c
    ${GAME}/r3d_planet.c
    ${GAME}/craft_font.c
    ${MESHES_GEN_C}
)

# Three targets from one source set:
#   thumbyelite_host        device-faithful 128x128 (SS=1)
#   thumbyelite_host_hires  Android preview     256x256 (SS=2, split overlay)
#   thumbyelite_host_quad   desktop quad-res    512x512 (SS=4, split overlay)
# The hires/quad builds render the 3D world at the higher resolution and
# composite the 128-logical HUD pixel-multiplied on top (text scales with SS).
# This SDL2 host is the cross-platform "PC" build — the same source compiles
# and runs on Linux and Windows.
# Compiler flags: GCC/Clang (Linux, macOS, MinGW) vs MSVC (Windows/VS).
if(MSVC)
    set(ELITE_COPTS /O2 /fp:fast /W3 /wd4244 /wd4267 /wd4101)
else()
    set(ELITE_COPTS -O2 -g -Wall -Wextra
        -Wno-unused-parameter -Wno-unused-function -ffast-math)
endif()

set(WIN_CROSS FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT MSVC)
    set(WIN_CROSS TRUE)
    set(ELITE_WIN_RC ${ELITE_ROOT}/host/win/thumbyelite.rc)   # icon + version
endif()

# Linux/macOS build all three resolutions for dev/preview; the Windows
# distributable is the quad-res game only (MinGW cross or MSVC native).
if(WIN32)
    set(ELITE_TARGETS thumbyelite_host_quad)
else()
    set(ELITE_TARGETS thumbyelite_host thumbyelite_host_hires thumbyelite_host_quad)
endif()

foreach(tgt IN LISTS ELITE_TARGETS)
    if(WIN_CROSS)
        add_executable(${tgt} ${ELITE_SRC} ${ELITE_WIN_RC})
    else()
        add_executable(${tgt} ${ELITE_SRC})
    endif()
    add_dependencies(${tgt} generate_meshes)
    target_compile_options(${tgt} PRIVATE ${ELITE_COPTS})
    target_include_directories(${tgt} PRIVATE
        ${GAME}
        ${ELITE_ROOT}/device   # craft_buttons.h type (host doesn't link buttons.c)
        ${CMAKE_BINARY_DIR}/generated
        ${SDL2_INCLUDE_DIRS}
    )
    target_link_libraries(${tgt} PRIVATE ${SDL2_LIBRARIES} m)
    target_compile_definitions(${tgt} PRIVATE ELITE_INPUT_DEBUG=1 ELITE_ANALOG_SETTINGS=1)
    if(WIN_CROSS)
        # GUI subsystem (no console window) + static libgcc so the .exe needs
        # only SDL2.dll alongside it.
        target_link_options(${tgt} PRIVATE -mwindows -static-libgcc)
    endif()
endforeach()
if(TARGET thumbyelite_host_hires)
    target_compile_definitions(thumbyelite_host_hires PRIVATE
        R3D_SS=2 ELITE_OVERLAY_SPLIT=1)
endif()
target_compile_definitions(thumbyelite_host_quad PRIVATE
    R3D_SS=4 ELITE_OVERLAY_SPLIT=1)

# --- Windows distribution package ---------------------------------------
# The quad-res build is the shippable game: name it ThumbyElite.exe and drop
# it next to SDL2.dll + a readme in build_win/dist/ for sideloading or a Steam
# depot.
if(WIN_CROSS)
    set_target_properties(thumbyelite_host_quad PROPERTIES OUTPUT_NAME ThumbyElite)
    set(DIST ${CMAKE_BINARY_DIR}/dist)
    add_custom_command(TARGET thumbyelite_host_quad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:thumbyelite_host_quad> ${DIST}/ThumbyElite.exe
        COMMAND ${CMAKE_COMMAND} -E copy ${SDL2_DLL} ${DIST}/SDL2.dll
        COMMAND ${CMAKE_COMMAND} -E copy ${ELITE_ROOT}/host/win/DIST_README.txt ${DIST}/README.txt
        COMMENT "Packaging Windows dist/ (ThumbyElite.exe + SDL2.dll)"
        VERBATIM)
endif()
