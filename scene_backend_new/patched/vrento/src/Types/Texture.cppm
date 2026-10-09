export module vrento.texture_types;
import rstd;

export namespace vrento
{

enum class TextureFormat
{
    BC1,
    BC2,
    BC3,
    RGB8,
    RGBA8,
    RG8,
    R8,
    D32F
};
enum class TextureWrap
{
    CLAMP_TO_EDGE,
    CLAMP_TO_BORDER,
    REPEAT
};
enum class TextureFilter
{
    LINEAR,
    NEAREST
};
enum class CompareOp
{
    Never,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,
    Always
};
enum class TextureBorderColor
{
    TransparentBlack,
    OpaqueBlack,
    OpaqueWhite
};

struct TextureSample {
    TextureWrap        wrapS { TextureWrap::REPEAT };
    TextureWrap        wrapT { TextureWrap::REPEAT };
    TextureFilter      magFilter { TextureFilter::NEAREST };
    TextureFilter      minFilter { TextureFilter::NEAREST };
    bool               compare_enable { false };
    CompareOp          compare_op { CompareOp::Never };
    TextureBorderColor border_color { TextureBorderColor::OpaqueBlack };
};

} // namespace vrento

export namespace rstd
{

template<>
struct Impl<hash::Hash, vrento::TextureSample> : ImplBase<vrento::TextureSample> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        const auto& sample = this->self();
        hash::hash_into(u32(static_cast<rstd::uint32_t>(sample.wrapS)), state);
        hash::hash_into(u32(static_cast<rstd::uint32_t>(sample.wrapT)), state);
        hash::hash_into(u32(static_cast<rstd::uint32_t>(sample.magFilter)), state);
        hash::hash_into(u32(static_cast<rstd::uint32_t>(sample.minFilter)), state);
        hash::hash_into(sample.compare_enable, state);
        hash::hash_into(u32(static_cast<rstd::uint32_t>(sample.compare_op)), state);
        hash::hash_into(u32(static_cast<rstd::uint32_t>(sample.border_color)), state);
    }
};

} // namespace rstd
