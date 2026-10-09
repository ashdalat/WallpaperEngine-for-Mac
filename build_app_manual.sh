#!/usr/bin/env bash
# Builds WallpaperEngineMac.app WITHOUT Xcode — for machines without
# the App Store / a full Xcode install. Compiles the ObjC++ bridge
# with clang++, the Swift sources with swiftc (both ship with the
# Command Line Tools), and packages the .app bundle by hand.
#
# Prerequisite: scene_backend/build/install/lib/libwescene-renderer.a
# (run ./build.sh or bash scene_backend/BuildSceneBackend.sh first).
set -euo pipefail
cd "$(dirname "$0")"

SDK="$(xcrun --show-sdk-path)"
BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"
MVK_INCLUDE="$BREW_PREFIX/opt/molten-vk/libexec/include"
# New engine (open-wallpaper-engine). The old catsout
# engine is still in scene_backend/ for rollback — see
# scene_backend_new/README.md.
SCENE_LIB="scene_backend_new/build/install/lib/libwescene-renderer.a"
OUT="build_manual"

[ -f "$SCENE_LIB" ] || {
    echo "Scene engine not built yet — run: bash scene_backend_new/BuildSceneBackend.sh" >&2
    exit 1
}
command -v swiftc >/dev/null 2>&1 || {
    echo "swiftc not found — install the Xcode Command Line Tools (xcode-select --install)" >&2
    exit 1
}

mkdir -p "$OUT"

echo ">> Compiling ObjC++ bridge..."
clang++ -c \
    WallpaperEngineMac/Scene/SceneRendererBridge.mm \
    -o "$OUT/SceneRendererBridge.o" \
    -I WallpaperEngineMac/Scene \
    -I scene_backend_new/build/install/include \
    -I "$MVK_INCLUDE" \
    -I "$BREW_PREFIX/include" \
    -isysroot "$SDK" \
    -fobjc-arc -fexceptions -std=c++20

echo ">> Compiling Swift sources..."
swiftc \
    "$OUT/SceneRendererBridge.o" \
    WallpaperEngineMac/AppDelegate.swift \
    WallpaperEngineMac/AppState.swift \
    WallpaperEngineMac/main.swift \
    WallpaperEngineMac/Models/*.swift \
    WallpaperEngineMac/Desktop/*.swift \
    WallpaperEngineMac/MenuBar/*.swift \
    WallpaperEngineMac/Services/*.swift \
    WallpaperEngineMac/Views/*.swift \
    -import-objc-header WallpaperEngineMac/WallpaperEngineMac-Bridging-Header.h \
    -I WallpaperEngineMac/Scene \
    -o "$OUT/WallpaperEngineMac" \
    -target arm64-apple-macos15.0 \
    -sdk "$SDK" \
    -L scene_backend_new/build/install/lib -lwescene-renderer \
    -L "$BREW_PREFIX/lib" -lMoltenVK -llz4 -lavformat -lavcodec -lavutil -lswscale -lswresample -lfontconfig -lfreetype -lc++ \
    -Xlinker -rpath -Xlinker "$BREW_PREFIX/lib" \
    -framework Cocoa -framework SwiftUI -framework AppKit -framework Combine \
    -framework QuartzCore -framework Metal -framework IOSurface -framework CoreVideo \
    -framework CoreGraphics -framework CoreAudio -framework AudioToolbox \
    -framework AVFoundation -framework CoreMedia -framework WebKit -framework IOKit \
    -framework OpenGL

echo ">> Packaging WallpaperEngineMac.app..."
APP="$OUT/WallpaperEngineMac.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp "$OUT/WallpaperEngineMac" "$APP/Contents/MacOS/WallpaperEngineMac"
sed -e 's|\$(EXECUTABLE_NAME)|WallpaperEngineMac|g' \
    -e 's|\$(PRODUCT_BUNDLE_IDENTIFIER)|com.wallpaperengine.mac|g' \
    -e 's|\$(PRODUCT_NAME)|WallpaperEngineMac|g' \
    -e 's|\$(MACOSX_DEPLOYMENT_TARGET)|15.0|g' \
    WallpaperEngineMac/Info.plist > "$APP/Contents/Info.plist"

echo ""
echo ">> Done: ${APP}"
echo "   Run with: open '${APP}'"
