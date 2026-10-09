module;

export module vrento.resource_registry:texture_registry;
import rstd;
import vrento.resource;
import vrento.vulkan;

using rstd::collections::HashMap;
using rstd::collections::HashSet;
using rstd::sync::Arc;

using namespace rstd::prelude;

export namespace vrento::resource
{

struct Texture {
    resource::TextureHandle                id;
    TextureRequest                         request;
    Option<ImportedTextureContentIdentity> imported_content;
    u64                                    definition_version { 1 };
    u64                                    content_version { 1 };

    auto clone() const -> Texture {
        return Texture {
            .id               = id,
            .request          = request.clone(),
            .imported_content = imported_content.is_some() ? Some(imported_content->clone())
                                                           : None<ImportedTextureContentIdentity>(),
            .definition_version = definition_version,
            .content_version    = content_version,
        };
    }
};

struct TexturePhysical {
    Arc<vulkan::TextureAllocation> allocation;
    u64                            generation { 1 };
    u64                            source_generation { 0 };
    u64                            definition_version { 0 };
    u64                            content_version { 0 };
    ReadyToken                     ready;

    auto clone() const -> TexturePhysical {
        return TexturePhysical {
            .allocation         = allocation.clone(),
            .generation         = generation,
            .source_generation  = source_generation,
            .definition_version = definition_version,
            .content_version    = content_version,
            .ready              = ready,
        };
    }
};

struct PendingTextureUpload {
    TextureHandle   handle;
    TexturePhysical physical;
};

struct TextureRegistryIdentity {
    TextureRequestKind kind { TextureRequestKind::Imported };
    String             key;

    friend bool operator==(const TextureRegistryIdentity& lhs, const TextureRegistryIdentity& rhs) {
        return lhs.kind == rhs.kind && lhs.key == rhs.key.as_str();
    }

    auto clone() const -> TextureRegistryIdentity {
        return TextureRegistryIdentity {
            .kind = kind,
            .key  = key.clone(),
        };
    }
};

} // namespace vrento::resource

export namespace rstd
{

template<>
struct Impl<hash::Hash, vrento::resource::TextureRegistryIdentity>
    : ImplBase<vrento::resource::TextureRegistryIdentity> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(u32(static_cast<uint32_t>(this->self().kind)), state);
        hash::hash_into(this->self().key, state);
    }
};

template<>
struct Impl<hash::Hash, vrento::resource::ImportedTextureContentIdentity>
    : ImplBase<vrento::resource::ImportedTextureContentIdentity> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().key, state);
        hash::hash_into(this->self().revision, state);
    }
};

} // namespace rstd

export namespace vrento::resource
{

class TextureRegistry {
public:
    TextureRegistry()                                  = default;
    TextureRegistry(const TextureRegistry&)            = delete;
    TextureRegistry& operator=(const TextureRegistry&) = delete;

    auto Register(TextureRequest request) -> resource::TextureHandle {
        if (request.kind == TextureRequestKind::Imported) return {};
        return RegisterImpl(rstd::move(request), None<ImportedTextureContentIdentity>());
    }

    auto RegisterImported(TextureRequest request, ImportedTextureContentIdentity content)
        -> resource::TextureHandle {
        if (request.kind != TextureRequestKind::Imported || content.key.is_empty()) return {};
        return RegisterImpl(rstd::move(request), Some(rstd::move(content)));
    }

private:
    auto RegisterImpl(TextureRequest request, Option<ImportedTextureContentIdentity> content)
        -> resource::TextureHandle {
        if (request.name.is_empty()) return {};

        TextureRegistryIdentity identity {
            .kind = request.kind,
            .key  = request.name.clone(),
        };
        auto existing = m_handles.get(identity);
        if (existing.is_some()) {
            auto handle  = **existing;
            auto texture = m_textures.get_mut(handle);
            if (texture.is_none()) return {};
            if (! SameTextureRequest((**texture).request, request)) {
                if ((**texture).request.definition != request.definition) {
                    ++(**texture).definition_version;
                }
                if (request.kind != TextureRequestKind::Imported &&
                    (**texture).request.source != request.source) {
                    ++(**texture).content_version;
                }
                (**texture).request = rstd::move(request);
            }
            if ((**texture).imported_content != content) {
                ++(**texture).content_version;
                (**texture).imported_content = rstd::move(content);
            }
            return handle;
        }

        resource::TextureHandle handle {
            .index      = m_next_index++,
            .generation = m_generation,
        };
        (void)m_textures.insert(handle,
                                Texture {
                                    .id               = handle,
                                    .request          = rstd::move(request),
                                    .imported_content = rstd::move(content),
                                });
        (void)m_handles.insert(rstd::move(identity), handle);
        return handle;
    }

public:
    auto Publish(resource::TextureHandle handle, Arc<vulkan::TextureAllocation> allocation,
                 ReadyToken ready, Option<vulkan::ImageUploadTicket> upload = None())
        -> Option<u64> {
        auto image = allocation->View();
        if (ResolveTexture(handle).is_none() || image.slots.is_empty()) return None();

        auto logical = ResolveTexture(handle);
        if (logical.is_none()) return None();
        auto source_generation   = image.getActive().generation;
        auto current             = m_resources.get(handle);
        u64  physical_generation = current.is_some() ? (**current).generation : u64(1);
        if (current.is_some() && (**current).source_generation != source_generation) {
            ++physical_generation;
        }
        TexturePhysical physical {
            .allocation         = rstd::move(allocation),
            .generation         = physical_generation,
            .source_generation  = source_generation,
            .definition_version = (**logical).definition_version,
            .content_version    = (**logical).content_version,
            .ready              = ready,
        };
        if (upload.is_some() && upload->Valid()) {
            auto pending = m_pending_uploads.get_mut(upload->value);
            if (pending.is_none()) {
                (void)m_pending_uploads.insert(upload->value, Vec<PendingTextureUpload>::make());
                pending = m_pending_uploads.get_mut(upload->value);
            }
            if (pending.is_some()) {
                for (auto& item : **pending) {
                    if (item.handle != handle) continue;
                    item.physical = rstd::move(physical);
                    return Some(physical_generation);
                }
                (*pending)->push(
                    PendingTextureUpload { .handle = handle, .physical = rstd::move(physical) });
            }
        } else {
            (void)m_resources.insert(handle, rstd::move(physical));
        }
        return Some(physical_generation);
    }

    void MarkUploadsSubmitted(slice<vulkan::ImageUploadTicket> tickets, Option<ReadyToken> ready) {
        for (const auto& ticket : tickets) {
            auto pending = m_pending_uploads.remove(ticket.value);
            if (pending.is_none()) continue;
            for (auto& item : *pending) {
                if (ready.is_some()) item.physical.ready = *ready;
                (void)m_resources.insert(item.handle, rstd::move(item.physical));
            }
        }
    }

    void DiscardPendingUploads() { m_pending_uploads.clear(); }

    auto ResolveTexture(resource::TextureHandle handle) const noexcept -> Option<ref<Texture>> {
        return m_textures.get(handle);
    }

    auto ResolveTextureState(resource::TextureHandle handle) const noexcept
        -> Option<TextureLogicalState> {
        auto texture = ResolveTexture(handle);
        if (texture.is_none()) return None();
        return Some(TextureLogicalState {
            .handle             = handle,
            .definition_version = (**texture).definition_version,
            .content_version    = (**texture).content_version,
        });
    }

    auto Resolve(resource::TextureHandle handle) const noexcept -> Option<ref<TexturePhysical>> {
        return m_resources.get(handle);
    }

    auto ResolveCurrent(resource::TextureHandle handle) const noexcept
        -> Option<ref<TexturePhysical>> {
        auto logical  = ResolveTexture(handle);
        auto physical = Resolve(handle);
        if (logical.is_none() || physical.is_none()) return None();
        if ((**logical).definition_version != (**physical).definition_version ||
            (**logical).content_version != (**physical).content_version) {
            return None();
        }
        return physical;
    }

    auto ResolvePending(resource::TextureHandle handle) const noexcept
        -> Option<ref<TexturePhysical>> {
        auto logical = ResolveTexture(handle);
        if (logical.is_none()) return None();
        auto batches = m_pending_uploads.values();
        for (auto batch = batches.next(); batch.is_some(); batch = batches.next()) {
            for (const auto& item : **batch) {
                if (item.handle != handle) continue;
                if ((**logical).definition_version != item.physical.definition_version ||
                    (**logical).content_version != item.physical.content_version) {
                    continue;
                }
                return Some(ref<TexturePhysical>::from_raw_parts(rstd::addressof(item.physical)));
            }
        }
        return None();
    }

    auto Find(TextureRequestKind kind, ref<str> key) const -> Option<resource::TextureHandle> {
        auto handle = m_handles.get(TextureRegistryIdentity {
            .kind = kind,
            .key  = String::make(key),
        });
        if (handle.is_none()) return None();
        auto value = **handle;
        return Some(rstd::move(value));
    }

    bool Unbind(resource::TextureHandle handle) { return m_resources.remove(handle).is_some(); }

    bool BeginPrepareTransaction() {
        if (m_transaction.is_some() || ! m_pending_uploads.is_empty()) return false;
        m_transaction = Some(Box<Snapshot>::make(CloneSnapshot()));
        return true;
    }

    void CommitPrepareTransaction() { (void)m_transaction.take(); }

    void AbortPrepareTransaction() {
        auto snapshot = m_transaction.take();
        if (snapshot.is_none()) return;
        m_textures   = rstd::move((*snapshot)->textures);
        m_resources  = rstd::move((*snapshot)->resources);
        m_handles    = rstd::move((*snapshot)->handles);
        m_next_index = (*snapshot)->next_index;
        m_pending_uploads.clear();
    }

    void RetainActive(slice<resource::TextureHandle> handles) {
        auto active = HashSet<resource::TextureHandle>::with_capacity(handles.len());
        for (usize index {}; index < handles.len(); ++index) {
            (void)active.insert(handles[index]);
        }
        m_resources.retain([&](const resource::TextureHandle& handle, TexturePhysical&) {
            return active.contains(handle);
        });
    }

    void EvictUnused(bool transient_only = false) {
        m_resources.retain([&](const resource::TextureHandle& handle, TexturePhysical& physical) {
            if (physical.allocation.strong_count() > usize(1)) return true;
            if (! transient_only) return false;
            auto texture = m_textures.get(handle);
            return texture.is_none() ||
                   (**texture).request.lifetime != TextureLifetimeClass::FrameLocal;
        });
    }

    void ClearGraphResources() {
        m_resources.retain([&](const resource::TextureHandle& handle, TexturePhysical&) {
            auto texture = m_textures.get(handle);
            return texture.is_some() && (**texture).request.kind == TextureRequestKind::Imported;
        });
    }

    void Reset() {
        m_transaction = None();
        m_resources.clear();
        m_pending_uploads.clear();
        m_textures.clear();
        m_handles.clear();
        m_next_index = u64();
        ++m_generation;
        if (m_generation == u64()) ++m_generation;
    }

    auto Generation() const noexcept -> u64 { return m_generation; }
    auto Size() const noexcept -> usize { return m_textures.len(); }

private:
    template<typename Value>
    using HandleMap = HashMap<resource::TextureHandle, Value>;

    struct Snapshot {
        HandleMap<Texture>                                        textures;
        HandleMap<TexturePhysical>                                resources;
        HashMap<TextureRegistryIdentity, resource::TextureHandle> handles;
        u64                                                       next_index { 0 };
    };

    auto CloneSnapshot() const -> Snapshot {
        Snapshot snapshot;
        snapshot.textures.reserve(m_textures.len());
        auto textures = m_textures.iter();
        for (auto item = textures.next(); item.is_some(); item = textures.next()) {
            (void)snapshot.textures.insert(*item->template get<0>(),
                                           item->template get<1>()->clone());
        }
        snapshot.resources.reserve(m_resources.len());
        auto resources = m_resources.iter();
        for (auto item = resources.next(); item.is_some(); item = resources.next()) {
            (void)snapshot.resources.insert(*item->template get<0>(),
                                            item->template get<1>()->clone());
        }
        snapshot.handles.reserve(m_handles.len());
        auto handles = m_handles.iter();
        for (auto item = handles.next(); item.is_some(); item = handles.next()) {
            (void)snapshot.handles.insert(item->template get<0>()->clone(),
                                          *item->template get<1>());
        }
        snapshot.next_index = m_next_index;
        return snapshot;
    }

    u64                                                       m_generation { 1 };
    u64                                                       m_next_index { 0 };
    HandleMap<Texture>                                        m_textures;
    HandleMap<TexturePhysical>                                m_resources;
    HashMap<TextureRegistryIdentity, resource::TextureHandle> m_handles;
    HashMap<u64, Vec<PendingTextureUpload>>                   m_pending_uploads;
    Option<Box<Snapshot>>                                     m_transaction;
};

} // namespace vrento::resource

export namespace rstd
{

template<>
struct Impl<vrento::resource::TextureLogicalRegistryView, vrento::resource::TextureRegistry>
    : ImplBase<vrento::resource::TextureRegistry> {
    auto ResolveTextureState(vrento::resource::TextureHandle handle) const
        -> Option<vrento::resource::TextureLogicalState> {
        return this->self().ResolveTextureState(handle);
    }
};

} // namespace rstd
