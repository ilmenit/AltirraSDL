#!/usr/bin/env bash
#
# Stage files for future Libretro infrastructure/docs pull requests.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${1:-$ROOT_DIR/build/libretro-upstream}"

INFO_SRC="$ROOT_DIR/src/AltirraLibretro/altirra_libretro.info"
DOCS_DRAFT="$ROOT_DIR/docs/libretro-docs-altirra-draft.md"
CMAKE_FILE="$ROOT_DIR/CMakeLists.txt"
CORE_FILE="$ROOT_DIR/src/AltirraLibretro/libretro.cpp"

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

[ -f "$INFO_SRC" ] || fail "missing source info: $INFO_SRC"
[ -f "$DOCS_DRAFT" ] || fail "missing docs draft: $DOCS_DRAFT"

bash "$ROOT_DIR/scripts/validate-libretro-info.sh" "$INFO_SRC" "$CMAKE_FILE"
bash "$ROOT_DIR/scripts/validate-libretro-docs.sh" \
    "$DOCS_DRAFT" "$INFO_SRC" "$CORE_FILE"

DISPLAY_NAME=$(sed -n \
    's/^display_name[[:space:]]*=[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' \
    "$INFO_SRC" | head -1)
[ -n "$DISPLAY_NAME" ] || fail "missing display_name in $INFO_SRC"

rm -rf "$OUT_DIR"
mkdir -p \
    "$OUT_DIR/libretro-super/dist/info" \
    "$OUT_DIR/libretro-docs/docs/library"

cp "$INFO_SRC" "$OUT_DIR/libretro-super/dist/info/altirra_libretro.info"

# Drop the local-only preface before staging the Libretro docs page.
STAGED_DOC="$OUT_DIR/libretro-docs/docs/library/altirra.md"
awk -v display_name="$DISPLAY_NAME" '
    BEGIN { skip = 1 }
    /^## Background$/ {
        print "# " display_name
        print ""
        skip = 0
    }
    /^## Notes For Upstream Submission$/ {
        exit
    }
    !skip { print }
' "$DOCS_DRAFT" > "$STAGED_DOC"

if grep -Eq 'local draft|Notes For Upstream Submission|docs/libretro-upstream.md' \
    "$STAGED_DOC"; then
    fail "staged docs still contain local-only upstream-preparation text"
fi

bash "$ROOT_DIR/scripts/validate-libretro-docs.sh" \
    "$STAGED_DOC" "$INFO_SRC" "$CORE_FILE"

cat > "$OUT_DIR/README.md" <<EOF
# Altirra Libretro Upstream Staging

Generated from: $ROOT_DIR

Copy these files into forks of the corresponding Libretro repositories:

- libretro-super/dist/info/altirra_libretro.info
- libretro-docs/docs/library/altirra.md

Before opening pull requests:

1. Confirm the core has passed docs/libretro-upstream.md readiness gates.
2. Verify RetroArch shows the copied info file correctly under
   Information / Core Information.
3. In libretro/docs, also update:
   - mkdocs.yml
   - docs/guides/core-list.md
   - docs/development/licenses.md
   - docs/library/bios.md
   - docs/meta/see-also.md, if maintainers want an Atari800 cross-link
4. Sync final core option and control tables in docs/library/altirra.md from
   the accepted core build before submitting.
EOF

printf 'ok: staged Libretro upstream files in %s\n' "$OUT_DIR"
