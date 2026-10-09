export module vrento.resource_registry:owner;
import rstd;
import vrento.vulkan;

import :texture_registry;
import :buffer_registry;
import :shader_registry;
import :descriptor;
import :lifetime;
import :state;
import :policy;
import :external;
import :graphics;

export namespace vrento::resource_registry
{

using namespace rstd::prelude;

class ResourceRegistries {
public:
    ResourceRegistries()                                     = default;
    ResourceRegistries(const ResourceRegistries&)            = delete;
    ResourceRegistries& operator=(const ResourceRegistries&) = delete;
    ResourceRegistries(ResourceRegistries&&)                 = delete;
    ResourceRegistries& operator=(ResourceRegistries&&)      = delete;

    bool Initialize(const vulkan::Device& device) {
        if (Ready()) return true;

        auto buffer_manager = Box<vulkan::BufferManager>::make(device);
        if (! buffer_manager.get()->init()) return false;
        auto image_uploads = Box<vulkan::ImageUploadManager>::make(device);
        if (! image_uploads.get()->init()) return false;

        m_textures       = Some(Box<vulkan::TextureCache>::make(device));
        m_buffer_manager = Some(rstd::move(buffer_manager));
        m_image_uploads  = Some(rstd::move(image_uploads));
        return true;
    }

    void Reset() {
        m_submissions.Reset();
        m_uploads.Reset();
        m_pipeline_cache.Reset();
        m_framebuffer_cache.Reset();
        m_descriptor_system.Reset();
        m_pipeline_layouts.Reset();
        m_descriptor_layouts.Reset();
        m_render_pass_cache.Reset();
        m_buffer_entries.Reset();
        m_texture_entries.Reset();
        if (m_buffer_manager.is_some()) m_buffer_manager->get()->destroy();
        if (m_image_uploads.is_some()) m_image_uploads->get()->destroy();
        m_buffer_manager = None();
        m_image_uploads  = None();
        m_textures       = None();
        m_shader_entries.Reset();
        m_states.Reset();
        m_memory.Reset();
        m_pipeline_diagnostics.Reset();
        m_framebuffer_diagnostics.Reset();
    }

    bool Ready() const noexcept {
        return m_textures.is_some() && m_buffer_manager.is_some() && m_image_uploads.is_some();
    }

    auto Textures() -> vulkan::TextureCache& { return *m_textures->get(); }
    auto Textures() const -> const vulkan::TextureCache& {
        return *m_textures->as_ptr().as_raw_ptr();
    }
    auto BufferManager() -> vulkan::BufferManager& { return *m_buffer_manager->get(); }
    auto BufferManager() const -> const vulkan::BufferManager& {
        return *m_buffer_manager->as_ptr().as_raw_ptr();
    }
    auto ImageUploads() -> vulkan::ImageUploadManager& { return *m_image_uploads->get(); }
    auto ImageUploads() const -> const vulkan::ImageUploadManager& {
        return *m_image_uploads->as_ptr().as_raw_ptr();
    }

    auto TextureEntries() -> resource::TextureRegistry& { return m_texture_entries; }
    auto Buffers() -> BufferRegistry& { return m_buffer_entries; }
    auto Shaders() -> ShaderRegistry& { return m_shader_entries; }
    auto DescriptorLayouts() -> DescriptorLayoutRegistry& { return m_descriptor_layouts; }
    auto PipelineLayouts() -> PipelineLayoutRegistry& { return m_pipeline_layouts; }
    auto Descriptors() -> DescriptorSystem& { return m_descriptor_system; }
    auto Uploads() -> UploadScheduler& { return m_uploads; }
    auto Submissions() -> SubmissionTracker& { return m_submissions; }
    auto States() -> ResourceStateTracker& { return m_states; }
    auto Memory() -> MemoryBudgetPolicy& { return m_memory; }
    auto External() -> ExternalResourceBridge& { return m_external; }
    auto Pipelines() -> PipelineRegistry& { return m_pipeline_cache; }
    auto PipelineCache() -> PipelineRegistry& { return m_pipeline_cache; }
    auto RenderPasses() -> RenderPassRegistry& { return m_render_pass_cache; }
    auto RenderPassCache() -> RenderPassRegistry& { return m_render_pass_cache; }
    auto Framebuffers() -> FramebufferRegistry& { return m_framebuffer_cache; }
    auto FramebufferCache() -> FramebufferRegistry& { return m_framebuffer_cache; }
    auto PipelineDiagnostics() -> PipelineCacheDiagnostics& { return m_pipeline_diagnostics; }
    auto FramebufferDiagnostics() -> FramebufferCacheDiagnostics& {
        return m_framebuffer_diagnostics;
    }

private:
    Option<Box<vulkan::TextureCache>>       m_textures;
    Option<Box<vulkan::BufferManager>>      m_buffer_manager;
    Option<Box<vulkan::ImageUploadManager>> m_image_uploads;
    resource::TextureRegistry               m_texture_entries;
    BufferRegistry                          m_buffer_entries;
    ShaderRegistry                          m_shader_entries;
    DescriptorLayoutRegistry                m_descriptor_layouts;
    PipelineLayoutRegistry                  m_pipeline_layouts;
    DescriptorSystem                        m_descriptor_system;
    UploadScheduler                         m_uploads;
    SubmissionTracker                       m_submissions;
    ResourceStateTracker                    m_states;
    MemoryBudgetPolicy                      m_memory;
    ExternalResourceBridge                  m_external;
    PipelineRegistry                        m_pipeline_cache;
    RenderPassRegistry                      m_render_pass_cache;
    FramebufferRegistry                     m_framebuffer_cache;
    PipelineCacheDiagnostics                m_pipeline_diagnostics;
    FramebufferCacheDiagnostics             m_framebuffer_diagnostics;
};

} // namespace vrento::resource_registry
