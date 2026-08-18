cmake_minimum_required(VERSION 3.20)

project(bbasic_modern VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(bbasic_runtime STATIC
    src/bbasic_runtime.c
    src/bbasic_win32.c
)
target_include_directories(bbasic_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(WIN32)
    target_link_libraries(bbasic_runtime PUBLIC gdi32 user32 comdlg32 winmm)
endif()

add_executable(bbasicc
    src/bbasicc.c
)

if(WIN32)
    add_executable(bbasic_ide WIN32
        src/bbasic_ide.c
    )
    target_link_libraries(bbasic_ide PRIVATE comdlg32 gdi32 user32)
endif()

if(MSVC)
    target_compile_definitions(bbasic_runtime PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(bbasicc PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(WIN32)
        target_compile_definitions(bbasic_ide PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(bbasic_ide PRIVATE /W4)
    endif()
    target_compile_options(bbasic_runtime PRIVATE /W4)
    target_compile_options(bbasicc PRIVATE /W4)
else()
    target_compile_options(bbasic_runtime PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(bbasicc PRIVATE -Wall -Wextra -Wpedantic)
    if(WIN32)
        target_compile_options(bbasic_ide PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

set(BBASIC_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${BBASIC_GENERATED_DIR})

add_custom_command(
    OUTPUT ${BBASIC_GENERATED_DIR}/hello.c
    COMMAND $<TARGET_FILE:bbasicc>
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.bas
            -o ${BBASIC_GENERATED_DIR}/hello.c
    DEPENDS bbasicc ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.bas
    VERBATIM
)

add_executable(bbasic_hello ${BBASIC_GENERATED_DIR}/hello.c)
target_link_libraries(bbasic_hello PRIVATE bbasic_runtime)

add_custom_command(
    OUTPUT ${BBASIC_GENERATED_DIR}/core.c
    COMMAND $<TARGET_FILE:bbasicc>
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/core.bas
            -o ${BBASIC_GENERATED_DIR}/core.c
    DEPENDS bbasicc ${CMAKE_CURRENT_SOURCE_DIR}/examples/core.bas
    VERBATIM
)

add_executable(bbasic_core ${BBASIC_GENERATED_DIR}/core.c)
target_link_libraries(bbasic_core PRIVATE bbasic_runtime)

add_custom_command(
    OUTPUT ${BBASIC_GENERATED_DIR}/core_extended.c
    COMMAND $<TARGET_FILE:bbasicc>
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/core_extended.bas
            -o ${BBASIC_GENERATED_DIR}/core_extended.c
    DEPENDS bbasicc ${CMAKE_CURRENT_SOURCE_DIR}/examples/core_extended.bas
    VERBATIM
)

add_executable(bbasic_core_extended ${BBASIC_GENERATED_DIR}/core_extended.c)
target_link_libraries(bbasic_core_extended PRIVATE bbasic_runtime)

add_custom_command(
    OUTPUT ${BBASIC_GENERATED_DIR}/file_io.c
    COMMAND $<TARGET_FILE:bbasicc>
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/file_io.bas
            -o ${BBASIC_GENERATED_DIR}/file_io.c
    DEPENDS bbasicc ${CMAKE_CURRENT_SOURCE_DIR}/examples/file_io.bas
    VERBATIM
)

add_executable(bbasic_file_io ${BBASIC_GENERATED_DIR}/file_io.c)
target_link_libraries(bbasic_file_io PRIVATE bbasic_runtime)

add_custom_command(
    OUTPUT ${BBASIC_GENERATED_DIR}/win32_smoke.c
    COMMAND $<TARGET_FILE:bbasicc>
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/win32_smoke.bas
            -o ${BBASIC_GENERATED_DIR}/win32_smoke.c
    DEPENDS bbasicc ${CMAKE_CURRENT_SOURCE_DIR}/examples/win32_smoke.bas
    VERBATIM
)

add_executable(bbasic_win32_smoke ${BBASIC_GENERATED_DIR}/win32_smoke.c)
target_link_libraries(bbasic_win32_smoke PRIVATE bbasic_runtime)

enable_testing()
add_test(NAME hello COMMAND bbasic_hello)
set_tests_properties(hello PROPERTIES
    PASS_REGULAR_EXPRESSION "Hello from modern BasicBasic!"
)
add_test(NAME core_language COMMAND bbasic_core)
set_tests_properties(core_language PROPERTIES
    PASS_REGULAR_EXPRESSION "branch two.* 4 "
)
add_test(NAME extended_core_language COMMAND bbasic_core_extended)
set_tests_properties(extended_core_language PROPERTIES
    PASS_REGULAR_EXPRESSION "TOTAL:12.* 9 "
)
add_test(NAME file_io_language COMMAND bbasic_file_io)
set_tests_properties(file_io_language PROPERTIES
    PASS_REGULAR_EXPRESSION " 7 .*hello.*AB  CD"
)
if(WIN32)
    add_test(NAME win32_runtime COMMAND bbasic_win32_smoke)
endif()
