cmake_minimum_required(VERSION 3.24)
project(Altirra VERSION 4.40 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

###############################################################################
# Platform detection
#
# Three independent axes:
#   1. Host OS      — WIN32 / APPLE / ANDROID / UNIX (CMake built-ins)
#   2. Frontend     — ALTIRRA_SDL3 option (SDL3+ImGui vs native Win32)
#   3. CPU arch     — CMAKE_SYSTEM_PROCESSOR (x86_64, ARM64, …)
#
# The OS axis selects system-level sources (file I/O, sockets, threading).
# The frontend axis selects the UI layer.  They are orthogonal: Windows+SDL3
# uses Win32 OS APIs but the SDL3+ImGui frontend.
#
# Native Win32 UI is built exclusively via the Visual Studio .sln — CMake
# never produces a native Win32 frontend.
###############################################################################

# ALTIRRA_SDL3 — build the SDL3+ImGui frontend.
# Defaults ON for non-Windows, OFF for Windows (use .sln for native).
# Override with -DALTIRRA_SDL3=ON to get the SDL3 build on any platform.
if(WIN32)
    option(ALTIRRA_SDL3 "Build SDL3+ImGui frontend" OFF)
else()
    option(ALTIRRA_SDL3 "Build SDL3+ImGui frontend" ON)
endif()

# Platform compile definitions — belt-and-suspenders with vdtypes.h auto-detect
if(WIN32)
    add_compile_definitions(VD_OS_WINDOWS=1)
elseif(ANDROID)
    add_compile_definitions(VD_OS_ANDROID=1)
elseif(APPLE)
    add_compile_definitions(VD_OS_MACOS=1)
elseif(UNIX)
    add_compile_definitions(VD_OS_LINUX=1)
endif()

# Match MSVC convention: _DEBUG in debug builds (MSVC defines it automatically)
if(NOT MSVC)
    add_compile_definitions($<$<CONFIG:Debug>:_DEBUG>)
endif()

# Shared include paths
set(ALTIRRA_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src/h)

# Compatibility shims for non-MSVC (intercepts bare <intrin.h>, <tchar.h>)
# MSVC provides these natively; GCC/Clang/NDK need shims.
if(NOT MSVC)
    set(ALTIRRA_COMPAT_DIR ${CMAKE_SOURCE_DIR}/src/compat)
endif()

# SDL3 — required when the SDL3 frontend is enabled, or on non-Windows where
# the system libraries use SDL3 for threading/timing.
if(ALTIRRA_SDL3 OR NOT WIN32)
    find_package(SDL3 REQUIRED)
endif()

# Dear ImGui (docking branch) — built as static library for the SDL3 frontend
if(ALTIRRA_SDL3)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.8-docking
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui)

    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    )
    target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imgui_SOURCE_DIR}/misc/cpp
    )
    target_link_libraries(imgui PUBLIC SDL3::SDL3)
    # Enable test engine hooks — our ui_testmode.cpp provides the hook implementations.
    # PUBLIC so the define propagates to AltirraSDL (which includes imgui_internal.h).
    target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)
    # Suppress warnings in third-party code
    if(MSVC)
        target_compile_options(imgui PRIVATE /w)
    else()
        target_compile_options(imgui PRIVATE -w)
    endif()
endif()

add_subdirectory(src)
