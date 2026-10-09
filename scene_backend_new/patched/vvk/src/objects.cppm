module;

#include <rstd/macro.hpp>

export module vvk:objects;

import rstd;
import :ffi.vulkan;
import :handle;
import :dispatch;

using namespace rstd::prelude;

namespace
{
constexpr auto vk_count(usize value) noexcept -> rstd::uint32_t {
    return rstd::as_cast<rstd::uint32_t>(value);
}
} // namespace

export namespace vvk
{

class PhysicalDevice;

class Instance : public Handle<VkInstance, NoOwner, InstanceDispatch> {
    using Handle<VkInstance, NoOwner, InstanceDispatch>::Handle;

public:
    static auto Create(Instance&, const GlobalDispatch&, const VkInstanceCreateInfo&,
                       InstanceDispatch&) -> Result<empty, DispatchError>;

    rstd::vec::Vec<PhysicalDevice> EnumeratePhysicalDevices() const noexcept;

    DebugUtilsMessenger
    CreateDebugUtilsMessenger(const VkDebugUtilsMessengerCreateInfoEXT&) const noexcept;

    const InstanceDispatch& Dispatch() const noexcept { return *dld; }
};

class Buffer : public Handle<VkBuffer, VkDevice, DeviceDispatch> {
    using Handle<VkBuffer, VkDevice, DeviceDispatch>::Handle;

public:
    VkResult BindMemory(VkDeviceMemory memory, VkDeviceSize offset) const noexcept;
};

class Image : public Handle<VkImage, VkDevice, DeviceDispatch> {
    using Handle<VkImage, VkDevice, DeviceDispatch>::Handle;

public:
    VkResult BindMemory(VkDeviceMemory memory, VkDeviceSize offset) const noexcept;
};

class ImageView : public Handle<VkImageView, VkDevice, DeviceDispatch> {
    using Handle<VkImageView, VkDevice, DeviceDispatch>::Handle;
};

class Queue : public Handle<VkQueue, NoOwnerLife, DeviceDispatch> {
    using Handle<VkQueue, NoOwnerLife, DeviceDispatch>::Handle;

public:
    VkResult Submit(slice<VkSubmitInfo> submit_infos,
                    VkFence             fence = VK_NULL_HANDLE) const noexcept {
        return dld->vkQueueSubmit(
            handle, vk_count(submit_infos.len()), submit_infos.as_raw_ptr(), fence);
    }

    VkResult Submit(const VkSubmitInfo& submit_info,
                    VkFence             fence = VK_NULL_HANDLE) const noexcept {
        return Submit(slice<VkSubmitInfo>::from_raw_parts(&submit_info, usize(1)), fence);
    }

    VkResult Present(const VkPresentInfoKHR& present_info) const noexcept {
        return dld->vkQueuePresentKHR(handle, &present_info);
    }
};

class SwapchainKHR : public Handle<VkSwapchainKHR, VkDevice, DeviceDispatch> {
    using Handle<VkSwapchainKHR, VkDevice, DeviceDispatch>::Handle;

public:
    VkResult GetImages(rstd::vec::Vec<VkImage>&) const;
};

class PhysicalDevice : public Handle<VkPhysicalDevice, NoOwnerLife, InstanceDispatch> {
    using Handle<VkPhysicalDevice, NoOwnerLife, InstanceDispatch>::Handle;

public:
    VkPhysicalDeviceProperties GetProperties() const noexcept;

    void GetProperties2KHR(VkPhysicalDeviceProperties2KHR&) const noexcept;

    VkPhysicalDeviceFeatures GetFeatures() const noexcept;

    void GetFeatures2KHR(VkPhysicalDeviceFeatures2KHR&) const noexcept;

    VkFormatProperties GetFormatProperties(VkFormat) const noexcept;

    VkResult EnumerateDeviceExtensionProperties(rstd::vec::Vec<VkExtensionProperties>&) const;

    rstd::vec::Vec<VkQueueFamilyProperties> GetQueueFamilyProperties() const;

    VkResult GetSurfaceSupportKHR(rstd::uint32_t queue_family_index, VkSurfaceKHR, bool&) const;

    VkResult GetSurfaceCapabilitiesKHR(VkSurfaceKHR, VkSurfaceCapabilitiesKHR&) const noexcept;

    VkResult GetSurfaceFormatsKHR(VkSurfaceKHR surface, rstd::vec::Vec<VkSurfaceFormatKHR>&) const;

    VkResult GetSurfacePresentModesKHR(VkSurfaceKHR surface,
                                       rstd::vec::Vec<VkPresentModeKHR>&) const;

    VkPhysicalDeviceMemoryProperties2
    GetMemoryProperties(void* next_structures = nullptr) const noexcept;
};

class CommandPool : public Handle<VkCommandPool, VkDevice, DeviceDispatch> {
    using Handle<VkCommandPool, VkDevice, DeviceDispatch>::Handle;

public:
    VkResult Allocate(usize num_buffers, VkCommandBufferLevel level, CommandBuffers&) const;
};

class DeviceMemory : public Handle<VkDeviceMemory, VkDevice, DeviceDispatch> {
    using Handle<VkDeviceMemory, VkDevice, DeviceDispatch>::Handle;

public:
    VkResult GetMemoryFdKHR(int*) const;

    VkResult Map(VkDeviceSize offset, VkDeviceSize size, rstd::uint8_t** data) const {
        return (dld->vkMapMemory(owner, handle, offset, size, 0, (void**)data));
    }

    void Unmap() const noexcept { dld->vkUnmapMemory(owner, handle); }
};

class Framebuffer : public Handle<VkFramebuffer, VkDevice, DeviceDispatch> {
    using Handle<VkFramebuffer, VkDevice, DeviceDispatch>::Handle;
};

class ShaderModule : public Handle<VkShaderModule, VkDevice, DeviceDispatch> {
    using Handle<VkShaderModule, VkDevice, DeviceDispatch>::Handle;
};

class Fence : public Handle<VkFence, VkDevice, DeviceDispatch> {
    using Handle<VkFence, VkDevice, DeviceDispatch>::Handle;

public:
    VkResult Wait(rstd::uint64_t timeout = ~rstd::uint64_t(0)) const noexcept {
        return dld->vkWaitForFences(owner, 1, &handle, true, timeout);
    }

    VkResult GetStatus() const noexcept { return dld->vkGetFenceStatus(owner, handle); }

    VkResult Reset() const { return dld->vkResetFences(owner, 1, &handle); }
};

class Semaphore : public Handle<VkSemaphore, VkDevice, DeviceDispatch> {
    using Handle<VkSemaphore, VkDevice, DeviceDispatch>::Handle;

public:
    VkResult GetCounter(rstd::uint64_t* value) const {
        return dld->vkGetSemaphoreCounterValueKHR(owner, handle, value);
    }

    VkResult Wait(rstd::uint64_t value, rstd::uint64_t timeout = ~rstd::uint64_t(0)) const {
        const VkSemaphoreWaitInfoKHR wait_info {
            .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR,
            .pNext          = nullptr,
            .flags          = 0,
            .semaphoreCount = 1,
            .pSemaphores    = &handle,
            .pValues        = &value,
        };
        return dld->vkWaitSemaphoresKHR(owner, &wait_info, timeout);
    }
};

class Device : public Handle<VkDevice, NoOwner, DeviceDispatch> {
    using Handle<VkDevice, NoOwner, DeviceDispatch>::Handle;

public:
    static auto Create(Device&, VkPhysicalDevice, const InstanceDispatch&,
                       const VkDeviceCreateInfo&, DeviceDispatch&) -> Result<empty, DispatchError>;

    Queue GetQueue(rstd::uint32_t family_index) const noexcept;

    VkMemoryRequirements GetImageMemoryRequirements(VkImage image) const noexcept;
    VkMemoryRequirements2
    GetImageMemoryRequirements2(const VkImageMemoryRequirementsInfo2& info,
                                void* next_structures = nullptr) const noexcept;
    VkMemoryRequirements GetBufferMemoryRequirements(VkBuffer buffer) const noexcept;
    VkMemoryRequirements2
    GetBufferMemoryRequirements2(const VkBufferMemoryRequirementsInfo2& info,
                                 void* next_structures = nullptr) const noexcept;

    VkSubresourceLayout
    GetImageSubresourceLayout(VkImage image, const VkImageSubresource& subresource) const noexcept;

    VkResult GetImageDrmFormatModifierPropertiesEXT(
        VkImage image, VkImageDrmFormatModifierPropertiesEXT* props) const noexcept;
    VkResult GetMemoryFdPropertiesKHR(VkExternalMemoryHandleTypeFlagBits handle_type, int fd,
                                      VkMemoryFdPropertiesKHR& properties) const noexcept;
    VkResult BindImageMemory2(slice<VkBindImageMemoryInfo> bindings) const noexcept;

    VkResult AllocateMemory(const VkMemoryAllocateInfo& ai, DeviceMemory&) const noexcept;

    VkResult CreateBuffer(const VkBufferCreateInfo& ci, Buffer&) const noexcept;
    VkResult CreateCommandPool(const VkCommandPoolCreateInfo& ci, CommandPool&) const;
    VkResult CreateDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& ci,
                                       DescriptorSetLayout&) const noexcept;
    VkResult CreateGraphicsPipeline(const VkGraphicsPipelineCreateInfo& ci,
                                    Pipeline&) const noexcept;
    VkResult CreateComputePipeline(const VkComputePipelineCreateInfo& ci, Pipeline&) const noexcept;

    VkResult CreateRenderPass(const VkRenderPassCreateInfo& ci, RenderPass&) const noexcept;

    VkResult CreatePipelineLayout(const VkPipelineLayoutCreateInfo& ci,
                                  PipelineLayout&) const noexcept;

    VkResult CreateSwapchainKHR(const VkSwapchainCreateInfoKHR& ci, SwapchainKHR&) const noexcept;

    VkResult CreateShaderModule(const VkShaderModuleCreateInfo& ci, ShaderModule&) const noexcept;

    VkResult CreateSemaphore(const VkSemaphoreCreateInfo& ci, Semaphore&) const noexcept;
    VkResult GetSemaphoreFdKHR(const VkSemaphoreGetFdInfoKHR& gi, int* fd) const noexcept;

    VkResult CreateImage(const VkImageCreateInfo& ci, Image&) const noexcept;

    VkResult CreateImageView(const VkImageViewCreateInfo& ci, ImageView&) const noexcept;

    VkResult CreateFramebuffer(const VkFramebufferCreateInfo& ci, Framebuffer&) const noexcept;

    VkResult CreateFence(const VkFenceCreateInfo& ci, Fence&) const noexcept;

    VkResult CreateSampler(const VkSamplerCreateInfo& ci, Sampler&) const noexcept;

    VkResult WaitIdle() const noexcept { return dld->vkDeviceWaitIdle(handle); }

    VkResult AcquireNextImageKHR(VkSwapchainKHR swapchain, rstd::uint64_t timeout,
                                 VkSemaphore semaphore, VkFence fence,
                                 rstd::uint32_t* image_index) const noexcept {
        return dld->vkAcquireNextImageKHR(
            handle, swapchain, timeout, semaphore, fence, image_index);
    }

    const DeviceDispatch& Dispatch() const noexcept { return *dld; }
};

class CommandBuffer : public Handle<VkCommandBuffer, NoOwnerLife, DeviceDispatch> {
    using Handle<VkCommandBuffer, NoOwnerLife, DeviceDispatch>::Handle;

public:
    VkResult Begin(const VkCommandBufferBeginInfo& begin_info) const {
        return dld->vkBeginCommandBuffer(handle, &begin_info);
    }

    VkResult End() const { return dld->vkEndCommandBuffer(handle); }

    VkResult Reset(VkCommandBufferResetFlags flags = 0) const {
        return dld->vkResetCommandBuffer(handle, flags);
    }

    void BeginRenderPass(const VkRenderPassBeginInfo& renderpass_bi,
                         VkSubpassContents            contents) const noexcept {
        dld->vkCmdBeginRenderPass(handle, &renderpass_bi, contents);
    }

    void EndRenderPass() const noexcept { dld->vkCmdEndRenderPass(handle); }

    void BeginQuery(VkQueryPool query_pool, rstd::uint32_t query,
                    VkQueryControlFlags flags) const noexcept {
        dld->vkCmdBeginQuery(handle, query_pool, query, flags);
    }

    void EndQuery(VkQueryPool query_pool, rstd::uint32_t query) const noexcept {
        dld->vkCmdEndQuery(handle, query_pool, query);
    }

    void BindDescriptorSets(VkPipelineBindPoint bind_point, VkPipelineLayout layout,
                            rstd::uint32_t first, slice<VkDescriptorSet> sets,
                            slice<rstd::uint32_t> dynamic_offsets) const noexcept {
        dld->vkCmdBindDescriptorSets(handle,
                                     bind_point,
                                     layout,
                                     first,
                                     vk_count(sets.len()),
                                     sets.as_raw_ptr(),
                                     vk_count(dynamic_offsets.len()),
                                     dynamic_offsets.as_raw_ptr());
    }

    void PushDescriptorSetKHR(VkPipelineBindPoint bind_point, VkPipelineLayout layout,
                              rstd::uint32_t              set,
                              slice<VkWriteDescriptorSet> wsets) const noexcept {
        rstd_assert(wsets[usize()].sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        dld->vkCmdPushDescriptorSetKHR(
            handle, bind_point, layout, set, vk_count(wsets.len()), wsets.as_raw_ptr());
    }

    void PushDescriptorSetKHR(VkPipelineBindPoint bind_point, VkPipelineLayout layout,
                              rstd::uint32_t set, const VkWriteDescriptorSet& wset) const noexcept {
        PushDescriptorSetKHR(
            bind_point, layout, set, slice<VkWriteDescriptorSet>::from_raw_parts(&wset, usize(1)));
    }

    void PushDescriptorSetWithTemplateKHR(VkDescriptorUpdateTemplateKHR update_template,
                                          VkPipelineLayout layout, rstd::uint32_t set,
                                          const void* data) const noexcept {
        dld->vkCmdPushDescriptorSetWithTemplateKHR(handle, update_template, layout, set, data);
    }

    void BindPipeline(VkPipelineBindPoint bind_point, VkPipeline pipeline) const noexcept {
        dld->vkCmdBindPipeline(handle, bind_point, pipeline);
    }

    void BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset,
                         VkIndexType index_type) const noexcept {
        dld->vkCmdBindIndexBuffer(handle, buffer, offset, index_type);
    }

    void BindVertexBuffers(rstd::uint32_t first, rstd::uint32_t count, const VkBuffer* buffers,
                           const VkDeviceSize* offsets) const noexcept {
        dld->vkCmdBindVertexBuffers(handle, first, count, buffers, offsets);
    }

    void BindVertexBuffer(rstd::uint32_t binding, VkBuffer buffer,
                          VkDeviceSize offset) const noexcept {
        BindVertexBuffers(binding, 1, &buffer, &offset);
    }

    void Draw(rstd::uint32_t vertex_count, rstd::uint32_t instance_count,
              rstd::uint32_t first_vertex, rstd::uint32_t first_instance) const noexcept {
        dld->vkCmdDraw(handle, vertex_count, instance_count, first_vertex, first_instance);
    }

    void DrawIndexed(rstd::uint32_t index_count, rstd::uint32_t instance_count,
                     rstd::uint32_t first_index, rstd::int32_t vertex_offset,
                     rstd::uint32_t first_instance) const noexcept {
        dld->vkCmdDrawIndexed(
            handle, index_count, instance_count, first_index, vertex_offset, first_instance);
    }

    void ClearColorImage(VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor,
                         slice<VkImageSubresourceRange> ranges) const noexcept {
        return dld->vkCmdClearColorImage(
            handle, image, imageLayout, pColor, vk_count(ranges.len()), ranges.as_raw_ptr());
    }

    void ClearColorImage(VkImage image, VkImageLayout image_layout, const VkClearColorValue* color,
                         const VkImageSubresourceRange& range) const noexcept {
        ClearColorImage(image,
                        image_layout,
                        color,
                        slice<VkImageSubresourceRange>::from_raw_parts(&range, usize(1)));
    }

    void ClearAttachments(slice<VkClearAttachment> attachments,
                          slice<VkClearRect>       rects) const noexcept {
        dld->vkCmdClearAttachments(handle,
                                   vk_count(attachments.len()),
                                   attachments.as_raw_ptr(),
                                   vk_count(rects.len()),
                                   rects.as_raw_ptr());
    }

    void BlitImage(VkImage src_image, VkImageLayout src_layout, VkImage dst_image,
                   VkImageLayout dst_layout, slice<VkImageBlit> regions,
                   VkFilter filter) const noexcept {
        dld->vkCmdBlitImage(handle,
                            src_image,
                            src_layout,
                            dst_image,
                            dst_layout,
                            vk_count(regions.len()),
                            regions.as_raw_ptr(),
                            filter);
    }

    void BlitImage(VkImage src_image, VkImageLayout src_layout, VkImage dst_image,
                   VkImageLayout dst_layout, const VkImageBlit& region,
                   VkFilter filter) const noexcept {
        BlitImage(src_image,
                  src_layout,
                  dst_image,
                  dst_layout,
                  slice<VkImageBlit>::from_raw_parts(&region, usize(1)),
                  filter);
    }

    void ResolveImage(VkImage src_image, VkImageLayout src_layout, VkImage dst_image,
                      VkImageLayout dst_layout, slice<VkImageResolve> regions) {
        dld->vkCmdResolveImage(handle,
                               src_image,
                               src_layout,
                               dst_image,
                               dst_layout,
                               vk_count(regions.len()),
                               regions.as_raw_ptr());
    }

    void ResolveImage(VkImage src_image, VkImageLayout src_layout, VkImage dst_image,
                      VkImageLayout dst_layout, const VkImageResolve& region) {
        ResolveImage(src_image,
                     src_layout,
                     dst_image,
                     dst_layout,
                     slice<VkImageResolve>::from_raw_parts(&region, usize(1)));
    }

    void Dispatch(rstd::uint32_t x, rstd::uint32_t y, rstd::uint32_t z) const noexcept {
        dld->vkCmdDispatch(handle, x, y, z);
    }

    void PipelineBarrier(VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
                         VkDependencyFlags dependency_flags, slice<VkMemoryBarrier> memory_barriers,
                         slice<VkBufferMemoryBarrier> buffer_barriers,
                         slice<VkImageMemoryBarrier>  image_barriers) const noexcept {
        dld->vkCmdPipelineBarrier(handle,
                                  src_stage_mask,
                                  dst_stage_mask,
                                  dependency_flags,
                                  vk_count(memory_barriers.len()),
                                  memory_barriers.as_raw_ptr(),
                                  vk_count(buffer_barriers.len()),
                                  buffer_barriers.as_raw_ptr(),
                                  vk_count(image_barriers.len()),
                                  image_barriers.as_raw_ptr());
    }

    void PipelineBarrier2(const VkDependencyInfo& dependency_info) const noexcept {
        dld->vkCmdPipelineBarrier2(handle, &dependency_info);
    }

    void PipelineBarrier(VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
                         VkDependencyFlags dependency_flags = 0) const noexcept {
        PipelineBarrier(src_stage_mask, dst_stage_mask, dependency_flags, {}, {}, {});
    }

    void PipelineBarrier(VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
                         VkDependencyFlags      dependency_flags,
                         const VkMemoryBarrier& memory_barrier) const noexcept {
        PipelineBarrier(src_stage_mask,
                        dst_stage_mask,
                        dependency_flags,
                        slice<VkMemoryBarrier>::from_raw_parts(&memory_barrier, usize(1)),
                        {},
                        {});
    }

    void PipelineBarrier(VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
                         VkDependencyFlags            dependency_flags,
                         const VkBufferMemoryBarrier& buffer_barrier) const noexcept {
        PipelineBarrier(src_stage_mask,
                        dst_stage_mask,
                        dependency_flags,
                        {},
                        slice<VkBufferMemoryBarrier>::from_raw_parts(&buffer_barrier, usize(1)),
                        {});
    }

    void PipelineBarrier(VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
                         VkDependencyFlags           dependency_flags,
                         const VkImageMemoryBarrier& image_barrier) const noexcept {
        PipelineBarrier(src_stage_mask,
                        dst_stage_mask,
                        dependency_flags,
                        {},
                        {},
                        slice<VkImageMemoryBarrier>::from_raw_parts(&image_barrier, usize(1)));
    }

    void CopyBufferToImage(VkBuffer src_buffer, VkImage dst_image, VkImageLayout dst_image_layout,
                           slice<VkBufferImageCopy> regions) const noexcept {
        dld->vkCmdCopyBufferToImage(handle,
                                    src_buffer,
                                    dst_image,
                                    dst_image_layout,
                                    vk_count(regions.len()),
                                    regions.as_raw_ptr());
    }

    void CopyBufferToImage(VkBuffer src_buffer, VkImage dst_image, VkImageLayout dst_image_layout,
                           const VkBufferImageCopy& region) const noexcept {
        CopyBufferToImage(src_buffer,
                          dst_image,
                          dst_image_layout,
                          slice<VkBufferImageCopy>::from_raw_parts(&region, usize(1)));
    }

    void CopyBuffer(VkBuffer src_buffer, VkBuffer dst_buffer,
                    slice<VkBufferCopy> regions) const noexcept {
        dld->vkCmdCopyBuffer(
            handle, src_buffer, dst_buffer, vk_count(regions.len()), regions.as_raw_ptr());
    }

    void CopyBuffer(VkBuffer src_buffer, VkBuffer dst_buffer,
                    const VkBufferCopy& region) const noexcept {
        CopyBuffer(src_buffer, dst_buffer, slice<VkBufferCopy>::from_raw_parts(&region, usize(1)));
    }

    void CopyImage(VkImage src_image, VkImageLayout src_layout, VkImage dst_image,
                   VkImageLayout dst_layout, slice<VkImageCopy> regions) const noexcept {
        dld->vkCmdCopyImage(handle,
                            src_image,
                            src_layout,
                            dst_image,
                            dst_layout,
                            vk_count(regions.len()),
                            regions.as_raw_ptr());
    }

    void CopyImage(VkImage src_image, VkImageLayout src_layout, VkImage dst_image,
                   VkImageLayout dst_layout, const VkImageCopy& region) const noexcept {
        CopyImage(src_image,
                  src_layout,
                  dst_image,
                  dst_layout,
                  slice<VkImageCopy>::from_raw_parts(&region, usize(1)));
    }

    void CopyImageToBuffer(VkImage src_image, VkImageLayout src_layout, VkBuffer dst_buffer,
                           slice<VkBufferImageCopy> regions) const noexcept {
        dld->vkCmdCopyImageToBuffer(handle,
                                    src_image,
                                    src_layout,
                                    dst_buffer,
                                    vk_count(regions.len()),
                                    regions.as_raw_ptr());
    }

    void CopyImageToBuffer(VkImage src_image, VkImageLayout src_layout, VkBuffer dst_buffer,
                           const VkBufferImageCopy& region) const noexcept {
        CopyImageToBuffer(src_image,
                          src_layout,
                          dst_buffer,
                          slice<VkBufferImageCopy>::from_raw_parts(&region, usize(1)));
    }

    void FillBuffer(VkBuffer dst_buffer, VkDeviceSize dst_offset, VkDeviceSize size,
                    rstd::uint32_t data) const noexcept {
        dld->vkCmdFillBuffer(handle, dst_buffer, dst_offset, size, data);
    }

    void PushConstants(VkPipelineLayout layout, VkShaderStageFlags flags, rstd::uint32_t offset,
                       rstd::uint32_t size, const void* values) const noexcept {
        dld->vkCmdPushConstants(handle, layout, flags, offset, size, values);
    }

    template<typename T>
    void PushConstants(VkPipelineLayout layout, VkShaderStageFlags flags,
                       const T& data) const noexcept {
        static_assert(rstd::mtp::triv_copy<T>, "<data> is not trivially copyable");
        dld->vkCmdPushConstants(
            handle, layout, flags, 0, static_cast<rstd::uint32_t>(sizeof(T)), &data);
    }

    void SetViewport(rstd::uint32_t first, slice<VkViewport> viewports) const noexcept {
        dld->vkCmdSetViewport(handle, first, vk_count(viewports.len()), viewports.as_raw_ptr());
    }

    void SetViewport(rstd::uint32_t first, const VkViewport& viewport) const noexcept {
        SetViewport(first, slice<VkViewport>::from_raw_parts(&viewport, usize(1)));
    }

    void SetScissor(rstd::uint32_t first, slice<VkRect2D> scissors) const noexcept {
        dld->vkCmdSetScissor(handle, first, vk_count(scissors.len()), scissors.as_raw_ptr());
    }

    void SetScissor(rstd::uint32_t first, const VkRect2D& scissor) const noexcept {
        SetScissor(first, slice<VkRect2D>::from_raw_parts(&scissor, usize(1)));
    }

    void SetBlendConstants(const float blend_constants[4]) const noexcept {
        dld->vkCmdSetBlendConstants(handle, blend_constants);
    }

    void SetStencilCompareMask(VkStencilFaceFlags face_mask,
                               rstd::uint32_t     compare_mask) const noexcept {
        dld->vkCmdSetStencilCompareMask(handle, face_mask, compare_mask);
    }

    void SetStencilReference(VkStencilFaceFlags face_mask,
                             rstd::uint32_t     reference) const noexcept {
        dld->vkCmdSetStencilReference(handle, face_mask, reference);
    }

    void SetStencilWriteMask(VkStencilFaceFlags face_mask,
                             rstd::uint32_t     write_mask) const noexcept {
        dld->vkCmdSetStencilWriteMask(handle, face_mask, write_mask);
    }

    void SetDepthBias(float constant_factor, float clamp, float slope_factor) const noexcept {
        dld->vkCmdSetDepthBias(handle, constant_factor, clamp, slope_factor);
    }

    void SetDepthBounds(float min_depth_bounds, float max_depth_bounds) const noexcept {
        dld->vkCmdSetDepthBounds(handle, min_depth_bounds, max_depth_bounds);
    }

    void SetEvent(VkEvent event, VkPipelineStageFlags stage_flags) const noexcept {
        dld->vkCmdSetEvent(handle, event, stage_flags);
    }

    void WaitEvents(slice<VkEvent> events, VkPipelineStageFlags src_stage_mask,
                    VkPipelineStageFlags dst_stage_mask, slice<VkMemoryBarrier> memory_barriers,
                    slice<VkBufferMemoryBarrier> buffer_barriers,
                    slice<VkImageMemoryBarrier>  image_barriers) const noexcept {
        dld->vkCmdWaitEvents(handle,
                             vk_count(events.len()),
                             events.as_raw_ptr(),
                             src_stage_mask,
                             dst_stage_mask,
                             vk_count(memory_barriers.len()),
                             memory_barriers.as_raw_ptr(),
                             vk_count(buffer_barriers.len()),
                             buffer_barriers.as_raw_ptr(),
                             vk_count(image_barriers.len()),
                             image_barriers.as_raw_ptr());
    }

    void BeginDebugUtilsLabelEXT(const char* label, slice<float> color) const noexcept {
        rstd_assert(color.len() == usize(4));
        const VkDebugUtilsLabelEXT label_info {
            .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pNext      = nullptr,
            .pLabelName = label,
            .color { color[usize()], color[usize(1)], color[usize(2)], color[usize(3)] },
        };
        dld->vkCmdBeginDebugUtilsLabelEXT(handle, &label_info);
    }

    void EndDebugUtilsLabelEXT() const noexcept { dld->vkCmdEndDebugUtilsLabelEXT(handle); }
};

rstd::Option<rstd::vec::Vec<VkExtensionProperties>>
EnumerateInstanceExtensionProperties(const GlobalDispatch& dld);

rstd::Option<rstd::vec::Vec<VkLayerProperties>>
EnumerateInstanceLayerProperties(const GlobalDispatch& dld);

} // namespace vvk
