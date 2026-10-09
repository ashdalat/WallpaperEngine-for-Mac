#!/usr/bin/env bash
# Builds open-wallpaper-engine (patched for MoltenVK)
# into a single static library the app links against:
#   scene_backend_new/build/install/lib/libwescene-renderer.a
#
# The upstream repo already carries macOS support
# (VideoToolbox→Metal→Vulkan interop, Metal surface
# creation, portability-subset device extensions). The
# local patches live in patched/ and are applied by
# redirecting the dependency sources through
# .lito/config.toml — see patched/README.md:
#   - vvk:        try libMoltenVK.dylib (Homebrew)
#                 before libvulkan.1.dylib (MoltenVK SDK)
#   - wavsen:     fix the VideoToolbox prefetch error
#                 string and add the Metal texture
#                 registry API vrento imports
#   - vrento:     import the wavsen Apple module that
#                 provides the Metal texture registry
#
# Prerequisites: the lito toolchain (built from
# source at ~/lito-repo), Homebrew LLVM 23 (clang +
# lld), ffmpeg, fontconfig/freetype, glslang and
# molten-vk.
set -euo pipefail
cd "$(dirname "$0")"

LITO_BIN="$HOME/lito-repo/build/install/bin"
BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"

command -v swiftc >/dev/null 2>&1 || true

echo "==> Building with lito (release profile)"
# lito bootstraps its own compiler requirements: brew LLVM
# provides clang 23 + lld (Apple clang has no lld), and
# brew bin puts glslangValidator / pkg-config on PATH.
export PATH="$LITO_BIN:$BREW_PREFIX/opt/llvm/bin:$BREW_PREFIX/bin:$PATH"
lito build -p owe-scene --profile release

echo "==> Merging static libraries"
# The lito build produces one archive per package under
# build/release/lib/ plus the CMake-built external
# dependencies (glslang, quickjs) under
# build/release/dependencies/cmake/. The app links one
# library, so merge everything into a fat archive.
mkdir -p build/install/lib build/install/include
LIBS=(
    $(find build/release/lib -name '*.a' | sort)
    $(find build/release/dependencies/cmake -path '*/install/lib/*.a' | sort)
)
libtool -static -o build/install/lib/libwescene-renderer.a "${LIBS[@]}"

echo "==> Installing bridge header"
cp src/Scene/Bridge/wescene_bridge.h build/install/include/wescene_bridge.h

echo ""
echo "==> Done: $(ls -lh build/install/lib/libwescene-renderer.a | awk '{print $5}')"
echo "    Now run: bash build_app_manual.sh"
