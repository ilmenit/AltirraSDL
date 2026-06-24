#!/usr/bin/env bash
#
# Validate the local Altirra libretro docs draft against current metadata.

set -euo pipefail

DOC_FILE="${1:-docs/libretro-docs-altirra-draft.md}"
INFO_FILE="${2:-src/AltirraLibretro/altirra_libretro.info}"
CORE_FILE="${3:-src/AltirraLibretro/libretro.cpp}"

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

extract_info_value() {
    local key="$1"
    sed -n "s/^${key}[[:space:]]*=[[:space:]]*\"\\(.*\\)\"[[:space:]]*$/\\1/p" \
        "$INFO_FILE" | head -1
}

[ -f "$DOC_FILE" ] || fail "docs draft not found: $DOC_FILE"
[ -f "$INFO_FILE" ] || fail "info file not found: $INFO_FILE"
[ -f "$CORE_FILE" ] || fail "core source not found: $CORE_FILE"

DISPLAY_NAME=$(extract_info_value display_name)
AUTHORS=$(extract_info_value authors)
LICENSE=$(extract_info_value license)
DATABASES=$(extract_info_value database)
SUPPORTED_EXTENSIONS=$(extract_info_value supported_extensions)
FIRMWARE_COUNT=$(sed -n \
    's/^firmware_count[[:space:]]*=[[:space:]]*\([0-9][0-9]*\)[[:space:]]*$/\1/p' \
    "$INFO_FILE" | head -1)

[ -n "$DISPLAY_NAME" ] || fail "missing display_name in $INFO_FILE"
[ -n "$AUTHORS" ] || fail "missing authors in $INFO_FILE"
[ -n "$LICENSE" ] || fail "missing license in $INFO_FILE"
[ -n "$DATABASES" ] || fail "missing database in $INFO_FILE"
[ -n "$SUPPORTED_EXTENSIONS" ] \
    || fail "missing supported_extensions in $INFO_FILE"
[ -n "$FIRMWARE_COUNT" ] || fail "missing firmware_count in $INFO_FILE"

grep -Fxq "# $DISPLAY_NAME" "$DOC_FILE" \
    || fail "docs draft title does not match display_name"

if [[ "$AUTHORS" == *", fork of "* ]]; then
    AUTHOR_PRIMARY="${AUTHORS%%, fork of *}"
    AUTHOR_FORK="${AUTHORS#*, fork of }"

    grep -Fxq -- "- $AUTHOR_PRIMARY" "$DOC_FILE" \
        || fail "docs draft missing primary author: $AUTHOR_PRIMARY"
    grep -Fxq -- "- Fork of $AUTHOR_FORK" "$DOC_FILE" \
        || fail "docs draft missing fork attribution: $AUTHOR_FORK"
else
    grep -Fxq -- "- $AUTHORS" "$DOC_FILE" \
        || fail "docs draft missing author: $AUTHORS"
fi

grep -Fxq -- "- $LICENSE" "$DOC_FILE" \
    || fail "docs draft missing license: $LICENSE"

IFS='|' read -r -a EXTENSIONS <<< "$SUPPORTED_EXTENSIONS"
for ext in "${EXTENSIONS[@]}"; do
    grep -Fxq -- "- \`.$ext\`" "$DOC_FILE" \
        || fail "docs draft missing extension: .$ext"
done

IFS='|' read -r -a DATABASE_LIST <<< "$DATABASES"
for database in "${DATABASE_LIST[@]}"; do
    grep -Fxq -- "- $database" "$DOC_FILE" \
        || fail "docs draft missing database: $database"
done

for ((I = 0; I < FIRMWARE_COUNT; ++I)); do
    path=$(extract_info_value "firmware${I}_path")
    [ -n "$path" ] || fail "missing firmware${I}_path"

    grep -Fq "\`$path\`" "$DOC_FILE" \
        || fail "docs draft missing firmware path: $path"
done

mapfile -t OPTION_NAMES < <(
    sed -n '/static const CompactOptionDefinition kOptionSpecs\[\]/,/};/p' \
        "$CORE_FILE" \
        | sed -n 's/^[[:space:]]*"\([^"]*\)",[[:space:]]*"\([^"]*\)".*/\2/p'
)

[ "${#OPTION_NAMES[@]}" -gt 0 ] \
    || fail "could not extract core option names from $CORE_FILE"

for option_name in "${OPTION_NAMES[@]}"; do
    awk -v option_name="$option_name" '
        /^## Core Options$/ { in_options = 1; next }
        /^## / && in_options { exit }
        in_options && index($0, "| " option_name " |") == 1 { found = 1 }
        END { exit found ? 0 : 1 }
    ' "$DOC_FILE" \
        || fail "docs draft missing core option row: $option_name"
done

OPTION_DEFAULTS=()
IN_OPTION_SPECS=0
CURRENT_OPTION_NAME=""
while IFS= read -r line || [ -n "$line" ]; do
    if [ "$IN_OPTION_SPECS" = "0" ]; then
        [[ "$line" == *"static const CompactOptionDefinition kOptionSpecs[]"* ]] \
            && IN_OPTION_SPECS=1
        continue
    fi

    [[ "$line" =~ ^\}\; ]] && break

    if [[ "$line" =~ ^[[:space:]]*\"[^\"]+\",[[:space:]]*\"([^\"]+)\" ]]; then
        CURRENT_OPTION_NAME="${BASH_REMATCH[1]}"
        continue
    fi

    if [ -n "$CURRENT_OPTION_NAME" ] \
        && [[ "$line" =~ ^[[:space:]]*nullptr,[[:space:]]*\"[^\"]+\",[[:space:]]*[A-Za-z0-9_]+,[[:space:]]*\"([^\"]+)\" ]]; then
        OPTION_DEFAULTS+=("${CURRENT_OPTION_NAME}"$'\t'"${BASH_REMATCH[1]}")
        CURRENT_OPTION_NAME=""
    fi
done < "$CORE_FILE"

[ "${#OPTION_DEFAULTS[@]}" -gt 0 ] \
    || fail "could not extract core option defaults from $CORE_FILE"

for option_default in "${OPTION_DEFAULTS[@]}"; do
    option_name="${option_default%%	*}"
    expected_default="${option_default#*	}"
    row=$(awk -v option_name="$option_name" '
        /^## Core Options$/ { in_options = 1; next }
        /^## / && in_options { exit }
        in_options && index($0, "| " option_name " |") == 1 {
            print
            exit
        }
    ' "$DOC_FILE")
    [ -n "$row" ] || fail "docs draft missing core option row: $option_name"

    IFS='|' read -r _ _ documented_values documented_default _ <<< "$row"
    documented_default="${documented_default#"${documented_default%%[![:space:]]*}"}"
    documented_default="${documented_default%"${documented_default##*[![:space:]]}"}"

    [ "$documented_default" = "$expected_default" ] \
        || fail "docs draft default for '$option_name' is '$documented_default' (expected '$expected_default')"
done

for descriptor in \
    "Joystick Up" \
    "Joystick 2 Up" \
    "Paddle Knob" \
    "Mouse X" \
    "Light Gun X" \
    "Pointer X"; do
    grep -Fq "$descriptor" "$DOC_FILE" \
        || fail "docs draft missing input descriptor: $descriptor"
done

printf 'ok: %s matches current libretro metadata/docs expectations\n' \
    "$DOC_FILE"
