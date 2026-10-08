#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

SCENE_LIB="scene_backend/build/install/lib/libwescene-renderer.a"
if [ ! -f "$SCENE_LIB" ]; then
    echo ">> Scene engine not built yet — building wallpaper-scene-renderer first..."
    bash scene_backend/BuildSceneBackend.sh
fi

if ! command -v xcodegen &> /dev/null; then
    echo "[ERROR] XcodeGen not found."
    echo "Install it with: brew install xcodegen"
    exit 1
fi

echo ">> Generating Xcode project..."
xcodegen generate

echo ">> Building WallpaperEngineMac..."
xcodebuild \
    -project WallpaperEngineMac.xcodeproj \
    -scheme WallpaperEngineMac \
    -configuration Debug \
    -derivedDataPath build \
    CODE_SIGNING_ALLOWED=NO \
    build

APP_PATH="build/Build/Products/Debug/WallpaperEngineMac.app"
echo ""
echo ">> Build succeeded: ${APP_PATH}"
echo ">> Run it with:   open '${APP_PATH}'"
