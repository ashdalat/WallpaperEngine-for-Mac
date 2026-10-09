module;

#if defined(__SSE__)
#    include <xmmintrin.h>
#endif

module owe.scene_audio_response;

import owe.audio_response;
import rstd;

namespace owe::scene_audio
{

using namespace rstd::prelude;

namespace
{

constexpr float kActivityThreshold = 0.0001f;
constexpr float kEnvelopeFloor     = 0.001f;
constexpr float kPeakFloorRatio    = 0.333f;
constexpr float kSmoothRate        = 20.0f;
constexpr float kResponseRate      = 40.0f;

float Clamp(float value, float minimum, float maximum) {
    return rstd::cmp::min(rstd::cmp::max(value, minimum), maximum);
}

float Abs(float value) { return value < 0.0f ? -value : value; }

float ApproximateReciprocal(float value) {
#if defined(__SSE__)
    return _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(value)));
#else
    return 1.0f / value;
#endif
}

template<size_t N, size_t SourceN>
void MaxPool(const rstd::array<float, SourceN>& source, rstd::array<float, N>& output) {
    static_assert(SourceN % N == 0);
    constexpr size_t ratio = SourceN / N;
    for (size_t index = 0; index < N; ++index) {
        float maximum = 0.0f;
        for (size_t offset = 0; offset < ratio; ++offset) {
            maximum = rstd::cmp::max(maximum, source.data()[index * ratio + offset]);
        }
        output.data()[index] = maximum;
    }
}

template<size_t N>
float BufferValue(const ResolutionBuffers<N>& buffers, Channel channel, usize index) {
    switch (channel) {
    case Channel::Left: return buffers.left[index];
    case Channel::Right: return buffers.right[index];
    case Channel::Average: return buffers.average[index];
    }
    rstd::unreachable();
}

void BuildBuffers(const rstd::array<float, audio::kResponseBins * 2>& values, Buffers& output) {
    for (size_t index = 0; index < audio::kResponseBins; ++index) {
        output.bands64.left.data()[index]  = values.data()[index];
        output.bands64.right.data()[index] = values.data()[index + audio::kResponseBins];
        output.bands64.average.data()[index] =
            (output.bands64.left.data()[index] + output.bands64.right.data()[index]) * 0.5f;
    }

    MaxPool(output.bands64.left, output.bands32.left);
    MaxPool(output.bands64.right, output.bands32.right);
    MaxPool(output.bands64.average, output.bands32.average);
    MaxPool(output.bands32.left, output.bands16.left);
    MaxPool(output.bands32.right, output.bands16.right);
    MaxPool(output.bands32.average, output.bands16.average);
}

} // namespace

float Buffers::value(Channel channel, rstd::uint32_t resolution, rstd::uint32_t index) const {
    const auto position = usize(index);
    if (resolution <= 16) return BufferValue(bands16, channel, position);
    if (resolution <= 32) return BufferValue(bands32, channel, position);
    return BufferValue(bands64, channel, position);
}

void ResponseProcessor::submit(audio::ResponseFrame response) {
    if (! primed_ || response.generation != generation_) {
        reset_state();
        generation_ = response.generation;
        primed_     = true;
    }
    latest_ = rstd::move(response);
}

bool ResponseProcessor::advance(float frame_time_seconds, Buffers& output, EndpointFlow flow) {
    output = {};
    if (! primed_) return false;

    output.generation       = latest_.generation;
    output.sequence         = latest_.sequence;
    output.captured_at_ns   = latest_.captured_at_ns;
    output.end_sample_frame = latest_.end_sample_frame;

    rstd::array<float, audio::kResponseBins * 2> input {};
    for (size_t index = 0; index < audio::kResponseBins; ++index) {
        input.data()[index]                        = latest_.left.data()[index];
        input.data()[index + audio::kResponseBins] = latest_.right.data()[index];
    }

    if (flow == EndpointFlow::InputCapture) {
        BuildBuffers(input, output);
        return true;
    }

    rstd::array<float, 16> group_peaks {};
    float                  global_peak = 0.0f;
    for (size_t group = 0; group < 16; ++group) {
        float peak = 0.0f;
        for (size_t offset = 0; offset < 8; ++offset) {
            peak = rstd::cmp::max(peak, input.data()[group * 8 + offset]);
        }
        group_peaks.data()[group] = peak;
        global_peak               = rstd::cmp::max(global_peak, peak);
    }

    const float peak_floor = global_peak * kPeakFloorRatio;
    for (float& peak : group_peaks) peak = rstd::cmp::max(peak, peak_floor);

    if (envelopes_[usize()] <= kActivityThreshold && global_peak >= kActivityThreshold) {
        for (float& envelope : envelopes_) envelope = 1.0f;
    }

    const float frame_scale = Clamp(frame_time_seconds, kActivityThreshold, 0.25f);
    for (size_t group = 0; group < 16; ++group) {
        const float delta = group_peaks.data()[group] - envelopes_.data()[group];
        if (Abs(delta) <= kActivityThreshold) {
            envelopes_.data()[group] = group_peaks.data()[group];
            continue;
        }
        const float amount = rstd::cmp::min(frame_scale, Abs(delta));
        envelopes_.data()[group] += amount * (delta > 0.0f ? 1.0f : -0.5f);
    }

    if (global_peak < kActivityThreshold) {
        BuildBuffers({}, output);
        return true;
    }

    const float smooth_amount = rstd::cmp::min(frame_scale * kSmoothRate, 1.0f);
    const float rise_limit    = rstd::cmp::min(frame_scale * kResponseRate, 1.0f);
    const float fall_limit    = rstd::cmp::max(frame_scale * -kResponseRate, -1.0f);
    for (size_t index = 0; index < audio::kResponseBins * 2; ++index) {
        const size_t group      = index / 8;
        const float  envelope   = rstd::cmp::max(envelopes_.data()[group], kEnvelopeFloor);
        const float  normalized = input.data()[index] * ApproximateReciprocal(envelope);
        smooth_history_.data()[index] +=
            (normalized - smooth_history_.data()[index]) * smooth_amount;
        const float delta = smooth_history_.data()[index] - output_history_.data()[index];
        output_history_.data()[index] += Clamp(delta, fall_limit, rise_limit);
    }

    BuildBuffers(output_history_, output);
    return true;
}

void ResponseProcessor::end() {
    latest_     = {};
    generation_ = 0;
    primed_     = false;
    reset_state();
}

void ResponseProcessor::reset_state() {
    output_history_ = {};
    smooth_history_ = {};
    envelopes_      = {};
}

} // namespace owe::scene_audio
