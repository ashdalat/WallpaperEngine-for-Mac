#include <vulkan/vk_platform.h>
#include <rstd/test/gtest.hpp>
#include "shaders/address.hpp"
import rstd;
import vvk;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{
struct VulkanMemoryTest {
    Option<vvk::VulkanLoader>  loader;
    const vvk::GlobalDispatch* global {};
    vvk::InstanceDispatch      instance_dispatch;
    vvk::DeviceDispatch        device_dispatch;
    vvk::Instance              instance_owner;
    vvk::Device                device_owner;

    VkInstance                 instance {};
    VkPhysicalDevice           gpu {};
    VkDevice                   device {};
    VkQueue                    queue {};
    VkCommandPool              pool {};
    VkDebugUtilsMessengerEXT   messenger {};
    rstd::uint32_t             family {}, errors {};
    VkPhysicalDeviceProperties properties {};
    bool                       validation {};
    bool                       unavailable {};
    ~VulkanMemoryTest() {
        if (device) {
            device_dispatch.vkDeviceWaitIdle(device);
            if (pool) device_dispatch.vkDestroyCommandPool(device, pool, nullptr);
            device_owner.reset();
        }
        if (messenger)
            instance_dispatch.vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
        instance_owner.reset();
    }
    static VKAPI_ATTR VkBool32 VKAPI_CALL Debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                VkDebugUtilsMessageTypeFlagsEXT,
                                                const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                void*                                       user) {
        auto* self = static_cast<VulkanMemoryTest*>(user);
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            ++self->errors;
            rstd::io::eprint("Vulkan validation: {}\n", data->pMessage);
        }
        return VK_FALSE;
    }
    bool initialize(unsigned api = VK_API_VERSION_1_1, bool format_list_extension = false,
                    bool address = false) {
        auto opened = vvk::VulkanLoader::Open();
        if (opened.is_err()) {
            auto error  = opened.unwrap_err_unchecked();
            unavailable = error.kind == vvk::LoaderErrorKind::OpenLibrary;
            rstd::io::eprint("vvk loader failed: kind={}\n", unsigned(error.kind));
            return false;
        }
        loader                 = Some(rstd::move(opened).unwrap_unchecked());
        global                 = &loader->global();
        unsigned supported_api = VK_API_VERSION_1_0;
        if (global->vkEnumerateInstanceVersion &&
            global->vkEnumerateInstanceVersion(&supported_api) != VK_SUCCESS)
            return false;
        if (supported_api < api) {
            unavailable = true;
            return false;
        }
        rstd::uint32_t count = 0;
        if (global->vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) return false;
        auto layers = alloc::vec::Vec<VkLayerProperties>::with_capacity(usize(count));
        for (unsigned i = 0; i < count; ++i) layers.push(VkLayerProperties {});
        if (global->vkEnumerateInstanceLayerProperties(&count, layers.as_mut_ptr().as_raw_ptr()) !=
            VK_SUCCESS)
            return false;
        for (const auto& layer : layers)
            if ((rstd::ffi::CStr::from_ptr(layer.layerName).to_bytes() ==
                 rstd::ffi::CStr::from_ptr("VK_LAYER_KHRONOS_validation").to_bytes()))
                validation = true;
        const char*       layer_name = "VK_LAYER_KHRONOS_validation";
        const char*       extension  = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkApplicationInfo application {
            VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "vvk-memory-tests", 1, nullptr, 0, api
        };
        VkInstanceCreateInfo info { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        info.pApplicationInfo = &application;
        if (validation) {
            info.enabledLayerCount       = 1;
            info.ppEnabledLayerNames     = &layer_name;
            info.enabledExtensionCount   = 1;
            info.ppEnabledExtensionNames = &extension;
        }
        auto instance_result =
            vvk::Instance::Create(instance_owner, *global, info, instance_dispatch);
        if (instance_result.is_err()) {
            auto error  = instance_result.unwrap_err_unchecked();
            unavailable = error.kind == vvk::DispatchErrorKind::Vulkan &&
                          error.api_result == VK_ERROR_INCOMPATIBLE_DRIVER;
            rstd::io::eprint("vvk instance failed: command={}, result={}\n",
                             error.command ? error.command : "configuration",
                             error.api_result);
            return false;
        }
        instance = *instance_owner;
        if (validation) {
            VkDebugUtilsMessengerCreateInfoEXT debug {
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT
            };
            debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug.pfnUserCallback = Debug;
            debug.pUserData       = this;
            if (instance_dispatch.vkCreateDebugUtilsMessengerEXT(
                    instance, &debug, nullptr, &messenger) != VK_SUCCESS)
                return false;
        }
        count = 0;
        if (instance_dispatch.vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS)
            return false;
        if (count == 0) {
            unavailable = true;
            return false;
        }
        auto devices = alloc::vec::Vec<VkPhysicalDevice>::with_capacity(usize(count));
        for (unsigned i = 0; i < count; ++i) devices.push(VkPhysicalDevice {});
        if (instance_dispatch.vkEnumeratePhysicalDevices(
                instance, &count, devices.as_mut_ptr().as_raw_ptr()) != VK_SUCCESS)
            return false;
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties candidate_properties;
            instance_dispatch.vkGetPhysicalDeviceProperties(candidate, &candidate_properties);
            if (candidate_properties.apiVersion < api) continue;
            unsigned families = 0;
            instance_dispatch.vkGetPhysicalDeviceQueueFamilyProperties(
                candidate, &families, nullptr);
            auto queues = alloc::vec::Vec<VkQueueFamilyProperties>::with_capacity(usize(families));
            for (unsigned i = 0; i < families; ++i) queues.push(VkQueueFamilyProperties {});
            instance_dispatch.vkGetPhysicalDeviceQueueFamilyProperties(
                candidate, &families, queues.as_mut_ptr().as_raw_ptr());
            for (unsigned i = 0; i < families; ++i)
                if ((queues[usize(i)].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                    (! address || (queues[usize(i)].queueFlags & VK_QUEUE_COMPUTE_BIT))) {
                    gpu        = candidate;
                    family     = i;
                    properties = candidate_properties;
                    break;
                }
            if (gpu) break;
        }
        if (! gpu) {
            unavailable = true;
            return false;
        }
        float                   priority = 1;
        VkDeviceQueueCreateInfo queue_info {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, family, 1, &priority
        };
        VkDeviceCreateInfo device_info { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos    = &queue_info;
        const char* enabled_extensions[2] {};
        unsigned    enabled_count    = 0;
        auto        enable_extension = [&](const char* name) {
            unsigned count = 0;
            if (instance_dispatch.vkEnumerateDeviceExtensionProperties(
                    gpu, nullptr, &count, nullptr) != VK_SUCCESS)
                return false;
            auto extensions = alloc::vec::Vec<VkExtensionProperties>::with_capacity(usize(count));
            for (unsigned i = 0; i < count; ++i) extensions.push(VkExtensionProperties {});
            if (instance_dispatch.vkEnumerateDeviceExtensionProperties(
                    gpu, nullptr, &count, extensions.as_mut_ptr().as_raw_ptr()) != VK_SUCCESS)
                return false;
            for (const auto& extension : extensions) {
                if ((rstd::ffi::CStr::from_ptr(extension.extensionName).to_bytes() ==
                     rstd::ffi::CStr::from_ptr(name).to_bytes())) {
                    enabled_extensions[enabled_count++] = name;
                    return true;
                }
            }
            unavailable = true;
            return false;
        };
        if (format_list_extension && ! enable_extension(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME))
            return false;
        if (address && api < VK_API_VERSION_1_2 &&
            ! enable_extension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
            return false;
        device_info.enabledExtensionCount   = enabled_count;
        device_info.ppEnabledExtensionNames = enabled_extensions;
        VkPhysicalDeviceBufferDeviceAddressFeatures address_features {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES
        };
        if (address) {
            VkPhysicalDeviceFeatures2 supported { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                                  &address_features };
            instance_dispatch.vkGetPhysicalDeviceFeatures2(gpu, &supported);
            if (! address_features.bufferDeviceAddress) {
                unavailable = true;
                return false;
            }
            address_features.bufferDeviceAddressCaptureReplay = VK_FALSE;
            address_features.bufferDeviceAddressMultiDevice   = VK_FALSE;
            device_info.pNext                                 = &address_features;
        }
        auto created =
            vvk::Device::Create(device_owner, gpu, instance_dispatch, device_info, device_dispatch);
        if (created.is_err()) {
            auto error = created.unwrap_err_unchecked();
            rstd::io::eprint("vvk device failed: command={}, result={}\n",
                             error.command ? error.command : "configuration",
                             error.api_result);
            return false;
        }
        device = *device_owner;
        device_dispatch.vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo pool_info { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                            nullptr,
                                            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                            family };
        if (device_dispatch.vkCreateCommandPool(device, &pool_info, nullptr, &pool) != VK_SUCCESS)
            return false;
        rstd::io::print("vvk device: {}; validation={}; type={}\n",
                        properties.deviceName,
                        validation ? "enabled" : "unavailable",
                        unsigned(properties.deviceType));
        return true;
    }
    VkCommandBuffer begin() {
        VkCommandBuffer             command {};
        VkCommandBufferAllocateInfo info { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           nullptr,
                                           pool,
                                           VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                           1 };
        if (device_dispatch.vkAllocateCommandBuffers(device, &info, &command) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        VkCommandBufferBeginInfo begin_info { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                              nullptr,
                                              VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        if (device_dispatch.vkBeginCommandBuffer(command, &begin_info) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        return command;
    }
    VkFence submit(VkCommandBuffer command) {
        if (device_dispatch.vkEndCommandBuffer(command) != VK_SUCCESS) return VK_NULL_HANDLE;
        VkFence           fence {};
        VkFenceCreateInfo info { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        if (device_dispatch.vkCreateFence(device, &info, nullptr, &fence) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        VkSubmitInfo submit_info { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers    = &command;
        if (device_dispatch.vkQueueSubmit(queue, 1, &submit_info, fence) != VK_SUCCESS) {
            device_dispatch.vkDestroyFence(device, fence, nullptr);
            return VK_NULL_HANDLE;
        }
        return fence;
    }
    bool wait(VkFence fence) {
        return device_dispatch.vkWaitForFences(device, 1, &fence, VK_TRUE, 10'000'000'000ULL) ==
               VK_SUCCESS;
    }
};
VkBufferCreateInfo BufferCreate(VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo info { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size  = size;
    info.usage = usage;
    return info;
}
void TransferBarrier(const vvk::DeviceDispatch& dispatch, VkCommandBuffer command, VkBuffer buffer,
                     VkAccessFlags destination, VkPipelineStageFlags stage) {
    VkBufferMemoryBarrier barrier { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask       = destination;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer                                            = buffer;
    barrier.size                                              = VK_WHOLE_SIZE;
    dispatch.vkCmdPipelineBarrier(
        command, VK_PIPELINE_STAGE_TRANSFER_BIT, stage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}
} // namespace

TEST(MemoryVulkan, UploadSlicesAndSubmissionLifetime) {
    VulkanMemoryTest context;
    const bool       initialized = context.initialize();
    if (context.unavailable)
        GTEST_SKIP() << "No Vulkan loader, ICD, or Vulkan 1.1 graphics device available";
    ASSERT_TRUE(initialized) << "Vulkan test context creation failed";
    {
        auto allocator_result = vvk::MemoryAllocator::Create(
            context.gpu, context.instance_dispatch, context.device_dispatch);
        ASSERT_TRUE(allocator_result.is_ok());
        auto allocator = allocator_result.unwrap_unchecked();
        auto host      = vvk::MemoryRequest { .required           = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                              .preferred          = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                              .persistent_mapping = true };
        auto stage_result =
            allocator.create_buffer(BufferCreate(4096, VK_BUFFER_USAGE_TRANSFER_SRC_BIT), host);
        ASSERT_TRUE(stage_result.is_ok());
        auto read_result =
            allocator.create_buffer(BufferCreate(4096, VK_BUFFER_USAGE_TRANSFER_DST_BIT), host);
        ASSERT_TRUE(read_result.is_ok());
        auto gpu_result = allocator.create_buffer(BufferCreate(
            4096, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT));
        ASSERT_TRUE(gpu_result.is_ok());
        auto stage = stage_result.unwrap_unchecked(), readback = read_result.unwrap_unchecked(),
             destination  = gpu_result.unwrap_unchecked();
        auto stage_memory = stage.allocation(), read_memory = readback.allocation();
        auto write_result = stage_memory.map();
        ASSERT_TRUE(write_result.is_ok());
        auto write = write_result.unwrap_unchecked();
        rstd::mem::memset(write.data(), u8(0), usize(4096));
        alloc::RangeAllocator ranges(4096);
        const VkDeviceSize    atom          = context.properties.limits.nonCoherentAtomSize;
        const VkDeviceSize    alignment     = atom > 256 ? atom : 256;
        auto                  first_result  = ranges.allocate(256, alignment),
                              second_result = ranges.allocate(256, alignment);
        ASSERT_TRUE(first_result.is_ok());
        ASSERT_TRUE(second_result.is_ok());
        auto  first = first_result.unwrap_unchecked(), second = second_result.unwrap_unchecked();
        auto* bytes = static_cast<unsigned char*>(write.data());
        for (unsigned i = 0; i < 256; ++i) {
            bytes[first.offset + i]  = static_cast<unsigned char>(i);
            bytes[second.offset + i] = static_cast<unsigned char>(255 - i);
        }
        ASSERT_TRUE(stage_memory.flush().is_ok());
        VkFence         fences[2] {};
        VkCommandBuffer commands[2] {};
        auto            upload_lease       = stage.clone();
        auto            destination_lease  = destination.clone();
        const VkBuffer  destination_handle = destination.handle();
        for (unsigned batch = 0; batch < 2; ++batch) {
            auto slice      = batch == 0 ? first : second;
            commands[batch] = context.begin();
            ASSERT_NE(commands[batch], VK_NULL_HANDLE);
            VkBufferCopy copy { slice.offset, slice.offset, slice.size };
            context.device_dispatch.vkCmdCopyBuffer(
                commands[batch], stage.handle(), destination.handle(), 1, &copy);
            TransferBarrier(context.device_dispatch,
                            commands[batch],
                            destination.handle(),
                            VK_ACCESS_TRANSFER_READ_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT);
            context.device_dispatch.vkCmdCopyBuffer(
                commands[batch], destination.handle(), readback.handle(), 1, &copy);
            TransferBarrier(context.device_dispatch,
                            commands[batch],
                            readback.handle(),
                            VK_ACCESS_HOST_READ_BIT,
                            VK_PIPELINE_STAGE_HOST_BIT);
            fences[batch] = context.submit(commands[batch]);
            ASSERT_NE(fences[batch], VK_NULL_HANDLE);
        }
        destination.reset();
        stage.reset();
        allocator.trim();
        EXPECT_EQ(destination_lease.handle(), destination_handle);
        ASSERT_TRUE(context.wait(fences[1]));
        ASSERT_TRUE(context.wait(fences[0]));
        upload_lease.reset();
        destination_lease.reset();
        auto read_map_result = read_memory.map();
        ASSERT_TRUE(read_map_result.is_ok());
        auto read_map = read_map_result.unwrap_unchecked();
        ASSERT_TRUE(read_memory.invalidate().is_ok());
        const auto* actual = static_cast<const unsigned char*>(read_map.data());
        EXPECT_EQ(rstd::mem::memcmp(actual + first.offset, bytes + first.offset, usize(256)), 0);
        EXPECT_EQ(rstd::mem::memcmp(actual + second.offset, bytes + second.offset, usize(256)), 0);
        ASSERT_TRUE(ranges.deallocate(first.id).is_ok());
        auto reused = ranges.allocate(256, alignment).unwrap_unchecked();
        EXPECT_EQ(reused.offset, first.offset);
        for (auto fence : fences)
            context.device_dispatch.vkDestroyFence(context.device, fence, nullptr);
        context.device_dispatch.vkFreeCommandBuffers(context.device, context.pool, 2, commands);
    }
    EXPECT_EQ(context.errors, 0u);
}

namespace
{
void CheckImageTransfer(VkImageCreateFlags flags, bool format_list = false,
                        unsigned api = VK_API_VERSION_1_1, vvk::MemoryBlockPolicy policy = {},
                        bool access_intent = false) {
    VulkanMemoryTest context;
    const bool       initialized = context.initialize(api, format_list && api < VK_API_VERSION_1_2);
    if (context.unavailable)
        GTEST_SKIP()
            << "Requested Vulkan API, graphics device or image format list extension unavailable";
    ASSERT_TRUE(initialized) << "Vulkan test context creation failed";
    {
        auto allocator_result = vvk::MemoryAllocator::Create(
            context.gpu, context.instance_dispatch, context.device_dispatch, policy);
        ASSERT_TRUE(allocator_result.is_ok());
        auto              allocator = allocator_result.unwrap_unchecked();
        VkImageCreateInfo image_info { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format    = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.extent    = { 16, 16, 1 };
        image_info.mipLevels = image_info.arrayLayers = 1;
        image_info.flags                              = flags;
        if (flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) image_info.arrayLayers = 6;
        const auto                  layers      = image_info.arrayLayers;
        const auto                  bytes_count = 1024 * layers;
        const VkFormat              formats[] { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB };
        VkImageFormatListCreateInfo list {
            VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO, nullptr, 2, formats
        };
        if (format_list) image_info.pNext = &list;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling  = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage   = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                             VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImageFormatProperties supported_image {};
        auto                    supported_result =
            context.instance_dispatch.vkGetPhysicalDeviceImageFormatProperties(context.gpu,
                                                                               image_info.format,
                                                                               image_info.imageType,
                                                                               image_info.tiling,
                                                                               image_info.usage,
                                                                               flags,
                                                                               &supported_image);
        if (supported_result == VK_ERROR_FORMAT_NOT_SUPPORTED)
            GTEST_SKIP() << "Requested image format/flags unsupported";
        ASSERT_EQ(supported_result, VK_SUCCESS);
        auto image_result = allocator.create_image(image_info);
        ASSERT_TRUE(image_result.is_ok());
        auto image = image_result.unwrap_unchecked();
        struct Views {
            VulkanMemoryTest& context;
            VkImageView       handles[2] {};
            ~Views() {
                for (auto view : handles)
                    if (view)
                        context.device_dispatch.vkDestroyImageView(context.device, view, nullptr);
            }
        } views { context };
        if (flags) {
            VkImageViewCreateInfo view { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            view.image            = image.handle();
            view.viewType         = layers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
            view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers };
            const unsigned count  = (flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) ? 2 : 1;
            for (unsigned i = 0; i < count; ++i) {
                view.format = formats[i];
                ASSERT_EQ(context.device_dispatch.vkCreateImageView(
                              context.device, &view, nullptr, &views.handles[i]),
                          VK_SUCCESS);
            }
        }
        auto host = vvk::MemoryRequest { .required  = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                         .preferred = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT };
        auto stage_result = allocator.create_buffer(
                 BufferCreate(bytes_count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
                 access_intent ? vvk::MemoryRequest::Upload() : host),
             read_result = allocator.create_buffer(
                 BufferCreate(bytes_count, VK_BUFFER_USAGE_TRANSFER_DST_BIT),
                 access_intent ? vvk::MemoryRequest::Readback() : host);
        ASSERT_TRUE(stage_result.is_ok());
        ASSERT_TRUE(read_result.is_ok());
        auto stage = stage_result.unwrap_unchecked(), readback = read_result.unwrap_unchecked();
        auto stage_memory = stage.allocation(), read_memory = readback.allocation();
        if (access_intent) {
            const auto upload_info = stage_memory.info(), readback_info = read_memory.info();
            EXPECT_TRUE(upload_info.properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            EXPECT_TRUE(readback_info.properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            EXPECT_EQ(upload_info.selection.placement_reason,
                      vvk::MemoryPlacementReason::AutomaticHost);
            EXPECT_EQ(readback_info.selection.placement_reason,
                      vvk::MemoryPlacementReason::AutomaticHost);
            rstd::io::print("vvk intent: upload type={} heap={} flags={:x}; readback type={} "
                            "heap={} flags={:x}\n",
                            upload_info.memory_type,
                            upload_info.heap,
                            upload_info.properties,
                            readback_info.memory_type,
                            readback_info.heap,
                            readback_info.properties);
        }
        auto write_result = stage_memory.map();
        ASSERT_TRUE(write_result.is_ok());
        auto  write = write_result.unwrap_unchecked();
        auto* bytes = static_cast<unsigned char*>(write.data());
        for (unsigned i = 0; i < bytes_count; ++i)
            bytes[i] = static_cast<unsigned char>(i * 17U + i / 1024);
        ASSERT_TRUE(stage_memory.flush().is_ok());
        struct Submission {
            VulkanMemoryTest& context;
            VkCommandBuffer   command {};
            VkFence           fence {};
            bool              completed {};
            ~Submission() {
                if (fence) {
                    if (! completed && ! context.wait(fence))
                        context.device_dispatch.vkDeviceWaitIdle(context.device);
                    context.device_dispatch.vkDestroyFence(context.device, fence, nullptr);
                }
                if (command)
                    context.device_dispatch.vkFreeCommandBuffers(
                        context.device, context.pool, 1, &command);
            }
        } submission { context };
        auto command = submission.command = context.begin();
        ASSERT_NE(command, VK_NULL_HANDLE);
        VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                                             = image.handle();
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers };
        context.device_dispatch.vkCmdPipelineBarrier(command,
                                                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                     0,
                                                     0,
                                                     nullptr,
                                                     0,
                                                     nullptr,
                                                     1,
                                                     &barrier);
        VkBufferImageCopy copy {};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers };
        copy.imageExtent      = { 16, 16, 1 };
        context.device_dispatch.vkCmdCopyBufferToImage(command,
                                                       stage.handle(),
                                                       image.handle(),
                                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                       1,
                                                       &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        context.device_dispatch.vkCmdPipelineBarrier(command,
                                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                     0,
                                                     0,
                                                     nullptr,
                                                     0,
                                                     nullptr,
                                                     1,
                                                     &barrier);
        context.device_dispatch.vkCmdCopyImageToBuffer(command,
                                                       image.handle(),
                                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                       readback.handle(),
                                                       1,
                                                       &copy);
        TransferBarrier(context.device_dispatch,
                        command,
                        readback.handle(),
                        VK_ACCESS_HOST_READ_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT);
        auto fence = submission.fence = context.submit(command);
        ASSERT_NE(fence, VK_NULL_HANDLE);
        submission.completed = context.wait(fence);
        ASSERT_TRUE(submission.completed);
        auto mapped_result = read_memory.map();
        ASSERT_TRUE(mapped_result.is_ok());
        auto mapped = mapped_result.unwrap_unchecked();
        ASSERT_TRUE(read_memory.invalidate().is_ok());
        EXPECT_EQ(rstd::mem::memcmp(write.data(), mapped.data(), usize(bytes_count)), 0);
        if (! flags) {
            unsigned       attachments = 0;
            const VkFormat depth_formats[] { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM };
            for (auto format : depth_formats) {
                VkImageFormatProperties supported {};
                if (context.instance_dispatch.vkGetPhysicalDeviceImageFormatProperties(
                        context.gpu,
                        format,
                        VK_IMAGE_TYPE_2D,
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                        0,
                        &supported) != VK_SUCCESS)
                    continue;
                image_info.format = format;
                image_info.usage  = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                auto depth        = allocator.create_image(image_info);
                ASSERT_TRUE(depth.is_ok());
                ++attachments;
                break;
            }
            EXPECT_GT(attachments, 0u);
            VkImageFormatProperties supported {};
            if (context.instance_dispatch.vkGetPhysicalDeviceImageFormatProperties(
                    context.gpu,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TYPE_2D,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                    0,
                    &supported) == VK_SUCCESS &&
                (supported.sampleCounts & VK_SAMPLE_COUNT_4_BIT)) {
                image_info.format  = VK_FORMAT_R8G8B8A8_UNORM;
                image_info.usage   = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                image_info.samples = VK_SAMPLE_COUNT_4_BIT;
                auto msaa          = allocator.create_image(image_info);
                ASSERT_TRUE(msaa.is_ok());
                rstd::io::print("vvk MSAA: 4x verified\n");
            } else
                rstd::io::print("vvk MSAA: 4x unavailable, not covered\n");
        }
    }
    EXPECT_EQ(context.errors, 0u);
}
} // namespace

TEST(MemoryVulkan, ImageTransferAndSupportedAttachments) { CheckImageTransfer(0); }
TEST(MemoryVulkan, CubeImageTransferAndView) {
    CheckImageTransfer(VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
}
TEST(MemoryVulkan, MutableImageTransferAndViews) {
    CheckImageTransfer(VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT);
}
TEST(MemoryVulkan, FormatListExtensionTransferAndViews) {
    CheckImageTransfer(VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
                       true);
}
TEST(MemoryVulkan, FormatListCoreTransferAndViews) {
    CheckImageTransfer(VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, true, VK_API_VERSION_1_2);
}

TEST(MemoryVulkan, RepeatedReuseAndBudget) {
    VulkanMemoryTest context;
    const bool       initialized = context.initialize();
    if (context.unavailable)
        GTEST_SKIP() << "No Vulkan loader, ICD, or Vulkan 1.1 graphics device available";
    ASSERT_TRUE(initialized) << "Vulkan test context creation failed";
    {
        auto result = vvk::MemoryAllocator::Create(
            context.gpu, context.instance_dispatch, context.device_dispatch);
        ASSERT_TRUE(result.is_ok());
        auto allocator = result.unwrap_unchecked();
        for (unsigned iteration = 0; iteration < 100; ++iteration) {
            {
                auto buffer = allocator.create_buffer(
                    BufferCreate(1024 + (iteration % 7) * 256, VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
                ASSERT_TRUE(buffer.is_ok());
            }
            auto snapshot = allocator.budget();
            for (unsigned h = 0; h < snapshot.heap_count; ++h)
                EXPECT_EQ(snapshot.heaps[h].allocation_count, 0u);
        }
        allocator.trim();
        auto snapshot = allocator.budget();
        for (unsigned h = 0; h < snapshot.heap_count; ++h)
            EXPECT_EQ(snapshot.heaps[h].block_count, 0u);
        VkPhysicalDeviceMemoryProperties memory;
        context.instance_dispatch.vkGetPhysicalDeviceMemoryProperties(context.gpu, &memory);
        bool non_coherent = false;
        for (unsigned i = 0; i < memory.memoryTypeCount; ++i)
            non_coherent |=
                (memory.memoryTypes[i].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        rstd::io::print("vvk host non-coherent memory advertised: {}\n",
                        non_coherent ? "yes" : "no; atom behavior covered by mock dispatch only");
    }
    EXPECT_EQ(context.errors, 0u);
}

TEST(MemoryVulkan, RingUploadWrapAndSubmissionLifetime) {
    VulkanMemoryTest context;
    const bool       initialized = context.initialize();
    if (context.unavailable)
        GTEST_SKIP() << "No Vulkan loader, ICD, or Vulkan 1.1 graphics device available";
    ASSERT_TRUE(initialized) << "Vulkan test context creation failed";
    {
        const VkDeviceSize atom             = context.properties.limits.nonCoherentAtomSize;
        const VkDeviceSize unit             = atom > 256 ? atom : 256;
        const VkDeviceSize capacity         = unit * 4;
        auto               allocator_result = vvk::MemoryAllocator::Create(
            context.gpu, context.instance_dispatch, context.device_dispatch);
        ASSERT_TRUE(allocator_result.is_ok());
        auto allocator = allocator_result.unwrap_unchecked();
        auto host      = vvk::MemoryRequest { .required           = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                              .preferred          = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                              .persistent_mapping = true };
        auto source_result =
            allocator.create_buffer(BufferCreate(capacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT), host);
        auto destination_result = allocator.create_buffer(BufferCreate(
            unit * 6, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT));
        auto readback_result =
            allocator.create_buffer(BufferCreate(unit * 6, VK_BUFFER_USAGE_TRANSFER_DST_BIT), host);
        ASSERT_TRUE(source_result.is_ok());
        ASSERT_TRUE(destination_result.is_ok());
        ASSERT_TRUE(readback_result.is_ok());
        auto source        = source_result.unwrap_unchecked(),
             destination   = destination_result.unwrap_unchecked(),
             readback      = readback_result.unwrap_unchecked();
        auto source_memory = source.allocation(), read_memory = readback.allocation();
        auto write_result = source_memory.map(), read_result = read_memory.map();
        ASSERT_TRUE(write_result.is_ok());
        ASSERT_TRUE(read_result.is_ok());
        auto write = write_result.unwrap_unchecked(), read = read_result.unwrap_unchecked();
        alloc::RingRangeAllocator ranges(capacity);
        struct Batch {
            const vvk::DeviceDispatch* dispatch {};
            VkDevice                   device {};
            VkCommandPool              pool {};
            VkCommandBuffer            command {};
            VkFence                    fence {};
            alloc::RingRangeAllocation range;
            vvk::AllocatedBuffer       source, destination, readback;
            bool                       observed {};
            ~Batch() {
                if (fence) {
                    if (dispatch->vkWaitForFences(device, 1, &fence, VK_TRUE, 10'000'000'000ULL) !=
                        VK_SUCCESS)
                        dispatch->vkDeviceWaitIdle(device);
                    dispatch->vkDestroyFence(device, fence, nullptr);
                }
                if (command) dispatch->vkFreeCommandBuffers(device, pool, 1, &command);
            }
        } batches[4];
        const VkDeviceSize sizes[] { unit * 2, unit, unit * 2, unit };
        const VkDeviceSize outputs[] { 0, unit * 2, unit * 3, unit * 5 };
        auto               pattern = [](unsigned batch, VkDeviceSize i) {
            return static_cast<unsigned char>((batch + 1) * 37 + i * 13);
        };
        auto enqueue = [&](unsigned batch) {
            auto& pending = batches[batch];
            auto  result  = ranges.allocate(sizes[batch], unit);
            ASSERT_TRUE(result.is_ok());
            pending.range       = result.unwrap_unchecked();
            pending.device      = context.device;
            pending.dispatch    = &context.device_dispatch;
            pending.pool        = context.pool;
            pending.source      = source.clone();
            pending.destination = destination.clone();
            pending.readback    = readback.clone();
            auto* bytes         = static_cast<unsigned char*>(write.data()) + pending.range.offset;
            for (VkDeviceSize i = 0; i < sizes[batch]; ++i) bytes[i] = pattern(batch, i);
            ASSERT_TRUE(source_memory.flush(pending.range.offset, pending.range.size).is_ok());
            pending.command = context.begin();
            ASSERT_NE(pending.command, VK_NULL_HANDLE);
            VkBufferCopy upload { pending.range.offset, outputs[batch], sizes[batch] };
            context.device_dispatch.vkCmdCopyBuffer(
                pending.command, pending.source.handle(), pending.destination.handle(), 1, &upload);
            TransferBarrier(context.device_dispatch,
                            pending.command,
                            pending.destination.handle(),
                            VK_ACCESS_TRANSFER_READ_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferCopy download { outputs[batch], outputs[batch], sizes[batch] };
            context.device_dispatch.vkCmdCopyBuffer(pending.command,
                                                    pending.destination.handle(),
                                                    pending.readback.handle(),
                                                    1,
                                                    &download);
            TransferBarrier(context.device_dispatch,
                            pending.command,
                            pending.readback.handle(),
                            VK_ACCESS_HOST_READ_BIT,
                            VK_PIPELINE_STAGE_HOST_BIT);
            pending.fence = context.submit(pending.command);
            ASSERT_NE(pending.fence, VK_NULL_HANDLE);
        };
        auto retire = [&](unsigned batch) {
            auto& pending = batches[batch];
            ASSERT_TRUE(pending.observed);
            ASSERT_TRUE(ranges.release(pending.range.id).is_ok());
            pending.source.reset();
            pending.destination.reset();
            pending.readback.reset();
        };
        enqueue(0);
        ASSERT_NE(batches[0].fence, VK_NULL_HANDLE);
        enqueue(1);
        ASSERT_NE(batches[1].fence, VK_NULL_HANDLE);
        ASSERT_TRUE(context.wait(batches[1].fence));
        batches[1].observed = true;
        EXPECT_EQ(ranges.release(batches[1].range.id).unwrap_err_unchecked(),
                  alloc::RangeError::OutOfOrder);
        EXPECT_TRUE(ranges.allocate(unit * 2, unit).is_err());
        ASSERT_TRUE(context.wait(batches[0].fence));
        batches[0].observed = true;
        retire(0);
        enqueue(2);
        ASSERT_NE(batches[2].fence, VK_NULL_HANDLE);
        EXPECT_EQ(batches[2].range.offset, 0u);
        EXPECT_EQ(ranges.statistics().padding_bytes, unit);
        EXPECT_EQ(ranges.statistics().free_bytes, 0u);
        EXPECT_TRUE(ranges.release(batches[0].range.id).is_err());
        retire(1);
        enqueue(3);
        ASSERT_NE(batches[3].fence, VK_NULL_HANDLE);
        EXPECT_EQ(batches[3].range.offset, unit * 2);
        source.reset();
        destination.reset();
        readback.reset();
        allocator.trim();
        ASSERT_TRUE(context.wait(batches[3].fence));
        batches[3].observed = true;
        EXPECT_EQ(ranges.release(batches[3].range.id).unwrap_err_unchecked(),
                  alloc::RangeError::OutOfOrder);
        ASSERT_TRUE(context.wait(batches[2].fence));
        batches[2].observed = true;
        ASSERT_TRUE(read_memory.invalidate().is_ok());
        const auto* bytes = static_cast<const unsigned char*>(read.data());
        for (unsigned batch = 0; batch < 4; ++batch)
            for (VkDeviceSize i = 0; i < sizes[batch]; ++i)
                EXPECT_EQ(bytes[outputs[batch] + i], pattern(batch, i));
        retire(2);
        retire(3);
        EXPECT_TRUE(ranges.front().is_none());
        EXPECT_EQ(ranges.statistics().free_bytes, capacity);
        rstd::io::print("vvk ring: four submissions, tail padding={}, wrapped upload and byte "
                        "readback verified\n",
                        static_cast<unsigned long long>(unit));
    }
    EXPECT_EQ(context.errors, 0u);
}

TEST(MemoryVulkan, GrowingAllocatorImageReadback) {
    CheckImageTransfer(
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, false, VK_API_VERSION_1_1, { 4096, 65536, 3, true });
}

TEST(MemoryVulkan, GrowingAllocatorChurnAndTrim) {
    VulkanMemoryTest context;
    const bool       initialized = context.initialize();
    if (context.unavailable)
        GTEST_SKIP() << "No Vulkan loader, ICD, or Vulkan 1.1 graphics device available";
    ASSERT_TRUE(initialized);
    {
        auto made = vvk::MemoryAllocator::Create(context.gpu,
                                                 context.instance_dispatch,
                                                 context.device_dispatch,
                                                 { 4096, 65536, 3, true });
        ASSERT_TRUE(made.is_ok());
        auto                 allocator = made.unwrap_unchecked();
        vvk::AllocatedBuffer slots[16];
        for (unsigned i = 0; i < 256; ++i) {
            auto& slot = slots[(i * 7) % 16];
            slot.reset();
            auto made_buffer = allocator.create_buffer(
                BufferCreate(4096 + (i % 5) * 512, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
                { .required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT });
            ASSERT_TRUE(made_buffer.is_ok());
            slot        = made_buffer.unwrap_unchecked();
            auto memory = slot.allocation();
            auto mapped = memory.map();
            ASSERT_TRUE(mapped.is_ok());
            auto mapping = mapped.unwrap_unchecked();
            rstd::mem::memset(mapping.data(), u8(i & 255), usize(4096));
            ASSERT_TRUE(memory.flush().is_ok());
        }
        for (auto& slot : slots) slot.reset();
        allocator.trim();
        const auto snapshot = allocator.budget();
        for (unsigned h = 0; h < snapshot.heap_count; ++h) {
            EXPECT_EQ(snapshot.heaps[h].allocation_count, 0u);
            EXPECT_EQ(snapshot.heaps[h].block_count, 0u);
            EXPECT_EQ(snapshot.heaps[h].block_bytes, 0u);
        }
    }
    EXPECT_EQ(context.errors, 0u);
}

TEST(MemoryVulkan, AccessIntentImageReadback) {
    CheckImageTransfer(VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, false, VK_API_VERSION_1_1, {}, true);
}

namespace
{
void CheckBufferAddressShader(unsigned api, bool pooled = false) {
    VulkanMemoryTest context;
    const bool       initialized = context.initialize(api, false, true);
    if (context.unavailable)
        GTEST_SKIP()
            << "Requested Vulkan API, BDA feature/extension or graphics-compute queue unavailable";
    ASSERT_TRUE(initialized);
    {
        auto made = vvk::MemoryAllocator::Create(
            context.gpu, context.instance_dispatch, context.device_dispatch);
        ASSERT_TRUE(made.is_ok());
        auto allocator   = made.unwrap_unchecked();
        auto create_info = BufferCreate(
            256, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        auto padding = allocator.create_buffer(create_info);
        ASSERT_TRUE(padding.is_ok());
        for (unsigned dedicated = 0; dedicated < (pooled ? 1u : 2u); ++dedicated) {
            vvk::MemoryPool output_pool, read_pool;
            if (pooled) {
                vvk::MemoryPoolCreateInfo info {
                    .memory_type      = padding.unwrap_unchecked().allocation().info().memory_type,
                    .allocation_flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
                    .block_policy     = { 65536, 65536, 0, false },
                    .min_blocks       = 1,
                    .max_blocks       = 1
                };
                auto made_pool = allocator.create_pool(info);
                ASSERT_TRUE(made_pool.is_ok());
                output_pool = made_pool.unwrap_unchecked();
            }
            auto output_result =
                pooled ? output_pool.create_buffer(create_info, { .existing_blocks_only = true })
                       : allocator.create_buffer(create_info, { .dedicated = dedicated != 0 });
            auto read_result =
                allocator.create_buffer(BufferCreate(256, VK_BUFFER_USAGE_TRANSFER_DST_BIT),
                                        vvk::MemoryRequest::Readback());
            ASSERT_TRUE(output_result.is_ok());
            ASSERT_TRUE(read_result.is_ok());
            auto output   = output_result.unwrap_unchecked(),
                 readback = read_result.unwrap_unchecked();
            if (pooled) {
                vvk::MemoryPoolCreateInfo info { .memory_type =
                                                     readback.allocation().info().memory_type,
                                                 .block_policy = { 65536, 65536, 0, false },
                                                 .min_blocks   = 1,
                                                 .max_blocks   = 1 };
                auto                      made_pool = allocator.create_pool(info);
                ASSERT_TRUE(made_pool.is_ok());
                read_pool                    = made_pool.unwrap_unchecked();
                auto request                 = vvk::MemoryRequest::Readback();
                request.existing_blocks_only = true;
                auto pooled_read             = read_pool.create_buffer(
                    BufferCreate(256, VK_BUFFER_USAGE_TRANSFER_DST_BIT), request);
                ASSERT_TRUE(pooled_read.is_ok());
                readback = pooled_read.unwrap_unchecked();
            }
            auto address_result = output.device_address();
            ASSERT_TRUE(address_result.is_ok());
            auto address = address_result.unwrap_unchecked();
            ASSERT_NE(address, 0u);
            struct Submission {
                VulkanMemoryTest&    context;
                vvk::AllocatedBuffer output, readback;
                VkShaderModule       shader {};
                VkPipelineLayout     layout {};
                VkPipeline           pipeline {};
                VkCommandBuffer      command {};
                VkFence              fence {};
                bool                 completed {};
                ~Submission() {
                    auto& d = context.device_dispatch;
                    if (fence) {
                        if (! completed && ! context.wait(fence))
                            d.vkDeviceWaitIdle(context.device);
                        d.vkDestroyFence(context.device, fence, nullptr);
                    }
                    if (command) d.vkFreeCommandBuffers(context.device, context.pool, 1, &command);
                    if (pipeline) d.vkDestroyPipeline(context.device, pipeline, nullptr);
                    if (layout) d.vkDestroyPipelineLayout(context.device, layout, nullptr);
                    if (shader) d.vkDestroyShaderModule(context.device, shader, nullptr);
                }
            } submission { context, output.clone(), readback.clone() };
            auto&                    d = context.device_dispatch;
            VkShaderModuleCreateInfo shader { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            shader.codeSize = sizeof(address_shader);
            shader.pCode    = address_shader;
            ASSERT_EQ(d.vkCreateShaderModule(context.device, &shader, nullptr, &submission.shader),
                      VK_SUCCESS);
            VkPushConstantRange range { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VkDeviceAddress) };
            VkPipelineLayoutCreateInfo layout { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            layout.pushConstantRangeCount = 1;
            layout.pPushConstantRanges    = &range;
            ASSERT_EQ(
                d.vkCreatePipelineLayout(context.device, &layout, nullptr, &submission.layout),
                VK_SUCCESS);
            VkComputePipelineCreateInfo pipeline { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            pipeline.stage  = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                nullptr,
                                0,
                                VK_SHADER_STAGE_COMPUTE_BIT,
                                submission.shader,
                                "main",
                                nullptr };
            pipeline.layout = submission.layout;
            ASSERT_EQ(
                d.vkCreateComputePipelines(
                    context.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &submission.pipeline),
                VK_SUCCESS);
            submission.command = context.begin();
            ASSERT_NE(submission.command, VK_NULL_HANDLE);
            d.vkCmdBindPipeline(
                submission.command, VK_PIPELINE_BIND_POINT_COMPUTE, submission.pipeline);
            d.vkCmdPushConstants(submission.command,
                                 submission.layout,
                                 VK_SHADER_STAGE_COMPUTE_BIT,
                                 0,
                                 sizeof(address),
                                 &address);
            d.vkCmdDispatch(submission.command, 1, 1, 1);
            VkBufferMemoryBarrier barrier { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
            barrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer                                            = output.handle();
            barrier.size                                              = VK_WHOLE_SIZE;
            d.vkCmdPipelineBarrier(submission.command,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   0,
                                   0,
                                   nullptr,
                                   1,
                                   &barrier,
                                   0,
                                   nullptr);
            VkBufferCopy copy { 0, 0, 256 };
            d.vkCmdCopyBuffer(submission.command, output.handle(), readback.handle(), 1, &copy);
            TransferBarrier(d,
                            submission.command,
                            readback.handle(),
                            VK_ACCESS_HOST_READ_BIT,
                            VK_PIPELINE_STAGE_HOST_BIT);
            submission.fence = context.submit(submission.command);
            ASSERT_NE(submission.fence, VK_NULL_HANDLE);
            output.reset();
            if (pooled) {
                output_pool.reset();
                read_pool.reset();
                padding.unwrap_unchecked().reset();
                allocator.reset();
                readback.reset();
            }
            EXPECT_EQ(submission.output.device_address().unwrap_unchecked(), address);
            submission.completed = context.wait(submission.fence);
            ASSERT_TRUE(submission.completed);
            auto memory = submission.readback.allocation();
            auto mapped = memory.map();
            ASSERT_TRUE(mapped.is_ok());
            auto mapping = mapped.unwrap_unchecked();
            ASSERT_TRUE(memory.invalidate().is_ok());
            auto* values = static_cast<const unsigned*>(mapping.data());
            for (unsigned i = 0; i < 64; ++i) EXPECT_EQ(values[i], i * 17 + 9);
        }
    }
    EXPECT_EQ(context.errors, 0u);
}
} // namespace
TEST(MemoryVulkan, BufferAddressCoreShaderReadback) {
    CheckBufferAddressShader(VK_API_VERSION_1_2);
}
TEST(MemoryVulkan, BufferAddressExtensionShaderReadback) {
    CheckBufferAddressShader(VK_API_VERSION_1_1);
}

TEST(MemoryVulkan, PoolBufferAddressSubmissionLifetime) {
    CheckBufferAddressShader(VK_API_VERSION_1_2, true);
}
TEST(MemoryVulkan, PoolTransferAndSubmissionLifetime) {
    VulkanMemoryTest context;
    const bool       initialized = context.initialize();
    if (context.unavailable) GTEST_SKIP() << "Vulkan 1.1 graphics device unavailable";
    ASSERT_TRUE(initialized);
    {
        auto made = vvk::MemoryAllocator::Create(
            context.gpu, context.instance_dispatch, context.device_dispatch);
        ASSERT_TRUE(made.is_ok());
        auto       allocator = made.unwrap_unchecked();
        const auto usage     = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        auto       probe_result =
            allocator.create_buffer(BufferCreate(256, usage), vvk::MemoryRequest::Readback());
        ASSERT_TRUE(probe_result.is_ok());
        auto                      probe = probe_result.unwrap_unchecked();
        vvk::MemoryPoolCreateInfo info { .memory_type  = probe.allocation().info().memory_type,
                                         .block_policy = { 65536, 65536, 0, false },
                                         .min_blocks   = 1,
                                         .max_blocks   = 1 };
        auto                      pool_result = allocator.create_pool(info);
        ASSERT_TRUE(pool_result.is_ok());
        auto pool                    = pool_result.unwrap_unchecked();
        auto request                 = vvk::MemoryRequest::Readback();
        request.existing_blocks_only = true;
        auto src_result              = pool.create_buffer(BufferCreate(256, usage), request);
        auto dst_result              = pool.create_buffer(BufferCreate(256, usage), request);
        ASSERT_TRUE(src_result.is_ok());
        ASSERT_TRUE(dst_result.is_ok());
        auto source = src_result.unwrap_unchecked(), destination = dst_result.unwrap_unchecked();
        {
            auto memory = source.allocation();
            auto mapped = memory.map();
            ASSERT_TRUE(mapped.is_ok());
            auto mapping = mapped.unwrap_unchecked();
            for (unsigned i = 0; i < 256; ++i)
                static_cast<unsigned char*>(mapping.data())[i] =
                    static_cast<unsigned char>(i ^ 0x5a);
            ASSERT_TRUE(memory.flush().is_ok());
        }
        struct Submission {
            VulkanMemoryTest&    context;
            vvk::AllocatedBuffer source, destination;
            VkCommandBuffer      command {};
            VkFence              fence {};
            bool                 completed {};
            ~Submission() {
                if (fence) {
                    if (! completed && ! context.wait(fence))
                        context.device_dispatch.vkDeviceWaitIdle(context.device);
                    context.device_dispatch.vkDestroyFence(context.device, fence, nullptr);
                }
                if (command)
                    context.device_dispatch.vkFreeCommandBuffers(
                        context.device, context.pool, 1, &command);
            }
        } submission { context, source.clone(), destination.clone() };
        submission.command = context.begin();
        ASSERT_NE(submission.command, VK_NULL_HANDLE);
        VkBufferCopy copy { 0, 0, 256 };
        context.device_dispatch.vkCmdCopyBuffer(
            submission.command, source.handle(), destination.handle(), 1, &copy);
        TransferBarrier(context.device_dispatch,
                        submission.command,
                        destination.handle(),
                        VK_ACCESS_HOST_READ_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT);
        submission.fence = context.submit(submission.command);
        ASSERT_NE(submission.fence, VK_NULL_HANDLE);
        source.reset();
        destination.reset();
        probe.reset();
        pool.reset();
        allocator.reset();
        submission.completed = context.wait(submission.fence);
        ASSERT_TRUE(submission.completed);
        auto memory = submission.destination.allocation();
        auto mapped = memory.map();
        ASSERT_TRUE(mapped.is_ok());
        auto mapping = mapped.unwrap_unchecked();
        ASSERT_TRUE(memory.invalidate().is_ok());
        for (unsigned i = 0; i < 256; ++i)
            EXPECT_EQ(static_cast<const unsigned char*>(mapping.data())[i],
                      static_cast<unsigned char>(i ^ 0x5a));
    }
    EXPECT_EQ(context.errors, 0u);
}
