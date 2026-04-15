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

# ── macOS: also produce a .dmg disk image ────────────────────────────────
#
# hdiutil ships with macOS so no extra tooling is required.  We produce a
# UDZO (zlib-compressed) DMG containing AltirraSDL.app alongside the extras
# folder and Copying file — same layout as the zip, so users who prefer
# drag-to-Applications get a native experience and users who prefer the
# zip are unaffected.
#
# The DMG is built from $PKG_DIR (the already-populated package directory
# under $BUILD_DIR), so it includes every post-build step that ran against
# the zip: bundled libSDL3*.dylib, librashader.dylib, and the ad-hoc
# codesign from the package_altirra target.  hdiutil preserves the code
# signature — no re-signing needed.
#
# Guarded by command -v hdiutil so this is safe to leave unconditional on
# the package.sh path: Linux / Windows CI runners don't have hdiutil and
# simply skip this step without failing the package run.
if [ "${PLATFORM}" = "macos" ] && command -v hdiutil &>/dev/null; then
    DMG_PATH="$BUILD_DIR/${ARCHIVE_NAME}.dmg"
    info "Creating DMG: ${ARCHIVE_NAME}.dmg"

    # -volname is the Finder label when the DMG is mounted.
    # -srcfolder is the already-populated package directory.
    # -format UDZO is zlib-compressed (good balance of size vs. mount speed).
    # -ov overwrites any existing file at the destination (from previous run).
    # Intentionally NOT using -quiet: when hdiutil fails on a CI runner
    # (disk pressure, xattr quirks, transient APFS issues) we need the real
    # error in the log. Progress bars are harmless noise in comparison.
    #
    # Failure is non-fatal: the .zip produced above is the primary macOS
    # artifact. Losing the .dmg convenience is not worth failing the whole
    # release pipeline. Record a warning and carry on.
    if hdiutil create \
        -volname "AltirraSDL ${VERSION}" \
        -srcfolder "$PKG_DIR" \
        -format UDZO \
        -ov \
        "$DMG_PATH"; then
        SIZE=$(du -h "$DMG_PATH" | cut -f1)
        ok "Disk image:     ${C_BOLD}${DMG_PATH}${C_RESET} ($SIZE)"
    else
        warn "hdiutil create failed — .zip is still available as the primary artifact"
    fi
fi

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
