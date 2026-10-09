export module vrento.resource:handle;
import rstd;

export namespace vrento::resource
{

using namespace rstd::prelude;

template<typename Tag>
struct ResourceHandle {
    u64 index { u64::MAX };
    u64 generation {};

    bool Valid() const noexcept { return index != u64::MAX && generation != u64(); }

    friend bool operator==(const ResourceHandle&, const ResourceHandle&) = default;
};

struct TextureHandleTag;
struct BufferHandleTag;
struct PipelineHandleTag;
struct ShaderHandleTag;
struct RenderPassHandleTag;
struct FramebufferHandleTag;
struct ResourceUseHandleTag;
struct TextureUseHandleTag;
struct BufferUseHandleTag;
struct ShaderUseHandleTag;
struct PipelineUseHandleTag;
struct RenderPassUseHandleTag;
struct FramebufferUseHandleTag;
struct ExternalUseHandleTag;
struct DescriptorLayoutHandleTag;
struct PipelineLayoutHandleTag;
struct DescriptorBindingHandleTag;

using TextureHandle           = ResourceHandle<TextureHandleTag>;
using BufferHandle            = ResourceHandle<BufferHandleTag>;
using PipelineHandle          = ResourceHandle<PipelineHandleTag>;
using ShaderHandle            = ResourceHandle<ShaderHandleTag>;
using RenderPassHandle        = ResourceHandle<RenderPassHandleTag>;
using FramebufferHandle       = ResourceHandle<FramebufferHandleTag>;
using ResourceUseHandle       = ResourceHandle<ResourceUseHandleTag>;
using TextureUseHandle        = ResourceHandle<TextureUseHandleTag>;
using BufferUseHandle         = ResourceHandle<BufferUseHandleTag>;
using ShaderUseHandle         = ResourceHandle<ShaderUseHandleTag>;
using PipelineUseHandle       = ResourceHandle<PipelineUseHandleTag>;
using RenderPassUseHandle     = ResourceHandle<RenderPassUseHandleTag>;
using FramebufferUseHandle    = ResourceHandle<FramebufferUseHandleTag>;
using ExternalUseHandle       = ResourceHandle<ExternalUseHandleTag>;
using DescriptorLayoutHandle  = ResourceHandle<DescriptorLayoutHandleTag>;
using PipelineLayoutHandle    = ResourceHandle<PipelineLayoutHandleTag>;
using DescriptorBindingHandle = ResourceHandle<DescriptorBindingHandleTag>;

} // namespace vrento::resource

export namespace rstd
{

template<typename Tag>
struct Impl<Copy, vrento::resource::ResourceHandle<Tag>> {};

template<typename Tag>
struct Impl<hash::Hash, vrento::resource::ResourceHandle<Tag>>
    : ImplBase<vrento::resource::ResourceHandle<Tag>> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().index, state);
        hash::hash_into(this->self().generation, state);
    }
};

} // namespace rstd
