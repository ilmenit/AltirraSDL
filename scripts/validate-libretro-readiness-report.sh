#!/usr/bin/env bash
#
# Validate a completed Altirra libretro RetroArch readiness report.

set -euo pipefail

REPORT=""
SELF_TEST=0

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<USAGE
Usage: $(basename "$0") REPORT.md
       $(basename "$0") --self-test

Options:
  --self-test         Run validator self-tests.
  -h, --help          Show this help.
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --self-test) SELF_TEST=1 ;;
        --help|-h) usage; exit 0 ;;
        --*) fail "unknown argument: $1" ;;
        *)
            [ -z "$REPORT" ] || fail "multiple report files specified"
            REPORT="$1"
            ;;
    esac
    shift
done

is_positive() {
    case "$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')" in
        yes|pass|passed|ok|true) return 0 ;;
        *) return 1 ;;
    esac
}

is_none() {
    case "$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')" in
        ""|none|n/a|na|no) return 0 ;;
        *) return 1 ;;
    esac
}

is_placeholder() {
    case "$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')" in
        "not selected"|"not detected"|"not run"|"yes / no"|\
        "native / flatpak / steam / other"|\
        "flatpak installed, version not reported") return 0 ;;
        *) return 1 ;;
    esac
}

field_value() {
    local label="$1"
    awk -v prefix="- ${label}: " \
        'index($0, prefix) == 1 { print substr($0, length(prefix) + 1); exit }' \
        "$REPORT"
}

require_nonempty_field() {
    local label="$1"
    local value
    value=$(field_value "$label")

    [ -n "$value" ] \
        || fail "missing or empty report field: $label"
    ! is_placeholder "$value" \
        || fail "report field still contains placeholder value: $label (got '$value')"
}

require_positive_field() {
    local label="$1"
    local value
    value=$(field_value "$label")

    [ -n "$value" ] \
        || fail "missing report field: $label"
    is_positive "$value" \
        || fail "report field must be yes/pass/ok: $label (got '$value')"
}

validate_report() {
    [ -n "$REPORT" ] \
        || fail "usage: $0 REPORT.md"
    [ -f "$REPORT" ] \
        || fail "readiness report not found: $REPORT"

    for label in \
    Date \
    Tester \
        "Git commit" \
        "Altirra version" \
        "Build command" \
        "Smoke test command" \
        "Package path" \
        "Package verifier command" \
        "Host OS and version" \
    "CPU architecture" \
    "RetroArch distribution" \
    "RetroArch version" \
    "RetroArch video driver" \
    "RetroArch audio driver" \
    "Core path" \
    "Core Info path"; do
        require_nonempty_field "$label"
    done

    require_positive_field "Smoke test result"
    require_positive_field "Package verifier result"
    require_positive_field '`altirra_libretro.info` installed'
    require_positive_field "Core Information page shows display name"
    require_positive_field "Core Information page shows author"
    require_positive_field "Core Information page shows firmware list"
    require_positive_field "Core Information page shows supported extensions"
    require_positive_field "Core Information page shows save-state support"
    require_positive_field 'Ready for `is_experimental = "false"`'
    require_positive_field 'Ready for `libretro-super` `.info` PR'
    require_positive_field 'Ready for `libretro/docs` PR'

    blocking=$(field_value "Blocking issues")
    is_none "$blocking" \
        || fail "Blocking issues must be empty or none/n/a before readiness signoff"

    if ! grep -Fq "passes libretro package checks" "$REPORT"; then
        fail "package verifier output is missing successful package check"
    fi

    if ! grep -Fq "RetroArch smoke summary:" "$REPORT"; then
        fail "RetroArch smoke summary section is missing"
    fi

    if ! grep -Fq -- "- Result: pass" "$REPORT"; then
        fail "RetroArch smoke summary is missing a passing result"
    fi

    for smoke_case in \
    "no-content" \
    "executable-xex" \
    "disk-atr" \
    "cartridge-a52" \
    "cassette-cas" \
    "playlist-m3u" \
    "compressed-zip"; do
        if ! grep -Fq -- "- ${smoke_case}: pass" "$REPORT"; then
            fail "RetroArch smoke summary is missing passing case: $smoke_case"
        fi
    done

    if ! grep -Fq "Content handoff and content-specific save path for content cases" "$REPORT"; then
        fail "RetroArch smoke summary is missing content handoff verification"
    fi

require_table_row_positive() {
    local row_name="$1"
    local row
    row=$(grep -F "| $row_name |" "$REPORT" | head -1 || true)
    [ -n "$row" ] || fail "missing table row: $row_name"

    IFS='|' read -r _ col1 col2 col3 col4 col5 col6 col7 _ <<< "$row"
    col1="${col1#"${col1%%[![:space:]]*}"}"; col1="${col1%"${col1##*[![:space:]]}"}"
    col2="${col2#"${col2%%[![:space:]]*}"}"; col2="${col2%"${col2##*[![:space:]]}"}"
    col3="${col3#"${col3%%[![:space:]]*}"}"; col3="${col3%"${col3##*[![:space:]]}"}"
    col4="${col4#"${col4%%[![:space:]]*}"}"; col4="${col4%"${col4##*[![:space:]]}"}"
    col5="${col5#"${col5%%[![:space:]]*}"}"; col5="${col5%"${col5##*[![:space:]]}"}"
    col6="${col6#"${col6%%[![:space:]]*}"}"; col6="${col6%"${col6##*[![:space:]]}"}"
    col7="${col7#"${col7%%[![:space:]]*}"}"; col7="${col7%"${col7##*[![:space:]]}"}"

    [ -n "$col2" ] || fail "table row has empty file/value column: $row_name"
    is_positive "$col3" || fail "table row Load/Pass column is not positive: $row_name"
    is_positive "$col4" || fail "table row Run/Pass column is not positive: $row_name"
    is_positive "$col5" || fail "table row Reset/Pass column is not positive: $row_name"
    is_positive "$col6" \
        || fail "table row Close Content column is not positive: $row_name"
    is_positive "$col7" \
        || fail "table row Exit RetroArch column is not positive: $row_name"
}

    for row_name in \
    "No-content boot" \
    Executable \
    Disk \
    Cartridge \
    Cassette \
    Playlist \
    "Compressed content"; do
        require_table_row_positive "$row_name"
    done

    for feature in \
    "Save state creates successfully" \
    "Save state loads successfully" \
    "Save state survives close/reload content" \
    "Core options can change before loading content" \
    "Core options can change while content is running" \
    "Video standard option changes geometry/timing as expected" \
    "Input Port 1 joystick works" \
    "Keyboard focus works for Atari keyboard input" \
    "Disk Control interface opens and reports media" \
    "Disk Control can eject, swap, and remount media" \
    "Disk Control rejects media-list changes while tray is closed" \
    "Audio is present and stable" \
    "RetroArch logs contain no Altirra errors" \
    "No coredump or frontend crash produced" \
    "Alt+F4 / window close exits without crash"; do
        row=$(grep -F "| $feature |" "$REPORT" | head -1 || true)
        [ -n "$row" ] || fail "missing runtime feature row: $feature"
        IFS='|' read -r _ _ pass _ <<< "$row"
        pass="${pass#"${pass%%[![:space:]]*}"}"
        pass="${pass%"${pass##*[![:space:]]}"}"
        is_positive "$pass" \
            || fail "runtime feature is not marked positive: $feature"
    done

    for check in \
    "D-pad drives joystick input" \
    "Left analog drives joystick input" \
    "START console key reachable from pad" \
    "SELECT console key reachable from pad" \
    "OPTION console key reachable from pad" \
    "Virtual keyboard opens and closes from pad" \
    "Virtual keyboard types Atari keys from pad" \
    "Virtual keyboard page switching works from pad" \
    "5200 keypad page works for 5200 content" \
    "Warm reset binding works from pad" \
    "Cold reset binding works from pad" \
    "Spare button key mappings can be changed from core options"; do
        row=$(grep -F "| $check |" "$REPORT" | head -1 || true)
        [ -n "$row" ] || fail "missing gamepad UX row: $check"
        IFS='|' read -r _ _ pass _ <<< "$row"
        pass="${pass#"${pass%%[![:space:]]*}"}"
        pass="${pass%"${pass##*[![:space:]]}"}"
        is_positive "$pass" \
            || fail "gamepad UX check is not marked positive: $check"
    done

    printf 'ok: %s passes libretro readiness report checks\n' "$REPORT"
}

run_self_test() {
    local tmpdir
    local valid
    local invalid

    tmpdir=$(mktemp -d)
    valid="$tmpdir/valid.md"
    invalid="$tmpdir/invalid.md"
    trap 'rm -rf "$tmpdir"' EXIT

    cat > "$valid" <<'REPORT'
# Altirra Libretro Readiness Test Report

## Build Under Test

- Date: 2026-06-24
- Tester: self-test
- Git commit: 0000000000000000000000000000000000000000
- Altirra version: 4.40
- Build command: ./build.sh --libretro --package
- Smoke test command: ./build.sh --libretro --libretro-test
- Smoke test result: pass
- Package path: /tmp/AltirraLibretro-4.40-linux-x86_64.tar.gz
- Package verifier command: bash scripts/verify-libretro-package.sh /tmp/AltirraLibretro-4.40-linux-x86_64.tar.gz
- Package verifier result: pass
- Host OS and version: self-test
- CPU architecture: x86_64
- RetroArch distribution: Flatpak
- RetroArch version: 1.22.2
- RetroArch video driver: gl
- RetroArch audio driver: pulse

## Installed Files

- Core path: /tmp/altirra_libretro.so
- Core Info path: /tmp/altirra_libretro.info
- `altirra_libretro.info` installed: yes
- Core Information page shows display name: yes
- Core Information page shows author: yes
- Core Information page shows firmware list: yes
- Core Information page shows supported extensions: yes
- Core Information page shows save-state support: yes

## Content Matrix

| Type | File | Load | Run 60s | Reset | Close Content | Exit RetroArch | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| No-content boot | none | pass | pass | pass | pass | pass | |
| Executable | smoke.xex | pass | pass | pass | pass | pass | |
| Disk | smoke.atr | pass | pass | pass | pass | pass | |
| Cartridge | smoke.car | pass | pass | pass | pass | pass | |
| Cassette | smoke.cas | pass | pass | pass | pass | pass | |
| Playlist | smoke.m3u | pass | pass | pass | pass | pass | |
| Compressed content | smoke.zip | pass | pass | pass | pass | pass | |

## Runtime Features

| Feature | Pass | Notes |
| --- | --- | --- |
| Save state creates successfully | pass | |
| Save state loads successfully | pass | |
| Save state survives close/reload content | pass | |
| Core options can change before loading content | pass | |
| Core options can change while content is running | pass | |
| Video standard option changes geometry/timing as expected | pass | |
| Input Port 1 joystick works | pass | |
| Keyboard focus works for Atari keyboard input | pass | |
| Disk Control interface opens and reports media | pass | |
| Disk Control can eject, swap, and remount media | pass | |
| Disk Control rejects media-list changes while tray is closed | pass | |
| Audio is present and stable | pass | |
| RetroArch logs contain no Altirra errors | pass | |
| No coredump or frontend crash produced | pass | |
| Alt+F4 / window close exits without crash | pass | |

## Gamepad UX

| Check | Pass | Notes |
| --- | --- | --- |
| D-pad drives joystick input | pass | |
| Left analog drives joystick input | pass | |
| START console key reachable from pad | pass | |
| SELECT console key reachable from pad | pass | |
| OPTION console key reachable from pad | pass | |
| Virtual keyboard opens and closes from pad | pass | |
| Virtual keyboard types Atari keys from pad | pass | |
| Virtual keyboard page switching works from pad | pass | |
| 5200 keypad page works for 5200 content | pass | |
| Warm reset binding works from pad | pass | |
| Cold reset binding works from pad | pass | |
| Spare button key mappings can be changed from core options | pass | |

## Logs And Diagnostics

ok: /tmp/AltirraLibretro-4.40-linux-x86_64.tar.gz passes libretro package checks

RetroArch smoke summary:

- Summary path: /tmp/retroarch-smoke/summary.md
- Result: pass

```text
# Altirra Libretro RetroArch Smoke

- Result: pass
- Date: 2026-06-28
- RetroArch: Flatpak: org.libretro.RetroArch
- Frames: 120
- Timeout: 30s
- Video driver: null
- Input driver: null
- Package: /tmp/AltirraLibretro-4.40-linux-x86_64.tar.gz
- Core: /tmp/retroarch-smoke/install/cores/altirra_libretro.so
- Core Info: /tmp/retroarch-smoke/install/info/altirra_libretro.info
- Config: /tmp/retroarch-smoke/install/retroarch.cfg
- Log directory: /tmp/retroarch-smoke/logs
- Coredump check: none

Cases:

- no-content: pass (/tmp/retroarch-smoke/logs/no-content.log)
- executable-xex: pass (/tmp/retroarch-smoke/logs/executable-xex.log)
- disk-atr: pass (/tmp/retroarch-smoke/logs/disk-atr.log)
- cartridge-a52: pass (/tmp/retroarch-smoke/logs/cartridge-a52.log)
- cassette-cas: pass (/tmp/retroarch-smoke/logs/cassette-cas.log)
- playlist-m3u: pass (/tmp/retroarch-smoke/logs/playlist-m3u.log)
- compressed-zip: pass (/tmp/retroarch-smoke/logs/compressed-zip.log)

Verified log markers:

- Core load
- No-content support
- Disk-control interface registration
- Geometry initialization
- Content handoff and content-specific save path for content cases
- Game unload
- Core unload
- Core symbol unload
```

## Verdict

- Ready for `is_experimental = "false"`: yes
- Ready for `libretro-super` `.info` PR: yes
- Ready for `libretro/docs` PR: yes
- Blocking issues: none
- Follow-up issues: none
REPORT

    sed 's/- Package verifier result: pass/- Package verifier result: not run/' \
        "$valid" > "$invalid"

    REPORT="$invalid"
    if ( validate_report ) >/dev/null 2>&1; then
        fail "self-test expected incomplete report to fail"
    fi

    awk '
        /^RetroArch smoke summary:/ { skip = 1; next }
        skip && /^## Verdict/ { skip = 0 }
        !skip { print }
    ' "$valid" > "$invalid"

    REPORT="$invalid"
    if ( validate_report ) >/dev/null 2>&1; then
        fail "self-test expected report without RetroArch smoke summary to fail"
    fi

    REPORT="$valid"
    ( validate_report ) >/dev/null

    rm -rf "$tmpdir"
    trap - EXIT

    printf 'ok: readiness report validator self-test passed\n'
}

if [ "$SELF_TEST" = "1" ]; then
    run_self_test
else
    validate_report
fi
