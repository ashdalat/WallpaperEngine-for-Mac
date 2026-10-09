export module owe.scene_audio_response;

export import owe.audio_response;
import rstd;

export namespace owe::scene_audio
{

enum class EndpointFlow : rstd::uint8_t
{
    OutputMonitor,
    InputCapture,
};

enum class Channel : rstd::uint8_t
{
    Left,
    Right,
    Average,
};

template<rstd::size_t N>
struct ResolutionBuffers {
    rstd::array<float, N> left {};
    rstd::array<float, N> right {};
    rstd::array<float, N> average {};
};

struct Buffers {
    rstd::uint64_t        generation {};
    rstd::uint64_t        sequence {};
    rstd::uint64_t        captured_at_ns {};
    rstd::uint64_t        end_sample_frame {};
    ResolutionBuffers<16> bands16;
    ResolutionBuffers<32> bands32;
    ResolutionBuffers<64> bands64;

    float value(Channel channel, rstd::uint32_t resolution, rstd::uint32_t index) const;
};

class ResponseProcessor {
public:
    void submit(audio::ResponseFrame response);
    bool advance(float frame_time_seconds, Buffers& output,
                 EndpointFlow flow = EndpointFlow::OutputMonitor);
    void end();

private:
    void reset_state();

    audio::ResponseFrame                         latest_ {};
    rstd::array<float, audio::kResponseBins * 2> output_history_ {};
    rstd::array<float, audio::kResponseBins * 2> smooth_history_ {};
    rstd::array<float, 16>                       envelopes_ {};
    rstd::uint64_t                               generation_ {};
    bool                                         primed_ {};
};

} // namespace owe::scene_audio
