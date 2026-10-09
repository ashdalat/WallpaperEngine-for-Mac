module;
#include "native.hpp"

export module wavsen.video.apple;

import rstd;
import wavsen.video;

using namespace rstd::prelude;
using rstd::collections::HashMap;
using namespace rstd::literals;

export namespace wavsen::video
{

// Keep this owner alive until external GPU use of native_handle() has completed.
class MetalVideoTexture {
public:
    MetalVideoTexture() noexcept                                   = default;
    MetalVideoTexture(const MetalVideoTexture&)                    = delete;
    auto operator=(const MetalVideoTexture&) -> MetalVideoTexture& = delete;
    MetalVideoTexture(MetalVideoTexture&& other) noexcept;
    auto operator=(MetalVideoTexture&& other) noexcept -> MetalVideoTexture&;
    ~MetalVideoTexture();
    auto native_handle() const noexcept -> void*;

private:
    friend class MetalVideoAdapter;
    WavsenMetalTexture* state_ {};
};

// Call from one control thread; retire external GPU use before updating a texture.
class MetalVideoAdapter {
public:
    static auto create(void* device) -> Result<MetalVideoAdapter, Error>;
    MetalVideoAdapter(const MetalVideoAdapter&)                    = delete;
    auto operator=(const MetalVideoAdapter&) -> MetalVideoAdapter& = delete;
    MetalVideoAdapter(MetalVideoAdapter&& other) noexcept;
    auto operator=(MetalVideoAdapter&& other) noexcept -> MetalVideoAdapter&;
    ~MetalVideoAdapter();
    auto import_frame(const AppleFrameLease& frame) -> Result<MetalVideoTexture, Error>;
    auto update(const AppleFrameLease& frame, MetalVideoTexture& texture) -> Result<empty, Error>;

private:
    explicit MetalVideoAdapter(WavsenMetalAdapter* state) noexcept: state_(state) {}
    WavsenMetalAdapter* state_ {};
};

// vrento tracks Metal video textures as raw MTLTexture
// handles. The owning MetalVideoAdapter and MetalVideoTexture
// must stay alive while Vulkan shares the MTLTexture, so they
// are parked here and released through
// release_apple_video_metal_texture().
namespace apple_metal_registry
{

struct OwnedMetalTexture {
    MetalVideoAdapter adapter;
    MetalVideoTexture texture;
};

struct Registry {
    rstd::sync::Mutex<HashMap<void*, OwnedMetalTexture>> map;
};

Registry& registry() {
    static Registry instance;
    return instance;
}

} // namespace apple_metal_registry

// Update `previous_metal_texture` in place when it is still
// registered here, otherwise create a fresh texture and return
// its handle. The caller releases the previous handle when the
// returned handle differs.
Result<void*, Error> create_apple_video_metal_texture(
    const AppleFrameLease& frame, void* metal_device, void* previous_metal_texture) {
    if (metal_device == nullptr) {
        return Err(Error("create_apple_video_metal_texture: no Metal device"_str));
    }
    if (!frame.valid()) {
        return Err(Error("create_apple_video_metal_texture: no frame"_str));
    }

    if (previous_metal_texture != nullptr) {
        auto guard = apple_metal_registry::registry().map.lock().unwrap_unchecked();
        if (auto entry = guard->get_mut(previous_metal_texture)) {
            auto& owned = entry->get_mut();
            auto updated = owned.adapter.update(frame, owned.texture);
            if (updated.is_err()) {
                return Err(rstd::move(updated).unwrap_err());
            }
            // BGRA VideoToolbox frames produce a brand-new MTLTexture on
            // every update() and release the previous one inside it, so
            // the handle the caller still holds names a freed object.
            // Re-key the registry to the texture that is actually alive
            // and hand that handle back, otherwise the caller imports a
            // dangling MTLTexture into Vulkan (crash in MoltenVK's
            // setMTLTexture).
            void* current = owned.texture.native_handle();
            if (current == nullptr) {
                return Err(Error("create_apple_video_metal_texture: update produced no texture"_str));
            }
            if (current != previous_metal_texture) {
                if (auto moved = guard->remove(previous_metal_texture)) {
                    guard->insert(current, rstd::move(*moved));
                }
            }
            return Ok(current);
        }
    }

    auto adapter_result = MetalVideoAdapter::create(metal_device);
    if (adapter_result.is_err()) {
        return Err(rstd::move(adapter_result).unwrap_err());
    }
    auto adapter = rstd::move(adapter_result).unwrap();

    auto texture_result = adapter.import_frame(frame);
    if (texture_result.is_err()) {
        return Err(rstd::move(texture_result).unwrap_err());
    }
    auto texture = rstd::move(texture_result).unwrap();

    void* handle = texture.native_handle();
    auto guard = apple_metal_registry::registry().map.lock().unwrap_unchecked();
    guard->insert(
        handle,
        apple_metal_registry::OwnedMetalTexture {
            rstd::move(adapter),
            rstd::move(texture),
        });
    return Ok(handle);
}

void release_apple_video_metal_texture(void* metal_texture) {
    if (metal_texture == nullptr) return;
    auto guard = apple_metal_registry::registry().map.lock().unwrap_unchecked();
    guard->remove(metal_texture);
}

} // namespace wavsen::video
