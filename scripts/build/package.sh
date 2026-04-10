#!/usr/bin/env bash
# package.sh — Build the package_altirra target and create distributable archives.
#
# Produces (matching Altirra's naming convention):
#   AltirraSDL-<version>-<platform>.zip   — binary distribution
#   AltirraSDL-<version>-src.tar.gz       — source archive (if --source)
#
# Expects: BUILD_DIR, PLATFORM, ROOT_DIR, BUILD_TYPE (from build.sh)

[ -z "${C_RESET:-}" ] && source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# Portable capitalize (works on Bash 3.2+, no ${var^} needed)
capitalize() {
    local first="${1:0:1}" rest="${1:1}"
    printf '%s%s' "$(echo "$first" | tr '[:lower:]' '[:upper:]')" "$rest"
}

# ── Binary package ────────────────────────────────────────────────────────

info "Creating binary distribution package..."

BUILD_ARGS=(--build "$BUILD_DIR" --target package_altirra)
if [ "${PLATFORM}" = "windows" ]; then
    BUILD_ARGS+=(--config "$(capitalize "$BUILD_TYPE")")
fi

cmake "${BUILD_ARGS[@]}" || die "Package target failed"

# Find the package directory created by CMake
PKG_DIR=$(find "$BUILD_DIR" -maxdepth 1 -name "AltirraSDL-*" -type d | head -1)
[ -z "$PKG_DIR" ] && die "Package directory not found in $BUILD_DIR"

# Extract version from directory name (AltirraSDL-4.40 → 4.40)
VERSION=$(basename "$PKG_DIR" | sed 's/AltirraSDL-//')

# ── Generate BUILD-INFO.txt ──────────────────────────────────────────────
COMMIT_SHORT=$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo "unknown")
COMMIT_FULL=$(git -C "$ROOT_DIR" rev-parse HEAD 2>/dev/null || echo "unknown")
BUILD_DATE=$(date -u +%Y-%m-%d)
ARCH=$(uname -m)

cat > "$PKG_DIR/BUILD-INFO.txt" <<BUILDINFO
AltirraSDL ${VERSION} ${COMMIT_SHORT} — ${PLATFORM} ${ARCH}
Built ${BUILD_DATE} from commit ${COMMIT_FULL}
BUILDINFO

info "Generated BUILD-INFO.txt in package"

# Archive name follows Altirra convention: AltirraSDL-<ver>-<platform>.zip
ARCHIVE_NAME="AltirraSDL-${VERSION}-${PLATFORM}"

info "Creating archive: ${ARCHIVE_NAME}.zip"

if command -v zip &>/dev/null; then
    ARCHIVE_PATH="$BUILD_DIR/${ARCHIVE_NAME}.zip"
    (cd "$BUILD_DIR" && rm -f "${ARCHIVE_NAME}.zip" \
        && cd "$(basename "$PKG_DIR")" \
        && zip -rq "../${ARCHIVE_NAME}.zip" .)
else
    ARCHIVE_PATH="$BUILD_DIR/${ARCHIVE_NAME}.tar.gz"
    warn "zip not found — using tar instead"
    (cd "$BUILD_DIR" && tar czf "${ARCHIVE_NAME}.tar.gz" \
        -C "$(basename "$PKG_DIR")" .)
fi

SIZE=$(du -h "$ARCHIVE_PATH" | cut -f1)
ok "Binary package: ${C_BOLD}${ARCHIVE_PATH}${C_RESET} ($SIZE)"

# ── Source archive (optional, enabled by SOURCE_ARCHIVE=1) ────────────────

if [ "${SOURCE_ARCHIVE:-0}" = "1" ]; then
    SRC_ARCHIVE_NAME="AltirraSDL-${VERSION}-src"
    SRC_ARCHIVE="$BUILD_DIR/${SRC_ARCHIVE_NAME}.tar.gz"

    info "Creating source archive: ${SRC_ARCHIVE_NAME}.tar.gz"

    # Use git archive if in a git repo, otherwise plain tar
    if [ -d "$ROOT_DIR/.git" ]; then
        (cd "$ROOT_DIR" && git archive --format=tar.gz \
            --prefix="${SRC_ARCHIVE_NAME}/" \
            -o "$SRC_ARCHIVE" HEAD)
    else
        (cd "$(dirname "$ROOT_DIR")" && tar czf "$SRC_ARCHIVE" \
            --exclude='build' --exclude='.git' \
            --transform="s|^$(basename "$ROOT_DIR")|${SRC_ARCHIVE_NAME}|" \
            "$(basename "$ROOT_DIR")")
    fi

    SIZE=$(du -h "$SRC_ARCHIVE" | cut -f1)
    ok "Source archive: ${C_BOLD}${SRC_ARCHIVE}${C_RESET} ($SIZE)"
fi
