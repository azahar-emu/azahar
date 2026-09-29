// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/frame_buffers.h"
#include "audio_core/period_splicer.h"
#include "common/common_types.h"

namespace {

constexpr std::size_t kPeriod = 100;
constexpr std::size_t kCallback = 512;

/// A sine with an exactly 100-frame period, built from i % 100 so every period is the same
/// rounded samples: cutting or repeating a whole period is then invisible to the sample.
std::vector<s16> Sine(std::size_t num_frames) {
    std::vector<s16> out(num_frames * 2);
    for (std::size_t i = 0; i < num_frames; i++) {
        const double phase = 2.0 * std::numbers::pi * static_cast<double>(i % kPeriod) /
                             static_cast<double>(kPeriod);
        const auto v = static_cast<s16>(std::lround(8000.0 * std::sin(phase)));
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

int MaxDiff(const std::vector<s16>& a, const std::vector<s16>& b, std::size_t num_frames) {
    int worst = 0;
    for (std::size_t i = 0; i < num_frames * 2; i++) {
        worst = std::max(worst, std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
    }
    return worst;
}

AudioCore::FrameStash Filled(const std::vector<s16>& frames) {
    AudioCore::FrameStash stash;
    stash.Append(frames.data(), frames.size() / 2);
    return stash;
}

} // namespace

TEST_CASE("PeriodSplicer::Cut removes one whole period of a tone invisibly",
          "[audio_core][bypass]") {
    const auto tone = Sine(4000);
    auto stash = Filled(tone);
    std::vector<s16> out(kCallback * 2);
    AudioCore::PeriodSplicer splicer;

    const auto r = splicer.Cut(out.data(), kCallback, stash, 300);
    REQUIRE(r.spliced == kPeriod);
    REQUIRE(r.written == kCallback);
    REQUIRE(r.consumed == kCallback + kPeriod);
    REQUIRE(stash.Size() == 4000 - kCallback - kPeriod);
    REQUIRE(MaxDiff(out, tone, kCallback) <= 1);
    // The stash now continues one period later, which for this tone is the same waveform.
    REQUIRE(stash.Data()[0] == tone[(kCallback + kPeriod) * 2]);
}

TEST_CASE("PeriodSplicer::Cut takes a period that fits the budget, or waits",
          "[audio_core][bypass]") {
    // A 150-frame tone: with a budget of 250 the cut is one whole period, not the
    // best-fitting multiple over the whole range, which would not fit; with a budget of
    // 120, under one period, nothing is cut rather than a phase step of up to 54 frames.
    constexpr std::size_t period = 150;
    std::vector<s16> tone(4000 * 2);
    for (std::size_t i = 0; i < 4000; i++) {
        const double phase =
            2.0 * std::numbers::pi * static_cast<double>(i % period) / static_cast<double>(period);
        const auto v = static_cast<s16>(std::lround(8000.0 * std::sin(phase)));
        tone[i * 2] = v;
        tone[(i * 2) + 1] = v;
    }
    AudioCore::PeriodSplicer splicer;
    std::vector<s16> out(kCallback * 2);
    auto fits = Filled(tone);
    const auto r = splicer.Cut(out.data(), kCallback, fits, 250);
    REQUIRE(r.spliced == period);
    auto waits = Filled(tone);
    const auto w = splicer.Cut(out.data(), kCallback, waits, 120);
    REQUIRE(w.spliced == 0);
    REQUIRE(w.written == kCallback);
    // Noise has no period to wait for: the best fit within the budget is cut.
    auto noisy = Filled(Noise(4000, 5));
    const auto n = splicer.Cut(out.data(), kCallback, noisy, 98);
    REQUIRE(n.spliced >= AudioCore::PeriodSplicer::kMinPeriod);
    REQUIRE(n.spliced <= 98);
}

TEST_CASE("PeriodSplicer::Cut never cuts more than the budget or a period",
          "[audio_core][bypass]") {
    const auto tone = Sine(4000);
    std::vector<s16> out(kCallback * 2);
    AudioCore::PeriodSplicer splicer;

    auto small = Filled(tone);
    const auto under =
        splicer.Cut(out.data(), kCallback, small, AudioCore::PeriodSplicer::kMinPeriod - 1);
    REQUIRE(under.spliced == 0);
    REQUIRE(under.consumed == kCallback);
    REQUIRE(MaxDiff(out, tone, kCallback) == 0);

    auto big = Filled(tone);
    const auto capped = splicer.Cut(out.data(), kCallback, big, 5000);
    REQUIRE(capped.spliced == kPeriod);
}

TEST_CASE("PeriodSplicer::Cut cuts silence outright", "[audio_core][bypass]") {
    const std::vector<s16> silence(4000 * 2, 0);
    auto stash = Filled(silence);
    std::vector<s16> out(kCallback * 2, 1);
    AudioCore::PeriodSplicer splicer;

    const auto r = splicer.Cut(out.data(), kCallback, stash, 500);
    REQUIRE(r.spliced == 500);
    REQUIRE(r.consumed == kCallback + 500);
    REQUIRE(out[0] == 0);
    REQUIRE(out[(kCallback * 2) - 1] == 0);
}

TEST_CASE("PeriodSplicer::Cut drops a silent lead-in without touching the onset",
          "[audio_core][bypass]") {
    // 300 frames of silence, then the tone from its peak (its sample 0 is itself silent): the
    // cut takes exactly the silence, and the tone starts at its own first sample with no fade.
    const auto tone = Sine(4000);
    const std::size_t onset = kPeriod / 4;
    std::vector<s16> stream(300 * 2, 0);
    stream.insert(stream.end(), tone.begin() + (onset * 2), tone.end());
    auto stash = Filled(stream);
    std::vector<s16> out(kCallback * 2);
    AudioCore::PeriodSplicer splicer;

    const auto r = splicer.Cut(out.data(), kCallback, stash, 1000);
    REQUIRE(r.spliced == 300);
    REQUIRE(r.written == kCallback);
    REQUIRE(r.consumed == kCallback + 300);
    const std::vector<s16> expected(tone.begin() + (onset * 2),
                                    tone.begin() + ((onset + kCallback) * 2));
    REQUIRE(MaxDiff(out, expected, kCallback) == 0);
}

TEST_CASE("PeriodSplicer::Cut takes only its budget of a longer silent lead-in",
          "[audio_core][bypass]") {
    // 600 frames of silence, then the tone from its peak, with a budget of 300: the cut takes
    // 300 of the silence and nothing else, so the tone still starts at its own first sample.
    const auto tone = Sine(4000);
    const std::size_t onset = kPeriod / 4;
    std::vector<s16> stream(600 * 2, 0);
    stream.insert(stream.end(), tone.begin() + (onset * 2), tone.end());
    auto stash = Filled(stream);
    std::vector<s16> out(kCallback * 2);
    AudioCore::PeriodSplicer splicer;

    const auto r = splicer.Cut(out.data(), kCallback, stash, 300);
    REQUIRE(r.spliced == 300);
    REQUIRE(r.written == kCallback);
    REQUIRE(r.consumed == kCallback + 300);
    for (std::size_t i = 0; i < 300 * 2; i++) {
        REQUIRE(out[i] == 0);
    }
    const std::vector<s16> expected(tone.begin() + (onset * 2),
                                    tone.begin() + ((onset + kCallback - 300) * 2));
    std::vector<s16> rest(out.begin() + (300 * 2), out.end());
    REQUIRE(MaxDiff(rest, expected, kCallback - 300) == 0);
}

TEST_CASE("PeriodSplicer::Cut copies a short stash without cutting", "[audio_core][bypass]") {
    const auto tone = Sine(300);
    auto stash = Filled(tone);
    std::vector<s16> out(kCallback * 2);
    AudioCore::PeriodSplicer splicer;

    const auto r = splicer.Cut(out.data(), kCallback, stash, 1000);
    REQUIRE(r.spliced == 0);
    REQUIRE(r.written == 300);
    REQUIRE(r.consumed == 300);
    REQUIRE(stash.Size() == 0);
    REQUIRE(MaxDiff(out, tone, 300) == 0);
}

TEST_CASE("PeriodSplicer::Insert repeats one whole period of a tone invisibly",
          "[audio_core][bypass]") {
    const auto tone = Sine(4000);
    auto stash = Filled(tone);
    std::vector<s16> out(kCallback * 2);
    AudioCore::PeriodSplicer splicer;

    const auto r = splicer.Insert(out.data(), kCallback, stash);
    REQUIRE(r.spliced == kPeriod);
    REQUIRE(r.written == kCallback);
    REQUIRE(r.consumed == kCallback - kPeriod);
    REQUIRE(stash.Size() == 4000 - kCallback + kPeriod);
    REQUIRE(MaxDiff(out, tone, kCallback) <= 1);
}

TEST_CASE("PeriodSplicer::Insert covers a stash short by less than a period",
          "[audio_core][bypass]") {
    // 450 frames for a 512-frame callback: repeating the 100-frame period leaves 412 to
    // consume, which the stash has.
    const auto tone = Sine(450);
    auto stash = Filled(tone);
    std::vector<s16> out(kCallback * 2);
    AudioCore::PeriodSplicer splicer;

    const auto r = splicer.Insert(out.data(), kCallback, stash);
    REQUIRE(r.spliced == kPeriod);
    REQUIRE(r.written == kCallback);
    REQUIRE(r.consumed == kCallback - kPeriod);
    REQUIRE(stash.Size() == 450 - (kCallback - kPeriod));
    const auto full = Sine(kCallback);
    REQUIRE(MaxDiff(out, full, kCallback) <= 1);
}

TEST_CASE("PeriodSplicer::Insert falls back to a plain copy", "[audio_core][bypass]") {
    AudioCore::PeriodSplicer splicer;
    std::vector<s16> out(kCallback * 2);

    // 300 frames: a 100-frame repeat would still need 412.
    const auto tone = Sine(300);
    auto shallow = Filled(tone);
    const auto r = splicer.Insert(out.data(), kCallback, shallow);
    REQUIRE(r.spliced == 0);
    REQUIRE(r.written == 300);
    REQUIRE(r.consumed == 300);
}

TEST_CASE("PeriodSplicer::Insert in silence inserts silence", "[audio_core][bypass]") {
    // No period to repeat, but repeating silence costs nothing and slows the raw path so a
    // warm-up in silence can still be overtaken: as much of the silent lead as the join
    // would have needed room for.
    AudioCore::PeriodSplicer splicer;
    std::vector<s16> out(kCallback * 2, 1);
    const std::vector<s16> silence(4000 * 2, 0);
    auto silent = Filled(silence);
    const auto s = splicer.Insert(out.data(), kCallback, silent);
    REQUIRE(s.written == kCallback);
    REQUIRE(s.spliced == kCallback - AudioCore::PeriodSplicer::kJoinFrames);
    REQUIRE(s.consumed == kCallback - s.spliced);
    REQUIRE(std::all_of(out.begin(), out.end(), [](s16 v) { return v == 0; }));

    // A silent lead shorter than a period is not worth a splice.
    std::vector<s16> onset = Sine(2000);
    onset.insert(onset.begin(), 2 * 40, 0);
    auto soon = Filled(onset);
    const auto r = splicer.Insert(out.data(), kCallback, soon);
    REQUIRE(r.written == kCallback);
    REQUIRE(r.spliced != kCallback - AudioCore::PeriodSplicer::kJoinFrames);
}

TEST_CASE("PeriodSplicer::JoinFlush finds the exact continuation in an overlapping tail",
          "[audio_core][bypass]") {
    // First stream: noise[0, 3000). Second: noise[2000, 6000), overlapping it by 1000. The
    // join must drop exactly the 1000 duplicated frames and leave the source continuous.
    const auto noise = Noise(6000, 11);
    std::vector<s16> stash_frames(noise.begin(), noise.begin() + (3000 * 2));
    stash_frames.insert(stash_frames.end(), noise.begin() + (2000 * 2), noise.end());
    auto stash = Filled(stash_frames);
    AudioCore::PeriodSplicer splicer;
    const std::size_t joined = splicer.JoinFlush(stash, 3000);
    REQUIRE(joined == 1000);
    REQUIRE(stash.Size() == 6000);
    std::vector<s16> got(stash.Data(), stash.Data() + (6000 * 2));
    REQUIRE(MaxDiff(got, noise, 6000) <= 1);
}

TEST_CASE("PeriodSplicer::JoinFlush declines when nothing continues the first stream",
          "[audio_core][bypass]") {
    const auto first = Noise(3000, 12);
    const auto other = Noise(4000, 13);
    std::vector<s16> stash_frames(first);
    stash_frames.insert(stash_frames.end(), other.begin(), other.end());
    auto stash = Filled(stash_frames);
    AudioCore::PeriodSplicer splicer;
    REQUIRE(splicer.JoinFlush(stash, 3000) == 0);
    REQUIRE(stash.Size() == 7000);

    // A first stream ending in silence has nothing to match either.
    std::vector<s16> quiet(first);
    std::fill(quiet.end() - (200 * 2), quiet.end(), 0);
    quiet.insert(quiet.end(), first.begin(), first.end());
    auto silent = Filled(quiet);
    REQUIRE(splicer.JoinFlush(silent, 3000) == 0);

    // A copy that lies past the reach is not found: noise then declines, and a tone, on
    // which any point in phase continues the first stream, joins within the reach only.
    const auto noise = Noise(6000, 14);
    std::vector<s16> far(noise.begin(), noise.begin() + (3000 * 2));
    far.insert(far.end(), noise.begin() + (2000 * 2), noise.end());
    auto out_of_reach = Filled(far);
    REQUIRE(splicer.JoinFlush(out_of_reach, 3000, 200) == 0);
    REQUIRE(out_of_reach.Size() == 7000);
    const auto tone = Sine(6000);
    std::vector<s16> far_tone(tone.begin(), tone.begin() + (3000 * 2));
    far_tone.insert(far_tone.end(), tone.begin() + (2000 * 2), tone.end());
    auto within = Filled(far_tone);
    const std::size_t joined = splicer.JoinFlush(within, 3000, 200);
    REQUIRE(joined <= 200);
    REQUIRE(within.Size() == 7000 - joined);
}

TEST_CASE("PeriodSplicer::Insert on noise repeats a period that fits the join",
          "[audio_core][bypass]") {
    const auto noise = Noise(4000, 7);
    auto stash = Filled(noise);
    std::vector<s16> out(kCallback * 2);
    AudioCore::PeriodSplicer splicer;

    const auto r = splicer.Insert(out.data(), kCallback, stash);
    REQUIRE(r.spliced >= AudioCore::PeriodSplicer::kMinPeriod);
    REQUIRE(r.spliced <= kCallback - AudioCore::PeriodSplicer::kJoinFrames);
    REQUIRE(r.written == kCallback);
    REQUIRE(r.consumed == kCallback - r.spliced);
    // The frames before the repeat are the source, verbatim.
    REQUIRE(MaxDiff(out, noise, r.spliced) == 0);
}
