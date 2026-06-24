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

PROJECT_VERSION=$(sed -n \
    's/^project(Altirra[[:space:]]\+VERSION[[:space:]]\+\([^[:space:])]*\).*/\1/p' \
    "$CMAKE_FILE" | head -1)
[ -n "$PROJECT_VERSION" ] \
    || fail "could not read project version from $CMAKE_FILE"

declare -A INFO

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

    if [[ -v "INFO[$KEY]" ]]; then
        fail "$INFO_FILE:$LINE_NO: duplicate key: $KEY"
    fi

    INFO["$KEY"]="$VALUE"
done < "$INFO_FILE"

require_key() {
    local key="$1"
    [[ -v "INFO[$key]" ]] || fail "missing required key: $key"
}

require_value() {
    local key="$1"
    local expected="$2"

    require_key "$key"
    if [ "${INFO[$key]}" != "$expected" ]; then
        fail "unexpected $key: '${INFO[$key]}' (expected '$expected')"
    fi
}

require_extension() {
    local ext="$1"
    require_key supported_extensions

    case "|${INFO[supported_extensions]}|" in
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
require_value core_options "true"
require_value core_options_version "2.0"
require_value needs_fullpath "true"
require_value disk_control "true"
require_value needs_kbd_mouse_focus "true"

if [ "${ALTIRRA_LIBRETRO_ALLOW_NON_EXPERIMENTAL:-0}" != "1" ]; then
    require_value is_experimental "true"
else
    require_key is_experimental
    case "${INFO[is_experimental]}" in
        true|false) ;;
        *) fail "is_experimental must be 'true' or 'false': ${INFO[is_experimental]}" ;;
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
[[ "${INFO[firmware_count]}" =~ ^[0-9]+$ ]] \
    || fail "firmware_count is not numeric: ${INFO[firmware_count]}"

for ((I = 0; I < INFO[firmware_count]; ++I)); do
    require_key "firmware${I}_desc"
    require_key "firmware${I}_path"
    require_value "firmware${I}_opt" "true"
done

printf 'ok: %s matches Altirra %s libretro metadata expectations\n' \
    "$INFO_FILE" "$PROJECT_VERSION"
