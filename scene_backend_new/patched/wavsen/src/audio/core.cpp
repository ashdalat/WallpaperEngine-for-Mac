module wavsen.audio.core;
import rstd;
import rstd.log;
import wavsen.audio.gain;
import wavsen.audio.backend;
#if defined(__APPLE__)
import wavsen.audio.backend.coreaudio.output;
using NativeOutput = wavsen::audio::backend::CoreAudioOutput;
#else
import wavsen.audio.backend.pulse.output;
using NativeOutput = wavsen::audio::backend::PulseOutput;
#endif

using namespace rstd::prelude;
using rstd::sync::Condvar;
using rstd::sync::Mutex;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
namespace wavsen::audio
{
enum class CommandKind
{
    Apply,
    Mount,
    Unmount,
    Shutdown
};
struct DeviceCommand {
    CommandKind                         kind {};
    AudioDeviceDesiredState             desired;
    Option<Box<dyn<PullChannelObject>>> channel;
    u64                                 revision {};
};
struct CommandQueue {
    Vec<DeviceCommand> pending;
    bool               scheduled {};
    bool               accepting { true };
};

class AudioDevice::Impl {
public:
    Impl()
        : commands_(CommandQueue {}),
          event_sink_(None()),
          stopped_(false),
          stopped_cv_(Condvar::make()),
          native_(
              backend::OutputSink { this, &render_callback, &state_callback, &position_callback }) {
    }
    ~Impl() {
        shutdown();
        wait_stopped();
    }
    void set_event_sink(Option<AudioDeviceEventSink> sink) {
        {
            auto guard = event_sink_.lock().unwrap_unchecked();
            rstd::mem::swap(*guard, sink);
        }
    }
    bool apply(AudioDeviceDesiredState desired) {
        return enqueue(
            DeviceCommand { .kind = CommandKind::Apply, .desired = rstd::move(desired) });
    }
    bool mount(Box<dyn<PullChannelObject>> channel, u64 revision) {
        return enqueue(DeviceCommand { .kind     = CommandKind::Mount,
                                       .channel  = Some(rstd::move(channel)),
                                       .revision = revision });
    }
    bool unmount_all(u64 revision) {
        return enqueue(DeviceCommand { .kind = CommandKind::Unmount, .revision = revision });
    }
    void shutdown() {
        bool schedule = false;
        {
            auto guard = commands_.lock().unwrap_unchecked();
            if (! guard->accepting) return;
            guard->accepting = false;
            guard->pending.push(DeviceCommand { .kind = CommandKind::Shutdown });
            schedule         = ! guard->scheduled;
            guard->scheduled = true;
        }
        if (schedule && ! as<backend::Output>(native_).dispatch(&drain_callback, this))
            mark_stopped();
    }
    void wait_stopped() {
        auto guard = stopped_.lock().unwrap_unchecked();
        stopped_cv_.wait_while(guard, [](bool value) {
            return ! value;
        });
    }
    auto state() const -> AudioDeviceState { return state_.load(Ordering::Acquire); }
    auto desc() const -> DeviceDesc { return { u32(2), u32(48000) }; }
    auto completed_volume_scale_revision() const -> u64 {
        const auto revision = completed_scale_.load(Ordering::Acquire);
        return position_.load(Ordering::Acquire) >= scale_end_.load(Ordering::Relaxed) ? revision
                                                                                       : u64();
    }
    auto stream_position_frames() const -> u64 { return position_.load(Ordering::Relaxed); }

private:
    bool enqueue(DeviceCommand command) {
        bool schedule = false;
        {
            auto guard = commands_.lock().unwrap_unchecked();
            if (! guard->accepting) return false;
            guard->pending.push(rstd::move(command));
            schedule         = ! guard->scheduled;
            guard->scheduled = true;
        }
        if (schedule && ! as<backend::Output>(native_).dispatch(&drain_callback, this)) {
            auto guard = commands_.lock().unwrap_unchecked();
            guard->pending.clear();
            guard->scheduled = false;
            return false;
        }
        return true;
    }
    static void drain_callback(void* user) { static_cast<Impl*>(user)->drain(); }
    void        drain() {
        for (;;) {
            Vec<DeviceCommand> batch;
            {
                auto guard = commands_.lock().unwrap_unchecked();
                if (guard->pending.is_empty()) {
                    guard->scheduled = false;
                    return;
                }
                batch          = rstd::move(guard->pending);
                guard->pending = Vec<DeviceCommand>();
            }
            for (auto& command : batch) {
                switch (command.kind) {
                case CommandKind::Apply: apply_desired(rstd::move(command.desired)); break;
                case CommandKind::Mount:
                    if (command.revision < stream_revision_) break;
                    stream_revision_ = command.revision;
                    (*command.channel)->channel().pass_desc(desc());
                    channels_.push(rstd::move(command.channel).unwrap());
                    break;
                case CommandKind::Unmount:
                    if (command.revision < stream_revision_) break;
                    stream_revision_ = command.revision;
                    channels_.clear();
                    break;
                case CommandKind::Shutdown:
                    close();
                    channels_.clear();
                    emit(AudioDeviceState::Stopped);
                    mark_stopped();
                    break;
                }
            }
        }
    }
    void close() {
        ready_     = false;
        opened_    = false;
        operation_ = Operation::None;
        as<backend::Output>(native_).close();
        position_.store(u64(), Ordering::Relaxed);
        submitted_ = u64();
        completed_scale_.store(u64(), Ordering::Release);
        scale_end_.store(u64(), Ordering::Relaxed);
    }
    void apply_desired(AudioDeviceDesiredState desired) {
        if (desired.generation != desired_.generation) close();
        desired_ = rstd::move(desired);
        if (desired_.volume_scale_revision != scale_revision_) {
            scale_revision_ = desired_.volume_scale_revision;
            scale_.redirect(
                desired_.volume_scale, desc().sample_rate, desired_.volume_scale_fade_ms);
        }
        if (! desired_.active) {
            close();
            emit(AudioDeviceState::Idle);
            return;
        }
        if (! opened_) {
            emit(AudioDeviceState::Connecting);
            opened_     = true;
            auto result = as<backend::Output>(native_).open(desired_.identity);
            if (result.is_err()) {
                close();
                auto error = rstd::move(result).unwrap_err();
                emit(AudioDeviceState::Failed,
                     rstd::format("{} ({})", backend::error_name(error.kind), error.native_code));
            }
            return;
        }
        reconcile();
    }
    enum class Operation
    {
        None,
        Playing,
        Flush
    };
    void reconcile() {
        if (! ready_ || operation_ != Operation::None) return;
        if (applied_buffer_revision_ != desired_.playback_buffer_revision) {
            if (playing_) {
                operation_ = Operation::Playing;
                as<backend::Output>(native_).set_playing(false);
                return;
            }
            operation_               = Operation::Flush;
            pending_buffer_revision_ = desired_.playback_buffer_revision;
            as<backend::Output>(native_).flush();
            return;
        }
        if (playing_ != desired_.playing) {
            operation_ = Operation::Playing;
            as<backend::Output>(native_).set_playing(desired_.playing);
            return;
        }
        emit(playing_ ? AudioDeviceState::ReadyPlaying : AudioDeviceState::ReadyPaused);
    }
    static void state_callback(void* user, backend::OutputState state, backend::Error error) {
        auto& self = *static_cast<Impl*>(user);
        switch (state) {
        case backend::OutputState::Ready:
            self.ready_                   = true;
            self.playing_                 = false;
            self.applied_buffer_revision_ = self.desired_.playback_buffer_revision;
            for (auto& channel : self.channels_) channel->channel().pass_desc(self.desc());
            break;
        case backend::OutputState::Playing:
        case backend::OutputState::Paused:
            self.playing_   = state == backend::OutputState::Playing;
            self.operation_ = Operation::None;
            break;
        case backend::OutputState::Flushed:
            self.applied_buffer_revision_ = self.pending_buffer_revision_;
            self.operation_               = Operation::None;
            break;
        case backend::OutputState::Failed:
            self.ready_     = false;
            self.operation_ = Operation::None;
            self.emit(AudioDeviceState::Failed,
                      rstd::format("native audio error ({})", error.native_code));
            return;
        }
        self.reconcile();
    }
    static void position_callback(void* user, u64 value) noexcept {
        static_cast<Impl*>(user)->position_.store(value, Ordering::Relaxed);
    }
    static void render_callback(void* user, float* output, rstd::uint32_t frames) noexcept {
        static_cast<Impl*>(user)->render(output, frames);
    }
    void render(float* output, rstd::uint32_t frames) {
        rstd::mem::memset(output, u8(), usize(frames) * usize(2 * sizeof(float)));
        if (desired_.muted) {
            submitted_ += u64(frames);
            return;
        }
        for (rstd::uint32_t offset = 0; offset < frames;) {
            const auto count = rstd::cmp::min(rstd::uint32_t(8192), frames - offset);
            auto*      block = output + offset * 2;
            for (auto& channel : channels_) {
                rstd::mem::memset(scratch_, u8(), usize(count) * usize(2 * sizeof(float)));
                channel->channel().output_offset(submitted_ + u64(offset));
                auto produced =
                    rstd::cmp::min(channel->channel().next_pcm(scratch_, u32(count)), u64(count));
                for (rstd::uint64_t i = 0; i < produced.to_primitive() * 2; ++i)
                    block[i] += scratch_[i];
            }
            scale_.apply(rstd::mut_ref<float[]>::from_raw_parts(block, usize(count) * usize(2)),
                         u32(2),
                         desired_.volume);
            offset += count;
        }
        submitted_ += u64(frames);
        if (scale_.finished() && completed_scale_.load(Ordering::Relaxed) != scale_revision_) {
            scale_end_.store(submitted_, Ordering::Relaxed);
            completed_scale_.store(scale_revision_, Ordering::Release);
        }
    }
    void emit(AudioDeviceState state, String error = {}) {
        state_.store(state, Ordering::Release);
        Option<AudioDeviceEventSink> sink;
        {
            auto guard = event_sink_.lock().unwrap_unchecked();
            sink       = guard->clone();
        }
        if (sink)
            (*sink)->operator()(AudioDeviceEvent { desired_.generation, state, rstd::move(error) });
    }
    void mark_stopped() {
        auto guard = stopped_.lock().unwrap_unchecked();
        *guard     = true;
        stopped_cv_.notify_all();
    }

    Mutex<CommandQueue>                 commands_;
    Mutex<Option<AudioDeviceEventSink>> event_sink_;
    Mutex<bool>                         stopped_;
    Condvar                             stopped_cv_;
    Atomic<AudioDeviceState>            state_ { AudioDeviceState::Idle };
    Atomic<u64>                         position_ { u64() };
    Atomic<u64>                         completed_scale_ { u64() };
    Atomic<u64>                         scale_end_ { u64() };
    u64                                 submitted_ {};
    AudioDeviceDesiredState             desired_;
    Vec<Box<dyn<PullChannelObject>>>    channels_;
    detail::VolumeScaleRamp             scale_;
    float                               scratch_[8192 * 2] {};
    u64 stream_revision_ {}, scale_revision_ {}, applied_buffer_revision_ {},
        pending_buffer_revision_ {};
    bool         ready_ {}, opened_ {}, playing_ {};
    Operation    operation_ { Operation::None };
    NativeOutput native_;
};

AudioDevice::AudioDevice(): impl_(Box<Impl>::make()) {}
AudioDevice::~AudioDevice() = default;
void AudioDevice::set_event_sink(Option<AudioDeviceEventSink> sink) {
    impl_->set_event_sink(rstd::move(sink));
}
bool AudioDevice::apply(AudioDeviceDesiredState desired) {
    return impl_->apply(rstd::move(desired));
}
bool AudioDevice::mount(Box<dyn<PullChannelObject>> channel, u64 revision) {
    return impl_->mount(rstd::move(channel), revision);
}
bool AudioDevice::unmount_all(u64 revision) { return impl_->unmount_all(revision); }
void AudioDevice::shutdown() { impl_->shutdown(); }
void AudioDevice::wait_stopped() { impl_->wait_stopped(); }
auto AudioDevice::state() const -> AudioDeviceState { return impl_->state(); }
auto AudioDevice::desc() const -> DeviceDesc { return impl_->desc(); }
auto AudioDevice::stream_position_frames() const -> u64 { return impl_->stream_position_frames(); }
auto AudioDevice::completed_volume_scale_revision() const -> u64 {
    return impl_->completed_volume_scale_revision();
}
} // namespace wavsen::audio
