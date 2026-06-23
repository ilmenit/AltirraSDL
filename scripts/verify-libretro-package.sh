#!/usr/bin/env bash
#
# Verify a packaged Altirra libretro archive.

set -euo pipefail

ARCHIVE="${1:-}"
CMAKE_FILE="${2:-CMakeLists.txt}"

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

[ -n "$ARCHIVE" ] || fail "usage: $0 ARCHIVE [CMAKE_FILE]"
[ -f "$ARCHIVE" ] || fail "package not found: $ARCHIVE"
[ -f "$CMAKE_FILE" ] || fail "CMake file not found: $CMAKE_FILE"

TMP_DIR=$(mktemp -d)
cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

case "$ARCHIVE" in
    *.tar.gz|*.tgz)
        tar -xzf "$ARCHIVE" -C "$TMP_DIR"
        ;;
    *.zip)
        command -v unzip >/dev/null 2>&1 \
            || fail "unzip not found; cannot verify zip package"
        unzip -q "$ARCHIVE" -d "$TMP_DIR"
        ;;
    *)
        fail "unsupported package extension: $ARCHIVE"
        ;;
esac

mapfile -t ROOTS < <(find "$TMP_DIR" -mindepth 1 -maxdepth 1 -type d | sort)
[ "${#ROOTS[@]}" -eq 1 ] \
    || fail "package should contain exactly one top-level directory"

PKG_DIR="${ROOTS[0]}"

for required in BUILD-INFO.txt LICENSE README.md altirra_libretro.info \
    install-retroarch.sh; do
    [ -f "$PKG_DIR/$required" ] || fail "package missing $required"
done

[ -x "$PKG_DIR/install-retroarch.sh" ] \
    || fail "package install-retroarch.sh is not executable"

mapfile -t CORES < <(find "$PKG_DIR" -maxdepth 1 -type f \
    \( -name 'altirra_libretro.so' \
    -o -name 'altirra_libretro.dylib' \
    -o -name 'altirra_libretro.dll' \) | sort)
[ "${#CORES[@]}" -eq 1 ] \
    || fail "package should contain exactly one altirra_libretro core"

bash "$(dirname "$0")/verify-libretro-artifact.sh" \
    "${CORES[0]}" "$PKG_DIR/altirra_libretro.info" "$CMAKE_FILE"

if ! grep -q '^AltirraLibretro ' "$PKG_DIR/BUILD-INFO.txt"; then
    fail "BUILD-INFO.txt missing AltirraLibretro header"
fi

printf 'ok: %s passes libretro package checks\n' "$ARCHIVE"
