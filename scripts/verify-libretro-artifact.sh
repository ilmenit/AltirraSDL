#!/usr/bin/env bash
#
# Verify a built Altirra libretro artifact and its sidecar metadata.

set -euo pipefail

CORE="${1:-}"
INFO_FILE="${2:-}"
CMAKE_FILE="${3:-CMakeLists.txt}"
TEMP_FILES=()

cleanup() {
    if [ "${#TEMP_FILES[@]}" -gt 0 ]; then
        rm -f "${TEMP_FILES[@]}"
    fi
}

trap cleanup EXIT

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

warn() {
    printf 'warning: %s\n' "$*" >&2
}

info_value() {
    local key="$1"

    sed -n \
        "s/^${key}[[:space:]]*=[[:space:]]*\"\\(.*\\)\"[[:space:]]*$/\\1/p" \
        "$INFO_FILE" | tail -1
}

[ -n "$CORE" ] || fail "usage: $0 CORE [INFO_FILE] [CMAKE_FILE] [CORE_SOURCE_FILE]"
[ -f "$CORE" ] || fail "core not found: $CORE"

CORE_DIR=$(dirname "$CORE")
CORE_NAME=$(basename "$CORE")
INFO_FILE="${INFO_FILE:-$CORE_DIR/altirra_libretro.info}"
CORE_SOURCE_FILE="${4:-src/AltirraLibretro/libretro.cpp}"

[ -f "$INFO_FILE" ] || fail "core info not found: $INFO_FILE"

case "$CORE_NAME" in
    altirra_libretro.so|altirra_libretro.dylib|altirra_libretro.dll) ;;
    *) fail "unexpected core filename: $CORE_NAME" ;;
esac

bash "$(dirname "$0")/validate-libretro-info.sh" \
    "$INFO_FILE" "$CMAKE_FILE" "$CORE_SOURCE_FILE"

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

extract_pe_exports() {
    local output_file="$1"
    shift

    "$@" -p "$CORE" | awk '
        /^[[:space:]]*Ordinal[[:space:]]+RVA[[:space:]]+Name[[:space:]]*$/ {
            in_export_names = 1
            next
        }
        in_export_names && /^[[:space:]]*[0-9]+[[:space:]]+0x[0-9A-Fa-f]+[[:space:]]+/ {
            print $NF
            next
        }
        in_export_names && /^[^[:space:]]/ {
            in_export_names = 0
        }
        /^[[:space:]]*\[[[:space:]]*[0-9]+\][[:space:]]+/ {
            print $NF
        }
    ' | sed 's/^_//' | sed '/^$/d' | sort -u > "$output_file"

    [ -s "$output_file" ]
}

check_runtime_system_info() {
    command -v python3 >/dev/null 2>&1 || {
        warn "python3 not found; skipping runtime system-info check"
        return 0
    }

    command -v file >/dev/null 2>&1 || {
        warn "file not found; skipping runtime system-info check"
        return 0
    }

    case "$(uname -m)" in
        x86_64|amd64|AMD64) ;;
        *)
            warn "host architecture is not x86_64; skipping runtime system-info check"
            return 0
            ;;
    esac

    if ! file "$CORE" | grep -Eq 'ELF 64-bit.*x86-64'; then
        warn "core is not a host-loadable x86_64 ELF; skipping runtime system-info check"
        return 0
    fi

    local expected_extensions
    expected_extensions=$(info_value supported_extensions)
    [ -n "$expected_extensions" ] \
        || fail "core info missing supported_extensions: $INFO_FILE"

    python3 - "$CORE" "$expected_extensions" <<'PY'
import ctypes
import sys

core_path = sys.argv[1]
expected_extensions = sys.argv[2].encode("ascii")

class RetroSystemInfo(ctypes.Structure):
    _fields_ = [
        ("library_name", ctypes.c_char_p),
        ("library_version", ctypes.c_char_p),
        ("valid_extensions", ctypes.c_char_p),
        ("need_fullpath", ctypes.c_bool),
        ("block_extract", ctypes.c_bool),
    ]

lib = ctypes.CDLL(core_path)
retro_get_system_info = lib.retro_get_system_info
retro_get_system_info.argtypes = [ctypes.POINTER(RetroSystemInfo)]
retro_get_system_info.restype = None

info = RetroSystemInfo()
retro_get_system_info(ctypes.byref(info))

if info.library_name != b"Altirra":
    raise SystemExit(
        "unexpected retro_system_info.library_name: %r" % (info.library_name,))

if not info.library_version:
    raise SystemExit("missing retro_system_info.library_version")

if not info.valid_extensions:
    raise SystemExit("missing retro_system_info.valid_extensions")

if info.valid_extensions != expected_extensions:
    raise SystemExit(
        "retro_system_info.valid_extensions mismatch: %r != %r"
        % (info.valid_extensions, expected_extensions))

extensions = set(info.valid_extensions.decode("ascii").split("|"))
for ext in ("atr", "car", "xex", "cas", "m3u"):
    if ext not in extensions:
        raise SystemExit(
            "retro_system_info.valid_extensions missing %s: %r"
            % (ext, info.valid_extensions))

if not info.need_fullpath:
    raise SystemExit("retro_system_info.need_fullpath must be true")

if info.block_extract:
    raise SystemExit("retro_system_info.block_extract must be false")
PY
}

check_system_info_codegen() {
    local objdump_tool="${OBJDUMP:-objdump}"

    command -v "$objdump_tool" >/dev/null 2>&1 || {
        warn "$objdump_tool not found; skipping retro_get_system_info codegen check"
        return 0
    }

    command -v file >/dev/null 2>&1 || {
        warn "file not found; skipping retro_get_system_info codegen check"
        return 0
    }

    if ! file "$CORE" | grep -Eq 'ELF 64-bit.*x86-64'; then
        warn "core is not an x86_64 ELF; skipping retro_get_system_info codegen check"
        return 0
    fi

    local symbol_addr
    symbol_addr=$("$NM_TOOL" -D --defined-only "$CORE" \
        | awk '$3 == "retro_get_system_info" { print "0x"$1; exit }')

    [ -n "$symbol_addr" ] \
        || fail "cannot locate retro_get_system_info for codegen check"

    local stop_addr
    stop_addr=$(printf '0x%x' "$((symbol_addr + 128))")

    local disasm_file
    disasm_file=$(mktemp)
    TEMP_FILES+=("$disasm_file")

    "$objdump_tool" -d --start-address="$symbol_addr" --stop-address="$stop_addr" \
        "$CORE" > "$disasm_file"

    if grep -Eq 'movq[[:space:]]+.*\(%rip\),[[:space:]]*%xmm' "$disasm_file"; then
        fail "retro_get_system_info uses an RIP-relative XMM pointer-table load; this has caused invalid system-info pointers during RetroArch shutdown"
    fi
}

check_elf_identity() {
    local readelf_tool="$1"

    local expected_class="${ALTIRRA_LIBRETRO_EXPECT_ELF_CLASS:-}"
    local expected_machine="${ALTIRRA_LIBRETRO_EXPECT_ELF_MACHINE:-}"

    if [ -z "$expected_class" ] && [ -z "$expected_machine" ]; then
        return 0
    fi

    command -v "$readelf_tool" >/dev/null 2>&1 \
        || fail "$readelf_tool not found; cannot verify expected ELF identity"

    local elf_header
    elf_header=$("$readelf_tool" -h "$CORE" 2>/dev/null) \
        || fail "failed to read ELF header from $CORE"

    if [ -n "$expected_class" ]; then
        printf '%s\n' "$elf_header" \
            | grep -Eq "^[[:space:]]*Class:[[:space:]]*$expected_class([[:space:]]|\$)" \
            || fail "ELF class mismatch for $CORE (expected $expected_class)"
    fi

    if [ -n "$expected_machine" ]; then
        printf '%s\n' "$elf_header" \
            | grep -Eq "^[[:space:]]*Machine:[[:space:]]*$expected_machine([[:space:]]|\$)" \
            || fail "ELF machine mismatch for $CORE (expected $expected_machine)"
    fi
}

case "$CORE_NAME" in
    *.so)
        NM_TOOL="${NM:-nm}"
        command -v "$NM_TOOL" >/dev/null 2>&1 \
            || fail "$NM_TOOL not found; cannot verify ELF exports"
        SYMBOLS_FILE=$(mktemp)
        TEMP_FILES+=("$SYMBOLS_FILE")
        "$NM_TOOL" -D --defined-only "$CORE" | awk '{ print $3 }' \
            | sed '/^$/d' | sort > "$SYMBOLS_FILE"
        check_symbols "$SYMBOLS_FILE"

        READELF_TOOL="${READELF:-readelf}"
        check_elf_identity "$READELF_TOOL"
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

        check_runtime_system_info
        check_system_info_codegen
        ;;

    *.dylib)
        if command -v nm >/dev/null 2>&1; then
            SYMBOLS_FILE=$(mktemp)
            TEMP_FILES+=("$SYMBOLS_FILE")
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
        SYMBOLS_FILE=$(mktemp)
        TEMP_FILES+=("$SYMBOLS_FILE")

        LLVM_OBJDUMP_TOOL="${LLVM_OBJDUMP:-llvm-objdump}"
        OBJDUMP_TOOL="${OBJDUMP:-objdump}"

        if command -v "$LLVM_OBJDUMP_TOOL" >/dev/null 2>&1 \
            && extract_pe_exports "$SYMBOLS_FILE" "$LLVM_OBJDUMP_TOOL"; then
            check_symbols "$SYMBOLS_FILE"
        elif command -v "$OBJDUMP_TOOL" >/dev/null 2>&1 \
            && extract_pe_exports "$SYMBOLS_FILE" "$OBJDUMP_TOOL"; then
            check_symbols "$SYMBOLS_FILE"
        else
            warn "llvm-objdump/objdump PE export table not available; skipping PE export check"
        fi

        if command -v "$OBJDUMP_TOOL" >/dev/null 2>&1; then
            PE_DYNAMIC_FILE=$(mktemp)
            TEMP_FILES+=("$PE_DYNAMIC_FILE")

            if "$OBJDUMP_TOOL" -p "$CORE" > "$PE_DYNAMIC_FILE" 2>/dev/null; then
                if grep -Eiq '^[[:space:]]*DLL Name: (SDL3|libSDL3|asound|libasound)' \
                    "$PE_DYNAMIC_FILE"; then
                    fail "unexpected SDL3/ALSA dependency in $CORE"
                fi
            else
                warn "$OBJDUMP_TOOL could not inspect PE dependencies; skipping PE dependency check"
            fi
        else
            warn "objdump not found; skipping PE dependency check"
        fi
        ;;
esac

printf 'ok: %s passes libretro artifact checks\n' "$CORE"
