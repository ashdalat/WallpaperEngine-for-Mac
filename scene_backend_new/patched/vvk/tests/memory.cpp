#include <vulkan/vk_platform.h>
#include <rstd/test/gtest.hpp>
import rstd;
import vvk;
using namespace rstd::prelude;

namespace
{
struct FakeMemory {
    struct Block {
        bool                  live {};
        VkDeviceSize          size {};
        unsigned              type {};
        unsigned              maps {};
        VkMemoryAllocateFlags flags {};
        unsigned char         data[8192] {};
    } blocks[128];
    struct Buffer {
        bool               live {};
        VkDeviceSize       size {};
        VkDeviceMemory     memory {};
        VkBufferUsageFlags usage {};
        VkDeviceSize       offset {};
    } buffers[128];
    struct Image {
        bool           live {};
        VkDeviceMemory memory {};
    } images[128];
    unsigned next_block { 1 }, next_buffer { 1 }, next_image { 1 };
    unsigned allocations {}, frees {}, creates {}, destroys {}, queries {}, maps {}, unmaps {},
        flushes {}, invalidates {};
    bool fail_create {}, fail_allocate {}, fail_bind {}, fail_map {}, dedicated {},
        prefer_dedicated {}, reject_device {};
    bool         driver_budget {}, device_lost {};
    VkDeviceSize budget { 8192 };
    VkDeviceSize heap_size { 1024 * 1024 }, max_allocation_size { 8192 }, max_success_size { 8192 };
    VkDeviceSize driver_usage { 256 }, atom { 64 }, requirement_size {};
    unsigned     max_allocations { 128 }, budget_queries {};
    bool         reject_shared {}, reject_dedicated {};
    VkResult     allocation_error { VK_SUCCESS };
    struct Attempt {
        VkDeviceSize size;
        unsigned     type;
        bool         dedicated;
    } attempts[256] {};
    VkPhysicalDeviceMemoryProperties topology {};
    unsigned                         requirement_types { 3 }, reject_types {};
    unsigned                         address_queries {};
    bool                             zero_address {};
    unsigned                         attempt_count {};
    unsigned                         fail_at_attempt {};
    VkMappedMemoryRange              last_range {};
    VkImageCreateFlags               image_flags {};
    const void*                      image_chain {};
}* fake;
template<typename T>
T handle(unsigned id) {
    return vvk::HandleFromIdentity<T>(id);
}
template<typename T>
unsigned index(T value) {
    return unsigned(vvk::OpaqueHandleIdentity(value));
}
VKAPI_ATTR void VKAPI_CALL Properties(VkPhysicalDevice, VkPhysicalDeviceProperties* p) {
    *p                                 = {};
    p->apiVersion                      = VK_API_VERSION_1_1;
    p->limits.nonCoherentAtomSize      = fake->atom;
    p->limits.bufferImageGranularity   = 256;
    p->limits.maxMemoryAllocationCount = fake->max_allocations;
}
VKAPI_ATTR void VKAPI_CALL Properties2(VkPhysicalDevice gpu, VkPhysicalDeviceProperties2* p) {
    Properties(gpu, &p->properties);
    auto* limits = static_cast<VkPhysicalDeviceMaintenance3Properties*>(p->pNext);
    EXPECT_EQ(limits->sType, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES);
    limits->maxMemoryAllocationSize = fake->max_allocation_size;
}
VKAPI_ATTR void VKAPI_CALL MemoryProperties(VkPhysicalDevice,
                                            VkPhysicalDeviceMemoryProperties2* p) {
    p->memoryProperties                     = {};
    p->memoryProperties.memoryHeapCount     = 1;
    p->memoryProperties.memoryHeaps[0].size = fake->heap_size;
    p->memoryProperties.memoryTypeCount     = 2;
    p->memoryProperties.memoryTypes[0]      = { VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0 };
    p->memoryProperties.memoryTypes[1]      = { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 };
    if (fake->topology.memoryTypeCount) p->memoryProperties = fake->topology;
    if (p->pNext) {
        ++fake->budget_queries;
        auto* b          = static_cast<VkPhysicalDeviceMemoryBudgetPropertiesEXT*>(p->pNext);
        b->heapBudget[0] = fake->driver_budget ? fake->budget : 0;
        b->heapUsage[0]  = fake->driver_usage;
    }
}
VKAPI_ATTR VkResult VKAPI_CALL Allocate(VkDevice, const VkMemoryAllocateInfo*         info,
                                        const VkAllocationCallbacks*, VkDeviceMemory* out) {
    const VkMemoryDedicatedAllocateInfo* dedicated_info   = nullptr;
    VkMemoryAllocateFlags                allocation_flags = 0;
    for (auto* next = static_cast<const VkBaseInStructure*>(info->pNext); next;
         next       = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO) {
            auto* flags      = reinterpret_cast<const VkMemoryAllocateFlagsInfo*>(next);
            allocation_flags = flags->flags;
            EXPECT_EQ(flags->deviceMask, 0u);
        } else if (next->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO)
            dedicated_info = reinterpret_cast<const VkMemoryDedicatedAllocateInfo*>(next);
        else
            EXPECT_TRUE(false);
    }
    const bool separate                   = dedicated_info != nullptr;
    fake->attempts[fake->attempt_count++] = { info->allocationSize,
                                              info->memoryTypeIndex,
                                              separate };
    if (fake->fail_at_attempt && fake->attempt_count == fake->fail_at_attempt)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (fake->reject_types & (1u << info->memoryTypeIndex)) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (fake->allocation_error != VK_SUCCESS) return fake->allocation_error;
    if ((separate && fake->reject_dedicated) || (! separate && fake->reject_shared) ||
        info->allocationSize > fake->max_success_size)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (fake->device_lost) return VK_ERROR_DEVICE_LOST;
    if (fake->fail_allocate || (fake->reject_device && info->memoryTypeIndex == 1))
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;

    auto i                = fake->next_block++;
    fake->blocks[i].live  = true;
    fake->blocks[i].size  = info->allocationSize;
    fake->blocks[i].type  = info->memoryTypeIndex;
    fake->blocks[i].flags = allocation_flags;
    *out                  = handle<VkDeviceMemory>(i);
    ++fake->allocations;
    if (fake->dedicated) {
        EXPECT_NE(info->pNext, nullptr);
        EXPECT_NE(dedicated_info, nullptr);
        EXPECT_TRUE(dedicated_info->buffer || dedicated_info->image);
    }
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL Free(VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks*) {
    auto& block = fake->blocks[index(memory)];
    EXPECT_TRUE(block.live);
    for (const auto& b : fake->buffers) EXPECT_FALSE(b.live && b.memory == memory);
    for (const auto& i : fake->images) EXPECT_FALSE(i.live && i.memory == memory);
    block.live = false;
    ++fake->frees;
}
VKAPI_ATTR VkResult VKAPI_CALL CreateBuffer(VkDevice, const VkBufferCreateInfo*     info,
                                            const VkAllocationCallbacks*, VkBuffer* out) {
    if (fake->fail_create) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    auto i                 = fake->next_buffer++;
    fake->buffers[i].live  = true;
    fake->buffers[i].size  = info->size;
    fake->buffers[i].usage = info->usage;
    *out                   = handle<VkBuffer>(i);
    ++fake->creates;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL DestroyBuffer(VkDevice, VkBuffer buffer, const VkAllocationCallbacks*) {
    EXPECT_TRUE(fake->buffers[index(buffer)].live);
    fake->buffers[index(buffer)].live = false;
    ++fake->destroys;
}
VKAPI_ATTR VkResult VKAPI_CALL CreateImage(VkDevice, const VkImageCreateInfo*     info,
                                           const VkAllocationCallbacks*, VkImage* out) {
    if (fake->fail_create) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    fake->image_flags    = info->flags;
    fake->image_chain    = info->pNext;
    auto i               = fake->next_image++;
    fake->images[i].live = true;
    *out                 = handle<VkImage>(i);
    ++fake->creates;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL DestroyImage(VkDevice, VkImage image, const VkAllocationCallbacks*) {
    EXPECT_TRUE(fake->images[index(image)].live);
    fake->images[index(image)].live = false;
    ++fake->destroys;
}
void Requirements(VkMemoryRequirements2* req, VkDeviceSize size) {
    ++fake->queries;
    req->memoryRequirements = { size, 16, fake->requirement_types };
    auto* dedicated         = static_cast<VkMemoryDedicatedRequirements*>(req->pNext);
    EXPECT_NE(dedicated, nullptr);
    dedicated->requiresDedicatedAllocation = fake->dedicated;
    dedicated->prefersDedicatedAllocation  = fake->prefer_dedicated;
}
VKAPI_ATTR void VKAPI_CALL BufferRequirements(VkDevice, const VkBufferMemoryRequirementsInfo2* info,
                                              VkMemoryRequirements2* req) {
    Requirements(req,
                 fake->requirement_size ? fake->requirement_size
                                        : fake->buffers[index(info->buffer)].size);
}
VKAPI_ATTR void VKAPI_CALL ImageRequirements(VkDevice, const VkImageMemoryRequirementsInfo2*,
                                             VkMemoryRequirements2* req) {
    Requirements(req, 128);
}
VKAPI_ATTR VkResult VKAPI_CALL BindBuffer(VkDevice, VkBuffer buffer, VkDeviceMemory memory,
                                          VkDeviceSize offset) {
    if (fake->fail_bind) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    EXPECT_EQ(offset % 16, 0u);
    fake->buffers[index(buffer)].memory = memory;
    fake->buffers[index(buffer)].offset = offset;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL BindImage(VkDevice, VkImage image, VkDeviceMemory memory,
                                         VkDeviceSize offset) {
    if (fake->fail_bind) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    EXPECT_EQ(offset % 16, 0u);
    fake->images[index(image)].memory = memory;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL Map(VkDevice, VkDeviceMemory memory, VkDeviceSize offset,
                                   VkDeviceSize, VkMemoryMapFlags, void**        out) {
    if (fake->fail_map) return VK_ERROR_MEMORY_MAP_FAILED;
    auto& b = fake->blocks[index(memory)];
    if (b.size > sizeof(b.data)) return VK_ERROR_MEMORY_MAP_FAILED;
    EXPECT_EQ(b.maps, 0u);
    ++b.maps;
    ++fake->maps;
    *out = b.data + offset;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL Unmap(VkDevice, VkDeviceMemory memory) {
    auto& b = fake->blocks[index(memory)];
    EXPECT_EQ(b.maps, 1u);
    --b.maps;
    ++fake->unmaps;
}
VKAPI_ATTR VkResult VKAPI_CALL Flush(VkDevice, unsigned count, const VkMappedMemoryRange* ranges) {
    EXPECT_EQ(count, 1u);
    fake->last_range = *ranges;
    ++fake->flushes;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL Invalidate(VkDevice, unsigned count,
                                          const VkMappedMemoryRange* ranges) {
    EXPECT_EQ(count, 1u);
    fake->last_range = *ranges;
    ++fake->invalidates;
    return VK_SUCCESS;
}
VKAPI_ATTR VkDeviceAddress VKAPI_CALL BufferAddress(VkDevice,
                                                    const VkBufferDeviceAddressInfo* info) {
    ++fake->address_queries;
    const auto& buffer = fake->buffers[index(info->buffer)];
    EXPECT_TRUE(buffer.live);
    EXPECT_TRUE(buffer.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    EXPECT_TRUE(fake->blocks[index(buffer.memory)].flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);
    return fake->zero_address ? 0 : VkDeviceAddress(index(buffer.memory)) * 65536 + buffer.offset;
}
auto MakeAllocator(vvk::MemoryMetadata    metadata    = alloc::allocator_ref(alloc::GLOBAL),
                   bool                   format_list = false,
                   vvk::MemoryBlockPolicy policy = { 1024, 1024, 0, false }, bool address = false) {
    vvk::MemoryDispatch            dispatch { Properties,
                                              MemoryProperties,
                                              Allocate,
                                              Free,
                                              CreateBuffer,
                                              DestroyBuffer,
                                              CreateImage,
                                              DestroyImage,
                                              BufferRequirements,
                                              ImageRequirements,
                                              BindBuffer,
                                              BindImage,
                                              Map,
                                              Unmap,
                                              Flush,
                                              Invalidate,
                                              Properties2,
                                              BufferAddress };
    vvk::MemoryAllocatorCreateInfo info { handle<VkPhysicalDevice>(1),
                                          handle<VkDevice>(1),
                                          policy,
                                          true,
                                          dispatch,
                                          format_list,
                                          address };
    return vvk::MemoryAllocator::Create(info, metadata);
}
auto BufferInfo(VkDeviceSize size = 73) -> VkBufferCreateInfo {
    return {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
}
auto ImageInfo() -> VkImageCreateInfo {
    VkImageCreateInfo i { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    i.imageType = VK_IMAGE_TYPE_2D;
    i.format    = VK_FORMAT_R8G8B8A8_UNORM;
    i.extent    = { 4, 4, 1 };
    i.mipLevels = i.arrayLayers = 1;
    i.samples                   = VK_SAMPLE_COUNT_1_BIT;
    i.tiling                    = VK_IMAGE_TILING_OPTIMAL;
    i.usage                     = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return i;
}
} // namespace

struct MemoryFailMetadata {
    int remaining { -1 };
    int live {};
};
namespace rstd
{
template<>
struct Impl<alloc::Allocator, ::MemoryFailMetadata>
    : DefaultInImpl<alloc::Allocator, ::MemoryFailMetadata> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        auto& s = const_cast<::MemoryFailMetadata&>(this->self());
        if (s.remaining == 0) return Err(alloc::AllocError {});
        if (s.remaining > 0) --s.remaining;
        auto result = as<alloc::Allocator>(::alloc::GLOBAL).allocate(layout);
        if (result.is_ok()) ++s.live;
        return result;
    }
    void deallocate(void* pointer, alloc::Layout layout) const noexcept {
        --const_cast<::MemoryFailMetadata&>(this->self()).live;
        as<alloc::Allocator>(::alloc::GLOBAL).deallocate(pointer, layout);
    }
};
} // namespace rstd
TEST(Memory, PoolsRequirementsAndDedicated) {
    FakeMemory context;
    fake = &context;
    {
        auto allocator = MakeAllocator().unwrap_unchecked();
        auto a         = allocator.create_buffer(BufferInfo()).unwrap_unchecked();
        auto b         = allocator.create_buffer(BufferInfo()).unwrap_unchecked();
        EXPECT_EQ(fake->queries, 2u);
        EXPECT_EQ(fake->allocations, 1u);
        auto ai = a.allocation().info(), bi = b.allocation().info();
        EXPECT_EQ(ai.memory, bi.memory);
        EXPECT_NE(ai.offset, bi.offset);
        EXPECT_EQ(ai.memory_type, 1u);
        auto image = allocator.create_image(ImageInfo()).unwrap_unchecked();
        EXPECT_NE(image.allocation().info().memory, ai.memory);
        fake->dedicated = true;
        auto separate   = allocator.create_buffer(BufferInfo()).unwrap_unchecked();
        EXPECT_TRUE(separate.allocation().info().dedicated);
        EXPECT_EQ(separate.allocation().info().offset, 0u);
    }
    EXPECT_EQ(fake->allocations, fake->frees);
    EXPECT_EQ(fake->creates, fake->destroys);
}
TEST(Memory, MappingAtomsAndLifetime) {
    FakeMemory context;
    fake                         = &context;
    auto               allocator = MakeAllocator().unwrap_unchecked();
    vvk::MemoryRequest request { .required           = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                 .persistent_mapping = true };
    auto               a      = allocator.create_buffer(BufferInfo(), request).unwrap_unchecked();
    auto               b      = allocator.create_buffer(BufferInfo(), request).unwrap_unchecked();
    auto               memory = a.allocation();
    EXPECT_EQ(memory.info().occupied_size, 128u);
    EXPECT_EQ(b.allocation().info().offset % 64, 0u);
    {
        auto m      = memory.map(3, 5).unwrap_unchecked();
        auto second = memory.map().unwrap_unchecked();
        EXPECT_EQ(fake->maps, 1u);
        EXPECT_TRUE(memory.flush(3, 5).is_ok());
        EXPECT_EQ(fake->last_range.offset, 0u);
        EXPECT_EQ(fake->last_range.size, 64u);
        EXPECT_TRUE(b.allocation().invalidate(70, 3).is_ok());
        EXPECT_EQ(fake->last_range.offset, 192u);
        EXPECT_EQ(fake->last_range.size, 64u);
        EXPECT_TRUE(memory.flush(74, 1).is_err());
        EXPECT_TRUE(memory.map(73, 1).is_err());
        allocator.reset();
        a.reset();
        b.reset();
        memory.reset();
        EXPECT_EQ(fake->frees, 0u);
    }
    EXPECT_EQ(fake->frees, 1u);
    EXPECT_EQ(fake->unmaps, 1u);
}
TEST(Memory, VulkanFailuresAndUnsupportedRequests) {
    for (unsigned mode = 0; mode < 5; ++mode) {
        FakeMemory context;
        fake = &context;
        {
            auto allocator      = MakeAllocator().unwrap_unchecked();
            fake->fail_create   = mode == 0;
            fake->fail_allocate = mode == 1;
            fake->fail_bind     = mode == 2;
            fake->fail_map      = mode == 3;
            fake->device_lost   = mode == 4;
            auto result         = allocator.create_buffer(
                BufferInfo(),
                { .required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, .persistent_mapping = true });
            EXPECT_TRUE(result.is_err());
            if (mode == 4)
                EXPECT_EQ(result.unwrap_err_unchecked().api_result, VK_ERROR_DEVICE_LOST);
            EXPECT_EQ(allocator.budget().heaps[0].allocation_count, 0u);
            allocator.trim();
        }
        EXPECT_EQ(fake->allocations, fake->frees);
        EXPECT_EQ(fake->creates, fake->destroys);
    }
    FakeMemory context;
    fake           = &context;
    auto allocator = MakeAllocator().unwrap_unchecked();
    auto info      = BufferInfo();
    info.flags     = VK_BUFFER_CREATE_SPARSE_BINDING_BIT;
    EXPECT_TRUE(allocator.create_buffer(info).is_err());
    info       = BufferInfo();
    info.pNext = &info;
    EXPECT_TRUE(allocator.create_buffer(info).is_err());
    EXPECT_EQ(fake->creates, 0u);
}
TEST(Memory, HostMetadataFailureRollback) {
    bool succeeded = false;
    for (int fail = 0; fail < 20; ++fail) {
        FakeMemory context;
        fake = &context;
        MemoryFailMetadata metadata { fail };
        {
            auto allocator_result = MakeAllocator(alloc::allocator_ref(metadata));
            if (allocator_result.is_ok()) {
                auto allocator = allocator_result.unwrap_unchecked();
                auto result    = allocator.create_buffer(BufferInfo());
                if (result.is_ok())
                    succeeded = true;
                else
                    EXPECT_EQ(allocator.budget().heaps[0].allocation_count, 0u);
            }
        }
        EXPECT_EQ(metadata.live, 0);
        EXPECT_EQ(fake->allocations, fake->frees);
        EXPECT_EQ(fake->creates, fake->destroys);
    }
    EXPECT_TRUE(succeeded);
}
TEST(Memory, BudgetFallbackAndMemoryTypeFallback) {
    FakeMemory context;
    fake           = &context;
    auto allocator = MakeAllocator().unwrap_unchecked();
    EXPECT_EQ(allocator.budget().heaps[0].source, vvk::MemoryBudgetSource::AllocatorEstimate);
    fake->driver_budget = true;
    fake->budget        = 500;
    EXPECT_EQ(allocator.budget().heaps[0].source, vvk::MemoryBudgetSource::Driver);
    EXPECT_TRUE(allocator.create_buffer(BufferInfo(), { .within_budget = true }).is_err());
    fake->budget        = 8192;
    fake->reject_device = true;
    auto buffer         = allocator.create_buffer(BufferInfo()).unwrap_unchecked();
    EXPECT_EQ(buffer.allocation().info().memory_type, 0u);
    buffer.reset();
    allocator.trim();
    EXPECT_EQ(allocator.budget().heaps[0].block_count, 0u);
}
TEST(Memory, SubmissionLeaseNeedsEveryCompletionSource) {
    FakeMemory context;
    fake                  = &context;
    auto allocator        = MakeAllocator().unwrap_unchecked();
    auto buffer           = allocator.create_buffer(BufferInfo()).unwrap_unchecked();
    auto submission_lease = buffer.clone();
    buffer.reset();
    auto queue =
        vvk::MakeQueueDomain(handle<VkDevice>(1), handle<VkQueue>(1), u32(0), u32(0), u64(1));
    auto source  = vvk::MakeTimelineCompletionSource(queue, handle<VkSemaphore>(1), u64(1));
    auto source2 = vvk::MakeTimelineCompletionSource(queue, handle<VkSemaphore>(2), u64(1));
    vvk::SubmissionToken     a { source, u64(3) }, b { source2, u64(7) };
    vvk::CompletedWatermarks completed;
    completed.Observe({ source, u64(100) });
    EXPECT_TRUE(completed.Covers(a));
    EXPECT_FALSE(completed.Covers(b));
    auto stale       = source2;
    stale.generation = u64(2);
    completed.Observe({ stale, u64(100) });
    EXPECT_FALSE(completed.Covers(b));
    EXPECT_EQ(allocator.budget().heaps[0].allocation_count, 1u);
    vvk::TimelineCompletionObserver observer(
        handle<VkDevice>(1),
        +[](VkDevice, VkSemaphore, rstd::uint64_t*) -> VkResult {
            return VK_ERROR_DEVICE_LOST;
        },
        +[](VkDevice, VkSemaphore, rstd::uint64_t, rstd::uint64_t) -> VkResult {
            return VK_ERROR_DEVICE_LOST;
        });
    auto observation = observer.Poll(source2);
    EXPECT_FALSE(observation.observed());
    EXPECT_EQ(observation.status, vvk::CompletionObservationStatus::DeviceLost);
    completed.Observe(b);
    if (completed.Covers(a) && completed.Covers(b)) submission_lease.reset();
    EXPECT_EQ(allocator.budget().heaps[0].allocation_count, 0u);
}
TEST(Memory, EmptyBlockRetentionAndDedicatedTail) {
    FakeMemory context;
    fake           = &context;
    auto allocator = MakeAllocator().unwrap_unchecked();
    auto a         = allocator.create_buffer(BufferInfo(400)).unwrap_unchecked();
    auto b         = allocator.create_buffer(BufferInfo(400)).unwrap_unchecked();
    auto c         = allocator.create_buffer(BufferInfo(400)).unwrap_unchecked();
    EXPECT_EQ(allocator.budget().heaps[0].block_count, 2u);
    a.reset();
    b.reset();
    c.reset();
    EXPECT_EQ(allocator.budget().heaps[0].block_count, 1u);
    allocator.trim();
    EXPECT_EQ(allocator.budget().heaps[0].block_count, 0u);
    fake->prefer_dedicated = true;
    auto tail              = allocator
                                 .create_buffer(BufferInfo(73),
                                                { .required           = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                                  .persistent_mapping = true })
                                 .unwrap_unchecked();
    auto memory            = tail.allocation();
    EXPECT_TRUE(memory.info().dedicated);
    EXPECT_TRUE(memory.flush(70, 3).is_ok());
    EXPECT_EQ(fake->last_range.offset, 64u);
    EXPECT_EQ(fake->last_range.size, 9u);
    memory.reset();
    tail.reset();
    EXPECT_EQ(allocator.budget().heaps[0].block_count, 0u);
    fake->prefer_dedicated = false;
    fake->fail_bind        = true;
    EXPECT_TRUE(allocator.create_image(ImageInfo()).is_err());
    allocator.trim();
    EXPECT_EQ(fake->allocations, fake->frees);
    EXPECT_EQ(fake->creates, fake->destroys);
    auto unsupported  = ImageInfo();
    unsupported.flags = VK_IMAGE_CREATE_DISJOINT_BIT;
    EXPECT_TRUE(allocator.create_image(unsupported).is_err());
}

TEST(Memory, RingSubmissionCompletionAndAtomIsolation) {
    FakeMemory context;
    fake           = &context;
    auto allocator = MakeAllocator().unwrap_unchecked();
    auto upload    = allocator
                         .create_buffer(BufferInfo(256),
                                        { .required           = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                          .persistent_mapping = true })
                         .unwrap_unchecked();
    auto memory    = upload.allocation();
    auto mapping   = memory.map().unwrap_unchecked();
    alloc::RingRangeAllocator ranges(256);
    auto                      first  = ranges.allocate(128, 64).unwrap_unchecked();
    auto                      second = ranges.allocate(128, 64).unwrap_unchecked();
    auto*                     bytes  = static_cast<unsigned char*>(mapping.data());
    for (unsigned i = 0; i < 73; ++i) {
        bytes[first.offset + i]  = 0x35;
        bytes[second.offset + i] = 0x97;
    }
    EXPECT_TRUE(memory.flush(first.offset, 73).is_ok());
    EXPECT_EQ(fake->last_range.offset, 0u);
    EXPECT_EQ(fake->last_range.size, 128u);
    EXPECT_TRUE(memory.flush(second.offset, 73).is_ok());
    EXPECT_EQ(fake->last_range.offset, 128u);
    EXPECT_EQ(fake->last_range.size, 128u);
    auto queue =
        vvk::MakeQueueDomain(handle<VkDevice>(1), handle<VkQueue>(1), u32(0), u32(0), u64(1));
    auto source_a = vvk::MakeTimelineCompletionSource(queue, handle<VkSemaphore>(1), u64(1));
    auto source_b = vvk::MakeTimelineCompletionSource(queue, handle<VkSemaphore>(2), u64(1));
    struct Pending {
        alloc::RingRangeAllocation range;
        vvk::SubmissionToken       requirements[2];
        unsigned                   count;
        vvk::AllocatedBuffer       lease;
    };
    alloc::vec::Vec<Pending> pending;
    pending.push(
        Pending { first, { { source_a, u64(3) }, { source_b, u64(5) } }, 2, upload.clone() });
    pending.push(Pending { second, { { source_b, u64(7) }, {} }, 1, upload.clone() });
    upload.reset();
    vvk::CompletedWatermarks completed;
    auto                     collect = [&] {
        while (! pending.is_empty()) {
            const auto& first_pending = pending[usize(0)];
            bool        ready         = true;
            for (unsigned i = 0; i < first_pending.count; ++i)
                ready &= completed.Covers(first_pending.requirements[i]);
            if (! ready) break;
            auto retired = pending.remove(usize(0));
            EXPECT_TRUE(ranges.release(retired.range.id).is_ok());
        }
    };
    completed.Observe({ source_b, u64(7) });
    collect();
    EXPECT_EQ(pending.len(), usize(2));
    EXPECT_EQ(ranges.statistics().free_bytes, 0u);
    EXPECT_EQ(ranges.release(second.id).unwrap_err_unchecked(), alloc::RangeError::OutOfOrder);
    EXPECT_TRUE(ranges.allocate(1, 1).is_err());
    EXPECT_EQ(fake->destroys, 0u);
    auto stale       = source_a;
    stale.generation = u64(2);
    completed.Observe({ stale, u64(100) });
    collect();
    EXPECT_EQ(pending.len(), usize(2));
    vvk::TimelineCompletionObserver lost(
        handle<VkDevice>(1),
        +[](VkDevice, VkSemaphore, rstd::uint64_t*) -> VkResult {
            return VK_ERROR_DEVICE_LOST;
        },
        +[](VkDevice, VkSemaphore, rstd::uint64_t, rstd::uint64_t) -> VkResult {
            return VK_ERROR_DEVICE_LOST;
        });
    auto observation = lost.Poll(source_a);
    EXPECT_EQ(observation.status, vvk::CompletionObservationStatus::DeviceLost);
    if (observation.observed()) completed.Observe(observation.completed);
    collect();
    EXPECT_EQ(pending.len(), usize(2));
    EXPECT_EQ(fake->destroys, 0u);
    for (unsigned i = 0; i < 73; ++i) {
        EXPECT_EQ(bytes[first.offset + i], 0x35u);
        EXPECT_EQ(bytes[second.offset + i], 0x97u);
    }
    completed.Observe({ source_a, u64(3) });
    collect();
    EXPECT_TRUE(pending.is_empty());
    EXPECT_EQ(fake->destroys, 1u);
    EXPECT_EQ(ranges.statistics().free_bytes, 256u);
    auto reused = ranges.allocate(256, 64).unwrap_unchecked();
    EXPECT_EQ(reused.offset, 0u);
    EXPECT_TRUE(ranges.release(first.id).is_err());
    EXPECT_TRUE(ranges.release(reused.id).is_ok());
}

TEST(Memory, ImageConstraintsAndBorrowedChain) {
    FakeMemory context;
    fake           = &context;
    auto allocator = MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), true).unwrap_unchecked();
    VkFormat                    formats[] { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB };
    VkImageFormatListCreateInfo list {
        VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO, nullptr, 2, formats
    };
    auto info        = ImageInfo();
    info.flags       = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT | VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    info.arrayLayers = 6;
    info.pNext       = &list;
    {
        auto image = allocator.create_image(info);
        ASSERT_TRUE(image.is_ok());
        EXPECT_EQ(fake->image_flags, info.flags);
        EXPECT_EQ(fake->image_chain, &list);
        EXPECT_EQ(fake->queries, 1u);
    }
    auto reject = [&](vvk::MemoryErrorKind kind) {
        auto result = allocator.create_image(info);
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.unwrap_err_unchecked().kind, kind);
        EXPECT_EQ(fake->creates, 1u);
        EXPECT_EQ(fake->queries, 1u);
    };
    list.pNext = &list;
    reject(vvk::MemoryErrorKind::InvalidRequest);
    VkImageFormatListCreateInfo duplicate = list;
    duplicate.pNext                       = nullptr;
    list.pNext                            = &duplicate;
    reject(vvk::MemoryErrorKind::InvalidRequest);
    VkExternalMemoryImageCreateInfo external {
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO
    };
    list.pNext = &external;
    reject(vvk::MemoryErrorKind::Unsupported);
    list.pNext        = nullptr;
    list.pViewFormats = nullptr;
    reject(vvk::MemoryErrorKind::InvalidRequest);
    list.pViewFormats = formats;
    formats[1]        = VK_FORMAT_UNDEFINED;
    reject(vvk::MemoryErrorKind::InvalidRequest);
    formats[1] = VK_FORMAT_R8G8B8A8_SRGB;
    info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    reject(vvk::MemoryErrorKind::InvalidRequest);
    info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    info.extent.height = 2;
    reject(vvk::MemoryErrorKind::InvalidRequest);
    info.extent.height = 4;
    info.arrayLayers   = 5;
    reject(vvk::MemoryErrorKind::InvalidRequest);
    info.arrayLayers = 6;
    info.imageType   = VK_IMAGE_TYPE_3D;
    reject(vvk::MemoryErrorKind::InvalidRequest);
    info.imageType = VK_IMAGE_TYPE_2D;
    info.samples   = VK_SAMPLE_COUNT_4_BIT;
    reject(vvk::MemoryErrorKind::InvalidRequest);
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    const VkImageCreateFlags unsupported_flags[] { VK_IMAGE_CREATE_SPARSE_BINDING_BIT,
                                                   VK_IMAGE_CREATE_DISJOINT_BIT,
                                                   VK_IMAGE_CREATE_PROTECTED_BIT,
                                                   VK_IMAGE_CREATE_ALIAS_BIT };
    for (auto flag : unsupported_flags) {
        info.flags = flag;
        reject(vvk::MemoryErrorKind::Unsupported);
    }
    info.flags              = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    auto without_capability = MakeAllocator().unwrap_unchecked();
    auto rejected           = without_capability.create_image(info);
    ASSERT_TRUE(rejected.is_err());
    EXPECT_EQ(rejected.unwrap_err_unchecked().kind, vvk::MemoryErrorKind::Unsupported);
    info.flags           = 0;
    list.viewFormatCount = 0;
    list.pViewFormats    = nullptr;
    EXPECT_TRUE(allocator.create_image(info).is_ok());
    list.viewFormatCount = 1;
    list.pViewFormats    = formats;
    EXPECT_TRUE(allocator.create_image(info).is_ok());
    info.pNext = nullptr;
    info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    EXPECT_TRUE(without_capability.create_image(info).is_ok());
}

TEST(Memory, CompatibleImageFailureRollback) {
    bool succeeded = false;
    for (int failure = 0; failure < 24; ++failure) {
        FakeMemory context;
        fake = &context;
        MemoryFailMetadata metadata { failure < 4 ? -1 : failure - 4 };
        {
            auto made = MakeAllocator(alloc::allocator_ref(metadata), true);
            if (made.is_ok()) {
                auto allocator = made.unwrap_unchecked();
                auto info      = ImageInfo();
                info.flags =
                    VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT | VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
                info.arrayLayers                   = 6;
                VkFormat                    format = info.format;
                VkImageFormatListCreateInfo list {
                    VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO, nullptr, 1, &format
                };
                info.pNext          = &list;
                fake->fail_create   = failure == 0;
                fake->fail_allocate = failure == 1;
                fake->fail_bind     = failure == 2;
                fake->device_lost   = failure == 3;
                {
                    auto image = allocator.create_image(info);
                    succeeded |= image.is_ok();
                    if (failure < 4) {
                        ASSERT_TRUE(image.is_err());
                    }
                    if (failure == 3)
                        EXPECT_EQ(image.unwrap_err_unchecked().api_result, VK_ERROR_DEVICE_LOST);
                    EXPECT_EQ(fake->queries, fake->creates);
                }
                EXPECT_EQ(allocator.budget().heaps[0].allocation_count, 0u);
            }
        }
        EXPECT_EQ(metadata.live, 0);
        EXPECT_EQ(fake->creates, fake->destroys);
        EXPECT_EQ(fake->allocations, fake->frees);
    }
    EXPECT_TRUE(succeeded);
}

TEST(Memory, GrowingBlocksAndTrim) {
    FakeMemory context;
    fake = &context;
    auto allocator =
        MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 256, 1024, 3, true })
            .unwrap_unchecked();
    vvk::AllocatedBuffer buffers[10];
    for (auto& buffer : buffers) {
        auto result = allocator.create_buffer(BufferInfo(256));
        ASSERT_TRUE(result.is_ok());
        buffer = result.unwrap_unchecked();
    }
    ASSERT_EQ(fake->attempt_count, 4u);
    EXPECT_EQ(fake->attempts[0].size, 256u);
    EXPECT_EQ(fake->attempts[1].size, 512u);
    EXPECT_EQ(fake->attempts[2].size, 1024u);
    EXPECT_EQ(fake->attempts[3].size, 1024u);
    EXPECT_EQ(allocator.budget().heaps[0].allocation_count, 10u);
    for (auto& buffer : buffers) buffer.reset();
    EXPECT_EQ(allocator.budget().heaps[0].block_count, 1u);
    allocator.trim();
    EXPECT_EQ(fake->allocations, fake->frees);
    auto fresh = allocator.create_buffer(BufferInfo(256));
    ASSERT_TRUE(fresh.is_ok());
    EXPECT_EQ(fake->attempts[4].size, 256u);
}

TEST(Memory, ShrinkAndDedicatedFallbackOrder) {
    for (unsigned mode = 0; mode < 5; ++mode) {
        FakeMemory context;
        fake                   = &context;
        fake->max_success_size = mode == 0 ? 256 : 8192;
        fake->reject_shared    = mode == 1;
        fake->reject_dedicated = mode >= 2;
        fake->prefer_dedicated = mode == 2;
        const auto policy      = vvk::MemoryBlockPolicy { 1024, 1024, mode == 0 ? 3u : 0u, true };
        auto       allocator =
            MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, policy).unwrap_unchecked();
        auto result =
            allocator.create_buffer(BufferInfo(mode == 3 ? 600 : 128), { .dedicated = mode == 4 });
        if (mode == 4) {
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(fake->attempt_count, 2u);
            EXPECT_TRUE(fake->attempts[0].dedicated);
            EXPECT_TRUE(fake->attempts[1].dedicated);
            EXPECT_NE(fake->attempts[0].type, fake->attempts[1].type);
        } else {
            ASSERT_TRUE(result.is_ok());
            auto buffer = result.unwrap_unchecked();
            EXPECT_EQ(buffer.allocation().info().memory_type, 1u);
            EXPECT_EQ(buffer.allocation().info().dedicated, mode == 1);
            if (mode == 0) {
                ASSERT_EQ(fake->attempt_count, 3u);
                EXPECT_EQ(fake->attempts[0].size, 1024u);
                EXPECT_EQ(fake->attempts[1].size, 512u);
                EXPECT_EQ(fake->attempts[2].size, 256u);
            } else {
                ASSERT_EQ(fake->attempt_count, 2u);
                EXPECT_EQ(fake->attempts[0].dedicated, mode != 1);
                EXPECT_EQ(fake->attempts[1].dedicated, mode == 1);
                EXPECT_EQ(fake->attempts[0].type, fake->attempts[1].type);
            }
        }
        EXPECT_EQ(fake->queries, 1u);
    }
    FakeMemory context;
    fake                   = &context;
    fake->dedicated        = true;
    fake->reject_dedicated = true;
    auto allocator =
        MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 256, 1024, 3, true })
            .unwrap_unchecked();
    auto result = allocator.create_image(ImageInfo());
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(fake->attempt_count, 2u);
    EXPECT_TRUE(fake->attempts[0].dedicated);
    EXPECT_TRUE(fake->attempts[1].dedicated);
}

TEST(Memory, BudgetChargesPhysicalBlocksAndRefreshes) {
    FakeMemory context;
    fake                = &context;
    fake->driver_budget = true;
    fake->budget        = 600;
    fake->driver_usage  = 256;
    auto allocator =
        MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 1024, 1024, 3, true })
            .unwrap_unchecked();
    auto first = allocator.create_buffer(BufferInfo(128), { .within_budget = true });
    ASSERT_TRUE(first.is_ok());
    EXPECT_EQ(fake->attempt_count, 1u);
    EXPECT_EQ(fake->attempts[0].size, 256u);
    EXPECT_EQ(fake->budget_queries, 3u);
    fake->driver_usage = 700;
    auto second        = allocator.create_buffer(BufferInfo(128), { .within_budget = true });
    ASSERT_TRUE(second.is_ok());
    EXPECT_EQ(fake->attempt_count, 1u);
    EXPECT_EQ(fake->budget_queries, 3u);
    auto rejected = allocator.create_buffer(BufferInfo(128), { .within_budget = true });
    ASSERT_TRUE(rejected.is_err());
    EXPECT_EQ(fake->attempt_count, 1u);
    EXPECT_GT(fake->budget_queries, 3u);
    EXPECT_EQ(allocator.budget().heaps[0].allocation_count, 2u);
}

TEST(Memory, AllocationErrorsDoNotRetry) {
    const VkResult errors[] { VK_ERROR_OUT_OF_HOST_MEMORY,
                              VK_ERROR_DEVICE_LOST,
                              VK_ERROR_TOO_MANY_OBJECTS,
                              VK_ERROR_UNKNOWN };
    for (auto error : errors) {
        FakeMemory context;
        fake                   = &context;
        fake->allocation_error = error;
        auto allocator =
            MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 1024, 1024, 3, true })
                .unwrap_unchecked();
        auto result = allocator.create_buffer(BufferInfo(128));
        ASSERT_TRUE(result.is_err());
        const auto detail = result.unwrap_err_unchecked();
        EXPECT_EQ(detail.api_result, error);
        EXPECT_EQ(detail.device_allocation_attempts, 1u);
        EXPECT_EQ(detail.memory_type, 1u);
        EXPECT_EQ(detail.allocation_size, 1024u);
        EXPECT_FALSE(detail.dedicated);
        EXPECT_EQ(fake->attempt_count, 1u);
        EXPECT_EQ(fake->creates, fake->destroys);
    }
}

TEST(Memory, PolicyLimitsAndExhaustion) {
    FakeMemory context;
    fake = &context;
    const vvk::MemoryBlockPolicy invalid[] {
        { 0, 0 }, { 0, 1024 }, { 1024, 0 }, { 1024, 256 }, { 1, 1, 64 }
    };
    for (auto policy : invalid) {
        auto result = MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, policy);
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.unwrap_err_unchecked().kind, vvk::MemoryErrorKind::InvalidRequest);
    }
    fake->max_allocation_size = 256;
    fake->heap_size           = 512;
    auto allocator =
        MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 1024, 2048, 3, false })
            .unwrap_unchecked();
    auto result = allocator.create_buffer(BufferInfo(128));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(fake->attempt_count, 1u);
    EXPECT_EQ(fake->attempts[0].size, 256u);
    fake->fail_allocate = true;
    auto failed         = allocator.create_buffer(BufferInfo(256));
    ASSERT_TRUE(failed.is_err());
    EXPECT_EQ(failed.unwrap_err_unchecked().device_allocation_attempts, 2u);
    EXPECT_EQ(fake->attempt_count, 3u);
    EXPECT_EQ(fake->attempts[1].type, 1u);
    EXPECT_EQ(fake->attempts[2].type, 0u);
    EXPECT_EQ(allocator.budget().heaps[0].allocation_count, 1u);
}

TEST(Memory, GrowthSaturatesAndAtomOverflowStops) {
    {
        FakeMemory context;
        fake            = &context;
        fake->heap_size = fake->max_allocation_size = fake->max_success_size = ~VkDeviceSize(0);
        const auto initial     = VkDeviceSize(1) << 63;
        auto       allocator   = MakeAllocator(alloc::allocator_ref(alloc::GLOBAL),
                                               false,
                                               { initial, ~VkDeviceSize(0), 0, false })
                                     .unwrap_unchecked();
        fake->requirement_size = initial - 1;
        auto large             = allocator.create_buffer(BufferInfo());
        ASSERT_TRUE(large.is_ok());
        fake->requirement_size = 0;
        fake->allocation_error = VK_ERROR_DEVICE_LOST;
        auto failed            = allocator.create_buffer(BufferInfo());
        ASSERT_TRUE(failed.is_err());
        ASSERT_EQ(fake->attempt_count, 2u);
        EXPECT_EQ(fake->attempts[0].size, initial);
        EXPECT_EQ(fake->attempts[1].size, ~VkDeviceSize(0));
        EXPECT_EQ(failed.unwrap_err_unchecked().api_result, VK_ERROR_DEVICE_LOST);
    }
    {
        FakeMemory context;
        fake                   = &context;
        fake->requirement_size = ~VkDeviceSize(0);
        auto allocator =
            MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 256, 1024, 3, true })
                .unwrap_unchecked();
        auto result = allocator.create_buffer(BufferInfo(),
                                              { .required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT });
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.unwrap_err_unchecked().kind, vvk::MemoryErrorKind::InvalidRequest);
        EXPECT_EQ(fake->attempt_count, 0u);
    }
}

TEST(Memory, AllocationCountAndEstimatedBudget) {
    for (unsigned mode = 0; mode < 2; ++mode) {
        FakeMemory context;
        fake                  = &context;
        fake->max_allocations = mode == 0 ? 1 : 128;
        fake->heap_size       = 512;
        auto allocator =
            MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 512, 512, 0, false })
                .unwrap_unchecked();
        auto first = allocator.create_buffer(BufferInfo(256), { .within_budget = true });
        ASSERT_TRUE(first.is_ok());
        auto second = allocator.create_buffer(BufferInfo(256), { .within_budget = true });
        ASSERT_TRUE(second.is_ok());
        auto failed = allocator.create_buffer(BufferInfo(256), { .within_budget = true });
        ASSERT_TRUE(failed.is_err());
        EXPECT_EQ(fake->attempt_count, 1u);
        EXPECT_EQ(failed.unwrap_err_unchecked().device_allocation_attempts, 0u);
        EXPECT_EQ(failed.unwrap_err_unchecked().api_result,
                  mode == 0 ? VK_ERROR_TOO_MANY_OBJECTS : VK_ERROR_OUT_OF_DEVICE_MEMORY);
        auto heap = allocator.budget().heaps[0];
        EXPECT_EQ(heap.source, vvk::MemoryBudgetSource::AllocatorEstimate);
        EXPECT_EQ(heap.block_bytes, 512u);
        EXPECT_EQ(heap.allocation_count, 2u);
    }
}

TEST(Memory, PolicyMetadataMappingAndBindingRollback) {
    bool succeeded = false;
    for (int failure = 0; failure < 24; ++failure) {
        FakeMemory context;
        fake = &context;
        MemoryFailMetadata metadata { failure < 2 ? -1 : failure - 2 };
        {
            auto made =
                MakeAllocator(alloc::allocator_ref(metadata), false, { 256, 1024, 3, true });
            if (made.is_ok()) {
                auto allocator  = made.unwrap_unchecked();
                fake->fail_bind = failure == 0;
                fake->fail_map  = failure == 1;
                {
                    auto result =
                        allocator.create_buffer(BufferInfo(73), { .persistent_mapping = true });
                    succeeded |= result.is_ok();
                    if (failure < 2) {
                        EXPECT_TRUE(result.is_err());
                    }
                    EXPECT_LE(fake->attempt_count, 1u);
                }
                EXPECT_EQ(allocator.budget().heaps[0].allocation_count, 0u);
            }
        }
        EXPECT_EQ(metadata.live, 0);
        EXPECT_EQ(fake->creates, fake->destroys);
        EXPECT_EQ(fake->allocations, fake->frees);
        EXPECT_EQ(fake->maps, fake->unmaps);
    }
    EXPECT_TRUE(succeeded);
}

TEST(Memory, ShrinkBoundsAndAtomPadding) {
    {
        FakeMemory context;
        fake                   = &context;
        fake->max_success_size = 128;
        auto allocator =
            MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 1000, 1000, 63, false })
                .unwrap_unchecked();
        auto result = allocator.create_buffer(BufferInfo(73),
                                              { .required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT });
        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(fake->attempt_count, 4u);
        EXPECT_EQ(fake->attempts[0].size, 1000u);
        EXPECT_EQ(fake->attempts[1].size, 500u);
        EXPECT_EQ(fake->attempts[2].size, 250u);
        EXPECT_EQ(fake->attempts[3].size, 128u);
        auto buffer = result.unwrap_unchecked();
        EXPECT_EQ(buffer.allocation().info().occupied_size, 128u);
    }
    {
        FakeMemory context;
        fake                = &context;
        fake->fail_allocate = true;
        auto allocator =
            MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 1024, 1024, 2, true })
                .unwrap_unchecked();
        auto result = allocator.create_buffer(BufferInfo(73));
        ASSERT_TRUE(result.is_err());
        ASSERT_EQ(fake->attempt_count, 8u);
        EXPECT_EQ(result.unwrap_err_unchecked().device_allocation_attempts, 8u);
        for (unsigned i = 0; i < 8; ++i) {
            EXPECT_EQ(fake->attempts[i].type, i < 4 ? 1u : 0u);
            EXPECT_EQ(fake->attempts[i].dedicated, i % 4 == 3);
            EXPECT_EQ(fake->attempts[i].size, i % 4 == 3 ? 73u : 1024u >> (i % 4));
        }
        EXPECT_EQ(fake->allocations, 0u);
        EXPECT_EQ(fake->creates, fake->destroys);
    }
}

TEST(Memory, DedicatedPreferenceReusesSharedUnderBudgetPressure) {
    FakeMemory context;
    fake = &context;
    auto allocator =
        MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 256, 1024, 3, true })
            .unwrap_unchecked();
    auto first = allocator.create_buffer(BufferInfo(128));
    ASSERT_TRUE(first.is_ok());
    fake->driver_budget    = true;
    fake->budget           = 1;
    fake->prefer_dedicated = true;
    auto second            = allocator.create_buffer(BufferInfo(128), { .within_budget = true });
    ASSERT_TRUE(second.is_ok());
    auto buffer = second.unwrap_unchecked();
    EXPECT_FALSE(buffer.allocation().info().dedicated);
    EXPECT_EQ(fake->attempt_count, 1u);
    EXPECT_EQ(allocator.budget().heaps[0].allocation_count, 2u);
}

TEST(Memory, DefaultPolicyGrowthAndRecovery) {
    constexpr VkDeviceSize mib = 1024 * 1024;
    for (unsigned mode = 0; mode < 4; ++mode) {
        FakeMemory context;
        fake            = &context;
        fake->heap_size = fake->max_allocation_size = fake->max_success_size = 64 * mib;
        fake->max_success_size = mode == 1 ? mib : 64 * mib;
        fake->reject_shared    = mode == 2;
        fake->reject_dedicated = mode == 3;
        fake->prefer_dedicated = mode == 3;
        {
            auto allocator =
                MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, {}).unwrap_unchecked();
            vvk::AllocatedBuffer buffers[13];
            const unsigned       count = mode == 0 ? 13 : 1;
            for (unsigned i = 0; i < count; ++i) {
                auto result = allocator.create_buffer(BufferInfo(mib));
                ASSERT_TRUE(result.is_ok());
                buffers[i] = result.unwrap_unchecked();
                EXPECT_EQ(buffers[i].allocation().info().memory_type, 1u);
                EXPECT_EQ(buffers[i].allocation().info().dedicated, mode == 2);
            }
            if (mode == 0) {
                ASSERT_EQ(fake->attempt_count, 3u);
                EXPECT_EQ(fake->attempts[0].size, 4 * mib);
                EXPECT_EQ(fake->attempts[1].size, 8 * mib);
                EXPECT_EQ(fake->attempts[2].size, 16 * mib);
            } else if (mode == 1) {
                ASSERT_EQ(fake->attempt_count, 3u);
                EXPECT_EQ(fake->attempts[0].size, 4 * mib);
                EXPECT_EQ(fake->attempts[1].size, 2 * mib);
                EXPECT_EQ(fake->attempts[2].size, mib);
            } else if (mode == 2) {
                ASSERT_EQ(fake->attempt_count, 4u);
                EXPECT_EQ(fake->attempts[2].size, mib);
                EXPECT_TRUE(fake->attempts[3].dedicated);
                EXPECT_EQ(fake->attempts[3].size, mib);
            } else {
                ASSERT_EQ(fake->attempt_count, 2u);
                EXPECT_TRUE(fake->attempts[0].dedicated);
                EXPECT_FALSE(fake->attempts[1].dedicated);
            }
            EXPECT_EQ(fake->queries, count);
        }
        EXPECT_EQ(fake->allocations, fake->frees);
        EXPECT_EQ(fake->creates, fake->destroys);
    }
}

namespace
{
void DiscreteTopology(FakeMemory& context) {
    auto& memory           = context.topology;
    memory.memoryHeapCount = 2;
    memory.memoryHeaps[0]  = { 1024 * 1024, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
    memory.memoryHeaps[1]  = { 1024 * 1024, 0 };
    memory.memoryTypeCount = 4;
    memory.memoryTypes[0]  = { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 };
    memory.memoryTypes[1]  = {
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1
    };
    memory.memoryTypes[2] = {
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 1
    };
    memory.memoryTypes[3]     = { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  0 };
    context.requirement_types = 15;
}
} // namespace
TEST(Memory, HostAccessAndPlacementSelection) {
    FakeMemory context;
    fake = &context;
    DiscreteTopology(context);
    auto allocator = MakeAllocator().unwrap_unchecked();
    auto check     = [&](vvk::MemoryRequest         request,
                         VkBufferUsageFlags         usage,
                         unsigned                   type,
                         vvk::MemoryPlacementReason reason) {
        auto info   = BufferInfo();
        info.usage  = usage;
        auto result = allocator.create_buffer(info, request);
        ASSERT_TRUE(result.is_ok());
        auto buffer   = result.unwrap_unchecked();
        auto selected = buffer.allocation().info();
        EXPECT_EQ(selected.memory_type, type);
        EXPECT_EQ(selected.heap, context.topology.memoryTypes[type].heapIndex);
        EXPECT_EQ(selected.selection.placement_reason, reason);
        if (request.host_access != vvk::MemoryHostAccess::None || request.persistent_mapping)
            EXPECT_TRUE(selected.selection.required & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    };
    check({}, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, vvk::MemoryPlacementReason::AutomaticDevice);
    check(vvk::MemoryRequest::Upload(),
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          1,
          vvk::MemoryPlacementReason::AutomaticHost);
    check(vvk::MemoryRequest::Upload(),
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          3,
          vvk::MemoryPlacementReason::AutomaticDevice);
    check(vvk::MemoryRequest::Readback(),
          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          2,
          vvk::MemoryPlacementReason::AutomaticHost);
    auto upload       = vvk::MemoryRequest::Upload();
    upload.preference = vvk::MemoryPreference::Host;
    check(upload, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 1, vvk::MemoryPlacementReason::RequestedHost);
    upload.preference = vvk::MemoryPreference::Device;
    check(upload, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 3, vvk::MemoryPlacementReason::RequestedDevice);
    check({ .persistent_mapping = true },
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          3,
          vvk::MemoryPlacementReason::AutomaticDevice);
    for (unsigned direct = 0; direct < 2; ++direct) {
        auto image_info = ImageInfo();
        if (direct) image_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        auto result = allocator.create_image(image_info, vvk::MemoryRequest::Upload());
        ASSERT_TRUE(result.is_ok());
        auto image = result.unwrap_unchecked();
        EXPECT_EQ(image.allocation().info().memory_type, direct ? 3u : 1u);
    }
}

TEST(Memory, ExplicitFlagsAndHardConstraints) {
    FakeMemory context;
    fake = &context;
    DiscreteTopology(context);
    auto allocator   = MakeAllocator().unwrap_unchecked();
    auto upload      = vvk::MemoryRequest::Upload();
    upload.preferred = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    auto result      = allocator.create_buffer(BufferInfo(), upload);
    ASSERT_TRUE(result.is_ok());
    auto buffer   = result.unwrap_unchecked();
    auto selected = buffer.allocation().info();
    EXPECT_EQ(selected.memory_type, 2u);
    EXPECT_EQ(selected.selection.explicit_preferred, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    EXPECT_TRUE(selected.selection.avoided & VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    auto readback     = vvk::MemoryRequest::Readback();
    readback.required = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    auto local        = allocator.create_buffer(BufferInfo(), readback);
    ASSERT_TRUE(local.is_ok());
    auto local_buffer = local.unwrap_unchecked();
    EXPECT_EQ(local_buffer.allocation().info().memory_type, 3u);
    readback.required |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    auto impossible = allocator.create_buffer(BufferInfo(), readback);
    ASSERT_TRUE(impossible.is_err());
    EXPECT_EQ(impossible.unwrap_err_unchecked().kind, vvk::MemoryErrorKind::NoMemoryType);
    context.requirement_types = 1;
    auto restricted = allocator.create_buffer(BufferInfo(), vvk::MemoryRequest::Readback());
    ASSERT_TRUE(restricted.is_err());
    EXPECT_EQ(restricted.unwrap_err_unchecked().kind, vvk::MemoryErrorKind::NoMemoryType);
    EXPECT_EQ(restricted.unwrap_err_unchecked().device_allocation_attempts, 0u);
    context.requirement_types = 15;
    auto flags = allocator.create_buffer(BufferInfo(),
                                         { .required   = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                           .preferred  = VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                           .preference = vvk::MemoryPreference::FlagsOnly });
    ASSERT_TRUE(flags.is_ok());
    auto flags_buffer = flags.unwrap_unchecked();
    auto detail       = flags_buffer.allocation().info();
    EXPECT_EQ(detail.memory_type, 2u);
    EXPECT_EQ(detail.selection.preferred, 0u);
    EXPECT_EQ(detail.selection.avoided, 0u);
    EXPECT_EQ(detail.selection.placement_reason, vvk::MemoryPlacementReason::ExplicitFlags);
}

TEST(Memory, FlagsOnlyWithoutInferredPreferences) {
    FakeMemory context;
    fake           = &context;
    auto allocator = MakeAllocator().unwrap_unchecked();
    auto result =
        allocator.create_buffer(BufferInfo(), { .preference = vvk::MemoryPreference::FlagsOnly });
    ASSERT_TRUE(result.is_ok());
    auto buffer = result.unwrap_unchecked();
    EXPECT_EQ(buffer.allocation().info().memory_type, 0u);
    auto automatic = allocator.create_buffer(BufferInfo());
    ASSERT_TRUE(automatic.is_ok());
    auto automatic_buffer = automatic.unwrap_unchecked();
    EXPECT_EQ(automatic_buffer.allocation().info().memory_type, 1u);
    auto request       = vvk::MemoryRequest::Upload();
    request.preference = vvk::MemoryPreference::FlagsOnly;
    request.preferred  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    auto mapped        = allocator.create_buffer(BufferInfo(), request);
    ASSERT_TRUE(mapped.is_ok());
    auto mapped_buffer = mapped.unwrap_unchecked();
    EXPECT_EQ(mapped_buffer.allocation().info().memory_type, 0u);
}

TEST(Memory, UnifiedMemoryAndUncachedReadback) {
    for (unsigned mode = 0; mode < 2; ++mode) {
        FakeMemory context;
        fake                             = &context;
        context.topology.memoryHeapCount = 1;
        context.topology.memoryHeaps[0]  = { 1024 * 1024, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
        context.topology.memoryTypeCount = 1;
        context.topology.memoryTypes[0]  = {
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                (mode == 0 ? VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_HOST_CACHED_BIT) : 0u),
            0
        };
        context.requirement_types = 1;
        auto allocator            = MakeAllocator().unwrap_unchecked();
        auto request = mode == 0 ? vvk::MemoryRequest::Upload() : vvk::MemoryRequest::Readback();
        auto result  = allocator.create_buffer(BufferInfo(), request);
        ASSERT_TRUE(result.is_ok());
        auto buffer = result.unwrap_unchecked();
        EXPECT_EQ(buffer.allocation().info().memory_type, 0u);
        EXPECT_EQ(buffer.allocation().info().heap, 0u);
    }
}

TEST(Memory, SelectionFallbackAndSharedHeapAccounting) {
    FakeMemory context;
    fake = &context;
    DiscreteTopology(context);
    auto allocator = MakeAllocator().unwrap_unchecked();
    auto readback  = allocator.create_buffer(BufferInfo(), vvk::MemoryRequest::Readback());
    ASSERT_TRUE(readback.is_ok());
    auto first   = readback.unwrap_unchecked();
    auto memory  = first.allocation();
    auto mapping = memory.map();
    ASSERT_TRUE(mapping.is_ok());
    EXPECT_TRUE(memory.invalidate().is_ok());
    EXPECT_EQ(context.invalidates, 1u);
    auto upload = allocator.create_buffer(BufferInfo(), vvk::MemoryRequest::Upload());
    ASSERT_TRUE(upload.is_ok());
    auto second   = upload.unwrap_unchecked();
    auto snapshot = allocator.budget();
    EXPECT_EQ(snapshot.heaps[1].block_count, 2u);
    EXPECT_EQ(snapshot.heaps[1].allocation_count, 2u);
    context.reject_types = 1 << 2;
    auto fallback        = allocator.create_buffer(
        BufferInfo(512), { .dedicated = true, .host_access = vvk::MemoryHostAccess::Random });
    ASSERT_TRUE(fallback.is_ok());
    auto third    = fallback.unwrap_unchecked();
    auto selected = third.allocation().info();
    EXPECT_EQ(selected.memory_type, 1u);
    EXPECT_TRUE(selected.selection.type_fallback);
    EXPECT_EQ(selected.selection.device_allocation_attempts, 2u);
    EXPECT_EQ(selected.selection.dedicated_reason, vvk::MemoryDedicatedReason::Requested);
    auto lease = third.allocation();
    third.reset();
    EXPECT_EQ(lease.info().selection.dedicated_reason, vvk::MemoryDedicatedReason::Requested);
}

TEST(Memory, DedicatedSelectionReasons) {
    for (unsigned mode = 0; mode < 5; ++mode) {
        FakeMemory context;
        fake                     = &context;
        context.dedicated        = mode == 0;
        context.prefer_dedicated = mode == 2;
        context.reject_shared    = mode == 4;
        auto allocator =
            MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 1024, 1024, 0, true })
                .unwrap_unchecked();
        auto result =
            allocator.create_buffer(BufferInfo(mode == 3 ? 600 : 73), { .dedicated = mode == 1 });
        ASSERT_TRUE(result.is_ok());
        auto                             buffer = result.unwrap_unchecked();
        const vvk::MemoryDedicatedReason reasons[] { vvk::MemoryDedicatedReason::Required,
                                                     vvk::MemoryDedicatedReason::Requested,
                                                     vvk::MemoryDedicatedReason::DriverPreferred,
                                                     vvk::MemoryDedicatedReason::LargeResource,
                                                     vvk::MemoryDedicatedReason::SharedExhausted };
        EXPECT_EQ(buffer.allocation().info().selection.dedicated_reason, reasons[mode]);
    }
}

TEST(Memory, InvalidAccessIntentRollsBack) {
    FakeMemory context;
    fake                = &context;
    auto allocator      = MakeAllocator().unwrap_unchecked();
    auto invalid        = vvk::MemoryRequest {};
    invalid.host_access = static_cast<vvk::MemoryHostAccess>(99);
    auto first          = allocator.create_buffer(BufferInfo(), invalid);
    ASSERT_TRUE(first.is_err());
    EXPECT_EQ(first.unwrap_err_unchecked().kind, vvk::MemoryErrorKind::InvalidRequest);
    invalid            = {};
    invalid.preference = static_cast<vvk::MemoryPreference>(99);
    auto second        = allocator.create_image(ImageInfo(), invalid);
    ASSERT_TRUE(second.is_err());
    EXPECT_EQ(second.unwrap_err_unchecked().kind, vvk::MemoryErrorKind::InvalidRequest);
    EXPECT_EQ(fake->attempt_count, 0u);
    EXPECT_EQ(fake->creates, fake->destroys);
}

TEST(Memory, BufferAddressCapabilityAndBlockIsolation) {
    FakeMemory context;
    fake      = &context;
    auto info = BufferInfo(128);
    info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    auto disabled = MakeAllocator().unwrap_unchecked();
    auto denied   = disabled.create_buffer(info);
    ASSERT_TRUE(denied.is_err());
    EXPECT_EQ(denied.unwrap_err_unchecked().kind, vvk::MemoryErrorKind::Unsupported);
    EXPECT_EQ(fake->creates, 0u);
    auto allocator =
        MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 256, 1024, 0, true }, true)
            .unwrap_unchecked();
    auto plain  = allocator.create_buffer(BufferInfo(128)).unwrap_unchecked();
    auto first  = allocator.create_buffer(info).unwrap_unchecked();
    auto second = allocator.create_buffer(info).unwrap_unchecked();
    EXPECT_NE(plain.allocation().info().memory, first.allocation().info().memory);
    EXPECT_EQ(first.allocation().info().memory, second.allocation().info().memory);
    EXPECT_EQ(first.allocation().info().allocation_flags, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);
    EXPECT_EQ(plain.allocation().info().allocation_flags, 0u);
    auto capture  = info;
    capture.flags = VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT;
    EXPECT_TRUE(allocator.create_buffer(capture).is_err());
    VkBufferOpaqueCaptureAddressCreateInfo opaque {
        VK_STRUCTURE_TYPE_BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO
    };
    capture.flags = 0;
    capture.pNext = &opaque;
    EXPECT_TRUE(allocator.create_buffer(capture).is_err());
    auto a = first.device_address(), b = second.device_address();
    ASSERT_TRUE(a.is_ok());
    ASSERT_TRUE(b.is_ok());
    EXPECT_NE(a.unwrap_unchecked(), b.unwrap_unchecked());
    EXPECT_EQ(b.unwrap_unchecked() - a.unwrap_unchecked(), 128u);
    auto clone = second.clone();
    second.reset();
    EXPECT_EQ(clone.device_address().unwrap_unchecked(), b.unwrap_unchecked());
    EXPECT_TRUE(second.device_address().is_err());
    EXPECT_TRUE(plain.device_address().is_err());
    fake->zero_address = true;
    EXPECT_TRUE(first.device_address().is_err());
    fake->zero_address = false;
    plain.reset();
    first.reset();
    clone.reset();
    EXPECT_EQ(allocator.budget().heaps[0].block_count, 2u);
    allocator.trim();
    EXPECT_EQ(fake->allocations, fake->frees);
    EXPECT_EQ(fake->creates, fake->destroys);
}

TEST(Memory, AddressAllocationRollbackAndDedicatedChain) {
    bool succeeded = false;
    for (int mode = 0; mode < 24; ++mode) {
        FakeMemory context;
        fake = &context;
        MemoryFailMetadata metadata { mode < 4 ? -1 : mode - 4 };
        {
            auto made =
                MakeAllocator(alloc::allocator_ref(metadata), false, { 1024, 1024, 0, true }, true);
            if (made.is_ok()) {
                auto allocator = made.unwrap_unchecked();
                auto info      = BufferInfo();
                info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                fake->dedicated     = true;
                fake->fail_allocate = mode == 0;
                fake->fail_bind     = mode == 1;
                fake->fail_create   = mode == 2;
                fake->device_lost   = mode == 3;
                {
                    auto result = allocator.create_buffer(info);
                    if (mode < 4) {
                        EXPECT_TRUE(result.is_err());
                    }
                    if (result.is_ok()) {
                        auto buffer = result.unwrap_unchecked();
                        EXPECT_TRUE(buffer.allocation().info().dedicated);
                        EXPECT_TRUE(buffer.device_address().is_ok());
                        succeeded = true;
                    }
                }
                EXPECT_EQ(allocator.budget().heaps[0].allocation_count, 0u);
            }
        }
        EXPECT_EQ(fake->allocations, fake->frees);
        EXPECT_EQ(fake->creates, fake->destroys);
        EXPECT_EQ(metadata.live, 0);
    }
    EXPECT_TRUE(succeeded);
}

namespace
{
auto PoolInfo(unsigned type = 0) -> vvk::MemoryPoolCreateInfo {
    return { .memory_type = type, .block_policy = { 256, 1024, 0, true } };
}
} // namespace
TEST(MemoryPool, IsolationCompatibilityAndCounters) {
    FakeMemory context;
    fake = &context;
    auto allocator =
        MakeAllocator(alloc::allocator_ref(alloc::GLOBAL), false, { 1024, 1024, 0, false }, true)
            .unwrap_unchecked();
    auto info       = PoolInfo();
    info.min_blocks = 1;
    auto first      = allocator.create_pool(info).unwrap_unchecked();
    auto second     = allocator.create_pool(info).unwrap_unchecked();
    EXPECT_EQ(first.statistics().allocation_count, 0u);
    EXPECT_EQ(first.statistics().block_bytes, 256u);
    auto a = first.create_buffer(BufferInfo()).unwrap_unchecked();
    auto b = second.create_buffer(BufferInfo()).unwrap_unchecked();
    auto c = allocator.create_buffer(BufferInfo(), vvk::MemoryRequest::Upload()).unwrap_unchecked();
    EXPECT_NE(a.allocation().info().memory, b.allocation().info().memory);
    EXPECT_NE(a.allocation().info().memory, c.allocation().info().memory);
    EXPECT_EQ(a.allocation().info().selection.placement_reason,
              vvk::MemoryPlacementReason::FixedPool);
    EXPECT_FALSE(a.allocation().info().selection.type_fallback);
    EXPECT_EQ(first.statistics().requested_bytes, 73u);
    EXPECT_EQ(first.statistics().occupied_bytes, 128u);
    EXPECT_EQ(allocator.budget().heaps[0].block_count, 3u);
    EXPECT_EQ(allocator.budget().heaps[0].allocation_count, 3u);
    EXPECT_EQ(first.create_image(ImageInfo()).unwrap_err_unchecked().kind,
              vvk::MemoryErrorKind::PoolIncompatible);
    auto address = BufferInfo();
    address.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    EXPECT_EQ(first.create_buffer(address).unwrap_err_unchecked().kind,
              vvk::MemoryErrorKind::PoolIncompatible);
    info.allocation_flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    auto address_pool     = allocator.create_pool(info).unwrap_unchecked();
    EXPECT_EQ(address_pool.create_buffer(BufferInfo()).unwrap_err_unchecked().kind,
              vvk::MemoryErrorKind::PoolIncompatible);
    auto addressed = address_pool.create_buffer(address).unwrap_unchecked();
    EXPECT_TRUE(addressed.device_address().is_ok());
    auto incompatible =
        first.create_buffer(BufferInfo(), { .required = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT });
    EXPECT_EQ(incompatible.unwrap_err_unchecked().kind, vvk::MemoryErrorKind::NoMemoryType);
    fake->requirement_types = 2;
    EXPECT_EQ(first.create_buffer(BufferInfo()).unwrap_err_unchecked().kind,
              vvk::MemoryErrorKind::NoMemoryType);
    fake->requirement_types = 3;
    auto soft =
        first.create_buffer(BufferInfo(), { .preferred = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT });
    EXPECT_TRUE(soft.is_ok());
    EXPECT_EQ(first.statistics().allocation_count, 2u);
    EXPECT_EQ(fake->queries, fake->creates);
}
TEST(MemoryPool, ExistingOnlyAndDedicatedConflicts) {
    FakeMemory context;
    fake            = &context;
    auto allocator  = MakeAllocator().unwrap_unchecked();
    auto info       = PoolInfo(1);
    info.min_blocks = 1;
    info.max_blocks = 1;
    auto pool       = allocator.create_pool(info).unwrap_unchecked();
    auto before     = fake->attempt_count;
    auto a          = pool.create_buffer(BufferInfo(256), { .existing_blocks_only = true });
    ASSERT_TRUE(a.is_ok());
    EXPECT_EQ(pool.create_buffer(BufferInfo(), { .existing_blocks_only = true })
                  .unwrap_err_unchecked()
                  .kind,
              vvk::MemoryErrorKind::ExistingBlocksOnly);
    EXPECT_EQ(pool.create_buffer(BufferInfo(), { .dedicated = true }).unwrap_err_unchecked().kind,
              vvk::MemoryErrorKind::DedicatedConflict);
    fake->dedicated = true;
    EXPECT_EQ(pool.create_buffer(BufferInfo()).unwrap_err_unchecked().kind,
              vvk::MemoryErrorKind::DedicatedConflict);
    EXPECT_EQ(allocator.create_buffer(BufferInfo(), { .existing_blocks_only = true })
                  .unwrap_err_unchecked()
                  .kind,
              vvk::MemoryErrorKind::DedicatedConflict);
    fake->dedicated = false;
    EXPECT_EQ(fake->attempt_count, before);
    a.unwrap_unchecked().reset();
    fake->prefer_dedicated = true;
    auto shared            = pool.create_buffer(BufferInfo(200));
    ASSERT_TRUE(shared.is_ok());
    EXPECT_FALSE(shared.unwrap_unchecked().allocation().info().dedicated);
    EXPECT_EQ(fake->attempt_count, before);
    auto default_buffer =
        allocator.create_buffer(BufferInfo(), vvk::MemoryRequest::Upload()).unwrap_unchecked();
    default_buffer.reset();
    // Existing-only visits less-preferred types, without taking the dedicated preference.
    fake->prefer_dedicated = false;
    auto host =
        allocator.create_buffer(BufferInfo(), vvk::MemoryRequest::Upload()).unwrap_unchecked();
    auto count = fake->attempt_count;
    auto reuse = allocator.create_buffer(BufferInfo(), { .existing_blocks_only = true });
    ASSERT_TRUE(reuse.is_ok());
    EXPECT_EQ(reuse.unwrap_unchecked().allocation().info().memory_type, 0u);
    EXPECT_EQ(fake->attempt_count, count);
}
TEST(MemoryPool, LimitsGrowthAndTrim) {
    FakeMemory context;
    fake            = &context;
    auto allocator  = MakeAllocator().unwrap_unchecked();
    auto info       = PoolInfo(1);
    info.min_blocks = 1;
    info.max_blocks = 2;
    info.max_bytes  = 640;
    auto pool       = allocator.create_pool(info).unwrap_unchecked();
    auto a          = pool.create_buffer(BufferInfo(256)).unwrap_unchecked();
    auto b          = pool.create_buffer(BufferInfo(384)).unwrap_unchecked();
    EXPECT_EQ(pool.statistics().block_bytes, 640u);
    EXPECT_EQ(fake->attempts[1].size, 384u);
    auto attempts = fake->attempt_count;
    EXPECT_EQ(pool.create_buffer(BufferInfo()).unwrap_err_unchecked().kind,
              vvk::MemoryErrorKind::PoolCapacity);
    EXPECT_EQ(fake->attempt_count, attempts);
    allocator.trim();
    EXPECT_EQ(pool.statistics().block_count, 2u);
    a.reset();
    pool.trim();
    EXPECT_EQ(pool.statistics().block_count, 1u);
    EXPECT_EQ(pool.statistics().block_bytes, 384u);
    b.reset();
    allocator.trim();
    EXPECT_EQ(pool.statistics().block_count, 1u);
    EXPECT_EQ(pool.statistics().allocation_count, 0u);
    EXPECT_EQ(pool.statistics().occupied_bytes, 0u);
    EXPECT_EQ(pool.statistics().requested_bytes, 0u);
    EXPECT_EQ(pool.create_buffer(BufferInfo(1025)).unwrap_err_unchecked().kind,
              vvk::MemoryErrorKind::PoolCapacity);
    auto empty_info = PoolInfo(1);
    auto empty      = allocator.create_pool(empty_info).unwrap_unchecked();
    auto c          = empty.create_buffer(BufferInfo()).unwrap_unchecked();
    EXPECT_EQ(fake->attempts[fake->attempt_count - 1].size, 256u);
    c.reset();
    empty.trim();
    EXPECT_EQ(empty.statistics().block_count, 0u);
}
TEST(MemoryPool, InvalidConfigurationAndPreallocationLimits) {
    FakeMemory context;
    fake           = &context;
    auto allocator = MakeAllocator().unwrap_unchecked();
    EXPECT_TRUE(allocator.create_pool({}).is_err());
    for (unsigned mode = 0; mode < 8; ++mode) {
        auto info = PoolInfo();
        if (mode == 0) info.resource_class = static_cast<vvk::MemoryClass>(9);
        if (mode == 1) info.block_policy.initial_size = 0;
        if (mode == 2) {
            info.min_blocks = 2;
            info.max_blocks = 1;
        }
        if (mode == 3) {
            info.min_blocks = 2;
            info.max_bytes  = 511;
        }
        if (mode == 4) {
            info.min_blocks   = 2;
            info.block_policy = { ~VkDeviceSize(0), ~VkDeviceSize(0), 0, false };
        }
        if (mode == 5) info.allocation_flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        if (mode == 6) info.allocation_flags = VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT;
        if (mode == 7) info.block_policy.shrink_attempts = 64;
        EXPECT_TRUE(allocator.create_pool(info).is_err());
    }
    EXPECT_EQ(fake->attempt_count, 0u);
    fake->max_allocation_size = 128;
    auto info                 = PoolInfo();
    info.min_blocks           = 1;
    auto limited_allocator    = MakeAllocator().unwrap_unchecked();
    EXPECT_TRUE(limited_allocator.create_pool(info).is_err());
    EXPECT_EQ(fake->attempt_count, 0u);
}
TEST(MemoryPool, PreallocationRollbackAndGlobalLimits) {
    for (unsigned fail_at = 0; fail_at < 3; ++fail_at) {
        FakeMemory context;
        fake                  = &context;
        fake->fail_at_attempt = fail_at + 1;
        auto allocator        = MakeAllocator().unwrap_unchecked();
        auto info             = PoolInfo();
        info.min_blocks       = 3;
        auto result           = allocator.create_pool(info);
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.unwrap_err_unchecked().device_allocation_attempts, fail_at + 1);
        EXPECT_EQ(fake->allocations, fake->frees);
        EXPECT_EQ(allocator.budget().heaps[0].block_count, 0u);
    }
    FakeMemory context;
    fake                  = &context;
    fake->max_allocations = 2;
    auto allocator        = MakeAllocator().unwrap_unchecked();
    auto info             = PoolInfo();
    info.min_blocks       = 1;
    auto first            = allocator.create_pool(info).unwrap_unchecked();
    auto second           = allocator.create_pool(info).unwrap_unchecked();
    EXPECT_TRUE(allocator.create_buffer(BufferInfo()).is_err());
    EXPECT_TRUE(allocator.create_pool(info).is_err());
    EXPECT_EQ(fake->attempt_count, 2u);
    EXPECT_EQ(allocator.budget().heaps[0].block_count, 2u);
}
TEST(MemoryPool, BudgetAndExistingReuse) {
    FakeMemory context;
    fake                = &context;
    auto allocator      = MakeAllocator().unwrap_unchecked();
    fake->driver_budget = true;
    fake->driver_usage  = 0;
    fake->budget        = 128;
    auto info           = PoolInfo();
    info.min_blocks     = 1;
    info.within_budget  = true;
    EXPECT_TRUE(allocator.create_pool(info).is_err());
    EXPECT_EQ(fake->attempt_count, 0u);
    fake->budget       = 256;
    auto pool          = allocator.create_pool(info).unwrap_unchecked();
    fake->driver_usage = 512;
    auto a             = pool.create_buffer(BufferInfo(256));
    ASSERT_TRUE(a.is_ok());
    EXPECT_TRUE(pool.create_buffer(BufferInfo()).is_err());
    EXPECT_EQ(fake->attempt_count, 1u);
    EXPECT_EQ(allocator.budget().heaps[0].block_bytes, 256u);
}
TEST(MemoryPool, LeasesKeepOwnerAndStorageAlive) {
    FakeMemory context;
    fake = &context;
    MemoryFailMetadata metadata;
    {
        auto allocator  = MakeAllocator(alloc::allocator_ref(metadata)).unwrap_unchecked();
        auto info       = PoolInfo();
        info.min_blocks = 2;
        auto pool       = allocator.create_pool(info).unwrap_unchecked();
        auto buffer =
            pool.create_buffer(BufferInfo(), { .persistent_mapping = true }).unwrap_unchecked();
        auto memory  = buffer.allocation();
        auto mapping = memory.map().unwrap_unchecked();
        auto clone   = buffer.clone();
        allocator.reset();
        auto more = pool.create_buffer(BufferInfo()).unwrap_unchecked();
        more.reset();
        pool.reset();
        buffer.reset();
        EXPECT_EQ(fake->destroys, 1u);
        EXPECT_EQ(fake->frees, 0u);
        clone.reset();
        EXPECT_EQ(fake->destroys, 2u);
        memory.reset();
        EXPECT_EQ(fake->frees, 0u);
        static_cast<unsigned char*>(mapping.data())[0] = 42;
    }
    EXPECT_EQ(fake->allocations, fake->frees);
    EXPECT_EQ(fake->maps, fake->unmaps);
    EXPECT_EQ(metadata.live, 0);
}
TEST(MemoryPool, MetadataAndResourceFailureRollback) {
    bool created = false, allocated = false;
    for (int fail_at = 0; fail_at < 24; ++fail_at) {
        FakeMemory context;
        fake = &context;
        MemoryFailMetadata metadata;
        {
            auto allocator     = MakeAllocator(alloc::allocator_ref(metadata)).unwrap_unchecked();
            metadata.remaining = fail_at;
            auto info          = PoolInfo();
            info.min_blocks    = 2;
            auto result        = allocator.create_pool(info);
            if (result.is_ok()) {
                created     = true;
                auto pool   = result.unwrap_unchecked();
                auto buffer = pool.create_buffer(BufferInfo());
                if (buffer.is_ok()) allocated = true;
            }
            EXPECT_EQ(allocator.budget().heaps[0].block_count, 0u);
        }
        EXPECT_EQ(fake->allocations, fake->frees);
        EXPECT_EQ(metadata.live, 0);
    }
    EXPECT_TRUE(created);
    EXPECT_TRUE(allocated);
    for (unsigned mode = 0; mode < 5; ++mode) {
        FakeMemory context;
        fake            = &context;
        auto allocator  = MakeAllocator().unwrap_unchecked();
        auto info       = PoolInfo();
        info.min_blocks = 1;
        auto pool       = allocator.create_pool(info).unwrap_unchecked();
        auto kept       = pool.create_buffer(BufferInfo(256)).unwrap_unchecked();
        auto memory     = kept.allocation();
        auto mapping    = memory.map().unwrap_unchecked();
        static_cast<unsigned char*>(mapping.data())[0] = 91;
        auto before                                    = pool.statistics();
        fake->fail_create                              = mode == 0;
        fake->fail_allocate                            = mode == 1;
        fake->fail_bind                                = mode == 2;
        fake->fail_map                                 = mode == 3;
        fake->device_lost                              = mode == 4;
        EXPECT_TRUE(pool.create_buffer(BufferInfo(), { .persistent_mapping = true }).is_err());
        EXPECT_EQ(pool.statistics().block_count, before.block_count);
        EXPECT_EQ(pool.statistics().block_bytes, before.block_bytes);
        EXPECT_EQ(pool.statistics().allocation_count, before.allocation_count);
        EXPECT_EQ(static_cast<unsigned char*>(mapping.data())[0], 91u);
    }
}
TEST(MemoryPool, BoundedGrowthAndTypeIsolation) {
    FakeMemory context;
    fake               = &context;
    auto allocator     = MakeAllocator().unwrap_unchecked();
    auto info          = PoolInfo(1);
    info.max_blocks    = 1;
    auto       limited = allocator.create_pool(info).unwrap_unchecked();
    auto       full    = limited.create_buffer(BufferInfo(256)).unwrap_unchecked();
    const auto before  = fake->attempt_count;
    EXPECT_EQ(limited.create_buffer(BufferInfo()).unwrap_err_unchecked().kind,
              vvk::MemoryErrorKind::PoolCapacity);
    EXPECT_EQ(fake->attempt_count, before);
    info.max_blocks                   = 0;
    info.block_policy.shrink_attempts = 2;
    auto pool                         = allocator.create_pool(info).unwrap_unchecked();
    auto a                            = pool.create_buffer(BufferInfo(256)).unwrap_unchecked();
    fake->max_success_size            = 256;
    auto b                            = pool.create_buffer(BufferInfo(128)).unwrap_unchecked();
    EXPECT_EQ(b.allocation().info().selection.device_allocation_attempts, 2u);
    EXPECT_EQ(pool.statistics().block_bytes, 512u);
    fake->reject_types = 2;
    auto c             = pool.create_buffer(BufferInfo(512));
    ASSERT_TRUE(c.is_err());
    EXPECT_EQ(c.unwrap_err_unchecked().device_allocation_attempts, 1u);
    EXPECT_EQ(fake->attempts[fake->attempt_count - 1].type, 1u);
    EXPECT_FALSE(fake->attempts[fake->attempt_count - 1].dedicated);
    auto optimal_info           = PoolInfo(1);
    optimal_info.resource_class = vvk::MemoryClass::Optimal;
    optimal_info.block_policy   = { 1024, 1024, 0, false };
    fake->reject_types          = 0;
    fake->max_success_size      = 1024;
    auto optimal                = allocator.create_pool(optimal_info).unwrap_unchecked();
    auto image                  = optimal.create_image(ImageInfo());
    ASSERT_TRUE(image.is_ok());
    EXPECT_EQ(optimal.create_buffer(BufferInfo()).unwrap_err_unchecked().kind,
              vvk::MemoryErrorKind::PoolIncompatible);
    auto linear_image_info   = ImageInfo();
    linear_image_info.tiling = VK_IMAGE_TILING_LINEAR;
    auto linear              = allocator.create_pool(PoolInfo(1)).unwrap_unchecked();
    auto linear_image        = linear.create_image(linear_image_info);
    ASSERT_TRUE(linear_image.is_ok());
}
TEST(MemoryPool, AllocationMetadataRollbackPreservesLiveResources) {
    bool succeeded = false;
    for (int fail_at = 0; fail_at < 16; ++fail_at) {
        FakeMemory context;
        fake = &context;
        MemoryFailMetadata metadata;
        {
            auto allocator = MakeAllocator(alloc::allocator_ref(metadata)).unwrap_unchecked();
            auto pool      = allocator.create_pool(PoolInfo()).unwrap_unchecked();
            auto kept      = pool.create_buffer(BufferInfo(256)).unwrap_unchecked();
            auto mapped    = kept.allocation().map().unwrap_unchecked();
            static_cast<unsigned char*>(mapped.data())[0] = 53;
            metadata.remaining                            = fail_at;
            {
                auto result = pool.create_buffer(BufferInfo(), { .persistent_mapping = true });
                if (result.is_ok())
                    succeeded = true;
                else {
                    EXPECT_EQ(result.unwrap_err_unchecked().kind, vvk::MemoryErrorKind::HostMemory);
                    EXPECT_EQ(pool.statistics().block_count, 1u);
                    EXPECT_EQ(pool.statistics().block_bytes, 256u);
                }
            }
            EXPECT_EQ(pool.statistics().allocation_count, 1u);
            EXPECT_EQ(pool.statistics().requested_bytes, 256u);
            EXPECT_EQ(static_cast<unsigned char*>(mapped.data())[0], 53u);
        }
        EXPECT_EQ(metadata.live, 0);
        EXPECT_EQ(fake->allocations, fake->frees);
        EXPECT_EQ(fake->creates, fake->destroys);
    }
    EXPECT_TRUE(succeeded);
}
TEST(MemoryPool, ReservedByteOverflowIsRejected) {
    FakeMemory context;
    fake            = &context;
    fake->heap_size = fake->max_allocation_size = fake->max_success_size = ~VkDeviceSize(0);
    auto allocator         = MakeAllocator().unwrap_unchecked();
    auto info              = PoolInfo(1);
    info.block_policy      = { VkDeviceSize(1) << 63, ~VkDeviceSize(0), 0, false };
    auto pool              = allocator.create_pool(info).unwrap_unchecked();
    fake->requirement_size = (VkDeviceSize(1) << 63) - 1;
    auto large             = pool.create_buffer(BufferInfo());
    ASSERT_TRUE(large.is_ok());
    fake->requirement_size = 0;
    auto failed            = pool.create_buffer(BufferInfo());
    ASSERT_TRUE(failed.is_err());
    EXPECT_EQ(failed.unwrap_err_unchecked().kind, vvk::MemoryErrorKind::InvalidRequest);
    EXPECT_EQ(fake->attempt_count, 1u);
    EXPECT_EQ(pool.statistics().block_bytes, VkDeviceSize(1) << 63);
}
