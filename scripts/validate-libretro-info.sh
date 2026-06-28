#!/usr/bin/env bash
#
# Validate Altirra's libretro core info file before packaging or upstreaming.

set -euo pipefail

INFO_FILE="${1:-src/AltirraLibretro/altirra_libretro.info}"
CMAKE_FILE="${2:-CMakeLists.txt}"
CORE_FILE="${3:-src/AltirraLibretro/libretro.cpp}"

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

[ -f "$INFO_FILE" ] || fail "info file not found: $INFO_FILE"
[ -f "$CMAKE_FILE" ] || fail "CMake file not found: $CMAKE_FILE"
[ -f "$CORE_FILE" ] || fail "core source not found: $CORE_FILE"

# Note: `\+` is a GNU sed extension; use the portable `X X*` form so this
# matches under BSD sed (macOS) as well.
PROJECT_VERSION=$(sed -n \
    's/^project(Altirra[[:space:]][[:space:]]*VERSION[[:space:]][[:space:]]*\([^[:space:])]*\).*/\1/p' \
    "$CMAKE_FILE" | head -1)
[ -n "$PROJECT_VERSION" ] \
    || fail "could not read project version from $CMAKE_FILE"

# bash 3.2 (the default /bin/bash on macOS) has no associative arrays, so
# emulate INFO[key]=value with prefixed dynamic variables.  Keys are validated
# to [A-Za-z0-9_]+ below, so they are always valid shell identifier suffixes.
info_set() { eval "INFOVAL_$1=\$2"; }
info_get() { eval "printf '%s' \"\${INFOVAL_$1-}\""; }
info_has() { eval "[ \"\${INFOVAL_$1+set}\" = set ]"; }

LINE_NO=0
while IFS= read -r LINE || [ -n "$LINE" ]; do
    LINE_NO=$((LINE_NO + 1))

    if [[ "$LINE" =~ ^[[:space:]]*$ ]] \
        || [[ "$LINE" =~ ^[[:space:]]*# ]]; then
        continue
    fi

    if [[ ! "$LINE" =~ ^[[:space:]]*([A-Za-z0-9_]+)[[:space:]]*=[[:space:]]*(.*[^[:space:]])[[:space:]]*$ ]]; then
        fail "$INFO_FILE:$LINE_NO: malformed .info assignment"
    fi

    KEY="${BASH_REMATCH[1]}"
    VALUE="${BASH_REMATCH[2]}"

    if [[ "$VALUE" =~ ^\"(.*)\"$ ]]; then
        VALUE="${BASH_REMATCH[1]}"
    elif [[ ! "$VALUE" =~ ^[0-9]+$ ]]; then
        fail "$INFO_FILE:$LINE_NO: unquoted non-numeric value for key: $KEY"
    fi

    if info_has "$KEY"; then
        fail "$INFO_FILE:$LINE_NO: duplicate key: $KEY"
    fi

    info_set "$KEY" "$VALUE"
done < "$INFO_FILE"

require_key() {
    local key="$1"
    info_has "$key" || fail "missing required key: $key"
}

require_value() {
    local key="$1"
    local expected="$2"

    require_key "$key"
    if [ "$(info_get "$key")" != "$expected" ]; then
        fail "unexpected $key: '$(info_get "$key")' (expected '$expected')"
    fi
}

require_extension() {
    local ext="$1"
    require_key supported_extensions

    case "|$(info_get supported_extensions)|" in
        *"|$ext|"*) ;;
        *) fail "supported_extensions is missing '$ext'" ;;
    esac
}

require_value display_name "Atari - 400/800/600XL/800XL/130XE/5200 (Altirra)"
require_value authors "Jakub 'ilmenit' Debski, fork of Altirra by Avery Lee"
require_value corename "Altirra"
require_value categories "Emulator"
require_value license "GPLv2+"
require_value manufacturer "Atari"
require_value systemname "Atari 8-bit Family"
require_value systemid "atari_8bit"
require_value display_version "$PROJECT_VERSION"
require_value database "Atari - 8-bit Family|Atari - 5200"
require_value supports_no_game "true"
require_value savestate "true"
require_value savestate_features "serialized"
require_value cheats "true"
require_value memory_descriptors "true"
require_value libretro_saves "true"
require_value core_options "true"
require_value core_options_version "2.0"
require_value load_subsystem "true"
require_value needs_fullpath "true"
require_value disk_control "true"
require_value needs_kbd_mouse_focus "false"

if [ "${ALTIRRA_LIBRETRO_ALLOW_NON_EXPERIMENTAL:-0}" != "1" ]; then
    require_value is_experimental "true"
else
    require_key is_experimental
    case "$(info_get is_experimental)" in
        true|false) ;;
        *) fail "is_experimental must be 'true' or 'false': $(info_get is_experimental)" ;;
    esac
fi

for EXT in atr xfd atx atz dcm pro arc bin rom car a52 xex exe obx com bas \
    cas wav flac ogg sap vgm vgz zip gz altstate atstate2 m3u; do
    require_extension "$EXT"
done

CORE_EXTENSIONS=$(sed -n '/kValidExtensions[[:space:]]*=/,/;/p' "$CORE_FILE" \
    | sed -n 's/.*"\([^"]*\)".*/\1/p' \
    | tr -d '\n')
[ -n "$CORE_EXTENSIONS" ] \
    || fail "could not extract kValidExtensions from $CORE_FILE"
require_value supported_extensions "$CORE_EXTENSIONS"

require_key firmware_count
FIRMWARE_COUNT=$(info_get firmware_count)
[[ "$FIRMWARE_COUNT" =~ ^[0-9]+$ ]] \
    || fail "firmware_count is not numeric: $FIRMWARE_COUNT"

for ((I = 0; I < FIRMWARE_COUNT; ++I)); do
    require_key "firmware${I}_desc"
    require_key "firmware${I}_path"
    require_value "firmware${I}_opt" "true"
done

printf 'ok: %s matches Altirra %s libretro metadata expectations\n' \
    "$INFO_FILE" "$PROJECT_VERSION"
