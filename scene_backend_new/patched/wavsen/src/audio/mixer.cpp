module wavsen.audio;

import rstd;
import wavsen.audio.core;
import :mixer;

using namespace rstd::prelude;

namespace wavsen::audio
{

namespace
{

// Adapter exposing a SoundStream to AudioDevice's IPullChannel interface.
class StreamPullChannel : public IPullChannel {
public:
    explicit StreamPullChannel(Box<dyn<SoundStreamObject>> ss): ss_(rstd::move(ss)) {}

    auto next_pcm(void* dst, u32 frames) -> u64 override {
        return ss_->stream().next_pcm(dst, frames);
    }
    void pass_desc(const DeviceDesc& d) override {
        ss_->stream().pass_desc({ d.channels, d.sample_rate });
    }

private:
    Box<dyn<SoundStreamObject>> ss_;
};

} // namespace

class SoundManager::Impl {
public:
    explicit Impl(AudioClientIdentity identity_): identity(rstd::move(identity_)) {}

    void apply(u32 fade_ms = u32()) {
        if (! activated || shutting_down) return;
        (void)device.apply(AudioDeviceDesiredState {
            .generation            = generation,
            .active                = ! muted,
            .playing               = playing,
            .identity              = identity.clone(),
            .volume                = volume,
            .muted                 = false,
            .volume_scale          = volume_scale,
            .volume_scale_revision = volume_scale_revision,
            .volume_scale_fade_ms  = fade_ms,
        });
    }

    AudioDevice         device;
    AudioClientIdentity identity;
    AudioDeviceState    observed_state { AudioDeviceState::Idle };
    u64                 generation;
    u64                 stream_revision;
    u64                 volume_scale_revision;
    f32                 volume { f32(1.0f) };
    f32                 volume_scale { f32(1.0f) };
    bool                muted {};
    bool                playing {};
    bool                activated {};
    bool                shutting_down {};
};

SoundManager::SoundManager(AudioClientIdentity identity)
    : impl_(Box<Impl>::make(rstd::move(identity))) {}
SoundManager::~SoundManager() = default;

void SoundManager::mount(Box<dyn<SoundStreamObject>> ss) {
    auto channel = Box<StreamPullChannel>::make(rstd::move(ss));
    (void)impl_->device.mount(Box<dyn<PullChannelObject>>::from_raw(
                                  dyn<PullChannelObject>::from_ptr(rstd::move(channel).into_raw())),
                              impl_->stream_revision);
}

void SoundManager::unmount_all() {
    ++impl_->stream_revision;
    (void)impl_->device.unmount_all(impl_->stream_revision);
}

void SoundManager::activate(AudioDeviceEventSink sink) {
    if (impl_->activated || impl_->shutting_down) return;
    impl_->device.set_event_sink(Some(rstd::move(sink)));
    impl_->activated = true;
    ++impl_->generation;
    impl_->apply();
}

void SoundManager::on_device_event(AudioDeviceEvent event) {
    if (event.generation != impl_->generation && event.state != AudioDeviceState::Stopped) return;
    impl_->observed_state = event.state;
}

void SoundManager::shutdown() {
    if (impl_->shutting_down) return;
    impl_->shutting_down = true;
    impl_->device.shutdown();
}

bool SoundManager::set_identity(AudioClientIdentity identity) {
    if (impl_->shutting_down) return false;
    impl_->identity = rstd::move(identity);
    if (impl_->activated) ++impl_->generation;
    impl_->apply();
    return true;
}

bool SoundManager::is_inited() const {
    return impl_->observed_state == AudioDeviceState::ReadyPaused ||
           impl_->observed_state == AudioDeviceState::ReadyPlaying;
}

void SoundManager::play() {
    impl_->playing = true;
    impl_->apply();
}
void SoundManager::pause() {
    impl_->playing = false;
    impl_->apply();
}

auto SoundManager::volume() const -> f32 { return impl_->volume; }
bool SoundManager::muted() const { return impl_->muted; }
void SoundManager::set_volume(f32 value) {
    impl_->volume = value;
    impl_->apply();
}
auto SoundManager::volume_scale() const -> f32 { return impl_->volume_scale; }
void SoundManager::set_volume_scale(f32 value) { set_volume_scale(value, u32()); }
void SoundManager::set_volume_scale(f32 value, u32 fade_ms) {
    impl_->volume_scale = value;
    ++impl_->volume_scale_revision;
    impl_->apply(fade_ms);
}

void SoundManager::set_muted(bool m) {
    if (impl_->muted == m) return;
    impl_->muted = m;
    if (impl_->activated) ++impl_->generation;
    impl_->apply();
}

} // namespace wavsen::audio
