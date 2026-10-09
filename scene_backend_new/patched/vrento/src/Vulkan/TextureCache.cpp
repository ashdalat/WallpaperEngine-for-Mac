module;

#include <rstd/macro.hpp>

#include "vvk/macros.hpp"

#include <unistd.h>
#if __is_target_os(macos)
#    include <array>
#    include <vulkan/vulkan.h>
#    include <vulkan/vulkan_metal.h>
#endif

module vrento.vulkan;
import rstd;
import rstd.log;

import vrento.texture_types;
import vrento.image;
import vrento.video_playback;
import wavsen.video;
import wavsen.video.apple;

using rstd::mem::memcpy;

using namespace vrento;
using namespace vrento::vulkan;
using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::ffi::CString;
using rstd::sync::Arc;
using rstd::sync::Weak;

namespace vrento
{
namespace vulkan
{
VkFormat ToVkType(TextureFormat tf) {
    switch (tf) {
    case TextureFormat::BC1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case TextureFormat::BC2: return VK_FORMAT_BC2_UNORM_BLOCK;
    case TextureFormat::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
    case TextureFormat::R8: return VK_FORMAT_R8_UNORM;
    case TextureFormat::RG8: return VK_FORMAT_R8G8_UNORM;
    case TextureFormat::RGB8: return VK_FORMAT_R8G8B8_UNORM;
    case TextureFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::D32F: return VK_FORMAT_D32_SFLOAT;
    default: rstd_assert(false); return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

VkSamplerAddressMode ToVkType(vrento::TextureWrap sam) {
    using namespace vrento;
    switch (sam) {
    case TextureWrap::CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case TextureWrap::CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case TextureWrap::REPEAT:
    default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}
VkFilter ToVkType(vrento::TextureFilter sam) {
    using namespace vrento;
    switch (sam) {
    case TextureFilter::LINEAR: return VK_FILTER_LINEAR;
    case TextureFilter::NEAREST:
    default: return VK_FILTER_NEAREST;
    }
}
} // namespace vulkan
} // namespace vrento

namespace
{
VkBorderColor ToVkBorderColor(TextureBorderColor color) {
    switch (color) {
    case TextureBorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case TextureBorderColor::OpaqueBlack: return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    case TextureBorderColor::OpaqueWhite: return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }
    return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
}

VkSamplerCreateInfo GenSamplerInfo(TextureKey key) {
    auto& sam = key.sample;

    VkSamplerCreateInfo sampler_info { .sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                       .pNext            = nullptr,
                                       .magFilter        = ToVkType(sam.magFilter),
                                       .minFilter        = (ToVkType(sam.minFilter)),
                                       .mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                       .addressModeU     = (ToVkType(sam.wrapS)),
                                       .addressModeV     = (ToVkType(sam.wrapT)),
                                       .addressModeW     = (ToVkType(sam.wrapT)),
                                       .anisotropyEnable = (false),
                                       .maxAnisotropy    = (1.0f),
                                       .compareEnable    = sam.compare_enable,
                                       .compareOp        = ToVkType(sam.compare_op),
                                       .minLod           = (0.0f),
                                       .maxLod           = (1.0f),
                                       .borderColor      = ToVkBorderColor(sam.border_color),
                                       .unnormalizedCoordinates = (false) };
    return sampler_info;
}

VkResult TransImgLayout(const vvk::Queue& queue, vvk::CommandBuffer& cmd,
                        const ImageParameters& image, VkImageLayout layout) {
    VkResult result;
    do {
        result = cmd.Begin(VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        });
        if (result != VK_SUCCESS) break;

        VkImageSubresourceRange subresourceRange {
            .aspectMask     = layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                  ? VK_IMAGE_ASPECT_DEPTH_BIT
                                  : VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount     = VK_REMAINING_ARRAY_LAYERS,
        };
        const bool    depth_layout = layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAccessFlags dst_access   = depth_layout ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                                  : VK_ACCESS_MEMORY_READ_BIT;
        VkPipelineStageFlags dst_stage = depth_layout
                                             ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
                                             : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        {
            VkImageMemoryBarrier out_bar {
                .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext            = nullptr,
                .srcAccessMask    = {},
                .dstAccessMask    = dst_access,
                .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout        = layout,
                .image            = image.handle,
                .subresourceRange = subresourceRange,
            };
            cmd.PipelineBarrier(
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dst_stage, VK_DEPENDENCY_BY_REGION_BIT, out_bar);
        }
        result = cmd.End();
        if (result != VK_SUCCESS) break;

        VkSubmitInfo sub_info {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext              = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers    = cmd.address(),
        };
        result = queue.Submit(sub_info);
    } while (false);
    return result;
}

Option<vvk::DeviceMemory> AllocateMemory(const vvk::Device& device, vvk::PhysicalDevice gpu,
                                         VkMemoryRequirements reqs, VkMemoryPropertyFlags property,
                                         void* pNext = nullptr) {
    VkPhysicalDeviceMemoryProperties pros = gpu.GetMemoryProperties().memoryProperties;
    for (rstd::uint32_t i = 0; i < pros.memoryTypeCount; ++i) {
        if ((reqs.memoryTypeBits & (1u << i)) && (pros.memoryTypes[i].propertyFlags & property)) {
            VkMemoryAllocateInfo memory_allocate_info { .sType =
                                                            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                                        .pNext           = pNext,
                                                        .allocationSize  = reqs.size,
                                                        .memoryTypeIndex = i };
            vvk::DeviceMemory    mem;
            VkResult             res = device.AllocateMemory(memory_allocate_info, mem);
            if (res == VK_SUCCESS) {
                return Some(rstd::move(mem));
            } else {
                VVK_CHECK(res);
                return None();
            }
        }
    }
    rstd_error("vulkan allocate memory failed, no memory match requires");
    return None();
}

// DRM fourcc codes we emit. We currently only render R8G8B8A8_UNORM and
// B8G8R8A8_UNORM into the ExSwapchain, so those are the only mappings.
// Vulkan component order X-Y-Z-W maps to the *first-byte-in-memory* ordering
// used by DRM fourccs ('AB24' = little-endian 'AB24' bytes = A, B, 2, 4 →
// DRM_FORMAT_ABGR8888).
static rstd::uint32_t VkFormatToDrmFourcc(VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_R8G8B8A8_UNORM: return 0x34324241u; // DRM_FORMAT_ABGR8888
    case VK_FORMAT_B8G8R8A8_UNORM: return 0x34324152u; // DRM_FORMAT_ARGB8888
    default: return 0u;
    }
}

Option<ExImageParameters> CreateExImage(rstd::uint32_t width, rstd::uint32_t height,
                                        VkFormat format, VkImageTiling tiling,
                                        VkSamplerCreateInfo sampler_info, VkImageUsageFlags usage,
                                        const vvk::Device& device, const vvk::PhysicalDevice& gpu) {
    ExImageParameters image;
    do {
        // Iteration 1a: switch the external handle type from OPAQUE_FD to
        // real Linux DMA-BUF so the FD is importable outside this Vulkan
        // instance. The OPTIMAL code path would additionally use
        // VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT and pick a modifier from
        // a queried list; we keep LINEAR-only for now so that consumers
        // can mmap the buffer directly (the iteration 4 milestone).
        if (tiling != VK_IMAGE_TILING_LINEAR) {
            rstd_info("[ex-image] OPTIMAL tiling requested; downgrading to LINEAR "
                      "because the DRM-format-modifier path is not yet wired up");
            tiling = VK_IMAGE_TILING_LINEAR;
        }

        VkExternalMemoryImageCreateInfo ex_info {
            .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .pNext       = nullptr,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        };
        VkExportMemoryAllocateInfo ex_mem_info {
            .sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
            .pNext       = nullptr,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        };
        VkImageCreateInfo info {
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = &ex_info,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = format,
            .extent                = VkExtent3D { .width = width, .height = height, .depth = 1 },
            .mipLevels             = 1,
            .arrayLayers           = 1,
            .samples               = VK_SAMPLE_COUNT_1_BIT,
            .tiling                = tiling,
            .usage                 = usage,
            .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        image.extent = info.extent;

        VVK_CHECK_ACT(break, device.CreateImage(info, image.handle));

        image.mem_reqs = device.GetImageMemoryRequirements(*image.handle);

        if (auto opt = AllocateMemory(
                device, gpu, image.mem_reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ex_mem_info);
            opt.is_some()) {
            image.mem = rstd::move(opt).unwrap();
        } else
            break;

        VVK_CHECK_ACT(break, image.handle.BindMemory(*image.mem, 0));
        {
            VkImageViewCreateInfo createinfo {
                .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext    = nullptr,
                .image    = *image.handle,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format   = format,
                .subresourceRange =
                    VkImageSubresourceRange {
                        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel   = 0,
                        .levelCount     = 1,
                        .baseArrayLayer = 0,
                        .layerCount     = 1,
                    },
            };
            VVK_CHECK_ACT(break, device.CreateImageView(createinfo, image.view));
        }
        VVK_CHECK_ACT(break, device.CreateSampler(sampler_info, image.sampler));
        VVK_CHECK_ACT(break, image.mem.GetMemoryFdKHR(&image.fd));

        // Populate the DRM metadata so LocalExSwapchain → ExHandle (and
        // eventually the waywallen-host process) can forward it to external
        // consumers without re-querying Vulkan.
        VkImageSubresource subres {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel   = 0,
            .arrayLayer = 0,
        };
        VkSubresourceLayout layout = device.GetImageSubresourceLayout(*image.handle, subres);
        image.plane0_offset        = layout.offset;
        image.plane0_stride        = static_cast<rstd::uint32_t>(layout.rowPitch);
        image.drm_modifier         = 0; // DRM_FORMAT_MOD_LINEAR
        image.drm_fourcc           = VkFormatToDrmFourcc(format);

        return Some(rstd::move(image));

    } while (false);
    return None();
}

inline Option<AllocatedImageParameters>
CreateImage(const Device& device, VkExtent3D extent, rstd::uint32_t miplevel, VkFormat format,
            VkSamplerCreateInfo sampler_info, VkImageUsageFlags usage,
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) {
    AllocatedImageParameters image;
    do {
        // Multisample images can't have mipmaps; force levelCount=1 and
        // restrict usage to color attachment (no transfer/sampled needed
        // since the resolved sibling carries the readable copy).
        if (samples != VK_SAMPLE_COUNT_1_BIT) {
            miplevel = 1;
            if ((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0)
                usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        VkImageCreateInfo info {
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = nullptr,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = format,
            .extent                = extent,
            .mipLevels             = miplevel,
            .arrayLayers           = 1,
            .samples               = samples,
            .tiling                = VK_IMAGE_TILING_OPTIMAL,
            .usage                 = usage,
            .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        image.extent   = info.extent;
        auto allocated = device.memory_allocator().create_image(info);
        if (allocated.is_err()) {
            const auto error = allocated.unwrap_err_unchecked();
            rstd_error("image allocation failed: kind={}, vk={}",
                       static_cast<int>(error.kind),
                       static_cast<int>(error.api_result));
            break;
        }
        image.handle = allocated.unwrap_unchecked();

        image.mipmap_level = miplevel;
        {
            const bool depth_usage = (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
            VkImageViewCreateInfo createinfo {
                .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext    = nullptr,
                .image    = image.handle.handle(),
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format   = format,
                .subresourceRange =
                    VkImageSubresourceRange {
                        .aspectMask =
                            depth_usage ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel   = 0,
                        .levelCount     = miplevel,
                        .baseArrayLayer = 0,
                        .layerCount     = 1,
                    },
            };
            VVK_CHECK_ACT(break, device.handle().CreateImageView(createinfo, image.view));
        }
        VVK_CHECK_ACT(break, device.handle().CreateSampler(sampler_info, image.sampler));
        return Some(rstd::move(image));
    } while (false);
    /*
    if (result != vk::Result::eSuccess) {
        device.DestroyImageParameters(image);
    }
    */
    return None();
}

} // namespace

usize TextureKey::HashValue(const TextureKey& k) {
    rstd::hash::DefaultHasher state;
    rstd::hash::hash_into(k.width, state);
    rstd::hash::hash_into(k.height, state);
    rstd::hash::hash_into(u32(k.usage), state);
    rstd::hash::hash_into(u32(static_cast<rstd::uint32_t>(k.format)), state);
    rstd::hash::hash_into(u32(k.mipmap_level), state);
    rstd::hash::hash_into(k.sample, state);
    rstd::hash::hash_into(u32(k.samples), state);
    return rstd::as_cast<usize>(state.finish());
}

Option<ExImageParameters> TextureCache::CreateExTex(u32 width, u32 height, VkFormat format,
                                                    VkImageTiling tiling) {
    VkSamplerCreateInfo sampler_info {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = nullptr,
        .magFilter               = VK_FILTER_NEAREST,
        .minFilter               = VK_FILTER_NEAREST,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .anisotropyEnable        = false,
        .maxAnisotropy           = 1.0f,
        .compareEnable           = false,
        .compareOp               = VK_COMPARE_OP_NEVER,
        .minLod                  = 0.0f,
        .maxLod                  = 1.0f,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = false,
    };

    auto opt = CreateExImage(width.to_primitive(),
                             height.to_primitive(),
                             format,
                             tiling,
                             sampler_info,
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                             m_device.device(),
                             m_device.gpu());
    if (opt.is_some()) {
        AssignImageGeneration(*opt);
        const auto& eximg = *opt;

        if (! m_tex_cmd) allocateCmd();
        TransImgLayout(m_device.graphics_queue().handle,
                       m_tex_cmd,
                       ToImageParameters(eximg),
                       VK_IMAGE_LAYOUT_GENERAL);
        VVK_CHECK(m_device.handle().WaitIdle());
    }
    return opt;
}

Option<Arc<TextureAllocation>>
TextureCache::AllocateImportedTexture(const Image&                          image,
                                      Option<Arc<rstd::dyn<VideoPlayback>>> playback) {
    if (image.header.kind == ImageKind::Video) {
        return CreateVideoTex(image, rstd::move(playback));
    }

    ImageSlots img_slots;

    img_slots.slots.reserve(image.slots.len());

    auto& sam = image.header.sample;

    for (usize i {}; i < image.slots.len(); ++i) {
        img_slots.slots.push(AllocatedImageParameters {});
        auto&       image_paras   = img_slots.slots[i];
        const auto& image_slot    = image.slots[i];
        auto        mipmap_levels = image_slot.mipmaps.len().to_primitive();

        // check data
        if (! image_slot) return rstd::None();
        VkSamplerCreateInfo sampler_info {
            .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext                   = nullptr,
            .magFilter               = ToVkType(sam.magFilter),
            .minFilter               = (ToVkType(sam.minFilter)),
            .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU            = (ToVkType(sam.wrapS)),
            .addressModeV            = (ToVkType(sam.wrapS)),
            .addressModeW            = (ToVkType(sam.wrapT)),
            .anisotropyEnable        = (false),
            .maxAnisotropy           = (1.0f),
            .compareEnable           = (false),
            .compareOp               = VK_COMPARE_OP_NEVER,
            .minLod                  = (0.0f),
            .maxLod                  = (float)mipmap_levels,
            .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = (false),
        };
        VkFormat   format = ToVkType(image.header.format);
        VkExtent3D ext { static_cast<rstd::uint32_t>(image_slot.width),
                         static_cast<rstd::uint32_t>(image_slot.height),
                         1 };

        if (auto opt = CreateImage(m_device,
                                   ext,
                                   static_cast<rstd::uint32_t>(mipmap_levels),
                                   format,
                                   sampler_info,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
            opt.is_some()) {
            image_paras = rstd::move(opt).unwrap();
            AssignImageGeneration(image_paras);
        } else {
            return rstd::None();
        }
    }
    return rstd::Some(Arc<TextureAllocation>::make(rstd::move(img_slots)));
}

void TextureCache::allocateCmd() {
    const auto& pool = m_device.cmd_pool();
    VVK_CHECK(pool.Allocate(usize(1), VK_COMMAND_BUFFER_LEVEL_PRIMARY, m_tex_cmds));
    m_tex_cmd = vvk::CommandBuffer(m_tex_cmds[usize()], m_device.handle().Dispatch());
}

Option<AllocatedImageParameters> TextureCache::CreateTex(TextureKey tex_key) {
    AllocatedImageParameters image_paras;
    do {
        VkSamplerCreateInfo sam_info = GenSamplerInfo(tex_key);
        VkFormat            format   = ToVkType(tex_key.format);
        VkExtent3D          ext { static_cast<rstd::uint32_t>(tex_key.width.to_primitive()),
                                  static_cast<rstd::uint32_t>(tex_key.height.to_primitive()),
                                  1 };
        const bool depth_usage = (tex_key.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
        const auto properties  = m_device.gpu().GetFormatProperties(format);
        VkFormatFeatureFlags required_features {};
        if ((tex_key.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0)
            required_features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((tex_key.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0)
            required_features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        if (depth_usage) required_features |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if ((properties.optimalTilingFeatures & required_features) != required_features) {
            rstd_error("texture format {} does not support requested usage {:#x}",
                       static_cast<int>(format),
                       tex_key.usage);
            break;
        }

        if (auto opt = CreateImage(m_device,
                                   ext,
                                   tex_key.mipmap_level,
                                   format,
                                   sam_info,
                                   tex_key.usage,
                                   tex_key.samples);
            opt.is_some()) {
            image_paras = rstd::move(opt).unwrap();
            AssignImageGeneration(image_paras);
        } else
            break;

        // Single-sample images settle in SHADER_READ_ONLY (sampled by other
        // passes). MSAA twin is never sampled — pre-transition to
        // COLOR_ATTACHMENT_OPTIMAL so the first render pass with LoadOp=LOAD
        // doesn't see UNDEFINED on a non-DONT_CARE attachment.
        if (! m_tex_cmd) allocateCmd();
        TransImgLayout(m_device.graphics_queue().handle,
                       m_tex_cmd,
                       ToImageParameters(image_paras),
                       depth_usage ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                       : tex_key.samples == VK_SAMPLE_COUNT_1_BIT
                           ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                           : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        VVK_CHECK_ACT(break, m_device.handle().WaitIdle());
        return Some(rstd::move(image_paras));
    } while (false);
    return None();
}

Option<Arc<TextureAllocation>> TextureCache::AllocateTexture(TextureKey key) {
    auto image = CreateTex(rstd::move(key));
    if (image.is_none()) return None();
    ImageSlots slots;
    slots.slots.push(rstd::move(image).unwrap());
    return Some(Arc<TextureAllocation>::make(rstd::move(slots)));
}

/* ===========================================================================
 * Video-tex pipeline
 *
 * When TexImageParser detects an MP4 / WebM container inlined in a
 * .tex body (header.kind == ImageKind::Video), it doesn't decompress
 * pixels. It stores a typed read range in ImageData instead. CreateTex
 * routes those Images here:
 *
 *   1. We allocate a stable RGBA8 VkImage at (w,h), cleared to black,
 *      and register it in m_tex_map under the Image's key so the rest
 *      of the renderer (descriptor sets, sprite-anim fallback, etc.)
 *      sees an ordinary single-slot texture.
 *   2. Each decoder trial creates an independent reader over that range.
 *   3. We spin up a wavsen::video::VideoDecoder via open_from_stream.
 *      When hwdec is enabled, a lazily-created wavsen Producer supplies
 *      the hwdevice; otherwise the decoder stays sw-only. On Linux the
 *      avformat probe is happy with ftyp/EBML at the head of the stream.
 *   4. Each render tick PumpVideoTextures advances PTS, pulls the
 *      correct frame view for the active decoder kind, and writes the
 *      stable RGBA8 VkImage through wavsen::video::YuvToRgba. Hardware
 *      decode uses vrento's own VkDevice via Producer::from_external so
 *      conversion and material sampling stay on the same device.
 *
 * ========================================================================= */

namespace
{

class RangeInputStream {
public:
    explicit RangeInputStream(rstd::io::ReadRange source)
        : m_length(static_cast<rstd::int64_t>(source.len().to_primitive())),
          m_reader(rstd::move(source).into_reader()) {}

    int read(rstd::uint8_t* buf, int size) {
        if (size <= 0) return 0;
        auto bytes = rstd::mut_ref<rstd::byte[]>::from_raw_parts(
            reinterpret_cast<rstd::byte*>(buf), usize(static_cast<rstd::size_t>(size)));
        auto result = m_reader.read(rstd::as_u8_slice_mut(bytes));
        if (result.is_err()) return -1;
        return static_cast<int>(rstd::move(result).unwrap_unchecked().to_primitive());
    }

    rstd::int64_t seek(rstd::int64_t offset, int whence) {
        constexpr int AVSEEK_SIZE = 0x10000;
        if (whence == AVSEEK_SIZE) return m_length;
        rstd::io::SeekFrom from;
        switch (whence) {
        case 0:
            if (offset < 0) return -1;
            from = rstd::io::SeekFrom::from_start(u64(static_cast<rstd::uint64_t>(offset)));
            break;
        case 1: from = rstd::io::SeekFrom::from_current(i64(offset)); break;
        case 2: from = rstd::io::SeekFrom::from_end(i64(offset)); break;
        default: return -1;
        }
        auto result = m_reader.seek(from);
        return result.is_ok() ? static_cast<rstd::int64_t>(
                                    rstd::move(result).unwrap_unchecked().to_primitive())
                              : -1;
    }

private:
    rstd::int64_t         m_length { 0 };
    rstd::io::RangeReader m_reader;
};

wavsen::video::HwAccel ParseHwdec(ref<str> value) {
    if (value == "vulkan"_str) return wavsen::video::HwAccel::Vulkan;
    if (value == "vaapi"_str) return wavsen::video::HwAccel::Vaapi;
#if __is_target_os(macos)
    if (value == "videotoolbox"_str) return wavsen::video::HwAccel::VideoToolbox;
#endif
    if (value == "none"_str) return wavsen::video::HwAccel::None;
    return wavsen::video::HwAccel::Auto;
}

const char* HwdecLabel(wavsen::video::HwAccel h) {
    switch (h) {
    case wavsen::video::HwAccel::Auto: return "auto";
    case wavsen::video::HwAccel::Vulkan: return "vulkan";
    case wavsen::video::HwAccel::Vaapi: return "vaapi";
    case wavsen::video::HwAccel::VideoToolbox: return "videotoolbox";
    case wavsen::video::HwAccel::None: return "none";
    }
    return "?";
}

const char* FrameKindLabel(wavsen::video::FrameKind k) {
    switch (k) {
    case wavsen::video::FrameKind::Sw: return "sw";
    case wavsen::video::FrameKind::VulkanShared: return "vulkan-shared";
    case wavsen::video::FrameKind::VaapiDrm: return "vaapi-drm";
    case wavsen::video::FrameKind::VideoToolbox: return "videotoolbox";
    }
    return "?";
}

Vec<const char*> ExtensionPtrs(slice<CString> names) {
    auto out = Vec<const char*>::with_capacity(names.len());
    for (const auto& name : names) out.push(name.as_ptr());
    return out;
}

Vec<wavsen::video::QueueFamily> QueueFamiliesForFfmpeg(const Device& device) {
    auto props = device.gpu().GetQueueFamilyProperties();
    auto out   = Vec<wavsen::video::QueueFamily>::with_capacity(props.len());
    for (rstd::uint32_t i = 0; i < props.len().to_primitive(); ++i) {
        out.push(wavsen::video::QueueFamily {
            .index      = u32(i),
            .flags      = props[usize(i)].queueFlags,
            .video_caps = u32(),
        });
    }
    return out;
}

wavsen::video::Producer::ExternalDeviceInfo
MakeExternalProducerInfo(const Device& device, rstd::uint32_t width, rstd::uint32_t height) {
    return wavsen::video::Producer::ExternalDeviceInfo {
        .instance                    = device.instance_handle(),
        .physical_device             = *device.gpu(),
        .device                      = *device.handle(),
        .queue                       = *device.graphics_queue().handle,
        .queue_family_index          = u32(device.graphics_queue().family_index),
        .queue_families              = QueueFamiliesForFfmpeg(device),
        .enabled_instance_extensions = ExtensionPtrs(device.enabled_instance_extensions()),
        .enabled_device_extensions   = ExtensionPtrs(device.enabled_device_extensions()),
        .api_version                 = u32(device.instance_api_version()),
        .width                       = u32(width),
        .height                      = u32(height),
        .instance_dispatch           = &device.instance_dispatch(),
        .device_dispatch             = &device.handle().Dispatch(),
    };
}

void CloseSyncFd(int fd) {
    if (fd >= 0) ::close(fd);
}

#if __is_target_os(macos)
void* ExportMetalDeviceHandle(const Device& device) {
    auto export_metal_objects = reinterpret_cast<PFN_vkExportMetalObjectsEXT>(
        device.handle().Dispatch().vkGetDeviceProcAddr(*device.handle(),
                                                       "vkExportMetalObjectsEXT"));
    if (export_metal_objects == nullptr) return nullptr;

    VkExportMetalDeviceInfoEXT device_info {
        .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_DEVICE_INFO_EXT,
        .pNext = nullptr,
    };
    VkExportMetalObjectsInfoEXT export_info {
        .sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT,
        .pNext = &device_info,
    };
    export_metal_objects(*device.handle(), &export_info);
    return device_info.mtlDevice;
}

Option<ExImageParameters> CreateImportedMetalTextureImage(
    const Device& device, void* metal_texture, rstd::uint32_t width, rstd::uint32_t height,
    VkFormat           format = VK_FORMAT_B8G8R8A8_UNORM,
    VkImageUsageFlags  usage  = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    if (metal_texture == nullptr || width == 0 || height == 0) return None();

    ExImageParameters           image;
    VkImportMetalTextureInfoEXT import_info {
        .sType      = VK_STRUCTURE_TYPE_IMPORT_METAL_TEXTURE_INFO_EXT,
        .pNext      = nullptr,
        .plane      = static_cast<VkImageAspectFlagBits>(aspect),
        .mtlTexture = reinterpret_cast<MTLTexture_id>(metal_texture),
    };
    VkImageCreateInfo image_info {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext         = &import_info,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = format,
        .extent        = VkExtent3D { width, height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = usage,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (device.handle().CreateImage(image_info, image.handle) != VK_SUCCESS) return None();
    image.extent       = image_info.extent;
    image.mipmap_level = 1;

    VkImageViewCreateInfo view_info {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext    = nullptr,
        .image    = *image.handle,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = format,
        .subresourceRange =
            VkImageSubresourceRange {
                .aspectMask     = aspect,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
    };
    if (device.handle().CreateImageView(view_info, image.view) != VK_SUCCESS) {
        image.handle.reset();
        return None();
    }
    return Some(rstd::move(image));
}

#endif

} // anonymous namespace

struct TextureCache::VideoRegistry {
    TextureCache::VideoDecodeOptions      options;
    Option<Box<wavsen::video::Producer>>  producer;
    Option<Box<wavsen::video::YuvToRgba>> yuv;
    rstd::uint32_t                        yuv_max_width { 0 };
    rstd::uint32_t                        yuv_max_height { 0 };

    struct Runtime {
        Runtime()                              = default;
        Runtime(const Runtime&)                = delete;
        Runtime& operator=(const Runtime&)     = delete;
        Runtime(Runtime&&) noexcept            = default;
        Runtime& operator=(Runtime&&) noexcept = default;
        ~Runtime();

        VideoRegistry*                           registry { nullptr };
        TextureCache*                            owner { nullptr };
        const Device*                            device { nullptr };
        String                                   key;
        rstd::uint32_t                           width { 0 };
        rstd::uint32_t                           height { 0 };
        ImageParameters                          target;
        Option<Arc<rstd::dyn<VideoPlayback>>>    playback;
        Option<Box<wavsen::video::VideoDecoder>> decoder;
        wavsen::video::Nv12Frame                 nv12_scratch;
#if __is_target_os(macos)
        struct AppleUploadSlot {
            vvk::CommandBuffers          command_storage;
            vvk::CommandBuffer           command;
            vvk::Fence                   fence;
            Option<ExImageParameters>    imported;
            Option<vvk::SubmissionToken> conversion;
            void*                        metal_texture { nullptr };
            bool                         pending { false };
        };

        rstd::array<AppleUploadSlot, 3> apple_upload_slots;
        rstd::size_t                    next_apple_upload_slot {};
        void*                           metal_device { nullptr };
        bool                            metal_device_queried { false };
        bool                            prepare_apple_upload_slot(AppleUploadSlot&);
        bool                            ensure_apple_upload_command(AppleUploadSlot&);
        void                            retire_apple_upload_slot(AppleUploadSlot&);
        bool upload_apple_frame(const wavsen::video::AppleFrameLease&, const ImageParameters&);
#endif
        f64  pts_acc {};
        f64  last_pts { -1.0 };
        bool have_frame { false };
        u64  applied_seek_sequence {};

        void Pump(double dt_seconds);
    };
    Vec<Weak<dyn<TextureAllocationRuntime>>> runtimes;

    const wavsen::video::Producer* ensureProducer(const Device& device, rstd::uint32_t width,
                                                  rstd::uint32_t height) {
        if (producer.is_some()) return producer->get();
        auto r =
            wavsen::video::Producer::from_external(MakeExternalProducerInfo(device, width, height));
        if (r.is_err()) {
            rstd_warn(
                "CreateVideoTex: shared-device producer unavailable; falling back to sw decode: {}",
                rstd::move(r).unwrap_err().message);
            return nullptr;
        }
        producer = rstd::Some(rstd::move(r).unwrap());
        return producer->get();
    }

    wavsen::video::YuvToRgba* ensureYuv(const Device& device, rstd::uint32_t width,
                                        rstd::uint32_t height) {
        if (yuv.is_some() && width <= yuv_max_width && height <= yuv_max_height) return yuv->get();
        auto next_w = rstd::cmp::max(yuv_max_width, width);
        auto next_h = rstd::cmp::max(yuv_max_height, height);
        auto r      = wavsen::video::YuvToRgba::create(device.instance_dispatch(),
                                                       *device.gpu(),
                                                       device.handle().Dispatch(),
                                                       u32(device.graphics_queue().family_index),
                                                       *device.graphics_queue().handle,
                                                       u32(next_w),
                                                       u32(next_h));
        if (r.is_err()) {
            rstd_error("CreateVideoTex: YuvToRgba create failed: {}",
                       rstd::move(r).unwrap_err().message);
            return nullptr;
        }
        yuv            = rstd::Some(rstd::move(r).unwrap());
        yuv_max_width  = next_w;
        yuv_max_height = next_h;
        return yuv->get();
    }
};

TextureCache::VideoRegistry::Runtime::~Runtime() {
#if __is_target_os(macos)
    for (auto& slot : apple_upload_slots) {
        retire_apple_upload_slot(slot);
        if (slot.metal_texture != nullptr) {
            wavsen::video::release_apple_video_metal_texture(slot.metal_texture);
            slot.metal_texture = nullptr;
        }
    }
#endif
}

#if __is_target_os(macos)
bool TextureCache::VideoRegistry::Runtime::prepare_apple_upload_slot(AppleUploadSlot& slot) {
    retire_apple_upload_slot(slot);
    return true;
}

bool TextureCache::VideoRegistry::Runtime::ensure_apple_upload_command(AppleUploadSlot& slot) {
    if (! slot.command) {
        if (device == nullptr) return false;
        if (device->cmd_pool().Allocate(
                usize(1), VK_COMMAND_BUFFER_LEVEL_PRIMARY, slot.command_storage) != VK_SUCCESS) {
            return false;
        }
        slot.command =
            vvk::CommandBuffer(slot.command_storage[usize()], device->handle().Dispatch());

        VkFenceCreateInfo fence_info {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        };
        if (device->handle().CreateFence(fence_info, slot.fence) != VK_SUCCESS) return false;
    }
    return true;
}

void TextureCache::VideoRegistry::Runtime::retire_apple_upload_slot(AppleUploadSlot& slot) {
    if (slot.pending) {
        VkResult result = slot.fence ? slot.fence.Wait() : VK_SUCCESS;
        if (result != VK_SUCCESS && device != nullptr) {
            rstd_warn("PumpVideoTextures[{}]: video upload fence wait failed ({}), draining device",
                      key.as_str(),
                      static_cast<int>(result));
            (void)device->handle().WaitIdle();
        }
        if (slot.fence) (void)slot.fence.Reset();
        slot.pending = false;
    }
    if (slot.conversion.is_some()) {
        bool completed = false;
        if (registry != nullptr && registry->yuv.is_some()) {
            const auto observation =
                (*registry->yuv)->wait_completion(*slot.conversion, u64(0xffffffffffffffffull));
            completed = observation.observed() && observation.completed.valid() &&
                        vvk::CompletionCovers(observation.completed, *slot.conversion);
            if (! completed && device != nullptr) {
                rstd_warn(
                    "PumpVideoTextures[{}]: external NV12 conversion wait failed, draining device",
                    key.as_str());
                (void)device->handle().WaitIdle();
            }
            (void)(*registry->yuv)->reclaim_submissions();
        }
        if (! completed && device != nullptr && (registry == nullptr || registry->yuv.is_none())) {
            (void)device->handle().WaitIdle();
        }
        (void)slot.conversion.take();
    }
    (void)slot.imported.take();
}

bool TextureCache::VideoRegistry::Runtime::upload_apple_frame(
    const wavsen::video::AppleFrameLease& frame, const ImageParameters& target) {
    if (owner == nullptr || device == nullptr || ! frame.valid()) return false;
    if (metal_device == nullptr && ! metal_device_queried) {
        metal_device         = ExportMetalDeviceHandle(*device);
        metal_device_queried = true;
    }
    if (metal_device == nullptr) {
        rstd_error("PumpVideoTextures[{}]: VK_EXT_metal_objects did not expose an MTLDevice",
                   key.as_str());
        return false;
    }

    auto& slot = apple_upload_slots[usize(next_apple_upload_slot++ %
                                          apple_upload_slots.len().to_primitive())];
    if (! prepare_apple_upload_slot(slot)) {
        rstd_error("PumpVideoTextures[{}]: failed to prepare asynchronous video upload slot",
                   key.as_str());
        return false;
    }

    if (! ensure_apple_upload_command(slot)) {
        rstd_error("PumpVideoTextures[{}]: failed to allocate BGRA upload command", key.as_str());
        return false;
    }

    void* previous_metal_texture = slot.metal_texture;
    auto  texture_result         = wavsen::video::create_apple_video_metal_texture(
        frame, metal_device, previous_metal_texture);
    if (texture_result.is_err()) {
        rstd_error("PumpVideoTextures[{}]: VideoToolbox Metal texture: {}",
                   key.as_str(),
                   texture_result.unwrap_err().message);
        return false;
    }
    void* metal_texture = rstd::move(texture_result).unwrap();
    if (previous_metal_texture != nullptr && previous_metal_texture != metal_texture) {
        wavsen::video::release_apple_video_metal_texture(previous_metal_texture);
    }
    slot.metal_texture = metal_texture;
    auto imported      = CreateImportedMetalTextureImage(*device,
                                                         metal_texture,
                                                         frame.view().width.to_primitive(),
                                                         frame.view().height.to_primitive());
    if (imported.is_none()) {
        retire_apple_upload_slot(slot);
        rstd_error("PumpVideoTextures[{}]: failed to import Metal video texture into Vulkan",
                   key.as_str());
        return false;
    }
    slot.imported = Some(rstd::move(imported).unwrap());
    auto& cmd     = slot.command;
    if (cmd.Begin(VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        }) != VK_SUCCESS) {
        retire_apple_upload_slot(slot);
        return false;
    }

    VkImageSubresourceRange range {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    VkImageMemoryBarrier imported_to_src {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = 0,
        .dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .image            = *slot.imported->handle,
        .subresourceRange = range,
    };
    VkImageMemoryBarrier target_to_dst {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image            = target.handle,
        .subresourceRange = range,
    };
    cmd.PipelineBarrier(
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, imported_to_src);
    cmd.PipelineBarrier(
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, target_to_dst);

    VkImageBlit blit {
        .srcSubresource =
            VkImageSubresourceLayers {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        .srcOffsets = { VkOffset3D { 0, 0, 0 },
                        VkOffset3D { static_cast<int>(frame.view().width.to_primitive()),
                                     static_cast<int>(frame.view().height.to_primitive()),
                                     1 } },
        .dstSubresource =
            VkImageSubresourceLayers {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        .dstOffsets = { VkOffset3D { 0, 0, 0 },
                        VkOffset3D { static_cast<int>(width), static_cast<int>(height), 1 } },
    };
    cmd.BlitImage(*slot.imported->handle,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  target.handle,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  blit,
                  VK_FILTER_LINEAR);
    VkImageMemoryBarrier target_to_shader {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image            = target.handle,
        .subresourceRange = range,
    };
    cmd.PipelineBarrier(
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, target_to_shader);
    if (cmd.End() != VK_SUCCESS) {
        retire_apple_upload_slot(slot);
        return false;
    }
    VkSubmitInfo submit {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext              = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers    = cmd.address(),
    };
    if (device->graphics_queue().handle.Submit(submit, *slot.fence) != VK_SUCCESS) {
        retire_apple_upload_slot(slot);
        return false;
    }
    slot.pending = true;
    return true;
}
#endif

namespace rstd
{

template<>
struct Impl<vrento::vulkan::TextureAllocationRuntime,
            vrento::vulkan::TextureCache::VideoRegistry::Runtime>
    : ImplBase<vrento::vulkan::TextureCache::VideoRegistry::Runtime> {
    void Pump(double seconds) { this->self().Pump(seconds); }
};

} // namespace rstd

Option<Arc<TextureAllocation>>
TextureCache::CreateVideoTex(const Image& image, Option<Arc<rstd::dyn<VideoPlayback>>> playback) {
    if (image.slots.is_empty() || image.slots[usize(0)].mipmaps.is_empty()) return rstd::None();
    auto& mip = image.slots[usize(0)].mipmaps[usize(0)];
    if (mip.video_source.is_none() || mip.width <= 0 || mip.height <= 0) {
        rstd_error("CreateVideoTex: incomplete video-tex slot for {}", image.key);
        return rstd::None();
    }

    if (m_video_registry.is_none()) {
        m_video_registry                 = Some(Box<VideoRegistry>::make());
        m_video_registry->get()->options = m_video_decode_options.clone();
    }
    auto* registry = m_video_registry->get();
    if (! m_tex_cmd) allocateCmd();

    auto video_source = (*mip.video_source).clone();

    VideoRegistry::Runtime runtime;
    runtime.registry = registry;
    runtime.owner    = this;
    runtime.device   = &m_device;
    runtime.key      = image.key.clone();
    runtime.playback = rstd::move(playback);
    /* NV12 chroma is 4:2:0 → both dims even. */
    const auto source_width  = static_cast<rstd::uint32_t>(mip.width);
    const auto source_height = static_cast<rstd::uint32_t>(mip.height);
    runtime.width            = source_width | (source_width & 1u);
    runtime.height           = source_height | (source_height & 1u);
    if (runtime.width != source_width) runtime.width = source_width + 1u;
    if (runtime.height != source_height) {
        runtime.height = source_height + 1u;
    }

    /* 1) Allocate the stable RGBA8 target. */
    VkSamplerCreateInfo sampler_info {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = nullptr,
        .magFilter               = ToVkType(image.header.sample.magFilter),
        .minFilter               = ToVkType(image.header.sample.minFilter),
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU            = ToVkType(image.header.sample.wrapS),
        .addressModeV            = ToVkType(image.header.sample.wrapS),
        .addressModeW            = ToVkType(image.header.sample.wrapT),
        .anisotropyEnable        = false,
        .maxAnisotropy           = 1.0f,
        .compareEnable           = false,
        .compareOp               = VK_COMPARE_OP_NEVER,
        .minLod                  = 0.0f,
        .maxLod                  = 1.0f,
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = false,
    };
    VkExtent3D ext { runtime.width, runtime.height, 1 };
    auto       img_opt = CreateImage(m_device,
                                     ext,
                                     /*miplevel=*/1u,
                                     VK_FORMAT_R8G8B8A8_UNORM,
                                     sampler_info,
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                         VK_IMAGE_USAGE_SAMPLED_BIT);
    if (! img_opt) {
        rstd_error("CreateVideoTex: VkImage allocation failed for {}", image.key);
        return rstd::None();
    }
    auto target_image = rstd::move(*img_opt);
    AssignImageGeneration(target_image);
    runtime.target = ToImageParameters(target_image);
#if ! __is_target_os(macos)
    if (! registry->ensureYuv(m_device, runtime.width, runtime.height)) return None();
#endif

    /* 2) Initial layout: UNDEFINED → TRANSFER_DST → clear black →
     * SHADER_READ_ONLY. Mirrors the one-shot pattern used by the
     * existing TransImgLayout / CopyImageData helpers in this file. */
    {
        ImageParameters ip = runtime.target;
        VVK_CHECK(m_tex_cmd.Begin(VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        }));
        VkImageSubresourceRange range {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
        VkImageMemoryBarrier to_xfer {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = 0,
            .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image            = ip.handle,
            .subresourceRange = range,
        };
        m_tex_cmd.PipelineBarrier(
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, to_xfer);
        VkClearColorValue clear { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
        m_tex_cmd.ClearColorImage(ip.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, range);
        VkImageMemoryBarrier to_shader {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image            = ip.handle,
            .subresourceRange = range,
        };
        m_tex_cmd.PipelineBarrier(
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, to_shader);
        VVK_CHECK(m_tex_cmd.End());
        VkSubmitInfo si {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers    = m_tex_cmd.address(),
        };
        VVK_CHECK(m_device.graphics_queue().handle.Submit(si));
        VVK_CHECK(m_device.handle().WaitIdle());
    }

    /* 3) Open the decoder. Each backend trial gets its own range cursor. */
    auto factory = Box<dyn<FnMut<Box<dyn<wavsen::video::InputStream>>()>>>::make(
        [source = rstd::move(video_source)]() -> Box<dyn<wavsen::video::InputStream>> {
            return Box<dyn<wavsen::video::InputStream>>::make(RangeInputStream(source.clone()));
        });
    const auto              requested_hwdec = ParseHwdec(registry->options.hwdec.as_str());
    wavsen::video::OpenOpts opts {
        requested_hwdec,
        registry->options.render_node.clone(),
    };
    const wavsen::video::Producer* producer = nullptr;
#if __is_target_os(macos)
    const bool needs_shared_vulkan = requested_hwdec == wavsen::video::HwAccel::Vulkan;
#else
    const bool needs_shared_vulkan = requested_hwdec != wavsen::video::HwAccel::None;
#endif
    if (needs_shared_vulkan) {
        producer = registry->ensureProducer(m_device, runtime.width, runtime.height);
        if (! producer) opts.hwaccel = wavsen::video::HwAccel::None;
    }
    auto dec_r = wavsen::video::VideoDecoder::open_from_stream(rstd::move(factory),
                                                               u32(runtime.width),
                                                               u32(runtime.height),
                                                               /*loop=*/true,
                                                               producer,
                                                               opts);
    if (dec_r.is_err()) {
        rstd_error("CreateVideoTex: open_from_stream failed for {}: {}",
                   image.key,
                   dec_r.unwrap_err().message);
        return None();
    }
    runtime.decoder = rstd::Some(rstd::move(dec_r).unwrap());
#if __is_target_os(macos)
    if ((*runtime.decoder)->kind() != wavsen::video::FrameKind::VideoToolbox &&
        ! registry->ensureYuv(m_device, runtime.width, runtime.height)) {
        return None();
    }
#endif
    if (runtime.playback.is_some()) {
        (*runtime.playback)->PublishTime(f64(), (*runtime.decoder)->duration());
    }
    rstd_info("CreateVideoTex: {} hwdec={} decoder kind={}",
              image.key,
              HwdecLabel(requested_hwdec),
              FrameKindLabel((*runtime.decoder)->kind()));

    ImageSlots img_slots {};
    img_slots.slots.push(rstd::move(target_image));
    auto runtime_owner = Arc<dyn<TextureAllocationRuntime>>::make(rstd::move(runtime));
    auto allocation =
        Arc<TextureAllocation>::make(rstd::move(img_slots), Some(runtime_owner.clone()));
    registry->runtimes.push(runtime_owner.downgrade());
    return Some(rstd::move(allocation));
}

void TextureCache::VideoRegistry::Runtime::Pump(double dt_seconds) {
    if (registry == nullptr || device == nullptr || decoder.is_none()) return;
    auto& s            = *this;
    auto  publish_time = [&] {
        if (s.playback.is_some()) {
            (*s.playback)->PublishTime(s.pts_acc, (*s.decoder)->duration());
        }
    };
    if (s.playback.is_some()) {
        auto state = (*s.playback)->Snapshot();
        if (state.seek_sequence != s.applied_seek_sequence) {
            auto seeked             = (*s.decoder)->seek(state.seek_seconds);
            s.applied_seek_sequence = state.seek_sequence;
            if (seeked.is_err()) {
                rstd_error("PumpVideoTextures[{}]: seek: {}",
                           s.key.as_str(),
                           rstd::move(seeked).unwrap_err().message);
            } else {
                s.pts_acc    = state.seek_seconds;
                s.last_pts   = f64(-1.0);
                s.have_frame = false;
            }
        }
        if (! state.playing) {
            publish_time();
            return;
        }
        dt_seconds *= state.rate.to_primitive();
    }
    s.pts_acc += f64(dt_seconds);
    ImageParameters ip = s.target;

    const auto                fkind = (*s.decoder)->kind();
    wavsen::video::YuvToRgba* yuv   = nullptr;
    if (fkind != wavsen::video::FrameKind::VideoToolbox) {
        yuv = registry->ensureYuv(*device, s.width, s.height);
    }
    if (! yuv && fkind != wavsen::video::FrameKind::VideoToolbox) {
        publish_time();
        return;
    }
    Option<wavsen::video::VkFrameLease>    vulkan_frame;
    Option<wavsen::video::VaapiFrameLease> vaapi_frame;
    Option<wavsen::video::AppleFrameLease> apple_frame;

    /* Drain decoded frames until we catch up to wall time. Cap to
     * 4 frames per tick to avoid spiral-of-death on heavy stalls. */
    bool got_new = false;
    for (int i = 0; i < 4; ++i) {
        if (s.last_pts >= f64() && s.last_pts > s.pts_acc) break;

        rstd::Result<wavsen::video::NextFrame, wavsen::video::Error> r =
            rstd::Ok(wavsen::video::NextFrame::Ok);
        switch (fkind) {
        case wavsen::video::FrameKind::VulkanShared: {
            auto pulled = (*s.decoder)->next_vk_frame();
            if (pulled.is_err()) {
                r = Err(rstd::move(pulled).unwrap_err());
            } else {
                auto value = rstd::move(pulled).unwrap();
                r          = Ok(value.status);
                if (value.frame.is_some()) vulkan_frame = rstd::move(value.frame);
            }
            break;
        }
        case wavsen::video::FrameKind::VaapiDrm: {
            auto pulled = (*s.decoder)->next_vaapi_frame();
            if (pulled.is_err()) {
                r = Err(rstd::move(pulled).unwrap_err());
            } else {
                auto value  = rstd::move(pulled).unwrap();
                r           = Ok(value.status);
                vaapi_frame = rstd::move(value.frame);
            }
            break;
        }
        case wavsen::video::FrameKind::VideoToolbox: {
            auto pulled = (*s.decoder)->next_apple_frame();
            if (pulled.is_err()) {
                r = Err(rstd::move(pulled).unwrap_err());
            } else {
                auto value = rstd::move(pulled).unwrap();
                r          = Ok(value.status);
                if (value.frame.is_some()) apple_frame = rstd::move(value.frame);
            }
            break;
        }
        case wavsen::video::FrameKind::Sw: r = (*s.decoder)->next_frame(s.nv12_scratch); break;
        }
        if (r.is_err()) {
            rstd_error("PumpVideoTextures[{}]: decode {}: {}",
                       s.key.as_str(),
                       FrameKindLabel(fkind),
                       rstd::move(r).unwrap_err().message);
            break;
        }
        auto kind = r.unwrap();
        if (kind == wavsen::video::NextFrame::Eof) {
            s.pts_acc  = f64();
            s.last_pts = f64(-1.0);
            break;
        }
        const bool decoder_looped = kind == wavsen::video::NextFrame::Looped;
        f64        frame_pts { -1.0 };
        switch (fkind) {
        case wavsen::video::FrameKind::VulkanShared:
            if (vulkan_frame.is_none()) {
                rstd_error("PumpVideoTextures[{}]: Vulkan decode returned no frame lease",
                           s.key.as_str());
                publish_time();
                return;
            }
            frame_pts = vulkan_frame->info().pts_seconds;
            break;
        case wavsen::video::FrameKind::VaapiDrm:
            if (vaapi_frame.is_none()) {
                rstd_error("PumpVideoTextures[{}]: VAAPI decode returned no surface lease",
                           s.key.as_str());
                publish_time();
                return;
            }
            frame_pts = vaapi_frame->view().pts_seconds;
            break;
        case wavsen::video::FrameKind::VideoToolbox:
            if (apple_frame.is_none()) {
                rstd_error("PumpVideoTextures[{}]: VideoToolbox decode returned no frame lease",
                           s.key.as_str());
                publish_time();
                return;
            }
            frame_pts = apple_frame->view().pts_seconds;
            break;
        case wavsen::video::FrameKind::Sw: frame_pts = s.nv12_scratch.pts_seconds; break;
        }
        if (decoder_looped) s.pts_acc = frame_pts.max(f64());
        s.last_pts = frame_pts;
        got_new    = true;
        if (decoder_looped) break;
    }
    if (! got_new && s.have_frame) {
        publish_time();
        return;
    }
    if (! got_new) {
        publish_time();
        return;
    }

    u32 cs_id;
    u32 cr_id;
    switch (fkind) {
    case wavsen::video::FrameKind::VulkanShared:
        cs_id = vulkan_frame->info().colorspace;
        cr_id = vulkan_frame->info().color_range;
        break;
    case wavsen::video::FrameKind::VaapiDrm:
        cs_id = vaapi_frame->view().colorspace;
        cr_id = vaapi_frame->view().color_range;
        break;
    case wavsen::video::FrameKind::VideoToolbox:
        cs_id = apple_frame->view().colorspace;
        cr_id = apple_frame->view().color_range;
        break;
    case wavsen::video::FrameKind::Sw:
        cs_id = s.nv12_scratch.colorspace;
        cr_id = s.nv12_scratch.color_range;
        break;
    }
    const auto color_matrix = wavsen::video::make_color_matrix(
        static_cast<wavsen::video::ColorSpace>(cs_id.to_primitive()),
        static_cast<wavsen::video::ColorRange>(cr_id.to_primitive()));

    rstd::Result<int, wavsen::video::Error> cv = rstd::Ok(-1);
    switch (fkind) {
    case wavsen::video::FrameKind::VulkanShared: {
        auto reserved = yuv->reserve({
            .target = {
                .image  = ip.handle,
                .view   = ip.view,
                .width  = u32(s.width),
                .height = u32(s.height),
                .kind   = wavsen::video::ConvertTarget::SampledLocal,
            },
        });
        if (reserved.is_err()) {
            cv = Err(rstd::move(reserved).unwrap_err());
            break;
        }
        auto reservation = rstd::move(reserved).unwrap();
        if (reservation.is_none()) {
            publish_time();
            return;
        }
        auto submitted = yuv->submit_av_vk_frame(
            rstd::move(*reservation), rstd::move(*vulkan_frame), color_matrix);
        if (submitted.is_err()) {
            cv = Err(rstd::move(submitted).unwrap_err());
        } else {
            cv = Ok(rstd::move(submitted).unwrap().sync_fd);
        }
        break;
    }
    case wavsen::video::FrameKind::VaapiDrm: {
        auto reserved = yuv->reserve({
            .target = {
                .image  = ip.handle,
                .view   = ip.view,
                .width  = u32(s.width),
                .height = u32(s.height),
                .kind   = wavsen::video::ConvertTarget::SampledLocal,
            },
        });
        if (reserved.is_err()) {
            cv = Err(rstd::move(reserved).unwrap_err());
            break;
        }
        auto reservation = rstd::move(reserved).unwrap();
        if (reservation.is_none()) {
            publish_time();
            return;
        }
        auto mapped = rstd::move(*vaapi_frame).into_drm();
        if (mapped.is_err()) {
            cv = Err(rstd::move(mapped).unwrap_err());
            break;
        }
        auto submitted = yuv->submit_drm_prime(
            rstd::move(*reservation), rstd::move(mapped).unwrap(), color_matrix);
        if (submitted.is_err()) {
            cv = Err(rstd::move(submitted).unwrap_err());
            break;
        }
        auto value = rstd::move(submitted).unwrap();
        if (value.is_none()) {
            publish_time();
            return;
        }
        cv = Ok(value->sync_fd);
        break;
    }
    case wavsen::video::FrameKind::VideoToolbox:
#if __is_target_os(macos)
        if (apple_frame.is_none() || ! s.upload_apple_frame(*apple_frame, ip)) {
            cv = Err(wavsen::video::Error(rstd::format("VideoToolbox Metal→Vulkan copy failed")));
        } else {
            cv = Ok(-1);
        }
#else
        cv = Err(wavsen::video::Error(
            rstd::format("VideoToolbox is only available on Apple platforms")));
#endif
        break;
    case wavsen::video::FrameKind::Sw:
        cv = yuv->convert_nv12(ip.handle,
                               u32(s.width),
                               u32(s.height),
                               s.nv12_scratch.data.data(),
                               s.nv12_scratch.data.len(),
                               color_matrix,
                               wavsen::video::ConvertTarget::SampledLocal);
        break;
    }
    if (cv.is_err()) {
        rstd_error("PumpVideoTextures[{}]: yuv conversion {}: {}",
                   s.key.as_str(),
                   FrameKindLabel(fkind),
                   rstd::move(cv).unwrap_err().message);
        publish_time();
        return;
    }
    CloseSyncFd(rstd::move(cv).unwrap());
    s.have_frame = true;
    publish_time();
}

void TextureCache::PumpVideoTextures(double dt_seconds) {
    if (m_video_registry.is_none()) return;
    auto* registry = m_video_registry->get();
    registry->runtimes.retain([](const Weak<dyn<TextureAllocationRuntime>>& runtime) {
        return ! runtime.expired();
    });
    for (const auto& weak : registry->runtimes) {
        auto runtime = weak.upgrade();
        if (runtime) runtime->Pump(dt_seconds);
    }
}

bool TextureCache::UploadFontAtlasRegion(ref<TextureAllocation> texture, const rstd::uint8_t* atlas,
                                         rstd::uint32_t atlas_w, rstd::uint32_t x, rstd::uint32_t y,
                                         rstd::uint32_t w, rstd::uint32_t h) {
    if (w == 0 || h == 0) return true;
    auto view = texture->View();
    if (view.slots.is_empty()) return false;
    ImageParameters ip = view.getActive();

    // Tightly-packed staging buffer for the AABB. Allocating per-call keeps
    // this code path independent of the video-tex ring; atlas pumps are
    // small (a handful of glyphs per frame) so cost is negligible.
    const VkDeviceSize        bytes = static_cast<VkDeviceSize>(w) * h;
    AllocatedBufferParameters stage;
    if (! CreateStagingBuffer(m_device.memory_allocator(), bytes, stage)) return false;

    {
        auto memory = stage.handle.allocation();
        auto mapped = memory.map();
        if (mapped.is_err()) return false;
        auto  mapping = mapped.unwrap_unchecked();
        auto* dst     = static_cast<rstd::uint8_t*>(mapping.data());
        for (rstd::uint32_t row = 0; row < h; ++row) {
            memcpy(dst + row * w,
                   atlas + (y + row) * atlas_w + x,
                   rstd::usize(static_cast<rstd::size_t>(w)));
        }
        if (memory.flush().is_err()) return false;
    }

    if (! m_tex_cmd) allocateCmd();
    VVK_CHECK(m_tex_cmd.Begin(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    }));
    VkImageSubresourceRange range {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    VkImageMemoryBarrier to_xfer {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image            = ip.handle,
        .subresourceRange = range,
    };
    m_tex_cmd.PipelineBarrier(
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, to_xfer);
    VkBufferImageCopy region {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset =
        VkOffset3D { static_cast<rstd::int32_t>(x), static_cast<rstd::int32_t>(y), 0 };
    region.imageExtent = VkExtent3D { w, h, 1 };
    m_tex_cmd.CopyBufferToImage(
        stage.handle.handle(), ip.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, region);
    VkImageMemoryBarrier to_shader {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image            = ip.handle,
        .subresourceRange = range,
    };
    m_tex_cmd.PipelineBarrier(
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, to_shader);
    VVK_CHECK(m_tex_cmd.End());
    VkSubmitInfo si {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = m_tex_cmd.address(),
    };
    VVK_CHECK(m_device.graphics_queue().handle.Submit(si));
    VVK_CHECK(m_device.handle().WaitIdle());
    return true;
}

TextureCache::TextureCache(const Device& device): m_device(device) {}

TextureCache::~TextureCache() {
    if (m_video_registry.is_none() || m_video_registry->get()->yuv.is_none()) return;
    auto* yuv = m_video_registry->get()->yuv->get();
    if (auto drained = yuv->drain_submissions(u64(1'000'000'000)); drained.is_err()) {
        rstd_warn("TextureCache: conversion drain failed during shutdown: {}",
                  rstd::move(drained).unwrap_err().message);
        (void)m_device.handle().WaitIdle();
        (void)yuv->reclaim_submissions();
    }
}

u64 TextureCache::nextImageGeneration() { return m_next_image_generation++; }

void TextureCache::AssignImageGeneration(AllocatedImageParameters& image) {
    image.generation = nextImageGeneration();
}

void TextureCache::AssignImageGeneration(ExImageParameters& image) {
    image.generation = nextImageGeneration();
}

void TextureCache::SetVideoDecodeOptions(VideoDecodeOptions options) {
    m_video_decode_options = rstd::move(options);
    if (m_video_registry.is_some()) {
        auto* registry    = m_video_registry->get();
        registry->options = m_video_decode_options.clone();
        registry->runtimes.retain([](const Weak<dyn<TextureAllocationRuntime>>& runtime) {
            return ! runtime.expired();
        });
        if (registry->runtimes.is_empty()) (void)registry->producer.take();
    }
}

void TextureCache::Clear() {
    if (m_video_registry.is_some()) m_video_registry->get()->runtimes.clear();
}

void vrento::vulkan::RecordGenerateMipmaps(vvk::CommandBuffer& cmd, const ImageParameters& image) {
    VkImageMemoryBarrier barrier {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image.handle,
        .subresourceRange =
            VkImageSubresourceRange {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
    };
    /*
    cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT,
                        out_bar);
        */

    rstd::int32_t mipWidth  = static_cast<rstd::int32_t>(image.extent.width);
    rstd::int32_t mipHeight = static_cast<rstd::int32_t>(image.extent.height);

    for (unsigned i = 1; i < image.mipmap_level; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout                     = i == 1 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                       : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = i == 1 ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VkPipelineStageFlags src_stage =
            i == 1 ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;
        cmd.PipelineBarrier(
            src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_DEPENDENCY_BY_REGION_BIT, barrier);

        barrier.subresourceRange.baseMipLevel = i;
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask                 = 0;
        barrier.dstAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;

        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_DEPENDENCY_BY_REGION_BIT,
                            barrier);

        VkImageBlit blit {
            .srcSubresource =
                VkImageSubresourceLayers {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel       = i - 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
            .srcOffsets = { VkOffset3D { 0, 0, 0 }, VkOffset3D { mipWidth, mipHeight, 1 } },
            .dstOffsets = { VkOffset3D { 0, 0, 0 },
                            VkOffset3D { mipWidth > 1 ? mipWidth / 2 : 1,
                                         mipHeight > 1 ? mipHeight / 2 : 1,
                                         1 } },
        };
        blit.dstSubresource =
            VkImageSubresourceLayers {
                .aspectMask     = blit.srcSubresource.aspectMask,
                .mipLevel       = blit.srcSubresource.mipLevel + 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },

        cmd.BlitImage(image.handle,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      image.handle,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      blit,
                      VK_FILTER_LINEAR);

        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask                 = VK_ACCESS_SHADER_READ_BIT;

        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_DEPENDENCY_BY_REGION_BIT,
                            barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = image.mipmap_level - 1;
    barrier.oldLayout                     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout                     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask                 = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask                 = VK_ACCESS_SHADER_READ_BIT;

    cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_DEPENDENCY_BY_REGION_BIT,
                        barrier);
}
