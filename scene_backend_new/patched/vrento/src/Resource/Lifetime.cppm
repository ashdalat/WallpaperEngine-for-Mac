export module vrento.resource_registry:lifetime;
import rstd;
import vrento.resource;
import vrento.vulkan;

import :prepared;

using rstd::collections::HashMap;

export namespace vrento::resource_registry
{

using namespace rstd::prelude;

struct UploadLease {
    resource::ReadyToken                   ready;
    Option<vulkan::BufferUploadBatchLease> buffers;
    Option<vulkan::ImageUploadBatchLease>  images;
};

class UploadScheduler {
public:
    auto Reserve() -> resource::ReadyToken {
        ++m_next_value;
        if (m_next_value == u64()) ++m_next_value;
        return resource::ReadyToken { .value = m_next_value };
    }

    bool MarkSubmitted(resource::ReadyToken ready, Option<vulkan::BufferUploadBatchLease> buffers,
                       Option<vulkan::ImageUploadBatchLease> images) {
        if (! ready.Valid() || ready.value > m_next_value) return false;
        if (m_in_flight
                .insert(ready.value,
                        UploadLease {
                            .ready   = ready,
                            .buffers = rstd::move(buffers),
                            .images  = rstd::move(images),
                        })
                .is_some()) {
            return false;
        }
        if (m_pending.is_none() || m_pending->value < ready.value) m_pending = Some(ready);
        return true;
    }

    bool MarkSubmitted(resource::ReadyToken ready, vulkan::BufferUploadBatchLease buffers) {
        return MarkSubmitted(
            ready, Some(rstd::move(buffers)), None<vulkan::ImageUploadBatchLease>());
    }

    auto Pending() const -> Option<resource::ReadyToken> {
        if (m_pending.is_none()) return None();
        return Some(resource::ReadyToken { .value = m_pending->value });
    }

    void CompleteThrough(u64 value) {
        m_in_flight.retain([value](const u64& key, UploadLease&) {
            return key > value;
        });
        if (m_pending.is_some() && m_pending->value <= value) m_pending = None();
    }

    void Reset() {
        m_in_flight.clear();
        m_pending    = None();
        m_next_value = u64();
    }

    auto InFlight() const noexcept -> usize { return m_in_flight.len(); }

private:
    u64                          m_next_value { 0 };
    Option<resource::ReadyToken> m_pending;
    HashMap<u64, UploadLease>    m_in_flight;
};

struct SubmissionLease {
    resource::CompletionToken              completion;
    u64                                    program_generation { 0 };
    Vec<PreparedTextureLease>              textures;
    Vec<PreparedBufferLease>               buffers;
    Vec<PreparedShaderLease>               shaders;
    Vec<PreparedPipelineLease>             pipelines;
    Vec<PreparedRenderPassLease>           render_passes;
    Vec<PreparedFramebufferLease>          framebuffers;
    Vec<PreparedDescriptorLease>           descriptors;
    Vec<PreparedExternalLease>             externals;
    Option<vulkan::BufferUploadBatchLease> buffer_uploads;
};

class SubmissionTracker {
public:
    auto Begin(const PreparedResourceTable&           resources,
               Option<vulkan::BufferUploadBatchLease> buffer_uploads = None())
        -> resource::CompletionToken {
        ++m_next_value;
        if (m_next_value == u64()) ++m_next_value;
        resource::CompletionToken completion { .value = m_next_value };
        auto                      leases = resources.Leases();
        if (m_in_flight
                .insert(completion.value,
                        SubmissionLease {
                            .completion         = completion,
                            .program_generation = resources.Generation(),
                            .textures           = rstd::move(leases.textures),
                            .buffers            = rstd::move(leases.buffers),
                            .shaders            = rstd::move(leases.shaders),
                            .pipelines          = rstd::move(leases.pipelines),
                            .render_passes      = rstd::move(leases.render_passes),
                            .framebuffers       = rstd::move(leases.framebuffers),
                            .descriptors        = rstd::move(leases.descriptors),
                            .externals          = rstd::move(leases.externals),
                            .buffer_uploads     = rstd::move(buffer_uploads),
                        })
                .is_some()) {
            return {};
        }
        return completion;
    }

    auto Complete(resource::CompletionToken completion) -> Option<SubmissionLease> {
        return m_in_flight.remove(completion.value);
    }

    void Reset() {
        m_in_flight.clear();
        m_next_value = u64();
    }

    auto InFlight() const noexcept -> usize { return m_in_flight.len(); }

private:
    u64                           m_next_value { 0 };
    HashMap<u64, SubmissionLease> m_in_flight;
};

} // namespace vrento::resource_registry
