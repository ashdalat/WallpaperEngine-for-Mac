export module vrento.geometry;
export import vrento.vertex_types;
import rstd;
using namespace rstd::prelude;
export namespace vrento
{
// Bytes borrow their array until mutation, move, or destruction; never retain across frames.
struct GeometryBufferView {
    slice<u8> bytes;
    usize     capacity {};
    usize     stride {};
    usize     count {};
    u64       data_generation {};
    u64       storage_generation {};
};

struct GeometryView {
    Vec<GeometryBufferView>    vertices;
    Option<GeometryBufferView> index;
    bool                       dynamic { false };
};

class IndexArray {
    constexpr static usize Unit_Byte_Size { sizeof(uint32_t) };

public:
    IndexArray(const IndexArray&)                    = delete;
    auto operator=(const IndexArray&) -> IndexArray& = delete;
    IndexArray(usize indexCount);
    IndexArray(slice<uint32_t> data);

    IndexArray(IndexArray&&) noexcept;
    ~IndexArray() = default;

    void Assign(usize index, slice<uint32_t> data) {
        if (index > m_data.len() || data.len() > m_data.len() - index) return;
        if (! IncreaseCheckSet((index + data.len()) * Unit_Byte_Size)) return;
        for (usize source_index {}; source_index < data.len(); ++source_index) {
            m_data[index + source_index] = data[source_index];
        }
        BumpDataGeneration();
    }

    const uint32_t* Data() const { return m_data.is_empty() ? nullptr : m_data.begin(); }
    usize           DataCount() const { return m_size; }
    usize           DataSizeOf() const { return m_size * Unit_Byte_Size; }

    usize RenderDataCount() const noexcept {
        return m_render_size > m_size ? m_size : m_render_size;
    }
    void SetRenderDataCount(usize val) noexcept { m_render_size = val; }

    usize CapacityCount() const { return m_data.len(); }
    usize CapacitySizeof() const { return m_data.len() * Unit_Byte_Size; }
    u64   DataGeneration() const { return m_generation; }
    auto  BufferView() const -> GeometryBufferView;

    u32  ID() const { return m_id; }
    void SetID(u32 id) { m_id = id; }

private:
    bool IncreaseCheckSet(usize size);
    void BumpDataGeneration() noexcept { ++m_generation; }

    Vec<uint32_t> m_data;
    usize         m_size { 0 };

    usize m_render_size { usize::MAX };

    u32 m_id { u32::MAX };
    u64 m_generation { 1 };
    u64 m_storage_generation;
};

struct VertexWriteResult {
    usize vertex_count {};
    usize capacity {};
    bool  overflowed { false };
};

class VertexWriter {
public:
    VertexWriter(const VertexWriter&) = delete;
    VertexWriter(VertexWriter&&)      = delete;
    auto AppendZeroedVertex() noexcept -> Option<mut_ref<float[]>>;

    usize Stride() const noexcept { return m_stride; }
    usize Capacity() const noexcept { return m_capacity; }
    usize Written() const noexcept { return m_written; }
    bool  Overflowed() const noexcept { return m_overflowed; }

private:
    friend class VertexArray;

    VertexWriter(mut_ref<float[]> data, usize stride) noexcept
        : m_data(data),
          m_stride(stride),
          m_capacity(stride == usize() ? usize() : data.len() / stride) {}

    mut_ref<float[]> m_data;
    usize            m_stride {};
    usize            m_capacity {};
    usize            m_written {};
    bool             m_overflowed { false };
};

class VertexArray {
public:
    struct VertexAttribute {
        String     name;
        VertexType type;
        bool       padding { true };

        auto clone() const -> VertexAttribute { return { name.clone(), type, padding }; }
        void clone_from(const VertexAttribute& source) { *this = source.clone(); }
    };

    VertexArray(const VertexArray&)                    = delete;
    auto operator=(const VertexArray&) -> VertexArray& = delete;
    VertexArray(Vec<VertexAttribute> attrs, usize count);
    VertexArray(initializer_list<VertexAttribute> attrs, usize count)
        : VertexArray(Vec<VertexAttribute>::from(slice<VertexAttribute>::from_raw_parts(
                          attrs.begin(), usize(attrs.size()))),
                      count) {}
    ~VertexArray() = default;

    VertexArray(VertexArray&&) noexcept;
    VertexArray& operator=(VertexArray&&) noexcept;

    bool AddVertex(const float*);
    bool SetVertex(ref<str> name, slice<float> data) noexcept;
    bool SetVertexs(usize index, slice<float> data) noexcept;

    template<typename Fill>
    [[nodiscard]] auto RewriteVertices(Fill&& fill) -> VertexWriteResult {
        VertexWriter writer(m_data.as_mut_slice().as_mut_ref(), m_oneSize);
        try {
            fill(writer);
        } catch (...) {
            (void)FinishVertexRewrite(writer);
            throw;
        }
        return FinishVertexRewrite(writer);
    }

    // Drops the active size to zero without releasing capacity. Subsequent
    // SetVertexs calls regrow it.
    void ResetSize() noexcept;

    const float* Data() const { return m_data.is_empty() ? nullptr : m_data.begin(); }
    usize        DataSize() const { return m_size; }
    usize        DataSizeOf() const { return m_size * usize(sizeof(float)); }
    usize        VertexCount() const { return m_oneSize == usize() ? usize() : m_size / m_oneSize; }
    usize        CapacitySize() const { return m_data.len(); }
    usize        CapacitySizeOf() const { return m_data.len() * usize(sizeof(float)); }
    usize        OneSize() const { return m_oneSize; }
    usize        OneSizeOf() const { return m_oneSize * usize(sizeof(float)); }
    u64          DataGeneration() const { return m_generation; }
    auto         BufferView() const -> GeometryBufferView;

    auto Attributes() const -> slice<VertexAttribute> { return m_attributes.as_slice(); }
    auto AttributeOffset(ref<str> name) const -> Option<usize>;

    u32  ID() const { return m_id; }
    void SetID(u32 id) { m_id = id; }

    static usize TypeCount(VertexType);
    static usize RealAttributeSize(const VertexAttribute&);

private:
    bool TrySetSize(usize) noexcept;
    auto FinishVertexRewrite(const VertexWriter&) noexcept -> VertexWriteResult;
    void BumpDataGeneration() noexcept { ++m_generation; }

    Vec<VertexAttribute> m_attributes;

    Vec<float> m_data;
    usize      m_oneSize { 0 };
    usize      m_size { 0 };

    u32 m_id { u32::MAX };
    u64 m_generation { 1 };
    u64 m_storage_generation;
};

} // namespace vrento
