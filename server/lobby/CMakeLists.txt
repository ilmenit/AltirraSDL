cmake_minimum_required(VERSION 3.24)

# When configured standalone (cmake -S server/lobby), we need our
# own project() call.  When included from the top-level AltirraSDL
# build via add_subdirectory(), the parent already owns project()
# and a nested call here would be redundant — but harmless.
if(NOT PROJECT_NAME)
    project(altirra-lobby LANGUAGES C CXX)
else()
    # When included via add_subdirectory(), the parent project() may
    # not have enabled C.  Mongoose ships as C — enable it explicitly
    # so altirra-lobby-mongoose's translation unit compiles.
    enable_language(C)
endif()

# The lobby server is a small standalone C++ binary.  It doesn't link
# any AltirraSDL code — only the shared protocol headers in
# src/AltirraSDL/source/netplay/ (lobby_protocol.h, json_cursor.h).

# Shared protocol headers live two levels up in the AltirraSDL tree.
# Resolve relative to this file so the directory works both when
# included from the top-level AltirraSDL build and when configured
# standalone (e.g. from the deploy repo's CI).
get_filename_component(_lobby_altirra_root
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(_lobby_include_dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cpp-httplib
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_lobby_altirra_root}/src/AltirraSDL/source/netplay)

# Mongoose ships as a single-translation-unit C library
# (mongoose.c + mongoose.h).  We compile it here as a small static
# library so its compiler flags (no warning-as-error, C99) don't bleed
# into the rest of the lobby build.  It powers the WS bridge that
# accepts browser (WSS) connections; the existing cpp-httplib server
# on :8080 stays untouched.
add_library(altirra-lobby-mongoose STATIC
    vendor/mongoose/mongoose.c)
target_include_directories(altirra-lobby-mongoose PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/mongoose)
# Caddy terminates TLS on the public edge; we never need mongoose's
# built-in TLS.  Disabling MG_TLS pulls fewer headers and shaves the
# binary by a few hundred KB.
target_compile_definitions(altirra-lobby-mongoose PUBLIC
    MG_TLS=MG_TLS_NONE
    MG_ENABLE_OPENSSL=0
    MG_ENABLE_MBEDTLS=0)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(altirra-lobby-mongoose PRIVATE
        -Wno-unused-function -Wno-unused-parameter
        -Wno-format-truncation -Wno-stringop-truncation)
endif()

add_executable(altirra-lobby
    server.cpp
    ws_bridge.cpp)
target_include_directories(altirra-lobby PRIVATE ${_lobby_include_dirs})
target_compile_features(altirra-lobby PRIVATE cxx_std_20)
target_link_libraries(altirra-lobby PRIVATE altirra-lobby-mongoose)

# cpp-httplib requires threads on all platforms; Winsock on Windows
# is pulled in by its own headers.
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(altirra-lobby PRIVATE Threads::Threads)
endif()

# Silence the few -Wdeprecated noises cpp-httplib raises on modern
# GCC/Clang — it's a vendored third-party dep and we don't maintain
# its warning-free state.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(altirra-lobby PRIVATE
        -Wno-deprecated-declarations)
endif()

# ----------------------------------------------------------------------
# Unit tests — a single executable that exercises Store / Validate /
# HTTP routes against an in-process server.  Build by default so it
# runs under `ctest` if the user enables testing at the top level.
# ----------------------------------------------------------------------
add_executable(altirra-lobby-test server_test.cpp)
target_include_directories(altirra-lobby-test PRIVATE
    ${_lobby_include_dirs}
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(altirra-lobby-test PRIVATE cxx_std_20)
target_compile_definitions(altirra-lobby-test PRIVATE
    ALTIRRA_LOBBY_TEST_MAIN=1)
if(NOT WIN32)
    target_link_libraries(altirra-lobby-test PRIVATE Threads::Threads)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(altirra-lobby-test PRIVATE
        -Wno-deprecated-declarations)
endif()

# WS bridge integration tests.  Spawns the bridge in-process with stub
# callbacks (no Store/RelayTable dependency) and drives it from a
# mongoose WS client running in the same process.
add_executable(altirra-lobby-ws-test
    server_ws_test.cpp
    ws_bridge.cpp)
target_include_directories(altirra-lobby-ws-test PRIVATE
    ${_lobby_include_dirs})
target_compile_features(altirra-lobby-ws-test PRIVATE cxx_std_20)
target_link_libraries(altirra-lobby-ws-test PRIVATE altirra-lobby-mongoose)
if(NOT WIN32)
    target_link_libraries(altirra-lobby-ws-test PRIVATE Threads::Threads)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(altirra-lobby-ws-test PRIVATE
        -Wno-deprecated-declarations)
endif()

enable_testing()
add_test(NAME altirra-lobby-test    COMMAND altirra-lobby-test)
add_test(NAME altirra-lobby-ws-test COMMAND altirra-lobby-ws-test)
