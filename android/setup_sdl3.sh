#!/usr/bin/env bash
#
# setup_sdl3.sh — Fetch SDL3 source and set up Java sources for Android build.
#
# SDL3 on Android requires both:
#   1. Native C library (built by CMake from android/SDL3)
#   2. Java SDLActivity class (must be in the Android project's Java source path)
#
# This script synchronizes a clean SDL3 clone to the pinned release and
# symlinks its Java sources into the Android project. Dirty clones and real
# Java directories are preserved with an error; move local work aside before
# retrying. Native CMake caches are invalidated when SDL revisions change.
#
# Usage:
#   cd android && ./setup_sdl3.sh
#
# Run before each build, including when restoring CI caches.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SDL3_DIR="$ROOT_DIR/android/SDL3"
SDL3_TAG="release-3.4.14"  # Keep Java SDLActivity sources aligned with CMake.

echo "=== Altirra Android — SDL3 Setup ==="

fail() {
    echo "[error] $*" >&2
    exit 1
}

# ── Step 1: Get SDL3 source ──────────────────────────────────────────────
if [ -e "$SDL3_DIR" ] || [ -L "$SDL3_DIR" ]; then
    [ ! -L "$SDL3_DIR" ] && [ -d "$SDL3_DIR/.git" ] \
        || fail "$SDL3_DIR must be an SDL3 Git clone; move it aside and rerun setup."
    # Never overwrite developer changes, even if HEAD already has the pin.
    [ -z "$(git -C "$SDL3_DIR" status --porcelain --untracked-files=all)" ] \
        || fail "SDL3 has local changes in $SDL3_DIR. Commit or move them aside before building the pinned $SDL3_TAG."
else
    echo "[info] Cloning SDL3 ($SDL3_TAG)..."
    git clone --depth 1 --branch "$SDL3_TAG" \
        https://github.com/libsdl-org/SDL.git "$SDL3_DIR"
    echo "[ok] SDL3 source cloned"
fi

if ! git -C "$SDL3_DIR" rev-parse --verify "refs/tags/$SDL3_TAG^{commit}" >/dev/null 2>&1; then
    git -C "$SDL3_DIR" fetch --depth 1 https://github.com/libsdl-org/SDL.git \
        "refs/tags/$SDL3_TAG:refs/tags/$SDL3_TAG"
fi
SDL3_REVISION="$(git -C "$SDL3_DIR" rev-parse "refs/tags/$SDL3_TAG^{commit}")"
if [ "$(git -C "$SDL3_DIR" rev-parse HEAD)" != "$SDL3_REVISION" ]; then
    echo "[info] Updating SDL3 to $SDL3_TAG"
    git -C "$SDL3_DIR" checkout --detach "$SDL3_REVISION"
fi
echo "[ok] SDL3 $SDL3_TAG ($SDL3_REVISION)"

# ── Step 2: Symlink Java sources ─────────────────────────────────────────
# SDL3's Java sources (SDLActivity.java etc.) must be on Gradle's Java
# source path. We symlink them rather than copy to stay in sync.

SDL3_JAVA_SRC="$SDL3_DIR/android-project/app/src/main/java"
LINK_TARGET="$SCRIPT_DIR/app/src/main/java/org/libsdl"
SDL3_JAVA_PKG="$SDL3_JAVA_SRC/org/libsdl"

if [ ! -d "$SDL3_JAVA_PKG" ]; then
    echo "[error] SDL3 Java sources not found at $SDL3_JAVA_PKG"
    echo "        Expected directory structure: SDL3/android-project/app/src/main/java/org/libsdl/"
    exit 1
fi

if [ -L "$LINK_TARGET" ]; then
    if [ "$(readlink "$LINK_TARGET")" != "$SDL3_JAVA_PKG" ]; then
        rm "$LINK_TARGET"
        ln -s "$SDL3_JAVA_PKG" "$LINK_TARGET"
    fi
    echo "[ok] SDL3 Java symlink verified"
elif [ -e "$LINK_TARGET" ]; then
    fail "$LINK_TARGET is not a symlink. Move it aside before setup; its contents have been preserved."
else
    mkdir -p "$(dirname "$LINK_TARGET")"
    ln -s "$SDL3_JAVA_PKG" "$LINK_TARGET"
    echo "[ok] SDL3 Java symlink created"
fi

# AGP caches CMake configuration and Ninja outputs here. A restored cache
# may belong to another SDL release, or predate revision tracking entirely.
NATIVE_CACHE="$SCRIPT_DIR/app/.cxx"
REVISION_STAMP="$NATIVE_CACHE/.altirra-sdl3-revision"
PREVIOUS_REVISION=""
if [ -f "$REVISION_STAMP" ]; then
    PREVIOUS_REVISION="$(cat "$REVISION_STAMP")"
fi
if [ "$PREVIOUS_REVISION" != "$SDL3_REVISION" ]; then
    echo "[info] Invalidating native CMake cache for SDL3 $SDL3_TAG"
    rm -rf "$NATIVE_CACHE"
    mkdir -p "$NATIVE_CACHE"
    echo "$SDL3_REVISION" > "$REVISION_STAMP"
fi

# ── Step 3: Verify Gradle wrapper ────────────────────────────────────────
if [ ! -f "$SCRIPT_DIR/gradlew" ]; then
    echo "[info] Gradle wrapper not found — generating..."
    if command -v gradle &>/dev/null; then
        (cd "$SCRIPT_DIR" && gradle wrapper --gradle-version 8.12)
        echo "[ok] Gradle wrapper generated"
    else
        echo "[warn] 'gradle' not found. Install Gradle or download gradlew manually."
        echo "       See: https://docs.gradle.org/current/userguide/gradle_wrapper.html"
        echo ""
        echo "       Quick fix: copy gradlew + gradle/ from any Android project, or run:"
        echo "         cd android && gradle wrapper --gradle-version 8.12"
    fi
fi

echo ""
echo "=== Setup complete ==="
echo ""
echo "Next steps:"
echo "  cd android"
echo "  export ANDROID_HOME=/path/to/android-sdk"
echo "  export ANDROID_NDK_HOME=\$ANDROID_HOME/ndk/<version>"
echo "  ./gradlew assembleDebug"
echo ""
echo "Or use the build script:"
echo "  ./build.sh --android"
