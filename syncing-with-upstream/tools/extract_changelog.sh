#!/usr/bin/env bash
#
# extract_changelog.sh — print just the prerelease / new-version section
# of the NEW snapshot's changes.txt.  Useful when you want a one-page
# summary without the full diff.
#
# Usage:
#   extract_changelog.sh <NEW_SNAPSHOT>

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
. "$SCRIPT_DIR/lib/common.sh"

[ $# -eq 1 ] || fatal "usage: extract_changelog.sh <NEW_SNAPSHOT>"

NEW=$(abs_path "$1")
F=$NEW/src/Altirra/res/changes.txt
[ -f "$F" ] || fatal "changes.txt not found in NEW: $F"

# Print lines up to (but not including) the first numbered "Version X.Y"
# heading, which separates the in-development section from the last
# shipped release.
awk '/^Version [0-9]/ { exit } { print }' "$F"
