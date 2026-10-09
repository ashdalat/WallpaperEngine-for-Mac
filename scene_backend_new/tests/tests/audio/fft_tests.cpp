#include <cmath>
#include <cstdio>
#include <rstd/test/gtest.hpp>

import owe.fft;
import rstd;

using namespace rstd::prelude;

static_assert(rstd::Impled<owe::fft::Complex32, rstd::Copy>);

namespace
{

template<rstd::size_t N>
rstd::array<owe::fft::Complex32, N> Direct(const rstd::array<owe::fft::Complex32, N>& input) {
    rstd::array<owe::fft::Complex32, N> output {};
    constexpr double                    pi = f64::consts::PI.to_primitive();
    for (rstd::size_t bin = 0; bin < N; ++bin) {
        double real = 0.0;
        double imag = 0.0;
        for (rstd::size_t index = 0; index < N; ++index) {
            const double angle =
                -2.0 * pi * static_cast<double>(bin * index) / static_cast<double>(N);
            const double cosine = std::cos(angle);
            const double sine   = std::sin(angle);
            real += static_cast<double>(input[usize(index)].real) * cosine -
                    static_cast<double>(input[usize(index)].imag) * sine;
            imag += static_cast<double>(input[usize(index)].real) * sine +
                    static_cast<double>(input[usize(index)].imag) * cosine;
        }
        output[usize(bin)] = {
            .real = static_cast<float>(real),
            .imag = static_cast<float>(imag),
        };
    }
    return output;
}

bool Close(float actual, float expected, float tolerance, const char* label, rstd::size_t index) {
    if (std::abs(actual - expected) <= tolerance) return true;
    std::fprintf(stderr, "%s[%zu]: expected %.9f, got %.9f\n", label, index, expected, actual);
    return false;
}

} // namespace

TEST(Fft, MatchesDirectDftForSupportedSizesAndLayouts) {
    constexpr rstd::size_t                       size = 7;
    const rstd::array<owe::fft::Complex32, size> input {
        owe::fft::Complex32 { 0.25f, -0.5f },   owe::fft::Complex32 { 1.0f, 0.0f },
        owe::fft::Complex32 { -0.75f, 0.25f },  owe::fft::Complex32 { 0.125f, 0.5f },
        owe::fft::Complex32 { 0.0f, -0.25f },   owe::fft::Complex32 { 0.625f, 0.125f },
        owe::fft::Complex32 { -0.375f, 0.75f },
    };
    const auto expected = Direct(input);

    owe::fft::DftPlan32                    plan(size);
    owe::fft::DftWorkspace32               workspace;
    rstd::array<owe::fft::Complex32, size> output {};
    plan.forward(input.as_slice(), output.as_mut_slice(), workspace);
    for (rstd::size_t index = 0; index < size; ++index) {
        EXPECT_TRUE(
            Close(output[usize(index)].real, expected[usize(index)].real, 2.0e-5f, "real", index));
        EXPECT_TRUE(
            Close(output[usize(index)].imag, expected[usize(index)].imag, 2.0e-5f, "imag", index));
    }

    constexpr rstd::size_t                       power_size = 8;
    rstd::array<owe::fft::Complex32, power_size> power_input {};
    for (rstd::size_t index = 0; index < power_size; ++index) {
        power_input[usize(index)] = {
            .real = static_cast<float>(index) * 0.125f - 0.25f,
            .imag = static_cast<float>(index % 3) * -0.2f,
        };
    }
    const auto                                   power_expected = Direct(power_input);
    rstd::array<owe::fft::Complex32, power_size> power_output {};
    owe::fft::DftPlan32                          power_plan(power_size);
    power_plan.forward(power_input.as_slice(), power_output.as_mut_slice(), workspace);
    for (rstd::size_t index = 0; index < power_size; ++index) {
        EXPECT_TRUE(Close(power_output[usize(index)].real,
                          power_expected[usize(index)].real,
                          2.0e-5f,
                          "power real",
                          index));
        EXPECT_TRUE(Close(power_output[usize(index)].imag,
                          power_expected[usize(index)].imag,
                          2.0e-5f,
                          "power imag",
                          index));
    }

    rstd::array<owe::fft::Complex32, power_size> power_in_place {};
    for (rstd::size_t index = 0; index < power_size; ++index)
        power_in_place[usize(index)] = power_input[usize(index)];
    const auto in_place_input = power_in_place.as_slice();
    power_plan.forward(in_place_input, power_in_place.as_mut_slice(), workspace);
    for (rstd::size_t index = 0; index < power_size; ++index) {
        EXPECT_TRUE(Close(power_in_place[usize(index)].real,
                          power_expected[usize(index)].real,
                          2.0e-5f,
                          "in-place real",
                          index));
        EXPECT_TRUE(Close(power_in_place[usize(index)].imag,
                          power_expected[usize(index)].imag,
                          2.0e-5f,
                          "in-place imag",
                          index));
    }

    rstd::array<owe::fft::Complex32, size> impulse {};
    impulse[usize(3)] = { 1.0f, 0.0f };
    plan.forward(impulse.as_slice(), output.as_mut_slice(), workspace);
    const auto impulse_expected = Direct(impulse);
    for (rstd::size_t index = 0; index < size; ++index) {
        EXPECT_TRUE(Close(output[usize(index)].real,
                          impulse_expected[usize(index)].real,
                          2.0e-5f,
                          "impulse real",
                          index));
        EXPECT_TRUE(Close(output[usize(index)].imag,
                          impulse_expected[usize(index)].imag,
                          2.0e-5f,
                          "impulse imag",
                          index));
    }

    constexpr rstd::size_t                          official_size = 2089;
    owe::fft::DftPlan32                             official_plan(official_size);
    rstd::array<owe::fft::Complex32, official_size> constant {};
    rstd::array<owe::fft::Complex32, official_size> constant_output {};
    for (auto& value : constant) value = { 127.0f, 1.0f / 127.0f };
    official_plan.forward(constant.as_slice(), constant_output.as_mut_slice(), workspace);
    EXPECT_TRUE(Close(constant_output[usize()].real,
                      127.0f * static_cast<float>(official_size),
                      0.5f,
                      "constant DC real",
                      0));
    EXPECT_TRUE(Close(constant_output[usize()].imag,
                      static_cast<float>(official_size) / 127.0f,
                      5.0e-3f,
                      "constant DC imag",
                      0));

    float largest_non_dc = 0.0f;
    for (rstd::size_t index = 1; index < official_size; ++index) {
        const auto value = constant_output[usize(index)];
        largest_non_dc   = rstd::cmp::max(
            largest_non_dc, std::sqrt(value.real * value.real + value.imag * value.imag));
    }
    EXPECT_LE(largest_non_dc, 0.5f);
}
