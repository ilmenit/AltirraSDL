#!/usr/bin/env bash
#
# Verify a built Altirra libretro artifact and its sidecar metadata.

set -euo pipefail

CORE="${1:-}"
INFO_FILE="${2:-}"
CMAKE_FILE="${3:-CMakeLists.txt}"

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

warn() {
    printf 'warning: %s\n' "$*" >&2
}

[ -n "$CORE" ] || fail "usage: $0 CORE [INFO_FILE] [CMAKE_FILE]"
[ -f "$CORE" ] || fail "core not found: $CORE"

CORE_DIR=$(dirname "$CORE")
CORE_NAME=$(basename "$CORE")
INFO_FILE="${INFO_FILE:-$CORE_DIR/altirra_libretro.info}"

[ -f "$INFO_FILE" ] || fail "core info not found: $INFO_FILE"

case "$CORE_NAME" in
    altirra_libretro.so|altirra_libretro.dylib|altirra_libretro.dll) ;;
    *) fail "unexpected core filename: $CORE_NAME" ;;
esac

bash "$(dirname "$0")/validate-libretro-info.sh" "$INFO_FILE" "$CMAKE_FILE"

REQUIRED_SYMBOLS=(
    retro_api_version
    retro_cheat_reset
    retro_cheat_set
    retro_deinit
    retro_get_memory_data
    retro_get_memory_size
    retro_get_region
    retro_get_system_av_info
    retro_get_system_info
    retro_init
    retro_load_game
    retro_load_game_special
    retro_reset
    retro_run
    retro_serialize
    retro_serialize_size
    retro_set_audio_sample
    retro_set_audio_sample_batch
    retro_set_controller_port_device
    retro_set_environment
    retro_set_input_poll
    retro_set_input_state
    retro_set_video_refresh
    retro_unload_game
    retro_unserialize
)

check_symbols() {
    local symbols_file="$1"

    for symbol in "${REQUIRED_SYMBOLS[@]}"; do
        grep -Fxq "$symbol" "$symbols_file" \
            || fail "missing exported libretro symbol: $symbol"
    done

    if grep -Ev '^retro_' "$symbols_file" | grep -q .; then
        fail "unexpected non-libretro exports: $(grep -Ev '^retro_' "$symbols_file" | tr '\n' ' ')"
    fi
}

case "$CORE_NAME" in
    *.so)
        NM_TOOL="${NM:-nm}"
        command -v "$NM_TOOL" >/dev/null 2>&1 \
            || fail "$NM_TOOL not found; cannot verify ELF exports"
        SYMBOLS_FILE=$(mktemp)
        trap 'rm -f "$SYMBOLS_FILE"' EXIT
        "$NM_TOOL" -D --defined-only "$CORE" | awk '{ print $3 }' \
            | sed '/^$/d' | sort > "$SYMBOLS_FILE"
        check_symbols "$SYMBOLS_FILE"

        READELF_TOOL="${READELF:-readelf}"
        if command -v "$READELF_TOOL" >/dev/null 2>&1; then
            if "$READELF_TOOL" -d "$CORE" 2>/dev/null \
                | grep -Eiq 'Shared library: \[(libSDL3|libasound)'; then
                fail "unexpected SDL3/ALSA dependency in $CORE"
            fi
        elif command -v ldd >/dev/null 2>&1; then
            if ldd "$CORE" 2>/dev/null | grep -Eiq '(^|/|[[:space:]])(libSDL3|libasound)'; then
                fail "unexpected SDL3/ALSA dependency in $CORE"
            fi
        else
            warn "readelf/ldd not found; skipping ELF dependency check"
        fi
        ;;

    *.dylib)
        if command -v nm >/dev/null 2>&1; then
            SYMBOLS_FILE=$(mktemp)
            trap 'rm -f "$SYMBOLS_FILE"' EXIT
            nm -gU "$CORE" | awk '{ print $3 }' \
                | sed 's/^_//' | sed '/^$/d' | sort > "$SYMBOLS_FILE"
            check_symbols "$SYMBOLS_FILE"
        else
            warn "nm not found; skipping Mach-O export check"
        fi

        if command -v otool >/dev/null 2>&1; then
            if otool -L "$CORE" | grep -Eiq '(^|/|[[:space:]])SDL3'; then
                fail "unexpected SDL3 dependency in $CORE"
            fi
        else
            warn "otool not found; skipping Mach-O dependency check"
        fi
        ;;

    *.dll)
        LLVM_NM_TOOL="${LLVM_NM:-llvm-nm}"
        if command -v "$LLVM_NM_TOOL" >/dev/null 2>&1; then
            SYMBOLS_FILE=$(mktemp)
            trap 'rm -f "$SYMBOLS_FILE"' EXIT
            "$LLVM_NM_TOOL" --defined-only "$CORE" | awk '{ print $3 }' \
                | sed '/^$/d' | sort > "$SYMBOLS_FILE"
            check_symbols "$SYMBOLS_FILE"
        else
            warn "$LLVM_NM_TOOL not found; skipping PE export check"
        fi
        ;;
esac

printf 'ok: %s passes libretro artifact checks\n' "$CORE"
