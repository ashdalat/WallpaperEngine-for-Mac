import rstd;
import wavsen.audio.gain;

using namespace rstd::prelude;

namespace
{

constexpr rstd::size_t kChunkSamples = 1024 * 2;
using Chunk                          = rstd::array<float, kChunkSamples>;

bool apply_and_check(wavsen::audio::detail::VolumeScaleRamp& ramp, f32 minimum, f32 maximum) {
    Chunk samples;
    for (auto& sample : samples) sample = 1.0f;
    ramp.apply(samples.as_mut_slice(), u32(2), f32(1.0f));
    for (const float sample : samples) {
        const auto value = f32(sample);
        if (! value.is_finite() || value < minimum || value > maximum) return false;
    }
    return true;
}

bool test_unmute_stops_at_target_inside_chunk() {
    wavsen::audio::detail::VolumeScaleRamp ramp;
    ramp.redirect(f32(), u32(48000), u32());
    ramp.redirect(f32(1.0f), u32(48000), u32(500));

    for (auto index = u32(); index < u32(30); ++index) {
        if (! apply_and_check(ramp, f32(), f32(1.0f))) return false;
    }
    return ramp.current() == f32(1.0f);
}

bool test_mute_stops_at_target_inside_chunk() {
    wavsen::audio::detail::VolumeScaleRamp ramp;
    ramp.redirect(f32(), u32(48000), u32(500));

    for (auto index = u32(); index < u32(30); ++index) {
        if (! apply_and_check(ramp, f32(), f32(1.0f))) return false;
    }
    return ramp.current() == f32();
}

bool test_repeated_redirection_stays_bounded() {
    wavsen::audio::detail::VolumeScaleRamp ramp;
    for (auto index = u32(); index < u32(20); ++index) {
        ramp.redirect(index % u32(2) == u32() ? f32() : f32(1.0f), u32(48000), u32(500));
        for (auto chunk = u32(); chunk < u32(30); ++chunk) {
            if (! apply_and_check(ramp, f32(), f32(1.0f))) return false;
        }
    }
    return true;
}

} // namespace

int main() {
    if (! test_unmute_stops_at_target_inside_chunk()) return 1;
    if (! test_mute_stops_at_target_inside_chunk()) return 1;
    if (! test_repeated_redirection_stays_bounded()) return 1;
    return 0;
}
