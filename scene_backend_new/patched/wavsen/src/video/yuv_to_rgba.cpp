module;
#include <cstdint>
#include "nv12_to_rgba.spv.h" // generated at build time by glslangValidator

module wavsen.video;

import rstd.log;
import rstd;
import vvk;
import :vk_device;
import :yuv_to_rgba;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;

namespace wavsen::video
{

/* Push-constant struct mirroring `PC` in shaders/nv12_to_rgba.comp.
 * std140-friendly: padded to 16-byte boundaries. */
struct alignas(16) ShaderPushConstants {
    rstd::uint32_t dst_w;
    rstd::uint32_t dst_h;
    rstd::uint32_t _pad0[2];
    float          m_r[4];
    float          m_g[4];
    float          m_b[4];
    float          offset[4];
};
static_assert(sizeof(ShaderPushConstants) == 80, "PC size mismatch with shader");

ColorMatrix make_color_matrix(ColorSpace cs, ColorRange cr) {
    /* Coefficients are the standard ITU-R rec luma/chroma weights, with
     * the limited-range Y/C scaling baked into the matrix so the shader
     * only needs to subtract the offset and matmul. Reference:
     *   BT.709: Kr=0.2126, Kb=0.0722
     *   BT.601: Kr=0.299,  Kb=0.114
     *   BT.2020 (NCL): Kr=0.2627, Kb=0.0593 (treated identically here —
     *     no PQ / HLG support yet, so non-constant-luma BT.2020 is
     *     close enough for SDR fallback).
     */
    ColorMatrix m {};
    if (cr == ColorRange::Full) {
        /* Full range: y_scale = 1.0, c_scale = 1.0; offsets = (0, -.5, -.5). */
        if (cs == ColorSpace::Bt601) {
            m.m_r[0] = 1.0f;
            m.m_r[1] = 0.0f;
            m.m_r[2] = 1.402f;
            m.m_g[0] = 1.0f;
            m.m_g[1] = -0.34414f;
            m.m_g[2] = -0.71414f;
            m.m_b[0] = 1.0f;
            m.m_b[1] = 1.772f;
            m.m_b[2] = 0.0f;
        } else if (cs == ColorSpace::Bt2020) {
            m.m_r[0] = 1.0f;
            m.m_r[1] = 0.0f;
            m.m_r[2] = 1.4746f;
            m.m_g[0] = 1.0f;
            m.m_g[1] = -0.16455f;
            m.m_g[2] = -0.57135f;
            m.m_b[0] = 1.0f;
            m.m_b[1] = 1.8814f;
            m.m_b[2] = 0.0f;
        } else {
            /* BT.709 default. */
            m.m_r[0] = 1.0f;
            m.m_r[1] = 0.0f;
            m.m_r[2] = 1.5748f;
            m.m_g[0] = 1.0f;
            m.m_g[1] = -0.18732f;
            m.m_g[2] = -0.46812f;
            m.m_b[0] = 1.0f;
            m.m_b[1] = 1.85563f;
            m.m_b[2] = 0.0f;
        }
        m.offset[0] = 0.0f;
        m.offset[1] = -128.0f / 255.0f;
        m.offset[2] = -128.0f / 255.0f;
    } else {
        /* Limited range. y_scale = 255/219; c_scale = 255/224.
         * Pre-bake into matrix coefficients. */
        constexpr float ys  = 255.0f / 219.0f;
        constexpr float cs_ = 255.0f / 224.0f;
        if (cs == ColorSpace::Bt601) {
            m.m_r[0] = ys;
            m.m_r[1] = 0.0f;
            m.m_r[2] = 1.402f * cs_;
            m.m_g[0] = ys;
            m.m_g[1] = -0.34414f * cs_;
            m.m_g[2] = -0.71414f * cs_;
            m.m_b[0] = ys;
            m.m_b[1] = 1.772f * cs_;
            m.m_b[2] = 0.0f;
        } else if (cs == ColorSpace::Bt2020) {
            m.m_r[0] = ys;
            m.m_r[1] = 0.0f;
            m.m_r[2] = 1.4746f * cs_;
            m.m_g[0] = ys;
            m.m_g[1] = -0.16455f * cs_;
            m.m_g[2] = -0.57135f * cs_;
            m.m_b[0] = ys;
            m.m_b[1] = 1.8814f * cs_;
            m.m_b[2] = 0.0f;
        } else {
            m.m_r[0] = ys;
            m.m_r[1] = 0.0f;
            m.m_r[2] = 1.5748f * cs_;
            m.m_g[0] = ys;
            m.m_g[1] = -0.18732f * cs_;
            m.m_g[2] = -0.46812f * cs_;
            m.m_b[0] = ys;
            m.m_b[1] = 1.85563f * cs_;
            m.m_b[2] = 0.0f;
        }
        m.offset[0] = -16.0f / 255.0f;
        m.offset[1] = -128.0f / 255.0f;
        m.offset[2] = -128.0f / 255.0f;
    }
    return m;
}

namespace
{

bool fail(Error* err, ref<str> message) {
    if (err) err->message = String::make(message);
    return false;
}

bool fail(Error* err, String message) {
    if (err) err->message = rstd::move(message);
    return false;
}

const char* vk_result_str(VkResult r) {
    switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    default: return "VK_ERROR_?";
    }
}

auto vk_error(ref<str> operation, VkResult result) -> String {
    return rstd::format("{}: {}", operation, vk_result_str(result));
}

rstd::uint32_t pick_memory_type(const vvk::PhysicalDevice& phys, rstd::uint32_t mask,
                                VkMemoryPropertyFlags want) {
    const auto mp = phys.GetMemoryProperties().memoryProperties;
    for (rstd::uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((mask & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool create_image_2d(const vvk::Device& device, const vvk::PhysicalDevice& phys, VkFormat fmt,
                     rstd::uint32_t w, rstd::uint32_t h, VkImageUsageFlags usage,
                     vvk::Image& out_img, vvk::DeviceMemory& out_mem, Error* err) {
    VkImageCreateInfo ici {};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = fmt;
    ici.extent        = { w, h, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = usage;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (VkResult r = device.CreateImage(ici, out_img); r != VK_SUCCESS) {
        fail(err, vk_error("vkCreateImage"_str, r));
        return false;
    }
    const auto     mr = device.GetImageMemoryRequirements(*out_img);
    rstd::uint32_t type =
        pick_memory_type(phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) {
        fail(err, "no DEVICE_LOCAL memory type for plane image"_str);
        return false;
    }
    VkMemoryAllocateInfo mai {};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = type;
    if (VkResult r = device.AllocateMemory(mai, out_mem); r != VK_SUCCESS) {
        fail(err, vk_error("vkAllocateMemory(plane)"_str, r));
        return false;
    }
    if (VkResult r = out_img.BindMemory(*out_mem, 0); r != VK_SUCCESS) {
        fail(err, vk_error("vkBindImageMemory(plane)"_str, r));
        return false;
    }
    return true;
}

bool create_image_view(const vvk::Device& device, VkImage img, VkFormat fmt, vvk::ImageView& out,
                       Error* err) {
    VkImageViewCreateInfo vci {};
    vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image            = img;
    vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vci.format           = fmt;
    vci.components       = { VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY };
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (VkResult r = device.CreateImageView(vci, out); r != VK_SUCCESS) {
        fail(err, vk_error("vkCreateImageView"_str, r));
        return false;
    }
    return true;
}

void barrier_image(const vvk::CommandBuffer& cmd, VkImage img, VkAccessFlags src_a,
                   VkAccessFlags dst_a, VkImageLayout old_l, VkImageLayout new_l,
                   VkPipelineStageFlags src_s, VkPipelineStageFlags dst_s,
                   rstd::uint32_t src_qf = VK_QUEUE_FAMILY_IGNORED,
                   rstd::uint32_t dst_qf = VK_QUEUE_FAMILY_IGNORED) {
    VkImageMemoryBarrier b {};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask       = src_a;
    b.dstAccessMask       = dst_a;
    b.oldLayout           = old_l;
    b.newLayout           = new_l;
    b.srcQueueFamilyIndex = src_qf;
    b.dstQueueFamilyIndex = dst_qf;
    b.image               = img;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    cmd.PipelineBarrier(src_s, dst_s, 0, b);
}

// sync2 variant: only this can name VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR
// for the cross-queue hazard tracker after vkCmdDecodeVideoKHR.
void barrier_image2(const vvk::CommandBuffer& cmd, VkImage img, VkPipelineStageFlags2 src_s,
                    VkAccessFlags2 src_a, VkPipelineStageFlags2 dst_s, VkAccessFlags2 dst_a,
                    VkImageLayout old_l, VkImageLayout new_l,
                    rstd::uint32_t src_qf = VK_QUEUE_FAMILY_IGNORED,
                    rstd::uint32_t dst_qf = VK_QUEUE_FAMILY_IGNORED) {
    VkImageMemoryBarrier2 b {};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask        = src_s;
    b.srcAccessMask       = src_a;
    b.dstStageMask        = dst_s;
    b.dstAccessMask       = dst_a;
    b.oldLayout           = old_l;
    b.newLayout           = new_l;
    b.srcQueueFamilyIndex = src_qf;
    b.dstQueueFamilyIndex = dst_qf;
    b.image               = img;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkDependencyInfo di {};
    di.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers    = &b;
    cmd.PipelineBarrier2(di);
}

bool target_exports_sync_fd(ConvertTarget target) { return target == ConvertTarget::BridgeForeign; }

VkResult submit_conversion(const vvk::Queue& queue, const VkSubmitInfo& conversion,
                           ConvertTarget target, VkSemaphore completion_timeline,
                           rstd::uint64_t completion_value, VkSemaphore export_semaphore) {
    if (! target_exports_sync_fd(target)) return queue.Submit(conversion);

    const VkPipelineStageFlags    wait_stage          = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    const rstd::uint64_t          binary_signal_value = 0;
    VkTimelineSemaphoreSubmitInfo handoff_timeline {};
    handoff_timeline.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    handoff_timeline.waitSemaphoreValueCount   = 1;
    handoff_timeline.pWaitSemaphoreValues      = &completion_value;
    handoff_timeline.signalSemaphoreValueCount = 1;
    handoff_timeline.pSignalSemaphoreValues    = &binary_signal_value;

    VkSubmitInfo handoff {};
    handoff.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    handoff.pNext                = &handoff_timeline;
    handoff.waitSemaphoreCount   = 1;
    handoff.pWaitSemaphores      = &completion_timeline;
    handoff.pWaitDstStageMask    = &wait_stage;
    handoff.signalSemaphoreCount = 1;
    handoff.pSignalSemaphores    = &export_semaphore;

    VkSubmitInfo submissions[2] = { conversion, handoff };
    return queue.Submit(slice<VkSubmitInfo>::from_raw_parts(submissions, usize(2)));
}

u64 next_completion_generation() {
    static Atomic<u64> next { u64(1) };
    auto               value = next.fetch_add(u64(1), Ordering::Relaxed);
    if (value == u64()) value = next.fetch_add(u64(1), Ordering::Relaxed);
    return value;
}

u64 next_content_revision() {
    static Atomic<u64> next { u64(1) };
    auto               value = next.fetch_add(u64(1), Ordering::Relaxed);
    if (value == u64()) value = next.fetch_add(u64(1), Ordering::Relaxed);
    return value;
}

void barrier_dst_to_storage(const vvk::CommandBuffer& cmd, VkImage dst, ConvertTarget target) {
    if (target == ConvertTarget::SampledLocal) {
        barrier_image(cmd,
                      dst,
                      VK_ACCESS_SHADER_READ_BIT,
                      VK_ACCESS_SHADER_WRITE_BIT,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_GENERAL,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        return;
    }
    barrier_image(cmd,
                  dst,
                  0,
                  VK_ACCESS_SHADER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_GENERAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void barrier_dst_from_storage(const vvk::CommandBuffer& cmd, VkImage dst, ConvertTarget target,
                              rstd::uint32_t queue_family) {
    if (target == ConvertTarget::SampledLocal) {
        barrier_image(cmd,
                      dst,
                      VK_ACCESS_SHADER_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT,
                      VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        return;
    }
    barrier_image(cmd,
                  dst,
                  VK_ACCESS_SHADER_WRITE_BIT,
                  0,
                  VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_GENERAL,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                  queue_family,
                  VK_QUEUE_FAMILY_FOREIGN_EXT);
}

} // namespace

namespace
{

constexpr u32 kMaxConversionContexts { 16 };
constexpr u32 kMaxDrmImports { 64 };

DrmFrameView drm_signature(const DrmFrameView& view) {
    auto signature = view;
    for (u32 i = u32(); i < signature.object_count; ++i) {
        signature.objects[i.to_primitive()].fd = -1;
    }
    signature.pts_seconds = f64(-1.0);
    return signature;
}

bool same_drm_signature(const DrmFrameView& lhs, const DrmFrameView& rhs) {
    if (lhs.object_count != rhs.object_count || lhs.layer_count != rhs.layer_count ||
        lhs.width != rhs.width || lhs.height != rhs.height || lhs.bit_depth != rhs.bit_depth) {
        return false;
    }
    for (u32 i = u32(); i < lhs.object_count; ++i) {
        const auto index = i.to_primitive();
        if (lhs.objects[index].size != rhs.objects[index].size ||
            lhs.objects[index].format_modifier != rhs.objects[index].format_modifier) {
            return false;
        }
    }
    for (u32 li = u32(); li < lhs.layer_count; ++li) {
        const auto  layer_index = li.to_primitive();
        const auto& a           = lhs.layers[layer_index];
        const auto& b           = rhs.layers[layer_index];
        if (a.fourcc != b.fourcc || a.plane_count != b.plane_count) return false;
        for (u32 pi = u32(); pi < a.plane_count; ++pi) {
            const auto  plane_index = pi.to_primitive();
            const auto& ap          = a.planes[plane_index];
            const auto& bp          = b.planes[plane_index];
            if (ap.object_index != bp.object_index || ap.offset != bp.offset ||
                ap.pitch != bp.pitch) {
                return false;
            }
        }
    }
    return true;
}

struct DrmImportEntry {
    DrmResourceKey         key;
    DrmFrameView           signature;
    Vec<vvk::DeviceMemory> memories;
    vvk::Image             image;
    vvk::Image             uv_image; // When split_planes, UV is a separate R8G8 image.
    vvk::ImageView         y_view;
    vvk::ImageView         uv_view;
    u32                    in_flight {};
    u64                    last_use_serial {};
    bool                   initialized { false };
    bool                   split_planes { false }; // Disjoint DMA-BUFs imported as two images.
};

struct ConversionTargetEntry {
    VkImage                      image { VK_NULL_HANDLE };
    VkImageView                  view { VK_NULL_HANDLE };
    u32                          width {};
    u32                          height {};
    ConvertTarget                kind { ConvertTarget::BridgeForeign };
    vvk::ImageView               owned_view;
    Option<vvk::SubmissionToken> last_completion;
    bool                         reserved { false };
    bool                         initialized { false };
};

struct ConversionSubmissionContext {
    vvk::CommandBuffers          command_buffers;
    vvk::CommandBuffer           command;
    vvk::DescriptorSetLease      descriptor_set;
    vvk::Semaphore               export_semaphore;
    Option<vvk::SubmissionToken> completion;
    vvk::ImageView               source_y_view;
    vvk::ImageView               source_uv_view;
    Option<VkFrameLease>         vk_frame;
    Option<DrmFrameLease>        frame;
    DrmImportEntry*              source { nullptr };
    ConversionTargetEntry*       target { nullptr };
    bool                         reserved { false };
};

auto find_or_create_target(const vvk::Device& device, Vec<Box<ConversionTargetEntry>>& targets,
                           const ConversionTargetView& destination)
    -> Result<ConversionTargetEntry*, Error> {
    for (auto& candidate : targets) {
        if (candidate->image != destination.image || candidate->width != destination.width ||
            candidate->height != destination.height || candidate->kind != destination.kind ||
            (destination.view != VK_NULL_HANDLE && candidate->view != destination.view)) {
            continue;
        }
        return Ok(candidate.get());
    }

    auto entry    = Box<ConversionTargetEntry>::make();
    entry->image  = destination.image;
    entry->width  = destination.width;
    entry->height = destination.height;
    entry->view   = destination.view;
    entry->kind   = destination.kind;
    if (entry->view == VK_NULL_HANDLE) {
        VkImageViewCreateInfo view_info {};
        view_info.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image      = destination.image;
        view_info.viewType   = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format     = VK_FORMAT_R8G8B8A8_UNORM;
        view_info.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
        };
        view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (const auto result = device.CreateImageView(view_info, entry->owned_view);
            result != VK_SUCCESS) {
            return Err(Error { vk_error("vkCreateImageView(conversion target)"_str, result) });
        }
        entry->view = *entry->owned_view;
    }
    auto* result = entry.get();
    targets.push(rstd::move(entry));
    return Ok(result);
}

} // namespace

struct YuvToRgba::ConversionPipelineState {
    Vec<Box<DrmImportEntry>>              imports;
    Vec<Box<ConversionTargetEntry>>       targets;
    Vec<Box<ConversionSubmissionContext>> contexts;
    u32                                   max_contexts { 3 };
    u32                                   max_imports { 32 };
    u64                                   use_serial {};
};

struct ConversionReservation::State {
    YuvToRgba*                   owner { nullptr };
    ConversionSubmissionContext* context { nullptr };
    ConversionTargetEntry*       target { nullptr };
    ConversionTargetView         destination;
    bool                         submitted { false };
};

void ConversionReservation::reset() noexcept {
    if (! state_) return;
    auto state = Box<State>::from_raw(mut_ptr<State>::from_raw_parts(state_));
    if (! state->submitted) {
        if (state->context) state->context->reserved = false;
        if (state->target) state->target->reserved = false;
    }
    state_ = nullptr;
}

ConversionReservation::ConversionReservation(ConversionReservation&& other) noexcept
    : state_(rstd::exchange(other.state_, nullptr)) {}

ConversionReservation& ConversionReservation::operator=(ConversionReservation&& other) noexcept {
    if (this == &other) return *this;
    reset();
    state_ = rstd::exchange(other.state_, nullptr);
    return *this;
}

ConversionReservation::~ConversionReservation() { reset(); }

namespace
{

/* Import one DMA-BUF object as a single-plane image.
 * Used when the Y and UV planes are backed by separate objects. */
auto import_single_drm_plane(const vvk::Device& device, const vvk::PhysicalDevice& phys,
                             const DrmObject& object, rstd::uint64_t offset, rstd::uint64_t pitch,
                             VkFormat format, rstd::uint32_t width, rstd::uint32_t height,
                             vvk::Image& image, vvk::ImageView& view, vvk::DeviceMemory& memory)
    -> Result<empty, Error> {
    if (width == 0 || height == 0) {
        return Err(Error { "import_single_drm_plane: zero extent"_str });
    }

    VkSubresourceLayout plane_layout {};
    plane_layout.offset   = offset;
    plane_layout.rowPitch = pitch;

    VkImageDrmFormatModifierExplicitCreateInfoEXT modifier_info {};
    modifier_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
    modifier_info.drmFormatModifier           = object.format_modifier;
    modifier_info.drmFormatModifierPlaneCount = 1;
    modifier_info.pPlaneLayouts               = &plane_layout;

    VkExternalMemoryImageCreateInfo external_info {};
    external_info.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    external_info.pNext       = &modifier_info;

    VkImageCreateInfo image_info {};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext         = &external_info;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.format        = format;
    image_info.extent        = { width, height, 1 };
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling        = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    image_info.usage         = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (const auto result = device.CreateImage(image_info, image); result != VK_SUCCESS) {
        return Err(Error {
            rstd::format("vkCreateImage(DRM plane): {}", vk_result_str(result)),
        });
    }

    auto duplicate = rstd::os::fd::BorrowedFd::borrow_raw(object.fd).try_clone_to_owned();
    if (duplicate.is_err()) {
        return Err(Error {
            rstd::format("dup(dma_buf): {}", rstd::move(duplicate).unwrap_err_unchecked()) });
    }
    auto       duplicate_fd = rstd::move(duplicate).unwrap_unchecked();
    const auto raw_fd       = duplicate_fd.as_raw_fd();

    VkMemoryFdPropertiesKHR fd_properties {};
    fd_properties.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
    if (const auto result = device.GetMemoryFdPropertiesKHR(
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, raw_fd, fd_properties);
        result != VK_SUCCESS) {
        return Err(Error { vk_error("vkGetMemoryFdPropertiesKHR"_str, result) });
    }

    const auto requirements = device.GetImageMemoryRequirements(*image);
    const auto type_bits    = requirements.memoryTypeBits & fd_properties.memoryTypeBits;
    const auto memory_type  = pick_memory_type(phys, type_bits, 0);
    if (memory_type == UINT32_MAX) {
        return Err(Error {
            "import_single_drm_plane: no compatible memory type"_str,
        });
    }

    VkImportMemoryFdInfoKHR import_info {};
    import_info.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    import_info.fd         = raw_fd;
    VkMemoryDedicatedAllocateInfo dedicated_info {};
    dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_info.image = *image;
    import_info.pNext    = &dedicated_info;
    VkMemoryAllocateInfo allocate_info {};
    allocate_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext           = &import_info;
    allocate_info.allocationSize  = object.size.to_primitive();
    allocate_info.memoryTypeIndex = memory_type;
    if (const auto result = device.AllocateMemory(allocate_info, memory); result != VK_SUCCESS) {
        return Err(Error { vk_error("vkAllocateMemory(import DRM plane)"_str, result) });
    }
    (void)rstd::move(duplicate_fd).into_raw_fd();
    if (const auto result = image.BindMemory(*memory, 0); result != VK_SUCCESS) {
        return Err(Error { vk_error("vkBindImageMemory(DRM plane)"_str, result) });
    }

    VkImageViewCreateInfo view_info {};
    view_info.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image      = *image;
    view_info.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format     = format;
    view_info.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
    };
    view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (const auto result = device.CreateImageView(view_info, view); result != VK_SUCCESS) {
        return Err(Error { vk_error("vkCreateImageView(DRM plane)"_str, result) });
    }
    return Ok(empty {});
}

auto create_drm_import(const vvk::Device& device, const vvk::PhysicalDevice& phys,
                       const DrmFrameLease& frame) -> Result<Box<DrmImportEntry>, Error> {
    const auto& drm = frame.view();
    if (drm.object_count == u32() || drm.layer_count == u32()) {
        return Err(Error { "submit_drm_prime: empty DRM_PRIME descriptor"_str });
    }

    struct FlatPlane {
        rstd::uint32_t object_index {};
        rstd::uint64_t offset {};
        rstd::uint64_t pitch {};
    };
    FlatPlane planes[2] {};
    u32       plane_count {};
    for (u32 layer_index {}; layer_index < drm.layer_count && plane_count < u32(2); ++layer_index) {
        const auto& layer = drm.layers[layer_index.to_primitive()];
        for (u32 index {}; index < layer.plane_count && plane_count < u32(2); ++index) {
            const auto& plane = layer.planes[index.to_primitive()];
            if (plane.object_index >= drm.object_count) {
                return Err(Error { "submit_drm_prime: plane object index is out of range"_str });
            }
            planes[plane_count.to_primitive()] = {
                .object_index = plane.object_index.to_primitive(),
                .offset       = plane.offset.to_primitive(),
                .pitch        = plane.pitch.to_primitive(),
            };
            ++plane_count;
        }
    }
    if (plane_count != u32(2)) {
        return Err(Error { "submit_drm_prime: expected two NV12 planes"_str });
    }

    auto entry          = Box<DrmImportEntry>::make();
    entry->key          = frame.resource_key();
    entry->signature    = drm_signature(drm);
    const bool disjoint = planes[0].object_index != planes[1].object_index;

    /* Y and UV backed by separate DMA-BUF objects: import each as a
     * single-plane image. Shared-object exports keep the path below. */
    if (disjoint) {
        entry->split_planes = true;
        vvk::DeviceMemory y_memory;
        vvk::DeviceMemory uv_memory;
        const auto        full_w   = drm.width.to_primitive();
        const auto        full_h   = drm.height.to_primitive();
        const auto        chroma_w = (full_w + 1) / 2;
        const auto        chroma_h = (full_h + 1) / 2;
        auto              y_import = import_single_drm_plane(device,
                                                             phys,
                                                             drm.objects[planes[0].object_index],
                                                             planes[0].offset,
                                                             planes[0].pitch,
                                                             VK_FORMAT_R8_UNORM,
                                                             full_w,
                                                             full_h,
                                                             entry->image,
                                                             entry->y_view,
                                                             y_memory);
        if (y_import.is_err()) return Err(rstd::move(y_import).unwrap_err());
        auto uv_import = import_single_drm_plane(device,
                                                 phys,
                                                 drm.objects[planes[1].object_index],
                                                 planes[1].offset,
                                                 planes[1].pitch,
                                                 VK_FORMAT_R8G8_UNORM,
                                                 chroma_w,
                                                 chroma_h,
                                                 entry->uv_image,
                                                 entry->uv_view,
                                                 uv_memory);
        if (uv_import.is_err()) return Err(rstd::move(uv_import).unwrap_err());
        entry->memories.push(rstd::move(y_memory));
        entry->memories.push(rstd::move(uv_memory));
        return Ok(rstd::move(entry));
    }

    VkSubresourceLayout plane_layouts[2] {};
    plane_layouts[0].offset   = planes[0].offset;
    plane_layouts[0].rowPitch = planes[0].pitch;
    plane_layouts[1].offset   = planes[1].offset;
    plane_layouts[1].rowPitch = planes[1].pitch;

    VkImageDrmFormatModifierExplicitCreateInfoEXT modifier_info {};
    modifier_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
    modifier_info.drmFormatModifier           = drm.objects[0].format_modifier;
    modifier_info.drmFormatModifierPlaneCount = 2;
    modifier_info.pPlaneLayouts               = plane_layouts;

    VkExternalMemoryImageCreateInfo external_info {};
    external_info.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    external_info.pNext       = &modifier_info;

    VkFormat view_formats[] = {
        VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
        VK_FORMAT_R8_UNORM,
        VK_FORMAT_R8G8_UNORM,
    };
    VkImageFormatListCreateInfo format_info {};
    format_info.sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
    format_info.pNext           = &external_info;
    format_info.viewFormatCount = 3;
    format_info.pViewFormats    = view_formats;

    VkImageCreateInfo image_info {};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext         = &format_info;
    image_info.flags         = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.format        = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    image_info.extent        = { drm.width.to_primitive(), drm.height.to_primitive(), 1 };
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling        = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    image_info.usage         = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (const auto result = device.CreateImage(image_info, entry->image); result != VK_SUCCESS) {
        return Err(Error {
            rstd::format("vkCreateImage(DRM_PRIME): {}", vk_result_str(result)),
        });
    }

    vvk::DeviceMemory plane_memories[2];
    auto              import_plane = [&](u32                   plane_index,
                                         rstd::uint32_t        object_index,
                                         VkImageAspectFlagBits aspect) -> Result<empty, Error> {
        const auto& object = drm.objects[object_index];
        auto duplicate     = rstd::os::fd::BorrowedFd::borrow_raw(object.fd).try_clone_to_owned();
        if (duplicate.is_err()) {
            return Err(Error {
                rstd::format("dup(dma_buf): {}", rstd::move(duplicate).unwrap_err_unchecked()) });
        }
        auto       duplicate_fd = rstd::move(duplicate).unwrap_unchecked();
        const auto raw_fd       = duplicate_fd.as_raw_fd();

        VkMemoryFdPropertiesKHR fd_properties {};
        fd_properties.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
        if (const auto result = device.GetMemoryFdPropertiesKHR(
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, raw_fd, fd_properties);
            result != VK_SUCCESS) {
            return Err(Error { vk_error("vkGetMemoryFdPropertiesKHR"_str, result) });
        }

        VkImageMemoryRequirementsInfo2 requirements_info {};
        requirements_info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
        requirements_info.image = *entry->image;
        const auto requirements = device.GetImageMemoryRequirements2(requirements_info);

        const auto type_bits =
            requirements.memoryRequirements.memoryTypeBits & fd_properties.memoryTypeBits;
        const auto memory_type = pick_memory_type(phys, type_bits, 0);
        if (memory_type == UINT32_MAX) {
            return Err(Error {
                "submit_drm_prime: no compatible memory type for imported DMA-BUF"_str,
            });
        }

        VkImportMemoryFdInfoKHR import_info {};
        import_info.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
        import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        import_info.fd         = raw_fd;
        VkMemoryDedicatedAllocateInfo dedicated_info {};
        dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated_info.image = *entry->image;
        import_info.pNext    = &dedicated_info;
        VkMemoryAllocateInfo allocate_info {};
        allocate_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate_info.pNext           = &import_info;
        allocate_info.allocationSize  = object.size.to_primitive();
        allocate_info.memoryTypeIndex = memory_type;
        if (const auto result =
                device.AllocateMemory(allocate_info, plane_memories[plane_index.to_primitive()]);
            result != VK_SUCCESS) {
            return Err(Error { vk_error("vkAllocateMemory(import DMA-BUF)"_str, result) });
        }
        (void)rstd::move(duplicate_fd).into_raw_fd();
        return Ok(empty {});
    };

    auto imported = import_plane(u32(), planes[0].object_index, VK_IMAGE_ASPECT_COLOR_BIT);
    if (imported.is_err()) return Err(rstd::move(imported).unwrap_err());
    if (const auto result = entry->image.BindMemory(*plane_memories[0], 0); result != VK_SUCCESS) {
        return Err(Error { vk_error("vkBindImageMemory(shared)"_str, result) });
    }
    entry->memories.push(rstd::move(plane_memories[0]));

    VkImageViewCreateInfo view_info {};
    view_info.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    view_info.image      = *entry->image;
    view_info.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
    };
    view_info.subresourceRange = { VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 1, 0, 1 };
    view_info.format           = VK_FORMAT_R8_UNORM;
    if (const auto result = device.CreateImageView(view_info, entry->y_view);
        result != VK_SUCCESS) {
        return Err(Error { vk_error("vkCreateImageView(DRM Y)"_str, result) });
    }
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
    view_info.format                      = VK_FORMAT_R8G8_UNORM;
    if (const auto result = device.CreateImageView(view_info, entry->uv_view);
        result != VK_SUCCESS) {
        return Err(Error { vk_error("vkCreateImageView(DRM UV)"_str, result) });
    }
    return Ok(rstd::move(entry));
}

} // namespace

YuvToRgba::~YuvToRgba() {
    if (device_) (void)device_.WaitIdle();
    if (pipeline_state_) {
        auto state = Box<ConversionPipelineState>::from_raw(
            mut_ptr<ConversionPipelineState>::from_raw_parts(pipeline_state_));
        pipeline_state_ = nullptr;
    }
    if (staging_map_ && staging_mem_) staging_mem_.Unmap();
    (void)completion_timeline_.take();
}

auto YuvToRgba::create(const vvk::InstanceDispatch& instance_dispatch, VkPhysicalDevice phys,
                       const vvk::DeviceDispatch& device_dispatch, u32 queue_family, VkQueue queue,
                       u32 max_w, u32 max_h) -> Result<Box<YuvToRgba>, Error> {
    if (max_w == u32() || max_h == u32()) {
        return Err(Error { "YuvToRgba: max_w/max_h must be non-zero"_str });
    }
    // NV12 chroma is 4:2:0, so plane W/H must be even.
    if (max_w % u32(2) != u32()) ++max_w;
    if (max_h % u32(2) != u32()) ++max_h;
    auto  self = Box<YuvToRgba>::make();
    Error err;
    if (! self->init(
            instance_dispatch, phys, device_dispatch, queue_family, queue, max_w, max_h, &err)) {
        return Err(rstd::move(err));
    }
    return Ok(rstd::move(self));
}

bool YuvToRgba::init(const vvk::InstanceDispatch& instance_dispatch, VkPhysicalDevice phys,
                     const vvk::DeviceDispatch& device_dispatch, u32 queue_family, VkQueue queue,
                     u32 max_w, u32 max_h, Error* err) {
    if (! instance_dispatch.instance || ! device_dispatch.device ||
        device_dispatch.instance != instance_dispatch.instance)
        return fail(err, "YuvToRgba: incompatible dispatch tables"_str);
    instance_dispatch_  = instance_dispatch;
    device_dispatch_    = device_dispatch;
    const auto instance = instance_dispatch.instance;
    const auto device   = device_dispatch.device;

    instance_                   = vvk::Instance(instance, instance_dispatch_, vvk::borrowed_handle);
    phys_                       = vvk::PhysicalDevice(phys, instance_dispatch_);
    device_                     = vvk::Device(device, device_dispatch_, vvk::borrowed_handle);
    queue_                      = vvk::Queue(queue, device_dispatch_);
    queue_family_               = queue_family;
    max_w_                      = max_w;
    max_h_                      = max_h;
    const auto queue_family_raw = queue_family.to_primitive();
    const auto max_w_raw        = max_w.to_primitive();
    const auto max_h_raw        = max_h.to_primitive();

    if (! device_dispatch_.vkGetSemaphoreFdKHR) {
#if defined(__APPLE__)
        // MoltenVK does not provide SYNC_FD export. The standalone viewer
        // uses SampledLocal, so the fd-export path remains unused.
        rstd::log::warn("YuvToRgba: vkGetSemaphoreFdKHR unavailable (macOS); "
                        "BridgeForeign fd-export will be unavailable");
#else
        return fail(err, "vkGetSemaphoreFdKHR missing"_str);
#endif
    }

    // ----- Sampler (linear, clamp-to-edge) -----
    {
        VkSamplerCreateInfo sci {};
        sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sci.magFilter    = VK_FILTER_LINEAR;
        sci.minFilter    = VK_FILTER_LINEAR;
        sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod       = 0.0f;
        if (VkResult r = device_.CreateSampler(sci, sampler_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateSampler"_str, r));
    }

    // ----- Y image (R8_UNORM, max_w × max_h) -----
    if (! create_image_2d(device_,
                          phys_,
                          VK_FORMAT_R8_UNORM,
                          max_w_raw,
                          max_h_raw,
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          y_image_,
                          y_memory_,
                          err))
        return false;
    if (! create_image_view(device_, *y_image_, VK_FORMAT_R8_UNORM, y_view_, err)) return false;

    // ----- UV image (R8G8_UNORM, half resolution) -----
    if (! create_image_2d(device_,
                          phys_,
                          VK_FORMAT_R8G8_UNORM,
                          max_w_raw / 2,
                          max_h_raw / 2,
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          uv_image_,
                          uv_memory_,
                          err))
        return false;
    if (! create_image_view(device_, *uv_image_, VK_FORMAT_R8G8_UNORM, uv_view_, err)) return false;

    // ----- Staging buffer (HOST_VISIBLE|COHERENT, NV12-sized) -----
    {
        const VkDeviceSize nv12_size = VkDeviceSize(max_w_raw) * max_h_raw * 3 / 2;
        staging_size_                = nv12_size;
        VkBufferCreateInfo bci {};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = nv12_size;
        bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (VkResult r = device_.CreateBuffer(bci, staging_buf_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateBuffer(stage)"_str, r));
        const auto     mr   = device_.GetBufferMemoryRequirements(*staging_buf_);
        rstd::uint32_t type = pick_memory_type(phys_,
                                               mr.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (type == UINT32_MAX)
            return fail(err, "no HOST_VISIBLE|COHERENT memory type for staging"_str);
        VkMemoryAllocateInfo mai {};
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize  = mr.size;
        mai.memoryTypeIndex = type;
        if (VkResult r = device_.AllocateMemory(mai, staging_mem_); r != VK_SUCCESS)
            return fail(err, vk_error("vkAllocateMemory(stage)"_str, r));
        if (VkResult r = staging_buf_.BindMemory(*staging_mem_, 0); r != VK_SUCCESS)
            return fail(err, vk_error("vkBindBufferMemory(stage)"_str, r));
        if (VkResult r = staging_mem_.Map(0, VK_WHOLE_SIZE, &staging_map_); r != VK_SUCCESS)
            return fail(err, vk_error("vkMapMemory(stage)"_str, r));
    }

    // ----- Shader module -----
    {
        VkShaderModuleCreateInfo smi {};
        smi.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smi.codeSize = sizeof(nv12_to_rgba_spv);
        smi.pCode    = reinterpret_cast<const rstd::uint32_t*>(nv12_to_rgba_spv);
        if (VkResult r = device_.CreateShaderModule(smi, shader_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateShaderModule"_str, r));
    }

    // ----- Descriptor set layout (binding 0/1 = sampled, binding 2 = storage)
    // -----
    {
        VkDescriptorSetLayoutBinding bs[3] {};
        bs[0].binding         = 0;
        bs[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bs[0].descriptorCount = 1;
        bs[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bs[1].binding         = 1;
        bs[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bs[1].descriptorCount = 1;
        bs[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bs[2].binding         = 2;
        bs[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bs[2].descriptorCount = 1;
        bs[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo dsli {};
        dsli.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsli.bindingCount = 3;
        dsli.pBindings    = bs;
        if (VkResult r = device_.CreateDescriptorSetLayout(dsli, dsl_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateDescriptorSetLayout"_str, r));
    }

    // ----- Pipeline layout (push constants: dst dims + color matrix) -----
    {
        VkPushConstantRange pcr {};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset     = 0;
        pcr.size       = sizeof(ShaderPushConstants);
        VkPipelineLayoutCreateInfo pli {};
        pli.sType                        = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount               = 1;
        const auto descriptor_set_layout = *dsl_;
        pli.pSetLayouts                  = &descriptor_set_layout;
        pli.pushConstantRangeCount       = 1;
        pli.pPushConstantRanges          = &pcr;
        if (VkResult r = device_.CreatePipelineLayout(pli, pipeline_layout_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreatePipelineLayout"_str, r));
    }

    // ----- Compute pipeline -----
    {
        VkPipelineShaderStageCreateInfo ssi {};
        ssi.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ssi.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        ssi.module = *shader_;
        ssi.pName  = "main";
        VkComputePipelineCreateInfo cpi {};
        cpi.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpi.stage  = ssi;
        cpi.layout = *pipeline_layout_;
        if (VkResult r = device_.CreateComputePipeline(cpi, pipeline_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateComputePipelines"_str, r));
    }

    // ----- Descriptor pool + set -----
    {
        VkDescriptorPoolSize ps[2] {};
        ps[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps[0].descriptorCount = 2 * (kMaxConversionContexts.to_primitive() + 1);
        ps[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        ps[1].descriptorCount = kMaxConversionContexts.to_primitive() + 1;
        const vvk::DescriptorDeviceDispatch dispatch {
            .create_pool   = device_dispatch_.vkCreateDescriptorPool,
            .destroy_pool  = device_dispatch_.vkDestroyDescriptorPool,
            .allocate_sets = device_dispatch_.vkAllocateDescriptorSets,
            .update_sets   = device_dispatch_.vkUpdateDescriptorSets,
        };
        auto arena = vvk::DescriptorArenaGeneration::Create(
            *device_,
            kMaxConversionContexts.to_primitive() + 1,
            slice<VkDescriptorPoolSize>::from_raw_parts(ps, usize(2)),
            dispatch);
        if (! arena.created())
            return fail(err, vk_error("vkCreateDescriptorPool"_str, arena.api_result));
        descriptor_arena_ = rstd::move(arena.arena);
        auto allocation   = vvk::DescriptorArenaGeneration::Allocate(*descriptor_arena_, *dsl_);
        if (! allocation.allocated())
            return fail(err, vk_error("vkAllocateDescriptorSets"_str, allocation.api_result));
        software_descriptor_set_ = rstd::move(allocation.lease);
    }

    // ----- Cmd pool + buffer + per-submit fence + signal semaphore -----
    {
        VkCommandPoolCreateInfo cpi {};
        cpi.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpi.queueFamilyIndex = queue_family_raw;
        if (VkResult r = device_.CreateCommandPool(cpi, cmd_pool_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateCommandPool"_str, r));
        if (VkResult r = cmd_pool_.Allocate(
                usize(1), VK_COMMAND_BUFFER_LEVEL_PRIMARY, software_command_buffers_);
            r != VK_SUCCESS)
            return fail(err, vk_error("vkAllocateCommandBuffers"_str, r));
        software_command_ =
            vvk::CommandBuffer(software_command_buffers_[usize()], device_dispatch_);

        VkFenceCreateInfo fci {};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (VkResult r = device_.CreateFence(fci, software_fence_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateFence"_str, r));

        VkSemaphoreCreateInfo sci {};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
#if defined(__APPLE__)
        // MoltenVK rejects the Linux SYNC_FD export type. Keep a plain binary
        // semaphore because Apple only uses the SampledLocal path here.
        sci.pNext = nullptr;
#else
        VkExportSemaphoreCreateInfo es {};
        es.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        es.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        sci.pNext      = &es;
#endif
        if (VkResult r = device_.CreateSemaphore(sci, software_export_semaphore_); r != VK_SUCCESS)
            return fail(err, vk_error("vkCreateSemaphore(signal)"_str, r));
        const auto generation = next_completion_generation();
        auto       timeline   = vvk::TimelineSemaphoreGeneration::Create(
            *device_,
            vvk::MakeQueueDomain(*device_, *queue_, queue_family_, u32(), generation),
            generation,
            vvk::TimelineSemaphoreDeviceDispatch::FromDispatch(device_dispatch_));
        if (! timeline.created()) {
            return fail(err,
                        rstd::format("vkCreateSemaphore(completion): {}",
                                     vk_result_str(timeline.api_result)));
        }
        completion_timeline_ = rstd::move(timeline.generation);
        completion_observer_ = Some(vvk::TimelineCompletionObserver::AdoptVulkan(
            *device_,
            device_dispatch_.vkGetSemaphoreCounterValueKHR,
            device_dispatch_.vkWaitSemaphoresKHR));
        if (completion_timeline_.is_none() || ! (*completion_timeline_)->source().valid() ||
            completion_observer_.is_none() || ! completion_observer_->valid()) {
            return fail(err, "timeline completion observer unavailable"_str);
        }
    }

    auto pipeline   = Box<ConversionPipelineState>::make();
    pipeline_state_ = rstd::move(pipeline).into_raw().as_raw_ptr();

    // Bindings 0/1 are stable across frames — write them once.
    {
        VkDescriptorImageInfo dii_y {};
        dii_y.sampler     = *sampler_;
        dii_y.imageView   = *y_view_;
        dii_y.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo dii_uv {};
        dii_uv.sampler     = *sampler_;
        dii_uv.imageView   = *uv_view_;
        dii_uv.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vvk::DescriptorUpdateBatch batch;
        if (! batch.WriteImage(software_descriptor_set_.clone(),
                               0,
                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_y, usize(1))) ||
            ! batch.WriteImage(software_descriptor_set_.clone(),
                               1,
                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_uv, usize(1))) ||
            ! batch.Commit().committed())
            return fail(err, "failed to update static YUV descriptors"_str);
    }

    return true;
}

int YuvToRgba::convert_nv12_(VkImage dst, rstd::uint32_t dst_w, rstd::uint32_t dst_h,
                             const rstd::uint8_t* nv12, usize nv12_size, const ColorMatrix& cm,
                             ConvertTarget target, Error* err) {
    if (dst == VK_NULL_HANDLE) {
        fail(err, "convert_nv12: dst VkImage null"_str);
        return -1;
    }
    if (dst_w == 0 || dst_h == 0) {
        fail(err, "convert_nv12: dst_w/h zero"_str);
        return -1;
    }
    if ((dst_w & 1u) || (dst_h & 1u)) {
        fail(err, "convert_nv12: dst dims must be even (NV12 chroma)"_str);
        return -1;
    }
    if (dst_w > max_w_.to_primitive() || dst_h > max_h_.to_primitive()) {
        fail(err, "convert_nv12: dst exceeds configured max extent"_str);
        return -1;
    }
    const usize want = usize(dst_w) * usize(dst_h) * usize(3) / usize(2);
    if (nv12_size != want) {
        fail(err, "convert_nv12: nv12_size mismatch (expected NV12 layout)"_str);
        return -1;
    }

    /* Software upload reuses one mapped staging allocation and command context. */
    if (software_fence_pending_) {
        if (VkResult r = software_fence_.Wait(1'000'000'000ull); r != VK_SUCCESS) {
            fail(err, vk_error("vkWaitForFences"_str, r));
            return -1;
        }
        if (VkResult r = software_fence_.Reset(); r != VK_SUCCESS) {
            fail(err, vk_error("vkResetFences"_str, r));
            return -1;
        }
        software_fence_pending_ = false;
    }

    /* Copy NV12 bytes into staging. */
    rstd::mem::memcpy(staging_map_, nv12, nv12_size);

    /* Create a transient view for the dst image — fresh each call because
     * the bridge cycles dst handles per slot. The view is destroyed at
     * the next call's fence wait via the deferred queue. */
    vvk::ImageView dst_view;
    {
        VkImageViewCreateInfo vci {};
        vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image            = dst;
        vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vci.format           = VK_FORMAT_R8G8B8A8_UNORM;
        vci.components       = { VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY };
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (VkResult r = device_.CreateImageView(vci, dst_view); r != VK_SUCCESS) {
            fail(err, vk_error("vkCreateImageView(dst)"_str, r));
            return -1;
        }
    }

    /* Bind dst into descriptor binding 2. */
    {
        VkDescriptorImageInfo dii {};
        dii.imageView   = *dst_view;
        dii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        vvk::DescriptorUpdateBatch batch;
        if (! batch.WriteImage(software_descriptor_set_.clone(),
                               2,
                               VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii, usize(1))) ||
            ! batch.Commit().committed()) {
            fail(err, "failed to update destination descriptor"_str);
            return -1;
        }
    }

    /* Reset + record. */
    if (VkResult r = software_command_.Reset(); r != VK_SUCCESS) {
        fail(err, vk_error("vkResetCommandBuffer"_str, r));
        return -1;
    }
    VkCommandBufferBeginInfo bi {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (VkResult r = software_command_.Begin(bi); r != VK_SUCCESS) {
        fail(err, vk_error("vkBeginCommandBuffer"_str, r));
        return -1;
    }

    /* Y plane: UNDEFINED → TRANSFER_DST. */
    barrier_image(software_command_,
                  *y_image_,
                  0,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    /* UV plane: UNDEFINED → TRANSFER_DST. */
    barrier_image(software_command_,
                  *uv_image_,
                  0,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    /* Copy Y from staging[0..W*H]. */
    {
        VkBufferImageCopy bic {};
        bic.bufferOffset                = 0;
        bic.bufferRowLength             = 0;
        bic.bufferImageHeight           = 0;
        bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bic.imageSubresource.layerCount = 1;
        bic.imageOffset                 = { 0, 0, 0 };
        bic.imageExtent                 = { dst_w, dst_h, 1 };
        software_command_.CopyBufferToImage(
            *staging_buf_, *y_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bic);
    }
    /* Copy UV from staging[W*H..W*H + W*H/2]. */
    {
        VkBufferImageCopy bic {};
        bic.bufferOffset                = VkDeviceSize(dst_w) * dst_h;
        bic.bufferRowLength             = 0;
        bic.bufferImageHeight           = 0;
        bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bic.imageSubresource.layerCount = 1;
        bic.imageOffset                 = { 0, 0, 0 };
        bic.imageExtent                 = { dst_w / 2, dst_h / 2, 1 };
        software_command_.CopyBufferToImage(
            *staging_buf_, *uv_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bic);
    }

    /* Y/UV: TRANSFER_DST → SHADER_READ_ONLY. */
    barrier_image(software_command_,
                  *y_image_,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    barrier_image(software_command_,
                  *uv_image_,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    barrier_dst_to_storage(software_command_, dst, target);

    /* Bind + dispatch. */
    software_command_.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline_);
    const VkDescriptorSet descriptor_set = software_descriptor_set_.handle;
    software_command_.BindDescriptorSets(
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *pipeline_layout_,
        0,
        slice<VkDescriptorSet>::from_raw_parts(&descriptor_set, usize(1)),
        {});
    ShaderPushConstants pc {};
    pc.dst_w = dst_w;
    pc.dst_h = dst_h;
    for (int i = 0; i < 3; ++i) {
        pc.m_r[i]    = cm.m_r[i];
        pc.m_g[i]    = cm.m_g[i];
        pc.m_b[i]    = cm.m_b[i];
        pc.offset[i] = cm.offset[i];
    }
    software_command_.PushConstants(*pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, pc);
    const rstd::uint32_t gx = (dst_w + 7) / 8;
    const rstd::uint32_t gy = (dst_h + 7) / 8;
    software_command_.Dispatch(gx, gy, 1);

    barrier_dst_from_storage(software_command_, dst, target, queue_family_.to_primitive());

    if (VkResult r = software_command_.End(); r != VK_SUCCESS) {
        fail(err, vk_error("vkEndCommandBuffer"_str, r));
        return -1;
    }

    auto next_completion_value = completion_value_ + u64(1);
    if (next_completion_value == u64()) ++next_completion_value;
    VkSemaphore signal_sems[2] = { (*completion_timeline_)->handle(), *software_export_semaphore_ };
    rstd::uint64_t                signal_vals[2] = { next_completion_value.to_primitive(), 0 };
    const auto                    signal_count   = target_exports_sync_fd(target) ? 2u : 1u;
    VkTimelineSemaphoreSubmitInfo timeline_info {};
    timeline_info.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_info.signalSemaphoreValueCount = signal_count;
    timeline_info.pSignalSemaphoreValues    = signal_vals;
    VkSubmitInfo si {};
    si.sType                             = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.pNext                             = &timeline_info;
    si.commandBufferCount                = 1;
    const VkCommandBuffer command_buffer = *software_command_;
    si.pCommandBuffers                   = &command_buffer;
    si.signalSemaphoreCount              = signal_count;
    si.pSignalSemaphores                 = signal_sems;
    if (VkResult r = queue_.Submit(si, *software_fence_); r != VK_SUCCESS) {
        fail(err, vk_error("vkQueueSubmit"_str, r));
        return -1;
    }
    software_fence_pending_ = true;
    publish_submission(dst, u32(dst_w), u32(dst_h), target, next_completion_value);
    software_target_view_ = rstd::move(dst_view);

    int sync_fd = -1;
    if (target_exports_sync_fd(target)) {
        VkSemaphoreGetFdInfoKHR sgfi {};
        sgfi.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
        sgfi.semaphore  = *software_export_semaphore_;
        sgfi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        if (VkResult r = device_.GetSemaphoreFdKHR(sgfi, &sync_fd); r != VK_SUCCESS) {
            fail(err, vk_error("vkGetSemaphoreFdKHR"_str, r));
            return -1;
        }
    }
    return sync_fd;
}

auto YuvToRgba::convert_av_vk_frame_(ConversionReservation& reservation, VkFrameAccess& access,
                                     const ColorMatrix& cm) -> Result<int, Error> {
    if (! reservation.valid() || reservation.state_->owner != this ||
        ! reservation.state_->context || ! reservation.state_->target ||
        ! reservation.state_->context->reserved || ! reservation.state_->target->reserved ||
        reservation.state_->context->completion.is_some()) {
        return Err(Error { "convert_av_vk_frame: invalid reservation"_str });
    }
    const auto& frame       = access.view();
    const auto& destination = reservation.state_->destination;
    const auto  dst         = destination.image;
    const auto  dst_w       = destination.width.to_primitive();
    const auto  dst_h       = destination.height.to_primitive();
    const auto  target      = destination.kind;
    if (dst == VK_NULL_HANDLE) {
        return Err(Error { "convert_av_vk_frame: dst null"_str });
    }
    const VkImage     y_image          = frame.img[0];
    const VkImage     uv_image         = frame.plane_count > u32(1) ? frame.img[1] : VK_NULL_HANDLE;
    const VkSemaphore y_sem            = frame.sem[0];
    const VkSemaphore uv_sem           = frame.plane_count > u32(1) ? frame.sem[1] : frame.sem[0];
    auto*             y_sem_val_in_out = &frame.sem_value[0];
    auto*             uv_sem_val_in_out =
        frame.plane_count > u32(1) ? &frame.sem_value[1] : &frame.sem_value[0];
    auto* y_layout_in_out  = &frame.layout[0];
    auto* uv_layout_in_out = frame.plane_count > u32(1) ? &frame.layout[1] : &frame.layout[0];
    auto* y_qf_in_out      = &frame.queue_family[0];
    auto* uv_qf_in_out =
        frame.plane_count > u32(1) ? &frame.queue_family[1] : &frame.queue_family[0];

    if (y_image == VK_NULL_HANDLE) {
        return Err(Error { "convert_av_vk_frame: AVVkFrame y_image NULL"_str });
    }
    /* Two layouts the producer can hand us:
     *   - disjoint:           y_image != uv_image, two separate VkImages
     *                         in R8 / R8G8 (or R16 / R16G16 for 10-bit);
     *   - single multi-plane: uv_image == NULL (or == y_image), one
     *                         VkImage with VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
     *                         backing both planes — what FFmpeg's vulkan
     *                         video decode produces (AV_VK_FRAME_FLAG_
     *                         DISABLE_MULTIPLANE is silently ignored for
     *                         the DPB by the spec).
     * In single-image mode we sample plane 0 / plane 1 via aspect masks. */
    const bool single_image = (uv_image == VK_NULL_HANDLE) || (uv_image == y_image);
    if ((dst_w & 1u) || (dst_h & 1u)) {
        return Err(Error { "convert_av_vk_frame: dst dims must be even"_str });
    }
    if (dst_w > max_w_.to_primitive() || dst_h > max_h_.to_primitive()) {
        return Err(Error { "convert_av_vk_frame: dst exceeds configured max extent"_str });
    }

    auto*      context      = reservation.state_->context;
    auto*      target_entry = reservation.state_->target;
    const auto target_view  = target_entry->view;

    /* Build per-call image views aliasing the AVVkFrame planes + dst. */
    vvk::ImageView y_view;
    vvk::ImageView uv_view;

    {
        VkImageViewUsageCreateInfo sampled_usage {};
        sampled_usage.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
        sampled_usage.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImageViewCreateInfo vci {};
        vci.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.pNext      = &sampled_usage;
        vci.viewType   = VK_IMAGE_VIEW_TYPE_2D;
        vci.components = { VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY };

        /* Per-plane formats:
         *   - disjoint 8-bit:  R8_UNORM    / R8G8_UNORM
         *   - disjoint 10-bit: R16_UNORM   / R16G16_UNORM
         *   - single multi-plane (always 8-bit here; 10-bit via plane
         *     view requires R10X6_UNORM_PACK16 / R10X6G10X6_UNORM_2PACK16
         *     and the shader expects 8-bit for now): R8_UNORM / R8G8_UNORM
         *     + VK_IMAGE_ASPECT_PLANE_{0,1}_BIT. */
        VkFormat y_fmt, uv_fmt;
        if (single_image) {
            y_fmt  = VK_FORMAT_R8_UNORM;
            uv_fmt = VK_FORMAT_R8G8_UNORM;
        } else {
            y_fmt  = frame.bit_depth >= u32(16) ? VK_FORMAT_R16_UNORM : VK_FORMAT_R8_UNORM;
            uv_fmt = frame.bit_depth >= u32(16) ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R8G8_UNORM;
        }

        const VkImageAspectFlags y_aspect  = single_image
                                                 ? VkImageAspectFlags(VK_IMAGE_ASPECT_PLANE_0_BIT)
                                                 : VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT);
        const VkImageAspectFlags uv_aspect = single_image
                                                 ? VkImageAspectFlags(VK_IMAGE_ASPECT_PLANE_1_BIT)
                                                 : VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT);

        vci.image            = y_image;
        vci.format           = y_fmt;
        vci.subresourceRange = { y_aspect, 0, 1, 0, 1 };
        if (VkResult r = device_.CreateImageView(vci, y_view); r != VK_SUCCESS) {
            return Err(Error { vk_error("vkCreateImageView(Y, AVVkFrame)"_str, r) });
        }
        vci.image            = single_image ? y_image : uv_image;
        vci.format           = uv_fmt;
        vci.subresourceRange = { uv_aspect, 0, 1, 0, 1 };
        if (VkResult r = device_.CreateImageView(vci, uv_view); r != VK_SUCCESS) {
            return Err(Error { vk_error("vkCreateImageView(UV, AVVkFrame)"_str, r) });
        }
    }

    /* Re-bind all three descriptor slots: Y/UV now alias FFmpeg's
     * images, dst is a fresh slot. */
    {
        VkDescriptorImageInfo      dii_y { *sampler_,
                                           *y_view,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo      dii_uv { *sampler_,
                                            *uv_view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo      dii_d { VK_NULL_HANDLE, target_view, VK_IMAGE_LAYOUT_GENERAL };
        vvk::DescriptorUpdateBatch batch;
        if (! batch.WriteImage(context->descriptor_set.clone(),
                               0,
                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_y, usize(1))) ||
            ! batch.WriteImage(context->descriptor_set.clone(),
                               1,
                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_uv, usize(1))) ||
            ! batch.WriteImage(context->descriptor_set.clone(),
                               2,
                               VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                               slice<VkDescriptorImageInfo>::from_raw_parts(&dii_d, usize(1))) ||
            ! batch.Commit().committed()) {
            return Err(Error { "failed to update AVVkFrame descriptors"_str });
        }
    }

    if (VkResult r = context->command.Reset(); r != VK_SUCCESS) {
        return Err(Error { vk_error("vkResetCommandBuffer"_str, r) });
    }
    VkCommandBufferBeginInfo bi {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (VkResult r = context->command.Begin(bi); r != VK_SUCCESS) {
        return Err(Error { vk_error("vkBeginCommandBuffer"_str, r) });
    }

    /* Acquire Y/UV from FFmpeg's image. FFmpeg's vulkan hwframes pool
     * uses VK_SHARING_MODE_CONCURRENT (we passed multiple queue families
     * to AVVulkanDeviceContext::qf), so AVVkFrame::queue_family[i] is
     * VK_QUEUE_FAMILY_IGNORED and no QFOT is allowed; both indices must
     * be IGNORED then. Only when FFmpeg ran the decode on a real,
     * different family do we record an actual ownership transfer.
     * Synchronization with the prior vkCmdDecodeVideoKHR is established
     * by the timeline semaphore wait at submit; we still set the
     * barrier's srcStageMask to VIDEO_DECODE so the validation layer's
     * per-resource hazard tracker can relate them. Single-image path
     * emits ONE barrier on the shared VkImage (covers all planes). */
    const rstd::uint32_t y_avvk_qf  = *y_qf_in_out;
    const rstd::uint32_t uv_avvk_qf = *uv_qf_in_out;
    struct QueueTransfer {
        rstd::uint32_t source;
        rstd::uint32_t destination;
    };
    auto qfot_pair = [this](rstd::uint32_t avvk_qf) -> QueueTransfer {
        if (avvk_qf == VK_QUEUE_FAMILY_IGNORED || avvk_qf == queue_family_.to_primitive()) {
            return { VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED };
        }
        return { avvk_qf, queue_family_.to_primitive() };
    };
    // src side: cross-queue execution dep is the timeline-semaphore wait at
    // submit (dstStage=COMPUTE_SHADER); barrier srcStage must overlap with
    // the wait dstStage so the validator can chain "prior decode → wait →
    // barrier → our compute". VIDEO_DECODE on this queue is meaningless
    // (this is a compute queue) and produces a WAR hazard report.
    // srcAccess=0: the semaphore release covers memory visibility.
    auto [y_acq_src, y_acq_dst]   = qfot_pair(y_avvk_qf);
    auto [uv_acq_src, uv_acq_dst] = qfot_pair(uv_avvk_qf);
    barrier_image2(context->command,
                   y_image,
                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                   0,
                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                   *y_layout_in_out,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   y_acq_src,
                   y_acq_dst);
    if (! single_image) {
        barrier_image2(context->command,
                       uv_image,
                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                       0,
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                       *uv_layout_in_out,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       uv_acq_src,
                       uv_acq_dst);
    }

    if (target == ConvertTarget::BridgeForeign) {
        barrier_image(
            context->command,
            dst,
            0,
            VK_ACCESS_SHADER_WRITE_BIT,
            target_entry->initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            target_entry->initialized ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_IGNORED,
            target_entry->initialized ? queue_family_.to_primitive() : VK_QUEUE_FAMILY_IGNORED);
    } else {
        barrier_dst_to_storage(context->command, dst, target);
    }

    context->command.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline_);
    const VkDescriptorSet descriptor_set = context->descriptor_set.handle;
    context->command.BindDescriptorSets(
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *pipeline_layout_,
        0,
        slice<VkDescriptorSet>::from_raw_parts(&descriptor_set, usize(1)),
        {});
    ShaderPushConstants pc {};
    pc.dst_w = dst_w;
    pc.dst_h = dst_h;
    for (int i = 0; i < 3; ++i) {
        pc.m_r[i]    = cm.m_r[i];
        pc.m_g[i]    = cm.m_g[i];
        pc.m_b[i]    = cm.m_b[i];
        pc.offset[i] = cm.offset[i];
    }
    context->command.PushConstants(*pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, pc);
    const rstd::uint32_t gx = (dst_w + 7) / 8;
    const rstd::uint32_t gy = (dst_h + 7) / 8;
    context->command.Dispatch(gx, gy, 1);

    /* Release Y/UV back in GENERAL layout (FFmpeg's next decode submit
     * expects that), and release dst to FOREIGN for the bridge consumer.
     * Same QFOT rules as ACQUIRE: when AVVkFrame says IGNORED (CONCURRENT
     * pool) we transition layout only and keep both indices IGNORED. */
    QueueTransfer y_release =
        (y_avvk_qf == VK_QUEUE_FAMILY_IGNORED || y_avvk_qf == queue_family_.to_primitive())
            ? QueueTransfer { VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED }
            : QueueTransfer { queue_family_.to_primitive(), y_avvk_qf };
    QueueTransfer uv_release =
        (uv_avvk_qf == VK_QUEUE_FAMILY_IGNORED || uv_avvk_qf == queue_family_.to_primitive())
            ? QueueTransfer { VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED }
            : QueueTransfer { queue_family_.to_primitive(), uv_avvk_qf };
    auto [y_rel_src, y_rel_dst]   = y_release;
    auto [uv_rel_src, uv_rel_dst] = uv_release;
    // dst side: VIDEO_DECODE on this queue would be meaningless. The
    // semaphore signal happens after all our cmds (sync1 vkQueueSubmit
    // signals at ALL_COMMANDS implicitly), so dstStage=ALL_COMMANDS,
    // dstAccess=0 just sequences the layout transition before signal.
    barrier_image2(context->command,
                   y_image,
                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                   0,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_IMAGE_LAYOUT_GENERAL,
                   y_rel_src,
                   y_rel_dst);
    if (! single_image) {
        barrier_image2(context->command,
                       uv_image,
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                       0,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_LAYOUT_GENERAL,
                       uv_rel_src,
                       uv_rel_dst);
    }
    barrier_dst_from_storage(context->command, dst, target, queue_family_.to_primitive());

    if (VkResult r = context->command.End(); r != VK_SUCCESS) {
        return Err(Error { vk_error("vkEndCommandBuffer"_str, r) });
    }

    /* Wait on AVVkFrame's timeline semaphores at their current values,
     * signal incremented values back. Bridge targets also signal the
     * binary semaphore used for SYNC_FD export. Single-image path uses one shared
     * timeline; main.cpp aliases y/uv to the same sem/value pointer
     * so we deduplicate here to avoid waiting on the same semaphore
     * twice (Vulkan UB). */
    const bool           sem_shared    = (y_sem == uv_sem);
    const rstd::uint64_t y_wait_val    = *y_sem_val_in_out;
    const rstd::uint64_t uv_wait_val   = *uv_sem_val_in_out;
    const rstd::uint64_t y_signal_val  = y_wait_val + 1;
    const rstd::uint64_t uv_signal_val = uv_wait_val + 1;

    VkSemaphore          wait_sems[2]   = { y_sem, uv_sem };
    rstd::uint64_t       wait_vals[2]   = { y_wait_val, uv_wait_val };
    VkPipelineStageFlags wait_stages[2] = {
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    };
    const rstd::uint32_t wait_count = sem_shared ? 1u : 2u;

    /* FFmpeg Vulkan decode on RADV has produced intermittent stale frames when
     * readiness is consumed only by the conversion queue. Complete source
     * readiness on the host; the queue wait below preserves AVVkFrame handoff. */
    VkSemaphoreWaitInfoKHR source_wait {};
    source_wait.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
    source_wait.semaphoreCount = wait_count;
    source_wait.pSemaphores    = wait_sems;
    source_wait.pValues        = wait_vals;
    if (const auto result =
            device_dispatch_.vkWaitSemaphoresKHR(*device_, &source_wait, ~rstd::uint64_t(0));
        result != VK_SUCCESS) {
        return Err(Error { vk_error("vkWaitSemaphores(AVVkFrame)"_str, result) });
    }

    /* Signal FFmpeg timeline semaphore(s) and converter completion.
     * Bridge handoff waits on completion before signaling its binary semaphore. */
    auto next_completion_value = completion_value_ + u64(1);
    if (next_completion_value == u64()) ++next_completion_value;
    VkSemaphore    signal_sems[3] {};
    rstd::uint64_t signal_vals[3] {};
    rstd::uint32_t signal_count = 0;
    signal_sems[signal_count]   = y_sem;
    signal_vals[signal_count]   = y_signal_val;
    ++signal_count;
    if (! sem_shared) {
        signal_sems[signal_count] = uv_sem;
        signal_vals[signal_count] = uv_signal_val;
        ++signal_count;
    }
    signal_sems[signal_count] = (*completion_timeline_)->handle();
    signal_vals[signal_count] = next_completion_value.to_primitive();
    ++signal_count;

    VkTimelineSemaphoreSubmitInfo tsi {};
    tsi.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    tsi.waitSemaphoreValueCount   = wait_count;
    tsi.pWaitSemaphoreValues      = wait_vals;
    tsi.signalSemaphoreValueCount = signal_count;
    tsi.pSignalSemaphoreValues    = signal_vals;

    VkSubmitInfo si {};
    si.sType                             = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.pNext                             = &tsi;
    si.waitSemaphoreCount                = wait_count;
    si.pWaitSemaphores                   = wait_sems;
    si.pWaitDstStageMask                 = wait_stages;
    si.commandBufferCount                = 1;
    const VkCommandBuffer command_buffer = *context->command;
    si.pCommandBuffers                   = &command_buffer;
    si.signalSemaphoreCount              = signal_count;
    si.pSignalSemaphores                 = signal_sems;
    if (VkResult r = submit_conversion(queue_,
                                       si,
                                       target,
                                       (*completion_timeline_)->handle(),
                                       next_completion_value.to_primitive(),
                                       target_exports_sync_fd(target) ? *context->export_semaphore
                                                                      : VK_NULL_HANDLE);
        r != VK_SUCCESS) {
        return Err(Error { vk_error("vkQueueSubmit"_str, r) });
    }
    vvk::SubmissionToken completion {
        (*completion_timeline_)->source(),
        next_completion_value,
    };
    context->source_y_view        = rstd::move(y_view);
    context->source_uv_view       = rstd::move(uv_view);
    context->target               = target_entry;
    context->completion           = Some(completion);
    context->reserved             = false;
    reservation.state_->submitted = true;
    target_entry->initialized     = true;
    target_entry->reserved        = false;
    target_entry->last_completion = Some(completion);
    publish_submission(dst, u32(dst_w), u32(dst_h), target, next_completion_value);

    /* Update the AVVkFrame's tracked state — caller's contract. */
    *y_sem_val_in_out  = y_signal_val;
    *uv_sem_val_in_out = uv_signal_val;
    access.clear_access(u32(0));
    if (frame.plane_count > u32(1)) access.clear_access(u32(1));
    *y_layout_in_out  = VK_IMAGE_LAYOUT_GENERAL;
    *uv_layout_in_out = VK_IMAGE_LAYOUT_GENERAL;
    // CONCURRENT pool: both stay IGNORED. Cross-family QFOT: we just
    // released back to the family AVVkFrame had on entry — preserve it.
    *y_qf_in_out  = y_avvk_qf;
    *uv_qf_in_out = uv_avvk_qf;

    int sync_fd = -1;
    if (target_exports_sync_fd(target)) {
        VkSemaphoreGetFdInfoKHR sgfi {};
        sgfi.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
        sgfi.semaphore  = *context->export_semaphore;
        sgfi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        if (VkResult r = device_.GetSemaphoreFdKHR(sgfi, &sync_fd); r != VK_SUCCESS) {
            (void)drain_submissions(u64(1'000'000'000));
            return Err(Error { vk_error("vkGetSemaphoreFdKHR"_str, r) });
        }
    }
    return Ok(sync_fd);
}

auto YuvToRgba::configure_pipeline(ConversionLimits limits) -> Result<empty, Error> {
    if (limits.max_in_flight == u32() || limits.max_in_flight > kMaxConversionContexts ||
        limits.max_drm_imports == u32() || limits.max_drm_imports > kMaxDrmImports) {
        return Err(Error { "configure_pipeline: limits are out of range"_str });
    }
    auto reclaimed = reclaim_submissions();
    if (reclaimed.is_err()) return Err(rstd::move(reclaimed).unwrap_err());
    for (const auto& context : pipeline_state_->contexts) {
        if (context->completion.is_some() || context->reserved) {
            return Err(Error { "configure_pipeline: contexts are still active"_str });
        }
    }
    pipeline_state_->max_contexts = limits.max_in_flight;
    pipeline_state_->max_imports  = limits.max_drm_imports;
    pipeline_state_->contexts.truncate(usize(limits.max_in_flight.to_primitive()));
    pipeline_state_->imports.truncate(usize(limits.max_drm_imports.to_primitive()));
    return Ok(empty {});
}

auto YuvToRgba::reserve(ConversionReservationRequest request)
    -> Result<Option<ConversionReservation>, Error> {
    const auto& destination = request.target;
    if (destination.image == VK_NULL_HANDLE || destination.width == u32() ||
        destination.height == u32()) {
        return Err(Error { "reserve: invalid destination"_str });
    }
    if ((destination.width % u32(2)) != u32() || (destination.height % u32(2)) != u32()) {
        return Err(Error { "reserve: destination dimensions must be even"_str });
    }
    if (destination.width > max_w_ || destination.height > max_h_) {
        return Err(Error { "reserve: destination exceeds configured extent"_str });
    }
    if (destination.kind == ConvertTarget::SampledLocal && destination.view == VK_NULL_HANDLE) {
        return Err(Error { "reserve: local destination view is required"_str });
    }

    auto reclaimed = reclaim_submissions();
    if (reclaimed.is_err()) return Err(rstd::move(reclaimed).unwrap_err());

    auto resolved = find_or_create_target(device_, pipeline_state_->targets, destination);
    if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
    auto* target = rstd::move(resolved).unwrap();
    if (target->reserved) return Ok(None());

    if (target->last_completion.is_some()) {
        if (request.deadline.is_none()) return Ok(None());
        const auto now = rstd::time::Instant::now();
        if (now >= *request.deadline) return Ok(None());
        auto       timeout_ns  = rstd::try_from<u64>((*request.deadline - now).as_nanos())
                                     .unwrap_or(u64(u64::MAX.to_primitive()));
        const auto observation = wait_completion(*target->last_completion, timeout_ns);
        if (observation.status == vvk::CompletionObservationStatus::Pending) return Ok(None());
        if (! observation.observed() || ! observation.completed.valid() ||
            ! vvk::CompletionCovers(observation.completed, *target->last_completion)) {
            return Err(Error {
                rstd::format("reserve: target completion wait failed: {}",
                             vk_result_str(observation.api_result)),
            });
        }
        reclaimed = reclaim_submissions();
        if (reclaimed.is_err()) return Err(rstd::move(reclaimed).unwrap_err());
        if (target->last_completion.is_some()) {
            return Err(Error { "reserve: completed target remains in flight"_str });
        }
    }

    ConversionSubmissionContext* context = nullptr;
    for (auto& candidate : pipeline_state_->contexts) {
        if (candidate->completion.is_none() && ! candidate->reserved) {
            context = candidate.get();
            break;
        }
    }
    if (! context &&
        pipeline_state_->contexts.len() < usize(pipeline_state_->max_contexts.to_primitive())) {
        auto candidate = Box<ConversionSubmissionContext>::make();
        if (const auto result = cmd_pool_.Allocate(
                usize(1), VK_COMMAND_BUFFER_LEVEL_PRIMARY, candidate->command_buffers);
            result != VK_SUCCESS) {
            return Err(Error { vk_error("vkAllocateCommandBuffers(conversion)"_str, result) });
        }
        candidate->command =
            vvk::CommandBuffer(candidate->command_buffers[usize()], device_dispatch_);
        auto allocation = vvk::DescriptorArenaGeneration::Allocate(*descriptor_arena_, *dsl_);
        if (! allocation.allocated()) {
            return Err(Error {
                vk_error("vkAllocateDescriptorSets(conversion)"_str, allocation.api_result) });
        }
        candidate->descriptor_set = rstd::move(allocation.lease);

        context = candidate.get();
        pipeline_state_->contexts.push(rstd::move(candidate));
    }
    if (! context) return Ok(None());

    if (target_exports_sync_fd(destination.kind) && ! context->export_semaphore) {
        VkExportSemaphoreCreateInfo export_info {};
        export_info.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        VkSemaphoreCreateInfo semaphore_info {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_info.pNext = &export_info;
        if (const auto result = device_.CreateSemaphore(semaphore_info, context->export_semaphore);
            result != VK_SUCCESS) {
            return Err(Error { vk_error("vkCreateSemaphore(conversion export)"_str, result) });
        }
    }
    context->reserved  = true;
    target->reserved   = true;
    auto state         = Box<ConversionReservation::State>::make();
    state->owner       = this;
    state->context     = context;
    state->target      = target;
    state->destination = destination;
    return Ok(Some(ConversionReservation(rstd::move(state).into_raw().as_raw_ptr())));
}

auto YuvToRgba::reclaim_submissions() -> Result<usize, Error> {
    bool has_pending = false;
    for (const auto& context : pipeline_state_->contexts) {
        if (context->completion.is_some()) {
            has_pending = true;
            break;
        }
    }
    if (! has_pending) return Ok(usize());

    const auto observation = poll_completion();
    if (observation.status == vvk::CompletionObservationStatus::DeviceLost ||
        observation.status == vvk::CompletionObservationStatus::ApiError ||
        observation.status == vvk::CompletionObservationStatus::Invalid) {
        return Err(Error {
            rstd::format("reclaim_submissions: timeline query failed: {}",
                         vk_result_str(observation.api_result)),
        });
    }

    usize reclaimed {};
    for (auto& context : pipeline_state_->contexts) {
        if (context->completion.is_none() || ! observation.completed.valid() ||
            ! vvk::CompletionCovers(observation.completed, *context->completion)) {
            continue;
        }
        if (context->target && (context->target->last_completion.is_none() ||
                                *context->target->last_completion != *context->completion)) {
            return Err(Error { "reclaim_submissions: target completion mismatch"_str });
        }
        if (context->source) --context->source->in_flight;
        if (context->target) (void)context->target->last_completion.take();
        context->source = nullptr;
        context->target = nullptr;
        (void)context->vk_frame.take();
        (void)context->frame.take();
        context->source_y_view  = {};
        context->source_uv_view = {};
        (void)context->completion.take();
        ++reclaimed;
    }
    return Ok(reclaimed);
}

auto YuvToRgba::drain_submissions(u64 timeout_ns) -> Result<empty, Error> {
    Option<vvk::SubmissionToken> latest;
    for (const auto& context : pipeline_state_->contexts) {
        if (context->completion.is_none()) continue;
        if (latest.is_none() || context->completion->value > latest->value) {
            latest = Some<vvk::SubmissionToken>(*context->completion);
        }
    }
    if (latest.is_none()) return Ok(empty {});

    const auto observation = wait_completion(*latest, timeout_ns);
    if (! observation.observed() || ! observation.completed.valid() ||
        ! vvk::CompletionCovers(observation.completed, *latest)) {
        return Err(Error {
            rstd::format("drain_submissions: timeline wait failed: {}",
                         vk_result_str(observation.api_result)),
        });
    }
    auto reclaimed = reclaim_submissions();
    if (reclaimed.is_err()) return Err(rstd::move(reclaimed).unwrap_err());
    return Ok(empty {});
}

auto YuvToRgba::invalidate_targets() -> Result<empty, Error> {
    auto reclaimed = reclaim_submissions();
    if (reclaimed.is_err()) return Err(rstd::move(reclaimed).unwrap_err());
    for (const auto& context : pipeline_state_->contexts) {
        if (context->completion.is_some() || context->reserved) {
            return Err(Error { "invalidate_targets: contexts are still active"_str });
        }
    }
    for (const auto& target : pipeline_state_->targets) {
        if (target->reserved || target->last_completion.is_some()) {
            return Err(Error { "invalidate_targets: targets are still active"_str });
        }
    }
    pipeline_state_->targets.clear();
    return Ok(empty {});
}

auto YuvToRgba::submit_drm_prime(ConversionReservation&& reservation, DrmFrameLease&& frame,
                                 const ColorMatrix& cm)
    -> Result<Option<ConversionSubmission>, Error> {
    if (! reservation.valid() || reservation.state_->owner != this ||
        ! reservation.state_->context || ! reservation.state_->target ||
        ! reservation.state_->context->reserved || ! reservation.state_->target->reserved ||
        reservation.state_->context->completion.is_some()) {
        return Err(Error { "submit_drm_prime: invalid reservation"_str });
    }
    const auto& destination = reservation.state_->destination;
    const auto  dst         = destination.image;
    const auto  dst_w       = destination.width;
    const auto  dst_h       = destination.height;
    const auto  target      = destination.kind;
    if (! frame.valid()) return Err(Error { "submit_drm_prime: invalid frame lease"_str });
    if (dst == VK_NULL_HANDLE || dst_w == u32() || dst_h == u32()) {
        return Err(Error { "submit_drm_prime: invalid destination"_str });
    }
    if (target == ConvertTarget::SampledLocal && destination.view == VK_NULL_HANDLE) {
        return Err(Error { "submit_drm_prime: local destination view is required"_str });
    }
    if ((dst_w % u32(2)) != u32() || (dst_h % u32(2)) != u32()) {
        return Err(Error { "submit_drm_prime: destination dimensions must be even"_str });
    }
    if (dst_w > max_w_ || dst_h > max_h_) {
        return Err(Error { "submit_drm_prime: destination exceeds configured extent"_str });
    }
    if (! device_dispatch_.vkGetMemoryFdPropertiesKHR) {
        return Err(Error { "submit_drm_prime: vkGetMemoryFdPropertiesKHR missing"_str });
    }

    auto* context      = reservation.state_->context;
    auto* target_entry = reservation.state_->target;

    DrmImportEntry* source      = nullptr;
    Option<usize>   stale_index = None();
    const auto      key         = frame.resource_key();
    const auto&     view        = frame.view();
    for (usize index {}; index < pipeline_state_->imports.len(); ++index) {
        auto* candidate = pipeline_state_->imports[index].get();
        if (candidate->key != key) continue;
        if (same_drm_signature(candidate->signature, view)) {
            if (candidate->in_flight != u32()) return Ok(None());
            source = candidate;
        } else {
            if (candidate->in_flight != u32()) return Ok(None());
            stale_index = Some(index);
        }
        break;
    }
    if (stale_index.is_some()) {
        (void)pipeline_state_->imports.remove(*stale_index);
    }
    if (! source) {
        if (pipeline_state_->imports.len() >= usize(pipeline_state_->max_imports.to_primitive())) {
            Option<usize> eviction;
            u64           oldest = u64::MAX;
            for (usize index {}; index < pipeline_state_->imports.len(); ++index) {
                const auto* candidate = pipeline_state_->imports[index].get();
                if (candidate->in_flight == u32() && candidate->last_use_serial < oldest) {
                    oldest   = candidate->last_use_serial;
                    eviction = Some(index);
                }
            }
            if (eviction.is_none()) return Ok(None());
            (void)pipeline_state_->imports.remove(*eviction);
        }
        auto imported = create_drm_import(device_, phys_, frame);
        if (imported.is_err()) return Err(rstd::move(imported).unwrap_err());
        auto entry = rstd::move(imported).unwrap();
        source     = entry.get();
        pipeline_state_->imports.push(rstd::move(entry));
    }

    const auto            target_view = target_entry->view;
    VkDescriptorImageInfo y_info {
        *sampler_,
        *source->y_view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo uv_info {
        *sampler_,
        *source->uv_view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo      dst_info { VK_NULL_HANDLE, target_view, VK_IMAGE_LAYOUT_GENERAL };
    vvk::DescriptorUpdateBatch descriptors;
    if (! descriptors.WriteImage(context->descriptor_set.clone(),
                                 0,
                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                 slice<VkDescriptorImageInfo>::from_raw_parts(&y_info, usize(1))) ||
        ! descriptors.WriteImage(
            context->descriptor_set.clone(),
            1,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            slice<VkDescriptorImageInfo>::from_raw_parts(&uv_info, usize(1))) ||
        ! descriptors.WriteImage(
            context->descriptor_set.clone(),
            2,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            slice<VkDescriptorImageInfo>::from_raw_parts(&dst_info, usize(1))) ||
        ! descriptors.Commit().committed()) {
        return Err(Error { "submit_drm_prime: failed to update descriptors"_str });
    }

    if (const auto result = context->command.Reset(); result != VK_SUCCESS) {
        return Err(Error { vk_error("vkResetCommandBuffer(DRM)"_str, result) });
    }
    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (const auto result = context->command.Begin(begin_info); result != VK_SUCCESS) {
        return Err(Error { vk_error("vkBeginCommandBuffer(DRM)"_str, result) });
    }

    barrier_image(context->command,
                  *source->image,
                  0,
                  VK_ACCESS_SHADER_READ_BIT,
                  source->initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_QUEUE_FAMILY_FOREIGN_EXT,
                  queue_family_.to_primitive());
    if (source->split_planes) {
        barrier_image(context->command,
                      *source->uv_image,
                      0,
                      VK_ACCESS_SHADER_READ_BIT,
                      source->initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_QUEUE_FAMILY_FOREIGN_EXT,
                      queue_family_.to_primitive());
    }
    if (target == ConvertTarget::BridgeForeign) {
        barrier_image(
            context->command,
            dst,
            0,
            VK_ACCESS_SHADER_WRITE_BIT,
            target_entry->initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            target_entry->initialized ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_IGNORED,
            target_entry->initialized ? queue_family_.to_primitive() : VK_QUEUE_FAMILY_IGNORED);
    } else {
        barrier_dst_to_storage(context->command, dst, target);
    }

    context->command.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline_);
    const auto descriptor_set = context->descriptor_set.handle;
    context->command.BindDescriptorSets(
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *pipeline_layout_,
        0,
        slice<VkDescriptorSet>::from_raw_parts(&descriptor_set, usize(1)),
        {});
    ShaderPushConstants push_constants {};
    push_constants.dst_w = dst_w.to_primitive();
    push_constants.dst_h = dst_h.to_primitive();
    for (int index = 0; index < 3; ++index) {
        push_constants.m_r[index]    = cm.m_r[index];
        push_constants.m_g[index]    = cm.m_g[index];
        push_constants.m_b[index]    = cm.m_b[index];
        push_constants.offset[index] = cm.offset[index];
    }
    context->command.PushConstants(*pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, push_constants);
    context->command.Dispatch((dst_w.to_primitive() + 7) / 8, (dst_h.to_primitive() + 7) / 8, 1);

    barrier_image(context->command,
                  *source->image,
                  VK_ACCESS_SHADER_READ_BIT,
                  0,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_GENERAL,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                  queue_family_.to_primitive(),
                  VK_QUEUE_FAMILY_FOREIGN_EXT);
    if (source->split_planes) {
        barrier_image(context->command,
                      *source->uv_image,
                      VK_ACCESS_SHADER_READ_BIT,
                      0,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_GENERAL,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                      queue_family_.to_primitive(),
                      VK_QUEUE_FAMILY_FOREIGN_EXT);
    }
    barrier_dst_from_storage(context->command, dst, target, queue_family_.to_primitive());
    if (const auto result = context->command.End(); result != VK_SUCCESS) {
        return Err(Error { vk_error("vkEndCommandBuffer(DRM)"_str, result) });
    }

    auto completion_value = completion_value_ + u64(1);
    if (completion_value == u64()) ++completion_value;
    const VkSemaphore             signal_semaphore = (*completion_timeline_)->handle();
    const auto                    signal_value     = completion_value.to_primitive();
    VkTimelineSemaphoreSubmitInfo timeline_info {};
    timeline_info.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_info.signalSemaphoreValueCount = 1;
    timeline_info.pSignalSemaphoreValues    = &signal_value;
    const auto   command_buffer             = *context->command;
    VkSubmitInfo submit_info {};
    submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext                = &timeline_info;
    submit_info.commandBufferCount   = 1;
    submit_info.pCommandBuffers      = &command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores    = &signal_semaphore;
    if (const auto result = submit_conversion(
            queue_,
            submit_info,
            target,
            signal_semaphore,
            signal_value,
            target_exports_sync_fd(target) ? *context->export_semaphore : VK_NULL_HANDLE);
        result != VK_SUCCESS) {
        return Err(Error { vk_error("vkQueueSubmit(DRM)"_str, result) });
    }

    const vvk::SubmissionToken token {
        (*completion_timeline_)->source(),
        completion_value,
    };
    source->initialized     = true;
    source->last_use_serial = ++pipeline_state_->use_serial;
    ++source->in_flight;
    target_entry->initialized     = true;
    target_entry->reserved        = false;
    target_entry->last_completion = Some<vvk::SubmissionToken>(token);
    context->source               = source;
    context->target               = target_entry;
    context->completion           = Some<vvk::SubmissionToken>(token);
    context->frame                = Some(rstd::move(frame));
    context->reserved             = false;
    reservation.state_->submitted = true;
    publish_submission(dst, dst_w, dst_h, target, completion_value);

    int sync_fd = -1;
    if (target_exports_sync_fd(target)) {
        VkSemaphoreGetFdInfoKHR fd_info {};
        fd_info.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
        fd_info.semaphore  = *context->export_semaphore;
        fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        if (const auto result = device_.GetSemaphoreFdKHR(fd_info, &sync_fd);
            result != VK_SUCCESS) {
            (void)drain_submissions(u64(1'000'000'000));
            return Err(Error { vk_error("vkGetSemaphoreFdKHR(DRM)"_str, result) });
        }
    }

    auto submission    = last_submission_->clone();
    submission.sync_fd = sync_fd;
    return Ok(Some(rstd::move(submission)));
}

void YuvToRgba::publish_submission(VkImage dst, u32 dst_w, u32 dst_h, ConvertTarget target,
                                   u64 completion_value) {
    completion_value_           = completion_value;
    const auto content_revision = next_content_revision();
    last_submission_            = Some(ConversionSubmission {
        .target       = dst,
        .width        = dst_w,
        .height       = dst_h,
        .target_kind  = target,
        .final_layout = target == ConvertTarget::SampledLocal
                            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_GENERAL,
        .final_queue_family =
            target == ConvertTarget::SampledLocal ? queue_family_.to_primitive()
                                                   : VK_QUEUE_FAMILY_FOREIGN_EXT,
        .content_revision = content_revision,
        .readiness        = { (*completion_timeline_)->source(), completion_value },
        .execution_dependency =
            {
                .timeline   = Some((*completion_timeline_).clone()),
                .completion = { (*completion_timeline_)->source(), completion_value },
            },
    });
}

vvk::CompletionObservation YuvToRgba::poll_completion() const noexcept {
    return completion_observer_.is_some() && completion_timeline_.is_some()
               ? completion_observer_->Poll((*completion_timeline_)->source())
               : vvk::CompletionObservation {};
}

Option<vvk::SubmissionToken> YuvToRgba::last_submission_readiness() const noexcept {
    if (last_submission_.is_none()) return None();
    auto readiness = last_submission_->readiness;
    return Some(rstd::move(readiness));
}

vvk::CompletionObservation YuvToRgba::wait_completion(const vvk::SubmissionToken& required,
                                                      u64 timeout_ns) const noexcept {
    return completion_observer_.is_some() ? completion_observer_->Wait(required, timeout_ns)
                                          : vvk::CompletionObservation {};
}

// ---------------------------------------------------------------------------
// Public Result wrappers around the legacy out-param helpers above.
// ---------------------------------------------------------------------------

auto YuvToRgba::convert_nv12(VkImage dst, u32 dst_w, u32 dst_h, const rstd::uint8_t* nv12,
                             usize nv12_size, const ColorMatrix& cm) -> Result<int, Error> {
    return convert_nv12(dst, dst_w, dst_h, nv12, nv12_size, cm, ConvertTarget::BridgeForeign);
}

auto YuvToRgba::convert_nv12(VkImage dst, u32 dst_w, u32 dst_h, const rstd::uint8_t* nv12,
                             usize nv12_size, const ColorMatrix& cm, ConvertTarget target)
    -> Result<int, Error> {
    auto submitted = submit_nv12(dst, dst_w, dst_h, nv12, nv12_size, cm, target);
    if (submitted.is_err()) return Err(rstd::move(submitted).unwrap_err());
    return Ok(rstd::move(submitted).unwrap().sync_fd);
}

auto YuvToRgba::submit_nv12(VkImage dst, u32 dst_w, u32 dst_h, const rstd::uint8_t* nv12,
                            usize nv12_size, const ColorMatrix& cm, ConvertTarget target)
    -> Result<ConversionSubmission, Error> {
    (void)last_submission_.take();
    Error err;
    int   fd = convert_nv12_(
        dst, dst_w.to_primitive(), dst_h.to_primitive(), nv12, nv12_size, cm, target, &err);
    if (fd < 0 && ! err.message.is_empty()) return Err(rstd::move(err));
    if (! last_submission_ || ! last_submission_->submitted()) {
        return Err(Error { "YuvToRgba: conversion returned without a submission"_str });
    }
    auto submission    = last_submission_->clone();
    submission.sync_fd = fd;
    return Ok(rstd::move(submission));
}

auto YuvToRgba::submit_av_vk_frame(ConversionReservation&& reservation, VkFrameLease&& frame,
                                   const ColorMatrix& cm) -> Result<ConversionSubmission, Error> {
    (void)last_submission_.take();
    auto accessed = frame.lock();
    if (accessed.is_err()) return Err(rstd::move(accessed).unwrap_err());
    auto access    = rstd::move(accessed).unwrap();
    auto converted = convert_av_vk_frame_(reservation, access, cm);
    if (converted.is_err()) return Err(rstd::move(converted).unwrap_err());
    auto fd                               = rstd::move(converted).unwrap();
    reservation.state_->context->vk_frame = Some(rstd::move(frame));
    if (! last_submission_ || ! last_submission_->submitted()) {
        return Err(Error { "YuvToRgba: conversion returned without a submission"_str });
    }
    auto submission    = last_submission_->clone();
    submission.sync_fd = fd;
    return Ok(rstd::move(submission));
}

} // namespace wavsen::video
