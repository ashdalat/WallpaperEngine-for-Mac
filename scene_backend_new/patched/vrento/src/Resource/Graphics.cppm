export module vrento.resource_registry:graphics;
import rstd;
import vrento.resource;
import vrento.vulkan;

import :descriptor;
import :resource_key;

using rstd::collections::HashMap;
using rstd::sync::Arc;
using rstd::sync::Weak;

using namespace rstd::prelude;

export namespace vrento::resource_registry
{

using namespace vrento::vulkan;

struct PipelineLayoutRequest {
    Vec<DescriptorSetInfo>   descriptor_sets;
    Vec<VkPushConstantRange> push_constants;

    auto clone() const -> PipelineLayoutRequest {
        auto cloned_sets = Vec<DescriptorSetInfo>::with_capacity(descriptor_sets.len());
        for (const auto& descriptor_set : descriptor_sets) {
            cloned_sets.push(descriptor_set.clone());
        }
        auto cloned_ranges = Vec<VkPushConstantRange>::with_capacity(push_constants.len());
        for (const auto& range : push_constants) {
            auto copied = range;
            cloned_ranges.push(rstd::move(copied));
        }
        return PipelineLayoutRequest {
            .descriptor_sets = rstd::move(cloned_sets),
            .push_constants  = rstd::move(cloned_ranges),
        };
    }
};

struct PipelinePushConstantSchema {
    rstd::uint32_t stage_flags {};
    rstd::uint32_t offset {};
    rstd::uint32_t size {};

    friend bool operator==(const PipelinePushConstantSchema&,
                           const PipelinePushConstantSchema&) = default;
};

} // namespace vrento::resource_registry

export namespace rstd
{

template<>
struct Impl<Copy, vrento::resource_registry::PipelinePushConstantSchema> {};

} // namespace rstd

export namespace vrento::resource_registry
{

struct PipelineLayoutSchema {
    Vec<resource::DescriptorLayoutHandle> descriptor_layouts;
    Vec<PipelinePushConstantSchema>       push_constants;

    auto clone() const -> PipelineLayoutSchema {
        return PipelineLayoutSchema {
            .descriptor_layouts = descriptor_layouts.clone(),
            .push_constants     = push_constants.clone(),
        };
    }

    friend bool operator==(const PipelineLayoutSchema&, const PipelineLayoutSchema&) = default;
};

struct PipelineLayoutResourceEntry {
    resource::PipelineLayoutHandle        handle;
    Vec<resource::DescriptorLayoutHandle> descriptor_layouts;
    Vec<PipelinePushConstantSchema>       push_constants;
    rstd::uint64_t                        push_constant_identity {};
    vvk::PipelineLayout                   layout;
};

struct PipelineLayoutResult {
    resource::PipelineLayoutHandle   handle;
    Arc<PipelineLayoutResourceEntry> physical;
};

} // namespace vrento::resource_registry

export namespace rstd
{

template<>
struct Impl<hash::Hash, vrento::resource_registry::PipelineLayoutSchema>
    : ImplBase<vrento::resource_registry::PipelineLayoutSchema> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        const auto& schema = this->self();
        hash::hash_into(schema.descriptor_layouts.len(), state);
        for (const auto& layout : schema.descriptor_layouts) hash::hash_into(layout, state);
        hash::hash_into(schema.push_constants.len(), state);
        for (const auto& range : schema.push_constants) {
            hash::hash_into(range.stage_flags, state);
            hash::hash_into(range.offset, state);
            hash::hash_into(range.size, state);
        }
    }
};

} // namespace rstd

export namespace vrento::resource_registry
{

using namespace vrento::vulkan;

class PipelineLayoutRegistry {
public:
    auto Ensure(const Device& device, const PipelineLayoutRequest& request,
                DescriptorLayoutRegistry& descriptor_layouts)
        -> Result<PipelineLayoutResult, resource::ResourceError> {
        auto vk_layouts = Vec<VkDescriptorSetLayout>::with_capacity(request.descriptor_sets.len());
        auto layout_handles =
            Vec<resource::DescriptorLayoutHandle>::with_capacity(request.descriptor_sets.len());
        for (const auto& descriptor_set : request.descriptor_sets) {
            auto ensured = descriptor_layouts.Ensure(device, descriptor_set);
            if (ensured.is_err()) return Err(rstd::move(ensured).unwrap_err_unchecked());
            auto handle   = rstd::move(ensured).unwrap_unchecked();
            auto resolved = descriptor_layouts.Resolve(handle);
            if (resolved.is_none()) {
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::MissingDefinition,
                    .message = rstd::format("descriptor layout unavailable"),
                });
            }
            layout_handles.push(resource::DescriptorLayoutHandle {
                .index      = handle.index,
                .generation = handle.generation,
            });
            auto vk_layout = *(**resolved).layout;
            vk_layouts.push(rstd::move(vk_layout));
        }

        auto push_constants =
            Vec<PipelinePushConstantSchema>::with_capacity(request.push_constants.len());
        for (const auto& range : request.push_constants) {
            if (range.stageFlags == 0 || range.size == 0) {
                return Err(resource::ResourceError {
                    .kind    = resource::ResourceErrorKind::MissingDefinition,
                    .message = rstd::format("invalid pipeline push constant range"),
                });
            }
            push_constants.push(PipelinePushConstantSchema {
                .stage_flags = range.stageFlags,
                .offset      = range.offset,
                .size        = range.size,
            });
        }
        rstd::slice_::sort_unstable_by(push_constants.as_mut_slice().as_mut_ref(),
                                       [](const auto& lhs, const auto& rhs) {
                                           if (lhs.offset != rhs.offset)
                                               return lhs.offset < rhs.offset;
                                           if (lhs.size != rhs.size) return lhs.size < rhs.size;
                                           return lhs.stage_flags < rhs.stage_flags;
                                       });

        PipelineLayoutSchema schema {
            .descriptor_layouts = rstd::move(layout_handles),
            .push_constants     = rstd::move(push_constants),
        };
        if (auto existing = m_handles.get(schema); existing.is_some()) {
            auto physical = m_entries.get(**existing);
            if (physical.is_some()) {
                return Ok(PipelineLayoutResult {
                    .handle   = **existing,
                    .physical = (**physical).clone(),
                });
            }
        }

        auto vk_push_constants =
            Vec<VkPushConstantRange>::with_capacity(schema.push_constants.len());
        for (const auto& range : schema.push_constants) {
            vk_push_constants.push(VkPushConstantRange {
                .stageFlags = range.stage_flags,
                .offset     = range.offset,
                .size       = range.size,
            });
        }
        VkPipelineLayoutCreateInfo create_info {
            .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext          = nullptr,
            .flags          = {},
            .setLayoutCount = static_cast<rstd::uint32_t>(vk_layouts.len().to_primitive()),
            .pSetLayouts    = vk_layouts.data(),
            .pushConstantRangeCount =
                static_cast<rstd::uint32_t>(vk_push_constants.len().to_primitive()),
            .pPushConstantRanges = vk_push_constants.data(),
        };
        vvk::PipelineLayout layout;
        if (device.handle().CreatePipelineLayout(create_info, layout) != VK_SUCCESS) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("create pipeline layout failed"),
            });
        }

        auto handle                 = NextHandle();
        auto push_constant_identity = PushConstantIdentity(schema.push_constants.as_slice());
        auto entry = Arc<PipelineLayoutResourceEntry>::make(PipelineLayoutResourceEntry {
            .handle                 = handle,
            .descriptor_layouts     = schema.descriptor_layouts.clone(),
            .push_constants         = schema.push_constants.clone(),
            .push_constant_identity = push_constant_identity,
            .layout                 = rstd::move(layout),
        });
        (void)m_handles.insert(schema.clone(), handle);
        (void)m_entries.insert(handle, entry.clone());
        return Ok(PipelineLayoutResult {
            .handle   = handle,
            .physical = rstd::move(entry),
        });
    }

    auto Resolve(resource::PipelineLayoutHandle handle) const
        -> Option<Arc<PipelineLayoutResourceEntry>> {
        auto entry = m_entries.get(handle);
        if (entry.is_none()) return None();
        return Some((**entry).clone());
    }

    void PruneExpired() {
        m_entries.retain(
            [&](const resource::PipelineLayoutHandle&, Arc<PipelineLayoutResourceEntry>& entry) {
                if (entry.strong_count() > usize(1)) return true;
                auto schema = PipelineLayoutSchema {
                    .descriptor_layouts = entry->descriptor_layouts.clone(),
                    .push_constants     = entry->push_constants.clone(),
                };
                (void)m_handles.remove(schema);
                return false;
            });
    }

    void Reset() {
        m_handles.clear();
        m_entries.clear();
        m_next_index = u64();
        ++m_generation;
        if (m_generation == u64()) ++m_generation;
    }

    auto Size() const noexcept -> usize { return m_entries.len(); }

private:
    static auto PushConstantIdentity(slice<PipelinePushConstantSchema> ranges) -> rstd::uint64_t {
        rstd::uint64_t identity = 1469598103934665603ULL;
        auto           mix      = [&](rstd::uint32_t value) {
            identity ^= value;
            identity *= 1099511628211ULL;
        };
        for (const auto& range : ranges) {
            mix(range.stage_flags);
            mix(range.offset);
            mix(range.size);
        }
        return identity;
    }

    auto NextHandle() -> resource::PipelineLayoutHandle {
        return { .index = m_next_index++, .generation = m_generation };
    }

    HashMap<PipelineLayoutSchema, resource::PipelineLayoutHandle>             m_handles;
    HashMap<resource::PipelineLayoutHandle, Arc<PipelineLayoutResourceEntry>> m_entries;
    u64                                                                       m_generation { 1 };
    u64                                                                       m_next_index { 0 };
};

struct PipelineResourceEntry {
    PipelineParameters               pipeline;
    Arc<PipelineLayoutResourceEntry> layout;
};

struct PipelineResourceResult {
    Arc<PipelineResourceEntry> pipeline;
    resource::PipelineHandle   handle;
    resource::RenderPassHandle render_pass;
    Arc<vvk::RenderPass>       render_pass_physical;
    PipelineCacheKey           cache_key;
    RenderPassCacheKey         render_pass_key;
    bool                       cache_hit { false };
    u64                        cache_observed_count { 0 };
    bool                       render_pass_cache_hit { false };
    u64                        render_pass_cache_observed_count { 0 };
};

struct FramebufferResourceResult {
    Arc<vvk::Framebuffer>       framebuffer;
    resource::FramebufferHandle handle;
    FramebufferCacheKey         cache_key;
    bool                        cache_hit { false };
    u64                         cache_observed_count { 0 };
};

class PipelineCacheDiagnostics {
public:
    PipelineCacheProbe Record(PipelineCacheKey key) {
        auto count = m_seen.get_mut(key);
        bool hit   = count.is_some();
        u64  observed_count { 1 };
        if (count.is_some()) {
            observed_count = ++**count;
        } else {
            (void)m_seen.insert(key.clone(), observed_count);
        }
        return PipelineCacheProbe {
            .key            = rstd::move(key),
            .hit            = hit,
            .observed_count = observed_count,
        };
    }

    void Reset() { m_seen.clear(); }

private:
    HashMap<PipelineCacheKey, u64, rstd::hash::RandomState, PipelineCacheKeyEqual> m_seen;
};

class FramebufferCacheDiagnostics {
public:
    struct Probe {
        FramebufferCacheKey key;
        bool                hit { false };
        u64                 observed_count { 0 };
    };

    Probe Record(FramebufferCacheKey key) {
        auto count = m_seen.get_mut(key);
        bool hit   = count.is_some();
        u64  observed_count { 1 };
        if (count.is_some()) {
            observed_count = ++**count;
        } else {
            (void)m_seen.insert(key.clone(), observed_count);
        }
        return Probe {
            .key            = rstd::move(key),
            .hit            = hit,
            .observed_count = observed_count,
        };
    }

    void Reset() { m_seen.clear(); }

private:
    HashMap<FramebufferCacheKey, u64, rstd::hash::RandomState, FramebufferCacheKeyEqual> m_seen;
};

class FramebufferRegistry {
public:
    auto Ensure(const Device& device, const FramebufferResourceRequest& request)
        -> Option<FramebufferResourceResult> {
        if (request.render_pass == VK_NULL_HANDLE || request.attachments.is_empty()) {
            return None();
        }

        auto desc = MakeFramebufferResourceDesc(request);
        auto key  = MakeFramebufferCacheKey(desc);
        auto slot = m_entries.get_mut(key);
        if (slot.is_some()) {
            ++(**slot).observed_count;
            return Some(FramebufferResourceResult {
                .framebuffer          = (**slot).framebuffer.clone(),
                .handle               = (**slot).handle,
                .cache_key            = rstd::move(key),
                .cache_hit            = true,
                .cache_observed_count = (**slot).observed_count,
            });
        }

        Vec<VkImageView> attachment_views;
        attachment_views.reserve(desc.attachments.len());
        for (const auto& attachment : desc.attachments)
            attachment_views.push(VkImageView(attachment.view));

        VkFramebufferCreateInfo info {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = nullptr,
            .renderPass      = desc.render_pass,
            .attachmentCount = static_cast<rstd::uint32_t>(attachment_views.len().to_primitive()),
            .pAttachments    = attachment_views.data(),
            .width           = desc.extent.width,
            .height          = desc.extent.height,
            .layers          = desc.layers,
        };
        vvk::Framebuffer framebuffer;
        if (device.handle().CreateFramebuffer(info, framebuffer) != VK_SUCCESS) {
            return None();
        }
        auto shared = Arc<vvk::Framebuffer>::make(rstd::move(framebuffer));
        auto handle = NextHandle();
        (void)m_handles.insert(handle, shared.downgrade());
        (void)m_entries.insert(key.clone(),
                               Entry {
                                   .framebuffer    = shared.clone(),
                                   .handle         = handle,
                                   .observed_count = u64(1),
                               });
        return Some(FramebufferResourceResult {
            .framebuffer          = rstd::move(shared),
            .handle               = handle,
            .cache_key            = rstd::move(key),
            .cache_hit            = false,
            .cache_observed_count = u64(1),
        });
    }

    void PruneExpired() {
        m_entries.retain([&](const FramebufferCacheKey&, Entry& entry) {
            if (entry.framebuffer.strong_count() > usize(1)) return true;
            (void)m_handles.remove(entry.handle);
            return false;
        });
    }

    void Reset() {
        m_entries.clear();
        m_handles.clear();
        m_next_index = u64();
        ++m_generation;
        if (m_generation == u64()) ++m_generation;
    }

    auto Resolve(resource::FramebufferHandle handle) const -> Option<Arc<vvk::Framebuffer>> {
        auto entry = m_handles.get(handle);
        if (entry.is_none()) return None();
        auto resource = (**entry).upgrade();
        if (! resource) return None();
        return Some(rstd::move(resource));
    }

    usize entryCount() const { return m_entries.len(); }

private:
    struct Entry {
        Arc<vvk::Framebuffer>       framebuffer;
        resource::FramebufferHandle handle;
        u64                         observed_count { 0 };
    };

    auto NextHandle() -> resource::FramebufferHandle {
        return { .index = m_next_index++, .generation = m_generation };
    }

    using HandleMap = HashMap<resource::FramebufferHandle, Weak<vvk::Framebuffer>>;

    HashMap<FramebufferCacheKey, Entry, rstd::hash::RandomState, FramebufferCacheKeyEqual>
              m_entries;
    u64       m_generation { 1 };
    u64       m_next_index { 0 };
    HandleMap m_handles;
};

inline bool HasPipelineResources(const PipelineParameters& pipeline) {
    return static_cast<bool>(pipeline.handle) || pipeline.layout != VK_NULL_HANDLE;
}

inline bool HasPipelineResources(const PipelineResourceEntry& entry) {
    return HasPipelineResources(entry.pipeline);
}

struct RenderPassResourceResult {
    Arc<vvk::RenderPass>       render_pass;
    resource::RenderPassHandle handle;
    RenderPassCacheKey         cache_key;
    bool                       cache_hit { false };
    u64                        cache_observed_count { 0 };
};

class RenderPassRegistry {
public:
    auto Ensure(const Device& device, const RenderPassResourceDesc& desc)
        -> Option<RenderPassResourceResult> {
        auto key  = MakeRenderPassCacheKey(desc);
        auto slot = m_entries.get_mut(key);
        if (slot.is_some()) {
            ++(**slot).observed_count;
            return Some(RenderPassResourceResult {
                .render_pass          = (**slot).render_pass.clone(),
                .handle               = (**slot).handle,
                .cache_key            = rstd::move(key),
                .cache_hit            = true,
                .cache_observed_count = (**slot).observed_count,
            });
        }

        auto created = CreateRenderPass(device, desc);
        if (created.is_none()) return None();
        auto shared = Arc<vvk::RenderPass>::make(rstd::move(*created));
        auto handle = NextHandle();
        (void)m_handles.insert(handle, shared.downgrade());
        (void)m_entries.insert(key.clone(),
                               Entry {
                                   .render_pass    = shared.clone(),
                                   .handle         = handle,
                                   .observed_count = u64(1),
                               });
        return Some(RenderPassResourceResult {
            .render_pass          = rstd::move(shared),
            .handle               = handle,
            .cache_key            = rstd::move(key),
            .cache_hit            = false,
            .cache_observed_count = u64(1),
        });
    }

    auto Ensure(const Device& device, const PipelineResourceRequest& request)
        -> Option<RenderPassResourceResult> {
        return Ensure(device, MakeRenderPassResourceDesc(request));
    }

    void PruneExpired() {
        m_entries.retain([&](const RenderPassCacheKey&, Entry& entry) {
            if (entry.render_pass.strong_count() > usize(1)) return true;
            (void)m_handles.remove(entry.handle);
            return false;
        });
    }

    void Reset() {
        m_entries.clear();
        m_handles.clear();
        m_next_index = u64();
        ++m_generation;
        if (m_generation == u64()) ++m_generation;
    }

    auto Resolve(resource::RenderPassHandle handle) const -> Option<Arc<vvk::RenderPass>> {
        auto entry = m_handles.get(handle);
        if (entry.is_none()) return None();
        auto resource = (**entry).upgrade();
        if (! resource) return None();
        return Some(rstd::move(resource));
    }

    usize entryCount() const { return m_entries.len(); }

private:
    struct Entry {
        Arc<vvk::RenderPass>       render_pass;
        resource::RenderPassHandle handle;
        u64                        observed_count { 0 };
    };

    auto NextHandle() -> resource::RenderPassHandle {
        return { .index = m_next_index++, .generation = m_generation };
    }

    using HandleMap = HashMap<resource::RenderPassHandle, Weak<vvk::RenderPass>>;

    static auto CreateRenderPass(const Device& device, const RenderPassResourceDesc& desc)
        -> Option<vvk::RenderPass> {
        const bool              has_color   = desc.has_color_attachment;
        const bool              has_resolve = has_color && desc.has_resolve_attachment;
        VkAttachmentDescription color {
            .format         = desc.color_format,
            .samples        = desc.samples,
            .loadOp         = desc.color_load_op,
            .storeOp        = desc.color_store_op,
            .stencilLoadOp  = desc.color_stencil_load_op,
            .stencilStoreOp = desc.color_stencil_store_op,
            .initialLayout  = desc.color_initial_layout,
            .finalLayout    = has_resolve ? desc.color_attachment_layout : desc.color_final_layout,
        };

        VkAttachmentDescription resolve {
            .format         = desc.color_format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = desc.resolve_load_op,
            .storeOp        = desc.resolve_store_op,
            .stencilLoadOp  = desc.resolve_stencil_load_op,
            .stencilStoreOp = desc.resolve_stencil_store_op,
            .initialLayout  = desc.resolve_initial_layout,
            .finalLayout    = desc.resolve_final_layout,
        };

        VkAttachmentDescription depth {
            .format         = desc.depth_format,
            .samples        = desc.samples,
            .loadOp         = desc.depth_load_op,
            .storeOp        = desc.depth_store_op,
            .stencilLoadOp  = desc.depth_stencil_load_op,
            .stencilStoreOp = desc.depth_stencil_store_op,
            .initialLayout  = desc.depth_initial_layout,
            .finalLayout    = desc.depth_final_layout,
        };

        VkAttachmentReference color_ref {
            .attachment = 0,
            .layout     = desc.color_attachment_layout,
        };
        VkAttachmentReference resolve_ref {
            .attachment = 1,
            .layout     = desc.resolve_attachment_layout,
        };
        VkAttachmentReference depth_ref {
            .attachment = has_color ? (has_resolve ? 2u : 1u) : 0u,
            .layout     = desc.depth_attachment_layout,
        };

        Vec<VkAttachmentDescription> attachments;
        attachments.reserve(usize(3));
        if (has_color) attachments.push(rstd::move(color));
        if (has_resolve) attachments.push(rstd::move(resolve));
        if (desc.has_depth_attachment) attachments.push(rstd::move(depth));

        VkSubpassDescription subpass {
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount    = has_color ? 1u : 0u,
            .pColorAttachments       = has_color ? &color_ref : nullptr,
            .pResolveAttachments     = has_resolve ? &resolve_ref : nullptr,
            .pDepthStencilAttachment = desc.has_depth_attachment ? &depth_ref : nullptr,
        };

        VkSubpassDependency dependency {
            .srcSubpass   = VK_SUBPASS_EXTERNAL,
            .dstSubpass   = 0,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        };

        VkRenderPassCreateInfo create {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = static_cast<rstd::uint32_t>(attachments.len().to_primitive()),
            .pAttachments    = attachments.data(),
            .subpassCount    = 1,
            .pSubpasses      = &subpass,
            .dependencyCount = 1,
            .pDependencies   = &dependency,
        };
        vvk::RenderPass pass;
        if (device.handle().CreateRenderPass(create, pass) != VK_SUCCESS) return None();
        return Some(rstd::move(pass));
    }

    HashMap<RenderPassCacheKey, Entry, rstd::hash::RandomState, RenderPassCacheKeyEqual> m_entries;
    u64       m_generation { 1 };
    u64       m_next_index { 0 };
    HandleMap m_handles;
};

class PipelineRegistry {
public:
    auto Ensure(const Device& device, PipelineResourceRequest request,
                RenderPassRegistry& render_pass_cache, PipelineLayoutRegistry& pipeline_layouts)
        -> Option<PipelineResourceResult> {
        auto desc = MakePipelineResourceDesc(request);
        auto key  = MakePipelineCacheKey(desc);
        auto slot = m_entries.get_mut(key);
        if (slot.is_some()) {
            ++(**slot).observed_count;
            return Some(PipelineResourceResult {
                .pipeline                         = (**slot).pipeline.clone(),
                .handle                           = (**slot).handle,
                .render_pass                      = (**slot).render_pass,
                .render_pass_physical             = (**slot).render_pass_physical.clone(),
                .cache_key                        = rstd::move(key),
                .render_pass_key                  = (**slot).render_pass_key.clone(),
                .cache_hit                        = true,
                .cache_observed_count             = (**slot).observed_count,
                .render_pass_cache_hit            = true,
                .render_pass_cache_observed_count = (**slot).observed_count,
            });
        }

        auto render_pass = render_pass_cache.Ensure(device, desc.render_pass);
        if (render_pass.is_none()) return None();
        auto pipeline_layout = pipeline_layouts.Resolve(desc.pipeline_layout);
        if (pipeline_layout.is_none()) return None();

        GraphicsPipeline   pipeline;
        PipelineParameters pipeline_parameters;
        pipeline.toDefault();
        pipeline.depth          = desc.depth;
        pipeline.raster         = desc.raster;
        pipeline.depth_clip     = desc.depth_clip;
        pipeline.multisample    = desc.multisample;
        const auto color_blends = desc.render_pass.has_color_attachment
                                      ? slice<VkPipelineColorBlendAttachmentState>::from_raw_parts(
                                            &desc.color_blend, usize(1))
                                      : slice<VkPipelineColorBlendAttachmentState> {};
        pipeline.setColorBlendStates(color_blends)
            .setCreateInfoOptions(desc.create_flags, desc.subpass)
            .setColorBlendOptions(desc.color_blend_flags, desc.blend_constants)
            .setLogicOp(desc.logic_op_enable, desc.logic_op)
            .setTopology(desc.topology)
            .setPrimitiveRestartEnable(desc.primitive_restart_enable)
            .setViewportScissorCount(desc.viewport_count, desc.scissor_count)
            .setDynamicStates(desc.dynamic_states.as_slice())
            .addInputBindingDescription(desc.vertex_bindings.as_slice())
            .addInputAttributeDescription(desc.vertex_attrs.as_slice());
        for (auto& spv : desc.shader_stages) {
            pipeline.addStage(Box<ShaderSpv>::make(rstd::move(spv)));
        }
        if (! pipeline.create(device,
                              **render_pass->render_pass,
                              *(**pipeline_layout).layout,
                              pipeline_parameters)) {
            return None();
        }
        auto entry  = Arc<PipelineResourceEntry>::make(PipelineResourceEntry {
            .pipeline = rstd::move(pipeline_parameters),
            .layout   = (*pipeline_layout).clone(),
        });
        auto handle = NextHandle();
        (void)m_handles.insert(handle, entry.downgrade());
        (void)m_entries.insert(key.clone(),
                               Entry {
                                   .pipeline             = entry.clone(),
                                   .handle               = handle,
                                   .render_pass          = render_pass->handle,
                                   .render_pass_physical = render_pass->render_pass.clone(),
                                   .render_pass_key      = render_pass->cache_key.clone(),
                                   .observed_count       = u64(1),
                               });
        return Some(PipelineResourceResult {
            .pipeline                         = rstd::move(entry),
            .handle                           = handle,
            .render_pass                      = render_pass->handle,
            .render_pass_physical             = render_pass->render_pass.clone(),
            .cache_key                        = rstd::move(key),
            .render_pass_key                  = render_pass->cache_key.clone(),
            .cache_hit                        = false,
            .cache_observed_count             = u64(1),
            .render_pass_cache_hit            = render_pass->cache_hit,
            .render_pass_cache_observed_count = render_pass->cache_observed_count,
        });
    }

    void PruneExpired() {
        m_entries.retain([&](const PipelineCacheKey&, Entry& entry) {
            if (entry.pipeline.strong_count() > usize(1)) return true;
            (void)m_handles.remove(entry.handle);
            return false;
        });
    }

    void Reset() {
        m_entries.clear();
        m_handles.clear();
        m_next_index = u64();
        ++m_generation;
        if (m_generation == u64()) ++m_generation;
    }

    auto Resolve(resource::PipelineHandle handle) const -> Option<Arc<PipelineResourceEntry>> {
        auto entry = m_handles.get(handle);
        if (entry.is_none()) return None();
        auto resource = (**entry).upgrade();
        if (! resource) return None();
        return Some(rstd::move(resource));
    }

    usize entryCount() const { return m_entries.len(); }

private:
    struct Entry {
        Arc<PipelineResourceEntry> pipeline;
        resource::PipelineHandle   handle;
        resource::RenderPassHandle render_pass;
        Arc<vvk::RenderPass>       render_pass_physical;
        RenderPassCacheKey         render_pass_key;
        u64                        observed_count { 0 };
    };

    auto NextHandle() -> resource::PipelineHandle {
        return { .index = m_next_index++, .generation = m_generation };
    }

    using HandleMap = HashMap<resource::PipelineHandle, Weak<PipelineResourceEntry>>;

    HashMap<PipelineCacheKey, Entry, rstd::hash::RandomState, PipelineCacheKeyEqual> m_entries;
    u64       m_generation { 1 };
    u64       m_next_index { 0 };
    HandleMap m_handles;
};

using PipelineResourceCache    = PipelineRegistry;
using RenderPassResourceCache  = RenderPassRegistry;
using FramebufferResourceCache = FramebufferRegistry;

class FramebufferResourceSystem {
public:
    explicit FramebufferResourceSystem(const Device& device, FramebufferResourceCache& cache,
                                       FramebufferCacheDiagnostics& diagnostics)
        : m_device(ref<Device>::from_raw_parts(rstd::addressof(device))),
          m_cache(mut_ref<FramebufferResourceCache>::from_raw_parts(rstd::addressof(cache))),
          m_diagnostics(
              mut_ref<FramebufferCacheDiagnostics>::from_raw_parts(rstd::addressof(diagnostics))) {}

    auto CreateFramebuffer(const FramebufferResourceRequest& request)
        -> Option<FramebufferResourceResult> {
        if (request.render_pass == VK_NULL_HANDLE || request.attachments.is_empty()) {
            return None();
        }
        m_diagnostics->Record(MakeFramebufferCacheKey(request));
        return m_cache->Ensure(*m_device, request);
    }

private:
    ref<Device>                          m_device;
    mut_ref<FramebufferResourceCache>    m_cache;
    mut_ref<FramebufferCacheDiagnostics> m_diagnostics;
};

class PipelineResourceSystem {
public:
    explicit PipelineResourceSystem(const Device& device, PipelineLayoutRegistry& pipeline_layouts,
                                    PipelineResourceCache&   pipeline_cache,
                                    RenderPassResourceCache& render_pass_cache)
        : m_device(ref<Device>::from_raw_parts(rstd::addressof(device))),
          m_pipeline_layouts(rstd::mut_ref<PipelineLayoutRegistry>::from_raw_parts(
              rstd::addressof(pipeline_layouts))),
          m_pipeline_cache(
              mut_ref<PipelineResourceCache>::from_raw_parts(rstd::addressof(pipeline_cache))),
          m_render_pass_cache(mut_ref<RenderPassResourceCache>::from_raw_parts(
              rstd::addressof(render_pass_cache))) {}

    auto CreateGraphicsPipeline(PipelineResourceRequest request) -> Option<PipelineResourceResult> {
        return m_pipeline_cache->Ensure(
            *m_device, rstd::move(request), *m_render_pass_cache, *m_pipeline_layouts);
    }

private:
    ref<Device>                           m_device;
    rstd::mut_ref<PipelineLayoutRegistry> m_pipeline_layouts;
    mut_ref<PipelineResourceCache>        m_pipeline_cache;
    mut_ref<RenderPassResourceCache>      m_render_pass_cache;
};

} // namespace vrento::resource_registry
