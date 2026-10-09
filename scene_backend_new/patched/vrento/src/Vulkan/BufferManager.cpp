module;

#include <rstd/macro.hpp>
#include "vvk/macros.hpp"

module vrento.vulkan;

import rstd;
import rstd.log;

using namespace rstd::prelude;
using rstd::cmp::max;
using rstd::cmp::min;
using rstd::sync::Arc;

namespace vrento::vulkan
{

namespace
{

constexpr VkDeviceSize kBufferPageSize  = 2 * 1024 * 1024;
constexpr VkDeviceSize kUploadBlockSize = 2 * 1024 * 1024;
constexpr VkDeviceSize kLargeAlignment  = 64 * 1024;

VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment <= 1) return value;
    return ((value + alignment - 1) / alignment) * alignment;
}

VkBufferUsageFlags DestinationUsage() {
    return VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
           VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
}

VkAccessFlags DestinationAccess(BufferUploadClass usage) {
    switch (usage) {
    case BufferUploadClass::Vertex: return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    case BufferUploadClass::Index: return VK_ACCESS_INDEX_READ_BIT;
    case BufferUploadClass::Uniform: return VK_ACCESS_UNIFORM_READ_BIT;
    case BufferUploadClass::Storage: return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    case BufferUploadClass::Transfer:
        return VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    return VK_ACCESS_MEMORY_READ_BIT;
}

VkPipelineStageFlags DestinationStages(BufferUploadClass usage) {
    switch (usage) {
    case BufferUploadClass::Vertex:
    case BufferUploadClass::Index: return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    case BufferUploadClass::Uniform:
    case BufferUploadClass::Storage:
        return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    case BufferUploadClass::Transfer: return VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    return VK_PIPELINE_STAGE_TRANSFER_BIT;
}

class BufferPage {
public:
    static Option<Arc<BufferPage>> Create(const Device& device, VkDeviceSize size) {
        auto page = Arc<BufferPage>::make(device, size);
        if (! page->Initialize()) return None();
        return Some(rstd::move(page));
    }

    bool TryAllocate(VkDeviceSize size, VkDeviceSize alignment, alloc::RangeId& allocation,
                     VkDeviceSize& offset) {
        auto result = m_ranges.allocate(size, alignment);
        if (result.is_err()) return false;
        const auto range = result.unwrap_unchecked();
        allocation       = range.id;
        offset           = range.offset;
        return true;
    }

    void Release(alloc::RangeId allocation) { (void)m_ranges.deallocate(allocation); }

    VkBuffer     handle() const noexcept { return m_buffer.handle.handle(); }
    VkDeviceSize size() const noexcept { return m_size; }
    bool         empty() const noexcept { return m_ranges.counters().allocation_count == 0; }

    BufferPage(const Device& device, VkDeviceSize size)
        : m_device(device), m_size(size), m_ranges(size) {}

private:
    bool Initialize() {
        VkBufferCreateInfo buffer_info {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size  = m_size,
            .usage = DestinationUsage(),
        };
        auto buffer = m_device.memory_allocator().create_buffer(buffer_info);
        if (buffer.is_err()) {
            const auto error = buffer.unwrap_err_unchecked();
            rstd_error("buffer page allocation failed: kind={}, vk={}",
                       static_cast<int>(error.kind),
                       static_cast<int>(error.api_result));
            return false;
        }
        m_buffer.req_size = m_size;
        m_buffer.handle   = buffer.unwrap_unchecked();
        return true;
    }

    const Device&             m_device;
    VkDeviceSize              m_size { 0 };
    AllocatedBufferParameters m_buffer;
    alloc::RangeAllocator<>   m_ranges;
};

class UploadBlock {
public:
    static Option<Arc<UploadBlock>> Create(const Device& device, VkDeviceSize size) {
        auto block = Arc<UploadBlock>::make(device, size);
        if (! block->Initialize()) return None();
        return Some(rstd::move(block));
    }

    void Reset() {
        m_cursor        = 0;
        m_touched_begin = m_size;
        m_touched_end   = 0;
    }

    bool TryAllocate(VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize& offset) {
        auto aligned = AlignUp(m_cursor, alignment);
        if (aligned > m_size || size > m_size - aligned) return false;
        offset   = aligned;
        m_cursor = aligned + size;
        return true;
    }

    void Write(VkDeviceSize offset, slice<rstd::uint8_t> data) {
        auto* bytes = static_cast<rstd::uint8_t*>(m_mapping->data());
        rstd::mem::memcpy(bytes + offset, data.as_raw_ptr(), data.len());
        m_touched_begin = min(m_touched_begin, offset);
        m_touched_end =
            max(m_touched_end, offset + static_cast<VkDeviceSize>(data.len().to_primitive()));
    }

    bool Flush() const {
        if (m_touched_end <= m_touched_begin) return true;
        auto flushed =
            m_buffer.handle.allocation().flush(m_touched_begin, m_touched_end - m_touched_begin);
        if (flushed.is_err()) {
            const auto error = flushed.unwrap_err_unchecked();
            rstd_error("upload flush failed: kind={}, vk={}",
                       static_cast<int>(error.kind),
                       static_cast<int>(error.api_result));
            return false;
        }
        return true;
    }

    VkBuffer     handle() const noexcept { return m_buffer.handle.handle(); }
    VkDeviceSize size() const noexcept { return m_size; }

    UploadBlock(const Device& device, VkDeviceSize size): m_device(device), m_size(size) {}

private:
    bool Initialize() {
        if (! CreateStagingBuffer(m_device.memory_allocator(), m_size, m_buffer)) return false;
        auto mapped = m_buffer.handle.allocation().map();
        if (mapped.is_err()) {
            const auto error = mapped.unwrap_err_unchecked();
            rstd_error("upload mapping failed: kind={}, vk={}",
                       static_cast<int>(error.kind),
                       static_cast<int>(error.api_result));
            return false;
        }
        m_mapping = Some(mapped.unwrap_unchecked());
        Reset();
        return true;
    }

    const Device&              m_device;
    VkDeviceSize               m_size { 0 };
    AllocatedBufferParameters  m_buffer;
    Option<vvk::MemoryMapping> m_mapping;
    VkDeviceSize               m_cursor { 0 };
    VkDeviceSize               m_touched_begin { 0 };
    VkDeviceSize               m_touched_end { 0 };
};

struct BufferCopyOperation {
    BufferAllocation  destination_lease;
    Arc<UploadBlock>  source;
    VkBuffer          destination { VK_NULL_HANDLE };
    VkDeviceSize      source_offset { 0 };
    VkDeviceSize      destination_offset { 0 };
    VkDeviceSize      size { 0 };
    BufferUploadClass usage { BufferUploadClass::Vertex };
};

struct ImageMipCopyOperation {
    Arc<UploadBlock> source;
    VkDeviceSize     source_offset { 0 };
    VkExtent3D       extent {};
    rstd::uint32_t   mip_level { 0 };
};

struct ImageCopyOperation {
    Arc<TextureAllocation>     destination_lease;
    ImageParameters            destination;
    Vec<ImageMipCopyOperation> mipmaps;
};

struct ImageClearOperation {
    Arc<TextureAllocation> destination_lease;
    ImageParameters        destination;
};

} // namespace

struct BufferAllocation::State {
    Option<Arc<BufferPage>> page;
    alloc::RangeId          allocation {};
    VkDeviceSize            offset { 0 };
    VkDeviceSize            size { 0 };
    BufferUploadClass       usage { BufferUploadClass::Vertex };

    ~State() {
        if (page) (*page)->Release(allocation);
    }
};

struct RecordedBufferUploads::State {
    u64                      serial { 0 };
    bool                     recorded { false };
    Vec<BufferCopyOperation> copies;
    Vec<Arc<UploadBlock>>    blocks;
    Vec<BufferUploadTicket>  tickets;
};

struct RecordedImageUploads::State {
    u64                      serial { 0 };
    bool                     recorded { false };
    Vec<ImageCopyOperation>  copies;
    Vec<ImageClearOperation> clears;
    Vec<Arc<UploadBlock>>    blocks;
    Vec<ImageUploadTicket>   tickets;
};

struct BufferManager::Impl {
    explicit Impl(const Device& value): device(value) {}

    const Device&                             device;
    bool                                      initialized { false };
    u64                                       next_ticket { 0 };
    u64                                       next_batch { 0 };
    Vec<Arc<BufferPage>>                      pages;
    Vec<Arc<UploadBlock>>                     upload_blocks;
    Option<Arc<RecordedBufferUploads::State>> pending;
};

struct ImageUploadManager::Impl {
    explicit Impl(const Device& value): device(value) {}

    const Device&                            device;
    bool                                     initialized { false };
    u64                                      next_ticket { 0 };
    u64                                      next_batch { 0 };
    Vec<Arc<UploadBlock>>                    upload_blocks;
    Option<Arc<RecordedImageUploads::State>> pending;
};

BufferAllocation::BufferAllocation()             = default;
RecordedBufferUploads::RecordedBufferUploads()   = default;
RecordedImageUploads::RecordedImageUploads()     = default;
BufferUploadBatchLease::BufferUploadBatchLease() = default;
ImageUploadBatchLease::ImageUploadBatchLease()   = default;

BufferAllocation::BufferAllocation(Arc<State> state): m_state(Some(rstd::move(state))) {}
BufferAllocation::~BufferAllocation() = default;
BufferAllocation::BufferAllocation(BufferAllocation&& other) noexcept
    : m_state(other.m_state.take()) {}
BufferAllocation& BufferAllocation::operator=(BufferAllocation&& other) noexcept {
    if (this != &other) m_state = other.m_state.take();
    return *this;
}

BufferAllocation::operator bool() const noexcept {
    return m_state && (*m_state)->page && (*m_state)->size > 0;
}

VkBuffer BufferAllocation::buffer() const noexcept {
    return m_state && (*m_state)->page ? (*(*m_state)->page)->handle() : VK_NULL_HANDLE;
}

VkDeviceSize BufferAllocation::offset() const noexcept { return m_state ? (*m_state)->offset : 0; }

VkDeviceSize BufferAllocation::size() const noexcept { return m_state ? (*m_state)->size : 0; }

RecordedBufferUploads::RecordedBufferUploads(BufferManager* owner, Arc<State> state)
    : m_owner(owner), m_state(Some(rstd::move(state))) {}

RecordedBufferUploads::~RecordedBufferUploads() { Reset(); }

RecordedBufferUploads::RecordedBufferUploads(RecordedBufferUploads&& other) noexcept
    : m_owner(other.m_owner), m_state(other.m_state.take()) {
    other.m_owner = nullptr;
}

RecordedBufferUploads& RecordedBufferUploads::operator=(RecordedBufferUploads&& other) noexcept {
    if (this == &other) return *this;
    Reset();
    m_owner       = other.m_owner;
    m_state       = other.m_state.take();
    other.m_owner = nullptr;
    return *this;
}

bool RecordedBufferUploads::Valid() const noexcept { return m_owner != nullptr && m_state; }

void RecordedBufferUploads::Reset() {
    if (m_owner && m_state) m_owner->CancelRecordedUploads(*m_state);
    m_owner = nullptr;
    m_state = None();
}

BufferUploadBatchLease::BufferUploadBatchLease(Arc<RecordedBufferUploads::State> state)
    : m_state(Some(rstd::move(state))) {}

BufferUploadBatchLease::~BufferUploadBatchLease() = default;
BufferUploadBatchLease::BufferUploadBatchLease(BufferUploadBatchLease&& other) noexcept
    : m_state(other.m_state.take()) {}

BufferUploadBatchLease& BufferUploadBatchLease::operator=(BufferUploadBatchLease&& other) noexcept {
    if (this != &other) m_state = other.m_state.take();
    return *this;
}

bool BufferUploadBatchLease::Valid() const noexcept { return static_cast<bool>(m_state); }

slice<BufferUploadTicket> BufferUploadBatchLease::Tickets() const noexcept {
    if (! m_state) return slice<BufferUploadTicket>();
    return (*m_state)->tickets.as_slice();
}

BufferManager::BufferManager(const Device& device): m_impl(Box<Impl>::make(device)) {}
BufferManager::~BufferManager() { destroy(); }

bool BufferManager::init() {
    m_impl->initialized = true;
    return true;
}

void BufferManager::destroy() {
    if (! m_impl->initialized) return;
    m_impl->pending = None();
    m_impl->upload_blocks.clear();
    m_impl->pages.clear();
    m_impl->initialized = false;
}

Option<BufferAllocation> BufferManager::Allocate(const BufferAllocationRequest& request) {
    if (! m_impl->initialized || request.size == 0) return None();

    VkDeviceSize alignment = max<VkDeviceSize>(request.alignment, 4);
    if (request.usage == BufferUploadClass::Uniform) {
        alignment = max(alignment, m_impl->device.limits().minUniformBufferOffsetAlignment);
    } else if (request.usage == BufferUploadClass::Storage) {
        alignment = max(alignment, m_impl->device.limits().minStorageBufferOffsetAlignment);
    }

    alloc::RangeId          allocation {};
    VkDeviceSize            offset { 0 };
    Option<Arc<BufferPage>> page;
    for (const auto& candidate : m_impl->pages) {
        if (candidate->TryAllocate(request.size, alignment, allocation, offset)) {
            page = Some(candidate.clone());
            break;
        }
    }
    if (! page) {
        const auto page_size = request.size > kBufferPageSize
                                   ? AlignUp(request.size, kLargeAlignment)
                                   : kBufferPageSize;
        page                 = BufferPage::Create(m_impl->device, page_size);
        if (! page || ! (*page)->TryAllocate(request.size, alignment, allocation, offset)) {
            return None();
        }
        m_impl->pages.push(page->clone());
        rstd_info(
            "new destination buffer page, size: {}, pages: {}", page_size, m_impl->pages.len());
    }

    auto state        = Arc<BufferAllocation::State>::make();
    state->page       = rstd::move(page);
    state->allocation = allocation;
    state->offset     = offset;
    state->size       = request.size;
    state->usage      = request.usage;
    return Some(BufferAllocation(rstd::move(state)));
}

Option<BufferUploadTicket> BufferManager::QueueWrite(BufferAllocation&    allocation,
                                                     slice<rstd::uint8_t> data,
                                                     VkDeviceSize         destination_offset) {
    if (! m_impl->initialized || ! allocation || destination_offset > allocation.size() ||
        static_cast<VkDeviceSize>(data.len().to_primitive()) >
            allocation.size() - destination_offset) {
        return None();
    }
    if (data.is_empty()) return Some(BufferUploadTicket {});
    if (m_impl->pending && (*m_impl->pending)->recorded) {
        rstd_error("queue buffer write while the pending upload batch is recorded");
        return None();
    }

    if (! m_impl->pending) {
        ++m_impl->next_batch;
        if (m_impl->next_batch == u64()) ++m_impl->next_batch;
        m_impl->pending            = Some(Arc<RecordedBufferUploads::State>::make());
        (*m_impl->pending)->serial = m_impl->next_batch;
    }

    VkDeviceSize             upload_offset { 0 };
    Option<Arc<UploadBlock>> block;
    for (const auto& candidate : (*m_impl->pending)->blocks) {
        if (candidate->TryAllocate(
                static_cast<VkDeviceSize>(data.len().to_primitive()), 4, upload_offset)) {
            block = Some(candidate.clone());
            break;
        }
    }
    if (! block) {
        const auto required = static_cast<VkDeviceSize>(data.len().to_primitive());
        for (const auto& candidate : m_impl->upload_blocks) {
            if (candidate.strong_count() != usize(1) || candidate->size() < required) continue;
            candidate->Reset();
            if (candidate->TryAllocate(required, 4, upload_offset)) {
                block = Some(candidate.clone());
                break;
            }
        }
        if (! block) {
            const auto block_size =
                required > kUploadBlockSize ? AlignUp(required, kLargeAlignment) : kUploadBlockSize;
            block = UploadBlock::Create(m_impl->device, block_size);
            if (! block || ! (*block)->TryAllocate(required, 4, upload_offset)) return None();
            m_impl->upload_blocks.push(block->clone());
            rstd_info("new upload buffer block, size: {}, blocks: {}",
                      block_size,
                      m_impl->upload_blocks.len());
        }
        (*m_impl->pending)->blocks.push(block->clone());
    }

    (*block)->Write(upload_offset, data);
    ++m_impl->next_ticket;
    if (m_impl->next_ticket == u64()) ++m_impl->next_ticket;
    BufferUploadTicket ticket { .value = m_impl->next_ticket };
    (*m_impl->pending)
        ->copies.push(BufferCopyOperation {
            .destination_lease  = BufferAllocation(allocation.m_state->clone()),
            .source             = block.take().unwrap(),
            .destination        = allocation.buffer(),
            .source_offset      = upload_offset,
            .destination_offset = allocation.offset() + destination_offset,
            .size               = static_cast<VkDeviceSize>(data.len().to_primitive()),
            .usage              = (*allocation.m_state)->usage,
        });
    (*m_impl->pending)->tickets.push(decltype(ticket)(ticket));
    return Some(ticket);
}

bool BufferManager::HasPendingUploads() const noexcept {
    return m_impl->pending && ! (*m_impl->pending)->copies.is_empty();
}

bool BufferManager::RecordPendingUploads(vvk::CommandBuffer& cmd, RecordedBufferUploads& recorded) {
    recorded.Reset();
    if (! HasPendingUploads()) return true;
    if ((*m_impl->pending)->recorded) {
        rstd_error("buffer upload batch is already recorded");
        return false;
    }
    for (const auto& block : (*m_impl->pending)->blocks) {
        if (! block->Flush()) return false;
    }
    for (const auto& copy : (*m_impl->pending)->copies) {
        cmd.CopyBuffer(copy.source->handle(),
                       copy.destination,
                       VkBufferCopy {
                           .srcOffset = copy.source_offset,
                           .dstOffset = copy.destination_offset,
                           .size      = copy.size,
                       });
    }
    for (const auto& copy : (*m_impl->pending)->copies) {
        VkBufferMemoryBarrier barrier {
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = DestinationAccess(copy.usage),
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = copy.destination,
            .offset              = copy.destination_offset,
            .size                = copy.size,
        };
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                            DestinationStages(copy.usage),
                            VK_DEPENDENCY_BY_REGION_BIT,
                            barrier);
    }
    (*m_impl->pending)->recorded = true;
    recorded                     = RecordedBufferUploads(this, m_impl->pending->clone());
    return true;
}

Option<BufferUploadBatchLease>
BufferManager::CommitRecordedUploads(RecordedBufferUploads&& recorded) {
    if (! recorded.Valid() || recorded.m_owner != this || ! m_impl->pending ||
        ! Arc<RecordedBufferUploads::State>::ptr_eq(*recorded.m_state, *m_impl->pending) ||
        ! (*recorded.m_state)->recorded) {
        return None();
    }
    auto state       = recorded.m_state.take().unwrap();
    recorded.m_owner = nullptr;
    state->recorded  = false;
    m_impl->pending  = None();
    return Some(BufferUploadBatchLease(rstd::move(state)));
}

void BufferManager::CancelRecordedUploads(const Arc<RecordedBufferUploads::State>& state) {
    if (m_impl->pending && Arc<RecordedBufferUploads::State>::ptr_eq(state, *m_impl->pending))
        state->recorded = false;
}

void BufferManager::Trim() {
    bool kept_empty_page = false;
    m_impl->pages.retain([&](const auto& page) {
        if (! page->empty()) return true;
        if (! kept_empty_page) {
            kept_empty_page = true;
            return true;
        }
        return page.strong_count() != usize(1);
    });

    bool kept_upload_block = false;
    m_impl->upload_blocks.retain([&](const auto& block) {
        if (block.strong_count() != usize(1)) return true;
        if (! kept_upload_block) {
            kept_upload_block = true;
            return true;
        }
        return false;
    });
}

RecordedImageUploads::RecordedImageUploads(ImageUploadManager* owner, Arc<State> state)
    : m_owner(owner), m_state(Some(rstd::move(state))) {}

RecordedImageUploads::~RecordedImageUploads() { Reset(); }

RecordedImageUploads::RecordedImageUploads(RecordedImageUploads&& other) noexcept
    : m_owner(other.m_owner), m_state(other.m_state.take()) {
    other.m_owner = nullptr;
}

RecordedImageUploads& RecordedImageUploads::operator=(RecordedImageUploads&& other) noexcept {
    if (this == &other) return *this;
    Reset();
    m_owner       = other.m_owner;
    m_state       = other.m_state.take();
    other.m_owner = nullptr;
    return *this;
}

bool RecordedImageUploads::Valid() const noexcept { return m_owner != nullptr && m_state; }

void RecordedImageUploads::Reset() {
    if (m_owner && m_state) m_owner->CancelRecordedUploads(*m_state);
    m_owner = nullptr;
    m_state = None();
}

ImageUploadBatchLease::ImageUploadBatchLease(Arc<RecordedImageUploads::State> state)
    : m_state(Some(rstd::move(state))) {}

ImageUploadBatchLease::~ImageUploadBatchLease() = default;
ImageUploadBatchLease::ImageUploadBatchLease(ImageUploadBatchLease&& other) noexcept
    : m_state(other.m_state.take()) {}

ImageUploadBatchLease& ImageUploadBatchLease::operator=(ImageUploadBatchLease&& other) noexcept {
    if (this != &other) m_state = other.m_state.take();
    return *this;
}

bool ImageUploadBatchLease::Valid() const noexcept { return static_cast<bool>(m_state); }

slice<ImageUploadTicket> ImageUploadBatchLease::Tickets() const noexcept {
    if (! m_state) return {};
    return (*m_state)->tickets.as_slice();
}

ImageUploadManager::ImageUploadManager(const Device& device): m_impl(Box<Impl>::make(device)) {}
ImageUploadManager::~ImageUploadManager() { destroy(); }

bool ImageUploadManager::init() {
    m_impl->initialized = true;
    return true;
}

void ImageUploadManager::destroy() {
    if (! m_impl->initialized) return;
    m_impl->pending = None();
    m_impl->upload_blocks.clear();
    m_impl->initialized = false;
}

Option<ImageUploadTicket> ImageUploadManager::QueueWrite(Arc<TextureAllocation> allocation,
                                                         const Image&           image) {
    if (! m_impl->initialized || image.header.kind == ImageKind::Video) return None();
    auto destinations = allocation->View();
    if (destinations.slots.len() != image.slots.len() || destinations.slots.is_empty()) {
        return None();
    }
    if (m_impl->pending && (*m_impl->pending)->recorded) {
        rstd_error("queue image write while the pending upload batch is recorded");
        return None();
    }
    if (! m_impl->pending) {
        ++m_impl->next_batch;
        if (m_impl->next_batch == u64()) ++m_impl->next_batch;
        m_impl->pending            = Some(Arc<RecordedImageUploads::State>::make());
        (*m_impl->pending)->serial = m_impl->next_batch;
    }

    Vec<ImageCopyOperation> operations;
    operations.reserve(image.slots.len());
    for (usize slot_index {}; slot_index < image.slots.len(); ++slot_index) {
        const auto& source_slot = image.slots[slot_index];
        const auto& destination = destinations.slots[slot_index];
        if (! source_slot || source_slot.mipmaps.len() != usize(destination.mipmap_level))
            return None();

        ImageCopyOperation operation {
            .destination_lease = allocation.clone(),
            .destination       = destination,
        };
        operation.mipmaps.reserve(source_slot.mipmaps.len());
        for (usize mip_index {}; mip_index < source_slot.mipmaps.len(); ++mip_index) {
            const auto& source = source_slot.mipmaps[mip_index];
            const auto  size   = static_cast<VkDeviceSize>(source.size.to_primitive());
            if (size == 0 || ! source.data || source.width <= 0 || source.height <= 0) {
                return None();
            }

            VkDeviceSize             upload_offset { 0 };
            Option<Arc<UploadBlock>> block;
            for (const auto& candidate : (*m_impl->pending)->blocks) {
                if (candidate->TryAllocate(size, 256, upload_offset)) {
                    block = Some(candidate.clone());
                    break;
                }
            }
            if (! block) {
                for (const auto& candidate : m_impl->upload_blocks) {
                    if (candidate.strong_count() != usize(1) || candidate->size() < size) continue;
                    candidate->Reset();
                    if (candidate->TryAllocate(size, 256, upload_offset)) {
                        block = Some(candidate.clone());
                        break;
                    }
                }
                if (! block) {
                    const auto block_size =
                        size > kUploadBlockSize ? AlignUp(size, kLargeAlignment) : kUploadBlockSize;
                    block = UploadBlock::Create(m_impl->device, block_size);
                    if (! block || ! (*block)->TryAllocate(size, 256, upload_offset)) return None();
                    m_impl->upload_blocks.push(block->clone());
                    rstd_info("new image upload buffer block, size: {}, blocks: {}",
                              block_size,
                              m_impl->upload_blocks.len());
                }
                (*m_impl->pending)->blocks.push(block->clone());
            }

            (*block)->Write(upload_offset,
                            slice<rstd::uint8_t>::from_raw_parts(source.data.get(), usize(size)));
            operation.mipmaps.push(ImageMipCopyOperation {
                .source        = block.take().unwrap(),
                .source_offset = upload_offset,
                .extent        = VkExtent3D { static_cast<rstd::uint32_t>(source.width),
                                              static_cast<rstd::uint32_t>(source.height),
                                              1 },
                .mip_level     = static_cast<rstd::uint32_t>(mip_index.to_primitive()),
            });
        }
        operations.push(rstd::move(operation));
    }

    for (auto& operation : operations) {
        (*m_impl->pending)->copies.push(rstd::move(operation));
    }
    ++m_impl->next_ticket;
    if (m_impl->next_ticket == u64()) ++m_impl->next_ticket;
    ImageUploadTicket ticket { .value = m_impl->next_ticket };
    (*m_impl->pending)->tickets.push(decltype(ticket)(ticket));
    return Some(ticket);
}

Option<ImageUploadTicket>
ImageUploadManager::QueueTransparentClear(Arc<TextureAllocation> allocation) {
    if (! m_impl->initialized) return None();
    auto destinations = allocation->View();
    if (destinations.slots.is_empty()) return None();
    if (m_impl->pending && (*m_impl->pending)->recorded) {
        rstd_error("queue image clear while the pending upload batch is recorded");
        return None();
    }
    if (! m_impl->pending) {
        ++m_impl->next_batch;
        if (m_impl->next_batch == u64()) ++m_impl->next_batch;
        m_impl->pending            = Some(Arc<RecordedImageUploads::State>::make());
        (*m_impl->pending)->serial = m_impl->next_batch;
    }
    for (const auto& destination : destinations.slots) {
        (*m_impl->pending)
            ->clears.push(ImageClearOperation {
                .destination_lease = allocation.clone(),
                .destination       = destination,
            });
    }
    ++m_impl->next_ticket;
    if (m_impl->next_ticket == u64()) ++m_impl->next_ticket;
    ImageUploadTicket ticket { .value = m_impl->next_ticket };
    (*m_impl->pending)->tickets.push(decltype(ticket)(ticket));
    return Some(ticket);
}

bool ImageUploadManager::HasPendingUploads() const noexcept {
    return m_impl->pending &&
           (! (*m_impl->pending)->copies.is_empty() || ! (*m_impl->pending)->clears.is_empty());
}

bool ImageUploadManager::RecordPendingUploads(vvk::CommandBuffer&   command,
                                              RecordedImageUploads& recorded) {
    recorded.Reset();
    if (! HasPendingUploads()) return true;
    if ((*m_impl->pending)->recorded) {
        rstd_error("image upload batch is already recorded");
        return false;
    }
    for (const auto& block : (*m_impl->pending)->blocks) {
        if (! block->Flush()) return false;
    }

    for (const auto& clear : (*m_impl->pending)->clears) {
        VkImageSubresourceRange range {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = clear.destination.mipmap_level,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
        VkImageMemoryBarrier to_transfer {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = 0,
            .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image            = clear.destination.handle,
            .subresourceRange = range,
        };
        command.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_DEPENDENCY_BY_REGION_BIT,
                                to_transfer);
        const VkClearColorValue transparent {};
        command.ClearColorImage(
            clear.destination.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &transparent, range);
        VkImageMemoryBarrier to_sampled {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image            = clear.destination.handle,
            .subresourceRange = range,
        };
        command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_DEPENDENCY_BY_REGION_BIT,
                                to_sampled);
    }

    for (const auto& copy : (*m_impl->pending)->copies) {
        VkImageSubresourceRange range {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = static_cast<rstd::uint32_t>(copy.mipmaps.len().to_primitive()),
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
        VkImageMemoryBarrier to_transfer {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = 0,
            .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image            = copy.destination.handle,
            .subresourceRange = range,
        };
        command.PipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_DEPENDENCY_BY_REGION_BIT,
                                to_transfer);

        for (const auto& mip : copy.mipmaps) {
            VkBufferImageCopy region {
                .bufferOffset = mip.source_offset,
                .imageSubresource =
                    VkImageSubresourceLayers {
                        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel       = mip.mip_level,
                        .baseArrayLayer = 0,
                        .layerCount     = 1,
                    },
                .imageExtent = mip.extent,
            };
            command.CopyBufferToImage(mip.source->handle(),
                                      copy.destination.handle,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      region);
        }

        VkImageMemoryBarrier to_sampled {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image            = copy.destination.handle,
            .subresourceRange = range,
        };
        command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_DEPENDENCY_BY_REGION_BIT,
                                to_sampled);
    }

    (*m_impl->pending)->recorded = true;
    recorded                     = RecordedImageUploads(this, m_impl->pending->clone());
    return true;
}

auto ImageUploadManager::CommitRecordedUploads(RecordedImageUploads&& recorded)
    -> Option<ImageUploadBatchLease> {
    if (! recorded.Valid() || recorded.m_owner != this || ! m_impl->pending ||
        ! Arc<RecordedImageUploads::State>::ptr_eq(*recorded.m_state, *m_impl->pending) ||
        ! (*recorded.m_state)->recorded) {
        return None();
    }
    auto state       = recorded.m_state.take().unwrap();
    recorded.m_owner = nullptr;
    state->recorded  = false;
    m_impl->pending  = None();
    return Some(ImageUploadBatchLease(rstd::move(state)));
}

void ImageUploadManager::CancelRecordedUploads(const Arc<RecordedImageUploads::State>& state) {
    if (m_impl->pending && Arc<RecordedImageUploads::State>::ptr_eq(state, *m_impl->pending))
        state->recorded = false;
}

void ImageUploadManager::DiscardPendingUploads() { m_impl->pending = None(); }

void ImageUploadManager::Trim() {
    bool kept_upload_block = false;
    m_impl->upload_blocks.retain([&](const auto& block) {
        if (block.strong_count() != usize(1)) return true;
        if (! kept_upload_block) {
            kept_upload_block = true;
            return true;
        }
        return false;
    });
}

auto ImagePrepareContext::CreateImportedTexture(ref<Image>                            image,
                                                Option<Arc<rstd::dyn<VideoPlayback>>> playback)
    -> Option<PreparedImageAllocation> {
    auto allocation = m_textures.AllocateImportedTexture(*image, rstd::move(playback));
    if (allocation.is_none()) return None();
    if (image->header.kind == ImageKind::Video) {
        return Some(PreparedImageAllocation {
            .allocation = rstd::move(*allocation),
        });
    }
    auto ticket = m_uploads.QueueWrite(allocation->clone(), *image);
    if (ticket.is_none()) return None();
    return Some(PreparedImageAllocation {
        .allocation = rstd::move(*allocation),
        .upload     = Some(*ticket),
    });
}

auto ImagePrepareContext::AllocateTexture(TextureKey key) -> Option<Arc<TextureAllocation>> {
    return m_textures.AllocateTexture(rstd::move(key));
}

auto ImagePrepareContext::AllocateTransparentTexture(TextureKey key)
    -> Option<PreparedImageAllocation> {
    auto allocation = m_textures.AllocateTexture(rstd::move(key));
    if (allocation.is_none()) return None();
    auto ticket = m_uploads.QueueTransparentClear(allocation->clone());
    if (ticket.is_none()) return None();
    return Some(PreparedImageAllocation {
        .allocation = rstd::move(*allocation),
        .upload     = Some(*ticket),
    });
}

} // namespace vrento::vulkan
