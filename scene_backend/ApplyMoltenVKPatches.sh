#!/usr/bin/env bash
# Patches wallpaper-scene-renderer for MoltenVK (macOS).
#
# Every change is guarded by `#if defined(__APPLE__)`, so the same source
# tree still builds unchanged on Linux. Safe to run multiple times
# (idempotent).
#
# Run this from scene_backend/ AFTER cloning the engine:
#   git clone --depth 1 https://github.com/catsout/wallpaper-scene-renderer src
#   bash ApplyMoltenVKPatches.sh
#
# What gets patched:
#   1. src/Vulkan/vulkan_wrapper.cpp  LoadLibrary(): dlopen libMoltenVK.dylib
#      instead of the Linux-only libvulkan.so.
#      (VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR is NOT
#      set: the engine links MoltenVK directly, with no Vulkan
#      loader, so portability enumeration is meaningless — and
#      VK_KHR_portability_enumeration is not even listed by
#      MoltenVK in vkEnumerateInstanceExtensionProperties. The
#      Swift bridge therefore passes no instanceExts.)
#   2. src/VulkanRender/VulkanRender.cpp  base_device_exts: drop the
#      Linux-only *_FD external memory/semaphore extensions (MoltenVK does
#      not expose them; only the offscreen path uses them), keep the
#      non-FD ones optional, and require VK_KHR_portability_subset
#      (mandatory on every MoltenVK device).
set -euo pipefail
cd "$(dirname "$0")"

python3 - <<'PYEOF'
import pathlib
import sys

# (file, old_text, new_text, marker_for_idempotency, description)
patches = [
    (
        "src/Vulkan/vulkan_wrapper.cpp",
        """    dlib = DynamicLibrary("libvulkan.so.1");
    if (! dlib.IsOpen()) dlib = DynamicLibrary("libvulkan.so");
""",
        """#if defined(__APPLE__)
    // MoltenVK: the stock engine loads the Linux Vulkan loader here. On
    // macOS, look for the MoltenVK library instead. Homebrew installs
    // it under /opt/homebrew (Apple Silicon) or /usr/local (Intel);
    // when the Xcode app links libMoltenVK.dylib directly, dlopen
    // simply returns the image that is already loaded.
    dlib = DynamicLibrary("libMoltenVK.dylib");
    if (! dlib.IsOpen()) dlib = DynamicLibrary("/opt/homebrew/lib/libMoltenVK.dylib");
    if (! dlib.IsOpen()) dlib = DynamicLibrary("/usr/local/lib/libMoltenVK.dylib");
    if (! dlib.IsOpen()) dlib = DynamicLibrary("libvulkan.dylib");
    if (! dlib.IsOpen()) dlib = DynamicLibrary("libvulkan.1.dylib");
#else
    dlib = DynamicLibrary("libvulkan.so.1");
    if (! dlib.IsOpen()) dlib = DynamicLibrary("libvulkan.so");
#endif
""",
        "libMoltenVK.dylib",
        "LoadLibrary: dlopen libMoltenVK.dylib on macOS",
    ),
    (
        "src/VulkanRender/VulkanRender.cpp",
        """constexpr std::array base_device_exts {
    Extension { false, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME },
    Extension { true, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME },
    Extension { true, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME },
    Extension { true, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME },
    Extension { true, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME },
    Extension { true, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME }
};
""",
        """#if defined(__APPLE__)
// MoltenVK compatibility:
//  - VK_KHR_external_memory_fd / VK_KHR_external_semaphore_fd do not
//    exist in MoltenVK (Linux-only). They are only used by the
//    offscreen external-swapchain path, never by onscreen rendering,
//    so they are dropped here.
//  - The non-FD external memory/semaphore extensions stay optional on
//    MoltenVK (it may or may not expose them; nothing on the onscreen
//    path needs them).
//  - VK_KHR_portability_subset must be enabled on every MoltenVK
//    device. (Its *_EXTENSION_NAME macro is only defined in the
//    C++ header vulkan.hpp, not in the C headers, so use the
//    literal string.)
constexpr std::array base_device_exts {
    Extension { false, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME },
    Extension { true, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME },
    Extension { false, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME },
    Extension { false, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME },
    Extension { true, "VK_KHR_portability_subset" }
};
#else
constexpr std::array base_device_exts {
    Extension { false, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME },
    Extension { true, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME },
    Extension { true, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME },
    Extension { true, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME },
    Extension { true, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME },
    Extension { true, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME }
};
#endif
""",
        "VK_KHR_portability_subset",
        "VulkanRender: device extensions for MoltenVK",
    ),
    (
        "src/Vulkan/StagingBuffer.cpp",
        """    VkDeviceSize size = std::min(ref.size - offset, data.size());""",
        """    // macOS: uint64_t (VkDeviceSize) is `unsigned long long` while
    // size_t is `unsigned long` — two different types, so std::min
    // cannot deduce the template parameter (on Linux both are
    // `unsigned long`). Pin the type explicitly.
    VkDeviceSize size = std::min<VkDeviceSize>(ref.size - offset, data.size());""",
        "std::min<VkDeviceSize>(ref.size - offset, data.size())",
        "writeToBuf: pin std::min type (uint64_t vs size_t on macOS)",
    ),
    (
        "src/Vulkan/StagingBuffer.cpp",
        """    VkDeviceSize size_     = std::min(ref.size - offset, size);""",
        """    VkDeviceSize size_     = std::min<VkDeviceSize>(ref.size - offset, size);""",
        "std::min<VkDeviceSize>(ref.size - offset, size)",
        "fillBuf: pin std::min type (uint64_t vs size_t on macOS)",
    ),
]

failed = False
for relpath, old, new, marker, desc in patches:
    p = pathlib.Path(relpath)
    if not p.exists():
        print(f"SKIP  {relpath}: not found (clone wallpaper-scene-renderer into src/ first)")
        continue
    text = p.read_text()
    if marker in text:
        print(f"SKIP  {relpath}: already patched ({desc})")
        continue
    count = text.count(old)
    if count != 1:
        print(f"FAIL  {relpath}: pattern for '{desc}' matched {count} times (expected exactly 1)")
        failed = True
        continue
    p.write_text(text.replace(old, new))
    print(f"OK    {relpath}: {desc}")

sys.exit(1 if failed else 0)
PYEOF
