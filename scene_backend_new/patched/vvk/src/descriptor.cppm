export module vvk:descriptor;

import rstd;
import :ffi.vulkan;
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

struct DescriptorDeviceDispatch {
    PFN_vkCreateDescriptorPool   create_pool { nullptr };
    PFN_vkDestroyDescriptorPool  destroy_pool { nullptr };
    PFN_vkAllocateDescriptorSets allocate_sets { nullptr };
    PFN_vkUpdateDescriptorSets   update_sets { nullptr };

    static DescriptorDeviceDispatch FromDispatch(const DeviceDispatch& dispatch) noexcept {
        return DescriptorDeviceDispatch {
            .create_pool   = dispatch.vkCreateDescriptorPool,
            .destroy_pool  = dispatch.vkDestroyDescriptorPool,
            .allocate_sets = dispatch.vkAllocateDescriptorSets,
            .update_sets   = dispatch.vkUpdateDescriptorSets,
        };
    }

    bool valid() const noexcept {
        return create_pool != nullptr && destroy_pool != nullptr && allocate_sets != nullptr &&
               update_sets != nullptr;
    }
};

class DescriptorArenaGeneration;

struct DescriptorArenaCreateResult {
    VkResult                                           api_result { VK_SUCCESS };
    Option<rstd::sync::Arc<DescriptorArenaGeneration>> arena;

    bool created() const noexcept { return api_result == VK_SUCCESS && arena.is_some(); }
};

struct DescriptorSetLease {
    VkDescriptorSet                                    handle { VK_NULL_HANDLE };
    VkDescriptorSetLayout                              layout { VK_NULL_HANDLE };
    Option<rstd::sync::Arc<DescriptorArenaGeneration>> owner;

    bool valid() const noexcept {
        return handle != VK_NULL_HANDLE && layout != VK_NULL_HANDLE && owner.is_some();
    }

    DescriptorSetLease clone() const {
        return DescriptorSetLease {
            .handle = handle,
            .layout = layout,
            .owner  = owner.is_some() ? Some((*owner).clone()) : None(),
        };
    }
};

struct DescriptorSetAllocationResult {
    VkResult           api_result { VK_SUCCESS };
    DescriptorSetLease lease;

    bool allocated() const noexcept { return api_result == VK_SUCCESS && lease.valid(); }
};

class DescriptorArenaGeneration final {
public:
    DescriptorArenaGeneration(const DescriptorArenaGeneration&)            = delete;
    DescriptorArenaGeneration& operator=(const DescriptorArenaGeneration&) = delete;
    DescriptorArenaGeneration(DescriptorArenaGeneration&&)                 = delete;
    DescriptorArenaGeneration& operator=(DescriptorArenaGeneration&&)      = delete;

    ~DescriptorArenaGeneration() {
        if (m_pool != VK_NULL_HANDLE && m_dispatch.destroy_pool != nullptr) {
            m_dispatch.destroy_pool(m_device, m_pool, nullptr);
        }
    }

    static DescriptorArenaCreateResult Create(VkDevice device, rstd::uint32_t max_sets,
                                              slice<VkDescriptorPoolSize> sizes,
                                              DescriptorDeviceDispatch    dispatch) {
        if (device == VK_NULL_HANDLE || max_sets == 0 || sizes.len() == usize() ||
            ! dispatch.valid()) {
            return { .api_result = VK_ERROR_INITIALIZATION_FAILED };
        }

        VkDescriptorPoolCreateInfo create_info {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext         = nullptr,
            .flags         = 0,
            .maxSets       = max_sets,
            .poolSizeCount = vk_count(sizes.len()),
            .pPoolSizes    = sizes.as_raw_ptr(),
        };
        VkDescriptorPool pool   = VK_NULL_HANDLE;
        const auto       result = dispatch.create_pool(device, &create_info, nullptr, &pool);
        if (result != VK_SUCCESS || pool == VK_NULL_HANDLE) {
            return { .api_result = result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result };
        }
        return DescriptorArenaCreateResult {
            .api_result = VK_SUCCESS,
            .arena = Some(rstd::sync::Arc<DescriptorArenaGeneration>::make(device, pool, dispatch)),
        };
    }

    static DescriptorSetAllocationResult
    Allocate(const rstd::sync::Arc<DescriptorArenaGeneration>& arena,
             VkDescriptorSetLayout                             layout) {
        const auto* self = arena.as_ptr().as_raw_ptr();
        if (layout == VK_NULL_HANDLE || self->m_pool == VK_NULL_HANDLE ||
            self->m_dispatch.allocate_sets == nullptr) {
            return { .api_result = VK_ERROR_INITIALIZATION_FAILED };
        }
        VkDescriptorSetAllocateInfo allocate_info {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext              = nullptr,
            .descriptorPool     = self->m_pool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &layout,
        };
        VkDescriptorSet set = VK_NULL_HANDLE;
        const auto result   = self->m_dispatch.allocate_sets(self->m_device, &allocate_info, &set);
        if (result != VK_SUCCESS || set == VK_NULL_HANDLE) {
            return { .api_result = result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result };
        }
        return DescriptorSetAllocationResult {
            .api_result = VK_SUCCESS,
            .lease =
                DescriptorSetLease {
                    .handle = set,
                    .layout = layout,
                    .owner  = Some(arena.clone()),
                },
        };
    }

    VkDevice                        device() const noexcept { return m_device; }
    VkDescriptorPool                pool() const noexcept { return m_pool; }
    const DescriptorDeviceDispatch& dispatch() const noexcept { return m_dispatch; }

public:
    DescriptorArenaGeneration(VkDevice device, VkDescriptorPool pool,
                              DescriptorDeviceDispatch dispatch) noexcept
        : m_device(device), m_pool(pool), m_dispatch(dispatch) {}

    VkDevice                 m_device { VK_NULL_HANDLE };
    VkDescriptorPool         m_pool { VK_NULL_HANDLE };
    DescriptorDeviceDispatch m_dispatch;
};

enum class DescriptorUpdateStatus
{
    Committed,
    Empty,
    Invalid,
};

struct DescriptorUpdateResult {
    DescriptorUpdateStatus status { DescriptorUpdateStatus::Invalid };
    rstd::uint32_t         write_count {};

    bool committed() const noexcept {
        return status == DescriptorUpdateStatus::Committed ||
               status == DescriptorUpdateStatus::Empty;
    }
};

class DescriptorUpdateBatch {
public:
    bool WriteImage(DescriptorSetLease lease, rstd::uint32_t binding,
                    VkDescriptorType descriptor_type, slice<VkDescriptorImageInfo> image_infos,
                    rstd::uint32_t array_element = 0) {
        if (m_committed || ! lease.valid() || image_infos.len() == usize()) return false;
        auto infos = rstd::vec::Vec<VkDescriptorImageInfo>::with_capacity(image_infos.len());
        infos.extend_from_slice(image_infos.as_raw_ptr(), image_infos.len());
        m_image_writes.push(ImageWrite {
            .lease         = rstd::move(lease),
            .binding       = binding,
            .array_element = array_element,
            .type          = descriptor_type,
            .infos         = rstd::move(infos),
        });
        return true;
    }

    bool WriteBuffer(DescriptorSetLease lease, rstd::uint32_t binding,
                     VkDescriptorType descriptor_type, slice<VkDescriptorBufferInfo> buffer_infos,
                     rstd::uint32_t array_element = 0) {
        if (m_committed || ! lease.valid() || buffer_infos.len() == usize()) return false;
        auto infos = rstd::vec::Vec<VkDescriptorBufferInfo>::with_capacity(buffer_infos.len());
        infos.extend_from_slice(buffer_infos.as_raw_ptr(), buffer_infos.len());
        m_buffer_writes.push(BufferWrite {
            .lease         = rstd::move(lease),
            .binding       = binding,
            .array_element = array_element,
            .type          = descriptor_type,
            .infos         = rstd::move(infos),
        });
        return true;
    }

    DescriptorUpdateResult Commit() {
        if (m_committed) return {};
        if (m_image_writes.is_empty() && m_buffer_writes.is_empty()) {
            m_committed = true;
            return { .status = DescriptorUpdateStatus::Empty };
        }

        const auto* owner = FirstOwner();
        if (owner == nullptr || owner->device() == VK_NULL_HANDLE ||
            owner->dispatch().update_sets == nullptr || ! AllUseDevice(owner->device())) {
            return {};
        }

        auto writes = rstd::vec::Vec<VkWriteDescriptorSet>::with_capacity(m_image_writes.len() +
                                                                          m_buffer_writes.len());
        for (const auto& write : m_image_writes) {
            auto descriptor_write = VkWriteDescriptorSet {
                .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext            = nullptr,
                .dstSet           = write.lease.handle,
                .dstBinding       = write.binding,
                .dstArrayElement  = write.array_element,
                .descriptorCount  = vk_count(write.infos.len()),
                .descriptorType   = write.type,
                .pImageInfo       = write.infos.data(),
                .pBufferInfo      = nullptr,
                .pTexelBufferView = nullptr,
            };
            writes.push(rstd::move(descriptor_write));
        }
        for (const auto& write : m_buffer_writes) {
            auto descriptor_write = VkWriteDescriptorSet {
                .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext            = nullptr,
                .dstSet           = write.lease.handle,
                .dstBinding       = write.binding,
                .dstArrayElement  = write.array_element,
                .descriptorCount  = vk_count(write.infos.len()),
                .descriptorType   = write.type,
                .pImageInfo       = nullptr,
                .pBufferInfo      = write.infos.data(),
                .pTexelBufferView = nullptr,
            };
            writes.push(rstd::move(descriptor_write));
        }
        owner->dispatch().update_sets(
            owner->device(), vk_count(writes.len()), writes.data(), 0, nullptr);
        m_committed = true;
        return DescriptorUpdateResult {
            .status      = DescriptorUpdateStatus::Committed,
            .write_count = vk_count(writes.len()),
        };
    }

private:
    struct ImageWrite {
        DescriptorSetLease                    lease;
        rstd::uint32_t                        binding {};
        rstd::uint32_t                        array_element {};
        VkDescriptorType                      type { VK_DESCRIPTOR_TYPE_MAX_ENUM };
        rstd::vec::Vec<VkDescriptorImageInfo> infos;
    };

    struct BufferWrite {
        DescriptorSetLease                     lease;
        rstd::uint32_t                         binding {};
        rstd::uint32_t                         array_element {};
        VkDescriptorType                       type { VK_DESCRIPTOR_TYPE_MAX_ENUM };
        rstd::vec::Vec<VkDescriptorBufferInfo> infos;
    };

    const DescriptorArenaGeneration* FirstOwner() const noexcept {
        if (! m_image_writes.is_empty()) {
            return m_image_writes[usize()].lease.owner->as_ptr().as_raw_ptr();
        }
        if (! m_buffer_writes.is_empty()) {
            return m_buffer_writes[usize()].lease.owner->as_ptr().as_raw_ptr();
        }
        return nullptr;
    }

    bool AllUseDevice(VkDevice device) const noexcept {
        for (const auto& write : m_image_writes) {
            if (! write.lease.valid() || (*write.lease.owner)->device() != device) return false;
        }
        for (const auto& write : m_buffer_writes) {
            if (! write.lease.valid() || (*write.lease.owner)->device() != device) return false;
        }
        return true;
    }

    rstd::vec::Vec<ImageWrite>  m_image_writes;
    rstd::vec::Vec<BufferWrite> m_buffer_writes;
    bool                        m_committed { false };
};

} // namespace vvk
