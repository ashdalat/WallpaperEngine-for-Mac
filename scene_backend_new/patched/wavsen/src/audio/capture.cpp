module wavsen.audio.capture;

import rstd;
import rstd.log;
import wavsen.audio.capture_window;
import wavsen.audio.backend;
#if defined(__APPLE__)
import wavsen.audio.backend.coreaudio.capture;
using NativeCapture = wavsen::audio::backend::CoreAudioCapture;
#else
import wavsen.audio.backend.pulse.capture;
using NativeCapture = wavsen::audio::backend::PulseCapture;
#endif

using namespace rstd::prelude;

namespace wavsen::audio
{

class AudioCapture::Impl {
public:
    Impl(): native_(backend::CaptureSink { this, &ingest, &discontinuity }) {}
    ~Impl() { uninit(); }
    bool init() {
        if (is_inited()) return true;
        auto result = as<backend::Capture>(native_).start();
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            rstd::log::error(
                "wavsen::audio: {} ({})", backend::error_name(error.kind), error.native_code);
            return false;
        }
        return true;
    }
    void uninit() {
        as<backend::Capture>(native_).stop();
        publisher_.restart();
        last_generation_ = 0;
        last_sequence_   = 0;
    }
    bool is_inited() const { return as<backend::Capture>(native_).is_running(); }
    bool snapshot(AudioPcmWindow& out) {
        AudioPcmWindow candidate {};
        if (! publisher_.snapshot(candidate)) return false;
        if (candidate.generation == last_generation_ && candidate.sequence == last_sequence_)
            return false;
        last_generation_ = candidate.generation;
        last_sequence_   = candidate.sequence;
        out              = candidate;
        return true;
    }

private:
    static void ingest(void* self, const float* data, rstd::uint32_t frames,
                       rstd::uint32_t channels) noexcept {
        static_cast<Impl*>(self)->publisher_.ingest(data, frames, channels);
    }
    static void discontinuity(void* self) noexcept {
        static_cast<Impl*>(self)->publisher_.restart();
    }
    capture::PcmWindowPublisher publisher_;
    NativeCapture               native_;
    rstd::uint64_t              last_generation_ {};
    rstd::uint64_t              last_sequence_ {};
};

AudioCapture::AudioCapture(): impl_(Box<Impl>::make()) {}
AudioCapture::~AudioCapture() = default;
bool AudioCapture::init() { return impl_->init(); }
void AudioCapture::uninit() { impl_->uninit(); }
bool AudioCapture::is_inited() const { return impl_->is_inited(); }
bool AudioCapture::snapshot(AudioPcmWindow& out) { return impl_->snapshot(out); }
} // namespace wavsen::audio
