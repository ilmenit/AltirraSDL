#!/usr/bin/env bash
#
# Run a real RetroArch/libretro frontend smoke test for Altirra.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PACKAGE_PATH=""
CORE_PATH=""
INFO_PATH=""
OUT_DIR=""
FRAMES=120
TIMEOUT_SECONDS=60
RETROARCH_MODE="auto"
VIDEO_DRIVER="null"
INPUT_DRIVER="null"
VERIFY_PACKAGE=0
KEEP_TMP=1
CONTENT_SMOKE=1

fail() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

usage() {
	cat <<USAGE
Usage: $(basename "$0") [options]

Options:
  --package FILE       Extract and test an AltirraLibretro package.
  --core FILE          Test a built altirra_libretro shared library.
  --info FILE          Core Info file to install with --core.
  --output-dir DIR     Directory for the isolated RetroArch config and logs.
  --frames N           Number of frontend frames to run (default: $FRAMES).
  --timeout SECONDS    Wall-clock timeout for RetroArch launch (default: $TIMEOUT_SECONDS).
  --retroarch MODE     auto, native, or flatpak (default: auto).
  --video-driver NAME  RetroArch video driver for the isolated config (default: $VIDEO_DRIVER).
  --input-driver NAME  RetroArch input driver for the isolated config (default: $INPUT_DRIVER).
  --no-content-only    Only run the no-content frontend smoke.
  --verify-package     Verify --package before launching RetroArch.
  -h, --help           Show this help.

The smoke test creates an isolated retroarch.cfg, installs only the selected
Altirra core and .info file into that config, launches RetroArch with
--max-frames, and checks that the frontend loaded and unloaded the core without
fatal log errors. By default it also launches generated .xex, .atr, .a52, .cas,
.m3u, and .zip smoke fixtures. It defaults to null video/input drivers for
deterministic automation. Use explicit drivers such as --video-driver gl
--input-driver udev for a visible frontend run. It is a smoke test only; keep
short manual notes for any device/controller checks you run.
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

while [ $# -gt 0 ]; do
	case "$1" in
		--package) PACKAGE_PATH="$(need_value "$@")"; shift ;;
		--core) CORE_PATH="$(need_value "$@")"; shift ;;
		--info) INFO_PATH="$(need_value "$@")"; shift ;;
		--output-dir) OUT_DIR="$(need_value "$@")"; shift ;;
		--frames) FRAMES="$(need_value "$@")"; shift ;;
		--timeout) TIMEOUT_SECONDS="$(need_value "$@")"; shift ;;
		--retroarch) RETROARCH_MODE="$(need_value "$@")"; shift ;;
		--video-driver) VIDEO_DRIVER="$(need_value "$@")"; shift ;;
		--input-driver) INPUT_DRIVER="$(need_value "$@")"; shift ;;
		--no-content-only) CONTENT_SMOKE=0 ;;
		--verify-package) VERIFY_PACKAGE=1 ;;
		--help|-h) usage; exit 0 ;;
		*) fail "unknown argument: $1" ;;
	esac
	shift
done

case "$RETROARCH_MODE" in
	auto|native|flatpak) ;;
	*) fail "--retroarch must be auto, native, or flatpak" ;;
esac

case "$FRAMES" in
	*[!0-9]*|"") fail "--frames must be a positive integer" ;;
esac
[ "$FRAMES" -gt 0 ] || fail "--frames must be a positive integer"

case "$TIMEOUT_SECONDS" in
	*[!0-9]*|"") fail "--timeout must be a positive integer" ;;
esac
[ "$TIMEOUT_SECONDS" -gt 0 ] || fail "--timeout must be a positive integer"
[ -n "$VIDEO_DRIVER" ] || fail "--video-driver requires a non-empty value"
[ -n "$INPUT_DRIVER" ] || fail "--input-driver requires a non-empty value"

[ -z "$PACKAGE_PATH" ] || [ -z "$CORE_PATH" ] \
	|| fail "--package and --core are mutually exclusive"

if [ -z "$PACKAGE_PATH" ] && [ -z "$CORE_PATH" ]; then
	if [ -d "$ROOT_DIR/build" ]; then
		PACKAGE_PATH=$(find "$ROOT_DIR/build" -type f \
			\( -name 'AltirraLibretro-*.tar.gz' -o -name 'AltirraLibretro-*.zip' \) \
			2>/dev/null | sort | tail -1)
	fi
	[ -n "$PACKAGE_PATH" ] \
		|| fail "no package found under build/; pass --package or --core/--info"
fi

if [ -n "$PACKAGE_PATH" ]; then
	PACKAGE_PATH=$(absolute_path "$PACKAGE_PATH")
	[ -f "$PACKAGE_PATH" ] || fail "package not found: $PACKAGE_PATH"
	if [ "$VERIFY_PACKAGE" = "1" ]; then
		bash "$ROOT_DIR/scripts/verify-libretro-package.sh" "$PACKAGE_PATH"
	fi
else
	CORE_PATH=$(absolute_path "$CORE_PATH")
	[ -f "$CORE_PATH" ] || fail "core not found: $CORE_PATH"
	[ -n "$INFO_PATH" ] || fail "--core requires --info"
	INFO_PATH=$(absolute_path "$INFO_PATH")
	[ -f "$INFO_PATH" ] || fail "core info not found: $INFO_PATH"
fi

if [ -z "$OUT_DIR" ]; then
	STAMP=$(date -u +%Y%m%d-%H%M%S)
	OUT_DIR="$ROOT_DIR/build/libretro-smoke/retroarch-smoke-$STAMP"
fi
mkdir -p "$OUT_DIR"
OUT_DIR=$(absolute_path "$OUT_DIR")

TMP_EXTRACT="$OUT_DIR/package"
INSTALL_DIR="$OUT_DIR/install"
LOG_DIR="$OUT_DIR/logs"
CORES_DIR="$INSTALL_DIR/cores"
INFO_DIR="$INSTALL_DIR/info"
SYSTEM_DIR="$INSTALL_DIR/system"
SAVES_DIR="$INSTALL_DIR/saves"
STATES_DIR="$INSTALL_DIR/states"
CONFIG_DIR="$INSTALL_DIR/config"
CACHE_DIR="$INSTALL_DIR/cache"
PLAYLIST_DIR="$INSTALL_DIR/playlists"
RUNTIME_LOG_DIR="$INSTALL_DIR/runtime"
CORE_OPTIONS_DIR="$INSTALL_DIR/core-options"
FIXTURE_DIR="$INSTALL_DIR/fixtures"
CONFIG_FILE="$INSTALL_DIR/retroarch.cfg"
COREDUMP_LOG="$LOG_DIR/coredumpctl-after.log"
SUMMARY_FILE="$OUT_DIR/summary.md"

mkdir -p "$TMP_EXTRACT" "$CORES_DIR" "$INFO_DIR" "$SYSTEM_DIR" \
	"$SAVES_DIR" "$STATES_DIR" "$CONFIG_DIR" "$CACHE_DIR" \
	"$PLAYLIST_DIR" "$RUNTIME_LOG_DIR" "$CORE_OPTIONS_DIR" "$FIXTURE_DIR" \
	"$LOG_DIR"

if [ -n "$PACKAGE_PATH" ]; then
	case "$PACKAGE_PATH" in
		*.tar.gz|*.tgz) tar -xzf "$PACKAGE_PATH" -C "$TMP_EXTRACT" ;;
		*.zip)
			command -v unzip >/dev/null 2>&1 \
				|| fail "unzip not found; cannot extract package"
			unzip -q "$PACKAGE_PATH" -d "$TMP_EXTRACT"
			;;
		*) fail "unsupported package extension: $PACKAGE_PATH" ;;
	esac

	PKG_ROOTS=()
	while IFS= read -r line; do
		PKG_ROOTS+=("$line")
	done < <(find "$TMP_EXTRACT" -mindepth 1 -maxdepth 1 -type d | sort)
	[ "${#PKG_ROOTS[@]}" -eq 1 ] \
		|| fail "package should contain exactly one top-level directory"

	PKG_DIR="${PKG_ROOTS[0]}"
	CORE_PATH=$(find "$PKG_DIR" -maxdepth 1 -type f \
		\( -name 'altirra_libretro.so' \
		-o -name 'altirra_libretro.dylib' \
		-o -name 'altirra_libretro.dll' \) | sort | head -1)
	INFO_PATH="$PKG_DIR/altirra_libretro.info"
	[ -n "$CORE_PATH" ] && [ -f "$CORE_PATH" ] \
		|| fail "package does not contain an altirra_libretro core"
	[ -f "$INFO_PATH" ] || fail "package does not contain altirra_libretro.info"
fi

case "$(basename "$CORE_PATH")" in
	altirra_libretro.*) ;;
	*) fail "core basename must be altirra_libretro.<so|dylib|dll>" ;;
esac

cp "$CORE_PATH" "$CORES_DIR/"
cp "$INFO_PATH" "$INFO_DIR/altirra_libretro.info"
INSTALLED_CORE="$CORES_DIR/$(basename "$CORE_PATH")"

cat > "$CONFIG_FILE" <<EOF
libretro_directory = "$CORES_DIR"
libretro_info_path = "$INFO_DIR"
system_directory = "$SYSTEM_DIR"
savefile_directory = "$SAVES_DIR"
savestate_directory = "$STATES_DIR"
core_options_path = "$CORE_OPTIONS_DIR/Altirra.opt"
global_core_options = "true"
cache_directory = "$CACHE_DIR"
playlist_directory = "$PLAYLIST_DIR"
runtime_log_directory = "$RUNTIME_LOG_DIR"
content_favorites_path = "$PLAYLIST_DIR/content_favorites.lpl"
content_history_path = "$PLAYLIST_DIR/content_history.lpl"
content_image_history_path = "$PLAYLIST_DIR/content_image_history.lpl"
content_music_history_path = "$PLAYLIST_DIR/content_music_history.lpl"
content_video_history_path = "$PLAYLIST_DIR/content_video_history.lpl"
log_dir = "$LOG_DIR"
configuration_settings = "true"
config_save_on_exit = "false"
content_runtime_log = "false"
content_runtime_log_aggregate = "false"
history_list_enable = "false"
video_driver = "$VIDEO_DRIVER"
input_driver = "$INPUT_DRIVER"
video_fullscreen = "false"
pause_nonactive = "false"
EOF

write_smoke_xex() {
	local path="$1"

	# Loads a tiny program at $2000 that stores $42 in CH and then loops.
	printf '%b' \
		'\377\377\000\040\007\040\251\102\215\374\002\114\005\040\340\002\341\002\000\040' \
		>"$path"
}

write_smoke_atr() {
	local path="$1"
	local sectors=720
	local sector_size=128
	local image_bytes=$((sectors * sector_size))
	local paragraphs=$((image_bytes / 16))

	{
		printf '%b' "\\226\\002$(printf '\\%03o' $((paragraphs & 255)))$(printf '\\%03o' $(((paragraphs >> 8) & 255)))$(printf '\\%03o' $((sector_size & 255)))$(printf '\\%03o' $(((sector_size >> 8) & 255)))\\000\\000\\000\\000\\000\\000\\000\\000\\000\\000"
		dd if=/dev/zero bs="$image_bytes" count=1 2>/dev/null
	} >"$path"
}

write_smoke_a52() {
	local path="$1"
	local rom="$FIXTURE_DIR/smoke-a52-rom.bin"
	local checksum
	local b0
	local b1
	local b2
	local b3

	dd if=/dev/zero bs=8192 count=1 of="$rom" 2>/dev/null
	checksum=0
	b0=$(((checksum >> 24) & 255))
	b1=$(((checksum >> 16) & 255))
	b2=$(((checksum >> 8) & 255))
	b3=$((checksum & 255))

	{
		printf 'CART'
		printf '%b' "\\000\\000\\000\\023$(printf '\\%03o' "$b0")$(printf '\\%03o' "$b1")$(printf '\\%03o' "$b2")$(printf '\\%03o' "$b3")\\000\\000\\000\\000"
		cat "$rom"
	} >"$path"
	rm -f "$rom"
}

write_smoke_cas() {
	local path="$1"

	{
		# CAS chunks are 4-byte ID, 16-bit little-endian length, and two
		# auxiliary bytes. Use a short standard-data block at 600 baud.
		printf 'FUJI'
		printf '%b' '\000\000\000\000'
		printf 'baud'
		printf '%b' '\000\000\130\002'
		printf 'data'
		printf '%b' '\004\000\000\000\125\125\372\000'
	} >"$path"
}

write_smoke_m3u() {
	local path="$1"
	local disk_path="$2"

	printf '%s\n' "$(basename "$disk_path")" >"$path"
}

write_smoke_zip() {
	local path="$1"
	local content_path="$2"

	command -v zip >/dev/null 2>&1 \
		|| fail "zip not found; cannot create compressed content smoke fixture"
	(
		cd "$(dirname "$content_path")"
		zip -q -X "$path" "$(basename "$content_path")"
	)
}

RETROARCH_CMD=()
RETROARCH_LABEL=""
if { [ "$RETROARCH_MODE" = "auto" ] || [ "$RETROARCH_MODE" = "native" ]; } \
	&& command -v retroarch >/dev/null 2>&1; then
	RETROARCH_CMD=(retroarch)
	RETROARCH_LABEL="native: $(command -v retroarch)"
elif { [ "$RETROARCH_MODE" = "auto" ] || [ "$RETROARCH_MODE" = "flatpak" ]; } \
	&& command -v flatpak >/dev/null 2>&1 \
	&& flatpak info org.libretro.RetroArch >/dev/null 2>&1; then
	RETROARCH_CMD=(flatpak run org.libretro.RetroArch)
	RETROARCH_LABEL="Flatpak: org.libretro.RetroArch"
else
	fail "RetroArch not found for mode: $RETROARCH_MODE"
fi

CASE_SUMMARY=""

run_case() {
	local name="$1"
	local content_path="${2:-}"
	local retroarch_log="$LOG_DIR/${name}.log"
	local stdout_log="$LOG_DIR/${name}-stdout.log"
	local args=(
		"${RETROARCH_CMD[@]}" -v
		"--log-file=$retroarch_log"
		-c "$CONFIG_FILE"
		-L "$INSTALLED_CORE"
		"--max-frames=$FRAMES"
	)

	if [ -n "$content_path" ]; then
		args+=("$content_path")
	fi

	set +e
	if command -v timeout >/dev/null 2>&1; then
		timeout --foreground --kill-after=5 "$TIMEOUT_SECONDS" \
			"${args[@]}" >"$stdout_log" 2>&1
	else
		"${args[@]}" >"$stdout_log" 2>&1
	fi
	local retroarch_status=$?
	set -e

	[ "$retroarch_status" -ne 124 ] \
		|| fail "RetroArch timed out in case '$name' after ${TIMEOUT_SECONDS}s; see $stdout_log and $retroarch_log"
	[ "$retroarch_status" -eq 0 ] \
		|| fail "RetroArch exited with status $retroarch_status in case '$name'; see $stdout_log and $retroarch_log"
	[ -f "$retroarch_log" ] \
		|| fail "RetroArch log was not written for case '$name': $retroarch_log"

	grep -Fq "[Core] Loading dynamic libretro core from:" "$retroarch_log" \
		|| fail "RetroArch log does not show core load for case '$name'"
	grep -Fq "[Environ] SET_SUPPORT_NO_GAME: yes." "$retroarch_log" \
		|| fail "RetroArch log does not show no-content support for case '$name'"
	grep -Fq "[Environ] SET_DISK_CONTROL_EXT_INTERFACE." "$retroarch_log" \
		|| fail "RetroArch log does not show disk-control registration for case '$name'"
	grep -Fq "[Core] Geometry:" "$retroarch_log" \
		|| fail "RetroArch log does not show geometry initialization for case '$name'"
	if [ -n "$content_path" ]; then
		local content_base
		local content_stem
		content_base=$(basename "$content_path")
		content_stem="${content_base%.*}"

		grep -Fq "[Content] Content loading skipped. Implementation will load it on its own." "$retroarch_log" \
			|| fail "RetroArch log does not show content handoff for case '$name'"
		grep -Fq "/${content_stem}.srm\"" "$retroarch_log" \
			|| fail "RetroArch log does not show content-specific save redirect for case '$name'"
	fi
	grep -Fq "[Core] Unloading game..." "$retroarch_log" \
		|| fail "RetroArch log does not show game unload for case '$name'"
	grep -Fq "[Core] Unloading core..." "$retroarch_log" \
		|| fail "RetroArch log does not show core unload for case '$name'"
	grep -Fq "[Core] Unloading core symbols..." "$retroarch_log" \
		|| fail "RetroArch log does not show core symbol unload for case '$name'"

	if grep -Eiq '\[(ERROR|FATAL)\]|Fatal error|Segmentation fault|core dumped|AddressSanitizer|UndefinedBehaviorSanitizer' \
		"$retroarch_log" "$stdout_log"; then
		fail "RetroArch logs contain fatal/error diagnostics for case '$name'; see $retroarch_log and $stdout_log"
	fi

	if grep -Fq "$HOME/.var/app/org.libretro.RetroArch/config/retroarch" \
		"$retroarch_log" "$stdout_log" \
		|| grep -Fq "$HOME/.config/retroarch" "$retroarch_log" "$stdout_log"; then
		fail "RetroArch log references the normal user config profile for case '$name'; isolated config paths are incomplete"
	fi

	CASE_SUMMARY="${CASE_SUMMARY}- ${name}: pass (${retroarch_log})"$'\n'
}

START_TIME=$(date -u '+%Y-%m-%d %H:%M:%S UTC')
run_case no-content

if [ "$CONTENT_SMOKE" = "1" ]; then
	XEX_FIXTURE="$FIXTURE_DIR/smoke.xex"
	ATR_FIXTURE="$FIXTURE_DIR/smoke.atr"
	A52_FIXTURE="$FIXTURE_DIR/smoke.a52"
	CAS_FIXTURE="$FIXTURE_DIR/smoke.cas"
	M3U_FIXTURE="$FIXTURE_DIR/smoke.m3u"
	ZIP_FIXTURE="$FIXTURE_DIR/smoke.zip"

	write_smoke_xex "$XEX_FIXTURE"
	write_smoke_atr "$ATR_FIXTURE"
	write_smoke_a52 "$A52_FIXTURE"
	write_smoke_cas "$CAS_FIXTURE"
	write_smoke_m3u "$M3U_FIXTURE" "$ATR_FIXTURE"
	write_smoke_zip "$ZIP_FIXTURE" "$XEX_FIXTURE"

	run_case executable-xex "$XEX_FIXTURE"
	run_case disk-atr "$ATR_FIXTURE"
	run_case cartridge-a52 "$A52_FIXTURE"
	run_case cassette-cas "$CAS_FIXTURE"
	run_case playlist-m3u "$M3U_FIXTURE"
	run_case compressed-zip "$ZIP_FIXTURE"
fi

COREDUMP_STATUS="not checked"
if command -v coredumpctl >/dev/null 2>&1; then
	if coredumpctl --no-pager --since "$START_TIME" list retroarch \
		>"$COREDUMP_LOG" 2>&1; then
		if grep -Eq 'retroarch|org\.libretro\.RetroArch' "$COREDUMP_LOG"; then
			fail "coredumpctl reports a new RetroArch coredump; see $COREDUMP_LOG"
		fi
		COREDUMP_STATUS="none"
	else
		if grep -Fq "No coredumps found." "$COREDUMP_LOG"; then
			COREDUMP_STATUS="none"
		else
			COREDUMP_STATUS="coredumpctl unavailable or permission denied"
		fi
	fi
fi

cat > "$SUMMARY_FILE" <<EOF
# Altirra Libretro RetroArch Smoke

- Result: pass
- Date: $(date -u +%Y-%m-%d)
- RetroArch: $RETROARCH_LABEL
- Frames: $FRAMES
- Timeout: ${TIMEOUT_SECONDS}s
- Video driver: $VIDEO_DRIVER
- Input driver: $INPUT_DRIVER
- Package: ${PACKAGE_PATH:-n/a}
- Core: $INSTALLED_CORE
- Core Info: $INFO_DIR/altirra_libretro.info
- Config: $CONFIG_FILE
- Log directory: $LOG_DIR
- Coredump check: $COREDUMP_STATUS

Cases:

$CASE_SUMMARY
Verified log markers:

- Core load
- No-content support
- Disk-control interface registration
- Geometry initialization
- Content handoff and content-specific save path for content cases
- Game unload
- Core unload
- Core symbol unload
EOF

if [ "$KEEP_TMP" != "1" ]; then
	rm -rf "$TMP_EXTRACT"
fi

printf 'ok: RetroArch smoke passed\n'
printf 'summary: %s\n' "$SUMMARY_FILE"
printf 'logs: %s\n' "$LOG_DIR"
