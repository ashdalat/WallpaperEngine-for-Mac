module;

#include <cerrno>
#include <vulkan/vulkan.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavcodec/version.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libavutil/mem.h>
#include <libavutil/version.h>
}

export module wavsen.ffi.ffmpeg:avcodec;
export import wavsen.ffi.ffmpeg.audio;

namespace _wv_avcodec
{

const VkVideoProfileListInfoKHR* find_video_profiles(const void* chain) {
    auto* node = static_cast<const VkBaseInStructure*>(chain);
    while (node) {
        if (node->sType == VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR) {
            return reinterpret_cast<const VkVideoProfileListInfoKHR*>(node);
        }
        node = node->pNext;
    }
    return nullptr;
}

bool supports_video_format(AVVulkanDeviceContext* device, AVVulkanFramesContext* frames,
                           const VkVideoProfileListInfoKHR* profiles, VkImageUsageFlags usage) {
    auto get_formats = reinterpret_cast<PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR>(
        device->get_proc_addr(device->inst, "vkGetPhysicalDeviceVideoFormatPropertiesKHR"));
    if (! get_formats) return false;

    VkPhysicalDeviceVideoFormatInfoKHR info {
        .sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR,
        .pNext      = profiles,
        .imageUsage = usage,
    };
    uint32_t count = 0;
    if (get_formats(device->phys_dev, &info, &count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }

    auto* formats = static_cast<VkVideoFormatPropertiesKHR*>(
        av_calloc(count, sizeof(VkVideoFormatPropertiesKHR)));
    if (! formats) return false;
    for (uint32_t i = 0; i < count; ++i) {
        formats[i].sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
    }

    bool supported = false;
    if (get_formats(device->phys_dev, &info, &count, formats) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i) {
            const auto& format = formats[i];
            if (format.format == frames->format[0] && format.imageType == VK_IMAGE_TYPE_2D &&
                format.imageTiling == frames->tiling && (format.imageUsageFlags & usage) == usage) {
                supported = true;
                break;
            }
        }
    }
    av_free(formats);
    return supported;
}

int prepare_vulkan_decode_frames(AVBufferRef* frames_ref) {
    auto* frames   = reinterpret_cast<AVHWFramesContext*>(frames_ref->data);
    auto* vkframes = reinterpret_cast<AVVulkanFramesContext*>(frames->hwctx);
    auto* profiles = find_video_profiles(vkframes->create_pnext);
    if (! profiles) return av_hwframe_ctx_init(frames_ref);

    constexpr VkImageUsageFlags consumer_usage =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
    vkframes->usage = static_cast<VkImageUsageFlagBits>(
        static_cast<VkImageUsageFlags>(vkframes->usage) & consumer_usage);

    VkImageUsageFlags actual_usage = vkframes->usage;
#if LIBAVUTIL_VERSION_MAJOR >= 60
    // Newer hwcontext_vulkan versions add generic format usage during initialization.
    actual_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
#endif

    auto* device_context = reinterpret_cast<AVHWDeviceContext*>(frames->device_ref->data);
    auto* vkdevice       = reinterpret_cast<AVVulkanDeviceContext*>(device_context->hwctx);
    if (! supports_video_format(vkdevice, vkframes, profiles, actual_usage)) return -ENOTSUP;

    return av_hwframe_ctx_init(frames_ref);
}

} // namespace _wv_avcodec

export {

    using ::avcodec_default_get_format;
    using ::avcodec_get_hw_frames_parameters;

    inline int avcodec_prepare_vulkan_decode_frames(AVBufferRef* frames_ref) {
        return _wv_avcodec::prepare_vulkan_decode_frames(frames_ref);
    }

}
