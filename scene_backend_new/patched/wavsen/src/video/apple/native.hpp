#pragma once

struct WavsenMetalAdapter;
struct WavsenMetalTexture;

// Owned handles must be released through the matching destroy function.
const char* wavsen_metal_create(void* device, WavsenMetalAdapter** out);
void wavsen_metal_destroy(WavsenMetalAdapter* adapter);
// On failure, the existing texture remains owned by the caller and unchanged.
const char* wavsen_metal_import(WavsenMetalAdapter* adapter, void* pixel_buffer,
                               WavsenMetalTexture** texture);
void wavsen_metal_texture_destroy(WavsenMetalTexture* texture);
void* wavsen_metal_texture_handle(const WavsenMetalTexture* texture);
