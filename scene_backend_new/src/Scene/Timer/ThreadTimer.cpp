module;

#include <cstdint>
#include <cstdlib>
#include <pthread.h>
#include <sys/qos.h>

#if defined(__APPLE__)
#include <CoreVideo/CoreVideo.h>
#endif

module wescene.timer;
import rstd;

using namespace rstd::prelude;
using namespace owe;

ThreadTimer::ThreadTimer(Callback callback): m_callback(rstd::move(callback)) {}
ThreadTimer::~ThreadTimer() { Stop(); }

bool ThreadTimer::Running() const { return m_state.lock().unwrap()->running; }

void ThreadTimer::SetInterval(rstd::time::Duration interval) {
    auto state      = m_state.lock().unwrap();
    state->interval = interval;
}

#if defined(__APPLE__)

// macOS pacing: a sleep-based timer thread suffers kernel timer
// coalescing (a 16.6 ms wait wakes every ~20 ms at background QoS,
// and even USER_INTERACTIVE QoS still slips ~1 ms), which shows up
// as an uneven ~49-57 fps and choppy pointer-driven effects.
// CVDisplayLink fires exactly at the display refresh — the same
// clock the swapchain presents against — so frame spacing is even
// and the required fps is met exactly (up to the refresh rate).
// The interval still gates firing, so a 30 fps requirement fires
// every second refresh tick.

static CVReturn wem_display_link_callback(
    CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp* output_time,
    CVOptionFlags, CVOptionFlags*, void* userdata)
{
    // The refresh rate comes from the OUTPUT timestamp —
    // videoTimeScale / videoRefreshPeriod — so ProMotion (120 Hz)
    // or any external display works unchanged. (The input
    // timestamp's refresh fields are not always populated.)
    double refresh_hz = 0.0;
    if (output_time != nullptr && output_time->videoRefreshPeriod > 0)
        refresh_hz = double(output_time->videoTimeScale) / double(output_time->videoRefreshPeriod);
    static_cast<owe::ThreadTimer*>(userdata)->wemTick(refresh_hz);
    return kCVReturnSuccess;
}

void ThreadTimer::wemTick(double refresh_hz) {
    std::uint64_t interval_us;
    {
        auto state = m_state.lock().unwrap();
        if (! state->running) return;
        // Remember the refresh rate across callbacks: some
        // timestamps may not carry it.
        if (refresh_hz > 0.0) {
            static bool wem_printed = false;
            if (! wem_printed) {
                wem_printed = true;
                rstd::io::eprintln("WEM: display link refresh_hz = {}", refresh_hz);
            }
            state->refresh_hz = refresh_hz;
        }
        refresh_hz = state->refresh_hz;
        // Fire once every N refresh ticks, N = round(refresh/fps) —
        // counting frames instead of wall time keeps the spacing
        // perfectly even no matter how the callback thread is
        // scheduled. A required fps above the refresh caps at it.
        interval_us = rstd::as_cast<std::uint64_t>(state->interval.as_micros());
        auto required_fps = interval_us != 0u
            ? double(1'000'000u) / double(interval_us)
            : 60.0;
        auto stride = std::uint64_t(refresh_hz / required_fps + 0.5);
        if (stride == 0u) stride = 1u;
        ++state->tick_count;
        if (state->tick_count % stride != 0u) return;
    }
    // Callback may call SetInterval — never invoked holding the lock.
    m_callback->operator()();
}

void ThreadTimer::Start() {
    auto operation = m_op_mutex.lock().unwrap();
    {
        auto state = m_state.lock().unwrap();
        if (state->running) return;
        state->running = true;
    }
    if (CVDisplayLinkCreateWithActiveCGDisplays(&m_display_link) != kCVReturnSuccess)
        return;
    CVDisplayLinkSetOutputCallback(
        m_display_link, wem_display_link_callback, this);
    CVDisplayLinkStart(m_display_link);
    // Valid only after Start.
    {
        auto state = m_state.lock().unwrap();
        CVTime refresh = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(m_display_link);
        if (refresh.timeScale != 0 && refresh.timeValue != 0)
            state->refresh_hz = double(refresh.timeScale) / double(refresh.timeValue);
        rstd::io::eprintln("WEM: display link refresh_hz = {}", state->refresh_hz);
    }
}

void ThreadTimer::Stop() {
    auto operation = m_op_mutex.lock().unwrap();
    if (m_display_link == nullptr) return;
    {
        auto state = m_state.lock().unwrap();
        state->running = false;
    }
    CVDisplayLinkStop(m_display_link);
    CVDisplayLinkRelease(m_display_link);
    m_display_link = nullptr;
}

#else

void ThreadTimer::Start() {
    auto operation = m_op_mutex.lock().unwrap();
    {
        auto state = m_state.lock().unwrap();
        if (state->running) return;
        state->running = true;
    }
    auto thread = rstd::thread::spawn([this] {
        for (;;) {
            {
                auto state = m_state.lock().unwrap();
                if (! state->running) break;
                m_condition.wait_timeout_while(state, state->interval, [](const State& value) {
                    return value.running;
                });
                if (! state->running) break;
            }
            m_callback->operator()();
        }
    });
    if (thread.is_err()) {
        auto state     = m_state.lock().unwrap();
        state->running = false;
    }
    m_timer_thread = Some(rstd::move(thread).unwrap());
}

void ThreadTimer::Stop() {
    auto operation = m_op_mutex.lock().unwrap();
    if (! m_timer_thread) return;
    if (rstd::thread::current_id() == m_timer_thread->thread().id())
        rstd::panic { "ThreadTimer cannot stop from its callback" };
    {
        auto state     = m_state.lock().unwrap();
        state->running = false;
    }
    m_condition.notify_all();
    rstd::move(m_timer_thread.take().unwrap()).join().unwrap();
}

#endif
