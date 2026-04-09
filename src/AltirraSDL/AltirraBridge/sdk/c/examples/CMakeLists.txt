# Standalone CMake build for the AltirraBridge C SDK examples.
#
# This file does NOT depend on the parent AltirraSDL CMake build.
# It builds against just the SDK header + .c file in the parent
# directory, so users who unpack a release package and want to write
# C clients can build the examples in isolation:
#
#   cd AltirraBridge/sdk/c/examples
#   mkdir build && cd build
#   cmake ..
#   cmake --build .

cmake_minimum_required(VERSION 3.16)
project(altirra_bridge_examples C)

set(CMAKE_C_STANDARD 99)

# --- libc-only examples ---
# 01..03 build with no external dependencies. They demonstrate the
# core bridge protocol against the C SDK header alone.
set(BRIDGE_EXAMPLES_PLAIN
    01_ping
    02_peek_regs
    03_input
)

foreach(name ${BRIDGE_EXAMPLES_PLAIN})
    add_executable(${name} ${name}.c ../altirra_bridge.c)
    target_include_directories(${name} PRIVATE ..)
    if(WIN32)
        target_link_libraries(${name} PRIVATE ws2_32)
    endif()
endforeach()

# --- 04_paint requires SDL3 ---
# The C SDK itself is libc-only by design, but the interactive
# paint example needs a window + event loop. SDL3 is the project's
# canonical UI layer and ships in every AltirraBridge release.
#
# We try the standard CMake config search first; if SDL3 is not
# found, we print a status and skip 04_paint instead of failing —
# users without SDL3 still get the other three examples.
find_package(SDL3 CONFIG QUIET)

if(SDL3_FOUND)
    add_executable(04_paint 04_paint.c ../altirra_bridge.c)
    target_include_directories(04_paint PRIVATE ..)
    target_link_libraries(04_paint PRIVATE SDL3::SDL3)
    if(WIN32)
        target_link_libraries(04_paint PRIVATE ws2_32)
    endif()
    # Look for libSDL3 / SDL3.dll next to the executable so users
    # can drop both into the same directory and run without
    # installing SDL3 system-wide. Falls back to the system loader
    # if the bundled lib is missing.
    if(APPLE)
        set_target_properties(04_paint PROPERTIES
            BUILD_RPATH   "@loader_path"
            INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set_target_properties(04_paint PROPERTIES
            BUILD_RPATH   "\$ORIGIN"
            INSTALL_RPATH "\$ORIGIN")
    endif()
    message(STATUS "AltirraBridge examples: 04_paint enabled (SDL3 ${SDL3_VERSION})")

    # Copy g2f.act next to 04_paint at build time so the example can
    # find it via its argv[0]-relative search path. The .act ships
    # in the SDK assets directory (../../assets relative to this
    # CMakeLists). If the asset isn't present (e.g. user stripped
    # it from a custom build tree) we log a note but don't fail.
    set(_ACT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/g2f.act")
    if(EXISTS "${_ACT_SRC}")
        add_custom_command(TARGET 04_paint POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_ACT_SRC}"
                "$<TARGET_FILE_DIR:04_paint>/g2f.act"
            VERBATIM)
        message(STATUS "AltirraBridge examples: g2f.act will be copied next to 04_paint")
    else()
        message(STATUS
            "AltirraBridge examples: g2f.act not found at ${_ACT_SRC}, "
            "04_paint will run with the default Altirra palette.")
    endif()
else()
    message(STATUS
        "AltirraBridge examples: SDL3 not found, skipping 04_paint. "
        "Install SDL3 or pass -DCMAKE_PREFIX_PATH=<sdl3-build-dir> to build it.")
endif()
