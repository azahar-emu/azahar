// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <numbers>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "audio_core/speedup_lowpass.h"
#include "common/common_types.h"

using Catch::Matchers::WithinRel;

namespace {

constexpr double kSampleRate = 32728.0;

/// Interleaved stereo sine at `freq`, amplitude 12000.
std::vector<s16> MakeSine(double freq, std::size_t num_frames) {
    std::vector<s16> out(num_frames * 2);
    for (std::size_t i = 0; i < num_frames; i++) {
        const double t = static_cast<double>(i) / kSampleRate;
        const auto v = static_cast<s16>(12000.0 * std::sin(2.0 * std::numbers::pi * freq * t));
        out[(i * 2) + 0] = v;
        out[(i * 2) + 1] = v;
    }
    return out;
}

/// Peak absolute amplitude over the back half, so the filter's transient is excluded.
double TailPeak(const std::vector<s16>& samples) {
    double peak = 0.0;
    for (std::size_t i = samples.size() / 2; i < samples.size(); i++) {
        peak = std::max(peak, std::fabs(static_cast<double>(samples[i])));
    }
    return peak;
}

} // namespace

TEST_CASE("SpeedupLowPass wide open is transparent", "[audio_core][speedup]") {
    AudioCore::SpeedupLowPass filter;
    filter.Init(kSampleRate);

    REQUIRE_THAT(filter.WideOpenCutoff(), WithinRel(0.45 * kSampleRate, 1e-9));
    REQUIRE(filter.Bypassed());

    auto samples = MakeSine(1000.0, 4096);
    const auto original = samples;
    filter.Process(samples.data(), 4096, filter.WideOpenCutoff(), 4096.0 / kSampleRate);

    // Bypassed means the buffer is returned untouched, not merely close.
    REQUIRE(samples == original);
}

TEST_CASE("SpeedupLowPass attenuates above the cutoff", "[audio_core][speedup]") {
    AudioCore::SpeedupLowPass filter;
    filter.Init(kSampleRate);
    filter.SetCutoffNow(1000.0);

    auto passed = MakeSine(300.0, 8192);
    auto stopped = MakeSine(8000.0, 8192);

    filter.Process(passed.data(), 8192, 1000.0, 8192.0 / kSampleRate);

    AudioCore::SpeedupLowPass filter2;
    filter2.Init(kSampleRate);
    filter2.SetCutoffNow(1000.0);
    filter2.Process(stopped.data(), 8192, 1000.0, 8192.0 / kSampleRate);

    // Well below cutoff survives; three octaves above is crushed by a 4th-order rolloff.
    REQUIRE(TailPeak(passed) > 10000.0);
    REQUIRE(TailPeak(stopped) < 200.0);
}

TEST_CASE("SpeedupLowPass smooths rather than jumps", "[audio_core][speedup]") {
    AudioCore::SpeedupLowPass filter;
    filter.Init(kSampleRate);

    const double wide_open = filter.WideOpenCutoff();
    REQUIRE_THAT(filter.Cutoff(), WithinRel(wide_open, 1e-9));

    // One 512-frame block is far shorter than the 50 ms smoothing constant, so the cutoff
    // must move toward the target without arriving.
    filter.Smooth(1000.0, 512.0 / kSampleRate);
    REQUIRE(filter.Cutoff() < wide_open);
    REQUIRE(filter.Cutoff() > 1000.0);
}

TEST_CASE("SpeedupLowPass moves its coefficients without ringing", "[audio_core][speedup]") {
    constexpr std::size_t block = 256;
    constexpr std::size_t blocks = 16;
    constexpr std::size_t half = block * blocks;

    AudioCore::SpeedupLowPass filter;
    filter.Init(kSampleRate);

    // Low enough that the tone's own slope is small next to any transient the filter adds.
    auto samples = MakeSine(50.0, half * 2);
    for (std::size_t b = 0; b < blocks; b++) {
        filter.Process(&samples[b * block * 2], block, filter.WideOpenCutoff(),
                       block / kSampleRate);
    }
    for (std::size_t b = blocks; b < 2 * blocks; b++) {
        filter.Process(&samples[b * block * 2], block, 2000.0, block / kSampleRate);
    }

    const auto largest_step = [&samples](std::size_t first, std::size_t last) {
        double largest = 0.0;
        for (std::size_t i = first + 1; i < last; i++) {
            const double d = static_cast<double>(samples[i * 2]) - samples[(i - 1) * 2];
            largest = std::max(largest, std::fabs(d));
        }
        return largest;
    };

    // A transposed direct-form biquad's state was produced under the previous coefficients, so
    // replacing them in one step per block leaves the two inconsistent and the filter rings at
    // every block edge while the cutoff slides. Spread across the block, it must not.
    const double steady = largest_step(half - 1024, half);
    const double sliding = largest_step(half, 2 * half);
    CAPTURE(steady, sliding);
    REQUIRE(sliding <= 1.5 * steady);
}

TEST_CASE("SpeedupLowPass clamps overshoot instead of wrapping", "[audio_core][speedup]") {
    constexpr int num_frames = 512;
    constexpr int half_period = 32; // about 500 Hz at 32728 Hz

    std::vector<s16> square(num_frames * 2);
    for (int i = 0; i < num_frames; i++) {
        const s16 v = ((i / half_period) % 2 == 0) ? 32767 : -32768;
        square[(i * 2) + 0] = v;
        square[(i * 2) + 1] = v;
    }

    // A fourth-order Butterworth overshoots on a step, so a full-scale square well inside the
    // passband drives the filter past what an s16 holds. Find where, via the pre-clamp value.
    AudioCore::SpeedupLowPass probe;
    probe.Init(kSampleRate);
    probe.SetCutoffNow(4000.0);

    int overshoot_index = -1;
    for (int i = 0; i < num_frames; i++) {
        if (probe.ProcessSample(square[(i * 2) + 0], 0) > 32767.0) {
            overshoot_index = i;
            break;
        }
    }
    REQUIRE(overshoot_index >= 0);

    // Saturate must clamp to the maximum. Wrapping would land on a large negative value, so
    // this distinguishes the two - unlike asserting the result is in the s16 range, which is
    // true of every s16 by construction.
    AudioCore::SpeedupLowPass filter;
    filter.Init(kSampleRate);
    filter.SetCutoffNow(4000.0);
    filter.Process(square.data(), num_frames, 4000.0, num_frames / kSampleRate);

    REQUIRE(square[(overshoot_index * 2) + 0] == 32767);
}

TEST_CASE("SpeedupLowPassCutoff scales the reference by the speed", "[audio_core][speedup]") {
    constexpr double kWideOpen = 0.45 * kSampleRate;

    // At or below full speed the filter is transparent, whatever the reference says.
    REQUIRE_THAT(AudioCore::SpeedupLowPassCutoff(1.0, 8000, kWideOpen), WithinRel(kWideOpen));
    REQUIRE_THAT(AudioCore::SpeedupLowPassCutoff(0.5, 8000, kWideOpen), WithinRel(kWideOpen));

    // Above it, reference / speed, clamped to wide open at the top and the floor at the bottom.
    REQUIRE_THAT(AudioCore::SpeedupLowPassCutoff(2.0, 8000, kWideOpen), WithinRel(4000.0));
    REQUIRE_THAT(AudioCore::SpeedupLowPassCutoff(4.0, 8000, kWideOpen), WithinRel(2000.0));
    REQUIRE_THAT(AudioCore::SpeedupLowPassCutoff(2.0, 44100, kWideOpen), WithinRel(kWideOpen));
    REQUIRE_THAT(AudioCore::SpeedupLowPassCutoff(100.0, 8000, kWideOpen),
                 WithinRel(AudioCore::kMinLowPassCutoff));

    // Both ends of the range mean "do not filter": the sentinel, and zero, which would
    // otherwise clamp to the floor and be the strongest setting rather than none.
    REQUIRE_THAT(AudioCore::SpeedupLowPassCutoff(2.0, AudioCore::kSpeedupLowPassOff, kWideOpen),
                 WithinRel(kWideOpen));
    REQUIRE_THAT(AudioCore::SpeedupLowPassCutoff(2.0, 0, kWideOpen), WithinRel(kWideOpen));
}

TEST_CASE("SpeedupIsOffSpeed ignores a hair either side of full", "[audio_core][speedup]") {
    REQUIRE_FALSE(AudioCore::SpeedupIsOffSpeed(1.0));
    REQUIRE_FALSE(AudioCore::SpeedupIsOffSpeed(1.005));
    REQUIRE_FALSE(AudioCore::SpeedupIsOffSpeed(0.995));
    REQUIRE(AudioCore::SpeedupIsOffSpeed(1.5));
    REQUIRE(AudioCore::SpeedupIsOffSpeed(0.5));
}
