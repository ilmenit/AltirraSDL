#!/bin/sh
# Clone the SDL3-for-Pascal community binding into a local
# directory so 04_paint.pas can find the SDL3 unit. Safe to
# re-run — skips if the checkout already exists.
#
# Usage:
#   ./fetch-sdl3-for-pascal.sh
#
# After running, build 04_paint.pas with:
#   fpc -Fu.. -Fu./sdl3-for-pascal/units 04_paint.pas -o04_paint
#
# License: SDL3-for-Pascal is Zlib-licensed (same as SDL itself),
# so redistribution is permitted — but we fetch at build time
# rather than vendoring to make version upgrades trivial.

set -e

REPO="https://github.com/PascalGameDevelopment/SDL3-for-Pascal.git"
TAG="v0.5"
DEST="sdl3-for-pascal"

if [ -d "$DEST" ]; then
    echo "$DEST already exists, skipping clone."
    exit 0
fi

echo "Cloning $REPO (tag $TAG) into $DEST ..."
git clone --depth 1 --branch "$TAG" "$REPO" "$DEST"

echo
echo "SDL3 for Pascal ready. Next step:"
echo "    fpc -Fu.. -Fu./$DEST/units 04_paint.pas -o04_paint"
