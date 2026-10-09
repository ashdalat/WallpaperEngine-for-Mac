export module wescene.pkg.parse:global_uniform;
import rstd;
import wescene.scene;
import wescene.pkg.spec_names;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace owe
{

enum class GlobalUniformProducer : rstd::uint8_t
{
    Frame,
    Audio,
    Light,
    Shadow,
};

enum class GlobalUniformBlockKind : rstd::uint8_t
{
    Frame,
    Audio,
    Lighting,
};

struct GlobalUniformBlockSchema {
    GlobalUniformBlockKind kind;
    ref<str>               name;
    u32                    binding;
    u64                    identity;
    usize                  size;
};

struct GlobalUniformField {
    ref<str>              name;
    ref<str>              alias;
    ref<str>              type;
    GlobalUniformProducer producer;
    UniformOutputId       output;
    UniformValueShape     shape;
    u32                   offset;
};

inline constexpr u32      kGlobalUniformSet { u32(0) };
inline constexpr u32      kDrawUniformSet { u32(1) };
inline constexpr u32      kDrawUniformBinding { u32(0) };
inline constexpr u64      kGlobalUniformSetIdentity { u64(0x5745474c4f424c03ULL) };
inline constexpr u64      kFrameUniformSchemaIdentity { u64(0x57454652414d4501ULL) };
inline constexpr u64      kAudioUniformSchemaIdentity { u64(0x5745415544494f01ULL) };
inline constexpr u64      kLightingUniformSchemaIdentity { u64(0x57454c4947485401ULL) };
inline constexpr usize    kFrameUniformBlockSize { usize(48) };
inline constexpr usize    kAudioUniformBlockSize { usize(3584) };
inline constexpr usize    kLightingUniformBlockSize { usize(800) };
inline constexpr ref<str> kGlobalUniformBlockName { "ww_GlobalUniforms"_str };
inline constexpr ref<str> kAudioUniformBlockName { "ww_AudioUniforms"_str };
inline constexpr ref<str> kLightingUniformBlockName { "ww_LightingUniforms"_str };
inline constexpr ref<str> kDrawUniformBlockName { "ww_DrawUniforms"_str };

inline auto GlobalUniformBlocks() -> slice<GlobalUniformBlockSchema> {
    static const array<GlobalUniformBlockSchema, 3> blocks {
        GlobalUniformBlockSchema { GlobalUniformBlockKind::Frame,
                                   kGlobalUniformBlockName,
                                   u32(0),
                                   kFrameUniformSchemaIdentity,
                                   kFrameUniformBlockSize },
        GlobalUniformBlockSchema { GlobalUniformBlockKind::Audio,
                                   kAudioUniformBlockName,
                                   u32(1),
                                   kAudioUniformSchemaIdentity,
                                   kAudioUniformBlockSize },
        GlobalUniformBlockSchema { GlobalUniformBlockKind::Lighting,
                                   kLightingUniformBlockName,
                                   u32(2),
                                   kLightingUniformSchemaIdentity,
                                   kLightingUniformBlockSize },
    };
    return blocks.as_slice();
}

inline auto GlobalUniformBlockFor(GlobalUniformProducer producer) -> GlobalUniformBlockKind {
    switch (producer) {
    case GlobalUniformProducer::Frame: return GlobalUniformBlockKind::Frame;
    case GlobalUniformProducer::Audio: return GlobalUniformBlockKind::Audio;
    case GlobalUniformProducer::Light:
    case GlobalUniformProducer::Shadow: return GlobalUniformBlockKind::Lighting;
    }
    rstd::unreachable();
}

inline auto FindGlobalUniformBlock(GlobalUniformBlockKind kind)
    -> Option<ref<GlobalUniformBlockSchema>> {
    for (const auto& block : GlobalUniformBlocks()) {
        if (block.kind == kind) {
            return Some(ref<GlobalUniformBlockSchema>::from_raw_parts(rstd::addressof(block)));
        }
    }
    return None();
}

inline auto FindGlobalUniformBlock(ref<str> name) -> Option<ref<GlobalUniformBlockSchema>> {
    for (const auto& block : GlobalUniformBlocks()) {
        if (block.name == name) {
            return Some(ref<GlobalUniformBlockSchema>::from_raw_parts(rstd::addressof(block)));
        }
    }
    return None();
}

inline auto GlobalUniformFields() -> slice<GlobalUniformField> {
    static const array<GlobalUniformField, 20> fields {
        GlobalUniformField { G_TIME,
                             {},
                             "float"_str,
                             GlobalUniformProducer::Frame,
                             UniformOutputId { u32(0) },
                             UniformValueShape::Float(u32(1)),
                             u32(0) },
        GlobalUniformField { G_FRAMETIME,
                             {},
                             "float"_str,
                             GlobalUniformProducer::Frame,
                             UniformOutputId { u32(1) },
                             UniformValueShape::Float(u32(1)),
                             u32(4) },
        GlobalUniformField { G_DAYTIME,
                             G_DAYTIME_LEGACY,
                             "float"_str,
                             GlobalUniformProducer::Frame,
                             UniformOutputId { u32(2) },
                             UniformValueShape::Float(u32(1)),
                             u32(8) },
        GlobalUniformField { G_POINTERPOSITION,
                             {},
                             "vec2"_str,
                             GlobalUniformProducer::Frame,
                             UniformOutputId { u32(3) },
                             UniformValueShape::Float(u32(2)),
                             u32(16) },
        GlobalUniformField { G_POINTERPOSITIONLAST,
                             {},
                             "vec2"_str,
                             GlobalUniformProducer::Frame,
                             UniformOutputId { u32(4) },
                             UniformValueShape::Float(u32(2)),
                             u32(24) },
        GlobalUniformField { G_PARALLAXPOSITION,
                             {},
                             "vec2"_str,
                             GlobalUniformProducer::Frame,
                             UniformOutputId { u32(5) },
                             UniformValueShape::Float(u32(2)),
                             u32(32) },
        GlobalUniformField { G_AUDIO_SPEC_16_L,
                             {},
                             "float[16]"_str,
                             GlobalUniformProducer::Audio,
                             UniformOutputId { u32(0) },
                             UniformValueShape::Float(u32(16)),
                             u32(0) },
        GlobalUniformField { G_AUDIO_SPEC_16_R,
                             {},
                             "float[16]"_str,
                             GlobalUniformProducer::Audio,
                             UniformOutputId { u32(1) },
                             UniformValueShape::Float(u32(16)),
                             u32(256) },
        GlobalUniformField { G_AUDIO_SPEC_32_L,
                             {},
                             "float[32]"_str,
                             GlobalUniformProducer::Audio,
                             UniformOutputId { u32(2) },
                             UniformValueShape::Float(u32(32)),
                             u32(512) },
        GlobalUniformField { G_AUDIO_SPEC_32_R,
                             {},
                             "float[32]"_str,
                             GlobalUniformProducer::Audio,
                             UniformOutputId { u32(3) },
                             UniformValueShape::Float(u32(32)),
                             u32(1024) },
        GlobalUniformField { G_AUDIO_SPEC_64_L,
                             {},
                             "float[64]"_str,
                             GlobalUniformProducer::Audio,
                             UniformOutputId { u32(4) },
                             UniformValueShape::Float(u32(64)),
                             u32(1536) },
        GlobalUniformField { G_AUDIO_SPEC_64_R,
                             {},
                             "float[64]"_str,
                             GlobalUniformProducer::Audio,
                             UniformOutputId { u32(5) },
                             UniformValueShape::Float(u32(64)),
                             u32(2560) },
        GlobalUniformField { G_LP,
                             {},
                             "vec3[4]"_str,
                             GlobalUniformProducer::Light,
                             UniformOutputId { u32(0) },
                             UniformValueShape::Float(u32(12)),
                             u32(0) },
        GlobalUniformField { G_LCP,
                             {},
                             "vec4[3]"_str,
                             GlobalUniformProducer::Light,
                             UniformOutputId { u32(1) },
                             UniformValueShape::Float(u32(12)),
                             u32(64) },
        GlobalUniformField { G_LCR,
                             {},
                             "vec4[4]"_str,
                             GlobalUniformProducer::Light,
                             UniformOutputId { u32(2) },
                             UniformValueShape::Float(u32(16)),
                             u32(112) },
        GlobalUniformField { G_LIGHTDIRECTIONTYPE,
                             {},
                             "vec4[4]"_str,
                             GlobalUniformProducer::Light,
                             UniformOutputId { u32(3) },
                             UniformValueShape::Float(u32(16)),
                             u32(176) },
        GlobalUniformField { G_LIGHTCONEEXPONENT,
                             {},
                             "vec4[4]"_str,
                             GlobalUniformProducer::Light,
                             UniformOutputId { u32(4) },
                             UniformValueShape::Float(u32(16)),
                             u32(240) },
        GlobalUniformField { G_LIGHTCASTSHADOW,
                             {},
                             "float[4]"_str,
                             GlobalUniformProducer::Light,
                             UniformOutputId { u32(5) },
                             UniformValueShape::Float(u32(4)),
                             u32(304) },
        GlobalUniformField { G_VIEWPORTVIEWPROJECTIONMATRICES,
                             {},
                             "mat4[6]"_str,
                             GlobalUniformProducer::Shadow,
                             UniformOutputId { u32(0) },
                             UniformValueShape::MatrixArray(u32(4), u32(4), usize(6), usize(6)),
                             u32(368) },
        GlobalUniformField { G_SHADOWATLASTRANSFORMS,
                             {},
                             "vec4[3]"_str,
                             GlobalUniformProducer::Shadow,
                             UniformOutputId { u32(1) },
                             UniformValueShape::Float(u32(12)),
                             u32(752) },
    };
    return fields.as_slice();
}

inline auto FindGlobalUniform(ref<str> name) -> Option<ref<GlobalUniformField>> {
    for (const auto& field : GlobalUniformFields()) {
        if (field.name == name || (! field.alias.is_empty() && field.alias == name)) {
            return Some(ref<GlobalUniformField>::from_raw_parts(rstd::addressof(field)));
        }
    }
    return None();
}

} // namespace owe
