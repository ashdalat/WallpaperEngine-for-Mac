#include <cmath>
#include <rstd/test/gtest.hpp>

import owe.audio_response;
import rstd;

using namespace rstd::prelude;

namespace
{

owe::audio::PcmWindow tones(float scale) {
    owe::audio::PcmWindow window {};
    window.generation     = 1;
    window.sequence       = 1;
    window.sample_rate_hz = owe::audio::kSampleRate;
    window.channels       = owe::audio::kChannels;
    window.frames         = static_cast<rstd::uint32_t>(owe::audio::kWindowFrames);
    const rstd::array<float, 7> frequencies {
        200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f, 12800.0f,
    };
    for (rstd::size_t frame = 0; frame < owe::audio::kWindowFrames; ++frame) {
        float       sample = 0.0f;
        const float time = static_cast<float>(frame) / static_cast<float>(owe::audio::kSampleRate);
        for (float frequency : frequencies) {
            sample += scale * std::sin(2.0f * f32::consts::PI.to_primitive() * frequency * time);
        }
        window.samples[usize(frame * 2)]     = sample;
        window.samples[usize(frame * 2 + 1)] = sample;
    }
    return window;
}

owe::audio::PcmWindow aligned_tones(bool include_second) {
    owe::audio::PcmWindow window {};
    window.generation     = 1;
    window.sequence       = 1;
    window.sample_rate_hz = owe::audio::kSampleRate;
    window.channels       = owe::audio::kChannels;
    window.frames         = static_cast<rstd::uint32_t>(owe::audio::kWindowFrames);
    for (rstd::size_t frame = 0; frame < owe::audio::kWindowFrames; ++frame) {
        const float time = static_cast<float>(frame) / static_cast<float>(owe::audio::kSampleRate);
        float sample = 0.05f * std::sin(2.0f * f32::consts::PI.to_primitive() * 3093.75f * time);
        if (include_second) {
            sample += 0.05f * std::sin(2.0f * f32::consts::PI.to_primitive() * 3164.0625f * time);
        }
        window.samples[usize(frame * 2)]     = sample;
        window.samples[usize(frame * 2 + 1)] = sample;
    }
    return window;
}

owe::audio::PcmWindow impulse() {
    owe::audio::PcmWindow window {};
    window.generation     = 1;
    window.sequence       = 1;
    window.sample_rate_hz = owe::audio::kSampleRate;
    window.channels       = owe::audio::kChannels;
    window.frames         = static_cast<rstd::uint32_t>(owe::audio::kWindowFrames);
    window.samples[usize((owe::audio::kWindowFrames - 1) * owe::audio::kChannels)] = 1.0f;
    return window;
}

rstd::size_t peak_near(const rstd::array<float, 64>& values, rstd::size_t center) {
    auto peak = center - 1;
    for (auto index = center; index <= center + 1; ++index) {
        if (values[usize(index)] > values[usize(peak)]) peak = index;
    }
    return peak;
}

rstd::size_t peak(const rstd::array<float, 64>& values) {
    rstd::size_t result = 0;
    for (rstd::size_t index = 1; index < 64; ++index) {
        if (values[usize(index)] > values[usize(result)]) result = index;
    }
    return result;
}

} // namespace

TEST(AudioResponse, MatchesWallpaperEngineBandsAndChannels) {
    owe::audio::ResponseEngine engine;
    owe::audio::ResponseFrame  first {};
    auto                       input = tones(0.02f);
    ASSERT_TRUE(engine.analyze(input, first));
    const rstd::array<rstd::size_t, 7> expected {
        rstd::size_t(8),  rstd::size_t(16), rstd::size_t(30), rstd::size_t(36),
        rstd::size_t(43), rstd::size_t(51), rstd::size_t(61),
    };
    for (auto band : expected) {
        const auto actual = peak_near(first.left, band);
        EXPECT_EQ(actual, band) << "band " << band;
    }
    EXPECT_FALSE(engine.analyze(input, first));

    input.sequence         = 2;
    input.end_sample_frame = owe::audio::kWindowFrames;
    for (auto& sample : input.samples) sample = sample * 2.0f;
    owe::audio::ResponseFrame second {};
    ASSERT_TRUE(engine.analyze(input, second));
    for (auto band : expected) {
        const float ratio = second.left[usize(band)] / first.left[usize(band)];
        EXPECT_NEAR(ratio, 2.0f, 1.0e-4f) << "band " << band;
    }
    EXPECT_GT(second.left[usize(61)], 1.0f);

    engine.end();
    input.sequence = 1;
    ASSERT_TRUE(engine.analyze(input, first));

    owe::audio::PcmWindow stereo {};
    stereo.generation     = 2;
    stereo.sequence       = 1;
    stereo.sample_rate_hz = owe::audio::kSampleRate;
    stereo.channels       = owe::audio::kChannels;
    stereo.frames         = static_cast<rstd::uint32_t>(owe::audio::kWindowFrames);
    for (rstd::size_t frame = 0; frame < owe::audio::kWindowFrames; ++frame) {
        const float time = static_cast<float>(frame) / static_cast<float>(owe::audio::kSampleRate);
        stereo.samples[usize(frame * 2)] =
            0.45f * std::sin(2.0f * f32::consts::PI.to_primitive() * 110.0f * time);
        stereo.samples[usize(frame * 2 + 1)] =
            0.45f * std::sin(2.0f * f32::consts::PI.to_primitive() * 1760.0f * time);
    }
    owe::audio::ResponseFrame split {};
    ASSERT_TRUE(engine.analyze(stereo, split));
    EXPECT_EQ(peak(split.left), rstd::size_t(4));
    EXPECT_EQ(peak(split.right), rstd::size_t(37));
    EXPECT_GT(split.left[usize(4)], split.right[usize(4)]);
    EXPECT_GT(split.right[usize(37)], split.left[usize(37)]);
    EXPECT_NEAR(split.left[usize(4)], 1.05f, 0.2f);
    EXPECT_NEAR(split.right[usize(37)], 4.3f, 0.5f);

    owe::audio::ResponseEngine aligned_engine;
    auto                       aligned_input = aligned_tones(false);
    owe::audio::ResponseFrame  single {};
    ASSERT_TRUE(aligned_engine.analyze(aligned_input, single));
    aligned_input          = aligned_tones(true);
    aligned_input.sequence = 2;
    owe::audio::ResponseFrame dual {};
    ASSERT_TRUE(aligned_engine.analyze(aligned_input, dual));
    const float peak_ratio = dual.left[usize(43)] / single.left[usize(43)];
    EXPECT_NEAR(peak_ratio, 1.160275f, 2.0e-4f);

    auto expired = aligned_tones(false);
    for (rstd::size_t frame = owe::audio::kWindowFrames / 2; frame < owe::audio::kWindowFrames;
         ++frame) {
        expired.samples[usize(frame * 2)]     = 0.0f;
        expired.samples[usize(frame * 2 + 1)] = 0.0f;
    }
    owe::audio::ResponseEngine expired_engine;
    owe::audio::ResponseFrame  expired_response {};
    ASSERT_TRUE(expired_engine.analyze(expired, expired_response));
    for (float value : expired_response.left) EXPECT_FLOAT_EQ(value, 0.0f);

    owe::audio::ResponseEngine impulse_engine;
    owe::audio::ResponseFrame  impulse_response {};
    auto                       impulse_input = impulse();
    ASSERT_TRUE(impulse_engine.analyze(impulse_input, impulse_response));
    EXPECT_NEAR(impulse_response.left[usize(0)], 0.003480064f, 1.0e-5f);
    EXPECT_NEAR(impulse_response.left[usize(29)], 0.006702113f, 1.0e-5f);
    EXPECT_NEAR(impulse_response.left[usize(63)], 0.077817120f, 1.0e-5f);
    for (float value : impulse_response.right) EXPECT_FLOAT_EQ(value, 0.0f);
}
