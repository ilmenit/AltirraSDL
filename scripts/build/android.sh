#!/usr/bin/env bash
# android.sh — Build Altirra for Android.
#
# Validates the full Android toolchain (Java, SDK, NDK), sets up SDL3,
# and invokes Gradle to produce an APK.
#
# Expects: ROOT_DIR, BUILD_TYPE, CLEAN (from build.sh)
# Optional: SETUP_ANDROID=1 — install SDK/NDK/platform non-interactively

[ -z "${C_RESET:-}" ] && source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

ANDROID_DIR="$ROOT_DIR/android"
BUILD_TYPE="${BUILD_TYPE:-debug}"

# ── Version pins ─────────────────────────────────────────────────────────
# Update these when bumping Android target versions.
# They're used for both auto-setup and diagnostic messages.
REQUIRED_PLATFORM="android-36"
REQUIRED_BUILD_TOOLS="36.0.0"
REQUIRED_NDK="28.2.13676358"
REQUIRED_CMAKE_SDK="3.31.6"
MIN_JAVA_VERSION=17

# =========================================================================
# Step 1: Java
# =========================================================================

check_java() {
    if ! command -v javac &>/dev/null; then
        return 1
    fi
    # Parse "javac 17.0.x" or "javac 21.0.x"
    local ver
    ver=$(javac -version 2>&1 | head -1 | sed -E 's/javac ([0-9]+).*/\1/')
    if [ -z "$ver" ] || [ "$ver" -lt "$MIN_JAVA_VERSION" ] 2>/dev/null; then
        return 1
    fi
    return 0
}

if ! check_java; then
    echo ""
    warn "Java $MIN_JAVA_VERSION+ (JDK) is required but not found."
    echo ""
    echo "  Install it for your platform:"
    echo ""
    echo "    ${C_BOLD}Fedora/RHEL:${C_RESET}"
    echo "      sudo dnf install java-latest-openjdk-devel"
    echo ""
    echo "    ${C_BOLD}Debian/Ubuntu:${C_RESET}"
    echo "      sudo apt install openjdk-21-jdk"
    echo ""
    echo "    ${C_BOLD}Arch:${C_RESET}"
    echo "      sudo pacman -S jdk-openjdk"
    echo ""
    echo "    ${C_BOLD}macOS:${C_RESET}"
    echo "      brew install openjdk"
    echo ""
    echo "    ${C_BOLD}Windows (Git Bash / MSYS2):${C_RESET}"
    echo "      Download from https://adoptium.net/ or:"
    echo "      winget install EclipseAdoptium.Temurin.21.JDK"
    echo ""
    die "Install a JDK and retry."
fi

JAVA_VER=$(javac -version 2>&1 | head -1)
info "Java: $JAVA_VER"

# =========================================================================
# Step 2: Android SDK
# =========================================================================

# Try common locations if ANDROID_HOME is not set
find_android_home() {
    for candidate in \
        "${ANDROID_HOME:-}" \
        "${ANDROID_SDK_ROOT:-}" \
        "$HOME/Android/Sdk" \
        "$HOME/Library/Android/sdk" \
        "$HOME/android-sdk" \
        "/opt/android-sdk" \
        "${LOCALAPPDATA:-}/Android/Sdk"
    do
        if [ -n "$candidate" ] && [ -d "$candidate/platforms" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

if FOUND_HOME=$(find_android_home); then
    export ANDROID_HOME="$FOUND_HOME"
else
    echo ""
    warn "Android SDK not found."
    echo ""
    echo "  ${C_BOLD}Option A: Install via Android Studio${C_RESET} (easiest)"
    echo "    Download from https://developer.android.com/studio"
    echo "    SDK is installed automatically during first-run setup."
    echo ""
    echo "  ${C_BOLD}Option B: Command-line only${C_RESET}"
    echo ""
    echo "    1. Download command-line tools:"
    echo "       https://developer.android.com/studio#command-line-tools-only"
    echo ""
    echo "    2. Extract and set up:"
    echo "       mkdir -p ~/Android/Sdk/cmdline-tools"
    echo "       cd ~/Android/Sdk/cmdline-tools"
    echo "       unzip commandlinetools-*_latest.zip"
    echo "       mv cmdline-tools latest"
    echo ""
    echo "    3. Add to your shell profile (~/.bashrc, ~/.zshrc, etc.):"
    echo "       export ANDROID_HOME=\$HOME/Android/Sdk"
    echo "       export PATH=\$PATH:\$ANDROID_HOME/cmdline-tools/latest/bin"
    echo "       export PATH=\$PATH:\$ANDROID_HOME/platform-tools"
    echo ""
    echo "    4. Install required components:"
    echo "       sdkmanager --install \\"
    echo "           'platforms;${REQUIRED_PLATFORM}' \\"
    echo "           'ndk;${REQUIRED_NDK}' \\"
    echo "           'build-tools;${REQUIRED_BUILD_TOOLS}'"
    echo "       sdkmanager --licenses"
    echo ""
    echo "  ${C_BOLD}Option C: Use ./build.sh --setup-android${C_RESET}"
    echo "    If you have sdkmanager on PATH, this installs components automatically."
    echo ""
    die "Set ANDROID_HOME and retry."
fi

info "Android SDK: $ANDROID_HOME"

# =========================================================================
# Step 3: sdkmanager — install missing components if --setup-android
# =========================================================================

SDKMANAGER=""
for candidate in \
    "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" \
    "$ANDROID_HOME/cmdline-tools/bin/sdkmanager" \
    "$ANDROID_HOME/tools/bin/sdkmanager"
do
    if [ -x "$candidate" ]; then
        SDKMANAGER="$candidate"
        break
    fi
done

# Also check PATH
if [ -z "$SDKMANAGER" ] && command -v sdkmanager &>/dev/null; then
    SDKMANAGER="sdkmanager"
fi

if [ "${SETUP_ANDROID:-0}" = "1" ] && [ -n "$SDKMANAGER" ]; then
    info "Installing Android SDK components..."
    yes | "$SDKMANAGER" --install \
        "platforms;${REQUIRED_PLATFORM}" \
        "ndk;${REQUIRED_NDK}" \
        "build-tools;${REQUIRED_BUILD_TOOLS}" \
        "cmake;${REQUIRED_CMAKE_SDK}" \
        2>&1 | grep -v "^\[=" || true
    yes | "$SDKMANAGER" --licenses 2>/dev/null | tail -1 || true
    ok "SDK components installed"
elif [ "${SETUP_ANDROID:-0}" = "1" ] && [ -z "$SDKMANAGER" ]; then
    warn "sdkmanager not found — cannot auto-install components."
    warn "Install manually (see instructions above)."
fi

# ── Check required components ────────────────────────────────────────────
MISSING=""

if [ ! -d "$ANDROID_HOME/platforms/${REQUIRED_PLATFORM}" ]; then
    MISSING="$MISSING  platforms;${REQUIRED_PLATFORM}\n"
fi

if [ ! -d "$ANDROID_HOME/ndk/${REQUIRED_NDK}" ]; then
    # Try any NDK
    if ! ls -d "$ANDROID_HOME/ndk/"* &>/dev/null 2>&1; then
        MISSING="$MISSING  ndk;${REQUIRED_NDK}\n"
    fi
fi

if [ ! -d "$ANDROID_HOME/build-tools/${REQUIRED_BUILD_TOOLS}" ]; then
    # Try any build-tools
    if ! ls -d "$ANDROID_HOME/build-tools/"* &>/dev/null 2>&1; then
        MISSING="$MISSING  build-tools;${REQUIRED_BUILD_TOOLS}\n"
    fi
fi

if [ -n "$MISSING" ]; then
    echo ""
    warn "Missing Android SDK components."
    echo ""
    # Build the sdkmanager command with proper quoting
    SDKCMD="${SDKMANAGER:-sdkmanager}"
    CMD="    $SDKCMD --install"
    while IFS= read -r comp; do
        comp=$(echo "$comp" | xargs)  # trim whitespace
        [ -z "$comp" ] && continue
        CMD="$CMD '${comp}'"
    done <<< "$(echo -e "$MISSING")"

    echo "  Install with:"
    echo "$CMD"
    echo ""
    echo "  Or run: ./build.sh --setup-android"
    echo ""
    die "Install missing components and retry."
fi

# ── Auto-detect NDK ──────────────────────────────────────────────────────
if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    # Prefer the pinned version, fall back to newest available
    if [ -d "$ANDROID_HOME/ndk/${REQUIRED_NDK}" ]; then
        export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/${REQUIRED_NDK}"
    else
        NDK_DIR=$(find "$ANDROID_HOME/ndk" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | sort -V | tail -1)
        if [ -n "$NDK_DIR" ]; then
            export ANDROID_NDK_HOME="$NDK_DIR"
        else
            die "No NDK found in $ANDROID_HOME/ndk/"
        fi
    fi
fi
info "NDK: $ANDROID_NDK_HOME"

# =========================================================================
# Step 4: SDL3 source + Gradle wrapper
# =========================================================================

info "Checking SDL3 Android setup..."
if [ ! -d "$ANDROID_DIR/SDL3" ] || [ ! -L "$ANDROID_DIR/app/src/main/java/org/libsdl" ]; then
    info "Running SDL3 setup..."
    (cd "$ANDROID_DIR" && bash setup_sdl3.sh)
fi
ok "SDL3 ready"

# Gradle wrapper setup.
#
# We ALWAYS (re)write gradle-wrapper.properties so that bumping
# GRADLE_VER below actually takes effect on subsequent runs even when
# the android/gradle/ directory was restored from a CI cache.  The
# wrapper jar and gradlew launcher are only fetched/generated when
# missing — those are version-agnostic.
#
# 9.3.1 is the minimum required by the Android Gradle Plugin version
# pinned in android/app/build.gradle (AGP's version-check plugin
# refuses to apply on anything older).
GRADLE_VER="9.3.1"
WRAPPER_DIR="$ANDROID_DIR/gradle/wrapper"
mkdir -p "$WRAPPER_DIR"

# Always rewrite the .properties file so a GRADLE_VER bump propagates
# even when a stale wrapper jar is restored from cache.
cat > "$WRAPPER_DIR/gradle-wrapper.properties" <<PROPS
distributionBase=GRADLE_USER_HOME
distributionPath=wrapper/dists
distributionUrl=https\\://services.gradle.org/distributions/gradle-${GRADLE_VER}-bin.zip
networkTimeout=10000
zipStoreBase=GRADLE_USER_HOME
zipStorePath=wrapper/dists
PROPS

# Track which version the cached wrapper jar was downloaded for, so a
# version bump invalidates it.  Stored next to the jar; missing or
# mismatched stamp ⇒ re-download.
WRAPPER_JAR="$WRAPPER_DIR/gradle-wrapper.jar"
WRAPPER_STAMP="$WRAPPER_DIR/.gradle-version-stamp"
NEED_JAR_DL=0
if [ ! -f "$WRAPPER_JAR" ] || [ ! -s "$WRAPPER_JAR" ]; then
    NEED_JAR_DL=1
elif [ ! -f "$WRAPPER_STAMP" ] || [ "$(cat "$WRAPPER_STAMP" 2>/dev/null)" != "$GRADLE_VER" ]; then
    info "Cached wrapper jar is for a different Gradle version — refreshing"
    rm -f "$WRAPPER_JAR"
    NEED_JAR_DL=1
fi

if [ "$NEED_JAR_DL" = "1" ]; then
    info "Fetching gradle-wrapper.jar for Gradle ${GRADLE_VER}..."
    WRAPPER_JAR_REMOTE="https://raw.githubusercontent.com/gradle/gradle/v${GRADLE_VER}/gradle/wrapper/gradle-wrapper.jar"
    if command -v curl &>/dev/null; then
        curl -fsSL -o "$WRAPPER_JAR" "$WRAPPER_JAR_REMOTE" 2>/dev/null || true
    elif command -v wget &>/dev/null; then
        wget -q -O "$WRAPPER_JAR" "$WRAPPER_JAR_REMOTE" 2>/dev/null || true
    fi

    if [ ! -f "$WRAPPER_JAR" ] || [ ! -s "$WRAPPER_JAR" ]; then
        # Fallback: try system Gradle to generate it
        rm -f "$WRAPPER_JAR"
        if command -v gradle &>/dev/null; then
            info "Generating wrapper via system Gradle..."
            (cd "$ANDROID_DIR" && gradle wrapper --gradle-version "$GRADLE_VER" 2>/dev/null)
        else
            die "Could not obtain Gradle wrapper.
Install Gradle (e.g. 'brew install gradle' / 'sdk install gradle') and retry,
or download gradlew manually from an Android project template."
        fi
    fi
    echo "$GRADLE_VER" > "$WRAPPER_STAMP"
fi

# Create gradlew launcher script if missing.  This script is
# version-agnostic — it just classpath-loads gradle-wrapper.jar.
if [ ! -f "$ANDROID_DIR/gradlew" ]; then
    cat > "$ANDROID_DIR/gradlew" <<'GRADLEW'
#!/bin/sh
# Gradle wrapper launcher — downloads Gradle on first run.
# See gradle/wrapper/gradle-wrapper.properties for the version.

APP_HOME=$(cd "$(dirname "$0")" && pwd)
CLASSPATH="$APP_HOME/gradle/wrapper/gradle-wrapper.jar"

# Use JAVA_HOME if set, otherwise 'java' from PATH
if [ -n "$JAVA_HOME" ]; then
    JAVACMD="$JAVA_HOME/bin/java"
else
    JAVACMD="java"
fi

exec "$JAVACMD" \
    -classpath "$CLASSPATH" \
    org.gradle.wrapper.GradleWrapperMain \
    "$@"
GRADLEW
    chmod +x "$ANDROID_DIR/gradlew"
fi

ok "Gradle wrapper ready (v${GRADLE_VER})"

# =========================================================================
# Step 5: Build
# =========================================================================

# ── Clean (optional) ─────────────────────────────────────────────────────
if [ "${CLEAN:-0}" = "1" ]; then
    info "Cleaning Android build..."
    (cd "$ANDROID_DIR" && ./gradlew clean 2>/dev/null || true)
fi

# ── local.properties ─────────────────────────────────────────────────────
cat > "$ANDROID_DIR/local.properties" <<EOF
sdk.dir=$ANDROID_HOME
EOF

# ── Gradle build ─────────────────────────────────────────────────────────
if [ "$BUILD_TYPE" = "release" ]; then
    GRADLE_TASK="assembleRelease"
else
    GRADLE_TASK="assembleDebug"
fi

info "Building Android APK (${BUILD_TYPE})..."
echo ""
(cd "$ANDROID_DIR" && ./gradlew "$GRADLE_TASK") \
    || die "Gradle build failed.

Common fixes:
  - CMake errors: check that android/SDL3/ is complete (rm -rf android/SDL3 and retry)
  - License errors: run 'sdkmanager --licenses' to accept all licenses
  - NDK errors: verify NDK version matches app/build.gradle abiFilters
  - Java errors: ensure JAVA_HOME points to JDK $MIN_JAVA_VERSION+"

# =========================================================================
# Step 6: Sign APK (optional, --sign flag)
# =========================================================================

if [ "$BUILD_TYPE" = "release" ]; then
    APK_DIR="$ANDROID_DIR/app/build/outputs/apk/release"
else
    APK_DIR="$ANDROID_DIR/app/build/outputs/apk/debug"
fi

# Modern AGP (with Gradle 9+) drops the "-unsigned" suffix when a
# signing config is applied during the assemble task, so the APK can
# be either app-{release,debug}.apk or app-{release,debug}-unsigned.apk
# depending on the AGP/Gradle version.  Glob to find whichever one is
# present and pick the first match.
APK_PATH=$(ls "$APK_DIR"/*.apk 2>/dev/null | head -1)
APK_NAME=$(basename "${APK_PATH:-app-${BUILD_TYPE}.apk}")

if [ "${SIGN_APK:-0}" = "1" ] && [ -f "$APK_PATH" ]; then
    KEYSTORE="$HOME/.altirra-debug.jks"
    KS_PASS="altirra-debug"
    KS_ALIAS="altirra"

    # ── Find apksigner in build-tools ────────────────────────────────
    APKSIGNER=""
    for bt_dir in $(ls -d "$ANDROID_HOME/build-tools/"* 2>/dev/null | sort -V -r); do
        if [ -x "$bt_dir/apksigner" ]; then
            APKSIGNER="$bt_dir/apksigner"
            break
        fi
    done
    if [ -z "$APKSIGNER" ]; then
        die "apksigner not found in $ANDROID_HOME/build-tools/*/
Install build-tools: sdkmanager --install 'build-tools;${REQUIRED_BUILD_TOOLS}'"
    fi

    # ── Find keytool ─────────────────────────────────────────────────
    KEYTOOL=""
    if command -v keytool &>/dev/null; then
        KEYTOOL="keytool"
    elif [ -n "${JAVA_HOME:-}" ] && [ -x "$JAVA_HOME/bin/keytool" ]; then
        KEYTOOL="$JAVA_HOME/bin/keytool"
    fi
    if [ -z "$KEYTOOL" ]; then
        die "keytool not found. Ensure Java JDK is installed and on PATH."
    fi

    # ── Generate debug keystore if it doesn't exist ──────────────────
    if [ ! -f "$KEYSTORE" ]; then
        info "Generating debug keystore at $KEYSTORE ..."
        "$KEYTOOL" -genkey -v \
            -keystore "$KEYSTORE" \
            -keyalg RSA -keysize 2048 \
            -validity 10000 \
            -alias "$KS_ALIAS" \
            -storepass "$KS_PASS" \
            -keypass "$KS_PASS" \
            -dname "CN=Altirra Debug,O=Altirra,C=US" \
            2>&1 | tail -1
        ok "Debug keystore created"
    fi

    # ── Sign the APK ─────────────────────────────────────────────────
    info "Signing APK..."
    "$APKSIGNER" sign \
        --ks "$KEYSTORE" \
        --ks-pass "pass:$KS_PASS" \
        --ks-key-alias "$KS_ALIAS" \
        --key-pass "pass:$KS_PASS" \
        "$APK_PATH" \
        || die "APK signing failed"

    # apksigner signs in place — APK_PATH/APK_NAME are unchanged.
    ok "APK signed (debug keystore)"
fi

# =========================================================================
# Step 7: Report
# =========================================================================

echo ""
if [ -f "$APK_PATH" ]; then
    SIZE=$(du -h "$APK_PATH" | cut -f1)
    ok "APK built: ${C_BOLD}${APK_PATH}${C_RESET} ($SIZE)"
    echo ""
    info "Install on a connected device:"
    echo "  adb install -r $APK_PATH"
    echo ""
    info "Or install + launch:"
    echo "  adb install -r $APK_PATH && adb shell am start -n org.altirra.app/.AltirraActivity"
else
    warn "APK not found at expected path: $APK_PATH"
    info "Check $APK_DIR/ for output files"
fi
