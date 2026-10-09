export module vrento.uniform_value;
import rstd;

using namespace rstd::prelude;

export namespace vrento
{
using UniformValueStorage = rstd::array<float, 16>;

enum class UniformScalarType : rstd::uint8_t
{
    Float32,
};

enum class UniformValueKind : rstd::uint8_t
{
    Linear,
    Matrix,
};

enum class UniformMatrixStorage : rstd::uint8_t
{
    RowMajor,
    ColumnMajor,
};

struct UniformValueLayout {
    UniformScalarType    scalar { UniformScalarType::Float32 };
    UniformValueKind     kind { UniformValueKind::Linear };
    u32                  rows { u32(1) };
    u32                  columns { u32(1) };
    usize                array_count { usize(1) };
    UniformMatrixStorage matrix_storage { UniformMatrixStorage::ColumnMajor };
    bool                 zero_fill_tail { false };

    static auto Linear(usize elements) -> UniformValueLayout {
        return { .columns = rstd::as_cast<u32>(elements) };
    }

    static auto Matrix(u32 rows, u32 columns, usize array_count, UniformMatrixStorage storage)
        -> UniformValueLayout {
        return {
            .kind           = UniformValueKind::Matrix,
            .rows           = rows,
            .columns        = columns,
            .array_count    = array_count,
            .matrix_storage = storage,
        };
    }

    usize MatrixElements() const {
        return usize(rows.to_primitive()) * usize(columns.to_primitive());
    }

    friend bool operator==(const UniformValueLayout&, const UniformValueLayout&) = default;
};

struct UniformValueView {
    const float*       data { nullptr };
    usize              size {};
    UniformValueLayout layout;
};

class UniformValue {
public:
    using value_type = float;

    UniformValue()  = default;
    ~UniformValue() = default;

    UniformValue(const UniformValue& other) noexcept
        : m_dynamic(other.m_dynamic),
          m_value(other.m_value),
          m_dynamic_value(CloneDynamic(other.m_dynamic_value)),
          m_size(other.m_size),
          m_layout(other.m_layout) {}
    UniformValue& operator=(const UniformValue& other) noexcept {
        if (this == &other) return *this;
        m_dynamic       = other.m_dynamic;
        m_value         = other.m_value;
        m_dynamic_value = CloneDynamic(other.m_dynamic_value);
        m_size          = other.m_size;
        m_layout        = other.m_layout;
        return *this;
    }

    UniformValue(const value_type& value) noexcept {
        fromSlice(slice<value_type>::from_raw_parts(rstd::addressof(value), usize(1)));
    }
    UniformValue(slice<value_type> values) noexcept { fromSlice(values); }
    template<typename Range>
    UniformValue(const Range& range) noexcept {
        const auto len = [&]() -> usize {
            if constexpr (requires { range.len(); })
                return range.len();
            else
                return usize(range.size());
        }();
        fromSlice(slice<value_type>::from_raw_parts(range.data(), len));
    }
    UniformValue(const value_type* ptr, usize num) noexcept {
        fromSlice(slice<value_type>::from_raw_parts(ptr, num));
    }
    explicit UniformValue(UniformValueView view) noexcept {
        fromSlice(slice<value_type>::from_raw_parts(view.data, view.size));
        m_layout = view.layout;
    }

    static UniformValue fromZeroExtended(slice<float> values) {
        auto value                    = UniformValue(values.as_raw_ptr(), values.len());
        value.m_layout.zero_fill_tail = true;
        return value;
    }
    static UniformValue fromMatrixArray(const value_type* ptr, u32 rows, u32 columns, usize count,
                                        UniformMatrixStorage storage) {
        auto value =
            UniformValue(ptr, usize(rows.to_primitive()) * usize(columns.to_primitive()) * count);
        value.m_layout = UniformValueLayout::Matrix(rows, columns, count, storage);
        return value;
    }

    const auto& operator[](usize index) const { return value()[index]; }
    auto& operator[](usize index) { return m_dynamic ? m_dynamic_value[index] : m_value[index]; }

    auto  data() const noexcept { return value().as_raw_ptr(); }
    usize size() const noexcept { return m_size; }
    auto  View() const noexcept -> UniformValueView { return { data(), size(), m_layout }; }

    void setSize(usize size) noexcept { m_size = rstd::cmp::min(size, value().len()); }

private:
    static auto CloneDynamic(const Vec<value_type>& source) noexcept -> Vec<value_type> {
        auto result = Vec<value_type>::with_capacity(source.len());
        for (const auto value : source) result.push_back(value);
        return result;
    }

    void fromSlice(slice<value_type> values) noexcept;

    slice<value_type> value() const noexcept {
        if (m_dynamic) return m_dynamic_value.as_slice();
        return m_value.as_slice();
    }

    bool                m_dynamic { false };
    UniformValueStorage m_value;
    Vec<value_type>     m_dynamic_value;
    usize               m_size {};
    UniformValueLayout  m_layout;
};

} // namespace vrento
