module;

#include <span>

export module vrento.resource_registry:system;
import rstd;
import vrento.resource;
import vrento.vulkan;

import :owner;
import :prepared;

export namespace vrento::resource_registry
{

using namespace rstd::prelude;

struct PipelinePreparation {
    resource::PipelineHandle   resource;
    resource::RenderPassHandle render_pass;
    vulkan::PipelineCacheKey   cache_key;
    vulkan::RenderPassCacheKey render_pass_key;
    bool                       cache_hit { false };
    u64                        cache_observed_count { 0 };
    bool                       render_pass_cache_hit { false };
    u64                        render_pass_cache_observed_count { 0 };
};

struct FramebufferPreparation {
    resource::FramebufferHandle resource;
    vulkan::FramebufferCacheKey cache_key;
    bool                        cache_hit { false };
    u64                         cache_observed_count { 0 };
};

struct RenderPassPreparation {
    resource::RenderPassHandle resource;
    vulkan::RenderPassCacheKey cache_key;
    bool                       cache_hit { false };
    u64                        cache_observed_count { 0 };
};

struct GraphicsResourcePreparer {
    using Trait                  = GraphicsResourcePreparer;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = GraphicsResourcePreparer;

        auto PreparePipeline(resource::PipelineUseHandle   pipeline_use,
                             resource::RenderPassUseHandle render_pass_use,
                             const vulkan::Device& device, vulkan::PipelineResourceRequest request)
            -> Result<PipelinePreparation, resource::ResourceError> {
            return rstd::trait_call<0>(
                this, pipeline_use, render_pass_use, device, rstd::move(request));
        }

        auto PrepareFramebuffer(resource::FramebufferUseHandle         framebuffer_use,
                                resource::RenderPassUseHandle          render_pass_use,
                                const vulkan::Device&                  device,
                                Vec<vulkan::FramebufferAttachmentDesc> attachments,
                                VkExtent2D                             extent)
            -> Result<FramebufferPreparation, resource::ResourceError> {
            return rstd::trait_call<1>(
                this, framebuffer_use, render_pass_use, device, rstd::move(attachments), extent);
        }

        auto PrepareRenderPass(resource::RenderPassUseHandle use, const vulkan::Device& device,
                               const vulkan::RenderPassResourceDesc& desc)
            -> Result<RenderPassPreparation, resource::ResourceError> {
            return rstd::trait_call<2>(this, use, device, desc);
        }

        auto PrepareDescriptor(const vulkan::Device&          device,
                               resource::PipelineLayoutHandle pipeline_layout, u32 set_index,
                               slice<DescriptorImageBinding>  images,
                               slice<DescriptorBufferBinding> buffers, DescriptorBindingReuse reuse)
            -> Result<resource::DescriptorBindingHandle, resource::ResourceError> {
            return rstd::trait_call<3>(
                this, device, pipeline_layout, set_index, images, buffers, reuse);
        }

        auto UpdateDescriptorImages(resource::DescriptorBindingHandle handle,
                                    slice<DescriptorImageBinding>     images)
            -> Result<empty, resource::ResourceError> {
            return rstd::trait_call<4>(this, handle, images);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::PreparePipeline, &T::PrepareFramebuffer, &T::PrepareRenderPass,
                             &T::PrepareDescriptor, &T::UpdateDescriptorImages>;
};

struct ExternalResourcePreparer {
    using Trait                  = ExternalResourcePreparer;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = ExternalResourcePreparer;

        auto PrepareExternal(resource::ExternalUseHandle       external_use,
                             resource::TextureUseHandle        source_use,
                             const vulkan::DeviceCapabilities& capabilities,
                             FrameSurfaceLease lease, rstd::uint32_t graphics_queue_family)
            -> Result<empty, resource::ResourceError> {
            return rstd::trait_call<0>(this,
                                       external_use,
                                       source_use,
                                       capabilities,
                                       rstd::move(lease),
                                       graphics_queue_family);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::PrepareExternal>;
};

class RenderResourceSystem {
public:
    bool Initialize(const vulkan::Device& device) { return m_registries.Initialize(device); }

    void Reset() {
        m_prepare_rollback = None();
        m_prepared         = PreparedResourceTable {};
        m_registries.Reset();
    }

    bool Ready() const noexcept { return m_registries.Ready(); }

    auto
    PreparePlan(const resource::ResourcePlan& plan, ResourceContentProviders providers,
                resource::ResourcePlanSections sections = resource::ResourcePlanAll,
                Option<mut_ref<dyn<resource::TexturePrepareObserver>>> texture_observer = None())
        -> Result<empty, resource::ResourceError> {
        auto started = BeginPreparePlan(plan, rstd::move(providers), sections, texture_observer);
        if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
        auto session = rstd::move(started).unwrap_unchecked();
        while (true) {
            auto progress = ContinuePreparePlan(session, texture_observer);
            if (progress.is_err()) return Err(rstd::move(progress).unwrap_err_unchecked());
            if (progress.unwrap_unchecked() == ResourcePrepareProgress::Complete) break;
        }
        CommitPreparePlan();
        return Ok(empty {});
    }

    auto BeginPreparePlan(const resource::ResourcePlan& plan, ResourceContentProviders providers,
                          resource::ResourcePlanSections sections = resource::ResourcePlanAll,
                          Option<mut_ref<dyn<resource::TexturePrepareObserver>>> texture_observer =
                              None()) -> Result<ResourcePrepareSession, resource::ResourceError> {
        if (m_prepare_rollback.is_some()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("resource prepare transaction is already active"),
            });
        }
        if (! m_registries.TextureEntries().BeginPrepareTransaction()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("texture prepare transaction is already active"),
            });
        }
        vulkan::ImagePrepareContext image_context(m_registries.Textures(),
                                                  m_registries.ImageUploads());
        auto image_backend  = dyn<vulkan::ImagePrepareBackend>::from_ref(image_context);
        auto buffer_backend = dyn<vulkan::BufferBackend>::from_ref(m_registries.BufferManager());
        ResourcePrepareService service(m_registries.TextureEntries(),
                                       Some(image_backend.as_mut_ref()),
                                       m_registries.Buffers(),
                                       buffer_backend.as_mut_ref(),
                                       m_registries.Shaders());
        auto started = service.Begin(plan, rstd::move(providers), sections, texture_observer);
        if (started.is_err()) {
            AbortPreparePlan();
            return Err(rstd::move(started).unwrap_err_unchecked());
        }
        return Ok(rstd::move(started).unwrap_unchecked());
    }

    auto ContinuePreparePlan(
        ResourcePrepareSession&                                session,
        Option<mut_ref<dyn<resource::TexturePrepareObserver>>> texture_observer = None())
        -> Result<ResourcePrepareProgress, resource::ResourceError> {
        vulkan::ImagePrepareContext image_context(m_registries.Textures(),
                                                  m_registries.ImageUploads());
        auto image_backend  = dyn<vulkan::ImagePrepareBackend>::from_ref(image_context);
        auto buffer_backend = dyn<vulkan::BufferBackend>::from_ref(m_registries.BufferManager());
        ResourcePrepareService service(m_registries.TextureEntries(),
                                       Some(image_backend.as_mut_ref()),
                                       m_registries.Buffers(),
                                       buffer_backend.as_mut_ref(),
                                       m_registries.Shaders());
        auto                   progress = service.Continue(session, texture_observer);
        if (progress.is_err()) {
            AbortPreparePlan();
            return Err(rstd::move(progress).unwrap_err_unchecked());
        }
        if (progress.unwrap_unchecked() == ResourcePrepareProgress::BatchReady) {
            return Ok(ResourcePrepareProgress::BatchReady);
        }

        const auto& plan     = session.Plan();
        const auto  sections = session.Sections();
        auto        next     = rstd::move(session).TakeTable();
        if (resource::ResourcePlanIncludes(sections, resource::ResourcePlanTextures) &&
            ! m_registries.States().Compile(plan, next)) {
            AbortPreparePlan();
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("compile resource state plan failed"),
            });
        }
        auto previous = m_prepared.clone();
        next.CarryForward(rstd::move(m_prepared), sections);
        m_prepared         = rstd::move(next);
        m_prepare_rollback = Some(PrepareRollback {
            .table = rstd::move(previous),
        });
        return Ok(ResourcePrepareProgress::Complete);
    }

    void AbortPreparePlan() {
        m_registries.ImageUploads().DiscardPendingUploads();
        m_registries.TextureEntries().DiscardPendingUploads();
        m_registries.TextureEntries().AbortPrepareTransaction();
        auto rollback = m_prepare_rollback.take();
        if (rollback.is_none()) return;
        m_prepared = rstd::move(rollback->table);
    }

    void CommitPreparePlan() {
        m_prepare_rollback = None();
        m_registries.TextureEntries().CommitPrepareTransaction();
        auto active = m_prepared.TextureResources();
        m_registries.TextureEntries().RetainActive(active.as_slice());
    }

    auto Prepared() const -> const PreparedResourceTable& { return m_prepared; }
    auto States() -> ResourceStateTracker& { return m_registries.States(); }

    void RemovePreparedGraphics(slice<resource::PipelineUseHandle>       pipelines,
                                slice<resource::RenderPassUseHandle>     render_passes,
                                slice<resource::FramebufferUseHandle>    framebuffers,
                                slice<resource::DescriptorBindingHandle> descriptors,
                                slice<resource::ExternalUseHandle>       externals) {
        for (usize index {}; index < pipelines.len(); ++index) m_prepared.Remove(pipelines[index]);
        for (usize index {}; index < render_passes.len(); ++index) {
            m_prepared.Remove(render_passes[index]);
        }
        for (usize index {}; index < framebuffers.len(); ++index) {
            m_prepared.Remove(framebuffers[index]);
        }
        for (usize index {}; index < descriptors.len(); ++index) {
            m_prepared.Remove(descriptors[index]);
        }
        for (usize index {}; index < externals.len(); ++index) {
            m_prepared.Remove(externals[index]);
        }
    }

    void RemovePreparedDescriptor(resource::DescriptorBindingHandle descriptor) {
        m_prepared.Remove(descriptor);
    }

    auto PreparePipelineLayout(const vulkan::Device& device, const PipelineLayoutRequest& request)
        -> Result<resource::PipelineLayoutHandle, resource::ResourceError> {
        auto prepared = m_registries.PipelineLayouts().Ensure(
            device, request, m_registries.DescriptorLayouts());
        if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err_unchecked());
        return Ok(rstd::move(prepared).unwrap_unchecked().handle);
    }

    auto PreparePipeline(resource::PipelineUseHandle   pipeline_use,
                         resource::RenderPassUseHandle render_pass_use,
                         const vulkan::Device& device, vulkan::PipelineResourceRequest request)
        -> Result<PipelinePreparation, resource::ResourceError> {
        PipelineResourceSystem system(device,
                                      m_registries.PipelineLayouts(),
                                      m_registries.PipelineCache(),
                                      m_registries.RenderPassCache());
        auto                   result = system.CreateGraphicsPipeline(rstd::move(request));
        if (result.is_none()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("prepare graphics pipeline failed"),
            });
        }
        if (! m_prepared.Insert(PreparedRenderPass {
                .use       = render_pass_use,
                .resource  = result->render_pass,
                .cache_key = result->render_pass_key.clone(),
                .physical  = result->render_pass_physical.clone(),
            }) ||
            ! m_prepared.Insert(PreparedPipeline {
                .use         = pipeline_use,
                .resource    = result->handle,
                .render_pass = result->render_pass,
                .physical    = result->pipeline.clone(),
            })) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("duplicate prepared graphics use"),
            });
        }
        return Ok(PipelinePreparation {
            .resource                         = result->handle,
            .render_pass                      = result->render_pass,
            .cache_key                        = rstd::move(result->cache_key),
            .render_pass_key                  = rstd::move(result->render_pass_key),
            .cache_hit                        = result->cache_hit,
            .cache_observed_count             = result->cache_observed_count,
            .render_pass_cache_hit            = result->render_pass_cache_hit,
            .render_pass_cache_observed_count = result->render_pass_cache_observed_count,
        });
    }

    auto PrepareFramebuffer(resource::FramebufferUseHandle         framebuffer_use,
                            resource::RenderPassUseHandle          render_pass_use,
                            const vulkan::Device&                  device,
                            Vec<vulkan::FramebufferAttachmentDesc> attachments, VkExtent2D extent)
        -> Result<FramebufferPreparation, resource::ResourceError> {
        auto render_pass = m_prepared.Resolve(render_pass_use);
        if (render_pass.is_none()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::MissingDefinition,
                .message = rstd::format("prepared render pass unavailable"),
            });
        }
        FramebufferResourceSystem system(
            device, m_registries.FramebufferCache(), m_registries.FramebufferDiagnostics());
        auto result = system.CreateFramebuffer(vulkan::FramebufferResourceRequest {
            .render_pass     = **(**render_pass).physical,
            .render_pass_key = (**render_pass).cache_key.clone(),
            .attachments     = rstd::move(attachments),
            .extent          = extent,
        });
        if (result.is_none()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("prepare framebuffer failed"),
            });
        }
        if (! m_prepared.Insert(PreparedFramebuffer {
                .use      = framebuffer_use,
                .resource = result->handle,
                .physical = result->framebuffer.clone(),
            })) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("duplicate prepared framebuffer use"),
            });
        }
        return Ok(FramebufferPreparation {
            .resource             = result->handle,
            .cache_key            = rstd::move(result->cache_key),
            .cache_hit            = result->cache_hit,
            .cache_observed_count = result->cache_observed_count,
        });
    }

    auto PrepareRenderPass(resource::RenderPassUseHandle use, const vulkan::Device& device,
                           const vulkan::RenderPassResourceDesc& desc)
        -> Result<RenderPassPreparation, resource::ResourceError> {
        auto result = m_registries.RenderPasses().Ensure(device, desc);
        if (result.is_none()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("prepare render pass failed"),
            });
        }
        if (! m_prepared.Insert(PreparedRenderPass {
                .use       = use,
                .resource  = result->handle,
                .cache_key = result->cache_key.clone(),
                .physical  = result->render_pass.clone(),
            })) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("duplicate prepared render pass use"),
            });
        }
        return Ok(RenderPassPreparation {
            .resource             = result->handle,
            .cache_key            = rstd::move(result->cache_key),
            .cache_hit            = result->cache_hit,
            .cache_observed_count = result->cache_observed_count,
        });
    }

    auto PrepareDescriptor(const vulkan::Device&          device,
                           resource::PipelineLayoutHandle pipeline_layout, u32 set_index,
                           slice<DescriptorImageBinding>  images,
                           slice<DescriptorBufferBinding> buffers, DescriptorBindingReuse reuse)
        -> Result<resource::DescriptorBindingHandle, resource::ResourceError> {
        auto layout_resource = m_registries.PipelineLayouts().Resolve(pipeline_layout);
        if (layout_resource.is_none() ||
            rstd::as_cast<usize>(set_index) >= (**layout_resource).descriptor_layouts.len()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::MissingDefinition,
                .message = rstd::format("pipeline descriptor layout unavailable"),
            });
        }
        auto layout = m_registries.DescriptorLayouts().Resolve(
            (**layout_resource).descriptor_layouts[rstd::as_cast<usize>(set_index)]);
        if (layout.is_none()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::MissingDefinition,
                .message = rstd::format("descriptor layout unavailable"),
            });
        }
        auto prepared = m_registries.Descriptors().Prepare(
            device, set_index.to_primitive(), **layout, images, buffers, reuse);
        if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err_unchecked());
        auto binding = rstd::move(prepared).unwrap_unchecked();
        auto handle  = binding.handle;
        if (! m_prepared.Insert(rstd::move(binding))) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("duplicate descriptor binding {}", handle.index),
            });
        }
        return Ok(handle);
    }

    auto UpdateDescriptorImages(resource::DescriptorBindingHandle handle,
                                slice<DescriptorImageBinding>     images)
        -> Result<empty, resource::ResourceError> {
        auto prepared = m_prepared.ResolveMut(handle);
        if (prepared.is_none()) {
            return Err(resource::ResourceError {
                .kind = resource::ResourceErrorKind::MissingDefinition,
                .message =
                    rstd::format("prepared descriptor binding {} is unavailable", handle.index),
            });
        }
        return (**prepared).UpdateImages(images);
    }

    bool HasPendingUploads() const {
        return m_registries.BufferManager().HasPendingUploads() ||
               m_registries.ImageUploads().HasPendingUploads();
    }

    bool RecordPendingUploads(vvk::CommandBuffer& command, vulkan::RecordedBufferUploads& buffers,
                              vulkan::RecordedImageUploads& images) {
        return m_registries.BufferManager().RecordPendingUploads(command, buffers) &&
               m_registries.ImageUploads().RecordPendingUploads(command, images);
    }

    bool RecordPendingUploads(vvk::CommandBuffer& command, vulkan::RecordedBufferUploads& buffers) {
        return m_registries.BufferManager().RecordPendingUploads(command, buffers);
    }

    auto UpdateBuffer(resource::BufferUseHandle use, slice<u8> content)
        -> Result<empty, resource::ResourceError> {
        auto prepared = m_prepared.Resolve(use);
        if (prepared.is_none()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::MissingDefinition,
                .message = rstd::format("prepared buffer use {} is unavailable", use.index),
            });
        }
        auto backend = dyn<vulkan::BufferBackend>::from_ref(m_registries.BufferManager());
        return m_registries.Buffers().Update(
            (**prepared).buffer.resource, content, backend.as_mut_ref());
    }

    auto ReserveUpload() -> resource::ReadyToken { return m_registries.Uploads().Reserve(); }
    bool MarkUploadSubmitted(resource::ReadyToken token, vulkan::RecordedBufferUploads buffers,
                             vulkan::RecordedImageUploads images) {
        Option<vulkan::BufferUploadBatchLease> buffer_lease = None();
        Option<vulkan::ImageUploadBatchLease>  image_lease  = None();
        if (buffers.Valid()) {
            auto committed =
                m_registries.BufferManager().CommitRecordedUploads(rstd::move(buffers));
            if (committed.is_none()) return false;
            buffer_lease = Some(rstd::move(*committed));
        }
        if (images.Valid()) {
            auto committed = m_registries.ImageUploads().CommitRecordedUploads(rstd::move(images));
            if (committed.is_none()) return false;
            image_lease = Some(rstd::move(*committed));
        }
        if (buffer_lease.is_none() && image_lease.is_none()) return false;
        auto buffer_tickets = Vec<vulkan::BufferUploadTicket>::make();
        if (buffer_lease.is_some()) {
            auto tickets = buffer_lease->Tickets();
            buffer_tickets.extend_from_slice(tickets.as_raw_ptr(), tickets.len());
        }
        auto image_tickets = Vec<vulkan::ImageUploadTicket>::make();
        if (image_lease.is_some()) {
            auto tickets = image_lease->Tickets();
            image_tickets.extend_from_slice(tickets.as_raw_ptr(), tickets.len());
        }
        if (! m_registries.Uploads().MarkSubmitted(
                token, rstd::move(buffer_lease), rstd::move(image_lease))) {
            return false;
        }
        m_registries.Buffers().MarkUploadsSubmitted(buffer_tickets.as_slice(), Some(token));
        m_registries.TextureEntries().MarkUploadsSubmitted(image_tickets.as_slice(), Some(token));
        return true;
    }
    auto PendingUpload() -> Option<resource::ReadyToken> {
        return m_registries.Uploads().Pending();
    }
    void CompleteUploadsThrough(u64 value) { m_registries.Uploads().CompleteThrough(value); }

    auto BeginSubmission(vulkan::RecordedBufferUploads recorded = {}) -> resource::CompletionToken {
        Option<vulkan::BufferUploadBatchLease> upload_lease = None();
        if (recorded.Valid()) {
            auto lease = m_registries.BufferManager().CommitRecordedUploads(rstd::move(recorded));
            if (lease.is_none()) return {};
            m_registries.Buffers().MarkUploadsSubmitted(lease->Tickets(), None());
            upload_lease = Some(rstd::move(*lease));
        }
        return m_registries.Submissions().Begin(m_prepared, rstd::move(upload_lease));
    }
    auto CompleteSubmission(resource::CompletionToken token) -> Option<SubmissionLease> {
        return m_registries.Submissions().Complete(token);
    }

    auto PrepareExternal(resource::ExternalUseHandle       external_use,
                         resource::TextureUseHandle        source_use,
                         const vulkan::DeviceCapabilities& capabilities, FrameSurfaceLease lease,
                         rstd::uint32_t graphics_queue_family)
        -> Result<empty, resource::ResourceError> {
        auto source = m_prepared.Resolve(source_use);
        if (source.is_none()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::MissingDefinition,
                .message = rstd::format("prepared external source unavailable"),
            });
        }
        auto prepared =
            m_registries.External().Prepare(capabilities, rstd::move(lease), graphics_queue_family);
        if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err_unchecked());
        m_prepared.Insert(PreparedExternalUse {
            .use   = external_use,
            .frame = rstd::move(prepared).unwrap_unchecked(),
        });
        return Ok(empty {});
    }

    void Collect(mut_ref<dyn<vulkan::MemoryBudgetSource>> memory) {
        m_registries.PipelineCache().PruneExpired();
        m_registries.PipelineLayouts().PruneExpired();
        m_registries.RenderPassCache().PruneExpired();
        m_registries.FramebufferCache().PruneExpired();
        m_registries.Memory().Refresh(memory.as_ref());
        if (! m_registries.Memory().ShouldEvictTransient()) return;
        m_registries.TextureEntries().EvictUnused();
        m_registries.Buffers().EvictUnused();
        m_registries.BufferManager().Trim();
        m_registries.ImageUploads().Trim();
    }

    void PumpVideoTextures(double seconds) { m_registries.Textures().PumpVideoTextures(seconds); }
    void SetVideoDecodeOptions(vulkan::TextureCache::VideoDecodeOptions options) {
        m_registries.Textures().SetVideoDecodeOptions(rstd::move(options));
    }
    bool UploadFontAtlasRegion(ref<str> key, const rstd::uint8_t* atlas, rstd::uint32_t atlas_width,
                               rstd::uint32_t x, rstd::uint32_t y, rstd::uint32_t width,
                               rstd::uint32_t height) {
        auto handle =
            m_registries.TextureEntries().Find(resource::TextureRequestKind::Imported, key);
        if (handle.is_none()) return false;
        auto physical = m_registries.TextureEntries().ResolveCurrent(*handle);
        if (physical.is_none()) return false;
        return m_registries.Textures().UploadFontAtlasRegion(
            (**physical).allocation.deref(), atlas, atlas_width, x, y, width, height);
    }
    auto CreateLocalSwapchain(const vulkan::Device& device, unsigned width, unsigned height,
                              VkImageTiling tiling) -> Option<ExSwapchainOwner> {
        return vulkan::CreateLocalExSwapchain(
            device, m_registries.Textures(), width, height, tiling);
    }
    void ClearTextures() {
        m_prepare_rollback = None();
        m_prepared         = PreparedResourceTable {};
        m_registries.ImageUploads().DiscardPendingUploads();
        m_registries.TextureEntries().Reset();
        m_registries.Textures().Clear();
    }
    void ClearPreparedGraphics() { m_prepared.ClearPreparedState(); }
    void ClearTransientTextures() { m_registries.TextureEntries().ClearGraphResources(); }
    void EvictUnusedBuffers() { m_registries.Buffers().EvictUnused(); }

    auto PipelineDiagnostics() -> PipelineCacheDiagnostics& {
        return m_registries.PipelineDiagnostics();
    }
    auto FramebufferDiagnostics() -> FramebufferCacheDiagnostics& {
        return m_registries.FramebufferDiagnostics();
    }

private:
    struct PrepareRollback {
        PreparedResourceTable table;
    };

    ResourceRegistries      m_registries;
    PreparedResourceTable   m_prepared;
    Option<PrepareRollback> m_prepare_rollback;
};

} // namespace vrento::resource_registry

export namespace rstd
{

template<>
struct Impl<vrento::resource_registry::GraphicsResourcePreparer,
            vrento::resource_registry::RenderResourceSystem>
    : ImplBase<vrento::resource_registry::RenderResourceSystem> {
    auto PreparePipeline(vrento::resource::PipelineUseHandle     pipeline_use,
                         vrento::resource::RenderPassUseHandle   render_pass_use,
                         const vrento::vulkan::Device&           device,
                         vrento::vulkan::PipelineResourceRequest request)
        -> Result<vrento::resource_registry::PipelinePreparation, vrento::resource::ResourceError> {
        return this->self().PreparePipeline(
            pipeline_use, render_pass_use, device, rstd::move(request));
    }

    auto PrepareFramebuffer(vrento::resource::FramebufferUseHandle         framebuffer_use,
                            vrento::resource::RenderPassUseHandle          render_pass_use,
                            const vrento::vulkan::Device&                  device,
                            Vec<vrento::vulkan::FramebufferAttachmentDesc> attachments,
                            VkExtent2D                                     extent)
        -> Result<vrento::resource_registry::FramebufferPreparation,
                  vrento::resource::ResourceError> {
        return this->self().PrepareFramebuffer(
            framebuffer_use, render_pass_use, device, rstd::move(attachments), extent);
    }

    auto PrepareRenderPass(vrento::resource::RenderPassUseHandle         use,
                           const vrento::vulkan::Device&                 device,
                           const vrento::vulkan::RenderPassResourceDesc& desc)
        -> Result<vrento::resource_registry::RenderPassPreparation,
                  vrento::resource::ResourceError> {
        return this->self().PrepareRenderPass(use, device, desc);
    }

    auto PrepareDescriptor(const vrento::vulkan::Device&          device,
                           vrento::resource::PipelineLayoutHandle pipeline_layout, u32 set_index,
                           slice<vrento::resource_registry::DescriptorImageBinding>  images,
                           slice<vrento::resource_registry::DescriptorBufferBinding> buffers,
                           vrento::resource_registry::DescriptorBindingReuse         reuse)
        -> Result<vrento::resource::DescriptorBindingHandle, vrento::resource::ResourceError> {
        return this->self().PrepareDescriptor(
            device, pipeline_layout, set_index, images, buffers, reuse);
    }

    auto UpdateDescriptorImages(vrento::resource::DescriptorBindingHandle                handle,
                                slice<vrento::resource_registry::DescriptorImageBinding> images)
        -> Result<empty, vrento::resource::ResourceError> {
        return this->self().UpdateDescriptorImages(handle, images);
    }
};

template<>
struct Impl<vrento::resource_registry::ExternalResourcePreparer,
            vrento::resource_registry::RenderResourceSystem>
    : ImplBase<vrento::resource_registry::RenderResourceSystem> {
    auto PrepareExternal(vrento::resource::ExternalUseHandle       external_use,
                         vrento::resource::TextureUseHandle        source_use,
                         const vrento::vulkan::DeviceCapabilities& capabilities,
                         vrento::FrameSurfaceLease lease, rstd::uint32_t graphics_queue_family)
        -> Result<empty, vrento::resource::ResourceError> {
        return this->self().PrepareExternal(
            external_use, source_use, capabilities, rstd::move(lease), graphics_queue_family);
    }
};

template<>
struct Impl<vrento::resource::BufferContentWriter, vrento::resource_registry::RenderResourceSystem>
    : ImplBase<vrento::resource_registry::RenderResourceSystem> {
    auto UpdateBuffer(vrento::resource::BufferUseHandle use, slice<u8> content)
        -> Result<empty, vrento::resource::ResourceError> {
        return this->self().UpdateBuffer(use, content);
    }
};

} // namespace rstd
