export module vrento.uniform_buffer;
export import vrento.uniform_value;
export import vrento.shader_types;
import vrento.resource;
import rstd;

using namespace rstd::prelude;

export namespace vrento
{
struct UniformBufferUpdateError {
    String message;
};

struct UniformSlot {
    String            name;
    usize             offset { 0 };
    usize             size { 0 };
    usize             count { 1 };
    ShaderScalarKind  scalar_kind { ShaderScalarKind::Unknown };
    u32               scalar_width {};
    u32               vector_components { u32(1) };
    u32               matrix_rows {};
    u32               matrix_columns {};
    u32               matrix_stride {};
    ShaderMatrixMajor matrix_major { ShaderMatrixMajor::None };
    u32               array_stride {};
    Vec<u32>          array_dimensions;

    usize LogicalFloatElements() const {
        if (scalar_kind == ShaderScalarKind::Unknown) return size / usize(sizeof(float));
        if (matrix_rows != u32() && matrix_columns != u32()) {
            return usize(matrix_rows.to_primitive()) * usize(matrix_columns.to_primitive()) * count;
        }
        return usize(vector_components.to_primitive()) * count;
    }
};

struct UniformBufferLayout {
    usize            size { 0 };
    Vec<UniformSlot> slots;
};

auto CompileUniformBufferLayout(const resource::ShaderArtifactUniformBlock&)
    -> Result<UniformBufferLayout, UniformBufferUpdateError>;

auto SerializeUniformValue(mut_ref<u8[]> destination, const UniformSlot&, UniformValueView,
                           ShaderMatrixConvention,
                           ShaderMatrixAbi matrix_abi = ShaderMatrixAbi::NativeSpirv)
    -> Result<empty, UniformBufferUpdateError>;

struct MatrixCoordinate {
    u32 row;
    u32 column;
};

MatrixCoordinate UniformMatrixCoordinate(u32 row, u32 column, ShaderMatrixConvention convention,
                                         ShaderMatrixAbi matrix_abi) {
    const auto shader_row    = matrix_abi == ShaderMatrixAbi::Hlsl ? column : row;
    const auto shader_column = matrix_abi == ShaderMatrixAbi::Hlsl ? row : column;
    return convention == ShaderMatrixConvention::RowVector
               ? MatrixCoordinate { shader_column, shader_row }
               : MatrixCoordinate { shader_row, shader_column };
}

} // namespace vrento

export namespace rstd
{

template<>
struct Impl<fmt::Display, vrento::UniformBufferUpdateError>
    : ImplBase<vrento::UniformBufferUpdateError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make("{}", this->self().message));
    }
};

template<>
struct Impl<fmt::Debug, vrento::UniformBufferUpdateError>
    : ImplBase<vrento::UniformBufferUpdateError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(
            fmt::Arguments::make("UniformBufferUpdateError({})", this->self().message));
    }
};

template<>
struct Impl<error::Error, vrento::UniformBufferUpdateError>
    : DefaultInImpl<error::Error, vrento::UniformBufferUpdateError> {};

} // namespace rstd

static_assert(rstd::Impled<vrento::UniformBufferUpdateError, rstd::error::Error>);
