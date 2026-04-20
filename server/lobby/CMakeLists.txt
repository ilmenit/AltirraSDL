cmake_minimum_required(VERSION 3.24)

# The lobby server is a small standalone C++ binary.  It doesn't link
# any AltirraSDL code — only the shared protocol headers in
# src/AltirraSDL/source/netplay/ (lobby_protocol.h, json_cursor.h).

set(_lobby_include_dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cpp-httplib
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/netplay)

add_executable(altirra-lobby server.cpp)
target_include_directories(altirra-lobby PRIVATE ${_lobby_include_dirs})
target_compile_features(altirra-lobby PRIVATE cxx_std_23)

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
target_compile_features(altirra-lobby-test PRIVATE cxx_std_23)
target_compile_definitions(altirra-lobby-test PRIVATE
    ALTIRRA_LOBBY_TEST_MAIN=1)
if(NOT WIN32)
    target_link_libraries(altirra-lobby-test PRIVATE Threads::Threads)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(altirra-lobby-test PRIVATE
        -Wno-deprecated-declarations)
endif()

enable_testing()
add_test(NAME altirra-lobby-test COMMAND altirra-lobby-test)
