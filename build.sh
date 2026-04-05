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
#   ./build.sh --cmake "-DFOO=1"  Pass extra CMake arguments
#   ./build.sh --help             Show this help
#
# On Windows, run from Git Bash, MSYS2, or WSL.
# Requires: cmake 3.24+, C++20 compiler, SDL3 dev package.
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
