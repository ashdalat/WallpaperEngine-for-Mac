export module vrento.trace;
import rstd;

using rstd::sync::Arc;

export namespace vrento
{

using namespace rstd::prelude;

enum class RenderEvent
{
    Instance,
    Device,
    Swapchain,
    Resources,
    UploadSubmit,
    UploadWait,
    Scopes,
    GraphCompile,
    ProgramBuild,
    ResourceRequests,
    TexturePlan,
    TextureDecode,
    TextureUpload,
    ResourcePrepare,
};

// Callbacks run synchronously on the emitting thread; shared observers synchronize their state.
struct RenderObserver {
    using Trait                  = RenderObserver;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = RenderObserver;
        auto Begin(RenderEvent event) const -> u64 { return rstd::trait_call<0>(this, event); }
        void End(u64 token) const { rstd::trait_call<1>(this, token); }
    };

    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::Begin, &T::End>;
};

class RenderTrace {
public:
    RenderTrace() = default;
    explicit RenderTrace(Arc<dyn<RenderObserver>> observer)
        : m_observer(Some(rstd::move(observer))) {}
    RenderTrace(const RenderTrace& other)
        : m_observer(other.m_observer.is_some() ? Some(other.m_observer->clone()) : None()) {}
    auto operator=(const RenderTrace& other) -> RenderTrace& {
        if (this != &other) {
            m_observer = other.m_observer.is_some() ? Some(other.m_observer->clone()) : None();
        }
        return *this;
    }
    RenderTrace(RenderTrace&&)                    = default;
    auto operator=(RenderTrace&&) -> RenderTrace& = default;

    auto Begin(RenderEvent event) const -> Option<u64> {
        return m_observer.is_some() ? Some((*m_observer)->Begin(event)) : None();
    }
    void End(u64 token) const {
        if (m_observer.is_some()) (*m_observer)->End(token);
    }

private:
    Option<Arc<dyn<RenderObserver>>> m_observer;
};

class RenderScopeGuard {
public:
    RenderScopeGuard(RenderTrace trace, RenderEvent event)
        : m_trace(rstd::move(trace)), m_token(m_trace.Begin(event)) {}
    ~RenderScopeGuard() { Finish(); }
    RenderScopeGuard(const RenderScopeGuard&)                    = delete;
    auto operator=(const RenderScopeGuard&) -> RenderScopeGuard& = delete;
    RenderScopeGuard(RenderScopeGuard&& other)
        : m_trace(rstd::move(other.m_trace)), m_token(other.m_token.take()) {}
    auto operator=(RenderScopeGuard&& other) -> RenderScopeGuard& {
        if (this != &other) {
            Finish();
            m_trace = rstd::move(other.m_trace);
            m_token = other.m_token.take();
        }
        return *this;
    }

private:
    void Finish() {
        if (m_token.is_some()) m_trace.End(m_token.take().unwrap());
    }

    RenderTrace m_trace;
    Option<u64> m_token;
};

inline auto RenderScope(RenderTrace trace, RenderEvent event) -> RenderScopeGuard {
    return RenderScopeGuard(rstd::move(trace), event);
}

} // namespace vrento
