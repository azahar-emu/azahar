// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>
#include "audio_core/stretch_gate.h"

namespace {

using AudioCore::StretchGate;
using Edge = StretchGate::Edge;
using Mode = StretchGate::Mode;

constexpr std::size_t kCallback = 512;

StretchGate::Input In(double speed) {
    StretchGate::Input in{};
    in.speed = speed;
    in.speed_settled = true;
    in.speed_fast = speed;
    in.buffered = 4000;
    in.low_water = kCallback;
    in.stretched = 4000;
    in.handover_low = 2000;
    in.handover_high = 2600;
    in.ratio = 1.0;
    in.enabled = true;
    in.prefilling = false;
    in.silenced = false;
    in.synced = false;
    in.num_frames = kCallback;
    return in;
}

/// Updates `calls` times with the same input; returns the last edge.
Edge Repeat(StretchGate& gate, const StretchGate::Input& in, std::size_t calls) {
    Edge last = Edge::None;
    for (std::size_t c = 0; c < calls; c++) {
        last = gate.Update(in);
    }
    return last;
}

/// Drives a fresh gate into Stretch.
StretchGate Stretching() {
    StretchGate gate;
    REQUIRE(gate.Update(In(0.5)) == Edge::Engage);
    auto in = In(0.5);
    in.synced = true;
    REQUIRE(gate.Update(in) == Edge::Sync);
    REQUIRE(gate.CurrentMode() == Mode::Stretch);
    return gate;
}

} // namespace

TEST_CASE("StretchGate stays in Bypass at full speed", "[audio_core][bypass]") {
    StretchGate gate;
    REQUIRE(Repeat(gate, In(1.0), 1000) == Edge::None);
    REQUIRE(gate.CurrentMode() == Mode::Bypass);
    REQUIRE(Repeat(gate, In(0.96), 100) == Edge::None);
    REQUIRE(Repeat(gate, In(1.04), 100) == Edge::None);
}

TEST_CASE("StretchGate engages on slow, fast, or an empty buffer", "[audio_core][bypass]") {
    SECTION("slow") {
        StretchGate gate;
        REQUIRE(gate.Update(In(0.94)) == Edge::Engage);
        REQUIRE(gate.CurrentMode() == Mode::Warming);
    }
    SECTION("fast") {
        StretchGate gate;
        REQUIRE(gate.Update(In(1.06)) == Edge::Engage);
        REQUIRE(gate.CurrentMode() == Mode::Warming);
    }
    SECTION("low water") {
        StretchGate gate;
        auto in = In(1.0);
        in.buffered = kCallback - 1;
        REQUIRE(gate.Update(in) == Edge::Engage);
        REQUIRE(gate.CurrentMode() == Mode::Warming);
    }
    SECTION("fast estimate well below full, before the slow one moves") {
        StretchGate gate;
        auto in = In(1.0);
        in.speed_fast = 0.89;
        REQUIRE(gate.Update(in) == Edge::Engage);
    }
    SECTION("fast estimate well above full, as fast-forward reads within a tenth of a second") {
        StretchGate gate;
        auto in = In(1.0);
        in.speed_fast = 1.11;
        REQUIRE(gate.Update(in) == Edge::Engage);
    }
    SECTION("a burst gap's dip in the fast estimate is not a slowdown") {
        StretchGate gate;
        auto in = In(1.0);
        in.speed_fast = 0.94;
        REQUIRE(Repeat(gate, in, 100) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Bypass);
    }
    SECTION("low water while prefilling is not an underrun") {
        StretchGate gate;
        auto in = In(1.0);
        in.buffered = 0;
        in.prefilling = true;
        REQUIRE(Repeat(gate, in, 10) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Bypass);
    }
}

TEST_CASE("StretchGate never engages with stretching off", "[audio_core][bypass]") {
    StretchGate gate;
    auto in = In(0.5);
    in.enabled = false;
    in.buffered = 0;
    REQUIRE(Repeat(gate, in, 100) == Edge::None);
    REQUIRE(gate.CurrentMode() == Mode::Bypass);
}

TEST_CASE("StretchGate syncs out of Warming when told, or on the timeout", "[audio_core][bypass]") {
    SECTION("synced") {
        StretchGate gate;
        REQUIRE(gate.Update(In(0.9)) == Edge::Engage);
        REQUIRE(Repeat(gate, In(0.9), 5) == Edge::None);
        auto in = In(0.9);
        in.synced = true;
        REQUIRE(gate.Update(in) == Edge::Sync);
        REQUIRE(gate.CurrentMode() == Mode::Stretch);
    }
    SECTION("timeout") {
        StretchGate gate;
        REQUIRE(gate.Update(In(0.9)) == Edge::Engage);
        // 32728 frames of warm-up at 512 per callback: the 64th callback crosses it.
        REQUIRE(Repeat(gate, In(0.9), 63) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Warming);
        REQUIRE(gate.Update(In(0.9)) == Edge::Sync);
        REQUIRE(gate.CurrentMode() == Mode::Stretch);
    }
    SECTION("aborted when stretching is turned off") {
        StretchGate gate;
        REQUIRE(gate.Update(In(0.9)) == Edge::Engage);
        auto in = In(0.9);
        in.enabled = false;
        REQUIRE(gate.Update(in) == Edge::Abort);
        REQUIRE(gate.CurrentMode() == Mode::Bypass);
    }
}

TEST_CASE("StretchGate drains after two seconds in band with the ratio near one",
          "[audio_core][bypass]") {
    SECTION("in band") {
        StretchGate gate = Stretching();
        // 65456 frames of dwell at 512 per callback: the 128th in-band callback crosses it.
        REQUIRE(Repeat(gate, In(1.0), 127) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Stretch);
        REQUIRE(gate.Update(In(1.0)) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Drain);
    }
    SECTION("a slight deficit keeps it stretching, since Bypass cannot cover one") {
        StretchGate gate = Stretching();
        // The most the slow estimate can read at 99.5%, one burst over its window high.
        REQUIRE(Repeat(gate, In(0.9967), 300) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Stretch);
        // The least it can read at true full speed, one burst low.
        REQUIRE(Repeat(gate, In(0.9983), 128) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Drain);
    }
    SECTION("an unsettled slow estimate does not count toward the dwell") {
        StretchGate gate = Stretching();
        auto in = In(1.0);
        in.speed_settled = false;
        REQUIRE(Repeat(gate, in, 300) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Stretch);
    }
    SECTION("ratio off keeps it stretching") {
        StretchGate gate = Stretching();
        auto in = In(1.0);
        in.ratio = 1.05;
        REQUIRE(Repeat(gate, in, 300) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Stretch);
    }
    SECTION("leaving the band restarts the dwell") {
        StretchGate gate = Stretching();
        REQUIRE(Repeat(gate, In(1.0), 100) == Edge::None);
        REQUIRE(Repeat(gate, In(0.97), 1) == Edge::None);
        REQUIRE(Repeat(gate, In(1.0), 127) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Stretch);
        REQUIRE(gate.Update(In(1.0)) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Drain);
    }
}

TEST_CASE("StretchGate hands over when the flush would land within the band",
          "[audio_core][bypass]") {
    StretchGate gate = Stretching();
    Repeat(gate, In(1.0), 128);
    REQUIRE(gate.CurrentMode() == Mode::Drain);

    SECTION("not while the stretcher holds more than Bypass wants") {
        REQUIRE(Repeat(gate, In(1.0), 50) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Drain);
    }
    SECTION("not while it holds less than Bypass needs to start from") {
        auto in = In(1.0);
        in.stretched = 1500;
        in.ratio = 1.0;
        REQUIRE(Repeat(gate, in, 5) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Drain);
    }
    SECTION("not while the ratio is outside the drain bounds") {
        auto in = In(1.0);
        in.stretched = 2200;
        in.ratio = 0.9;
        REQUIRE(Repeat(gate, in, 5) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Drain);
    }
    SECTION("content in the band, ratio in bounds") {
        auto in = In(1.0);
        in.stretched = 2200;
        in.ratio = 1.05;
        REQUIRE(gate.Update(in) == Edge::Handover);
        REQUIRE(gate.CurrentMode() == Mode::Bypass);
    }
    SECTION("speed leaving the band returns to Stretch") {
        REQUIRE(gate.Update(In(0.9)) == Edge::None);
        REQUIRE(gate.CurrentMode() == Mode::Stretch);
    }
}

TEST_CASE("StretchGate drains and hands over when stretching is turned off, at any speed",
          "[audio_core][bypass]") {
    StretchGate gate = Stretching();
    auto in = In(0.5);
    in.enabled = false;
    REQUIRE(gate.Update(in) == Edge::None);
    REQUIRE(gate.CurrentMode() == Mode::Drain);
    // Speed far outside the band does not revert a forced drain.
    REQUIRE(Repeat(gate, in, 20) == Edge::None);
    REQUIRE(gate.CurrentMode() == Mode::Drain);
    // A forced drain hands over with whatever is left, however little.
    in.stretched = 10;
    in.ratio = 1.0;
    REQUIRE(gate.Update(in) == Edge::Handover);
    REQUIRE(gate.CurrentMode() == Mode::Bypass);
    // And with it off, Bypass is permanent.
    in.buffered = 0;
    REQUIRE(Repeat(gate, in, 100) == Edge::None);
    REQUIRE(gate.CurrentMode() == Mode::Bypass);
}

TEST_CASE("StretchGate is frozen while silenced", "[audio_core][bypass]") {
    StretchGate gate;
    auto in = In(0.5);
    in.silenced = true;
    REQUIRE(Repeat(gate, in, 50) == Edge::None);
    REQUIRE(gate.CurrentMode() == Mode::Bypass);

    StretchGate warming;
    REQUIRE(warming.Update(In(0.9)) == Edge::Engage);
    auto quiet = In(0.9);
    quiet.silenced = true;
    // A silence longer than the warm-up timeout does not count toward it.
    REQUIRE(Repeat(warming, quiet, 100) == Edge::None);
    REQUIRE(warming.CurrentMode() == Mode::Warming);
}

TEST_CASE("StretchGate::Reset returns to Bypass", "[audio_core][bypass]") {
    StretchGate gate = Stretching();
    gate.Reset();
    REQUIRE(gate.CurrentMode() == Mode::Bypass);
}
