module vrento.uniform_value;
import rstd;
using namespace rstd::prelude;
namespace vrento
{
void UniformValue::fromSlice(slice<value_type> values) noexcept {
    m_size    = values.len();
    m_layout  = UniformValueLayout::Linear(values.len());
    m_dynamic = values.len() > m_value.len();
    if (m_dynamic) {
        m_dynamic_value.clear();
        m_dynamic_value.reserve(values.len());
        for (usize index {}; index < values.len(); ++index) {
            m_dynamic_value.push_back(values[index]);
        }
    } else {
        for (usize index {}; index < values.len(); ++index) m_value[index] = values[index];
    }
}

} // namespace vrento
