export module vrento.resource:shader;
import rstd;
import vrento.shader_types;
import :handle;

export namespace vrento::resource
{

using namespace rstd::prelude;

struct ShaderDefinitionId {
    u32 index { u32::MAX };
    u64 generation {};

    bool Valid() const noexcept { return index != u32::MAX && generation != u64(); }

    friend bool operator==(const ShaderDefinitionId&, const ShaderDefinitionId&) = default;
};

struct ShaderRequest {
    String             name;
    ShaderDefinitionId source;
    u64                content_version { u64(1) };

    auto clone() const -> ShaderRequest {
        return ShaderRequest {
            .name            = name.clone(),
            .source          = source,
            .content_version = content_version,
        };
    }

    friend bool operator==(const ShaderRequest&, const ShaderRequest&) = default;
};

} // namespace vrento::resource

export namespace rstd
{

template<>
struct Impl<hash::Hash, vrento::resource::ShaderRequest>
    : ImplBase<vrento::resource::ShaderRequest> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().name, state);
        hash::hash_into(this->self().source.index, state);
        hash::hash_into(this->self().source.generation, state);
        hash::hash_into(this->self().content_version, state);
    }
};

} // namespace rstd

export namespace vrento::resource
{

struct ShaderArtifactStage {
    ShaderType stage { ShaderType::VERTEX };
    String     entry_point;
    ShaderCode code;

    auto clone() const -> ShaderArtifactStage {
        return ShaderArtifactStage {
            .stage       = stage,
            .entry_point = entry_point.clone(),
            .code        = code.clone(),
        };
    }
};

struct ShaderArtifactUniformMember {
    String            name;
    u32               offset {};
    usize             size {};
    usize             count { usize(1) };
    ShaderScalarKind  scalar_kind { ShaderScalarKind::Unknown };
    u32               scalar_width {};
    u32               vector_components { u32(1) };
    u32               matrix_rows {};
    u32               matrix_columns {};
    u32               matrix_stride {};
    ShaderMatrixMajor matrix_major { ShaderMatrixMajor::None };
    u32               array_stride {};
    Vec<u32>          array_dimensions;

    auto clone() const -> ShaderArtifactUniformMember {
        auto dimensions = Vec<u32>::with_capacity(array_dimensions.len());
        for (auto dimension : array_dimensions) dimensions.push(u32(dimension));
        return ShaderArtifactUniformMember {
            .name              = name.clone(),
            .offset            = offset,
            .size              = size,
            .count             = count,
            .scalar_kind       = scalar_kind,
            .scalar_width      = scalar_width,
            .vector_components = vector_components,
            .matrix_rows       = matrix_rows,
            .matrix_columns    = matrix_columns,
            .matrix_stride     = matrix_stride,
            .matrix_major      = matrix_major,
            .array_stride      = array_stride,
            .array_dimensions  = rstd::move(dimensions),
        };
    }
};

struct ShaderArtifactUniformBlock {
    String name;
    usize  size {};
    u32    set {};
    u32    binding {};
    enum class Scope : rstd::uint8_t
    {
        Shared,
        Local,
    } scope { Scope::Local };
    u64                              identity {};
    Vec<ShaderArtifactUniformMember> members;

    auto clone() const -> ShaderArtifactUniformBlock {
        auto cloned = Vec<ShaderArtifactUniformMember>::with_capacity(members.len());
        for (const auto& member : members) cloned.push(member.clone());
        return ShaderArtifactUniformBlock {
            .name     = name.clone(),
            .size     = size,
            .set      = set,
            .binding  = binding,
            .scope    = scope,
            .identity = identity,
            .members  = rstd::move(cloned),
        };
    }
};

struct ShaderArtifactDescriptorBinding {
    String name;
    u32    set {};
    u32    binding {};
    u32    descriptor_type {};
    u32    descriptor_count { u32(1) };
    u32    stage_flags {};

    auto clone() const -> ShaderArtifactDescriptorBinding {
        return ShaderArtifactDescriptorBinding {
            .name             = name.clone(),
            .set              = set,
            .binding          = binding,
            .descriptor_type  = descriptor_type,
            .descriptor_count = descriptor_count,
            .stage_flags      = stage_flags,
        };
    }
};

struct ShaderArtifactDescriptorSet {
    u32                                  set {};
    bool                                 push_descriptor { false };
    u64                                  identity {};
    Vec<ShaderArtifactDescriptorBinding> bindings;

    auto clone() const -> ShaderArtifactDescriptorSet {
        auto cloned = Vec<ShaderArtifactDescriptorBinding>::with_capacity(bindings.len());
        for (const auto& binding : bindings) cloned.push(binding.clone());
        return ShaderArtifactDescriptorSet {
            .set             = set,
            .push_descriptor = push_descriptor,
            .identity        = identity,
            .bindings        = rstd::move(cloned),
        };
    }
};

struct ShaderArtifactVertexInput {
    String name;
    u32    location {};
    u32    format {};

    auto clone() const -> ShaderArtifactVertexInput {
        return ShaderArtifactVertexInput {
            .name     = name.clone(),
            .location = location,
            .format   = format,
        };
    }
};

struct ShaderArtifact {
    ShaderDefinitionId                   source;
    u64                                  content_version { u64(1) };
    ShaderMatrixConvention               matrix_convention { ShaderMatrixConvention::ColumnVector };
    ShaderMatrixAbi                      matrix_abi { ShaderMatrixAbi::NativeSpirv };
    Vec<ShaderArtifactStage>             stages;
    Vec<ShaderArtifactUniformBlock>      uniform_blocks;
    Vec<ShaderArtifactDescriptorBinding> descriptor_bindings;
    Vec<ShaderArtifactDescriptorSet>     descriptor_sets;
    Vec<ShaderArtifactVertexInput>       vertex_inputs;

    auto clone() const -> ShaderArtifact {
        auto cloned_stages = Vec<ShaderArtifactStage>::with_capacity(stages.len());
        for (const auto& stage : stages) cloned_stages.push(stage.clone());
        auto cloned_blocks = Vec<ShaderArtifactUniformBlock>::with_capacity(uniform_blocks.len());
        for (const auto& block : uniform_blocks) cloned_blocks.push(block.clone());
        auto cloned_bindings =
            Vec<ShaderArtifactDescriptorBinding>::with_capacity(descriptor_bindings.len());
        for (const auto& binding : descriptor_bindings) cloned_bindings.push(binding.clone());
        auto cloned_sets = Vec<ShaderArtifactDescriptorSet>::with_capacity(descriptor_sets.len());
        for (const auto& set : descriptor_sets) cloned_sets.push(set.clone());
        auto cloned_inputs = Vec<ShaderArtifactVertexInput>::with_capacity(vertex_inputs.len());
        for (const auto& input : vertex_inputs) cloned_inputs.push(input.clone());
        return ShaderArtifact {
            .source              = source,
            .content_version     = content_version,
            .matrix_convention   = matrix_convention,
            .matrix_abi          = matrix_abi,
            .stages              = rstd::move(cloned_stages),
            .uniform_blocks      = rstd::move(cloned_blocks),
            .descriptor_bindings = rstd::move(cloned_bindings),
            .descriptor_sets     = rstd::move(cloned_sets),
            .vertex_inputs       = rstd::move(cloned_inputs),
        };
    }
};

} // namespace vrento::resource
