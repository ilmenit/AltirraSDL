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

# ALTIRRA_FETCH_SDL3 — force-fetch SDL3 from source instead of looking
# for a system install.  Use this when find_package(SDL3) picks up a
# broken or incompatible system SDL3 (e.g. an old SDL3 lying around in
# /usr/local from a previous source build).  Default OFF: prefer system
# SDL3 when available, fall back to FetchContent when it isn't.
option(ALTIRRA_FETCH_SDL3 "Always fetch SDL3 from source (skip find_package)" OFF)

# ALTIRRA_STATIC_SDL3 — link SDL3 + SDL3_image statically into the
# AltirraSDL binary.  Default ON for desktop (Linux/macOS/Windows): the
# resulting executable is self-contained (no sidecar libSDL3.so / .dll /
# .dylib next to it).  Forced OFF on Android because Java SDLActivity
# calls System.loadLibrary("SDL3") at startup and requires the .so to
# exist as a separate file in the APK's lib/<abi>/ directory.
#
# Static-linking implies fetching SDL3/SDL3_image from source — a system
# SDL3 package typically ships only the shared library, so we set
# ALTIRRA_FETCH_SDL3 automatically whenever ALTIRRA_STATIC_SDL3 is ON.
if(ANDROID)
    option(ALTIRRA_STATIC_SDL3 "Link SDL3 and SDL3_image statically into AltirraSDL" OFF)
    if(ALTIRRA_STATIC_SDL3)
        message(WARNING
            "ALTIRRA_STATIC_SDL3 is not supported on Android (Java "
            "SDLActivity requires libSDL3.so to exist separately in "
            "the APK).  Forcing OFF.")
        set(ALTIRRA_STATIC_SDL3 OFF CACHE BOOL "" FORCE)
    endif()
else()
    option(ALTIRRA_STATIC_SDL3 "Link SDL3 and SDL3_image statically into AltirraSDL" ON)
endif()

# Static-linking implies FetchContent — system SDL3 typically ships only
# the shared library, so we can't honour a request for static with
# find_package.
if(ALTIRRA_STATIC_SDL3 AND NOT ALTIRRA_FETCH_SDL3)
    message(STATUS "ALTIRRA_STATIC_SDL3=ON implies ALTIRRA_FETCH_SDL3=ON")
    set(ALTIRRA_FETCH_SDL3 ON CACHE BOOL "" FORCE)
endif()

# ALTIRRA_SKIP_SDL3_IMAGE — build without SDL3_image entirely.  Use this
# as an escape hatch when SDL3_image is unavailable, broken, or you just
# want a smaller build.  Image load/save (screenshots, loading art tapes,
# any UI that decodes PNG/JPEG at runtime) will be unavailable and the
# corresponding entry points throw a "not supported" error, but the rest
# of the emulator builds and runs normally.  Default OFF: SDL3_image is
# required so feature parity with the Windows build is the default.
option(ALTIRRA_SKIP_SDL3_IMAGE "Build without SDL3_image (disables image load/save)" OFF)

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

# Suppress two large warning categories that are intentional in the codebase
# and unfixable without churning cross-platform headers:
#   -Wmultichar: FourCC tags like 'ATR ', 'disk' used as interface/category IDs
#                throughout at/atcore/device.h and friends.
#   -Wattributes: MSVC-specific [[msvc::no_unique_address]] in vdalloc.h and
#                 'packed' on templated fields in vd2/system/binary.h — GCC/Clang
#                 correctly ignore these; MSVC consumes them.
# Together these account for ~99.8% of warnings on GCC/Clang builds.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wno-multichar -Wno-attributes)
endif()

# Mainline GCC on aarch64 is the strictest of the three toolchains we
# support for NEON intrinsics. Distinct opaque typedefs per element
# type/width with no implicit conversion between them — whereas MSVC
# (ARM64) treats all 128-bit NEON vectors as essentially interchangeable,
# and Apple clang's arm_neon.h marks the vector typedefs __may_alias__
# so conversions are implicit there too.
#
# Every NEON source file in this tree — src/ATCore, src/ATIO, src/Kasumi,
# src/Altirra, and the vecmath_neon.h header — contains some amount of
# idiomatic vector reinterpretation (bit patterns shared between signed
# and unsigned lanes, element-width shuffling, etc.) that MSVC and Apple
# clang accept implicitly but mainline GCC rejects without
# -flax-vector-conversions.
#
# Rather than annotating every NEON file individually (and adding every
# new NEON file to a list forever after), enable the relaxation globally
# for GNU on aarch64. This flag ONLY affects SIMD vector-type
# conversions; it's a no-op on scalar code and on every other file in
# the project, so it doesn't hide any non-SIMD type bugs.
#
# Scope is deliberately narrow: GNU only (clang/Apple clang don't need
# it), aarch64 only (x86_64 NEON doesn't exist), and not MSVC (which
# has its own rules and doesn't read these CMake flags anyway).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|ARM64|arm64)")
    add_compile_options(-flax-vector-conversions)
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

        if(ALTIRRA_FETCH_SDL3)
            message(STATUS "ALTIRRA_FETCH_SDL3=ON — skipping find_package(SDL3)")
            set(SDL3_FOUND FALSE)
        else()
            find_package(SDL3 QUIET)
        endif()

        # Track how SDL3 was resolved so downstream dependencies
        # (SDL3_image) can avoid mixing a FetchContent SDL3 with a
        # system SDL3_image that transitively links the system SDL3 —
        # a mismatch that trips CMake with "INTERFACE_SDL3_SHARED
        # property does not agree".
        set(ALTIRRA_SDL3_FETCHED FALSE)

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
            # On macOS, a system-installed SDL3 (e.g. Homebrew) has an
            # absolute install_name like /opt/homebrew/opt/sdl3/lib/
            # libSDL3.0.dylib.  That's fine for local dev — the binary
            # runs directly out of the build tree — but the resulting
            # .app bundle is NOT self-contained and may fail to launch
            # on another Mac.  ./build.sh --package forces FetchContent
            # SDL3 automatically (see BUILD.md "macOS Distribution").
            if(APPLE)
                message(STATUS "  │")
                message(STATUS "  │ NOTE: OK for local dev, but the resulting .app is")
                message(STATUS "  │       NOT redistributable.  For a redistributable")
                message(STATUS "  │       bundle + DMG run:  ./build.sh --package")
                message(STATUS "  │       (forces FetchContent SDL3 with @rpath names)")
            endif()
            message(STATUS "  └────────────────────────────────────────────")
            message(STATUS "")
        else()
            # ── Auto-fetch SDL3 from source ─────────────────────────────
            set(ALTIRRA_SDL3_VERSION "release-3.2.30")
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

            # SDL3 is zlib-licensed so either shared or static is legally fine.
            # Static (default): linked directly into AltirraSDL, no sidecar
            #     libSDL3.so/.dll/.dylib next to the binary.  On Linux SDL3
            #     still dlopens X11/Wayland/PulseAudio/ALSA/libdecor at
            #     runtime (unavoidable — those are the display/audio
            #     backends), but those libraries are always present on a
            #     Linux desktop, so the binary itself is self-contained.
            # Shared: kept available behind -DALTIRRA_STATIC_SDL3=OFF for
            #     users who want to drop in a newer SDL3 without rebuilding.
            if(ALTIRRA_STATIC_SDL3)
                set(SDL_SHARED OFF CACHE BOOL "Build SDL3 shared library" FORCE)
                set(SDL_STATIC ON  CACHE BOOL "Build SDL3 static library" FORCE)
            else()
                set(SDL_SHARED ON  CACHE BOOL "Build SDL3 shared library" FORCE)
                set(SDL_STATIC OFF CACHE BOOL "Build SDL3 static library" FORCE)
            endif()

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
            set(ALTIRRA_SDL3_FETCHED TRUE)
        endif()
    endif()
endif()

# Dear ImGui (docking branch) — built as static library for the SDL3 frontend
#
# By default we FetchContent the upstream sources pinned to a known-good tag.
# Distro packagers who need to avoid network access during build (Fedora,
# Debian, etc.) can instead point ALTIRRA_IMGUI_SOURCE_DIR at an existing
# ImGui source tree (e.g. /usr/include/imgui on Fedora's imgui-devel).  We
# always compile ImGui ourselves from source — linking a pre-built distro
# libimgui.so is intentionally unsupported, because Altirra requires
# IMGUI_ENABLE_TEST_ENGINE (which changes ImGuiContext layout) and distro
# packages do not set that define; mixing would cause silent ODR / memory
# corruption at runtime.
set(ALTIRRA_IMGUI_SOURCE_DIR "" CACHE PATH "Path to an external Dear ImGui source tree (must contain imgui.cpp, backends/, misc/cpp/). If empty, ImGui is fetched via FetchContent.")

if(ALTIRRA_SDL3)
    if(ALTIRRA_IMGUI_SOURCE_DIR)
        # Validate the external tree has everything we compile below.  A clear
        # up-front error is better than a cryptic missing-file during build.
        foreach(_req
                imgui.cpp
                imgui_draw.cpp
                imgui_tables.cpp
                imgui_widgets.cpp
                imgui_demo.cpp
                imgui.h
                imgui_internal.h
                backends/imgui_impl_sdl3.cpp
                backends/imgui_impl_sdlrenderer3.cpp
                backends/imgui_impl_opengl3.cpp
                misc/cpp/imgui_stdlib.cpp)
            if(NOT EXISTS "${ALTIRRA_IMGUI_SOURCE_DIR}/${_req}")
                message(FATAL_ERROR
                    "ALTIRRA_IMGUI_SOURCE_DIR='${ALTIRRA_IMGUI_SOURCE_DIR}' is "
                    "missing required file '${_req}'.  The directory must "
                    "contain the full upstream Dear ImGui source layout "
                    "(core sources, backends/, misc/cpp/).")
            endif()
        endforeach()
        set(imgui_SOURCE_DIR "${ALTIRRA_IMGUI_SOURCE_DIR}")
        message(STATUS "Dear ImGui: using external source at ${imgui_SOURCE_DIR}")
    else()
        FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG        v1.91.8-docking
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(imgui)
    endif()

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

        # Patch librashader_ld.h on Linux AND macOS: replace the simple
        # dlopen() call with a search function that tries the executable's
        # own directory first (for --librashader builds where the .so /
        # .dylib is bundled next to the binary), then versioned sonames
        # (Linux) / @executable_path / system paths (macOS).
        #
        # Without this patch, dlopen() with a bare filename does NOT look
        # next to the running binary on either Linux or macOS — it only
        # consults LD_LIBRARY_PATH / DYLD_*_PATH and the system search
        # path, so a sibling library inside the AppImage or .app bundle
        # is invisible to it.
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

        if(APPLE)
            set(_LIBRA_LD_H "${LIBRASHADER_VENDOR_DIR}/librashader_ld.h")
            file(READ "${_LIBRA_LD_H}" _LIBRA_LD_CONTENT)
            string(REPLACE
                "#define _LIBRASHADER_LOAD dlopen(\"librashader.dylib\", RTLD_LAZY)"
                [=[
// Patched by AltirraSDL CMakeLists.txt — search exe dir + @executable_path
// + system paths.  Upstream just does dlopen("librashader.dylib", ...),
// but on macOS that does NOT look next to the running binary; it only
// consults DYLD_LIBRARY_PATH / DYLD_FALLBACK_LIBRARY_PATH (defaults to
// $HOME/lib:/usr/local/lib:/usr/lib), so a sibling dylib inside
// AltirraSDL.app/Contents/MacOS/ is invisible to it.  Mirror the Linux
// patch above using _NSGetExecutablePath() to walk the running binary's
// own directory first.
#include <mach-o/dyld.h>
#include <sys/syslimits.h>
#include <string.h>
#include <stdint.h>
static inline void *_librashader_dlopen_macos(void) {
    // 1. Try loading from the running executable's own directory.
    char exe_path[PATH_MAX];
    uint32_t size = (uint32_t)sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        char *slash = strrchr(exe_path, '/');
        if (slash) {
            static const char *local_names[] = {
                "librashader.dylib",
                "librashader.2.dylib",
                "librashader.1.dylib",
            };
            for (size_t i = 0;
                 i < sizeof(local_names)/sizeof(local_names[0]);
                 i++) {
                if ((size_t)(slash - exe_path + 1)
                        + strlen(local_names[i]) < PATH_MAX) {
                    strcpy(slash + 1, local_names[i]);
                    void *h = dlopen(exe_path, RTLD_LAZY);
                    if (h) return h;
                }
            }
        }
    }
    // 2. Try @executable_path / @loader_path — dyld resolves these
    //    natively to the running binary's directory and the directory
    //    of whatever Mach-O is calling dlopen() respectively.
    {
        static const char *at_names[] = {
            "@executable_path/librashader.dylib",
            "@loader_path/librashader.dylib",
        };
        for (size_t i = 0;
             i < sizeof(at_names)/sizeof(at_names[0]);
             i++) {
            void *h = dlopen(at_names[i], RTLD_LAZY);
            if (h) return h;
        }
    }
    // 3. Bare name — falls through to DYLD_LIBRARY_PATH /
    //    DYLD_FALLBACK_LIBRARY_PATH for users with a system-wide copy.
    {
        static const char *sys_names[] = {
            "librashader.dylib",
            "librashader.2.dylib",
            "librashader.1.dylib",
        };
        for (size_t i = 0;
             i < sizeof(sys_names)/sizeof(sys_names[0]);
             i++) {
            void *h = dlopen(sys_names[i], RTLD_LAZY);
            if (h) return h;
        }
    }
    return NULL;
}
#define _LIBRASHADER_LOAD _librashader_dlopen_macos()
]=]
                _LIBRA_LD_CONTENT "${_LIBRA_LD_CONTENT}")
            file(WRITE "${_LIBRA_LD_H}" "${_LIBRA_LD_CONTENT}")
        endif()

        message(STATUS "librashader headers: ${librashader_headers_SOURCE_DIR}/include")
    endif()

    # SDL3_image — PNG/JPEG load/save for oshelper_sdl3.cpp.
    # Part of the official SDL3 ecosystem; replaces the previous stb_image
    # vendored dependency (which had known security issues).
    #
    # ALTIRRA_SKIP_SDL3_IMAGE forces it off: no target is created, nothing
    # is linked, and oshelper_sdl3.cpp compiles stub entry points that
    # throw at runtime when the UI asks to load or save an image.
    set(ALTIRRA_SDL3_IMAGE_VERSION "release-3.2.4" CACHE STRING "SDL3_image git tag to fetch")

    if(ALTIRRA_SKIP_SDL3_IMAGE)
        message(STATUS "")
        message(STATUS "  ┌─ SDL3_image ──────────────────────────────────")
        message(STATUS "  │ ALTIRRA_SKIP_SDL3_IMAGE=ON")
        message(STATUS "  │ Building without SDL3_image. Image load/save")
        message(STATUS "  │ (screenshots, loaded art tapes, PNG/JPEG UI)")
        message(STATUS "  │ will be unavailable at runtime.")
        message(STATUS "  └───────────────────────────────────────────────")
        message(STATUS "")
        add_compile_definitions(ALTIRRA_NO_SDL3_IMAGE=1)
    else()
    # If SDL3 came from FetchContent, we MUST NOT use a system
    # SDL3_image — its exported target pulls in the system SDL3 as a
    # shared dep, and CMake then rejects the configure with
    # "INTERFACE_SDL3_SHARED property ... does not agree".  Fetch
    # SDL3_image from source in that case so both libraries agree on
    # SDL3_SHARED.  Same story if the user forced a static SDL3: most
    # distros ship only a shared SDL3_image that won't link against it.
    option(ALTIRRA_FETCH_SDL3_IMAGE "Always fetch SDL3_image from source (skip find_package)" OFF)
    if(ALTIRRA_SDL3_FETCHED OR ALTIRRA_STATIC_SDL3 OR ALTIRRA_FETCH_SDL3_IMAGE)
        if(ALTIRRA_FETCH_SDL3_IMAGE)
            message(STATUS "ALTIRRA_FETCH_SDL3_IMAGE=ON — skipping find_package(SDL3_image)")
        else()
            message(STATUS "SDL3 was fetched/static — skipping find_package(SDL3_image) to keep SDL3_SHARED consistent")
        endif()
        set(SDL3_image_FOUND FALSE)
    else()
        find_package(SDL3_image QUIET)
    endif()
    if(NOT TARGET SDL3_image::SDL3_image)
        message(STATUS "")
        message(STATUS "  ┌─ SDL3_image ──────────────────────────────────")
        message(STATUS "  │ No system SDL3_image found.")
        message(STATUS "  │ Fetching SDL3_image ${ALTIRRA_SDL3_IMAGE_VERSION} from GitHub...")
        message(STATUS "  └───────────────────────────────────────────────")
        message(STATUS "")

        # Disable backends we don't need — speeds up the build and
        # eliminates optional external dependencies (libtiff, libwebp, etc.).
        # Note: SDL3_image uses SDLIMAGE_* (not SDL3IMAGE_*) for its options.
        set(SDLIMAGE_AVIF       OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_BMP        OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_GIF        OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_JPG        ON  CACHE BOOL "" FORCE)
        set(SDLIMAGE_JXL        OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_LBM        OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_PCX        OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_PNG        ON  CACHE BOOL "" FORCE)
        set(SDLIMAGE_PNM        OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_QOI        OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_SVG        OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_TGA        OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_TIF        OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_WEBP       OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_XCF        OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_XPM        OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_XV         OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_TESTS      OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_SAMPLES    OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_INSTALL    OFF CACHE BOOL "" FORCE)
        # Use stb backend (bundled in SDL3_image, no external deps needed).
        # This avoids pulling in vendored libpng/zlib/libjpeg submodules.
        set(SDLIMAGE_BACKEND_STB    ON  CACHE BOOL "" FORCE)
        set(SDLIMAGE_BACKEND_WIC    OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_BACKEND_IMAGEIO OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
            sdl3_image
            GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
            GIT_TAG        ${ALTIRRA_SDL3_IMAGE_VERSION}
            GIT_SHALLOW    TRUE
            # Don't fetch any vendored submodules — we use the stb
            # backend (SDLIMAGE_BACKEND_STB) which is bundled directly
            # in the SDL3_image source tree, no external deps needed.
            GIT_SUBMODULES ""
        )

        # SDL3_image has no dedicated shared/static option — it respects
        # the standard CMake BUILD_SHARED_LIBS.  Save/restore around
        # FetchContent_MakeAvailable so the override is scoped to
        # SDL3_image only (all other AltirraSDL libraries declare
        # STATIC / OBJECT explicitly, so the global value is harmless,
        # but scoping keeps intent clear).
        set(_ALTIRRA_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
        if(ALTIRRA_STATIC_SDL3)
            set(BUILD_SHARED_LIBS OFF)
        else()
            set(BUILD_SHARED_LIBS ON)
        endif()
        FetchContent_MakeAvailable(sdl3_image)
        set(BUILD_SHARED_LIBS "${_ALTIRRA_SAVED_BUILD_SHARED_LIBS}")
        unset(_ALTIRRA_SAVED_BUILD_SHARED_LIBS)

        if(NOT TARGET SDL3_image::SDL3_image)
            message(FATAL_ERROR
                "SDL3_image FetchContent completed but SDL3_image::SDL3_image target not found.")
        endif()
        message(STATUS "  SDL3_image ${ALTIRRA_SDL3_IMAGE_VERSION} ready (built from source)")
    endif()
    endif()  # NOT ALTIRRA_SKIP_SDL3_IMAGE

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
    # On Android (and iOS, when we get there) the GL context is OpenGL ES 3.0.
    # ImGui's imgui_impl_opengl3.cpp picks its API flavour at compile time
    # via this define — without it, the backend would try to call entry
    # points that don't exist in GLES (glPolygonMode etc.) and link-fail
    # against system libGLESv3.  On desktop the default (Desktop GL) path
    # is correct.
    if(ANDROID)
        target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)
    endif()
    # Suppress warnings in third-party code
    if(MSVC)
        target_compile_options(imgui PRIVATE /w)
    else()
        target_compile_options(imgui PRIVATE -w)
    endif()
endif()

add_subdirectory(src)

###############################################################################
# Reference lobby server (C++).
#
# AltirraSDL ships the source of its matchmaking lobby at server/lobby/
# so anyone can run their own — see server/lobby/README.md.  Built by
# default as a normal C++ target: no external toolchain required
# (cpp-httplib is vendored in the tree).
###############################################################################
add_subdirectory(server/lobby)
