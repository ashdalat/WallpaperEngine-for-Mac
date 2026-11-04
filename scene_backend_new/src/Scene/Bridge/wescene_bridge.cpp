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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "wescene_bridge.h"

#include <memory>

import rstd;
import rstd.log;
import owe.audio_response;
import wavsen.audio;
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
    wescene_first_frame_fn first_frame_fn { nullptr };
    void*          first_frame_userdata { nullptr };

    std::atomic<bool> audio_enabled { false };
    std::atomic<bool> audio_demand { false };
    std::atomic<bool> audio_worker_quit { false };
    wescene_audio_data_fn audio_data_fn { nullptr };
    void*                 audio_data_userdata { nullptr };
    std::unique_ptr<std::thread> audio_worker;
};

} // namespace

extern "C" {

void* wescene_bridge_create(void* metal_layer, unsigned width, unsigned height)
{
    auto state = std::make_unique<BridgeState>();
    state->scene = std::make_unique<owe::SceneWallpaper>();
    if (! state->scene->init()) return nullptr;

    // The scene reports when it wants audio response data; the
    // capture worker idles unless it does.
    auto* raw_state = state.get();
    state->scene->setAudioResponseDemandCallback([raw_state](bool active) {
        raw_state->audio_demand.store(active, std::memory_order_release);
    });

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
    auto* state = static_cast<BridgeState*>(bridge);
    if (state == nullptr) return;
    state->audio_worker_quit.store(true, std::memory_order_release);
    if (state->audio_worker != nullptr && state->audio_worker->joinable())
        state->audio_worker->join();
    delete state;
}

void wescene_bridge_set_first_frame_callback(void* bridge,
                                             wescene_first_frame_fn fn,
                                             void* userdata)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state == nullptr) return;
    state->first_frame_fn = fn;
    state->first_frame_userdata = userdata;
    // The engine invokes this on its runtime thread. The
    // captures are copies, so the lambda never touches
    // BridgeState — and a null fn installs a no-op, which
    // also disconnects any previous registration (the
    // host does this before destroying the bridge).
    wescene_first_frame_fn captured_fn = fn;
    void* captured_user = userdata;
    state->scene->setOnFirstFrame([captured_fn, captured_user] {
        if (captured_fn != nullptr) captured_fn(captured_user);
    });
}

// Audio response worker — mirrors the reference SceneViewer host:
// the scene demands audio (demand callback), the worker taps the
// system default output through wavsen's CoreAudio capture and
// pushes complete stereo windows to the engine's FFT pipeline.
// Runs for the bridge's lifetime; idles (and releases the tap)
// whenever the feature is disabled or the scene does not want it.
static void wem_audio_worker(BridgeState* state)
{
    wavsen::audio::AudioCapture capture;
    owe::audio::ResponseEngine  response_engine;
    bool capture_ready = false;
    bool ended_sent    = true;
    unsigned tick      = 0;

    while (! state->audio_worker_quit.load(std::memory_order_acquire)) {
        const bool active = state->audio_enabled.load(std::memory_order_acquire) &&
                            state->audio_demand.load(std::memory_order_acquire);
        if (! active) {
            if (capture_ready) {
                capture.uninit();
                capture_ready = false;
            }
            response_engine.end();
            if (! ended_sent) {
                state->scene->endAudioResponse();
                ended_sent = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (! capture_ready) {
            capture_ready = capture.init();
            if (! capture_ready) {
                // Common before the Screen & System Audio Recording
                // permission is granted — retry, the engine also
                // keeps working without audio response meanwhile.
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }
        wavsen::audio::AudioPcmWindow captured {};
        // ~30 Hz snapshot cadence, same as the reference host.
        if ((tick++ & 1u) == 0u && capture.snapshot(captured)) {
            ended_sent = false;
            owe::audio::PcmWindow window {};
            window.generation       = captured.generation;
            window.sequence         = captured.sequence;
            window.captured_at_ns   = captured.captured_at_ns;
            window.end_sample_frame = captured.end_sample_frame;
            window.sample_rate_hz   = captured.sample_rate_hz;
            window.channels         = captured.channels;
            window.frames           = captured.frames;
            for (std::size_t index = 0; index < owe::audio::kSampleCount; ++index)
                window.samples[rstd::usize(index)] =
                    captured.samples[rstd::usize(index)].to_primitive();

            if (state->audio_data_fn != nullptr) {
                owe::audio::ResponseFrame response {};
                if (response_engine.analyze(window, response)) {
                    float spectrum[128];
                    for (std::size_t index = 0; index < owe::audio::kResponseBins; ++index) {
                        spectrum[index]       = response.left[rstd::usize(index)];
                        spectrum[64u + index] = response.right[rstd::usize(index)];
                    }
                    state->audio_data_fn(spectrum, state->audio_data_userdata);
                }
            }
            state->scene->setAudioPcmWindow(std::move(window));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    if (capture_ready) capture.uninit();
}

void wescene_bridge_set_audio_response_enabled(void* bridge, int enabled)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state == nullptr) return;
    state->audio_enabled.store(enabled != 0, std::memory_order_release);
    if (enabled != 0 && state->audio_worker == nullptr)
        state->audio_worker = std::make_unique<std::thread>(wem_audio_worker, state);
}

void wescene_bridge_set_audio_data_callback(void* bridge,
                                            wescene_audio_data_fn fn,
                                            void* userdata)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state == nullptr) return;
    state->audio_data_fn       = fn;
    state->audio_data_userdata = userdata;
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

void wescene_bridge_mouse_button(void* bridge, int button, int down)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state != nullptr) state->scene->mouseButton(button, down != 0);
}

void wescene_bridge_mouse_enter(void* bridge, int in_window)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state != nullptr) state->scene->mouseEnter(in_window != 0);
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

void wescene_bridge_set_user_property(void* bridge,
                                            const char* key,
                                            const char* value)
{
    auto* state = static_cast<BridgeState*>(bridge);
    if (state == nullptr) return;
    // setUserPropertyRaw copies the strings (it posts
    // them as MainMsg::SetUserProperty), so the
    // temporaries only have to outlive this call.
    state->scene->setUserPropertyRaw(make_string(key), make_string(value));
}

} // extern "C"
