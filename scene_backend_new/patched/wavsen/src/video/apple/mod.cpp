module;
#include "native.hpp"
#include <rstd/macro.hpp>

module wavsen.video.apple;

import rstd;
import wavsen.video;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace wavsen::video
{
MetalVideoTexture::MetalVideoTexture(MetalVideoTexture&& other) noexcept
    : state_(rstd::exchange(other.state_, nullptr)) {}

auto MetalVideoTexture::operator=(MetalVideoTexture&& other) noexcept -> MetalVideoTexture& {
    if (this != &other) {
        wavsen_metal_texture_destroy(state_);
        state_ = rstd::exchange(other.state_, nullptr);
    }
    return *this;
}

MetalVideoTexture::~MetalVideoTexture() { wavsen_metal_texture_destroy(state_); }

auto MetalVideoTexture::native_handle() const noexcept -> void* {
    return wavsen_metal_texture_handle(state_);
}

auto MetalVideoAdapter::create(void* device) -> Result<MetalVideoAdapter, Error> {
    WavsenMetalAdapter* state {};
    if (auto error = wavsen_metal_create(device, &state))
        return Err(Error(String::make(rstd::ffi::CStr::from_ptr(error).to_str().unwrap())));
    return Ok(MetalVideoAdapter(state));
}

MetalVideoAdapter::MetalVideoAdapter(MetalVideoAdapter&& other) noexcept
    : state_(rstd::exchange(other.state_, nullptr)) {}

auto MetalVideoAdapter::operator=(MetalVideoAdapter&& other) noexcept -> MetalVideoAdapter& {
    if (this != &other) {
        wavsen_metal_destroy(state_);
        state_ = rstd::exchange(other.state_, nullptr);
    }
    return *this;
}

MetalVideoAdapter::~MetalVideoAdapter() { wavsen_metal_destroy(state_); }

auto MetalVideoAdapter::import_frame(const AppleFrameLease& frame)
    -> Result<MetalVideoTexture, Error> {
    auto texture = MetalVideoTexture {};
    rstd_try(update(frame, texture));
    return Ok(rstd::move(texture));
}

auto MetalVideoAdapter::update(const AppleFrameLease& frame, MetalVideoTexture& texture)
    -> Result<empty, Error> {
    if (! state_ || ! frame.valid()) return Err(Error("invalid apple video adapter or frame"_str));
    if (auto error = wavsen_metal_import(state_, frame.view().pixel_buffer, &texture.state_))
        return Err(Error(String::make(rstd::ffi::CStr::from_ptr(error).to_str().unwrap())));
    return Ok(empty {});
}
} // namespace wavsen::video
