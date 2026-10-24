# Tests are assert-based; Release's -DNDEBUG would strip them to vacuous
# no-ops (or worse: side-effecting asserts vanish — caught live when the
# release workflow's Release-mode suite aborted/false-passed). Re-enable
# asserts for everything in this directory; the library under test keeps
# its real configuration flags.
if(NOT MSVC)
    add_compile_options(-UNDEBUG)
endif()

set(LIBTERM_LINK_TARGET libterm_static)
if(NOT LIBTERM_BUILD_STATIC)
    set(LIBTERM_LINK_TARGET libterm_shared)
endif()

# openpty(): in libutil on Linux/BSD; in libSystem on macOS (no libutil there).
if(APPLE)
    set(LIBTERM_PTY_LIBS "")
else()
    set(LIBTERM_PTY_LIBS util)
endif()

function(libterm_add_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE ${LIBTERM_LINK_TARGET})
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

libterm_add_test(test_utf8)
libterm_add_test(test_wcwidth)
libterm_add_test(test_buffer)
libterm_add_test(test_api)
libterm_add_test(test_simd_diff)
libterm_add_test(test_keymap)
libterm_add_test(test_detect_color_depth)
libterm_add_test(test_color_parse)
libterm_add_test(test_color_macros)
# compat/termbox2.h drop-in layer: compile+link proof that every aliased symbol
# resolves. Platform-agnostic (no terminal init), so it runs everywhere.
add_executable(compat_smoke compat_smoke.c)
target_link_libraries(compat_smoke PRIVATE ${LIBTERM_LINK_TARGET})
target_include_directories(compat_smoke PRIVATE
    ${PROJECT_SOURCE_DIR}/compat
    ${PROJECT_SOURCE_DIR}/include)
add_test(NAME compat_smoke COMMAND compat_smoke)

# White-box regression test for examples/editor.c's buffer model (#includes the
# editor TU with main() compiled out). Guards the insert_newline use-after-free.
# POSIX-only: the editor uses getline(), which MinGW does not declare; examples
# themselves are already EXAMPLES=OFF on the Windows CI lanes.
if(NOT WIN32)
    add_executable(test_compat_editor test_compat_editor.c)
    target_link_libraries(test_compat_editor PRIVATE ${LIBTERM_LINK_TARGET})
    target_include_directories(test_compat_editor PRIVATE
        ${PROJECT_SOURCE_DIR}/examples
        ${PROJECT_SOURCE_DIR}/compat
        ${PROJECT_SOURCE_DIR}/include)
    add_test(NAME test_compat_editor COMMAND test_compat_editor)
endif()

# Dispatch builds: tell the SIMD tests which backends were compiled and
# register the dispatcher sanity test. (Static builds: no-op.)
if(LIBTERM_SIMD_BACKENDS)
    set(LIBTERM_SIMD_TEST_DEFS LT_SIMD_DISPATCH)
    foreach(backend IN LISTS LIBTERM_SIMD_BACKENDS)
        string(TOUPPER ${backend} LIBTERM_SIMD_BU)
        list(APPEND LIBTERM_SIMD_TEST_DEFS LT_SIMD_HAVE_${LIBTERM_SIMD_BU})
    endforeach()
    target_compile_definitions(test_simd_diff PRIVATE ${LIBTERM_SIMD_TEST_DEFS})
    libterm_add_test(test_simd_dispatch)
    target_compile_definitions(test_simd_dispatch PRIVATE ${LIBTERM_SIMD_TEST_DEFS})
endif()
if(WIN32)
    libterm_add_test(test_win_sgr_output)
    libterm_add_test(test_win_key)
    libterm_add_test(test_win_mouse)
    libterm_add_test(test_win_color_query)
endif()
if(NOT WIN32)
    libterm_add_test(test_posix_input_parse)
    add_executable(test_posix_input_sgr_run_split test_posix_input_sgr_run_split.c)
    target_include_directories(test_posix_input_sgr_run_split PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/include
    )
    add_test(NAME test_posix_input_sgr_run_split COMMAND test_posix_input_sgr_run_split)
    add_executable(test_pty_lifecycle test_pty_lifecycle.c)
    target_link_libraries(test_pty_lifecycle PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
    target_include_directories(test_pty_lifecycle PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME test_pty_lifecycle COMMAND test_pty_lifecycle)
    set_tests_properties(test_pty_lifecycle PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    # macOS pty-test parity pending (see ROADMAP "Known blockers"): most pty tests
    # hang or fail on macOS runners via several distinct mechanisms (pty capacity
    # in byte-asserting tests; a near-universal shutdown-path hang, suspected
    # tcsetattr(TCSAFLUSH) waiting for the master to drain; sub-50ms timing
    # margins). test_pty_lifecycle stays enabled everywhere as the macOS pty
    # canary (it runs with pty_autodrain and is green there).
    if(NOT APPLE)
        add_executable(test_posix_sgr_output test_posix_sgr_output.c)
        target_link_libraries(test_posix_sgr_output PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_posix_sgr_output PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_posix_sgr_output COMMAND test_posix_sgr_output)
        set_tests_properties(test_posix_sgr_output PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_cursor_visibility test_cursor_visibility.c)
        target_link_libraries(test_cursor_visibility PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        add_test(NAME test_cursor_visibility COMMAND test_cursor_visibility)
        set_tests_properties(test_cursor_visibility PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_posix_input_utf8 test_posix_input_utf8.c)
        target_link_libraries(test_posix_input_utf8 PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_posix_input_utf8 PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_posix_input_utf8 COMMAND test_posix_input_utf8)
        set_tests_properties(test_posix_input_utf8 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_posix_input_frag test_posix_input_frag.c)
        target_link_libraries(test_posix_input_frag PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_posix_input_frag PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_posix_input_frag COMMAND test_posix_input_frag)
        set_tests_properties(test_posix_input_frag PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_print test_print.c)
        target_link_libraries(test_print PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_print PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_print COMMAND test_print)
        set_tests_properties(test_print PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_get_cell test_get_cell.c)
        target_link_libraries(test_get_cell PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_get_cell PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_get_cell COMMAND test_get_cell)
        set_tests_properties(test_get_cell PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_send test_send.c)
        target_link_libraries(test_send PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_send PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_send COMMAND test_send)
        set_tests_properties(test_send PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_invalidate test_invalidate.c)
        target_link_libraries(test_invalidate PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_invalidate PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_invalidate COMMAND test_invalidate)
        set_tests_properties(test_invalidate PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    endif()
    add_executable(test_last_errno test_last_errno.c)
    target_link_libraries(test_last_errno PRIVATE ${LIBTERM_LINK_TARGET})
    target_include_directories(test_last_errno PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME test_last_errno COMMAND test_last_errno)
    set_tests_properties(test_last_errno PROPERTIES SKIP_RETURN_CODE 77)
    # macOS: gated — see the pty-parity comment above.
    if(NOT APPLE)
        add_executable(test_init_file test_init_file.c)
        target_link_libraries(test_init_file PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_init_file PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_init_file COMMAND test_init_file)
        set_tests_properties(test_init_file PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_get_fds test_get_fds.c)
        target_link_libraries(test_get_fds PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_get_fds PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_get_fds COMMAND test_get_fds)
        set_tests_properties(test_get_fds PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_egc test_egc.c)
        target_link_libraries(test_egc PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_egc PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_egc COMMAND test_egc)
        set_tests_properties(test_egc PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_clear_attrs test_clear_attrs.c)
        target_link_libraries(test_clear_attrs PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_clear_attrs PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_clear_attrs COMMAND test_clear_attrs)
        set_tests_properties(test_clear_attrs PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_wide_tail test_wide_tail.c)
        target_link_libraries(test_wide_tail PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_wide_tail PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_wide_tail COMMAND test_wide_tail)
        set_tests_properties(test_wide_tail PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_cursor_bytes test_cursor_bytes.c)
        target_link_libraries(test_cursor_bytes PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_cursor_bytes PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_cursor_bytes COMMAND test_cursor_bytes)
        set_tests_properties(test_cursor_bytes PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_resize_event test_resize_event.c)
        target_link_libraries(test_resize_event PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_resize_event PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_resize_event COMMAND test_resize_event)
        set_tests_properties(test_resize_event PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_release_filter test_release_filter.c)
        target_link_libraries(test_release_filter PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_release_filter PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_release_filter COMMAND test_release_filter)
        set_tests_properties(test_release_filter PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_kitty_negotiation test_kitty_negotiation.c)
        target_link_libraries(test_kitty_negotiation PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_kitty_negotiation PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_kitty_negotiation COMMAND test_kitty_negotiation)
        set_tests_properties(test_kitty_negotiation PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_color_query test_color_query.c)
        target_link_libraries(test_color_query PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_color_query PRIVATE ${PROJECT_SOURCE_DIR}/src)
        add_test(NAME test_color_query COMMAND test_color_query)
        set_tests_properties(test_color_query PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
        add_executable(test_compat_get_cell test_compat_get_cell.c)
        target_link_libraries(test_compat_get_cell PRIVATE ${LIBTERM_LINK_TARGET} ${LIBTERM_PTY_LIBS})
        target_include_directories(test_compat_get_cell PRIVATE
            ${PROJECT_SOURCE_DIR}/compat
            ${PROJECT_SOURCE_DIR}/include)
        add_test(NAME test_compat_get_cell COMMAND test_compat_get_cell)
        set_tests_properties(test_compat_get_cell PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    endif()
endif()

# Assert the selected SIMD backend actually emitted its ISA instructions, so a
# silent scalar fallback cannot pass the correctness contract test. Runs on the
# host (bash + objdump on the per-backend object); not under the cross emulator.
#
# Host-only: the script assumes a Unix toolchain — POSIX `bash`, `objdump`, and
# `<backend>.c.o` object naming. On Windows ctest resolves `bash` to the WSL
# shim (which can't open the C:/ script path) and MinGW emits `<backend>.c.obj`,
# so the assertion is neither runnable nor meaningful there. CI runs it on Linux.
# Linux-only: the script assumes ELF objects and GNU objdump output; Mach-O would false-fail.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND DEFINED LIBTERM_SIMD_RESOLVED)
    if(LIBTERM_SIMD_BACKENDS)
        # Dispatch build: every compiled backend object must emit its ISA.
        foreach(backend IN LISTS LIBTERM_SIMD_BACKENDS)
            add_test(
                NAME test_simd_emitted_${backend}
                COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/assert_simd_emitted.sh
                        ${backend} ${PROJECT_BINARY_DIR})
        endforeach()
    else()
        add_test(
            NAME test_simd_emitted
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/assert_simd_emitted.sh
                    ${LIBTERM_SIMD_RESOLVED} ${PROJECT_BINARY_DIR})
    endif()
endif()

# Assert the shared library actually exports its public API (guards B2: the
# visibility regression where LT_API was empty under -fvisibility=hidden).
# Linux-only: assert_exports.sh uses GNU 'nm -D' (ELF) and bash-4 mapfile; macOS ships nm without -D and bash 3.2.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET libterm_shared)
    add_test(
        NAME test_exports
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/assert_exports.sh
                ${PROJECT_SOURCE_DIR}/include/libterm/libterm.h
                $<TARGET_FILE:libterm_shared>)
endif()

# Tag the pure, non-pty, non-timing tests so the valgrind CI job can select
# them (ctest -L valgrind_safe). The pty/timing tests are excluded: valgrind's
# ~20x slowdown blows their sub-50ms timing margins (esp. test_posix_input_frag).
# Only tests that actually exist in this configuration are labeled.
set(LIBTERM_VALGRIND_SAFE
    test_utf8 test_wcwidth test_buffer test_api test_simd_diff test_keymap
    test_color_parse test_detect_color_depth)
if(NOT WIN32)
    list(APPEND LIBTERM_VALGRIND_SAFE
        test_last_errno test_posix_input_parse test_posix_input_sgr_run_split)
endif()
foreach(t IN LISTS LIBTERM_VALGRIND_SAFE)
    if(TEST ${t})
        set_tests_properties(${t} PROPERTIES LABELS valgrind_safe)
    endif()
endforeach()

# libFuzzer targets (opt-in, clang-only). LIBTERM_LINK_TARGET is already set
# above. Not registered as ctest tests — CI invokes them directly.
if(LIBTERM_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
