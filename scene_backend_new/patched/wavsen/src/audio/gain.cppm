export module wavsen.audio.gain;

import rstd;

using namespace rstd::prelude;

export namespace wavsen::audio::detail
{

class VolumeScaleRamp {
public:
    void redirect(f32 target, u32 sample_rate, u32 fade_ms) {
        target = clamped(target);
        if (fade_ms == u32()) {
            current_     = target;
            target_      = target;
            step_        = f32();
            frames_left_ = u32();
            return;
        }

        auto fade_frames =
            u64(sample_rate.to_primitive()) * u64(fade_ms.to_primitive()) / u64(1000);
        fade_frames  = fade_frames.max(u64(1)).min(u64(u32::MAX.to_primitive()));
        target_      = target;
        frames_left_ = u32(fade_frames.to_primitive());
        step_        = (target_ - current_) / f32(static_cast<float>(frames_left_.to_primitive()));
    }

    void apply(mut_ref<float[]> output, u32 channels, f32 volume) {
        if (channels == u32()) return;
        if (frames_left_ == u32()) {
            const float gain = (volume * current_).to_primitive();
            for (auto& sample : output) sample *= gain;
            return;
        }

        const auto channel_count = usize(channels.to_primitive());
        const auto frames        = output.len() / channel_count;
        for (auto frame = usize(); frame < frames; ++frame) {
            const float gain = (volume * current_).to_primitive();
            const auto  base = frame * channel_count;
            for (auto channel = usize(); channel < channel_count; ++channel) {
                output[base + channel] *= gain;
            }
            advance();
        }
    }

    auto current() const -> f32 { return current_; }
    bool finished() const { return frames_left_ == u32(); }

private:
    static auto clamped(f32 value) -> f32 {
        if (! value.is_finite() || value < f32()) return f32();
        return value.min(f32(1.0f));
    }

    void advance() {
        if (frames_left_ == u32()) return;
        --frames_left_;
        current_ = frames_left_ == u32() ? target_ : current_ + step_;
    }

    f32 current_ { f32(1.0f) };
    f32 target_ { f32(1.0f) };
    f32 step_;
    u32 frames_left_;
};

} // namespace wavsen::audio::detail
