#
# ThumbyElite — device firmware (RP2350, Thumby Color). Phase 1.
#
# Standalone build — produces thumbyelite.uf2 to drop onto the RPI-RP2350
# mass-storage drive after entering BOOTSEL (turn off, hold DOWN, turn on).
#
# Build:
#   cmake -S device -B build_device \
#       -DPICO_SDK_PATH=$HOME/mp-thumby/lib/pico-sdk
#   cmake --build build_device -j8
#   cp build_device/thumbyelite.uf2 ../firmware_thumbyelite.uf2
#
# Wrapped as a ThumbyOne slot (THUMBYONE_SLOT_MODE) in Phase 10.
#
cmake_minimum_required(VERSION 3.16)

# Standalone defines its own SDK/project; as a ThumbyOne slot the parent
# CMakeLists has already initialised the SDK and just add_subdirectory()s us.
if (NOT THUMBYONE_SLOT_MODE)
    set(PICO_BOARD tinycircuits_thumby_color_rp2350)
    set(PICO_PLATFORM rp2350)
    include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

    project(thumbyelite_device C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 14)

    pico_sdk_init()
endif()

set(ELITE_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(GAME       ${ELITE_ROOT}/game)
set(DEVICE     ${CMAKE_CURRENT_LIST_DIR})

add_executable(thumbyelite
    # Platform layer.
    ${DEVICE}/elite_device_main.c
    ${DEVICE}/craft_lcd_gc9107.c
    ${DEVICE}/craft_buttons.c

    # Game + renderer (platform-independent).
    ${GAME}/elite_game.c
    ${GAME}/r3d_raster.c
    ${GAME}/r3d_pipe.c
    ${GAME}/craft_font.c
)

target_include_directories(thumbyelite PRIVATE ${GAME} ${DEVICE})

target_compile_options(thumbyelite PRIVATE
    -O3 -g -Wall -Wextra
    -Wno-unused-parameter -Wno-unused-function -Wno-unused-but-set-variable
    -ffast-math
    -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16
)

target_link_libraries(thumbyelite
    pico_stdlib pico_multicore pico_rand
    hardware_spi hardware_dma hardware_pwm hardware_irq
    hardware_flash hardware_sync hardware_clocks hardware_gpio
)

if (THUMBYONE_SLOT_MODE)
    set(ELITE_HEAP_SIZE 0x2000)
else()
    set(ELITE_HEAP_SIZE 0x4000)
endif()

target_compile_definitions(thumbyelite PRIVATE
    PICO_HEAP_SIZE=${ELITE_HEAP_SIZE}
    PICO_STACK_SIZE=0x1000
    PICO_CORE1_STACK_SIZE=0x1000
    $<$<BOOL:${THUMBYONE_SLOT_MODE}>:THUMBYONE_SLOT_MODE=1>
)

pico_enable_stdio_usb(thumbyelite 0)
pico_enable_stdio_uart(thumbyelite 0)

# Slot mode: switch the UF2 family to "absolute" so the lobby's chain_image
# path can load us wherever the rebase places us in flash.
if (THUMBYONE_SLOT_MODE)
    pico_set_uf2_family(thumbyelite "absolute")
endif()

pico_add_extra_outputs(thumbyelite)

if (THUMBYONE_SLOT_MODE)
    set_target_properties(thumbyelite PROPERTIES OUTPUT_NAME "firmware_thumbyelite")
endif()
