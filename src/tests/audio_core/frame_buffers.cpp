// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/frame_buffers.h"
#include "common/common_types.h"
#include "common/ring_buffer.h"

namespace {

/// Frame i carries i on the left and -i on the right, so a frame identifies itself.
std::vector<s16> Frames(std::size_t first, std::size_t count) {
    std::vector<s16> out(count * 2);
    for (std::size_t i = 0; i < count; i++) {
        out[i * 2] = static_cast<s16>(first + i);
        out[(i * 2) + 1] = static_cast<s16>(-static_cast<int>(first + i));
    }
    return out;
}

} // namespace

TEST_CASE("FrameStash pulls from a FIFO up to what it wants", "[audio_core][bypass]") {
    Common::RingBuffer<s16, 0x2000, 2> fifo;
    const auto in = Frames(0, 3000);
    fifo.Push(in.data(), 3000);

    AudioCore::FrameStash stash;
    REQUIRE(stash.PullFrom(fifo, 1000) == 1000);
    REQUIRE(stash.Size() == 1000);
    REQUIRE(fifo.Size() == 2000);
    REQUIRE(stash.PullFrom(fifo, 1000) == 0);
    REQUIRE(stash.PullFrom(fifo, 5000) == 2000);
    REQUIRE(stash.Size() == 3000);
    REQUIRE(stash.Data()[0] == 0);
    REQUIRE(stash.Data()[2999 * 2] == 2999);
}

TEST_CASE("FrameStash consumes from the front and compacts to keep room", "[audio_core][bypass]") {
    AudioCore::FrameStash stash;
    const auto a = Frames(0, 10000);
    REQUIRE(stash.Append(a.data(), 10000) == 10000);
    stash.Consume(9000);
    REQUIRE(stash.Size() == 1000);
    REQUIRE(stash.Data()[0] == 9000);
    // 1000 held plus 11288 more is the full capacity of 12288: only possible after compaction.
    const auto b = Frames(10000, 11288);
    REQUIRE(stash.Append(b.data(), 11288) == 11288);
    REQUIRE(stash.Size() == AudioCore::FrameStash::kCapacity);
    REQUIRE(stash.Data()[0] == 9000);
    REQUIRE(stash.Data()[(stash.Size() - 1) * 2] == 21287);
    REQUIRE(stash.Append(b.data(), 1) == 0);
    stash.Consume(stash.Size() + 5);
    REQUIRE(stash.Size() == 0);
}

TEST_CASE("FrameHistory hands back the most recent frames across its wrap",
          "[audio_core][bypass]") {
    AudioCore::FrameHistory history;
    REQUIRE(history.Size() == 0);
    for (std::size_t first = 0; first < 10000; first += 700) {
        const auto chunk = Frames(first, 700);
        history.Record(chunk.data(), 700);
    }
    // 15 chunks of 700 = 10500 recorded; the ring keeps the last 8192.
    REQUIRE(history.Size() == AudioCore::FrameHistory::kCapacity);
    std::vector<s16> out(3000 * 2);
    REQUIRE(history.Last(3000, out.data()) == 3000);
    REQUIRE(out[0] == 7500);
    REQUIRE(out[2999 * 2] == 10499);
    REQUIRE(out[(2999 * 2) + 1] == -10499);
    // A record longer than the ring keeps its tail.
    const auto big = Frames(0, 9000);
    history.Record(big.data(), 9000);
    REQUIRE(history.Last(1, out.data()) == 1);
    REQUIRE(out[0] == 8999);
    history.Clear();
    REQUIRE(history.Last(10, out.data()) == 0);
}
