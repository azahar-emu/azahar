// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/period_finder.h"

TEST_CASE("FindPeriod picks the shortest period of a periodic signal", "[audio_core][bypass]") {
    // A sawtooth with a 100-frame period also matches at 200 and 300; the shortest wins.
    std::vector<float> sum(4096);
    for (std::size_t i = 0; i < sum.size(); i++) {
        sum[i] = static_cast<float>(i % 100) - 50.0f;
    }
    const unsigned p =
        AudioCore::FindPeriod([&](unsigned k) { return sum[k]; }, 4096, 96, 1024, 128);
    REQUIRE(p == 100);
}

TEST_CASE("FindPeriod reports no period for silence or too little data", "[audio_core][bypass]") {
    std::vector<float> zeros(4096, 0.0f);
    REQUIRE(AudioCore::FindPeriod([&](unsigned k) { return zeros[k]; }, 4096, 96, 1024, 128) == 0);
    std::vector<float> saw(4096);
    for (std::size_t i = 0; i < saw.size(); i++) {
        saw[i] = static_cast<float>(i % 100) - 50.0f;
    }
    // 200 frames cannot hold a 96-frame candidate plus a 128-frame window.
    REQUIRE(AudioCore::FindPeriod([&](unsigned k) { return saw[k]; }, 200, 96, 1024, 128) == 0);
}

TEST_CASE("FindPeriod never reads past avail", "[audio_core][bypass]") {
    // 300 frames: candidates up to 300 - 128 = 172 are searched, and 100 still wins.
    std::vector<float> saw(300);
    for (std::size_t i = 0; i < saw.size(); i++) {
        saw[i] = static_cast<float>(i % 100) - 50.0f;
    }
    unsigned max_seen = 0;
    const unsigned p = AudioCore::FindPeriod(
        [&](unsigned k) {
            max_seen = std::max(max_seen, k);
            return saw.at(k);
        },
        300, 96, 1024, 128);
    REQUIRE(p == 100);
    REQUIRE(max_seen < 300);
}
