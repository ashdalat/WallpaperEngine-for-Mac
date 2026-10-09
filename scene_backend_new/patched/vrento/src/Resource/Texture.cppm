export module vrento.resource:texture;
import rstd;
import vrento.texture_types;
import :handle;

export namespace vrento::resource
{

using namespace rstd::prelude;

struct TextureDefinitionId {
    u32 index { u32::MAX };
    u64 generation {};

    bool Valid() const noexcept { return index != u32::MAX && generation != u64(); }

    friend bool operator==(const TextureDefinitionId&, const TextureDefinitionId&) = default;
};

enum class TextureDomain
{
    Asset,
    RenderTarget,
    Dynamic,
    External,
};

enum class TextureLifetimeClass
{
    FrameLocal,
    Retained,
    Dedicated,
    ExternalOwned,
};

using TextureContentFlags = u32;

enum class TextureContent : rstd::uint32_t
{
    SourceDefined         = 1u << 0u,
    DiscardOnAcquire      = 1u << 1u,
    ClearOnFirstWrite     = 1u << 2u,
    ClearOnEveryWrite     = 1u << 3u,
    PreserveWithinGraph   = 1u << 4u,
    PreserveAcrossFrames  = 1u << 5u,
    ProducerUpdated       = 1u << 6u,
    ExternalCurrentFrame  = 1u << 7u,
    InitializeTransparent = 1u << 8u,
};

inline constexpr TextureContentFlags TextureContentFlag(TextureContent content) {
    return TextureContentFlags(static_cast<rstd::uint32_t>(content));
}

enum class TextureContractSource
{
    Explicit,
    LegacyAdapter,
};

struct TextureResourceContract {
    TextureDomain         domain { TextureDomain::Asset };
    TextureLifetimeClass  lifetime { TextureLifetimeClass::Retained };
    TextureContentFlags   content { TextureContentFlag(TextureContent::SourceDefined) };
    TextureContractSource source { TextureContractSource::Explicit };

    friend bool operator==(const TextureResourceContract&,
                           const TextureResourceContract&) = default;
};

struct TextureResourceIntent {
    TextureResourceContract contract;

    friend bool operator==(const TextureResourceIntent&, const TextureResourceIntent&) = default;
};

enum class TextureUsage : rstd::uint32_t
{
    None                = 0,
    Sampled             = 1u << 0,
    ColorAttachment     = 1u << 1,
    DepthAttachment     = 1u << 2,
    TransferSource      = 1u << 3,
    TransferDestination = 1u << 4,
    Color               = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4),
    Depth               = 1u << 2,
};

constexpr auto operator|(TextureUsage lhs, TextureUsage rhs) noexcept -> TextureUsage {
    return static_cast<TextureUsage>(static_cast<rstd::uint32_t>(lhs) |
                                     static_cast<rstd::uint32_t>(rhs));
}

constexpr bool HasTextureUsage(TextureUsage usages, TextureUsage usage) noexcept {
    return (static_cast<rstd::uint32_t>(usages) & static_cast<rstd::uint32_t>(usage)) ==
           static_cast<rstd::uint32_t>(usage);
}

struct TextureDefinition {
    i32           width {};
    i32           height {};
    TextureUsage  usage { TextureUsage::Color };
    TextureFormat format { TextureFormat::RGBA8 };
    TextureSample sample;
    u32           mip_levels { u32(1) };
    u32           samples { u32(1) };

    friend bool operator==(const TextureDefinition& lhs, const TextureDefinition& rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height && lhs.usage == rhs.usage &&
               lhs.format == rhs.format && lhs.sample.wrapS == rhs.sample.wrapS &&
               lhs.sample.wrapT == rhs.sample.wrapT &&
               lhs.sample.magFilter == rhs.sample.magFilter &&
               lhs.sample.minFilter == rhs.sample.minFilter &&
               lhs.sample.compare_enable == rhs.sample.compare_enable &&
               lhs.sample.compare_op == rhs.sample.compare_op &&
               lhs.sample.border_color == rhs.sample.border_color &&
               lhs.mip_levels == rhs.mip_levels && lhs.samples == rhs.samples;
    }
};

struct ImportedTextureContentIdentity {
    String key;
    u64    revision { 1 };

    auto clone() const -> ImportedTextureContentIdentity {
        return ImportedTextureContentIdentity {
            .key      = key.clone(),
            .revision = revision,
        };
    }

    friend bool operator==(const ImportedTextureContentIdentity& lhs,
                           const ImportedTextureContentIdentity& rhs) {
        return lhs.key == rhs.key.as_str() && lhs.revision == rhs.revision;
    }
};

enum class TextureRequestKind
{
    Imported,
    RenderTarget,
    RenderTargetMsaa,
    DepthAttachment,
};

struct TextureRequest {
    TextureRequestKind          kind { TextureRequestKind::Imported };
    String                      name;
    Option<TextureDefinitionId> source;
    Option<TextureDefinition>   definition;
    TextureLifetimeClass        lifetime { TextureLifetimeClass::Retained };
    TextureContentFlags         content { TextureContentFlag(TextureContent::SourceDefined) };

    auto clone() const -> TextureRequest {
        return TextureRequest {
            .kind       = kind,
            .name       = name.clone(),
            .source     = source,
            .definition = definition,
            .lifetime   = lifetime,
            .content    = content,
        };
    }
};

struct TextureBindingRequest {
    String                   name;
    Option<TextureUseHandle> use;
    Option<TextureRequest>   request;

    bool empty() const { return name.is_empty(); }

    auto clone() const -> TextureBindingRequest {
        return TextureBindingRequest {
            .name    = name.clone(),
            .use     = use,
            .request = request.is_some() ? Some(request->clone()) : None<TextureRequest>(),
        };
    }
};

inline bool SameTextureRequest(const TextureRequest& lhs, const TextureRequest& rhs) {
    return lhs.kind == rhs.kind && lhs.name == rhs.name.as_str() && lhs.source == rhs.source &&
           lhs.definition == rhs.definition && lhs.lifetime == rhs.lifetime &&
           lhs.content == rhs.content;
}

inline bool SameTextureRequest(const Option<TextureRequest>& lhs,
                               const Option<TextureRequest>& rhs) {
    if (lhs.is_some() != rhs.is_some()) return false;
    return lhs.is_none() || SameTextureRequest(*lhs, *rhs);
}

inline bool SameTextureBindingRequest(const TextureBindingRequest& lhs,
                                      const TextureBindingRequest& rhs) {
    return lhs.name == rhs.name.as_str() && lhs.use == rhs.use &&
           SameTextureRequest(lhs.request, rhs.request);
}

inline bool SetTextureRequestIfChanged(Option<TextureRequest>& target,
                                       Option<TextureRequest>  request) {
    if (SameTextureRequest(target, request)) return false;
    target = rstd::move(request);
    return true;
}

inline bool SetTextureRequestIfChanged(Option<TextureRequest>& target, TextureRequest request) {
    return SetTextureRequestIfChanged(target, Some(rstd::move(request)));
}

} // namespace vrento::resource
