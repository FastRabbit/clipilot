cmake_minimum_required(VERSION 3.15)
project(clipilot VERSION 0.1.0 LANGUAGES C)

option(CLI_BUILD_TESTS "Build the clipilot unit tests" ON)
option(CLI_BUILD_EXAMPLES "Build the clipilot host demo" ON)

add_library(clipilot STATIC
    src/cli.c
    src/cli_parse.c
    src/cli_edit.c
    src/cli_complete.c
    src/cli_builtin.c
)

target_include_directories(clipilot PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(clipilot PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(clipilot PRIVATE /W4 /WX)
else()
    target_compile_options(clipilot PRIVATE
        -Wall -Wextra -Werror -Wshadow -Wconversion -Wundef
        -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
    )
endif()

add_library(clipilot::clipilot ALIAS clipilot)

if(CLI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CLI_BUILD_EXAMPLES)
    add_subdirectory(examples/host_demo)
endif()
