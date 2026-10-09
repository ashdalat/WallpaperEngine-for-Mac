#!/usr/bin/env bash
# Builds WallpaperEngineMac.app WITHOUT bundling the
# Homebrew dylibs into the app — the "old way", for
# testing whether a problem is caused by bundling.
# The app loads its dylibs from the Homebrew prefix
# at runtime (via the rpath), so it only runs on a
# machine that has Homebrew installed.
#
# Same compile as build_app_manual.sh; only the
# bundle_dylibs.sh step is skipped.
set -euo pipefail
cd "$(dirname "$0")"

SDK="$(xcrun --show-sdk-path)"
BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"
MVK_INCLUDE="$BREW_PREFIX/opt/molten-vk/libexec/include"
SCENE_LIB="scene_backend_new/build/install/lib/libwescene-renderer.a"
OUT="build_unbundled"

[ -f "$SCENE_LIB" ] || {
    echo "Scene engine not built yet — run: bash scene_backend_new/BuildSceneBackend.sh" >&2
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

echo ">> Packaging WallpaperEngineMac.app (unbundled)..."
APP="$OUT/WallpaperEngineMac.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$OUT/WallpaperEngineMac" "$APP/Contents/MacOS/WallpaperEngineMac"

ICONSET="$OUT/AppIcon.iconset"
rm -rf "$ICONSET"
mkdir -p "$ICONSET"
icon_size() {
    sips -z "$2" "$2" AppIcon.png --out "$ICONSET/.tmp_$1" >/dev/null
    mv "$ICONSET/.tmp_$1" "$ICONSET/$1"
}
icon_size icon_16x16.png 16
icon_size icon_16x16@2x.png 32
icon_size icon_32x32.png 32
icon_size icon_32x32@2x.png 64
icon_size icon_128x128.png 128
icon_size icon_128x128@2x.png 256
icon_size icon_256x256.png 256
icon_size icon_256x256@2x.png 512
icon_size icon_512x512.png 512
icon_size icon_512x512@2x.png 1024
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
rm -rf "$ICONSET"

sed -e 's|\$(EXECUTABLE_NAME)|WallpaperEngineMac|g' \
    -e 's|\$(PRODUCT_BUNDLE_IDENTIFIER)|com.wallpaperengine.mac|g' \
    -e 's|\$(PRODUCT_NAME)|WallpaperEngineMac|g' \
    -e 's|\$(MACOSX_DEPLOYMENT_TARGET)|15.0|g' \
    WallpaperEngineMac/Info.plist > "$APP/Contents/Info.plist"

# The linker's automatic signature is flagged
# "linker-signed", which macOS treats as unsigned
# for TCC — the Accessibility grant would not
# stick, and the mouse event tap could never be
# created. Re-sign ad-hoc like the bundled build
# so the permission persists.
codesign --force --sign - "$APP/Contents/MacOS/WallpaperEngineMac"
codesign --force --sign - "$APP"

echo ""
echo ">> Done (unbundled): ${APP}"
echo "   Run with: open '${APP}'"
