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

FLATPAK_SDK_REF="${ALTIRRA_LIBRETRO_FLATPAK_SDK:-org.kde.Sdk//6.10}"
FLATPAK_RUNTIME_REF="${ALTIRRA_LIBRETRO_FLATPAK_RUNTIME:-org.kde.Platform//6.10}"

detect_max_symbol_minor() {
    local prefix="$1"
    shift
    local files=()

    for path in "$@"; do
        [ -e "$path" ] && files+=("$path")
    done

    [ "${#files[@]}" -gt 0 ] || return 0

    strings "${files[@]}" 2>/dev/null \
        | sed -n "s/^${prefix}_2\\.\\([0-9][0-9]*\\)$/\\1/p" \
        | sort -n | tail -1
}

detect_required_symbol_minor() {
    local binary="$1"
    local prefix="$2"

    { readelf --version-info "$binary" 2>/dev/null || true; } \
        | sed -n "s/.*Name: ${prefix}_2\\.\\([0-9][0-9]*\\).*/\\1/p" \
        | sort -n | tail -1
}

check_symbol_version_ceiling() {
    local binary="$1"
    local prefix="$2"
    local available_minor="$3"

    local required_minor
    required_minor=$(detect_required_symbol_minor "$binary" "$prefix")

    if [ -z "$required_minor" ]; then
        return
    fi

    if [ -z "$available_minor" ]; then
        warn "Could not determine ${prefix} ceiling from the Flatpak SDK; skipping ${prefix} ABI check"
        return
    fi

    if [ "$required_minor" -gt "$available_minor" ]; then
        die "Flatpak libretro core requires ${prefix}_2.${required_minor}; ${FLATPAK_RUNTIME_REF} provides ${prefix}_2.${available_minor}"
    fi
}

if [ "${LIBRETRO_FLATPAK:-0}" = "1" ]; then
    if [ "$PLATFORM" != "linux" ]; then
        die "--libretro-flatpak is only supported on Linux"
    fi

    if [ -z "${ALTIRRA_LIBRETRO_FLATPAK_INNER:-}" ]; then
        command -v flatpak >/dev/null 2>&1 \
            || die "flatpak not found; install Flatpak and ${FLATPAK_SDK_REF}"

        flatpak info "$FLATPAK_SDK_REF" >/dev/null 2>&1 \
            || die "${FLATPAK_SDK_REF} is not installed. Run: flatpak install flathub ${FLATPAK_SDK_REF}"

        info "Entering ${C_BOLD}${FLATPAK_SDK_REF}${C_RESET} for RetroArch-compatible libretro build"

        INNER_ARGS=(./build.sh --libretro --libretro-flatpak --jobs "$JOBS")
        [ "${CLEAN:-0}" = "1" ] && INNER_ARGS+=(--clean)
        [ "${PACKAGE:-0}" = "1" ] && INNER_ARGS+=(--package)
        if [ -n "${CMAKE_EXTRA_ARGS:-}" ]; then
            INNER_ARGS+=(--cmake "$CMAKE_EXTRA_ARGS")
        fi

        exec flatpak run --devel \
            "--filesystem=${ROOT_DIR}" \
            "--env=ALTIRRA_LIBRETRO_FLATPAK_INNER=1" \
            "--env=ALTIRRA_LIBRETRO_FLATPAK_SDK=${FLATPAK_SDK_REF}" \
            "--env=ALTIRRA_LIBRETRO_FLATPAK_RUNTIME=${FLATPAK_RUNTIME_REF}" \
            --command=sh "$FLATPAK_SDK_REF" \
            -c 'cd "$1"; shift; exec "$@"' sh "$ROOT_DIR" "${INNER_ARGS[@]}"
    fi
fi

case "$PLATFORM" in
    linux)
        if [ "${LIBRETRO_FLATPAK:-0}" = "1" ]; then
            PRESET="linux-libretro-flatpak-kde610"
        else
            PRESET="linux-libretro"
            warn "Host-native Linux libretro builds inherit this system's glibc ABI."
            warn "Use ./build.sh --libretro-flatpak for Flathub/Flatpak RetroArch packages."
        fi
        ;;
    macos)   PRESET="macos-libretro" ;;
    windows) PRESET="windows-libretro" ;;
    *)       die "Unsupported platform for --libretro: $PLATFORM" ;;
esac

BUILD_DIR="${ROOT_DIR}/build/${PRESET}"

if [ "${CLEAN:-0}" = "1" ] && [ -d "$BUILD_DIR" ]; then
    info "Cleaning build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

if [ "${LIBRETRO_FLATPAK:-0}" = "1" ]; then
    info "Configuring Flatpak-compatible libretro build: ${C_BOLD}${BUILD_DIR}${C_RESET}"
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DALTIRRA_LIBRETRO=ON \
        -DALTIRRA_SDL3=OFF \
        -DALTIRRA_LIBRETRO_NO_SDL3=ON \
        ${CMAKE_EXTRA_ARGS:-} || die "CMake configure failed"
else
    info "Configuring preset: ${C_BOLD}${PRESET}${C_RESET}"
    cmake --preset "$PRESET" ${CMAKE_EXTRA_ARGS:-} || die "CMake configure failed"
fi

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

if [ "${LIBRETRO_FLATPAK:-0}" = "1" ]; then
    AVAILABLE_GLIBC_MINOR=$(detect_max_symbol_minor GLIBC \
        /usr/lib*/libc.so.6 \
        /usr/lib*/*/libc.so.6 \
        /lib*/libc.so.6 \
        /lib*/*/libc.so.6)
    AVAILABLE_GLIBCXX_MINOR=$(detect_max_symbol_minor GLIBCXX \
        /usr/lib*/libstdc++.so.6 \
        /usr/lib*/*/libstdc++.so.6 \
        /lib*/libstdc++.so.6 \
        /lib*/*/libstdc++.so.6)

    check_symbol_version_ceiling "$CORE" GLIBC "$AVAILABLE_GLIBC_MINOR"
    check_symbol_version_ceiling "$CORE" GLIBCXX "$AVAILABLE_GLIBCXX_MINOR"

    ok "Flatpak ABI check passed for ${C_BOLD}${FLATPAK_RUNTIME_REF}${C_RESET}"
fi

if [ "${PACKAGE:-0}" = "1" ]; then
    VERSION=$(sed -n 's/^#define[[:space:]]\\+AT_VERSION[[:space:]]\\+"\\([^"]*\\)".*/\\1/p' \
        "$ROOT_DIR/src/Altirra/autobuild_default/version.h" | head -1)
    [ -n "$VERSION" ] || VERSION="dev"

    ARCH=$(uname -m)
    if [ "${LIBRETRO_FLATPAK:-0}" = "1" ]; then
        PKG_NAME="AltirraLibretro-${VERSION}-${PLATFORM}-${ARCH}-flatpak-kde610"
    else
        PKG_NAME="AltirraLibretro-${VERSION}-${PLATFORM}-${ARCH}"
    fi
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
$(if [ "${LIBRETRO_FLATPAK:-0}" = "1" ]; then printf 'Built inside %s for RetroArch Flatpak runtime %s\n' "$FLATPAK_SDK_REF" "$FLATPAK_RUNTIME_REF"; fi)
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
