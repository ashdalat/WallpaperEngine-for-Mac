#!/usr/bin/env bash
# Double-click this file to build WallpaperEngineMac
# and install it into /Applications (Launchpad).
#
# No Terminal knowledge needed — everything is shown
# in popup dialogs. Build logs go to /tmp if the
# dialog reports a failure.

cd "$(dirname "$0")"

log=/tmp/wallpaperenginemac-build.log
: > "$log"

fail() {
    osascript -e "display dialog \"Build failed: $1

Details: $log\" buttons {\"OK\" default button \"OK\"} with icon stop" 2>/dev/null
    exit 1
}

# 1. Check the build toolchain (lito lives in ~/lito-repo;
#    it is NOT wiped on restart like /tmp would be).
if [ ! -x "$HOME/lito-repo/build/install/bin/lito" ]; then
    fail "build tool 'lito' is missing from ~/lito-repo — ask the assistant to reinstall it"
fi

# 2. Build the scene engine (open-wallpaper-engine).
echo "==> Building scene engine..." >> "$log"
if ! bash scene_backend_new/BuildSceneBackend.sh >> "$log" 2>&1; then
    fail "scene engine build failed"
fi

# 3. Build the app bundle.
echo "==> Building app..." >> "$log"
if ! bash build_app_manual.sh >> "$log" 2>&1; then
    fail "app build failed"
fi

# 4. Install: quit the running copy, replace /Applications.
pkill -f "WallpaperEngineMac.app" 2>/dev/null
sleep 1
rm -rf /Applications/WallpaperEngineMac.app || fail "cannot remove old /Applications/WallpaperEngineMac.app"
cp -R build_manual/WallpaperEngineMac.app /Applications/ || fail "cannot copy to /Applications"

# 5. Refresh Launchpad so the icon appears.
defaults write com.apple.dock ResetLaunchPad -bool true
killall Dock 2>/dev/null

osascript -e 'display dialog "Done! WallpaperEngineMac is installed.

Find it in Launchpad. Its icon lives in the menu bar (top-right) once running." buttons {"OK"} default button "OK"' 2>/dev/null
