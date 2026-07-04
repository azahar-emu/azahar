// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version.
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>
#include "common/slot_vector.h"

TEST_CASE("SlotVector contains", "[common]") {
    Common::SlotVector<int> slots;
    const Common::SlotId invalid{};
    const Common::SlotId out_of_range{128};

    REQUIRE_FALSE(slots.contains(invalid));
    REQUIRE_FALSE(slots.contains(out_of_range));

    const Common::SlotId first = slots.insert(1);
    const Common::SlotId second = slots.insert(2);
    REQUIRE(slots.contains(first));
    REQUIRE(slots.contains(second));

    slots.erase(first);
    REQUIRE_FALSE(slots.contains(first));
    REQUIRE(slots.contains(second));
}
