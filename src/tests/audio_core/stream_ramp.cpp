// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <cstdlib>
#include <numbers>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/stream_ramp.h"
#include "common/common_types.h"

namespace {

constexpr double kSampleRate = 32728.0;
constexpr double kAmplitude = 8000.0;
constexpr std::size_t kBlock = 256;
/// Exactly 16 frames per period, so a 64-frame window holds whole periods and its RMS does not
/// depend on where in the cycle it starts.
constexpr double kToneHz = kSampleRate / 16.0;

/// Interleaved stereo sine at `freq`, continuing from frame `first`.
std::vector<s16> MakeSine(double freq, std::size_t first, std::size_t num_frames) {
    std::vector<s16> out(num_frames * 2);
    for (std::size_t i = 0; i < num_frames; i++) {
        const double t = static_cast<double>(first + i) / kSampleRate;
        const auto v = static_cast<s16>(kAmplitude * std::sin(2.0 * std::numbers::pi * freq * t));
        out[(i * 2) + 0] = v;
        out[(i * 2) + 1] = v;
    }
    return out;
}

double Rms(const std::vector<s16>& samples, std::size_t first, std::size_t last) {
    double sum = 0.0;
    for (std::size_t i = first; i < last; i++) {
        const double v = samples[i * 2];
        sum += v * v;
    }
    return std::sqrt(sum / static_cast<double>(last - first));
}

/// 4096 frames of the tone through Track(), a block at a time, so the history is full.
void PlayTone(AudioCore::StreamRamp& ramp) {
    for (std::size_t b = 0; b < 16; b++) {
        auto block = MakeSine(kToneHz, b * kBlock, kBlock);
        ramp.Track(block.data(), kBlock);
    }
}

} // namespace

TEST_CASE("StreamRamp tail continues the waveform and decays to silence",
          "[audio_core][stream_ramp]") {
    AudioCore::StreamRamp ramp;
    PlayTone(ramp);

    REQUIRE(!ramp.Ended());
    ramp.End();
    REQUIRE(ramp.Ended());

    std::vector<s16> tail(1024 * 2, 0);
    ramp.FillTail(tail.data(), 1024);

    // Continuous with what was playing: the head of the tail is the tone carried on, not a
    // step back to wherever the repeated period happens to start.
    const auto continued = MakeSine(kToneHz, 16 * kBlock, 8);
    for (std::size_t i = 0; i < 8; i++) {
        REQUIRE(std::abs(static_cast<int>(tail[i * 2]) - continued[i * 2]) <= 0.05 * 32767);
    }

    // Decays monotonically over the tail, then is exactly silent.
    double previous = Rms(tail, 0, 64);
    for (std::size_t first = 64; first < AudioCore::StreamRamp::kTailFrames; first += 64) {
        const double current = Rms(tail, first, first + 64);
        REQUIRE(current <= previous);
        previous = current;
    }
    for (std::size_t i = AudioCore::StreamRamp::kTailFrames; i < 1024; i++) {
        REQUIRE(tail[i * 2] == 0);
        REQUIRE(tail[(i * 2) + 1] == 0);
    }
}

TEST_CASE("StreamRamp ramps a resumed stream in from silence", "[audio_core][stream_ramp]") {
    AudioCore::StreamRamp ramp;
    ramp.Begin();

    auto block = MakeSine(kToneHz, 0, 1024);
    ramp.Track(block.data(), 1024);

    // Starts near silent, reaches the tone's own level once the ramp is spent.
    for (std::size_t i = 0; i < 8; i++) {
        REQUIRE(std::abs(block[i * 2]) <= 0.02 * kAmplitude);
    }
    const double level = kAmplitude / std::sqrt(2.0);
    REQUIRE(std::abs(Rms(block, 512, 1024) - level) <= 0.02 * level);
}

TEST_CASE("StreamRamp finishes a tail cut short over the buffers that follow",
          "[audio_core][stream_ramp]") {
    AudioCore::StreamRamp ramp;
    PlayTone(ramp);
    ramp.End();

    // A buffer shorter than the tail: 200 frames of it go out now.
    std::vector<s16> out(200 * 2, 0);
    ramp.FillTail(out.data(), 200);
    REQUIRE(ramp.InTail());

    // The source comes back while the tail is still playing, in 256-frame buffers.
    ramp.Begin();
    for (std::size_t b = 0; b < 8; b++) {
        auto block = MakeSine(kToneHz, (16 * kBlock) + 200 + (b * kBlock), kBlock);
        ramp.Track(block.data(), kBlock);
        out.insert(out.end(), block.begin(), block.end());
    }

    // The tail ran to completion over the head of those buffers rather than stopping short.
    double previous = Rms(out, 0, 64);
    for (std::size_t first = 64; first < AudioCore::StreamRamp::kTailFrames; first += 64) {
        const double current = Rms(out, first, first + 64);
        REQUIRE(current <= previous);
        previous = current;
    }
    REQUIRE(Rms(out, AudioCore::StreamRamp::kTailFrames - 64, AudioCore::StreamRamp::kTailFrames) <
            0.05 * kAmplitude);

    // Then the mute, then the ramp in, then the tone at its own level.
    const std::size_t mute_end =
        AudioCore::StreamRamp::kTailFrames + AudioCore::StreamRamp::kMuteFrames;
    for (std::size_t i = AudioCore::StreamRamp::kTailFrames; i < mute_end; i++) {
        REQUIRE(out[i * 2] == 0);
    }
    for (std::size_t i = mute_end; i < mute_end + 8; i++) {
        REQUIRE(std::abs(out[i * 2]) <= 0.02 * kAmplitude);
    }
    const std::size_t ramp_end = mute_end + AudioCore::StreamRamp::kRampInFrames;
    const double level = kAmplitude / std::sqrt(2.0);
    REQUIRE(std::abs(Rms(out, ramp_end, ramp_end + 512) - level) <= 0.02 * level);

    // And nothing of the old tail is left to replay at the next End().
    REQUIRE(!ramp.Ended());
    ramp.End();
    REQUIRE(ramp.InTail());
}

TEST_CASE("StreamRamp keeps ramping in across a short first buffer", "[audio_core][stream_ramp]") {
    AudioCore::StreamRamp ramp;
    REQUIRE(ramp.Down());

    // The first buffer after a load: the source has 160 frames of a 512-frame callback.
    std::vector<s16> real;
    std::vector<s16> block(512 * 2, 0);
    auto head = MakeSine(kToneHz, 0, 160);
    std::copy(head.begin(), head.end(), block.begin());
    ramp.Process(block.data(), 512, 160);
    REQUIRE(!ramp.Ended());
    real.insert(real.end(), block.begin(), block.begin() + (160 * 2));
    for (std::size_t i = 160; i < 512; i++) {
        REQUIRE(block[i * 2] == 0);
    }

    std::size_t frame = 160;
    for (std::size_t b = 0; b < 3; b++) {
        block = MakeSine(kToneHz, frame, 512);
        ramp.Process(block.data(), 512, 512);
        REQUIRE(!ramp.Ended());
        real.insert(real.end(), block.begin(), block.end());
        frame += 512;
    }

    // One ramp from silence to the tone's level, continuing across the short buffer rather
    // than restarting after it.
    for (std::size_t i = 0; i < 8; i++) {
        REQUIRE(std::abs(real[i * 2]) <= 0.02 * kAmplitude);
    }
    double previous = Rms(real, 0, 64);
    for (std::size_t first = 64; first < AudioCore::StreamRamp::kRampInFrames; first += 64) {
        const double current = Rms(real, first, first + 64);
        REQUIRE(current > previous);
        previous = current;
    }
    const double level = kAmplitude / std::sqrt(2.0);
    REQUIRE(std::abs(Rms(real, 1024, 1536) - level) <= 0.02 * level);
}

TEST_CASE("StreamRamp falls back to a DC ramp with too little history to match",
          "[audio_core][stream_ramp]") {
    AudioCore::StreamRamp ramp;
    auto block = MakeSine(kToneHz, 0, 100);
    ramp.Track(block.data(), 100);
    const s16 last = block[99 * 2];
    REQUIRE(last != 0);

    ramp.End();
    std::vector<s16> tail(AudioCore::StreamRamp::kTailFrames * 2, 0);
    ramp.FillTail(tail.data(), AudioCore::StreamRamp::kTailFrames);

    // The last sample, taken away under the raised cosine: same sign throughout, never
    // louder than the sample before, silent by the end.
    REQUIRE(std::abs(static_cast<int>(tail[0]) - last) <= 0.05 * 32767);
    for (std::size_t i = 1; i < AudioCore::StreamRamp::kTailFrames; i++) {
        REQUIRE((tail[i * 2] == 0 || (tail[i * 2] > 0) == (last > 0)));
        REQUIRE(std::abs(tail[i * 2]) <= std::abs(tail[(i - 1) * 2]));
    }
    REQUIRE(tail[(AudioCore::StreamRamp::kTailFrames - 1) * 2] == 0);
}

TEST_CASE("StreamRamp does not end a stream that was already silent", "[audio_core][stream_ramp]") {
    AudioCore::StreamRamp ramp;
    std::vector<s16> silence(kBlock * 2, 0);
    ramp.Track(silence.data(), kBlock);

    ramp.End();
    REQUIRE(!ramp.Ended());
    std::vector<s16> out(64 * 2, 1);
    ramp.FillTail(out.data(), 64);
    for (std::size_t i = 0; i < 64; i++) {
        REQUIRE(out[i * 2] == 0);
        REQUIRE(out[(i * 2) + 1] == 0);
    }
}

TEST_CASE("StreamRamp mutes what follows a tail before ramping back in",
          "[audio_core][stream_ramp]") {
    AudioCore::StreamRamp ramp;
    PlayTone(ramp);
    ramp.End();

    std::vector<s16> tail(AudioCore::StreamRamp::kTailFrames * 2, 0);
    ramp.FillTail(tail.data(), AudioCore::StreamRamp::kTailFrames);
    REQUIRE(ramp.Ended());

    // Begin() during the mute is latched: the stale frames still go, and the ramp follows.
    ramp.Begin();
    auto block = MakeSine(kToneHz, 0, 1024);
    ramp.Track(block.data(), 1024);
    for (std::size_t i = 0; i < AudioCore::StreamRamp::kMuteFrames; i++) {
        REQUIRE(block[i * 2] == 0);
    }
    REQUIRE(!ramp.Ended());
    for (std::size_t i = AudioCore::StreamRamp::kMuteFrames;
         i < AudioCore::StreamRamp::kMuteFrames + 8; i++) {
        REQUIRE(std::abs(block[i * 2]) <= 0.02 * kAmplitude);
    }
}

TEST_CASE("StreamRamp finishes a tail across short buffers without restarting it",
          "[audio_core][stream_ramp]") {
    AudioCore::StreamRamp ramp;
    // Through Process(), so the stream is up: a fresh ramp starts down, and PlayTone()'s
    // Track() calls would leave it there.
    std::size_t phase = 0;
    for (std::size_t b = 0; b < 16; b++) {
        auto block = MakeSine(kToneHz, phase, kBlock);
        ramp.Process(block.data(), kBlock, kBlock);
        phase += kBlock;
    }

    // An underrun at a 256-frame callback: the tail is armed and half of it goes out.
    std::vector<s16> out(kBlock * 2, 0);
    ramp.Process(out.data(), kBlock, 0);
    REQUIRE(ramp.InTail());

    // The source is back, but short, for four callbacks running. The tail has to run on over
    // the heads of those buffers and finish, not start again from the top at each one.
    for (std::size_t b = 0; b < 4; b++) {
        auto block = MakeSine(kToneHz, phase, 100);
        block.resize(kBlock * 2, 0);
        ramp.Process(block.data(), kBlock, 100);
        out.insert(out.end(), block.begin(), block.end());
        phase += 100;
    }
    REQUIRE(!ramp.InTail());
    REQUIRE(ramp.Ended());

    double previous = Rms(out, 0, 64);
    for (std::size_t first = 64; first < AudioCore::StreamRamp::kTailFrames; first += 64) {
        const double current = Rms(out, first, first + 64);
        REQUIRE(current <= previous);
        previous = current;
    }
    REQUIRE(Rms(out, AudioCore::StreamRamp::kTailFrames - 64, AudioCore::StreamRamp::kTailFrames) <
            0.05 * kAmplitude);

    // Full buffers spend the rest of the mute and then ramp the tone in.
    for (std::size_t b = 0; b < 6; b++) {
        auto block = MakeSine(kToneHz, phase, kBlock);
        ramp.Process(block.data(), kBlock, kBlock);
        out.insert(out.end(), block.begin(), block.end());
        phase += kBlock;
    }
    REQUIRE(!ramp.Ended());

    // Silence from the end of the tail until the mute is spent. Only source frames count
    // against the mute: the first short buffer's 100 fell under the tail, the next three
    // spent 100 each, and the full buffers that follow spend the remaining 212.
    const std::size_t mute_end = (5 * kBlock) + (AudioCore::StreamRamp::kMuteFrames - 300);
    for (std::size_t i = AudioCore::StreamRamp::kTailFrames; i < mute_end; i++) {
        REQUIRE(out[i * 2] == 0);
    }
    for (std::size_t i = mute_end; i < mute_end + 8; i++) {
        REQUIRE(std::abs(out[i * 2]) <= 0.02 * kAmplitude);
    }
    const std::size_t ramp_end = mute_end + AudioCore::StreamRamp::kRampInFrames;
    const double level = kAmplitude / std::sqrt(2.0);
    REQUIRE(std::abs(Rms(out, ramp_end, ramp_end + 512) - level) <= 0.02 * level);
}
