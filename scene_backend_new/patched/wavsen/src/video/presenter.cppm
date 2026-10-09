export module wavsen.video:presenter;

import rstd;

using namespace rstd::prelude;
using rstd::time::Duration;
using rstd::time::Instant;

export namespace wavsen::video
{

struct PresentationSchedule {
    bool            present { true };
    Option<Instant> deadline;
};

// PTS → wall-clock pacing helper for the video plugin's render loop.
//
// Behavior (default — no external clock):
//   - First frame primes the baseline (t0_wall = now; t0_pts = pts).
//   - Subsequent frames sleep until t0_wall + (pts - t0_pts).
//   - PTS that drops backwards (loop wrap-around) re-baselines silently.
//   - Frames more than `max_lag` behind schedule are dropped (return
//     false) so a slow consumer or stalled decoder doesn't snowball.
//   - Frames more than `max_sleep` ahead of schedule treat the PTS as
//     a forward discontinuity and re-baseline rather than sleep forever.
//   - Frames with pts<0 (PTS unavailable) skip pacing entirely.
//
// External-clock mode (set_external_clock(fn) with fn returning the
// current audio playback PTS in seconds, NaN if not yet primed):
//   - skew = pts - clock_fn()
//   - skew < -max_lag      ⇒ drop (return false)
//   - skew >  max_sleep    ⇒ treat as discontinuity, present immediately
//   - skew >  0            ⇒ sleep_for(skew)
//   - else                 ⇒ present immediately
//   - If clock_fn returns NaN, fall back to the wall-clock algorithm.
class Presenter {
public:
    using TimePoint = Instant;

    explicit Presenter(Duration max_lag   = Duration::from_millis(u64(250)),
                       Duration max_sleep = Duration::from_secs(u64(1)))
        : max_lag_(max_lag), max_sleep_(max_sleep) {}

    // Force the next call to re-prime the baseline. Useful when the
    // caller knows the stream just looped or the decoder was reset.
    void reset() {
        primed_               = false;
        t0_pts_               = f64(-1.0);
        external_drop_streak_ = u32();
    }

    template<typename F>
    void set_external_clock(F&& clock_fn)
        requires rstd::Impled<rstd::mtp::rm_cvf<F>, rstd::FnMut<f64()>>
    {
        clock_fn_ = Some(Box<dyn<FnMut<f64()>>>::make(rstd::forward<F>(clock_fn)));
    }

    void clear_external_clock() { clock_fn_ = None(); }

    auto set_playback_rate(f64 rate) -> bool {
        if (! rate.is_finite() || rate <= f64()) return false;
        if (rate == playback_rate_) return true;
        playback_rate_ = rate;
        reset();
        return true;
    }

    auto playback_rate() const -> f64 { return playback_rate_; }

    PresentationSchedule schedule_frame(f64 pts_seconds) {
        if (pts_seconds < f64()) return {};

        if (clock_fn_.is_some()) {
            const f64 now_pts = clock_fn_->as_mut_ptr()->operator()();
            if (! now_pts.is_nan()) {
                const f64 skew        = (pts_seconds - now_pts) / playback_rate_;
                const f64 max_lag_s   = f64(max_lag_.as_secs_f64());
                const f64 max_sleep_s = f64(max_sleep_.as_secs_f64());
                if (skew < -max_lag_s) {
                    // Video far behind the audio clock. Normally drop,
                    // but if we've been dropping for a long stretch the
                    // decoder can't catch up to where audio is — let one
                    // frame through to break the infinite-drop loop and
                    // re-converge on a closer PTS over the next frames.
                    if (++external_drop_streak_ >= kExternalDropStreakReset) {
                        external_drop_streak_ = u32();
                        return {};
                    }
                    return { .present = false };
                }
                external_drop_streak_ = u32();
                if (skew > max_sleep_s) return {};
                if (skew > f64()) {
                    return {
                        .deadline =
                            Some(TimePoint::now() + Duration::from_secs_f64(skew.to_primitive())),
                    };
                }
                return {};
            }
            // External clock not primed yet — fall through to wall-clock.
        }

        const auto now = TimePoint::now();
        if (! primed_) {
            t0_wall_ = now;
            t0_pts_  = pts_seconds;
            primed_  = true;
            return {};
        }
        if (pts_seconds < t0_pts_) {
            t0_wall_ = now;
            t0_pts_  = pts_seconds;
            return {};
        }

        const auto delta =
            Duration::from_secs_f64(((pts_seconds - t0_pts_) / playback_rate_).to_primitive());
        const auto target = t0_wall_ + delta;

        if (target + max_lag_ < now) {
            t0_wall_ = now;
            t0_pts_  = pts_seconds;
            return { .present = false };
        }
        if (now < target) {
            if (target - now > max_sleep_) {
                t0_wall_ = now;
                t0_pts_  = pts_seconds;
                return {};
            }
            return { .deadline = Some<TimePoint>(target) };
        }
        return {};
    }

    static void wait_until(const PresentationSchedule& schedule) {
        if (schedule.deadline.is_none()) return;
        const auto now = TimePoint::now();
        if (now < *schedule.deadline) rstd::thread::sleep(*schedule.deadline - now);
    }

    bool present_frame(f64 pts_seconds) {
        const auto schedule = schedule_frame(pts_seconds);
        if (! schedule.present) return false;
        wait_until(schedule);
        return true;
    }

private:
    // External-clock drop streak before forcing one present-through.
    // 30 ≈ 1 s @ 30 fps — long enough to ride out a real backpressure
    // burst, short enough that A/V doesn't drift past noticeable.
    static constexpr u32 kExternalDropStreakReset { 30 };

    Duration                       max_lag_;
    Duration                       max_sleep_;
    TimePoint                      t0_wall_ {};
    f64                            t0_pts_ { -1.0 };
    bool                           primed_ { false };
    f64                            playback_rate_ { 1.0 };
    u32                            external_drop_streak_ {};
    Option<Box<dyn<FnMut<f64()>>>> clock_fn_;
};

} // namespace wavsen::video
