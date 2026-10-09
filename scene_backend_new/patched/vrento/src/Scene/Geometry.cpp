module;
#include <rstd/macro.hpp>
module vrento.geometry;
import rstd;
using namespace rstd::prelude;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
namespace vrento
{
namespace
{
u64 next_storage_generation() {
    static Atomic<u64> next { u64(1) };
    auto               value = next.fetch_add(u64(1), Ordering::Relaxed);
    rstd_assert(value != u64());
    return value;
}
} // namespace

auto VertexArray::BufferView() const -> GeometryBufferView {
    return {
        .bytes    = slice<u8>::from_raw_parts(reinterpret_cast<const byte*>(Data()), DataSizeOf()),
        .capacity = CapacitySizeOf(),
        .stride   = OneSizeOf(),
        .count    = VertexCount(),
        .data_generation    = m_generation,
        .storage_generation = m_storage_generation,
    };
}

auto IndexArray::BufferView() const -> GeometryBufferView {
    return {
        .bytes    = slice<u8>::from_raw_parts(reinterpret_cast<const byte*>(Data()), DataSizeOf()),
        .capacity = CapacitySizeof(),
        .stride   = Unit_Byte_Size,
        .count    = RenderDataCount(),
        .data_generation    = m_generation,
        .storage_generation = m_storage_generation,
    };
}

usize VertexArray::TypeCount(VertexType t) {
    switch (t) {
    case VertexType::FLOAT1:
    case VertexType::UINT1: return usize(1);
    case VertexType::FLOAT2:
    case VertexType::UINT2: return usize(2);
    case VertexType::FLOAT3:
    case VertexType::UINT3: return usize(3);
    case VertexType::FLOAT4:
    case VertexType::UINT4: return usize(4);
    }
    return usize(1);
}

usize VertexArray::RealAttributeSize(const VertexArray::VertexAttribute& attr) {
    return attr.padding ? usize(4) : TypeCount(attr.type);
}

auto VertexWriter::AppendZeroedVertex() noexcept -> Option<mut_ref<float[]>> {
    if (m_stride == usize() || m_written >= Capacity()) {
        m_overflowed = true;
        return None();
    }

    auto start = m_written * m_stride;
    auto vertex =
        mut_ref<float[]>::from_raw_parts(m_data.as_raw_ptr() + start.to_primitive(), m_stride);
    for (usize index {}; index < m_stride; ++index) vertex[index] = 0.0f;
    ++m_written;
    return Some(vertex);
}

auto VertexArray::FinishVertexRewrite(const VertexWriter& writer) noexcept -> VertexWriteResult {
    m_size = writer.Written() * m_oneSize;
    BumpDataGeneration();
    return {
        .vertex_count = writer.Written(),
        .capacity     = writer.Capacity(),
        .overflowed   = writer.Overflowed(),
    };
}

VertexArray::VertexArray(Vec<VertexAttribute> attrs, const usize count)
    : m_attributes(rstd::move(attrs)), m_storage_generation(next_storage_generation()) {
    for (const auto& el : m_attributes) {
        m_oneSize += VertexArray::RealAttributeSize(el);
    }
    auto capacity = m_oneSize * count;
    m_data        = Vec<float>::with_capacity(capacity);
    for (usize i {}; i < capacity; ++i) m_data.push(0.0f);
}

VertexArray::VertexArray(VertexArray&& other) noexcept
    : m_attributes(rstd::move(other.m_attributes)),
      m_data(rstd::move(other.m_data)),
      m_oneSize(other.m_oneSize),
      m_size(other.m_size),
      m_id(other.m_id),
      m_generation(other.m_generation),
      m_storage_generation(other.m_storage_generation) {
    other.m_size               = usize();
    other.m_oneSize            = usize();
    other.m_storage_generation = u64();
}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept {
    if (this == &other) return *this;
    m_attributes               = rstd::move(other.m_attributes);
    m_data                     = rstd::move(other.m_data);
    m_oneSize                  = other.m_oneSize;
    m_size                     = other.m_size;
    m_id                       = other.m_id;
    m_generation               = other.m_generation;
    m_storage_generation       = other.m_storage_generation;
    other.m_size               = usize();
    other.m_oneSize            = usize();
    other.m_storage_generation = u64();
    return *this;
}

bool VertexArray::AddVertex(const float* data) {
    if (data == nullptr || m_oneSize == usize() || m_oneSize > m_data.len() - m_size) return false;
    usize pos {};
    usize mpos = m_size;
    for (const auto& el : m_attributes) {
        auto typeSize = VertexArray::TypeCount(el.type);
        for (usize component {}; component < typeSize; ++component)
            m_data[mpos + component] = data[(pos + component).to_primitive()];
        pos += typeSize;
        mpos += VertexArray::RealAttributeSize(el);
    }
    m_size += m_oneSize;
    BumpDataGeneration();
    return true;
}

bool VertexArray::SetVertex(ref<str> name, slice<float> data) noexcept {
    usize offset {};
    for (const auto& el : m_attributes) {
        if (el.name.as_str() == name) {
            const auto typeSize = VertexArray::TypeCount(el.type);
            if (data.len() % typeSize != usize()) return false;
            const auto count = data.len() / typeSize;
            if (! TrySetSize(count * m_oneSize)) return false;

            for (usize i {}; i < data.len(); i += typeSize) {
                auto num = i / typeSize;
                for (usize component {}; component < typeSize; ++component) {
                    m_data[offset + num * m_oneSize + component] = data[i + component];
                }
            }
            BumpDataGeneration();
            return true;
        } else
            offset += RealAttributeSize(el);
    }
    return false;
}

bool VertexArray::SetVertexs(usize index, slice<float> data) noexcept {
    if (m_oneSize == usize() || index > m_data.len() / m_oneSize) return false;
    usize start = index * m_oneSize;
    if (data.len() > m_data.len() - start) return false;
    if (TrySetSize(start + data.len())) {
        for (usize source_index {}; source_index < data.len(); ++source_index) {
            m_data[start + source_index] = data[source_index];
        }
        BumpDataGeneration();
        return true;
    }
    return false;
}

void VertexArray::ResetSize() noexcept {
    if (m_size == usize()) return;
    m_size = usize();
    BumpDataGeneration();
}

bool VertexArray::TrySetSize(usize new_size) noexcept {
    rstd_assert(new_size <= m_data.len());
    if (new_size > m_data.len()) {
        return false;
    }
    if (new_size > m_size) m_size = new_size;
    return true;
}

auto VertexArray::AttributeOffset(ref<str> name) const -> Option<usize> {
    Option<usize> result;
    usize         offset {};
    for (const auto& attr : m_attributes) {
        if (attr.name.as_str() == name) result = Some(offset);
        offset += VertexArray::RealAttributeSize(attr) * usize(sizeof(float));
    }
    return result;
}

IndexArray::IndexArray(usize index_count)
    : m_data(Vec<uint32_t>::with_capacity(index_count)),
      m_storage_generation(next_storage_generation()) {
    for (usize i {}; i < index_count; ++i) m_data.push(0);
}
IndexArray::IndexArray(slice<uint32_t> data)
    : m_data(Vec<uint32_t>::with_capacity(data.len())),
      m_size(data.len()),
      m_storage_generation(next_storage_generation()) {
    for (uint32_t value : data) m_data.push(rstd::move(value));
}

IndexArray::IndexArray(IndexArray&& other) noexcept
    : m_data(rstd::move(other.m_data)),
      m_size(other.m_size),
      m_render_size(other.m_render_size),
      m_id(other.m_id),
      m_generation(other.m_generation),
      m_storage_generation(other.m_storage_generation) {
    other.m_size               = usize();
    other.m_storage_generation = u64();
}

bool IndexArray::IncreaseCheckSet(usize nsize) {
    if (nsize > CapacitySizeof()) return false;
    if (nsize > DataSizeOf()) {
        m_size = nsize / Unit_Byte_Size + (nsize % Unit_Byte_Size == usize() ? usize() : usize(1));
    }
    return true;
}

} // namespace vrento
