export module owe.audio_response;

import owe.fft;
import rstd;

export namespace owe::audio
{

using namespace rstd::prelude;

inline constexpr rstd::uint32_t kSampleRate   = 48000;
inline constexpr rstd::uint32_t kChannels     = 2;
inline constexpr rstd::size_t   kWindowFrames = 4096;
inline constexpr rstd::size_t   kSampleCount  = kWindowFrames * kChannels;
inline constexpr rstd::size_t   kResponseBins = 64;

struct PcmWindow {
    rstd::uint64_t                   generation;
    rstd::uint64_t                   sequence;
    rstd::uint64_t                   captured_at_ns;
    rstd::uint64_t                   end_sample_frame;
    rstd::uint32_t                   sample_rate_hz;
    rstd::uint32_t                   channels;
    rstd::uint32_t                   frames;
    rstd::array<float, kSampleCount> samples;
};

struct ResponseFrame {
    rstd::uint64_t                    generation;
    rstd::uint64_t                    sequence;
    rstd::uint64_t                    captured_at_ns;
    rstd::uint64_t                    end_sample_frame;
    rstd::array<float, kResponseBins> left;
    rstd::array<float, kResponseBins> right;
};

class ResponseEngine {
public:
    bool analyze(const PcmWindow& window, ResponseFrame& output);
    void end();

private:
    fft::DftWorkspace32 dft_workspace_;
    rstd::uint64_t      generation_ = 0;
    rstd::uint64_t      sequence_   = 0;
    bool                primed_     = false;
};

} // namespace owe::audio
