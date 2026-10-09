#include <metal_stdlib>
using namespace metal;

struct Params {
    float y_offset;
    float y_scale;
    float r_cr;
    float g_cb;
    float g_cr;
    float b_cb;
};

kernel void nv12_to_bgra(texture2d<float, access::sample> y_texture [[texture(0)]],
                         texture2d<float, access::sample> uv_texture [[texture(1)]],
                         texture2d<half, access::write> output_texture [[texture(2)]],
                         constant Params& params [[buffer(0)]],
                         uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= output_texture.get_width() || gid.y >= output_texture.get_height()) return;
    constexpr sampler sampler_state(coord::normalized, address::clamp_to_edge, filter::linear);
    const float2 uv = (float2(gid) + 0.5f) /
                      float2(output_texture.get_width(), output_texture.get_height());
    const float y = y_texture.sample(sampler_state, uv).r;
    const float2 cbcr = uv_texture.sample(sampler_state, uv).rg - float2(0.5f, 0.5f);
    const float luma = clamp((y - params.y_offset) * params.y_scale, 0.0f, 1.0f);
    const float r = saturate(luma + params.r_cr * cbcr.y);
    const float g = saturate(luma + params.g_cb * cbcr.x + params.g_cr * cbcr.y);
    const float b = saturate(luma + params.b_cb * cbcr.x);
    // MTLTexture shader values are logical RGBA even for BGRA storage.  The
    // Vulkan import uses VK_FORMAT_B8G8R8A8_UNORM, so writing (r,g,b) here
    // preserves the original video colors instead of swapping red and blue.
    output_texture.write(half4(half(r), half(g), half(b), half(1.0f)), gid);
}
