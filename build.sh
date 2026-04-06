#!/usr/bin/env bash
#
# build.sh — Cross-platform build script for AltirraSDL
#
# Usage:
#   ./build.sh                    Build release for current platform
#   ./build.sh --debug            Build debug
#   ./build.sh --android          Build Android APK (debug by default)
#   ./build.sh --android --release  Build Android APK (release)
#   ./build.sh --android --sign   Build + sign with debug keystore
#   ./build.sh --setup-android    Install Android SDK components, then build
#   ./build.sh --package          Build + create distributable archive
#   ./build.sh --package --source Also create source archive
#   ./build.sh --clean            Clean rebuild
#   ./build.sh --native           Windows only: build libs for .sln (no SDL3)
#   ./build.sh --jobs 8           Override parallel job count
#   ./build.sh --librashader      Build librashader from source (needs Rust)
#   ./build.sh --fetch-sdl3       Force-fetch SDL3 from source (ignore system SDL3).
#                                 Use this if find_package picks up a broken SDL3.
#   ./build.sh --appimage         Linux only: produce a portable .AppImage
#                                 (implies --package --librashader)
#   ./build.sh --cmake "-DFOO=1"  Pass extra CMake arguments
#   ./build.sh --help             Show this help
#
# On Windows, run from Git Bash, MSYS2, or WSL.
# Requires: cmake 3.24+, C++20 compiler, git (for fetching SDL3/ImGui).
# SDL3 is auto-fetched and built from source if no system SDL3 is found,
# so no manual SDL3 install is required.  If you have SDL3 installed and
# CMake picks up a broken copy, pass --fetch-sdl3 to force the source build.
# Android: ANDROID_HOME set, NDK installed, Java 11+.
#
# Output archives (with --package):
#   build/<preset>/AltirraSDL-<ver>-<platform>.zip
#   build/<preset>/AltirraSDL-<ver>-src.tar.gz  (with --source)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
SCRIPTS_DIR="$ROOT_DIR/scripts/build"

source "$SCRIPTS_DIR/common.sh"

# ── Defaults ──────────────────────────────────────────────────────────────
BUILD_TYPE=release
FRONTEND=sdl
ANDROID=0
SETUP_ANDROID=0
SIGN_APK=0
CLEAN=0
PACKAGE=0
SOURCE_ARCHIVE=0
JOBS=""
CMAKE_EXTRA_ARGS=""
BUILD_LIBRASHADER=0
APPIMAGE=0

# ── Parse arguments ───────────────────────────────────────────────────────
while [ $# -gt 0 ]; do
    case "$1" in
        --debug)    BUILD_TYPE=debug ;;
        --release)  BUILD_TYPE=release ;;
        --android)  ANDROID=1; BUILD_TYPE=debug ;;
        --sign)     SIGN_APK=1 ;;
        --setup-android) ANDROID=1; SETUP_ANDROID=1; BUILD_TYPE=debug ;;
        --native)   FRONTEND=native ;;
        --sdl)      FRONTEND=sdl ;;
        --clean)    CLEAN=1 ;;
        --package)  PACKAGE=1 ;;
        --source)   SOURCE_ARCHIVE=1 ;;
        --jobs)     shift; JOBS="$1" ;;
        -j*)        JOBS="${1#-j}" ;;
        --librashader) BUILD_LIBRASHADER=1 ;;
        --fetch-sdl3)
            if [ -z "$CMAKE_EXTRA_ARGS" ]; then
                CMAKE_EXTRA_ARGS="-DALTIRRA_FETCH_SDL3=ON"
            else
                CMAKE_EXTRA_ARGS="$CMAKE_EXTRA_ARGS -DALTIRRA_FETCH_SDL3=ON"
            fi
            ;;
        --appimage) APPIMAGE=1; PACKAGE=1; BUILD_LIBRASHADER=1 ;;
        --cmake)    shift; CMAKE_EXTRA_ARGS="$1" ;;
        --help|-h)
            sed -n '3,/^$/{ s/^# //; s/^#//; p }' "$0"
            exit 0 ;;
        *) die "Unknown option: $1  (try --help)" ;;
    esac
    shift
done

# ── Android build (separate path — uses Gradle, not CMake presets) ────────
if [ "$ANDROID" = "1" ]; then
    detect_platform

    echo ""
    info "Platform:   ${C_BOLD}Android${C_RESET}"
    info "Build type: ${C_BOLD}${BUILD_TYPE}${C_RESET}"
    echo ""

    export ROOT_DIR BUILD_TYPE CLEAN SETUP_ANDROID SIGN_APK
    source "$SCRIPTS_DIR/android.sh"

    echo ""
    ok "All done!"
    exit 0
fi

# ── Detect & resolve ──────────────────────────────────────────────────────
detect_platform
resolve_preset
detect_jobs

echo ""
info "Platform:   ${C_BOLD}${PLATFORM}${C_RESET}"
info "Build type: ${C_BOLD}${BUILD_TYPE}${C_RESET}"
info "Frontend:   ${C_BOLD}${FRONTEND}${C_RESET}"
info "Preset:     ${C_BOLD}${PRESET}${C_RESET}"
info "Jobs:       ${C_BOLD}${JOBS}${C_RESET}"
echo ""

# ── Export for sub-scripts ────────────────────────────────────────────────
export ROOT_DIR BUILD_DIR PRESET BUILD_TYPE PLATFORM FRONTEND
export CLEAN JOBS CMAKE_EXTRA_ARGS SOURCE_ARCHIVE

# ── Packaging implies a self-contained build ─────────────────────────────
# When the user asks for a redistributable package on Linux/macOS, force
# SDL3 to be fetched from source.  Otherwise find_package picks up the
# host's system SDL3 and the resulting archive doesn't bundle libSDL3.so,
# leaving end users to chase a system SDL3 install — defeating the point
# of --package.  Skip on Windows (no system SDL3 to find anyway) and
# Android (handled separately).  Honour an explicit override.
if [ "$PACKAGE" = "1" ] && [ "$PLATFORM" != "windows" ]; then
    if ! echo "$CMAKE_EXTRA_ARGS" | grep -q "ALTIRRA_FETCH_SDL3"; then
        info "Packaging mode — forcing -DALTIRRA_FETCH_SDL3=ON for self-contained archive"
        if [ -z "$CMAKE_EXTRA_ARGS" ]; then
            CMAKE_EXTRA_ARGS="-DALTIRRA_FETCH_SDL3=ON"
        else
            CMAKE_EXTRA_ARGS="$CMAKE_EXTRA_ARGS -DALTIRRA_FETCH_SDL3=ON"
        fi
        # If a previous configure cached a system SDL3, the cache will
        # keep using it even after we add -DALTIRRA_FETCH_SDL3=ON.
        # Drop the cache so the next configure starts fresh.
        if [ -f "$BUILD_DIR/CMakeCache.txt" ] \
           && grep -q '^SDL3_DIR:' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null \
           && ! grep -q '^ALTIRRA_FETCH_SDL3:BOOL=ON' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
            info "Resetting CMake cache to switch from system SDL3 to fetched SDL3"
            rm -f "$BUILD_DIR/CMakeCache.txt"
            rm -rf "$BUILD_DIR/CMakeFiles"
        fi
    fi
fi

# ── Configure ─────────────────────────────────────────────────────────────
source "$SCRIPTS_DIR/configure.sh"

# ── Build ─────────────────────────────────────────────────────────────────
source "$SCRIPTS_DIR/compile.sh"

# ── librashader (optional) ───────────────────────────────────────────────
if [ "$BUILD_LIBRASHADER" = "1" ]; then
    source "$SCRIPTS_DIR/librashader.sh"
fi

# ── Package (optional) ────────────────────────────────────────────────────
if [ "$PACKAGE" = "1" ]; then
    source "$SCRIPTS_DIR/package.sh"
fi

# ── AppImage (Linux, optional) ────────────────────────────────────────────
if [ "$APPIMAGE" = "1" ]; then
    if [ "$PLATFORM" != "linux" ]; then
        die "--appimage is only supported on Linux (current: $PLATFORM)"
    fi
    source "$SCRIPTS_DIR/appimage.sh"
fi

# ── Report output ────────────────────────────────────────────────────────
echo ""
if [ "$FRONTEND" = "sdl" ]; then
    if [ "${PLATFORM}" = "windows" ]; then
        local_type="$(echo "${BUILD_TYPE:0:1}" | tr '[:lower:]' '[:upper:]')${BUILD_TYPE:1}"
        EXE="$BUILD_DIR/src/AltirraSDL/${local_type}/AltirraSDL.exe"
    else
        EXE="$BUILD_DIR/src/AltirraSDL/AltirraSDL"
    fi
    if [ -f "$EXE" ]; then
        SIZE=$(du -h "$EXE" | cut -f1)
        ok "Executable: ${C_BOLD}${EXE}${C_RESET} ($SIZE)"
    fi
fi
ok "All done!"
