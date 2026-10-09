export module vrento.resource_registry:shader_registry;
import rstd;
import vrento.resource;

using rstd::collections::HashMap;
using rstd::sync::Arc;

export namespace vrento::resource_registry
{

using namespace rstd::prelude;

struct ShaderPhysical {
    resource::ShaderArtifact artifact;
    u64                      physical_generation { 1 };

    ShaderPhysical(resource::ShaderArtifact value, u64 generation)
        : artifact(rstd::move(value)), physical_generation(generation) {}
};

struct ShaderEntry {
    resource::ShaderHandle  handle;
    resource::ShaderRequest request;
    Arc<ShaderPhysical>     physical;
};

struct PreparedShader {
    resource::ShaderHandle resource;
    Arc<ShaderPhysical>    physical;

    auto clone() const -> PreparedShader {
        return PreparedShader {
            .resource = resource,
            .physical = physical.clone(),
        };
    }
};

class ShaderRegistry {
public:
    auto Prepare(resource::ShaderRequest                        request,
                 mut_ref<dyn<resource::ShaderArtifactProvider>> provider)
        -> Result<PreparedShader, resource::ResourceError> {
        auto existing = m_requests.get(request);
        if (existing.is_some()) {
            auto entry = m_entries.get(**existing);
            if (entry.is_none()) {
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::BackendFailure,
                    .message = rstd::format("shader registry entry is unavailable"),
                });
            }
            return Ok(PreparedShader {
                .resource = (**entry).handle,
                .physical = (**entry).physical.clone(),
            });
        }

        auto artifact = provider->LoadShader(request);
        if (artifact.is_err()) return Err(rstd::move(artifact).unwrap_err_unchecked());
        auto handle      = NextHandle();
        auto request_key = request.clone();
        auto physical = Arc<ShaderPhysical>::make(rstd::move(artifact).unwrap_unchecked(), u64(1));
        (void)m_entries.insert(handle,
                               ShaderEntry {
                                   .handle   = handle,
                                   .request  = rstd::move(request),
                                   .physical = physical.clone(),
                               });
        (void)m_requests.insert(rstd::move(request_key), handle);
        return Ok(PreparedShader {
            .resource = handle,
            .physical = rstd::move(physical),
        });
    }

    auto Ensure(resource::ShaderRequest                        request,
                mut_ref<dyn<resource::ShaderArtifactProvider>> provider)
        -> Result<resource::ShaderHandle, resource::ResourceError> {
        auto prepared = Prepare(rstd::move(request), provider);
        if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err_unchecked());
        return Ok(rstd::move(prepared).unwrap_unchecked().resource);
    }

    auto Resolve(resource::ShaderHandle handle) const noexcept -> Option<ref<ShaderEntry>> {
        return m_entries.get(handle);
    }

    void Reset() {
        m_entries.clear();
        m_requests.clear();
        m_next_index = u64();
        ++m_generation;
        if (m_generation == u64()) ++m_generation;
    }

    auto Generation() const noexcept -> u64 { return m_generation; }
    auto Size() const noexcept -> usize { return m_entries.len(); }

private:
    auto NextHandle() -> resource::ShaderHandle {
        return { .index = m_next_index++, .generation = m_generation };
    }

    using EntryMap = HashMap<resource::ShaderHandle, ShaderEntry>;

    u64                                                      m_generation { 1 };
    u64                                                      m_next_index { 0 };
    EntryMap                                                 m_entries;
    HashMap<resource::ShaderRequest, resource::ShaderHandle> m_requests;
};

} // namespace vrento::resource_registry
