// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/audio_types.h"
#include "audio_core/time_stretch.h"
#include "common/common_types.h"

namespace {

constexpr std::size_t kCallback = 512;

std::vector<s16> Tone(std::size_t first, std::size_t num_frames) {
    std::vector<s16> out(num_frames * 2);
    for (std::size_t i = 0; i < num_frames; i++) {
        const double t = static_cast<double>(first + i) / AudioCore::native_sample_rate;
        const auto v =
            static_cast<s16>(std::lround(8000.0 * std::sin(2.0 * std::numbers::pi * 440.0 * t)));
        out[i * 2] = v;
        out[(i * 2) + 1] = v;
    }
    return out;
}

std::vector<s16> Noise(std::size_t num_frames, unsigned seed) {
    std::vector<s16> out(num_frames * 2);
    std::srand(seed);
    for (std::size_t i = 0; i < num_frames * 2; i++) {
        out[i] = static_cast<s16>((std::rand() % 16000) - 8000);
    }
    return out;
}

/// Runs `calls` callbacks of in == out frames at the current settings; returns the last count.
std::size_t Run(AudioCore::TimeStretcher& ts, std::size_t calls, std::size_t& first_frame) {
    std::vector<s16> out(kCallback * 2);
    std::size_t got = 0;
    for (std::size_t c = 0; c < calls; c++) {
        const auto in = Tone(first_frame, kCallback);
        first_frame += kCallback;
        got = ts.Process(in.data(), kCallback, out.data(), kCallback);
    }
    return got;
}

} // namespace

TEST_CASE("A primed TimeStretcher outputs as soon as its first round can run",
          "[audio_core][bypass]") {
    AudioCore::TimeStretcher primed;
    primed.SetRatioBounds(1.0, 1.0);
    const std::size_t need = primed.BeginPrime();
    REQUIRE(need > 2000);
    REQUIRE(need < 4000);

    const auto history = Tone(0, need);
    primed.Feed(history.data(), need);
    const std::size_t dropped = primed.Discard(std::numeric_limits<std::size_t>::max());
    // A round ran inside the priming, and its output is gone.
    REQUIRE(dropped > 0);
    REQUIRE(dropped < need);
    REQUIRE(primed.OutputBacklog() == 0);
    const std::size_t residency = primed.InputResidency();
    REQUIRE(residency > 0);
    REQUIRE(residency < need);

    // Just enough new input to complete the next round: the primed stretcher fills a callback.
    const std::size_t top_up = need - residency + 64;
    const auto next = Tone(need, top_up);
    std::vector<s16> out(kCallback * 2);
    REQUIRE(primed.Process(next.data(), top_up, out.data(), kCallback) == kCallback);

    // A cold one, given the same input, has nothing yet.
    AudioCore::TimeStretcher cold;
    cold.SetRatioBounds(1.0, 1.0);
    REQUIRE(cold.Process(next.data(), top_up, out.data(), kCallback) == 0);
}

TEST_CASE("A primed TimeStretcher's output index tracks its input index", "[audio_core][bypass]") {
    // What OutputPipeline::Sync() (audio_core/output_pipeline.cpp) relies on: after feeding
    // `need` frames and dropping what came out, the next output frame is source frame
    // `dropped`, within half a seek window.
    AudioCore::TimeStretcher ts;
    ts.SetRatioBounds(1.0, 1.0);
    const std::size_t need = ts.BeginPrime();
    const std::size_t total = need + 6000;
    const auto src = Noise(total, 5);
    ts.Feed(src.data(), need);
    const std::size_t dropped = ts.Discard(std::numeric_limits<std::size_t>::max());
    REQUIRE(dropped > 0);

    std::vector<s16> out(4096 * 2);
    const std::size_t got = ts.Process(src.data() + (need * 2), 6000, out.data(), 4096);
    REQUIRE(got >= 1024);

    // The first frames out are an overlap blend and do not locate; the first that does must
    // sit at its own index plus `dropped`, give or take the seek jitter.
    bool located = false;
    for (std::size_t f = 0; f + 8 <= got && !located; f++) {
        for (std::size_t i = 0; i + 8 <= total; i++) {
            bool same = true;
            for (std::size_t k = 0; k < 8 * 2 && same; k++) {
                same = out[(f * 2) + k] == src[(i * 2) + k];
            }
            if (same) {
                const long error = static_cast<long>(i) - static_cast<long>(f + dropped);
                INFO("output frame " << f << " is source frame " << i << ", dropped " << dropped);
                REQUIRE(std::abs(error) <= 320);
                located = true;
                break;
            }
        }
    }
    REQUIRE(located);
}

TEST_CASE("TimeStretcher::Process keeps its input when nothing is asked for",
          "[audio_core][bypass]") {
    AudioCore::TimeStretcher ts;
    const auto in = Tone(0, 1000);
    std::vector<s16> out(2);
    REQUIRE(ts.Process(in.data(), 1000, out.data(), 0) == 0);
    // The rate transposer holds a few frames back and the rest is inside, unprocessed. How
    // many is up to the build: the bundled integer SoundTouch keeps one, the float build a
    // system package gives keeps three. The point is that none of it was discarded.
    REQUIRE(ts.InputResidency() >= 990);
}

TEST_CASE("TimeStretcher::FlushInto returns what was fed, less at most an overlap, at ratio 1",
          "[audio_core][bypass]") {
    AudioCore::TimeStretcher ts;
    ts.SetRatioBounds(1.0, 1.0);
    const auto in = Tone(0, 4000);
    std::vector<s16> out(kCallback * 2);
    const std::size_t first = ts.Process(in.data(), 4000, out.data(), kCallback);
    std::vector<s16> flushed(8192 * 2);
    const std::size_t rest = ts.FlushInto(flushed.data(), 8192);
    // The last round starts anywhere within a seek window, as every round does, and the
    // overlap it blends into the padding is trimmed.
    REQUIRE(first + rest >= 4000 - ts.OverlapFrames() - ts.SeekFrames() - 16);
    REQUIRE(first + rest <= 4000);
    REQUIRE(ts.OutputBacklog() == 0);
    REQUIRE(ts.InputResidency() == 0);
}

TEST_CASE("TimeStretcher::FlushInto keeps the residency across a tempo step",
          "[audio_core][bypass]") {
    // Fed at tempo 3 and flushed at tempo 1, SoundTouch's own flush() would expect a third of
    // the residency and discard the rest; ours reads what the padding rounds produce.
    AudioCore::TimeStretcher ts;
    ts.SetRatioBounds(3.0, 3.0);
    const auto in = Tone(0, 4000);
    std::vector<s16> out(kCallback * 2);
    ts.Process(in.data(), 4000, out.data(), kCallback);
    const std::size_t residency = ts.InputResidency();
    REQUIRE(residency > 1000);
    ts.SetRatioBounds(1.0, 1.0);
    ts.SetRatio(1.0);
    const std::size_t backlog = ts.OutputBacklog();
    std::vector<s16> flushed(8192 * 2);
    const std::size_t rest = ts.FlushInto(flushed.data(), 8192);
    // At tempo 1 the residency comes out frame for frame, less the seek window the first
    // round starts within and the overlap trimmed off. SoundTouch's flush() would give a
    // third of it.
    REQUIRE(rest >= backlog + residency - ts.OverlapFrames() - ts.SeekFrames() - 16);
    REQUIRE(rest <= backlog + residency);
    REQUIRE(rest > (backlog + residency) / 2);
}

TEST_CASE("TimeStretcher::FlushInto keeps the audio's own trailing silence",
          "[audio_core][bypass]") {
    // A game silent on a load screen has buffered time in that silence; only the padding's
    // zeros come off, never a run that reaches down to where the audio could still be.
    AudioCore::TimeStretcher ts;
    ts.SetRatioBounds(1.0, 1.0);
    std::vector<s16> in = Tone(0, 4000);
    in.resize((4000 + 3000) * 2, 0);
    std::vector<s16> out(kCallback * 2);
    const std::size_t first = ts.Process(in.data(), 7000, out.data(), kCallback);
    std::vector<s16> flushed(12288 * 2);
    const std::size_t rest = ts.FlushInto(flushed.data(), 12288);
    REQUIRE(first + rest >= 7000 - ts.SeekFrames() - ts.OverlapFrames() - 16);
    REQUIRE(first + rest <= 7000);

    AudioCore::TimeStretcher silent;
    silent.SetRatioBounds(1.0, 1.0);
    const std::vector<s16> zeros(5000 * 2, 0);
    const std::size_t heard = silent.Process(zeros.data(), 5000, out.data(), kCallback);
    const std::size_t kept = silent.FlushInto(flushed.data(), 12288);
    REQUIRE(heard + kept >= 5000 - silent.SeekFrames() - silent.OverlapFrames() - 16);
}

TEST_CASE("TimeStretcher::CopyFedTail holds the last frames fed, not the flush's padding",
          "[audio_core][bypass]") {
    AudioCore::TimeStretcher ts;
    ts.SetRatioBounds(1.0, 1.0);
    const auto in = Tone(0, 6000);
    std::vector<s16> out(kCallback * 2);
    ts.Process(in.data(), 6000, out.data(), kCallback);
    std::vector<s16> flushed(12288 * 2);
    ts.FlushInto(flushed.data(), 12288);
    std::vector<s16> tail(AudioCore::TimeStretcher::kFedTailFrames * 2);
    const std::size_t got = ts.CopyFedTail(tail.data(), AudioCore::TimeStretcher::kFedTailFrames);
    REQUIRE(got == AudioCore::TimeStretcher::kFedTailFrames);
    const std::size_t from = 6000 - got;
    for (std::size_t i = 0; i < got * 2; i++) {
        REQUIRE(tail[i] == in[(from * 2) + i]);
    }
    ts.Clear();
    REQUIRE(ts.CopyFedTail(tail.data(), 16) == 0);
}

TEST_CASE("TimeStretcher::SetTargetBacklog steers the ratio toward the target",
          "[audio_core][bypass]") {
    AudioCore::TimeStretcher wants_more;
    AudioCore::TimeStretcher wants_less;
    wants_more.SetTargetBacklog(0.125);
    wants_less.SetTargetBacklog(static_cast<double>(kCallback) / AudioCore::native_sample_rate);
    const auto seed = Tone(0, 3000);
    wants_more.Feed(seed.data(), 3000);
    wants_less.Feed(seed.data(), 3000);
    std::size_t a = 3000;
    std::size_t b = 3000;
    Run(wants_more, 10, a);
    Run(wants_less, 10, b);
    REQUIRE(wants_more.Ratio() < 1.0);
    REQUIRE(wants_less.Ratio() > 1.0);
}

TEST_CASE("TimeStretcher::SetRatioBounds clamps the servo state", "[audio_core][bypass]") {
    AudioCore::TimeStretcher ts;
    ts.SetTargetBacklog(static_cast<double>(kCallback) / AudioCore::native_sample_rate);
    ts.SetRatioBounds(1.0, 1.1);
    const auto seed = Tone(0, 6000);
    ts.Feed(seed.data(), 6000);
    std::size_t frame = 6000;
    Run(ts, 20, frame);
    REQUIRE(ts.Ratio() >= 1.0);
    REQUIRE(ts.Ratio() <= 1.1 + 1e-9);
    // Lifting the bound moves the state from where it was clamped, not from where the servo
    // wanted to be.
    ts.SetRatioBounds(0.05, std::numeric_limits<double>::infinity());
    Run(ts, 1, frame);
    REQUIRE(ts.Ratio() < 1.2);
}

TEST_CASE("TimeStretcher copies input to output verbatim between overlaps",
          "[audio_core][bypass]") {
    // With SoundTouch's anti-alias filter off, the body of each round is a copy of the input,
    // which is what the pipeline test relies on to locate frames. Look for a 64-frame output
    // window in the input.
    AudioCore::TimeStretcher ts;
    ts.SetRatioBounds(1.0, 1.0);
    const auto in = Noise(8000, 3);
    std::vector<s16> out(2000 * 2);
    const std::size_t got = ts.Process(in.data(), 8000, out.data(), 2000);
    REQUIRE(got == 2000);
    const std::size_t start = 1000;
    bool found = false;
    for (std::size_t i = 0; i + 64 <= 8000 && !found; i++) {
        bool same = true;
        for (std::size_t k = 0; k < 64 * 2 && same; k++) {
            same = in[(i * 2) + k] == out[(start * 2) + k];
        }
        found = same;
    }
    REQUIRE(found);
}
