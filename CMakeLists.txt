cmake_minimum_required(VERSION 3.24)
project(Altirra VERSION 4.40 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enforce 64-bit build on Windows MSVC.
# The native Windows x86 build uses the VS .sln with MASM assembly.
# The CMake portable build uses C++ reference/intrinsics implementations
# which work cleanly on x64 (matching Linux/Android behavior), but x86
# activates MASM-dependent code paths via _M_IX86/VD_CPU_X86 that require
# assembly files only the VS .sln provides.
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "The CMake portable build requires a 64-bit (x64) target on Windows.\n"
        "Use: cmake -A x64 ..\n"
        "For 32-bit x86, use the native Visual Studio .sln build instead.")
endif()

# AT_SDL3_PORTABLE — marks this as the portable SDL3 build.
# The native Windows build (VS .sln) never sees this define.
# Code uses this to select portable paths: C++ reference/intrinsics instead
# of MASM assembly, template thunks instead of dynamic JIT thunks, etc.
add_compile_definitions(AT_SDL3_PORTABLE=1)

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
# Always ON: CMake exclusively builds the SDL3+ImGui frontend.
# The native Win32 UI is built via the Visual Studio .sln, never via CMake.
option(ALTIRRA_SDL3 "Build SDL3+ImGui frontend" ON)

# Platform compile definitions — belt-and-suspenders with vdtypes.h auto-detect
if(WIN32)
    add_compile_definitions(VD_OS_WINDOWS=1)
    # Prevent windows.h from defining min/max macros that break std::min/std::max.
    # The .sln build doesn't need this because its code paths avoid the conflict;
    # the CMake/SDL3 build compiles system library code alongside Windows headers
    # where std::min/std::max are used.
    add_compile_definitions(NOMINMAX)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
    # The codebase uses wchar_t Win32 APIs throughout (VDStringW).
    # The .sln sets UNICODE/_UNICODE so CreateFile → CreateFileW etc.
    add_compile_definitions(UNICODE)
    add_compile_definitions(_UNICODE)
    # Enable the conformant C preprocessor (/Zc:preprocessor).  The codebase
    # uses __VA_OPT__ (e.g. in console.h's ATConsolePrintf macro), which MSVC's
    # legacy preprocessor does not support.  This flag is safe for all C/C++ code
    # including FetchContent dependencies — it only makes the preprocessor
    # standards-compliant.
    add_compile_options(/Zc:preprocessor)
    # POSIX string functions used by AltirraSDL code — MSVC spells them with
    # a leading underscore.  Define as macros so the same source compiles on
    # both Windows and Linux/macOS without #ifdefs everywhere.
    add_compile_definitions(strcasecmp=_stricmp)
    add_compile_definitions(strncasecmp=_strnicmp)
    # POSIX/GNU string functions with no MSVC equivalent (e.g. strcasestr).
    # Applied per-target (not globally) to avoid leaking into FetchContent
    # dependencies like SDL3 which compile as C, not C++.
    set(ALTIRRA_POSIX_COMPAT_HEADER "${CMAKE_SOURCE_DIR}/src/compat/posix_string.h")
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
    if(ANDROID)
        # On Android, SDL3 must be built from source alongside the project.
        # The android/setup_sdl3.sh script clones SDL3 into android/SDL3/.
        # We add it as a subdirectory so it builds as a shared library
        # that the Android activity loads.
        set(SDL3_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android/SDL3")
        if(EXISTS "${SDL3_SOURCE_DIR}/CMakeLists.txt")
            add_subdirectory("${SDL3_SOURCE_DIR}" SDL3)
        else()
            message(FATAL_ERROR
                "SDL3 source not found at ${SDL3_SOURCE_DIR}\n"
                "Run: cd android && ./setup_sdl3.sh")
        endif()
    else()
        # ── SDL3 dependency resolution ──────────────────────────────────
        # Strategy: prefer a system/vcpkg-installed SDL3 for faster builds;
        # if none is found, automatically fetch and build from source so
        # the project works out-of-the-box on Windows, macOS, and Linux.
        #
        # To force a specific SDL3, set SDL3_DIR or CMAKE_PREFIX_PATH:
        #   cmake --preset <preset> -DSDL3_DIR=/path/to/sdl3/cmake
        # ────────────────────────────────────────────────────────────────

        find_package(SDL3 QUIET)

        if(SDL3_FOUND)
            # ── System SDL3 found ───────────────────────────────────────
            get_target_property(_sdl3_loc SDL3::SDL3 LOCATION)
            message(STATUS "")
            message(STATUS "  ┌─ SDL3 ─────────────────────────────────────")
            message(STATUS "  │ Using system-installed SDL3")
            message(STATUS "  │ Config:  ${SDL3_DIR}")
            if(_sdl3_loc)
                message(STATUS "  │ Library: ${_sdl3_loc}")
            endif()
            message(STATUS "  └────────────────────────────────────────────")
            message(STATUS "")
        else()
            # ── Auto-fetch SDL3 from source ─────────────────────────────
            set(ALTIRRA_SDL3_VERSION "release-3.2.8")
            message(STATUS "")
            message(STATUS "  ┌─ SDL3 ─────────────────────────────────────")
            message(STATUS "  │ No system SDL3 found.")
            message(STATUS "  │ Fetching SDL3 ${ALTIRRA_SDL3_VERSION} from GitHub...")
            message(STATUS "  │ (first build downloads ~30 MB; subsequent builds use cache)")
            message(STATUS "  │")
            message(STATUS "  │ To skip this, install SDL3 and set SDL3_DIR or")
            message(STATUS "  │ CMAKE_PREFIX_PATH to point to its cmake config.")
            message(STATUS "  └────────────────────────────────────────────")
            message(STATUS "")

            FetchContent_Declare(
                sdl3
                GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                GIT_TAG        ${ALTIRRA_SDL3_VERSION}
                GIT_SHALLOW    TRUE
                GIT_PROGRESS   TRUE
            )

            # Build as shared library (matches system-package behavior and
            # avoids static-linking license complications with LGPL).
            set(SDL_SHARED ON  CACHE BOOL "Build SDL3 shared library" FORCE)
            set(SDL_STATIC OFF CACHE BOOL "Build SDL3 static library" FORCE)

            # Disable SDL3 extras we don't need — speeds up the build.
            set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL3 test library" FORCE)
            set(SDL_TESTS        OFF CACHE BOOL "Build SDL3 tests" FORCE)
            set(SDL_EXAMPLES     OFF CACHE BOOL "Build SDL3 examples" FORCE)
            set(SDL_DISABLE_INSTALL ON CACHE BOOL "Skip SDL3 install rules" FORCE)

            FetchContent_MakeAvailable(sdl3)

            # Verify the target was created — a clear error is better than
            # cryptic link failures later.
            if(NOT TARGET SDL3::SDL3)
                message(FATAL_ERROR
                    "SDL3 FetchContent completed but SDL3::SDL3 target not found.\n"
                    "This is unexpected — please check the CMake output above for errors.")
            endif()

            message(STATUS "  SDL3 ${ALTIRRA_SDL3_VERSION} ready (built from source)")
        endif()
    endif()
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

    # librashader C headers — fetched from the librashader repo.
    # Only the include/ directory is needed (two C headers); the rest
    # of the repo (Rust source) is ignored.  The actual shared library
    # (librashader.so/.dll/.dylib) is loaded at runtime via dlopen —
    # no link-time dependency.
    option(ENABLE_LIBRASHADER "Enable librashader shader preset support" ON)
    if(ENABLE_LIBRASHADER)
        FetchContent_Declare(
            librashader_headers
            GIT_REPOSITORY https://github.com/SnowflakePowered/librashader.git
            GIT_TAG        librashader-v0.10.1
            GIT_SHALLOW    TRUE
            # SOURCE_SUBDIR points to a dir with no CMakeLists.txt, so
            # FetchContent_MakeAvailable skips add_subdirectory (we only
            # need the C headers from include/, not the Rust build).
            SOURCE_SUBDIR  include
        )
        FetchContent_MakeAvailable(librashader_headers)
        # Copy the two C headers into vendor/librashader/ so
        # __has_include("../vendor/librashader/librashader_ld.h") finds them.
        # file(COPY) is used instead of symlinks for Windows compatibility.
        set(LIBRASHADER_VENDOR_DIR "${CMAKE_SOURCE_DIR}/src/AltirraSDL/vendor/librashader")
        file(MAKE_DIRECTORY "${LIBRASHADER_VENDOR_DIR}")
        file(COPY
            "${librashader_headers_SOURCE_DIR}/include/librashader.h"
            "${librashader_headers_SOURCE_DIR}/include/librashader_ld.h"
            DESTINATION "${LIBRASHADER_VENDOR_DIR}"
        )

        # Patch librashader_ld.h on Linux: replace the simple dlopen() call
        # with a search function that tries the executable's directory first
        # (for --librashader builds), then versioned sonames (for distros
        # that don't install an unversioned symlink or ship a broken one).
        if(UNIX AND NOT APPLE)
            set(_LIBRA_LD_H "${LIBRASHADER_VENDOR_DIR}/librashader_ld.h")
            file(READ "${_LIBRA_LD_H}" _LIBRA_LD_CONTENT)
            string(REPLACE
                "#define _LIBRASHADER_LOAD dlopen(\"librashader.so\", RTLD_LAZY)"
                [=[
// Patched by AltirraSDL CMakeLists.txt — search exe dir + versioned sonames.
#include <linux/limits.h>
#include <unistd.h>
#include <string.h>
static inline void *_librashader_dlopen_linux(void) {
    // 1. Try loading from the executable's own directory
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        char *slash = strrchr(exe_path, '/');
        if (slash) {
            static const char *local_names[] = {
                "librashader.so", "librashader.so.2", "librashader.so.1",
            };
            for (size_t i = 0; i < sizeof(local_names)/sizeof(local_names[0]); i++) {
                if ((size_t)(slash - exe_path + 1) + strlen(local_names[i]) < PATH_MAX) {
                    strcpy(slash + 1, local_names[i]);
                    void *h = dlopen(exe_path, RTLD_LAZY);
                    if (h) return h;
                }
            }
        }
    }
    // 2. Try system paths with versioned sonames
    static const char *sys_names[] = {
        "librashader.so.2", "librashader.so.1", "librashader.so",
    };
    for (size_t i = 0; i < sizeof(sys_names)/sizeof(sys_names[0]); i++) {
        void *h = dlopen(sys_names[i], RTLD_LAZY);
        if (h) return h;
    }
    return NULL;
}
#define _LIBRASHADER_LOAD _librashader_dlopen_linux()
]=]
                _LIBRA_LD_CONTENT "${_LIBRA_LD_CONTENT}")
            file(WRITE "${_LIBRA_LD_H}" "${_LIBRA_LD_CONTENT}")
        endif()

        message(STATUS "librashader headers: ${librashader_headers_SOURCE_DIR}/include")
    endif()

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
