#!/usr/bin/env bash
# librashader.sh — Build librashader from source and place the shared library
# next to the AltirraSDL executable so dlopen() finds it at runtime.
#
# Expects: ROOT_DIR, BUILD_DIR, PLATFORM (from build.sh)

[ -z "${C_RESET:-}" ] && source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

LIBRASHADER_VERSION="librashader-v0.10.1"
LIBRASHADER_SRC="$ROOT_DIR/build/_deps/librashader-src"
LIBRASHADER_PROFILE="optimized"

# ── Platform-specific library name ───────────────────────────────────────
case "$PLATFORM" in
    linux)   LIBRA_SO="librashader.so" ;;
    macos)   LIBRA_SO="librashader.dylib" ;;
    windows) LIBRA_SO="librashader.dll" ;;
    *)       die "Unsupported platform for librashader: $PLATFORM" ;;
esac

# Destination: next to the AltirraSDL executable
LIBRA_DEST="$BUILD_DIR/src/AltirraSDL/$LIBRA_SO"

# ── Skip if already built ───────────────────────────────────────────────
if [ -f "$LIBRA_DEST" ]; then
    SIZE=$(du -h "$LIBRA_DEST" | cut -f1)
    ok "librashader already built: ${C_BOLD}${LIBRA_DEST}${C_RESET} ($SIZE)"
    return 0 2>/dev/null || exit 0
fi

# ── Check for Rust toolchain ────────────────────────────────────────────
if ! command -v cargo &>/dev/null; then
    echo ""
    warn "Rust toolchain (cargo) is required to build librashader."
    echo ""
    echo "  Install Rust with:"
    echo "    ${C_BOLD}curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh${C_RESET}"
    echo ""
    echo "  Then restart your shell and retry."
    die "cargo not found — cannot build librashader."
fi

CARGO_VER=$(cargo --version 2>&1)
info "Rust: $CARGO_VER"

# ── Clone / update source ──────────────────────────────────────────────
if [ -d "$LIBRASHADER_SRC/.git" ]; then
    info "librashader source already present at $LIBRASHADER_SRC"
else
    info "Cloning librashader ${LIBRASHADER_VERSION}..."
    git clone --depth 1 --branch "$LIBRASHADER_VERSION" \
        https://github.com/SnowflakePowered/librashader.git \
        "$LIBRASHADER_SRC" \
        || die "Failed to clone librashader"
fi

# ── Build ───────────────────────────────────────────────────────────────
info "Building librashader (this may take a few minutes on first build)..."
echo ""

# Use --stable if nightly is not available
STABLE_FLAG=""
if ! rustup run nightly rustc --version &>/dev/null 2>&1; then
    STABLE_FLAG="--stable"
    info "Using stable Rust (nightly not found)"
fi

(cd "$LIBRASHADER_SRC" && \
    cargo run -p librashader-build-script -- \
        --profile "$LIBRASHADER_PROFILE" \
        $STABLE_FLAG) \
    || die "librashader build failed.

Common fixes:
  - Ensure Rust is up to date: rustup update
  - If nightly is needed: rustup install nightly"

# ── Find and copy the built library ─────────────────────────────────────
BUILT_LIB="$LIBRASHADER_SRC/target/$LIBRASHADER_PROFILE/$LIBRA_SO"
if [ ! -f "$BUILT_LIB" ]; then
    # Try release profile as fallback
    BUILT_LIB="$LIBRASHADER_SRC/target/release/$LIBRA_SO"
fi

if [ ! -f "$BUILT_LIB" ]; then
    die "librashader build succeeded but $LIBRA_SO not found in target directory.
Check $LIBRASHADER_SRC/target/ for the output file."
fi

# Copy next to the executable
mkdir -p "$(dirname "$LIBRA_DEST")"
cp "$BUILT_LIB" "$LIBRA_DEST"

SIZE=$(du -h "$LIBRA_DEST" | cut -f1)
echo ""
ok "librashader built: ${C_BOLD}${LIBRA_DEST}${C_RESET} ($SIZE)"
