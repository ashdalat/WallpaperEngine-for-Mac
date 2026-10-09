module vvk;
import rstd;
using namespace rstd::prelude;

namespace vvk
{
DispatchError MissingDispatch(DispatchStage stage, const char* command) {
    return { DispatchErrorKind::MissingCommand, stage, command };
}
bool HasExtension(rstd::uint32_t count, const char* const* names, const char* wanted) {
    if (! names) return false;
    const auto wanted_bytes = rstd::ffi::CStr::from_ptr(wanted).to_bytes();
    for (rstd::uint32_t i = 0; i < count; ++i)
        if (names[i] && (rstd::ffi::CStr::from_ptr(names[i]).to_bytes() == wanted_bytes))
            return true;
    return false;
}
auto ParseInstanceCapabilities(const VkInstanceCreateInfo& info)
    -> Result<InstanceCapabilities, DispatchError> {
    if (info.sType != VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO ||
        (info.enabledExtensionCount && ! info.ppEnabledExtensionNames))
        return Err(DispatchError { DispatchErrorKind::InvalidInput, DispatchStage::Instance });
    InstanceCapabilities caps;
    caps.api_version = info.pApplicationInfo && info.pApplicationInfo->apiVersion
                           ? info.pApplicationInfo->apiVersion
                           : VK_API_VERSION_1_0;
    if (VK_API_VERSION_VARIANT(caps.api_version) != 0 || caps.api_version < VK_API_VERSION_1_1)
        return Err(DispatchError {
            DispatchErrorKind::Unsupported, DispatchStage::Instance, "apiVersion" });
    caps.surface =
        HasExtension(info.enabledExtensionCount, info.ppEnabledExtensionNames, "VK_KHR_surface");
    caps.debug_utils = HasExtension(
        info.enabledExtensionCount, info.ppEnabledExtensionNames, "VK_EXT_debug_utils");
    caps.portability_enumeration = HasExtension(
        info.enabledExtensionCount, info.ppEnabledExtensionNames, "VK_KHR_portability_enumeration");
    return Ok(caps);
}
auto ParseDeviceCapabilities(const VkDeviceCreateInfo& info, const InstanceCapabilities& parent,
                             rstd::uint32_t physical_api_version)
    -> Result<DeviceCapabilities, DispatchError> {
    if (info.sType != VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO ||
        (info.enabledExtensionCount && ! info.ppEnabledExtensionNames))
        return Err(DispatchError { DispatchErrorKind::InvalidInput, DispatchStage::Device });
    DeviceCapabilities caps;
    caps.api_version =
        parent.api_version < physical_api_version ? parent.api_version : physical_api_version;
    if (VK_API_VERSION_VARIANT(caps.api_version) != 0 || caps.api_version < VK_API_VERSION_1_1)
        return Err(
            DispatchError { DispatchErrorKind::Unsupported, DispatchStage::Device, "apiVersion" });
    auto enabled = [&](const char* name) {
        return HasExtension(info.enabledExtensionCount, info.ppEnabledExtensionNames, name);
    };
    caps.debug_utils = parent.debug_utils;
    caps.swapchain   = enabled("VK_KHR_swapchain");
    if (caps.swapchain && ! parent.surface)
        return Err(DispatchError {
            DispatchErrorKind::Unsupported, DispatchStage::Device, "VK_KHR_surface" });
    caps.image_format_list =
        caps.api_version >= VK_API_VERSION_1_2 || enabled("VK_KHR_image_format_list");
    caps.buffer_device_address_extension = enabled("VK_KHR_buffer_device_address");
    bool address_features_seen           = false;
    caps.memory_budget                   = enabled("VK_EXT_memory_budget");
    caps.timeline_extension              = enabled("VK_KHR_timeline_semaphore");
    caps.synchronization2_extension      = enabled("VK_KHR_synchronization2");
    caps.push_descriptor                 = enabled("VK_KHR_push_descriptor");
    caps.external_memory_fd              = enabled("VK_KHR_external_memory_fd");
    caps.external_semaphore_fd           = enabled("VK_KHR_external_semaphore_fd");
    caps.drm_format_modifier             = enabled("VK_EXT_image_drm_format_modifier");
    caps.pipeline_executable_extension   = enabled("VK_KHR_pipeline_executable_properties");
    for (auto* next = static_cast<const VkBaseInStructure*>(info.pNext); next; next = next->pNext) {
        switch (next->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
            caps.timeline_semaphore =
                reinterpret_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(next)
                    ->timelineSemaphore;
            break;
        case VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO:
            caps.physical_device_count =
                reinterpret_cast<const VkDeviceGroupDeviceCreateInfo*>(next)->physicalDeviceCount;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:
            if (address_features_seen)
                return Err(DispatchError { DispatchErrorKind::InvalidInput,
                                           DispatchStage::Device,
                                           "bufferDeviceAddress" });
            address_features_seen = true;
            caps.buffer_device_address =
                reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(next)
                    ->bufferDeviceAddress;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
            if (address_features_seen)
                return Err(DispatchError { DispatchErrorKind::InvalidInput,
                                           DispatchStage::Device,
                                           "bufferDeviceAddress" });
            address_features_seen = true;
            caps.buffer_device_address =
                reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(next)
                    ->bufferDeviceAddress;
            caps.timeline_semaphore =
                reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(next)->timelineSemaphore;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES:
            caps.synchronization2 =
                reinterpret_cast<const VkPhysicalDeviceSynchronization2Features*>(next)
                    ->synchronization2;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
            caps.synchronization2 =
                reinterpret_cast<const VkPhysicalDeviceVulkan13Features*>(next)->synchronization2;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR:
            caps.pipeline_executable =
                reinterpret_cast<const VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR*>(
                    next)
                    ->pipelineExecutableInfo;
            break;
        default: break;
        }
    }
    if (caps.buffer_device_address &&
        ((caps.api_version < VK_API_VERSION_1_2 && ! caps.buffer_device_address_extension) ||
         caps.physical_device_count != 1 || enabled("VK_EXT_buffer_device_address")))
        return Err(DispatchError {
            DispatchErrorKind::Unsupported, DispatchStage::Device, "bufferDeviceAddress" });
    if (caps.timeline_semaphore && caps.api_version < VK_API_VERSION_1_2 &&
        ! caps.timeline_extension)
        return Err(DispatchError {
            DispatchErrorKind::Unsupported, DispatchStage::Device, "timelineSemaphore" });
    if (caps.synchronization2 && caps.api_version < VK_API_VERSION_1_3 &&
        ! caps.synchronization2_extension)
        return Err(DispatchError {
            DispatchErrorKind::Unsupported, DispatchStage::Device, "synchronization2" });
    if (caps.pipeline_executable && ! caps.pipeline_executable_extension)
        return Err(DispatchError {
            DispatchErrorKind::Unsupported, DispatchStage::Device, "pipelineExecutableInfo" });
    return Ok(caps);
}

auto LoadGlobal(PFN_vkGetInstanceProcAddr resolver) -> Result<GlobalDispatch, DispatchError> {
    if (! resolver)
        return Err(DispatchError {
            DispatchErrorKind::InvalidInput, DispatchStage::Global, "vkGetInstanceProcAddr" });
    GlobalDispatch result;
    result.vkGetInstanceProcAddr = resolver;
#define LOAD(name)                                                               \
    result.name = reinterpret_cast<PFN_##name>(resolver(VK_NULL_HANDLE, #name)); \
    if (! result.name) return Err(MissingDispatch(DispatchStage::Global, #name))
    LOAD(vkCreateInstance);
    LOAD(vkEnumerateInstanceExtensionProperties);
    LOAD(vkEnumerateInstanceLayerProperties);
#undef LOAD
    result.vkEnumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        resolver(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    return Ok(result);
}
auto LoadInstance(const GlobalDispatch& global, VkInstance instance, InstanceCapabilities caps)
    -> Result<InstanceDispatch, DispatchError> {
    if (! global.vkGetInstanceProcAddr || ! instance)
        return Err(DispatchError { DispatchErrorKind::InvalidInput, DispatchStage::Instance });
    if (VK_API_VERSION_VARIANT(caps.api_version) != 0 || caps.api_version < VK_API_VERSION_1_1)
        return Err(DispatchError {
            DispatchErrorKind::Unsupported, DispatchStage::Instance, "apiVersion" });
    InstanceDispatch result;
    result.instance     = instance;
    result.resolver     = global.vkGetInstanceProcAddr;
    result.capabilities = caps;
#define LOAD(name)                                                                             \
    result.name = reinterpret_cast<PFN_##name>(global.vkGetInstanceProcAddr(instance, #name)); \
    if (! result.name) return Err(MissingDispatch(DispatchStage::Instance, #name))
    LOAD(vkDestroyInstance);
    if (caps.debug_utils) {
        LOAD(vkCreateDebugUtilsMessengerEXT);
    }
    LOAD(vkCreateDevice);
    if (caps.debug_utils) {
        LOAD(vkDestroyDebugUtilsMessengerEXT);
    }
    if (caps.surface) {
        LOAD(vkDestroySurfaceKHR);
    }
    LOAD(vkEnumerateDeviceExtensionProperties);
    LOAD(vkEnumeratePhysicalDevices);
    LOAD(vkGetDeviceProcAddr);
    LOAD(vkGetPhysicalDeviceFeatures2);
    LOAD(vkGetPhysicalDeviceFormatProperties);
    LOAD(vkGetPhysicalDeviceMemoryProperties);
    LOAD(vkGetPhysicalDeviceMemoryProperties2);
    LOAD(vkGetPhysicalDeviceProperties);
    LOAD(vkGetPhysicalDeviceProperties2);
    LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD(vkGetPhysicalDeviceQueueFamilyProperties2);
    if (caps.surface) {
        LOAD(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    }
    if (caps.surface) {
        LOAD(vkGetPhysicalDeviceSurfaceFormatsKHR);
    }
    if (caps.surface) {
        LOAD(vkGetPhysicalDeviceSurfacePresentModesKHR);
    }
    if (caps.surface) {
        LOAD(vkGetPhysicalDeviceSurfaceSupportKHR);
    }
    LOAD(vkGetPhysicalDeviceImageFormatProperties);
#undef LOAD
    return Ok(result);
}
auto LoadDevice(const InstanceDispatch& parent, VkDevice device, DeviceCapabilities caps)
    -> Result<DeviceDispatch, DispatchError> {
    if (! parent.vkGetDeviceProcAddr || ! parent.instance || ! device)
        return Err(DispatchError { DispatchErrorKind::InvalidInput, DispatchStage::Device });
    if (VK_API_VERSION_VARIANT(caps.api_version) != 0 || caps.api_version < VK_API_VERSION_1_1 ||
        caps.api_version > parent.capabilities.api_version ||
        (caps.buffer_device_address &&
         ((caps.api_version < VK_API_VERSION_1_2 && ! caps.buffer_device_address_extension) ||
          caps.physical_device_count != 1)) ||
        (caps.timeline_semaphore && caps.api_version < VK_API_VERSION_1_2 &&
         ! caps.timeline_extension) ||
        (caps.synchronization2 && caps.api_version < VK_API_VERSION_1_3 &&
         ! caps.synchronization2_extension) ||
        (caps.pipeline_executable && ! caps.pipeline_executable_extension) ||
        (caps.swapchain && ! parent.capabilities.surface) ||
        caps.debug_utils != parent.capabilities.debug_utils)
        return Err(DispatchError {
            DispatchErrorKind::Unsupported, DispatchStage::Device, "capabilities" });
    DeviceDispatch result;
    result.device              = device;
    result.instance            = parent.instance;
    result.capabilities        = caps;
    result.vkGetDeviceProcAddr = parent.vkGetDeviceProcAddr;
#define LOAD_AS(field, name)                                                                \
    result.field =                                                                          \
        reinterpret_cast<decltype(result.field)>(parent.vkGetDeviceProcAddr(device, name)); \
    if (! result.field) return Err(MissingDispatch(DispatchStage::Device, name))
#define LOAD(name) LOAD_AS(name, #name)
    LOAD(vkDestroyDevice);
    if (caps.swapchain) {
        LOAD(vkAcquireNextImageKHR);
    }
    LOAD(vkAllocateCommandBuffers);
    LOAD(vkAllocateDescriptorSets);
    LOAD(vkAllocateMemory);
    LOAD(vkBeginCommandBuffer);
    LOAD(vkBindBufferMemory);
    LOAD(vkBindImageMemory);
    LOAD(vkBindImageMemory2);
    LOAD(vkBindBufferMemory2);
    LOAD(vkGetBufferMemoryRequirements);
    if (caps.debug_utils) {
        LOAD(vkCmdBeginDebugUtilsLabelEXT);
    }
    LOAD(vkCmdBeginQuery);
    LOAD(vkCmdBeginRenderPass);
    LOAD(vkCmdBindDescriptorSets);
    LOAD(vkCmdBindIndexBuffer);
    LOAD(vkCmdBindPipeline);
    LOAD(vkCmdBindVertexBuffers);
    LOAD(vkCmdBlitImage);
    LOAD(vkCmdClearColorImage);
    LOAD(vkCmdClearAttachments);
    LOAD(vkCmdCopyBuffer);
    LOAD(vkCmdCopyBufferToImage);
    LOAD(vkCmdCopyImage);
    LOAD(vkCmdCopyImageToBuffer);
    LOAD(vkCmdDispatch);
    LOAD(vkCmdDraw);
    LOAD(vkCmdDrawIndexed);
    if (caps.debug_utils) {
        LOAD(vkCmdEndDebugUtilsLabelEXT);
    }
    LOAD(vkCmdEndQuery);
    LOAD(vkCmdEndRenderPass);
    LOAD(vkCmdFillBuffer);
    LOAD(vkCmdPipelineBarrier);
    if (caps.synchronization2) {
        LOAD_AS(vkCmdPipelineBarrier2,
                caps.api_version >= VK_API_VERSION_1_3 ? "vkCmdPipelineBarrier2"
                                                       : "vkCmdPipelineBarrier2KHR");
    }
    LOAD(vkCmdPushConstants);
    if (caps.push_descriptor) {
        LOAD(vkCmdPushDescriptorSetKHR);
    }
    if (caps.push_descriptor) {
        LOAD(vkCmdPushDescriptorSetWithTemplateKHR);
    }
    LOAD(vkCmdResolveImage);
    LOAD(vkCmdSetBlendConstants);
    LOAD(vkCmdSetDepthBias);
    LOAD(vkCmdSetDepthBounds);
    LOAD(vkCmdSetEvent);
    LOAD(vkCmdSetLineWidth);
    LOAD(vkCmdSetScissor);
    LOAD(vkCmdSetStencilCompareMask);
    LOAD(vkCmdSetStencilReference);
    LOAD(vkCmdSetStencilWriteMask);
    LOAD(vkCmdSetViewport);
    LOAD(vkCmdWaitEvents);
    LOAD(vkCreateBuffer);
    LOAD(vkCreateBufferView);
    LOAD(vkCreateCommandPool);
    LOAD(vkCreateComputePipelines);
    LOAD(vkCreateDescriptorPool);
    LOAD(vkCreateDescriptorSetLayout);
    LOAD_AS(vkCreateDescriptorUpdateTemplateKHR, "vkCreateDescriptorUpdateTemplate");
    LOAD(vkCreateEvent);
    LOAD(vkCreateFence);
    LOAD(vkCreateFramebuffer);
    LOAD(vkCreateGraphicsPipelines);
    LOAD(vkCreateImage);
    LOAD(vkCreateImageView);
    LOAD(vkCreatePipelineLayout);
    LOAD(vkCreateQueryPool);
    LOAD(vkCreateRenderPass);
    LOAD(vkCreateSampler);
    LOAD(vkCreateSemaphore);
    LOAD(vkCreateShaderModule);
    if (caps.swapchain) {
        LOAD(vkCreateSwapchainKHR);
    }
    LOAD(vkDestroyBuffer);
    LOAD(vkDestroyBufferView);
    LOAD(vkDestroyCommandPool);
    LOAD(vkDestroyDescriptorPool);
    LOAD(vkDestroyDescriptorSetLayout);
    LOAD_AS(vkDestroyDescriptorUpdateTemplateKHR, "vkDestroyDescriptorUpdateTemplate");
    LOAD(vkDestroyEvent);
    LOAD(vkDestroyFence);
    LOAD(vkDestroyFramebuffer);
    LOAD(vkDestroyImage);
    LOAD(vkDestroyImageView);
    LOAD(vkDestroyPipeline);
    LOAD(vkDestroyPipelineLayout);
    LOAD(vkDestroyQueryPool);
    LOAD(vkDestroyRenderPass);
    LOAD(vkDestroySampler);
    LOAD(vkDestroySemaphore);
    LOAD(vkDestroyShaderModule);
    if (caps.swapchain) {
        LOAD(vkDestroySwapchainKHR);
    }
    LOAD(vkDeviceWaitIdle);
    LOAD(vkEndCommandBuffer);
    LOAD(vkFreeCommandBuffers);
    LOAD(vkFreeDescriptorSets);
    LOAD(vkFreeMemory);
    LOAD(vkGetBufferMemoryRequirements2);
    LOAD(vkGetDeviceQueue);
    LOAD(vkGetEventStatus);
    LOAD(vkGetFenceStatus);
    LOAD(vkGetImageMemoryRequirements);
    LOAD(vkGetImageMemoryRequirements2);
    LOAD(vkGetImageSubresourceLayout);
    if (caps.external_memory_fd) {
        LOAD(vkGetMemoryFdKHR);
    }
    if (caps.external_memory_fd) {
        LOAD(vkGetMemoryFdPropertiesKHR);
    }
    if (caps.external_semaphore_fd) {
        LOAD(vkGetSemaphoreFdKHR);
    }
    if (caps.drm_format_modifier) {
        LOAD(vkGetImageDrmFormatModifierPropertiesEXT);
    }
    if (caps.pipeline_executable) {
        LOAD(vkGetPipelineExecutablePropertiesKHR);
    }
    if (caps.pipeline_executable) {
        LOAD(vkGetPipelineExecutableStatisticsKHR);
    }
    if (caps.buffer_device_address) {
        LOAD_AS(vkGetBufferDeviceAddress,
                caps.api_version >= VK_API_VERSION_1_2 ? "vkGetBufferDeviceAddress"
                                                       : "vkGetBufferDeviceAddressKHR");
    }
    LOAD(vkGetQueryPoolResults);
    if (caps.timeline_semaphore) {
        LOAD_AS(vkGetSemaphoreCounterValueKHR,
                caps.api_version >= VK_API_VERSION_1_2 ? "vkGetSemaphoreCounterValue"
                                                       : "vkGetSemaphoreCounterValueKHR");
    }
    LOAD(vkFlushMappedMemoryRanges);
    LOAD(vkInvalidateMappedMemoryRanges);
    LOAD(vkMapMemory);
    LOAD(vkQueueSubmit);
    LOAD(vkResetFences);
    LOAD(vkResetCommandBuffer);
    LOAD(vkUnmapMemory);
    LOAD_AS(vkUpdateDescriptorSetWithTemplateKHR, "vkUpdateDescriptorSetWithTemplate");
    LOAD(vkUpdateDescriptorSets);
    LOAD(vkWaitForFences);
    if (caps.timeline_semaphore) {
        LOAD_AS(vkWaitSemaphoresKHR,
                caps.api_version >= VK_API_VERSION_1_2 ? "vkWaitSemaphores"
                                                       : "vkWaitSemaphoresKHR");
    }
    if (caps.debug_utils) {
        LOAD(vkSetDebugUtilsObjectNameEXT);
    }
    if (caps.debug_utils) {
        LOAD(vkSetDebugUtilsObjectTagEXT);
    }
    if (caps.swapchain) {
        LOAD(vkGetSwapchainImagesKHR);
    }
    if (caps.swapchain) {
        LOAD(vkQueuePresentKHR);
    }
#undef LOAD
#undef LOAD_AS
    return Ok(result);
}
} // namespace vvk
