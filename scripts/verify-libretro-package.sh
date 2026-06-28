#!/usr/bin/env bash
#
# Verify a packaged Altirra libretro archive.

set -euo pipefail

ARCHIVE="${1:-}"
CMAKE_FILE="${2:-CMakeLists.txt}"
ROOT_DIR=""
SOURCE_INFO_FILE=""
SOURCE_README_FILE=""

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

warn() {
    printf 'warning: %s\n' "$*" >&2
}

require_readme_text() {
    local text="$1"

    grep -Fq "$text" "$PKG_DIR/README.md" \
        || fail "packaged README.md missing required guidance: $text"
}

[ -n "$ARCHIVE" ] || fail "usage: $0 ARCHIVE [CMAKE_FILE]"
[ -f "$ARCHIVE" ] || fail "package not found: $ARCHIVE"
[ -f "$CMAKE_FILE" ] || fail "CMake file not found: $CMAKE_FILE"

ROOT_DIR=$(cd "$(dirname "$CMAKE_FILE")" && pwd)
SOURCE_INFO_FILE="$ROOT_DIR/src/AltirraLibretro/altirra_libretro.info"
SOURCE_README_FILE="$ROOT_DIR/src/AltirraLibretro/README.md"
[ -f "$SOURCE_INFO_FILE" ] \
    || fail "source core info not found: $SOURCE_INFO_FILE"
[ -f "$SOURCE_README_FILE" ] \
    || fail "source README not found: $SOURCE_README_FILE"

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

# Use a portable read loop instead of `mapfile`/`readarray`, which are not
# available in the bash 3.2 shipped on macOS.
ROOTS=()
while IFS= read -r line; do
    ROOTS+=("$line")
done < <(find "$TMP_DIR" -mindepth 1 -maxdepth 1 -type d | sort)
[ "${#ROOTS[@]}" -eq 1 ] \
    || fail "package should contain exactly one top-level directory"

PKG_DIR="${ROOTS[0]}"

for required in BUILD-INFO.txt LICENSE README.md altirra_libretro.info \
    install-retroarch.sh; do
    [ -f "$PKG_DIR/$required" ] || fail "package missing $required"
done

cmp -s "$SOURCE_INFO_FILE" "$PKG_DIR/altirra_libretro.info" \
    || fail "packaged altirra_libretro.info does not match source metadata"
cmp -s "$SOURCE_README_FILE" "$PKG_DIR/README.md" \
    || fail "packaged README.md does not match source README"

for readme_text in \
    "The core is usable from a controller-only RetroArch device." \
    "| Left analog | Joystick |" \
    "| Select+R2 | Toggle virtual keyboard fallback for stickless pads |" \
    "| Select+Start | Warm reset |" \
    "| Select+L | Cold reset |" \
    "The face/shoulder key bindings are concurrent with the joystick" \
    "and a 5200 keypad page with \`0\`-\`9\`, \`*\`, \`#\`, START, PAUSE, and RESET." \
    "Disk images mount in virtual read/write mode by default." \
    "Runtime disk-control replacement accepts disk images and \`.m3u\` playlists."; do
    require_readme_text "$readme_text"
done

CORES=()
while IFS= read -r line; do
    CORES+=("$line")
done < <(find "$PKG_DIR" -maxdepth 1 -type f \
    \( -name 'altirra_libretro.so' \
    -o -name 'altirra_libretro.dylib' \
    -o -name 'altirra_libretro.dll' \) | sort)
[ "${#CORES[@]}" -eq 1 ] \
    || fail "package should contain exactly one altirra_libretro core"

case "${CORES[0]}" in
    *.dll)
        [ -x "$PKG_DIR/install-retroarch.sh" ] \
            || warn "package install-retroarch.sh is not executable; acceptable for Windows ZIP packages"
        ;;
    *)
        [ -x "$PKG_DIR/install-retroarch.sh" ] \
            || fail "package install-retroarch.sh is not executable"
        ;;
esac

bash "$(dirname "$0")/verify-libretro-artifact.sh" \
    "${CORES[0]}" "$PKG_DIR/altirra_libretro.info" "$CMAKE_FILE"

INSTALL_SMOKE_DIR="$TMP_DIR/install-smoke"
INSTALL_SMOKE_CFG="$INSTALL_SMOKE_DIR/retroarch.cfg"
mkdir -p "$INSTALL_SMOKE_DIR"
cat > "$INSTALL_SMOKE_CFG" <<EOF
libretro_directory = "$INSTALL_SMOKE_DIR/cores"
libretro_info_path = "$INSTALL_SMOKE_DIR/info"
EOF

bash "$PKG_DIR/install-retroarch.sh" \
    --config "$INSTALL_SMOKE_CFG" \
    --core "${CORES[0]}" \
    --info "$PKG_DIR/altirra_libretro.info" \
    --cores-dir "$INSTALL_SMOKE_DIR/cores" \
    --info-dir "$INSTALL_SMOKE_DIR/info" \
    --dry-run >/dev/null

if ! grep -q '^AltirraLibretro ' "$PKG_DIR/BUILD-INFO.txt"; then
    fail "BUILD-INFO.txt missing AltirraLibretro header"
fi

if ! grep -Eq '^Built [0-9]{4}-[0-9]{2}-[0-9]{2} from commit [0-9a-f]{40}$' \
    "$PKG_DIR/BUILD-INFO.txt"; then
    fail "BUILD-INFO.txt missing full git commit provenance"
fi

if grep -Eq '(^|[[:space:]])unknown([[:space:]]|$)' "$PKG_DIR/BUILD-INFO.txt"; then
    fail "BUILD-INFO.txt contains unknown provenance"
fi

printf 'ok: %s passes libretro package checks\n' "$ARCHIVE"
