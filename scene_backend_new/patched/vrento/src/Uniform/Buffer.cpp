module;
#include <rstd/macro.hpp>
module vrento.uniform_buffer;
import rstd;
import rstd.log;
import vrento.resource;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace vrento
{
namespace
{
bool WriteFloat(mut_ref<u8[]> destination, usize offset, float value) {
    if (offset > destination.len() || usize(sizeof(float)) > destination.len() - offset)
        return false;
    auto bytes =
        slice<u8>::from_raw_parts(reinterpret_cast<const byte*>(&value), usize(sizeof(float)));
    for (usize index {}; index < bytes.len(); ++index) destination[offset + index] = bytes[index];
    return true;
}

float MatrixValue(UniformValueView value, usize matrix, u32 row, u32 column) {
    const auto rows     = usize(value.layout.rows.to_primitive());
    const auto columns  = usize(value.layout.columns.to_primitive());
    const auto base     = matrix * rows * columns;
    const auto row_i    = usize(row.to_primitive());
    const auto column_i = usize(column.to_primitive());
    const auto index    = value.layout.matrix_storage == UniformMatrixStorage::ColumnMajor
                              ? base + column_i * rows + row_i
                              : base + row_i * columns + column_i;
    return value.data[index.to_primitive()];
}

auto SerializeMatrixValue(mut_ref<u8[]> destination, const UniformSlot& slot,
                          UniformValueView value, ShaderMatrixConvention convention,
                          ShaderMatrixAbi matrix_abi) -> Result<empty, UniformBufferUpdateError> {
    if (slot.scalar_kind != ShaderScalarKind::Float || slot.scalar_width != u32(32)) {
        return Err(UniformBufferUpdateError {
            .message = rstd::format("uniform {} matrix scalar is not float32", slot.name.as_str()),
        });
    }
    if (slot.matrix_rows == u32() || slot.matrix_columns == u32() ||
        slot.matrix_major == ShaderMatrixMajor::None || slot.matrix_stride == u32()) {
        return Err(UniformBufferUpdateError {
            .message =
                rstd::format("uniform {} has incomplete matrix reflection", slot.name.as_str()),
        });
    }

    const auto source_rows    = value.layout.rows;
    const auto source_columns = value.layout.columns;
    const auto last           = UniformMatrixCoordinate(
        slot.matrix_rows - u32(1), slot.matrix_columns - u32(1), convention, matrix_abi);
    const bool source_fits     = source_rows > last.row && source_columns > last.column &&
                                 value.layout.array_count >= slot.count;
    const auto source_elements = value.layout.MatrixElements() * value.layout.array_count;
    if (! source_fits || value.size < source_elements) {
        return Err(UniformBufferUpdateError {
            .message = rstd::format("uniform {} matrix shape {}x{}[{}] cannot fill {}x{}[{}]",
                                    slot.name.as_str(),
                                    source_rows,
                                    source_columns,
                                    value.layout.array_count,
                                    slot.matrix_rows,
                                    slot.matrix_columns,
                                    slot.count),
        });
    }

    const auto scalar_size = usize(sizeof(float));
    const auto stride      = usize(slot.matrix_stride.to_primitive());
    const auto major_count = slot.matrix_major == ShaderMatrixMajor::Row
                                 ? usize(slot.matrix_rows.to_primitive())
                                 : usize(slot.matrix_columns.to_primitive());
    const auto minor_count = slot.matrix_major == ShaderMatrixMajor::Row
                                 ? usize(slot.matrix_columns.to_primitive())
                                 : usize(slot.matrix_rows.to_primitive());
    if (stride < minor_count * scalar_size) {
        return Err(UniformBufferUpdateError {
            .message = rstd::format(
                "uniform {} matrix stride {} is too small", slot.name.as_str(), slot.matrix_stride),
        });
    }
    const auto matrix_size = major_count * stride;
    const auto array_stride =
        slot.count > usize(1) ? usize(slot.array_stride.to_primitive()) : matrix_size;
    if (slot.count > usize(1) && array_stride < matrix_size) {
        return Err(UniformBufferUpdateError {
            .message = rstd::format(
                "uniform {} array stride {} is too small", slot.name.as_str(), slot.array_stride),
        });
    }

    const auto write_count = slot.count;
    for (usize matrix {}; matrix < write_count; ++matrix) {
        const auto matrix_base = slot.offset + matrix * array_stride;
        for (u32 row {}; row < slot.matrix_rows; ++row) {
            for (u32 column {}; column < slot.matrix_columns; ++column) {
                const auto source = UniformMatrixCoordinate(row, column, convention, matrix_abi);
                const auto target_offset = slot.matrix_major == ShaderMatrixMajor::Row
                                               ? matrix_base + usize(row.to_primitive()) * stride +
                                                     usize(column.to_primitive()) * scalar_size
                                               : matrix_base +
                                                     usize(column.to_primitive()) * stride +
                                                     usize(row.to_primitive()) * scalar_size;
                if (! WriteFloat(destination,
                                 target_offset,
                                 MatrixValue(value, matrix, source.row, source.column))) {
                    return Err(UniformBufferUpdateError {
                        .message = rstd::format("uniform {} matrix write exceeds block",
                                                slot.name.as_str()),
                    });
                }
            }
        }
    }
    return Ok(empty {});
}

auto SerializeLinearValue(mut_ref<u8[]> destination, const UniformSlot& slot,
                          UniformValueView value) -> Result<empty, UniformBufferUpdateError> {
    const auto scalar_size = usize(sizeof(float));
    if (slot.scalar_kind != ShaderScalarKind::Unknown &&
        (slot.scalar_kind != ShaderScalarKind::Float || slot.scalar_width != u32(32))) {
        return Err(UniformBufferUpdateError {
            .message = rstd::format("uniform {} scalar is not float32", slot.name.as_str()),
        });
    }
    if (slot.scalar_kind != ShaderScalarKind::Unknown && ! value.layout.zero_fill_tail &&
        value.size < slot.LogicalFloatElements()) {
        return Err(UniformBufferUpdateError {
            .message = rstd::format("uniform {} value has {} floats, expected at least {}",
                                    slot.name.as_str(),
                                    value.size,
                                    slot.LogicalFloatElements()),
        });
    }

    if (slot.scalar_kind == ShaderScalarKind::Unknown || slot.array_stride == u32() ||
        slot.count <= usize(1)) {
        const auto count = rstd::cmp::min(value.size, slot.size / scalar_size);
        for (usize index {}; index < count; ++index) {
            if (! WriteFloat(destination,
                             slot.offset + index * scalar_size,
                             value.data[index.to_primitive()])) {
                return Err(UniformBufferUpdateError {
                    .message = rstd::format("uniform {} write exceeds block", slot.name.as_str()),
                });
            }
        }
        return Ok(empty {});
    }

    const auto components = usize(slot.vector_components.to_primitive());
    const auto write_count =
        rstd::cmp::min(slot.count, (value.size + components - usize(1)) / components);
    const auto array_stride = usize(slot.array_stride.to_primitive());
    if (array_stride < components * scalar_size) {
        return Err(UniformBufferUpdateError {
            .message = rstd::format(
                "uniform {} array stride {} is too small", slot.name.as_str(), slot.array_stride),
        });
    }
    for (usize element {}; element < write_count; ++element) {
        for (usize component {}; component < components; ++component) {
            const auto source = element * components + component;
            if (source >= value.size) break;
            if (! WriteFloat(destination,
                             slot.offset + element * array_stride + component * scalar_size,
                             value.data[source.to_primitive()])) {
                return Err(UniformBufferUpdateError {
                    .message =
                        rstd::format("uniform {} array write exceeds block", slot.name.as_str()),
                });
            }
        }
    }
    return Ok(empty {});
}

} // namespace

auto SerializeUniformValue(mut_ref<u8[]> destination, const UniformSlot& slot,
                           UniformValueView value, ShaderMatrixConvention convention,
                           ShaderMatrixAbi matrix_abi) -> Result<empty, UniformBufferUpdateError> {
    if (value.data == nullptr) {
        return Err(UniformBufferUpdateError {
            .message = rstd::format("uniform {} value is empty", slot.name.as_str()),
        });
    }
    if (slot.offset > destination.len() || slot.size > destination.len() - slot.offset) {
        return Err(UniformBufferUpdateError {
            .message =
                rstd::format("uniform {} lies outside destination block", slot.name.as_str()),
        });
    }
    const bool reflected_matrix = slot.matrix_rows != u32() && slot.matrix_columns != u32();
    const bool value_matrix     = value.layout.kind == UniformValueKind::Matrix;
    if (reflected_matrix != value_matrix) {
        return Err(UniformBufferUpdateError {
            .message =
                rstd::format("uniform {} value kind does not match reflection", slot.name.as_str()),
        });
    }
    for (usize index {}; index < slot.size; ++index) destination[slot.offset + index] = u8();
    if (value_matrix) {
        return SerializeMatrixValue(destination, slot, value, convention, matrix_abi);
    }
    return SerializeLinearValue(destination, slot, value);
}

auto CompileUniformBufferLayout(const resource::ShaderArtifactUniformBlock& block)
    -> Result<UniformBufferLayout, UniformBufferUpdateError> {
    UniformBufferLayout layout { .size = block.size };
    layout.slots.reserve(block.members.len());

    for (const auto& member : block.members) {
        const auto offset = rstd::as_cast<usize>(member.offset);
        if (offset > block.size || member.size > block.size - offset) {
            return Err(UniformBufferUpdateError {
                .message = rstd::format(
                    "uniform {} lies outside block {}", member.name.as_str(), block.name.as_str()),
            });
        }
        auto dimensions = Vec<u32>::with_capacity(member.array_dimensions.len());
        for (auto dimension : member.array_dimensions) dimensions.push(u32(dimension));
        layout.slots.push(UniformSlot {
            .name              = member.name.clone(),
            .offset            = offset,
            .size              = member.size,
            .count             = member.count,
            .scalar_kind       = member.scalar_kind,
            .scalar_width      = member.scalar_width,
            .vector_components = member.vector_components,
            .matrix_rows       = member.matrix_rows,
            .matrix_columns    = member.matrix_columns,
            .matrix_stride     = member.matrix_stride,
            .matrix_major      = member.matrix_major,
            .array_stride      = member.array_stride,
            .array_dimensions  = rstd::move(dimensions),
        });
    }

    auto order = Vec<usize>::with_capacity(layout.slots.len());
    for (usize index {}; index < layout.slots.len(); ++index) {
        order.push(usize(index.to_primitive()));
    }
    rstd::slice_::sort_unstable_by(order.as_mut_slice().as_mut_ref(), [&](usize lhs, usize rhs) {
        return layout.slots[lhs].offset < layout.slots[rhs].offset;
    });
    for (usize index { 1 }; index < order.len(); ++index) {
        const auto& previous = layout.slots[order[index - usize(1)]];
        const auto& current  = layout.slots[order[index]];
        if (current.offset < previous.offset + previous.size) {
            rstd_warn("uniform block {} overlaps {} and {}",
                      block.name.as_str(),
                      previous.name.as_str(),
                      current.name.as_str());
        }
    }
    return Ok(rstd::move(layout));
}

} // namespace vrento
