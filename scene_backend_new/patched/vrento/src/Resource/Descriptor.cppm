module;

#include <algorithm>

export module vrento.resource_registry:descriptor;
import rstd;
import vrento.resource;
import vrento.vulkan;

using rstd::collections::HashMap;
using rstd::sync::Arc;

export namespace vrento::resource_registry
{

using namespace rstd::prelude;

struct DescriptorBindingSchema {
    rstd::uint32_t binding { 0 };
    rstd::uint32_t descriptor_type { 0 };
    rstd::uint32_t descriptor_count { 1 };
    rstd::uint32_t stage_flags { 0 };

    friend bool operator==(const DescriptorBindingSchema&,
                           const DescriptorBindingSchema&) = default;
};

} // namespace vrento::resource_registry

export namespace rstd
{

template<>
struct Impl<Copy, vrento::resource_registry::DescriptorBindingSchema> {};

} // namespace rstd

export namespace vrento::resource_registry
{

struct DescriptorSetSchema {
    bool                         push_descriptor { false };
    Vec<DescriptorBindingSchema> bindings;

    auto clone() const -> DescriptorSetSchema {
        return DescriptorSetSchema {
            .push_descriptor = push_descriptor,
            .bindings        = bindings.clone(),
        };
    }

    friend bool operator==(const DescriptorSetSchema& lhs, const DescriptorSetSchema& rhs) {
        if (lhs.push_descriptor != rhs.push_descriptor ||
            lhs.bindings.len() != rhs.bindings.len()) {
            return false;
        }
        for (usize index = usize(); index < lhs.bindings.len(); ++index) {
            if (lhs.bindings[index] != rhs.bindings[index]) return false;
        }
        return true;
    }
};

} // namespace vrento::resource_registry

export namespace rstd
{

template<>
struct Impl<hash::Hash, vrento::resource_registry::DescriptorSetSchema>
    : ImplBase<vrento::resource_registry::DescriptorSetSchema> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        const auto& schema = this->self();
        hash::hash_into(schema.push_descriptor, state);
        hash::hash_into(schema.bindings.len(), state);
        for (const auto& binding : schema.bindings) {
            hash::hash_into(binding.binding, state);
            hash::hash_into(binding.descriptor_type, state);
            hash::hash_into(binding.descriptor_count, state);
            hash::hash_into(binding.stage_flags, state);
        }
    }
};

} // namespace rstd

export namespace vrento::resource_registry
{

struct DescriptorLayoutEntry {
    resource::DescriptorLayoutHandle handle;
    DescriptorSetSchema              schema;
    vvk::DescriptorSetLayout         layout;
};

class DescriptorLayoutRegistry {
public:
    DescriptorLayoutRegistry()                                           = default;
    DescriptorLayoutRegistry(const DescriptorLayoutRegistry&)            = delete;
    DescriptorLayoutRegistry& operator=(const DescriptorLayoutRegistry&) = delete;

    auto Ensure(const vulkan::Device& device, const vulkan::DescriptorSetInfo& info)
        -> Result<resource::DescriptorLayoutHandle, resource::ResourceError> {
        if (info.push_descriptor && ! device.capabilities().push_descriptor) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("push descriptors are unavailable"),
            });
        }
        auto schema = CanonicalSchema(info);
        if (schema.is_err()) return Err(rstd::move(schema).unwrap_err_unchecked());
        auto normalized = rstd::move(schema).unwrap_unchecked();
        if (auto existing = m_handles.get(normalized); existing.is_some()) return Ok(**existing);

        auto bindings = Vec<VkDescriptorSetLayoutBinding>::with_capacity(normalized.bindings.len());
        for (const auto& binding : normalized.bindings) {
            bindings.push(VkDescriptorSetLayoutBinding {
                .binding            = binding.binding,
                .descriptorType     = static_cast<VkDescriptorType>(binding.descriptor_type),
                .descriptorCount    = binding.descriptor_count,
                .stageFlags         = binding.stage_flags,
                .pImmutableSamplers = nullptr,
            });
        }

        VkDescriptorSetLayoutCreateInfo create_info {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext        = nullptr,
            .flags        = normalized.push_descriptor
                                ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR
                                : VkDescriptorSetLayoutCreateFlags {},
            .bindingCount = static_cast<rstd::uint32_t>(bindings.len().to_primitive()),
            .pBindings    = bindings.data(),
        };
        vvk::DescriptorSetLayout layout;
        if (device.handle().CreateDescriptorSetLayout(create_info, layout) != VK_SUCCESS) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("create descriptor set layout failed"),
            });
        }

        resource::DescriptorLayoutHandle handle {
            .index      = m_next_index++,
            .generation = m_generation,
        };
        auto identity = normalized.clone();
        (void)m_entries.insert(handle,
                               DescriptorLayoutEntry {
                                   .handle = handle,
                                   .schema = rstd::move(normalized),
                                   .layout = rstd::move(layout),
                               });
        (void)m_handles.insert(rstd::move(identity), handle);
        return Ok(handle);
    }

    auto Resolve(resource::DescriptorLayoutHandle handle) const
        -> Option<ref<DescriptorLayoutEntry>> {
        return m_entries.get(handle);
    }

    void Reset() {
        m_handles.clear();
        m_entries.clear();
        m_next_index = u64();
        ++m_generation;
        if (m_generation == u64()) ++m_generation;
    }

    auto Size() const noexcept -> usize { return m_entries.len(); }

    static auto CanonicalSchema(const vulkan::DescriptorSetInfo& info)
        -> Result<DescriptorSetSchema, resource::ResourceError> {
        auto bindings = Vec<DescriptorBindingSchema>::with_capacity(info.bindings.len());
        for (const auto& binding : info.bindings) {
            if (binding.pImmutableSamplers != nullptr) {
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::MissingDefinition,
                    .message = rstd::format("immutable descriptor samplers are not supported"),
                });
            }
            bindings.push(DescriptorBindingSchema {
                .binding          = binding.binding,
                .descriptor_type  = static_cast<rstd::uint32_t>(binding.descriptorType),
                .descriptor_count = binding.descriptorCount,
                .stage_flags      = binding.stageFlags,
            });
        }
        rstd::slice_::sort_unstable_by(bindings.as_mut_slice().as_mut_ref(),
                                       [](const auto& lhs, const auto& rhs) {
                                           return lhs.binding < rhs.binding;
                                       });
        for (usize index = usize(); index < bindings.len(); ++index) {
            if (bindings[index].descriptor_count == 0 || bindings[index].stage_flags == 0 ||
                (index > usize() &&
                 bindings[index - usize(1)].binding == bindings[index].binding)) {
                return Err(resource::ResourceError {
                    .kind = resource::ResourceErrorKind::MissingDefinition,
                    .message =
                        rstd::format("invalid descriptor binding {}", bindings[index].binding),
                });
            }
        }
        return Ok(DescriptorSetSchema {
            .push_descriptor = info.push_descriptor,
            .bindings        = rstd::move(bindings),
        });
    }

private:
    using EntryMap    = HashMap<resource::DescriptorLayoutHandle, DescriptorLayoutEntry>;
    using IdentityMap = HashMap<DescriptorSetSchema, resource::DescriptorLayoutHandle>;

    u64         m_generation { 1 };
    u64         m_next_index { 0 };
    EntryMap    m_entries;
    IdentityMap m_handles;
};

struct DescriptorImageBinding {
    rstd::uint32_t          binding { 0 };
    vulkan::ImageParameters image;
    VkImageLayout           layout { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
};

struct DescriptorBufferBinding {
    rstd::uint32_t binding { 0 };
    VkBuffer       buffer { VK_NULL_HANDLE };
    VkDeviceSize   offset { 0 };
    VkDeviceSize   size { 0 };
};

} // namespace vrento::resource_registry

export namespace rstd
{

template<>
struct Impl<Copy, vrento::resource_registry::DescriptorImageBinding> {};

template<>
struct Impl<Copy, vrento::resource_registry::DescriptorBufferBinding> {};

} // namespace rstd

export namespace vrento::resource_registry
{

struct DescriptorSetPacketKey {
    resource::DescriptorLayoutHandle layout;
    Vec<DescriptorBufferBinding>     buffers;

    friend bool operator==(const DescriptorSetPacketKey& lhs, const DescriptorSetPacketKey& rhs) {
        if (lhs.layout != rhs.layout || lhs.buffers.len() != rhs.buffers.len()) return false;
        for (usize index {}; index < lhs.buffers.len(); ++index) {
            const auto& lhs_buffer = lhs.buffers[index];
            const auto& rhs_buffer = rhs.buffers[index];
            if (lhs_buffer.binding != rhs_buffer.binding ||
                lhs_buffer.buffer != rhs_buffer.buffer || lhs_buffer.offset != rhs_buffer.offset ||
                lhs_buffer.size != rhs_buffer.size) {
                return false;
            }
        }
        return true;
    }
};

} // namespace vrento::resource_registry

export namespace rstd
{

template<>
struct Impl<hash::Hash, vrento::resource_registry::DescriptorSetPacketKey>
    : ImplBase<vrento::resource_registry::DescriptorSetPacketKey> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        const auto& key = this->self();
        hash::hash_into(key.layout, state);
        hash::hash_into(key.buffers.len(), state);
        for (const auto& buffer : key.buffers) {
            hash::hash_into(buffer.binding, state);
            hash::hash_into(reinterpret_cast<rstd::uintptr_t>(buffer.buffer), state);
            hash::hash_into(buffer.offset, state);
            hash::hash_into(buffer.size, state);
        }
    }
};

} // namespace rstd

export namespace vrento::resource_registry
{

struct DescriptorBindingRecordState {
    struct BoundSet {
        rstd::uint32_t                   set_index { 0 };
        resource::DescriptorLayoutHandle layout;
        VkDescriptorSet                  set { VK_NULL_HANDLE };
    };

    bool BindRequired(rstd::uint32_t set_index, resource::DescriptorLayoutHandle layout,
                      VkDescriptorSet set) {
        for (const auto& bound : m_bound) {
            if (bound.set_index != set_index) continue;
            if (bound.layout == layout && bound.set == set) return false;
            break;
        }
        InvalidateFrom(set_index);
        m_bound.push(BoundSet {
            .set_index = set_index,
            .layout    = layout,
            .set       = set,
        });
        return true;
    }

    void UsePipeline(resource::PipelineLayoutHandle          pipeline,
                     slice<resource::DescriptorLayoutHandle> descriptor_layouts,
                     rstd::uint64_t                          push_constant_identity) {
        if (m_pipeline.is_some() && *m_pipeline == pipeline) return;

        rstd::uint32_t incompatible_from {};
        if (m_pipeline.is_some() && m_push_constant_identity == push_constant_identity) {
            auto common = rstd::cmp::min(m_descriptor_layouts.len(), descriptor_layouts.len());
            incompatible_from = static_cast<rstd::uint32_t>(common.to_primitive());
            bool compatible   = true;
            for (usize index {}; index < common; ++index) {
                if (m_descriptor_layouts[index] == descriptor_layouts[index]) continue;
                incompatible_from = static_cast<rstd::uint32_t>(index.to_primitive());
                compatible        = false;
                break;
            }
            if (compatible && common == m_descriptor_layouts.len() &&
                common == descriptor_layouts.len()) {
                incompatible_from = rstd::uint32_t(-1);
            }
        }
        if (incompatible_from != rstd::uint32_t(-1)) InvalidateFrom(incompatible_from);

        m_pipeline = Some(resource::PipelineLayoutHandle {
            .index      = pipeline.index,
            .generation = pipeline.generation,
        });
        m_descriptor_layouts.clear();
        m_descriptor_layouts.reserve(descriptor_layouts.len());
        for (const auto& layout : descriptor_layouts) {
            m_descriptor_layouts.push(resource::DescriptorLayoutHandle {
                .index      = layout.index,
                .generation = layout.generation,
            });
        }
        m_push_constant_identity = push_constant_identity;
    }

    void Push(rstd::uint32_t set_index) { InvalidateFrom(set_index); }

    void Reset() {
        m_bound.clear();
        m_pipeline = None();
        m_descriptor_layouts.clear();
        m_push_constant_identity = 0;
    }

private:
    void InvalidateFrom(rstd::uint32_t set_index) {
        auto retained = Vec<BoundSet>::with_capacity(m_bound.len());
        for (const auto& bound : m_bound) {
            if (bound.set_index < set_index) {
                retained.push(BoundSet {
                    .set_index = bound.set_index,
                    .layout    = bound.layout,
                    .set       = bound.set,
                });
            }
        }
        m_bound = rstd::move(retained);
    }

    Vec<BoundSet>                          m_bound;
    Option<resource::PipelineLayoutHandle> m_pipeline;
    Vec<resource::DescriptorLayoutHandle>  m_descriptor_layouts;
    rstd::uint64_t                         m_push_constant_identity {};
};

enum class DescriptorBindingBackend
{
    Push,
    Set,
};

enum class DescriptorBindingReuse
{
    Exclusive,
    Shared,
};

struct PreparedDescriptorBinding {
    resource::DescriptorBindingHandle handle;
    resource::DescriptorLayoutHandle  layout;
    rstd::uint32_t                    set_index { 0 };
    DescriptorBindingBackend          backend { DescriptorBindingBackend::Push };
    Vec<DescriptorImageBinding>       images;
    Vec<DescriptorBufferBinding>      buffers;
    Option<vvk::DescriptorSetLease>   set;

    auto clone() const -> PreparedDescriptorBinding {
        return PreparedDescriptorBinding {
            .handle    = handle,
            .layout    = layout,
            .set_index = set_index,
            .backend   = backend,
            .images    = images.clone(),
            .buffers   = buffers.clone(),
            .set       = set.is_some() ? Some(set->clone()) : None(),
        };
    }

    auto UpdateImages(slice<DescriptorImageBinding> next)
        -> Result<empty, resource::ResourceError> {
        if (images.len() != next.len()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::MissingDefinition,
                .message = rstd::format("descriptor image binding count changed"),
            });
        }

        bool changed = false;
        for (usize index {}; index < next.len(); ++index) {
            const auto& current = images[index];
            changed |= current.binding != next[index].binding ||
                       current.image.view != next[index].image.view ||
                       current.image.sampler != next[index].image.sampler ||
                       current.layout != next[index].layout;
        }
        if (! changed) return Ok(empty {});

        auto updated = Vec<DescriptorImageBinding>::from(next);

        if (backend == DescriptorBindingBackend::Set) {
            if (set.is_none() || ! set->valid()) {
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::MissingDefinition,
                    .message = rstd::format("descriptor set is unavailable"),
                });
            }
            vvk::DescriptorUpdateBatch updates;
            for (const auto& binding : updated) {
                VkDescriptorImageInfo image {
                    .sampler     = binding.image.sampler,
                    .imageView   = binding.image.view,
                    .imageLayout = binding.layout,
                };
                if (! updates.WriteImage(
                        set->clone(),
                        binding.binding,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        slice<VkDescriptorImageInfo>::from_raw_parts(&image, usize(1)))) {
                    return Err(resource::ResourceError {
                        .kind    = resource::ResourceErrorKind::BackendFailure,
                        .message = rstd::format("update descriptor image binding failed"),
                    });
                }
            }
            if (! updates.Commit().committed()) {
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::BackendFailure,
                    .message = rstd::format("update descriptor set failed"),
                });
            }
        }

        images = rstd::move(updated);
        return Ok(empty {});
    }

    void Record(vvk::CommandBuffer& command, VkPipelineLayout pipeline_layout,
                DescriptorBindingRecordState& state) const {
        if (backend == DescriptorBindingBackend::Set) {
            if (set.is_none() || ! set->valid()) return;
            auto descriptor_set = set->handle;
            if (! state.BindRequired(set_index, layout, descriptor_set)) return;
            command.BindDescriptorSets(
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline_layout,
                set_index,
                slice<VkDescriptorSet>::from_raw_parts(&descriptor_set, usize(1)),
                {});
            return;
        }
        state.Push(set_index);
        for (const auto& binding : images) {
            VkDescriptorImageInfo image {
                .sampler     = binding.image.sampler,
                .imageView   = binding.image.view,
                .imageLayout = binding.layout,
            };
            VkWriteDescriptorSet write {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext           = nullptr,
                .dstSet          = {},
                .dstBinding      = binding.binding,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo      = &image,
            };
            command.PushDescriptorSetKHR(
                VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, set_index, write);
        }
        for (const auto& buffer : buffers) {
            VkDescriptorBufferInfo info {
                .buffer = buffer.buffer,
                .offset = buffer.offset,
                .range  = buffer.size,
            };
            VkWriteDescriptorSet write {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext           = nullptr,
                .dstSet          = {},
                .dstBinding      = buffer.binding,
                .descriptorCount = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo     = &info,
            };
            command.PushDescriptorSetKHR(
                VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, set_index, write);
        }
    }
};

class DescriptorSystem {
public:
    auto PreparePush(rstd::uint32_t set_index, rstd::slice<DescriptorImageBinding> images,
                     rstd::slice<DescriptorBufferBinding> buffers) -> PreparedDescriptorBinding {
        auto prepared_images = Vec<DescriptorImageBinding>::from(images);
        rstd::slice_::sort_unstable_by(prepared_images.as_mut_slice().as_mut_ref(),
                                       [](const auto& lhs, const auto& rhs) {
                                           return lhs.binding < rhs.binding;
                                       });
        auto prepared_buffers = Vec<DescriptorBufferBinding>::from(buffers);
        rstd::slice_::sort_unstable_by(prepared_buffers.as_mut_slice().as_mut_ref(),
                                       [](const auto& lhs, const auto& rhs) {
                                           return lhs.binding < rhs.binding;
                                       });
        return PreparedDescriptorBinding {
            .handle =
                resource::DescriptorBindingHandle {
                    .index      = m_next_index++,
                    .generation = m_generation,
                },
            .set_index = set_index,
            .backend   = DescriptorBindingBackend::Push,
            .images    = rstd::move(prepared_images),
            .buffers   = rstd::move(prepared_buffers),
        };
    }

    auto Prepare(const vulkan::Device& device, rstd::uint32_t set_index,
                 const DescriptorLayoutEntry& layout, rstd::slice<DescriptorImageBinding> images,
                 rstd::slice<DescriptorBufferBinding> buffers, DescriptorBindingReuse reuse)
        -> Result<PreparedDescriptorBinding, resource::ResourceError> {
        for (usize index {}; index < images.len(); ++index) {
            const auto& image = images[index];
            if (! HasBinding(
                    layout.schema, image.binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)) {
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::MissingDefinition,
                    .message = rstd::format("descriptor image binding {} is outside the layout",
                                            image.binding),
                });
            }
            for (usize previous {}; previous < index; ++previous) {
                if (images[previous].binding != image.binding) continue;
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::MissingDefinition,
                    .message = rstd::format("duplicate descriptor image binding {}", image.binding),
                });
            }
        }
        for (usize index {}; index < buffers.len(); ++index) {
            if (! HasBinding(
                    layout.schema, buffers[index].binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)) {
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::MissingDefinition,
                    .message = rstd::format("descriptor buffer binding {} is outside the layout",
                                            buffers[index].binding),
                });
            }
            for (usize previous {}; previous < index; ++previous) {
                if (buffers[previous].binding != buffers[index].binding) continue;
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::MissingDefinition,
                    .message = rstd::format("duplicate descriptor buffer binding {}",
                                            buffers[index].binding),
                });
            }
            for (const auto& image : images) {
                if (image.binding != buffers[index].binding) continue;
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::MissingDefinition,
                    .message = rstd::format("descriptor binding {} has multiple writes",
                                            buffers[index].binding),
                });
            }
        }
        auto prepared   = PreparePush(set_index, images, buffers);
        prepared.layout = layout.handle;
        if (layout.schema.push_descriptor) return Ok(rstd::move(prepared));
        prepared.backend = DescriptorBindingBackend::Set;
        prepared.layout  = layout.handle;

        Option<DescriptorSetPacketKey> cache_key = None();
        if (reuse == DescriptorBindingReuse::Shared && images.is_empty()) {
            cache_key = Some(DescriptorSetPacketKey {
                .layout  = layout.handle,
                .buffers = prepared.buffers.clone(),
            });
            if (auto cached = m_packets.get(*cache_key); cached.is_some()) {
                prepared.set = Some((**cached).clone());
                return Ok(rstd::move(prepared));
            }
        }

        auto allocated = AllocateSet(device, *layout.layout);
        if (allocated.is_err()) return Err(rstd::move(allocated).unwrap_err_unchecked());
        prepared.set = Some(rstd::move(allocated).unwrap_unchecked());

        vvk::DescriptorUpdateBatch updates;
        for (const auto& binding : prepared.images) {
            VkDescriptorImageInfo image {
                .sampler     = binding.image.sampler,
                .imageView   = binding.image.view,
                .imageLayout = binding.layout,
            };
            if (! updates.WriteImage(
                    prepared.set->clone(),
                    binding.binding,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    slice<VkDescriptorImageInfo>::from_raw_parts(&image, usize(1)))) {
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::BackendFailure,
                    .message = rstd::format("prepare descriptor image binding failed"),
                });
            }
        }
        for (const auto& buffer : prepared.buffers) {
            VkDescriptorBufferInfo info {
                .buffer = buffer.buffer,
                .offset = buffer.offset,
                .range  = buffer.size,
            };
            if (! updates.WriteBuffer(
                    prepared.set->clone(),
                    buffer.binding,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    slice<VkDescriptorBufferInfo>::from_raw_parts(&info, usize(1)))) {
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::BackendFailure,
                    .message = rstd::format("prepare descriptor buffer binding failed"),
                });
            }
        }
        if (! updates.Commit().committed()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("update descriptor set failed"),
            });
        }
        if (cache_key.is_some()) {
            (void)m_packets.insert(rstd::move(*cache_key), prepared.set->clone());
        }
        return Ok(rstd::move(prepared));
    }

    void Reset() {
        m_packets.clear();
        m_pool       = None();
        m_next_index = u64();
        ++m_generation;
        if (m_generation == u64()) ++m_generation;
    }

private:
    static bool HasBinding(const DescriptorSetSchema& schema, rstd::uint32_t binding,
                           VkDescriptorType type) {
        for (const auto& candidate : schema.bindings) {
            if (candidate.binding != binding) continue;
            return candidate.descriptor_type == static_cast<rstd::uint32_t>(type) &&
                   candidate.descriptor_count > 0;
        }
        return false;
    }

    auto AllocateSet(const vulkan::Device& device, VkDescriptorSetLayout layout)
        -> Result<vvk::DescriptorSetLease, resource::ResourceError> {
        if (m_pool.is_none()) {
            auto created = CreatePool(device);
            if (created.is_err()) return Err(rstd::move(created).unwrap_err_unchecked());
            m_pool = Some(rstd::move(created).unwrap_unchecked());
        }

        auto allocated = vvk::DescriptorArenaGeneration::Allocate(*m_pool, layout);
        if (! allocated.allocated()) {
            auto created = CreatePool(device);
            if (created.is_err()) return Err(rstd::move(created).unwrap_err_unchecked());
            m_pool    = Some(rstd::move(created).unwrap_unchecked());
            allocated = vvk::DescriptorArenaGeneration::Allocate(*m_pool, layout);
        }
        if (! allocated.allocated()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("allocate descriptor set failed: {}",
                                        static_cast<rstd::int32_t>(allocated.api_result)),
            });
        }
        return Ok(rstd::move(allocated.lease));
    }

    auto CreatePool(const vulkan::Device& device)
        -> Result<Arc<vvk::DescriptorArenaGeneration>, resource::ResourceError> {
        auto sizes = Vec<VkDescriptorPoolSize>::with_capacity(usize(2));
        sizes.push(VkDescriptorPoolSize {
            .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 8192,
        });
        sizes.push(VkDescriptorPoolSize {
            .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 2048,
        });
        auto created = vvk::DescriptorArenaGeneration::Create(
            *device.handle(),
            2048,
            sizes.as_slice(),
            vvk::DescriptorDeviceDispatch::FromDispatch(device.handle().Dispatch()));
        if (! created.created()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("create descriptor pool failed: {}",
                                        static_cast<rstd::int32_t>(created.api_result)),
            });
        }
        return Ok(rstd::move(*created.arena));
    }

    Option<Arc<vvk::DescriptorArenaGeneration>>              m_pool;
    HashMap<DescriptorSetPacketKey, vvk::DescriptorSetLease> m_packets;
    u64                                                      m_generation { 1 };
    u64                                                      m_next_index { 0 };
};

} // namespace vrento::resource_registry
