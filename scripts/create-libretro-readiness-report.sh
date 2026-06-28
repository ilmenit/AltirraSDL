#!/usr/bin/env bash
#
# Create a prefilled Altirra libretro RetroArch readiness report.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_FILE=""
PACKAGE_PATH=""
BUILD_COMMAND="./build.sh --libretro --package"
SMOKE_COMMAND="not run"
SMOKE_RESULT="not run"
RETROARCH_SMOKE_SUMMARY=""
RETROARCH_SMOKE_OUTPUT=""
RETROARCH_COMMAND=""
RETROARCH_COMMAND_PROVIDED=0
VERIFY_PACKAGE=0
PACKAGE_VERIFY_RESULT=""

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

absolute_path() {
    local path="$1"
    local dir
    local base

    dir=$(dirname "$path")
    base=$(basename "$path")

    if [ -d "$dir" ]; then
        printf '%s/%s\n' "$(cd "$dir" && pwd)" "$base"
    else
        printf '%s\n' "$path"
    fi
}

expand_user_path() {
    local path="$1"

    case "$path" in
        "~") printf '%s\n' "$HOME" ;;
        "~/"*) printf '%s/%s\n' "$HOME" "${path#\~/}" ;;
        *) printf '%s\n' "$path" ;;
    esac
}

config_value() {
    local config_file="$1"
    local key="$2"

    [ -f "$config_file" ] || return 0

    sed -n \
        "s/^${key}[[:space:]]*=[[:space:]]*\"\\(.*\\)\"[[:space:]]*$/\\1/p" \
        "$config_file" | tail -1
}

yes_no_for_file() {
    local path="$1"

    if [ -n "$path" ] && [ -f "$path" ]; then
        printf 'yes\n'
    else
        printf 'no\n'
    fi
}

usage() {
    cat <<USAGE
Usage: $(basename "$0") [options]

Options:
  --output FILE          Write the report to FILE.
  --package FILE         Record FILE as the package under test.
  --build-command TEXT   Record TEXT as the build command.
  --smoke-command TEXT   Record TEXT as the smoke test command.
  --smoke-result TEXT    Record the smoke test result.
  --retroarch-smoke-summary FILE
                         Record FILE from run-libretro-retroarch-smoke.sh.
  --retroarch-command C  Record C as the RetroArch command/distribution.
  --verify-package       Run the package verifier and record its result.
  -h, --help             Show this help.

The default output path is build/libretro-readiness/<timestamp>.md.
USAGE
}

need_value() {
    [ $# -gt 1 ] || fail "$1 requires a value"
    [ -n "$2" ] || fail "$1 requires a non-empty value"
    printf '%s\n' "$2"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --output) OUT_FILE="$(need_value "$@")"; shift ;;
        --package) PACKAGE_PATH="$(need_value "$@")"; shift ;;
        --build-command) BUILD_COMMAND="$(need_value "$@")"; shift ;;
        --smoke-command) SMOKE_COMMAND="$(need_value "$@")"; shift ;;
        --smoke-result) SMOKE_RESULT="$(need_value "$@")"; shift ;;
        --retroarch-smoke-summary)
            RETROARCH_SMOKE_SUMMARY="$(need_value "$@")"
            shift
            ;;
        --retroarch-command)
            RETROARCH_COMMAND="$(need_value "$@")"
            RETROARCH_COMMAND_PROVIDED=1
            shift
            ;;
        --verify-package) VERIFY_PACKAGE=1 ;;
        --help|-h) usage; exit 0 ;;
        *) fail "unknown argument: $1" ;;
    esac
    shift
done

PROJECT_VERSION=$(sed -n \
    's/^project(Altirra[[:space:]]\+VERSION[[:space:]]\+\([^[:space:])]*\).*/\1/p' \
    "$ROOT_DIR/CMakeLists.txt" | head -1)
[ -n "$PROJECT_VERSION" ] || PROJECT_VERSION="unknown"

COMMIT_FULL=$(git -C "$ROOT_DIR" rev-parse HEAD 2>/dev/null || printf unknown)
COMMIT_SHORT=$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || printf unknown)
DATE_UTC=$(date -u +%Y-%m-%d)
STAMP=$(date -u +%Y%m%d-%H%M%S)

if [ -z "$OUT_FILE" ]; then
    OUT_FILE="$ROOT_DIR/build/libretro-readiness/AltirraLibretro-${PROJECT_VERSION}-${COMMIT_SHORT}-${STAMP}.md"
fi

if [ -n "$RETROARCH_SMOKE_SUMMARY" ]; then
    RETROARCH_SMOKE_SUMMARY=$(absolute_path "$RETROARCH_SMOKE_SUMMARY")
    [ -f "$RETROARCH_SMOKE_SUMMARY" ] \
        || fail "RetroArch smoke summary not found: $RETROARCH_SMOKE_SUMMARY"
    if grep -Fq -- "- Result: pass" "$RETROARCH_SMOKE_SUMMARY"; then
        RETROARCH_SMOKE_RESULT="pass"
    else
        RETROARCH_SMOKE_RESULT="fail"
    fi
    RETROARCH_SMOKE_OUTPUT=$(cat "$RETROARCH_SMOKE_SUMMARY")

    if [ "$SMOKE_COMMAND" = "not run" ]; then
        SMOKE_COMMAND="bash scripts/run-libretro-retroarch-smoke.sh --package PACKAGE --verify-package"
    fi
    if [ "$SMOKE_RESULT" = "not run" ]; then
        SMOKE_RESULT="$RETROARCH_SMOKE_RESULT"
    fi
else
    RETROARCH_SMOKE_SUMMARY="not run"
    RETROARCH_SMOKE_RESULT="not run"
fi

if [ -z "$PACKAGE_PATH" ]; then
    if [ -d "$ROOT_DIR/build" ]; then
        PACKAGE_PATH=$(find "$ROOT_DIR/build" -type f \
            \( -name 'AltirraLibretro-*.tar.gz' -o -name 'AltirraLibretro-*.zip' \) \
            2>/dev/null | sort | tail -1)
    fi
fi

if [ -n "$PACKAGE_PATH" ]; then
    PACKAGE_PATH=$(absolute_path "$PACKAGE_PATH")
    PACKAGE_VERIFY_COMMAND="bash scripts/verify-libretro-package.sh \"$PACKAGE_PATH\""
    if [ "$VERIFY_PACKAGE" = "1" ]; then
        VERIFY_OUTPUT=""
        if VERIFY_OUTPUT=$(cd "$ROOT_DIR" \
            && bash scripts/verify-libretro-package.sh "$PACKAGE_PATH" 2>&1); then
            PACKAGE_VERIFY_RESULT="pass"
        else
            PACKAGE_VERIFY_RESULT="fail"
        fi
    else
        PACKAGE_VERIFY_RESULT="not run"
    fi
else
    if [ "$VERIFY_PACKAGE" = "1" ]; then
        fail "--verify-package requires --package or an existing AltirraLibretro package under build/"
    fi
    PACKAGE_PATH="not selected"
    PACKAGE_VERIFY_COMMAND="not run"
    PACKAGE_VERIFY_RESULT="not run"
fi

if [ "$RETROARCH_SMOKE_SUMMARY" != "not run" ] \
    && [ "$SMOKE_COMMAND" = "bash scripts/run-libretro-retroarch-smoke.sh --package PACKAGE --verify-package" ]; then
    SMOKE_COMMAND="bash scripts/run-libretro-retroarch-smoke.sh --package \"$PACKAGE_PATH\" --verify-package"
fi

UNAME_S=$(uname -s)
HOST_OS=$(uname -srmo 2>/dev/null || uname -a)
if [ -f /etc/os-release ]; then
    OS_PRETTY=$(sed -n 's/^PRETTY_NAME="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' \
        /etc/os-release | head -1)
    [ -n "$OS_PRETTY" ] && HOST_OS="$OS_PRETTY ($(uname -srmo))"
fi

ARCH=$(uname -m)

if [ -z "$RETROARCH_COMMAND" ]; then
    if command -v retroarch >/dev/null 2>&1; then
        RETROARCH_COMMAND="native: $(command -v retroarch)"
    elif command -v flatpak >/dev/null 2>&1 \
        && flatpak info org.libretro.RetroArch >/dev/null 2>&1; then
        RETROARCH_COMMAND="Flatpak: org.libretro.RetroArch"
    else
        RETROARCH_COMMAND="not detected"
    fi
fi

RETROARCH_VERSION="not detected"
RETROARCH_CONFIG=""
if printf '%s' "$RETROARCH_COMMAND" | grep -Eiq '^flatpak:' \
    && command -v flatpak >/dev/null 2>&1 \
    && flatpak info org.libretro.RetroArch >/dev/null 2>&1; then
    RETROARCH_VERSION=$(flatpak info org.libretro.RetroArch 2>/dev/null \
        | sed -n 's/^[[:space:]]*Version:[[:space:]]*//p' | head -1)
    [ -n "$RETROARCH_VERSION" ] \
        || RETROARCH_VERSION="Flatpak installed, version not reported"
    FLATPAK_CONFIG="$HOME/.var/app/org.libretro.RetroArch/config/retroarch/retroarch.cfg"
    [ -f "$FLATPAK_CONFIG" ] && RETROARCH_CONFIG="$FLATPAK_CONFIG"
elif { [ "$RETROARCH_COMMAND_PROVIDED" = "0" ] \
        || printf '%s' "$RETROARCH_COMMAND" | grep -Eiq '^native:'; } \
    && command -v retroarch >/dev/null 2>&1; then
    RETROARCH_VERSION=$(retroarch --version 2>/dev/null | head -1 \
        || printf 'not detected')
    [ -f "$HOME/.config/retroarch/retroarch.cfg" ] \
        && RETROARCH_CONFIG="$HOME/.config/retroarch/retroarch.cfg"
elif [ "$RETROARCH_COMMAND_PROVIDED" = "0" ] \
    && command -v flatpak >/dev/null 2>&1 \
    && flatpak info org.libretro.RetroArch >/dev/null 2>&1; then
    RETROARCH_VERSION=$(flatpak info org.libretro.RetroArch 2>/dev/null \
        | sed -n 's/^[[:space:]]*Version:[[:space:]]*//p' | head -1)
    [ -n "$RETROARCH_VERSION" ] \
        || RETROARCH_VERSION="Flatpak installed, version not reported"
    FLATPAK_CONFIG="$HOME/.var/app/org.libretro.RetroArch/config/retroarch/retroarch.cfg"
    [ -f "$FLATPAK_CONFIG" ] && RETROARCH_CONFIG="$FLATPAK_CONFIG"
fi

RETROARCH_VIDEO_DRIVER=""
RETROARCH_AUDIO_DRIVER=""
CORE_PATH=""
CORE_INFO_PATH=""
INFO_INSTALLED="no"
if [ -n "$RETROARCH_CONFIG" ]; then
    RETROARCH_VIDEO_DRIVER=$(config_value "$RETROARCH_CONFIG" video_driver)
    RETROARCH_AUDIO_DRIVER=$(config_value "$RETROARCH_CONFIG" audio_driver)

    LIBRETRO_DIR=$(config_value "$RETROARCH_CONFIG" libretro_directory)
    LIBRETRO_INFO_DIR=$(config_value "$RETROARCH_CONFIG" libretro_info_path)

    if [ -n "$LIBRETRO_DIR" ]; then
        LIBRETRO_DIR=$(expand_user_path "$LIBRETRO_DIR")
        case "$UNAME_S" in
            Darwin) CORE_EXT=dylib ;;
            MINGW*|MSYS*|CYGWIN*) CORE_EXT=dll ;;
            *) CORE_EXT=so ;;
        esac
        CORE_PATH="$LIBRETRO_DIR/altirra_libretro.$CORE_EXT"
    fi

    if [ -n "$LIBRETRO_INFO_DIR" ]; then
        LIBRETRO_INFO_DIR=$(expand_user_path "$LIBRETRO_INFO_DIR")
        CORE_INFO_PATH="$LIBRETRO_INFO_DIR/altirra_libretro.info"
    fi

    INFO_INSTALLED=$(yes_no_for_file "$CORE_INFO_PATH")
fi

mkdir -p "$(dirname "$OUT_FILE")"

cat > "$OUT_FILE" <<REPORT
# Altirra Libretro Readiness Test Report

## Build Under Test

- Date: $DATE_UTC
- Tester:
- Git commit: $COMMIT_FULL
- Altirra version: $PROJECT_VERSION
- Build command: $BUILD_COMMAND
- Smoke test command: $SMOKE_COMMAND
- Smoke test result: $SMOKE_RESULT
- Package path: $PACKAGE_PATH
- Package verifier command: $PACKAGE_VERIFY_COMMAND
- Package verifier result: $PACKAGE_VERIFY_RESULT
- Host OS and version: $HOST_OS
- CPU architecture: $ARCH
- RetroArch distribution: $RETROARCH_COMMAND
- RetroArch version: $RETROARCH_VERSION
- RetroArch video driver: $RETROARCH_VIDEO_DRIVER
- RetroArch audio driver: $RETROARCH_AUDIO_DRIVER

## Installed Files

- Core path: $CORE_PATH
- Core Info path: $CORE_INFO_PATH
- \`altirra_libretro.info\` installed: $INFO_INSTALLED
- Core Information page shows display name: yes / no
- Core Information page shows author: yes / no
- Core Information page shows firmware list: yes / no
- Core Information page shows supported extensions: yes / no
- Core Information page shows save-state support: yes / no

## Content Matrix

Use known-good test media. Record filename, expected machine type if relevant,
and whether the core loads, runs, resets, unloads, and survives frontend exit
without errors or coredumps.

| Type | File | Load | Run 60s | Reset | Close Content | Exit RetroArch | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| No-content boot | none |  |  |  |  |  |  |
| Executable | \`.xex\` |  |  |  |  |  |  |
| Disk | \`.atr\` |  |  |  |  |  |  |
| Cartridge | \`.car\` |  |  |  |  |  |  |
| Cassette | \`.cas\` |  |  |  |  |  |  |
| Playlist | \`.m3u\` |  |  |  |  |  |  |
| Compressed content | \`.zip\` or \`.gz\` |  |  |  |  |  |  |

## Runtime Features

| Feature | Pass | Notes |
| --- | --- | --- |
| Save state creates successfully |  |  |
| Save state loads successfully |  |  |
| Save state survives close/reload content |  |  |
| Core options can change before loading content |  |  |
| Core options can change while content is running |  |  |
| Video standard option changes geometry/timing as expected |  |  |
| Input Port 1 joystick works |  |  |
| Keyboard focus works for Atari keyboard input |  |  |
| Disk Control interface opens and reports media |  |  |
| Disk Control can eject, swap, and remount media |  |  |
| Disk Control rejects media-list changes while tray is closed |  |  |
| Audio is present and stable |  |  |
| RetroArch logs contain no Altirra errors |  |  |
| No coredump or frontend crash produced |  |  |
| Alt+F4 / window close exits without crash |  |  |

## Gamepad UX

Use a controller-only setup, or ignore/unplug the keyboard after launching the
frontend. Record pass/fail for the user-facing controller path, not just the
automated libretro input API.

| Check | Pass | Notes |
| --- | --- | --- |
| D-pad drives joystick input |  |  |
| Left analog drives joystick input |  |  |
| START console key reachable from pad |  |  |
| SELECT console key reachable from pad |  |  |
| OPTION console key reachable from pad |  |  |
| Virtual keyboard opens and closes from pad |  |  |
| Virtual keyboard types Atari keys from pad |  |  |
| Virtual keyboard page switching works from pad |  |  |
| 5200 keypad page works for 5200 content |  |  |
| Warm reset binding works from pad |  |  |
| Cold reset binding works from pad |  |  |
| Spare button key mappings can be changed from core options |  |  |

## Logs And Diagnostics

Package verifier output:

\`\`\`text
${VERIFY_OUTPUT:-}
\`\`\`

RetroArch smoke summary:

- Summary path: $RETROARCH_SMOKE_SUMMARY
- Result: $RETROARCH_SMOKE_RESULT

\`\`\`text
$RETROARCH_SMOKE_OUTPUT
\`\`\`

RetroArch log location:

\`\`\`sh
find ~/.var/app/org.libretro.RetroArch/config/retroarch/logs \\
  -maxdepth 1 -type f -print -exec tail -120 {} \\;
\`\`\`

Native RetroArch log location:

\`\`\`sh
find ~/.config/retroarch/logs \\
  -maxdepth 1 -type f -print -exec tail -120 {} \\;
\`\`\`

Coredump check:

\`\`\`sh
coredumpctl list retroarch
\`\`\`

Attach relevant log excerpts here:

\`\`\`text

\`\`\`

## Verdict

- Ready for \`is_experimental = "false"\`: yes / no
- Ready for \`libretro-super\` \`.info\` PR: yes / no
- Ready for \`libretro/docs\` PR: yes / no
- Blocking issues:
- Follow-up issues:
REPORT

printf 'ok: wrote %s\n' "$OUT_FILE"

if [ "${PACKAGE_VERIFY_RESULT:-}" = "fail" ]; then
    fail "package verification failed; report was written to $OUT_FILE"
fi
