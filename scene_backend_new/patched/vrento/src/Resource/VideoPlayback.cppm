export module vrento.video_playback;
import rstd;

using rstd::f64;

export namespace vrento
{

struct VideoPlaybackSnapshot {
    bool      playing { true };
    f64       rate { 1.0 };
    rstd::u64 seek_sequence {};
    f64       seek_seconds {};
};

// The shared provider must synchronize access from control and render threads.
struct VideoPlayback {
    using Trait                  = VideoPlayback;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = VideoPlayback;

        auto Snapshot() const -> VideoPlaybackSnapshot { return rstd::trait_call<0>(this); }

        void PublishTime(f64 current, rstd::Option<f64> duration) const {
            rstd::trait_call<1>(this, current, duration);
        }
    };

    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::Snapshot, &T::PublishTime>;
};

} // namespace vrento
