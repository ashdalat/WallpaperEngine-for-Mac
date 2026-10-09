module wavsen.video;

import rstd;
import vvk;
import :vk_device;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace wavsen::video
{

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

bool device_has_ext(const vvk::PhysicalDevice& physical_device, ref<str> name) {
    Vec<VkExtensionProperties> properties;
    if (physical_device.EnumerateDeviceExtensionProperties(properties) != VK_SUCCESS) return false;
    for (const auto& property : properties) {
        if (rstd::ffi::CStr::from_ptr(property.extensionName).to_str().unwrap() == name)
            return true;
    }
    return false;
}

auto vk_error(ref<str> operation, VkResult result) -> String {
    return rstd::format("{}: {}", operation, result);
}

} // namespace

Producer::~Producer() {
    if (device_) (void)device_.WaitIdle();
    if (staging_map_ && staging_mem_) staging_mem_.Unmap();
}

auto Producer::create(u32 width, u32 height) -> Result<Box<Producer>, Error> {
    Error err;
    auto  producer = build_(width, height, None(), &err);
    if (producer.is_none()) return Err(rstd::move(err));
    return Ok(rstd::move(producer).unwrap());
}

auto Producer::create_with_render_node(u32 width, u32 height, ref<str> render_node)
    -> Result<Box<Producer>, Error> {
    Error            err;
    Option<ref<str>> pinned_node = render_node.is_empty() ? None() : Some(render_node);
    auto             producer    = build_(width, height, pinned_node, &err);
    if (producer.is_none()) return Err(rstd::move(err));
    return Ok(rstd::move(producer).unwrap());
}

auto Producer::from_external(ExternalDeviceInfo info) -> Result<Box<Producer>, Error> {
    if (! info.instance || ! info.physical_device || ! info.device || ! info.queue) {
        return Err(Error { "Producer::from_external: missing handle(s)"_str });
    }

    auto self = Box<Producer>::make();
    if (! info.instance_dispatch || ! info.device_dispatch ||
        info.instance_dispatch->instance != info.instance ||
        info.device_dispatch->instance != info.instance ||
        info.device_dispatch->device != info.device) {
        return Err(Error { "Producer::from_external: matching dispatch tables are required"_str });
    }
    self->instance_dispatch_ = *info.instance_dispatch;
    self->device_dispatch_   = *info.device_dispatch;

    self->owns_device_ = false;
    self->instance_ = vvk::Instance(info.instance, self->instance_dispatch_, vvk::borrowed_handle);
    self->phys_     = vvk::PhysicalDevice(info.physical_device, self->instance_dispatch_);
    self->device_   = vvk::Device(info.device, self->device_dispatch_, vvk::borrowed_handle);
    self->queue_    = vvk::Queue(info.queue, self->device_dispatch_);
    self->queue_family_         = info.queue_family_index;
    self->width_                = info.width;
    self->height_               = info.height;
    self->instance_api_version_ = info.api_version;
    self->enabled_inst_exts_    = rstd::move(info.enabled_instance_extensions);
    self->enabled_dev_exts_     = rstd::move(info.enabled_device_extensions);
    self->queue_families_       = rstd::move(info.queue_families);

    if (info.drm_render_major == u32() && info.drm_render_minor == u32() &&
        device_has_ext(self->phys_, "VK_EXT_physical_device_drm"_str)) {
        VkPhysicalDeviceDrmPropertiesEXT drm {};
        drm.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
        VkPhysicalDeviceProperties2 properties {};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &drm;
        self->phys_.GetProperties2KHR(properties);
        if (drm.hasRender) {
            info.drm_render_major = u32(drm.renderMajor);
            info.drm_render_minor = u32(drm.renderMinor);
        }
    }
    if (info.drm_render_fd >= 0) {
        self->drm_render_file_ = rstd::fs::File::from_raw_fd(info.drm_render_fd);
    }
    self->drm_render_major_ = info.drm_render_major;
    self->drm_render_minor_ = info.drm_render_minor;
    return Ok(rstd::move(self));
}

auto Producer::drm_render_node() const -> Option<String> {
    if (drm_render_major_ == u32() && drm_render_minor_ == u32()) return None();

    auto node     = rstd::format("/dev/dri/renderD{}", drm_render_minor_);
    auto path     = rstd::path::PathBuf::from(node.as_str());
    auto metadata = rstd::fs::metadata(path.as_path());
    if (metadata.is_err()) return None();
    auto value = rstd::move(metadata).unwrap();
    if (value.rdev_major() != drm_render_major_ || value.rdev_minor() != drm_render_minor_) {
        return None();
    }
    return Some(rstd::move(node));
}

Option<Box<Producer>> Producer::build_(u32 width, u32 height, Option<ref<str>> render_node,
                                       Error* err) {
    if (width == u32() || height == u32()) {
        fail(err, "Producer: width/height must be non-zero"_str);
        return None();
    }

    auto self     = Box<Producer>::make();
    self->width_  = width;
    self->height_ = height;
    self->enabled_inst_exts_.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
    self->enabled_inst_exts_.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
    self->enabled_inst_exts_.push_back(VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME);
    self->enabled_inst_exts_.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    auto loader = vvk::VulkanLoader::Open();
    if (loader.is_err()) {
        fail(err, "Producer: Vulkan loader unavailable"_str);
        return None();
    }
    self->loader_ = Some(loader.unwrap_unchecked());
    VkApplicationInfo app {};
    app.sType                   = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName        = "wavsen-video";
    app.apiVersion              = VK_API_VERSION_1_3;
    self->instance_api_version_ = u32(app.apiVersion);
    VkInstanceCreateInfo instance_info {};
    instance_info.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app;
    instance_info.enabledExtensionCount =
        static_cast<rstd::uint32_t>(self->enabled_inst_exts_.len().to_primitive());
    instance_info.ppEnabledExtensionNames = self->enabled_inst_exts_.data();
    auto created                          = vvk::Instance::Create(
        self->instance_, self->loader_->global(), instance_info, self->instance_dispatch_);
    if (created.is_err()) {
        const auto error = created.unwrap_err_unchecked();
        fail(err,
             rstd::format("Producer: instance creation failed, kind={}, vk={}",
                          static_cast<int>(error.kind),
                          static_cast<int>(error.api_result)));
        return None();
    }

    auto physical_devices = self->instance_.EnumeratePhysicalDevices();
    if (physical_devices.is_empty()) {
        fail(err, "no Vulkan physical devices found"_str);
        return None();
    }

    const char* required_extensions[] = {
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
    };
    const ref<str> drm_extension = "VK_EXT_physical_device_drm"_str;

    auto      pinning = render_node.is_some();
    rstd::u32 wanted_major {};
    rstd::u32 wanted_minor {};
    if (pinning) {
        auto render_node_path = rstd::path::PathBuf::from(render_node.unwrap());
        auto metadata_result  = rstd::fs::metadata(render_node_path.as_path());
        if (metadata_result.is_err()) {
            fail(err,
                 rstd::format("Producer: metadata({}) failed: {}",
                              render_node.unwrap(),
                              rstd::move(metadata_result).unwrap_err_unchecked()));
            return None();
        }
        auto metadata = rstd::move(metadata_result).unwrap_unchecked();
        wanted_major  = metadata.rdev_major();
        wanted_minor  = metadata.rdev_minor();
    }

    bool have_drm_extension = false;
    for (auto& physical_device : physical_devices) {
        bool supported = true;
        for (const char* extension : required_extensions) {
            if (! device_has_ext(physical_device,
                                 rstd::ffi::CStr::from_ptr(extension).to_str().unwrap())) {
                supported = false;
                break;
            }
        }
        if (! supported) continue;

        bool device_has_drm = device_has_ext(physical_device, drm_extension);
        if (pinning) {
            if (! device_has_drm) continue;
            VkPhysicalDeviceDrmPropertiesEXT drm {};
            drm.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
            VkPhysicalDeviceProperties2 properties {};
            properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties.pNext = &drm;
            physical_device.GetProperties2KHR(properties);
            if (! drm.hasRender) continue;
            if (drm.renderMajor != wanted_major.to_primitive() ||
                drm.renderMinor != wanted_minor.to_primitive()) {
                continue;
            }
        }

        self->phys_        = rstd::move(physical_device);
        have_drm_extension = device_has_drm;
        break;
    }
    if (! self->phys_) {
        if (pinning) {
            fail(err,
                 rstd::format("Producer: no Vulkan device matches render_node {}",
                              render_node.unwrap()));
        } else {
            fail(err, "no physical device supports the DMA-BUF export extension set"_str);
        }
        return None();
    }

    auto queue_properties = self->phys_.GetQueueFamilyProperties();
    if (queue_properties.is_empty()) {
        fail(err, "no queue families"_str);
        return None();
    }
    self->queue_families_.reserve(queue_properties.len());
    bool picked_queue = false;
    for (usize i {}; i < queue_properties.len(); ++i) {
        QueueFamily queue_family {
            .index      = rstd::as_cast<u32>(i),
            .flags      = queue_properties[i].queueFlags,
            .video_caps = u32(),
        };
        self->queue_families_.push(rstd::move(queue_family));
        if (! picked_queue &&
            (queue_properties[i].queueFlags &
             (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT))) {
            self->queue_family_ = rstd::as_cast<u32>(i);
            picked_queue        = true;
        }
    }
    if (! picked_queue) {
        fail(err, "no graphics/compute/transfer queue family"_str);
        return None();
    }

    float default_priority = 1.0f;
    auto  queue_infos = Vec<VkDeviceQueueCreateInfo>::with_capacity(self->queue_families_.len());
    for (const auto& queue_family : self->queue_families_) {
        VkDeviceQueueCreateInfo queue_info {};
        queue_info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family.index.to_primitive();
        queue_info.queueCount       = 1;
        queue_info.pQueuePriorities = &default_priority;
        queue_infos.push(rstd::move(queue_info));
    }

    for (const char* extension : required_extensions) self->enabled_dev_exts_.push_back(extension);
    if (have_drm_extension) self->enabled_dev_exts_.push_back("VK_EXT_physical_device_drm");
    const char* optional_extensions[] = {
        "VK_KHR_video_queue",       "VK_KHR_video_decode_queue", "VK_KHR_video_decode_h264",
        "VK_KHR_video_decode_h265", "VK_KHR_video_decode_av1",   "VK_EXT_external_memory_host",
        "VK_KHR_push_descriptor",   "VK_KHR_synchronization2",   "VK_KHR_timeline_semaphore",
        "VK_EXT_descriptor_buffer", "VK_EXT_shader_object",
    };
    for (const char* extension : optional_extensions) {
        if (device_has_ext(self->phys_, rstd::ffi::CStr::from_ptr(extension).to_str().unwrap()))
            self->enabled_dev_exts_.push_back(extension);
    }

    VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr_features {};
    ycbcr_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
    VkPhysicalDeviceVulkan12Features features12 {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan13Features features13 {};
    features13.sType     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    ycbcr_features.pNext = &features12;
    features12.pNext     = &features13;
    VkPhysicalDeviceFeatures2 features {};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &ycbcr_features;
    self->phys_.GetFeatures2KHR(features);

    VkPhysicalDeviceSamplerYcbcrConversionFeatures wanted_ycbcr {};
    wanted_ycbcr.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
    wanted_ycbcr.samplerYcbcrConversion = ycbcr_features.samplerYcbcrConversion;
    VkPhysicalDeviceVulkan12Features wanted12 {};
    wanted12.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    wanted12.timelineSemaphore   = features12.timelineSemaphore;
    wanted12.bufferDeviceAddress = features12.bufferDeviceAddress;
    VkPhysicalDeviceVulkan13Features wanted13 {};
    wanted13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    wanted13.synchronization2 = features13.synchronization2;
    wanted13.maintenance4     = features13.maintenance4;
    wanted_ycbcr.pNext        = &wanted12;
    wanted12.pNext            = &wanted13;
    VkPhysicalDeviceFeatures2 wanted_features {};
    wanted_features.sType                      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    wanted_features.pNext                      = &wanted_ycbcr;
    wanted_features.features.samplerAnisotropy = features.features.samplerAnisotropy;

    VkDeviceCreateInfo device_info {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &wanted_features;
    device_info.queueCreateInfoCount =
        static_cast<rstd::uint32_t>(queue_infos.len().to_primitive());
    device_info.pQueueCreateInfos = queue_infos.data();
    device_info.enabledExtensionCount =
        static_cast<rstd::uint32_t>(self->enabled_dev_exts_.len().to_primitive());
    device_info.ppEnabledExtensionNames = self->enabled_dev_exts_.data();
    auto made_device                    = vvk::Device::Create(
        self->device_, *self->phys_, self->instance_dispatch_, device_info, self->device_dispatch_);
    if (made_device.is_err()) {
        const auto error = made_device.unwrap_err_unchecked();
        fail(err,
             rstd::format("Producer: device creation failed, kind={}, vk={}",
                          static_cast<int>(error.kind),
                          static_cast<int>(error.api_result)));
        return None();
    }
    self->queue_ = self->device_.GetQueue(self->queue_family_.to_primitive());

    VkPhysicalDeviceIDProperties id_properties {};
    id_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceDrmPropertiesEXT drm_properties {};
    drm_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
    if (have_drm_extension) id_properties.pNext = &drm_properties;
    VkPhysicalDeviceProperties2 properties {};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &id_properties;
    self->phys_.GetProperties2KHR(properties);
    rstd::mem::memcpy(self->device_uuid_, id_properties.deviceUUID, usize(16));
    rstd::mem::memcpy(self->driver_uuid_, id_properties.driverUUID, usize(16));
    self->have_uuid_ = true;
    if (have_drm_extension && drm_properties.hasRender) {
        self->drm_render_major_ = u32(drm_properties.renderMajor);
        self->drm_render_minor_ = u32(drm_properties.renderMinor);
    }

    if (self->drm_render_minor_ != u32()) {
        for (int i = 128; i < 192; ++i) {
            auto path    = rstd::path::PathBuf::from(rstd::format("/dev/dri/renderD{}", i));
            auto options = rstd::fs::File::options();
            auto file    = options.read(true).write(true).open(path.as_path());
            if (file.is_ok()) {
                self->drm_render_file_ = rstd::move(file).unwrap_unchecked();
                break;
            }
        }
    }

    VkCommandPoolCreateInfo pool_info {};
    pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = self->queue_family_.to_primitive();
    if (VkResult result = self->device_.CreateCommandPool(pool_info, self->cmd_pool_);
        result != VK_SUCCESS) {
        fail(err, vk_error("vkCreateCommandPool"_str, result));
        return None();
    }
    if (VkResult result = self->cmd_pool_.Allocate(
            usize(1), VK_COMMAND_BUFFER_LEVEL_PRIMARY, self->command_buffers_);
        result != VK_SUCCESS) {
        fail(err, vk_error("vkAllocateCommandBuffers"_str, result));
        return None();
    }
    self->cmd_ = vvk::CommandBuffer(self->command_buffers_[usize()], self->device_dispatch_);

    VkFenceCreateInfo fence_info {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (VkResult result = self->device_.CreateFence(fence_info, self->done_fence_);
        result != VK_SUCCESS) {
        fail(err, vk_error("vkCreateFence"_str, result));
        return None();
    }

    VkExportSemaphoreCreateInfo export_semaphore {};
    export_semaphore.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    export_semaphore.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    VkSemaphoreCreateInfo semaphore_info {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_info.pNext = &export_semaphore;
    if (VkResult result = self->device_.CreateSemaphore(semaphore_info, self->signal_sem_);
        result != VK_SUCCESS) {
        fail(err, vk_error("vkCreateSemaphore"_str, result));
        return None();
    }

    self->staging_size_ = static_cast<VkDeviceSize>(width.to_primitive()) *
                          static_cast<VkDeviceSize>(height.to_primitive()) * 4;
    VkBufferCreateInfo buffer_info {};
    buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size        = self->staging_size_;
    buffer_info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (VkResult result = self->device_.CreateBuffer(buffer_info, self->staging_buf_);
        result != VK_SUCCESS) {
        fail(err, vk_error("vkCreateBuffer(staging)"_str, result));
        return None();
    }

    auto memory_requirements = self->device_.GetBufferMemoryRequirements(*self->staging_buf_);
    auto memory_properties   = self->phys_.GetMemoryProperties().memoryProperties;
    rstd::uint32_t host_type = u32::MAX.to_primitive();
    for (rstd::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        auto flags = memory_properties.memoryTypes[i].propertyFlags;
        if ((memory_requirements.memoryTypeBits & (1u << i)) &&
            (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            host_type = i;
            break;
        }
    }
    if (host_type == u32::MAX.to_primitive()) {
        fail(err, "no HOST_VISIBLE|COHERENT memory type for staging"_str);
        return None();
    }

    VkMemoryAllocateInfo allocate_info {};
    allocate_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize  = memory_requirements.size;
    allocate_info.memoryTypeIndex = host_type;
    if (VkResult result = self->device_.AllocateMemory(allocate_info, self->staging_mem_);
        result != VK_SUCCESS) {
        fail(err, vk_error("vkAllocateMemory(staging)"_str, result));
        return None();
    }
    if (VkResult result = self->staging_buf_.BindMemory(*self->staging_mem_, 0);
        result != VK_SUCCESS) {
        fail(err, vk_error("vkBindBufferMemory(staging)"_str, result));
        return None();
    }
    if (VkResult result = self->staging_mem_.Map(0, VK_WHOLE_SIZE, &self->staging_map_);
        result != VK_SUCCESS) {
        fail(err, vk_error("vkMapMemory(staging)"_str, result));
        return None();
    }

    return Some(rstd::move(self));
}

auto Producer::upload_into(VkImage target, u32 target_width, u32 target_height,
                           const rstd::uint8_t* data, usize size) -> Result<int, Error> {
    Error err;
    int   fd = upload_into_(target, target_width, target_height, data, size, &err);
    if (fd < 0) return Err(rstd::move(err));
    return Ok(fd);
}

int Producer::upload_into_(VkImage target, u32 target_width, u32 target_height,
                           const rstd::uint8_t* data, usize size, Error* err) {
    if (target == VK_NULL_HANDLE) {
        fail(err, "upload_into: target VkImage is null"_str);
        return -1;
    }
    if (! owns_device_ || ! staging_buf_) {
        fail(err,
             "upload_into: Producer has no staging buffer "_str
             "(from_external Producers are decode-only)");
        return -1;
    }
    if (static_cast<VkDeviceSize>(size.to_primitive()) != staging_size_) {
        fail(err, "upload_into: size mismatch"_str);
        return -1;
    }

    if (fence_pending_) {
        if (VkResult result = done_fence_.Wait(1'000'000'000ull); result != VK_SUCCESS) {
            fail(err, vk_error("vkWaitForFences(prev upload)"_str, result));
            return -1;
        }
        if (VkResult result = done_fence_.Reset(); result != VK_SUCCESS) {
            fail(err, vk_error("vkResetFences"_str, result));
            return -1;
        }
        fence_pending_ = false;
    }

    rstd::mem::memcpy(staging_map_, data, size);
    if (VkResult result = cmd_.Reset(); result != VK_SUCCESS) {
        fail(err, vk_error("vkResetCommandBuffer"_str, result));
        return -1;
    }

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (VkResult result = cmd_.Begin(begin_info); result != VK_SUCCESS) {
        fail(err, vk_error("vkBeginCommandBuffer"_str, result));
        return -1;
    }

    VkImageMemoryBarrier to_destination {};
    to_destination.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_destination.srcAccessMask       = 0;
    to_destination.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_destination.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    to_destination.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_destination.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_destination.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_destination.image               = target;
    to_destination.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    cmd_.PipelineBarrier(
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, to_destination);

    VkBufferImageCopy copy {};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { target_width.to_primitive(), target_height.to_primitive(), 1 };
    cmd_.CopyBufferToImage(*staging_buf_, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copy);

    VkImageMemoryBarrier to_foreign {};
    to_foreign.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_foreign.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_foreign.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_foreign.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    to_foreign.srcQueueFamilyIndex = queue_family_.to_primitive();
    to_foreign.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
    to_foreign.image               = target;
    to_foreign.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    cmd_.PipelineBarrier(
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, to_foreign);

    if (VkResult result = cmd_.End(); result != VK_SUCCESS) {
        fail(err, vk_error("vkEndCommandBuffer"_str, result));
        return -1;
    }

    VkCommandBuffer raw_command_buffer = *cmd_;
    VkSemaphore     raw_semaphore      = *signal_sem_;
    VkSubmitInfo    submit_info {};
    submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount   = 1;
    submit_info.pCommandBuffers      = &raw_command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores    = &raw_semaphore;
    if (VkResult result = queue_.Submit(submit_info, *done_fence_); result != VK_SUCCESS) {
        fail(err, vk_error("vkQueueSubmit"_str, result));
        return -1;
    }
    fence_pending_ = true;

    VkSemaphoreGetFdInfoKHR fd_info {};
    fd_info.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    fd_info.semaphore  = *signal_sem_;
    fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    int sync_fd        = -1;
    if (VkResult result = device_.GetSemaphoreFdKHR(fd_info, &sync_fd); result != VK_SUCCESS) {
        fail(err, vk_error("vkGetSemaphoreFdKHR"_str, result));
        return -1;
    }
    return sync_fd;
}

} // namespace wavsen::video
