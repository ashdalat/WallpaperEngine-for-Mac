export module vvk:memory;
import rstd;
export import :ffi.vulkan;
import :dispatch;

using namespace rstd::prelude;

export namespace vvk
{

/// All handles from one allocator require external synchronization. The device and
/// metadata allocator must outlive the allocator, resources, allocations and mappings.
struct MemoryDispatch {
    PFN_vkGetPhysicalDeviceProperties        properties {};
    PFN_vkGetPhysicalDeviceMemoryProperties2 memory_properties {};
    PFN_vkAllocateMemory                     allocate {};
    PFN_vkFreeMemory                         free {};
    PFN_vkCreateBuffer                       create_buffer {};
    PFN_vkDestroyBuffer                      destroy_buffer {};
    PFN_vkCreateImage                        create_image {};
    PFN_vkDestroyImage                       destroy_image {};
    PFN_vkGetBufferMemoryRequirements2       buffer_requirements {};
    PFN_vkGetImageMemoryRequirements2        image_requirements {};
    PFN_vkBindBufferMemory                   bind_buffer {};
    PFN_vkBindImageMemory                    bind_image {};
    PFN_vkMapMemory                          map {};
    PFN_vkUnmapMemory                        unmap {};
    PFN_vkFlushMappedMemoryRanges            flush {};
    PFN_vkInvalidateMappedMemoryRanges       invalidate {};
    PFN_vkGetPhysicalDeviceProperties2       properties2 {};
    PFN_vkGetBufferDeviceAddress             buffer_device_address {};
    static MemoryDispatch                    FromDispatch(const InstanceDispatch& instance,
                                                          const DeviceDispatch&   dispatch) noexcept {
        return { instance.vkGetPhysicalDeviceProperties,
                 instance.vkGetPhysicalDeviceMemoryProperties2,
                 dispatch.vkAllocateMemory,
                 dispatch.vkFreeMemory,
                 dispatch.vkCreateBuffer,
                 dispatch.vkDestroyBuffer,
                 dispatch.vkCreateImage,
                 dispatch.vkDestroyImage,
                 dispatch.vkGetBufferMemoryRequirements2,
                 dispatch.vkGetImageMemoryRequirements2,
                 dispatch.vkBindBufferMemory,
                 dispatch.vkBindImageMemory,
                 dispatch.vkMapMemory,
                 dispatch.vkUnmapMemory,
                 dispatch.vkFlushMappedMemoryRanges,
                 dispatch.vkInvalidateMappedMemoryRanges,
                 instance.vkGetPhysicalDeviceProperties2,
                 dispatch.vkGetBufferDeviceAddress };
    }
    bool valid() const noexcept {
        return properties && memory_properties && allocate && free && create_buffer &&
               destroy_buffer && create_image && destroy_image && buffer_requirements &&
               image_requirements && bind_buffer && bind_image && map && unmap && flush &&
               invalidate;
    }
};

enum class MemoryErrorKind
{
    InvalidRequest,
    Unsupported,
    NoMemoryType,
    HostMemory,
    DeviceMemory,
    Vulkan,
    PoolCapacity,
    ExistingBlocksOnly,
    PoolIncompatible,
    DedicatedConflict
};
struct MemoryError {
    MemoryErrorKind kind;
    VkResult        api_result { VK_SUCCESS };
    // Block failures identify the rejected size; attempts count actual vkAllocateMemory calls.
    rstd::uint32_t device_allocation_attempts {};
    rstd::uint32_t memory_type { VK_MAX_MEMORY_TYPES };
    VkDeviceSize   allocation_size {};
    bool           dedicated {};
};
using MemoryMetadata = rstd::ref<rstd::dyn<rstd::alloc::Allocator>>;

struct MemoryBlockPolicy {
    // Equal sizes keep shared block growth fixed. Both sizes must be nonzero.
    VkDeviceSize initial_size { 4 * 1024 * 1024 }, maximum_size { 16 * 1024 * 1024 };
    // At most 63 halvings, clamped to the atom-padded request size.
    rstd::uint32_t shrink_attempts { 3 };
    // Try the other allocation kind in the same type only after device-memory exhaustion.
    bool dedicated_fallback { true };
};
enum class MemoryClass
{
    Linear,
    Optimal
};
struct MemoryPoolCreateInfo {
    rstd::uint32_t        memory_type { VK_MAX_MEMORY_TYPES };
    MemoryClass           resource_class { MemoryClass::Linear };
    VkMemoryAllocateFlags allocation_flags {};
    // Dedicated fallback does not apply to explicit pools.
    MemoryBlockPolicy block_policy {};
    // Zero maxima impose no explicit limit; min blocks use initial_size.
    rstd::uint32_t min_blocks {}, max_blocks {};
    VkDeviceSize   max_bytes {};
    bool           within_budget {};
};
struct MemoryPoolStatistics {
    rstd::size_t block_count {}, allocation_count {};
    VkDeviceSize block_bytes {}, requested_bytes {}, occupied_bytes {};
};
struct MemoryAllocatorCreateInfo {
    VkPhysicalDevice physical_device {};
    VkDevice         device {};
    // Set only when VK_EXT_memory_budget was enabled on this device.
    bool           memory_budget_enabled {};
    MemoryDispatch dispatch {};
    // Core Vulkan 1.2 or enabled VK_KHR_image_format_list.
    bool              image_format_list_enabled {};
    MemoryBlockPolicy block_policy {};
    bool              buffer_device_address_enabled {};
    MemoryAllocatorCreateInfo() = default;
    MemoryAllocatorCreateInfo(VkPhysicalDevice physical, VkDevice device, MemoryBlockPolicy policy,
                              bool budget, MemoryDispatch dispatch, bool image_format_list = false,
                              bool buffer_device_address = false)
        : physical_device(physical),
          device(device),
          memory_budget_enabled(budget),
          dispatch(dispatch),
          image_format_list_enabled(image_format_list),
          block_policy(policy),
          buffer_device_address_enabled(buffer_device_address) {}
};
enum class MemoryHostAccess
{
    None,
    SequentialWrite,
    Random
};
enum class MemoryPreference
{
    Automatic,
    Device,
    Host,
    FlagsOnly
};
enum class MemoryPlacementReason
{
    AutomaticDevice,
    AutomaticHost,
    RequestedDevice,
    RequestedHost,
    ExplicitFlags,
    FixedPool
};
enum class MemoryDedicatedReason
{
    None,
    Required,
    Requested,
    DriverPreferred,
    LargeResource,
    SharedExhausted
};
struct MemorySelectionInfo {
    VkMemoryPropertyFlags required {}, explicit_preferred {}, preferred {}, avoided {};
    MemoryPlacementReason placement_reason { MemoryPlacementReason::AutomaticDevice };
    MemoryDedicatedReason dedicated_reason { MemoryDedicatedReason::None };
    bool                  type_fallback {};
    rstd::uint32_t        device_allocation_attempts {};
};
struct MemoryRequest {
    VkMemoryPropertyFlags required {};
    // Explicit preferences rank ahead of inferred access and placement preferences.
    VkMemoryPropertyFlags preferred {};
    bool                  dedicated {};
    bool                  persistent_mapping {};
    bool                  within_budget {};
    MemoryHostAccess      host_access { MemoryHostAccess::None };
    MemoryPreference      preference { MemoryPreference::Automatic };
    // Forbids new VkDeviceMemory; CPU metadata allocation can still occur.
    bool                 existing_blocks_only {};
    static MemoryRequest Upload() {
        MemoryRequest result;
        result.host_access = MemoryHostAccess::SequentialWrite;
        return result;
    }
    static MemoryRequest Readback() {
        MemoryRequest result;
        result.host_access = MemoryHostAccess::Random;
        return result;
    }
};
struct MemoryInfo {
    VkDeviceMemory        memory {};
    VkDeviceSize          offset {}, size {}, occupied_size {};
    rstd::uint32_t        memory_type {};
    VkMemoryPropertyFlags properties {};
    bool                  dedicated {};
    rstd::uint32_t        heap {};
    MemorySelectionInfo   selection {};
    VkMemoryAllocateFlags allocation_flags {};
};
enum class MemoryBudgetSource
{
    AllocatorEstimate,
    Driver
};
struct MemoryHeapBudget {
    VkDeviceSize       heap_size {}, usage {}, budget {}, block_bytes {}, allocation_bytes {};
    rstd::uint32_t     block_count {}, allocation_count {};
    MemoryBudgetSource source { MemoryBudgetSource::AllocatorEstimate };
};
struct MemoryBudgetSnapshot {
    MemoryHeapBudget heaps[VK_MAX_MEMORY_HEAPS] {};
    rstd::uint32_t   heap_count {};
};

class MemoryAllocator;
class MemoryPool;
class MemoryAllocation;
class MemoryMapping;
class AllocatedBuffer;
class AllocatedImage;
} // namespace vvk

namespace vvk
{
struct MemoryState;
struct MemoryBlock;
struct MemoryBlockSet;
struct MemoryRegion;
struct MemoryResource;

MemoryError HostMemoryError() {
    return { MemoryErrorKind::HostMemory, VK_ERROR_OUT_OF_HOST_MEMORY };
}
MemoryError ApiMemoryError(VkResult result) {
    return { result == VK_ERROR_OUT_OF_DEVICE_MEMORY ? MemoryErrorKind::DeviceMemory
             : result == VK_ERROR_OUT_OF_HOST_MEMORY ? MemoryErrorKind::HostMemory
                                                     : MemoryErrorKind::Vulkan,
             result };
}
template<typename T, typename... Args>
T* NewMemoryObject(MemoryMetadata metadata, Args&&... args) {
    auto allocation = metadata->allocate(rstd::alloc::Layout::make<T>());
    if (allocation.is_err()) return nullptr;
    auto pointer = allocation.unwrap_unchecked().template as_mut_ptr<T>();
    rstd::ptr_::construct(pointer, rstd::forward<Args>(args)...);
    return pointer.as_raw_ptr();
}
template<typename T>
void DeleteMemoryObject(MemoryMetadata metadata, T* object) {
    object->~T();
    metadata->deallocate(object, rstd::alloc::Layout::make<T>());
}

struct ResourceMemoryConstraints {
    MemoryClass           resource_class;
    bool                  device_access;
    VkMemoryAllocateFlags allocation_flags {};
};
auto ParseResourceMemoryConstraints(const VkBufferCreateInfo& info)
    -> Result<ResourceMemoryConstraints, MemoryError> {
    if (info.sType != VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO || info.size == 0 || info.usage == 0)
        return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    constexpr VkBufferUsageFlags supported =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (info.flags || info.pNext || (info.usage & ~supported))
        return Err(MemoryError { MemoryErrorKind::Unsupported });
    return Ok(ResourceMemoryConstraints {
        MemoryClass::Linear,
        bool(info.usage & ~(VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)),
        (info.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
            ? VkMemoryAllocateFlags(VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT)
            : 0u });
}
auto ParseResourceMemoryConstraints(const VkImageCreateInfo& info, bool format_list_enabled)
    -> Result<ResourceMemoryConstraints, MemoryError> {
    if (info.sType != VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO || ! info.extent.width ||
        ! info.extent.height || ! info.extent.depth || ! info.mipLevels || ! info.arrayLayers ||
        ! info.usage)
        return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    constexpr VkImageUsageFlags supported =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    constexpr VkImageCreateFlags supported_flags =
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT | VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    if ((info.flags & ~supported_flags) || (info.usage & ~supported) ||
        (info.tiling != VK_IMAGE_TILING_LINEAR && info.tiling != VK_IMAGE_TILING_OPTIMAL))
        return Err(MemoryError { MemoryErrorKind::Unsupported });
    if ((info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
        (info.imageType != VK_IMAGE_TYPE_2D || info.extent.width != info.extent.height ||
         info.extent.depth != 1 || info.arrayLayers < 6 || info.samples != VK_SAMPLE_COUNT_1_BIT))
        return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    bool saw_format_list = false;
    for (auto* next = static_cast<const VkBaseInStructure*>(info.pNext); next; next = next->pNext) {
        if (next->sType != VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO)
            return Err(MemoryError { MemoryErrorKind::Unsupported });
        if (saw_format_list) return Err(MemoryError { MemoryErrorKind::InvalidRequest });
        saw_format_list = true;
        if (! format_list_enabled) return Err(MemoryError { MemoryErrorKind::Unsupported });
        const auto& list = *reinterpret_cast<const VkImageFormatListCreateInfo*>(next);
        if ((list.viewFormatCount && ! list.pViewFormats) ||
            (! (info.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) && list.viewFormatCount > 1))
            return Err(MemoryError { MemoryErrorKind::InvalidRequest });
        for (unsigned i = 0; i < list.viewFormatCount; ++i)
            if (list.pViewFormats[i] == VK_FORMAT_UNDEFINED)
                return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    }
    return Ok(ResourceMemoryConstraints {
        info.tiling == VK_IMAGE_TILING_OPTIMAL ? MemoryClass::Optimal : MemoryClass::Linear,
        bool(info.usage & ~(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) });
}
struct MemoryTypePreferences {
    MemorySelectionInfo   info;
    VkMemoryPropertyFlags access_preferred {}, access_avoided {}, placement_preferred {},
        placement_avoided {};
    bool           prefer_coherent {};
    rstd::uint32_t score(VkMemoryPropertyFlags flags) const {
        const auto explicit_misses  = __builtin_popcount(info.explicit_preferred & ~flags);
        const auto access_misses    = __builtin_popcount(access_preferred & ~flags) +
                                      __builtin_popcount(access_avoided & flags);
        const auto placement_misses = __builtin_popcount(placement_preferred & ~flags) +
                                      __builtin_popcount(placement_avoided & flags);
        const auto coherence_misses =
            prefer_coherent && ! (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        // Disjoint fields preserve lexicographic priority; soft preferences never filter types.
        return (rstd::uint32_t(explicit_misses) << 16) | (rstd::uint32_t(access_misses) << 8) |
               (rstd::uint32_t(placement_misses) << 1) | coherence_misses;
    }
};
auto ParseMemoryTypePreferences(MemoryRequest request, const ResourceMemoryConstraints& resource)
    -> Result<MemoryTypePreferences, MemoryError> {
    if (request.host_access != MemoryHostAccess::None &&
        request.host_access != MemoryHostAccess::SequentialWrite &&
        request.host_access != MemoryHostAccess::Random)
        return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    MemoryTypePreferences result;
    result.info.required           = request.required;
    result.info.explicit_preferred = request.preferred;
    if (request.host_access != MemoryHostAccess::None || request.persistent_mapping)
        result.info.required |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    switch (request.preference) {
    case MemoryPreference::FlagsOnly:
        result.info.placement_reason = MemoryPlacementReason::ExplicitFlags;
        return Ok(result);
    case MemoryPreference::Automatic:
        result.info.placement_reason =
            request.host_access == MemoryHostAccess::Random ||
                    (request.host_access == MemoryHostAccess::SequentialWrite &&
                     ! resource.device_access)
                ? MemoryPlacementReason::AutomaticHost
                : MemoryPlacementReason::AutomaticDevice;
        break;
    case MemoryPreference::Device:
        result.info.placement_reason = MemoryPlacementReason::RequestedDevice;
        break;
    case MemoryPreference::Host:
        result.info.placement_reason = MemoryPlacementReason::RequestedHost;
        break;
    default: return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    }
    if (request.host_access == MemoryHostAccess::Random)
        result.access_preferred = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    else if (request.host_access == MemoryHostAccess::SequentialWrite)
        result.access_avoided = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    if (result.info.placement_reason == MemoryPlacementReason::AutomaticHost ||
        result.info.placement_reason == MemoryPlacementReason::RequestedHost)
        result.placement_avoided = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    else
        result.placement_preferred = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    result.prefer_coherent =
        request.host_access != MemoryHostAccess::None || request.persistent_mapping;
    result.info.preferred = result.access_preferred | result.placement_preferred |
                            (result.prefer_coherent ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0);
    result.info.avoided   = result.access_avoided | result.placement_avoided;
    return Ok(result);
}
struct MemoryBlock {
    MemoryState*                          owner;
    MemoryBlockSet*                       collection {};
    VkDeviceMemory                        memory {};
    VkDeviceSize                          size;
    rstd::uint32_t                        type;
    MemoryClass                           resource_class;
    bool                                  dedicated;
    VkMemoryAllocateFlags                 allocation_flags;
    alloc::RangeAllocator<MemoryMetadata> ranges;
    void*                                 mapped {};
    rstd::size_t                          maps {};
    MemoryBlock(MemoryState* owner, VkDeviceSize size, rstd::uint32_t type, MemoryClass cls,
                bool dedicated, VkMemoryAllocateFlags flags, MemoryMetadata metadata)
        : owner(owner),
          size(size),
          type(type),
          resource_class(cls),
          dedicated(dedicated),
          allocation_flags(flags),
          ranges(size, metadata) {}
};
struct MemoryBlockSet {
    MemoryState*                                  owner;
    MemoryPoolCreateInfo                          info {};
    bool                                          explicit_pool {};
    rstd::size_t                                  refs { 1 };
    alloc::vec::Vec<MemoryBlock*, MemoryMetadata> blocks;
    MemoryPoolStatistics                          stats {};
    MemoryBlockSet(MemoryState* owner, MemoryMetadata metadata)
        : owner(owner), blocks(alloc::vec::Vec<MemoryBlock*, MemoryMetadata>::new_in(metadata)) {}
};
struct MemoryState {
    MemoryAllocatorCreateInfo        info;
    MemoryMetadata                   metadata;
    MemoryBlockSet                   defaults;
    VkPhysicalDeviceProperties       properties {};
    VkPhysicalDeviceMemoryProperties memory {};
    // Non-owning registry across all block sets for device limits and heap accounting.
    alloc::vec::Vec<MemoryBlock*, MemoryMetadata> blocks;
    VkDeviceSize                                  max_allocation_size { ~VkDeviceSize(0) };
    rstd::size_t                                  refs { 1 };
    explicit MemoryState(MemoryAllocatorCreateInfo info, MemoryMetadata metadata)
        : info(info),
          metadata(metadata),
          defaults(this, metadata),
          blocks(alloc::vec::Vec<MemoryBlock*, MemoryMetadata>::new_in(metadata)) {}
};
struct MemoryRegion {
    MemoryState*           owner;
    MemoryBlock*           block;
    alloc::RangeAllocation range;
    VkDeviceSize           requested;
    MemorySelectionInfo    selection;
    rstd::size_t           refs { 1 };
    bool                   persistent {};
    bool                   rollback_block {};
    MemoryRegion(MemoryState* owner, MemoryBlock* block, alloc::RangeAllocation range,
                 VkDeviceSize requested, MemorySelectionInfo selection)
        : owner(owner), block(block), range(range), requested(requested), selection(selection) {
        if (block->collection->explicit_pool)
            ++block->collection->refs;
        else
            ++owner->refs;
        auto& stats = block->collection->stats;
        ++stats.allocation_count;
        stats.requested_bytes += requested;
        stats.occupied_bytes += range.size;
    }
};
void DestroyMemoryBlock(MemoryState* owner, MemoryBlock* block) {
    auto* set = block->collection;
    for (usize i {}; i < set->blocks.len(); ++i)
        if (set->blocks[i] == block) {
            set->blocks.remove(i);
            break;
        }
    for (usize i {}; i < owner->blocks.len(); ++i)
        if (owner->blocks[i] == block) {
            owner->blocks.remove(i);
            break;
        }
    --set->stats.block_count;
    set->stats.block_bytes -= block->size;
    if (block->mapped) owner->info.dispatch.unmap(owner->info.device, block->memory);
    owner->info.dispatch.free(owner->info.device, block->memory, nullptr);
    DeleteMemoryObject(owner->metadata, block);
}
void DropMemoryState(MemoryState* owner) {
    if (--owner->refs != 0) return;
    while (! owner->defaults.blocks.is_empty())
        DestroyMemoryBlock(owner, owner->defaults.blocks[usize(0)]);
    const auto metadata = owner->metadata;
    DeleteMemoryObject(metadata, owner);
}
void DropMemoryBlockSet(MemoryBlockSet* set) {
    auto* owner = set->owner;
    if (! set->explicit_pool) {
        DropMemoryState(owner);
        return;
    }
    if (--set->refs != 0) return;
    while (! set->blocks.is_empty()) DestroyMemoryBlock(owner, set->blocks[usize(0)]);
    DeleteMemoryObject(owner->metadata, set);
    DropMemoryState(owner);
}
void TrimMemoryBlockSet(MemoryBlockSet* set) {
    for (usize i {}; i < set->blocks.len();) {
        auto* block = set->blocks[i];
        if (block->ranges.counters().allocation_count == 0 &&
            (! set->explicit_pool || set->blocks.len().to_primitive() > set->info.min_blocks))
            DestroyMemoryBlock(set->owner, block);
        else
            ++i;
    }
}
void UnmapMemoryBlock(MemoryBlock* block) {
    if (--block->maps == 0) {
        block->owner->info.dispatch.unmap(block->owner->info.device, block->memory);
        block->mapped = nullptr;
    }
}
VkResult MapMemoryBlock(MemoryBlock* block) {
    if (block->maps == 0) {
        void* mapped = nullptr;
        auto  result = block->owner->info.dispatch.map(
            block->owner->info.device, block->memory, 0, VK_WHOLE_SIZE, 0, &mapped);
        if (result != VK_SUCCESS) return result;
        block->mapped = mapped;
    }
    ++block->maps;
    return VK_SUCCESS;
}
void DropMemoryRegion(MemoryRegion* region) {
    if (--region->refs != 0) return;
    auto* owner = region->owner;
    auto* block = region->block;
    if (region->persistent) UnmapMemoryBlock(block);
    block->ranges.deallocate(region->range.id);
    auto* set = block->collection;
    --set->stats.allocation_count;
    set->stats.requested_bytes -= region->requested;
    set->stats.occupied_bytes -= region->range.size;
    bool release_block = block->dedicated || region->rollback_block;
    if (! release_block && block->ranges.counters().allocation_count == 0 &&
        (! set->explicit_pool || set->blocks.len().to_primitive() > set->info.min_blocks)) {
        for (auto* other : set->blocks) {
            if (other != block && ! other->dedicated && other->type == block->type &&
                other->resource_class == block->resource_class &&
                other->allocation_flags == block->allocation_flags &&
                other->ranges.counters().allocation_count == 0) {
                release_block = true;
                break;
            }
        }
    }
    if (release_block) DestroyMemoryBlock(owner, block);
    DeleteMemoryObject(owner->metadata, region);
    DropMemoryBlockSet(set);
}

/// A memory lease preserves storage, not its bound buffer or image. Retain the
/// resource lease until every submission using that resource has completed.
export class MemoryAllocation {
    MemoryRegion* region_ {};
    explicit MemoryAllocation(MemoryRegion* region): region_(region) {}
    friend class MemoryAllocator;
    friend class MemoryMapping;

public:
    MemoryAllocation()                                   = default;
    MemoryAllocation(const MemoryAllocation&)            = delete;
    MemoryAllocation& operator=(const MemoryAllocation&) = delete;
    MemoryAllocation(MemoryAllocation&& other) noexcept
        : region_(rstd::exchange(other.region_, nullptr)) {}
    MemoryAllocation& operator=(MemoryAllocation&& other) noexcept {
        if (this != &other) {
            reset();
            region_ = rstd::exchange(other.region_, nullptr);
        }
        return *this;
    }
    ~MemoryAllocation() { reset(); }
    void reset() {
        if (region_) {
            DropMemoryRegion(region_);
            region_ = nullptr;
        }
    }
    bool             valid() const noexcept { return region_ != nullptr; }
    MemoryAllocation clone() const {
        if (region_) ++region_->refs;
        return MemoryAllocation(region_);
    }
    auto info() const noexcept -> MemoryInfo {
        if (! region_) return {};
        const auto* block = region_->block;
        return { block->memory,      region_->range.offset,
                 region_->requested, region_->range.size,
                 block->type,        region_->owner->memory.memoryTypes[block->type].propertyFlags,
                 block->dedicated,   region_->owner->memory.memoryTypes[block->type].heapIndex,
                 region_->selection, block->allocation_flags };
    }
    auto map(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE) const
        -> Result<MemoryMapping, MemoryError>;
    auto flush(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE) const
        -> Result<empty, MemoryError>;
    auto invalidate(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE) const
        -> Result<empty, MemoryError>;

private:
    auto cache_operation(VkDeviceSize offset, VkDeviceSize size, bool invalidate) const
        -> Result<empty, MemoryError>;
};

export class MemoryMapping {
    MemoryAllocation allocation_;
    void*            data_ {};
    VkDeviceSize     size_ {};
    MemoryMapping(MemoryAllocation allocation, void* data, VkDeviceSize size)
        : allocation_(rstd::move(allocation)), data_(data), size_(size) {}
    friend class MemoryAllocation;

public:
    MemoryMapping(const MemoryMapping&)            = delete;
    MemoryMapping& operator=(const MemoryMapping&) = delete;
    MemoryMapping(MemoryMapping&& other) noexcept
        : allocation_(rstd::move(other.allocation_)),
          data_(rstd::exchange(other.data_, nullptr)),
          size_(other.size_) {}
    MemoryMapping& operator=(MemoryMapping&& other) noexcept {
        if (this != &other) {
            if (allocation_.valid()) UnmapMemoryBlock(allocation_.region_->block);
            allocation_ = rstd::move(other.allocation_);
            data_       = rstd::exchange(other.data_, nullptr);
            size_       = other.size_;
        }
        return *this;
    }
    ~MemoryMapping() {
        if (allocation_.valid()) UnmapMemoryBlock(allocation_.region_->block);
    }
    void*        data() const noexcept { return data_; }
    VkDeviceSize size() const noexcept { return size_; }
};

auto MemoryAllocation::map(VkDeviceSize offset, VkDeviceSize size) const
    -> Result<MemoryMapping, MemoryError> {
    if (! region_ || offset > region_->requested)
        return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    if (size == VK_WHOLE_SIZE) size = region_->requested - offset;
    if (size == 0 || size > region_->requested - offset ||
        (info().properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
        return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    if (offset > rstd::size_t(-1) || region_->range.offset > rstd::size_t(-1) - offset ||
        size > rstd::size_t(-1) - region_->range.offset - offset)
        return Err(MemoryError { MemoryErrorKind::Unsupported });
    auto result = MapMemoryBlock(region_->block);
    if (result != VK_SUCCESS) return Err(ApiMemoryError(result));
    auto pointer =
        static_cast<rstd::uint8_t*>(region_->block->mapped) + region_->range.offset + offset;
    return Ok(MemoryMapping(clone(), pointer, size));
}
auto MemoryAllocation::cache_operation(VkDeviceSize offset, VkDeviceSize size,
                                       bool invalidating) const -> Result<empty, MemoryError> {
    if (! region_ || offset > region_->requested)
        return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    if (size == VK_WHOLE_SIZE) size = region_->requested - offset;
    if (size > region_->requested - offset)
        return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    if (size == 0) return Ok(empty {});
    if (region_->block->maps == 0) return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    if (info().properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) return Ok(empty {});
    auto* owner = region_->owner;
    auto  atom  = owner->properties.limits.nonCoherentAtomSize;
    auto  start = region_->range.offset + offset;
    auto  end   = start + size;
    start -= start % atom;
    const auto remainder = end % atom;
    if (remainder) {
        const auto padding = atom - remainder;
        end = padding > region_->block->size - end ? region_->block->size : end + padding;
    }
    VkMappedMemoryRange range {
        VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, region_->block->memory, start, end - start
    };
    auto result = (invalidating ? owner->info.dispatch.invalidate
                                : owner->info.dispatch.flush)(owner->info.device, 1, &range);
    if (result != VK_SUCCESS) return Err(ApiMemoryError(result));
    return Ok(empty {});
}
auto MemoryAllocation::flush(VkDeviceSize offset, VkDeviceSize size) const
    -> Result<empty, MemoryError> {
    return cache_operation(offset, size, false);
}
auto MemoryAllocation::invalidate(VkDeviceSize offset, VkDeviceSize size) const
    -> Result<empty, MemoryError> {
    return cache_operation(offset, size, true);
}

struct MemoryResource {
    MemoryAllocation allocation;
    VkBuffer         buffer {};
    VkImage          image {};
    MemoryState*     owner;
    rstd::size_t     refs { 1 };
    MemoryResource(MemoryAllocation allocation, VkBuffer buffer, VkImage image, MemoryState* owner)
        : allocation(rstd::move(allocation)), buffer(buffer), image(image), owner(owner) {}
};
void DropMemoryResource(MemoryResource* resource) {
    if (--resource->refs != 0) return;
    auto* owner    = resource->owner;
    auto  metadata = owner->metadata;
    if (resource->buffer)
        owner->info.dispatch.destroy_buffer(owner->info.device, resource->buffer, nullptr);
    if (resource->image)
        owner->info.dispatch.destroy_image(owner->info.device, resource->image, nullptr);
    DeleteMemoryObject(metadata, resource);
}

export class AllocatedBuffer {
    MemoryResource* resource_ {};
    explicit AllocatedBuffer(MemoryResource* resource): resource_(resource) {}
    friend class MemoryAllocator;

public:
    AllocatedBuffer()                                  = default;
    AllocatedBuffer(const AllocatedBuffer&)            = delete;
    AllocatedBuffer& operator=(const AllocatedBuffer&) = delete;
    AllocatedBuffer(AllocatedBuffer&& other) noexcept
        : resource_(rstd::exchange(other.resource_, nullptr)) {}
    AllocatedBuffer& operator=(AllocatedBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            resource_ = rstd::exchange(other.resource_, nullptr);
        }
        return *this;
    }
    ~AllocatedBuffer() { reset(); }
    void reset() {
        if (resource_) {
            DropMemoryResource(resource_);
            resource_ = nullptr;
        }
    }
    bool     valid() const noexcept { return resource_ != nullptr; }
    VkBuffer handle() const noexcept { return resource_ ? resource_->buffer : VK_NULL_HANDLE; }
    AllocatedBuffer clone() const {
        if (resource_) ++resource_->refs;
        return AllocatedBuffer(resource_);
    }
    MemoryAllocation allocation() const {
        return resource_ ? resource_->allocation.clone() : MemoryAllocation();
    }
    // Retain a buffer clone until all GPU users of the returned address have completed.
    auto device_address() const -> Result<VkDeviceAddress, MemoryError> {
        if (! resource_) return Err(MemoryError { MemoryErrorKind::InvalidRequest });
        if (! (resource_->allocation.info().allocation_flags &
               VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT))
            return Err(MemoryError { MemoryErrorKind::Unsupported });
        const auto&               info = resource_->owner->info;
        VkBufferDeviceAddressInfo query { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                                          nullptr,
                                          resource_->buffer };
        auto address = info.dispatch.buffer_device_address(info.device, &query);
        if (! address) return Err(ApiMemoryError(VK_ERROR_UNKNOWN));
        return Ok(address);
    }
};
export class AllocatedImage {
    MemoryResource* resource_ {};
    explicit AllocatedImage(MemoryResource* resource): resource_(resource) {}
    friend class MemoryAllocator;

public:
    AllocatedImage()                                 = default;
    AllocatedImage(const AllocatedImage&)            = delete;
    AllocatedImage& operator=(const AllocatedImage&) = delete;
    AllocatedImage(AllocatedImage&& other) noexcept
        : resource_(rstd::exchange(other.resource_, nullptr)) {}
    AllocatedImage& operator=(AllocatedImage&& other) noexcept {
        if (this != &other) {
            reset();
            resource_ = rstd::exchange(other.resource_, nullptr);
        }
        return *this;
    }
    ~AllocatedImage() { reset(); }
    void reset() {
        if (resource_) {
            DropMemoryResource(resource_);
            resource_ = nullptr;
        }
    }
    bool           valid() const noexcept { return resource_ != nullptr; }
    VkImage        handle() const noexcept { return resource_ ? resource_->image : VK_NULL_HANDLE; }
    AllocatedImage clone() const {
        if (resource_) ++resource_->refs;
        return AllocatedImage(resource_);
    }
    MemoryAllocation allocation() const {
        return resource_ ? resource_->allocation.clone() : MemoryAllocation();
    }
};

struct MemoryPlacement {
    MemoryBlock*           block;
    alloc::RangeAllocation range;
    bool                   new_block {};
};
export class MemoryAllocator {
    MemoryState*    state_ {};
    MemoryBlockSet* set_ {};
    friend class MemoryPool;
    explicit MemoryAllocator(MemoryState* state): state_(state), set_(&state->defaults) {}
    explicit MemoryAllocator(MemoryBlockSet* set): state_(set->owner), set_(set) {}
    auto create_block(VkDeviceSize bytes, rstd::uint32_t type, MemoryClass cls,
                      VkMemoryAllocateFlags flags, bool separate, VkBuffer buffer, VkImage image,
                      bool within_budget) const -> Result<MemoryBlock*, MemoryError>;
    auto allocate_block(VkDeviceSize bytes, VkDeviceSize size, VkDeviceSize alignment,
                        rstd::uint32_t type, MemoryClass cls, VkMemoryAllocateFlags flags,
                        bool separate, VkBuffer buffer, VkImage image, bool within_budget) const
        -> Result<MemoryPlacement, MemoryError>;
    auto allocate(const VkMemoryRequirements&          requirements,
                  const VkMemoryDedicatedRequirements& dedicated,
                  const ResourceMemoryConstraints& resource, VkBuffer buffer, VkImage image,
                  MemoryRequest request) const -> Result<MemoryAllocation, MemoryError>;

public:
    MemoryAllocator()                                  = default;
    MemoryAllocator(const MemoryAllocator&)            = delete;
    MemoryAllocator& operator=(const MemoryAllocator&) = delete;
    MemoryAllocator(MemoryAllocator&& other) noexcept
        : state_(rstd::exchange(other.state_, nullptr)),
          set_(rstd::exchange(other.set_, nullptr)) {}
    MemoryAllocator& operator=(MemoryAllocator&& other) noexcept {
        if (this != &other) {
            reset();
            state_ = rstd::exchange(other.state_, nullptr);
            set_   = rstd::exchange(other.set_, nullptr);
        }
        return *this;
    }
    ~MemoryAllocator() { reset(); }
    void reset() {
        if (state_) {
            DropMemoryBlockSet(set_);
            state_ = nullptr;
            set_   = nullptr;
        }
    }
    static auto Create(VkPhysicalDevice physical, const InstanceDispatch& instance,
                       const DeviceDispatch& device, MemoryBlockPolicy policy = {},
                       MemoryMetadata metadata = alloc::allocator_ref(alloc::GLOBAL))
        -> Result<MemoryAllocator, MemoryError> {
        if (! instance.instance || device.instance != instance.instance)
            return Err(MemoryError { MemoryErrorKind::InvalidRequest });
        MemoryAllocatorCreateInfo info { physical,
                                         device.device,
                                         policy,
                                         device.capabilities.memory_budget,
                                         MemoryDispatch::FromDispatch(instance, device),
                                         device.capabilities.image_format_list,
                                         device.capabilities.buffer_device_address };
        return Create(info, metadata);
    }
    static auto Create(MemoryAllocatorCreateInfo info,
                       MemoryMetadata            metadata = alloc::allocator_ref(alloc::GLOBAL))
        -> Result<MemoryAllocator, MemoryError> {
        if (! info.device || ! info.physical_device || ! info.dispatch.valid())
            return Err(MemoryError { MemoryErrorKind::InvalidRequest });
        if (info.buffer_device_address_enabled && ! info.dispatch.buffer_device_address)
            return Err(MemoryError { MemoryErrorKind::InvalidRequest });
        const auto policy = info.block_policy;
        if (! policy.initial_size || ! policy.maximum_size ||
            policy.initial_size > policy.maximum_size || policy.shrink_attempts > 63)
            return Err(MemoryError { MemoryErrorKind::InvalidRequest });
        auto* state = NewMemoryObject<MemoryState>(metadata, info, metadata);
        if (! state) return Err(HostMemoryError());
        if (info.dispatch.properties2) {
            VkPhysicalDeviceMaintenance3Properties limits {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES
            };
            VkPhysicalDeviceProperties2 properties { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                                     &limits };
            info.dispatch.properties2(info.physical_device, &properties);
            state->properties          = properties.properties;
            state->max_allocation_size = limits.maxMemoryAllocationSize;
        } else {
            info.dispatch.properties(info.physical_device, &state->properties);
        }
        VkPhysicalDeviceMemoryProperties2 properties {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2
        };
        info.dispatch.memory_properties(info.physical_device, &properties);
        state->memory = properties.memoryProperties;
        if (state->properties.apiVersion < VK_API_VERSION_1_1 ||
            state->memory.memoryTypeCount == 0 ||
            state->properties.limits.nonCoherentAtomSize == 0) {
            DropMemoryState(state);
            return Err(MemoryError { MemoryErrorKind::Unsupported });
        }
        return Ok(MemoryAllocator(state));
    }
    // Create infos must satisfy Vulkan valid usage, including format compatibility.
    // Supported pNext data is borrowed only until the creation call returns.
    auto create_buffer(const VkBufferCreateInfo& info, MemoryRequest request = {}) const
        -> Result<AllocatedBuffer, MemoryError>;
    auto create_image(const VkImageCreateInfo& info, MemoryRequest request = {}) const
        -> Result<AllocatedImage, MemoryError>;
    auto create_pool(MemoryPoolCreateInfo info) const -> Result<MemoryPool, MemoryError>;
    auto budget() const -> MemoryBudgetSnapshot;
    void trim() const {
        if (! state_) return;
        for (usize i {}; i < state_->blocks.len();) {
            auto* block = state_->blocks[i];
            auto* set   = block->collection;
            if (block->ranges.counters().allocation_count == 0 &&
                (! set->explicit_pool || set->blocks.len().to_primitive() > set->info.min_blocks))
                DestroyMemoryBlock(state_, block);
            else
                ++i;
        }
    }
};

export class MemoryPool {
    MemoryAllocator allocator_;
    friend class MemoryAllocator;
    explicit MemoryPool(MemoryBlockSet* set): allocator_(set) {}

public:
    MemoryPool()                                 = default;
    MemoryPool(const MemoryPool&)                = delete;
    MemoryPool& operator=(const MemoryPool&)     = delete;
    MemoryPool(MemoryPool&&) noexcept            = default;
    MemoryPool& operator=(MemoryPool&&) noexcept = default;
    void        reset() { allocator_.reset(); }
    auto        create_buffer(const VkBufferCreateInfo& info, MemoryRequest request = {}) const
        -> Result<AllocatedBuffer, MemoryError> {
        return allocator_.create_buffer(info, request);
    }
    auto create_image(const VkImageCreateInfo& info, MemoryRequest request = {}) const
        -> Result<AllocatedImage, MemoryError> {
        return allocator_.create_image(info, request);
    }
    auto statistics() const -> MemoryPoolStatistics {
        return allocator_.set_ ? allocator_.set_->stats : MemoryPoolStatistics {};
    }
    void trim() const {
        if (allocator_.set_) TrimMemoryBlockSet(allocator_.set_);
    }
};
auto MemoryAllocator::create_pool(MemoryPoolCreateInfo info) const
    -> Result<MemoryPool, MemoryError> {
    const auto policy = info.block_policy;
    if (! state_ || info.memory_type >= state_->memory.memoryTypeCount ||
        (info.resource_class != MemoryClass::Linear &&
         info.resource_class != MemoryClass::Optimal) ||
        ! policy.initial_size || ! policy.maximum_size ||
        policy.initial_size > policy.maximum_size || policy.shrink_attempts > 63 ||
        (info.max_blocks && info.min_blocks > info.max_blocks) ||
        (info.min_blocks && policy.initial_size > ~VkDeviceSize(0) / info.min_blocks) ||
        (info.max_bytes && policy.initial_size * info.min_blocks > info.max_bytes))
        return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    if ((info.allocation_flags & ~VkMemoryAllocateFlags(VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT)) ||
        (info.allocation_flags && ! state_->info.buffer_device_address_enabled) ||
        (state_->memory.memoryTypes[info.memory_type].propertyFlags &
         (VK_MEMORY_PROPERTY_PROTECTED_BIT | VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD)))
        return Err(MemoryError { MemoryErrorKind::Unsupported });
    auto* set = NewMemoryObject<MemoryBlockSet>(state_->metadata, state_, state_->metadata);
    if (! set) return Err(HostMemoryError());
    set->explicit_pool                        = true;
    set->info                                 = info;
    set->info.block_policy.dedicated_fallback = false;
    ++state_->refs;
    MemoryPool result(set);
    for (rstd::uint32_t i = 0; i < info.min_blocks; ++i) {
        auto block = result.allocator_.create_block(policy.initial_size,
                                                    info.memory_type,
                                                    info.resource_class,
                                                    info.allocation_flags,
                                                    false,
                                                    VK_NULL_HANDLE,
                                                    VK_NULL_HANDLE,
                                                    info.within_budget);
        if (block.is_err()) {
            auto error = block.unwrap_err_unchecked();
            error.device_allocation_attempts += i;
            return Err(error);
        }
    }
    return Ok(rstd::move(result));
}

auto MemoryAllocator::budget() const -> MemoryBudgetSnapshot {
    MemoryBudgetSnapshot result;
    if (! state_) return result;
    result.heap_count = state_->memory.memoryHeapCount;
    for (rstd::uint32_t i = 0; i < result.heap_count; ++i) {
        result.heaps[i].heap_size = state_->memory.memoryHeaps[i].size;
        result.heaps[i].budget    = result.heaps[i].heap_size;
    }
    for (auto* block : state_->blocks) {
        auto& heap = result.heaps[state_->memory.memoryTypes[block->type].heapIndex];
        heap.block_bytes += block->size;
        ++heap.block_count;
        const auto stats = block->ranges.counters();
        heap.allocation_bytes += stats.occupied_bytes;
        heap.allocation_count += rstd::uint32_t(stats.allocation_count);
    }
    for (rstd::uint32_t i = 0; i < result.heap_count; ++i)
        result.heaps[i].usage = result.heaps[i].block_bytes;
    if (state_->info.memory_budget_enabled) {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT driver {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT
        };
        VkPhysicalDeviceMemoryProperties2 properties {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, &driver
        };
        state_->info.dispatch.memory_properties(state_->info.physical_device, &properties);
        for (rstd::uint32_t i = 0; i < result.heap_count; ++i)
            if (driver.heapBudget[i] != 0) {
                auto& heap = result.heaps[i];
                heap.usage = driver.heapUsage[i];
                heap.budget =
                    driver.heapBudget[i] < heap.heap_size ? driver.heapBudget[i] : heap.heap_size;
                heap.source = MemoryBudgetSource::Driver;
            }
    }
    return result;
}

auto MemoryAllocator::create_block(VkDeviceSize bytes, rstd::uint32_t type, MemoryClass cls,
                                   VkMemoryAllocateFlags flags, bool separate, VkBuffer buffer,
                                   VkImage image, bool within_budget) const
    -> Result<MemoryBlock*, MemoryError> {
    auto error = [&](VkResult       result,
                     rstd::uint32_t calls = 0) -> Result<MemoryBlock*, MemoryError> {
        auto value = ApiMemoryError(result);
        if (result == VK_ERROR_TOO_MANY_OBJECTS) value.kind = MemoryErrorKind::DeviceMemory;
        value.device_allocation_attempts = calls;
        value.memory_type                = type;
        value.allocation_size            = bytes;
        value.dedicated                  = separate;
        return Err(value);
    };
    if (set_->explicit_pool && bytes > ~VkDeviceSize(0) - set_->stats.block_bytes)
        return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    const auto heap_index = state_->memory.memoryTypes[type].heapIndex;
    if (bytes > state_->memory.memoryHeaps[heap_index].size || bytes > state_->max_allocation_size)
        return error(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    if (state_->blocks.len().to_primitive() >= state_->properties.limits.maxMemoryAllocationCount)
        return error(VK_ERROR_TOO_MANY_OBJECTS);
    if (within_budget) {
        const auto heap = budget().heaps[heap_index];
        if (heap.usage > heap.budget || bytes > heap.budget - heap.usage)
            return error(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    }
    if (set_->explicit_pool &&
        ((set_->info.max_blocks && set_->stats.block_count >= set_->info.max_blocks) ||
         (set_->info.max_bytes && (set_->stats.block_bytes > set_->info.max_bytes ||
                                   bytes > set_->info.max_bytes - set_->stats.block_bytes))))
        return Err(MemoryError { MemoryErrorKind::PoolCapacity });
    if (set_->blocks.try_reserve(usize(1)).is_err()) return error(VK_ERROR_OUT_OF_HOST_MEMORY);
    if (state_->blocks.try_reserve(usize(1)).is_err()) return error(VK_ERROR_OUT_OF_HOST_MEMORY);
    auto* block = NewMemoryObject<MemoryBlock>(
        state_->metadata, state_, bytes, type, cls, separate, flags, state_->metadata);
    if (! block) return error(VK_ERROR_OUT_OF_HOST_MEMORY);
    block->collection = set_;
    VkMemoryDedicatedAllocateInfo dedicated_info {
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, nullptr, image, buffer
    };
    VkMemoryAllocateFlagsInfo flags_info {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, separate ? &dedicated_info : nullptr, flags, 0
    };
    VkMemoryAllocateInfo info { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                flags      ? static_cast<const void*>(&flags_info)
                                : separate ? &dedicated_info
                                           : nullptr,
                                bytes,
                                type };
    auto                 result =
        state_->info.dispatch.allocate(state_->info.device, &info, nullptr, &block->memory);
    if (result != VK_SUCCESS) {
        DeleteMemoryObject(state_->metadata, block);
        return error(result, 1);
    }
    state_->blocks.push(rstd::move(block));
    set_->blocks.push(rstd::move(block));
    ++set_->stats.block_count;
    set_->stats.block_bytes += bytes;
    return Ok(block);
}

auto MemoryAllocator::allocate_block(VkDeviceSize bytes, VkDeviceSize size, VkDeviceSize alignment,
                                     rstd::uint32_t type, MemoryClass cls,
                                     VkMemoryAllocateFlags flags, bool separate, VkBuffer buffer,
                                     VkImage image, bool within_budget) const
    -> Result<MemoryPlacement, MemoryError> {
    auto result = create_block(bytes, type, cls, flags, separate, buffer, image, within_budget);
    if (result.is_err()) return Err(result.unwrap_err_unchecked());
    auto* block = result.unwrap_unchecked();
    auto  range = block->ranges.allocate(size, alignment);
    if (range.is_err()) {
        auto error = range.unwrap_err_unchecked() == alloc::RangeError::MetadataAllocation
                         ? HostMemoryError()
                         : ApiMemoryError(VK_ERROR_OUT_OF_DEVICE_MEMORY);
        error.device_allocation_attempts = 1;
        DestroyMemoryBlock(state_, block);
        return Err(error);
    }
    return Ok(MemoryPlacement { block, range.unwrap_unchecked(), true });
}

auto MemoryAllocator::allocate(const VkMemoryRequirements&          req,
                               const VkMemoryDedicatedRequirements& dedicated,
                               const ResourceMemoryConstraints& resource, VkBuffer buffer,
                               VkImage image, MemoryRequest request) const
    -> Result<MemoryAllocation, MemoryError> {
    if (! alloc::ValidRangeLayout(req.size, req.alignment))
        return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    auto parsed_preferences = ParseMemoryTypePreferences(request, resource);
    if (parsed_preferences.is_err()) return Err(parsed_preferences.unwrap_err_unchecked());
    const auto     preferences   = parsed_preferences.unwrap_unchecked();
    const auto     cls           = resource.resource_class;
    bool           type_fallback = false;
    rstd::uint32_t candidates    = req.memoryTypeBits;
    MemoryError    last { MemoryErrorKind::NoMemoryType, VK_ERROR_FEATURE_NOT_PRESENT };
    rstd::uint32_t calls  = 0;
    const bool     pool   = set_->explicit_pool;
    const auto     policy = pool ? set_->info.block_policy : state_->info.block_policy;
    if (pool) {
        const auto& info = set_->info;
        if (cls != info.resource_class || resource.allocation_flags != info.allocation_flags)
            return Err(MemoryError { MemoryErrorKind::PoolIncompatible });
        if (! (candidates & (1u << info.memory_type)) ||
            (state_->memory.memoryTypes[info.memory_type].propertyFlags &
             preferences.info.required) != preferences.info.required)
            return Err(MemoryError { MemoryErrorKind::NoMemoryType });
        candidates            = 1u << info.memory_type;
        request.within_budget = request.within_budget || info.within_budget;
    }
    const bool forced = request.dedicated || dedicated.requiresDedicatedAllocation;
    if (forced && (pool || request.existing_blocks_only))
        return Err(MemoryError { MemoryErrorKind::DedicatedConflict });
    const bool prefer_dedicated =
        ! pool && ! request.existing_blocks_only &&
        (forced || dedicated.prefersDedicatedAllocation || req.size > policy.maximum_size / 2);
    auto retryable = [](MemoryError error) {
        return error.api_result == VK_ERROR_OUT_OF_DEVICE_MEMORY;
    };
    auto traced = [&](MemoryError error) -> Result<MemoryAllocation, MemoryError> {
        error.device_allocation_attempts = calls;
        return Err(error);
    };
    while (candidates) {
        rstd::uint32_t selected = VK_MAX_MEMORY_TYPES, best = ~rstd::uint32_t(0);
        for (rstd::uint32_t i = 0; i < state_->memory.memoryTypeCount; ++i) {
            auto flags = state_->memory.memoryTypes[i].propertyFlags;
            if (! (candidates & (1U << i)) ||
                (flags & preferences.info.required) != preferences.info.required ||
                (flags &
                 (VK_MEMORY_PROPERTY_PROTECTED_BIT | VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD)))
                continue;
            auto score = preferences.score(flags);
            if (score < best) {
                best     = score;
                selected = i;
            }
        }
        if (selected == VK_MAX_MEMORY_TYPES) break;
        candidates &= ~(1U << selected);

        const auto         flags     = state_->memory.memoryTypes[selected].propertyFlags;
        const VkDeviceSize atom      = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                                               ! (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                           ? state_->properties.limits.nonCoherentAtomSize
                                           : 1;
        const auto         alignment = req.alignment > atom ? req.alignment : atom;
        auto               attempt   = [&](bool separate) -> Result<MemoryPlacement, MemoryError> {
            auto size = req.size;
            if (! separate && size % atom) {
                const auto padding = atom - size % atom;
                if (size > ~VkDeviceSize(0) - padding)
                    return Err(MemoryError { MemoryErrorKind::InvalidRequest });
                size += padding;
            }
            VkDeviceSize largest = 0;
            if (! separate) {
                for (auto* block : set_->blocks) {
                    if (block->dedicated || block->type != selected ||
                        block->resource_class != cls ||
                        block->allocation_flags != resource.allocation_flags)
                        continue;
                    if (block->size > largest) largest = block->size;
                    auto range = block->ranges.allocate(size, alignment);
                    if (range.is_ok())
                        return Ok(MemoryPlacement { block, range.unwrap_unchecked() });
                    if (range.unwrap_err_unchecked() == alloc::RangeError::MetadataAllocation)
                        return Err(HostMemoryError());
                }
            }
            if (request.existing_blocks_only)
                return Err(MemoryError { MemoryErrorKind::ExistingBlocksOnly });
            auto bytes = size;
            if (! separate) {
                bytes = policy.initial_size;
                if (largest) {
                    const auto doubled =
                        largest > policy.maximum_size / 2 ? policy.maximum_size : largest * 2;
                    if (doubled > bytes) bytes = doubled;
                }
                if (bytes < size) bytes = size;
                if (bytes > policy.maximum_size) {
                    auto error            = pool ? MemoryError { MemoryErrorKind::PoolCapacity }
                                                 : ApiMemoryError(VK_ERROR_OUT_OF_DEVICE_MEMORY);
                    error.memory_type     = selected;
                    error.allocation_size = bytes;
                    return Err(error);
                }
            }
            if (pool && set_->info.max_bytes) {
                const auto remaining = set_->info.max_bytes - set_->stats.block_bytes;
                if (remaining < size) return Err(MemoryError { MemoryErrorKind::PoolCapacity });
                if (bytes > remaining) bytes = remaining;
            }
            for (rstd::uint32_t shrink = 0;; ++shrink) {
                auto result = allocate_block(bytes,
                                             size,
                                             alignment,
                                             selected,
                                             cls,
                                             resource.allocation_flags,
                                             separate,
                                             buffer,
                                             image,
                                             request.within_budget);
                if (result.is_ok()) {
                    ++calls;
                    return result;
                }
                const auto error = result.unwrap_err_unchecked();
                calls += error.device_allocation_attempts;
                if (! retryable(error) || separate || shrink == policy.shrink_attempts ||
                    bytes == size)
                    return Err(error);
                auto smaller = bytes / 2;
                if (smaller < size) smaller = size;
                bytes = smaller;
            }
        };
        auto placement = attempt(prefer_dedicated);
        if (placement.is_err() && retryable(placement.unwrap_err_unchecked()) && ! forced &&
            ! pool && ! request.existing_blocks_only && policy.dedicated_fallback)
            placement = attempt(! prefer_dedicated);
        if (placement.is_err()) {
            last = placement.unwrap_err_unchecked();
            if (! retryable(last) && ! (request.existing_blocks_only &&
                                        last.kind == MemoryErrorKind::ExistingBlocksOnly))
                return traced(last);
            type_fallback = true;
            continue;
        }
        auto [block, range, new_block] = placement.unwrap_unchecked();
        auto selection                 = preferences.info;
        if (pool) selection.placement_reason = MemoryPlacementReason::FixedPool;
        selection.type_fallback              = type_fallback;
        selection.device_allocation_attempts = calls;
        if (block->dedicated)
            selection.dedicated_reason =
                dedicated.requiresDedicatedAllocation  ? MemoryDedicatedReason::Required
                : request.dedicated                    ? MemoryDedicatedReason::Requested
                : dedicated.prefersDedicatedAllocation ? MemoryDedicatedReason::DriverPreferred
                : prefer_dedicated                     ? MemoryDedicatedReason::LargeResource
                                                       : MemoryDedicatedReason::SharedExhausted;
        auto* region = NewMemoryObject<MemoryRegion>(
            state_->metadata, state_, block, range, req.size, selection);
        if (! region) {
            block->ranges.deallocate(range.id);
            if (new_block) DestroyMemoryBlock(state_, block);
            return traced(HostMemoryError());
        }
        region->rollback_block = new_block;
        MemoryAllocation result(region);
        if (request.persistent_mapping) {
            auto mapped = MapMemoryBlock(block);
            if (mapped != VK_SUCCESS) return traced(ApiMemoryError(mapped));
            region->persistent = true;
        }
        return Ok(rstd::move(result));
    }
    return traced(last);
}

auto MemoryAllocator::create_buffer(const VkBufferCreateInfo& info, MemoryRequest request) const
    -> Result<AllocatedBuffer, MemoryError> {
    if (! state_) return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    auto parsed = ParseResourceMemoryConstraints(info);
    if (parsed.is_err()) return Err(parsed.unwrap_err_unchecked());
    const auto constraints = parsed.unwrap_unchecked();
    if (constraints.allocation_flags && ! state_->info.buffer_device_address_enabled)
        return Err(MemoryError { MemoryErrorKind::Unsupported });
    VkBuffer buffer {};
    auto result = state_->info.dispatch.create_buffer(state_->info.device, &info, nullptr, &buffer);
    if (result != VK_SUCCESS) return Err(ApiMemoryError(result));
    VkMemoryDedicatedRequirements dedicated { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
    VkMemoryRequirements2 requirements { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicated };
    VkBufferMemoryRequirementsInfo2 query { VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
                                            nullptr,
                                            buffer };
    state_->info.dispatch.buffer_requirements(state_->info.device, &query, &requirements);
    auto allocation = allocate(
        requirements.memoryRequirements, dedicated, constraints, buffer, VK_NULL_HANDLE, request);
    if (allocation.is_err()) {
        state_->info.dispatch.destroy_buffer(state_->info.device, buffer, nullptr);
        return Err(allocation.unwrap_err_unchecked());
    }
    auto memory      = allocation.unwrap_unchecked();
    auto memory_info = memory.info();
    result           = state_->info.dispatch.bind_buffer(
        state_->info.device, buffer, memory_info.memory, memory_info.offset);
    if (result != VK_SUCCESS) {
        state_->info.dispatch.destroy_buffer(state_->info.device, buffer, nullptr);
        auto error                       = ApiMemoryError(result);
        error.device_allocation_attempts = memory_info.selection.device_allocation_attempts;
        return Err(error);
    }
    auto* resource = NewMemoryObject<MemoryResource>(
        state_->metadata, rstd::move(memory), buffer, VK_NULL_HANDLE, state_);
    if (! resource) {
        state_->info.dispatch.destroy_buffer(state_->info.device, buffer, nullptr);
        auto error                       = HostMemoryError();
        error.device_allocation_attempts = memory_info.selection.device_allocation_attempts;
        return Err(error);
    }
    resource->allocation.region_->rollback_block = false;
    return Ok(AllocatedBuffer(resource));
}
auto MemoryAllocator::create_image(const VkImageCreateInfo& info, MemoryRequest request) const
    -> Result<AllocatedImage, MemoryError> {
    if (! state_) return Err(MemoryError { MemoryErrorKind::InvalidRequest });
    auto parsed = ParseResourceMemoryConstraints(info, state_->info.image_format_list_enabled);
    if (parsed.is_err()) return Err(parsed.unwrap_err_unchecked());
    const auto constraints = parsed.unwrap_unchecked();
    VkImage    image {};
    auto result = state_->info.dispatch.create_image(state_->info.device, &info, nullptr, &image);
    if (result != VK_SUCCESS) return Err(ApiMemoryError(result));
    VkMemoryDedicatedRequirements dedicated { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
    VkMemoryRequirements2 requirements { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicated };
    VkImageMemoryRequirementsInfo2 query { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
                                           nullptr,
                                           image };
    state_->info.dispatch.image_requirements(state_->info.device, &query, &requirements);
    auto allocation = allocate(
        requirements.memoryRequirements, dedicated, constraints, VK_NULL_HANDLE, image, request);
    if (allocation.is_err()) {
        state_->info.dispatch.destroy_image(state_->info.device, image, nullptr);
        return Err(allocation.unwrap_err_unchecked());
    }
    auto memory      = allocation.unwrap_unchecked();
    auto memory_info = memory.info();
    result           = state_->info.dispatch.bind_image(
        state_->info.device, image, memory_info.memory, memory_info.offset);
    if (result != VK_SUCCESS) {
        state_->info.dispatch.destroy_image(state_->info.device, image, nullptr);
        auto error                       = ApiMemoryError(result);
        error.device_allocation_attempts = memory_info.selection.device_allocation_attempts;
        return Err(error);
    }
    auto* resource = NewMemoryObject<MemoryResource>(
        state_->metadata, rstd::move(memory), VK_NULL_HANDLE, image, state_);
    if (! resource) {
        state_->info.dispatch.destroy_image(state_->info.device, image, nullptr);
        auto error                       = HostMemoryError();
        error.device_allocation_attempts = memory_info.selection.device_allocation_attempts;
        return Err(error);
    }
    resource->allocation.region_->rollback_block = false;
    return Ok(AllocatedImage(resource));
}
} // namespace vvk
