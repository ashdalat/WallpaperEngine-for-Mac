module wavsen.audio;

import rstd;
import rstd.log;
import :byte_stream;
import wavsen.audio.core; // AudioDevice, IPullChannel, DeviceDesc
import :file;             // StreamDecoder
import :av_sync;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;

namespace wavsen::audio
{

namespace
{

// Internal pull channel for AvPlayer. Calls into StreamDecoder and
// stamps the master-clock anchor on the first frame after open / seek.
class AvPullChannel : public IPullChannel {
public:
    AvPullChannel(StreamDecoder* decoder, Atomic<f64>* pts_at_anchor,
                  Atomic<u64>* device_pos_at_anchor, Atomic<bool>* needs_reanchor,
                  Atomic<bool>* anchored, Atomic<f64>* desired_rate,
                  Atomic<u64>* desired_rate_revision, Atomic<u64>* applied_rate_revision,
                  Atomic<f64>* rate_at_anchor, Atomic<f64>* desired_seek_seconds,
                  Atomic<u64>* desired_seek_revision, Box<dyn<FnMut<u64()>>> device_pos_now)
        : decoder_(decoder),
          pts_at_anchor_(pts_at_anchor),
          device_pos_at_anchor_(device_pos_at_anchor),
          needs_reanchor_(needs_reanchor),
          anchored_(anchored),
          desired_rate_(desired_rate),
          desired_rate_revision_(desired_rate_revision),
          applied_rate_revision_(applied_rate_revision),
          rate_at_anchor_(rate_at_anchor),
          desired_seek_seconds_(desired_seek_seconds),
          desired_seek_revision_(desired_seek_revision),
          device_pos_now_(rstd::move(device_pos_now)) {}

    auto next_pcm(void* dst, u32 frames) -> u64 override {
        const auto desired_seek_revision = desired_seek_revision_->load(Ordering::Acquire);
        if (desired_seek_revision != applied_seek_revision_) {
            anchored_->store(false, Ordering::Release);
            needs_reanchor_->store(true, Ordering::Release);
            const auto seconds     = desired_seek_seconds_->load(Ordering::Acquire);
            seek_ready_            = decoder_->seek_to(seconds);
            applied_seek_revision_ = desired_seek_revision;
            if (! seek_ready_) {
                rstd::log::error("wavsen::audio::AvPlayer: failed to seek to {}", seconds);
            }
        }
        if (! seek_ready_) return u64();

        const auto desired_revision = desired_rate_revision_->load(Ordering::Acquire);
        if (desired_revision != applied_rate_revision_->load(Ordering::Relaxed)) {
            anchored_->store(false, Ordering::Release);
            needs_reanchor_->store(true, Ordering::Release);
            const auto rate = desired_rate_->load(Ordering::Acquire);
            rate_ready_     = decoder_->set_playback_rate(rate);
            applied_rate_revision_->store(desired_revision, Ordering::Release);
            if (! rate_ready_) {
                rstd::log::error("wavsen::audio::AvPlayer: failed to apply playback rate {}", rate);
            }
        }
        if (! rate_ready_) return u64();

        const auto produced = decoder_->next_pcm(dst, frames);
        if (produced > u64() && needs_reanchor_->load(Ordering::Acquire)) {
            if (desired_rate_revision_->load(Ordering::Acquire) !=
                applied_rate_revision_->load(Ordering::Acquire)) {
                anchored_->store(false, Ordering::Release);
                return produced;
            }
            // Anchor: at the moment this batch enters the device, the
            // decoder's most-recent PTS corresponds to the device's
            // current playback position.
            pts_at_anchor_->store(decoder_->current_pts_seconds(), Ordering::Relaxed);
            device_pos_at_anchor_->store(device_pos_now_->operator()(), Ordering::Relaxed);
            rate_at_anchor_->store(decoder_->playback_rate(), Ordering::Relaxed);
            anchored_->store(true, Ordering::Release);
            needs_reanchor_->store(false, Ordering::Release);
        }
        return produced;
    }

    void pass_desc(const DeviceDesc& d) override { decoder_->retarget(d); }

private:
    StreamDecoder*         decoder_;
    Atomic<f64>*           pts_at_anchor_;
    Atomic<u64>*           device_pos_at_anchor_;
    Atomic<bool>*          needs_reanchor_;
    Atomic<bool>*          anchored_;
    Atomic<f64>*           desired_rate_;
    Atomic<u64>*           desired_rate_revision_;
    Atomic<u64>*           applied_rate_revision_;
    Atomic<f64>*           rate_at_anchor_;
    Atomic<f64>*           desired_seek_seconds_;
    Atomic<u64>*           desired_seek_revision_;
    Box<dyn<FnMut<u64()>>> device_pos_now_;
    u64                    applied_seek_revision_;
    bool                   rate_ready_ { true };
    bool                   seek_ready_ { true };
};

} // namespace

class AvPlayer::Impl {
public:
    explicit Impl(AudioClientIdentity identity_) { desired.identity = rstd::move(identity_); }

    ~Impl() {
        // The audio callback borrows decoder_ptr, so it must stop before the decoder is destroyed.
        device.shutdown();
        device.wait_stopped();
        decoder_ptr     = nullptr;
        decoder_storage = None();
    }

    AudioDevice                device;
    AudioDeviceDesiredState    desired;
    u64                        stream_revision;
    StreamDecoder*             decoder_ptr = nullptr;
    Option<Box<StreamDecoder>> decoder_storage;

    Atomic<f64>  pts_at_anchor { f64() };
    Atomic<u64>  device_pos_at_anchor { u64() };
    Atomic<bool> needs_reanchor { true };
    Atomic<bool> anchored { false };
    Atomic<bool> paused { true };
    Atomic<f64>  desired_rate { f64(1.0) };
    Atomic<u64>  desired_rate_revision { u64() };
    Atomic<u64>  applied_rate_revision { u64() };
    Atomic<f64>  rate_at_anchor { f64(1.0) };
    Atomic<f64>  desired_seek_seconds { f64() };
    Atomic<u64>  desired_seek_revision { u64() };
};

AvPlayer::AvPlayer(ConstructionKey, AudioClientIdentity identity)
    : impl_(Box<Impl>::make(rstd::move(identity))) {}
AvPlayer::~AvPlayer() = default;

auto AvPlayer::open(ByteStream src) -> Result<Box<AvPlayer>, AvPlayerError> {
    return open(rstd::move(src), true, {});
}

auto AvPlayer::open(ByteStream src, bool open_device) -> Result<Box<AvPlayer>, AvPlayerError> {
    return open(rstd::move(src), open_device, {});
}

auto AvPlayer::open(ByteStream src, bool open_device, AudioClientIdentity identity)
    -> Result<Box<AvPlayer>, AvPlayerError> {
    auto p = Box<AvPlayer>::make(ConstructionKey {}, rstd::move(identity));

    DeviceDesc desc {
        .channels    = u32(2),
        .sample_rate = u32(48000),
    };
    if (open_device && ! p->open_device()) {
        return Err(AvPlayerError { String::make("audio device init failed"_str) });
    }

    p->impl_->decoder_storage.insert(Box<StreamDecoder>::make());
    if (! p->impl_->decoder_storage->get()->open(rstd::move(src), desc)) {
        return Err(AvPlayerError { String::make("audio decoder open failed"_str) });
    }
    p->impl_->decoder_ptr = p->impl_->decoder_storage->get();

    rstd::log::info("wavsen::audio::AvPlayer: opened ({} ch @ {} Hz target, source {} ch @ {} Hz)",
                    desc.channels,
                    desc.sample_rate,
                    p->impl_->decoder_ptr->channels(),
                    p->impl_->decoder_ptr->sample_rate());

    auto* dev     = &p->impl_->device;
    auto  channel = Box<AvPullChannel>::make(p->impl_->decoder_ptr,
                                             &p->impl_->pts_at_anchor,
                                             &p->impl_->device_pos_at_anchor,
                                             &p->impl_->needs_reanchor,
                                             &p->impl_->anchored,
                                             &p->impl_->desired_rate,
                                             &p->impl_->desired_rate_revision,
                                             &p->impl_->applied_rate_revision,
                                             &p->impl_->rate_at_anchor,
                                             &p->impl_->desired_seek_seconds,
                                             &p->impl_->desired_seek_revision,
                                             Box<dyn<FnMut<u64()>>>::make([dev]() {
                                                return dev->stream_position_frames();
                                             }));
    if (! p->impl_->device.mount(
            Box<dyn<PullChannelObject>>::from_raw(
                dyn<PullChannelObject>::from_ptr(rstd::move(channel).into_raw())),
            p->impl_->stream_revision)) {
        return Err(AvPlayerError { String::make("audio device stream mount failed"_str) });
    }

    return Ok(rstd::move(p));
}

bool AvPlayer::open_device() {
    if (impl_->desired.active) return true;
    ++impl_->desired.generation;
    impl_->desired.active = true;
    if (! impl_->device.apply(impl_->desired.clone())) return false;
    impl_->anchored.store(false, Ordering::Release);
    impl_->needs_reanchor.store(true, Ordering::Release);
    impl_->device_pos_at_anchor.store(u64(), Ordering::Relaxed);
    return true;
}

void AvPlayer::close_device() {
    if (! impl_->desired.active) return;
    ++impl_->desired.generation;
    impl_->desired.active = false;
    (void)impl_->device.apply(impl_->desired.clone());
    impl_->anchored.store(false, Ordering::Release);
    impl_->needs_reanchor.store(true, Ordering::Release);
    impl_->device_pos_at_anchor.store(u64(), Ordering::Relaxed);
}

bool AvPlayer::is_device_open() const { return impl_->desired.active; }

void AvPlayer::play() {
    const bool was_paused = impl_->paused.load(Ordering::Relaxed);
    if (was_paused) {
        impl_->anchored.store(false, Ordering::Release);
        impl_->needs_reanchor.store(true, Ordering::Release);
    }
    impl_->paused.store(false, Ordering::Relaxed);
    impl_->desired.playing = true;
    if (impl_->desired.active) (void)impl_->device.apply(impl_->desired.clone());
}

void AvPlayer::pause() {
    impl_->paused.store(true, Ordering::Relaxed);
    impl_->desired.playing = false;
    if (impl_->desired.active) (void)impl_->device.apply(impl_->desired.clone());
}

bool AvPlayer::is_paused() const { return impl_->paused.load(Ordering::Relaxed); }

void AvPlayer::seek_to_start() { seek_to(f64()); }

void AvPlayer::seek_to(f64 seconds) {
    if (! seconds.is_finite() || seconds < f64()) {
        rstd::log::warn("wavsen::audio::AvPlayer: invalid seek target {}", seconds);
        return;
    }
    impl_->desired_seek_seconds.store(seconds, Ordering::Release);
    impl_->desired_seek_revision.fetch_add(u64(1), Ordering::AcqRel);
    impl_->anchored.store(false, Ordering::Release);
    impl_->needs_reanchor.store(true, Ordering::Release);
    ++impl_->desired.playback_buffer_revision;
    if (impl_->desired.active) (void)impl_->device.apply(impl_->desired.clone());
}

auto AvPlayer::set_playback_rate(f64 rate) -> bool {
    if (! rate.is_finite() || rate <= f64()) return false;
    if (rate == playback_rate()) return true;
    impl_->desired_rate.store(rate, Ordering::Release);
    impl_->desired_rate_revision.fetch_add(u64(1), Ordering::AcqRel);
    impl_->anchored.store(false, Ordering::Release);
    impl_->needs_reanchor.store(true, Ordering::Release);
    return true;
}

auto AvPlayer::playback_rate() const -> f64 { return impl_->desired_rate.load(Ordering::Acquire); }

auto AvPlayer::current_time_seconds() const -> f64 {
    if (impl_->device.state() != AudioDeviceState::ReadyPlaying) {
        return f64::NAN_;
    }
    if (! impl_->anchored.load(Ordering::Acquire)) {
        return f64::NAN_;
    }
    const auto sr = impl_->device.desc().sample_rate;
    if (sr == u32()) return f64::NAN_;
    const auto played = impl_->device.stream_position_frames();
    const auto base   = impl_->device_pos_at_anchor.load(Ordering::Relaxed);
    const auto pts0   = impl_->pts_at_anchor.load(Ordering::Relaxed);
    const auto rate   = impl_->rate_at_anchor.load(Ordering::Relaxed);
    // played may legally drop below base across some backends after stop/start;
    // saturate to anchor pts in that case.
    if (played < base) return pts0;
    return pts0 + f64(static_cast<double>((played - base).to_primitive())) /
                      f64(static_cast<double>(sr.to_primitive())) * rate;
}

void AvPlayer::set_volume(f32 value) {
    impl_->desired.volume = value;
    if (impl_->desired.active) (void)impl_->device.apply(impl_->desired.clone());
}
void AvPlayer::set_muted(bool m) {
    impl_->desired.muted = m;
    if (impl_->desired.active) (void)impl_->device.apply(impl_->desired.clone());
}
auto AvPlayer::volume_scale() const -> f32 { return impl_->desired.volume_scale; }
void AvPlayer::set_volume_scale(f32 value) { set_volume_scale(value, u32()); }
void AvPlayer::set_volume_scale(f32 value, u32 fade_ms) {
    impl_->desired.volume_scale = value;
    ++impl_->desired.volume_scale_revision;
    impl_->desired.volume_scale_fade_ms = fade_ms;
    if (impl_->desired.active) (void)impl_->device.apply(impl_->desired.clone());
}

bool AvPlayer::is_eof() const { return impl_->decoder_ptr && impl_->decoder_ptr->is_eof(); }

} // namespace wavsen::audio
