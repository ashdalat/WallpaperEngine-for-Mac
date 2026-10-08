#!/usr/bin/env zsh
# Builds wallpaper-scene-renderer (patched for MoltenVK) into a single
# static library the Xcode app links against:
#   scene_backend/build/install/lib/libwescene-renderer.a
#
# Prerequisites: macOS 12+, Xcode, Homebrew (cmake, pkg-config, lz4,
# molten-vk are installed automatically). Eigen, nlohmann/json,
# SPIRV-Reflect, glslang, VMA and miniaudio are bundled in the engine's
# third_party/ — no need to install them separately.
set -euo pipefail
cd "$(dirname "$0")"

echo "==> Checking Homebrew"
if ! command -v brew >/dev/null 2>&1; then
    echo "ERROR: Homebrew not found. Install it first: https://brew.sh" >&2
    exit 1
fi
BREW_PREFIX="$(brew --prefix)"

echo "==> Installing build dependencies"
brew install cmake pkg-config lz4 molten-vk

echo "==> Cloning wallpaper-scene-renderer"
if [ ! -f src/SceneWallpaper.cpp ]; then
    git clone --depth 1 https://github.com/catsout/wallpaper-scene-renderer src
else
    echo "    src/ already exists, skipping clone (delete it to re-clone)"
fi

echo "==> Updating submodules (glslang, SPIRV-Reflect, nlohmann, ...)"
if ! git submodule update --init --recursive; then
    echo "    WARNING: 'git submodule update' failed (GitLab is often overloaded)."
fi
# Eigen is header-only and its GitLab repo frequently rate-limits;
# fall back to the GitHub mirror if the submodule is still empty.
if [ ! -f third_party/Eigen/Eigen/Core ]; then
    echo "    Eigen missing — cloning from GitHub mirror instead..."
    git clone --quiet https://github.com/eigenteam/eigen-git-mirror third_party/Eigen
fi

echo "==> Applying MoltenVK patches"
bash ApplyMoltenVKPatches.sh

echo "==> Configuring CMake for $(uname -m)"
# Homebrew on Apple Silicon installs into /opt/homebrew, which is not
# in the default compiler/pkg-config search paths — point the tools at
# it explicitly. VULKAN_SDK/CMAKE_PREFIX_PATH make find_package(Vulkan)
# pick up MoltenVK's headers and libMoltenVK.dylib.
export VULKAN_SDK="$BREW_PREFIX"
export CMAKE_PREFIX_PATH="$BREW_PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export PKG_CONFIG_PATH="$BREW_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

# NOTE: build the native architecture only. A universal (arm64;x86_64)
# build would fail to link against Homebrew's molten-vk/lz4, which
# ship arm64-only on Apple Silicon (and x86_64-only on Intel Macs).
#
# Homebrew's molten-vk ships libMoltenVK.dylib (not libvulkan.dylib),
# and keeps the Vulkan headers under libexec/include (not include/),
# so CMake's FindVulkan needs explicit hints to both.
rm -rf build
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DVulkan_LIBRARY="$BREW_PREFIX/lib/libMoltenVK.dylib" \
    -DVulkan_INCLUDE_DIR="$BREW_PREFIX/opt/molten-vk/libexec/include"

echo "==> Building (glslang is compiled from source, this takes a while)"
cmake --build build -j"$(sysctl -n hw.ncpu)"

echo "==> Merging static libraries"
# The engine's CMake produces a tree of static libraries (the top-level
# library plus wpVulkan, wpScene, glslang, SPIRV, SPIRV-Reflect, ...).
# The project has no install() rules, so merge everything into one fat
# library that Xcode can link in a single step.
mkdir -p build/install/lib
LIBS=($(find build -name '*.a' -not -path 'build/install/*' | sort -u))
libtool -static -o build/install/lib/libwescene-renderer.a "${LIBS[@]}"

echo ""
echo "==> Done: $(ls -lh build/install/lib/libwescene-renderer.a | awk '{print $5}')"
echo "    Now run: ./build.sh   (generates the Xcode project and builds the app)"
