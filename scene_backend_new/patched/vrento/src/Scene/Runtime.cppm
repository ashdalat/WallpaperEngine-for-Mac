export module vrento.runtime;
import rstd;

using namespace rstd::prelude;

export namespace vrento
{

struct FrameContext {
    u64 index { 0 };
    f64 elapsed { 0.0 };
    f64 delta { 0.0 };
    u64 revision { 1 };
};

struct RuntimeSystem {
    using Trait                  = RuntimeSystem;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = RuntimeSystem;

        void Update(ref<FrameContext> frame) { rstd::trait_call<0>(this, frame); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::Update>;
};

enum class RuntimeSchedule
{
    FrameAdvance,
    BeforeRender,
};

class Runtime {
public:
    const FrameContext& Frame() const noexcept { return m_frame; }

    void Register(Box<dyn<RuntimeSystem>> system,
                  RuntimeSchedule         schedule = RuntimeSchedule::FrameAdvance) {
        if (schedule == RuntimeSchedule::BeforeRender) {
            m_before_render.push(rstd::move(system));
        } else {
            m_frame_advance.push(rstd::move(system));
        }
    }

    template<typename T>
    void RegisterSystem(T system, RuntimeSchedule schedule = RuntimeSchedule::FrameAdvance) {
        Register(Box<dyn<RuntimeSystem>>::make(rstd::move(system)), schedule);
    }

    void Advance(f64 delta) {
        m_frame.delta = delta;
        m_frame.elapsed += delta;
        ++m_frame.index;
        ++m_frame.revision;
        if (m_frame.revision == u64()) m_frame.revision = u64(1);

        UpdateSystems(m_frame_advance);
    }

    void BeforeRender() { UpdateSystems(m_before_render); }

private:
    void UpdateSystems(Vec<Box<dyn<RuntimeSystem>>>& systems) {
        auto frame = ref<FrameContext>::from_raw_parts(rstd::addressof(m_frame));
        for (auto& system : systems) system->Update(frame);
    }

    FrameContext                 m_frame;
    Vec<Box<dyn<RuntimeSystem>>> m_frame_advance;
    Vec<Box<dyn<RuntimeSystem>>> m_before_render;
};

} // namespace vrento
