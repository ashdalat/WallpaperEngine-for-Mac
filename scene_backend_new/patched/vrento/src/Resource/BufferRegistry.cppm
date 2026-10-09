module;

#include <cstdint>

export module vrento.resource_registry:buffer_registry;
import rstd;
import vrento.resource;
import vrento.vulkan;

using rstd::collections::HashMap;
using rstd::sync::Arc;

export namespace vrento::resource_registry
{

using namespace rstd::prelude;

struct BufferEntry {
    resource::BufferHandle  handle;
    resource::BufferRequest request;
    u64                     definition_version { 1 };
    u64                     content_version { 1 };
};

struct BufferPhysical {
    vulkan::BufferAllocation buffer;
    u64                      generation { 1 };
    u64                      definition_generation { 1 };
    u64                      source_generation { 0 };
    u64                      source_identity {};
    u64                      submitted_generation { 0 };
    resource::ReadyToken     ready;

    BufferPhysical(vulkan::BufferAllocation value, u64 physical_generation, u64 definition_version)
        : buffer(rstd::move(value)),
          generation(physical_generation),
          definition_generation(definition_version) {}
};

struct PendingBufferUpload {
    Arc<BufferPhysical> physical;
    u64                 source_generation { 0 };
};

struct PreparedBuffer {
    resource::BufferHandle resource;
    Arc<BufferPhysical>    physical;

    auto clone() const -> PreparedBuffer {
        return PreparedBuffer {
            .resource = resource,
            .physical = physical.clone(),
        };
    }
};

class BufferRegistry {
public:
    auto Declare(resource::BufferRequest request) -> resource::BufferHandle {
        return Register(rstd::move(request));
    }

    auto Ensure(resource::BufferRequest request, slice<u8> content,
                mut_ref<dyn<vulkan::BufferBackend>> backend)
        -> Result<PreparedBuffer, resource::ResourceError> {
        auto handle = Register(request.clone());
        if (! handle.Valid()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::MissingDefinition,
                .message = rstd::format("invalid buffer request"),
            });
        }

        auto entry = m_entries.get(handle);
        if (entry.is_none()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::MissingDefinition,
                .message = rstd::format("buffer definition is unavailable"),
            });
        }

        auto existing = m_resources.get(handle);
        if (existing.is_some() &&
            (**existing)->definition_generation == (**entry).definition_version) {
            if ((**entry).request.lifetime != resource::BufferLifetimeClass::Dynamic) {
                auto queued = QueueWrite((**existing).clone(),
                                         content,
                                         request.content_version,
                                         backend,
                                         request.content_identity);
                if (queued.is_err()) return Err(rstd::move(queued).unwrap_err_unchecked());
            }
            return Ok(PreparedBuffer {
                .resource = handle,
                .physical = (**existing).clone(),
            });
        }

        auto allocated = backend->AllocateBuffer(vulkan::BufferAllocationRequest {
            .size      = static_cast<VkDeviceSize>(request.definition.size.to_primitive()),
            .alignment = static_cast<VkDeviceSize>(request.definition.alignment.to_primitive()),
            .usage     = AllocationUsage(request.definition.usage),
        });
        if (allocated.is_none()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("allocate buffer {} failed", request.name.as_str()),
            });
        }

        u64  physical_generation = existing.is_some() ? (**existing)->generation + u64(1) : u64(1);
        auto physical            = Arc<BufferPhysical>::make(
            rstd::move(*allocated), physical_generation, (**entry).definition_version);
        auto queued = QueueWrite(
            physical.clone(), content, request.content_version, backend, request.content_identity);
        if (queued.is_err()) return Err(rstd::move(queued).unwrap_err_unchecked());
        (void)m_resources.insert(handle, physical.clone());
        return Ok(PreparedBuffer {
            .resource = handle,
            .physical = rstd::move(physical),
        });
    }

    auto Resolve(resource::BufferHandle handle) const -> Option<Arc<BufferPhysical>> {
        auto physical = m_resources.get(handle);
        if (physical.is_none()) return None();
        return Some((**physical).clone());
    }

    auto ResolveBuffer(resource::BufferHandle handle) const -> Option<ref<BufferEntry>> {
        return m_entries.get(handle);
    }

    auto Update(resource::BufferHandle handle, slice<u8> content,
                mut_ref<dyn<vulkan::BufferBackend>> backend)
        -> Result<empty, resource::ResourceError> {
        auto entry    = m_entries.get_mut(handle);
        auto physical = m_resources.get_mut(handle);
        if (entry.is_none() || physical.is_none() ||
            (**entry).request.lifetime != resource::BufferLifetimeClass::Dynamic) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::MissingDefinition,
                .message = rstd::format("dynamic buffer is unavailable"),
            });
        }
        auto source_generation = (**physical)->source_generation + u64(1);
        if (source_generation == u64()) source_generation = u64(1);
        auto queued = QueueWrite((**physical).clone(),
                                 content,
                                 source_generation,
                                 backend,
                                 (**physical)->source_identity);
        if (queued.is_err()) return queued;
        ++(**entry).content_version;
        if ((**entry).content_version == u64()) (**entry).content_version = u64(1);
        return Ok(empty {});
    }

    void MarkUploadsSubmitted(slice<vulkan::BufferUploadTicket> tickets,
                              Option<resource::ReadyToken>      ready) {
        for (const auto& ticket : tickets) {
            auto pending = m_pending_uploads.remove(ticket.value);
            if (pending.is_none()) continue;
            pending->physical->submitted_generation = pending->source_generation;
            if (ready.is_some()) pending->physical->ready = *ready;
        }
    }

    void EvictUnused() {
        m_resources.retain([](const resource::BufferHandle&, Arc<BufferPhysical>& value) {
            return value.strong_count() > usize(1);
        });
    }

    void Reset() {
        m_resources.clear();
        m_pending_uploads.clear();
        m_entries.clear();
        m_names.clear();
        m_next_index = u64();
        ++m_generation;
        if (m_generation == u64()) ++m_generation;
    }

    auto Size() const noexcept -> usize { return m_entries.len(); }

private:
    auto QueueWrite(Arc<BufferPhysical> physical, slice<u8> content, u64 source_generation,
                    mut_ref<dyn<vulkan::BufferBackend>> backend, u64 source_identity)
        -> Result<empty, resource::ResourceError> {
        if (physical->source_generation == source_generation &&
            physical->source_identity == source_identity)
            return Ok(empty {});
        auto allocation =
            mut_ref<vulkan::BufferAllocation>::from_raw_parts(rstd::addressof(physical->buffer));
        auto ticket = backend->QueueBufferWrite(allocation, content);
        if (ticket.is_none()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("queue {} buffer bytes into {} byte allocation failed",
                                        content.len(),
                                        physical->buffer.size()),
            });
        }
        physical->source_generation = source_generation;
        physical->source_identity   = source_identity;
        if (ticket->Valid()) {
            (void)m_pending_uploads.insert(ticket->value,
                                           PendingBufferUpload {
                                               .physical          = physical.clone(),
                                               .source_generation = source_generation,
                                           });
        } else {
            physical->submitted_generation = source_generation;
        }
        return Ok(empty {});
    }

    static auto AllocationUsage(resource::BufferUsage usage) -> vulkan::BufferUploadClass {
        switch (usage) {
        case resource::BufferUsage::Vertex: return vulkan::BufferUploadClass::Vertex;
        case resource::BufferUsage::Index: return vulkan::BufferUploadClass::Index;
        case resource::BufferUsage::Uniform: return vulkan::BufferUploadClass::Uniform;
        case resource::BufferUsage::Storage: return vulkan::BufferUploadClass::Storage;
        case resource::BufferUsage::Transfer: return vulkan::BufferUploadClass::Transfer;
        }
        return vulkan::BufferUploadClass::Vertex;
    }

    auto Register(resource::BufferRequest request) -> resource::BufferHandle {
        if (request.name.is_empty() || request.definition.size == usize()) return {};
        auto existing = m_names.get(request.name);
        if (existing.is_some()) {
            auto entry = m_entries.get_mut(**existing);
            if (entry.is_none()) return {};
            if ((**entry).request.definition != request.definition) {
                ++(**entry).definition_version;
            }
            if ((**entry).request.content_version != request.content_version ||
                (**entry).request.content_identity != request.content_identity) {
                ++(**entry).content_version;
            }
            (**entry).request = rstd::move(request);
            return (**entry).handle;
        }

        auto handle = resource::BufferHandle {
            .index      = m_next_index++,
            .generation = m_generation,
        };
        auto name = request.name.clone();
        (void)m_entries.insert(handle,
                               BufferEntry {
                                   .handle  = handle,
                                   .request = rstd::move(request),
                               });
        (void)m_names.insert(rstd::move(name), handle);
        return handle;
    }

    template<typename Value>
    using HandleMap = HashMap<resource::BufferHandle, Value>;

    u64                                     m_generation { 1 };
    u64                                     m_next_index { 0 };
    HandleMap<BufferEntry>                  m_entries;
    HandleMap<Arc<BufferPhysical>>          m_resources;
    HashMap<String, resource::BufferHandle> m_names;
    HashMap<u64, PendingBufferUpload>       m_pending_uploads;
};

} // namespace vrento::resource_registry
