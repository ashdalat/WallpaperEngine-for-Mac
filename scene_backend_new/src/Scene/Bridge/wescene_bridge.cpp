// C API bridge between the host application (ObjC++/Swift)
// and the open-wallpaper-engine scene renderer. Compiled
// into the owe-scene archive so the app links one library
// without needing the C++20 module build setup.
//
// The app passes paths as UTF-8 C strings; the engine wants
// rstd String/PathBuf, so convert through CStr. The engine's
// typed setters (setFps, setFillMode, ...) post messages to
// its main looper, and configure() re-applies the whole
// config — so the current settings are tracked here and
// copied into the config on every load.

#define VK_USE_PLATFORM_METAL_EXT 1
#include <vulkan/vulkan.h>

#include "wescene_bridge.h"

#include <memory>

import rstd;
import rstd.log;
import wescene.scene_wallpaper;

using namespace rstd::prelude;
using rstd::path::PathBuf;
using ::alloc::string::String;

namespace
{

String make_string(const char* cstr)
{
    return String::make(
        rstd::ffi::CStr::from_ptr(cstr ? cstr : "").to_str().unwrap());
}

PathBuf make_path(const char* cstr)
{
    return PathBuf::from(make_string(cstr));
}

struct BridgeState {
    std::unique_ptr<owe::SceneWallpaper> scene;
    owe::FillMode fill_mode { owe::FillMode::ASPECTCROP };
    unsigned       fps { 30 };
    float          volume { 1.0f };
    float          speed { 1.0f };
    bool           muted { false };
};

} // namespace

extern "C" {

void* wescene_bridge_create(void* metal_layer, unsigned width, unsigned height)
{
    auto state = std::make_unique<BridgeState>();
    state->scene = std::make_unique<owe::SceneWallpaper>();
    if (! state->scene->init()) return nullptr;

    owe::RenderInitInfo info;
    info.offscreen = false; // onscreen desktop rendering
    info.width     = static_cast<rstd::uint16_t>(width);
    info.height    = static_cast<rstd::uint16_t>(height);
    info.msaa_samples = 1;

    // The engine runs this on its render thread to create the
    // Vulkan surface that presents into our CAMetalLayer.
    // vkCreateMetalSurfaceEXT takes a VkMetalSurfaceCreateInfoEXT
    // struct whose pLayer field holds the layer — NOT the layer
    // directly. (In plain C++ the header typedefs CAMetalLayer to
    // void, so the caller's layer pointer drops in as-is.)
    auto layer = static_cast<const CAMetalLayer*>(metal_layer);
    info.surface_info.createSurfaceOp = Some(
        owe::CreateSurfaceCallback::make(
            [layer](VkInstance instance, VkSurfaceKHR* surface) -> VkResult {
                VkMetalSurfaceCreateInfoEXT createInfo = {};
                createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
                createInfo.pLayer = layer;
                return vkCreateMetalSurfaceEXT(instance, &createInfo, nullptr, surface);
            }));

    // Instance extensions the surface needs: VK_KHR_surface plus
    // the platform surface extension. (VK_KHR_portability_enumeration
    // is deliberately absent: the engine links libMoltenVK.dylib
    // directly, with no Vulkan loader, and MoltenVK does not even
    // list that extension in vkEnumerateInstanceExtensionProperties.)
    info.surface_info.instanceExts.push(make_string("VK_KHR_surface"));
    info.surface_info.instanceExts.push(make_string("VK_EXT_metal_surface"));

    // initVulkan posts the work to the engine's render looper;
    // it returns immediately.
    state->scene->initVulkan(rstd::move(info));
    return state.release();
}

void wescene_bridge_destroy(void* bridge)
{
    delete static_cast<BridgeState*>(bridge);
}

// The engine logs through rstd-log, which is inert until a logger is
// installed. The viewer does this at startup; do the same here so
// RSTD_LOG works for the app. One-shot: later calls are no-ops.
void wescene_bridge_enable_env_logging(void)
{
    static rstd::log::EnvLogger logger;
    (void)rstd::log::set_logger(logger);
    rstd::log::set_max_level(logger.filter());
}

void wescene_bridge_load(void* bridge,
                         const char* scene_json,
                         const char* assets_dir,
                         const char* cache_dir)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state == nullptr) return;

    owe::SceneWallpaperConfig config;
    // Setting source_pkg_path and assets_dir makes the engine
    // mount the scene.pkg archive (derived from the scene.json
    // path) over the assets directory and start loading the
    // scene.
    config.source_pkg_path = make_path(scene_json);
    config.assets_dir      = make_path(assets_dir);
    config.cache_dir       = make_path(cache_dir);
    config.fps             = state->fps;
    config.fill_mode       = state->fill_mode;
    config.volume          = state->volume;
    config.speed           = state->speed;
    config.muted           = state->muted;
    state->scene->configure(rstd::move(config));
}

void wescene_bridge_play(void* bridge)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state != nullptr) state->scene->play();
}

void wescene_bridge_pause(void* bridge)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state != nullptr) state->scene->pause();
}

void wescene_bridge_mouse_input(void* bridge, double x, double y)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state != nullptr) state->scene->mouseInput(x, y);
}

void wescene_bridge_set_fps(void* bridge, unsigned fps)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state == nullptr) return;
    state->fps = fps;
    state->scene->setFps(static_cast<uint32_t>(fps));
}

void wescene_bridge_set_fill_mode(void* bridge, int fill_mode)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state == nullptr) return;
    state->fill_mode = static_cast<owe::FillMode>(fill_mode);
    state->scene->setFillMode(state->fill_mode);
}

void wescene_bridge_set_speed(void* bridge, float speed)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state == nullptr) return;
    state->speed = speed;
    state->scene->setSpeed(speed);
}

void wescene_bridge_set_volume(void* bridge, float volume)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state == nullptr) return;
    state->volume = volume;
    state->scene->setVolume(volume);
}

void wescene_bridge_set_muted(void* bridge, int muted)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state == nullptr) return;
    state->muted = muted != 0;
    state->scene->setMuted(state->muted);
}

} // extern "C"
