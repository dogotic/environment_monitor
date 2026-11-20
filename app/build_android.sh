#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/mobile_app/build/android_arm64"
SOURCE_DIR="$SCRIPT_DIR/mobile_app"
OUT_DIR="$SCRIPT_DIR/android-build"

QT_DIR="$HOME/Qt/6.12.0/android_arm64_v8a"
ANDROID_SDK="$HOME/Android/Sdk"
ANDROID_NDK="$ANDROID_SDK/ndk/27.2.12479018"
BUILD_TOOLS="$ANDROID_SDK/build-tools/36.0.0"

UNSIGNED_APK="$BUILD_DIR/android-build/build/outputs/apk/release/android-build-release-unsigned.apk"
SIGNED_APK="$SCRIPT_DIR/EnvMonitorApp.apk"

KEYSTORE="${KEYSTORE:-$HOME/.android/debug.keystore}"
KS_PASS="${KS_PASS:-android}"
KS_ALIAS="${KS_ALIAS:-androiddebugkey}"

# --- CMake configure (skip if already configured) ---
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    echo ">>> Configuring CMake..."
    mkdir -p "$BUILD_DIR"
    # Remove stale CMake files from project root
    rm -f "$SCRIPT_DIR/CMakeCache.txt"
    rm -rf "$SCRIPT_DIR/CMakeFiles"
    "$QT_DIR/bin/qt-cmake" \
        -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DANDROID_ABI=arm64-v8a \
        -DANDROID_SDK_ROOT="$ANDROID_SDK" \
        -DANDROID_NDK_ROOT="$ANDROID_NDK" \
        -DQT_CHAINLOAD_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
        -B "$BUILD_DIR" \
        "$SOURCE_DIR"
else
    echo ">>> CMake already configured, skipping."
fi

# --- Build ---
echo ">>> Building..."
cd "$SCRIPT_DIR"
ninja -C "$BUILD_DIR" -j"$(nproc)"

# --- Sign ---
echo ">>> Signing APK..."
"$BUILD_TOOLS/apksigner" sign \
    --ks "$KEYSTORE" \
    --ks-pass "pass:$KS_PASS" \
    --ks-key-alias "$KS_ALIAS" \
    --out "$SIGNED_APK" \
    "$UNSIGNED_APK"

echo ""
echo "Done! APK: $SIGNED_APK"
