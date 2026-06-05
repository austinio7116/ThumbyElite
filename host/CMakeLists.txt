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

find_package(SDL2 REQUIRED)

set(ELITE_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(GAME       ${ELITE_ROOT}/game)
set(MODELS     ${ELITE_ROOT}/tools/models)

# Bake ship meshes (OBJ -> const C tables in flash) with the native
# obj2mesh tool — mirrors the ThumbyRogue texture-baker pattern.
include(ExternalProject)
ExternalProject_Add(thumbyelite_baker
    SOURCE_DIR ${ELITE_ROOT}/tools
    BINARY_DIR ${CMAKE_BINARY_DIR}/baker
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
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

add_executable(thumbyelite_host
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
    ${GAME}/mission.c
    ${GAME}/elite_audio.c
    ${GAME}/elite_save.c
    ${GAME}/elite_ai.c
    ${GAME}/ui_hud.c
    ${GAME}/ui_map.c
    ${GAME}/ui_station.c
    ${GAME}/ui_status.c
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
add_dependencies(thumbyelite_host generate_meshes)

target_compile_options(thumbyelite_host PRIVATE
    -O2 -g -Wall -Wextra
    -Wno-unused-parameter -Wno-unused-function
    -ffast-math
)
target_include_directories(thumbyelite_host PRIVATE
    ${GAME}
    ${ELITE_ROOT}/device   # craft_buttons.h type (host doesn't link buttons.c)
    ${CMAKE_BINARY_DIR}/generated
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(thumbyelite_host PRIVATE ${SDL2_LIBRARIES} m)
