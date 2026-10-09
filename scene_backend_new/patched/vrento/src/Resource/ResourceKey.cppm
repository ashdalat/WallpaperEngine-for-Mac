module;

#include <vulkan/vulkan_core.h>

export module vrento.resource_registry:resource_key;
import rstd;
import vrento.resource;
import vrento.shader_types;
import vrento.vulkan;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::slice_::sort_unstable_by;

export namespace vrento::vulkan
{

struct PipelineResourceRequest {
    resource::PipelineLayoutHandle         pipeline_layout;
    Vec<VkVertexInputBindingDescription>   vertex_bindings;
    Vec<VkVertexInputAttributeDescription> vertex_attrs;
    Vec<Uni_ShaderSpv>                     shader_stages;
    VkPipelineColorBlendAttachmentState    color_blend {};
    VkPipelineColorBlendStateCreateFlags   color_blend_flags { 0 };
    rstd::array<float, 4>                  blend_constants { 0.0f, 0.0f, 0.0f, 0.0f };
    VkPipelineDepthStencilStateCreateInfo  depth {};
    VkPipelineRasterizationStateCreateInfo raster {};
    Option<bool>                           depth_clip;
    VkPipelineMultisampleStateCreateInfo   multisample {};
    VkPipelineCreateFlags                  create_flags { 0 };
    VkPrimitiveTopology                    topology { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP };
    rstd::uint32_t                         subpass { 0 };
    bool                                   primitive_restart_enable { false };
    rstd::uint32_t                         viewport_count { 1 };
    rstd::uint32_t                         scissor_count { 1 };
    bool                                   logic_op_enable { false };
    VkLogicOp                              logic_op { VK_LOGIC_OP_COPY };
    Vec<VkDynamicState>                    dynamic_states = Vec<VkDynamicState>::from(
        rstd::array<VkDynamicState, 2> { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }
            .as_slice());
    VkFormat            color_format { VK_FORMAT_R8G8B8A8_UNORM };
    VkImageLayout       color_initial_layout { VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageLayout       color_final_layout { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkAttachmentLoadOp  color_load_op { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
    VkAttachmentLoadOp  depth_load_op { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
    VkAttachmentStoreOp depth_store_op { VK_ATTACHMENT_STORE_OP_STORE };
    VkImageLayout       depth_final_layout { VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    bool                has_color_attachment { true };
    bool                has_depth_attachment { false };
};

struct PipelineCacheKey {
    rstd::size_t value { 0 };
    Vec<u8>      bytes;

    PipelineCacheKey clone() const { return { .value = value, .bytes = bytes.clone() }; }
    void             clone_from(const PipelineCacheKey& other) {
        value = other.value;
        bytes.clone_from(other.bytes);
    }
};

struct RenderPassCacheKey {
    rstd::size_t value { 0 };
    Vec<u8>      bytes;

    RenderPassCacheKey clone() const { return { .value = value, .bytes = bytes.clone() }; }
    void               clone_from(const RenderPassCacheKey& other) {
        value = other.value;
        bytes.clone_from(other.bytes);
    }
};

struct FramebufferCacheKey {
    rstd::size_t value { 0 };
    Vec<u8>      bytes;

    FramebufferCacheKey clone() const { return { .value = value, .bytes = bytes.clone() }; }
    void                clone_from(const FramebufferCacheKey& other) {
        value = other.value;
        bytes.clone_from(other.bytes);
    }
};

struct FramebufferAttachmentIdentity {
    rstd::size_t value { 0 };
    Vec<u8>      bytes;

    FramebufferAttachmentIdentity clone() const {
        return { .value = value, .bytes = bytes.clone() };
    }
    void clone_from(const FramebufferAttachmentIdentity& other) {
        value = other.value;
        bytes.clone_from(other.bytes);
    }
};

struct FramebufferAttachmentDesc {
    VkImageView                   view { VK_NULL_HANDLE };
    FramebufferAttachmentIdentity identity;

    FramebufferAttachmentDesc clone() const {
        return { .view = view, .identity = identity.clone() };
    }
    void clone_from(const FramebufferAttachmentDesc& other) {
        view = other.view;
        identity.clone_from(other.identity);
    }
};

struct FramebufferResourceRequest {
    VkRenderPass                   render_pass { VK_NULL_HANDLE };
    RenderPassCacheKey             render_pass_key;
    Vec<FramebufferAttachmentDesc> attachments;
    VkExtent2D                     extent { 0, 0 };
    rstd::uint32_t                 layers { 1 };
};

struct RenderPassResourceDesc {
    VkFormat              color_format { VK_FORMAT_R8G8B8A8_UNORM };
    VkFormat              depth_format { VK_FORMAT_D32_SFLOAT };
    VkSampleCountFlagBits samples { VK_SAMPLE_COUNT_1_BIT };
    VkImageLayout         color_initial_layout { VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageLayout         color_final_layout { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkAttachmentLoadOp    color_load_op { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
    VkAttachmentStoreOp   color_store_op { VK_ATTACHMENT_STORE_OP_STORE };
    VkAttachmentLoadOp    color_stencil_load_op { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
    VkAttachmentStoreOp   color_stencil_store_op { VK_ATTACHMENT_STORE_OP_DONT_CARE };
    VkImageLayout         color_attachment_layout { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkImageLayout         resolve_initial_layout { VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageLayout         resolve_final_layout { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkAttachmentLoadOp    resolve_load_op { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
    VkAttachmentStoreOp   resolve_store_op { VK_ATTACHMENT_STORE_OP_STORE };
    VkAttachmentLoadOp    resolve_stencil_load_op { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
    VkAttachmentStoreOp   resolve_stencil_store_op { VK_ATTACHMENT_STORE_OP_DONT_CARE };
    VkImageLayout         resolve_attachment_layout { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkImageLayout         depth_initial_layout { VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageLayout         depth_final_layout { VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkAttachmentLoadOp    depth_load_op { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
    VkAttachmentStoreOp   depth_store_op { VK_ATTACHMENT_STORE_OP_STORE };
    VkAttachmentLoadOp    depth_stencil_load_op { VK_ATTACHMENT_LOAD_OP_DONT_CARE };
    VkAttachmentStoreOp   depth_stencil_store_op { VK_ATTACHMENT_STORE_OP_DONT_CARE };
    VkImageLayout depth_attachment_layout { VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    bool          has_color_attachment { true };
    bool          has_resolve_attachment { false };
    bool          has_depth_attachment { false };
};

struct PipelineResourceDesc {
    resource::PipelineLayoutHandle         pipeline_layout;
    Vec<VkVertexInputBindingDescription>   vertex_bindings;
    Vec<VkVertexInputAttributeDescription> vertex_attrs;
    Vec<ShaderSpv>                         shader_stages;
    VkPipelineColorBlendAttachmentState    color_blend {};
    VkPipelineColorBlendStateCreateFlags   color_blend_flags { 0 };
    rstd::array<float, 4>                  blend_constants { 0.0f, 0.0f, 0.0f, 0.0f };
    VkPipelineDepthStencilStateCreateInfo  depth {};
    VkPipelineRasterizationStateCreateInfo raster {};
    Option<bool>                           depth_clip;
    VkPipelineMultisampleStateCreateInfo   multisample {};
    VkPipelineCreateFlags                  create_flags { 0 };
    VkPrimitiveTopology                    topology { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP };
    rstd::uint32_t                         subpass { 0 };
    bool                                   primitive_restart_enable { false };
    rstd::uint32_t                         viewport_count { 1 };
    rstd::uint32_t                         scissor_count { 1 };
    bool                                   logic_op_enable { false };
    VkLogicOp                              logic_op { VK_LOGIC_OP_COPY };
    Vec<VkDynamicState>                    dynamic_states = Vec<VkDynamicState>::from(
        rstd::array<VkDynamicState, 2> { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }
            .as_slice());
    RenderPassResourceDesc render_pass;
};

struct FramebufferResourceDesc {
    VkRenderPass                   render_pass { VK_NULL_HANDLE };
    RenderPassCacheKey             render_pass_key;
    Vec<FramebufferAttachmentDesc> attachments;
    VkExtent2D                     extent { 0, 0 };
    rstd::uint32_t                 layers { 1 };
};

struct PipelineCacheProbe {
    PipelineCacheKey key;
    bool             hit { false };
    u64              observed_count { 0 };
};

inline bool SamePipelineCacheKey(const PipelineCacheKey& lhs, const PipelineCacheKey& rhs) {
    return lhs.value == rhs.value && lhs.bytes == rhs.bytes;
}

inline bool SameRenderPassCacheKey(const RenderPassCacheKey& lhs, const RenderPassCacheKey& rhs) {
    return lhs.value == rhs.value && lhs.bytes == rhs.bytes;
}

inline bool SameFramebufferCacheKey(const FramebufferCacheKey& lhs,
                                    const FramebufferCacheKey& rhs) {
    return lhs.value == rhs.value && lhs.bytes == rhs.bytes;
}

struct CanonicalCacheKeyData {
    rstd::size_t value { 0 };
    Vec<u8>      bytes;
};

class PipelineKeyWriter {
public:
    void writeArraySize(rstd::size_t value) {
        writeType(ValueType::ArraySize);
        writeRawU64(static_cast<rstd::uint64_t>(value));
    }

    void writeBool(bool value) {
        writeType(ValueType::Bool);
        writeRawU8(static_cast<rstd::uint8_t>(value ? 1u : 0u));
    }

    void writeU32(rstd::uint32_t value) {
        writeType(ValueType::U32);
        writeRawU32(value);
    }

    void writeU64(rstd::uint64_t value) {
        writeType(ValueType::U64);
        writeRawU64(value);
    }

    void writeF32(float value) {
        writeType(ValueType::F32);
        writeRawU32(rstd::bit_cast<rstd::uint32_t>(value));
    }

    void writeString(ref<str> value) {
        writeType(ValueType::String);
        writeRawU64(static_cast<rstd::uint64_t>(value.len().to_primitive()));
        for (auto ch : value.as_bytes()) writeRawU8(ch.to_primitive());
    }

    void writeBytes(slice<u8> value) {
        writeType(ValueType::Bytes);
        writeRawU64(static_cast<rstd::uint64_t>(value.len().to_primitive()));
        m_bytes.extend_from_slice(value);
    }

    CanonicalCacheKeyData finish() && {
        return CanonicalCacheKeyData {
            .value = HashCanonicalBytes(m_bytes.as_slice()),
            .bytes = rstd::move(m_bytes),
        };
    }

private:
    enum class ValueType : rstd::uint8_t
    {
        U32       = 1,
        U64       = 2,
        Bool      = 3,
        F32       = 4,
        String    = 5,
        ArraySize = 6,
        Bytes     = 7,
    };

    static rstd::size_t HashCanonicalBytes(slice<u8> bytes) {
        rstd::uint64_t hash { 1469598103934665603ull };
        for (auto byte : bytes) {
            hash ^= byte.to_primitive();
            hash *= 1099511628211ull;
        }
        return static_cast<rstd::size_t>(hash);
    }

    void writeType(ValueType type) { writeRawU8(static_cast<rstd::uint8_t>(type)); }

    void writeRawU8(rstd::uint8_t value) { m_bytes.push(u8(value)); }

    void writeRawU32(rstd::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            writeRawU8(static_cast<rstd::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void writeRawU64(rstd::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            writeRawU8(static_cast<rstd::uint8_t>((value >> shift) & 0xffull));
        }
    }

    Vec<u8> m_bytes;
};

struct PipelineCacheKeyEqual {
    bool operator()(const PipelineCacheKey& lhs, const PipelineCacheKey& rhs) const {
        return SamePipelineCacheKey(lhs, rhs);
    }
};

struct RenderPassCacheKeyEqual {
    bool operator()(const RenderPassCacheKey& lhs, const RenderPassCacheKey& rhs) const {
        return SameRenderPassCacheKey(lhs, rhs);
    }
};

struct FramebufferCacheKeyEqual {
    bool operator()(const FramebufferCacheKey& lhs, const FramebufferCacheKey& rhs) const {
        return SameFramebufferCacheKey(lhs, rhs);
    }
};

template<typename T>
inline void WritePipelineScalar(PipelineKeyWriter& writer, T value) {
    writer.writeU64(static_cast<rstd::uint64_t>(value));
}

inline void WriteCacheKey(PipelineKeyWriter& writer, const RenderPassCacheKey& key) {
    writer.writeU64(static_cast<rstd::uint64_t>(key.value));
    writer.writeBytes(key.bytes.as_slice());
}

inline void WritePipelineShaderStages(PipelineKeyWriter& writer, slice<ShaderSpv> stages) {
    struct StageRecord {
        vrento::ShaderType    stage;
        ref<str>              entry_point;
        slice<rstd::uint32_t> spirv;
    };

    Vec<StageRecord> records;
    records.reserve(stages.len());
    for (const auto& stage : stages) {
        records.push(StageRecord {
            .stage       = stage.stage,
            .entry_point = stage.entry_point.as_str(),
            .spirv       = stage.spirv.as_slice(),
        });
    }
    sort_unstable_by(records.as_mut_slice().as_mut_ref(), [](const auto& lhs, const auto& rhs) {
        if (lhs.stage != rhs.stage) {
            return static_cast<int>(lhs.stage) < static_cast<int>(rhs.stage);
        }
        return lhs.entry_point.bytes().cmp(rhs.entry_point.bytes()) < 0;
    });

    writer.writeArraySize(records.len().to_primitive());
    for (const auto& record : records) {
        WritePipelineScalar(writer, record.stage);
        writer.writeString(record.entry_point);
        writer.writeArraySize(record.spirv.len().to_primitive());
        for (auto word : record.spirv) writer.writeU32(word);
    }
}

inline void WritePipelineVertexInput(PipelineKeyWriter&                       writer,
                                     slice<VkVertexInputBindingDescription>   bindings,
                                     slice<VkVertexInputAttributeDescription> attrs) {
    auto sorted_bindings = Vec<VkVertexInputBindingDescription>::from(bindings);
    sort_unstable_by(sorted_bindings.as_mut_slice().as_mut_ref(),
                     [](const auto& lhs, const auto& rhs) {
                         return lhs.binding < rhs.binding;
                     });

    auto sorted_attrs = Vec<VkVertexInputAttributeDescription>::from(attrs);
    sort_unstable_by(sorted_attrs.as_mut_slice().as_mut_ref(),
                     [](const auto& lhs, const auto& rhs) {
                         if (lhs.location != rhs.location) return lhs.location < rhs.location;
                         return lhs.binding < rhs.binding;
                     });

    writer.writeArraySize(sorted_bindings.len().to_primitive());
    for (const auto& binding : sorted_bindings) {
        writer.writeU32(binding.binding);
        writer.writeU32(binding.stride);
        WritePipelineScalar(writer, binding.inputRate);
    }
    writer.writeArraySize(sorted_attrs.len().to_primitive());
    for (const auto& attr : sorted_attrs) {
        writer.writeU32(attr.location);
        writer.writeU32(attr.binding);
        WritePipelineScalar(writer, attr.format);
        writer.writeU32(attr.offset);
    }
}

inline void WritePipelineColorBlend(PipelineKeyWriter&                         writer,
                                    const VkPipelineColorBlendAttachmentState& state) {
    writer.writeBool(state.blendEnable == VK_TRUE);
    WritePipelineScalar(writer, state.srcColorBlendFactor);
    WritePipelineScalar(writer, state.dstColorBlendFactor);
    WritePipelineScalar(writer, state.colorBlendOp);
    WritePipelineScalar(writer, state.srcAlphaBlendFactor);
    WritePipelineScalar(writer, state.dstAlphaBlendFactor);
    WritePipelineScalar(writer, state.alphaBlendOp);
    writer.writeU32(state.colorWriteMask);
}

inline void WritePipelineColorBlendState(PipelineKeyWriter&          writer,
                                         const PipelineResourceDesc& desc) {
    writer.writeU32(desc.color_blend_flags);
    writer.writeBool(desc.logic_op_enable);
    WritePipelineScalar(writer, desc.logic_op);
    for (auto value : desc.blend_constants) writer.writeF32(value);
    writer.writeArraySize(1u);
    WritePipelineColorBlend(writer, desc.color_blend);
}

inline void WritePipelineInputAssembly(PipelineKeyWriter&          writer,
                                       const PipelineResourceDesc& desc) {
    WritePipelineScalar(writer, desc.topology);
    writer.writeBool(desc.primitive_restart_enable);
}

inline void WritePipelineViewportState(PipelineKeyWriter&          writer,
                                       const PipelineResourceDesc& desc) {
    writer.writeU32(desc.viewport_count);
    writer.writeU32(desc.scissor_count);
}

inline void WritePipelineDynamicStates(PipelineKeyWriter& writer, slice<VkDynamicState> states) {
    auto sorted_states = Vec<VkDynamicState>::from(states);
    sort_unstable_by(sorted_states.as_mut_slice().as_mut_ref(), [](auto lhs, auto rhs) {
        return lhs < rhs;
    });
    writer.writeArraySize(sorted_states.len().to_primitive());
    for (auto state : sorted_states) WritePipelineScalar(writer, state);
}

template<typename T>
inline void WritePipelineStencil(PipelineKeyWriter& writer, const T& state) {
    WritePipelineScalar(writer, state.failOp);
    WritePipelineScalar(writer, state.passOp);
    WritePipelineScalar(writer, state.depthFailOp);
    WritePipelineScalar(writer, state.compareOp);
    writer.writeU32(state.compareMask);
    writer.writeU32(state.writeMask);
    writer.writeU32(state.reference);
}

inline void WritePipelineDepthStencil(PipelineKeyWriter&                           writer,
                                      const VkPipelineDepthStencilStateCreateInfo& state) {
    writer.writeU32(state.flags);
    writer.writeBool(state.depthTestEnable == VK_TRUE);
    writer.writeBool(state.depthWriteEnable == VK_TRUE);
    WritePipelineScalar(writer, state.depthCompareOp);
    writer.writeBool(state.depthBoundsTestEnable == VK_TRUE);
    writer.writeBool(state.stencilTestEnable == VK_TRUE);
    WritePipelineStencil(writer, state.front);
    WritePipelineStencil(writer, state.back);
    writer.writeF32(state.minDepthBounds);
    writer.writeF32(state.maxDepthBounds);
}

inline void WritePipelineRaster(PipelineKeyWriter&                            writer,
                                const VkPipelineRasterizationStateCreateInfo& state) {
    writer.writeU32(state.flags);
    writer.writeBool(state.depthClampEnable == VK_TRUE);
    writer.writeBool(state.rasterizerDiscardEnable == VK_TRUE);
    WritePipelineScalar(writer, state.polygonMode);
    WritePipelineScalar(writer, state.cullMode);
    WritePipelineScalar(writer, state.frontFace);
    writer.writeBool(state.depthBiasEnable == VK_TRUE);
    writer.writeF32(state.depthBiasConstantFactor);
    writer.writeF32(state.depthBiasClamp);
    writer.writeF32(state.depthBiasSlopeFactor);
    writer.writeF32(state.lineWidth);
}

inline rstd::uint32_t SampleMaskWordCount(VkSampleCountFlagBits samples) {
    auto count = static_cast<rstd::uint32_t>(samples);
    return (count + 31u) / 32u;
}

inline void WritePipelineMultisample(PipelineKeyWriter&                          writer,
                                     const VkPipelineMultisampleStateCreateInfo& state) {
    writer.writeU32(state.flags);
    WritePipelineScalar(writer, state.rasterizationSamples);
    writer.writeBool(state.sampleShadingEnable == VK_TRUE);
    writer.writeF32(state.minSampleShading);
    const auto mask_word_count = SampleMaskWordCount(state.rasterizationSamples);
    writer.writeArraySize(state.pSampleMask != nullptr ? static_cast<rstd::size_t>(mask_word_count)
                                                       : rstd::size_t { 0 });
    if (state.pSampleMask != nullptr) {
        for (rstd::uint32_t i = 0; i < mask_word_count; ++i) {
            writer.writeU32(state.pSampleMask[i]);
        }
    }
    writer.writeBool(state.alphaToCoverageEnable == VK_TRUE);
    writer.writeBool(state.alphaToOneEnable == VK_TRUE);
}

inline RenderPassResourceDesc MakeRenderPassResourceDesc(const PipelineResourceRequest& request) {
    RenderPassResourceDesc desc {
        .color_format           = request.color_format,
        .depth_format           = VK_FORMAT_D32_SFLOAT,
        .samples                = request.multisample.rasterizationSamples,
        .color_initial_layout   = request.color_initial_layout,
        .color_final_layout     = request.color_final_layout,
        .color_load_op          = request.color_load_op,
        .resolve_final_layout   = request.color_final_layout,
        .depth_final_layout     = request.depth_final_layout,
        .depth_load_op          = request.depth_load_op,
        .depth_store_op         = request.depth_store_op,
        .has_color_attachment   = request.has_color_attachment,
        .has_resolve_attachment = request.has_color_attachment &&
                                  request.multisample.rasterizationSamples != VK_SAMPLE_COUNT_1_BIT,
        .has_depth_attachment   = request.has_depth_attachment,
    };
    if (request.color_load_op == VK_ATTACHMENT_LOAD_OP_LOAD) {
        desc.color_initial_layout =
            request.multisample.rasterizationSamples != VK_SAMPLE_COUNT_1_BIT
                ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (request.depth_load_op == VK_ATTACHMENT_LOAD_OP_LOAD) {
        desc.depth_initial_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    return desc;
}

inline PipelineResourceDesc MakePipelineResourceDesc(const PipelineResourceRequest& request) {
    Vec<ShaderSpv> shader_stages;
    shader_stages.reserve(request.shader_stages.len());
    for (const auto& stage : request.shader_stages) {
        shader_stages.push(stage->clone());
    }

    return PipelineResourceDesc {
        .pipeline_layout          = request.pipeline_layout,
        .vertex_bindings          = request.vertex_bindings.clone(),
        .vertex_attrs             = request.vertex_attrs.clone(),
        .shader_stages            = rstd::move(shader_stages),
        .color_blend              = request.color_blend,
        .color_blend_flags        = request.color_blend_flags,
        .blend_constants          = request.blend_constants,
        .depth                    = request.depth,
        .raster                   = request.raster,
        .depth_clip               = request.depth_clip,
        .multisample              = request.multisample,
        .create_flags             = request.create_flags,
        .topology                 = request.topology,
        .subpass                  = request.subpass,
        .primitive_restart_enable = request.primitive_restart_enable,
        .viewport_count           = request.viewport_count,
        .scissor_count            = request.scissor_count,
        .logic_op_enable          = request.logic_op_enable,
        .logic_op                 = request.logic_op,
        .dynamic_states           = request.dynamic_states.clone(),
        .render_pass              = MakeRenderPassResourceDesc(request),
    };
}

inline FramebufferResourceDesc
MakeFramebufferResourceDesc(const FramebufferResourceRequest& request) {
    return FramebufferResourceDesc {
        .render_pass     = request.render_pass,
        .render_pass_key = request.render_pass_key.clone(),
        .attachments     = request.attachments.clone(),
        .extent          = request.extent,
        .layers          = request.layers,
    };
}

inline void WriteRenderPassDesc(PipelineKeyWriter& writer, const RenderPassResourceDesc& desc) {
    const bool has_resolve = desc.has_resolve_attachment;
    writer.writeBool(desc.has_color_attachment);
    WritePipelineScalar(writer, desc.depth_format);
    WritePipelineScalar(writer, desc.samples);
    if (desc.has_color_attachment) {
        WritePipelineScalar(writer, desc.color_format);
        WritePipelineScalar(writer, desc.color_initial_layout);
        WritePipelineScalar(writer, desc.color_final_layout);
        WritePipelineScalar(writer, desc.color_load_op);
        WritePipelineScalar(writer, desc.color_store_op);
        WritePipelineScalar(writer, desc.color_stencil_load_op);
        WritePipelineScalar(writer, desc.color_stencil_store_op);
        WritePipelineScalar(writer, desc.color_attachment_layout);
        writer.writeBool(has_resolve);
        if (has_resolve) {
            WritePipelineScalar(writer, desc.resolve_initial_layout);
            WritePipelineScalar(writer, desc.resolve_final_layout);
            WritePipelineScalar(writer, desc.resolve_load_op);
            WritePipelineScalar(writer, desc.resolve_store_op);
            WritePipelineScalar(writer, desc.resolve_stencil_load_op);
            WritePipelineScalar(writer, desc.resolve_stencil_store_op);
            WritePipelineScalar(writer, desc.resolve_attachment_layout);
        }
    }
    writer.writeBool(desc.has_depth_attachment);
    if (desc.has_depth_attachment) {
        WritePipelineScalar(writer, desc.depth_initial_layout);
        WritePipelineScalar(writer, desc.depth_final_layout);
        WritePipelineScalar(writer, desc.depth_load_op);
        WritePipelineScalar(writer, desc.depth_store_op);
        WritePipelineScalar(writer, desc.depth_stencil_load_op);
        WritePipelineScalar(writer, desc.depth_stencil_store_op);
        WritePipelineScalar(writer, desc.depth_attachment_layout);
    }
}

inline PipelineCacheKey ToPipelineCacheKey(CanonicalCacheKeyData data) {
    return PipelineCacheKey { .value = data.value, .bytes = rstd::move(data.bytes) };
}

inline RenderPassCacheKey ToRenderPassCacheKey(CanonicalCacheKeyData data) {
    return RenderPassCacheKey { .value = data.value, .bytes = rstd::move(data.bytes) };
}

inline FramebufferCacheKey ToFramebufferCacheKey(CanonicalCacheKeyData data) {
    return FramebufferCacheKey { .value = data.value, .bytes = rstd::move(data.bytes) };
}

inline FramebufferAttachmentIdentity ToFramebufferAttachmentIdentity(CanonicalCacheKeyData data) {
    return FramebufferAttachmentIdentity { .value = data.value, .bytes = rstd::move(data.bytes) };
}

inline RenderPassCacheKey MakeRenderPassCacheKey(const RenderPassResourceDesc& desc);

inline PipelineCacheKey MakePipelineCacheKey(const PipelineResourceDesc& desc) {
    PipelineKeyWriter writer;
    writer.writeString("pipeline-v2"_str);
    writer.writeU32(desc.create_flags);
    writer.writeU32(desc.subpass);
    WriteCacheKey(writer, MakeRenderPassCacheKey(desc.render_pass));
    WritePipelineShaderStages(writer, desc.shader_stages.as_slice());
    writer.writeU64(desc.pipeline_layout.index.to_primitive());
    writer.writeU64(desc.pipeline_layout.generation.to_primitive());
    WritePipelineVertexInput(writer, desc.vertex_bindings.as_slice(), desc.vertex_attrs.as_slice());
    WritePipelineInputAssembly(writer, desc);
    WritePipelineViewportState(writer, desc);
    WritePipelineColorBlendState(writer, desc);
    WritePipelineDepthStencil(writer, desc.depth);
    WritePipelineRaster(writer, desc.raster);
    writer.writeBool(desc.depth_clip.is_some());
    if (desc.depth_clip.is_some()) writer.writeBool(*desc.depth_clip);
    WritePipelineMultisample(writer, desc.multisample);
    WritePipelineDynamicStates(writer, desc.dynamic_states.as_slice());
    return ToPipelineCacheKey(rstd::move(writer).finish());
}

inline PipelineCacheKey MakePipelineCacheKey(const PipelineResourceRequest& request) {
    return MakePipelineCacheKey(MakePipelineResourceDesc(request));
}

inline RenderPassCacheKey MakeRenderPassCacheKey(const RenderPassResourceDesc& desc) {
    PipelineKeyWriter writer;
    writer.writeString("render-pass-v1"_str);
    WriteRenderPassDesc(writer, desc);
    return ToRenderPassCacheKey(rstd::move(writer).finish());
}

inline RenderPassCacheKey MakeRenderPassCacheKey(const PipelineResourceRequest& request) {
    return MakeRenderPassCacheKey(MakeRenderPassResourceDesc(request));
}

inline FramebufferCacheKey MakeFramebufferCacheKey(const FramebufferResourceDesc& desc) {
    PipelineKeyWriter writer;
    writer.writeString("framebuffer-v1"_str);
    WriteCacheKey(writer, desc.render_pass_key);
    writer.writeArraySize(desc.attachments.len().to_primitive());
    for (const auto& attachment : desc.attachments) {
        writer.writeU64(
            static_cast<rstd::uint64_t>(reinterpret_cast<rstd::uintptr_t>(attachment.view)));
        writer.writeU64(static_cast<rstd::uint64_t>(attachment.identity.value));
        writer.writeBytes(attachment.identity.bytes.as_slice());
    }
    writer.writeU32(desc.extent.width);
    writer.writeU32(desc.extent.height);
    writer.writeU32(desc.layers);
    return ToFramebufferCacheKey(rstd::move(writer).finish());
}

inline FramebufferCacheKey MakeFramebufferCacheKey(const FramebufferResourceRequest& request) {
    return MakeFramebufferCacheKey(MakeFramebufferResourceDesc(request));
}

} // namespace vrento::vulkan

export namespace rstd
{

template<>
struct Impl<hash::Hash, vrento::vulkan::PipelineCacheKey>
    : ImplBase<vrento::vulkan::PipelineCacheKey> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().value, state);
    }
};

template<>
struct Impl<hash::Hash, vrento::vulkan::RenderPassCacheKey>
    : ImplBase<vrento::vulkan::RenderPassCacheKey> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().value, state);
    }
};

template<>
struct Impl<hash::Hash, vrento::vulkan::FramebufferCacheKey>
    : ImplBase<vrento::vulkan::FramebufferCacheKey> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().value, state);
    }
};

} // namespace rstd
