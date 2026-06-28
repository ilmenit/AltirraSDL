#!/usr/bin/env bash
#
# Create a tester-facing kit for Altirra libretro manual/device readiness.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PACKAGE_PATH=""
SMOKE_SUMMARY=""
REPORT_PATH=""
OUT_DIR=""

fail() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

usage() {
	cat <<USAGE
Usage: $(basename "$0") --package FILE --retroarch-smoke-summary FILE [options]

Options:
  --package FILE                 Package under test.
  --retroarch-smoke-summary FILE Summary from run-libretro-retroarch-smoke.sh.
  --report FILE                  Prefilled readiness report to include.
  --output-dir DIR               Output directory for the manual test kit.
  -h, --help                     Show this help.

The kit copies the package, generated smoke fixtures, automated smoke summary,
and an optional prefilled readiness report into one directory with a manual
visible-session/device checklist. It does not mark readiness as complete.
USAGE
}

need_value() {
	[ $# -gt 1 ] || fail "$1 requires a value"
	[ -n "$2" ] || fail "$1 requires a non-empty value"
	printf '%s\n' "$2"
}

absolute_path() {
	local path="$1"
	local dir
	local base

	dir=$(dirname "$path")
	base=$(basename "$path")

	[ -d "$dir" ] || fail "directory not found: $dir"
	printf '%s/%s\n' "$(cd "$dir" && pwd)" "$base"
}

field_value() {
	local file="$1"
	local label="$2"

	awk -v prefix="- ${label}: " \
		'index($0, prefix) == 1 { print substr($0, length(prefix) + 1); exit }' \
		"$file"
}

while [ $# -gt 0 ]; do
	case "$1" in
		--package) PACKAGE_PATH="$(need_value "$@")"; shift ;;
		--retroarch-smoke-summary) SMOKE_SUMMARY="$(need_value "$@")"; shift ;;
		--report) REPORT_PATH="$(need_value "$@")"; shift ;;
		--output-dir) OUT_DIR="$(need_value "$@")"; shift ;;
		--help|-h) usage; exit 0 ;;
		*) fail "unknown argument: $1" ;;
	esac
	shift
done

[ -n "$PACKAGE_PATH" ] || fail "--package is required"
[ -n "$SMOKE_SUMMARY" ] || fail "--retroarch-smoke-summary is required"

PACKAGE_PATH=$(absolute_path "$PACKAGE_PATH")
SMOKE_SUMMARY=$(absolute_path "$SMOKE_SUMMARY")
[ -f "$PACKAGE_PATH" ] || fail "package not found: $PACKAGE_PATH"
[ -f "$SMOKE_SUMMARY" ] || fail "RetroArch smoke summary not found: $SMOKE_SUMMARY"

if [ -n "$REPORT_PATH" ]; then
	REPORT_PATH=$(absolute_path "$REPORT_PATH")
	[ -f "$REPORT_PATH" ] || fail "readiness report not found: $REPORT_PATH"
fi

SMOKE_DIR=$(dirname "$SMOKE_SUMMARY")
FIXTURE_DIR="$SMOKE_DIR/install/fixtures"
[ -d "$FIXTURE_DIR" ] || fail "smoke fixture directory not found: $FIXTURE_DIR"

for fixture in smoke.xex smoke.atr smoke.a52 smoke.cas smoke.m3u smoke.zip; do
	[ -f "$FIXTURE_DIR/$fixture" ] \
		|| fail "smoke fixture missing: $FIXTURE_DIR/$fixture"
done

SMOKE_RESULT=$(field_value "$SMOKE_SUMMARY" "Result")
[ "$SMOKE_RESULT" = "pass" ] \
	|| fail "RetroArch smoke summary is not passing: $SMOKE_SUMMARY"

if [ -z "$OUT_DIR" ]; then
	STAMP=$(date -u +%Y%m%d-%H%M%S)
	OUT_DIR="$ROOT_DIR/build/libretro-readiness/manual-kit-$STAMP"
fi
mkdir -p "$OUT_DIR"
OUT_DIR=$(absolute_path "$OUT_DIR")

mkdir -p "$OUT_DIR/package" "$OUT_DIR/fixtures" "$OUT_DIR/evidence"
cp "$PACKAGE_PATH" "$OUT_DIR/package/"
cp "$FIXTURE_DIR"/smoke.* "$OUT_DIR/fixtures/"
cp "$FIXTURE_DIR/smoke.atr" "$OUT_DIR/fixtures/smoke-swap.atr"
cp "$SMOKE_SUMMARY" "$OUT_DIR/evidence/retroarch-smoke-summary.md"
if [ -n "$REPORT_PATH" ]; then
	cp "$REPORT_PATH" "$OUT_DIR/evidence/readiness-report.md"
fi

PACKAGE_NAME=$(basename "$PACKAGE_PATH")
REPORT_NOTE="No prefilled report was included. Generate one with scripts/create-libretro-readiness-report.sh after the visible/device pass."
if [ -n "$REPORT_PATH" ]; then
	REPORT_NOTE="Prefilled report: evidence/readiness-report.md"
fi

cat > "$OUT_DIR/README.md" <<EOF
# Altirra Libretro Manual Readiness Kit

This kit is for visible-session and device readiness testing of the Altirra
libretro core. It complements the automated RetroArch smoke test; it does not
replace manual signoff.

## Contents

- Package under test: \`package/$PACKAGE_NAME\`
- Generated test media: \`fixtures/\` (\`smoke-swap.atr\` is a second disk
  fixture for Disk Control swap/remount checks)
- Automated smoke summary: \`evidence/retroarch-smoke-summary.md\`
- $REPORT_NOTE

## Install

Install the package into the RetroArch instance under test. For packaged builds,
use the included installer from the extracted package so both the core and
\`altirra_libretro.info\` are installed.

For Flatpak RetroArch, use a Flatpak-runtime package. For native RetroArch, use
a package built for that native runtime.

## Visible/Device Matrix

Use a visible RetroArch session on the target device. For each row, load the
listed file from \`fixtures/\`, let it run for at least 60 seconds, trigger
Reset from the visible UI/input path, Close Content, then fully exit RetroArch.
Record pass/fail and notes in the readiness report.

| Report row | Test file |
| --- | --- |
| No-content boot | Start Altirra with no content |
| Executable | \`fixtures/smoke.xex\` |
| Disk | \`fixtures/smoke.atr\` |
| Cartridge | \`fixtures/smoke.a52\` |
| Cassette | \`fixtures/smoke.cas\` |
| Playlist | \`fixtures/smoke.m3u\` |
| Compressed content | \`fixtures/smoke.zip\` |

## Runtime Feature Checks

Record these in the readiness report:

- Save state creates, loads, and survives Close Content/reload.
- Core options can change before loading content and while content is running.
- Video standard option changes geometry/timing as expected.
- Input Port 1 joystick works.
- Atari keyboard input works without relying on RetroArch keyboard focus.
- Disk Control opens and reports media.
- Disk Control can eject, swap to another fixture disk, and remount media.
- Disk Control rejects media-list changes while the tray is closed.
- Audio is present and stable.
- Logs contain no Altirra errors.
- No coredump or frontend crash is produced.
- Alt+F4/window close exits without crash on desktop-visible sessions.

Disk Control check procedure:

1. Load \`fixtures/smoke.atr\`.
2. Open RetroArch's Disk Control UI and confirm the current disk is listed.
3. Eject the tray, replace or append \`fixtures/smoke-swap.atr\`, select it,
   then close the tray and confirm content continues running.
4. With the tray closed, try to append or replace media again. The frontend
   should reject the change or leave the media list unchanged.

## Gamepad UX Checks

On a controller-only device or with keyboard unplugged/ignored:

- D-pad and left analog both drive joystick input.
- START, SELECT, and OPTION are reachable from the pad.
- Virtual keyboard opens, types keys, switches pages, and closes cleanly.
- 5200 keypad page is reachable for 5200 content.
- Warm reset and cold reset bindings work.
- Spare button key mappings can be changed from core options.

Record these checks in the readiness report's Gamepad UX table.

## Evidence

Attach RetroArch logs and coredump checks to the report. Completed reports must
pass:

\`\`\`sh
bash scripts/validate-libretro-readiness-report.sh REPORT.md
\`\`\`
EOF

printf 'ok: wrote manual readiness kit: %s\n' "$OUT_DIR"
