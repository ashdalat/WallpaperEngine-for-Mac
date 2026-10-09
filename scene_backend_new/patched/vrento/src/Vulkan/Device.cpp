module;

#include <rstd/macro.hpp>

#include "vvk/macros.hpp"

module vrento.vulkan;

import rstd;
import rstd.log;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::collections::BTreeSet;
using rstd::ffi::CStr;
using rstd::ffi::CString;

#include "ExtensionNames.hpp"
using namespace vrento::vulkan;

namespace
{

void EnumateDeviceExts(const vvk::PhysicalDevice& gpu, BTreeSet<String>& set) {
    Vec<VkExtensionProperties> properties;
    VVK_CHECK_VOID_RE(gpu.EnumerateDeviceExtensionProperties(properties));
    for (auto& ext : properties)
        set.insert(String::make(CStr::from_ptr(ext.extensionName).to_str().unwrap()));
}

} // namespace

bool Device::CheckGPU(vvk::PhysicalDevice gpu, slice<Extension> exts, VkSurfaceKHR surface) {
    auto props = gpu.GetQueueFamilyProperties();

    bool     has_graphics_queue { false };
    bool     has_present_queue { false };
    unsigned index { 0 };
    for (auto& prop : props) {
        if (prop.queueFlags & VK_QUEUE_GRAPHICS_BIT) has_graphics_queue = true;
        if (surface) {
            bool ok { false };
            VVK_CHECK(gpu.GetSurfaceSupportKHR(index, surface, ok));
            if (ok) has_present_queue = true;
        }
        index++;
    }
    if (! has_graphics_queue) return false;
    if (surface && ! has_present_queue) return false;

    auto extensions = BTreeSet<String>::make();
    EnumateDeviceExts(gpu, extensions);
    bool requires_timeline_semaphore { false };
    for (auto& ext : exts) {
        if (ext.required) {
            if (! extensions.contains(ext.name)) return false;
            if (ext.name ==
                CStr::from_ptr(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME).to_str().unwrap()) {
                requires_timeline_semaphore = true;
            }
        }
    }
    VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timeline_features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR,
        .pNext = nullptr,
    };
    VkPhysicalDeviceFeatures2KHR features2 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR,
        .pNext = requires_timeline_semaphore ? &timeline_features : nullptr,
    };
    gpu.GetFeatures2KHR(features2);
#if ! __is_target_os(macos)
    if (! features2.features.geometryShader) return false;
#endif
    if (requires_timeline_semaphore && ! timeline_features.timelineSemaphore) return false;
    return true;
}

Vec<VkDeviceQueueCreateInfo> Device::ChooseDeviceQueue(VkSurfaceKHR surface) {
    Vec<VkDeviceQueueCreateInfo> queues;

    auto props = m_gpu.GetQueueFamilyProperties();

    Vec<rstd::uint32_t> graphic_indexs, present_indexs;
    rstd::uint32_t      index = 0;
    for (auto& prop : props) {
        if (prop.queueFlags & VK_QUEUE_GRAPHICS_BIT) graphic_indexs.emplace_back(index);
        index++;
    }
    m_graphics_queue.family_index           = graphic_indexs[usize()];
    const static float defaultQueuePriority = 0.0f;
    m_present_queue.family_index            = graphic_indexs[usize()];
    if (surface) {
        index = 0;
        for (auto& prop : props) {
            (void)prop;
            bool ok { false };
            VVK_CHECK(m_gpu.GetSurfaceSupportKHR(index, surface, ok))
            if (ok) present_indexs.emplace_back(index);
            index++;
        }
        if (present_indexs.is_empty()) {
            rstd_error("not find present queue");
        } else {
            m_present_queue.family_index = present_indexs[usize()];
        }
    }
    for (rstd::uint32_t i = 0; i < props.len().to_primitive(); ++i) {
        if (props[usize(i)].queueCount == 0) continue;
        VkDeviceQueueCreateInfo info {
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = i,
            .queueCount       = 1,
            .pQueuePriorities = &defaultQueuePriority,
        };
        queues.push(rstd::move(info));
    }
    return queues;
}

bool Device::Create(Instance& inst, slice<Extension> exts, VkExtent2D extent, Device& device) {
    device.m_instance             = *inst.inst();
    device.m_instance_api_version = inst.api_version();
    device.m_gpu                  = inst.gpu();
    device.m_limits               = inst.gpu().GetProperties().limits;
    device.set_out_extent(extent);
    device.m_enabled_instance_extensions = Vec<CString>::from(inst.enabled_extensions());

    EnumateDeviceExts(inst.gpu(), device.m_extensions);
    auto selected = detail::SelectExtensionNames(device.m_extensions, exts);
    if (selected.is_err()) {
        rstd_error("required vulkan device extension \"{}\" is not supported",
                   selected.unwrap_err());
        return false;
    }
    device.m_enabled_device_extensions = rstd::move(selected).unwrap();
    auto tested_exts_c = Vec<const char*>::with_capacity(device.m_enabled_device_extensions.len());
    auto tested_exts   = BTreeSet<String>::make();
    for (const auto& name : device.m_enabled_device_extensions) {
        tested_exts_c.push(name.as_ptr());
        tested_exts.insert(String::make(name.as_ref().to_str().unwrap()));
    }
    device.m_instance_dispatch = &inst.inst().Dispatch();
    bool rq_surface            = ! inst.offscreen();

    // The WE particle vertex ABI can use a geometry-shader or expanded-quad path
    // on Apple/Metal, where Vulkan geometry shaders are not available.
    VkPhysicalDeviceTimelineSemaphoreFeaturesKHR supported_timeline {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR,
        .pNext = nullptr,
    };
    const bool has_depth_clip_extension = tested_exts.contains(
        CStr::from_ptr(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME).to_str().unwrap());
    VkPhysicalDeviceDepthClipEnableFeaturesEXT supported_depth_clip {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT,
    };
    VkPhysicalDeviceSynchronization2FeaturesKHR supported_sync2 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR,
        .pNext = has_depth_clip_extension ? &supported_depth_clip : nullptr,
    };
    VkPhysicalDeviceSamplerYcbcrConversionFeatures supported_ycbcr {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,
        .pNext = &supported_sync2,
    };
    supported_timeline.pNext = &supported_ycbcr;
    VkPhysicalDeviceFeatures2KHR supported2 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR,
        .pNext = &supported_timeline,
    };
    device.m_gpu.GetFeatures2KHR(supported2);
    if (! supported_timeline.timelineSemaphore) {
        rstd_error("required vulkan feature timelineSemaphore is not supported");
        return false;
    }
#if ! __is_target_os(macos)
    if (! supported2.features.geometryShader) {
        rstd_error("required vulkan feature geometryShader is not supported");
        return false;
    }
#endif
    const bool enable_shader_output_viewport_index = tested_exts.contains(
        CStr::from_ptr(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME).to_str().unwrap());
    const bool enable_multi_viewport =
        enable_shader_output_viewport_index && supported2.features.multiViewport;
    const auto d32_features =
        device.m_gpu.GetFormatProperties(VK_FORMAT_D32_SFLOAT).optimalTilingFeatures;
    const bool sampled_depth_d32 =
        (d32_features &
         (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) ==
        (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    VkPhysicalDeviceFeatures enabled {};
#if __is_target_os(macos)
    enabled.geometryShader = supported2.features.geometryShader;
#else
    enabled.geometryShader = VK_TRUE;
#endif
    enabled.sampleRateShading = supported2.features.sampleRateShading;
    enabled.samplerAnisotropy = supported2.features.samplerAnisotropy;
    enabled.multiViewport     = enable_multi_viewport ? VK_TRUE : VK_FALSE;
    enabled.depthClamp        = supported2.features.depthClamp;
    VkPhysicalDeviceTimelineSemaphoreFeaturesKHR enabled_timeline {
        .sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR,
        .pNext             = nullptr,
        .timelineSemaphore = VK_TRUE,
    };
    const bool enable_sync2 =
        tested_exts.contains(
            CStr::from_ptr(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME).to_str().unwrap()) &&
        supported_sync2.synchronization2;
    const bool enable_depth_clip = has_depth_clip_extension && supported_depth_clip.depthClipEnable;
    VkPhysicalDeviceDepthClipEnableFeaturesEXT enabled_depth_clip {
        .sType           = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT,
        .depthClipEnable = VK_TRUE,
    };
    VkPhysicalDeviceSynchronization2FeaturesKHR enabled_sync2 {
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR,
        .pNext            = enable_depth_clip ? &enabled_depth_clip : nullptr,
        .synchronization2 = enable_sync2 ? VK_TRUE : VK_FALSE,
    };
    VkPhysicalDeviceSamplerYcbcrConversionFeatures enabled_ycbcr {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,
        .pNext = enable_sync2 ? static_cast<void*>(&enabled_sync2)
                              : (enable_depth_clip ? &enabled_depth_clip : nullptr),
        .samplerYcbcrConversion = supported_ycbcr.samplerYcbcrConversion,
    };
    enabled_timeline.pNext = &enabled_ycbcr;

    auto               queue_create_infos = device.ChooseDeviceQueue(*inst.surface());
    VkDeviceCreateInfo device_info {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled_timeline,
        .queueCreateInfoCount =
            static_cast<rstd::uint32_t>(queue_create_infos.len().to_primitive()),
        .pQueueCreateInfos       = queue_create_infos.data(),
        .enabledExtensionCount   = static_cast<rstd::uint32_t>(tested_exts_c.len().to_primitive()),
        .ppEnabledExtensionNames = tested_exts_c.data(),
        .pEnabledFeatures        = &enabled,
    };
    auto created = vvk::Device::Create(
        device.m_device, *device.m_gpu, inst.inst().Dispatch(), device_info, device.dld);
    if (created.is_err()) {
        const auto error = created.unwrap_err_unchecked();
        rstd_error("device creation failed: kind={}, vk={}, command={}",
                   static_cast<int>(error.kind),
                   static_cast<int>(error.api_result),
                   error.command ? error.command : "");
        return false;
    }

    device.m_graphics_queue.handle = device.m_device.GetQueue(device.m_graphics_queue.family_index);
    device.m_present_queue.handle  = device.m_device.GetQueue(device.m_present_queue.family_index);
    rstd::uint32_t max_push_descriptors {};
    if (tested_exts.contains(
            CStr::from_ptr(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME).to_str().unwrap())) {
        VkPhysicalDevicePushDescriptorPropertiesKHR push_properties {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR,
        };
        VkPhysicalDeviceProperties2 properties {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &push_properties,
        };
        device.m_gpu.GetProperties2KHR(properties);
        max_push_descriptors = push_properties.maxPushDescriptors;
    }
    device.m_capabilities = DeviceCapabilities {
        .timeline_semaphore = true,
        .geometry_shader    = supported2.features.geometryShader != VK_FALSE,
        .synchronization2   = enable_sync2,
        .push_descriptor    = tested_exts.contains(
            CStr::from_ptr(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME).to_str().unwrap()),
        .max_push_descriptors                 = max_push_descriptors,
        .multi_viewport                       = enable_multi_viewport,
        .shader_output_viewport_index         = enable_shader_output_viewport_index,
        .sampled_depth_d32                    = sampled_depth_d32,
        .depth_clamp                          = supported2.features.depthClamp != VK_FALSE,
        .depth_clip_enable                    = enable_depth_clip,
        .max_geometry_output_vertices         = device.m_limits.maxGeometryOutputVertices,
        .max_geometry_total_output_components = device.m_limits.maxGeometryTotalOutputComponents,
        .memory_budget                        = tested_exts.contains(
            CStr::from_ptr(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME).to_str().unwrap()),
        .external_memory_fd = tested_exts.contains(
            CStr::from_ptr(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME).to_str().unwrap()),
        .external_memory_dma_buf = tested_exts.contains(
            CStr::from_ptr(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME).to_str().unwrap()),
        .drm_format_modifier = tested_exts.contains(
            CStr::from_ptr(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME).to_str().unwrap()),
        .foreign_queue = tested_exts.contains(
            CStr::from_ptr(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME).to_str().unwrap()),
        .graphics_queue_family = device.m_graphics_queue.family_index,
        .present_queue_family  = device.m_present_queue.family_index,
    };

    if (rq_surface) {
        if (! Swapchain::Create(device, *inst.surface(), extent, device.m_swapchain)) {
            rstd_error("create swapchain failed");
            return false;
        }
    }
    {
        VkCommandPoolCreateInfo info { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                       .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                                                VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                       .queueFamilyIndex = device.m_graphics_queue.family_index };
        VVK_CHECK_BOOL_RE(device.m_device.CreateCommandPool(info, device.m_command_pool));
    }
    {
        auto allocator =
            vvk::MemoryAllocator::Create(*device.m_gpu, inst.inst().Dispatch(), device.dld);
        if (allocator.is_err()) {
            const auto error = allocator.unwrap_err_unchecked();
            rstd_error("memory allocator creation failed: kind={}, vk={}",
                       static_cast<int>(error.kind),
                       static_cast<int>(error.api_result));
            return false;
        }
        device.m_allocator = allocator.unwrap_unchecked();
    }
    return true;
}

VkDeviceSize Device::GetUsage() const { return MemoryBudget().usage; }

auto Device::MemoryBudget() const -> MemoryBudgetSnapshot {
    const auto           budgets = m_allocator.budget();
    MemoryBudgetSnapshot snapshot;
    for (rstd::uint32_t index = 0; index < budgets.heap_count; ++index) {
        snapshot.usage += budgets.heaps[index].usage;
        snapshot.budget += budgets.heaps[index].budget;
    }
    return snapshot;
}

void Device::Destroy() { VVK_CHECK(m_device.WaitIdle()); }

Device::Device() = default;
Device::~Device() {}

bool Device::supportExt(ref<str> name) const { return m_extensions.contains(name); }
