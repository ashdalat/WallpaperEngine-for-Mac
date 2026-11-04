module;

#include <cstdint>
#if defined(__APPLE__)
#include <CoreVideo/CoreVideo.h>
#endif

export module wescene.timer:thread_timer;
import rstd;

using namespace rstd::prelude;
using rstd::sync::Condvar;
using rstd::sync::Mutex;
using rstd::thread::JoinHandle;
using rstd::time::Duration;

export namespace owe
{
class ThreadTimer {
public:
    using Callback = Box<dyn<FnMut<void()>>>;
    explicit ThreadTimer(Callback callback);
    ~ThreadTimer();
    void Start();
    void Stop();
    bool Running() const;
    void SetInterval(rstd::time::Duration);

private:
    struct State {
        bool     running {};
        rstd::time::Duration interval { rstd::time::Duration::from_millis(u64(1)) };
        std::uint64_t tick_count { 0 };
        double refresh_hz { 60.0 };
    };
    Callback                 m_callback;
    Mutex<empty>             m_op_mutex { empty {} };
    Option<JoinHandle<void>> m_timer_thread;
    Mutex<State>             m_state { State {} };
    Condvar                  m_condition;

#if defined(__APPLE__)
public:
    // Called by the display-link callback (a free function).
    void wemTick(double refresh_hz);

private:
    CVDisplayLinkRef m_display_link { nullptr };
#endif
};
} // namespace owe
