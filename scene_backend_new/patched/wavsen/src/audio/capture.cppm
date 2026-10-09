export module wavsen.audio.capture;

import rstd;

using namespace rstd::prelude;

export namespace wavsen::audio
{

inline constexpr rstd::uint32_t kAudioSampleRate   = 48000;
inline constexpr rstd::uint32_t kAudioChannels     = 2;
inline constexpr rstd::size_t   kAudioWindowFrames = 4096;
inline constexpr rstd::size_t   kAudioSampleCount  = kAudioWindowFrames * kAudioChannels;

struct AudioPcmWindow {
    rstd::uint64_t                      generation;
    rstd::uint64_t                      sequence;
    rstd::uint64_t                      captured_at_ns;
    rstd::uint64_t                      end_sample_frame;
    rstd::uint32_t                      sample_rate_hz;
    rstd::uint32_t                      channels;
    rstd::uint32_t                      frames;
    rstd::array<f32, kAudioSampleCount> samples;

    void clear() {
        generation       = 0;
        sequence         = 0;
        captured_at_ns   = 0;
        end_sample_frame = 0;
        sample_rate_hz   = 0;
        channels         = 0;
        frames           = 0;
        for (auto& sample : samples) sample = f32();
    }
};

// Taps the system default sink and publishes complete stereo F32 windows.
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();
    AudioCapture(const AudioCapture&)            = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    auto init() -> bool;
    void uninit();
    auto is_inited() const -> bool;

    // Call from one reader; returns each complete window at most once.
    bool snapshot(AudioPcmWindow& out);

private:
    class Impl;
    Box<Impl> impl_;
};

} // namespace wavsen::audio
