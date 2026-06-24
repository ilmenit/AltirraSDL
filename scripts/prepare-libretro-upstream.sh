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

bash "$ROOT_DIR/scripts/validate-libretro-info.sh" \
    "$INFO_SRC" "$CMAKE_FILE" "$CORE_FILE"
bash "$ROOT_DIR/scripts/validate-libretro-docs.sh" \
    "$DOCS_DRAFT" "$INFO_SRC" "$CORE_FILE"

DISPLAY_NAME=$(sed -n \
    's/^display_name[[:space:]]*=[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' \
    "$INFO_SRC" | head -1)
[ -n "$DISPLAY_NAME" ] || fail "missing display_name in $INFO_SRC"

AUTHOR_TEXT=$(sed -n \
    's/^authors[[:space:]]*=[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' \
    "$INFO_SRC" | head -1)
LICENSE_TEXT=$(sed -n \
    's/^license[[:space:]]*=[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' \
    "$INFO_SRC" | head -1)
DATABASE_TEXT=$(sed -n \
    's/^database[[:space:]]*=[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' \
    "$INFO_SRC" | head -1)
DATABASE_DISPLAY=${DATABASE_TEXT//|/, }

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

SNIPPETS_DOC="$OUT_DIR/libretro-docs/ALTIRRA-UPSTREAM-SNIPPETS.md"
cat > "$SNIPPETS_DOC" <<EOF
# Altirra Libretro Docs PR Snippets

These are candidate snippets for the non-page files that the Libretro docs
README asks new cores to update. Check surrounding sort order and local style in
the target repository before applying them.

## mkdocs.yml

Add the library page under the appropriate Library / Computer or Atari section:

\`\`\`yaml
- $DISPLAY_NAME: library/altirra.md
\`\`\`

## docs/guides/core-list.md

Add Altirra to the Atari 8-bit Family / Atari 5200 core list, reusing the
existing platform database names instead of creating an Altirra-specific
database:

\`\`\`markdown
| $DISPLAY_NAME | [Altirra](../library/altirra.md) | $DATABASE_DISPLAY |
\`\`\`

## docs/development/licenses.md

Add the core license entry:

\`\`\`markdown
| Altirra | $LICENSE_TEXT | $AUTHOR_TEXT |
\`\`\`

## docs/library/bios.md

Add the optional firmware files listed by \`altirra_libretro.info\`:

\`\`\`markdown
| Core | System file | Required | Description |
| --- | --- | --- | --- |
EOF

firmware_count=$(sed -n \
    's/^firmware_count[[:space:]]*=[[:space:]]*\([0-9][0-9]*\)[[:space:]]*$/\1/p' \
    "$INFO_SRC" | head -1)
for ((I = 0; I < firmware_count; ++I)); do
    path=$(sed -n \
        "s/^firmware${I}_path[[:space:]]*=[[:space:]]*\"\\(.*\\)\"[[:space:]]*$/\\1/p" \
        "$INFO_SRC" | head -1)
    desc=$(sed -n \
        "s/^firmware${I}_desc[[:space:]]*=[[:space:]]*\"\\(.*\\)\"[[:space:]]*$/\\1/p" \
        "$INFO_SRC" | head -1)
    opt=$(sed -n \
        "s/^firmware${I}_opt[[:space:]]*=[[:space:]]*\"\\(.*\\)\"[[:space:]]*$/\\1/p" \
        "$INFO_SRC" | head -1)

    required="Yes"
    [ "$opt" = "true" ] && required="No"
    printf '| Altirra | `%s` | %s | %s |\n' \
        "$path" "$required" "$desc" >> "$SNIPPETS_DOC"
done

cat >> "$SNIPPETS_DOC" <<'EOF'
```

## docs/meta/see-also.md

Optional, if maintainers want cross-links between Atari cores:

```markdown
- [Atari800](../library/atari800.md) and [Altirra](../library/altirra.md)
  both emulate Atari 8-bit family systems.
```
EOF

grep -Fq -- "- $DISPLAY_NAME: library/altirra.md" "$SNIPPETS_DOC" \
    || fail "generated snippets missing mkdocs.yml entry"
grep -Fq -- "| $DISPLAY_NAME | [Altirra](../library/altirra.md) | $DATABASE_DISPLAY |" \
    "$SNIPPETS_DOC" \
    || fail "generated snippets missing core-list entry"
if grep -Fq -- "| $DISPLAY_NAME | [Altirra](../library/altirra.md) | $DATABASE_TEXT |" \
    "$SNIPPETS_DOC"; then
    fail "generated snippets contain raw .info database separator in Markdown table"
fi
grep -Fq -- "| Altirra | $LICENSE_TEXT | $AUTHOR_TEXT |" "$SNIPPETS_DOC" \
    || fail "generated snippets missing license entry"
for ((I = 0; I < firmware_count; ++I)); do
    path=$(sed -n \
        "s/^firmware${I}_path[[:space:]]*=[[:space:]]*\"\\(.*\\)\"[[:space:]]*$/\\1/p" \
        "$INFO_SRC" | head -1)
    grep -Fq -- "\`$path\`" "$SNIPPETS_DOC" \
        || fail "generated snippets missing firmware path: $path"
done

cat > "$OUT_DIR/README.md" <<EOF
# Altirra Libretro Upstream Staging

Generated from: $ROOT_DIR

Copy these files into forks of the corresponding Libretro repositories:

- libretro-super/dist/info/altirra_libretro.info
- libretro-docs/docs/library/altirra.md
- libretro-docs/ALTIRRA-UPSTREAM-SNIPPETS.md

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
4. Use libretro-docs/ALTIRRA-UPSTREAM-SNIPPETS.md as a starting point for
   those secondary docs edits, adapting to current sort order and style.
5. Sync final core option and control tables in docs/library/altirra.md from
   the accepted core build before submitting.
EOF

printf 'ok: staged Libretro upstream files in %s\n' "$OUT_DIR"
