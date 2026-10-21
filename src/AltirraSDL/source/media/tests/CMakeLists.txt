# AltirraSDL game-metadata self-tests.
#
# Exercises the ScreenScraper response parser and error classifier
# against saved fixtures — no network, so this is safe to run in CI.
# The parser is the most intricate piece of the metadata feature
# (region/language preference chains, provider fields that arrive as
# JSON strings, media type-then-region selection), and it is exactly
# the part that cannot be verified by clicking around the UI.
#
# Gated by ALTIRRA_BUILD_TESTS=ON, like the netplay self-tests.

if(NOT ALTIRRA_BUILD_TESTS)
    return()
endif()

set(_media_root ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(altirra-metadata-parser-test
    ${_media_root}/metadata_screenscraper.cpp
    ${_media_root}/http_client.cpp
    metadata_parser_test.cpp
)

# The minimal stdafx shim the netplay self-tests already generate keeps
# these sources off the project's full precompiled header.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/shim/stdafx.h
    "// Auto-generated minimal stdafx for metadata self-tests.\n"
    "#pragma once\n#include <cstdint>\n#include <cstring>\n#include <cwchar>\n"
    "#include <vd2/system/vdtypes.h>\n")

target_include_directories(altirra-metadata-parser-test PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/shim          # FIRST so the shim wins
    ${_media_root}
    ${_media_root}/../..                       # src/AltirraSDL/source/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../h  # src/h for vd2/*
)

target_compile_features(altirra-metadata-parser-test PRIVATE cxx_std_17)
target_link_libraries(altirra-metadata-parser-test PRIVATE
    vdjson
    system
    ${CMAKE_DL_LIBS}
)

add_test(NAME metadata-parser
    COMMAND altirra-metadata-parser-test)

# Local HTTP fixtures cover transport, redirects, cancellation, cache
# publication and bounded/corrupt archives without external network access.
if(NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(altirra-media-url-test
        ${_media_root}/media_url.cpp
        ${_media_root}/http_client.cpp
        media_url_test.cpp)
    target_include_directories(altirra-media-url-test PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/shim
        ${_media_root})
    target_compile_features(altirra-media-url-test PRIVATE cxx_std_17)
    target_link_libraries(altirra-media-url-test PRIVATE
        ATIO ATCore system Threads::Threads ${CMAKE_DL_LIBS})
    if(WIN32)
        target_link_libraries(altirra-media-url-test PRIVATE winhttp)
    endif()
    add_test(NAME media-url-http
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/media_url_http_test.py
            $<TARGET_FILE:altirra-media-url-test>)
    set_tests_properties(media-url-http PROPERTIES TIMEOUT 60
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()

if(NOT ANDROID AND NOT EMSCRIPTEN)
    add_executable(altirra-fujinet-compat-test fujinet_compat_test.cpp)
    target_include_directories(altirra-fujinet-compat-test PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/shim
        ${_media_root}
        ${PROJECT_SOURCE_DIR}/src/h)
    target_compile_features(altirra-fujinet-compat-test PRIVATE cxx_std_17)
    add_test(NAME fujinet-compatibility COMMAND altirra-fujinet-compat-test)

    add_executable(altirra-fujinet-pc-test
        ${_media_root}/fujinet_pc.cpp
        fujinet_pc_test.cpp)
    target_include_directories(altirra-fujinet-pc-test PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/shim
        ${_media_root}
        ${PROJECT_SOURCE_DIR}/src/h)
    target_compile_features(altirra-fujinet-pc-test PRIVATE cxx_std_17)
    target_link_libraries(altirra-fujinet-pc-test PRIVATE SDL3::SDL3)
    add_test(NAME fujinet-pc-process COMMAND altirra-fujinet-pc-test)
    set_tests_properties(fujinet-pc-process PROPERTIES TIMEOUT 30)
endif()
