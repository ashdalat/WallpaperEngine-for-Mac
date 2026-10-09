export module vrento.shader_types;
import rstd;

using rstd::vec::Vec;

export namespace vrento
{

using ShaderCode = Vec<rstd::uint32_t>;

enum class ShaderType
{
    VERTEX,
    GEOMETRY,
    FRAGMENT
};
enum class ShaderScalarKind
{
    Unknown,
    Float,
    SignedInteger,
    UnsignedInteger,
    Boolean
};
enum class ShaderMatrixMajor
{
    None,
    Row,
    Column
};
enum class ShaderMatrixConvention
{
    ColumnVector,
    RowVector
};
enum class ShaderMatrixAbi
{
    NativeSpirv,
    Hlsl
};

} // namespace vrento
