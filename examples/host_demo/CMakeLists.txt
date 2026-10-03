add_executable(clipilot_demo main.c)

target_link_libraries(clipilot_demo PRIVATE clipilot)

set_target_properties(clipilot_demo PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

if(MSVC)
    target_compile_options(clipilot_demo PRIVATE /W4)
else()
    target_compile_options(clipilot_demo PRIVATE -Wall -Wextra)
    if(NOT WIN32)
        target_compile_definitions(clipilot_demo PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
endif()
