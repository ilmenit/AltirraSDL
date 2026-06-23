#!/usr/bin/env bash
# libretro.sh — Build the RetroArch/libretro core
#               (altirra_libretro.{so,dll,dylib}).
#
# Sourced by build.sh when --libretro is passed; never run directly.
# Expects: ROOT_DIR, PLATFORM, CLEAN, JOBS, CMAKE_EXTRA_ARGS, PACKAGE
#          (from build.sh)
#
# The libretro core is a standalone shared library with no SDL3 dependency
# (ALTIRRA_LIBRETRO_NO_SDL3=ON), so it neither fetches nor links SDL3.  It is
# always built Release — RetroArch cores are distributed optimised.

[ -z "${C_RESET:-}" ] && source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

case "$PLATFORM" in
    linux)   PRESET="linux-libretro" ;;
    macos)   PRESET="macos-libretro" ;;
    windows) PRESET="windows-libretro" ;;
    *)       die "Unsupported platform for --libretro: $PLATFORM" ;;
esac

BUILD_DIR="${ROOT_DIR}/build/${PRESET}"

if [ "${CLEAN:-0}" = "1" ] && [ -d "$BUILD_DIR" ]; then
    info "Cleaning build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

info "Configuring preset: ${C_BOLD}${PRESET}${C_RESET}"
cmake --preset "$PRESET" ${CMAKE_EXTRA_ARGS:-} || die "CMake configure failed"

info "Building libretro core with ${C_BOLD}${JOBS}${C_RESET} parallel jobs..."
BUILD_ARGS=(--build "$BUILD_DIR" --target AltirraLibretro -j "$JOBS")
# MSVC multi-config generators need --config.
if [ "$PLATFORM" = "windows" ]; then
    BUILD_ARGS+=(--config Release)
fi
cmake "${BUILD_ARGS[@]}" || die "Build failed"

# ── Locate and report the artifact ────────────────────────────────────────
case "$PLATFORM" in
    linux)   CORE="$BUILD_DIR/src/AltirraLibretro/altirra_libretro.so" ;;
    macos)   CORE="$BUILD_DIR/src/AltirraLibretro/altirra_libretro.dylib" ;;
    windows)
        CORE="$BUILD_DIR/src/AltirraLibretro/Release/altirra_libretro.dll"
        [ -f "$CORE" ] || CORE="$BUILD_DIR/src/AltirraLibretro/altirra_libretro.dll"
        ;;
esac

[ -f "$CORE" ] || die "libretro core not found after build (expected: $CORE)"

INFO_SRC="${ROOT_DIR}/src/AltirraLibretro/altirra_libretro.info"
INFO_DST="$(dirname "$CORE")/altirra_libretro.info"
cp "$INFO_SRC" "$INFO_DST"

SIZE=$(du -h "$CORE" | cut -f1)
ok "libretro core: ${C_BOLD}${CORE}${C_RESET} ($SIZE)"
ok "core info:     ${C_BOLD}${INFO_DST}${C_RESET}"
info "Install the core into RetroArch's cores directory and altirra_libretro.info into RetroArch's Core Info directory."

if [ "${PACKAGE:-0}" = "1" ]; then
    VERSION=$(sed -n 's/^#define[[:space:]]\\+AT_VERSION[[:space:]]\\+"\\([^"]*\\)".*/\\1/p' \
        "$ROOT_DIR/src/Altirra/autobuild_default/version.h" | head -1)
    [ -n "$VERSION" ] || VERSION="dev"

    ARCH=$(uname -m)
    PKG_NAME="AltirraLibretro-${VERSION}-${PLATFORM}-${ARCH}"
    PKG_DIR="$BUILD_DIR/$PKG_NAME"

    info "Creating libretro package: ${C_BOLD}${PKG_NAME}${C_RESET}"
    rm -rf "$PKG_DIR"
    mkdir -p "$PKG_DIR"

    cp "$CORE" "$PKG_DIR/"
    cp "$INFO_SRC" "$PKG_DIR/"
    cp "$ROOT_DIR/src/AltirraLibretro/README.md" "$PKG_DIR/"
    cp "$ROOT_DIR/src/AltirraLibretro/install-retroarch.sh" "$PKG_DIR/"

    COMMIT_SHORT=$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo "unknown")
    COMMIT_FULL=$(git -C "$ROOT_DIR" rev-parse HEAD 2>/dev/null || echo "unknown")
    BUILD_DATE=$(date -u +%Y-%m-%d)

    cat > "$PKG_DIR/BUILD-INFO.txt" <<BUILDINFO
AltirraLibretro ${VERSION} ${COMMIT_SHORT} — ${PLATFORM} ${ARCH}
Built ${BUILD_DATE} from commit ${COMMIT_FULL}
Install altirra_libretro.info into RetroArch's Core Info directory.
BUILDINFO

    case "$PLATFORM" in
        linux)
            ARCHIVE_PATH="$BUILD_DIR/$PKG_NAME.tar.gz"
            (cd "$BUILD_DIR" && rm -f "$PKG_NAME.tar.gz" \
                && tar -czf "$PKG_NAME.tar.gz" "$PKG_NAME")
            ;;
        macos|windows)
            if command -v zip &>/dev/null; then
                ARCHIVE_PATH="$BUILD_DIR/$PKG_NAME.zip"
                (cd "$BUILD_DIR" && rm -f "$PKG_NAME.zip" \
                    && zip -rq "$PKG_NAME.zip" "$PKG_NAME")
            else
                ARCHIVE_PATH="$BUILD_DIR/$PKG_NAME.tar.gz"
                warn "zip not found — using tar instead"
                (cd "$BUILD_DIR" && rm -f "$PKG_NAME.tar.gz" \
                    && tar -czf "$PKG_NAME.tar.gz" "$PKG_NAME")
            fi
            ;;
    esac

    SIZE=$(du -h "$ARCHIVE_PATH" | cut -f1)
    ok "Package:       ${C_BOLD}${ARCHIVE_PATH}${C_RESET} ($SIZE)"
fi
