add_executable(clipilot_tests
    test_main.c
    test_util.c
    test_cmds.c
    test_parse.c
    test_dispatch.c
    test_edit.c
    test_complete.c
)

target_link_libraries(clipilot_tests PRIVATE clipilot)

set_target_properties(clipilot_tests PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(clipilot_tests PRIVATE /W4)
else()
    target_compile_options(clipilot_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME clipilot_tests COMMAND clipilot_tests)
