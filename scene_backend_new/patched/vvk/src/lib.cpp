module;
#include "vvk/macros.hpp"

module vvk;
import rstd.log;
import rstd;
import :ffi.vulkan;

using namespace rstd::prelude;

namespace vvk
{
constexpr auto vk_count(usize value) noexcept -> rstd::uint32_t {
    return rstd::as_cast<rstd::uint32_t>(value);
}

void Destroy(VkInstance instance, const InstanceDispatch& dld) noexcept {
    dld.vkDestroyInstance(instance, nullptr);
}

void Destroy(VkDevice device, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyDevice(device, nullptr);
}

void Destroy(VkInstance Instance, VkDebugUtilsMessengerEXT handle,
             const InstanceDispatch& dld) noexcept {
    dld.vkDestroyDebugUtilsMessengerEXT(Instance, handle, nullptr);
}

void Destroy(VkDevice device, VkCommandPool handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyCommandPool(device, handle, nullptr);
}
void Destroy(VkDevice device, VkBuffer handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyBuffer(device, handle, nullptr);
}
void Destroy(VkDevice device, VkPipeline handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyPipeline(device, handle, nullptr);
}
void Destroy(VkDevice device, VkPipelineLayout handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyPipelineLayout(device, handle, nullptr);
}

void Destroy(VkDevice device, VkRenderPass handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyRenderPass(device, handle, nullptr);
}

void Destroy(VkDevice device, VkDescriptorSetLayout handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyDescriptorSetLayout(device, handle, nullptr);
}

void Destroy(VkDevice device, VkImage handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyImage(device, handle, nullptr);
}

void Destroy(VkDevice device, VkImageView handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyImageView(device, handle, nullptr);
}

void Destroy(VkDevice device, VkDeviceMemory handle, const DeviceDispatch& dld) noexcept {
    dld.vkFreeMemory(device, handle, nullptr);
}

void Destroy(VkDevice device, VkShaderModule handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyShaderModule(device, handle, nullptr);
}

void Destroy(VkDevice device, VkSwapchainKHR handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroySwapchainKHR(device, handle, nullptr);
}

void Destroy(VkDevice device, VkSampler handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroySampler(device, handle, nullptr);
}

void Destroy(VkDevice device, VkSemaphore handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroySemaphore(device, handle, nullptr);
}

void Destroy(VkDevice device, VkFence handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyFence(device, handle, nullptr);
}

void Destroy(VkDevice device, VkFramebuffer handle, const DeviceDispatch& dld) noexcept {
    dld.vkDestroyFramebuffer(device, handle, nullptr);
}

void Destroy(VkInstance instance, VkSurfaceKHR handle, const InstanceDispatch& dld) noexcept {
    dld.vkDestroySurfaceKHR(instance, handle, nullptr);
}

VkResult Free(VkDevice device, VkCommandPool pool, slice<VkCommandBuffer> allos,
              const DeviceDispatch& dld) noexcept {
    dld.vkFreeCommandBuffers(device, pool, vk_count(allos.len()), allos.as_raw_ptr());
    return VK_SUCCESS;
}

auto Instance::Create(Instance& inst, const GlobalDispatch& global,
                      const VkInstanceCreateInfo& input, InstanceDispatch& dispatch)
    -> Result<empty, DispatchError> {
    if (inst || dispatch.instance || ! global.vkCreateInstance || ! global.vkGetInstanceProcAddr)
        return Err(DispatchError { DispatchErrorKind::InvalidInput, DispatchStage::Instance });
    auto parsed = ParseInstanceCapabilities(input);
    if (parsed.is_err()) return Err(parsed.unwrap_err_unchecked());
    auto caps = parsed.unwrap_unchecked();
    auto info = input;
    if (caps.portability_enumeration)
        info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    VkInstance instance {};
    auto       result = global.vkCreateInstance(&info, nullptr, &instance);
    if (result != VK_SUCCESS)
        return Err(DispatchError {
            DispatchErrorKind::Vulkan, DispatchStage::Instance, "vkCreateInstance", result });
    auto loaded = LoadInstance(global, instance, caps);
    if (loaded.is_err()) {
        auto error   = loaded.unwrap_err_unchecked();
        auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(
            global.vkGetInstanceProcAddr(instance, "vkDestroyInstance"));
        if (destroy)
            destroy(instance, nullptr);
        else
            error.unowned_instance = instance;
        return Err(error);
    }
    dispatch = loaded.unwrap_unchecked();
    inst     = Instance(instance, dispatch);
    return Ok(empty {});
}

rstd::vec::Vec<PhysicalDevice> Instance::EnumeratePhysicalDevices() const noexcept {
    rstd::uint32_t num;
    VVK_CHECK(dld->vkEnumeratePhysicalDevices(handle, &num, nullptr));
    auto vkphysical_devices = rstd::vec::Vec<VkPhysicalDevice>::with_capacity(usize(num));
    vkphysical_devices.resize(usize(num), VK_NULL_HANDLE);
    VVK_CHECK(dld->vkEnumeratePhysicalDevices(handle, &num, vkphysical_devices.data()));
    auto physical_devices = rstd::vec::Vec<PhysicalDevice>::with_capacity(usize(num));
    for (const auto vkphysical_device : vkphysical_devices) {
        physical_devices.push(PhysicalDevice(vkphysical_device, *dld));
    }
    return physical_devices;
}

DebugUtilsMessenger Instance::CreateDebugUtilsMessenger(
    const VkDebugUtilsMessengerCreateInfoEXT& create_info) const noexcept {
    VkDebugUtilsMessengerEXT object;
    VVK_CHECK(dld->vkCreateDebugUtilsMessengerEXT(handle, &create_info, nullptr, &object));
    return DebugUtilsMessenger(object, handle, *dld);
}
auto Device::Create(Device& output, VkPhysicalDevice physical, const InstanceDispatch& parent,
                    const VkDeviceCreateInfo& info, DeviceDispatch& dispatch)
    -> Result<empty, DispatchError> {
    if (output || dispatch.device || ! physical || ! parent.instance || ! parent.vkCreateDevice ||
        ! parent.vkGetDeviceProcAddr || ! parent.vkGetPhysicalDeviceProperties)
        return Err(DispatchError { DispatchErrorKind::InvalidInput, DispatchStage::Device });
    VkPhysicalDeviceProperties properties {};
    parent.vkGetPhysicalDeviceProperties(physical, &properties);
    auto parsed = ParseDeviceCapabilities(info, parent.capabilities, properties.apiVersion);
    if (parsed.is_err()) return Err(parsed.unwrap_err_unchecked());
    VkDevice device {};
    auto     result = parent.vkCreateDevice(physical, &info, nullptr, &device);
    if (result != VK_SUCCESS)
        return Err(DispatchError {
            DispatchErrorKind::Vulkan, DispatchStage::Device, "vkCreateDevice", result });
    auto loaded = LoadDevice(parent, device, parsed.unwrap_unchecked());
    if (loaded.is_err()) {
        auto error   = loaded.unwrap_err_unchecked();
        auto destroy = reinterpret_cast<PFN_vkDestroyDevice>(
            parent.vkGetDeviceProcAddr(device, "vkDestroyDevice"));
        if (destroy)
            destroy(device, nullptr);
        else
            error.unowned_device = device;
        return Err(error);
    }
    dispatch = loaded.unwrap_unchecked();
    output   = Device(device, dispatch);
    return Ok(empty {});
}

Queue Device::GetQueue(rstd::uint32_t family_index) const noexcept {
    VkQueue queue;
    dld->vkGetDeviceQueue(handle, family_index, 0, &queue);
    return Queue(queue, *dld);
}

VkMemoryRequirements Device::GetImageMemoryRequirements(VkImage image) const noexcept {
    VkMemoryRequirements requirements;
    dld->vkGetImageMemoryRequirements(handle, image, &requirements);
    return requirements;
}

VkMemoryRequirements2
Device::GetImageMemoryRequirements2(const VkImageMemoryRequirementsInfo2& info,
                                    void* next_structures) const noexcept {
    VkMemoryRequirements2 requirements {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = next_structures,
    };
    dld->vkGetImageMemoryRequirements2(handle, &info, &requirements);
    return requirements;
}

VkMemoryRequirements Device::GetBufferMemoryRequirements(VkBuffer buffer) const noexcept {
    const VkBufferMemoryRequirementsInfo2 info {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
        .pNext  = nullptr,
        .buffer = buffer,
    };
    return GetBufferMemoryRequirements2(info).memoryRequirements;
}

VkMemoryRequirements2
Device::GetBufferMemoryRequirements2(const VkBufferMemoryRequirementsInfo2& info,
                                     void* next_structures) const noexcept {
    VkMemoryRequirements2 requirements {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = next_structures,
    };
    dld->vkGetBufferMemoryRequirements2(handle, &info, &requirements);
    return requirements;
}

VkSubresourceLayout
Device::GetImageSubresourceLayout(VkImage                   image,
                                  const VkImageSubresource& subresource) const noexcept {
    VkSubresourceLayout layout {};
    dld->vkGetImageSubresourceLayout(handle, image, &subresource, &layout);
    return layout;
}

VkResult Device::GetImageDrmFormatModifierPropertiesEXT(
    VkImage image, VkImageDrmFormatModifierPropertiesEXT* props) const noexcept {
    return dld->vkGetImageDrmFormatModifierPropertiesEXT(handle, image, props);
}

VkResult Device::GetMemoryFdPropertiesKHR(VkExternalMemoryHandleTypeFlagBits handle_type, int fd,
                                          VkMemoryFdPropertiesKHR& properties) const noexcept {
    if (! dld->vkGetMemoryFdPropertiesKHR) return VK_ERROR_EXTENSION_NOT_PRESENT;
    return dld->vkGetMemoryFdPropertiesKHR(handle, handle_type, fd, &properties);
}

VkResult Device::BindImageMemory2(slice<VkBindImageMemoryInfo> bindings) const noexcept {
    return dld->vkBindImageMemory2(handle, vk_count(bindings.len()), bindings.as_raw_ptr());
}

VkResult Device::AllocateMemory(const VkMemoryAllocateInfo& ai, DeviceMemory& mem) const noexcept {
    VkDeviceMemory memory;
    auto           res = dld->vkAllocateMemory(handle, &ai, nullptr, &memory);
    if (res == VK_SUCCESS) mem = DeviceMemory(memory, handle, *dld);
    return res;
}

VkResult Device::CreateBuffer(const VkBufferCreateInfo& ci, Buffer& buffer) const noexcept {
    VkBuffer vkbuffer;
    auto     res = dld->vkCreateBuffer(handle, &ci, nullptr, &vkbuffer);
    if (res == VK_SUCCESS) buffer = Buffer(vkbuffer, handle, *dld);
    return res;
}

VkResult Device::CreateCommandPool(const VkCommandPoolCreateInfo& ci, CommandPool& pool) const {
    VkCommandPool vkpool;
    VkResult      res = dld->vkCreateCommandPool(handle, &ci, nullptr, &vkpool);
    if (res == VK_SUCCESS) pool = CommandPool(vkpool, handle, *dld);
    return res;
}

VkResult Device::CreateDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& ci,
                                           DescriptorSetLayout& layout) const noexcept {
    VkDescriptorSetLayout object;
    VkResult              res = dld->vkCreateDescriptorSetLayout(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) layout = DescriptorSetLayout(object, handle, *dld);
    return res;
}

VkResult Device::CreateRenderPass(const VkRenderPassCreateInfo& ci,
                                  RenderPass&                   pass) const noexcept {
    VkRenderPass object;
    VkResult     res = dld->vkCreateRenderPass(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) pass = RenderPass(object, handle, *dld);
    return res;
}

VkResult Device::CreatePipelineLayout(const VkPipelineLayoutCreateInfo& ci,
                                      PipelineLayout&                   layout) const noexcept {
    VkPipelineLayout object;
    VkResult         res = dld->vkCreatePipelineLayout(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) layout = PipelineLayout(object, handle, *dld);
    return res;
}

VkResult Device::CreateSwapchainKHR(const VkSwapchainCreateInfoKHR& ci,
                                    SwapchainKHR&                   surface) const noexcept {
    VkSwapchainKHR object;
    VkResult       res = dld->vkCreateSwapchainKHR(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) surface = SwapchainKHR(object, handle, *dld);
    return res;
}

VkResult Device::CreateShaderModule(const VkShaderModuleCreateInfo& ci,
                                    ShaderModule&                   sm) const noexcept {
    VkShaderModule object;
    VkResult       res = dld->vkCreateShaderModule(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) sm = ShaderModule(object, handle, *dld);
    return res;
}

VkResult Device::CreateSemaphore(const VkSemaphoreCreateInfo& ci, Semaphore& sm) const noexcept {
    VkSemaphore object;
    VkResult    res = dld->vkCreateSemaphore(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) sm = Semaphore(object, handle, *dld);
    return res;
}

VkResult Device::GetSemaphoreFdKHR(const VkSemaphoreGetFdInfoKHR& gi, int* fd) const noexcept {
    if (! dld->vkGetSemaphoreFdKHR) return VK_ERROR_EXTENSION_NOT_PRESENT;
    return dld->vkGetSemaphoreFdKHR(handle, &gi, fd);
}

VkResult Device::CreateImage(const VkImageCreateInfo& ci, Image& img) const noexcept {
    VkImage object;

    VkResult res = dld->vkCreateImage(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) img = Image(object, handle, *dld);
    return res;
}

VkResult Device::CreateImageView(const VkImageViewCreateInfo& ci, ImageView& view) const noexcept {
    VkImageView object;
    VkResult    res = dld->vkCreateImageView(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) view = ImageView(object, handle, *dld);
    return res;
}

VkResult Device::CreateFramebuffer(const VkFramebufferCreateInfo& ci,
                                   Framebuffer&                   fb) const noexcept {
    VkFramebuffer object;
    VkResult      res = dld->vkCreateFramebuffer(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) fb = Framebuffer(object, handle, *dld);
    return res;
}

VkResult Device::CreateFence(const VkFenceCreateInfo& ci, Fence& fe) const noexcept {
    VkFence  object;
    VkResult res = dld->vkCreateFence(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) fe = Fence(object, handle, *dld);
    return res;
}

VkResult Device::CreateSampler(const VkSamplerCreateInfo& ci, Sampler& sam) const noexcept {
    VkSampler object;
    VkResult  res = dld->vkCreateSampler(handle, &ci, nullptr, &object);
    if (res == VK_SUCCESS) sam = Sampler(object, handle, *dld);
    return res;
}

VkResult Device::CreateGraphicsPipeline(const VkGraphicsPipelineCreateInfo& ci,
                                        Pipeline& pipeline) const noexcept {
    VkPipeline object;
    VkResult res = dld->vkCreateGraphicsPipelines(handle, VK_NULL_HANDLE, 1, &ci, nullptr, &object);
    if (res == VK_SUCCESS) pipeline = Pipeline(object, handle, *dld);
    return res;
}

VkResult Device::CreateComputePipeline(const VkComputePipelineCreateInfo& ci,
                                       Pipeline&                          pipeline) const noexcept {
    VkPipeline vkpipeline;
    auto res = dld->vkCreateComputePipelines(handle, VK_NULL_HANDLE, 1, &ci, nullptr, &vkpipeline);
    if (res == VK_SUCCESS) pipeline = Pipeline(vkpipeline, handle, *dld);
    return res;
}

VkResult Buffer::BindMemory(VkDeviceMemory memory, VkDeviceSize offset) const noexcept {
    return dld->vkBindBufferMemory(owner, handle, memory, offset);
}

VkResult Image::BindMemory(VkDeviceMemory memory, VkDeviceSize offset) const noexcept {
    return dld->vkBindImageMemory(owner, handle, memory, offset);
}

VkResult SwapchainKHR::GetImages(rstd::vec::Vec<VkImage>& images) const {
    rstd::uint32_t num;
    if (auto res = dld->vkGetSwapchainImagesKHR(owner, handle, &num, nullptr); res != VK_SUCCESS)
        return res;
    images.resize(usize(num), VK_NULL_HANDLE);
    return dld->vkGetSwapchainImagesKHR(owner, handle, &num, images.data());
}

VkPhysicalDeviceProperties PhysicalDevice::GetProperties() const noexcept {
    VkPhysicalDeviceProperties2 props {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = nullptr,
    };
    dld->vkGetPhysicalDeviceProperties2(handle, &props);
    return props.properties;
}

void PhysicalDevice::GetProperties2KHR(VkPhysicalDeviceProperties2KHR& props) const noexcept {
    dld->vkGetPhysicalDeviceProperties2(handle, &props);
}

void PhysicalDevice::GetFeatures2KHR(VkPhysicalDeviceFeatures2KHR& feats) const noexcept {
    dld->vkGetPhysicalDeviceFeatures2(handle, &feats);
}

VkFormatProperties PhysicalDevice::GetFormatProperties(VkFormat format) const noexcept {
    VkFormatProperties properties {};
    dld->vkGetPhysicalDeviceFormatProperties(handle, format, &properties);
    return properties;
}

VkResult PhysicalDevice::EnumerateDeviceExtensionProperties(
    rstd::vec::Vec<VkExtensionProperties>& properties) const {
    rstd::uint32_t num;
    VkResult       res = dld->vkEnumerateDeviceExtensionProperties(handle, nullptr, &num, nullptr);
    if (res != VK_SUCCESS) return res;

    properties.resize(usize(num), VkExtensionProperties {});
    return dld->vkEnumerateDeviceExtensionProperties(handle, nullptr, &num, properties.data());
}

VkResult PhysicalDevice::GetSurfaceSupportKHR(rstd::uint32_t queue_family_index,
                                              VkSurfaceKHR surface, bool& supported) const {
    VkBool32 vksupported;
    VkResult res = dld->vkGetPhysicalDeviceSurfaceSupportKHR(
        handle, queue_family_index, surface, &vksupported);
    if (res == VK_SUCCESS) supported = vksupported;
    return res;
}

VkResult
PhysicalDevice::GetSurfaceCapabilitiesKHR(VkSurfaceKHR              surface,
                                          VkSurfaceCapabilitiesKHR& capabilities) const noexcept {
    return (dld->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(handle, surface, &capabilities));
}

VkResult PhysicalDevice::GetSurfaceFormatsKHR(VkSurfaceKHR                        surface,
                                              rstd::vec::Vec<VkSurfaceFormatKHR>& formats) const {
    rstd::uint32_t num;
    if (auto res = dld->vkGetPhysicalDeviceSurfaceFormatsKHR(handle, surface, &num, nullptr);
        res != VK_SUCCESS) {
        return res;
    }
    formats.resize(usize(num), VkSurfaceFormatKHR {});
    return dld->vkGetPhysicalDeviceSurfaceFormatsKHR(handle, surface, &num, formats.data());
}

VkResult PhysicalDevice::GetSurfacePresentModesKHR(VkSurfaceKHR                      surface,
                                                   rstd::vec::Vec<VkPresentModeKHR>& modes) const {
    rstd::uint32_t num;
    if (auto res = dld->vkGetPhysicalDeviceSurfacePresentModesKHR(handle, surface, &num, nullptr);
        res != VK_SUCCESS) {
        return res;
    }
    modes.resize(usize(num), VK_PRESENT_MODE_IMMEDIATE_KHR);
    return dld->vkGetPhysicalDeviceSurfacePresentModesKHR(handle, surface, &num, modes.data());
}

VkPhysicalDeviceMemoryProperties2
PhysicalDevice::GetMemoryProperties(void* next_structures) const noexcept {
    VkPhysicalDeviceMemoryProperties2 properties {};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    properties.pNext = next_structures;
    dld->vkGetPhysicalDeviceMemoryProperties2(handle, &properties);
    return properties;
}

rstd::vec::Vec<VkQueueFamilyProperties> PhysicalDevice::GetQueueFamilyProperties() const {
    rstd::uint32_t num;
    dld->vkGetPhysicalDeviceQueueFamilyProperties2(handle, &num, nullptr);
    auto properties2 = rstd::vec::Vec<VkQueueFamilyProperties2>::with_capacity(usize(num));
    properties2.resize(
        usize(num),
        VkQueueFamilyProperties2 { .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 });
    dld->vkGetPhysicalDeviceQueueFamilyProperties2(handle, &num, properties2.data());

    auto properties = rstd::vec::Vec<VkQueueFamilyProperties>::with_capacity(usize(num));
    properties.reserve(usize(num));
    for (rstd::uint32_t i = 0; i < num; ++i) {
        auto property = properties2[usize(i)].queueFamilyProperties;
        properties.push(rstd::move(property));
    }
    return properties;
}

VkResult CommandPool::Allocate(usize num_buffers, VkCommandBufferLevel level,
                               CommandBuffers& buffers_) const {
    const VkCommandBufferAllocateInfo ai {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = handle,
        .level              = level,
        .commandBufferCount = vk_count(num_buffers),
    };

    auto buffers = rstd::vec::Vec<VkCommandBuffer>::with_capacity(num_buffers);
    buffers.resize(num_buffers, VK_NULL_HANDLE);
    VkResult res = dld->vkAllocateCommandBuffers(owner, &ai, buffers.data());
    if (res == VK_SUCCESS) buffers_ = CommandBuffers(rstd::move(buffers), owner, handle, *dld);
    return res;
}

VkResult DeviceMemory::GetMemoryFdKHR(int* fd) const {
    // Iteration 1a: memory is exported as a real Linux DMA-BUF so that the
    // FD is importable by any consumer that can parse DRM format modifiers,
    // not just another Vulkan instance on the identical driver build.
    const VkMemoryGetFdInfoKHR get_fd_info {
        .sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .pNext      = nullptr,
        .memory     = handle,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    return dld->vkGetMemoryFdKHR(owner, &get_fd_info, fd);
}

rstd::Option<rstd::vec::Vec<VkExtensionProperties>>
EnumerateInstanceExtensionProperties(const GlobalDispatch& dld) {
    rstd::uint32_t num;
    if (dld.vkEnumerateInstanceExtensionProperties(nullptr, &num, nullptr) != VK_SUCCESS) {
        return rstd::None();
    }
    auto properties = rstd::vec::Vec<VkExtensionProperties>::with_capacity(usize(num));
    properties.resize(usize(num), VkExtensionProperties {});
    if (dld.vkEnumerateInstanceExtensionProperties(nullptr, &num, properties.data()) !=
        VK_SUCCESS) {
        return rstd::None();
    }
    return rstd::Some(rstd::move(properties));
}

rstd::Option<rstd::vec::Vec<VkLayerProperties>>
EnumerateInstanceLayerProperties(const GlobalDispatch& dld) {
    rstd::uint32_t num;
    if (dld.vkEnumerateInstanceLayerProperties(&num, nullptr) != VK_SUCCESS) {
        return rstd::None();
    }
    auto properties = rstd::vec::Vec<VkLayerProperties>::with_capacity(usize(num));
    properties.resize(usize(num), VkLayerProperties {});
    if (dld.vkEnumerateInstanceLayerProperties(&num, properties.data()) != VK_SUCCESS) {
        return rstd::None();
    }
    return rstd::Some(rstd::move(properties));
}

} // namespace vvk
