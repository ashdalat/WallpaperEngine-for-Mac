export module wavsen.audio:mixer;

import rstd;
import wavsen.audio.core;

using namespace rstd::prelude;

export namespace wavsen::audio
{

// A mountable PCM source. Data callback fills `frames` interleaved frames
// of the device's negotiated format (f32 little-endian) and channel count.
class SoundStream {
public:
    struct Desc {
        u32 channels;
        u32 sample_rate;
    };

    SoundStream()                              = default;
    virtual ~SoundStream()                     = default;
    SoundStream(const SoundStream&)            = delete;
    SoundStream& operator=(const SoundStream&) = delete;

    virtual auto next_pcm(void* dst, u32 frames) -> u64 = 0;
    virtual void pass_desc(const Desc&)                 = 0;

    // 3D-audio hooks — no-op in 0.1; spatial backend will override these in
    // a future iteration. Coordinates are listener-relative (right-handed,
    // metres). See plans/wavsen-...md "推后" section.
    virtual void set_position(f32 /*x*/, f32 /*y*/, f32 /*z*/) {}
    virtual void set_listener_position(f32 /*x*/, f32 /*y*/, f32 /*z*/) {}
};

struct SoundStreamObject {
    using Trait                  = SoundStreamObject;
    static constexpr bool direct = false;
    template<typename Self, typename = void>
    struct Api {
        using Trait = SoundStreamObject;
        auto stream() -> SoundStream& { return rstd::trait_call<0>(this); }
    };
    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::stream>;
};
// Main-loop-owned playback policy. The AudioDevice backend owns native
// resources and emits events that the caller must relay back to this owner.
class SoundManager {
public:
    explicit SoundManager(AudioClientIdentity identity = {});
    ~SoundManager();
    SoundManager(const SoundManager&)            = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    void mount(Box<dyn<SoundStreamObject>>);
    void unmount_all();

    void activate(AudioDeviceEventSink);
    void on_device_event(AudioDeviceEvent);
    void shutdown();
    auto set_identity(AudioClientIdentity identity) -> bool;
    auto is_inited() const -> bool;
    void play();
    void pause();

    auto volume() const -> f32;
    auto muted() const -> bool;
    void set_volume(f32 value);
    void set_muted(bool m);
    auto volume_scale() const -> f32;
    void set_volume_scale(f32 value);
    void set_volume_scale(f32 value, u32 fade_ms);

private:
    class Impl;
    Box<Impl> impl_;
};

} // namespace wavsen::audio

export namespace rstd
{
template<typename T>
    requires requires(T& value) { static_cast<wavsen::audio::SoundStream&>(value); }
struct Impl<wavsen::audio::SoundStreamObject, T> : ImplBase<T> {
    auto stream() -> wavsen::audio::SoundStream& { return this->self(); }
};
} // namespace rstd
