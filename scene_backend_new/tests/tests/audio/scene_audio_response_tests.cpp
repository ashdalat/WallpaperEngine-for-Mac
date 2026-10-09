#include <cmath>
#include <rstd/test/gtest.hpp>

import owe.audio_response;
import owe.scene_audio_response;
import rstd;

using namespace owe;
using namespace rstd::prelude;

namespace
{

audio::ResponseFrame ConstantResponse(rstd::uint64_t generation, rstd::uint64_t sequence,
                                      float left, float right) {
    audio::ResponseFrame result {};
    result.generation       = generation;
    result.sequence         = sequence;
    result.captured_at_ns   = sequence * 1000;
    result.end_sample_frame = sequence * 10;
    for (float& value : result.left) value = left;
    for (float& value : result.right) value = right;
    return result;
}

bool Near(float left, float right, float tolerance = 1.0e-5f) {
    return std::abs(left - right) <= tolerance;
}

template<rstd::size_t N>
bool All(const rstd::array<float, N>& values, float expected, float tolerance = 1.0e-5f) {
    for (float value : values) {
        if (! Near(value, expected, tolerance)) return false;
    }
    return true;
}

bool Same64(const scene_audio::Buffers& left, const scene_audio::Buffers& right,
            float tolerance = 1.0e-5f) {
    for (size_t index = 0; index < 64; ++index) {
        if (! Near(left.bands64.left.data()[index], right.bands64.left.data()[index], tolerance) ||
            ! Near(
                left.bands64.right.data()[index], right.bands64.right.data()[index], tolerance)) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST(SceneAudioResponse, PreservesChannelsSmoothingAndGenerationState) {
    scene_audio::ResponseProcessor processor;
    scene_audio::Buffers           output {};
    EXPECT_FALSE(processor.advance(1.0f / 30.0f, output));

    auto patterned = ConstantResponse(1, 7, 0.0f, 0.0f);
    for (size_t index = 0; index < 64; ++index) {
        patterned.left.data()[index]  = static_cast<float>(index + 1);
        patterned.right.data()[index] = static_cast<float>(201 + index);
    }
    processor.submit(patterned);
    ASSERT_TRUE(processor.advance(1.0f / 30.0f, output, scene_audio::EndpointFlow::InputCapture));
    EXPECT_EQ(output.generation, rstd::uint64_t(1));
    EXPECT_EQ(output.sequence, rstd::uint64_t(7));
    EXPECT_EQ(output.captured_at_ns, rstd::uint64_t(7000));
    EXPECT_EQ(output.end_sample_frame, rstd::uint64_t(70));
    for (size_t index = 0; index < 32; ++index) {
        EXPECT_TRUE(Near(output.bands32.left.data()[index], static_cast<float>(index * 2 + 2)))
            << "left band " << index;
        EXPECT_TRUE(Near(output.bands32.right.data()[index], static_cast<float>(index * 2 + 202)))
            << "right band " << index;
        EXPECT_TRUE(Near(output.bands32.average.data()[index], static_cast<float>(index * 2 + 102)))
            << "average band " << index;
    }
    for (size_t index = 0; index < 16; ++index) {
        EXPECT_TRUE(Near(output.bands16.left.data()[index], static_cast<float>(index * 4 + 4)))
            << "left band " << index;
        EXPECT_TRUE(Near(output.bands16.right.data()[index], static_cast<float>(index * 4 + 204)))
            << "right band " << index;
        EXPECT_TRUE(Near(output.bands16.average.data()[index], static_cast<float>(index * 4 + 104)))
            << "average band " << index;
    }
    EXPECT_TRUE(Near(output.bands64.left[usize()], 1.0f));
    EXPECT_TRUE(Near(output.bands64.average[usize()], 101.0f));
    EXPECT_TRUE(Near(output.bands32.left[usize()], 2.0f));
    EXPECT_TRUE(Near(output.bands32.right[usize(31)], 264.0f));
    EXPECT_TRUE(Near(output.bands16.left[usize()], 4.0f));
    EXPECT_TRUE(Near(output.bands16.average[usize(15)], 164.0f));
    EXPECT_TRUE(Near(output.value(scene_audio::Channel::Left, 16, 0), 4.0f));
    EXPECT_TRUE(Near(output.value(scene_audio::Channel::Right, 32, 31), 264.0f));
    EXPECT_TRUE(Near(output.value(scene_audio::Channel::Average, 64, 0), 101.0f));

    scene_audio::ResponseProcessor fresh;
    scene_audio::Buffers           after_bypass {};
    scene_audio::Buffers           direct {};
    fresh.submit(patterned);
    ASSERT_TRUE(processor.advance(1.0f / 30.0f, after_bypass));
    ASSERT_TRUE(fresh.advance(1.0f / 30.0f, direct));
    EXPECT_TRUE(Same64(after_bypass, direct));

    scene_audio::ResponseProcessor grouped;
    auto                           grouped_input = ConstantResponse(2, 1, 0.0f, 0.0f);
    grouped_input.left[usize(7)]                 = 1.0f;
    grouped_input.left[usize(8)]                 = 0.5f;
    grouped.submit(grouped_input);
    ASSERT_TRUE(grouped.advance(1.0f / 30.0f, output));
    EXPECT_TRUE(Near(output.bands64.left[usize(7)], 0.6665f, 0.002f));
    EXPECT_TRUE(Near(output.bands64.left[usize(8)], 0.3389f, 0.002f));

    processor.end();
    processor.submit(ConstantResponse(2, 1, 0.2f, 0.4f));
    ASSERT_TRUE(processor.advance(1.0f / 30.0f, output));
    EXPECT_TRUE(All(output.bands64.left, 0.1356f, 0.001f));
    EXPECT_TRUE(All(output.bands64.right, 0.2712f, 0.001f));
    const float first_left = output.bands64.left[usize()];
    ASSERT_TRUE(processor.advance(1.0f / 30.0f, output));
    EXPECT_GT(output.bands64.left[usize()], first_left);

    processor.submit(ConstantResponse(3, 1, 0.2f, 0.4f));
    ASSERT_TRUE(processor.advance(1.0f / 30.0f, output));
    EXPECT_TRUE(Near(output.bands64.left[usize()], first_left, 1.0e-5f));

    processor.submit(ConstantResponse(3, 2, 0.0f, 0.0f));
    ASSERT_TRUE(processor.advance(1.0f / 30.0f, output));
    EXPECT_TRUE(All(output.bands64.left, 0.0f));
    EXPECT_TRUE(All(output.bands64.right, 0.0f));
    ASSERT_TRUE(processor.advance(0.25f, output));
    EXPECT_TRUE(All(output.bands64.left, 0.0f));
    processor.submit(ConstantResponse(3, 3, 0.2f, 0.4f));
    ASSERT_TRUE(processor.advance(1.0f / 30.0f, output));
    EXPECT_GT(output.bands64.left[usize()], 0.0f);

    scene_audio::ResponseProcessor minimum_a;
    scene_audio::ResponseProcessor minimum_b;
    scene_audio::ResponseProcessor maximum_a;
    scene_audio::ResponseProcessor maximum_b;
    auto                           clamp_input = ConstantResponse(4, 1, 0.5f, 1.0f);
    minimum_a.submit(clamp_input);
    minimum_b.submit(clamp_input);
    maximum_a.submit(clamp_input);
    maximum_b.submit(clamp_input);
    scene_audio::Buffers minimum_output_a {};
    scene_audio::Buffers minimum_output_b {};
    scene_audio::Buffers maximum_output_a {};
    scene_audio::Buffers maximum_output_b {};
    (void)minimum_a.advance(0.0f, minimum_output_a);
    (void)minimum_b.advance(0.0001f, minimum_output_b);
    (void)maximum_a.advance(0.25f, maximum_output_a);
    (void)maximum_b.advance(1.0f, maximum_output_b);
    EXPECT_TRUE(Same64(minimum_output_a, minimum_output_b));
    EXPECT_TRUE(Same64(maximum_output_a, maximum_output_b));

    processor.end();
    EXPECT_FALSE(processor.advance(1.0f / 30.0f, output));
}
