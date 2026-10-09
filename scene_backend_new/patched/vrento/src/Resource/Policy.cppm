export module vrento.resource_registry:policy;
import rstd;
import vrento.vulkan;

export namespace vrento::resource_registry
{

using namespace rstd::prelude;

enum class MemoryPressure
{
    Normal,
    Elevated,
    Critical,
};

class MemoryBudgetPolicy {
public:
    void Refresh(ref<dyn<vulkan::MemoryBudgetSource>> source) { Refresh(source->MemoryBudget()); }

    void Refresh(vulkan::MemoryBudgetSnapshot snapshot) {
        m_snapshot = snapshot;
        if (m_snapshot.budget == 0) {
            m_pressure = MemoryPressure::Normal;
        } else if (m_snapshot.usage >= m_snapshot.budget - m_snapshot.budget / 20) {
            m_pressure = MemoryPressure::Critical;
        } else if (m_snapshot.usage >= m_snapshot.budget - m_snapshot.budget / 5) {
            m_pressure = MemoryPressure::Elevated;
        } else {
            m_pressure = MemoryPressure::Normal;
        }
    }

    auto Snapshot() const noexcept -> vulkan::MemoryBudgetSnapshot { return m_snapshot; }
    auto Pressure() const noexcept -> MemoryPressure { return m_pressure; }
    bool ShouldEvictTransient() const noexcept { return m_pressure != MemoryPressure::Normal; }

    void Reset() {
        m_snapshot = {};
        m_pressure = MemoryPressure::Normal;
    }

private:
    vulkan::MemoryBudgetSnapshot m_snapshot;
    MemoryPressure               m_pressure { MemoryPressure::Normal };
};

} // namespace vrento::resource_registry
