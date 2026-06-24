#!/usr/bin/env bash
# Install Altirra's libretro core and core-info metadata into RetroArch.

set -euo pipefail

die() { echo "error: $*" >&2; exit 1; }
warn() { echo "[warn] $*" >&2; }
info() { echo "[info] $*"; }
ok() { echo "[ok] $*"; }

need_value() {
	[ $# -gt 1 ] || die "$1 requires a value"
	[ -n "$2" ] || die "$1 requires a non-empty value"
	printf '%s\n' "$2"
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CORE=""
INFO_FILE="$SCRIPT_DIR/altirra_libretro.info"
CFG=""
CONFIG_DIR=""
CORE_DIR_OVERRIDE=""
INFO_DIR_OVERRIDE=""
DRY_RUN=0

case "$(uname -s)" in
	Linux*) CORE="$SCRIPT_DIR/altirra_libretro.so" ;;
	Darwin*) CORE="$SCRIPT_DIR/altirra_libretro.dylib" ;;
	MINGW*|MSYS*|CYGWIN*) CORE="$SCRIPT_DIR/altirra_libretro.dll" ;;
	*) die "unsupported platform: $(uname -s)" ;;
esac

usage() {
	cat <<USAGE
Usage: $(basename "$0") [options]

Options:
  --config FILE       Use a specific retroarch.cfg.
  --config-dir DIR    Use DIR/retroarch.cfg.
  --cores-dir DIR     Install the core into this directory.
  --info-dir DIR      Install altirra_libretro.info into this directory.
  --core FILE         Use a specific altirra_libretro shared library.
  --info FILE         Use a specific altirra_libretro.info file.
  --dry-run           Show what would be changed without writing files.
  -h, --help          Show this help.

The script reads RetroArch's configured libretro_directory and
libretro_info_path from retroarch.cfg. If the Core Info directory points into a
read-only package area such as Flatpak /app, Snap /snap, an AppImage mount, or
/usr/share, it switches only libretro_info_path to a writable user directory
and copies existing core-info files there when possible.
USAGE
}

while [ $# -gt 0 ]; do
	case "$1" in
		--config) CFG="$(need_value "$@")"; shift ;;
		--config-dir) CONFIG_DIR="$(need_value "$@")"; shift ;;
		--cores-dir) CORE_DIR_OVERRIDE="$(need_value "$@")"; shift ;;
		--info-dir) INFO_DIR_OVERRIDE="$(need_value "$@")"; shift ;;
		--core) CORE="$(need_value "$@")"; shift ;;
		--info) INFO_FILE="$(need_value "$@")"; shift ;;
		--dry-run) DRY_RUN=1 ;;
		--help|-h) usage; exit 0 ;;
		*) die "unknown argument: $1" ;;
	esac
	shift
done

[ -f "$CORE" ] || die "core not found: $CORE"
[ -f "$INFO_FILE" ] || die "core info not found: $INFO_FILE"

expand_path() {
	local p="$1"
	case "$p" in
		\~) printf '%s\n' "$HOME" ;;
		\~/*) printf '%s/%s\n' "$HOME" "${p#\~/}" ;;
		\$HOME) printf '%s\n' "$HOME" ;;
		\$HOME/*) printf '%s/%s\n' "$HOME" "${p#\$HOME/}" ;;
		*) printf '%s\n' "$p" ;;
	esac
}

add_cfg_candidate() {
	local path="$1"
	[ -n "$path" ] || return 0
	path="$(expand_path "$path")"
	for existing in "${CFG_CANDIDATES[@]:-}"; do
		[ "$existing" = "$path" ] && return 0
	done
	CFG_CANDIDATES+=("$path")
}

find_retroarch_config() {
	CFG_CANDIDATES=()

	[ -n "${RETROARCH_CONFIG:-}" ] && add_cfg_candidate "$RETROARCH_CONFIG"
	[ -n "${XDG_CONFIG_HOME:-}" ] && add_cfg_candidate "$XDG_CONFIG_HOME/retroarch/retroarch.cfg"
	add_cfg_candidate "$HOME/.config/retroarch/retroarch.cfg"
	add_cfg_candidate "$HOME/.var/app/org.libretro.RetroArch/config/retroarch/retroarch.cfg"
	add_cfg_candidate "$HOME/snap/retroarch/current/.config/retroarch/retroarch.cfg"
	add_cfg_candidate "$HOME/snap/retroarch/common/.config/retroarch/retroarch.cfg"
	add_cfg_candidate "$HOME/.local/share/Steam/steamapps/common/RetroArch/retroarch.cfg"
	add_cfg_candidate "$HOME/.steam/steam/steamapps/common/RetroArch/retroarch.cfg"
	add_cfg_candidate "$HOME/Library/Application Support/RetroArch/retroarch.cfg"
	add_cfg_candidate "$HOME/Library/Application Support/RetroArch/config/retroarch.cfg"

	for candidate in "${CFG_CANDIDATES[@]}"; do
		if [ -f "$candidate" ]; then
			printf '%s\n' "$candidate"
			return 0
		fi
	done

	return 1
}

if [ -n "$CONFIG_DIR" ]; then
	CFG="$(expand_path "$CONFIG_DIR")/retroarch.cfg"
elif [ -n "$CFG" ]; then
	CFG="$(expand_path "$CFG")"
else
	CFG="$(find_retroarch_config)" \
		|| die "retroarch.cfg not found; pass --config /path/to/retroarch.cfg"
fi

[ -f "$CFG" ] || die "retroarch.cfg not found: $CFG"
CONFIG_DIR="$(cd "$(dirname "$CFG")" && pwd)"

read_cfg_path() {
	local key="$1"
	sed -n "s/^${key}[[:space:]]*=[[:space:]]*\"\\(.*\\)\".*/\\1/p" "$CFG" | tail -1
}

quote_sed_replacement() {
	printf '%s\n' "$1" | sed 's/[\\&|]/\\&/g'
}

run_cmd() {
	if [ "$DRY_RUN" = "1" ]; then
		printf '[dry-run]'
		printf ' %q' "$@"
		printf '\n'
	else
		"$@"
	fi
}

set_cfg_path() {
	local key="$1"
	local value="$2"
	local tmp="${CFG}.tmp.$$"
	local quoted
	quoted="$(quote_sed_replacement "$value")"

	if [ "$DRY_RUN" = "1" ]; then
		info "Would set $key = \"$value\" in $CFG"
		return 0
	fi

	if grep -q "^${key}[[:space:]]*=" "$CFG"; then
		sed "s|^${key}[[:space:]]*=.*|${key} = \"${quoted}\"|" "$CFG" > "$tmp"
	else
		cp "$CFG" "$tmp"
		printf '%s = "%s"\n' "$key" "$value" >> "$tmp"
	fi

	cp "$CFG" "${CFG}.bak"
	mv "$tmp" "$CFG"
}

path_is_package_managed() {
	local raw="$1"
	local expanded="$2"

	case "$raw" in
		/app/*|/snap/*|/usr/*|/opt/*|/nix/store/*) return 0 ;;
	esac

	case "$expanded" in
		/app/*|/snap/*|/usr/*|/opt/*|/nix/store/*|/tmp/.mount_*) return 0 ;;
	esac

	return 1
}

path_is_existing_unusable_dir() {
	local path="$1"

	[ -e "$path" ] || return 1
	[ -d "$path" ] && [ -w "$path" ] && return 1
	return 0
}

ensure_writable_config_path() {
	local outvar="$1"
	local label="$2"
	local key="$3"
	local override="$4"
	local raw="$5"
	local expanded="$6"
	local fallback="$7"
	local result="$expanded"

	if [ -z "$override" ]; then
		if path_is_package_managed "$raw" "$expanded" \
			|| path_is_existing_unusable_dir "$expanded"; then
			result="$fallback"
			run_cmd mkdir -p "$result"
			set_cfg_path "$key" "$result"
			info "Using writable $label directory: $result"
		fi
	fi

	printf -v "$outvar" '%s' "$result"
}

copy_existing_info_files() {
	local old_raw="$1"
	local old_expanded="$2"
	local target="$3"

	if [ -d "$old_expanded" ]; then
		info "Copying existing core-info files from: $old_expanded"
		if [ "$DRY_RUN" = "1" ]; then
			info "Would copy: $old_expanded/*.info -> $target/"
		else
			find "$old_expanded" -maxdepth 1 -type f -name '*.info' \
				-exec cp -n {} "$target/" \; 2>/dev/null || true
		fi
	fi

	if [[ "$old_raw" == /app/* ]] \
		&& command -v flatpak >/dev/null 2>&1 \
		&& flatpak info org.libretro.RetroArch >/dev/null 2>&1; then
		info "Copying Flatpak bundled core-info files to writable RetroArch config"
		if [ "$DRY_RUN" = "1" ]; then
			info "Would run Flatpak copy from /app/share/libretro/info"
		else
			flatpak run --command=sh org.libretro.RetroArch -c \
				'mkdir -p "$XDG_CONFIG_HOME/retroarch/info"; cp -n /app/share/libretro/info/*.info "$XDG_CONFIG_HOME/retroarch/info/" 2>/dev/null || true' \
				|| true
		fi
	fi
}

CORE_DIR_RAW="${CORE_DIR_OVERRIDE:-$(read_cfg_path libretro_directory)}"
[ -n "$CORE_DIR_RAW" ] || CORE_DIR_RAW="$CONFIG_DIR/cores"
CORE_DIR="$(expand_path "$CORE_DIR_RAW")"
ensure_writable_config_path CORE_DIR "Cores" libretro_directory \
	"$CORE_DIR_OVERRIDE" "$CORE_DIR_RAW" "$CORE_DIR" "$CONFIG_DIR/cores"

INFO_DIR_RAW="${INFO_DIR_OVERRIDE:-$(read_cfg_path libretro_info_path)}"
[ -n "$INFO_DIR_RAW" ] || INFO_DIR_RAW="$CONFIG_DIR/info"
INFO_DIR="$(expand_path "$INFO_DIR_RAW")"

if [ -z "$INFO_DIR_OVERRIDE" ]; then
	if path_is_package_managed "$INFO_DIR_RAW" "$INFO_DIR" \
		|| path_is_existing_unusable_dir "$INFO_DIR"; then
		NEW_INFO_DIR="$CONFIG_DIR/info"
		run_cmd mkdir -p "$NEW_INFO_DIR"
		copy_existing_info_files "$INFO_DIR_RAW" "$INFO_DIR" "$NEW_INFO_DIR"
		INFO_DIR="$NEW_INFO_DIR"
		set_cfg_path libretro_info_path "$INFO_DIR"
		info "Using writable Core Info directory: $INFO_DIR"
	fi
fi

run_cmd mkdir -p "$CORE_DIR" "$INFO_DIR"
run_cmd cp "$CORE" "$CORE_DIR/"
run_cmd cp "$INFO_FILE" "$INFO_DIR/altirra_libretro.info"

ok "RetroArch config:     $CFG"
ok "Installed core:       $CORE_DIR/$(basename "$CORE")"
ok "Installed core info:  $INFO_DIR/altirra_libretro.info"
info "Restart RetroArch before loading the core."
