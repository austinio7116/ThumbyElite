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

add_executable(thumbyelite_host
    ${CMAKE_CURRENT_LIST_DIR}/host_main.c
    ${GAME}/elite_game.c
    ${GAME}/r3d_raster.c
    ${GAME}/r3d_pipe.c
    ${GAME}/craft_font.c
)

target_compile_options(thumbyelite_host PRIVATE
    -O2 -g -Wall -Wextra
    -Wno-unused-parameter -Wno-unused-function
    -ffast-math
)
target_include_directories(thumbyelite_host PRIVATE
    ${GAME}
    ${ELITE_ROOT}/device   # craft_buttons.h type (host doesn't link buttons.c)
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(thumbyelite_host PRIVATE ${SDL2_LIBRARIES} m)
