cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD pocket_scion CACHE STRING "Pocket SCION board")

if(NOT DEFINED PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    else()
        message(FATAL_ERROR "Set PICO_SDK_PATH to a Raspberry Pi Pico SDK checkout")
    endif()
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pocket_scion_synth C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pocket_scion_synth
    src/main.c
    src/audio_i2s.c
    src/controls.c
    src/midi_uart.c
    src/raw_capture.c
    src/sensor.c
    src/sensor_math.c
    src/status_rgb.c
    src/synth.cpp
    src/usb_descriptors.c
)

pico_set_program_name(pocket_scion_synth "Pocket SCION PRA32-U")
pico_set_program_version(pocket_scion_synth "2.3.0")
pico_set_program_description(pocket_scion_synth "Sensor-driven four-voice PRA32-U synthesizer")
pico_set_program_url(pocket_scion_synth "https://github.com/toonhuysmans/pocket-scion-synth")

pico_generate_pio_header(pocket_scion_synth
    ${CMAKE_CURRENT_LIST_DIR}/pio/i2s_tx.pio
)
pico_generate_pio_header(pocket_scion_synth
    ${CMAKE_CURRENT_LIST_DIR}/pio/rgb_tx.pio
)

target_include_directories(pocket_scion_synth PRIVATE src)
target_include_directories(pocket_scion_synth SYSTEM PRIVATE vendor/pra32-u)
target_compile_options(pocket_scion_synth PRIVATE
    -Wall -Wextra -Wshadow -Wconversion
)
target_compile_definitions(pocket_scion_synth PRIVATE SCION_RGB_OUTPUT_ENABLED=1)
target_link_libraries(pocket_scion_synth PRIVATE
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_gpio
    hardware_irq
    hardware_pio
    hardware_uart
    pico_multicore
    tinyusb_device
    m
)

pico_enable_stdio_usb(pocket_scion_synth 0)
pico_enable_stdio_uart(pocket_scion_synth 0)
pico_add_extra_outputs(pocket_scion_synth)
