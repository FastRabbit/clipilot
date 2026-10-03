cmake_minimum_required(VERSION 3.13)

# Pico 2 = RP2350, Cortex-M33. Set before the SDK is imported.
set(PICO_BOARD pico2 CACHE STRING "Target board")

# pico_sdk_import.cmake ships with the SDK. Copy it next to this file, or let
# it be picked up from the SDK installation pointed at by PICO_SDK_PATH.
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
elseif(DEFINED ENV{PICO_SDK_PATH} AND
       EXISTS $ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
else()
    message(FATAL_ERROR
        "pico_sdk_import.cmake not found. Set PICO_SDK_PATH to your Pico SDK "
        "checkout, or copy <sdk>/external/pico_sdk_import.cmake next to this "
        "CMakeLists.txt.")
endif()

project(clipilot_pico2 C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

# clipilot lives two directories up. Its tests and host demo are host
# programs and must not be built for the target.
#
# Moving this example into its own repository? This is the only line that
# needs changing - see "Using this example in your own repository" in
# README.md.
set(CLI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(CLI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(../.. clipilot_build)

add_executable(clipilot_pico2 main.c)

target_link_libraries(clipilot_pico2 PRIVATE
    clipilot::clipilot
    pico_stdlib
)

# Console on USB CDC, not on the UART pins.
pico_enable_stdio_usb(clipilot_pico2 1)
pico_enable_stdio_uart(clipilot_pico2 0)

# Trim the CLI to what a two-LED console needs. These must be PUBLIC on the
# clipilot target: putting them on this executable would leave the library
# compiled with the defaults, and the two would disagree about sizeof(cli_t).
target_compile_definitions(clipilot PUBLIC
    CLI_MAX_LINE_LEN=64
    CLI_HISTORY_DEPTH=4
    CLI_MAX_TOKENS=8
    CLI_MAX_ARGS=6
    CLI_ENABLE_FLOAT=0
)

pico_add_extra_outputs(clipilot_pico2)
