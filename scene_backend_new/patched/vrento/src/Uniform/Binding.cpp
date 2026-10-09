module vrento.uniform_binding;
import rstd;
import vrento.resource;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace vrento
{
namespace
{
class SourceBindingCompiler {
public:
    SourceBindingCompiler(const UniformBufferLayout& layout, BoundUniformSource& source,
                          ShaderMatrixConvention convention, ShaderMatrixAbi matrix_abi)
        : m_layout(layout), m_source(source), m_convention(convention), m_matrix_abi(matrix_abi) {}

    auto Bind(UniformOutputId output, ref<str> shader_member, UniformValueShape shape)
        -> Result<bool, UniformError> {
        bool matched = false;
        for (usize index {}; index < m_layout.slots.len(); ++index) {
            const auto& slot = m_layout.slots[index];
            if (slot.name.as_str() != shader_member) continue;
            if (slot.size % usize(sizeof(float)) != usize()) {
                return Err(UniformError {
                    .message =
                        rstd::format("uniform {} has non-float size {}", shader_member, slot.size),
                });
            }
            if (slot.scalar_kind != ShaderScalarKind::Unknown &&
                (shape.scalar != UniformScalarType::Float32 ||
                 slot.scalar_kind != ShaderScalarKind::Float || slot.scalar_width != u32(32))) {
                return Err(UniformError {
                    .message =
                        rstd::format("uniform {} scalar type does not match source", shader_member),
                });
            }
            const bool matrix = slot.matrix_rows != u32() && slot.matrix_columns != u32();
            if (matrix != (shape.kind == UniformValueKind::Matrix)) {
                return Err(UniformError {
                    .message = rstd::format("uniform {} value kind does not match reflection",
                                            shader_member),
                });
            }
            if (matrix) {
                const auto last = UniformMatrixCoordinate(slot.matrix_rows - u32(1),
                                                          slot.matrix_columns - u32(1),
                                                          m_convention,
                                                          m_matrix_abi);
                if (shape.rows <= last.row || shape.columns <= last.column ||
                    slot.count < shape.min_array_count || slot.count > shape.max_array_count) {
                    return Err(UniformError {
                        .message =
                            rstd::format("uniform {} matrix shape mismatch: reflected {}x{}[{}], "
                                         "source "
                                         "accepts at least {}x{}[{}..{}]",
                                         shader_member,
                                         slot.matrix_rows,
                                         slot.matrix_columns,
                                         slot.count,
                                         shape.rows,
                                         shape.columns,
                                         shape.min_array_count,
                                         shape.max_array_count),
                    });
                }
            } else {
                const auto elements =
                    u32(static_cast<rstd::uint32_t>(slot.LogicalFloatElements().to_primitive()));
                if ((shape.min_elements != u32() && elements < shape.min_elements) ||
                    (shape.max_elements != u32() && elements > shape.max_elements)) {
                    return Err(UniformError {
                        .message = rstd::format("uniform {} shape mismatch: reflected {} floats, "
                                                "source expects {}..{}",
                                                shader_member,
                                                elements,
                                                shape.min_elements,
                                                shape.max_elements),
                    });
                }
            }
            bool exists = false;
            for (const auto& binding : m_source.outputs) {
                if (binding.output == output && binding.slot_index == index) {
                    exists = true;
                    break;
                }
            }
            if (! exists) {
                m_source.outputs.push(BoundUniformOutput {
                    .output     = output,
                    .slot_index = index,
                });
            }
            matched = true;
        }
        return Ok(matched);
    }

private:
    const UniformBufferLayout& m_layout;
    BoundUniformSource&        m_source;
    ShaderMatrixConvention     m_convention;
    ShaderMatrixAbi            m_matrix_abi;
};

class SourceValueWriter {
public:
    SourceValueWriter(mut_ref<u8[]> data, const UniformBufferLayout& layout,
                      const BoundUniformSource& source, ShaderMatrixConvention convention,
                      ShaderMatrixAbi abi)
        : m_data(data), m_layout(layout), m_source(source), m_convention(convention), m_abi(abi) {}
    bool Wants(UniformOutputId output) const {
        for (const auto& binding : m_source.outputs)
            if (binding.output == output) return true;
        return false;
    }
    auto Write(UniformOutputId output, UniformValueView value) -> Result<empty, UniformError> {
        bool wrote = false;
        for (const auto& binding : m_source.outputs) {
            if (binding.output != output) continue;
            if (binding.slot_index >= m_layout.slots.len())
                return Err(UniformError { "invalid uniform slot"_Str });
            auto result = SerializeUniformValue(
                m_data, m_layout.slots[binding.slot_index], value, m_convention, m_abi);
            if (result.is_err())
                return Err(UniformError { rstd::move(result).unwrap_err_unchecked().message });
            wrote = true;
        }
        if (! wrote) return Err(UniformError { "uniform output is not bound"_Str });
        return Ok(empty {});
    }

private:
    mut_ref<u8[]>              m_data;
    const UniformBufferLayout& m_layout;
    const BoundUniformSource&  m_source;
    ShaderMatrixConvention     m_convention;
    ShaderMatrixAbi            m_abi;
};

} // namespace

auto PrepareUniformSource(const UniformBufferLayout& layout, UniformSourceOwner owner, i32 priority,
                          ShaderMatrixConvention convention, ShaderMatrixAbi abi)
    -> Result<Option<BoundUniformSource>, UniformBufferUpdateError> {
    auto               source = owner->as_ref();
    BoundUniformSource bound { .owner = rstd::move(owner), .source = source, .priority = priority };
    SourceBindingCompiler compiler(layout, bound, convention, abi);
    auto                  sink      = dyn<UniformBindingSink>::from_ref(compiler);
    auto                  described = bound.source->Describe(sink.as_mut_ref());
    if (described.is_err())
        return Err(
            UniformBufferUpdateError { rstd::move(described).unwrap_err_unchecked().message });
    if (bound.outputs.is_empty()) return Ok(None<BoundUniformSource>());
    bound.lease = bound.source->AcquireBindingLease();
    return Ok(Some(rstd::move(bound)));
}

UniformBinding::UniformBinding(resource::BufferUseHandle buffer, UniformBufferLayout layout,
                               Vec<BoundUniformSource> sources, ShaderMatrixConvention convention,
                               ShaderMatrixAbi abi)
    : m_buffer(buffer),
      m_layout(rstd::move(layout)),
      m_sources(rstd::move(sources)),
      m_base(Vec<u8>::with_capacity(m_layout.size)),
      m_convention(convention),
      m_abi(abi) {
    m_base.resize(m_layout.size, u8());
}

auto UniformBinding::SetParameters(slice<UniformParameter> parameters)
    -> Result<empty, UniformBufferUpdateError> {
    auto data = Vec<u8>::with_capacity(m_layout.size);
    data.resize(m_layout.size, u8());
    for (const auto& parameter : parameters) {
        for (const auto& slot : m_layout.slots) {
            if (slot.name.as_str() != parameter.name) continue;
            auto result = SerializeUniformValue(
                data.as_mut_slice().as_mut_ref(), slot, parameter.value, m_convention, m_abi);
            if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
        }
    }
    m_base  = rstd::move(data);
    m_dirty = true;
    return Ok(empty {});
}

auto UniformBinding::Update(ref<dyn<UniformUpdateContext>>              context,
                            mut_ref<dyn<resource::BufferContentWriter>> buffers)
    -> Result<empty, UniformBufferUpdateError> {
    auto versions = Vec<u64>::with_capacity(m_sources.len());
    bool changed  = m_dirty;
    for (auto& source : m_sources) {
        auto version = source.source->Version(context);
        changed      = changed || ! source.evaluated || source.version != version;
        versions.push(rstd::move(version));
    }
    if (! changed) return Ok(empty {});
    auto data = m_base.clone();
    for (auto& source : m_sources) {
        SourceValueWriter writer(
            data.as_mut_slice().as_mut_ref(), m_layout, source, m_convention, m_abi);
        auto sink      = dyn<UniformValueSink>::from_ref(writer);
        auto evaluated = source.source->Evaluate(context, sink.as_mut_ref());
        if (evaluated.is_err())
            return Err(
                UniformBufferUpdateError { rstd::move(evaluated).unwrap_err_unchecked().message });
    }
    auto updated = buffers->UpdateBuffer(m_buffer, data.as_slice());
    if (updated.is_err())
        return Err(UniformBufferUpdateError { rstd::move(updated).unwrap_err_unchecked().message });
    // Failed evaluation or upload must leave every source eligible for retry.
    for (usize index {}; index < m_sources.len(); ++index) {
        m_sources[index].version   = versions[index];
        m_sources[index].evaluated = true;
    }
    m_dirty = false;
    return Ok(empty {});
}

} // namespace vrento
