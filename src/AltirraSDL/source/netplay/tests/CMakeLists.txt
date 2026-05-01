# AltirraSDL netplay self-tests.
#
# Each selftest is a small standalone C++ executable that compiles a
# subset of the netplay source directly — no linkage against the main
# AltirraSDL binary or its vd2/system pre-compiled headers.  This mirrors
# the manual build commands documented at the top of each *_selftest.cpp
# file.
#
# Gated by ALTIRRA_BUILD_TESTS=ON so default builds are unaffected.
# Wire each test into ctest so `ctest --test-dir build` runs them all.

if(NOT ALTIRRA_BUILD_TESTS)
    return()
endif()

# Generate the minimal stdafx.h shim at config time.  The netplay
# sources `#include <stdafx.h>` first; the real stdafx.h pulls in the
# whole vd2 type system, but for these standalone tests we only need
# the `uint32` typedef referenced by <at/atcore/logging.h>.  Putting
# the shim under ${CMAKE_CURRENT_BINARY_DIR} and adding that as the
# FIRST include directory below makes the netplay sources see this
# minimal shim instead of the project's stdafx.h.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/shim/stdafx.h "// Auto-generated minimal stdafx for netplay selftests.\n#pragma once\n#include <cstdint>\n#include <cstring>\n#include <cwchar>\ntypedef uint32_t uint32;\n")

set(_np_root ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(_np_includes
    ${CMAKE_CURRENT_BINARY_DIR}/shim     # FIRST so our minimal stdafx wins
    ${_np_root}                           # netplay sources
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../h)    # src/h for at/atcore/*

# Threads needed by transport_selftest (UDP loopback) and the lobby
# tests (cpp-httplib backed by std::thread).
find_package(Threads REQUIRED)

# Helper that wires a single selftest exe + ctest registration.
# Usage: altirra_netplay_selftest(NAME proto SOURCES protocol.cpp protocol_selftest.cpp)
function(altirra_netplay_selftest)
    cmake_parse_arguments(NS "" "NAME" "SOURCES" ${ARGN})
    set(_target altirra-netplay-${NS_NAME}-test)
    set(_full_sources)
    foreach(_s ${NS_SOURCES})
        list(APPEND _full_sources ${_np_root}/${_s})
    endforeach()
    add_executable(${_target} ${_full_sources})
    target_include_directories(${_target} PRIVATE ${_np_includes})
    target_compile_features(${_target} PRIVATE cxx_std_17)
    target_link_libraries(${_target} PRIVATE Threads::Threads)
    # Suppress noise that's not useful in a test build (some sources
    # have unused-parameter warnings under the more-restrictive flags
    # the main project uses).
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${_target} PRIVATE
            -Wno-unused-parameter)
    endif()
    add_test(NAME ${_target} COMMAND ${_target})
endfunction()

altirra_netplay_selftest(
    NAME protocol
    SOURCES protocol.cpp protocol_selftest.cpp)

altirra_netplay_selftest(
    NAME snapshot
    SOURCES snapshot_channel.cpp snapshot_channel_selftest.cpp)

altirra_netplay_selftest(
    NAME lockstep
    SOURCES lockstep.cpp lockstep_selftest.cpp)

altirra_netplay_selftest(
    NAME transport
    SOURCES transport.cpp transport_selftest.cpp)

altirra_netplay_selftest(
    NAME lobby-client
    SOURCES http_minimal.cpp lobby_client.cpp lobby_client_selftest.cpp)

altirra_netplay_selftest(
    NAME lobby-config
    SOURCES lobby_config.cpp lan_discovery.cpp lobby_config_selftest.cpp)

# Item 6b regression: end-to-end coordinator selftest, including the
# new LossyTransport-based 30%-loss snapshot regression and the
# stalled-host watchdog regression.  Has more transitive deps than
# the simpler tests above:
#   - ATCore logging.cpp for ATLogChannel symbols.
#   - coordinator_test_stubs.cpp for the ATEmoteNetplay::OnReceivedFromPeer
#     symbol (live emote dispatch lives in the UI module which the
#     standalone test doesn't link).
add_executable(altirra-netplay-coordinator-test
    ${_np_root}/protocol.cpp
    ${_np_root}/transport.cpp
    ${_np_root}/snapshot_channel.cpp
    ${_np_root}/lockstep.cpp
    ${_np_root}/coordinator.cpp
    ${_np_root}/coordinator_selftest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/coordinator_test_stubs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ATCore/source/logging.cpp)
target_include_directories(altirra-netplay-coordinator-test PRIVATE
    ${_np_includes})
target_compile_features(altirra-netplay-coordinator-test PRIVATE cxx_std_17)
target_link_libraries(altirra-netplay-coordinator-test PRIVATE Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(altirra-netplay-coordinator-test PRIVATE
        -Wno-unused-parameter)
endif()
add_test(NAME altirra-netplay-coordinator-test
    COMMAND altirra-netplay-coordinator-test)
# The LossyTransport stalled-host regression deliberately waits up
# to 40 s for the 30-s ReceivingSnapshot watchdog to fire (Item 1).
# Default ctest timeout is 1500 s so this is fine, but bump
# explicitly so a future tightening of the ctest default doesn't
# surprise this test, and pad enough for slow CI runners.
set_tests_properties(altirra-netplay-coordinator-test PROPERTIES
    TIMEOUT 120)
